# Documentation screenshots

Every image here is a screenshot of Shattered Stars (this port) running, captured from its
own OpenGL output (not a raw VRAM dump, not an emulator) for the README and wiki. They are
the one approved exception to "no game assets in git" (`docs/CONVENTIONS.md`): nothing else
from the discs is committed. Captured headless (`SDL_VIDEO_DRIVER=offscreen`), at 1280x960
for every in-game shot; the setup-program shots are the setup window's own size,
920x660. How each one was produced, including the exact command: the readme_shots handoff log.

- `title-screen.png` — the title screen (logo, New Game / Continue / Voice Collection).
- `field-arlia.png` — Arlia, an early Expel town, near its fountain.
- `world-map.png` — the world map, the party's leader approaching a town gate.
- `battle-spell.png` — a battle with two spell effects on screen at once (Bowman's
  Thunderbolt and Rena's ice spell).
- `boss-battle.png` — a boss battle (encounter 538), the boss mid-cast inside a glowing
  magic circle.
- `camp-status.png` — the camp menu's Status page: a character's stats, equipment and
  portrait.
- `shop-equipment-wizard.png` — Arlia's Equipment Wizard, a shop-like screen that compares
  and swaps gear.
- `late-game-fienal.png` — a late-game Energy Nede location: the Final Save Point, Fienal's
  ninth floor (map 671).
- `ap-item-received.png` — Archipelago in game: a real item (Blueberries) received from
  another player (Bob) over a local multiworld server, with the in-game banner.
- `setup-discs.png` — the setup program's Discs page, both discs verified (recaptured for 0.02.50: the four releases' text, the Gameplay page, the version; `discs-ok.png`).
- `setup-archipelago.png` — the setup program's Archipelago page, a seed file loaded (its Saves part and the sidebar's on / off switch; recaptured for 0.02.50 with `port/tools/setup_screenshots.sh`, `archipelago-seed.png`).
- `setup-mods.png` — the setup program's Mods page: the installed mods in load order, two
  marked with a conflict and one that can't load (rendered by `port/tools/setup_screenshots.sh`,
  shot `mods-manager`, with test packages).
- `setup-mods-conflict.png` — the Mods page's info panel for a mod in a conflict, each
  conflict explained in a sentence (`port/tools/setup_screenshots.sh`, shot `mods-conflict`).
- `setup-mods-replace.png` — installing a .zip of a mod that is installed already: setup asks
  before replacing it (`port/tools/setup_screenshots.sh`, shot `mods-replace`).
