#!/usr/bin/env python3
"""Build the Annex menu override from the active XMark/Claymore binary table.

String tables replace one another wholesale. Starting from XMarkBurn's table
preserves its Claymore names while a size-preserving patch adds the Annex title.
The options reset-description row supplies storage for the Annex label. The
runtime uses one-based room labels, so room index 1 requests area_name_test_2.
An unused No Underlabs cheat row already occupies that key's hash bin and is
retargeted to the stored Annex label without rebuilding the binary table.
"""

from __future__ import annotations

import struct
import sys
import os
from pathlib import Path


MOD_ROOT = Path(__file__).resolve().parents[1]
GAME_ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(GAME_ROOT / "tools"))

from extract_ycd_pack import list_entries  # noqa: E402
PACK = GAME_ROOT / "data" / "global_startup.pak.yc"
ENTRY_NAME = "menus/menus.stb.yc"
OUTPUT = MOD_ROOT / "data" / "menus" / "menus.stb.yc"
XMARK_SOURCES = (
    GAME_ROOT / "mod_workspace" / "modapi" / "XMarkBurn" / "data" / "menus" / "menus.stb.yc",
    Path(os.environ["APPDATA"])
    / "Yacht Club Games"
    / "Mina the Hollower"
    / "mods"
    / "XMarkBurn"
    / "data"
    / "menus"
    / "menus.stb.yc",
)
HASH_BIN_COUNT = 804

AREA_KEY_HASH = 1708477250
OLD_AREA_VALUE_HASH = 2948953047
NEW_AREA_VALUE_HASH = 1270729766
OLD_ROOM_KEY_HASH = 760466839
NEW_ROOM_KEY_HASH = 1046428735
ROOM2_SOURCE_KEY_HASH = 3468365387
ROOM2_SOURCE_VALUE_HASH = 3107032216
NEW_ROOM2_KEY_HASH = 1092072635
OLD_ROOM_VALUE_HASH = 2374379357
NEW_ROOM_VALUE_HASH = 1115827885
INTRO_SEA_KEY_HASH = 3119953497
INTRO_SEA_VALUE_HASH = 1809261588
ANNEX_FULL_VALUE_HASH = 3812896539
ANNEX_FULL_LABEL = "UnderLab: Training Annex Area"


def unique_offset(data: bytes | bytearray, needle: bytes, label: str) -> int:
    offsets: list[int] = []
    cursor = 0
    while True:
        cursor = data.find(needle, cursor)
        if cursor < 0:
            break
        offsets.append(cursor)
        cursor += 1
    if len(offsets) != 1:
        raise RuntimeError(f"Expected one {label}, found {len(offsets)}")
    return offsets[0]


def unique_u32(data: bytes | bytearray, value: int, label: str) -> int:
    return unique_offset(data, struct.pack("<I", value), label)


def find_string_descriptor(
    data: bytes | bytearray,
    row_hash_offset: int,
    string_offset: int,
    old_length: int,
) -> int:
    # Twelve 32-bit value hashes precede the serialized string-reference array.
    for offset in range(row_hash_offset + 48, min(string_offset, row_hash_offset + 512), 4):
        if offset + 12 > len(data):
            break
        length, capacity = struct.unpack_from("<II", data, offset)
        relative = struct.unpack_from("<i", data, offset + 8)[0]
        if length == old_length and capacity == 16 and offset + 4 + relative == string_offset:
            return offset
    raise RuntimeError(
        f"Could not find the {old_length}-byte string descriptor targeting 0x{string_offset:x}"
    )


def replace_serialized_string(
    data: bytearray,
    row_hash_offset: int,
    old: str,
    new: str,
) -> None:
    old_bytes = old.encode("utf-8")
    new_bytes = new.encode("utf-8")
    if len(new_bytes) > len(old_bytes):
        raise RuntimeError(f"Replacement {new!r} does not fit in {old!r}")
    string_offset = unique_offset(data, old_bytes + b"\0", repr(old))
    descriptor = find_string_descriptor(data, row_hash_offset, string_offset, len(old_bytes))
    struct.pack_into("<I", data, descriptor, len(new_bytes))
    allocation_size = len(old_bytes) + 1
    replacement = new_bytes + b"\0" + bytes(allocation_size - len(new_bytes) - 1)
    data[string_offset : string_offset + allocation_size] = replacement


def retarget_serialized_string(
    data: bytearray,
    row_hash_offset: int,
    old: str,
    target: str,
) -> None:
    """Point an existing row value at a longer string stored by another row."""
    old_bytes = old.encode("utf-8")
    target_bytes = target.encode("utf-8")
    old_offset = unique_offset(data, old_bytes + b"\0", repr(old))
    target_offset = unique_offset(data, target_bytes + b"\0", repr(target))
    descriptor = find_string_descriptor(data, row_hash_offset, old_offset, len(old_bytes))
    struct.pack_into("<I", data, descriptor, len(target_bytes))
    struct.pack_into("<i", data, descriptor + 8, target_offset - (descriptor + 4))


def extract_startup_menu() -> bytearray:
    pack_data = PACK.read_bytes()
    entry = next((item for item in list_entries(PACK) if item[0] == ENTRY_NAME), None)
    if entry is None:
        raise RuntimeError(f"{ENTRY_NAME} is missing from {PACK}")
    _, start, size = entry
    return bytearray(pack_data[start : start + size])


def load_source_menu() -> tuple[bytearray, str]:
    for source in XMARK_SOURCES:
        if source.is_file():
            data = source.read_bytes()
            if data[:4] == b"YCD\0":
                return bytearray(data), str(source)
    return extract_startup_menu(), f"{PACK}:{ENTRY_NAME}"


def already_patched(data: bytearray) -> bool:
    return (
        data.count(b"UnderLab\0") == 1
        and data.count(b"area_name_test_1\0") == 1
        and data.count(b"area_name_test_2\0") == 1
        and data.count(ANNEX_FULL_LABEL.encode("utf-8") + b"\0") == 1
        and data.count(struct.pack("<I", NEW_AREA_VALUE_HASH)) == 1
        and data.count(struct.pack("<I", NEW_ROOM_KEY_HASH)) == 1
        and data.count(struct.pack("<I", NEW_ROOM2_KEY_HASH)) == 1
        and data.count(struct.pack("<I", NEW_ROOM_VALUE_HASH)) == 2
        and data.count(struct.pack("<I", ANNEX_FULL_VALUE_HASH)) == 1
    )


def main() -> None:
    if OLD_ROOM_KEY_HASH % HASH_BIN_COUNT != NEW_ROOM_KEY_HASH % HASH_BIN_COUNT:
        raise RuntimeError("The label-storage key no longer shares its binary hash bin")
    if ROOM2_SOURCE_KEY_HASH % HASH_BIN_COUNT != NEW_ROOM2_KEY_HASH % HASH_BIN_COUNT:
        raise RuntimeError("The room-2 key no longer shares its binary hash bin")

    data, source_name = load_source_menu()
    if already_patched(data):
        OUTPUT.parent.mkdir(parents=True, exist_ok=True)
        OUTPUT.write_bytes(data)
        print(f'Built merged menu override from {source_name}: "UnderLab: Training Annex Area"')
        return

    area_row = unique_u32(data, AREA_KEY_HASH, "area_name_test key hash")
    room_row = unique_u32(data, OLD_ROOM_KEY_HASH, "options_reset_desc key hash")
    room2_row = unique_u32(data, ROOM2_SOURCE_KEY_HASH, "cheat_name_noCheckpoint key hash")
    intro_sea_row = unique_u32(data, INTRO_SEA_KEY_HASH, "area_name_introSea key hash")

    replace_serialized_string(data, area_row, "Test Area", "UnderLab")
    replace_serialized_string(data, room_row, "options_reset_desc", "area_name_test_1")
    replace_serialized_string(
        data,
        room_row,
        "All options have been cleared!",
        ANNEX_FULL_LABEL,
    )
    retarget_serialized_string(data, room_row, ANNEX_FULL_LABEL, "Training Annex Area")
    replace_serialized_string(data, room2_row, "cheat_name_noCheckpoint", "area_name_test_2")
    retarget_serialized_string(data, room2_row, "No Underlabs", "Training Annex Area")
    retarget_serialized_string(data, intro_sea_row, "Tenebrous Sea", ANNEX_FULL_LABEL)

    struct.pack_into("<I", data, unique_u32(data, OLD_AREA_VALUE_HASH, "Test Area hash"), NEW_AREA_VALUE_HASH)
    struct.pack_into("<I", data, unique_u32(data, OLD_ROOM_KEY_HASH, "options_reset_desc hash"), NEW_ROOM_KEY_HASH)
    struct.pack_into("<I", data, unique_u32(data, OLD_ROOM_VALUE_HASH, "options reset English hash"), NEW_ROOM_VALUE_HASH)
    struct.pack_into("<I", data, unique_u32(data, ROOM2_SOURCE_KEY_HASH, "No Underlabs key hash"), NEW_ROOM2_KEY_HASH)
    struct.pack_into("<I", data, unique_u32(data, ROOM2_SOURCE_VALUE_HASH, "No Underlabs English hash"), NEW_ROOM_VALUE_HASH)
    struct.pack_into("<I", data, unique_u32(data, INTRO_SEA_VALUE_HASH, "Tenebrous Sea hash"), ANNEX_FULL_VALUE_HASH)

    if not already_patched(data):
        raise RuntimeError("Binary menu verification failed after patching")

    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT.write_bytes(data)
    print(f'Built merged menu override from {source_name}: "UnderLab: Training Annex Area"')


if __name__ == "__main__":
    main()
