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
# Every game run under a memory cap (docs/HANDOFF.md machine budget; tools/memcap.py):
# SS_MEM_CAP_GB (default 4) GB of address space each. An ASan build can't take one.
if [ "${SS_MEM_CAP_GB:-4}" != 0 ] && ! grep -q __asan_init "$BIN" 2>/dev/null; then
  ulimit -v $(( ${SS_MEM_CAP_GB:-4} * 1024 * 1024 ))
fi
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
# [cleanup] Its reader must not hold stdin at exit: with stdin a pipe that stays open (a line,
# then nothing), the line runs and the game still exits at its last frame (it used to hang in
# exit(), up to this timeout).
mkfifo "$WORK/debug_stdin"
{ printf 'help\n'; exec sleep 300; } >"$WORK/debug_stdin" &
debug_feeder=$!
SS_MODS_DIR="$WORK/no-mods" SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy SS_MEMCARD1="$WORK/card.mcd" \
  timeout 60 "$BIN" --headless --debug --frames 120 --map 18 <"$WORK/debug_stdin" >"$WORK/debug_exit.log" 2>&1
echo "exit status $?" >>"$WORK/debug_exit.log"
kill "$debug_feeder" 2>/dev/null
check debug_exit "a line from a pipe runs" '\[debug\] commands \(numbers'
check debug_exit "exits with stdin a pipe kept open" '^exit status 0$'

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

# Phase 3 [world agent] (docs/ap/seed_format.md "Phase 3"). Character items: a received
# character joins in free play; Ashton's choice site (map 71, the call at 0x7348) is a check
# and does not add him unreceived (event sweep, entry 2). Traps: Fol loss and poison. The
# result screen: first-drop check (Berle, odds 255) and the multipliers. DeathLink: in the
# camp menu it waits for the menu to close.
cat >"$WORK/p3.apso2" <<'SEED'
{"format": "apso2", "version": 1, "seed": "smoke-3", "slot": {"name": "Claude", "id": 1},
 "options": {"character_items": true, "enemy_dropsanity": "first_drop", "exp_multiplier": 3, "fol_multiplier": 2,
             "deathlink": true},
 "items": {"5100003": {"name": "Celine", "kind": "character", "character": 3}},
 "locations": [{"id": 1353915392, "name": "Ashton joins", "check": {"type": "join", "map": 71, "offset": "0x7348", "character": "Ashton"},
                "item": {"player": 2, "player_name": "Alice", "name": "Hookshot"}},
               {"id": 1362100421, "name": "Berle's first drop", "check": {"type": "drop", "enemy": 197},
                "item": {"player": 2, "player_name": "Bob", "name": "Map Width"}}]}
SEED
"${AP_RUN[@]}" SS_TRACE_LEADER=30 SS_DEBUG_INPUT="200:ap_give Celine Bob;300:fol 1000;320:ap_give trap:fol_loss:25 Bob;500:ap_give trap:status Bob;700:ap_status" \
  timeout 900 "$BIN" --headless --debug --ap-seed "$WORK/p3.apso2" --frames 900 --map 18 </dev/null >"$WORK/ap_p3_items.log" 2>&1
check ap_p3_items "a received character joins in free play" '\[ap\] Celine joins the party \(slot 1\)'
check ap_p3_items "the Fol trap takes 25%" '\[ap\] trap fol_loss: lost 250 Fol'
check ap_p3_items "the status trap poisons the party" '\[ap\] trap status: the party is poisoned'
check ap_p3_items "the status line" '\[debug\]   received characters: Celine$'
"${AP_RUN[@]}" SS_SCRIPT_GLOBALS="a0=17,1bc=100" SS_SWEEP_FLAGS=113 SS_EVENT_SWEEP=1 SS_EVENT_SWEEP_SKIP=2 \
  timeout 900 "$BIN" --headless --debug --ap-seed "$WORK/p3.apso2" --frames 10400 --map 71 </dev/null >"$WORK/ap_p3_join.log" 2>&1
check ap_p3_join "the join site is a check" '\[ap\] check 1353915392 \(Ashton joins\): Hookshot for Alice'
check ap_p3_join "and Ashton stays out" '\[ap\] join site 71:29512: Ashton has not been received: not added'
check ap_p3_join "the scene plays on" '\[sweep\] map 71 vblank [0-9]+ entry 2 T2 end ok'
MASH=$(for f in $(seq 200 20 2400); do printf '%d:cross,%d:none,' "$f" $((f + 4)); done)
"${AP_RUN[@]}" timeout 900 "$BIN" --headless --ap-seed "$WORK/p3.apso2" --battle 535 --battle-end 60:0 --frames 2400 \
  --input "${MASH%,}" </dev/null >"$WORK/ap_p3_drop.log" 2>&1
check ap_p3_drop "exp x3, fol x2" '\[ap\] battle rewards: exp 99000 -> 297000, fol 58000 -> 116000'
check ap_p3_drop "the first drop is a check" '\[ap\] check 1362100421 \(Berle.s first drop\): Map Width for Bob'
check ap_p3_drop "instead of the item" '\[ap\] first drop of enemy type 197 \(item 240\): the check instead'
"${AP_RUN[@]}" SS_TRACE_LEADER=20 SS_DEBUG_INPUT="400:ap_deathlink Bob" timeout 900 "$BIN" --headless --debug \
  --ap-seed "$WORK/p3.apso2" --frames 800 --map 18 --input "300:triangle,306:none,700:circle,706:none" \
  </dev/null >"$WORK/ap_p3_dl_menu.log" 2>&1
check ap_p3_dl_menu "a DeathLink in the camp menu waits for it to close" '\[ap\] DeathLink from Bob: the party falls \(vblank 7[0-9][0-9],'
check ap_p3_dl_menu "then the game over" '\[leader\] vblank 7[4-9]0 map 18 state 7 '
# The seed's story_route: New Game skips the hero select and the naming screen (that hero, the
# default name) [charselect agent]; under "any" both screens show (Right picks Rena).
NEWGAME_A="850:start,854:none,951:cross,957:none,1251:cross,1257:none,1551:cross,1557:none,1851:cross,1857:none"
NEWGAME_B="2151:start,2157:none,2451:cross,2457:none,2700:start,2706:none,3000:start,3006:none,3300:start,3306:none"
for hero in claude rena any; do
  printf '{"format": "apso2", "version": 1, "seed": "smoke-hero", "slot": {"name": "P", "id": 1}, "options": {"story_route": "%s"}, "items": {}, "locations": []}\n' \
    $hero >"$WORK/hero_$hero.apso2"
  [ $hero = any ] && PRESS="2000:right,2006:none," || PRESS=
  "${AP_RUN[@]}" SS_TRACE=1 SS_TRACE_LEADER=600 timeout 900 "$BIN" --headless --no-memcard --ap-seed "$WORK/hero_$hero.apso2" \
    --frames 3700 --input "$NEWGAME_A,$PRESS$NEWGAME_B" </dev/null >"$WORK/ap_hero_$hero.log" 2>&1
done
check ap_hero_claude "Claude's seed skips the hero select and the naming screen" '\[ap\] story route Claude \(the seed.s\): New Game skips the hero select and the naming screen \(Claude\)'
check ap_hero_claude "and starts Claude's game (map 7)" '\[leader\] vblank 3600 map 7 state 0 '
check ap_hero_rena "Rena's seed skips the hero select and the naming screen" '\[ap\] story route Rena \(the seed.s\): New Game skips the hero select and the naming screen \(Rena\)'
check ap_hero_rena "and starts Rena's game (map 18)" '\[leader\] vblank 3600 map 18 state 0 '
check ap_hero_any "story_route any: the hero select shows" '\[trace\] 0x80047690 at vblank'
check ap_hero_any "and the naming screen" '\[trace\] 0x800478e8 at vblank'
check ap_hero_any "and Right picks Rena's game (map 18)" '\[leader\] vblank 3600 map 18 state 0 '
for hero in claude rena; do
  if grep -q '\[trace\] 0x80047690\|\[trace\] 0x800478e8' "$WORK/ap_hero_$hero.log"; then
    echo "FAIL ap_hero_$hero: the hero select or the naming screen showed"; fail=1
  fi
done
# The character select [charselect agent]: an open world seed with starting_character "any"
# (Bowman and Opera excluded) picks a member at New Game, after the story route's skip: Opera is
# refused, Celine picked, and she joins at the first free play and leads; a named member (Ashton)
# is shown alone to confirm; "hero" shows nothing (the combat mode right after the skip). The
# back paths: Circle on the select goes back to Vibration, Circle on the combat mode back to the
# select (its cursor on the pick). NEWGAME_B 300 vblanks later (the select's presses between).
NEWGAME_B3=$(echo "$NEWGAME_B" | tr ',' '\n' | awk -F: '{printf "%s%d:%s", (NR > 1 ? "," : ""), $1 + 300, $2}')
for start in any Ashton hero; do
  printf '{"format": "apso2", "version": 2, "seed": "smoke-cs", "slot": {"name": "P", "id": 1}, "options": {"story_route": "claude", "world_mode": "open_world", "party_rules": "story", "starting_character": "%s", "exclude_characters": ["Bowman", "Opera"]}, "items": {}, "locations": []}\n' \
    $start >"$WORK/cs_$start.apso2"
done
CS_PICK="1960:down,1966:none,1990:down,1996:none,2020:cross,2026:none,2050:up,2056:none,2080:up,2086:none,2110:right,2116:none,2140:right,2146:none,2170:cross,2176:none"
CS_BACK="1960:circle,1966:none,2100:cross,2106:none,2200:right,2206:none,2230:right,2236:none,2260:cross,2266:none,2350:circle,2356:none,2450:cross,2456:none"
NEWGAME_B4=$(echo "$NEWGAME_B" | tr ',' '\n' | awk -F: '{printf "%s%d:%s", (NR > 1 ? "," : ""), $1 + 400, $2}')
cs_run() {  # cs_run NAME SEED FRAMES INPUT
  "${AP_RUN[@]}" SS_TRACE=1 SS_TRACE_LEADER=600 timeout 900 "$BIN" --headless --no-memcard --ap-seed "$WORK/$2.apso2" \
    --frames "$3" --input "$4" </dev/null >"$WORK/$1.log" 2>&1
}
cs_run ap_cs_any cs_any 4300 "$NEWGAME_A,$CS_PICK,$NEWGAME_B3"
cs_run ap_cs_named cs_Ashton 4300 "$NEWGAME_A,2170:cross,2176:none,$NEWGAME_B3"
cs_run ap_cs_hero cs_hero 3700 "$NEWGAME_A,$NEWGAME_B"
cs_run ap_cs_back cs_any 4300 "$NEWGAME_A,$CS_BACK,$NEWGAME_B4"
check ap_cs_any "the select opens after the story route's skip" '\[ap\] character select opens: 10 of 12 to pick'
check ap_cs_any "an excluded member is refused" '\[ap\] character select: this seed.s party has no place for that member'
check ap_cs_any "Celine picked" '\[ap\] character select: Celine Jules$'
check ap_cs_any "she starts the game" '\[ap\] starting character Celine: picked at New Game'
check ap_cs_any "joins at the first free play" '\[ap\] starting character Celine: joins at the first free play and leads'
check ap_cs_any "and leads" '\[ap\] Celine leads \(Formation leader, slot 1\)'
check ap_cs_any "Claude's game (map 7)" '\[leader\] vblank 4200 map 7 state 0 '
check ap_cs_named "a named member: the select to confirm" '\[ap\] character select opens: Ashton Anchors to confirm'
check ap_cs_named "Cross confirms" '\[ap\] character select: Ashton Anchors$'
check ap_cs_named "Ashton starts the game" '\[ap\] starting character Ashton: joins at the first free play and leads'
check ap_cs_named "Claude's game (map 7)" '\[leader\] vblank 4200 map 7 state 0 '
check ap_cs_hero "starting_character hero: the combat mode right after the skip" '\[trace\] 0x800489dc at vblank 1[0-9]{3}$'
check ap_cs_hero "Claude's game (map 7)" '\[leader\] vblank 3600 map 7 state 0 '
check ap_cs_back "Circle on the select: back to Vibration" '\[ap\] character select: Claude Kenni \(backed out\)'
check ap_cs_back "Celine picked" '\[ap\] character select: Celine Jules$'
check ap_cs_back "Circle on the combat mode: back to the select, Celine again" '\[trace\] 0x800489dc at vblank 2[0-9]{3}$'
check ap_cs_back "she starts the game" '\[ap\] starting character Celine: joins at the first free play and leads'
check ap_cs_back "Claude's game (map 7)" '\[leader\] vblank 4200 map 7 state 0 '
for name in ap_cs_hero ap_hero_claude ap_hero_rena ap_hero_any; do
  if [ -f "$WORK/$name.log" ] && grep -q '\[ap\] character select' "$WORK/$name.log"; then
    echo "FAIL $name: the character select showed"; fail=1
  fi
done
if [ "$(grep -c '\[ap\] character select: Celine Jules$' "$WORK/ap_cs_back.log")" != 2 ] ||
   [ "$(grep -c '\[ap\] story route Claude' "$WORK/ap_cs_back.log")" != 2 ]; then
  echo "FAIL ap_cs_back: two skips and two picks of Celine expected"; fail=1
fi
# The difficulty [difficulty agent]: options.difficulty earth / galaxy / universe sets the Battle
# Rank (config +0x4f) after the combat mode and skips its screen (window 0x80048cec); "any" offers
# it (Right Right Cross: Universe); no seed: the original (no screen without the Voice Collection's
# unlock). Then encounter 1 from the debug console: its two monsters' HP 60 x1 / x1.5 / x2.
for d in earth galaxy universe any; do
  printf '{"format": "apso2", "version": 2, "seed": "smoke-diff", "slot": {"name": "P", "id": 1}, "options": {"story_route": "claude", "difficulty": "%s"}, "items": {}, "locations": []}\n' \
    $d >"$WORK/diff_$d.apso2"
done
diff_run() {  # diff_run NAME SEEDARGS INPUT BATTLE_AT
  "${AP_RUN[@]}" SS_TRACE=1 SS_TRACE_LEADER=600 SS_BATTLE_UNITS=0 SS_DEBUG_INPUT="$4:battle 1" timeout 900 "$BIN" --headless \
    --debug --no-memcard $2 --frames $(($4 + 300)) --input "$3" </dev/null >"$WORK/$1.log" 2>&1
}
for d in earth galaxy universe; do diff_run ap_diff_$d "--ap-seed $WORK/diff_$d.apso2" "$NEWGAME_A,$NEWGAME_B" 3700; done
diff_run ap_diff_any "--ap-seed $WORK/diff_any.apso2" \
  "$NEWGAME_A,2151:start,2157:none,2250:right,2256:none,2280:right,2286:none,2310:cross,2316:none,${NEWGAME_B#2151:start,2157:none,}" 3900
diff_run ap_diff_none "" "$NEWGAME_A,$NEWGAME_B" 3700
check ap_diff_earth "Earth set, the screen skipped" '\[ap\] difficulty Earth \(the seed.s\): New Game skips the Battle Rank screen'
check ap_diff_earth "the monsters as the original" '\[bunits\] b 0 .* e0 [-0-9,]+ hp 60 \| e1 [-0-9,]+ hp 60$'
check ap_diff_galaxy "Galaxy set, the screen skipped" '\[ap\] difficulty Galaxy \(the seed.s\): New Game skips the Battle Rank screen'
check ap_diff_galaxy "the monsters' HP x1.5" '\[bunits\] b 0 .* e0 [-0-9,]+ hp 90 \| e1 [-0-9,]+ hp 90$'
check ap_diff_universe "Universe set, the screen skipped" '\[ap\] difficulty Universe \(the seed.s\): New Game skips the Battle Rank screen'
check ap_diff_universe "the monsters' HP x2" '\[bunits\] b 0 .* e0 [-0-9,]+ hp 120 \| e1 [-0-9,]+ hp 120$'
check ap_diff_universe "Claude's game (map 7)" '\[leader\] vblank 3600 map 7 state 0 '
check ap_diff_any "any: the screen offered" '\[ap\] difficulty any \(the seed.s\): New Game offers the Battle Rank screen'
check ap_diff_any "the Battle Rank screen opens" '\[trace\] 0x80048cec at vblank'
check ap_diff_any "Universe picked" '\[ap\] difficulty: Universe picked'
check ap_diff_any "the monsters' HP x2" '\[bunits\] b 0 .* e0 [-0-9,]+ hp 120 \| e1 [-0-9,]+ hp 120$'
check ap_diff_none "no seed: Claude's game (map 7)" '\[leader\] vblank 3600 map 7 state 0 '
check ap_diff_none "the monsters as the original" '\[bunits\] b 0 .* e0 [-0-9,]+ hp 60 \| e1 [-0-9,]+ hp 60$'
for name in ap_diff_earth ap_diff_galaxy ap_diff_universe; do
  if grep -q '\[trace\] 0x80048cec' "$WORK/$name.log"; then echo "FAIL $name: the Battle Rank screen showed"; fail=1; fi
done
for name in ap_diff_none ap_hero_claude ap_hero_rena ap_hero_any ap_cs_any; do  # (no seed, or no difficulty key)
  if grep -q '\[trace\] 0x80048cec\|\[ap\] difficulty' "$WORK/$name.log"; then
    echo "FAIL $name: the Battle Rank screen or a seed's difficulty without one"; fail=1
  fi
done
# A story battle the script lets the party lose (the ship, map 548: Shin, ext 0xb7 third
# argument 0): a DeathLink received in it waits; the loss itself is no game over (nothing
# sent), and the DeathLink applies once the field leaves the controls to the player.
"${AP_RUN[@]}" SS_DEBUG_INPUT="1500:ap_deathlink Bob" SS_SCRIPT_GLOBALS="a0=29,a4=30" SS_EVENT_SWEEP=1 SS_SWEEP_PARTY=1 \
  SS_SWEEP_PLAY=9999:T0 SS_TRACE_LEADER=100 timeout 900 "$BIN" --headless --debug --ap-seed "$WORK/p3.apso2" \
  --frames 2800 --map 548 </dev/null >"$WORK/ap_p3_dl_story.log" 2>&1
check ap_p3_dl_story "the story battle is lost as scripted" '\[battle\] exit: encounter 517 outcome 2 battle frames 2184 '
check ap_p3_dl_story "the party falls after it, not in it" '\[ap\] DeathLink from Bob: the party falls \(vblank 26[0-9][0-9],'
if grep -q "falls in battle" "$WORK/ap_p3_dl_story.log"; then echo "FAIL ap_p3_dl_story: applied inside the losable battle"; fail=1; fi

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
# The save guard [hubcore agent] (docs/ap/seed_format.md "The save sidecar"): with a seed, a save
# loads only with this seed's sidecar holding the hash of the save as it is on the card. The save
# just made loads; one byte changed in it on the card, the load fails as a damaged save does; a
# save without a sidecar fails too. Without a seed the changed card is the game's own business.
if grep -q '"save_hash": "sha256:[0-9a-f]\{64\}"' "$WORK/save_ap.slot01.ap.json"; then echo "PASS ap_guard_hash: the sidecar holds the save's hash"; else echo "FAIL ap_guard_hash: no save_hash in the sidecar"; fail=1; fi
guard_load() {  # guard_load NAME CARD [env...]
  local name=$1 card=$2
  shift 2
  env "$@" SS_MODS_DIR="$WORK/no-mods" SS_LOAD_SAVE=0 SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy SS_TRACE_LEADER=30 \
    SS_MEMCARD1="$card" timeout 900 "$BIN" --headless --ap-seed "$WORK/tiny.apso2" --frames 90 </dev/null >"$WORK/$name.log" 2>&1
}
guard_load ap_guard_ok "$WORK/save_ap.mcd"
check ap_guard_ok "this seed's save loads" '\[ap\] loaded .*save_ap\.slot01\.ap\.json: 1 checks'
check ap_guard_ok "into the field" '\[leader\] vblank [0-9]+ map 18 state 0 '
cp "$WORK/save_ap.mcd" "$WORK/guard.mcd"
cp "$WORK/save_ap.slot01.ap.json" "$WORK/guard.slot01.ap.json"
printf '\x5a' | dd of="$WORK/guard.mcd" bs=1 seek=$((0x2000 + 0x500)) conv=notrunc status=none
guard_load ap_guard_edit "$WORK/guard.mcd"
check ap_guard_edit "a changed save is refused" '\[ap\] load refused, save 1 of .*guard\.mcd: this save was changed outside the game'
check ap_guard_edit "as a failed load" 'SS_LOAD_SAVE: no valid save at that position'
if grep -q '\[leader\] vblank' "$WORK/ap_guard_edit.log"; then echo "FAIL ap_guard_edit: the changed save reached the field"; fail=1; fi
guard_load ap_guard_plain "$WORK/save_plain.mcd"
check ap_guard_plain "a save without the seed's state is refused" "\\[ap\\] load refused, save 1 of .*save_plain\\.mcd: this save isn't from this Archipelago seed"
guard_load ap_guard_any "$WORK/guard.mcd" SS_AP_ANY_SAVE=1
check ap_guard_any "SS_AP_ANY_SAVE=1 (tests) loads it, logged" 'changed outside the game .*; loaded anyway \(SS_AP_ANY_SAVE=1\)'

# Archipelago phase 4 [world agent]: the open world. Map 17's door to Arlia (door 1116181,
# routine 0x204c) needs key 1: walking in shows the banner and nothing transfers; once the key
# comes (console), walking in again loads map 18. With a target, the same door leads to map 23.
# [salvadrift agent] The shut door holds the hero on the step that entered its region (no walking
# on through it: script_triggers.cpp ShutDoor); the key opens it at the next step in.
# The Expel teleporter at chapter 40: the arena's old man (map 303, K25) sends the party to
# Arlia in an open-world seed and only talks without one.
cat >"$WORK/p4door.apso2" <<'SEED'
{"format": "apso2", "version": 1, "seed": "smoke-4", "slot": {"name": "Claude", "id": 1},
 "options": {"world_mode": "open_world"},
 "items": {"5300001": {"name": "Arlia Pass", "kind": "key", "key": 1}}, "locations": [],
 "doors": [{"id": 1116181, "routine": "0x204c", "key": 1, "name": "Arlia exit -> Arlia"}]}
SEED
cat >"$WORK/p4redirect.apso2" <<'SEED'
{"format": "apso2", "version": 1, "seed": "smoke-4r", "slot": {"name": "Claude", "id": 1}, "locations": [],
 "doors": [{"id": 1116181, "to": {"map": 23, "x": 224, "y": 0, "z": 106, "dir": 198}}]}
SEED
printf '{"format": "apso2", "version": 1, "seed": "smoke-4s", "slot": {"name": "Claude", "id": 1}, "locations": []}\n' >"$WORK/p4story.apso2"
sed 's/"smoke-4s"/"smoke-4t"/; s/"locations"/"options": {"world_mode": "open_world"}, "locations"/' "$WORK/p4story.apso2" >"$WORK/p4open.apso2"
"${AP_RUN[@]}" SS_TRACE=1 SS_TRACE_LEADER=15 SS_DEBUG_INPUT="400:ap_give key:1 Bob" timeout 900 "$BIN" --headless --debug \
  --ap-seed "$WORK/p4door.apso2" --frames 800 --map 17 --map-pos 110,0,-457 --input "100:right,200:none,600:right,700:none" \
  </dev/null >"$WORK/ap_p4_door.log" 2>&1
check ap_p4_door "the locked door does not transfer" '\[ap\] door 1116181 \(Arlia exit -> Arlia\): locked, needs Arlia Pass'
check ap_p4_door "the leader stays at the door" '\[leader\] vblank 180 map 17 state 0 pos 13[0-9],0,-457 .* control 0 '
check ap_p4_door "the key opens it" 'trigger 9 fires \(code word 2067\) at leader 5[01][0-9],0,11[0-9] at vblank 6[0-9][0-9]'
check ap_p4_door "map 18 loads" 'field_load_map\(map=18 keep=1 flags=0\) at vblank 6[0-9][0-9]'
if grep -q 'field_load_map(map=18 keep=1 flags=0) at vblank [1-5][0-9][0-9]$' "$WORK/ap_p4_door.log"; then echo "FAIL ap_p4_door: transferred while locked"; fail=1; fi
"${AP_RUN[@]}" SS_TRACE=1 timeout 900 "$BIN" --headless --ap-seed "$WORK/p4redirect.apso2" --frames 170 --map 17 \
  --map-pos 110,0,-457 --input "100:right,200:none" </dev/null >"$WORK/ap_p4_redirect.log" 2>&1
check ap_p4_redirect "the door's target is the seed's" '\[ap\] door 1116181: map 18 \(230, 0, -133\) -> map 23 \(224, 0, 106\)'
check ap_p4_redirect "map 23 loads" 'field_load_map\(map=23 keep=1'
# [salvadrift agent] The hub's mansion door (Alen's room 53 with the bookcase moved, door 3480401,
# routine 0x6c08: the ramp behind the bookcase down to Salva Drift 62). Without its key the hero
# stays on the ramp at the region's edge (before the fix: on past the ramp's end into the dark, y
# -98, out of sight); the key then opens it at the next step in, map 62 as before.
cat >"$WORK/hubmansion.apso2" <<'SEED'
{"format": "apso2", "version": 3, "seed": "smoke-hubmansion", "slot": {"name": "Hub", "id": 1}, "options": {"world_mode": "hub"},
 "items": {"5300001": {"name": "Salva Drift Key", "kind": "key", "key": 1}}, "locations": [],
 "doors": [{"id": 3480401, "key": 1, "routine": "0x6c08", "name": "Salva Drift (map 53 -> 62)"}],
 "hub": {"lock": {"chapter": true, "step": true}}}
SEED
"${AP_RUN[@]}" SS_TRACE=1 SS_TRACE_LEADER=30 SS_DEBUG_INPUT="450:ap_give key:1 Bob" timeout 900 "$BIN" --headless --debug \
  --ap-seed "$WORK/hubmansion.apso2" --frames 700 --map 53 --map-pos -30,0,-54 --input "100:up,600:none" \
  </dev/null >"$WORK/ap_hub_mansion.log" 2>&1
check ap_hub_mansion "refused without the key" '\[ap\] door 3480401 \(Salva Drift \(map 53 -> 62\)\): locked, needs Salva Drift Key'
check ap_hub_mansion "the hero stays on the ramp" '\[leader\] vblank 420 map 53 state 0 pos -29,-(6[89]|70),-1(39|4[01]) '
check ap_hub_mansion "the key opens it at the next step" 'field_load_map\(map=62 keep=1 flags=2\) at vblank 4[5-9][0-9]'
for v in open story; do
  SS_DISC2="${SS_DISC2:-${SS_DISC1//Disc 1/Disc 2}}" "${AP_RUN[@]}" SS_SCRIPT_GLOBALS="a0=40,a4=0" SS_EVENT_SWEEP=1 SS_EVENT_SWEEP_DISC=2 \
    timeout 900 "$BIN" --headless --ap-seed "$WORK/p4$v.apso2" --frames 4700 --map 303 </dev/null >"$WORK/ap_p4_tele_$v.log" 2>&1
done
check ap_p4_tele_open "the teleporter's edits apply (3, with the harbour's 12)" '\[mods\] 15 script, 0 item, 0 shop, 0 enemy patch\(es\) from the seed'
check ap_p4_tele_open "the old man sends the party to Arlia at chapter 40" '\[sweep\] map 303 vblank [0-9]+ entry 22 K25 end map 18 '
check ap_p4_tele_story "without the teleporter he only talks" '\[sweep\] map 303 vblank [0-9]+ entry 22 K25 end ok '

# Archipelago phase 4 [ships agent]: Port Hilton's harbour (docs/ap/seed_format.md "The harbour").
# Chapter 23, in front of the Hilton sailor (map 169, actor 17): in an open-world seed he no longer
# refuses (the soft-lock) and offers Cross / Hoffmann Ruins / Not now; Cross pays and sails (map 1).
# In story mode he refuses as the original (no menu, no voyage). Chapter 27 step 7000: El is
# offered, and without its pass (the seed's El voyage door) the banner names it, nothing stored.
cat >"$WORK/p4harbour.apso2" <<'SEED'
{"format": "apso2", "version": 1, "seed": "smoke-4h", "slot": {"name": "Claude", "id": 1},
 "options": {"world_mode": "open_world"},
 "items": {"5300018": {"name": "El Beach Pass", "kind": "key", "key": 18}}, "locations": [],
 "doors": [{"id": 29106503, "routine": "0x7c28", "keys": [18], "name": "El voyage", "purpose": "to sail"}]}
SEED
HARBOUR=(--headless --debug --map 169 --map-pos -4127,0,-1091)
for v in harbour story; do
  "${AP_RUN[@]}" SS_TRACE=1 SS_SCRIPT_GLOBALS="a0=23,a4=0" SS_DEBUG_INPUT="60:fol 5000;250:warp 169 -4127 0 -1091 89" \
    timeout 900 "$BIN" "${HARBOUR[@]}" --ap-seed "$WORK/p4$v.apso2" --frames 1000 --input "450:cross,454:none,670:cross,674:none" \
    </dev/null >"$WORK/ap_p4_$v.log" 2>&1
done
check ap_p4_harbour "the captain's menu at chapter 23" '\[ap\] harbour: Cross, Hoffmann Ruins, Not now \(chapter 23 step 0\)'
check ap_p4_harbour "Cross chosen" '\[ap\] harbour: option 0, Cross'
check ap_p4_harbour "and the voyage starts" 'field_load_map\(map=1 keep=1'
if grep -q '\[ap\] harbour' "$WORK/ap_p4_story.log"; then echo "FAIL ap_p4_story: the harbour in story mode"; fail=1; fi
if grep -q 'field_load_map(map=1 ' "$WORK/ap_p4_story.log"; then echo "FAIL ap_p4_story: sailed in story mode"; fail=1; fi
check ap_p4_story "story mode: he refuses (his window)" 'msg_window_open_default\(text=.* at vblank 45[0-9]'
"${AP_RUN[@]}" SS_SCRIPT_GLOBALS="a0=27,a4=7000" SS_DEBUG_INPUT="60:fol 5000;250:warp 169 -4127 0 -1091 89;1000:where" \
  timeout 900 "$BIN" "${HARBOUR[@]}" --ap-seed "$WORK/p4harbour.apso2" --frames 1100 \
  --input "450:cross,454:none,650:down,654:none,690:down,694:none,750:cross,754:none" </dev/null >"$WORK/ap_p4_el.log" 2>&1
check ap_p4_el "El on offer at chapter 27 step 7000" '\[ap\] harbour: Cross, Hoffmann Ruins, El, Not now \(chapter 27 step 7000\)'
check ap_p4_el "El without its pass: refused" '\[ap\] door 29106503 \(El voyage\): locked, needs El Beach Pass'
check ap_p4_el "nothing stored" '\[debug\] map 169 .* G\[0xa0\]=27 G\[0xa4\]=7000'

# Archipelago phase 4 [world agent; shopboth agent]: shop shuffle. Hearn's store (map 26, shop
# 1): entry 0 (Sandals, item 728) is Alice's; entry 2 (Lavender, item 224) is our own Revival
# Card. Talk to Hearn (up to his counter, confirm the Yes/No prompt), Buy, add 1 Sandal and 3
# Lavenders, settle: every copy bought is ours (the Equipment Wizard equips the Sandals) and
# each slot's first purchase is also its check (Sandals: Hookshot sent to Alice; Lavender: the
# Revival Card given). A second visit buys one more Lavender: just the item. `item N 20` says
# how many fit (20 minus those held). menu_shop.cpp settle / draw_row (the Archipelago mark);
# ap_shop_slots unit-tests the rest.
cat >"$WORK/p4shop.apso2" <<'SEED'
{"format": "apso2", "version": 1, "seed": "smoke-4shop", "slot": {"name": "Claude", "id": 1},
 "options": {"shuffle_shops": "full"},
 "items": {"5000021": {"name": "Revival Card", "kind": "item", "game_id": 21}},
 "locations": [
   {"id": 1365246466, "name": "Arlia shop: Lavender", "check": {"type": "shop", "shop": 1, "index": 2, "item": 224},
    "item": {"player": 1, "kind": "item", "game_id": 21, "name": "Revival Card"}},
   {"id": 1365246464, "name": "Arlia shop: Sandals", "check": {"type": "shop", "shop": 1, "index": 0},
    "item": {"player": 2, "player_name": "Alice", "name": "Hookshot"}}
 ]}
SEED
SHOP1="200:up,439:none,450:left,663:none,670:up,1000:none,1010:left,1060:none,1070:up,1250:none,1260:cross,1266:none,1300:cross,1306:none,1400:cross,1406:none,1500:cross,1506:none,1560:cross,1566:none,1600:right,1606:none,1650:down,1656:none,1700:down,1706:none,1750:right,1756:none,1800:right,1806:none,1850:right,1856:none,1900:cross,1906:none,1950:cross,1956:none,2100:cross,2106:none,2320:cross,2326:none"
SHOP2="2460:cross,2466:none,2500:cross,2506:none,2600:cross,2606:none,2700:cross,2706:none,2760:cross,2766:none,2850:down,2856:none,2900:down,2906:none,2950:right,2956:none,3100:cross,3106:none,3150:cross,3156:none,3300:cross,3306:none"
"${AP_RUN[@]}" SS_DEBUG_INPUT="50:fol 50000;1620:where;2300:where;2440:where;3450:where;3500:item 224 20;3510:item 21 20" \
  timeout 900 "$BIN" --headless --debug --ap-seed "$WORK/p4shop.apso2" --frames 3600 --map 18 --input "$SHOP1,$SHOP2" \
  </dev/null >"$WORK/ap_p4_shop.log" 2>&1
check ap_p4_shop "the Buy list opens (shop 1, Hearn's)" '\[debug\] map 26 \| state 3 '
check ap_p4_shop "another slot's item is sent" '\[ap\] check 1365246464 \(Arlia shop: Sandals\): Hookshot for Alice'
check ap_p4_shop "and the Sandals bought are ours, the purchase the check" '\[ap\] shop 1 entry 0 \(item 728\): 1 bought, and the slot.s check'
check ap_p4_shop "the Equipment Wizard equips them" '\[debug\] map 26 \| state 15 .* Fol 48520 '
check ap_p4_shop "our own slot's check gives the Revival Card" '\[ap\] check 1365246466 \(Arlia shop: Lavender\): Revival Card'
check ap_p4_shop "and the 3 Lavenders bought are ours" '\[ap\] shop 1 entry 2 \(item 224\): 3 bought, and the slot.s check'
check ap_p4_shop "Fol paid for all 4 (1x10 + 3x490)" '\[debug\] map 26 \| state 0 .* Fol 48520 '
check ap_p4_shop "a second purchase: one more Lavender (490)" '\[debug\] map 26 \| state 0 .* Fol 48030 '
check ap_p4_shop "4 Lavenders held" '\[debug\] item 224: added 16$'
check ap_p4_shop "1 Revival Card held" '\[debug\] item 21: added 19$'
if [ "$(grep -c '\[ap\] shop 1 entry' "$WORK/ap_p4_shop.log")" != 2 ]; then
  echo "FAIL ap_p4_shop: the second purchase made a check"; fail=1
else
  echo "PASS ap_p4_shop: the second purchase is just the item"
fi
# A full stack: 20 Revival Cards held, the Lavender slot's Revival Card waits (the 3 Lavenders
# go in); a Revival Card sold on the second visit makes room and it is given.
SHOP2SELL="2460:cross,2466:none,2500:cross,2506:none,2600:cross,2606:none,2700:cross,2706:none,2770:right,2776:none,2810:cross,2816:none,2900:right,2906:none,3100:cross,3106:none,3150:cross,3156:none,3300:cross,3306:none"
"${AP_RUN[@]}" SS_DEBUG_INPUT="50:fol 50000;60:item 21 20;3450:where;3700:item 224 20;3710:item 21 20" \
  timeout 900 "$BIN" --headless --debug --ap-seed "$WORK/p4shop.apso2" --frames 3800 --map 18 --input "$SHOP1,$SHOP2SELL" \
  </dev/null >"$WORK/ap_shop_full.log" 2>&1
check ap_shop_full "the check's Revival Card waits (20 held)" '\[ap\] the inventory is full; Revival Card waits'
check ap_shop_full "the 3 Lavenders bought go in" '\[ap\] shop 1 entry 2 \(item 224\): 3 bought, and the slot.s check'
check ap_shop_full "one sold (50 Fol) makes room" '\[debug\] map 26 \| state 0 .* Fol 48570 '
check ap_shop_full "and the Revival Card is given" '\[ap\] given Revival Card'
check ap_shop_full "3 Lavenders held" '\[debug\] item 224: added 17$'
check ap_shop_full "20 Revival Cards held" '\[debug\] item 21: added 0$'
# An order (shops 0x5a-0x5e: the APWorld makes none of them slots; a seed may): Hearn's B 0x6d 1
# made the order shop 0x5a (map 26 word 0x21d), its Blueberry slot holds our own Blueberry. 20
# Blueberries ordered: placing the order is the check (a Blueberry given at once); the courier
# (G[0x334] = game_state +0x294 due, flag 13) delivers all 20: 19 fit, the 21st waits.
mkdir -p "$WORK/mods-order"
echo "script 26 0x21d 0xff6d0001 0xff6d005a" >"$WORK/mods-order/order.patch"
cat >"$WORK/order.apso2" <<'SEED'
{"format": "apso2", "version": 1, "seed": "smoke-order", "slot": {"name": "Claude", "id": 1},
 "options": {"shuffle_shops": "full"},
 "items": {"5000757": {"name": "Blueberry", "kind": "item", "game_id": 757}},
 "locations": [
   {"id": 1365269506, "name": "Order shop: Blueberry", "check": {"type": "shop", "shop": 90, "index": 2, "item": 757},
    "item": {"player": 1, "kind": "item", "game_id": 757, "name": "Blueberry"}}
 ]}
SEED
ORDER_R=$(for i in $(seq 0 19); do f=$((1750 + i * 12)); printf '%d:right,%d:none,' "$f" $((f + 5)); done)
"${AP_RUN[@]}" SS_MODS_DIR="$WORK/mods-order" SS_DEBUG_INPUT="50:fol 50000;2390:where;2400:global 0x334 1;2401:flag 13 1;3200:item 757 20" \
  timeout 900 "$BIN" --headless --debug --ap-seed "$WORK/order.apso2" --frames 3300 --map 18 \
  --input "200:up,439:none,450:left,663:none,670:up,1000:none,1010:left,1060:none,1070:up,1250:none,1260:cross,1266:none,1300:cross,1306:none,1400:cross,1406:none,1500:cross,1506:none,1560:cross,1566:none,1650:down,1656:none,1700:down,1706:none,${ORDER_R}2100:cross,2106:none,2150:cross,2156:none,2300:cross,2306:none,2700:cross,2706:none,2800:cross,2806:none,2900:cross,2906:none" \
  </dev/null >"$WORK/ap_shop_order.log" 2>&1
check ap_shop_order "Hearn's shop is the order shop 0x5a" 'order.patch:1: map 26 word 541 0xff6d0001 -> 0xff6d005a'
check ap_shop_order "placing the order is the check" '\[ap\] shop 90 entry 2 \(item 757\): 20 bought, and the slot.s check'
check ap_shop_order "20 Blueberries paid (60 each)" '\[debug\] map 26 \| state 0 .* Fol 48800 '
check ap_shop_order "the courier: 19 fit, the 21st waits" '\[ap\] courier: 20 x item 757 ordered, 19 fit; 1 wait'
check ap_shop_order "20 Blueberries held" '\[debug\] item 757: added 0$'

# Received members stay recruited [world agent] (docs/ap/seed_format.md "Character items").
# Rena's game (flag 1 cleared, chapter 15): Celine received with flag 112 clear sets it; map
# 126's scene (step 1000) takes her out for a while, which drops her pending join; map 125 at
# step 3300 adds her back (its join site is the check). Map 131's scene (Claude's game, step 0)
# stores party_has(Celine) = 0 in flag 112: kept set. Cross Cave (map 524, chapter 13) with its
# choice forced to 1 by a seed patch ("Decide against it"): she leaves, then rejoins in free play.
cat >"$WORK/cel.apso2" <<'SEED'
{"format": "apso2", "version": 1, "seed": "smoke-cel", "slot": {"name": "Claude", "id": 1},
 "options": {"character_items": true},
 "items": {"5100003": {"name": "Celine", "kind": "character", "character": 3}},
 "locations": [{"id": 1353970688, "name": "Celine joins (map 125)", "check": {"type": "join", "map": 125, "offset": 39660, "character": "Celine"},
                "item": {"player": 2, "player_name": "Alice", "name": "Map Width"}}],
 "patches": ["script 524 0x1c09 0x0b000000 0x12000001"]}
SEED
CEL_RUN=(env SS_MODS_DIR="$WORK/no-mods" SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy SS_EVENT_SWEEP=1 SS_BATTLE_STUB=1)
"${CEL_RUN[@]}" SS_SWEEP_FLAGS="!1" SS_SWEEP_PARTY=1,2 SS_SCRIPT_GLOBALS="a0=15,a4=900,c4=0x1" SS_SWEEP_PLAY="999:T0" \
  SS_DEBUG_INPUT="5:ap_give Celine Bob;30:global 0xa4 1000;32:warp 126;500:global 0xa4 3300;1100:warp 125" \
  timeout 900 "$BIN" --headless --no-memcard --debug --ap-seed "$WORK/cel.apso2" --frames 1300 --map 126 </dev/null >"$WORK/ap_cel_mars.log" 2>&1
check ap_cel_mars "Celine received: flag 112 set" '\[ap\] Celine received: script flag 112 set'
check ap_cel_mars "map 126 takes her out for a while" '\[ap\] Celine leaves at map 126 for a while'
check ap_cel_mars "map 125 brings her back (the check)" '\[ap\] check 1353970688 '
check ap_cel_mars "through the game's join" '\[ap\] join site 125:39660: Celine was received: the game.s join'
"${CEL_RUN[@]}" SS_SWEEP_PARTY=1 SS_SCRIPT_GLOBALS="a0=15,a4=500" SS_SWEEP_PLAY="999:T0" \
  SS_DEBUG_INPUT="5:ap_give Celine Bob;30:global 0xa4 0;32:warp 131" \
  timeout 900 "$BIN" --headless --no-memcard --debug --ap-seed "$WORK/cel.apso2" --frames 300 --map 131 </dev/null >"$WORK/ap_cel_131.log" 2>&1
check ap_cel_131 "map 131 cannot clear flag 112" '\[ap\] script flag 112 kept set: Celine was received'
"${CEL_RUN[@]}" SS_SWEEP_PARTY=1,2 SS_SCRIPT_GLOBALS="a0=13,a4=0" SS_SWEEP_PLAY="524:T4" SS_DEBUG_INPUT="5:ap_give Celine Bob" \
  timeout 900 "$BIN" --headless --no-memcard --debug --ap-seed "$WORK/cel.apso2" --frames 5200 --map 524 </dev/null >"$WORK/ap_cel_cave.log" 2>&1
check ap_cel_cave "Cross Cave's parting" '\[ap\] Celine leaves at map 524 \(Cross Cave, "Decide against it"\): received, so they rejoin'
check ap_cel_cave "she rejoins in free play" '\[ap\] Celine joins the party \(slot 2\)'

# v2 party [party agent] (docs/ap/seed_format.md "Party v2"): party_rules "open". Rena's game
# (a --map start begins as Claude: SS_SWEEP_PARTY_DROP empties his slot), Claude not received:
# map 12's scene (chapter 4 step 150) takes Rena out and lets Claude in for the fight only (its
# listed site still checks). Claude's game: the tournament's Dias fight (477, chapter 19 step
# 4400) lets Dias in for a while; the starting character (Ashton) joins at the first free play
# and leads; a received Dias joins (no lockout); Linga's split (193, chapter 22 step 3000) keeps
# received members out until it is over.
cat >"$WORK/v2rena.apso2" <<'SEED'
{"format": "apso2", "version": 2, "seed": "smoke-v2r", "slot": {"name": "Rena", "id": 1},
 "options": {"world_mode": "open_world", "character_items": true, "party_rules": "open", "story_route": "rena"},
 "items": {"5100001": {"name": "Claude", "kind": "character", "character": 1}},
 "locations": [{"id": 1353854976, "name": "Shingo Forest: Claude joins", "check": {"type": "join", "map": 12, "offset": 28168, "character": "Claude"},
                "item": {"player": 2, "player_name": "Alice", "name": "Map Width"}}]}
SEED
sed 's/"smoke-v2r"/"smoke-v2c"/; s/"story_route": "rena"/"story_route": "claude", "starting_character": "Ashton"/; s/"locations": \[.*/"locations": []}/' \
  "$WORK/v2rena.apso2" | grep -v '"item": {"player": 2' >"$WORK/v2claude.apso2"
V2_RENA=(SS_SWEEP_FLAGS="!1" SS_SWEEP_PARTY_DROP=0 SS_SWEEP_PARTY=1 SS_SWEEP_CONFIG=45=1 SS_SWEEP_PLAY="999:T0")
"${CEL_RUN[@]}" "${V2_RENA[@]}" SS_SCRIPT_GLOBALS="a0=4,a4=150" \
  timeout 900 "$BIN" --headless --no-memcard --debug --ap-seed "$WORK/v2rena.apso2" --frames 1700 --map 12 </dev/null >"$WORK/ap_v2_m12.log" 2>&1
check ap_v2_m12 "the listed site checks" '\[ap\] check 1353854976 '
check ap_v2_m12 "Claude in for the fight only" '\[ap\] join Claude at map 12 .*: for a while \(Shingo Forest \(map 12\)\): Claude, \(Rena\)'
check ap_v2_m12 "the fight" '\[ap\] a battle at vblank'
check ap_v2_m12 "Rena back" '\[ap\] join Rena at map 12 .*: the game.s join'
"${CEL_RUN[@]}" SS_SWEEP_PLAY="999:T0" SS_SCRIPT_GLOBALS="a0=19,a4=4400" \
  timeout 900 "$BIN" --headless --no-memcard --debug --ap-seed "$WORK/v2claude.apso2" --frames 900 --map 477 </dev/null >"$WORK/ap_v2_t477.log" 2>&1
check ap_v2_t477 "Dias in for his fight" '\[ap\] join Dias at map 477 .*: for a while \(the tournament'
check ap_v2_t477 "Claude back after it" '\[ap\] join Claude at map 477 .*Claude.s game\): the game.s join'
"${CEL_RUN[@]}" SS_SWEEP_PLAY="999:T0" SS_DEBUG_INPUT="600:ap_give Dias Bob" \
  timeout 900 "$BIN" --headless --no-memcard --debug --ap-seed "$WORK/v2claude.apso2" --frames 1000 --map 7 </dev/null >"$WORK/ap_v2_start.log" 2>&1
check ap_v2_start "the starting character joins first" '\[ap\] Ashton joins the party \(slot 1\): Claude, Ashton'
check ap_v2_start "and leads" '\[ap\] Ashton leads \(Formation leader, slot 1\)'
check ap_v2_start "no lockout: Dias in Claude's game" '\[ap\] Dias joins the party \(slot 2\)'
# The join level (open world, hub) [skillsanity2]: the party's median (the hero's level 1 here).
check ap_v2_start "the join level: the start at the hero's" '\[ap\] join level: Ashton joins at level 1 \(the party.s median; their own 17\): down, HP 1000 -> 288'
check ap_v2_start "the join level: a received member" '\[ap\] join level: Dias joins at level 1 \(the party.s median; their own 35\): down'
# Skillsanity [skillsanity2] (docs/ap/seed_format.md "Skillsanity"): Herlie's guildmaster (map 147,
# K17) sells Knowledge 2 (flag 704): its 3 checks (ours given, Alice's sent), no skill opened by it.
cat >"$WORK/skguild.apso2" <<'SEED'
{"format": "apso2", "version": 2, "seed": "smoke-skguild", "slot": {"name": "Claude", "id": 1},
 "options": {"skillsanity": "skills_only"}, "items": {},
 "locations": [
   {"id": 1372718336, "name": "Skill Guild: Knowledge 2 (1 of 3)", "check": {"type": "guild", "set": 4, "index": 0},
    "item": {"player": 1, "kind": "skill", "skill": 4, "name": "Skill: Tool Knowledge"}},
   {"id": 1372718337, "name": "Skill Guild: Knowledge 2 (2 of 3)", "check": {"type": "guild", "set": 4, "index": 1},
    "item": {"player": 2, "player_name": "Alice", "name": "Hookshot"}},
   {"id": 1372718338, "name": "Skill Guild: Knowledge 2 (3 of 3)", "check": {"type": "guild", "set": 4, "index": 2},
    "item": {"player": 1, "kind": "fol", "amount": 500}}]}
SEED
"${CEL_RUN[@]}" SS_SWEEP_PLAY="147:K17:0/0/0/0" SS_SCRIPT_GLOBALS="a0=16,a4=0" SS_DEBUG_INPUT="5:fol 50000" \
  timeout 900 "$BIN" --headless --no-memcard --debug --ap-seed "$WORK/skguild.apso2" --frames 600 --map 147 </dev/null >"$WORK/ap_skill_guild.log" 2>&1
check ap_skill_guild "a set bought is its checks" '\[ap\] skillsanity: guild set 4 bought \(flag 704\): 3 checks'
check ap_skill_guild "ours given" '\[ap\] check 1372718336 \(Skill Guild: Knowledge 2 \(1 of 3\)\): Skill: Tool Knowledge'
check ap_skill_guild "Alice's sent" '\[ap\] check 1372718337 \(Skill Guild: Knowledge 2 \(2 of 3\)\): Hookshot for Alice'
# Special: Ashton starts (open world, joins at the hero's level 1) knowing only his first move,
# Twin Stab; Cross Slash and Leaf Slash (his starting data's) become learn points to find.
sed 's/"smoke-v2c"/"smoke-sksp"/; s/"story_route": "claude"/"story_route": "claude", "skillsanity": "special_only"/; s/"locations": \[\]}/"locations": [{"id": 1375735553, "name": "Ashton Lv 9: Cross Slash", "check": {"type": "learn", "character": 7, "index": 1, "level": 9}, "item": {"player": 1, "kind": "fol", "amount": 10}}, {"id": 1375735559, "name": "Ashton Lv 32: Leaf Slash", "check": {"type": "learn", "character": 7, "index": 7, "level": 32}, "item": {"player": 1, "kind": "fol", "amount": 10}}]}/' \
  "$WORK/v2claude.apso2" >"$WORK/sksp.apso2"
"${CEL_RUN[@]}" SS_SWEEP_PLAY="999:T0" \
  timeout 900 "$BIN" --headless --no-memcard --debug --ap-seed "$WORK/sksp.apso2" --frames 700 --map 7 </dev/null >"$WORK/ap_skill_join.log" 2>&1
check ap_skill_join "the hero's first move at New Game" '\[ap\] skillsanity: Claude at level 1: 0 learn points checked'
check ap_skill_join "the start: his moves to find" '\[ap\] skillsanity: Ashton at level 1: 0 learn points checked, 2 moves to find'
# Shops (skillsanity_shop_share): Hearn's entries 0 and 2 sell Perseverance (400) and Claude's
# Shooting Stars (700) as themselves: one each, no game item, Fol 50000 -> 48900.
cat >"$WORK/skshop.apso2" <<'SEED'
{"format": "apso2", "version": 2, "seed": "smoke-skshop", "slot": {"name": "Claude", "id": 1},
 "options": {"shuffle_shops": "full", "skillsanity": "insanity"}, "items": {},
 "locations": [
   {"id": 1365246466, "name": "Arlia shop: Lavender", "check": {"type": "shop", "shop": 1, "index": 2, "item": 224}, "price": 700,
    "item": {"player": 1, "kind": "move", "character": 1, "index": 1, "name": "Claude: Shooting Stars"}},
   {"id": 1365246464, "name": "Arlia shop: Sandals", "check": {"type": "shop", "shop": 1, "index": 0}, "price": 400,
    "item": {"player": 1, "kind": "skill", "skill": 11, "name": "Skill: Perseverance"}}]}
SEED
rm -f "$WORK/card.mcd"
"${AP_RUN[@]}" SS_DEBUG_INPUT="50:fol 50000;2300:where" \
  timeout 900 "$BIN" --headless --debug --ap-seed "$WORK/skshop.apso2" --frames 2400 --map 18 --input "$SHOP1" \
  </dev/null >"$WORK/ap_skill_shop.log" 2>&1
check ap_skill_shop "the skill sold as itself" '\[ap\] check 1365246464 \(Arlia shop: Sandals\): Skill: Perseverance'
check ap_skill_shop "the skill may be learned" '\[ap\] skillsanity: skill 11 may be learned'
check ap_skill_shop "the move sold as itself, known" '\[ap\] skillsanity: Claude knows move 1'
check ap_skill_shop "one each at their prices" '\[debug\] map 26 \| state 0 .* Fol 48900 '
sed 's/, "starting_character": "Ashton"//; s/"items": {/"items": {"5100002": {"name": "Rena", "kind": "character", "character": 2}, "5100003": {"name": "Celine", "kind": "character", "character": 3}, /' \
  "$WORK/v2claude.apso2" >"$WORK/v2linga.apso2"
"${CEL_RUN[@]}" SS_SWEEP_PLAY="999:T0" SS_SWEEP_PARTY=1,2 SS_SCRIPT_GLOBALS="a0=22,a4=3000" \
  SS_DEBUG_INPUT="5:ap_give Rena Bob;6:ap_give Celine Bob;1500:global 0xa4 5100" \
  timeout 900 "$BIN" --headless --no-memcard --debug --ap-seed "$WORK/v2linga.apso2" --frames 1800 --map 193 </dev/null >"$WORK/ap_v2_linga.log" 2>&1
check ap_v2_linga "the split keeps Rena out" '\[ap\] Rena waits: Linga \(193 -> 195\) keeps the party apart'
check ap_v2_linga "back once it is over" '\[ap\] Rena joins the party \(slot [0-7]\)'

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
  # This and the cases below load an ordinary save (or another run's sidecar) with a seed: the
  # save guard would refuse them (ap_guard_*), so they run with the tests' SS_AP_ANY_SAVE=1.
  cp "$SAVE" "$WORK/world.mcd"
  printf '{"format": "apso2-save", "version": 1, "seed": "smoke-1", "slot_name": "Claude", "received_index": 3, "start_given": true, "checked": [1347569665]}\n' \
    >"$WORK/world.slot01.ap.json"
  SS_AP_ANY_SAVE=1 SS_MODS_DIR="$WORK/no-mods" SS_LOAD_SAVE=0 SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy \
    SS_MEMCARD1="$WORK/world.mcd" timeout 900 "$BIN" --headless --ap-seed "$WORK/tiny.apso2" --frames 60 \
    </dev/null >"$WORK/ap_load.log" 2>&1
  check ap_load "a load reads the save's sidecar" '\[ap\] loaded .*world\.slot01\.ap\.json: 1 checks, received index 3'
  # Phase 3 [world agent] on world map 2: an ambush on the area's encounter group (then
  # SS_BATTLE_STUB wins it), a DeathLink there, and one received during the ambush's
  # transition, which takes the party in the battle; encounter_rate 200% / 0%.
  printf '{"format": "apso2", "version": 1, "seed": "smoke-w", "slot": {"name": "Claude", "id": 1}, "options": {"deathlink": true, "encounter_rate": %s}, "items": {}, "locations": []}\n' \
    100 >"$WORK/w100.apso2"
  for r in 200 0; do sed "s/\"encounter_rate\": 100/\"encounter_rate\": $r/" "$WORK/w100.apso2" >"$WORK/w$r.apso2"; done
  wrun() {  # wrun NAME SEED FRAMES DEBUG_INPUT INPUT [env...]
    local name=$1 seed=$2 frames=$3 dbg=$4 input=$5
    shift 5
    cp "$SAVE" "$WORK/world.mcd"
    rm -f "$WORK/world.slot01.ap.json"
    env "$@" SS_AP_ANY_SAVE=1 SS_MODS_DIR="$WORK/no-mods" SS_LOAD_SAVE=0 SS_TRACE_LEADER=10 SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy \
      SS_MEMCARD1="$WORK/world.mcd" SS_DEBUG_INPUT="$dbg" timeout 900 "$BIN" --headless --debug --ap-seed "$WORK/$seed" \
      --frames "$frames" --input "$input" </dev/null >"$WORK/$name.log" 2>&1
  }
  wrun ap_p3_ambush w100.apso2 900 "200:ap_give trap:ambush Bob" "0:none" SS_BATTLE_STUB=1
  check ap_p3_ambush "the ambush takes the area's group" '\[ap\] ambush: the area.s encounter group 50, stage 2'
  check ap_p3_ambush "a battle, then back on the world map" '\[leader\] vblank 7[0-9]0 map 2 state 0 '
  wrun ap_p3_dl_world w100.apso2 400 "200:ap_deathlink Bob" "0:none"
  check ap_p3_dl_world "DeathLink on the world map" '\[leader\] vblank 2[2-9]0 map 2 state 7 '
  wrun ap_p3_dl_trans w100.apso2 1600 "200:ap_give trap:ambush Bob;430:ap_deathlink Bob" "0:none"
  check ap_p3_dl_trans "a DeathLink in the battle transition takes the party in the battle" '\[ap\] DeathLink from Bob: the party falls in battle'
  for r in 100 200 0; do wrun ap_p3_enc$r w$r.apso2 2500 "" "100:left"; done
  check ap_p3_enc100 "encounter rate 100%: first battle as the original" '\[leader\] vblank 1150 map 2 state 4 '
  check ap_p3_enc200 "encounter rate 200%: sooner" '\[leader\] vblank 630 map 2 state 4 '
  if grep -qE 'map 2 state 4 ' "$WORK/ap_p3_enc0.log"; then echo "FAIL ap_p3_enc0: a random battle at 0%"; fail=1; else echo "PASS ap_p3_enc0: no random battle at 0%"; fi
  # Archipelago phase 4 [world agent]: North City's world-map entrance (door 68685998) needs
  # key 2: the leader starts in its region and nothing runs; with the key, re-entering it loads
  # the town.
  printf '{"format": "apso2", "version": 1, "seed": "smoke-4w", "slot": {"name": "Claude", "id": 1}, "options": {"world_mode": "open_world"}, "items": {"5300002": {"name": "North City Pass", "kind": "key", "key": 2}}, "locations": [], "doors": [{"id": 68685998, "routine": "0x42ac", "key": 2, "name": "Nede -> North City"}]}\n' >"$WORK/p4w.apso2"
  cp "$SAVE" "$WORK/world.mcd"
  rm -f "$WORK/world.slot01.ap.json"
  SS_AP_ANY_SAVE=1 SS_MODS_DIR="$WORK/no-mods" SS_LOAD_SAVE=0 SS_TRACE=1 SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy SS_MEMCARD1="$WORK/world.mcd" \
    SS_DEBUG_INPUT="300:ap_give key:2 Bob" timeout 900 "$BIN" --headless --debug --ap-seed "$WORK/p4w.apso2" --frames 520 \
    --map-pos 57219,-300,28622 --input "${NC_INPUT},100:down,160:none,400:up,470:none" </dev/null >"$WORK/ap_p4_world.log" 2>&1
  check ap_p4_world "the locked entrance does not run" '\[ap\] door 68685998 \(Nede -> North City\): locked, needs North City Pass'
  check ap_p4_world "with the key the town loads" 'field_load_map\(map=260 keep=1 flags=2\) at vblank 4[0-9][0-9]'
else
  echo "SKIP north_city: $SAVE not present"
fi

# Archipelago v2 [hooks agent] (docs/ap/seed_format.md "v2 fields"). The goal gate (door
# 43978493, map 671 -> 684, routine 0x3bec) in a story-mode seed at chapter 60: refused without
# flag 556, then without the Iseria Queen (561) won; a forced 561 victory (console, --battle-end)
# goes in goal_won and the door opens. Without a seed the region transfers as the original. The
# Silver Trumpet (item 48) stays off a shop's sell list until The Evil Melody (flag 53).
# Auto-advance (SS_AUTO_ADVANCE) closes the post-battle equipment report by itself.
cat >"$WORK/v2gate.apso2" <<'SEED'
{"format": "apso2", "version": 2, "seed": "smoke-v2", "slot": {"name": "Claude", "id": 1}, "items": {}, "locations": [],
 "goal": {"all": [561, 540], "encounters": [540], "flags": [445, 446]},
 "doors": [{"id": 43978493, "routine": "0x3bec", "flags": [556], "won": [561], "name": "Indalecio (Limiter Off)",
            "banners": ["Talk to Filia in Central City first", "Defeat the Iseria Queen first"]}]}
SEED
"${AP_RUN[@]}" SS_TRACE=1 SS_SCRIPT_GLOBALS=a0=60 SS_AUTO_ADVANCE=1000 SS_DEBUG_INPUT="280:flag 556 1;500:battle 561" timeout 900 \
  "$BIN" --headless --debug --ap-seed "$WORK/v2gate.apso2" --frames 2300 --map 671 --battle-end 60:0 \
  --input "100:up,200:none,210:down,290:none,300:up,380:none,390:down,470:none,2000:up,2200:none" </dev/null >"$WORK/ap_v2_gate.log" 2>&1
check ap_v2_gate "goal.flags set" '\[ap\] goal flags set \(2\)'
check ap_v2_gate "refused without flag 556" 'locked, needs flag 556 \("Talk to Filia in Central City first"\)'
check ap_v2_gate "refused without the Iseria Queen" 'locked, needs encounter 561 won \("Defeat the Iseria Queen first"\)'
check ap_v2_gate "the forced 561 victory is kept" '\[ap\] goal: encounter 561 won \(goal_won 1, goal.all 2\)'
check ap_v2_gate "then the door opens" 'field_load_map\(map=684 keep=1 flags=0\) at vblank 2[01][0-9][0-9]'
"${AP_RUN[@]}" SS_TRACE=1 SS_SCRIPT_GLOBALS=a0=60 timeout 900 "$BIN" --headless --frames 220 --map 671 --input "100:up,200:none" \
  </dev/null >"$WORK/ap_v2_nogate.log" 2>&1
check ap_v2_nogate "no seed: the original transfer" 'field_load_map\(map=684 keep=1 flags=0\) at vblank [12][0-9][0-9]'
V2_SHOP="200:up,439:none,450:left,663:none,670:up,1000:none,1010:left,1060:none,1070:up,1250:none,1260:cross,1266:none,1300:cross,1306:none,1400:cross,1406:none,1500:cross,1506:none"
for v in locked open; do
  [ $v = open ] && dbg="50:item 48;60:flag 53 1;1650:where" || dbg="50:item 48;1650:where"
  "${AP_RUN[@]}" SS_DEBUG_INPUT="$dbg" timeout 900 "$BIN" --headless --debug --ap-seed "$WORK/v2gate.apso2" --frames 1700 --map 18 \
    --input "$V2_SHOP" </dev/null >"$WORK/ap_v2_trumpet_$v.log" 2>&1
done
check ap_v2_trumpet_locked "Hearn's shop opens" '\[debug\] map 26 \| state 3 '
check ap_v2_trumpet_locked "the Silver Trumpet stays off the sell list" '\[ap\] the Silver Trumpet stays off the sell list'
check ap_v2_trumpet_open "Hearn's shop opens" '\[debug\] map 26 \| state 3 '
if grep -q 'stays off the sell list' "$WORK/ap_v2_trumpet_open.log"; then echo "FAIL ap_v2_trumpet_open: kept off after flag 53"; fail=1; else echo "PASS ap_v2_trumpet_open: sold once The Evil Melody is composed"; fi
"${AP_RUN[@]}" SS_AUTO_ADVANCE=1000 SS_TRACE_LEADER=30 timeout 900 "$BIN" --headless --frames 1300 --map 18 \
  --input "200:up,439:none,450:left,593:none,600:up,612:none,640:cross,646:none,800:cross,806:none" </dev/null >"$WORK/auto_wizard.log" 2>&1
check auto_wizard "auto-advance: the Equipment Wizard opened" '\[leader\] vblank [6-9][0-9]0 map 18 state 15 '
check auto_wizard "and closed with no press" '\[leader\] vblank 1260 map 18 state 0 .* control 0 '

# The leader stand-in (open world; docs/ap/seed_format.md "The leader stand-in"): Ashton, the
# starting character, joins on map 8 and leads; the walking actor is drawn as him and Claude's
# line at the ruins' shut door (ruins_exit above) names him. A story-mode seed keeps Claude.
for v in ow story; do
  [ $v = ow ] && wm='"world_mode": "open_world", ' || wm=''
  printf '{"format": "apso2", "version": 2, "seed": "smoke-leader-%s", "slot": {"name": "P", "id": 1}, "options": {"story_route": "claude", %s"starting_character": "Ashton"}, "items": {}, "locations": []}\n' \
    $v "$wm" >"$WORK/leader_$v.apso2"
  "${AP_RUN[@]}" SS_TRACE=1 SS_LEADER_TRACE=1 timeout 900 "$BIN" --headless --ap-seed "$WORK/leader_$v.apso2" --frames 5400 --map 8 \
    --input "${ADV}5000:down,5600:none" </dev/null >"$WORK/ap_leader_$v.log" 2>&1
done
check ap_leader_ow "the walking actor shows Ashton" '\[standin\] vblank [0-9]+ map 8 shows 6 \(stand-in'
check ap_leader_ow "the shut door's line names him" '\[standin\] vblank [0-9]+ map 8 message rewritten 1 6'
check ap_leader_ow "the trigger plays as without him" 'msg_window_open.* at vblank 51[2-9][0-9]'
if grep -qE 'shows 6|message rewritten' "$WORK/ap_leader_story.log"; then
  echo "FAIL ap_leader_story: story mode drew a stand-in"; fail=1
else
  echo "PASS ap_leader_story: story mode keeps Claude"
fi

# Archipelago, the hub [hubvm agent] (docs/ap/seed_format.md "Hub"; engine/src/game/ap/hub.h). At
# chapter 60 step 4000 (the hub's state) Arcmene's chamber (map 417, entered from 418's lower
# door at y -200, where the set-up starts the scene) plays its original scene through the story
# view (chapter 40 step 1000): the fight, the check, the aftermath's chapter-41 store dropped with
# the view at the world map's load, flag 11 kept; the chapter stays 60. A story seed with the
# same rows, and no seed: the chamber is empty at chapter 60 (the original). The chapter lock:
# Cross Cave's key chest (547, flag 2569 clear) stores chapter 13 step 0, dropped in the hub,
# stored in a story seed.
cat >"$WORK/hub.apso2" <<'SEED'
{"format": "apso2", "version": 3, "seed": "smoke-hub", "slot": {"name": "Hub", "id": 1}, "options": {"world_mode": "hub"},
 "items": {"5000757": {"name": "Blueberry", "kind": "item", "game_id": 757}},
 "locations": [{"id": 1364328970, "name": "Arcmene", "check": {"type": "boss", "encounter": 522},
                "item": {"player": 1, "kind": "item", "game_id": 757, "name": "Blueberry"}}],
 "bosses": [{"encounter": 522, "name": "Arcmene", "map": 417,
             "view": {"chapter": 40, "step": 1000, "flags": {"745": 0, "11": 0, "12": 0}, "keep": [11]}}],
 "hub": {"lock": {"chapter": true, "step": true}}}
SEED
sed 's/"version": 3/"version": 2/; s/"world_mode": "hub"/"world_mode": "story"/; s/"smoke-hub"/"smoke-hub-story"/' "$WORK/hub.apso2" >"$WORK/hubstory.apso2"
HUB_RUN=("${CEL_RUN[@]}" SS_SCRIPT_GLOBALS="a0=60,a4=4000" SS_SWEEP_FLAGS=11)
"${HUB_RUN[@]}" SS_SWEEP_PLAY="418:T2" timeout 900 "$BIN" --headless --no-memcard --ap-seed "$WORK/hub.apso2" --frames 9000 --map 418 \
  </dev/null >"$WORK/ap_hub_arcmene.log" 2>&1
check ap_hub_arcmene "the story view comes on in the chamber" '\[ap\] hub: story view on: map 417 \(Arcmene, encounter 522\): chapter 40 step 1000'
check ap_hub_arcmene "the original scene's fight, its check" '\[ap\] check 1364328970 \(Arcmene\)'
check ap_hub_arcmene "the view ends at the transfer out: chapter 41 dropped, flag 11 kept" 'story view off: map 2 .*dropped chapter 41, step 0, flag 12=1, flag 745=1; kept flag 11=1'
check ap_hub_arcmene "the chapter stays 60" 'play: story chapter 60 step 4000'
for v in hubstory none; do
  [ $v = none ] && seedarg=() || seedarg=(--ap-seed "$WORK/$v.apso2")
  "${HUB_RUN[@]}" SS_SWEEP_PLAY="418:T2" timeout 900 "$BIN" --headless --no-memcard "${seedarg[@]}" --frames 600 --map 418 \
    </dev/null >"$WORK/ap_hub_arcmene_$v.log" 2>&1
  check ap_hub_arcmene_$v "no view: the chamber is empty at chapter 60" 'play: control on map 417 after 1 of 1 steps'
  if grep -qE 'story view on|a battle at' "$WORK/ap_hub_arcmene_$v.log"; then echo "FAIL ap_hub_arcmene_$v: a view or a fight"; fail=1; else echo "PASS ap_hub_arcmene_$v: no view, no fight"; fi
done
"${HUB_RUN[@]}" SS_SWEEP_PLAY="547:K60" timeout 900 "$BIN" --headless --no-memcard --ap-seed "$WORK/hub.apso2" --frames 2100 --map 547 \
  </dev/null >"$WORK/ap_hub_lock.log" 2>&1
check ap_hub_lock "the chest's chapter store is dropped" '\[ap\] hub: chapter store 13 dropped at map 547 offset 0x6a80 \(the lock\)'
check ap_hub_lock "and its step store" '\[ap\] hub: step store 0 dropped at map 547 offset 0x6a88 \(the lock\)'
check ap_hub_lock "the chapter stays 60" 'play: story chapter 60 step 4000'
"${HUB_RUN[@]}" SS_SWEEP_PLAY="547:K60" timeout 900 "$BIN" --headless --no-memcard --ap-seed "$WORK/hubstory.apso2" --frames 2100 --map 547 \
  </dev/null >"$WORK/ap_hub_lock_story.log" 2>&1
check ap_hub_lock_story "a story seed: the chest stores chapter 13 (the original)" 'play: story chapter 13 step 0'

# Archipelago, the hub's New Game, starts, doors and P5 [hubcore agent] (docs/ap/seed_format.md
# "Hub", "The port: New Game, the starts, the doors, P5"). Seeds from docs/ap/hub_state.json's
# hub block and starts and hub_bosses.json's rows. New Game in each kind of start: Fun City
# (Energy Nede, Claude's game), Arlia (Expel, Rena's) and Linga (one of random_town's other
# towns): chapter 60 step 4000 on disc 2, the party at the start's arrival, its exit to the
# world map, the Synard parked where the row says. Fun City's world map: the Cave of Red Crystal's
# entrance (door 68686044) refused without its key and entered with it, Fienal's (68685691)
# refused until Magichand (524) is won. The arena antechamber's door 304 -> 296 leads to
# Marsilio's arena 305 while he is unbeaten (to.while_unbeaten) and to 296 after. Needs disc 2.
D2="${SS_DISC2:-${SS_DISC1:+${SS_DISC1//Disc 1/Disc 2}}}"
if [ -n "$D2" ] && [ -f "$D2" ]; then
  python3 - "$WORK" <<'PY'
import json, sys
work = sys.argv[1]
hs = json.load(open("docs/ap/hub_state.json"))
hb = json.load(open("docs/ap/hub_bosses.json"))
starts = {s["key"]: s for s in hs["starts"]}
keys = {str(5390000 + g["key"]): {"kind": "key", "key": g["key"], "name": g["name"]} for g in hb["dungeons"]}
rows = [{k: b[k] for k in ("encounter", "name", "map", "maps", "view", "game") if b.get(k) is not None} for b in hb["bosses"]]
# The two chambers that ask first (the APWorld's apworld/so2/hub.py ASK) [hubask agent].
ask = {533: {"text": "Shigeo is on the rampage ahead.\nFace Shigeo now?", "again": "visit"},
       532: {"text": "Marsilio waits in the arena.\nFace Marsilio now?", "doors": [19929509], "again": "visit",
             "options": [{"map": 297, "window": "0x5c50", "choice": "0x5c6c", "text": "Face Marsilio.",
                          "to": {"map": 305, "x": -13, "y": 0, "z": 276, "dir": 271}},
                         {"map": 305, "window": "0x6ea8", "choice": "0x6ec4", "text": "Leave the arena.",
                          "to": {"map": 304, "x": 1339, "y": 0, "z": -6, "dir": 180}}]}}
for r in rows:
    if r["encounter"] in ask:
        r["ask"] = ask[r["encounter"]]
doors = [{"id": 68686044, "routine": "0x4364", "key": 13, "name": "Energy Nede -> Cave of Red Crystal"},
         {"id": 68685691, "routine": "0x3de0", "won": [524], "banners": ["Defeat Magichand first"], "name": "Energy Nede -> Fienal"},
         {"id": 19929509, "to": {"map": 305, "x": -13, "y": 0, "z": 276, "dir": 271, "while_unbeaten": 532},
          "name": "Fun City Fighting Arena (map 304 -> 305 while Marsilio is unbeaten)"}]
def seed(name, hero, start, start_keys=(), fol=0, starting=None):
    hub = dict(hs["hub"], start=dict(starts[start]))
    s = {"format": "apso2", "version": 3, "seed": "smoke-" + name, "slot": {"name": "Hub", "id": 1},
         "options": {"world_mode": "hub", "story_route": hero, "party_rules": "open", "character_items": True},
         "items": keys, "locations": [], "hub": hub, "bosses": rows, "doors": doors}
    if start_keys:
        s["start"] = {"items": [keys[str(5390000 + k)] for k in start_keys]}
    if fol:
        s["start"] = dict(s.get("start", {}), fol=fol)
    if starting:  # [charselect agent] the character select
        s["options"].update(starting_character=starting, exclude_characters=["Bowman", "Opera"])
    json.dump(s, open(f"{work}/{name}.apso2", "w"))
seed("hubfc", "claude", "fun_city")
seed("hubfol", "claude", "fun_city", fol=50000)  # (the arena's registration fee)
seed("hubarlia", "rena", "arlia")
seed("hublinga", "claude", "linga")
seed("hubkey", "claude", "fun_city", (13,))
seed("hubany", "claude", "fun_city", starting="any")
seed("hubashton", "claude", "fun_city", starting="Ashton")  # [hubleader agent] the leader stand-in
PY
  hub_ng() {  # hub_ng NAME SEED HERO FRAMES [env...]: New Game (the prologue skipped) with a hub seed
    local name=$1 seed=$2 press=$3 frames=$4
    shift 4
    # (the seed names the hero: New Game skips the hero select and the naming screen; HUB_INPUT:
    # other presses)
    env "$@" SS_DISC2="$D2" SS_MODS_DIR="$WORK/no-mods" SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy SS_EVENT_SWEEP=1 \
      SS_TRACE_LEADER=50 timeout 900 "$BIN" --headless --debug --no-memcard --ap-seed "$WORK/$seed.apso2" --frames "$frames" \
      --input "${HUB_INPUT:-$NEWGAME_A,$NEWGAME_B}" </dev/null >"$WORK/$name.log" 2>&1
  }
  hub_ng ap_hub_ng_fc hubfc claude 3500 SS_TRACE=1 SS_SWEEP_PLAY="279:T4,2:T21,2:T10"
  check ap_hub_ng_fc "hub New Game at Fun City" "\\[ap\\] hub New Game: Claude's game, [0-9]+ flags, .*chapter 60 step 4000, disc 2; Fun City \\(map 279 at 470, 0, 68, facing 180\\)"
  check ap_hub_ng_fc "the party at the start" '\[leader\] vblank [0-9]+ map 279 state 0 pos 470,0,68 '
  check ap_hub_ng_fc "the Synard by Fun City" 'world: vehicle 1 \(the Synard\) parked at 81546,-258,33880 on map 2'
  check ap_hub_ng_fc "the dungeon door wants its key" '\[ap\] door 68686044 \(Energy Nede -> Cave of Red Crystal\): locked, needs Cave of Red Crystal Key'
  check ap_hub_ng_fc "Fienal wants Magichand" '\[ap\] door 68685691 \(Energy Nede -> Fienal\): locked, needs encounter 524 won \("Defeat Magichand first"\)'
  check ap_hub_ng_fc "the banners fit" '\[ap\] door banners measured: [0-9]+, 0 too wide'
  check ap_hub_ng_fc "the chapter stays 60" 'play: story chapter 60 step 4000'
  hub_ng ap_hub_ng_arlia hubarlia rena 3500 SS_TRACE=1 SS_SWEEP_PLAY="17:T0"
  check ap_hub_ng_arlia "hub New Game at Arlia (Rena's game)" "\\[ap\\] hub New Game: Rena's game, [0-9]+ flags, .*chapter 60 step 4000, disc 2; Arlia Village \\(map 17 at -126, 0, -614, facing 90\\)"
  check ap_hub_ng_arlia "the party at the start" '\[leader\] vblank [0-9]+ map 17 state 0 pos -126,0,-614 '
  check ap_hub_ng_arlia "out to Expel's world map" 'play: control on map 1 after 1 of 1 steps'
  check ap_hub_ng_arlia "the Synard by Arlia" 'world: vehicle 1 \(the Synard\) parked at 51209,-150,17464 on map 1'
  hub_ng ap_hub_ng_linga hublinga claude 3500 SS_TRACE=1 SS_SWEEP_PLAY="183:T27"
  check ap_hub_ng_linga "hub New Game at Linga" "\\[ap\\] hub New Game: Claude's game, [0-9]+ flags, .*; Town of Linga \\(map 183 at -3392, 0, -1182, facing 340\\)"
  check ap_hub_ng_linga "out to Expel's world map" 'play: control on map 1 after 1 of 1 steps'
  check ap_hub_ng_linga "the Synard by Linga" 'world: vehicle 1 \(the Synard\) parked at 90815,-150,13368 on map 1'
  # The character select in a hub game [charselect agent] (starting_character "any", Bowman and
  # Opera excluded; the open world's presses): Celine joins at the hub's first free play.
  HUB_INPUT="$NEWGAME_A,$CS_PICK,$NEWGAME_B3" hub_ng ap_hub_ng_cs hubany claude 3800 SS_TRACE=1 SS_LEADER_TRACE=1
  check ap_hub_ng_cs "the character select in a hub game" '\[ap\] character select opens: 10 of 12 to pick'
  check ap_hub_ng_cs "Celine picked" '\[ap\] starting character Celine: picked at New Game'
  check ap_hub_ng_cs "hub New Game at Fun City" "\\[ap\\] hub New Game: Claude's game, [0-9]+ flags, .*; Fun City \\(map 279 at 470, 0, 68, facing 180\\)"
  check ap_hub_ng_cs "she joins at the first free play and leads" '\[ap\] Celine leads \(Formation leader, slot 1\)'
  check ap_hub_ng_cs "and walks Fun City in the hero's place (the leader stand-in)" '\[standin\] vblank [0-9]+ map 279 shows 2 \(stand-in'
  # The leader stand-in in hub mode [hubleader agent] (field/leader.h; open world and hub): a named
  # starting character (Ashton: the select to confirm) walks Fun City in Claude's place and the
  # door out leads to the world map, where Claude shows (the world map keeps the hero). Then
  # Alen-Tax's chamber (the --map approach 68 at the hub's state, its door into 61): the original
  # scene through its story view with Ashton walking in and named where it names Claude (the
  # speaker label and the lines), until the scene's own animation of Claude puts him back for the
  # rest of the scene; the battle is won as without the stand-in.
  HUB_INPUT="$NEWGAME_A,2170:cross,2176:none,$NEWGAME_B3" hub_ng ap_hub_leader_ng hubashton claude 3400 SS_TRACE=1 \
    SS_LEADER_TRACE=1 SS_SWEEP_PLAY="279:T4"
  check ap_hub_leader_ng "Ashton confirmed at New Game" '\[ap\] character select: Ashton Anchors$'
  check ap_hub_leader_ng "he joins and leads" '\[ap\] Ashton leads \(Formation leader, slot 1\)'
  check ap_hub_leader_ng "Ashton walks Fun City in Claude's place" '\[standin\] vblank [0-9]+ map 279 shows 6 \(stand-in'
  check ap_hub_leader_ng "out to the world map" 'field_load_map\(map=2 '
  env SS_DISC2="$D2" SS_MODS_DIR="$WORK/no-mods" SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy SS_HUB_STATE=1 SS_TRACE=1 \
    SS_EVENT_SWEEP=1 SS_BATTLE_STUB=1 SS_LEADER_TRACE=1 SS_SWEEP_PLAY="68:T6" timeout 900 "$BIN" --headless --no-memcard \
    --ap-seed "$WORK/hubashton.apso2" --frames 3300 --map 68 </dev/null >"$WORK/ap_hub_leader_alentax.log" 2>&1
  check ap_hub_leader_alentax "Alen-Tax's view" '\[ap\] hub: story view on: map 61 \(Alen-Tax, encounter 191\)'
  check ap_hub_leader_alentax "Ashton walks into the scene" '\[standin\] vblank [0-9]+ map 61 shows 6 \(stand-in\)'
  check ap_hub_leader_alentax "the scene names him where it names Claude" '\[standin\] vblank [0-9]+ map 61 message rewritten 1 6'
  check ap_hub_leader_alentax "Claude's own scene animation: Claude for the rest" '\[standin\] vblank [0-9]+ map 61 shows 0 \(scene animation\)'
  check ap_hub_leader_alentax "the fight, won" '\[ap\] goal: encounter 191 won'
  hub_ng ap_hub_key hubkey claude 3500 SS_TRACE=1 SS_SWEEP_PLAY="279:T4,2:T21"
  check ap_hub_key "with the key the cave loads" 'field_load_map\(map=419 '
  # (the win between the field's start, ~2714 since New Game skips the hero screens, and the walk)
  hub_ng ap_hub_fienal hubfc claude 3700 SS_TRACE=1 SS_BATTLE_STUB=1 SS_DEBUG_INPUT="2760:battle 524" SS_SWEEP_PLAY="279:T4,2:T10"
  check ap_hub_fienal "Magichand won" '\[ap\] goal: encounter 524 won'
  check ap_hub_fienal "then Fienal opens" 'field_load_map\(map=687 '
  hub_ng ap_hub_arena hubfc claude 9000 SS_BATTLE_STUB=1 SS_SWEEP_PLAY="279:T0,280:T12,281:T3,297:T4,299:T2,304:T4,305:K16:2,296:T2,304:T4"
  check ap_hub_arena "the antechamber's door leads to the arena" '\[ap\] door 19929509: map 296 \(-6, 0, 1014\) -> map 305 \(-13, 0, 276\)'
  check ap_hub_arena "Marsilio's view" '\[ap\] hub: story view on: map 305 \(Marsilio, encounter 532\): chapter 57 step 20'
  check ap_hub_arena "Go see Narl: the fight, won" '\[ap\] goal: encounter 532 won'
  check ap_hub_arena "the scene's way out ends the view" '\[ap\] hub: story view off: map 296 \(Marsilio, encounter 532; map 296 loaded\)'
  check ap_hub_arena "after the win the door is the original" '\[ap\] door 19929509: encounter 532 won, the door.s own target \(map 296\)'
  # The two chambers that ask first (the user's rulings: Shigeo 281, the arena 305) [hubask agent]
  # (docs/ap/seed_format.md "Hub", `ask`). Shigeo: the door 280 -> 281 asks; yes plays his scene
  # and fight, no leaves 281 at the hub's state: the route on to Expel (297, 299, the arena's old
  # man 303 -> Virtual Arlia 18) and a real walk across the street (the strip fires, nothing runs).
  # The arena: the desk's third option "Face Marsilio." (the view on; the Clerk's fourth option
  # "Leave the arena." added; "Go see Narl" the fight), its "Register." the normal arena (no view,
  # its battles, back to 304), the antechamber's door (yes: the arena; the Clerk's way out, then
  # no: the door's own target 296), a save in Marsilio's view loading with it.
  ask_none() {  # ask_none NAME DESCRIPTION REGEX: no line may match
    if grep -qE "$3" "$WORK/$1.log"; then echo "FAIL $1: $2 (a line matches: $3)"; fail=1; else echo "PASS $1: $2"; fi
  }
  hub_ng ap_hub_ask_yes hubfc claude 9000 SS_TRACE=1 SS_BATTLE_STUB=1 SS_SWEEP_PLAY="279:T0,280:T12,281:T0"
  check ap_hub_ask_yes "the door into 281 asks first" '\[ap\] hub: ask: Shigeo, encounter 533: door 18356813 \(map 280 -> map 281\): "Shigeo is on the rampage ahead. / Face Shigeo now\?"'
  check ap_hub_ask_yes "yes: the view" '\[ap\] hub: ask: Shigeo, encounter 533: yes: map 281 with its story view'
  check ap_hub_ask_yes "his scene's view comes on" '\[ap\] hub: story view on: map 281 \(Shigeo, encounter 533\): chapter 57 step 2000'
  check ap_hub_ask_yes "the fight, won" '\[ap\] goal: encounter 533 won'
  check ap_hub_ask_yes "the view ends in place" '\[ap\] hub: story view off: map 281 \(Shigeo, encounter 533; free play after the battle\)'
  hub_ng ap_hub_ask_no hubfc claude 6000 SS_TRACE=1 SS_BATTLE_STUB=1 SS_SWEEP_PLAY="279:T0,280:T12:1,281:T3,297:T4,299:T6,303:K25"
  check ap_hub_ask_no "no: 281 at the hub's state" '\[ap\] hub: chamber map 281 \(Shigeo, encounter 533\): no story view \(the answer was no\)'
  check ap_hub_ask_no "on to Expel (Virtual Arlia)" 'field_load_map\(map=18 '
  ask_none ap_hub_ask_no "no view, no fight" 'story view on: map 281|a battle at'
  env SS_DISC2="$D2" SS_MODS_DIR="$WORK/no-mods" SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy SS_HUB_STATE=1 SS_TRACE=1 \
    timeout 900 "$BIN" --headless --no-memcard --ap-seed "$WORK/hubfc.apso2" --frames 1300 --map 281 --map-pos -3795,0,-3406,2048 \
    --input "60:up,640:right,720:up" </dev/null >"$WORK/ap_hub_ask_walk.log" 2>&1
  check ap_hub_ask_walk "walking across 281: the strip fires" 'trigger 0 fires \(code word 8535\)'
  check ap_hub_ask_walk "and the party reaches the arena's hall 297" 'field_load_map\(map=297 '
  ask_none ap_hub_ask_walk "no scene, no fight" 'story view on|a battle at'
  hub_ng ap_hub_ask_desk hubfc claude 9500 SS_TRACE=1 SS_BATTLE_STUB=1 SS_SWEEP_PLAY="279:T0,280:T12:1,281:T3,297:K16:2,305:K16:2"
  check ap_hub_ask_desk "the desk's menu gets Face Marsilio" '\[ap\] hub: ask: Marsilio, encounter 532: map 297.s choice at 0x5c6c: option 2 "Face Marsilio." added'
  check ap_hub_ask_desk "chosen: into the arena with the view" '"Face Marsilio." chosen: to map 305 \(-13, 0, 276\), with its story view'
  check ap_hub_ask_desk "Marsilio's view" '\[ap\] hub: story view on: map 305 \(Marsilio, encounter 532\): chapter 57 step 20'
  check ap_hub_ask_desk "the Clerk's menu gets the way out" 'map 305.s choice at 0x6ec4: option 3 "Leave the arena." added'
  check ap_hub_ask_desk "Go see Narl: the fight, won" '\[ap\] goal: encounter 532 won'
  check ap_hub_ask_desk "the scene's way out ends the view" '\[ap\] hub: story view off: map 296 \(Marsilio, encounter 532; map 296 loaded\)'
  hub_ng ap_hub_ask_register hubfol claude 9000 SS_TRACE=1 SS_BATTLE_STUB=1 SS_SWEEP_PLAY="279:T0,280:T12:1,281:T3,297:K16:1/0/0"
  check ap_hub_ask_register "Register: the arena with no view" '\[ap\] hub: chamber map 305 \(Marsilio, encounter 532\): no story view'
  check ap_hub_ask_register "a ranking battle starts" 'a battle at vblank'
  check ap_hub_ask_register "and the arena's own way back to 304" 'field_load_map\(map=304 '
  ask_none ap_hub_ask_register "no Marsilio" 'story view on: map 305|encounter 532 won'
  hub_ng ap_hub_ask_exit hubfc claude 6000 SS_TRACE=1 SS_BATTLE_STUB=1 SS_SWEEP_PLAY="279:T0,280:T12:1,281:T3,297:T4,299:T2,304:T4,305:K16:3,304:T4"
  check ap_hub_ask_exit "the antechamber's door asks" '\[ap\] hub: ask: Marsilio, encounter 532: door 19929509 \(map 304 -> map 305\)'
  check ap_hub_ask_exit "yes: the arena with the view" '\[ap\] hub: story view on: map 305 \(Marsilio, encounter 532\)'
  check ap_hub_ask_exit "Leave the arena: out without the fight" '"Leave the arena." chosen: to map 304 \(1339, 0, -6\), out of the chamber'
  check ap_hub_ask_exit "the view ends at 304" '\[ap\] hub: story view off: map 304 \(Marsilio, encounter 532; map 304 loaded\)'
  check ap_hub_ask_exit "the way out is a no for the visit" '\[ap\] hub: ask: Marsilio, encounter 532: quiet until the party has been on a world map'
  check ap_hub_ask_exit "the door quiet: its own target" '\[ap\] hub: ask: Marsilio, encounter 532: door 19929509 \(map 304 -> map 305\): quiet this visit: the door.s own target \(map 296\)'
  check ap_hub_ask_exit "the lobby 296" 'field_load_map\(map=296 '
  ask_none ap_hub_ask_exit "nothing won" 'encounter 532 won'
  # Once per visit (the user's ruling; `again` "visit") [hubask2 agent]: after Shigeo's no the
  # doors into 281 are quiet (297 -> 281, 280 -> 281: no question, 281 at the hub's state) until a
  # world map: out by 279 to Energy Nede's, back into Fun City, and 280 -> 281 asks again (yes: his
  # view). The arena's door alike: no (the lobby 296), back to 304, the door quiet (296 again).
  hub_ng ap_hub_ask_visit hubfc claude 4800 SS_TRACE=1 SS_BATTLE_STUB=1 \
    SS_SWEEP_PLAY="279:T0,280:T12:1,281:T3,297:T0,281:T5,280:T12,281:T5,280:T0,279:T4,2:T27,279:T0,280:T12"
  check ap_hub_ask_visit "no: quiet for the visit" '\[ap\] hub: ask: Shigeo, encounter 533: quiet until the party has been on a world map'
  check ap_hub_ask_visit "297 -> 281 without the question" '\[ap\] hub: ask: Shigeo, encounter 533: door 19468777 \(map 297 -> map 281\): quiet this visit: the chamber at the hub.s state'
  check ap_hub_ask_visit "280 -> 281 without the question" '\[ap\] hub: ask: Shigeo, encounter 533: door 18356813 \(map 280 -> map 281\): quiet this visit'
  check ap_hub_ask_visit "the world map ends the visit" '\[ap\] hub: ask: Shigeo, encounter 533: the world map \(map 10[0-9][0-9]\): asks again'
  check ap_hub_ask_visit "back in Fun City: yes, his view" '\[ap\] hub: story view on: map 281 \(Shigeo, encounter 533\)'
  n=$(grep -c 'ask: Shigeo, encounter 533: door [0-9]* (map [0-9]* -> map 281): "' "$WORK/ap_hub_ask_visit.log")
  if [ "$n" = 2 ]; then echo "PASS ap_hub_ask_visit: asked twice (the first entry and after the world map)"; else echo "FAIL ap_hub_ask_visit: asked twice (the first entry and after the world map): $n"; fail=1; fi
  hub_ng ap_hub_ask_visit_door hubfc claude 4000 SS_TRACE=1 SS_BATTLE_STUB=1 SS_SWEEP_PLAY="279:T0,280:T12:1,281:T3,297:T4,299:T2,304:T4:1,296:T2,304:T4"
  check ap_hub_ask_visit_door "the arena's door: no, the lobby" '\[ap\] hub: ask: Marsilio, encounter 532: no: door 19929509 goes to its own target \(map 296\)'
  check ap_hub_ask_visit_door "then quiet: the lobby again" '\[ap\] hub: ask: Marsilio, encounter 532: door 19929509 \(map 304 -> map 305\): quiet this visit: the door.s own target \(map 296\)'
  ask_none ap_hub_ask_visit_door "no arena, no view" 'story view on|field_load_map\(map=305 '
  rm -f "$WORK/askcard.mcd" "$WORK"/askcard.slot*.ap.json
  env SS_DISC2="$D2" SS_MODS_DIR="$WORK/no-mods" SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy SS_HUB_STATE=1 SS_TRACE=1 \
    SS_SAVE_ANYWHERE=1 SS_MEMCARD1="$WORK/askcard.mcd" timeout 900 "$BIN" --headless --ap-seed "$WORK/hubfc.apso2" --frames 2000 \
    --map 304 --map-pos 1339,0,-6,2048 --input "60:up,330:none,450:cross,456:none,490:cross,496:none,530:cross,536:none,1000:triangle,1006:none,1100:up,1106:none,1150:cross,1156:none,1250:cross,1256:none,1350:cross,1356:none,1450:cross,1456:none,1550:cross,1556:none" \
    </dev/null >"$WORK/ap_hub_ask_save.log" 2>&1
  check ap_hub_ask_save "walked into the door, yes: Marsilio's view" '\[ap\] hub: story view on: map 305 \(Marsilio, encounter 532\)'
  check ap_hub_ask_save "saved there" '\[ap\] saved .*askcard\.slot01\.ap\.json'
  if grep -q '"hub_view": 532' "$WORK/askcard.slot01.ap.json" 2>/dev/null; then echo "PASS ap_hub_ask_save: the sidecar keeps the view"; else echo "FAIL ap_hub_ask_save: the sidecar keeps the view"; fail=1; fi
  env SS_DISC2="$D2" SS_MODS_DIR="$WORK/no-mods" SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy SS_TRACE=1 SS_LOAD_SAVE=0 \
    SS_MEMCARD1="$WORK/askcard.mcd" SS_EVENT_SWEEP=1 SS_SWEEP_PLAY="305:K16:3" timeout 900 "$BIN" --headless --ap-seed "$WORK/hubfc.apso2" \
    --frames 900 </dev/null >"$WORK/ap_hub_ask_load.log" 2>&1
  check ap_hub_ask_load "the load: the view on again" '\[ap\] hub: ask: Marsilio, encounter 532: the save was made in its story view'
  check ap_hub_ask_load "Marsilio's view" '\[ap\] hub: story view on: map 305 \(Marsilio, encounter 532\)'
  check ap_hub_ask_load "and the Clerk's way out" '"Leave the arena." chosen: to map 304'
else
  echo "SKIP ap_hub_ng: no disc 2 image next to disc 1"
fi

exit $fail
