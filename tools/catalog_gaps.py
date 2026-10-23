#!/usr/bin/env python3
"""Scan each program's own code for function addresses the catalog is missing.

catalog_gaps.py [MODULE...]

For every program in PROGRAMS (or only those named on the command line), three kinds of
evidence are gathered for "every function entry the code has":
  jal   every jal instruction word found anywhere in the program's own code blocks
        (scratchpad/fx_worker/archive/disc1.pickle, or boot's raw PS-X EXE), whose
        target address falls inside that module's known range in functions.csv (the
        26-bit field after the 0x0c000000 opcode, decoded as a plain KSEG0 address --
        every address in this game is 0x80xxxxxx, so the instruction's own address
        never has to be known). jal is always a real call (MIPS has no other use for it),
        so this is the strong signal;
  j     the same, but for plain j (0x08000000) instructions. Measured against
        battle_ovl1981 (see docs/handoff/catalog.md): 34/34 of its j-only hits were
        compiler-generated intra-function jumps (loop branches, switch dispatch, the
        epilogue's jr-ra setup), not calls to a missing function -- zero were real. j is
        kept, clearly separate, only because a tail call does compile to a plain j
        sometimes; treat a j-only hit as noise until the decompilation says otherwise;
  proc  REA's (Ghidra) list_procedures output for that program's ELF, dumped ahead of
        time to JSON (scratchpad/catalog/<name>_procs.json: [{"address","name","thunk",
        "external"}, ...]) with the MCP tools (REA sessions can't be driven from a
        plain script); thunks and externals are skipped.
Addresses in any of these sets that functions.csv does not already have a row for, for
that module, are "missing". jal+proc agreeing is the strongest signal; a jal-only hit
(no procs.json, or Ghidra does not call it a function) is next; j (alone or with proc) is
weak and needs the decompilation read before it earns a row -- raw code and data both
decompress into the same pickle blocks, so a jal/j-shaped word can come from data by
chance too. Always re-check a hit against the decompilation (procedure_pseudo_code /
procedure_assembly, or a manual disassembly of the bytes at that address) before adding a
row: a clean function start on this compiler looks like `addiu sp, sp, -N` (negative).

Appends a "## Missing rows" section to docs/catalog/COVERAGE.md (replacing any earlier
one), after the sections tools/catalog_coverage.py writes; run that script first. Rerun
this script whenever functions.csv gains rows, to see what is still missing.

To add a program: extend PROGRAMS below with its catalog module name(s), its archive
(entry, block) pairs or {"psxexe": path}, and, if you have dumped one, a procs.json path.
"""
import argparse
import csv
import json
import os
import pickle
import struct
import sys

CATALOG = "docs/catalog/functions.csv"
OUT = "docs/catalog/COVERAGE.md"
PICKLE = "scratchpad/fx_worker/archive/disc1.pickle"
BOOT_EXE = "extracted/disc1/SCUS_944.21"
PROCS_DIR = "scratchpad/catalog"

RAM_LO, RAM_HI = 0x80000000, 0x80200000  # 2 MB main RAM (KSEG0): every valid target lives here

# name -> {"modules": [...], "blocks": [(entry, block), ...] | "psxexe": path,
#          "procs": "scratchpad/catalog/<x>_procs.json" (optional)}
PROGRAMS = {
    "boot": {"modules": ["boot"], "psxexe": BOOT_EXE, "procs": f"{PROCS_DIR}/boot_procs.json"},
    "title": {"modules": ["title"], "blocks": [(1, 0)], "procs": f"{PROCS_DIR}/title1_procs.json"},
    "field": {"modules": ["field"], "blocks": [(3100, 0)], "procs": f"{PROCS_DIR}/field3100_procs.json"},
    "field_ovl_c1d": {"modules": ["field_ovl_c1d"], "blocks": [(3101, 0)]},
    "field_ovl_c1e": {"modules": ["field_ovl_c1e"], "blocks": [(3102, 0)]},
    "field_sub3103": {"modules": ["field_sub3103"], "blocks": [(3103, 0)]},
    "field_sub3104": {"modules": ["field_sub3104"], "blocks": [(3104, 0)]},
    "field_sub3105": {"modules": ["field_sub3105"], "blocks": [(3105, 0)]},
    "field_sub3106": {"modules": ["field_sub3106"], "blocks": [(3106, 0)]},
    "field_sub3108": {"modules": ["field_sub3108"], "blocks": [(3108, 0)]},
    "field_sub3109": {"modules": ["field_sub3109"], "blocks": [(3109, 0)]},
    "battle": {"modules": ["battle"], "blocks": [(2129, 0)], "procs": f"{PROCS_DIR}/battle2129_procs.json"},
    "battle_ovl1981": {"modules": ["battle_ovl1981"], "blocks": [(1981, 0)]},
    # #1984 is 11 blocks, each its own catalog module (battle_ovl1984_0 .. _10).
    **{
        f"battle_ovl1984_{n}": {"modules": [f"battle_ovl1984_{n}"], "blocks": [(1984, n)]}
        for n in range(11)
    },
    # Menu overlays 0xba5-0xbb9 (archive entries 2981-3001): one entry each.
    "menu_0ba5": {"modules": ["menu_0ba5"], "blocks": [(2981, 0)]},
    "menu_0ba6": {"modules": ["menu_0ba6"], "blocks": [(2982, 0)]},
    "menu_0baa": {"modules": ["menu_0baa"], "blocks": [(2986, 0)]},
    "menu_0bae": {"modules": ["menu_0bae"], "blocks": [(2990, 0)]},
    "menu_0bb2": {"modules": ["menu_0bb2"], "blocks": [(2994, 0)]},
    "menu_0bb6": {"modules": ["menu_0bb6"], "blocks": [(2998, 0)]},
    "menu_0bb9": {"modules": ["menu_0bb9"], "blocks": [(3001, 0)]},
    "credits_3107": {"modules": ["credits_3107"], "blocks": [(3107, 0)]},
}


def load_catalog():
    rows = list(csv.DictReader(open(CATALOG, newline="")))
    by_module = {}
    for r in rows:
        try:
            a = int(r["address"], 16)
        except (KeyError, ValueError):
            continue
        by_module.setdefault(r["module"], {})[a] = r["name"]
    return by_module


def jal_targets(data):
    """Every jal/j target in data (4-byte aligned words), within RAM_LO..RAM_HI."""
    jal, j = set(), set()
    for off in range(0, len(data) - 3, 4):
        w = struct.unpack_from("<I", data, off)[0]
        op = w >> 26
        if op not in (2, 3):
            continue
        t = RAM_LO | ((w & 0x3FFFFFF) << 2)
        if RAM_LO <= t < RAM_HI:
            (jal if op == 3 else j).add(t)
    return jal, j


def program_bytes(spec):
    """[(label, bytes), ...] for one program's code: archive blocks, or boot's raw EXE."""
    if "psxexe" in spec:
        data = open(spec["psxexe"], "rb").read()
        pc0, gp0, t_addr, t_size = struct.unpack_from("<4I", data, 0x10)
        return [("exe", data[0x800 : 0x800 + t_size])]
    blocks = pickle.load(open(PICKLE, "rb"))
    out = []
    for entry, block in spec["blocks"]:
        data = blocks.get((entry, block))
        if data is None:
            print(f"  ! {entry}.{block} not in {PICKLE} (rerun archive_scan.py?)", file=sys.stderr)
            continue
        out.append((f"{entry}.{block}", data))
    return out


def looks_like_code(data, base, addr):
    """A quick plausibility check: does addr look like a real function start in data
    (loaded at base), or more like data a jal/j-shaped word happened to point into?
    "fn" = a clean `addiu sp, sp, -N` prologue (this compiler's normal function entry);
    "leaf" = `jr ra` as the first word (a trivial no-argument-using leaf, as boot's
    kernel_noop_callback is); "?" = neither -- most likely a data table, not a function.
    Not authoritative (a hand-written leaf can start differently); always read the
    decompilation too."""
    off = addr - base
    if off < 0 or off + 4 > len(data):
        return "?"
    w = struct.unpack_from("<I", data, off)[0]
    if (w >> 26) == 0x09 and ((w >> 21) & 31) == 29 and ((w >> 16) & 31) == 29 and (w & 0x8000):
        return "fn"
    if w == 0x03E00008:
        return "leaf"
    return "?"


def load_procs(path):
    if not path or not os.path.exists(path):
        return None
    procs = json.load(open(path))
    return {int(p["address"], 16): p["name"] for p in procs if not p.get("thunk") and not p.get("external")}


def scan_program(name, spec, known):
    modules = spec["modules"]
    lo = min((min(known.get(m, {0: 0}) or [0]) for m in modules), default=0)
    hi = max((max(known.get(m, {0: 0}) or [0]) for m in modules), default=0)
    if lo == 0 and hi == 0:
        print(f"{name}: no catalog rows for {modules}, skipping range filter")
    jal, j = set(), set()
    labels = []
    blobs = program_bytes(spec)
    for label, data in blobs:
        labels.append(label)
        pj, pjj = jal_targets(data)
        jal |= pj
        j |= pjj
    # For the "looks like code" check: every PROGRAMS entry here is one block, so one
    # (data, base) pair covers it. psxexe's base is its own header; an archive block's is
    # assumed to be the module's lowest known address (true for every module checked by
    # hand so far -- credits_3107, battle_ovl1981 -- but not proven in general).
    code_check = None
    if blobs:
        data0 = blobs[0][1]
        if "psxexe" in spec:
            exe = open(spec["psxexe"], "rb").read()
            base0 = struct.unpack_from("<I", exe, 0x18)[0]
        else:
            base0 = lo
        code_check = lambda a: looks_like_code(data0, base0, a)  # noqa: E731
    jal_in_range = {a for a in jal if lo <= a <= hi}
    j_in_range = {a for a in j if lo <= a <= hi} - jal_in_range
    procs = load_procs(spec.get("procs"))
    proc_in_range = {a for a in procs if lo <= a <= hi} if procs else None
    have = set()
    for m in modules:
        have |= set(known.get(m, {}))
    jal_m, j_m, proc_m = jal_in_range - have, j_in_range - have, (proc_in_range or set()) - have
    both = sorted(jal_m & proc_m)
    jal_only = sorted(jal_m - proc_m)
    proc_only = sorted(proc_m - jal_m)
    j_only = sorted(j_m - jal_m - proc_m)
    checks = {a: code_check(a) for a in both + jal_only + proc_only + j_only} if code_check else {}
    return {
        "blocks": labels,
        "range": (lo, hi),
        "has_procs": procs is not None,
        "both": both,
        "jal_only": jal_only,
        "proc_only": proc_only,
        "j_only": j_only,
        "checks": checks,
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("names", nargs="*", help="program names to scan (default: all of PROGRAMS)")
    args = ap.parse_args()
    names = args.names or list(PROGRAMS)
    known = load_catalog()
    results = {}
    for name in names:
        if name not in PROGRAMS:
            print(f"unknown program {name!r}; choices: {', '.join(PROGRAMS)}", file=sys.stderr)
            continue
        results[name] = scan_program(name, PROGRAMS[name], known)

    total_missing = 0
    total_weak = 0
    cap = 25
    gaps_dir = f"{PROCS_DIR}/gaps"
    os.makedirs(gaps_dir, exist_ok=True)
    lines = ["## Missing rows", "",
             "`tools/catalog_gaps.py`: addresses the code calls (jal) or REA's procedure list",
             "names, inside a program's own known address range, that functions.csv has no row",
             "for yet. `both` = jal and a non-thunk Ghidra procedure agree (strongest); `jal",
             "only` / `proc only` = one source found it (weaker: check the decompilation --",
             "procedure_pseudo_code, or a manual disassembly -- before adding a row). `j only`",
             "(plain j, no jal, no proc) is listed separately and does not count toward a",
             "program's missing total: measured against battle_ovl1981, 34/34 of its j-only",
             "hits were intra-function jumps (loop branches, switch dispatch, epilogues), not",
             "missing functions; treat it as noise unless the decompilation says otherwise.",
             "Each address is tagged `[fn]` (a clean `addiu sp,sp,-N` prologue at that",
             "address), `[leaf]` (a bare `jr ra`, as a trivial no-op leaf would be) or `[?]`",
             "(neither -- measured against credits_3107, every `[?]` jal hit checked by hand",
             "was a data table a jal-shaped word happened to point into, not a function; most",
             "of a program's `[?]` hits are probably not real and the count below excludes",
             "them). No tag at all means the byte check could not run for that program.",
             "Programs with no `scratchpad/catalog/<name>_procs.json` dump show jal/j hits only.",
             f"Lists longer than {cap} are capped here; the full list (with tags) is in",
             f"`{gaps_dir}/<program>.json`.", ""]
    for name, r in results.items():
        checks = r["checks"]
        plausible = lambda a: checks.get(a) in ("fn", "leaf") or a not in checks  # noqa: E731
        n = sum(1 for a in r["both"] + r["jal_only"] + r["proc_only"] if plausible(a))
        nj = len(r["j_only"])
        if n == 0 and nj == 0:
            continue
        total_missing += n
        total_weak += nj
        json.dump({k: [f"0x{a:08x} [{checks.get(a, '?')}]" for a in r[k]]
                   for k in ("both", "jal_only", "proc_only", "j_only")},
                  open(f"{gaps_dir}/{name}.json", "w"), indent=1)
        lo, hi = r["range"]
        suffix = "" if r["has_procs"] else ", no procs dump"
        lines.append(f"### {name} ({n} + {nj} weak j-only, range 0x{lo:08x}-0x{hi:08x}, "
                      f"blocks {','.join(r['blocks'])}{suffix})")
        lines.append("")
        for label, key in (("both", "both"), ("jal only", "jal_only"), ("proc only", "proc_only"),
                            ("j only, weak", "j_only")):
            addrs = r[key]
            if not addrs:
                continue
            tag = lambda a: f"[{checks[a]}]" if a in checks else ""  # noqa: E731
            shown = ", ".join(f"0x{a:08x}{tag(a)}" for a in addrs[:cap])
            more = f" (+{len(addrs) - cap} more)" if len(addrs) > cap else ""
            lines.append(f"- {label} ({len(addrs)}): {shown}{more}")
        lines.append("")
    if total_missing == 0 and total_weak == 0:
        lines.append("None found in the programs scanned this run.")
        lines.append("")

    text = open(OUT).read() if os.path.exists(OUT) else ""
    # Replace a previous "## Missing rows" section (up to the next "## " or EOF), else append.
    marker = "## Missing rows"
    start = text.find(marker)
    block = "\n".join(lines).rstrip() + "\n"
    if start == -1:
        text = text.rstrip() + "\n\n" + block
    else:
        end = text.find("\n## ", start + len(marker))
        tail = text[end:] if end != -1 else ""
        text = text[:start] + block + ("\n" + tail.lstrip("\n") if tail else "")
    open(OUT, "w").write(text)
    print(f"{OUT}: {total_missing} missing addresses (+ {total_weak} weak j-only) across "
          f"{len(results)} programs scanned")


if __name__ == "__main__":
    main()
