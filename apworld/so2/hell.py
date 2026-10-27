"""The hell seed (the user, 2026-10-27; docs/ap/apworld.md "The hell seed"; docs/handoff/hellseed.md).

A seed is a hell seed when encounter_rate is 0 (no random battles) and something still needs
training: killer_move_checks (a move's use count) or skillsanity's learn points (special_only,
insanity: levels). Its only battles to train in are then the Fun City arena's (the Fighting
Arena's desk, map 297), so:
- the arena is free on it: the seed patches the desk's 2000-Fol entry fee to 0 (FEE_PATCH), and
  says so (`options.hell_seed`, slot data `hell_seed`: the port's setup and log warn the player);
- killer-move levels, learn points and (the arena's fights pay their enemies' Fol) guild checks
  count from "the Fun City arena is reachable" (arena_need), with their usual member, move and
  character-item needs. Progression may sit behind arena grinding, by design ("the grind is
  intentionally made preposterously hard").
Every other seed keeps the fee and the rules it had.

The arena (verified headless, docs/handoff/hellseed.md "Phase 1"): every fight is an ordinary
battle (map 305's ext 0xb7, no battle rule): a killer move's use counts (record + 0x8a + 2 * move,
the port's killer-move check fires after the fights), the fighter gains the enemy's EXP and the
party its Fol (rank E duel: 104 EXP, 102 Fol; rank C up to ~12000 Fol a registration). Any member
in the party may enter. It is open whenever Fun City is: story mode after the Synard (no item
needed after the disc switch: the disc switch stands for it), open world from the end of stage
TELEPORTER_STAGE with the Fun City Pass (the Expel teleporter's rule: the old man is in the same
building), hub mode from the start. Fun City's gate is shut in chapter 58 only, which the story
passes with no new key (stage 23's keys are all needed earlier).
"""

# Map 297 (the Fighting Arena's desk), routine 0x5c20: `push 0x7d0` (2000 Fol, then Bargaining's
# +/- 3 % a level, 0x25a4) at 0x5dfc = word 0x177f becomes `push 0`: the fee is 0 (0 +/- 0), the
# Fol test never fails, `add_counter18(-0)`; the window reads "An entry fee of 0 FOL". The words
# are alike on NA, Europe, France and Germany (the port relocates by release).
FEE_PATCH = "script 297 0x177f 0x120007d0 0x12000000"


def is_hell_seed(options):
    """encounter_rate 0 with killer_move_checks or skillsanity's learn points (special_only,
    insanity)."""
    return options.encounter_rate.value == 0 and (bool(options.killer_move_checks)
                                                  or options.skillsanity.value in (2, 3))


def trained(options):
    """Whether the party has battles to train in: random battles (encounter_rate above 0) or, on a
    hell seed, the Fun City arena's."""
    return options.encounter_rate.value > 0 or is_hell_seed(options)


def arena_need(world):
    """The items and events "the Fun City arena is reachable" needs: story mode the disc switch
    (the story reaches the Synard with no item after it), open world the end of stage
    TELEPORTER_STAGE and the Fun City Pass, hub mode nothing."""
    from .data import TELEPORTER_STAGE
    from .items import EVENT_ENERGY_NEDE, KEY_NAME, stage_event
    if getattr(world, "hub", False):
        return []
    if world.open_world:
        return [stage_event(TELEPORTER_STAGE), KEY_NAME["Fun City"]]
    return [EVENT_ENERGY_NEDE]


def arena_way(world, way):
    """Open world: an access way (event or None, keys) of a guild place combined with the arena (a
    hell seed's Fol): its event no earlier than TELEPORTER_STAGE's end (the teleporter's and the
    Final Save Point's are later already), its keys and the Fun City Pass. (The caller leaves out
    an Expel place's own early way: Expel closes at the El voyage, before the arena opens; the
    teleporter and the late proof reach Expel after it.)"""
    from .data import STAGES, TELEPORTER_STAGE
    from .items import EVENT_FINAL_SAVE_POINT, EVENT_TELEPORTER, KEY_NAME, stage_event
    ev, keys = way
    stage_of = {stage_event(k): k for k in range(1, len(STAGES) + 1)}
    if ev not in (EVENT_TELEPORTER, EVENT_FINAL_SAVE_POINT):
        ev = stage_event(max(stage_of.get(ev, 0), TELEPORTER_STAGE))
    return ev, tuple(dict.fromkeys(tuple(keys) + (KEY_NAME["Fun City"],)))


def early_way(way):
    """Whether an open-world access way is a place's early way (a stage event or none), not the
    teleporter's or the late proof's."""
    from .items import EVENT_FINAL_SAVE_POINT, EVENT_TELEPORTER
    return way[0] not in (EVENT_TELEPORTER, EVENT_FINAL_SAVE_POINT)
