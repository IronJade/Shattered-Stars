#!/usr/bin/env python3
"""World edits for Shattered Stars: write patches for chests, doors, items, shops and enemies.

Reads the map scripts straight from your disc image (.cue or .bin) and prints
`script MAP WORD EXPECTED VALUE` lines for the engine's mods directory (mods/ next to
config.ini, or --mods DIR); see engine/src/game/field/mods.h. Nothing on the disc is
changed: the engine patches each script in memory as the map loads, and only while
the original word is still there.

Wherever a command takes a map, an item, a shop or an enemy type, it takes its number
(decimal or 0x hex) or its name: "Cross: Skill Guild", "Blueberry", "Hearn's", "Armedknight".
Names are matched without regard to case, punctuation or plurals; a name that fits several
entries lists them (use one of their numbers), and a misspelt one gets suggestions.

  so2_mod.py list  DISC MAP
      chests (opened flag, contents), map transfers (index, target) and save point of MAP
  so2_mod.py chest DISC MAP FLAG (--item ITEM | --fol N) [-o FILE]
      make the chest with opened-flag FLAG give ITEM or N Fol
  so2_mod.py door  DISC MAP INDEX --to MAP2 [--x X] [--y Y] [--z Z] [--dir D] [-o FILE]
      send map transfer INDEX (from `list`) to MAP2 at (X, Y, Z) facing D

  so2_mod.py item  DISC ITEM FIELD VALUE [-o FILE]
      set a field of item ITEM's record (FIELD: a name below, or OFFSET[:SIZE])
  so2_mod.py items DISC ITEM
      show item ITEM's record fields
  so2_mod.py shop  DISC SHOP INDEX ITEM [-o FILE]
      put ITEM at stock entry INDEX of shop SHOP (INDEX `add` appends, ITEM 0 removes)
  so2_mod.py shops DISC
      list every shop's stock
  so2_mod.py enemy DISC TYPE FIELD VALUE [-o FILE]
      set a stats field of enemy type TYPE (FIELD: a name below, or OFFSET[:SIZE]; the value
      of drop1 / drop2 is an item)
  so2_mod.py enemies DISC [TYPE]
      list the enemy types found in the encounter packs (or one type's stats)

  so2_mod.py find DISC TEXT
      search the names of maps, items, shops and enemy types
  so2_mod.py reference DISC --out DIR [--game PATH] [--thumbs MAPS] [--jobs N]
      build a local reference page (DIR/index.html): every table, searchable, with the item
      descriptions from your disc and, with --game (the built game), a picture of each map.
      Made from your disc for you only: don't share it. The facts-only tables are in
      docs/modding/reference/ (tools/mod_reference.py).

Item fields: price, atk, def, amount (effect amount), effect (kind), type, sell (sell %).
Enemy fields: hp, mp, exp, fol, speed, drop1, drop2, atk, def, int, level, eva, acc,
guts, drop1_odds, drop2_odds.

Discs: the North American, European, French or German disc 1 (or 2). Patch lines always use
NA's script word numbers (the engine translates them for the disc being played), so a mod
made with any of them applies to every release whose data matches EXPECTED.

Limits: a chest holding an item can only be changed to another item (its value is a
24-bit push); a chest holding Fol (a full-word push) can take either. Only the
inline map-transfer form (map_transfer_c with constant arguments) can be retargeted.
Every patch carries the value the disc holds; the engine skips (and logs) a patch whose
data no longer matches.
"""

import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
# The disc readers live in so2_data (shared with mod_reference.py); they stay importable from
# here for the tools that use so2_mod.X (ap_extract.py, ap_battles.py, ...).
from so2_archive import ARCHIVE_LBA, TOC_BYTES, SECTOR, SLZ_MAGIC, decrypt_toc, unpack_block  # noqa: E402,F401
from so2_script import CODE_START, MAP_ENTRY_BASE, disassemble, parse_script  # noqa: E402,F401
from so2_data import (SYNC, Disc, na_script_offset, instructions, push_value, chests, transfers, save_points,  # noqa: E402,F401
                      ITEM_ENTRY, SHOP_ENTRY, SHOP_TABLE, PAGE_BASE, FORMATION_BASE, ITEM_FIELDS,
                      ENEMY_FIELDS, field_spec, item_size, enemy_size, le, item_records, shop_lists,
                      formation_records, enemy_stats, enemy_name, item_type_name, item_effect_name,
                      item_use_name, equip_names)
import so2_names  # noqa: E402

MAX_CANDIDATES = 25


def names():
    return so2_names.tables()


def resolve(kind, text):
    """The number 'text' stands for (a number, or a name of a map / item / shop / enemy type);
    exits with the candidates or suggestions when it isn't one entry's name."""
    try:
        return names().resolve(kind, text)
    except so2_names.LookupFailed as e:
        lines = [str(e)] + [f"  {c}" for c in e.candidates[:MAX_CANDIDATES]]
        if len(e.candidates) > MAX_CANDIDATES:
            lines.append(f"  ... and {len(e.candidates) - MAX_CANDIDATES} more (so2_mod.py find DISC TEXT lists all)")
        sys.stderr.write("\n".join(lines) + "\n")
        raise SystemExit(2)


def map_label(m):
    return f"map {m} ({names().map_name(m)})"


def item_label(i):
    return f"item {i:#x} {names().item_name(i)}" if i else "nothing (0)"


def describe_value(v):
    return f"{-v} Fol" if v < 0 else item_label(v)


def emit(lines, path):
    text = "".join(line + "\n" for line in lines)
    if path:
        with open(path, "a") as f:
            f.write(text)
        print(f"appended {len(lines)} patch line(s) to {path}")
    else:
        sys.stdout.write(text)


def cmd_list(a):
    m = resolve("map", a.map)
    blob = Disc(a.disc).map_script(m)
    ins = instructions(blob)
    print(f"{map_label(m)}: chests")
    for flag, handler, v in chests(blob, ins):
        print(f"  flag {flag:#x}  handler word {handler:#x}  {describe_value(push_value(v))}")
    print(f"{map_label(m)}: map transfers (map_transfer_c)")
    for n, i, args in transfers(blob, ins):
        print(f"  {n}: offset {i[0]:#x} -> {map_label(args[0])} mode {args[1]} at ({args[2]}, {args[3]}, {args[4]}) dir {args[5]}")
    for x, y, z, _ in save_points(blob, ins):
        print(f"{map_label(m)}: save point at ({x}, {y}, {z})")


def na_word(disc, m, offset):
    """NA's word number of byte 'offset' of this disc's script code of map m."""
    at = na_script_offset(disc, m, offset)
    if at is None:
        raise SystemExit(f"{map_label(m)}: this place of the {disc.serial} script has no counterpart on the "
                         "North American disc, so no patch line can name it")
    return at // 4


def cmd_chest(a):
    m = resolve("map", a.map)
    disc = Disc(a.disc)
    blob = disc.map_script(m)
    item = resolve("item", a.item) if a.item is not None else None
    for flag, _, ins in chests(blob):
        if flag != a.flag:
            continue
        words = ins[1]
        word_index = na_word(disc, m, ins[0])
        new = item if item is not None else -a.fol
        if words[0] >> 24 == 0x12:
            if new < 0 or new > 0xFFFFFF:
                raise SystemExit("this chest holds an item (24-bit push): it can only be changed to another item")
            line = f"script {m} {word_index:#x} {words[0]:#010x} {(words[0] & 0xFF000000) | new:#010x}"
        else:
            line = f"script {m} {word_index + 1:#x} {words[1]:#010x} {new & 0xFFFFFFFF:#010x}"
        emit([f"# {map_label(m)} chest {a.flag:#x}: {describe_value(push_value(ins))} -> {describe_value(new)}", line],
             a.output)
        return
    raise SystemExit(f"{map_label(m)}: no chest with flag {a.flag:#x} (see `list`)")


def cmd_door(a):
    m = resolve("map", a.map)
    to = resolve("map", a.to)
    disc = Disc(a.disc)
    blob = disc.map_script(m)
    for n, ins, args in transfers(blob):
        if n != a.index:
            continue
        new = list(args)
        new[0] = to
        for k, v in ((2, a.x), (3, a.y), (4, a.z), (5, a.dir)):
            if v is not None:
                new[k] = v
        h = [v & 0xFFFF for v in new]
        words = [(ins[1][0] & 0xFFFF0000) | h[0], h[1] | h[2] << 16, h[3] | h[4] << 16, h[5] | h[6] << 16]
        base = na_word(disc, m, ins[0])
        lines = [f"# {map_label(m)} transfer {n}: {map_label(args[0])} ({args[2]}, {args[3]}, {args[4]}) -> "
                 f"{map_label(new[0])} ({new[2]}, {new[3]}, {new[4]})"]
        for k in range(4):
            if words[k] != ins[1][k]:
                lines.append(f"script {m} {base + k:#x} {ins[1][k]:#010x} {words[k]:#010x}")
        emit(lines, a.output)
        return
    raise SystemExit(f"{map_label(m)}: no map transfer {a.index} (see `list`)")


def cmd_item(a):
    item = resolve("item", a.item)
    off, size = field_spec(a.field, ITEM_FIELDS, item_size)
    rec = item_records(Disc(a.disc))
    at = (item - 1) * 0x30 + off
    if not 1 <= item <= len(rec) // 0x30 or off + size > 0x30:
        raise SystemExit(f"no item {item} / field {a.field}")
    old = le(rec, at, size)
    spec = f"{off:#x}" if size == item_size(off) else f"{off:#x}:{size}"
    emit([f"# {item_label(item)} {a.field}: {old} -> {a.value}",
          f"item {item:#x} {spec} {old:#x} {a.value & ((1 << 8 * size) - 1):#x}"], a.output)


def cmd_items(a):
    item = resolve("item", a.item)
    rec = item_records(Disc(a.disc))
    if not 1 <= item <= len(rec) // 0x30:
        raise SystemExit(f"no item {item} (items are 1-{len(rec) // 0x30})")
    r = rec[(item - 1) * 0x30:item * 0x30]
    print(f"{item_label(item)}: " + " ".join(f"{k}={le(r, o, n)}" for k, (o, n) in ITEM_FIELDS.items()))
    t, use = r[0x2A], r[0x29]
    words = ["usable item" if t == 0 and use else item_type_name(t)]
    if use:
        words.append(f"used in: {item_use_name(use)}")
    if r[0x28]:
        words.append(f"effect: {item_effect_name(r[0x28])}")
    if t:
        words.append(f"equip: {equip_names(le(r, 4, 2)) or 'nobody'}")
    print("  " + "; ".join(words))
    print("  raw " + r.hex())


def cmd_shop(a):
    shop = resolve("shop", a.shop)
    item = resolve("item", a.item)
    lists = shop_lists(Disc(a.disc))
    if shop not in lists:
        raise SystemExit(f"no shop {shop} (see `shops`)")
    items = lists[shop]
    if a.index == "add":
        index, old = len(items), 0
    else:
        index = int(a.index, 0)
        if index >= len(items):
            raise SystemExit(f"shop {shop} has {len(items)} entries; use `add` to append")
        old = items[index]
    emit([f"# {names().shop_name(shop)} entry {index}: {item_label(old)} -> {item_label(item)}",
          f"shop {shop} {index} {old:#x} {item:#x}"], a.output)


def cmd_shops(a):
    for shop, items in shop_lists(Disc(a.disc)).items():
        if items:
            print(f"shop {shop} ({shop:#x}): {names().shop_name(shop)}")
            for n, i in enumerate(items):
                print(f"  {n}: {i:#x} {names().item_name(i)}")


def cmd_enemy(a):
    typ = resolve("enemy", a.type)
    off, size = field_spec(a.field, ENEMY_FIELDS, enemy_size)
    value = resolve("item", a.value) if a.field in ("drop1", "drop2") else int(a.value, 0)
    found = enemy_stats(Disc(a.disc), typ)
    if typ not in found:
        raise SystemExit(f"enemy type {typ} not found in the encounter packs")
    n, rec = found[typ]
    old = le(rec, off, size)
    spec = f"{off:#x}" if size == enemy_size(off) else f"{off:#x}:{size}"
    shown = f"{item_label(old)} -> {item_label(value)}" if a.field in ("drop1", "drop2") else f"{old} -> {value}"
    emit([f"# enemy {typ} {enemy_name(rec)} {a.field} (as in encounter {n}): {shown}",
          f"enemy {typ} {spec} {old:#x} {value & ((1 << 8 * size) - 1):#x}"], a.output)


def cmd_enemies(a):
    typ = resolve("enemy", a.type) if a.type is not None else None
    found = enemy_stats(Disc(a.disc), typ)
    for t in sorted(found):
        if typ is not None and t != typ:
            continue
        n, rec = found[t]
        parts = []
        for k, (o, s) in ENEMY_FIELDS.items():
            v = le(rec, o, s)
            parts.append(f"{k}={v:#x} ({names().item_name(v)})" if k in ("drop1", "drop2") and v else f"{k}={v}")
        print(f"type {t} {enemy_name(rec)}: encounter {n} " + " ".join(parts))


def cmd_find(a):
    Disc(a.disc)  # the same arguments as every command; the names come from the repo's tables
    hits = names().find(a.text)
    if not hits:
        raise SystemExit(f"nothing named like {a.text!r}")
    heads = {"map": "maps", "item": "items", "shop": "shops", "enemy": "enemy types"}
    for kind in ("map", "item", "shop", "enemy"):
        rows = [h for h in hits if h[0] == kind]
        if rows:
            print(f"{heads[kind]}:")
            for _, _, label in rows:
                print(f"  {label}")


def cmd_reference(a):
    import mod_reference  # noqa: E402 (only this command needs it)
    if a.game and not os.access(a.game, os.X_OK):
        raise SystemExit(f"{a.game}: not the game's executable")
    mod_reference.build_local(a.disc, a.out, game=a.game, thumbs=a.thumbs, jobs=a.jobs)


def main():
    num = lambda s: int(s, 0)  # noqa: E731
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("list", help="a map's chests, doors and save point")
    p.add_argument("disc")
    p.add_argument("map", help="map number or name")
    p.set_defaults(run=cmd_list)
    p = sub.add_parser("chest", help="change what a chest holds")
    p.add_argument("disc")
    p.add_argument("map", help="map number or name")
    p.add_argument("flag", type=num, help="the chest's opened flag (from `list`)")
    g = p.add_mutually_exclusive_group(required=True)
    g.add_argument("--item", help="item number or name")
    g.add_argument("--fol", type=num)
    p.add_argument("-o", "--output")
    p.set_defaults(run=cmd_chest)
    p = sub.add_parser("door", help="send a door somewhere else")
    p.add_argument("disc")
    p.add_argument("map", help="map number or name")
    p.add_argument("index", type=num, help="the transfer's index (from `list`)")
    p.add_argument("--to", required=True, help="map number or name")
    for k in ("x", "y", "z", "dir"):
        p.add_argument(f"--{k}", type=num)
    p.add_argument("-o", "--output")
    p.set_defaults(run=cmd_door)
    p = sub.add_parser("item", help="change a field of an item's record")
    p.add_argument("disc")
    p.add_argument("item", help="item number or name")
    p.add_argument("field")
    p.add_argument("value", type=num)
    p.add_argument("-o", "--output")
    p.set_defaults(run=cmd_item)
    p = sub.add_parser("items", help="show an item's record")
    p.add_argument("disc")
    p.add_argument("item", help="item number or name")
    p.set_defaults(run=cmd_items)
    p = sub.add_parser("shop", help="change a shop's stock")
    p.add_argument("disc")
    p.add_argument("shop", help="shop number or name (of a map that opens it)")
    p.add_argument("index")
    p.add_argument("item", help="item number or name (0 removes)")
    p.add_argument("-o", "--output")
    p.set_defaults(run=cmd_shop)
    p = sub.add_parser("shops", help="every shop's stock")
    p.add_argument("disc")
    p.set_defaults(run=cmd_shops)
    p = sub.add_parser("enemy", help="change an enemy type's stats")
    p.add_argument("disc")
    p.add_argument("type", help="enemy type number or name")
    p.add_argument("field")
    p.add_argument("value", help="a number (an item number or name for drop1 / drop2)")
    p.add_argument("-o", "--output")
    p.set_defaults(run=cmd_enemy)
    p = sub.add_parser("enemies", help="enemy types and their stats")
    p.add_argument("disc")
    p.add_argument("type", nargs="?", help="enemy type number or name")
    p.set_defaults(run=cmd_enemies)
    p = sub.add_parser("find", help="search maps, items, shops and enemies by name")
    p.add_argument("disc")
    p.add_argument("text", nargs="+")
    p.set_defaults(run=lambda a: cmd_find(argparse.Namespace(disc=a.disc, text=" ".join(a.text))))
    p = sub.add_parser("reference", help="build the local reference page from your disc")
    p.add_argument("disc")
    p.add_argument("--out", required=True, help="folder for index.html (and thumbs/)")
    p.add_argument("--game", help="the built game (shattered-stars) to render map thumbnails with")
    p.add_argument("--thumbs", help="only these maps' thumbnails, e.g. 18,26,80-90 (default all)")
    p.add_argument("--jobs", type=int, default=4, help="game runs at once for thumbnails (1-6, default 4)")
    p.set_defaults(run=cmd_reference)
    a = ap.parse_args()
    return a.run(a)


if __name__ == "__main__":
    sys.exit(main())
