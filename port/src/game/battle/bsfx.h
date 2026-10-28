#pragma once

#include "core/types.h"
#include "game/battle/bobj.h"

// The battle's sound-effect front end (#1984 block 0): 8 tracked channels with 3-D
// panning from their emitters, and the sound-test record in the config (every effect
// heard sets its bit in config +0x1a0, checksummed at +0x50).
namespace ss::game::battle {

// 0x8007f3f0: mode 0 an effect (sound_play_sfx), 1 a clip (sound_open_clip on the data
// at 'clip'), 2 an effect on an internal emitter. Returns slot << 16 | sfx (0xffffffff
// when nothing played).
u32 bsfx_play(int sfx, const void* clip, BObj* emitter, int mode);
inline u32 bsfx_play_at(int sfx, BObj* emitter) { return bsfx_play(sfx, nullptr, emitter, 0); }  // 0x8007f3ac
inline u32 bsfx_play_ui(int sfx) { return bsfx_play(sfx, nullptr, nullptr, 0); }                // 0x8007f3cc
int bsfx_play_at_pos(int sfx, const s32 pos[3]);  // 0x8007f6c8
void bsfx_update();                               // 0x8007ff08 (battle_main_loop)
void bsfx_pan_update(int ch);                     // 0x8007ffa8
void bsfx_reset();                                // bsfx_emitters_init 0x8008016c + the channel table
// (Port, original_bugs.md B15) called as an object is destroyed: a channel that pans with it
// keeps its last position (the channel's internal emitter) instead of reading freed memory.
void bsfx_forget_emitter(const BObj* o);

// Calls on a sound by the handle bsfx_play returned (slot << 16 | sfx), made only while
// that sound still plays in its slot (sound_handle_active). Voice handles (0x40 / 0x41 <<
// 24, from the voice player) are never active in the port: voices are not ported.
bool sound_handle_active(u32 handle);                       // 0x8007fc54
void sound_handle_duck(u32 handle, s32 ticks);              // 0x8007fd98: sound_clip_duck
void sound_handle_fade_out(u32 handle, s16 frames, s16 delay);  // 0x8007fdec: sound_clip_fade_out
void sound_handle_pitch_up(u32 handle, s16 frames);         // 0x8007fe58: sound_clip_pitch_rise
void sound_handle_pitch_down(u32 handle, s16 frames);       // 0x8007feb0: sound_clip_pitch_drop

}  // namespace ss::game::battle
