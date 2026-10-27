#!/usr/bin/env python3
"""The reference evaluator of so2.apworld's tracker export (docs/ap/seed_format.md "The tracker
export"; docs/plans/trackers.md B3): what a tracker shows from a room's `slot_data["tracker"]` and
the items the slot has received, with no Archipelago and no world code.

    from ap_tracker_logic import TrackerLogic
    logic = TrackerLogic(slot_data["tracker"])
    view = logic.evaluate({ap_item_id: count, ...})
    view.locations      # the AP location ids in logic (checked or not)
    view.regions        # the reached region names; view.events: event name -> how many reached
    view.goal           # the completion condition holds
    logic.connect(entrance_name, region_name)   # a deferred entrance found (Phase C's randomizer)

The algorithm (the PopTracker pack's Lua, tools/poptracker/logic.lua, is the same, line for line):
start in region 0 with no events; repeat passes until a pass changes nothing: each entrance from a
reached region whose rule holds reaches its region (an entrance not connected yet, `to` -1, reaches
nothing), each event in a reached region whose rule holds is collected (once). Every kind of rule
is monotone (more items, events or regions never make one fail), so this is the least fixpoint, the
state Archipelago's own region sweep and event sweep reach (CollectionState.update_reachable_regions,
sweep_for_advancements). Rule results are memoised per
pass; the last pass changed nothing, so its memo holds for the locations: a location is in logic
when its region is reached and its rule holds.

The world's tests (apworld/so2/test/test_tracker.py) check this against the world's own logic sphere
by sphere for every mix; tools/ap_poptracker_test.py checks the Lua against this.

    python3 tools/ap_tracker_logic.py SLOT_DATA.json [ITEM_ID ...]   # prints what is in logic
"""
import json
import sys

FORMATS = (1,)      # the export formats this evaluator reads


class View:
    """What is in logic for one item state."""
    def __init__(self, regions, events, locations, goal):
        self.regions, self.events, self.locations, self.goal = regions, events, locations, goal


class TrackerLogic:
    def __init__(self, export):
        if not isinstance(export, dict) or export.get("format") not in FORMATS:
            raise ValueError(f"not a tracker export this evaluator reads (format {export.get('format')!r}, "
                             f"reads {FORMATS})" if isinstance(export, dict) else "not a tracker export")
        self.regions = export["regions"]
        self.rules = export["rules"]
        self.entrances = [list(e) for e in export["entrances"]]     # [name, from, to, rule]
        self.events = export["events"]
        self.goal = export["goal"]
        self.location_of = {}       # AP location id -> (region, rule)
        for group in export["locations"]:
            for address in group[2:]:
                self.location_of[address] = (group[0], group[1])

    def connect(self, name, region):
        """An entrance not connected in the export (`to` -1: an entrance randomizer's deferred one)
        now leads to `region` (a name), as the slot data's pairings or a data-storage key say."""
        target = self.regions.index(region)
        found = [e for e in self.entrances if e[0] == name]
        if not found:
            raise KeyError(f"no entrance {name!r}")
        for e in found:
            e[2] = target

    def evaluate(self, counts):
        """counts: AP item id -> how many received (absent: none)."""
        reached = {0}
        got = {}
        used_entrances, used_events = set(), set()

        def count(ref):
            return got.get(ref, 0) if isinstance(ref, str) else counts.get(ref, 0)

        while True:
            memo = {}

            def holds(k):
                if k in memo:
                    return memo[k]
                body = self.rules[k]
                if body is True or body is False:
                    result = body
                else:
                    kind, args = body[0], body[1:]
                    if kind == "has":
                        result = count(args[0]) >= args[1]
                    elif kind == "all":
                        result = all(count(r) >= 1 for r in args)
                    elif kind == "any":
                        result = any(count(r) >= 1 for r in args)
                    elif kind == "and":
                        result = all(holds(c) for c in args)
                    elif kind == "or":
                        result = any(holds(c) for c in args)
                    elif kind == "region":
                        result = args[0] in reached
                    else:
                        raise ValueError(f"rule {k}: unknown kind {kind!r}")
                memo[k] = result
                return result

            changed = False
            for k, (_, origin, target, rule) in enumerate(self.entrances):
                if k not in used_entrances and target >= 0 and origin in reached and holds(rule):
                    used_entrances.add(k)
                    if target not in reached:
                        reached.add(target)
                        changed = True
            for k, (name, region, rule) in enumerate(self.events):
                if k not in used_events and region in reached and holds(rule):
                    used_events.add(k)
                    got[name] = got.get(name, 0) + 1
                    changed = True
            if not changed:
                break
        locations = {address for address, (region, rule) in self.location_of.items()
                     if region in reached and holds(rule)}
        return View({self.regions[r] for r in reached}, got, locations, holds(self.goal))


def main():
    with open(sys.argv[1], encoding="utf-8") as f:
        data = json.load(f)
    export = data.get("tracker", data)
    counts = {}
    for arg in sys.argv[2:]:
        counts[int(arg)] = counts.get(int(arg), 0) + 1
    view = TrackerLogic(export).evaluate(counts)
    print(f"regions: {sorted(view.regions)}")
    print(f"events: {dict(sorted(view.events.items()))}")
    print(f"goal: {view.goal}")
    print(f"in logic: {len(view.locations)} of {len(TrackerLogic(export).location_of)} locations")
    for address in sorted(view.locations):
        print(address)


if __name__ == "__main__":
    main()
