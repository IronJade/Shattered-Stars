#!/usr/bin/env python3
"""Shared reading of Star Ocean: The Second Story's data from a disc image: map scripts and
what they hold (chests, map transfers, save points), item records and names, shop stock, enemy
stats, and (for a local reference only) item descriptions.

Used by tools/so2_mod.py (patch writing), port/tools/mod_reference.py (the modder's reference) and
the Archipelago extractors (through so2_mod, which re-exports everything here). Python 3, no
third-party modules.

Formats: docs/port/catalog/formats.md; the engine's readers are named next to each function.
"""

import hashlib
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from so2_archive import ARCHIVE_LBA, TOC_BYTES, SECTOR, SLZ_MAGIC, decrypt_toc, unpack_block  # noqa: E402
from so2_script import GLYPHS, MAP_ENTRY_BASE, disassemble, parse_script  # noqa: E402

SYNC = b"\x00" + b"\xff" * 10 + b"\x00"
FIELD_MAP_LIMIT = 1200     # map ids scanned for a script (the highest on the discs is 1114)
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# The releases (port/src/runtime/region.cpp kProfiles): SYSTEM.CNF boot file -> region key.
# Every table here, the patches and the repository's facts use North American numbers
# (addresses, script words); another release's disc is read through the engine's address
# maps (port/data/regions/<key>.inc, port/tools/region_map.py). Japan has none yet.
RELEASES = {
    "SCUS_944.21": "na", "SCUS_944.22": "na",
    "SCES_021.59": "eu", "SCES_121.59": "eu",
    "SCES_021.60": "fr", "SCES_121.60": "fr",
    "SCES_021.61": "de", "SCES_121.61": "de",
    "SLPM_861.05": "jp", "SLPM_861.06": "jp",
}


class Disc:
    def __init__(self, path):
        if path.lower().endswith(".cue"):
            base = os.path.dirname(os.path.abspath(path))
            for line in open(path, encoding="latin-1"):
                parts = line.strip().split('"')
                if line.strip().upper().startswith("FILE") and len(parts) >= 2:
                    path = os.path.join(base, parts[1])
                    break
        self.path = path
        self.f = open(path, "rb")
        # Raw 2352-byte sectors (.bin / .img: the user data after the header, and after the
        # subheader in mode 2) or a 2048-byte-sector .iso, as port/src/disc/disc_image.cpp.
        size = os.path.getsize(path)
        head = self.f.read(16)
        if size % 2352 == 0 and head[:12] == SYNC:
            self.raw, self.data_at = 2352, 24 if head[15] == 2 else 16
        elif size % 2048 == 0:
            self.raw, self.data_at = 2048, 0
        else:
            raise SystemExit(f"{path}: not a CD image this tool reads (.cue / .bin / .iso)")
        self.toc = {i: (lba, n) for i, lba, n in decrypt_toc(self.read(ARCHIVE_LBA, (TOC_BYTES + SECTOR - 1) // SECTOR))}
        self._scripts = {}
        self.serial = boot_serial(self)
        self.region = RELEASES.get(self.serial.upper())
        if self.region not in ("na", None) and not os.path.exists(region_inc_path(self.region)):
            raise SystemExit(f"{path}: the {self.serial} release has no address map yet "
                             "(port/data/regions/); use the North American or European discs")

    def close(self):
        self.f.close()

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    def read(self, lba, count):
        out = bytearray()
        for k in range(count):
            self.f.seek((lba + k) * self.raw + self.data_at)
            out += self.f.read(SECTOR)
        return bytes(out)

    def entry(self, index):
        lba, n = self.toc.get(index, (None, 0))
        if lba is None:
            raise SystemExit(f"archive entry {index} not present")
        return self.read(lba, n)

    def map_sections(self, map_id):
        data = self.entry(MAP_ENTRY_BASE + map_id)
        count = struct.unpack_from("<I", data, 0)[0]
        if not 1 <= count <= 16:
            raise SystemExit(f"map {map_id} is not a field map")
        return data, dict(struct.unpack_from("<II", data, 4 + 8 * i) for i in range(count))

    def map_script(self, map_id):
        if map_id in self._scripts:
            return self._scripts[map_id]
        data, sections = self.map_sections(map_id)
        offset = sections.get(1)
        if offset is None or data[offset:offset + 3] != SLZ_MAGIC:
            raise SystemExit(f"map {map_id}: no script section")
        blob = bytes(unpack_block(data, offset))
        self._scripts[map_id] = blob
        return blob

    def map_font(self, map_id):
        """The map's message font: section 1's second block (u32 count, u32 height, u8
        widths[count], u16 rows[count][height]; codes 1.. as so2_script.GLYPHS)."""
        data, sections = self.map_sections(map_id)
        offset = sections[1]
        nxt = struct.unpack_from("<I", data, offset + 12)[0]
        return bytes(unpack_block(data, offset + nxt))

    def field_maps(self):
        """Every map id with a script, ascending."""
        out = []
        for m in range(FIELD_MAP_LIMIT):
            if MAP_ENTRY_BASE + m not in self.toc:
                continue
            try:
                self.map_script(m)
            except (SystemExit, ValueError, struct.error, IndexError):
                continue
            out.append(m)
        return out


def boot_serial(disc):
    """SYSTEM.CNF's boot file ("SCUS_944.21"), or "" (ISO 9660 at sector 16)."""
    def file(name):
        pvd = disc.read(16, 1)
        if pvd[1:6] != b"CD001":
            return None
        lba, size = struct.unpack_from("<I", pvd, 158)[0], struct.unpack_from("<I", pvd, 166)[0]
        d = disc.read(lba, (size + SECTOR - 1) // SECTOR)
        pos = 0
        while pos < size:
            ln = d[pos]
            if ln == 0:
                pos = (pos // SECTOR + 1) * SECTOR
                continue
            nm = d[pos + 33:pos + 33 + d[pos + 32]].decode("latin-1").split(";")[0]
            if nm.upper() == name:
                flba, fsize = struct.unpack_from("<I", d, pos + 2)[0], struct.unpack_from("<I", d, pos + 10)[0]
                return disc.read(flba, (fsize + SECTOR - 1) // SECTOR)[:fsize]
            pos += ln
        return None
    cnf = file("SYSTEM.CNF")
    if not cnf:
        return ""
    for line in cnf.decode("latin-1").splitlines():
        if line.replace(" ", "").upper().startswith("BOOT="):
            return line.split("\\")[-1].split(":")[-1].split(";")[0].strip()
    return ""


def region_inc_path(key):
    return os.path.join(ROOT, "port", "data", "regions", f"{key}.inc")


_REGION_TABLES = {}


def region_tables(key):
    """A release's address maps, from the engine's generated port/data/regions/<key>.inc:
    {"images": {image key: [(na_off, size, delta)]}, "scripts": {map: [(na_off, size, delta)]}}."""
    if key in _REGION_TABLES:
        return _REGION_TABLES[key]
    import re
    arrays, images, scripts, cur = {}, {}, {}, None
    for line in open(region_inc_path(key)):
        mo = re.match(r"constexpr Segment (\w+)\[\] = \{", line)
        if mo:
            cur = arrays.setdefault(mo.group(1), [])
            continue
        if line.startswith("};"):
            cur = None
            continue
        mo = re.match(r"\s*\{(0x[0-9a-f]+|0), (0x[0-9a-f]+|0), (-?\d+)\},", line)
        if cur is not None and mo:
            cur.append((int(mo.group(1), 16), int(mo.group(2), 16), int(mo.group(3))))
            continue
        mo = re.match(r"\s*\{ImageKind::k\w+, .*?(k\w+)_seg, \d+, .*// (\S+)$", line)
        if mo:
            images[mo.group(2)] = arrays[mo.group(1) + "_seg"]
            continue
        mo = re.match(r"\s*\{(\d+), (kscript_\d+), \d+\},", line)
        if mo:
            scripts[int(mo.group(1))] = arrays[mo.group(2)]
    _REGION_TABLES[key] = {"images": images, "scripts": scripts}
    return _REGION_TABLES[key]


def _seg_map(segs, off, inverse=False):
    for lo, n, d in segs:
        start = lo + d if inverse else lo
        if start <= off < start + n:
            return off - d if inverse else off + d
    return None


def na_script_offset(disc, map_id, offset):
    """NA's byte offset (in the script code) of 'offset' in this disc's script of map_id:
    the same on NA and in every map the release laid out like NA; None when NA has none."""
    if disc.region in ("na", None):
        return offset
    segs = region_tables(disc.region)["scripts"].get(map_id)
    return offset if segs is None else _seg_map(segs, offset, inverse=True)


def image_offset(disc, image, na_offset):
    """Where NA's byte 'na_offset' of image 'image' ("baa", "title", ...) lies in this disc's
    image (None: no counterpart)."""
    if disc.region in ("na", None):
        return na_offset
    segs = region_tables(disc.region)["images"].get(image)
    return None if segs is None else _seg_map(segs, na_offset)


def find_disc2(disc1_path):
    """Disc 2's image next to disc 1's (the same name with "Disc 2"), or None."""
    p = os.path.abspath(disc1_path)
    for a, b in (("Disc 1", "Disc 2"), ("disc 1", "disc 2"), ("Disc1", "Disc2"), ("disc1", "disc2")):
        if a in p:
            q = p.replace(a, b)
            if os.path.exists(q):
                return q
    return None


# ---- Map scripts -------------------------------------------------------------------------

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


def chests(blob, ins=None):
    """[(flag, handler_word, value_ins)] for every chest (B 0x6c registration + handler)."""
    ins = instructions(blob) if ins is None else ins
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


def transfers(blob, ins=None):
    """[(index, ins, args)] for every inline map_transfer_c; args = (map, mode, x, y, z, dir,
    arrival parameter)."""
    out = []
    for i in (instructions(blob) if ins is None else ins):
        if i[2].startswith("map_transfer_c(") and len(i[1]) == 4:
            w = i[1]
            args = [w[0] & 0xFFFF] + [half for pair in w[1:] for half in (pair & 0xFFFF, pair >> 16)]
            args = [a - 0x10000 if a & 0x8000 else a for a in args[:7]]
            out.append((len(out), i, args))
    return out


def save_points(blob, ins=None):
    """[(x, y, z, size)] where the map's own code puts the save point: script B 0x66
    (marker_create: actor 0x80, script_table_b.cpp) with constant arguments, or a call with
    constant arguments of the library routine that runs it (every map carries that routine)."""
    ins = instructions(blob) if ins is None else ins
    starts = [n for n, i in enumerate(ins) if i[2].startswith("frame")]
    found = []
    for n, i in enumerate(ins):
        if not i[2].startswith("B_66") or n < 4:
            continue
        args = [push_value(ins[n - 4 + k]) for k in range(4)]
        if None not in args:
            found.append(tuple(args))
            continue
        start = max((s for s in starts if s <= n), default=None)
        if start is None:
            continue
        target = ins[start][0]
        for c, j in enumerate(ins):
            if j[2].startswith("call ") and j[3] == target and c >= 4:
                a2 = [push_value(ins[c - 4 + k]) for k in range(4)]
                if None not in a2:
                    found.append(tuple(a2))
    return sorted(set(found))


# ---- Items, shops, enemies ---------------------------------------------------------------

ITEM_ENTRY = 3002      # item records, 1023 x 0x30 (item 1 first)
ITEM_PACK = 3000       # the menus' item pack: block 2 locates each item's file in ITEM_FILES
ITEM_FILES = 0x1189    # item files (description text pack, then the 3D model), by sector
TEXT_ENTRY = 1980      # the battle's string table (item names: key 0xbb8 + item - 1)
ITEM_NAME_KEY = 0xBB8
SHOP_ENTRY = 0xBAA     # the shop page's code overlay, loaded at 0x8007e000
SHOP_TABLE = 0x80081C98
PAGE_BASE = 0x8007E000
FORMATION_BASE = 0x57D  # encounter N's pack is entry 0x57d + N
ENCOUNTERS = 0x234
ITEM_FIELDS = {"price": (0x00, 4), "atk": (0x08, 2), "def": (0x10, 2), "amount": (0x19, 1),
               "effect": (0x28, 1), "type": (0x2A, 1), "sell": (0x2D, 1)}
ENEMY_FIELDS = {"hp": (0x00, 4), "mp": (0x04, 4), "exp": (0x08, 4), "fol": (0x0C, 4), "speed": (0x16, 2),
                "drop1": (0x18, 2), "drop2": (0x1A, 2), "atk": (0x1C, 2), "def": (0x1E, 2), "int": (0x20, 2),
                "level": (0x24, 2), "eva": (0x26, 2), "acc": (0x28, 2), "guts": (0x36, 1),
                "drop1_odds": (0x37, 1), "drop2_odds": (0x38, 1)}

# Item type (+0x2a; menu_widget.cpp show_item_stats, menu_equip.cpp): our words.
ITEM_TYPES = {0: "item", 1: "weapon", 2: "weapon", 7: "weapon", 8: "armor", 9: "shield",
              10: "headgear", 11: "footwear", 12: "accessory"}
# Effect kind (+0x28; field/item_effect.cpp, battle/items.cpp). Kinds from 10 up are each one
# item's own effect.
ITEM_EFFECTS = {0: "", 1: "restores HP (%)", 2: "restores HP (%), whole party", 3: "restores MP (%)",
                4: "restores MP (%), whole party", 5: "restores HP and MP (%)", 6: "revives, restores HP (%)"}
# Who may equip (+0x04): bit n = character n + 1 (common/party.cpp char_equip_mask).
CHARACTERS = ["Claude", "Rena", "Celine", "Bowman", "Dias", "Precis", "Ashton", "Leon",
              "Opera", "Ernest", "Noel", "Chisato"]


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


def item_type_name(t):
    return ITEM_TYPES.get(t, f"type {t}")


def item_effect_name(kind):
    return ITEM_EFFECTS.get(kind, f"special effect {kind}")


def item_use_name(flags):
    return ", ".join(w for b, w in ((1, "battle"), (2, "field")) if flags & b)


def equip_names(mask):
    if mask & 0xFFF == 0xFFF:
        return "all"
    return " ".join(c for n, c in enumerate(CHARACTERS) if mask & (1 << n))


def item_rows(disc, names=None):
    """[{field: value}] for items 1..1023 from the records (entry 3002)."""
    rec = item_records(disc)
    out = []
    for item in range(1, len(rec) // 0x30 + 1):
        r = rec[(item - 1) * 0x30:item * 0x30]
        t, use, kind = r[0x2A], r[0x29], r[0x28]
        out.append({"id": item, "name": (names or {}).get(item, ""), "type": t, "category":
                    "usable item" if t == 0 and use else item_type_name(t), "use": use,
                    "price": le(r, 0, 4), "atk": le(r, 8, 2), "def": le(r, 0x10, 2), "effect": kind,
                    "amount": r[0x19], "sell": r[0x2D], "equip": le(r, 4, 2) if t else 0})
    return out


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


def item_names(disc):
    """{item: name} as the game shows it (two items can share a name)."""
    names = read_names(disc)
    return {i: names.get(ITEM_NAME_KEY + i - 1, "") for i in range(1, len(item_records(disc)) // 0x30 + 1)}


def shop_lists(disc):
    code = unpack_block(disc.entry(SHOP_ENTRY), 0)
    out = {}
    table = image_offset(disc, f"{SHOP_ENTRY:x}", SHOP_TABLE - PAGE_BASE)  # (another release: moved)
    if table is None:
        raise SystemExit(f"{disc.serial}: the shop table has no place in this release's map")
    for shop in range(0x60):
        at = table + shop * 4
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


def encounter_types(disc):
    """{encounter: [(type, stats record)]} of every encounter pack that reads."""
    out = {}
    for n in range(0, ENCOUNTERS):
        try:
            out[n] = formation_records(disc.entry(FORMATION_BASE + n))
        except (SystemExit, struct.error, IndexError):
            continue
    return out


def enemy_stats(disc, wanted=None):
    """{type: (encounter, record)} from the first encounter pack that holds each type."""
    found = {}
    for n in range(0, ENCOUNTERS):
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


def enemy_name(rec):
    return rec[0x4C:0x5C].split(b"\0")[0].decode("latin-1")


# ---- Item descriptions (local reference only: game text, never committed) -----------------

def _glyph_key(width, rows):
    return hashlib.sha1(bytes([width]) + rows).hexdigest()[:16]


def font_glyphs(font):
    """[(width, rows bytes)] of a message font."""
    n, h = struct.unpack_from("<II", font, 0)
    widths = font[8:8 + n]
    rows = font[8 + n:]
    return [(widths[g], rows[g * h * 2:(g + 1) * h * 2]) for g in range(n)]


# The map font's codes past so2_script.GLYPHS that item descriptions use.
MAP_FONT_EXTRA = {0x106: "'", 0xf0: ")", 0xf7: "(", 0xf6: '"', 0xfd: '"', 0xfe: " ", 0x118: "", 0x109: "\u266a"}
# Glyphs found only in item fonts, identified by their shape (hash of width + rows).
ITEM_FONT_EXTRA = {"81d36ffbecf85bc0": "'", "7eb91b2d7b6387a5": "'", "2cb882f2250f2250": "%",
                   "1f4aa56d50bc0bc7": "...", "fd444ef6138fd875": "\u03b1", "6406e05dc436d226": "\u03b2",
                   "b113672c1c57fe2f": "\u03b3"}


def glyph_table(disc, map_id=18):
    """{glyph hash: text} from a map's font (standard codes) plus the item-only glyphs."""
    table = {}
    for code, (w, r) in enumerate(font_glyphs(disc.map_font(map_id)), start=1):
        text = GLYPHS.get(code, MAP_FONT_EXTRA.get(code))
        if text is not None:
            table.setdefault(_glyph_key(w, r), text)
    for k, v in ITEM_FONT_EXTRA.items():
        table.setdefault(k, v)
    return table


def decode_item_text(text_pack, table):
    """The description (strings group 0, message 0) of an item file's text pack, with the
    file's own glyph codes read through 'table'. Unknown glyphs come out as U+FFFD."""
    _, s, f = struct.unpack_from("<III", text_pack, 0)
    strings = text_pack[s:]
    glyphs = font_glyphs(text_pack[f:])
    p = struct.unpack_from("<I", strings, 0)[0]
    out, raw = [], strings[p:]
    i = 0
    while i < len(raw):
        b = raw[i]
        i += 1
        code = b
        if b >= 0x80:
            code = (b & 0x7F) | (raw[i] << 7 if i < len(raw) else 0)
            i += 1
        if code == 0:
            break
        if code == 0x4000:
            out.append("\n")
        elif code >= 0x4000:
            continue
        elif 1 <= code <= len(glyphs):
            w, r = glyphs[code - 1]
            out.append(table.get(_glyph_key(w, r), "\ufffd"))
        else:
            out.append("\ufffd")
    lines = [" ".join(line.split()) for line in "".join(out).split("\n")]
    return "\n".join(line for line in lines if line)


def item_descriptions(disc, count=1023):
    """{item: description} read from each item's file (entry 0x1189, located through the item
    pack's block 2: u16 sector, u16 sectors per item; menu_viewer.cpp). Game text: for a
    modder's local reference only."""
    pack = bytes(unpack_block(disc.entry(ITEM_PACK), 0))
    table_at = struct.unpack_from("<I", pack, 8)[0]
    lba = disc.toc[ITEM_FILES][0]
    table = glyph_table(disc)
    out = {}
    for item in range(1, count + 1):
        sector, sectors = struct.unpack_from("<HH", pack, table_at + (item - 1) * 4)
        if sectors == 0:
            continue
        try:
            out[item] = decode_item_text(bytes(unpack_block(disc.read(lba + sector, sectors), 0)), table)
        except (ValueError, struct.error, IndexError):
            continue
    return out
