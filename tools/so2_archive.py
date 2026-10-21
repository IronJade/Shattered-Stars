#!/usr/bin/env python3
"""Read Star Ocean: The Second Story's data archive (SO2.BIN / disc 2's hidden file).

Everything here mirrors code in the boot executable (SCUS_944.21 / SCUS_944.22):

Table of contents -- FUN_80011c20
  The first 0x5A00 bytes of the archive (disc LBA 300) hold 4608 entries:
  a 32-bit word per entry, then one extra byte per entry at +0x4800. Both are
  XORed with a keystream seeded by 0x13578642. After decryption the low 24
  bits of a word are the entry's BCD disc position (minute, second, sector)
  and its top byte is the high byte of a 16-bit sector count; the extra byte
  is the low byte (read by FUN_80011654). Entry 0 is the table's own slot.

SLZ containers -- FUN_800121a8
  16-byte header: "SLZ", method byte, compressed size, unpacked size,
  offset to the next block (0 = last). Method 0 is stored, 1 is LZSS
  (FUN_800122b4), 2 is LZSS with run-length fills (FUN_8001275c).

Usage:
  so2_archive.py toc SO2.BIN [--csv out.csv]
  so2_archive.py extract SO2.BIN OUTDIR [--raw-only] [--only N ...]
  so2_archive.py unpack FILE OUT [--block N]
"""

import argparse
import csv
import os
import struct
import sys

ARCHIVE_LBA = 300
SECTOR = 2048
TOC_ENTRIES = 0x1200
TOC_BYTES = TOC_ENTRIES * 5
TOC_KEY = 0x13578642
SLZ_MAGIC = b"SLZ"
MASK32 = 0xFFFFFFFF


def bcd(value):
    return (value >> 4) * 10 + (value & 0x0F)


def decrypt_toc(raw):
    """Return [(index, lba, sectors)] for every entry, decrypted as FUN_80011c20 does."""
    words = list(struct.unpack_from(f"<{TOC_ENTRIES}I", raw, 0))
    low = bytearray(raw[TOC_ENTRIES * 4:TOC_BYTES])
    key = TOC_KEY
    for i in range(TOC_ENTRIES):
        mixed = (key ^ (key << 1)) & MASK32
        words[i] ^= key
        low[i] ^= mixed & 0xFF
        key = (~mixed ^ TOC_KEY) & MASK32
    entries = []
    for i, word in enumerate(words):
        minute, second, frame = word & 0xFF, (word >> 8) & 0xFF, (word >> 16) & 0xFF
        sectors = ((word >> 24) << 8) | low[i]
        if i == 0 or word == 0:
            entries.append((i, None, 0))
            continue
        lba = (bcd(minute) * 60 + bcd(second)) * 75 + bcd(frame) - 150
        entries.append((i, lba, sectors))
    return entries


def lzss(src, pos, out_size=None, rle=False):
    """Decode one SLZ method 1 (rle=False) or method 2 (rle=True) stream."""
    out = bytearray()
    flags, bits = 0, 0
    while out_size is None or len(out) < out_size:
        if bits == 0:
            flags, bits = src[pos], 8
            pos += 1
        literal = flags & 1
        flags >>= 1
        bits -= 1
        if literal:
            out.append(src[pos])
            pos += 1
            continue
        b0, b1 = src[pos], src[pos + 1]
        distance = b0 | (b1 & 0x0F) << 8
        nibble = b1 >> 4
        if not rle and distance == 0:
            break
        if rle and nibble == 0x0F:
            if distance < 0x100:
                out += bytes([src[pos + 2]]) * (b0 + 0x13)
                pos += 3
            else:
                out += bytes([b0]) * ((b1 & 0x0F) + 3)
                pos += 2
            continue
        start = len(out) - distance
        for k in range(nibble + 3):
            out.append(out[start + k])
        pos += 2
    return bytes(out[:out_size] if out_size is not None else out)


def slz_blocks(data):
    """Yield (offset, method, packed_size, unpacked_size) for each chained SLZ block."""
    offset = 0
    while offset + 16 <= len(data) and data[offset:offset + 3] == SLZ_MAGIC:
        method = data[offset + 3]
        packed, unpacked, next_offset = struct.unpack_from("<III", data, offset + 4)
        yield offset, method, packed, unpacked
        if next_offset == 0:
            break
        offset += next_offset


def nested_slz_offsets(data):
    """4-byte-aligned offsets past 0 where a plausible SLZ header sits.

    Some entries are containers the main program parses itself (for example a
    32-bit length followed by an SLZ block); their blocks are found by scanning.
    A hit only counts once it decodes to exactly its declared size.
    """
    offset = data.find(SLZ_MAGIC, 4)
    while offset != -1:
        if offset % 4 == 0 and offset + 16 <= len(data) and data[offset + 3] in (0, 1, 2):
            packed, unpacked = struct.unpack_from("<II", data, offset + 4)
            if 0 < packed <= len(data) - offset and 0 < unpacked < 0x200000:
                yield offset
        offset = data.find(SLZ_MAGIC, offset + 1)


def unpack_block(data, offset):
    method = data[offset + 3]
    unpacked = struct.unpack_from("<I", data, offset + 8)[0]
    body = offset + 16
    if method == 0:
        return data[body:body + unpacked]
    if method == 1:
        return lzss(data, body)
    if method == 2:
        return lzss(data, body, out_size=unpacked, rle=True)
    raise ValueError(f"unknown SLZ method {method} at {offset:#x}")


def read_toc(archive):
    with open(archive, "rb") as f:
        return decrypt_toc(f.read(TOC_BYTES))


def entry_bytes(archive_file, lba, sectors):
    archive_file.seek((lba - ARCHIVE_LBA) * SECTOR)
    return archive_file.read(sectors * SECTOR)


def cmd_toc(args):
    rows = []
    with open(args.archive, "rb") as f:
        for index, lba, sectors in read_toc(args.archive):
            if lba is None:
                continue
            head = entry_bytes(f, lba, 1)[:16]
            kind = "SLZ%d" % head[3] if head[:3] == SLZ_MAGIC else head[:4].hex()
            rows.append((index, lba, sectors, (lba - ARCHIVE_LBA) * SECTOR, sectors * SECTOR, kind))
    if args.csv:
        with open(args.csv, "w", newline="") as out:
            w = csv.writer(out)
            w.writerow(["index", "lba", "sectors", "archive_offset", "bytes", "kind"])
            w.writerows(rows)
    for r in rows if not args.csv else rows[:20]:
        print(f"#{r[0]:4d}  LBA {r[1]:6d}  {r[2]:5d} sectors  offset {r[3]:#010x}  {r[5]}")
    print(f"{len(rows)} entries", file=sys.stderr)


def cmd_extract(args):
    os.makedirs(args.outdir, exist_ok=True)
    wanted = set(args.only) if args.only else None
    failures = 0
    with open(args.archive, "rb") as f:
        for index, lba, sectors in read_toc(args.archive):
            if lba is None or (wanted is not None and index not in wanted):
                continue
            data = entry_bytes(f, lba, sectors)
            stem = os.path.join(args.outdir, f"{index:04d}")
            with open(stem + ".bin", "wb") as out:
                out.write(data)
            if args.raw_only:
                continue
            for n, (offset, method, _, unpacked) in enumerate(slz_blocks(data)):
                try:
                    plain = unpack_block(data, offset)
                except (IndexError, ValueError) as error:
                    failures += 1
                    print(f"#{index}: block {n} at {offset:#x}: {error}", file=sys.stderr)
                    continue
                if len(plain) != unpacked:
                    failures += 1
                    print(f"#{index}: block {n} unpacked to {len(plain):#x}, header says {unpacked:#x}", file=sys.stderr)
                with open(f"{stem}.{n}.unpacked", "wb") as out:
                    out.write(plain)
            if data[:3] == SLZ_MAGIC:
                continue
            for offset in nested_slz_offsets(data):
                unpacked = struct.unpack_from("<I", data, offset + 8)[0]
                try:
                    plain = unpack_block(data, offset)
                except (IndexError, ValueError):
                    continue
                if len(plain) == unpacked:
                    with open(f"{stem}.@{offset:x}.unpacked", "wb") as out:
                        out.write(plain)
    print(f"done; {failures} block(s) failed", file=sys.stderr)
    return 1 if failures else 0


def cmd_unpack(args):
    data = open(args.file, "rb").read()
    blocks = list(slz_blocks(data))
    if not blocks:
        raise SystemExit("no SLZ header at the start of the file")
    offset = blocks[args.block][0]
    plain = unpack_block(data, offset)
    open(args.out, "wb").write(plain)
    print(f"block {args.block}: method {data[offset + 3]}, {len(plain):#x} bytes -> {args.out}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="command", required=True)
    p = sub.add_parser("toc", help="decrypt and list the table of contents")
    p.add_argument("archive")
    p.add_argument("--csv", help="also write the table as CSV")
    p.set_defaults(run=cmd_toc)
    p = sub.add_parser("extract", help="write every entry, plus unpacked SLZ blocks")
    p.add_argument("archive")
    p.add_argument("outdir")
    p.add_argument("--raw-only", action="store_true", help="skip SLZ unpacking")
    p.add_argument("--only", type=int, nargs="+", metavar="N", help="extract only these entry numbers")
    p.set_defaults(run=cmd_extract)
    p = sub.add_parser("unpack", help="unpack one SLZ block from a file")
    p.add_argument("file")
    p.add_argument("out")
    p.add_argument("--block", type=int, default=0)
    p.set_defaults(run=cmd_unpack)
    args = ap.parse_args()
    return args.run(args)


if __name__ == "__main__":
    sys.exit(main())
