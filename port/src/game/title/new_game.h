#pragma once

#include "core/types.h"
#include "game/common/screen.h"

namespace ss::game {

// 0x80042cc8: the New Game option screens: memory card notice, Sound Select,
// Vibration, character (Claude / Rena), name entry, combat mode and (once unlocked)
// Battle Rank. Choices go to the config block (0x44 sound, 0x45 character, 0x46
// vibration, 0x49-0x4b combat mode, 0x4f battle rank); the name goes to 'name'.
// Returns 0 to start the game, 1 when the player backs out to the title.
// 'holder' receives the sequence's render context, as the original's 4-byte object.
int new_game_sequence(RenderContext** holder, char* name);

// 0x80046f8c: one textured sprite (and its draw mode) on a render context.
void new_game_draw_sprite(RenderContext* ctx, int x, int y, int w, int h, int u, int v, int tpage, int clut,
                          int brightness, bool semi);
// 0x80046e64: the sound demo of the later screens (entry #2675 into 'bank' / 'sequence').
void new_game_sound_update(u8* bank, u8* sequence);
// 0x8004789c: each channel of a 0xBBGGRR colour times level / 256 (GTE GPF, sf = 1).
u32 scale_colour(u32 colour, int level);

}  // namespace ss::game
