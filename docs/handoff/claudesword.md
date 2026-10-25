# Handoff: Claude's sword missing in Rena's game (battle graphics)

Queued 2026-10-25 by the main session. Agent "claudesword" started 2026-10-25 15:00.

## The user's report (verbatim)
> In Rena's story mode, when Claude fights Alan-Tax and thereon, the 'sword' that he is supposed
> to be using graphically isn't there, it looks like a stretched version of his air punching
> sprite. The Claude start that I did had the sword normally.

## To find out
- Plain game or Archipelago? Reproduce both: a plain Rena game at Claude's join (Salva Drift 61,
  Alen-Tax encounter 191) and onwards, and an AP story-mode seed on Rena's route (and with
  character_items / party v2 if the user's seed used them: ask the coordinator to ask the user for
  the seed if it matters). Compare with Claude's own game.
- Claude's weapon in Rena's game: his equipment record when he joins (script ext 0xae party_add,
  or the AP character item / party v2 join path), the weapon's battle model / texture / animation
  set (battle weapon loading), against PCSX-Redux on a Rena save at the same point (`-cli`, scratch
  HOME, scratch card copies) where a reference is needed.
- Fix the port to match the original (docs/handoff/battle.md, effects.md for the battle model
  code). If the original itself shows it this way: report to the coordinator (the user decides).

## Rules
As in the other handoffs: headless only, own build dir engine/build-claudesword, memcap 4 GB, at
most 2 game processes, scratch XDG_CONFIG_HOME, scratchpad/claudesword/, no config-saving flags, no
commits, this file as the log (Done / In progress / Next / How to verify), roll over at ~400k, stop
and report on a refusal; suites with counts before "verified" (ss_tests 99/0, ss_game_tests 173/0,
smoke 198/0). Enhance, never degrade. Other agents: hubask (engine/src/game/ap/*, apworld/),
hubproof, charselect (title / New Game): coordinate shared files through dated notes in their logs.

## Done (log)
- 15:00 engine/build-claudesword configured (RelWithDebInfo, AP net + setup on, as build-hubcore)
  and built. Runner: scratchpad/claudesword/run.sh NAME ARGS (prlimit 4 GB, scratch XDG, dummy
  audio, offscreen video, timeout 600; log in runs/NAME.log). sheet.py makes contact sheets.
- Script facts (scratchpad/claudesword/find_equip.py over all maps): Claude starts with weapon 0
  (init_claude 0x8007a37c / title 0x80062xxx: record +0xc = 0) in both games. In Rena's game map 61
  (Salva Drift) 0x4888 / 0x4890 equips him B 0x74 (0, 0, 490 Long Sword) and (0, 1, 677 Leather
  Armor) right before battle_c(191) when G#1 == 0 (Rena's game). In Claude's game Regis gives a
  Long Sword (an item; map 34). So Claude holds the same Long Sword in both games: the item is not
  the difference.
- Reproduced in the port (`--battle 191`, cross mash, --capture): Claude in party slot 0 with weapon
  0 punches (correct), with 490 shows the sword (correct). Claude in slot 1 (SS_BATTLE_PARTY=2,1,
  Rena in slot 0, as in Rena's game) with 490: the sword is replaced by a stretched grey/blue
  blob (scratchpad/claudesword/z_cmp.png: top row slot 0, bottom row slot 1). So it is the SLOT,
  not the game or AP: in Rena's game Rena is slot 0 and Claude slot 1.
- Cause (port bug): party.cpp tex_rects_offset, the port of 0x80083d40 (asm read with REA on
  analysis/elf/SO2_boot+prog2129.elf). The weapon texture of slot N is uploaded at
  (0x80084fc8[N], 0x80084fd0[N]) = (576,256) / (608,256) / (576,384) / (608,384) under the fixed
  tpage 0x19 (4-bit), and 0x80083d40 moves each of the block's count (+0xa) cel rects by the slot's
  offset from slot 0: bytes +0x12 / +0x14 + 8i (u0 / u1) += dx * 4 and +0x13 / +0x15 + 8i (v0 / v1)
  += dy (the 0x10-based cel table cel_texture reads with big = true: e[2..5] = u0 v0 u1 v1). The
  port added dx (not dx * 4) to byte +0xc + 8i and dy to +0xd + 8i, i.e. to the previous cel's u1
  / v1 (and the header for i = 0): slot 1's cels kept slot 0's u0 (Rena's area) and got u1 + 32
  (stretched). Slots 2 / 3 got v1 + 128. Slot 0 (dx = dy = 0) was never affected, hence Claude's
  game was fine. Affects every weapon user (Claude, Dias, Ashton, ...) outside slot 0, in any game.
  The catalog (docs/catalog/functions.csv 0x80083d40) already describes it correctly.

- 15:45 Fix in engine/src/game/battle/party.cpp tex_rects_offset (the only change): as 0x80083d40,
  bytes +0x12 / +0x14 + 8i += dx * 4, +0x13 / +0x15 + 8i += dy. Rebuilt.
- Proof (all headless, --capture, crops in scratchpad/claudesword/):
  - `--battle 191`: Claude with the Long Sword in slots 1, 2 and 3 (z_fix.png rows: slot 0
    reference, slot 1, slot 2; z_fix3.png row 1: slot 3) now draws the sword as in slot 0.
  - The real Rena-game path: --map 61 with the sweep (Rena's game, chapter 6 step 2100, Rena +
    Claude), SS_ROUTE 61:down+left walks into the scene's region, the Salva Drift scene plays,
    B 0x74 gives Claude the Long Sword, battle_c(191) runs with Rena slot 0 / Claude slot 1.
    proof_m61.png: top = before (an unfixed binary built from the same tree with the fix
    reverted for one build, scratchpad/claudesword/shattered-stars.orig), bottom = after. Both
    runs end the battle identically (rng draws 3207, 1386 battle frames): textures only, the play
    state does not change.
  - AP story seeds on Rena's route (seeds/rena_story.apso2: world_mode story; rena_story_ci:
    + character_items), same path: proof_ap.png top = before (orig binary, rena_story_ci), bottom
    = after. The AP join path ("[ap] join Claude at map 61 ...: the game's join") does not touch
    the weapon; the cause is the same loader for both.
  - No PCSX-Redux run: the asm of 0x80083d40 is unambiguous and the catalog (functions.csv) already
    described it correctly; slot 0 (unchanged) is the console-verified case.
- Note for runners: a `--debug` run must have stdin from /dev/null, or the debug console thread
  holds stdin's lock and the process hangs in exit() (seen once here; my run.sh now redirects).
- Suites (engine/build-claudesword, with the fix): first pass ss_tests 99/0, ss_game_tests 173/0,
  smoke 208 PASS / 9 FAIL: the 9 were charselect's new ap_cs_* cases, added to the smoke script
  after my binary was built (their New Game code was newer than it). Rebuilt on the current tree
  (16:40): ss_tests 99/0, ss_game_tests 174/0, smoke 220/0 (scratchpad/claudesword/smoke2.log);
  the slot 1 capture is pixel-identical to the first fixed run (cap/final vs cap/f1).
- Scratch pruned to the proof frames (scratchpad/claudesword, 23 MB; the unfixed reference
  binary deleted).

## In progress
- Nothing: done, final report sent.

## Next steps
- None beyond the report. Not done: Dias / Ashton before/after (same code path; Dias in slot 1
  showed his sword after the fix, cap/d1).

## How to verify
- `cmake --build engine/build-claudesword -j8`
- `cd scratchpad/claudesword; SS_BATTLE_PARTY=2,1 SS_BATTLE_POKE=4:0xdc=490/2,2:0x41=1/1
  ./run.sh s1c --battle 191 --frames 700 --capture cap/s1c --capture-frames 360,372,384 --input
  "$(python3 -c "print(','.join(f'{f}:cross,{f+4}:none' for f in range(60,1200,30)))")"`
  then look at Claude's sword in cap/s1c (crop 60,120,200,200).
- The real path: `env SS_MODS_DIR=$PWD/no-mods SS_EVENT_SWEEP=1 SS_ROUTE=61:down+left
  SS_ROUTE_GOTO=61 'SS_SWEEP_FLAGS=!1' SS_SWEEP_CONFIG=45=1 SS_SWEEP_PARTY_DROP=0
  SS_SWEEP_PARTY=1,0 SS_SCRIPT_GLOBALS=a0=6,a4=0x834 SS_AUTO_ADVANCE=1 SS_INSTANT_TEXT=1 ./run.sh
  m61 --no-memcard --debug --map 61 --frames 4500 --capture cap/m61 --capture-frames 4300,4460`
  (add `--ap-seed seeds/rena_story.apso2` for the AP story seed); Claude is at about (255,150)
  in frame 4300 and (165,155) in 4460.
