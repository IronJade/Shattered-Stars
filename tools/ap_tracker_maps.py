#!/usr/bin/env python3
"""The tracker map layout (docs/ap/tracker_maps.json; decision D5 of docs/plans/trackers.md).

    tools/ap_tracker_maps.py DISC1.cue [-o docs/ap/tracker_maps.json] [--check]

The PopTracker pack (tools/ap_poptracker.py, B4) and the port's `--render-tracker-maps`
command share one layout: the same image paths and sizes, each field map at the same
rectangle, each check's dot at the same pixel. This tool computes that layout once from the
repo's tables and the disc, and writes it as numbers and names only (no pixels, no textures,
no text from the disc beyond the names already in the repo's tables):

- one image per Archipelago area that holds a place check (chest, gift, boss, join): the
  field maps holding its checks, ordered by the door graph from the area's entrance, packed
  in rows at one scale per image (1, 3/4, 2/3 or 1/2: the largest that keeps both sides
  within 2048 px), each with a label strip above it;
- one image per world map with an area dot (Expel, Energy Nede), drawn top-down: image x =
  (world x - x0) / u, image y = (z0 - world z) / u (Expel: the whole torus, x0 0, z0
  0x15000, u 54; Energy Nede: its playable disc, x0 42000, z0 65000, u 25), one dot per
  world-map entrance group at the arrival point of its doors back to that world map
  (docs/ap/doors.json);
- each place check's pixel: chests from their chest_create position (B 0x6c), gifts / joins
  / bosses from the actor (ext 0x70 / 0xf3) or the trigger region (ext 0x74 / 0x75 / 0xd8 /
  0xd9) that registers the routine giving them (docs/ap/field.json `via`), else the map's
  default spawn point; world points projected through the map's field camera (MapScene
  +0x08..+0x6a, the view actor_sys_setup_view builds) into background pixels, trigger regions
  (screen coordinates) moved by the view origin, then into the image;
- (format 1.1) one room image per field map an area's doors reach (images/rooms/<map>.png:
  the background 1:1 under a label strip), grouped by area, with each doorway's spot, letter
  box, doorway id (the lowest docs/ap/doors.json door id it holds) and vanilla destinations,
  and each check's pixel on its room.

The disc is read for the map scenes (camera, background size) and scripts (constant
arguments of the registering instructions). --check regenerates and compares with the
committed file. Format: docs/ap/tracker_maps.md.
"""

import argparse
import ast
import collections
import csv
import json
import math
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import so2_data  # noqa: E402
from so2_archive import unpack_block  # noqa: E402

FORMAT = 1
FORMAT_MINOR = 1     # 1.1: rooms, doors, room_groups, checks' "room" (additive: a format 1 reader ignores them)
PACK_UID = "so2_shattered_stars"
PLACE_TYPES = ("chest", "gift", "boss", "join")
MAX_SIDE = 2048
SCALES = ((1, 1), (3, 4), (2, 3), (1, 2))   # tried in order, the first that fits
MARGIN = 8           # image border
GAP = 8              # between maps, both ways
LABEL_H = 16         # label strip above each map (the port draws a 5x7 font at 2x in it)
BACKGROUND = "#1c1c22"
LABEL_COLOUR = "#e8e8f0"
DOT = {"size": 12, "colour": "#ff3030", "outline": "#ffffff"}
DOOR_STYLE = {"fill": "#ffd23c", "text": "#14141a", "outline": "#14141a"}   # a room's door letters
# World maps: the torus is 9 x 7 blocks of 0x3000 units (docs/notes/field_overlay_c1e.md).
WORLD_W, WORLD_H = 0x1B000, 0x15000
WORLD_NAMES = {1: "Expel", 2: "Energy Nede", 3: "world map 3"}
# The part of each world drawn (world units: left x, top z, units per pixel, size): Expel's
# whole torus; Energy Nede's playable disc (radius 0x5cf8 around (67000, 40000):
# world_terrain_query) with a margin.
WORLD_VIEWS = {1: (0, WORLD_H, 54, WORLD_W // 54, WORLD_H // 54),
               2: (42000, 65000, 25, 2000, 2000)}
WORLD_MAPS = (1, 2, 3)


# ---- Inputs ---------------------------------------------------------------------------------

def literal_table(path, name):
    """A table of the generated apworld data.py, read without importing Archipelago."""
    src = open(path, encoding="utf-8").read()
    tree = ast.parse(src)
    for node in tree.body:
        if isinstance(node, ast.Assign) and any(isinstance(t, ast.Name) and t.id == name for t in node.targets):
            return ast.literal_eval(node.value)
    raise SystemExit(f"{path}: no table {name}")


def place_rows():
    data_py = os.path.join(REPO, "apworld", "so2", "data.py")
    rows = literal_table(data_py, "LOCATIONS") + literal_table(data_py, "HUB_LOCATIONS")
    return [r for r in rows if r["type"] in PLACE_TYPES]


def map_names():
    with open(os.path.join(REPO, "docs", "modding", "reference", "map_names.csv"), newline="", encoding="utf-8") as f:
        return {int(r["map"]): r for r in csv.DictReader(f)}


def slug(text):
    return re.sub(r"[^a-z0-9]+", "_", text.lower().replace("'", "")).strip("_")


# ---- The field camera -----------------------------------------------------------------------

def _rx(a):
    c, s = math.cos(a), math.sin(a)
    return ((1, 0, 0), (0, c, -s), (0, s, c))


def _ry(a):
    c, s = math.cos(a), math.sin(a)
    return ((c, 0, s), (0, 1, 0), (-s, 0, c))


def _rz(a):
    c, s = math.cos(a), math.sin(a)
    return ((c, -s, 0), (s, c, 0), (0, 0, 1))


def _mul(a, b):
    return tuple(tuple(sum(a[i][k] * b[k][j] for k in range(3)) for j in range(3)) for i in range(3))


_FLIP = ((1, 0, 0), (0, -1, 0), (0, 0, -1))   # the field's constant flip matrix (0x80072e2c)
_ANGLE = 2 * math.pi / 4096


class Scene:
    """The parts of a map's MapScene (section 0 block 0) the projection and layout need
    (docs/notes/field_part3.md 3.1): background size +4/+6, eye +8, default spawn (camera
    target) +0x14, projection origin +0x28/+0x2c, roll +0x30, projection plane +0x34/+0x36,
    floor y +0x58, H +0x6a, camera angles +0xdc/+0xde. actor_sys_setup_view loads the GTE with
    Rz(roll) Rx(+0xdc) Ry(+0xde) diag(1, -1, -1) and translation -eye; an actor's screen
    point is H x / z + plane / 2 (fieldactor_place), and its background pixel that minus
    +0x28/+0x2c (bg_get_view_origin)."""

    def __init__(self, blob):
        self.w, self.h = struct.unpack_from("<HH", blob, 4)
        self.eye = struct.unpack_from("<3i", blob, 8)
        self.spawn = struct.unpack_from("<3i", blob, 0x14)
        self.ox, self.oy, roll = struct.unpack_from("<3i", blob, 0x28)
        self.pw, self.ph = struct.unpack_from("<HH", blob, 0x34)
        self.floor = struct.unpack_from("<h", blob, 0x58)[0]
        self.H = struct.unpack_from("<H", blob, 0x6A)[0]
        ax, ay = struct.unpack_from("<hh", blob, 0xDC)
        r = _mul(_mul(_mul(_rz(((roll & 0xFFFF) if roll & 0xFFFF < 0x8000 else (roll & 0xFFFF) - 0x10000) * _ANGLE),
                               _rx(ax * _ANGLE)), _ry(ay * _ANGLE)), _FLIP)
        self.R = r

    def project(self, x, y, z):
        """World (x, y, z) to background pixels (floats), or None behind the camera."""
        v = (x - self.eye[0], y - self.eye[1], z - self.eye[2])
        c = [sum(self.R[i][k] * v[k] for k in range(3)) for i in range(3)]
        if c[2] <= 1e-6:
            return None
        return (self.H * c[0] / c[2] + self.pw / 2 - self.ox, self.H * c[1] / c[2] + self.ph / 2 - self.oy)


class DiscData:
    def __init__(self, path):
        self.disc = so2_data.Disc(path)
        self._scenes = {}
        self._ins = {}

    def scene(self, m):
        if m not in self._scenes:
            try:
                data, sections = self.disc.map_sections(m)
                self._scenes[m] = Scene(bytes(unpack_block(data, sections[0]))) if 0 in sections else None
            except (SystemExit, KeyError, ValueError, IndexError, struct.error):
                self._scenes[m] = None
        return self._scenes[m]

    def ins(self, m):
        if m not in self._ins:
            try:
                ins = so2_data.instructions(self.disc.map_script(m))
            except SystemExit:
                ins = []
            self._ins[m] = (ins, {i[0]: n for n, i in enumerate(ins)})
        return self._ins[m]


def instruction_args(ins, n):
    """The arguments of instruction n as the VM fetches them (a[] in script_ext.cpp): the
    constants pushed before a stack form (None where not constant), the 16-bit halves of an
    inline form; and the form."""
    text = ins[n][2]
    m = re.match(r"^\w+ <(\d+) from stack>", text)
    if m:
        count = int(m.group(1))
        if n < count:
            return [None] * count, "stack"
        return [so2_data.push_value(ins[n - count + k]) for k in range(count)], "stack"
    m = re.match(r"^\w+\((.*)\)", text)
    if m:
        return [int(x) for x in re.findall(r"-?\d+", m.group(1))], "inline"
    return [], "none"


def registration_point(dd, m, offset, by, scene):
    """(x, y, z, how[, corners]) of what a registering instruction places: an actor's world
    position (ext 0x70 actor_create, 0xf3 actor_create with a pickpocket entry), or a trigger
    region (ext 0x74 / 0x75 thread_start, 0xd8 / 0xd9 trigger_add_button) as (centre x, None,
    centre y, "region", corners). A field map's regions are in screen coordinates, not world
    ones: trigger_check_regions tests the leader's actor +0x74 / +0x76 (its projected screen
    point) while field_state +0x68 (3D mode) is 0, as on every field map (docs/notes/
    field_part4.md 2), so a region's background pixels are its corners minus the view origin
    (MapScene +0x28 / +0x2c); bg_pixel does that. None when the arguments are not constant or
    the instruction places nothing."""
    ins, at = dd.ins(m)
    n = at.get(offset)
    if n is None:
        return None
    a, form = instruction_args(ins, n)
    if by in ("ext_70", "ext_f3"):
        if len(a) >= 5 and None not in a[2:5]:
            return (a[2], a[3], a[4], "actor")
        return None
    if by in ("thread_start", "thread_start_ex", "ext_d8", "ext_d9"):
        first = 0 if by.startswith("thread") else (3 if form == "inline" else 1)
        c = a[first:first + 8]
        if len(c) == 8 and None not in c:
            xs, ys = c[0::2], c[1::2]
            return ((min(xs) + max(xs)) / 2, None, (min(ys) + max(ys)) / 2, "region", tuple(zip(xs, ys)))
    return None


def via_registrations(via):
    """Registering instructions in a field.json `via` tree, shallowest first."""
    out, queue = [], [(v, 0) for v in via or []]
    while queue:
        v, depth = queue.pop(0)
        if "registered_at" in v:
            out.append((depth, v["registered_at"], v["by"]))
        queue.extend((w, depth + 1) for w in v.get("via") or [])
    out.sort(key=lambda r: r[0])
    return out


# ---- Check positions inside their map -------------------------------------------------------

def chest_points(dd, m):
    """{opened flag: (x, y, z)} of the map's chest_create calls (the first per flag)."""
    ins, _ = dd.ins(m)
    out = {}
    for n, i in enumerate(ins):
        if not i[2].startswith("B_6c"):
            continue
        a, _ = instruction_args(ins, n)
        if len(a) >= 7 and None not in a[2:5] and a[6] is not None:
            out.setdefault(a[6], (a[2], a[3], a[4]))
    return out


def event_records(field):
    """Location id -> the field.json records (gifts, Fol gifts, joins) of that location."""
    out = collections.defaultdict(list)
    for key in ("gifts", "fol_gifts", "recruitments"):
        for g in field[key]:
            ids = {g.get("location_id")} | {s.get("location_id") for s in g.get("sites", [])}
            for i in ids - {None}:
                out[i].append(g)
    return out


def check_points(rows, field, dd):
    """{location name: (map, x, y, z, how)}; how: chest (chest_create), actor, region, spawn
    (the map's default spawn point: the check is somewhere in this map)."""
    events = event_records(field)
    bosses = {b["encounter"]: b for b in field["bosses"]}
    chests = {}
    out = {}
    for r in rows:
        m = r["map"]
        scene = dd.scene(m)
        if scene is None:
            out[r["name"]] = (m, None, None, None, "no-scene", None)
            continue
        point = None
        if r["type"] == "chest":
            if m not in chests:
                chests[m] = chest_points(dd, m)
            p = chests[m].get(r["flag"])
            if p:
                point = (*p, "chest", None)
        else:
            vias = []
            if r["type"] == "boss":
                for s in bosses.get(r["encounter"], {}).get("sites", []):
                    if s["map"] == m:
                        vias.append(s.get("via"))
            else:
                for g in events.get(r["id"], []):
                    if g["map"] == m or m in g.get("maps", []):
                        vias.append(g.get("via"))
            for via in vias:
                for _, offset, by in via_registrations(via):
                    point = registration_point(dd, m, offset, by, scene)
                    if point:
                        break
                if point:
                    break
        if point is None:
            p = scene.project(*scene.spawn)
            inside = p is not None and 0 <= p[0] < scene.w and 0 <= p[1] < scene.h
            point = (*scene.spawn, "spawn", None) if inside else (None, None, None, "map", None)
        elif len(point) == 4:
            point = (*point, None)
        out[r["name"]] = (m, *point)
    return out


def hull(points):
    """Convex hull (counter-clockwise in image coordinates' orientation), monotone chain."""
    pts = sorted(set(points))
    if len(pts) <= 2:
        return pts
    def cross(o, a, b):
        return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])
    lower, upper = [], []
    for p in pts:
        while len(lower) >= 2 and cross(lower[-2], lower[-1], p) <= 0:
            lower.pop()
        lower.append(p)
    for p in reversed(pts):
        while len(upper) >= 2 and cross(upper[-2], upper[-1], p) <= 0:
            upper.pop()
        upper.append(p)
    return lower[:-1] + upper[:-1]


def clip(poly, w, h):
    """Sutherland-Hodgman: the polygon inside [0, w] x [0, h]."""
    edges = ((lambda p: p[0] >= 0, lambda a, b: (0, a[1] + (b[1] - a[1]) * (0 - a[0]) / (b[0] - a[0]))),
             (lambda p: p[0] <= w, lambda a, b: (w, a[1] + (b[1] - a[1]) * (w - a[0]) / (b[0] - a[0]))),
             (lambda p: p[1] >= 0, lambda a, b: (a[0] + (b[0] - a[0]) * (0 - a[1]) / (b[1] - a[1]), 0)),
             (lambda p: p[1] <= h, lambda a, b: (a[0] + (b[0] - a[0]) * (h - a[1]) / (b[1] - a[1]), h)))
    for inside, cut in edges:
        if not poly:
            break
        out = []
        for i, cur in enumerate(poly):
            prev = poly[i - 1]
            if inside(cur):
                if not inside(prev):
                    out.append(cut(prev, cur))
                out.append(cur)
            elif inside(prev):
                out.append(cut(prev, cur))
        poly = out
    return poly


def centroid(poly):
    a = cx = cy = 0.0
    for i, p in enumerate(poly):
        q = poly[(i + 1) % len(poly)]
        f = p[0] * q[1] - q[0] * p[1]
        a += f
        cx += (p[0] + q[0]) * f
        cy += (p[1] + q[1]) * f
    if abs(a) < 1e-9:
        return (sum(p[0] for p in poly) / len(poly), sum(p[1] for p in poly) / len(poly))
    return (cx / (3 * a), cy / (3 * a))


def bg_pixel(scene, x, y, z, how, corners):
    """(px, py, inside): a spot's background pixel (floats, inside the background). A world
    point projected; a trigger region's centre (screen coordinates minus the view origin),
    or when that is off the background the middle of the region's visible part; else the
    nearest edge pixel (inside False). 'map' spots and points behind the camera take the
    background's centre."""
    bw, bh = scene.w, scene.h
    if how == "region":
        p = (x - scene.ox, z - scene.oy)
    else:
        p = scene.project(x, y, z) if how != "map" else (bw / 2, bh / 2)
    inside = p is not None and 0 <= p[0] < bw and 0 <= p[1] < bh
    if not inside and corners:
        poly = clip(hull([(cx - scene.ox, cy - scene.oy) for cx, cy in corners]), bw - 1, bh - 1)
        if len(poly) >= 3:
            p, inside = centroid(poly), True
    if p is None:
        p = (bw / 2, bh / 2)
    return min(max(p[0], 0), bw - 1), min(max(p[1], 0), bh - 1), inside


# ---- Map order (door graph) -----------------------------------------------------------------

def door_graph(doors):
    """Undirected field-map adjacency (world maps and their chunks left out) and each map's
    exits to a world map: {map: [(world, x, z)]}."""
    adj = collections.defaultdict(set)
    exits = collections.defaultdict(list)
    for d in doors["doors"]:
        t = d["to"]["map"]
        for m in d.get("maps") or [d["map"]]:
            if m in WORLD_MAPS or m >= 900 or t is None:
                continue
            if t in WORLD_MAPS:
                if d["to"]["x"] is not None and d["to"]["z"] is not None:
                    exits[m].append((t, d["to"]["x"], d["to"]["z"]))
            elif t < 900:
                adj[m].add(t)
                adj[t].add(m)
    return adj, exits


def bfs(adj, starts):
    dist = {s: 0 for s in starts}
    queue = collections.deque(sorted(starts))
    while queue:
        m = queue.popleft()
        for n in sorted(adj.get(m, ())):
            if n not in dist:
                dist[n] = dist[m] + 1
                queue.append(n)
    return dist


def order_maps(maps, adj, exits, entrances):
    """The image's maps from the area's entrance outwards: BFS over the door graph from the
    maps its world-map entrance leads to (else those among them with a world-map exit, else
    the lowest), ties by map number."""
    entrances = entrances or [m for m in maps if exits.get(m)] or [min(maps)]
    dist = bfs(adj, entrances)
    return sorted(maps, key=lambda m: (dist.get(m, 1 << 20), m))


# ---- Packing --------------------------------------------------------------------------------

def scaled(v, s):
    return (v * s[0] + s[1] // 2) // s[1]


def pack(sizes, s, row_width):
    """Shelf packing in the given order: rows of maps left to right up to row_width.
    Returns ([(x, y, w, h)] of the map rectangles (the label strip is above each), W, H)."""
    rects, x, y, row_h, width = [], MARGIN, MARGIN, 0, 0
    for (bw, bh) in sizes:
        w, h = scaled(bw, s), scaled(bh, s)
        if x > MARGIN and x + w > MARGIN + row_width:
            x, y, row_h = MARGIN, y + row_h + GAP, 0
        rects.append((x, y + LABEL_H, w, h))
        x += w + GAP
        row_h = max(row_h, LABEL_H + h)
        width = max(width, x - GAP + MARGIN)
    return rects, width, y + row_h + MARGIN


def layout_area(sizes):
    """The scale and packing of one area image: the largest scale that fits MAX_SIDE, then
    the row width giving the squarest image (ties: the smaller area, then the narrower)."""
    for s in SCALES:
        best = None
        widest = max(scaled(w, s) for w, _ in sizes)
        total = sum(scaled(w, s) + GAP for w, _ in sizes)
        for row_width in range(widest, max(widest, total) + 1, 8):
            rects, W, H = pack(sizes, s, row_width)
            if W > MAX_SIDE or H > MAX_SIDE:
                continue
            key = (max(W, H), W * H, W)
            if best is None or key < best[0]:
                best = (key, rects, W, H)
        if best:
            return s, best[1], best[2], best[3]
    raise SystemExit(f"an area does not fit {MAX_SIDE} px at scale 1/2: {sizes}")


# ---- Rooms and doors (format 1.1) -----------------------------------------------------------

PHYSICAL = ("thread_start", "thread_start_ex", "ext_d8", "ext_d9")   # trigger regions


def is_room_map(m, names):
    return m not in WORLD_MAPS and m < 900 and (names.get(m) or {}).get("kind") != "world"


def room_groups(area_maps, entrance_of, extra_groups, adj, doors, names, dd):
    """{map: group} and {group: entrance maps} for every field map drawn as a room.

    The groups are the area images (seeded with their maps) and the world-map entrance groups
    with no area image (seeded with their entrance maps). From the seeds, breadth first and
    nearest group first: (1) over any door to a map of one of the group's places (map_names.csv
    `area` of its seeds); (2) then over two-way doors with trigger regions both ways, to any
    map still without a group (buildings with places of their own). A map in two area images
    belongs to the one whose name is its place, else the first by name."""
    place = {m: (r.get("area") or "") for m, r in names.items()}
    seeds = {}
    for g in sorted(area_maps):
        for m in sorted(area_maps[g]):
            seeds.setdefault(m, []).append(g)
    for g in sorted(extra_groups):
        for m in sorted(extra_groups[g]):
            if dd.scene(m) is not None and is_room_map(m, names):
                # an entrance into a place that has an area image of its own joins that image
                seeds.setdefault(m, []).append(place.get(m) if place.get(m) in area_maps else g)
    home = {m: sorted(gs, key=lambda g: (g != place.get(m), g))[0] for m, gs in seeds.items()}
    places = collections.defaultdict(set)
    for m, gs in seeds.items():
        for g in gs:
            places[g].add(place.get(m))

    def ok(n):
        return n not in home and is_room_map(n, names) and dd.scene(n) is not None

    queue = collections.deque(sorted(home, key=lambda m: (home[m], m)))
    while queue:
        m = queue.popleft()
        for n in sorted(adj.get(m, ())):
            if ok(n) and place.get(n) in places[home[m]]:
                home[n] = home[m]
                queue.append(n)
    directed = collections.defaultdict(set)
    for d in doors["doors"]:
        t = d["to"]["map"]
        if t is None or d.get("world_script") or not any(v.get("by") in PHYSICAL for v in d.get("via") or []):
            continue
        directed[d["map"]].add(t)
    queue = collections.deque(sorted(home, key=lambda m: (home[m], m)))
    while queue:
        m = queue.popleft()
        for n in sorted(directed.get(m, ())):
            if ok(n) and m in directed.get(n, ()):
                home[n] = home[m]
                queue.append(n)

    def leads_back(n, g):
        """n reaches a map of group g again through doors with trigger regions."""
        seen, todo = {n}, [n]
        while todo:
            for t in sorted(directed.get(todo.pop(), ())):
                if home.get(t) == g:
                    return True
                if t not in seen and t not in home:
                    seen.add(t)
                    todo.append(t)
        return False

    # (3) one-way loops of such doors (in through one door, out through another: Bowman's House)
    changed = True
    while changed:
        changed = False
        for m in sorted(home, key=lambda m: (home[m], m)):
            for n in sorted(directed.get(m, ())):
                if ok(n) and leads_back(n, home[m]):
                    home[n] = home[m]
                    changed = True
    members = collections.defaultdict(list)
    for m, g in home.items():
        members[g].append(m)
    entrances = {}
    for g, ms in members.items():
        ins = [m for m in entrance_of.get(g, []) if m in ms]
        entrances[g] = ins
    return home, entrances


def font_scale(w, h):
    """The room's text scale (5x7 font, 1..4): about 20 px high once PopTracker fits the
    image into a 1000 x 800 panel."""
    k = max(w * 4, h * 5)
    return max(1, min(4, (20 * k + 14000) // 28000))


def letter(i):
    """0 -> A, 25 -> Z, 26 -> AA."""
    s, i = "", i + 1
    while i:
        i, r = divmod(i - 1, 26)
        s = chr(65 + r) + s
    return s


def text_box_size(text, fs):
    """A text box: the text in the 5x7 font at fs (advance 6 fs) with a 2 fs border."""
    return (6 * len(text) - 1) * fs + 4 * fs, 7 * fs + 4 * fs


def overlap(a, b):
    w = min(a[0] + a[2], b[0] + b[2]) - max(a[0], b[0])
    h = min(a[1] + a[3], b[1] + b[3]) - max(a[1], b[1])
    return w * h if w > 0 and h > 0 else 0


def place_box(px, py, bw, bh, area, obstacles, gap):
    """Top-left of a bw x bh box beside (px, py), inside 'area' (x, y, w, h), clear of the
    obstacle rectangles if possible: right, left, above, below, then the diagonals, at
    'gap' and then one and two box heights further out; else the spot overlapping least."""
    ax, ay, aw, ah = area
    best = None
    for ring in range(3):
        d = gap + ring * bh
        for x, y in ((px + d, py - bh // 2), (px - d - bw, py - bh // 2), (px - bw // 2, py - d - bh),
                     (px - bw // 2, py + d), (px + d, py - d - bh), (px - d - bw, py - d - bh),
                     (px + d, py + d), (px - d - bw, py + d)):
            x = min(max(x, ax), ax + aw - bw)
            y = min(max(y, ay), ay + ah - bh)
            cost = sum(overlap((x, y, bw, bh), o) for o in obstacles)
            if cost == 0:
                return x, y
            if best is None or cost < best[0]:
                best = (cost, x, y)
    return best[1], best[2]


def door_spots(dd, m, records):
    """The map's doors grouped into doorways: [(doorway id, [door records], [(px, py, inside,
    how, anchor)])] and the doors started only by the map's own code. A doorway is the set of
    doors (doors.json records: one transfer instruction each) started by one trigger: doors of
    one routine, or whose triggers sit on the same pixel, are one doorway; its id is its lowest
    door id. A trigger's spot is the shallowest registration with constant arguments in the
    door's `via` tree (a trigger region, or an actor), placed as a check's dot is (bg_pixel);
    'anchor' is the trigger's box in background pixels (an actor's: its point)."""
    scene = dd.scene(m)
    spots = {}       # door id -> [(px, py, inside, how, anchor)]
    scene_only = []
    for d in records:
        regs = via_registrations(d.get("via"))
        found, depth0 = [], None
        for depth, offset, by in regs:
            if depth0 is not None and depth > depth0:
                break
            p = registration_point(dd, m, offset, by, scene)
            if p:
                depth0 = depth
                corners = p[4] if len(p) > 4 else None
                px, py, inside = bg_pixel(scene, p[0], p[1], p[2], p[3], corners)
                if corners:
                    xs = [c[0] - scene.ox for c in corners]
                    ys = [c[1] - scene.oy for c in corners]
                    anchor = (min(xs), min(ys), max(xs), max(ys))
                else:
                    anchor = (px, py, px, py)
                found.append((int(px), int(py), inside, p[3], anchor))
        if found:
            spots[d["id"]] = sorted(set(found), key=lambda s: (s[0], s[1]))
        else:
            scene_only.append(d)
    parent = {i: i for i in spots}

    def root(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i

    def join(a, b):
        a, b = root(a), root(b)
        if a != b:
            parent[max(a, b)] = min(a, b)

    by_routine, by_pixel = {}, {}
    for d in records:
        i = d["id"]
        if i not in spots:
            continue
        join(i, by_routine.setdefault(d["routine"], i))
        for s in spots[i]:
            join(i, by_pixel.setdefault((s[0], s[1]), i))
    ways = collections.defaultdict(list)
    for d in records:
        if d["id"] in spots:
            ways[root(d["id"])].append(d)
    out = []
    for wid in sorted(ways):
        ds = sorted(ways[wid], key=lambda d: d["id"])
        marks = {}
        for d in ds:
            for s in spots[d["id"]]:
                marks.setdefault((s[0], s[1]), s)
        out.append((min(d["id"] for d in ds), ds, [marks[k] for k in sorted(marks)]))
    return out, sorted(scene_only, key=lambda d: d["id"])


def anchor_distance(anchor, x, y):
    """Squared distance in background pixels from (x, y) to a trigger's box (0 inside)."""
    dx = max(anchor[0] - x, 0, x - anchor[2])
    dy = max(anchor[1] - y, 0, y - anchor[3])
    return dx * dx + dy * dy


def build_rooms(dd, names, doors, adj, home, entrances, area_maps, check_px, group_world, group_image):
    """The room images (format 1.1; docs/ap/tracker_maps.md "Rooms")."""
    records = collections.defaultdict(list)
    for d in doors["doors"]:
        if d["to"]["map"] is not None and not d.get("world_script") and d["map"] in home:
            records[d["map"]].append(d)
    ways = {m: door_spots(dd, m, records.get(m, [])) for m in sorted(home)}

    def back(m, d):
        """The doorway of the target map that leads back to m nearest the arrival point (in
        the target's background pixels)."""
        t, to = d["to"]["map"], d["to"]
        if t not in ways or None in (to["x"], to["y"], to["z"]):
            return None
        q = dd.scene(t).project(to["x"], to["y"], to["z"])
        if q is None:
            return None
        x, z = q
        best = None
        for wid, ds, marks in ways[t][0]:
            if any(e["to"]["map"] == m for e in ds):
                for s in marks:
                    key = (anchor_distance(s[4], x, z), wid)
                    if best is None or key < best:
                        best = key
        return best[1] if best else None

    def target(m, d):
        rec = {"door": d["id"], "map": d["to"]["map"]}
        b = back(m, d)
        if b is not None:
            rec["back"] = b
        if d.get("requires"):
            rec["conditional"] = True
        return rec

    checks_in = collections.defaultdict(list)
    for name, (m, px, py) in check_px.items():
        checks_in[m].append((name, px, py))
    rooms, room_px = [], {}
    for m in sorted(home):
        scene = dd.scene(m)
        bw, bh = scene.w, scene.h
        fs = font_scale(bw, bh)
        strip = 10 * fs
        loc = 12 * fs
        r = names.get(m) or {}
        place = r.get("place", "").strip()
        solid = place and not place.startswith("(") and "placeholder" not in r.get("notes", "")
        name = place if solid else (r.get("area") or f"map {m}")
        obstacles = []
        for cname, px, py in sorted(checks_in.get(m, [])):
            x, y = int(px), strip + int(py)
            room_px[cname] = (m, x, y)
            obstacles.append((x - loc // 2, y - loc // 2, loc, loc))
        doorways, scene_only = ways[m]
        for _, _, marks in doorways:
            for s in marks:
                obstacles.append((s[0] - loc // 2, strip + s[1] - loc // 2, loc, loc))
        entries = []
        for k, (wid, ds, marks) in enumerate(doorways):
            text = letter(k)
            tw, th = text_box_size(text, fs)
            for s in marks:
                x, y = s[0], strip + s[1]
                bx, by = place_box(x, y, tw, th, (0, strip, bw, bh), obstacles, loc // 2 + fs)
                obstacles.append((bx - fs, by - fs, tw + 2 * fs, th + 2 * fs))
                e = {"letter": text, "id": wid, "x": x, "y": y, "box": [bx, by, tw, th], "how": s[3]}
                if not s[2]:
                    e["clamped"] = True
                e["to"] = [target(m, d) for d in ds]
                entries.append(e)
        room = {"path": f"images/rooms/{m}.png", "map": m, "name": name, "label": f"{m} {name}",
                "area": home[m], "width": bw, "height": strip + bh, "bg_y": strip, "font_scale": fs,
                "location_size": loc, "label_box": [0, 0, bw, strip]}
        if scene_only:
            room["scene_doors"] = [target(m, d) for d in scene_only]
        room["doors"] = entries
        rooms.append(room)
    groups = []
    for g in sorted(set(home.values())):
        ms = {m for m in home if home[m] == g}
        starts = entrances.get(g) or area_maps.get(g, [])[:1] or [min(ms)]
        dist = bfs({m: [n for n in adj.get(m, ()) if n in ms] for m in ms}, starts)
        groups.append({"area": g, "image": group_image.get(g), "world": group_world.get(g),
                       "entrances": sorted(starts), "rooms": sorted(ms, key=lambda m: (dist.get(m, 1 << 20), m))})
    return rooms, groups, room_px


# ---- Build ----------------------------------------------------------------------------------

def label_of(m, names):
    """The place's name (map_names.csv); for the door-graph placeholders, the area and map."""
    r = names.get(m) or {}
    place = r.get("place", "").strip()
    if not place or place.startswith("(") or "placeholder" in r.get("notes", ""):
        return f"{r.get('area') or 'map'} {m}" if r.get("area") else f"map {m}"
    return place


def build(disc_path):
    rows = place_rows()
    field = json.load(open(os.path.join(REPO, "docs", "ap", "field.json"), encoding="utf-8"))
    doors = json.load(open(os.path.join(REPO, "docs", "ap", "doors.json"), encoding="utf-8"))
    names = map_names()
    dd = DiscData(disc_path)
    points = check_points(rows, field, dd)
    adj, exits = door_graph(doors)

    area_maps = collections.defaultdict(set)
    for r in rows:
        if dd.scene(r["map"]) is not None:
            area_maps[r["area"]].add(r["map"])

    # World maps: one dot per world-map entrance group (data.KEY_GROUPS, the open world's area
    # keys) at the arrival point of the doors from the group's maps back to that world map
    # (the most used one; ties: the lowest map); each area belongs to the group most of its
    # locations name.
    data_py = os.path.join(REPO, "apworld", "so2", "data.py")
    groups = literal_table(data_py, "KEY_GROUPS")
    area_group = collections.defaultdict(collections.Counter)
    for r in literal_table(data_py, "LOCATIONS") + literal_table(data_py, "HUB_LOCATIONS"):
        if r.get("map") is not None and r.get("group"):
            area_group[r["area"]][r["group"]] += 1
    members = collections.defaultdict(list)
    for area in sorted(area_group):
        members[max(sorted(area_group[area]), key=lambda g: area_group[area][g])].append(area)
    group_of = {area: g for g, areas in members.items() for area in areas}
    entrance_maps = {g["group"]: g["maps"] for g in groups}

    images, checks, area_only = [], {}, {}
    rect_of = {}
    for area in sorted(area_maps):
        maps = order_maps(sorted(area_maps[area]), adj, exits, entrance_maps.get(group_of.get(area), []))
        sizes = [(dd.scene(m).w, dd.scene(m).h) for m in maps]
        s, rects, W, H = layout_area(sizes)
        path = f"images/maps/{slug(area)}.png"
        entries = []
        for m, (bw, bh), (x, y, w, h) in zip(maps, sizes, rects):
            entries.append({"map": m, "label": label_of(m, names), "x": x, "y": y, "w": w, "h": h,
                            "bg_w": bw, "bg_h": bh, "label_y": y - LABEL_H})
            rect_of[(area, m)] = (path, x, y, w, h, bw, bh)
        images.append({"path": path, "kind": "area", "area": area, "width": W, "height": H,
                       "scale": f"{s[0]}/{s[1]}", "maps": entries})

    check_px = {}
    for r in rows:
        m, x, y, z, how, corners = points[r["name"]]
        if how == "no-scene":
            area_only[r["name"]] = f"map {m} has no field background"
            continue
        path, rx, ry, w, h, bw, bh = rect_of[(r["area"], m)]
        px, py, inside = bg_pixel(dd.scene(m), x, y, z, how, corners)
        check_px[r["name"]] = (m, px, py)
        rec = {"image": path, "x": rx + int(px * w / bw), "y": ry + int(py * h / bh), "map": m, "how": how}
        if how not in ("map", "region"):
            rec["world"] = [round(x), round(y), round(z)]
        if not inside:
            rec["clamped"] = True
        checks[r["name"]] = rec

    dots = collections.defaultdict(list)
    for g in groups:
        world = g["world"]
        seen = collections.Counter()
        first = {}
        for m in sorted(g["maps"]):
            for (w, x, z) in exits.get(m, ()):
                if w == world:
                    seen[(x, z)] += 1
                    first.setdefault((x, z), m)
        if not seen:
            continue
        (x, z) = min(seen, key=lambda k: (-seen[k], first[k], k))
        x0, z0, upp, _, _ = WORLD_VIEWS[world]
        dots[world].append({"group": g["group"], "key": g["name"], "x": (x - x0) // upp,
                            "y": (z0 - z) // upp, "world": [x, z], "via_map": first[(x, z)],
                            "areas": members.get(g["group"], [])})
    for world in sorted(dots):
        x0, z0, upp, w, h = WORLD_VIEWS[world]
        images.append({"path": f"images/maps/world_{slug(WORLD_NAMES[world])}.png", "kind": "world",
                       "world_map": world, "name": WORLD_NAMES[world], "width": w, "height": h,
                       "x0": x0, "z0": z0, "units_per_pixel": upp, "dots": dots[world]})

    # Rooms (format 1.1): every field map an area's doors reach, one image each.
    image_maps = {i["area"]: [e["map"] for e in i["maps"]] for i in images if i["kind"] == "area"}
    group_image = {i["area"]: i["path"] for i in images if i["kind"] == "area"}
    group_world, entrance_of, extra = {}, {}, {}
    for img in images:
        if img["kind"] == "world":
            for dot in img["dots"]:
                for area in dot["areas"]:
                    if area in image_maps:
                        group_world.setdefault(area, img["world_map"])
    for area in image_maps:
        entrance_of[area] = entrance_maps.get(group_of.get(area), [])
    for g in groups:
        if not any(a in image_maps for a in members.get(g["group"], [])) and g["group"] not in image_maps:
            extra[g["group"]] = g["maps"]
            entrance_of[g["group"]] = g["maps"]
            group_world[g["group"]] = g["world"]
    home, entrances = room_groups(image_maps, entrance_of, extra, adj, doors, names, dd)
    rooms, room_groups_out, room_px = build_rooms(dd, names, doors, adj, home, entrances, image_maps, check_px,
                                                  group_world, group_image)
    for name, (m, x, y) in room_px.items():
        checks[name]["room"] = {"image": f"images/rooms/{m}.png", "x": x, "y": y}

    return {
        "format": FORMAT,
        "format_minor": FORMAT_MINOR,
        "source": "tools/ap_tracker_maps.py",
        "pack_uid": PACK_UID,
        "style": {"margin": MARGIN, "gap": GAP, "label_height": LABEL_H, "background": BACKGROUND,
                  "label_colour": LABEL_COLOUR, "dot": DOT, "door": DOOR_STYLE},
        "images": images,
        "rooms": rooms,
        "room_groups": room_groups_out,
        "checks": dict(sorted(checks.items())),
        "area_only": dict(sorted(area_only.items())),
    }


def dumps(data):
    """Stable JSON, one map / check / dot per line."""
    def one(v):
        return json.dumps(v, ensure_ascii=False, separators=(", ", ": "))
    out = ["{"]
    keys = list(data)
    for k_i, k in enumerate(keys):
        v = data[k]
        tail = "," if k_i < len(keys) - 1 else ""
        if k == "images":
            out.append(f' "{k}": [')
            for i_i, img in enumerate(v):
                inner = {kk: vv for kk, vv in img.items() if kk not in ("maps", "dots")}
                lst_key = "maps" if "maps" in img else "dots"
                head = one(inner)[:-1]
                out.append(f"  {head}, \"{lst_key}\": [")
                for e_i, e in enumerate(img[lst_key]):
                    out.append("   " + one(e) + ("," if e_i < len(img[lst_key]) - 1 else ""))
                out.append("  ]}" + ("," if i_i < len(v) - 1 else ""))
            out.append(" ]" + tail)
        elif k == "rooms":
            out.append(f' "{k}": [')
            for i_i, room in enumerate(v):
                head = one({kk: vv for kk, vv in room.items() if kk != "doors"})[:-1]
                end = "," if i_i < len(v) - 1 else ""
                if not room["doors"]:
                    out.append(f'  {head}, "doors": []}}{end}')
                    continue
                out.append(f'  {head}, "doors": [')
                for e_i, e in enumerate(room["doors"]):
                    out.append("   " + one(e) + ("," if e_i < len(room["doors"]) - 1 else ""))
                out.append("  ]}" + end)
            out.append(" ]" + tail)
        elif k == "room_groups":
            out.append(f' "{k}": [')
            for i_i, g in enumerate(v):
                out.append("  " + one(g) + ("," if i_i < len(v) - 1 else ""))
            out.append(" ]" + tail)
        elif k in ("checks", "area_only"):
            out.append(f' "{k}": {{')
            items = list(v.items())
            for c_i, (name, rec) in enumerate(items):
                out.append(f"  {one(name)}: {one(rec)}" + ("," if c_i < len(items) - 1 else ""))
            out.append(" }" + tail)
        else:
            out.append(f" {one(k)}: {one(v)}{tail}")
    out.append("}")
    return "\n".join(out) + "\n"


def summary(data):
    checks = data["checks"]
    how = collections.Counter(c["how"] for c in checks.values())
    clamped = sum(1 for c in checks.values() if c.get("clamped"))
    areas = [i for i in data["images"] if i["kind"] == "area"]
    worlds = [i for i in data["images"] if i["kind"] == "world"]
    big = max(areas, key=lambda i: max(i["width"], i["height"]))
    return (f"images: {len(areas)} areas ({sum(len(i['maps']) for i in areas)} map rectangles, largest "
            f"{big['area']} {big['width']}x{big['height']} at {big['scale']}), {len(worlds)} world maps "
            f"({sum(len(i['dots']) for i in worlds)} area dots); checks placed: {len(checks)} "
            f"({', '.join(f'{k} {v}' for k, v in sorted(how.items()))}; clamped {clamped}); "
            f"area-level only: {len(data['area_only'])}; " + room_summary(data))


def room_summary(data):
    rooms = data.get("rooms", [])
    marks = [e for r in rooms for e in r["doors"]]
    ways = {(r["map"], e["id"]) for r in rooms for e in r["doors"]}
    doors = {t["door"] for e in marks for t in e["to"]}
    scene = sum(len(r.get("scene_doors", [])) for r in rooms)
    backs = sum(1 for e in marks for t in e["to"] if "back" in t)
    pixels = sum(r["width"] * r["height"] for r in rooms)
    return (f"rooms: {len(rooms)} in {len(data.get('room_groups', []))} groups ({pixels / 1e6:.1f} M pixels), "
            f"{len(ways)} doorways ({len(marks)} markers, {sum(1 for e in marks if e.get('clamped'))} clamped; "
            f"{len(doors)} doors, {backs} with a way back) + {scene} scene transfers")


def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    p.add_argument("disc", nargs="?", default=os.environ.get("SS_DISC1"))
    p.add_argument("-o", "--output", default=os.path.join(REPO, "docs", "ap", "tracker_maps.json"))
    p.add_argument("--check", action="store_true", help="compare with the committed file, write nothing")
    a = p.parse_args()
    if not a.disc:
        raise SystemExit("give the NA disc 1 image (or set SS_DISC1)")
    data = build(a.disc)
    text = dumps(data)
    print(summary(data), file=sys.stderr)
    if a.check:
        old = open(a.output, encoding="utf-8").read() if os.path.exists(a.output) else ""
        if old != text:
            raise SystemExit(f"{a.output} is out of date: run tools/ap_tracker_maps.py")
        print(f"{a.output}: up to date", file=sys.stderr)
        return
    with open(a.output, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)


if __name__ == "__main__":
    main()
