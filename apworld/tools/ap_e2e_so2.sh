#!/bin/bash
# The end-to-end Archipelago check with a real Star Ocean slot: the SO2 APWorld (apworld/so2)
# generates a multiworld of an SO2 player (Claude) and a ChecksFinder player (Bob), a private
# MultiServer hosts it on 127.0.0.1:AP_E2E_PORT (default 38292; ap_e2e.sh uses 38291), and the
# port plays the SO2 slot headless. Nothing here talks to a public server.
#
#   SS_DISC1=DISC1.cue apworld/tools/ap_e2e_so2.sh [BINARY]   (apworld/tools/ap_test_server.sh start, once, sets up)
#
# Claude plays with the phase-3 options character_items, enemy_dropsanity (first_drop) and
# traps (20 %), and the phase-4 ones world_mode open_world and shuffle_shops full. Item plando
# fixes the cross-world placements, so the run is the same every time:
#   - Arlia's 200-Fol crate (map 18, flag 0x962; "Arlia Village: 200 Fol chest") holds Bob's
#     Map Width;
#   - Hearn's shop slot 2 (map 26, shop 1, Lavender; "Arlia Village: shop 1 Lavender") holds
#     Claude's own Spectacles (shop slots never hold progression, and every ChecksFinder item
#     is progression);
#   - Bob's Tiles 1, 2, 4 and 10 hold Claude's Resurrection Bottle, Salva Pass (an area key),
#     Celine (a character item) and a Fol Loss Trap.
# In one scripted run:
#   1. Generate.py (seed AP_E2E_SEED, default 4242) writes the multiworld and Claude's .apso2.
#   2. The port (paced: SS_PACE; scratch config, no memory card) connects as Claude, starts on
#      map 18 and opens the crate: the check goes out, the window says "Sent Map Width to
#      Bob." and no Fol is paid.
#   3. Bob (a Python client) receives Map Width from the crate's location, then checks Tiles 1,
#      2, 4 and 10: in free play the port gives the Resurrection Bottle and the Salva Pass (the
#      key Salva's world-map entrances need), Celine joins the party, and the trap takes 10 % of
#      the 10000 Fol the console gave (`fol 10000`), each with its banner.
#   4. The console starts a random encounter that drops (`battle 110`, two Kitties, enemy type
#      90; this run's draws drop the Mithril Shield); --battle-end wins it after 60 frames, and
#      the result screen's first drop of type 90 is the check "Kitty first drop" instead.
#   5. The console starts the final boss (`battle 539`), won the same way: the battle's exit
#      records the goal (CLIENT_GOAL).
#   6. A second session (a new game, no memory card) walks into Hearn's shop and buys one
#      Sandals and three Lavenders: the Lavender slot's first purchase is also the check and
#      gives the Spectacles; every copy bought is the player's.
#   7. A third session plays Dias, a second SO2 slot with a version-2 seed (so2.apworld 0.2.0:
#      goal indalecio_limiter_off_and_iseria_queen, craftsanity, killer-move checks, no-fail
#      crafting; story mode): goal.flags are set at the start; the console's final boss (`battle
#      540`) is only one of goal.all's two wins (no goal yet); the Iseria Queen (`battle 561`)
#      completes it and the goal goes out. Auto-advance (SS_AUTO_ADVANCE) moves the result
#      screens on.
#   7b. A check made offline and saved reaches the server when that save is continued while
#      connected [loadsend agent] (the Windows recheck of 0.03.00, item 8: a game connects at the
#      title, before Continue, and the connect sent only the session's checks). Session 5, offline
#      (no server, a scratch card): the console checks Arlia's Resurrection Bottle chest, the camp
#      menu saves in file 1 (SS_SAVE_ANYWHERE). Session 6, connected, from the title: Continue,
#      slot 1, file 1, Yes; the load sends the save's checks. Both run before session 4, whose
#      map key must be the last one written (step 9).
#   8. After the port has quit, a tracker client logs in to Claude's slot and reads the
#      server's view: the crate, the drop, the shop slot and session 5's chest among the checked
#      locations, and the client status 30 (goal); another reads Dias's client status (30).
#   9. The tracker keys in data storage [dskeys agent] (docs/apworld/data_storage.md): a tracker
#      login to Claude's slot (apworld/tools/ap_ds_read.py --watch: SetNotify) watches so2_map_T_S and
#      so2_story_T_S from before session 1 to the end; session 2's walk shows map 18 then Hearn's
#      store (26); a fourth session (Claude, a new game on world map 1 at a walkable spot) walks,
#      and the map key carries x / z that change, at most every 30 vblanks. After the port has
#      quit, the reader gets both keys of Claude and of Dias (Get) under the names the server's
#      Connected gives (team, slot), the port's log naming the same keys.
# The server runs with release / collect off, so the checked locations are exactly what was
# sent. Output, logs and captures (frames 700: the window, 850: the first banner) in
# scratchpad/ap_e2e_so2/ (AP_E2E_WORK to put them elsewhere). Every game run is under the
# memory cap (tools/memcap.py: SS_MEM_CAP_GB, default 4 GB); a run that dies at it fails.
# Exit status 0 when every step is seen.
set -u
cd "$(dirname "$0")/../.."
P=$PWD
BIN=${1:-$P/port/build/shattered-stars}
: "${SS_DISC1:?set SS_DISC1 to disc 1 (.cue)}"
W=${AP_E2E_WORK:-$P/scratchpad/ap_e2e_so2}
case "$W" in "$P"/scratchpad/?*) ;; *) echo "AP_E2E_WORK must be a directory under $P/scratchpad" >&2; exit 1 ;; esac
AP=$P/scratchpad/ap
PORT=${AP_E2E_PORT:-38292}
SEED=${AP_E2E_SEED:-4242}
CRATE=1347569665   # Arlia Village: 200 Fol chest (docs/apworld/field.json)
TILE1=81000        # ChecksFinder's Tile 1 (Tile n: 81000 + n - 1)
DROP=1362231386    # Kitty first drop: 0x50320000 + (8 << 21) + enemy type 90
SHOP=1366425858    # Hearn's shop slot 2: 0x50320000 + (10 << 21) + (shop 1 << 8) + 2
OFFLINE=1347569666 # Arlia Village: Resurrection Bottle chest (map 18, flag 0x963): session 5's offline check
export SKIP_REQUIREMENTS_UPDATE=1
if [ ! -x "$AP/ap-venv/bin/python" ]; then
  echo "no Archipelago install: run apworld/tools/ap_test_server.sh start (then stop) once" >&2
  exit 1
fi
# Check the port before touching the work directory: a busy port may be another run's server.
if (exec 3<>"/dev/tcp/127.0.0.1/$PORT") 2>/dev/null; then
  echo "port $PORT is in use (AP_E2E_PORT picks another)" >&2
  exit 1
fi
rm -rf "$W"
mkdir -p "$W/xdg" "$W/nomods" "$W/players" "$W/out"
ln -sfn "$P/apworld/so2" "$AP/Archipelago/worlds/so2"

cat > "$W/players/Claude.yaml" <<'EOF'
name: Claude
game: "Star Ocean: The Second Story"
"Star Ocean: The Second Story":
  story_route: claude
  character_items: true
  enemy_dropsanity: first_drop
  traps: 20
  world_mode: open_world
  shuffle_shops: full
  plando_items:
    - items: {"Resurrection Bottle": 1}
      locations: ["Tile 1"]
      world: Bob
    - items: {"Salva Pass": 1}
      locations: ["Tile 2"]
      world: Bob
    - items: {"Blueberry": 1}
      locations: ["Kitty first drop"]
      world: Claude
    - items: {"Spectacles": 1}
      locations: ["Arlia Village: shop 1 Lavender"]
      world: Claude
    - items: {"Celine": 1}
      locations: ["Tile 4"]
      world: Bob
    - items: {"Fol Loss Trap": 1}
      locations: ["Tile 10"]
      world: Bob
EOF
cat > "$W/players/Dias.yaml" <<'EOF'
name: Dias
game: "Star Ocean: The Second Story"
"Star Ocean: The Second Story":
  story_route: claude
  goal: indalecio_limiter_off_and_iseria_queen
  craftsanity: true
  killer_move_checks: true
  no_fail_crafting: true
EOF
cat > "$W/players/Bob.yaml" <<'EOF'
name: Bob
game: ChecksFinder
ChecksFinder:
  plando_items:
    - items: {"Map Width": 1}
      locations: ["Arlia Village: 200 Fol chest"]
      world: Claude
EOF
(cd "$AP/Archipelago" && "$AP/ap-venv/bin/python" Generate.py --player_files_path "$W/players" --outputpath "$W/out" \
   --seed "$SEED" --plando "bosses, items, connections, texts" --spoiler 2) < /dev/null > "$W/generate.log" 2>&1
zip=$(ls "$W"/out/AP_*.zip 2>/dev/null | head -1)
if [ -z "$zip" ]; then
  echo "generation failed: see $W/generate.log" >&2
  exit 1
fi
(cd "$W/out" && unzip -o -q "$zip")
seed_file=$(ls "$W"/out/*_Claude.apso2 | head -1)
seed_v2=$(ls "$W"/out/*_Dias.apso2 | head -1)

cat > "$W/bob.py" <<'BOB'
# Bob: connects, waits until Claude's crate sends him Map Width, then checks Tiles 1, 2, 4 and
# 10 (Claude's Resurrection Bottle, Salva Pass, Celine, a Fol Loss Trap). Stays on to see
# Claude's goal.
import asyncio, json, sys, time, websockets
CRATE, TILE1 = int(sys.argv[3]), int(sys.argv[4])
async def main():
    t0 = time.time()
    log = lambda *a: print(f"[bob {time.time() - t0:6.2f}]", *a, flush=True)
    async with websockets.connect(sys.argv[2], max_size=None) as ws:
        await ws.recv()  # RoomInfo
        await ws.send(json.dumps([{"cmd": "Connect", "game": "ChecksFinder", "name": "Bob", "password": "",
                                   "uuid": "bob-ap-e2e-so2", "version": {"major": 0, "minor": 6, "build": 8, "class": "Version"},
                                   "items_handling": 0b001, "tags": [], "slot_data": False}]))
        got = False
        deadline = time.time() + float(sys.argv[1])
        while time.time() < deadline:
            try:
                msg = await asyncio.wait_for(ws.recv(), timeout=1.0)
            except asyncio.TimeoutError:
                continue
            for p in json.loads(msg):
                if p["cmd"] == "Connected":
                    log("connected as Bob, slot", p["slot"])
                elif p["cmd"] == "ReceivedItems":
                    for it in p["items"]:
                        log(f"received item {it['item']} from player {it['player']} location {it['location']}")
                        if (it["item"], it["location"]) == (80000, CRATE) and not got:
                            got = True
                            await ws.send(json.dumps([{"cmd": "LocationChecks",
                                                       "locations": [TILE1, TILE1 + 1, TILE1 + 3, TILE1 + 9]}]))
                            log("checked Tiles 1, 2, 4 and 10")
                elif p["cmd"] == "PrintJSON" and p.get("type") == "Goal":
                    log("goal announced for player", p.get("slot"))
                    deadline = min(deadline, time.time() + 3)
        log("done; Map Width from the crate:", got)
asyncio.run(main())
BOB

cat > "$W/tracker.py" <<'TRACKER'
# The server's view of Claude's slot: a tracker login (no items) reads the checked locations
# from Connected and the client status from the read-only data storage.
import asyncio, json, sys, websockets
async def main():
    name = sys.argv[2] if len(sys.argv) > 2 else "Claude"
    async with websockets.connect(sys.argv[1], max_size=None) as ws:
        await ws.recv()  # RoomInfo
        await ws.send(json.dumps([{"cmd": "Connect", "game": "Star Ocean: The Second Story", "name": name,
                                   "password": "", "uuid": "tracker-ap-e2e-so2",
                                   "version": {"major": 0, "minor": 6, "build": 8, "class": "Version"},
                                   "items_handling": 0, "tags": ["Tracker"], "slot_data": False}]))
        team = slot = None
        while True:
            for p in json.loads(await asyncio.wait_for(ws.recv(), timeout=10)):
                if p["cmd"] == "Connected":
                    team, slot = p["team"], p["slot"]
                    print("checked_locations", sorted(p["checked_locations"]))
                    print("missing_locations", len(p["missing_locations"]))
                    await ws.send(json.dumps([{"cmd": "Get", "keys": [f"_read_client_status_{team}_{slot}"]}]))
                elif p["cmd"] == "ConnectionRefused":
                    print("refused", p.get("errors"))
                    return
                elif p["cmd"] == "Retrieved":
                    print("client_status", p["keys"].get(f"_read_client_status_{team}_{slot}"))
                    return
asyncio.run(main())
TRACKER

# The port under the memory cap (tools/memcap.py), with the environment the caller sets:
# run_game LOG TIMEOUT ARGS... runs "$BIN" ARGS (no stdin), output to LOG, killed after TIMEOUT
# seconds; a line per run goes to $W/memcap.txt ("at_cap True" when it died at the cap).
run_game() {
  python3 - "$BIN" "$@" <<'PY' >> "$W/memcap.txt"
import os, subprocess, sys
sys.path.insert(0, os.path.join(os.getcwd(), "tools"))
from memcap import cap_bytes, hit_cap, run_capped
binary, log, timeout, args = sys.argv[1], sys.argv[2], float(sys.argv[3]), sys.argv[4:]
with open(log, "w") as f:
    rc, timed_out, at_cap = run_capped([binary] + args, binary, timeout, stdin=subprocess.DEVNULL,
                                       stdout=f, stderr=subprocess.STDOUT)
text = open(log, errors="replace").read()
at_cap = at_cap or (not timed_out and hit_cap(rc, text, killed=timed_out))
print(f"{os.path.basename(log)} rc {rc} timed_out {timed_out} at_cap {at_cap} cap_mb {cap_bytes() >> 20}")
PY
}
: > "$W/memcap.txt"

# The server in its own session and process group (MultiServer starts a child process).
(cd "$AP/Archipelago" && exec setsid "$AP/ap-venv/bin/python" MultiServer.py "$zip" --host 127.0.0.1 \
   --port "$PORT" --disable_save --loglevel info --release_mode disabled --collect_mode disabled) \
   > "$W/server.log" 2>&1 < /dev/null &
server=$!
for _ in $(seq 1 300); do
  (exec 3<>"/dev/tcp/127.0.0.1/$PORT") 2>/dev/null && break
  sleep 0.1
done
"$AP/ap-venv/bin/python" "$W/bob.py" 260 "ws://localhost:$PORT" "$CRATE" "$TILE1" > "$W/bob.log" 2>&1 &
bob=$!
# 600-766: open the crate and close its window; 730: 10000 Fol for the trap to take; from 850:
# the received items' banners; 1600: the drop battle, 4400: the final boss (each won after 60
# battle frames), each followed by the result screen's pages.
# [dskeys agent] The tracker keys: watched from here to after session 4 (step 9).
"$AP/ap-venv/bin/python" "$P/apworld/tools/ap_ds_read.py" "ws://localhost:$PORT" Claude --watch 900 --done "$W/ds_done" \
  > "$W/ds_watch.log" 2>&1 < /dev/null &
dswatch=$!
presses=""
for v in 600 760 1100 $(seq 2200 200 4000) $(seq 5000 200 7400); do presses="$presses,$v:cross,$((v + 6)):none"; done
XDG_CONFIG_HOME="$W/xdg" SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy SS_PACE=1 \
  SS_AP_SERVER="ws://localhost:$PORT" SS_AP_SLOT=Claude SS_MODS_DIR="$W/nomods" \
  SS_TRACE_LEADER=60 SS_DEBUG_INPUT="730:fol 10000;1500:ap_status;1600:battle 110;4400:battle 539;7900:ap_status" \
  run_game "$W/game.log" 600 --headless --debug --no-memcard --ap-seed "$seed_file" --frames 8000 --map 18 \
  --battle-end 60:0 \
  --input "200:up,439:none,450:left,554:none,560:up,572:none$presses" \
  --capture "$W/cap" --capture-frames 700,850
wait $bob
# Session 2: Hearn's shop (the walk and purchases of smoke_field_events.sh's ap_p4_shop, 1200
# frames later, after the banners of the four items the server sends again to the new game):
# one Sandals (slot 0) and three Lavenders (slot 2, Claude's Spectacles).
walk="200:up,439:none,450:left,663:none,670:up,1000:none,1010:left,1060:none,1070:up,1250:none,1260:cross,1266:none,1300:cross,1306:none,1400:cross,1406:none,1500:cross,1506:none,1560:cross,1566:none,1600:right,1606:none,1650:down,1656:none,1700:down,1706:none,1750:right,1756:none,1800:right,1806:none,1850:right,1856:none,1900:cross,1906:none,1950:cross,1956:none,2100:cross,2106:none"
walk=$(echo "$walk" | tr ',' '\n' | awk -F: '{printf "%s%d:%s", (NR > 1 ? "," : ""), $1 + 1200, $2}')
XDG_CONFIG_HOME="$W/xdg" SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy SS_PACE=1 \
  SS_AP_SERVER="ws://localhost:$PORT" SS_AP_SLOT=Claude SS_MODS_DIR="$W/nomods" \
  SS_DEBUG_INPUT="50:fol 50000;3500:ap_status" \
  run_game "$W/game2.log" 300 --headless --debug --no-memcard --ap-seed "$seed_file" --frames 3700 --map 18 \
  --input "$walk"
# Session 3: Dias's version-2 seed (goal.all 561 + 540): 540 first (one of two), then 561.
XDG_CONFIG_HOME="$W/xdg" SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy SS_PACE=1 SS_AUTO_ADVANCE=1000 \
  SS_AP_SERVER="ws://localhost:$PORT" SS_AP_SLOT=Dias SS_MODS_DIR="$W/nomods" \
  SS_DEBUG_INPUT="300:battle 540;2100:ap_status;2200:battle 561;4000:ap_status" \
  run_game "$W/game3.log" 300 --headless --debug --no-memcard --ap-seed "$seed_v2" --frames 4100 --map 18 \
  --battle-end 60:0
# Sessions 5 and 6 [loadsend agent] (step 7b): offline, Claude's chest checked and saved in file 1
# (smoke_field_events.sh's ap_save input: camp menu, Data > Save > slot 1 > #1 > Yes)...
XDG_CONFIG_HOME="$W/xdg" SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy SS_MODS_DIR="$W/nomods" \
  SS_MEMCARD1="$W/offline.mcd" SS_SAVE_ANYWHERE=1 SS_DEBUG_INPUT="200:ap_check $OFFLINE" \
  run_game "$W/game5.log" 300 --headless --debug --ap-seed "$seed_file" --frames 1100 --map 18 \
  --input "300:triangle,306:none,400:up,406:none,450:cross,456:none,550:cross,556:none,650:cross,656:none,750:cross,756:none,850:cross,856:none"
# ... then connected from the title: triangle skips the opening movie (and does nothing on the title
# menu), then Continue (the cursor's place with a save on the card), slot 1, file 1, Yes, each
# 300 frames apart; the field runs on for 10 s.
XDG_CONFIG_HOME="$W/xdg" SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy SS_PACE=1 \
  SS_AP_SERVER="ws://localhost:$PORT" SS_AP_SLOT=Claude SS_MODS_DIR="$W/nomods" SS_MEMCARD1="$W/offline.mcd" \
  run_game "$W/game6.log" 300 --headless --ap-seed "$seed_file" --frames 3100 \
  --input "600:triangle,606:none,700:triangle,706:none,800:triangle,806:none,900:triangle,906:none,1000:triangle,1006:none,1100:triangle,1106:none,1200:triangle,1206:none,1300:triangle,1306:none,1400:triangle,1406:none,1600:cross,1606:none,1900:cross,1906:none,2200:cross,2206:none,2500:cross,2506:none"
# Session 4 [dskeys agent]: Claude on world map 1 (a walkable spot west of Arlia), walking: the
# map key with the party's x / z (step 9).
XDG_CONFIG_HOME="$W/xdg" SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy SS_PACE=1 \
  SS_AP_SERVER="ws://localhost:$PORT" SS_AP_SLOT=Claude SS_MODS_DIR="$W/nomods" SS_TRACE_LEADER=60 \
  run_game "$W/game4.log" 120 --headless --no-memcard --ap-seed "$seed_file" --frames 1500 --map 1 \
  --map-pos 51200,0,18500 --input "600:up,900:left,1200:none"
touch "$W/ds_done"
wait $dswatch
"$AP/ap-venv/bin/python" "$P/apworld/tools/ap_ds_read.py" "ws://localhost:$PORT" Claude > "$W/ds_claude.log" 2>&1 < /dev/null
"$AP/ap-venv/bin/python" "$P/apworld/tools/ap_ds_read.py" "ws://localhost:$PORT" Dias > "$W/ds_dias.log" 2>&1 < /dev/null
"$AP/ap-venv/bin/python" "$W/tracker.py" "ws://localhost:$PORT" > "$W/tracker.log" 2>&1
"$AP/ap-venv/bin/python" "$W/tracker.py" "ws://localhost:$PORT" Dias > "$W/tracker3.log" 2>&1
kill -- "-$server" 2>/dev/null || kill "$server" 2>/dev/null

fails=0
check() {  # FILE what pattern
  if grep -qE "$3" "$1"; then echo "PASS $2"; else echo "FAIL $2"; fails=$((fails + 1)); fi
}
check_not() {  # FILE what pattern (must not appear)
  if grep -qE "$3" "$1"; then echo "FAIL $2"; fails=$((fails + 1)); else echo "PASS $2"; fi
}
check "$W/generate.log" "the multiworld generates (SO2 + ChecksFinder)" 'Creating final archive'
if [ "$(grep -cE '^game[2-6]?\.log rc .* cap_mb [1-9]' "$W/memcap.txt")" = 6 ]; then
  echo "PASS the six game runs ran under the memory cap"
else
  echo "FAIL the six game runs ran under the memory cap"; fails=$((fails + 1))
fi
check_not "$W/memcap.txt" "... and none died at it" 'at_cap True'
check "$seed_file" "the crate holds Bob's Map Width" '"name": "Map Width"'
check_not "$W/generate.log" "plando found every item in the pool" 'Could not remove'
check "$seed_file" "the seed's party holds Celine (character_items)" '"Celine"'
check "$seed_file" "open world: the seed locks Salva's entrances" '"name": "Expel -> Town of Salva"'
check "$seed_file" "shop slots are locations (shuffle_shops full)" '"name": "Arlia Village: shop 1 Lavender"'
check "$W/game.log" "the port connects as Claude" '\[ap\] connected \(slot [0-9]+\)'
check "$W/game.log" "the crate's check" "\[ap\] check $CRATE .*: Map Width for Bob"
check "$W/game.log" "no Fol from the crate" '\[leader\] vblank 720 map 18 .* fol 0$'
check "$W/bob.log" "Bob receives Map Width from the crate" "received item 80000 from player [0-9]+ location $CRATE"
check "$W/game.log" "Bob's Tile 1 gives the Resurrection Bottle" '\[ap\] given Resurrection Bottle'
check "$W/game.log" "Bob's Tile 2 gives the Salva Pass (a key)" '\[ap\] key 2 \(Salva Pass\) received'
check "$W/game.log" "Bob's Tile 4: Celine joins the party" '\[ap\] Celine joins the party \(slot [0-9]+\)'
check "$W/game.log" "Bob's Tile 10: the Fol Loss Trap takes 1000 Fol" '\[ap\] trap fol_loss: lost 1000 Fol'
check "$W/game.log" "the first drop of Kitty is withheld" '\[ap\] first drop of enemy type 90 \(item [0-9]+\): the check instead'
check "$W/game.log" "the drop's check" "\[ap\] check $DROP "
check "$W/game.log" "the goal (encounter 539 won)" '\[ap\] goal: encounter 539 won'
check "$W/game.log" "the port's status" 'slot Claude \| checks 2/[0-9]+ \| received 4 \| waiting 0 \| connected \| goal'
check "$W/game2.log" "session 2: the shop slot's check pays the Spectacles" "\[ap\] check $SHOP .*: Spectacles$"
check "$W/game2.log" "... and the 3 Lavenders bought are ours" '\[ap\] shop 1 entry 2 \(item 224\): 3 bought, and the slot.s check'
check "$W/server.log" "the server: Claude completed the goal" 'Claude.*has completed their goal'
check "$W/tracker.log" "the server's checked locations hold the crate" "checked_locations \[.*\b$CRATE\b"
check "$W/tracker.log" "... and the drop" "checked_locations \[.*\b$DROP\b"
check "$W/tracker.log" "... and the shop slot" "checked_locations \[.*\b$SHOP\b"
check "$W/tracker.log" "the server's client status is 30 (goal)" '^client_status 30$'
check "$seed_v2" "Dias's seed is version 2 (goal.all)" '"version": 2'
if python3 -c 'import json, sys; sys.exit(json.load(open(sys.argv[1]))["goal"].get("all") != [561, 540])' "$seed_v2"; then
  echo "PASS ... with goal.all 561 and 540"
else
  echo "FAIL ... with goal.all 561 and 540"; fails=$((fails + 1))
fi
check "$W/game3.log" "session 3: the port connects as Dias" '\[ap\] connected \(slot [0-9]+\)'
check "$W/game3.log" "... goal.flags set at the start" '\[ap\] goal flags set \(2\)'
check "$W/game3.log" "... 540 is one of goal.all's two wins" '\[ap\] goal: encounter 540 won \(goal_won 1, goal.all 2\)'
check "$W/game3.log" "... no goal yet" 'slot Dias \| checks 0/[0-9]+ .*\| connected$'
check "$W/game3.log" "... 561 completes goal.all" '\[ap\] goal: every encounter of goal.all won'
check "$W/game3.log" "... the port's status" 'slot Dias \| checks 0/[0-9]+ .*\| connected \| goal'
check "$W/server.log" "the server: Dias completed the goal" 'Dias.*has completed their goal'
check "$W/tracker3.log" "the server's client status for Dias is 30 (goal)" '^client_status 30$'
# Step 7b [loadsend agent]: an offline check saved, then continued while connected.
check "$W/game5.log" "session 5: offline, the chest's check" "\[ap\] check $OFFLINE "
check_not "$W/game5.log" "... with no server" '\[ap\] connecting to the server'
check "$W/game5.log" "... saved in file 1 with the sidecar" '\[ap\] saved .*offline\.slot01\.ap\.json'
connected6=$(grep -nm1 '\[ap\] connected (slot' "$W/game6.log" | cut -d: -f1)
loaded6=$(grep -nm1 '\[ap\] loaded .*offline\.slot01\.ap\.json' "$W/game6.log" | cut -d: -f1)
if [ -n "$connected6" ] && [ -n "$loaded6" ] && [ "$connected6" -lt "$loaded6" ]; then
  echo "PASS session 6: connected at the title, then Continue loads file 1"
else
  echo "FAIL session 6: connected at the title, then Continue loads file 1 (lines ${connected6:-none}, ${loaded6:-none})"
  fails=$((fails + 1))
fi
check "$W/game6.log" "... the load sends the save's checks" '\[ap\] loaded .*offline\.slot01\.ap\.json: [0-9]+ checks, received index 0, sent [0-9]+ checks'
check "$W/server.log" "... the server logs the chest" 'Claude (sent|found) .*\(Arlia Village: Resurrection Bottle chest\)'
check "$W/tracker.log" "... and has it among the checked locations" "checked_locations \[.*\b$OFFLINE\b"
# Step 9 [dskeys agent]: the tracker keys (docs/apworld/data_storage.md).
ds_team_slot() { sed -nE 's/^connected team ([0-9]+) slot ([0-9]+)$/\1_\2/p' "$1" | head -1; }
claude_ts=$(ds_team_slot "$W/ds_claude.log")
dias_ts=$(ds_team_slot "$W/ds_dias.log")
check "$W/ds_claude.log" "tracker keys: the reader logs in to Claude's slot" '^connected team [0-9]+ slot [0-9]+$'
check "$W/game.log" "... the port writes so2_map / so2_story under the server's team and slot" \
  "\[ap\] tracker: so2_map_${claude_ts:-X} \{\"map\":18\}"
check "$W/game.log" "... and the story key" "\[ap\] tracker: so2_story_${claude_ts:-X} \{\"chapter\":-?[0-9]+,\"step\":-?[0-9]+\}"
check "$W/ds_watch.log" "... the watcher sees Arlia (map 18)" "^set so2_map_${claude_ts:-X} \{\"map\":18\}$"
check "$W/ds_watch.log" "... then Hearn's store (map 26, session 2's walk)" "^set so2_map_${claude_ts:-X} \{\"map\":26\}$"
check "$W/ds_watch.log" "... the story key" "^set so2_story_${claude_ts:-X} \{\"chapter\":-?[0-9]+,\"step\":-?[0-9]+\}$"
check "$W/ds_watch.log" "... on world map 1 with x / z (session 4)" "^set so2_map_${claude_ts:-X} \{\"map\":1,\"x\":-?[0-9]+,\"z\":-?[0-9]+\}$"
wm_values=$(grep -cE '^set so2_map_[0-9_]+ \{"map":1,' "$W/ds_watch.log")
wm_distinct=$(grep -E '^set so2_map_[0-9_]+ \{"map":1,' "$W/ds_watch.log" | sort -u | wc -l)
if [ "$wm_distinct" -ge 3 ] && [ "$wm_values" -le 51 ]; then  # session 4: 1500 frames, a key at most every 30
  echo "PASS ... the party's x / z follow the walk ($wm_distinct places), throttled ($wm_values writes)"
else
  echo "FAIL ... the party's x / z follow the walk ($wm_distinct places), throttled ($wm_values writes)"; fails=$((fails + 1))
fi
check "$W/ds_claude.log" "... Get after the runs: Claude's map key holds world map 1" "^retrieved so2_map_${claude_ts:-X} \{\"map\":1,\"x\":-?[0-9]+,\"z\":-?[0-9]+\}$"
check "$W/ds_claude.log" "... and the story key" "^retrieved so2_story_${claude_ts:-X} \{\"chapter\":-?[0-9]+,\"step\":-?[0-9]+\}$"
check "$W/ds_dias.log" "... Dias's keys are his own slot's" "^retrieved so2_story_${dias_ts:-X} \{\"chapter\":-?[0-9]+,\"step\":-?[0-9]+\}$"
if [ -n "$claude_ts" ] && [ "$claude_ts" != "$dias_ts" ]; then
  echo "PASS ... two slots, two sets of keys ($claude_ts, $dias_ts)"
else
  echo "FAIL ... two slots, two sets of keys ($claude_ts, $dias_ts)"; fails=$((fails + 1))
fi
[ -f "$W/cap/frame00700.png" ] && echo "captures: $W/cap/frame00700.png (window), frame00850.png (banner)"
exit $fails
