# Handoff: Claude's sword missing in Rena's game (battle graphics)

Queued 2026-10-25 by the main session. No agent yet.

## The user's report (verbatim)
> In Rena's story mode, when Claude fights Alan-Tax and thereon, the 'sword' that he is supposed
> to be using graphically isn't there, it looks like a stretched version of his air punching
> sprite. The Claude start that I did had the sword normally.

## To find out
- Plain game or Archipelago? Reproduce both: a plain Rena game at Claude's join (Salva Drift 61,
  Alen-Tax encounter 191) and onwards, and an AP story-mode seed on Rena's route (and with
  character_items / party v2 if the user's seed used them: ask the coordinator to ask the user for
  the seed if it matters). Compare with Claude's own game.
- Claude's weapon in Rena's game: his equipment record when he joins (script ext 0xae party_add,
  or the AP character item / party v2 join path), the weapon's battle model / texture / animation
  set (battle weapon loading), against PCSX-Redux on a Rena save at the same point (`-cli`, scratch
  HOME, scratch card copies) where a reference is needed.
- Fix the port to match the original (docs/handoff/battle.md, effects.md for the battle model
  code). If the original itself shows it this way: report to the coordinator (the user decides).

## Rules
As in the other handoffs: headless only, own build dir engine/build-claudesword, memcap 4 GB, at
most 2 game processes, scratch XDG_CONFIG_HOME, scratchpad/claudesword/, no config-saving flags, no
commits, this file as the log (Done / In progress / Next / How to verify), roll over at ~400k, stop
and report on a refusal; suites with counts before "verified" (ss_tests 99/0, ss_game_tests 172/0,
smoke 161/0). Enhance, never degrade.
