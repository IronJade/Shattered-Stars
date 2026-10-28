#!/usr/bin/env python3
"""Renders the modder's reference tables (docs/modding/reference/*.csv, made by
tools/mod_reference.py) as wiki pages.

  mod_reference_wiki.py [--csv docs/modding/reference] [--wiki wiki]

Writes Modding-Reference.md (the hub) and the Reference-* pages: Items (one page per kind),
Maps (per disc), Doors (in parts by map number), Chests, Shops and Enemies. Facts only, as
the tables are. The pages are written the same way each time; a page whose text is
unchanged is not rewritten, and Reference-* pages no longer made are deleted.
"""

import argparse
import collections
import csv
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CSV_DIR = os.path.join(ROOT, "docs", "modding", "reference")
WIKI = os.path.join(ROOT, "wiki")
REPO = "https://github.com/IronJade/Shattered-Stars"
CSV_URL = f"{REPO}/blob/main/docs/modding/reference"
DOOR_PAGE_ROWS = 450

FOOTER = ("\n---\n\nMade by `tools/mod_reference_wiki.py` from the tables in "
          f"[`docs/modding/reference`]({CSV_URL}) (`tools/mod_reference.py`). IDs, names and figures "
          "only; the game's own text and pictures are in the [local reference](Modding-Reference#the-local-reference) "
          "you build from your disc.\n")


def read(name, csv_dir):
    with open(os.path.join(csv_dir, f"{name}.csv"), newline="", encoding="utf-8") as f:
        return list(csv.DictReader(f))


def cell(v):
    v = str(v if v is not None else "")
    return v.replace("|", "\\|").replace("<", "&lt;").replace(">", "&gt;").replace("\n", " ")


def table(head, rows):
    out = ["| " + " | ".join(head) + " |", "|" + "|".join("---" for _ in head) + "|"]
    out += ["| " + " | ".join(cell(x) for x in r) + " |" for r in rows]
    return "\n".join(out) + "\n"


def anchor(text):
    """GitHub's heading anchor."""
    out = []
    for ch in text.lower():
        if ch.isalnum() or ch in "-_":
            out.append(ch)
        elif ch == " ":
            out.append("-")
    return "".join(out)


def num(v):
    try:
        return int(v, 0)
    except (TypeError, ValueError):
        return 0


# ---- Pages -----------------------------------------------------------------------------------

ITEM_PAGES = [("Weapons", "Reference-Items-Weapons", {"weapon"}),
              ("Armor, shields, headgear and footwear", "Reference-Items-Armor",
               {"armor", "shield", "headgear", "footwear"}),
              ("Accessories", "Reference-Items-Accessories", {"accessory"}),
              ("Usable items", "Reference-Items-Usable", {"usable item"}),
              ("Other items (ingredients, materials, valuables, unused)", "Reference-Items-Other", {"item"})]


def item_pages(items):
    pages = {}
    index = ["# Reference: Items\n",
             "Every item's number, name and figures, read from the item records (archive entry 3002). "
             "The number (decimal `id` or `hex`) is what `so2_mod.py item`, `chest --item`, `shop` and the "
             "enemy fields `drop1` / `drop2` take, and what a patch line `item ID FIELD EXPECTED VALUE` "
             "holds; `so2_mod.py` takes the name too. Two items that share a name need their number.\n",
             "`price` is what a shop asks; a shop pays `sell %` of it. `effect` is what a usable item "
             "does (the record's effect kind in brackets: the value of the `effect` field), `amount` "
             "its amount (a percentage for the restoring kinds). `type` is the value of the `type` field.\n",
             f"All {len(items)} items in one file: [`items.csv`]({CSV_URL}/items.csv).\n",
             table(["Page", "Items"], [[f"[{t}]({p})", sum(1 for r in items if r["category"] in cats)]
                                       for t, p, cats in ITEM_PAGES])]
    pages["Reference-Items"] = "\n".join(index)
    for title, page, cats in ITEM_PAGES:
        rows = [r for r in items if r["category"] in cats]
        lines = [f"# Reference: {title}\n", f"[All items](Reference-Items) · {len(rows)} items · "
                 f"[`items.csv`]({CSV_URL}/items.csv)\n"]
        if cats & {"weapon", "armor", "shield", "headgear", "footwear", "accessory"}:
            head = ["id", "hex", "Name", "Kind", "type", "Price", "Atk", "Def", "Sell %", "Equip"]
            body = [[r["id"], r["hex"], r["name"], r["category"], r["type"], r["price"], r["atk"], r["def"],
                     r["sell_pct"], r["equip"]] for r in rows]
        elif "usable item" in cats:
            head = ["id", "hex", "Name", "Used in", "Effect", "Amount", "Price", "Sell %"]
            body = [[r["id"], r["hex"], r["name"], r["use"],
                     f"{r['effect']} ({r['effect_kind']})" if r["effect_kind"] != "0" else "", r["amount"],
                     r["price"], r["sell_pct"]] for r in rows]
        else:
            head = ["id", "hex", "Name", "Price", "Sell %", "Effect"]
            body = [[r["id"], r["hex"], r["name"], r["price"], r["sell_pct"],
                     f"{r['effect']} ({r['effect_kind']})" if r["effect_kind"] != "0" else ""] for r in rows]
        lines.append(table(head, body))
        pages[page] = "\n".join(lines)
    return pages


DISC_PAGES = [("Disc 1 (Expel)", "Reference-Maps-Disc-1", lambda d: d.startswith("1")),
              ("Disc 2 (Energy Nede)", "Reference-Maps-Disc-2", lambda d: d == "2"),
              ("World script and unplaced maps", "Reference-Maps-Other", lambda d: d == "")]


def areas_of(rows):
    """[(area, [rows])] grouped by area, in order of each area's lowest map."""
    groups = collections.OrderedDict()
    for r in sorted(rows, key=lambda r: num(r["map"])):
        groups.setdefault(r["area"] or r["name"], []).append(r)
    return list(groups.items())


def map_pages(maps):
    pages = {}
    named = sum(1 for r in maps if r["name_from"] == "map_names.csv")
    index = ["# Reference: Maps\n",
             "Every field map's number and our name for it. The number is what `so2_mod.py list`, "
             "`chest`, `door` and a door's `--to` take, and what a patch line `script MAP WORD EXPECTED "
             "VALUE` holds; `so2_mod.py` takes the name too (\"Cross: Skill Guild\"). A name is \"area: "
             "place\"; where a map has no description of its own yet, it is the banner the game shows "
             "on entering, or its area.\n",
             "Doors: what `so2_mod.py door` can change ([doors](Reference-Doors)); chests: "
             "[chests](Reference-Chests); shops: the shop numbers the map opens ([shops](Reference-Shops)); "
             "save: the map's own script places a save point. Maps sharing one script (the world-map "
             "chunks) take the same patches, one map at a time.\n",
             f"{len(maps)} maps ({named} with a descriptive name so far) in one file: "
             f"[`maps.csv`]({CSV_URL}/maps.csv).\n",
             table(["Page", "Maps", "Areas"], [[f"[{t}]({p})", sum(1 for r in maps if f(r["discs"])),
                                                len(areas_of([r for r in maps if f(r["discs"])]))]
                                               for t, p, f in DISC_PAGES])]
    pages["Reference-Maps"] = "\n".join(index)
    for title, page, f in DISC_PAGES:
        rows = [r for r in maps if f(r["discs"])]
        groups = areas_of(rows)
        lines = [f"# Reference: Maps, {title}\n", f"[All maps](Reference-Maps) · {len(rows)} maps · "
                 f"[`maps.csv`]({CSV_URL}/maps.csv)\n", "Areas: " + " · ".join(
                     f"[{a}](#{anchor(a)})" for a, _ in groups) + "\n"]
        for area, rs in groups:
            lines.append(f"## {area}\n")
            lines.append(table(["Map", "Name", "Kind", "Doors", "Chests", "Shops", "Save"],
                               [[r["map"], r["name"], r["kind"], r["doors"], r["chests"], r["shops"],
                                 "yes" if r["save_point"] else ""] for r in rs]))
        pages[page] = "\n".join(lines)
    return pages


def door_pages(doors, maps):
    names = {r["map"]: r["name"] for r in maps}
    by_map = collections.OrderedDict()
    for r in sorted(doors, key=lambda r: (num(r["map"]), num(r["index"]))):
        by_map.setdefault(r["map"], []).append(r)
    parts, cur, count = [], [], 0
    for m, rs in by_map.items():
        if cur and count + len(rs) > DOOR_PAGE_ROWS:
            parts.append(cur)
            cur, count = [], 0
        cur.append((m, rs))
        count += len(rs)
    if cur:
        parts.append(cur)
    pages = {}
    index = ["# Reference: Doors\n",
             "Every door `so2_mod.py door` can send somewhere else: a map's map transfers written as one "
             "inline instruction (map_transfer_c), numbered from 0 as `so2_mod.py list` numbers them. "
             "To change one:\n",
             "```\npython3 tools/so2_mod.py door DISC.cue MAP INDEX --to NEW_MAP [--x X --y Y --z Z --dir D]\n```\n",
             "`MAP` and `NEW_MAP` take a number or a name: `door DISC.cue 18 1 --to \"Cross: Skill Guild\"`. "
             "Without `--x --y --z --dir` the party arrives where the door's old target put it, which on "
             "another map may be inside a wall: copy the arrival point of a door that already leads to "
             "the new map (its row below). `mode` is the transition.\n",
             f"{len(doors)} doors on {len(by_map)} maps in one file: [`doors.csv`]({CSV_URL}/doors.csv).\n"]
    rows = []
    for part in parts:
        lo, hi = part[0][0], part[-1][0]
        page = f"Reference-Doors-{lo}-{hi}"
        areas = []
        for m, _ in part:
            a = next((r["area"] or r["name"] for r in maps if r["map"] == m), "")
            if a and a not in areas:
                areas.append(a)
        rows.append([f"[Maps {lo}-{hi}]({page})", sum(len(rs) for _, rs in part),
                     ", ".join(areas[:6]) + (f" and {len(areas) - 6} more" if len(areas) > 6 else "")])
        lines = [f"# Reference: Doors, maps {lo}-{hi}\n", f"[All doors](Reference-Doors) · "
                 f"[`doors.csv`]({CSV_URL}/doors.csv)\n",
                 "`python3 tools/so2_mod.py door DISC.cue MAP INDEX --to NEW_MAP`\n"]
        for m, rs in part:
            lines.append(f"### {m} {names.get(m, '')}\n")
            note = rs[0]["note"]
            if note:
                lines.append(f"*{note}" + (f" ({rs[0]['same_script']})" if rs[0]["same_script"] else "") + ".*\n")
            lines.append(table(["Index", "Leads to", "Name", "Mode", "x", "y", "z", "dir"],
                               [[r["index"], r["target"], r["target_name"], r["mode"], r["x"], r["y"], r["z"],
                                 r["dir"]] for r in rs]))
        pages[page] = "\n".join(lines)
    index.append(table(["Page", "Doors", "Areas"], rows))
    pages["Reference-Doors"] = "\n".join(index)
    return pages


def chest_page(chests):
    lines = ["# Reference: Chests\n",
             "Every chest in the map scripts. A chest's ID is its opened flag (`flag`, also in decimal): "
             "the game sets it when the chest is opened. To change what one holds:\n",
             "```\npython3 tools/so2_mod.py chest DISC.cue MAP FLAG --item ITEM\n"
             "python3 tools/so2_mod.py chest DISC.cue MAP FLAG --fol N\n```\n",
             "`MAP` and `ITEM` take a number or a name. A chest holding an item can only be changed to "
             "another item; one holding Fol takes either (`Can hold`). The same chest can be in several "
             "versions of a map (`Also on`): change each, or it holds the old contents there.\n",
             f"{len(chests)} chest sites, {len({r['flag'] for r in chests})} chests: "
             f"[`chests.csv`]({CSV_URL}/chests.csv).\n"]
    lines.append(table(["Map", "Map name", "Flag", "Flag (dec)", "Contents", "Item", "Can hold", "Also on"],
                       [[r["map"], r["map_name"], r["flag"], r["flag_dec"], r["contents"], r["item"],
                         r["can_hold"], r["also_on"]] for r in chests]))
    return {"Reference-Chests": "\n".join(lines)}


def shop_page(shops):
    by = collections.OrderedDict()
    for r in sorted(shops, key=lambda r: (num(r["shop"]), num(r["index"]))):
        by.setdefault(r["shop"], []).append(r)
    lines = ["# Reference: Shops\n",
             "Every shop's stock, entry by entry. A shop's number is what `so2_mod.py shop` takes (a name "
             "of a map that opens it works too), with the entry's index:\n",
             "```\npython3 tools/so2_mod.py shop DISC.cue SHOP INDEX ITEM   # replace entry INDEX\n"
             "python3 tools/so2_mod.py shop DISC.cue SHOP add ITEM     # add an entry at the end\n"
             "python3 tools/so2_mod.py shop DISC.cue SHOP INDEX 0      # remove entry INDEX\n```\n",
             f"{len(by)} shops, {len(shops)} entries: [`shops.csv`]({CSV_URL}/shops.csv). Shops: " +
             " · ".join(f"[{s}](#shop-{s})" for s in by) + "\n"]
    for s, rs in by.items():
        where = rs[0]["where"] or "no map found that opens it"
        lines.append(f"### Shop {s}\n")
        lines.append(f"{where}\n")
        lines.append(table(["Index", "Item", "Name"], [[r["index"], r["item"], r["item_name"]] for r in rs]))
    return {"Reference-Shops": "\n".join(lines)}


def enemy_page(enemies):
    lines = ["# Reference: Enemies\n",
             "Every enemy type in the encounter packs, with the stats every battle gives it. The type "
             "number is what `so2_mod.py enemy` takes (the name works too) with a field name: the "
             "column names below (`hp`, `mp`, `exp`, `fol`, `level`, `atk`, `def`, `drop1`, `drop2`, "
             "`drop1_odds`, `drop2_odds`, ...):\n",
             "```\npython3 tools/so2_mod.py enemy DISC.cue TYPE FIELD VALUE\n"
             "python3 tools/so2_mod.py enemy DISC.cue Armedknight drop1 \"Blackberry\"\n```\n",
             "A drop is an item and its odds. *Where* lists the areas of the encounters it is in "
             "(random encounters, scripted battles, the world maps); the encounter numbers and every "
             f"map are in [`enemies.csv`]({CSV_URL}/enemies.csv). Two types can share a name: use "
             f"the number. {len(enemies)} types.\n"]

    def drop(r, k):
        return f"{r[k]} {r[k + '_name']} ({r[k + '_odds']})" if r[k] else ""
    lines.append(table(["Type", "Name", "Level", "HP", "MP", "Atk", "Def", "Exp", "Fol", "Drop 1", "Drop 2",
                        "Boss", "Where"],
                       [[r["type"], r["name"], r["level"], r["hp"], r["mp"], r["atk"], r["def"], r["exp"],
                         r["fol"], drop(r, "drop1"), drop(r, "drop2"), r["boss"], r["areas"]] for r in enemies]))
    return {"Reference-Enemies": "\n".join(lines)}


def hub(counts, pages):
    c = counts
    return "\n".join([
        "# Modding Reference\n",
        "What the numbers in a mod stand for: every item, map, door, chest, shop and enemy type with "
        "its ID, its name and its figures. The tables are read from the disc by "
        "`tools/mod_reference.py` and hold IDs, names, types, prices and stats only, never the game's "
        "text or pictures; for those, build the [local reference](#the-local-reference) from your "
        "own disc.\n",
        "## The tables\n",
        table(["Table", "Rows", "Wiki", "File"], [
            ["Items", c["items"], "[Reference: Items](Reference-Items)", f"[`items.csv`]({CSV_URL}/items.csv)"],
            ["Maps", c["maps"], "[Reference: Maps](Reference-Maps)", f"[`maps.csv`]({CSV_URL}/maps.csv)"],
            ["Doors", c["doors"], "[Reference: Doors](Reference-Doors)", f"[`doors.csv`]({CSV_URL}/doors.csv)"],
            ["Chests", c["chests"], "[Reference: Chests](Reference-Chests)", f"[`chests.csv`]({CSV_URL}/chests.csv)"],
            ["Shop entries", c["shops"], "[Reference: Shops](Reference-Shops)", f"[`shops.csv`]({CSV_URL}/shops.csv)"],
            ["Enemy types", c["enemies"], "[Reference: Enemies](Reference-Enemies)",
             f"[`enemies.csv`]({CSV_URL}/enemies.csv)"]]),
        f"The CSV files open in any spreadsheet; their columns are explained in "
        f"[`docs/modding/reference/README.md`]({CSV_URL}/README.md).\n",
        "## Reading the IDs\n",
        "- **Numbers** are written in decimal or in hex with `0x` (`677` and `0x2a5` are the same item, "
        "Leather Armor). `so2_mod.py` and patch files take either.\n"
        "- **Items** are numbered 1-1023. Two items can share a name (the game has two Fanzines); "
        "then use the number.\n"
        "- **Maps** are field map numbers. Our names read \"area: place\", for example `18 Arlia "
        "Village: north: fountain square`; a map without a description of its own yet shows the banner "
        "the game puts up on entering, or its area. The world maps' chunks (909-980 for Expel, "
        "1048-1110 for Energy Nede) share one script: a patch names one map, so repeat it for each.\n"
        "- **Doors** are numbered per map from 0, in the order `so2_mod.py list` prints them.\n"
        "- **Chests** are known by their opened flag, a number the game sets once the chest is "
        "opened (`0x962` on map 18 holds 200 Fol). One chest can sit in several versions of a map.\n"
        "- **Shop entries** are a shop number and an index from 0.\n"
        "- **Enemy types** are the numbers in the encounter packs; the stats are the same in every "
        "battle the type is in.\n",
        "## Names instead of numbers\n",
        "Every `so2_mod.py` argument that takes an item, a map, a shop or an enemy type also takes "
        "its name, without regard to case, punctuation or plurals:\n",
        "```\n"
        "python3 tools/so2_mod.py door DISC.cue 18 1 --to \"Cross: Skill Guild\"\n"
        "python3 tools/so2_mod.py chest DISC.cue 18 0x962 --item \"Blueberries\"\n"
        "python3 tools/so2_mod.py shop DISC.cue \"Hearn's\" add \"Resurrection Bottle\"\n"
        "python3 tools/so2_mod.py enemy DISC.cue Armedknight hp 400\n"
        "```\n",
        "A name that fits more than one entry lists them and stops (pick one, by its number); a "
        "misspelt one gets the closest names. To search every name:\n",
        "```\npython3 tools/so2_mod.py find DISC.cue skill guild\n```\n",
        "The listings print names next to the numbers, too: `so2_mod.py list DISC.cue 18` shows "
        "`flag 0x962 ... 200 Fol` and `-> map 26 (Arlia Village: ...)`.\n",
        "## The local reference\n",
        "`so2_mod.py reference` builds one web page on your computer with every table above, "
        "searchable and sortable, plus each item's description read from your disc. With `--game` "
        "pointing at the built game it also renders a small picture of every map (the game runs "
        "without a window and without sound, a few at a time):\n",
        "```\n"
        "python3 tools/so2_mod.py reference DISC.cue --out my-reference\n"
        "python3 tools/so2_mod.py reference DISC.cue --out my-reference --game engine/build/shattered-stars\n"
        "python3 tools/so2_mod.py reference DISC.cue --out my-reference --game engine/build/shattered-stars --thumbs 17-40\n"
        "```\n",
        "Open `my-reference/index.html` in a browser. The page is made from your own disc and holds the "
        "game's text and pictures: keep it to yourself; don't upload or share it. Rendering every map "
        "takes a while; `--thumbs` picks maps and `--jobs` (1-6) how many run at once; pictures "
        "already made are kept for the next run.\n",
        "## Keeping the tables current\n",
        "`python3 tools/mod_reference.py DISC1.cue` rewrites the CSV files (only those that changed); "
        "`--check` compares them with the Archipelago tables (313 chests, 1023 items, 64 shops, 187 "
        "enemy types, 2441 doors). `python3 tools/mod_reference_wiki.py` turns them into these pages.\n",
        "See also: [Making a Mod](Making-a-Mod) · [Modding](Modding) · "
        f"[the patch grammar]({REPO}/blob/main/engine/src/game/field/mods.h)\n",
    ])


def build(csv_dir):
    t = {n: read(n, csv_dir) for n in ("items", "maps", "doors", "chests", "shops", "enemies")}
    pages = {}
    pages.update(item_pages(t["items"]))
    pages.update(map_pages(t["maps"]))
    pages.update(door_pages(t["doors"], t["maps"]))
    pages.update(chest_page(t["chests"]))
    pages.update(shop_page(t["shops"]))
    pages.update(enemy_page(t["enemies"]))
    pages = {k: v + FOOTER for k, v in pages.items()}
    pages["Modding-Reference"] = hub({k: len(v) for k, v in t.items()}, pages) + FOOTER
    return pages


def write(pages, wiki):
    changed = []
    for name, text in sorted(pages.items()):
        path = os.path.join(wiki, f"{name}.md")
        old = None
        if os.path.exists(path):
            with open(path, encoding="utf-8") as f:
                old = f.read()
        if old != text:
            with open(path, "w", encoding="utf-8") as f:
                f.write(text)
            changed.append(name)
    gone = [f[:-3] for f in os.listdir(wiki) if f.startswith("Reference-") and f.endswith(".md")
            and f[:-3] not in pages]
    for name in gone:
        os.remove(os.path.join(wiki, f"{name}.md"))
    return changed, gone


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--csv", default=CSV_DIR)
    ap.add_argument("--wiki", default=WIKI)
    a = ap.parse_args()
    pages = build(a.csv)
    changed, gone = write(pages, a.wiki)
    sizes = sorted(((len(v), k) for k, v in pages.items()), reverse=True)
    print(f"{len(pages)} pages; changed {len(changed)}, removed {len(gone)}; largest "
          + ", ".join(f"{k} {n // 1024} KB" for n, k in sizes[:3]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
