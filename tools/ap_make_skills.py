#!/usr/bin/env python3
"""Skillsanity's tables (apworld/so2/skills.py) from disc 1 and the port's member data.

    tools/ap_make_skills.py DISC1.cue [-o apworld/so2/skills.py]

Read off the disc (docs/notes/menu.md "Skill guilds", the research of the agent "skillsanity"):
- the guild sets' skills: k_book_groups (#3001 0x8008d2d8, five ids per set, flag 700 + set);
- the specialties' skills (field 0x8007197c, three per specialty) and the super specialties'
  pairs (0x800719ae);
- every member's learn table (#1981, as battle/result.cpp levelup_learn_skills reads it: the
  fighters' move numbers, the casters' spells by their place in the class list of #2129) and the
  moves' names (#1980: spells 1000 + spell - 1, fighters the member's own base; a fighter's move
  named as apworld/so2/moves.py has it when listed there).
From the port's engine/src/game/field/characters.cpp (the decompiled init_* routines): each
member's starting skills (record +0x5c + skill). Constants below (the research, from the guild
scripts' menus and explainer texts): the sets' names, prices and guild maps, the skill names and
the moves taught outside the learn tables.
"""
import argparse
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from so2_archive import unpack_block  # noqa: E402
from so2_data import Disc, read_names  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

SKILL_NAMES = {
    1: "Sketching", 2: "Musical Notation", 3: "Music Instrument", 4: "Tool Knowledge", 5: "Mineralogy",
    6: "Herbal Medicine", 7: "Craft", 8: "Esthetic Sense", 9: "Writing", 0xA: "Effort", 0xB: "Perseverance",
    0xC: "Patience", 0xD: "Danger Sense", 0xE: "Biology", 0xF: "Mental Science", 0x10: "Kitchen Knife",
    0x11: "Recipe", 0x12: "Good Eye", 0x13: "Whistling", 0x14: "Animal Training", 0x15: "Metal Casting",
    0x16: "Scientific Ability", 0x17: "Fairyology", 0x18: "Radar", 0x19: "Piety", 0x1A: "Playfulness",
    0x1B: "Functionality", 0x1C: "Courage", 0x1D: "Poker Face", 0x1E: "Copying", 0x1F: "Mech Knowledge",
    0x20: "Mech Operation", 0x21: "Below the Belt", 0x22: "Strong Blow", 0x23: "Flip", 0x24: "Counterattack",
    0x25: "Feint", 0x26: "Mental Training", 0x27: "Motormouth", 0x28: "Body Control", 0x29: "Spirit Force",
    0x2A: "Parry", 0x2B: "Cancel", 0x2C: "Gale", 0x2D: "Provocation", 0x2E: "Float"}
# Set (flag 700 + set): name, price (Fol), the guild maps that sell it (menu.md "Skill guilds").
SETS = [("Knowledge 1", 300, (80, 123, 190, 258)), ("Sensibility 1", 400, (80, 123, 258)),
        ("Technique 1", 400, (80, 123, 147, 258)), ("Combat 1", 400, (123, 147, 219, 316)),
        ("Knowledge 2", 1500, (147, 173, 190, 258)), ("Sensibility 2", 1600, (147, 173, 264)),
        ("Technique 2", 1600, (173, 264)), ("Combat 2", 1600, (173, 219, 316)),
        ("Knowledge 3", 2700, (190, 264)), ("Sensibility 3", 2700, (219, 316)),
        ("Technique 3", 3600, (190, 264)), ("Combat 3", 4500, (219, 316))]
GUILD_TOWNS = {80: "Cross", 123: "Port of Clik", 147: "Herlie", 173: "Hilton", 190: "Linga", 219: "Lacour",
               316: "Armlock", 258: "Central City", 264: "North City"}
SPECIALTIES = ["", "Art", "Oracle", "Musical Talent", "Customize", "Identify", "Metalwork", "Authoring",
               "Practice", "Scout", "Compounding", "Cooking", "Familiar", "Alchemy", "Survival", "Pickpocketing",
               "Reproduction", "Machinery"]
SUPER_SPECIALTIES = ["", "Master Chef", "Orchestra", "Comprehension", "Come on Bunny", "Publishing",
                     "Identify All!", "Blacksmith", "Reverse Side"]
MEMBERS = ["Claude", "Rena", "Celine", "Bowman", "Dias", "Precis", "Ashton", "Leon", "Opera", "Ernest", "Noel",
           "Chisato"]
# The learn tables (battle/result.cpp levelup_learn_skills): table, class list (casters), class, the
# message base of a fighter's move names (#1980).
TABLES = [(0x80087DD0, None, 0, 5000), (0x80087E74, 0x8006B1B8, 9, 1000), (0x80087EA4, 0x8006B1E4, 11, 1000),
          (0x80087DE8, None, 0, 0x141E), (0x80087E34, None, 0, 0x1450), (0x80087E24, None, 0, 0x1482),
          (0x80087E10, None, 0, 0x14B4), (0x80087EFC, 0x8006B1D0, 10, 1000), (0x80087E00, None, 0, 0x1518),
          (0x80087E4C, None, 0, 0x154A), (0x80087ED0, 0x8006B1FC, 12, 1000), (0x80087E60, None, 0, 0x15AE)]
# Moves taught outside the learn tables (record index): the items 464-469 (item effect
# character_flag; 465-469 only from Machinery, 464 also the arena's Private Action, map 304), and
# the Cave of Trials' learn chests (script ext 0x73; their floor). Claude's Phase Gun (index 2, the
# opening's ext 0x73, taken back at Salva) is left out (the user, 2026-10-25).
EXTRA = [("Precis", 8, "item 464 Holoprojector or the arena's Private Action (map 304)", 5000),
         ("Precis", 9, "item 465 Plasma Zap-stick", 5000), ("Precis", 3, "item 466 Mech Launcher", 5000),
         ("Opera", 0, "item 467 White System", 5000), ("Opera", 7, "item 468 Black System", 5000),
         ("Opera", 9, "item 469 Green System", 5000),
         ("Leon", 12, "the Cave of Trials Level 4 learn chest (map 740)", 8000),
         ("Ashton", 9, "the Cave of Trials Level 10 learn chest (map 798)", 8000),
         ("Celine", 13, "the Cave of Trials Level 10 learn chest (map 799)", 8000)]
FLOAT_PRICE = 900


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("disc")
    ap.add_argument("-o", "--out", default=os.path.join(ROOT, "apworld", "so2", "skills.py"))
    a = ap.parse_args()
    disc = Disc(a.disc)
    names = read_names(disc)
    effect = bytes(unpack_block(disc.entry(3001), 0))
    field = bytes(unpack_block(disc.entry(3100), 0))
    prog = bytes(unpack_block(disc.entry(2129), 0))
    res = bytes(unpack_block(disc.entry(1981), 0))
    groups, p = [], 0x8008D2D8 - 0x80088000
    while struct.unpack_from("b", effect, p)[0] != -1:
        groups.append(effect[p])
        p += 1
    set_skills = [tuple(s for s in groups[k * 5:k * 5 + 5] if s) for k in range(len(groups) // 5)]
    assert len(set_skills) == 12, set_skills
    spec_skills = {}
    for s in range(1, 18):
        o = 0x8007197C - 0x8002F810 + (s - 1) * 3
        spec_skills[SPECIALTIES[s]] = tuple(x for x in field[o:o + 3] if x)
    supers = {}
    for i in range(1, 9):
        o = 0x800719AE - 0x8002F810 + i * 2
        supers[SUPER_SPECIALTIES[i]] = (SPECIALTIES[field[o]], SPECIALTIES[field[o + 1]])
    src = open(os.path.join(ROOT, "engine", "src", "game", "field", "characters.cpp")).read()
    start_skills = {}
    for m in re.finditer(r"void init_(\w+)\(u8\* slot, u8\* record\) \{(.*?)\n\}", src, re.S):
        found = []
        for off in re.findall(r"record\[(0x[0-9A-Fa-f]+)\] = \d+;", m.group(2)):
            s = int(off, 16) - 0x5C
            if 1 <= s <= 0x2E:
                found.append(s)
        start_skills[m.group(1).capitalize()] = tuple(sorted(found))
    sys.path.insert(0, os.path.join(ROOT, "apworld"))
    moves_py = {}
    exec(open(os.path.join(ROOT, "apworld", "so2", "moves.py")).read(), moves_py)
    fighter_name = {(who, mv - 1): name for who, _, mv, name, *_ in moves_py["MOVES"]}
    learn, firsts = [], {}
    for pid, (table, clist, cls, base) in enumerate(TABLES, start=1):
        who = MEMBERS[pid - 1]
        rows, t = [], table - 0x80085518
        while res[t]:
            rows.append((res[t], res[t + 1]))
            t += 2
        spells = [prog[clist - 0x8002F810 + i] for i in range(prog[0x8006B1AB + cls - 0x8002F810])] if clist else None
        for k, (level, v) in enumerate(rows):
            if spells is None:
                index, name = v - 1, fighter_name.get((who, v - 1)) or names.get(base + v - 1, f"move {v}")
            else:
                index, name = spells.index(v), names.get(1000 + v - 1, f"spell {v}")
            learn.append((who, pid, index, name, level, k == 0))
            if k == 0:
                firsts[who] = index
    extra = []
    for who, index, how, price in EXTRA:
        pid = MEMBERS.index(who) + 1
        table, clist, cls, base = TABLES[pid - 1]
        if clist:
            spells = [prog[clist - 0x8002F810 + i] for i in range(prog[0x8006B1AB + cls - 0x8002F810])]
            name = names.get(1000 + spells[index] - 1, f"spell {index}")
        else:
            name = names.get(base + index, f"move {index}")
        extra.append((who, pid, index, name, how, price))
    with open(a.out, "w", encoding="utf-8", newline="\n") as f:
        w = f.write
        w('"""Skillsanity (docs/ap/apworld.md "Skillsanity"; docs/ap/seed_format.md "Skillsanity"). Written by\n'
          'tools/ap_make_skills.py from disc 1 and the port\'s member data; do not edit.\n\n'
          "- SKILLS: the 46 skills by id (the port's record +0x5c + id holds a member's level).\n"
          "- SKILL_SETS: the 12 guild sets, (name, price, skill ids, guild maps); flag 700 + set.\n"
          "- STARTING_SKILLS: the skills each member joins with (init_*: they stay the member's).\n"
          "- SPECIALTIES / SUPER_SPECIALTIES: the skills a specialty needs (field 0x8007197c), and the\n"
          "  two specialties of a super specialty (0x800719ae).\n"
          "- LEARN: every learn-table entry, (member, party id, record index, name, level, first): the\n"
          "  first is the member's first move (known from the start under special_only / insanity).\n"
          "- EXTRA_MOVES: the moves taught outside the learn tables, (member, party id, record index,\n"
          "  name, how, price); FLOAT: the Cave of Trials' Level 11 prize (special 0x12).\n"
          '"""\n')
        w(f"SKILLS = {{\n" + "".join(f"    {k}: {v!r},\n" for k, v in SKILL_NAMES.items()) + "}\n")
        w("SKILL_SETS = [\n" + "".join(f"    ({n!r}, {p}, {s!r}, {g!r}),\n"
                                       for (n, p, g), s in zip(SETS, set_skills)) + "]\n")
        w(f"GUILD_TOWNS = {GUILD_TOWNS!r}\n")
        w("STARTING_SKILLS = {\n" + "".join(f"    {k!r}: {v!r},\n" for k, v in start_skills.items()) + "}\n")
        w("SPECIALTIES = {\n" + "".join(f"    {k!r}: {v!r},\n" for k, v in spec_skills.items()) + "}\n")
        w("SUPER_SPECIALTIES = {\n" + "".join(f"    {k!r}: {v!r},\n" for k, v in supers.items()) + "}\n")
        w("LEARN = [\n" + "".join(f"    {r!r},\n" for r in learn) + "]\n")
        w("EXTRA_MOVES = [\n" + "".join(f"    {r!r},\n" for r in extra) + "]\n")
        w(f"FLOAT = 0x2E\nFLOAT_PRICE = {FLOAT_PRICE}\n")
    print(f"{a.out}: {len(set_skills)} sets ({sum(map(len, set_skills))} skills), {len(learn)} learn-table moves "
          f"({len(firsts)} first), {len(extra)} extra moves")


if __name__ == "__main__":
    main()
