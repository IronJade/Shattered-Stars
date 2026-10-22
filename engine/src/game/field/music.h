#pragma once

#include <vector>

#include "core/types.h"
#include "game/field/field.h"

// Field music (field_install_music 0x8004b2b4 and the buffers it keeps). A music entry
// holds a sound bank (at the offset in its first word) and an SLZ-packed sequence
// (from byte 4). Installing loads the bank into the sound driver and unpacks the
// sequence into g_music_sequence (0x80075734), which the scripts then start with
// sound_play_sequence.
namespace ss::game::field {

// 0x8004b2b4: installs 'entry' (0 = what field_load_entry just read). Returns 0 while a
// preload of the entry is still reading.
int field_install_music(Field* f, int entry);
std::vector<u8>& field_music_sequence();  // 0x80075734
void field_music_free_buffers();          // frees 0x80075740 (the bank) and the load buffer

// The music kept across another program (handoff 0xf holds a 4-byte box with the
// driver's save buffer; sound_bank_holder_new 0x8006f90c). Save: script table B 0x5a
// (the driver copies its tracks on its next tick). Restore: table B 0x5b and field_run
// when coming back (the driver restores on its next tick).
void sound_holder_save();
void sound_holder_restore();

}  // namespace ss::game::field
