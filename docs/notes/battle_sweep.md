# Battle encounter sweep

`tools/battle_sweep.py` runs every encounter in `docs/ap/battles.json` (564) headless in the
port. It is port only, with no PCSX-Redux. It looks for crashes, sanitizer reports and code that
isn't ported yet. Each run logs a `[port] ... not ported yet ...` line (runtime log_once) when
it reaches unported code.

## How a run is set up

- **Battle.** `--battle ENC:STAGE`.
- **Stage.** Taken from the field data, in this order:
  - the scripted battle sites and preloads in battles.json;
  - the random-encounter groups (the field program's lists at 0x80074fe0, with
    `encounter_maps.md`'s group → stage pairs);
  - stage 3.
- **Disc.** When the boot disc lacks a load-list entry, `--battle` inserts the other disc.
- **Party.**
  - Claude, Rena and Celine, plus a fourth member who rotates by encounter through the other
    nine (`SS_BATTLE_PARTY`).
  - The AI drives all four (config +0x181 = 1).
  - MP 200; the fighters get killer moves on L1 / R1.
  - HP / ATK / DEF are scaled (`SS_BATTLE_STRONG=H:A:D`, HP capped at 9999).
  - `--god` (`SS_BATTLE_GOD`) refills the party's HP every frame.
- **Input.** Cross every 30 vblanks, so the result screen moves on.
- **End.** At the battle's exit (`SS_BATTLE_QUIT_AT_EXIT`), or at the cap of 15000 vblanks.
- **Isolation.**
  - XDG_CONFIG_HOME=OUT/xdg and --no-memcard.
  - At most 5 jobs at once.
  - The run uses a copy of the binary, so a rebuild during the sweep can't change it.

```
. scratchpad/battle/discs.sh          # SS_DISC1 / SS_DISC2
tools/battle_sweep.py --out scratchpad/battle/sweep/NAME [--god] [--strong 20:30:4]
    [--bin engine/build-battle-asan/shattered-stars] [--encounters 0-563] [--stages 0-63]
```

Output in OUT:

- `results.json` / `results.csv`: per run, the stage and its source, the members, the party,
  the outcome, battle / fight frames, rc / signal, the `[port]` lines and sanitizer lines;
- `missing.json`: the `[port]` lines, ranked by the number of encounters that hit each;
- `summary.md`;
- `logs/`.

## Pass 1 (2026-10-22, release build, HP:ATK:DEF x20:8:4)

- Outcomes: 248 victory, 296 defeat, 20 cap.
- No crashes, signals or timeouts.
- The trial runs did crash in pc_ai_choose_heal, but that was the sweep's own doing. HP x20
  took a member over 9999, and the heal search starts below 10000. HP is capped at 9999 now,
  as in the game.

Missing pieces, by encounters hit (B = boss encounters among them):

| Encounters | B | Missing | Status |
|---|---|---|---|
| 564 | 47 | pc_apply_equip_effects: equipment effects 159 / 128 / 228 / 112 / 187 / 115 / 172 (the starting gear) | ported and verified (0x800818dc and its helpers, pc_update_effects 0x8003adc8, pc_has_effect's +0x135 test) |
| 248 | 8 | voice_play_entry (the victory voice) | the effects agent's battle talk layer (party_talk), in progress |
| 18 | 1 | stage code of stage 0x10 (hanging objects) | ported and verified (ref stage16c) |
| 18 | 1 | stage code of stage 0x24 (breakables, runaway cart) | ported and verified (refs stage36, stage36_long) |
| 13 | 0 | stage 8 lightning (0x80063a70) | ported and verified (refs stage8_light, stage8_shots) |
| 10 | 1 | stage code of stage 0xe (the toppling stack) | ported and verified (ref stage14c, after the B14 fix: Claude's first jump check) |
| 9 | 0 | stage code of stage 0x29 (snow flakes) | ported and verified (ref stage41) |
| 2 | 2 | stage code of stage 0x33 (the last battle's models) | ported and verified (ref stage51) |
| 1 | 1 | stage code of stage 0x17 (the toppling halves) | ported and verified (ref stage23) |
| 1 | 0 | stage code of stage 0x1a (rolling boulders) | ported and verified (ref stage26) |
| 1 | 1 | stage code of stage 0x1f (floating models) | ported and verified (ref stage31) |
| 13 | 0 | particle kind 4 (shards; kind 5 too) | ported and verified visually (kind 4: Explode in the world agent's i11a; kind 5: item 0x2f9, ref conf340d) |

Not missing:

- `sprite texture cache: no room (NO CASH)` (219 encounters) is the console's own message
  from texcache_alloc. The port logs it once. Whether the console also runs out in these
  fights hasn't been checked; a BW_HIT_BP on debug_print 0x8003beb8 would show it.

Stages 0xd / 0x38 / 0x39 / 0x15 / 0x28 come from no encounter in battles.json. Their code is
ported anyway: stage 0x15 is verified (ref stage21) and stage 0xd too (ref stage13).

Pass 1's bosses all ran to an outcome, except the caps on encounters 509 (M-eater) and 531 (Bark).
37 of the 47 boss fights were defeats, which is why pass 2 runs in god mode.

## Pass 2 (god mode, x20:30:4, the binary with all of the above)

Results are in `scratchpad/battle/sweep/pass2/` (`summary.md`, `results.json`, `missing.json`).

- **Outcomes:** 325 victory, 83 defeat, 156 cap.
- **No crashes, signals or sanitizer reports.**
- **Defeats despite the HP refill:** these happen when a member is downed within a single
  frame (one-hit kills or instant death), so refilling HP each frame doesn't save them.
- **Bosses:** 17 victory, 13 defeat and 17 cap. The caps are encounters 504, 505, 506, 508,
  509, 512, 514, 520, 521, 524, 525, 527, 530, 531, 532, 535 and 539 (the final boss). These
  are long fights that outlast the 15000-vblank cap.
- **Missing pieces:**
  - The victory voice (326 encounters, the enemy agent's).
  - Particle kind 4 (12 encounters). This line comes from the binary snapshot taken just
    before kinds 4 / 5 were ported, which is now done.
  - Nothing else in battle reported a missing piece.

## Stage references (encounter 2 on each special stage, `cmpstage.sh`)

All have the RNG equal to past the battle exit and the Shots equal in every frame. Unit
samples differ only in the intro x samples (b28-b42) and in HP sampling. The intro x samples
are the console's party load: its unit sits at the formation point until state 1.

| Stage | Ref | Shot frames |
|---|---|---|
| 0xd | stage13 | 660 |
| 0xe | stage14c (Claude + AI Celine, encounter 35) | 975 |
| 0x10 | stage16c | 909 |
| 0x15 | stage21 | 549 |
| 0x17 | stage23 | 593 |
| 0x1a | stage26 | 503 |
| 0x1f | stage31 | 637 |
| 0x24 | stage36, stage36_long | 525, 1298 |
| 0x29 | stage41 | 660 |
| 0x33 | stage51 | 639 |

## ASan pass (2026-10-23, god mode, x20:30:4)

```
tools/battle_sweep.py --out scratchpad/battle/sweep/asan --god --strong 20:30:4 \
    --bin engine/build-battle-asan/shattered-stars
```

- **Outcomes:** 325 victory, 82 defeat, 153 cap, and 4 runs that ASan stopped.
- **The 4 reports** (encounters 350, 430, 471, 540) were all one heap-use-after-free in
  bsfx_pan_update. A SpellCast deletes itself while the sound it started at itself still
  plays, and the channel keeps the pointer. This is the console's own bug (B15); on the
  console it reads freed memory.
  - The fix: ~BObj calls bsfx_forget_emitter, so the channel pans from the object's last
    position. It is sound only.
  - Rerun under ASan (`sweep/asan_fix`): all four are clean, with pass 2's outcomes
    (encounter 540: defeat at 3174).
- **Otherwise clean:** no other sanitizer report, crash or timeout. The only missing piece is
  the victory voice, the effects agent's talk layer.

## RNG after CD-timed loads

The port loads instantly; the console waits for the CD. Draws made around a load therefore
land at other moments, and that is expected (no slowdown modelling).
- **Battle state 0** (the party loader) lasts about 44 frames per member on the console:
  Claude alone near b44, four members near b102. The port reaches state 1 at b0.
  - Draws gated on state 1 come that much earlier in the port, e.g. enemy_think's
    intro-voice rng_range(2) at 0x8003cb44.
  - The camera intro ends the intro at the same frame either way. On the late-save final
    boss the fight starts at b406 in both.
- **The field voice queue** across map loads (voice_queue_build_field).

Comparisons re-sync the generator after such a load:
- SS_BATTLE_RNG_SKIP=N makes N extra draws at the port's state 0→1, or the generator can be
  re-seeded from a Redux dump.
- The menu agent's late-save final boss (encounter 540, `scratchpad/menu/m8/late/r_b`) with
  N = 1 (`scratchpad/battle/late/p2`): the generator equals Redux at all 250 samples from b102
  to b600, the AI's draws from b408 included.
