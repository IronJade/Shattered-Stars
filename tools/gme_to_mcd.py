#!/usr/bin/env python3
"""Convert a DexDrive memory-card image (.gme) to a raw 128 KB card (.mcd).

A .gme file is a 3904-byte (0xf40) DexDrive header followed by the raw 131072-byte
card. The engine (and PCSX-Redux) read raw cards, so the header is dropped. The input
file is only read, never modified.

    tools/gme_to_mcd.py SAVE.gme CARD.mcd
"""
import sys

HEADER = 0xF40
CARD = 128 * 1024


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__.strip().splitlines()[-1].strip(), file=sys.stderr)
        return 2
    src, dst = sys.argv[1], sys.argv[2]
    data = open(src, "rb").read()
    if len(data) != HEADER + CARD:
        print(f"{src}: {len(data)} bytes, expected {HEADER + CARD} (header + 128 KB card)", file=sys.stderr)
        return 1
    if not data[:12].startswith(b"123-456-STD"):
        print(f"{src}: no DexDrive signature; converting anyway", file=sys.stderr)
    card = data[HEADER:]
    if card[:2] != b"MC":
        print(f"{src}: the card does not start with 'MC' (not a PS1 card?)", file=sys.stderr)
        return 1
    with open(dst, "xb") as out:  # never overwrite an existing card
        out.write(card)
    print(f"wrote {dst} ({len(card)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
