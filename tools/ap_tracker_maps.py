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
  default spawn point; projected through the map's field camera (MapScene +0x08..+0x6a, the
  view actor_sys_setup_view builds) into background pixels, then into the image.

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
            except (SystemExit, KeyError, ValueError, struct.error):
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
    """(x, y, z, how) of what a registering instruction places: an actor's position (ext 0x70
    actor_create, 0xf3 actor_create with a pickpocket entry), a trigger region's centre (ext
    0x74 / 0x75 thread_start, 0xd8 / 0xd9 trigger_add_button; y = the scene's floor); None
    when its arguments are not constant or the instruction places nothing."""
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
            xs, zs = c[0::2], c[1::2]
            return ((min(xs) + max(xs)) / 2, scene.floor, (min(zs) + max(zs)) / 2, "region",
                    tuple(zip(xs, zs)))
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


def region_point(scene, corners, y):
    """The middle of the visible part of a trigger region (its corners projected on the
    floor, clipped to the background), or None when none of it is visible."""
    pts = [scene.project(x, y, z) for x, z in corners]
    if any(p is None for p in pts):
        return None
    poly = clip(hull(pts), scene.w - 1, scene.h - 1)
    if len(poly) < 3:
        return None
    return centroid(poly)


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

    for r in rows:
        m, x, y, z, how, corners = points[r["name"]]
        if how == "no-scene":
            area_only[r["name"]] = f"map {m} has no field background"
            continue
        path, rx, ry, w, h, bw, bh = rect_of[(r["area"], m)]
        p = dd.scene(m).project(x, y, z) if how != "map" else (bw / 2, bh / 2)
        inside = p is not None and 0 <= p[0] < bw and 0 <= p[1] < bh
        if not inside and corners:
            q = region_point(dd.scene(m), corners, y)
            if q is not None:
                p, inside = q, True
        if p is None:
            p = (bw / 2, bh / 2)
        px = min(max(p[0], 0), bw - 1)
        py = min(max(p[1], 0), bh - 1)
        rec = {"image": path, "x": rx + int(px * w / bw), "y": ry + int(py * h / bh), "map": m, "how": how}
        if how != "map":
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

    return {
        "format": FORMAT,
        "source": "tools/ap_tracker_maps.py",
        "pack_uid": PACK_UID,
        "style": {"margin": MARGIN, "gap": GAP, "label_height": LABEL_H, "background": BACKGROUND,
                  "label_colour": LABEL_COLOUR, "dot": DOT},
        "images": images,
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
            f"area-level only: {len(data['area_only'])}")


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
