"""Items: every game item (AP id = ITEM_BASE + item number), the Fol bundles found at
locations (FOL_BASE + amount), the characters character_items shuffles (CHARACTER_BASE + party
id), the traps (TRAP_BASE + n), open world's area keys (KEY_BASE + n) and hub mode's dungeon keys
(HUB_KEY_BASE + n). Story key items are script flags and stay where they are."""
from BaseClasses import Item, ItemClassification

from . import skillsanity
from .data import DISC_SWITCH_STAGE, FOL_AMOUNTS, FOL_BASE, HUB_DUNGEONS, ITEM_BASE, ITEMS, KEY_GROUPS, USEFUL_RANGES

GAME = "Star Ocean: The Second Story"
FILLER_NAME = "Blueberry"

CHARACTER_BASE = ITEM_BASE + 0x20000
TRAP_BASE = ITEM_BASE + 0x30000
KEY_BASE = ITEM_BASE + 0x40000      # an area key (open world): KEY_BASE + key number 1-35
HUB_KEY_BASE = ITEM_BASE + 0x50000  # a dungeon key (hub mode): HUB_KEY_BASE + key number (data.HUB_DUNGEONS)
# Party ids (the port's 1-12: party_add's argument + 1).
PARTY_ID = {"Claude": 1, "Rena": 2, "Celine": 3, "Bowman": 4, "Dias": 5, "Precis": 6, "Ashton": 7, "Leon": 8,
            "Opera": 9, "Ernest": 10, "Noel": 11, "Chisato": 12}
CHARACTER_ITEMS = ("Celine", "Bowman", "Dias", "Precis", "Ashton", "Leon", "Opera", "Ernest", "Chisato")
# The members that never join by an item under the story's party rules: the story_routes and
# Noel (the story adds him). Open world's party rules make them items too, except the story
# route's hero (docs/ap/seed_format.md, "Party v2").
FIXED_MEMBERS = ("Claude", "Rena", "Noel")
ALL_CHARACTER_ITEMS = CHARACTER_ITEMS + FIXED_MEMBERS
CHARACTER_NAMES = tuple(sorted(PARTY_ID, key=PARTY_ID.get))     # by party id
HEALERS = ("Rena", "Noel")      # the only members who learn healing spells (Rena alone revives)
PARTY_SIZE = 8
# With the bench (options.Bench; open world with character_items and hub mode): the 8 slots the game
# knows and 4 bench places the port keeps (docs/ap/seed_format.md "The bench"): room for all 12.
BENCH_SIZE = 4
BENCH_PARTY_SIZE = PARTY_SIZE + BENCH_SIZE
# Trap items and what the port does with them (docs/ap/seed_format.md, phase 3).
TRAPS = {
    "Ambush Trap": {"trap": "ambush"},
    "Fol Loss Trap": {"trap": "fol_loss", "percent": 10},
    "Poison Trap": {"trap": "status", "status": "poison"},
    "Paralysis Trap": {"trap": "status", "status": "paralysis"},
    "Stone Trap": {"trap": "status", "status": "stone"},
}
TRAP_IDS = {name: TRAP_BASE + k for k, name in enumerate(TRAPS, start=1)}

ITEM_NAME_TO_ID = {name: ITEM_BASE + n for n, name in ITEMS.items()}
ITEM_NAME_TO_ID.update({f"{amount} Fol": FOL_BASE + amount for amount in FOL_AMOUNTS})
ITEM_NAME_TO_ID.update({name: CHARACTER_BASE + PARTY_ID[name] for name in ALL_CHARACTER_ITEMS})
ITEM_NAME_TO_ID.update(TRAP_IDS)
# Area keys (open world, phase 4): one per world-map entrance group (data.KEY_GROUPS).
KEY_OF = {KEY_BASE + g["key"]: g for g in KEY_GROUPS}
KEY_NAME = {g["group"]: g["name"] for g in KEY_GROUPS}
ITEM_NAME_TO_ID.update({g["name"]: KEY_BASE + g["key"] for g in KEY_GROUPS})
# Dungeon keys (hub mode, D4): one per dungeon of data.HUB_DUNGEONS, named "Salva Drift Key" ... The
# seed describes them as area keys ({"kind": "key", "key": N}: the port matches doors by N, which a
# hub seed uses for dungeon keys alone). None for Fienal (the Fienal requirement, D6, gates it)
# and the Portal of Sealing (out of hub mode: hubcore, 2026-10-25; the late state never reaches it,
# its doors are story-gated and Armlock's 306 is Berle's chamber; one chest).
HUB_KEYLESS = ("Fienal", "Portal of Sealing")
HUB_KEY_OF = {HUB_KEY_BASE + g["key"]: g for g in HUB_DUNGEONS if g["dungeon"] not in HUB_KEYLESS}
HUB_KEY_NAME = {g["dungeon"]: g["name"] for g in HUB_KEY_OF.values()}
ITEM_NAME_TO_ID.update({g["name"]: code for code, g in HUB_KEY_OF.items()})
ALL_KEY_IDS = frozenset(KEY_OF) | frozenset(HUB_KEY_OF)    # any Star Ocean key (area_keys_in_drops)
# Keys the Expel story needs (the disc switch can't be reached without them).
EXPEL_STORY_KEYS = frozenset(g["name"] for g in KEY_GROUPS if g["stage"] and g["stage"] <= DISC_SWITCH_STAGE)
# Open world's starting keys: the start (Arlia) and the teleporter's way back to Expel (Fun City),
# which also opens Virtual Expel after the Final Save Point. NEDE_START_KEYS too unless
# expel_teleporter and expel_progression are both on (world.create_items; 0.2.1).
START_KEYS = ("Arlia Pass", "Fun City Pass")
NEDE_START_KEYS = ("North City Pass", "Noel's House Pass")
# Skillsanity [skillsanity2]: the skills (SKILL_BASE + id) and the killer moves / spells (MOVE_BASE +
# (party id << 8) + record index), skillsanity.py.
ITEM_NAME_TO_ID.update(skillsanity.ITEM_NAME_TO_ID)
GAME_ITEM = {ITEM_BASE + n: n for n in ITEMS}          # AP id -> game item number
FOL_AMOUNT = {FOL_BASE + a: a for a in FOL_AMOUNTS}     # AP id -> Fol
CHARACTER_OF = {CHARACTER_BASE + PARTY_ID[n]: n for n in ALL_CHARACTER_ITEMS}
TRAP_OF = {v: k for k, v in TRAP_IDS.items()}

ITEM_NAME_GROUPS = {
    "Fol": {f"{amount} Fol" for amount in FOL_AMOUNTS},
    "Equipment": {name for n, name in ITEMS.items() if any(lo <= n <= hi for lo, hi in USEFUL_RANGES)},
    "Characters": set(ALL_CHARACTER_ITEMS),
    "Traps": set(TRAPS),
    "Keys": set(KEY_NAME.values()),
    "Dungeon Keys": set(HUB_KEY_NAME.values()),
    **skillsanity.ITEM_GROUPS,
}

# Items a script tests, so they stay at their own locations (locked there, never in the pool):
# the Cave of Trials' doors on maps 751 and 764 take the Red Lotus Gem (317; gifts on maps 711
# and 754) and test the Cracked Gem (289; the chest on map 771). Shuffled, a door could need
# a gem placed behind it (docs/ap/apworld.md, "Logic").
PUZZLE_ITEMS = (289, 317)

# Events (no id): story milestones the regions hang on, and the goal.
EVENT_ENERGY_NEDE = "Arrived on Energy Nede"
EVENT_FINAL_SAVE_POINT = "Saved at the Final Save Point"
EVENT_VICTORY = "Victory"
EVENT_TELEPORTER = "Expel Teleporter"     # open world: Expel reachable again once the Synard flies (chapter 41)
EVENT_LIMITER_OFF = "Limiter Off"         # goals: Filia's scene in Central City City Hall sets flag 556
EVENT_ISERIA_QUEEN = "Iseria Queen defeated"   # goal indalecio_limiter_off_and_iseria_queen
EVENT_FIENAL_OPEN = "Fienal opened"       # hub mode: the Fienal requirement's bosses beaten (D6)


def stage_event(k):
    """Open world: the event of story stage k (1-based, data.STAGES) being finished."""
    return f"Stage {k} done"


class SO2Item(Item):
    game = GAME


def classification(name):
    """Equipment and characters are useful. A character is not progression: every location
    that needs a member is excluded from progression (members rule each other out, which the
    logic can't express), so nothing in the logic waits for one. The exception,
    killer_move_progression any, is applied in SO2World.create_item (progression_characters)."""
    code = ITEM_NAME_TO_ID[name]
    if code in KEY_OF or code in HUB_KEY_OF:
        return ItemClassification.progression
    if code in TRAP_OF:
        return ItemClassification.trap
    if code in CHARACTER_OF or skillsanity.is_skillsanity_item(code):
        return ItemClassification.useful    # (a skill or move the logic needs: SO2World.create_item)
    n = GAME_ITEM.get(code)
    if n is not None and any(lo <= n <= hi for lo, hi in USEFUL_RANGES):
        return ItemClassification.useful
    return ItemClassification.filler


def pool_filler(item):
    """The pool item a location adds that has no vanilla contents of its own (a craft): the
    item itself when it is filler, else the filler item (equipment stays out, so the pool's
    useful share doesn't grow)."""
    name = ITEMS[item]
    return name if classification(name) == ItemClassification.filler else FILLER_NAME


def item_name(item, fol):
    """The item a location's vanilla contents give."""
    return f"{fol} Fol" if fol is not None else ITEMS[item]


def seed_item(name, code):
    """An own item as the seed describes it (docs/ap/seed_format.md, "An item")."""
    if code in FOL_AMOUNT:
        return {"name": name, "kind": "fol", "amount": FOL_AMOUNT[code]}
    if code in CHARACTER_OF:
        return {"name": name, "kind": "character", "character": CHARACTER_OF[code]}
    if code in TRAP_OF:
        return dict({"name": name, "kind": "trap"}, **TRAPS[name])
    if code in KEY_OF:
        return {"name": name, "kind": "key", "key": KEY_OF[code]["key"]}
    if code in HUB_KEY_OF:
        return {"name": name, "kind": "key", "key": HUB_KEY_OF[code]["key"]}
    if skillsanity.is_skillsanity_item(code):
        return skillsanity.seed_item(name, code)
    return {"name": name, "kind": "item", "game_id": GAME_ITEM[code], "count": 1}
