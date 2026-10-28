-- Archipelago autotracking (docs/trackers/poptracker.md "Autotracking"), after the usual AP pack layout
-- (the template pack's item_mapping / location_mapping tables and handlers). The pack connects
-- as a tracker: it sends no checks (manifest flag "ap" only).

ScriptHost:LoadScript("scripts/autotracking/item_mapping.lua")
ScriptHost:LoadScript("scripts/autotracking/location_mapping.lua")

SO2_AP = {
    index = -1,  -- the last received item's index
    keys = {},   -- this slot's data-storage keys: hints, map, story
    map = nil,   -- the field map last reported by the port
}

-- Archipelago's hint status -> the section highlight.
local HINT_HIGHLIGHT = {}
if Highlight then
    HINT_HIGHLIGHT = {[0] = Highlight.Unspecified, [10] = Highlight.NoPriority, [20] = Highlight.Avoid,
                      [30] = Highlight.Priority, [40] = Highlight.None}
end

local function ds_key(prefix)
    local team, slot = Archipelago.TeamNumber, Archipelago.PlayerNumber
    if team == nil or slot == nil or team < 0 or slot < 0 then
        return nil
    end
    return string.format("%s_%d_%d", prefix, math.floor(team), math.floor(slot))
end

local function reset_item(code, kind)
    local obj = Tracker:FindObjectForCode(code)
    if not obj then
        return
    end
    if kind == "toggle" then
        obj.Active = false
    elseif kind == "consumable" then
        obj.AcquiredCount = 0
    elseif kind == "progressive" then
        obj.CurrentStage = 0
        obj.Active = false
    end
end

local function add_item(code, kind)
    local obj = Tracker:FindObjectForCode(code)
    if not obj then
        return
    end
    if kind == "toggle" then
        obj.Active = true
    elseif kind == "consumable" then
        obj.AcquiredCount = obj.AcquiredCount + obj.Increment
    elseif kind == "progressive" then
        if obj.Active then
            obj.CurrentStage = obj.CurrentStage + 1
        else
            obj.Active = true
        end
    end
end

-- The Room tab: the slot data's options as setting items (pack_data.lua SO2_SETTINGS).
local function apply_settings(slot_data)
    for _, s in ipairs(SO2_SETTINGS) do
        local obj = Tracker:FindObjectForCode(s.code)
        local v = slot_data[s.key]
        if obj and s.kind == "choice" then
            if v == nil then
                v = s.default
            end
            for k, name in ipairs(s.values) do
                if name == v then
                    obj.CurrentStage = k - 1
                end
            end
        elseif obj then
            if v == nil then
                v = s.absent
                if v == nil then
                    v = s.default
                end
            end
            obj.Active = (v == true or v == 1)
        end
    end
end

-- Members outside the room's pool (slot data `characters`) and, in hub mode, dungeon keys the
-- room doesn't use (`dungeon_keys`) get the badge "out".
local function apply_party(slot_data)
    local pool, keys = nil, nil
    if type(slot_data.characters) == "table" then
        pool = {}
        for _, who in pairs(slot_data.characters) do
            pool[who] = true
        end
    end
    for who, code in pairs(SO2_CHARACTERS) do
        local obj = Tracker:FindObjectForCode(code)
        if obj then
            obj.BadgeText = (pool and not pool[who]) and "out" or ""
        end
    end
    if slot_data.world_mode == "hub" and type(slot_data.dungeon_keys) == "table" then
        keys = {}
        for _, name in pairs(slot_data.dungeon_keys) do
            keys[name] = true
        end
    end
    for name, code in pairs(SO2_DUNGEON_KEYS) do
        local obj = Tracker:FindObjectForCode(code)
        if obj then
            obj.BadgeText = (keys and not keys[name]) and "out" or ""
        end
    end
end

local function check_location(id)
    local entry = LOCATION_MAPPING[id]
    if not entry then
        return
    end
    for _, code in ipairs(entry) do
        local obj = Tracker:FindObjectForCode(code)
        if obj then
            obj.AvailableChestCount = 0
        end
    end
end

function so2_on_clear(slot_data)
    slot_data = slot_data or {}
    Tracker.BulkUpdate = true
    local ok, err = pcall(function()
        SO2_AP.index = -1
        SO2_AP.map = nil
        SO2.received = {}
        SO2.slot_data = slot_data
        SO2.story = nil
        for _, entry in pairs(LOCATION_MAPPING) do
            for _, code in ipairs(entry) do
                local obj = Tracker:FindObjectForCode(code)
                if obj then
                    obj.AvailableChestCount = obj.ChestCount
                    if obj.Highlight then
                        obj.Highlight = Highlight.None
                    end
                end
            end
        end
        for _, entry in pairs(ITEM_MAPPING) do
            for _, e in ipairs(entry) do
                reset_item(e[1], e[2])
            end
        end
        reset_item("so2_chapter", "consumable")
        apply_settings(slot_data)
        apply_party(slot_data)
        -- the locations this room has: the server's missing and checked ones
        local exists = {}
        for _, id in pairs(Archipelago.MissingLocations or {}) do
            exists[id] = true
        end
        for _, id in pairs(Archipelago.CheckedLocations or {}) do
            exists[id] = true
            check_location(id)
        end
        SO2.exists = exists
        SO2.connected = true
        so2_logic_load(slot_data)
        if so2_doors_load then
            so2_doors_load(slot_data)    -- the door panel: vanilla, or the room's pairings (Phase C)
        end
    end)
    Tracker.BulkUpdate = false
    if not ok then
        print("so2: clear handler failed: " .. tostring(err))
    end
    so2_refresh()
    SO2_AP.keys = {hints = ds_key("_read_hints"), map = ds_key("so2_map"), story = ds_key("so2_story")}
    if SO2_DOORS and SO2_DOORS.pairs then
        SO2_AP.keys.doors = ds_key(SO2_DOORS.found_key)   -- randomized doors found (Phase C)
    end
    local keys = {}
    for _, k in pairs(SO2_AP.keys) do
        table.insert(keys, k)
    end
    if #keys > 0 then
        Archipelago:SetNotify(keys)
        Archipelago:Get(keys)
    end
end

function so2_on_item(index, item_id, item_name, player)
    if index <= SO2_AP.index then
        return
    end
    SO2_AP.index = index
    SO2.received[item_id] = (SO2.received[item_id] or 0) + 1
    local entry = ITEM_MAPPING[item_id]
    if entry then
        for _, e in ipairs(entry) do
            add_item(e[1], e[2])
        end
    end
end

function so2_on_location(id, name)
    if SO2.exists and not SO2.exists[id] then
        SO2.exists[id] = true
        so2_refresh()
    end
    check_location(id)
end

-- so2_map_{team}_{slot}: {"map": M[, "x": X, "z": Z]} (docs/apworld/data_storage.md). A new map switches
-- the map tabs when "Auto tab" is on (a room: its own tab, inside its area); position-only updates
-- change nothing.
local function on_map(value)
    if type(value) ~= "table" or type(value.map) ~= "number" then
        return
    end
    local map = math.floor(value.map)
    if map == SO2_AP.map then
        return
    end
    SO2_AP.map = map
    local auto = Tracker:FindObjectForCode("so2_auto_tab")
    local tabs = SO2_MAP_TABS[map]
    if auto and auto.Active and tabs then
        for _, title in ipairs(tabs) do
            Tracker:UiHint("ActivateTab", title)
        end
    end
end

-- so2_story_{team}_{slot}: {"chapter": C, "step": S}. Sets the Chapter item, which the story awareness
-- reads (logic.lua so2_story_stage), then tells the logic (so2_story_changed: a step-only change too).
local function on_story(value)
    if type(value) ~= "table" or type(value.chapter) ~= "number" then
        return
    end
    SO2.story = {chapter = math.floor(value.chapter), step = value.step}
    local obj = Tracker:FindObjectForCode("so2_chapter")
    if obj then
        obj.AcquiredCount = SO2.story.chapter
    end
    if so2_story_changed then
        so2_story_changed(SO2.story)
    end
end

-- _read_hints_{team}_{slot}: highlight the sections of hinted locations in this world.
local function on_hints(hints)
    if type(hints) ~= "table" or not Highlight then
        return
    end
    local highlight = {}
    for _, hint in ipairs(hints) do
        if hint.finding_player == Archipelago.PlayerNumber then
            local entry = LOCATION_MAPPING[hint.location]
            local h = HINT_HIGHLIGHT[hint.status]
            if h == nil then
                h = hint.found and Highlight.None or Highlight.Unspecified
            end
            if entry then
                for _, code in ipairs(entry) do
                    highlight[code] = h
                end
            end
        end
    end
    for code, h in pairs(highlight) do
        local obj = Tracker:FindObjectForCode(code)
        if obj and obj.Highlight ~= nil then
            obj.Highlight = h
        end
    end
end

function so2_on_data(key, value, old_value)
    if key == SO2_AP.keys.hints then
        on_hints(value)
    elseif key == SO2_AP.keys.map then
        on_map(value)
    elseif key == SO2_AP.keys.story then
        on_story(value)
    elseif SO2_AP.keys.doors and key == SO2_AP.keys.doors then
        so2_doors_found(value)
    end
end

if Archipelago then
    Archipelago:AddClearHandler("so2 clear", so2_on_clear)
    Archipelago:AddItemHandler("so2 item", so2_on_item)
    Archipelago:AddLocationHandler("so2 location", so2_on_location)
    Archipelago:AddRetrievedHandler("so2 retrieved", so2_on_data)
    Archipelago:AddSetReplyHandler("so2 set reply", so2_on_data)
end
