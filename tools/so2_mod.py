#!/usr/bin/env python3
"""World edits for Shattered Stars: write script patches for chests and doors.

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

Limits: a chest holding an item can only be changed to another item (its value is a
24-bit push); a chest holding Fol (a full-word push) can take either. Only the
inline map-transfer form (map_transfer_c with constant arguments) can be retargeted.
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
    a = ap.parse_args()
    return a.run(a)


if __name__ == "__main__":
    sys.exit(main())
