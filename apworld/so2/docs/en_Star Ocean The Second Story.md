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

On top of that baseline, four more kinds of locations can be turned on:
- defeating a boss,
- a party member joining you,
- the first time an enemy type drops anything in a random encounter,
- buying a slot at a shop for the first time.

## What is the goal?

The goal is always defeating Indalecio, the final boss, in Fienal. There are two versions of
that fight (the second, harder one follows an earlier scene with Filia); winning either one
completes your game. The `goal` option exists for future expansion but currently offers only
this one choice — there's nothing to configure here yet.

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
  party member arrives as an item — see "Who can join your party?" below.
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
`cave_of_trials`). Four more kinds exist only with their own option:

| Kind | Count | Option |
|---|---|---|
| Boss fights | 41 (13 in the Cave of Trials) | `boss_checks` (on by default) |
| Party members joining | 18 | `character_items` |
| An enemy type's first drop | 89 | `drops_as_checks: first_drop` |
| Shop slots | up to 550 (98 of them sell something no other shop does) | `shuffle_shops: specials` or `full` |

Shop slots never hold progression items, in any mode — a price can run into the tens of
thousands of Fol, so nothing the story needs is ever gated behind a purchase.

**Some locations can be missed for good**, and the `missable_locations` option decides what
happens to them:
- a scene closed by a chapter window, or a town that's changed too much to visit (by default,
  `exclude`, these still exist but hold only filler; `include` treats them as ordinary
  locations you must not be late for);
- anything that depends on a choice that rules out another location — an optional party
  member's own scene, a Private Action, a side quest, a fight the story lets you lose, a join
  site, or a location that exists only in Claude's or only in Rena's game when `protagonist` is
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
protagonist's game, or with `protagonist` set to `any`, neither of them is offered as an item
at all. Some members also compete for the same party slot: you'll get one of Bowman or Precis
(Precis rules Bowman out) and one of Ashton or Opera-with-Ernest (Ashton rules Opera out;
Ernest only comes along with Opera). Celine and Chisato always join if `character_items` is
on. Claude or Rena, Noel, and the current protagonist's game always get these members through
the story as usual — they're never shuffled.

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
| `goal` | `final_boss` | Defeat Indalecio (the only choice today). |
| `world_mode` | `story` (default), `open_world` | `open_world`: the 35 area keys lock the world map's entrances until you find them. |
| `expel_teleporter` | on (default), off | Open world only: from partway through the story, Fun City's arena takes you back to Expel. Off: Expel reopens only at the late-game save point, and until then it can only hold this world's own area keys (besides non-progression items). |
| `shuffle_shops` | off (default), `specials`, `full` | Turns shop slots into locations. `specials` is the 98 slots selling something unique to that shop; `full` is every slot in every shop a script opens. Never progression. |
| `protagonist` | `any` (default), `claude`, `rena` | Pick `claude` or `rena` to start that story and leave the other one's locations out entirely. With `any`, both exist, but a location that belongs to only one game holds filler. |
| `missable_locations` | `exclude` (default), `include`, `remove` | How locations that can be missed for good are treated — see above. |
| `expel_progression` | on (default), off | Off: keep other players' progression items out of Expel (this world's own Expel-story keys are the exception in open world). |
| `cave_of_trials` | off (default), on | Add the Cave of Trials, a very hard postgame dungeon, and its chests, gifts and bosses. |
| `boss_checks` | on (default), off | Defeating a boss (story, side quest or Cave of Trials) is a location. Indalecio is always the goal, never a location. |
| `character_items` | off (default), on | Party members join through items instead of their story scene — see "Who can join your party?" above. |
| `drops_as_checks` | off (default), `first_drop` | The first time each enemy type drops anything in a random encounter, that drop is a location. |
| `traps` | 0-100 (default 0) | The percentage of this world's filler items replaced by traps. |
| `exp_multiplier`, `fol_multiplier` | 1-10 (default 1) | Multiplies the EXP or Fol a battle awards. |
| `encounter_rate` | 0-200% (default 100%) | How often random encounters occur. 0 turns them off. |
| `death_link` | off (default), on | Standard DeathLink — see "DeathLink" above for the story's scripted-loss fights, which are the exception. |
