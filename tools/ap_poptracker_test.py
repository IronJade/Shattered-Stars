#!/usr/bin/env python3
"""Tests for the PopTracker pack (docs/ap/poptracker.md "Testing").

    SKIP_REQUIREMENTS_UPDATE=1 prlimit --as=$((8<<30)) scratchpad/ap/ap-venv/bin/python \\
        tools/ap_poptracker_test.py [-v] [--pack DIR] [--ap-dir DIR]

Builds the pack with tools/ap_poptracker.py into scratchpad/packgen/test_pack (unless --pack names a
built pack folder), then checks it against the world and the layout:
- ids: every AP item / location id the pack maps is in the data package of the registered world
  (worlds.so2, the repo's apworld/so2 linked into --ap-dir); every location of the data package has
  exactly one section; each section's rules are the logic seam's and name its own id;
- progression: in generated rooms (story, open world and hub, each with every option that makes
  items progression) every item created as progression, and every item of the groups Keys,
  Dungeon Keys, Characters, Skills and Moves, is mapped; every location of those rooms has a section;
- layout (docs/ap/tracker_maps.json, "For the pack"): every place check is a section of a map
  location on its area image at the layout's x/y; checks sharing a dot share a location; every
  layout image is at the layout's path with the layout's size, none in a variant folder; every map
  location is inside its map's image;
- JSON: every file parses; the manifest (uid, version, flags "ap" only, PopTracker 0.36.0); every
  item code a layout or rule names exists; every map a layout or location names exists; every section
  ref resolves; every tab title in map_tabs.lua is a tab of the map layout, in order;
- Lua: the pack's scripts run in Lua 5.4 (lupa, the Lua PopTracker bundles) against a stub of
  PopTracker's API: load both variants, connect (slot data, missing and checked locations), receive
  items, check a location, hints, the port's data-storage keys (auto tab switching, the chapter);
- the UT name mapping: unique keys, ids in the data package, names that differ from the AP names.

Needs lupa in the AP venv (`ap-venv/bin/python -m pip install lupa`). The pack checker
(PopTracker/pack-checker, strict) runs from tools/build_poptracker.sh check.
"""
import argparse
import csv
import glob
import json
import os
import re
import struct
import subprocess
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
GAME = "Star Ocean: The Second Story"
UID = "so2_shattered_stars"
ARGS = None
PACK = None         # the pack folder
WORLD = None        # the registered world class
with open(os.path.join(REPO, "docs", "ap", "tracker_maps.json"), encoding="utf-8") as _f:
    LAYOUT = json.load(_f)

# Rooms whose items cover every way an item becomes progression (keys, dungeon keys, characters
# through killer_move_progression any and learn points, skills and moves through skillsanity).
ROOMS = {
    "story": {"world_mode": "story", "character_items": True, "craftsanity": True, "killer_move_checks": True,
              "killer_move_progression": "any", "skillsanity": "insanity"},
    "open": {"world_mode": "open_world", "character_items": True, "shuffle_shops": "full",
             "enemy_dropsanity": "first_drop", "craftsanity": True, "killer_move_checks": True,
             "killer_move_progression": "any", "skillsanity": "insanity", "cave_of_trials": True,
             "goal": "indalecio_limiter_off_and_iseria_queen"},
    "hub": {"world_mode": "hub", "fienal_requirement": "all_bosses", "craftsanity": True,
            "killer_move_checks": True, "killer_move_progression": "any", "skillsanity": "insanity",
            "enemy_dropsanity": "first_drop", "shuffle_shops": "full"},
}


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


def pack_json(path):
    return json.loads(read(os.path.join(PACK, path)))


def png_size(path):
    with open(path, "rb") as f:
        head = f.read(24)
    if head[:8] != b"\x89PNG\r\n\x1a\n" or head[12:16] != b"IHDR":
        return None
    return struct.unpack(">II", head[16:24])


def lua_table(path, name):
    """A generated `NAME = { [k] = {...}, ... }` table as {int: [strings]}."""
    text = read(os.path.join(PACK, path))
    body = text[text.index(name + " = {"):]
    out = {}
    for m in re.finditer(r"^\s*\[(\d+)\] = \{(.*)\},$", body, re.M):
        out[int(m.group(1))] = re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(2))
    return out


def walk_locations(nodes, parent=""):
    """(location id, location dict) for every location in a locations file."""
    for node in nodes:
        lid = f"{parent}/{node['name']}" if parent else node["name"]
        yield lid, node
        yield from walk_locations(node.get("children", []), lid)


def all_locations():
    out = {}
    for f in ("places", "boards", "world"):
        for lid, node in walk_locations(pack_json(f"locations/{f}.json")):
            out[lid] = node
    return out


def sections():
    """'location id/section' -> (location dict, section dict) for every named section."""
    out = {}
    for lid, node in all_locations().items():
        for sec in node.get("sections", []):
            if "name" in sec:
                out[f"{lid}/{sec['name']}"] = (node, sec)
    return out


def layout_nodes(node):
    if isinstance(node, dict):
        yield node
        for v in node.values():
            yield from layout_nodes(v)
    elif isinstance(node, list):
        for v in node:
            yield from layout_nodes(v)


def item_codes():
    codes = set()
    for it in pack_json("items/items.json"):
        if "codes" in it:
            codes.update(c.strip() for c in it["codes"].split(","))
        for st in it.get("stages", []):
            codes.update(c.strip() for c in st["codes"].split(","))
    return codes


class TestIds(unittest.TestCase):
    def test_item_ids_in_data_package(self):
        mapping = lua_table("scripts/autotracking/item_mapping.lua", "ITEM_MAPPING")
        ids = set(WORLD.item_name_to_id.values())
        self.assertTrue(mapping)
        self.assertEqual([i for i in mapping if i not in ids], [])
        codes = item_codes()
        self.assertEqual([c for v in mapping.values() for c in v[::2] if c not in codes], [])

    def test_every_location_has_one_section(self):
        mapping = lua_table("scripts/autotracking/location_mapping.lua", "LOCATION_MAPPING")
        ids = set(WORLD.location_name_to_id.values())
        self.assertEqual(set(mapping), ids, "the pack maps exactly the data package's locations")
        secs = sections()
        paths = [v[0] for v in mapping.values()]
        self.assertEqual(len(paths), len(set(paths)), "one section per location")
        for lid, (path,) in mapping.items():
            self.assertTrue(path.startswith("@"))
            node, sec = secs[path[1:]]
            self.assertEqual(sec["access_rules"], [f"^$so2_access|{lid}"])
            self.assertEqual(sec["visibility_rules"], [f"$so2_exists|{lid}"])
        named = {k for k in secs if not k.startswith(("Expel World Map/", "Energy Nede World Map/"))}
        self.assertEqual(named, {p[0][1:] for p in mapping.values()}, "no section without a location")

    def test_ut_name_mapping(self):
        mapping = json.loads(read(os.path.join(os.path.dirname(PACK), "ut_name_mapping.json")))
        names = {v: k for k, v in WORLD.location_name_to_id.items()}
        loc_map = lua_table("scripts/autotracking/location_mapping.lua", "LOCATION_MAPPING")
        covered = 0
        for key, lid in mapping.items():
            loc, sec = key.rsplit("/", 1)
            self.assertIn(lid, names)
            self.assertNotEqual(sec, names[lid])
            self.assertTrue(loc_map[lid][0].endswith(f"/{key}"), key)
            covered += 1
        # every section either is its AP name or is in the mapping
        for lid, (path,) in loc_map.items():
            sec = path.rsplit("/", 1)[1]
            self.assertTrue(sec == names[lid] or lid in mapping.values(), names[lid])
        self.assertGreater(covered, 0)


class TestProgression(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        from test.general import gen_steps, setup_multiworld
        cls.rooms = {}
        for name, options in ROOMS.items():
            mw = setup_multiworld(WORLD, gen_steps, seed=7, options=options)
            cls.rooms[name] = mw

    def test_progression_items_are_mapped(self):
        mapping = lua_table("scripts/autotracking/item_mapping.lua", "ITEM_MAPPING")
        for name, mw in self.rooms.items():
            with self.subTest(room=name):
                items = list(mw.itempool) + list(mw.precollected_items[1]) + \
                    [loc.item for loc in mw.get_locations(1) if loc.item is not None]
                prog = {it.name for it in items if it.advancement and it.code is not None}
                self.assertGreater(len(prog), 20, f"{name}: {len(prog)} progression items")
                self.assertEqual(sorted(n for n in prog if WORLD.item_name_to_id[n] not in mapping), [])

    def test_groups_are_mapped(self):
        mapping = lua_table("scripts/autotracking/item_mapping.lua", "ITEM_MAPPING")
        for group in ("Keys", "Dungeon Keys", "Characters", "Skills", "Moves"):
            names = WORLD.item_name_groups[group]
            self.assertTrue(names)
            self.assertEqual(sorted(n for n in names if WORLD.item_name_to_id[n] not in mapping), [], group)

    def test_room_locations_have_sections(self):
        mapping = lua_table("scripts/autotracking/location_mapping.lua", "LOCATION_MAPPING")
        for name, mw in self.rooms.items():
            with self.subTest(room=name):
                ids = {loc.address for loc in mw.get_locations(1) if loc.address is not None}
                self.assertGreater(len(ids), 300)
                self.assertEqual(sorted(ids - set(mapping)), [])


class TestLayout(unittest.TestCase):
    def test_place_checks_on_their_dots(self):
        secs = sections()
        loc_map = lua_table("scripts/autotracking/location_mapping.lua", "LOCATION_MAPPING")
        maps = {m["name"]: m for m in pack_json("maps/maps.json")}
        by_dot = {}
        for name, check in LAYOUT["checks"].items():
            lid = WORLD.location_name_to_id[name]
            node, _ = secs[loc_map[lid][0][1:]]
            (ml,) = node["map_locations"]
            self.assertEqual(maps[ml["map"]]["img"], check["image"], name)
            self.assertEqual((ml["x"], ml["y"]), (check["x"], check["y"]), name)
            by_dot.setdefault((check["image"], check["x"], check["y"]), set()).add(id(node))
        self.assertEqual([k for k, v in by_dot.items() if len(v) != 1], [], "a dot is one location")
        self.assertEqual(len(LAYOUT["checks"]), 472)

    def test_images(self):
        for img in LAYOUT["images"]:
            path = os.path.join(PACK, img["path"])
            self.assertEqual(png_size(path), (img["width"], img["height"]), img["path"])
        manifest = pack_json("manifest.json")
        for variant in manifest["variants"]:
            self.assertEqual(glob.glob(os.path.join(PACK, variant, "images", "**"), recursive=True), [],
                             "no variant copy of an image: it would hide the player's render")

    def test_referenced_files_exist(self):
        refs = []
        for it in pack_json("items/items.json"):
            refs += [it["img"]] if "img" in it else []
            refs += [st["img"] for st in it.get("stages", [])]
        refs += [m["img"] for m in pack_json("maps/maps.json")]
        missing = [r for r in refs if png_size(os.path.join(PACK, r)) is None]
        self.assertEqual(missing, [])

    def test_map_locations_inside_images(self):
        maps = {m["name"]: m for m in pack_json("maps/maps.json")}
        for lid, node in all_locations().items():
            for ml in node.get("map_locations", []):
                w, h = png_size(os.path.join(PACK, maps[ml["map"]]["img"]))
                self.assertTrue(0 <= ml["x"] < w and 0 <= ml["y"] < h, lid)


class TestJson(unittest.TestCase):
    def test_parses(self):
        files = glob.glob(os.path.join(PACK, "**", "*.json"), recursive=True)
        self.assertGreater(len(files), 8)
        for f in files:
            json.loads(read(f))

    def test_manifest(self):
        m = pack_json("manifest.json")
        self.assertEqual(m["package_uid"], UID)
        self.assertEqual(m["package_uid"], LAYOUT["pack_uid"])
        self.assertEqual(m["package_version"], "1.0.0")
        self.assertEqual(m["game_name"], GAME)
        self.assertEqual(m["min_poptracker_version"], "0.36.0")
        self.assertEqual(m["target_poptracker_version"], "0.36.0")
        self.assertTrue(m["versions_url"].startswith("https://"))
        self.assertEqual(set(m["variants"]), {"map", "items_only"})
        for v in m["variants"].values():
            self.assertEqual(v["flags"], ["ap"], "never apmanual / aphintgame: the pack sends no checks")

    def test_layout_codes_and_maps(self):
        codes = item_codes()
        maps = {m["name"] for m in pack_json("maps/maps.json")}
        layouts = {}
        for f in ["layouts/items.json", "layouts/tracker.json", "items_only/layouts/tracker.json"]:
            data = pack_json(f)
            layouts.update(data)
            for node in layout_nodes(data):
                if node.get("type") == "item":
                    self.assertIn(node["item"], codes)
                if node.get("type") == "itemgrid":
                    for row in node["rows"]:
                        for c in row:
                            self.assertIn(c, codes)
                if node.get("type") == "map":
                    for name in node["maps"]:
                        self.assertIn(name, maps)
        for node in layout_nodes(layouts):
            if node.get("type") == "layout":
                self.assertIn(node["key"], layouts)
        self.assertIn("tracker_default", pack_json("layouts/tracker.json"))
        self.assertIn("tracker_default", pack_json("items_only/layouts/tracker.json"))
        self.assertIn("tracker_broadcast", layouts)
        for lid, node in all_locations().items():
            for ml in node.get("map_locations", []):
                self.assertIn(ml["map"], maps, lid)

    def test_refs_resolve(self):
        secs = sections()
        n = 0
        for lid, node in all_locations().items():
            for sec in node.get("sections", []):
                if "ref" in sec:
                    self.assertIn(sec["ref"], secs, lid)
                    n += 1
        self.assertEqual(n, 472, "the world maps' dots refer to every place check once")

    def test_map_tabs(self):
        tabs = lua_table("scripts/map_tabs.lua", "SO2_MAP_TABS")
        tree = pack_json("layouts/tracker.json")["tracker_default"]

        def paths(node, prefix):
            for n in layout_nodes(node):
                if n.get("type") == "tabbed":
                    for t in n["tabs"]:
                        yield prefix + [t["title"]], t["content"]
                    return

        def reachable(node, prefix=()):
            out = set()
            for path, content in paths(node, list(prefix)):
                out.add(tuple(path))
                out |= reachable(content, path)
            return out
        valid = reachable(tree)
        names = {int(r["map"]) for r in csv.DictReader(read(os.path.join(
            REPO, "docs", "modding", "reference", "map_names.csv")).splitlines())}
        for m, path in tabs.items():
            self.assertIn(m, names)
            self.assertIn(tuple(path), valid, f"map {m}: {path}")
        drawn = {m["map"] for img in LAYOUT["images"] if img["kind"] == "area" for m in img["maps"]}
        self.assertEqual(sorted(drawn - set(tabs)), [], "every map with a place check switches a tab")
        self.assertEqual(tabs[1], ["Expel", "Expel World Map"])
        self.assertEqual(tabs[2], ["Energy Nede", "Energy Nede World Map"])


LUA_STUB = r"""
local py = ...
AccessibilityLevel = {None = 0, Partial = 1, Inspect = 3, SequenceBreak = 5, Normal = 6, Cleared = 7}
Highlight = {Avoid = -1, None = 0, NoPriority = 1, Unspecified = 2, Priority = 3}
PopVersion = "0.36.0"
STUB = {items = {}, by_code = {}, sections = {}, watches = {}, hints = {}, handlers = {}, notify = {}, get = {},
        log = {}}

local function changed(obj)
    for _, cb in pairs(STUB.watches) do
        for code in pairs(obj._codes) do
            cb(code)
            break
        end
    end
end

local function make_item(spec)
    local obj = {_codes = {}, _type = spec.type, Type = spec.type, Increment = 1}
    local state = {Active = false, CurrentStage = spec.initial_stage_idx or 0, AcquiredCount = 0, BadgeText = ""}
    if spec.initial_active_state then state.Active = true end
    setmetatable(obj, {__index = state, __newindex = function(t, k, v)
        local old = state[k]
        state[k] = v
        if old ~= v then changed(t) end
    end})
    return obj
end

Tracker = {ActiveVariantUID = py.variant, BulkUpdate = false}
function Tracker:AddItems(path)
    for _, spec in ipairs(py.json(path)) do
        local obj = make_item(spec)
        local codes = {}
        if spec.codes then table.insert(codes, spec.codes) end
        for _, st in ipairs(spec.stages or {}) do table.insert(codes, st.codes) end
        for _, c in ipairs(codes) do
            obj._codes[c] = true
            STUB.by_code[c] = STUB.by_code[c] or obj
        end
        table.insert(STUB.items, obj)
    end
    return true
end
function Tracker:AddMaps(path) py.json(path) return true end
function Tracker:AddLayouts(path) py.json(path) return true end
local function add_locations(nodes, parent)
    for _, node in ipairs(nodes) do
        local id = parent and (parent .. "/" .. node.name) or node.name
        for _, sec in ipairs(node.sections or {}) do
            if sec.name and not sec.ref then
                STUB.sections[id .. "/" .. sec.name] = {ChestCount = 1, AvailableChestCount = 1, Highlight = 0,
                    access = sec.access_rules[1], visible = sec.visibility_rules[1]}
            end
        end
        add_locations(node.children or {}, id)
    end
end
function Tracker:AddLocations(path) add_locations(py.json(path), nil) return true end
function Tracker:FindObjectForCode(code)
    if code:sub(1, 1) == "@" then return STUB.sections[code:sub(2)] end
    return STUB.by_code[code]
end
function Tracker:ProviderCountForCode(code)
    local obj = STUB.by_code[code]
    if not obj then return 0 end
    if obj._type == "consumable" then return obj.AcquiredCount end
    if obj._type == "toggle" then return obj.Active and 1 or 0 end
    return 1
end
function Tracker:UiHint(name, value) table.insert(STUB.hints, {name, value}) end

ScriptHost = {}
function ScriptHost:LoadScript(path) local f = assert(load(py.read(path), "@" .. path)) f() return true end
function ScriptHost:AddWatchForCode(name, code, cb) STUB.watches[name] = cb return name end

Archipelago = {PlayerNumber = -1, TeamNumber = -1, CheckedLocations = {}, MissingLocations = {}}
for _, kind in ipairs({"Clear", "Item", "Location", "Retrieved", "SetReply", "Scout", "Bounced"}) do
    Archipelago["Add" .. kind .. "Handler"] = function(self, name, cb) STUB.handlers[kind] = cb end
end
function Archipelago:SetNotify(keys) STUB.notify = keys end
function Archipelago:Get(keys) STUB.get = keys end

-- evaluate a section's rules as PopTracker would ("^$f|a" / "$f|a")
function STUB.rule(rule)
    local caret = rule:sub(1, 1) == "^"
    if caret then rule = rule:sub(2) end
    local f, arg = rule:match("^%$([%w_]+)|(.*)$")
    return _G[f](arg)
end
"""


class TestLua(unittest.TestCase):
    def runtime(self, variant):
        from lupa import lua54
        lua = lua54.LuaRuntime(unpack_returned_tuples=True)

        def pack_file(path):
            return read(os.path.join(PACK, *path.split("/")))

        def load_json(path):
            vpath = os.path.join(PACK, variant, *path.split("/"))
            return to_lua(lua, json.loads(read(vpath if os.path.exists(vpath) else os.path.join(PACK, *path.split("/")))))
        api = lua.table_from({"variant": variant, "read": pack_file, "json": load_json})
        lua.execute(LUA_STUB, api)
        lua.execute(pack_file("scripts/init.lua"))
        return lua

    def connect(self, lua, slot_data, missing, checked):
        g = lua.globals()
        g.Archipelago.PlayerNumber = 1
        g.Archipelago.TeamNumber = 0
        g.Archipelago.MissingLocations = lua.table_from(missing)
        g.Archipelago.CheckedLocations = lua.table_from(checked)
        g.STUB.handlers.Clear(to_lua(lua, slot_data))

    def test_items_only_variant_loads(self):
        lua = self.runtime("items_only")
        g = lua.globals()
        self.assertEqual(len(list(g.STUB.sections)), 0)
        self.connect(lua, {"world_mode": "story", "characters": ["Claude", "Rena"]}, [1347569664], [])
        g.STUB.handlers.Item(0, WORLD.item_name_to_id["Arlia Pass"], "Arlia Pass", 1)
        self.assertTrue(g.Tracker.FindObjectForCode(g.Tracker, "key_arlia_pass").Active)

    def test_map_variant(self):
        lua = self.runtime("map")
        g, T = lua.globals(), lua.globals().Tracker
        ids = WORLD.location_name_to_id
        chest, gift = ids["Arlia Village: Leather Armor chest"], ids["Arlia Village: Seventh Ray gift"]
        craft = ids["Art: 'Spring'"]
        loc_map = lua_table("scripts/autotracking/location_mapping.lua", "LOCATION_MAPPING")

        def sec(lid):
            return T.FindObjectForCode(T, loc_map[lid][0])

        def rule(lid, which):
            return g.STUB.rule(getattr(sec(lid), which))
        # before connecting: everything shows, as a checklist
        self.assertTrue(rule(craft, "visible"))
        self.assertEqual(rule(craft, "access"), 6)
        self.assertEqual(T.FindObjectForCode(T, "so2_logic_offline").CurrentStage, 0)
        rev = T.FindObjectForCode(T, "so2_rev").AcquiredCount
        slot = {"world_mode": "hub", "goal": "indalecio_limiter_off", "craftsanity": True, "skillsanity": "insanity",
                "characters": ["Claude", "Rena", "Celine", "Bowman", "Dias", "Precis", "Ashton", "Opera"],
                "dungeon_keys": ["Salva Drift Key", "Cross Cave Key"], "bench": True, "logic_version": 1}
        self.connect(lua, slot, [chest, gift], [craft])
        # only the room's locations show; the checked one is cleared
        self.assertTrue(rule(chest, "visible"))
        self.assertTrue(rule(craft, "visible"))
        self.assertFalse(rule(ids["Arlia Village: shop 1 Sandals"], "visible"), "not in this room")
        self.assertEqual(sec(craft).AvailableChestCount, 0)
        self.assertEqual(sec(chest).AvailableChestCount, 1)
        self.assertEqual(rule(chest, "access"), 6)
        self.assertGreater(T.FindObjectForCode(T, "so2_rev").AcquiredCount, rev, "the rules are re-run")
        # the banner: connected, no evaluator yet (B4a): checklist
        self.assertEqual(T.FindObjectForCode(T, "so2_logic_offline").CurrentStage, 1)
        # the settings from the slot data
        self.assertEqual(T.FindObjectForCode(T, "opt_world_mode_story").CurrentStage, 2)
        self.assertEqual(T.FindObjectForCode(T, "opt_goal_indalecio").CurrentStage, 1)
        self.assertEqual(T.FindObjectForCode(T, "opt_skillsanity_none").CurrentStage, 3)
        self.assertTrue(T.FindObjectForCode(T, "opt_craftsanity").Active)
        self.assertTrue(T.FindObjectForCode(T, "opt_bench").Active)
        self.assertFalse(T.FindObjectForCode(T, "opt_hell_seed").Active)
        self.assertEqual(T.FindObjectForCode(T, "char_leon").BadgeText, "out")
        self.assertEqual(T.FindObjectForCode(T, "char_celine").BadgeText, "")
        self.assertEqual(T.FindObjectForCode(T, "dkey_salva_drift").BadgeText, "")
        self.assertEqual(T.FindObjectForCode(T, "dkey_mars_dungeon").BadgeText, "out")
        # data storage: hints, map and story keys of team 0, slot 1
        self.assertEqual(sorted(g.STUB.get.values()), ["_read_hints_0_1", "so2_map_0_1", "so2_story_0_1"])
        self.assertEqual(sorted(g.STUB.notify.values()), sorted(g.STUB.get.values()))
        # items: toggles, consumables (traps), an unmapped item counted for the logic
        key = WORLD.item_name_to_id["Salva Drift Key"]
        trap = WORLD.item_name_to_id["Poison Trap"]
        fol = WORLD.item_name_to_id["200 Fol"] if "200 Fol" in WORLD.item_name_to_id else None
        g.STUB.handlers.Item(0, key, "Salva Drift Key", 1)
        g.STUB.handlers.Item(1, trap, "Poison Trap", 2)
        g.STUB.handlers.Item(2, trap, "Poison Trap", 2)
        g.STUB.handlers.Item(2, trap, "Poison Trap", 2)     # a repeated index is ignored
        self.assertTrue(T.FindObjectForCode(T, "dkey_salva_drift").Active)
        self.assertEqual(T.FindObjectForCode(T, "trap_poison").AcquiredCount, 2)
        self.assertEqual(g.so2_item_count(key), 1)
        self.assertEqual(g.so2_item_count(trap), 2)
        if fol is not None:
            g.STUB.handlers.Item(3, fol, "200 Fol", 1)
            self.assertEqual(g.so2_item_count(fol), 1)
        # a check
        g.STUB.handlers.Location(chest, "Arlia Village: Leather Armor chest")
        self.assertEqual(sec(chest).AvailableChestCount, 0)
        # hints in this world highlight the section
        hints = [{"finding_player": 1, "location": gift, "status": 30, "found": False},
                 {"finding_player": 2, "location": chest, "status": 30, "found": False}]
        g.STUB.handlers.Retrieved("_read_hints_0_1", to_lua(lua, hints))
        self.assertEqual(sec(gift).Highlight, 3)
        self.assertEqual(sec(chest).Highlight, 0)
        # the port's map key switches tabs (auto tab on by default); position-only updates don't
        tabs = lua_table("scripts/map_tabs.lua", "SO2_MAP_TABS")
        g.STUB.handlers.SetReply("so2_map_0_1", to_lua(lua, {"map": 26}), None)
        self.assertEqual([tuple(h.values()) for h in g.STUB.hints.values()],
                         [("ActivateTab", t) for t in tabs[26]])
        g.STUB.handlers.SetReply("so2_map_0_1", to_lua(lua, {"map": 26}), None)
        g.STUB.handlers.SetReply("so2_map_0_1", to_lua(lua, {"map": 1, "x": 51200, "z": 18500}), None)
        g.STUB.handlers.SetReply("so2_map_0_1", to_lua(lua, {"map": 1, "x": 51300, "z": 18500}), None)
        self.assertEqual(len(g.STUB.hints), len(tabs[26]) + len(tabs[1]))
        T.FindObjectForCode(T, "so2_auto_tab").Active = False
        g.STUB.handlers.SetReply("so2_map_0_1", to_lua(lua, {"map": 18}), None)
        self.assertEqual(len(g.STUB.hints), len(tabs[26]) + len(tabs[1]))
        g.STUB.handlers.SetReply("so2_map_0_1", None, None)        # null before the port wrote it
        # the story key sets the chapter
        g.STUB.handlers.SetReply("so2_story_0_1", to_lua(lua, {"chapter": 12, "step": 300}), None)
        self.assertEqual(T.FindObjectForCode(T, "so2_chapter").AcquiredCount, 12)
        # a reconnect starts over: items reset, checks from the server again
        self.connect(lua, slot, [chest, gift, craft], [])
        self.assertFalse(T.FindObjectForCode(T, "dkey_salva_drift").Active)
        self.assertEqual(sec(chest).AvailableChestCount, 1)
        self.assertEqual(sec(gift).Highlight, 0)
        self.assertEqual(T.FindObjectForCode(T, "so2_chapter").AcquiredCount, 0)

    def test_every_section_rule_runs(self):
        lua = self.runtime("map")
        g = lua.globals()
        n = 0
        for _, s in g.STUB.sections.items():
            self.assertEqual(g.STUB.rule(s.access), 6)
            self.assertTrue(g.STUB.rule(s.visible))
            n += 1
        self.assertEqual(n, len(WORLD.location_name_to_id))


def to_lua(lua, value):
    if isinstance(value, dict):
        return lua.table_from({k: to_lua(lua, v) for k, v in value.items()})
    if isinstance(value, list):
        return lua.table_from([to_lua(lua, v) for v in value])
    return value


def build_pack(ap_dir, out):
    cmd = [sys.executable, os.path.join(HERE, "ap_poptracker.py"), "--ap-dir", ap_dir, "--out", out]
    subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL)


def main():
    global ARGS, PACK, WORLD
    p = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    p.add_argument("--pack", help="a built pack folder (default: build one)")
    p.add_argument("--ap-dir", default=os.path.join(REPO, "scratchpad", "ap", "Archipelago"))
    ARGS, rest = p.parse_known_args()
    ap_dir = os.path.abspath(ARGS.ap_dir)
    if ARGS.pack:
        PACK = os.path.abspath(ARGS.pack)
    else:
        PACK = os.path.join(REPO, "scratchpad", "packgen", "test_pack", UID)
        build_pack(ap_dir, PACK)
    sys.path.insert(0, ap_dir)
    linked = os.path.realpath(os.path.join(ap_dir, "worlds", "so2"))
    if linked != os.path.realpath(os.path.join(REPO, "apworld", "so2")):
        raise SystemExit(f"{ap_dir}/worlds/so2 is not this repo's apworld/so2 (link it as tools/ap_ut_check.py does)")
    import warnings
    warnings.simplefilter("ignore")
    from worlds.AutoWorld import AutoWorldRegister
    WORLD = AutoWorldRegister.world_types[GAME]
    unittest.main(argv=[sys.argv[0]] + rest)


if __name__ == "__main__":
    main()
