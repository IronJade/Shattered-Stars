# Handoff: field map names for the modder's reference (mapnames agent)

Started 2026-10-23. Resume from this file. Owner area: `docs/modding/reference/map_names.csv`
(the only file that goes in git from this task), `scratchpad/mapatlas/` (scratch, git-ignored:
thumbnails, render scripts, the door graph, the classification plan).

## Assignment (from the coordinator)

Name every field map of SO2 for modders, who otherwise see only numeric map IDs. Output
`docs/modding/reference/map_names.csv`: `map,disc,area,place,kind,notes,basis`, header, LF
endings, sorted by map then disc. Full brief: see the task's own text (it's long); the short
version is in the Rules section below.

## Status at handoff

**964/964 rows written** (every id in `docs/ap/field.json`'s `field_maps`). Quality varies:

- **142 "sure"**: the 3 world-map screens (1 Expel, 2 Energy Nede, 3 "world map 3"), their 137
  overworld terrain chunks (909-980, 1048-1110, 1113-1114 -- internal subdivisions, not separate
  destinations), and maps 0/826 (an unused slot sharing the world-transfer script with 1-3).
- **~175 "guess"/banner**: the map's own in-game banner text (`ext_b9`, decoded into
  `map_names`) used directly as `place`, basis=`banner`. High confidence, no thumbnail needed
  -- the game told us the name itself (shops, inns, named rooms).
- **~75 hand-verified with thumbnails**: `basis` includes `thumbnail`. Full door-graph +
  thumbnail pass done for **Arlia Village** (17-41 cluster incl. the night-time prologue
  copies), **Alen's Mansion** (46, 49), **Town of Cross** (73-90 incl. its story-state plaza
  variants), **Town of Salva + Salva Drift** (43-71 cluster), **Town of Lacour**'s main plaza
  (211/213/214) and **Lacour Castle**'s Registration/Throne Room/King's Room/Front Line/Meeting
  Room clusters (doors-based, no thumbnail needed once the room graph was clear).
- **~591 "doors-only placeholder"**: `notes` contains the literal word "placeholder" and
  `basis` is bare `doors`. These came from `scratchpad/mapatlas/scripts/placeholder_pass.py`:
  multi-source BFS from each area's "entrance" tile(s) (a map with its own banner, or reached
  from outside the area) over the door graph, labelled `"entrance / threshold area"` (depth 0)
  or `"interior room, about N doors in from the entrance"` (depth N). Every one of these still
  has the right `area` and a defensible `kind`, but `place` is NOT a real name yet -- this is
  the main remaining work. The big offenders: **Cave of Trials Level 1 (128 maps!)**, **Hoffmann
  Ruins (46)**, **Mountain Palace (41)**, **Fienal (39)**, plus most of **Central City, Field
  of Power, Mars Village, Town of Linga, Cross Cave, Giveaway, Eluria Tower, Port Hilton, Fun
  City, Port of Clik, Town of Herlie, North City, L'Aqua, Town of Armlock, Heraldry Weapons
  Laboratory, Sanctuary of Linga, Zand's Mansion, Field of Intelligence/Courage/Love** -- i.e.
  almost every town/dungeon past Lacour hasn't had its plaza/floor split verified yet.
- **5 genuinely unresolved** (231, 625 note below, 643, 687, 697): no banner, no `map_areas`
  entry, and either no doors at all or doors that don't reach anything named. Marked
  `kind=special`, `area=""`, `place="(unidentified)"`. 625 got one extra fact: it's the map
  whose script runs the disc-2 swap (`docs/notes/m8_audit.md`, script B 0x6a / word 5608) --
  not a visitable place, so it's fine as `special` even though it's "real" in the sense of
  having a script.

Examples the coordinator asked to be nailed (all done, hand-verified with thumbnails):
- **map 17**: Arlia Village, "south: bridge and forest path".
- **map 18**: Arlia Village, "north: fountain square" (the two are the fountain/shops half vs.
  the bridge/forest half; confirmed both visually and from arrival-facing angles in the door
  table -- 17->18 arrives facing north (dir 0), 18->17 arrives facing south (dir 180), so 18 is
  north of 17).
- **map 26**: Arlia Village, "Hearn's General Store (inside)" -- banner reads `General Store
  "Hearn's of Arlia"`.
- **Cross's Skill Guild = map 80**: Town of Cross, "Skill Guild (Skillie)" -- banner reads
  `Skill Guild "Skillie"`. (Map 173, "Skill Guild Skill Power", is a *different* skill guild,
  in Port Hilton -- don't confuse the two; this was worth double-checking since both share the
  word "Skill Guild".)

## How (the pipeline, reusable for the rest)

All scripts in `scratchpad/mapatlas/scripts/` (git-ignored; rewrite if a fresh session needs
them and they're gone -- nothing here is precious, it's all derived from `docs/ap/field.json`
and the discs):

1. **`classify.py`**: reads `docs/ap/field.json` (`field_maps`, `map_areas`, `map_names`,
   `late_maps`, `world_maps`). Classifies world/system maps by id (see constants at the top:
   `SPECIAL_WORLD_SCRIPT={0,826}`, `WORLD_SCREENS={1,2,3}`, `CHUNK_RANGES`). For everything
   else: if the map's own banner differs from its area's name, uses the banner as `place`
   (confidence "guess"); if the banner *equals* the area name (ambiguous -- which map is
   "Arlia Village" really?) or there's no banner at all, marks it "todo". `kind` is guessed
   from keywords in the banner/area text (`DUNGEON_WORDS`, `FIELD_WORDS`, `BUILDING_WORDS`,
   `TOWN_WORDS` near the top of the file -- a named sub-place checks dungeon/field/building
   keywords *before* falling back to the area's own town/dungeon/field word, since a shop
   inside "Town of X" is an interior, not a town). Writes `scratchpad/mapatlas/plan.json`.
   **Re-run this if you add area overrides** (see the Port Hilton/Lacour Castle bug below) --
   but it has no `if __name__` guard, so importing it (as `fix_single_member.py` does) re-runs
   and reprints its summary; harmless but noisy.
2. **`all_doors.py`**: opens disc 1 *once* (`so2_mod.Disc`) and pulls every map's
   `map_transfer_c` targets directly via `so2_mod.transfers()` -- much faster than shelling out
   to `so2_mod.py list` per map (964 maps in ~7s). Writes `scratchpad/mapatlas/doors.json`,
   `{map: [target, ...]}` (duplicates kept -- a map can have two doors to the same target).
   Disc 2 was **not** queried: `docs/ap/field.json`'s own `disc2` summary says
   `identical_maps=820, different=[], only_disc1=[], only_disc2=[]` -- map scripts (hence
   doors/chests/geometry) are byte-identical across both discs. Only game *state* (story
   flags) differs by disc, and a `--map N` debug start doesn't carry disc-specific flags
   anyway, so disc 1 alone is sufficient for every render and every door query; `disc=1` for
   every row in the CSV so far for that reason (see "disc column" below).
3. **`placeholder_pass.py`**: for every "todo" map, multi-source BFS over the *within-area*
   door subgraph from "entrance" tiles (own banner present, or reached from outside the area)
   to a depth-based generic `place`. **Idempotent and safe to re-run**: it reads the current
   CSV first and skips any row that isn't itself a bare-`doors` placeholder, so it will never
   clobber hand-verified work -- but see the bug I hit below if you're calling the merge steps
   in a different order than written here.
4. **`bulk_export.py CONF,CONF out.json`**: dumps `plan.json` rows at given confidence levels
   (`sure`, `guess`, `todo`) to a batch JSON, skipping map ids already in the CSV.
5. **`update_csv.py batch.json`**: merges a batch (list of row dicts) into the CSV by
   `(map, disc)` key, rewrites the whole file sorted, LF endings. This is the *only* writer --
   every batch, including hand ones, goes through it so sort order and formatting stay
   consistent. Hand batches are plain JSON files you write yourself (see `batch001.json`
   -3.json` for the Arlia/Cross/Salva/Lacour examples -- a list of row dicts, same columns).
6. **`fix_single_member.py`**: a one-time fix for a real bug (see below), already run; no need
   to run again unless you reset `classify.py`'s word lists and regenerate from scratch.
7. **`render.sh MAP [MAP...]`**: headless thumbnails. `SDL_VIDEO_DRIVER=offscreen`,
   `SDL_AUDIO_DRIVER=dummy`, scratch `XDG_CONFIG_HOME=scratchpad/mapatlas/config`,
   `--headless --no-memcard --map N --frames 191` with `SS_SHOT=190:path.png`. At most 6
   engine processes at once (bash `wait -n` throttling). Binary: `engine/build-mapatlas/`
   (own build dir, built fresh at `-j8` from a clean `engine/` tree at the start of this task;
   rebuild if `engine/src` has moved on since). ~0.3s per map -- cheap; the expensive part is
   *looking* at the PNGs, not making them.
8. **`resize.py`**: shrinks `thumbs/raw/*.png` to 320x240 into `thumbs/small/`, deletes the
   raw copy. Always run this before `Read`-ing a thumbnail (keeps image tokens down).
9. Then: `Read` each `thumbs/small/mapNNNN.png` once, cross-reference the banner
   (`map_names`), the doors (`doors.json`), and (not yet used this session -- see below) the
   strategy guide, write a hand batch JSON, `update_csv.py` it.

### A bug worth knowing about (already fixed, but repeat it carefully)

`placeholder_pass.py`'s *first* version didn't check the existing CSV at all -- it dumped a
placeholder for literally every "todo" map, including ones already hand-verified in
`batch001.json` (Arlia/Cross), because a map whose banner *equals* its area name is always
"todo" in `classify.py`, no matter how well you've already named it by hand. Running
`update_csv.py` on that output **overwrote map 17/18/74's good rows with generic placeholders**.
Caught immediately by grepping the CSV right after (always do this), fixed by re-running
`update_csv.py` on `batch001.json` again (last-write-wins by design) and then patching
`placeholder_pass.py` to read the CSV first and skip anything not itself a bare-`doors`
placeholder. **If you hand-edit areas or add new hand batches, always apply them *after* any
bulk/placeholder pass, or re-run them afterward to be safe.**

### A real data bug found and worked around

`docs/ap/field.json`'s `map_areas` is generated by a nearest-named-map BFS search over doors
(see `tools/ap_extract.py`'s `areas()`/`place_names()`) -- a crude heuristic the coordinator
already knew about (it's *why* this task exists). One concrete case found and fixed by hand:
**map 427** is labelled `area="Port Hilton"` in `field.json`, but it's actually the Lacour
Castle corridor that the Conference Room/Great Dining Hall/Arsenal/Main Library (431/432/
433/435) all open off of, *and* it connects straight to the Registration and Throne Room maps
(420, 422) -- nowhere near Port Hilton. The BFS apparently followed a long unconditional door
chain across the map and grabbed the wrong nearest banner. Overridden by hand in the CSV
(`area="Lacour Castle"`, noted in that row). **There are probably more of these** in the
unexplored areas -- when a "todo" map's doors connect to rooms from a totally different,
unrelated-sounding area cluster, don't trust `map_areas` blindly; check the doors.

### The "disc" column

Every row so far is `disc=1`. Since map data is identical across discs (see above), I didn't
see a reason to duplicate rows for disc 2 *unless* a place is genuinely a disc-2/endgame-only
destination worth flagging to a modder (e.g. Energy Nede content). Nobody's done that
analysis yet -- if a future pass wants to mark certain areas "disc 2 / late game only", use
the `notes` column (e.g. "only after the disc-2 swap") rather than inventing a disc=2 row that
points at the same, identical map data; reserve an actual second row only if you find a case
where the *same map id* genuinely needs two different descriptions depending on which disc/
story-state you're in and one description would be misleading on its own.

### The strategy guide

**Not used yet this session** -- `scratchpad/guide/guide.txt` wasn't opened. Everything so far
came from banners, doors, and thumbnails, which were enough for the early towns. It'll help
most for: (a) the big unnamed dungeons (Cave of Trials, Hoffmann Ruins, Mountain Palace,
Fienal -- the guide likely describes these floor by floor), and (b) confirming which chapter/
story-state gates a cutscene-kind map. Remember: never commit or quote it verbatim, names/facts
only, in our own words, `basis` gets `guide pN`.

## Next steps, in priority order

1. **Finish the remaining early/mid-game towns' plaza splits** (same method as Arlia/Cross/
   Salva/Lacour): Town of Herlie, Town of Linga + Sanctuary of Linga, Port Hilton + Inn Hilton
   Island, Central City, Mars Village, Port of Clik, North City, Town of Armlock, L'Aqua, Fun
   City (note: Fun City has the bunny-racing minigame rooms already marked `kind=special`), Zand's
   Mansion, Eluria Colony/Tower, Cross Cave. Grep the CSV for `"entrance / threshold area"` or
   `"interior room, about"` within each area to find what's left; `doors.json` + a `render.sh`
   batch of ~6-8 per town is the pattern. Watch for the same night-time-prologue-copy pattern
   Arlia had (compare `late_maps` membership -- a map *absent* from `late_maps` that shares a
   script-era banner or visually matches another map 1:1 is likely a story-state duplicate, not
   a new place) -- but don't assume it; Arlia's prologue was a special case (the game's own
   opening), most towns won't have one.
2. **The four big dungeons** (Cave of Trials Level 1 128 maps, Hoffmann Ruins 46, Mountain
   Palace 41, Fienal 39) are the bulk of the remaining "placeholder" rows and the least
   tractable by hand -- 254 maps between them. Don't thumbnail each one individually. Better:
   pull the guide's floor-by-floor description (if it has one) and batch-assign `place="Level
   N"` / `"BN"` style names along the BFS depth already computed (the depth number in the
   current placeholder text is a reasonable proxy for floor already -- spot check a handful
   with thumbnails to confirm depth tracks floor number sanely before trusting it wholesale).
3. **Remaining fields** (Field of Power/Courage/Intelligence/Love, Giveaway, Lasguss Mountain,
   El Beach, Salva Drift's untouched tail, Mihne Cavern) similarly -- smaller clusters (under
   30 maps each), same doors+thumbnail method as Salva Drift.
4. Re-grep for any other `map_areas` mislabels like the Lacour Castle/Port Hilton one while
   you're in each area (check: do a "todo" map's doors lead somewhere thematically unrelated to
   its assigned area? If so, investigate before trusting the label).
5. Sanity-check pass at the end: every shared-banner area has distinct `place`s (grep the CSV
   for duplicate `area,place` pairs); every `kind=interior` row names the building; spot-check
   a few `north/south/east/west` claims against their thumbnails.

## Dungeons (mapnames-dungeons agent, 2026-10-23)

**Done: all four big dungeons, 254/254 maps, zero remaining `doors`-only placeholders.**
Worked in parallel with a `mapnames-towns` agent on the same CSV (towns/fields rows untouched
here). Partial audit file: `scratchpad/mapatlas/names_dungeons.csv` (232 rows, union of all
four dungeons' batches). Per-dungeon detail:

- **Cave of Trials (128 maps, area unified to `Cave of Trials`** -- it was `Cave of Trials
  Level 1` in `map_areas`/the CSV before this pass, a `map_areas` BFS mislabel like the Port
  Hilton/Lacour Castle one; fixed the 22 pre-existing banner rows' `area` too, not just the new
  ones). The dungeon's own banners (`ext_b9`) read `"Cave of Trials Level N"` **plus** a
  level title (`Heraldic Ruins` ... `Holy Nest of Angels`) at the gate room of most levels --
  this is a separate HUD overlay from the static banner field, only visible by rendering a
  frame (see thumbnail note below), and it matches the guide's 13-level breakdown exactly.
  Level boundaries: contiguous id ranges anchored on those banner rows (see the CSV's banner
  rows for exact boundaries, e.g. 716/727/737/740/744/751/756/760/763/766/775/777/786/788/
  791/794/798/807/808/811/814/820). Boss rooms from `docs/ap/field.json`'s `bosses[]`
  cross-checked against the guide (p130-133) and thumbnails for all 13 bosses + a few fillers
  (12 renders total). Chest rooms (45) named directly from `chests[]`, item names verbatim.
  Two anomalies worth knowing if you revisit this area: **map 710** is a "bonus vault" with 19
  chests spanning several levels' loot lists and doors reaching into 3 different levels' room
  clusters -- not really part of any one level's path. **Map 711** ("Weird Beast arena") is
  similarly cross-linked (doors reach Level 5/6/8 clusters despite its low, Level-1-range id);
  almost certainly the unwinnable Puffy/Weird-Beast "flee" encounter, with the real rematch at
  the Level 5 boss room (754). A few more boundary rooms near Level 1/2, 5/6, 12/13 are flagged
  in their own `notes` as "door graph ties it to more than one level" rather than force-fit.
  **Thumbnail gotcha**: a boss's gate room can show the *next* level's banner/title (e.g. map
  705, Darkfeather's room, displays "Love Alley / Level 2") because the boss physically guards
  the doorway into the next level and the overlay fires on crossing it -- don't assume the
  banner you see always names the room's own level, check which boss/chest data anchors it.
- **Hoffmann Ruins (46 maps)**. No in-game "Level N" banners here (single contiguous mine, not
  a multi-level banner-driven dungeon like Cave of Trials), so zones are the door graph's own
  hub structure, confirmed with 6 thumbnails: `entrance & approach` (478-482, exterior facade +
  the Ghost/Ernest boss just past it at 479), `the elevator down into the mines` (483, stairs
  down, matches the guide literally saying "the elevator"), `upper tunnels` (484-490),
  `mine-cart tunnels` (491-510, visually confirmed barrels+rail tracks at 491 and 499; also
  where the guide's "explosive charges" hazard most likely lives, not pinned to one room),
  `lower passages` (511-522), `the Energy Stone chamber` (523, visually a glowing green
  mineral mass, Harfainx boss). 17 chest rooms from `chests[]`. Guide p68-69.
- **Mountain Palace (41 maps)**. Forks right after the guarded entrance (355, thumbnail-
  confirmed) into two quest-dependent paths per the guide (p50): **Ashton's path** (west
  archway, boss Nightmare at 380 -- thumbnail shows an altar/dais flanked by two chests) and
  **Ernest's/Opera's path** (east passageway, boss Flarelizards x2 at 359 -- thumbnail shows
  the "large square chamber" the guide describes). Zones from a nearest-boss door-graph split
  (two-anchor BFS owner assignment), not a clean id range, since the fork isn't id-contiguous.
  8 chest rooms from `chests[]`. **8 maps (340/345/346/349/352/353/354/356) have zero doors in
  or out anywhere in the graph** -- no banner, no chest, no boss either. Guide text explicitly
  says the boss location (and by extension, presumably some rooms) depends on which character
  (Ashton vs Opera) is in the party, so these read as alternate/flag-gated copies of rooms used
  by the other quest thread rather than ordinarily walkable space; flagged as such, not forced
  into either path. Worth a deeper look if someone ever wants exact certainty on these 8.
- **Fienal (39 maps)**. Lobby (657, thumbnail-confirmed grand hall) hosts *two* story-distinct
  boss encounters at the same map id: Marsilio's unwinnable first-visit ambush, and the
  Ruprecht/Nicolus/Jibril fight on the real second visit -- both per `field.json` `bosses[]`.
  The guide (p115-119) describes floors 1-8 reached by a mix of corridors and **transporters**
  ("pink/green letter" pairs on its own maps); that explains two anomalously high-degree nodes
  (652: 13 doors, 653: 14 doors) whose thumbnails show a circular platform with transfer-glyph
  signage -- labelled `transporter hub`, not folded into the floor-number zones. Floor 5 boss
  (Vesper & Decus, 669) and Floor 7 boss (Cyril, 674) are thumbnail-confirmed; Floor 8 ends at
  Indalecio's chamber (685, a glowing green sphere, final boss). Floor 6 is a no-monster logic
  puzzle per the guide (4x4 switch grid) but its exact rooms weren't identifiable in the door
  graph (the switch-gated doors likely aren't captured by the static `map_transfer_c` scan, the
  same reason Cave of Trials' Level 10 light-grid puzzle didn't show clean structure either) --
  not claimed anywhere, rooms in that depth range just get the generic zone label. Because of
  the transporters, **"about N doors in from the lobby" is a rough position, not a strict floor
  number** -- said explicitly in every such row's `notes`. Map 647 is a 19-chest bonus vault
  near the lobby, same pattern as Cave of Trials' 710. 4 chest rooms from `chests[]`.

**Methodology notes for whoever extends the dungeon work (fields, or re-verifying any of the
above):**
- A dungeon's own `map_areas` label can be wrong at the whole-area level, not just one map
  (Cave of Trials' `Level 1` suffix on every row) -- if an area name looks like it's describing
  one sub-part of a bigger dungeon, check whether it should cover the whole cluster instead.
- Undirected BFS depth from the entrance is unreliable if the dungeon has one-way "escape
  portal" shortcuts back toward the entrance/hub (Cave of Trials had two: 756->704, 807->704,
  found by comparing each candidate hub's inbound vs outbound door lists -- a real escape
  portal has an edge toward the hub but no return edge). Strip those edges before computing
  depth, or BFS gives nonsense (rooms deep in the dungeon reading as 1 step from the entrance).
- When a dungeon has more boss-tagged `field.json` map ids than the guide's boss count, the
  extras are usually either (a) a second physical copy of the same fight for a flee/rematch
  story beat (Cave of Trials' two Weird Beast entries), or (b) the same fight registered under
  two different story-flag conditions reusing one room id. Cross-check against a thumbnail
  before assuming either.
- Nearest-anchor BFS (multi-source, owner = closest known landmark) is a reasonable way to
  split a dungeon into zones when there's no clean id-range or banner boundary (used for
  Mountain Palace's two quest paths and Fienal's floor zones); it's inherently fuzzy at
  boundaries, so say so in `notes` rather than presenting a hard cutoff as fact.
- Rendering a frame (`render.sh` + `resize.py` + `Read`) sometimes surfaces a HUD title overlay
  (location name + "Area Level N" banner) that is **not** the same data as the static `ext_b9`
  banner field `classify.py` reads -- it only shows up live, mid-animation. Worth one thumbnail
  per suspected level/floor boundary even in an otherwise doors-only dungeon.
- `scratchpad/mapatlas/thumbs/{raw,small}/` are shared with the other naming agent working in
  parallel -- files can disappear between your render and your `Read` if the other agent's
  `resize.py` run interleaves with yours. If a `Read` 404s right after a successful render,
  that's almost certainly why; just re-render rather than assuming something's broken.

## Towns (mapnames-towns agent, 2026-10-23)

**Done: every placeholder in Herlie, Linga, Port Hilton, Central City, North City, Armlock,
Mars Village, Fun City, Port of Clik, L'Aqua, Eluria, Cross Cave, Giveaway, the four Fields
(Power/Courage/Intelligence/Love), plus Zand's Mansion and Heraldry Weapons Laboratory (pulled
in by area-fixes below), plus map 231. 280 rows written, 0 remaining in these areas.** Worked in
parallel with the `mapnames-dungeons` agent on the same CSV; never touched Cave of Trials,
Hoffmann Ruins, Mountain Palace or Fienal. Partial audit file:
`scratchpad/mapatlas/names_towns.csv` (own running copy of every row this agent wrote, same
columns, merged into the shared CSV after each area via `flock scratchpad/mapnames.lock
python3 scratchpad/mapatlas/scripts/update_csv.py <batch>.json`). Batch JSONs for each area are
in `scratchpad/mapatlas/batches/` (git-ignored, one file per area, reusable if any row needs
revisiting). Helper script added: `scratchpad/mapatlas/scripts/towns_doors_dir.py MAP [MAP...]`
-- like `all_doors.py` but also returns each door's arrival-facing `dir` (so2_mod.transfers()'s
6th arg), useful for north/south/twin-room calls; read-only, opens the disc itself, doesn't
touch `doors.json`.

**`map_areas` mislabels found and fixed** (same "nearest-named-map BFS crossed into an
unrelated cluster" bug as the pre-existing map 427 case -- always check when a "todo" map's
doors lead somewhere thematically unrelated to its assigned area):
- **Map 143**: tagged `Town of Herlie`, actually Zand's Mansion's front gate (doors go straight
  to map 159, banner "Zand's Mansion"; thumbnail is a manor facade, not a Herlie street; guide
  p46 confirms "the mansion at the top of the hill, at the north end of town, is home to the
  ruthless Zand"). Pulled the whole 9-map Zand's Mansion cluster (158-166) into scope since it
  was otherwise completely untouched.
- **Maps 426/434/467-477** (13 maps): tagged `Port Hilton`, actually Lacour Castle's Tournament
  of Arms coliseum (doors only reach the existing map-427 Lacour Castle corridor, never Port
  Hilton's own cluster; guide p60 walks through registering at "the desk before the entrance to
  the coliseum area" and storing equipment). Overridden to `area=Lacour Castle`, matching 427's
  own precedent rather than inventing a new sub-area.
- **Maps 593-595**: tagged `North City`, actually the Field of Love's switch-puzzle platforms
  and Temple of Love gate (thumbnails show star-marked platforms "suspended in air above sea"
  per guide p112). Map 596 (the sci-fi transporter dais reached from both North City and
  Armlock) was kept under North City as the civic terminal rather than moved, since it's shared
  infrastructure, not part of the Field itself.
- **Maps 398/399/401**: tagged `Town of Armlock`, actually inside Heraldry Weapons Laboratory
  (401's thumbnail is a forest hollow-tree entrance that leads straight down to the Lab's own
  map 400; 398/399 are industrial machine-hall interiors, nothing like Armlock's plaza). Pulled
  the whole 13-map Heraldry Weapons Laboratory cluster into scope.
- **Maps 617-619**: tagged `L'Aqua`, actually Energy Nede's very first arrival scene -- a
  flower-lined path to a stone gazebo/Transporter on a cliff, matching the guide's "Outer Wall:
  Paradise and Beyond" chapter opening almost word for word. No existing area fit, so a new one
  was created: `area=The Outer Wall`.

**Notable finds, not area-fixes, just worth knowing:**
- **Central City maps 690-696**: not a room at all -- a 7-screen "Ten Wise Men" historical
  flashback cutscene (narration quoted in `notes`, paraphrased, not the guide), reached from the
  Mayor's Office (247). `kind=special`.
- **Fun City map 688/689**: the "Cooking Master" minigame hall (guide p108, numbered attraction
  9) -- confirmed by literal "COOKING MASTER" wall signage in the thumbnail.
- **Cross Cave maps 538/540/541**: three glowing signpost markers, almost certainly the "Ancient
  Writings" Celine goes looking for (guide p36, "no member of the party is able to read the
  Ancient Writings"). Map 547 is a dead ringer for the guide's "star-shaped dais, five treasure
  chests" finale.
- **Port Hilton / Inn Hilton Island (168/170/172/176/178/180/182)**: all seven render at night
  and mirror 175/177/179/181's inn layout almost exactly -- flagged as a probable story-state
  night duplicate (the guide's Tournament-Eve inn scene, p59-60) rather than a second physical
  building, but not provable from the door graph alone.
- **Map 231**: no doors/chests/banner, so `classify.py`/`placeholder_pass.py` left it fully
  unidentified -- but the thumbnail is a beach with a wrecked boat and two characters, matching
  the guide's "Shipwrecked!" scene that opens Eluria Colony almost exactly. Resolved to
  `area=Eluria Colony`, `kind=special` (cutscene-only, no doors).
- **Maps 643/697** (2 of the 5 originally-unresolved maps): thumbnailed, both striking (a
  futuristic penthouse lounge; an industrial glass-and-pipe walkway) but **not resolved** --
  no doors, no textual guide match found, and nothing to anchor an area guess to. Left as
  `area=""`/`kind=special`/`place="(unidentified)"`, unchanged.
- **Map 687** (3rd of the 5): its only door goes to map 657, which is Fienal's lobby (the
  `mapnames-dungeons` agent's territory) -- **deliberately left untouched** rather than guessed
  at, since it's almost certainly that agent's to resolve, not mine.
- **Map 625** (4th of the 5): already resolved before this agent started (disc-2 swap script
  trigger, not a visitable place) -- confirmed correct, not touched.

**Field of Power/Courage/Intelligence/Love**: all four match the guide's "Four Fields" framing
(training-ground puzzle dungeons unlocked via Narl's Rune Codes after Energy Nede) closely
enough to name room-by-room rather than fall back to generic depth placeholders: Intelligence's
twin "mirror rooms" + spinning-switch main floors + boss-transporter chamber (guide p52), Power's
Yeti-guarded switch caverns + summit bridge + Guardian temple (p40), Courage's "always advance
forward" one-way maze + Warrior Statue + teleporter (p42), Love's flying-mount arrival + rotating
star-platform bridges + Temple of Love gate (p46/p112). All four also have a sci-fi "transit
corridor"/"transporter hub" cluster bridging to Eluria Tower and/or map 625 -- kept `kind=special`
and under their own Field's area rather than merged, since nothing better-named exists nearby.

**If anyone revisits this work**: the five night-duplicate/twin-room calls (Herlie 153/154/155,
Port Hilton 177/179/181, Port of Clik 105/106, the Inn Hilton Island night cluster, Fun City's
two Fighting Arena arena-floor entries) are all inferred from identical-looking thumbnails plus
symmetric door topology, not from any story-flag introspection -- so2_mod.py doesn't currently
expose per-map flag gating, which would be the real way to confirm these.

## Rules this task was given (for any follow-up)

- Only `map_names.csv` and this log go in git; thumbnails/guide text/scripts stay in
  `scratchpad/mapatlas/` (git-ignored).
- Headless only (`SDL_VIDEO_DRIVER=offscreen`, `SDL_AUDIO_DRIVER=dummy`); never pop windows;
  scratch `XDG_CONFIG_HOME=scratchpad/mapatlas/config`, `--no-memcard`; never touch
  `~/.config/shattered-stars` or `~/.config/pcsx-redux`.
- Kill by exact PID only (none left running from this session -- `render.sh` always `wait`s).
  At most 6 game processes at once (enforced in `render.sh`).
- `SS_DISC1` = the Disc 1 folder + its `.cue` filename (see `render.sh`/`doors.py` for the
  exact path; same for `tools/so2_mod.py`'s DISC argument).
- `tools/mod_reference.py` and `tools/so2_mod.py` are another agent's -- only read
  `so2_mod.py`'s output (via `all_doors.py`/`so2_mod` import), never edited them.
- No commits/pushes from here; the coordinator commits hourly (`scratchpad/checkpoint_notes.txt`
  has one `mapnames: ...` line per ~100 maps done, per the task's instructions).
- Build dir `engine/build-mapatlas` (own, `-j8`), not the shared `engine/build`.
