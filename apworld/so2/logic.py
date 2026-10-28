"""Rules as data: every access rule of this world is an Archipelago Rule Builder rule
(rule_builder/, Archipelago's docs/rule builder.md; docs/apworld/apworld.md "Rules"; docs/plans/trackers.md
B2). The rule sites (regions.py, rules.py, hub.py, skillsanity.py) work out what a place needs from
the world's data and options, then set it with `world.set_rule`, which resolves it for this world
into frozen dataclasses (rule_builder.rules.Rule.Resolved): no lambdas on locations, entrances or
events. Universal Tracker's /explain prints them; the tracker export (trackers.md B3) walks them.
Item rules (add_item_rule: what a location may hold) stay functions.

The rule data comes in three shapes, one builder each:
- `need(names)`: all of these items and events (none: open);
- `ways(modes)`: any of the (event or None, keys) ways, each the event and all its keys: an
  open-world location's modes (rules.open_world_access), a drop's places (rules.drop_rule), a guild
  check's (skillsanity.guild_rule); no ways: never;
- `any_of(alternatives)`: any of these item tuples, all of one (a specialty's skill items per
  member, skillsanity.specialty_needs; a hub drop's dungeons' accesses, hub.drop_rule).

Only the Rule Builder's own rules are used (True_, Has, HasAll, And, Or; resolved, also HasAny and
False_): RESOLVED_KINDS. No rule reads a region (no CanReachRegion): none needs an indirect
condition. Every spot without a rule of its own gets True_ (open_by_default, after set_rules), so
each location, entrance and event carries a Rule Builder rule (test/test_logic.py)."""
from rule_builder.rules import And, False_, Has, HasAll, HasAny, Or, Rule, True_

# The resolved rule classes this world's rules resolve to (test/test_logic.py; the tracker export).
RESOLVED_KINDS = (True_.Resolved, False_.Resolved, Has.Resolved, HasAll.Resolved, HasAny.Resolved,
                  And.Resolved, Or.Resolved)


def need(names):
    """All of these items and events; none: open (True_)."""
    return HasAll(*names)


def ways(modes):
    """Any of the (event or None, keys) ways: the event, if any, and all of the way's keys. No ways:
    never (False_)."""
    return Or(*(HasAll(*((event,) if event is not None else ()), *keys) for event, keys in modes))


def any_of(alternatives):
    """Any of these item tuples, all of one; an empty tuple is open."""
    return Or(*(HasAll(*names) for names in alternatives))


def both(first, second):
    """Both rules (second None: the first alone)."""
    return first if second is None else And(first, second)


def as_rule(resolved):
    """A resolved rule (one of RESOLVED_KINDS) as the Rule it resolves from again: its `to_dict()`
    serialises the resolved (simplified, option-free) rule, and `SO2World.rule_from_dict` of that,
    resolved for the same world, is the same resolved rule (test/test_logic.py). For the tracker
    export (trackers.md B3)."""
    if isinstance(resolved, (And.Resolved, Or.Resolved)):
        return (And if isinstance(resolved, And.Resolved) else Or)(*map(as_rule, resolved.children))
    if isinstance(resolved, Has.Resolved):
        return Has(resolved.item_name, resolved.count)
    if isinstance(resolved, (HasAll.Resolved, HasAny.Resolved)):
        return (HasAll if isinstance(resolved, HasAll.Resolved) else HasAny)(*resolved.item_names)
    if isinstance(resolved, (True_.Resolved, False_.Resolved)):
        return True_() if resolved.always_true else False_()
    raise TypeError(f"not one of this world's rule kinds: {type(resolved).__qualname__}")


def open_by_default(world):
    """Every location (events too) and entrance of this world that set no rule of its own is open:
    True_. A spot's own rule, a Rule Builder rule or not, is left as it is (the no-lambda test sees
    any lambda that is left)."""
    for region in world.multiworld.get_regions(world.player):
        for spot in (*region.locations, *region.exits):
            if "access_rule" not in vars(spot):
                world.set_rule(spot, True_())


__all__ = ["RESOLVED_KINDS", "Has", "Rule", "True_", "need", "ways", "any_of", "both", "as_rule", "open_by_default"]
