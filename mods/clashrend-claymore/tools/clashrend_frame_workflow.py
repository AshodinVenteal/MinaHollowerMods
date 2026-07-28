"""Split and recompose the Clashrend hammer-layout sprite sheet.

This keeps the official hammer atlas dimensions intact while letting us edit
individual Clashrend frames as normal PNGs.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from png_rgba import read_png_rgba, write_png_rgba


SLASH_FRAMES = {0, 2, 4, 14, 34, 35, 52, 53, 54, 55, 71, 73, 75}
WEAPON_PIECE_FRAMES = {1, 3, 5, 68, 70, 72, 74, 76}
ENGINE_IMPACT_FRAMES = set(range(19, 25)) | set(range(36, 52))


def frame_kind(frame_index: int) -> str:
    """Return the artist-facing role used in generated frame filenames."""
    if frame_index in WEAPON_PIECE_FRAMES:
        return "weapon_piece"
    if frame_index in SLASH_FRAMES:
        return "exhaust_slash"
    if frame_index in ENGINE_IMPACT_FRAMES:
        return "engine_impact"
    return "fragment"


DEFAULT_LAYOUT = Path("mod_workspace/sprites/hammer/hammer_frames.json")
DEFAULT_SHEET = Path("mod_workspace/clashrend_design/merged/clashrend_official_hammer_layout_merged.png")
DEFAULT_FRAMES_DIR = Path("mod_workspace/clashrend_design/frames/exact")
DEFAULT_PREVIEW_DIR = Path("mod_workspace/clashrend_design/frames/preview_4x")
DEFAULT_COMPOSED = Path("mod_workspace/clashrend_design/merged/clashrend_official_hammer_layout_from_frames.png")


def load_layout(path: Path) -> list[dict[str, int]]:
    return json.loads(path.read_text(encoding="utf-8"))


def frame_name(prefix: str, item: dict[str, int]) -> str:
    index = int(item["index"])
    width = int(item["width"])
    height = int(item["height"])
    return f"{prefix}_{index:03d}_{frame_kind(index)}_{width}x{height}.png"


def crop_rgba(rgba: bytearray, sheet_w: int, x: int, y: int, width: int, height: int) -> bytearray:
    out = bytearray(width * height * 4)
    for yy in range(height):
        src = ((y + yy) * sheet_w + x) * 4
        dst = yy * width * 4
        out[dst : dst + width * 4] = rgba[src : src + width * 4]
    return out


def paste_rgba(dst: bytearray, dst_w: int, src: bytearray, x: int, y: int, width: int, height: int) -> None:
    for yy in range(height):
        dst_offset = ((y + yy) * dst_w + x) * 4
        src_offset = yy * width * 4
        dst[dst_offset : dst_offset + width * 4] = src[src_offset : src_offset + width * 4]


def scale_nearest(width: int, height: int, rgba: bytearray, scale: int) -> tuple[int, int, bytearray]:
    out_w = width * scale
    out_h = height * scale
    out = bytearray(out_w * out_h * 4)
    for y in range(height):
        for x in range(width):
            src = (y * width + x) * 4
            for yy in range(scale):
                for xx in range(scale):
                    dst = ((y * scale + yy) * out_w + x * scale + xx) * 4
                    out[dst : dst + 4] = rgba[src : src + 4]
    return out_w, out_h, out


def find_frame(frames_dir: Path, prefix: str, item: dict[str, int]) -> Path:
    expected = frames_dir / frame_name(prefix, item)
    if expected.exists():
        return expected

    matches = sorted(frames_dir.glob(f"{prefix}_{int(item['index']):03d}_*.png"))
    if len(matches) == 1:
        return matches[0]
    if not matches:
        raise FileNotFoundError(f"missing frame {int(item['index']):03d} in {frames_dir}")
    raise ValueError(f"multiple frame files match index {int(item['index']):03d}: {matches}")


def split_sheet(
    sheet_path: Path,
    layout_path: Path,
    frames_dir: Path,
    preview_dir: Path | None,
    prefix: str,
    preview_scale: int,
) -> None:
    sheet_w, sheet_h, rgba = read_png_rgba(sheet_path)
    layout = load_layout(layout_path)
    frames_dir.mkdir(parents=True, exist_ok=True)
    if preview_dir is not None:
        preview_dir.mkdir(parents=True, exist_ok=True)

    manifest = []
    for item in layout:
        width = int(item["width"])
        height = int(item["height"])
        x = int(item["x"])
        y = int(item["y"])
        if x + width > sheet_w or y + height > sheet_h:
            raise ValueError(f"frame {int(item['index']):03d} exceeds sheet bounds")

        frame = crop_rgba(rgba, sheet_w, x, y, width, height)
        name = frame_name(prefix, item)
        write_png_rgba(frames_dir / name, width, height, frame)

        if preview_dir is not None:
            pw, ph, preview = scale_nearest(width, height, frame, preview_scale)
            write_png_rgba(preview_dir / name, pw, ph, preview)

        manifest.append(
            {
                "file": name,
                "index": int(item["index"]),
                "kind": frame_kind(int(item["index"])),
                "width": width,
                "height": height,
                "sheet_x": x,
                "sheet_y": y,
            }
        )

    (frames_dir / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    print(f"split {len(layout)} frames from {sheet_w}x{sheet_h} sheet")
    print(f"wrote exact frames: {frames_dir}")
    if preview_dir is not None:
        print(f"wrote {preview_scale}x previews: {preview_dir}")


def compose_sheet(
    frames_dir: Path,
    layout_path: Path,
    reference_sheet: Path | None,
    out_sheet: Path,
    prefix: str,
    preview_path: Path | None,
    preview_scale: int,
) -> None:
    layout = load_layout(layout_path)
    if reference_sheet is not None and reference_sheet.exists():
        sheet_w, sheet_h, _ = read_png_rgba(reference_sheet)
    else:
        sheet_w = max(int(item["x"]) + int(item["width"]) for item in layout) + 2
        sheet_h = max(int(item["y"]) + int(item["height"]) for item in layout) + 2
    output = bytearray(sheet_w * sheet_h * 4)

    for item in layout:
        expected_w = int(item["width"])
        expected_h = int(item["height"])
        frame_path = find_frame(frames_dir, prefix, item)
        frame_w, frame_h, rgba = read_png_rgba(frame_path)
        if (frame_w, frame_h) != (expected_w, expected_h):
            raise ValueError(
                f"{frame_path} is {frame_w}x{frame_h}; expected {expected_w}x{expected_h}"
            )
        paste_rgba(output, sheet_w, rgba, int(item["x"]), int(item["y"]), expected_w, expected_h)

    out_sheet.parent.mkdir(parents=True, exist_ok=True)
    write_png_rgba(out_sheet, sheet_w, sheet_h, output)
    print(f"composed {len(layout)} frames into {out_sheet} ({sheet_w}x{sheet_h})")

    if preview_path is not None:
        pw, ph, preview = scale_nearest(sheet_w, sheet_h, output, preview_scale)
        preview_path.parent.mkdir(parents=True, exist_ok=True)
        write_png_rgba(preview_path, pw, ph, preview)
        print(f"wrote {preview_scale}x composed preview: {preview_path} ({pw}x{ph})")


def main() -> None:
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="command", required=True)

    split = subparsers.add_parser("split")
    split.add_argument("--sheet", type=Path, default=DEFAULT_SHEET)
    split.add_argument("--layout", type=Path, default=DEFAULT_LAYOUT)
    split.add_argument("--frames-dir", type=Path, default=DEFAULT_FRAMES_DIR)
    split.add_argument("--preview-dir", type=Path, default=DEFAULT_PREVIEW_DIR)
    split.add_argument("--prefix", default="clashrend")
    split.add_argument("--preview-scale", type=int, default=4)
    split.add_argument("--no-preview", action="store_true")

    compose = subparsers.add_parser("compose")
    compose.add_argument("--frames-dir", type=Path, default=DEFAULT_FRAMES_DIR)
    compose.add_argument("--layout", type=Path, default=DEFAULT_LAYOUT)
    compose.add_argument("--reference-sheet", type=Path, default=DEFAULT_SHEET)
    compose.add_argument("--out-sheet", type=Path, default=DEFAULT_COMPOSED)
    compose.add_argument("--prefix", default="clashrend")
    compose.add_argument(
        "--preview",
        type=Path,
        default=Path("mod_workspace/clashrend_design/merged/clashrend_official_hammer_layout_from_frames_4x.png"),
    )
    compose.add_argument("--preview-scale", type=int, default=4)
    compose.add_argument("--no-preview", action="store_true")

    args = parser.parse_args()
    if args.command == "split":
        split_sheet(
            args.sheet,
            args.layout,
            args.frames_dir,
            None if args.no_preview else args.preview_dir,
            args.prefix,
            args.preview_scale,
        )
    elif args.command == "compose":
        compose_sheet(
            args.frames_dir,
            args.layout,
            args.reference_sheet,
            args.out_sheet,
            args.prefix,
            None if args.no_preview else args.preview,
            args.preview_scale,
        )


if __name__ == "__main__":
    main()
