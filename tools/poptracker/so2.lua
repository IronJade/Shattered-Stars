-- Shared state and helpers (docs/ap/poptracker.md "Lua"): which locations the room has, what the
-- server has sent, the logic banner and the refresh the rules need when Lua state changes.

SO2 = {
    connected = false, -- a room's slot data has been read (the Clear handler ran)
    exists = nil,      -- AP location id -> true for every location the room has; nil: show all
    received = {},     -- AP item id -> how many the server has sent (every item, mapped or not)
    slot_data = nil,   -- the room's slot data (docs/ap/seed_format.md "Slot data and trackers")
    story = nil,       -- {chapter, step} from so2_story_{team}_{slot} (docs/ap/data_storage.md)
}

-- Visibility rule of every section: "$so2_exists|<AP location id>". Before a room is connected
-- every location shows; afterwards only the ones the room has (missing or checked on the server).
function so2_exists(id)
    if SO2.exists == nil then
        return true
    end
    return SO2.exists[tonumber(id)] == true
end

-- How many of AP item `ap_id` the player has: the pack's own item when the id is mapped (so a
-- click counts as well as a received item), else what the server has sent.
function so2_item_count(ap_id)
    local entry = ITEM_MAPPING[ap_id]
    if entry then
        local n = 0
        for _, e in ipairs(entry) do
            n = n + Tracker:ProviderCountForCode(e[1])
        end
        return n
    end
    return SO2.received[ap_id] or 0
end

-- PopTracker re-runs the access and visibility rules when an item changes. When only Lua state
-- changed (the room's locations, the logic's inputs), change the internal counter item.
function so2_refresh()
    local rev = Tracker:FindObjectForCode("so2_rev")
    if rev then
        rev.AcquiredCount = rev.AcquiredCount + 1
    end
end

-- The logic banner (the "Logic" item at the top of the item panel): "offline" (no room yet),
-- "checklist" (no logic for this room: every location it has shows as reachable), "on" (the
-- room's logic export, logic.lua) or "newer" (an export this pack can't read yet: a checklist).
local LOGIC_STAGE = {offline = 0, checklist = 1, on = 2, newer = 3}
function so2_set_logic_status(key)
    local obj = Tracker:FindObjectForCode("so2_logic_offline")
    if obj then
        obj.CurrentStage = LOGIC_STAGE[key] or 0
    end
end

-- Every item change (received, clicked or reset) reaches the logic's hook, so an evaluator can
-- drop what it has memoized (logic.lua).
ScriptHost:AddWatchForCode("so2 logic inputs", "*", function(code)
    if so2_logic_items_changed then
        so2_logic_items_changed(code)
    end
end)
