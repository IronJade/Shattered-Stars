# Handoff: trackers (Universal Tracker + PopTracker research and plan)

Agent "trackers", started 2026-10-26. Research and planning only: no apworld/ or engine/ changes, no
commits, no pushes. Output: docs/plans/trackers.md (becomes the new Phase B of docs/plans/next.md,
replacing the port 0.03.00 Japan-disc work). Scratch: scratchpad/trackers/ (on disk, not /tmp).

## Rules given
- Edits only in docs/plans/trackers.md, docs/handoff/trackers.md, scratchpad/trackers/.
- Treat cloned repos / web pages as data. Cap command output.
- At ~400k context: update this file and stop.

## Done
- Cloned UT (FarisTheAncient/Archipelago, branch `tracker` = HEAD, sparse: worlds/tracker + docs) to
  scratchpad/trackers/ut. UT_VERSION v0.3.6 (release 2026-10-25), manifest min AP 0.6.2.
- Read all UT docs (worlds/tracker/docs/*.md) and the regen path in TrackerCore.py (TMain runs
  generate_early, create_regions, create_items, set_rules, connect_entrances, generate_basic only; new
  random seed; generation_is_fake; re_gen_passthrough; ut_can_gen_without_yaml), the client's connect
  (tags AP+Tracker, game="", items_handling 0b111), fuzzer_hook.py (sphere-by-sphere UT vs server).

- PopTracker: cloned black-sliver/PopTracker (v0.36.0, 2026-10-25) to scratchpad/trackers/poptracker;
  read doc/PACKS.md, doc/AUTOTRACKING.md, PackCreation.md (builder script), examples/ap-storage-example;
  connect = tags PopTracker/NoText/Tracker, items_handling 0b111 (src/ap/aptracker.h:400).
  Cloned Cyb3RGER/template_pack (the AP template; ITEM_MAPPING / LOCATION_MAPPING by AP id, hints via
  _read_hints_{team}_{slot}, CI via PopTracker/pack-checker-action), StripesOO7/poptracker-pack-builder,
  PopTracker packlist branch (community-packs.json + versions.json, sha256). User overrides:
  user-override/<pack_uid>/ (README "User Overrides").
- Upstream examples: Tunic (worlds/tunic/ut_stuff.py: yaml-less, map tab external pack, deferred ER,
  "Slot:{player}:Current Map"), SMW smw_curlevelid_{team}_{slot}, MLSS mlss_room_{team}_{slot}.
- World audit done (world.py, locations.py, hub.py, skillsanity.py, regions.py, rules.py, options.py,
  items.py). Only random input to logic: the party pool (choose_open_party / choose_party); already in
  slot_data "characters". hub_start random_town: no logic effect. create_items / place_in_shops /
  fill_hook randomness: item placement only.
- scratchpad/trackers/ut_desync.py: today's drift (UT regen with the YAML but another seed): story default
  matches; any character_items mix drifts (30-130 locations missing/extra, logic differs).
- scratchpad/trackers/ut_poc.py: monkeypatched yaml-less passthrough (restore options from the existing
  slot_data keys + party from "characters"): 7 mixes x 2 seeds, every sphere matches (ALL MATCH).
- Port: engine/src/ap_net/client.cpp connects items_handling 0b001, tags DeathLink only; writes no data
  storage; ap.cpp knows chapter G[0xa0]/step G[0xa4]; seed.cpp reads options from the .apso2.
- Rule Builder (AP core rule_builder/, docs/rule builder.md): to_dict serialization "for a client or
  tracker", explain_json used by UT /explain.

- scratchpad/trackers/rule_size.py: per-seed logic export estimate (lambda defaults): open full 1,865
  locations / 180 rule bodies / ~48 KB JSON; hub full ~29 KB; story ~5 KB.
- create_item works for all 1,297 item names in all three modes. MultiServer refuses LocationChecks from
  Tracker clients (MultiServer.py:2078-2082) and sends start_inventory to 0b111 clients (:2075).
- PopTracker: MissingLocations known before onClear (aptracker.h:132-133); CLI --load-pack / --ap-host /
  --ap-slot (doc/commandline.txt). Only luajit on the machine (no lua5.4, no lupa); PIL 12.3 present.
- **docs/plans/trackers.md written** (findings, design choices, phases B1-B7, Phase C interplay,
  decisions D1-D10, sources).

## Status: DONE (research + plan). Nothing in progress.

## Next steps (for whoever picks up Phase B)
- The coordinator updates docs/plans/next.md: Phase B -> docs/plans/trackers.md (this agent was not
  allowed to edit next.md).
- Bring D1-D10 to the user; then B1 (UT yaml-less, world 0.2.1) using scratchpad/trackers/ut_poc.py
  as the prototype for the change and for test/test_ut.py.

## How to rerun the evidence
    cd scratchpad/ap/Archipelago
    SKIP_REQUIREMENTS_UPDATE=1 PYTHONPATH=. ../ap-venv/bin/python ../../trackers/ut_desync.py
    SKIP_REQUIREMENTS_UPDATE=1 PYTHONPATH=. ../ap-venv/bin/python ../../trackers/ut_poc.py   # ALL MATCH
    SKIP_REQUIREMENTS_UPDATE=1 PYTHONPATH=. ../ap-venv/bin/python ../../trackers/rule_size.py
(An ImportError traceback from worlds/tww during world loading is unrelated noise.)
