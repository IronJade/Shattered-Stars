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

# Archipelago (game/ap, docs/ap/seed_format.md) [world agent]: a one-chest seed. Its patch
# line makes the 200-Fol crate pay 5000 and opening the crate records the check (its opened
# flag); an item received through the console is given in free play with a banner.
cat >"$WORK/tiny.apso2" <<'SEED'
{"format": "apso2", "version": 1, "seed": "smoke-1", "slot": {"name": "Claude", "id": 1},
 "items": {"5000021": {"name": "Revival Card", "kind": "item", "game_id": 21}},
 "locations": [{"id": 1347569665, "name": "Arlia: crate by the store",
                "check": {"type": "chest", "map": 18, "flag": "0x962"},
                "item": {"player": 1, "kind": "fol", "amount": 5000, "name": "5000 Fol"}}],
 "patches": ["script 18 0x82c 0xffffff38 0xffffec78"]}
SEED
SS_DEBUG_INPUT="1150:ap_give 5000021 Alice;1290:ap_status" SS_MODS_DIR="$WORK/no-mods" SS_TRACE_LEADER=30 \
  SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy SS_MEMCARD1="$WORK/card.mcd" timeout 900 "$BIN" --headless --debug \
  --ap-seed "$WORK/tiny.apso2" --frames 1300 --map 18 \
  --input "200:up,439:none,450:left,554:none,560:up,572:none,600:cross,606:none,760:cross,766:none,1100:cross,1106:none" \
  </dev/null >"$WORK/ap_seed.log" 2>&1
check ap_seed "seed loads" '\[ap\] seed smoke-1 slot Claude: 1 locations'
check ap_seed "the seed's chest pays 5000 Fol" '\[leader\] vblank 1260 .* fol 5000$'
check ap_seed "chest check recorded" '\[ap\] check 1347569665 \(Arlia: crate by the store\): 5000 Fol'
check ap_seed "received item given" '\[ap\] given Revival Card$'
check ap_seed "status" '\[debug\] seed smoke-1 \| slot Claude \| checks 1/1 \| received 0 \| waiting 0 \| offline'
# Phase 2 [world agent]: the crate's patch puts placeholder 0x400 where its 200 Fol was; the
# port sends Alice's Hookshot instead (no Fol; the chest's window says "Sent Hookshot to
# Alice."). A boss location and the goal on encounter 1 (--battle-end: a victory); a
# DeathLink (console ap_deathlink) KOs the party in a battle and is a game over in the field.
cat >"$WORK/ph.apso2" <<'SEED'
{"format": "apso2", "version": 1, "seed": "smoke-2", "slot": {"name": "Claude", "id": 1},
 "options": {"deathlink": true}, "goal": {"encounters": [1]},
 "items": {"5000021": {"name": "Revival Card", "kind": "item", "game_id": 21}},
 "locations": [{"id": 1347569665, "name": "Arlia: crate by the store", "placeholder": 1024,
                "check": {"type": "chest", "map": 18, "flag": "0x962"},
                "item": {"player": 2, "player_name": "Alice", "name": "Hookshot"}},
               {"id": 900001, "name": "Lizardaxes", "check": {"type": "boss", "encounter": 1},
                "item": {"player": 1, "kind": "fol", "amount": 300, "name": "300 Fol"}}],
 "patches": ["script 18 0x82c 0xffffff38 0x00000400"]}
SEED
AP_RUN=(env SS_MODS_DIR="$WORK/no-mods" SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy SS_MEMCARD1="$WORK/card.mcd")
"${AP_RUN[@]}" SS_TRACE_LEADER=30 timeout 900 "$BIN" --headless --ap-seed "$WORK/ph.apso2" --frames 1300 --map 18 \
  --input "200:up,439:none,450:left,554:none,560:up,572:none,600:cross,606:none,760:cross,766:none,1100:cross,1106:none" \
  </dev/null >"$WORK/ap_placeholder.log" 2>&1
check ap_placeholder "the crate sends Alice's item" '\[ap\] check 1347569665 \(Arlia: crate by the store\): Hookshot for Alice'
check ap_placeholder "and gives no Fol" '\[leader\] vblank 1260 .* fol 0$'
"${AP_RUN[@]}" timeout 900 "$BIN" --headless --ap-seed "$WORK/ph.apso2" --frames 1800 --battle 1 --battle-end 400:0 \
  </dev/null >"$WORK/ap_boss.log" 2>&1
check ap_boss "boss check" '\[ap\] check 900001 \(Lizardaxes\): 300 Fol'
check ap_boss "goal" '\[ap\] goal: encounter 1 won'
check ap_boss "the boss's own item given" '\[ap\] given 300 Fol'
"${AP_RUN[@]}" SS_DEBUG_INPUT="300:ap_deathlink Bob" timeout 900 "$BIN" --headless --debug --ap-seed "$WORK/ph.apso2" \
  --frames 1000 --battle 1 </dev/null >"$WORK/ap_deathlink_battle.log" 2>&1
check ap_deathlink_battle "the party falls" '\[ap\] DeathLink from Bob: the party falls in battle'
check ap_deathlink_battle "the battle is lost" '\[battle\] exit: encounter 1 outcome 2'
"${AP_RUN[@]}" SS_TRACE_LEADER=30 SS_DEBUG_INPUT="300:ap_deathlink Bob" timeout 900 "$BIN" --headless --debug \
  --ap-seed "$WORK/ph.apso2" --frames 400 --map 18 </dev/null >"$WORK/ap_deathlink_field.log" 2>&1
check ap_deathlink_field "game over in the field" '\[leader\] vblank 3[3-9]0 map 18 state 7 '

# Saving (camp menu, Data > Save > slot 1 > #1 > Yes; SS_SAVE_ANYWHERE) writes the sidecar
# card.slot01.ap.json; the card itself is byte for byte the one saved without a seed.
SAVE_INPUT="300:triangle,306:none,400:up,406:none,450:cross,456:none,550:cross,556:none,650:cross,656:none,750:cross,756:none,850:cross,856:none"
for v in ap plain; do
  [ $v = ap ] && AP_ARGS=(--ap-seed "$WORK/tiny.apso2") || AP_ARGS=()
  SS_DEBUG_INPUT="200:ap_check 1347569665" SS_SAVE_ANYWHERE=1 SS_MODS_DIR="$WORK/no-mods" SDL_VIDEO_DRIVER=offscreen \
    SDL_AUDIO_DRIVER=dummy SS_MEMCARD1="$WORK/save_$v.mcd" timeout 900 "$BIN" --headless --debug "${AP_ARGS[@]}" \
    --frames 1100 --map 18 --input "$SAVE_INPUT" </dev/null >"$WORK/ap_save_$v.log" 2>&1
done
check ap_save_ap "a save writes the sidecar" '\[ap\] saved .*save_ap\.slot01\.ap\.json'
if [ -f "$WORK/save_ap.slot01.ap.json" ] && grep -q 1347569665 "$WORK/save_ap.slot01.ap.json" && cmp -s "$WORK/save_ap.mcd" "$WORK/save_plain.mcd" \
   && [ ! -e "$WORK/save_plain.slot01.ap.json" ]; then
  echo "PASS ap_save: the sidecar holds the check; the card equals one saved without a seed"
else
  echo "FAIL ap_save: sidecar or card differs"
  fail=1
fi

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
  # Archipelago [world agent]: loading the save reads its sidecar (save 1: world.slot01.ap.json).
  cp "$SAVE" "$WORK/world.mcd"
  printf '{"format": "apso2-save", "version": 1, "seed": "smoke-1", "slot_name": "Claude", "received_index": 3, "start_given": true, "checked": [1347569665]}\n' \
    >"$WORK/world.slot01.ap.json"
  SS_MODS_DIR="$WORK/no-mods" SS_LOAD_SAVE=0 SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy \
    SS_MEMCARD1="$WORK/world.mcd" timeout 900 "$BIN" --headless --ap-seed "$WORK/tiny.apso2" --frames 60 \
    </dev/null >"$WORK/ap_load.log" 2>&1
  check ap_load "a load reads the save's sidecar" '\[ap\] loaded .*world\.slot01\.ap\.json: 1 checks, received index 3'
else
  echo "SKIP north_city: $SAVE not present"
fi

exit $fail
