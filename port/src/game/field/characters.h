#pragma once

#include "core/types.h"

// The twelve characters' starting data, as the field program sets it up when one
// joins the party (party_slot_init 0x8007a20c and init_claude .. init_chisato
// 0x8007a37c-0x8007b0c0; the title program has its own copy for Claude and Rena). A
// party slot is 0x60 bytes (handoff 3), a character record 0xd0 (handoff 4).
namespace ss::game::field {

// 0x8007a20c: clears the slot and record, fills in character 'character' (1 Claude,
// 2 Rena, 3 Celine, 4 Bowman, 5 Dias, 6 Precis, 7 Ashton, 8 Leon, 9 Opera, 10 Ernest,
// 11 Noel, 12 Chisato) and stores the id in slot +0.
void party_slot_init(u8* slot, u8* record, int character);
// The per-character part alone (init_claude .. init_chisato), without the clearing and
// the id store (script ext 0xa8 op 0xd re-runs init_dias on Dias's slot this way).
void character_init_data(u8* slot, u8* record, int character);
// 0x8007b1f0: up to four talents (record +0x20 bits) from a table of ten {u16 bit,
// u16 chance%} entries, in shuffled order (a 100% talent is tried first).
void roll_talents(u8* record, const u8* table);

}  // namespace ss::game::field
