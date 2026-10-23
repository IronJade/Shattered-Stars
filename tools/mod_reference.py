#!/usr/bin/env python3
"""The modder's reference: what every number in a world-edit patch stands for.

  mod_reference.py DISC1.cue [--out docs/modding/reference] [--check]
      writes the facts-only tables (items, maps, doors, chests, shops, enemies as CSV, plus a
      README) read from your disc and the repository's tables; --check only compares them
      with the known tables (docs/ap) and prints the results

The tables hold numbers and names only (IDs, names, types, prices, stats, our own map
names), never the game's prose or pictures, so they can be committed and published. The
local reference with item descriptions and map thumbnails is `so2_mod.py reference DISC --out
DIR` (build_local below): it is made on your machine from your own disc and is not for
redistribution.

Every table is sorted and written the same way each time, so regenerating it changes only
what really changed. Python 3, no third-party modules (Pillow, if installed, shrinks the
local reference's thumbnails).
"""

import argparse
import base64
import collections
import csv
import hashlib
import html
import io
import json
import os
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import so2_data as sd  # noqa: E402
import so2_names as sn  # noqa: E402

ROOT = sn.ROOT
OUT = os.path.join(ROOT, "docs", "modding", "reference")
TOOL = "python3 tools/so2_mod.py"

COLUMNS = {
    "items": ["id", "hex", "name", "category", "type", "use", "price", "atk", "def", "effect", "effect_kind",
              "amount", "sell_pct", "equip"],
    "maps": ["map", "discs", "name", "area", "place", "kind", "doors", "chests", "shops", "save_point",
             "same_script", "name_from"],
    "doors": ["map", "map_name", "index", "target", "target_name", "mode", "x", "y", "z", "dir", "same_script",
              "note", "command"],
    "chests": ["map", "map_name", "flag", "flag_dec", "handler_word", "contents", "item", "fol", "can_hold",
               "also_on", "command"],
    "shops": ["shop", "where", "index", "item", "item_name", "command"],
    "enemies": ["type", "name", "hp", "mp", "exp", "fol", "level", "atk", "def", "int", "speed", "eva", "acc",
                "drop1", "drop1_name", "drop1_odds", "drop2", "drop2_name", "drop2_odds", "boss", "encounters",
                "maps", "areas", "stats_from"],
}


def ranges(nums):
    """'1-3, 7, 9-10' for a set of numbers."""
    nums = sorted(set(nums))
    out, i = [], 0
    while i < len(nums):
        j = i
        while j + 1 < len(nums) and nums[j + 1] == nums[j] + 1:
            j += 1
        out.append(str(nums[i]) if i == j else f"{nums[i]}-{nums[j]}")
        i = j + 1
    return ", ".join(out)


def hexid(n):
    return f"{n:#x}"


# ---- Collecting --------------------------------------------------------------------------

def collect(disc_path, t=None, log=print):
    """{table: [row dicts]} and the extra facts the checks use, read from disc 1 and the repo."""
    t = t or sn.tables()
    disc = sd.Disc(disc_path)
    start = time.time()
    ref = {"tables": {}, "facts": {}}
    tab = ref["tables"]

    # Items.
    shown = sd.item_names(disc)
    rows = []
    for r in sd.item_rows(disc, shown):
        rows.append({"id": r["id"], "hex": hexid(r["id"]), "name": r["name"], "category": r["category"],
                     "type": r["type"], "use": sd.item_use_name(r["use"]), "price": r["price"],
                     "atk": r["atk"], "def": r["def"], "effect": sd.item_effect_name(r["effect"]),
                     "effect_kind": r["effect"], "amount": r["amount"], "sell_pct": r["sell"],
                     "equip": sd.equip_names(r["equip"]) if r["equip"] else ""})
    tab["items"] = rows
    ref["facts"]["item_names"] = shown
    log(f"items: {len(rows)} ({time.time() - start:.0f}s)")

    # Maps: one pass over every script (maps sharing one script are read once).
    maps = disc.field_maps()
    groups = collections.defaultdict(list)
    digest = {}
    for m in maps:
        h = hashlib.sha1(disc.map_script(m)).hexdigest()
        groups[h].append(m)
        digest[m] = h
    per_script = {}
    for h, ids in groups.items():
        primary = next((m for m in ids if m > 0), ids[0])
        blob = disc.map_script(primary)
        try:
            ins = sd.instructions(blob)
        except Exception as e:  # noqa: BLE001 (a script the disassembler can't read)
            log(f"map {primary}: script not read ({e})")
            ins = []
        per_script[h] = {"primary": primary, "maps": sorted(ids), "chests": sd.chests(blob, ins),
                         "doors": sd.transfers(blob, ins), "saves": sd.save_points(blob, ins)}
    log(f"maps: {len(maps)} maps, {len(groups)} scripts ({time.time() - start:.0f}s)")

    shop_maps = collections.defaultdict(set)
    for s, ms in t.shop_maps.items():
        for m in ms:
            shop_maps[m].add(s)

    def same_script(h):
        ids = per_script[h]["maps"]
        return ranges(ids) if len(ids) > 1 else ""

    rows = []
    for m in maps:
        s = per_script[digest[m]]
        info = t.map_info(m)
        rows.append({"map": m, "discs": info["discs"], "name": info["name"], "area": info["area"],
                     "place": info["place"], "kind": info["kind"], "doors": len(s["doors"]),
                     "chests": len(s["chests"]), "shops": " ".join(str(x) for x in sorted(shop_maps.get(m, ()))),
                     "save_point": "yes" if s["saves"] else "", "same_script": same_script(digest[m]),
                     "name_from": info["source"]})
    tab["maps"] = rows

    # Doors (the inline map_transfer_c of each script, as `so2_mod.py list` numbers them).
    rows = []
    for h, s in sorted(per_script.items(), key=lambda kv: kv[1]["primary"]):
        m = s["primary"]
        note = ""
        if m in sn.WORLD_SCRIPT:
            note = "world script: the world maps' warp list"
        elif len(s["maps"]) > 1:
            note = "one script for all maps in same_script: patch each map you want changed"
        for n, ins, a in s["doors"]:
            rows.append({"map": m, "map_name": t.map_name(m), "index": n, "target": a[0],
                         "target_name": t.map_name(a[0]), "mode": a[1], "x": a[2], "y": a[3], "z": a[4],
                         "dir": a[5], "same_script": same_script(h), "note": note,
                         "command": f"{TOOL} door DISC.cue {m} {n} --to NEW_MAP"})
    tab["doors"] = rows

    # Chests.
    by_flag = collections.defaultdict(set)
    for s in per_script.values():
        for flag, _, _ in s["chests"]:
            by_flag[flag].update(s["maps"])
    rows = []
    for h, s in sorted(per_script.items(), key=lambda kv: kv[1]["primary"]):
        for m in s["maps"]:
            for flag, handler, ins in sorted(s["chests"], key=lambda c: c[0]):
                v = sd.push_value(ins)
                item_form = ins[1][0] >> 24 == 0x12
                rows.append({"map": m, "map_name": t.map_name(m), "flag": hexid(flag), "flag_dec": flag,
                             "handler_word": hexid(handler),
                             "contents": f"{-v} Fol" if v < 0 else shown.get(v, t.item_name(v)),
                             "item": hexid(v) if v >= 0 else "", "fol": -v if v < 0 else "",
                             "can_hold": "items" if item_form else "items or Fol",
                             "also_on": " ".join(str(x) for x in sorted(by_flag[flag] - {m})),
                             "command": f"{TOOL} chest DISC.cue {m} {flag:#x} " + ("--item ITEM" if v >= 0 else "--fol N")})
    rows.sort(key=lambda r: (r["map"], r["flag_dec"]))
    tab["chests"] = rows

    # Shops.
    lists = sd.shop_lists(disc)
    rows = []
    for shop in sorted(lists):
        where = "; ".join(f"map {m} {t.map_name(m)}" for m in sorted(t.shop_maps.get(shop, ())))
        for n, item in enumerate(lists[shop]):
            rows.append({"shop": shop, "where": where, "index": n, "item": hexid(item),
                         "item_name": shown.get(item, t.item_name(item)),
                         "command": f"{TOOL} shop DISC.cue {shop} {n} ITEM"})
    tab["shops"] = rows
    ref["facts"]["shop_lists"] = {s: v for s, v in lists.items() if v}
    log(f"doors {len(tab['doors'])}, chests {len(tab['chests'])}, shop entries {len(rows)} ({time.time() - start:.0f}s)")

    # Enemies: stats from the first encounter pack holding each type; where each appears.
    packs = sd.encounter_types(disc)
    first, encs = {}, collections.defaultdict(set)
    for n in sorted(packs):
        for typ, rec in packs[n]:
            first.setdefault(typ, (n, rec))
            encs[typ].add(n)
    enc_maps, enc_worlds = collections.defaultdict(set), collections.defaultdict(set)
    groups_ = {int(k): v for k, v in t.field.get("encounter_groups", {}).items()}
    for site in t.field.get("encounter_sites", []):
        for e in groups_.get(site["group"], []):
            enc_maps[e].add(site["map"])
    for w, es in t.field.get("world_map_groups", {}).items():
        for e in es:
            enc_worlds[e].add(sn.WORLDS.get(int(w), f"world {w}") + " world map")
    for b in t.battles.get("scripted_battles", []) + t.battles.get("battle_preloads", []):
        if isinstance(b.get("encounter"), int):
            enc_maps[b["encounter"]].add(b["map"])
    bosses = set()
    for e in t.battles.get("encounters", {}).values():
        for mem in e.get("members", []):
            if mem.get("boss"):
                bosses.add(mem["type"])
    rows = []
    f = sd.ENEMY_FIELDS
    for typ in sorted(first):
        n, rec = first[typ]
        v = {k: sd.le(rec, o, size) for k, (o, size) in f.items()}
        ms, worlds = set(), set()
        for e in encs[typ]:
            ms |= enc_maps.get(e, set())
            worlds |= enc_worlds.get(e, set())
        areas = sorted({t.map_info(m)["area"] or t.map_name(m) for m in ms} | worlds)
        rows.append({"type": typ, "name": sd.enemy_name(rec), "hp": v["hp"], "mp": v["mp"], "exp": v["exp"],
                     "fol": v["fol"], "level": v["level"], "atk": v["atk"], "def": v["def"], "int": v["int"],
                     "speed": v["speed"], "eva": v["eva"], "acc": v["acc"],
                     "drop1": hexid(v["drop1"]) if v["drop1"] else "",
                     "drop1_name": shown.get(v["drop1"], "") if v["drop1"] else "",
                     "drop1_odds": v["drop1_odds"] if v["drop1"] else "",
                     "drop2": hexid(v["drop2"]) if v["drop2"] else "",
                     "drop2_name": shown.get(v["drop2"], "") if v["drop2"] else "",
                     "drop2_odds": v["drop2_odds"] if v["drop2"] else "",
                     "boss": "yes" if typ in bosses else "", "encounters": ranges(encs[typ]),
                     "maps": ranges(ms), "areas": "; ".join(areas), "stats_from": n})
    tab["enemies"] = rows
    log(f"enemies: {len(rows)} types ({time.time() - start:.0f}s)")
    ref["facts"]["per_script"] = per_script
    return ref


# ---- Checks against the known tables -------------------------------------------------------

def check(ref, t=None):
    """[(ok, text)] comparing the tables with docs/ap's (made by other tools, other code)."""
    t = t or sn.tables()
    tab, out = ref["tables"], []

    def add(ok, text):
        out.append((bool(ok), text))
    items = tab["items"]
    add(len(items) == 1023, f"items: {len(items)} rows (1023 expected)")
    diff = [r["id"] for r in items if r["name"] != t.item_names.get(r["id"])]
    add(not diff, f"item names: {len(items) - len(diff)} match docs/ap/field.json item_names" +
        (f"; differ: {diff[:8]}" if diff else ""))
    ap = sn._apworld_items()
    if ap:
        same = sum(1 for r in items if ap.get(r["id"], "").startswith(r["name"]))
        add(same == len(items), f"item names: {same} match the APWorld's ITEMS (shared names carry the id there)")
    maps = tab["maps"]
    known = t.field.get("field_maps", [])
    add(len(maps) == len(known) and {r["map"] for r in maps} == set(known),
        f"maps: {len(maps)} rows ({len(known)} field maps in docs/ap/field.json)")
    chests = tab["chests"]
    flags = {r["flag_dec"] for r in chests}
    ap_chests = t.field.get("chests", [])
    add(len(flags) == len(ap_chests), f"chests: {len(chests)} chest sites, {len(flags)} chests (opened flags); "
        f"docs/ap has {len(ap_chests)}")
    ap_sites = {(s["map"], c["flag"]) for c in ap_chests for s in c.get("sites", [c])}
    ours = {(r["map"], r["flag_dec"]) for r in chests}
    add(ours == ap_sites, f"chest sites: {len(ours & ap_sites)} shared with docs/ap; only here {sorted(ours - ap_sites)[:6]}, "
        f"only there {sorted(ap_sites - ours)[:6]}")
    ap_val = {(s["map"], c["flag"]): (s.get("item") if s.get("role") == "item" else None) for c in ap_chests
              for s in c.get("sites", [c])}
    bad = [(r["map"], r["flag"]) for r in chests if r["item"] and ap_val.get((r["map"], r["flag_dec"])) not in
           (None, int(r["item"], 16))]
    add(not bad, f"chest contents: item chests agree with docs/ap" + (f"; differ: {bad[:6]}" if bad else ""))
    ap_shops = {int(k): v["stock"] for k, v in t.field.get("shops", {}).items()}
    lists = ref["facts"]["shop_lists"]
    add(lists == ap_shops, f"shops: {len(lists)} shops with stock, {len(tab['shops'])} entries; docs/ap has "
        f"{len(ap_shops)} shops" + ("" if lists == ap_shops else
                                     f"; differ: {sorted(set(lists) ^ set(ap_shops))[:6]}"))
    types = t.battles.get("enemy_types", {})
    en = tab["enemies"]
    names = sum(1 for r in en if types.get(str(r["type"]), {}).get("name") == r["name"])
    stats = sum(1 for r in en if all(types.get(str(r["type"]), {}).get(k) == r[k] for k in ("hp", "level", "exp", "fol")))
    add(len(en) >= len(types) and names == len(types) and stats == len(types),
        f"enemies: {len(en)} types; docs/ap/battles.json has {len(types)}: {names} names and {stats} stat rows agree")
    doors_json = os.path.join(ROOT, "docs", "ap", "doors.json")
    if os.path.exists(doors_json):
        dj = json.load(open(doors_json))["doors"]
        inline = collections.Counter(d["map"] for d in dj if d["op"] == "map_transfer_c" and d["form"] == "inline")
        ours_d = collections.Counter(r["map"] for r in tab["doors"])
        add(inline == ours_d, f"doors: {len(tab['doors'])} retargetable doors; docs/ap/doors.json has "
            f"{sum(inline.values())} inline map_transfer_c")
    saves = sum(1 for r in maps if r["save_point"])
    add(671 in {r["map"] for r in maps if r["save_point"]}, f"save points: {saves} maps (671, the Final Save Point, among them)")
    return out


# ---- Writing ---------------------------------------------------------------------------------

def write_csv(path, columns, rows):
    """Writes rows (dicts) in a fixed column order, '\\n' line ends, UTF-8."""
    buf = io.StringIO()
    w = csv.DictWriter(buf, fieldnames=columns, lineterminator="\n", extrasaction="ignore")
    w.writeheader()
    for r in rows:
        w.writerow({k: r.get(k, "") for k in columns})
    text = buf.getvalue()
    old = None
    if os.path.exists(path):
        with open(path, encoding="utf-8") as f:
            old = f.read()
    if old != text:
        with open(path, "w", encoding="utf-8", newline="") as f:
            f.write(text)
    return old != text


def read_csv(path):
    with open(path, newline="", encoding="utf-8") as f:
        return list(csv.DictReader(f))


README = """# Modder's reference tables

What the numbers in a world-edit patch stand for. Made by `tools/mod_reference.py` from the
disc and the repository's own tables: IDs, names, types, prices, stats and our own map names
only, never the game's text or pictures. Every table is sorted the same way each time it is
made.

The map names come from `map_names.csv` (our own descriptive names, "area: place") where it
names a map, else from the area banners the game shows on entering, else "Map N".

| File | Rows | What it lists |
|---|---|---|
{table_rows}

## How the IDs plug in

Each table's IDs are what `tools/so2_mod.py` takes and what a patch line holds (the grammar
is in `engine/src/game/field/mods.h`). `so2_mod.py` also takes a name wherever it takes an
item, map, shop or enemy type: `--to "Cross: Skill Guild"`, `--item "Blueberry"`. A name that
fits more than one entry lists them and stops; `so2_mod.py find DISC.cue TEXT` searches every
name.

- **items.csv**: `id` / `hex` is the item number (1-1023) for `so2_mod.py item`, `chest
  --item`, `shop` and the enemy fields `drop1` / `drop2`; a patch line `item ID FIELD EXPECTED
  VALUE`. `category` is in words; `type` and `effect_kind` are the record's numbers (the
  values the `type` and `effect` fields take). `use`: where it can be used (battle, field).
  `price` is what a shop asks; `sell_pct` is the percentage of it a shop pays. `amount` is
  the effect's amount (for the restoring kinds, the percentage). `equip`: who can wear or
  wield it.
- **maps.csv**: `map` is the field map number for `so2_mod.py list`, `chest`, `door`, the
  door target `--to`, and a patch line `script MAP WORD EXPECTED VALUE`. `doors` counts the
  doors `so2_mod.py door` can change; `chests` the chests; `shops` the shop numbers whose
  menu the map opens; `save_point` whether the map's own script places one. Maps listed in
  `same_script` run one script: a patch names one map, so repeat it for each map it should
  change. `discs`: the disc the story visits the map on (from the world it belongs to; maps
  open again in Virtual Expel list both; every map's data is on both discs, and the scripts
  are identical). `name_from` says where the name came from.
- **doors.csv**: `map` + `index` is a door for `so2_mod.py door DISC.cue MAP INDEX --to
  NEW_MAP` (`--x --y --z --dir` move the arrival point; the current ones are in the row).
  `target` is the map it leads to now. Only the doors written as one inline instruction
  (map_transfer_c) can be retargeted; the other transfers are not listed.
- **chests.csv**: `flag` is the chest's ID (its opened flag) for `so2_mod.py chest DISC.cue
  MAP FLAG --item ITEM` or `--fol N`. `can_hold`: a chest holding an item can only be
  changed to another item; one holding Fol takes either. `also_on` lists other maps with the
  same chest (one chest seen in several versions of a map): change each.
- **shops.csv**: `shop` + `index` is a stock entry for `so2_mod.py shop DISC.cue SHOP INDEX
  ITEM` (`add` for INDEX appends, ITEM 0 removes); a patch line `shop SHOP INDEX EXPECTED
  VALUE`. `where` names the maps that open the shop.
- **enemies.csv**: `type` is the enemy type for `so2_mod.py enemy DISC.cue TYPE FIELD VALUE`
  and the patch line `enemy TYPE FIELD EXPECTED VALUE`; the stat columns are the field
  names it takes (hp, mp, exp, fol, level, atk, def, int, speed, eva, acc, drop1, drop2,
  drop1_odds, drop2_odds). `encounters` are the encounter numbers it is in; `maps` and
  `areas` where those encounters happen (random encounters, scripted battles and the world
  maps); `stats_from` the encounter its stats were read from (every encounter carries the
  same record).

## The local reference

`python3 tools/so2_mod.py reference DISC.cue --out DIR` builds one HTML page with every table
above, searchable, plus the item descriptions read from your disc; with `--game PATH` (the
built game) it adds a thumbnail of every map, rendered without a window. That page is made
from your own disc, on your machine, and is not for sharing.

Regenerate these tables with `python3 tools/mod_reference.py DISC1.cue` (`--check` compares
them with the Archipelago tables in `docs/ap`).
"""

DESCRIPTIONS = {
    "items": "every item: name, category, price, attack / defence, effect, sell percentage, who can equip it",
    "maps": "every field map: our name, area, kind, door / chest counts, shops, save point",
    "doors": "every door `so2_mod.py door` can change: where it leads, arrival point, the command",
    "chests": "every chest: its flag (ID), what it holds, the command",
    "shops": "every shop's stock, entry by entry, and where the shop is",
    "enemies": "every enemy type: stats, drops, and the encounters, maps and areas it appears in",
}


def write_all(ref, out_dir, log=print):
    os.makedirs(out_dir, exist_ok=True)
    changed = []
    for name, cols in COLUMNS.items():
        if write_csv(os.path.join(out_dir, f"{name}.csv"), cols, ref["tables"][name]):
            changed.append(name)
    rows = "\n".join(f"| `{n}.csv` | {len(ref['tables'][n])} | {DESCRIPTIONS[n]} |" for n in COLUMNS)
    text = README.format(table_rows=rows)
    path = os.path.join(out_dir, "README.md")
    old = None
    if os.path.exists(path):
        with open(path, encoding="utf-8") as f:
            old = f.read()
    if old != text:
        with open(path, "w", encoding="utf-8") as f:
            f.write(text)
        changed.append("README")
    log(f"wrote {out_dir}: " + (", ".join(changed) if changed else "nothing changed"))


# ---- The local reference (descriptions, thumbnails; never committed) -------------------------

def shoot_thumbnails(game, disc_path, maps, thumbs_dir, jobs=4, log=print, frame=650, timeout=90):
    """Renders each map headless with the game's SS_SHOT option into thumbs_dir/MAP.png
    (skipping maps already there). The game runs with its own scratch config folder."""
    thumbs_dir, game = os.path.abspath(thumbs_dir), os.path.abspath(game)
    os.makedirs(thumbs_dir, exist_ok=True)
    cfg = os.path.join(thumbs_dir, ".game-config")
    os.makedirs(cfg, exist_ok=True)
    todo = [m for m in maps if not os.path.exists(os.path.join(thumbs_dir, f"{m}.png"))]
    log(f"thumbnails: {len(maps) - len(todo)} already made, {len(todo)} to render with {jobs} at a time")
    jobs = max(1, min(int(jobs), 6))
    # No window and no sound: SDL's offscreen video and dummy audio drivers (SDL 3 names, and
    # SDL 2's for good measure).
    env = dict(os.environ, SS_DISC1=os.path.abspath(disc_path), XDG_CONFIG_HOME=os.path.abspath(cfg),
               SDL_VIDEO_DRIVER="offscreen", SDL_AUDIO_DRIVER="dummy", SDL_VIDEODRIVER="offscreen",
               SDL_AUDIODRIVER="dummy")
    disc2 = sd.find_disc2(disc_path)
    if disc2:
        env["SS_DISC2"] = disc2
    running, done, failed = {}, 0, []
    queue = list(todo)
    deadline_all = time.time() + max(600, len(todo) * timeout)
    while (queue or running) and time.time() < deadline_all:
        while queue and len(running) < jobs:
            m = queue.pop(0)
            out = os.path.join(thumbs_dir, f"{m}.png")
            e = dict(env, SS_SHOT=f"{frame}:{out}")
            p = subprocess.Popen([game, "--headless", "--no-memcard", "--no-audio", "--map", str(m),
                                  "--frames", str(frame + 10)], env=e, stdout=subprocess.DEVNULL,
                                 stderr=subprocess.DEVNULL, cwd=thumbs_dir)
            running[p.pid] = (p, m, time.time())
        time.sleep(0.2)
        for pid, (p, m, t0) in list(running.items()):
            if p.poll() is not None:
                del running[pid]
                done += 1
                if not os.path.exists(os.path.join(thumbs_dir, f"{m}.png")):
                    failed.append(m)
            elif time.time() - t0 > timeout:
                p.kill()   # this run's own child, by its handle (exact pid)
                p.wait()
                del running[pid]
                failed.append(m)
        if done and done % 50 == 0:
            log(f"thumbnails: {done}/{len(todo)}")
    for p, m, _ in running.values():
        p.kill()
        p.wait()
        failed.append(m)
    if failed:
        log(f"thumbnails: no picture for {len(failed)} maps ({ranges(failed)[:200]})")
    return failed


def thumbnail_uri(path, width=192):
    """A data: URI for the page: a small JPEG with Pillow, else the PNG as it is."""
    try:
        from PIL import Image  # noqa: E402 (optional)
        im = Image.open(path).convert("RGB")
        h = max(1, round(im.height * width / im.width))
        im = im.resize((width, h), Image.LANCZOS)
        buf = io.BytesIO()
        im.save(buf, "JPEG", quality=80)
        return "data:image/jpeg;base64," + base64.b64encode(buf.getvalue()).decode()
    except ImportError:
        return "data:image/png;base64," + base64.b64encode(open(path, "rb").read()).decode()


PAGE = """<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Shattered Stars: local mod reference</title>
<style>
:root { --bg: #fbfbfa; --fg: #1d1d1f; --muted: #6b6b70; --line: #deded9; --head: #f0f0ec;
        --accent: #2f5fb3; --mark: #fff2a8; }
@media (prefers-color-scheme: dark) {
  :root { --bg: #17181a; --fg: #e6e6e3; --muted: #9a9a9f; --line: #2e3033; --head: #202225;
          --accent: #8fb1f0; --mark: #5a4f12; } }
* { box-sizing: border-box; }
body { margin: 0; background: var(--bg); color: var(--fg); font: 14px/1.45 system-ui, sans-serif; }
header { padding: 16px; border-bottom: 1px solid var(--line); }
h1 { font-size: 20px; margin: 0 0 4px; }
.note { color: var(--muted); margin: 0; max-width: 70em; }
nav { display: flex; flex-wrap: wrap; gap: 6px; padding: 12px 16px; position: sticky; top: 0;
      background: var(--bg); border-bottom: 1px solid var(--line); z-index: 2; }
nav button { border: 1px solid var(--line); background: var(--head); color: var(--fg); padding: 5px 12px;
             border-radius: 6px; cursor: pointer; font: inherit; }
nav button.on { border-color: var(--accent); color: var(--accent); font-weight: 600; }
nav input, nav select { font: inherit; padding: 5px 8px; border: 1px solid var(--line); border-radius: 6px;
             background: var(--bg); color: var(--fg); min-width: 0; }
nav input { flex: 1 1 220px; }
.count { color: var(--muted); align-self: center; }
main { padding: 0 16px 32px; overflow-x: auto; }
table { border-collapse: collapse; width: 100%; margin-top: 12px; }
th, td { border-bottom: 1px solid var(--line); padding: 4px 8px; text-align: left; vertical-align: top; }
th { background: var(--head); cursor: pointer; white-space: nowrap; }
td.num { text-align: right; font-variant-numeric: tabular-nums; }
td.desc { color: var(--muted); min-width: 18em; max-width: 30em; }
td code { font-size: 12px; }
img.thumb { width: 192px; height: auto; display: block; border-radius: 4px; }
</style></head><body>
<header><h1>Shattered Stars: local mod reference</h1>
<p class="note">Generated by <code>so2_mod.py reference</code> from your own disc on this computer. It
contains the game's own text{thumb_note}: keep it to yourself and don't share or upload it. The
facts-only tables (IDs, names, stats) are in the repository under
<code>docs/modding/reference/</code> and on the wiki.</p></header>
<nav id="tabs"></nav>
<main><table id="grid"><thead></thead><tbody></tbody></table></main>
<script id="data" type="application/json">{data}</script>
<script>
const D = JSON.parse(document.getElementById('data').textContent);
const tabs = Object.keys(D.tables);
let cur = tabs[0], sortCol = null, sortDir = 1;
const nav = document.getElementById('tabs');
const q = document.createElement('input'); q.placeholder = 'Search (name, number, place...)'; q.type = 'search';
const sel = document.createElement('select');
const cnt = document.createElement('span'); cnt.className = 'count';
for (const t of tabs) {
  const b = document.createElement('button'); b.textContent = D.labels[t]; b.dataset.t = t;
  b.onclick = () => { cur = t; sortCol = null; q.value = ''; fillFilter(); draw(); };
  nav.appendChild(b);
}
nav.appendChild(q); nav.appendChild(sel); nav.appendChild(cnt);
q.oninput = draw; sel.onchange = draw;
function fillFilter() {
  const f = D.filters[cur]; sel.innerHTML = '';
  sel.style.display = f ? '' : 'none';
  if (!f) return;
  const vals = [...new Set(D.tables[cur].map(r => r[f]).filter(v => v !== '' && v != null))].sort();
  sel.appendChild(new Option('All: ' + f, ''));
  for (const v of vals) sel.appendChild(new Option(v, v));
}
const isNum = v => typeof v === 'number';
function cell(t, c, v, r) {
  const td = document.createElement('td');
  if (c === 'thumb') { if (v) { const i = document.createElement('img'); i.className = 'thumb'; i.loading = 'lazy'; i.src = v; i.alt = 'map ' + r.map; td.appendChild(i); } return td; }
  if (c === 'description') { td.className = 'desc'; td.textContent = v || ''; return td; }
  if (c === 'command') { const k = document.createElement('code'); k.textContent = v; td.appendChild(k); return td; }
  if (isNum(v)) td.className = 'num';
  td.textContent = v == null ? '' : v; return td;
}
function draw() {
  for (const b of nav.querySelectorAll('button')) b.classList.toggle('on', b.dataset.t === cur);
  const cols = D.columns[cur], rows = D.tables[cur];
  const words = q.value.toLowerCase().split(/\\s+/).filter(Boolean);
  const f = D.filters[cur], fv = sel.value;
  let shown = rows.filter(r => (!fv || String(r[f]) === fv) &&
    words.every(w => cols.some(c => c !== 'thumb' && String(r[c] ?? '').toLowerCase().includes(w))));
  if (sortCol) shown = shown.slice().sort((a, b) => {
    const x = a[sortCol], y = b[sortCol];
    return (isNum(x) && isNum(y) ? x - y : String(x).localeCompare(String(y))) * sortDir; });
  const thead = document.querySelector('#grid thead'), tbody = document.querySelector('#grid tbody');
  thead.innerHTML = ''; tbody.innerHTML = '';
  const tr = document.createElement('tr');
  for (const c of cols) { const th = document.createElement('th'); th.textContent = c + (sortCol === c ? (sortDir > 0 ? ' \\u25b2' : ' \\u25bc') : '');
    th.onclick = () => { sortDir = sortCol === c ? -sortDir : 1; sortCol = c; draw(); }; tr.appendChild(th); }
  thead.appendChild(tr);
  const frag = document.createDocumentFragment();
  for (const r of shown.slice(0, 3000)) { const row = document.createElement('tr');
    for (const c of cols) row.appendChild(cell(cur, c, r[c], r)); frag.appendChild(row); }
  tbody.appendChild(frag);
  cnt.textContent = shown.length + ' of ' + rows.length + (shown.length > 3000 ? ' (first 3000 shown)' : '');
}
fillFilter(); draw();
</script></body></html>
"""


def build_local(disc_path, out_dir, game=None, thumbs=None, jobs=4, log=print):
    """Writes out_dir/index.html: every table, the item descriptions from the disc, and with
    'game' (the built game) a thumbnail of each map. Local only: not for redistribution."""
    t = sn.tables()
    ref = collect(disc_path, t, log=log)
    disc = sd.Disc(disc_path)
    desc = sd.item_descriptions(disc)
    tables = {k: [dict(r) for r in v] for k, v in ref["tables"].items()}
    columns = {k: list(v) for k, v in COLUMNS.items()}
    for r in tables["items"]:
        r["description"] = desc.get(r["id"], "")
    columns["items"].insert(3, "description")
    thumb_note = ""
    if game:
        maps = [r["map"] for r in tables["maps"]]
        if thumbs:
            wanted = set()
            for part in str(thumbs).split(","):
                lo, _, hi = part.strip().partition("-")
                if lo:
                    wanted.update(range(int(lo, 0), int(hi or lo, 0) + 1))
            maps = [m for m in maps if m in wanted]
        tdir = os.path.join(out_dir, "thumbs")
        shoot_thumbnails(game, disc_path, maps, tdir, jobs=jobs, log=log)
        n = 0
        for r in tables["maps"]:
            p = os.path.join(tdir, f"{r['map']}.png")
            r["thumb"] = thumbnail_uri(p) if os.path.exists(p) else ""
            n += bool(r["thumb"])
        columns["maps"].insert(0, "thumb")
        thumb_note = " and pictures of its maps"
        log(f"thumbnails: {n} maps")
    data = {"tables": tables, "columns": columns,
            "labels": {"items": "Items", "maps": "Maps", "doors": "Doors", "chests": "Chests",
                       "shops": "Shops", "enemies": "Enemies"},
            "filters": {"items": "category", "maps": "area", "doors": "map_name", "chests": "map_name",
                        "shops": "shop", "enemies": "boss"}}
    blob = json.dumps(data, ensure_ascii=False, separators=(",", ":")).replace("</", "<\\/")
    page = PAGE.replace("{thumb_note}", html.escape(thumb_note)).replace("{data}", blob)
    os.makedirs(out_dir, exist_ok=True)
    path = os.path.join(out_dir, "index.html")
    with open(path, "w", encoding="utf-8") as f:
        f.write(page)
    log(f"wrote {path} ({len(page) // 1024} KB): local only, made from your disc; don't share it")
    return path


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("disc", help="disc 1 image (.cue / .bin / .iso)")
    ap.add_argument("--out", default=OUT, help="where the tables go (default docs/modding/reference)")
    ap.add_argument("--check", action="store_true", help="only compare with the known tables")
    a = ap.parse_args()
    t = sn.tables()
    ref = collect(a.disc, t)
    results = check(ref, t)
    for ok, text in results:
        print(("ok    " if ok else "DIFF  ") + text)
    if not a.check:
        write_all(ref, a.out)
    return 0 if all(ok for ok, _ in results) else 1


if __name__ == "__main__":
    sys.exit(main())
