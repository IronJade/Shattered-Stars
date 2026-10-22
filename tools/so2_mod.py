#!/usr/bin/env python3
"""World edits for Shattered Stars: write patches for chests, doors, items, shops and enemies.

Reads the map scripts straight from your disc image (.cue or .bin) and prints
`script MAP WORD EXPECTED VALUE` lines for the engine's mods directory (mods/ next to
config.ini, or --mods DIR); see engine/src/game/field/mods.h. Nothing on the disc is
changed: the engine patches each script in memory as the map loads, and only while
the original word is still there.

  so2_mod.py list  DISC MAP
      chests (opened flag, contents) and map transfers (index, target) of field map MAP
  so2_mod.py chest DISC MAP FLAG (--item ID | --fol N) [-o FILE]
      make the chest with opened-flag FLAG give item ID or N Fol
  so2_mod.py door  DISC MAP INDEX --to MAP2 [--x X] [--y Y] [--z Z] [--dir D] [-o FILE]
      send map transfer INDEX (from `list`) to MAP2 at (X, Y, Z) facing D

  so2_mod.py item  DISC ITEM FIELD VALUE [-o FILE]
      set a field of item ITEM's record (FIELD: a name below, or OFFSET[:SIZE])
  so2_mod.py items DISC ITEM
      show item ITEM's record fields
  so2_mod.py shop  DISC SHOP INDEX ITEM [-o FILE]
      put item ITEM at stock entry INDEX of shop SHOP (INDEX `add` appends, ITEM 0 removes)
  so2_mod.py shops DISC
      list every shop's stock
  so2_mod.py enemy DISC TYPE FIELD VALUE [-o FILE]
      set a stats field of enemy type TYPE (FIELD: a name below, or OFFSET[:SIZE])
  so2_mod.py enemies DISC [TYPE]
      list the enemy types found in the encounter packs (or one type's stats)

Item fields: price, atk, def, amount (effect amount), effect (kind), type, sell (sell %).
Enemy fields: hp, mp, exp, fol, speed, drop1, drop2, atk, def, int, level, eva, acc,
guts, drop1_odds, drop2_odds.

Limits: a chest holding an item can only be changed to another item (its value is a
24-bit push); a chest holding Fol (a full-word push) can take either. Only the
inline map-transfer form (map_transfer_c with constant arguments) can be retargeted.
Every patch carries the value the disc holds; the engine skips (and logs) a patch whose
data no longer matches.
"""

import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from so2_archive import ARCHIVE_LBA, TOC_BYTES, SECTOR, SLZ_MAGIC, decrypt_toc, unpack_block  # noqa: E402
from so2_script import CODE_START, MAP_ENTRY_BASE, disassemble, parse_script  # noqa: E402

RAW = 2352
DATA_AT = 24  # MODE2/2352 form 1: sync 12, header 4, subheader 8


class Disc:
    def __init__(self, path):
        if path.lower().endswith(".cue"):
            base = os.path.dirname(os.path.abspath(path))
            for line in open(path, encoding="latin-1"):
                parts = line.strip().split('"')
                if line.strip().upper().startswith("FILE") and len(parts) >= 2:
                    path = os.path.join(base, parts[1])
                    break
        self.f = open(path, "rb")
        self.toc = {i: (lba, n) for i, lba, n in decrypt_toc(self.read(ARCHIVE_LBA, (TOC_BYTES + SECTOR - 1) // SECTOR))}

    def read(self, lba, count):
        out = bytearray()
        for k in range(count):
            self.f.seek((lba + k) * RAW + DATA_AT)
            out += self.f.read(SECTOR)
        return bytes(out)

    def entry(self, index):
        lba, n = self.toc.get(index, (None, 0))
        if lba is None:
            raise SystemExit(f"archive entry {index} not present")
        return self.read(lba, n)

    def map_script(self, map_id):
        data = self.entry(MAP_ENTRY_BASE + map_id)
        count = struct.unpack_from("<I", data, 0)[0]
        if not 1 <= count <= 16:
            raise SystemExit(f"map {map_id} is not a field map")
        sections = dict(struct.unpack_from("<II", data, 4 + 8 * i) for i in range(count))
        offset = sections.get(1)
        if offset is None or data[offset:offset + 3] != SLZ_MAGIC:
            raise SystemExit(f"map {map_id}: no script section")
        return bytes(unpack_block(data, offset))


def instructions(blob):
    script = parse_script(blob)
    return list(disassemble(script["code"]))  # (offset, words, text, target, problem)


def push_value(ins):
    """The constant an instruction pushes, or None: push imm24 (0x12) / push word (0x13)."""
    words = ins[1]
    op = words[0] >> 24
    if op == 0x12:
        return words[0] & 0xFFFFFF
    if op == 0x13 and len(words) > 1:
        v = words[1]
        return v - (1 << 32) if v & 0x80000000 else v
    return None


def chests(blob):
    """[(flag, handler_word, value_ins)] for every chest (B 0x6c registration + handler)."""
    ins = instructions(blob)
    by_offset = {i[0]: n for n, i in enumerate(ins)}
    found = []
    for n, i in enumerate(ins):
        if not i[2].startswith("B_6c <8 from stack>") or n < 8:
            continue
        args = [push_value(ins[n - 8 + k]) for k in range(8)]
        if None in args:
            continue
        flag, handler = args[6], args[7]
        h = by_offset.get(handler * 4)
        if h is None:
            continue
        # The handler: frame; push actor; push flag; push value; push message; call.
        pushes = []
        for j in range(h + 1, min(h + 12, len(ins))):
            v = push_value(ins[j])
            if v is not None:
                pushes.append(ins[j])
            elif ins[j][2].startswith("call"):
                break
        if len(pushes) >= 3 and push_value(pushes[1]) == flag:
            found.append((flag, handler, pushes[2]))
    return found


def transfers(blob):
    """[(index, ins, args)] for every inline map_transfer_c."""
    out = []
    for i in instructions(blob):
        if i[2].startswith("map_transfer_c(") and len(i[1]) == 4:
            w = i[1]
            args = [w[0] & 0xFFFF] + [half for pair in w[1:] for half in (pair & 0xFFFF, pair >> 16)]
            args = [a - 0x10000 if a & 0x8000 else a for a in args[:7]]
            out.append((len(out), i, args))
    return out


def describe_value(v):
    return f"{-v} Fol" if v < 0 else f"item {v:#x}"


def emit(lines, path):
    text = "".join(line + "\n" for line in lines)
    if path:
        with open(path, "a") as f:
            f.write(text)
        print(f"appended {len(lines)} patch line(s) to {path}")
    else:
        sys.stdout.write(text)


def cmd_list(a):
    blob = Disc(a.disc).map_script(a.map)
    print(f"map {a.map}: chests")
    for flag, handler, ins in chests(blob):
        print(f"  flag {flag:#x}  handler word {handler:#x}  {describe_value(push_value(ins))}")
    print(f"map {a.map}: map transfers (map_transfer_c)")
    for n, ins, args in transfers(blob):
        print(f"  {n}: offset {ins[0]:#x} -> map {args[0]} mode {args[1]} at ({args[2]}, {args[3]}, {args[4]}) dir {args[5]}")


def cmd_chest(a):
    blob = Disc(a.disc).map_script(a.map)
    for flag, _, ins in chests(blob):
        if flag != a.flag:
            continue
        words = ins[1]
        word_index = ins[0] // 4
        new = a.item if a.item is not None else -a.fol
        if words[0] >> 24 == 0x12:
            if new < 0 or new > 0xFFFFFF:
                raise SystemExit("this chest holds an item (24-bit push): it can only be changed to another item")
            line = f"script {a.map} {word_index:#x} {words[0]:#010x} {(words[0] & 0xFF000000) | new:#010x}"
        else:
            line = f"script {a.map} {word_index + 1:#x} {words[1]:#010x} {new & 0xFFFFFFFF:#010x}"
        emit([f"# map {a.map} chest {a.flag:#x}: {describe_value(push_value(ins))} -> {describe_value(new)}", line], a.output)
        return
    raise SystemExit(f"map {a.map}: no chest with flag {a.flag:#x} (see `list`)")


def cmd_door(a):
    blob = Disc(a.disc).map_script(a.map)
    for n, ins, args in transfers(blob):
        if n != a.index:
            continue
        new = list(args)
        new[0] = a.to
        for k, v in ((2, a.x), (3, a.y), (4, a.z), (5, a.dir)):
            if v is not None:
                new[k] = v
        h = [v & 0xFFFF for v in new]
        words = [(ins[1][0] & 0xFFFF0000) | h[0], h[1] | h[2] << 16, h[3] | h[4] << 16, h[5] | h[6] << 16]
        base = ins[0] // 4
        lines = [f"# map {a.map} transfer {n}: map {args[0]} ({args[2]}, {args[3]}, {args[4]}) -> map {new[0]} ({new[2]}, {new[3]}, {new[4]})"]
        for k in range(4):
            if words[k] != ins[1][k]:
                lines.append(f"script {a.map} {base + k:#x} {ins[1][k]:#010x} {words[k]:#010x}")
        emit(lines, a.output)
        return
    raise SystemExit(f"map {a.map}: no map transfer {a.index} (see `list`)")


ITEM_ENTRY = 3002      # item records, 1023 x 0x30 (item 1 first)
SHOP_ENTRY = 0xBAA     # the shop page's code overlay, loaded at 0x8007e000
SHOP_TABLE = 0x80081C98
PAGE_BASE = 0x8007E000
FORMATION_BASE = 0x57D  # encounter N's pack is entry 0x57d + N
ITEM_FIELDS = {"price": (0x00, 4), "atk": (0x08, 2), "def": (0x10, 2), "amount": (0x19, 1),
               "effect": (0x28, 1), "type": (0x2A, 1), "sell": (0x2D, 1)}
ENEMY_FIELDS = {"hp": (0x00, 4), "mp": (0x04, 4), "exp": (0x08, 4), "fol": (0x0C, 4), "speed": (0x16, 2),
                "drop1": (0x18, 2), "drop2": (0x1A, 2), "atk": (0x1C, 2), "def": (0x1E, 2), "int": (0x20, 2),
                "level": (0x24, 2), "eva": (0x26, 2), "acc": (0x28, 2), "guts": (0x36, 1),
                "drop1_odds": (0x37, 1), "drop2_odds": (0x38, 1)}


def field_spec(text, names, default_size):
    if text in names:
        return names[text]
    off, _, size = text.partition(":")
    off = int(off, 0)
    return off, int(size, 0) if size else default_size(off)


def item_size(off):
    return 4 if off == 0 else 2 if 4 <= off < 0x14 else 1


def enemy_size(off):
    return 4 if off < 0x10 else 2 if off < 0x2A or off == 0x3A else 1


def le(data, off, size):
    return int.from_bytes(data[off:off + size], "little")


def item_records(disc):
    return unpack_block(disc.entry(ITEM_ENTRY), 0)


def shop_lists(disc):
    code = unpack_block(disc.entry(SHOP_ENTRY), 0)
    out = {}
    for shop in range(0x60):
        at = SHOP_TABLE + shop * 4 - PAGE_BASE
        if at + 4 > len(code):
            break
        ptr = le(code, at, 4) - PAGE_BASE
        if not 0 <= ptr < len(code):
            continue
        items = []
        while ptr + 2 <= len(code) and le(code, ptr, 2) != 0 and len(items) < 64:
            items.append(le(code, ptr, 2))
            ptr += 2
        out[shop] = items
    return out


def formation_records(pack):
    """[(type, stats record)] of an encounter pack (load_formation 0x8007344c)."""
    p = 0
    def s32(at):
        return struct.unpack_from("<i", pack, at)[0]
    while p + 4 <= len(pack) and s32(p) != -1:   # (1) sprite sets {offset, kinds}
        p += 8
    p += 4
    for _ in range(2):                            # (2) palettes, (3) code blocks {kind, offset}
        while p + 4 <= len(pack) and s32(p) != -1:
            p += 8
        p += 4
    types = []
    while p + 12 <= len(pack) and struct.unpack_from("<h", pack, p)[0] != 0 and len(types) < 8:
        types.append(struct.unpack_from("<h", pack, p)[0])
        p += 12
    r = p + 4
    out = []
    for t in types:
        if r + 0x5C > len(pack):
            break
        out.append((t, pack[r:r + 0x5C]))
        r += 0x5C
    return out


def enemy_stats(disc, wanted=None):
    """{type: (encounter, record)} from the first encounter pack that holds each type."""
    found = {}
    for n in range(0, 0x234):
        try:
            pack = disc.entry(FORMATION_BASE + n)
            records = formation_records(pack)
        except (SystemExit, struct.error, IndexError):
            continue
        for t, rec in records:
            if t not in found:
                found[t] = (n, rec)
        if wanted is not None and wanted in found:
            break
    return found


def cmd_item(a):
    off, size = field_spec(a.field, ITEM_FIELDS, item_size)
    rec = item_records(Disc(a.disc))
    at = (a.item - 1) * 0x30 + off
    if not 1 <= a.item <= len(rec) // 0x30 or off + size > 0x30:
        raise SystemExit(f"no item {a.item} / field {a.field}")
    old = le(rec, at, size)
    spec = f"{off:#x}" if size == item_size(off) else f"{off:#x}:{size}"
    emit([f"# item {a.item:#x} {a.field}: {old} -> {a.value}", f"item {a.item:#x} {spec} {old:#x} {a.value & ((1 << 8 * size) - 1):#x}"], a.output)


def cmd_items(a):
    rec = item_records(Disc(a.disc))
    r = rec[(a.item - 1) * 0x30:a.item * 0x30]
    print(f"item {a.item:#x}: " + " ".join(f"{k}={le(r, o, n)}" for k, (o, n) in ITEM_FIELDS.items()))
    print("  raw " + r.hex())


def cmd_shop(a):
    lists = shop_lists(Disc(a.disc))
    if a.shop not in lists:
        raise SystemExit(f"no shop {a.shop} (see `shops`)")
    items = lists[a.shop]
    if a.index == "add":
        index, old = len(items), 0
    else:
        index = int(a.index, 0)
        if index >= len(items):
            raise SystemExit(f"shop {a.shop} has {len(items)} entries; use `add` to append")
        old = items[index]
    emit([f"# shop {a.shop} entry {index}: item {old:#x} -> item {a.item:#x}", f"shop {a.shop} {index} {old:#x} {a.item:#x}"], a.output)


def cmd_shops(a):
    for shop, items in shop_lists(Disc(a.disc)).items():
        if items:
            print(f"shop {shop:#x}: " + " ".join(f"{i:#x}" for i in items))


def cmd_enemy(a):
    off, size = field_spec(a.field, ENEMY_FIELDS, enemy_size)
    found = enemy_stats(Disc(a.disc), a.type)
    if a.type not in found:
        raise SystemExit(f"enemy type {a.type} not found in the encounter packs")
    n, rec = found[a.type]
    old = le(rec, off, size)
    spec = f"{off:#x}" if size == enemy_size(off) else f"{off:#x}:{size}"
    emit([f"# enemy {a.type} {a.field} (as in encounter {n}): {old} -> {a.value}",
          f"enemy {a.type} {spec} {old:#x} {a.value & ((1 << 8 * size) - 1):#x}"], a.output)


def cmd_enemies(a):
    found = enemy_stats(Disc(a.disc), a.type)
    for t in sorted(found):
        if a.type is not None and t != a.type:
            continue
        n, rec = found[t]
        name = rec[0x4C:0x5C].split(b"\0")[0].decode("latin-1")
        print(f"type {t}: encounter {n} " + " ".join(f"{k}={le(rec, o, s)}" for k, (o, s) in ENEMY_FIELDS.items()) + f" name {name}")


def main():
    num = lambda s: int(s, 0)  # noqa: E731
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("list")
    p.add_argument("disc")
    p.add_argument("map", type=num)
    p.set_defaults(run=cmd_list)
    p = sub.add_parser("chest")
    p.add_argument("disc")
    p.add_argument("map", type=num)
    p.add_argument("flag", type=num)
    g = p.add_mutually_exclusive_group(required=True)
    g.add_argument("--item", type=num)
    g.add_argument("--fol", type=num)
    p.add_argument("-o", "--output")
    p.set_defaults(run=cmd_chest)
    p = sub.add_parser("door")
    p.add_argument("disc")
    p.add_argument("map", type=num)
    p.add_argument("index", type=num)
    p.add_argument("--to", type=num, required=True)
    for k in ("x", "y", "z", "dir"):
        p.add_argument(f"--{k}", type=num)
    p.add_argument("-o", "--output")
    p.set_defaults(run=cmd_door)
    p = sub.add_parser("item")
    p.add_argument("disc")
    p.add_argument("item", type=num)
    p.add_argument("field")
    p.add_argument("value", type=num)
    p.add_argument("-o", "--output")
    p.set_defaults(run=cmd_item)
    p = sub.add_parser("items")
    p.add_argument("disc")
    p.add_argument("item", type=num)
    p.set_defaults(run=cmd_items)
    p = sub.add_parser("shop")
    p.add_argument("disc")
    p.add_argument("shop", type=num)
    p.add_argument("index")
    p.add_argument("item", type=num)
    p.add_argument("-o", "--output")
    p.set_defaults(run=cmd_shop)
    p = sub.add_parser("shops")
    p.add_argument("disc")
    p.set_defaults(run=cmd_shops)
    p = sub.add_parser("enemy")
    p.add_argument("disc")
    p.add_argument("type", type=num)
    p.add_argument("field")
    p.add_argument("value", type=num)
    p.add_argument("-o", "--output")
    p.set_defaults(run=cmd_enemy)
    p = sub.add_parser("enemies")
    p.add_argument("disc")
    p.add_argument("type", type=num, nargs="?")
    p.set_defaults(run=cmd_enemies)
    a = ap.parse_args()
    return a.run(a)


if __name__ == "__main__":
    sys.exit(main())
