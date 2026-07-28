"""Build a patched hammer.anb.yc from Clashrend frame PNGs.

Mina's hammer ANB stores each frame as a one-byte indexed image compressed with
wfLZ. The Clashrend frames are quantized into existing player/default palette
indexes that read closer to the Claymore's steel/red design:

  0 transparent
  14/8/23/4 steel ramp and outline
  28/1 red engine detail
  3 orange reactor accent
  26/13 upgraded white-gold body/glow

The wfLZ encoder here is intentionally small and conservative, but uses
backreferences where they keep the patched ANB within the original package slot.
"""

from __future__ import annotations

import argparse
import json
import math
import struct
from pathlib import Path

import export_anb_frames
from png_rgba import read_png_rgba


def read_u64(data: bytes | bytearray, offset: int) -> int:
    return struct.unpack_from("<Q", data, offset)[0]


def write_u64(data: bytearray, offset: int, value: int) -> None:
    struct.pack_into("<Q", data, offset, value)


def best_match(raw: bytes, pos: int) -> tuple[int, int] | None:
    max_length = min(259, len(raw) - pos)
    if max_length < 5:
        return None

    best_dist = 0
    best_len = 0
    start_min = max(0, pos - 0xFFFF)
    # Direct search is sufficient for small frame buffers.
    for start in range(pos - 1, start_min - 1, -1):
        if raw[start] != raw[pos]:
            continue
        length = 1
        while length < max_length and raw[start + length] == raw[pos + length]:
            length += 1
            if start + length >= pos and length >= pos - start:
                break
        if length >= 5 and length > best_len:
            best_len = length
            best_dist = pos - start
            if best_len == max_length:
                break

    # Extend repeated bytes with overlapping backreferences.
    if pos > 0 and raw[pos] == raw[pos - 1]:
        length = 1
        while length < max_length and raw[pos + length] == raw[pos]:
            length += 1
        if length >= 5 and length > best_len:
            best_len = length
            best_dist = 1

    if best_len >= 5:
        return best_dist, best_len
    return None


def parse_lz_items(raw: bytes) -> list[tuple[str, int | bytes, int]]:
    items: list[tuple[str, int | bytes, int]] = []
    pos = 0
    literal_run = bytearray()

    def flush_literals() -> None:
        nonlocal literal_run
        if literal_run:
            items.append(("lit", bytes(literal_run), 0))
            literal_run = bytearray()

    while pos < len(raw):
        match = best_match(raw, pos)
        if match is None:
            literal_run.append(raw[pos])
            pos += 1
            continue
        flush_literals()
        dist, length = match
        items.append(("match", dist, length))
        pos += length
    flush_literals()
    return items


def encode_wflz(raw: bytes) -> bytes:
    if not raw:
        return b"WFLZ" + struct.pack("<IIHBB", 4, 0, 0, 0, 0)

    items = parse_lz_items(raw)
    cursor = 0
    out = bytearray(b"WFLZ")
    out.extend(b"\0" * 8)

    initial = bytearray()
    while cursor < len(items) and items[cursor][0] == "lit" and len(initial) < 255:
        chunk = items[cursor][1]
        assert isinstance(chunk, bytes)
        take = min(255 - len(initial), len(chunk))
        initial.extend(chunk[:take])
        if take < len(chunk):
            items[cursor] = ("lit", chunk[take:], 0)
            break
        cursor += 1

    out.extend(struct.pack("<HBB", 0, 0, len(initial)))
    out.extend(initial)

    while cursor < len(items):
        item = items[cursor]
        if item[0] == "match":
            dist = int(item[1])
            length = int(item[2])
            cursor += 1
        else:
            dist = 0
            length = 0

        literals = bytearray()
        while cursor < len(items) and items[cursor][0] == "lit" and len(literals) < 255:
            chunk = items[cursor][1]
            assert isinstance(chunk, bytes)
            take = min(255 - len(literals), len(chunk))
            literals.extend(chunk[:take])
            if take < len(chunk):
                items[cursor] = ("lit", chunk[take:], 0)
                break
            cursor += 1

        out.extend(struct.pack("<HBB", dist, max(0, length - 4), len(literals)))
        out.extend(literals)

    out.extend(struct.pack("<HBB", 0, 0, 0))
    struct.pack_into("<I", out, 4, len(out) - 16)
    struct.pack_into("<I", out, 8, len(raw))
    return bytes(out)


def frame_png_for(frames_dir: Path, item: dict[str, object]) -> Path:
    matches = sorted(frames_dir.glob(f"clashrend_{int(item['index']):03d}_*.png"))
    if len(matches) != 1:
        raise FileNotFoundError(f"expected exactly one frame for index {int(item['index']):03d}, found {len(matches)}")
    return matches[0]


def pixel_luma(r: int, g: int, b: int) -> float:
    return r * 0.299 + g * 0.587 + b * 0.114


def quantize_base(r: int, g: int, b: int) -> int:
    lum = pixel_luma(r, g, b)
    red_score = r - max(g, b)
    warm_score = r - b
    if red_score > 60:
        return 28
    if warm_score > 95 and r > 170 and g > 90:
        return 3
    if lum < 74:
        return 14
    if lum > 230:
        return 4
    if lum > 175:
        return 23
    if lum > 130:
        return 8
    if lum > 74:
        return 14
    return 14


def quantize_upgraded(r: int, g: int, b: int) -> int:
    lum = pixel_luma(r, g, b)
    warm_score = r - b
    if warm_score > 75 and r > 170:
        return 13 if lum > 150 else 3
    if lum < 42:
        return 14
    if lum > 230:
        return 4
    if lum > 165:
        return 26
    if lum > 100:
        return 14
    return 14


def png_to_indices(path: Path, expected_w: int, expected_h: int, variant: str) -> bytes:
    width, height, rgba = read_png_rgba(path)
    if (width, height) != (expected_w, expected_h):
        raise ValueError(f"{path} is {width}x{height}; expected {expected_w}x{expected_h}")
    out = bytearray(width * height)
    upgraded = variant == "upgraded_white_gold"
    for index in range(width * height):
        offset = index * 4
        r, g, b, a = rgba[offset : offset + 4]
        if a <= 20:
            out[index] = 0
        elif upgraded:
            out[index] = quantize_upgraded(r, g, b)
        else:
            out[index] = quantize_base(r, g, b)
    return bytes(out)


def compact_fallback_indices(width: int, height: int, variant: str) -> bytes:
    """Tiny fallback for frames whose original compressed slots are very small."""
    raw = bytearray(width * height)
    body = 26 if variant == "upgraded_white_gold" else 23
    accent = 13 if variant == "upgraded_white_gold" else 28
    if width >= height:
        y = height // 2
        start = max(1, width // 4)
        end = min(width - 2, width - start - 1)
        if end < start:
            start = 0
            end = width - 1
        for x in range(start, end + 1):
            raw[y * width + x] = body
        # One centered color break still compresses into frame 31's 38-byte slot.
        for x in (width // 2 - 1, width // 2 + 1):
            if start <= x <= end:
                raw[y * width + x] = accent
    else:
        x = width // 2
        start = max(1, height // 4)
        end = min(height - 2, height - start - 1)
        if end < start:
            start = 0
            end = height - 1
        for y in range(start, end + 1):
            raw[y * width + x] = body
        for y in (height // 2 - 1, height // 2 + 1):
            if start <= y <= end:
                raw[y * width + x] = accent
    return bytes(raw)


def remap_indices(raw: bytes, mapping: dict[int, int]) -> bytes:
    return bytes(mapping.get(value, value) for value in raw)


def simplify_for_slot(raw: bytes, variant: str, slot_size: int) -> tuple[bytes, bytes, str]:
    """Try shape-preserving palette reductions before generic line fallback."""
    if variant == "upgraded_white_gold":
        candidates = [
            ("palette_simplified", {4: 26, 13: 26, 3: 13}),
            ("palette_silhouette", {4: 26, 26: 26, 13: 14, 3: 13}),
        ]
    else:
        candidates = [
            ("palette_simplified", {4: 23, 8: 14, 3: 28, 1: 28}),
            ("palette_silhouette", {4: 23, 23: 14, 8: 14, 3: 28, 1: 28}),
            ("palette_mark", {4: 14, 23: 14, 8: 14, 3: 28, 1: 28}),
        ]

    for method, mapping in candidates:
        candidate = remap_indices(raw, mapping)
        encoded = encode_wflz(candidate)
        if len(encoded) <= slot_size:
            return candidate, encoded, method
    encoded = b""
    return raw, encoded, "none"


def pad_wflz_chunk(encoded: bytes, slot_size: int) -> bytes:
    if len(encoded) > slot_size:
        raise ValueError(f"encoded chunk is {len(encoded)} bytes, slot is only {slot_size}")
    padded = bytearray(encoded)
    padded.extend(b"\0" * (slot_size - len(padded)))
    struct.pack_into("<I", padded, 4, slot_size - 16)
    return bytes(padded)


def build_anb_preserve_slots(
    original_anb: Path,
    layout_path: Path,
    manifest_path: Path,
    frames_dir: Path,
    out_path: Path,
) -> None:
    data = bytearray(original_anb.read_bytes())
    if data[:4] != b"YCD\0":
        raise ValueError(f"{original_anb} does not start with YCD magic")

    layout = json.loads(layout_path.read_text(encoding="utf-8"))
    manifest_items = json.loads(manifest_path.read_text(encoding="utf-8"))
    manifest = {int(item["index"]): item for item in manifest_items}
    original_frames = export_anb_frames.parse_frames(original_anb)

    if len(original_frames) != len(layout):
        raise ValueError(f"layout has {len(layout)} frames, original has {len(original_frames)}")

    report = []
    for offset, (item, frame) in enumerate(zip(layout, original_frames)):
        frame_index = int(item["index"])
        width = int(item["width"])
        height = int(item["height"])
        if frame_index != int(frame["index"]):
            raise ValueError(f"layout frame {frame_index} does not match original frame {int(frame['index'])}")
        frame_path = frame_png_for(frames_dir, item)
        variant = str(manifest[frame_index].get("variant", "base_red_steel"))
        raw = png_to_indices(frame_path, width, height, variant)
        encoded = encode_wflz(raw)
        next_start = (
            int(original_frames[offset + 1]["wflz_offset"])
            if offset + 1 < len(original_frames)
            else len(data)
        )
        start = int(frame["wflz_offset"])
        slot_size = next_start - start
        fallback = False
        fallback_method = ""
        if len(encoded) > slot_size:
            fallback = True
            raw, encoded, fallback_method = simplify_for_slot(raw, variant, slot_size)
            if not encoded:
                fallback_method = "compact_line"
                raw = compact_fallback_indices(width, height, variant)
                encoded = encode_wflz(raw)
        if len(encoded) > slot_size:
            raise ValueError(
                f"frame {frame_index} still encodes to {len(encoded)} bytes; "
                f"original slot is {slot_size}"
            )
        data[start:next_start] = pad_wflz_chunk(encoded, slot_size)
        report.append(
            {
                "index": frame_index,
                "width": width,
                "height": height,
                "variant": variant,
                "encoded_size": len(encoded),
                "slot_size": slot_size,
                "fallback": fallback,
                "fallback_method": fallback_method,
                "source": str(frame_path),
            }
        )

    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_bytes(data)
    (out_path.with_suffix(out_path.suffix + ".build_report.json")).write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(f"wrote {out_path} ({len(data)} -> {len(data)} bytes, preserve-slots)")
    print(f"frames={len(layout)}")
    print(f"fallbacks={sum(1 for item in report if item['fallback'])}")


def build_anb(
    original_anb: Path,
    layout_path: Path,
    manifest_path: Path,
    frames_dir: Path,
    out_path: Path,
    pad_to_original: bool,
) -> None:
    data = bytearray(original_anb.read_bytes())
    if data[:4] != b"YCD\0":
        raise ValueError(f"{original_anb} does not start with YCD magic")

    data_start = read_u64(data, 0x30)
    layout = json.loads(layout_path.read_text(encoding="utf-8"))
    manifest_items = json.loads(manifest_path.read_text(encoding="utf-8"))
    manifest = {int(item["index"]): item for item in manifest_items}
    table_start = data_start - len(layout) * 32
    if table_start < 0:
        raise ValueError("computed frame table before start of file")

    prefix = data[:data_start]
    chunks = bytearray()
    report = []
    for item in layout:
        frame_index = int(item["index"])
        width = int(item["width"])
        height = int(item["height"])
        frame_path = frame_png_for(frames_dir, item)
        variant = str(manifest[frame_index].get("variant", "base_red_steel"))
        raw = png_to_indices(frame_path, width, height, variant)
        encoded = encode_wflz(raw)
        record_offset = table_start + frame_index * 32
        struct.pack_into("<I", prefix, record_offset + 16, len(encoded))
        chunks.extend(encoded)
        report.append(
            {
                "index": frame_index,
                "width": width,
                "height": height,
                "variant": variant,
                "encoded_size": len(encoded),
                "source": str(frame_path),
            }
        )

    patched = bytearray(prefix)
    patched.extend(chunks)
    if pad_to_original:
        if len(patched) > len(data):
            raise ValueError(f"rebuilt ANB is {len(patched)} bytes; original slot is only {len(data)} bytes")
        patched.extend(b"\0" * (len(data) - len(patched)))
    write_u64(patched, 0x38, len(patched))
    write_u64(patched, 0x40, len(patched))
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_bytes(patched)
    (out_path.with_suffix(out_path.suffix + ".build_report.json")).write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(f"wrote {out_path} ({len(data)} -> {len(patched)} bytes)")
    print(f"frames={len(layout)}")


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Package authored Clashrend frame PNGs into hammer.anb.yc."
    )
    parser.add_argument("--original-anb", type=Path, required=True)
    parser.add_argument("--layout", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--frames-dir", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--no-pad-to-original", action="store_true")
    parser.add_argument("--preserve-slots", action="store_true")
    args = parser.parse_args()
    if args.preserve_slots:
        build_anb_preserve_slots(args.original_anb, args.layout, args.manifest, args.frames_dir, args.out)
        return
    build_anb(args.original_anb, args.layout, args.manifest, args.frames_dir, args.out, not args.no_pad_to_original)


if __name__ == "__main__":
    main()
