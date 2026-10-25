# Handoff: ships (Port Hilton's harbour in open-world seeds)

Started 2026-10-25 by the "ships" implementation agent (wave 1 of docs/handoff/apv2.md "The plan").
Research it builds on: scratchpad/openworld/party_ships.md section A. Scratch: scratchpad/ships/.
Build dir: engine/build-ships.

## The brief (short)
Open-world seeds only (`world_mode: open_world`); story mode and non-AP games unchanged.
1. Soft-lock fix: lift the Hilton sailor's (map 169 actor 17, routine 0x714c) ch 23/24 refusal and
   the ch 18 step >= 6000 one; decided from world_mode, so 0.1.1 open-world seeds get it with a port
   update (no new seed field).
2. Hoffmann ship (169 actor 24, routine 0x79c0): available ch 23-59; first trip still stores ch 24.
3. The captain's "where to?" menu at the Hilton sailor: Cross / Hoffmann Ruins (ch 23+) / El (only
   at ch 27 step 7000 with every key of seed door 29106503, keys checked before any chapter store,
   same effect as room 444's routine 0x7c28) / Not now.
4. Landing-key pre-check at the NPC ("Map 231 Pass needed to sail"), before the fare.
5. Logic: no apworld edits (apworld2 owns apworld/); write a proposal (Hoffmann reachable from the
   teleporter via the Hoffmann ship at ch 28-59).
6. Docs: seed_format.md Phase 4, apworld.md Open world, engine/README.md, catalog, apv2.md plan line.
7. Tests: test_ap.cpp, a headless demo, the field smoke script.

## Rules given
Build in engine/build-ships (-j4). Tests: SS_DISC1=<cue> engine/build-ships/ss_tests,
engine/build-ships/ss_game_tests, tools/smoke_field_events.sh engine/build-ships/shattered-stars.
Headless only, every game process under tools/memcap.py (SS_MEM_CAP_GB 4), at most 2 at once, never
a config-saving flag, never a GUI. Edit only engine/src/game/ap/, the field files needed,
engine/tests/, and my doc sections (dialogue owns field/message.cpp, menu_options, config/setup;
killer_prof owns battle/; apworld2 owns apworld/, apworld.md option sections, seed_format.md's v2
section). Keep the tree compiling. Don't commit. Scratch in scratchpad/ships/. On a permission
refusal stop and report the exact command. Roll over at ~400k tokens. Log every ~20 minutes.

## Findings (verified against the disc, 2026-10-25)
- Words (NA; map 169): 0x1c65 = 0x12 (ch 18 test imm), 0x1c79 = 0x17, 0x1c7c = 0x18 (ch 23/24
  refusal imms), 0x1c99 = 0x12000002 (`push 2`, the choice's option count), 0x1ca6/0x1ca7 =
  0x19800004/0 (`push_eq L[4], 0`: the choice dispatch), 0x2647 = 0x1b (actor 24's registration
  `le ch 27`), 0x1f23 = 0x1b (his "again" dialogue `le ch 27`), 0x1f3b/0x1f3c = 0x19800000/0
  (his `push_eq L[0], 0`). Map 144 (Herlie sailor 0x7298): 0x1cdc/0x1cdd = 0x19800000/0.
- Germany lays map 169 out otherwise from 0x7bfc (de.inc kscript_169: +84 bytes from 0x7c70): the
  sailor routine is unchanged; the Hoffmann tail 0x7cf8 and 0x991c move (patch lines are translated
  by region::script_offset; the port op's jump target must be translated too). Maps 144/231/444/422:
  NA layout in every release.
- The choice library 0x484c takes (… question string, choice width, COUNT, options string, flag);
  ext_df handles 2-4 options; option lines default 1.
- Strings 108/109 (question / options) are used only by the sailor's routine.
- Room 444's routine 0x7c28 ends: music fade 90 (ext_ba), fade (ext_7c 3, 90), wait 90, store ch
  28 step 0, map_transfer_c(422, 130, -215, 0, 0, 180, 128). No flags or party changes in it.
- Walking from the Front Line to Port Hilton at ch 27 step 7000: the voyage proof's route
  444 T4 > 440 T0 > 445 T2 > 439 T0 > 438 T0 > world map; ap_walkmap.py --chapter 27 puts the
  Front Line (438) and Port Hilton (169) in one walkable component (3); the world chunk script
  (909) tests no chapter 26/27; 169's init has no ch 27 scene and registers the sailor. So yes
  (to confirm headless).
- Landing doors: 17 Hilton->Herlie 59578174 (Herlie group), 18 Herlie->Hilton 59578036 (Port
  Hilton), 21 231->Hilton 59578440 (Port Hilton), 22 Hilton->231 59578306 (map 231). 231's return
  soldier can never be refused (the party entered Hilton with the Port Hilton key), nor the Hilton
  sailor's Cross trip (Herlie is the Lacour continent's only way in): pre-checks are needed at the
  Herlie sailor (Hilton key) and for map 231 (the captain's Hoffmann option and the soldier).

## Design (as built)
Port-only table A ops (opcode 0xfe, subs above 0x48 do nothing in the original), only in scripts
through the 12 patch lines start_with adds to an open-world seed (ap.cpp):
- 0xFE700000 harbour count (replaces `push 2`): ap::harbour_open(question, options) -> count; the
  question string index is value_sp[-2], the options index the next instruction's `push`
  immediate (France numbers the strings one higher: 0x6d/0x6e, the others 0x6c/0x6d).
- 0xFEF10000 harbour go (pops the option): ap::harbour_choose -> kCross push 1 (the original fare +
  voyage); kJump into 0x79c0's sail tail 0x7cf8 (region-translated, word checked = `push 1`);
  kEl: music fade 90 + fade(3, 90) + wait 90, re-run with a sentinel, then
  ap::el_voyage_transfer (ch 28 step 0) + map_transfer_c(422, 130, -215, 0, 0, 180, 128) from the
  stack via vm_exec_ext(0xDA800000); kStay push 0.
- 0xFEF200NN landing pre-check (pops the option): pushes option == 0 && voyage_landing_open(NN).
Text: script_ext.cpp message_text -> ap::script_text(index, original) while the menu is open (only
with more than Cross on offer): question = the script's own speaker label (up to code 1, or the
FR/DE new line + indent) + "Where to, matey?\nCross is {number_var:00} FOL."; options = the
original lines' common lead-in codes + Cross / Hoffmann Ruins / El / Not now, then {wait}.
El without the keys: listed (when due) and refused with the El gate's banners (chosen over
hiding it: the player sees why the ship will not sail, as at the Front Line's trigger).

## Done (2026-10-25)
- Files: engine/src/game/ap/ap.h, ap.cpp (harbour API, patch lines, door_refused purpose
  override); engine/src/game/field/script_table_a.cpp (subs 0x70-0x72);
  engine/src/game/field/script_ext.cpp (message_text hook); engine/tests/test_ap.cpp
  (TEST ap_harbour). Builds clean (engine/build-ships).
- Patch words verified on NA / EU / FR / DE disc 1 (scratchpad/ships/verify_words.py: 0
  mismatches; DE 169 shifted +84 from 0x7c70).
- ss_game_tests 135/0 (incl. ap_harbour).
- Headless demo (scratchpad/ships/demo/run.sh, demo.py; logs + captures there), open-world seed
  open.apso2 (elgate batch, 6-key El gate) / story.apso2:
  - s23_open: ch 23, Cross -> fare 30 paid -> map 1 voyage -> Herlie (144) at vblank 2882.
  - s23_story: ch 23 story mode -> refusal window (string 107), no transfer.
  - menu16: ch 16 -> the sailor's own Cross / Don't cross (2 options, original strings).
  - menu23q / menu27q captures: 3 / 4 options; question "Sailor / Where to, matey? / Cross is
    30 FOL." (menu27q/frame00760.png).
  - el_nokeys: El -> banners for the 6 missing passes (el_nokeys/frame00900.png "El Beach Pass
    needed to sail"), chapter stays 27/7000, control back.
  - el_keys: 6 passes -> El -> ch 28 step 0 -> 422 (1922) -> 169 (6141) -> 548 (10982), ch 29.
  - hoff_nokey: Hoffmann without Map 231 Pass -> banner, no voyage. hoff_key: ch 24 stored,
    voyage, map 231 at 3792.
  - soldier40: ch 40, the Hoffmann soldier (now registered) with the key -> map 231 at 3752;
    soldier40n: without -> banner; soldier40s (story): nobody to talk to.
  - herlie_nokey: Herlie sailor without the Hilton Pass -> banner, Fol kept; herlie_key -> 169.
  - frontline: ch 27 step 7000, SS_SWEEP_PLAY 444:T4 > 440:T0 > 445:T2 > 439:T0 > 438:T0 ->
    world map (103670, 46491) with control.
- Front Line walk: exits open (above), one walkable component (ap_walkmap --chapter 27;
  scratchpad/ships/walkpath.py finds a 5-leg path), no world-script chapter 26/27 event, sailor
  registered at ch 27. Headless walk: leg 1 (11,000 of ~25,000 units) walked; the rest can't be
  scripted with SS_ROUTE (one held direction per map; the world map steers camera-relative with a
  following camera: holding left walks an arc). Answer: yes (strong static evidence).
- Docs: seed_format.md Phase 4 "The harbour"; apworld.md Open world bullet.

- Docs also: engine/README.md (Open world bullet), docs/catalog/script_opcodes.csv (table A
  0x70-0x72: port-only uses), docs/catalog/formats.md (the map library's choice routine 0x484c
  and the ship NPCs).
- Smoke: tools/smoke_field_events.sh gains ap_p4_harbour / ap_p4_story / ap_p4_el (chapter 23
  menu + Cross sails; story mode refuses; El without its pass refused, nothing stored);
  ap_p4_tele_open's mods count is now 15 (3 teleporter + 12 harbour lines). First run: 92 PASS,
  1 FAIL (that count; the dialogue agent saw the same failure): the expectation in the smoke
  script is updated to 15 (my patch set is final). Rerun: scratchpad/ships/smoke/smoke2.log
  (see "In progress").
- ss_tests 99/0 (with SS_DISC1).
- tools/ap_e2e_so2.sh not run: the change does not touch the client path (no item, location,
  slot data or network change; the port decides from the seed's world_mode).

## In progress (at hand-off, 2026-10-25)
- Nothing. The smoke rerun with the corrected count finished: 93 PASS, 0 FAIL, exit 0
  (scratchpad/ships/smoke/smoke2.log; includes ap_p4_harbour, ap_p4_story, ap_p4_el and
  ap_p4_tele_open at 15 seed lines). The tree compiles (engine/build-ships).

## Next steps (for a successor)
1. Optional: a scripted world-map walk Front Line -> Port Hilton (needs a waypoint walker; the
   world map steers camera-relative), if the user wants more than the static evidence.
2. Hand the proposal below to apworld2 (the coordinator relays it); no apworld edits here.

## Proposal for the apworld agent (apworld2): Hoffmann Ruins through the teleporter
Nothing in the logic must change for this work: the soft-lock fix makes the logic's existing
assumption (the Cross continent stays reachable from the Lacour continent) true, El from the
captain uses the same gate and the same story point, and the landing keys were already
required. One optional gain, for 0.2.0 or later:
- With the port's harbour, the Hoffmann ship sails Port Hilton -> map 231 at chapters 23-59
  in open-world seeds (map 231's soldier sails back while chapter < 60). So the teleporter's
  party (stage 16 + Fun City Pass: arena -> Arlia -> Herlie ferry -> Port Hilton -> Hoffmann ship)
  reaches map 231 and Hoffmann Ruins (478) between the disc switch and the Final Save Point.
- To prove it: tools/ap_late_proof.py's teleporter state needs a proof seed with
  `"world_mode": "open_world"` (it has none today, so the port's harbour lines would not apply;
  the proof seed has no doors, so nothing gets locked), 231 and 478 out of TELEPORTER_ISLANDS,
  and tools/ap_walkmap.py treating Port Hilton <-> map 231 as a ferry like FERRY_MAPS (144, 169)
  so its --check agrees (islands then 229, 232, 557, 704). Rerun the teleporter proof for both
  games with a port that has the harbour (this build or later), merge late_proof.json's
  `teleporter` key, regenerate data.py: Hoffmann's 26 chests (and map 231's rows, if proven)
  get `tele`.
- Then the existing rule applies (an Expel location the teleporter proof reached may hold any
  progression when expel_progression is on). The tele mode's keys must include the ship
  landings on the route: Herlie Pass (to board the ferry), Hilton Pass (Port Hilton landing),
  Map 231 Pass (map 231 landing, data.KEY_GROUPS `landing`), Hoffmann Pass; check the proof's
  route lists them, else add them as the early mode does.
- Version coupling: a port before this change (0.02.00) stops the Hoffmann ship at chapter 27,
  so a seed that puts progression in Hoffmann's chests on the strength of the teleporter could
  strand it there. Tie the gain to the world version that requires the new port (and say so in
  the release notes), or keep Hoffmann's rows as today.
- Smaller: the four "Lacour Hope" soldiers beside the Hoffmann soldier also stand at chapters
  28-59 now; their pickpocket entries (Bandit's Gloves, Buckler, Plate Mail, Plate Helm, Plate
  Greaves, if any are locations) are reachable longer: only more access, nothing to change.

## Open items / notes
- The full Front Line -> Port Hilton walk is not scripted headless (see above); evidence is
  static plus the exits chain and leg 1.
- The captain's text is English in every release (the speaker label is the release's own);
  France / Germany get "Where to, matey?" in English, as the port's AP banners are.
- At chapter 23 the captain's Hoffmann option skips the soldier's first-time lines (flag 263,
  Leon's "Can we head out?"); flag 263 has no other reader, and the sail is the soldier's own.
- Open world v2 (the hub, later): the chapter windows here (Hoffmann 23-59, El at 27/7000,
  Herlie's ferry from 16) would become key rules (party_ships.md "DEPENDS ON OW v2").

## How to verify
- Build: cmake --build engine/build-ships -j4.
- engine/build-ships/ss_game_tests (ap_harbour); SS_DISC1=<cue> engine/build-ships/ss_tests.
- TMPDIR/XDG under scratchpad/ships/smoke: tools/smoke_field_events.sh engine/build-ships/shattered-stars.
- scratchpad/ships/demo/run.sh [names] (2 at a time; summary.txt, out_NAME.txt, NAME.log, captures).
- python3 scratchpad/ships/verify_words.py (the patch words on NA / EU / FR / DE).
