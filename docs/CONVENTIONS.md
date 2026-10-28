# Conventions to keep

The project's standing rules for everyone working in this repo, people and agents. They were
the "Conventions to keep" section of the old `docs/HANDOFF.md` (removed 2026-10-28; the new
one is a one-screen state file).

- Enhance, don't degrade: the port runs at full speed (no modelling of the original's
  slowdown), and the original's bugs are commented out and corrected, each listed in
  `docs/notes/original_bugs.md`. Save data stays byte-compatible.
  A fix never changes the play state: hit timing, hits, damage, difficulty and pacing stay
  the original's, measured against PCSX-Redux before the fix goes in.
- Agents keep their `docs/handoff/<area>.md` complete enough to resume from, because each
  one is replaced by a fresh agent once its context passes ~400k tokens (cost per step
  grows with context size).
- No game code or assets in git; data is read from the user's discs at run time. One
  exception, the user's decision (2026-10-23): a handful of documentation screenshots of the
  port running, in `docs/images/` only, used by the README and wiki. Nothing else from the
  discs (no extracted textures, sprites, audio, logos or data files). The application icon
  (`res/icon/`) is an original design drawn from vector shapes by `tools/make_icon.py`;
  change it there, never with game art, a logo or a trademark.
- Headless only (`--headless`, `SDL_VIDEO_DRIVER=offscreen`, `SS_VRAM_PNG`); never pop
  windows; never play audio audibly in tests (`SDL_AUDIO_DRIVER=disk` / `dummy`).
  Third-party programs (Universal Tracker, PopTracker, Wine, Archipelago's clients) run with
  `DISPLAY`, `WAYLAND_DISPLAY` and `DBUS_SESSION_BUS_ADDRESS` unset and `XDG_RUNTIME_DIR`
  pointed at a scratch folder (libdbus falls back to `$XDG_RUNTIME_DIR/bus`; under `systemd-run`
  set it inside the scope, since systemd-run itself needs the real one), or under
  `~/.local/bin/xvfb-run`, so a missing folder or a prompt can't open a dialog on the user's
  desktop: on 2026-10-27 UT's folder picker did, and the user answered it.
- Never touch `~/.config/shattered-stars/config.ini` (no `--audio` / `--no-audio` or other
  flags that save the config; use `--headless` and `SS_*` variables) or
  `~/.config/pcsx-redux`; run PCSX-Redux only with `-cli` and a scratch HOME, on scratch
  memory-card copies — never the user's saves.
- Kill processes only by exact PID.
- Machine budget (20 cores, 62 GB RAM): at most 3 agents running at once; builds with -j8 when
  one or two agents build, -j4 when three do (big C++ files take about 1 GB per compile job);
  at most 4 game processes and 3 PCSX-Redux instances per agent. Four agents building -j8 with
  game, Redux and Wine runs on top filled RAM and swap on 2026-10-23 and ended the session.
  The actual cause that day was one runaway headless run (Europe disc, map 0: 28 GB in
  3.5 minutes), so every headless game run has a memory cap of about 4 GB (RLIMIT_AS, or a
  cgroup MemoryMax for ASan builds); a run that hits it counts as a failure, never a retry.
  `tools/memcap.py` implements it (`SS_MEM_CAP_GB`, default 4) and `tools/event_sweep.py`,
  `tools/battle_sweep.py`, `tools/ref_regress.py` and `tools/smoke_field_events.sh` apply it
  (the sweeps report "memory cap" and don't rerun that map); one-off runs:
  `prlimit --as=$((4<<30)) engine/build-X/shattered-stars ...`. The cause was a credits loop
  over an unreadable European card script with SS_REGION_READ_LOG on; fixed (the card
  script is read as the release's own run, and the region read / miss logs are bounded).
- `/tmp` is RAM-backed (tmpfs): keep large scratch (captures, dumps, extra builds) on disk under
  the git-ignored `scratchpad/<name>/` in the repo, and delete finished scratch promptly.
- Catalogue every reversed function in `docs/catalog` (LF line endings; write CSVs with
  `lineterminator="\n"`, and never open a file for writing in the same expression that
  reads it); prose goes in `docs/notes/`. Keep each module's rows in address order inside
  its existing block; don't reorder the file (it hides real changes in the diff).
- Parallel agents: separate build dirs (`engine/build-<name>`) and Ghidra project dirs; the
  REA server is shared, so re-open your binary before each batch of REA calls and check
  the result's subject.
- Commit after each completed piece, docs first; stage only finished work (build blobs from
  HEAD plus the finished hunks when agents share a file) and verify the staged tree with
  `git checkout-index` + build + tests before committing.
