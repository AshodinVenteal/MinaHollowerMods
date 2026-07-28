from __future__ import annotations

import os
import re
from pathlib import Path


MOD_ROOT = Path(__file__).resolve().parents[1]
SOURCE = (
    Path(os.environ["APPDATA"])
    / "Yacht Club Games"
    / "Mina the Hollower"
    / "mods"
    / "unpak"
    / "data"
    / "menus"
    / "menus.stb.yc"
)
DESTINATION = MOD_ROOT / "data" / "menus" / "menus.stb.yc"
ROW_HASH_BIN_COUNT = 804


def rebuild_row_hash_bins(text: str) -> str:
    """Rebuild the lookup index after adding the Annex room-name row."""
    row_hashes = [
        int(value)
        for value in re.findall(
            r"ycStringTableRow\s*\{\s*"
            r"m_valuesHash:\s*\[\s*\(\s*Reserve:\s*12\s*\)\s*(\d+)",
            text,
            flags=re.DOTALL,
        )
    ]
    if len(row_hashes) != 2414:
        raise SystemExit(f"Expected 2414 menu rows, found {len(row_hashes)}")

    bins: list[list[int]] = [[] for _ in range(ROW_HASH_BIN_COUNT)]
    for row_index, row_hash in enumerate(row_hashes):
        bins[row_hash % ROW_HASH_BIN_COUNT].append(row_index)

    entries: list[str] = []
    for row_indices in bins:
        if not row_indices:
            entries.append("")
            continue
        contents = ", ".join(str(index) for index in row_indices)
        entries.append(
            "ycStringTableHashBin\n"
            "\t\t{\n"
            f"\t\t\tm_binContents: [ ( Reserve: {len(row_indices)} ) {contents} ],\n"
            "\t\t}"
        )

    replacement = (
        f"\tm_rowHashBins: [ ( Reserve: {ROW_HASH_BIN_COUNT} ) \n\t\t"
        + ", ".join(entries)
        + " ],"
    )
    start = text.index("\tm_rowHashBins:")
    end = text.rfind("\n};")
    if end <= start:
        raise SystemExit("Could not locate the menu string-table hash-bin section")
    return text[:start] + replacement + text[end:]


def main() -> None:
    text = SOURCE.read_text(encoding="utf-8-sig")
    key = '"area_name_test"'
    if text.count(key) != 1:
        raise SystemExit("Expected exactly one native area_name_test row")
    key_at = text.index(key)
    row_start = text.rfind("ycStringTableRow", 0, key_at)
    close_at = text.index("\t\t}, ycStringTableRow", key_at)
    row_end = close_at + len("\t\t},")
    replacement = (
        '\t\tycStringTableRow\n\t\t{\n'
        '\t\t\tm_valuesHash: [ ( Reserve: 12 ) '
        '1708477250, 1270729766, , , , , , , , , ,  ],\n'
        '\t\t\tm_values: [ ( Reserve: 12 ) '
        '"area_name_test", "UnderLab", , , , , , , , , ,  ],\n'
        '\t\t}, ycStringTableRow\n\t\t{\n'
        '\t\t\tm_valuesHash: [ ( Reserve: 12 ) '
        '1046428735, 1115827885, , , , , , , , , ,  ],\n'
        '\t\t\tm_values: [ ( Reserve: 12 ) '
        '"area_name_test_1", "Training Annex Area", , , , , , , , , ,  ],\n'
        '\t\t},'
    )
    text = text[:row_start] + replacement + text[row_end:]
    text = text.replace("m_rows: [ ( Reserve: 2413 )", "m_rows: [ ( Reserve: 2414 )", 1)
    text = rebuild_row_hash_bins(text)
    DESTINATION.parent.mkdir(parents=True, exist_ok=True)
    DESTINATION.write_text(text, encoding="utf-8", newline="\n")
    print('Annex pause location: "UnderLab: Training Annex Area"')


if __name__ == "__main__":
    main()
