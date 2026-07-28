# UnderLab Training Annex

> Legacy example: retained as a reference for constructing and connecting
> custom rooms. The repository's main pipeline does not build or package it.

An isolated combat-test room connected to the UnderLab's right side.

The annex has three respawning targets with 50, 150, and 500 HP. Save writes
are suspended while the room is active and restored on return or clean
shutdown. `F8` is an emergency enter/return shortcut.

## Build

Run Mina once with `-mod -unpak` so the build tools can derive the room data
from your own game installation. Then build the native component:

```powershell
.\build.ps1 -ApiRoot ..\..\MinaModAPI
```

The Python tools in `tools/` generate the level and menu assets. They operate on
local game data; generated files are ignored by Git.

Launch the finished mod with `-mod -mod-allow-code`.
