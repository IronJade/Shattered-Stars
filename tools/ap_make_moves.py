#!/usr/bin/env python3
"""Writes apworld/so2/moves.py (killer-move checks) from docs/ap/data/move_levels.csv
(the killer_prof agent's table, read off the disc). Run from the repo root."""
import csv

HEAD = '''"""Killer-move checks (killer_move_checks; docs/ap/apworld.md, "Killer moves"; docs/ap/seed_format.md,
"v2 fields"): one location per proficiency level of each killer move a learn table teaches (69
moves of Claude, Bowman, Dias, Precis, Ashton, Opera, Ernest and Chisato; the table below was
written by tools/ap_make_moves.py from docs/ap/data/move_levels.csv, the killer_prof agent's
reading of the disc: the learn tables in #1981, the move data in #2129).

A move's level is min(uses / div, cap), the use count kept at record + 0x8a + 2 * move (+1 each
time the move starts, at most 999; battle/party_states.cpp, the original's pc_begin_action), so
its levels are reached at div, 2 div, ... uses (the armed table: Claude's, Dias's and Ashton's
bare-handed tables differ for three moves; the check is the use count, see seed_format.md).

- A move with at most 3 levels (61 moves, 112 levels; most need 100-300 uses for their last):
  one location per level.
- A move that counts in fine steps (Bowman's Poison Pills 5 levels, Pillory 29, Secret Medicine
  999, Whirlwind Fist 20; Opera's Photon Prison 99, Cold Wind 5, Gravity Shell 9): three
  milestones, at the levels 100, 200 and 300 uses reach (capped at the reachable maximum), the
  use counts the tiered moves' levels mostly need (their first level's median is 100 uses,
  their last one's 200).
- Ashton's Dead Triangle has no levels (cap 0): none.
"""
MILESTONE_USES = (100, 200, 300)
TIERED_MAX = 3


def move_levels(move):
    """[(level, uses)]: the move's locations, each the level and the use count that reaches it."""
    _, _, _, _, _, div, top = move
    if top <= TIERED_MAX:
        return [(lv, lv * div) for lv in range(1, top + 1)]
    levels = sorted({min(uses // div, top) for uses in MILESTONE_USES})
    return [(lv, lv * div) for lv in levels]'''

rows = list(csv.DictReader(open("docs/ap/data/move_levels.csv")))
learned = [r for r in rows if r["learn_level"].isdigit()]
skipped = [f"{r['character']}'s {r['name']}" for r in rows if not r["learn_level"].isdigit()]
assert len(learned) == 69, len(learned)
out = []
for r in sorted(learned, key=lambda r: (int(r["char_id"]), int(r["learn_level"]))):
    out.append((r["character"], int(r["char_id"]), int(r["move_id"]), r["name"], int(r["learn_level"]), int(r["div"]),
                int(r["max_level_reachable"])))
lines = [HEAD.rstrip("\n"), "", "# (character, party id, move (the learn tables' number), name, learn level, div, reachable max level)",
         "MOVES = ["]
lines += [f"    {m!r}," for m in out]
lines += ["]", "", f"# Not in a learn table (left out): {', '.join(skipped)}.", ""]
open("apworld/so2/moves.py", "w", newline="\n").write("\n".join(lines))
print(len(out), "moves; left out:", skipped)
