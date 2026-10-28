#!/bin/bash
# The end-to-end Archipelago check of hub mode (world_mode: hub; docs/apworld/apworld.md "Hub mode",
# docs/handoff/hubproof.md): the SO2 APWorld generates a multiworld of a hub player (Claude) and
# a ChecksFinder player (Bob), a private MultiServer hosts it on 127.0.0.1:AP_E2E_PORT (default
# 38293; ap_e2e.sh uses 38291, ap_e2e_so2.sh 38292), and the port plays the hub slot headless.
# Nothing here talks to a public server.
#
#   SS_DISC1=DISC1.cue apworld/tools/ap_e2e_hub.sh [BINARY]   (apworld/tools/ap_test_server.sh start, once, sets up)
#
# Claude plays world_mode hub (story_route claude, hub_start fun_city, boss_checks on). Item plando
# puts the chamber's dungeon key (default: Arcmene in the Cave of Red Crystal, AP_E2E_CHAMBER = a
# chamber key of apworld/tools/ap_hub_chambers.py in Claude's game) in Bob's Tile 1. In one scripted run:
#   1. Generate.py (seed AP_E2E_SEED, default 4242) writes the multiworld and Claude's version-3
#      .apso2 (the hub block, the 22 chambers, the dungeon doors locked with their keys).
#   2. Bob (a Python client) connects and checks Tile 1: the server sends Claude the key.
#   3. The port starts from the title: New Game, Claude (the hub New Game: chapter 60, the start
#      in Fun City), connects as Claude, receives the key in free play, then walks the hub
#      proof's route (docs/apworld/hub_proof.json `routes`, SS_SWEEP_PLAY) to the chamber through its
#      dungeon's locked door, and plays the chamber's steps (ap_hub_chambers.py): the story view
#      turns on, the boss scene's battle is won by the battle stand-in (SS_BATTLE_STUB), and the
#      chamber's check goes to the server.
#   4. After the port has quit, a tracker client logs in to Claude's slot: the chamber's location
#      is among the server's checked locations.
# The server runs with release / collect off. Output and logs in scratchpad/ap_e2e_hub/
# (AP_E2E_WORK to put them elsewhere). Every game run is under the memory cap (tools/memcap.py:
# SS_MEM_CAP_GB, default 4 GB); a run that dies at it fails. Exit status 0 when every step is seen.
set -u
cd "$(dirname "$0")/../.."
P=$PWD
BIN=${1:-$P/port/build/shattered-stars}
: "${SS_DISC1:?set SS_DISC1 to disc 1 (.cue)}"
export SS_DISC2=${SS_DISC2:-${SS_DISC1//Disc 1/Disc 2}}
W=${AP_E2E_WORK:-$P/scratchpad/ap_e2e_hub}
case "$W" in "$P"/scratchpad/?*) ;; *) echo "AP_E2E_WORK must be a directory under $P/scratchpad" >&2; exit 1 ;; esac
AP=$P/scratchpad/ap
PORT=${AP_E2E_PORT:-38293}
SEED=${AP_E2E_SEED:-4242}
CHAMBER=${AP_E2E_CHAMBER:-arcmene}
TILE1=81000        # ChecksFinder's Tile 1
export SKIP_REQUIREMENTS_UPDATE=1
if [ ! -x "$AP/ap-venv/bin/python" ]; then
  echo "no Archipelago install: run apworld/tools/ap_test_server.sh start (then stop) once" >&2
  exit 1
fi
if (exec 3<>"/dev/tcp/127.0.0.1/$PORT") 2>/dev/null; then
  echo "port $PORT is in use (AP_E2E_PORT picks another)" >&2
  exit 1
fi
rm -rf "$W"
mkdir -p "$W/xdg" "$W/nomods" "$W/players" "$W/out"
ln -sfn "$P/apworld/so2" "$AP/Archipelago/worlds/so2"

# The chamber: its spec (approach map, steps), its row (encounter, location, dungeon), the key's
# item name, and the hub proof's route from the start (Fun City, map 279) to the approach map.
python3 - "$CHAMBER" > "$W/chamber.env" <<'PY' || { echo "no chamber $CHAMBER" >&2; exit 1; }
import json, os, sys
sys.path.insert(0, "apworld/tools")
import ap_hub_chambers as hc
spec = next(s for s in hc.CHAMBERS if s["key"] == sys.argv[1] and s["game"] == "claude")
rows = {b["encounter"]: b for b in json.load(open("docs/apworld/hub_bosses.json"))["bosses"]}
row = rows[spec["enc"]]
groups = {d["dungeon"]: d for d in json.load(open("docs/apworld/hub_bosses.json"))["dungeons"]}
proof = json.load(open("docs/apworld/hub_proof.json"))["games"]["claude"]
best = None
for node, text in proof["routes"].items():
    if int(node.split("@")[0]) == spec["map"]:
        root, _, steps = text.partition("|")
        if int(root) == 279 and (best is None or len(steps.split(",")) < len(best.split(","))):
            best = steps
assert best is not None, "no route from Fun City"
steps = [x for x in best.split(",") if x] + spec["play"]
g = groups.get(row["dungeon"])
print(f"ENC={spec['enc']}")
print(f"LOC={row['location']}")
print(f"KEYNO={g['key'] if g else 0}")
print(f"KEYNAME='{g['name'] if g else ''}'")
print(f"BOSSNAME='{row['name']}'")
print(f"CMAP={row['map']}")
print(f"ROUTE='{','.join(steps)}'")
print(f"FRAMES={spec.get('frames', 12000) + 600 * len(steps) + 5000}")
PY
. "$W/chamber.env"

cat > "$W/players/Claude.yaml" <<EOF
name: Claude
game: "Star Ocean: The Second Story"
"Star Ocean: The Second Story":
  world_mode: hub
  story_route: claude
  hub_start: fun_city
  boss_checks: true
  plando_items:
    - items: {"$KEYNAME": 1}
      locations: ["Tile 1"]
      world: Bob
EOF
cat > "$W/players/Bob.yaml" <<'EOF'
name: Bob
game: ChecksFinder
ChecksFinder: {}
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

cat > "$W/bob.py" <<'BOB'
# Bob: connects and checks Tile 1 (Claude's dungeon key) at once; stays on for the port's checks.
import asyncio, json, sys, time, websockets
TILE1 = int(sys.argv[3])
async def main():
    t0 = time.time()
    log = lambda *a: print(f"[bob {time.time() - t0:6.2f}]", *a, flush=True)
    async with websockets.connect(sys.argv[2], max_size=None) as ws:
        await ws.recv()  # RoomInfo
        await ws.send(json.dumps([{"cmd": "Connect", "game": "ChecksFinder", "name": "Bob", "password": "",
                                   "uuid": "bob-ap-e2e-hub", "version": {"major": 0, "minor": 6, "build": 8, "class": "Version"},
                                   "items_handling": 0b001, "tags": [], "slot_data": False}]))
        deadline = time.time() + float(sys.argv[1])
        while time.time() < deadline:
            try:
                msg = await asyncio.wait_for(ws.recv(), timeout=1.0)
            except asyncio.TimeoutError:
                continue
            for p in json.loads(msg):
                if p["cmd"] == "Connected":
                    log("connected as Bob, slot", p["slot"])
                    await ws.send(json.dumps([{"cmd": "LocationChecks", "locations": [TILE1]}]))
                    log("checked Tile 1")
                elif p["cmd"] == "ReceivedItems":
                    for it in p["items"]:
                        log(f"received item {it['item']} from player {it['player']} location {it['location']}")
        log("done")
asyncio.run(main())
BOB

cat > "$W/tracker.py" <<'TRACKER'
# The server's view of Claude's slot (a tracker login): the checked locations.
import asyncio, json, sys, websockets
async def main():
    async with websockets.connect(sys.argv[1], max_size=None) as ws:
        await ws.recv()  # RoomInfo
        await ws.send(json.dumps([{"cmd": "Connect", "game": "Star Ocean: The Second Story", "name": "Claude",
                                   "password": "", "uuid": "tracker-ap-e2e-hub",
                                   "version": {"major": 0, "minor": 6, "build": 8, "class": "Version"},
                                   "items_handling": 0, "tags": ["Tracker"], "slot_data": False}]))
        while True:
            for p in json.loads(await asyncio.wait_for(ws.recv(), timeout=10)):
                if p["cmd"] == "Connected":
                    print("checked_locations", sorted(p["checked_locations"]))
                    print("missing_locations", len(p["missing_locations"]))
                    return
                if p["cmd"] == "ConnectionRefused":
                    print("refused", p.get("errors"))
                    return
asyncio.run(main())
TRACKER

run_game() {  # LOG TIMEOUT ARGS...: "$BIN" ARGS under the memory cap (tools/memcap.py)
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

(cd "$AP/Archipelago" && exec setsid "$AP/ap-venv/bin/python" MultiServer.py "$zip" --host 127.0.0.1 \
   --port "$PORT" --disable_save --loglevel info --release_mode disabled --collect_mode disabled) \
   > "$W/server.log" 2>&1 < /dev/null &
server=$!
for _ in $(seq 1 300); do
  (exec 3<>"/dev/tcp/127.0.0.1/$PORT") 2>/dev/null && break
  sleep 0.1
done
"$AP/ap-venv/bin/python" "$W/bob.py" 900 "ws://localhost:$PORT" "$TILE1" > "$W/bob.log" 2>&1 &
bob=$!
# The title: New Game, Claude (the cursor's right), the names and the opening skipped (as
# scratchpad/hubcore/run_ng.py); then the field's free play in Fun City, where the event sweep's
# play mode walks the route (each step fires when the player has control on its map).
NEWGAME="850:start,854:none,951:cross,957:none,1251:cross,1257:none,1551:cross,1557:none,1851:cross,1857:none"
NEWGAME="$NEWGAME,2000:right,2006:none,2151:start,2157:none,2451:cross,2457:none,2700:start,2706:none"
NEWGAME="$NEWGAME,3000:start,3006:none,3300:start,3306:none"
XDG_CONFIG_HOME="$W/xdg" SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy SS_PACE=1 \
  SS_AP_SERVER="ws://localhost:$PORT" SS_AP_SLOT=Claude SS_MODS_DIR="$W/nomods" \
  SS_EVENT_SWEEP=1 SS_SWEEP_PLAY="$ROUTE" SS_BATTLE_STUB=1 SS_DEBUG_INPUT="$((FRAMES - 100)):ap_status" \
  run_game "$W/game.log" $((FRAMES / 60 + 300)) --headless --debug --no-memcard --ap-seed "$seed_file" \
  --frames "$FRAMES" --input "$NEWGAME"
kill "$bob" 2>/dev/null; wait "$bob" 2>/dev/null
"$AP/ap-venv/bin/python" "$W/tracker.py" "ws://localhost:$PORT" > "$W/tracker.log" 2>&1
kill -- "-$server" 2>/dev/null || kill "$server" 2>/dev/null

fails=0
check() {  # FILE what pattern
  if grep -qE "$3" "$1"; then echo "PASS $2"; else echo "FAIL $2"; fails=$((fails + 1)); fi
}
check_not() {
  if grep -qE "$3" "$1"; then echo "FAIL $2"; fails=$((fails + 1)); else echo "PASS $2"; fi
}
check "$W/generate.log" "the multiworld generates (an SO2 hub slot + ChecksFinder)" 'Creating final archive'
check "$seed_file" "Claude's seed is version 3, world_mode hub" '"world_mode": "hub"'
check "$seed_file" "... its key is in Bob's Tile 1" "\"name\": \"$KEYNAME\""
if [ "$(grep -cE '^game\.log rc .* cap_mb [1-9]' "$W/memcap.txt")" = 1 ]; then
  echo "PASS the game run ran under the memory cap"
else
  echo "FAIL the game run ran under the memory cap"; fails=$((fails + 1))
fi
check_not "$W/memcap.txt" "... and didn't die at it" 'at_cap True'
check "$W/bob.log" "Bob checks Tile 1 (Claude's key)" 'checked Tile 1'
check "$W/game.log" "the port connects as Claude" '\[ap\] connected \(slot [0-9]+\)'
check "$W/game.log" "the title's New Game builds the hub (Claude, Fun City)" "\[ap\] hub New Game: Claude's game, .*chapter 60 step 4000, disc 2; Fun City \(map 279"
check "$W/game.log" "the key arrives from the server ($KEYNAME)" "\[ap\] key $KEYNO \(.*\) received"
check_not "$W/game.log" "no door refused on the way" '\[ap\] door [0-9]+.*: locked, needs'
check "$W/game.log" "the chamber's story view turns on ($BOSSNAME, map $CMAP)" "\[ap\] hub: (story view on: map $CMAP .*encounter $ENC\)|chamber map $CMAP .*encounter $ENC\): no story view)"
check "$W/game.log" "the boss battle is won (encounter $ENC)" "\[ap\] encounter $ENC ended: outcome 0"
check "$W/game.log" "the chamber's check ($LOC)" "\[ap\] check $LOC "
check "$W/game.log" "goal_won records the boss" "\[ap\] goal: encounter $ENC won"
check "$W/tracker.log" "the server's checked locations hold the chamber" "checked_locations \[.*\b$LOC\b"
exit $fails
