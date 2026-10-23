"""Player options (docs/ap/apworld.md)."""
from dataclasses import dataclass

from Options import Choice, DeathLink, DefaultOnToggle, OptionGroup, PerGameCommonOptions, Range, Toggle


class Goal(Choice):
    """What completes the game.
    final_boss: defeat Indalecio, the final boss (either version of the fight).
    """
    display_name = "Goal"
    option_final_boss = 0
    default = option_final_boss


class Protagonist(Choice):
    """The protagonist you will pick at New Game. Locations that exist only in the other
    protagonist's game are left out. With "any", they stay in the world but hold only
    filler (or follow missable_locations)."""
    display_name = "Protagonist"
    option_any = 0
    option_claude = 1
    option_rena = 2
    default = option_any


class MissableLocations(Choice):
    """Locations the story can close for good (a chapter window, a town that changes, a
    Private Action, an optional party member's scene, a side quest, a fight you may lose).
    exclude: they stay in the world but hold only filler.
    include: those you can always reach by being in time (a chapter window, a town that
      changes) are ordinary locations: you must not miss them. Those that depend on a choice
      (a Private Action, a member, a side quest, a join scene, a losable fight) stay filler
      only, because two of them can rule each other out.
    remove: they are left out of the world, and their items out of the pool (join scenes stay
      while character_items is on: they are how the members join).
    """
    display_name = "Missable Locations"
    option_exclude = 0
    option_include = 1
    option_remove = 2
    default = option_exclude


class ExpelProgression(Toggle):
    """Expel's locations may hold progression items. The disc switch leaves Expel behind, but
    after the Final Save Point in Fienal the old man in Fun City's arena reopens it (Virtual
    Expel), so they are never lost. Turn this off to keep progression out of Expel."""
    display_name = "Expel Progression"
    default = 1


class CaveOfTrials(Toggle):
    """Add the Cave of Trials' chests (Virtual Expel, after the Final Save Point; very hard
    monsters)."""
    display_name = "Cave of Trials"


class BossChecks(DefaultOnToggle):
    """Defeating a boss is a location: the story's bosses, the side quests' and the Cave of
    Trials' (with cave_of_trials). Indalecio is the goal, not a location."""
    display_name = "Boss Checks"


class CharacterItems(Toggle):
    """The members who join by your choice become items: Celine, Bowman, Precis, Ashton, Opera,
    Ernest, Chisato, Leon (Claude's game) and Dias (Rena's game). The scenes where they join
    for good become locations: a member joins when their item arrives, never at the scene
    (scenes that take one along for a while play as usual). The pool always holds a party the
    story allows: Bowman or Precis, Ashton or Opera (with Ernest), within the party's 8 places
    with Claude, Rena and Noel. With protagonist "any", Leon and Dias stay out."""
    display_name = "Character Items"


class DropsAsChecks(Choice):
    """first_drop: the first item each enemy type drops is a location (enemy types met in
    random encounters). Drops never hold progression items."""
    display_name = "Drops as Checks"
    option_off = 0
    option_first_drop = 1
    default = option_off


class Traps(Range):
    """The share of this world's filler items replaced by traps: an ambush, losing some Fol, or
    a status ailment (poison, paralysis, stone)."""
    display_name = "Traps"
    range_start = 0
    range_end = 100
    default = 0


class ExpMultiplier(Range):
    """Battles give this many times the EXP."""
    display_name = "EXP Multiplier"
    range_start = 1
    range_end = 10
    default = 1


class FolMultiplier(Range):
    """Battles give this many times the Fol."""
    display_name = "Fol Multiplier"
    range_start = 1
    range_end = 10
    default = 1


class EncounterRate(Range):
    """Random encounters, in percent of the original rate (0: none)."""
    display_name = "Encounter Rate"
    range_start = 0
    range_end = 200
    default = 100


class WorldMode(Choice):
    """story: the areas open as the story opens them.
    open_world: each area's entrances on the world maps (35 areas, 66 entrances) stay shut
      until its pass arrives ("Salva Pass"). You start with the Arlia Pass and the Fun City
      Pass; the story still runs in its order, so its areas need their passes on the way. The
      passes are progression items shuffled into the multiworld."""
    display_name = "World Mode"
    option_story = 0
    option_open_world = 1
    default = option_story


class ExpelTeleporter(DefaultOnToggle):
    """Open world only: from chapter 36 the old man in Fun City's arena takes the party back to
    Arlia (and Arlia's guide brings it back), so Expel stays reachable after the disc switch.
    Off: Expel reopens only after the Final Save Point, and Expel's locations then hold only
    Expel's own passes or non-progression items."""
    display_name = "Expel Teleporter"


class ShuffleShops(Choice):
    """Shop slots become locations: the first copy you buy of a slot's item gives the slot's
    item instead (any price, the slot's own). They never hold progression items.
    specials: the slots selling an item no other shop sells (98 slots).
    full: every slot of every shop a script opens (550 slots)."""
    display_name = "Shuffle Shops"
    option_off = 0
    option_specials = 1
    option_full = 2
    default = option_off


@dataclass
class SO2Options(PerGameCommonOptions):
    goal: Goal
    world_mode: WorldMode
    expel_teleporter: ExpelTeleporter
    shuffle_shops: ShuffleShops
    protagonist: Protagonist
    missable_locations: MissableLocations
    expel_progression: ExpelProgression
    cave_of_trials: CaveOfTrials
    boss_checks: BossChecks
    character_items: CharacterItems
    drops_as_checks: DropsAsChecks
    traps: Traps
    exp_multiplier: ExpMultiplier
    fol_multiplier: FolMultiplier
    encounter_rate: EncounterRate
    death_link: DeathLink


option_groups = [
    OptionGroup("World", [WorldMode, ExpelTeleporter, ShuffleShops]),
    OptionGroup("Locations", [Protagonist, MissableLocations, ExpelProgression, CaveOfTrials, BossChecks,
                              DropsAsChecks]),
    OptionGroup("Characters and items", [CharacterItems, Traps]),
    OptionGroup("Battles", [ExpMultiplier, FolMultiplier, EncounterRate]),
]
