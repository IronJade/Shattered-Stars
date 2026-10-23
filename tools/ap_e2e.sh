#!/bin/bash
# The end-to-end Archipelago check: the port against a two-slot multiworld on a local server
# of its own (tools/ap_test_server.sh's install and multiworld, a private MultiServer on
# 127.0.0.1:AP_E2E_PORT, default 38291, started and stopped here, so other runs of the shared
# test server don't interfere). Nothing here talks to a public server.
#
#   SS_DISC1=DISC1.cue tools/ap_e2e.sh [BINARY]      (tools/ap_test_server.sh start, once, sets up)
#
# Until there is a Star Ocean world to host, the server's ChecksFinder slots Alice and Bob
# stand in, as for the ap_net tests: the port logs in as Alice (SS_AP_GAME=ChecksFinder) with a
# hand-written seed for that slot. In one scripted run:
#   1. apworld/so2/patches.py fills the seed: Arlia's 200-Fol crate (map 18, flag 0x962) is
#      Alice's Tile 1, which holds Bob's Map Width, so it gets placeholder 0x400 and its patch.
#   2. The port (headless, paced at the field rate: SS_PACE; scratch config, no memory card)
#      starts on map 18 and opens the crate: the check goes to the server, the window says
#      "Sent Map Width to Bob." and no Fol is paid.
#   3. Bob (a Python client) receives Map Width from Alice, then checks three of his tiles that
#      hold Alice's items; the port gives them in free play (Blueberries, Blackberries, 100 Fol)
#      with the banner.
# Output, logs and captures (frames 700: the window, 850: the first banner) in
# scratchpad/ap_e2e/. Exit status 0 when every step is seen.
set -u
cd "$(dirname "$0")/.."
P=$PWD
BIN=${1:-$P/engine/build/shattered-stars}
: "${SS_DISC1:?set SS_DISC1 to disc 1 (.cue)}"
W=$P/scratchpad/ap_e2e
rm -rf "$W"
mkdir -p "$W/xdg" "$W/nomods"

cat > "$W/seed_in.apso2" <<'SEED'
{"format": "apso2", "version": 1, "seed": "13515657874892102023",
 "game": "Star Ocean: The Second Story",
 "slot": {"name": "Alice", "id": 1},
 "options": {"deathlink": false},
 "items": {
  "80000": {"name": "Blueberries", "kind": "item", "game_id": 1},
  "80001": {"name": "Blackberries", "kind": "item", "game_id": 2},
  "80002": {"name": "100 Fol", "kind": "fol", "amount": 100}
 },
 "locations": [
  {"id": 81000, "name": "Arlia: crate by the store (Tile 1)",
   "check": {"type": "chest", "map": 18, "flag": "0x962"},
   "sites": [{"map": 18, "word": 2092, "expected": null, "form": "word", "role": "item"}],
   "item": {"player": 2, "player_name": "Bob", "name": "Map Width", "ap_id": 80000,
            "classification": "progression"}}
 ],
 "patches": []}
SEED

cat > "$W/bob.py" <<'BOB'
# Bob: connects, waits until Alice's chest sends him Map Width (item 80000 from location
# 81000), then checks his Tiles 1, 4 and 10 (Alice's Map Bombs, Map Width, Map Height).
import asyncio, json, sys, time, websockets
async def main():
    t0 = time.time()
    log = lambda *a: print(f"[bob {time.time() - t0:6.2f}]", *a, flush=True)
    async with websockets.connect(sys.argv[2]) as ws:
        await ws.recv()  # RoomInfo
        await ws.send(json.dumps([{"cmd": "Connect", "game": "ChecksFinder", "name": "Bob", "password": "",
                                   "uuid": "bob-ap-e2e", "version": {"major": 0, "minor": 6, "build": 8, "class": "Version"},
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
                        if (it["item"], it["player"], it["location"]) == (80000, 1, 81000) and not got:
                            got = True
                            await ws.send(json.dumps([{"cmd": "LocationChecks", "locations": [81000, 81003, 81009]}]))
                            log("checked Tiles 1, 4, 10")
                            deadline = min(deadline, time.time() + 8)
        log("done; Map Width from Alice:", got)
asyncio.run(main())
BOB

python3 apworld/so2/patches.py "$W/seed_in.apso2" --disc "$SS_DISC1" -o "$W/e2e.apso2" || exit 1
AP=$P/scratchpad/ap
PORT=${AP_E2E_PORT:-38291}
zip=$(ls "$AP"/out/AP_*.zip 2>/dev/null | head -1)
if [ -z "$zip" ] || [ ! -x "$AP/ap-venv/bin/python" ]; then
  echo "no test multiworld: run tools/ap_test_server.sh start (then stop) once" >&2
  exit 1
fi
if (exec 3<>"/dev/tcp/127.0.0.1/$PORT") 2>/dev/null; then
  echo "port $PORT is in use (AP_E2E_PORT picks another)" >&2
  exit 1
fi
# The server in its own session and process group (MultiServer starts a child process).
(cd "$AP/Archipelago" && exec setsid "$AP/ap-venv/bin/python" MultiServer.py "$zip" --host 127.0.0.1 \
   --port "$PORT" --disable_save --loglevel info) > "$W/server.log" 2>&1 < /dev/null &
server=$!
for _ in $(seq 1 300); do
  (exec 3<>"/dev/tcp/127.0.0.1/$PORT") 2>/dev/null && break
  sleep 0.1
done
"$AP/ap-venv/bin/python" "$W/bob.py" 90 "ws://localhost:$PORT" > "$W/bob.log" 2>&1 &
bob=$!
env XDG_CONFIG_HOME="$W/xdg" SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy SS_PACE=1 \
  SS_AP_SERVER="ws://localhost:$PORT" SS_AP_SLOT=Alice SS_AP_GAME=ChecksFinder SS_MODS_DIR="$W/nomods" \
  SS_TRACE_LEADER=60 SS_DEBUG_INPUT="1270:ap_status" \
  timeout 600 "$BIN" --headless --debug --no-memcard --ap-seed "$W/e2e.apso2" --frames 1300 --map 18 \
  --input "200:up,439:none,450:left,554:none,560:up,572:none,600:cross,606:none,760:cross,766:none,1100:cross,1106:none" \
  --capture "$W/cap" --capture-frames 700,850 < /dev/null > "$W/game.log" 2>&1
wait $bob
kill -- "-$server" 2>/dev/null || kill "$server" 2>/dev/null

fails=0
check() {  # FILE what pattern
  if grep -qE "$3" "$1"; then echo "PASS $2"; else echo "FAIL $2"; fails=$((fails + 1)); fi
}
check "$W/e2e.apso2" "patches.py: the crate's placeholder line" 'script 18 0x82c 0xffffff38 0x00000400'
check "$W/game.log" "the port connects as Alice" '\[ap\] connected \(slot 1\)'
check "$W/game.log" "the crate's check" '\[ap\] check 81000 .*: Map Width for Bob'
check "$W/game.log" "no Fol from the crate" '\[leader\] vblank 720 map 18 .* fol 0$'
check "$W/bob.log" "Bob receives Map Width from Alice's crate" 'received item 80000 from player 1 location 81000'
check "$W/game.log" "Bob's items given (3)" '\[ap\] given (Blueberries|Blackberries|100 Fol) \(server item 2\)'
check "$W/game.log" "status" 'slot Alice \| checks 1/1 \| received 3 \| waiting 0 \| connected'
[ -f "$W/cap/frame00700.png" ] && echo "captures: $W/cap/frame00700.png (window), frame00850.png (banner)"
exit $fails
