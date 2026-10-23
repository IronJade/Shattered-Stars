# Handoff: mod manager documentation (agent "mod-docs")

Sources: docs/notes/mods.md, docs/handoff/modmanager.md, mods/README.md, mods/index.json, the
two sample mods, .github/PULL_REQUEST_TEMPLATE/mod_submission.md, .github/workflows/mods.yml,
tools/{so2_mod,mod_check,mod_index,mod_verify}.py (--help), engine/src/game/field/mods.h.

## Rules this agent was given (all mandatory)

- Wiki repo (scratchpad/wiki) only: check `git config user.email` is the noreply address before
  each commit; never change git config; commit messages end with the Co-Authored-By line; push
  to origin, never force.
- Main repo: write only docs/images/ (copied screenshots + README lines), README.md (the
  "Mods and the mod manager" section only), this log, and ONE line in
  scratchpad/checkpoint_notes.txt prefixed "mod-docs: ". No commits or pushes of the main repo.
  No code edits.
- Tools run for --help / tries: scratch XDG_CONFIG_HOME under scratchpad/mod_docs/; never
  ~/.config/shattered-stars. Headless only. Kill only by exact PID.
- No email address anywhere; no game assets beyond the setup screenshots; never quote the
  Prima guide. Every fact from the sources; real flags only. Cap output. Past ~300k tokens:
  push, update this log, end the turn.

## State (newest first)

- DONE. Wiki commit 5bb484b pushed to the wiki's origin (master): new Making-a-Mod.md,
  Installing-and-Managing-Mods.md, Submitting-a-Mod.md; Modding.md rewritten as the hub;
  Installing-and-Setup.md Mods section (screenshot + link); _Sidebar.md (three pages); stale
  "mod manager being built" lines fixed in Home.md, FAQ.md, Playing.md (setup page table),
  Contributing.md (bug-report table). Images use raw.githubusercontent URLs (resolve once the
  main repo is pushed).
- README.md "Mods and the mod manager" (46 -> 50 lines): "Coming next" replaced by the real
  process (strict check, mod_verify, maintainer-only verification), screenshot
  docs/images/setup-mods.png, links to the three wiki pages + Modding. Uncommitted (the
  coordinator commits).
- Example outputs in the pages are real: so2_mod.py run on disc 1, mod_check.py run on
  scratch packages (scratchpad/mod_docs, deleted when done).
- Source disagreements found (for the coordinator / user):
  - .github/PULL_REQUEST_TEMPLATE/mod_submission.md's checklist says
    `python3 tools/mod_check.py mods/<id>` (no --strict) while CI and mods/README.md use
    --strict. The wiki tells creators to use --strict.
  - docs/notes/mods.md section 8 shows mod_verify `--jobs 4` and `--out
    scratchpad/modverify/<id>`; the tool's defaults are --jobs 3 and
    scratchpad/modverify/<id>-<time>.
  - The game's [mods] lines are printed with fprintf(stderr), so shattered-stars.log holds them
    only on Windows (where stderr is redirected to it); on Linux they appear only in a
    terminal. The wiki says so. The PR checklist's "the game's log shows every line applied"
    therefore needs a terminal on Linux.
  - mod_check also has a same-place *warning* (an exact repeat), not only the error the design
    lists. Not mentioned in the wiki beyond the error.
  - The mods-conflict and mods-replace shots (as all setup shots) show local paths; the
    conflict shot's info panel shows the full scratch folder path including the drive UUID.

- Screenshots done: docs/images/setup-mods.png (mods-manager), setup-mods-conflict.png
  (mods-conflict), setup-mods-replace.png (mods-replace); RGBA -> RGB (alpha was all 255),
  Pillow optimize=True, pixel-identical; ~2.4 MB -> ~210-255 KB each. docs/images/README.md:
  three lines, and "the two setup-program shots" -> "the setup-program shots".
