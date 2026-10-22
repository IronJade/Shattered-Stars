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

}  // namespace ss::game::battle
