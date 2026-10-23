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
  shot quit-confirm --set "disc1=$d1" --state quit-confirm
  shot game-missing --set "disc1=$d1" --state game-missing
  shot discs-ok-sdl-renderer --renderer sdl --set "disc1=$d1" --set "disc2=$d2"
fi
shot discs-not-a-disc --set "disc1=$f${sep}notes.txt"
shot discs-missing-file --set "disc1=$f${sep}gone${sep}Disc 1.cue"
shot mods-missing --page mods --set "mods=$f${sep}no-mods-here"
echo "in $out"
