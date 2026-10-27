-- The logic seam (docs/ap/poptracker.md "The logic seam"). This file is the pack's whole logic.
--
-- Every section's access rule is "^$so2_access|<AP location id>": PopTracker calls so2_access(id)
-- and reads what it returns as an AccessibilityLevel (the "^"). The pack holds no game logic of
-- its own: the plan (docs/plans/trackers.md, D2) is an evaluator over the room's per-seed export,
-- slot_data["tracker"] (B3), written by B4b in place of the bodies below. The four functions keep
-- their names and contracts:
--
--   so2_logic_load(slot_data)     the Clear handler, once per connect; slot_data is the room's
--                                 slot data. Sets SO2_LOGIC.mode and the banner
--                                 (so2_set_logic_status "checklist" or "on").
--   so2_logic_items_changed(code) after any item changes (so2.lua's watch on "*"): drop memos.
--   so2_access(id)                id is the location id as a string; returns an
--                                 AccessibilityLevel (None, SequenceBreak, Normal, ...).
--   so2_story_changed(story)      optional (B6): {chapter, step} from so2_story_{team}_{slot}.
--
-- Inputs for an evaluator: so2_item_count(ap_item_id) (so2.lua), SO2.slot_data, SO2.exists,
-- SO2.story. After changing what so2_access returns without an item change, call so2_refresh().
--
-- As built by B4a (pack 1.0.0 before B4b): no evaluator. Every location the room has is shown as
-- reachable (a checklist) and the banner says that logic is unavailable for the room.

SO2_LOGIC = {mode = "offline", export = nil}

function so2_logic_load(slot_data)
    SO2_LOGIC.export = slot_data and slot_data.tracker or nil
    SO2_LOGIC.mode = "checklist"
    so2_set_logic_status("checklist")
end

function so2_logic_items_changed(code)
end

function so2_access(id)
    return AccessibilityLevel.Normal
end
