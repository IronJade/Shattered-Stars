#pragma once

#include "core/types.h"

// The special stages' code (#1984 blocks 2-10, unpacked by battle_run into the stage code
// slot 0x8008c1f4 by stage id; stage 0xe uses the #2129 image's own function there) and the
// stage objects (Shot kinds 0x18-0x2b): rolling boulders, a runaway cart, hanging and
// toppling scenery, breakable crates, rain / debris drops and snow flakes, and the last
// battle's floating models. docs/notes/battle.md "stage code slot".
//
// Block by stage: 0x1a 2, 0x1f 3, 0x10 4, 0xd / 0x38 / 0x39 5, 0x17 6, 0x15 / 0x28 7,
// 0x24 8, 0x29 9, 0x33 10.
namespace ss::game::battle {

struct Shot;

// battle_run 0x8007160c (its stage switch): the stage objects the stage starts with
// (kinds 0x19 / 0x1a / 0x1c / 0x1f / 0x20 / 0x22 / 0x2a / 0x2b, and stage 0x17's two
// stage sprites).
void stage_objects_create();
// battle_frame_director 0x800715d0 (unless g_battle_flags & 0xc): the block's per-frame
// hook at 0x8008c1f4 on stages 0xd / 0x38 / 0x39 / 0x15 / 0x28 / 0x1a / 0x24 / 0x29.
void stage_code_update();
// Shot::update for kinds 0x18-0x2b of the stage code (the kind table 0x8006fea8 points them
// into the stage code slot); false for the other kinds.
bool stage_shot_update(Shot* s);

}  // namespace ss::game::battle
