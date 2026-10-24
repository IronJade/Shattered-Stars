# Soulmaster battle divergence (agent `soulmaster`, 2026-10-23 from ~21:15)

Task: the PAL battle sweeps (q/bt_eu, bt_fr, bt_de) differ from NA in enc466 and enc478 (5 more
RNG draws a frame from frame 610 / 672). Find the cause; fix it if it's a port bug.
Rules: as given (docs/handoff/regions.md "Rules given" plus: build dir engine/build-soulmaster
-j4, scratch in scratchpad/soulmaster/, don't touch tools/battle_sweep.py or q/bt_*/logs, no
commit / push, one line per step in scratchpad/checkpoint_notes.txt "soulmaster: ").

## Findings

1. Reproduced (scratchpad/soulmaster/bt1.sh REGION ENC NAME [VAR=..]: one run as
   battle_sweep.py run_one does, prlimit 4 GB): enc478 NA = the sweep's NA trace; EU parts at
   frame 610 (draws 3067 vs 3062).
2. SS_RNG_SITES=3050:3090 + addr2line on bin/ss_q: from frame 610 each logic frame NA draws 5
   (4x rng_between + 1 rng_next in spell_mod_extinction step 2: one meteor), EU 10: two
   Extinction casts run at once.
3. SS_SPELL_TRACE: NA frame 576 e0 casts Extinction (0x25) alone, combo 0; EU e0 and e3 cast it
   together, combo 2 / 4 (a spell clash, result 0x40002 from spell_clash_check).
4. The EU log has `[port] region: NA address 80085508 / 80085509 / 8008550a of image 7c0.1 has
   no counterpart`: entries 5-7 of skill_no_clash (7c0.1 0x80085504, u8[7] = 03 26 13 14 24 25
   19) read as 0 on PAL, so Extinction (0x25), Daemon's Gate (0x24) and Earthquake (0x19) can
   clash there. The bytes are the same on every release:
   - NA  0x80085504: 03 26 13 14 24 25 19 | 10 (pad)
   - EU  0x800852ac: 03 26 13 14 24 25 19 41 42 | 08 00 1c (pad)
   - FR / DE same list, pad 80 03 3c.
   PAL's spell_clash_check (REA, eu1_battle_ovl1984_1.elf 0x80081b34) loops 9 entries (NA 7):
   the PAL edit adds 0x41 'The Scream' and 0x42 'Judgement Day' (item spells). The word at NA
   0x80085508 (24 25 19 10) differs from PAL's (24 25 19 41) in its pad byte, so
   tools/region_map.py (word granularity) left it unmapped: segments {0x3c30, 0x1b0, -1620},
   {0x3de4, 0xc, -1616}. Its check never saw the miss: tools/regions/na_reads.txt has no 7c0.1
   reads (the battle sweeps' read logs weren't merged into it).
   => port bug (a misread table), not release data.
5. 99 runs of each PAL sweep logged the three misses; nothing else missed in the battle sweeps.

6. Every logged read checked (tools/regions/na_reads.txt + all scratchpad/regions/q/reads/*:
   8423, of which 1733 were never in the list): only the never-list bytes are unmapped; one more
   "suspicious" read, battle 0x800851aa (Claude's 10th killer-move entry, s16, ends 20 bytes
   before a layout change: fine, READ_EXTENTS entry added).
7. Static scan (scratchpad/soulmaster/work/globgaps.py): cataloged tables (globals.csv) holding
   an unmapped NA word, per release: 7c0.1 clash_opposed_mask (no port reader: projectile clash
   isn't registered in the port), g_menu_pages_debug_step (field / title; port state), title
   load queue (EU only; port state), credits_colours word 0 (the card script's end, read as a
   run in the release's image; colours are read from +4). None is a misread.

## Fix (done, verified 22:50)

- tools/region_map.py: TABLES "7c0.1" (0x80085504, 7) and READ_EXTENTS battle 0x800851aa.
- engine/data/regions/{eu,fr,de}.inc regenerated: one line each, k7c0_1_seg
  {0x3c30, 0x1b0, -1620} -> {0x3c30, 0x1b4, -1620} (na_modules.inc unchanged; the unchanged
  tool reproduced the committed .inc files byte for byte first).
- tools/regions/na_reads.txt: + the 1733 sweep reads (8423); region_map --check: 0 unmapped,
  0 suspicious, 0 fatal for EU / FR / DE.
- engine/tests/test_region.cpp check_release_images: the 7 entries read the same through each
  PAL map as on NA.
- docs/notes/regions.md (PAL edits: the 9-entry list; coverage paragraph), docs/catalog rows
  (functions spell_clash_check PAL evidence, globals skill_no_clash).
- First check: enc478 EU on the new build = q/bt_na's trace (all 600 [bunits] lines), no misses.

Results (engine/build-soulmaster):
- Suites: ss_tests 99/0 (region disc tests EU / FR / DE ran, with the new check), ss_game_tests
  129/0, smoke 86/0, ref_regress quick 28/28 (scratchpad/regress/20261023-215256).
- Battles (work/cmp.py, against q/bt_na traces and endings): EU 22/22, FR 22/22, DE 22/22
  identical (enc466, enc478, 12 same-stage runs that hit the clash check, 8 others), NA control
  2/2; no region misses. enc478 now runs to the frame cap as on NA (was defeat at 1644).
- NA is unaffected by construction (NA has no maps; the tool, .inc and test changes only touch
  the PAL maps) and the NA suites are green.

Who else it touched: any PAL battle where Daemon's Gate (0x24), Extinction (0x25) or
Earthquake (0x19) is cast while another unit is chanting long enough to pair (+0x174 < -10):
the port let the pair clash (0x10001 / 0x30003 / 0x20004 / 0x40002: cancel, hit own side,
power-up, centre moved) where NA (and the PAL console) never clash those spells. Casters:
party Leon (0x24 / 0x25), Noel (0x19); monsters Soulmaster, Cyril, D-feather, Greatergoat and
the rest listed in docs/notes/spell_coverage.md. The other 365 sweep encounters (different
stages from NA's, sweep tool issue) can't show it in the comparison; 99 runs per PAL sweep
reached the miss.

## Files touched

- tools/region_map.py, tools/regions/na_reads.txt, engine/data/regions/{eu,fr,de}.inc,
  engine/tests/test_region.cpp, docs/notes/regions.md, docs/catalog/{functions,globals}.csv,
  docs/handoff/soulmaster.md; scratch under scratchpad/soulmaster/.

## Next steps / how to verify

- `python3 scratchpad/soulmaster/work/cmp.py` after "batch done" in work/batch.log; suites in
  suites_fix.txt.
- Map check: `python3 tools/region_map.py --na NA1.cue --disc RG1.cue --cache
  scratchpad/regions/cache --check` (reads the new na_reads.txt).
- Known difference to record (by decision): PAL's never-clash list adds The Scream / Judgement
  Day; the port runs NA's 7.
