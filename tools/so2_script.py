#!/usr/bin/env python3
"""Disassemble Star Ocean 2 field event scripts.

Map N's data is archive entry 3207 + N (0xc87 + N; see the map-transfer opcodes
in program #3100). The entry is a container: a u32 section count, then
(section id, offset) pairs. Section 1 is an SLZ block holding the map's script:

  header (7 u32): code_size, ?, ?, string_count, string_bytes, ?, ?
  code           0x1c .. 0x1c + code_size, 32-bit instructions
  string offsets string_count u16 values, relative to the text start
  text           string_bytes bytes of 16-bit character codes

Instruction word: opcode in bits 24-31, bit 23 selects the thread's locals
(set) or globals (clear), bits 0-22 carry an address or immediate. The VM is
FUN_8006b944 (opcodes below 0x64), FUN_8006241c (0x64 and up), FUN_80067634
(0xfe, sub-command in bits 16-22) and FUN_80064f30 (0xff). Commands at 0x64+
take N arguments (so2_script_args.json, measured by running those handlers):
inline when bit 23 is clear (the first in bits 0-15, the rest packed two
16-bit values per following word, so N // 2 extra words), popped from the
value stack when it is set.

Usage:
  so2_script.py dis ARCHIVE MAP           disassemble map MAP's script
  so2_script.py strings ARCHIVE MAP       list its text
  so2_script.py check ARCHIVE             disassemble every map, report problems
"""

import argparse
import json
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from so2_archive import read_toc, entry_bytes, unpack_block, SLZ_MAGIC  # noqa: E402

MAP_ENTRY_BASE = 0xC87
CODE_START = 0x1C
ARGS = {name: {int(k, 16): tuple(v) for k, v in table.items()}
        for name, table in json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                                       "so2_script_args.json"))).items()}

TYPES = {0: "s32", 1: "s16", 2: "s8", 3: "flag", 8: "u32", 9: "u16", 10: "u8"}
BINOPS = {0x23: "add", 0x25: "sub", 0x27: "mul", 0x29: "div", 0x2b: "mod", 0x2d: "and", 0x2f: "or",
          0x31: "xor", 0x33: "shl", 0x35: "shr", 0x3b: "eq", 0x3d: "ne", 0x3f: "gt", 0x41: "lt",
          0x43: "ge", 0x45: "le"}
ASSIGNOPS = {0x4d: "add", 0x4f: "sub", 0x51: "mul", 0x53: "div", 0x55: "mod", 0x57: "and", 0x59: "or",
             0x5b: "xor", 0x5d: "shl", 0x5f: "shr"}
# Commands whose meaning is established from the handlers; the rest print as ext_XX / A_XX / B_XX.
EXT_NAMES = {0x74: "thread_start", 0x75: "thread_start_ex", 0x7a: "wait", 0x7d: "map_transfer",
             0xa4: "map_transfer_b", 0xda: "map_transfer_c", 0xbb: "map_transfer_table", 0xb7: "battle",
             0xbc: "battle_b", 0xed: "battle_c", 0xb8: "music", 0xc5: "music_ex", 0xd3: "get_counter18",
             0xd4: "add_counter18", 0xf9: "get_state180", 0xfa: "set_state180_random", 0xc8: "play_sfx"}
A_NAMES = {0x01: "add_money", 0x02: "get_money", 0x14: "get_equipment", 0x1e: "get_hp", 0x1f: "set_hp",
           0x2d: "exit_to_program", 0x39: "item_count"}
B_NAMES = {0x5e: "has_item", 0x5f: "item_query", 0x60: "give_item", 0x63: "set_item_count"}


def map_script(archive, map_id):
    """Return the unpacked script section of map MAP_ID."""
    entry = MAP_ENTRY_BASE + map_id
    for index, lba, sectors in read_toc(archive):
        if index == entry:
            with open(archive, "rb") as f:
                data = entry_bytes(f, lba, sectors)
            break
    else:
        raise SystemExit(f"map {map_id}: archive entry {entry} not present")
    count = struct.unpack_from("<I", data, 0)[0]
    if not 1 <= count <= 16:
        raise SystemExit(f"map {map_id}: entry {entry} is not a map container")
    sections = dict(struct.unpack_from("<II", data, 4 + 8 * i) for i in range(count))
    offset = sections.get(1)
    if offset is None or data[offset:offset + 3] != SLZ_MAGIC:
        raise SystemExit(f"map {map_id}: no SLZ script section")
    return unpack_block(data, offset)


def parse_script(blob):
    header = struct.unpack_from("<7I", blob, 0)
    code_size, string_count, string_bytes = header[0], header[3], header[4]
    code = blob[CODE_START:CODE_START + code_size]
    table_at = CODE_START + code_size
    offsets = struct.unpack_from(f"<{string_count}H", blob, table_at)
    text_at = table_at + 2 * string_count
    text = blob[text_at:text_at + string_bytes]
    strings = []
    for i, start in enumerate(offsets):
        end = offsets[i + 1] if i + 1 < len(offsets) else len(text)
        strings.append(text[start:end])
    return {"header": header, "code": code, "strings": strings}


GLYPHS = {0xb4: "-", 0xb5: ".", 0xed: "?", 0xef: "!", 0xff: "=", 0x100: ":", 0x101: ";",
          0x103: ",", 0x104: "/", 0x105: " "}
GLYPHS.update({0xaa + i: str(i) for i in range(10)})
GLYPHS.update({0xb6 + i: chr(ord("A") + i) for i in range(26)})
GLYPHS.update({0xd0 + i: chr(ord("a") + i) for i in range(26)})
# Control codes with the number of parameter bytes that follow them.
CONTROL = {0x4000: ("\\n", 0), 0x4001: ("{page}", 0), 0x4002: ("{wait}", 0), 0x4003: ("speed", 1),
           0x4004: ("colour", 1), 0x4005: ("{centre}", 0), 0x4006: ("delay", 2), 0x4007: ("name_var", 1),
           0x4008: ("item_var", 1), 0x4009: ("choice", 2), 0x400a: ("{block}", 0), 0x400b: ("{/block}", 0),
           0x400c: ("name", 1), 0x400d: ("space", 1), 0x400e: ("number_var", 1), 0x400f: ("x", 1)}


def decode_text(raw):
    """Render a string: a byte stream of codes (b < 0x80, else (b & 0x7f) | next << 7).
    Codes < 0x4000 are 1-based glyphs of the map font; 0x4000-0x400f are control codes."""
    out, i = [], 0
    while i < len(raw):
        b = raw[i]
        i += 1
        code = b
        if b >= 0x80:
            code = (b & 0x7F) | (raw[i] << 7 if i < len(raw) else 0)
            i += 1
        if code == 0:
            break
        if code in CONTROL:
            name, n = CONTROL[code]
            params = raw[i:i + n]
            i += n
            out.append(name if n == 0 else "{%s:%s}" % (name, params.hex()))
        else:
            out.append(GLYPHS.get(code, "{%x}" % code))
    return "".join(out)


def operand(word, typed):
    local = "L" if word & 0x800000 else "G"
    if typed:
        kind = TYPES.get(word >> 16 & 0xF, f"type{word >> 16 & 0xF}")
        addr = word & 0xFFFF
        if word & 0x80FFFF == 0:
            return f"{kind} [indexed]"
        return f"{kind} {local}[{addr:#x}]" if kind != "flag" else f"flag {local}#{addr:#x}"
    return f"{local}[{word & 0x7FFFFF:#x}]"


def disassemble(code):
    """Yield (offset, words, text, branch_target or None, problem or None)."""
    words = struct.unpack_from(f"<{len(code) // 4}I", code, 0)
    i = 0
    while i < len(words):
        w = words[i]
        op = w >> 24
        imm24 = w & 0xFFFFFF
        extra, text, target, problem = 0, None, None, None
        if op in (1, 2, 8):
            text = f"pop_store s32 {operand(w, False)}"
        elif op in (3, 4):
            text = f"pop_store s16 {operand(w, False)}"
        elif op in (5, 6):
            text = f"pop_store s8 {operand(w, False)}"
        elif op == 7:
            text = f"pop_store flag {'L' if w & 0x800000 else 'G'}#{w & 0x7FFFFF:#x}"
        elif op == 9:
            text = f"index_scale {w & 0x7FFFFF}"
        elif op == 0x0A:
            text = f"index_load +{imm24:#x}"
        elif op in (0x0B, 0x0F):
            text = f"push s32 {operand(w, False)}"
        elif op == 0x0C:
            text = f"push s16 {operand(w, False)}"
        elif op == 0x0D:
            text = f"push u8 {operand(w, False)}"
        elif op == 0x0E:
            text = f"push flag {'L' if w & 0x800000 else 'G'}#{w & 0x7FFFFF:#x}"
        elif op == 0x10:
            extra, text = 1, f"index_load_var {operand(w, False)} +{{w1}}"
        elif op == 0x11:
            text = f"ret_mul {w & 0x7FFFFF}"
        elif op == 0x12:
            text = f"push {imm24:#x}"
        elif op == 0x13:
            extra, text = 1, "push {w1:#x}"
        elif op in (0x14, 0x15, 0x16):
            target = imm24 * 4
            text = {0x14: "call", 0x15: "jmp", 0x16: "jz"}[op] + f" {target:#06x}"
        elif op == 0x17:
            text = "return"
        elif op == 0x18:
            extra, text = 1, f"frame flags+{imm24:#x} vars+{{w1:#x}}"
        elif op in (0x19, 0x1B, 0x1C, 0x1D):
            kind = {0x19: "s32", 0x1B: "s16", 0x1C: "u8", 0x1D: "flag"}[op]
            extra, text = 1, f"push_eq {kind} {operand(w, False)}, {{w1:#x}}"
        elif op == 0x1A:
            extra, text = 1, f"store s32 {operand(w, False)} = {{w1:#x}}"
        elif op == 0x21:
            extra, text = 1, f"store {operand(w, True)} = {{w1:#x}}"
        elif op == 0x22:
            text = f"pop_store {operand(w, True)}"
        elif 0x23 <= op <= 0x46 and (op if op & 1 else op - 1) in BINOPS:
            name = BINOPS[op | 1] if op & 1 else BINOPS[op - 1]
            if op & 1:
                extra, text = 1, f"{name} {operand(w, True)}, {{w1:#x}}"
            else:
                text = f"{name} {operand(w, True)}, pop"
        elif op == 0x38:
            text = "logical_and pop, pop"
        elif op == 0x3A:
            text = "logical_or pop, pop"
        elif op == 0x47:
            text = f"inc {operand(w, True)}"
        elif op == 0x48:
            text = f"dec {operand(w, True)}"
        elif op == 0x49:
            text = f"not {operand(w, True)}"
        elif op == 0x4A:
            text = "not pop"
        elif op == 0x4B:
            text = f"is_zero {operand(w, True)}"
        elif op == 0x4C:
            text = "is_zero pop"
        elif 0x4D <= op <= 0x60:
            name = ASSIGNOPS[op if op & 1 else op - 1]
            if op & 1:
                extra, text = 1, f"{name}= {operand(w, True)}, {{w1:#x}}"
            else:
                text = f"{name}= {operand(w, True)}, pop"
        elif op in (0x61, 0x62, 0x63):
            count = words[i + 1] if i + 1 < len(words) else 0
            extra = 1 + count
            text = f"block_store {operand(w, False)} <- {count} words"
        else:
            if op == 0xFE or op == 0xFF:
                sub = w >> 16 & 0x7F
                table, names, label = ("fe", A_NAMES, "A") if op == 0xFE else ("ff", B_NAMES, "B")
                counts = ARGS[table].get(sub)
                name = names.get(sub, f"{label}_{sub:02x}")
            else:
                counts = ARGS["ext"].get(op, (0, 0))
                name = EXT_NAMES.get(op, f"ext_{op:02x}")
            stack = bool(w & 0x800000)
            n = None if counts[1 if stack else 0] is None else counts[1 if stack else 0]
            if n is None:
                problem = f"argument count unknown for {name}"
                n = 0
            if stack:
                text = f"{name} <{n} from stack>"
            else:
                extra = n // 2
                args = [w & 0xFFFF]
                for k in range(extra):
                    if i + 1 + k < len(words):
                        pair = words[i + 1 + k]
                        args += [pair & 0xFFFF, pair >> 16]
                args = [a - 0x10000 if a & 0x8000 else a for a in args[:n]]
                text = f"{name}(" + ", ".join(str(a) for a in args) + ")" if n else name
        follow = words[i + 1:i + 1 + extra]
        if len(follow) < extra:
            problem = problem or "instruction runs past the end of the code"
        if "{w1" in text:
            text = text.format(w1=follow[0] if follow else 0)
        yield i * 4, [w] + list(follow), text, target, problem
        i += 1 + extra


def cmd_dis(args):
    script = parse_script(map_script(args.archive, args.map))
    h = script["header"]
    print(f"; map {args.map} (entry {MAP_ENTRY_BASE + args.map}): code {h[0]:#x} bytes, {h[3]} strings, header {[hex(x) for x in h]}")
    for offset, words, text, target, problem in disassemble(script["code"]):
        raw = " ".join(f"{x:08x}" for x in words)
        print(f"{offset:06x}  {raw:<35} {text}" + (f"   ; !! {problem}" if problem else ""))


def cmd_strings(args):
    script = parse_script(map_script(args.archive, args.map))
    for i, raw in enumerate(script["strings"]):
        print(f"{i:4d}: {decode_text(raw)}")


def cmd_check(args):
    from collections import Counter
    totals, problems, unknown_ops, maps = Counter(), Counter(), Counter(), 0
    toc = {index: (lba, sectors) for index, lba, sectors in read_toc(args.archive) if lba is not None}
    for map_id in range(0, 1200):
        if MAP_ENTRY_BASE + map_id not in toc:
            continue
        try:
            script = parse_script(map_script(args.archive, map_id))
        except SystemExit:
            continue
        maps += 1
        starts, targets = set(), []
        for offset, words, text, target, problem in disassemble(script["code"]):
            starts.add(offset)
            totals["instructions"] += 1
            if target is not None:
                targets.append(target)
            if problem:
                problems[problem] += 1
                unknown_ops[text.split("(")[0].split(" ")[0]] += 1
        # Branch targets are word indexes from the code base; check both candidate bases.
        for base, label in ((CODE_START, "section start"), (0, "code start")):
            bad = sum(1 for t in targets if (t - base) not in starts)
            totals[f"branch targets off-boundary (base = {label})"] += bad
        totals["branches"] += len(targets)
    print(f"{maps} map scripts, {totals['instructions']} instructions, {totals['branches']} branches")
    for key, value in totals.items():
        if "off-boundary" in key:
            print(f"  {key}: {value}")
    for problem, n in problems.most_common():
        print(f"  {n:6d}  {problem}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="command", required=True)
    for name, fn in (("dis", cmd_dis), ("strings", cmd_strings)):
        p = sub.add_parser(name)
        p.add_argument("archive")
        p.add_argument("map", type=int)
        p.set_defaults(run=fn)
    p = sub.add_parser("check")
    p.add_argument("archive")
    p.set_defaults(run=cmd_check)
    args = ap.parse_args()
    return args.run(args)


if __name__ == "__main__":
    sys.exit(main())
