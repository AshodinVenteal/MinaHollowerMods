from __future__ import annotations

import base64
import os
import re
import struct
from pathlib import Path


MOD_ROOT = Path(__file__).resolve().parents[1]
SOURCE = (
    Path(os.environ["APPDATA"])
    / "Yacht Club Games"
    / "Mina the Hollower"
    / "mods"
    / "unpak"
    / "data"
    / "levels"
    / "extra"
    / "checkpoint.tlb.yc"
)
DESTINATION = MOD_ROOT / "data" / "levels" / "extra" / "checkpoint.tlb.yc"

DIVIDER_LEFT = 2808
DIVIDER_RIGHT = 2888
RETAIL_EXPANSION_LEFT = 2848
RETAIL_EXPANSION_RIGHT = 3104
ANNEX_LEFT = RETAIL_EXPANSION_LEFT
ANNEX_RIGHT = RETAIL_EXPANSION_RIGHT
ANNEX_TOP = 2432
ANNEX_BOTTOM = 2560
ANNEX_CENTER_X = 2976
ANNEX_CENTER_Y = 2496
ANNEX_SHIFT_Y = ANNEX_TOP - 2240
TUNNEL_TOP = 2280
TUNNEL_BOTTOM = 2312

SOURCE_LEFT = RETAIL_EXPANSION_LEFT
SOURCE_TOP = 2240
ROOM_SIZE = 256
COPY_LAYERS = {"GROUND", "DETAIL", "WALL", "FENCE"}


def decompress_wflz(data: bytes) -> bytes:
    if data[:4] != b"WFLZ":
        raise ValueError("missing WFLZ signature")
    expected_size = struct.unpack_from("<I", data, 8)[0]
    _, _, literal_count = struct.unpack_from("<HBB", data, 12)
    cursor = 16
    output = bytearray(data[cursor : cursor + literal_count])
    cursor += literal_count
    while True:
        distance, length, literal_count = struct.unpack_from("<HBB", data, cursor)
        cursor += 4
        if distance == 0 and length == 0 and literal_count == 0:
            break
        if length:
            start = len(output) - distance
            for index in range(length + 4):
                output.append(output[start + index])
        output.extend(data[cursor : cursor + literal_count])
        cursor += literal_count
    if len(output) != expected_size:
        raise ValueError(f"decoded {len(output)} bytes, expected {expected_size}")
    return bytes(output)


def encode_wflz_literals(raw: bytes) -> bytes:
    output = bytearray(b"WFLZ")
    output.extend(b"\0" * 8)
    cursor = 0
    first = raw[:255]
    output.extend(struct.pack("<HBB", 0, 0, len(first)))
    output.extend(first)
    cursor += len(first)
    while cursor < len(raw):
        chunk = raw[cursor : cursor + 255]
        output.extend(struct.pack("<HBB", 0, 0, len(chunk)))
        output.extend(chunk)
        cursor += len(chunk)
    output.extend(struct.pack("<HBB", 0, 0, 0))
    struct.pack_into("<I", output, 4, len(output) - 16)
    struct.pack_into("<I", output, 8, len(raw))
    return bytes(output)


def find_braced_block(text: str, token_start: int) -> tuple[int, int]:
    brace_start = text.index("{", token_start)
    depth = 0
    in_string = False
    escaped = False
    cursor = brace_start
    while cursor < len(text):
        char = text[cursor]
        if in_string:
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == '"':
                in_string = False
        elif char == '"':
            in_string = True
        elif char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return token_start, cursor + 1
        cursor += 1
    raise ValueError("unterminated YCD block")


def copy_room_in_layer(block: str, layer_name: str) -> str:
    if layer_name not in COPY_LAYERS:
        return block

    position = re.search(
        r"m_position:\s*ycVec2\s*\{.*?x:\s*([-\d.]+),.*?y:\s*([-\d.]+),",
        block,
        re.DOTALL,
    )
    dimensions = re.search(
        r"m_widthInTiles:\s*(\d+),.*?m_heightInTiles:\s*(\d+),", block, re.DOTALL
    )
    chunks = re.search(
        r"m_chunkStart:\s*\[\s*\(\s*Reserve:\s*(\d+)\s*\)(.*?)\],", block, re.DOTALL
    )
    blob = re.search(
        r"(m_tileData:\s*ycDataBlob\s*\{\s*size:\s*)(\d+)(,.*?data:\s*\")([^\"]*)(\")",
        block,
        re.DOTALL,
    )
    if not all((position, dimensions, chunks, blob)):
        raise ValueError(f"{layer_name} layer metadata is incomplete")

    layer_x, layer_y = (int(float(value)) for value in position.groups())
    width, height = (int(value) for value in dimensions.groups())
    reserve = int(chunks.group(1))
    parts = chunks.group(2).split(",")[:reserve]
    starts: list[int | None] = [
        None if part.strip() == "4294967295" else int(part.strip() or 0) for part in parts
    ]
    if len(starts) != reserve:
        raise ValueError(f"{layer_name} chunk table is truncated")

    raw = bytearray(decompress_wflz(base64.b64decode(blob.group(4))))
    chunks_wide = (width + 15) // 16

    def read_tile(world_x: int, world_y: int) -> int:
        tile_x = (world_x - layer_x) // 8
        tile_y = (world_y - layer_y) // 8
        if tile_x < 0 or tile_y < 0 or tile_x >= width or tile_y >= height:
            return 0
        chunk_index = (tile_y // 16) * chunks_wide + tile_x // 16
        within_chunk = (tile_y % 16) * 16 + tile_x % 16
        chunk_start = starts[chunk_index]
        if chunk_start is None:
            return 0
        return struct.unpack_from("<H", raw, chunk_start * 2 + within_chunk * 2)[0]

    def tile_location(world_x: int, world_y: int) -> tuple[int, int, int] | None:
        tile_x = (world_x - layer_x) // 8
        tile_y = (world_y - layer_y) // 8
        if tile_x < 0 or tile_y < 0 or tile_x >= width or tile_y >= height:
            return None
        chunk_index = (tile_y // 16) * chunks_wide + tile_x // 16
        within_chunk = (tile_y % 16) * 16 + tile_x % 16
        return chunk_index, within_chunk, tile_x

    def read_tile(world_x: int, world_y: int) -> int:
        location = tile_location(world_x, world_y)
        if location is None:
            return 0
        chunk_index, within_chunk, _ = location
        chunk_start = starts[chunk_index]
        if chunk_start is None:
            return 0
        return struct.unpack_from("<H", raw, chunk_start * 2 + within_chunk * 2)[0]

    def write_tile(world_x: int, world_y: int, value: int) -> None:
        location = tile_location(world_x, world_y)
        if location is None:
            raise ValueError(f"annex falls outside {layer_name} at ({world_x},{world_y})")
        chunk_index, within_chunk, _ = location
        chunk_start = starts[chunk_index]
        if chunk_start is None:
            chunk_start = len(raw) // 2
            starts[chunk_index] = chunk_start
            raw.extend(b"\0" * (16 * 16 * 2))
        struct.pack_into("<H", raw, chunk_start * 2 + within_chunk * 2, value)

    source_tiles = [
        [read_tile(SOURCE_LEFT + column * 8, SOURCE_TOP + row * 8) for column in range(32)]
        for row in range(32)
    ]
    for row in range(32):
        for column in range(32):
            value = source_tiles[row][column]
            if 4 <= row < 28 and 4 <= column < 28:
                if layer_name == "GROUND":
                    value = 0x0A2A
                elif layer_name == "DETAIL":
                    value = 0
            write_tile(ANNEX_LEFT + column * 8, ANNEX_TOP + row * 8, value)

    chunk_values = ["4294967295" if value is None else str(value) for value in starts]
    chunk_text = f"m_chunkStart: [ ( Reserve: {len(starts)} ) " + ", ".join(chunk_values) + " ],"
    block = block[: chunks.start()] + chunk_text + block[chunks.end() :]

    replacement_blob = encode_wflz_literals(bytes(raw))
    replacement_b64 = base64.b64encode(replacement_blob).decode("ascii")
    blob = re.search(
        r"(m_tileData:\s*ycDataBlob\s*\{\s*size:\s*)(\d+)(,.*?data:\s*\")([^\"]*)(\")",
        block,
        re.DOTALL,
    )
    assert blob is not None
    return (
        block[: blob.start()]
        + blob.group(1)
        + str(len(replacement_blob))
        + blob.group(3)
        + replacement_b64
        + blob.group(5)
        + block[blob.end() :]
    )


def copy_annex_room(text: str) -> str:
    layers_start = text.index("\tm_tileLayers:")
    entities_start = text.index("\tm_entities:", layers_start)
    cursor = layers_start
    replacements: list[tuple[int, int, str]] = []
    while True:
        token_start = text.find("ycTileLevel2TileLayer", cursor, entities_start)
        if token_start < 0:
            break
        block_start, block_end = find_braced_block(text, token_start)
        block = text[block_start:block_end]
        name = re.search(r'm_name:\s*"([^\"]+)",', block)
        if name and name.group(1) in COPY_LAYERS:
            replacements.append((block_start, block_end, copy_room_in_layer(block, name.group(1))))
        cursor = block_end
    for block_start, block_end, replacement in reversed(replacements):
        text = text[:block_start] + replacement + text[block_end:]
    return text


def warp_entity(label: str, x: int, y: int, level_hash: int, name_hash: int) -> str:
    return f'''ycTileLevel2Entity
        {{
            m_props: [ ( Reserve: 3 )
                ycTileLevel2Property
                {{
                    m_nameHash: 577210278,
                    m_value: "{label}",
                }}, ycTileLevel2Property
                {{
                    m_nameHash: 1933903777,
                    m_value: "RoomTransition",
                }}, ycTileLevel2Property
                {{
                    m_nameHash: 947929513,
                    m_value: "BurrowHole",
                }} ],
            m_position: ycVec2
            {{
                x: {x},
                y: {y},
            }},
            m_scale: ycVec2
            {{
                x: 1,
                y: 1,
            }},
            m_rotation: -0,
            m_nameLevelHash: {level_hash},
            m_nameHash: {name_hash},
            m_layerNameHash: 3706456149,
            m_entityType: 64,
            m_tileLayerIndex: 255,
            m_spawnType: kTileLevel2EntitySpawnType_RoomTransition,
        }}'''


def tunnel_sprite_entity(x: int, y: int, scale_x: int, level_hash: int) -> str:
    sequence = "hub"
    return f'''ycTileLevel2Entity
        {{
            m_props: [ ( Reserve: 4 )
                ycTileLevel2Property
                {{
                    m_nameHash: 1410415984,
                    m_value: "levels/tilesets/hub/animTiles/doorway.anb.yc",
                }}, ycTileLevel2Property
                {{
                    m_nameHash: 2401088400,
                    m_value: "true",
                }}, ycTileLevel2Property
                {{
                    m_nameHash: 3026379260,
                    m_value: "{sequence}",
                }}, ycTileLevel2Property
                {{
                    m_nameHash: 4271143480,
                    m_value: "10",
                }} ],
            m_position: ycVec2
            {{
                x: {x},
                y: {y},
            }},
            m_scale: ycVec2
            {{
                x: {scale_x},
                y: 1,
            }},
            m_nameLevelHash: {level_hash},
            m_layerNameHash: 3792921557,
            m_entityType: 48,
            m_tileLayerIndex: 70,
            m_spawnType: kTileLevel2EntitySpawnType_RoomViewInfinite,
        }}'''


def annex_sprite_entity(
    asset: str,
    x: float,
    y: float,
    level_hash: int,
    name_hash: int,
    spawn_type: str = "PersistentA",
    tile_layer_index: int = 70,
    scale: float = 1.0,
) -> str:
    return f'''ycTileLevel2Entity
        {{
            m_props: [ ( Reserve: 4 )
                ycTileLevel2Property
                {{
                    m_nameHash: 1410415984,
                    m_value: "levels/tilesets/underlabTrainingAnnex/animTiles/{asset}.anb.yc",
                }}, ycTileLevel2Property
                {{
                    m_nameHash: 2401088400,
                    m_value: "true",
                }}, ycTileLevel2Property
                {{
                    m_nameHash: 3026379260,
                    m_value: "idle",
                }}, ycTileLevel2Property
                {{
                    m_nameHash: 4271143480,
                    m_value: "10",
                }} ],
            m_position: ycVec2
            {{
                x: {x:g},
                y: {y:g},
            }},
            m_scale: ycVec2
            {{
                x: {scale:g},
                y: {scale:g},
            }},
            m_rotation: -0,
            m_nameLevelHash: {level_hash},
            m_nameHash: {name_hash},
            m_layerNameHash: 3792921557,
            m_entityType: 48,
            m_tileLayerIndex: {tile_layer_index},
            m_spawnType: kTileLevel2EntitySpawnType_{spawn_type},
        }}'''


def annex_region_entity() -> str:
    return '''ycTileLevel2Entity
        {
            m_props: [ ( Reserve: 1 )
                ycTileLevel2Property
                {
                    m_nameHash: 2673678086,
                    m_value: "Test_2",
                } ],
            m_position: ycVec2
            {
                x: 2976,
                y: 2496,
            },
            m_scale: ycVec2
            {
                x: 256,
                y: 128,
            },
            m_rotation: -0,
            m_nameLevelHash: 15191824161433712303,
            m_nameHash: 3987441303,
            m_layerNameHash: 4200755758,
            m_entityType: 91,
            m_tileLayerIndex: 255,
            m_spawnType: kTileLevel2EntitySpawnType_Persistent,
            m_shapeType: kTileLevel2EntityShapeType_Rect,
        }'''


def native_water_anim_entity(sequence: str, x: int, level_hash: int) -> str:
    name_hash = 3987441500 + (x - 2952) // 16
    return f'''ycTileLevel2Entity
        {{
            m_props: [ ( Reserve: 4 )
                ycTileLevel2Property
                {{
                    m_nameHash: 1410415984,
                    m_value: "levels/tilesets/checkpointRoom/animTiles/healingWater.anb.yc",
                }}, ycTileLevel2Property
                {{
                    m_nameHash: 1657031253,
                    m_value: "true",
                }}, ycTileLevel2Property
                {{
                    m_nameHash: 1162648605,
                    m_value: "palettes/environments/healingWater_default.pal.yc",
                }}, ycTileLevel2Property
                {{
                    m_nameHash: 3026379260,
                    m_value: "{sequence}",
                }} ],
            m_position: ycVec2
            {{
                x: {x},
                y: 2552,
            }},
            m_scale: ycVec2
            {{
                x: 1,
                y: 1,
            }},
            m_rotation: -0,
            m_nameLevelHash: {level_hash},
            m_nameHash: {name_hash},
            m_layerNameHash: 1384675405,
            m_entityType: 50,
            m_tileLayerIndex: 77,
            m_spawnType: kTileLevel2EntitySpawnType_PersistentA,
        }}'''


def native_training_dummy_entity(
    x: int,
    y: int,
    variant: int,
    level_hash: int,
    name_hash: int,
    scale_x: int = 1,
) -> str:
    flying_prop = "" if variant == 1 else '''ycTileLevel2Property
                {
                    m_nameHash: 1389856868,
                }, '''
    reserve = 1 if variant == 1 else 2
    return f'''ycTileLevel2Entity
        {{
            m_props: [ ( Reserve: {reserve} )
                {flying_prop}ycTileLevel2Property
                {{
                    m_nameHash: 1006933746,
                    m_value: "{variant}",
                }} ],
            m_position: ycVec2
            {{
                x: {x},
                y: {y},
            }},
            m_scale: ycVec2
            {{
                x: {scale_x},
                y: 1,
            }},
            m_rotation: -0,
            m_nameLevelHash: {level_hash},
            m_nameHash: {name_hash},
            m_layerNameHash: 3706456149,
            m_entityType: 463,
            m_tileLayerIndex: 255,
            m_spawnType: kTileLevel2EntitySpawnType_RoomInfinite,
        }}'''


def native_training_dummy_art_entity(
    asset: str,
    x: int,
    y: int,
    level_hash: int,
    name_hash: int,
    scale_x: int = 1,
) -> str:
    """Persistent retail dummy animation, independent of upgrade gating."""
    return f'''ycTileLevel2Entity
        {{
            m_props: [ ( Reserve: 4 )
                ycTileLevel2Property
                {{
                    m_nameHash: 1410415984,
                    m_value: "objects/{asset}.anb.yc",
                }}, ycTileLevel2Property
                {{
                    m_nameHash: 2401088400,
                    m_value: "true",
                }}, ycTileLevel2Property
                {{
                    m_nameHash: 3026379260,
                    m_value: "idle",
                }}, ycTileLevel2Property
                {{
                    m_nameHash: 4271143480,
                    m_value: "10",
                }} ],
            m_position: ycVec2
            {{
                x: {x},
                y: {y},
            }},
            m_scale: ycVec2
            {{
                x: {scale_x},
                y: 1,
            }},
            m_rotation: -0,
            m_nameLevelHash: {level_hash},
            m_nameHash: {name_hash},
            m_layerNameHash: 3792921557,
            m_entityType: 48,
            m_tileLayerIndex: 70,
            m_spawnType: kTileLevel2EntitySpawnType_PersistentA,
        }}'''


ANNEX_REGION_ENTITY = f'''ycTileLevel2Entity
        {{
            m_props: [ ( Reserve: 1 )
                ycTileLevel2Property
                {{
                    m_nameHash: 2673678086,
                    m_value: "Hub_10",
                }} ],
            m_position: ycVec2
            {{
                x: {ANNEX_CENTER_X},
                y: {ANNEX_CENTER_Y},
            }},
            m_scale: ycVec2
            {{
                x: {ROOM_SIZE},
                y: {ROOM_SIZE},
            }},
            m_rotation: -0,
            m_nameLevelHash: 15191824161433712061,
            m_nameHash: 3987441191,
            m_layerNameHash: 4200755758,
            m_entityType: 91,
            m_tileLayerIndex: 255,
            m_spawnType: kTileLevel2EntitySpawnType_Persistent,
            m_shapeType: kTileLevel2EntityShapeType_Rect,
        }}'''


def append_native_room_entities(text: str) -> str:
    entities_start = text.index("\tm_entities:")
    markers_start = text.index("\tm_markers:", entities_start)
    section = text[entities_start:markers_start]
    entities = [
        ANNEX_REGION_ENTITY,
        warp_entity("underlabTrainingTunnel_0a", 2664, 1000, 15191824161433712062, 3987441192),
        warp_entity("underlabTrainingTunnel_0b", 6000, 1000, 15191824161433712063, 3987441193),
        tunnel_sprite_entity(2672, 1000, -1, 15191824161433712064),
        tunnel_sprite_entity(2688, 1000, -1, 15191824161433712065),
        tunnel_sprite_entity(5968, 1000, 1, 15191824161433712066),
        tunnel_sprite_entity(5984, 1000, 1, 15191824161433712067),
    ]
    section, count = re.subn(
        r"(\tm_entities:\s*\[\s*\(\s*Reserve:\s*)(\d+)(\s*\))",
        lambda match: match.group(1) + str(int(match.group(2)) + len(entities)) + match.group(3),
        section,
        count=1,
    )
    if count != 1:
        raise ValueError("could not increment the hub entity reserve")
    close = section.rfind("],")
    if close < 0:
        raise ValueError("could not find the end of the hub entity list")
    section = section[:close].rstrip() + ", " + ", ".join(entities) + " ],\n"
    return text[:entities_start] + section + text[markers_start:]


def append_tunnel_art_only(text: str) -> str:
    entities_start = text.index("\tm_entities:")
    markers_start = text.index("\tm_markers:", entities_start)
    section = text[entities_start:markers_start]
    entities = [
        tunnel_sprite_entity(2672, 1000, -1, 15191824161433712064),
        tunnel_sprite_entity(2688, 1000, -1, 15191824161433712065),
        tunnel_sprite_entity(5968, 1000, 1, 15191824161433712066),
        tunnel_sprite_entity(5984, 1000, 1, 15191824161433712067),
    ]
    section, count = re.subn(
        r"(\tm_entities:\s*\[\s*\(\s*Reserve:\s*)(\d+)(\s*\))",
        lambda match: match.group(1) + str(int(match.group(2)) + len(entities)) + match.group(3),
        section,
        count=1,
    )
    if count != 1:
        raise ValueError("could not increment the hub entity reserve")
    close = section.rfind("],")
    if close < 0:
        raise ValueError("could not find the end of the hub entity list")
    section = section[:close].rstrip() + ", " + ", ".join(entities) + " ],\n"
    return text[:entities_start] + section + text[markers_start:]


def edit_checkpoint_layer(block: str, layer_name: str) -> str:
    """Carve the divider and remove retail decorations from the annex screen."""
    position = re.search(
        r"m_position:\s*ycVec2\s*\{.*?x:\s*([-\d.]+),.*?y:\s*([-\d.]+),",
        block,
        re.DOTALL,
    )
    dimensions = re.search(
        r"m_widthInTiles:\s*(\d+),.*?m_heightInTiles:\s*(\d+),", block, re.DOTALL
    )
    chunks = re.search(
        r"m_chunkStart:\s*\[\s*\(\s*Reserve:\s*(\d+)\s*\)(.*?)\],", block, re.DOTALL
    )
    blob = re.search(
        r"(m_tileData:\s*ycDataBlob\s*\{\s*size:\s*)(\d+)(,.*?data:\s*\")([^\"]*)(\")",
        block,
        re.DOTALL,
    )
    if not all((position, dimensions, chunks, blob)):
        raise ValueError(f"{layer_name} layer metadata is incomplete")

    layer_x, layer_y = (int(float(value)) for value in position.groups())
    width, height = (int(value) for value in dimensions.groups())
    reserve = int(chunks.group(1))
    parts = chunks.group(2).split(",")[:reserve]
    starts: list[int | None] = [
        None if part.strip() == "4294967295" else int(part.strip() or 0) for part in parts
    ]
    raw = bytearray(decompress_wflz(base64.b64decode(blob.group(4))))
    chunks_wide = (width + 15) // 16

    def read_tile(world_x: int, world_y: int) -> int:
        tile_x = (world_x - layer_x) // 8
        tile_y = (world_y - layer_y) // 8
        if tile_x < 0 or tile_y < 0 or tile_x >= width or tile_y >= height:
            return 0
        chunk_index = (tile_y // 16) * chunks_wide + tile_x // 16
        within_chunk = (tile_y % 16) * 16 + tile_x % 16
        chunk_start = starts[chunk_index]
        if chunk_start is None:
            return 0
        return struct.unpack_from("<H", raw, chunk_start * 2 + within_chunk * 2)[0]

    def write_tile(world_x: int, world_y: int, value: int) -> None:
        tile_x = (world_x - layer_x) // 8
        tile_y = (world_y - layer_y) // 8
        if tile_x < 0 or tile_y < 0 or tile_x >= width or tile_y >= height:
            return
        chunk_index = (tile_y // 16) * chunks_wide + tile_x // 16
        within_chunk = (tile_y % 16) * 16 + tile_x % 16
        chunk_start = starts[chunk_index]
        if chunk_start is None:
            chunk_start = len(raw) // 2
            starts[chunk_index] = chunk_start
            raw.extend(b"\0" * (16 * 16 * 2))
        struct.pack_into("<H", raw, chunk_start * 2 + within_chunk * 2, value)

    # Clear a 32-pixel doorway.
    if (
        layer_name in {"GROUND", "WALL_BURROW"}
        or layer_name.startswith("WALL_")
        or layer_name.startswith("GROUND_BURROW_")
    ):
        for world_y in range(TUNNEL_TOP, TUNNEL_BOTTOM, 8):
            for world_x in range(DIVIDER_LEFT, DIVIDER_RIGHT, 8):
                write_tile(world_x, world_y, 0)

    if "COLLISION" in layer_name:
        for world_y in range(TUNNEL_TOP, TUNNEL_BOTTOM, 8):
            for world_x in range(DIVIDER_LEFT - 24, DIVIDER_RIGHT, 8):
                write_tile(world_x, world_y, 0)

    if layer_name.startswith("GROUND_") and not (
        layer_name.startswith("GROUND_BURROW_")
        or layer_name.startswith("GROUND_SAVER_")
        or layer_name.startswith("GROUND_KEEPER_")
        or layer_name == "GROUND_PHANDS"
    ):
        for world_y in range(TUNNEL_TOP, TUNNEL_BOTTOM, 8):
            for world_x in range(DIVIDER_LEFT, DIVIDER_RIGHT, 8):
                source_x = 2664 + (((world_x // 8) & 1) * 8)
                write_tile(world_x, world_y, read_tile(source_x, world_y))

    if layer_name.startswith("EMPTY_POOL_") or layer_name == "HEALING_WATER":
        for world_y in range(layer_y, layer_y + height * 8, 8):
            for world_x in range(DIVIDER_RIGHT, ANNEX_RIGHT, 8):
                write_tile(world_x, world_y, 0)

    chunk_values = ["4294967295" if value is None else str(value) for value in starts]
    chunk_text = f"m_chunkStart: [ ( Reserve: {len(starts)} ) " + ", ".join(chunk_values) + " ],"
    block = block[: chunks.start()] + chunk_text + block[chunks.end() :]
    replacement_blob = encode_wflz_literals(bytes(raw))
    replacement_b64 = base64.b64encode(replacement_blob).decode("ascii")
    blob = re.search(
        r"(m_tileData:\s*ycDataBlob\s*\{\s*size:\s*)(\d+)(,.*?data:\s*\")([^\"]*)(\")",
        block,
        re.DOTALL,
    )
    assert blob is not None
    return (
        block[: blob.start()]
        + blob.group(1)
        + str(len(replacement_blob))
        + blob.group(3)
        + replacement_b64
        + blob.group(5)
        + block[blob.end() :]
    )


def carve_checkpoint_tunnel(text: str) -> str:
    layers_start = text.index("\tm_tileLayers:")
    entities_start = text.index("\tm_entities:", layers_start)
    cursor = layers_start
    replacements: list[tuple[int, int, str]] = []
    while True:
        token_start = text.find("ycTileLevel2TileLayer", cursor, entities_start)
        if token_start < 0:
            break
        block_start, block_end = find_braced_block(text, token_start)
        block = text[block_start:block_end]
        name = re.search(r'm_name:\s*"([^\"]+)",', block)
        if name:
            replacements.append(
                (block_start, block_end, edit_checkpoint_layer(block, name.group(1)))
            )
        cursor = block_end
    for block_start, block_end, replacement in reversed(replacements):
        text = text[:block_start] + replacement + text[block_end:]
    return text


def append_annex_room_bound(text: str) -> str:
    start = text.index("\tm_roomBounds:")
    end = text.index("\tm_tileLayers:", start)
    section = text[start:end]
    section, count = re.subn(
        r"(m_roomBounds:\s*\[\s*\(\s*Reserve:\s*)2(\s*\))",
        r"\g<1>3\2",
        section,
        count=1,
    )
    if count != 1:
        raise ValueError("expected the retail checkpoint's two room bounds")
    geometry = f'''ycTileLevel2Geometry
        {{
            m_nameHash: 3987441600,
            m_nameLevelHash: 15191824161433712600,
            m_center: ycVec2
            {{
                x: {ANNEX_CENTER_X},
                y: -2496,
            }},
            m_extent: ycVec2
            {{
                x: 128,
                y: 64,
            }},
        }}'''
    close = section.rfind(" ],")
    if close < 0:
        raise ValueError("could not append the Annex room bound")
    section = section[:close].rstrip() + ", " + geometry + " ],\n"
    return text[:start] + section + text[end:]


def clone_retail_expansion_tiles(text: str) -> str:
    """Clone retail room 1 into room 2 before applying the custom layout."""
    layers_start = text.index("\tm_tileLayers:")
    entities_start = text.index("\tm_entities:", layers_start)
    cursor = layers_start
    replacements: list[tuple[int, int, str]] = []
    while True:
        token_start = text.find("ycTileLevel2TileLayer", cursor, entities_start)
        if token_start < 0:
            break
        block_start, block_end = find_braced_block(text, token_start)
        block = text[block_start:block_end]
        cursor = block_end
        position = re.search(
            r"m_position:\s*ycVec2\s*\{.*?x:\s*([-\d.]+),.*?y:\s*([-\d.]+),",
            block,
            re.DOTALL,
        )
        dimensions = re.search(
            r"m_widthInTiles:\s*(\d+),.*?m_heightInTiles:\s*(\d+),",
            block,
            re.DOTALL,
        )
        chunks = re.search(
            r"m_chunkStart:\s*\[\s*\(\s*Reserve:\s*(\d+)\s*\)(.*?)\],",
            block,
            re.DOTALL,
        )
        blob = re.search(
            r"(m_tileData:\s*ycDataBlob\s*\{\s*size:\s*)(\d+)(,.*?data:\s*\")([^\"]*)(\")",
            block,
            re.DOTALL,
        )
        if not all((position, dimensions, chunks, blob)):
            continue

        layer_x, layer_y = (int(float(value)) for value in position.groups())
        old_width, old_height = (int(value) for value in dimensions.groups())
        old_chunks_wide = (old_width + 15) // 16
        old_chunk_rows = (old_height + 15) // 16
        reserve = int(chunks.group(1))
        old_starts: list[int | None] = [
            None if part.strip() == "4294967295" else int(part.strip() or 0)
            for part in chunks.group(2).split(",")[:reserve]
        ]
        if len(old_starts) != reserve:
            raise ValueError("checkpoint layer chunk table is truncated")
        raw = bytearray(decompress_wflz(base64.b64decode(blob.group(4))))

        def old_read(world_x: int, world_y: int) -> int:
            tile_x = (world_x - layer_x) // 8
            tile_y = (world_y - layer_y) // 8
            if tile_x < 0 or tile_y < 0 or tile_x >= old_width or tile_y >= old_height:
                return 0
            index = (tile_y // 16) * old_chunks_wide + tile_x // 16
            if index >= len(old_starts) or old_starts[index] is None:
                return 0
            within = (tile_y % 16) * 16 + tile_x % 16
            return struct.unpack_from("<H", raw, old_starts[index] * 2 + within * 2)[0]

        source = [
            [old_read(x, y) for x in range(RETAIL_EXPANSION_LEFT, RETAIL_EXPANSION_RIGHT, 8)]
            for y in range(2240, 2368, 8)
        ]
        new_width = old_width
        new_height = max(old_height, (ANNEX_BOTTOM - layer_y + 7) // 8)
        new_chunks_wide = (new_width + 15) // 16
        new_chunk_rows = (new_height + 15) // 16
        starts: list[int | None] = [None] * (new_chunks_wide * new_chunk_rows)
        for row in range(old_chunk_rows):
            for column in range(old_chunks_wide):
                old_index = row * old_chunks_wide + column
                if old_index < len(old_starts):
                    starts[row * new_chunks_wide + column] = old_starts[old_index]

        def write(world_x: int, world_y: int, value: int) -> None:
            tile_x = (world_x - layer_x) // 8
            tile_y = (world_y - layer_y) // 8
            if tile_x < 0 or tile_y < 0 or tile_x >= new_width or tile_y >= new_height:
                return
            index = (tile_y // 16) * new_chunks_wide + tile_x // 16
            within = (tile_y % 16) * 16 + tile_x % 16
            chunk_start = starts[index]
            if chunk_start is None:
                if value == 0:
                    return
                chunk_start = len(raw) // 2
                starts[index] = chunk_start
                raw.extend(b"\0" * (16 * 16 * 2))
            struct.pack_into("<H", raw, chunk_start * 2 + within * 2, value)

        for tile_y in range(old_height, new_height):
            world_y = layer_y + tile_y * 8
            for tile_x in range(new_width):
                write(layer_x + tile_x * 8, world_y, 0)

        for row, world_y in enumerate(range(ANNEX_TOP, ANNEX_BOTTOM, 8)):
            for column, world_x in enumerate(range(ANNEX_LEFT, ANNEX_RIGHT, 8)):
                write(world_x, world_y, source[row][column])

        block = re.sub(
            r"m_widthInTiles:\s*\d+,", f"m_widthInTiles: {new_width},", block, count=1
        )
        block = re.sub(
            r"m_heightInTiles:\s*\d+,", f"m_heightInTiles: {new_height},", block, count=1
        )
        chunks = re.search(
            r"m_chunkStart:\s*\[\s*\(\s*Reserve:\s*(\d+)\s*\)(.*?)\],",
            block,
            re.DOTALL,
        )
        assert chunks is not None
        chunk_values = ["4294967295" if value is None else str(value) for value in starts]
        chunk_text = (
            f"m_chunkStart: [ ( Reserve: {len(starts)} ) "
            + ", ".join(chunk_values)
            + " ],"
        )
        block = block[: chunks.start()] + chunk_text + block[chunks.end() :]
        replacement_blob = encode_wflz_literals(bytes(raw))
        replacement_b64 = base64.b64encode(replacement_blob).decode("ascii")
        blob = re.search(
            r"(m_tileData:\s*ycDataBlob\s*\{\s*size:\s*)(\d+)(,.*?data:\s*\")([^\"]*)(\")",
            block,
            re.DOTALL,
        )
        assert blob is not None
        block = (
            block[: blob.start()]
            + blob.group(1)
            + str(len(replacement_blob))
            + blob.group(3)
            + replacement_b64
            + blob.group(5)
            + block[blob.end() :]
        )
        replacements.append((block_start, block_end, block))

    for block_start, block_end, replacement in reversed(replacements):
        text = text[:block_start] + replacement + text[block_end:]
    return text


def clean_annex_entities(text: str) -> str:
    entities_start = text.index("\tm_entities:")
    markers_start = text.index("\tm_markers:", entities_start)
    section = text[entities_start:markers_start]
    kept: list[str] = []
    cursor = 0
    while True:
        token_start = section.find("ycTileLevel2Entity", cursor)
        if token_start < 0:
            break
        block_start, block_end = find_braced_block(section, token_start)
        block = section[block_start:block_end]
        position = re.search(
            r"m_position:\s*ycVec2\s*\{.*?x:\s*([-\d.]+),", block, re.DOTALL
        )
        x = float(position.group(1)) if position else -1.0
        entity_type_match = re.search(r"m_entityType:\s*(\d+),", block)
        entity_type = int(entity_type_match.group(1)) if entity_type_match else -1
        kept.append(block)
        cursor = block_end
    art_entities = [
        annex_region_entity(),
        native_water_anim_entity(
            "healingWaterCorner_TopLeft", 2952, 15191824161433712500
        ),
        native_water_anim_entity(
            "healingWater_Top", 2968, 15191824161433712501
        ),
        native_water_anim_entity(
            "healingWaterCorner_TopRight", 2984, 15191824161433712502
        ),
        annex_sprite_entity(
            "teleport", 2745.67993, 2335.94998,
            15191824161433712300, 3987441300,
            tile_layer_index=70,
        ),
        # Bottom-left Annex pad.
        annex_sprite_entity(
            "teleport", 2912, 2528, 15191824161433712301, 3987441301,
            tile_layer_index=70,
        ),
        annex_sprite_entity(
            "rug", 2976, 2456, 15191824161433712302, 3987441302,
            tile_layer_index=70,
            scale=0.75,
        ),
        native_training_dummy_entity(
            2928, 2486, 1, 15191824161433712700, 3987441700
        ),
        native_training_dummy_entity(
            2976, 2486, 1, 15191824161433712701, 3987441701, scale_x=-1
        ),
        native_training_dummy_entity(
            3024, 2486, 2, 15191824161433712702, 3987441702
        ),
    ]
    kept.extend(art_entities)
    rebuilt = (
        f"\tm_entities: [ ( Reserve: {len(kept)} ) \n\t\t"
        + ", ".join(kept)
        + " ],\n"
    )
    return text[:entities_start] + rebuilt + text[markers_start:]


def remove_annex_hole_tiles(text: str) -> str:
    """Move the native water to bottom-center and restore the former top recess."""
    layers_start = text.index("\tm_tileLayers:")
    entities_start = text.index("\tm_entities:", layers_start)
    cursor = layers_start
    replacements: list[tuple[int, int, str]] = []
    while True:
        token_start = text.find("ycTileLevel2TileLayer", cursor, entities_start)
        if token_start < 0:
            break
        block_start, block_end = find_braced_block(text, token_start)
        block = text[block_start:block_end]
        cursor = block_end
        name_match = re.search(r'm_name:\s*"([^\"]+)",', block)
        if not name_match:
            continue
        layer_name = name_match.group(1)
        is_ground = layer_name.startswith("GROUND_") and not any(
            marker in layer_name for marker in ("BURROW", "SAVER", "KEEPER", "PHANDS")
        )
        is_native_water = layer_name == "HEALING_WATER" or layer_name.startswith(
            "EMPTY_POOL_"
        )
        if not is_ground and not is_native_water:
            continue

        position = re.search(
            r"m_position:\s*ycVec2\s*\{.*?x:\s*([-\d.]+),.*?y:\s*([-\d.]+),",
            block,
            re.DOTALL,
        )
        dimensions = re.search(
            r"m_widthInTiles:\s*(\d+),.*?m_heightInTiles:\s*(\d+),",
            block,
            re.DOTALL,
        )
        chunks = re.search(
            r"m_chunkStart:\s*\[\s*\(\s*Reserve:\s*(\d+)\s*\)(.*?)\],",
            block,
            re.DOTALL,
        )
        blob = re.search(
            r"(m_tileData:\s*ycDataBlob\s*\{\s*size:\s*)(\d+)(,.*?data:\s*\")([^\"]*)(\")",
            block,
            re.DOTALL,
        )
        if not all((position, dimensions, chunks, blob)):
            continue
        layer_x, layer_y = (int(float(value)) for value in position.groups())
        width, height = (int(value) for value in dimensions.groups())
        original_width = width
        original_height = height
        if is_native_water:
            width = max(width, (2992 - layer_x) // 8 + 1)
            height = max(height, (2544 - layer_y) // 8 + 1)
        reserve = int(chunks.group(1))
        starts: list[int | None] = [
            None if part.strip() == "4294967295" else int(part.strip() or 0)
            for part in chunks.group(2).split(",")[:reserve]
        ]
        raw = bytearray(decompress_wflz(base64.b64decode(blob.group(4))))
        chunks_wide = (width + 15) // 16

        def location(world_x: int, world_y: int) -> tuple[int, int] | None:
            tile_x = (world_x - layer_x) // 8
            tile_y = (world_y - layer_y) // 8
            if tile_x < 0 or tile_y < 0 or tile_x >= width or tile_y >= height:
                return None
            return ((tile_y // 16) * chunks_wide + tile_x // 16, (tile_y % 16) * 16 + tile_x % 16)

        def read_tile(world_x: int, world_y: int) -> int:
            found = location(world_x, world_y)
            if found is None:
                return 0
            chunk_index, within_chunk = found
            chunk_start = starts[chunk_index]
            if chunk_start is None:
                return 0
            return struct.unpack_from("<H", raw, chunk_start * 2 + within_chunk * 2)[0]

        def write_tile(world_x: int, world_y: int, value: int) -> None:
            found = location(world_x, world_y)
            if found is None:
                return
            chunk_index, within_chunk = found
            chunk_start = starts[chunk_index]
            if chunk_start is None:
                chunk_start = len(raw) // 2
                starts[chunk_index] = chunk_start
                raw.extend(b"\0" * (16 * 16 * 2))
            struct.pack_into("<H", raw, chunk_start * 2 + within_chunk * 2, value)

        if is_ground:
            for world_y in range(2456, 2473, 8):
                source_y = 2496 + (world_y - 2456)
                for world_x in range(2928, 3017, 8):
                    write_tile(world_x, world_y, read_tile(world_x, source_y))
            for world_y in (2536, 2544):
                for world_x in range(2952, 3000, 8):
                    write_tile(world_x, world_y, 0)
            rim_target_x = tuple(range(2944, 3008, 8))
            rim_source_x = (2680, 2688, 2696, 2704, 2728, 2736, 2744, 2752)
            for target_x, source_x in zip(rim_target_x, rim_source_x):
                write_tile(target_x, 2528, read_tile(source_x, 2296))
            for target_y, source_y in ((2536, 2304), (2544, 2312)):
                write_tile(2944, target_y, read_tile(2680, source_y))
                write_tile(3000, target_y, read_tile(2752, source_y))
        else:
            for tile_y in range(height):
                for tile_x in range(width):
                    if tile_x >= original_width or tile_y >= original_height:
                        write_tile(layer_x + tile_x * 8, layer_y + tile_y * 8, 0)
            source_xs = (2688, 2696, 2704, 2712, 2736, 2744)
            source_values = [
                read_tile(source_x, source_y)
                for source_y in (2304, 2312)
                for source_x in source_xs
            ]
            for world_y in (2456, 2464):
                for world_x in (2968, 2976):
                    write_tile(world_x, world_y, 0)
            target_positions = [
                (world_x, world_y)
                for world_y in (2536, 2544)
                for world_x in range(2952, 3000, 8)
            ]
            for (world_x, world_y), value in zip(target_positions, source_values):
                write_tile(world_x, world_y, value)

        if is_native_water:
            block = re.sub(
                r"m_widthInTiles:\s*\d+,",
                f"m_widthInTiles: {width},",
                block,
                count=1,
            )
            block = re.sub(
                r"m_heightInTiles:\s*\d+,",
                f"m_heightInTiles: {height},",
                block,
                count=1,
            )

        chunk_values = ["4294967295" if value is None else str(value) for value in starts]
        chunk_text = f"m_chunkStart: [ ( Reserve: {len(starts)} ) " + ", ".join(chunk_values) + " ],"
        block = block[: chunks.start()] + chunk_text + block[chunks.end() :]
        replacement_blob = encode_wflz_literals(bytes(raw))
        replacement_b64 = base64.b64encode(replacement_blob).decode("ascii")
        blob = re.search(
            r"(m_tileData:\s*ycDataBlob\s*\{\s*size:\s*)(\d+)(,.*?data:\s*\")([^\"]*)(\")",
            block,
            re.DOTALL,
        )
        assert blob is not None
        block = (
            block[: blob.start()]
            + blob.group(1)
            + str(len(replacement_blob))
            + blob.group(3)
            + replacement_b64
            + blob.group(5)
            + block[blob.end() :]
        )
        replacements.append((block_start, block_end, block))

    for block_start, block_end, replacement in reversed(replacements):
        text = text[:block_start] + replacement + text[block_end:]
    return text


def main() -> None:
    if not SOURCE.is_file():
        raise SystemExit(f"Missing unpak checkpoint source: {SOURCE}")
    output = SOURCE.read_text(encoding="utf-8-sig")
    output = append_annex_room_bound(output)
    output = clone_retail_expansion_tiles(output)
    output = remove_annex_hole_tiles(output)
    output = clean_annex_entities(output)
    DESTINATION.parent.mkdir(parents=True, exist_ok=True)
    DESTINATION.write_text(output, encoding="utf-8", newline="\n")
    print(
        "Checkpoint annex: preserved both retail rooms and appended the custom Annex as room 2"
    )


if __name__ == "__main__":
    main()
