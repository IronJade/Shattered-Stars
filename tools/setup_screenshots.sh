#!/bin/bash
# Renders the setup program's pages and states offscreen as PNGs, to check its layout
# without opening a window. Uses a scratch settings folder under OUT_DIR, never the
# player's. SS_DISC1 (and SS_DISC2, else "Disc 2" next to it) fill the disc states.
#
#   tools/setup_screenshots.sh [SETUP_PROGRAM] [OUT_DIR]
#   (defaults: engine/build-setup/shattered-stars-setup, scratchpad/setup/shots)
#
# For the Windows build under Wine: SETUP_PROGRAM=engine/build-win/setup.exe (the script
# runs it with wine and WINEPREFIX=scratchpad/wineprefix; settings in the prefix's APPDATA).
set -u
cd "$(dirname "$0")/.."
bin=${1:-engine/build-setup/shattered-stars-setup}
out=${2:-scratchpad/setup/shots}
mkdir -p "$out"
out=$(cd "$out" && pwd)
export SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy
cfg="$out/settings"
rm -rf "$cfg" "$out/files"
mkdir -p "$cfg" "$out/files/mods"

run=("$bin")
if [[ "$bin" == *.exe ]]; then
  # Wine: the settings go to %APPDATA%\ShatteredStars. Wine sets APPDATA to the prefix's
  # own profile folder whatever the Unix variable says, so that is where they land (inside
  # the scratch prefix); it is emptied first. XDG_CONFIG_HOME is dropped (Wine passes Unix
  # variables through, and the program would prefer it).
  export WINEPREFIX=${WINEPREFIX:-$PWD/scratchpad/wineprefix} WINEDEBUG=-all
  unset XDG_CONFIG_HOME
  case "$WINEPREFIX" in
    "$PWD"/scratchpad/*) rm -rf "$WINEPREFIX"/drive_c/users/*/AppData/Roaming/ShatteredStars ;;
    *) echo "WINEPREFIX must be under scratchpad/ (it holds the settings)" >&2; exit 1 ;;
  esac
  run=(env -u HOME wine "$bin")
  winpath() { winepath -w "$1" 2>/dev/null; }
else
  export XDG_CONFIG_HOME="$cfg"
  winpath() { echo "$1"; }
fi

d1=${SS_DISC1:-}
d2=${SS_DISC2:-}
if [ -z "$d2" ] && [ -n "$d1" ] && [ -f "${d1//Disc 1/Disc 2}" ]; then d2=${d1//Disc 1/Disc 2}; fi
[ -n "$d1" ] && d1=$(winpath "$d1")
[ -n "$d2" ] && d2=$(winpath "$d2")

# Test files: a formatted card with one Star Ocean save, an unformatted one, a not-a-disc
# file and two patch files.
python3 - "$out/files" <<'EOF'
import sys, struct
d = sys.argv[1]
card = bytearray(128 * 1024)
def frame(n): return n * 128
def checksum(n):
    x = 0
    for b in card[frame(n):frame(n) + 127]: x ^= b
    card[frame(n) + 127] = x
card[0:2] = b"MC"; checksum(0)
for b in range(1, 16):
    o = frame(b)
    card[o:o + 4] = struct.pack("<I", 0xA0); card[o + 8:o + 10] = b"\xff\xff"; checksum(b)
o = frame(1)
card[o:o + 4] = struct.pack("<I", 0x51); card[o + 4:o + 8] = struct.pack("<I", 0x2000)
card[o + 10:o + 10 + 19] = b"BASCUS-94421S02-S01"; checksum(1)
for n in range(16, 36):
    card[frame(n):frame(n) + 4] = b"\xff\xff\xff\xff"; card[frame(n) + 8:frame(n) + 10] = b"\xff\xff"; checksum(n)
open(f"{d}/saves.mcd", "wb").write(card)
open(f"{d}/blank.mcd", "wb").write(bytes(128 * 1024))
open(f"{d}/notes.txt", "w").write("not a disc\n")
open(f"{d}/mods/10-cheaper-potions.patch", "w").write("# example\n")
open(f"{d}/mods/20-tougher-bosses.patch", "w").write("# example\n")
import json
open(f"{d}/example.apso2", "w").write(json.dumps({
    "format": "apso2", "version": 1, "seed": "54321012345", "game": "Star Ocean: The Second Story",
    "slot": {"name": "Claude", "id": 1}, "options": {}, "items": {},
    "locations": [{"id": 0x50320000 + i, "name": f"Chest {i}", "check": {"type": "chest", "map": 18, "flag": 0x900 + i},
                   "item": {"player": 2, "player_name": "Rena", "name": "Hookshot", "ap_id": 77}} for i in range(12)],
    "patches": [], "start": {"items": [], "fol": 0, "flags": []}}))
EOF
f=$(winpath "$out/files")
sep=/; [[ "$bin" == *.exe ]] && sep='\'

shot() {  # NAME ARGS...: one screenshot
  local name=$1
  shift
  if "${run[@]}" --screenshot "$(winpath "$out/$name.png")" "$@" > "$out/$name.log" 2>&1; then
    echo "$name.png"
  else
    echo "$name: FAILED (see $name.log)"
  fi
}

shot discs-empty
shot discs-from-game --from-game
if [ -n "$d1" ]; then
  shot discs-ok --set "disc1=$d1" --set "disc2=$d2"
  shot discs-live-swap --do "set:disc1=$d1"  # no disc at start, then one: the logo appears
  shot discs-disc2-missing --set "disc1=$d1"
  [ -n "$d2" ] && shot discs-wrong-disc --set "disc1=$d2"
  shot display --page display --set "disc1=$d1"
  shot display-fullscreen --page display --set "disc1=$d1" --set window=fullscreen
  shot display-borderless --page display --set "disc1=$d1" --set window=borderless
  shot sound --page sound --set "disc1=$d1" --set volume=65
  shot controls --page controls --set "disc1=$d1"
  shot controls-duplicate --page controls --set "disc1=$d1" --set key_circle=Z --set pad_l2=a
  shot controls-capture --page controls --set "disc1=$d1" --state capture-key
  shot controls-custom --page controls --set "disc1=$d1" --set key_cross=Space --set pad_r2=none \
    --set stick_deadzone=40 --set stick_dpad=0
  shot memcards --page memcards --set "disc1=$d1" --set "memcard1=$f${sep}saves.mcd" --set "memcard2=$f${sep}blank.mcd"
  shot memcards-new --page memcards --set "disc1=$d1"
  shot memcards-clash --page memcards --set "disc1=$d1" --set "memcard1=$f${sep}saves.mcd" --set "memcard2=$f${sep}saves.mcd"
  shot mods --page mods --set "disc1=$d1" --set "mods=$f${sep}mods"
  shot archipelago --page archipelago --set "disc1=$d1"
  shot archipelago-seed --page archipelago --set "disc1=$d1" --set "ap_seed=$f${sep}example.apso2" \
    --set ap_server=localhost:38281 --set ap_password=secret
  shot archipelago-bad-seed --page archipelago --set "disc1=$d1" --set "ap_seed=$f${sep}notes.txt"
  shot quit-confirm --set "disc1=$d1" --state quit-confirm
  shot game-missing --set "disc1=$d1" --state game-missing
  shot discs-ok-sdl-renderer --renderer sdl --set "disc1=$d1" --set "disc2=$d2"
fi
shot discs-not-a-disc --set "disc1=$f${sep}notes.txt"
shot discs-missing-file --set "disc1=$f${sep}gone${sep}Disc 1.cue"
shot mods-missing --page mods --set "mods=$f${sep}no-mods-here"

# The Mods page as a manager (docs/notes/mods.md), in its own scratch settings folder with
# the list saved in config.ini (so no unsaved-changes notice): the repository's sample mods,
# a loose patch, a mod that conflicts with a sample, one that needs a newer port, and .zip
# packages to install (a new one, a newer version of a sample, and one that isn't allowed).
if [[ "$bin" != *.exe ]] && [ -n "$d1" ]; then
  mods_cfg="$out/settings-mods"
  pkgs="$out/files/mods-packages"
  rm -rf "$mods_cfg" && mkdir -p "$mods_cfg/shattered-stars" "$pkgs"
  cp -r mods/arlia-crate-fol mods/hearn-store-plus "$pkgs/"
  python3 - "$out/files" <<'EOF'
import sys, os, json, zipfile
d = sys.argv[1]
p = f"{d}/mods-packages"
open(f"{p}/cheaper-bottles.patch", "w").write("# Resurrection Bottles at 2400 Fol\nitem 0x115 price 3600 2400\n")
def package(folder, manifest, files):
    os.makedirs(folder, exist_ok=True)
    json.dump(manifest, open(f"{folder}/mod.json", "w"), indent=2)
    for name, text in files.items():
        open(f"{folder}/{name}", "w").write(text)
base = {"format": 1, "version": "0.3", "author": "A. Modder", "min_port_version": "0.01.00"}
package(f"{p}/crate-jackpot", dict(base, id="crate-jackpot", name="Crate jackpot", tags=["chests"],
        description="The crate in front of Hearn's store holds 10000 Fol.", patches=["jackpot.patch"]),
        {"jackpot.patch": "script 18 0x82c 0xffffff38 0xffffd8f0\nscript 21 0x297 0xffffff38 0xffffd8f0\n"})
package(f"{p}/tomorrow", dict(base, id="tomorrow", name="Tomorrow's mod", min_port_version="9.0",
        description="Made for a later version of the port.", patches=["later.patch"]),
        {"later.patch": "item 0x115 sell 25 30\n"})
def zip_of(path, root, manifest, files):
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr(f"{root}mod.json", json.dumps(manifest, indent=2))
        for name, text in files.items():
            z.writestr(f"{root}{name}", text)
zip_of(f"{d}/tougher-landworms.zip", "tougher-landworms/", dict(base, id="tougher-landworms", name="Tougher Landworms",
       version="1.0.0", tags=["enemies", "difficulty"], description="Landworms have twice their HP.",
       patches=["landworm.patch"]), {"landworm.patch": "enemy 4 hp 0xc8 0x190\n", "README.md": "Twice the HP.\n"})
m = json.load(open(f"{p}/hearn-store-plus/mod.json")); m["version"] = "1.1.0"
zip_of(f"{d}/hearn-store-plus-1.1.0.zip", "", m, {"stock.patch": open(f"{p}/hearn-store-plus/stock.patch").read()})
zip_of(f"{d}/with-picture.zip", "", dict(base, id="with-picture", name="With a picture", description="x",
       patches=["a.patch"]), {"a.patch": "item 1 price 1 2\n", "preview.png": "not really a picture"})
EOF
  printf 'disc1=%s\ndisc2=%s\nmods=%s\nmod_order=(loose),arlia-crate-fol,hearn-store-plus,crate-jackpot,-tomorrow\n' \
    "$d1" "$d2" "$pkgs" > "$mods_cfg/shattered-stars/config.ini"
  mshot() { XDG_CONFIG_HOME="$mods_cfg" shot "$@"; }
  mshot mods-manager --page mods --do mods-select:arlia-crate-fol
  mshot mods-conflict --page mods --do mods-select:crate-jackpot,mods-show-info
  mshot mods-readme --page mods --do mods-select:hearn-store-plus,mods-show-info
  mshot mods-cannot-load --page mods --do mods-select:tomorrow,mods-show-info
  mshot mods-loose --page mods --do mods-select:"(loose)",mods-show-info
  mshot mods-reordered --page mods --do mods-select:crate-jackpot,mods-up,mods-up,mods-toggle:hearn-store-plus
  mshot mods-remove --page mods --do mods-select:crate-jackpot,mods-remove
  mshot mods-install-refused --page mods --do "mods-install:$f/with-picture.zip"
  mshot mods-replace --page mods --do "mods-install:$f/hearn-store-plus-1.1.0.zip"
  mshot mods-installed --page mods --do "mods-install:$f/tougher-landworms.zip"
fi
echo "in $out"
