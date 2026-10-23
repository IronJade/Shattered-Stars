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
    - every pointer the port follows (a read address that is the value of a listed pointer
      word) maps;
    - the region image's FNV-1a 64 is recorded, so the port refuses a different revision of
      the disc instead of reading wrong addresses.

Enemy-code blocks (the 40 modules of the encounter packs) and spell-code modules are matched
by a hash of their normalised words (the same normalisation the engine computes at load,
region.cpp module_hash), so the port finds each region's module whatever its link addresses.
"""

import argparse
import collections
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
        # Segments: runs of NA words mapped at a constant word delta.
        segs = []
        for i in sorted(m):
            d = m[i] - i
            if segs and segs[-1][1] == i and segs[-1][2] == d:
                segs[-1][1] = i + 1
                segs[-1][3][kind[i]] += 1
            else:
                segs.append([i, i + 1, d, collections.Counter({kind[i]: 1})])
        self.segs = segs
        self.na_size, self.rg_size = len(na), len(rg)
        # The tail bytes of an image whose size isn't a multiple of 4: mapped with the last
        # word when both images end alike (same tail length, last words aligned to each other).
        nt, rt = len(na) - 4 * len(nw), len(rg) - 4 * len(rw)
        self.tail = nt if nt and nt == rt and m.get(len(nw) - 1) == len(rw) - 1 else 0
        if self.tail and segs and segs[-1][1] == len(nw):
            segs[-1].append(self.tail)  # extra bytes after the segment's last word

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
    return maps, problems


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
EVIDENCE = {"eq": "equal", "chg": "same-size change"}


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


def emit(maps, rg, region, out_dir, na_path, rg_path):
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
    # Checks (step 6).
    reads = load_reads(a.reads)
    fatal = []
    for key, addrs in sorted(reads.items()):
        im = maps.get(key)
        if im is None:
            fatal.append(f"reads of image {key}: no map")
            continue
        # A read outside NA's image gets nothing on NA either (and nothing in the region).
        outside = [x for x in addrs if not (im.na_base <= x < im.na_base + im.na_size)]
        miss = sorted(x for x in addrs if x not in outside and im.map_addr(x) is None)
        for x in miss:
            fatal.append(f"{key} {x:#010x}: the port reads it, but it is not mapped")
        log(f"reads {key}: {len(addrs)} addresses, {len(addrs) - len(miss) - len(outside)} mapped"
            + (f", {len(outside)} outside NA's image (nothing there either)" if outside else ""))
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
    for p in problems:
        log("problem: " + p)
    nseg = sum(len(im.segs) for im in maps.values())
    nptr = sum(len(runs(im.ptrs)) for im in maps.values())
    log(f"total: {len(maps)} images, {nseg} segments, {nptr} pointer runs "
        f"({sum(len(im.ptrs) for im in maps.values())} words), {len(fatal)} fatal, {len(problems)} problems")
    if a.report:
        with open(a.report, "w", newline="\n") as f:
            f.write("\n".join(report + ["FATAL: " + x for x in fatal]) + "\n")
    print(report[-1])
    for x in fatal[:20]:
        print("FATAL:", x)
    if fatal:
        sys.exit(1)
    if not a.check:
        print("wrote", emit(maps, rg, region, a.out, a.na, a.disc))
        print("wrote", emit_na_modules(na, a.out))


if __name__ == "__main__":
    main()
