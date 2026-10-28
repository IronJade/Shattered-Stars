-- The pack's logic (docs/trackers/poptracker.md "Logic"): a generic evaluator of the room's tracker
-- export, slot_data["tracker"] (so2.apworld 0.2.2+; docs/apworld/seed_format.md "The tracker export").
-- The pack holds no game logic of its own (decision D2): the world writes its resolved rules,
-- regions, entrances and events per seed, and this file only reads them. The algorithm is the
-- reference evaluator's, trackers/tools/ap_tracker_logic.py, step for step (trackers/tools/ap_poptracker_test.py
-- checks the two agree for every option mix and many item states):
--
--   start in region 0 with no events; repeat passes until one changes nothing: an entrance from a
--   reached region whose rule holds reaches its region (one not connected yet, `to` -1, reaches
--   nothing), an event in a reached region whose rule holds is collected (once). A location is in
--   logic when its region is reached and its rule holds.
--
-- Every rule kind is monotone, so the passes reach the state Archipelago's own sweep reaches. Rule
-- results are memoised per pass; the last pass changed nothing, so its memo serves the locations,
-- and the whole result is kept until an item changes (so2_logic_items_changed).
--
-- The seam's four functions (docs/trackers/poptracker.md "The logic seam"):
--   so2_logic_load(slot_data)     the Clear handler, once per connect: read the export, banner.
--   so2_logic_items_changed(code) after any item changes: drop the memo.
--   so2_access(id)                id is the AP location id as a string: an AccessibilityLevel.
--   so2_story_changed(story)      on each so2_story_* value: the story view is redone.
-- Rooms without the export (0.2.0 / 0.2.1) or with a format this pack doesn't read: every location
-- the room has shows as reachable (a checklist) and the banner says so. Entrances are named
-- ({name, from, to, rule}); so2_logic_connect connects a deferred one (Phase C's randomizer).
--
-- Story awareness (story mode; the export's optional `story` key, docs/apworld/seed_format.md "Story
-- awareness"; the reference's evaluate(counts, story)): the story position is the Chapter item (the
-- port's so2_story_* sets it, a click changes it) with the port's step while the chapters agree; it
-- has reached the points (`stages`: {name, chapter, step}) at or before it, the first always. A
-- second sweep holds back the story's events ({name, k}) and locations ({k, id, ...}) of points not
-- reached yet. A location in logic by the items but not for the story shows as Inspect ("later in
-- the story"). No `story` key
-- (open world, hub, older rooms), Chapter 0 (no position yet) or "Story" off: the plain logic.

SO2_LOGIC = {mode = "offline", export = nil}
SO2_LOGIC_FORMATS = {[1] = true}    -- the export formats this pack reads

local L = nil       -- the loaded export: regions, rules, entrances, events, goal, loc (AP id -> {region, rule, ...}),
                    -- story (nil or {stages, events = name -> k, loc = AP id -> k})
local view = nil    -- the result for the current items: reached, got, memo, counts
local sview = nil   -- the same with the story held back at point sview.stage

function so2_logic_load(slot_data)
    local ex = slot_data and slot_data.tracker or nil
    SO2_LOGIC.export = ex
    L, view, sview = nil, nil, nil
    if type(ex) == "table" and SO2_LOGIC_FORMATS[ex.format] then
        local loc = {}
        for _, group in ipairs(ex.locations) do
            for i = 3, #group do
                loc[group[i]] = group
            end
        end
        local entrances = {}
        for k, e in ipairs(ex.entrances) do
            entrances[k] = {e[1], e[2], e[3], e[4]}     -- a copy: so2_logic_connect may fill `to`
        end
        local story = nil
        if type(ex.story) == "table" and type(ex.story.stages) == "table" then
            story = {stages = ex.story.stages, events = {}, loc = {}}
            for _, e in ipairs(ex.story.events or {}) do
                story.events[e[1]] = e[2]
            end
            for _, group in ipairs(ex.story.locations or {}) do
                for i = 2, #group do
                    story.loc[group[i]] = group[1]
                end
            end
        end
        L = {regions = ex.regions, rules = ex.rules, entrances = entrances, events = ex.events, goal = ex.goal,
             loc = loc, story = story}
        SO2_LOGIC.mode = "on"
        so2_set_logic_status("on")
    elseif type(ex) == "table" then
        SO2_LOGIC.mode = "newer"        -- a newer world's export: checklist until the pack is updated
        so2_set_logic_status("newer")
        print("so2: this room's logic export is format " .. tostring(ex.format) .. "; this pack reads 1")
    else
        SO2_LOGIC.mode = "checklist"
        so2_set_logic_status("checklist")
    end
end

function so2_logic_items_changed(code)
    view, sview = nil, nil
end

-- An entrance the export leaves unconnected (`to` -1: an entrance randomizer's deferred one) now
-- leads to region `region` (a name), as the slot data's pairings or a data-storage key say.
-- Returns whether it knew the entrance. Re-runs the rules.
function so2_logic_connect(name, region)
    if L == nil then
        return false
    end
    local target = nil
    for k, r in ipairs(L.regions) do
        if r == region then target = k - 1 end
    end
    local known = false
    for _, e in ipairs(L.entrances) do
        if e[1] == name and target ~= nil then
            e[3] = target
            known = true
        end
    end
    if known then
        view, sview = nil, nil
        so2_refresh()
    end
    return known
end

-- How many of `ref` the player has: an AP item id (a number: so2_item_count) or an event (its name).
local function count(v, ref)
    if type(ref) == "string" then
        return v.got[ref] or 0
    end
    local n = v.counts[ref]
    if n == nil then
        n = so2_item_count(ref)
        v.counts[ref] = n
    end
    return n
end

-- Does rule k (0-based, as the export names it) hold? Memoised in v.memo for the current pass.
local function holds(v, k)
    local result = v.memo[k]
    if result ~= nil then
        return result
    end
    local body = L.rules[k + 1]
    if body == true or body == false then
        result = body
    else
        local kind = body[1]
        if kind == "has" then
            result = count(v, body[2]) >= body[3]
        elseif kind == "all" then
            result = true
            for i = 2, #body do
                if count(v, body[i]) < 1 then result = false break end
            end
        elseif kind == "any" then
            result = false
            for i = 2, #body do
                if count(v, body[i]) >= 1 then result = true break end
            end
        elseif kind == "and" then
            result = true
            for i = 2, #body do
                if not holds(v, body[i]) then result = false break end
            end
        elseif kind == "or" then
            result = false
            for i = 2, #body do
                if holds(v, body[i]) then result = true break end
            end
        elseif kind == "region" then
            result = v.reached[body[2]] == true
        else
            error("so2: rule " .. k .. ": unknown kind " .. tostring(kind))
        end
    end
    v.memo[k] = result
    return result
end

-- The region sweep and the events, to a fixpoint, for the current items; with `stage`, the story's
-- events of later points are held back (v.stage: the point reached, nil for the plain sweep).
local function sweep(stage)
    local v = {reached = {[0] = true}, got = {}, memo = {}, counts = {}, stage = stage}
    local held = {}
    if stage ~= nil then
        for k, e in ipairs(L.events) do
            local need = L.story.events[e[1]]
            held[k] = need ~= nil and need > stage
        end
    end
    local used_entrances, used_events = {}, {}
    while true do
        v.memo = {}
        local changed = false
        for k, e in ipairs(L.entrances) do
            if not used_entrances[k] and e[3] >= 0 and v.reached[e[2]] and holds(v, e[4]) then
                used_entrances[k] = true
                if not v.reached[e[3]] then
                    v.reached[e[3]] = true
                    changed = true
                end
            end
        end
        for k, e in ipairs(L.events) do
            if not used_events[k] and not held[k] and v.reached[e[2]] and holds(v, e[3]) then
                used_events[k] = true
                v.got[e[1]] = (v.got[e[1]] or 0) + 1
                changed = true
            end
        end
        if not changed then
            return v
        end
    end
end

-- The current result (swept once per item change).
function so2_logic_view()
    if L == nil then
        return nil
    end
    if view == nil then
        view = sweep(nil)
    end
    return view
end

-- The story position: the Chapter item (0: none yet), with the port's step while its chapter is the
-- item's. Returns chapter, step, or nil.
function so2_story_position()
    local obj = Tracker:FindObjectForCode("so2_chapter")
    local chapter = obj and obj.AcquiredCount or 0
    if chapter <= 0 then
        return nil
    end
    local step = 0
    if SO2.story and SO2.story.chapter == chapter and type(SO2.story.step) == "number" then
        step = SO2.story.step
    end
    return chapter, step
end

-- How many story points the position has reached, or nil: no story awareness (no `story` key in the
-- export, no position, or the "Story" setting off).
function so2_story_stage()
    if L == nil or L.story == nil then
        return nil
    end
    local aware = Tracker:FindObjectForCode("so2_story_aware")
    if aware and not aware.Active then
        return nil
    end
    local chapter, step = so2_story_position()
    if chapter == nil then
        return nil
    end
    local n = 1     -- at least the first point: a chapter before 3 is the story's start
    for k, p in ipairs(L.story.stages) do
        if k > 1 and (chapter > p[2] or (chapter == p[2] and step >= p[3])) then
            n = k
        end
    end
    return n
end

-- The result with the story held back (nil without story awareness), swept once per item or story
-- change.
function so2_story_view()
    local stage = so2_story_stage()
    if stage == nil then
        return nil
    end
    if sview == nil or sview.stage ~= stage then
        sview = sweep(stage)
    end
    return sview
end

-- A port's so2_story_* value arrived (archipelago.lua, after the Chapter item): redo the story view.
function so2_story_changed(story)
    sview = nil
    so2_refresh()
end

-- Is location `id` (a number) in logic? nil without an export or for a location it doesn't list.
function so2_in_logic(id)
    local v = so2_logic_view()
    local group = v and L.loc[id]
    if group == nil then
        return nil
    end
    return v.reached[group[1]] == true and holds(v, group[2])
end

-- Is location `id` in logic for the story too? nil without story awareness (or as so2_in_logic).
function so2_story_in_logic(id)
    local v = so2_story_view()
    local group = v and L.loc[id]
    if group == nil then
        return nil
    end
    local need = L.story.loc[id]
    if need ~= nil and need > v.stage then
        return false
    end
    return v.reached[group[1]] == true and holds(v, group[2])
end

-- The goal (the completion condition) holds? nil without an export.
function so2_goal_in_logic()
    local v = so2_logic_view()
    return v and holds(v, L.goal)
end

-- Normal: in logic (and reached by the story); Inspect: in logic by the items, later in the story;
-- None: not in logic. Without an export: Normal (checklist).
function so2_access(id)
    local n = tonumber(id)
    local ok = so2_in_logic(n)
    if ok == nil then
        return AccessibilityLevel.Normal
    end
    if not ok then
        return AccessibilityLevel.None
    end
    if so2_story_in_logic(n) == false then
        return AccessibilityLevel.Inspect
    end
    return AccessibilityLevel.Normal
end
