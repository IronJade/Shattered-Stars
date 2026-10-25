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
# The seed's story_route: New Game keeps the cursor on Claude (Right buzzes) or starts on Rena.
NEWGAME_A="850:start,854:none,951:cross,957:none,1251:cross,1257:none,1551:cross,1557:none,1851:cross,1857:none"
NEWGAME_B="2151:start,2157:none,2451:cross,2457:none,2700:start,2706:none,3000:start,3006:none,3300:start,3306:none"
for hero in claude rena; do
  printf '{"format": "apso2", "version": 1, "seed": "smoke-hero", "slot": {"name": "P", "id": 1}, "options": {"story_route": "%s"}, "items": {}, "locations": []}\n' \
    $hero >"$WORK/hero_$hero.apso2"
  [ $hero = claude ] && PRESS=right || PRESS=left
  "${AP_RUN[@]}" SS_TRACE_LEADER=600 timeout 900 "$BIN" --headless --no-memcard --ap-seed "$WORK/hero_$hero.apso2" --frames 3700 \
    --input "$NEWGAME_A,2000:$PRESS,2006:none,$NEWGAME_B" </dev/null >"$WORK/ap_hero_$hero.log" 2>&1
done
check ap_hero_claude "Claude's seed refuses Rena" '\[ap\] this seed is for Claude.s game'
check ap_hero_claude "and starts Claude's game (map 7)" '\[leader\] vblank 3600 map 7 state 0 '
check ap_hero_rena "Rena's seed refuses Claude" '\[ap\] this seed is for Rena.s game'
check ap_hero_rena "and starts Rena's game (map 18)" '\[leader\] vblank 3600 map 18 state 0 '
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

# Archipelago phase 4 [world agent]: the open world. Map 17's door to Arlia (door 1116181,
# routine 0x204c) needs key 1: walking in shows the banner and nothing transfers; once the key
# comes (console), walking in again loads map 18. With a target, the same door leads to map 23.
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
  --ap-seed "$WORK/p4door.apso2" --frames 800 --map 17 --map-pos 110,0,-457 --input "100:right,200:none,600:left,700:none" \
  </dev/null >"$WORK/ap_p4_door.log" 2>&1
check ap_p4_door "the locked door does not transfer" '\[ap\] door 1116181 \(Arlia exit -> Arlia\): locked, needs Arlia Pass'
check ap_p4_door "the leader walks on" '\[leader\] vblank 180 map 17 state 0 pos 1[6-9][0-9],0,-457 .* control 0 '
check ap_p4_door "the key opens it" 'trigger 9 fires \(code word 2067\) at leader 5[01][0-9],0,11[0-9] at vblank 6[0-9][0-9]'
check ap_p4_door "map 18 loads" 'field_load_map\(map=18 keep=1 flags=0\) at vblank 6[0-9][0-9]'
if grep -q 'field_load_map(map=18 keep=1 flags=0) at vblank [1-5][0-9][0-9]$' "$WORK/ap_p4_door.log"; then echo "FAIL ap_p4_door: transferred while locked"; fail=1; fi
"${AP_RUN[@]}" SS_TRACE=1 timeout 900 "$BIN" --headless --ap-seed "$WORK/p4redirect.apso2" --frames 170 --map 17 \
  --map-pos 110,0,-457 --input "100:right,200:none" </dev/null >"$WORK/ap_p4_redirect.log" 2>&1
check ap_p4_redirect "the door's target is the seed's" '\[ap\] door 1116181: map 18 \(230, 0, -133\) -> map 23 \(224, 0, 106\)'
check ap_p4_redirect "map 23 loads" 'field_load_map\(map=23 keep=1'
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

# Archipelago phase 4 [world agent]: shop shuffle. Hearn's store (map 26, shop 1): entry 0
# (Sandals, item 728) is Alice's; entry 2 (Lavender, item 224) is our own Revival Card. Talk
# to Hearn (up to his counter, confirm the Yes/No prompt), Buy, add 1 Sandal and 3 Lavenders,
# settle: the first copy of each is the check (Sandals sent to Alice, nothing given; Lavender
# gives the Revival Card), the other 2 Lavenders are the shop's own item (menu_shop.cpp settle,
# draw_row colours an unchecked slot's name 2; ap_shop_slots unit-tests the colour logic).
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
"${AP_RUN[@]}" SS_DEBUG_INPUT="50:fol 50000;1620:where;2300:where" timeout 900 "$BIN" --headless --debug \
  --ap-seed "$WORK/p4shop.apso2" --frames 2400 --map 18 \
  --input "200:up,439:none,450:left,663:none,670:up,1000:none,1010:left,1060:none,1070:up,1250:none,1260:cross,1266:none,1300:cross,1306:none,1400:cross,1406:none,1500:cross,1506:none,1560:cross,1566:none,1600:right,1606:none,1650:down,1656:none,1700:down,1706:none,1750:right,1756:none,1800:right,1806:none,1850:right,1856:none,1900:cross,1906:none,1950:cross,1956:none,2100:cross,2106:none" \
  </dev/null >"$WORK/ap_p4_shop.log" 2>&1
check ap_p4_shop "the Buy list opens (shop 1, Hearn's)" '\[debug\] map 26 \| state 3 '
check ap_p4_shop "another slot's item is sent" '\[ap\] check 1365246464 \(Arlia shop: Sandals\): Hookshot for Alice'
check ap_p4_shop "and none of it is given" '\[ap\] shop 1 entry 0 \(item 728\): the slot.s check took the first copy of 1'
check ap_p4_shop "our own slot's first copy is the check" '\[ap\] check 1365246466 \(Arlia shop: Lavender\): Revival Card'
check ap_p4_shop "the other 2 copies are the shop's own item" '\[ap\] shop 1 entry 2 \(item 224\): the slot.s check took the first copy of 3'
check ap_p4_shop "Fol paid for all 4 (1x10 + 3x490)" '\[debug\] map 26 \| state 0 .* Fol 48520 '

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
  cp "$SAVE" "$WORK/world.mcd"
  printf '{"format": "apso2-save", "version": 1, "seed": "smoke-1", "slot_name": "Claude", "received_index": 3, "start_given": true, "checked": [1347569665]}\n' \
    >"$WORK/world.slot01.ap.json"
  SS_MODS_DIR="$WORK/no-mods" SS_LOAD_SAVE=0 SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy \
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
    env "$@" SS_MODS_DIR="$WORK/no-mods" SS_LOAD_SAVE=0 SS_TRACE_LEADER=10 SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy \
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
  SS_MODS_DIR="$WORK/no-mods" SS_LOAD_SAVE=0 SS_TRACE=1 SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy SS_MEMCARD1="$WORK/world.mcd" \
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

exit $fail
