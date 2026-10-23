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
- **Fixes never change the play state** (user, 2026-10-22: "We are fixing bugs, not breaking
  the play state"). Hit timing, the number of hits, damage, difficulty and battle pacing stay
  as the original plays. A fix corrects the look or the evident mistake around them. Each fix
  is measured against the original in a PCSX-Redux comparison (hit frames, hits, damage,
  waits). If it would change any of those, it is held, and the numbers go to the user.
- **Not bugs.** Data formats are kept exactly, even where they hold the original's leftover
  memory: save files (the uninitialised pad word, the snapshot's tail, the icon-sheet
  bytes) must stay byte-compatible with real memory cards.

Status: **kept** = the port still does what the original does (to fix); **avoided** = the port
already does something safe instead; **fixed** = commented out and corrected; **decide** =
a quirk that may be the intended look or feel, so it needs a decision before changing.
**keep** = decided by the user: the original's behaviour is intended and stays; **to fix** = decided
by the user as a bug, fix pending. **held** = decided by the user: not fixed for now, because the
fix would change play.

## Battle (battle agent)

| Id | Where (original / port) | The bug | Status |
|---|---|---|---|
| B1 | ribbon_trail_update 0x80075a74 / battle/killer.cpp | xy1 / xy3 use cos(start angle) where cos(end) belongs. Only quad 0 computes that start angle (+0x400), so every quad's far edge uses quad 0's cosine and the ribbon's width wanders along its length | fixed (cos(end); visual only. Faithful version checked on Opera's move 7, ref op_m8: b480-500 drop from 3-4% to 0.06-0.15% of pixels, so the remaining difference there is this fix) |
| B2 | afterimage draw / battle/killer.cpp | copies drawn as `(head + 2i + 2) % count`: with an even count half the copies are drawn twice and half never | fixed (user, 2026-10-22: keep the fix. Each copy is drawn once, newest first: `(head + i + 1) % count`) |
| B3 | particle line draw / battle/particle.cpp | the depth comes from the GTE OTZ register after RTPS, a stale value from the last AVSZ, so lines sort at the wrong depth | fixed (AVSZ4 over the line's two ends) |
| B4 | shot_apply_damage 0x800548c4 / battle/shot.cpp | the redirect loop compares the random member with targets[tries] and walks past the list (stack garbage) | avoided (compares within the list) |
| B5 | lens_flare_draw / battle/fx_draw.cpp | patches the shared stage flare tables in place (sizes[0..1], intensity[0]) | fixed (user, 2026-10-22: lens_flare_render now patches a per-call copy of the flare set; the stage flare looks the same, and the shared tables keep their disc values) |
| B6 | unit_move / battle/unit_move.cpp | an output vector's x / z start as stack garbage | avoided (starts at the unit's position) |
| B7 | voice pick / battle/voice.cpp | a member who isn't there reads past an array | avoided (taken as down) |
| B8 | sound handles / battle/bsfx.cpp | a bad handle (0xffffffff) reads past the 8 effect slots | avoided |
| B9 | projectile_arc_velocity 0x8004906c / battle/unit_move.cpp | the jump arc's x is scaled by the speed but its z by a constant 24, so Bowman's leap (move 3, speed 10) misses the target in depth by up to 2.4x | keep (user, 2026-10-22, revised: the leap is meant to be able to miss when the target is far or moves, as Claude's and Dias's Headsplitter can. Revert to the original line) |
| B10 | Bowman move 6 0x801ef470 / battle/pc_bowman.cpp | the clones are placed at the target's four corners for i 0..3; a fifth clone (level 3) keeps the pool slot's old position | kept |
| B11 | stage 0x10 hanging object, #1984 block 4 0x8008c1f4 (kind 0x1a) / battle/stage_objects.cpp | the ground shadow is sized from obstacle 23 - +0x128 (the model index 1 / 1 / 2) instead of 23 - +0x12c (its own obstacle 0 / 1 / 2), so the first object's shadow takes the second object's box and stretches towards it | fixed (visual only: the shadow uses the object's own box) |
| B12 | same function | the fall check (after a spell's finishing hit) takes obstacle 23 - +0x128 out of the way instead of its own, so the first object meets its own box and never falls, and the second object's obstacle kind turns from 2 to 1 | kept (it decides whether the object falls: play) |
| B13 | stage 0x24 cart, #1984 block 8 0x8008c47c (kind 0x23) / battle/stage_objects.cpp | the popup shows 50 but the cart takes 100 HP | fixed (visual only, user's standing direction: the popup shows the damage taken, 100; the boulders' popups stay 50. A popup draws no random numbers. Verified on ref/stage36_long (the cart): RNG, units and Shots unchanged) |
| B14 | pc_state_approach 0x80036410 (PartyUnit +0x230) / battle/party.cpp (pc_construct_slot) | nothing initialises +0x230, the approach's last distance to the approach point. A member's first jump check on an airborne target (`tj <= +0x230`) therefore reads heap garbage: the word the raw stage file left there, because the field preloads the stage at the top of the heap (ending at 0x801fe000), and the units are later allocated top down over it from 0x801edff0. A positive word makes the first approach jump at once. A negative one waits for the jump distance to be crossed | kept (it decides when the jump attack starts: play). The port works out each unit's console address (the modules' operator new sizes, mem_alloc_low's (size + 0x13) & ~3 blocks, slot order) and reads that word from the raw stage file. Checked on ref/stage14c: Claude jumps at b951 as on the console, the RNG is equal to the exit (b1954), and all 975 Shot frames are equal (with 0 it split at b960). The model holds for twelve stages and for Claude, Celine and Ernest in the references |
| B15 | bsfx_play / bsfx_pan_update 0x8007f3f0 / 0x8007ffa8 / battle/bsfx.cpp, bobj.cpp | a channel keeps its emitter pointer after the object is freed (a SpellCast deletes itself while the sound it started at itself still plays), so bsfx_pan_update reads freed memory for the pan and volume. A use-after-free in the port (the ASan sweep: encounters 350, 430, 471 and 540) | fixed (sound only): ~BObj calls bsfx_forget_emitter, which moves the channel onto its internal emitter at the object's last position, as the console's stale memory usually reads. The four encounters are clean under ASan with the same outcomes as pass 2 (enc540 defeat at 3174) |

## Enemies (enemy agent)

| Id | Where | The bug | Status |
|---|---|---|---|
| E1 | arcmene blink test / enemy_mod_arcmene.cpp | compares an index with 0x3e8000, always true | fixed (the nearest member's distance within 1000 units; verified faithful first: Redux encounter 50) |
| E2 | ericodus attack 5 / enemy_mod_ericodus.cpp | can reach a step no branch handles, freezing the monster in state 13 | fixed (a block or a landing sets step 2) |
| E3 | visseyer / jibril release search / enemy_mod_visseyer.cpp, enemy_mod_jibril.cpp | the even tries store the z into the member's z velocity (+0x30) instead of its z | fixed |
| E4 | bark vortex ring / enemy_mod_bark.cpp | the ring heights shift a 20.12 value by 12 again, right only at height 0 | fixed |
| E5 | marsilio afterimage / enemy_mod_marsilio.cpp | attack 1 reads enemy_attack_step's result (hits + 1) as "done", so the afterimage ends on a hit, not at the end of the swing | fixed (user, 2026-10-22: the afterimage should last to the end of the swing; it now ends with the swing, when begin_action's release_effects lets it go. Visual only: in a port run of encounter 530 where his plain swing hits, the log (RNG, units, HP) is byte for byte the same with the original's line and with the fix) |
| E6 | shigeo choose_target / enemy_mod_shigeo.cpp | the 0x291c0 chance test always passes | keep (user, 2026-10-22: intended. Shigeo is meant to be unbeatable in the first fight; in the second he holds no aggro and is a one-time fight the story needs won) |
| E7 | celesta status_test / enemy_mod_celesta.cpp | returns bit 24 of the mask argument instead of the status test | not a bug: effect word 0 bit 24 is stun immunity (unit_apply_stun), so she reports that one effect and nothing else: G-Celesta cannot be stunned |
| E8 | indalecio step 6 / enemy_mod_indalecio.cpp | the halo afterimage is never made | fixed (the first frame of step 6 is told by the fade +0x38, which step 5 clears) |
| E9 | carlaeagle feathers / enemy_mod_carlaeagle.cpp | every feather starts 0x78 above y 0 rather than above the bird | fixed (user, 2026-10-22, revised: the feathers start at the bird and fan out toward the party, landing at the original's hit frames. They keep the console's path, so they hit when and where the console's do; only the drawing starts 0x78 above the bird and slides onto that path by the point nearest the target (Shot::draw_offset in shot_k16_projectile; the collision boxes stay on the path). Measured on ref/caster35: positions equal to BW_SHOTS every frame, the same 3 of 5 feathers hit Claude at the original's frame (ref b849) for the same damage (1136 to 626), and the RNG is equal to the battle's exit (b2046), where the first fix split it at b850) |
| E10 | indalecio funnel / enemy_mod_indalecio.cpp | the funnel has already turned +0x34 this frame when it is read | the cause of E8, not a second bug: fixed with it (the funnel itself turns as before) |
| E11 | bunny / enemy_mod_bunny.cpp | when no point is found, an uninitialised stack slot is read | avoided (0) |
| E12 | enemy_support.cpp | the colour register holds garbage for an unknown mode | avoided (0) |
| E13 | Noah module 6563c70f92 step 2 / battle/spell_mod_noah.cpp | as Foehn's S1: the wall's splashes (sounds, sparks, shakes) go to the caster's own side (party / monster lists swapped); the damage (spell_hit_all) is right | fixed (verified faithful first: Redux encounters 515, 539) |
| E14 | visseyer / jibril release / enemy_mod_visseyer.cpp, enemy_mod_jibril.cpp | a released member's facing x is set from its own x velocity (-vel x, 0 after being swallowed), so it comes out facing an arbitrary way instead of toward the monster | keep (user, 2026-10-22: released members are meant to come out facing a random way) |
| E15 | voice player 0x8007faa4 / 0x8007fc54 / 0x8007f76c / battle/voice_player.cpp | voice_update keeps the low byte of the clip handle (the voice buffer's address bits) where sound_handle_active and voice_stop_all later expect the sound slot, so a voice counts as playing only while its entry stays in its slot (freed when the stream ends, before the last samples play) and voice_stop_all never releases a playing clip | keep (user, 2026-10-22: the original's voice timing stays, including how long a boss waits after its intro voice) |

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
| F12 | Chisato move 6 0x801ef718 / battle/pc_chisato.cpp | the stars after the first are scattered by rng_spread(n << 7) units in x (through shot_place_at_unit) but rng_spread(n << 6) is added to z raw, in 20.12, so they never scatter in depth (at most 0.08 of a unit) | held (user, 2026-10-22: the fix would make more of the later stars miss, which weakens the move) |
| F13 | Opera move 7 burst 0x801f1098 / battle/pc_opera.cpp | the last of the lvl + 4 shards should fly straight up (-0x14000) but the store goes to the burst's own velocity, so that shard keeps 0 and drops from the burst point | kept (the play-state rule: with the velocity the shard hits too, and on op_b8 the move's two hits land 8 and 19 frames earlier (trace frames 320 / 414 -> 312 / 395, damage the same 60 / 60); the original stays) |
| F14 | Opera move 7 shard hit 0x801f129c / battle/pc_opera.cpp | the hit sprites' y jitter uses rng_between(-0xc0, 0) raw (in 20.12, under 0.05 of a unit) where the other shots use rng_between_fixed (192 units) | fixed (user, 2026-10-22: rng_between_fixed(-0xc0, 0), the 192 units the other hit sprites use; play-neutral: the same RNG draws, and on op_b8 the hits and damage are the same as the original's (frames 320 / 414, 60 / 60); the faithful version was verified first: op_b8, RNG equal to the exit) |
| F15 | Ashton move 3 darts 0x801eecf8 / battle/pc_ashton.cpp | each dart's facing is set from his x velocity (+0x28) instead of his facing (+0x18), so the dart sprites can face the wrong way | fixed, play-neutral (user, 2026-10-22: draw the darts by Ashton's facing). Console check (asv_b4 with the dragons, RAM at the dart spawn 0x801eedd0): his x velocity is 0 there, so every dart had facing 0; the facing also places the attack box (mirrored with the drawing), so only the drawing follows his facing and the boxes are mirrored back to facing 0's side. Measured (SS_ENEMY_TRACE): the original, the fix and a forced opposite drawing (the mirror path) give the same hits and damage (frames 232 / 388, 60 / 60), and the fix's RNG equals Redux's asv_b4 to the exit |
| F16 | Ashton move 3 darts 0x801eecf8 / battle/pc_ashton.cpp | the sound plays at the last dart spawned even when that spawn failed (a null Shot) | avoided (no sound then) |
| F17 | Ashton move 0 hit sprites 0x801ee424 / battle/pc_ashton.cpp | the sprite for every unit hit is picked by the first hit point's height (scratch 0x1f8002a0) instead of its own | fixed (user, 2026-10-22: each unit's sprite by its own hit point's height; play-neutral (the sprite only): on as_b1 the hits and damage match the original's (frames 206 / 211 / 379, 43 / 17 / 60); the faithful version was verified first: as_b1, RNG equal to the exit) |
| F18 | voice_queue_update 0x80079764 / field/voice_queue.cpp | when the field-clip queue empties, the "voice bank loaded" flag is set on all four sound slots if party slot 0 has a member (the party pointer is never advanced), not on each member's slot; Redux (world card, 3 members) shows slot 3 flagged | avoided (each member's own slot; play-neutral: the queue builders skip non-member slots and every party change invalidates the slot) |

## Field, menus and the rest

| Id | Where | The bug | Status |
|---|---|---|---|
| W1 | post-battle report / field/post_battle.cpp | the report's fifth row (MAG) falls under the window's clip and is never seen | fixed (the window opens one line taller, 0xd; visual only: the report's content and the auto-equip are unchanged) |
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

## Field events (menu agent, M8 audit)

| Id | Where (original / port) | The bug | Status |
|---|---|---|---|
| V1 | notice_update 0x80057790 state 3 / field/notice.cpp | when the banner's window is already gone (status -1), it still queues the slide-out move on `message_windows[n]`, a null pointer: the console writes into low RAM (0x6c-0xcc, around the exception vector); the port crashed. Reached by the event sweep closing every window under a showing banner; no shipped script was seen doing it | fixed (the move is skipped when the window is gone) |
| V2 | actor_pop_state 0x80041aa4 / field/actor_base.cpp | it runs the actor's update (0x8003d95c) before restoring the snapshot; when that update executes a queued delete (a talk that ends with ext 0x71 on the NPC it talked to, e.g. map 753's NPC 18), the actor is freed and the console goes on with it: it copies the freed snapshot back into the freed actor and frees the snapshot a second time. Harmless on the console only by luck (the memory is not reused at once); the port crashed at the end of that talk | fixed (the pop stops once the update has deleted the actor) |

## Spells and items (world agent)

| Id | Where (original / port) | The bug | Status |
|---|---|---|---|
| S1 | Foehn module 2a9c041ec7 step 3 / battle/spell_mod_foehn.cpp | the wall's sounds and leaf sprites go to the caster's own side (the party / monster lists are swapped: a party caster walks the 4 members and monsters 0-3, a monster the first 4 monsters); the damage itself (spell_hit_all) is right | fixed (visual only, 2026-10-22: the console's leaf walk keeps its spawns and its game random draws, but those leaves are drawn at scale 0; the leaves and sounds of the right side come from the visual-only random source. RNG and units equal to Redux on k516 and i12. Default; SS_ORIGINAL_S1=1 shows the original leaves) |
| S2 | item_use_battle 0x80082dec, effect kind 0x5c (0x800844d4) / battle/items.cpp | the 28 blasts round the enemies take the enemy's height (+0xc) as their depth, so they all land near the arena's middle line instead of at the enemies; visual only (no RNG or state depends on it) | fixed |
| S3 | spell_cast 0x80062c24 (the Shot spells) / battle/spells.cpp | a monster's counter-cast right after an item's Shot hit it has no target (+0x1e8 last attacker is null); the SpellCast spells fall back through spellcast_retarget (it reads the kernel bytes at 0x123 / 0x125: the first standing member from slot 3 on), but the Shot spells (Fire Bolt, Ice Needle, ...) aim at the "unit" at address 0 | fixed (play-identical, 2026-10-22: the bytes the console reads at address 0 are constant (low kernel RAM 0x0-0x1ff equal across 121 Redux dumps): pos 0, size 0, slot 2, stats pointer 0, so the port gives the Shot spells a stand-in unit with those values (unit_null_target; unit_is_down treats it as down) and the Shot flies where the console's does. Verified iC340 (Fire Bolt counter-cast with no target at battle frame 1508): RNG 220/220 samples equal, enemies 220/220 and party 218/218 exact) |
| S4 | Black Saber 67a7a8d842 / Sonic Saber b82e4cf842 blade callbacks (0x8008dcd4 / 0x8008dcd8) / battle/spell_mod_black_saber.cpp, spell_mod_sonic_saber.cpp | the hit keeps 4 units, 4 listed units and 4 points in a row on the stack: with more than 4 units in the blade's box the list overwrites units 5-8 before they are read and points 5-8 run over the saved registers (6+ units: a likely crash); the hit points passed on are not filtered with the list | avoided (4+ units in the blade's box overrun the console's stack, likely a crash; the port keeps 8-entry lists. Up to 4 units it plays exactly as the console: verified i10a / i12) |
| S5 | the same blade callbacks / the same files | on a combo (+0x10c 3) the blade turning back to its owner gets only its x velocity scaled to its speed (0x28), so it flies back almost flat along x | held (rule of 2026-10-22: the returning blade still hits, so its path decides what it meets. Faithful by default; SS_HELD_FIXES=S5 turns the fix on) |
| S6 | Earthquake 43d5518828 step 0 / battle/spell_mod_earthquake.cpp | the copy loop puts two of the three wave centres into the points model_find_vertices_near measures from; the third is stale scratchpad data, so wave 3 lifts the wrong vertices | fixed (all three copied) |
| S7 | Blood Sucker 6d3d24cf0b step 5 / battle/spell_mod_blood_sucker.cpp | the glow's height is y - (y << 13), not y - (height << 13): at the gainer's feet for a unit on the ground (y 0), far off for a flying one | keep (user, 2026-10-22) |
| S8 | Lunar Light e794431858 / battle/spell_mod_lunar_light.cpp | the second spark ring's Shot is dropped and its turn (-11) is written to the first ring, so the first turns -11 and the second not at all | fixed |
| S9 | Shadow Flare / Judgement Day ae70313b32 shimmer / battle/spell_mod_shadow_flare.cpp | the second shimmer row's y phase is taken from the x phase (s24 + 0x200) instead of s30 | fixed |
| S10 | Press 6d0c9bf130 0x8008d418 / Gravity Press bf76cb13f3 0x8008d6e8 / their files | the dizzy stars / unit ring loop draws at 0, 120, 120 and 240 degrees: one drawn twice, and the animation steps four times a frame | fixed (user, 2026-10-22: the third star at 240 degrees, the animation at 3 steps a frame. Measured: visual only; the Redux sweep i9b (Press, Gravity Press) still matches sample for sample: RNG, units, cast and finish frames) |
| S11 | Deep Mist 60d0314477 / battle/spell_mod_deep_mist.cpp | no pause test: the steps (rolls, sprites) run on while the battle is paused | held (rule of 2026-10-22: the steps' random draws during a pause move, so the game's RNG sequence changes when the player pauses in the spell. Faithful by default; SS_HELD_FIXES=S11 turns the fix on) |
| S12 | Explode a639d47ca2 step 3 / battle/spell_mod_explode.cpp | the hit takes the caster's own side, not flipped for combo 3 as the resident hit functions (spell_hit_rect) do | held (rule of 2026-10-22: changes who is hit in a combo-3 Explode. Faithful by default; SS_HELD_FIXES=S12 turns the fix on) |
| S13 | model_shatter 0x8005f30c / battle/shatter.cpp | lit G4 shards ignore their colour (leftover RGBC); unlit G3 reads its vertices at +4 (+12 meant); unlit FT3 / GT3 shards get the quad kinds 6 / 8 (a fourth vertex, uv and colour left from the slot's last use); shard_light_normals loads each normal through a pointer read from the normal itself (the model holds them inline) | fixed (verified faithful first: Redux i9b, Haste / Delay) |
| S14 | Silence 2ecc4bc085 step 0 / battle/spell_mod_silence.cpp | the sign starts at y - (y << 14) + 3 units, not from the caster's height: at the feet for a unit on the ground | keep (user, 2026-10-22) |
| S15 | Curse efdb20dc3f 0x8008d02c / battle/spell_mod_curse.cpp | the skull's start height adds size x * 4 without the << 12 (a fraction of a unit) | keep (user, 2026-10-22) |
| S16 | Delay 8e03dba0da / Haste 83a78870f2 / their files | the shatter sprites' z spread has both bounds -4 x depth (rng_between then returns without a draw) | fixed (user, 2026-10-22: -4 .. 4 x depth from the port's visual-only random source (spell_fx visual_rng_*), so the game's RNG sequence stays the original's. Measured: i9b (Delay, Haste) still matches Redux sample for sample) |
| S17 | Gremlin Lair fc13393fe3 / battle/spell_mod_gremlin_lair.cpp | the handler never ticks the SpellCast timer, so its waits (steps 2, 3, 4, 6) hold nothing, and step 6's wait skips step 7 (waiting for the effects) | held (user rule: changes play). Measured with the fix on (SS_HELD_FIXES=S17): the cast runs 42 frames longer (914-1766 -> 914-1808, i10a). Units and the AI are frozen while a SpellCast runs and the gremlin's own damage is unchanged (the wizard 20000 -> 18659 either way), but the spell's per-frame draws come at other times, so the game's RNG sequence after the cast differs and later rolls change (i10a: the wizard ends on 17221 instead of 17201 after the next four spells). The extra frames also count against the 60-second battles' time limit (rule 3: g_rule_timer runs while a SpellCast holds the battle) and the fight-time score of the victory voice |
| S18 | Raise Dead 96440a6c9b / battle/spell_mod_raise_dead.cpp | the funnel's y shifts the target's 20.12 y left by 12 again: right only at y 0 | fixed (user, 2026-10-22: the target's y without the extra << 12. Measured: visual only; x9b (Raise Dead) still matches Redux sample for sample) |
| S19 | Deep Mist 60d0314477 / battle/spell_mod_deep_mist.cpp | the handler never ticks the SpellCast timer, so step 0's 30-tick wait holds nothing | held (user rule: changes play). Measured with the fix on (SS_HELD_FIXES=S19): the mist starts 16 frames later and the cast ends 14 frames earlier (2142-2384 -> 2142-2370, i9a; the data finishes loading inside the wait), the wisps draw 10 more random numbers, so every roll after the cast shifts (the game's RNG sequence +10). Units are frozen during the cast; the 60-second battles' time limit counts the frames |
| S20 | the spell list's confirm 0x80073294 (#1984.0), target class 3 / battle/command_menu.cpp | a spell on the whole party (Fairy Light, Cure All, ...) keeps the member's current target; with none (no action yet this battle) spellcast_retarget keeps the null target for a party caster, so the SpellCast is dropped after the MP is paid and the spell does nothing | fixed (user, 2026-10-22: burning MP for nothing is a real bug, not intended play. With no current target the spell takes the caster as its target (command_menu.cpp; the original line kept as the ORIGINAL BUG comment). Measured: a first-action Fairy Light from the menu now heals (20000 -> 22200) and pays its MP once (999 -> 959); with a current target present, play is unchanged: m9b (Rena attacks, then Fairy Light from the magic list) against Redux, RNG 240/240 samples, enemies 240/240 and party 238/238 exact, the cast at battle frame 1612 and its end at 2032 on both. Default; `SS_ORIGINAL_S20=1` keeps the original for comparisons: m9 (the first-action menu Fairy Light) against Redux then RNG 220/220, enemies 220/220, party 218/218) |


## Adding an entry

Give it the next id in its area. Name the original address and the port file, describe the
bug in one line, and set the status. When an agent fixes one, it changes the status to
**fixed** and keeps the commented-out original code next to the fix.
