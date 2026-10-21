#pragma once

#include "core/types.h"
#include "game/boot/system.h"
#include "game/common/memcard.h"  // the memory card manager (ported; no longer a stub)

// Subsystems the title program calls that are not ported yet. Each logs once
// and behaves as "finished" so the original control flow continues; the
// checklist (docs/CHECKLIST.md) tracks replacing them.
namespace ss::game {

// Sound system on System + 0x5aa4.
void sound_load_bank(Sound* s, const void* bank, int a, int b);  // 0x80018ff4: SPU transfer of a VAB-like bank
int sound_bank_busy(Sound* s);                                   // 0x80018f04
void sound_set_sequences(Sound* s, void* data);                  // 0x80014508
void sound_set_volume(Sound* s, int a, int b);                   // 0x8001ab00
void sound_play_sequence(Sound* s, void* data);                  // 0x8001a5d8
void sound_stop(Sound* s);                                       // 0x8001a650
void sound_fade_out(Sound* s, int frames);                       // 0x8001a7e4: fade the music out
int sound_play_sfx(Sound* s, int id, int pan, int volume);       // 0x80019b1c
int sound_output_mode(Sound* s);                                 // 0x8001ab30 (+0x32): 0 surround, 1 stereo, 2 mono
void sound_set_output_mode(Sound* s, int mode);                  // 0x8001aaf0
void sound_set_pan(Sound* s, int pan, int volume);               // 0x8001a9e4 (the Sound Select demo's sweep)
void sound_nop_8001aabc(Sound* s, int a, int b);                 // 0x8001aabc: empty in this build

}  // namespace ss::game
