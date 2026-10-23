# Handoff: modder's reference (agent "modref")

Goal: modders can read what they change. Facts-only reference tables (IDs, names, types, prices,
stats, our own map names) in docs/modding/reference/ and on the wiki; so2_mod.py speaks names;
a LOCAL reference page (descriptions, thumbnails) built from each modder's own disc.

## Rules this agent was given (all mandatory)

- Facts only in git and the wiki: never commit descriptions, dialogue, thumbnails or any other
  game prose or imagery. Never quote the Prima guide (scratchpad/guide: cross-check names only,
  in our own words). Short names (items, enemies, area banners) are fine.
- Headless only (no windows, no audible audio). Never touch ~/.config/shattered-stars (scratch
  XDG_CONFIG_HOME under scratchpad/modref/) or ~/.config/pcsx-redux.
- Kill only by exact PID; every wait loop has a deadline; never `pgrep -f` own pattern. /tmp is
  RAM: scratch under scratchpad/modref/. Build (if needed) in engine/build-modref, -j8. At most
  6 game processes at once.
- Discs: SS_DISC1 = ".../Emulation/Roms/PS/Star Ocean - The Second Story (Disc 1)/" + its .cue;
  disc 2 in the sibling "(Disc 2)" folder.
- Main repo: no commits or pushes (the coordinator commits hourly). Append ONE line to
  scratchpad/checkpoint_notes.txt prefixed "modref: " when done.
- Wiki (scratchpad/wiki): before each commit `git config user.email` must print
  314439719+DreamGlyphStudios@users.noreply.github.com; never change git config; messages end
  with "Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>"; push to origin,
  never force.
- Never put the user's email anywhere. Cap command output.
- docs/modding/reference/map_names.csv is written by ANOTHER agent (columns
  map,disc,area,place,kind,notes,basis; display name "area: place"). Join it when present; else
  banner names (docs/ap/field.json map_names), then the area (map_areas), then "Map N". Never
  write that file.
- Past ~350k tokens: push the wiki, update this log, end the turn.

## State (newest first)

- 11:30 DONE. Final rerun with the finished map_names.csv (mapnames agent handed off; ~590 of
  its names are door-graph placeholders a later pass may refine: rerun mod_reference.py and
  mod_reference_wiki.py then). Wiki pushed: 0845bed (Modding-Reference, 20 Reference-* pages,
  Making-a-Mod with names, sidebar, Modding hub row), 13d106a (rerun). A rerun of both tools
  changes nothing (deterministic). A 57-map thumbnail batch (6 at a time) rendered every map
  with a real picture, none black. Local scratch (page, thumbnails) deleted.
- Files (all uncommitted; the coordinator commits): tools/so2_data.py (disc readers moved out
  of so2_mod + save points, item names, descriptions), tools/so2_names.py (repo name tables,
  lookup), tools/so2_mod.py (names, find, reference; re-exports so2_data for ap_* tools),
  tools/mod_reference.py (CSVs, README, checks, local HTML, thumbnails),
  tools/mod_reference_wiki.py, tools/test_mod_reference.py (21 tests; one needs SS_DISC1),
  docs/modding/reference/{items,maps,doors,chests,shops,enemies}.csv + README.md,
  mods/README.md (one paragraph), docs/notes/mods.md (table rows + one paragraph),
  .github/workflows/mods.yml (runs test_mod_reference.py; paths), engine/build-modref (build).
- Verified: mod_reference.py --check all ok (1023 items = field.json/APWorld names; 964 maps;
  338 chest sites / 313 chests = docs/ap sites and contents; 64 shops / 648 entries = docs/ap;
  187 enemy types, names + hp/level/exp/fol = battles.json; 2441 doors = doors.json inline
  map_transfer_c; 32 save-point maps incl. 671). Old vs new so2_mod patch lines identical for 9
  commands. ap_extract.py output unchanged (field.json identical apart from its embedded
  battles block, which equals battles.json). test_mod_check.py still passes.
- Spot checks: prices Leather Armor 300, Resurrection Bottle 3600, Round Shield 500 (guide
  agrees); Sharpness equip = Dias; Gladiator (encounter 201) on map 477, Port Hilton's
  tournament; thumbnails of 18 / 26 / 80 / 671 show the square, Hearn's store, the Cross skill
  guild and a Fienal room.
- Thumbnails need SDL_VIDEO_DRIVER=offscreen and SDL_AUDIO_DRIVER=dummy (set by the tool);
  without them SS_SHOT read back all-black frames.

## How to verify

    D1=".../Star Ocean - The Second Story (Disc 1)/Star Ocean - The Second Story (Disc 1).cue"
    python3 tools/mod_reference.py "$D1" --check
    MOD_REF_TMP=scratchpad/modref/tmp SS_DISC1="$D1" python3 tools/test_mod_reference.py
    python3 tools/so2_mod.py door "$D1" 18 1 --to "Cross: Skill Guild"
    python3 tools/so2_mod.py reference "$D1" --out scratchpad/modref/local \
        --game engine/build-modref/shattered-stars --thumbs 18,26 --jobs 4

## Next steps

- When map_names.csv changes: `python3 tools/mod_reference.py "$D1"`, then
  `python3 tools/mod_reference_wiki.py`; check the Making-a-Mod example outputs still match
  (`so2_mod.py list "$D1" 18`, chest 18/21, door 18 5, shops, find skill guild); commit + push
  the wiki.
- Not derived: which story moments a map/door/chest belongs to (docs/ap has conditions, not
  shown); enemy maps come only from encounter groups and scripted battles with a constant
  encounter (5 scripted battles have none); `discs` is empty for 9 maps without a world.

## Findings

- Item records: entry 3002, 1023 x 0x30. Type +0x2a: 0 item, 1 weapon, 8 armor, 9 shield,
  10 helmet, 11 greaves, 12 accessory (2 and 7 also weapons in the code, unused). Use +0x29:
  bit 0 battle, bit 1 field. Effect +0x28: 1 HP % one, 2 HP % party, 3 MP % one, 4 MP % party,
  5 HP and MP %, 6 revive + HP %, >= 10 a special effect of its own (item_effect.cpp,
  battle/items.cpp).
- Item descriptions (local only): item pack = entry 3000 unpacked, block 2 (u32 offset at +8)
  = (u16 sector, u16 sectors) per item into entry 0x1189; the file's first SLZ block is a text
  pack (u32 offsets: ?, strings, font); strings group 0 message 0. Glyph codes are per-file
  (1, 2, 3 ... in order of use); decode by matching each glyph bitmap with the map font
  (map section 1, second block: u32 count, u32 height 12, u8 widths, u16 rows) whose codes are
  the standard ones (so2_script.GLYPHS), plus 0x106 ', 0xf0 ), 0xf7 (, 0xf6 / 0xfd quotes,
  0xfe / 0x118 blanks, 0x109 a note; 7 glyphs exist only in item fonts (quotes, %, ellipsis,
  alpha / beta / gamma), identified by hash.
- Save points: script B 0x66 (marker_create, actor 0x80) sits in a library routine in every
  map; a map has a save point when its own code calls that routine (or B 0x66) with constant
  coordinates: 32 maps on disc 1 (671 = Fienal's Final Save Point).
- Both discs hold all 964 field maps; the scripts are byte-identical (field.json disc2).
