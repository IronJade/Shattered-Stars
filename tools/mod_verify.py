#!/usr/bin/env python3
"""Runtime verification of a mod package with the game and the discs (maintainer only;
docs/notes/mods.md section 8).

  SS_DISC1=<disc 1 .cue> mod_verify.py PACKAGE [--bin engine/build/shattered-stars]
      [--out scratchpad/modverify/<id>-<time>] [--jobs 3] [--frames 3600] [--serial]

PACKAGE is a package folder or .zip. Everything runs headless (offscreen video, dummy
audio) with scratch settings folders under --out (never the player's), no memory card, and
the package copied into a scratch mods folder, enabled through a scratch config.ini's
mod_order (the way a player's setup enables it).

1. The static check (tools/mod_check.py); errors stop here.
2. Maps: every map the mod's script lines touch, and the map of every shop it edits, run
   through the event sweep (tools/event_sweep.py: each trigger region, Private Action and
   NPC talk entry, battles stubbed), once with the mod and once without. A crash, a hang,
   a new timeout or game over, or more stand-in services with the mod fail; other
   differences (a door leading elsewhere) are listed.
3. Battles: for every enemy type the mod edits, its first encounter (docs/ap/battles.json)
   starts with SS_ENEMY_TRACE and must run its frames without a crash; item lines are
   applied when a battle loads the item table, so a mod with item lines gets a battle too.
4. Lines: each patch line must log as applied in some run. Skipped (its EXPECTED didn't
   match) fails; never reached is a warning.
5. report.md in --out, ready to paste into the pull request. Exit status 0 for a pass.
"""

import argparse
import datetime
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import mod_check  # noqa: E402

APPLIED = re.compile(r"\[mods\] (\S+): (.+) (0x[0-9a-f]+|0) -> (0x[0-9a-f]+|0)$")
SKIPPED = re.compile(r"\[mods\] (\S+): (.*)(skipped|not applied|nothing to do)$")
NOT_UNDERSTOOD = re.compile(r"\[mods\] (\S+): not understood")
ENEMY = re.compile(r"\[enemy\] .*\bt(\d+)\b(.*)$")


def git_commit():
    try:
        head = subprocess.run(["git", "rev-parse", "--short", "HEAD"], cwd=ROOT, capture_output=True, text=True,
                              check=True).stdout.strip()
        dirty = subprocess.run(["git", "status", "--porcelain", "--untracked-files=no"], cwd=ROOT,
                               capture_output=True, text=True).stdout.strip()
        return head + (" with local changes" if dirty else "")
    except (OSError, subprocess.CalledProcessError):
        return "unknown"


def write_config(xdg, mods_dir, order):
    folder = os.path.join(xdg, "shattered-stars")
    os.makedirs(folder, exist_ok=True)
    with open(os.path.join(folder, "config.ini"), "w") as f:
        f.write(f"mods={mods_dir}\nmod_order={order}\n")


def clean_env(xdg, disc1):
    env = {k: v for k, v in os.environ.items() if k not in ("SS_MODS_DIR", "SS_MOD_ORDER", "SS_MEMCARD1",
                                                            "SS_MEMCARD2", "SS_EVENT_SWEEP", "SS_LOAD_SAVE")}
    env.update({"XDG_CONFIG_HOME": xdg, "SS_DISC1": disc1, "SDL_VIDEO_DRIVER": "offscreen",
                "SDL_AUDIO_DRIVER": "dummy"})
    return env


def run_sweep(args, maps, out, mods_dir, order):
    """The event sweep over 'maps' with a config.ini enabling 'order'; (results, logs)."""
    os.makedirs(out, exist_ok=True)
    write_config(os.path.join(out, "xdg"), mods_dir, order)
    cmd = [sys.executable, os.path.join(HERE, "event_sweep.py"), "--bin", args.bin, "--disc1", args.disc1,
           "--out", out, "--maps", ",".join(str(m) for m in maps), "--jobs", str(args.jobs),
           "--frames", str(args.frames)]
    with open(os.path.join(out, "driver.log"), "w") as log:
        subprocess.run(cmd, env=clean_env(os.path.join(out, "xdg"), args.disc1), stdout=log, stderr=subprocess.STDOUT,
                       timeout=args.timeout, check=False)
    try:
        with open(os.path.join(out, "results.json")) as f:
            results = {int(k): v for k, v in json.load(f).items()}
    except (OSError, json.JSONDecodeError):
        results = {}
    logs = {}
    for m in maps:
        path = os.path.join(out, f"map_{m:03d}.log")
        logs[m] = open(path, errors="replace").read().splitlines() if os.path.exists(path) else []
    return results, logs


def run_battle(args, encounter, out, mods_dir, order):
    xdg = os.path.join(out, f"xdg_{encounter}")
    write_config(xdg, mods_dir, order)
    env = clean_env(xdg, args.disc1)
    env["SS_ENEMY_TRACE"] = "1"
    cmd = [args.bin, "--headless", "--no-memcard", "--frames", str(args.battle_frames), "--battle", str(encounter)]
    try:
        p = subprocess.run(cmd, env=env, stdin=subprocess.DEVNULL, capture_output=True, text=True, errors="replace",
                           timeout=600)
        code, text = p.returncode, p.stdout + p.stderr
    except subprocess.TimeoutExpired as e:
        code, text = "timeout", (e.stdout or b"").decode(errors="replace") if isinstance(e.stdout, bytes) else (e.stdout or "")
    with open(os.path.join(out, f"battle_{encounter}.log"), "w") as f:
        f.write(text)
    return code, text.splitlines()


def entry_summary(result):
    counts = {}
    for e in result.get("entries", []):
        r = e.get("result") or "?"
        key = "map change" if r.startswith("map ") else r
        counts[key] = counts.get(key, 0) + 1
    return ", ".join(f"{n} {k}" for k, n in sorted(counts.items())) or "no entries"


def compare_maps(with_mod, without):
    """(failures, differences) for one map's sweep against the baseline."""
    failures, diffs = [], []
    if with_mod is None:
        return ["the sweep didn't run"], []
    base = {e.get("n"): e for e in (without or {}).get("entries", [])}
    for e in with_mod.get("entries", []):
        r = e.get("result") or ""
        b = base.get(e.get("n"), {})
        br = b.get("result") or ""
        where = f"entry {e.get('n')} ({e.get('kind', '')}{e.get('index', '')})"
        if r.startswith("crash") or r == "hang":
            failures.append(f"{where}: {r}")
        elif r != br:
            if r in ("timeout", "game over") and br not in ("timeout", "game over"):
                failures.append(f"{where}: {r} with the mod, {br or 'not run'} without")
            else:
                diffs.append(f"{where}: {r} with the mod, {br or 'not run'} without")
        if (e.get("standins") or 0) > (b.get("standins") or 0):
            failures.append(f"{where}: {e.get('standins')} stand-in service(s) with the mod, {b.get('standins') or 0} "
                            "without")
    for note in with_mod.get("notes", []):
        if "crash" in note or "hang" in note:
            failures.append(note)
    return failures, diffs


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("package")
    ap.add_argument("--bin", default=os.path.join(ROOT, "engine", "build", "shattered-stars"))
    ap.add_argument("--disc1", default=os.environ.get("SS_DISC1", ""))
    ap.add_argument("--out", default=None)
    ap.add_argument("--jobs", type=int, default=3, help="game processes per sweep (two sweeps run at once)")
    ap.add_argument("--frames", type=int, default=3600, help="event sweep frames per entry")
    ap.add_argument("--battle-frames", type=int, default=1200)
    ap.add_argument("--timeout", type=int, default=7200, help="seconds per sweep")
    ap.add_argument("--serial", action="store_true",
                    help="one game process at a time (sweeps one after the other, --jobs 1; for a busy machine)")
    args = ap.parse_args(argv)
    if not args.disc1 or not os.path.exists(args.disc1):
        ap.error("set SS_DISC1 (or --disc1) to the disc 1 image")
    if not os.access(args.bin, os.X_OK):
        ap.error(f"{args.bin} isn't built")
    args.bin = os.path.abspath(args.bin)
    args.disc1 = os.path.abspath(args.disc1)
    args.jobs = 1 if args.serial else max(1, min(args.jobs, 3))

    # 1. The static check.
    tables = mod_check.Tables()
    check = mod_check.check_package(args.package, tables, mod_check.port_version())
    mod_id = check.mod_id or "mod"
    stamp = datetime.datetime.now(datetime.timezone.utc)
    out = os.path.abspath(args.out or os.path.join(ROOT, "scratchpad", "modverify",
                                                    f"{mod_id}-{stamp.strftime('%Y%m%d-%H%M%S')}"))
    os.makedirs(out, exist_ok=True)
    report = []
    files = mod_check.read_package(args.package, mod_check.Result(args.package)) or {}
    version = check.manifest.get("version", "?") if isinstance(check.manifest, dict) else "?"
    report += [f"## Mod verification: {mod_id} {version}", ""]
    report.append(f"- Package: `{os.path.relpath(os.path.abspath(args.package), ROOT)}`")
    for name in sorted(files):
        report.append(f"  - `{name}` sha256 `{hashlib.sha256(files[name]).hexdigest()}`")
    report.append(f"- Port {mod_check.port_version()} (commit {git_commit()}), "
                  f"disc 1 `{os.path.basename(args.disc1)}`")
    report.append(f"- Run {stamp.strftime('%Y-%m-%d %H:%M')} UTC by tools/mod_verify.py, headless, scratch settings "
                  f"in `{os.path.relpath(out, ROOT)}`")
    verdict_at = len(report)
    report += ["", "### Static check (tools/mod_check.py)", "",
               f"{check.count('error')} error(s), {check.count('warning')} warning(s), {check.count('declared')} "
               "declared."]
    for f in check.findings:
        reason = f" (declared: {f.reason})" if f.reason else ""
        report.append(f"- {f.level} `{f.rule}` {f.where()}: {f.message}{reason}")
    failures, warnings = [], []
    if check.count("error"):
        failures.append("the static check has errors")
        return finish(report, verdict_at, failures, warnings, out)

    # The package in a scratch mods folder.
    mods_dir = os.path.join(out, "mods")
    shutil.rmtree(mods_dir, ignore_errors=True)
    os.makedirs(os.path.join(mods_dir, mod_id))
    for name, data in files.items():
        with open(os.path.join(mods_dir, mod_id, name), "wb") as f:
            f.write(data)

    # What to run.
    field_json = json.load(open(os.path.join(ROOT, "docs", "ap", "field.json")))
    shop_maps = {}
    for site in field_json["shop_sites"]:
        shop_maps.setdefault(site["shop"], site["map"])
    maps = sorted({ln.target for ln in check.lines if ln.kind == "script"} |
                  {shop_maps[ln.target] for ln in check.lines if ln.kind == "shop" and ln.target in shop_maps})
    encounters = {}
    for ln in check.lines:
        if ln.kind == "enemy" and ln.target in tables.enemies:
            encounters.setdefault(tables.enemies[ln.target]["first_encounter"], set()).add(ln.target)
    if any(ln.kind == "item" for ln in check.lines) and not encounters:
        encounters[19] = set()  # any battle loads the item table

    # 2-3. Sweeps (with and without the mod, at once) and battles.
    sweeps = {}
    with ThreadPoolExecutor(max_workers=1 if args.serial else 2) as pool:
        if maps:
            jobs = {"mod": pool.submit(run_sweep, args, maps, os.path.join(out, "sweep_mod"), mods_dir, mod_id),
                    "base": pool.submit(run_sweep, args, maps, os.path.join(out, "sweep_base"), mods_dir, f"-{mod_id}")}
            sweeps = {k: j.result() for k, j in jobs.items()}
    battles = {}
    with ThreadPoolExecutor(max_workers=1 if args.serial else min(4, max(1, len(encounters)))) as pool:
        jobs = {enc: pool.submit(run_battle, args, enc, os.path.join(out, "battles"), mods_dir, mod_id)
                for enc in sorted(encounters)} if encounters else {}
        os.makedirs(os.path.join(out, "battles"), exist_ok=True)
        battles = {enc: j.result() for enc, j in jobs.items()}

    # 4. Lines: applied or skipped, anywhere.
    seen = {}  # label -> (result, where)
    def scan(lines, where):
        for text in lines:
            if (m := APPLIED.search(text)) and m.group(1) not in seen:
                seen[m.group(1)] = ("applied", where)
            elif (m := SKIPPED.search(text)):
                if seen.get(m.group(1), ("",))[0] != "applied":
                    seen[m.group(1)] = ("skipped: " + m.group(2).strip().rstrip(";").strip(), where)
            elif (m := NOT_UNDERSTOOD.search(text)):
                seen[m.group(1)] = ("not understood", where)
    if "mod" in sweeps:
        for m, lines in sweeps["mod"][1].items():
            scan(lines, f"map {m}")
    for enc, (_, lines) in battles.items():
        scan(lines, f"battle {enc}")
    report += ["", "### Patch lines", "", "| Line | Patch | Result | Seen in |", "|---|---|---|---|"]
    for ln in check.lines:
        label = f"{mod_id}/{ln.label}"
        result, where = seen.get(label, ("not reached", ""))
        report.append(f"| {ln.label} | `{ln.text.split('#')[0].strip()}` | {result} | {where} |")
        if result.startswith("skipped") or result == "not understood":
            failures.append(f"{ln.label} {result}")
        elif result == "not reached":
            warnings.append(f"{ln.label} wasn't reached by any run")

    if maps:
        report += ["", "### Maps (event sweep with the mod, against without)", "",
                   "| Map | With the mod | Without | Result |", "|---|---|---|---|"]
        res_mod, res_base = sweeps["mod"][0], sweeps["base"][0]
        diffs_all = []
        for m in maps:
            fails, diffs = compare_maps(res_mod.get(m), res_base.get(m))
            failures += [f"map {m}: {x}" for x in fails]
            diffs_all += [f"map {m}: {x}" for x in diffs]
            report.append(f"| {m} | {entry_summary(res_mod.get(m) or {})} | {entry_summary(res_base.get(m) or {})} | "
                          f"{'FAIL' if fails else 'ok'}{' (' + str(len(diffs)) + ' difference(s))' if diffs else ''} |")
        if diffs_all:
            report += ["", "Differences (expected for door edits; check they are what the mod says):", ""]
            report += [f"- {d}" for d in diffs_all]
    if battles:
        report += ["", "### Battles", "", "| Encounter | Enemy types edited | Exit | Stats traced |", "|---|---|---|---|"]
        for enc, (code, lines) in sorted(battles.items()):
            types = sorted(encounters[enc])
            traced = sorted({int(m.group(1)) for m in (ENEMY.search(t) for t in lines) if m})
            ok = code == 0 and any("stopped after" in t for t in lines)
            missing = [t for t in types if t not in traced]
            if not ok:
                failures.append(f"battle {enc}: exit {code}")
            if missing:
                failures.append(f"battle {enc}: enemy type(s) {missing} not seen")
            report.append(f"| {enc} | {', '.join(map(str, types)) or '(item table)'} | {code} | "
                          f"{', '.join(f't{t}' for t in traced) or '-'} |")
    return finish(report, verdict_at, failures, warnings, out)


def finish(report, verdict_at, failures, warnings, out):
    verdict = "FAIL" if failures else "PASS"
    head = [f"- **Verdict: {verdict}**" + (f" ({len(warnings)} warning(s))" if warnings and not failures else "")]
    head += [f"  - fails: {f}" for f in failures]
    head += [f"  - warning: {w}" for w in warnings]
    report[verdict_at:verdict_at] = head
    path = os.path.join(out, "report.md")
    with open(path, "w") as f:
        f.write("\n".join(report) + "\n")
    print(f"{verdict}: {len(failures)} failure(s), {len(warnings)} warning(s); report: {os.path.relpath(path, ROOT)}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
