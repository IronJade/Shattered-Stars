#include "game/common/screen.h"

#include <cstring>
#include <memory>
#include <vector>

namespace ss::game {
namespace {

constexpr int kSlots = 12;

struct Arena {
  std::unique_ptr<u8[]> base[2];
  size_t size = 0;
  size_t used[2] = {};
};

struct Screen {
  int buffer = 1;                       // 0x8004ded4
  psyq::DRAWENV draw[2];                // 0x8004ded8 + i * 0x70
  psyq::DISPENV disp[2];                // 0x8004df34 + i * 0x70
  int draw_y[2] = {};                   // 0x8004e168
  Arena arenas[kSlots];                 // 0x8004dfb8 / 0x8004e018 / 0x8004e078
  std::vector<psyq::P_TAG> ots[kSlots][2];  // 0x8004e0d8, sizes at 0x8004e138
  void (*on_overflow)() = nullptr;      // 0x8004e170
};
Screen g_screen;

// 0x8003bdac: the display's screen rectangle for a buffer, then clears its VRAM.
void set_display_window(psyq::DISPENV* disp, int x, int y, int w, int h, int top, int bottom, int left, int right) {
  const int span = w - x;
  disp->screen.y = static_cast<s16>(top);
  disp->screen.h = static_cast<s16>((0xF0 - bottom) - top);
  disp->screen.x = static_cast<s16>((left << 8) / span);
  disp->screen.w = static_cast<s16>((((w - left) - right) * 0x100) / span);
  const psyq::RECT r{static_cast<s16>(x), static_cast<s16>(y), static_cast<s16>(w), static_cast<s16>(h)};
  psyq::ClearImage(&r, 0, 0, 0);
}

}  // namespace

void screen_init() {
  g_screen.buffer = 1;
  g_screen.on_overflow = nullptr;
  for (auto& a : g_screen.arenas) a = Arena{};
  for (auto& pair : g_screen.ots) pair[0].clear(), pair[1].clear();
}

void screen_setup_buffer0(int x, int y, int w, int h, int top, int bottom, int left, int right) {
  psyq::SetDefDrawEnv(&g_screen.draw[0], x, y, (w - left) - right, (h - top) - bottom);
  psyq::SetDefDispEnv(&g_screen.disp[1], x, y, w, h);
  g_screen.draw_y[0] = y;
  set_display_window(&g_screen.disp[1], x, y, w, h, top, bottom, left, right);
}

void screen_setup_buffer1(int x, int y, int w, int h, int top, int bottom, int left, int right) {
  psyq::SetDefDispEnv(&g_screen.disp[0], x, y, w, h);
  psyq::SetDefDrawEnv(&g_screen.draw[1], x, y, (w - left) - right, (h - top) - bottom);
  g_screen.draw_y[1] = y;
  set_display_window(&g_screen.disp[0], x, y, w, h, top, bottom, left, right);
}

psyq::DRAWENV* screen_drawenv(int buffer) { return &g_screen.draw[buffer]; }
psyq::DISPENV* screen_dispenv(int buffer) { return &g_screen.disp[buffer]; }

int screen_create_arena(size_t bytes) {
  for (int i = 0; i < kSlots; ++i) {
    Arena& a = g_screen.arenas[i];
    if (a.base[0]) continue;
    // Sizes are in PS1 bytes; host primitives carry a pointer-sized tag and are
    // up to 1.6x larger (LINE_G2: 20 -> 32 bytes), so the host arena is doubled.
    bytes *= 2;
    a.base[0] = std::make_unique<u8[]>(bytes);
    a.base[1] = std::make_unique<u8[]>(bytes);
    a.size = bytes;
    a.used[0] = a.used[1] = 0;
    return i;
  }
  return -1;
}

int screen_create_ot(int entries) {
  for (int i = 0; i < kSlots; ++i) {
    auto& pair = g_screen.ots[i];
    if (!pair[0].empty()) continue;
    pair[0].assign(static_cast<size_t>(entries), psyq::P_TAG{});
    pair[1].assign(static_cast<size_t>(entries), psyq::P_TAG{});
    return i;
  }
  return -1;
}

void screen_destroy_arena(int arena) { g_screen.arenas[arena] = Arena{}; }

void screen_destroy_ot(int ot) {
  g_screen.ots[ot][0].clear();
  g_screen.ots[ot][1].clear();
}

void screen_flip() {
  g_screen.buffer = 1 - g_screen.buffer;
  for (auto& a : g_screen.arenas) a.used[g_screen.buffer] = 0;
}

psyq::P_TAG* screen_clear_ot(int ot) {
  auto& table = g_screen.ots[ot][g_screen.buffer];
  return psyq::ClearOTag(table.data(), static_cast<int>(table.size()));
}

void screen_draw_ot(int ot) { psyq::DrawOTag(g_screen.ots[ot][g_screen.buffer].data()); }

void screen_put_env() {
  psyq::PutDispEnv(&g_screen.disp[g_screen.buffer]);
  psyq::PutDrawEnv(&g_screen.draw[g_screen.buffer]);
}

void* screen_alloc(int arena, size_t bytes) {
  Arena& a = g_screen.arenas[arena];
  bytes = (bytes + 7) & ~size_t{7};  // keep host structures aligned
  size_t& used = a.used[g_screen.buffer];
  if (used + bytes > a.size) {
    if (g_screen.on_overflow) g_screen.on_overflow();
    return nullptr;
  }
  void* p = a.base[g_screen.buffer].get() + used;
  used += bytes;
  std::memset(p, 0, bytes);
  return p;
}

int screen_draw_y() { return g_screen.draw_y[g_screen.buffer]; }

bool render_ctx_create_arena(RenderContext* ctx, size_t bytes) {
  ctx->arena = screen_create_arena(bytes);
  return ctx->arena != -1;
}

bool render_ctx_create_ot(RenderContext* ctx, int entries) {
  ctx->ot = screen_create_ot(entries);
  return ctx->ot != -1;
}

}  // namespace ss::game
