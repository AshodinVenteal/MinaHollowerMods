# X Mark Burn ModAPI Migration Notes

Updated for MinaModAPI commit `5f6dd6662714e6ebd8781b0b30cbe1584874643d`
(`functions for animations, palettes, cameras, mouse`, 2026-07-19).

## Integrated

- Owned effect assets remain under `data/effects` and palettes under
  `data/palettes`.
- Enemy and combat-core handles use ModAPI weak pointers.
- Mark and burn lifecycle work runs from the entity-hit, entity-post-art, and
  HUD update queues instead of a full scene traversal every draw.
- Enemy visual attachment now retains the target `GameAnim` and reads
  `GameAnimGetWorldTransform` plus `GameAnimGetCurrentFrameBound`. This gives
  the marker the current sprite center and frame dimensions while it moves or
  changes animation.
- Burn clones the target animation palette, maps visible entries into three
  red intensity bands, and applies it only to that enemy. The original palette
  clone is restored when burn expires; all references are released on burn
  end, world destruction, and shutdown.
- The current SDK `MM_Rtti` struct ABI is supported.
- Mark, burn, HUD, and world-effect lifetimes now advance on one monotonic
  gameplay clock accumulated from `WorldGetElapsedTime`. Pause and time scale
  no longer require shifting every active deadline; wall time remains in use
  for hit evidence, queue watchdogs, probes, logging, and file polling.
- Player attack animation lookup caches the active `GameAnim` with a weak
  pointer. `smack`, `smack_D`, `smack_U`, and the charged-contact
  `attackHammerSwing` variants are read from sequence/frame/loop state, and a
  stable draw token is emitted only when that animation state changes. This
  ModAPI state is authoritative by default; the D3D frame bridge is retained
  as a compatibility fallback.
- Selected enemy animations receive a bounded, once-per-component property
  inspection. The report includes sequence timing, loop count, visibility,
  play rate, world transform, frame bounds, and recognized named properties.

## Useful New Hooks

- `GameAnimGetCurrentFrameBound` and `GameAnimGetWorldTransform` replace the
  old visual-host center estimate for authoritative enemy hosts.
- `GameAnimGetPalette`, `ClonePalette`, `PaletteWriteIndex`, and
  `GameAnimSetPalette` provide an owned burn-palette path without global atlas
  mutation.
- `GameAnimIsVisible` now rejects hidden player attack animations before they
  can arm hit evidence.
- `GameAnimGetCurrentFrameTime`, `GameAnimGetNumSeqFrames`,
  `GameAnimGetNumLoopsPlayed`, and `GameAnimGetPlayRate` are recorded for
  attack-control diagnosis without changing the authored animation speed.
- Camera conversion helpers can simplify world/screen diagnostics, but the
  API still does not expose the assembled native enemy-HP-bar endcap. HUD
  marker placement therefore remains on the existing guarded HUD layout path.

## Retained Compatibility Paths

- Native combat health and hit evidence remain authoritative for applying and
  consuming statuses.
- DebugDraw remains the stable renderer for the X mark, HUD marks, fire wisps,
  and damage popup.
- D3D12 remains responsible only for effects explicitly kept there, including
  the currently approved HammerCrater path.
- Texture-signature visual hosts remain a fallback when an entity has no
  resolvable `GameAnim`; they should not supersede a valid official host.

## Test Gate

The attachment source lock is intentionally not refreshed by this build.
Retest moving-enemy attachment, burn palette restoration, enemy death, Muriel,
and a scripted boss before accepting the new source hash into the lock.
