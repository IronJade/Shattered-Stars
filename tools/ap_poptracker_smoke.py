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
4. PopTracker with --load-pack so2_shattered_stars, both variants in turn, --ap-host / --ap-slot,
   quit after --seconds with SIGINT (it saves its state on quit).
5. Checks, from PopTracker's log and its saved state (saves/so2_shattered_stars/.../autosave.json):
   the pack loaded with no Lua error and no warning; the tracker connected (the server's log has a
   PopTracker tracker joining); exactly the 4 checked sections are cleared; every item the server
   sent to the slot that the pack maps is on (or counted); the chapter is 12; the Room settings
   match the slot's options; the map tabs show SO2_MAP_TABS[26]. Items Only: loads, connects, items.
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


def lua_table(path, name):
    text = open(path, encoding="utf-8").read()
    body = text[text.index(name + " = {"):]
    return {int(m.group(1)): re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(2))
            for m in re.finditer(r"^\s*\[(\d+)\] = \{(.*)\},$", body, re.M)}


def run_poptracker(apprun, root, variant, port, seconds):
    home = os.path.join(root, "home")
    env = {"HOME": home, "XDG_CONFIG_HOME": os.path.join(home, ".config"),
           "XDG_DATA_HOME": os.path.join(home, ".local", "share"), "XDG_CACHE_HOME": os.path.join(home, ".cache"),
           "XDG_RUNTIME_DIR": os.path.join(root, "run"), "PATH": os.path.join(root, "bin"), "LANG": "C.UTF-8",
           "SDL_VIDEODRIVER": "dummy", "SDL_AUDIODRIVER": "dummy",          # SDL2's names
           "SDL_VIDEO_DRIVER": "dummy", "SDL_AUDIO_DRIVER": "dummy"}
    log_path = os.path.join(root, f"poptracker-{variant}.log")
    with open(log_path, "w") as log:
        p = subprocess.Popen([apprun, "--load-pack", UID, "--pack-variant", variant, "--ap-host",
                              f"localhost:{port}", "--ap-slot", "Tester"], env=env, stdin=subprocess.DEVNULL,
                             stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
        try:
            p.wait(timeout=seconds)
        except subprocess.TimeoutExpired:
            p.send_signal(signal.SIGINT)        # SDL turns it into a quit: PopTracker saves its state
            try:
                p.wait(timeout=30)
            except subprocess.TimeoutExpired:
                os.killpg(p.pid, signal.SIGKILL)
                p.wait()
    log = open(log_path, encoding="utf-8", errors="replace").read()
    state = os.path.join(home, ".config", "PopTracker", "saves", UID)
    found = [os.path.join(b, f) for b, _, fs in os.walk(state) for f in fs
             if f == "autosave.json" and os.path.basename(b) == variant]
    return log, json.load(open(found[0], encoding="utf-8")) if found else None


def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    p.add_argument("--zip", default=os.path.join(REPO, "dist", "poptracker", f"{UID}_1.0.0.zip"))
    p.add_argument("--ap-dir", default=os.path.join(REPO, "scratchpad", "ap", "Archipelago"))
    p.add_argument("--poptracker", default=os.path.join(REPO, "scratchpad", "packgen", "poptracker"))
    p.add_argument("--scratch", default=os.path.join(REPO, "scratchpad", "packgen", "smoke"))
    p.add_argument("--port", type=int, default=38391)
    p.add_argument("--seconds", type=float, default=40)
    p.add_argument("--keep", action="store_true", help="keep the scratch folder")
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
                expected = {loc_map[i][0][1:] for i in checks}
                check(cleared == expected, f"map: the {len(expected)} checked sections are cleared, no other")
                chapter = next((v.get("count") for k, v in t["json_items"].items()
                                if t["json_item_ids"].get(k, "").endswith(":Story chapter (from the game)")), None)
                check(chapter == CHAPTER, f"map: chapter {chapter} from so2_story")
                check(all((name in on) == want_on for name, want_on in SETTINGS.items()),
                      "map: the Room settings follow the slot data")
                hints = [h[1] for h in state.get("ui_hints", []) if h[0] == "ActivateTab"]
                # the default tabs are each tab bar's first: Expel's is its world map
                check(all(title in hints for title in tabs[MAP]) and "Expel World Map" not in hints,
                      f"map: map {MAP} switched the tabs to {tabs[MAP]}")
        joined = open(os.path.join(root, "server.log")).read()
        check(joined.count("['PopTracker', 'NoText', 'Tracker']") >= 2, "server: PopTracker joined as a tracker")
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
