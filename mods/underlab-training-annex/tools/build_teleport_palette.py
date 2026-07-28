#!/usr/bin/env python3
"""Build the Annex teleport palette from Mina's native global palette."""

from __future__ import annotations

import sys
from pathlib import Path


MOD_ROOT = Path(__file__).resolve().parents[1]
GAME_ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(GAME_ROOT / "tools"))

import extract_ycd_pack  # noqa: E402


def main() -> None:
    pack = GAME_ROOT / "data" / "global_startup.pak.yc"
    entry_name = "palettes/global.pal.yc"
    entries = extract_ycd_pack.list_entries(pack)
    match = next((entry for entry in entries if entry[0] == entry_name), None)
    if match is None:
        raise RuntimeError(f"{entry_name} was not found in {pack}")

    _, start, size = match
    palette = bytearray(pack.read_bytes()[start : start + size])
    palette_base = 0xF0

    pad_cyan = (37, 226, 205, 255)
    radiant_cyan = (151, 255, 244, 255)
    for index, color in (
        (1, pad_cyan),
        (2, radiant_cyan),
        (38, pad_cyan),
        (43, radiant_cyan),
        (44, pad_cyan),
    ):
        offset = palette_base + (index - 1) * 4
        palette[offset : offset + 4] = bytes(color)

    target = MOD_ROOT / "data" / "palettes" / "underlabTrainingAnnex" / "teleportBlue.pal.yc"
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(palette)
    print(f"Built radiant-blue native teleport palette: {target}")


if __name__ == "__main__":
    main()
