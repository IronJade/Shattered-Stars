#include "game/common/disc_screen.h"

#include <cstdio>
#include <cstring>
#include <vector>

#include "disc/slz.h"
#include "game/boot/pad.h"
#include "game/boot/rng.h"
#include "game/boot/system.h"
#include "runtime/libgpu.h"
#include "runtime/runtime.h"

namespace ss::game {
namespace {

u16 rd16(const u8* p) {
  u16 v;
  std::memcpy(&v, p, 2);
  return v;
}

std::vector<u8> read_entry(int entry) {
  Loader& ld = get_system()->loader;
  std::vector<u8> data(static_cast<size_t>(std::max(0, ld.entry_size(entry))), 0);
  if (!data.empty()) ld.load_sync(data.data(), entry, 0);
  return data;
}

// field 0x8006fc64 (disc_screen_load_tim): SLZ block 'block' of a pack is a 24-bit TIM
// {.., +0x10 u16 w (16-bit units), +0x12 u16 h, pixels}; it goes to (x * 1.5, y) (x in
// 640-wide screen pixels), right-aligned on x with align bit 0, bottom-aligned on y with
// bit 1. 'clear' first blanks the 0x36 lines above y (twice, a frame apart).
void draw_tim(const std::vector<u8>& pack, int block, int x, int y, int align, bool clear) {
  if (clear) {
    psyq::VSync(0);
    const psyq::RECT band{0, static_cast<s16>(y - 0x36), 0x3c0, 0x36};
    psyq::ClearImage(&band, 0, 0, 0);
    psyq::DrawSync(0);
    psyq::VSync(0);
    psyq::ClearImage(&band, 0, 0, 0);
    psyq::DrawSync(0);
  }
  auto tim = disc::slz::unpack_block(std::span<const u8>(pack), 0, block, nullptr);
  if (!tim || tim->size() < 0x14) return;
  psyq::RECT r{};
  r.w = static_cast<s16>(rd16(tim->data() + 0x10));
  r.h = static_cast<s16>(rd16(tim->data() + 0x12));
  if ((align & 1) == 0) {
    r.x = static_cast<s16>((x * 3) >> 1);
  } else {
    r.x = static_cast<s16>((static_cast<s32>(static_cast<u32>(x - (r.w * 2) / 3) & 0xfffffffeu) * 3) >> 1);
  }
  r.y = static_cast<s16>((align & 2) != 0 ? y - r.h : y);
  const size_t need = 0x14 + static_cast<size_t>(r.w) * static_cast<size_t>(r.h) * 2;
  if (tim->size() < need) tim->resize(need, 0);
  psyq::LoadImage(&r, reinterpret_cast<const u16*>(tim->data() + 0x14));
  psyq::DrawSync(0);
}

bool g_declined[3] = {};

}  // namespace

bool disc_screen_declined(int disc) { return disc >= 1 && disc <= 2 && g_declined[disc]; }

void disc_change_screen(int disc) {
  System* sys = get_system();
  auto& rt = runtime::Runtime::get();
  pad_set_option(&sys->pad, 0);
  const std::vector<u8> background = read_entry(0x12 + rng_range(3));
  psyq::DISPENV d{};
  d.disp = {0, 0, 0x280, 0x1e0};
  d.screen = {0, 0, 0x100, 0xf0};
  d.isinter = 1;
  d.isrgb24 = 1;
  const psyq::RECT all{0, 0, 0x3c0, 0x1e0};
  psyq::ClearImage(&all, 0, 0, 0);
  psyq::PutDispEnv(&d);
  draw_tim(background, 0, 0xaa, 0x38, 0, false);
  const std::vector<u8> prompt = read_entry(0x11);
  draw_tim(prompt, 0, 0xaa, 0x186, 0, false);
  draw_tim(prompt, disc, 0xa2, 0x186, 3, false);
  draw_tim(prompt, disc + 2, 0x262, 0x37, 3, false);
  psyq::VSync(0);
  psyq::SetDispMask(1);
  rt.log_once("disc_screen_" + std::to_string(disc),
              "insert disc " + std::to_string(disc) + ": the action button tries the configured images, Select goes on");
  for (;;) {
    // The lid: wait for the action button / Start (Select: carry on with this disc). The
    // host controller is read directly: the game's pad module only polls the ports the
    // title enabled, and a run that skips the title has none.
    u16 pressed = 0, previous = rt.host_buttons();
    while ((pressed & (0x40 | 0x800 | 0x100)) == 0) {
      psyq::VSync(0);
      const u16 now = rt.host_buttons();
      pressed = static_cast<u16>(pressed | (now & ~previous));
      previous = now;
    }
    draw_tim(prompt, 6, 0x262, 0x37, 3, true);  // reading
    if ((pressed & 0x100) != 0 || loader_insert_disc(sys->loader, disc)) {
      if ((pressed & 0x100) != 0) {
        rt.log_once("disc_screen_skip", "insert disc: carrying on with the disc in the drive (Select)");
        if (disc >= 1 && disc <= 2) g_declined[disc] = true;
      }
      psyq::VSync(0);
      psyq::SetDispMask(0);
      return;
    }
    draw_tim(prompt, 5, 0x262, 0x37, 3, true);  // the wrong disc
  }
}

}  // namespace ss::game
