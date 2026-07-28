# Clashrend Claymore sprite pipeline

This is the source pipeline used to turn the authored Clashrend hammer sheet
into Mina's `hammer.anb.yc` format. It keeps the artist workflow separate from
the game-derived inputs:

1. Split a layout-compatible RGBA sheet into exact per-frame PNGs.
2. Edit those PNGs without changing their dimensions.
3. Recompose a review sheet and optional nearest-neighbor preview.
4. Quantize the edited pixels into the established Clashrend palette indices.
5. wfLZ-compress each frame and write it into the matching ANB frame slot.
6. Emit a JSON build report with sizes and any fallback used to fit a slot.

The default packaging command uses slot-preserving mode. That leaves the ANB
container size and frame-table layout unchanged, which is safer for replacement
inside the game's existing package.

## Requirements

- Python 3.10 or newer
- A legally installed copy of Mina the Hollower
- The original `hammer.anb.yc` extracted from your own installation
- A frame-layout JSON exported from that same ANB
- An authored Clashrend sheet matching the exported layout

No game files or finished packages are included in this repository.

## Usage

Use the repository-level `build.ps1 -Mode Release` pipeline for complete
payload and archive generation. The commands below run only the sprite stage.

First split the authored sheet:

```powershell
python .\tools\clashrend_frame_workflow.py split `
  --sheet .\art\clashrend-hammer.png `
  --layout .\private-input\hammer_frames.json `
  --frames-dir .\work\frames `
  --preview-dir .\work\preview
```

Edit the PNGs in `work/frames`, then package them:

```powershell
.\package-sprites.ps1 `
  -OriginalAnb .\private-input\hammer.anb.yc `
  -Layout .\private-input\hammer_frames.json `
  -FramesDirectory .\work\frames `
  -OutputAnb .\out\hammer.anb.yc
```

The split step writes `manifest.json`. Add an optional `variant` property to a
frame entry to select `upgraded_white_gold`; frames otherwise use
`base_red_steel`.

## Palette mapping

Transparent pixels become index `0`. Base sprites use the established dark
steel, light steel, red, and orange indices. Upgraded sprites use the white-gold
ramp. The packager chooses colors by luminance and hue, then uses conservative
palette reductions only when compressed data cannot fit the original slot.
