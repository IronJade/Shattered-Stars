#!/bin/bash
# The end-to-end Archipelago check with a real Star Ocean slot: the SO2 APWorld (apworld/so2)
# generates a multiworld of an SO2 player (Claude) and a ChecksFinder player (Bob), a private
# MultiServer hosts it on 127.0.0.1:AP_E2E_PORT (default 38292; ap_e2e.sh uses 38291), and the
# port plays the SO2 slot headless. Nothing here talks to a public server.
#
#   SS_DISC1=DISC1.cue tools/ap_e2e_so2.sh [BINARY]   (tools/ap_test_server.sh start, once, sets up)
#
# Claude plays with the phase-3 options character_items, drops_as_checks (first_drop) and
# traps (20 %). Item plando fixes the cross-world placements, so the run is the same every time:
#   - Arlia's 200-Fol crate (map 18, flag 0x962; "Arlia Village: 200 Fol chest") holds Bob's
#     Map Width;
#   - Bob's Tiles 1, 4 and 10 hold Claude's Resurrection Bottle, Celine (a character item) and a
#     Fol Loss Trap.
# In one scripted run:
#   1. Generate.py (seed AP_E2E_SEED, default 4242) writes the multiworld and Claude's .apso2.
#   2. The port (paced: SS_PACE; scratch config, no memory card) connects as Claude, starts on
#      map 18 and opens the crate: the check goes out, the window says "Sent Map Width to
#      Bob." and no Fol is paid.
#   3. Bob (a Python client) receives Map Width from the crate's location, then checks Tiles 1,
#      4 and 10: in free play the port gives the Resurrection Bottle, Celine joins the party,
#      and the trap takes 10 % of the 10000 Fol the console gave (`fol 10000`), each with its
#      banner.
#   4. The console starts a random encounter with a sure drop (`battle 547`, five
#      Shadowflowers, enemy type 14); --battle-end wins it after 60 frames, and the result
#      screen's first drop of type 14 is the check "Shadowflower first drop" instead.
#   5. The console starts the final boss (`battle 539`), won the same way: the battle's exit
#      records the goal (CLIENT_GOAL).
#   6. After the port has quit, a tracker client logs in to Claude's slot and reads the
#      server's view: the crate and the drop among the checked locations, and the client status
#      30 (goal).
# The server runs with release / collect off, so the checked locations are exactly what was
# sent. Output, logs and captures (frames 700: the window, 850: the first banner) in
# scratchpad/ap_e2e_so2/. Exit status 0 when every step is seen.
set -u
cd "$(dirname "$0")/.."
P=$PWD
BIN=${1:-$P/engine/build/shattered-stars}
: "${SS_DISC1:?set SS_DISC1 to disc 1 (.cue)}"
W=$P/scratchpad/ap_e2e_so2
AP=$P/scratchpad/ap
PORT=${AP_E2E_PORT:-38292}
SEED=${AP_E2E_SEED:-4242}
CRATE=1347569665   # Arlia Village: 200 Fol chest (docs/ap/field.json)
TILE1=81000        # ChecksFinder's Tile 1 (Tile n: 81000 + n - 1)
DROP=1362231310    # Shadowflower first drop: 0x50320000 + (8 << 21) + enemy type 14
export SKIP_REQUIREMENTS_UPDATE=1
rm -rf "$W"
mkdir -p "$W/xdg" "$W/nomods" "$W/players" "$W/out"
if [ ! -x "$AP/ap-venv/bin/python" ]; then
  echo "no Archipelago install: run tools/ap_test_server.sh start (then stop) once" >&2
  exit 1
fi
if (exec 3<>"/dev/tcp/127.0.0.1/$PORT") 2>/dev/null; then
  echo "port $PORT is in use (AP_E2E_PORT picks another)" >&2
  exit 1
fi
ln -sfn "$P/apworld/so2" "$AP/Archipelago/worlds/so2"

cat > "$W/players/Claude.yaml" <<'EOF'
name: Claude
game: "Star Ocean: The Second Story"
"Star Ocean: The Second Story":
  protagonist: claude
  character_items: true
  drops_as_checks: first_drop
  traps: 20
  plando_items:
    - items: {"Resurrection Bottle": 1}
      locations: ["Tile 1"]
      world: Bob
    - items: {"Celine": 1}
      locations: ["Tile 4"]
      world: Bob
    - items: {"Fol Loss Trap": 1}
      locations: ["Tile 10"]
      world: Bob
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

cat > "$W/bob.py" <<'BOB'
# Bob: connects, waits until Claude's crate sends him Map Width, then checks Tiles 1, 4 and 10
# (Claude's Resurrection Bottle, Celine, a Fol Loss Trap). Stays on to see Claude's goal.
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
                            await ws.send(json.dumps([{"cmd": "LocationChecks", "locations": [TILE1, TILE1 + 3, TILE1 + 9]}]))
                            log("checked Tiles 1, 4 and 10")
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
    async with websockets.connect(sys.argv[1], max_size=None) as ws:
        await ws.recv()  # RoomInfo
        await ws.send(json.dumps([{"cmd": "Connect", "game": "Star Ocean: The Second Story", "name": "Claude",
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

# The server in its own session and process group (MultiServer starts a child process).
(cd "$AP/Archipelago" && exec setsid "$AP/ap-venv/bin/python" MultiServer.py "$zip" --host 127.0.0.1 \
   --port "$PORT" --disable_save --loglevel info --release_mode disabled --collect_mode disabled) \
   > "$W/server.log" 2>&1 < /dev/null &
server=$!
for _ in $(seq 1 300); do
  (exec 3<>"/dev/tcp/127.0.0.1/$PORT") 2>/dev/null && break
  sleep 0.1
done
"$AP/ap-venv/bin/python" "$W/bob.py" 220 "ws://localhost:$PORT" "$CRATE" "$TILE1" > "$W/bob.log" 2>&1 &
bob=$!
# 600-766: open the crate and close its window; 730: 10000 Fol for the trap to take; from 850:
# the received items' banners; 1600: the drop battle, 3600: the final boss (each won after 60
# battle frames), each followed by the result screen's pages.
presses=""
for v in 600 760 1100 $(seq 2200 200 3200) $(seq 4200 200 5200); do presses="$presses,$v:cross,$((v + 6)):none"; done
env XDG_CONFIG_HOME="$W/xdg" SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy SS_PACE=1 \
  SS_AP_SERVER="ws://localhost:$PORT" SS_AP_SLOT=Claude SS_MODS_DIR="$W/nomods" \
  SS_TRACE_LEADER=60 SS_DEBUG_INPUT="730:fol 10000;1500:ap_status;1600:battle 547;3600:battle 539;5700:ap_status" \
  timeout 600 "$BIN" --headless --debug --no-memcard --ap-seed "$seed_file" --frames 5800 --map 18 \
  --battle-end 60:0 \
  --input "200:up,439:none,450:left,554:none,560:up,572:none$presses" \
  --capture "$W/cap" --capture-frames 700,850 < /dev/null > "$W/game.log" 2>&1
wait $bob
"$AP/ap-venv/bin/python" "$W/tracker.py" "ws://localhost:$PORT" > "$W/tracker.log" 2>&1
kill -- "-$server" 2>/dev/null || kill "$server" 2>/dev/null

fails=0
check() {  # FILE what pattern
  if grep -qE "$3" "$1"; then echo "PASS $2"; else echo "FAIL $2"; fails=$((fails + 1)); fi
}
check_not() {  # FILE what pattern (must not appear)
  if grep -qE "$3" "$1"; then echo "FAIL $2"; fails=$((fails + 1)); else echo "PASS $2"; fi
}
check "$W/generate.log" "the multiworld generates (SO2 + ChecksFinder)" 'Creating final archive'
check "$seed_file" "the crate holds Bob's Map Width" '"name": "Map Width"'
check_not "$W/generate.log" "plando found every item in the pool" 'Could not remove'
check "$seed_file" "the seed's party holds Celine (character_items)" '"Celine"'
check "$W/game.log" "the port connects as Claude" '\[ap\] connected \(slot [0-9]+\)'
check "$W/game.log" "the crate's check" "\[ap\] check $CRATE .*: Map Width for Bob"
check "$W/game.log" "no Fol from the crate" '\[leader\] vblank 720 map 18 .* fol 0$'
check "$W/bob.log" "Bob receives Map Width from the crate" "received item 80000 from player [0-9]+ location $CRATE"
check "$W/game.log" "Bob's Tile 1 gives the Resurrection Bottle" '\[ap\] given Resurrection Bottle'
check "$W/game.log" "Bob's Tile 4: Celine joins the party" '\[ap\] Celine joins the party \(slot [0-9]+\)'
check "$W/game.log" "Bob's Tile 10: the Fol Loss Trap takes 1000 Fol" '\[ap\] trap fol_loss: lost 1000 Fol'
check "$W/game.log" "the first drop of Shadowflower is withheld" '\[ap\] first drop of enemy type 14 \(item [0-9]+\): the check instead'
check "$W/game.log" "the drop's check" "\[ap\] check $DROP "
check "$W/game.log" "the goal (encounter 539 won)" '\[ap\] goal: encounter 539 won'
check "$W/game.log" "the port's status" 'slot Claude \| checks 2/[0-9]+ \| received 3 \| waiting 0 \| connected \| goal'
check "$W/server.log" "the server: Claude completed the goal" 'Claude.*has completed their goal'
check "$W/tracker.log" "the server's checked locations hold the crate" "checked_locations \[.*\b$CRATE\b"
check "$W/tracker.log" "... and the drop" "checked_locations \[.*\b$DROP\b"
check "$W/tracker.log" "the server's client status is 30 (goal)" '^client_status 30$'
[ -f "$W/cap/frame00700.png" ] && echo "captures: $W/cap/frame00700.png (window), frame00850.png (banner)"
exit $fails
