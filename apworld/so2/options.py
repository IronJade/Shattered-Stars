"""Player options (docs/ap/apworld.md)."""
from dataclasses import dataclass

from Options import Choice, DeathLink, DefaultOnToggle, OptionGroup, OptionSet, PerGameCommonOptions, Range, Toggle

# The twelve members, as the seed and the option values name them (party ids 1-12).
CHARACTER_NAMES = ("Claude", "Rena", "Celine", "Bowman", "Dias", "Precis", "Ashton", "Leon", "Opera", "Ernest",
                   "Noel", "Chisato")


class Goal(Choice):
    """What completes the game.
    indalecio: defeat Indalecio in Fienal, either version of the fight ("final_boss" still works).
    indalecio_limiter_off: defeat Indalecio with his limiter off (the harder fight). Filia's
      meeting in Clik is done for you at the start; after the Final Save Point, enter Central
      City as a Private Action and walk into City Hall: Filia's scene takes the limiter off. The
      way to Indalecio stays shut until then.
    indalecio_limiter_off_and_iseria_queen: as above, and defeat the Iseria Queen first. She is
      summoned by the song "The Evil Melody", composed from the Silver Trumpet that G-Celesta
      gives at the bottom of the Cave of Trials, and played by a member at Musical level 10 with
      Pitch and Sense of Rhythm. The goal needs the cave cleared whether or not cave_of_trials
      adds its locations. The Silver Trumpet can't be sold until the song is composed.
    In hub mode the same goals; Filia's scene can be done from the start (Central City is open
    and the hub is past the Final Save Point), and the Iseria Queen needs the Cave of Trials Key.
    """
    display_name = "Goal"
    option_indalecio = 0
    option_indalecio_limiter_off = 1
    option_indalecio_limiter_off_and_iseria_queen = 2
    alias_final_boss = 0
    default = option_indalecio


class StoryRoute(Choice):
    """The story route: Claude's game or Rena's. With claude or rena, New Game starts that hero's
    game with the default name (the hero select and the naming screen are skipped); with "any",
    you pick at New Game. Locations that exist only in the other route's game are left out. With "any", they stay in the world but hold
    only filler (or follow missable_locations).
    Hub mode: the hero's own boss is Varmillion (Claude's game) or Azamgil (Rena's); with "any"
    both stay as filler-only locations, and both heroes are items (the one you pick at New Game
    is already in the party)."""
    display_name = "Story Route"
    option_any = 0
    option_claude = 1
    option_rena = 2
    default = option_any


class StartingCharacter(Choice):
    """Open world and hub mode: a member who joins you at the start, whatever the story route, and leads
    the party: you walk around as them, and the story calls them by name where it would name
    the hero. 
    
    [The story route's hero stays in the party no matter what.]
    
    hero: nobody extra; the hero leads, as in the original.
    any: you pick at New Game, on a character select after the hero's, among the members this
      world allows (the heroes and the character items' members; never one in exclude_characters).
      Picking the hero means nobody extra. The logic counts on nobody: your pick's own item, when
      it comes, finds them in the party already. (random: the generator picks one of these values.)
    A named member (shown on that screen to confirm) who is excluded still starts (this option
    wins). Ignored in story mode."""
    display_name = "Starting Character"
    option_hero = 13
    option_any = 0
    option_claude = 1
    option_rena = 2
    option_celine = 3
    option_bowman = 4
    option_dias = 5
    option_precis = 6
    option_ashton = 7
    option_leon = 8
    option_opera = 9
    option_ernest = 10
    option_noel = 11
    option_chisato = 12
    default = option_hero


class ExcludeCharacters(OptionSet):
    """Open world with character_items, and hub mode: members who are never in the item pool, so they never
    join (to make room for the ones you want). The story route's hero can't be excluded (under
    story_route "any", neither Claude nor Rena). If you exclude both Rena and Noel, the only
    healers, the party may have no healer."""
    display_name = "Exclude Characters"
    valid_keys = CHARACTER_NAMES


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
    Hub mode: nothing closes there, so a chapter window or a time limit no longer applies; the
    hub only has the locations it can reach, and the choice-bound ones follow this option.
    """
    display_name = "Missable Locations"
    option_exclude = 0
    option_include = 1
    option_remove = 2
    default = option_exclude


class ExpelProgression(Toggle):
    """Expel's locations may hold progression items. The disc switch leaves Expel behind, but
    after the Final Save Point in Fienal the old man in Fun City's arena reopens it (Virtual
    Expel), so they are never lost. Turn this off to keep progression out of Expel (hub mode
    too, where Expel is open from the start). In open world, off also gives you the North City
    Pass at the start: Energy Nede's first pass would have nowhere else to go."""
    display_name = "Expel Progression"
    default = 1


class CaveOfTrials(Toggle):
    """Add the Cave of Trials' chests (Virtual Expel, after the Final Save Point; very hard
    monsters). In hub mode, behind the Cave of Trials Key."""
    display_name = "Cave of Trials"


class BossChecks(DefaultOnToggle):
    """Defeating a boss is a location: the story's bosses, the side quests' and the Cave of
    Trials' (with cave_of_trials). Indalecio is the goal, not a location. In hub mode the story
    bosses wait in their chambers (the Cross Cave guardians among them); off, they still play
    and Fienal's requirement still needs them, but they are no locations."""
    display_name = "Boss Checks"


class CharacterItems(Toggle):
    """The members who join by your choice become items: Celine, Bowman, Precis, Ashton, Opera,
    Ernest, Chisato, Leon (Claude's game) and Dias (Rena's game). The scenes where they join
    for good become locations: a member joins when their item arrives, never at the scene
    (scenes that take one along for a while play as usual). The pool always holds a party the
    story allows: Bowman or Precis, Ashton or Opera (with Ernest), within the party's 8 places
    with Claude, Rena and Noel. With story_route "any", Leon and Dias stay out.
    Open world: no lockouts. Any member but the story route's hero can be an item (the other
    hero and Noel too, Leon and Dias in either game, Bowman with Precis, Ashton with Opera), up
    to 8 with the hero and the starting character, minus exclude_characters. The party always
    gets Rena or Noel (the only healers) unless you exclude both. Members who are not items
    never join.
    Hub mode: always on (no join scene runs there), with open world's rules."""
    display_name = "Character Items"


class EnemyDropsanity(Choice):
    """first_drop: the first item each enemy type drops is a location (enemy types met in
    random encounters). drop_progression decides whether they may hold progression items."""
    display_name = "Enemy Dropsanity"
    option_off = 0
    option_first_drop = 1
    default = option_off


class DropProgression(Choice):
    """With enemy_dropsanity: may an enemy type's first drop hold a progression item?
    allow: it may, but needn't.
    force: the drops with fair odds (at least 1 in 13 kills without Luck) get progression
      first: the generator fills them before anything else, as far as the logic allows.
    never: drops hold only useful, filler and trap items.
    Whatever the choice, only an enemy that can always be met again holds progression (met with
    no story condition, in an area the logic's proofs keep open), never with encounter_rate 0,
    and never a drop that comes less than once in 64 kills without Luck (Magius, Ericodus,
    Shout, Starguarder, the slimes and others need a lucky party to drop at all)."""
    display_name = "Drop Progression"
    option_allow = 0
    option_force = 1
    option_never = 2
    default = option_allow


class AreaKeysInDrops(DefaultOnToggle):
    """With enemy_dropsanity: area keys (open world's) may be among the progression an enemy's
    first drop holds (within drop_progression's rules). Turn it off so that no area is ever
    locked behind a fight's drop: this world's drops then never hold an area key, this world's
    or another Star Ocean player's."""
    display_name = "Area Keys in Drops"


class Craftsanity(Toggle):
    """Each item that Item Creation can make (cooking, compounding, metalwork, customizing, the
    super specialties, Publishing's books...; 553 items) is a location, checked the first time
    you make it. You keep what you made. These locations never hold progression items; one
    whose item only a certain member can make (or, for portraits and books, be the subject of)
    holds only filler unless that member is sure to be in the party, and is left out when that
    member can never join (open world with character_items)."""
    display_name = "Craftsanity"


class KillerMoveChecks(Toggle):
    """Each proficiency level of each killer move is a location, checked when the member's use
    count of the move reaches it (Claude, Bowman, Dias, Precis, Ashton, Opera, Ernest and
    Chisato; 133 locations). Most moves have 1-3 levels at 100-300 uses; the moves that count in
    fine steps (Secret Medicine, Photon Prison, Pillory, Whirlwind Fist, Poison Pills, Cold Wind,
    Gravity Shell) have three, at 100, 200 and 300 uses. A member who may never join has none;
    one who joins only by your choice in the story holds only filler."""
    display_name = "Killer Move Checks"


class KillerMoveProgression(Choice):
    """With killer_move_checks: may a killer move's level hold a progression item?
    never: no; each holds a random non-progression item.
    hero: only the story route's hero's moves (Claude's in Claude's game, and under story_route
      "any"; Rena has no killer moves).
    any: the moves of every member the logic can count on: the heroes, the starting character,
      and members who join by their character item (the logic then needs the item).
    A level counts from the disc switch (moves learned up to level 30) or the Final Save Point
    (the rest), and never with encounter_rate 0 (the uses need battles)."""
    display_name = "Killer Move Progression"
    option_never = 0
    option_hero = 1
    option_any = 2
    default = option_never


class Skillsanity(Choice):
    """Skills, killer moves and spells as items. [skillsanity2]
    none: as the original: skills bought in sets at the skill guilds, moves and spells learned by
      level (or by their item or scene).
    skills_only: buying a guild's set gives one check per skill in it (45 in the 12 sets), and each
      skill is an item ("Skill: Perseverance") that lets the party learn it with SP, as buying the
      set did. With the Cave of Trials, its Level 11 prize (Float) is a check and Float an item.
      Members keep the skills they join with. Item Creation then needs its specialty's skills (a
      member who joins with them, or the items), and so do craftsanity's locations.
    special_only: every member knows the first killer move or spell of their list; the others are
      items ("Claude: Shooting Stars"), and each point where one was learned is a check instead (a
      level: "Claude Lv 7: Shooting Stars", Precis's and Opera's machines, the arena's Private
      Action, the Cave of Trials' learn chests). A member who joins past a level gets its checks at
      the join. Killer-move levels (killer_move_checks) then need the move.
    insanity: both.
    Guild checks and learn points never hold progression."""
    display_name = "Skillsanity"
    option_none = 0
    option_skills_only = 1
    option_special_only = 2
    option_insanity = 3
    default = option_none


class SkillsanityShopShare(Range):
    """With skillsanity and shuffle_shops: the share (percent) of this world's skill, killer move
    and spell items (those the logic doesn't need) put in this world's shop slots, where each is sold
    as itself: a move for its learn level x 100 Fol (an item-taught one 5000, a Cave of Trials prize
    8000), a skill for its set's price / the set's size (Perseverance: 1600 / 4 = 400), Float 900.
    Buying it is the slot's check. [skillsanity2]"""
    display_name = "Skillsanity Shop Share"
    range_start = 0
    range_end = 100
    default = 10


class NoFailCrafting(Toggle):
    """Item Creation never fails by chance: every attempt makes the item the specialty picked
    (a member without the specialty's talents still makes the simplest results)."""
    display_name = "No-Fail Crafting"


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


class Difficulty(Choice):
    """The game's difficulty (its Battle Rank): New Game sets it instead of asking.
    earth: the normal game.
    galaxy: monsters have half again their HP and attack.
    universe: monsters have twice their HP and MP, two thirds more attack, a third more
      intelligence, and better accuracy and evasion.
    any: New Game asks, offering all three (the original offers the choice only after more than
      half of the Voice Collection has been heard).
    Only the monsters change: EXP, Fol, drops and encounters stay the same, so the logic is the
    same on every difficulty."""
    display_name = "Difficulty"
    option_earth = 0
    option_galaxy = 1
    option_universe = 2
    option_any = 3
    default = option_earth


class WorldMode(Choice):
    """story: the areas open as the story opens them.
    open_world: each area's entrances on the world maps (35 areas, 66 entrances) stay shut
      until its pass arrives ("Salva Pass"). You start with the Arlia Pass and the Fun City
      Pass, and the North City Pass too unless expel_teleporter and expel_progression are both
      on; the story still runs in its order, so its areas need their passes on the way. The
      passes are progression items shuffled into the multiworld.
    hub: New Game starts after the Final Save Point, with every chest and boss still waiting.
      Both worlds and every town are open from the start and nothing ever closes; each dungeon
      has one key ("Salva Drift Key"). Each story boss waits in its dungeon and plays its
      original scene; beating it is a location. Fienal opens once the bosses of
      fienal_requirement are beaten. The story's own scenes, the join scenes (party members
      are always items), the early NPC gifts, old Clik and the tournament are not part of it."""
    display_name = "World Mode"
    option_story = 0
    option_open_world = 1
    option_hub = 2
    default = option_story


class HubStart(Choice):
    """Hub mode only: where New Game puts you.
    fun_city: Fun City on Energy Nede (the arena's old man takes you to Expel).
    arlia: Arlia on Expel.
    random_town: any town of either world with a start point, picked at generation.
    (Archipelago's own "random" picks one of these three values.)"""
    display_name = "Hub Start"
    option_fun_city = 0
    option_arlia = 1
    option_random_town = 2
    default = option_fun_city


class FienalRequirement(Choice):
    """Hub mode only: the bosses you must beat before Fienal (and so the goal) opens.
    fields_and_berle: the bosses of the four Fields (Magichand, the Guardians of Power and
      Courage, Lover) and Berle in Armlock.
    fields: the four Fields' bosses only.
    all_bosses: every story boss outside Fienal (Varmillion or Azamgil only with a story_route
      that picks the game)."""
    display_name = "Fienal Requirement"
    option_fields_and_berle = 0
    option_fields = 1
    option_all_bosses = 2
    default = option_fields_and_berle


class ExpelTeleporter(DefaultOnToggle):
    """Open world only (hub mode opens Expel from the start): from chapter 36 the old man in Fun City's arena takes the party back to
    Arlia (and Arlia's guide brings it back), so Expel stays reachable after the disc switch.
    Off: Expel reopens only after the Final Save Point, and Expel's locations then hold only
    Expel's own passes or non-progression items; you also start with the North City Pass."""
    display_name = "Expel Teleporter"


class ShuffleShops(Choice):
    """Shop slots become locations: the first time you buy a slot's item, you get what you buy,
    and the slot's check too (its item given or sent; the price is the slot's own). The shop's
    list marks a slot still to check with the Archipelago logo. Never progression items.
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
    hub_start: HubStart
    fienal_requirement: FienalRequirement
    expel_teleporter: ExpelTeleporter
    shuffle_shops: ShuffleShops
    story_route: StoryRoute
    starting_character: StartingCharacter
    exclude_characters: ExcludeCharacters
    missable_locations: MissableLocations
    expel_progression: ExpelProgression
    cave_of_trials: CaveOfTrials
    boss_checks: BossChecks
    character_items: CharacterItems
    enemy_dropsanity: EnemyDropsanity
    drop_progression: DropProgression
    area_keys_in_drops: AreaKeysInDrops
    craftsanity: Craftsanity
    no_fail_crafting: NoFailCrafting
    killer_move_checks: KillerMoveChecks
    killer_move_progression: KillerMoveProgression
    skillsanity: Skillsanity                        # [skillsanity2]
    skillsanity_shop_share: SkillsanityShopShare
    traps: Traps
    exp_multiplier: ExpMultiplier
    fol_multiplier: FolMultiplier
    encounter_rate: EncounterRate
    difficulty: Difficulty
    death_link: DeathLink


option_groups = [
    OptionGroup("World", [WorldMode, HubStart, FienalRequirement, ExpelTeleporter, ShuffleShops]),
    OptionGroup("Locations", [StoryRoute, MissableLocations, ExpelProgression, CaveOfTrials, BossChecks,
                              EnemyDropsanity, DropProgression, AreaKeysInDrops, Craftsanity, KillerMoveChecks,
                              KillerMoveProgression, Skillsanity, SkillsanityShopShare]),
    OptionGroup("Characters and items", [StartingCharacter, CharacterItems, ExcludeCharacters, Traps]),
    OptionGroup("Battles and crafting", [ExpMultiplier, FolMultiplier, EncounterRate, Difficulty, NoFailCrafting]),
]
