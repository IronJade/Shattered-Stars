"""The tracker export (docs/plans/trackers.md B3; docs/ap/seed_format.md "The tracker export"):
this world's resolved logic as plain data in the slot data's `tracker` key, so a tracker that
can't run the world (the PopTracker pack's Lua, tools/poptracker/logic.lua) evaluates exactly
the world's own rules (decision D2). The reference evaluator is tools/ap_tracker_logic.py.

It walks the world as it stands when fill_slot_data runs (after connect_entrances: a later entrance
randomizer's graph is the one exported): the regions, every entrance, every event (a location with
no address holding an event item), every location with an address, and the completion condition.
Every rule is one of logic.RESOLVED_KINDS (Rule Builder, B2), written as a body in one table
(`rules`), equal bodies once: a spot names its rule by index, an And / Or names its children by
index. Items are AP item ids; events are their event item's name (rules read events as items).

Format 1:
    {"format": 1,
     "regions":   [name, ...],                  index 0 is the origin region ("Menu")
     "rules":     [body, ...],                  see below
     "entrances": [[name, from, to, rule], ...],  the entrance's name, region indices (to -1: not
                                                connected yet), a rule index
     "events":    [[name, region, rule], ...],  the event item's name, its location's region
     "locations": [[region, rule, id, ...], ...],  the AP location ids in that region with that rule
     "goal":      rule}                         the completion condition
A body: true | false | ["has", ref, count] | ["all", ref, ...] | ["any", ref, ...]
| ["and", rule, ...] | ["or", rule, ...] | ["region", region]. A ref is an AP item id (a number:
the count received) or an event's name (a string: how many of its events are reached). "region"
(reached that region) is the room left for a rule that reads a region (Rule Builder's
CanReachRegion, Phase C): no rule of this world makes one yet; the evaluators handle it.

Room for the entrance randomizer (Phase C) without a new format: entrances are named (a door's
entrances will be named by their door ids, docs/ap/tracker_maps.md) and may be many; an entrance
whose target is unknown until it is found (deferred: filled later from the slot data's pairings or a
data-storage key, through the evaluators' `connect`) is written with `to` -1, and the sweep skips it
until then; optional keys a reader doesn't know (a region -> map ids hint, the pairings) may be added
and are ignored by older readers. A new body kind, or a key whose meaning changes, is a new format.
"""
from rule_builder.rules import And, False_, Has, HasAll, HasAny, Or, True_

from . import story

FORMAT = 1


class _Export:
    def __init__(self, world):
        self.world = world
        self.events = set()
        self.region_index = {}
        self.rules, self.rule_index, self.memo = [], {}, {}

    def ref(self, name):
        if name in self.events:
            return name
        code = self.world.item_name_to_id.get(name)
        if code is None:
            raise ValueError(f"a rule reads {name!r}, neither an item nor an event of this world")
        return code

    def intern(self, body):
        key = repr(body)
        if key not in self.rule_index:
            self.rule_index[key] = len(self.rules)
            self.rules.append(body)
        return self.rule_index[key]

    def rule(self, resolved):
        """The index of a resolved rule's body (children first). Resolved rules are frozen
        dataclasses: equal rules are often one object, so memoise by identity too."""
        found = self.memo.get(id(resolved))
        if found is not None and found[0] is resolved:
            return found[1]
        if isinstance(resolved, (True_.Resolved, False_.Resolved)):
            body = bool(resolved.always_true)
        elif isinstance(resolved, Has.Resolved):
            body = ["has", self.ref(resolved.item_name), resolved.count]
        elif isinstance(resolved, (HasAll.Resolved, HasAny.Resolved)):
            body = ["all" if isinstance(resolved, HasAll.Resolved) else "any",
                    *(self.ref(n) for n in resolved.item_names)]
        elif isinstance(resolved, (And.Resolved, Or.Resolved)):
            body = ["and" if isinstance(resolved, And.Resolved) else "or",
                    *(self.rule(c) for c in resolved.children)]
        else:
            raise TypeError(f"the tracker export has no form for {type(resolved).__qualname__}")
        index = self.intern(body)
        self.memo[id(resolved)] = (resolved, index)
        return index

    def build(self):
        world = self.world
        regions = list(world.multiworld.get_regions(world.player))
        origin = world.get_region(world.origin_region_name)
        regions.remove(origin)
        regions.insert(0, origin)
        self.region_index = {region.name: k for k, region in enumerate(regions)}
        event_spots = []
        for region in regions:
            for loc in region.locations:
                if loc.address is None:
                    if loc.item is None or loc.item.code is not None:
                        raise ValueError(f"{loc.name}: a location without an address but no event item")
                    self.events.add(loc.item.name)
                    event_spots.append(loc)
        entrances = [[e.name, self.region_index[e.parent_region.name],
                      -1 if e.connected_region is None else self.region_index[e.connected_region.name],
                      self.rule(e.access_rule)]
                     for region in regions for e in region.exits]
        events = [[loc.item.name, self.region_index[loc.parent_region.name], self.rule(loc.access_rule)]
                  for loc in event_spots]
        groups = {}
        for loc in sorted((loc for region in regions for loc in region.locations if loc.address is not None),
                          key=lambda loc: loc.address):
            key = (self.region_index[loc.parent_region.name], self.rule(loc.access_rule))
            groups.setdefault(key, []).append(loc.address)
        locations = [[region, rule, *addresses] for (region, rule), addresses in sorted(groups.items())]
        goal = self.rule(world.multiworld.completion_condition[world.player])
        out = {"format": FORMAT, "regions": [r.name for r in regions], "rules": self.rules,
               "entrances": entrances, "events": events, "locations": locations, "goal": goal}
        found = story.export(world)     # story mode: story awareness (optional; format-1 readers ignore it)
        if found is not None:
            out["story"] = found
        return out


def export(world):
    """The `tracker` slot data of this world (format 1, above)."""
    return _Export(world).build()


__all__ = ["FORMAT", "export"]
