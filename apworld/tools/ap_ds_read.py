#!/usr/bin/env python3
"""Reads the port's tracker keys from an Archipelago server's data storage (docs/apworld/data_storage.md).

    ap_ds_read.py URL SLOT [--watch SECONDS] [--done FILE] [--game GAME]

Logs in to SLOT as a tracker (tags ["Tracker"], no items), takes the team and slot number from
the Connected packet, and asks for so2_map_{team}_{slot} and so2_story_{team}_{slot} (Get), so the
key names are the server's, not the port's. With --watch it also subscribes to both (SetNotify)
and prints every change for up to SECONDS seconds, or until FILE exists (--done). One line per
value, the JSON compact with sorted keys:

    connected team 0 slot 1
    retrieved so2_map_0_1 {"map":18}
    retrieved so2_story_0_1 {"chapter":3,"step":100}
    set so2_map_0_1 {"map":26}

A key the port never wrote reads as `null`. Run it with the Archipelago venv's Python
(scratchpad/ap/ap-venv/bin/python: it has `websockets`); apworld/tools/ap_e2e_so2.sh calls it. It talks
only to the server it is given (the e2e's local one).
"""
import argparse
import asyncio
import json
import os
import sys
import time

import websockets


def compact(value):
    return json.dumps(value, separators=(",", ":"), sort_keys=True)


async def main(args):
    say = lambda *a: print(*a, flush=True)
    async with websockets.connect(args.url, max_size=None) as ws:
        await asyncio.wait_for(ws.recv(), timeout=10)  # RoomInfo
        await ws.send(json.dumps([{
            "cmd": "Connect", "game": args.game, "name": args.slot, "password": args.password,
            "uuid": "ap-ds-read", "version": {"major": 0, "minor": 6, "build": 8, "class": "Version"},
            "items_handling": 0, "tags": ["Tracker"], "slot_data": False}]))
        keys = []
        retrieved = False
        deadline = time.time() + 10
        while True:
            now = time.time()
            if retrieved and (not args.watch or now >= deadline or (args.done and os.path.exists(args.done))):
                return 0
            if not retrieved and now >= deadline:
                say("timeout: no answer from the server")
                return 1
            try:
                text = await asyncio.wait_for(ws.recv(), timeout=0.5)
            except asyncio.TimeoutError:
                continue
            for p in json.loads(text):
                cmd = p.get("cmd")
                if cmd == "ConnectionRefused":
                    say("refused", p.get("errors"))
                    return 1
                if cmd == "Connected":
                    team, slot = p["team"], p["slot"]
                    say(f"connected team {team} slot {slot}")
                    keys = [f"so2_map_{team}_{slot}", f"so2_story_{team}_{slot}"]
                    packets = [{"cmd": "Get", "keys": keys}]
                    if args.watch:
                        packets.append({"cmd": "SetNotify", "keys": keys})
                    await ws.send(json.dumps(packets))
                elif cmd == "Retrieved":
                    for k in keys:
                        say(f"retrieved {k} {compact(p['keys'].get(k))}")
                    retrieved = True
                    deadline = time.time() + (args.watch or 0)
                elif cmd == "SetReply" and p.get("key") in keys:
                    say(f"set {p['key']} {compact(p.get('value'))}")


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("url")
    ap.add_argument("slot")
    ap.add_argument("--watch", type=float, default=0, help="print changes for this many seconds")
    ap.add_argument("--done", help="stop watching once this file exists")
    ap.add_argument("--game", default="Star Ocean: The Second Story")
    ap.add_argument("--password", default="")
    sys.exit(asyncio.run(main(ap.parse_args())))
