#!/usr/bin/env python3
"""Archipelago survey: counts chests (by the chest detector of so2_mod.py) and the script
instructions that give items or Fol, add party members, start battles or change maps, on
every field map. Usage: apworld/tools/ap_survey.py DISC1 [DISC2]"""
sys.path[:0] = [os.path.dirname(os.path.abspath(__file__)),  # this folder, then the shared tools/
                os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools")]
import so2_mod
discs = {n + 1: p for n, p in enumerate(sys.argv[1:3])}
want = re.compile(r'^(party_add|B_60|give_item|add_money|battle\b|battle\(|map_transfer|B_6c|B_6d|A_0?2d|ext_d4)')
for d, path in discs.items():
    disc = so2_mod.Disc(path)
    maps = chests = 0
    ops = collections.Counter(); per_map_chests = {}
    chest_items = chest_fol = 0
    for m in range(0, 827):
        try:
            blob = disc.map_script(m)
        except SystemExit:
            continue
        maps += 1
        try:
            ins = so2_mod.instructions(blob)
        except Exception:
            continue
        for i in ins:
            name = re.split(r'[ (<]', i[2], maxsplit=1)[0]
            ops[name] += 1
        cs = so2_mod.chests(blob)
        chests += len(cs)
        for flag, h, v in cs:
            val = so2_mod.push_value(v)
            if val is not None and val < 0: chest_fol += 1
            else: chest_items += 1
    keys = [k for k in ops if re.match(r'(party_add|B_60|B_6c|B_6d|add_money|battle|map_transfer|B_5e|A_2d|give)', k)]
    print(f"disc {d}: {maps} field maps; chests {chests} (items {chest_items}, Fol {chest_fol})")
    for k in sorted(keys): print(f"   {k}: {ops[k]}")
