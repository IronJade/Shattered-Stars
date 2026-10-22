# Field visual effects and the SceneRenderer

Port: `engine/src/game/field/effects*.cpp`, `scene_renderer.cpp` (effects agent). Catalog
rows: module `field_ovl_c1d` 0x8007e000-0x8008d928 and the field SceneRenderer / camera
block functions.

## How it runs

- **SceneRenderer** (0x8005f5c8, 8 bytes, task priority 1; global 0x800752d8): each frame
  it flips and clears the four double-buffered OT layers, takes the BG view origin and
  updates every particle between PushMatrix / PopMatrix.
- **Splicing:** `actor_sys_draw` splices each layer that something drew into into the
  screen OT at layer id + 0x11, then sorts the actors on that layer into it by depth (map
  588's dragon draws inside layer 0x7f, as on the console). `actor_link_prims` inserts
  after the first element of the entry (assembly 0x8003fc00), not directly after it.
- **Camera:** `camera_update_matrices` builds the view matrix (0x80075ac8), the effects'
  copy (0x80075b44) and the light and colour matrices; on map 588 they equal the RAM dump.
- **Effects** (script commands): ring B 0x7d, streaks B 0x7b, textured ring A 0x13, beam
  B 0x0b, burst A 0x12, edge glow ext 0x79; also spark, lit sphere, triangle and spinner,
  which no script on either disc uses.

## Verification

Each effect a script uses was rebuilt one update back from a PCSX-Redux RAM dump, run,
and its primitives compared with the dump's OT layer: word-for-word identical (ring map
588, streaks 574, textured ring 61, beam / burst 575, glow 548). Whole-frame comparisons
differ only by GPU-level steps (one gouraud step, sampling) and the random screen shake /
streak positions. `tests/test_field_effects.cpp` has 7 unit tests and 4 dump tests
(`SS_FX_REF=<dir>`).

## Quirks kept from the original

- The line and burst drawers read a stale OTZ.
- The burst's OT index is not range-checked (the port drops a primitive that would fall
  outside the table).
- A second ext 0x79 leaves the old glow drawing.
- Only the lower hemisphere of the lit sphere is drawn.

## Known differences / open

- `psyq::SetDrawMode(..., nullptr)` writes 0xe2000000 as the second word where libgpu
  writes 0; the effects write their DR_MODE words directly.
- The field's own RotMatrix (0x8006e52c) adds cz·sx + B·cx in m21 where libgte subtracts;
  the call sites that use it are being moved off `psyq::RotMatrix`.
- Map 548 enters a different scene in the port with the same script globals (the embedded
  map code, container sections 5-12, is not ported yet).
