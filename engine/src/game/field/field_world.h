#pragma once

#include "core/types.h"
#include "game/field/field.h"

// field_update (0x8004c334) on the world map (field_world.cpp): the play-state checks
// and states 9-0xe (the vehicle), 0x11-0x1a (the bunny), 0x1b-0x20 (the clock event and
// the camp menu's warp).
namespace ss::game::field {

struct ScriptCtx;

// State 0 after the post-battle check (script thread idle, leader in control): the clock
// event, getting off the bunny, boarding / landing the vehicle. True when the frame
// ends here.
bool field_world_play_checks(Field* f);
// States 9-0xe and 0x11-0x20.
void field_world_update(Field* f);
// The camp menu's results 4 (warp) and 5 (bunny call), field_update state 3.
void field_world_menu_result(Field* f, int result);
// field_load_map's end with script flag 14 (on the bunny): its music 0xa92 again, state 0x11.
void field_world_bunny_reload(Field* f);
// overlay 0x80082b6c: whether (x, z) (x == 10000000: the leader) is in a world trigger.
int worldmap_trigger_test(ScriptCtx* ctx, s32 x, s32 z);

}  // namespace ss::game::field
