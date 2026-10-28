# Archipelago: the battle side

Phase 1 of the Archipelago work (the world itself: [apworld.md](apworld.md)) for battles. It covers the table of
scripted battles, bosses, the final boss and enemy drops (read from the discs), and the hooks
the port offers for checks, the goal and DeathLink. The field side is in [field.md](field.md),
written by `tools/ap_extract.py`, which imports this tool.

## The tool: `tools/ap_battles.py`

    tools/ap_battles.py DISC1.cue DISC2.cue -o docs/ap/battles.json
    tools/ap_battles.py DISC1.cue DISC2.cue --summary

It reads the player's disc images and writes only numbers and the monsters' own names. From
Python, `ap_battles.build([disc1, disc2])` returns the same dict.

### What it reads

- **Encounter packs.** Encounter N is archive entry 0x57d + N (load_formation 0x8007344c; the
  port's `battle/encounter.cpp`).
  - The pack starts with three lists, each ending in -1: sprite sets, palettes, code blocks.
  - Then 12-byte placements until type 0: {type, x, z, y, model, flags}. Flags bit 1 marks a
    **boss**; bit 2 means the monster starts airborne.
  - Then the formation flags: bit 0 means **no escape**; bits 1 / 2 set the intro type.
  - Then one 0x5c-byte stats record per placement:
    - name at +0x4c;
    - EXP +0x08, Fol +0x0c, level +0x24;
    - drops at +0x18 / +0x1a, their odds at +0x37 / +0x38.
- **Map scripts** (both discs; the field data is identical, so each site is listed once with
  `discs: [1, 2]`):
  - ext 0xb7, `battle(encounter, stage, param)`, takes its arguments from the stack.
  - Every map carries a helper routine that pops three values and calls `battle` (encounter
    0 is the random-encounter path). So a **scripted battle** is a call to that helper after
    three constant pushes, and the encounter is the first push.
  - The tool skips the random path (encounter 0).
  - It records the call site, the routine it sits in, and the global script flags that
    routine tests and sets.
  - ext 0xed (`battle_c`) and 0xbc (`battle_b`) only preload a battle's data. They are listed
    separately (`battle_preloads`).

### The JSON (`docs/ap/battles.json`)

```
{
  "encounters": { "<n>": { "members": [{"type", "name", "boss"}], "boss": bool,
                           "can_escape": bool, "intro": 0|1|2 } },
  "enemy_types": { "<type>": { "name", "first_encounter", "hp", "level", "exp", "fol",
                               "drops": [{"item", "odds"}, {"item", "odds"}] } },
  "scripted_battles": [ { "map", "discs", "offset", "routine", "helper",
                          "encounter", "stage", "param", "monsters", "boss",
                          "flags_tested", "flags_set" }
                        | { "map", ..., "dynamic": true, "args": [...] } ],
  "battle_preloads": [ { "map", "disc", "offset", "op", "encounter", "stage" } ],
  "boss_encounters": [n, ...],
  "final_boss_encounters": [539, 540]
}
```

- `offset` and `routine` are byte offsets in the map's script code: the same numbers the
  port's script tracer and `so2_script.py dis` print.
- An item id of 0 in `drops` means no drop.
- Drop odds are out of 256. On the console the chance is
  `odds + (party luck >> 4) - 7`, doubled by an accessory (result.cpp `roll_drops`).

### What the discs hold (NA discs)

- **Encounters and enemies.** There are 564 encounters, 47 of them with a boss member. There
  are 187 enemy types; 62 have no drop.
- **Scripted battle call sites.** There are 89. Five have a computed encounter, all on map
  305: the encounter comes from a script variable, so the Archipelago world has to name
  those by hand.
- **The final boss.** Indalecio, encounters **539 and 540**, map 685. They are not two forms
  in a row. They are alternatives: routine 28592 tests flag 0x22c (556) at 28900. If it is set,
  the fight is 540 (type 207, HP 1.5M, level 240; battle at 29444). If it is clear, the fight
  is 539 (type 206, HP 500k, level 160; battle at 29880). Both paths join at 30116 and go on
  to the ending. Only map 239's script sets 0x22c, at 30416 in Filia's event. A victory in
  either one is the goal.
- **Boss encounters with no scripted call site.** Encounter 0 (Cyril), 263 (Dias), 534
  (FunnyThief) and 561 (Iselia-queen). They are reached another way (a table or a computed
  encounter) and need checking in Phase 2.
- **Story flags.**
  - Global flag 0x2e9 is set by almost every battle routine, so it is a "battle happened"
    marker, not a story one.
  - The other flags a battle routine sets place it in the story. For example, the Cave of
    the Seven Stars bosses (maps 705-815) set 0x3b6..0x3c1 one each.

## Battle hooks in the port

There is no port code for Archipelago yet. This section is where it will go.

| What | Where | What is known there |
|---|---|---|
| The battle's identity | `battle_scene_start` (battle_program.cpp) | `g.encounter_id` (0x8006ef98) and `g.setup` (handoff 5: encounter, stage, the field's flags) |
| The outcome | `battle_exit_to_field` (battle_program.cpp) | `g.outcome`: 0 victory, 1 escaped, 2 defeat (party wiped), 3 ran off, 4 time up. It is written to config +0x178 just before the field takes over. |
| The field's view of it | `field_program.cpp` (back from a battle) | config +0x178 goes to the script register R. A lost scripted battle (outcome 2, field flag 0x7e) starts the game over (`map.cpp`). |
| Drops | `roll_drops` (result.cpp, 0x800869fc) | One drop per enemy at most, with its RNG draw; `inventory_add` gives it. |
| EXP / Fol | `award_exp_money` (result.cpp, 0x80086290) | the totals before they are added (`rs->exp`, `rs->fol`) |

- **Boss checks and the goal.**
  - In `battle_exit_to_field`, once the outcome is final (after the "all down" fix-up), look
    `g.encounter_id` up in the seed's location table.
  - A victory (outcome 0) on a listed encounter records the check.
  - Encounter 539 / 540 with outcome 0 sends CLIENT_GOAL.
  - Keying by encounter id is enough, because each boss encounter is unique to its fight.
    The scripted-site table above tells which map and story point it belongs to.
- **DeathLink.**
  - **Sending:** outcome 2 in the same place, only when the party really was wiped. Escapes
    and time-ups do not count.
  - **Receiving:** in a battle, call `battle_party_lost()` (battle_program.cpp). It sets
    outcome 2 the way the console does when the last member falls, so the normal defeat path
    and game over follow. Outside a battle, queue it for the next battle's first frame of
    state 2.
- **Drops as checks.**
  - In `roll_drops`, keep the RNG draw exactly as it is, so the battle doesn't diverge.
  - On the first drop of each enemy type, replace `inventory_add` with recording the check:
    the location is "first drop of type T".
  - The seed's item for that location is given by the normal receive path.
- **EXP / Fol multipliers.** Scale `rs->exp` / `rs->fol` in `award_exp_money` before they are
  added. The RNG is not involved.
- **Enemy scaling and encounter rate.**
  - Enemy stats already have a run-time patch path: `mods.cpp` `enemy` lines, applied as a
    pack loads.
  - The encounter rate is a field-side setting (the field's encounter counter).
- **Verifying a seed.** `--battle ENC[:STAGE]` (battle_debug.cpp) starts any encounter
  headless, so a validator can confirm each listed encounter id exists and loads.
