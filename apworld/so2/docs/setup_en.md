# Star Ocean: The Second Story Setup Guide

This guide covers playing Star Ocean: The Second Story in an Archipelago multiworld on
Shattered Stars, a native port of the game.

## Required Software

- **The Shattered Stars release**, for Windows or Linux. It's a self-contained package; no
  separate emulator or runtime is needed.
- **Your own Star Ocean: The Second Story disc images** (PlayStation, North American
  release) — for example `.cue`/`.bin` pairs. Shattered Stars reads your discs at run time;
  it does not ship with any game data, and there is nowhere to download it. You need to supply
  disc images you made from your own copy of the game.
- **This world's `.apworld` file** and a YAML (below), like any other Archipelago game.

## Running the Setup Program

The Shattered Stars release bundles a graphical setup program alongside the game itself. Run
it to configure things before you play:

- Its **Discs** page is where you point the program at your disc images.
- Its **Archipelago** page (below) is where you give it your seed file, server, slot name
  and password.

Save your settings, then launch the game from setup (or close setup and launch the game
directly — it reads the same configuration).

## Making Your YAML

Like any Archipelago game, your options are set in a YAML file. There are two ways to make it:

- **With a form:** once `so2.apworld` is installed, open the Archipelago Launcher and choose
  **Options Creator**. Pick *Star Ocean: The Second Story*, set the options with buttons and
  sliders, and export the YAML.
- **By hand:** the template for this world ships alongside its `.apworld` file as
  `Star Ocean The Second Story.yaml`. Fill in the options you want (`world_mode`,
  `character_items`, `traps`, and so on; see the game page for what each one does).

Put the finished YAML wherever you'd normally place a player YAML for generation: your
Archipelago install's `players` folder, or wherever your group's generation process expects
player files. This step is the same as for any other Archipelago game; nothing
about it is specific to this world.

## Generating

Generation is standard Archipelago generation: run it the same way you would for any
multiworld (Archipelago's own `Generate` process, or however your group normally generates a
room). Archipelago writes one archive for the whole multiworld, `AP_<number>.zip`, into its
`output` folder (on Linux `~/.local/share/Archipelago/output`, on Windows
`C:\ProgramData\Archipelago\output`). Inside it is one file per player; a Star Ocean player's
ends in `.apso2`. Shattered Stars reads the archive as it is, so there's no need to unpack it,
though a loose `.apso2` (one the host sent you on its own, say) works just as well.

## Connecting

Shattered Stars has a native Archipelago client built into the game itself — you don't run a
separate program to connect. Open the setup program's **Archipelago** page:

1. **Seed.** Click **Browse** and pick the `AP_<number>.zip` archive (Browse opens Archipelago's
   `output` folder) or your `.apso2` file. If the archive holds seeds for more than one Star
   Ocean player, a **Your slot** list appears: choose yours.
2. **Server address.** Enter the room's server address. The client works with either an
   insecure (`ws://`) or secure (`wss://`) server address — use whichever the room host gives
   you.
3. **Slot name.** Enter your slot name. The field hints at the name your seed file was
   generated for, so you'll usually just confirm it.
4. **Password**, if the room has one. There's a **Show** button if you want to check what
   you typed.

Save and start the game (or Save & Play). It connects using the native client, the same way
the game plays normally — there's no separate client window to manage.

**Your saves.** Each seed (and slot) saves on its own memory card, which the game makes the
first time you play it. Your ordinary memory cards aren't inserted while a seed is chosen, so a
finished or unrandomized game can't be loaded into the seed by mistake, and the seed can never
save over your ordinary saves. The Archipelago page shows the seed's card under **Saves** (with
**Open folder**, for backups). Clear the seed to play on your ordinary cards again. Saves an
earlier Shattered Stars made for the same seed on your ordinary card are copied onto the seed's
card the first time (your card itself isn't changed).

## Playing Offline

Shattered Stars can also play a generated seed by itself, with no server and no multiworld
connection. On setup's **Archipelago** page, choose the archive or the `.apso2` file and leave
**Server** empty (or start the game with `--ap-seed FILE`, adding `--ap-seed-entry NAME` for an
archive with several Star Ocean seeds). Your own locations are shuffled and
checked as usual, but nothing is sent to or received from other players, since there's no one
else's game to connect to. It's a good way to try the randomizer without hosting or joining a
room.

## Troubleshooting

- **"Did not load ... as its game ... is already loaded."** Archipelago refuses to load the
  same game twice. Make sure this world's `.apworld` (or an unpacked copy of it) exists in
  only one of your Archipelago worlds locations — don't have it in both the main `worlds`
  folder and `custom_worlds` at the same time.
- **Wrong disc image.** Shattered Stars only recognizes the North American release of Star
  Ocean: The Second Story. A disc image from another region's release, or one that isn't this
  game at all, will be identified as such on the Discs page rather than accepted — double check
  you've pointed setup at the right disc 1 (and that disc 2 is nearby, since the game looks for
  it automatically).
- **Connection refused.** If the server turns down the connection — for example because the
  slot name or password doesn't match the room — the client reports the refusal rather than
  silently failing to connect; double-check the slot name and password on the Archipelago page
  against what the room host gave you.
- **A seed that won't load.** The seed is checked when you pick it; if the archive holds no
  Star Ocean seed (Star Ocean wasn't one of the games generated), or the seed isn't valid (or is
  damaged), setup will tell you rather than letting you start the game with it.
