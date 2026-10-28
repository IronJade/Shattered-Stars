-- Star Ocean: The Second Story (Shattered Stars) for PopTracker: the entry point.
-- docs/ap/poptracker.md. Hand-written in tools/poptracker/; tools/ap_poptracker.py copies it into
-- the pack beside the generated files (pack_data.lua, map_tabs.lua, doors_data.lua,
-- autotracking/*_mapping.lua).

SO2_VARIANT = Tracker.ActiveVariantUID
SO2_ITEMS_ONLY = SO2_VARIANT == "items_only"

ScriptHost:LoadScript("scripts/pack_data.lua")
ScriptHost:LoadScript("scripts/map_tabs.lua")
ScriptHost:LoadScript("scripts/so2.lua")
ScriptHost:LoadScript("scripts/logic.lua")

Tracker:AddItems("items/items.json")
if not SO2_ITEMS_ONLY then
    -- rooms and doors: the door table, the door panel's items, the click handler
    ScriptHost:LoadScript("scripts/doors_data.lua")
    ScriptHost:LoadScript("scripts/doors.lua")
    Tracker:AddMaps("maps/maps.json")
    Tracker:AddLocations("locations/places.json")
    Tracker:AddLocations("locations/boards.json")
    Tracker:AddLocations("locations/world.json")
    Tracker:AddLocations("locations/doors.json")
end
Tracker:AddLayouts("layouts/items.json")
Tracker:AddLayouts("layouts/tracker.json")

ScriptHost:LoadScript("scripts/autotracking/archipelago.lua")
print(string.format("%s %s (%s) loaded", SO2_PACK.uid, SO2_PACK.version, SO2_VARIANT))
