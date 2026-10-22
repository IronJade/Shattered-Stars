#include "game/field/screen.h"

#include <cstring>

#include "game/field/field.h"
#include "runtime/runtime.h"

namespace ss::game::field {
namespace {

// Per-buffer primitive area: 0x14520 bytes on field maps, 0x1c520 on the world map.
// Host primitives carry a pointer-sized tag and are up to ~1.6x larger.
constexpr size_t kHostScale = 2;

Screen g_screen_storage;
Screen* g_screen = nullptr;

// 0x80048450: the display window of one buffer (screen x/w in 1/256 units of the
// span), then clears its VRAM.
void set_display_window(psyq::DISPENV* disp, int x, int y, int w, int h, int top, int bottom, int left, int right) {
  disp->screen.x = static_cast<s16>((left << 8) / (w - x));
  disp->screen.y = static_cast<s16>(top);
  disp->screen.w = static_cast<s16>((((w - left) - right) * 0x100) / (w - x));
  disp->screen.h = static_cast<s16>((0xF0 - bottom) - top);
  const psyq::RECT r{static_cast<s16>(x), static_cast<s16>(y), static_cast<s16>(w), static_cast<s16>(h)};
  psyq::ClearImage(&r, 0, 0, 0);
}

}  // namespace

Screen* field_screen() { return g_screen; }

void* Screen::alloc(size_t bytes) {
  bytes = (bytes + 7) & ~size_t{7};
  if (cursor + bytes > limit * kHostScale) {
    runtime::Runtime::get().log_once("field_screen_overflow", "field primitive area full; primitives dropped");
    return nullptr;
  }
  void* p = current->prims.get() + cursor;
  cursor += bytes;
  std::memset(p, 0, bytes);
  return p;
}

void* Screen::alloc_extra(size_t bytes) {
  bytes = (bytes + 7) & ~size_t{7};
  if (extra_cursor + bytes > extra_size) return nullptr;
  void* p = extra.get() + extra_cursor;
  extra_cursor += bytes;
  return p;
}

Screen* screen_construct(Screen* s) {
  s->current = nullptr;
  s->cursor = 0;
  s->limit = 0;
  g_screen = s;
  return s;
}

void screen_alloc_arena(Screen* s) {
  // One 0x40c40-byte allocation on the console (0x80075a7c); the host keeps each
  // part separately.
  for (auto& b : s->buffers) {
    b.ot.assign(kOtEntries, psyq::P_TAG{});
    b.aux_ot.assign(16, psyq::P_TAG{});
    b.prims = std::make_unique<u8[]>(0x1C520 * kHostScale);
  }
  s->extra_size = 0x14000 * kHostScale;
  s->extra = std::make_unique<u8[]>(s->extra_size);
}

void screen_layout_arena(Screen* s, int field_map) {
  s->field_map_layout = field_map != 0;
  s->extra_cursor = 0;
}

void screen_setup_buffer0(Screen* s, int x, int y, int w, int h, int top, int bottom, int left, int right) {
  psyq::SetDefDrawEnv(&s->buffers[0].draw, x, y, (w - left) - right, (h - top) - bottom);
  psyq::SetDefDispEnv(&s->buffers[1].disp, x, y, w, h);
  set_display_window(&s->buffers[1].disp, x, y, w, h, top, bottom, left, right);
}

void screen_setup_buffer1(Screen* s, int x, int y, int w, int h, int top, int bottom, int left, int right) {
  psyq::SetDefDispEnv(&s->buffers[0].disp, x, y, w, h);
  psyq::SetDefDrawEnv(&s->buffers[1].draw, x, y, (w - left) - right, (h - top) - bottom);
  set_display_window(&s->buffers[0].disp, x, y, w, h, top, bottom, left, right);
}

void screen_clear_vram(Screen* s) {
  psyq::ClearImage(&s->buffers[0].disp.disp, 0, 0, 0);
  psyq::ClearImage(&s->buffers[1].disp.disp, 0, 0, 0);
  psyq::DrawSync(0);
}

void screen_set_clear_colour(Screen* s, u8 r, u8 g, u8 b) {
  for (auto& buf : s->buffers) {
    buf.draw.r0 = r;
    buf.draw.g0 = g;
    buf.draw.b0 = b;
    buf.draw.dtd = 1;
    buf.draw.tpage = 0;
    buf.draw.isbg = 1;
  }
}

void screen_flip(Screen* s) {
  s->current = s->current == &s->buffers[0] ? &s->buffers[1] : &s->buffers[0];
  s->cursor = 0;
  const u8* flags = globals().field_flags;
  s->limit = flags && flags[0x68] != 0 ? 0x1C520 : 0x14520;
  s->extra_cursor = 0;
}

void screen_clear_ot(Screen* s) {
  psyq::ClearOTag(s->current->ot.data(), kOtEntries);
  psyq::ClearOTag(s->current->aux_ot.data(), 16);
}

void screen_compact_ot(Screen*) {
  // The console short-circuits empty entries so the GPU walks fewer tags; drawing
  // the host table is unaffected by empty entries.
}

}  // namespace ss::game::field
