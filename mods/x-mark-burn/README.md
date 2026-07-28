# X Mark Burn

The native runtime component for an experimental X Mark/Burn combat system.
It tracks eligible enemies, manages mark and burn state, and coordinates the
runtime visual effects used by the companion Clashrend Claymore data mod.

## Build

The repository-level pipeline is the preferred entry point:

```powershell
..\..\build.ps1 -Mode CodeOnly -ApiRoot ..\..\MinaModAPI
```

The local script remains available for runtime-only development:

```powershell
.\build.ps1 -ApiRoot ..\..\MinaModAPI
```

The generated headers under `src/generated/` are checked in deliberately.
They turn reviewed asset/profile data into compile-time tables and keep builds
independent of the private asset-authoring workspace.

Launch the finished mod with `-mod -mod-allow-code`. See
`MIGRATION_NOTES.md` for known API limitations and the remaining split between
ModAPI code and the runtime renderer.

## Source layout

`src/mod.cpp` owns the public ModAPI entry points and includes the ordered
implementation modules under `src/detail/`:

- `runtime.inl` — types, state, configuration, and timing
- `frame_bridge.inl` — input and rendered-frame observations
- `enemy_registry.inl` — enemy discovery and lifecycle tracking
- `rendering.inl` — HUD, markers, overlays, and render backends
- `effects.inl` — X Mark, crater, and Cross Blast effects
- `targeting.inl` — runtime target discovery and positioning
- `burn.inl` — burn state, palettes, damage, and cleanup
- `combat.inl` — contact probes and mark application
- `commands.inl` — debug commands and input processing
- `lifecycle.inl` — queues, world hooks, initialization, and shutdown

The modules remain in one translation unit. This preserves the original
anonymous-namespace linkage and initialization order while keeping each source
file reviewable.
