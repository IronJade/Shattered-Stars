# Handoff: the hub open world (`world_mode: hub`)

Started 2026-10-25 by the main session ("Start the hub open world"). The design is
`scratchpad/openworld/story_flags.md` sections 3-5 (mechanisms M-A / M-B, the boss table in
section 2, the port pieces P1-P6, work items W1-W10, risks R1-R8); this file holds the user's
decisions, which override that design where they differ, and the plan.

## The user's decisions (2026-10-25)

- **Mode:** a new `world_mode` value (`hub`) beside `story` and `open_world`, which keep the
  story (decided earlier: "Both: roads now, hub later").
- **D1 mechanism:** the hub start: New Game starts after the Final Save Point (chapter 60, the
  late proof's state, flag 730, disc 2, every location's done flags cleared); both worlds open
  (Energy Nede by the Synard, Expel by the Fun City arena's old man / Virtual Expel's Synard);
  nothing one-way; a chapter lock keeps the chapter at 60.
- **D2 boss scenes: "Original scenes from the start".** Each story boss plays its original scene
  in its chamber through a per-chamber story view (story_flags.md P6 / M-B: while the boss is
  unbeaten, that chamber's map sees the boss's chapter / step / flags; the view ends at the
  scene's first transfer out or at free play; stores made in it are dropped except a whitelist).
  The story view is core work, not optional. A restaged battle (P3) is only the fallback for a
  boss whose scene can't be made self-contained; say which and why.
- **D3 unlocking: "Every boss independent".** No chained quest lines: each chamber is open as soon
  as its dungeon's key has arrived. Beating a boss is its location (check).
- **D4 keys: towns open, ~20 dungeon keys** (one per dungeon, including dungeons entered from a
  town: Salva Drift, the Mars dungeon, Zand's Mansion, the Synard Plant, the Portal of Sealing...).
- **D5:** the four losable fights (Zand 265, Dias 204 / the tournament, Shin on the ship 517,
  Marsilio's first Fienal fight 530) are not hub chambers. The Cross Cave guardians (set battle
  523, chest flag 2569) are a chamber.
- **D6 Fienal's requirement: "Make these options in the yaml".** A YAML option, three values: the
  four Fields' bosses + Berle at Armlock (default), the four Fields only, every hub story boss.
- **D7:** characters are always items in hub mode (join scenes don't run at chapter 60); the
  open-world party v2 rules and the starting character apply.
- **D9 start: "YAML option":** Fun City (279, default), Arlia, or random.
- **D10 Fienal: "Their original scenes":** the three Fienal pairs (Ruprecht & Nicolus 657,
  Vesper & Decus 669, Cyril 674) play their scenes at their earlier steps through the story view;
  Fienal is proven again.
- **Gifts (all modes, 2026-10-25): "NPC gift checks should NOT hold progression."** (Before:
  the 9 gift sites the late proof reaches could.)
- Keys go in chests (hub-proven) and boss checks; drops per `drop_progression` /
  `area_keys_in_drops`; never shops, gifts or crafts.
- Goals (indalecio / limiter_off / + Iseria Queen) work in hub mode too (Filia's scene needs
  chapter >= 58 and flag 730, which the hub has; the Clik flags are preset by `goal.flags`).

## The plan

Order (story_flags.md 5.1, adjusted for D2 / D3): W1 hub state data + W3 boss chamber table →
the VM (P2 chapter lock + P6 story view) → P1 hub New Game → chamber specs + headless checks per
boss → P5 the chambers the hub can't reach (Eluria Tower past 557: Shin 569 / Berle 575; the
Synard Plant 269; the Field of Intelligence's 554; Mihne 454; the arena 305) → the hub proof
(late proof from the hub start with the chapter lock) and a `--bosses` check → W6 APWorld
(world_mode hub, regions, dungeon keys, boss locations, the Fienal option, the start option,
seed blocks) → docs.

| Agent | Work | Log |
|---|---|---|
| gifts | APWorld: gifts never progression, all modes (0.2.0) | docs/handoff/apworld2.md |
| hubdata | W1 hub state data (`docs/ap/hub_state.json` + tool) and W3 the boss chamber table (chambers, guards, scene entry, story-view spec per boss), and the seed's `hub` / `bosses` format in seed_format.md | docs/handoff/hubdata.md |
| hubvm | Port: P2 chapter lock and P6 story view in the script VM (var_ptr / flag_ptr), behind the hub seed | docs/handoff/hubvm.md |
| (next) hubcore | Port: P1 hub New Game, start option, sidecar, chamber doors, P5 | |
| (next) hubworld | APWorld W6 | |
| (next) hubproof | W7 proofs | |

## Status

- 2026-10-25: decisions taken; gifts, hubdata, hubvm started.
- 2026-10-25: **gifts done.** rules.py `may_hold_progression` / `open_world_progression` treat
  `gift` as never-progression (like `drop` / `join` / `shop`), in either world mode, for this
  world's own keys and other worlds' progression alike. World tests 240 passed (was 235); the
  96-seed batch (full) regenerated, 96/96 exit 0, check.py (extended with the gift rule) 0
  errors; no fill failure with expel_progression off. docs/ap/apworld.md and
  docs/handoff/apworld2.md ("Session 4") updated; the player guide needed no change (it never
  claimed gifts may hold progression). Folds into the unreleased so2.apworld 0.2.0.
