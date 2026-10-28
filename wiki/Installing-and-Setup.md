# Installing and Setup

This page takes you from download to the title screen. It covers what you need, how to choose
your disc images, and the setup program's pages.

## What you need

| | Windows | Linux |
|---|---|---|
| System | 64-bit Windows | 64-bit (x86-64) Linux with glibc 2.36 or newer (the release is built on Debian 12) |
| Graphics | OpenGL 3.3 | OpenGL 3.3 |
| Controls | Keyboard, or any gamepad SDL recognises | The same |
| The game | Images of your own *Star Ocean: The Second Story* discs, North American, European, French or German release | The same |

There is no macOS build today.

### Your disc images

Shattered Stars plays four releases of the game. The setup program checks each image and
tells you which disc and release it is:

| Release | Disc 1 | Disc 2 | Text |
|---|---|---|---|
| North American | SCUS-94421 | SCUS-94422 | English |
| European | SCES-02159 | SCES-12159 | English |
| French | SCES-02160 | SCES-12160 | French |
| German | SCES-02161 | SCES-12161 | German |

The European, French and German releases are supported from version 0.02.00. They play with
their own text and data on the North American game logic, at 60 Hz;
[Playing](Playing#european-french-and-german-discs) has what that means.

**Both discs must come from the same release.** Setup refuses a pair from two releases, and
says which is which. Each release keeps its own saves (see
[Playing](Playing#saves-and-memory-cards)).

Images of the Japanese release (SLPM-86105 / SLPM-86106) are recognised, but not supported
yet: setup says so and lists the releases it plays.

These image formats work:

- a `.cue` file together with its `.bin` file (choose the `.cue`);
- an `.iso` or `.img` image.

CHD, PBP, ECM, CSO, MDS, CCD, M3U and TOC files can't be read yet; convert those images to
`.cue` / `.bin` first. If your image is inside a `.zip` or similar archive, extract it first.

**Nothing is included and nothing can be downloaded.** You need to make the images from discs
you own. Disc 2 is optional at first: you can play until the story changes discs, and the game
asks for it then, as the console did. (An [Archipelago](Archipelago#hub-mode) seed in hub mode
needs it from New Game.)

## Downloads

Each release has two packages, one per system, on the
[Releases page](https://github.com/IronJade/Shattered-Stars/releases/latest). The 0.03.00
files are:

| Package | Contents |
|---|---|
| `ShatteredStars-0.03.00-win64.zip` | `shattered-stars.exe` (the game), `setup.exe` (the setup program), `SDL3.dll`, the README, `LICENSE`, the third-party licences and an empty `discs` folder |
| `ShatteredStars-0.03.00-linux-x86_64.tar.gz` | `shattered-stars` (the game), `shattered-stars-setup` (the setup program), the README, `LICENSE`, the third-party licences, an empty `discs` folder, and `install-desktop-entry.sh` with a `desktop` folder (the optional menu entries and icon) |

The packages are self-contained: there is no installer, and nothing else to install. If no
release is published yet, see [Building from Source](Building-from-Source).

The Archipelago world and the PopTracker pack are downloads of their own, from their own releases:
[so2.apworld 0.2.2](https://github.com/IronJade/Shattered-Stars/releases/tag/apworld-v0.2.2)
([Archipelago](Archipelago)) and
[PopTracker pack 1.0.0](https://github.com/IronJade/Shattered-Stars/releases/tag/poptracker-v1.0.0)
([Trackers](Trackers)).

## Installing

**Windows:** unzip `ShatteredStars-0.03.00-win64.zip` anywhere you like, for example in your
Documents folder, and run **`setup.exe`**.

**Linux:** unpack the archive and run the setup program:

```sh
tar xzf ShatteredStars-0.03.00-linux-x86_64.tar.gz
cd ShatteredStars-0.03.00-linux-x86_64
./shattered-stars-setup
```

You can also start the game itself (`shattered-stars.exe` or `./shattered-stars`) straight
away: while it has no Disc 1, it opens the setup program by itself.

### Linux: the application menu and the taskbar

To start the game from your desktop's application menu, run the package's
`install-desktop-entry.sh` once, from the unpacked folder:

```sh
./install-desktop-entry.sh
```

It adds **Shattered Stars** and **Shattered Stars Setup** to the menu, with their icon, for
your user only: two `.desktop` files in `~/.local/share/applications` and the icon in
`~/.local/share/icons/hicolor` (no root needed). It is optional, but GNOME and KDE Plasma on
Wayland show a program's name and icon in the taskbar only once its desktop entry is
installed; without it the game still runs, with a generic icon there. The entries point at the
programs in that folder, so run the script again after moving or updating the folder.
`./install-desktop-entry.sh --uninstall` removes them.

On Windows nothing is needed: the icon and the names ("Shattered Stars", "Shattered Stars
Setup") are built into the two programs.

### The quickest way: the discs folder

Put your disc images in the **`discs`** folder next to the game. Subfolders are fine. The game
finds them by itself, and the setup program's **Auto-detect** button fills them in.

## The setup program

The setup program has eight pages, listed down the side. The buttons at the bottom are
**Quit**, **Save** and **Save & Play**. If you try to quit with unsaved changes, it asks first.

### Discs

<p align="center">
  <img src="https://raw.githubusercontent.com/IronJade/Shattered-Stars/main/docs/images/setup-discs.png" alt="The setup program's Discs page with both North American discs chosen and verified" width="640">
</p>

Choose **Disc 1** and **Disc 2** with **Browse...**, or press **Auto-detect**. Next to each
disc, a line tells you what the program found, for example "Disc 1, French release: OK", or
what is wrong ("That is Disc 2. Please choose Disc 1 here.", "File not found. Was it moved or
renamed?"). **Open discs folder** opens the folder the game searches; hold the mouse over the
button to see every folder it looks in.

If the two discs come from different releases, Disc 2 is marked as an error, **Save** and
**Save & Play** are disabled, and the bottom bar says which release each one is, for example
"Disc 2 is the European release and Disc 1 the French one. Both discs must be the same
release." Choose a matching Disc 2 (or none for now) to go on.

### Display

| Setting | Choices |
|---|---|
| Window mode | **Windowed** (a window you can move and resize), **Borderless full screen** (covers the screen at the desktop's resolution; switching to other programs is instant) or **Full screen** (takes over the monitor at the resolution you choose) |
| Monitor | Shown when you have more than one display |
| Window size | Windowed mode: 320 x 240 times 1, 2, 3 and so on; 3x is the default |
| Resolution | Full screen mode: the desktop's resolution or any mode the monitor offers |
| Vertical sync | Shows each frame in step with the monitor's refresh, so the picture never tears |
| Dithering | The PlayStation's colour dithering, as on a real console. Off gives smooth, banded gradients |

The picture keeps its original shape at any size. While playing, **F11** or **Alt+Enter**
switches between the window and full screen.

### Sound

**Sound on**, and a **Volume** slider from 0 to 100%. Music, voices and sound effects all
follow this volume.

### Controls

A table with a box for each PlayStation button, one column for the keyboard and one for the
gamepad. Click a box, then press the key or gamepad button you want for it. **Esc** cancels,
**Delete** unbinds, and **Reset to defaults** puts everything back. The page warns you when
one key or button is set to press two PlayStation buttons.

Under **Analog stick** you can choose whether the left stick also works the d-pad (in menus,
as on the console), and set the stick's **Dead zone**. Raise the dead zone if your character
walks by itself; **Game's** goes back to the game's own value.

The default buttons are listed on [Playing](Playing#controls).

### Gameplay

Two dialogue settings, both off by default. With them off the game plays exactly as on the
console; with them on, only the time messages wait changes, never what happens. They work in
every game, Archipelago seeds included.

| Setting | What it does |
|---|---|
| Instant text | Each message page appears at once instead of letter by letter, and text that scrolls up moves at once. Pauses that scenes time themselves still wait. |
| Auto-advance | A message waiting for the confirm button moves on by itself, exactly as if you had pressed it. It stops at every choice (yes / no, shops, inns, names): you always choose. |
| Wait | How long auto-advance waits once the page is complete: 0.5, 1 (the default), 1.5, 2 or 3 seconds |

You can still press the button yourself at any time. The game's own message speed (in the camp
menu's options) still applies while instant text is off.

### Memory cards

The game saves on memory card images, just like a PlayStation's cards. One save takes one of a
card's 15 blocks. For each of the two slots you can:

- tick or untick **Card inserted**;
- choose a card file with **Browse...**, or go back to the default card;
- see what is on it: how many Star Ocean saves it holds (of every release), or a warning if it
  is unformatted or not a card image at all.

Card 1 is `memcard1.mcd` in the settings folder (see below). If the file doesn't exist yet,
the game creates a new, formatted card when it starts. Slot 2 is empty until you insert a card
there (its default file is `memcard2.mcd`); each slot needs its own file. Raw 128 KB cards from emulators (`.mcd`,
`.mcr`) work as they are. A DexDrive save (`.gme`) can be turned into a card with **Make a .mcd
copy**. More on saves: [Playing](Playing#saves-and-memory-cards).

While an Archipelago seed is chosen (and Archipelago is switched on), the page says so: the game
then plays on that seed's own memory card, not on these, and uses them again once the seed is
cleared or Archipelago is switched off.

### Mods

<p align="center">
  <img src="https://raw.githubusercontent.com/IronJade/Shattered-Stars/main/docs/images/setup-mods.png" alt="The setup program's Mods page: the installed mods in load order with tick boxes, conflict and error marks, and the Install from .zip, Move up, Move down and Remove buttons" width="640">
</p>

Your installed mods, in the order they apply, each with a tick box to turn it on or off. The
buttons beside the list are **Install from .zip...**, **Move up**, **Move down** and
**Remove...**; below it, the selected mod's details, README and any conflicts with other
mods. Above it is the mods folder, with **Browse...**, **Default** and **Open folder**. With no
mods enabled, the game plays unmodified. Everything on this page is explained in
[Installing and Managing Mods](Installing-and-Managing-Mods).

### Archipelago

<p align="center">
  <img src="https://raw.githubusercontent.com/IronJade/Shattered-Stars/main/docs/images/setup-archipelago.png" alt="The setup program's Archipelago page with a seed file loaded and a server address, slot name and password filled in" width="640">
</p>

Where you choose an Archipelago seed and, to play online, the room's server, your slot name and
its password. With no seed, the game plays as the original. The page has four parts:

- **Seed:** **Browse...** starts in Archipelago's own `output` folder (or in the folder of the
  seed already chosen). Choose the `AP_<number>.zip` the generator made (no need to unpack it)
  or a loose `.apso2` seed file; **Clear** removes it. If the zip holds seeds for several Star
  Ocean players, a **Your slot** list appears. The line below says which seed and slot it is
  and how many locations it has, or what is wrong. A
  [hell seed](Archipelago#hell-seeds) gets a warning under it.
- **Saves** (once a seed is chosen): each seed plays on its own memory card. This part says
  whether it holds saves yet, and **Open folder** shows it.
- **Server:** the **Address** (leave it empty to play offline), the **Slot name** (the box hints
  at the one in your seed) and the **Password**, with **Show**.
- **Trackers** (at the bottom; it needs only a valid Disc 1, no seed): **Render tracker maps for
  PopTracker** draws the PopTracker pack's maps from your Disc 1 into PopTracker's override folder
  for the pack (862 images, about 20 seconds). A progress bar shows how far it is, and the rest of
  setup can be used meanwhile; at the end it says where the images went, with **Open folder**.
  Closing setup stops it and keeps the images made so far. See
  [Trackers](Trackers#maps-from-your-own-disc).

**The on / off switch.** The Archipelago row in the list of pages has a switch of its own. Off,
the game plays exactly as the original, on your ordinary memory cards, and the seed and server
stay filled in for later; the page then shows a warning line saying so. Hold the mouse over the
switch to see which way it is set.

Everything about playing a seed is on [Archipelago](Archipelago#connecting).

## The first launch

1. Choose your discs on the **Discs** page. The status line at the bottom says "Ready to play."
   once Disc 1 is accepted.
2. Look over **Display**, **Sound**, **Controls** and **Gameplay** if you like. The defaults work.
3. Press **Save & Play**.

The game opens with the startup logos and the opening movie, then the title screen: **New
Game**, **Continue** and **Voice Collection**, as in the original. New Game goes through the
original's own start-up choices, including whether to follow Claude or Rena. (An
[Archipelago](Archipelago#the-starting-character) seed can make that choice for you, or add a
character select.)

<p align="center">
  <img src="https://raw.githubusercontent.com/IronJade/Shattered-Stars/main/docs/images/title-screen.png" alt="The title screen, with New Game, Continue and Voice Collection" width="560">
</p>

Later, you can start the game directly with `shattered-stars.exe` / `./shattered-stars`. It
reads the same settings.

## Where your files live

The settings, memory cards and mods are kept in your user folder, not next to the game, so a
new version of the game can replace the old one without touching them.

| | Windows | Linux |
|---|---|---|
| The game | `shattered-stars.exe` | `shattered-stars` |
| The setup program | `setup.exe` | `shattered-stars-setup` |
| Settings | `%APPDATA%\ShatteredStars\config.ini` | `~/.config/shattered-stars/config.ini` (or `$XDG_CONFIG_HOME/shattered-stars/`) |
| Memory card 1 | `memcard1.mcd` beside `config.ini` | The same |
| Archipelago seeds' memory cards | `archipelago\<seed>_<slot>\memcard1.mcd` beside `config.ini` | `archipelago/<seed>_<slot>/memcard1.mcd` beside `config.ini` |
| Mods | `mods\` beside `config.ini` | `mods/` beside `config.ini` |
| Disc folders searched | `discs\` next to the game, or beside `config.ini` | `discs/` next to the game, or beside `config.ini` |

`config.ini` is plain `key=value` text. The setup program writes it, and keeps any keys it
doesn't know.

**Back up your memory card** by copying `memcard1.mcd` somewhere safe. For an Archipelago
seed's card, copy its whole folder, so the `.ap.json` files beside the card go with it.

## Updating and uninstalling

To update, unpack the new package and use it in place of the old one. Your settings and memory
cards stay where they are. On Linux, run the new package's `install-desktop-entry.sh` if you
use the menu entries and the new folder has a different name. If you keep your disc images in the old package's `discs` folder,
move them across (or into the `discs` folder beside `config.ini`, which every version reads).

To uninstall, delete the game's folder. On Linux, if you added the menu entries, run
`./install-desktop-entry.sh --uninstall` first. To remove your settings and saves as well,
delete the settings folder listed above, after backing up any memory cards you want to keep.

## If something goes wrong

| Problem | What to do |
|---|---|
| "Not a Star Ocean: The Second Story disc image" | The file is a CD image, but of something else. Check you chose the right file |
| "Star Ocean: The Second Story, Japanese release ...: not supported yet" | The Japanese discs aren't supported yet. Use the North American, European, French or German discs ([the table above](#your-disc-images)) |
| "Star Ocean: The Second Story, but a release Shattered Stars doesn't know" | A release that isn't in [the table above](#your-disc-images). The message lists the releases that work |
| "Both discs must be the same release." | Disc 1 and Disc 2 come from different releases. Choose the Disc 2 of Disc 1's release |
| "The .cue file needs ... in the same folder" | The `.cue` names a `.bin` file that isn't beside it. Keep both files in one folder with their original names |
| "This is a compressed archive" | Extract it first, then choose the `.cue` inside |
| The game asks for Disc 2 | Choose Disc 2 in setup. See [Playing](Playing#changing-discs) |
| Your character walks by itself | Raise the stick's dead zone on the Controls page |

Still stuck? See the [FAQ](FAQ), or [report a bug](Contributing#reporting-a-bug).
