#include "game/title/intro.h"

#include <cstdlib>
#include <memory>
#include <vector>

#include "disc/slz.h"
#include "game/boot/pad.h"
#include "game/boot/rng.h"
#include "game/boot/stubs.h"
#include "game/common/math.h"
#include "game/common/texture.h"
#include "game/title/movie.h"
#include "game/title/title_program.h"
#include "runtime/libgpu.h"
#include "runtime/runtime.h"

namespace ss::game {
namespace {

using psyq::addPrim;
using psyq::LINE_G2;
using psyq::POLY_FT4;

void add(TitleIntro* self, void* prim) { addPrim(self->render->ot_head, prim); }

// 0x8003ead8: the "Sony Computer Entertainment America presents" screen, a
// 320x240 4-bit image split across two texture pages.
void draw_scea(TitleIntro* self, int brightness) {
  auto* p = screen_alloc_prims<POLY_FT4>(self->render->arena, 2);
  if (!p) return;
  set_textured_quad(&p[0], 0, 0, 0xFF, 0xF0, 0, 0, 0x0D, 0x7900, static_cast<u8>(brightness), false);
  add(self, &p[0]);
  set_textured_quad(&p[1], 0xFF, 0, 0x40, 0xF0, 0, 0, 0x0E, 0x7900, static_cast<u8>(brightness), false);
  add(self, &p[1]);
}

// 0x8003ea00: the Enix logo.
void draw_enix(TitleIntro* self, int brightness) {
  auto* p = screen_alloc_prims<POLY_FT4>(self->render->arena, 1);
  if (!p) return;
  set_textured_quad(p, 0x7C, 0x40, 0x48, 0x5E, 0, 0, 0x8A, 0x7800, static_cast<u8>(brightness), false);
  add(self, p);
}

// 0x8003f180: the tri-Ace logo.
void draw_triace(TitleIntro* self, int brightness) {
  auto* p = screen_alloc_prims<POLY_FT4>(self->render->arena, 1);
  if (!p) return;
  set_textured_quad(p, 0x50, 0x58, 0x90, 0x2F, 0, 0, 0x85, 0x7840, static_cast<u8>(brightness), false);
  add(self, p);
}

// 0x8003ec54 and 0x8003ed2c: the two lines of text under the tri-Ace logo.
void draw_triace_text1(TitleIntro* self, int brightness) {
  auto* p = screen_alloc_prims<POLY_FT4>(self->render->arena, 1);
  if (!p) return;
  set_textured_quad(p, 0x56, 0x90, 0x88, 0x10, 0, 0, 0x1C, 0x7880, static_cast<u8>(brightness), false);
  add(self, p);
}

void draw_triace_text2(TitleIntro* self, int brightness) {
  auto* p = screen_alloc_prims<POLY_FT4>(self->render->arena, 1);
  if (!p) return;
  set_textured_quad(p, 0x29, 200, 0xE6, 7, 0, 0, 0x1D, 0x78C0, static_cast<u8>(brightness), false);
  add(self, p);
}

// 0x8003e898 / 0x8003e86c: count the hold timer down, then advance.
void hold_then_next_substate(TitleIntro* self) {
  if (--self->timer < 1) {
    self->timer = 0;
    ++self->substate;
  }
}

void hold_then_next_state(TitleIntro* self) {
  if (--self->timer < 1) {
    self->timer = 0;
    ++self->state;
  }
}

// 0x8003f258: reads the drawn logo back from VRAM and turns every lit pixel into
// a particle that starts scattered away from the logo's centre.
void capture_particles(TitleIntro* self, int x, int y, int w, int h) {
  std::vector<u16> pixels(static_cast<size_t>(w) * h);
  // The frame being drawn is in the buffer that is not current yet.
  const psyq::RECT r{static_cast<s16>(x), static_cast<s16>(y + (screen_draw_y() == 0 ? 0xF0 : 0)),
                     static_cast<s16>(w), static_cast<s16>(h)};
  psyq::StoreImage(&r, pixels.data());
  psyq::DrawSync(0);
  const int cx = x + w / 2, cy = y + h / 2;
  self->particle_count = 0;
  for (int row = 0, row_base = 0; row < h; ++row, row_base += w) {
    const int py = row + y;
    const bool above = py < cy;
    for (int col = 0; col < w; ++col) {
      const u32 c = pixels[static_cast<size_t>(col + row_base)];
      if (c == 0 || self->particle_count >= TitleIntro::kMaxParticles) continue;
      const int n = self->particle_count;
      const int px = col + x;
      self->alive[n] = 1;
      self->target_x[n] = px * 0x10000;
      self->target_y[n] = py * 0x10000;
      const u32 angle = rng_range(0x400);
      s32 cs = cos4096(angle), sn = sin4096(angle);
      if (px < cx) cs = -cs;
      if (above) sn = -sn;
      const s32 dx = cs * 0x10 * std::abs(px - cx);
      const s32 dy = sn * 0x10 * std::abs(py - cy);
      self->pos_x[n] = (self->target_x[n] + dx * 2) * 0x10;
      self->pos_y[n] = (self->target_y[n] + dy * 8) * 0x10;
      self->target_x[n] <<= 4;
      self->target_y[n] <<= 4;
      const s32 back_x = dx * -2, back_y = dy * -8;
      self->vel_x[n] = back_x / 512;
      self->vel_y[n] = back_y / 512;
      self->trail_x[n] = back_x;
      self->trail_y[n] = back_y;
      self->accel_x[n] = back_x / 64;
      self->accel_y[n] = back_y / 64;
      self->red[n] = self->green[n] = self->blue[n] = 0;
      self->target_red[n] = static_cast<u8>(((c & 0x1F) * 0x2022) / 1000);
      self->target_green[n] = static_cast<u8>((((c & 0x3E0) >> 5) * 0x2022) / 1000);
      self->target_blue[n] = static_cast<u8>((((c & 0x7C00) >> 10) * 0x2022) / 1000);
      ++self->particle_count;
    }
  }
  self->particles_moving = self->particle_count;
}

// 0x8003f760: moves each particle toward its pixel and draws it as an additive
// line with a dark tail; speeds change only on frames drawn at y = 0 (every other).
void update_particles(TitleIntro* self) {
  auto* lines = screen_alloc_prims<LINE_G2>(self->render->arena, static_cast<size_t>(self->particle_count));
  psyq::P_TAG* ot = self->render->ot_head;
  const bool even_frame = screen_draw_y() == 0;
  for (int i = 0; lines && i < self->particle_count; ++i) {
    const s32 vx = self->vel_x[i], vy = self->vel_y[i];
    const s32 tx = self->trail_x[i], ty = self->trail_y[i];
    const s32 px = self->pos_x[i], py = self->pos_y[i];
    const u8 r = std::min<int>(self->target_red[i], self->red[i] + 2);
    const u8 g = std::min<int>(self->target_green[i], self->green[i] + 2);
    const u8 b = std::min<int>(self->target_blue[i], self->blue[i] + 2);
    self->red[i] = r;
    self->green[i] = g;
    self->blue[i] = b;

    LINE_G2* l = &lines[i];
    psyq::setlen(l, 4);
    l->r0 = r;
    l->g0 = g;
    l->b0 = b;
    l->code = 0x52;  // gouraud line, semi-transparent
    // The original packs x without masking it to 16 bits, so a particle left of
    // the screen (negative x) also sets y to -1; reproduced as-is.
    const u32 xy0 = static_cast<u32>(px >> 20) | (static_cast<u32>(py >> 4) & 0xFFFF0000u);
    l->x0 = static_cast<s16>(xy0 & 0xFFFF);
    l->y0 = static_cast<s16>(xy0 >> 16);
    l->r1 = 0x08;
    l->g1 = 0x10;
    l->b1 = 0x30;
    l->p1 = 0;
    l->x1 = static_cast<s16>((px + tx * -6) >> 20);
    l->y1 = static_cast<s16>((py + ty * -6) >> 20);

    bool arrived = false;
    if (self->alive[i] != 0 && std::abs(px - self->target_x[i]) <= std::abs(vx) + 0x100000 &&
        std::abs(py - self->target_y[i]) <= std::abs(vy) + 0x100000) {
      arrived = true;
    }
    if (arrived) {
      self->alive[i] = 0;
      self->pos_x[i] = self->target_x[i];
      self->pos_y[i] = self->target_y[i];
      --self->particles_moving;
    }
    if (self->alive[i] != 0) {
      self->pos_x[i] = px + vx;
      self->pos_y[i] = py + vy;
      if (even_frame) {
        self->vel_x[i] = vx + self->accel_x[i];
        self->vel_y[i] = vy + self->accel_y[i];
        self->trail_x[i] = tx - self->accel_x[i];
        self->trail_y[i] = ty - self->accel_y[i];
      }
    }
    addPrim(ot, l);
  }
  auto* mode = screen_alloc_prims<psyq::DR_MODE>(self->render->arena, 1);
  if (!mode) return;
  psyq::SetDrawMode(mode, 0, 1, 0x20, nullptr);  // semi-transparency mode 1: additive
  addPrim(self->render->ot_head, mode);
}

}  // namespace

void set_textured_quad(POLY_FT4* p, int x, int y, int w, int h, int u, int v, u16 tpage, u16 clut, u8 brightness,
                       bool semi) {
  p->x0 = p->x2 = static_cast<s16>(x);
  p->y0 = p->y1 = static_cast<s16>(y);
  p->x1 = p->x3 = static_cast<s16>(x + w);
  p->y2 = p->y3 = static_cast<s16>(y + h);
  p->u0 = p->u2 = static_cast<u8>(u);
  p->u1 = p->u3 = static_cast<u8>(u + w);
  p->v0 = p->v1 = static_cast<u8>(v);
  p->v2 = p->v3 = static_cast<u8>(v + h);
  p->tpage = tpage;
  p->clut = clut;
  p->r0 = p->g0 = p->b0 = brightness;
  p->code = semi ? 0x2E : 0x2C;
  psyq::setlen(p, 9);
}

void title_logo_update(TitleIntro* self) {
  switch (self->state) {
    case 0:
      self->brightness = 0;
      ++self->state;
      return;
    case 1:
      switch (self->substate) {
        case 0:
          self->brightness += 4;
          draw_scea(self, self->brightness);
          if (self->brightness < 0x80) return;
          self->timer = 0x74;
          ++self->substate;
          return;
        case 1:
          draw_scea(self, 0x80);
          hold_then_next_substate(self);
          return;
        case 2:
          self->brightness -= 4;
          draw_scea(self, self->brightness);
          if (self->brightness > 0) return;
          self->timer = 0x1E;
          ++self->substate;
          return;
        case 3:
          hold_then_next_substate(self);
          return;
        case 4:
          self->brightness += 4;
          draw_enix(self, self->brightness);
          if (self->brightness < 0x80) return;
          self->timer = 0x74;
          ++self->state;
          return;
        default:
          return;
      }
    case 2:
      draw_enix(self, 0x80);
      hold_then_next_state(self);
      return;
    case 3:
      self->brightness -= 4;
      draw_enix(self, self->brightness);
      if (self->brightness > 0) return;
      self->timer = 5;
      ++self->state;
      return;
    case 4:
      hold_then_next_state(self);
      return;
    case 5:
      // Draw the logo once at full brightness, capture it as particles, then
      // redraw it black so the particles assemble it from nothing.
      draw_triace(self, 0x80);
      screen_draw_ot(self->render->ot);
      psyq::DrawSync(0);
      capture_particles(self, 0x50, 0x58, 0x90, 0x2F);
      self->render->ot_head = screen_clear_ot(self->render->ot);
      draw_triace(self, 0);
      screen_draw_ot(self->render->ot);
      ++self->state;
      return;
    case 6:
      update_particles(self);
      if (self->particles_moving != 0) return;
      ++self->state;
      return;
    case 7:
      draw_triace(self, 0x80);
      draw_triace_text1(self, self->brightness);
      draw_triace_text2(self, self->brightness);
      self->brightness += 4;
      if (self->brightness < 0x80) return;
      self->timer = 0x74;
      ++self->state;
      return;
    case 8:
      draw_triace(self, 0x80);
      draw_triace_text1(self, self->brightness);
      draw_triace_text2(self, self->brightness);
      hold_then_next_state(self);
      return;
    case 9:
      draw_triace(self, self->brightness);
      draw_triace_text1(self, self->brightness);
      draw_triace_text2(self, self->brightness);
      self->brightness -= 8;
      if (self->brightness >= 0) return;
      ++self->state;
      return;
    default:
      self->state = -1;
      return;
  }
}

TitleIntro* title_intro_create(TitleIntro* self) {
  screen_init();
  self->textures = nullptr;
  self->field_20 = 0;
  self->field_24 = 0;
  self->render = new RenderContext();
  render_ctx_create_arena(self->render, 0x10000);
  render_ctx_create_ot(self->render, 1);
  return self;
}

void title_intro_sequence(TitleIntro* self) {
  TitleGlobals& t = title_globals();
  pad_set_port_mask(t.pad, 3);
  pad_set_repeat_delay(t.pad, 0x0C);
  screen_setup_buffer0(0, 0, 0x140, 0xF0, 0, 0, 0, 0);
  screen_setup_buffer1(0, 0xF0, 0x140, 0xF0, 0, 0, 0, 0);
  for (int i = 0; i < 2; ++i) {
    screen_drawenv(i)->isbg = 1;
    screen_dispenv(i)->isinter = 1;
    screen_drawenv(i)->r0 = 0;
    screen_drawenv(i)->g0 = 0;
    screen_drawenv(i)->b0 = 0;
  }

  // Logo textures (archive entry #10), while entry #12 streams in behind them.
  std::vector<u8> textures(static_cast<size_t>(t.loader->entry_size(10)));
  t.loader->select_entry(10);
  t.loader->start_read(textures.data(), 0);
  while (t.loader->poll() != 0) {
    rng_next();
    pad_held(t.pad, 0);
    pad_clear(t.pad, 0);
    memcard_update(t.memcard);
  }
  int frames_until_display = 2;
  t.loader->select_entry(0x0C);
  t.loader->start_read(t.entry12.data(), 0);
  vram_upload_packed_block(runtime::Runtime::get().gpu, textures, 0, 0x280, 0, 0x1E0);
  vram_upload_packed_block(runtime::Runtime::get().gpu, textures, 1, 0x140, 0, 0x1E1);
  vram_upload_packed_block(runtime::Runtime::get().gpu, textures, 2, 0x300, 0x100, 0x1E2);
  vram_upload_packed_block(runtime::Runtime::get().gpu, textures, 3, 0x340, 0x100, 0x1E3);
  vram_upload_packed_block(runtime::Runtime::get().gpu, textures, 4, 0x340, 0, 0x1E4);
  textures.clear();
  self->timer = 0;
  self->state = 0;
  self->substate = 0;

  // Entry #2676 (0xa74): a sound bank followed by an SLZ block of sequence data.
  std::vector<u8> sound_entry(static_cast<size_t>(t.loader->entry_size(0xA74)));
  std::vector<u8> sequence(0x8000);
  self->skip_requested = 0;
  bool sound_started = false;
  do {
    rng_next();
    screen_flip();
    RenderContext* ctx = self->render;
    ctx->ot_head = screen_clear_ot(ctx->ot);
    pad_pressed(t.pad, 0);
    pad_clear(t.pad, 0);
    if (t.loader->poll() != 1 && !sound_started) {
      sound_started = true;
      t.loader->select_entry(0xA74);
      t.loader->start_read(sound_entry.data(), 0);
    }
    title_logo_update(self);
    memcard_update(t.memcard);
    psyq::DrawSync(0);
    psyq::VSync(static_cast<u32>(self->state - 0x0B) < 3 ? 2 : 0);
    screen_put_env();
    screen_draw_ot(self->render->ot);
    if (frames_until_display != 0 && --frames_until_display == 0) psyq::SetDispMask(1);
  } while (self->state != -1);

  while (t.loader->poll() != 0) {
  }
  const u32 bank_offset = read_u32(sound_entry.data());
  sound_load_bank(t.sound, sound_entry.data() + bank_offset, 0, 0);
  if (auto seq = disc::slz::unpack_block(std::span<const u8>(sound_entry).subspan(4), 0, 0, nullptr))
    std::copy(seq->begin(), seq->begin() + std::min(seq->size(), sequence.size()), sequence.begin());
  while (sound_bank_busy(t.sound) != 0) {
  }

  // The studio movie (#4586), which cannot be skipped.
  Movie* movie = movie_create(new Movie, 0, 0, 0x140, 0xF0, 1, 3);
  movie_open(movie, 0x11EA);
  pad_clear(t.pad, 0);
  psyq::SetDispMask(0);
  for (;;) {
    pad_pressed(t.pad, 0);
    pad_clear(t.pad, 0);
    if (movie_update(movie) != 1) break;
    movie_present(movie);
    memcard_update(t.memcard);
  }
  t.loader->cancel();
  while (t.loader->poll() != 0) {
  }
  delete movie;

  // The opening (#4563) when this is disc 1. Any button skips it; its music starts
  // once ten frames have gone by.
  if (t.loader->archive() && loader_is_disc(*t.loader, 1)) {
    movie = movie_create(new Movie, 0, 0, 0x140, 0xF0, 1, 3);
    movie_open(movie, 0x11D3);
    bool music_started = false;
    pad_clear(t.pad, 0);
    psyq::SetDispMask(0);
    for (;;) {
      if (movie_frame_count() > 9 && !music_started) {
        music_started = true;
        sound_play_sequence(t.sound, sequence.data());
      }
      const u16 pressed = pad_pressed(t.pad, 0);
      pad_clear(t.pad, 0);
      if (pressed != 0 || movie_update(movie) != 1) break;
      movie_present(movie);
    }
    t.loader->cancel();
    sound_stop(t.sound);
    while (t.loader->poll() != 0) {
    }
    delete movie;
  }
  psyq::SetDispMask(0);
}

}  // namespace ss::game
