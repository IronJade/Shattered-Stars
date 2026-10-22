#pragma once

#include "core/types.h"

// The field's voice queue (field 0x80078e0c-0x80079e58): while the player walks (load
// queue idle, streaming on), the active members' voice banks are read from the disc
// and uploaded to the sound driver's voice slots, so the battle has them. Battle mode
// (voice_queue_build_battle) loads the banks of each member's killer moves (casters:
// one bank by class); field mode (voice_queue_build_field) then fills the remaining
// SPU space with one random clip bank per member. Driven by load_queue_update
// (0x80043324) through VoiceStream (0x8007599c / 0x800759a0 / 0x80075998).
namespace ss::game::field {

int voice_party_spu_needed();               // 0x80078e0c: SPU budget the battle banks need
void voice_queue_reset();                   // 0x80078f88
bool voice_queue_pending();                 // 0x80078fc4: entries queued
bool voice_queue_busy();                    // 0x80078fd4: queued, or a buffer still in transfer
int voice_queue_build_battle(int budget);   // 0x80079000: remaining budget, -1 when it does not fit
int voice_queue_build_field(int budget);    // 0x80079308: remaining budget, -1 when nothing fits
void voice_slot_invalidate(int slot);       // 0x8007972c: the slot's banks must be reloaded
int voice_queue_update();                   // 0x80079764: one step of reading / uploading
void voice_queue_abort();                   // 0x80079a7c
// 0x80079c30: the voice bank entry of 'killer' (record +0xcc..) for the member in
// 'slot' (party slot, 0x60 bytes); 0 when the character has none.
int voice_killer_bank(const u8* slot, const u8* record, int killer);

}  // namespace ss::game::field
