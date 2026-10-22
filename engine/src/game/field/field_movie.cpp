#include "game/field/field_movie.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

#include "game/boot/loader.h"
#include "game/boot/sound.h"
#include "game/field/actor_base.h"
#include "game/field/actors.h"
#include "game/field/bg.h"
#include "game/field/encounter.h"
#include "game/field/field.h"
#include "game/field/load_queue.h"
#include "game/field/message.h"
#include "game/field/scene_renderer.h"
#include "game/field/screen.h"
#include "game/field/script_vm.h"
#include "game/field/tasks.h"
#include "game/field/worldmap.h"
#include "game/title/movie.h"
#include "runtime/runtime.h"

namespace ss::game::field {
namespace {

using psyq::RECT;

game::Movie* g_sub = nullptr;  // 0x800752c8
u8 g_stop = 0;                 // 0x800762ff
s16 g_sound_entry = 0;         // 0x8007627a: sound entry B 0x16 preloaded for the next sequence
// 0x800761a4: heap the console keeps free when it measures room for a preload; every
// map load sets 0x14000, and so does the sequence.
s32 g_heap_reserve = 0x14000;

// The scratchpad (0x1f800000) compose fetches CLUTs into; it keeps what the last
// fetch left, as the console's does.
u16 g_scratch_clut[256]{};

s16 rd16s(u32 address) { return static_cast<s16>(read_u16(field_data(address))); }

// Tables in the field image.
int movie_entry(int index) { return rd16s(0x800751EC + index * 2); }     // B 0x50: STR entries
int sequence_entry(int index) { return rd16s(0x8007522C + index * 2); }  // B 0x6f: cue lists #4504-#4519

// The load queue's update until it and the loader are idle (B 0x50 / 0x6f before
// they take the drive).
void wait_for_loads() {
  FieldGlobals& g = globals();
  bool busy;
  do {
    LoadQueue* q = load_queue();
    if (q != nullptr) q->update();
    busy = (q != nullptr && load_queue_busy(q)) || (g.loader != nullptr && g.loader->poll() != 0);
  } while (busy);
}

// 0x800537ac / 0x8005381c (with 0x800538a8): the sequence parks the part of the cue
// list that was preloaded in VRAM (x 0x200, 1024 bytes a row) while it frees the map,
// then copies it back into the new buffer. The console loads the second rectangle even
// when its size is 0 (with whatever the stack holds); the host skips it.
void vram_park(const u8* data, s32 bytes) {
  const int rows = bytes / 0x400, rest = bytes - rows * 0x400;
  if (rows != 0) {
    const RECT a{0x200, 0, 0x200, static_cast<s16>(rows)};
    psyq::LoadImage(&a, data);
  }
  if (rest != 0) {
    std::vector<u8> tail(static_cast<size_t>(rest) + 2, 0);
    std::memcpy(tail.data(), data + rows * 0x400, static_cast<size_t>(rest));
    const RECT b{0x200, static_cast<s16>(rows), static_cast<s16>((rest + 1) / 2), 1};
    psyq::LoadImage(&b, tail.data());
  }
  psyq::DrawSync(0);
}

void vram_unpark(u8* dest, s32 bytes) {
  const int rows = bytes / 0x400, rest = bytes - rows * 0x400;
  if (rows != 0) {
    const RECT a{0x200, 0, 0x200, static_cast<s16>(rows)};
    psyq::StoreImage(&a, dest);
  }
  if (rest != 0) {
    std::vector<u8> tail(static_cast<size_t>(rest) + 2, 0);
    const RECT b{0x200, static_cast<s16>(rows), static_cast<s16>((rest + 1) / 2), 1};
    psyq::StoreImage(&b, tail.data());
    std::memcpy(dest + rows * 0x400, tail.data(), static_cast<size_t>(rest));
  }
  psyq::DrawSync(0);
}

// 0x80056b44: drops an entry from the preload table without freeing its buffer (the
// sequence handed it to prologue_load_sound, which freed it).
void preload_forget(int entry) {
  PreloadSlot* t = preload_table();
  for (int i = 0; i < 12; ++i) {
    if (t[i].kind != 0 && t[i].entry == entry) {
      t[i].data = {};
      t[i].kind = 0;
    }
  }
}

// B 0x16 / 0x70: how much of the cue list to preload. The console takes its free heap
// (0x80013428) and preloads all of the entry when it fits with 0x800761a4 to spare,
// otherwise as much as fits, at most 0x80000 bytes, in whole sectors. Which way it goes
// matters: only a partial preload loads the sequence's music bank (B 0x6f).
// The host keeps no console heap. Its free space is taken as measured in PCSX-Redux
// with a map loaded (map 576 at its B 0x70: 897444 bytes free before the preload) less
// what the preload table holds now (B 0x16's music, preloaded just before).
// TODO(port): the free heap depends on the map's data; measured on one map only.
constexpr s32 kConsoleFieldHeapFree = 897444;
s32 preload_bytes(int entry_bytes) {
  s32 free_bytes = kConsoleFieldHeapFree;
  const PreloadSlot* t = preload_table();
  for (int i = 0; i < 12; ++i)
    if (t[i].kind != 0) free_bytes -= static_cast<s32>(t[i].data.size()) + 0x10;
  if (free_bytes < entry_bytes + g_heap_reserve) return std::min(free_bytes - g_heap_reserve, 0x80000) & ~0x7FF;
  return 0;
}

// ---- compose (0x801e9000) ---------------------------------------------------------

// Host: console time per pixel blended (see field_movie_compose).
constexpr double kComposeVblanksPerPixel = 0.86 / 990.0;

// One textured quad of the field's ordering table blended into the movie frame at
// (movie_x halfwords, frame_y) in VRAM. Only quads whose corners form an axis-aligned
// rectangle count. The original's quirks are kept: unscaled quads need 15-bit textures
// and scaled ones 4-bit; only some blend modes are handled; a depth-1 CLUT is rewritten
// with green and blue taken from the wrong bits; a quad clipped by the screen's left or
// right edge writes all its rows onto its top row.
// Returns the pixels it blended in (0 when the quad is skipped), for the host's timing.
s32 compose_quad(const psyq::POLY_FT4& q, int movie_x, int frame_y) {
  int dx = q.x0 - q.x1;
  if (q.x2 - q.x3 != dx) return 0;
  int dy = q.y0 - q.y2;
  if (q.y1 - q.y3 != dy) return 0;
  // Visible: the centre within half the size of the 320x240 screen's.
  {
    const int cx = (q.x0 + q.x1) / 2;
    const int ax = cx - 0xA0 >= 0 ? cx - 0xA0 : 0xA0 - cx;
    if ((dx >= 0 ? dx + 1 : q.x1 - q.x0 + 1) / 2 + 0xA0 < ax) return 0;
    const int cy = (q.y0 + q.y2) / 2;
    const int ay = cy - 0x78 >= 0 ? cy - 0x78 : 0x78 - cy;
    if ((dy >= 0 ? dy + 1 : q.y2 - q.y0 + 1) / 2 + 0x78 < ay) return 0;
  }
  int du = q.u0 - q.u1;
  int dv = q.v0 - q.v2;
  const int flip = (du < 0) != (dx < 0) ? 1 : 0;  // mirrored: texels run right to left
  dx = std::abs(dx);
  dy = std::abs(dy);
  du = std::abs(du);
  dv = std::abs(dv);

  const int tp = q.tpage;
  const int page_x = (tp << 6) & 0x3C0;
  const int depth = (tp >> 7) & 3;
  const int abr = (tp >> 5) & 3;
  const int page_y = (tp << 4) & 0x100;
  const int clut_x = (q.clut & 0x3F) << 4;
  const int clut_y = (q.clut >> 6) & 0x1FF;
  const bool semi = (q.code & 2) != 0;
  const bool raw = (q.code & 1) != 0;
  const int width = depth == 0 ? (du + 3) >> 2 : depth == 1 ? (du + 1) / 2 : du;  // halfwords a texel row

  // The texture (0x80012fec: width * dv halfwords; a scaled 4-bit quad can read one
  // halfword past it, so the host buffer is padded with zeros).
  const int min_u = std::min(q.u0, q.u1);
  std::vector<u16> tex(static_cast<size_t>(std::max(width * dv, 0)) + 4, 0);
  {
    const RECT r{static_cast<s16>(page_x + ((min_u + flip) >> ((2 - depth) & 31))),
                 static_cast<s16>(std::min(q.v0, q.v2) | page_y), static_cast<s16>(width), static_cast<s16>(dv)};
    psyq::StoreImage(&r, tex.data());
  }
  u16* const clut = g_scratch_clut;
  if (depth != 2) {
    const RECT r{static_cast<s16>(clut_x), static_cast<s16>(clut_y), static_cast<s16>(16 << (depth * 4)), 1};
    psyq::StoreImage(&r, clut);
    // Shaded quads: a depth-0 loop computes the products and stores nothing; other
    // depths rewrite the 256 entries (red modulated, green / blue from the low bits).
    if (!raw && depth != 0) {
      for (int i = 0; i < 256; ++i) {
        const u32 c = clut[i];
        u32 red = (((c << 3) & 0xF0) * q.r0) >> 7;
        if (red >= 0x100) red = 0xFF;
        clut[i] = static_cast<u16>(((red & 0xFFFF) >> 3) | ((c & 0xF7) << 2) | ((c & 0xF7) << 7));
      }
    }
  }

  // The frame under the quad: 24-bit pixels, rows of (3 * dx + 1) / 2 halfwords.
  const int min_x = std::min(q.x0, q.x1);
  const int min_y = std::min(q.y0, q.y2);
  const int row_hw = (dx * 3 + 1) / 2;
  const int stride = row_hw * 2;
  std::vector<u16> frame(static_cast<size_t>(((dx * 3 + 1) * std::max(dy, 0) + 1) / 2) + 4, 0);
  {
    const RECT r{static_cast<s16>(movie_x + (min_x * 3) / 2), static_cast<s16>(min_y + frame_y),
                 static_cast<s16>(row_hw), static_cast<s16>(dy)};
    psyq::StoreImage(&r, frame.data());
    psyq::DrawSync(0);
  }
  u8* const frame_bytes = reinterpret_cast<u8*>(frame.data());
  u8* row = frame_bytes + ((min_x * 3) & 1);  // the first pixel starts mid-halfword
  const int c0 = flip ? dx - 1 : 0, c_end = flip ? -1 : dx, step = flip ? -1 : 1;

  // Calls fn(texel, pixel) for each texel in order, the pixels running in the quad's
  // direction (unscaled quads).
  auto each_texel = [&](auto&& fn) {
    const u16* t = tex.data();
    for (int r = 0; r < dy; ++r, row += stride) {
      u8* d = row + c0 * 3;
      for (int c = c0; c != c_end; c += step, d += 3 * step) fn(*t++, d);
    }
  };
  auto mod = [](u32 v, u8 colour) { return (v * colour) >> 7; };

  if (dx == du && dy == dv) {
    if (depth == 2) {
      if (!semi && raw) {
        each_texel([](u16 c, u8* d) {
          if ((c & 0x7FFF) == 0) return;
          d[0] = static_cast<u8>((c << 3) & 0xF7);
          d[1] = static_cast<u8>((c >> 2) & 0xF7);
          d[2] = static_cast<u8>((c >> 7) & 0xF7);
        });
      } else if (!semi) {
        each_texel([&](u16 c, u8* d) {
          if ((c & 0x7FFF) == 0) return;
          u32 r = mod((c << 3) & 0xF0, q.r0);
          if (r >= 0x100) r = 0xFF;
          d[0] = static_cast<u8>(r & 0xF7);
          u32 g = mod((c >> 2) & 0xF7, q.g0);
          if ((g & 0xFFFF) >= 0x100) g = 0xFF;
          d[1] = static_cast<u8>(g & 0xF7);
          u32 b = mod((c >> 7) & 0xF7, q.b0);
          if ((b & 0xFFFF) >= 0x100) b = 0xFF;
          d[2] = static_cast<u8>(b & 0xF7);
        });
      } else if (raw && abr == 0) {
        each_texel([](u16 c, u8* d) {
          if ((c & 0x7FFF) == 0) return;
          d[0] = static_cast<u8>((d[0] + ((c << 3) & 0xF7)) >> 1);
          d[1] = static_cast<u8>((d[1] + ((c >> 2) & 0xF7)) >> 1);
          d[2] = static_cast<u8>((d[2] + ((c >> 7) & 0xF7)) >> 1);
        });
      } else if (raw && abr == 1) {
        each_texel([](u16 c, u8* d) {
          if ((c & 0x7FFF) == 0) return;
          const u32 r = ((c << 3) & 0xF0) + d[0], g = ((c >> 2) & 0xF7) + d[1], b = ((c >> 7) & 0xF7) + d[2];
          d[0] = static_cast<u8>(r < 0xFF ? r : 0xFF);
          d[1] = static_cast<u8>(g < 0xFF ? g : 0xFF);
          d[2] = static_cast<u8>(b < 0xFF ? b : 0xFF);
        });
      } else if (!raw && abr == 0) {
        each_texel([&](u16 c, u8* d) {
          if ((c & 0x7FFF) == 0) return;
          const u32 r = (d[0] + mod((c << 3) & 0xF0, q.r0)) >> 1;
          d[0] = static_cast<u8>(r >= 0x100 ? 0xFF : r);
          const u32 g = (d[1] + mod((c >> 2) & 0xF7, q.g0)) >> 1;
          d[1] = static_cast<u8>(g >= 0x100 ? 0xFF : g);
          const u32 b = (d[2] + mod((c >> 7) & 0xF7, q.b0)) >> 1;
          d[2] = static_cast<u8>(b >= 0x100 ? 0xFF : b);
        });
      } else if (!raw && abr == 1) {
        each_texel([&](u16 c, u8* d) {
          if ((c & 0x7FFF) == 0) return;
          const u32 r = d[0] + mod((c << 3) & 0xF0, q.r0);
          d[0] = static_cast<u8>(r >= 0x100 ? 0xFF : r);
          const u32 g = d[1] + mod((c >> 2) & 0xF7, q.g0);
          d[1] = static_cast<u8>(g >= 0x100 ? 0xFF : g);
          const u32 b = d[2] + mod((c >> 7) & 0xF7, q.b0);
          d[2] = static_cast<u8>(b >= 0x100 ? 0xFF : b);
        });
      }
      // Other blend modes: the frame goes back unchanged.
    }
  } else if (depth == 0 && (!semi || abr == 2)) {
    // Scaled 4-bit quads, nearest texel. The columns are visited in the quad's direction
    // but each pixel takes the texel of its own column, so mirroring is lost.
    const int nibble0 = min_u & 3;
    for (int r = 0, acc = 0; r < dy; ++r, row += stride, acc += dv) {
      const u16* trow = tex.data() + (acc / dy) * width;
      u8* d = row + c0 * 3;
      for (int c = c0; c != c_end; c += step, d += 3 * step) {
        const int n = (c * du) / dx + nibble0;
        const u16 cc = clut[(trow[n >> 2] >> ((n & 3) * 4)) & 0xF];
        if ((cc & 0x7FFF) == 0) continue;
        if (!semi) {
          d[0] = static_cast<u8>((cc << 3) & 0xF0);
          d[1] = static_cast<u8>((cc >> 2) & 0xF7);
          d[2] = static_cast<u8>((cc >> 7) & 0xF7);
        } else {  // subtractive
          const int rr = d[0] - static_cast<int>((cc << 3) & 0xF7);
          d[0] = static_cast<u8>(rr > 0 ? rr : 0);
          const int gg = d[1] - static_cast<int>((cc >> 2) & 0xF7);
          d[1] = static_cast<u8>(gg > 0 ? gg : 0);
          const int bb = d[2] - static_cast<int>((cc >> 7) & 0xF7);
          d[2] = static_cast<u8>(bb > 0 ? bb : 0);
        }
      }
    }
  }

  // Back to VRAM.
  // ORIGINAL BUG (original_bugs.md F5): only quads starting left of the screen were
  // clipped, and wrongly: every row went to the quad's top row (the row was never added to
  // y), read with the clipped width's stride from the buffer's start, and the right edge
  // stopped at 0x13e; a quad running past the right edge was written whole, past the
  // movie's columns (wrapping to VRAM x 0). The original:
  //   if (static_cast<u32>(min_x) < 0x140) {
  //     LoadImage(RECT{movie_x + (min_x * 3) / 2, frame_y + min_y, row_hw, dy}, frame);
  //   } else {
  //     x = max(min_x, 0); w = dx - (x - min_x); if (x + w >= 0x140) w = 0x13f - x;
  //     for (r = 0; r < dy; ++r)  // rows with min_y + r < 0xe0
  //       LoadImage(RECT{movie_x + (x * 3) / 2, frame_y + min_y, (w * 3 + 1) / 2, 1},
  //                 frame + r * ((w * 3 + 1) / 2) * 2);
  //   }
  // The port writes back each row's visible columns, 0 to 0x13f, at its own row.
  const int x0 = std::max(min_x, 0), x1 = std::min(min_x + dx, 0x140);
  if (x0 == min_x && x1 == min_x + dx) {
    const RECT r{static_cast<s16>(movie_x + (min_x * 3) / 2), static_cast<s16>(frame_y + min_y),
                 static_cast<s16>(row_hw), static_cast<s16>(dy)};
    psyq::LoadImage(&r, frame.data());
    psyq::DrawSync(0);
  } else if (x1 > x0) {
    // Byte offsets of the visible columns in a buffer row (the row starts at the
    // halfword (min_x * 3) / 2, the first pixel ((min_x * 3) & 1) bytes in).
    const int lead = (min_x * 3) & 1;
    const int k0 = (lead + (x0 - min_x) * 3) / 2, k1 = (lead + (x1 - min_x) * 3 - 1) / 2;
    const int hw = k1 - k0 + 1;
    std::vector<u16> line(static_cast<size_t>(hw));
    for (int r = 0; r < dy; ++r) {
      if (static_cast<u32>(min_y + r) >= 0xE0) continue;
      std::memcpy(line.data(), frame_bytes + static_cast<size_t>(r) * stride + static_cast<size_t>(k0) * 2,
                  static_cast<size_t>(hw) * 2);
      const RECT rr{static_cast<s16>(movie_x + (min_x * 3) / 2 + k0), static_cast<s16>(frame_y + min_y + r),
                    static_cast<s16>(hw), 1};
      psyq::LoadImage(&rr, line.data());
      psyq::DrawSync(0);
    }
  }
  return dx * dy;
}

}  // namespace

game::Movie* field_sub_program() { return g_sub; }
s32 field_movie_frame_count() { return movie_frame_count(); }
void field_movie_set_stop(int v) { g_stop = static_cast<u8>(v); }

void field_movie_compose(Screen* s) {
  psyq::DrawSync(0);
  MovieGlobals& mg = movie_globals();
  const int movie_x = mg.x;            // 0x801ecba0
  const int frame_y = mg.slice_rect.y;  // 0x801ecb86: the buffer the finished frame is in
  screen_compact_ot(s);
  // The ordering table from entry 0 in drawing order (the console stops at the last
  // entry's terminator word).
  s32 pixels = 0;
  for (psyq::P_TAG* p = s->current->ot.data(); p != nullptr; p = p->next) {
    if (p->len < 9) continue;
    const auto* q = reinterpret_cast<const psyq::POLY_FT4*>(p);
    if ((q->code & 0xFC) != 0x2C) continue;
    pixels += compose_quad(*q, movie_x, frame_y);
  }
  // Host: the console's time for this, which the field's loop and the movie wait out.
  // Calibrated on map 548 (two sprites, ~1000 pixels: 0.86 vblank) from PCSX-Redux's
  // main-loop passes per vblank while the movie plays.
  movie_console_spend(pixels * kComposeVblanksPerPixel);
}

void field_movie_to_15bit(const RECT& disp, int y) {
  const int w = disp.w, h = disp.h;
  std::vector<u16> buf(static_cast<size_t>(std::max(w * h * 3, 0) + 1) / 2 + 4, 0);
  const RECT src{disp.x, disp.y, static_cast<s16>((w * 3) / 2), disp.h};
  psyq::StoreImage(&src, buf.data());
  psyq::DrawSync(0);
  // In place: 24-bit pixels in, 15-bit out, each channel rounded up by 4 first. Source
  // rows start on halfwords.
  const u8* bytes = reinterpret_cast<const u8*>(buf.data());
  size_t in = 0, out = 0;
  for (int row = 0; row < h; ++row) {
    for (int col = 0; col < w; ++col, in += 3) {
      const u32 r = std::min<u32>(bytes[in] + 4u, 0xFF);
      const u32 g = std::min<u32>(bytes[in + 1] + 4u, 0xFF);
      const u32 b = std::min<u32>(bytes[in + 2] + 4u, 0xFF);
      buf[out++] = static_cast<u16>((r >> 3) | (g >> 3) << 5 | (b >> 3) << 10);
    }
    if (in & 1) ++in;
  }
  const RECT dst{0, static_cast<s16>(y), 0x140, disp.h};
  psyq::LoadImage(&dst, buf.data());
  psyq::DrawSync(0);
}

void field_movie_pip_draw() {
  Screen* s = field_screen();
  BgRenderer* bg = bg_renderer();
  s16 origin[2] = {0, 0};
  if (bg != nullptr) bg_get_view_origin(bg, origin);
  const s16 ox = origin[0], oy = origin[1];
  psyq::P_TAG* entry = s->ot_entry(0x80F);
  // The movie (15-bit, decoded at 0x280,0x110) on a screen in the scene: two textured
  // halves over a darkened quad (subtractive grey 0x40).
  auto* a = s->alloc_prims<psyq::POLY_FT4>();
  auto* b = s->alloc_prims<psyq::POLY_FT4>();
  auto* dim = s->alloc_prims<psyq::POLY_F4>();
  auto* mode = s->alloc_prims<psyq::DR_MODE>();
  if (a == nullptr || b == nullptr || dim == nullptr || mode == nullptr) return;
  auto quad = [&](psyq::POLY_FT4* p, u16 tpage, const u8 uv[8], const s16 xy[8]) {
    psyq::setlen(p, 9);
    p->code = 0x2F;  // semi-transparent, unshaded
    p->tpage = tpage;
    p->u0 = uv[0], p->v0 = uv[1], p->u1 = uv[2], p->v1 = uv[3];
    p->u2 = uv[4], p->v2 = uv[5], p->u3 = uv[6], p->v3 = uv[7];
    p->x0 = static_cast<s16>(xy[0] - ox), p->y0 = static_cast<s16>(xy[1] - oy);
    p->x1 = static_cast<s16>(xy[2] - ox), p->y1 = static_cast<s16>(xy[3] - oy);
    p->x2 = static_cast<s16>(xy[4] - ox), p->y2 = static_cast<s16>(xy[5] - oy);
    p->x3 = static_cast<s16>(xy[6] - ox), p->y3 = static_cast<s16>(xy[7] - oy);
    psyq::addPrim(entry, p);
  };
  static constexpr u8 kUvA[8] = {0x00, 0x10, 0xA0, 0x10, 0x00, 0xF0, 0xA0, 0xF0};
  static constexpr s16 kXyA[8] = {0x42, 0x2C, 0xA0, 0x2C, 0x50, 0xA4, 0xA0, 0xA4};
  static constexpr u8 kUvB[8] = {0x20, 0x10, 0xC0, 0x10, 0x20, 0xF0, 0xC0, 0xF0};
  static constexpr s16 kXyB[8] = {0xA0, 0x2C, 0xFC, 0x2C, 0xA0, 0xA4, 0xEE, 0xA4};
  quad(a, 0x11A, kUvA, kXyA);
  quad(b, 0x11C, kUvB, kXyB);
  psyq::setlen(dim, 5);
  dim->code = 0x2B;
  dim->r0 = dim->g0 = dim->b0 = 0x40;
  dim->x0 = static_cast<s16>(0x44 - ox), dim->y0 = static_cast<s16>(0x2C - oy);
  dim->x1 = static_cast<s16>(0xFC - ox), dim->y1 = static_cast<s16>(0x2C - oy);
  dim->x2 = static_cast<s16>(0x50 - ox), dim->y2 = static_cast<s16>(0xA4 - oy);
  dim->x3 = static_cast<s16>(0xEC - ox), dim->y3 = static_cast<s16>(0xA4 - oy);
  psyq::addPrim(entry, dim);
  // Drawn first: subtractive blending, dithered.
  psyq::setlen(mode, 2);
  mode->code[0] = 0xE1000240;
  mode->code[1] = 0;
  psyq::addPrim(entry, mode);
}

void field_movie_start(ScriptCtx* ctx) {
  FieldGlobals& g = globals();
  vm_fetch_args(ctx, 2);
  if (g_sub != nullptr) {
    vm_set_r(0);
    return;
  }
  wait_for_loads();
  const int h = g_vm_args[1] == 0 ? 0xF0 : 0xE0;
  if (g.sub_overlay_hook == 0) {
    // Over the field: 24-bit at x 0x220 (VRAM halfwords), frames shown as they finish.
    g_sub = movie_create(new game::Movie, 0x220, 0, 0x140, h, 0, 3);
  } else {
    // Into a texture for field_movie_pip_draw: 15-bit at 0x280,0x110, mask bit set.
    g_sub = movie_create(new game::Movie, 0x280, 0x110, 0x140, h, 0, 2);
    g_sub->dct_flags = 2;
    Screen* s = field_screen();
    screen_flip(s);
    psyq::VSync(g.vsync_mode);
    psyq::PutDispEnv(&s->current->disp);
    psyq::PutDrawEnv(&s->current->draw);
  }
  // Host: the field's main loop polls the movie; in PCSX-Redux it runs 28 passes a vblank
  // while the movie waits for the drive (field_update each time, frame ticks 1).
  movie_globals().loop_vblanks = 1.0 / 28.0;
  movie_open(g_sub, movie_entry(g_vm_args[0]));
  runtime::Runtime::get().trace("field_movie_start");
  vm_set_r(1);
}

bool field_movie_main_loop(Field* f) {
  FieldGlobals& g = globals();
  Screen* s = field_screen();
  MovieGlobals& mg = movie_globals();
  if (g_stop == 0 && movie_update(g_sub) == 1) {
    if (movie_frame_ready()) {
      if (g.sub_overlay_hook == 0) {
        // The field draws into its ordering table, which is never sent to the GPU:
        // its sprites go into the frame, which then goes on screen.
        screen_flip(s);
        screen_clear_ot(s);
        task_run_all();
        field_movie_compose(s);
        movie_present(g_sub);
      } else {
        screen_flip(s);
        task_run_all();
        field_movie_pip_draw();
        mg.frame_ready = 0;
        mg.decoding = 0;
        screen_compact_ot(s);
        psyq::DrawOTag(s->current->ot.data());
        psyq::DrawSync(0);
        psyq::PutDispEnv(&s->current->disp);
        psyq::PutDrawEnv(&s->current->draw);
      }
    }
    g.frame_ticks = g.vsync_mode != 0 ? g.vsync_mode : 1;
    return false;
  }
  if (g.sub_overlay_hook != 0) {
    delete g_sub;
    g_sub = nullptr;
    return true;
  }
  // The end of a movie over the field: its last frame, in 15-bit, into the buffer the
  // field draws next, which then goes on screen.
  field_movie_to_15bit(g_sub->disp[mg.draw_buffer].disp, s->current != &s->buffers[0] ? 0xE0 : 0);
  psyq::DrawSync(0);
  screen_clear_ot(s);
  screen_flip(s);
  psyq::VSync(g.vsync_mode);
  psyq::PutDrawEnv(&s->current->draw);
  psyq::PutDispEnv(&s->current->disp);
  f->present = 0;
  delete g_sub;
  g_sub = nullptr;
  runtime::Runtime::get().trace("field_movie_end");
  return false;
}

void field_movie_preload(ScriptCtx* ctx, u32 instr) {
  FieldGlobals& g = globals();
  preload_release(-1);
  if (((instr >> 16) & 0x7F) == 0x16) {
    vm_fetch_args(ctx, 2);
    if (g_vm_args[1] < 100) g_vm_args[1] = rd16s(0x800750CC + g_vm_args[1] * 2);  // k_music_ids
    vm_preload_entry(ctx, g_vm_args[1], 1, 0);
    g_sound_entry = static_cast<s16>(g_vm_args[1]);
  } else {
    vm_fetch_args(ctx, 1);
    g_sound_entry = 0;
  }
  const int entry = sequence_entry(g_vm_args[0]);
  vm_preload_entry(ctx, entry, 0, preload_bytes(g.loader->entry_size(entry)));
}

int field_movie_sequence(ScriptCtx* ctx) {
  FieldGlobals& g = globals();
  Field* f = field_program();
  wait_for_loads();
  const s32* a = g_vm_args;
  const int entry = sequence_entry(a[0]);
  const s16 old_map = static_cast<s16>(read_u16(g.game_state + 0x1A));
  // Sequences 4 and 8-10 keep the map; the others free it (and the caller reloads).
  bool unloaded = false;
  if (a[0] != 4 && !(a[0] >= 8 && a[0] <= 10)) {
    field_unload_map(f, true);
    field_free_ot_layers();
    bg_destroy();             // 0x8007528c: vtable +0x14
    world_destroy_objects();  // 0x800752c4 (null on field maps)
    unloaded = true;
  }
  actor_delete_slot(actor_system(), -1);
  if (MsgSystem* msg = msg_system()) msg_window_destroy(msg, -1);
  // screen_free_arena (0x800481fc): nothing to free on the host.
  // VRAM x 0x180-0x1c0, y 0x100-0x200 (map textures under the 24-bit movie) to x 0x1e0.
  {
    RECT r{0x180, 0x100, 0x20, 0x100};
    psyq::MoveImage(&r, 0x1E0, 0);
    r.x = 0x1A0;
    psyq::MoveImage(&r, 0x1E0, 0x100);
    psyq::DrawSync(0);
  }
  std::vector<u8>* sound = nullptr;
  s32 bytes = 0;
  if (g_sound_entry != 0) sound = preload_find(g_sound_entry, nullptr, &bytes);
  bytes = 0;
  std::vector<u8>* pre = preload_find(entry, nullptr, &bytes);
  std::vector<u8> own;  // the cue list, when only part of it was preloaded
  const u8* script = pre != nullptr ? pre->data() : nullptr;
  game::Prologue* p = nullptr;
  if (bytes != 0) {
    // Part of it was preloaded: park that in VRAM, free the slot, bring in the sound,
    // then a whole buffer from the parked part and the rest from the disc.
    vram_park(pre->data(), bytes);
    preload_remove(entry);
    if (g_sound_entry != 0) {
      p = prologue_create(new game::Prologue);
      g_sub = p;
      if (sound != nullptr) {
        prologue_load_sound(p, *sound);
        preload_forget(g_sound_entry);
      }
    }
    const int size = g.loader->entry_size(entry);
    own.assign(static_cast<size_t>(size), 0);
    vram_unpark(own.data(), bytes);
    // 0x80011988 (seek to sector bytes / 2048 of the entry), loader_start_read.
    std::vector<u8> whole(static_cast<size_t>(size), 0);
    g.loader->load_sync(whole.data(), entry, 0);
    if (size > bytes) std::copy(whole.begin() + bytes, whole.end(), own.begin() + bytes);
    script = own.data();
  } else if (pre == nullptr) {
    // The console would play from a null cue list; scripts always preload first.
    std::fprintf(stderr, "[field_movie] sequence %d: cue list %d was not preloaded; reading it\n", a[0], entry);
    own.assign(static_cast<size_t>(g.loader->entry_size(entry)), 0);
    g.loader->load_sync(own.data(), entry, 0);
    script = own.data();
  }
  if (g_sound_entry == 0 || g_sub == nullptr) {
    p = prologue_create(new game::Prologue);
    g_sub = p;
  }
  p->script = script;
  p->cue = script;
  p->keep_music = static_cast<u8>(a[7]);
  u8* gs = g.game_state;
  const s16 map = static_cast<s16>(a[1]);
  std::memcpy(gs + 0x1A, &map, 2);
  std::memcpy(gs + 0x08, &a[2], 4);
  std::memcpy(gs + 0x0C, &a[3], 4);
  std::memcpy(gs + 0x10, &a[4], 4);
  const s16 facing = static_cast<s16>((a[5] * 0x1000) / 360);
  std::memcpy(gs + 0x18, &facing, 2);
  gs[0x21] = static_cast<u8>(a[6]);
  runtime::Runtime::get().trace("field_movie_sequence");
  while (prologue_update(p, 0) == 0) movie_present(p);
  delete g_sub;
  g_sub = nullptr;
  if (bytes == 0) preload_release(-1);
  own = {};

  Screen* s = field_screen();
  screen_alloc_arena(s);
  {
    RECT r{0x1E0, 0, 0x20, 0x100};
    psyq::MoveImage(&r, 0x180, 0x100);
    r.y = 0x100;
    psyq::MoveImage(&r, 0x1A0, 0x100);
    psyq::DrawSync(0);
  }
  field_setup_screen(f);
  psyq::PutDispEnv(&s->current->disp);
  psyq::PutDrawEnv(&s->current->draw);
  screen_flip(s);
  screen_clear_ot(s);
  if (a[1] != old_map || unloaded) return 1;
  // Same map, still loaded: the leader comes back and the map's script restarts.
  int angle = static_cast<s16>(read_u16(gs + 0x18)) * 360;
  if (angle < 0) angle += 0xFFF;
  angle >>= 12;
  const int leader = ((g.script_flags[0] >> 1) & 1) ^ 1;
  const s32 pos[3] = {static_cast<s32>(read_u32(gs + 8)), static_cast<s32>(read_u32(gs + 0xC)),
                      static_cast<s32>(read_u32(gs + 0x10))};
  actor_create(actor_system(), leader, leader, pos, angle, gs[0x21], 0xFFFF, 0xFFFF, 0x1000F);
  actor_or_flags(field_actors()[leader], 0, 0x200004);
  g.field_flags[0x26] = 0;
  const u16 slot = gs[0x24];
  std::memcpy(g.field_flags + 0x10, &slot, 2);
  std::memcpy(g.field_flags + 0x0E, &slot, 2);
  vm_init(g_script_vm);
  ctx->restart = 1;
  actor_sys_reset_sprite_cache(actor_system());
  while (sound_stream_busy(g.sound) != 0) {
  }
  psyq::VSync(4);
  g_heap_reserve = 0x14000;
  return 0;
}

}  // namespace ss::game::field
