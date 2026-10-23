#!/usr/bin/env python3
"""Placeholder patches for Shattered Stars seeds (.apso2): a placement -> script patch lines.

A shuffled chest or gift gets a *placeholder*: a value no game item or Fol amount has, which
a patch line puts where the location's original item (or Fol) is pushed. When the game reaches
that give, the port (engine/src/game/ap/ap.cpp: on_give_item / on_give_money) gives the
location's own item or sends another player's, records the check and shows "Got X." /
"Sent X to Y." in the window the script opens. docs/ap/seed_format.md describes the seed;
docs/ap/field.md the sites.

This module is the generator side. It is pure Python (3.8+, stdlib only), reads no disc and
imports nothing from tools/, so the APWorld can ship it (apworld/so2/). The API:

    placeholder_for(n, role) -> int
    placeholder_patches(sites, placeholder) -> list[str]
    placement_patches(locations, start=0) -> list[str]

A *site* is one patched word:

    {"map": int, "word": int, "expected": int, "form": "imm24" | "word" | "inline",
     "role": "item" | "fol"}

- word: the word index in the map's script code (byte offset / 4), already resolved:
  - imm24 (push imm24, op 0x12): value_at / 4;
  - word (push word, op 0x13): value_at / 4 + 1 (the word after the push);
  - inline give_item (B 0x60): value_at / 4 (the item is in bits 0-15);
  - inline add_money (ext 0xd4, 3 arguments): value_at / 4 + 1 (the amount is that word).
- expected: the 32-bit word the disc holds there (the port skips a line whose word differs).
- role: the routine that reads the value: "item" (a chest's value, the give routine,
  give_item) or "fol" (the money routine, add_money). A chest that held Fol is "item": its
  negative value becomes an item placeholder, so the chest takes its item branch.

The CLI fills a hand-written seed for tests (see main()).
"""

import argparse
import json
import os
import struct
import sys

ITEM_PLACEHOLDER_FIRST = 0x400      # .. 0x270f: item routines. No game item has such a number.
ITEM_PLACEHOLDER_LAST = 0x270F
FOL_PLACEHOLDER_FIRST = 0x00F00000  # .. 0x00ffffff: Fol routines. Fits the money routine's 24-bit
FOL_PLACEHOLDER_LAST = 0x00FFFFFF   # push; no script gives that much Fol.
# (The port also reads 0x7f000000-0x7fffffff as Fol placeholders: full-word sites only.)

FORMS = ("imm24", "word", "inline")
ROLES = ("item", "fol")

OP_PUSH_IMM24 = 0x12
OP_TABLE_B = 0xFF
B_GIVE_ITEM = 0x60


def placeholder_for(n, role):
    """The n-th placeholder (n from 0) of a role: "item" -> 0x400 + n, "fol" -> 0xf00000 + n.
    Raises ValueError past the range."""
    if role not in ROLES:
        raise ValueError(f"role {role!r}: 'item' or 'fol' expected")
    if n < 0:
        raise ValueError(f"placeholder index {n} < 0")
    first, last = ((ITEM_PLACEHOLDER_FIRST, ITEM_PLACEHOLDER_LAST) if role == "item"
                   else (FOL_PLACEHOLDER_FIRST, FOL_PLACEHOLDER_LAST))
    if first + n > last:
        raise ValueError(f"placeholder index {n}: past the {role} range ({last - first + 1} values)")
    return first + n


def role_of_placeholder(value):
    """'item', 'fol' or None (the ranges the port reads)."""
    if ITEM_PLACEHOLDER_FIRST <= value <= ITEM_PLACEHOLDER_LAST:
        return "item"
    if FOL_PLACEHOLDER_FIRST <= value <= FOL_PLACEHOLDER_LAST or 0x7F000000 <= value <= 0x7FFFFFFF:
        return "fol"
    return None


def patched_word(site, placeholder):
    """The word a site holds with the placeholder in it. Raises ValueError when the site's
    expected word is not the instruction its form names, or the placeholder doesn't fit."""
    form, role = site.get("form"), site.get("role")
    expected = int(site["expected"]) & 0xFFFFFFFF
    where = f"map {site.get('map')} word {int(site.get('word', -1)):#x}"
    if form not in FORMS:
        raise ValueError(f"{where}: form {form!r}: one of {FORMS} expected")
    if role not in ROLES:
        raise ValueError(f"{where}: role {role!r}: 'item' or 'fol' expected")
    if role_of_placeholder(placeholder) != role:
        raise ValueError(f"{where}: placeholder {placeholder:#x} is not in the {role} range")
    if form == "imm24":
        if expected >> 24 != OP_PUSH_IMM24:
            raise ValueError(f"{where}: {expected:#010x} is not a push imm24")
        if placeholder > 0xFFFFFF:
            raise ValueError(f"{where}: placeholder {placeholder:#x} does not fit a 24-bit push")
        return (expected & 0xFF000000) | placeholder
    if form == "word":
        return placeholder & 0xFFFFFFFF
    # inline
    if role == "item":
        # give_item(item): op 0xff, table B sub-command 0x60, bit 23 clear (inline); the item
        # is bits 0-15, read as an s16.
        if expected >> 24 != OP_TABLE_B or (expected >> 16) & 0x7F != B_GIVE_ITEM or expected & 0x800000:
            raise ValueError(f"{where}: {expected:#010x} is not an inline give_item")
        if placeholder > 0x7FFF:
            raise ValueError(f"{where}: placeholder {placeholder:#x} does not fit 16 bits")
        return (expected & 0xFFFF0000) | placeholder
    # add_money(_, lo, hi): the amount is this whole word.
    return placeholder & 0xFFFFFFFF


def patch_line(site, placeholder):
    """One mods line, as tools/so2_mod.py writes them."""
    value = patched_word(site, placeholder)
    return (f"script {int(site['map'])} {int(site['word']):#x} {int(site['expected']) & 0xFFFFFFFF:#010x} "
            f"{value:#010x}")


def placeholder_patches(sites, placeholder):
    """One "script MAP WORD EXPECTED VALUE" line per site (a chest on two map versions has
    two sites; a gift one)."""
    if not sites:
        raise ValueError("a placeholder needs at least one site")
    return [patch_line(s, placeholder) for s in sites]


def placement_patches(locations, start=0):
    """Give each location with "sites" a placeholder (in place: location["placeholder"]) and
    return all their patch lines. A location that has a placeholder already keeps it. New ones
    are placeholder_for(n, role) for n = start, start + 1, ..., skipping values in use. All
    sites of one location must share a role (one placeholder serves them all).

    Gifts need a placeholder even when they hold their own original item: the port checks a
    gift only through its placeholder (a chest also by its opened flag)."""
    lines = []
    used = {int(l["placeholder"]) for l in locations if l.get("placeholder") is not None}
    n = start
    for loc in locations:
        sites = loc.get("sites") or []
        if not sites:
            continue
        roles = {s.get("role") for s in sites}
        if len(roles) != 1:
            raise ValueError(f"location {loc.get('id')}: sites with roles {sorted(map(str, roles))}")
        role = roles.pop()
        if loc.get("placeholder") is None:
            while True:
                value = placeholder_for(n, role)
                n += 1
                if value not in used:
                    break
            loc["placeholder"] = value
            used.add(value)
        lines += placeholder_patches(sites, int(loc["placeholder"]))
    return lines


# ---- CLI (tests; not used by the APWorld) ----

def _num(v):
    return int(v, 0) if isinstance(v, str) else int(v)


def site_of(map_id, s):
    """A field.json site as a patch site: its word / expected / role when it has them (the
    regenerated field.json), else built from value_at / form ("expected" None: the CLI reads
    it from the disc)."""
    m = s.get("map", map_id)
    if "word" in s and "expected" in s and "role" in s:
        return {"map": m, "word": s["word"], "expected": s["expected"], "form": s["form"], "role": s["role"]}
    role = "fol" if s.get("what") == "fol" else "item"
    w = s["value_at"] // 4
    if s["form"] == "word" or (s["form"] == "inline" and role == "fol"):
        w += 1
    return {"map": m, "word": w, "expected": None, "form": s["form"], "role": role}


def field_index(field):
    """{location id: [sites]} from field.json (docs/ap/field.md). A chest's location covers
    every map version of its opened flag (its "sites"); a gift, Fol gift or prize site with
    its own location_id is a location by itself, else the record's location_id covers its
    item / Fol sites. Key-item flag sites (no word) are left out."""
    idx = {}
    for r in field.get("chests", []):
        if r.get("location_id") is None:
            continue
        sites = r.get("sites") or [dict(r, what="item")]
        idx[r["location_id"]] = [site_of(r["map"], x) for x in sites]
    for key in ("gifts", "fol_gifts", "prize_tables"):
        for r in field.get(key, []):
            plain = []
            for x in r.get("sites", []):
                if x.get("what") not in ("item", "fol"):
                    continue
                if x.get("location_id") is not None:
                    idx[x["location_id"]] = [site_of(r["map"], x)]
                else:
                    plain.append(site_of(r["map"], x))
            if plain and r.get("location_id") is not None and r["location_id"] not in idx:
                idx[r["location_id"]] = plain
    return idx


def disc_words(disc_path):
    """A reader of script words from disc 1 (tools/so2_mod.py; CLI only)."""
    here = os.path.dirname(os.path.abspath(__file__))
    sys.path.insert(0, os.path.join(here, "..", "..", "tools"))
    import so2_mod  # noqa: E402
    import so2_script  # noqa: E402
    disc = so2_mod.Disc(disc_path)
    cache = {}

    def word(map_id, index):
        if map_id not in cache:
            cache[map_id] = so2_script.parse_script(disc.map_script(map_id))["code"]
        return struct.unpack_from("<I", cache[map_id], index * 4)[0]
    return word


def main(argv=None):
    ap = argparse.ArgumentParser(description=(
        "Fill a seed's placeholders and patch lines: each location with 'sites', or one whose id "
        "is in --field (its sites come from field.json), gets a placeholder; the lines go after "
        "the seed's own patches. Sites without 'expected' read it from --disc."))
    ap.add_argument("seed", help="the seed JSON (.apso2) to fill")
    ap.add_argument("-o", "--out", help="write the filled seed here (default: print the patch lines)")
    ap.add_argument("--field", help="docs/ap/field.json, for locations given by id only")
    ap.add_argument("--disc", help="disc 1 (.cue / .bin), for sites without 'expected'")
    ap.add_argument("--start", type=lambda s: int(s, 0), default=0, help="first placeholder index")
    a = ap.parse_args(argv)
    seed = json.load(open(a.seed, encoding="utf-8"))
    idx = field_index(json.load(open(a.field, encoding="utf-8"))) if a.field else {}
    read = None
    locs = seed.get("locations", [])
    for loc in locs:
        check = loc.get("check", {})
        if not loc.get("sites") and check.get("type") in ("chest", "gift") and _num(loc["id"]) in idx:
            loc["sites"] = [dict(x) for x in idx[_num(loc["id"])]]
        for s in loc.get("sites", []):
            if s.get("expected") is None:
                if not a.disc:
                    raise SystemExit(f"location {loc['id']}: a site without 'expected' needs --disc")
                read = read or disc_words(a.disc)
                s["expected"] = read(int(s["map"]), int(s["word"]))
    lines = placement_patches(locs, a.start)
    for loc in locs:
        loc.pop("sites", None)
    seed["patches"] = list(seed.get("patches", [])) + lines
    if a.out:
        with open(a.out, "w", encoding="utf-8") as f:
            json.dump(seed, f, indent=1)
            f.write("\n")
        print(f"{len(lines)} patch lines, {sum(1 for l in locs if l.get('placeholder') is not None)} placeholders -> {a.out}")
    else:
        print("\n".join(lines))
    return 0


if __name__ == "__main__":
    sys.exit(main())
