# Star Ocean: The Second Story

Star Ocean: The Second Story is a PlayStation RPG (North American release) following Claude
and Rena across the planet Expel and the forgotten world of Energy Nede. This world is played
through Shattered Stars, a from-scratch port that reads your own disc images at run time;
there is no ROM or game data bundled with it.

## What does randomization do to this game?

Every chest and every gift site (a scene, a Private Action, a prize table — anything that
hands you an item or Fol outside a chest) holds an item from the multiworld instead of its
original contents. The story's own key items are never touched: they are script flags, not
objects, so the story always goes on no matter what you find.

On top of that baseline, six more kinds of locations can be turned on:
- defeating a boss,
- a party member joining you,
- the first time an enemy type drops anything in a random encounter,
- buying a slot at a shop for the first time,
- making an item with Item Creation for the first time (craftsanity),
- a killer move reaching a new proficiency level.

## What is the goal?

The goal is always beating Indalecio, the final boss, in Fienal. The `goal` option picks which
version of him counts:

- **`indalecio`** (the default; `final_boss` still works): either version of the fight. This is
  how the game normally ends.
- **`indalecio_limiter_off`**: only the harder version, with his limiter off. In the original you
  get it by meeting Filia in Clik early in the story (a Private Action that's easy to miss) and
  meeting her again near the end. Here the Clik meeting is done for you; after you've saved at
  the Final Save Point in Fienal, enter Central City as a Private Action (from the world map)
  and walk into City Hall, where Filia's scene takes the limiter off (and gives you Israfil's
  Tear). The way from the Final Save Point to Indalecio stays shut until then ("Talk to Filia
  in Central City first").
- **`indalecio_limiter_off_and_iseria_queen`**: as above, and you must defeat the Iseria Queen
  first. She has no fixed battle: she's summoned by the song "The Evil Melody", which you
  compose with the Silver Trumpet that G-Celesta gives you at the bottom of the Cave of Trials,
  and play with a Conductor's Baton through a party member at Musical level 10 with the Pitch
  and Sense of Rhythm talents. So this goal means clearing the Cave of Trials, whether or not
  `cave_of_trials` adds its locations to your world. The Silver Trumpet can't be sold until the
  song is composed, so you can't lose it by accident. The way to Indalecio stays shut until
  she's beaten ("Defeat the Iseria Queen first").

If `cave_of_trials` is on, the Cave of Trials becomes available after that late-game save
point as extra, very hard optional content with its own chests, gifts and boss fights. It
doesn't change the goal.

## What items can you find?

- **Equipment and consumables.** Most of what you find is an ordinary game item — weapons,
  armor, accessories, recovery items, and so on. The game's own classification decides whether
  a given item is *useful* (worth watching for) or plain *filler*; weapons, armor and a few
  other strong item ranges count as useful, everything else as filler. The filler item's name
  is **Blueberry**.
- **Fol.** Many gift sites and some chests give Fol bundles instead of an item. These are
  filler.
- **Characters**, if `character_items` is on. Instead of joining you at their story scene, a
  party member arrives as an item — see "Who can join your party?" below. A character is
  usually just *useful*; with `killer_move_progression: any`, a member whose killer moves hold
  progression items is a progression item too, since you need them to reach those checks.
- **Traps**, if `traps` is above 0%. A share of the filler pool becomes one of five traps
  instead:
  - **Ambush Trap** — your next free step on a map with random encounters starts a forced
    battle.
  - **Fol Loss Trap** — you lose 10% of your current Fol.
  - **Poison Trap** — poisons your active party.
  - **Paralysis Trap** — paralyzes one active party member.
  - **Stone Trap** — turns one active party member to stone.
- **Area keys**, if `world_mode` is `open_world`. Thirty-five keys, each named after an area
  ("Salva Pass", "Fienal Pass", and so on), unlock that area's entrances on the world map. These
  are progression items and are placed in the multiworld like any other progression item.

## What locations exist?

Chests and gift sites always exist as locations: 311 chests and 101 gift sites (78 of the
chests and 3 of the gifts are in the Cave of Trials, which only appears with
`cave_of_trials`). Six more kinds exist only with their own option:

| Kind | Count | Option |
|---|---|---|
| Boss fights | 41 (13 in the Cave of Trials) | `boss_checks` (on by default) |
| Party members joining | 18 (21 in open world with `character_items`) | `character_items` |
| An enemy type's first drop | 89 | `enemy_dropsanity: first_drop` |
| Shop slots | up to 550 (98 of them sell something no other shop does) | `shuffle_shops: specials` or `full` |
| Items made with Item Creation | 553 | `craftsanity` |
| Killer-move proficiency levels | 133 | `killer_move_checks` |

Shop slots never hold progression items, in any mode — a price can run into the tens of
thousands of Fol, so nothing the story needs is ever gated behind a purchase.

**Enemy drops** may hold progression items (`drop_progression: allow`, the default), must get
them first where the odds are fair (`force`), or never hold them (`never`). Whatever you
choose, a drop only holds progression when its enemy can always be met again in random
encounters from the point the logic expects you to get it, and only when it drops at least once
in 64 kills without any Luck: enemies that need a lucky party to drop at all (Magius, Ericodus,
Shout, Starguarder, the slimes and others) never hold anything important. With
`encounter_rate: 0` no drop holds progression. `force` fills the drops that come about once in
13 kills or better. Turn `area_keys_in_drops` off to keep every area key out of the drops, so
no area ever waits on one fight's drop.

**Craftsanity** makes each distinct item Item Creation can produce (cooking, compounding,
metalwork, customizing, art, authoring, alchemy, machinery and the super specialties Master
Chef, Publishing, Blacksmith and Reverse Side) a location, checked the first time you make it,
whatever recipe you used. You keep the item you made, and the location's item comes on top. These
locations never hold progression items. A few results only certain members can make (each
member's own Customize weapons, Leon's rare books), and the portraits and Publishing's books
are each about one member, who must be in your party; those hold only filler unless that member
is sure to be in your party. With `no_fail_crafting` on, Item Creation never fails by chance.

**Killer-move checks** make each proficiency level of each killer move a location: Claude,
Bowman, Dias, Precis, Ashton, Opera, Ernest and Chisato, 69 moves. A level is checked when the
member has used the move often enough (most moves level up at 100 to 300 uses; the moves that
count in small steps get three checks, at 100, 200 and 300 uses). By default
(`killer_move_progression: never`) they hold no progression items; `hero` lets the story
route's hero's moves hold them (Claude's; Rena has no killer moves), `any` every member the
logic can count on (the hero, the starting character, and members who join through their
character item, in which case that character becomes a progression item). Moves learned by
level 30 are expected from the disc switch, the rest from the Final Save Point. A member who
may never join your party has no killer-move checks; one who only joins by your choice in the
story holds only filler there.

**Some locations can be missed for good**, and the `missable_locations` option decides what
happens to them:
- a scene closed by a chapter window, or a town that's changed too much to visit (by default,
  `exclude`, these still exist but hold only filler; `include` treats them as ordinary
  locations you must not be late for);
- anything that depends on a choice that rules out another location — an optional party
  member's own scene, a Private Action, a side quest, a fight the story lets you lose, a join
  site, or a location that exists only in Claude's or only in Rena's game when `story_route` is
  `any` — always holds only filler, regardless of the option, because two such locations can
  rule each other out;
- `remove` leaves every missable location out of the world entirely, items and all — except
  join sites, which stay, because they're how party members join when `character_items` is on.

## What does another player's item look like?

A chest or gift whose contents were sent to someone else's world still plays out the original
scene — the message window opens where the item would normally be named — but instead of
naming the real item, it says something like "Sent Hookshot to Alice." Your own items the game
hands you the same way read "Got Blueberries." (or whatever the item is). When you receive an
item from someone else's world, it arrives the next time you're free to walk around, with a
banner like "Received Revival Card from Alice."

## Who can join your party?

With `character_items` on, Celine, Bowman, Precis, Ashton, Opera, Ernest, Chisato, Leon and
Dias become items instead of joining at their story scene; the scene where they'd normally
join for good becomes a location instead, holding that location's own shuffled item. A member
only actually joins once their item has arrived and there's room in the party.

Leon can only join as an item in Claude's game, and Dias only in Rena's — in the other
story_route's game, or with `story_route` set to `any`, neither of them is offered as an item
at all. Some members also compete for the same party slot: you'll get one of Bowman or Precis
(Precis rules Bowman out) and one of Ashton or Opera-with-Ernest (Ashton rules Opera out;
Ernest only comes along with Opera). Celine and Chisato always join if `character_items` is
on. Claude or Rena, Noel, and the current story_route's game always get these members through
the story as usual — they're never shuffled.

### In open world

With `world_mode: open_world`, the party rules are looser (story mode keeps the rules above):

- **No lockouts with `character_items`.** Any member but your story route's hero can be an
  item: the other hero (Rena in Claude's game, Claude in Rena's), Noel, Leon and Dias in either
  game, Bowman together with Precis, Ashton together with Opera and Ernest. With `story_route:
  any` both Claude and Rena join through the story, since you pick between them at New Game.
  The pool holds as many members as fit the party's 8 places with the hero and your starting
  character, picked at random when more are allowed. Members who aren't in the pool never join.
- **`exclude_characters`** keeps the members you list out of the pool, to make room for the ones
  you want. The hero can't be excluded.
- **A healer is guaranteed.** Rena and Noel are the only healers, so if neither would be in your
  party, one of them takes a random pool member's place, unless you've excluded both.
- **`starting_character`** puts a member of your choice in the party from the very start, as
  the leader: you walk around as them, and the story calls them by name where it would name
  the hero. The hero still stays in the party. `hero` (the default) means nobody extra; `any`
  picks a random member who isn't the hero or excluded. A member you name starts even if you've
  also excluded them. This works with or without `character_items`; without it, everyone else
  joins through the story as usual.

## Where does Expel stand?

Expel is never permanently lost. In `open_world`, the old man in Fun City's arena (once you
reach that point in the story) can take you back to Expel at any time; in story mode, the same
character reopens it — Virtual Expel — once you've saved at the game's last save point. Two
design decisions follow from this:
- shops never hold progression items, in any mode, as above;
- with `expel_progression` off, no other player's progression items are placed in Expel — but
  this world's own area keys that the Expel story itself needs to reach the disc switch can
  still appear there, since the story picks them up on its own before Expel closes behind you.

## DeathLink

With `death_link` on, your game is linked to every other DeathLink game in the multiworld: a
death in any of them knocks out your own party, and a Game Over in your game sends one to
everyone else.

A DeathLink is sent only on a real Game Over. Three of the story's fights let the party lose as
a scripted story beat instead — Shin on the ship to El, Dias at the Lacour Tournament of Arms,
and Marsilio's first fight in Fienal — and losing one of these never sends a DeathLink; a
DeathLink that arrives during one of them waits until you're back in normal play instead of
knocking out your party there and then. Shin's fight at the Lacour Front Line and Berle's in
Eluria Tower are different: they're timed endurance battles you must survive, not scripted
losses, so they send and receive DeathLink normally — a wipe there is a real Game Over.

The same fights count as boss checks even though you can't win them outright: a scripted-loss
fight's check counts when the fight ends, win or lose, and the two timed survival fights (Shin
at the Lacour Front Line, Berle in Eluria Tower) count when you survive until time runs out.
Running away from a fight never counts.

## Options

| Option | Choices | What it does |
|---|---|---|
| `goal` | `indalecio` (default; `final_boss` works too), `indalecio_limiter_off`, `indalecio_limiter_off_and_iseria_queen` | Which Indalecio counts, and whether the Iseria Queen must fall first — see "What is the goal?" above. |
| `world_mode` | `story` (default), `open_world` | `open_world`: the 35 area keys lock the world map's entrances until you find them. |
| `expel_teleporter` | on (default), off | Open world only: from partway through the story, Fun City's arena takes you back to Expel. Off: Expel reopens only at the late-game save point, and until then it can only hold this world's own area keys (besides non-progression items). |
| `shuffle_shops` | off (default), `specials`, `full` | Turns shop slots into locations. `specials` is the 98 slots selling something unique to that shop; `full` is every slot in every shop a script opens. Never progression. |
| `story_route` | `any` (default), `claude`, `rena` | Pick `claude` or `rena` to start that story and leave the other one's locations out entirely. With `any`, both exist, but a location that belongs to only one game holds filler. |
| `starting_character` | `hero` (default), `any`, `claude`, `rena`, `celine`, `bowman`, `dias`, `precis`, `ashton`, `leon`, `opera`, `ernest`, `noel`, `chisato` | Open world only (ignored in story mode): a member who joins at the start and leads the party, standing in for the hero on the field and in dialogue; the hero stays in the party. `hero`: nobody extra. `any`: a random member who isn't the hero or excluded. See "In open world" above. |
| `exclude_characters` | a list of names, e.g. `["Bowman", "Leon"]` (default: none) | Open world with `character_items`: these members are never items, so they never join. The story route's hero can't be excluded. |
| `missable_locations` | `exclude` (default), `include`, `remove` | How locations that can be missed for good are treated — see above. |
| `expel_progression` | on (default), off | Off: keep other players' progression items out of Expel (this world's own Expel-story keys are the exception in open world). |
| `cave_of_trials` | off (default), on | Add the Cave of Trials, a very hard postgame dungeon, and its chests, gifts and bosses. |
| `boss_checks` | on (default), off | Defeating a boss (story, side quest or Cave of Trials) is a location. Indalecio is always the goal, never a location. |
| `character_items` | off (default), on | Party members join through items instead of their story scene — see "Who can join your party?" above (open world has no lockouts). |
| `enemy_dropsanity` | off (default), `first_drop` | The first time each enemy type drops anything in a random encounter, that drop is a location. |
| `drop_progression` | `allow` (default), `force`, `never` | With `enemy_dropsanity`: whether drops may hold progression items; `force` fills the fair-odds drops with progression first. Never for enemies you can't meet again or that rarely drop — see "Enemy drops" above. |
| `area_keys_in_drops` | on (default), off | With `enemy_dropsanity`: off keeps every area key (yours and other Star Ocean players') out of your drops. |
| `craftsanity` | off (default), on | Each of the 553 items Item Creation can make is a location, checked the first time you make it. Never progression. |
| `no_fail_crafting` | off (default), on | Item Creation never fails by chance. |
| `killer_move_checks` | off (default), on | Each proficiency level of each killer move (133) is a location. |
| `killer_move_progression` | `never` (default), `hero`, `any` | With `killer_move_checks`: whose killer-move levels may hold progression items — see "Killer-move checks" above. |
| `traps` | 0-100 (default 0) | The percentage of this world's filler items replaced by traps. |
| `exp_multiplier`, `fol_multiplier` | 1-10 (default 1) | Multiplies the EXP or Fol a battle awards. |
| `encounter_rate` | 0-200% (default 100%) | How often random encounters occur. 0 turns them off. |
| `death_link` | off (default), on | Standard DeathLink — see "DeathLink" above for the story's scripted-loss fights, which are the exception. |
