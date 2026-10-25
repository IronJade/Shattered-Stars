#!/usr/bin/env python3
"""Writes apworld/so2/crafts.py (craftsanity's table) from the craft research
(docs/ap/data/craft_results.csv + craft_decode.txt, read off the disc at the addresses
engine/src/game/field/menu_creation.cpp uses; Publishing from the overlay itself). Run from the
repo root.

Publishing's three tables come straight from archive entry 3010 (overlay 0xbc2, loaded at
0x8007e000): analysis/archive/disc1/3010.0.unpacked. analysis/ is derived from the user's own
disc image and is git-ignored (not reproduced here); regenerate it with

    tools/so2_archive.py extract SO2.BIN analysis/archive/disc1 --only 3010

where SO2.BIN is disc 2's hidden archive file (docs/catalog/formats.md, "Disc layout").
"""
import ast, csv, re, struct, sys
sys.path.insert(0, "apworld/so2")
import importlib.util
spec = importlib.util.spec_from_file_location("data", "apworld/so2/data.py")
data = importlib.util.module_from_spec(spec); spec.loader.exec_module(data)

NAMES = ("Claude", "Rena", "Celine", "Bowman", "Dias", "Precis", "Ashton", "Leon", "Opera", "Ernest", "Noel", "Chisato")
who = lambda pid: NAMES[pid - 1]
# The item-making specialties in the menu's order: page 0xf variants 1 Art, 4 Customize,
# 6 Metalwork, 7 Authoring, 10 Compounding, 11 Cooking, 13 Alchemy, 17 Machinery; page 0x10
# (super) 1 Master Chef, 7 Blacksmith, 8 Reverse Side.
ORDER = ["Art", "Customize", "Metalwork", "Authoring", "Compounding", "Cooking", "Alchemy", "Machinery",
         "Master Chef", "Publishing", "Blacksmith", "Reverse Side"]
CSV_NAME = {"Master Chef (super)": "Master Chef", "Blacksmith (super)": "Blacksmith", "Reverse Side (super)": "Reverse Side"}

rows = [r for r in csv.DictReader(open("docs/ap/data/craft_results.csv")) if r["kind"] == "result"]
dump = open("docs/ap/data/craft_decode.txt").read().splitlines()
ids = lambda s: [int(x, 16) for x in re.findall(r"'0x([0-9a-f]{3}) ", s.replace('"', "'"))]

makers = {}     # (specialty, item) -> set of names, or None (anyone)
def give(spec, item, m):
    key = (spec, item)
    if m is None or makers.get(key, set()) is None:
        makers[key] = None
    else:
        makers[key] = makers.get(key, set()) | set(m)

# Customize: char N's tier list and recipe outputs (N 1-7 = party ids; char 0's row is not a table).
cur = None
for line in dump:
    m = re.match(r"char (\d): tiers 0\.\.10 -> (.*)", line)
    if m and m.group(1) != "0":
        for it in ids(m.group(2)):
            give("Customize", it, [who(int(m.group(1)))])
    m = re.match(r"char (\d) recipe list", line)
    if m:
        cur = int(m.group(1)); continue
    m = re.match(r"\s+threshold>\d+ .* -> 0x([0-9a-f]{3}) ", line)
    if m and cur:
        give("Customize", int(m.group(1), 16), [who(cur)])
    if line.startswith("== Cooking"):
        cur = None
# Machinery: the normal list anyone; the rare tables Precis (6) and Opera (9) only.
for line in dump:
    if line.startswith("normal: [("):
        for it in ids(line): give("Machinery", it, None)
    m = re.match(r"char(\d) rare: (.*)", line)
    if m:
        for it in ids(m.group(2)): give("Machinery", it, [who(int(m.group(1)))])
    m = re.match(r"character-8 rare books: (.*)", line)
    if m:
        for it in ids(m.group(1)): give("Authoring", it, [who(8)])
    m = re.match(r"favourite-food character table \(items 6\.\.0x11\): (.*)", line)
    if m:   # Art: a portrait is the one of a random active member (Creation1::compute)
        for k, pid in enumerate(ast.literal_eval(m.group(1))):
            give("Art", 6 + k, [who(pid)])
    m = re.match(r"\s+0x119 Grain \(idx 2\): dishes=(.*?) kinds", line)
    if m:
        grain = ids(m.group(1))
    m = re.match(r"special 3rd-ingredient dishes \(chars 1,2,6,8\): (.*)", line)
    if m:
        special = set(ids(m.group(1)))
# Cooking's Grain: Claude, Rena, Precis and Leon cook from the special list instead of it.
for it in grain:
    if it not in special:
        give("Cooking", it, [n for n in NAMES if n not in ("Claude", "Rena", "Precis", "Leon")])

table = {}
for r in rows:
    spec = CSV_NAME.get(r["specialty"], r["specialty"])
    it = int(r["item_id_hex"], 16)
    if it not in data.ITEMS:
        raise SystemExit(f"{r}: not a game item")
    table.setdefault(it, {})[spec] = makers.get((spec, it), None) if (spec, it) in makers or spec not in (
        "Customize",) else None
    if spec == "Customize" and (spec, it) not in makers:
        print("customize item with no character list:", hex(it), r["item_name"])
# Publishing (super specialty 5, menu_creation.cpp Super5::compute): a book about the party member
# picked (any of the party's 8 slots), from the member table 0x80083d68 (12 party ids) and the
# books 0x80083d30 (common) / 0x80083d4c (rare: 10 % of tries), read from overlay 0xbc2
# (archive entry 3010, loaded at 0x8007e000; analysis/archive/disc1/3010.0.unpacked). The subject
# must be in the party, as for Art's portraits.
ovl = open("analysis/archive/disc1/3010.0.unpacked", "rb").read()
OVL_BASE = 0x8007E000
subjects = list(ovl[0x80083D68 - OVL_BASE:0x80083D68 - OVL_BASE + 12])
if sorted(subjects) != list(range(1, 13)):
    raise SystemExit(f"Publishing's member table is not the 12 members: {subjects}")
for books_at in (0x80083D30, 0x80083D4C):
    for pid, it in zip(subjects, struct.unpack_from("<12H", ovl, books_at - OVL_BASE)):
        if it not in data.ITEMS or it in table:
            raise SystemExit(f"Publishing's book {it:#x}: not a game item, or made by another specialty")
        table[it] = {"Publishing": [who(pid)]}
out = []
for it in sorted(table):
    specs = sorted(table[it], key=ORDER.index)
    ms = [table[it][s] for s in specs]
    m = None if any(x is None for x in ms) else sorted(set().union(*ms), key=NAMES.index)
    out.append({"item": it, "specialty": specs[0], "specialties": specs, "makers": m})
locked = [c for c in out if c["makers"]]
print(len(out), "crafts;", len(locked), "member-locked;",
      {s: sum(1 for c in out if c["specialty"] == s) for s in ORDER})
lines = ['"""Craftsanity\'s table (docs/ap/apworld.md, "Craftsanity"): every item Item Creation can make,',
         "with the specialties that make it (the first names the location) and, when only some members",
         "can make it, who (`makers`, party names; None: anyone). Written by",
         "tools/ap_make_crafts.py from the craft research (docs/ap/data/craft_results.csv and",
         "craft_decode.txt: the specialties' tables read off the disc at the addresses",
         "engine/src/game/field/menu_creation.cpp uses). Member locks: Customize (each member's",
         "own weapon list, Claude to Ashton), Machinery's rare tables (Precis, Opera), Authoring's rare",
         "books (Leon), Art's portraits (the portrayed member must be in the active party), Publishing's",
         "books (overlay 0xbc2: one common and one rare book about each member, who must be in the",
         "party), Cooking's Grain teas (not Claude, Rena, Precis or Leon, who cook from another list).",
         "Metalwork's per-member equip filter is not covered.\"\"\"", "",
         f"SPECIALTIES = {tuple(ORDER)!r}", "", "CRAFTS = ["]
lines += [f"    {c!r}," for c in out]
lines += ["]", ""]
open("apworld/so2/crafts.py", "w", newline="\n").write("\n".join(lines))
