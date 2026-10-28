#pragma once

#include "core/types.h"
#include "game/common/screen.h"
#include "game/title/intro.h"

// The title's Voice Collection (title choice 2; title 0x8003fb78-0x80041a00). The
// voices heard in battle (the unlock bits at config + 0x1a0, merged from every save on
// the memory cards by memcard_update) can be played back by number; at 75% of them the
// battle music player (BGM, random, stop) opens, at 50% the title's Battle Rank
// unlocks (0x8004ec28), at 95% the grid reacts to the sound voices. The grid behind
// the menu draws one cell per voice (32 x 40): heard ones as a textured quad of a
// picture (entry #13 block 1), the others as wire frames.
namespace ss::game {

// 0x80041100: the grid (0x4480 bytes; its own render context, 0x1c000-byte arena and
// 0x80-entry ordering table).
struct VoiceGrid {
  s32 level[0x80][0x22] = {};  // +0x0000: row 0 the columns' levels (32 + 2), rows 1-0x60 their history
  s32 last_active[24] = {};    // +0x4400: each sound voice's key-on byte (+0x26) last frame
  s32 field_4460 = 1;          // +0x4460
  s32 scroll_phase = 0;        // +0x4464: the history scrolls every other frame
  u16 tpage = 0x87;            // +0x4468: the picture (8-bit at (0x1c0, 0))
  u16 clut = 0x7800;           // +0x446a: its palette (row 0x1e0)
  s32 u_scale = 0xff;          // +0x446c
  s32 v_scale = 0xff;          // +0x4470
  s32 current = 0;             // +0x4474: the selected voice (white wire frame / palette row 0x1e3)
  s32 reactive = 0;            // +0x4478: 95% heard: the levels follow the sound voices
  RenderContext* ctx = nullptr;  // +0x447c
};

VoiceGrid* voice_grid_create(VoiceGrid* g);   // 0x80041100
void voice_grid_destroy(VoiceGrid* g);        // 0x8004137c (flags 3)
void voice_grid_update(VoiceGrid* g);         // 0x800413ec: levels, then the cells into its OT

// 0x800411a4: 'value' in 'digits' digits right-aligned before x + digits * 10 (leading
// zeros left out), from the text sheet's digit strip (0x8004e2dc).
void draw_number(TitleIntro* self, int x, int y, int value, int digits, int clut_row, int brightness);

void voice_collection(TitleIntro* self);     // 0x8003fb78

}  // namespace ss::game
