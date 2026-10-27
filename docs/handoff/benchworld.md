# Handoff: benchworld (the APWorld side of the 12-member bench, so2.apworld 0.2.1)

Agent "benchworld", started 2026-10-27 ~11:45. Scratch: scratchpad/benchworld/.

## The user's decisions
- 2026-10-27, verbatim: "Bench needs to be rolled into the 0.02.51 + 0.2.1 release."
- docs/handoff/bench.md "The user's answers": B7 a `bench` option, **on by default**; B3 everyone
  but the hero can be benched; B6 the join level is the median over the 8 slots (port side).
- The motivation (the user): "make every character obtainable ... keep from causing any issues
  wherein the user gets a full party and there are 4 checks that were the character items ... It
  also guarantees that the player can choose to use whoever they would like in the party."

## The contract (party-bench, read-only: seed_format.md "The bench", seed.cpp)
- `options.bench` true / 1, read only with party_rules "open" in open world, and in the hub;
  anywhere else it reads as off. A bench seed is **version 4** (a port reading 1-3 refuses it).
- The port's `starting_character: "any"` (a string) = every member 1-12 minus exclude_characters;
  the APWorld writes the list itself (Party.choices), so with the bench the list is every member
  not excluded (the heroes are never excluded).
- The hand-made test seeds (scratchpad/partybuild/seeds/bench.apso2, mini6/bench*.apso2): version
  4, options.bench true, nothing else new.

## Rules given
- Python from scratchpad/ap/Archipelago with ../ap-venv/bin/python, SKIP_REQUIREMENTS_UPDATE=1.
- Low load: pytest -n 4; at most 1 heavy job at a time; generation under prlimit --as (8 GB).
  Never touch scratchpad/wt/party (agent "benchmerge" builds and runs the port there).
- Touch only apworld/so2/ (not the player guide apworld/so2/docs/*.md, nor docs/ap/seed_format.md,
  until the coordinator says party-bench is merged into main), tools/ap_batch*, docs/ap/apworld.md,
  docs/ap/release_checklist.md, docs/release_notes/0.2.1.md and this file.
- Commits on main after each verified piece, docs first, staged by path; message ends with
  `Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>`. Never push, tag, publish.
- Never leave uncommitted edits in docs/ or apworld/so2/docs/ when stopping (merges happen).
- Denied tool call: don't retry; write the blocked step here, stop with a message naming it.
- Cap output; long jobs in the background with deadlines; kill only by exact PID. Roll over ~400k.

## Plan
1. [x] World: options.Bench (DefaultOnToggle), world.bench (applies: open world + character_items,
   or hub), party size 12, no sample, no healer swap, version 4, slot data `bench` only when it
   applies (bench off: byte-identical seeds).
2. [x] Tests: bench mixes in test_party.py / test_hub.py / test_v2.py; 8-place mixes pinned.
3. [x] Checker: tools/ap_batch_check.py for the bench.
4. [x] Identity: all 288 seeds identical (bench: "false" pinned vs scratchpad/hellseed/batch).
5. [x] Batches with the bench on (96/96 each, 0 errors); fill rates 5 x 100 (0 FillError);
   world 699 / general 379 / scoped 752 / unittest 699 OK.
6. [x] Docs: options docstrings, apworld.md, release_notes/0.2.1.md; after the merge (319f5ba,
   the coordinator 2026-10-27): seed_format.md (the `bench` row, Party v2's "keeps the party to
   8", "What the APWorld writes" under the bench) and the player guide (en_*.md: the pool, the
   bench bullet, exclude / healer / select, hub, options row; setup_en.md: "unsupported version 4").
7. [x] End-to-end proof on main's merged port; dist/apworld-0.2.1/ rebuilt and verified.

## Status (2026-10-27 ~12:30): DONE (steps 1-7)
- Commits: 809276c (docs), 64a7d20 (world + tests + tools), then the step-7 docs commit
  (release_checklist.md section 10, 0.2.1 sums, apworld.md e2e bullet, this file).
- Results (also in apworld.md "Results ... the bench" and release_checklist.md section 10):
  - Identity: scratchpad/benchworld/players_off = tools/ap_batch_players/* with `  bench: "false"`
    appended to each SO2 YAML; run_off.sh -> off/{sk_full,sk_hub,solo}; identity.py
    ../hellseed/batch off -> identity.txt: 96/96 identical each (480 .apso2 byte for byte, 288
    spoilers but their "Bench: No" lines); the checker's output the same but `('bench', False)`.
    The baseline was made at b2d7a75; apworld/so2 unchanged from then to 51e3aa7 (git log).
  - on/: story_open, hub, solo_noexpel 96/96, 0 checker errors; bench seeds 60/192, 168/192, 49/96.
  - fill/summary.txt: open, open_moves, open_noexpel, hub, hub_moves: 0/100 FillError each.
  - Suites: world 699 (50730), general 379 (117745), scoped 752 (56003), unittest 699 OK.
  - E2E (scratchpad/benchworld/e2e/run.sh, engine/build-benchworld built from main after the
    merge, -j4, rc 0): runA/B/C.log, shots/ (frame01180.png: the bench view, "Held: Leon").
  - Package (scratchpad/benchworld/pkg: build.log, run.sh, out/, dev/; old files in pkg/old):
    so2.apworld 186265 bytes, 31 files, sha256 83af433fa5f17a09e59b29eae859efd9df945e3b3eaefc59b04d6749f019849f;
    YAML 93c41087a011bb2e8c54419c3a7ca1c44d96b2ccd1c8583a4fb277728eb7d035. Version stays 0.2.1.
- Design note: the option is on by default, so story mode logs an info line (not a warning) when
  ignoring it; open world without character_items logs a warning.
- Not done (not asked): no tag, push or publish. engine/build-benchworld can go when not needed.

## Key files
apworld/so2/{options,items,world}.py; apworld/so2/test/{test_party,test_hub,test_v2,test_difficulty}.py;
tools/ap_batch_check.py, tools/ap_batch.sh (header); docs/ap/apworld.md, docs/ap/seed_format.md,
docs/release_notes/0.2.1.md, apworld/so2/docs/{en_Star Ocean The Second Story,setup_en}.md.
Scratch: scratchpad/benchworld/ (quick.py, identity.py, run_off.sh, run_on.sh, players_off/, off/,
on/, fill/, t_*.log).
