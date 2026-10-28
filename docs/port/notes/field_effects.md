# Field visual effects and the SceneRenderer

Port: `port/src/game/field/effects*.cpp`, `scene_renderer.cpp` (effects agent). Catalog
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

## The original's quirks (docs/port/notes/original_bugs.md)

- Fixed (F1): the line and burst drawers read a stale OTZ; the port averages their own
  points' depth. The dump tests look for those primitives in any slot of the layer.
- Avoided (F2): the burst's OT index is not range-checked (the port drops a primitive that
  would fall outside the table).
- Fixed (F3): a second ext 0x79 left the old glow drawing; the new one replaces it.
- Fixed (F7): a glow tint of 0x100 wrapped to 0.
- Kept, looks intended (F4): only the lower hemisphere of the lit sphere is drawn.

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

## Embedded map code at 0x801f9800 (sections 6-12)

`game/field/field_sub_overlays.cpp`. Each map that needs one carries a small overlay
linked at 0x801f9800. The port runs the native code only when the map carries the
section (`field_map_sub_sections()`); otherwise the old stand-ins run, so the VM unit
tests see no difference.

| Section | Entry | Maps | What it is | Checked against Redux |
|---|---|---|---|---|
| 8 | #3108 | 384, 598-602 | Walking along a scripted path (ladders, stairs): ext 0x9c / 0x9e, a player-control hook while 0x800761ea is set, the A 0x2a / 0x2b / ext 0xea globals, no encounters on a path | map 384: the leader's position equal at every 40th vblank to 1360 |
| 9 | #3106 | 139 | Mirror images: ext 0xbf adds a mirror line; a `fieldactor_draw` hook places each actor's reflected child | images match; 0-3 pixels one step off (Redux's interpolation, `gpu_accuracy.md`) |
| 10 | #3104 | 143 | The rainbow (special command 0): 24 rays x 8 bands of additive POLY_G4 | within one 5-bit step from +2 to +400 frames |
| 7, 11 | #3109, entry #2 | 688 | The Cooking Master TIME / PRESS gauge (A 0x33 / 0x35 / 0x37; updated after `task_run_all`); section 11 is the 0x30-byte item records A 0x33 reads | TIME row pixel-identical at +200 / +800 |
| 12 | #3105 | 581 | The final "fin." screen (A 0x40, never returns): the caption typed in, then the logo and "fin." at 640x480 with music | caption, logo and "fin." identical (the logo's misaligned upload, `gpu_accuracy.md`) |
| 6 | #3107 | 581 (A 0x2d) | The credits: a program of its own (`docs/port/notes/credits.md`, `game/credits`); A 0x2d tears the field down and runs it, and it starts the field again | movie opens within a vblank, name cards pixel-identical, frames within 2-3 LSB |

## Known differences / open

- `psyq::SetDrawMode(..., nullptr)` writes 0xe2000000 as the second word where libgpu
  writes 0; the effects write their DR_MODE words directly.
- The B_16 / B_70 preload chooses whole or partial as the console does, against the free
  heap measured in PCSX-Redux on map 576 (897444 bytes) less the preload table's buffers.
  Map 576 preloads whole and map 685 partially (so its sequence loads the music bank), but
  the figure was measured on one map only.
- During a movie the field's main loop runs about as many passes per vblank as the console
  (28 before the first frame, then 20 / 0 / 1 / 6 per frame against Redux's 18 / 0 / 1 / 5):
  each pass costs 1/28 vblank of host time and the compose 0.86 vblank per ~990 pixels,
  calibrated on map 548.
- (Resolved in phase 4: the end screen's logo was uploaded from a misaligned address,
  which the DMA aligns down; the mirror shadow's pixels were the GPU's interpolation. See
  `docs/port/notes/gpu_accuracy.md`.)
