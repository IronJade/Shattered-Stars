// The tracker maps (port feature; docs/ap/tracker_maps.md). A field map's background is
// composed in software from its container, the way the background renderer
// (game/field/bg.cpp, overlay 0xc1d bg_draw) draws it, but over the whole background at
// once instead of a 320x224 window, into a VRAM of its own: no game state is touched.

#include "tools/tracker_maps.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <optional>
#include <span>
#include <system_error>

#if defined(_WIN32)
#include <SDL3/SDL_filesystem.h>
#endif

#include "disc/slz.h"
#include "game/ap/json.h"
#include "runtime/debug_overlay.h"
#include "util/deflate.h"
#include "util/utf8.h"

namespace ss::tools {
namespace {

using game::ap::Json;
namespace fs = std::filesystem;

constexpr char kPackUid[] = "so2_shattered_stars";
constexpr int kMapEntryBase = 0xC87;  // a field map's container is archive entry 0xc87 + map

s32 rd32(const u8* p) {
  s32 v;
  std::memcpy(&v, p, 4);
  return v;
}
s16 rd16(const u8* p) {
  s16 v;
  std::memcpy(&v, p, 2);
  return v;
}
u16 rdu16(const u8* p) {
  u16 v;
  std::memcpy(&v, p, 2);
  return v;
}

// ---- The map's VRAM: section 0's texture pages and CLUT rows --------------------------------

struct MapImage {
  std::vector<u8> scene;            // section 0 block 0 (MapScene)
  std::vector<u16> vram = std::vector<u16>(1024 * 512, 0);
};

bool load_map_image(disc::Archive& archive, int map, MapImage* out, std::string* error) {
  const int entry = kMapEntryBase + map;
  if (!archive.has_entry(static_cast<u32>(entry))) {
    *error = "map " + std::to_string(map) + ": no container on this disc";
    return false;
  }
  auto data = archive.read_entry(static_cast<u32>(entry));
  if (!data || data->size() < 12) {
    *error = "map " + std::to_string(map) + ": container not readable";
    return false;
  }
  const u8* c = data->data();
  const s32 count = rd32(c);
  size_t section0 = 0;
  bool found = false;
  for (s32 i = 0; i < count && 12 + 8 * static_cast<size_t>(i) <= data->size(); ++i) {
    if (rd32(c + 4 + i * 8) == 0) {
      section0 = static_cast<size_t>(rd32(c + 8 + i * 8));
      found = true;
    }
  }
  if (!found || section0 >= data->size()) {
    *error = "map " + std::to_string(map) + ": no background section";
    return false;
  }
  const std::span<const u8> chain(c + section0, data->size() - section0);
  auto block = [&](int n) { return disc::slz::unpack_block(chain, 0, n, nullptr); };
  auto scene = block(0);
  auto dir = block(1);
  if (!scene || scene->size() < 0x12c || !dir || dir->size() < 0x10) {
    *error = "map " + std::to_string(map) + ": background section not readable";
    return false;
  }
  out->scene = std::move(*scene);
  // Texture directory: tex_count, clut_count, then {u8 pos; u8 depth; u8 pad[6]} per page:
  // VRAM x = 0x200 + 64 (pos & 7), y = 256 (pos >> 3); 256 x 256 texels at that depth.
  const s32 textures = rd32(dir->data()), cluts = rd32(dir->data() + 4);
  int n = 2;
  for (s32 t = 0; t < textures; ++t, ++n) {
    if (0x10 + 8 * static_cast<size_t>(t) + 2 > dir->size()) break;
    const u8 pos = (*dir)[0x10 + t * 8], depth = (*dir)[0x11 + t * 8] & 3;
    auto page = block(n);
    if (!page) continue;
    const int x0 = 0x200 + ((pos & 7) << 6), y0 = (pos >> 3) << 8, words = 64 << depth;
    for (int y = 0; y < 256; ++y)
      for (int x = 0; x < words; ++x) {
        const size_t at = (static_cast<size_t>(y) * words + x) * 2;
        if (at + 1 >= page->size()) continue;
        out->vram[((y0 + y) & 511) * 1024 + ((x0 + x) & 1023)] = rdu16(page->data() + at);
      }
  }
  // CLUT rows 0x1e0 + i: mode 0 makes colour 0 of each 16-colour bank transparent, mode 1
  // only colour 0 (field_load_map_sections).
  if (cluts > 0) {
    if (auto table = block(n)) {
      for (s32 i = 0; i < cluts && (static_cast<size_t>(i) + 1) * 0x204 <= table->size(); ++i) {
        u8* e = table->data() + i * 0x204;
        if (e[0] == 0) {
          for (int bank = 0; bank < 16; ++bank) std::memset(e + 4 + bank * 0x20, 0, 2);
        } else if (e[0] == 1) {
          std::memset(e + 4, 0, 2);
        }
        for (int k = 0; k < 256; ++k) out->vram[((0x1E0 + i) & 511) * 1024 + k] = rdu16(e + 4 + k * 2);
      }
    }
  }
  return true;
}

// ---- Primitives, as bg_draw builds them -----------------------------------------------------

struct Prim {
  int ot = 0, seq = 0;
  int left = 0, top = 0, right = 0, bottom = 0;   // destination, right / bottom exclusive
  int u_left = 0, u_right = 0, v_top = 0, v_bottom = 0;  // texture coordinates at the edges
  bool sprite = true;                              // SPRT: one texel per pixel from (u_left, v_top)
  int tex_x = 0, tex_y = 0, depth = 0;             // texture page
  int clut_x = 0, clut_y = 0;
  bool semi = false;
  int abr = 0;
  u8 rgb[3] = {0x80, 0x80, 0x80};
};

// The texture page slot's VRAM position and depth (MapScene +0xec; bg_build_tpages).
void page_of(const u8* scene, int page, Prim* p) {
  const u8* e = scene + 0xEC + (page & 15) * 4;
  if (e[0] == 0) {  // an unused slot: the renderer's tpage word stays 0
    p->tex_x = p->tex_y = p->depth = 0;
    return;
  }
  p->tex_x = 0x200 + (page & 7) * 64;
  p->tex_y = (page >> 3 & 1) * 256;
  p->depth = e[1] & 3;
}

u16 texel(const std::vector<u16>& vram, const Prim& p, int u, int v) {
  u &= 0xFF;
  v &= 0xFF;
  const int row = ((p.tex_y + v) & 511) * 1024;
  switch (p.depth) {
    case 0: {
      const u16 w = vram[row + ((p.tex_x + (u >> 2)) & 1023)];
      const int index = w >> ((u & 3) * 4) & 0xF;
      return vram[(p.clut_y & 511) * 1024 + ((p.clut_x + index) & 1023)];
    }
    case 1: {
      const u16 w = vram[row + ((p.tex_x + (u >> 1)) & 1023)];
      const int index = w >> ((u & 1) * 8) & 0xFF;
      return vram[(p.clut_y & 511) * 1024 + ((p.clut_x + index) & 1023)];
    }
    default:
      return vram[row + ((p.tex_x + u) & 1023)];
  }
}

// One pixel the GPU's way: transparent texel 0, modulation (texel * colour) >> 7 per 5-bit
// channel, semi-transparency only for texels with bit 15 set.
void plot(std::vector<u16>& canvas, int width, int x, int y, u16 t, const Prim& p) {
  if (t == 0) return;
  int c[3] = {t & 31, t >> 5 & 31, t >> 10 & 31};
  for (int i = 0; i < 3; ++i) c[i] = std::min(31, (c[i] * p.rgb[i]) >> 7);
  u16& dst = canvas[static_cast<size_t>(y) * width + x];
  if (p.semi && (t & 0x8000)) {
    const int b[3] = {dst & 31, dst >> 5 & 31, dst >> 10 & 31};
    for (int i = 0; i < 3; ++i) {
      switch (p.abr) {
        case 0: c[i] = (b[i] + c[i]) >> 1; break;
        case 1: c[i] = std::min(31, b[i] + c[i]); break;
        case 2: c[i] = std::max(0, b[i] - c[i]); break;
        default: c[i] = std::min(31, b[i] + (c[i] >> 2)); break;
      }
    }
  }
  dst = static_cast<u16>(c[0] | c[1] << 5 | c[2] << 10 | (t & 0x8000));
}

// Draws 'p' clipped to [x0, x1) x [y0, y1) of the canvas (as the GPU's drawing area).
void draw(std::vector<u16>& canvas, int width, const std::vector<u16>& vram, const Prim& p, int x0, int y0, int x1,
          int y1) {
  const int w = p.right - p.left, h = p.bottom - p.top;
  if (w <= 0 || h <= 0) return;
  for (int y = std::max(y0, p.top); y < std::min(y1, p.bottom); ++y) {
    const int v = p.sprite ? p.v_top + (y - p.top) : p.v_top + (y - p.top) * (p.v_bottom - p.v_top) / h;
    for (int x = std::max(x0, p.left); x < std::min(x1, p.right); ++x) {
      const int u = p.sprite ? p.u_left + (x - p.left) : p.u_left + (x - p.left) * (p.u_right - p.u_left) / w;
      plot(canvas, width, x, y, texel(vram, p, u, v), p);
    }
  }
}

// Scene sub-blocks (docs/notes/field_part3.md 3.1.1; game/field/bg.cpp).
struct Tile {
  u8 u, v, w, h, flags, clut_x, clut_y, prim;
};
Tile tile_at(const std::vector<u8>& scene, int index) {
  Tile t{};
  const size_t at = static_cast<size_t>(rd32(scene.data() + 0x78)) + static_cast<size_t>(index) * 8;
  if (index >= 0 && at + 8 <= scene.size()) std::memcpy(&t, scene.data() + at, 8);
  return t;
}

struct Sprite {
  s16 x, y;
  u16 tile;
  u8 page, layer;
  s16 anim, colour, x_min, x_max, y_min, y_max;
};
Sprite sprite_at(const u8* p) {
  return Sprite{rd16(p), rd16(p + 2), rdu16(p + 4), p[6], p[7], rd16(p + 8), rd16(p + 10),
                rd16(p + 12), rd16(p + 14), rd16(p + 16), rd16(p + 18)};
}

// The fixed state: every animation record at its first frame, every colour record at its
// first step (what step_animations / step_colours give at timer 0), frozen records as
// they are on disc.
void first_frames(std::vector<u8>& scene) {
  u8* s = scene.data();
  const int anims = rdu16(s + 0x64), colours = rdu16(s + 0x72);
  for (int r = 0; r < anims; ++r) {
    const size_t at = static_cast<size_t>(rd32(s + 0x88)) + r * 0xA0;
    if (at + 0xA0 > scene.size()) break;
    u8* a = s + at;
    if (a[5] & 0x80) continue;
    const u8* f = a + 0x10;
    std::memcpy(a + 2, f, 2);
    a[6] = f[7];
    a[7] = 0;
    std::memcpy(a + 8, f + 4, 2);
    a[0xA] = f[6];
    std::memcpy(a + 0xC, f + 8, 4);
  }
  for (int r = 0; r < colours; ++r) {
    const size_t at = static_cast<size_t>(rd32(s + 0x8C)) + r * 0x14C;
    if (at + 0x14C > scene.size()) break;
    u8* c = s + at;
    std::memcpy(c + 0x148, c + 0x8A, 2);
    c[0x14A] = c[0x8C];
  }
}

void sprite_look(const std::vector<u8>& scene, const Sprite& s, u8 rgb[3], u16* tile, u8* page) {
  *tile = s.tile;
  *page = s.page;
  rgb[0] = rgb[1] = rgb[2] = 0x80;
  if (s.anim != -1) {
    const u8* a = scene.data() + rd32(scene.data() + 0x88) + s.anim * 0xA0;
    rgb[0] = a[8];
    rgb[1] = a[9];
    rgb[2] = a[0xA];
    *tile = rdu16(a + 2);
    *page = static_cast<u8>(rdu16(a + 6));
  } else if (s.colour != -1) {
    const u8* c = scene.data() + rd32(scene.data() + 0x8C) + s.colour * 0x14C;
    rgb[0] = c[0x148];
    rgb[1] = c[0x149];
    rgb[2] = c[0x14A];
  }
}

// A BG tile at (x, y): a SPRT, or a POLY_FT4 when the tile is flipped or scaled (bg_draw).
Prim tile_prim(const std::vector<u8>& scene, const Sprite& s, const Tile& t, int x, int y, const u8 rgb[3], u8 page) {
  Prim p;
  std::memcpy(p.rgb, rgb, 3);
  page_of(scene.data(), page, &p);
  p.clut_x = (t.clut_x & 0x3F) * 16;
  p.clut_y = 0x1E0 + t.clut_y;
  if (t.prim == 0x64 && (t.flags & 0x18) == 0) {
    p.sprite = true;
    p.left = x, p.top = y, p.right = x + t.w, p.bottom = y + t.h;
    p.u_left = t.u, p.v_top = t.v;
    p.semi = (t.flags & 1) != 0;
    p.abr = (t.flags & 6) >> 1;
    return p;
  }
  p.sprite = false;
  p.semi = (tile_at(scene, s.tile).flags & 1) != 0;  // the code takes the record's own tile, as bg_draw does
  p.abr = (t.flags & 1) ? (t.flags & 6) >> 1 : 0;
  const int w = (t.w * t.prim) / 100, h = (t.h * t.prim) / 100;
  const bool fx = (t.flags & 8) != 0, fy = (t.flags & 0x10) != 0;
  // set_ft4_uvs: one texel back when flipped, kept inside the page.
  int u = t.u, v = t.v, tw = t.w, th = t.h;
  if (fx && --u < 0) u = 0, --tw;
  if (fy && --v < 0) v = 0, --th;
  if (u + tw > 0xFF) --tw;
  if (v + th > 0xFF) --th;
  if (fx) {
    p.left = x - 1, p.right = x + w - 1, p.u_left = u + tw, p.u_right = u;
  } else {
    p.left = x, p.right = x + w, p.u_left = u, p.u_right = u + tw;
  }
  if (fy) {
    p.top = y, p.bottom = y + h, p.v_top = v + th, p.v_bottom = v;  // y flip swaps the rows only
  } else {
    p.top = y, p.bottom = y + h, p.v_top = v, p.v_bottom = v + th;
  }
  return p;
}

}  // namespace

namespace {

// The primitives bg_draw queues with the background scrolled to (scroll_x, scroll_y), in
// background coordinates. 'any_scroll': plain sprites are kept when their window holds any
// scroll position of the map instead of this one (their position never depends on it).
void collect_prims(const std::vector<u8>& scene, int scroll_x, int scroll_y, bool any_scroll, std::vector<Prim>* prims) {
  const u8* s = scene.data();
  const int width = rdu16(s + 4), height = rdu16(s + 6);
  const int scroll_x_max = width > 0x140 ? width - 0x140 : 0, scroll_y_max = height > 0xE0 ? height - 0xE0 : 0;
  auto window_open = [&](int lo, int hi, int max) { return std::max(0, lo + 1) <= std::min(max, hi - 1); };
  int seq = 0;
  const u8* records = s + rd32(s + 0x7C);
  int index = 0;
  const int plain = rdu16(s + 0x5C);
  for (int n = 0; n < plain; ++n, ++index) {
    const Sprite sp = sprite_at(records + index * 20);
    // Drawn only while the scroll position is inside its window.
    const bool shown = any_scroll ? window_open(sp.x_min, sp.x_max, scroll_x_max) && window_open(sp.y_min, sp.y_max, scroll_y_max)
                                  : sp.x_min < scroll_x && scroll_x < sp.x_max && sp.y_min < scroll_y && scroll_y < sp.y_max;
    if (!shown) continue;
    u8 rgb[3], page;
    u16 tile_index;
    sprite_look(scene, sp, rgb, &tile_index, &page);
    int x = sp.x, y = sp.y;
    if (sp.anim != -1) {
      const u8* a = s + rd32(s + 0x88) + sp.anim * 0xA0;
      x += rd16(a + 0xC);
      y += rd16(a + 0xE);
    }
    Prim p = tile_prim(scene, sp, tile_at(scene, tile_index), x, y, rgb, page);
    p.ot = sp.layer + 0x11;
    p.seq = seq++;
    prims->push_back(p);
  }
  // Parallax sprites: each layer (+0x74 records) moves by its own share of the scroll.
  const int layers = rdu16(s + 0x74), parallax = rdu16(s + 0x5E);
  if (layers == 0) return;
  const u8* layer_recs = s + rd32(s + 0x90);
  const int cx = (rdu16(s + 0x34) - 0x140) / 2, cy = (rdu16(s + 0x36) - 0xF0) / 2;
  for (int n = 0; n < parallax; ++n, ++index) {
    const Sprite sp = sprite_at(records + index * 20);
    int px = 0, py = 0;
    for (int l = 0; l < layers; ++l) {
      const u8* L = layer_recs + l * 0x10;
      if (L[0] == sp.layer) {
        px = cx - (rd16(L + 4) + ((cx - scroll_x) * rd16(L + 8)) / 100);
        py = cy - (rd16(L + 6) + ((cy - scroll_y) * rd16(L + 0xA)) / 100);
        break;
      }
    }
    if (!(sp.x_min < px && px < sp.x_max && sp.y_min < py && py < sp.y_max)) continue;
    u8 rgb[3], page;
    u16 tile_index;
    sprite_look(scene, sp, rgb, &tile_index, &page);
    const Tile t = tile_at(scene, tile_index);
    int x = sp.x - px + scroll_x, y = sp.y - py + scroll_y;
    if (sp.anim != -1) {
      const u8* a = s + rd32(s + 0x88) + sp.anim * 0xA0;
      x += rd16(a + 0xC);
      y += rd16(a + 0xE);
    }
    Prim p;  // always a SPRT (make_sprite)
    std::memcpy(p.rgb, rgb, 3);
    page_of(s, page, &p);
    p.clut_x = (t.clut_x & 0x3F) * 16;
    p.clut_y = 0x1E0 + t.clut_y;
    p.left = x, p.top = y, p.right = x + t.w, p.bottom = y + t.h;
    p.u_left = t.u, p.v_top = t.v;
    p.semi = (t.flags & 1) != 0;
    p.abr = (t.flags & 6) >> 1;
    p.ot = sp.layer + 0x11;
    p.seq = seq++;
    prims->push_back(p);
  }
  // Wrap-around scrolling windows (+0x76; water and the like) are not drawn: none of the
  // layout's maps has one.
}

// The ordering table: higher entries on top; inside one entry the last one added first.
void draw_prims(std::vector<Prim>& prims, std::vector<u16>& canvas, int width, const std::vector<u16>& vram, int x0,
                int y0, int x1, int y1) {
  std::stable_sort(prims.begin(), prims.end(), [](const Prim& a, const Prim& b) {
    return a.ot != b.ot ? a.ot < b.ot : a.seq > b.seq;
  });
  for (const Prim& p : prims) draw(canvas, width, vram, p, x0, y0, x1, y1);
}

}  // namespace

bool render_field_background(disc::Archive& archive, int map, Background* out, std::string* error) {
  MapImage image;
  if (!load_map_image(archive, map, &image, error)) return false;
  std::vector<u8>& scene = image.scene;
  first_frames(scene);
  const u8* s = scene.data();
  const int width = rdu16(s + 4), height = rdu16(s + 6);
  if (width <= 0 || height <= 0 || width > 4096 || height > 4096) {
    *error = "map " + std::to_string(map) + ": no background";
    return false;
  }
  // The screen's clear colour (MapScene +0x6f) under everything.
  const u16 clear = static_cast<u16>((s[0x6F] >> 3) | (s[0x70] >> 3) << 5 | (s[0x71] >> 3) << 10);
  std::vector<u16> canvas(static_cast<size_t>(width) * height, clear);
  if (rdu16(s + 0x74) == 0) {
    // Nothing moves with the scroll: the whole background at once.
    std::vector<Prim> prims;
    collect_prims(scene, 0, 0, true, &prims);
    draw_prims(prims, canvas, width, image.vram, 0, 0, width, height);
  } else {
    // Parallax layers have no single place: the background is put together from screens
    // (320 x 224, as bg_update_scroll scrolls), each drawn at its own scroll position.
    const int scroll_x_max = width > 0x140 ? width - 0x140 : 0, scroll_y_max = height > 0xE0 ? height - 0xE0 : 0;
    for (int ty = 0; ty * 0xE0 < height; ++ty)
      for (int tx = 0; tx * 0x140 < width; ++tx) {
        const int sx = std::min(tx * 0x140, scroll_x_max), sy = std::min(ty * 0xE0, scroll_y_max);
        std::vector<Prim> prims;
        collect_prims(scene, sx, sy, false, &prims);
        const int x1 = std::min(width, sx + 0x140), y1 = std::min(height, sy + 0xE0);
        for (int y = sy; y < y1; ++y) std::fill(canvas.begin() + y * width + sx, canvas.begin() + y * width + x1, clear);
        draw_prims(prims, canvas, width, image.vram, sx, sy, x1, y1);
      }
  }
  out->width = width;
  out->height = height;
  out->pixels.resize(canvas.size());
  for (size_t i = 0; i < canvas.size(); ++i) {
    const u16 c = canvas[i];
    const u32 r = c & 31, g = c >> 5 & 31, b = c >> 10 & 31;
    out->pixels[i] = (r << 3 | r >> 2) | (g << 3 | g >> 2) << 8 | (b << 3 | b >> 2) << 16 | 0xFF000000u;
  }
  return true;
}

namespace {

// ---- Image composition --------------------------------------------------------------------

struct Canvas {
  int width = 0, height = 0;
  std::vector<u32> pixels;
  void fill_rect(int x, int y, int w, int h, u32 c) {
    for (int j = std::max(0, y); j < std::min(height, y + h); ++j)
      for (int i = std::max(0, x); i < std::min(width, x + w); ++i) pixels[static_cast<size_t>(j) * width + i] = c;
  }
  void put(int x, int y, u32 c) {
    if (x >= 0 && y >= 0 && x < width && y < height) pixels[static_cast<size_t>(y) * width + x] = c;
  }
};

u32 colour_of(const std::string& hex, u32 fallback) {
  if (hex.size() != 7 || hex[0] != '#') return fallback;
  const unsigned long v = std::strtoul(hex.c_str() + 1, nullptr, 16);
  return static_cast<u32>((v >> 16 & 0xFF) | (v & 0xFF00) | (v & 0xFF) << 16) | 0xFF000000u;
}

// Box filter: each destination pixel averages the source area it covers (exact integer
// weights, so the result never depends on floating point).
void blit_scaled(Canvas& dst, int x0, int y0, int w, int h, const Background& src) {
  const int sw = src.width, sh = src.height;
  for (int dy = 0; dy < h; ++dy) {
    const s64 ya = static_cast<s64>(dy) * sh, yb = static_cast<s64>(dy + 1) * sh;  // in 1/h of a source row
    for (int dx = 0; dx < w; ++dx) {
      const s64 xa = static_cast<s64>(dx) * sw, xb = static_cast<s64>(dx + 1) * sw;
      u64 sum[3] = {0, 0, 0}, total = 0;
      for (s64 sy = ya / h; sy * h < yb && sy < sh; ++sy) {
        const s64 wy = std::min(yb, (sy + 1) * h) - std::max(ya, sy * h);
        for (s64 sx = xa / w; sx * w < xb && sx < sw; ++sx) {
          const s64 wx = std::min(xb, (sx + 1) * w) - std::max(xa, sx * w);
          const u64 wt = static_cast<u64>(wx * wy);
          const u32 p = src.pixels[static_cast<size_t>(sy) * sw + sx];
          sum[0] += wt * (p & 0xFF);
          sum[1] += wt * (p >> 8 & 0xFF);
          sum[2] += wt * (p >> 16 & 0xFF);
          total += wt;
        }
      }
      if (total == 0) continue;
      const u32 r = static_cast<u32>((sum[0] + total / 2) / total), g = static_cast<u32>((sum[1] + total / 2) / total),
                b = static_cast<u32>((sum[2] + total / 2) / total);
      dst.put(x0 + dx, y0 + dy, r | g << 8 | b << 16 | 0xFF000000u);
    }
  }
}

// A label in the overlay's 5x7 font at twice its size (12 px per character), cut to 'width'.
void draw_label(Canvas& c, int x, int y, int width, const std::string& text, u32 colour) {
  constexpr int kScale = 2, kAdvance = 6 * kScale;
  const int fit = std::max(0, (width + kScale) / kAdvance);
  std::string s;
  for (unsigned char ch : text) s.push_back(ch >= 0x20 && ch < 0x7F ? static_cast<char>(ch) : '?');
  if (static_cast<int>(s.size()) > fit) s = fit >= 2 ? s.substr(0, fit - 2) + ".." : s.substr(0, fit);
  for (size_t i = 0; i < s.size(); ++i)
    for (int col = 0; col < 5; ++col) {
      const u8 bits = runtime::overlay_font_column(static_cast<unsigned char>(s[i]), col);
      for (int row = 0; row < 7; ++row)
        if (bits >> row & 1) c.fill_rect(x + static_cast<int>(i) * kAdvance + col * kScale, y + row * kScale, kScale, kScale, colour);
    }
}

void draw_dot(Canvas& c, int x, int y, int size, u32 fill, u32 outline) {
  const int r = std::max(2, size / 2);
  for (int dy = -r; dy <= r; ++dy)
    for (int dx = -r; dx <= r; ++dx) {
      const int d = dx * dx + dy * dy;
      if (d <= (r - 2) * (r - 2)) c.put(x + dx, y + dy, fill);
      else if (d <= r * r) c.put(x + dx, y + dy, outline);
    }
}

// ---- World maps, from above ----------------------------------------------------------------
// The terrain blocks (docs/notes/field_overlay_c1e.md 3; game/field/worldmap_render.cpp
// world_draw_block): 9 x 7 blocks of 0x3000 units, archive entry base + bx + 9 bz, each 16
// sub-cells of textured polygons whose vertices are relative to the block's centre. Drawn
// flat (x, z only), lowest first, with the textures and CLUTs of the world's chunk
// container (section 3: every chunk of a world carries the same set).

struct WorldPoly {
  int n = 0;
  float x[4]{}, z[4]{}, y = 0;      // image coordinates (x, z), mean height (world y, down)
  int u[4]{}, v[4]{};
  u8 rgb[4][3]{};
  u16 clut = 0, tpage = 0;
  bool semi = false;
  int order = 0;
};

u32 ru32(const u8* p) {
  u32 v;
  std::memcpy(&v, p, 4);
  return v;
}

bool load_world_vram(disc::Archive& archive, int world, std::vector<u16>* vram, std::string* error) {
  const int entry = world == 1 ? 0x1014 : world == 2 ? 0x109F : 0x10E0;  // the first chunk container
  auto data = archive.read_entry(static_cast<u32>(entry));
  if (!data || data->size() < 12) {
    *error = "world map " + std::to_string(world) + ": chunk container not readable";
    return false;
  }
  const u8* c = data->data();
  const size_t size = data->size();
  size_t section3 = 0;
  for (s32 i = 0; i < rd32(c) && 12 + 8 * static_cast<size_t>(i) <= size; ++i)
    if (rd32(c + 4 + i * 8) == 3) section3 = static_cast<size_t>(rd32(c + 8 + i * 8));
  if (section3 == 0 || section3 >= size) {
    *error = "world map " + std::to_string(world) + ": no terrain section";
    return false;
  }
  vram->assign(1024 * 512, 0);
  auto put_clut = [&](const u16* q, int x, int y) {
    for (int k = 0; k < 256; ++k) (*vram)[(y & 511) * 1024 + ((x + k) & 1023)] = q[k];
  };
  // (The darkened copies of the CLUT rows are for distant polygons only: not needed from above.)
  const u8* base = c + section3;
  const size_t room = size - section3;
  size_t p = 0;
  int clut_row = 0x1E0;
  while (p + 4 <= room) {
    const s32 tag = rd32(base + p);
    if (tag == 0) break;
    if (tag == 1 || tag == 2 || tag == 7) {
      if (p + 12 > room) break;
      const int x = rd16(base + p + 4), y = rdu16(base + p + 6);
      const size_t off = static_cast<size_t>(rd32(base + p + 8));
      auto img = off < room ? disc::slz::unpack_block(std::span<const u8>(base + off, room - off), 0, 0, nullptr)
                            : std::nullopt;
      p += 12;
      if (!img) continue;
      img->resize(std::max<size_t>(img->size(), 4 + 0x200 + 8));
      std::vector<u16> w(img->size() / 2);
      std::memcpy(w.data(), img->data(), w.size() * 2);
      const int cluts = w[1];
      size_t q = 2;
      if (cluts != 0) {
        if (q + 256 > w.size()) w.resize(q + 256);
        put_clut(w.data() + q, 0, tag == 1 ? clut_row++ : tag == 2 ? 0x1FB : 0x1FA);
        q += cluts;
      }
      if (q + 2 > w.size()) continue;
      const int words = w[q], rows = w[q + 1];
      q += 2;
      for (int j = 0; j < rows; ++j)
        for (int i = 0; i < words; ++i) {
          const size_t at = q + static_cast<size_t>(j) * words + i;
          if (at < w.size()) (*vram)[((y + j) & 511) * 1024 + ((x + i) & 1023)] = w[at];
        }
    } else if (tag == 3) {  // texture animation groups: their first state is the image itself
      p += 8;
      while (p + 4 <= room && rd32(base + p) != 0) p += 12;
      p += 4;
    } else if (tag == 4 || tag == 5) {
      p += 8;
    } else if (tag == 6) {
      if (p + 12 > room) break;
      const int x = rd16(base + p + 4), y = rdu16(base + p + 6);
      const size_t off = static_cast<size_t>(rd32(base + p + 8)) + 0x14;
      if (off + 0x200 <= room) {
        std::vector<u16> clut(256);
        std::memcpy(clut.data(), base + off, 0x200);
        put_clut(clut.data(), x, y);
      }
      p += 12;
    } else {
      break;
    }
  }
  return true;
}

// The polygons of one block (archive entry), in image coordinates.
void block_polygons(const std::vector<u8>& blk, int bx, int bz, const std::function<void(float&, float&, s32, s32)>& to_image,
                    std::vector<WorldPoly>* out) {
  const size_t size = blk.size();
  if (size < 0x68) return;
  const u8* b = blk.data();
  const s32 nverts = rd32(b);
  const size_t hdr_base = 0x68 + static_cast<size_t>(std::max(0, nverts)) * 8;
  if (nverts < 0 || hdr_base + 0x280 > size) return;
  const s32 ox = bx * 0x3000 + 0x1800, oz = bz * 0x3000 + 0x1800;
  static constexpr int kSize[11] = {0x18, 0x14, 0x10, 0x10, 0x20, 0x18, 0x14, 0x14, 0x10, 0x18, 0x20};
  for (int cell = 0; cell < 16; ++cell) {
    const size_t verts = 0x68 + static_cast<size_t>(rd32(b + 0x28 + cell * 4));
    const int count = rdu16(b + 8 + cell * 2);
    const size_t hdr = hdr_base + cell * 0x20;
    size_t rec = hdr_base + 0x280 + static_cast<size_t>(rd32(b + hdr_base + 0x200 + cell * 4)) + (rdu16(b + hdr) & 0xFFFC);
    auto vertex = [&](u32 field, WorldPoly& p, int k) {
      const size_t i = field >> 2;
      if (static_cast<int>(i) >= count || verts + i * 8 + 8 > size) return false;
      const u8* v = b + verts + i * 8;
      const s32 x = ox + (static_cast<s32>(ru32(v + 4)) >> 16), z = oz + static_cast<s16>(ru32(v + 4));
      to_image(p.x[k], p.z[k], x, z);
      p.y += rd16(v + 2);
      return true;
    };
    auto grey3 = [](u8 g, u8 rgb[3]) { rgb[0] = rgb[1] = rgb[2] = g; };
    for (int type = 0; type < 11; ++type) {
      const int n = rdu16(b + hdr + 2 + type * 2);
      for (int k = 0; k < n; ++k, rec += kSize[type]) {
        if (rec + kSize[type] > size) return;
        if (type == 5 || type == 7) continue;  // F, H: collision only
        const u8* r = b + rec;
        WorldPoly p;
        p.u[0] = r[0], p.v[0] = r[1];
        p.clut = rdu16(r + 2);
        bool ok = true;
        u32 fields[4] = {0, 0, 0, 0};
        switch (type) {
          case 0: case 9: {  // A / J: GT3
            const u32 ab = ru32(r + 0x14), uvc = ru32(r + 4);
            p.n = 3;
            fields[0] = ab & 0xFFFF, fields[1] = ab >> 16, fields[2] = uvc >> 16;
            p.u[1] = r[4], p.v[1] = r[5], p.tpage = r[0x13];
            p.u[2] = r[0xB], p.v[2] = r[0xF];
            for (int c = 0; c < 3; ++c) std::memcpy(p.rgb[c], r + 8 + c * 4, 3);
            p.semi = type == 9;
            break;
          }
          case 1: {  // B: GT3
            const u32 t0 = ru32(r + 0x10), t1 = ru32(r + 8);
            p.n = 3;
            fields[0] = t0 & 0xFFFF, fields[1] = t1 >> 16, fields[2] = t0 >> 16;
            p.u[1] = r[4], p.v[1] = r[5], p.tpage = r[6];
            p.u[2] = r[8], p.v[2] = r[9];
            for (int c = 0; c < 3; ++c) grey3(r[0xC + c], p.rgb[c]);
            break;
          }
          case 2: {  // C: GT3
            const u32 t0 = ru32(r + 8), t1 = ru32(r + 4);
            p.n = 3;
            fields[0] = (t0 >> 14) & 0x3FC, fields[1] = (t1 >> 22) & 0x3FC, fields[2] = (t0 >> 22) & 0x3FC;
            p.u[1] = r[4], p.v[1] = r[5], p.tpage = r[6];
            p.u[2] = r[8], p.v[2] = r[9];
            for (int c = 0; c < 3; ++c) grey3(r[0xC + c], p.rgb[c]);
            break;
          }
          case 3: {  // D: FT3
            const u32 t1 = ru32(r + 0xC), t2 = ru32(r + 8);
            p.n = 3;
            fields[0] = t1 & 0xFFFF, fields[1] = t2 >> 16, fields[2] = t1 >> 16;
            p.u[1] = r[4], p.v[1] = r[5], p.tpage = r[6];
            p.u[2] = r[8], p.v[2] = r[9];
            for (int c = 0; c < 3; ++c) grey3(r[7], p.rgb[c]);
            break;
          }
          case 4: case 10: {  // E / K: GT4
            const u32 ab = ru32(r + 0x18), cd = ru32(r + 0x1C);
            p.n = 4;
            fields[0] = ab & 0xFFFF, fields[1] = ab >> 16, fields[2] = cd & 0xFFFF, fields[3] = cd >> 16;
            p.u[1] = r[4], p.v[1] = r[5], p.tpage = r[6];
            p.u[2] = r[0xB], p.v[2] = r[0xF], p.u[3] = r[0x13], p.v[3] = r[0x17];
            for (int c = 0; c < 4; ++c) std::memcpy(p.rgb[c], r + 8 + c * 4, 3);
            p.semi = type == 10;
            break;
          }
          case 6: case 8: {  // G: GT4, I: FT4
            const u32 t = ru32(r + (type == 6 ? 0x10 : 0xC));
            p.n = 4;
            fields[0] = (t & 0xFF) << 2, fields[1] = (t >> 6) & 0x3FC, fields[2] = (t >> 14) & 0x3FC,
            fields[3] = (t >> 22) & 0x3FC;
            p.u[1] = r[4], p.v[1] = r[5], p.tpage = r[6];
            p.u[2] = r[8], p.v[2] = r[9], p.u[3] = r[0xA], p.v[3] = r[0xB];
            for (int c = 0; c < 4; ++c) grey3(type == 6 ? r[0xC + c] : r[7], p.rgb[c]);
            break;
          }
          default:
            ok = false;
        }
        for (int c = 0; ok && c < p.n; ++c) ok = vertex(fields[c], p, c);
        if (!ok) continue;
        p.y /= static_cast<float>(p.n);
        p.order = static_cast<int>(out->size());
        out->push_back(p);
      }
    }
  }
}

// One textured, Gouraud-shaded triangle (image space, pixel centres), PSX texel rules.
void raster_triangle(std::vector<u16>& canvas, int W, int H, const std::vector<u16>& vram, const WorldPoly& p,
                     const int idx[3]) {
  const float x0 = p.x[idx[0]], y0 = p.z[idx[0]], x1 = p.x[idx[1]], y1 = p.z[idx[1]], x2 = p.x[idx[2]],
              y2 = p.z[idx[2]];
  const float area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
  if (std::abs(area) < 1e-6f) return;
  const int minx = std::max(0, static_cast<int>(std::floor(std::min({x0, x1, x2}))));
  const int maxx = std::min(W - 1, static_cast<int>(std::ceil(std::max({x0, x1, x2}))));
  const int miny = std::max(0, static_cast<int>(std::floor(std::min({y0, y1, y2}))));
  const int maxy = std::min(H - 1, static_cast<int>(std::ceil(std::max({y0, y1, y2}))));
  Prim t;
  t.tex_x = (p.tpage & 0xF) * 64;
  t.tex_y = (p.tpage >> 4 & 1) * 256;
  t.depth = p.tpage >> 7 & 3;
  t.abr = p.tpage >> 5 & 3;
  t.semi = p.semi;
  t.clut_x = (p.clut & 0x3F) * 16;
  t.clut_y = p.clut >> 6 & 0x1FF;
  for (int py = miny; py <= maxy; ++py)
    for (int px = minx; px <= maxx; ++px) {
      const float cx = static_cast<float>(px) + 0.5f, cy = static_cast<float>(py) + 0.5f;
      float w0 = ((x1 - cx) * (y2 - cy) - (x2 - cx) * (y1 - cy)) / area;
      float w1 = ((x2 - cx) * (y0 - cy) - (x0 - cx) * (y2 - cy)) / area;
      float w2 = 1.0f - w0 - w1;
      if (w0 < 0 || w1 < 0 || w2 < 0) continue;
      const int u = static_cast<int>(w0 * p.u[idx[0]] + w1 * p.u[idx[1]] + w2 * p.u[idx[2]]);
      const int v = static_cast<int>(w0 * p.v[idx[0]] + w1 * p.v[idx[1]] + w2 * p.v[idx[2]]);
      for (int c = 0; c < 3; ++c)
        t.rgb[c] = static_cast<u8>(std::clamp(static_cast<int>(w0 * p.rgb[idx[0]][c] + w1 * p.rgb[idx[1]][c] +
                                                               w2 * p.rgb[idx[2]][c] + 0.5f), 0, 255));
      plot(canvas, W, px, py, texel(vram, t, u, v), t);
    }
}

constexpr int kWorldSupersample = 3;

bool render_world_map(disc::Archive& archive, int world, const Json& img, Background* out, std::string* error) {
  std::vector<u16> vram;
  if (!load_world_vram(archive, world, &vram, error)) return false;
  const s64 x0 = img["x0"].as_int(), z0 = img["z0"].as_int(), upp = img["units_per_pixel"].as_int(1);
  const int W = static_cast<int>(img["width"].as_int()) * kWorldSupersample;
  const int H = static_cast<int>(img["height"].as_int()) * kWorldSupersample;
  const float scale = static_cast<float>(kWorldSupersample) / static_cast<float>(upp);
  auto to_image = [&](float& ix, float& iy, s32 x, s32 z) {
    ix = static_cast<float>(x - x0) * scale;
    iy = static_cast<float>(z0 - z) * scale;
  };
  const int base = world == 1 ? 0xFD4 : world == 2 ? 0x1060 : 0x10DE;
  std::vector<WorldPoly> polys;
  for (int bz = 0; bz < 7; ++bz)
    for (int bx = 0; bx < 9; ++bx) {
      // The block's extent against the view.
      const s64 bx0 = bx * 0x3000, bz0 = bz * 0x3000;
      if (bx0 + 0x3000 < x0 || bx0 > x0 + upp * W / kWorldSupersample || bz0 > z0 ||
          bz0 + 0x3000 < z0 - upp * H / kWorldSupersample)
        continue;
      int entry = base + bx + 9 * bz;
      // Map 1's block 0x30 has a second version (entry 0x1013) for chapters 0xf..0x3c, most
      // of the game (worldbg_init_blocks): that one is drawn.
      if (world == 1 && entry == 0x1004) entry = 0x1013;
      if (!archive.has_entry(static_cast<u32>(entry))) continue;
      auto blk = archive.read_entry(static_cast<u32>(entry));
      if (blk) block_polygons(*blk, bx, bz, to_image, &polys);
    }
  if (polys.empty()) {
    *error = "world map " + std::to_string(world) + ": no terrain";
    return false;
  }
  // Lowest ground first (world y grows downwards), the semi-transparent ones (water) last.
  std::stable_sort(polys.begin(), polys.end(), [](const WorldPoly& a, const WorldPoly& b) {
    if (a.semi != b.semi) return !a.semi;
    return a.y > b.y;
  });
  std::vector<u16> canvas(static_cast<size_t>(W) * H, 0);
  for (const WorldPoly& p : polys) {
    static constexpr int kTri[2][3] = {{0, 1, 2}, {1, 3, 2}};
    raster_triangle(canvas, W, H, vram, p, kTri[0]);
    if (p.n == 4) raster_triangle(canvas, W, H, vram, p, kTri[1]);
  }
  out->width = W;
  out->height = H;
  out->pixels.resize(canvas.size());
  for (size_t i = 0; i < canvas.size(); ++i) {
    const u16 c = canvas[i];
    const u32 r = c & 31, g = c >> 5 & 31, b = c >> 10 & 31;
    out->pixels[i] = (r << 3 | r >> 2) | (g << 3 | g >> 2) << 8 | (b << 3 | b >> 2) << 16 | 0xFF000000u;
  }
  return true;
}

bool wanted(const std::string& path, const TrackerRenderOptions& options) {
  if (options.only.empty()) return true;
  for (const std::string& o : options.only)
    if (path.find(o) != std::string::npos) return true;
  return false;
}

// A relative path of the layout ("images/maps/x.png"), refused if it could leave 'dir'.
std::optional<fs::path> image_path(const fs::path& dir, const std::string& rel) {
  if (rel.empty() || rel.find("..") != std::string::npos || rel.find('\\') != std::string::npos || rel[0] == '/' ||
      rel.find(':') != std::string::npos || rel.size() < 5 || rel.compare(rel.size() - 4, 4, ".png") != 0)
    return std::nullopt;
  return dir / util::utf8_path(rel);
}

}  // namespace

fs::path poptracker_override_dir(const fs::path& home, const fs::path& documents) {
  std::error_code ec;
  const fs::path home_pt = home.empty() ? fs::path() : home / "PopTracker";
  const fs::path docs_pt = documents.empty() ? fs::path() : documents / "PopTracker";
  for (const fs::path& base : {home_pt, docs_pt})
    if (!base.empty() && fs::is_directory(base / "user-override" / kPackUid, ec)) return base / "user-override" / kPackUid;
  if (!home_pt.empty() && fs::is_directory(home_pt, ec)) return home_pt / "user-override" / kPackUid;
  if (!docs_pt.empty()) return docs_pt / "user-override" / kPackUid;
  return home_pt / "user-override" / kPackUid;
}

fs::path poptracker_override_dir() {
  fs::path home, documents;
#if defined(_WIN32)
  if (const char* h = SDL_GetUserFolder(SDL_FOLDER_HOME)) home = util::utf8_path(h);
  if (const char* d = SDL_GetUserFolder(SDL_FOLDER_DOCUMENTS)) documents = util::utf8_path(d);
#else
  if (const char* h = std::getenv("HOME"); h && *h) home = h;
  const char* xdg = std::getenv("XDG_DOCUMENTS_DIR");
  std::error_code ec;
  if (xdg && *xdg && fs::path(xdg) != home) documents = xdg;
  else if (!home.empty() && fs::is_directory(home / "Documents", ec)) documents = home / "Documents";
  else documents = home;
#endif
  return poptracker_override_dir(home, documents);
}

int render_tracker_maps(disc::Archive& archive, const fs::path& dir, const TrackerRenderOptions& options) {
  Json layout;
  std::string error;
  if (!Json::parse(std::string(tracker_layout_json()), &layout, &error) || layout["format"].as_int() != 1) {
    std::fprintf(stderr, "tracker maps: the built-in layout is not readable (%s)\n", error.c_str());
    return 1;
  }
  const Json& style = layout["style"];
  const u32 background = colour_of(style["background"].as_string(), 0xFF221C1Cu);
  const u32 label_colour = colour_of(style["label_colour"].as_string(), 0xFFF0E8E8u);
  const u32 dot_fill = colour_of(style["dot"]["colour"].as_string(), 0xFF3030FFu);
  const u32 dot_outline = colour_of(style["dot"]["outline"].as_string(), 0xFFFFFFFFu);
  const int dot_size = static_cast<int>(style["dot"]["size"].as_int(12));
  // Check dots per image (only drawn with options.dots).
  std::vector<std::pair<std::string, std::pair<int, int>>> dots;
  for (const auto& [name, c] : layout["checks"].items())
    dots.push_back({c["image"].as_string(), {static_cast<int>(c["x"].as_int()), static_cast<int>(c["y"].as_int())}});

  int written = 0, failed = 0;
  const Json& images = layout["images"];
  for (size_t i = 0; i < images.size(); ++i) {
    const Json& img = images[i];
    const std::string rel = img["path"].as_string();
    if (!wanted(rel, options)) continue;
    const auto path = image_path(dir, rel);
    const int W = static_cast<int>(img["width"].as_int()), H = static_cast<int>(img["height"].as_int());
    if (!path || W <= 0 || H <= 0 || W > 4096 || H > 4096) {
      std::fprintf(stderr, "tracker maps: bad image entry %s\n", rel.c_str());
      ++failed;
      continue;
    }
    Canvas canvas{W, H, std::vector<u32>(static_cast<size_t>(W) * H, background)};
    if (img["kind"].as_string() == "area") {
      const Json& maps = img["maps"];
      for (size_t k = 0; k < maps.size(); ++k) {
        const Json& m = maps[k];
        const int x = static_cast<int>(m["x"].as_int()), y = static_cast<int>(m["y"].as_int());
        const int w = static_cast<int>(m["w"].as_int()), h = static_cast<int>(m["h"].as_int());
        draw_label(canvas, x, static_cast<int>(m["label_y"].as_int()) + 1, w, m["label"].as_string(), label_colour);
        Background bg;
        std::string why;
        if (!render_field_background(archive, static_cast<int>(m["map"].as_int()), &bg, &why) ||
            bg.width != m["bg_w"].as_int() || bg.height != m["bg_h"].as_int()) {
          std::fprintf(stderr, "tracker maps: %s: %s\n", rel.c_str(),
                       why.empty() ? "the disc's background size differs from the layout's" : why.c_str());
          canvas.fill_rect(x, y, w, h, 0xFF000000u);
          ++failed;
          continue;
        }
        blit_scaled(canvas, x, y, w, h, bg);
      }
    } else if (img["kind"].as_string() == "world") {
      Background terrain;
      std::string why;
      if (!render_world_map(archive, static_cast<int>(img["world_map"].as_int()), img, &terrain, &why)) {
        std::fprintf(stderr, "tracker maps: %s: %s\n", rel.c_str(), why.c_str());
        ++failed;
        continue;
      }
      blit_scaled(canvas, 0, 0, W, H, terrain);
      if (options.dots)
        for (size_t k = 0; k < img["dots"].size(); ++k)
          draw_dot(canvas, static_cast<int>(img["dots"][k]["x"].as_int()), static_cast<int>(img["dots"][k]["y"].as_int()),
                   dot_size, dot_fill, dot_outline);
    } else {
      continue;
    }
    if (options.dots)
      for (const auto& [image, at] : dots)
        if (image == rel) draw_dot(canvas, at.first, at.second, dot_size, dot_fill, dot_outline);
    std::error_code ec;
    fs::create_directories(path->parent_path(), ec);
    if (ec || !util::write_png_rgb(*path, canvas.pixels, W, H, &error)) {
      std::fprintf(stderr, "tracker maps: %s: %s\n", rel.c_str(), ec ? ec.message().c_str() : error.c_str());
      ++failed;
      continue;
    }
    ++written;
    std::printf("tracker maps: %s (%dx%d)\n", rel.c_str(), W, H);
    std::fflush(stdout);
  }
  std::printf("tracker maps: %d image(s) written to %s%s\n", written, util::path_utf8(dir).c_str(),
              failed ? ", with errors" : "");
  return failed ? 1 : 0;
}

}  // namespace ss::tools
