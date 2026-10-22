#!/bin/bash
# WIP checkpoint: commits what the agents have on disk so a cut-off session cannot lose
# much work. If engine/build builds, everything changed is committed; if it does not
# (an agent mid-edit), only docs/ is (logs, catalog, notes: always safe to commit).
# Verified work is still committed separately as each agent reports it.
set -u
cd "$(dirname "$0")/.."
if [ -z "$(git status --porcelain)" ]; then
  echo "checkpoint: nothing changed"
  exit 0
fi
stamp=$(date +%H:%M)
areas=$(git status --porcelain | awk '{print $2}' | sed -E 's|^engine/src/game/([^/]+)/.*|\1|; s|^docs/handoff/([^.]+)\.md|\1 log|; s|^docs/.*|docs|; s|^engine/.*|engine|; s|^tools/.*|tools|' | sort -u | paste -sd, -)
if cmake --build engine/build -j8 > /dev/null 2>&1; then
  git add -A
  what="everything (builds)"
else
  git add -A docs
  what="docs only (the tree does not build right now)"
fi
if git diff --cached --quiet; then
  echo "checkpoint: nothing to commit ($what)"
  exit 0
fi
git commit -q -m "WIP checkpoint $stamp: $areas

Automatic 20-minute checkpoint of the agents' work on disk: $what.
Not yet verified unless a later commit says so.

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>" && echo "checkpoint: $(git log --oneline -1) [$what]"
