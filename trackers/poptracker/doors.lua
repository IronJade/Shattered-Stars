-- Rooms and doors (docs/trackers/poptracker.md "Rooms and doors"; docs/trackers/tracker_maps.md "For the pack
-- (part 2)"). Every room tab has a door marker per doorway (a diamond on the map, a "Door <letter>"
-- section that is no Archipelago location) and a door panel beside the map: a Lua item per doorway
-- whose badge reads "A -> 412 Salva Drift". No destination is ever drawn into an image: they are
-- shown live, from the vanilla door table (scripts/doors_data.lua) or, with the entrance randomizer
-- (Phase C), from the room's pairings and the doors found so far. Clicking a door marker or its panel
-- line opens the destination room's tab.

SO2_DOORS = {
    -- Entrance randomizer (Phase C; provisional names, the seam only): slot data `door_pairs`
    -- {"<doorway id>": <doorway id at the other end>} and the data-storage key
    -- so2_doors_{team}_{slot} (a list of the doorway ids found). With pairs, a doorway's
    -- destination is its pair's room once found, "?" before; without pairs, the vanilla table.
    pairs_key = "door_pairs",
    found_key = "so2_doors",
    pairs = nil,     -- doorway id -> doorway id at the other end, nil: vanilla
    found = nil,     -- doorway id -> true, nil: every pair counts as found
    pending = nil,   -- a door section clicked: acted on in the next frame
    items = {},      -- doorway id -> its panel item
    room_of = {},    -- doorway id -> its room (map), from SO2_DOORWAYS
}

for id, d in pairs(SO2_DOORWAYS or {}) do
    SO2_DOORS.room_of[id] = d.map
end

local ARROW = "\226\134\146"   -- U+2192 (DejaVu Sans, PopTracker's font, has it)
local LINE_CHARS = 40   -- a panel line, cut with ".." to fit the bar

-- The live destinations of doorway `id`: a list of {map, back, cond}, or nil while unknown (a
-- randomized doorway not found yet).
function so2_door_destinations(id)
    id = tonumber(id)
    local d = SO2_DOORWAYS[id]
    if not d then
        return nil
    end
    local pairs_ = SO2_DOORS.pairs
    if pairs_ and pairs_[id] ~= nil then
        if SO2_DOORS.found and not SO2_DOORS.found[id] then
            return nil
        end
        local other = pairs_[id]
        local map = SO2_DOORS.room_of[other]
        if not map then
            return nil
        end
        return {{map = map, back = other}}
    end
    return d.to
end

-- The destinations by map, each once: the unconditional ones first, then the story variants (a
-- map some doors of the doorway reach in any story state counts as unconditional).
function so2_door_order(dests)
    local order, seen = {}, {}
    for pass = 1, 2 do
        for _, t in ipairs(dests or {}) do
            if not seen[t.map] and ((pass == 1) == not t.cond) then
                seen[t.map] = true
                table.insert(order, t)
            end
        end
    end
    return order
end

-- The destination a click opens: the first unconditional one, else the first; `which` > 1 picks
-- the next ones in turn (a doorway's story variants).
function so2_door_pick(dests, which)
    local order = so2_door_order(dests)
    if #order == 0 then
        return nil
    end
    return order[((which or 1) - 1) % #order + 1]
end

-- A map's name for the panel: a room's, a world map's, else the generated name of an undrawn map.
function so2_map_title(map)
    local room = SO2_ROOMS[map]
    if room then
        return string.format("%d %s", map, room.name)
    end
    local name = SO2_MAP_NAMES[map]
    if name and name ~= "" then
        return (map <= 3) and name or string.format("%d %s", map, name)
    end
    return string.format("map %d", map)
end

-- Characters, not bytes (the arrow is three bytes in UTF-8).
local function ulen(text)
    local _, n = text:gsub("[^\128-\191]", "")
    return n
end

local function cut(text, n)
    if ulen(text) <= n then
        return text
    end
    local out, k = {}, 0
    for ch in text:gmatch("[\0-\127\194-\244][\128-\191]*") do
        if k >= n - 2 then
            break
        end
        table.insert(out, ch)
        k = k + 1
    end
    return table.concat(out) .. ".."
end

-- A doorway's panel line: "A -> 412 Salva Drift", "*" marking a destination that only happens in
-- some story states, the other destinations by number ("(+53* 46)", at most three, then "+N");
-- "A -> ?" while unknown.
function so2_door_text(id)
    local d = SO2_DOORWAYS[tonumber(id)]
    if not d then
        return ""
    end
    local dests = so2_door_destinations(id)
    if dests == nil or #dests == 0 then
        return string.format("%s %s ?", d.letter, ARROW)
    end
    local order = so2_door_order(dests)
    local first = order[1]
    local others = {}
    for k = 2, math.min(#order, 4) do
        table.insert(others, tostring(order[k].map) .. (order[k].cond and "*" or ""))
    end
    local more = #order > 4 and string.format(" +%d", #order - 4) or ""
    local tail = #others > 0 and (" (+" .. table.concat(others, " ") .. more .. ")") or ""
    local head = string.format("%s %s %s%s", d.letter, ARROW, so2_map_title(first.map), first.cond and "*" or "")
    local room = LINE_CHARS - ulen(tail)
    return cut(head, math.max(12, room)) .. tail
end

-- The door markers' colour (each door section's access rule): SequenceBreak (yellow), the one
-- colour no check uses (checks are green / red / blue / grey, orange / teal / purple when mixed),
-- so a door never looks like a check. A door not found yet (the entrance randomizer, Phase C) is
-- yellow too for now (its panel line reads "?"); its own colour is Phase C's decision, and this
-- is the one place to change it (so2_door_destinations(id) == nil there).
function so2_door_access(id)
    return AccessibilityLevel.SequenceBreak
end

-- Opens the destination's tab, outermost first; false when there is none (unknown, or a map that
-- has no tab: an undrawn story scene).
function so2_door_go(id, which)
    local t = so2_door_pick(so2_door_destinations(id), which)
    local tabs = t and SO2_MAP_TABS[t.map]
    if not tabs then
        return false
    end
    for _, title in ipairs(tabs) do
        Tracker:UiHint("ActivateTab", title)
    end
    return true
end

-- Every panel line again (after the pairs or the found doors change).
function so2_doors_refresh()
    for id, item in pairs(SO2_DOORS.items) do
        item.BadgeText = so2_door_text(id)
    end
    if so2_refresh then
        so2_refresh()    -- the door sections' colour
    end
end

-- The Clear handler: the room's pairings, if it randomizes doors (Phase C).
function so2_doors_load(slot_data)
    local raw = slot_data and slot_data[SO2_DOORS.pairs_key]
    SO2_DOORS.pairs, SO2_DOORS.found = nil, nil
    if type(raw) == "table" then
        local p = {}
        for k, v in pairs(raw) do
            if tonumber(k) and tonumber(v) then
                p[tonumber(k)] = tonumber(v)
            end
        end
        SO2_DOORS.pairs = p
        SO2_DOORS.found = {}    -- nothing found until the key says so
    end
    so2_doors_refresh()
end

-- so2_doors_{team}_{slot}: the doorway ids found (a list, or {id: true}).
function so2_doors_found(value)
    if not SO2_DOORS.pairs then
        return
    end
    local found = {}
    if type(value) == "table" then
        for k, v in pairs(value) do
            if type(k) == "number" and tonumber(v) then
                found[tonumber(v)] = true      -- a list
            elseif tonumber(k) and v then
                found[tonumber(k)] = true      -- a set
            end
        end
    end
    SO2_DOORS.found = found
    so2_doors_refresh()
end

-- A door section changed: a click (fewer available than its one chest). Its count is set back and
-- the tab opened in the next frame, outside PopTracker's own click handling.
function so2_door_section_changed(section)
    local id = SO2_DOOR_SECTIONS[section.FullID]
    if id and section.AvailableChestCount < section.ChestCount then
        SO2_DOORS.pending = {full_id = section.FullID, id = id}
    end
end

function so2_doors_frame()
    local p = SO2_DOORS.pending
    if not p then
        return
    end
    SO2_DOORS.pending = nil
    local section = Tracker:FindObjectForCode("@" .. p.full_id)
    if section then
        section.AvailableChestCount = section.ChestCount
    end
    so2_door_go(p.id)
end

-- The panel: a Lua item per doorway (code door_<map>_<letter>, as the room tabs' item grids name
-- them). Left click: the destination; right click: the doorway's other destinations in turn.
local function make_item(id, d)
    local item = ScriptHost:CreateLuaItem()
    local which = 1
    item.Name = string.format("Door %s of %d", d.letter, d.map)
    item.Icon = ImageReference:FromPackRelativePath("images/doors/line.png")
    item.PotentialCodes = {d.code}
    item.CanProvideCodeFunc = function(self, code) return code == d.code end
    item.ProvidesCodeFunc = function(self, code) return 0 end
    item.OnLeftClickFunc = function(self)
        which = 1
        so2_door_go(id, 1)
    end
    item.OnRightClickFunc = function(self)
        which = which + 1
        so2_door_go(id, which)
    end
    item:SetOverlayAlign("left")
    item:SetOverlayFontSize(12)
    item.BadgeText = so2_door_text(id)
    return item
end

if not SO2_ITEMS_ONLY then
    for id, d in pairs(SO2_DOORWAYS) do
        SO2_DOORS.items[id] = make_item(id, d)
    end
    ScriptHost:AddOnLocationSectionChangedHandler("so2 doors", so2_door_section_changed)
    ScriptHost:AddOnFrameHandler("so2 doors", so2_doors_frame)
end
