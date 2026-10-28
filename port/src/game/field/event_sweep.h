#pragma once

#include "core/types.h"

// Debugging (not in the original): the event sweep. With SS_EVENT_SWEEP=1 and a --map N
// start, it waits for the map's main script to hand control to the player, then runs
// each of the map's event entry points in turn: the trigger regions, the Private Action
// regions, every NPC's talk entry and pickpocket entry. Before each one it puts back the
// state block, party, records and inventory it saw when the map first settled, and
// reloads the map. While an event runs it presses Cross (and Circle every fourth press)
// so windows and choices move on. Each entry ends as one of:
//   ok          back to free play on the same map
//   map M       the event took the party to map M
//   game over   the field's game over started
//   timeout     no free play within SS_EVENT_SWEEP_FRAMES (default 3600) frames
// with the stand-in services the event reached (vm_services: g_vm_standins) and the
// instructions no handler claims (g_vm_stats). Lines start with "[sweep]" on stderr; a
// crash or a hang shows as a missing "done" line, which port/tools/event_sweep.py reports.
// SS_EVENT_SWEEP_SKIP=k starts at entry k (to go on after a crash).
namespace ss::game::field {

struct Field;

void event_sweep_frame(Field* f);   // field_update, every frame (does nothing unless enabled)
void event_sweep_menu_frame();      // a menu's own frame loop (keeps the input going)

}  // namespace ss::game::field
