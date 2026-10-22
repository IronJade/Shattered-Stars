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

run() {  # run NAME FRAMES MAP INPUT
  SS_TRACE=1 SS_TRACE_LEADER=30 SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy \
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

exit $fail
