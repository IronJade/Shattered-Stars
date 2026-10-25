#include "game/title/new_game.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "disc/slz.h"
#include "game/ap/ap.h"
#include "game/boot/pad.h"
#include "game/boot/stubs.h"
#include "game/common/math.h"
#include "game/common/program_image.h"
#include "game/common/texture.h"
#include "game/common/text_rules.h"
#include "game/title/load_queue.h"
#include "game/title/member_profiles.h"
#include "game/title/text.h"
#include "game/title/title_program.h"
#include "game/title/window.h"
#include "platform/window.h"
#include "runtime/libgpu.h"
#include "runtime/runtime.h"

namespace ss::game {
namespace {

using platform::kPadCircle, platform::kPadCross, platform::kPadDown, platform::kPadL1, platform::kPadL2,
    platform::kPadLeft, platform::kPadR1, platform::kPadR2, platform::kPadRight, platform::kPadSelect,
    platform::kPadStart, platform::kPadUp;
constexpr u16 kConfirm = kPadStart | kPadCross;  // 0x840

constexpr int kSoundDemo = 0xA72;  // #2674: sound bank + tune for Sound Select
constexpr int kLaterDemo = 0xA73;  // #2675: the tune of the later screens

s32 rd32(const u8* p) {
  s32 v;
  std::memcpy(&v, p, 4);
  return v;
}

std::vector<u8> unpack(std::span<const u8> data, int block) {
  auto out = disc::slz::unpack_block(data, 0, block, nullptr);
  return out ? std::move(*out) : std::vector<u8>{};
}

// The sequence's working set: the original's locals of 0x80042cc8.
struct Sequence {
  explicit Sequence(TitleGlobals& globals) : t(globals) {}
  TitleGlobals& t;
  RenderContext* main = nullptr;  // *param_1
  std::vector<u8> bank;           // local_c8: 0x50000, the demo tune's bank (and its sequence at +4)
  std::vector<u8> sequence;       // local_c0: 0x8000, the unpacked sequence
  char* name = nullptr;           // param_2
  int result = 0;                 // local_b8: 0 forward, 1 back
  int countdown = 2;              // local_a8: frames until the display is switched on

  Sound* sound() const { return t.sound; }
  u8* config() const { return t.config; }
};

void add_prim(RenderContext* ctx, void* prim) { psyq::addPrim(ctx->ot_head, prim); }

void begin_frame(std::initializer_list<RenderContext*> contexts) {
  deferred_delete_update();
  screen_flip();
  for (RenderContext* ctx : contexts) ctx->ot_head = screen_clear_ot(ctx->ot);
  text_frame_begin();
}

void end_frame(Sequence& s, int vsync_mode, std::initializer_list<RenderContext*> draw_order) {
  psyq::DrawSync(0);
  psyq::VSync(vsync_mode);
  screen_put_env();
  for (RenderContext* ctx : draw_order) screen_draw_ot(ctx->ot);
  if (s.countdown != 0 && --s.countdown == 0) psyq::SetDispMask(1);
}

// Full-screen subtractive grey that fades a screen in and out.
void queue_fade(RenderContext* ctx, int level) {
  auto* tile = screen_alloc_prims<psyq::TILE>(ctx->arena, 1);
  psyq::setlen(tile, 3);
  tile->r0 = tile->g0 = tile->b0 = static_cast<u8>(level);
  tile->code = 0x62;
  tile->x0 = tile->y0 = 0;
  tile->w = 0x140;
  tile->h = 0xF0;
  add_prim(ctx, tile);
  auto* mode = screen_alloc_prims<psyq::DR_MODE>(ctx->arena, 1);
  psyq::SetDrawMode(mode, 0, 1, 0x40, nullptr);
  add_prim(ctx, mode);
}

// Full-screen gouraud backdrop; corners top-left, top-right, bottom-left, bottom-right.
void queue_backdrop(RenderContext* ctx, u32 c0, u32 c1, u32 c2, u32 c3) {
  auto* p = screen_alloc_prims<psyq::POLY_G4>(ctx->arena, 1);
  psyq::setlen(p, 8);
  std::memcpy(&p->r0, &c0, 3);
  std::memcpy(&p->r1, &c1, 3);
  std::memcpy(&p->r2, &c2, 3);
  std::memcpy(&p->r3, &c3, 3);
  p->code = 0x38;
  p->x0 = 0, p->y0 = 0;
  p->x1 = 0x140, p->y1 = 0;
  p->x2 = 0, p->y2 = 0xF0;
  p->x3 = 0x140, p->y3 = 0xF0;
  add_prim(ctx, p);
  auto* mode = screen_alloc_prims<psyq::DR_MODE>(ctx->arena, 1);
  psyq::SetDrawMode(mode, 0, 1, 0, nullptr);
  add_prim(ctx, mode);
}

// Fades 'fade' toward 0 (or up while leaving); true once a fade-out is complete.
bool step_fade(int& fade, bool leaving, int step) {
  if (leaving) {
    fade += step;
    return fade > 0xFF;
  }
  fade -= step;
  if (fade < 0) fade = 0;
  return false;
}

// Left / Right over 'count' choices, with the menu's move sound.
void move_choice(Sequence& s, s32& selection, u16 repeat, int count) {
  if (repeat & kPadLeft) {
    if (--selection < 0) {
      selection = 0;
    } else {
      sound_play_sfx(s.sound(), 1, 0, 0x7F40);
    }
  }
  if (repeat & kPadRight) {
    if (++selection < count) {
      sound_play_sfx(s.sound(), 1, 0, 0x7F40);
    } else {
      selection = count - 1;
    }
  }
}

void draw_choice_icons(Sequence& s, int x, int step, int count, int tpage, int highlighted_v_of_selected,
                       int selection) {
  for (int i = 0; i < count; ++i, x += step) {
    const bool lit = (i == selection) == (highlighted_v_of_selected != 0);
    new_game_draw_sprite(s.main, x, 0x30, 0x40, 0x40, i << 6, static_cast<int>(lit) << 6, tpage, 0x7800, 0x80, false);
  }
}

// LAB_80043128: shown when the memory card cannot take a save.
void memcard_notice_screen(Sequence& s, int notice) {
  int countdown = 2, fade = 0xF0, blink = 0;
  bool leaving = false;
  s.result = 0;
  pad_clear(s.t.pad, 0);
  auto* w = new MemcardNoticeWindow;
  w->selection = notice - 1;
  w->set_rect(0x0C, 0x50, 0x124, 0x3A);
  for (;;) {
    begin_frame({s.main, w->ctx});
    notice = memcard_new_game_notice(s.t.memcard);
    if (notice > 0) w->selection = notice - 1;
    if (fade != 0) queue_fade(s.main, fade);
    if (step_fade(fade, leaving, 0x10)) break;
    const u16 pressed = pad_pressed(s.t.pad, 0);
    pad_pressed_repeat(s.t.pad, 0);
    pad_clear(s.t.pad, 0);
    if (!leaving) {
      if ((pressed & kConfirm) || notice == 0) {
        leaving = true;
        s.result = 0;
        sound_play_sfx(s.sound(), 2, 0, 0x7F40);
      }
      if (pressed & kPadCircle) {
        leaving = true;
        s.result = 1;
        sound_play_sfx(s.sound(), 6, 0, 0x7F40);
      }
      blink = (blink + 1) % 0x38;
      if (!leaving) new_game_draw_sprite(s.main, 0x118, 0x81, 0x10, 0x10, (blink / 4) << 4, 0xE0, 6, 0x780A, 0x80, false);
    }
    w->draw();
    memcard_update(s.t.memcard);
    text_frame_upload();
    load_queue_update();
    psyq::DrawSync(0);
    psyq::VSync(0);
    screen_put_env();
    screen_draw_ot(w->ctx->ot);
    screen_draw_ot(s.main->ot);
    if (countdown != 0 && --countdown == 0) psyq::SetDispMask(1);
  }
  delete w;
}

// LAB_800434f4: Surround / Stereo / Monaural, playing a demo tune panned left and right.
void sound_select_screen(Sequence& s) {
  auto* w = new SoundWindow;
  async_load(kSoundDemo, s.bank.data(), 0);
  s.countdown = 2;
  int demo = 1, pan = 0x40, fade = 0xF0, side = 0, step = 2;
  bool leaving = false;
  sound_set_output_mode(s.sound(), w->selection);
  w->set_rect(0x24, 0x76, 0xE8, 0x72);
  for (;;) {
    begin_frame({s.main, w->ctx});
    if (fade != 0) queue_fade(s.main, fade);
    if (step_fade(fade, leaving, 0x10)) break;
    const u16 pressed = pad_pressed(s.t.pad, 0);
    const u16 repeat = pad_pressed_repeat(s.t.pad, 0);
    pad_clear(s.t.pad, 0);
    if (!leaving) {
      if (pressed & kConfirm) {
        leaving = true;
        s.result = 0;
        sound_play_sfx(s.sound(), 2, 0, 0x7F40);
        sound_fade_out(s.sound(), 0x1E);
      }
      if (pressed & kPadCircle) {
        leaving = true;
        s.result = 1;
        sound_play_sfx(s.sound(), 6, 0, 0x7F40);
        sound_fade_out(s.sound(), 0x1E);
      }
      if (repeat & kPadLeft) {
        if (--w->selection < 0) {
          w->selection = 0;
        } else {
          sound_play_sfx(s.sound(), 1, 0, 0x7F40);
        }
        sound_set_output_mode(s.sound(), w->selection);
      }
      if (repeat & kPadRight) {
        if (++w->selection < 3) {
          sound_play_sfx(s.sound(), 1, 0, 0x7F40);
        } else {
          w->selection = 2;
        }
        sound_set_output_mode(s.sound(), w->selection);
      }
    }
    new_game_draw_sprite(s.main, 8, 0x0C, 0x90, 0x20, 0, 0x80, 5, 0x7801, 0x80, false);       // "Sound Select"
    new_game_draw_sprite(s.main, text::layout(text::lang()).sound_subtitle_x, 0x1C, 0xD8, 0x10, 0, 0xC0, 0x25, 0x7806, 0x70,
                         true);  // "Select your audio system." (NA x 0x8c)
    draw_choice_icons(s, 0x28, 0x50, 3, 5, 1, w->selection);
    // The demo: wait for the load, send the bank to the SPU, play, then sweep the pan.
    if (demo == 2) {
      demo = 3;
      sound_stop(s.sound());
      const auto seq = unpack(std::span<const u8>(s.bank).subspan(4), 0);
      std::copy_n(seq.begin(), std::min(seq.size(), s.sequence.size()), s.sequence.begin());
    } else if (demo < 3) {
      if (demo == 1 && async_load_done(kSoundDemo)) {
        demo = 2;
        sound_load_bank(s.sound(), s.bank.data() + rd32(s.bank.data()), 0, 0);
      }
    } else if (demo == 3) {
      if (sound_bank_busy(s.sound()) != 1) demo = 4;
    } else if (demo == 4) {
      demo = 0xFF;
      sound_play_sequence(s.sound(), s.sequence.data());
    }
    if (demo == 0xFF) {
      const int now = pan & 0xFF;
      pan += step;
      sound_set_pan(s.sound(), now, 0xFF);
      sound_nop_8001aabc(s.sound(), side, 0xFF);
      if (pan > 0x7F) {
        step = -step;
        pan = 0x7F;
        side = 1;
      }
      if (pan < 0) {
        step = -step;
        pan = 0;
        side = 0;
      }
    }
    w->draw();
    queue_backdrop(w->ctx, 0x303000, 0x606000, 0x303000, 0);
    text_frame_upload();
    load_queue_update();
    end_frame(s, 0, {w->ctx, s.main});
  }
  s.config()[0x44] = static_cast<u8>(w->selection);
  delete w;
  async_load_cancel(kSoundDemo, s.bank.data());
}

// LAB_80043dc0: Vibration ON / OFF; choosing ON gives a short rumble.
void vibration_screen(Sequence& s) {
  auto* w = new VibrationWindow;
  int rumble = 0, fade = 0xF0;
  bool leaving = false;
  s.result = 0;
  w->set_rect(0x24, 0x76, 0xE8, 0x72);
  pad_clear(s.t.pad, 0);
  for (;;) {
    begin_frame({s.main, w->ctx});
    if (fade != 0) queue_fade(s.main, fade);
    if (step_fade(fade, leaving, 0x10)) break;
    const u16 pressed = pad_pressed(s.t.pad, 0);
    const u16 repeat = pad_pressed_repeat(s.t.pad, 0);
    pad_clear(s.t.pad, 0);
    if (!leaving) {
      if (pressed & kConfirm) {
        leaving = true;
        s.result = 0;
        sound_play_sfx(s.sound(), 2, 0, 0x7F40);
      }
      if (pressed & kPadCircle) {
        leaving = true;
        s.result = 1;
        sound_fade_out(s.sound(), 0x1E);
        sound_play_sfx(s.sound(), 6, 0, 0x7F40);
      }
      if (repeat & kPadLeft) {
        if (--w->selection < 0) {
          w->selection = 0;
        } else {
          rumble = 8;
          sound_play_sfx(s.sound(), 1, 0, 0x7F40);
          pad_enable_vibration(s.t.pad, 0, true);
          pad_vibrate(s.t.pad, 0, 1, 0x90);
        }
      }
      if (repeat & kPadRight) {
        if (++w->selection < 2) {
          sound_play_sfx(s.sound(), 1, 0, 0x7F40);
        } else {
          w->selection = 1;
        }
      }
    }
    if (rumble != 0 && --rumble < 1) pad_vibrate(s.t.pad, 0, 0, 0);
    new_game_draw_sprite(s.main, 8, 0x0C, 0x98, 0x20, 0, 0x80, 6, 0x7801, 0x80, false);
    new_game_draw_sprite(s.main, text::layout(text::lang()).screen2_subtitle_x, 0x1C, 0xD8, 0x10, 0, 0xC0, 0x26, 0x7806,
                         0x70, true);  // NA x 0x98
    draw_choice_icons(s, 0x40, 0x68, 2, 6, 1, w->selection);
    w->draw();
    queue_backdrop(w->ctx, 0x303000, 0, 0x606000, 0x303000);
    new_game_sound_update(s.bank.data(), s.sequence.data());
    text_frame_upload();
    load_queue_update();
    // The original draws the Sound Select window's (freed) table here; its slot is
    // the one this window was given.
    end_frame(s, 0, {w->ctx, s.main});
  }
  pad_vibrate(s.t.pad, 0, 0, 0);
  s.config()[0x46] = static_cast<u8>(1 - w->selection);
  pad_enable_vibration(s.t.pad, 0, s.config()[0x46] != 0);
  delete w;
}

// Angle (4096 = full turn) of step i of 16 over 'degrees'.
int ring_angle(int i, int degrees) { return ((i * degrees) / 16) * 0x1000 / 360; }

void queue_g3(RenderContext* ctx, u32 c0, int x0, int y0, u32 c1, int x1, int y1, u32 c2, int x2, int y2) {
  auto* p = screen_alloc_prims<psyq::POLY_G3>(ctx->arena, 1);
  psyq::setlen(p, 6);
  std::memcpy(&p->r0, &c0, 3);
  std::memcpy(&p->r1, &c1, 3);
  std::memcpy(&p->r2, &c2, 3);
  p->code = 0x32;
  p->x0 = static_cast<s16>(x0), p->y0 = static_cast<s16>(y0);
  p->x1 = static_cast<s16>(x1), p->y1 = static_cast<s16>(y1);
  p->x2 = static_cast<s16>(x2), p->y2 = static_cast<s16>(y2);
  add_prim(ctx, p);
}

u32 grey(int level) { return static_cast<u32>(level | level << 8 | level << 16); }

// LAB_80044568: Claude or Rena, with spotlights over their sprites and the portraits.
void character_screen(Sequence& s) {
  auto* w = new CharacterWindow;
  int fade = 0xF0, anim = 0, anim_timer = 0;
  bool leaving = false;
  s.result = 0;
  w->set_rect(0x24, 0x78, 0xE8, 0x72);
  int bright[2] = {0x100, 0};  // local_110 / local_10c
  const int widths[2] = {0xCE, 0xE4};  // portrait widths (local_108 / local_104)
  pad_clear(s.t.pad, 0);
  for (;;) {
    deferred_delete_update();
    screen_flip();
    s.main->ot_head = screen_clear_ot(s.main->ot);
    ++anim_timer;
    w->ctx->ot_head = screen_clear_ot(w->ctx->ot);
    text_frame_begin();
    if (anim_timer > 4) {
      anim = (anim + 1) % 4;
      anim_timer = 0;
    }
    if (fade != 0) queue_fade(w->ctx, fade + 8);
    if (step_fade(fade, leaving, 0x20)) break;
    const u16 pressed = pad_pressed(s.t.pad, 0);
    const u16 repeat = pad_pressed_repeat(s.t.pad, 0);
    pad_clear(s.t.pad, 0);
    if (!leaving) {
      if (pressed & kConfirm) {
        leaving = true;
        s.result = 0;
        sound_play_sfx(s.sound(), 2, 0, 0x7F40);
      }
      if (pressed & kPadCircle) {
        leaving = true;
        s.result = 1;
        sound_play_sfx(s.sound(), 6, 0, 0x7F40);
      }
      if (repeat & kPadLeft) {
        if (--w->selection < 0) {
          w->selection = 0;
        } else {
          anim = anim_timer = 0;
          sound_play_sfx(s.sound(), 1, 0, 0x7F40);
        }
      }
      if (repeat & kPadRight) {
        if (++w->selection < 2) {
          anim = anim_timer = 0;
          sound_play_sfx(s.sound(), 1, 0, 0x7F40);
        } else {
          w->selection = 1;
        }
      }
    }
    new_game_draw_sprite(s.main, 8, 0x0C, 0x80, 0x20, 0, 0xA0, 5, 0x7801, 0x80, false);
    new_game_draw_sprite(s.main, text::layout(text::lang()).screen3_subtitle_x, 0x1C, 0xD8, 0x10, 0, 0xD0, 0x25, 0x7806,
                         0x70, true);  // NA x 0x80
    for (int c = 0; c < 2; ++c) {
      bright[c] = c == w->selection ? std::min(bright[c] + 0x20, 0x100) : std::max(bright[c] - 0x20, 0);
    }
    int left = 0xC0 - ((bright[0] * 0x90) >> 8);
    for (int c = 0; c < 2; ++c, left += 0x30) {
      const int cx = left + 0x0C;
      // The spotlight's beam: a fan from (cx, 0x28) to the front half of the floor ellipse.
      for (int i = 0; i < 16; ++i) {
        int a = i > 8 ? 0x10 - i : i;
        int b = i + 1 > 8 ? 0x10 - (i + 1) : i + 1;
        b += 1;  // sic: the second vertex is shaded one step further out
        const int g1 = ((std::abs(a) * 8 + 8) * bright[c]) >> 8;
        const int g2 = ((std::abs(b) * 8 + 8) * bright[c]) >> 8;
        const int a1 = ring_angle(i, 0xB4), a2 = ring_angle(i + 1, 0xB4);
        queue_g3(s.main, scale_colour(0x303030, bright[c]), cx, 0x28, grey(g1),
                 ((cos4096(static_cast<u32>(a1)) << 5) >> 12) + cx, ((sin4096(static_cast<u32>(a1)) << 3) >> 12) + 0x6C,
                 grey(g2), ((cos4096(static_cast<u32>(a2)) << 5) >> 12) + cx,
                 ((sin4096(static_cast<u32>(a2)) << 3) >> 12) + 0x6C);
      }
      {
        auto* mode = screen_alloc_prims<psyq::DR_MODE>(s.main->arena, 1);
        psyq::SetDrawMode(mode, 0, 1, 0x20, nullptr);
        add_prim(s.main, mode);
      }
      const bool selected = (c ^ w->selection) == 0;
      int frame = selected ? anim : 0;
      if (frame == 2) frame = 0;
      if (frame == 3) frame = 2;
      new_game_draw_sprite(s.main, left, 0x40, 0x18, 0x30, c * 0x18 + 0xC0, frame * 0x30 + 0x10, 5,
                           ((c * 0x10 + (selected ? 0 : 0x20) + 0x20) >> 4) | 0x7800, 0x80, false);
      new_game_draw_sprite(s.main, left, 100, 0x18, 0x10, 0xC0, 0, 5, 0x7802, 0x80, false);  // shadow
      // The lit floor: a full ellipse around (cx, 0x6c).
      const u32 floor = scale_colour(0x203020, bright[c]);
      for (int i = 0; i < 16; ++i) {
        const int a1 = ring_angle(i, 0x168), a2 = ring_angle(i + 1, 0x168);
        queue_g3(s.main, floor, cx, 0x6C, floor, ((cos4096(static_cast<u32>(a1)) << 5) >> 12) + cx,
                 ((sin4096(static_cast<u32>(a1)) << 3) >> 12) + 0x6C, floor,
                 ((cos4096(static_cast<u32>(a2)) << 5) >> 12) + cx, ((sin4096(static_cast<u32>(a2)) << 3) >> 12) + 0x6C);
      }
      auto* mode = screen_alloc_prims<psyq::DR_MODE>(s.main->arena, 1);
      psyq::SetDrawMode(mode, 0, 1, 0x20, nullptr);
      add_prim(s.main, mode);
    }
    w->draw();
    // Portraits (16-bit images at 0x140 / 0x240, 0x100), the chosen one on top.
    for (u32 c = static_cast<u32>(w->selection); c < 2; c = w->selection == 0 ? c + 1 : c - 1) {
      if (bright[c] != 0) {
        const int x = c == 0 ? 0x138 - widths[0] : 8;
        new_game_draw_sprite(s.main, x, 0, widths[c], 0xF0, 0, 0, ((c * 0x100 + 0x140) & 0x3FF) >> 6 | 0x130, 0x7880,
                             (bright[c] * 0x90) >> 8, bright[c] != 0x100);
      }
    }
    queue_backdrop(s.main, 0, 0x303000, 0x303000, 0x606000);
    new_game_sound_update(s.bank.data(), s.sequence.data());
    text_frame_upload();
    load_queue_update();
    end_frame(s, 2, {s.main, w->ctx});
  }
  psyq::VSync(2);
  s.config()[0x45] = static_cast<u8>(w->selection);
  delete w;
  if (s.countdown == 0) psyq::SetDispMask(1);
}

// LAB_800454cc: name entry. Start or OK accepts; Return backs out.
void name_screen(Sequence& s) {
  auto* kb = new NameKeyboardWindow;
  auto* box = new NameBoxWindow(s.config()[0x45]);
  bool leaving = false;
  kb->set_rect(0x50, 0x6E, 0xE8, 100);
  box->set_rect(0xA0, 0x30, 0xAC, 0x18);
  // Ten palette entries of the portrait art that cycle (CLUT 0x1e0, entries 0x87..0x90).
  const psyq::RECT palette_rect{0x87, 0x1E0, 10, 1};
  u16 palette[10];
  psyq::StoreImage(&palette_rect, palette);
  int fade = 0xF0;
  const int character = s.config()[0x45];
  async_load(character != 0 ? 0xA79 : 0xA78, s.t.prologue_data.data(), -1);
  int frames = 0;
  const s32* rows = reinterpret_cast<const s32*>(title_data(0x8004E458));
  const int widths[2] = {0xCE, 0xE4};  // local_e0 / local_dc
  pad_clear(s.t.pad, 0);
  for (;;) {
    begin_frame({s.main, kb->ctx, box->ctx});
    if (fade != 0) queue_fade(kb->ctx, fade + 8);
    if (step_fade(fade, leaving, 0x20)) break;
    const u16 pressed = pad_pressed(s.t.pad, 0);
    const u16 repeat = pad_pressed_repeat(s.t.pad, 0);
    pad_clear(s.t.pad, 0);
    if (!leaving) {
      if (pressed & kPadStart) {
        if (!name_box_commit(box, s.name)) {
          sound_play_sfx(s.sound(), 7, 0, 0x7F40);
        } else {
          leaving = true;
          s.result = 0;
          sound_play_sfx(s.sound(), 2, 0, 0x7F40);
        }
      } else if (pressed & kPadCross) {
        const int r = keyboard_confirm(kb, box);
        if (r == 1) {
          int sfx = 2;
          if (!name_box_commit(box, s.name)) {
            sfx = 7;
          } else {
            leaving = true;
            s.result = 0;
          }
          sound_play_sfx(s.sound(), sfx, 0, 0x7F40);
        }
        if (r == 2) {
          leaving = true;
          s.result = 1;
          sound_play_sfx(s.sound(), 6, 0, 0x7F40);
        }
      } else {
        if (pressed & kPadCircle) name_box_backspace(box);
        // FR / DE: one character set, no switch (text_rules.h NameKeyboard).
        const bool charset_switch = text::name_keyboard(text::lang()).charset_switch;
        if (charset_switch && (repeat & (kPadR2 | kPadR1))) {
          kb->selection = (kb->selection + 1) & 1;
          sound_play_sfx(s.sound(), 1, 0, 0x7F40);
          if (rows[kb->selection] <= kb->row) kb->row = rows[kb->selection];
        }
        if (charset_switch && (repeat & (kPadL2 | kPadL1))) {
          if (--kb->selection < 0) kb->selection = 1;
          sound_play_sfx(s.sound(), 1, 0, 0x7F40);
        }
        if (pressed & kPadSelect) {
          sound_play_sfx(s.sound(), 3, 0, 0x7F40);
          name_box_reset(box);
        }
        if (repeat & kPadLeft) keyboard_move(kb, box, -1, 0);
        if (repeat & kPadRight) keyboard_move(kb, box, 1, 0);
        if (repeat & kPadUp) keyboard_move(kb, box, 0, -1);
        if (repeat & kPadDown) keyboard_move(kb, box, 0, 1);
      }
    }
    if (rows[kb->selection] <= kb->row) kb->row = rows[kb->selection];
    const text::Layout& lay = text::layout(text::lang());
    new_game_draw_sprite(s.main, 8, 0x0C, lay.name_heading_w, 0x20, 0, 0xA0, 6, 0x7801, 0x80, false);  // NA w 0x80
    new_game_draw_sprite(s.main, lay.name_subtitle_x, 0x1C, 0xA8, 0x10, 0, 0xD0, 0x26, 0x7806, 0x70, true);  // NA x 0x80
    kb->draw();
    box->draw();
    new_game_draw_sprite(s.main, 0, 0, widths[character], 0xF0, 0, 0, ((character * 0x100 + 0x140) & 0x3FF) >> 6 | 0x110,
                         0x7880, 0x80, false);
    queue_backdrop(s.main, 0x606000, 0x303000, 0, 0x303000);
    ++frames;
    new_game_sound_update(s.bank.data(), s.sequence.data());
    text_frame_upload();
    load_queue_update();
    if (frames > 3) {
      frames = 0;
      const u16 first = palette[0];
      for (int i = 0; i < 8; ++i) palette[i] = palette[i + 1];
      palette[8] = first;
    }
    psyq::LoadImage(&palette_rect, palette);
    end_frame(s, 0, {s.main, box->ctx, kb->ctx});
  }
  psyq::VSync(2);
  delete kb;
  delete box;
  if (s.countdown == 0) psyq::SetDispMask(1);
}

// Archipelago (port feature) [charselect agent]: a seed whose options.story_route names a hero
// skips the hero select and the naming screen. What they would have left behind: that hero in
// config +0x45, the name box's default name (as Start on an untouched name box keeps it), and
// the load of the hero's first prologue part that the naming screen starts.
void skip_hero_screens(Sequence& s, int route) {
  const int character = route - 1;
  s.config()[0x45] = static_cast<u8>(character);
  {
    NameBoxWindow box(character);
    name_box_commit(&box, s.name);
  }
  async_load(character != 0 ? 0xA79 : 0xA78, s.t.prologue_data.data(), -1);
  std::fprintf(stderr, "[ap] story route %s (the seed's): New Game skips the hero select and the naming screen (%s)\n",
               character != 0 ? "Rena" : "Claude", s.name);
}

// ---- Archipelago (port feature) [charselect agent]: the character select ----------------------
// Open world and hub seeds (options.starting_character "any" or a list: every member, the ones
// the player may not pick dark; a named member: that member only, to confirm). The hero select's
// format: its heading art, a face per member where the spotlights were (the Status page's
// portraits, scaled), the highlighted member's picture behind (the renaming page's, 0xbd3 +
// member: the hero select's own two are the same bytes), and the window of seven lines: the
// heroes' original bios; for the others the name, the race and occupation and the favourite food
// read from the disc in its own language (Status page pack 0xbb3, item pack 3000, drawn with
// those packs' fonts), and four lines of our own (member_profiles.h).
namespace charselect {

// The faces: a grid of four by three on the side away from the picture, where the hero select
// keeps its spotlit sprites, so that the picture's face stays clear; on a shade panel (the bio
// window's own shade) that keeps them readable over the picture's edge.
constexpr int kFaceSize = 22;
constexpr int kGridColumns = 4, kGridX = 0x0E, kGridY = 0x2E, kGridStepX = 0x1C, kGridStepY = 0x19;
constexpr int kFaceX = 0x240;                // VRAM (0x240, 0): free while New Game runs
constexpr int kSlotX[2] = {0x140, 0x240};    // the hero select's portrait slots (y 0x100)
constexpr int kFaceTpage = (kFaceX >> 6) | 0x100;  // 15-bit
constexpr int kLineWidth = 0xE8 - 0x10;      // the window less its margins

struct Segment {
  std::vector<u8> codes;  // glyph codes, 0-terminated
  int font = 0;           // 0 the title's, 1 the Status pack's, 2 the item pack's
};
struct Line {
  std::vector<Segment> segments;
  u32 palette = 0;
};

struct Data {
  std::vector<u8> status_pack, item_pack;   // block 0 of 0xbb3 / 3000
  std::vector<u8> fonts[3];                 // [0] unused (the title's stays in title_text())
};

std::vector<u8> load_entry_block(Loader* loader, int entry) {
  std::vector<u8> raw(static_cast<size_t>(std::max(loader->entry_size(entry), 0)));
  if (raw.empty()) return {};
  loader->load_sync(raw.data(), entry, 0);
  return unpack(raw, 0);
}

// Message 'index' of 'group' in a page pack (block 0 offsets: strings, font), up to its end or a
// control code, 0-terminated.
std::vector<u8> pack_message(const std::vector<u8>& pack, int group, int index) {
  std::vector<u8> out;
  if (pack.size() < 8) return {0};
  const u8* strings = pack.data() + rd32(pack.data());
  const u8* p = strings + rd32(strings + group * 4);
  const u8* end = pack.data() + pack.size();
  for (int n = index; n != 0 && p < end;)
    if (*p++ == 0) --n;
  while (p < end && *p != 0) {
    const u32 c = *p > 0x7F && p + 1 < end ? (p[0] & 0x7Fu) | static_cast<u32>(p[1]) << 7 : *p;
    if (c >= 0x4000) break;
    const int n = *p > 0x7F ? 2 : 1;
    out.insert(out.end(), p, p + n);
    p += n;
  }
  out.push_back(0);
  return out;
}

Segment ascii_segment(Window* w, const char* text) {
  Segment seg;
  seg.codes.assign(std::strlen(text) * 2 + 2, 0);
  text_from_ascii(w, text, seg.codes.data());
  return seg;
}

// Runs 'f' with the segment's font in title_text() (the renderer reads it there).
template <typename F>
auto with_font(Data& d, int font, F f) {
  TitleText& text = title_text();
  if (font != 0) std::swap(text.font, d.fonts[font]);
  auto r = f();
  if (font != 0) std::swap(text.font, d.fonts[font]);
  return r;
}

int line_width(Window* w, Data& d, const Line& line) {
  int width = 0;
  for (const Segment& seg : line.segments)
    width += with_font(d, seg.font, [&] { return text_width(w, seg.codes.data()); });
  return width;
}

// The panel behind the faces (the faces are queued into its context before its shade).
class FacePanel : public Window {
 public:
  FacePanel() : Window(0x30) {}
  void draw() override { window_draw_shade_frame(this); }
};

class MemberWindow : public Window {
 public:
  explicit MemberWindow(Data& data) : Window(0x180), d(data) {}
  void draw() override {
    window_text_begin(this);
    int y_ = 0;
    for (const Line& line : lines) {
      int x_ = 0;
      for (const Segment& seg : line.segments)
        x_ = static_cast<int>(with_font(d, seg.font, [&] { return text_draw(this, x_, y_, seg.codes.data(), line.palette); }) &
                              0xFFFF);
      y_ += 0x0E;
    }
    window_text_end(this);
    window_draw_shade_frame(this);
  }
  Data& d;
  std::vector<Line> lines;
};

void build_lines(MemberWindow* w, int member) {
  Data& d = w->d;
  w->lines.clear();
  if (member <= 2) {  // the heroes: the original bios (title strings 0x20 + selection * 7)
    for (int i = 0; i < 7; ++i) {
      const u8* s = text_find(w, (member - 1) * 7 + i + 0x20);
      Segment seg;
      if (s != nullptr)
        for (const u8* p = s;; ++p) {
          seg.codes.push_back(*p);
          if (*p == 0) break;
        }
      else
        seg.codes.push_back(0);
      w->lines.push_back({{seg}, static_cast<u32>(i == 0) << 2});
    }
    return;
  }
  const member_profiles::Profile& pr = member_profiles::kProfiles[member - 1];
  w->lines.push_back({{ascii_segment(w, pr.name)}, 4});
  Segment race{pack_message(d.status_pack, 0xB, member - 1), 1};
  Segment job{pack_message(d.status_pack, 0xA, member - 1), 1};
  Line facts{{race, ascii_segment(w, ". "), job}, 0};
  if (line_width(w, d, facts) <= kLineWidth) {
    w->lines.push_back(facts);
  } else {  // (a long translation: race and occupation on their own lines)
    w->lines.push_back({{race}, 0});
    w->lines.push_back({{job}, 0});
  }
  const u8* food_id = program_data_at(0xBB2, 0x8007E000, 0x80081612 + static_cast<u32>(member) * 2);
  const int food = food_id != nullptr ? food_id[0] | food_id[1] << 8 : 0;
  w->lines.push_back({{Segment{pack_message(d.status_pack, 7, 2), 1}, ascii_segment(w, ": "),
                       Segment{pack_message(d.item_pack, 3, food), 2}},
                      0});
  for (const char* text : pr.lines) {
    if (text == nullptr || w->lines.size() >= 7) break;
    Line line{{ascii_segment(w, text)}, 0};
    if (line_width(w, d, line) > kLineWidth)
      runtime::Runtime::get().log_once(std::string("charselect-wide-") + text,
                                       std::string("[ap] character select: a line wider than the window: ") + text);
    w->lines.push_back(std::move(line));
  }
}

// The member's picture into portrait slot 'slot'; its width (0 when it does not load).
int upload_portrait(Loader* loader, int slot, int member) {
  const auto block = load_entry_block(loader, 0xBD3 + member);
  const auto t = parse_texture_block(block);
  if (!t) return 0;
  upload_texture(runtime::Runtime::get().gpu, t->pixels.data(), 2, 0, kSlotX[slot], 0x100, t->width_px, t->height);
  psyq::DrawSync(0);
  return t->width_px;
}

// The Status page's 160 x 160 portraits (0x1170 + member; 0x10 bytes of header in every sector),
// scaled to kFaceSize, two rows of six at (kFaceX, 0).
void upload_faces(Loader* loader) {
  std::vector<u16> faces(static_cast<size_t>(6 * kFaceSize * 2 * kFaceSize), 0);
  for (int m = 1; m <= 12; ++m) {
    std::vector<u8> raw(static_cast<size_t>(std::max(loader->entry_size(0x1170 + m), 0)));
    if (raw.size() < 0x800) continue;
    loader->load_sync(raw.data(), 0x1170 + m, 0);
    std::vector<u8> tim;
    for (size_t s = 0; s + 0x800 <= raw.size(); s += 0x800) tim.insert(tim.end(), raw.begin() + s + 0x10, raw.begin() + s + 0x800);
    if (tim.size() < 20 || rd32(tim.data() + 4) != 2) continue;
    const int w = tim[16] | tim[17] << 8, h = tim[18] | tim[19] << 8;
    if (w <= 0 || h <= 0 || tim.size() < 20 + static_cast<size_t>(w * h * 2)) continue;
    const int fx = ((m - 1) % 6) * kFaceSize, fy = ((m - 1) / 6) * kFaceSize;
    for (int y = 0; y < kFaceSize; ++y)
      for (int x = 0; x < kFaceSize; ++x) {
        // A box filter over the source cells this texel covers (the colour channels averaged).
        const int x0 = x * w / kFaceSize, x1 = (x + 1) * w / kFaceSize, y0 = y * h / kFaceSize, y1 = (y + 1) * h / kFaceSize;
        int r = 0, g = 0, b = 0, n = 0;
        for (int sy = y0; sy < y1; ++sy)
          for (int sx = x0; sx < x1; ++sx) {
            const u8* p = tim.data() + 20 + (sy * w + sx) * 2;
            const int v = p[0] | p[1] << 8;
            r += v & 0x1F, g += (v >> 5) & 0x1F, b += (v >> 10) & 0x1F, ++n;
          }
        if (n == 0) continue;
        u16 v = static_cast<u16>((r / n) | (g / n) << 5 | (b / n) << 10);
        if (v == 0) v = 0x8000;  // (black stays opaque)
        faces[static_cast<size_t>((fy + y) * 6 * kFaceSize + fx + x)] = v;
      }
  }
  upload_texture(runtime::Runtime::get().gpu, faces.data(), 2, 0, kFaceX, 0, 6 * kFaceSize, 2 * kFaceSize);
  psyq::DrawSync(0);
}

void queue_tile(RenderContext* ctx, int x, int y, int w, int h, u32 colour) {
  auto* t = screen_alloc_prims<psyq::TILE>(ctx->arena, 1);
  psyq::setlen(t, 3);
  std::memcpy(&t->r0, &colour, 3);
  t->code = 0x60;
  t->x0 = static_cast<s16>(x), t->y0 = static_cast<s16>(y);
  t->w = static_cast<s16>(w), t->h = static_cast<s16>(h);
  add_prim(ctx, t);
}

}  // namespace charselect

// The character select. 'hero' is the story route's hero (1 / 2); 'fixed' a named starting
// character (0: the player picks among 'choices', the hero always allowed: nobody extra);
// 'last' the member picked before (back from the combat mode: the cursor starts there; 0 none).
// Returns the member picked (the hero: nobody extra); s.result 1 when backed out.
int member_select_screen(Sequence& s, int hero, int fixed, const std::vector<int>& choices, int last) {
  using namespace charselect;
  Loader* loader = s.t.loader;
  Data data;
  data.status_pack = load_entry_block(loader, 0xBB3);
  data.item_pack = load_entry_block(loader, 3000);
  for (int f = 1; f <= 2; ++f) {
    const std::vector<u8>& pack = f == 1 ? data.status_pack : data.item_pack;
    if (pack.size() >= 8) data.fonts[f].assign(pack.begin() + rd32(pack.data() + 4), pack.end());
  }
  upload_faces(loader);
  auto selectable = [&](int m) {
    return fixed != 0 ? m == fixed : (m == hero || std::find(choices.begin(), choices.end(), m) != choices.end());
  };
  int cursor = fixed != 0 ? fixed : last >= 1 && last <= 12 ? last : hero;
  {
    int count = 0;
    for (int m = 1; m <= 12; ++m) count += selectable(m) ? 1 : 0;
    std::fprintf(stderr, "[ap] character select opens: %s\n",
                 fixed != 0 ? (std::string(member_profiles::kProfiles[fixed - 1].name) + " to confirm").c_str()
                            : (std::to_string(count) + " of 12 to pick").c_str());
  }
  auto* w = new MemberWindow(data);
  w->set_rect(8, 0x78, 0xE8, 0x72);
  build_lines(w, cursor);
  // The panel: the grid with the shade's soft margins (8 left and top, 0x10 right and bottom),
  // its top under the heading and its bottom at the bio window's top.
  auto* panel = new FacePanel;
  {
    const int columns = fixed != 0 ? 1 : kGridColumns, rows = fixed != 0 ? 1 : (12 + kGridColumns - 1) / kGridColumns;
    const int left = kGridX - 0x0A, top = kGridY - 8;
    const int right = kGridX + (columns - 1) * kGridStepX + kFaceSize + 0x12;
    const int bottom = std::min(kGridY + (rows - 1) * kGridStepY + kFaceSize + 0x12, 0x78);
    panel->set_rect(left, top, right - left, bottom - top);
  }
  int slot_member[2] = {cursor, 0}, slot_width[2] = {upload_portrait(loader, 0, cursor), 0};
  int bright[2] = {0x100, 0}, front = 0;
  int fade = 0xF0;
  bool leaving = false;
  s.result = 0;
  pad_clear(s.t.pad, 0);
  for (;;) {
    deferred_delete_update();
    screen_flip();
    s.main->ot_head = screen_clear_ot(s.main->ot);
    w->ctx->ot_head = screen_clear_ot(w->ctx->ot);
    panel->ctx->ot_head = screen_clear_ot(panel->ctx->ot);
    text_frame_begin();
    if (fade != 0) queue_fade(w->ctx, fade + 8);
    if (step_fade(fade, leaving, 0x20)) break;
    const u16 pressed = pad_pressed(s.t.pad, 0);
    const u16 repeat = pad_pressed_repeat(s.t.pad, 0);
    pad_clear(s.t.pad, 0);
    if (!leaving) {
      if (pressed & kConfirm) {
        if (selectable(cursor)) {
          leaving = true;
          s.result = 0;
          sound_play_sfx(s.sound(), 2, 0, 0x7F40);
        } else {
          sound_play_sfx(s.sound(), 7, 0, 0x7F40);
          runtime::Runtime::get().log_once("charselect-refused",
                                           "[ap] character select: this seed's party has no place for that member");
        }
      } else if (pressed & kPadCircle) {
        leaving = true;
        s.result = 1;
        sound_play_sfx(s.sound(), 6, 0, 0x7F40);
      } else if (fixed == 0 && (repeat & (kPadLeft | kPadRight | kPadUp | kPadDown))) {
        int next = cursor - 1;  // 0-11: three rows of four (no wrap)
        if ((repeat & kPadLeft) && next % kGridColumns > 0) --next;
        if ((repeat & kPadRight) && next % kGridColumns < kGridColumns - 1) ++next;
        if ((repeat & kPadUp) && next >= kGridColumns) next -= kGridColumns;
        if ((repeat & kPadDown) && next + kGridColumns < 12) next += kGridColumns;
        if (next + 1 != cursor) {
          cursor = next + 1;
          sound_play_sfx(s.sound(), 1, 0, 0x7F40);
          build_lines(w, cursor);
          // The new picture goes into the slot behind, and comes to the front fading in.
          front ^= 1;
          if (slot_member[front] != cursor) {
            slot_member[front] = cursor;
            slot_width[front] = upload_portrait(loader, front, cursor);
          }
        }
      }
    }
    new_game_draw_sprite(s.main, 8, 0x0C, 0x80, 0x20, 0, 0xA0, 5, 0x7801, 0x80, false);  // "Hero select"
    new_game_draw_sprite(s.main, text::layout(text::lang()).screen3_subtitle_x, 0x1C, 0xD8, 0x10, 0, 0xD0, 0x25, 0x7806,
                         0x70, true);  // "Select your character."
    for (int c = 0; c < 2; ++c)
      bright[c] = c == front ? std::min(bright[c] + 0x20, 0x100) : std::max(bright[c] - 0x20, 0);
    // The pictures, right-aligned, the front one on top.
    for (int k = 0; k < 2; ++k) {
      const int c = k == 0 ? front : front ^ 1;
      if (bright[c] == 0 || slot_width[c] == 0) continue;
      new_game_draw_sprite(s.main, 0x138 - slot_width[c], 0, slot_width[c], 0xF0, 0, 0, ((kSlotX[c] & 0x3FF) >> 6) | 0x130, 0x7880,
                           (bright[c] * 0x90) >> 8, bright[c] != 0x100);
    }
    // The faces (a named member: theirs alone, in the first place): the highlighted one lit in a
    // frame, the others dimmer, those not to be picked dark; queued before the panel's shade, so
    // drawn over it (an ordering table entry draws its last primitive first, as on the console).
    for (int m = 1; m <= 12; ++m) {
      if (fixed != 0 && m != fixed) continue;
      const int i = m - 1, place = fixed != 0 ? 0 : i;
      const int x = kGridX + (place % kGridColumns) * kGridStepX, y = kGridY + (place / kGridColumns) * kGridStepY;
      const int level = m == cursor ? 0x80 : selectable(m) ? 0x58 : 0x20;
      new_game_draw_sprite(panel->ctx, x, y, kFaceSize, kFaceSize, (i % 6) * kFaceSize, (i / 6) * kFaceSize, kFaceTpage, 0, level,
                           false);
      if (m == cursor) queue_tile(panel->ctx, x - 2, y - 2, kFaceSize + 4, kFaceSize + 4, selectable(m) ? 0x60E0F0 : 0x5050A0);
    }
    panel->draw();
    w->draw();
    queue_backdrop(s.main, 0, 0x303000, 0x303000, 0x606000);
    new_game_sound_update(s.bank.data(), s.sequence.data());
    text_frame_upload();
    load_queue_update();
    end_frame(s, 2, {s.main, panel->ctx, w->ctx});
  }
  psyq::VSync(2);
  delete w;
  delete panel;
  // The hero select and the naming screen draw Claude / Rena from these slots.
  upload_portrait(loader, 0, 1);
  upload_portrait(loader, 1, 2);
  if (s.countdown == 0) psyq::SetDispMask(1);
  std::fprintf(stderr, "[ap] character select: %s%s\n", member_profiles::kProfiles[cursor - 1].name,
               s.result != 0 ? " (backed out)" : cursor == hero ? " (the hero: nobody extra)" : "");
  return cursor;
}

// LAB_80045ebc: Standard / Semi-active / Full Active.
void combat_mode_screen(Sequence& s) {
  auto* w = new CombatModeWindow;
  int fade = 0xF0;
  bool leaving = false;
  s.result = 0;
  w->set_rect(0x24, 0x76, 0xE8, 0x72);
  pad_clear(s.t.pad, 0);
  for (;;) {
    begin_frame({s.main, w->ctx});
    if (fade != 0) queue_fade(s.main, fade);
    if (step_fade(fade, leaving, 0x10)) break;
    const u16 pressed = pad_pressed(s.t.pad, 0);
    const u16 repeat = pad_pressed_repeat(s.t.pad, 0);
    pad_clear(s.t.pad, 0);
    if (!leaving) {
      if (pressed & kConfirm) {
        if (!s.t.battle_rank_unlocked) sound_fade_out(s.sound(), 0x1E);
        leaving = true;
        s.result = 0;
        sound_play_sfx(s.sound(), 2, 0, 0x7F40);
      }
      if (pressed & kPadCircle) {
        leaving = true;
        s.result = 1;
        sound_play_sfx(s.sound(), 6, 0, 0x7F40);
      }
      move_choice(s, w->selection, repeat, 3);
    }
    const text::Layout& lay = text::layout(text::lang());
    new_game_draw_sprite(s.main, 8, 0x0C, lay.tpage7_heading_w, 0x20, 0, 0x80, 7, 0x7801, 0x80, false);  // NA w 0x86
    new_game_draw_sprite(s.main, lay.tpage7_subtitle_x, 0x10, 0xDC, 0x20, 0, 0xB8, 0x27, 0x7806, 0x70, true);  // NA x 0x88
    draw_choice_icons(s, 0x28, 0x50, 3, 7, 1, w->selection);
    // The original reaches this window (and its table below) through the Vibration
    // window's stale pointer; the allocator hands this window the same memory.
    w->draw();
    queue_backdrop(w->ctx, 0x606000, 0x303000, 0, 0x303000);
    new_game_sound_update(s.bank.data(), s.sequence.data());
    text_frame_upload();
    load_queue_update();
    end_frame(s, 0, {w->ctx, s.main});
  }
  u8* config = s.config();
  switch (w->selection) {
    case 0: config[0x49] = 1, config[0x4A] = 0, config[0x4B] = 0; break;  // targeting manual
    case 1: config[0x49] = 0, config[0x4A] = 1, config[0x4B] = 0; break;  // semi-auto
    case 2: config[0x49] = 2, config[0x4A] = 1, config[0x4B] = 1; break;  // auto
  }
  psyq::VSync(2);
  delete w;
  if (s.countdown == 0) psyq::SetDispMask(1);
  config[0x4F] = 0;
}

// LAB_800466cc: Earth / Galaxy / Universe (after the Voice Collection unlocks it).
void battle_rank_screen(Sequence& s) {
  auto* w = new BattleRankWindow;
  int fade = 0xF0;
  bool leaving = false;
  w->set_rect(0x24, 0x76, 0xE8, 0x72);
  pad_clear(s.t.pad, 0);
  for (;;) {
    begin_frame({s.main, w->ctx});
    if (fade != 0) queue_fade(s.main, fade);
    if (step_fade(fade, leaving, 0x10)) break;
    const u16 pressed = pad_pressed(s.t.pad, 0);
    const u16 repeat = pad_pressed_repeat(s.t.pad, 0);
    pad_clear(s.t.pad, 0);
    if (!leaving) {
      if (pressed & kConfirm) {
        leaving = true;
        s.result = 0;
        sound_fade_out(s.sound(), 0x1E);
        sound_play_sfx(s.sound(), 2, 0, 0x7F40);
      }
      if (pressed & kPadCircle) {
        leaving = true;
        s.result = 1;
        sound_play_sfx(s.sound(), 6, 0, 0x7F40);
      }
      move_choice(s, w->selection, repeat, 3);
    }
    const text::Layout& lay = text::layout(text::lang());
    new_game_draw_sprite(s.main, 8, 0x0C, lay.tpage8_heading_w, 0x20, 0, 0x80, 8, 0x7801, 0x80, false);  // NA w 0x96
    new_game_draw_sprite(s.main, lay.tpage8_subtitle_x, 0x1C, 0xDC, 0x20, 0, 0xB8, 0x28, 0x7806, 0x70, true);  // NA x 0x90
    // NA's art is lit when not selected; the FR / DE art when selected.
    draw_choice_icons(s, 0x28, 0x50, 3, 8, lay.tpage8_lit_selected ? 1 : 0, w->selection);
    w->draw();
    queue_backdrop(w->ctx, 0x606000, 0x303000, 0, 0x303000);
    new_game_sound_update(s.bank.data(), s.sequence.data());
    text_frame_upload();
    load_queue_update();
    end_frame(s, 0, {w->ctx, s.main});
  }
  s.config()[0x4F] = static_cast<u8>(w->selection);
  psyq::VSync(2);
  delete w;
  if (s.countdown == 0) psyq::SetDispMask(1);
}

}  // namespace

void new_game_draw_sprite(RenderContext* ctx, int x, int y, int w, int h, int u, int v, int tpage, int clut,
                          int brightness, bool semi) {
  auto* p = screen_alloc_prims<psyq::SPRT>(ctx->arena, 1);
  p->x0 = static_cast<s16>(x);
  p->y0 = static_cast<s16>(y);
  p->w = static_cast<s16>(w);
  p->h = static_cast<s16>(h);
  p->u0 = static_cast<u8>(u);
  p->v0 = static_cast<u8>(v);
  p->r0 = p->g0 = p->b0 = static_cast<u8>(brightness);
  p->code = semi ? 0x66 : 0x64;
  psyq::setlen(p, 4);
  p->clut = static_cast<u16>(clut);
  add_prim(ctx, p);
  auto* mode = screen_alloc_prims<psyq::DR_MODE>(ctx->arena, 1);
  psyq::SetDrawMode(mode, 0, 0, tpage, nullptr);
  add_prim(ctx, mode);
}

void new_game_sound_update(u8* bank, u8* sequence) {
  TitleGlobals& t = title_globals();
  int& state = t.sound_demo_state;
  if (state == 2) {
    const auto seq = unpack(std::span<const u8>(bank + 4, 0x50000 - 4), 0);
    std::copy_n(seq.begin(), std::min<size_t>(seq.size(), 0x8000), sequence);
    ++state;
  } else if (state < 3) {
    if (state == 1 && async_load_done(kLaterDemo)) {
      sound_stop(t.sound);
      sound_load_bank(t.sound, bank + rd32(bank), 0, 0);
      ++state;
    }
  } else if (state == 3) {
    if (sound_bank_busy(t.sound) != 1) ++state;
  } else if (state == 4) {
    sound_play_sequence(t.sound, sequence);
    state = 0xFF;
  }
}

u32 scale_colour(u32 colour, int level) {
  // GPF with IR0 = level << 4 and IR1..3 = channel << 4: MAC = IR0 * IR >> 12, then
  // the colour FIFO takes MAC >> 4. IR0 keeps the low 16 bits (sign-extended): levels past
  // +-0x7ff wrap (world agent: kind 0x30's bombs pass 0x100 - a colour).
  u32 out = 0;
  const s32 ir0 = static_cast<s16>(static_cast<u32>(level) << 4);
  for (int shift = 0; shift < 24; shift += 8) {
    const s64 mac = (static_cast<s64>(ir0) * static_cast<s64>(((colour >> shift) & 0xFF) << 4)) >> 12;
    out |= static_cast<u32>(std::clamp<s64>(mac >> 4, 0, 0xFF)) << shift;
  }
  return out;
}

int new_game_sequence(RenderContext** holder, char* name) {
  TitleGlobals& t = title_globals();
  psx::Gpu& gpu = runtime::Runtime::get().gpu;
  pad_set_port_mask(t.pad, 1);
  pad_set_repeat_delay(t.pad, 0x0C);
  screen_setup_buffer0(0, 0, 0x140, 0xF0, 0, 0, 0, 0);
  screen_setup_buffer1(0, 0xF0, 0x140, 0xF0, 0, 0, 0, 0);
  // 0x8004ae2c sets up the GTE (ZSF3/4, H, DQA/B, offsets) for scale_colour's GPF.
  for (int i = 0; i < 2; ++i) {
    screen_drawenv(i)->isbg = 1;
    screen_dispenv(i)->isinter = 0;
    screen_drawenv(i)->r0 = screen_drawenv(i)->g0 = screen_drawenv(i)->b0 = 0;
  }
  Sequence s(t);
  s.name = name;
  s.main = new RenderContext;
  *holder = s.main;
  render_ctx_create_arena(s.main, 0x1680);
  render_ctx_create_ot(s.main, 1);
  s.bank.assign(0x50000, 0);
  while (t.loader->poll() != 0) {
  }
  load_queue_reset();

  // Entries #14 (strings, font, text CLUT, menu art), #15 (window skin) and #16
  // (portraits) were read back to back into new_game_data by the title screen.
  const std::span<const u8> e14(t.new_game_data);
  const auto size14 = static_cast<size_t>(t.loader->entry_size(0x0E));
  const auto size15 = static_cast<size_t>(t.loader->entry_size(0x0F));
  const std::span<const u8> e15 = e14.subspan(size14);
  const std::span<const u8> e16 = e15.subspan(size15);
  {
    const auto skin = unpack(e15, 0);
    const u8* inner = skin.data() + rd32(skin.data() + 0x0C);
    upload_clut256(gpu, reinterpret_cast<const u16*>(inner + rd32(inner) + 4), 0, 0x1FE);
    const auto* image = reinterpret_cast<const u16*>(inner + rd32(inner + 4));
    upload_texture(gpu, image + 2, 0, 0, 0x3C0, 0, image[0], image[1]);
    psyq::DrawSync(0);
  }
  TitleText& text = title_text();
  text.strings = unpack(e14, 0);
  text.font = unpack(e14, 1);
  {
    const auto clut = unpack(e14, 2);  // a TIM; its palette is at +0x14
    upload_clut256(gpu, reinterpret_cast<const u16*>(clut.data() + 0x14), 0, 0x1F9);
    vram_upload_packed_block(gpu, e14, 3, 0x140, 0, 0x1E0);
    vram_upload_packed_block(gpu, e16, 0, 0x140, 0x100, 0x1E4);
    vram_upload_packed_block(gpu, e16, 1, 0x240, 0x100, 0x1E5);
    psyq::DrawSync(0);
  }
  t.new_game_data.clear();
  t.new_game_data.shrink_to_fit();
  t.prologue_data.assign(0x52800, 0);
  text.pixels.assign(0x8000, 0);
  s.sequence.assign(0x8000, 0);

  int notice = memcard_new_game_notice(t.memcard);
  while (notice == -1) {
    memcard_update(t.memcard);
    notice = memcard_new_game_notice(t.memcard);
  }
  if (notice != 0) memcard_notice_screen(s, notice);
  pad_clear(t.pad, 0);

  // The screens, with Circle stepping back one screen (from Sound Select, to the title).
  // Archipelago: with a seed's story_route (1 Claude, 2 Rena; 0 for "any", no seed or
  // Archipelago off) the hero select and the naming screen are skipped, both ways.
  const int route = ap::required_story_route();
  // [charselect agent] Open world and hub: the character select after the hero's name
  // (options.starting_character "any" / a list: the player picks; a named member: to confirm).
  const std::vector<int> choices = ap::starting_choices();
  const int fixed_start = ap::seed_starting_character();
  if (!choices.empty()) ap::set_chosen_start(0);  // (a pick from a New Game backed out to the title)
  bool member_shown = false;
  int member_picked = 0;  // (the cursor's place when the combat mode backs out to the select)
  enum class Step { Sound, Vibration, Character, Name, Member, CombatMode, BattleRank, Done };
  Step step = s.result == 0 ? Step::Sound : Step::Done;
  while (step != Step::Done) {
    switch (step) {
      case Step::Sound:
        sound_select_screen(s);
        if (s.result != 0) {
          step = Step::Done;
          break;
        }
        psyq::SetDispMask(0);
        s.countdown = 2;
        psyq::VSync(2);
        t.sound_demo_state = 1;
        async_load(kLaterDemo, s.bank.data(), 0);
        step = Step::Vibration;
        break;
      case Step::Vibration:
        vibration_screen(s);
        if (s.result != 0) {
          async_load_cancel(kLaterDemo, s.bank.data());
          s.result = 0;
          step = Step::Sound;
          break;
        }
        psyq::SetDispMask(0);
        s.countdown = 2;
        psyq::VSync(2);
        step = Step::Character;
        break;
      case Step::Character:
        if (route != 0) {
          skip_hero_screens(s, route);
          step = Step::Member;
          break;
        }
        character_screen(s);
        if (s.result != 0) {
          step = Step::Vibration;
          break;
        }
        psyq::SetDispMask(0);
        s.countdown = 2;
        psyq::VSync(2);
        step = Step::Name;
        break;
      case Step::Name:
        name_screen(s);
        if (s.result != 0) {
          async_load_cancel(s.config()[0x45] != 0 ? 0xA79 : 0xA78, t.prologue_data.data());
          step = Step::Character;
          break;
        }
        psyq::SetDispMask(0);
        s.countdown = 2;
        psyq::VSync(2);
        step = Step::Member;
        break;
      case Step::Member: {
        const int hero = s.config()[0x45] + 1;
        member_shown = !choices.empty() || (fixed_start != 0 && fixed_start != hero);
        if (!member_shown) {
          step = Step::CombatMode;
          break;
        }
        const int picked = member_select_screen(s, hero, choices.empty() ? fixed_start : 0, choices, member_picked);
        member_picked = 0;
        if (s.result != 0) {
          s.result = 0;
          if (route != 0) async_load_cancel(hero == 2 ? 0xA79 : 0xA78, t.prologue_data.data());
          step = route != 0 ? Step::Vibration : Step::Name;
          break;
        }
        if (!choices.empty()) ap::set_chosen_start(picked == hero ? 0 : picked);
        member_picked = picked;
        psyq::SetDispMask(0);
        s.countdown = 2;
        psyq::VSync(2);
        step = Step::CombatMode;
        break;
      }
      case Step::CombatMode:
        combat_mode_screen(s);
        if (s.result != 0 && member_shown) {
          s.result = 0;
          step = Step::Member;
        } else if (s.result != 0 && route != 0) {
          async_load_cancel(s.config()[0x45] != 0 ? 0xA79 : 0xA78, t.prologue_data.data());
          step = Step::Vibration;
        } else if (s.result != 0) {
          step = Step::Name;
        } else {
          step = t.battle_rank_unlocked ? Step::BattleRank : Step::Done;
        }
        break;
      case Step::BattleRank:
        battle_rank_screen(s);
        step = s.result != 0 ? Step::CombatMode : Step::Done;
        break;
      case Step::Done:
        break;
    }
  }

  async_load_cancel(kSoundDemo, s.bank.data());
  async_load_cancel(kLaterDemo, s.bank.data());
  while (sound_bank_busy(t.sound) != 0) {
  }
  sound_stop(t.sound);
  screen_destroy_arena(s.main->arena);
  screen_destroy_ot(s.main->ot);
  delete s.main;
  *holder = nullptr;
  text.strings.clear();
  text.font.clear();
  text.pixels.clear();
  deferred_delete_update();
  deferred_delete_update();
  psyq::SetDispMask(0);
  psyq::ResetGraph(1);
  return s.result;
}

}  // namespace ss::game
