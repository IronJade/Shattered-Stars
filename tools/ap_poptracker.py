#!/usr/bin/env python3
"""The PopTracker pack for so2.apworld (docs/ap/poptracker.md; docs/plans/trackers.md B4).

    SKIP_REQUIREMENTS_UPDATE=1 scratchpad/ap/ap-venv/bin/python tools/ap_poptracker.py \\
        [--out dist/poptracker/so2_shattered_stars] [--zip dist/poptracker/so2_shattered_stars_1.0.0.zip] \\
        [--ap-dir scratchpad/ap/Archipelago] [--versions-url URL] [--no-images]

Writes the pack folder (manifest, items, locations, maps, layouts, the generated Lua tables and
every image) and, with --zip, the pack as a zip with fixed timestamps (the same inputs give the
same bytes, so the sha256 in versions.json is reproducible). tools/build_poptracker.sh runs it.

Inputs, all from this repo:
- the APWorld's own tables (apworld/so2: items, locations.ALL_ROWS, data.KEY_GROUPS /
  HUB_DUNGEONS / STAGES, skillsanity, options), imported as a bare package so the world class
  and the other worlds are never loaded. Archipelago's BaseClasses / Options come from --ap-dir.
- docs/ap/tracker_maps.json: the shared map layout (image paths and sizes, map rectangles, a pixel
  for every place check, the world-map dots). The port's --render-tracker-maps draws the player's
  own disc into the same images, so the placeholders here are exactly the layout's sizes and sit at
  the pack's root paths (tracker_maps.md "For the pack").
- docs/modding/reference/map_names.csv: field map -> tab (scripts/map_tabs.lua).
- tools/poptracker/: the hand-written Lua (init, so2, logic, autotracking), copied as they are.

Every image is drawn here from plain shapes and text (Pillow; the font is Pillow's bundled Aileron
Regular, CC0). Nothing comes from the discs.

Also writes, beside the pack folder, ut_name_mapping.json: "<location>/<section>" -> AP location id
for every section whose name is not the AP location name (what Universal Tracker's
`poptracker_name_mapping` would need to read this pack directly), and ut_tracker/: Universal
Tracker's map-tab JSON (docs/ap/apworld.md "Universal Tracker's map tab"), which `--ut-out
apworld/so2/tracker` also writes into the APWorld and `--ut-check DIR` compares.
"""
import argparse
import csv
import importlib
import io
import json
import math
import os
import re
import shutil
import sys
import types
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
LUA_SRC = os.path.join(HERE, "poptracker")

PACK_UID = "so2_shattered_stars"
PACK_VERSION = "1.0.0"
POPTRACKER_VERSION = "0.36.0"
GAME = "Star Ocean: The Second Story"
AUTHOR = "DreamGlyph Studios"
# Where PopTracker looks for updates (docs/ap/poptracker.md "Distribution": a proposal until the
# coordinator / the user settles it; --versions-url overrides).
VERSIONS_URL = "https://raw.githubusercontent.com/IronJade/Shattered-Stars/main/tools/poptracker/versions.json"
VARIANT_MAP = "map"
VARIANT_ITEMS = "items_only"

REGIONS = ("Expel", "Energy Nede", "Fienal", "Cave of Trials")
PLACE_TYPES = ("chest", "gift", "boss", "join")
# The boards: the checks that are not places, one tab each (trackers.md 3.4).
BOARDS = (
    # key, top-level location / tab title, map name, row types
    ("shops", "Shops", "board_shops", ("shop",)),
    ("crafts", "Item Creation", "board_crafts", ("craft",)),
    ("killers", "Killer Moves", "board_killer_moves", ("killer_move",)),
    ("skills", "Guilds and Learning", "board_skills", ("guild", "learn")),
    ("drops", "Enemy Drops", "board_drops", ("drop",)),
)
CRAFT_CELL = 25         # crafts per Item Creation cell (Cooking has 104)

# The slot-data options shown as "setting" items (the Room tab). Choices take their values from
# the option class; `absent` is the value when the key is missing (bench, hell_seed: only written
# when on). PopTracker codes: opt_<key>_<value> for a choice's stages, opt_<key> for a toggle.
SETTINGS = (
    ("world_mode", "Mode"), ("goal", "Goal"), ("story_route", "Route"), ("shuffle_shops", "Shops"),
    ("missable_locations", "Missables"), ("enemy_dropsanity", "Drops"), ("skillsanity", "Skills"),
    ("killer_move_progression", "Killer prog."), ("fienal_requirement", "Fienal"),
    ("character_items", "Characters"), ("craftsanity", "Crafts"), ("killer_move_checks", "Killer moves"),
    ("boss_checks", "Bosses"), ("cave_of_trials", "Cave of Trials"), ("expel_teleporter", "Teleporter"),
    ("expel_progression", "Expel prog."), ("bench", "Bench"), ("hell_seed", "Hell seed"),
)
SETTING_ABSENT = {"bench": False, "hell_seed": False}
SETTING_VALUE_TEXT = {"indalecio": "Indalecio", "indalecio_limiter_off": "Limiter Off",
                      "indalecio_limiter_off_and_iseria_queen": "Iseria Queen", "open_world": "Open World",
                      "first_drop": "First drop", "skills_only": "Skills only", "special_only": "Specials only",
                      "fields_and_berle": "Fields + Berle", "all_bosses": "All bosses"}

# Badge colours (category -> fill); text is white.
COLOURS = {
    "key_expel": "#2f5f96", "key_nede": "#6a3f94", "dkey": "#93582c", "char": "#2f7f4c", "skill": "#7d6a1e",
    "move": "#8f2f3f", "trap": "#4f4f57", "opt": "#24606e", "status": "#3a3a46", "logic_off": "#55555f",
    "logic_list": "#8a6a16", "logic_on": "#2f7f4c",
}
BG = "#1c1c22"
BADGE = (160, 60)       # badge image size: drawn at twice the layout's item size (80 x 30)
ITEM_SIZE = "80,30"
# The live door panel (docs/ap/poptracker.md "Rooms and doors"): one Lua item per doorway, a bar
# with its line ("A -> 412 Salva Drift") as the badge text.
DOOR_LINE = (260, 22)                   # the panel item's size in the layout
DOOR_LINE_IMG = "images/doors/line.png"
DOOR_ICON_IMG = "images/doors/door.png"  # a door section's icon in the map popup
DOOR_PANEL_HEADER = "Doors (* story)"
PLAYER_IMG = "images/status/you.png"    # Universal Tracker's player marker on the world maps
WORLD_MAP_NAMES = {1: "Expel", 2: "Energy Nede", 3: "world map 3"}


# ---------------------------------------------------------------------------------------- inputs

def load_world(ap_dir):
    """apworld/so2's table modules as the package `so2`, without its __init__ (the world class,
    which would load every world through worlds.AutoWorld)."""
    ap_dir = os.path.abspath(ap_dir)
    if not os.path.isfile(os.path.join(ap_dir, "BaseClasses.py")):
        raise SystemExit(f"{ap_dir}: no Archipelago here (BaseClasses.py); pass --ap-dir")
    sys.path.insert(0, ap_dir)
    pkg = types.ModuleType("so2")
    pkg.__path__ = [os.path.join(REPO, "apworld", "so2")]
    sys.modules["so2"] = pkg
    mods = types.SimpleNamespace()
    for name in ("data", "items", "locations", "skillsanity", "options"):
        setattr(mods, name, importlib.import_module(f"so2.{name}"))
    return mods


def slug(text):
    """tracker_maps.md's slug: lower case, apostrophes dropped, other non-alphanumeric runs -> _."""
    return re.sub(r"[^a-z0-9]+", "_", text.lower().replace("'", "")).strip("_")


def clean(name):
    """A PopTracker location or section name: '/' separates the path, so it can't be in a name."""
    return name.replace("/", "-")


def natural(text):
    return [int(t) if t.isdigit() else t for t in re.split(r"(\d+)", text)]


# ---------------------------------------------------------------------------------------- images

def font(size):
    from PIL import ImageFont
    return ImageFont.load_default(size=size)


def text_w(draw, text, f):
    return draw.textlength(text, font=f)


def wrap(draw, text, f, width):
    lines, line = [], ""
    for word in text.split():
        trial = f"{line} {word}".strip()
        if line and text_w(draw, trial, f) > width:
            lines.append(line)
            line = word
        else:
            line = trial
    if line:
        lines.append(line)
    return lines


def fit(draw, text, width, height, start, low=8, max_lines=2):
    """The largest font (start down to low) whose wrapped lines fit in width x height."""
    size = start
    while True:
        f = font(size)
        lines = wrap(draw, text, f, width)
        tall = len(lines) * size * 1.15
        if (len(lines) <= max_lines and tall <= height and all(text_w(draw, ln, f) <= width for ln in lines)) \
                or size <= low:
            return f, size, lines[:max_lines]
        size -= 1


def cut(draw, text, f, width):
    """tracker_maps.md's label rule: cut with '..' to fit."""
    if text_w(draw, text, f) <= width:
        return text
    while text and text_w(draw, text + "..", f) > width:
        text = text[:-1]
    return text + ".."


def centred_text(draw, box, lines, f, size, fill="#ffffff", outline=None):
    x0, y0, x1, y1 = box
    line_h = size * 1.15
    y = (y0 + y1) / 2 - line_h * len(lines) / 2
    for ln in lines:
        w = text_w(draw, ln, f)
        x = (x0 + x1) / 2 - w / 2
        if outline:
            for dx, dy in ((-1, 0), (1, 0), (0, -1), (0, 1)):
                draw.text((x + dx, y + dy), ln, font=f, fill=outline)
        draw.text((x, y), ln, font=f, fill=fill)
        y += line_h


def badge(text, colour, size=BADGE, dim=False):
    """An item badge: a rounded tile in the category colour with the short name."""
    from PIL import Image, ImageDraw
    w, h = size
    img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle((1, 1, w - 2, h - 2), radius=h // 5, fill=colour, outline="#e8e8f0", width=2)
    f, fs, lines = fit(d, text, w - 14, h - 8, start=int(h * 0.42))
    centred_text(d, (0, 0, w, h), lines, f, fs)
    return img


def png_bytes(img):
    buf = io.BytesIO()
    img.save(buf, "PNG", optimize=True)
    return buf.getvalue()


def map_colour(k):
    """The placeholder rectangles' fills: muted slate tones, alternating so neighbours differ."""
    tones = ("#2b3442", "#33302b", "#2b3a33", "#3a2b36", "#2d3140", "#38342a")
    return tones[k % len(tones)]


def draw_area(image, style):
    """An area placeholder: every field map's rectangle as a labelled box, exactly where the
    port draws the disc's background (tracker_maps.md "Area images")."""
    from PIL import Image, ImageDraw
    img = Image.new("RGB", (image["width"], image["height"]), style["background"])
    d = ImageDraw.Draw(img)
    label_f = font(12)
    for k, m in enumerate(image["maps"]):
        x, y, w, h = m["x"], m["y"], m["w"], m["h"]
        d.rectangle((x, y, x + w - 1, y + h - 1), fill=map_colour(k), outline="#5a6274")
        # grid lines every 40 px: something to read the dots' places against
        for gx in range(x + 40, x + w - 1, 40):
            d.line((gx, y + 1, gx, y + h - 2), fill="#3a4250")
        for gy in range(y + 40, y + h - 1, 40):
            d.line((x + 1, gy, x + w - 2, gy), fill="#3a4250")
        d.text((x + 2, m["label_y"] + 2), cut(d, m["label"], label_f, w - 4), font=label_f,
               fill=style["label_colour"])
        f, fs, lines = fit(d, f"map {m['map']}", w - 16, h / 3, start=min(28, max(10, h // 8)))
        centred_text(d, (x, y, x + w, y + h * 0.45), lines, f, fs, fill="#8c94a8")
        if w >= 200 and h >= 120:
            hint = "placeholder: draw it from your own disc with shattered-stars --render-tracker-maps"
            f, fs, lines = fit(d, hint, w - 24, h / 4, start=12, low=8, max_lines=3)
            centred_text(d, (x, y + h * 0.6, x + w, y + h * 0.9), lines, f, fs, fill="#6c7488")
    return img


def draw_room(room, style):
    """A room placeholder (tracker_maps.md "Rooms"): exactly the layout's size, the label strip,
    a flat box where the port draws the background, and each door's letter box where the port
    draws it (the same boxes, so a player's render lines up). Original: shapes and text only."""
    from PIL import Image, ImageDraw
    w, h, top, fs = room["width"], room["height"], room["bg_y"], room["font_scale"]
    img = Image.new("RGB", (w, h), style["background"])
    d = ImageDraw.Draw(img)
    d.rectangle((0, top, w - 1, h - 1), fill=map_colour(room["map"]), outline="#5a6274")
    f, fs_, lines = fit(d, f"map {room['map']}", w - 16, (h - top) / 4, start=min(40, max(10, (h - top) // 6)))
    centred_text(d, (0, top, w, top + (h - top) * 0.5), lines, f, fs_, fill="#8c94a8")
    if w >= 240 and h - top >= 160:
        hint = "placeholder: draw it from your own disc with shattered-stars --render-tracker-maps"
        f, fs_, lines = fit(d, hint, w - 24, (h - top) / 5, start=10 + 2 * fs, low=8, max_lines=3)
        centred_text(d, (0, top + (h - top) * 0.55, w, top + (h - top) * 0.85), lines, f, fs_, fill="#6c7488")
    lx, ly, lw, lh = room["label_box"]
    label_f = font(max(8, 9 * fs))
    d.text((lx + 2 * fs, ly + max(0, (lh - 9 * fs) // 2)), cut(d, room["label"], label_f, lw - 4 * fs),
           font=label_f, fill=style["label_colour"])
    door = style["door"]
    letter_f = font(max(8, 9 * fs))
    for e in room["doors"]:
        bx, by, bw, bh = e["box"]
        d.rectangle((bx, by, bx + bw - 1, by + bh - 1), fill=door["fill"], outline=door["outline"], width=fs)
        tw = text_w(d, e["letter"], letter_f)
        d.text((bx + (bw - tw) / 2, by + (bh - 9 * fs) / 2 - fs / 2), e["letter"], font=letter_f, fill=door["text"])
    keep = [style["background"], style["label_colour"], door["fill"], door["text"], door["outline"],
            map_colour(room["map"])]
    return to_palette(img, keep)


def to_palette(img, keep, colours=32):
    """A 32-colour palette image (a room placeholder is shapes and text: a third of the bytes):
    the layout's style colours exactly, and the rest (the text's edges) chosen from the image."""
    from PIL import Image
    fixed = [tuple(int(h[k:k + 2], 16) for k in (1, 3, 5)) for h in dict.fromkeys(keep)]
    rest = img.quantize(colors=colours - len(fixed), method=Image.Quantize.MEDIANCUT).convert("RGB")
    extra = [c for _, c in sorted(rest.getcolors(colours) or [], reverse=True)
             if all(sum((a - b) ** 2 for a, b in zip(c, f)) > 24 ** 2 for f in fixed)]
    pal = [v for c in (fixed + extra)[:colours] for v in c]
    pal_img = Image.new("P", (1, 1))
    pal_img.putpalette(pal + pal[:3] * (256 - len(pal) // 3))
    return img.quantize(palette=pal_img, dither=Image.Dither.NONE)


def draw_door_line():
    """The door panel's bar: Lua sets its text (the badge) at run time."""
    from PIL import Image, ImageDraw
    w, h = DOOR_LINE[0] * 2, DOOR_LINE[1] * 2
    img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    ImageDraw.Draw(img).rounded_rectangle((1, 1, w - 2, h - 2), radius=8, fill="#24303f", outline="#5a6274", width=2)
    return img


def draw_door_icon():
    """A door, for the door sections in the map popup."""
    from PIL import Image, ImageDraw
    img = Image.new("RGBA", (64, 64), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle((14, 6, 50, 60), radius=6, fill="#8a6a3c", outline="#e8e8f0", width=3)
    d.rectangle((20, 12, 44, 30), outline="#5c4526", width=2)
    d.rectangle((20, 36, 44, 54), outline="#5c4526", width=2)
    d.ellipse((40, 33, 46, 39), fill="#e8e8f0")
    return img


def draw_player_marker():
    """The party's place on a world map (Universal Tracker's location icon)."""
    from PIL import Image, ImageDraw
    img = Image.new("RGBA", (64, 64), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.ellipse((6, 6, 58, 58), fill="#ffffff")
    d.ellipse((12, 12, 52, 52), fill="#e0302a")
    d.ellipse((24, 24, 40, 40), fill="#ffffff")
    return img


def draw_world(image):
    """A world schematic: the arrival points of the area groups (doors.json arrivals) as labelled
    marks on a plain sea, north up. Original: positions are numbers, the rest is geometry."""
    from PIL import Image, ImageDraw
    w, h = image["width"], image["height"]
    img = Image.new("RGB", (w, h), "#122236")
    d = ImageDraw.Draw(img)
    if image["world_map"] == 2:
        # Energy Nede: the playable disc (radius 0x5cf8 around (67000, 40000))
        cx = (67000 - image["x0"]) / image["units_per_pixel"]
        cy = (image["z0"] - 40000) / image["units_per_pixel"]
        r = 0x5CF8 / image["units_per_pixel"]
        d.ellipse((cx - r, cy - r, cx + r, cy + r), fill="#1a3350", outline="#3c5a7c", width=3)
    step = 128
    for gx in range(0, w, step):
        d.line((gx, 0, gx, h), fill="#1d3049")
    for gy in range(0, h, step):
        d.line((0, gy, w, gy), fill="#1d3049")
    title_f = font(36)
    d.text((24, 18), f"{image['name']} (schematic)", font=title_f, fill="#e8e8f0")
    note_f = font(20)
    d.text((24, 62), "Placeholder: draw the real map from your own disc with "
                     "shattered-stars --render-tracker-maps", font=note_f, fill="#9aa8bc")
    # north arrow
    ax, ay = w - 70, 120
    d.polygon(((ax, ay - 50), (ax - 16, ay), (ax + 16, ay)), fill="#9aa8bc")
    d.text((ax - 9, ay + 6), "N", font=font(28), fill="#9aa8bc")
    label_f = font(22)
    circles = [(dot["x"] - 30, dot["y"] - 30, dot["x"] + 30, dot["y"] + 30) for dot in image["dots"]]
    for c in circles:
        d.ellipse(c, outline="#5f7896", width=2)
    placed = []

    def free(box, own):
        return (box[0] >= 4 and box[2] <= w - 4 and box[1] >= 90 and box[3] <= h - 4
                and not any(overlap(box, o) for o in placed)
                and not any(overlap(box, c) for k, c in enumerate(circles) if k != own))

    for k, dot in enumerate(image["dots"]):
        name = dot.get("title")
        if not name:        # a group without place checks (map 231, L'Aqua): an unlabelled ring
            continue
        x, y = dot["x"], dot["y"]
        tw, th = text_w(d, name, label_f), 26
        spots = [(x + 34, y - 13), (x - 34 - tw, y - 13), (x - tw / 2, y - 34 - th), (x - tw / 2, y + 34),
                 (x + 26, y - 40), (x + 26, y + 16), (x - 26 - tw, y - 40), (x - 26 - tw, y + 16)]
        tx, ty = next(((sx, sy) for sx, sy in spots if free((sx, sy, sx + tw, sy + th), k)), spots[0])
        placed.append((tx, ty, tx + tw, ty + th))
        for dx, dy in ((-2, 0), (2, 0), (0, -2), (0, 2)):
            d.text((tx + dx, ty + dy), name, font=label_f, fill="#0a1420")
        d.text((tx, ty), name, font=label_f, fill="#e8e8f0")
    return img


def overlap(a, b):
    return a[0] < b[2] and b[0] < a[2] and a[1] < b[3] and b[1] < a[3]


CELL_W, CELL_H, CELL_GAP, BOARD_MARGIN, BOARD_TOP = 330, 46, 8, 16, 76


def board_geometry(n):
    cols = max(1, min(n, math.ceil(math.sqrt(n * 0.22))))
    rows = math.ceil(n / cols)
    width = BOARD_MARGIN * 2 + cols * CELL_W + (cols - 1) * CELL_GAP
    height = BOARD_TOP + BOARD_MARGIN + rows * CELL_H + (rows - 1) * CELL_GAP
    return cols, width, height


def cell_box(k, cols):
    c, r = k % cols, k // cols
    x = BOARD_MARGIN + c * (CELL_W + CELL_GAP)
    y = BOARD_TOP + r * (CELL_H + CELL_GAP)
    return x, y


def draw_board(title, cells, colour):
    """A check board: one labelled cell per location; PopTracker draws its square at the cell's
    left end (cell_box + (24, CELL_H / 2))."""
    from PIL import Image, ImageDraw
    cols, width, height = board_geometry(len(cells))
    img = Image.new("RGB", (width, height), BG)
    d = ImageDraw.Draw(img)
    d.text((BOARD_MARGIN, 14), title, font=font(30), fill="#e8e8f0")
    d.text((BOARD_MARGIN, 50), "click a square for its checks", font=font(15), fill="#8c94a8")
    for k, cell in enumerate(cells):
        x, y = cell_box(k, cols)
        d.rounded_rectangle((x, y, x + CELL_W - 1, y + CELL_H - 1), radius=8, fill=colour, outline="#5a6274")
        f, fs, lines = fit(d, cell["label"], CELL_W - 60, CELL_H - 6, start=17, low=9)
        line_h = fs * 1.15
        ty = y + CELL_H / 2 - line_h * len(lines) / 2
        for ln in lines:
            d.text((x + 50, ty), ln, font=f, fill="#e8e8f0")
            ty += line_h
    return img


# ---------------------------------------------------------------------------------------- the pack

class Pack:
    def __init__(self, w, layout, map_names):
        self.w, self.layout, self.map_names = w, layout, map_names
        self.files = {}             # pack path -> bytes
        self.items = []             # items/items.json
        self.item_mapping = {}      # AP item id -> [(code, type)]
        self.location_mapping = {}  # AP location id -> "@path/section"
        self.ut_names = {}          # "<location>/<section>" -> AP location id (names that differ)
        self.section_of = {}        # AP location id -> (location path, section name)
        self.images = {}            # pack path -> PIL image (drawn at the end)
        self.tabs = {}              # area name -> tab path (list of titles)
        self.badges = {}            # image path -> (text, colour)
        self.world_tab = {}         # world map 1 / 2 -> its tab path
        # format 1.1: rooms (one map each), their groups, the doorways (tracker_maps.md "Rooms")
        self.rooms = {r["map"]: r for r in layout.get("rooms", [])}
        self.room_groups = {g["area"]: g for g in layout.get("room_groups", [])}
        self.room_group_of = {m: g["area"] for g in layout.get("room_groups", []) for m in g["rooms"]}
        self.room_tabs = {}         # room map -> its tab path
        self.doorways = {}          # doorway id -> {map, letter, code, marks, to}
        self.room_doorways = {}     # room map -> doorway ids in letter order
        self.door_sections = {}     # section full id -> doorway id

    # ---- files
    def put_json(self, path, obj):
        self.files[path] = (json.dumps(obj, indent=1, ensure_ascii=False) + "\n").encode("utf-8")

    def put_text(self, path, text):
        self.files[path] = text.encode("utf-8")

    # ---- items
    def item(self, name, code, kind, text, colour, ap_id=None, **extra):
        img = f"images/items/{code}.png"
        self.badges[img] = (text, colour)
        entry = {"name": name, "type": kind, "img": img, "codes": code}
        entry.update(extra)
        self.items.append(entry)
        if ap_id is not None:
            self.item_mapping.setdefault(ap_id, []).append((code, kind))
        return code

    def build_items(self):
        w = self.w
        D, I, S = w.data, w.items, w.skillsanity
        self.keys = {1: [], 2: []}
        for g in D.KEY_GROUPS:
            text = g["name"][:-5] if g["name"].endswith(" Pass") else g["name"]
            code = self.item(g["name"], "key_" + slug(g["name"]), "toggle", text,
                             COLOURS["key_expel" if g["world"] == 1 else "key_nede"], I.ITEM_NAME_TO_ID[g["name"]])
            self.keys[g["world"]].append(code)
        self.dungeon_keys = []
        for code_id, g in sorted(I.HUB_KEY_OF.items()):
            self.dungeon_keys.append(self.item(g["name"], "dkey_" + slug(g["dungeon"]), "toggle", g["dungeon"],
                                               COLOURS["dkey"], code_id))
        self.characters = []
        for who in I.CHARACTER_NAMES:
            self.characters.append(self.item(who, "char_" + slug(who), "toggle", who, COLOURS["char"],
                                             I.ITEM_NAME_TO_ID[who]))
        self.traps = []
        for name, code_id in I.TRAP_IDS.items():
            self.traps.append(self.item(name, "trap_" + slug(name.replace(" Trap", "")), "consumable",
                                        name.replace(" Trap", "") + " trap", COLOURS["trap"], code_id,
                                        min_quantity=0, max_quantity=-1))
        self.skills = []
        for name, code_id in sorted(S.SKILL_ITEMS.items(), key=lambda kv: kv[1]):
            short = name.split(": ", 1)[1]
            self.skills.append(self.item(name, "skill_" + slug(short), "toggle", short, COLOURS["skill"], code_id))
        self.moves = {}
        for code_id, (who, index) in sorted(S.MOVE_OF.items(), key=lambda kv: (I.PARTY_ID[kv[1][0]], kv[1][1])):
            name = next(n for n, c in S.MOVE_ITEMS.items() if c == code_id)
            short = name.split(": ", 1)[1]
            self.moves.setdefault(who, []).append(
                self.item(name, "move_" + slug(name), "toggle", short, COLOURS["move"], code_id))
        # status: the logic banner, the story chapter, story awareness, auto tab switching, the refresh counter
        self.status_logic = "so2_logic"
        stages = []
        for key, text, colour in (("offline", "Logic: not connected (checklist)", "logic_off"),
                                  ("checklist", "Logic unavailable for this room: checklist", "logic_list"),
                                  ("on", "Logic: from the room", "logic_on"),
                                  ("newer", "Logic needs a newer pack: checklist", "logic_list")):
            img = f"images/status/logic_{key}.png"
            self.images[img] = badge(text, COLOURS[colour], size=(512, 48))
            stages.append({"img": img, "codes": f"so2_logic_{key}", "inherit_codes": False,
                           "name": text})
        self.items.append({"name": "Logic", "type": "progressive", "allow_disabled": False, "loop": True,
                           "stages": stages, "initial_stage_idx": 0})
        self.item("Story chapter (from the game)", "so2_chapter", "consumable", "Chapter", COLOURS["status"],
                  min_quantity=0, max_quantity=99)
        self.item("Story awareness: checks later in the story show blue (story mode)", "so2_story_aware", "toggle",
                  "Story", COLOURS["opt"], initial_active_state=True)
        self.item("Switch map tabs with the game", "so2_auto_tab", "toggle", "Auto tab", COLOURS["opt"],
                  initial_active_state=True)
        self.item("Refresh (internal)", "so2_rev", "consumable", "rev", COLOURS["status"], min_quantity=0,
                  max_quantity=-1)
        self.build_settings()

    def build_settings(self):
        from Options import Choice
        import typing
        hints = typing.get_type_hints(self.w.options.SO2Options)
        self.settings = []      # for pack_data.lua
        self.setting_codes = []
        for key, label in SETTINGS:
            cls = hints.get(key)    # hell_seed is derived (hell.is_hell_seed), not an option
            if cls is not None and issubclass(cls, Choice):
                values = [v for _, v in sorted(cls.name_lookup.items())]
                stages = []
                for v in values:
                    text = f"{label}: {SETTING_VALUE_TEXT.get(v, v.replace('_', ' ').capitalize())}"
                    img = f"images/items/opt_{key}_{v}.png"
                    self.badges[img] = (text, COLOURS["opt"])
                    stages.append({"img": img, "codes": f"opt_{key}_{v}", "inherit_codes": False, "name": text})
                default = cls.name_lookup[cls.default]
                self.items.append({"name": cls.display_name, "type": "progressive", "allow_disabled": False,
                                   "loop": True, "stages": stages, "initial_stage_idx": values.index(default)})
                self.settings.append({"key": key, "code": f"opt_{key}_{values[0]}", "kind": "choice",
                                      "values": values, "default": default})
                self.setting_codes.append(f"opt_{key}_{values[0]}")
            else:
                default = bool(cls.default) if cls is not None else False
                self.item(cls.display_name if cls is not None else label, f"opt_{key}", "toggle", label,
                          COLOURS["opt"], initial_active_state=default)
                entry = {"key": key, "code": f"opt_{key}", "kind": "toggle", "default": default}
                if key in SETTING_ABSENT:
                    entry["absent"] = SETTING_ABSENT[key]
                self.settings.append(entry)
                self.setting_codes.append(f"opt_{key}")

    # ---- locations
    def section(self, path, loc_name, row, short):
        """A section for one AP location: access through the logic seam, visible when the room has it."""
        lid = row["id"]
        name = clean(short)
        if lid in self.section_of:
            raise SystemExit(f"location {row['name']} placed twice")
        self.section_of[lid] = (path, name)
        self.location_mapping[lid] = f"@{path}/{name}"
        if name != row["name"]:
            key = f"{loc_name}/{name}"
            if key in self.ut_names:
                raise SystemExit(f"two sections map to {key!r}")
            self.ut_names[key] = lid
        return {"name": name, "access_rules": [f"^$so2_access|{lid}"], "visibility_rules": [f"$so2_exists|{lid}"]}

    def build_places(self):
        """Region -> area -> one location per dot (checks sharing a pixel share a location)."""
        L = self.w.locations
        checks = self.layout["checks"]
        area_images = [i for i in self.layout["images"] if i["kind"] == "area"]
        image_of = {i["path"]: i for i in area_images}
        self.map_of_image = {i["path"]: os.path.splitext(os.path.basename(i["path"]))[0]
                             for i in self.layout["images"]}
        rows = [r for r in L.ALL_ROWS if r["type"] in PLACE_TYPES]
        missing = [r["name"] for r in rows if r["name"] not in checks]
        if missing:
            raise SystemExit(f"{len(missing)} place checks have no dot in tracker_maps.json: {missing[:3]}")
        by_area = {}
        for r in rows:
            c = checks[r["name"]]
            img = image_of[c["image"]]
            by_area.setdefault(img["area"], {}).setdefault((c["x"], c["y"]), []).append(r)
        self.region_of_area = {}
        tree = {reg: [] for reg in REGIONS}
        self.area_sections = {}     # area -> [(AP location name, section path)] for the world dots' refs
        used_names = set()
        for img in area_images:
            area = img["area"]
            dots = by_area.get(area, {})
            regions = {r["region"] for rs in dots.values() for r in rs}
            region = sorted(regions, key=REGIONS.index)[0]
            self.region_of_area[area] = region
            area_path = f"{region}/{clean(area)}"
            children = []
            for (x, y), rs in sorted(dots.items(), key=lambda kv: (kv[1][0]["id"])):
                rs = sorted(rs, key=lambda r: r["id"])
                shorts = [self.short_place(r, area) for r in rs]
                loc_name = clean(f"{area}: {shorts[0]}" + (f" (+{len(rs) - 1})" if len(rs) > 1 else ""))
                base, k = loc_name, 2
                while loc_name in used_names:
                    loc_name = f"{base} #{k}"
                    k += 1
                used_names.add(loc_name)
                path = f"{area_path}/{loc_name}"
                secs = [self.section(path, loc_name, r, s) for r, s in zip(rs, shorts)]
                self.area_sections.setdefault(area, []).extend(
                    (clean(r["name"]), f"{path}/{s['name']}") for r, s in zip(rs, secs))
                spots = [{"map": self.map_of_image[img["path"]], "x": x, "y": y}]
                for r in rs:        # the same dot on its room (format 1.1)
                    room = checks[r["name"]].get("room")
                    if room:
                        spot = {"map": room_map(room["image"]), "x": room["x"], "y": room["y"]}
                        if spot not in spots:
                            spots.append(spot)
                children.append({"name": loc_name, "sections": secs, "map_locations": spots})
            tree[region].append({"name": clean(area), "children": children})
        return [{"name": reg, "children": tree[reg]} for reg in REGIONS if tree[reg]]

    def build_doors(self):
        """One location per doorway on its room (a square per marker: two trigger regions of one
        routine are one doorway), one section "Door <letter>" that is no Archipelago location:
        clicking it opens the destination's tab (scripts/doors.lua), and the section is set back
        at once, so a door never counts as checked. Its access rule is the door's colour."""
        children = []
        for m in sorted(self.rooms):
            room = self.rooms[m]
            ids = []
            for e in room["doors"]:
                d = self.doorways.get(e["id"])
                if d is None:
                    d = self.doorways[e["id"]] = {"map": m, "letter": e["letter"], "marks": [],
                                                  "code": f"door_{m}_{e['letter'].lower()}", "to": e["to"]}
                    ids.append(e["id"])
                if (d["map"], d["letter"]) != (m, e["letter"]):
                    raise SystemExit(f"doorway {e['id']} is in two rooms")
                d["marks"].append((e["x"], e["y"]))
            self.room_doorways[m] = ids
            for wid in ids:
                d = self.doorways[wid]
                name, sec = f"{m} {d['letter']}", f"Door {d['letter']}"
                self.door_sections[f"Doors/{name}/{sec}"] = wid
                children.append({
                    "name": name,
                    "sections": [{"name": sec, "item_count": 1, "access_rules": [f"^$so2_door_access|{wid}"],
                                  "chest_unopened_img": DOOR_ICON_IMG, "chest_opened_img": DOOR_ICON_IMG}],
                    "map_locations": [{"map": f"room_{m}", "x": x, "y": y, "shape": "diamond"}
                                      for x, y in d["marks"]]})
        return [{"name": "Doors", "children": children}]

    @staticmethod
    def short_place(row, area):
        name = row["name"]
        prefix = f"{area}: "
        return name[len(prefix):] if name.startswith(prefix) else name

    def build_world(self):
        """The world maps: one location per area group's dot, its sections refs to the place checks
        of the group's areas (so a dot shows the state of everything behind that entrance)."""
        out = []
        self.world_titles = {}
        for img in self.layout["images"]:
            if img["kind"] != "world":
                continue
            children = []
            for dot in img["dots"]:
                areas = [a for a in dot["areas"] if a in self.area_sections]
                if not areas:
                    continue
                title = self.group_title(dot["group"], areas)
                dot["title"] = title
                # a ref still needs a name unique in its location (PopTracker warns about
                # duplicate sections otherwise): the AP location name, which UT also matches
                refs = [{"name": name, "ref": path} for a in areas for name, path in self.area_sections[a]]
                children.append({"name": clean(title), "sections": refs,
                                 "map_locations": [{"map": self.map_of_image[img["path"]], "x": dot["x"],
                                                    "y": dot["y"]}]})
            out.append({"name": f"{img['name']} World Map", "children": children})
        return out

    @staticmethod
    def group_title(group, areas):
        """A group's name on the world map and its tab: the area when there is one, else the group
        (KEY_GROUPS names a few by a map number; the Cave of Trials' group by its first level)."""
        if len(areas) == 1:
            return areas[0]
        if group.startswith("Cave of Trials"):
            return "Cave of Trials"
        return group if not group.startswith("map ") else areas[0]

    def build_boards(self):
        L = self.w.locations
        rows = L.ALL_ROWS
        self.boards = []
        out = []
        for key, title, map_name, types_ in BOARDS:
            cells = getattr(self, f"cells_{key}")([r for r in rows if r["type"] in types_])
            cols, width, height = board_geometry(len(cells))
            children = []
            for k, cell in enumerate(cells):
                x, y = cell_box(k, cols)
                path = f"{title}/{clean(cell['name'])}"
                secs = [self.section(path, clean(cell["name"]), r, s) for r, s in cell["checks"]]
                children.append({"name": clean(cell["name"]), "sections": secs,
                                 "map_locations": [{"map": map_name, "x": x + 24, "y": y + CELL_H // 2}]})
            img_path = f"images/boards/{map_name}.png"
            self.images[img_path] = draw_board(title, cells, "#283040")
            self.boards.append({"title": title, "map": map_name, "img": img_path, "size": (width, height)})
            out.append({"name": title, "children": children})
        return out

    def cells_shops(self, rows):
        cells = {}
        for r in sorted(rows, key=lambda r: (r["shop"], r["index"])):
            name = f"{r['area']}: shop {r['shop']}"
            cell = cells.setdefault(name, {"name": name, "label": name, "checks": []})
            prefix = f"{r['area']}: shop {r['shop']} "
            short = r["name"][len(prefix):] if r["name"].startswith(prefix) else r["name"]
            cell["checks"].append((r, unique(short, [s for _, s in cell["checks"]])))
        return list(cells.values())

    def cells_crafts(self, rows):
        by_spec = {}
        for r in rows:
            by_spec.setdefault(r["specialty"], []).append(r)
        cells = []
        for spec, rs in sorted(by_spec.items()):
            n = math.ceil(len(rs) / CRAFT_CELL)
            for k in range(n):
                part = rs[k * CRAFT_CELL:(k + 1) * CRAFT_CELL]
                name = f"Item Creation: {spec}" + (f" ({k + 1} of {n})" if n > 1 else "")
                label = f"{spec}" + (f" ({k + 1} of {n})" if n > 1 else "") + f": {len(part)}"
                checks = []
                for r in part:
                    short = r["name"][len(spec) + 2:] if r["name"].startswith(spec + ": ") else r["name"]
                    checks.append((r, unique(short, [s for _, s in checks])))
                cells.append({"name": name, "label": label, "checks": checks})
        return cells

    def cells_killers(self, rows):
        I = self.w.items
        by = {}
        for r in rows:
            by.setdefault(r["character"], []).append(r)
        cells = []
        for who in sorted(by, key=I.PARTY_ID.get):
            checks = [(r, r["name"][len(who) + 2:]) for r in by[who]]
            cells.append({"name": f"Killer moves: {who}", "label": f"{who}: {len(checks)}", "checks": checks})
        return cells

    def cells_skills(self, rows):
        I = self.w.items
        guilds, learn, other = {}, {}, []
        for r in rows:
            if r["type"] == "guild":
                m = re.match(r"Skill Guild: (.*) \(\d+ of \d+\)$", r["name"])
                guilds.setdefault(m.group(1) if m else r["name"], []).append(r)
            elif r["character"]:
                learn.setdefault(r["character"], []).append(r)
            else:
                other.append(r)
        cells = []
        for name, rs in sorted(guilds.items(), key=lambda kv: kv[1][0]["set"]):
            checks = [(r, r["name"][len("Skill Guild: "):]) for r in rs]
            cells.append({"name": f"Skill Guild: {name}", "label": f"Guild: {name}", "checks": checks})
        for who in sorted(learn, key=I.PARTY_ID.get):
            checks = []
            for r in learn[who]:
                short = r["name"]
                for prefix in (f"{who}: ", f"{who} "):
                    if short.startswith(prefix):
                        short = short[len(prefix):]
                        break
                checks.append((r, unique(short, [s for _, s in checks])))
            cells.append({"name": f"Learning: {who}", "label": f"Learning: {who}", "checks": checks})
        for r in other:
            cells.append({"name": f"Learning: {r['name'].split(': ')[-1]}", "label": r["name"],
                          "checks": [(r, r["name"])]})
        return cells

    def cells_drops(self, rows):
        by = {}
        for r in rows:
            by.setdefault((REGIONS.index(r["region"]), r["area"]), []).append(r)
        cells = []
        for (_, area), rs in sorted(by.items(), key=lambda kv: (kv[0][0], natural(kv[0][1]))):
            cells.append({"name": f"Drops: {area}", "label": f"{area}: {len(rs)}",
                          "checks": [(r, r["name"]) for r in sorted(rs, key=lambda r: r["name"])]})
        return cells

    # ---- maps, tabs, layouts
    def build_maps(self):
        maps = []
        for img in self.layout["images"]:
            size = max(img["width"], img["height"])
            loc = max(16, round(size / 56)) if img["kind"] == "area" else 44
            maps.append({"name": self.map_of_image[img["path"]], "location_size": loc,
                         "location_border_thickness": 2 if img["kind"] == "area" else 4, "img": img["path"]})
        for b in self.boards:
            maps.append({"name": b["map"], "location_size": 26, "location_border_thickness": 2, "img": b["img"]})
        for m in sorted(self.rooms):
            r = self.rooms[m]
            maps.append({"name": f"room_{m}", "location_size": r["location_size"],
                         "location_border_thickness": r["font_scale"], "img": r["path"]})
        return maps

    def build_tabs(self):
        """The map variant's tabs: Expel and Energy Nede (their world map, then a tab per area
        group, then a tab per area inside a group of several), Fienal, the Cave of Trials (a tab
        per level), then the boards. An area with rooms (format 1.1) is itself tabbed: its
        overview (the area image, titled as the area) and a tab per room, titled by its map
        number, in room_groups order; a room group with no area image (L'Aqua) is a tab of rooms
        only under its world map. Also each area's and each room's tab path, for
        scripts/map_tabs.lua and the door clicks."""
        D = self.w.data
        stage_of = {g["group"]: (g["stage"] or 99) for g in D.KEY_GROUPS}
        worlds = {i["world_map"]: i for i in self.layout["images"] if i["kind"] == "world"}
        top = []
        for wm, region in ((1, "Expel"), (2, "Energy Nede")):
            img = worlds[wm]
            world_title = f"{img['name']} World Map"
            self.world_tab[wm] = [region, world_title]
            tabs = [{"title": world_title, "content": {"type": "map", "maps": [self.map_of_image[img["path"]]]}}]
            for dot in sorted(img["dots"], key=lambda d: (stage_of.get(d["group"], 99), d["group"])):
                # by the world map the area is entered from (King's Room's one chest is in the
                # Energy Nede region, but the room is in Lacour); Fienal and the Cave of Trials
                # have tabs of their own
                areas = [a for a in dot["areas"] if a in self.area_sections
                         and self.region_of_area[a] not in ("Fienal", "Cave of Trials")]
                if not areas:
                    g = self.room_groups.get(dot["group"])
                    if g and not g["image"] and g["world"] == wm:     # rooms only (L'Aqua)
                        tabs.append({"title": dot["group"], "content": self.area_content(dot["group"])})
                        self.tabs[dot["group"]] = [region, dot["group"]]
                    continue
                title = self.group_title(dot["group"], areas)
                areas.sort(key=lambda a: (a != title, natural(a)))
                if len(areas) == 1:
                    tabs.append({"title": title, "content": self.area_content(areas[0])})
                    self.tabs[areas[0]] = [region, title]
                else:
                    inner = [{"title": a, "content": self.area_content(a)} for a in areas]
                    tabs.append({"title": title, "content": {"type": "tabbed", "tabs": inner}})
                    for a in areas:
                        self.tabs[a] = [region, title, a]
            top.append({"title": region, "content": {"type": "tabbed", "tabs": tabs}})
        for region in ("Fienal", "Cave of Trials"):
            areas = sorted((a for a, r in self.region_of_area.items() if r == region), key=natural)
            if len(areas) == 1:
                top.append({"title": region, "content": self.area_content(areas[0])})
                self.tabs[areas[0]] = [region]
            elif areas:
                inner = []
                for a in areas:
                    t = a[len(region) + 1:] if a.startswith(region + " ") else a
                    inner.append({"title": t, "content": self.area_content(a)})
                    self.tabs[a] = [region, t]
                top.append({"title": region, "content": {"type": "tabbed", "tabs": inner}})
        for b in self.boards:
            top.append({"title": b["title"], "content": {"type": "map", "maps": [b["map"]]}})
            self.tabs["board:" + b["title"]] = [b["title"]]
        for group, g in self.room_groups.items():
            if group not in self.tabs:
                raise SystemExit(f"room group {group!r} has no tab")
            for m in g["rooms"]:
                self.room_tabs[m] = self.tabs[group] + [str(m)]
        return top

    def area_map(self, area):
        img = next(i for i in self.layout["images"] if i.get("area") == area)
        return {"type": "map", "maps": [self.map_of_image[img["path"]]]}

    def area_content(self, area):
        """An area's tab content: its overview map, or with rooms a tabbed of the overview (titled
        as the area, so activating the area's tab also shows its overview) and its rooms."""
        g = self.room_groups.get(area)
        tabs = [{"title": area, "content": self.area_map(area)}] if g is None or g["image"] else []
        if g is None:
            return tabs[0]["content"]
        tabs += [{"title": str(m), "content": self.room_content(m)} for m in g["rooms"]]
        return {"type": "tabbed", "tabs": tabs}

    def room_content(self, m):
        """A room's tab: the room's map, and beside it the live door panel (a Lua item per
        doorway, in letter order, whose badge text scripts/doors.lua keeps current)."""
        content = {"type": "map", "maps": [f"room_{m}"]}
        ids = self.room_doorways.get(m) or []
        if not ids:
            return content
        grid = {"type": "itemgrid", "item_margin": "1,2", "item_size": f"{DOOR_LINE[0]},{DOOR_LINE[1]}",
                "h_alignment": "left", "v_alignment": "top", "rows": [[self.doorways[i]["code"]] for i in ids]}
        return {"type": "dock", "content": [
            {"type": "group", "header": DOOR_PANEL_HEADER, "dock": "right", "content": grid}, content]}

    def build_map_tabs(self):
        """Field map -> the tab path to activate (scripts/map_tabs.lua)."""
        D, L = self.w.data, self.w.locations
        tab = {1: self.world_tab[1], 2: self.world_tab[2]}
        area_images = [i for i in self.layout["images"] if i["kind"] == "area"]
        names = {int(r["map"]): r for r in self.map_names}
        how = {"world": 2, "room": 0, "image": 0, "name": 0, "rows": 0, "group": 0, "region": 0}
        for m, path in self.room_tabs.items():     # a room: its own tab (format 1.1)
            tab[m] = path
            how["room"] += 1
        for img in area_images:      # maps drawn in an area image (a map in two: its own area's first)
            for m in img["maps"]:
                if m["map"] in self.room_tabs:
                    continue
                own = names.get(m["map"], {}).get("area") == img["area"]
                if m["map"] not in tab or own:
                    if m["map"] not in tab:
                        how["image"] += 1
                    tab[m["map"]] = self.tabs[img["area"]]
        group_tab = {}
        for img in self.layout["images"]:
            if img["kind"] == "world":
                for dot in img["dots"]:
                    areas = [a for a in dot["areas"] if a in self.tabs]
                    if areas:
                        group_tab[dot["group"]] = self.tabs[areas[0]][:2]
        row_area = {}
        for r in L.ALL_ROWS:
            for m in [r.get("map")] + [mm for p in r.get("places", []) for mm in p.get("maps", [])]:
                if m and r.get("area") in self.tabs:
                    row_area.setdefault(m, r["area"])
        group_maps = {}
        for g in D.KEY_GROUPS:
            for m in g["maps"]:
                group_maps.setdefault(m, g["group"])
        for g in D.HUB_DUNGEONS:
            for m in g["maps"]:
                if g["dungeon"] in self.tabs:
                    tab.setdefault(m, self.tabs[g["dungeon"]])
        for m, r in sorted(names.items()):
            if m in tab:
                continue
            area = r["area"]
            if area in self.tabs:
                tab[m], k = self.tabs[area], "name"
            elif m in row_area:
                tab[m], k = self.tabs[row_area[m]], "rows"
            elif group_maps.get(m) in group_tab:
                tab[m], k = group_tab[group_maps[m]], "group"
            elif area in ("Expel", "Energy Nede"):
                tab[m], k = self.world_tab[1 if area == "Expel" else 2], "region"
            elif area.startswith("Cave of Trials"):
                tab[m], k = ["Cave of Trials"], "region"
            else:
                continue
            how[k] += 1
        self.map_tab_stats = dict(how, total=len(names), mapped=len(tab))
        self.map_tab_paths = dict(tab)
        lines = ["-- Generated by tools/ap_poptracker.py: do not edit.",
                 "-- Field map id (docs/modding/reference/map_names.csv) -> the tab titles to activate,",
                 "-- outermost first (archipelago.lua switches tabs from so2_map_{team}_{slot}).",
                 "SO2_MAP_TABS = {"]
        for m in sorted(tab):
            lines.append(f"  [{m}] = {{{', '.join(lua_str(t) for t in tab[m])}}},")
        lines.append("}")
        self.put_text("scripts/map_tabs.lua", "\n".join(lines) + "\n")

    def build_layouts(self, map_tabs):
        def grid(codes, per_row, size=ITEM_SIZE):
            rows = [codes[k:k + per_row] for k in range(0, len(codes), per_row)]
            return {"type": "itemgrid", "item_margin": "2,2", "item_size": size, "h_alignment": "left", "rows": rows}

        def group(header, content):
            return {"type": "group", "header": header, "content": content}

        def stack(*content):
            return {"type": "array", "orientation": "vertical", "margin": "0,0", "content": list(content)}

        banner = {"type": "item", "item": "so2_logic_offline", "max_width": 320, "max_height": 30,
                  "h_alignment": "left"}
        keys = stack(group("Area keys: Expel", grid(self.keys[1], 5)),
                     group("Area keys: Energy Nede", grid(self.keys[2], 5)),
                     group("Dungeon keys (hub)", grid(self.dungeon_keys, 5)))
        party = stack(group("Characters", grid(self.characters, 4)),
                      group("Status", grid(["so2_chapter", "so2_story_aware", "so2_auto_tab"], 4)),
                      group("Traps received", grid(self.traps, 5)))
        skills = group("Skills (skillsanity)", grid(self.skills, 5))
        moves = {"type": "tabbed", "tabs": [{"title": who, "content": group(f"{who}'s moves", grid(codes, 5))}
                                             for who, codes in self.moves.items()]}
        room = group("This room's options (from the slot data)", grid(self.setting_codes, 5))
        item_tabs = {"type": "tabbed", "tabs": [
            {"title": "Keys", "content": keys}, {"title": "Party", "content": party},
            {"title": "Skills", "content": skills}, {"title": "Moves", "content": moves},
            {"title": "Room", "content": room}]}
        items_layouts = {
            "so2_banner": banner,
            "so2_item_tabs": item_tabs,
            "so2_items_keys": keys, "so2_items_party": party, "so2_items_skills": skills,
            "so2_items_moves": moves, "so2_items_room": room,
            "tracker_broadcast": {"type": "array", "orientation": "vertical", "background": BG, "content": [
                grid(self.keys[1] + self.keys[2], 7), grid(self.characters, 6)]},
        }
        self.put_json("layouts/items.json", items_layouts)
        tracker = {"tracker_default": {
            "type": "container", "background": BG,
            "content": {"type": "dock", "dropshadow": True, "content": [
                {"type": "array", "dock": "left", "orientation": "vertical", "v_alignment": "top", "content": [
                    {"type": "layout", "key": "so2_banner"},
                    {"type": "layout", "key": "so2_item_tabs"}]},
                {"type": "tabbed", "tabs": map_tabs}]}}}
        self.put_json("layouts/tracker.json", tracker)
        items_only = {"tracker_default": {
            "type": "container", "background": BG,
            "content": {"type": "array", "orientation": "vertical", "content": [
                {"type": "layout", "key": "so2_banner"},
                {"type": "array", "orientation": "horizontal", "content": [
                    {"type": "layout", "key": "so2_items_keys"},
                    stack({"type": "layout", "key": "so2_items_party"}, {"type": "layout", "key": "so2_items_room"}),
                    stack({"type": "layout", "key": "so2_items_skills"},
                          {"type": "layout", "key": "so2_items_moves"})]}]}}}
        self.put_json(f"{VARIANT_ITEMS}/layouts/tracker.json", items_only)

    # ---- generated Lua
    def build_lua(self):
        lines = ["-- Generated by tools/ap_poptracker.py: do not edit.",
                 "-- AP item id -> {{code, type}}: the pack's items each id advances (archipelago.lua).",
                 "ITEM_MAPPING = {"]
        for i in sorted(self.item_mapping):
            entries = ", ".join(f"{{{lua_str(c)}, {lua_str(t)}}}" for c, t in self.item_mapping[i])
            lines.append(f"  [{i}] = {{{entries}}},")
        lines.append("}")
        self.put_text("scripts/autotracking/item_mapping.lua", "\n".join(lines) + "\n")
        lines = ["-- Generated by tools/ap_poptracker.py: do not edit.",
                 "-- AP location id -> {\"@location path/section\"}: one section per AP location.",
                 "LOCATION_MAPPING = {"]
        for i in sorted(self.location_mapping):
            lines.append(f"  [{i}] = {{{lua_str(self.location_mapping[i])}}},")
        lines.append("}")
        self.put_text("scripts/autotracking/location_mapping.lua", "\n".join(lines) + "\n")
        I = self.w.items
        lines = ["-- Generated by tools/ap_poptracker.py: do not edit.",
                 f"SO2_PACK = {{uid = {lua_str(PACK_UID)}, version = {lua_str(PACK_VERSION)}, "
                 f"game = {lua_str(GAME)}, layout_format = {self.layout['format']}}}",
                 "-- Slot-data options shown as setting items (archipelago.lua sets them on connect).",
                 "SO2_SETTINGS = {"]
        for s in self.settings:
            fields = [f"key = {lua_str(s['key'])}", f"code = {lua_str(s['code'])}", f"kind = {lua_str(s['kind'])}"]
            if s["kind"] == "choice":
                fields.append("values = {" + ", ".join(lua_str(v) for v in s["values"]) + "}")
                fields.append(f"default = {lua_str(s['default'])}")
            else:
                fields.append(f"default = {lua_bool(s['default'])}")
                if "absent" in s:
                    fields.append(f"absent = {lua_bool(s['absent'])}")
            lines.append("  {" + ", ".join(fields) + "},")
        lines.append("}")
        lines.append("-- Party member -> character item code (slot data `characters` is the room's pool).")
        lines.append("SO2_CHARACTERS = {" + ", ".join(f"[{lua_str(w)}] = {lua_str('char_' + slug(w))}"
                                                     for w in I.CHARACTER_NAMES) + "}")
        lines.append("-- Hub mode's dungeon key item -> code (slot data `dungeon_keys`).")
        lines.append("SO2_DUNGEON_KEYS = {" + ", ".join(
            f"[{lua_str(g['name'])}] = {lua_str('dkey_' + slug(g['dungeon']))}"
            for _, g in sorted(I.HUB_KEY_OF.items())) + "}")
        worlds = [i for i in self.layout["images"] if i["kind"] == "world"]
        lines.append("-- The world maps: world x/z -> image pixels (tracker_maps.md \"World images\").")
        lines.append("SO2_WORLD_MAPS = {" + ", ".join(
            f"[{i['world_map']}] = {{map = {lua_str(self.map_of_image[i['path']])}, x0 = {i['x0']}, "
            f"z0 = {i['z0']}, units_per_pixel = {i['units_per_pixel']}}}" for i in worlds) + "}")
        self.put_text("scripts/pack_data.lua", "\n".join(lines) + "\n")

    def map_title(self, m):
        """A destination's name for the door panel: a room's name, a world map's, else
        map_names.csv's place (an undrawn map)."""
        if m in self.rooms:
            return self.rooms[m]["name"]
        if m in WORLD_MAP_NAMES:
            return f"{WORLD_MAP_NAMES[m]} (world map)"
        r = next((r for r in self.map_names if int(r["map"]) == m), None)
        return (r["place"] or r["area"]) if r else ""

    def build_door_lua(self):
        """scripts/doors_data.lua: the rooms (name, tab path), the vanilla door table (doorway id
        -> its room, letter, panel item code and destinations), the door sections, and names for
        the destinations that are no room. scripts/doors.lua reads it."""
        lines = ["-- Generated by tools/ap_poptracker.py: do not edit.",
                 "-- Rooms (docs/ap/tracker_maps.md \"Rooms\"): map -> {name, tab path}.",
                 "SO2_ROOMS = {"]
        for m in sorted(self.rooms):
            tabs = ", ".join(lua_str(t) for t in self.room_tabs[m])
            lines.append(f"  [{m}] = {{name = {lua_str(self.rooms[m]['name'])}, tabs = {{{tabs}}}}},")
        lines.append("}")
        lines.append("-- Destinations that are no room: world maps and undrawn story maps.")
        others = sorted({t["map"] for d in self.doorways.values() for t in d["to"] if t["map"] not in self.rooms})
        lines.append("SO2_MAP_NAMES = {" + ", ".join(f"[{m}] = {lua_str(self.map_title(m))}" for m in others) + "}")
        lines.append("-- The vanilla door table: doorway id -> {map, letter, code (its panel item), to = {{map,")
        lines.append("-- back (the doorway at the other end), cond (only in some story states)}, ...}}.")
        lines.append("SO2_DOORWAYS = {")
        for wid in sorted(self.doorways):
            d = self.doorways[wid]
            to = []
            for t in d["to"]:
                f = [f"map = {t['map']}"]
                if "back" in t:
                    f.append(f"back = {t['back']}")
                if t.get("conditional"):
                    f.append("cond = true")
                to.append("{" + ", ".join(f) + "}")
            lines.append(f"  [{wid}] = {{map = {d['map']}, letter = {lua_str(d['letter'])}, "
                         f"code = {lua_str(d['code'])}, to = {{{', '.join(to)}}}}},")
        lines.append("}")
        lines.append("-- Room map -> its doorway ids in letter order (the panel's lines).")
        lines.append("SO2_ROOM_DOORS = {")
        for m in sorted(self.room_doorways):
            if self.room_doorways[m]:
                lines.append(f"  [{m}] = {{{', '.join(str(i) for i in self.room_doorways[m])}}},")
        lines.append("}")
        lines.append("-- A door section's full id -> its doorway id (the click handler).")
        lines.append("SO2_DOOR_SECTIONS = {")
        for sec, wid in sorted(self.door_sections.items()):
            lines.append(f"  [{lua_str(sec)}] = {wid},")
        lines.append("}")
        self.put_text("scripts/doors_data.lua", "\n".join(lines) + "\n")

    # ---- Universal Tracker's map tab (docs/ap/apworld.md "Universal Tracker's map tab")
    def build_ut(self, places, world, boards, tabs):
        """UT's map tab, hybrid: these JSON files go into the APWorld (apworld/so2/tracker/), the
        images come from this pack's zip (the player points UT at it once). The same maps and
        squares as the pack, but every section named by its AP location (UT matches names; no name
        mapping), no rules (UT has the world's own logic), no doors (UT shows only AP locations,
        entrances and events: the doors are none of them until Phase C names them as entrances),
        rooms titled by their label in UT's map menu. Also map_pages.json: field map -> the page
        (index in maps.json) UT opens from so2_map_{team}_{player}, and the world maps' x/z ->
        pixel mapping for UT's player marker."""
        name_of = {lid: r["name"] for r in self.w.locations.ALL_ROWS for lid in [r["id"]]}
        id_of_section = {f"{path}/{name}": lid for lid, (path, name) in self.section_of.items()}

        def nodes(tree, parent=""):
            out = []
            for node in tree:
                path = f"{parent}/{node['name']}" if parent else node["name"]
                n = {"name": node["name"]}
                if "sections" in node:
                    n["sections"] = [{"name": name_of[id_of_section[sec["ref"] if "ref" in sec else f"{path}/{sec['name']}"]]}
                                     for sec in node["sections"]]
                if "map_locations" in node:
                    n["map_locations"] = [dict(ml) for ml in node["map_locations"]]
                if "children" in node:
                    n["children"] = nodes(node["children"], path)
                out.append(n)
            return out
        maps = self.build_maps()
        page_of = {m["name"]: k for k, m in enumerate(maps)}
        labels = {str(m): r["label"] for m, r in self.rooms.items()}

        def ut_tab(tab):
            content = tab["content"]
            room = tab["title"] in labels and any(n.get("type") == "map" and n["maps"] == [f"room_{tab['title']}"]
                                                  for n in walk(content))
            if room:
                return {"title": labels[tab["title"]], "content": {"type": "map", "maps": [f"room_{tab['title']}"]}}
            if content.get("type") == "tabbed":
                return {"title": tab["title"], "content": {"type": "tabbed", "tabs": [ut_tab(t) for t in content["tabs"]]}}
            return {"title": tab["title"], "content": content}
        layout = {"so2_ut_maps": {"type": "tabbed", "tabs": [ut_tab(t) for t in tabs]}}
        pages = {}
        overview = {}
        for area, path in self.tabs.items():
            img = next((i for i in self.layout["images"] if i.get("area") == area), None)
            if img is not None:
                overview[tuple(path)] = page_of[self.map_of_image[img["path"]]]
        for m, path in sorted(self.map_tab_paths.items()):
            if m in self.rooms:
                pages[str(m)] = page_of[f"room_{m}"]
            elif m in self.world_tab:
                pages[str(m)] = page_of[self.map_of_image[next(i["path"] for i in self.layout["images"]
                                                               if i.get("world_map") == m)]]
            elif tuple(path) in overview:
                pages[str(m)] = overview[tuple(path)]
        worlds = {}
        for img in self.layout["images"]:
            if img["kind"] == "world":
                worlds[str(img["world_map"])] = {"page": page_of[self.map_of_image[img["path"]]], "x0": img["x0"],
                                                 "z0": img["z0"], "units_per_pixel": img["units_per_pixel"],
                                                 "width": img["width"], "height": img["height"]}
        files = {
            "maps/maps.json": maps,
            "locations/locations.json": nodes(places) + nodes(world) + nodes(boards),
            "layouts/maps.json": layout,
            "map_pages.json": {"source": "tools/ap_poptracker.py", "pack": f"{PACK_UID} {PACK_VERSION}",
                               "player_icon": PLAYER_IMG, "pages": pages, "world": worlds},
        }
        return {path: (json.dumps(obj, indent=1, ensure_ascii=False) + "\n").encode("utf-8")
                for path, obj in files.items()}

    def build_manifest(self, versions_url):
        flags = ["ap"]
        manifest = {
            "name": "Star Ocean: The Second Story (Shattered Stars)",
            "game_name": GAME,
            "package_uid": PACK_UID,
            "package_version": PACK_VERSION,
            "platform": "PC",
            "author": AUTHOR,
            "variants": {
                VARIANT_MAP: {"display_name": "Map Tracker", "flags": flags},
                VARIANT_ITEMS: {"display_name": "Items Only", "flags": flags},
            },
            "versions_url": versions_url,
            "min_poptracker_version": POPTRACKER_VERSION,
            "target_poptracker_version": POPTRACKER_VERSION,
        }
        self.put_json("manifest.json", manifest)
        self.put_json("settings.json", {"smooth_scaling": True, "smooth_map_scaling": True})

    def copy_lua(self):
        for base, _, names in os.walk(LUA_SRC):
            for n in sorted(names):
                if not n.endswith(".lua"):
                    continue
                src = os.path.join(base, n)
                rel = os.path.relpath(src, LUA_SRC).replace(os.sep, "/")
                self.files[f"scripts/{rel}"] = open(src, "rb").read()
        self.files["LICENSE"] = open(os.path.join(REPO, "LICENSE"), "rb").read()
        self.files["images/icon.png"] = open(os.path.join(REPO, "res", "icon", "shattered-stars-128.png"), "rb").read()

    def draw_images(self, with_images):
        style = self.layout["style"]
        for img in self.layout["images"]:
            if with_images:
                self.images[img["path"]] = draw_area(img, style) if img["kind"] == "area" else draw_world(img)
        for m, room in sorted(self.rooms.items()):
            self.images[room["path"]] = draw_room(room, style) if with_images else None
        self.images[DOOR_LINE_IMG] = draw_door_line() if with_images else None
        self.images[DOOR_ICON_IMG] = draw_door_icon() if with_images else None
        self.images[PLAYER_IMG] = draw_player_marker() if with_images else None
        if with_images:
            for path, (text, colour) in self.badges.items():
                self.images[path] = badge(text, colour)
        for path, img in self.images.items():
            self.files[path] = png_bytes(img) if with_images else b""

    def build(self, versions_url, with_images=True):
        self.build_items()
        places = self.build_places()
        world = self.build_world()
        boards = self.build_boards()
        rows = self.w.locations.ALL_ROWS
        unplaced = [r["name"] for r in rows if r["id"] not in self.section_of]
        if unplaced:
            raise SystemExit(f"{len(unplaced)} locations have no section: {unplaced[:3]}")
        self.put_json("items/items.json", self.items)
        self.put_json("locations/places.json", places)
        self.put_json("locations/world.json", world)
        self.put_json("locations/boards.json", boards)
        self.put_json("locations/doors.json", self.build_doors())
        self.put_json("maps/maps.json", self.build_maps())
        tabs = self.build_tabs()
        self.build_map_tabs()
        self.build_layouts(tabs)
        self.ut_files = self.build_ut(places, world, boards, tabs)
        self.build_lua()
        self.build_door_lua()
        self.build_manifest(versions_url)
        self.copy_lua()
        self.draw_images(with_images)


def walk(node):
    """Every dict in a layout node, depth first."""
    if isinstance(node, dict):
        yield node
        for v in node.values():
            yield from walk(v)
    elif isinstance(node, list):
        for v in node:
            yield from walk(v)


def room_map(path):
    """images/rooms/<m>.png -> the PopTracker map name room_<m>."""
    return "room_" + os.path.splitext(os.path.basename(path))[0]


def unique(name, taken):
    out, k = name, 2
    while out in taken:
        out = f"{name} #{k}"
        k += 1
    return out


def lua_str(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def lua_bool(b):
    return "true" if b else "false"


def write_folder(pack, out):
    if os.path.isdir(out):
        shutil.rmtree(out)
    for path, data in sorted(pack.files.items()):
        dest = os.path.join(out, *path.split("/"))
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        with open(dest, "wb") as f:
            f.write(data)


def write_zip(pack, path):
    """The pack's files at the zip's root, sorted, with a fixed date: reproducible bytes."""
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for name, data in sorted(pack.files.items()):
            info = zipfile.ZipInfo(name, date_time=(2026, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            z.writestr(info, data)


def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    p.add_argument("--out", default=os.path.join(REPO, "dist", "poptracker", PACK_UID))
    p.add_argument("--zip", help="also write the pack as this zip")
    p.add_argument("--ap-dir", default=os.path.join(REPO, "scratchpad", "ap", "Archipelago"))
    p.add_argument("--layout", default=os.path.join(REPO, "docs", "ap", "tracker_maps.json"))
    p.add_argument("--map-names", default=os.path.join(REPO, "docs", "modding", "reference", "map_names.csv"))
    p.add_argument("--versions-url", default=VERSIONS_URL)
    p.add_argument("--no-images", action="store_true", help="empty image files (quick JSON/Lua checks)")
    p.add_argument("--ut-out", help="also write Universal Tracker's map-tab JSON here (apworld/so2/tracker)")
    p.add_argument("--ut-check", help="compare that folder (apworld/so2/tracker) with this build's; exit 1 if it differs")
    a = p.parse_args()
    w = load_world(a.ap_dir)
    layout = json.load(open(a.layout, encoding="utf-8"))
    if layout.get("format") != 1 or layout.get("pack_uid") != PACK_UID:
        raise SystemExit(f"{a.layout}: format {layout.get('format')} / uid {layout.get('pack_uid')}: "
                         "this generator reads format 1 for " + PACK_UID)
    map_names = list(csv.DictReader(open(a.map_names, encoding="utf-8")))
    pack = Pack(w, layout, map_names)
    pack.build(a.versions_url, with_images=not a.no_images)
    write_folder(pack, a.out)
    with open(os.path.join(os.path.dirname(os.path.abspath(a.out)), "ut_name_mapping.json"), "w",
              encoding="utf-8") as f:
        json.dump(dict(sorted(pack.ut_names.items())), f, indent=1, ensure_ascii=False)
        f.write("\n")
    ut_dir = os.path.join(os.path.dirname(os.path.abspath(a.out)), "ut_tracker")
    for target in [ut_dir] + ([a.ut_out] if a.ut_out else []):
        if os.path.isdir(target):
            shutil.rmtree(target)
        for path, data in sorted(pack.ut_files.items()):
            dest = os.path.join(target, *path.split("/"))
            os.makedirs(os.path.dirname(dest), exist_ok=True)
            with open(dest, "wb") as f:
                f.write(data)
    if a.ut_check:
        stale = [p for p, data in pack.ut_files.items()
                 if not os.path.exists(os.path.join(a.ut_check, *p.split("/")))
                 or open(os.path.join(a.ut_check, *p.split("/")), "rb").read() != data]
        extra = [os.path.relpath(os.path.join(b, f), a.ut_check).replace(os.sep, "/")
                 for b, _, fs in os.walk(a.ut_check) for f in fs]
        extra = [p for p in extra if p not in pack.ut_files and "__pycache__" not in p]
        if stale or extra:
            raise SystemExit(f"{a.ut_check} is out of date ({stale + extra}): run tools/ap_poptracker.py --ut-out {a.ut_check}")
    if a.zip:
        write_zip(pack, a.zip)
    st = pack.map_tab_stats
    print(f"pack {PACK_UID} {PACK_VERSION}: {len(pack.files)} files, {len(pack.items)} items, "
          f"{len(pack.item_mapping)} item ids, {len(pack.location_mapping)} location ids, "
          f"{len(pack.boards)} boards, map tabs {st['mapped']}/{st['total']} maps -> {a.out}"
          + (f", {a.zip}" if a.zip else ""))


if __name__ == "__main__":
    main()
