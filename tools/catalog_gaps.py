#!/usr/bin/env python3
"""Scan each program's own code for function addresses the catalog is missing.

catalog_gaps.py [MODULE...]

For every program in PROGRAMS (or only those named on the command line), four kinds of
evidence are gathered for "every function entry the code has":
  jal     every jal instruction word found anywhere in the program's own code blocks
          (scratchpad/fx_worker/archive/disc1.pickle, or boot's raw PS-X EXE), whose
          target address falls inside that module's known range in functions.csv (the
          26-bit field after the 0x0c000000 opcode, decoded as a plain KSEG0 address --
          every address in this game is 0x80xxxxxx, so the instruction's own address
          never has to be known). jal is always a real call (MIPS has no other use for
          it), so every jal target is a function start, whether or not it happens to
          begin with this compiler's usual `addiu sp,sp,-N` prologue -- a hand-written
          PsyQ leaf (memcpy, setjmp, ...) often doesn't have one, and used to be dropped
          here for exactly that reason;
  tail-j  a plain j (0x08000000) instruction whose own address falls inside a function
          (by the boundary list below) but whose TARGET lands outside that function's
          range: a tail call, compiled as a plain jump because it's the last thing the
          caller does. Measured against battle_ovl1981 (docs/handoff/catalog.md), every
          other plain j is an intra-function jump (loop branch, switch dispatch, the
          epilogue's jr-ra setup) -- those stay noise, listed separately as "j only,
          weak" and excluded from the missing total, same as before;
  table   a run of >=3 consecutive words in the program's own data that each look like
          a code pointer into this program's own address range AND land on a plausible
          instruction opcode at that target (not a random byte landing mid-instruction).
          This is how a vtable or a function-pointer table turns up: its entries are
          never a jal/j instruction anywhere, so the jal/tail-j scans can't see them;
  REA     REA's (Ghidra) list_procedures output for that program's ELF, dumped ahead of
          time to JSON (scratchpad/catalog/<name>_procs.json: [{"address","name","thunk",
          "external"}, ...]) with the MCP tools (REA sessions can't be driven from a
          plain script); thunks and externals are skipped. Ghidra's own analysis finds
          some function starts no jal/j word points at (reached only through a table, or
          through a register-indirect jalr) -- those are exactly the ones "jal only" /
          "tail-j" / "table" above cannot see either, so REA is kept as its own source.
Every one of the four sources on its own is enough to call an address a candidate --
there is no more "no prologue -> skip" filtering. The `[fn]` / `[leaf]` / `[?]` byte tag
(a clean `addiu sp,sp,-N` prologue / a bare `jr ra` / neither) is still shown beside each
address, but purely as a hint for the human filling the row in; it no longer removes an
address from the missing count. Always re-check a hit against the decompilation
(procedure_pseudo_code / procedure_assembly, or a manual disassembly of the bytes at that
address) before adding a row -- raw code and data both decompress into the same pickle
blocks, so a jal/j/table-shaped word can come from data by chance, especially for `table`
and for a `[?]`-tagged address with only one source.

Appends a "## Missing rows" section to docs/catalog/COVERAGE.md (replacing any earlier
one), after the sections tools/catalog_coverage.py writes; run that script first. Rerun
this script whenever functions.csv gains rows, to see what is still missing.

WARNING: Naming specific programs on the command line replaces the "Missing rows"
section with only those programs' listings; run without arguments to scan all programs.

To add a program: extend PROGRAMS below with its catalog module name(s), its archive
(entry, block) pairs or {"psxexe": path}, and, if you have dumped one, a procs.json path.
"""
import argparse
import bisect
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


def scan_words(data, base):
    """One pass over data (loaded at base): every jal target, every plain-j (src, target)
    pair, and every offset that looks like a `addiu sp,sp,-N` function-start prologue
    (this compiler's normal entry -- used only to help bound functions for the tail-call
    check below, not as a filter on candidates)."""
    jal = set()
    j_hits = []  # (src_addr, target)
    fn_starts = []
    for off in range(0, len(data) - 3, 4):
        w = struct.unpack_from("<I", data, off)[0]
        op = w >> 26
        if op in (2, 3):
            t = RAM_LO | ((w & 0x3FFFFFF) << 2)
            if RAM_LO <= t < RAM_HI:
                if op == 3:
                    jal.add(t)
                else:
                    j_hits.append((base + off, t))
        if op == 0x09 and ((w >> 21) & 31) == 29 and ((w >> 16) & 31) == 29 and (w & 0x8000):
            fn_starts.append(base + off)
    return jal, j_hits, fn_starts


def table_targets(data, base, lo, hi, run_min=3):
    """Runs of >=run_min consecutive words that all look like code pointers into
    [lo, hi] (the program's own range: "inside the same image") and land on a plausible
    instruction boundary at that target (looks_like_code: a clean `addiu sp,sp,-N`
    prologue, or a bare `jr ra`). This is how a vtable / function-pointer table shows
    up: its entries are never a jal/j instruction, so the scans above can't see them.
    Returns the set of distinct target addresses found this way. The boundary check
    (not just "is it a valid-looking opcode") matters a lot here: without it, ordinary
    code reinterpreted as a flat word stream throws up hundreds of 3-in-a-row
    coincidences that are not real tables (measured on field: ~625 of those, almost all
    landing on neither a prologue nor a `jr ra`)."""
    targets = set()
    run = []

    def flush():
        if len(run) >= run_min:
            targets.update(run)

    for off in range(0, len(data) - 3, 4):
        w = struct.unpack_from("<I", data, off)[0]
        ok = lo <= w <= hi and (w & 3) == 0 and looks_like_code(data, base, w) in ("fn", "leaf")
        if ok:
            run.append(w)
        else:
            flush()
            run = []
    flush()
    return targets


def looks_like_code(data, base, addr):
    """A quick plausibility check: does addr look like a real function start in data
    (loaded at base), or more like data a jal/j/table-shaped word happened to point
    into? "fn" = a clean `addiu sp, sp, -N` prologue (this compiler's normal function
    entry); "leaf" = `jr ra` as the first word (a trivial no-argument-using leaf, as
    boot's kernel_noop_callback is); "?" = neither -- informational only now (see module
    docstring): every candidate still counts as missing regardless of this tag. Not
    authoritative (a hand-written leaf can start differently); always read the
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


def program_bytes(spec):
    """(label, bytes, base) for one program's code: archive blocks, or boot's raw EXE.
    base is the address the bytes are loaded at -- the EXE's own header t_addr for a
    psxexe, or (same assumption scan_program already made for the byte-plausibility
    check) the module's lowest known catalog address for an archive block."""
    if "psxexe" in spec:
        data = open(spec["psxexe"], "rb").read()
        t_addr, t_size = struct.unpack_from("<2I", data, 0x18)
        return [("exe", data[0x800 : 0x800 + t_size], t_addr)]
    blocks = pickle.load(open(PICKLE, "rb"))
    out = []
    for entry, block in spec["blocks"]:
        data = blocks.get((entry, block))
        if data is None:
            print(f"  ! {entry}.{block} not in {PICKLE} (rerun archive_scan.py?)", file=sys.stderr)
            continue
        out.append((f"{entry}.{block}", data, None))  # base filled in by caller (needs lo)
    return out


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
    blobs = program_bytes(spec)
    jal, j_hits, fn_starts = set(), [], []
    data0 = base0 = None
    for label, data, base in blobs:
        base = base if base is not None else lo  # archive block: assume base == module's lowest known address
        if data0 is None:
            data0, base0 = data, base
        pj, pjh, pfn = scan_words(data, base)
        jal |= pj
        j_hits += pjh
        fn_starts += pfn
    have = set()
    for m in modules:
        have |= set(known.get(m, {}))
    procs = load_procs(spec.get("procs"))
    proc_in_range = {a for a in procs if lo <= a <= hi} if procs else set()

    jal_in_range = {a for a in jal if lo <= a <= hi}
    tables = table_targets(data0, base0, lo, hi) if data0 is not None else set()
    table_in_range = {a for a in tables if lo <= a <= hi}

    # Tail-call classification: boundaries = every function start we can see (scanned
    # prologues, plus what the catalog and REA already know), inside this blob's own
    # byte range (not just the narrower catalog lo..hi, so the check also works right up
    # to the end of the blob). A plain j whose own address falls inside one of these
    # ranges but whose target lands outside it is a tail call -- kept only when the
    # target also lands on a plausible instruction boundary (fn/leaf), the same
    # requirement as the table scan; without it, a stale/merged boundary (a real
    # function this pass never found a prologue for swallows its neighbours' range)
    # throws up far more "tail calls" than there really are. Anything else is an
    # intra-function jump (noise, same precedent as before).
    tail_j = set()
    j_noise = set()
    if data0 is not None:
        blob_lo, blob_hi = base0, base0 + len(data0)
        boundaries = sorted(set(fn_starts) | {a for a in have if blob_lo <= a < blob_hi}
                             | {a for a in proc_in_range if blob_lo <= a < blob_hi})
        for src, tgt in j_hits:
            if not (blob_lo <= src < blob_hi):
                continue
            i = bisect.bisect_right(boundaries, src) - 1
            if i < 0:
                continue
            start = boundaries[i]
            end = boundaries[i + 1] if i + 1 < len(boundaries) else blob_hi
            if (tgt < start or tgt >= end) and looks_like_code(data0, base0, tgt) in ("fn", "leaf"):
                tail_j.add(tgt)
            else:
                j_noise.add(tgt)
    tail_j_in_range = {a for a in tail_j if lo <= a <= hi}
    j_noise_in_range = {a for a in j_noise if lo <= a <= hi} - jal_in_range - tail_j_in_range

    sources = {}  # addr -> set of source tags
    for a in jal_in_range - have:
        sources.setdefault(a, set()).add("jal")
    for a in proc_in_range - have:
        sources.setdefault(a, set()).add("REA")
    for a in tail_j_in_range - have:
        sources.setdefault(a, set()).add("tail-j")
    for a in table_in_range - have:
        sources.setdefault(a, set()).add("table")

    code_check = (lambda a: looks_like_code(data0, base0, a)) if data0 is not None else (lambda a: "?")
    checks = {a: code_check(a) for a in sources}
    return {
        "blocks": [b[0] for b in blobs],
        "range": (lo, hi),
        "has_procs": procs is not None,
        "sources": sources,
        "checks": checks,
        "j_only": sorted(j_noise_in_range),
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
             "`tools/catalog_gaps.py`: addresses the code calls (jal/tail-j), a pointer table",
             "points at (table), or REA's procedure list names (REA), inside a program's own",
             "known address range, that functions.csv has no row for yet. Every one of these",
             "four sources on its own is enough to call an address a candidate -- jal is always",
             "a real call, tail-j a plain j whose target lands outside the jumping function's",
             "own range (a tail call), table a run of >=3 consecutive data words that all point",
             "at plausible code in this program's own range, REA Ghidra's own function-boundary",
             "analysis. A candidate is tagged with every source that found it (`jal+REA` is the",
             "strongest). Each address also carries `[fn]` (a clean `addiu sp,sp,-N` prologue),",
             "`[leaf]` (a bare `jr ra`) or `[?]` (neither) -- informational only: it no longer",
             "excludes an address from the count (a hand-written PsyQ leaf like memcpy or",
             "setjmp has no prologue and used to be dropped here for exactly that reason).",
             "Always check the decompilation (procedure_pseudo_code / a manual disassembly)",
             "before adding a row, especially for a single-source or `[?]` hit.",
             "",
             "A plain j that is NOT a tail call (lands inside its own function -- a loop",
             "branch, switch dispatch, the epilogue's jr-ra setup) is listed separately as",
             "\"j only, weak\" and does not count toward a program's missing total; measured",
             "against battle_ovl1981, 34/34 of its hits here were intra-function, not missing",
             "functions -- treat it as noise unless the decompilation says otherwise.",
             "",
             "Programs with no `scratchpad/catalog/<name>_procs.json` dump show jal/tail-j/table",
             "hits only (no REA source, and the tail-j boundaries rely only on scanned",
             "prologues and the catalog's own rows).",
             f"Lists longer than {cap} are capped here; the full list (with sources and tags) is",
             f"in `{gaps_dir}/<program>.json`.", ""]
    for name, r in results.items():
        sources = r["sources"]
        checks = r["checks"]
        n = len(sources)
        nj = len(r["j_only"])
        if n == 0 and nj == 0:
            continue
        total_missing += n
        total_weak += nj
        addrs_sorted = sorted(sources)
        json.dump({
            "candidates": [
                {"address": f"0x{a:08x}", "sources": sorted(sources[a]), "check": checks.get(a, "?")}
                for a in addrs_sorted
            ],
            "j_only_weak": [f"0x{a:08x}" for a in r["j_only"]],
        }, open(f"{gaps_dir}/{name}.json", "w"), indent=1)
        lo, hi = r["range"]
        suffix = "" if r["has_procs"] else ", no procs dump"
        lines.append(f"### {name} ({n} + {nj} weak j-only, range 0x{lo:08x}-0x{hi:08x}, "
                      f"blocks {','.join(r['blocks'])}{suffix})")
        lines.append("")
        tag = lambda a: f"[{'+'.join(sorted(sources[a]))},{checks.get(a, '?')}]"  # noqa: E731
        shown = ", ".join(f"0x{a:08x}{tag(a)}" for a in addrs_sorted[:cap])
        more = f" (+{n - cap} more)" if n > cap else ""
        lines.append(f"- candidates ({n}): {shown}{more}")
        if r["j_only"]:
            shown_j = ", ".join(f"0x{a:08x}" for a in r["j_only"][:cap])
            more_j = f" (+{nj - cap} more)" if nj > cap else ""
            lines.append(f"- j only, weak ({nj}): {shown_j}{more_j}")
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
