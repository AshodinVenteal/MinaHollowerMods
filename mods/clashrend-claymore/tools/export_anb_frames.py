#!/usr/bin/env python3
"""Export ANB wfLZ frame chunks as PNG previews and a contact sheet."""

from __future__ import annotations

import argparse
import json
import struct
import zlib
from pathlib import Path

import wflz


PREVIEW_PALETTE = [
    (0, 0, 0, 0),
    (35, 35, 35, 255),
    (142, 98, 48, 255),
    (255, 233, 197, 255),
]

for index in range(len(PREVIEW_PALETTE), 256):
    PREVIEW_PALETTE.append(
        (
            (index * 67) % 256,
            (index * 113) % 256,
            (index * 179) % 256,
            255,
        )
    )


def png_chunk(tag: bytes, payload: bytes) -> bytes:
    return (
        struct.pack(">I", len(payload))
        + tag
        + payload
        + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF)
    )


def rgba_from_indices(indices: bytes, width: int, height: int) -> bytes:
    rows = bytearray()
    for y in range(height):
        rows.append(0)
        row = indices[y * width : (y + 1) * width]
        for value in row:
            rows.extend(PREVIEW_PALETTE[value])
    return bytes(rows)


def write_png(path: Path, width: int, height: int, indices: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    payload = rgba_from_indices(indices, width, height)
    data = b"\x89PNG\r\n\x1a\n"
    data += png_chunk(b"IHDR", ihdr)
    data += png_chunk(b"IDAT", zlib.compress(payload, level=9))
    data += png_chunk(b"IEND", b"")
    path.write_bytes(data)


def find_wflz_offsets(data: bytes, start: int) -> list[int]:
    offsets: list[int] = []
    cursor = start
    while True:
        found = data.find(b"WFLZ", cursor)
        if found < 0:
            break
        offsets.append(found)
        cursor = found + 1
    return offsets


def parse_frames(path: Path) -> list[dict[str, object]]:
    data = path.read_bytes()
    if data[:4] != b"YCD\0":
        raise ValueError(f"{path} does not start with YCD magic")

    data_start = struct.unpack_from("<Q", data, 0x30)[0]
    offsets = find_wflz_offsets(data, data_start)
    table_start = data_start - len(offsets) * 32

    frames: list[dict[str, object]] = []
    for index, offset in enumerate(offsets):
        record_offset = table_start + index * 32
        width, height, origin_x, origin_y, packed_size = struct.unpack_from(
            "<IIIII", data, record_offset
        )
        raw = wflz.decompress(data, offset)
        if width * height != len(raw):
            raise ValueError(
                f"frame {index}: metadata says {width}x{height}, "
                f"but decoded {len(raw)} pixels"
            )
        frames.append(
            {
                "index": index,
                "width": width,
                "height": height,
                "origin_x": origin_x,
                "origin_y": origin_y,
                "record_offset": record_offset,
                "wflz_offset": offset,
                "wflz_relative_offset": offset - data_start,
                "packed_size": packed_size,
                "pixels": raw,
            }
        )
    return frames


def make_contact_sheet(frames: list[dict[str, object]], max_width: int, padding: int) -> tuple[int, int, bytes, list[dict[str, int]]]:
    placements: list[dict[str, int]] = []
    x = padding
    y = padding
    row_height = 0
    sheet_width = max_width

    for frame in frames:
        width = int(frame["width"])
        height = int(frame["height"])
        if x + width + padding > max_width and x != padding:
            x = padding
            y += row_height + padding
            row_height = 0
        placements.append({"index": int(frame["index"]), "x": x, "y": y})
        x += width + padding
        row_height = max(row_height, height)

    sheet_height = y + row_height + padding
    sheet = bytearray([0] * (sheet_width * sheet_height))
    for frame, placement in zip(frames, placements):
        width = int(frame["width"])
        height = int(frame["height"])
        pixels = frame["pixels"]
        for row in range(height):
            dest = (placement["y"] + row) * sheet_width + placement["x"]
            src = row * width
            sheet[dest : dest + width] = pixels[src : src + width]

    return sheet_width, sheet_height, bytes(sheet), placements


def export(path: Path, out_dir: Path, max_sheet_width: int) -> None:
    frames = parse_frames(path)
    name = path.stem.replace(".anb", "")
    frame_dir = out_dir / name / "frames"
    for frame in frames:
        frame_path = frame_dir / f"{name}_{int(frame['index']):03d}_{int(frame['width'])}x{int(frame['height'])}.png"
        write_png(frame_path, int(frame["width"]), int(frame["height"]), frame["pixels"])

    sheet_width, sheet_height, sheet_pixels, placements = make_contact_sheet(frames, max_sheet_width, 2)
    write_png(out_dir / name / f"{name}_sheet.png", sheet_width, sheet_height, sheet_pixels)

    metadata = []
    for frame, placement in zip(frames, placements):
        clean = {key: value for key, value in frame.items() if key != "pixels"}
        clean.update(placement)
        metadata.append(clean)
    metadata_path = out_dir / name / f"{name}_frames.json"
    metadata_path.write_text(json.dumps(metadata, indent=2), encoding="utf-8")

    print(f"exported {len(frames)} frames from {path}")
    print(f"wrote {out_dir / name}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("anb", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--sheet-width", type=int, default=320)
    args = parser.parse_args()
    export(args.anb, args.out, args.sheet_width)


if __name__ == "__main__":
    main()
