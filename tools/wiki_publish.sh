#!/usr/bin/env bash
# Publish the repo's wiki/ folder to the GitHub wiki (IronJade/Shattered-Stars.wiki).
#
# The wiki's pages live in this repo under wiki/ (one .md per page, plus _Sidebar.md and
# _Footer.md) and are edited with the code they describe. The GitHub wiki is a separate git
# repo; this script copies wiki/ into a checkout of it, shows what changed, and commits and
# pushes only when asked to.
#
#   tools/wiki_publish.sh [--wiki-dir DIR] [--dry-run] [--push [-m MESSAGE]]
#
#   (no flag)        sync wiki/ into the checkout (pages added, changed and removed) and show
#                    `git status` and the diff there; nothing is committed
#   --dry-run        only list the pages that would change; the checkout is not touched
#   --push           also commit the checkout and push it (refuses while wiki/ has uncommitted
#                    changes, so the published wiki is always a commit of this repo)
#   -m MESSAGE       the wiki commit's message (default: "Sync from Shattered-Stars <sha>")
#   --wiki-dir DIR   the checkout (default: scratchpad/wiki; clone one with
#                    git clone https://github.com/IronJade/Shattered-Stars.wiki.git scratchpad/wiki)
#
# Publishing is a release step (docs/ap/release_checklist.md): run --push only with the
# maintainer's go-ahead.
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
SRC="$ROOT/wiki"
DIR="$ROOT/scratchpad/wiki"
PUSH=0
DRY=0
MSG=""

while [ $# -gt 0 ]; do
  case "$1" in
    --wiki-dir) DIR=$2; shift 2 ;;
    --push) PUSH=1; shift ;;
    --dry-run) DRY=1; shift ;;
    -m) MSG=$2; shift 2 ;;
    -h|--help) sed -n '2,22p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "wiki_publish: unknown argument: $1" >&2; exit 2 ;;
  esac
done

[ -d "$SRC" ] || { echo "wiki_publish: no wiki/ folder at $SRC" >&2; exit 1; }
git -C "$DIR" rev-parse --git-dir >/dev/null 2>&1 \
  || { echo "wiki_publish: $DIR is not a git checkout of the wiki" >&2; exit 1; }
url=$(git -C "$DIR" remote get-url origin 2>/dev/null || true)
case "$url" in
  *Shattered-Stars.wiki*) ;;
  *) echo "wiki_publish: warning: $DIR's origin is '$url', not the Shattered-Stars wiki" >&2 ;;
esac

RSYNC=(rsync -a --delete --checksum --exclude=.git --exclude='*.swp' "$SRC/" "$DIR/")

if [ "$DRY" = 1 ]; then
  echo "== pages that would change in $DIR (dry run)"
  "${RSYNC[@]}" -n --itemize-changes | grep -E '^(>f|\*deleting)' || echo "(none)"
  exit 0
fi

if [ "$PUSH" = 1 ]; then
  if [ -n "$(git -C "$ROOT" status --porcelain -- wiki)" ]; then
    echo "wiki_publish: wiki/ has uncommitted changes; commit them first" >&2
    git -C "$ROOT" status --short -- wiki >&2
    exit 1
  fi
fi

"${RSYNC[@]}"

echo "== $DIR after the sync"
git -C "$DIR" status --short
if [ -z "$(git -C "$DIR" status --porcelain)" ]; then
  echo "(the wiki already matches wiki/)"
  exit 0
fi
git -C "$DIR" add -A -N .          # show new pages in the diff too
git -C "$DIR" --no-pager diff --stat
git -C "$DIR" --no-pager diff

if [ "$PUSH" = 1 ]; then
  sha=$(git -C "$ROOT" rev-parse --short HEAD)
  [ -n "$MSG" ] || MSG="Sync from Shattered-Stars $sha"
  git -C "$DIR" add -A
  git -C "$DIR" commit -q -m "$MSG"
  git -C "$DIR" push origin HEAD
  echo "== pushed: $(git -C "$DIR" log --oneline -1)"
else
  echo "== not committed (run again with --push to commit and push)"
fi
