# Killer-move proficiency: continuation log

Agent: killer_prof (2026-10-25). Assignment: find where the original increments killer-move
proficiency (record +0x8a + 2 * move), check whether the port is missing it, port it if so,
verify against PCSX-Redux. Rules: battle code only (engine/src/game/battle/, engine/tests/),
the catalog rows touched, this log; no commits; headless Redux (`-cli`) under
`tools/memcap.py` (SS_MEM_CAP_GB 4), at most 2 game processes; scratch in
`scratchpad/killer_prof/`; on a refused command stop and report it.

## Findings (2026-10-25)

- **The original increments it in `pc_begin_action` 0x80037098 (battle, vtable slot 12),
  killer-move case (+0x12a = 7..0x11), store at 0x800373b4** (`sh v0, 0x7e(v1)` with
  v1 = record +0x324 + kind * 2; kind = move byte + 6, so +0x7e + 2 * kind = +0x8a + 2 * move).
  Disassembly (REA, `scratchpad/battle/modules/char01_live.elf`, 0x800372ec-0x80037400):
  1. no target (+0x1d4 == 0) -> back to idle, nothing counted;
  2. `pc_record_counter_dec(self, 2)`; slot 39 voice (move +0x2f0); slot 36 MP cost;
     velocity zeroed;
  3. stats MP (+0x1fc -> +0x24) < cost -> back to idle, nothing counted;
  4. +0x136 = 0; MP -= cost; state +0x124 = kind + 9;
  5. `n = record[+0x7e + 2 * kind]; if (n < 999) record[...] = n + 1;`
  6. animate(0), anim reset, counters, +0x68 / +0x6c = 1, +0x238 += 1.
  So: +1 per killer-move start (not per hit, not at battle end), cap 999, only when the move
  actually starts (target present, MP paid). Chained second moves (combo, case 0x12 -> a new
  command -> begin_action again) count separately. AI-controlled members go through the same
  function. **No other condition: no flag, no game-state bit, no Cave of Trials check.**
- **No other writer.** Pattern scan (`scratchpad/killer_prof/scan.py`, stores with immediate
  0x7e / 0x8a / 0x8c / 0x2f0-0x2f8) over the 12 Redux battle RAM dumps
  `scratchpad/battle/modules/ram_char01..12.bin` (#2129 + #1984.0/.1 + #1981 + each character's
  module at 0x801ee000): the only record stores are 0x800373b4 (this one), 0x8003a178
  (pc_cast_spell, spells) and 0x80085984 / 0x80085a70 (#1981 learn_from_table, zeroing on
  learn). Nothing stores unit +0x2f2..+0x2f8 at run time (pc_setup_killer_moves 0x80084874
  sets them once at load, in #2129 loader code later overwritten by #1984.1), so the charge
  level a battle uses is the count at its start.
- **The port already has it**: `PartyUnit::begin_action`, engine/src/game/battle/party_states.cpp
  (case 7..0x11: `record + 0x7E + action_kind * 2`, `if (n < 999) ++n`), matching the
  original instruction for instruction. The research agent's grep looked for 0x8a / 0x8c and
  missed the 0x7e form. Catalog row pc_begin_action already says "record use counter +0x7e+2n
  (max 999)".
- **Existing Redux refs confirm it** (`scratchpad/killer_prof/refcount.py` over
  `scratchpad/fx_worker/battle/redux/ref/*`: slot-0 record count at battle_main_loop vs the
  dump at battle +1500, by then back on map 7): every base-level ref (USES 0) grows, e.g.
  prec_b1 0 -> 6, prec_b7 0 -> 4, op_b5 0 -> 3, chis_b1 0 -> 3, as_b3 0 -> 3; the max-level
  refs stay 999 -> 999. These are New-Game saves on map 7 (no Cave of Trials).

## Redux run and port comparison (2026-10-25)

Scripts (copies of the fx agent's, writing only to `scratchpad/killer_prof/`):
`redux/run_kp.sh NAME CHAR L1BYTE [USES]` (PCSX-Redux `-cli -interpreter -debugger`, New Game
as Claude, map 7 poked to the forced battle encounter 2 / stage 3, slot 0 strong, MP 200,
killer byte L1BYTE at USES; `BW_HIT_BP` on 0x8003739c (move start) and 0x800373b4 (the
increment; a0 = old, v0 = new); `BW_DUMP_BP` on battle_exit_to_field 0x80087644) and
`port_kp.sh NAME CHAR L1BYTE [USES]` (the port from that run's field_exit dump, same pokes
and presses, `SS_BATTLE_DUMP` = handoffs 2/3/4/6 at the battle's exit, records at file
offset 0x5a0). Both run under `capped.py` (tools/memcap.py, 4 GB).

| Run | Claude move 1 count, Redux | Redux increments (battle tick: old -> new) | Port after the battle |
|---|---|---|---|
| kp_c1 (USES 0) | 0 at battle_main_loop -> 2 at battle_exit_to_field (b1282) -> 2 back on the field (b1500) | b296: 0 -> 1; b662: 1 -> 2 | 2 |
| kp_c1cap (USES 997) | 997 -> 999 (exit b1146) -> 999 (b1500) | b296: 997 -> 998; b662: 998 -> 999 | 999 |
| kp_c1max (USES 998) | 998 -> 999 (exit b1146) | b296: 998 -> 999; b662: move starts, no store (cap) | 999 |

The fights are the same fight: `scratchpad/battle/rngcmp.py` shows the RNG draws equal at every
sample to the battle's exit (kp_c1 to b1290, the port exits at battle frame 1277; afterwards
the console is back on the field, 1024 field draws, and the port has quit). Each increment
falls on the frame the move starts (state 0x10), before any hit, once per move.
kp_c1cap / kp_c1max: RNG equal to b1150 / b1150 (first mismatch b1160, after both exits).

## Verdict

- **The port was not missing it.** `PartyUnit::begin_action` (party_states.cpp) already does
  exactly what 0x800373b4 does, and the port's record after each fight equals the console's.
  No behaviour change made. What changed:
  - party_states.cpp: comment only (names the counter as the killer-move proficiency,
    +0x8a + 2 * move, 0x800373b4, so a grep for proficiency / 0x8a finds it).
  - docs/catalog/functions.csv, pc_begin_action 0x80037098: summary spells out the
    proficiency increment (offsets, cap, store address, only writer, no condition);
    evidence adds this check; port column names `PartyUnit::begin_action`.
- **The user's "only updates once the Cave of Trials has been completed": not so in the
  original.** The increment has no flag / game-state / story test, and it grows in the first
  battle of a New Game (all the refs above are map-7 New-Game fights). If something in play
  looked like it, the likely causes are: the count only rises when a move actually starts
  (no target or too little MP -> not counted); the battle's charge level and Claude's
  upgrades / Dias's range use the count at the battle's start, so a threshold crossed in a
  battle shows from the next battle; and the counts are per move per character record.
- No test added: no change to behaviour, and the battle test file has no PartyUnit harness
  (begin_action needs a live unit with sprite data); the end-to-end check above is the proof.

## Suites (engine/build, 2026-10-25, after the comment change)

- ss_tests 99/0 (SS_DISC1), ss_game_tests 129/0, tools/smoke_field_events.sh 86 PASS / 0 FAIL
  (smoke run on the build before the comment-only edit).

## State

Done. Scratch kept: `scratchpad/killer_prof/` (scripts, Redux logs and RAM dumps of the three
runs, port logs and handoff dumps; captures deleted).

## Follow-up: proficiency levels of every killer move (2026-10-25, for Archipelago checks)

`scratchpad/killer_prof/move_levels.csv` (made by `scratchpad/killer_prof/move_levels.py DISC1.cue`,
reading the disc through tools/so2_data.py `Disc` / `read_names` and so2_archive `unpack_block`,
following the port: killer_move_data_entry tables in #2129, MoveData = entry block 0 + 0x20
{s16 AI bonus per level, s16 div, s16 cap, s16 mode}, learn tables in #1981, names from the
#1980 string table at key 5000 + (char - 1) * 0x32 + move - 1). Level = uses / div, capped at
cap (pc_move_charge_level); uses stop at 999, so the column `max_level_reachable` =
min(cap, 999 / div) and `uses_max_level` = div * that.

- **Weapon variant:** Claude, Dias and Ashton have two data tables by weapon_variant (+0x205 =
  the weapon item's byte +0x2a). Every sword they can equip has 1, so the CSV uses the armed
  table; the bare-handed table (variant 0) differs only for Claude's Shooting Stars (div 120
  instead of 100), Ripper Blast (80 instead of 90) and Ashton's Piercing Swords (no levels
  instead of div 64 cap 3). Claude's ten armed values equal the Prima guide's "Proficiency"
  column (200, 300, 260, 200, 100, 270, 160, 500, 420, 460).
- **77 moves, not 69:** the learn tables hold 69; 8 more have data and names but no learn-table
  row (Claude's Phase Gun, move 3; Precis's M. Super Beam / Holo-holograph / Barrier; Ashton's
  Tri-Ace, which the guide lists as a Cave of Trials level-10 treasure; Opera's Healing Star /
  Laser Bit / Hyper Launcher). They are in the CSV with learn_level "not in learn table".
- **Special thresholds outside the formula** (all take effect from the next battle, because
  a battle reads the count at its start):
  - Claude move 2, Shooting Stars: the module keeps its own level, uses / 100 capped at 3
    (ClaudeUnit::move1, pc_claude.cpp:485; module 0x801ee4ec): the animation variant
    (level & 1) and the spark chance (level * 7000 / 32768 each even frame). At >= 200 uses
    the move loads upgraded data (entry 0x872 armed / 0x8c0 bare; party.cpp:459).
  - Claude move 8, Burst Knuckle: upgraded data at >= 100 uses (0x878 / 0x8c7; party.cpp:460).
  - Dias move 2, Full Moon Slash: at >= 100 uses its near/far reach halves (party.cpp:172).
  - Opera move 1, Healing Star (not in a learn table): the orb's power is the raw use count
    (pc_opera.cpp:961); its levels are div 4, cap 300 (249 reachable).
  - Moves whose "level" is a large count rather than a tier: Bowman's Secret Medicine (div 1,
    cap 9999: level = uses), Opera's Photon Prison (div 10, cap 100: 99 reachable), Bowman's
    Pillory (div 16, 29), Whirlwind Fist (div 25, 20), Opera's Gravity Shell (9) and Hyper
    Launcher (9), Bowman's Poison Pills and Opera's Cold Wind (5 each).
- **Totals (69 learn-table moves):** sum of caps 10279 (9999 of it Secret Medicine); sum of
  reachable max levels 1278, or 279 without Secret Medicine. Moves by reachable max level: 0:1,
  1:22, 2:27, 3:12, 5:2, 9:1, 20:1, 29:1, 99:1, 999:1. The 61 moves with at most 3 levels
  hold 112 levels. No levels: 1 (Ashton's Dead Triangle, div 1 cap 0); div 0: none.
- **Totals (all 77):** sum of caps 10596; reachable 1544 (545 without Secret Medicine);
  the 67 moves with at most 3 levels hold 120 levels; no levels: 1.
