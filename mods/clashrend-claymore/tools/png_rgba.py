"""Small PNG RGBA reader/writer for deterministic sprite tooling."""

from __future__ import annotations

import struct
import zlib
from pathlib import Path


PNG_SIG = b"\x89PNG\r\n\x1a\n"


def _paeth(a: int, b: int, c: int) -> int:
    p = a + b - c
    pa = abs(p - a)
    pb = abs(p - b)
    pc = abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    if pb <= pc:
        return b
    return c


def read_png_rgba(path: Path) -> tuple[int, int, bytearray]:
    data = path.read_bytes()
    if not data.startswith(PNG_SIG):
        raise ValueError(f"{path} is not a PNG")

    cursor = len(PNG_SIG)
    width = height = color_type = bit_depth = None
    palette: list[tuple[int, int, int]] = []
    transparency: bytes | None = None
    idat = bytearray()

    while cursor < len(data):
        size = struct.unpack_from(">I", data, cursor)[0]
        cursor += 4
        tag = data[cursor : cursor + 4]
        cursor += 4
        payload = data[cursor : cursor + size]
        cursor += size + 4

        if tag == b"IHDR":
            width, height, bit_depth, color_type, compression, filt, interlace = struct.unpack(
                ">IIBBBBB", payload
            )
            if bit_depth != 8 or compression != 0 or filt != 0 or interlace != 0:
                raise ValueError(f"unsupported PNG settings in {path}")
        elif tag == b"PLTE":
            palette = [
                tuple(payload[i : i + 3])  # type: ignore[arg-type]
                for i in range(0, len(payload), 3)
            ]
        elif tag == b"tRNS":
            transparency = payload
        elif tag == b"IDAT":
            idat.extend(payload)
        elif tag == b"IEND":
            break

    if width is None or height is None or color_type is None:
        raise ValueError(f"{path} has no PNG header")

    channels_by_type = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}
    if color_type not in channels_by_type:
        raise ValueError(f"unsupported PNG color type {color_type} in {path}")
    channels = channels_by_type[color_type]
    stride = width * channels
    raw = zlib.decompress(bytes(idat))
    rows: list[bytearray] = []
    source = 0
    previous = bytearray(stride)
    bpp = channels

    for _y in range(height):
        filter_type = raw[source]
        source += 1
        row = bytearray(raw[source : source + stride])
        source += stride
        for x in range(stride):
            left = row[x - bpp] if x >= bpp else 0
            up = previous[x]
            upper_left = previous[x - bpp] if x >= bpp else 0
            if filter_type == 1:
                row[x] = (row[x] + left) & 0xFF
            elif filter_type == 2:
                row[x] = (row[x] + up) & 0xFF
            elif filter_type == 3:
                row[x] = (row[x] + ((left + up) // 2)) & 0xFF
            elif filter_type == 4:
                row[x] = (row[x] + _paeth(left, up, upper_left)) & 0xFF
            elif filter_type != 0:
                raise ValueError(f"unsupported PNG filter {filter_type} in {path}")
        rows.append(row)
        previous = row

    rgba = bytearray(width * height * 4)
    out = 0
    for row in rows:
        if color_type == 0:
            for value in row:
                rgba[out : out + 4] = bytes((value, value, value, 255))
                out += 4
        elif color_type == 2:
            for i in range(0, len(row), 3):
                rgba[out : out + 4] = row[i : i + 3] + b"\xff"
                out += 4
        elif color_type == 3:
            for value in row:
                r, g, b = palette[value]
                a = transparency[value] if transparency and value < len(transparency) else 255
                rgba[out : out + 4] = bytes((r, g, b, a))
                out += 4
        elif color_type == 4:
            for i in range(0, len(row), 2):
                value, alpha = row[i], row[i + 1]
                rgba[out : out + 4] = bytes((value, value, value, alpha))
                out += 4
        elif color_type == 6:
            rgba[out : out + len(row)] = row
            out += len(row)

    return width, height, rgba


def write_png_rgba(path: Path, width: int, height: int, rgba: bytes | bytearray) -> None:
    if len(rgba) != width * height * 4:
        raise ValueError("RGBA buffer size does not match dimensions")

    rows = bytearray()
    for y in range(height):
        rows.append(0)
        start = y * width * 4
        rows.extend(rgba[start : start + width * 4])

    def chunk(tag: bytes, payload: bytes) -> bytes:
        return (
            struct.pack(">I", len(payload))
            + tag
            + payload
            + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF)
        )

    out = bytearray(PNG_SIG)
    out.extend(chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)))
    out.extend(chunk(b"IDAT", zlib.compress(bytes(rows), level=9)))
    out.extend(chunk(b"IEND", b""))
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(out)
