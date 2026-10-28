#pragma once

#include "core/types.h"

namespace ss::game::field {

struct ScriptCtx;

// Overlay 0x8008d928 (script ext 0xa8): one-off story operations. 0 sub-overlay object
// (embedded map code); 1 / 2 Claude's name ('????' or the entered one) and party order;
// 3 / 4 colour / blend mode of every BG-animation frame (field_flags +0x38..); 5 reset
// the 12 names; 6 sort actors by screen y; 7 disc 2 (config +0x4c); 8 / 9 scroll layers
// off / on; 0xb field_flags +0x82; 0xc fixed stats for Dias; 0xd Dias's starting data again;
// 0xe / 0xf the sub-overlay's per-frame hook; 0x12 record +0x8a; 0x13 stop the field song.
void field_special_command(ScriptCtx* ctx, int op);

// 0x800531dc: moves the character with id 'character' + 1 to party slot 'slot' (the two
// slots and their records swap).
void party_move_character_to_slot(int character, int slot);

}  // namespace ss::game::field
