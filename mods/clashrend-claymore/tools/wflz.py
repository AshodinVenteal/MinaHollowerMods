"""Minimal wfLZ decompressor for Mina asset inspection."""

from __future__ import annotations

import struct


def compressed_size(data: bytes, offset: int = 0) -> int:
    if data[offset : offset + 4] != b"WFLZ":
        raise ValueError("missing WFLZ signature")
    return struct.unpack_from("<I", data, offset + 4)[0] + 16


def decompressed_size(data: bytes, offset: int = 0) -> int:
    if data[offset : offset + 4] != b"WFLZ":
        raise ValueError("missing WFLZ signature")
    return struct.unpack_from("<I", data, offset + 8)[0]


def decompress(data: bytes, offset: int = 0) -> bytes:
    if data[offset : offset + 4] != b"WFLZ":
        raise ValueError("missing WFLZ signature")

    expected_size = struct.unpack_from("<I", data, offset + 8)[0]
    dist, length, num_literals = struct.unpack_from("<HBB", data, offset + 12)
    cursor = offset + 16
    out = bytearray()

    while True:
        if num_literals:
            out.extend(data[cursor : cursor + num_literals])
            cursor += num_literals

        dist, length, num_literals = struct.unpack_from("<HBB", data, cursor)
        cursor += 4
        if dist == 0 and length == 0 and num_literals == 0:
            break

        if length:
            copy_len = length + 4
            start = len(out) - dist
            if start < 0:
                raise ValueError("invalid wfLZ back-reference")
            for index in range(copy_len):
                out.append(out[start + index])

    if len(out) != expected_size:
        raise ValueError(f"decompressed {len(out)} bytes, expected {expected_size}")
    return bytes(out)
