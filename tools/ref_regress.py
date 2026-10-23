#!/usr/bin/env python3
"""ref_regress.py: regression suite over tools/refs/refs.csv.

Replays each reference's port side through the OWNING AGENT'S OWN script (never PCSX-Redux),
compares the result with that script's own compare tool, and judges it against refs.csv's
"expected" column. Never edits port code; never runs Redux.

Usage:
  tools/ref_regress.py [--build DIR] [--jobs N] [--out DIR]
                        (--all | --quick | --area AREA[,AREA..] | --id ID[,ID..])

  --build DIR   engine build directory (default: engine/build). Must contain
                shattered-stars built from the tree you want to check.
  --jobs N      max parallel port jobs (default 5, hard-capped at 5).
  --out DIR     where to write this run's scratch + summary (default:
                scratchpad/regress/<timestamp>).
  --quick       ~28 refs spanning every area and most of the battle cast; <10 minutes.
  --area X      only rows whose area column is X (enemy, world_spell, fx_battle, battle,
                menu, world_map, fx_legacy).
  --id A,B,C    only the named ref ids.
  --all         every row in refs.csv (including "manual"/"needs_owner" rows, which are
                reported as skipped, not run).

Exit code: nonzero if any ref's result is judged REGRESS.
"""
import argparse, csv, datetime, os, re, subprocess, sys
from concurrent.futures import ThreadPoolExecutor, as_completed

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REFS_CSV = os.path.join(ROOT, "tools/refs/refs.csv")

# A ~28-ref set covering every area, most of the battle cast (Bowman, Dias, Ernest, Chisato,
# Precis, Opera, Ashton), a handful of enemy families/modules (including known "spell not
# ported" and "console slowdown" exceptions, to make sure those don't silently start passing
# for the wrong reason) and the four party-class spell sweeps. No menu/world_map/fx_legacy ref
# has a confirmed automated recipe yet (see docs/handoff/tools.md), so none is in this set.
QUICK_IDS = [
    # battle: a stage-object ref, plus one move each of Bowman / Dias / Ernest
    "stage13", "bowman_l1", "dias_l1", "ernest_2_3",
    # fx_battle: one level-0 and one max-level move for each of the four party modules here
    "chis_b1", "chis_m1", "prec_b1", "prec_m1", "op_b1", "op_m1", "as_b1", "as_m1", "asv_b3",
    # enemy: five resident families, a module, and three documented exceptions (shot kind not
    # ported, console slowdown, spell module not ported) so a false "now passing" is visible
    "d10_3", "e10_3", "f10_3", "g10_3", "h10_3", "m107_3", "m524_3", "n420_3", "m538_3",
    # world_spell: the four party-class spell sweeps plus one boss spell check
    "iLv_3", "iCv_3", "iNa_3", "iNb_3", "k197_3", "m111_3",
]


def load_refs():
    with open(REFS_CSV, newline="") as f:
        return list(csv.DictReader(f))


def fmt(cmd, **kw):
    out = cmd
    for k, v in kw.items():
        out = out.replace("{" + k + "}", str(v))
    return out


# ---------------------------------------------------------------------------
# Area handlers. Each returns (ok_to_judge: bool, combined_text: str, note: str).
# They call the OWNING AGENT'S scripts directly (pcrun.sh, run_port.sh, port_pc.sh,
# cmpstage.sh, cmpstage_caster.sh) with SS_BIN / XDG_CONFIG_HOME overrides added for this
# suite (see docs/handoff/tools.md: the only edits made to those scripts, both additive
# and backward compatible).

def run(cmd, env, cwd=None, timeout=900):
    try:
        p = subprocess.run(cmd, shell=True, executable="/bin/bash", cwd=cwd or ROOT, env=env,
                            timeout=timeout, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            text=True, errors="replace")
        return p.returncode, p.stdout
    except subprocess.TimeoutExpired as e:
        return -1, (e.stdout or "") + "\n[ref_regress] TIMEOUT"


DISC1 = "/run/media/ironjade/7b066518-e6be-41f3-909e-76e7f05db6e4/Emulation/Roms/PS/Star Ocean - The Second Story (Disc 1)/Star Ocean - The Second Story (Disc 1).cue"


def job_env(build, out):
    env = dict(os.environ)
    env["SS_BIN"] = os.path.join(build, "shattered-stars")
    # A fresh, per-job XDG_CONFIG_HOME has no remembered disc path (unlike the agents'
    # persistent scratch XDG dirs, set up once by hand); pcrun.sh / port_pc.sh rely on that
    # memory instead of passing --disc1, so give every job SS_DISC1 explicitly.
    env.setdefault("SS_DISC1", DISC1)
    env["XDG_CONFIG_HOME"] = os.path.join(out, "xdg")
    env["SDL_VIDEO_DRIVER"] = "offscreen"
    env["SDL_AUDIO_DRIVER"] = "dummy"
    os.makedirs(env["XDG_CONFIG_HOME"], exist_ok=True)
    return env


def h_enemy(ref_id, build, out):
    m = re.match(r'^([a-z]+)(\d+)_3$', ref_id)
    tag, enc = m[1], m[2]
    env = job_env(build, out)
    env["NO_MASH"] = "1"  # every scratchpad/enemy/ref/*_3 reference was recorded idle (batch.sh)
    if tag == 't':
        env["TOUGH"] = "1"
    rc, run_out = run(f"scratchpad/enemy/run_port.sh {enc} 3 scratchpad/enemy/ref/{ref_id} {out}",
                       env, timeout=700)
    rc2, cmp_out = run(f"python3 scratchpad/enemy/cmp.py scratchpad/enemy/ref/{ref_id}/log.txt {out}/port.log",
                        env, timeout=120)
    return True, run_out + "\n" + cmp_out, ""


SWEEP_IDS = {'eA', 'eB', 'eC', 'eD', 'eE', 'eF', 'i9a', 'i9b', 'i10a', 'i10b', 'i11a', 'i11b',
             'i12', 'iLv', 'iCv', 'iNa', 'iNb', 'm9', 'm9b', 'x9', 'x9b', 'x11', 's9a', 's11'}
BOSS_IDS = {'k27': 27, 'k41': 41, 'k197': 197, 'k516': 516, 'm111': 111, 'm277': 277}


def h_world_spell(ref_id, build, out):
    ws = os.path.join(ROOT, "scratchpad/world/spell")
    m = re.match(r'^(.+)_3$', ref_id)
    tag = m[1]
    env = job_env(build, out)
    # run_port.sh hardcodes one shared SS_MEMCARD1 (redux/cards/empty_port.mcd) unless
    # overridden; under 5-way parallelism, 5 port processes read/write that one file at
    # once and the resulting log is corrupt (cmp.py then fails to parse it). Give this job
    # its own copy, same as fx_worker/battle/port_pc.sh already does for its own memcard.
    import shutil
    card = os.path.join(out, "empty_port.mcd")
    shutil.copyfile(os.path.join(ws, "redux/cards/empty_port.mcd"), card)
    env["SS_MEMCARD1"] = card
    if tag in SWEEP_IDS:
        cfg = os.path.join(ws, f"sweep_{tag}.sh")
        if not os.path.exists(cfg):
            return False, "", f"no {cfg}"
        logtxt = open(os.path.join(ws, "ref", ref_id, "log.txt"), errors="replace").read()
        ticks = re.findall(r'\[bw\] tick (\d+) (\d+)', logtxt)
        tick_map = ",".join(f"{int(a) - int(b)}:{b}" for a, b in ticks)
        sweep_for = 1500
        mfor = re.search(r'SWEEP_FOR=(\d+)', open(cfg).read())
        if mfor:
            sweep_for = int(mfor[1])
        env["SS_BATTLE_TICK_MAP"] = tick_map
        env["NO_MASH"] = "1"
        script = f"source sweep_{tag}.sh && ./run_port.sh 340 3 ref/i340_3 {out} {sweep_for + 50}"
        rc, run_out = run(script, env, cwd=ws, timeout=700)
    elif tag in BOSS_IDS:
        env["NO_MASH"] = "1"
        rc, run_out = run(f"./run_port.sh {BOSS_IDS[tag]} 3 ref/{ref_id} {out}", env, cwd=ws, timeout=700)
    else:
        return False, "", "no confirmed cfg script for this id"
    rc2, cmp_out = run(f"python3 {ws}/cmp.py {ws}/ref/{ref_id}/log.txt {out}/port.log", env, timeout=120)
    return True, run_out + "\n" + cmp_out, ""


FB_CHAR = {'as': 7, 'asv': 7, 'chis': 12, 'prec': 6, 'op': 9}
FB_MANUAL = {'as_b6', 'asv_b4d', 'chis_m4_box', 'op_b1_dense', 'op_b10_dense', 'op_m8_trail',
             'prec_b4_box', 'prec_b4_cam', 'precsmoke', 'talk_ko', 'talk_prov', 'talk_flee',
             'talk_ko2', 'talk_prov2', 'precsmoke', 'prec_b4_box'}


def h_fx_battle(ref_id, build, out):
    m = re.match(r'^(as|asv|chis|prec|op)_(b|m)(\d+)$', ref_id)
    if not m:
        return False, "", "no killer-move naming match; manual ref"
    prefix, level, mv = m[1], m[2], m[3]
    ch = FB_CHAR[prefix]
    uses = 999 if level == 'm' else 0
    fb = os.path.join(ROOT, "scratchpad/fx_worker/battle")
    env = job_env(build, out)
    env["OUTNAME"] = ref_id  # port_pc.sh writes to fb/port/<OUTNAME>
    if prefix == 'asv':
        env["EXTRA_POKE"] = "4:0xc=307/2"
    rc, run_out = run(f"{fb}/port_pc.sh {ref_id} {ch} {mv} {uses} 1500", env, timeout=700)
    port_log = os.path.join(fb, "port", ref_id, "log.txt")
    ref_log = os.path.join(fb, "redux/ref", ref_id, "log.txt")
    rc2, cmp_out = run(f"python3 scratchpad/battle/rngcmp.py {ref_log} {port_log}", env, timeout=60)
    return True, run_out + "\n" + cmp_out, ""


STAGE_PLAIN = {'stage13', 'stage16c', 'stage21', 'stage23', 'stage26', 'stage31', 'stage36',
               'stage36_long', 'stage41', 'stage51', 'stage8_light', 'stage8_shots'}
STAGE_CASTER = {'stage14c', 'caster35', 'caster35s'}


def _plus_state_dump(bdir, env, ref_id, port_log, o):
    """pcrun.sh / cmpstage*.sh only echo a terse one-line summary; append the full,
    state-tagged rngcmp.py dump (same tool, same ref/port logs) so judge() can tell a
    result-screen-only divergence from one still inside the move/stage itself."""
    rc, extra = run(f"python3 rngcmp.py ref/{ref_id}/log.txt {port_log}", env, cwd=bdir, timeout=60)
    return o + "\n" + extra


def h_battle(ref_id, build, out):
    env = job_env(build, out)
    bdir = os.path.join(ROOT, "scratchpad/battle")
    if ref_id in STAGE_PLAIN:
        rc, o = run(f"./cmpstage.sh {ref_id}", env, cwd=bdir, timeout=700)
        return True, _plus_state_dump(bdir, env, ref_id, f"port/{ref_id}/log.txt", o), ""
    if ref_id in STAGE_CASTER:
        globs = __import__("glob").glob(os.path.join(bdir, "ref", ref_id, "ram_battle_loop_*.bin"))
        if not globs:
            return False, "", "no ram_battle_loop_*.bin to read S from"
        s = re.search(r'ram_battle_loop_(\d+)\.bin', globs[0])[1]
        rc, o = run(f"./cmpstage_caster.sh {ref_id} {int(s)}", env, cwd=bdir, timeout=700)
        return True, _plus_state_dump(bdir, env, ref_id, f"port/{ref_id}/log.txt", o), ""
    m = re.match(r'^(bowman|dias|ernest)_l([0-9a])$', ref_id)
    m2 = re.match(r'^ernest_(2_3|58)$', ref_id)
    if m:
        char = {'bowman': 4, 'dias': 5, 'ernest': 10}[m[1]]
        byte = 10 if m[2] == 'a' else int(m[2])
        rc, o = run(f"./pcrun.sh {char} {byte} ref/{ref_id} {out}", env, cwd=bdir, timeout=700)
        return True, _plus_state_dump(bdir, env, ref_id, f"{out}/log.txt", o), ""
    if m2:
        byte = 1 if ref_id.endswith('2_3') else 5
        rc, o = run(f"./pcrun.sh 10 {byte} ref/{ref_id} {out}", env, cwd=bdir, timeout=700)
        return True, _plus_state_dump(bdir, env, ref_id, f"{out}/log.txt", o), ""
    return False, "", "no automated recipe (manual ref; see docs/handoff/battle.md)"


HANDLERS = {
    'enemy': h_enemy, 'world_spell': h_world_spell, 'fx_battle': h_fx_battle, 'battle': h_battle,
}


# ---------------------------------------------------------------------------
# Judging: pull a "first divergence at bNNN" (or "no divergence found") out of whatever the
# area's own compare tool printed, and check it against refs.csv's expected column.

def find_divergence(text):
    for pat in [r'first difference at b(\d+)', r'first mismatch at b\s*(\d+)',
                r'first b(\d+)']:
        m = re.search(pat, text)
        if m:
            return int(m[1])
    if re.search(r'first (difference|mismatch) at b\s*None', text):
        return None
    if re.search(r'equal throughout|no mismatch|rng.*same', text, re.I):
        return None
    return "unknown"


def divergence_in_result_screen(div, text):
    """True if the b<div> sample the compare tool printed falls at or after the first
    sample tagged "state 4" or higher. Killer-move-style references (pcrun.sh / port_pc.sh)
    keep recording well past the battle's own end, into the result screen and its exit
    fade, where a known, already-reported fade-timing difference (battle/result.cpp) shows
    up as an RNG divergence that has nothing to do with the move/stage under test. Battle
    state: 1 setup/combat, 2 victory pose?, 3-4 result screen, 5 exit (see rngcmp.py /
    cmp.py's own "state" column)."""
    if div is None:
        return False
    states = {int(b): int(s) for b, s in re.findall(r'b0*(\d+)[^\n]*?state (\d+)', text)}
    if not states:
        return False
    onset = min((b for b, s in states.items() if s >= 4), default=None)
    return onset is not None and div >= onset


def judge(expected, text, ref_id='', area=''):
    if expected in ('manual', 'needs_owner'):
        return 'SKIP', 'no automated recipe'
    div = find_divergence(text)
    lenient = 'slowdown' in expected or 'slow run' in expected or 'tick' in expected
    exp_b = None
    m = re.search(r'b(\d+)', expected)
    if m and ('equal_to:' in expected or 'rng_equal_to:' in expected):
        exp_b = int(m[1])
    if div == "unknown":
        return 'REVIEW', 'could not parse compare output'
    if area in ('battle', 'fx_battle') and exp_b is None and not lenient and divergence_in_result_screen(div, text):
        return 'PASS', (f'divergence at b{div} is in the result screen (state >= 4): a known, '
                         f'pre-existing fade-timing difference in battle/result.cpp, not scored here')
    if lenient:
        return 'PASS', f'divergence at b{div} (expected: console slowdown, not modelled, not a regression)' if div is not None else 'no divergence (expected console slowdown did not trigger this run)'
    if exp_b is None:
        # expected: equal straight through (plus maybe units/shots qualifiers we don't parse)
        if div is None:
            return 'PASS', 'no divergence, as expected'
        return 'REGRESS', f'new divergence at b{div} (reference log + docs say it should run to the exit)'
    # expected: a documented divergence point
    if div is None:
        return 'PASS', f'better than documented (ref ran clean; docs expected a split near b{exp_b})'
    if abs(div - exp_b) <= 30:
        return 'PASS', f'divergence at b{div}, matches documented b{exp_b} (within tolerance)'
    return 'REVIEW', f'divergence at b{div}, documented b{exp_b} differs by more than 30 frames'


def one(row, build, root_out, jobs_sem):
    ref_id, area, expected = row['id'], row['area'], row['expected']
    out = os.path.join(root_out, ref_id)
    os.makedirs(out, exist_ok=True)
    handler = HANDLERS.get(area)
    if handler is None:
        return ref_id, area, 'SKIP', 'no handler for this area', ''
    try:
        ok, text, note = handler(ref_id, build, out)
    except Exception as e:
        return ref_id, area, 'ERROR', f'{type(e).__name__}: {e}', ''
    if not ok:
        return ref_id, area, 'SKIP', note or 'not runnable', ''
    status, why = judge(expected, text, ref_id, area)
    return ref_id, area, status, why, text[-4000:]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--build', default=os.path.join(ROOT, 'engine/build'))
    ap.add_argument('--jobs', type=int, default=5)
    ap.add_argument('--out', default=None)
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument('--all', action='store_true')
    g.add_argument('--quick', action='store_true')
    g.add_argument('--area')
    g.add_argument('--id')
    args = ap.parse_args()

    build = os.path.abspath(args.build)
    if not os.path.exists(os.path.join(build, 'shattered-stars')):
        print(f"error: {build}/shattered-stars not found (build it first, or pass --build)", file=sys.stderr)
        return 2

    rows = load_refs()
    if args.quick:
        want = set(QUICK_IDS)
        rows = [r for r in rows if r['id'] in want]
    elif args.area:
        areas = set(args.area.split(','))
        rows = [r for r in rows if r['area'] in areas]
    elif args.id:
        want = set(args.id.split(','))
        rows = [r for r in rows if r['id'] in want]
    # --all: every row, unchanged

    jobs = min(max(1, args.jobs), 5)
    ts = datetime.datetime.now().strftime('%Y%m%d-%H%M%S')
    root_out = args.out or os.path.join(ROOT, 'scratchpad/regress', ts)
    os.makedirs(root_out, exist_ok=True)

    results = []
    with ThreadPoolExecutor(max_workers=jobs) as ex:
        futs = {ex.submit(one, row, build, root_out, None): row for row in rows}
        for fut in as_completed(futs):
            results.append(fut.result())

    by_status = {}
    for ref_id, area, status, why, _ in results:
        by_status.setdefault(status, []).append((ref_id, area, why))

    order = ['PASS', 'REGRESS', 'REVIEW', 'ERROR', 'SKIP']
    lines = [f"# Reference regression run {ts}", "", f"Build: `{build}`", f"Refs: {len(results)}", ""]
    for st in order:
        items = by_status.get(st, [])
        lines.append(f"## {st} ({len(items)})")
        for ref_id, area, why in sorted(items):
            lines.append(f"- `{ref_id}` ({area}): {why}")
        lines.append("")
    summary_path = os.path.join(root_out, 'summary.md')
    with open(summary_path, 'w', newline="\n") as f:
        f.write("\n".join(lines) + "\n")

    n = {st: len(by_status.get(st, [])) for st in order}
    total_line = (f"ref_regress: {n['PASS']} pass, {n['REGRESS']} regress, {n['REVIEW']} review, "
                  f"{n['ERROR']} error, {n['SKIP']} skip (of {len(results)}) -> {summary_path}")
    print(total_line)
    return 1 if n['REGRESS'] else 0


if __name__ == '__main__':
    sys.exit(main())
