# Mina the Hollower mods

Welcome! Here's some source code for the mods I've been working on.

This repository contains source and packaging tools for Clashrend Claymore and
its X Mark / Burn runtime:

- **Clashrend Claymore** provides the artist-facing sprite-to-ANB packaging
  pipeline for the custom weapon.
- **X Mark / Burn** implements the runtime side of the X Mark/Burn combat system.

The current packaged alpha is
[v0.1.4-alpha](https://github.com/AshodinVenteal/MinaHollowerMods/releases/tag/v0.1.4-alpha).
Release notes are tracked in [CHANGELOG.md](CHANGELOG.md).

The native runtime targets Windows and C++17. It requires the experimental
[MinaModAPI](https://github.com/YachtClubGames/MinaModAPI) headers and a current
Visual Studio C++ toolchain.

`mods/underlab-training-annex` is retained as a legacy room-construction
example in source only. It is not part of the build or public release payload.

## Repository scope

This repository intentionally contains source code, build tooling, and small
generated C++ lookup tables only. It does not include compiled binaries,
extracted game archives, or copyrighted game assets. Build outputs are created
locally from a legally installed copy of the game. Make sure you have a legally owned copy of Mina the Hollower from Steam for best results.

Clashrend Claymore is a companion data mod used by the X Mark / Burn mod. Its
packaging pipeline is included, but edited or extracted game assets are not
redistributed here.

## Build pipeline

Clone MinaModAPI beside this repository. For a fast code-only build:

```powershell
git clone https://github.com/YachtClubGames/MinaModAPI.git
.\build.ps1 -Mode CodeOnly -ApiRoot .\MinaModAPI
```

This compiles the X Mark / Burn runtime to
`mods/x-mark-burn/build/mod.dll` without reading private game or art inputs.

For a complete local release build:

```powershell
.\build.ps1 -Mode Release `
  -ApiRoot .\MinaModAPI `
  -Version 0.1.4-alpha `
  -PayloadDataDirectory C:\private\ClashrendClaymore\data `
  -OriginalAnb C:\private\original\hammer.anb.yc `
  -Layout C:\private\hammer_frames.json `
  -FramesDirectory C:\private\clashrend-frames
```

Release mode compiles the runtime, creates a fresh staging tree, copies the
private payload data, rebuilds `data/player/hammer.anb.yc`, validates the
payload, writes checksums, and verifies the finished ZIP. Generated releases
remain ignored by Git and never use the installed mod directory as output.

## Status

These projects track an experimental API and may need updates after game or API
releases. X Mark / Burn is still a research-heavy implementation; its generated
tables are checked in so a source checkout is reproducible.
