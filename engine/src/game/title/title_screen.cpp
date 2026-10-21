#include "game/title/title_screen.h"

#include <algorithm>
#include <vector>

#include "disc/slz.h"
#include "game/boot/pad.h"
#include "game/boot/rng.h"
#include "game/boot/stubs.h"
#include "game/common/texture.h"
#include "game/title/movie.h"
#include "game/title/title_program.h"
#include "platform/window.h"
#include "runtime/libgpu.h"
#include "runtime/runtime.h"

namespace ss::game {
namespace {

using psyq::addPrim;

constexpr u16 kConfirmButtons = platform::kPadStart | platform::kPadCross;  // 0x840

struct MenuRect {
  s16 u0, v0, u1, v1;
};

// Menu label rectangles in the text sheet: 0x8004e264 (3 entries), 0x8004e27c (12).
MenuRect menu_rect(u32 table, int index) {
  const u8* p = title_data(table + static_cast<u32>(index) * 8);
  if (!p) return {};
  return {static_cast<s16>(read_u16(p)), static_cast<s16>(read_u16(p + 2)), static_cast<s16>(read_u16(p + 4)),
          static_cast<s16>(read_u16(p + 6))};
}

// A highlight underline (LINE_F2) of the given colour.
void add_underline(RenderContext* ctx, u32 rgb, int x0, int x1, int y) {
  auto* l = screen_alloc_prims<psyq::LINE_F2>(ctx->arena, 1);
  if (!l) return;
  psyq::setlen(l, 3);
  l->r0 = rgb & 0xFF;
  l->g0 = rgb >> 8 & 0xFF;
  l->b0 = rgb >> 16 & 0xFF;
  l->code = 0x40;
  l->x0 = static_cast<s16>(x0);
  l->y0 = static_cast<s16>(y);
  l->x1 = static_cast<s16>(x1);
  l->y1 = static_cast<s16>(y);
  addPrim(ctx->ot_head, l);
}

// A label from the text sheet. Rows past 255 live in the next texture page down.
void add_label(TitleIntro* self, int x, int y, const MenuRect& r, int clut_row, int brightness) {
  auto* p = screen_alloc_prims<psyq::POLY_FT4>(self->render->arena, 1);
  if (!p) return;
  int v = r.v0;
  u16 tpage = 0x8A;
  if (v > 0xFF) {
    v -= 0x100;
    tpage = 0x9A;
  }
  set_textured_quad(p, x, y, r.u1 - r.u0, r.v1 - r.v0, r.u0, v, tpage, static_cast<u16>(clut_row << 6),
                    static_cast<u8>(brightness), false);
  addPrim(self->render->ot_head, p);
}

}  // namespace

int title_screen(TitleIntro* self) {
  runtime::Runtime::get().trace("title_screen");
  TitleGlobals& t = title_globals();
  auto& gpu = runtime::Runtime::get().gpu;
  screen_setup_buffer0(0, 0, 0x280, 0x1E0, 0, 0, 0, 0);
  screen_setup_buffer1(0, 0, 0x280, 0x1E0, 0, 0, 0, 0);
  for (int i = 0; i < 2; ++i) {
    screen_drawenv(i)->isbg = 0;
    screen_drawenv(i)->dfe = 1;
    screen_dispenv(i)->isinter = 1;
  }
  int frames_until_display = 2;
  int cursor = 0;
  bool confirmed = false;
  vram_upload_packed_block(gpu, t.entry12, 0, 0, 0, 0x1E0);      // background
  vram_upload_packed_block(gpu, t.entry12, 1, 0x280, 0, 0x1E1);  // menu text
  vram_upload_packed_clut(gpu, t.entry12, 2, 0x1E2);              // highlighted text
  vram_upload_packed_clut(gpu, t.entry12, 3, 0x1E4);              // unavailable text
  while (t.loader->poll() != 0) {
  }

  // Title music: archive entry #2677 (bank + SLZ sequence), started once loaded.
  std::vector<u8> sequence(0x8000);
  std::vector<u8> sound_entry(static_cast<size_t>(
      std::max(t.loader->entry_size(0xA74), t.loader->entry_size(0xA75))));
  t.loader->select_entry(0xA75);
  t.loader->start_read(sound_entry.data(), 0);
  if (memcard_start_check(t.memcard) != 0) {
    int status;
    do {
      rng_next();
      memcard_update(t.memcard);
      status = memcard_status(t.memcard);
      if (status == 2) cursor = kTitleContinue;  // saves found: start on Continue
    } while (status < 1);
  }

  int brightness = 0x20;
  int sound_state = 0;    // 0 loading, 1 bank sent, 2 music playing
  int result = 0;
  int idle_frames = 0;    // frames on the menu (disc 1 only)
  bool demo_loading = false, play_demo = false;
  int input_cooldown = 0; // 600 frames after any cursor move
  int highlight_blue = 0x1E00;
  pad_set_port_mask(t.pad, 3);
  pad_clear(t.pad, 0);

  // The menu sits on a single 640x480 buffer, so the background under it is kept
  // and restored every frame before drawing.
  const psyq::RECT saved_rect{0xC0, 0x140, 0xE0, 0x80};
  std::vector<u16> saved(static_cast<size_t>(0xE0) * 0x80);
  bool have_saved = true;
  psyq::StoreImage(&saved_rect, saved.data());
  screen_put_env();
  psyq::DrawSync(0);

  for (;;) {
    rng_next();
    const int card = memcard_status(t.memcard);
    screen_flip();
    RenderContext* ctx = self->render;
    ctx->ot_head = screen_clear_ot(ctx->ot);
    const u16 pressed = pad_pressed(t.pad, 0);
    const u16 repeat = pad_pressed_repeat(t.pad, 0);
    pad_clear(t.pad, 0);
    pad_pressed(t.pad, 1);
    pad_pressed_repeat(t.pad, 1);
    pad_clear(t.pad, 1);

    if (t.loader->poll() != 1 && sound_state == 0) {
      sound_state = 1;
      sound_load_bank(t.sound, sound_entry.data() + read_u32(sound_entry.data()), 0, 0);
      if (auto seq = disc::slz::unpack_block(std::span<const u8>(sound_entry).subspan(4), 0, 0, nullptr))
        std::copy_n(seq->begin(), std::min(seq->size(), sequence.size()), sequence.begin());
    }
    if (sound_state == 1 && sound_bank_busy(t.sound) != 1) {
      sound_state = 2;
      sound_play_sequence(t.sound, sequence.data());
    }

    bool fading_out = confirmed;
    if (!confirmed) {
      const bool continue_ok = static_cast<u32>(card - 1) < 2;
      if ((pressed & kConfirmButtons) != 0 && (cursor != kTitleContinue || continue_ok)) {
        if (t.loader->poll() != 0) t.loader->cancel();
        have_saved = false;
        sound_fade_out(t.sound, 0x10);
        sound_play_sfx(t.sound, 2, 0, 0x7F40);
        if (self->field_20 == 0) {
          while (t.loader->poll() != 0) {
          }
          // Data for the chosen path is loaded while the screen fades.
          auto load_run = [&](std::vector<u8>& dest, int first, int count) {
            int bytes = 0;
            for (int i = 0; i < count; ++i) bytes += t.loader->entry_size(first + i);
            dest.assign(static_cast<size_t>(bytes), 0);
            t.loader->select_entry(first);
            t.loader->start_read(dest.data(), bytes);
          };
          if (cursor == kTitleNewGame) load_run(t.new_game_data, 0x0E, 3);       // 0x8004ec04
          if (cursor == kTitleContinue) load_run(t.load_screen_data, 0xBD0, 3);  // 0x8004ec10
          if (cursor == kTitleVoiceCollection) load_run(t.voice_data, 0x0D, 1);  // 0x8004ec0c
        }
        result = cursor;
        confirmed = true;
      }
      if (self->field_20 == 0) {
        if (repeat & platform::kPadUp) {
          if (--cursor < 0) {
            cursor = 0;
          } else {
            sound_play_sfx(t.sound, 1, 0, 0x7F40);
          }
          input_cooldown = 600;
        }
        if (repeat & platform::kPadDown) {
          if (++cursor < 3) {
            sound_play_sfx(t.sound, 1, 0, 0x7F40);
          } else {
            cursor = 2;
          }
          input_cooldown = 600;
        }
      } else {
        if (repeat & platform::kPadUp) {
          if (--self->field_24 < 0) {
            self->field_24 = 0;
          } else {
            sound_play_sfx(t.sound, 1, 0, 0x7F40);
          }
          input_cooldown = 600;
        }
        if (repeat & platform::kPadDown) {
          if (++self->field_24 < 0x0C) {
            sound_play_sfx(t.sound, 1, 0, 0x7F40);
          } else {
            self->field_24 = 0x0B;
          }
          input_cooldown = 600;
        }
      }
      fading_out = confirmed;
      if (!confirmed && brightness < 0x80) {
        highlight_blue += 0xF00;
        brightness += 0x10;
      }
    }

    if (fading_out) {
      highlight_blue -= 0x780;
      brightness -= 8;
      if (brightness < 0) {
        while (sound_bank_busy(t.sound) != 0) {
        }
        sound_stop(t.sound);
        if (!play_demo) {
          psyq::SetDispMask(0);
          if (self->field_20 != 0) result = self->field_24 + 4;
        } else {
          // Attract mode: the opening movie with its music.
          sound_load_bank(t.sound, sound_entry.data() + read_u32(sound_entry.data()), 0, 0);
          if (auto seq = disc::slz::unpack_block(std::span<const u8>(sound_entry).subspan(4), 0, 0, nullptr))
            std::copy_n(seq->begin(), std::min(seq->size(), sequence.size()), sequence.begin());
          while (sound_bank_busy(t.sound) != 0) {
          }
          while (t.loader->poll() != 0) {
          }
          Movie* movie = movie_create(new Movie, 0, 0, 0x140, 0xF0, 1, 3);
          movie_open(movie, 0x11D3);
          bool music_started = false;
          pad_clear(t.pad, 0);
          psyq::SetDispMask(0);
          for (;;) {
            if (movie_frame_count() > 9 && !music_started) {
              music_started = true;
              sound_play_sequence(t.sound, sequence.data());  // TODO(port): sound system (stub logs once)
            }
            const u16 skip = pad_pressed(t.pad, 0);
            pad_clear(t.pad, 0);
            if (skip != 0 || movie_update(movie) != 1) break;
            movie_present(movie);
          }
          t.loader->cancel();
          sound_stop(t.sound);
          while (t.loader->poll() != 0) {
          }
          delete movie;
          psyq::SetDispMask(0);
          result = kTitleAttractMovie;
        }
        return result;
      }
      // Darken the whole screen a little more each frame (subtractive tile).
      if (auto* tile = screen_alloc_prims<psyq::TILE>(ctx->arena, 1)) {
        psyq::setlen(tile, 3);
        tile->r0 = tile->g0 = tile->b0 = 0x10;
        tile->code = 0x62;
        tile->x0 = tile->y0 = 0;
        tile->w = 0x280;
        tile->h = 0x1E0;
        addPrim(ctx->ot_head, tile);
      }
      if (auto* mode = screen_alloc_prims<psyq::DR_MODE>(ctx->arena, 1)) {
        psyq::SetDrawMode(mode, 1, 1, 0x40, nullptr);
        addPrim(ctx->ot_head, mode);
      }
    }

    if (!confirmed) {
      const u32 highlight = static_cast<u32>((brightness << 4) >> 7 | ((brightness << 5) >> 7) << 8 |
                                             (highlight_blue >> 7) << 16);
      for (int i = 0; i < 3; ++i) {
        const MenuRect r = menu_rect(0x8004E264, i);
        const int w = r.u1 - r.u0;
        const int x = 0xC0 - (w / 2 - 0x80);
        int clut_row = 0x1E1;
        if (cursor == i) {
          add_underline(ctx, highlight, x, x + w, 0x154 + i * 0x20);
          clut_row = 0x1E2;
        }
        if (static_cast<u32>(card - 1) >= 2 && i == kTitleContinue) clut_row = 0x1E4;
        add_label(self, x, 0x140 + i * 0x20, r, clut_row, brightness);
      }
    }
    if (self->field_20 != 0) {
      for (int i = 0; i < 0x0C; ++i) {
        const MenuRect r = menu_rect(0x8004E27C, i);
        const bool selected = self->field_24 == i;
        const u32 colour = selected ? static_cast<u32>((brightness << 4) >> 7 | ((brightness << 5) >> 7) << 8 |
                                                       (highlight_blue >> 7) << 16)
                                    : 0;
        add_underline(ctx, colour, 0x20, 0x20 + (r.u1 - r.u0), 0xA0 + i * 0x14);
        add_label(self, 0x20, 0x90 + i * 0x14, r, selected ? 0x1E2 : 0x1E1, brightness);
      }
    }

    if (loader_is_disc(*t.loader, 1) != 0) ++idle_frames;
    if (idle_frames > 0x95F && !demo_loading && t.loader->poll() != 1) {
      demo_loading = true;
      t.loader->select_entry(0xA74);
      t.loader->start_read(sound_entry.data(), 0);
    }
    input_cooldown = std::max(0, input_cooldown - 1);
    if (demo_loading && !confirmed && t.loader->poll() != 1 && input_cooldown == 0) {
      play_demo = true;
      confirmed = true;
    }
    memcard_update(t.memcard);
    psyq::DrawSync(0);
    psyq::VSync(0);
    if (!confirmed && have_saved) psyq::LoadImage(&saved_rect, saved.data());
    screen_draw_ot(self->render->ot);
    if (frames_until_display != 0 && --frames_until_display == 0) psyq::SetDispMask(1);
  }
}

}  // namespace ss::game
