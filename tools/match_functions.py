#!/usr/bin/env python3
"""Find functions shared by two programs that were linked at different addresses.

match_functions.py IMAGE_A INDEX_A IMAGE_B INDEX_B [--catalog functions.csv --module-a M]

IMAGE_* are raw program images loaded at 0x8002f810 (e.g. the title program, archive
entry #1, and the field program, #3100); INDEX_* are the index.csv files the Ghidra
script DecompileAll.java writes (address,name,size,...). Each function's instructions
are normalised so relocation does not matter: jal/j targets, lui/addiu/ori/lw/sw
immediates of address-like values and gp-relative offsets are masked. Functions of
both programs whose normalised bodies are identical (and unique on both sides) are
printed as "address_a,address_b,size,name_a" — with --catalog, name_a is taken from
the catalog rows of --module-a.
"""
import argparse
import csv
import struct
from collections import defaultdict

BASE = 0x8002F810


def normalise(img, addr, size):
    words = []
    for off in range(addr - BASE, addr - BASE + size, 4):
        if off + 4 > len(img):
            break
        w = struct.unpack_from("<I", img, off)[0]
        op = w >> 26
        if op in (2, 3):  # j / jal
            w &= 0xFC000000
        elif op == 0x0F:  # lui: high half of an address
            if (w & 0xFFFF) >= 0x8000:
                w &= 0xFFFF0000
        elif op >= 0x08 and op not in (0x0A, 0x0B):  # I-type ALU / loads / stores
            rs = (w >> 21) & 31
            if rs not in (0, 29, 30):  # not zero-, sp- or fp-relative: may be an address low half
                w &= 0xFFFF0000
        words.append(w)
    return tuple(words)


def load(index, img):
    out = {}
    for r in csv.DictReader(open(index)):
        a = int(r["address"], 16)
        sz = int(r["size"])
        if a < BASE or sz < 16:
            continue
        out[a] = (sz, r["name"], normalise(img, a, sz))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("image_a")
    ap.add_argument("index_a")
    ap.add_argument("image_b")
    ap.add_argument("index_b")
    ap.add_argument("--catalog")
    ap.add_argument("--module-a", default="title")
    args = ap.parse_args()
    a = load(args.index_a, open(args.image_a, "rb").read())
    b = load(args.index_b, open(args.image_b, "rb").read())
    names = {}
    if args.catalog:
        for r in csv.DictReader(open(args.catalog)):
            if r["module"] == args.module_a:
                names[int(r["address"], 16)] = r["name"]
    by_body = defaultdict(list)
    for addr, (sz, name, body) in b.items():
        by_body[body].append(addr)
    count_a = defaultdict(int)
    for addr, (sz, name, body) in a.items():
        count_a[body] += 1
    matched = 0
    for addr, (sz, name, body) in sorted(a.items()):
        hits = by_body.get(body, [])
        if len(hits) == 1 and count_a[body] == 1:
            matched += 1
            print(f"0x{addr:08x},0x{hits[0]:08x},{sz},{names.get(addr, name)}")
    print(f"# {matched} of {len(a)} functions matched", flush=True)


if __name__ == "__main__":
    main()
