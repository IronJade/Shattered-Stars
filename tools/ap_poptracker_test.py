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
    for f in ("places", "boards", "world", "doors"):
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


def door_codes():
    """The door panel's Lua item codes (scripts/doors_data.lua; doors.lua creates the items)."""
    path = os.path.join(PACK, "scripts", "doors_data.lua")
    return set(re.findall(r'code = "(door_[^"]+)"', read(path))) if os.path.exists(path) else set()


def item_codes():
    codes = door_codes()
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
        named = {k for k in secs if not k.startswith(("Expel World Map/", "Energy Nede World Map/", "Doors/"))}
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
            ml, *on_rooms = node["map_locations"]
            self.assertEqual(maps[ml["map"]]["img"], check["image"], name)
            self.assertEqual((ml["x"], ml["y"]), (check["x"], check["y"]), name)
            room = check["room"]       # the same check on its room (format 1.1)
            self.assertIn({"map": f"room_{check['map']}", "x": room["x"], "y": room["y"]}, on_rooms, name)
            self.assertEqual(room["image"], f"images/rooms/{check['map']}.png")
            self.assertTrue(all(r["map"].startswith("room_") for r in on_rooms), name)
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


# Door destinations that are neither a room nor a world map: no tab to open (the panel names them).
UNDRAWN_DESTINATIONS = {182: "Inn Hilton Island at night: a story scene, not drawn (tracker_maps.md Room groups)"}
PACK_BUDGET = 20 << 20      # the pack zip's budget (docs/handoff/mapdoors.md)


def doorways():
    """doorway id -> (room map, letter, [(x, y)], to) from the layout."""
    out = {}
    for r in LAYOUT["rooms"]:
        for e in r["doors"]:
            d = out.setdefault(e["id"], (r["map"], e["letter"], [], e["to"]))
            d[2].append((e["x"], e["y"]))
    return out


class TestRooms(unittest.TestCase):
    """Format 1.1 in the pack: room maps, door locations, Area > Room tabs (tracker_maps.md "For the
    pack (part 2)")."""

    def test_room_maps_and_placeholders(self):
        from PIL import Image
        maps = {m["name"]: m for m in pack_json("maps/maps.json")}
        style = LAYOUT["style"]

        def rgb(h):
            return tuple(int(h[k:k + 2], 16) for k in (1, 3, 5))

        def near(a, b):
            return all(abs(x - y) <= 2 for x, y in zip(a, b))
        for r in LAYOUT["rooms"]:
            m = maps[f"room_{r['map']}"]
            self.assertEqual(m["img"], r["path"])
            self.assertEqual(m["location_size"], r["location_size"])
            path = os.path.join(PACK, r["path"])
            self.assertEqual(png_size(path), (r["width"], r["height"]), r["path"])
            img = Image.open(path).convert("RGB")
            fs = r["font_scale"]
            for e in r["doors"]:        # the letter boxes where the port draws them
                bx, by, bw, bh = e["box"]
                inner = [img.getpixel((x, y)) for x in range(bx + fs, bx + bw - fs) for y in range(by + fs, by + bh - fs)]
                common = max(set(inner), key=inner.count)
                self.assertTrue(near(common, rgb(style["door"]["fill"])), (r["map"], e["letter"], common))
                self.assertTrue(near(img.getpixel((bx, by)), rgb(style["door"]["outline"])), (r["map"], e["letter"]))
        self.assertEqual(len([n for n in maps if n.startswith("room_")]), len(LAYOUT["rooms"]))

    def test_door_locations(self):
        ways = doorways()
        doors = {lid: node for lid, node in walk_locations(pack_json("locations/doors.json")) if "sections" in node}
        self.assertEqual(len(doors), len(ways))
        ap_names = set(WORLD.location_name_to_id)
        mapped = {v[0][1:] for v in lua_table("scripts/autotracking/location_mapping.lua", "LOCATION_MAPPING").values()}
        for wid, (m, letter, marks, _) in ways.items():
            node = doors[f"Doors/{m} {letter}"]
            (sec,) = node["sections"]
            self.assertEqual(sec["name"], f"Door {letter}")
            self.assertEqual(sec["access_rules"], [f"^$so2_door_access|{wid}"])
            self.assertNotIn("visibility_rules", sec)
            self.assertEqual(sec.get("item_count", 1), 1)
            self.assertNotIn(sec["name"], ap_names)
            self.assertNotIn(f"Doors/{m} {letter}/Door {letter}", mapped, "a door is no AP location")
            self.assertEqual([(ml["x"], ml["y"]) for ml in node["map_locations"]], marks)
            self.assertTrue(all(ml["map"] == f"room_{m}" and ml["shape"] == "diamond" for ml in node["map_locations"]))
        text = read(os.path.join(PACK, "scripts", "doors_data.lua"))
        keys = dict((k, int(v)) for k, v in re.findall(r'^\s*\["(Doors/[^"]+)"\] = (\d+),$', text, re.M))
        self.assertEqual(keys, {f"Doors/{m} {l}/Door {l}": wid for wid, (m, l, _, _) in ways.items()})

    def test_door_destinations_have_tabs(self):
        tabs = lua_table("scripts/map_tabs.lua", "SO2_MAP_TABS")
        rooms = {r["map"] for r in LAYOUT["rooms"]}
        undrawn = set()
        for wid, (m, letter, _, to) in doorways().items():
            for t in to:
                if t["map"] in rooms:
                    self.assertEqual(tabs[t["map"]][-1], str(t["map"]), "a room destination opens its own tab")
                elif t["map"] in (1, 2):
                    self.assertEqual(tabs[t["map"]][-1], ("Expel" if t["map"] == 1 else "Energy Nede") + " World Map")
                else:
                    undrawn.add(t["map"])
        self.assertEqual(undrawn, set(UNDRAWN_DESTINATIONS), "every other destination is explained")

    def test_area_room_tabs(self):
        tree = pack_json("layouts/tracker.json")["tracker_default"]
        tabs = lua_table("scripts/map_tabs.lua", "SO2_MAP_TABS")
        ways = doorways()
        by_room = {}
        for wid, (m, letter, _, _) in sorted(ways.items(), key=lambda kv: (kv[1][0], len(kv[1][1]), kv[1][1])):
            by_room.setdefault(m, []).append(f"door_{m}_{letter.lower()}")

        def find(node, path):
            for title in path:
                node = next(n for n in layout_nodes(node) if n.get("type") == "tabbed")
                node = next(t["content"] for t in node["tabs"] if t["title"] == title)
            return node
        for g in LAYOUT["room_groups"]:
            first = g["rooms"][0]
            area_path = tabs[first][:-1]
            inner = next(n for n in layout_nodes(find(tree, area_path)) if n.get("type") == "tabbed")
            titles = [t["title"] for t in inner["tabs"]]
            expect = ([g["area"]] if g["image"] else []) + [str(m) for m in g["rooms"]]
            self.assertEqual(titles, expect, g["area"])
            if g["image"]:
                (overview,) = [n for n in layout_nodes(inner["tabs"][0]["content"]) if n.get("type") == "map"]
                self.assertEqual(overview["maps"], [os.path.splitext(os.path.basename(g["image"]))[0]])
            for m, t in zip(g["rooms"], inner["tabs"][-len(g["rooms"]):]):
                self.assertEqual(tabs[m], area_path + [str(m)], f"map {m} switches to its room")
                maps_ = [n["maps"] for n in layout_nodes(t["content"]) if n.get("type") == "map"]
                self.assertEqual(maps_, [[f"room_{m}"]])
                grids = [n["rows"] for n in layout_nodes(t["content"]) if n.get("type") == "itemgrid"]
                self.assertEqual(grids, [[[c] for c in by_room[m]]] if m in by_room else [], f"map {m}'s panel")
        # every tab title on a room's path is unique where it matters: a room's title is its number
        numbers = [t for p in tabs.values() for t in p if t.isdigit()]
        self.assertTrue(numbers)

    def test_pack_size(self):
        total = sum(os.path.getsize(f) for f in glob.glob(os.path.join(PACK, "**", "*"), recursive=True)
                    if os.path.isfile(f))
        self.assertLess(total, PACK_BUDGET, "the pack's files (the zip is smaller)")


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
                    self.assertTrue(node["item"] in codes, node["item"])
                if node.get("type") == "itemgrid":
                    for row in node["rows"]:
                        for c in row:
                            self.assertTrue(c in codes, c)
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
        log = {}, lua_items = {}, section_handlers = {}, frame_handlers = {}}

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
-- a section: PopTracker calls every section-changed handler when its count or highlight changes
local function make_section(full_id, spec)
    local state = {ChestCount = spec.item_count or 1, AvailableChestCount = spec.item_count or 1, Highlight = 0}
    local sec = {FullID = full_id, access = spec.access_rules and spec.access_rules[1],
                 visible = spec.visibility_rules and spec.visibility_rules[1]}
    return setmetatable(sec, {__index = state, __newindex = function(t, k, v)
        local old = state[k]
        state[k] = v
        if old ~= v and not Tracker.BulkUpdate then
            for _, cb in pairs(STUB.section_handlers) do cb(t) end
        end
    end})
end
local function add_locations(nodes, parent)
    for _, node in ipairs(nodes) do
        local id = parent and (parent .. "/" .. node.name) or node.name
        for _, sec in ipairs(node.sections or {}) do
            if sec.name and not sec.ref then
                STUB.sections[id .. "/" .. sec.name] = make_section(id .. "/" .. sec.name, sec)
            end
        end
        add_locations(node.children or {}, id)
    end
end
function Tracker:AddLocations(path) add_locations(py.json(path), nil) return true end
function Tracker:FindObjectForCode(code)
    if code:sub(1, 1) == "@" then return STUB.sections[code:sub(2)] end
    if STUB.by_code[code] then return STUB.by_code[code] end
    for _, item in ipairs(STUB.lua_items) do
        for _, c in ipairs(item.PotentialCodes or {}) do
            if c == code then return item end
        end
    end
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
function ScriptHost:AddOnLocationSectionChangedHandler(name, cb) STUB.section_handlers[name] = cb return name end
function ScriptHost:AddOnFrameHandler(name, cb) STUB.frame_handlers[name] = cb return name end
function ScriptHost:CreateLuaItem()
    local item = {Type = "custom", BadgeText = "", _align = nil, _font = nil}
    function item:SetOverlayAlign(a) self._align = a end
    function item:SetOverlayFontSize(n) self._font = n end
    table.insert(STUB.lua_items, item)
    return item
end
ImageReference = {}
function ImageReference:FromPackRelativePath(path) return path end
-- one frame: every frame handler once
function STUB.frame() for _, cb in pairs(STUB.frame_handlers) do cb(0.016) end end

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

-- every section's access rule, as PopTracker runs them after an item change: AP id -> level
function STUB.levels()
    local out = {}
    for _, s in pairs(STUB.sections) do
        if s.access:find("so2_access|", 1, true) then
            out[tonumber(s.access:match("|(%d+)$"))] = STUB.rule(s.access)
        end
    end
    return out
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
        n = doors = 0
        for _, s in g.STUB.sections.items():
            if s.FullID.startswith("Doors/"):
                self.assertEqual(g.STUB.rule(s.access), 3, "a door with a known destination: Inspect")
                self.assertIsNone(s.visible, "doors are always visible")
                doors += 1
                continue
            self.assertEqual(g.STUB.rule(s.access), 6)
            self.assertTrue(g.STUB.rule(s.visible))
            n += 1
        self.assertEqual(n, len(WORLD.location_name_to_id))
        self.assertEqual(doors, len({e["id"] for r in LAYOUT["rooms"] for e in r["doors"]}))


class TestDoorsLua(TestLua):
    """The live door panel, door clicks, auto tab to rooms and the entrance randomizer's seam, in Lua
    5.4 against the stub (docs/ap/poptracker.md "Rooms and doors")."""

    @classmethod
    def setUpClass(cls):
        cls.ways = doorways()
        cls.tabs = lua_table("scripts/map_tabs.lua", "SO2_MAP_TABS")
        rooms = {r["map"]: r for r in LAYOUT["rooms"]}
        cls.rooms = rooms

    def hints(self, g):
        return [tuple(h.values()) for h in g.STUB.hints.values()]

    def clear_hints(self, lua):
        lua.globals().STUB.hints = lua.table_from([])

    def door(self, want):
        """The first doorway (by id) whose destinations satisfy want(to)."""
        return next(wid for wid, w in sorted(self.ways.items()) if want(w[3]))

    def item(self, g, wid):
        m, letter = self.ways[wid][:2]
        return g.Tracker.FindObjectForCode(g.Tracker, f"door_{m}_{letter.lower()}")

    def test_panel_lines(self):
        lua = self.runtime("map")
        g = lua.globals()
        arrow = "\u2192"
        n = 0
        for wid, (m, letter, _, to) in self.ways.items():
            item = self.item(g, wid)
            self.assertIsNotNone(item, (m, letter))
            text = item.BadgeText
            self.assertTrue(text.startswith(f"{letter} {arrow} "), text)
            first = next((t for t in to if not t.get("conditional")), to[0])
            self.assertIn(str(first["map"]) if first["map"] > 3 else "world map", text)
            self.assertLessEqual(len(text), 48, text)
            self.assertEqual(item._align, "left")
            n += 1
        self.assertEqual(n, len(self.ways))
        # a room's own door: the destination's number and name
        wid = self.door(lambda to: len(to) == 1 and to[0]["map"] in self.rooms and not to[0].get("conditional")
                        and len(self.rooms[to[0]["map"]]["name"]) < 20)
        t = self.ways[wid][3][0]
        self.assertEqual(self.item(g, wid).BadgeText,
                         f"{self.ways[wid][1]} {arrow} {t['map']} {self.rooms[t['map']]['name']}")
        # story variants: the first unconditional destination named, the others by number, * = story
        wid = self.door(lambda to: len({x["map"] for x in to}) > 1 and any(x.get("conditional") for x in to)
                        and any(not x.get("conditional") for x in to))
        text = self.item(g, wid).BadgeText
        self.assertIn("(+", text)
        self.assertIn("*", text)

    def test_click_on_a_door_section(self):
        lua = self.runtime("map")
        g, T = lua.globals(), lua.globals().Tracker
        wid = self.door(lambda to: len(to) == 1 and to[0]["map"] in self.rooms)
        m, letter, _, to = self.ways[wid]
        sec = T.FindObjectForCode(T, f"@Doors/{m} {letter}/Door {letter}")
        self.clear_hints(lua)
        sec.AvailableChestCount = 0                 # PopTracker: a click on "Door A" in the popup
        self.assertEqual(self.hints(g), [], "nothing happens inside PopTracker's click")
        g.STUB.frame()
        self.assertEqual(sec.AvailableChestCount, 1, "a door never counts as checked")
        self.assertEqual(self.hints(g), [("ActivateTab", t) for t in self.tabs[to[0]["map"]]])
        self.assertEqual(self.tabs[to[0]["map"]][-1], str(to[0]["map"]))
        g.STUB.frame()                              # once only
        self.assertEqual(len(self.hints(g)), len(self.tabs[to[0]["map"]]))
        # a check's section changing (the server, a click) is no door: nothing happens
        self.clear_hints(lua)
        loc_map = lua_table("scripts/autotracking/location_mapping.lua", "LOCATION_MAPPING")
        T.FindObjectForCode(T, next(iter(loc_map.values()))[0]).AvailableChestCount = 0
        g.STUB.frame()
        self.assertEqual(self.hints(g), [])

    def test_click_on_a_panel_line(self):
        lua = self.runtime("map")
        g = lua.globals()
        wid = self.door(lambda to: len(to) == 1 and to[0]["map"] in self.rooms)
        self.clear_hints(lua)
        item = self.item(g, wid)
        item.OnLeftClickFunc(item)
        self.assertEqual(self.hints(g), [("ActivateTab", t) for t in self.tabs[self.ways[wid][3][0]["map"]]])
        # several destinations: left = the first unconditional, right = the next ones in turn
        wid = self.door(lambda to: len({x["map"] for x in to}) > 1 and all(x["map"] in self.rooms for x in to)
                        and any(not x.get("conditional") for x in to))
        to = self.ways[wid][3]
        order = [x for x in to if not x.get("conditional")] + [x for x in to if x.get("conditional")]
        item = self.item(g, wid)
        for click, want in ((item.OnLeftClickFunc, order[0]), (item.OnRightClickFunc, order[1]),
                            (item.OnLeftClickFunc, order[0])):
            self.clear_hints(lua)
            click(item)
            self.assertEqual(self.hints(g), [("ActivateTab", t) for t in self.tabs[want["map"]]])
        # a world map: its tab; an undrawn story map: no tab, nothing happens
        wid = self.door(lambda to: to[0]["map"] == 1)
        self.clear_hints(lua)
        self.assertTrue(g.so2_door_go(wid))
        self.assertEqual(self.hints(g), [("ActivateTab", "Expel"), ("ActivateTab", "Expel World Map")])
        wid = self.door(lambda to: any(x["map"] in UNDRAWN_DESTINATIONS for x in to))
        self.clear_hints(lua)
        picked = g.so2_door_pick(g.so2_door_destinations(wid), 1)
        if picked.map in UNDRAWN_DESTINATIONS:
            self.assertFalse(g.so2_door_go(wid))
            self.assertEqual(self.hints(g), [])

    def test_auto_tab_to_rooms(self):
        lua = self.runtime("map")
        g = lua.globals()
        self.connect(lua, {"world_mode": "story"}, [], [])
        for m in (62, 18, 412 if 412 in self.rooms else 26):
            self.clear_hints(lua)
            g.STUB.handlers.SetReply("so2_map_0_1", to_lua(lua, {"map": m}), None)
            self.assertEqual(self.hints(g), [("ActivateTab", t) for t in self.tabs[m]])
            self.assertEqual(self.tabs[m][-1], str(m), "a room's own tab")

    def test_entrance_randomizer_seam(self):
        """Phase C's seam: slot data door_pairs (doorway id -> the doorway at the other end) and the
        found doors in so2_doors_{team}_{slot}: '?' and yellow until found, then the pair's room."""
        lua = self.runtime("map")
        g, T = lua.globals(), lua.globals().Tracker
        ids = sorted(self.ways)
        a = next(i for i in ids if self.ways[i][0] == 18)
        b = next(i for i in ids if self.ways[i][0] == 62)
        other = next(i for i in ids if i not in (a, b))
        self.connect(lua, {"world_mode": "open_world", "door_pairs": {str(a): b, str(b): a}}, [], [])
        self.assertIn("so2_doors_0_1", list(g.STUB.get.values()))
        letter = self.ways[a][1]
        self.assertEqual(self.item(g, a).BadgeText, f"{letter} \u2192 ?")
        sec = T.FindObjectForCode(T, f"@Doors/18 {letter}/Door {letter}")
        self.assertEqual(g.STUB.rule(sec.access), 5, "not found yet: SequenceBreak (yellow)")
        self.assertFalse(g.so2_door_go(a), "unknown: no tab to open")
        self.assertNotIn("?", self.item(g, other).BadgeText, "a door the room doesn't randomize: vanilla")
        g.STUB.handlers.SetReply("so2_doors_0_1", to_lua(lua, [a]), None)
        self.assertTrue(self.item(g, a).BadgeText.startswith(f"{letter} \u2192 62 "))
        self.assertEqual(g.STUB.rule(sec.access), 3)
        self.assertEqual(self.item(g, b).BadgeText, f"{self.ways[b][1]} \u2192 ?", "found one way only")
        self.clear_hints(lua)
        self.assertTrue(g.so2_door_go(a))
        self.assertEqual(self.hints(g), [("ActivateTab", t) for t in self.tabs[62]])
        # a vanilla room again: no pairs, no found key
        self.connect(lua, {"world_mode": "open_world"}, [], [])
        self.assertNotIn("so2_doors_0_1", list(g.STUB.get.values()))
        self.assertNotIn("?", self.item(g, a).BadgeText)


class TestLuaLogic(unittest.TestCase):
    """The pack's evaluator (scripts/logic.lua) against the reference (tools/ap_tracker_logic.py) on the
    exports of rooms of every option mix of the world's UT tests (test_ut.MIXES), each generated and
    filled: for the empty state, the items found before each sphere (the precollected ones first, as
    the server sends them), every item, and random states, the items received through the pack's item
    handler, every section's access rule (`^$so2_access|<id>`) is Normal exactly for the reference's
    locations in logic and None for the rest of the room's; the reached regions, events and goal agree.
    Rooms without the export are a checklist; an export of a newer format too, with its banner."""
    STATES_RANDOM = 3

    @classmethod
    def setUpClass(cls):
        import random
        sys.path.insert(0, HERE)
        import ap_tracker_logic
        from worlds.so2.test.test_ut import MIXES, real_generation, room_slot_data
        cls.rooms = []
        for label, options in MIXES.items():
            mw = real_generation(options, 21)
            world = mw.worlds[1]
            slot = room_slot_data(world)
            ids = lambda items: [world.item_name_to_id[i.name] for i in items]
            received = ids(i for i in mw.precollected_items[1] if i.code is not None)
            states = [("empty", [])]
            for k, sphere in enumerate(mw.get_sendable_spheres(), start=1):
                found = [loc for loc in sphere if loc.address is not None]
                if not found:
                    break
                states.append((f"sphere {k}", list(received)))
                received += ids(loc.item for loc in found if loc.item.player == 1 and loc.item.code is not None)
            states.append(("all", list(received)))
            rng = random.Random(label)
            states += [(f"random {k}", [i for i in received if rng.random() < 0.4]) for k in range(cls.STATES_RANDOM)]
            cls.rooms.append((label, mw, slot, states))
        cls.reference = ap_tracker_logic

    def deliver(self, lua, slot, locations, items):
        g = lua.globals()
        TestLua.connect(self, lua, slot, locations, [])
        for index, code in enumerate(items):
            g.STUB.handlers.Item(index, code, "", 1)

    def test_rules_read_mapped_items(self):
        """Every item a room's rules name is one of the pack's items, so its change (received, clicked)
        reaches the evaluator through the item watch and makes PopTracker run the rules again."""
        mapping = lua_table("scripts/autotracking/item_mapping.lua", "ITEM_MAPPING")
        for label, mw, slot, states in self.rooms:
            refs = {r for body in slot["tracker"]["rules"] if isinstance(body, list) and body[0] in ("has", "all", "any")
                    for r in (body[1:2] if body[0] == "has" else body[1:]) if not isinstance(r, str)}
            with self.subTest(mix=label):
                self.assertEqual(sorted(refs - set(mapping)), [])

    def test_lua_is_the_reference(self):
        lua = TestLua.runtime(self, "map")
        g, T = lua.globals(), lua.globals().Tracker
        total = 0
        for label, mw, slot, states in self.rooms:
            evaluator = self.reference.TrackerLogic(slot["tracker"])
            room = sorted(evaluator.location_of)
            for name, items in states:
                with self.subTest(mix=label, state=name):
                    self.deliver(lua, slot, room, items)
                    self.assertEqual(T.FindObjectForCode(T, "so2_logic_offline").CurrentStage, 2)
                    counts = {}
                    for code in items:
                        counts[code] = counts.get(code, 0) + 1
                    view = evaluator.evaluate(counts)
                    levels = dict(g.STUB.levels().items())
                    self.assertEqual({a for a in room if levels[a] == 6}, view.locations)
                    self.assertEqual({a for a in room if levels[a] == 0}, set(room) - view.locations)
                    lv = g.so2_logic_view()
                    regions = {slot["tracker"]["regions"][k] for k, v in lv.reached.items() if v}
                    self.assertEqual(regions, view.regions)
                    self.assertEqual(dict(lv.got.items()), view.events)
                    self.assertEqual(g.so2_goal_in_logic(), view.goal)
                    total += 1
        self.assertGreater(total, 16 * 5)

    def test_memo_follows_items(self):
        """A received item, a click on a mapped item and its undo change what is in logic at once."""
        lua = TestLua.runtime(self, "map")
        g, T = lua.globals(), lua.globals().Tracker
        label, mw, slot, states = next(r for r in self.rooms if r[0] == "open full")
        evaluator = self.reference.TrackerLogic(slot["tracker"])
        room = sorted(evaluator.location_of)
        start = states[1][1]          # before sphere 1: the precollected items
        self.deliver(lua, slot, room, start)
        before = {a for a, v in g.STUB.levels().items() if v == 6} & set(room)
        self.assertEqual(before, evaluator.evaluate({c: 1 for c in start}).locations)
        key = WORLD.item_name_to_id["Salva Pass"]
        self.assertNotIn(key, start)
        g.STUB.handlers.Item(len(start), key, "Salva Pass", 1)
        after = {a for a, v in g.STUB.levels().items() if v == 6} & set(room)
        self.assertEqual(after, evaluator.evaluate({**{c: 1 for c in start}, key: 1}).locations)
        self.assertGreater(len(after), len(before))
        T.FindObjectForCode(T, "key_salva_pass").Active = False     # the player undoes it by a click
        self.assertEqual({a for a, v in g.STUB.levels().items() if v == 6} & set(room), before)

    def test_deferred_entrance(self):
        """An entrance the export leaves unconnected (`to` -1, Phase C's deferred entrances) reaches
        nothing until so2_logic_connect names its region; then it is the reference's again, rules re-run."""
        lua = TestLua.runtime(self, "map")
        g, T = lua.globals(), lua.globals().Tracker
        label, mw, slot, states = next(r for r in self.rooms if r[0] == "open full")
        export = json.loads(json.dumps(slot["tracker"]))
        door = next(e for e in export["entrances"] if e[0] == "Disc Switch")
        door[2] = -1
        cut = dict(slot, tracker=export)
        reference = self.reference.TrackerLogic(export)
        room = sorted(reference.location_of)
        items = states[-4][1]           # every item
        counts = {}
        for code in items:
            counts[code] = counts.get(code, 0) + 1
        self.deliver(lua, cut, room, items)

        def in_logic():
            return {a for a, v in g.STUB.levels().items() if v == 6} & set(room)
        view = reference.evaluate(counts)
        self.assertNotIn("Energy Nede", view.regions)
        self.assertEqual(in_logic(), view.locations)
        self.assertFalse(g.so2_logic_connect("No such door", "Energy Nede"))
        rev = T.FindObjectForCode(T, "so2_rev").AcquiredCount
        self.assertTrue(g.so2_logic_connect("Disc Switch", "Energy Nede"))
        self.assertGreater(T.FindObjectForCode(T, "so2_rev").AcquiredCount, rev, "the rules are re-run")
        reference.connect("Disc Switch", "Energy Nede")
        whole = self.reference.TrackerLogic(slot["tracker"]).evaluate(counts)
        self.assertEqual(reference.evaluate(counts).locations, whole.locations)
        self.assertEqual(in_logic(), whole.locations)
        self.assertGreater(len(whole.locations), len(view.locations))

    STORY_POSITIONS = [(0, 0), (2, 0), (3, 0), (5, 0), (12, 0), (24, 500), (35, 9999), (36, 0), (41, 0), (48, 0),
                       (57, 0), (58, 0), (60, 3000), (60, 4000), (60, 5000)]

    def test_story_is_the_reference(self):
        """Story awareness: in every story room, for item states and story positions sent as the port
        sends them (SetReply of so2_story_0_1), every section is Normal for the reference's locations in
        logic for the story, Inspect for its `later` ones and None for the rest; a step-only change
        (chapter 60 step 3000 -> 4000) re-runs the rules; Chapter clicked by hand counts as a position
        (step 0); the "Story" setting off and chapter 0 give the plain logic; open world and hub rooms
        never show Inspect."""
        lua = TestLua.runtime(self, "map")
        g, T = lua.globals(), lua.globals().Tracker
        total = 0
        for label, mw, slot, states in self.rooms:
            evaluator = self.reference.TrackerLogic(slot["tracker"])
            room = sorted(evaluator.location_of)
            story = "story" in slot["tracker"]
            self.assertEqual(story, label.startswith("story"), label)
            for name, items in (states[:1] + states[1:2] + states[-4:-3]) if story else states[-4:-3]:
                counts = {}
                for code in items:
                    counts[code] = counts.get(code, 0) + 1
                plain = evaluator.evaluate(counts).locations
                self.deliver(lua, slot, room, items)
                for chapter, step in self.STORY_POSITIONS:
                    value = {"chapter": chapter, "step": step}
                    rev = T.FindObjectForCode(T, "so2_rev").AcquiredCount
                    g.STUB.handlers.SetReply("so2_story_0_1", to_lua(lua, value), None)
                    self.assertGreater(T.FindObjectForCode(T, "so2_rev").AcquiredCount, rev)
                    view = evaluator.evaluate(counts, value if chapter > 0 else None)
                    levels = dict(g.STUB.levels().items())
                    with self.subTest(mix=label, state=name, story=value):
                        self.assertEqual({a for a in room if levels[a] == 6}, view.locations)
                        self.assertEqual({a for a in room if levels[a] == 3}, set(view.later))
                        self.assertEqual({a for a in room if levels[a] == 0}, set(room) - plain)
                        if not story:
                            self.assertEqual(view.locations, plain)
                    total += 1
                if not story:
                    continue
                # the "Story" setting off: the plain logic; on again: the story's
                aware = T.FindObjectForCode(T, "so2_story_aware")
                self.assertTrue(aware.Active)
                g.STUB.handlers.SetReply("so2_story_0_1", to_lua(lua, {"chapter": 12, "step": 0}), None)
                aware.Active = False
                levels = dict(g.STUB.levels().items())
                self.assertEqual({a for a in room if levels[a] == 6}, plain)
                aware.Active = True
                levels = dict(g.STUB.levels().items())
                self.assertEqual({a for a in room if levels[a] == 6},
                                 evaluator.evaluate(counts, {"chapter": 12, "step": 0}).locations)
                # Chapter by hand (no port): chapter 20, step 0; 0 is no position
                T.FindObjectForCode(T, "so2_chapter").AcquiredCount = 20
                levels = dict(g.STUB.levels().items())
                self.assertEqual({a for a in room if levels[a] == 6},
                                 evaluator.evaluate(counts, {"chapter": 20, "step": 0}).locations)
                T.FindObjectForCode(T, "so2_chapter").AcquiredCount = 0
                levels = dict(g.STUB.levels().items())
                self.assertEqual({a for a in room if levels[a] == 6}, plain)
                # a value that is no position changes nothing
                g.STUB.handlers.SetReply("so2_story_0_1", to_lua(lua, {"step": 5}), None)
                g.STUB.handlers.SetReply("so2_story_0_1", None, None)
                self.assertEqual({a for a in room if dict(g.STUB.levels().items())[a] == 6}, plain)
        self.assertGreater(total, 3 * 3 * len(self.STORY_POSITIONS))

    def test_checklist_rooms(self):
        """A room without the export (0.2.0 / 0.2.1) and one whose export is a newer format: every
        location shows as reachable, with the banner saying so."""
        lua = TestLua.runtime(self, "map")
        g, T = lua.globals(), lua.globals().Tracker
        label, mw, slot, states = self.rooms[0]
        room = sorted(self.reference.TrackerLogic(slot["tracker"]).location_of)
        for tracker, stage in ((None, 1), (dict(slot["tracker"], format=99), 3)):
            with self.subTest(stage=stage):
                old = {k: v for k, v in slot.items() if k != "tracker"}
                if tracker is not None:
                    old["tracker"] = tracker
                self.deliver(lua, old, room, [])
                self.assertEqual(T.FindObjectForCode(T, "so2_logic_offline").CurrentStage, stage)
                levels = g.STUB.levels()
                self.assertEqual({levels[a] for a in room}, {6})
                self.assertIsNone(g.so2_logic_view())


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
