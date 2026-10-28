#!/usr/bin/env python3
"""Wrap a PlayStation 1 PS-X EXE (and optional raw overlays) in an ELF32 file.

REA's Ghidra provider opens MIPS code only from ELF, and it does not know the
PS-X EXE container. This script maps the EXE's text segment at its load
address, marks the entry point, and can add raw blobs (overlays, RAM dumps) as
extra segments at the addresses the game loads them.

The header uses the shape REA admits as a "recordless legacy" MIPS32 image:
ET_EXEC, EF_MIPS_ARCH_32, no ABI flags record and no GNU attributes. The PS1's
R3000A executes MIPS I, which is a subset of that ISA. Ghidra's stock MIPS32
language does not model MIPS I load-delay slots and shows GTE (COP2) operations
as generic coprocessor instructions; use the ghidra_psx_ldr PSX language in the
Ghidra GUI when those details matter.

By default the rest of the 2 MB of main RAM, the scratchpad and the I/O port
window are mapped as zero-filled segments, and the CD-ROM, DMA, GPU, SPU and
interrupt registers are labelled, so cross-references to them resolve.

Examples:
  psxexe2elf.py SCUS_944.21 -o SCUS_944.21.elf
  psxexe2elf.py SCUS_944.21 --symbols docs/port/catalog/functions.csv:boot -o SCUS_944.21.elf
  psxexe2elf.py SCUS_944.21 --add battle.ovl@0x800a0000 -o with_battle.elf
  psxexe2elf.py --raw ramdump.bin --base 0x80000000 --entry 0x80010008 -o ram.elf
"""

import argparse
import csv
import struct
import sys

PSX_MAGIC = b"PS-X EXE"
PSX_HEADER_SIZE = 0x800

EM_MIPS = 8
ET_EXEC = 2
EF_MIPS_ARCH_32 = 0x50000000
PT_LOAD = 1
PF_RWX = 7

# PS1 address space (KSEG0 view). Regions not covered by file data become
# zero-filled segments so references into them resolve during analysis.
RAM_BASE, RAM_END = 0x80000000, 0x80200000
SCRATCHPAD = (0x1F800000, 0x400)
IO_PORTS = (0x1F801000, 0x2000)
HARDWARE_LABELS = {
    0x1F801070: "I_STAT", 0x1F801074: "I_MASK",
    0x1F8010B0: "DMA3_CDROM_MADR", 0x1F8010B4: "DMA3_CDROM_BCR", 0x1F8010B8: "DMA3_CDROM_CHCR",
    0x1F8010F0: "DMA_DPCR", 0x1F8010F4: "DMA_DICR",
    0x1F801800: "CDROM_INDEX_STATUS", 0x1F801801: "CDROM_CMD_RESPONSE",
    0x1F801802: "CDROM_PARAM_DATA", 0x1F801803: "CDROM_IRQ_REQUEST",
    0x1F801810: "GPU_GP0", 0x1F801814: "GPU_GP1",
    0x1F801DA6: "SPU_TRANSFER_ADDR", 0x1F801DA8: "SPU_TRANSFER_FIFO", 0x1F801DAA: "SPU_CONTROL",
}
SHT_PROGBITS, SHT_SYMTAB, SHT_STRTAB, SHT_NOBITS = 1, 2, 3, 8
SHF_WRITE, SHF_ALLOC, SHF_EXECINSTR = 1, 2, 4
STB_GLOBAL, STT_NOTYPE, STT_FUNC = 1, 0, 2
PAGE = 0x1000


def parse_psx_exe(data):
    if data[:8] != PSX_MAGIC:
        raise SystemExit("not a PS-X EXE (missing 'PS-X EXE' magic)")
    pc0, gp0, t_addr, t_size, _d_addr, _d_size, b_addr, b_size = struct.unpack_from("<8I", data, 0x10)
    text = data[PSX_HEADER_SIZE:PSX_HEADER_SIZE + t_size]
    if len(text) != t_size:
        raise SystemExit(f"truncated EXE: header declares {t_size:#x} text bytes, file holds {len(text):#x}")
    return {"pc0": pc0, "gp0": gp0, "t_addr": t_addr, "text": text, "b_addr": b_addr, "b_size": b_size}


def parse_add(spec):
    path, sep, addr = spec.rpartition("@")
    if not sep or not path:
        raise argparse.ArgumentTypeError("expected FILE@ADDRESS, e.g. overlay.bin@0x800a0000")
    return path, int(addr, 0)


def read_symbol_file(spec):
    """Yield (address, name) from '0xADDR NAME' lines, or from a catalog CSV.

    A CSV is given as PATH.csv:module1,module2 and must have module, address and
    name columns; only rows for the listed modules are used.
    """
    path, _, modules = spec.partition(".csv:")
    if modules or spec.endswith(".csv"):
        path = path + ".csv" if modules else spec
        wanted = set(modules.split(",")) if modules else None
        with open(path, newline="") as f:
            for row in csv.DictReader(f):
                if wanted is None or row["module"] in wanted:
                    yield int(row["address"], 0), row["name"]
        return
    for line in open(spec):
        parts = line.split()
        if len(parts) >= 2 and not line.startswith("#"):
            yield int(parts[0], 0), parts[1]


def ram_gap_segments(segments):
    """Zero-filled segments for the parts of main RAM no file segment covers."""
    covered = sorted((a, a + len(d) + z) for _, a, d, z in segments if RAM_BASE <= a < RAM_END)
    gaps, cursor = [], RAM_BASE
    for start, end in covered:
        if start > cursor:
            gaps.append((cursor, start))
        cursor = max(cursor, end)
    if cursor < RAM_END:
        gaps.append((cursor, RAM_END))
    return [(f".ram_{start:08x}", start, b"", end - start) for start, end in gaps]


def build_elf(segments, entry, symbols):
    """segments: [(name, vaddr, bytes, bss_size)]; symbols: [(name, vaddr, type)]."""
    segments = sorted(segments, key=lambda s: s[1])
    for (n1, a1, d1, z1), (n2, a2, _, _) in zip(segments, segments[1:]):
        if a1 + len(d1) + z1 > a2:
            raise SystemExit(f"segments overlap: {n1} ends at {a1 + len(d1) + z1:#x}, {n2} starts at {a2:#x}")

    ehsize, phentsize, shentsize = 52, 32, 40
    phoff = ehsize
    offset = PAGE
    layout = []
    for name, vaddr, data, bss in segments:
        # Keep file offset congruent to vaddr modulo the page size.
        offset += (vaddr - offset) % PAGE
        layout.append((name, vaddr, data, bss, offset))
        offset += len(data)

    shstrtab = b"\0"
    def shname(n):
        nonlocal shstrtab
        idx = len(shstrtab)
        shstrtab += n.encode() + b"\0"
        return idx

    strtab = b"\0"
    symtab = b"\0" * 16
    text_index = {}
    sections = [b"\0" * shentsize]
    for name, vaddr, data, bss, off in layout:
        text_index[name] = len(sections)
        if data:
            sections.append(struct.pack("<10I", shname(name), SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR | SHF_WRITE,
                                        vaddr, off, len(data), 0, 0, 4, 0))
        if bss:
            sections.append(struct.pack("<10I", shname(name + ".bss" if data else name), SHT_NOBITS,
                                        SHF_ALLOC | SHF_WRITE, vaddr + len(data), off + len(data), bss, 0, 0, 4, 0))

    def section_for(addr):
        for name, vaddr, data, bss, _ in layout:
            if vaddr <= addr < vaddr + len(data) + bss:
                return text_index[name]
        return 0xFFF1  # SHN_ABS

    for name, addr, kind in symbols:
        idx = len(strtab)
        strtab += name.encode() + b"\0"
        symtab += struct.pack("<IIIBBH", idx, addr, 0, (STB_GLOBAL << 4) | kind, 0, section_for(addr))

    symtab_name, strtab_name, shstrtab_name = shname(".symtab"), shname(".strtab"), shname(".shstrtab")
    symtab_off = offset
    strtab_off = symtab_off + len(symtab)
    shstrtab_off = strtab_off + len(strtab)
    shoff = (shstrtab_off + len(shstrtab) + 3) & ~3
    strtab_index = len(sections) + 1
    sections.append(struct.pack("<10I", symtab_name, SHT_SYMTAB, 0, 0, symtab_off, len(symtab), strtab_index, 1, 4, 16))
    sections.append(struct.pack("<10I", strtab_name, SHT_STRTAB, 0, 0, strtab_off, len(strtab), 0, 0, 1, 0))
    shstrndx = len(sections)
    sections.append(struct.pack("<10I", shstrtab_name, SHT_STRTAB, 0, 0, shstrtab_off, len(shstrtab), 0, 0, 1, 0))

    ident = b"\x7fELF" + bytes([1, 1, 1, 0]) + b"\0" * 8  # ELFCLASS32, little-endian, SYSV
    header = ident + struct.pack("<HHIIIIIHHHHHH", ET_EXEC, EM_MIPS, 1, entry, phoff, shoff, EF_MIPS_ARCH_32,
                                 ehsize, phentsize, len(layout), shentsize, len(sections), shstrndx)
    out = bytearray(header)
    for name, vaddr, data, bss, off in layout:
        out += struct.pack("<8I", PT_LOAD, off, vaddr, vaddr, len(data), len(data) + bss, PF_RWX, PAGE)
    for name, vaddr, data, bss, off in layout:
        out += b"\0" * (off - len(out))
        out += data
    out += symtab + strtab + shstrtab
    out += b"\0" * (shoff - len(out))
    for s in sections:
        out += s
    return bytes(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("exe", nargs="?", help="PS-X EXE to wrap")
    ap.add_argument("-o", "--output", required=True, help="ELF file to write")
    ap.add_argument("--add", action="append", type=parse_add, default=[], metavar="FILE@ADDR",
                    help="add a raw blob (overlay, RAM dump) as a segment at ADDR; repeatable")
    ap.add_argument("--raw", help="wrap a raw blob instead of a PS-X EXE (needs --base)")
    ap.add_argument("--base", type=lambda v: int(v, 0), help="load address for --raw")
    ap.add_argument("--entry", type=lambda v: int(v, 0), help="entry point (defaults to the EXE's pc0, or --base)")
    ap.add_argument("--symbols", action="append", default=[], metavar="FILE",
                    help="function names to embed: a '0xADDR NAME' list, or a catalog CSV as "
                         "functions.csv:module1,module2; repeatable")
    ap.add_argument("--no-memory-map", action="store_true",
                    help="map only the file data, not the rest of RAM, scratchpad and I/O ports")
    args = ap.parse_args()

    if bool(args.exe) == bool(args.raw):
        ap.error("give exactly one of EXE or --raw")

    segments, symbols = [], []
    if args.exe:
        exe = parse_psx_exe(open(args.exe, "rb").read())
        segments.append((".text", exe["t_addr"], exe["text"], 0))
        if exe["b_size"]:
            segments.append((".psx_bss", exe["b_addr"], b"", exe["b_size"]))
        entry = args.entry if args.entry is not None else exe["pc0"]
        if exe["gp0"]:
            symbols.append(("_gp", exe["gp0"], STT_NOTYPE))
    else:
        if args.base is None:
            ap.error("--raw needs --base")
        segments.append((".raw", args.base, open(args.raw, "rb").read(), 0))
        entry = args.entry if args.entry is not None else args.base

    for i, (path, addr) in enumerate(args.add):
        segments.append((f".overlay{i}", addr, open(path, "rb").read(), 0))
        symbols.append((f"overlay{i}_start", addr, STT_NOTYPE))

    if not args.no_memory_map:
        segments += ram_gap_segments(segments)
        segments.append((".scratchpad", SCRATCHPAD[0], b"", SCRATCHPAD[1]))
        segments.append((".io", IO_PORTS[0], b"", IO_PORTS[1]))
        symbols += [(name, addr, STT_NOTYPE) for addr, name in sorted(HARDWARE_LABELS.items())]

    named = {addr for _, addr, _ in symbols}
    for spec in args.symbols:
        for addr, name in read_symbol_file(spec):
            if addr not in named:
                symbols.append((name, addr, STT_FUNC))
                named.add(addr)

    symbols.insert(0, ("_start", entry, STT_FUNC))
    elf = build_elf(segments, entry, symbols)
    with open(args.output, "wb") as f:
        f.write(elf)
    print(f"wrote {args.output}: entry {entry:#010x}, " +
          ", ".join(f"{n} @ {a:#010x} ({len(d) + z:#x} bytes)" for n, a, d, z in sorted(segments, key=lambda s: s[1])))


if __name__ == "__main__":
    sys.exit(main())
