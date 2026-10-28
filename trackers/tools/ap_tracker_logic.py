#!/usr/bin/env python3
"""The reference evaluator of so2.apworld's tracker export (docs/apworld/seed_format.md "The tracker
export"; docs/plans/trackers.md B3): what a tracker shows from a room's `slot_data["tracker"]` and
the items the slot has received, with no Archipelago and no world code.

    from ap_tracker_logic import TrackerLogic
    logic = TrackerLogic(slot_data["tracker"])
    view = logic.evaluate({ap_item_id: count, ...})
    view.locations      # the AP location ids in logic (checked or not)
    view.regions        # the reached region names; view.events: event name -> how many reached
    view.goal           # the completion condition holds
    logic.connect(entrance_name, region_name)   # a deferred entrance found (Phase C's randomizer)

    view = logic.evaluate(counts, story={"chapter": 30, "step": 0})   # story awareness (story mode)
    view.locations      # in logic and reached by the story
    view.later          # in logic by the items, but later in the story
    view.stage          # how many of the story's points the position has reached (None: no awareness)

The algorithm (the PopTracker pack's Lua, trackers/poptracker/logic.lua, is the same, line for line):
start in region 0 with no events; repeat passes until a pass changes nothing: each entrance from a
reached region whose rule holds reaches its region (an entrance not connected yet, `to` -1, reaches
nothing), each event in a reached region whose rule holds is collected (once). Every kind of rule
is monotone (more items, events or regions never make one fail), so this is the least fixpoint, the
state Archipelago's own region sweep and event sweep reach (CollectionState.update_reachable_regions,
sweep_for_advancements). Rule results are memoised per
pass; the last pass changed nothing, so its memo holds for the locations: a location is in logic
when its region is reached and its rule holds.

Story awareness (story mode; the export's optional `story` key, docs/apworld/seed_format.md "Story
awareness"): with the port's story position (`so2_story_{team}_{slot}`, {"chapter": C, "step": S}),
the position has reached the points (`stages`: [name, chapter, step], in order) at or before it, and
always the first (chapter 0 or below is no position); an
event of `story.events` ([name, k]) is collected only once point k is reached, and a location of
`story.locations` ([k, id, ...]) is in logic only once point k is reached (the others: from point 1).
`later` is what the items alone would put in logic but the story hasn't reached. Without a position
(None, or a value that isn't one), or an export without `story` (open world, hub, older rooms), the
evaluation is the plain one and `later` is empty.

The world's tests (apworld/so2/test/test_tracker.py, test_story.py) check this against the world's own
logic sphere by sphere for every mix and against Universal Tracker's deferred story events;
trackers/tools/ap_poptracker_test.py checks the Lua against this.

    python3 trackers/tools/ap_tracker_logic.py SLOT_DATA.json [ITEM_ID ...] [chapter=C[,S]]   # prints what is in logic
"""
import json
import sys

FORMATS = (1,)      # the export formats this evaluator reads


class View:
    """What is in logic for one item state (and story position)."""
    def __init__(self, regions, events, locations, goal, later=frozenset(), stage=None):
        self.regions, self.events, self.locations, self.goal = regions, events, locations, goal
        self.later, self.stage = later, stage


def story_position(value):
    """The port's story value as (chapter, step), or None when it isn't one (or its chapter is 0 or
    below: no game in progress has one)."""
    if not isinstance(value, dict):
        return None
    chapter, step = value.get("chapter"), value.get("step", 0)
    if isinstance(chapter, bool) or not isinstance(chapter, (int, float)) or chapter <= 0:
        return None
    if isinstance(step, bool) or not isinstance(step, (int, float)):
        step = 0
    return int(chapter), int(step)


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
        story = export.get("story")
        self.story = story if isinstance(story, dict) else None
        self.story_events, self.story_locations = {}, {}    # name / AP id -> the point it needs
        if self.story is not None:
            for name, k in self.story.get("events", []):
                self.story_events[name] = k
            for group in self.story.get("locations", []):
                for address in group[1:]:
                    self.story_locations[address] = group[0]

    def story_stage(self, value):
        """How many of the export's story points the position `value` has reached, or None (no story
        awareness: no `story` key, or no position)."""
        pos = story_position(value)
        if self.story is None or pos is None:
            return None
        return max(1, sum(1 for _, chapter, step in self.story["stages"] if (chapter, step) <= pos))

    def connect(self, name, region):
        """An entrance not connected in the export (`to` -1: an entrance randomizer's deferred one)
        now leads to `region` (a name), as the slot data's pairings or a data-storage key say."""
        target = self.regions.index(region)
        found = [e for e in self.entrances if e[0] == name]
        if not found:
            raise KeyError(f"no entrance {name!r}")
        for e in found:
            e[2] = target

    def evaluate(self, counts, story=None):
        """counts: AP item id -> how many received (absent: none); story: the port's story value
        (`so2_story_*`) or None."""
        stage = self.story_stage(story)
        regions, got, locations, goal = self._sweep(counts, stage)
        if stage is None:
            return View(regions, got, locations, goal)
        plain = self._sweep(counts, None)[2]
        return View(regions, got, locations, goal, frozenset(plain - locations), stage)

    def _sweep(self, counts, stage):
        """The sweep with the story's events and locations held back past point `stage` (None: none)."""
        def ahead(k):
            return stage is not None and k > stage

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
                if k not in used_events and region in reached and not ahead(self.story_events.get(name, 1)) \
                        and holds(rule):
                    used_events.add(k)
                    got[name] = got.get(name, 0) + 1
                    changed = True
            if not changed:
                break
        locations = {address for address, (region, rule) in self.location_of.items()
                     if region in reached and not ahead(self.story_locations.get(address, 1)) and holds(rule)}
        return {self.regions[r] for r in reached}, got, locations, holds(self.goal)


def main():
    with open(sys.argv[1], encoding="utf-8") as f:
        data = json.load(f)
    export = data.get("tracker", data)
    counts, story = {}, None
    for arg in sys.argv[2:]:
        if arg.startswith("chapter="):      # chapter=C[,S]: a story position
            chapter, _, step = arg[8:].partition(",")
            story = {"chapter": int(chapter), "step": int(step or 0)}
            continue
        counts[int(arg)] = counts.get(int(arg), 0) + 1
    view = TrackerLogic(export).evaluate(counts, story)
    if view.stage is not None:
        print(f"story: {view.stage} points reached; later in the story: {len(view.later)}")
    print(f"regions: {sorted(view.regions)}")
    print(f"events: {dict(sorted(view.events.items()))}")
    print(f"goal: {view.goal}")
    print(f"in logic: {len(view.locations)} of {len(TrackerLogic(export).location_of)} locations")
    for address in sorted(view.locations):
        print(address)


if __name__ == "__main__":
    main()
