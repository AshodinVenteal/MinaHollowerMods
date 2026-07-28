from __future__ import annotations

import os
import re
from pathlib import Path


MOD_ROOT = Path(__file__).resolve().parents[1]
SOURCE = MOD_ROOT / "data" / "levels" / "overworld" / "hub.tlb.yc"
DESTINATION = MOD_ROOT / "data" / "levels" / "gyms" / "gym_worldloadtest1.tlb.yc"
LEGACY_DESTINATION = MOD_ROOT / "data" / "levels" / "extra" / "evraArena.tlb.yc"

UNDERLAB_MIN_X = 2400.0
UNDERLAB_MAX_X = 2688.0
UNDERLAB_MIN_Y = 896.0
UNDERLAB_MAX_Y = 1064.0
KEEP_ENTITY_TYPES = {47, 91, 96}


def extract_entity_blocks(section: str) -> list[tuple[int, str]]:
    token = "ycTileLevel2Entity"
    blocks: list[tuple[int, str]] = []
    cursor = 0
    while True:
        token_start = section.find(token, cursor)
        if token_start < 0:
            return blocks
        brace_start = section.find("{", token_start + len(token))
        if brace_start < 0:
            raise ValueError("Entity token has no opening brace")
        depth = 0
        in_string = False
        escaped = False
        end = brace_start
        while end < len(section):
            char = section[end]
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
                    end += 1
                    break
            end += 1
        else:
            raise ValueError("Unterminated entity block")

        block = section[token_start:end]
        entity_type_match = re.search(r"\bm_entityType:\s*(\d+),", block)
        if not entity_type_match:
            raise ValueError("Entity block has no m_entityType")
        blocks.append((int(entity_type_match.group(1)), block))
        cursor = end


def entity_position(block: str) -> tuple[float, float] | None:
    match = re.search(
        r"\bm_position:\s*ycVec2\s*\{.*?\bx:\s*([-\d.]+),.*?\by:\s*([-\d.]+),",
        block,
        flags=re.DOTALL,
    )
    return (float(match.group(1)), float(match.group(2))) if match else None


def keep_underlab_entity(entity_type: int, block: str) -> bool:
    position = entity_position(block)
    if position is None:
        return False
    x, y = position
    if not (UNDERLAB_MIN_X <= x <= UNDERLAB_MAX_X and UNDERLAB_MIN_Y <= y <= UNDERLAB_MAX_Y):
        return False
    if entity_type in KEEP_ENTITY_TYPES:
        return True
    # Retain the four guild lamps.
    return entity_type == 48 and "animTiles/guildLamp.anb.yc" in block


def main() -> None:
    if not SOURCE.is_file():
        raise SystemExit(
            "Missing text-format hub level. Run Mina once with '-mod -unpak', "
            f"then retry. Expected: {SOURCE}"
        )

    source_text = SOURCE.read_text(encoding="utf-8-sig")
    entities_start = source_text.index("\tm_entities:")
    markers_start = source_text.index("\tm_markers:", entities_start)
    entity_section = source_text[entities_start:markers_start]
    all_entities = extract_entity_blocks(entity_section)
    kept = [(entity_type, block) for entity_type, block in all_entities if keep_underlab_entity(entity_type, block)]

    replacement = (
        f"\tm_entities: [ ( Reserve: {len(kept)} ) \n\t\t"
        + ", ".join(block for _, block in kept)
        + " ],\n"
    )
    output = source_text[:entities_start] + replacement + source_text[markers_start:]

    unique_types = sorted({entity_type for entity_type, _ in kept})
    unique_replacement = (
        "\tm_uniqueEntityTypes: [ ( Reserve: "
        f"{len(unique_types)} ) "
        + ", ".join(str(value) for value in unique_types)
        + " ],"
    )
    output, replacements = re.subn(
        r"\tm_uniqueEntityTypes:\s*\[.*?\],",
        unique_replacement,
        output,
        count=1,
        flags=re.DOTALL,
    )
    if replacements != 1:
        raise ValueError("Could not update m_uniqueEntityTypes")

    DESTINATION.parent.mkdir(parents=True, exist_ok=True)
    DESTINATION.write_text(output, encoding="utf-8", newline="\n")
    if LEGACY_DESTINATION.is_file():
        LEGACY_DESTINATION.unlink()
    print(
        f"Training level: cloned the empty UnderLab room; kept {len(kept)}/{len(all_entities)} "
        f"entities; types={unique_types}; output={DESTINATION}"
    )


if __name__ == "__main__":
    main()
