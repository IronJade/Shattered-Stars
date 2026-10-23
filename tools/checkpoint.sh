#!/bin/bash
# Hourly checkpoint: commits what the agents have on disk. If engine/build builds, the
# suites run (ss_tests; ss_game_tests and the smoke script when SS_DISC1 is set) and
# everything changed is committed with the results; if it does not build (an agent
# mid-edit), only docs/ is (logs, catalog, notes: always safe to commit).
#   tools/checkpoint.sh [NOTES_FILE]   NOTES_FILE: what was verified since the last commit
set -u
cd "$(dirname "$0")/.."
notes=""
[ $# -ge 1 ] && [ -f "$1" ] && notes=$(cat "$1")
if [ -z "$(git status --porcelain)" ]; then
  echo "checkpoint: nothing changed"
  exit 0
fi
stamp=$(date +%H:%M)
areas=$(git status --porcelain | awk '{print $2}' | sed -E 's|^engine/src/game/([^/]+)/.*|\1|; s|^docs/handoff/([^.]+)\.md|\1 log|; s|^docs/.*|docs|; s|^engine/.*|engine|; s|^tools/.*|tools|' | sort -u | paste -sd, -)
if cmake --build engine/build -j8 > /dev/null 2>&1; then
  t1=$(engine/build/ss_tests 2>/dev/null | tail -1)
  if [ -n "${SS_DISC1:-}" ]; then
    t2=$(stdbuf -oL engine/build/ss_game_tests 2>/dev/null | tail -1)
    smoke_log=$(mktemp -p scratchpad smoke.XXXX 2>/dev/null || echo scratchpad/smoke.log)
    tools/smoke_field_events.sh > "$smoke_log" 2>&1
    t3="$(grep -c '^PASS' "$smoke_log") PASS, $(grep -c '^FAIL' "$smoke_log") FAIL"
    rm -f "$smoke_log"
  else
    t2="(not run: SS_DISC1 unset)"; t3="(not run)"
  fi
  # Reference regression suite (tools/refs/refs.csv), quick set: a failure here is reported
  # like the other suites, never blocks the commit.
  t4=$(timeout 600 python3 tools/ref_regress.py --quick --jobs 5 2>&1 | tail -1)
  git add -A
  what="everything (builds). ss_tests: $t1; ss_game_tests: $t2; smoke: $t3; $t4"
else
  git add -A docs
  what="docs only (the tree does not build right now)"
fi
if git diff --cached --quiet; then
  echo "checkpoint: nothing to commit ($what)"
  exit 0
fi
msg="Checkpoint $stamp: $areas

Hourly commit of the agents' work on disk: $what."
[ -n "$notes" ] && msg="$msg

Verified since the last checkpoint:
$notes"
msg="$msg

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>"
git commit -q -m "$msg" && echo "checkpoint: $(git log --oneline -1) [$what]"
