# Handoff: Phase B build (tracker support: port 0.03.00 + so2.apworld 0.2.2 + pack 1.0.0)

Coordinator log (main session), started 2026-10-27. The plan is docs/plans/trackers.md (sections 5
B1-B7, 7 decisions); this file records how the build is split into agents, their state, and the
rules every agent gets. Resume from here plus each agent's own docs/handoff/<name>.md.

## The user's words
- 2026-10-27: "Next: port 0.03.00 + world 0.2.2, tracker support, from docs/plans/trackers.md."
- Decisions already made (trackers.md section 7): D1, D2, D3, D4, D6, D7 as recommended; D5 = the
  port renders maps from the player's own disc into PopTracker's user-override folder, nothing from
  the disc is published ("This keeps us trouble free from soliciting any IP"); D9 = all tracker work
  ships as port 0.03.00 + world 0.2.2 + pack 1.0.0.
- D10 (not asked before): taken as recommended (UT refuses with TrackerException and a clear
  message when the installed world can't reproduce the room's `logic_version`); told to the user.
- D8 (story awareness, B6's optional part): asked 2026-10-27; answer pending. Needed only at B6.

## Lanes (at most two agents at once: memory pressure crashed a session before)

| Lane | Order | Agent | Log | State |
|---|---|---|---|---|
| World | B1 UT yaml-less | utworld | docs/handoff/utworld.md | started 2026-10-27 |
| Port | B5 data-storage keys | dskeys | docs/handoff/dskeys.md | started 2026-10-27 |
| World | B2 Rule Builder | (after B1) | | queued |
| Port | map render command (D5) | (after B5) | | queued: research + layout contract first |
| World | B3 logic export | (after B2) | | queued |
| World | B4 PopTracker pack generator | (after B3; needs the map layout contract) | | queued |
| World | B6 UT map tab (+ D8 story events if yes) | (after B4 + B5) | | queued |
| Both | B7 docs, release checklist, release | (last) | | queued; release only with the user's go-ahead |

A fresh agent per assignment (rule in ~/.claude/CLAUDE.md), started from the previous one's log.

### The map render contract (for the port's render agent and B4)
- The shipped pack and a player's rendered override must share **one layout**: the same image file
  names and sizes, check dots at the same pixel positions. So the layout facts (which maps make up an
  area image, each map's rectangle, background size, the check positions as pixels) are computed once
  and committed as data (like docs/ap/doors.json / field.json: coordinates and ids, no pixels). The
  generator (B4) draws original placeholder images from them; the port renders the real backgrounds
  into the same rectangles.
- The port command writes only PNGs (and nothing else that is disc art) into
  `<Documents>/PopTracker/user-override/so2_shattered_stars/` at the pack's image paths, headless,
  reading the player's own disc. Never in git, never in a release.
- AP off / no-seed runs untouched; the command does not save the config or touch saves.

## Rules every agent gets
- Edits only in the files named in its brief; its log docs/handoff/<name>.md kept current after each
  finished step and at least every 30 minutes (done / in progress with files / next / how to verify /
  rules given).
- Commits on main after each verified piece, docs first, staged by path (never `git add -A`); message
  ends with `Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>`. Never push, tag
  or publish. If git reports index.lock, wait and retry; never delete the lock.
- A denied tool call: don't retry; write the blocked step in the log and stop with a message naming
  the exact command.
- Port: headless only (`--headless`, SDL_VIDEO_DRIVER=offscreen, SDL_AUDIO_DRIVER=dummy), every game
  process under tools/memcap.py (4 GB), at most 2 game processes, own build dir engine/build-<name>,
  builds -j6, scratch XDG_CONFIG_HOME, never a config-saving flag, never the user's saves or
  ~/.config/shattered-stars/config.ini; kill only by exact PID.
- World: Python from scratchpad/ap/Archipelago with ../ap-venv/bin/python and
  SKIP_REQUIREMENTS_UPDATE=1, pytest -n 6, generation under prlimit --as (8 GB); the batches
  (tools/ap_batch.sh) stay byte-identical unless the brief says otherwise.
- AP off = the disc's game untouched; nothing from the discs in git or in anything published.
- Cap command output; long jobs in the background with deadlines; scratch under scratchpad/<name>/.
- Roll over at ~400k context: bring the log up to date and stop.

Discs: NA disc 1 = `/run/media/ironjade/7b066518-e6be-41f3-909e-76e7f05db6e4/Emulation/Roms/PS/Star Ocean - The Second Story (Disc 1)/Star Ocean - The Second Story (Disc 1).cue`
(disc 2 beside it, "(Disc 2)"); set SS_DISC1 to the .cue, not the folder.

## Log
- 2026-10-27: coordinator log written; utworld (B1) and dskeys (B5) started.
