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
**keep** = decided by the user: the original's behaviour is intended and stays; **to fix** = decided
by the user as a bug, fix pending.

## Battle (battle agent)

| Id | Where (original / port) | The bug | Status |
|---|---|---|---|
| B1 | ribbon_trail_update 0x80075a74 / battle/killer.cpp | xy1 / xy3 use cos(start angle) where cos(end) belongs, so the trail's far edge is skewed | fixed (cos(end); the faithful version was verified with kind 0x16) |
| B2 | afterimage draw / battle/killer.cpp | copies drawn as `(head + 2i + 2) % count`: with an even count half the copies are drawn twice and half never | fixed (each copy once, newest first: `(head + i + 1) % count`) |
| B3 | particle line draw / battle/particle.cpp | the depth comes from the GTE OTZ register after RTPS, a stale value from the last AVSZ, so lines sort at the wrong depth | fixed (AVSZ4 over the line's two ends) |
| B4 | shot_apply_damage 0x800548c4 / battle/shot.cpp | the redirect loop compares the random member with targets[tries] and walks past the list (stack garbage) | avoided (compares within the list) |
| B5 | lens_flare_draw / battle/fx_draw.cpp | patches the shared stage flare tables in place (sizes[0..1], intensity[0]) | decide |
| B6 | unit_move / battle/unit_move.cpp | an output vector's x / z start as stack garbage | avoided (starts at the unit's position) |
| B7 | voice pick / battle/voice.cpp | a member who isn't there reads past an array | avoided (taken as down) |
| B8 | sound handles / battle/bsfx.cpp | a bad handle (0xffffffff) reads past the 8 effect slots | avoided |
| B9 | projectile_arc_velocity 0x8004906c / battle/unit_move.cpp | the jump arc's x is scaled by the speed but its z by a constant 24, so Bowman's leap (move 3, speed 10) misses the target in depth by up to 2.4x | fixed, pending the user's decision (z * speed; the faithful version was verified with Bowman's move 3 against Redux; reverting means uncommenting the original line) |
| B10 | Bowman move 6 0x801ef470 / battle/pc_bowman.cpp | the clones are placed at the target's four corners for i 0..3; a fifth clone (level 3) keeps the pool slot's old position | kept |

## Enemies (enemy agent)

| Id | Where | The bug | Status |
|---|---|---|---|
| E1 | arcmene blink test / enemy_mod_arcmene.cpp | compares an index with 0x3e8000, always true | fixed (the nearest member's distance within 1000 units; verified faithful first: Redux encounter 50) |
| E2 | ericodus attack 5 / enemy_mod_ericodus.cpp | can reach a step no branch handles, freezing the monster in state 13 | fixed (a block or a landing sets step 2) |
| E3 | visseyer / jibril release search / enemy_mod_visseyer.cpp, enemy_mod_jibril.cpp | the even tries store the z into the member's z velocity (+0x30) instead of its z | fixed |
| E4 | bark vortex ring / enemy_mod_bark.cpp | the ring heights shift a 20.12 value by 12 again, right only at height 0 | fixed |
| E5 | marsilio afterimage / enemy_mod_marsilio.cpp | the afterimage ends on a hit, not at the end of the swing | fixed (user, 2026-10-22: the afterimage should last to the end of the swing) |
| E6 | shigeo choose_target / enemy_mod_shigeo.cpp | the 0x291c0 chance test always passes | keep (user, 2026-10-22: intended. Shigeo is meant to be unbeatable in the first fight; in the second he holds no aggro and is a one-time fight the story needs won) |
| E7 | celesta status_test / enemy_mod_celesta.cpp | returns bit 24 of the mask argument instead of the status test | not a bug: effect word 0 bit 24 is stun immunity (unit_apply_stun), so she reports that one effect and nothing else: G-Celesta cannot be stunned |
| E8 | indalecio step 6 / enemy_mod_indalecio.cpp | the halo afterimage is never made | fixed (the first frame of step 6 is told by the fade +0x38, which step 5 clears) |
| E9 | carlaeagle feathers / enemy_mod_carlaeagle.cpp | every feather starts 0x78 above y 0 rather than above the bird | fixed (user, 2026-10-22: the feathers start at the bird and fan out toward the party) |
| E10 | indalecio funnel / enemy_mod_indalecio.cpp | the funnel has already turned +0x34 this frame when it is read | the cause of E8, not a second bug: fixed with it (the funnel itself turns as before) |
| E11 | bunny / enemy_mod_bunny.cpp | when no point is found, an uninitialised stack slot is read | avoided (0) |
| E12 | enemy_support.cpp | the colour register holds garbage for an unknown mode | avoided (0) |
| E13 | Noah module 6563c70f92 step 2 / battle/spell_mod_noah.cpp | as Foehn's S1: the wall's splashes (sounds, sparks, shakes) go to the caster's own side (party / monster lists swapped); the damage (spell_hit_all) is right | fixed (verified faithful first: Redux encounters 515, 539) |
| E14 | visseyer / jibril release / enemy_mod_visseyer.cpp, enemy_mod_jibril.cpp | a released member's facing x is set from its own x velocity (-vel x, 0 after being swallowed), so it comes out facing an arbitrary way instead of toward the monster | keep (user, 2026-10-22: released members are meant to come out facing a random way) |
| E15 | voice player 0x8007faa4 / 0x8007fc54 / 0x8007f76c / battle/voice_player.cpp | voice_update keeps the low byte of the clip handle (the voice buffer's address bits) where sound_handle_active and voice_stop_all later expect the sound slot, so a voice counts as playing only while its entry stays in its slot (freed when the stream ends, before the last samples play) and voice_stop_all never releases a playing clip | decide (kept for now: it sets how long a boss waits after its intro voice, which the Redux references measure; a fix would hold speakers to the end of the clip) |

## Field effects and map code (effects agent)

| Id | Where | The bug | Status |
|---|---|---|---|
| F1 | line and burst drawers / field/effects_draw.cpp | read a stale OTZ (no AVSZ first), so they sort at the wrong depth | fixed (avsz3 of their own points' depth, `otz_for_depth`) |
| F2 | burst / field/effects_draw.cpp | the OT index is not range-checked | avoided (the primitive is dropped) |
| F3 | ext 0x79 / field/effects.cpp `glow_spawn` | a second glow leaves the old one drawing | fixed (the new glow replaces the old) |
| F4 | lit sphere 0x8008c378 / field/effects_draw.cpp | only the lower hemisphere is drawn | keep (user, 2026-10-22: the intended look) |
| F5 | movie compose 0x801e9000 / field/field_movie.cpp | for a quad clipped at the screen edge the row is never added to y, so every row lands on its top row; the rows are also read with the clipped stride, the right edge stops at 0x13e, and a quad running past the right edge was written whole (past the movie's columns) | fixed (each row's visible columns written back at its own row) |
| F6 | overlay 0xc1d layer clip / field BG | both DR_AREAs land after the sprites, so the clip never applies; the clip x uses y0 | avoided (the port skips the DR_AREAs, matching the hardware result) |
| F7 | glow_draw / field/effects.cpp | a tint of 0x100 wraps to 0 in the clamp | fixed |
| F8 | overlay 0xc1d sprite pieces / field/actor_field.cpp `fieldactor_build_light_split` | (a) the underfoot colour lives in a stack slot never cleared between calls; (b) unscaled sprites divide the UV base too; (c) vertically flipped pieces get no UVs; (d) disabled or out-of-range polygons are still drawn; (e) a probe or corner off the cell grid ends the function early | fixed, all five (test_field_actors: the Redux trace comparison skips cache-B UVs (b) and accepts the 95 dup46 calls whose pieces are a subset of the original's (d); the list-4 triangle test expects the corrected UVs) |
| F9 | mirror images / field/field_sub_overlays.cpp | the console leaves a stale child pointer on the world map | avoided (cleared) |
| F10 | world map / field/worldmap_internal.h | points beyond the horizon read past a table | avoided |
| F11 | Precis move 8 0x801efb1c / battle/pc_precis.cpp | when the arm Shot cannot be spawned (a full pool) the beam is still drawn from a null Shot (address 0) | avoided (no beam) |
| F12 | Chisato move 6 0x801ef718 / battle/pc_chisato.cpp | the stars after the first are scattered by rng_spread(n << 7) units in x (through shot_place_at_unit) but rng_spread(n << 6) is added to z raw, in 20.12, so they never scatter in depth (at most 0.08 of a unit) | decide (looks like a missing << 12: fixing it scatters the later stars up to 320 units in depth, so more of them miss) |
| F13 | Opera move 7 burst 0x801f1098 / battle/pc_opera.cpp | the last of the lvl + 4 shards should fly straight up (-0x14000) but the store goes to the burst's own velocity, so that shard keeps 0 and drops from the burst point | to fix once the faithful version is verified (the shard gets the velocity; no RNG change) |
| F14 | Opera move 7 shard hit 0x801f129c / battle/pc_opera.cpp | the hit sprites' y jitter uses rng_between(-0xc0, 0) raw (in 20.12, under 0.05 of a unit) where the other shots use rng_between_fixed (192 units) | decide (visual only; the RNG draws are the same either way) |

## Field, menus and the rest

| Id | Where | The bug | Status |
|---|---|---|---|
| W1 | post-battle report / field/post_battle.cpp | the report's fifth row (MAG) falls under the window's clip and is never seen | kept |
| W2 | vehicle hover / field/field_world.cpp | with no direction held, the throttle cap is an unset register | avoided (0, as Redux shows) |
| W3 | script A 0x3a / field/vm_services.cpp, script_ext.cpp | actor +0x0c / +0x14 get uninitialised stack values | avoided (left as they were) |

## Menus (menu agent)

| Id | Where (original / port) | The bug | Status |
|---|---|---|---|
| M1 | rename_name_box_commit 0xbce 0x800804f4 / field/menu_rename.cpp | renaming in the camp menu compares Claude's new name with "Crawd" (the Japanese release's name; the title's New Game copy compares with "Claude"), so keeping or restoring "Claude" sets config +0x42, the renamed flag that switches the battle voices to the lines that avoid his name | fixed (compares with "Claude"; the faithful version was verified against Redux: a rename run, and one that keeps "Claude" and sets the flag) |
| M2 | rename_keyboard_confirm 0x8007fb98 and rename_screen's Select 0x8007e4dc / field/menu_rename.cpp | once the name has seven characters the cursor jumps to column 3 of the top row, Delete (the title's copy jumps to column 2, OK), so the next Cross deletes the character just typed | fixed (jumps to OK; the faithful version was verified against Redux first) |
| M3 | rename_run 0x8007e308 / field/menu_rename.cpp | the screen writes CLUT rows 0x1e0 / 0x1e1 at x 0-0xff and art 66 columns wide at (0x140, 0), but saves and restores x 0x100-0x1ff of those rows and 64 columns, so that VRAM is not put back | kept (harmless: the menu's own CLUTs there are at x 0x100 on, the item viewers re-upload theirs, and the field reloads its VRAM after the menu; the Status page after a rename matches Redux) |
| M4 | menu_gfx sprite CLUTs / common/menu_gfx.cpp | a sprite's graphic index 12 and up reads its "CLUT loaded" flag past the 12-byte table | avoided (those CLUTs are uploaded every time) |
| M5 | rename_text_draw 0x80081b9c / field/menu_rename.cpp | a line can start at row 0xd8 of the 0xd8-row line buffer, and its 12 rows would be written past it | avoided (the port's buffer has 0x100 rows; the screen draws about five lines) |

## Spells and items (world agent)

| Id | Where (original / port) | The bug | Status |
|---|---|---|---|
| S1 | Foehn module 2a9c041ec7 step 3 / battle/spell_mod_foehn.cpp | the wall's sounds and leaf sprites go to the caster's own side (the party / monster lists are swapped: a party caster walks the 4 members and monsters 0-3, a monster the first 4 monsters); the damage itself (spell_hit_all) is right | fixed (verified faithful first: Redux encounter 516) |
| S2 | item_use_battle 0x80082dec, effect kind 0x5c (0x800844d4) / battle/items.cpp | the 28 blasts round the enemies take the enemy's height (+0xc) as their depth, so they all land near the arena's middle line instead of at the enemies; visual only (no RNG or state depends on it) | fixed |
| S3 | spell_cast 0x80062c24 (the Shot spells) / battle/spells.cpp | a monster's counter-cast right after an item's Shot hit it has no target (+0x1e8 last attacker is null); the SpellCast spells fall back through spellcast_retarget (it reads the kernel bytes at 0x123 / 0x125: the first standing member from slot 3 on), but the Shot spells (Fire Bolt, Ice Needle, ...) aim at the "unit" at address 0 | fixed (the Shot spells take the same member) |

## Adding an entry

Give it the next id in its area. Name the original address and the port file, describe the
bug in one line, and set the status. When an agent fixes one, it changes the status to
**fixed** and keeps the commented-out original code next to the fix.
