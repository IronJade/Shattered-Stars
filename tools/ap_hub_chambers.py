#!/usr/bin/env python3
"""The hub's boss chambers, replayed headless (docs/handoff/hubchambers.md; docs/ap/seed_format.md
"Hub"; docs/handoff/hub.md D2 / D10).

Each chamber of docs/ap/hub_bosses.json is played from the hub state (docs/ap/hub_state.json:
chapter 60 step 4000, the hub's flags and globals, the hero's game) with a hub seed holding every
`bosses` row (as the APWorld copies them) and one boss location per row. A run starts on the
chamber's approach map (`--map`), fires the door the story walks through into the chamber and
then the scene's trigger (event sweep play mode: SS_SWEEP_PLAY), wins the battle with the battle
stand-in (SS_BATTLE_STUB=1) and plays on until the player has control again. It asserts:

  view    the row's story view turns on (`story view on: map M (NAME, encounter E)`), or for a row
          without a view the "no story view" line
  scene   the encounter's battle starts ([ap] a battle) and ends in a victory (outcome 0)
  check   the boss location's check ([ap] check ID)
  goal    the victory is recorded in goal_won
  end     the view ends where the scene ends (the transfer out: `map N loaded`, a cut_to, or free
          play after the battle)
  state   at the end: chapter 60 step 4000, and only the expected flags changed (the guard, the
          row's `keep` flags, key items / chests the scene gives); globals besides the map-entry
          counter, the speaker byte and the listed ones unchanged
  refire  (in-place endings, `sweep_after`) once the play is done the event sweep lists the room's
          ACTIVE entries (what the view's set-up and scene left registered) and fires each in
          turn, reloading the room between them at the real state: no second boss battle, no
          second view (the play steps alone can't test this: they fire a slot even when the
          scene removed it)

A few specs are walks, not chambers (`walk`: Fienal's last corridor on to Indalecio at 685). Every
chamber runs in Claude's game and in Rena's (`KEY_r`, a copy unless the route differs) except the
game-filtered ones. The seed also holds the hub's P5 door rows (P5_DOORS: the arena 305, and the
proposed Fienal 679 -> 674 redirect).

One line per chamber; the exit code is non-zero when any chamber fails. Logs, dumps and the seed
go to --work (default scratchpad/hubchambers). Every game process runs under tools/memcap.py
(SS_MEM_CAP_GB, default 4 here), at most --jobs (default 2, the limit) at once. A full run takes
about an hour with two jobs.

Usage:
  tools/ap_hub_chambers.py [--only KEY,...] [--bin PATH] [--jobs N] [--work DIR] [-v]
  tools/ap_hub_chambers.py --list MAP [--game rena] [--route MAP:Tn,... --frames N]
                           the sweep's entry list of a map (to write specs), after walking a route
  tools/ap_hub_chambers.py --keys                      the chamber keys
Needs SS_DISC1 (disc 1's .cue; disc 2 is found beside it). Known failure: courage (Claude's game)
may abort at exit on the field VM's ext 0xCB stack underflow (docs/handoff/hubchambers.md).
"""
import argparse
import concurrent.futures
import json
import os
import re
import struct
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
from memcap import run_capped  # noqa: E402

PARTY = "1,10"   # Rena and Noel join the --map start's Claude (as the late proof's runs)
FILLER = {"player": 1, "kind": "item", "game_id": 757, "name": "Blueberry", "classification": "filler"}
SEED_ROW_KEYS = ("encounter", "name", "map", "maps", "view", "game")   # what the APWorld copies
# The hub's P5 doors as the APWorld writes them (apworld/so2/hub.py P5_DOORS; hubcore's port
# `to.while_unbeaten`): the arena antechamber 304's door to 296 leads to Marsilio's arena 305
# while 532 is unbeaten.
P5_DOORS = [{"id": 19929509, "to": {"map": 305, "x": -13, "y": 0, "z": 276, "dir": 271, "while_unbeaten": 532},
             "name": "Fun City Fighting Arena (map 304 -> 305 while Marsilio is unbeaten)"},
            # Proposed (hubchambers, 2026-10-25): Fienal's pair 3 (Cyril, 674) entered from the west (679)
            # while unbeaten walks the hero into the enemies and the scene never ends; while unbeaten,
            # that door lands on the east arrival the story uses (676's).
            {"id": 44502760, "to": {"map": 674, "x": 882, "y": 0, "z": 30, "dir": 180, "while_unbeaten": 538},
             "name": "Fienal (679 -> 674: Cyril's east side while unbeaten)"}]

# Global words a scene changes as a matter of course (not story state): the map-entry counter, the
# speaker byte (G[0xc6], in the word at 0xc4), the map / position / music words (G[0xa8]-G[0xc3]).
NOISE_WORDS = {0x1D8}
NOISE_BYTES = {0xC6}

# ---- The chambers ------------------------------------------------------------------------------
# key: the run's name. enc: the boss encounter (hub_bosses.json row). game: the hero's game.
# map: the --map start (the approach room); pos: --map-pos (an arrival the story uses) if the map's
# own start is not where the door / trigger fires from. play: SS_SWEEP_PLAY steps: "MAP:Tn" fires
# trigger slot n (a door or a scene region), "MAP:Kn" talks to actor n. code: the routines (byte
# offsets) the T steps must start (checked against the log, so a changed slot order fails loudly).
# end: ("map", N) the view ends at the load of map N; ("free",) at free play in the chamber.
# flags_on / flags_off: the real flags the run may set / clear (anything else fails). words: other
# global words it may change. refire: steps (after the view's end) that fire the trigger again.
# frames: the run's frame limit.
CHAMBERS = [
    # Salva Drift: 68's door (0x667c) -> 61; the set-up starts the scene (Claude's game); the
    # aftermath goes to Arlia 53.
    dict(key="alentax", enc=191, game="claude", map=68, play=["68:T6"], code={0: 0x667C},
         end=("map", 53), final_map=53, frames=9000),
    # ... Rena's game: the scene starts from a region instead (0x5354, registered while !flag 1).
    dict(key="alentax_r", enc=191, game="rena", map=68, play=["68:T6", "61:T2"], code={0: 0x667C, 1: 0x5354},
         end=("map", 53), final_map=53, frames=16000),
    # Hoffmann Ruins: 409's door (0x6670) -> 412, Visseyer's region (0x66c0): no view (guard flag 483).
    dict(key="visseyer", enc=209, game="claude", map=409, play=["409:T6", "412:T2"], code={0: 0x6670, 1: 0x66C0},
         view=False, flags_on=[483], frames=9000),
    # Cave of Red Crystal: 418's lower door (0x6660, arrival y -200) -> 417; the set-up starts the
    # scene; the aftermath parks Nede's Synard by the cave (set_point, native) and goes to the world map.
    dict(key="arcmene", enc=522, game="claude", map=418, play=["418:T2"], code={0: 0x6660},
         end=("map", 1088), flags_on=[11], words=[0x314, 0x318, 0x31C, 0x330], frames=12000),
    # Cross Cave: 546's door (0x6644) -> 547; actor 64's talk (0x6af8) starts the set battle (two
    # Gargoyles) while flag 449 is clear (no view) and sets 449; the key chest (actor 60, 0x6660)
    # gives key item 68 (flag 2569; G[0x2e8] the chest statistic) and stores chapter 13 / step 0
    # (dropped by the lock). The re-fire sweep talks to actor 64 again: no second battle.
    dict(key="crosscave", enc=523, game="claude", map=546, play=["546:T3", "547:K64", "547:K60"],
         code={0: 0x6644, 1: 0x6AF8, 2: 0x6660}, view=False, flags_on=[449, 2569, 68], words=[0x2E8],
         sweep_after=True, views=0, frames=9000),
    # The dungeon under Mars (Claude's game): 464's door (0x3c44) -> 462 (view 15/2400), the region
    # 0x66cc (registered while flag 1 && 15/2400); the aftermath stores step 3000 and goes to Celine's
    # house 135 (no step-3000 scene there: only its music pick).
    dict(key="varmillion", enc=194, game="claude", map=464, play=["464:T4", "462:T7"], code={0: 0x3C44, 1: 0x66CC},
         end=("map", 135), final_map=135, frames=12000),
    # The Secret Charity (Rena's game): 464 -> 462 (no view in Rena's game) -> 462's door (0x78d8) ->
    # 463 (view 15/2500), Azamgil's region (0x66c8, registered while flag 389 == 0 && !flag 1 &&
    # 15/2500). In place: the aftermath gives key item 70, stores step 2600 and registers the next
    # beat's region (0x6b1c: flag 154, step 2700, no reward); the view ends at free play. The
    # re-fire sweep fires both regions with the real state: each re-tests its step (nothing starts).
    dict(key="azamgil", enc=195, game="rena", map=464, play=["464:T4", "462:T2", "463:T2"],
         code={0: 0x3C44, 1: 0x78D8, 2: 0x66C8}, end=("free",), flags_on=[70], sweep_after=True, frames=14000),
    # Lacour Front Line: 444's door (0x6730) -> 447 (view 27/1000); the set-up starts the scene; the
    # aftermath stores step 2000 and goes to 439 (only NPC talk keyed to 27/2000 there).
    dict(key="shin447", enc=516, game="claude", map=444, play=["444:T6"], code={0: 0x6730},
         end=("map", 439), final_map=439, frames=12000),
    # The Field of Intelligence: 553's door (0x4a80, taken while flag 175 is clear: the hub clears it)
    # -> 554 (view 42/0); the set-up starts the scene; the aftermath sets 175 (through) and goes to
    # 555, whose arrival scene sets 176 (view) and goes on by the hero: Claude's game step 1000 ->
    # 628 (a vision) -> 556; Rena's game step 1010 -> Shingo Forest 14 (Expel, read from disc 2: her
    # vision) -> 556. 556's end scene gives key item 82, sets 162 (the Field's done flag) and stores
    # step 0 (chapter 46 too once all four Fields are done: the view's). The view ends at free play in 556.
    dict(key="magichand", enc=524, game="claude", map=553, play=["553:T7"], code={0: 0x4A80},
         end=("free",), final_map=556, flags_on=[175, 162], flags_need=[162], sweep_after=True, frames=20000),
    dict(key="magichand_r", enc=524, game="rena", map=553, play=["553:T7"], code={0: 0x4A80},
         end=("free",), final_map=556, flags_on=[175, 162], flags_need=[162], sweep_after=True, frames=20000),
    # The Field of Power: 602's door (0x48a4) -> 603 (view 42/0 on [603, 604], until a transfer out);
    # the set-up starts the Guardian's scene (sets 528, through; in place). 603's door (0x6b64) -> 604,
    # whose region (0x6628: 528 && !163, no chapter test) starts the vision: Claude's game step 2000
    # -> 622; Rena's game step 2010 -> 27 (Expel) -> 30; both store step 2020 and return to 604, whose
    # set-up gives key item 83 (already in the hub state) and sets 163 (in place). The region again: nothing
    # (163 set); out by 604's door (0x6bfc) to 603 (the view goes on) and 603's (0x6b2c) to 602: it ends.
    dict(key="power", enc=525, game="claude", map=602, play=["602:T2", "603:T2", "604:T3", "604:T3", "604:T0", "603:T0"],
         code={0: 0x48A4, 1: 0x6B64, 2: 0x6628, 3: 0x6628, 4: 0x6BFC, 5: 0x6B2C}, end=("map", 602), final_map=602,
         flags_on=[528, 163], flags_need=[528, 163], frames=24000),
    dict(key="power_r", enc=525, game="rena", map=602, play=["602:T2", "603:T2", "604:T3", "604:T3", "604:T0", "603:T0"],
         code={0: 0x48A4, 1: 0x6B64, 2: 0x6628, 3: 0x6628, 4: 0x6BFC, 5: 0x6B2C}, end=("map", 602), final_map=602,
         flags_on=[528, 163], flags_need=[528, 163], frames=24000),
    # The Field of Power, leaving between the Guardian and 604's vision: out to 602 (the view ends),
    # back in: the view comes back (won, done_flag 163 clear) and 604's region plays the vision in it.
    dict(key="power_out", enc=525, game="claude", map=602,
         play=["602:T2", "603:T0", "602:T2", "603:T2", "604:T3", "604:T0", "603:T0"],
         code={0: 0x48A4, 1: 0x6B2C, 2: 0x48A4, 3: 0x6B64, 4: 0x6628, 5: 0x6BFC, 6: 0x6B2C}, end=("map", 602),
         final_map=602, views=2, flags_on=[528, 163], flags_need=[528, 163], frames=24000),
    # The Field of Courage: 641's door (0x47d4) -> 642 (view 42/0), the Guardian's region (0x6914,
    # thread_start_ex, registered while 42 && !164 && step != 20). Claude's game: the aftermath stores
    # step 10 and goes to 625 (a vision), which stores step 20 and returns to 642, whose set-up
    # (0x78a0) gives key item 84 and sets 164; Rena's game: the same reward in place. The view ends at
    # free play in 642. The scene asks "Don't put statue on altar." / "Put statue on altar." (only
    # while flag 521, the statue, is set: the hub has it): option 1 fights (":1", hubcore's sweep
    # option). The region's routine has no story test, but its end (0x7868) removes the region
    # (ext 0xa3 on the slot the set-up saved in G[0x344]); the re-fire sweep checks the room.
    dict(key="courage", enc=527, game="claude", map=641, play=["641:T2", "642:T1:1"], code={0: 0x47D4, 1: 0x6914},
         end=("free",), final_map=642, flags_on=[164, 84], flags_need=[164, 84], sweep_after=True, frames=24000),
    dict(key="courage_r", enc=527, game="rena", map=641, play=["641:T2", "642:T1:1"],
         code={0: 0x47D4, 1: 0x6914}, end=("free",), final_map=642, flags_on=[164, 84],
         flags_need=[164, 84], sweep_after=True, frames=24000),
    # The Field of Love: 595's door (0x3ba4) -> 596 (view 42/0); the set-up plays the Lover scene (sets
    # 518, through), then 0x7868: flag 508 (view), step 10, the vision by the hero (Claude's game 262,
    # Rena's game 310 in Armlock); back to 596 at step 20, whose set-up gives the key item and sets 519
    # and 165 (the Field's done flag); the view ends at free play in 596.
    dict(key="lover", enc=528, game="claude", map=595, play=["595:T0"], code={0: 0x3BA4},
         end=("free",), final_map=596, flags_on=[518, 519, 165], flags_need=[518, 165], sweep_after=True,
         frames=24000),
    dict(key="lover_r", enc=528, game="rena", map=595, play=["595:T0"], code={0: 0x3BA4},
         end=("free",), final_map=596, flags_on=[518, 519, 165], flags_need=[518, 165], sweep_after=True,
         frames=24000),
    # Mihne Cavern: 450's door (0x3c08) -> 453 (the view's approach room, chapter 55: its door
    # 0x3bd0 picks 454 at chapter <= 55, 455 after) -> 454, Bark's region (0x661c, registered
    # unconditionally); the aftermath stores step 1000 and goes to 455, whose 55/1000 set-up scene
    # gives key item 89 and stores chapter 56 / step 0 in place: the view ends at free play in 455.
    dict(key="bark", enc=531, game="claude", map=450, play=["450:T4", "453:T2", "454:T2"],
         code={0: 0x3C08, 1: 0x3BD0, 2: 0x661C}, end=("free",), final_map=455, flags_on=[89], flags_need=[89],
         sweep_after=True, frames=16000),
    # The Synard Plant: North City 260's door (0x69e4, slot 28) -> the Reception Area 264 (the view's
    # approach room, 38/4000: its doors 0x7390 / 0x7410 pick 269 at chapter <= 38, 270 after) -> 269,
    # whose set-up starts the scene; the aftermath goes to 271 (after_maps: its 38/4000 scene stores
    # step 5000) and on to 272, where the view ends (no 38/5000 reward there: 272's scene leads on
    # into the city, 267). On 272 (real state) its own chapter > 40 scene with Noel in the party plays
    # as on any visit (0x7048: sets 501, and 745): not the chamber's.
    dict(key="synard", enc=521, game="claude", map=260, play=["260:T28", "264:T0"], code={0: 0x69E4, 1: 0x7390},
         end=("map", 272), final_map=272, flags_on=[501, 745], frames=16000),
    # Fun City (a town chamber): 280's door (0x692c, slot 12) -> 281 (view 57/2000 on the district:
    # its NPCs and doors as at that story point until Shigeo is won), Shigeo's region (0x855c,
    # re-tests 57/2000); in place: the aftermath stores step 3000; the view ends at free play.
    dict(key="shigeo", enc=533, game="claude", map=280, play=["280:T12", "281:T0"], code={0: 0x692C, 1: 0x855C},
         end=("free",), final_map=281, sweep_after=True, frames=16000),
    # Armlock (a town chamber): 310's door (0x7bc) -> 306 (view 58/0), Berle's region (0x40, slot 21:
    # registered at 58/0 after the town's 21 others); the aftermath stores step 1000 and goes to 314
    # (after_maps), whose 58/1000 set-up scene (dialogue) stores step 2000 in place: the view ends at
    # free play in 314.
    dict(key="berle306", enc=535, game="claude", map=310, play=["310:T0", "306:T21"], code={0: 0x7BC, 1: 0x40},
         end=("free",), final_map=314, sweep_after=True, frames=20000),
    # The Fun City arena (P5, a town chamber): the antechamber 304's door (T4; the seed's P5 door
    # 19929509 sends it to 305 while 532 is unbeaten) -> 305 (view 57/20); the Clerk (actor 16, talk
    # registered at 57/20) offers training / rest / "Go see Narl": option 2 starts Marsilio's scene;
    # the aftermath stores step 2000 and goes to 296, where the view ends.
    dict(key="marsilio", enc=532, game="claude", map=304, play=["304:T4", "305:K16:2"],
         end=("map", 296), final_map=296, frames=16000),
    # Eluria Tower (P5): Berle 520's view (33/20) covers the tower 557-575; started on 568 (a view map,
    # as hubcore's tower BFS from 559): 568:T1 -> 569, whose set-up plays Shin 519's scene (guard flag
    # 587, cleared in the hub; no view of its own) -> 570:T3 -> 573:T4 (0x4804: shut at 33 below step
    # 20) -> 574 -> 575, whose set-up plays Berle's scene; its transfer to 576 (which stores chapter
    # 34 and goes on to the disc switch's 625) goes to 557 instead (cut_to), where the view ends.
    dict(key="tower", enc=520, game="claude", map=568, play=["568:T1", "569:T0", "570:T3", "573:T4"],
         code={3: 0x4804}, also=[519], end=("cut", 557), final_map=557, flags_on=[587], flags_need=[587],
         words=[0x214], frames=24000),   # (G[0x215]: 574's came-from byte, 573's door sets 0: through)
    # Fienal (D10): each pair's set-up scene at its step through the view (60/1000, 2000, 3000), in
    # place; the view's step store (2000 / 3000 / 4000) is dropped, the hub stays at 4000.
    dict(key="fienal2", enc=537, game="claude", map=670, play=["670:T4"], code={0: 0x3BD0},
         end=("free",), final_map=669, sweep_after=True, frames=12000),
    dict(key="fienal3", enc=538, game="claude", map=676, play=["676:T4"], code={0: 0x3B98},
         end=("free",), final_map=674, sweep_after=True, frames=12000),
    # The walk on from the Final Save Point 671 (door 0x3bec: the goal gate in a real seed) to the
    # final boss: the corridor 684 loops (its door 0x3c20 adds 1 to G[0x1ce] and returns to 684 until
    # it is 2; 671's arrival resets it) and then leads to 685, whose set-up starts Indalecio (539).
    dict(key="fienal_walk", enc=539, name="(walk to Indalecio)", walk=True, reach=685, battle=539, game="claude",
         map=671, play=["671:T5", "684:T2", "684:T2", "684:T2"], frames=9000),
    # ... and from the other side of each pair's room. Pair 1 from the east (687, Fienal's entrance:
    # the only way in, so its west side 655 is reached only after it); pair 3 from the west (679), the
    # door redirected to the east arrival while unbeaten (P5_DOORS, proposed); pair 2 from the west
    # (a --map arrival where 653's floor teleporter lands; no play step: the set-up plays the scene):
    # its scene works from either side.
    dict(key="fienal1b", enc=536, game="claude", map=687, play=["687:T2"], code={0: 0x3C8C},
         end=("free",), final_map=657, words=[0x1CC], sweep_after=True, frames=12000),
    dict(key="fienal3b", enc=538, game="claude", map=679, play=["679:T0"], code={0: 0x3B98},
         end=("free",), final_map=674, words=[0x1CC], sweep_after=True, frames=12000),
    dict(key="fienal2w", enc=537, game="claude", map=669, pos=(-980, 0, 18, 0), play=["9999:T0"], play_steps=0,
         end=("free",), final_map=669, frames=6000),
    # Mountain Palace: 515's door (0x3c14) -> 523, Harfainx's region (0x6750); in place; re-fired.
    dict(key="harfainx", enc=210, game="claude", map=515, play=["515:T4", "523:T2"],
         code={0: 0x3C14, 1: 0x6750}, end=("free",), flags_on=[77], sweep_after=True, frames=12000),
]
# Both heroes: nearly every scene reads flag 1 (the hero's lines, who walks where), so every chamber
# runs in Rena's game too: a copy of each Claude spec, except those that differ (their own `_r`
# spec), the game-filtered ones (Varmillion is Claude's) and the extra variants (claude_only).
CLAUDE_ONLY = {"varmillion", "power_out", "fienal2w", "alentax"}
_HAVE = {s["key"] for s in CHAMBERS}
for _s in list(CHAMBERS):
    if _s["game"] == "claude" and _s["key"] not in CLAUDE_ONLY and _s["key"] + "_r" not in _HAVE:
        CHAMBERS.append(dict(_s, key=_s["key"] + "_r", game="rena"))


def load_json(path):
    with open(os.path.join(ROOT, path), encoding="utf-8") as f:
        return json.load(f)


def hub_state(game):
    """(SS_SCRIPT_GLOBALS, SS_SWEEP_FLAGS) for the hub state of `game` (hub_state.json `hub`:
    chapter / step, flags, globals, then games[game])."""
    hub = load_json("docs/ap/hub_state.json")["hub"]
    words = {0xA0: hub["chapter"], 0xA4: hub["step"]}
    for a, v in hub["globals"].items():
        words[int(a, 16)] = int(v, 16) if isinstance(v, str) else v
    g = hub["games"][game]
    for a, v in g["globals"].items():
        words[int(a, 16)] = int(v, 16) if isinstance(v, str) else v
    flags = sorted(set(hub["flags"]) | set(g["flags"]))
    if game == "rena":
        flags = [f for f in flags if f != 1]
    glob_text = ",".join(f"{a:x}={struct.unpack('<i', struct.pack('<I', v & 0xFFFFFFFF))[0]}" for a, v in sorted(words.items()))
    flag_text = ",".join(str(f) for f in flags) + (",!1" if game == "rena" else "")
    return glob_text, flag_text


def write_seed(path, game):
    """A hub seed with every hub_bosses.json row and one boss location per row."""
    rows = load_json("docs/ap/hub_bosses.json")["bosses"]
    seed = {
        "format": "apso2", "version": 3, "seed": "hubchambers", "game": "Star Ocean: The Second Story",
        "slot": {"name": "Hub", "id": 1},
        "options": {"world_mode": "hub", "story_route": game},
        "items": {"5000757": {"name": "Blueberry", "kind": "item", "game_id": 757}},
        "locations": [{"id": r["location"], "name": r["name"], "check": {"type": "boss", "encounter": r["encounter"]},
                       "item": dict(FILLER)} for r in rows],
        "start": {"items": [], "fol": 0, "flags": []},
        "bosses": [{k: r[k] for k in SEED_ROW_KEYS if k in r and r[k] is not None} for r in rows],
        "doors": P5_DOORS,
        "hub": {"chapter": 60, "step": 4000, "lock": {"chapter": True, "step": True, "step_maps": [685]}},
    }
    os.makedirs(os.path.dirname(path), exist_ok=True)
    tmp = f"{path}.{os.getpid()}.tmp"   # (atomic: another runner may be reading it)
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(seed, f, indent=1)
    os.replace(tmp, path)
    return {r["encounter"]: r for r in rows}


def discs():
    d1 = os.environ.get("SS_DISC1")
    if not d1:
        sys.exit("set SS_DISC1 (disc 1's .cue)")
    return d1, d1.replace("Disc 1", "Disc 2")


def run_game(binary, work, name, game, start_map, frames, seed, extra_env, pos=None, dumps=True):
    """One memcapped headless run; returns (log path, pre dump, post dump, rc note)."""
    d1, d2 = discs()
    glob_text, flag_text = hub_state(game)
    runs = os.path.join(work, "runs")
    os.makedirs(runs, exist_ok=True)
    os.makedirs(os.path.join(work, "no-mods"), exist_ok=True)
    log = os.path.join(runs, f"{name}.log")
    pre, post = os.path.join(runs, f"{name}.pre"), os.path.join(runs, f"{name}.post")
    for p in (pre, post):
        if os.path.exists(p):
            os.remove(p)
    env = dict(os.environ)
    env.update(SS_MODS_DIR=os.path.join(work, "no-mods"), SDL_VIDEO_DRIVER="offscreen", SDL_AUDIO_DRIVER="dummy",
               SS_EVENT_SWEEP="1", SS_BATTLE_STUB="1", SS_EVENT_SWEEP_DISC="2", SS_DISC1=d1, SS_DISC2=d2,
               SS_SCRIPT_GLOBALS=glob_text, SS_SWEEP_FLAGS=flag_text, SS_SWEEP_PARTY=PARTY, SS_HUB_TRACE="1",
               XDG_CONFIG_HOME=os.path.join(work, "xdg"))
    env.setdefault("SS_MEM_CAP_GB", "4")
    if dumps:
        env.update(SS_STATE_DUMP=f"30:{pre}", SS_SWEEP_PLAY_DUMP=post)
    for k, v in extra_env.items():
        if k == "FLAGS+":
            env["SS_SWEEP_FLAGS"] += "," + v
        else:
            env[k] = v
    cmd = [binary, "--headless", "--no-memcard", "--debug", "--frames", str(frames), "--map", str(start_map)]
    if pos:
        cmd += ["--map-pos", ",".join(str(x) for x in pos)]
    if seed:
        cmd[4:4] = ["--ap-seed", seed]
    with open(log, "w") as out:
        rc, timed_out, at_cap = run_capped(cmd, binary, timeout=1800, stdout=out, stderr=out, env=env,
                                           stdin=subprocess.DEVNULL)
    note = ("timeout " if timed_out else "") + ("MEMCAP " if at_cap else "") + (f"rc {rc}" if rc else "")
    return log, pre, post, note.strip()


# ---- State dumps (SS_STATE_DUMP / SS_SWEEP_PLAY_DUMP: state block 0xa40, config, party, ...) ----
FLAGS_AT, GS_AT = 0x8C0, 0x220


def hub_words(game):
    """{G offset: s32} of the hub state's words (what a dump should hold where no scene changed it)."""
    words = {}
    for item in hub_state(game)[0].split(","):
        a, v = item.split("=")
        words[int(a, 16)] = int(v)
    return words


def dump_diff(a_path, b_path, game="claude"):
    """Flags / words that differ between the dumps. A word that ends at the hub state's own value is
    not a change (a --map start runs the first map's set-up before the sweep clears flag 1 in Rena's
    game, so its set-up writes Claude's hero bytes G[0xc4] / G[0xc5] into the first dump)."""
    hubw = hub_words(game)
    a, b = (open(p, "rb").read() for p in (a_path, b_path))
    fa, fb = a[FLAGS_AT:FLAGS_AT + 0x170], b[FLAGS_AT:FLAGS_AT + 0x170]
    on, off = [], []
    for n in range(0x170 * 8):
        x, y = fa[n >> 3] >> (n & 7) & 1, fb[n >> 3] >> (n & 7) & 1
        if x != y:
            (on if y else off).append(n)
    words = {}
    for o in range(0, 0x2A0, 4):
        g = 0xA0 + o
        if 0xA8 <= g < 0xC4:
            continue
        x, y = a[GS_AT + o:GS_AT + o + 4], b[GS_AT + o:GS_AT + o + 4]
        if x == y:
            continue
        hb = struct.pack("<i", hubw[g]) if g in hubw else None
        diff = [g + i for i in range(4) if x[i] != y[i] and g + i not in NOISE_BYTES and (hb is None or y[i] != hb[i])]
        if diff and g not in NOISE_WORDS:
            words[g] = (struct.unpack("<i", x)[0], struct.unpack("<i", y)[0])
    chapter, step = struct.unpack_from("<ii", b, GS_AT)
    inv = sum(1 for i in range(0xC28) if a[0xA40 + 0xC20 + i] != b[0xA40 + 0xC20 + i])
    return {"on": on, "off": off, "words": words, "chapter": chapter, "step": step, "inventory_bytes": inv}


# ---- Log parsing ----
VIEW_ON = re.compile(r"\[ap\] hub: story view on: map (\d+) \((.*?), encounter (\d+)\)")
NO_VIEW = re.compile(r"\[ap\] hub: chamber map (\d+) \((.*?), encounter (\d+)\): no story view")
VIEW_OFF = re.compile(r"\[ap\] hub: story view off: map (-?\d+) \((.*?), encounter (\d+); (.*?)\): dropped (.*)$")
CUT = re.compile(r"\[ap\] hub: story view: (.*?), encounter (\d+): the transfer to map (\d+) goes to map (\d+)")
BATTLE_END = re.compile(r"\[ap\] encounter (\d+) ended: outcome (\d+)")
CHECK = re.compile(r"\[ap\] check (\d+) \(")
GOAL = re.compile(r"\[ap\] goal: encounter (\d+) won")
PLAY_END = re.compile(r"play: story chapter (-?\d+) step (-?\d+)")
PLAY_DONE = re.compile(r"play: control on map (\d+) after (\d+) of (\d+) steps")
ENTRY_START = re.compile(r"\[sweep\] map \d+ vblank (\d+) entry \d+ (\w)(\d+) code (\d+) start")
PLAY_STEP = re.compile(r"\[sweep\] map \d+ vblank (\d+) play: step (\d+) on map (\d+): (\w)(\d+)")
NOW_ON = re.compile(r"\[sweep\] map \d+ vblank (\d+) now on map (\d+)")
BATTLE_AT = re.compile(r"\[ap\] a battle at vblank (\d+)")
READ = re.compile(r"\[ap\] hub trace: read flag (\d+) = (\d) at map (\d+) \((view|real)\)")
FSTORE = re.compile(r"\[ap\] hub trace: flag (\d+) = (\d) at map (\d+) \((view|through)\)")
GSTORE = re.compile(r"\[ap\] hub trace: store G\[(0x[0-9a-f]+)\]:(\d) @(0x[0-9a-f]+) = (-?\d+) at map (\d+) \((view|through)\)")
LOCK = re.compile(r"\[ap\] hub: (chapter|step) store (-?\d+) dropped at map (\d+) offset (0x[0-9a-f]+)")
PARTY_DROP = re.compile(r"\[ap\] hub: story view: party_(add|remove) (\w+) at map (\d+) dropped")
ENTRIES = re.compile(r"\[sweep\] map (\d+) vblank \d+ entries (\d+): ?(.*)$")
SWEEP_DONE = re.compile(r"\[sweep\] map (\d+) vblank \d+ done entries (\d+)")
DISC_DROP = re.compile(r"\[ap\] hub: disc switch")


def parse_log(path):
    ev = []   # (kind, data) in order
    with open(path, errors="replace") as f:
        for line in f:
            for kind, rx in (("view_on", VIEW_ON), ("no_view", NO_VIEW), ("view_off", VIEW_OFF), ("cut", CUT),
                             ("battle_end", BATTLE_END), ("check", CHECK), ("goal", GOAL), ("play_end", PLAY_END),
                             ("play_done", PLAY_DONE), ("entry", ENTRY_START), ("step", PLAY_STEP), ("now_on", NOW_ON),
                             ("battle", BATTLE_AT), ("read", READ), ("fstore", FSTORE), ("gstore", GSTORE),
                             ("lock", LOCK), ("party", PARTY_DROP), ("disc", DISC_DROP), ("entries", ENTRIES),
                             ("sweep_done", SWEEP_DONE)):
                m = rx.search(line)
                if m:
                    ev.append((kind, m.groups()))
                    break
    return ev


def summarize_trace(ev):
    """The view's first reads and its stores (for a row's flag list)."""
    reads = [(int(f), int(v), int(m), s) for k, (f, v, m, s) in ((k, d) for k, d in ev if k == "read")]
    fst = [(int(f), int(v), int(m), s) for k, (f, v, m, s) in ((k, d) for k, d in ev if k == "fstore")]
    gst = [(g, int(sz), at, int(v), int(m), s) for k, (g, sz, at, v, m, s) in ((k, d) for k, d in ev if k == "gstore")]
    return {"reads": reads, "flag_stores": fst, "global_stores": gst}


def check_walk(spec, ev, note):
    """[(ok, what)] for a walk (not a chamber): the run reaches spec['reach'] and, with
    spec['battle'], that encounter's battle starts and ends in a victory."""
    out = [(False, f"run: {note}")] if note and not spec.get("note_ok") else []
    reached = [d for k, d in ev if k == "now_on" and int(d[1]) == spec["reach"]]
    out.append((bool(reached), f"reached map {spec['reach']}" + (f" at vblank {reached[0][0]}" if reached else "")))
    if spec.get("battle") is not None:
        wins = [d for k, d in ev if k == "battle_end" and int(d[0]) == spec["battle"]]
        out.append((len(wins) >= 1 and wins[0][1] == "0", f"battle {spec['battle']} x{len(wins)}"))
    locks = [d for k, d in ev if k == "lock"]
    out.append((True, f"lock drops {len(locks)}"))
    return out


def check_chamber(spec, row, ev, diff, note):
    """[(ok, what)] for a chamber run."""
    if spec.get("walk"):
        return check_walk(spec, ev, note)
    enc, loc = spec["enc"], row["location"]
    out = []
    if note:
        out.append((False, f"run: {note}"))
    ons = [d for k, d in ev if k == "view_on" and int(d[2]) == enc]
    if spec.get("view", True):
        out.append((len(ons) >= 1, f"view on x{len(ons)}"))
    else:
        nv = [d for k, d in ev if k == "no_view" and int(d[2]) == enc]
        out.append((len(nv) >= 1 and not ons, "no view (plays at the hub state)"))
    for e in spec.get("also", []):   # other chambers' bosses the run beats on the way (Shin 569 in Berle's tower)
        ok = any(k == "battle_end" and int(d[0]) == e and d[1] == "0" for k, d in ev)
        out.append((ok, f"also won {e}"))
    wins = [d for k, d in ev if k == "battle_end" and int(d[0]) == enc]
    out.append((len(wins) == 1 and wins[0][1] == "0", f"battle {enc} x{len(wins)}" + (f" outcome {wins[0][1]}" if wins else "")))
    checks = [d for k, d in ev if k == "check" and int(d[0]) == loc]
    out.append((len(checks) == 1, f"check {loc} x{len(checks)}"))
    goals = [d for k, d in ev if k == "goal" and int(d[0]) == enc]
    out.append((len(goals) == 1, "goal_won"))
    offs = [d for k, d in ev if k == "view_off" and int(d[2]) == enc]
    end = spec.get("end")
    if spec.get("view", True) and end:
        if end[0] == "map":
            ok = len(offs) >= 1 and offs[0][3] == f"map {end[1]} loaded"
        elif end[0] == "cut":
            cuts = [d for k, d in ev if k == "cut" and int(d[1]) == enc]
            ok = len(cuts) == 1 and int(cuts[0][3]) == end[1] and len(offs) >= 1
        else:
            ok = len(offs) >= 1 and offs[0][3] == "free play after the battle"
        out.append((ok, f"view end {offs[0][3] if offs else 'none'}"))
    pe = [d for k, d in ev if k == "play_end"]
    pdone = [d for k, d in ev if k == "play_done"]
    steps = spec.get("play_steps", len(spec["play"]))
    out.append((bool(pdone) and int(pdone[-1][1]) == steps, f"play {pdone[-1][1] if pdone else '?'}/{steps} on map "
                f"{pdone[-1][0] if pdone else '?'}"))
    if spec.get("final_map") is not None:
        out.append((bool(pdone) and int(pdone[-1][0]) == spec["final_map"], f"ends on map {spec['final_map']}"))
    out.append((bool(pe) and pe[-1] == ("60", "4000"), f"story {'/'.join(pe[-1]) if pe else '?'}"))
    codes = spec.get("code", {})
    fired = [d for k, d in ev if k == "entry"]
    for i, c in codes.items():
        ok = i < len(fired) and int(fired[i][3]) * 4 == c
        out.append((ok, f"step {i} code {c:#x}"))
    if diff is not None:
        on_ok = set(spec.get("flags_on", []))
        off_ok = set(spec.get("flags_off", []))
        bad_on = [f for f in diff["on"] if f not in on_ok]
        bad_off = [f for f in diff["off"] if f not in off_ok]
        need = [f for f in spec.get("flags_need", []) if f not in diff["on"]]
        out.append((not bad_on and not bad_off and not need,
                    f"flags +{diff['on']} -{diff['off']}" + (f" UNEXPECTED +{bad_on} -{bad_off}" if bad_on or bad_off else "")
                    + (f" MISSING {need}" if need else "")))
        bad_w = {g: v for g, v in diff["words"].items() if g not in set(spec.get("words", []))}
        out.append((not bad_w, "globals " + (", ".join(f"G[{g:#x}] {a:#x}->{b:#x}" for g, (a, b) in sorted(diff["words"].items()))
                                             or "unchanged") + (" UNEXPECTED" if bad_w else "")))
        out.append((diff["chapter"] == 60 and diff["step"] == 4000, f"dump {diff['chapter']}/{diff['step']}"))
    else:
        out.append((False, "no state dumps"))
    if spec.get("refire") or "views" in spec:
        n_on = len(ons)
        out.append((n_on == spec.get("views", 1) and len(wins) == 1, f"views {n_on}, battles {len(wins)}"))
    if spec.get("sweep_after"):
        lists = [d for k, d in ev if k == "entries"]
        done = [d for k, d in ev if k == "sweep_done"]
        battles = sum(1 for k, d in ev if k == "battle")
        codes_left = " ".join(x.split("[")[0] for x in lists[0][2].split()) if lists else "?"
        out.append((bool(lists) and bool(done) and len(wins) == 1 and len(ons) == spec.get("views", 1),
                    f"re-fire sweep of map {lists[0][0] if lists else '?'}: {lists[0][1] if lists else '?'} active entries "
                    f"({codes_left}), {'done' if done else 'NOT DONE'}, battles in run {battles}, boss battles {len(wins)}"))
    return out


def run_chamber(spec, args, rows):
    seed = os.path.join(args.work, "seeds", f"hub_{spec['game']}.apso2")
    env = dict(SS_SWEEP_PLAY=",".join(spec["play"]), **spec.get("env", {}))
    frames = spec.get("frames", 12000)
    if spec.get("sweep_after"):
        # The re-fire test: once the play is done, the sweep lists the room's ACTIVE entries (the
        # triggers the view's set-up and scene left registered) and fires each in turn, reloading
        # the room between them (then at the real state); a second battle of the boss fails.
        env.update(SS_SWEEP_PLAY_SWEEP="1", SS_EVENT_SWEEP_FRAMES="1500")
        frames += 30000
    log, pre, post, note = run_game(args.bin, args.work, spec["key"], spec["game"], spec["map"], frames, seed, env,
                                    pos=spec.get("pos"))
    ev = parse_log(log)
    diff = dump_diff(pre, post, spec["game"]) if os.path.exists(pre) and os.path.exists(post) else None
    res = check_chamber(spec, rows.get(spec["enc"]), ev, diff, note)
    tr = summarize_trace(ev)
    with open(os.path.join(args.work, "runs", f"{spec['key']}.json"), "w") as f:
        json.dump({"key": spec["key"], "enc": spec["enc"], "results": res, "diff": diff and {k: (v if k != "words" else
                   {f"{g:#x}": x for g, x in v.items()}) for k, v in diff.items()}, "trace": tr,
                   "lock": [d for k, d in ev if k == "lock"], "party": [d for k, d in ev if k == "party"],
                   "views": [d for k, d in ev if k in ("view_on", "view_off", "cut", "no_view")]}, f, indent=1)
    return spec, res


def list_entries(args):
    seed = os.path.join(args.work, "seeds", f"hub_{args.game}.apso2")
    name = f"list{args.list}{args.game[0]}" + ("_" + re.sub(r"[^0-9A-Za-z]+", "_", args.route) if args.route else "")
    log, _, _, note = run_game(args.bin, args.work, name, args.game, args.list, args.frames or 400, seed,
                               {"SS_SWEEP_PLAY": args.route or "9999:T0", "SS_SWEEP_PLAY_SWEEP": "1"}, dumps=False)
    with open(log, errors="replace") as f:
        for line in f:
            if " entries " in line or "story view" in line or "chamber map" in line or "now on map" in line \
                    or "[ap] encounter" in line or "play: control" in line:
                print(line.rstrip()[:2000])
    if note:
        print("run:", note)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--only", default="")
    ap.add_argument("--bin", default=os.path.join(ROOT, "engine", "build-hubchambers", "shattered-stars"))
    ap.add_argument("--work", default=os.path.join(ROOT, "scratchpad", "hubchambers"))
    ap.add_argument("--jobs", type=int, default=2)
    ap.add_argument("--list", type=int)
    ap.add_argument("--game", default="claude")
    ap.add_argument("--route", default="", help="--list: SS_SWEEP_PLAY steps walked before the listing")
    ap.add_argument("--frames", type=int, default=0, help="--list: the frame limit (default 400)")
    ap.add_argument("--keys", action="store_true")
    ap.add_argument("--verbose", "-v", action="store_true")
    args = ap.parse_args()
    args.jobs = max(1, min(args.jobs, 2))
    rows = None
    for g in ("claude", "rena"):
        rows = write_seed(os.path.join(args.work, "seeds", f"hub_{g}.apso2"), g)
    if args.keys:
        for s in CHAMBERS:
            print(s["key"], s["enc"], s["game"])
        return 0
    if args.list is not None:
        list_entries(args)
        return 0
    only = set(x for x in args.only.split(",") if x)
    todo = [s for s in CHAMBERS if not only or s["key"] in only or str(s["enc"]) in only]
    failed = 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for spec, res in pool.map(lambda s: run_chamber(s, args, rows), todo):
            ok = all(r[0] for r in res)
            failed += not ok
            name = rows[spec["enc"]]["name"] if spec["enc"] in rows else spec.get("name", "")
            line = f"{'PASS' if ok else 'FAIL'} {spec['key']:14s} {spec['enc']:3d} {name[:20]:20s} {spec['game']:6s}"
            shown = res if (args.verbose or not ok) else [r for r in res if r[1].startswith(("view end", "flags"))]
            print(line + " | " + "; ".join(("" if r[0] else "!! ") + r[1] for r in shown), flush=True)
    print(f"{len(todo) - failed}/{len(todo)} chambers pass")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
