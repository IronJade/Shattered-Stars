#!/usr/bin/env python3
"""Static check of Shattered Stars mod packages (docs/port/notes/mods.md section 7).

Runs anywhere with Python 3.10+ and this repository: no game discs, no network. It checks
a package's manifest, its files, the grammar of every patch line (the four commands of
port/src/game/field/mods.h), the numbers against the repository's tables of the disc
(docs/apworld/field.json, doors.json, battles.json), and flags risky edits for the
maintainer's review.

  mod_check.py PACKAGE [PACKAGE ...]   a package folder or .zip
  mod_check.py --all                   every folder under mods/
    --json      the results as JSON
    --github    GitHub workflow annotations, plus a job summary when GITHUB_STEP_SUMMARY is set
    --strict    warnings (undeclared risky edits) fail too

Exit status: 1 when any package has an error (or, with --strict, a warning); 0 otherwise.

Errors are things the game can't use or that are certainly wrong. Risky edits are
warnings unless the package declares them in mod.json ("declared_risks": [{"rule": ...,
"reason": ..., "lines": [...]}]), which lists them as declared, with the reason.
"""

import argparse
import json
import os
import re
import sys
import zipfile
from dataclasses import dataclass, field

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # <section>/tools/ -> the repo

# Limits (the engine's own: port/src/mods/catalog.h).
MAX_PATCH_FILES = 32
MAX_PATCH_BYTES = 256 * 1024
MAX_README_BYTES = 64 * 1024
MAX_MANIFEST_BYTES = 16 * 1024
MAX_PACKAGE_BYTES = 1024 * 1024
MANIFEST_FORMAT = 1

# Bounds not in the tables, fixed conservatively:
# - the largest script word: every field map of the North American disc measured (map 474's
#   script is the largest, 25889 words), rounded up to 0x8000. The engine also skips a word
#   past the end of a map's script, so this only catches nonsense.
SCRIPT_WORD_LIMIT = 0x8000
# - records: items 0x30 bytes (1023 of them, item 1 first), enemies 0x5c; stock index < 0x100.
ITEM_RECORD, ENEMY_RECORD, STOCK_LIMIT, ITEM_COUNT = 0x30, 0x5C, 0x100, 1023
# - shop prices: 99 of an item must fit a signed 32-bit total, and the Fol cap is 999,999,999.
PRICE_LIMIT = (2**31 - 1) // 99   # 21,691,754
FOL_CAP = 999_999_999
# - story key items: chests and gifts whose value is 10000 + the key's flag (flags 66-93).
KEY_ITEM_BASE = 10000

ITEM_FIELDS = {"price": (0x00, 4), "atk": (0x08, 2), "def": (0x10, 2), "amount": (0x19, 1),
               "effect": (0x28, 1), "type": (0x2A, 1), "sell": (0x2D, 1)}
ENEMY_FIELDS = {"hp": (0x00, 4), "mp": (0x04, 4), "exp": (0x08, 4), "fol": (0x0C, 4), "speed": (0x16, 2),
                "drop1": (0x18, 2), "drop2": (0x1A, 2), "atk": (0x1C, 2), "def": (0x1E, 2), "int": (0x20, 2),
                "level": (0x24, 2), "eva": (0x26, 2), "acc": (0x28, 2), "guts": (0x36, 1),
                "drop1_odds": (0x37, 1), "drop2_odds": (0x38, 1)}

# Risky-edit rules (warnings unless declared) and what each flags.
RISKY = {
    "script-unknown": "a script word that isn't a chest's contents or a door's target",
    "door-missing-map": "a door sent to a map that isn't a field map",
    "door-retarget": "a door sent somewhere else",
    "key-item": "an edit to a chest or gift that gives a story key item",
    "chest-empty": "a chest emptied",
    "chest-other-sites": "a chest patched on only some of the maps it is on",
    "item-unknown": "an item number with no name in the tables",
    "boss-zero": "a boss's HP or EXP set to 0",
    "enemy-zero-hp": "an enemy's HP set to 0",
    "price-zero": "an item's price set to 0",
    "price-high": "a price too high to buy",
    "sell-over-price": "a sell percentage over 100",
    "item-behaviour": "an item's effect kind or type changed",
    "raw-field": "a record field given as a raw offset",
    "drop-odds": "drop odds over 100",
    "shop-only-source": "an item removed from the only shop that sells it",
}
ERRORS = ("manifest", "folder-name", "files", "size", "text", "grammar", "range", "expected", "same-place", "declared")

ID_RE = re.compile(r"^[a-z0-9][a-z0-9._-]{0,47}$")
VERSION_RE = re.compile(r"^[0-9]{1,9}(\.[0-9]{1,9}){0,3}$")
TAG_RE = re.compile(r"^[a-z0-9-]{1,24}$")
PATCH_NAME_RE = re.compile(r"^[^./\\:][^/\\:]*\.patch$")
KNOWN_KEYS = {"format", "id", "name", "version", "author", "description", "min_port_version", "tags", "homepage",
              "patches", "declared_risks"}


def port_version():
    with open(os.path.join(ROOT, "port", "CMakeLists.txt"), encoding="utf-8") as f:
        text = f.read()
    m = re.search(r"project\(ShatteredStars[^)]*VERSION\s+([0-9.]+)\)", text)
    return m.group(1) if m else "0"


def version_key(v):
    return [int(p) for p in v.split(".")]


def compare_versions(a, b):
    x, y = version_key(a), version_key(b)
    n = max(len(x), len(y))
    x += [0] * (n - len(x))
    y += [0] * (n - len(y))
    return (x > y) - (x < y)


# ---- the repository's tables of the disc ----------------------------------------------------

class Tables:
    def __init__(self, root=ROOT):
        def load(name):
            with open(os.path.join(root, "docs", "apworld", name), encoding="utf-8") as f:
                return json.load(f)
        field_json, doors_json, battles = load("field.json"), load("doors.json"), load("battles.json")
        self.field_maps = set(field_json["field_maps"])
        self.item_names = {int(k): v for k, v in field_json["item_names"].items()}
        self.shops = {int(k): v["stock"] for k, v in field_json["shops"].items()}
        # Chests: (map, word) -> site, plus the chest each site belongs to (its other sites).
        self.chest_sites = {}
        for chest in field_json["chests"]:
            sites = chest.get("sites") or [chest]
            keys = [(s["map"], s["word"]) for s in sites]
            for s in sites:
                self.chest_sites[(s["map"], s["word"])] = {
                    "form": s["form"], "expected": s["expected"], "item": s.get("item", chest.get("item")),
                    "flag": chest["flag"], "name": chest.get("name", ""), "all": keys}
        # Key-item gifts: (map, word of the flag value) -> the key's flag.
        self.key_gift_words = {}
        for gift in field_json["gifts"]:
            for s in gift.get("sites", []):
                if s.get("what") == "flag" and "value_at" in s:
                    self.key_gift_words[(gift["map"], s["value_at"] // 4)] = s.get("value")
        # Inline map transfers: (map, first word) -> the door; every map sharing the script.
        self.doors = {}
        for door in doors_json["doors"]:
            if door["op"] != "map_transfer_c" or door["form"] != "inline":
                continue
            for m in door.get("maps") or [door["map"]]:
                self.doors[(m, door["offset"] // 4)] = door
        self.enemies = {int(k): v for k, v in battles["enemy_types"].items()}
        self.boss_types = set()
        for enc in battles["encounters"].values():
            for member in enc["members"]:
                if member.get("boss") or enc.get("boss"):
                    self.boss_types.add(member["type"])

    def item_name(self, item):
        return self.item_names.get(item)

    def door_at(self, map_id, word):
        """(door, index 0-3 of the word in its instruction) or (None, None)."""
        for k in range(4):
            door = self.doors.get((map_id, word - k))
            if door is not None:
                return door, k
        return None, None


def door_words(door):
    """The four instruction words' known parts: (mask, value) per word, from the record."""
    to = door["to"]
    h = lambda v: v & 0xFFFF  # noqa: E731
    return [(0x0000FFFF, h(to["map"])),
            (0xFFFFFFFF, h(to["transition"]) | h(to["x"]) << 16),
            (0xFFFFFFFF, h(to["y"]) | h(to["z"]) << 16),
            (0xFFFFFFFF, h(to["dir"]) | h(to["extra"]) << 16)]


# ---- patch lines (the grammar of port/src/game/field/mods.h) ------------------------------

class ParseError(Exception):
    pass


def parse_number(s):
    """A number as the engine reads it (strtoll base 0, then to 32 bits); octal-looking
    numbers are refused, because the engine would read 010 as 8."""
    m = re.fullmatch(r"([+-]?)(0[xX][0-9a-fA-F]+|[0-9]+)", s)
    if not m:
        raise ParseError(f"'{s}' isn't a number (decimal, or hex with 0x)")
    digits = m.group(2)
    if len(digits) > 1 and digits[0] == "0" and digits[1] not in "xX":
        raise ParseError(f"'{s}' has a leading zero, which the engine reads as octal; write it without")
    v = int(digits, 0)
    if v >= 2**63:
        raise ParseError(f"'{s}' is too large")
    if m.group(1) == "-":
        v = -v
    return v & 0xFFFFFFFF, v


@dataclass
class Line:
    file: str
    number: int
    text: str
    kind: str
    target: int
    where: int
    size: int
    expected: int
    value: int
    field: str = ""   # the record field's name ("" for a raw offset or a script / shop line)

    @property
    def label(self):
        return f"{self.file}:{self.number}"

    def place(self):
        if self.kind == "script":
            return f"map {self.target} word {self.where:#x}"
        if self.kind == "shop":
            return f"shop {self.target} entry {self.where}"
        name = self.field or f"+{self.where:#x}:{self.size}"
        return f"{self.kind} {self.target} {name}"


def field_spec(text, kind):
    names = ITEM_FIELDS if kind == "item" else ENEMY_FIELDS
    if text in names:
        off, size = names[text]
        return off, size, text
    off_text, colon, size_text = text.partition(":")
    off, _ = parse_number(off_text)
    if colon:
        size, _ = parse_number(size_text)
        if size not in (1, 2, 4):
            raise ParseError(f"field size {size_text} must be 1, 2 or 4")
    elif kind == "item":
        size = 4 if off == 0 else 2 if 4 <= off < 0x14 else 1
    else:
        size = 4 if off < 0x10 else 2 if off < 0x2A or off == 0x3A else 1
    limit = ITEM_RECORD if kind == "item" else ENEMY_RECORD
    if off + size > limit:
        raise ParseError(f"field {text} is past the end of the {limit:#x}-byte {kind} record")
    name = next((n for n, (o, s) in names.items() if o == off and s == size), "")
    return off, size, name


def parse_line(text, file, number):
    """A Line, None for a blank / comment line; ParseError when the engine wouldn't take it."""
    body = text.split("#", 1)[0]
    if not body.strip():
        return None
    parts = body.split()
    if parts[0] not in ("script", "item", "shop", "enemy"):
        raise ParseError(f"unknown command '{parts[0]}' (only script, item, shop and enemy)")
    if len(parts) != 5:
        raise ParseError(f"'{parts[0]}' takes four values ({parts[0]} TARGET WHERE EXPECTED VALUE), not {len(parts) - 1}")
    kind = parts[0]
    target, target_raw = parse_number(parts[1])
    expected, expected_raw = parse_number(parts[3])
    value, value_raw = parse_number(parts[4])
    name = ""
    if kind == "script":
        where, _ = parse_number(parts[2])
        size = 4
    elif kind == "shop":
        where, _ = parse_number(parts[2])
        size = 2
        if where >= STOCK_LIMIT:
            raise ParseError(f"stock index {parts[2]} must be below {STOCK_LIMIT}")
    else:
        where, size, name = field_spec(parts[2], kind)
        if kind == "item" and not 1 <= target < 0x400:
            raise ParseError(f"item {parts[1]} must be 1-1023")
        mask = (1 << (8 * size)) - 1
        for label, raw in (("EXPECTED", expected_raw), ("VALUE", value_raw)):
            if raw < 0 or raw > mask:
                raise ParseError(f"{label} {raw} doesn't fit the {size}-byte field (0-{mask})")
    return Line(file, number, text.rstrip("\r\n"), kind, target_raw & 0xFFFFFFFF, where, size, expected, value, name)


# ---- results -----------------------------------------------------------------------------------

@dataclass
class Finding:
    level: str     # "error", "warning", "declared", "info"
    rule: str
    message: str
    file: str = ""
    line: int = 0
    reason: str = ""

    def where(self):
        return f"{self.file}:{self.line}" if self.line else self.file


@dataclass
class Result:
    path: str
    mod_id: str = ""
    manifest: dict = field(default_factory=dict)
    findings: list = field(default_factory=list)
    lines: list = field(default_factory=list)

    def add(self, level, rule, message, file="", line=0):
        self.findings.append(Finding(level, rule, message, file, line))

    def count(self, level):
        return sum(1 for f in self.findings if f.level == level)


# ---- reading packages --------------------------------------------------------------------------

def read_package(path, result):
    """{name: bytes} of a package folder or .zip, after the file rules; None when unreadable."""
    files = {}
    if os.path.isdir(path):
        for name in sorted(os.listdir(path)):
            full = os.path.join(path, name)
            if os.path.islink(full):
                result.add("error", "files", f"{name} is a link; a package holds only plain files", name)
            elif os.path.isdir(full):
                result.add("error", "files", f"{name}/ is a folder; a package holds only files", name)
            else:
                with open(full, "rb") as f:
                    files[name] = f.read(MAX_PACKAGE_BYTES + 1)
        return files
    if not zipfile.is_zipfile(path):
        result.add("error", "files", "not a package folder or a .zip archive")
        return None
    with zipfile.ZipFile(path) as z:
        infos = [i for i in z.infolist() if not i.is_dir()]
        names = [i.filename.replace("\\", "/") for i in infos]
        for n in names:
            parts = n.split("/")
            if n.startswith("/") or ".." in parts or ":" in n:
                result.add("error", "files", f"{n}: an unsafe path")
                return None
        prefix = ""
        if "mod.json" not in names:
            tops = {n.split("/")[0] for n in names}
            if len(tops) == 1 and f"{next(iter(tops))}/mod.json" in names:
                prefix = next(iter(tops)) + "/"
            else:
                result.add("error", "files", "no mod.json at the archive's top (or in its one top-level folder)")
                return None
        total = 0
        for info, n in zip(infos, names):
            rel = n[len(prefix):] if n.startswith(prefix) else n
            if not n.startswith(prefix) or "/" in rel:
                result.add("error", "files", f"{n}: a package holds only files, in one folder", rel)
                continue
            if info.file_size > MAX_PACKAGE_BYTES:
                result.add("error", "size", f"{rel} is larger than the whole package may be", rel)
                continue
            total += info.file_size
            files[rel] = z.read(info)
        if total > MAX_PACKAGE_BYTES:
            result.add("error", "size", "the package is larger than 1 MiB")
    return files


def check_manifest(data, result, folder_name, repo_version):
    try:
        text = data.decode("utf-8-sig")
    except UnicodeDecodeError:
        result.add("error", "text", "mod.json isn't UTF-8 text", "mod.json")
        return None
    try:
        m = json.loads(text)
    except json.JSONDecodeError as e:
        result.add("error", "manifest", f"mod.json isn't valid JSON: {e.msg} on line {e.lineno}", "mod.json", e.lineno)
        return None
    if not isinstance(m, dict):
        result.add("error", "manifest", "mod.json must hold a JSON object", "mod.json")
        return None

    def err(msg):
        result.add("error", "manifest", msg, "mod.json")

    if m.get("format") != MANIFEST_FORMAT or isinstance(m.get("format"), bool):
        err(f'"format" must be {MANIFEST_FORMAT}')
    for key, limit in (("id", 48), ("name", 60), ("version", 40), ("author", 60), ("description", 1000),
                       ("min_port_version", 40)):
        v = m.get(key)
        if not isinstance(v, str) or not v.strip():
            err(f'"{key}" is required (text)')
        elif len(v) > limit:
            err(f'"{key}" is longer than {limit} characters')
    mod_id = m.get("id") if isinstance(m.get("id"), str) else ""
    if mod_id and not ID_RE.match(mod_id):
        err(f'id "{mod_id}" isn\'t valid: up to 48 lower-case letters, digits, "-", "_" or ".", starting with a letter or digit')
    if folder_name is not None and mod_id and folder_name != mod_id:
        result.add("error", "folder-name", f"the folder is named {folder_name}, not after the id {mod_id}")
    for key in ("version", "min_port_version"):
        v = m.get(key)
        if isinstance(v, str) and v and not VERSION_RE.match(v):
            err(f'"{key}" must be one to four dot-separated numbers, like 1.0.0')
    mpv = m.get("min_port_version")
    if isinstance(mpv, str) and VERSION_RE.match(mpv) and compare_versions(mpv, repo_version) > 0:
        err(f'"min_port_version" {mpv} is newer than this repository\'s port ({repo_version})')
    tags = m.get("tags", [])
    if not isinstance(tags, list) or not all(isinstance(t, str) for t in tags):
        err('"tags" must be a list of words')
    else:
        if len(tags) > 8:
            err('"tags" may hold at most 8 tags')
        for t in tags:
            if not TAG_RE.match(t):
                err(f'tag "{t}" must be 1-24 of a-z, 0-9 and "-"')
        if len(set(tags)) != len(tags):
            err('"tags" repeats a tag')
    if "homepage" in m:
        hp = m["homepage"]
        if not isinstance(hp, str) or not re.match(r"^https://[^\s]+$", hp) or len(hp) > 200:
            err('"homepage" must be an https:// address')
    patches = m.get("patches")
    if not isinstance(patches, list) or not patches:
        err('"patches" must list the package\'s .patch files, in the order they apply')
        patches = []
    elif len(patches) > MAX_PATCH_FILES:
        err(f'"patches" lists more than {MAX_PATCH_FILES} files')
    for p in patches:
        if not isinstance(p, str) or not PATCH_NAME_RE.match(p):
            err(f'"patches" entry {p!r} must be a file name ending in .patch (no folders)')
    if len({p.lower() for p in patches if isinstance(p, str)}) != len(patches):
        err('"patches" names a file twice')
    risks = m.get("declared_risks", [])
    if not isinstance(risks, list):
        result.add("error", "declared", '"declared_risks" must be a list', "mod.json")
        risks = []
    for r in risks:
        if (not isinstance(r, dict) or not isinstance(r.get("rule"), str) or not isinstance(r.get("reason"), str)
                or not r["reason"].strip()):
            result.add("error", "declared", 'each "declared_risks" entry needs a "rule" and a "reason"', "mod.json")
        elif r["rule"] not in RISKY:
            result.add("error", "declared", f'declared rule "{r["rule"]}" isn\'t a risky-edit rule ({", ".join(RISKY)})',
                       "mod.json")
        elif len(r["reason"]) > 300:
            result.add("error", "declared", "a declared risk's reason is longer than 300 characters", "mod.json")
        elif "lines" in r and (not isinstance(r["lines"], list) or not all(isinstance(x, str) for x in r["lines"])):
            result.add("error", "declared", 'a declared risk\'s "lines" must be a list of "file.patch:N"', "mod.json")
    for key in m:
        if key not in KNOWN_KEYS:
            result.add("warning", "manifest", f'unknown key "{key}" (the game ignores it)', "mod.json")
    return m


# ---- the checks on lines ---------------------------------------------------------------------

def check_script(line, t, r, touched_chests):
    site = t.chest_sites.get((line.target, line.where))
    if site is not None:
        touched_chests.append((site, line))
        if line.expected != site["expected"]:
            r.add("error", "expected", f"{line.place()} is chest {site['flag']:#x}'s contents and holds "
                  f"{site['expected']:#010x} on the disc, not {line.expected:#010x}", line.file, line.number)
            return
        if site["form"] == "imm24":
            if line.value >> 24 != line.expected >> 24:
                r.add("error", "range", f"{line.place()} is chest {site['flag']:#x}'s push instruction: only its low "
                      "24 bits (the item) may change", line.file, line.number)
                return
            new = line.value & 0xFFFFFF
            new_fol = None
        else:
            v = line.value - (1 << 32) if line.value & 0x80000000 else line.value
            new, new_fol = (None, -v) if v < 0 else (v, None)
        if (site["item"] or 0) >= KEY_ITEM_BASE:
            r.add("warning", "key-item", f"chest {site['flag']:#x} on map {line.target} gives a story key item "
                  f"(flag {site['item'] - KEY_ITEM_BASE}); changing it can make the story impossible to finish",
                  line.file, line.number)
        if new == 0 or new_fol == 0:
            r.add("warning", "chest-empty", f"chest {site['flag']:#x} on map {line.target} is emptied", line.file, line.number)
        elif new is not None and new >= KEY_ITEM_BASE:
            r.add("warning", "key-item", f"chest {site['flag']:#x} would give story key flag {new - KEY_ITEM_BASE}",
                  line.file, line.number)
        elif new is not None and t.item_name(new) is None:
            r.add("warning", "item-unknown", f"chest {site['flag']:#x} would hold item {new:#x}, which has no name in "
                  "the tables", line.file, line.number)
        return
    if (line.target, line.where) in t.key_gift_words:
        r.add("warning", "key-item", f"{line.place()} is the story key item a gift gives (flag "
              f"{t.key_gift_words[(line.target, line.where)]}); changing it can make the story impossible to finish",
              line.file, line.number)
        return
    door, k = t.door_at(line.target, line.where)
    if door is not None:
        mask, known = door_words(door)[k]
        if line.expected & mask != known:
            r.add("error", "expected", f"{line.place()} is part of the door to map {door['to']['map']} and doesn't hold "
                  f"{line.expected:#010x} on the disc", line.file, line.number)
            return
        if k == 0 and line.value & 0xFFFF0000 != line.expected & 0xFFFF0000:
            r.add("error", "range", f"{line.place()} is a door's instruction: only its low 16 bits (the target map) "
                  "may change", line.file, line.number)
            return
        target = line.value & 0xFFFF if k == 0 else door["to"]["map"]
        if k == 0 and target not in t.field_maps:
            r.add("warning", "door-missing-map", f"the door on map {line.target} to map {door['to']['map']} would lead "
                  f"to map {target}, which isn't a field map: the game would fail there", line.file, line.number)
        r.add("warning", "door-retarget", f"the door on map {line.target} to map {door['to']['map']} is changed "
              f"({'target map' if k == 0 else 'arrival position'}); check the party can't be stranded and the story "
              "can go on", line.file, line.number)
        return
    r.add("warning", "script-unknown", f"{line.place()} isn't a chest's contents or a door's target, so the checker "
          "can't tell what it does: the maintainer reads it by hand", line.file, line.number)


def check_item(line, t, r):
    name = t.item_name(line.target)
    if name is None:
        r.add("warning", "item-unknown", f"item {line.target:#x} has no name in the tables", line.file, line.number)
    if not line.field:
        r.add("warning", "raw-field", f"{line.place()}: a raw offset the checker has no name for", line.file, line.number)
    if line.field == "price":
        if line.value == 0:
            r.add("warning", "price-zero", f"{name or line.target}'s price becomes 0: free in shops, worth nothing sold",
                  line.file, line.number)
        elif line.value > PRICE_LIMIT:
            r.add("warning", "price-high", f"{name or line.target}'s price {line.value} is over "
                  f"{PRICE_LIMIT:,} (buying 99 overflows{' and over the Fol cap' if line.value > FOL_CAP else ''})",
                  line.file, line.number)
    if line.field == "sell" and line.value > 100:
        r.add("warning", "sell-over-price", f"{name or line.target} would sell for {line.value}% of its price: buying "
              "and selling makes unlimited Fol", line.file, line.number)
    if line.field in ("effect", "type"):
        r.add("warning", "item-behaviour", f"{name or line.target}'s {line.field} changes {line.expected} -> "
              f"{line.value}: how it works in menus and battle", line.file, line.number)


def check_shop(line, t, r, removed):
    stock = t.shops.get(line.target)
    if stock is None:
        r.add("error", "range", f"there is no shop {line.target} (shops: {min(t.shops)}-{max(t.shops)}, some missing)",
              line.file, line.number)
        return
    if line.where < len(stock):
        if line.expected != stock[line.where]:
            r.add("error", "expected", f"shop {line.target} entry {line.where} holds item {stock[line.where]:#x} "
                  f"({t.item_name(stock[line.where]) or '?'}) on the disc, not {line.expected:#x}", line.file, line.number)
            return
        if line.value == 0:
            removed.append((line, stock[line.where]))
    elif line.where == len(stock) and line.expected == 0:
        pass  # an append
    else:
        r.add("error", "range", f"shop {line.target} has {len(stock)} entries: replace one of 0-{len(stock) - 1} "
              f"(EXPECTED its item), or append at {len(stock)} with EXPECTED 0", line.file, line.number)
        return
    if line.value != 0 and t.item_name(line.value) is None:
        r.add("warning", "item-unknown", f"shop {line.target} would sell item {line.value:#x}, which has no name in the "
              "tables", line.file, line.number)


def check_enemy(line, t, r):
    enemy = t.enemies.get(line.target)
    if enemy is None:
        r.add("error", "range", f"there is no enemy type {line.target} in the encounter packs", line.file, line.number)
        return
    name = enemy["name"]
    drops = enemy.get("drops") or []
    known = {"hp": enemy.get("hp"), "level": enemy.get("level"), "exp": enemy.get("exp"), "fol": enemy.get("fol")}
    for k in (0, 1):
        if k < len(drops):
            known[f"drop{k + 1}"] = drops[k]["item"]
            known[f"drop{k + 1}_odds"] = drops[k]["odds"]
    if line.field in known and known[line.field] is not None and line.expected != known[line.field]:
        r.add("error", "expected", f"{name} (type {line.target}) has {line.field} {known[line.field]} on the disc, not "
              f"{line.expected}", line.file, line.number)
        return
    if not line.field:
        r.add("warning", "raw-field", f"{line.place()}: a raw offset the checker has no name for", line.file, line.number)
    boss = line.target in t.boss_types
    if line.field in ("hp", "exp") and line.value == 0 and boss:
        r.add("warning", "boss-zero", f"{name} is a boss; its {line.field} becomes 0", line.file, line.number)
    elif line.field == "hp" and line.value == 0:
        r.add("warning", "enemy-zero-hp", f"{name}'s HP becomes 0", line.file, line.number)
    if line.field in ("drop1_odds", "drop2_odds") and line.value > 100:
        r.add("warning", "drop-odds", f"{name}'s {line.field} {line.value} is over 100", line.file, line.number)
    if line.field in ("drop1", "drop2") and line.value != 0 and t.item_name(line.value) is None:
        r.add("warning", "item-unknown", f"{name} would drop item {line.value:#x}, which has no name in the tables",
              line.file, line.number)


def same_place(a, b):
    if a.kind != b.kind or a.target != b.target:
        return False
    if a.kind in ("script", "shop"):
        return a.where == b.where and not (a.kind == "shop" and (a.expected == 0 or b.expected == 0))
    return a.where < b.where + b.size and b.where < a.where + a.size


def check_lines(lines, t, r):
    touched_chests, removed = [], []
    for line in lines:
        if line.kind == "script":
            if line.where >= SCRIPT_WORD_LIMIT:
                r.add("error", "range", f"word {line.where:#x} is past the largest script on the disc", line.file, line.number)
            elif line.target not in t.field_maps:
                r.add("error", "range", f"map {line.target} isn't a field map", line.file, line.number)
            else:
                check_script(line, t, r, touched_chests)
        elif line.kind == "item":
            check_item(line, t, r)
        elif line.kind == "shop":
            check_shop(line, t, r, removed)
        else:
            check_enemy(line, t, r)
    # Two lines of the package at one place: the second must build on the first.
    for i, a in enumerate(lines):
        for b in lines[i + 1:]:
            if not same_place(a, b):
                continue
            if (a.where, a.size) == (b.where, b.size) and b.expected == a.value:
                continue  # builds on it
            if (a.where, a.size, a.expected, a.value) == (b.where, b.size, b.expected, b.value):
                r.add("warning", "same-place", f"{b.label} repeats {a.label}", b.file, b.number)
                continue
            r.add("error", "same-place", f"{b.label} changes {b.place()} too, after {a.label}: it would never apply "
                  f"(the engine finds {a.value:#x} there, not {b.expected:#x})", b.file, b.number)
    # A chest on several maps patched on only some of them.
    patched = {(s["flag"], tuple(s["all"])) for s, _ in touched_chests}
    seen_keys = {(line.target, line.where) for _, line in touched_chests}
    for flag, keys in patched:
        missing = [k for k in keys if k not in seen_keys]
        if missing:
            first = next(line for s, line in touched_chests if s["flag"] == flag)
            r.add("warning", "chest-other-sites", f"chest {flag:#x} is also on " +
                  ", ".join(f"map {m} (word {w:#x})" for m, w in missing) +
                  ": patch those too, or the chest differs between versions of the place", first.file, first.number)
    # An item removed from the only shop that sold it.
    for line, item in removed:
        appended = any(l.kind == "shop" and l.value == item for l in lines)
        sellers = [s for s, stock in t.shops.items() if item in stock and s != line.target]
        if not sellers and not appended:
            r.add("warning", "shop-only-source", f"shop {line.target} is the only shop selling item {item:#x} "
                  f"({t.item_name(item) or '?'}); removing it leaves none", line.file, line.number)


def apply_declarations(r):
    risks = r.manifest.get("declared_risks", []) if isinstance(r.manifest, dict) else []
    used = [False] * len(risks)
    for f in r.findings:
        if f.level != "warning" or f.rule not in RISKY:
            continue
        for k, d in enumerate(risks):
            if not isinstance(d, dict) or d.get("rule") != f.rule or not isinstance(d.get("reason"), str):
                continue
            lines = d.get("lines")
            if isinstance(lines, list) and lines and f.where() not in lines:
                continue
            f.level, f.reason = "declared", d["reason"].strip()
            used[k] = True
            break
    for k, d in enumerate(risks):
        if not used[k] and isinstance(d, dict) and d.get("rule") in RISKY:
            r.add("warning", "declared", f'declared risk "{d["rule"]}" matches no line', "mod.json")


def check_package(path, tables, repo_version, in_registry=False):
    path = os.path.normpath(path)
    r = Result(path)
    files = read_package(path, r)
    if files is None:
        return r
    if "mod.json" not in files:
        r.add("error", "files", "no mod.json")
        return r
    if len(files["mod.json"]) > MAX_MANIFEST_BYTES:
        r.add("error", "size", "mod.json is larger than 16 KiB", "mod.json")
        return r
    folder_name = os.path.basename(path) if in_registry and os.path.isdir(path) else None
    m = check_manifest(files["mod.json"], r, folder_name, repo_version)
    if m is None:
        return r
    r.manifest = m
    r.mod_id = m.get("id", "") if isinstance(m.get("id"), str) else ""
    listed = [p for p in m.get("patches", []) if isinstance(p, str)] if isinstance(m.get("patches"), list) else []
    total = 0
    for name, data in files.items():
        total += len(data)
        if name == "mod.json":
            continue
        if name == "README.md":
            limit = MAX_README_BYTES
        elif name in listed:
            limit = MAX_PATCH_BYTES
        elif name.endswith(".patch"):
            r.add("error", "files", f"{name} isn't listed in mod.json's \"patches\"", name)
            continue
        else:
            r.add("error", "files", f"{name} isn't allowed in a package (only mod.json, the listed .patch files and "
                  "README.md; no images or programs)", name)
            continue
        if len(data) > limit:
            r.add("error", "size", f"{name} is larger than {limit // 1024} KiB", name)
        if b"\0" in data:
            r.add("error", "text", f"{name} isn't a text file", name)
            continue
        try:
            data.decode("utf-8")
        except UnicodeDecodeError:
            r.add("error", "text", f"{name} isn't UTF-8 text", name)
    if total > MAX_PACKAGE_BYTES:
        r.add("error", "size", "the package is larger than 1 MiB")
    for name in listed:
        if name not in files:
            r.add("error", "files", f"{name} (listed in mod.json) is missing", "mod.json")
    for name in listed:
        data = files.get(name)
        if data is None or b"\0" in data:
            continue
        for number, text in enumerate(data.decode("utf-8", "replace").split("\n"), 1):
            try:
                line = parse_line(text, name, number)
            except ParseError as e:
                r.add("error", "grammar", f"{e}: {text.strip()}", name, number)
                continue
            if line is not None:
                r.lines.append(line)
    if not r.lines and r.count("error") == 0:
        r.add("error", "files", "the package's patch files hold no patch lines")
    check_lines(r.lines, tables, r)
    apply_declarations(r)
    return r


# ---- output ----------------------------------------------------------------------------------

def summary_counts(r):
    kinds = {}
    for line in r.lines:
        kinds[line.kind] = kinds.get(line.kind, 0) + 1
    return ", ".join(f"{n} {k}" for k, n in kinds.items()) or "no lines"


def print_text(results):
    for r in results:
        verdict = "FAIL" if r.count("error") else ("WARN" if r.count("warning") else "OK")
        name = f"{r.mod_id} " if r.mod_id else ""
        print(f"{verdict} {name}({r.path}): {len(r.lines)} line(s) ({summary_counts(r)}); "
              f"{r.count('error')} error(s), {r.count('warning')} warning(s), {r.count('declared')} declared")
        order = {"error": 0, "warning": 1, "declared": 2, "info": 3}
        for f in sorted(r.findings, key=lambda f: (order[f.level], f.file, f.line)):
            where = f" {f.where()}:" if f.file else ""
            reason = f" (declared: {f.reason})" if f.reason else ""
            print(f"  {f.level} [{f.rule}]{where} {f.message}{reason}")


def github_output(results):
    for r in results:
        for f in r.findings:
            if f.level not in ("error", "warning"):
                continue
            file = os.path.relpath(os.path.join(r.path, f.file), ROOT) if f.file and os.path.isdir(r.path) else r.path
            loc = f"file={file}" + (f",line={f.line}" if f.line else "")
            msg = f.message.replace("%", "%25").replace("\r", "%0D").replace("\n", "%0A")
            print(f"::{f.level} {loc},title=mod_check {f.rule}::{msg}")
    summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if not summary:
        return
    with open(summary, "a", encoding="utf-8") as out:
        out.write("## Mod check\n\n| Mod | Lines | Errors | Warnings | Declared |\n|---|---|---|---|---|\n")
        for r in results:
            out.write(f"| {r.mod_id or r.path} | {len(r.lines)} ({summary_counts(r)}) | {r.count('error')} | "
                      f"{r.count('warning')} | {r.count('declared')} |\n")
        for r in results:
            if not r.findings:
                continue
            out.write(f"\n### {r.mod_id or r.path}\n\n")
            for f in r.findings:
                reason = f" — declared: {f.reason}" if f.reason else ""
                out.write(f"- **{f.level}** `{f.rule}` {f.where()}: {f.message}{reason}\n")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("packages", nargs="*")
    ap.add_argument("--all", action="store_true", help="every folder under mods/")
    ap.add_argument("--json", action="store_true")
    ap.add_argument("--github", action="store_true")
    ap.add_argument("--strict", action="store_true", help="warnings fail too")
    a = ap.parse_args(argv)
    paths = list(a.packages)
    registry = os.path.join(ROOT, "mods")
    if a.all:
        paths += sorted(os.path.relpath(os.path.join(registry, d)) for d in os.listdir(registry)
                        if os.path.isdir(os.path.join(registry, d)))
    if not paths:
        ap.error("name a package, or --all")
    tables = Tables()
    version = port_version()
    results = []
    for p in paths:
        in_registry = os.path.dirname(os.path.abspath(p)) == os.path.abspath(registry)
        results.append(check_package(p, tables, version, in_registry))
    if a.all:  # ids are unique across the registry
        seen = {}
        for r in results:
            if r.mod_id and r.mod_id in seen:
                r.add("error", "manifest", f"id {r.mod_id} is also {seen[r.mod_id]}'s")
            seen.setdefault(r.mod_id, r.path)
    if a.json:
        json.dump([{"path": r.path, "id": r.mod_id, "lines": len(r.lines),
                    "findings": [f.__dict__ for f in r.findings]} for r in results], sys.stdout, indent=1)
        print()
    else:
        print_text(results)
    if a.github:
        github_output(results)
    failed = any(r.count("error") or (a.strict and r.count("warning")) for r in results)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
