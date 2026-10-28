#!/bin/bash
# The end-to-end Archipelago check: the port against a two-slot multiworld on a local server
# of its own (apworld/tools/ap_test_server.sh's install and multiworld, a private MultiServer on
# 127.0.0.1:AP_E2E_PORT, default 38291, started and stopped here, so other runs of the shared
# test server don't interfere). Nothing here talks to a public server.
#
#   SS_DISC1=DISC1.cue apworld/tools/ap_e2e.sh [BINARY]      (apworld/tools/ap_test_server.sh start, once, sets up)
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
# Phase 3 [world agent], two more sessions on the same server (a new game each, no sidecar, so
# the server hands Alice's three items over again):
#   4. With a phase-3 seed for Alice (the same multiworld seed id), Bob's three items are a
#      character (Celine) and two traps (half the Fol, poison); her Tile 2 is the first drop
#      of Berle (enemy type 197): the port fights encounter 535 (console `battle`,
#      --battle-end 60:0), the drop is the check and Bob receives Map Width from location
#      81001. Bob, with the DeathLink tag, answers with a DeathLink: Alice's party falls, and
#      no DeathLink goes back (it was the link's death).
#   5. Alice loses a battle (--battle-end 60:2): the game over sends a DeathLink that Bob
#      receives (source Alice).
# Phase 4 [world agent], two more sessions, same server and multiworld:
#   6. An open-world seed with one gated door (map 17's door to Arlia, id 1116181, key 1,
#      "Arlia Pass"): the port walks in and is refused ("Arlia Pass needed"). Bob (a second
#      Python client) checks his Tile 7, which holds Alice's Map Width (relabelled here as the
#      key); once the port receives it over the network, walking in again opens the door and
#      map 18 loads.
#   7. A shuffle_shops seed where Hearn's store (map 26, shop 1) entry 2 (Lavender, item 224)
#      is Alice's Tile 3, holding Bob's Map Bombs: the port buys one, the shop check goes to
#      the server, and Bob receives the item in free play.
# Output, logs and captures (frames 700: the window, 850: the first banner) in
# scratchpad/ap_e2e/. Exit status 0 when every step is seen.
set -u
cd "$(dirname "$0")/../.."
P=$PWD
BIN=${1:-$P/port/build/shattered-stars}
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

cat > "$W/seed_p3.apso2" <<'SEED'
{"format": "apso2", "version": 1, "seed": "13515657874892102023",
 "game": "Star Ocean: The Second Story",
 "slot": {"name": "Alice", "id": 1},
 "options": {"deathlink": true, "character_items": true, "enemy_dropsanity": "first_drop"},
 "items": {
  "80002": {"name": "Celine", "kind": "character", "character": 3},
  "80000": {"name": "Fol trap", "kind": "trap", "trap": "fol_loss", "percent": 50},
  "80001": {"name": "Poison trap", "kind": "trap", "trap": "status", "status": "poison"}
 },
 "locations": [
  {"id": 81001, "name": "Berle's first drop (Tile 2)", "check": {"type": "drop", "enemy": 197},
   "item": {"player": 2, "player_name": "Bob", "name": "Map Width", "ap_id": 80000, "classification": "progression"}}
 ],
 "patches": []}
SEED

cat > "$W/bob_dl.py" <<'BOB'
# Bob for sessions 4 / 5 (DeathLink tag): logs every item and Bounced packet. With "answer",
# once Alice's Tile 2 (81001) sends him Map Width he sends a DeathLink of his own.
import asyncio, json, sys, time, websockets
async def main():
    t0 = time.time()
    log = lambda *a: print(f"[bob {time.time() - t0:6.2f}]", *a, flush=True)
    answer = sys.argv[3] == "answer"
    async with websockets.connect(sys.argv[2]) as ws:
        await ws.recv()  # RoomInfo
        await ws.send(json.dumps([{"cmd": "Connect", "game": "ChecksFinder", "name": "Bob", "password": "",
                                   "uuid": "bob-ap-e2e-dl", "version": {"major": 0, "minor": 6, "build": 8, "class": "Version"},
                                   "items_handling": 0b001, "tags": ["DeathLink"], "slot_data": False}]))
        sent = False
        deadline = time.time() + float(sys.argv[1])
        while time.time() < deadline:
            try:
                msg = await asyncio.wait_for(ws.recv(), timeout=1.0)
            except asyncio.TimeoutError:
                continue
            for p in json.loads(msg):
                if p["cmd"] == "Connected":
                    log("connected as Bob (DeathLink), slot", p["slot"])
                elif p["cmd"] == "ReceivedItems":
                    for it in p["items"]:
                        log(f"received item {it['item']} from player {it['player']} location {it['location']}")
                        if answer and (it["item"], it["player"], it["location"]) == (80000, 1, 81001) and not sent:
                            sent = True
                            await ws.send(json.dumps([{"cmd": "Bounce", "tags": ["DeathLink"],
                                                       "data": {"time": time.time(), "source": "Bob", "cause": "Bob fell"}}]))
                            log("sent a DeathLink")
                elif p["cmd"] == "Bounced" and "DeathLink" in p.get("tags", []):
                    log("DeathLink received from", p.get("data", {}).get("source"))
        log("done")
asyncio.run(main())
BOB

# Phase 4 [world agent]. The real multiworld's spoiler (scratchpad/ap/out/*_Spoiler.txt) gives
# the raw ids reused here: Alice's Tile 7 (id 81006) holds her own "Map Width" (id 80000, the
# same item session 1 sent Bob from Tile 1); relabelled "Arlia Pass" in this seed, it is the
# key door 1116181 (map 17 -> Arlia) needs in open_world. Alice's Tile 3 (id 81002) holds Bob's
# Map Bombs; here it is Hearn's shop (shop 1, entry 2, item 224, Lavender).
cat > "$W/seed_p4door.apso2" <<'SEED'
{"format": "apso2", "version": 1, "seed": "13515657874892102023",
 "game": "Star Ocean: The Second Story",
 "slot": {"name": "Alice", "id": 1},
 "options": {"world_mode": "open_world"},
 "items": {"80000": {"name": "Arlia Pass", "kind": "key", "key": 1}},
 "locations": [],
 "doors": [{"id": 1116181, "routine": "0x204c", "key": 1, "name": "Arlia exit -> Arlia"}]}
SEED

cat > "$W/seed_p4shop.apso2" <<'SEED'
{"format": "apso2", "version": 1, "seed": "13515657874892102023",
 "game": "Star Ocean: The Second Story",
 "slot": {"name": "Alice", "id": 1},
 "options": {"shuffle_shops": "full"},
 "locations": [
  {"id": 81002, "name": "Arlia shop: Lavender (Tile 3)",
   "check": {"type": "shop", "shop": 1, "index": 2, "item": 224},
   "item": {"player": 2, "player_name": "Bob", "name": "Map Bombs", "ap_id": 80001}}
 ]}
SEED

cat > "$W/bob_key.py" <<'BOB'
# Bob for session 6: connects and, without waiting for anything from Alice, checks his Tile 7
# (location 81006), which the multiworld gives to Alice (her Map Width, this seed's key item).
import asyncio, json, sys, time, websockets
async def main():
    t0 = time.time()
    log = lambda *a: print(f"[bob {time.time() - t0:6.2f}]", *a, flush=True)
    async with websockets.connect(sys.argv[2]) as ws:
        await ws.recv()  # RoomInfo
        await ws.send(json.dumps([{"cmd": "Connect", "game": "ChecksFinder", "name": "Bob", "password": "",
                                   "uuid": "bob-ap-e2e-key", "version": {"major": 0, "minor": 6, "build": 8, "class": "Version"},
                                   "items_handling": 0b001, "tags": [], "slot_data": False}]))
        sent = False
        deadline = time.time() + float(sys.argv[1])
        while time.time() < deadline:
            try:
                msg = await asyncio.wait_for(ws.recv(), timeout=1.0)
            except asyncio.TimeoutError:
                continue
            for p in json.loads(msg):
                if p["cmd"] == "Connected":
                    log("connected as Bob, slot", p["slot"])
                    if not sent:
                        sent = True
                        await ws.send(json.dumps([{"cmd": "LocationChecks", "locations": [81006]}]))
                        log("checked Tile 7 (Alice's Arlia Pass)")
        log("done")
asyncio.run(main())
BOB

cat > "$W/bob_shop.py" <<'BOB'
# Bob for session 7: connects and logs whatever the server hands him, so Hearn's shop check
# (Alice's Tile 3, location 81002) shows up as a received item.
import asyncio, json, sys, time, websockets
async def main():
    t0 = time.time()
    log = lambda *a: print(f"[bob {time.time() - t0:6.2f}]", *a, flush=True)
    async with websockets.connect(sys.argv[2]) as ws:
        await ws.recv()  # RoomInfo
        await ws.send(json.dumps([{"cmd": "Connect", "game": "ChecksFinder", "name": "Bob", "password": "",
                                   "uuid": "bob-ap-e2e-shop", "version": {"major": 0, "minor": 6, "build": 8, "class": "Version"},
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
                elif p["cmd"] == "ReceivedItems":
                    for it in p["items"]:
                        log(f"received item {it['item']} from player {it['player']} location {it['location']}")
        log("done")
asyncio.run(main())
BOB

python3 apworld/so2/patches.py "$W/seed_in.apso2" --disc "$SS_DISC1" -o "$W/e2e.apso2" || exit 1
AP=$P/scratchpad/ap
PORT=${AP_E2E_PORT:-38291}
zip=$(ls "$AP"/out/AP_*.zip 2>/dev/null | head -1)
if [ -z "$zip" ] || [ ! -x "$AP/ap-venv/bin/python" ]; then
  echo "no test multiworld: run apworld/tools/ap_test_server.sh start (then stop) once" >&2
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

# Session 4: Bob's items as a character and traps, the first-drop check, Bob's DeathLink.
"$AP/ap-venv/bin/python" "$W/bob_dl.py" 75 "ws://localhost:$PORT" answer > "$W/bob_p3.log" 2>&1 &
bob=$!
MASH=$(for f in $(seq 1240 20 2200); do printf '%d:cross,%d:none,' "$f" $((f + 4)); done)
env XDG_CONFIG_HOME="$W/xdg" SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy SS_PACE=1 \
  SS_AP_SERVER="ws://localhost:$PORT" SS_AP_SLOT=Alice SS_AP_GAME=ChecksFinder SS_MODS_DIR="$W/nomods" \
  SS_TRACE_LEADER=60 SS_DEBUG_INPUT="60:fol 1000;1150:ap_status;1200:battle 535" \
  timeout 600 "$BIN" --headless --debug --no-memcard --ap-seed "$W/seed_p3.apso2" --frames 2700 --map 18 \
  --battle-end 60:0 --input "${MASH%,}" < /dev/null > "$W/game_p3.log" 2>&1
wait $bob

# Session 5: Alice's own game over sends a DeathLink.
"$AP/ap-venv/bin/python" "$W/bob_dl.py" 45 "ws://localhost:$PORT" listen > "$W/bob_dl.log" 2>&1 &
bob=$!
env XDG_CONFIG_HOME="$W/xdg" SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy SS_PACE=1 \
  SS_AP_SERVER="ws://localhost:$PORT" SS_AP_SLOT=Alice SS_AP_GAME=ChecksFinder SS_MODS_DIR="$W/nomods" \
  SS_TRACE_LEADER=60 SS_DEBUG_INPUT="200:battle 1" \
  timeout 600 "$BIN" --headless --debug --no-memcard --ap-seed "$W/seed_p3.apso2" --frames 1800 --map 18 \
  --battle-end 60:2 < /dev/null > "$W/game_dl.log" 2>&1
wait $bob

# Session 6: a key item from Bob unlocks the open-world door (map 17 -> Arlia, id 1116181).
"$AP/ap-venv/bin/python" "$W/bob_key.py" 60 "ws://localhost:$PORT" > "$W/bob_key.log" 2>&1 &
bob=$!
env XDG_CONFIG_HOME="$W/xdg" SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy SS_PACE=1 \
  SS_AP_SERVER="ws://localhost:$PORT" SS_AP_SLOT=Alice SS_AP_GAME=ChecksFinder SS_MODS_DIR="$W/nomods" \
  SS_TRACE=1 SS_TRACE_LEADER=15 \
  timeout 600 "$BIN" --headless --debug --no-memcard --ap-seed "$W/seed_p4door.apso2" --frames 900 --map 17 \
  --map-pos 110,0,-457 --input "100:right,200:none,600:left,700:none" \
  < /dev/null > "$W/game_p4door.log" 2>&1
wait $bob

# Session 7: Hearn's shop (map 26, shop 1, entry 2, Lavender) sends a check; Bob gets the item.
"$AP/ap-venv/bin/python" "$W/bob_shop.py" 60 "ws://localhost:$PORT" > "$W/bob_shop.log" 2>&1 &
bob=$!
env XDG_CONFIG_HOME="$W/xdg" SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy SS_PACE=1 \
  SS_AP_SERVER="ws://localhost:$PORT" SS_AP_SLOT=Alice SS_AP_GAME=ChecksFinder SS_MODS_DIR="$W/nomods" \
  SS_DEBUG_INPUT="50:fol 50000" \
  timeout 600 "$BIN" --headless --debug --no-memcard --ap-seed "$W/seed_p4shop.apso2" --frames 2300 --map 18 \
  --input "200:up,439:none,450:left,663:none,670:up,1000:none,1010:left,1060:none,1070:up,1250:none,1260:cross,1266:none,1300:cross,1306:none,1400:cross,1406:none,1500:cross,1506:none,1560:cross,1566:none,1650:down,1656:none,1700:down,1706:none,1750:right,1756:none,1900:cross,1906:none,1950:cross,1956:none,2100:cross,2106:none" \
  < /dev/null > "$W/game_p4shop.log" 2>&1
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
check "$W/game_p3.log" "phase 3: Bob's Map Bombs is Celine, who joins" '\[ap\] Celine joins the party \(slot 1\)'
check "$W/game_p3.log" "phase 3: the Fol trap halves the Fol" '\[ap\] trap fol_loss: lost 500 Fol'
check "$W/game_p3.log" "phase 3: the poison trap" '\[ap\] trap status: the party is poisoned'
check "$W/game_p3.log" "phase 3: Berle's first drop is the check" '\[ap\] check 81001 .*: Map Width for Bob'
check "$W/bob_p3.log" "phase 3: Bob receives Map Width from the drop" 'received item 80000 from player 1 location 81001'
check "$W/game_p3.log" "phase 3: Bob's DeathLink takes Alice's party" '\[ap\] DeathLink from Bob: the party falls'
if grep -q 'DeathLink sent' "$W/game_p3.log" || grep -q 'DeathLink received from Alice' "$W/bob_p3.log"; then
  echo "FAIL phase 3: a DeathLink went back for the link's own death"; fails=$((fails + 1))
else
  echo "PASS phase 3: no DeathLink back for the link's own death"
fi
check "$W/game_dl.log" "phase 3: Alice's game over sends a DeathLink" '\[ap\] game over: DeathLink sent'
check "$W/bob_dl.log" "phase 3: Bob receives it" 'DeathLink received from Alice'
check "$W/game_p4door.log" "phase 4: the locked door refuses" '\[ap\] door 1116181 \(Arlia exit -> Arlia\): locked, needs Arlia Pass'
check "$W/bob_key.log" "phase 4: Bob sends the key" 'checked Tile 7'
check "$W/game_p4door.log" "phase 4: the key arrives over the network" '\[ap\] key 1 \(Arlia Pass\) received'
check "$W/game_p4door.log" "phase 4: the door opens and map 18 loads" 'field_load_map\(map=18 keep=1 flags=0\)'
if grep -q 'field_load_map(map=18 keep=1 flags=0) at vblank [1-5][0-9][0-9]$' "$W/game_p4door.log"; then
  echo "FAIL phase 4: the door transferred while still locked"; fails=$((fails + 1))
fi
check "$W/game_p4shop.log" "phase 4: the shop slot's check is sent" '\[ap\] check 81002 .*: Map Bombs for Bob'
check "$W/game_p4shop.log" "phase 4: the copy bought is ours, the purchase the check" '\[ap\] shop 1 entry 2 \(item 224\): 1 bought, and the slot.s check'
check "$W/bob_shop.log" "phase 4: Bob receives the shop item" 'received item [0-9]+ from player 1 location 81002'
[ -f "$W/cap/frame00700.png" ] && echo "captures: $W/cap/frame00700.png (window), frame00850.png (banner)"
exit $fails
