# Original bugs and how the port treats them

The port aims to play better than the original, not to copy its defects.

- **No slowdown.** The original drops to 3-vsync frames under heavy load, for example
  four Robinfakes firing at once. The port does not model CPU time and runs at full
  speed; a smoother game is the point. Comparisons with PCSX-Redux stop where the
  original slows down.
- **No old bugs.** Where the original has a real bug, the port does not reproduce it:
  - the original's code is kept as a comment at the spot, marked
    `// ORIGINAL BUG (original_bugs.md <id>): ...`;
  - the corrected behaviour is written below it;
  - the entry is listed here.

  When the bug changes something a PCSX-Redux comparison measures (RNG draws, timing),
  the faithful version is verified first and the fix goes in afterwards.
- **Not bugs.** Data formats are kept exactly, even where they hold the original's leftover
  memory: save files (the uninitialised pad word, the snapshot's tail, the icon-sheet
  bytes) must stay byte-compatible with real memory cards.

Status: **kept** = the port still does what the original does (to fix); **avoided** = the port
already does something safe instead; **fixed** = commented out and corrected; **decide** =
a quirk that may be the intended look or feel, so it needs a decision before changing.

## Battle (battle agent)

| Id | Where (original / port) | The bug | Status |
|---|---|---|---|
| B1 | ribbon_trail_update 0x80075a74 / battle/killer.cpp | xy1 / xy3 use cos(start angle) where cos(end) belongs, so the trail's far edge is skewed | kept |
| B2 | afterimage draw / battle/killer.cpp | copies drawn as `(head + 2i + 2) % count`: with an even count half the copies are drawn twice and half never | kept |
| B3 | particle line draw / battle/particle.cpp | the depth comes from the GTE OTZ register after RTPS, a stale value from the last AVSZ, so lines sort at the wrong depth | kept |
| B4 | shot_apply_damage 0x800548c4 / battle/shot.cpp | the redirect loop compares the random member with targets[tries] and walks past the list (stack garbage) | avoided (compares within the list) |
| B5 | lens_flare_draw / battle/fx_draw.cpp | patches the shared stage flare tables in place (sizes[0..1], intensity[0]) | decide |
| B6 | unit_move / battle/unit_move.cpp | an output vector's x / z start as stack garbage | avoided (starts at the unit's position) |
| B7 | voice pick / battle/voice.cpp | a member who isn't there reads past an array | avoided (taken as down) |
| B8 | sound handles / battle/bsfx.cpp | a bad handle (0xffffffff) reads past the 8 effect slots | avoided |

## Enemies (enemy agent)

| Id | Where | The bug | Status |
|---|---|---|---|
| E1 | arcmene blink test / enemy_mod_arcmene.cpp | compares an index with 0x3e8000, always true | kept |
| E2 | ericodus attack 5 / enemy_mod_ericodus.cpp | can reach a step no branch handles, freezing the monster in state 13 | kept |
| E3 | visseyer / jibril release search / enemy_mod_visseyer.cpp, enemy_mod_jibril.cpp | the even tries store the z into the member's z velocity (+0x30) instead of its z | kept |
| E4 | bark vortex ring / enemy_mod_bark.cpp | the ring heights shift a 20.12 value by 12 again, right only at height 0 | kept |
| E5 | marsilio afterimage / enemy_mod_marsilio.cpp | the afterimage ends on a hit, not at the end of the swing | decide |
| E6 | shigeo choose_target / enemy_mod_shigeo.cpp | the 0x291c0 chance test always passes | decide (changes how often it attacks) |
| E7 | celesta status_test / enemy_mod_celesta.cpp | returns bit 24 of the mask argument instead of the status test | kept |
| E8 | indalecio step 6 / enemy_mod_indalecio.cpp | the halo afterimage is never made | kept |
| E9 | carlaeagle feathers / enemy_mod_carlaeagle.cpp | every feather starts 0x78 above y 0 rather than above the bird | decide |
| E10 | indalecio funnel / enemy_mod_indalecio.cpp | the funnel has already turned +0x34 this frame when it is read | decide |
| E11 | bunny / enemy_mod_bunny.cpp | when no point is found, an uninitialised stack slot is read | avoided (0) |
| E12 | enemy_support.cpp | the colour register holds garbage for an unknown mode | avoided (0) |

## Field effects and map code (effects agent)

| Id | Where | The bug | Status |
|---|---|---|---|
| F1 | line and burst drawers / field/effects_draw.cpp | read a stale OTZ (no AVSZ first), so they sort at the wrong depth | kept |
| F2 | burst / field/effects_draw.cpp | the OT index is not range-checked | avoided (the primitive is dropped) |
| F3 | ext 0x79 / field effects | a second glow leaves the old one drawing | kept |
| F4 | lit sphere / field effects | only the lower hemisphere is drawn | decide |
| F5 | movie compose 0x801e9000 / field/field_movie.cpp | for a quad clipped at the screen edge the row is never added to y, so every row lands on its top row | kept |
| F6 | overlay 0xc1d layer clip / field BG | both DR_AREAs land after the sprites, so the clip never applies; the clip x uses y0 | avoided (the port skips the DR_AREAs, matching the hardware result) |
| F7 | glow_draw / field effects | a tint of 0x100 wraps to 0 in the clamp | kept |
| F8 | overlay 0xc1d sprite pieces / field sprites | the underfoot colour lives in a stack slot never cleared between calls; unscaled sprites divide the UV base too; vertically flipped pieces get no UVs; disabled or out-of-range polygons are still drawn; a corner off the cell grid ends the function early | kept (each to check) |
| F9 | mirror images / field/field_sub_overlays.cpp | the console leaves a stale child pointer on the world map | avoided (cleared) |
| F10 | world map / field/worldmap_internal.h | points beyond the horizon read past a table | avoided |

## Field, menus and the rest

| Id | Where | The bug | Status |
|---|---|---|---|
| W1 | post-battle report / field/post_battle.cpp | the report's fifth row (MAG) falls under the window's clip and is never seen | kept |
| W2 | vehicle hover / field/field_world.cpp | with no direction held, the throttle cap is an unset register | avoided (0, as Redux shows) |
| W3 | script A 0x3a / field/vm_services.cpp, script_ext.cpp | actor +0x0c / +0x14 get uninitialised stack values | avoided (left as they were) |

## Adding an entry

Give it the next id in its area. Name the original address and the port file, describe the
bug in one line, and set the status. When an agent fixes one, it changes the status to
**fixed** and keeps the commented-out original code next to the fix.
