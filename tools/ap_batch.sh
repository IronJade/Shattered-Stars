#!/bin/bash
# The APWorld's release workflow: many-seed generation, outside the unit tests (Archipelago's
# docs/tests.md, "Performance Considerations": random games belong to the release workflow, with
# minimal reproducers in the normal tests for what they find). docs/ap/apworld.md "Testing";
# docs/ap/release_checklist.md 2.5.
#
#   tools/ap_batch.sh SET OUT FIRST LAST
#
# SET: story_open (tools/ap_batch_players/story_open: two SO2 players, story 1 : open world 2, every
# option random, and a ChecksFinder player), hub (tools/ap_batch_players/hub: one SO2 player in hub
# mode, one hub / story / open world 3 : 1 : 1, and ChecksFinder), solo_noexpel
# (tools/ap_batch_players/solo_noexpel: one SO2 player alone, expel_progression off, open world /
# story / hub 3 : 1 : 1, the rest random; no other game's locations to take what this world can't
# place: so2.apworld 0.2.0's North City Pass failure hid behind ChecksFinder), or a directory of
# player YAMLs.
# For each seed FIRST..LAST, Generate.py (--spoiler 2) writes OUT/<seed>/ (gen.log, the zip,
# unzipped: the .apso2 files and the spoiler); "seed N exit RC apso2 K" lines go to OUT.txt. Then
# tools/ap_batch_check.py checks every .apso2 (OUT.check.txt). Exit status 0 only when every seed
# generated (exit 0) and the checker found no error.
#
# Environment: AP_DIR (the Archipelago checkout with worlds/so2 linked to apworld/so2; default
# scratchpad/ap/Archipelago, set up by tools/ap_test_server.sh), AP_PY (its Python; default
# $AP_DIR/../ap-venv/bin/python), SS_MEM_CAP_GB (each Python's address-space cap, default 4).
# Seeds run one at a time (one heavy process). About 3-5 s a seed.
#
# The 0.2.0 release ran both sets, seeds 1-96 (192 + 192 .apso2, 0 checker errors; docs/ap/apworld.md
# "Results"); 0.2.1 the three (solo_noexpel 95 of 96: seed 75, docs/handoff/ncpfix.md; 96 of 96
# each once guild checks and learn levels could hold progression, docs/handoff/skilllogic.md).
# The sets leave `bench` (0.2.1, on by default) out, so open-world players with character_items
# and hub players are bench players (version 4); benchworld (2026-10-27) ran them so, 96 of 96
# each, and with `bench: "false"` appended for the identity check (docs/handoff/benchworld.md). Moved here from scratchpad/apworld2/batch (run_batch.sh, check.py, players_sk,
# players_hub_sk) by aptests, 2026-10-26; the Generate.py call is the same, so a seed's files
# are byte-identical to the old script's.
set -u
REPO=$(cd "$(dirname "$0")/.." && pwd)
if [ $# -ne 4 ]; then
  echo "usage: tools/ap_batch.sh SET OUT FIRST LAST  (SET: story_open, hub, solo_noexpel or a players directory)" >&2
  exit 2
fi
SET=$1; OUT=$2; FIRST=$3; LAST=$4
case "$SET" in
  story_open|hub|solo_noexpel) PLAYERS=$REPO/tools/ap_batch_players/$SET ;;
  *) PLAYERS=$(cd "$SET" && pwd) || exit 2 ;;
esac
AP_DIR=$(cd "${AP_DIR:-$REPO/scratchpad/ap/Archipelago}" && pwd) || exit 2
AP_PY=${AP_PY:-$AP_DIR/../ap-venv/bin/python}
CAP=$(( ${SS_MEM_CAP_GB:-4} << 30 ))
[ -d "$AP_DIR/worlds/so2" ] || { echo "error: $AP_DIR/worlds/so2 missing (ln -sfn $REPO/apworld/so2 it)" >&2; exit 2; }
mkdir -p "$OUT" && OUT=$(cd "$OUT" && pwd)
rm -f "$OUT.txt" "$OUT.check.txt"
export SKIP_REQUIREMENTS_UPDATE=1
for s in $(seq "$FIRST" "$LAST"); do
  rm -rf "${OUT:?}/$s"; mkdir -p "$OUT/$s"
  (cd "$AP_DIR" && timeout 600 prlimit --as=$CAP "$AP_PY" Generate.py --player_files_path "$PLAYERS" \
     --outputpath "$OUT/$s" --seed "$s" --spoiler 2 < /dev/null > "$OUT/$s/gen.log" 2>&1)
  rc=$?
  (cd "$OUT/$s" && unzip -o -q AP_*.zip 2>/dev/null)
  echo "seed $s exit $rc apso2 $(ls "$OUT/$s"/*.apso2 2>/dev/null | wc -l)" >> "$OUT.txt"
done
(cd "$AP_DIR" && timeout 1800 prlimit --as=$CAP "$AP_PY" "$REPO/tools/ap_batch_check.py" "$OUT" 2>&1 \
   | grep -v pkg_resources > "$OUT.check.txt")
failed=$(awk '$4 != 0' "$OUT.txt" | wc -l)
summary=$(grep -m1 "seeds files" "$OUT.check.txt")
echo "$(wc -l < "$OUT.txt") seeds, $failed failed to generate; check: ${summary:-no summary (see $OUT.check.txt)}"
[ "$failed" -eq 0 ] && echo "$summary" | grep -q ", 0 with errors;"
