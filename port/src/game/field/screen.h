#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include "core/types.h"
#include "runtime/libgpu.h"

// The field's own screen object (0x10c bytes, global 0x80075290; 0x80048154-0x80048734):
// two 320x224 buffers drawn at y = 0 and y = 224, each with a 0x830-entry ordering
// table linked forward (higher indexes draw later, on top), a 16-entry auxiliary
// table and a primitive area. Fixed OT slots: 0x80c disc-error dim / poison flash,
// layer id + 0x11 layer splice, 0x818 game-over logo, 0x81e/0x81f transitions,
// 0x820 fades. (The title's screen manager also lives in the field program, for the
// menus: game/common/screen.)
namespace ss::game::field {

constexpr int kOtEntries = 0x830;

struct ScreenBuffer {                       // 0x7c bytes
  psyq::DRAWENV draw{};                     // +0x00
  psyq::DISPENV disp{};                     // +0x5c: shows the other buffer's image
  std::vector<psyq::P_TAG> ot;              // +0x70: kOtEntries
  std::unique_ptr<u8[]> prims;              // +0x74
  std::vector<psyq::P_TAG> aux_ot;          // +0x78: 16 entries
};

struct Screen {
  ScreenBuffer buffers[2];
  ScreenBuffer* current = nullptr;          // +0xf8
  size_t cursor = 0;                        // +0xfc: bytes used in current->prims
  size_t limit = 0;                         // +0x100: primitive area size for this mode
  std::unique_ptr<u8[]> extra;              // +0x104: extra arena after both buffers
  size_t extra_size = 0;
  size_t extra_cursor = 0;                  // +0x108
  bool field_map_layout = true;             // screen_layout_arena(1): smaller prim area

  // Typed allocation from the current buffer's primitive area (the original's
  // drawers bump +0xfc themselves; host primitives are larger, so the area is
  // sized for them). Returns zeroed memory.
  void* alloc(size_t bytes);
  template <typename T>
  T* alloc_prims(size_t count = 1) {
    return static_cast<T*>(alloc(sizeof(T) * count));
  }
  void* alloc_extra(size_t bytes);
  psyq::P_TAG* ot_entry(int index) { return &current->ot[static_cast<size_t>(index)]; }
};

Screen* screen_construct(Screen* s);                                      // 0x80048154
void screen_alloc_arena(Screen* s);                                       // 0x800481c0
void screen_layout_arena(Screen* s, int field_map);                       // 0x80048224
void screen_setup_buffer0(Screen* s, int x, int y, int w, int h, int top, int bottom, int left, int right);  // 0x80048280
void screen_setup_buffer1(Screen* s, int x, int y, int w, int h, int top, int bottom, int left, int right);  // 0x80048368
void screen_clear_vram(Screen* s);                                        // 0x800484e4
void screen_set_clear_colour(Screen* s, u8 r, u8 g, u8 b);                // 0x80048538
void screen_flip(Screen* s);                                              // 0x80048574
void screen_clear_ot(Screen* s);                                          // 0x8004860c
void screen_compact_ot(Screen* s);                                        // 0x800486d4 (host: nothing to do)

Screen* field_screen();  // 0x80075290

}  // namespace ss::game::field
