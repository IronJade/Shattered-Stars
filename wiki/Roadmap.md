# Roadmap

What's coming next for Shattered Stars and its Archipelago world: an **entrance randomizer** and a
**macOS** build, both in the next release. There are no dates here on purpose. A release ships when
its release candidate passes every check on Windows, Linux and (from the next release) macOS, so
this page shows the order of the work and where each part stands.

*Last updated 2026-10-28.*

## Releases at a glance

| Release | What's in it | Status |
|---|---|---|
| **Shattered Stars 0.03.00**, **so2.apworld 0.2.2**, **PopTracker pack 1.0.0** | Tracker support: Universal Tracker without your YAML, the PopTracker pack, maps drawn from your own disc ([Trackers](Trackers)) | **Released** 2026-10-28 |
| **Shattered Stars 0.04.00**, **so2.apworld 0.3.0**, **PopTracker pack 1.1.0** | The entrance randomizer (below); the first **macOS** build (Apple Silicon) | **In progress**: planned, macOS build a release candidate |
| Later | Mixing the two worlds' entrances; whole-world shuffling in open world; the Japanese discs | **Later**: designed or deferred, not scheduled |

## The entrance randomizer

With the entrance randomizer on, the places of *Star Ocean: The Second Story* are reconnected for
each seed. Walk into Arlia's spot on Expel's world map and you might find yourself in Cross Cave;
walk back out and you're standing at Arlia's spot again. Your seed decides where everything is,
and the logic still guarantees that every check can be reached.

### The options

Planned YAML options (the names may still change before release):

```yaml
  # Shuffles where entrances lead (open world and hub mode; story mode refuses it).
  # off: every entrance leads where it does in the game.
  # world_map: the world-map entrances of towns and dungeons lead to other towns and dungeons of
  #   the same world. Inside a place nothing moves.
  # dungeons: world_map, and the doors inside each dungeon lead to other rooms of that dungeon.
  # all_doorways: dungeons, and the doors inside each town lead to other rooms of that town.
  entrance_randomizer:
    off: 50
    world_map: 0
    dungeons: 0
    all_doorways: 0

  # coupled: going back through a door returns you where you came from.
  # uncoupled: every door leads somewhere of its own; going back may take you elsewhere.
  entrance_coupling:
    coupled: 50
    uncoupled: 0
```

| Level | What moves | Size (from the game's own door tables) |
|---|---|---|
| `world_map` | Towns' and dungeons' entrances on the world maps, among the same world's | about 40 pairs |
| `dungeons` | Also the doors inside each dungeon, among that dungeon's rooms | about 140 more |
| `all_doorways` | Also the doors inside each town, among that town's rooms | about 230 more |

- **Open world and hub mode only.** Story mode keeps the story's own order of places, which
  shuffled entrances would fight, so it refuses the option.
- **Coupled by default**: leaving a place takes you back where you came in. Uncoupled is there for
  players who want the chaos.
- **Expel and Energy Nede stay separate**: Expel's entrances shuffle among Expel's, Energy Nede's
  among Energy Nede's. A mixing option is designed for later.
- `all_doorways` is the most ambitious level. Town interiors carry many story scenes, so it may
  arrive marked experimental, or in hub mode first.

### How it stays beatable

A door only trades places with doors the game reaches under the same conditions: the same world,
the same continent or island in open world, the same always-open part of a dungeon. That keeps
the ships, the El voyage and the disc switch meaningful, and it means every check the logic says
is reachable stays reachable, with the same rules as today.

On the world map the game doesn't just move your landing point: walking into a spot runs the
**destination's own entrance**, so the story effects of arriving somewhere come with the place.
Some doors never move: Fienal, the boss rooms, the Cave of Trials, doors the story changes or
closes, one-way and scene transitions, and the vehicles.

### Trackers

Both trackers follow along without spoiling the seed. A door reads `A -> ?` until you've been
through it, and a place counts as reachable once you've found a way into it. That needs
so2.apworld 0.3.0 for [Universal Tracker](Trackers) and **PopTracker pack 1.1.0**. The game
reports each entrance you find to the server, as it reports your map today.

### Compatibility (planned)

- A seed made with the entrance randomizer needs **Shattered Stars 0.04.00**. Older versions
  won't load it, so you can't start one by mistake.
- so2.apworld 0.3.0 seeds **without** the entrance randomizer still play on Shattered Stars 0.03.00.
- Universal Tracker needs no new version of its own: 0.3.6 works.

### Build order

The work is built and proven level by level. Each level is done when its seeds are generated in
bulk and all beatable, sample seeds are replayed door by door in the game, and the reach proofs
match the unshuffled game.

| Step | Status |
|---|---|
| Groundwork: stable door ids, the trackers' live door panel and the found-entrances key (shipped in 0.03.00) | **Done** |
| The plan, from the game's door and map data | **Done** |
| The entrance model: every door, where it lands, which doors may trade places | Next |
| `world_map` in hub mode, then in open world | Planned |
| `dungeons` | Planned |
| `all_doorways` | Planned |
| Release candidate on Windows, Linux and macOS, then 0.04.00 / 0.3.0 / pack 1.1.0 | Planned |

## macOS

The first macOS build ships **with Shattered Stars 0.04.00**.

**Done (a release candidate today):**
- The game and setup build on Apple Silicon and pass the full test suites.
- The game's output is identical to Linux's: the same frames, and byte-identical tracker maps.
- Two apps, **Shattered Stars.app** and **Shattered Stars Setup.app**, for macOS 11 or newer.
- Your disc images go in `~/Library/Application Support/ShatteredStars/discs`, or you choose them
  in setup.

**Still to do before release:**
- Secure connections (`wss://`), which hosted rooms on archipelago.gg need.
- The Archipelago end-to-end and tracker checks on a Mac.
- A last hands-on check: audio, gamepads, setup's file picker.

**Good to know:**
- Apple Silicon (M1 or newer) only; Intel Macs aren't supported.
- The apps aren't signed with an Apple Developer ID, so macOS blocks them the first time. Open
  each once, then go to **System Settings > Privacy & Security > Open Anyway**. The download's
  README explains this, along with a one-line Terminal alternative.
- Until then you can build it yourself: see [Building from Source](Building-from-Source).

## Later

- **Mixing the worlds**: Expel's and Energy Nede's entrances shuffled together (an option, designed).
- **Whole-world shuffling in open world**, across continents and islands (designed).
- **The Japanese discs**: recognised today, supported later.

## Follow along

- [Releases](https://github.com/IronJade/Shattered-Stars/releases): every version, with its notes.
- [The full technical plan for the entrance randomizer](https://github.com/IronJade/Shattered-Stars/blob/main/docs/plans/er.md)
  (options, door counts, logic, port and tracker work, risks).
- [The macOS build notes](https://github.com/IronJade/Shattered-Stars/blob/main/docs/handoff/macos.md).
- [Report a bug or suggest something](https://github.com/IronJade/Shattered-Stars/issues).
