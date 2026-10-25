"""Killer-move checks (killer_move_checks; docs/ap/apworld.md, "Killer moves"; docs/ap/seed_format.md,
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
    return [(lv, lv * div) for lv in levels]

# (character, party id, move (the learn tables' number), name, learn level, div, reachable max level)
MOVES = [
    ('Claude', 1, 1, 'Air Slash', 3, 200, 1),
    ('Claude', 1, 2, 'Shooting Stars', 7, 100, 3),
    ('Claude', 1, 4, 'Head Splitter', 13, 130, 2),
    ('Claude', 1, 6, 'Energy Sword', 20, 100, 2),
    ('Claude', 1, 8, 'Burst Knuckle', 28, 100, 1),
    ('Claude', 1, 7, 'Ripper Blast', 38, 90, 3),
    ('Claude', 1, 5, 'Twin Slash', 45, 160, 1),
    ('Claude', 1, 9, 'Dragon Howl', 53, 250, 2),
    ('Claude', 1, 10, 'Sword Bomber', 62, 140, 3),
    ('Claude', 1, 11, 'Mirror Slice', 70, 230, 2),
    ('Bowman', 4, 1, 'Spirit Attack', 4, 100, 2),
    ('Bowman', 4, 2, 'Poison Pills', 7, 50, 5),
    ('Bowman', 4, 4, 'Pillory', 12, 16, 29),
    ('Bowman', 4, 6, 'Firebird Attack', 20, 200, 2),
    ('Bowman', 4, 3, 'Secret Medicine', 28, 1, 999),
    ('Bowman', 4, 7, 'Death Siege', 35, 100, 2),
    ('Bowman', 4, 9, 'Whirlwind Fist', 43, 25, 20),
    ('Bowman', 4, 10, 'Burst Fist', 51, 100, 2),
    ('Bowman', 4, 8, 'Explosion Pills', 66, 100, 2),
    ('Bowman', 4, 5, 'Sakura Attack', 74, 250, 1),
    ('Dias', 5, 5, 'Air Slash', 6, 100, 1),
    ('Dias', 5, 8, 'Chaos Sword', 11, 120, 1),
    ('Dias', 5, 1, 'Cross Wave', 24, 120, 1),
    ('Dias', 5, 7, 'Crescent Wave', 32, 130, 1),
    ('Dias', 5, 9, 'Gale Stab', 40, 130, 1),
    ('Dias', 5, 4, 'C. Moon Flash', 48, 100, 1),
    ('Dias', 5, 3, 'H. Scream Blast', 56, 100, 1),
    ('Dias', 5, 2, 'Full Moon Slash', 63, 100, 1),
    ('Dias', 5, 10, 'Illusion', 70, 210, 2),
    ('Dias', 5, 6, 'F. Shock Wave', 78, 100, 2),
    ('Precis', 6, 7, 'Rocket Punch', 7, 120, 1),
    ('Precis', 6, 8, 'Hop Step', 12, 140, 1),
    ('Precis', 6, 6, 'Mole', 17, 100, 1),
    ('Precis', 6, 3, 'Ally-oop!', 25, 200, 1),
    ('Precis', 6, 2, 'Parabola Beam', 38, 80, 3),
    ('Precis', 6, 1, 'B. B. Attack', 49, 170, 3),
    ('Precis', 6, 5, 'Bloody Mary', 57, 180, 2),
    ('Ashton', 7, 3, 'Twin Stab', 6, 100, 2),
    ('Ashton', 7, 2, 'Cross Slash', 9, 100, 1),
    ('Ashton', 7, 9, 'Northern Cross', 14, 100, 2),
    ('Ashton', 7, 4, 'Piercing Swords', 22, 64, 3),
    ('Ashton', 7, 8, 'Leaf Slash', 32, 200, 1),
    ('Ashton', 7, 7, 'Hurricane Slash', 45, 100, 2),
    ('Ashton', 7, 5, 'Dead Triangle', 53, 1, 0),
    ('Ashton', 7, 6, 'Dragon Breath', 64, 140, 1),
    ('Ashton', 7, 1, 'Sword Dance', 70, 200, 2),
    ('Opera', 9, 5, 'Flame Launcher', 5, 100, 2),
    ('Opera', 9, 3, 'Photon Prison', 9, 10, 99),
    ('Opera', 9, 4, 'A on One', 24, 220, 2),
    ('Opera', 9, 7, 'Spread Ray', 34, 180, 3),
    ('Opera', 9, 2, 'Cold Wind', 41, 50, 5),
    ('Opera', 9, 9, 'Gravity Shell', 54, 100, 9),
    ('Opera', 9, 6, 'Lightning Blade', 69, 100, 2),
    ('Ernest', 10, 1, 'Dimension Whip', 13, 80, 3),
    ('Ernest', 10, 7, 'Spiral Whip', 24, 120, 1),
    ('Ernest', 10, 3, 'Arc Attack', 32, 100, 2),
    ('Ernest', 10, 4, 'Thousand Whips', 41, 120, 1),
    ('Ernest', 10, 2, 'Cloud Dust', 49, 100, 3),
    ('Ernest', 10, 6, 'Broken Heart', 58, 100, 2),
    ('Ernest', 10, 8, 'Thunder Whip', 65, 100, 2),
    ('Ernest', 10, 5, 'Sonic Whip', 73, 100, 2),
    ('Chisato', 12, 1, 'Twister', 16, 80, 3),
    ('Chisato', 12, 3, 'Burning Cards', 24, 100, 2),
    ('Chisato', 12, 8, 'Whirlwind', 34, 100, 2),
    ('Chisato', 12, 6, '10,000 Volts', 44, 100, 2),
    ('Chisato', 12, 4, 'Flame Thrower', 53, 100, 2),
    ('Chisato', 12, 2, 'Rising Dragon', 62, 100, 3),
    ('Chisato', 12, 5, 'Tear Gas', 68, 100, 1),
    ('Chisato', 12, 7, 'Preparation', 76, 80, 3),
]

# Not in a learn table (left out): Claude's Phase Gun, Precis's M. Super Beam, Precis's Holo-holograph, Precis's Barrier, Ashton's tri-Ace, Opera's Healing Star, Opera's Laser Bit, Opera's Hyper Launcher.
