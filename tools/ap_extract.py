#!/usr/bin/env python3
"""Archipelago, field side: the location tables of Star Ocean: The Second Story read straight
from the disc images (docs/ap/field.md; the battle side is tools/ap_battles.py).

  ap_extract.py DISC1 [DISC2] [-o FILE]   writes the JSON tables (stdout without -o)
  ap_extract.py DISC1 [DISC2] --summary   a short human-readable summary

Nothing but numbers and names (items, characters, spells) is written: no dialogue.

How the field scripts give things (so2_script.py has the format):
  - Every map script carries the same library of routines, at different offsets per map.
    A routine is recognised by its shape (its instructions with call targets dropped and
    jump targets made relative); the library is every shape found in most maps.
  - The library's reward routines: give (item, message: give_item, or for an item number
    of 10000 and up the script flag number - 10000 with a fanfare), money (amount: ext 0xd4
    add_money), join (character: ext 0xae party_add) and the chest routine (actor, opened
    flag, value, message: an item when value >= 0, else -value Fol; then A 0x01 counts the
    treasure and the opened flag is set).
  - A reward location is a call into one of them with constant arguments from the map's own
    code (or an inline give_item / add_money / party_add there). Chests are registered by
    B 0x6c chest_create with the opened flag and the handler that calls the chest routine.
  - Script flags (G#n, 0x170 bytes = 2944 flags) and two story globals: game_state +0
    (script byte 0xa0, "chapter": the main story, 3 / 4 at a new game) and +4 (byte 0xa4,
    "step": progress inside a chapter, reset at chapter changes).
"""

import argparse
import collections
import hashlib
import json
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import so2_mod  # noqa: E402
from so2_archive import unpack_block  # noqa: E402

FIELD_MAPS = 1200
LIBRARY_SHARE = 0.33       # a shape in at least this share of the maps is library code
TEXT_ENTRY = 1980          # the battle's string table (names of items, spells, killer moves)
ITEM_NAME_KEY = 0xBB8      # item n's name: key 0xbb8 + n - 1
SPELL_NAME_KEY = 0x3E7     # spell n's name: key 0x3e7 + n
# Character numbers (party_add and party_has take n; a party slot holds n + 1).
CHARACTERS = ["Claude", "Rena", "Celine", "Bowman", "Dias", "Precis", "Ashton", "Leon",
              "Opera", "Ernest", "Noel", "Chisato"]

FLAG_TEST = re.compile(r"^(?:push flag|eq flag|is_zero flag|ne flag) G#(0x[0-9a-f]+)(?:, (0x[0-9a-f]+))?")
FLAG_STORE = re.compile(r"^store flag G#(0x[0-9a-f]+) = (0x[0-9a-f]+)")
STORY_TEST = re.compile(r"^(eq|push_eq|ge|le|lt|gt|ne) s32 G\[0x(a0|a4)\], (0x[0-9a-f]+)")
STORY_STORE = re.compile(r"^store s32 G\[0x(a0|a4)\] = (0x[0-9a-f]+)")
STORY_NAME = {"a0": "chapter", "a4": "step"}

# The world maps (map_transfer to 1 / 2 places the party on that world map) and their chunk
# scripts: every chunk of a world map carries one script, keyed here by the group's first
# map. The world maps' own script (maps 0-3 and 826) holds, besides the world map's code, a
# jump list of fragments that set story values and warp (reached through computed calls; a
# warp menu): its transfers, joins and story stores are kept apart from play.
WORLD_MAPS = {1: "Expel", 2: "Energy Nede", 3: "world map 3"}
CHUNK_GROUPS = {909: 1, 1048: 2, 1113: 3}
WORLD_SCRIPT = 1

# Instructions that register a routine of the map (its entry = byte offset / 4) to run on an
# event, and the argument holding the entry: trigger regions (thread_start, ext 0x75), actors'
# talk handlers (ext 0x70), chests (B 0x6c), Private Actions (B 0x0e) and others seen with
# routine entries (ext 0xf3 / 0xcf / 0xd9 / 0x76 / 0x66, B 0x5c / 0x0a).
REGISTRATIONS = {"thread_start": (9,), "thread_start_ex": (9,), "ext_f3": (7, 9), "ext_70": (7,),
                 "ext_cf": (1,), "B_5c": (1,), "B_0a": (0,), "B_6c": (7,), "ext_d9": (11,), "ext_76": (8,),
                 "ext_66": (1,), "B_0e": (1,)}

# Location ids (docs/ap/field.md): BASE + (type << 21) + (map << 10) + index, index counting
# the map's locations of that type in script order. Shared scripts use their first map.
LOCATION_BASE = 0x50320000
LOCATION_TYPES = {"chest": 1, "gift": 2, "fol_gift": 3, "recruitment": 4, "shop": 5, "private_action": 6,
                  "prize": 7}


# ---- Names ---------------------------------------------------------------------------------

def read_names(disc):
    """{key: name} of the #1980 string table ((u16 key, u16 offset) pairs, codes as the
    battle's text: one byte below 0x80, else two; glyph = code + 0x1f)."""
    t = bytes(unpack_block(disc.entry(TEXT_ENTRY), 0))
    names, p = {}, 0
    while p + 4 <= len(t):
        key, off = struct.unpack_from("<HH", t, p)
        if key == 0xFFFF:
            break
        out, i = [], off
        while i < len(t):
            b = t[i]
            i += 1
            c = b
            if b >= 0x80:
                c = (b & 0x7F) | (t[i] << 7)
                i += 1
            if c == 0:
                break
            out.append(" " if c + 0x1F < 0x21 else chr(c + 0x1F))
        names[key] = "".join(out).strip()
        p += 4
    return names


# ---- Scripts -------------------------------------------------------------------------------

def functions(ins):
    """[(start, end)] instruction ranges of the routines (a frame up to the next one; code
    before the first frame is a routine of its own)."""
    starts = [n for n, i in enumerate(ins) if i[2].startswith("frame")]
    out = [(0, starts[0])] if starts and starts[0] > 0 else []
    for k, s in enumerate(starts):
        out.append((s, starts[k + 1] if k + 1 < len(starts) else len(ins)))
    return out


def shape(ins, s, e):
    base = ins[s][0]
    parts = []
    for i in ins[s:e]:
        t = i[2]
        if t.startswith("call "):
            parts.append("call")
        elif i[3] is not None:
            parts.append(re.sub(r"0x[0-9a-f]+", "J%+d" % (i[3] - base), t, count=1))
        else:
            parts.append(t)
    return hashlib.sha1("\n".join(parts).encode()).hexdigest()[:16]


class Map:
    """One field map's script: instructions, routines and their shapes."""

    def __init__(self, map_id, blob):
        self.id = map_id
        self.ins = so2_mod.instructions(blob)
        self.blob = blob
        self.funcs = functions(self.ins)
        self.shapes = [shape(self.ins, s, e) for s, e in self.funcs]
        self.func_at = {self.ins[s][0]: k for k, (s, e) in enumerate(self.funcs)}
        self.func_of = {}
        for k, (s, e) in enumerate(self.funcs):
            for n in range(s, e):
                self.func_of[n] = k
        self.digest = hashlib.sha1(blob).hexdigest()[:16]

    def callers(self, k):
        """Instruction indices that call routine k."""
        if getattr(self, "_calls", None) is None:
            self._calls = collections.defaultdict(list)
            for n, i in enumerate(self.ins):
                if i[2].startswith("call "):
                    self._calls[i[3]].append(n)
        return self._calls.get(self.ins[self.funcs[k][0]][0], [])


def load_maps(path):
    """{map: Map} of every field map with a script. Maps that share one script (the world
    script of maps 0-3 and 826, the world map chunks 909-980, a dungeon's rooms) are read
    once: the Map of the lowest map number (above 0 when there is one) stands for all of them
    and lists them in .maps."""
    disc = so2_mod.Disc(path)
    blobs = {}
    for m in range(FIELD_MAPS):
        try:
            blobs[m] = disc.map_script(m)
        except SystemExit:
            continue
    groups = collections.defaultdict(list)
    for m, b in blobs.items():
        groups[hashlib.sha1(b).hexdigest()].append(m)
    maps = {}
    for ids in groups.values():
        ids.sort()
        primary = next((m for m in ids if m > 0), ids[0])
        try:
            mp = Map(primary, blobs[primary])
        except Exception:  # noqa: BLE001 (a script the disassembler can't read)
            continue
        mp.maps = ids
        maps[primary] = mp
    return disc, maps


def library(maps):
    """{shape: role} of the library routines that matter here, and the set of all library
    shapes."""
    count = collections.Counter()
    sample = {}
    for mp in maps.values():
        for k, h in enumerate(set(mp.shapes)):
            count[h] += 1
        for k, h in enumerate(mp.shapes):
            sample.setdefault(h, (mp, k))
    lib = {h for h, c in count.items() if c >= LIBRARY_SHARE * len(maps)}
    roles = {}
    for h in lib:
        mp, k = sample[h]
        s, e = mp.funcs[k]
        body = [i[2] for i in mp.ins[s:e]]
        pops = 0
        for t in body[1:]:
            if not t.startswith("pop_store s32 L["):
                break
            pops += 1
        has = lambda p: any(t.startswith(p) for t in body)  # noqa: E731
        if has("give_item <1 from stack>") and pops == 2:
            roles[h] = ("give", pops)
        elif has("add_counter18 <1 from stack>") and pops == 1:
            roles[h] = ("money", pops)
        elif has("ext_ae <1 from stack>") and pops == 1:
            roles[h] = ("join", pops)
        elif has("add_money(1)") and pops == 4:
            roles[h] = ("chest", pops)
        elif has("B_6d <1 from stack>") and pops >= 1:
            roles[h] = ("shop", pops)
    return roles, lib


def choice_shapes(maps, lib):
    """Library routines that ask a choice (ext 0xdc-0xe0) and return its result in R."""
    out = set()
    for mp in maps.values():
        for k, (s_, e_) in enumerate(mp.funcs):
            h = mp.shapes[k]
            if h in lib and h not in out and any(re.match(r"^ext_(dc|dd|de|df|e0)\b", i[2]) for i in mp.ins[s_:e_]):
                out.add(h)
    return out


def args_before(ins, n, count):
    """The constants pushed by the `count` instructions before n (None where not constant)."""
    if n < count:
        return [None] * count
    return [so2_mod.push_value(ins[n - count + j]) for j in range(count)]


COMMON_FLAGS = set()   # flags set to 1 by many routines (scene state, not one-time guards)
# Script flags 2 and 4: Private Action mode (the world chunk scripts set both before a town's
# PA entrance). Flag 1 is the protagonist: set in Claude's game (the new game sets it with
# map 7, chapter 3), clear in Rena's (map 18, chapter 4).
PA_FLAGS = {2, 4}


def common_flags(maps, lib):
    count = collections.Counter()
    for mp in maps.values():
        for k, (s, e) in enumerate(mp.funcs):
            if mp.shapes[k] in lib:
                continue
            for f in {int(m.group(1), 16) for i in mp.ins[s:e] for m in [FLAG_STORE.match(i[2])]
                      if m and int(m.group(2), 16) == 1}:
                count[f] += 1
    return {f for f, c in count.items() if c > 40}


def conditions(mp, k):
    """What a routine tests and sets: flags, the story globals; and the same for the
    routines calling it (one level up)."""
    def scan(k2):
        s, e = mp.funcs[k2]
        tested, stored, story, story_set = set(), {}, [], []
        for i in mp.ins[s:e]:
            t = i[2]
            m = FLAG_TEST.match(t)
            if m:
                tested.add(int(m.group(1), 16))
            m = FLAG_STORE.match(t)
            if m:
                stored[int(m.group(1), 16)] = int(m.group(2), 16)
            m = STORY_TEST.match(t)
            if m:
                story.append([STORY_NAME[m.group(2)], m.group(1).replace("push_", ""), int(m.group(3), 16)])
            m = STORY_STORE.match(t)
            if m:
                story_set.append([STORY_NAME[m.group(1)], int(m.group(2), 16)])
        return tested, stored, story, story_set
    tested, stored, story, story_set = scan(k)
    out = {
        "routine": mp.ins[mp.funcs[k][0]][0],
        "flags_tested": sorted(tested),
        "flags_set": {str(f): v for f, v in sorted(stored.items())},
        "story_tests": story,
        "story_set": story_set,
    }
    guard = sorted(f for f in tested if stored.get(f) == 1 and f not in COMMON_FLAGS)
    if guard:
        out["guard_flags"] = guard
    up = []
    for n in mp.callers(k):
        ck = mp.func_of.get(n)
        if ck is None or ck == k:
            continue
        t2, s2, st2, _ = scan(ck)
        up.append({"routine": mp.ins[mp.funcs[ck][0]][0], "flags_tested": sorted(t2), "story_tests": st2})
    if up:
        out["callers"] = up
    return out


def block_starts(mp, k):
    """Instruction indices where a basic block of routine k starts (the routine's start, every
    jump target inside it, the instruction after a jump)."""
    s, e = mp.funcs[k]
    starts = {s}
    by_offset = {mp.ins[n][0]: n for n in range(s, e)}
    for n in range(s, e):
        i = mp.ins[n]
        if i[3] is not None and not i[2].startswith("call "):
            if i[3] in by_offset:
                starts.add(by_offset[i[3]])
            starts.add(n + 1)
    return sorted(starts)


def party_tests(mp, k):
    """Members a routine asks about (ext 0xb1 party_has with a constant), whether it counts the
    party (ext 0xd5) and whom it removes (ext 0xaf)."""
    s, e = mp.funcs[k]
    has, removes, count = set(), set(), False
    for n in range(s, e):
        t = mp.ins[n][2]
        m = re.match(r"^ext_b1\((\d+)\)", t)
        v = int(m.group(1)) if m else (args_before(mp.ins, n, 1)[0] if t.startswith("ext_b1 <1 from stack>") else None)
        if v is not None:
            has.add(v)
        m = re.match(r"^ext_af\((\d+)\)", t)
        v = int(m.group(1)) if m else (args_before(mp.ins, n, 1)[0] if t.startswith("ext_af <1 from stack>") else None)
        if v is not None:
            removes.add(v)
        if t.startswith("ext_d5"):
            count = True
    return sorted(has), sorted(removes), count


# ---- Conditions --------------------------------------------------------------------------

OPS = {"eq": "==", "ne": "!=", "ge": ">=", "le": "<=", "lt": "<", "gt": ">"}
ARITH = {"add": "+", "sub": "-", "mul": "*", "div": "/", "mod": "%", "and": "&", "or": "|", "xor": "^"}


def operand_name(text):
    """A readable name for a script operand."""
    text = text.strip()
    m = re.match(r"^(?:s32|s16|s8|u32|u16|u8) G\[0x(a0|a4)\]$", text)
    if m:
        return STORY_NAME[m.group(1)]
    m = re.match(r"^flag G#(0x[0-9a-f]+)$", text)
    if m:
        return f"flag[{int(m.group(1), 16)}]"
    m = re.match(r"^(?:s32|s16|s8|u32|u16|u8) G\[(0x[0-9a-f]+)\]$", text)
    if m:
        return f"G[{m.group(1)}]"
    m = re.match(r"^(?:s32|s16|s8|u32|u16|u8) L\[(0x[0-9a-f]+)\]$", text)
    if m:
        return f"local[{m.group(1)}]"
    m = re.match(r"^flag L#(0x[0-9a-f]+)$", text)
    if m:
        return f"local_flag[{int(m.group(1), 16)}]"
    m = re.match(r"^(-?0x[0-9a-f]+|-?\d+)$", text)
    if m:
        return str(int(text, 0))
    return "?"


def signed(text):
    """A script constant as a signed 32-bit number."""
    v = int(text, 16) & 0xFFFFFFFF
    return v - (1 << 32) if v & 0x80000000 else v


def member(n):
    return CHARACTERS[n] if isinstance(n, int) and 0 <= n < len(CHARACTERS) else str(n)


CHOICE_SHAPES = set()   # library routines that put up a choice (ext 0xdc-0xe0) and return it


def local_sources(ins, s, e, mp=None):
    """{local offset: what it holds} for locals a routine loads from a choice (ext 0xdc-0xe0:
    the option picked; or the result of a library routine that asks one), the party count
    (ext 0xd5) or party_has (ext 0xb1)."""
    out = {}
    for p in range(s + 2, e):
        m = re.match(r"^pop_store s32 L\[(0x[0-9a-f]+)\]$", ins[p][2])
        if not m or ins[p - 1][2] != "push s32 G[0x0]":
            continue
        src = ins[p - 2][2]
        callee = mp.func_at.get(ins[p - 2][3]) if mp is not None and src.startswith("call ") else None
        if re.match(r"^ext_(dc|dd|de|df|e0)\b", src) or (callee is not None and mp.shapes[callee] in CHOICE_SHAPES):
            out[m.group(1)] = "choice"
        elif src.startswith("ext_d5"):
            out[m.group(1)] = "party_count"
        else:
            mm = re.match(r"^ext_b1\((\d+)\)", src)
            if mm:
                out[m.group(1)] = f"has({member(int(mm.group(1)))})"
    return out


def condition_at(ins, j, lo, locals_=None):
    """The expression a jz at index j tests, rebuilt by running the code before it (from the
    last statement, not before lo) on a symbolic stack; R is the result register G[0]."""
    start = j
    while start > lo:
        t = ins[start - 1][2]
        if re.match(r"^(jz|jmp|call|store |return|frame|wait|ext_7f|ext_81|pop_store)", t) and \
                not t.startswith("pop_store s32 G[0x0]"):
            break
        start -= 1
    stack, r = [], "?"

    def operand(text):
        name = operand_name(text)
        m = re.match(r"^local\[(0x[0-9a-f]+)\]$", name)
        if m and locals_ and m.group(1) in locals_:
            return locals_[m.group(1)]
        return name

    for i in ins[start:j]:
        t = i[2]
        m = re.match(r"^(eq|ne|ge|le|lt|gt) (.+), (-?0x[0-9a-f]+)$", t)
        if m:
            r = f"{operand(m.group(2))} {OPS[m.group(1)]} {signed(m.group(3))}"
            continue
        m = re.match(r"^push_eq (.+), (-?0x[0-9a-f]+)$", t)
        if m:
            stack.append(f"{operand(m.group(1))} == {signed(m.group(2))}")
            continue
        m = re.match(r"^is_zero (.+)$", t)
        if m:
            r = f"!{operand(m.group(1))}"
            continue
        if t == "push s32 G[0x0]":
            stack.append(r)
            continue
        m = re.match(r"^push (.+)$", t)
        if m:
            v = so2_mod.push_value(i)
            stack.append(str(v) if v is not None else operand(m.group(1)))
            continue
        m = re.match(r"^ext_b1\((\d+)\)", t)
        if m:
            r = f"has({member(int(m.group(1)))})"
            continue
        if t.startswith("ext_b1 <1 from stack>"):
            a = stack.pop() if stack else "?"
            r = f"has({member(int(a)) if a.lstrip('-').isdigit() else a})"
            continue
        m = re.match(r"^B_2c\((\d+)\)", t)
        if m:
            r = f"saved_party_has({member(int(m.group(1)))})"
            continue
        if t.startswith("ext_d5"):
            r = "party_count"
            continue
        m = re.match(r"^B_5e\((\d+)\)|^has_item\((\d+)\)", t)
        if m:
            r = f"has_item({m.group(1) or m.group(2)})"
            continue
        m = re.match(r"^(logical_and|logical_or) pop, pop", t)
        if m:
            b = stack.pop() if stack else "?"
            a = stack.pop() if stack else "?"
            r = f"({a} {'&&' if m.group(1) == 'logical_and' else '||'} {b})"
            continue
        m = re.match(r"^(add|sub|mul|div|mod) pop, pop", t)
        if m:
            b = stack.pop() if stack else "?"
            a = stack.pop() if stack else "?"
            r = f"({a} {ARITH[m.group(1)]} {b})"
            continue
        if t.startswith("not"):
            r = f"!{r}"
            continue
        r = "?"
    return stack[-1] if stack else r


def requirements(mp, k, n):
    """The conditions that must hold (or fail) for instruction n of routine k to run: every
    forward jz around it ('if' bodies) and the jmp-skipped 'else' parts."""
    s, e = mp.funcs[k]
    by_offset = {mp.ins[x][0]: x for x in range(s, e)}
    locals_ = local_sources(mp.ins, s, e, mp)
    out = []
    for j in range(s, n):
        i = mp.ins[j]
        if not i[2].startswith("jz") or i[3] not in by_offset:
            continue
        t = by_offset[i[3]]
        if j < n < t:
            out.append({"if": condition_at(mp.ins, j, s, locals_), "is": True})
        elif t <= n:
            prev = mp.ins[t - 1]
            if prev[2].startswith("jmp") and prev[3] in by_offset and t <= n < by_offset[prev[3]] and t - 1 > j:
                out.append({"if": condition_at(mp.ins, j, s, locals_), "is": False})
    return out


def registrations(mp):
    """{entry: [instruction index]} of the instructions registering routine entries
    (REGISTRATIONS), inline or with constant stack arguments."""
    if getattr(mp, "_regs", None) is not None:
        return mp._regs
    entries = {mp.ins[s][0] // 4 for s, e in mp.funcs}
    out = collections.defaultdict(list)
    for n, i in enumerate(mp.ins):
        t = i[2]
        op = re.match(r"^(\w+)", t).group(1)
        where = REGISTRATIONS.get(op)
        if where is None:
            continue
        m = re.match(r"^\w+ <(\d+) from stack>", t)
        if m:
            args = args_before(mp.ins, n, int(m.group(1)))
        else:
            m = re.match(r"^\w+\((.*)\)", t)
            if not m:
                continue
            args = [int(x) for x in re.findall(r"-?\d+", m.group(1))]
        for j in where:
            if j < len(args) and args[j] in entries:
                out[args[j]].append(n)
    mp._regs = out
    return out


def flat(req):
    """Requirement records as short strings ('X' when it holds, 'not (X)' when it fails)."""
    return [r["if"] if r["is"] else f"not ({r['if']})" for r in req]


def context(mp, k, n, depth=2, seen=None):
    """Why instruction n of routine k runs: the conditions around it in the routine
    ('requires'), and how the routine is reached ('via'): the calls into it with the
    conditions at each call, and the instructions registering it as an event (trigger
    region, talk handler, chest, Private Action...) with theirs; depth levels up."""
    seen = set() if seen is None else seen
    out = {"requires": flat(requirements(mp, k, n))}
    if depth == 0 or k in seen:
        return out
    seen = seen | {k}
    via = []
    for c in mp.callers(k):
        ck = mp.func_of.get(c)
        if ck is None or ck == k:
            continue
        v = {"call_at": mp.ins[c][0]}
        v.update(context(mp, ck, c, depth - 1, seen))
        via.append(v)
    for r in registrations(mp).get(mp.ins[mp.funcs[k][0]][0] // 4, []):
        rk = mp.func_of.get(r)
        if rk is None:
            continue
        v = {"registered_at": mp.ins[r][0], "by": re.match(r"^(\w+)", mp.ins[r][2]).group(1)}
        v.update(context(mp, rk, r, depth - 1, seen))
        via.append(v)
    if via:
        out["via"] = via
    return out


def all_conditions(ctx):
    """Every condition string in a context, its own and its callers' / registrations'."""
    out = list(ctx.get("requires", []))
    for v in ctx.get("via", []):
        out += all_conditions(v)
    return out


def story_window(conds):
    """The chapter range a set of conditions allows (lo, hi; None = open), from the plain
    chapter tests among them (both parts of an 'a && b' count)."""
    lo, hi = None, None
    for c in conds:
        neg = c.startswith("not (")
        body = c[5:-1] if neg else c
        parts = re.findall(r"chapter (==|<=|<|>=|>) (-?\d+)", body)
        if not parts or (neg and len(parts) > 1) or (neg and "||" in body) or ("||" in body):
            continue
        for op, v in parts:
            v = int(v)
            if neg:
                op = {"==": "!=", "<": ">=", "<=": ">", ">": "<=", ">=": "<"}[op]
            if op == "==":
                lo = v if lo is None else max(lo, v)
                hi = v if hi is None else min(hi, v)
            elif op in ("<", "<="):
                h = v - 1 if op == "<" else v
                hi = h if hi is None else min(hi, h)
            elif op in (">", ">="):
                l2 = v + 1 if op == ">" else v
                lo = l2 if lo is None else max(lo, l2)
    return lo, hi


def push_form(ins):
    """How an instruction holds its value: 'imm24' (push imm24, 0x12: a 24-bit value, so a
    patch can only put another item there), 'word' (push word, 0x13: the next word, any
    value) or 'inline' (an instruction with inline arguments)."""
    op = ins[1][0] >> 24
    return "imm24" if op == 0x12 else "word" if op == 0x13 else "inline"


def patch_word(ins, form, role):
    """The word a placeholder patch replaces ("word": byte offset / 4, the mods.cpp number) and
    its value on the disc ("expected"): the push itself for imm24 and for an inline give_item
    (the item in bits 0-15), the next word for a word push and for an inline add_money (the
    amount as lo16 | hi16 << 16). apworld/so2/patches.py builds the patch lines from these."""
    k = 1 if form == "word" or (form == "inline" and role == "fol") else 0
    return {"word": ins[0] // 4 + k, "expected": ins[1][k], "role": role}


def reward_sites(maps, roles, lib):
    """Reward events: in the maps' own code (not the library), every call into the give /
    money / join routines with constant arguments and every inline give_item / add_money /
    party_add, grouped by basic block (one event can give several things)."""
    events, dynamic = [], []
    for mp in maps.values():
        blocks = {}
        for k in range(len(mp.funcs)):
            if mp.shapes[k] in lib:
                continue
            starts = block_starts(mp, k)
            s, e = mp.funcs[k]
            for n in range(s, e):
                t = mp.ins[n][2]
                got = None
                push = n   # the instruction holding the value (the first argument's push)
                if t.startswith("call ") and mp.ins[n][3] in mp.func_at:
                    callee = mp.shapes[mp.func_at[mp.ins[n][3]]]
                    if callee not in roles or roles[callee][0] in ("chest", "shop"):
                        continue
                    role, pops = roles[callee]
                    vals = args_before(mp.ins, n, pops)
                    push = n - pops
                    if vals[0] is None:
                        dynamic.append({"map": mp.id, "offset": mp.ins[n][0], "role": role,
                                        "args": [mp.ins[n - pops + j][2] for j in range(pops)]})
                        continue
                    if role == "give":
                        got = ("flag", vals[0] - 10000) if vals[0] >= 10000 else ("item", vals[0])
                    elif role == "money":
                        got = ("fol", vals[0])
                    elif role == "join":
                        got = ("join", vals[0])
                else:
                    m = re.match(r"^give_item\((\d+)\)", t)
                    if m:
                        got = ("item", int(m.group(1)))
                    m = re.match(r"^add_counter18\((-?\d+), (-?\d+), (-?\d+)\)", t)
                    if m:
                        v = (int(m.group(2)) & 0xFFFF) | (int(m.group(3)) & 0xFFFF) << 16
                        got = ("fol", v - (1 << 32) if v & 0x80000000 else v)
                    m = re.match(r"^ext_ae\((\d+)\)", t)
                    if m:
                        got = ("join", int(m.group(1)))
                if got is None:
                    continue
                b = max(x for x in starts if x <= n)
                ev = blocks.get((k, b))
                if ev is None:
                    ev = {"map": mp.id, "offset": mp.ins[n][0], "routine": mp.ins[s][0], "block": mp.ins[b][0],
                          "items": [], "fol": [], "flags_given": [], "joins": []}
                    ev.update(context(mp, k, n))
                    blocks[(k, b)] = ev
                key = {"item": "items", "fol": "fol", "flag": "flags_given", "join": "joins"}[got[0]]
                ev[key].append(got[1])
                form = push_form(mp.ins[push])
                site = {"what": got[0], "value": got[1], "at": mp.ins[n][0], "value_at": mp.ins[push][0],
                        "form": form}
                if got[0] in ("item", "fol"):
                    site.update(patch_word(mp.ins[push], form, got[0]))
                    held = site["expected"] & (0xFFFFFF if form == "imm24" else 0xFFFF if got[0] == "item" and
                                               form == "inline" else 0xFFFFFFFF)
                    want = got[1] & (0xFFFFFF if form == "imm24" else 0xFFFF if got[0] == "item" and
                                     form == "inline" else 0xFFFFFFFF)
                    if held != want:
                        raise SystemExit(f"map {mp.id} {mp.ins[n][0]:#x}: {got} not in word {site['expected']:#x}")
                ev.setdefault("sites", []).append(site)
        per_routine = collections.Counter(ev["routine"] for ev in blocks.values())
        for (k, b), ev in sorted(blocks.items(), key=lambda kv: kv[1]["offset"]):
            ev.update(conditions(mp, k))
            ev["blocks_in_routine"] = per_routine[ev["routine"]]
            if ev["joins"]:
                has, removes, count = party_tests(mp, k)
                ev["party_has_tests"] = has
                ev["party_removes"] = removes
                ev["party_count_test"] = count
            events.append(ev)
    return events, dynamic


def chests(maps):
    """Every chest: B 0x6c chest_create registers the opened flag and the handler that calls
    the chest routine; the conditions are those around the registration (a chest that only
    appears after an event)."""
    out = []
    for mp in maps.values():
        regs = registrations(mp)
        for flag, handler, value_ins in so2_mod.chests(mp.blob):
            v = so2_mod.push_value(value_ins)
            c = {"map": mp.id, "flag": flag, "handler": handler * 4, "value_at": value_ins[0],
                 "form": push_form(value_ins)}
            # A chest's placeholder is always an item one: for a Fol chest it replaces the
            # negative value, so the chest routine takes its item branch.
            c.update(patch_word(value_ins, c["form"], "item"))
            if v is None:
                c["dynamic"] = True
            elif v < 0:
                c["fol"] = -v
            else:
                c["item"] = v
            at = [r for r in regs.get(handler, []) if mp.ins[r][2].startswith("B_6c")]
            if at:
                c["registered_at"] = mp.ins[at[0]][0]
                rk = mp.func_of.get(at[0])
                if rk is not None:
                    c.update(context(mp, rk, at[0], depth=1))
            out.append(c)
    return out


def shop_sites(maps):
    """B 0x6d menu_request_2 (the shop menu) with its shop id, inline or pushed."""
    out = []
    for mp in maps.values():
        for n, i in enumerate(mp.ins):
            t = i[2]
            m = re.match(r"^B_6d\((\d+)\)", t)
            shop = int(m.group(1)) if m else None
            if shop is None and t.startswith("B_6d <1 from stack>"):
                shop = args_before(mp.ins, n, 1)[0]
                if shop is None:
                    continue
            if shop is None:
                continue
            k = mp.func_of.get(n)
            s = {"map": mp.id, "offset": i[0], "shop": shop}
            if k is not None:
                s.update(conditions(mp, k))
                s.update(context(mp, k, n))
            out.append(s)
    return out


# ---- Place names -----------------------------------------------------------------------------

def clean_name(text):
    """A map-name notice as plain text: colour and timing codes dropped, {name:NN} as the
    character's name, the font's apostrophe (0x106) and ampersand (0x115)."""
    text = re.sub(r"\{(speed|delay):[0-9a-f]+\}", "", text)
    text = re.sub(r"\{(fd|f6)\}", " ", text)
    text = re.sub(r"\{name:([0-9a-f]+)\}", lambda m: member(int(m.group(1), 16)), text)
    text = text.replace("{106}", "'").replace("{115}", "&")
    return re.sub(r"\s+", " ", text).strip()


def place_names(maps):
    """{map: [names]}: the map-name notices (ext 0xb9 with a string; the banner shown on
    entering) in each map's script. A shared script's names go to its first map."""
    from so2_script import decode_text, parse_script
    out = {}
    for mp in maps.values():
        strings = parse_script(mp.blob)["strings"]
        found = []
        for i in mp.ins:
            m = re.match(r"^ext_b9\((-?\d+), (-?\d+), (-?\d+), (-?\d+)\)", i[2])
            if not m:
                continue
            idx = (int(m.group(2)) & 0xFFFF) | (int(m.group(3)) & 0xFFFF) << 16
            if idx < len(strings):
                name = clean_name(decode_text(strings[idx]))
                if name and name not in found:
                    found.append(name)
        if found:
            out[mp.id] = found
    return out


# Area names for maps without a banner of their own, which the nearest-name search gets wrong
# or misses (docs/ap/field.md, "Place names", has the guide's pages). They take precedence over
# that search.
GUIDE_AREAS = [((229, 230), "El Beach"),          # the shipwreck's beach, by the Eluria Colony
               ((340, 380), "Mountain Palace"),   # entered at map 355
               ((381, 385), "Lasguss Mountain"),
               ((548, 548), "Ship to El"),        # the ship out of Port Hilton (chapter 29)
               ((647, 685), "Fienal")]            # 671 is its 9th floor, the Final Save Point


def areas(maps, trs, names, worlds):
    """{map: area name}: the name shown at the nearest map entered from a world map (a town or
    dungeon entrance), else at the nearest map with a name. Searched over unconditional
    two-way doors first, then over any transfer between maps of the same world (story
    transfers join far places)."""
    entrances = {t["to"] for t in trs if "from_world" in t}
    plain, anyway = collections.defaultdict(set), collections.defaultdict(set)
    for t in trs:
        if t.get("world_script") or "from_world" in t or t["to"] in WORLD_MAPS or t["from"] in WORLD_MAPS:
            continue
        anyway[t["from"]].add(t["to"])
        if not t.get("requires"):
            plain[t["from"]].add(t["to"])
    doors, same = collections.defaultdict(set), collections.defaultdict(set)
    for a in anyway:
        for b in anyway[a]:
            if b in plain.get(a, ()) and a in plain.get(b, ()):
                doors[a].add(b)
                doors[b].add(a)
            if worlds.get(a) == worlds.get(b) and worlds.get(a) is not None:
                same[a].add(b)
                same[b].add(a)

    def nearest(m, links, want):
        seen, frontier = {m}, [m]
        while frontier:
            for x in sorted(frontier):
                if want(x):
                    return names[x][0]
            nxt = []
            for x in frontier:
                for y in links[x]:
                    if y not in seen:
                        seen.add(y)
                        nxt.append(y)
            frontier = nxt
        return None

    out = {}
    for mp in maps.values():
        if mp.id in CHUNK_GROUPS or mp.id == WORLD_SCRIPT:
            continue
        for m in mp.maps:
            for links in (doors, same):
                best = nearest(m, links, lambda x: x in entrances and x in names) or \
                    nearest(m, links, lambda x: x in names)
                if best is not None:
                    out[m] = best
                    break
    return out


# ---- Story, regions --------------------------------------------------------------------------

TRANSFER = re.compile(r"^(map_transfer|map_transfer_b|map_transfer_c)\((-?\d+)")
TRANSFER_STACK = re.compile(r"^(map_transfer|map_transfer_b|map_transfer_c) <(\d+) from stack>")


def transfers(maps):
    """Every map transfer with a constant target (inline, or the first of its stacked
    arguments), with the conditions around it (one level of callers / registrations)."""
    out = []
    for mp in maps.values():
        for n, i in enumerate(mp.ins):
            t = i[2]
            m = TRANSFER.match(t)
            target = int(m.group(2)) if m else None
            if target is None:
                m2 = TRANSFER_STACK.match(t)
                if m2:
                    target = args_before(mp.ins, n, int(m2.group(2)))[0]
            if target is None or target < 0:
                continue
            tr = {"from": mp.id, "to": target, "offset": i[0]}
            if mp.id == WORLD_SCRIPT:
                tr["world_script"] = True
            if mp.id in CHUNK_GROUPS:
                tr["from_world"] = CHUNK_GROUPS[mp.id]
            k = mp.func_of.get(n)
            if k is not None:
                tr.update(context(mp, k, n, depth=1))
            out.append(tr)
    return out


# ---- Late access (Virtual Expel) ---------------------------------------------------------------
#
# Saving at Fienal's Final Save Point (map 671) sets flag 730. From then on (chapter 60, before
# the final boss) the old man in Fun City's Fighting Arena (map 303, actor 25) sends the party
# to Virtual Expel: Arlia (map 18), then the Expel world map with every town and dungeon, the
# same maps and opened flags as on disc 1. Energy Nede stays open too. A map the party can
# still enter then is never lost; any other map is open only for a stretch of the story.

LATE_CHAPTER = 60
LATE_FLAGS = {730: True, 753: False, 2: False, 4: False}   # 753: set after the final boss; 2 / 4: PA mode
LATE_STARTS = (18,)                                         # Virtual Expel's arrival (303 -> 18)
COND_ATOM = re.compile(r"(?:\?|[A-Za-z_]\w*(?:\[[^\]]*\]|\([^()]*\))?)(?:\s*(?:==|!=|>=|<=|<|>)\s*-?(?:0x[0-9a-f]+|\d+))?")


def tri_eval(text, atom):
    """Three-valued truth (True / False / None = unknown) of a decompiled condition (&&, ||,
    not, parentheses; atoms as COND_ATOM). atom(text) gives each atom's value. Anything the
    parser doesn't follow is unknown."""
    pos = 0

    def skip():
        nonlocal pos
        while pos < len(text) and text[pos] == " ":
            pos += 1

    def unary():
        nonlocal pos
        skip()
        if text.startswith("not", pos) and not (text[pos + 3:pos + 4].isalnum() or text[pos + 3:pos + 4] == "_"):
            pos += 3
            v = unary()
            return None if v is None else not v
        if text.startswith("(", pos):
            pos += 1
            v = disj()
            skip()
            if not text.startswith(")", pos):
                raise ValueError(text)
            pos += 1
            return v
        m = COND_ATOM.match(text, pos)
        if not m or m.end() == pos:
            raise ValueError(text)
        pos = m.end()
        return atom(m.group(0))

    def conj():
        nonlocal pos
        v = unary()
        while True:
            skip()
            if not text.startswith("&&", pos):
                return v
            pos += 2
            w = unary()
            v = False if v is False or w is False else (None if v is None or w is None else True)

    def disj():
        nonlocal pos
        v = conj()
        while True:
            skip()
            if not text.startswith("||", pos):
                return v
            pos += 2
            w = conj()
            v = True if v is True or w is True else (None if v is None or w is None else False)

    try:
        v = disj()
        skip()
        return v if pos == len(text) else None
    except ValueError:
        return None


def late_atom(a):
    """An atom's value at chapter LATE_CHAPTER with LATE_FLAGS; everything else unknown."""
    m = re.fullmatch(r"chapter\s*(==|!=|>=|<=|<|>)\s*(-?(?:0x[0-9a-f]+|\d+))", a)
    if m:
        n = int(m.group(2), 0)
        return {"==": LATE_CHAPTER == n, "!=": LATE_CHAPTER != n, ">=": LATE_CHAPTER >= n,
                "<=": LATE_CHAPTER <= n, "<": LATE_CHAPTER < n, ">": LATE_CHAPTER > n}[m.group(1)]
    m = re.fullmatch(r"flag\[(\d+)\](?:\s*(==|!=)\s*(\d+))?", a)
    if m and int(m.group(1)) in LATE_FLAGS:
        v = LATE_FLAGS[int(m.group(1))]
        if m.group(2):
            v = (v == bool(int(m.group(3)))) if m.group(2) == "==" else (v != bool(int(m.group(3))))
        return v
    return None


def possible(ctx, atom):
    """Can this context run (its own conditions, and one of its callers' if it has any)?"""
    if any(tri_eval(c, atom) is False for c in ctx.get("requires", [])):
        return False
    via = ctx.get("via")
    return not via or any(possible(v, atom) for v in via)


def late_maps(trs):
    """The maps the party can still enter at chapter 60 after the Final Save Point: from the
    world maps' entrances and Virtual Expel's arrival, over every transfer whose conditions
    can hold then (unknown flags and steps count as possible; the world script's warps don't
    count)."""
    seen = set(LATE_STARTS) | set(WORLD_MAPS)
    for t in trs:
        if "from_world" in t and possible(t, late_atom):
            seen.add(t["to"])
    out = collections.defaultdict(list)
    for t in trs:
        if not t.get("world_script") and "from_world" not in t:
            out[t["from"]].append(t)
    frontier = sorted(seen)
    while frontier:
        nxt = []
        for m in frontier:
            for t in out.get(m, []):
                if t["to"] not in seen and possible(t, late_atom):
                    seen.add(t["to"])
                    nxt.append(t["to"])
        frontier = nxt
    return sorted(seen)


def chapter_values(mp):
    """The chapter values a map's script tests and stores."""
    tested, stored = set(), set()
    for i in mp.ins:
        m = STORY_TEST.match(i[2])
        if m and m.group(2) == "a0":
            tested.add(int(m.group(3), 16))
        m = STORY_STORE.match(i[2])
        if m and m.group(1) == "a0":
            stored.add(int(m.group(2), 16))
    return tested, stored


def regions(maps, trs):
    """Door regions: the maps joined by transfers that go both ways with no condition either
    way (doors; one-way and conditional transfers are scenes and story moves). Each
    region lists the world maps it is entered from (the chunk scripts) and left to, and the
    chapter values its scripts test and store."""
    plain, anyway = collections.defaultdict(set), collections.defaultdict(set)
    entered, left = collections.defaultdict(set), collections.defaultdict(set)
    oneway = []
    for t in trs:
        if t.get("world_script"):
            continue
        a, b = t["from"], t["to"]
        if "from_world" in t:
            entered[b].add(t["from_world"])
            continue
        if b in WORLD_MAPS:
            left[a].add(b)
            continue
        anyway[a].add(b)
        if not t.get("requires"):
            plain[a].add(b)
    link = collections.defaultdict(set)
    for a in list(plain):
        for b in plain[a]:
            if a in plain.get(b, ()):
                link[a].add(b)
                link[b].add(a)
    for a in list(anyway):
        for b in anyway[a]:
            if b not in link[a]:
                oneway.append((a, b))
    skip = set(WORLD_MAPS) | {m for g in CHUNK_GROUPS for m in maps[g].maps if g in maps} | \
        (set(maps[WORLD_SCRIPT].maps) if WORLD_SCRIPT in maps else set())
    owner = {}
    for mp in maps.values():
        for m in mp.maps:
            owner[m] = mp
    region_of, out = {}, []
    for m in sorted(owner):
        if m in skip or m in region_of:
            continue
        stack, members = [m], []
        region_of[m] = len(out)
        while stack:
            x = stack.pop()
            members.append(x)
            for y in link[x]:
                if y not in region_of and y not in skip:
                    region_of[y] = len(out)
                    stack.append(y)
        members.sort()
        tested, stored = set(), set()
        for x in members:
            if x in owner and owner[x].id == x:
                t2, s2 = chapter_values(owner[x])
                tested |= t2
                stored |= s2
        worlds = sorted(set().union(*[entered[x] | left[x] for x in members]))
        out.append({"id": len(out), "maps": members, "worlds": worlds,
                    "world_names": [WORLD_MAPS[w] for w in worlds],
                    "entered_from_world": sorted({(x, w) for x in members for w in entered[x]}),
                    "left_to_world": sorted({(x, w) for x in members for w in left[x]}),
                    "chapters_tested": sorted(tested), "chapters_stored": sorted(stored)})
    return out, region_of, sorted(set(oneway))


NEDE_FROM_CHAPTER = 36   # the disc switch moves the party to Energy Nede during chapter 35


def map_worlds(maps, trs, region_of, region_list):
    """{map: world} for every field map: its region's world map when the region touches one
    only; else the median chapter value its script tests and stores (below 36: Expel, from 36:
    Energy Nede; the ending's chapter 60 tests in old towns are why it is the median); else
    the nearest map with a world over any transfer. 0 for the hub map 625 and others whose
    region touches both."""
    owner = {}
    for mp in maps.values():
        for m in mp.maps:
            owner[m] = mp
    out = {}
    for m, mp in owner.items():
        if m in WORLD_MAPS:
            out[m] = m
            continue
        if mp.id in CHUNK_GROUPS:
            out[m] = CHUNK_GROUPS[mp.id]
            continue
        if mp.id == WORLD_SCRIPT:
            continue
        r = region_of.get(m)
        worlds = region_list[r]["worlds"] if r is not None else []
        if len(worlds) == 1:
            out[m] = worlds[0]
        elif len(worlds) > 1:
            out[m] = 0
        elif mp.id == m:
            tested, stored = chapter_values(mp)
            vals = sorted(v for v in tested | stored if v <= 100)
            if vals:
                out[m] = 1 if vals[len(vals) // 2] < NEDE_FROM_CHAPTER else 2
    near = collections.defaultdict(set)
    for t in trs:
        if not t.get("world_script") and "from_world" not in t and t["to"] not in WORLD_MAPS:
            near[t["from"]].add(t["to"])
            near[t["to"]].add(t["from"])
    todo = [m for m in owner if m not in out and owner[m].id != WORLD_SCRIPT]
    for m in todo:
        seen, frontier = {m}, [m]
        found = None
        while frontier and found is None:
            nxt = []
            for x in frontier:
                for y in sorted(near[x]):
                    if y in seen:
                        continue
                    seen.add(y)
                    if out.get(y) in (1, 2):
                        found = out[y]
                        break
                    nxt.append(y)
                if found is not None:
                    break
            frontier = nxt
        if found is not None:
            out[m] = found
    return out


def story(maps, trs, region_of, region_list):
    """The main story's order: every store to the chapter global (outside the world script's
    warp list) with its conditions and region, in chapter order; the disc switch (B 0x6a)
    with the chapter it is part of and the transfer after it."""
    chapters = collections.defaultdict(list)
    warps = []
    switches = []
    for mp in maps.values():
        for n, i in enumerate(mp.ins):
            m = STORY_STORE.match(i[2])
            k = mp.func_of.get(n)
            if m and m.group(1) == "a0":
                v = int(m.group(2), 16)
                if mp.id == WORLD_SCRIPT:
                    warps.append(v)
                    continue
                rec = {"map": mp.id, "offset": i[0]}
                if k is not None:
                    rec.update(context(mp, k, n, depth=1))
                r = region_of.get(mp.id)
                if r is not None:
                    rec["region"] = r
                    rec["worlds"] = region_list[r]["worlds"]
                chapters[v].append(rec)
            if i[2].startswith("B_6a") and k is not None:
                s_, e_ = mp.funcs[k]
                before = [int(x.group(2), 16) for x in (STORY_STORE.match(mp.ins[j][2]) for j in range(s_, n))
                          if x and x.group(1) == "a0"]
                after = None
                for j in range(n + 1, e_):
                    mm = TRANSFER.match(mp.ins[j][2])
                    if mm:
                        after = int(mm.group(2))
                        break
                rec = {"map": mp.id, "offset": i[0], "chapter": before[-1] if before else None,
                       "transfer_to": after}
                rec.update(context(mp, k, n, depth=1))
                switches.append(rec)
    return {"chapters": [{"chapter": v, "set_at": chapters[v]} for v in sorted(chapters)],
            "world_script_warp_chapters": sorted(set(warps)),
            "disc_switch": switches}


def story_points(maps):
    """Every store to the story globals: (global, value, map, routine)."""
    out = []
    for mp in maps.values():
        for n, i in enumerate(mp.ins):
            m = STORY_STORE.match(i[2])
            if m:
                k = mp.func_of.get(n)
                rec = {"global": STORY_NAME[m.group(1)], "value": int(m.group(2), 16), "map": mp.id,
                       "offset": i[0], "routine": mp.ins[mp.funcs[k][0]][0] if k is not None else None}
                if mp.id == WORLD_SCRIPT:
                    rec["world_script"] = True
                out.append(rec)
    return out


# ---- Private Actions -----------------------------------------------------------------------

def rewards_in(mp, k, roles, depth=0, seen=None):
    """What routine k (and the map routines it calls, two levels down) gives or sets: items,
    Fol, members (constant arguments only), script flags set to 1, story values stored."""
    seen = set() if seen is None else seen
    if k in seen:
        return {"items": [], "fol": [], "joins": [], "flags": [], "story": []}
    seen.add(k)
    s, e = mp.funcs[k]
    out = {"items": [], "fol": [], "joins": [], "flags": [], "story": []}
    for n in range(s, e):
        t = mp.ins[n][2]
        m = re.match(r"^give_item\((\d+)\)", t)
        if m:
            out["items"].append(int(m.group(1)))
        m = FLAG_STORE.match(t)
        if m and int(m.group(2), 16) == 1:
            out["flags"].append(int(m.group(1), 16))
        m = STORY_STORE.match(t)
        if m:
            out["story"].append([STORY_NAME[m.group(1)], int(m.group(2), 16)])
        if t.startswith("call ") and mp.ins[n][3] in mp.func_at:
            ck = mp.func_at[mp.ins[n][3]]
            role = roles.get(mp.shapes[ck])
            if role is not None:
                v = args_before(mp.ins, n, role[1])
                if role[0] == "give" and v[0] is not None:
                    if v[0] >= 10000:
                        out["flags"].append(v[0] - 10000)
                    else:
                        out["items"].append(v[0])
                elif role[0] == "money" and v[0] is not None:
                    out["fol"].append(v[0])
                elif role[0] == "join" and v[0] is not None:
                    out["joins"].append(v[0])
                elif role[0] == "chest" and v[2] is not None:
                    (out["items"] if v[2] >= 0 else out["fol"]).append(v[2] if v[2] >= 0 else -v[2])
            elif depth < 2 and mp.shapes[ck] not in LIB_SHAPES:
                sub = rewards_in(mp, ck, roles, depth + 1, seen)
                for key in out:
                    out[key] += sub[key]
    return out


LIB_SHAPES = set()


PA_MODE = re.compile(r"(?:^|[^\w])flag\[2\](?: == 1)?(?:$|[^\w=])")
PARTNER = re.compile(r"(?<!not )saved_party_has\((\w+)\)")


def flag_index(maps):
    """{flag: maps whose scripts test it}."""
    out = collections.defaultdict(set)
    for mp in maps.values():
        for i in mp.ins:
            m = FLAG_TEST.match(i[2])
            if m:
                out[int(m.group(1), 16)].add(mp.id)
    return out


def pa_events(maps, roles, flags_tested):
    """Private Actions. Entering a town from the world map in Private Action mode sets script
    flags 2 and 4 (the chunk scripts store both before such a transfer); the town's set-up
    then registers its PA events (talk handlers, trigger regions) under flag[2], most of
    them also under saved_party_has(X) (B 0x2c: X was in the party when the PA began, the
    partner). One record per registered routine: where and when it is offered (each
    registration's conditions), the partner(s), and what the routine gives or sets."""
    out = []
    for mp in maps.values():
        if mp.id == WORLD_SCRIPT or mp.id in CHUNK_GROUPS:
            continue
        for entry, idxs in sorted(registrations(mp).items()):
            offers = []
            for r in idxs:
                k = mp.func_of.get(r)
                if k is None:
                    continue
                req = flat(requirements(mp, k, r))
                if not any(PA_MODE.search(c) and not c.startswith("not ") and "flag[2] == 0" not in c
                           for c in req):
                    continue
                offers.append({"registered_at": mp.ins[r][0], "by": re.match(r"^(\w+)", mp.ins[r][2]).group(1),
                               "requires": req})
            if not offers:
                continue
            rk = mp.func_at.get(entry * 4)
            ev = {"map": mp.id, "routine": entry * 4, "offset": entry * 4, "offers": offers,
                  "partners": sorted({p for o in offers for c in o["requires"] for p in PARTNER.findall(c)})}
            windows = [story_window(o["requires"]) for o in offers]
            if all(w[1] is not None for w in windows):
                ev["window"] = [min((w[0] for w in windows if w[0] is not None), default=None),
                                max(w[1] for w in windows)]
                ev["missable"] = True
            else:
                ev["missable"] = False
            if rk is not None:
                g = rewards_in(mp, rk, roles)
                ev["gives"] = {key: v for key, v in g.items() if v}
                used = sorted(f for f in set(g["flags"]) if flags_tested.get(f, set()) - {mp.id})
                if used:
                    ev["flags_tested_elsewhere"] = used
            out.append(ev)
    return out


def private_actions(maps, roles):
    """B 0x0e (trigger, entry) sites: a routine (entry * 4) bound to a trigger, with the
    set-up's conditions and what the routine gives or sets (named trigger_set_pa in the
    catalog; see pa_events for the Private Actions themselves)."""
    out = []
    for mp in maps.values():
        for n, i in enumerate(mp.ins):
            t = i[2]
            m = re.match(r"^B_0e\((-?\d+), (-?\d+)\)", t)
            if m:
                entry = int(m.group(2))
            elif t.startswith("B_0e <2 from stack>"):
                entry = args_before(mp.ins, n, 2)[1]
                if entry is None:
                    continue
            else:
                continue
            pa = {"map": mp.id, "offset": i[0], "routine": entry * 4}
            k = mp.func_of.get(n)
            if k is not None:
                c = conditions(mp, k)
                pa["offered_if"] = {"flags_tested": c["flags_tested"], "story_tests": c["story_tests"]}
                pa["offered_if"].update(context(mp, k, n))
            rk = mp.func_at.get(entry * 4)
            if rk is not None:
                r = rewards_in(mp, rk, roles)
                pa["gives"] = {key: v for key, v in r.items() if v}
                c = conditions(mp, rk)
                pa["runs_if"] = {"flags_tested": c["flags_tested"], "story_tests": c["story_tests"]}
            out.append(pa)
    return out


# ---- Build -----------------------------------------------------------------------------------

def mark_window(rec):
    """'window': the chapters a location is open in (from its conditions), and 'missable'
    when the story closes it (an upper bound)."""
    conds = all_conditions(rec.get("offered_if", rec))
    lo, hi = story_window(conds)
    if lo is not None or hi is not None:
        rec["window"] = [lo, hi]
    rec["missable"] = hi is not None
    return rec


def classify_joins(joins):
    """Kinds of party_add sites: 'world_script' (the warp list), 'restore' (a routine that can
    add four or more different members: the party put back together after a split, member by
    member under the flags that remember it), 'choice' (the join follows a yes / no choice),
    'scene' (any other: the story adds the member)."""
    per_routine = collections.defaultdict(set)
    for j in joins:
        per_routine[(j["map"], j["routine"])].update(j["joins"])
    for j in joins:
        if j["map"] == WORLD_SCRIPT:
            j["join_kind"] = "world_script"
        elif len(per_routine[(j["map"], j["routine"])]) >= 4:
            j["join_kind"] = "restore"
        elif any("choice" in c for c in all_conditions(j)):
            j["join_kind"] = "choice"
        else:
            j["join_kind"] = "scene"
    by_char = {}
    for n, name in enumerate(CHARACTERS):
        sites = [j for j in joins if n in j["joins"] and j["join_kind"] in ("choice", "scene")]
        sites.sort(key=lambda j: (j.get("window", [None])[0] is None, (j.get("window") or [0])[0] or 0, j["map"]))
        by_char[name] = {
            "sites": [{"map": j["map"], "offset": j["offset"], "kind": j["join_kind"],
                       "window": j.get("window"), "conditions": all_conditions(j)} for j in sites],
            "restore_sites": sorted({(j["map"], j["offset"]) for j in joins
                                     if n in j["joins"] and j["join_kind"] == "restore"}),
            "party_has_tests_near": sorted({member(x) for j in sites for x in j.get("party_has_tests", [])}),
            "party_cap_tests": any("party_count" in c for j in sites for c in all_conditions(j)),
        }
    return by_char


def assign_ids(groups):
    """Location ids: LOCATION_BASE + (type << 21) + (map << 10) + index (per type and map, in
    offset order)."""
    for kind, records in groups:
        per_map = collections.defaultdict(list)
        for r in records:
            if r.get("world_script") or r.get("payment"):
                continue
            per_map[r["map"]].append(r)
        for m, rs in per_map.items():
            rs.sort(key=lambda r: (r.get("offset", r.get("registered_at", r.get("handler", 0))), r.get("flag", 0)))
            for idx, r in enumerate(rs):
                r["location_id"] = LOCATION_BASE + (LOCATION_TYPES[kind] << 21) + (m << 10) + idx
                r["location_type"] = kind


SITE_KEYS = ("map", "handler", "value_at", "form", "item", "fol", "name", "word", "expected", "role", "registered_at",
             "requires", "window", "region", "world", "area")


def merge_chests(records):
    """One chest location per opened flag. A flag on several maps (the versions of one place:
    Arlia 18 / 21, Rena's room 38 / 40, Hoffmann Ruins rooms ...) is one chest: whichever
    version is opened first sets the flag and the others stay shut. The lowest map's record
    stands for it and keeps its location id; 'sites' lists every version's handler push
    (each needs the placeholder patch), and the other versions' ids are retired. Returns the
    merged records and the retired ids."""
    by_flag = collections.defaultdict(list)
    for c in records:
        by_flag[c["flag"]].append(c)
    out, retired = [], []
    for c in records:
        group = by_flag.get(c["flag"])
        if group is None:
            continue
        del by_flag[c["flag"]]
        group.sort(key=lambda r: r["map"])
        head = dict(group[0])
        head["sites"] = []
        for r in group:
            for m in r.get("maps", [r["map"]]):
                s = {k: r[k] for k in SITE_KEYS if k in r}
                s["map"] = m
                head["sites"].append(s)
        if len(group) > 1:
            head["versions"] = [r["map"] for r in group]
            if len({(r.get("item"), r.get("fol")) for r in group}) > 1:
                head["contents_differ"] = True
            retired += [r["location_id"] for r in group[1:] if "location_id" in r]
        out.append(head)
    return out, retired


def gift_site_ids(gifts):
    """One location id per reward site of a gift event (an event may give several items, or
    an item and Fol). The event's first site keeps the event's id (the id every event had
    before); the other sites take the next free indices of their map, in offset order."""
    used = collections.Counter(g["map"] for g in gifts if "location_id" in g)
    added = 0
    for g in sorted((g for g in gifts if "location_id" in g), key=lambda g: (g["map"], g["offset"])):
        for k, s in enumerate(g["sites"]):
            if k == 0:
                s["location_id"] = g["location_id"]
                continue
            s["location_id"] = LOCATION_BASE + (LOCATION_TYPES["gift"] << 21) + (g["map"] << 10) + used[g["map"]]
            used[g["map"]] += 1
            added += 1
    return added


def routines_reached(mp, k, depth=0, seen=None):
    """Routine k and the map routines (not the library) it calls, two levels down, as rewards_in
    walks them: their byte offsets."""
    seen = set() if seen is None else seen
    if k in seen:
        return seen
    seen.add(k)
    s, e = mp.funcs[k]
    for n in range(s, e):
        if mp.ins[n][2].startswith("call ") and mp.ins[n][3] in mp.func_at:
            ck = mp.func_at[mp.ins[n][3]]
            if depth < 2 and mp.shapes[ck] not in LIB_SHAPES:
                routines_reached(mp, ck, depth + 1, seen)
    return seen


def tag_private_action_gifts(maps, pas, records):
    """Mark the gift / prize records a Private Action gives (its routine or one it calls):
    'private_action' lists the PA routines (byte offsets), 'pa_partners' their partners. A PA's
    reward is such a gift's own site, so the gift is the location and the PA its condition."""
    by_map = collections.defaultdict(list)
    for r in records:
        by_map[r["map"]].append(r)
    for p in pas:
        mp = maps.get(p["map"])
        rk = mp.func_at.get(p["routine"]) if mp is not None else None
        if rk is None or not by_map.get(p["map"]):
            continue
        reached = {mp.ins[mp.funcs[k][0]][0] for k in routines_reached(mp, rk)}
        for r in by_map[p["map"]]:
            if r["routine"] in reached:
                r.setdefault("private_action", []).append(p["routine"])
                r["pa_partners"] = sorted(set(r.get("pa_partners", [])) | set(p.get("partners", [])))


def build(paths, battles=True):
    disc, maps = load_maps(paths[0])
    names = read_names(disc)
    roles, lib = library(maps)
    LIB_SHAPES.update(lib)
    CHOICE_SHAPES.update(choice_shapes(maps, lib))
    COMMON_FLAGS.update(common_flags(maps, lib) | PA_FLAGS)
    events, dynamic = reward_sites(maps, roles, lib)
    item_name = lambda n: names.get(ITEM_NAME_KEY + n - 1, "")  # noqa: E731
    gifts, money, joins, prizes = [], [], [], []
    for ev in events:
        if ev["map"] == WORLD_SCRIPT:
            ev["world_script"] = True
        if ev["items"]:
            ev["item_names"] = [item_name(x) for x in ev["items"]]
        if ev["joins"]:
            ev["characters"] = [member(c) for c in ev["joins"]]
        # A routine handing out rewards in many branches is a prize table (contests,
        # exchanges): which branch runs depends on a result or a choice.
        ev["kind"] = "prize_table" if ev["blocks_in_routine"] >= 4 else "event"
        mark_window(ev)
        if ev["joins"]:
            joins.append(ev)
        elif ev["kind"] == "prize_table" and (ev["items"] or ev["flags_given"] or ev["fol"]):
            prizes.append(ev)
        elif ev["items"] or ev["flags_given"]:
            gifts.append(ev)
        elif ev["fol"]:
            if all(f < 0 for f in ev["fol"]):
                ev["payment"] = True   # a fee (add_money with a negative amount), not a reward
            money.append(ev)
    ch = chests(maps)
    for c in ch:
        if "item" in c:
            c["name"] = item_name(c["item"])
        mark_window(c)
    shops = so2_mod.shop_lists(disc)
    shop_table = {str(s): {"stock": items, "names": [item_name(x) for x in items]} for s, items in shops.items()
                  if items}
    sites = shop_sites(maps)
    for x in sites:
        mark_window(x)
    pa_triggers = private_actions(maps, roles)
    for p in pa_triggers:
        mark_window(p)
    pas = pa_events(maps, roles, flag_index(maps))
    for p in pas + pa_triggers:
        g = p.get("gives", {})
        if g.get("items"):
            g["item_names"] = [item_name(x) for x in g["items"]]
    pa_rewards = [p for p in pas if p.get("gives", {}).get("items") or p.get("gives", {}).get("fol")]
    trs = transfers(maps)
    region_list, region_of, oneway = regions(maps, trs)
    story_order = story(maps, trs, region_of, region_list)
    same_disc2 = None
    if len(paths) > 1:
        d2, maps2 = load_maps(paths[1])
        same = sorted(m for m in maps if m in maps2 and maps2[m].digest == maps[m].digest)
        same_disc2 = {"identical_maps": len(same), "maps_disc1": len(maps), "maps_disc2": len(maps2),
                      "different": sorted(m for m in maps if m in maps2 and maps2[m].digest != maps[m].digest),
                      "only_disc1": sorted(m for m in maps if m not in maps2),
                      "only_disc2": sorted(m for m in maps2 if m not in maps)}

    worlds = map_worlds(maps, trs, region_of, region_list)
    names_of = place_names(maps)
    area_of = areas(maps, trs, names_of, worlds)
    have_script = {m for mp in maps.values() for m in mp.maps}
    for (lo, hi), name in GUIDE_AREAS:
        for m in range(lo, hi + 1):
            if m in have_script and m not in names_of:
                area_of[m] = name

    def with_maps(records):
        for r in records:
            mp = maps.get(r.get("map"))
            if mp is not None and len(mp.maps) > 1:
                r["maps"] = mp.maps
            if r.get("map") in region_of:
                r["region"] = region_of[r["map"]]
            if r.get("map") in worlds:
                r["world"] = worlds[r["map"]]
            if r.get("map") in area_of:
                r["area"] = area_of[r["map"]]
        return records
    for lst in (ch, gifts, money, joins, prizes, sites, pas, pa_triggers):
        with_maps(lst)
    assign_ids([("chest", ch), ("gift", gifts), ("fol_gift", money), ("recruitment", joins), ("prize", prizes),
                ("shop", sites), ("private_action", pa_rewards)])
    ch, retired = merge_chests(ch)
    gift_site_ids(gifts)
    tag_private_action_gifts(maps, pas, gifts + prizes + money)
    late = set(late_maps(trs))
    for c in ch:
        c["late"] = any(x["map"] in late for x in c["sites"])
    for lst in (gifts, money, joins, prizes, sites, pas):
        for r in lst:
            r["late"] = any(m in late for m in r.get("maps", [r["map"]]))
    out = {
        "source": "tools/ap_extract.py",
        "shared_scripts": [mp.maps for mp in maps.values() if len(mp.maps) > 1],
        "field_maps": sorted(m for mp in maps.values() for m in mp.maps),
        "world_maps": {str(k): v for k, v in WORLD_MAPS.items()},
        "common_flags": sorted(COMMON_FLAGS),
        "library_routines": {h: r[0] for h, r in sorted(roles.items(), key=lambda kv: kv[1][0])},
        # Where each map's copy of the reward routines starts (byte offsets): a check hook
        # inside give_item / add_money / party_add knows it runs in one of these when the
        # current routine is one, and then takes the location from the caller's frame.
        "reward_routines": {str(mp.id): {roles[mp.shapes[k]][0]: mp.ins[s_][0]
                                         for k, (s_, e_) in enumerate(mp.funcs) if mp.shapes[k] in roles}
                            for mp in maps.values()},
        "location_id": {"base": LOCATION_BASE, "types": LOCATION_TYPES,
                        "formula": "base + (type << 21) + (map << 10) + index"},
        "chests": ch,
        # Ids of the chest records merged into another map's version of the same chest
        # (merge_chests): no longer locations.
        "retired_location_ids": sorted(retired),
        "gifts": gifts,
        "fol_gifts": money,
        "prize_tables": prizes,
        "recruitments": joins,
        "characters": classify_joins(joins),
        "dynamic_reward_calls": dynamic,
        "shops": shop_table,
        "shop_sites": sites,
        "private_actions": pas,
        "private_action_triggers": pa_triggers,
        "story": story_order,
        "story_points": story_points(maps),
        "regions": region_list,
        "map_worlds": {str(m): w for m, w in sorted(worlds.items())},
        # Maps still open at chapter 60 after the Final Save Point (Virtual Expel and Energy
        # Nede; late_maps): a location on one of them ('late') is never lost to the story.
        "late_maps": sorted(late),
        "map_names": {str(m): v for m, v in sorted(names_of.items())},
        "map_areas": {str(m): v for m, v in sorted(area_of.items())},
        "one_way_transfers": oneway,
        "transfers": trs,
        "item_names": {str(n): item_name(n) for n in range(1, 1024) if item_name(n)},
        "disc2": same_disc2,
    }
    if battles:
        try:
            import ap_battles  # the battle agent's side
            out["battles"] = ap_battles.build(paths)
        except Exception as e:  # noqa: BLE001
            out["battles_error"] = str(e)
    return out


def summary(data):
    play = lambda xs: [x for x in xs if not x.get("world_script")]  # noqa: E731
    print(f"field maps: {len(data['field_maps'])}; shared scripts: {[(g[0], len(g)) for g in data['shared_scripts']]}")
    print(f"library routines: {sorted(data['library_routines'].values())}")
    miss = lambda xs: sum(1 for x in play(xs) if x.get("missable"))  # noqa: E731
    for key in ("chests", "gifts", "fol_gifts", "prize_tables", "recruitments", "shop_sites", "private_actions",
                "private_action_triggers"):
        xs = play(data[key])
        print(f"{key}: {len(xs)} ({miss(xs)} in a closing story window; "
              f"{len(data[key]) - len(xs)} in the world script)")
    ch = data["chests"]
    print(f"chest locations (one per opened flag): {len(ch)}; on several maps {sum(1 for c in ch if 'versions' in c)} "
          f"(contents differ: {[(c['flag'], c['versions']) for c in ch if c.get('contents_differ')]}); "
          f"retired ids {len(data.get('retired_location_ids', []))}")
    gs = [g for g in data["gifts"] if "location_id" in g]
    print(f"gift sites with an id: {sum(len(g['sites']) for g in gs)} in {len(gs)} events "
          f"({sum(1 for g in gs for s in g['sites'] if s['what'] == 'flag')} key-item flags); "
          f"given by a Private Action: {sum(1 for g in gs if g.get('private_action'))}")
    print(f"computed reward calls: {len(data['dynamic_reward_calls'])}; shops: {len(data['shops'])}")
    lm = set(data.get("late_maps", []))
    print(f"maps open at chapter 60 after the Final Save Point: {len(lm)}; chests on them "
          f"{sum(1 for c in ch if c.get('late'))} of {len(ch)}; gift events {sum(1 for g in gs if g.get('late'))} "
          f"of {len(gs)}")
    pas = data["private_actions"]
    print(f"Private Actions: {len(pas)} events in {len({p['map'] for p in pas})} maps; giving items / Fol "
          f"{sum(1 for p in pas if 'location_id' in p)}; setting flags tested in other maps "
          f"{sum(1 for p in pas if p.get('flags_tested_elsewhere'))}; joins "
          f"{sum(1 for p in pas if p.get('gives', {}).get('joins'))}")
    kinds = collections.Counter(j["join_kind"] for j in data["recruitments"])
    print(f"join kinds: {dict(kinds)}")
    for name, c in data["characters"].items():
        first = c["sites"][0] if c["sites"] else None
        print(f"  {name}: {len(c['sites'])} sites"
              + (f", first map {first['map']} ({first['kind']}, window {first['window']})" if first else ""))
    st = data["story"]
    print(f"chapters stored: {len(st['chapters'])} values, "
          f"{st['chapters'][0]['chapter']}..{st['chapters'][-1]['chapter']}; "
          f"disc switch: {[(d['map'], hex(d['offset']), d['chapter'], d['transfer_to']) for d in st['disc_switch']]}")
    big = [r for r in data["regions"] if len(r["maps"]) > 1 or r["worlds"]]
    print(f"regions: {len(data['regions'])} ({len(big)} with doors or a world map); "
          f"one-way transfers: {len(data['one_way_transfers'])}; transfers: {len(data['transfers'])}")
    if data.get("disc2"):
        d = data["disc2"]
        print(f"disc 2: {d['identical_maps']} of {d['maps_disc1']} scripts identical; different {d['different'][:20]}; "
              f"only disc 1 {d['only_disc1'][:20]}; only disc 2 {d['only_disc2'][:20]}")
    if "battles" in data:
        b = data["battles"]
        print(f"battles: {', '.join(f'{k} {len(v)}' for k, v in b.items() if isinstance(v, (list, dict)))}")
    elif "battles_error" in data:
        print(f"battles: error {data['battles_error']}")


def dump(data):
    """JSON with one record per line in the record lists (compact and diff-friendly)."""
    parts = []
    for k, v in data.items():
        if isinstance(v, list) and v and isinstance(v[0], dict):
            body = ",\n  ".join(json.dumps(x, separators=(",", ":")) for x in v)
            parts.append(f" {json.dumps(k)}: [\n  {body}\n ]")
        else:
            parts.append(f" {json.dumps(k)}: {json.dumps(v, separators=(',', ':'))}")
    return "{\n" + ",\n".join(parts) + "\n}\n"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("discs", nargs="+", help="disc image(s): DISC1 [DISC2] (.cue / .bin / .iso)")
    ap.add_argument("-o", "--output", help="write the JSON here")
    ap.add_argument("--summary", action="store_true", help="print a summary instead of JSON")
    ap.add_argument("--no-battles", action="store_true", help="leave out tools/ap_battles.py's tables")
    ap.add_argument("--full", action="store_true",
                    help="also write every transfer and story-global store (analysis; large)")
    a = ap.parse_args()
    data = build(a.discs, battles=not a.no_battles)
    if a.summary:
        summary(data)
        if not a.output:
            return
    if not a.full:
        for key in ("transfers", "story_points"):
            data.pop(key, None)
        # Private Actions that only talk are left out: those kept give something, store a
        # story value or set a flag another map tests.
        data["private_actions"] = [p for p in data["private_actions"]
                                   if {"items", "fol", "joins", "story"} & set(p.get("gives", {}))
                                   or p.get("flags_tested_elsewhere")]
    text = dump(data)
    if a.output:
        open(a.output, "w").write(text)
    else:
        print(text)


if __name__ == "__main__":
    main()
