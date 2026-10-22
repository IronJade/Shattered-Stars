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

## Field movies (embedded code section 5, #3103)

`game/field/field_movie.cpp`: maps whose container carries section 5 load #3103 at
0x801e9000, a field movie player that is word for word the title's (it reuses
`game::Movie` / `game::Prologue`) plus field parts: compose (0x801e9000, the frame and the
map's sprites composited), the 24-to-15-bit conversion (0x801ea1d0), the B_6f sequence
(0x801ea374), B_50 / B_51 / B_53, the B_16 / B_70 preload (0x801eac28) and the
picture-in-picture draw (0x801ead88). While a movie plays the BG, the SceneRenderer and
DrawOTag stand down. Field movies are 320x240 shown 224 rows high (the frame size comes
from the STR sector header). Map 548's movie matches Redux to a mean 0.4/255 per frame,
about 10 vblanks early (stream start latency, shared with the title movies). Section 5 also
sets `shadow_boxes` (0x800756b8): shadows drawn as boxes.

The field's own RotMatrix (0x8006e52c) adds cz·sx + B·cx in m21 where libgte subtracts; every
field call site the original routes through it now calls `field_rot_matrix` (actor view,
battle swirl, world-map camera); map 129 (scene roll 3882) matches Redux.

## Known differences / open

- `psyq::SetDrawMode(..., nullptr)` writes 0xe2000000 as the second word where libgpu
  writes 0; the effects write their DR_MODE words directly.
- The B_16 / B_70 preload always reads the whole entry (the console's size depends on free
  heap; a partial preload decides whether the sequence gets its sound bank) and the main loop
  runs once per vblank during movies — both still to be checked against Redux.
