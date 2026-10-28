#include "game/title/voice_collection.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "disc/slz.h"
#include "game/boot/pad.h"
#include "game/boot/rng.h"
#include "game/boot/sound.h"
#include "game/common/memcard.h"
#include "game/common/text_rules.h"
#include "game/common/texture.h"
#include "game/title/title_program.h"
#include "platform/window.h"
#include "runtime/libgpu.h"
#include "runtime/runtime.h"

namespace ss::game {
namespace {

constexpr int kVoices = 0x4FE;      // 0x8004e328: the highest voice number
constexpr int kTracks = 0x44;       // battle music entries 0xa51-0xa95
constexpr u32 kVoiceEntries = 0x8004C8DE;  // u16 archive entry of each voice
constexpr u32 kDigitStrip = 0x8004E2DC;    // s16 x of digits 0-9 (and the end of 9)
constexpr u32 kSpread = 0x8004E2F4;        // s32[12]: how a note's level spreads over the columns

int g_total = kVoices;

u32 rd32(const u8* p) {
  u32 v;
  std::memcpy(&v, p, 4);
  return v;
}

bool heard(const u8* config, int voice) { return (rd32(config + 0x1A0 + (voice >> 5) * 4) >> (voice & 0x1F) & 1) != 0; }

u32 xy(int x, int y) { return static_cast<u16>(x) | static_cast<u32>(static_cast<u16>(y)) << 16; }
u32 grey(u32 c) { return c | c << 8 | c << 16; }

// A primitive of N GP0 words (the grid builds its packets word by word, as the original).
template <int N>
struct Packet {
  psyq::P_TAG tag;
  u32 w[N];
};

void add_quad(TitleIntro* self, int x, int y, int w, int h, int u, int v, u16 tpage, int clut_row, int brightness,
              bool semi) {
  auto* p = screen_alloc_prims<psyq::POLY_FT4>(self->render->arena, 1);
  if (p == nullptr) return;
  set_textured_quad(p, x, y, w, h, u, v, tpage, static_cast<u16>(clut_row << 6), static_cast<u8>(brightness), semi);
  psyq::addPrim(self->render->ot_head, p);
}

// The 32 x 40 cells, one per voice (0x800413ec's second half).
void draw_cells(VoiceGrid* g, const u8* config) {
  RenderContext* ctx = g->ctx;
  psyq::P_TAG* ot = ctx->ot_head;
  for (int col = 0; col < 0x20; ++col) {
    for (int row = 0; row < 0x28; ++row) {
      const int voice = row * 0x20 + col;
      if (voice > g_total) continue;
      const s32* lv = g->level[row];
      const s32* next = g->level[row + 1];
      const int x0 = 0x10 + row * 2 + col * 6, x1 = x0 + 6;  // this row's near corners
      const int x2 = x0 + 2, x3 = x2 + 6;                     // the next row's
      if (!heard(config, voice)) {
        // A wire frame (a polyline, command 0x4c, closed by a line), green fading with
        // the row, blue with the level; the selected voice white, in front.
        const int h0 = lv[col] >> 9;
        int blue = h0 << 2;
        if (blue > 0xFF) blue = 0xFF;
        u32 colour = static_cast<u32>(0xFF - row * 5) << 8 | static_cast<u32>(blue) << 16;
        int depth = row;
        if (voice == g->current) {
          depth = 0;
          colour = 0xFFFFFF;
        }
        auto* poly = screen_alloc_prims<Packet<6>>(ctx->arena, 1);
        auto* line = screen_alloc_prims<Packet<3>>(ctx->arena, 1);
        if (poly == nullptr || line == nullptr) return;
        psyq::setlen(poly, 6);
        poly->w[0] = colour | 0x4C000000;
        poly->w[1] = xy(x0, col - (row * 4 + h0 - 200));
        poly->w[2] = xy(x1, col - (row * 4 + (lv[col + 1] >> 9) - 0xC9));
        poly->w[3] = xy(x3, col - ((row + 1) * 4 + (next[col + 1] >> 9) - 0xC9));
        poly->w[4] = xy(x2, col - ((row + 1) * 4 + (next[col] >> 9) - 200));
        poly->w[5] = 0x55555555;
        psyq::setlen(line, 3);
        line->w[0] = colour | 0x40000000;
        line->w[1] = poly->w[4];
        line->w[2] = poly->w[1];
        psyq::addPrim(ot + (0x3E - depth), poly);
        psyq::addPrim(ot + (0x3E - depth), line);
        continue;
      }
      // A textured quad of the picture, lit by the slopes of the levels.
      const int a = 0xFF - row * 4, b = 0xFF - (row + 1) * 4;
      const int h0 = lv[col] >> 9, h1 = lv[col + 1] >> 9;
      int s0 = (h1 - h0) * 8 + 0x80;
      if (s0 < 0) s0 = 0;
      if (s0 > 0x100) s0 = 0x100;
      int s1 = ((lv[col + 2] >> 9) - h1) * 8 + 0x80;
      if (s1 < 0) s1 = 0;
      if (s1 > 0x100) s1 = 0x100;
      auto cap = [](int v) { return static_cast<u32>(v > 0xFF ? 0xFF : v); };
      const u32 c0 = cap((a * s0) >> 8), c1 = cap((a * s1) >> 8), c2 = cap((b * s0) >> 8), c3 = cap((b * s1) >> 8);
      const u32 u0 = static_cast<u32>((col * g->u_scale) >> 5) & 0xFF;
      const u32 u1 = static_cast<u32>(((col + 1) * g->u_scale) >> 5) & 0xFF;
      const u32 v0 = static_cast<u32>(g->v_scale - (row * g->v_scale) / 0x28) & 0xFF;
      const u32 v1 = static_cast<u32>(g->v_scale - ((row + 1) * g->v_scale) / 0x28) & 0xFF;
      const u32 clut = voice == g->current ? 0x78C0 : g->clut;
      auto* q = screen_alloc_prims<Packet<12>>(ctx->arena, 1);
      if (q == nullptr) return;
      psyq::setlen(q, 12);
      q->w[0] = grey(c0) | 0x3C000000;
      q->w[1] = xy(x0, col - (row * 4 + h0 - 200));
      q->w[2] = u0 | v0 << 8 | clut << 16;
      q->w[3] = grey(c1);
      q->w[4] = xy(x1, col - (row * 4 + h1 - 0xC9));
      q->w[5] = u1 | v0 << 8 | static_cast<u32>(g->tpage) << 16;
      q->w[6] = grey(c2);
      q->w[7] = xy(x2, col - ((row + 1) * 4 + (next[col] >> 9) - 200));
      q->w[8] = u0 | v1 << 8;
      q->w[9] = grey(c3);
      q->w[10] = xy(x3, col - ((row + 1) * 4 + (next[col + 1] >> 9) - 0xC9));
      q->w[11] = u1 | v1 << 8;
      psyq::addPrim(ot + (0x3E - row), q);
    }
  }
  auto* mode = screen_alloc_prims<psyq::DR_MODE>(ctx->arena, 1);
  if (mode == nullptr) return;
  psyq::SetDrawMode(mode, 0, 1, 0x20, nullptr);
  psyq::addPrim(ot, mode);
}

}  // namespace

VoiceGrid* voice_grid_create(VoiceGrid* g) {
  *g = VoiceGrid{};
  g->ctx = new RenderContext;
  render_ctx_create_arena(g->ctx, 0x1C000);
  render_ctx_create_ot(g->ctx, 0x80);
  g->field_4460 = 1;
  g->scroll_phase = 0;
  return g;
}

void voice_grid_destroy(VoiceGrid* g) {
  screen_destroy_arena(g->ctx->arena);
  screen_destroy_ot(g->ctx->ot);
  delete g->ctx;
  delete g;
}

void voice_grid_update(VoiceGrid* g) {
  TitleGlobals& t = title_globals();
  sound_tracks(t.sound);
  const SoundVoice* voices = sound_voices(t.sound);
  const u8* config = static_cast<const u8*>(handoff_get(t.handoff, 2));
  if (g->reactive != 0) {
    // Every key-on adds the voice's volume to the columns around its note (three notes
    // a column; 0x8004e2f4 spreads it over twelve).
    const u8* spread = title_data(kSpread);
    for (int i = 0; i < 0x18; ++i) {
      const SoundVoice& v = voices[i];
      const s32 active = static_cast<s8>(v.active);
      if (g->last_active[i] == active) continue;
      g->last_active[i] = active;
      s32 l = v.volume.left, r = v.volume.right;
      if (l < 0) l = -l;
      if (r < 0) r = -r;
      if (active == 0) continue;
      const u32 frac = v.note % 3;
      for (int k = 0; k < 12; ++k) {
        const s32 weight = spread != nullptr ? static_cast<s32>(rd32(spread + k * 4)) : 0;
        const u32 c = static_cast<u32>(v.note / 3 + k - 9);
        if (c < 0x20) g->level[0][c] += (static_cast<s32>(3 - frac) * (l + r) * weight) / 0x300;
        if (c + 1 < 0x20) g->level[0][c + 1] += (static_cast<s32>(frac) * (l + r) * weight) / 0x300;
      }
    }
    for (int col = 0; col < 0x22; ++col) {
      if (g->scroll_phase == 0)
        for (int row = 0x60; row > 0; --row) g->level[row][col] = g->level[row - 1][col];
      s32& lv = g->level[0][col];
      if (lv != 0) {
        lv -= lv / 16;
        if (lv < 0) lv = 0;
        if (lv > 0x7FFF) lv = 0x7FFF;
      }
    }
    g->scroll_phase = (g->scroll_phase + 1) & 1;
  }
  draw_cells(g, config);
}

void draw_number(TitleIntro* self, int x, int y, int value, int digits, int clut_row, int brightness) {
  const u8* strip = title_data(kDigitStrip);
  x += digits * 10;
  for (int i = 0; i < digits; ++i) {
    if (value % 10 == 0 && value == 0 && i != 0) break;
    const int d = value % 10;
    const s16 u = strip != nullptr ? static_cast<s16>(read_u16(strip + d * 2)) : 0;
    const s16 end = strip != nullptr ? static_cast<s16>(read_u16(strip + d * 2 + 2)) : 0;
    add_quad(self, x, y, end - u, 0xB, u, text::layout(text::lang()).digit_strip_v, 0x8C, clut_row, brightness,
             false);  // NA v 0x32
    x -= 10;
    value /= 10;
  }
}

void voice_collection(TitleIntro* self) {
  TitleGlobals& t = title_globals();
  RenderContext* ctx = self->render;
  psx::Gpu& gpu = runtime::Runtime::get().gpu;
  int display_delay = 2;
  VoiceGrid* grid = voice_grid_create(new VoiceGrid);
  screen_setup_buffer0(0, 0, 0x140, 0xF0, 0, 0, 0, 0);
  screen_setup_buffer1(0, 0xF0, 0x140, 0xF0, 0, 0, 0, 0);
  for (int i = 0; i < 2; ++i) {
    screen_drawenv(i)->isbg = 1;
    screen_dispenv(i)->isinter = 0;
    screen_drawenv(i)->r0 = screen_drawenv(i)->g0 = screen_drawenv(i)->b0 = 0;
  }
  while (t.loader->poll() != 0) {
  }
  // Entry #13: the text sheet (block 0, (0x300, 0), palettes 0x1e1 / 0x1e2), the
  // picture (block 1, (0x1c0, 0), palette 0x1e0) and the selection palette, row 0x1e0
  // with every channel's top bit flipped (row 0x1e3).
  vram_upload_packed_block(gpu, t.voice_data, 0, 0x300, 0, 0x1E1);
  vram_upload_packed_block(gpu, t.voice_data, 1, 0x1C0, 0, 0x1E0);
  vram_upload_packed_clut(gpu, t.voice_data, 2, 0x1E2);
  u16 row[0x100];
  const psyq::RECT palette{0, 0x1E0, 0x100, 1};
  psyq::StoreImage(&palette, row);
  psyq::DrawSync(0);
  for (u16& c : row) c = static_cast<u16>((c & 0x7FFF) ^ 0x4210);
  upload_clut256(gpu, row, 0, 0x1E3);
  psyq::DrawSync(0);
  int cursor = 0, voice = 0;
  bool bgm_unlocked = false, bonus = false, exiting = false;
  int track = 0, fade = 0xF0, slide = 0, bgm_brightness = 0;
  t.voice_data.clear();
  t.voice_data.shrink_to_fit();
  t.battle_rank_unlocked = false;
  grid->tpage = 0x87;
  grid->clut = 0x7800;
  grid->u_scale = 0xFF;
  grid->v_scale = 0xFF;
  u8* config = static_cast<u8*>(handoff_get(t.handoff, 2));
  g_total = kVoices;
  grid->reactive = 0;
  // The heard bits come back from the cards' saves (memcard_update, every frame).
  std::memset(config + 0x1A0, 0, 0x100);
  std::vector<u8> sequence(0x9400), clip(0x11800), bgm(0x69000);
  int play = 0;  // 1-5 a voice, 6-10 a track
  bool play_bgm = false, shuffle_on = false;
  int shuffle_at = 0, shuffle_wait = 0;
  pad_set_port_mask(t.pad, 3);
  memcard_enable_summaries(t.memcard, 1);
  u8 order[kTracks] = {};
  pad_clear(t.pad, 0);
  u32 seen[0x40] = {};
  auto reshuffle = [&] {
    for (int k = 0; k < 500; ++k) {
      const u32 i = rng_range(kTracks), j = rng_range(kTracks);
      const u8 tmp = order[i];
      order[i] = order[j];
      order[j] = tmp;
    }
  };
  for (;;) {
    screen_flip();
    ctx->ot_head = screen_clear_ot(ctx->ot);
    grid->ctx->ot_head = screen_clear_ot(grid->ctx->ot);
    const u16 pressed = pad_pressed(t.pad, 0);
    const u16 held = pad_held(t.pad, 0);
    const u16 repeat = pad_pressed_repeat(t.pad, 0);
    pad_clear(t.pad, 0);
    pad_held(t.pad, 1);
    pad_clear(t.pad, 1);
    if (fade != 0) {  // fading in or out: a subtractive grey tile over everything
      auto* tile = screen_alloc_prims<psyq::TILE>(ctx->arena, 1);
      if (tile == nullptr) return;
      psyq::setlen(tile, 3);
      tile->code = 0x62;
      tile->r0 = tile->g0 = tile->b0 = static_cast<u8>(fade);
      tile->x0 = tile->y0 = 0;
      tile->w = 0x140;
      tile->h = 0xF0;
      psyq::addPrim(ctx->ot_head, tile);
      auto* mode = screen_alloc_prims<psyq::DR_MODE>(ctx->arena, 1);
      if (mode == nullptr) return;
      psyq::SetDrawMode(mode, 0, 1, 0x40, nullptr);
      psyq::addPrim(ctx->ot_head, mode);
    }
    if (!exiting) {
      fade = fade - 0x20 < 0 ? 0 : fade - 0x20;
    } else {
      fade += 0x20;
      if (fade > 0xE0) {
        memcard_enable_summaries(t.memcard, 0);
        t.loader->cancel();
        while (sound_bank_busy(t.sound) != 0) {
        }
        sound_stop(t.sound);
        while (t.loader->poll() != 0) {
        }
        voice_grid_destroy(grid);
        deferred_delete_update();
        deferred_delete_update();
        psyq::SetDispMask(0);
        psyq::ResetGraph(1);
        return;
      }
    }
    // Once the cards are read: the unlocks by the share of voices heard.
    if (memcard_take_result(t.memcard) == -1) {
      int count = 0;
      for (int bit = 0; bit < 0x20; ++bit)
        for (int w = 0; w < 0x28; ++w)
          if ((rd32(config + 0x1A0 + w * 4) >> bit & 1) != 0) ++count;
      if (!t.battle_rank_unlocked && g_total / 2 < count) {
        t.battle_rank_unlocked = true;
        sound_play_sfx(t.sound, 0x13, 0, 0x7F40);
      }
      if (!bgm_unlocked && (g_total * 0x4B) / 100 < count) {
        bgm_unlocked = true;
        sound_play_sfx(t.sound, 0x14, 0, 0x7F40);
      }
      if (!bonus && (g_total * 0x5F) / 100 < count) {
        bonus = true;
        sound_play_sfx(t.sound, 0x15, 0, 0x7F40);
      }
      grid->reactive = bonus ? 1 : 0;
    }
    // Rows: 0 the voice, then (75% heard) 1 the track, 2 random, 3 stop; last, exit.
    int step = 0;
    if (!exiting) {
      const int last = bgm_unlocked ? 4 : 1;
      if ((pressed & platform::kPadCross) != 0 && cursor == last) {
        exiting = true;
        sound_play_sfx(t.sound, 2, 0, 0x7F40);
      }
      if ((repeat & platform::kPadUp) != 0 && --cursor < 0) cursor = 0;
      if ((repeat & platform::kPadDown) != 0 && ++cursor > last) cursor = last;
      if ((repeat & platform::kPadLeft) != 0) step = -1;
      if ((repeat & platform::kPadRight) != 0) step = 1;
      if ((held & platform::kPadCircle) != 0) step *= 10;
    }
    if (cursor == 0) {
      voice += step;
      if (voice < 0) voice = 0;
      if (voice > g_total) voice = g_total;
    } else if (cursor == 1 && bgm_unlocked) {
      track += step;
      if (track < 0) track = 0;
      if (track > kTracks) track = kTracks;
    }
    switch (play) {
      case 1:  // stop any read
        if (t.loader->poll() != 0) t.loader->cancel();
        ++play;
        break;
      case 2:
        if (t.loader->poll() == 0 && sound_bank_busy(t.sound) == 0) play = play_bgm ? 6 : 3;
        break;
      case 3: {  // the voice's entry (0xffff would start over)
        sound_stop(t.sound);
        for (int s = 1; s < 8; ++s) sound_release_clip(t.sound, s);
        const u8* entries = title_data(kVoiceEntries);
        const u16 entry = entries != nullptr ? read_u16(entries + voice * 2) : 0xFFFF;
        play = entry == 0xFFFF ? 1 : 4;
        t.loader->select_entry(entry);
        t.loader->start_read(clip.data(), 0);
        break;
      }
      case 4:
        if (t.loader->poll() != 1) ++play;
        break;
      case 5: {  // played straight from RAM, or streamed when long
        sound_open_clip(t.sound, clip.data(), 1, nullptr);
        const u8* bank = clip.data() + rd32(clip.data());
        const int mode = static_cast<s32>(rd32(bank)) - read_u16(bank + 4) > 0x57E4 ? 2 : 1;
        sound_open_clip(t.sound, clip.data(), mode, nullptr);
        play = 0;
        break;
      }
      case 6:  // the track's entry: its bank, then its sequence (SLZ at +4)
        sound_stop(t.sound);
        for (int s = 1; s < 8; ++s) sound_release_clip(t.sound, s);
        t.loader->select_entry(0xA51 + track);
        ++play;
        t.loader->start_read(bgm.data(), 0);
        break;
      case 7:
        if (t.loader->poll() != 1) {
          ++play;
          sound_load_bank(t.sound, bgm.data() + rd32(bgm.data()), 0, 0);
        }
        break;
      case 8: {
        ++play;
        std::string error;
        if (auto out = disc::slz::unpack_block(bgm, 4, 0, &error)) {
          std::memcpy(sequence.data(), out->data(), std::min(out->size(), sequence.size()));
        }
        break;
      }
      case 9:
        if (sound_bank_busy(t.sound) != 1) ++play;
        break;
      case 10:
        play = 0;
        sound_play_sequence(t.sound, sequence.data());
        break;
      default:
        break;
    }
    if ((pressed & platform::kPadCross) != 0) {
      if (cursor == 0) {
        if (!heard(config, voice)) {
          sound_play_sfx(t.sound, 7, 0, 0x7F40);
        } else {
          play = 1;
          play_bgm = false;
          shuffle_on = false;
        }
      }
      if (cursor == 1 && bgm_unlocked) {
        play = 1;
        play_bgm = true;
        shuffle_on = false;
      }
      if (cursor == 2 && bgm_unlocked) {
        for (int i = 0; i < kTracks; ++i) order[i] = static_cast<u8>(i);
        reshuffle();
        track = order[0];
        play = 1;
        shuffle_at = 1;
        play_bgm = true;
        shuffle_on = true;
      }
      if (cursor == 3) {
        shuffle_on = false;
        sound_fade_out(t.sound, 0xF0);
      }
    }
    // Random: once a track has looped twice it fades out; three seconds after the
    // music stops the next one starts.
    if (shuffle_on) {
      if (sound_music_playing(t.sound) == 0) {
        if (play == 0 && ++shuffle_wait > 0xB4) {
          if (shuffle_at > kTracks) {
            reshuffle();
            shuffle_at = 0;
          }
          play = 1;
          track = order[shuffle_at++];
          play_bgm = true;
          shuffle_on = true;
          shuffle_wait = 0;
        }
      } else if (t.sound->loop_count > 1 && t.sound->music_fade.direction == 0) {
        shuffle_wait = 0;
        sound_fade_out(t.sound, 0x1B8);
      }
    }
    // The menu.
    int clut_rows[5] = {0x1E1, 0x1E1, 0x1E1, 0x1E1, 0x1E1};
    clut_rows[cursor] = 0x1E2;
    add_quad(self, 0x10, 0x10, 0xAE, 0x12, 4, 6, 0x8C, 0x1E1, 0x80, false);           // VOICE COLLECTION
    // The label quads fit the release's texture (NA: VOICE w 0x2d, EXIT 0x1f / v 0x50, BGM 0x2e x 0xc / v 0x32,
    // RANDOM 0x3f / u 5, STOP 0x24 x 0xd / u 4 v 0x60).
    const text::Layout& lay = text::layout(text::lang());
    add_quad(self, 0x10, 0x28, lay.voice_label_w, 0xC, 4, 0x40, 0x8C, clut_rows[0], 0x80, false);  // VOICE
    draw_number(self, 0x40, 0x28, voice, 4, heard(config, voice) ? 0x1E2 : 0x1E1, 0x80);
    if (!bgm_unlocked) {
      add_quad(self, 0x10, 0x36, lay.exit_label[0], 0xB, 4, lay.exit_label[1], 0x8C, clut_rows[1], 0x80, false);  // EXIT
    } else {
      // The music rows fade in while EXIT slides down to its new place.
      const bool sliding = slide <= 0x2A;
      if (!sliding) {
        bgm_brightness = 0x80;
        slide = 0x2B;
      } else {
        ++slide;
        bgm_brightness += 3;
      }
      add_quad(self, 0x10, 0x36, lay.bgm_label[0], lay.bgm_label[1], 3, lay.bgm_label[2], 0xAC, clut_rows[1],
               bgm_brightness, sliding);  // BGM
      add_quad(self, 0x10, 0x44, lay.random_label[0], 0xB, lay.random_label[1], 0x70, 0xAC, clut_rows[2],
               bgm_brightness, sliding);  // RANDOM
      add_quad(self, 0x10, 0x52, lay.stop_label[0], lay.stop_label[1], lay.stop_label[2], lay.stop_label[3], 0xAC,
               clut_rows[3], bgm_brightness, sliding);  // STOP
      add_quad(self, 0x10, slide + 0x36, lay.exit_label[0], 0xB, 4, lay.exit_label[1], 0x8C, clut_rows[4], 0x80,
               false);  // EXIT
      draw_number(self, 0x40, 0x36, track, 4, 0x1E1, bgm_brightness);
    }
    grid->current = voice;
    voice_grid_update(grid);
    // Bits that changed behind the card reader's back are dropped; then the reader
    // merges the saves' bits again.
    for (int i = 0; i < 0x40; ++i) {
      if (seen[i] != rd32(config + 0x1A0 + i * 4)) {
        std::memset(config + 0x1A0, 0, 0x100);
        break;
      }
    }
    memcard_update(t.memcard);
    for (int i = 0; i < 0x40; ++i) seen[i] = rd32(config + 0x1A0 + i * 4);
    psyq::DrawSync(0);
    psyq::VSync(2);
    screen_put_env();
    screen_draw_ot(grid->ctx->ot);
    screen_draw_ot(ctx->ot);
    if (display_delay != 0 && --display_delay == 0) psyq::SetDispMask(1);
  }
}

}  // namespace ss::game
