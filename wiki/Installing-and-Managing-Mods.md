# Installing and Managing Mods

Mods change chests, doors, items, shops and enemies as the game loads them. **Your disc images
are never modified**, so turning a mod off brings the original back. You install and arrange
mods on the setup program's **Mods** page.

New to mods? The [Modding](Modding) page explains what they are. To make your own, see
[Making a Mod](Making-a-Mod).

## Where to get mods

The project keeps a **curated list** in the
[mods folder](https://github.com/IronJade/Shattered-Stars/tree/main/mods) of its repository.
Only the mods marked **verified** in its `index.json` are on the list: the maintainer has run
them with the real game and discs. What verified means exactly is on
[Submitting a Mod](Submitting-a-Mod#what-verified-means).

To use one, download its folder as a `.zip` (or download its files and zip them yourself) and
install it as below, or copy the folder into your mods folder.

A mod from anywhere else installs the same way. Setup checks that its package holds only mod
files, but only the curated list's verified mods have been tried with the game by the
maintainer.

## The Mods page

<p align="center">
  <img src="https://raw.githubusercontent.com/IronJade/Shattered-Stars/main/docs/images/setup-mods.png" alt="The setup program's Mods page: the installed mods in load order with tick boxes, two marked with a yellow conflict sign and one with a red error sign, and the Install from .zip, Move up, Move down and Remove buttons" width="640">
</p>

From top to bottom:

- **Mods folder:** where your mods are, with **Browse...** (choose another folder),
  **Default** (back to the standard one) and **Open folder** (show it in your file manager).
  The standard folder is `mods` beside `config.ini`: `%APPDATA%\ShatteredStars\mods\` on
  Windows, `~/.config/shattered-stars/mods/` on Linux.
- **Installed mods**, applied top to bottom. Each row has a tick box (enabled or not), the
  mod's name and version, its author and its tags. A yellow **!** marks a conflict with another
  enabled mod; a red **x** marks a mod that can't be loaded.
- The buttons beside the list: **Install from .zip...**, **Move up**, **Move down** and
  **Remove...**.
- A summary line under the list, such as "2 mods enabled, no conflicts." or "2 conflicts among
  the enabled mods (marked in yellow): select one for the details."
- **The info panel** for the mod you click: its name, version and author, its id, the oldest
  port version it needs, its tags and homepage, its description, its files with how many lines
  of each kind they hold, the conflicts it is part of, its README, and its folder.

The **Mods** item in the sidebar shows a yellow dot when enabled mods conflict, and a red dot
when an enabled mod can't be loaded.

Ticking, unticking and reordering are settings like any other: they take effect when you press
**Save** or **Save & Play**, and **Quit** asks before throwing them away.

## Installing a mod from a .zip

1. Press **Install from .zip...** and choose the file.
2. Setup checks the archive. It must hold a mod package (a `mod.json`, its `.patch` files and
   at most a `README.md`), either at the top of the archive or in one folder inside it.
3. If it passes, setup unpacks it into your mods folder, in a folder named after the mod's id.
   The new mod is **enabled, at the end of the list**: "Installed Tougher Landworms 1.0.0,
   enabled at the end of the list. Save to keep it enabled."
4. Press **Save** (or **Save & Play**).

If you quit without saving, the mod's files stay in your mods folder, but it is disabled.

If the archive isn't a valid package, nothing is installed and setup says why, for example "It
can't be installed: it holds preview.png, and a mod package may hold only mod.json, .patch
files and README.md."

### Updating a mod

Install the new version's `.zip` the same way. When that mod is installed already, setup asks
first:

<p align="center">
  <img src="https://raw.githubusercontent.com/IronJade/Shattered-Stars/main/docs/images/setup-mods-replace.png" alt="Setup asking: Hearn's store sells Resurrection Bottles is installed already (version 1.0.0). Replace it with version 1.1.0 from hearn-store-plus-1.1.0.zip? with Replace and Cancel buttons" width="640">
</p>

Choose **Replace**. The new version keeps the old one's place in the list, and stays enabled or
disabled as it was.

## Enabling and disabling

Tick a mod to enable it; untick it to leave it out. A disabled mod stays installed and keeps
its place in the list, so you can turn it back on later. With nothing enabled, setup says "No
mods enabled: the game plays unmodified."

### Mods copied in by hand start disabled

You can also copy a mod's folder straight into your mods folder (**Open folder** shows it). A
package that arrives this way **starts disabled**, at the end of the list, so nothing you drop
in changes your game until you choose to. Tick it and press **Save**. Until then, the game's
log notes it: `[mods] my-first-mod 1.0.0 is installed but not enabled (setup's Mods page enables it)`.

Only folders directly inside the mods folder are read, each holding its own `mod.json`.

## Load order: later wins

Enabled mods apply **from the top of the list to the bottom**. Usually the order doesn't
matter, because most mods change different things. It matters when two enabled mods change the
same thing:

- **Different changes to the same thing:** the mod **lower in the list (later) wins**. Its
  change is used and the earlier mod's change to that one thing is left out. Everything else
  both mods do still applies.
- **The same change:** both make it, and it isn't a conflict.
- **One built on top of the other** (a compatibility patch written for another mod's result):
  both apply, in order, and setup shows it as "builds on".
- **Written for different starting values:** at most one of them can apply; setup warns.
- **Overlapping parts of the same record:** the earlier one applies first, and the later one is
  most likely skipped; setup warns.
- **Items added to the end of the same shop** never conflict: every mod's item is added.

To choose which mod wins, select it and press **Move down** until it is below the other. Lines
inside a single mod never override each other.

## Reading conflict warnings

When enabled mods conflict, the mods involved get a yellow **!**, and the summary line says how
many conflicts there are. Click a marked mod to see each conflict explained in its info panel:

<p align="center">
  <img src="https://raw.githubusercontent.com/IronJade/Shattered-Stars/main/docs/images/setup-mods-conflict.png" alt="The Mods page's info panel for Crate jackpot, with two conflict warnings explaining that it and Arlia crate holds 500 Fol both change the same crate, and that Crate jackpot is later in the load order so its change is used" width="640">
</p>

Each warning names both mods, what they change, and which one wins. For example:

> "Arlia crate holds 500 Fol" and "Crate jackpot" both change map 18 word 0x82c: "Arlia crate
> holds 500 Fol" to 0xfffffe0c, "Crate jackpot" to 0xffffd8f0. "Crate jackpot" is later in
> the load order, so its change is used and "Arlia crate holds 500 Fol"'s is not. Move "Arlia
> crate holds 500 Fol" below "Crate jackpot" to use its change instead.

A conflict is a warning, not an error: the game still starts, and the later mod wins. To settle
it, either reorder the list so the mod you prefer is lower, or disable one of the two. A mod
with no conflicts shows "No conflicts with the other enabled mods."

## Removing a mod

1. Click the mod in the list.
2. Press **Remove...**. Setup asks, naming the folder it will delete: "Remove Crate jackpot
   0.3? Its folder is deleted: ..."
3. Press **Remove**. The mod's folder is deleted straight away.

To keep a mod for later without it affecting your game, untick it instead.

## Loose .patch files

Before mod packages existed, mods were single `.patch` files placed straight in the mods
folder. They still work. Setup shows all of them as one entry, **Loose patches**, which is
enabled unless you untick it. Its files apply in file-name order, and you can move the entry
up and down like any mod.

Loose patches aren't a package, so **Remove...** doesn't apply to them: press **Open folder**
and move or delete the files yourself. With only loose files and no packages, the game behaves
exactly as it did before the mod manager.

## Archipelago and mods

An [Archipelago](Archipelago) seed changes the world with the same kind of patch lines, and the
two combine:

- **The seed's changes always apply first, and no mod can override them.**
- A mod's line that changes the same place as the seed is skipped, since the seed's value is
  there. Everything else the mod does still applies.
- With a seed chosen on the Archipelago page (and Archipelago switched on), the Mods page
  includes it in its conflict warnings: "The Archipelago seed changes ... too; the seed wins, so ...'s line (...) is
  skipped."

A seed puts an item from the multiworld in the chests that are its locations, so a mod's
changes to those chests give way to the seed while you play it. Changes to doors, items, shops and enemies combine with a seed as
long as they don't touch the places the seed changes.

## Packages that can't load

A mod marked with a red **x** is never loaded, even if it is ticked. Click it to read the
reason in its info panel, for example "It can't be loaded: needs port version 9.0 or newer
(this is 0.03.00)." The reasons are:

- its `mod.json` is missing, isn't valid, or lacks a required field;
- a patch file its `mod.json` lists is missing from its folder;
- it needs a newer version of Shattered Stars than yours (its `min_port_version`).

Update Shattered Stars, get a version of the mod that suits yours, or remove it.

## Troubleshooting

### Where to see what the game did

The game lists what it does with mods in its messages. On Windows they are kept in
`shattered-stars.log`, beside `config.ini`. On Linux, start the game from a terminal and read
what it prints. At the start it lists the load order and any conflicts, then each mod line
once, as it applies or is skipped:

```text
[mods] load order: arlia-crate-fol 1.0.0, hearn-store-plus 1.0.0
[port] [mods] arlia-crate-fol/crate.patch:2: map 18 word 2092 0xffffff38 -> 0xfffffe0c
[port] [mods] hearn-store-plus/stock.patch:2: shop 1 +0x7 0 -> 0x115
```

Each line is labelled with the mod's id, the file and the line number (`id/file:line`; loose
files have no id).

### A change doesn't appear

- Is the mod ticked, and did you press **Save**?
- Does it have a yellow **!**? Another mod may win the conflict; see
  [Reading conflict warnings](#reading-conflict-warnings).
- Have you reached the moment the change applies? Chest and door changes apply when the map
  loads, shop changes when the shop opens, item changes when the game loads its item table (a
  battle, a shop or the camp menu), and enemy changes when a battle with that enemy starts.
- Are you playing an Archipelago seed that changes the same place? The seed wins.

### A line was skipped because the data differs

Every mod line carries the value it expects to find, and changes nothing unless that value is
there. When it isn't, the log says so:

```text
[port] [mods] other/x.patch:7: item 21 +0 holds 0x190, not 0xc8: set by dear/p.patch:1 earlier in the load order; skipped
```

This means one of the following:

- **Another mod changed it first.** The log names that mod's line ("set by ... earlier in the
  load order"). Reorder the two mods, or disable one.
- **The Archipelago seed changes it.** The seed always wins.
- **The mod was written for different data**, such as a value that differs in the release you
  play. Items, shops, enemy stats and chests are the same in every supported release, but some
  door and other script words differ, mostly on the French and German discs
  ([Modding](Modding#mods-and-the-releases)). The mod's other lines still apply; ask the mod's
  author if a skipped one matters.

A skipped line never damages anything: the value stays as it was.

### A mod can't be installed

The message names the problem. The common ones:

| Message | Cause |
|---|---|
| "it holds preview.png, and a mod package may hold only mod.json, .patch files and README.md" | The archive holds an extra file. Mod packages are text only |
| "It isn't a mod package: there is no mod.json at its top." | The archive has no `mod.json` at its top or in its one folder |
| "it holds a subfolder" | Packages hold only files |
| "the package is larger than 1 MiB" | Too big for a mod package |
| "... (listed in mod.json) isn't in it." | A patch file the manifest names is missing |
| "A folder named ... is in the mods folder already, and it isn't a mod package." | Something else uses that folder name. Rename or move it |

### Mods with the --mods option

Starting the game with `--mods DIR` reads mods from another folder for that run. Your list in
setup belongs to your usual folder, so in `DIR` only loose `.patch` files apply and packages
stay disabled.

### Playing without mods

Untick every mod and press **Save**, or point **Mods folder** at an empty folder.

Still stuck? See the [FAQ](FAQ), or [report a bug](Contributing#reporting-a-bug) and say which
mods were enabled.
