-- The pack's logic (docs/ap/poptracker.md "Logic"): a generic evaluator of the room's tracker
-- export, slot_data["tracker"] (so2.apworld 0.2.2+; docs/ap/seed_format.md "The tracker export").
-- The pack holds no game logic of its own (decision D2): the world writes its resolved rules,
-- regions, entrances and events per seed, and this file only reads them. The algorithm is the
-- reference evaluator's, tools/ap_tracker_logic.py, step for step (tools/ap_poptracker_test.py
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
-- The seam's four functions (docs/ap/poptracker.md "The logic seam"):
--   so2_logic_load(slot_data)     the Clear handler, once per connect: read the export, banner.
--   so2_logic_items_changed(code) after any item changes: drop the memo.
--   so2_access(id)                id is the AP location id as a string: an AccessibilityLevel.
--   so2_story_changed(story)      optional (B6), not defined here.
-- Rooms without the export (0.2.0 / 0.2.1) or with a format this pack doesn't read: every location
-- the room has shows as reachable (a checklist) and the banner says so. Entrances are named
-- ({name, from, to, rule}); so2_logic_connect connects a deferred one (Phase C's randomizer).

SO2_LOGIC = {mode = "offline", export = nil}
SO2_LOGIC_FORMATS = {[1] = true}    -- the export formats this pack reads

local L = nil       -- the loaded export: regions, rules, entrances, events, goal, loc (AP id -> {region, rule, ...})
local view = nil    -- the result for the current items: reached, got, memo, counts

function so2_logic_load(slot_data)
    local ex = slot_data and slot_data.tracker or nil
    SO2_LOGIC.export = ex
    L, view = nil, nil
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
        L = {regions = ex.regions, rules = ex.rules, entrances = entrances, events = ex.events, goal = ex.goal,
             loc = loc}
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
    view = nil
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
        view = nil
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

-- The region sweep and the events, to a fixpoint, for the current items.
local function sweep()
    local v = {reached = {[0] = true}, got = {}, memo = {}, counts = {}}
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
            if not used_events[k] and v.reached[e[2]] and holds(v, e[3]) then
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
        view = sweep()
    end
    return view
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

-- The goal (the completion condition) holds? nil without an export.
function so2_goal_in_logic()
    local v = so2_logic_view()
    return v and holds(v, L.goal)
end

function so2_access(id)
    local ok = so2_in_logic(tonumber(id))
    if ok == nil or ok then
        return AccessibilityLevel.Normal
    end
    return AccessibilityLevel.None
end
