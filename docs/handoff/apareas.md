# Handoff: Archipelago map-to-area audit (apareas agent, 2026-10-23)

## Assignment
Audit field.json `map_areas` (tools/ap_extract.py `areas()`, a nearest-banner search) against
docs/modding/reference/map_names.csv `area`; trace every use in the extraction, the door table,
the APWorld logic and the port; decide whether any location's open-world requirement is wrong;
fix at the source (real hazard: regenerate, full battery, world_version 0.1.1, changelog) or,
if cosmetic, fix the labels and show the logic and seeds are byte-identical.

## Status
- Step 1 (mismatch list): in progress. Script: scratchpad/apareas/mismatch.py.

## Rules given
- Logic must not drift unless it fixes a proven error; any change re-proven with the full
  battery (world tests, AP general tests, doors validator, 64+ seed batch 0 errors, late proof,
  port e2e on a local server).
- AP servers only on 127.0.0.1 (tools/ap_test_server.sh); never archipelago.gg.
- No game data in git beyond docs/ap's IDs/offsets/names. Headless only. Scratch
  XDG_CONFIG_HOME under scratchpad/apareas/; never ~/.config/shattered-stars or pcsx-redux.
- Kill by exact PID; every wait loop has a deadline; no `pgrep -f` of own pattern. Scratch in
  scratchpad/apareas/ (not /tmp). At most 4 game processes. Builds engine/build-apareas -j8.
- Don't touch region-profile / FR-DE text agents' files; docs/modding/reference/*.csv read-only.
- No commit / push. One "apareas: " line in scratchpad/checkpoint_notes.txt when done.
- Don't publish a .apworld; the coordinator asks the user.
