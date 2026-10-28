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

// The map's music across loads (0x80075744 pending start, 0x80075746 current entry).
struct FieldMusic {
  u8 pending = 0;        // 0x80075744: installed, to start once the SPU is idle
  s16 current = -1;      // 0x80075746: entry of the music in place
  bool bank_kept = false;  // 0x80075740 != 0: the bank came through the load buffer
};
FieldMusic& field_music();
// 0x8004b6b0: reads 'entry' (0 = what the load buffer holds), installs it, and marks it
// to start (game_state +0x1c = entry).
void field_load_music_sync(Field* f, int entry);
// 0x80052bf4: starts the installed music once the SPU is idle (or at once with 'force');
// coming back from another program it resumes where the driver saved it.
void field_start_pending_music(bool force);

// The music kept across another program (handoff 0xf holds a 4-byte box with the
// driver's save buffer; sound_holder_create 0x8006f90c). Save: script table B 0x5a
// (the driver copies its tracks on its next tick). Restore: table B 0x5b and field_run
// when coming back (the driver restores on its next tick).
void sound_holder_save();
void sound_holder_restore();

}  // namespace ss::game::field
