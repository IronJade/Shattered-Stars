#!/usr/bin/env python3
"""region_map.py: address maps of another release's executables and overlays against the
North American ones, for the port's region profiles (engine/src/game/common/region.*).

The port reads the original's constant tables (strings, layouts, coefficients, AI tables) from
the program images at their North American addresses. Another release holds the same tables
at other addresses: Europe, France and Germany relinked NA's code with a small PAL patch set,
so every whole program loads 0x1b0 bytes higher and each image shifts in steps wherever a
function or a table changed size. This tool derives, from the two discs, where every NA byte
of every image the port reads lives in the other release, and writes the result as facts
only (offsets, sizes, deltas, load addresses, hashes; never game bytes) to
engine/data/regions/<region>.inc, which engine/src/game/common/region.cpp compiles in.

  tools/region_map.py --na NA_DISC1.cue --disc REGION_DISC1.cue --region eu
                      [--out engine/data/regions] [--reads tools/regions/na_reads.txt]
                      [--report FILE] [--cache DIR] [--check]

How a map is derived and justified, per image (an archive entry's unpacked block, or the
boot executable):

 1. Both images are split into 32-bit words. The division-trap sequences after div / divu
    (NA's compiler emitted them, the PAL builds mostly don't) are set aside on both sides.
 2. Words are normalised: jal / j targets, lui immediates and the immediates of the
    instructions that use a lui-loaded or gp-based register are masked, and any word that
    holds an address in 0x80010000-0x801fffff (a pointer into a program) is masked. Relinked
    code and relocated pointer tables then compare equal.
 3. The two streams are aligned with GNU diff (--minimal); an insertion or deletion inside a
    run of repeated words (zero fill) is slid to the run's end, where pointers into the run
    say it belongs. Each run of equal words at a constant offset becomes a segment ("equal":
    code twins, equal instructions after normalisation, and data equal after relocation). A
    changed run with the same number of words on both sides, between two equal runs, is
    mapped word for word ("same-size change": a patched instruction or a changed table
    value; the region's value is the one the port then reads). Words only one side has are
    unmapped. A tail of 1-3 bytes after the last word follows the last word when both
    images end alike.
 3b. Near matches: diff maximises equal words, so inside a changed table it can pair a value
    with an equal value a few entries on (France's name-keyboard column map 0,0,1,1,2,2,...
    -> 0,0,0,1,1,1,...: a 4-word run 12 bytes off). A changed stretch of at most
    REALIGN_MAX_WORDS between two trusted runs (>= NEAR_WORDS words) at the same delta keeps
    its size, so it is mapped word for word, the short runs the diff found inside it dropped.
 3c. Tables the port indexes by position (TABLES: the ASCII tables, whose French / German
    entries NA never reads) are mapped word for word from their start over their whole
    extent; fatal if the start is a near match or the table changed size.
 4. The region's load address is the one its aligned, relinked jal targets into itself (or,
    in an image without any, its aligned relinked self-pointers) agree on; an image with
    neither loads at NA's address. (Words equal on both sides don't vote: a data table can
    hold values that read as jal.) Every vote must agree (fail otherwise).
 5. Pointer words: an NA word that addresses an image (itself or one it links against: the
    boot executable, the host program, ...) is a pointer when the region word at the mapped
    place addresses the mapped place of the same byte. Those words are listed; the port
    rewrites them to their NA values when it loads the region's image, so a pointer it reads
    and follows stays in NA space. Words that look like NA pointers but whose region value
    is not the mapped place are reported (and are not relocated).
 6. Checks, each fatal (exit 1) unless the report explains it:
    - every NA address the port reads (--reads: "image na_address" lines, recorded by the
      port with SS_REGION_READ_LOG over the suites and sweeps) lies in a segment of its
      image;
    - no read lies in a near match left after 3b (a short run at a delta of its own, inside
      a stretch that changed size): the place it maps to is a guess;
    - a read logged within CROSS_WINDOW bytes before a layout change, with no later read
      before the change, may start a block the port reads past it: its extent must be in
      READ_EXTENTS (checked by hand against the port's reader) and must end before the
      change ("suspicious" otherwise);
    - every pointer the port follows (a read address that is the value of a listed pointer
      word) maps;
    - the region image's FNV-1a 64 is recorded, so the port refuses a different revision of
      the disc instead of reading wrong addresses.

Enemy-code blocks (the 40 modules of the encounter packs) and spell-code modules are matched
by a hash of their normalised words (the same normalisation the engine computes at load,
region.cpp module_hash), so the port finds each region's module whatever its link addresses.

Map scripts: the port's Archipelago doors and join sites and the mods' script lines name
places in a map's event script by NA byte offsets. A release that laid a map's script out
otherwise (other code length: Europe 7 maps, France 24, Germany 32) gets a script map: the
two instruction streams (branch targets masked) are aligned with difflib, and each run of
equal instructions, or of changed ones of the same sizes, is a segment {NA offset, size,
delta}. A map whose code has the same length and the same instruction boundaries keeps NA's
offsets and gets none. Checked: every Archipelago door's transfer (docs/ap/doors.json) in
such a map must map to the same instruction (fatal otherwise; reported if it doesn't exist).
"""

import argparse
import bisect
import collections
import difflib
import hashlib
import json
import os
import pickle
import re
import struct
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from so2_archive import SLZ_MAGIC, slz_blocks, unpack_block  # noqa: E402
from so2_data import Disc  # noqa: E402
from so2_script import disassemble, parse_script  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# ---- The releases --------------------------------------------------------------------------

# Serial (SYSTEM.CNF boot file) -> (region key, disc number). The port's table is
# engine/src/game/common/region.cpp kProfiles.
SERIALS = {
    "SCUS_944.21": ("na", 1), "SCUS_944.22": ("na", 2),
    "SCES_021.59": ("eu", 1), "SCES_121.59": ("eu", 2),
    "SCES_021.60": ("fr", 1), "SCES_121.60": ("fr", 2),
    "SCES_021.61": ("de", 1), "SCES_121.61": ("de", 2),
    "SLPM_861.05": ("jp", 1), "SLPM_861.06": ("jp", 2),
}

PROGRAM_BASE = 0x8002F810

# Images the port reads at NA addresses: key -> (entry, block, NA load address, links).
# 'links' are the images a pointer in it may address (checked in order); "self" first.
IMAGES = collections.OrderedDict()
IMAGES["boot"] = ("exe", 0, 0x80010000, [])
for key, entry in (("title", 1), ("field", 3100), ("battle", 2129), ("credits", 3107)):
    IMAGES[key] = (entry, 0, PROGRAM_BASE, ["boot"])
IMAGES["c1d"] = (0xC1D, 0, 0x8007E000, ["field", "boot"])
IMAGES["c1e"] = (0xC1E, 0, 0x8007E000, ["field", "boot"])
IMAGES["c1f"] = (0xC1F, 0, 0x801E9000, ["field", "boot"])
for e in (0xC20, 0xC21, 0xC22, 0xC24, 0xC25):
    IMAGES[f"{e:x}"] = (e, 0, 0x801F9800, ["field", "boot"])
IMAGES["ba5"] = (0xBA5, 0, 0x800D1B28, ["field", "title", "boot"])
IMAGES["bb9"] = (0xBB9, 0, 0x80088000, ["field", "ba5", "boot"])
for e in (0xBA6, 0xBAA, 0xBAE, 0xBB2, 0xBB6, 0xBBC, 0xBBE, 0xBC0, 0xBC2, 0xBC4, 0xBC6, 0xBC8, 0xBCA, 0xBCC, 0xBCE):
    IMAGES[f"{e:x}"] = (e, 0, 0x8007E000, ["ba5", "field", "title", "boot"])
IMAGES["7bd"] = (1981, 0, 0x80085518, ["battle", "boot"])
IMAGES["7c0.0"] = (1984, 0, 0x800715D0, ["battle", "boot"])
IMAGES["7c0.1"] = (1984, 1, 0x80081728, ["battle", "7c0.0", "boot"])
for b in range(2, 11):
    IMAGES[f"7c0.{b}"] = (1984, b, 0x8008C1F4, ["battle", "7c0.0", "7c0.1", "boot"])
PC_ENTRIES = (2131, 2135, 2137, 2139, 2141, 2143, 2147, 2151, 2153, 2155, 2157, 2159)
for e in PC_ENTRIES:
    IMAGES[f"{e:x}"] = (e, 0, 0x801EE000, ["battle", "7c0.0", "7c0.1", "boot"])

# Parts of an image that other code loads over at run time: a jal into them calls that code,
# not the image's own copy, and the stale copy's own jals are not the image's, so neither
# votes on the image's load address (step 4).
OVERLAID = {"battle": (0x800715D0, "#1984, #1981, the enemy slots and the spell modules load there")}

ENEMY_SLOTS = {0: 0x80088110, 1: 0x80089F18, 2: 0x8008B1F0, 3: 0x80088900}
SPELL_BASE = 0x8008CC00
MODULE_LINKS = ["battle", "7c0.0", "7c0.1", "7bd", "boot"]

POINTER_LO, POINTER_HI = 0x80010000, 0x80200000

# Step 3b / 6: a run of equal words shorter than this, at a delta other than its neighbours',
# is a near match (values that happen to recur), not a place the release moved data to.
NEAR_WORDS = 16
# A longer changed stretch is not realigned: so little of it matches that its place says
# nothing (the battle program's stale overlay area, for one); its near matches stay, and a
# read in one is fatal.
REALIGN_MAX_WORDS = 64

# Step 6: a read the port starts within this many bytes before a layout change (the next
# NA words map at another delta, or not at all), with no later read logged before the change,
# may be the start of a block the port reads past the change. Each such read must have its
# extent here (checked against the port's reader by hand): (image, NA address) -> (bytes the
# port reads from there, the reader). A read whose extent crosses the change is fatal; one
# without an extent is fatal too ("suspicious"), until someone checks its reader.
CROSS_WINDOW = 1024
READ_EXTENTS = {
    ("title", 0x8004D37C): (12, "CombatModeWindow::draw: icon x, 3 s32"),
    ("title", 0x8004D388): (12, "CombatModeWindow::draw: icon y, 3 s32"),
    ("title", 0x8004E274): (8, "title_screen menu_rect: one 8-byte rect per read"),
    ("title", 0x80063198): (40, "roll_talents: 10 entries of u16 bit + u16 chance"),
    ("battle", 0x8008519C): (2, "party.cpp battle_s16 (0x80085198 + idx * 2)"),
}

# Step 3c: tables the port indexes by position, including entries NA never reads (a French or
# German character's entry in an ASCII table): image -> [(NA address, NA's extent in bytes,
# reader)]. The extent is mapped word for word from the table's start, which must lie in a
# trusted run (fatal otherwise); words around it whose region place falls inside the table
# lose their mapping, and if NEAR_WORDS or more do, the table shrank (fatal). A release's
# table may be longer than NA's: the port reads these tables from their translated start in
# the release's own layout (a pointer to the start, indexed), never entry by entry through
# the map past NA's extent. A table may end at the image's end (the rest is bss).
TABLES = {
    "title": [(0x8004EAF8, 0x28, "name keyboard column map (title window.cpp, new_game.cpp)"),
              (0x8004EAFC, 0x100, "title text_from_ascii: the ASCII table, by character")],
    # NA's table ends at 0xe0 (variables follow, at +0x100 in France / Germany, whose table is
    # 0x100 long: the port reads it from its start in the release's layout, window.cpp).
    "battle": [(0x8006E7C0, 0xE0, "battle text_from_ascii: the ASCII table, by character")],
    "bce": [(0x80082B70, 0x28, "rename page column map (menu_rename.cpp)"),
            (0x80082B78, 0x100, "rename page text_from_ascii: the ASCII table, by character")],
}

# ---- Reading the images --------------------------------------------------------------------


def iso_file(disc, name):
    pvd = disc.read(16, 1)
    if pvd[1:6] != b"CD001":
        raise SystemExit("no ISO 9660 volume")
    lba, size = struct.unpack_from("<I", pvd, 158)[0], struct.unpack_from("<I", pvd, 166)[0]
    d = disc.read(lba, (size + 2047) // 2048)
    pos = 0
    while pos < size:
        ln = d[pos]
        if ln == 0:
            pos = (pos // 2048 + 1) * 2048
            continue
        nm = d[pos + 33:pos + 33 + d[pos + 32]].decode("latin-1").split(";")[0]
        if nm.upper() == name.upper():
            flba, fsize = struct.unpack_from("<I", d, pos + 2)[0], struct.unpack_from("<I", d, pos + 10)[0]
            return disc.read(flba, (fsize + 2047) // 2048)[:fsize]
        pos += ln
    return None


def boot_name(disc):
    cnf = iso_file(disc, "SYSTEM.CNF")
    if cnf is None:
        return ""
    s = cnf.decode("latin-1")
    b = s.find("BOOT")
    st = s.find(":\\", b) + 2
    e = min(i for i in (s.find(";", st), s.find("\r", st), s.find("\n", st), len(s)) if i != -1)
    return s[st:e]


def block_of(data, n):
    for k, (off, *_rest) in enumerate(slz_blocks(data)):
        if k == n:
            return bytes(unpack_block(data, off))
    return None


def pack_codes(d):
    """An encounter pack's code list: [(slot kind, offset)] (engine battle/encounter.cpp)."""
    s32 = lambda o: struct.unpack_from("<i", d, o)[0]
    p = 0
    while s32(p) != -1:
        p += 8
    p += 4
    while s32(p) != -1:
        p += 8
    p += 4
    out = []
    while p + 4 <= len(d) and s32(p) != -1:
        out.append((s32(p), s32(p + 4)))
        p += 8
    return out


def spell_module_block(d):
    """A spell data entry's module (block 0 of the op-6 container), engine spellcast.cpp."""
    if len(d) < 8:
        return None
    at = 0
    op = struct.unpack_from("<I", d, 0)[0]
    if op == 5:
        at = 0xC
        op = struct.unpack_from("<I", d, at)[0] if at + 4 <= len(d) else 0
    if op != 6:
        return None
    off = struct.unpack_from("<I", d, at + 4)[0]
    if off + 16 > len(d) or d[off:off + 3] != SLZ_MAGIC:
        return None
    return bytes(unpack_block(d, off))


class Release:
    """One disc 1's images: {key: (bytes, NA-side base or None)} plus the modules."""

    def __init__(self, path, cache=None):
        self.path = path
        self.disc = Disc(path)
        self.serial = boot_name(self.disc)
        self.region, self.disc_no = SERIALS.get(self.serial, ("?", 0))
        cache_file = None
        if cache:
            os.makedirs(cache, exist_ok=True)
            st = os.stat(self.disc.path)
            cache_file = os.path.join(cache, f"{self.serial}_{st.st_size}_{int(st.st_mtime)}.pickle")
            if os.path.exists(cache_file):
                self.images, self.enemy, self.spell = pickle.load(open(cache_file, "rb"))
                return
        self.images = {}
        for key, (entry, blk, _base, _links) in IMAGES.items():
            if entry == "exe":
                exe = iso_file(self.disc, self.serial)
                t_addr, t_size = struct.unpack_from("<2I", exe, 0x18)
                self.images[key] = (exe[0x800:0x800 + t_size], t_addr)
                continue
            data = self.disc.entry(entry)
            self.images[key] = (block_of(data, blk), None)
        # Enemy-code blocks: (encounter, code index) -> (slot kind, bytes); spell modules:
        # entry -> bytes.
        self.enemy = {}
        for enc in range(564):
            try:
                d = self.disc.entry(1405 + enc)
            except SystemExit:
                continue
            for k, (kind, off) in enumerate(pack_codes(d)):
                if 0 <= off < len(d) and d[off:off + 3] == SLZ_MAGIC:
                    self.enemy[(enc, k)] = (kind, bytes(unpack_block(d, off)))
        self.spell = {}
        for e in range(0x8E1, 0x983):
            try:
                d = self.disc.entry(e)
            except SystemExit:
                continue
            b = spell_module_block(d)
            if b is not None:
                self.spell[e] = b
        if cache_file:
            pickle.dump((self.images, self.enemy, self.spell), open(cache_file, "wb"))


# ---- Normalisation and alignment -----------------------------------------------------------

LOADST = set(range(0x20, 0x27)) | set(range(0x28, 0x2F)) | {0x32, 0x3A}


def normalize(ws):
    """Mask what relinking changes (see the module docstring, step 2). Must match
    region.cpp normalize_words() for the module hashes."""
    lui = set()
    out = []
    for w in ws:
        if POINTER_LO <= w < POINTER_HI:
            out.append(0x80000000)
            continue
        op = w >> 26
        rs = (w >> 21) & 31
        rt = (w >> 16) & 31
        rd = (w >> 11) & 31
        if op in (2, 3):
            out.append(w & 0xFC000000)
            continue
        if op == 0xF:
            out.append(w & 0xFFFF0000)
            lui.add(rt)
            continue
        if op in (0x09, 0x0D) and (rs in lui or rs == 28):
            out.append(w & 0xFFFF0000)
            lui.add(rt)
            continue
        if op in LOADST and (rs in lui or rs == 28):
            out.append(w & 0xFFFF0000)
            if op < 0x28:
                lui.discard(rt)
            continue
        if op == 0 and (w & 0x3F) in (0x21, 0x20) and (rs in lui or rt in lui):
            out.append(w)
            lui.add(rd)
            continue
        out.append(w)
        if op == 0:
            lui.discard(rd)
        elif op in LOADST and op < 0x28:
            lui.discard(rt)
        elif 0x08 <= op <= 0x0F:
            lui.discard(rt)
        elif op == 3:
            lui.discard(31)
    return out


def trap_kept(ws):
    """Indices of the words kept after setting aside the division traps (bne rt,zero,+2; nop;
    break 7 [; li at,-1; bne rt,at,+4; lui at,0x8000; bne rs,at,+2; nop; break 6])."""
    keep, i, n = [], 0, len(ws)
    while i < n:
        w = ws[i]
        if (w & 0xFC1FFFFF) == 0x14000002 and i + 2 < n and ws[i + 2] == 0x0007000D:
            if i + 8 < n and ws[i + 8] == 0x0006000D and ws[i + 3] == 0x2401FFFF:
                i += 9
                continue
            i += 3
            continue
        keep.append(i)
        i += 1
    return keep


def words(b):
    n = len(b) // 4
    return list(struct.unpack_from(f"<{n}I", b, 0))


def diff_align(a, b, tmp):
    """GNU diff of two word lists: [(a_lo, a_hi, b_lo, b_hi)] hunks, half-open."""
    fa, fb = os.path.join(tmp, "a.txt"), os.path.join(tmp, "b.txt")
    with open(fa, "w") as f:
        f.write("".join(f"{w:08x}\n" for w in a))
    with open(fb, "w") as f:
        f.write("".join(f"{w:08x}\n" for w in b))
    out = subprocess.run(["diff", "--minimal", fa, fb], capture_output=True, text=True).stdout
    hunks = []
    for line in out.splitlines():
        m = re.match(r"^(\d+)(?:,(\d+))?([acd])(\d+)(?:,(\d+))?$", line)
        if not m:
            continue
        a1, a2, op, b1, b2 = m.groups()
        a1 = int(a1)
        a2 = int(a2) if a2 else a1
        b1 = int(b1)
        b2 = int(b2) if b2 else b1
        if op == "a":
            hunks.append((a1, a1, b1 - 1, b2))
        elif op == "d":
            hunks.append((a1 - 1, a2, b1, b1))
        else:
            hunks.append((a1 - 1, a2, b1 - 1, b2))
    return hunks


def slide_down(hunks, a, b):
    """Move each pure insertion / deletion as late as the equal words after it allow. Inside a
    run of repeated words (zero fill, a table of equal entries) diff may put the extra words
    anywhere; the start of a run usually keeps its place (pointers into it say so), so the
    extra words go to its end."""
    out = []
    for n, (a_lo, a_hi, b_lo, b_hi) in enumerate(hunks):
        nxt_a = hunks[n + 1][0] if n + 1 < len(hunks) else len(a)
        if b_lo == b_hi and a_lo < a_hi:  # deletion: NA words only
            while a_hi < nxt_a and a[a_lo] == a[a_hi]:
                a_lo, a_hi, b_lo, b_hi = a_lo + 1, a_hi + 1, b_lo + 1, b_hi + 1
        elif a_lo == a_hi and b_lo < b_hi:  # insertion: region words only
            nxt_b = hunks[n + 1][2] if n + 1 < len(hunks) else len(b)
            while b_hi < nxt_b and b[b_lo] == b[b_hi]:
                a_lo, a_hi, b_lo, b_hi = a_lo + 1, a_hi + 1, b_lo + 1, b_hi + 1
        out.append((a_lo, a_hi, b_lo, b_hi))
    return out


class ImageMap:
    """NA word index -> region word index, as segments."""

    def __init__(self, key, na, rg, na_base, tmp):
        self.key = key
        self.na, self.rg = na, rg
        self.na_base = na_base
        self.rg_base = None
        self.identical = na == rg
        nw, rw = words(na), words(rg)
        self.nw, self.rw = nw, rw
        m = {}     # NA word index -> region word index
        kind = {}  # NA word index -> 'eq' | 'chg'
        if self.identical:
            for i in range(len(nw)):
                m[i] = i
                kind[i] = "eq"
        else:
            kn, kr = trap_kept(nw), trap_kept(rw)
            a = normalize([nw[i] for i in kn])
            b = normalize([rw[i] for i in kr])
            hunks = slide_down(diff_align(a, b, tmp), a, b)
            i = j = 0
            for a_lo, a_hi, b_lo, b_hi in hunks + [(len(a), len(a), len(b), len(b))]:
                while i < a_lo:
                    m[kn[i]] = kr[j]
                    kind[kn[i]] = "eq"
                    i += 1
                    j += 1
                if a_hi - a_lo == b_hi - b_lo and a_lo < len(a):
                    for k in range(a_hi - a_lo):
                        m[kn[a_lo + k]] = kr[b_lo + k]
                        kind[kn[a_lo + k]] = "chg"
                i, j = a_hi, b_hi
        self.m, self.kind = m, kind
        self.realigned = []  # [(NA word lo, hi, {NA word index: diff's region index or None})]
        self.tables, self.table_fatal = [], []
        if not self.identical:
            self.realign(set(trap_kept(nw)))
            for base, size, why in TABLES.get(key, ()):
                self.force_table(base, size, why)
        self.segs = self.segments()
        self.na_size, self.rg_size = len(na), len(rg)
        # The tail bytes of an image whose size isn't a multiple of 4: mapped with the last
        # word when both images end alike (same tail length, last words aligned to each other).
        nt, rt = len(na) - 4 * len(nw), len(rg) - 4 * len(rw)
        self.tail = nt if nt and nt == rt and m.get(len(nw) - 1) == len(rw) - 1 else 0
        if self.tail and self.segs and self.segs[-1][1] == len(nw):
            self.segs[-1].append(self.tail)  # extra bytes after the segment's last word

    def segments(self):
        """Runs of NA words mapped at a constant word delta: [lo, hi, delta, Counter(kinds)]."""
        segs = []
        for i in sorted(self.m):
            d = self.m[i] - i
            k = self.kind[i]
            if segs and segs[-1][1] == i and segs[-1][2] == d:
                segs[-1][1] = i + 1
                segs[-1][3][k] += 1
            else:
                segs.append([i, i + 1, d, collections.Counter({k: 1})])
        return segs

    def realign(self, kept):
        """Step 3b: a changed stretch of the same length on both sides, between two runs at the
        same delta, is mapped word for word even when the diff matched a few of its words
        elsewhere (near matches: short runs at another delta inside it, such as a changed table
        whose new values happen to equal its old ones a few entries on). A table keeps its
        indexing when its values change, so the place, not the value, says which word is which.
        Only runs shorter than NEAR_WORDS count as near matches; a longer one is a real move and
        stays. 'kept' are the NA word indices outside the division traps."""
        segs = self.segments()
        n = 0
        while n < len(segs):
            d = segs[n][2]
            if segs[n][1] - segs[n][0] < NEAR_WORDS:  # brackets are runs long enough to trust
                n += 1
                continue
            k = n + 1
            while k < len(segs) and segs[k][2] != d and segs[k][1] - segs[k][0] < NEAR_WORDS:
                k += 1
            if (k < len(segs) and k > n + 1 and segs[k][2] == d and segs[k][1] - segs[k][0] >= NEAR_WORDS
                    and segs[k][0] - segs[n][1] <= REALIGN_MAX_WORDS):
                lo, hi = segs[n][1], segs[k][0]
                before = {}
                for i in range(lo, hi):
                    if i not in kept:
                        continue
                    before[i] = self.m.get(i)
                    self.m[i] = i + d
                    self.kind[i] = "eq" if normalize([self.nw[i]]) == normalize([self.rw[i + d]]) else "chg"
                self.realigned.append((lo, hi, before))
                n = k
            else:
                n += 1

    def force_table(self, base, size, why):
        """Step 3c: map a table the port indexes by position word for word from its start."""
        lo = (base - self.na_base) >> 2
        hi = min(len(self.nw), (base + size - self.na_base + 3) >> 2)
        name = f"{self.key} table {base:#010x}+{size:#x} ({why})"
        if lo not in self.m:
            self.table_fatal.append(f"{name}: its start has no counterpart")
            return
        d = self.m[lo] - lo
        run = [s for s in self.segments() if s[0] <= lo < s[1]][0]
        realigned = any(a <= lo < b for a, b, _ in self.realigned)
        if run[1] - run[0] < NEAR_WORDS and not realigned:
            self.table_fatal.append(f"{name}: its start is in a near match ({4 * (run[1] - run[0])} bytes at "
                                    f"delta {4 * d}), so its place in the release is unknown")
            return
        if hi + d > len(self.rw):
            self.table_fatal.append(f"{name}: runs past the end of the release's image")
            return
        changed = 0
        for i in range(lo, hi):
            if self.m.get(i) != i + d:
                changed += 1
                self.m[i] = i + d
                self.kind[i] = "tbl"
        dropped = [i for i, r in self.m.items() if (i < lo and r >= lo + d) or (i >= hi and r < hi + d)]
        for i in dropped:
            del self.m[i]
            del self.kind[i]
        if len(dropped) >= NEAR_WORDS:
            self.table_fatal.append(f"{name}: {len(dropped)} words around it map into its extent in the release: "
                                    f"the table changed size")
        self.tables.append((base, size, changed, len(dropped)))

    def map_off(self, off):
        """NA byte offset -> region byte offset, or None."""
        if self.tail and 4 * len(self.nw) <= off < self.na_size:
            return 4 * len(self.rw) + (off - 4 * len(self.nw))
        r = self.m.get(off >> 2)
        if r is None:
            return None
        ro = (r << 2) | (off & 3)
        return ro if ro < self.rg_size or off >= self.na_size else ro

    def map_addr(self, addr):
        off = addr - self.na_base
        if off < 0 or off > self.na_size:
            return None
        if off == self.na_size:  # one past the end
            return self.rg_base + self.rg_size if self.rg_base is not None else None
        r = self.map_off(off)
        return None if r is None or self.rg_base is None else self.rg_base + r

    def contains_na(self, addr):
        return self.na_base <= addr <= self.na_base + self.na_size


def find_region_base(im, links):
    """Step 4: the load address the region's aligned self-references agree on."""
    votes = collections.Counter()
    overlaid = OVERLAID.get(im.key, (1 << 32, ""))[0]
    for i, r in im.m.items():
        w, wr = im.nw[i], im.rw[r]
        if (w >> 26) == 3 and (wr >> 26) == 3 and w != wr:  # a relinked jal into itself
            t = ((w & 0x3FFFFFF) << 2) | (im.na_base & 0xF0000000)
            tr = ((wr & 0x3FFFFFF) << 2) | (im.na_base & 0xF0000000)
            off = t - im.na_base
            if 0 <= off < im.na_size and t < overlaid and im.na_base + 4 * i < overlaid:
                ro = im.map_off(off)
                if ro is not None:
                    votes[tr - ro] += 1
    how = "jal"
    if not votes:
        how = "pointer"
        for i, r in im.m.items():
            w, wr = im.nw[i], im.rw[r]
            off = w - im.na_base
            if 0 <= off < im.na_size and w != wr:
                ro = im.map_off(off)
                if ro is not None:
                    votes[wr - ro] += 1
    if not votes:
        return im.na_base, "none (no relinked self-reference: the NA address)", 0, 0
    base, n = votes.most_common(1)[0]
    return base, how, n, sum(votes.values()) - n


# ---- The whole release ---------------------------------------------------------------------

def fnv1a64(b):
    h = 0xCBF29CE484222325
    for x in b:
        h ^= x
        h = (h * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def module_hash(b):
    """FNV-1a 64 of the normalised words (traps set aside), little-endian: the engine's
    region::module_hash. Equal for a module relinked at other addresses."""
    ws = words(b)
    kept = trap_kept(ws)
    n = normalize([ws[i] for i in kept])
    return fnv1a64(struct.pack(f"<{len(n)}I", *n)) ^ len(n)


def build(na, rg, tmp, log):
    maps = collections.OrderedDict()
    problems = []
    # Programs and overlays.
    for key, (entry, blk, base, links) in IMAGES.items():
        nb, nbase = na.images[key]
        rb, rbase = rg.images[key]
        if nb is None or rb is None:
            if nb is not None:
                problems.append(f"{key}: image missing in the region")
            continue
        im = ImageMap(key, nb, rb, nbase if nbase is not None else base, tmp)
        im.links = ["self"] + links
        if rbase is not None:
            im.rg_base, im.base_how, im.base_votes, im.base_against = rbase, "header", 0, 0
        else:
            im.rg_base, im.base_how, im.base_votes, im.base_against = find_region_base(im, links)
            if im.base_against:
                problems.append(f"{key}: load address {im.rg_base:#x} has {im.base_against} votes against")
        maps[key] = im
        log(f"{key}: {len(nb):#x} -> {len(rb):#x} bytes, base {im.na_base:#x} -> {im.rg_base:#x} ({im.base_how}"
            f" {im.base_votes}/{im.base_against}), {len(im.segs)} segments, unmapped NA words {len(im.nw) - len(im.m)}")
    # Enemy-code blocks: the NA module of each (by its NA bytes), its twin in the region by the
    # same pack and code index; must agree for every pack that carries the module.
    enemy = collections.OrderedDict()
    for (enc, k), (kind, nbytes) in sorted(na.enemy.items()):
        h = fnv1a64(nbytes)
        twin = rg.enemy.get((enc, k))
        if twin is None:
            problems.append(f"enemy pack {enc} code {k}: missing in the region")
            continue
        rkind, rbytes = twin
        if h in enemy:
            prev = enemy[h]
            if fnv1a64(rbytes) != prev["rg_fnv"]:
                prev.setdefault("variants", set()).add(fnv1a64(rbytes))
            continue
        enemy[h] = {"enc": enc, "k": k, "kind": kind, "rkind": rkind, "na": nbytes, "rg": rbytes,
                    "rg_fnv": fnv1a64(rbytes)}
    for h, e in enemy.items():
        base = ENEMY_SLOTS.get(e["kind"])
        if base is None or e["kind"] != e["rkind"]:
            problems.append(f"enemy module {h:016x}: slot {e['kind']} -> {e['rkind']}")
            continue
        im = ImageMap(f"enemy.{h:016x}", e["na"], e["rg"], base, tmp)
        im.links = ["self"] + MODULE_LINKS
        im.rg_base, im.base_how, im.base_votes, im.base_against = find_region_base(im, MODULE_LINKS)
        im.module = e
        if e.get("variants"):
            problems.append(f"enemy module {h:016x}: {len(e['variants'])} other region variants")
        maps[im.key] = im
    spell = collections.OrderedDict()
    for ent, nbytes in sorted(na.spell.items()):
        h = fnv1a64(nbytes)
        rbytes = rg.spell.get(ent)
        if rbytes is None:
            problems.append(f"spell entry {ent:#x}: no module in the region")
            continue
        if h in spell:
            if fnv1a64(rbytes) != spell[h]["rg_fnv"]:
                spell[h].setdefault("variants", set()).add(fnv1a64(rbytes))
            continue
        spell[h] = {"entry": ent, "na": nbytes, "rg": rbytes, "rg_fnv": fnv1a64(rbytes)}
    for h, s in spell.items():
        im = ImageMap(f"spell.{h:016x}", s["na"], s["rg"], SPELL_BASE, tmp)
        im.links = ["self"] + MODULE_LINKS
        im.rg_base, im.base_how, im.base_votes, im.base_against = find_region_base(im, MODULE_LINKS)
        im.module = s
        if s.get("variants"):
            problems.append(f"spell module {h:016x}: {len(s['variants'])} other region variants")
        maps[im.key] = im
    # Pointer words (step 5).
    for key, im in maps.items():
        find_pointers(im, maps)
        code_references(im)
    return maps, problems


REF_LO_OPS = {0x09, 0x20, 0x21, 0x23, 0x24, 0x25, 0x28, 0x29, 0x2B, 0x22, 0x26, 0x2A, 0x2E, 0x32, 0x3A}


def code_references(im):
    """Step 6b: the release's own code as a witness for the data map. Each NA instruction pair
    lui + load / store / addiu that forms an address inside the image, where both instructions
    are unchanged code (equal after normalisation) and the release relocated the constant, says
    where the release keeps that byte. im.refs: {NA address: (release address, votes)} where the
    map disagrees with every vote, im.refs_ok: how many referenced addresses it agrees with."""
    im.refs, im.refs_ok = {}, 0
    if im.identical or im.rg_base is None:
        return
    def sx(v):
        return v - 0x10000 if v & 0x8000 else v
    hi = {}
    votes = collections.defaultdict(collections.Counter)
    for i, w in enumerate(im.nw):
        op = w >> 26
        if op == 0xF:
            hi[(w >> 16) & 31] = (i, (w & 0xFFFF) << 16)
            continue
        if op not in REF_LO_OPS or ((w >> 21) & 31) not in hi:
            continue
        li, base = hi[(w >> 21) & 31]
        t = (base + sx(w & 0xFFFF)) & 0xFFFFFFFF
        if i - li >= 24 or not (im.na_base <= t < im.na_base + im.na_size):
            continue
        ri, rl = im.m.get(i), im.m.get(li)
        if ri is None or rl is None or im.kind.get(i) != "eq" or im.kind.get(li) != "eq":
            continue
        rv = (((im.rw[rl] & 0xFFFF) << 16) + sx(im.rw[ri] & 0xFFFF)) & 0xFFFFFFFF
        if rv == t and im.map_addr(t) != t:
            continue  # an absolute address the release kept (a buffer the image overlaps)
        votes[t][rv] += 1
    for t, c in votes.items():
        mv = im.map_addr(t)
        if c.get(mv):
            im.refs_ok += 1
        else:
            rv, n = c.most_common(1)[0]
            im.refs[t] = (rv, n)


def find_pointers(im, maps):
    """Step 5. im.ptrs: sorted [(NA word index, target key)]; im.ptr_bad: [(index, why)]."""
    ptrs, bad = [], []
    targets = []
    for t in im.links:
        tm = im if t == "self" else maps.get(t)
        if tm is not None and tm.rg_base is not None:
            targets.append((t, tm))
    for i, r in sorted(im.m.items()):
        w = im.nw[i]
        if not (POINTER_LO <= w < POINTER_HI):
            continue
        wr = im.rw[r]
        hit = None
        for t, tm in targets:
            if tm.contains_na(w):
                hit = (t, tm)
                break
        if hit is None:
            continue
        t, tm = hit
        want = tm.map_addr(w)
        if want is not None and want == wr:
            if w != wr:
                ptrs.append((i, t))
        elif w != wr:
            bad.append((i, t, w, wr, want))
    im.ptrs, im.ptr_bad = ptrs, bad


def relocated(im, maps):
    """The region image with its pointer words rewritten to NA values (region.cpp prepare)."""
    out = bytearray(im.rg)
    keys = list(maps)
    for i, t in im.ptrs:
        r = im.m[i]
        tm = im if t == "self" else maps[t]
        v = struct.unpack_from("<I", im.rg, 4 * r)[0]
        na = to_na(tm, v)
        if na is not None:
            struct.pack_into("<I", out, 4 * r, na)
    return bytes(out)


def to_na(im, v):
    """Region address -> NA address through im's segments (inverse map), or None."""
    if not hasattr(im, "_inv"):
        im._inv = {r: i for i, r in im.m.items()}
    ro = v - im.rg_base
    if ro == im.rg_size:
        return im.na_base + im.na_size
    i = im._inv.get(ro >> 2)
    return None if i is None else im.na_base + 4 * i + (ro & 3)


# ---- Output --------------------------------------------------------------------------------

# Segment evidence (step 3): words equal after normalisation / a same-size changed run.
EVIDENCE = {"eq": "equal", "chg": "same-size change", "tbl": "table by position"}


def runs(items):
    """[(index, target)] -> [(first index, count, target)] for consecutive words."""
    out = []
    for i, t in items:
        if out and out[-1][0] + out[-1][1] == i and out[-1][2] == t:
            out[-1][1] += 1
        else:
            out.append([i, 1, t])
    return out


def image_ids(maps):
    """Stable ids for the targets: program/overlay keys, then modules."""
    return {k: n for n, k in enumerate(maps)}


def emit(maps, rg, region, out_dir, na_path, rg_path, scripts=None):
    ids = image_ids(maps)
    lines = []
    lines.append(f"// Generated by tools/region_map.py --region {region} from the user's discs: do not edit.")
    lines.append("// Facts only: offsets, sizes, deltas, load addresses and hashes; no game bytes.")
    lines.append(f"// Region disc 1: {rg.serial}. NA disc 1: SCUS_944.21.")
    lines.append("// Segments: {NA byte offset, byte count, region offset - NA offset}; the comment counts the")
    lines.append("// words by evidence (equal = equal after normalisation: code twins and relocated data;")
    lines.append("// same-size change = a changed run of the same length between equal runs, mapped word for")
    lines.append("// word). Pointers: {NA byte offset, words, target image index}. tools/region_map.py explains.")
    lines.append("")
    for key, im in maps.items():
        cname = "k" + re.sub(r"[^0-9a-zA-Z]", "_", key)
        lines.append(f"// {key}: NA {im.na_base:#010x} + {im.na_size:#x}, region {im.rg_base:#010x} + {im.rg_size:#x}"
                     f" (load address by {im.base_how}{'' if im.base_how in ('header',) else f', {im.base_votes} votes'})"
                     + (", byte-identical" if im.identical else ""))
        lines.append(f"constexpr Segment {cname}_seg[] = {{")
        for seg in im.segs:
            lo, hi, d, kinds = seg[:4]
            extra = seg[4] if len(seg) > 4 else 0
            ev = ", ".join(f"{EVIDENCE[k]} {v}" for k, v in sorted(kinds.items()))
            lines.append(f"    {{{lo * 4:#x}, {(hi - lo) * 4 + extra:#x}, {d * 4}}},  // {ev}"
                         + (f" + {extra} tail bytes" if extra else ""))
        if not im.segs:
            lines.append("    {0, 0, 0},")
        lines.append("};")
        pr = runs(im.ptrs)
        lines.append(f"constexpr Pointers {cname}_ptr[] = {{")
        for i, n, t in pr:
            lines.append(f"    {{{i * 4:#x}, {n}, {ids[key] if t == 'self' else ids[t]}}},")
        if not pr:
            lines.append("    {0, 0, 0},")
        lines.append("};")
    lines.append("")
    lines.append("constexpr ImageMap kImages[] = {")
    for key, im in maps.items():
        cname = "k" + re.sub(r"[^0-9a-zA-Z]", "_", key)
        if key.startswith(("enemy.", "spell.")):
            kind = "ImageKind::kEnemy" if key.startswith("enemy.") else "ImageKind::kSpell"
            entry, blk = -1, 0
            na_fnv = int(key.split(".")[1], 16)
        else:
            kind = "ImageKind::kBoot" if key == "boot" else "ImageKind::kEntry"
            entry, blk, _b, _l = IMAGES[key]
            entry = -1 if entry == "exe" else entry
            na_fnv = fnv1a64(im.na)
        nseg = len(im.segs)
        nptr = len(runs(im.ptrs))
        lines.append(f"    {{{kind}, {entry}, {blk}, {im.na_base:#010x}u, {im.rg_base:#010x}u, {im.na_size:#x}u, {im.rg_size:#x}u,"
                     f" {na_fnv:#018x}ull, {fnv1a64(im.rg):#018x}ull, {module_hash(im.rg):#018x}ull,"
                     f" {cname}_seg, {nseg}, {cname}_ptr, {nptr}}},  // {key}")
    lines.append("};")
    emit_scripts(lines, scripts or {})
    os.makedirs(out_dir, exist_ok=True)
    path = os.path.join(out_dir, f"{region}.inc")
    with open(path, "w", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    return path


def emit_na_modules(na, out_dir):
    """engine/data/regions/na_modules.inc: each NA enemy / spell module's FNV-1a 64 (the key of
    the port's module tables) and the hash of its normalised words, by which the port knows a
    module in any release. Every normalised hash must be unique (fatal otherwise)."""
    rows, seen = [], {}
    for kind, blocks in (("kEnemy", [b for _k, b in sorted(na.enemy.values(), key=lambda kb: R_key(kb))]),
                         ("kSpell", [na.spell[e] for e in sorted(na.spell)])):
        done = set()
        for b in blocks:
            f = fnv1a64(b)
            if f in done:
                continue
            done.add(f)
            h = module_hash(b)
            if (kind, h) in seen and seen[(kind, h)] != f:
                raise SystemExit(f"NA modules {seen[(kind, h)]:016x} and {f:016x} share a normalised hash")
            seen[(kind, h)] = f
            rows.append(f"    {{ImageKind::{kind}, {f:#018x}ull, {h:#018x}ull, {len(b):#x}u}},")
    lines = ["// Generated by tools/region_map.py from NA disc 1: do not edit. Facts only (hashes, sizes).",
             "// {kind, FNV-1a 64 of NA's block (the port's module key), module_hash of it, size}",
             "constexpr NaModule kNaModules[] = {"] + rows + ["};"]
    path = os.path.join(out_dir, "na_modules.inc")
    with open(path, "w", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    return path


# ---- Map scripts ---------------------------------------------------------------------------


def all_scripts(disc, cache):
    """{map: script blob} of every field map on the disc (cached in scratch)."""
    cache_file = None
    if cache:
        st = os.stat(disc.path)
        cache_file = os.path.join(cache, f"{boot_name(disc)}_{st.st_size}_{int(st.st_mtime)}_scripts.pickle")
        if os.path.exists(cache_file):
            return pickle.load(open(cache_file, "rb"))
    out = {m: disc.map_script(m) for m in disc.field_maps()}
    if cache_file:
        pickle.dump(out, open(cache_file, "wb"))
    return out


def script_key(ins):
    """An instruction for the alignment: its words, a branch's target masked (it moves)."""
    words = list(ins[1])
    if words and (words[0] >> 24) in (0x14, 0x15, 0x16):
        words[0] &= 0xFF000000
    return tuple(words)


def script_segments(na_code, rg_code):
    """Segments [(na_off, size, delta)] of a script whose layout differs, or None (same
    offsets)."""
    ia, ib = list(disassemble(na_code)), list(disassemble(rg_code))
    if len(na_code) == len(rg_code) and [x[0] for x in ia] == [y[0] for y in ib]:
        return None
    ka, kb = [script_key(x) for x in ia], [script_key(y) for y in ib]
    sm = difflib.SequenceMatcher(None, ka, kb, autojunk=False)
    segs = []

    def add(a1, a2, b1):
        lo = ia[a1][0]
        hi = ia[a2 - 1][0] + 4 * len(ia[a2 - 1][1])
        d = ib[b1][0] - lo
        if segs and segs[-1][0] + segs[-1][1] == lo and segs[-1][2] == d:
            segs[-1][1] += hi - lo
        else:
            segs.append([lo, hi - lo, d])

    for tag, a1, a2, b1, b2 in sm.get_opcodes():
        if tag == "equal":
            add(a1, a2, b1)
        elif tag == "replace" and a2 - a1 == b2 - b1 and all(
                len(ia[a1 + k][1]) == len(ib[b1 + k][1]) for k in range(a2 - a1)):
            add(a1, a2, b1)  # same-size change: word for word
    return [tuple(x) for x in segs]


def script_map_off(segs, off):
    for lo, n, d in segs:
        if lo <= off < lo + n:
            return off + d
    return None


def build_scripts(na_disc, rg_disc, cache, log):
    """{map: segments} for the maps whose script the release laid out otherwise; fatal list."""
    na, rg = all_scripts(na_disc, cache), all_scripts(rg_disc, cache)
    out, fatal = {}, []
    for m in sorted(na):
        if m not in rg:
            log(f"script of map {m}: missing in the release")
            continue
        a, b = parse_script(na[m])["code"], parse_script(rg[m])["code"]
        if a == b:
            continue
        segs = script_segments(a, b)
        if segs is None:
            continue
        out[m] = segs
        mapped = sum(n for _lo, n, _d in segs)
        log(f"script of map {m}: NA {len(a):#x} bytes, region {len(b):#x}; {len(segs)} segments map "
            f"{mapped:#x} bytes ({100 * mapped // max(1, len(a))}%)")
    # The Archipelago doors in those maps: the transfer must map onto the same instruction.
    doors_path = os.path.join(ROOT, "docs", "ap", "doors.json")
    if os.path.exists(doors_path):
        for d in json.load(open(doors_path))["doors"]:
            for m in [d["map"]] + d.get("maps", []):
                if m not in out:
                    continue
                at = script_map_off(out[m], d["offset"])
                a, b = parse_script(na[m])["code"], parse_script(rg[m])["code"]
                if at is None:
                    log(f"door {d['id']} (map {m} offset {d['offset']:#x}): no counterpart in the release's script")
                elif a[d["offset"]:d["offset"] + 4] != b[at:at + 4]:
                    fatal.append(f"door {d['id']} (map {m} offset {d['offset']:#x}): maps to {at:#x}, another instruction")
                break
    return out, fatal


def emit_scripts(lines, scripts):
    lines.append("")
    lines.append("// Map scripts the release laid out otherwise (code offsets: Archipelago doors, join sites,")
    lines.append("// mod script lines): segments of the script code {NA byte offset, byte count, region offset -")
    lines.append("// NA offset}. Other maps keep NA's offsets. Maps sharing a script share its segments.")
    names = {}
    for m, segs in sorted(scripts.items()):
        key = tuple(segs)
        if key in names:
            continue
        names[key] = f"kscript_{m}"
        lines.append(f"constexpr Segment kscript_{m}[] = {{")
        for lo, n, d in segs:
            lines.append(f"    {{{lo:#x}, {n:#x}, {d}}},")
        lines.append("};")
    lines.append("constexpr ScriptMap kScripts[] = {")
    for m, segs in sorted(scripts.items()):
        lines.append(f"    {{{m}, {names[tuple(segs)]}, {len(segs)}}},")
    if not scripts:
        lines.append("    {-1, nullptr, 0},")
    lines.append("};")


def R_key(kb):
    return (kb[0], fnv1a64(kb[1]))


def load_reads(path):
    """'image na_address' lines (hex) -> {image: set(addresses)}."""
    out = collections.defaultdict(set)
    if not path or not os.path.exists(path):
        return out
    for line in open(path):
        p = line.split()
        if len(p) >= 2 and not line.startswith("#"):
            out[p[0]].add(int(p[1], 16))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--na", required=True, help="NA disc 1 (.cue / .bin / .iso)")
    ap.add_argument("--disc", required=True, help="the region's disc 1")
    ap.add_argument("--region", help="region key (eu, fr, de); default from the disc's serial")
    ap.add_argument("--out", default=os.path.join(ROOT, "engine", "data", "regions"))
    ap.add_argument("--reads", default=os.path.join(ROOT, "tools", "regions", "na_reads.txt"))
    ap.add_argument("--report", help="write the full report here")
    ap.add_argument("--cache", help="cache the unpacked images here (scratch)")
    ap.add_argument("--check", action="store_true", help="only check; don't write the .inc")
    a = ap.parse_args()
    report = []

    def log(s):
        report.append(s)

    na = Release(a.na, a.cache)
    if na.serial != "SCUS_944.21":
        raise SystemExit(f"--na: {na.serial or 'no serial'} is not North American disc 1 (SCUS_944.21)")
    rg = Release(a.disc, a.cache)
    region = a.region or rg.region
    if rg.region != region or rg.disc_no != 1:
        raise SystemExit(f"--disc: {rg.serial} is not disc 1 of region {region}")
    with tempfile.TemporaryDirectory(dir=a.cache) as tmp:
        maps, problems = build(na, rg, tmp, log)
    scripts, script_fatal = build_scripts(na.disc, rg.disc, a.cache, log)
    # Checks (step 6).
    reads = load_reads(a.reads)
    fatal = []
    unmapped = 0
    for key, addrs in sorted(reads.items()):
        im = maps.get(key)
        if im is None:
            fatal.append(f"reads of image {key}: no map")
            continue
        # A read outside NA's image gets nothing on NA either (and nothing in the region).
        outside = [x for x in addrs if not (im.na_base <= x < im.na_base + im.na_size)]
        miss = sorted(x for x in addrs if x not in outside and im.map_addr(x) is None)
        unmapped += len(miss)
        for x in miss:
            fatal.append(f"{key} {x:#010x}: the port reads it, but it is not mapped")
        log(f"reads {key}: {len(addrs)} addresses, {len(addrs) - len(miss) - len(outside)} mapped"
            + (f", {len(outside)} outside NA's image (nothing there either)" if outside else ""))
    # Near matches and layout changes around what the port reads (step 6): a read in a short
    # run at a delta of its own (a near match the realignment couldn't resolve, because the
    # stretch around it changed size) is fatal; so is a read whose block may run past a
    # layout change (see READ_EXTENTS).
    suspicious = 0
    for key, addrs in sorted(reads.items()):
        im = maps.get(key)
        if im is None:
            continue
        segs = im.segs
        rd = sorted(x for x in addrs if im.na_base <= x < im.na_base + im.na_size)
        starts = [s[0] for s in segs]
        for n, x in enumerate(rd):
            w = (x - im.na_base) >> 2
            k = bisect.bisect_right(starts, w) - 1
            if k < 0 or w >= segs[k][1]:
                continue  # unmapped: fatal above
            lo, hi, d = segs[k][0], segs[k][1], segs[k][2]
            if (hi - lo < NEAR_WORDS and 0 < k < len(segs) - 1
                    and segs[k - 1][2] != d and segs[k + 1][2] != d):
                suspicious += 1
                fatal.append(f"{key} {x:#010x}: read in a near match ({4 * (hi - lo)} bytes at delta {4 * d}, "
                             f"between {4 * segs[k - 1][2]} and {4 * segs[k + 1][2]}): the data around it changed size")
                continue
            end = im.na_base + 4 * hi
            change = k + 1 >= len(segs) and hi < len(im.nw) or (
                k + 1 < len(segs) and (segs[k + 1][2] != d or segs[k + 1][0] != hi))
            nxt = rd[n + 1] if n + 1 < len(rd) else None
            in_table = any(b <= x < b + s for b, s, _w in TABLES.get(key, ()))
            if change and end - x <= CROSS_WINDOW and (nxt is None or nxt >= end) and not in_table:
                ext = READ_EXTENTS.get((key, x))
                if ext is None:
                    suspicious += 1
                    fatal.append(f"{key} {x:#010x}: suspicious: a layout change {end - x} bytes on, and the port's "
                                 f"read extent from here is unknown (check its reader; add it to READ_EXTENTS)")
                elif x + ext[0] > end:
                    suspicious += 1
                    fatal.append(f"{key} {x:#010x}: the port reads {ext[0]} bytes from here ({ext[1]}), past a "
                                 f"layout change {end - x} bytes on")
                else:
                    log(f"reads {key} {x:#x}: a layout change {end - x} bytes on; the port reads {ext[0]} ({ext[1]})")
        for lo, hi, before in im.realigned:
            moved = [x for x in rd if im.na_base + 4 * lo <= x < im.na_base + 4 * hi
                     and before.get((x - im.na_base) >> 2) != im.m.get((x - im.na_base) >> 2)]
            if moved:
                log(f"reads {key}: {len(moved)} in the realigned stretch {im.na_base + 4 * lo:#x}+{4 * (hi - lo):#x} "
                    f"(a near match before): " + " ".join(f"{x:#x}" for x in moved[:8]))
    refs_ok = refs_bad = refs_read = 0
    for key, im in maps.items():
        refs_ok += im.refs_ok
        refs_bad += len(im.refs)
        words = collections.defaultdict(list)
        for t, (rv, n) in im.refs.items():
            words[t & ~3].append((t, rv, n))
        for x in sorted(reads.get(key, ())):
            for t, rv, n in words.get(x & ~3, ()):
                refs_read += 1
                mv = im.map_addr(t)
                fatal.append(f"{key} {x:#010x}: the release's code keeps NA {t:#x} at {rv:#x} ({n} references), "
                             f"the map says " + (f"{mv:#x}" if mv is not None else "nothing"))
    log(f"code references (unchanged code, relocated): {refs_ok} addresses agree with the map, {refs_bad} don't "
        f"(zero-filled variables the port never reads, where the alignment can't tell the words apart); "
        f"{refs_read} of those the port reads")
    suspicious += refs_read
    for key, im in maps.items():
        for base, size, changed, dropped in im.tables:
            log(f"table {key} {base:#x}+{size:#x}: by position; {changed} words placed otherwise than the diff did, "
                f"{dropped} neighbouring words unmapped")
        fatal += im.table_fatal
    nreal = sum(len(im.realigned) for im in maps.values())
    log(f"near matches realigned: {nreal} stretches; suspicious reads: {suspicious}")
    # What the port will read that differs from NA (after pointer relocation): the region's
    # own data (text, PAL changes). Reported as ranges for review, not fatal.
    for key, addrs in sorted(reads.items()):
        im = maps.get(key)
        if im is None:
            continue
        rel = relocated(im, maps)
        diff = []
        for x in sorted(addrs):
            if not (im.na_base <= x < im.na_base + im.na_size):
                continue
            ro = im.map_off(x - im.na_base)
            if ro is None or ro >= len(rel):
                continue
            if im.na[x - im.na_base] != rel[ro]:
                diff.append(x)
        if diff:
            rngs = []
            for x in diff:
                if rngs and x - rngs[-1][1] <= 4:
                    rngs[-1][1] = x
                else:
                    rngs.append([x, x])
            log(f"reads {key}: {len(diff)} read bytes differ from NA in {len(rngs)} ranges: "
                + " ".join(f"{a:#x}-{b:#x}" for a, b in rngs[:60]))
    for key, im in maps.items():
        if im.ptr_bad:
            log(f"{key}: {len(im.ptr_bad)} NA pointers whose region word is not the mapped place:")
            for i, t, w, wr, want in im.ptr_bad[:40]:
                log(f"    +{i * 4:#06x} -> {t}: NA {w:#010x}, region {wr:#010x}, mapped "
                    + (f"{want:#010x}" if want is not None else "none"))
        rd = reads.get(key, set())
        bad_read = [(i, t, w) for i, t, w, wr, want in im.ptr_bad if im.na_base + i * 4 in rd]
        for i, t, w in bad_read:
            fatal.append(f"{key} {im.na_base + i * 4:#010x}: the port reads a pointer whose region value doesn't map")
    fatal += script_fatal
    for p in problems:
        log("problem: " + p)
    nseg = sum(len(im.segs) for im in maps.values())
    nptr = sum(len(runs(im.ptrs)) for im in maps.values())
    log(f"total: {len(maps)} images, {nseg} segments, {nptr} pointer runs "
        f"({sum(len(im.ptrs) for im in maps.values())} words), {len(scripts)} script maps; reads "
        f"{sum(len(v) for v in reads.values())}: {unmapped} unmapped, {suspicious} suspicious; {len(fatal)} fatal, "
        f"{len(problems)} problems")
    if a.report:
        with open(a.report, "w", newline="\n") as f:
            f.write("\n".join(report + ["FATAL: " + x for x in fatal]) + "\n")
    print(report[-1])
    for x in fatal[:20]:
        print("FATAL:", x)
    if fatal:
        sys.exit(1)
    if not a.check:
        print("wrote", emit(maps, rg, region, a.out, a.na, a.disc, scripts))
        print("wrote", emit_na_modules(na, a.out))


if __name__ == "__main__":
    main()
