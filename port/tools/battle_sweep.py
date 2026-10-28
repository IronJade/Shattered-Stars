#!/usr/bin/env python3
"""Encounter sweep: every battle of docs/apworld/battles.json run headless in the port.

Each encounter starts with `--battle ENC:STAGE`:
- The party is Claude, Rena, Celine and a fourth member that rotates through the other
  nine characters by encounter.
- The AI drives all four (config +0x181 = 1).
- HP and attack are scaled (SS_BATTLE_STRONG).
- MP is 200, and the fighters have killer moves on L1 / R1.
- Cross is pressed every 30 vblanks, so the result screen goes on.

A run ends at the battle's exit (SS_BATTLE_QUIT_AT_EXIT) or at the frame cap.

Per encounter the sweep records:
- the exit code or signal, and the outcome;
- the battle and fight frames;
- every `[port]` log_once line ("not ported yet" and the like);
- any AddressSanitizer / UBSan report.

It writes into OUT:
- `results.json` / `results.csv`: one row per run;
- `missing.json`: the `[port]` lines, ranked by how many encounters hit each;
- `summary.md`: totals, crashes and the ranked list;
- `logs/<run>.txt`: each run's stderr.

The stage comes from the field data:
- the scripted battle sites and preloads in battles.json;
- else the random-encounter groups (the field program's group table at 0x80074fe0, with
  the group -> stage pairs of docs/port/notes/encounter_maps.md);
- else stage 3.

On a PAL disc the field program is relinked (port/tools/region_map.py: every program loads
0x1b0 higher, and each image shifts in steps wherever a function or table changed size), so
the group table and its s16 lists sit at other offsets than on NA. `random_groups` reads the
disc's boot serial to find its region (tools/region_map.SERIALS) and, for Europe / France /
Germany, translates every address it reads through that region's field segments
(port/data/regions/<region>.inc kfield_seg, the same facts region.cpp's `translate` /
`to_na_address` use) instead of NA's raw offsets, so a PAL sweep picks the same stage per
encounter as NA's. The field image's FNV-1a 64 is checked against the .inc's recorded hash
first (a mismatch means the .inc is stale for this disc and the sweep falls back to no
random groups, same as a disc it can't read at all). NA itself is never translated.

`--stages LO-HI` instead runs one encounter (`--stage-encounter`, default 1) on every stage
in the range, which finds stage-only gaps (stage code blocks, stage ops, the stage-8
lightning).

Runs use XDG_CONFIG_HOME=OUT/xdg and --no-memcard, so the user's config and saves are never
touched. At most 5 jobs run at once. Every run is under a memory cap (tools/memcap.py:
SS_MEM_CAP_GB, default 4 GB); one that dies there is reported as a "memory cap" crash.

  port/tools/battle_sweep.py --out scratchpad/battle/sweep/main [--bin port/build-battle/shattered-stars]
      [--jobs 5] [--frames 15000] [--encounters 0-563] [--stages 0-63]
"""

import argparse
import bisect
import collections
import concurrent.futures
import csv
import json
import os
import re
import shutil
import signal
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # <section>/tools/ -> the repo
sys.path[:0] = [os.path.join(ROOT, "port", "tools"), os.path.join(ROOT, "tools")]  # region_map, then the shared tools/
from memcap import hit_cap, run_capped  # noqa: E402
import region_map as regionmap  # noqa: E402

OUTCOMES = {0: "victory", 1: "escaped", 2: "defeat", 3: "ran off", 4: "time up"}
# The fourth member, by encounter: the characters other than Claude, Rena and Celine.
ROTATION = [4, 5, 6, 7, 9, 10, 12, 8, 11]
NAMES = {1: "Claude", 2: "Rena", 3: "Celine", 4: "Bowman", 5: "Dias", 6: "Precis", 7: "Ashton",
         8: "Leon", 9: "Opera", 10: "Ernest", 11: "Noel", 12: "Chisato"}
# Killer moves per character module (bytes 1..n; casters have none).
KILLER_MOVES = {1: 10, 4: 10, 5: 10, 6: 10, 7: 10, 9: 10, 10: 8, 12: 8}
FIELD_PROGRAM = 3100
FIELD_BASE = 0x8002F810
GROUP_TABLE = 0x80074FE0
MISSING_RE = re.compile(r"not ported|not yet|not drawn|unknown|no handler|has no", re.I)
SANITIZER_RE = re.compile(r"ERROR: AddressSanitizer|ERROR: LeakSanitizer|runtime error:|"
                          r"SUMMARY: (Address|Undefined|Leak)Sanitizer")


def parse_range(text):
    out = []
    for part in text.split(","):
        part = part.strip()
        if not part:
            continue
        if "-" in part:
            a, b = part.split("-", 1)
            out.extend(range(int(a, 0), int(b, 0) + 1))
        else:
            out.append(int(part, 0))
    return out


def random_group_stages():
    """{group: [stages]} from docs/port/notes/encounter_maps.md (one entry per table row)."""
    path = os.path.join(ROOT, "docs", "port", "notes", "encounter_maps.md")
    stages = collections.defaultdict(list)
    if not os.path.exists(path):
        return stages
    row = re.compile(r"^\|\s*[\d\s,-]+\|\s*\d+\s*\|\s*(\d+)\s*\|\s*(\d+)\s*\|")
    for line in open(path, encoding="utf-8"):
        m = row.match(line)
        if m:
            stages[int(m.group(1))].append(int(m.group(2)))
    return stages


def fnv1a64(data):
    """region.cpp fnv1a64(): the field image's identity, to catch a stale .inc."""
    h = 0xCBF29CE484222325
    for b in data:
        h = ((h ^ b) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


_FIELD_MAP_CACHE = {}


def field_segment_map(region):
    """(na_base, rg_base, na_size, rg_size, rg_fnv, segs) of the field image from
    port/data/regions/<region>.inc (segs: [(na_off, size, delta)], region.cpp Segment)."""
    if region in _FIELD_MAP_CACHE:
        return _FIELD_MAP_CACHE[region]
    path = os.path.join(ROOT, "port", "data", "regions", f"{region}.inc")
    text = open(path, encoding="utf-8").read()
    m = re.search(r"constexpr Segment kfield_seg\[\] = \{(.*?)\n\};", text, re.S)
    if not m:
        raise ValueError(f"{path}: no kfield_seg[] (stage fix needs the field image's map)")
    segs = [(int(a, 16), int(b, 16), int(c)) for a, b, c in
            re.findall(r"\{\s*(0x[0-9a-fA-F]+)\s*,\s*(0x[0-9a-fA-F]+)\s*,\s*(-?\d+)\s*\}", m.group(1))]
    m2 = re.search(
        r"\{ImageKind::kEntry,\s*3100,\s*0,\s*(0x[0-9a-fA-F]+)u,\s*(0x[0-9a-fA-F]+)u,\s*"
        r"(0x[0-9a-fA-F]+)u,\s*(0x[0-9a-fA-F]+)u,\s*0x[0-9a-fA-F]+ull,\s*(0x[0-9a-fA-F]+)ull,", text)
    if not m2:
        raise ValueError(f"{path}: no field entry in kImages[] (stage fix needs its load bases / hash)")
    na_base, rg_base, na_size, rg_size, rg_fnv = (int(x, 16) for x in m2.groups())
    out = (na_base, rg_base, na_size, rg_size, rg_fnv, segs)
    _FIELD_MAP_CACHE[region] = out
    return out


def translate_off(segs, na_off):
    """region.cpp translate(): NA byte offset -> this image's byte offset, or None."""
    starts = [s[0] for s in segs]
    i = bisect.bisect_right(starts, na_off) - 1
    if i < 0:
        return None
    off, size, delta = segs[i]
    if na_off - off >= size:
        return None
    return na_off + delta


def to_na_off(segs, region_off):
    """region.cpp to_na_address(): this image's byte offset of a pointer's target -> its NA
    byte offset, or None. Segments' region-side starts rise with their NA ones (region.cpp)."""
    starts = [s[0] + s[2] for s in segs]
    i = bisect.bisect_right(starts, region_off) - 1
    if i < 0:
        return None
    off, size, delta = segs[i]
    if region_off - (off + delta) >= size:
        return None
    return region_off - delta


def region_key_of(disc):
    """The disc's region key ('eu' / 'fr' / 'de'), or None (NA, or not one of those)."""
    region, _disc_no = regionmap.SERIALS.get(regionmap.boot_name(disc), (None, 0))
    return region if region in ("eu", "fr", "de") else None


def random_groups(disc_path):
    """{group: [encounters]} from the field program's 0-terminated s16 lists.

    On NA these are read at their raw NA offsets, as the field program loads them. A PAL
    disc's field program is relinked (region_key_of / field_segment_map, the module
    docstring): every address here is translated through the region's field segments first,
    so the group table and the lists it points to (self-pointers into the same image, kept
    as region addresses on disc until the port's region::prepare rewrites them at load) read
    the same groups as NA.
    """
    import struct
    import so2_mod
    from so2_archive import unpack_block
    disc = so2_mod.Disc(disc_path)
    img = bytes(unpack_block(disc.entry(FIELD_PROGRAM), 0))

    segs = rg_base = None
    region = region_key_of(disc)
    if region:
        na_base, rg_base, na_size, rg_size, rg_fnv, segs = field_segment_map(region)
        if len(img) != rg_size or fnv1a64(img) != rg_fnv:
            raise ValueError(f"{region}.inc's field map doesn't match this disc's field program "
                              f"(size {len(img)} vs {rg_size}): stale .inc or wrong disc")

    def off(a):
        na_off = a - FIELD_BASE
        return na_off if segs is None else translate_off(segs, na_off)

    def r32(a):
        o = off(a)
        if o is None or not 0 <= o <= len(img) - 4:
            return None
        return struct.unpack_from("<I", img, o)[0]

    def r16(a):
        o = off(a)
        if o is None or not 0 <= o <= len(img) - 2:
            return 0
        return struct.unpack_from("<h", img, o)[0]

    groups = {}
    for g in range(1, 64):
        raw = r32(GROUP_TABLE + g * 4)
        if raw is None:
            break
        if segs is None:
            p = raw
        else:
            # raw is a region address (on-disc field self-pointers hold the region's own
            # addresses until region::prepare patches them): convert to NA, like the port.
            na_off2 = to_na_off(segs, raw - rg_base)
            p = FIELD_BASE + na_off2 if na_off2 is not None else None
        if p is None or not FIELD_BASE <= p < FIELD_BASE + len(img):
            break
        encs = []
        while r16(p) != 0 and len(encs) < 256:
            encs.append(r16(p))
            p += 2
        groups[g] = encs
    return groups


def stage_choices(data, disc_path):
    """{encounter: (stage, source)}: scripted sites first, then random groups."""
    votes = collections.defaultdict(collections.Counter)
    for s in data.get("scripted_battles", []) + data.get("battle_preloads", []):
        if isinstance(s.get("encounter"), int) and isinstance(s.get("stage"), int):
            votes[s["encounter"]][("scripted", s["stage"])] += 1
    try:
        groups = random_groups(disc_path) if disc_path else {}
    except (OSError, SystemExit, ValueError) as e:  # no disc: scripted stages only
        print(f"random groups not read: {e}", file=sys.stderr)
        groups = {}
    group_stages = random_group_stages()
    for g, encs in groups.items():
        for enc in set(encs):
            for st in group_stages.get(g, []):
                votes[enc][("random", st)] += 1
    out = {}
    for enc, c in votes.items():
        scripted = [(n, k) for k, n in c.items() if k[0] == "scripted"]
        pool = scripted if scripted else [(n, k) for k, n in c.items()]
        n, (source, stage) = max(pool, key=lambda t: (t[0], -t[1][1]))
        out[enc] = (stage, source)
    return out


def party_for(enc):
    fourth = ROTATION[enc % len(ROTATION)]
    party = [1, 2, 3, fourth]
    pokes = ["2:0x181=1/1"]  # the AI drives every member
    for slot, cid in enumerate(party):
        base = slot * 0x60
        pokes += [f"3:{base + 0x20:#x}=200/2", f"3:{base + 0x22:#x}=200/2", f"3:{base + 0x24:#x}=200/2"]
        n = KILLER_MOVES.get(cid)
        if n:
            rec = slot * 0xD0
            l1 = 1 + enc % n
            r1 = 1 + (enc + n // 2) % n
            pokes += [f"4:{rec + 0xCC:#x}={l1:#x}/1", f"4:{rec + 0xCD:#x}={r1:#x}/1"]
    return party, pokes


def mash_input(frames, every=30):
    return ",".join(f"{v}:cross,{v + 4}:none" for v in range(every, frames, every))


def run_one(job, args, out_dir):
    name, enc, stage = job["name"], job["encounter"], job["stage"]
    party, pokes = party_for(enc)
    env = dict(os.environ)
    env.update({
        "XDG_CONFIG_HOME": os.path.join(out_dir, "xdg"),
        "SDL_VIDEO_DRIVER": "offscreen",
        "SDL_AUDIO_DRIVER": "dummy",
        "SS_BATTLE_QUIT_AT_EXIT": "1",
        "SS_BATTLE_PARTY": ",".join(map(str, party)),
        "SS_BATTLE_STRONG": args.strong,
        "SS_BATTLE_POKE": ",".join(pokes),
        **({"SS_BATTLE_GOD": "1"} if args.god else {}),
        "ASAN_OPTIONS": os.environ.get("ASAN_OPTIONS", "detect_leaks=0:abort_on_error=0:halt_on_error=1"),
        "UBSAN_OPTIONS": os.environ.get("UBSAN_OPTIONS", "print_stacktrace=1"),
    })
    for k in ("SS_BATTLE_RAM", "SS_BATTLE_DUMP", "SS_BATTLE_TICKS", "SS_ENEMY_TRACE", "SS_BATTLE_RNG", "SS_BATTLE_COPY"):
        env.pop(k, None)
    cmd = [args.bin, "--headless", "--no-memcard", "--battle", f"{enc}:{stage}",
           "--frames", str(args.frames), "--input", mash_input(args.frames)]
    if args.disc1:
        cmd[1:1] = ["--disc1", args.disc1]
    if args.disc2:
        cmd[1:1] = ["--disc2", args.disc2]
    log_path = os.path.join(out_dir, "logs", f"{name}.txt")
    t0 = time.time()
    with open(log_path, "wb") as log:
        # Under the memory cap (tools/memcap.py): a run that dies there is a "memory cap" crash.
        rc, timed_out, at_cap = run_capped(cmd, args.bin, args.timeout, env=env, stdout=log, stderr=subprocess.STDOUT)
        if timed_out:
            rc = "timeout"
    secs = round(time.time() - t0, 1)
    text = open(log_path, "rb").read().decode("latin-1")
    row = dict(job)
    row.update({"party": "/".join(NAMES[c] for c in party), "rc": rc, "seconds": secs,
                "outcome": "", "battle_frames": "", "fight_frames": "", "vblanks": "",
                "port_lines": [], "sanitizer": [], "crash": ""})
    m = re.search(r"\[battle\] exit: encounter (-?\d+) outcome (\d+) battle frames (-?\d+) fight frames (-?\d+)", text)
    if m:
        row["outcome"] = OUTCOMES.get(int(m.group(2)), m.group(2))
        row["battle_frames"], row["fight_frames"] = int(m.group(3)), int(m.group(4))
    m = re.search(r"stopped after (\d+) vblanks", text)
    if m:
        row["vblanks"] = int(m.group(1))
    if not row["outcome"] and rc == 0:
        row["outcome"] = "cap"
    seen = []
    for line in text.splitlines():
        if line.startswith("[port] "):
            msg = line[7:].strip()
            if msg not in seen:
                seen.append(msg)
    row["port_lines"] = seen
    san = []
    for line in text.splitlines():
        if SANITIZER_RE.search(line):
            san.append(line.strip()[:300])
    row["sanitizer"] = san[:8]
    if rc == "timeout":
        row["crash"] = "timeout"
    elif isinstance(rc, int) and (at_cap or hit_cap(rc, text)):
        row["crash"] = "memory cap"
    elif isinstance(rc, int) and rc < 0:
        try:
            row["crash"] = signal.Signals(-rc).name
        except ValueError:
            row["crash"] = f"signal {-rc}"
    elif isinstance(rc, int) and rc != 0:
        row["crash"] = f"exit {rc}"
    if san and not row["crash"]:
        row["crash"] = "sanitizer"
    if row["crash"]:
        # The last lines before the end: the place it stopped.
        tail = [ln for ln in text.splitlines() if ln.strip()][-6:]
        row["tail"] = tail
    return row


def is_missing(msg):
    return bool(MISSING_RE.search(msg))


def summarize(rows, data, out_dir, args):
    bosses = set(int(e) for e in data.get("boss_encounters", []))
    finals = set(int(e) for e in data.get("final_boss_encounters", []))
    by_msg = collections.defaultdict(list)
    for r in rows:
        for msg in r["port_lines"]:
            by_msg[msg].append(r["encounter"] if not args.stages else r["stage"])
    ranked = sorted(by_msg.items(), key=lambda kv: (-len(set(kv[1])), kv[0]))
    missing = [{"line": msg, "missing": is_missing(msg), "count": len(set(hits)),
                "bosses": sorted(set(h for h in hits if h in bosses)) if not args.stages else [],
                "hits": sorted(set(hits))} for msg, hits in ranked]
    with open(os.path.join(out_dir, "missing.json"), "w") as f:
        json.dump(missing, f, indent=1)
    outcomes = collections.Counter(r["outcome"] or "-" for r in rows)
    crashes = [r for r in rows if r["crash"]]
    md = [f"# Battle sweep: {os.path.basename(out_dir)}", "",
          f"- Binary: `{os.path.relpath(args.bin_source, ROOT)}`, frame cap {args.frames}, party HP:ATK:DEF x{args.strong}"
          + (", HP refilled every frame (--god)." if args.god else "."),
          f"- Runs: {len(rows)}; outcomes: " + ", ".join(f"{k} {v}" for k, v in sorted(outcomes.items())) + ".",
          f"- Crashes / sanitizer reports / timeouts: {len(crashes)}.", ""]
    if crashes:
        md += ["## Crashes", "", "| Run | Stage | Boss | Party | Crash | Last lines |", "|---|---|---|---|---|---|"]
        for r in crashes:
            boss = "final" if r["encounter"] in finals else ("yes" if r["encounter"] in bosses else "")
            last = " / ".join((r.get("sanitizer") or r.get("tail") or [])[:3]).replace("|", "\\|")[:400]
            md.append(f"| {r['name']} | {r['stage']} | {boss} | {r['party']} | {r['crash']} | {last} |")
        md.append("")
    md += ["## Missing pieces, by encounters hit", "",
           "| Encounters | Bosses | Line | Examples |", "|---|---|---|---|"]
    for m in missing:
        if not m["missing"]:
            continue
        ex = ", ".join(map(str, m["hits"][:8])) + (" ..." if len(m["hits"]) > 8 else "")
        md.append(f"| {m['count']} | {len(m['bosses'])} | {m['line'].replace('|', '/')} | {ex} |")
    md += ["", "## Other [port] lines", "", "| Encounters | Line |", "|---|---|"]
    for m in missing:
        if m["missing"]:
            continue
        md.append(f"| {m['count']} | {m['line'].replace('|', '/')} |")
    with open(os.path.join(out_dir, "summary.md"), "w") as f:
        f.write("\n".join(md) + "\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", required=True)
    ap.add_argument("--bin", default=os.path.join(ROOT, "port", "build-battle", "shattered-stars"))
    ap.add_argument("--battles", default=os.path.join(ROOT, "docs", "apworld", "battles.json"))
    ap.add_argument("--jobs", type=int, default=5)
    ap.add_argument("--frames", type=int, default=15000)
    ap.add_argument("--timeout", type=int, default=1800, help="seconds per run")
    ap.add_argument("--strong", default="20:8:4", help="SS_BATTLE_STRONG (HP:ATK:DEF factors, HP capped at 9999)")
    ap.add_argument("--god", action="store_true", help="SS_BATTLE_GOD: the party's HP refilled every frame")
    ap.add_argument("--encounters", default="", help="e.g. 0-563 or 1,35,191 (default: all)")
    ap.add_argument("--stages", default="", help="stage pass: LO-HI, one encounter on each stage")
    ap.add_argument("--stage-encounter", type=int, default=1)
    ap.add_argument("--disc1", default=os.environ.get("SS_DISC1", ""))
    ap.add_argument("--disc2", default=os.environ.get("SS_DISC2", ""))
    args = ap.parse_args()
    if not args.disc1:
        ap.error("no disc image: give --disc1 (and --disc2) or set SS_DISC1 / SS_DISC2")
    args.jobs = max(1, min(args.jobs, 5))
    args.bin = os.path.abspath(args.bin)
    out_dir = os.path.abspath(args.out)
    os.makedirs(os.path.join(out_dir, "logs"), exist_ok=True)
    os.makedirs(os.path.join(out_dir, "xdg"), exist_ok=True)
    # A copy of the binary, so a rebuild during the sweep does not change it.
    snapshot = os.path.join(out_dir, os.path.basename(args.bin))
    if os.path.abspath(snapshot) != args.bin:
        shutil.copy2(args.bin, snapshot)
        args.bin_source, args.bin = args.bin, snapshot
    else:
        args.bin_source = args.bin
    data = json.load(open(args.battles))
    bosses = set(int(e) for e in data.get("boss_encounters", []))
    jobs = []
    if args.stages:
        for st in parse_range(args.stages):
            enc = args.stage_encounter
            jobs.append({"name": f"stage{st:03d}", "encounter": enc, "stage": st, "stage_source": "pass",
                         "boss": enc in bosses, "members": ""})
    else:
        stages = stage_choices(data, args.disc1)
        encs = sorted(int(k) for k in data["encounters"])
        if args.encounters:
            want = set(parse_range(args.encounters))
            encs = [e for e in encs if e in want]
        for enc in encs:
            e = data["encounters"][str(enc)]
            stage, source = stages.get(enc, (3, "default"))
            jobs.append({"name": f"enc{enc:03d}", "encounter": enc, "stage": stage, "stage_source": source,
                         "boss": bool(e.get("boss")),
                         "members": "/".join(m["name"] for m in e.get("members", []))})
    print(f"{len(jobs)} runs, {args.jobs} at a time, cap {args.frames} vblanks", file=sys.stderr)
    rows = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futs = {pool.submit(run_one, j, args, out_dir): j for j in jobs}
        for n, fut in enumerate(concurrent.futures.as_completed(futs), 1):
            r = fut.result()
            rows.append(r)
            miss = sum(1 for m in r["port_lines"] if is_missing(m))
            print(f"[{n}/{len(jobs)}] {r['name']} stage {r['stage']}: {r['outcome'] or '-'} "
                  f"{r['battle_frames']} {r['crash']} ({miss} missing, {r['seconds']} s)", file=sys.stderr, flush=True)
    rows.sort(key=lambda r: (r["encounter"], r["stage"]))
    with open(os.path.join(out_dir, "results.json"), "w") as f:
        json.dump(rows, f, indent=1)
    cols = ["name", "encounter", "stage", "stage_source", "boss", "members", "party", "outcome",
            "battle_frames", "fight_frames", "vblanks", "rc", "crash", "seconds"]
    with open(os.path.join(out_dir, "results.csv"), "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(cols + ["missing", "sanitizer"])
        for r in rows:
            w.writerow([r[c] for c in cols] + [" | ".join(m for m in r["port_lines"] if is_missing(m)),
                                               " | ".join(r["sanitizer"])])
    summarize(rows, data, out_dir, args)
    print(f"wrote {out_dir}/results.json, results.csv, missing.json, summary.md", file=sys.stderr)


if __name__ == "__main__":
    main()
