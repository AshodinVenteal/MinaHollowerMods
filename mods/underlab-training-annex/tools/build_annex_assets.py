from __future__ import annotations

import base64
import os
import re
import struct
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter


MOD_ROOT = Path(__file__).resolve().parents[1]
GAME_ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(GAME_ROOT / "tools"))

import export_anb_frames  # noqa: E402

UNPAK = (
    Path(os.environ["APPDATA"])
    / "Yacht Club Games"
    / "Mina the Hollower"
    / "mods"
    / "unpak"
    / "data"
)
SOURCE_SHEET = MOD_ROOT / "art_source" / "generated_asset_sheet.png"
PNG_OUT = MOD_ROOT / "art_source" / "final"
EDITABLE = MOD_ROOT / "editable_sprites"
ANB_OUT = MOD_ROOT / "data" / "levels" / "tilesets" / "underlabTrainingAnnex" / "animTiles"
PALETTE_OUT = MOD_ROOT / "data" / "palettes" / "underlabTrainingAnnex.pal.yc"

PALETTE = [(0, 0, 0, 255)] * 256
PALETTE[0] = (255, 255, 255, 0)
for index, color in {
    2: (13, 32, 48, 255), 4: (33, 22, 64, 255), 13: (90, 25, 145, 255),
    19: (52, 52, 52, 255), 22: (152, 120, 160, 255), 24: (174, 108, 55, 255),
    28: (123, 123, 123, 255), 38: (37, 226, 205, 255), 39: (152, 220, 255, 255),
    43: (248, 176, 48, 255),
    51: (226, 215, 181, 255), 52: (204, 143, 21, 255), 65: (209, 45, 42, 255),
    83: (64, 41, 8, 255),
}.items():
    PALETTE[index] = color

ASSET_RAMPS = {
    "dummy": (83, 24, 65),
    "teleport": (2, 43, 38),
    "rug": (4, 13, 52),
    "dais": (19, 28, 51),
    "pool": (4, 13, 22),
    "statue": (19, 28, 51),
}

# Source bounds and output size.
CONCEPT_ASSETS = {
    "dummy": ((52, 190, 212, 440), (16, 24)),
    "teleport": ((302, 278, 460, 436), (16, 16)),
    "rug": ((526, 160, 1487, 544), (96, 48)),
    "dais": ((407, 676, 592, 819), (16, 12)),
    "pool": ((816, 650, 1006, 832), (16, 16)),
}

BOSSES = [
    (27, "intro_boss", UNPAK / "bosses" / "introBoss.anb.yc"),
    (0, "gator", UNPAK / "bosses" / "gator.anb.yc"),
    (1, "duchess", UNPAK / "bosses" / "duchess.anb.yc"),
    (2, "giga_pumpkin", UNPAK / "bosses" / "gigaPumpkinHands.anb.yc"),
    (3, "brain", UNPAK / "bosses" / "brain.anb.yc"),
    (4, "major_miner", UNPAK / "bosses" / "majorMiner.anb.yc"),
    (5, "abducted", UNPAK / "bosses" / "abducted.anb.yc"),
    (6, "ice_creature", UNPAK / "bosses" / "creatureInTheIce.anb.yc"),
    (7, "lionel", UNPAK / "bosses" / "lionelBoss.anb.yc"),
    (8, "moon", UNPAK / "bosses" / "moonBoss.anb.yc"),
    (9, "butler", UNPAK / "bosses" / "catButlerBoss.anb.yc"),
    (10, "evra", UNPAK / "bosses" / "evraTheUndying.anb.yc"),
]


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
    for cursor in range(brace_start, len(text)):
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
    raise ValueError("unterminated YCD block")


def texture_from_text_anb(path: Path, texture_index: int) -> Image.Image:
    text = path.read_text(encoding="utf-8-sig")
    textures_start = text.index("m_textures:")
    blocks: list[str] = []
    cursor = textures_start
    while True:
        token = text.find("ycCutter2Texture", cursor)
        if token < 0:
            break
        start, end = find_braced_block(text, token)
        blocks.append(text[start:end])
        cursor = end
    if texture_index >= len(blocks):
        texture_index = 0
    block = blocks[texture_index]
    width = int(re.search(r"m_width:\s*(\d+)", block).group(1))
    height = int(re.search(r"m_height:\s*(\d+)", block).group(1))
    blob = base64.b64decode(re.search(r'data:\s*"([^"]+)"', block).group(1))
    raw = decompress_wflz(blob)
    if len(raw) != width * height:
        raise ValueError(f"{path.name}: decoded texture size mismatch")
    rgba = bytearray()
    for value in raw:
        rgba.extend((255, 255, 255, 0) if value == 0 else (255, 255, 255, 255))
    return Image.frombytes("RGBA", (width, height), bytes(rgba))


def first_sequence_frame(path: Path, sequence_names: tuple[str, ...]) -> int:
    text = path.read_text(encoding="utf-8-sig")
    for name in sequence_names:
        match = re.search(rf'm_name:\s*"{re.escape(name)}"', text)
        if not match:
            continue
        start, end = find_braced_block(text, text.rfind("ycCutter2Sequence", 0, match.start()))
        block = text[start:end]
        frame = re.search(r"m_frameIndex:\s*(\d+)", block)
        return int(frame.group(1)) if frame else 0
    return 0


def alpha_bbox(image: Image.Image) -> tuple[int, int, int, int]:
    bbox = image.getchannel("A").getbbox()
    if not bbox:
        raise ValueError("asset has no opaque pixels")
    return bbox


def nearest_resize(image: Image.Image, size: tuple[int, int]) -> Image.Image:
    return image.resize(size, Image.Resampling.NEAREST)


def concept_to_indexed(name: str, image: Image.Image) -> Image.Image:
    dark, mid, light = ASSET_RAMPS[name]
    out = Image.new("P", image.size, 0)
    data: list[int] = []
    for r, g, b, a in image.convert("RGBA").getdata():
        if a < 128:
            data.append(0)
            continue
        value = max(r, g, b)
        if name == "teleport":
            if b > r * 1.15 and g > r * 1.05:
                data.append(light)
            elif r > b * 1.25 and g > b * 0.9:
                data.append(mid)
            else:
                data.append(dark)
        elif name == "rug":
            if r > b * 1.22 and g > b * 0.75:
                data.append(light)
            elif value < 78:
                data.append(dark)
            else:
                data.append(mid)
        elif name == "dummy":
            if r > g * 1.35 and r > b * 1.35:
                data.append(light)
            elif value < 105:
                data.append(dark)
            else:
                data.append(mid)
        else:
            if value < 88:
                data.append(dark)
            elif value > 170:
                data.append(light)
            else:
                data.append(mid)
    out.putdata(data)
    out.putpalette([channel for color in PALETTE for channel in color[:3]])
    out.info["transparency"] = 0
    return out


def editable_to_indexed(
    path: Path, expected_size: tuple[int, int], ramp: tuple[int, int, int]
) -> Image.Image:
    image = Image.open(path).convert("RGBA")
    if image.size != expected_size:
        raise ValueError(f"{path.name}: expected {expected_size}, found {image.size}")
    out = Image.new("P", expected_size, 0)
    data: list[int] = []
    for r, g, b, a in image.getdata():
        if a < 128:
            data.append(0)
            continue
        data.append(
            min(
                ramp,
                key=lambda index: (r - PALETTE[index][0]) ** 2
                + (g - PALETTE[index][1]) ** 2
                + (b - PALETTE[index][2]) ** 2,
            )
        )
    out.putdata(data)
    out.putpalette([channel for color in PALETTE for channel in color[:3]])
    out.info["transparency"] = 0
    return out


def add_kear_to_rug(rug: Image.Image) -> Image.Image:
    # Replace the crest with a Kear.
    pixels = rug.load()
    for y in range(10, 38):
        for x in range(31, 65):
            pixels[x, y] = ASSET_RAMPS["rug"][1]
    kear = Image.new("P", (18, 24), 0)
    kear.putpalette([channel for color in PALETTE for channel in color[:3]])
    gold = {
        (6, 1), (7, 1), (10, 1), (11, 1),
        (4, 2), (5, 2), (6, 2), (7, 2), (8, 2), (9, 2), (10, 2), (11, 2), (12, 2), (13, 2),
        (3, 3), (4, 3), (13, 3), (14, 3),
        (2, 4), (3, 4), (5, 4), (6, 4), (7, 4), (8, 4), (9, 4), (10, 4), (11, 4), (12, 4), (14, 4), (15, 4),
        (2, 5), (4, 5), (5, 5), (12, 5), (13, 5), (15, 5),
        (1, 6), (2, 6), (4, 6), (13, 6), (15, 6), (16, 6),
        (1, 7), (2, 7), (4, 7), (13, 7), (15, 7), (16, 7),
        (2, 8), (4, 8), (5, 8), (12, 8), (13, 8), (15, 8),
        (2, 9), (3, 9), (5, 9), (6, 9), (7, 9), (8, 9), (9, 9), (10, 9), (11, 9), (12, 9), (14, 9), (15, 9),
        (3, 10), (4, 10), (13, 10), (14, 10),
        (4, 11), (5, 11), (6, 11), (7, 11), (8, 11), (9, 11), (10, 11), (11, 11), (12, 11), (13, 11),
        (7, 12), (8, 12), (9, 12), (10, 12),
        (7, 13), (8, 13), (9, 13), (10, 13),
        (7, 14), (8, 14), (9, 14), (10, 14),
        (7, 15), (8, 15), (9, 15), (10, 15),
        (7, 16), (8, 16), (9, 16), (10, 16),
        (7, 17), (8, 17), (9, 17), (10, 17),
        (7, 18), (8, 18), (9, 18), (10, 18),
        (7, 19), (8, 19), (9, 19), (10, 19), (11, 19), (12, 19),
        (7, 20), (8, 20), (9, 20), (10, 20), (12, 20), (13, 20),
        (7, 21), (8, 21), (9, 21), (10, 21), (11, 21), (12, 21), (13, 21),
    }
    for point in gold:
        kear.putpixel(point, ASSET_RAMPS["rug"][2])
    # Draw the axle and outline.
    mask = kear.point(lambda value: 255 if value else 0).convert("L")
    outline = mask.filter(ImageFilter.MaxFilter(3))
    x = (rug.width - kear.width) // 2
    y = (rug.height - kear.height) // 2
    rug.paste(ASSET_RAMPS["rug"][0], (x, y), outline)
    rug.paste(
        ASSET_RAMPS["rug"][2],
        (x, y, x + kear.width, y + kear.height),
        mask,
    )
    return rug


def boss_statue(path: Path) -> Image.Image:
    dark, mid, light = ASSET_RAMPS["statue"]
    frame = first_sequence_frame(path, ("idle_D", "idle", "stand", "walk_D", "Default"))
    source = texture_from_text_anb(path, frame)
    source = source.crop(alpha_bbox(source))
    source.thumbnail((12, 12), Image.Resampling.NEAREST)
    mask = source.getchannel("A").point(lambda a: 255 if a >= 128 else 0)
    canvas = Image.new("P", (16, 18), 0)
    canvas.putpalette([channel for color in PALETTE for channel in color[:3]])
    x = (16 - source.width) // 2
    y = 1 + (12 - source.height)
    outline = mask.filter(ImageFilter.MaxFilter(3))
    canvas.paste(dark, (x, y), outline)
    canvas.paste(mid, (x, y), mask)
    # Add highlights and the stone dais.
    highlight = mask.crop((0, 0, max(1, mask.width // 2), mask.height))
    canvas.paste(light, (x, y), highlight)
    for row, left, right, color in ((13, 4, 12, light), (14, 3, 13, mid), (15, 2, 14, dark), (16, 2, 14, dark)):
        for column in range(left, right):
            canvas.putpixel((column, row), color)
    canvas.info["transparency"] = 0
    return canvas


def write_png(name: str, indexed: Image.Image) -> None:
    PNG_OUT.mkdir(parents=True, exist_ok=True)
    indexed.save(PNG_OUT / f"{name}.png", transparency=0)


def write_anb(name: str, indexed: Image.Image) -> None:
    width, height = indexed.size
    floor_sorted = name in ("rug", "teleport")
    center_y = height / 2 if floor_sorted else -height / 2
    quad_y = 0 if floor_sorted else -height
    raw = bytes(indexed.getdata())
    encoded = encode_wflz_literals(raw)
    data = base64.b64encode(encoded).decode("ascii")
    ANB_OUT.mkdir(parents=True, exist_ok=True)
    text = f'''[YCD Version: 1]
ycCutter2AnimDef
{{
\tm_paletteName: "palettes/global.pal.yc",
\tm_usePointSampler: true,
\tm_sequences: [ ( Reserve: 1 )
\t\tycCutter2Sequence
\t\t{{
\t\t\tm_nameHash: 1511665218,
\t\t\tm_name: "idle",
\t\t\tm_frames: [ ( Reserve: 1 )
\t\t\t\tycCutter2SequenceFrame {{ m_delay: 1, }} ],
\t\t}} ],
\tm_frames: [ ( Reserve: 1 )
\t\tycCutter2Frame
\t\t{{
\t\t\tm_textureShadowIdx: -1,
\t\t\tm_centerX: {width / 2:g},
\t\t\tm_centerY: {center_y:g},
\t\t\tm_width: {width},
\t\t\tm_height: {height},
\t\t\tm_quads: [ ( Reserve: 1 )
\t\t\t\tycCutter2FrameQuad
\t\t\t\t{{
\t\t\t\t\tm_width: {width},
\t\t\t\t\tm_height: {height},
\t\t\t\t\tm_posX: {-width / 2:g},
\t\t\t\t\tm_posY: {quad_y:g},
\t\t\t\t}} ],
\t\t}} ],
\tm_textures: [ ( Reserve: 1 )
\t\tycCutter2Texture
\t\t{{
\t\t\tm_width: {width},
\t\t\tm_height: {height},
\t\t\tm_texData: ycDataBlob
\t\t\t{{
\t\t\t\tsize: {len(encoded)},
\t\t\t\talign: 0,
\t\t\t\tdata: "{data}",
\t\t\t}},
\t\t}} ],
}};
'''
    (ANB_OUT / f"{name}.anb.yc").write_text(text, encoding="utf-8", newline="\n")


def build_teleport_ring_frames() -> list[Image.Image]:
    width, height = 64, 32
    center_x, center_y = width // 2, height // 2
    ring_sets = (
        (2,),
        (4, 2),
        (7, 4),
        (11, 7, 3),
        (15, 10, 5),
        (20, 14, 8),
    )
    frames: list[Image.Image] = []
    for frame_index, radii in enumerate(ring_sets):
        frame = Image.new("P", (width, height), 0)
        frame.putpalette([channel for color in PALETTE for channel in color[:3]])
        frame.info["transparency"] = 0
        draw = ImageDraw.Draw(frame)
        for ring_index, radius_x in enumerate(radii):
            radius_y = max(1, round(radius_x * 0.46))
            color = 38 if (frame_index + ring_index) % 2 == 0 else 39
            draw.ellipse(
                (
                    center_x - radius_x,
                    center_y - radius_y,
                    center_x + radius_x,
                    center_y + radius_y,
                ),
                outline=color,
                width=1,
            )
        frames.append(frame)
    return frames


def write_teleport_ring_anb() -> None:
    frames = build_teleport_ring_frames()
    width, height = frames[0].size
    sequence_frames = ", ".join(
        f"ycCutter2SequenceFrame {{ m_frameIndex: {index}, m_delay: 0.045, }}"
        for index in range(len(frames))
    )
    frame_defs = []
    texture_defs = []
    for index, frame in enumerate(frames):
        raw = bytes(frame.getdata())
        encoded = encode_wflz_literals(raw)
        data = base64.b64encode(encoded).decode("ascii")
        frame_defs.append(
            f'''ycCutter2Frame
		{{
			m_textureIdx: {index},
			m_textureShadowIdx: -1,
			m_centerX: 0.5,
			m_centerY: -0.5,
			m_width: {width},
			m_height: {height},
			m_quads: [ ( Reserve: 1 )
				ycCutter2FrameQuad
				{{
					m_width: {width},
					m_height: {height},
					m_posX: {-width / 2:g},
					m_posY: {-height / 2:g},
				}} ],
		}}'''
        )
        texture_defs.append(
            f'''ycCutter2Texture
		{{
			m_width: {width},
			m_height: {height},
			m_texData: ycDataBlob
			{{
				size: {len(encoded)},
				align: 0,
				data: "{data}",
			}},
		}}'''
        )
        frame.save(PNG_OUT / f"teleportRing_{index}.png", transparency=0)

    ANB_OUT.mkdir(parents=True, exist_ok=True)
    PNG_OUT.mkdir(parents=True, exist_ok=True)
    text = f'''[YCD Version: 1]
ycCutter2AnimDef
{{
	m_paletteName: "palettes/global.pal.yc",
	m_usePointSampler: true,
	m_sequences: [ ( Reserve: 1 )
		ycCutter2Sequence
		{{
			m_nameHash: 1511665218,
			m_name: "idle",
			m_frames: [ ( Reserve: {len(frames)} ) {sequence_frames} ],
		}} ],
	m_frames: [ ( Reserve: {len(frames)} ) {", ".join(frame_defs)} ],
	m_textures: [ ( Reserve: {len(frames)} ) {", ".join(texture_defs)} ],
}};
'''
    (ANB_OUT / "teleportRing.anb.yc").write_text(text, encoding="utf-8", newline="\n")


def write_native_teleport_sparkle_anb() -> None:
    source = MOD_ROOT / "research" / "teleport_vfx" / "effects" / "sparkle.anb.yc"
    if not source.is_file():
        raise FileNotFoundError(f"native sparkle source is missing: {source}")
    parsed = export_anb_frames.parse_frames(source)
    sequence_frames = ", ".join(
        f"ycCutter2SequenceFrame {{ m_frameIndex: {index}, m_delay: 0.045, }}"
        for index in range(len(parsed))
    )
    frame_defs = []
    texture_defs = []
    for index, native_frame in enumerate(parsed):
        width = int(native_frame["width"])
        height = int(native_frame["height"])
        remapped_pixels = bytes(
            38 if value == 1 else 39 if value == 2 else 0
            for value in native_frame["pixels"]
        )
        encoded = encode_wflz_literals(remapped_pixels)
        data = base64.b64encode(encoded).decode("ascii")
        frame_defs.append(
            f'''ycCutter2Frame
		{{
			m_textureIdx: {index},
			m_textureShadowIdx: -1,
			m_centerX: 0.5,
			m_centerY: -0.5,
			m_width: {width},
			m_height: {height},
			m_quads: [ ( Reserve: 1 )
				ycCutter2FrameQuad
				{{
					m_width: {width},
					m_height: {height},
					m_posX: {-width / 2:g},
					m_posY: {-height / 2:g},
				}} ],
		}}'''
        )
        texture_defs.append(
            f'''ycCutter2Texture
		{{
			m_width: {width},
			m_height: {height},
			m_texData: ycDataBlob
			{{
				size: {len(encoded)},
				align: 0,
				data: "{data}",
			}},
		}}'''
        )
    text = f'''[YCD Version: 1]
ycCutter2AnimDef
{{
	m_paletteName: "palettes/global.pal.yc",
	m_usePointSampler: true,
	m_sequences: [ ( Reserve: 1 )
		ycCutter2Sequence
		{{
			m_nameHash: 1511665218,
			m_name: "idle",
			m_frames: [ ( Reserve: {len(parsed)} ) {sequence_frames} ],
		}} ],
	m_frames: [ ( Reserve: {len(parsed)} ) {", ".join(frame_defs)} ],
	m_textures: [ ( Reserve: {len(parsed)} ) {", ".join(texture_defs)} ],
}};
'''
    ANB_OUT.mkdir(parents=True, exist_ok=True)
    (ANB_OUT / "teleportSparkle.anb.yc").write_text(text, encoding="utf-8", newline="\n")


def write_palette() -> None:
    if PALETTE_OUT.exists():
        PALETTE_OUT.unlink()


def validate(name: str, indexed: Image.Image, expected: tuple[int, int, int]) -> None:
    used = set(indexed.getdata()) - {0}
    if used != set(expected):
        raise ValueError(f"{name}: expected opaque indexes {expected}, found {sorted(used)}")


def main() -> None:
    if not SOURCE_SHEET.is_file():
        raise SystemExit(f"missing generated concept sheet: {SOURCE_SHEET}")
    sheet = Image.open(SOURCE_SHEET).convert("RGBA")
    write_palette()
    PNG_OUT.mkdir(parents=True, exist_ok=True)
    write_teleport_ring_anb()
    write_native_teleport_sparkle_anb()
    for name, (crop, size) in CONCEPT_ASSETS.items():
        editable = EDITABLE / f"{name}.png"
        if editable.is_file():
            editable_size = Image.open(editable).size
            indexed = editable_to_indexed(editable, editable_size, ASSET_RAMPS[name])
        else:
            image = nearest_resize(sheet.crop(crop), size)
            indexed = concept_to_indexed(name, image)
            if name == "rug":
                indexed = add_kear_to_rug(indexed)
        validate(name, indexed, ASSET_RAMPS[name])
        write_png(name, indexed)
        write_anb(name, indexed)
    for bit, name, path in BOSSES:
        asset_name = f"statue_{bit:02d}_{name}"
        editable = EDITABLE / f"{asset_name}.png"
        indexed = (
            editable_to_indexed(editable, Image.open(editable).size, ASSET_RAMPS["statue"])
            if editable.is_file()
            else boss_statue(path)
        )
        validate(f"statue_{name}", indexed, ASSET_RAMPS["statue"])
        write_png(asset_name, indexed)
        write_anb(asset_name, indexed)
    print(f"Built 5 room assets and {len(BOSSES)} boss statues; every sprite is 3 colors + transparency")


if __name__ == "__main__":
    main()
