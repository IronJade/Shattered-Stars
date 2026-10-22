#!/bin/bash
# Headless smoke test of field events through the whole game loop: trigger regions,
# doors (map transfers), chests and talking. Each case starts a new game on a map
# with --map, plays a scripted input and checks the SS_TRACE / SS_TRACE_LEADER output.
#
#   tools/smoke_field_events.sh [path/to/shattered-stars]
#
# Needs the disc set up as for a normal run. Uses a throwaway memory card; never
# opens a window or plays audio.
set -u
BIN=${1:-engine/build/shattered-stars}
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
fail=0

run() {  # run NAME FRAMES MAP INPUT (no world edits unless SS_MODS_DIR is given)
  SS_MODS_DIR="${SS_MODS_DIR:-$WORK/no-mods}" SS_TRACE=1 SS_TRACE_LEADER=30 SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy \
    SS_MEMCARD1="$WORK/card.mcd" timeout 900 "$BIN" --headless --frames "$2" --map "$3" --input "$4" \
    >"$WORK/$1.log" 2>&1
}

check() {  # check NAME DESCRIPTION REGEX
  if grep -qE "$3" "$WORK/$1.log"; then
    echo "PASS $1: $2"
  else
    echo "FAIL $1: $2 (no line matching: $3)"
    fail=1
  fi
}

# Map 8 (the ruins): the exit strip's region fires and Claude says the door is shut.
ADV=$(for f in $(seq 480 45 4999); do printf '%d:cross,%d:none,' "$f" $((f + 5)); done)
run ruins_exit 5400 8 "${ADV}5000:down,5600:none"
check ruins_exit "trigger region fires" 'trigger 0 fires \(code word 5708\)'
check ruins_exit "its message opens" 'msg_window_open.* at vblank 51[2-9][0-9]'

# Map 18 (Arlia): up to the store front, left to its door (the door-open region fires),
# up through it: map 26 (Hearn's general store) loads with the leader at the script's spot.
run arlia_door 900 18 "200:up,439:none,450:left,663:none,670:up,1000:none"
check arlia_door "door-open region fires" 'trigger 0 fires \(code word 491\)'
check arlia_door "door region fires" 'trigger 4 fires \(code word 478\)'
check arlia_door "map 26 loads" 'field_load_map\(map=26'
check arlia_door "leader placed inside" '\[leader\] .* map 26 state 0 pos 5[0-9][0-9],0,-4[0-9][0-9]'

# Map 18: the middle crate in front of the store is a chest (B 0x6c) with 200 Fol;
# opening it twice pays once.
run arlia_chest 1300 18 "200:up,439:none,450:left,554:none,560:up,572:none,600:cross,606:none,760:cross,766:none,1100:cross,1106:none"
check arlia_chest "chest message" 'msg_window_open\(rect=2,30,37,1'
check arlia_chest "200 Fol added once" '\[leader\] vblank 1260 .* fol 200$'

# Map 18: the left crate holds Leather Armor; the post-battle path auto-equips it on
# Claude and shows the Equipment Wizard (field state 15) until confirmed.
run arlia_wizard 1300 18 "200:up,439:none,450:left,593:none,600:up,612:none,640:cross,646:none,800:cross,806:none,1000:cross,1006:none"
check arlia_wizard "Equipment Wizard opens" '\[leader\] vblank 9[0-9]0 map 18 state 15 '
check arlia_wizard "and closes" '\[leader\] vblank 12[0-9]0 map 18 state 0 .* control 0 '

# Map 26: talking to Mr. Hearn opens his window.
run hearn_talk 1700 18 "200:up,439:none,450:left,663:none,670:up,1000:none,1010:left,1060:none,1070:up,1250:none,1260:cross,1266:none"
check hearn_talk "talk opens a window" 'msg_window_open_default\(text=\+0xc8b'

# World edits (tools/so2_mod.py -> engine mods): the 200-Fol chest gives 5000 and the
# store door leads to map 33. Needs the disc image for the tool (SS_DISC1).
if [ -n "${SS_DISC1:-}" ] && [ -f "$SS_DISC1" ]; then
  mkdir -p "$WORK/mods"
  python3 tools/so2_mod.py chest "$SS_DISC1" 18 0x962 --fol 5000 -o "$WORK/mods/test.patch" >/dev/null
  python3 tools/so2_mod.py door "$SS_DISC1" 18 1 --to 33 --x -276 --z 241 -o "$WORK/mods/test.patch" >/dev/null
  SS_MODS_DIR="$WORK/mods" run mod_chest 1300 18 "200:up,439:none,450:left,554:none,560:up,572:none,600:cross,606:none,760:cross,766:none,1100:cross,1106:none"
  check mod_chest "patched chest pays 5000 Fol" '\[leader\] vblank 1260 .* fol 5000$'
  SS_MODS_DIR="$WORK/mods" run mod_door 900 18 "200:up,439:none,450:left,663:none,670:up,1000:none"
  check mod_door "patched door leads to map 33" 'field_load_map\(map=33'
  # Data overrides: Hearn's shop (shop 1) sells a Revival Card (item 0x15) first, at
  # 4321 Fol; a Landworm (enemy type 4, encounter 19) has 4321 HP.
  mkdir -p "$WORK/mods2"
  python3 tools/so2_mod.py shop "$SS_DISC1" 1 0 0x15 -o "$WORK/mods2/data.patch" >/dev/null
  python3 tools/so2_mod.py item "$SS_DISC1" 0x15 price 4321 -o "$WORK/mods2/data.patch" >/dev/null
  python3 tools/so2_mod.py enemy "$SS_DISC1" 4 hp 4321 -o "$WORK/mods2/data.patch" >/dev/null
  SS_MODS_DIR="$WORK/mods2" run mod_shop 1710 18 "200:up,439:none,450:left,663:none,670:up,1000:none,1010:left,1060:none,1070:up,1250:none,1260:cross,1266:none,1400:cross,1406:none,1500:cross,1506:none,1600:cross,1606:none"
  check mod_shop "shop stock patched" 'data.patch:[0-9]+: shop 1 \+0 0x2d8 -> 0x15'
  check mod_shop "item price patched" 'data.patch:[0-9]+: item 21 \+0 0xc8 -> 0x10e1'
  SS_MODS_DIR="$WORK/mods2" SS_ENEMY_TRACE=1 SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy \
    SS_MEMCARD1="$WORK/card.mcd" timeout 900 "$BIN" --headless --frames 60 --battle 19 >"$WORK/mod_enemy.log" 2>&1
  check mod_enemy "enemy stats patched" '\[enemy\] vblank 0 .* t4 .* hp4321 '
else
  echo "SKIP mods: set SS_DISC1 to the disc 1 .cue"
fi

# The debug console (--debug): commands from the terminal run in the field, and their
# output goes to the log (and the in-window overlay).
SS_DEBUG_INPUT="300:fol 1234;310:flag 5" SS_MODS_DIR="$WORK/no-mods" SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy \
  SS_MEMCARD1="$WORK/card.mcd" timeout 900 "$BIN" --headless --debug --frames 400 --map 18 </dev/null >"$WORK/debug_console.log" 2>&1
check debug_console "fol command" '\[debug\] Fol 1234'
check debug_console "flag command" '\[debug\] flag 5 = 0'

# World map 2 -> North City (map 260), from the user's own save if it is present: Continue
# with a throwaway copy, start in the town's entrance region, the transfer runs.
SAVE=analysis/saves/dex_north_city.mcd
if [ -f "$SAVE" ]; then
  cp "$SAVE" "$WORK/world.mcd"
  # The save is on disc 2: use the disc 2 image next to disc 1 if there is one (this run
  # only); otherwise Select at the insert-disc screen goes on with disc 1's archive.
  D2="${SS_DISC2:-${SS_DISC1:+${SS_DISC1//Disc 1/Disc 2}}}"
  NC_INPUT="0:none"
  if [ -n "$D2" ] && [ -f "$D2" ]; then export SS_DISC2="$D2"; else NC_INPUT="30:select,36:none,90:select,96:none,150:select,156:none,210:select,216:none"; fi
  SS_MODS_DIR="$WORK/no-mods" SS_LOAD_SAVE=0 SS_TRACE=1 SS_TRACE_LEADER=60 SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy \
    SS_MEMCARD1="$WORK/world.mcd" timeout 900 "$BIN" --headless --frames 400 --map-pos 57219,-300,28622 --input "$NC_INPUT" \
    >"$WORK/north_city.log" 2>&1
  check north_city "world trigger fires" 'trigger 17 fires \(code word 4267\)'
  check north_city "North City loads" 'field_load_map\(map=260'
  check north_city "leader in town" '\[leader\] vblank 1[0-9]0 map 260 state 0 '
else
  echo "SKIP north_city: $SAVE not present"
fi

exit $fail
