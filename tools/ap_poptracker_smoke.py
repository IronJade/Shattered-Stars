#!/usr/bin/env python3
"""Headless smoke test of the PopTracker pack in the real PopTracker (docs/ap/poptracker.md
"Smoke test"). No window can open: PopTracker runs with a cleared environment (no DISPLAY, no
WAYLAND_DISPLAY, no D-Bus, an empty XDG_RUNTIME_DIR and an empty PATH, so no display server and no
dialog helper is reachable) and SDL's dummy video driver, under a scratch HOME.

    SKIP_REQUIREMENTS_UPDATE=1 prlimit --as=$((8<<30)) scratchpad/ap/ap-venv/bin/python \\
        tools/ap_poptracker_smoke.py [--zip dist/poptracker/so2_shattered_stars_1.0.0.zip] \\
        [--port 38391] [--seconds 40] [--scratch scratchpad/packgen/smoke] [--keep]

1. PopTracker 0.36.0: the official Linux AppImage (black-sliver/PopTracker release v0.36.0),
   downloaded once into scratchpad/packgen/poptracker/, its minisign signature checked against
   PopTracker's release key (key/3B69F3B9A853297F.pub in its repo), then extracted
   (--appimage-extract). The bundled SDL 2.0.20 has no offscreen driver, so the dummy one is used.
2. A room: one open-world slot "Tester" (character items, crafts, killer moves, drops, shop
   specials, skills) generated with the scratch Archipelago (--ap-dir) and hosted on 127.0.0.1:PORT
   by MultiServer (--disable_save, its own process group, stopped at the end).
3. The port's part, played by a client of the slot: checks for the 3 lowest missing locations and
   the first craft, then the data-storage keys so2_map (map 26, Hearn's store in Arlia) and so2_story
   (chapter 12).
4. The logic (before step 3; docs/ap/poptracker.md "Logic"): PopTracker (map variant) connects to
   the fresh room with a probe added through its user-override folder (scripts/init.lua = the pack's
   own plus one LoadScript of a probe; the pack's logic, so2 and autotracking scripts load from the
   zip unchanged). The probe prints, whenever they change, the room's unchecked sections PopTracker
   itself shows as reachable (LocationSection.AccessibilityLevel: PopTracker's own evaluation of
   each section's `^$so2_access|<id>`). At three points (fresh; after every location then in logic
   is checked, by a client of the slot, with the items the server sends for them; the same again)
   that set must be the world's own: the room rebuilt from its slot data in Universal Tracker's way
   (apworld/so2/test/test_ut.py ut_rebuild / in_logic), the unchecked locations in logic. Then the
   override is removed.
5. PopTracker with --load-pack so2_shattered_stars, both variants in turn, --ap-host / --ap-slot,
   quit after --seconds with SIGINT (it saves its state on quit).
6. Checks, from PopTracker's log and its saved state (saves/so2_shattered_stars/.../autosave.json):
   the pack loaded with no Lua error and no warning; the tracker connected (the server's log has a
   PopTracker tracker joining); exactly the checked sections are cleared (step 4's and the port's
   4); every item the server sent to the slot that the pack maps is on (or counted); the chapter is
   12; the Room settings match the slot's options; the map tabs show SO2_MAP_TABS[26]. Items Only:
   loads, connects, items.
7. Story awareness (skipped with --no-story): a second room, one story-mode slot "Storyteller", on
   PORT + 1; PopTracker (map variant, the probe) connects to it. Fresh (no story key yet) its Normal
   sections are the world's own in logic and none is Inspect; then a client of the slot Sets
   so2_story_{team}_{slot} as the port does at chapters 3, 12, 36, 60 step 3000 and 60 step 4000 (a
   step-only change): each time PopTracker's Normal sections must be the reference evaluator's for
   the room's export with that position and its Inspect ("later in the story") ones the reference's
   `later`.
8. Rooms and doors (docs/ap/poptracker.md "Rooms and doors"): PopTracker (map variant) with a door
   probe in its user override connects to the first room; once connected the probe clears door A
   of map 26 (AvailableChestCount 0: what a click on "Door A" in the map popup does) and reports the
   section a few frames later. The pack's handler must set it back (a door is never checked) and open
   the destination room's tab path (the saved state's active tabs, SO2_MAP_TABS of door A's first
   destination). Also printed: the door panel's line for that door, and PopTracker's peak memory
   with the rooms' maps (VmHWM of its processes).
Prints one line per check; exit status 0 when all pass.
"""
import argparse
import asyncio
import base64
import hashlib
import json
import os
import re
import shutil
import signal
import socket
import subprocess
import sys
import time
import urllib.request
import uuid
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
UID = "so2_shattered_stars"
GAME = "Star Ocean: The Second Story"
POP_VERSION = "0.36.0"
APPIMAGE = f"PopTracker-{POP_VERSION}-x86_64.AppImage"
RELEASE = f"https://github.com/black-sliver/PopTracker/releases/download/v{POP_VERSION}/"
# PopTracker's release signing key (minisign), key/3B69F3B9A853297F.pub in black-sliver/PopTracker.
MINISIGN_KEY = "RWR/KVOoufNpOxFX4VZUW/gDaRnlr5tJkRIHNIvaC0qxbbYPTxOX22q9"
YAML = """name: Tester
game: "Star Ocean: The Second Story"
"Star Ocean: The Second Story":
  world_mode: open_world
  character_items: true
  craftsanity: true
  killer_move_checks: true
  enemy_dropsanity: first_drop
  shuffle_shops: specials
  skillsanity: skills_only
"""
SETTINGS = {"Character Items": True, "Craftsanity": True, "Killer Move Checks": True, "Cave of Trials": False}
MAP, CHAPTER = 26, 12

failures = []


def check(ok, what):
    print(f"{'ok  ' if ok else 'FAIL'} {what}", flush=True)
    if not ok:
        failures.append(what)
    return ok


def verify_minisign(data, sig_text):
    from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PublicKey
    pk = base64.b64decode(MINISIGN_KEY)
    key = Ed25519PublicKey.from_public_bytes(pk[10:])
    lines = sig_text.splitlines()
    sig = base64.b64decode(lines[1])
    if sig[2:10] != pk[2:10]:
        raise SystemExit("the AppImage's signature is from another key")
    key.verify(sig[10:], hashlib.blake2b(data, digest_size=64).digest() if sig[:2] == b"ED" else data)
    key.verify(base64.b64decode(lines[3]), sig[10:] + lines[2][len("trusted comment: "):].encode())


def poptracker(folder):
    apprun = os.path.join(folder, "squashfs-root", "AppRun")
    if os.path.exists(apprun):
        return apprun
    os.makedirs(folder, exist_ok=True)
    image = os.path.join(folder, APPIMAGE)
    if not os.path.exists(image):
        with urllib.request.urlopen(RELEASE + APPIMAGE, timeout=300) as r, open(image + ".part", "wb") as f:
            shutil.copyfileobj(r, f)
        os.replace(image + ".part", image)
    with urllib.request.urlopen(RELEASE + APPIMAGE + ".minisig", timeout=60) as r:
        sig = r.read().decode()
    with open(image, "rb") as f:
        verify_minisign(f.read(), sig)
    os.chmod(image, 0o755)
    subprocess.run([image, "--appimage-extract"], cwd=folder, check=True, stdout=subprocess.DEVNULL,
                   env={"PATH": "/usr/bin:/bin", "HOME": folder})
    return apprun


def port_open(port):
    with socket.socket() as s:
        return s.connect_ex(("127.0.0.1", port)) == 0


async def play(port, n, crafts):
    """The port's part: checks and the two data-storage keys; returns what the server sent."""
    import websockets
    async with websockets.connect(f"ws://127.0.0.1:{port}", max_size=None) as ws:
        await ws.recv()
        await ws.send(json.dumps([{"cmd": "Connect", "game": GAME, "name": "Tester", "password": "",
                                   "uuid": str(uuid.uuid4()), "items_handling": 0b111, "tags": [],
                                   "version": {"major": 0, "minor": 6, "build": 8, "class": "Version"},
                                   "slot_data": False}]))
        connected, received = None, []
        while connected is None:
            for p in json.loads(await ws.recv()):
                if p["cmd"] == "Connected":
                    connected = p
                elif p["cmd"] == "ReceivedItems":     # the starting items come with Connected
                    received += [it["item"] for it in p["items"]]
                elif p["cmd"] == "ConnectionRefused":
                    raise SystemExit(f"refused: {p}")
        missing = sorted(connected["missing_locations"])
        checks = missing[:n] + [i for i in missing if i in crafts][:1]
        team, slot = connected["team"], connected["slot"]
        await ws.send(json.dumps([{"cmd": "LocationChecks", "locations": checks}]))
        for key, value in ((f"so2_map_{team}_{slot}", {"map": MAP}),
                           (f"so2_story_{team}_{slot}", {"chapter": CHAPTER, "step": 100})):
            await ws.send(json.dumps([{"cmd": "Set", "key": key, "default": value, "want_reply": False,
                                       "operations": [{"operation": "replace", "value": value}]}]))
        end = time.time() + 3
        while time.time() < end:
            try:
                msg = await asyncio.wait_for(ws.recv(), timeout=end - time.time())
            except asyncio.TimeoutError:
                break
            for p in json.loads(msg):
                if p["cmd"] == "ReceivedItems":
                    received += [it["item"] for it in p["items"]]
        return checks, received


# The logic probe (step 4), loaded only from the smoke's user-override folder: a round over the
# room's sections, 100 per frame (PopTracker limits a frame handler's run time), then one line when
# the set changed: "so2probe <mode> <reachable> <unreachable> <ids,...>" (unchecked sections only:
# a checked one reads Cleared).
PROBE = r"""
local order, pos, ids, later, none, last = nil, 1, {}, {}, 0, nil
ScriptHost:AddOnFrameHandler("so2 smoke probe", function(elapsed)
    if not SO2.connected or SO2.exists == nil then
        return
    end
    if order == nil then
        order = {}
        for id in pairs(LOCATION_MAPPING) do
            if SO2.exists[id] then
                table.insert(order, id)
            end
        end
        table.sort(order)
        pos, ids, later, none = 1, {}, {}, 0
    end
    for _ = 1, 100 do
        local id = order[pos]
        if id == nil then
            local line = SO2_LOGIC.mode .. " " .. #ids .. " " .. none .. " " .. (#ids > 0 and table.concat(ids, ",") or "-")
                .. " " .. (#later > 0 and table.concat(later, ",") or "-")
            if line ~= last then
                last = line
                print("so2probe " .. line)
            end
            order = nil
            return
        end
        local level = Tracker:FindObjectForCode(LOCATION_MAPPING[id][1]).AccessibilityLevel
        if level == AccessibilityLevel.Normal then
            table.insert(ids, id)
        elseif level == AccessibilityLevel.Inspect then
            table.insert(later, id)
        elseif level == AccessibilityLevel.None then
            none = none + 1
        end
        pos = pos + 1
    end
end)
"""


def probe_lines(log_path):
    """The probe's lines so far: (mode, reachable ids, unreachable count, later-in-the-story ids)."""
    def ids(text):
        return set() if text == "-" else {int(x) for x in text.split(",")}
    out = []
    with open(log_path, encoding="utf-8", errors="replace") as f:
        for line in f:
            if line.startswith("so2probe "):
                parts = line.split()
                out.append((parts[1], ids(parts[4]), int(parts[3]), ids(parts[5])))
    return out


async def logic_points(port, log_path, ap_dir, points=3, timeout=60):
    """Step 4's client and checks: at each point, wait until the probe's last line is the world's own
    unchecked locations in logic; then check them all. Returns the locations checked."""
    sys.path.insert(0, ap_dir)
    from BaseClasses import ItemClassification
    from worlds.AutoWorld import AutoWorldRegister
    from worlds.so2.test.test_ut import in_logic, ut_rebuild
    import websockets
    names = AutoWorldRegister.world_types[GAME].item_id_to_name
    done = []
    async with websockets.connect(f"ws://127.0.0.1:{port}", max_size=None) as ws:
        await ws.recv()
        await ws.send(json.dumps([{"cmd": "Connect", "game": GAME, "name": "Tester", "password": "",
                                   "uuid": str(uuid.uuid4()), "items_handling": 0b111, "tags": [],
                                   "version": {"major": 0, "minor": 6, "build": 8, "class": "Version"},
                                   "slot_data": True}]))
        connected, received = None, []
        while connected is None:
            for p in json.loads(await ws.recv()):
                if p["cmd"] == "Connected":
                    connected = p
                elif p["cmd"] == "ReceivedItems":
                    received += p["items"]
        tracker = ut_rebuild(connected["slot_data"], 4243)     # the world's own logic, as UT rebuilds it
        unchecked = set(connected["missing_locations"])
        for k in range(points):
            expected = in_logic(tracker, [(names[it["item"]], ItemClassification(it["flags"])) for it in received],
                                unchecked)
            end, seen = time.time() + timeout, None
            while time.time() < end:
                lines = probe_lines(log_path)
                seen = lines[-1] if lines else None
                if seen and seen[0] == "on" and seen[1] == expected and seen[2] == len(unchecked) - len(expected):
                    break
                await asyncio.sleep(0.5)
            label = "fresh" if k == 0 else f"after {k} round{'s' if k > 1 else ''} of checks"
            ok = seen is not None and seen[0] == "on" and seen[1] == expected and not seen[3]
            what = (f"logic, {label}: PopTracker's reachable sections are the world's own ({len(expected)} in "
                    f"logic of {len(unchecked)} unchecked, {len(received)} items)")
            if not check(ok and seen[2] == len(unchecked) - len(expected), what):
                if seen:
                    print(f"     PopTracker: mode {seen[0]}, {len(seen[1])} reachable, {seen[2]} not; "
                          f"missing {sorted(expected - seen[1])[:8]}, extra {sorted(seen[1] - expected)[:8]}")
                return done
            if k + 1 == points:
                break
            checks = sorted(expected)
            await ws.send(json.dumps([{"cmd": "LocationChecks", "locations": checks}]))
            done += checks
            unchecked -= set(checks)
            end = time.time() + 3
            while time.time() < end:
                try:
                    msg = await asyncio.wait_for(ws.recv(), timeout=end - time.time())
                except asyncio.TimeoutError:
                    break
                for p in json.loads(msg):
                    if p["cmd"] == "ReceivedItems":
                        received += p["items"]
    return done


def env_for(root):
    """PopTracker's environment: nothing of the user's (no display, no D-Bus, an empty PATH)."""
    home = os.path.join(root, "home")
    return {"HOME": home, "XDG_CONFIG_HOME": os.path.join(home, ".config"),
            "XDG_DATA_HOME": os.path.join(home, ".local", "share"), "XDG_CACHE_HOME": os.path.join(home, ".cache"),
            "XDG_RUNTIME_DIR": os.path.join(root, "run"), "PATH": os.path.join(root, "bin"), "LANG": "C.UTF-8",
            "SDL_VIDEODRIVER": "dummy", "SDL_AUDIODRIVER": "dummy",          # SDL2's names
            "SDL_VIDEO_DRIVER": "dummy", "SDL_AUDIO_DRIVER": "dummy"}


def stop(p):
    p.send_signal(signal.SIGINT)        # SDL turns it into a quit: PopTracker saves its state
    try:
        p.wait(timeout=30)
    except subprocess.TimeoutExpired:
        os.killpg(p.pid, signal.SIGKILL)
        p.wait()


STORY_YAML = """name: Storyteller
game: "Star Ocean: The Second Story"
"Star Ocean: The Second Story":
  world_mode: story
  killer_move_checks: true
  cave_of_trials: true
  boss_checks: true
"""
# Step 7's positions, as the port writes them (the last two: a step-only change at the Final Save Point).
STORY_POSITIONS = [(3, 0), (12, 0), (36, 0), (60, 3000), (60, 4000)]


async def story_points(port, log_path, ap_dir, timeout=60):
    """Step 7's clients and checks: a tracker-like client reads the slot data and the items; a client
    of the slot as the port (items_handling 0b001) Sets so2_story_{team}_{slot}. Fresh (no key yet):
    PopTracker shows the world's own in logic and nothing later in the story; at each position its
    Normal sections are the reference evaluator's (tools/ap_tracker_logic.py) for the room's export with
    that position, its Inspect ones the reference's `later`. Returns the number of points that passed."""
    sys.path.insert(0, ap_dir)
    sys.path.insert(0, HERE)
    from BaseClasses import ItemClassification
    from worlds.AutoWorld import AutoWorldRegister
    from worlds.so2.test.test_ut import in_logic, ut_rebuild
    from ap_tracker_logic import TrackerLogic
    import websockets
    names = AutoWorldRegister.world_types[GAME].item_id_to_name
    hello = lambda handling, data: json.dumps([{
        "cmd": "Connect", "game": GAME, "name": "Storyteller", "password": "", "uuid": str(uuid.uuid4()),
        "items_handling": handling, "tags": [], "slot_data": data,
        "version": {"major": 0, "minor": 6, "build": 8, "class": "Version"}}])
    passed = 0
    async with websockets.connect(f"ws://127.0.0.1:{port}", max_size=None) as ws, \
            websockets.connect(f"ws://127.0.0.1:{port}", max_size=None) as game:
        await ws.recv()
        await ws.send(hello(0b111, True))
        connected, received = None, []
        end, hard = time.time() + 2, time.time() + 30
        while connected is None or time.time() < end:
            if time.time() > hard:
                raise RuntimeError("story: the tracker-like client got no Connected")
            try:
                msg = await asyncio.wait_for(ws.recv(), timeout=max(0.1, end - time.time()))
            except asyncio.TimeoutError:
                continue
            for p in json.loads(msg):
                if p["cmd"] == "Connected":
                    connected = p
                elif p["cmd"] == "ReceivedItems":
                    received += p["items"]
        await asyncio.wait_for(game.recv(), 30)
        await game.send(hello(0b001, False))
        while not any(p["cmd"] == "Connected" for p in json.loads(await asyncio.wait_for(game.recv(), 30))):
            pass
        key = f"so2_story_{connected['team']}_{connected['slot']}"
        unchecked = set(connected["missing_locations"])
        tracker = ut_rebuild(connected["slot_data"], 4244)
        plain = in_logic(tracker, [(names[it["item"]], ItemClassification(it["flags"])) for it in received], unchecked)
        reference = TrackerLogic(connected["slot_data"]["tracker"])
        counts = {}
        for it in received:
            counts[it["item"]] = counts.get(it["item"], 0) + 1
        check("story" in connected["slot_data"]["tracker"], "story: the room's tracker export has the `story` key")
        check(reference.evaluate(counts).locations & unchecked == plain,
              f"story: the reference's plain logic is the world's own ({len(plain)} in logic)")
        points = [(None, None)] + [({"chapter": c, "step": st}, f"chapter {c} step {st}") for c, st in STORY_POSITIONS]
        last_normal = None
        for value, label in points:
            if value is not None:
                await game.send(json.dumps([{"cmd": "Set", "key": key, "default": value, "want_reply": False,
                                             "operations": [{"operation": "replace", "value": value}]}]))
                view = reference.evaluate(counts, value)
                normal, later = view.locations & unchecked, set(view.later) & unchecked
            else:
                normal, later, label = plain, set(), "fresh, no story key yet"
            end, seen = time.time() + timeout, None
            while time.time() < end:
                lines = probe_lines(log_path)
                seen = lines[-1] if lines else None
                if seen and seen[0] == "on" and seen[1] == normal and seen[3] == later:
                    break
                await asyncio.sleep(0.5)
            ok = seen is not None and seen[0] == "on" and seen[1] == normal and seen[3] == later \
                and seen[2] == len(unchecked) - len(normal) - len(later)
            if check(ok, f"story, {label}: PopTracker's sections: {len(normal)} Normal, {len(later)} Inspect (later "
                         f"in the story), the rest None, as the reference says"):
                passed += 1
            elif seen:
                print(f"     PopTracker: mode {seen[0]}, {len(seen[1])} Normal, {len(seen[3])} Inspect, {seen[2]} None")
            if value is not None and last_normal is not None and label.endswith("step 4000"):
                check(normal != last_normal, "story: the step-only change (chapter 60 step 3000 -> 4000) changed them")
            last_normal = normal
    return passed


def run_story(apprun, root, pack_dir, port, ap_dir):
    """Step 7: a story-mode room on PORT; PopTracker with the probe connected to it."""
    return with_probe(apprun, root, pack_dir, port, "Storyteller", "poptracker-story.log",
                      lambda log_path: story_points(port, log_path, ap_dir))


def with_probe(apprun, root, pack_dir, port, slot, log_name, drive):
    """PopTracker (map variant) on PORT as SLOT with the probe in its user override, `drive(log)`
    meanwhile; the override removed afterwards."""
    override = os.path.join(root, "home", "PopTracker", "user-override", UID)
    os.makedirs(os.path.join(override, "scripts"))
    with open(os.path.join(pack_dir, "scripts", "init.lua"), encoding="utf-8") as f:
        init = f.read()
    with open(os.path.join(override, "scripts", "init.lua"), "w", encoding="utf-8") as f:
        f.write(init + '\nScriptHost:LoadScript("scripts/so2_smoke_probe.lua")\n')
    with open(os.path.join(override, "scripts", "so2_smoke_probe.lua"), "w", encoding="utf-8") as f:
        f.write(PROBE)
    log_path = os.path.join(root, log_name)
    with open(log_path, "w") as log:
        p = subprocess.Popen([apprun, "--load-pack", UID, "--pack-variant", "map", "--ap-host", f"localhost:{port}",
                              "--ap-slot", slot], env=env_for(root), stdin=subprocess.DEVNULL, stdout=log,
                             stderr=subprocess.STDOUT, start_new_session=True)
        try:
            done = asyncio.run(drive(log_path))
        finally:
            stop(p)
    shutil.rmtree(os.path.join(root, "home", "PopTracker", "user-override"))
    text = open(log_path, encoding="utf-8", errors="replace").read()
    bad = [ln for ln in text.splitlines() if re.search(r"WARNING|[Ee]rror|attempt to|stack traceback", ln)
           and "TLS handshake" not in ln and "Could not fetch" not in ln]
    check("overriding from" in text and "so2_smoke_probe" in text and not bad,
          f"{slot}: the probe ran from the user override, no Lua error or warning" + (f": {bad[:3]}" if bad else ""))
    return done


# Step 8's probe: once connected, a door section "clicked" (cleared) from Lua, then its count again.
DOOR_PROBE = r"""
local frames, clicked = 0, false
ScriptHost:AddOnFrameHandler("so2 smoke doors", function(elapsed)
    if not SO2.connected then
        return
    end
    frames = frames + 1
    local sec = Tracker:FindObjectForCode("@" .. "%(section)s")
    if frames == 60 and sec then
        local item = SO2_DOORS.items[%(door)d]
        print("so2door line " .. (item and item.BadgeText or "?"))
        print("so2door click " .. sec.FullID .. " " .. sec.AvailableChestCount)
        sec.AvailableChestCount = 0
        clicked = true
    elseif clicked and frames == 90 then
        print("so2door after " .. sec.AvailableChestCount .. " " .. sec.ChestCount)
    end
end)
"""


def peak_memory(pgid):
    """VmHWM (kB) summed over the processes of a process group."""
    total = 0
    for pid in os.listdir("/proc"):
        if not pid.isdigit():
            continue
        try:
            stat = open(f"/proc/{pid}/stat").read()
            if int(stat.rsplit(")", 1)[1].split()[3]) != pgid:
                continue
            for line in open(f"/proc/{pid}/status"):
                if line.startswith("VmHWM:"):
                    total += int(line.split()[1])
        except (OSError, ValueError, IndexError):
            continue
    return total


def run_doors(apprun, root, pack_dir, port, tabs):
    """Step 8: a door click in the real PopTracker."""
    doors = open(os.path.join(pack_dir, "scripts", "doors_data.lua"), encoding="utf-8").read()
    m = re.search(r'^\s*\["(Doors/26 A/Door A)"\] = (\d+),$', doors, re.M)
    if not check(m is not None, "doors: door A of map 26 is in the door table"):
        return
    section, door = m.group(1), int(m.group(2))
    way = re.search(r"^\s*\[%d\] = \{map = 26, letter = \"A\", code = \"[^\"]+\", to = \{(.*)\}\},$" % door, doors, re.M)
    dests = [(int(t.group(1)), "cond = true" in t.group(0)) for t in re.finditer(r"\{map = (\d+)[^}]*\}", way.group(1))]
    dest = next((d for d, cond in dests if not cond), dests[0][0])
    override = os.path.join(root, "home", "PopTracker", "user-override", UID)
    os.makedirs(os.path.join(override, "scripts"))
    with open(os.path.join(pack_dir, "scripts", "init.lua"), encoding="utf-8") as f:
        init = f.read()
    with open(os.path.join(override, "scripts", "init.lua"), "w", encoding="utf-8") as f:
        f.write(init + '\nScriptHost:LoadScript("scripts/so2_smoke_doors.lua")\n')
    with open(os.path.join(override, "scripts", "so2_smoke_doors.lua"), "w", encoding="utf-8") as f:
        f.write(DOOR_PROBE % {"section": section, "door": door})
    log_path = os.path.join(root, "poptracker-doors.log")
    peak = 0
    with open(log_path, "w") as log:
        p = subprocess.Popen([apprun, "--load-pack", UID, "--pack-variant", "map", "--ap-host", f"localhost:{port}",
                              "--ap-slot", "Tester"], env=env_for(root), stdin=subprocess.DEVNULL, stdout=log,
                             stderr=subprocess.STDOUT, start_new_session=True)
        try:
            deadline = time.time() + 90
            while time.time() < deadline and p.poll() is None:
                peak = max(peak, peak_memory(p.pid))
                if "so2door after" in open(log_path, encoding="utf-8", errors="replace").read():
                    time.sleep(1)
                    break
                time.sleep(0.5)
        finally:
            stop(p)
    shutil.rmtree(os.path.join(root, "home", "PopTracker", "user-override"))
    text = open(log_path, encoding="utf-8", errors="replace").read()
    line = re.search(r"so2door line (.*)", text)
    print(f"info  doors: door A of map 26 -> {dests}; panel line {line.group(1) if line else '?'!r}; "
          f"PopTracker peak memory {peak // 1024} MB")
    after = re.search(r"so2door after (\d+) (\d+)", text)
    check("so2door click " + section + " 1" in text, f"doors: {section} clicked (it had 1 of 1)")
    check(after is not None and after.group(1) == after.group(2) == "1",
          "doors: the pack set the door back at once (a door is never checked)")
    bad = [ln for ln in text.splitlines() if re.search(r"WARNING|[Ee]rror|attempt to|stack traceback", ln)
           and "TLS handshake" not in ln and "Could not fetch" not in ln]
    check(not bad, "doors: no Lua error or warning" + (f": {bad[:3]}" if bad else ""))
    state_dir = os.path.join(root, "home", ".config", "PopTracker", "saves", UID)
    found = [os.path.join(b, f) for b, _, fs in os.walk(state_dir) for f in fs
             if f == "autosave.json" and os.path.basename(b) == "map"]
    if not check(bool(found), "doors: state saved"):
        return
    state = json.load(open(found[0], encoding="utf-8"))
    hints = [h[1] for h in state.get("ui_hints", []) if h[0] == "ActivateTab"]
    check(all(t in hints for t in tabs[dest]) and tabs[dest][-1] == str(dest),
          f"doors: the click opened room {dest}'s tab {tabs[dest]}")
    sec = state["tracker"]["sections"].get(section)
    check(not (isinstance(sec, dict) and sec.get("cleared")), "doors: the door section is not saved as checked")


def run_logic(apprun, root, pack_dir, port, ap_dir):
    """Step 4: PopTracker with the probe, the three points."""
    return with_probe(apprun, root, pack_dir, port, "Tester", "poptracker-logic.log",
                      lambda log_path: logic_points(port, log_path, ap_dir))


def lua_table(path, name):
    text = open(path, encoding="utf-8").read()
    body = text[text.index(name + " = {"):]
    return {int(m.group(1)): re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(2))
            for m in re.finditer(r"^\s*\[(\d+)\] = \{(.*)\},$", body, re.M)}


def run_poptracker(apprun, root, variant, port, seconds):
    home = os.path.join(root, "home")
    log_path = os.path.join(root, f"poptracker-{variant}.log")
    with open(log_path, "w") as log:
        p = subprocess.Popen([apprun, "--load-pack", UID, "--pack-variant", variant, "--ap-host",
                              f"localhost:{port}", "--ap-slot", "Tester"], env=env_for(root), stdin=subprocess.DEVNULL,
                             stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
        try:
            p.wait(timeout=seconds)
        except subprocess.TimeoutExpired:
            stop(p)
    log = open(log_path, encoding="utf-8", errors="replace").read()
    state = os.path.join(home, ".config", "PopTracker", "saves", UID)
    found = [os.path.join(b, f) for b, _, fs in os.walk(state) for f in fs
             if f == "autosave.json" and os.path.basename(b) == variant]
    return log, json.load(open(found[0], encoding="utf-8")) if found else None


def story_room(a, root, py, ap_env):
    """Step 7's room: one story-mode slot "Storyteller", hosted on PORT + 1. (server, its log) or None."""
    players, out = os.path.join(root, "players_story"), os.path.join(root, "out_story")
    os.makedirs(players)
    os.makedirs(out)
    with open(os.path.join(players, "Storyteller.yaml"), "w") as f:
        f.write(STORY_YAML)
    gen = subprocess.run([py, "Generate.py", "--player_files_path", players, "--outputpath", out, "--seed", "4243"],
                         cwd=a.ap_dir, env=ap_env, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                         stderr=subprocess.STDOUT, text=True)
    rooms = [f for f in os.listdir(out) if f.endswith(".zip")]
    if not check(gen.returncode == 0 and len(rooms) == 1, "story: room generated (story mode)"):
        print(gen.stdout[-2000:])
        return None
    port = a.port + 1
    if port_open(port):
        raise SystemExit(f"port {port} is in use")
    log = open(os.path.join(root, "server-story.log"), "w")
    server = subprocess.Popen([py, "MultiServer.py", os.path.join(out, rooms[0]), "--host", "127.0.0.1",
                               "--port", str(port), "--disable_save", "--loglevel", "info"], cwd=a.ap_dir,
                              env=ap_env, stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT,
                              start_new_session=True)
    for _ in range(300):
        if port_open(port):
            break
        time.sleep(0.1)
    check(port_open(port), f"story: server on 127.0.0.1:{port} (PID {server.pid})")
    return server, log


def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    p.add_argument("--zip", default=os.path.join(REPO, "dist", "poptracker", f"{UID}_1.0.0.zip"))
    p.add_argument("--ap-dir", default=os.path.join(REPO, "scratchpad", "ap", "Archipelago"))
    p.add_argument("--poptracker", default=os.path.join(REPO, "scratchpad", "packgen", "poptracker"))
    p.add_argument("--scratch", default=os.path.join(REPO, "scratchpad", "packgen", "smoke"))
    p.add_argument("--port", type=int, default=38391)
    p.add_argument("--seconds", type=float, default=40)
    p.add_argument("--keep", action="store_true", help="keep the scratch folder")
    p.add_argument("--no-story", action="store_true", help="skip step 7 (the story-mode room)")
    a = p.parse_args()
    apprun = poptracker(os.path.abspath(a.poptracker))
    root = os.path.abspath(a.scratch)
    shutil.rmtree(root, ignore_errors=True)
    for d in ("home/PopTracker/packs", "run", "bin", "players", "out"):
        os.makedirs(os.path.join(root, d))
    os.chmod(os.path.join(root, "run"), 0o700)
    shutil.copy(a.zip, os.path.join(root, "home", "PopTracker", "packs"))
    with open(os.path.join(root, "players", "Tester.yaml"), "w") as f:
        f.write(YAML)
    ap_env = dict(os.environ, SKIP_REQUIREMENTS_UPDATE="1")
    py = sys.executable
    gen = subprocess.run([py, "Generate.py", "--player_files_path", os.path.join(root, "players"), "--outputpath",
                          os.path.join(root, "out"), "--seed", "4242"], cwd=a.ap_dir, env=ap_env,
                         stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    rooms = [f for f in os.listdir(os.path.join(root, "out")) if f.endswith(".zip")]
    if not check(gen.returncode == 0 and len(rooms) == 1, "room generated"):
        print(gen.stdout[-2000:])
        return 1
    if port_open(a.port):
        raise SystemExit(f"port {a.port} is in use")
    server_log = open(os.path.join(root, "server.log"), "w")
    server = subprocess.Popen([py, "MultiServer.py", os.path.join(root, "out", rooms[0]), "--host", "127.0.0.1",
                               "--port", str(a.port), "--disable_save", "--loglevel", "info"], cwd=a.ap_dir,
                              env=ap_env, stdin=subprocess.DEVNULL, stdout=server_log, stderr=subprocess.STDOUT,
                              start_new_session=True)
    try:
        for _ in range(300):
            if port_open(a.port):
                break
            time.sleep(0.1)
        check(port_open(a.port), f"server on 127.0.0.1:{a.port} (PID {server.pid})")
        pack_dir = os.path.join(root, "pack")
        with zipfile.ZipFile(a.zip) as z:
            z.extractall(pack_dir)
        loc_map = lua_table(os.path.join(pack_dir, "scripts", "autotracking", "location_mapping.lua"),
                            "LOCATION_MAPPING")
        item_map = lua_table(os.path.join(pack_dir, "scripts", "autotracking", "item_mapping.lua"), "ITEM_MAPPING")
        tabs = lua_table(os.path.join(pack_dir, "scripts", "map_tabs.lua"), "SO2_MAP_TABS")
        names = {it["codes"]: it["name"] for it in json.load(open(os.path.join(pack_dir, "items", "items.json")))
                 if "codes" in it}
        crafts = {i for i, (path,) in loc_map.items() if path.startswith("@Item Creation/")}
        earlier = run_logic(apprun, root, pack_dir, a.port, os.path.abspath(a.ap_dir))
        checks, received = asyncio.run(play(a.port, 3, crafts))
        check(len(checks) == 4, f"the port's part: {len(checks)} checks, {len(received)} items sent, keys written")
        for variant in ("map", "items_only"):
            log, state = run_poptracker(apprun, root, variant, a.port, a.seconds)
            lines = log.splitlines()
            check(f"{UID} 1.0.0 ({variant}) loaded" in log, f"{variant}: the pack loaded")
            bad = [ln for ln in lines if re.search(r"WARNING|[Ee]rror|attempt to|stack traceback", ln)
                   and "TLS handshake" not in ln and "Could not fetch" not in ln]
            check(not bad, f"{variant}: no Lua error or warning" + (f": {bad[:3]}" if bad else ""))
            check("APClient: Server connected" in log, f"{variant}: connected to the room")
            if not check(state is not None, f"{variant}: state saved"):
                continue
            check(state["pack"]["uid"] == UID and state["pack"]["version"] == "1.0.0"
                  and state["pack"]["variant"] == variant, f"{variant}: the state is this pack's")
            t = state["tracker"]
            on = set()
            for key, v in t["json_items"].items():
                label = t["json_item_ids"].get(key, key) if isinstance(t["json_item_ids"], dict) else key
                if v.get("state", [0])[0] or v.get("count"):
                    on.add(label.split(":", 1)[1])
            want = {names[c] for i in received if i in item_map for c in item_map[i][::2] if c in names}
            check(len(want) >= 3 and want <= on, f"{variant}: the received items are on ({sorted(want)})")
            if variant == "map":
                cleared = {k for k, v in t["sections"].items() if isinstance(v, dict) and v.get("cleared")}
                expected = {loc_map[i][0][1:] for i in earlier + checks}
                check(cleared == expected, f"map: the {len(expected)} checked sections are cleared, no other")
                chapter = next((v.get("count") for k, v in t["json_items"].items()
                                if t["json_item_ids"].get(k, "").endswith(":Story chapter (from the game)")), None)
                check(chapter == CHAPTER, f"map: chapter {chapter} from so2_story")
                check(all((name in on) == want_on for name, want_on in SETTINGS.items()),
                      "map: the Room settings follow the slot data")
                hints = [h[1] for h in state.get("ui_hints", []) if h[0] == "ActivateTab"]
                # the default tabs are each tab bar's first: Expel's is its world map
                check(all(title in hints for title in tabs[MAP]) and "Expel World Map" not in hints
                      and tabs[MAP][-1] == str(MAP), f"map: map {MAP} switched the tabs to its room, {tabs[MAP]}")
        joined = open(os.path.join(root, "server.log")).read()
        check(joined.count("['PopTracker', 'NoText', 'Tracker']") >= 2, "server: PopTracker joined as a tracker")
        run_doors(apprun, root, pack_dir, a.port, tabs)
        if not a.no_story:
            story = story_room(a, root, py, ap_env)
            if story is not None:
                try:
                    passed = run_story(apprun, root, pack_dir, a.port + 1, os.path.abspath(a.ap_dir))
                    check(passed == len(STORY_POSITIONS) + 1, f"story: {passed} of {len(STORY_POSITIONS) + 1} points")
                finally:
                    os.killpg(story[0].pid, signal.SIGTERM)
                    story[0].wait(timeout=30)
                    story[1].close()
    finally:
        os.killpg(server.pid, signal.SIGTERM)
        server.wait(timeout=30)
        server_log.close()
        if not a.keep and not failures:
            shutil.rmtree(root, ignore_errors=True)
    print(f"{'FAILED' if failures else 'passed'}: {len(failures)} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
