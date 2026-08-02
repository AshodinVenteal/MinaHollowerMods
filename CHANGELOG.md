# Clashrend Claymore Changelog

## 0.1.4-alpha

### X Mark and Burn

- Improved first-room and first-hit enemy discovery.
- Improved mark refresh, reapplication, multi-enemy tracking, and cleanup.
- Improved mark pinning and charged-slam Burn conversion across enemy states.
- Synchronized Burn timing, flame effects, palette restoration, damage ticks,
  and HUD state with the target lifetime.

### HUD

- Anchored X, Burn, and damage indicators to enemy health-bar endcaps.
- Suppressed mechanic HUD effects while menus and pause overlays are active.

### Cross Blast and Claymore Effects

- Kept the X, plus, X Cross Blast aligned across rooms and terrain conditions.
- Reduced inherited and duplicate explosion draws while retaining the final
  center burst and volcanic embers.
- Preserved the validated slash-smear and charged-slam crater behavior.

### Performance and Platforms

- Reduced repeated room, draw-host, enemy, mark, Burn, and Cross Blast scans.
- Reduced transition slowdown after visiting interiors.
- Added separate deterministic Windows and Steam Deck renderer builds.
- Added a dedicated Proton descriptor path for the f0029 and f0030 smears.
- Removed probes, test controls, saves, backups, and Training Annex content
  from the public package.

## 0.1.3-alpha

- Added the three-step X, plus, X Cross Blast and volcanic ember geyser.
- Locked Cross Blast to Mina's facing at release.
- Added opaque red, black, and cream held-charge flashes.
- Reduced Cross Blast center and outer-wave damage.
- Restricted crater texture matching to prevent screen-wide corruption.

## 0.1.2-alpha

- Reduced interior transition and busy-room slowdown.
- Clamped directional craters to their first-frame impact position.
- Prevented crater drift and stale attack data from moving later impacts.
- Updated the bundled ModAPI runtime and regular-play renderer profile.
