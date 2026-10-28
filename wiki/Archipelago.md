# Archipelago

Shattered Stars can play *Star Ocean: The Second Story* as part of an
[Archipelago](https://archipelago.gg) multiworld. The network client is built into the game,
so there is no separate client program to run.

This page describes **`so2.apworld` 0.2.2**, the world that goes with **Shattered Stars 0.03.00**.
The trackers that follow it, Universal Tracker and the PopTracker pack, have a page of their own:
[Trackers](Trackers).

> [!IMPORTANT]
> **A seed made with the bench needs Shattered Stars 0.02.51 or later.** The bench is on by
> default (since 0.2.1) for open world with `character_items` and for hub mode. Shattered Stars
> 0.02.50 refuses such a seed with "unsupported version 4" and plays every other 0.2.1 and 0.2.2
> seed (a hell seed without its warning). Shattered Stars 0.03.00 plays every seed: 0.2.2's,
> 0.2.1's, 0.2.0's, and those made with 0.1.0 and 0.1.1. See
> [What changed in 0.2.2](#what-changed-in-022) and [in 0.2.1](#what-changed-in-021).

## What is Archipelago?

Archipelago is a multiworld randomizer. Several players, each playing their own game (and not
necessarily the same game), have the items of all their games shuffled together. A chest in
your Star Ocean game might hold another player's item from a completely different game, and
the item you need might be sitting in someone else's world. When anyone finds an item that
belongs to another player, it is sent to them over the network. Every seed is checked when it
is made, so it can always be finished.

Some terms used below:

| Term | Meaning |
|---|---|
| **World** | One game's part of the multiworld. "This world" means Star Ocean: The Second Story |
| **Location** or **check** | A place that gives an item: a chest, a gift, a boss, and so on |
| **Seed** | One generated multiworld. Archipelago writes it as a zip file; each Star Ocean player's part of it is a seed file ending in `.apso2` |
| **Slot** | Your place in the multiworld, identified by your slot name |
| **YAML** | The text file with your options, read by the generator |
| **`.apworld`** | The plugin that teaches Archipelago about this game |
| **Progression item** | An item that the logic may require to reach other locations |
| **Filler** | An item nobody needs to finish, such as a recovery item or a little Fol |

## What you need

- **Shattered Stars 0.03.00** (0.02.51 plays 0.2.2's seeds too, without telling trackers your
  map and story), set up with your own disc images of the North American,
  European, French or German release (see [Installing and Setup](Installing-and-Setup)). One
  seed works with any of them, so the players of a multiworld can each use their own release.
- **Archipelago 0.6.8 or newer**, from [archipelago.gg](https://archipelago.gg).
- **This world's `.apworld` file** (`so2.apworld`) and its options template
  (downloaded as `Star.Ocean.The.Second.Story.yaml`), from the world's own release,
  [so2.apworld 0.2.2](https://github.com/IronJade/Shattered-Stars/releases/tag/apworld-v0.2.2). The world is released apart from the game, so either can
  be updated without the other. They can also be built from source
  ([Building from Source](Building-from-Source#the-apworld)).

This is a custom world: it works with any Archipelago install but is not yet part of
Archipelago itself, so the archipelago.gg website doesn't list it.

## The three world modes

The `world_mode` option decides how much of the story you play:

| Mode | What it is |
|---|---|
| `story` (the default) | The original game from New Game to the end, with its locations shuffled. The story opens the world in its own order |
| `open_world` | The story still runs in its order, but every entrance on the world maps is locked until its **area key** arrives (35 keys, "Salva Pass", "Fienal Pass" and so on), and the ships of Port Hilton go where you choose. See [Open world](#open-world) |
| `hub` | No story to play. New Game starts after the Final Save Point with both worlds open, a **key per dungeon**, and every story boss waiting in its dungeon. See [Hub mode](#hub-mode) |

## What gets randomized

**Always:** every chest (308) and every gift site (101) holds an item from the multiworld
instead of its original contents. A gift site is anything that hands you an item or Fol outside
a chest: a scene, a Private Action, a prize table. 75 of those chests and 3 of the gifts are in
the Cave of Trials, which takes part only with `cave_of_trials`.

**The story's key items are never shuffled.** They are story flags rather than objects, so the
story always carries on, whatever you find.

**Optionally**, more kinds of locations:

| Kind | Count | Option |
|---|---|---|
| Defeating a boss | 41 (13 of them in the Cave of Trials) | `boss_checks` (on by default) |
| A party member joining you | 18 (21 under open world's party rules) | `character_items` |
| The first time an enemy type drops anything in a random encounter | 89 | `enemy_dropsanity: first_drop` |
| Buying from a shop slot for the first time | up to 550 (98 sell something no other shop does) | `shuffle_shops: specials` or `full` |
| Making an item with Item Creation for the first time | 553 | `craftsanity` |
| A killer move reaching a new proficiency level | 133 (69 moves) | `killer_move_checks` |
| A skill bought at a skill guild | 45 (12 sets) | `skillsanity: skills_only` or `insanity` |
| A level or place where a member learns a killer move or spell | 139 levels and 9 other places | `skillsanity: special_only` or `insanity` |

In hub mode only the locations the hub can reach exist; see [Hub mode](#hub-mode).

**Where progression items never go**, in any mode: shop slots, gift sites, join sites and
crafts. A price can run into the tens of thousands of Fol, so nothing the logic needs is ever
behind a purchase. Enemy drops follow `drop_progression` ([Enemy drops](#enemy-drops)),
killer-move levels `killer_move_progression` ([Killer moves](#killer-moves)); skill-guild checks
and learn levels may hold progression once the logic can count on reaching them
([Skillsanity](#skillsanity)).

## The goal

The goal is always **defeating Indalecio**, the final boss, in Fienal. The `goal` option picks
which version of him counts:

| `goal` | What you must do |
|---|---|
| `indalecio` (the default; `final_boss`, its 0.1.x name, still works) | Defeat Indalecio, either version of the fight. This is how the game normally ends |
| `indalecio_limiter_off` | Defeat the harder version, with his limiter off. In the original that needs an easy-to-miss Private Action with Filia in Clik early in the story; here that meeting is done for you. After you have saved at the Final Save Point in Fienal, enter Central City as a Private Action from the world map and walk into City Hall: Filia's scene takes the limiter off (and gives you Israfil's Tear). The way from the Final Save Point to Indalecio stays shut until then: "Talk to Filia in Central City first" |
| `indalecio_limiter_off_and_iseria_queen` | As above, and defeat the Iseria Queen first. She has no fixed battle: she is summoned by the song "The Evil Melody", composed with the Silver Trumpet that G-Celesta gives you at the bottom of the Cave of Trials, and played with a Conductor's Baton by a party member at Musical level 10 with the Pitch and Sense of Rhythm talents. So this goal means clearing the Cave of Trials, whether or not `cave_of_trials` adds its locations. The Silver Trumpet can't be sold until the song is composed. The way to Indalecio stays shut until she is beaten: "Defeat the Iseria Queen first" |

With `cave_of_trials` on, the Cave of Trials becomes extra, very hard optional content after
the late-game save point, with its own chests, gifts and bosses. It doesn't change the goal.

## Items

- **Equipment and consumables.** Most items are ordinary game items. Weapons, armour and a few
  other strong item ranges count as *useful*; everything else is filler. The filler item is
  **Blueberry**.
- **Fol.** Many gift sites and some chests give bundles of Fol. These are filler.
- **Characters**, with `character_items` on (always in hub mode). See
  [Party members](#party-members). A character is *useful*; with
  `killer_move_progression: any` a member whose killer moves hold progression items is a
  progression item too.
- **Area keys**, in open world: 35 keys, each named after an area, unlock that area's
  entrances on the world map. Progression items.
- **Dungeon keys**, in hub mode: one per dungeon ("Salva Drift Key", "Field of Power Key" and so
  on, up to 18), each unlocking every door into its dungeon. Progression items.
- **Skills, killer moves and spells**, with `skillsanity`: "Skill: Perseverance", "Claude:
  Shooting Stars" and so on. See [Skillsanity](#skillsanity).
- **Traps**, with `traps` above 0%. That share of the filler becomes traps:

  | Trap | Effect |
  |---|---|
  | Ambush Trap | Your next free step on a map with random encounters starts a battle |
  | Fol Loss Trap | You lose 10% of your Fol |
  | Poison Trap | Poisons your active party |
  | Paralysis Trap | Paralyzes one active party member |
  | Stone Trap | Turns one active party member to stone |

## Missable locations

In story mode and open world, some locations can be missed for good. The `missable_locations`
option decides what happens to them:

| Kind of location | `exclude` (default) | `include` | `remove` |
|---|---|---|---|
| Closed off by the story moving on (a scene in a chapter window, a town that changes too much to revisit) | Holds only filler | An ordinary location; don't be late | Left out of the world |
| Depends on a choice that rules out another location (an optional member's scene, a Private Action, a side quest, a fight you may lose, a join site, or a Claude-only or Rena-only location when `story_route` is `any`) | Holds only filler | Holds only filler | Left out, except join sites |

Choice-dependent locations always hold filler, because two of them can rule each other out.
Join sites are never removed, because they are how members join with `character_items` on. In
hub mode nothing closes, so only the choice-dependent kind applies.

## Party members

### The story's rules (story mode)

With `character_items` on, **Celine, Bowman, Precis, Ashton, Opera, Ernest, Chisato, Leon and
Dias** become items instead of joining at their story scene. The scene where they would join for
good becomes a location with its own shuffled item. A member joins once their item has arrived
and there is room in the party.

- Leon can join as an item only in Claude's game, and Dias only in Rena's. In the other game,
  or with `story_route: any`, neither is offered as an item.
- Some members compete for one party slot: you get one of Bowman or Precis (Precis rules
  Bowman out), and one of Ashton or Opera with Ernest (Ashton rules Opera out; Ernest only comes
  with Opera).
- Celine and Chisato always join when `character_items` is on.
- Claude or Rena, Noel, and the current story route's own story members join through the story
  as usual. They are never shuffled.

### Open world and hub mode

In open world with `character_items`, and always in hub mode, the party rules are looser:

- **No lockouts.** Any member but your story route's hero can be an item: the other hero (Rena
  in Claude's game, Claude in Rena's), Noel, Leon and Dias in either game, Bowman together with
  Precis, Ashton together with Opera and Ernest. With [the bench](#the-bench) (on by default)
  the pool holds every member you haven't excluded. Without it, the pool holds as many members
  as fit the party's 8 places with the hero and your starting character, picked at random when
  more are allowed. Members who aren't in the pool never join.
- **`exclude_characters`** keeps the members you list out of the pool (without the bench, to
  make room for the ones you want). The hero can't be excluded.
- **A healer is guaranteed** without the bench. Rena and Noel are the only healers, so if
  neither would be in your party, one of them takes a random pool member's place, unless you
  have excluded both. With the bench both are in the pool unless you exclude them.
- In hub mode no join scene runs, so with `story_route: any` both Claude and Rena are items; the
  one you pick at New Game is with you already, so their item does nothing.

**The join level.** In open world and hub mode, with or without `character_items`, a member who
joins takes the median level of the party members in its 8 places (the bench doesn't count),
raised through the game's own level-ups or lowered by the levels' average gains (never below the
heroes' level-1 values). Their equipment and SP stay as they come. Story mode keeps each member's
own join level.

### The bench

`bench` (on by default; open world with `character_items`, and hub mode) gives the party 12
places: the game's 8 (4 in front, 4 in reserve) and a **bench** of 4 more that Shattered Stars
0.02.51 keeps. Every member you haven't excluded is an item, and all of them fit.

- **A member who arrives with the 8 places full joins the bench**, with everything they have,
  instead of taking someone's place.
- **You choose who fights.** In the camp menu's **Party Order**, L1 / R1 flips the right-hand
  column between the reserve and the bench, and you swap members in and out as you swap the
  front and the reserve; your first pick stays held across the flip. **Tactics** flips the same
  way. Anyone but the hero can be benched, except a member who has joined only for a while.
- **Benched members are kept, not used.** They keep their equipment, levels and skills, and you
  can manage them in the camp menu (Equipment, Skills, Tactics, Status, their Specialty
  settings), but they aren't in play until you bring them in: no Private Actions, specialties,
  field spells or Link Combos, inns don't heal them, and the endings don't count them.
- **Lent for a fight they make alone.** When the story needs a benched member for a fight they
  make alone (Claude in Shingo Forest in Rena's game, Dias or Claude at the Lacour tournament),
  they are lent to the party for that fight and go back to the bench afterwards with what it
  gave them.
- Bench moves wait while the story holds the party (a Private Action, a party split for a
  scene). Your save keeps the bench; Shattered Stars 0.02.50 can't load a save with anyone on
  it.
- `starting_character: any` offers every member you haven't excluded.

Set `bench: false` for the classic 8 places (and a seed that also plays on Shattered Stars
0.02.50). Story mode, and open world without `character_items`, keep the story's party and
ignore the option.

### The starting character

`starting_character` (open world and hub mode; ignored in story mode) puts a member in the
party from the very start, as its leader. The hero stays in the party, right behind them: your
starting member takes the party's first place (the one you control in battle, first in every
menu) and the hero the second. Whatever order you leave in the camp menu (Party Order, and
Formation > Leader) comes back once a story scene that shuffles the party is over, and your save
keeps it. A starting character joins at your hero's level, so at level 1 on New Game.

| Value | Effect |
|---|---|
| `hero` (the default) | Nobody extra; the hero leads, as in the original |
| `any` | You choose at New Game. After the hero select (or straight away, when `story_route` picks the hero), a character select in the hero select's style shows all twelve members: move over their faces to see each one's picture and a short profile (the game's own race, occupation and favourite food, and a few lines about them; Claude and Rena keep their original bios). Members this world can't have in the party are dark and can't be picked. Picking the hero means nobody extra |
| A name (`celine`, `ashton`, ...) | That member starts with you; the same screen shows just them, to confirm. A member you name starts even if you have also excluded them |

When your pick's own character item arrives later, it finds them in the party already. Want
the generator to pick for you? Use `random`, as with any Archipelago option.

**In open world and hub mode you walk around as the leader.** On field maps you see the
party's leader instead of the hero, and the story calls the leader by name where it would name
the hero, in dialogue and in the speaker labels; in hub mode that includes the bosses' original
scenes in their chambers. Change the leader at any time in the camp menu (Combat > Formation >
Leader). The hero comes back on the world maps and for a scene that shows the leader as
themselves, names them, or needs one of the hero's own gestures. This changes only what you
see: the story runs as it does with the hero.

## Open world

With `world_mode: open_world` the story still runs in its order, and the world map's entrances
wait for their keys:

- **Area keys.** Each world-map entrance stays shut until its area's key arrives. Walking into
  one shows a banner naming the missing key, such as "Salva Pass needed". You start with the
  Arlia Pass and the Fun City Pass, and with the North City Pass and the Noel's House Pass too
  unless `expel_teleporter` and `expel_progression` are both on (the defaults).
- **Port Hilton's captain.** The sailor at Port Hilton becomes a captain who asks "Where to,
  matey?": **Cross**, **Hoffmann Ruins** (from the point in the story where its ship first
  sails until just before the Final Save Point), **El** (only when the story is ready to sail
  there, and only with every pass El needs) or **Not now**. He never leaves the party stranded
  on the Lacour continent, and every ship checks its landing's key before you pay the fare
  ("Map 231 Pass needed to sail").
- **The ship to El waits for its passes.** The voyage to El is one way until the disc change,
  so the game doesn't start it until you hold the El Beach, Eluria and Eluria Tower Passes (and,
  with `expel_teleporter` and `expel_progression` on, the North City, Noel's House and Red
  Crystal Passes), and names each one you are missing ("El Beach Pass needed to sail").
- **Central City and Noel's House.** The story brings you into both without going through
  their entrance; until the story is done there, their exit asks for that area's pass
  ("Central City Pass needed to leave"), so you can't lock yourself out on an island you can't
  leave yet.
- **The Expel teleporter.** Once the Synard flies, the old man in Fun City's arena can take you
  back to Expel at any time, and Arlia's guide brings you back to the arena
  (`expel_teleporter`, on by default).

## Hub mode

With `world_mode: hub` you don't play the story at all. New Game puts you in the world as it
stands after the Final Save Point in Fienal, with every chest still closed and every story boss
still waiting, and the whole game open around you.

- **Both worlds, every town, from the start.** Energy Nede by the Synard. Between the worlds,
  the old man in Fun City's arena takes you to Arlia, and Arlia's guide takes you back to the
  arena; on Expel, Virtual Expel's Synard flies. Nothing ever closes behind you: the
  story is held at its last chapter, so there is no disc change, no voyage and no way to miss
  anything for good.
- **Disc 2 from the start.** The hub begins where the story would be on Disc 2, so set up your
  Disc 2 image before New Game; without it, the game shows the insert-disc screen at once.
- **Where you start** (`hub_start`): Fun City (the default), Arlia, or `random_town`, one of 16
  towns of either world (Fun City, Arlia, Salva, Cross, Clik, Mars, Herlie, Port Hilton, Lacour,
  Linga, Eluria Colony, Central City, North City, Armlock, Giveaway and L'Aqua). The Synard is
  parked nearby.
- **One key per dungeon.** Towns are always open. Each dungeon stays locked until its key
  arrives: Salva Drift, Cross Cave, the Mars dungeon, Zand's Mansion, the Sanctuary of Linga,
  Hoffmann Ruins, the Lacour Front Line, Eluria Tower, the Mountain Palace, Lasguss Mountain and
  the Cave of Trials on Expel; the Synard Plant, the Cave of Red Crystal, the four Fields and
  Mihne Cavern on Energy Nede. A dungeon has a key only when something in it counts for your
  world; one with nothing in it is simply open. A locked door says which key it needs, such as
  "Salva Drift Key needed". With the default options there are 17 keys, 18 with the Cave of
  Trials.
- **The bosses wait in their dungeons** and play their original scenes when you walk in:
  Alen-Tax in Salva Drift, the Cross Cave guardians, Varmillion (Claude's game) or Azamgil
  (Rena's) in the Mars dungeon, Visseyer, Harfainx, Shin at the Front Line and in Eluria Tower,
  Berle in Eluria Tower and in Armlock, the Synard, Arcmene, the four Fields' guardians, Bark,
  Marsilio in Fun City's arena, Shigeo in Fun City, and the three groups in Fienal. Fight each
  whenever you like, in any order, once you have its dungeon's key (Berle in Armlock, Shigeo and
  Marsilio are in towns and need none). Beating one is a location (`boss_checks`).
- **Fienal opens on `fienal_requirement`:** the four Fields' bosses and Berle in Armlock (the
  default, `fields_and_berle`), the four Fields' bosses only (`fields`), or every boss outside
  Fienal (`all_bosses`). Fienal's entrance names the first one still missing ("Defeat Lover
  first"). Fienal has no key of its own, and your goal is behind it. With a limiter-off goal,
  Filia waits in Central City's City Hall from the start, and the Iseria Queen needs the Cave of
  Trials Key.
- **Party members are always items**, whatever `character_items` says; see
  [Party members](#open-world-and-hub-mode).

### Shigeo and Marsilio ask first

Both wait in Fun City, on your way between the two worlds and in the arena you'll want for
training, so neither is forced on you:

- **Shigeo.** Your first step into Shigeo's street on each visit to Fun City asks "Face Shigeo
  now?". **Yes** plays his scene and the fight. **No** lets you walk through the street (to the
  arena and the old man's way to Expel) without him, and he doesn't ask again until you have been
  out on the world map: to face him later, leave Fun City for the world map and come back.
- **Marsilio.** The arena's receptionist offers "Face Marsilio." next to "Register.", and the
  arena's back door asks "Face Marsilio now?" (once per visit, like Shigeo; the receptionist
  always offers). Yes takes you to his scene; there, the Clerk adds "Leave the arena." to his
  menu if you'd rather go back.
- **The arena can be used for training at any time:** "Register." gives the ordinary ranking
  battles, whether Marsilio is beaten or not.
- Saying no (or leaving) changes nothing: the boss stays unbeaten and you are asked again on
  your next visit. Loading a save asks again too.

### What the hub includes

Everything the game can still reach after the Final Save Point:

| Locations | Count |
|---|---|
| Chests | 217, or 290 with the Cave of Trials (Eluria Tower's and Fienal's included) |
| Gift sites | 7, or 9 with the Cave of Trials |
| Boss chambers | 22 (only one of Varmillion and Azamgil, unless `story_route` is `any`) |
| Other bosses | XINE in Lasguss Mountain, and 10 of the Cave of Trials' bosses with the cave |
| Shops, drops, crafts, killer moves, skills | As your options add them, as far as the hub reaches: the shops open after the Final Save Point (41 in Claude's game, 42 in Rena's), the enemy types met there (85 of the 89) |

**Left out:** the story's scenes outside the boss chambers, the join scenes, the gifts of the
story's earlier scenes, the arena's prize, old Clik and its shops, Lacour's tournament and its
war-time shops, the fights the story lets you lose (Dias at the tournament, Shin on the ship,
Marsilio's first fight in Fienal, Zand), the side quests' bosses the late game no longer offers
(Flarelizard, Nightmare and Ghost; XINE stays), and the Portal of Sealing.

Progression items in hub mode go only to those chests, the boss fights and, by
`drop_progression`, enemy drops (with skillsanity, guild checks and learn levels too); never to
shops, gifts or crafts. Killer-move levels and learn levels past level 30, and the guild sets of
2,700 Fol and up, are expected once Fienal is open.

## Shops

With `shuffle_shops`, each shop slot (`specials`: the 98 that sell something no other shop
does; `full`: every slot of every shop a script opens) is a location.

- **You get what you buy, and the check too.** The first time you buy from a slot, you get
  every copy you paid for, and the slot's check as well: its item comes to you or goes to its
  player, with the usual message. After that the slot is an ordinary entry.
- **The Archipelago mark.** In the shop's list, a slot still to check has the Archipelago logo
  after its name.
- **Order shops** deliver what you ordered; placing the order is the check.
- If a check brings you an item you already hold 20 of, it waits and arrives once you have room
  ("Got ..." then).

This works the same for seeds made with 0.1.x, which Shattered Stars 0.02.00 handled
differently (the first copy bought *was* the check). See
[What changed in 0.2.0](#what-changed-in-020).

## Enemy drops

With `enemy_dropsanity: first_drop`, the first time each enemy type drops anything in a random
encounter is a location.

- **`drop_progression`**: drops may hold progression items (`allow`, the default), get them
  first where the odds are fair (`force`: the drops that come about once in 13 kills or
  better), or never hold them (`never`).
- Whatever you choose, a drop only holds progression when its enemy can always be met again in
  random encounters from the point the logic expects you to get it, and only when it drops at
  least once in 64 kills without any Luck. Enemies that need a lucky party to drop at all
  (Magius, Ericodus, Shout, Starguarder, the slimes and others) never hold anything important.
  With `encounter_rate: 0` no drop holds progression.
- **`area_keys_in_drops`** (on by default): turn it off to keep every key (yours and other Star
  Ocean players') out of your drops, so no area ever waits on one fight's drop.

## Craftsanity

With `craftsanity`, each of the 553 distinct items Item Creation can make (cooking,
compounding, metalwork, customizing, art, authoring, alchemy, machinery and the super
specialties Master Chef, Publishing, Blacksmith and Reverse Side) is a location, checked the
first time you make it, whatever recipe you used. You keep the item you made, and the
location's item comes on top. These locations never hold progression items.

A few results only certain members can make (each member's own Customize weapons, Leon's rare
books), and the portraits and Publishing's books are each about one member, who must be in your
party; those hold only filler unless that member is sure to be with you. With
`no_fail_crafting` on, Item Creation never fails by chance.

## Killer moves

With `killer_move_checks`, each proficiency level of each killer move is a location: 133 levels
of 69 moves, for Claude, Bowman, Dias, Precis, Ashton, Opera, Ernest and Chisato. A level is
checked when the member has used the move often enough, at the next free moment after the
battle (most moves level up at 100 to 300 uses; the moves that count in small steps get three
checks, at 100, 200 and 300 uses).

`killer_move_progression` decides which levels may hold progression items: `never` (the
default), `hero` (the story route's hero's moves: Claude's, since Rena has none), or `any` (every
member the logic can count on: the hero, the starting character, and members who join through
their character item, which then becomes a progression item). Moves learned by level 30 are
expected from the disc change, the rest from the Final Save Point (on a [hell seed](#hell-seeds),
all of them from the Fun City arena). A member who may never join has no killer-move checks; one
who joins only by your choice in the story holds only filler there.

## Skillsanity

`skillsanity` turns skills, killer moves and spells into items:

| Value | Effect |
|---|---|
| `none` (default) | As the original |
| `skills_only` | Buying a set at a skill guild no longer teaches its skills: it is one check per skill in the set ("Skill Guild: Sensibility 2 (1 of 4)"). Each skill is an item ("Skill: Perseverance"): once you have it, any member can learn it with SP on the Skills page, as buying its set used to allow. Members keep the skills they join with. With the Cave of Trials on, its Level 11 prize (Float) is a check too |
| `special_only` | Every member knows the first killer move or spell of their list from the start (Claude's Air Slash, Rena's Heal, ...); every other one is an item ("Claude: Shooting Stars"), and each place it was learned is a check instead: a level ("Claude Lv 7: Shooting Stars"), Precis's and Opera's machines, and the Cave of Trials' learn chests. A member who joins past a level gets that level's checks at once. A move you receive before its member joins waits for them. With killer-move checks, a move's levels need the move |
| `insanity` | Both |

**Guild checks and learn levels may hold progression items** (keys, characters, other worlds'
progression), like any other check, once the logic can count on reaching them:

- **A guild check** is expected once one of its set's guilds is in reach: the sets up to
  1,600 Fol right away, the 2,700-4,500 Fol sets once you've finished Hilton and the Hoffmann
  Ruins (story mode: from the disc switch; hub mode: once Fienal opens). A guild you can lose for
  good, like old Clik's, never counts, and with `expel_progression` off only the guilds on Energy
  Nede do.
- **A learn level** counts as killer-move levels do: up to level 30 from the disc switch, higher
  ones from the Final Save Point (hub mode: from the start, higher ones once Fienal opens), and
  only for members the logic can count on. A member who joins through their character item needs
  it first, so that character becomes a progression item.
- Not with `encounter_rate: 0` (no battles, so no Fol and no levels), unless the seed is a
  [hell seed](#hell-seeds). Precis's and Opera's machines, the Cave of Trials' learn chests and
  Float never hold progression, and shops never do.

Item Creation needs its specialty's skills, so with craftsanity a craft may wait for the right
skill items.

With shop slots on, `skillsanity_shop_share` (default 10%) puts some of your skill and move
items in your shops, sold as themselves with the Archipelago mark: a move for its learn level
x 100 Fol (one an item teaches, 5000; a Cave of Trials prize, 8000), a skill for its set's price
divided by the set's size (Perseverance: 1600 / 4 = 400 Fol), Float for 900. You buy one; that
is the slot's check.

## Difficulty

`difficulty` sets the game's difficulty, and New Game uses it without asking: `earth` (the
default), `galaxy` (monsters have half again their HP and attack) or `universe` (twice their HP
and MP, two thirds more attack, and more intelligence, accuracy and evasion). With `any`, New
Game asks, offering all three; normally that choice appears only once more than half of the
Voice Collection has been heard. EXP, Fol, drops and encounters stay the same, so the logic is
the same on every difficulty.

## Hell seeds

Set `encounter_rate: 0` (no random battles) together with `killer_move_checks`, or with
`skillsanity` `special_only` or `insanity` (learn levels), and you get a **hell seed**: a game
where the grind is meant to be preposterously hard. With no random battles, the only fights you
can train in are the **Fun City Fighting Arena**'s (the desk in Fun City: Duel, Bullying, Team
and Survival; any member in your party may enter). On a hell seed:

- **The arena is free.** Registering normally costs 2,000 Fol; a hell seed waives the fee. Every
  arena fight still pays its EXP and Fol, and killer-move uses count there as in any battle. On
  every other seed the fee stays.
- **Killer-move levels, learn levels and skill-guild checks count from the arena** and may hold
  progression items: in story mode from the disc switch, in open world once you've finished the
  Cave of Red Crystal (and hold the Fun City Pass, which you start with), in hub mode from the
  start. A key you need may well sit behind a long stretch of arena fights. That's the point.
- **Setup warns you.** When you choose such a seed, Shattered Stars 0.02.51's setup shows "Hell
  Seed: no random battles. Killer-move levels and learn points are trained in the Fun City arena,
  which is free on this seed. Expect a very long grind." on its Archipelago page, and the game
  logs the same line. Shattered Stars 0.02.50 plays a hell seed the same way, without the
  warning.

`encounter_rate: 0` without those options is an ordinary seed: the arena's fee stays, and guild
checks, learn levels and drops hold no progression.

## Expel

Expel is never permanently lost:

- In open world, the old man in Fun City's arena can take you back to Expel at any time once the
  Synard flies (`expel_teleporter`, on by default). Fun City lies on a different island of
  Energy Nede from Central City, so you reach him once you can fly.
- In story mode, the same character reopens it, as Virtual Expel, once you have saved at the
  game's last save point.
- In hub mode, both worlds are open from the start.

With `expel_progression` off, no other player's progression items are placed in Expel. In open
world, this world's own area keys that the Expel story needs can still be there, since the story
collects them before Expel closes behind you.

## Options

| Option | Choices | What it does |
|---|---|---|
| `goal` | `indalecio` (default; `final_boss` works too), `indalecio_limiter_off`, `indalecio_limiter_off_and_iseria_queen` | Which Indalecio counts, and whether the Iseria Queen must fall first. See [The goal](#the-goal) |
| `world_mode` | `story` (default), `open_world`, `hub` | See [The three world modes](#the-three-world-modes) |
| `hub_start` | `fun_city` (default), `arlia`, `random_town` | Hub mode only: where New Game puts you |
| `fienal_requirement` | `fields_and_berle` (default), `fields`, `all_bosses` | Hub mode only: the bosses that open Fienal |
| `expel_teleporter` | on (default), off | Open world only: once the Synard flies, Fun City's arena takes you back to Expel. Off: Expel reopens only at the late-game save point, and until then holds only non-progression items and this world's own area keys; you also start with the North City Pass and the Noel's House Pass |
| `shuffle_shops` | `off` (default), `specials`, `full` | Turns shop slots into locations; you get what you buy and the check. See [Shops](#shops). Never progression |
| `story_route` | `any` (default), `claude`, `rena` | `claude` or `rena` starts that hero's game and leaves the other one's locations out. New Game then skips the hero select and the naming screen: the hero keeps the default name (the camp menu's Status page can rename them). With `any`, you choose at New Game, and a location that belongs to only one story holds filler. In hub mode it also picks the hero's own boss (Varmillion or Azamgil). Called `protagonist` in 0.1.x |
| `starting_character` | `hero` (default), `any`, or a member: `claude`, `rena`, `celine`, `bowman`, `dias`, `precis`, `ashton`, `leon`, `opera`, `ernest`, `noel`, `chisato` | Open world and hub mode: a member who joins at the start and leads. See [The starting character](#the-starting-character) |
| `exclude_characters` | a list of names, for example `["Bowman", "Leon"]` (default: none) | Open world with `character_items`, and hub mode: these members are never items, so they never join. The hero can't be excluded |
| `bench` | on (default), off | Open world with `character_items`, and hub mode: 12 places, the game's 8 and a bench of 4, so every member you haven't excluded is an item. Needs Shattered Stars 0.02.51; off keeps the classic 8. See [The bench](#the-bench) |
| `missable_locations` | `exclude` (default), `include`, `remove` | See [Missable locations](#missable-locations) |
| `expel_progression` | on (default), off | Off: keep other players' progression items out of Expel; in open world you then also start with the North City Pass and the Noel's House Pass |
| `cave_of_trials` | off (default), on | Adds the Cave of Trials, a very hard post-game dungeon, with its chests, gifts and bosses (in hub mode behind the Cave of Trials Key) |
| `boss_checks` | on (default), off | Defeating a boss (story, side quest or Cave of Trials) is a location. Indalecio is the goal, never a location |
| `character_items` | off (default), on | Party members join through items. See [Party members](#party-members). Always on in hub mode |
| `enemy_dropsanity` | `off` (default), `first_drop` | The first time each enemy type drops anything in a random encounter, that drop is a location. Called `drops_as_checks` in 0.1.x |
| `drop_progression` | `allow` (default), `force`, `never` | With `enemy_dropsanity`: whether drops may hold progression items. See [Enemy drops](#enemy-drops) |
| `area_keys_in_drops` | on (default), off | With `enemy_dropsanity`: off keeps every Star Ocean key out of your drops |
| `craftsanity` | off (default), on | Each of the 553 items Item Creation can make is a location. Never progression |
| `no_fail_crafting` | off (default), on | Item Creation never fails by chance |
| `killer_move_checks` | off (default), on | Each proficiency level of each killer move (133) is a location |
| `killer_move_progression` | `never` (default), `hero`, `any` | With `killer_move_checks`: whose killer-move levels may hold progression. See [Killer moves](#killer-moves) |
| `skillsanity` | `none` (default), `skills_only`, `special_only`, `insanity` | Skills, and / or killer moves and spells, as items. See [Skillsanity](#skillsanity) |
| `skillsanity_shop_share` | 0 to 100 (default 10) | With `shuffle_shops` and skillsanity: the share of your skill and move items sold in your shops |
| `traps` | 0 to 100 (default 0) | The percentage of this world's filler replaced by traps |
| `exp_multiplier`, `fol_multiplier` | 1 to 10 (default 1) | Multiplies the EXP or Fol a battle awards |
| `encounter_rate` | 0 to 200% (default 100) | How often random encounters happen. 0 turns them off; with killer-move checks or learn levels that makes a [hell seed](#hell-seeds) |
| `difficulty` | `earth` (default), `galaxy`, `universe`, `any` | See [Difficulty](#difficulty) |
| `death_link` | off (default), on | See [DeathLink](#deathlink) |

With every option at its default, the game plays like the original apart from the shuffled
chests, gifts and boss checks.

## DeathLink

With `death_link` on, your game is linked to every other DeathLink game in the multiworld. A
death in any of them knocks out your party, and a Game Over in your game sends a death to
everyone else.

**A DeathLink is sent only on a real Game Over**, and never for a death that a received
DeathLink caused.

**A received DeathLink waits for a safe moment.** In free play it knocks out your party at
once. In a menu, a scene or a vehicle ride it waits until you're back in control. In a battle,
the party falls and the Game Over follows. It is dropped during the ending and the credits, and
when you are on the title screen or loading a game.

### The fights you can't win

Five story fights don't end like ordinary battles. They are handled like this:

| Fight | Kind | DeathLink | Boss check |
|---|---|---|---|
| Dias at the Lacour Tournament of Arms | The story lets you lose | Losing sends nothing. A DeathLink received during the fight waits until you're back in free play | Counts when the fight ends, win or lose |
| Shin on the ship to El | The story lets you lose | The same | Counts when the fight ends, win or lose |
| Marsilio's first fight in Fienal | The story lets you lose | The same | Counts when the fight ends, win or lose |
| Shin at the Lacour Front Line | Timed: survive until time runs out | Normal: a wipe is a real Game Over and sends a DeathLink, and a received one knocks out your party | Counts when you survive until time runs out |
| Berle in Eluria Tower | Timed: survive until time runs out | Normal, as above | Counts when you survive until time runs out |

Running away from a fight never counts as a boss check. The goal needs a real victory over
Indalecio. The three fights the story lets you lose are not part of hub mode.

## Installing the .apworld

1. Download `so2.apworld` and `Star.Ocean.The.Second.Story.yaml` from the world's own
   release, [so2.apworld 0.2.2](https://github.com/IronJade/Shattered-Stars/releases/tag/apworld-v0.2.2) (the game's releases don't carry them any more).
2. Copy `so2.apworld` into the **`custom_worlds`** folder of your Archipelago installation. If
   an older `so2.apworld` is there, replace it.
3. Make sure there is only one copy of the world: not one in `custom_worlds` and another
   (or an unpacked `so2` folder) in `worlds`. Archipelago refuses to load the same game twice.

## Making your YAML

**With a form (easiest).** With `so2.apworld` installed, open the Archipelago Launcher and
choose **Options Creator**. Pick *Star Ocean: The Second Story*, set each option with buttons
and sliders (every option shows its description), and export the YAML. If the world is
later accepted into Archipelago itself, archipelago.gg will also offer a Player Options
page for it in the browser.

**By hand.** Start from the template, `Star Ocean The Second Story.yaml`, and set the options
you want. The game's name contains a colon, so it must be quoted, as the template already does.
A short example:

```yaml
name: YourName
game: 'Star Ocean: The Second Story'
requires:
  version: 0.6.8
'Star Ocean: The Second Story':
  world_mode: hub
  hub_start: fun_city
  starting_character: any
  boss_checks: 'true'
  shuffle_shops: specials
  traps: 10
  death_link: 'false'
```

**An older YAML** still works, with two renamed options: 0.1.x's `protagonist` is now
`story_route`, and `drops_as_checks` is now `enemy_dropsanity`. The generator ignores the old
names (it warns that they aren't valid option names), so those options would fall back to their
defaults: rename them.

Put the finished YAML wherever your group collects player files, for example the `Players`
folder of your Archipelago installation.

## Generating

Generate the multiworld as you would for any Archipelago game, using Archipelago's own
generator on a computer that has the `.apworld` installed. Archipelago writes one zip file for
the whole multiworld, `AP_<number>.zip`, into its `output` folder (on Linux
`~/.local/share/Archipelago/output`, on Windows `C:\ProgramData\Archipelago\output`). Inside
it, each Star Ocean player has a seed file ending in **`.apso2`**.

Shattered Stars reads the zip as it is, so there is no need to unpack it. A loose `.apso2` (one
the host sent you on its own, say) works just as well.

## Hosting or joining

Host the generated zip the way your group normally does: locally with Archipelago's own server,
or on a hosting site that accepts uploaded games. To join, you need from the host:

- the server address, for example `archipelago.gg:38281`;
- your slot name;
- the room's password, if it has one;
- the generated zip, or your own `.apso2` seed file from it.

## Connecting

Open the setup program and go to its **Archipelago** page:

<p align="center">
  <img src="https://raw.githubusercontent.com/IronJade/Shattered-Stars/main/docs/images/setup-archipelago.png" alt="The setup program's Archipelago page with a seed file loaded and a server address, slot name and password filled in" width="640">
</p>

1. **Seed:** press **Browse...** and choose the `AP_<number>.zip` (Browse opens Archipelago's
   `output` folder) or your `.apso2` file. If the zip holds seeds for more than one Star Ocean
   player, a **Your slot** list appears: choose yours. Setup checks the seed at once and shows
   the seed and slot it was made for, and how many locations it has, or says what is wrong. A
   [hell seed](#hell-seeds) gets a warning under that line.
2. **Address:** enter the room's server address, such as `archipelago.gg:38281`. A `ws://` or
   `wss://` address works too; use whatever the host gives you.
3. **Slot name:** the box hints at the slot name stored in your seed. Enter your slot name.
4. **Password**, if the room has one. Tick **Show** to check what you typed.

Press **Save & Play**. The game connects when it starts and plays as usual. There is no
separate client window. Checks found while the game can't reach the server are sent later.

### Turning Archipelago off

The switch beside **Archipelago** in setup's list of pages turns Archipelago off and on. Off,
the game plays exactly as the original, on your ordinary memory cards; your seed, server and
slot stay filled in for when you switch it back on. **Clear** next to the seed removes the seed
itself.

### Your saves: a memory card for each seed

**Each seed (and slot) saves on its own memory card**, which the game makes the first time you
play it: `archipelago/<seed>_<slot>/memcard1.mcd`, beside the game's `config.ini` (see
[Installing and Setup](Installing-and-Setup#where-your-files-live)). Your ordinary memory cards
aren't inserted while a seed is chosen, so a finished or unrandomized game can't be loaded into
the seed by mistake, and the seed can never save over your ordinary saves.

- The Archipelago page shows the seed's card under **Saves**: "No saves yet", or how many saves
  it holds, with **Open folder** for backups.
- **Saves from an earlier version are adopted.** Shattered Stars 0.02.00 saved a seed's games on
  your ordinary card. The first time 0.02.50 plays that seed, it copies those saves (only that
  seed's and slot's) onto the seed's own card. Your ordinary card itself isn't changed.
- Each save's Archipelago progress is kept in a small file beside the card (for example
  `memcard1.slot03.ap.json`), so the card itself stays compatible with the console. Keep these
  files with the card when you back it up or move it. Since 0.02.51 it also keeps items still
  waiting to be given, the bench, and Dias between his tournament fights; Shattered Stars 0.02.50
  refuses such a save rather than lose them.

**Only the seed's own saves load.** With a seed, a save loads only when the file beside it names
this seed and slot and shows the save is exactly as the game wrote it. A save from another seed
or from an ordinary game, a save copied without its `.ap.json` file, or a save changed with a
save editor is refused the way the game refuses a damaged save ("The data could not be loaded.
Your data may be faulty."), and the game's log says which it was. This guards against accidents
and casual editing; it is not anti-cheat.

### No debugging in an Archipelago game

While a seed is being played, the game's testing switches are ignored: the debug console
(`--debug`), starting on a chosen map or in a battle, scripted input and capture, and memory
cards named on the command line. The game logs what it ignored. To use them, turn Archipelago
off.

## Playing offline

You can also play a seed on your own, with no server: choose the zip or the `.apso2` on the
**Archipelago** page and leave the **Address** empty. Your locations are shuffled and checked,
items from your own world are given to you, and your checks are kept in your save, to be sent if
you later play with a server.

For a one-off run without changing your settings, start the game with `--ap-seed FILE`
instead (FILE is the zip or the `.apso2`; add `--ap-seed-entry NAME` to pick the seed in a zip
that holds several Star Ocean seeds). A server, slot and password already saved by setup still
apply.

## While you play

- A chest or gift whose item belongs to someone else still plays its original scene, but the
  message names the real item and its owner: "Sent Hookshot to Alice." Your own items read as
  usual, for example "Got Blueberries."
- Items from other players arrive the next time you are free to walk around, with a banner such
  as "Received Revival Card from Alice." An item that doesn't fit (twenty of a kind already)
  steps aside with one banner ("Inventory full: Blackberries waiting") and arrives once there is
  room; everything else keeps coming meanwhile.

  <img src="https://raw.githubusercontent.com/IronJade/Shattered-Stars/main/docs/images/ap-item-received.png" alt="A banner reading 'Received Blueberries from Bob' over the village of Arlia" width="560">

- A craft is checked as you make it, and a skill-guild purchase as you buy it. A killer-move
  level is checked at the next free moment after the battle (or after loading a save).
- On the French and German discs, these messages use the game's own font. A symbol it doesn't
  have, in a player's or an item's name, shows as a similar character or a space.
- Setup's [Gameplay](Installing-and-Setup#gameplay) page can make text appear at once and move
  on by itself, which helps through the story's long scenes. It never answers a choice for you.
- **Trackers** can follow along, next to the game: Universal Tracker and the PopTracker pack
  ([Trackers](Trackers)). They update while the game is online; checks you make offline show once
  it reconnects.

## Known issues

- **Some of the new texts are English on every disc**: Port Hilton's captain ("Where to,
  matey?"), the hub's questions ("Face Shigeo now?" and the arena's options), the character
  select's descriptions, the hell seed's warning, and the banners (as before).
- **The leader in open world and hub mode:** a dash's afterimages still show the hero, and a long
  scene keeps the hero until free play (for example the opening's scenes, or a chamber's scene
  after the hero's own gesture).
- **At Port Hilton**, taking the captain's Hoffmann Ruins option the first time skips the
  Hoffmann ship's soldier's first-time lines. Nothing else depends on them.
- **With skillsanity**, the Cave of Trials' learn chests still say the member has learned the
  move; the move comes as its item.
- **In hub mode**, a boss scene in which the story splits the party is fought with the whole
  party.
- **On the French and German discs**, a very long item name in a shop list may run under the
  Archipelago mark.
- **Entrance shuffle** is designed but not built yet.
- **Trackers update only while the game is online**
  ([Trackers](Trackers#trackers-update-when-the-game-is-online)).

## Troubleshooting

| Problem | What to do |
|---|---|
| "Did not load ... as its game ... is already loaded." | The world is installed twice. Keep one copy only, in `custom_worlds` |
| Setup won't accept your disc image | The North American, European, French and German discs work, both from the same release; Japanese discs don't yet. See [Installing and Setup](Installing-and-Setup#your-disc-images) |
| "unsupported version 2" (or 3) | The seed uses 0.2.0's features and your Shattered Stars is older. Install 0.03.00 |
| "unsupported version 4" | The seed has [the bench](#the-bench) (0.2.1, on by default in open world with `character_items` and in hub mode) and your Shattered Stars is older. Install 0.03.00, or generate again with `bench: false` |
| An open-world seed can't be finished | It was probably generated with `so2.apworld` 0.1.0. Install 0.2.2 and generate again (see [What changed in 0.1.1](#what-changed-in-011)) |
| "No more spots to place ... Unplaced items: North City Pass" (or Noel's House Pass) | An open-world game with `expel_progression` or `expel_teleporter` off couldn't be generated with 0.2.0. Install 0.2.2 (since 0.2.1 you start with those passes) |
| The connection is refused | Check the slot name and password against what the host gave you. The game reports the server's refusal rather than failing silently |
| Setup says the seed isn't valid, or the zip holds no Star Ocean seed | Star Ocean wasn't one of the games generated, or the file is damaged. Get a fresh copy from the host |
| "This archive holds 2 Star Ocean seeds: choose your slot below." | Pick yours in the **Your slot** list |
| "The data could not be loaded. Your data may be faulty." when loading a seed's save | The save isn't this seed's, or was changed or copied without its `.ap.json` file. See [Your saves](#your-saves-a-memory-card-for-each-seed) |
| Your ordinary saves aren't there | A seed is chosen, so the seed's own card is used. Turn Archipelago off, or clear the seed |
| An option in your YAML has no effect | 0.1.x names: `protagonist` is now `story_route`, `drops_as_checks` is now `enemy_dropsanity` |
| You don't receive items | Items arrive in free play, not during scenes, menus or battles |
| A tracker can't connect, or shows something unexpected | See [Trackers](Trackers#troubleshooting) |

## What changed in 0.2.2

Version 0.2.2 of the world goes with Shattered Stars 0.03.00 and the PopTracker pack 1.0.0, each
released on its own ([so2.apworld 0.2.2](https://github.com/IronJade/Shattered-Stars/releases/tag/apworld-v0.2.2);
the game: [Shattered Stars 0.03.00](https://github.com/IronJade/Shattered-Stars/releases/tag/v0.03.00);
the pack: [PopTracker pack 1.0.0](https://github.com/IronJade/Shattered-Stars/releases/tag/poptracker-v1.0.0)).
It adds tracker support ([Trackers](Trackers)):

- **Universal Tracker without your YAML.** UT rebuilds your room from its own data (every option,
  your party pool, the hub's start town), and lists exactly what the generator counted on, in
  story order. `/explain <location>` says what a location needs: the world's rules are now data
  (Archipelago's Rule Builder), with the same logic as 0.2.1's. A room UT can't rebuild is refused
  with a message naming the `so2.apworld` to install.
- **The room carries its logic** (about 4 to 35 KB of slot data), which the PopTracker pack
  evaluates: green in logic, red not, the same as UT.
- **Story mode follows the story** in both trackers, with Shattered Stars 0.03.00 connected
  ([Story awareness](Trackers#story-awareness-story-mode)).
- **Older rooms** track in UT with 0.2.2 installed: 0.2.1's exactly, 0.2.0's too, with a warning
  when Skillsanity is on; a 0.2.0 hell seed is refused ([Older rooms](Trackers#older-rooms)). In
  PopTracker an older room is a checklist.

**Your seeds.** The same options and seed number make the same seed as 0.2.1: the same items in
the same places and the same spoiler. Only the version number written into the seed changes, and
the room's slot data gains three keys for trackers. There are no new options.

And in Shattered Stars 0.03.00:

- **Trackers follow your game.** While it is connected to its room, the game keeps the map you're
  on (and your position on a world map) and the story's chapter in the server's data storage, so
  trackers can switch to your map and follow the story. Offline, with Archipelago off or with
  another room's seed it writes nothing, and nothing goes into your saves.
- **Maps for PopTracker from your own disc**: setup's **Render tracker maps for PopTracker**
  button, or `--render-tracker-maps`, draws every area, every room (with a lettered box at each
  doorway) and both world maps into PopTracker's override folder for the pack
  ([Maps from your own disc](Trackers#maps-from-your-own-disc)).

## What changed in 0.2.1

Version 0.2.1 of the world goes with Shattered Stars 0.02.51, and is the first released on its
own ([so2.apworld 0.2.1](https://github.com/IronJade/Shattered-Stars/releases/tag/apworld-v0.2.1); the game: [Shattered Stars 0.02.51](https://github.com/IronJade/Shattered-Stars/releases/tag/v0.02.51)):

- **The bench** (`bench`, on by default): in open world with `character_items` and in hub mode,
  every member you haven't excluded is in the pool, with no random choice, and up to 12 fit. The
  healer guarantee is no longer needed then. More members also means more killer-move levels,
  crafts and learn levels as checks. A bench seed needs Shattered Stars 0.02.51. See
  [The bench](#the-bench).
- **Open-world games that wouldn't generate.** With `expel_progression` off (and the teleporter
  on), a Star Ocean world alone, or with only other Star Ocean players, stopped with "No more
  spots to place ... Unplaced items: North City Pass"; with `boss_checks` off and either Expel
  option off, the same happened to the Noel's House Pass. In open world you now start with both
  passes, as well as the Arlia and Fun City passes, unless `expel_teleporter` and
  `expel_progression` are both on.
- **Skillsanity's guild checks and learn levels may hold progression** (see
  [Skillsanity](#skillsanity)). This also fixes a rare mix that couldn't be generated at all.
- **Hell seeds** (see [Hell seeds](#hell-seeds)).

**Your seeds.** Seeds made with 0.2.0 keep working on Shattered Stars 0.02.50 and 0.02.51. With
0.2.1, the same options and seed number make the same seed as 0.2.0 unless the bench applies (on
by default; `bench: false` gives the seed from before the option), an open-world player has
`expel_teleporter` or `expel_progression` off, a player has `skillsanity` on, or a player's seed
is a hell seed.

And in Shattered Stars 0.02.51:

- **Your starting member leads from the first place**, the hero second, and the order you set
  in the camp menu stays (see [The starting character](#the-starting-character)).
- **The bench** in Party Order and Tactics, and **the hell seed's warning** in setup.
- **Items still waiting to be given are kept when you save.** A check that gives several items
  (a skill guild set, a boss, a craft, a killer-move level...) hands them out one at a time. A
  save made while some were still waiting lost them for good; now they come back when you load
  it, and nothing is given twice. Loading an older save after a game over, or after quitting
  without saving, also gives you the items of the checks you had made since.
- **A full inventory no longer holds up everything else you receive**: the item that doesn't fit
  waits ("Inventory full: Blackberries waiting") and everything behind it keeps arriving.
- **A shut door no longer lets you walk off the map** (open world and hub mode): at a door the
  seed keeps shut, you now stop at the doorway.
- **A full party no longer loses a member** when someone joins just for a fight (Claude in
  Shingo Forest in Rena's game, Dias or Claude at the Lacour tournament) and the scene moved
  people around meanwhile.
- **Dias keeps his own levels and gear through the Lacour tournament** (a Dias you received or
  started with), instead of coming out at the tournament's level with his starting equipment.

## What changed in 0.2.0

Version 0.2.0 of the world, released with Shattered Stars 0.02.50, is the world's first big
revision:

- **Hub mode**, a new `world_mode`, with `hub_start`, `fienal_requirement` and the dungeon keys.
- **Goals:** `goal` gains the two limiter-off goals; `final_boss` is now called `indalecio`.
- **The open-world party:** no lockouts, the other hero and Noel as items, three more join
  sites, `exclude_characters`, the healer guarantee, `starting_character` with the character
  select, and joining at the party's median level (also in hub mode).
- **New kinds of locations:** craftsanity (with `no_fail_crafting`), killer-move checks (with
  `killer_move_progression`) and skillsanity (with `skillsanity_shop_share`).
- **Enemy drops may hold progression** (`drop_progression`, `area_keys_in_drops`).
- **`difficulty`**, in every world mode.
- **Gift sites never hold progression**, in any mode. Before, a few gift sites the late game
  reaches could.
- **The Cave of Trials' three learn chests are no longer locations.** They are where Leon learns
  Extinction, Ashton tri-Ace and Celine Meteor Swarm, not real chests, so a seed with
  `cave_of_trials` has 3 locations fewer (skillsanity brings them back as learn points).
- **The Cave of Trials' locations are named by their real floor** ("Cave of Trials Level 7:
  ..." instead of "Level 1" for all of them), with the treasure vault and the Weird Beast's arena
  named as such. The ids are the same; rooms generated before keep their old names.
- **Renamed options:** `protagonist` is `story_route`, `drops_as_checks` is `enemy_dropsanity`.

And in Shattered Stars 0.02.50, for every seed, old ones included:

- **Shop slots give what you bought and the check** (0.02.00 gave the slot's item instead of
  the first copy you paid for, and a slot whose check was Fol lost that Fol when the purchase
  was paid), and the Archipelago mark shows which slots are still to check. 0.02.00 meant to
  show those slots in another colour, but the colour never appeared.
- **Port Hilton in open world** (0.1.1 seeds too): the captain's destinations, and no more
  stranding. In 0.02.00, Port Hilton's sailor refused to sail back to Cross for a while after
  you first reached Lacour, which could leave you stuck with a key you needed out of reach.
- **The Cave of Trials' learn chests in 0.1.x seeds** with `cave_of_trials` read and wrote
  outside the party's records in 0.02.00. Now nothing is learned or written there, and the chest
  gives or sends its location's item.
- Seeds from the generator's zip, the slot list, a memory card per seed, the save guard, the
  on / off switch and the end of debugging in an Archipelago game, all described above.

## What changed in 0.1.1

Version 0.1.1 of the world (with Shattered Stars 0.02.00) changed open world only. Story mode,
the item pool, and the location names and ids were the same as in 0.1.0. With 0.1.0 (the
0.01.00 release), about 4 in 10 open-world seeds couldn't be finished; if your multiworld has an
open-world Star Ocean slot generated with 0.1.0, generate it again.

- **Two ship landings were missing from the logic.** The Lacour continent is reached only by
  the Herlie ship, which lands at Port Hilton (the Hilton Pass opens it), and Hoffmann Ruins
  only by the Hilton ship, which lands on its beach (the Map 231 Pass). 0.1.0 could put the
  Hilton Pass in Lacour Castle, or the Map 231 Pass in Hoffmann Ruins: places you can't reach
  without those very keys. 0.1.1 asks for the landing's key in every area reached by ship.
- **The teleporter doesn't reach Expel's islands.** Fun City's old man takes you back to
  Expel, where you can walk the Cross continent and take the Herlie ferry to the Lacour
  continent, but not reach Hoffmann Ruins or the El continent, which only the story's ships
  reach. 0.1.0 counted Hoffmann Ruins' 26 chests and Eluria Colony's 2 gifts as reachable again,
  so they could hold another player's progression, or a key needed after the disc change that
  was lost for good if you hadn't picked it up. In 0.1.1 they hold only the Expel story's own
  keys, which you collect before Expel closes.
- **The ship to El waits for the El passes.** 0.1.0 could leave the El Beach, Eluria or Eluria
  Tower Pass behind on Expel. Now the logic puts those passes where you can still get them
  before you sail, and the game doesn't start the voyage until you hold all three.
- **The teleporter needs the Synard.** 0.1.0 counted on Fun City's teleporter from your first
  walk on Energy Nede. With `expel_teleporter` and `expel_progression` both on (the defaults),
  the ship to El also waits for the North City, Noel's House and Red Crystal Passes, which the
  story needs before then.
- **No walking out of Central City or Noel's House too early.** Their exit asks for that area's
  pass until the story is done there.

## For developers

The seed format, the client's design and the world's logic are documented in the repository:
[docs/apworld/seed_format.md](https://github.com/IronJade/Shattered-Stars/blob/main/docs/apworld/seed_format.md),
[docs/apworld/apworld.md](https://github.com/IronJade/Shattered-Stars/blob/main/docs/apworld/apworld.md)
(its trackers: [Trackers](Trackers#for-developers)) and the world's source in
[apworld/so2](https://github.com/IronJade/Shattered-Stars/tree/main/apworld/so2).
