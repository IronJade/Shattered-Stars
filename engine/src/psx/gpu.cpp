#include "psx/gpu.h"

#include <algorithm>
#include <cstdlib>

namespace ss::psx {
namespace {

// GP0 vertex coordinates are signed 11-bit values.
int sext11(u32 v) { return static_cast<int>(static_cast<s32>(v << 21) >> 21); }
int vx(u32 w) { return sext11(w & 0x7FF); }
int vy(u32 w) { return sext11(w >> 16 & 0x7FF); }

// Ordered dither added to 8-bit colour before truncating to 5 bits.
constexpr int kDither[4][4] = {{-4, 0, -3, 1}, {2, -2, 3, -1}, {-3, 1, -4, 0}, {3, -1, 2, -2}};

int clamp5(int v) { return std::clamp(v, 0, 31); }

u16 pack(int r5, int g5, int b5) {
  return static_cast<u16>(clamp5(r5) | clamp5(g5) << 5 | clamp5(b5) << 10);
}

u32 to_rgba(u16 c) {
  const u32 r = c & 31, g = c >> 5 & 31, b = c >> 10 & 31;
  return (r << 3 | r >> 2) | (g << 3 | g >> 2) << 8 | (b << 3 | b >> 2) << 16 | 0xFF000000u;
}

}  // namespace

Gpu::Gpu() : vram_(static_cast<size_t>(kVramWidth) * kVramHeight, 0) {
  state_.area_right = kVramWidth - 1;
  state_.area_bottom = kVramHeight - 1;
}

int Gpu::gp0_packet_words(u32 first) {
  const u32 cmd = first >> 24;
  if (cmd >= 0x20 && cmd < 0x40) {
    const int vertices = cmd & 0x08 ? 4 : 3;
    const bool gouraud = cmd & 0x10, textured = cmd & 0x04;
    return 1 + vertices * (1 + (textured ? 1 : 0)) + (gouraud ? vertices - 1 : 0);
  }
  if (cmd >= 0x40 && cmd < 0x60) return cmd & 0x08 ? 0 : (cmd & 0x10 ? 4 : 3);
  if (cmd >= 0x60 && cmd < 0x80) {
    const bool textured = cmd & 0x04;
    const bool variable = (cmd >> 3 & 3) == 0;
    return 2 + (textured ? 1 : 0) + (variable ? 1 : 0);
  }
  if (cmd >= 0x80 && cmd < 0xA0) return 4;
  if (cmd >= 0xA0 && cmd < 0xC0) return 0;
  if (cmd >= 0xC0 && cmd < 0xE0) return 3;
  if (cmd == 0x02) return 3;
  return 1;
}

void Gpu::gp0_stream(std::span<const u32> words) {
  size_t i = 0;
  while (i < words.size()) {
    const u32 cmd = words[i] >> 24;
    size_t n = static_cast<size_t>(gp0_packet_words(words[i]));
    if (n == 0) {
      if (cmd >= 0x40 && cmd < 0x60) {  // polyline: runs to the 0x5xxx5xxx terminator
        n = 2;
        while (i + n < words.size() && (words[i + n] & 0xF000F000) != 0x50005000) ++n;
        n = std::min(words.size() - i, n + 1);
      } else if (cmd >= 0xA0 && cmd < 0xC0 && i + 2 < words.size()) {  // CPU -> VRAM
        const u32 w = words[i + 2] & 0xFFFF, h = words[i + 2] >> 16;
        const u32 pixels = (((w - 1) & 0x3FF) + 1) * (((h - 1) & 0x1FF) + 1);
        n = std::min<size_t>(words.size() - i, 3 + (pixels + 1) / 2);
      } else {
        n = words.size() - i;
      }
    }
    n = std::min(n, words.size() - i);
    gp0(words.subspan(i, n));
    i += n;
  }
}

void Gpu::gp0(std::span<const u32> p) {
  if (p.empty()) return;
  const u32 w0 = p[0];
  const u32 cmd = w0 >> 24;
  if (cmd >= 0x20 && cmd < 0x40) return draw_polygon(p);
  if (cmd >= 0x40 && cmd < 0x60) return draw_line(p);
  if (cmd >= 0x60 && cmd < 0x80) return draw_rectangle(p);
  switch (cmd) {
    case 0x02: {  // fill rectangle: ignores draw area and mask; x and width snap to 16
      if (p.size() < 3) return;
      Rect r{static_cast<s16>(p[1] & 0x3F0), static_cast<s16>(p[1] >> 16 & 0x1FF),
             static_cast<s16>(((p[2] & 0x3FF) + 0xF) & ~0xF), static_cast<s16>(p[2] >> 16 & 0x1FF)};
      clear_image(r, w0 & 0xFF, w0 >> 8 & 0xFF, w0 >> 16 & 0xFF);
      return;
    }
    case 0xE1:
      state_.texpage = static_cast<u16>(w0 & 0x9FF);
      state_.dither = w0 >> 9 & 1;
      state_.rect_flip_x = w0 >> 12 & 1;
      state_.rect_flip_y = w0 >> 13 & 1;
      return;
    case 0xE2:
      state_.window_mask_x = w0 & 0x1F;
      state_.window_mask_y = w0 >> 5 & 0x1F;
      state_.window_off_x = w0 >> 10 & 0x1F;
      state_.window_off_y = w0 >> 15 & 0x1F;
      return;
    case 0xE3:
      state_.area_left = w0 & 0x3FF;
      state_.area_top = w0 >> 10 & 0x1FF;
      return;
    case 0xE4:
      state_.area_right = w0 & 0x3FF;
      state_.area_bottom = w0 >> 10 & 0x1FF;
      return;
    case 0xE5:
      state_.offset_x = sext11(w0 & 0x7FF);
      state_.offset_y = sext11(w0 >> 11 & 0x7FF);
      return;
    case 0xE6:
      state_.set_mask = w0 & 1;
      state_.check_mask = w0 >> 1 & 1;
      return;
    default:
      break;
  }
  if (cmd >= 0x80 && cmd < 0xA0 && p.size() >= 4) {
    Rect src{static_cast<s16>(p[1] & 0x3FF), static_cast<s16>(p[1] >> 16 & 0x1FF),
             static_cast<s16>(p[3] & 0xFFFF), static_cast<s16>(p[3] >> 16)};
    move_image(src, p[2] & 0x3FF, p[2] >> 16 & 0x1FF);
  } else if (cmd >= 0xA0 && cmd < 0xC0 && p.size() >= 3) {
    Rect r{static_cast<s16>(p[1] & 0x3FF), static_cast<s16>(p[1] >> 16 & 0x1FF),
           static_cast<s16>(p[2] & 0xFFFF), static_cast<s16>(p[2] >> 16)};
    const int w = ((r.w - 1) & 0x3FF) + 1, h = ((r.h - 1) & 0x1FF) + 1;
    std::vector<u16> pixels(static_cast<size_t>(w) * h);
    for (size_t i = 0; i < pixels.size(); ++i) {
      const size_t word = 3 + i / 2;
      if (word >= p.size()) break;
      pixels[i] = static_cast<u16>(i & 1 ? p[word] >> 16 : p[word]);
    }
    load_image(Rect{r.x, r.y, static_cast<s16>(w), static_cast<s16>(h)}, pixels.data());
  }
}

// ---------------------------------------------------------------- transfers

void Gpu::load_image(const Rect& r, const u16* pixels) {
  const u16 mask_or = state_.set_mask ? 0x8000 : 0;
  for (int y = 0; y < r.h; ++y) {
    for (int x = 0; x < r.w; ++x) {
      u16& dst = vram_[((r.y + y) & 511) * kVramWidth + ((r.x + x) & 1023)];
      if (state_.check_mask && (dst & 0x8000)) continue;
      dst = pixels[y * r.w + x] | mask_or;
    }
  }
}

void Gpu::store_image(const Rect& r, u16* pixels) const {
  for (int y = 0; y < r.h; ++y)
    for (int x = 0; x < r.w; ++x) pixels[y * r.w + x] = pixel(r.x + x, r.y + y);
}

void Gpu::move_image(const Rect& src, int dx, int dy) {
  const int w = ((src.w - 1) & 0x3FF) + 1, h = ((src.h - 1) & 0x1FF) + 1;
  std::vector<u16> tmp(static_cast<size_t>(w) * h);
  store_image(Rect{src.x, src.y, static_cast<s16>(w), static_cast<s16>(h)}, tmp.data());
  load_image(Rect{static_cast<s16>(dx), static_cast<s16>(dy), static_cast<s16>(w), static_cast<s16>(h)}, tmp.data());
}

void Gpu::clear_image(const Rect& r, u8 red, u8 green, u8 blue) {
  const u16 c = pack(red >> 3, green >> 3, blue >> 3);
  for (int y = 0; y < r.h; ++y)
    for (int x = 0; x < r.w; ++x) vram_[((r.y + y) & 511) * kVramWidth + ((r.x + x) & 1023)] = c;
}

// ---------------------------------------------------------------- pixels

void Gpu::write_pixel(int x, int y, u16 color, bool semi, int semi_mode) {
  u16& dst = vram_[y * kVramWidth + x];
  if (state_.check_mask && (dst & 0x8000)) return;
  if (semi) {
    const int br = dst & 31, bg = dst >> 5 & 31, bb = dst >> 10 & 31;
    const int fr = color & 31, fg = color >> 5 & 31, fb = color >> 10 & 31;
    int r, g, b;
    switch (semi_mode) {
      case 0: r = (br + fr) >> 1; g = (bg + fg) >> 1; b = (bb + fb) >> 1; break;
      case 1: r = br + fr; g = bg + fg; b = bb + fb; break;
      case 2: r = br - fr; g = bg - fg; b = bb - fb; break;
      default: r = br + (fr >> 2); g = bg + (fg >> 2); b = bb + (fb >> 2); break;
    }
    color = static_cast<u16>(pack(r, g, b) | (color & 0x8000));
  }
  dst = static_cast<u16>(color | (state_.set_mask ? 0x8000 : 0));
}

u16 Gpu::sample(int u, int v, const Shading& s) const {
  u &= 0xFF;
  v &= 0xFF;
  // Texture window: replace the masked bits with the offset bits (units of 8 texels).
  u = (u & ~(state_.window_mask_x * 8)) | ((state_.window_off_x & state_.window_mask_x) * 8);
  v = (v & ~(state_.window_mask_y * 8)) | ((state_.window_off_y & state_.window_mask_y) * 8);
  const int base_x = (s.texpage & 0xF) * 64;
  const int base_y = (s.texpage >> 4 & 1) * 256;
  const int depth = s.texpage >> 7 & 3;
  const int clut_x = (s.clut & 0x3F) * 16, clut_y = s.clut >> 6 & 0x1FF;
  switch (depth) {
    case 0: {
      const u16 word = pixel(base_x + u / 4, base_y + v);
      return pixel(clut_x + (word >> ((u & 3) * 4) & 0xF), clut_y);
    }
    case 1: {
      const u16 word = pixel(base_x + u / 2, base_y + v);
      return pixel(clut_x + (word >> ((u & 1) * 8) & 0xFF), clut_y);
    }
    default:
      return pixel(base_x + u, base_y + v);
  }
}

void Gpu::shade_pixel(int x, int y, int r, int g, int b, int u, int v, const Shading& s, bool dither) {
  const int d = dither ? kDither[y & 3][x & 3] : 0;
  int semi_mode = s.textured ? (s.texpage >> 5 & 3) : (state_.texpage >> 5 & 3);
  if (!s.textured) {
    write_pixel(x, y, pack((r + d) >> 3, (g + d) >> 3, (b + d) >> 3), s.semi, semi_mode);
    return;
  }
  const u16 texel = sample(u, v, s);
  if (texel == 0) return;  // fully transparent texel
  const bool semi = s.semi && (texel & 0x8000);
  if (s.raw) {
    write_pixel(x, y, texel, semi, semi_mode);
    return;
  }
  // Modulation: texel * colour / 128, computed in 8-bit and dithered like shading.
  const int tr = (texel & 31) * r >> 4, tg = (texel >> 5 & 31) * g >> 4, tb = (texel >> 10 & 31) * b >> 4;
  write_pixel(x, y, static_cast<u16>(pack((tr + d) >> 3, (tg + d) >> 3, (tb + d) >> 3) | (texel & 0x8000)), semi,
              semi_mode);
}

// ---------------------------------------------------------------- polygons

void Gpu::draw_polygon(std::span<const u32> p) {
  const u32 cmd = p[0] >> 24;
  const int count = cmd & 0x08 ? 4 : 3;
  Shading s;
  s.gouraud = cmd & 0x10;
  s.textured = cmd & 0x04;
  s.semi = cmd & 0x02;
  s.raw = cmd & 0x01;
  if (static_cast<int>(p.size()) < gp0_packet_words(p[0])) return;

  Vertex v[4];
  size_t i = 0;
  for (int n = 0; n < count; ++n) {
    const u32 color = (n == 0 || !s.gouraud) ? p[0] : p[i++];
    if (n == 0) i = 1;
    v[n].r = color & 0xFF;
    v[n].g = color >> 8 & 0xFF;
    v[n].b = color >> 16 & 0xFF;
    const u32 xy = p[i++];
    v[n].x = vx(xy) + state_.offset_x;
    v[n].y = vy(xy) + state_.offset_y;
    if (s.textured) {
      const u32 uv = p[i++];
      v[n].u = uv & 0xFF;
      v[n].v = uv >> 8 & 0xFF;
      if (n == 0) s.clut = static_cast<u16>(uv >> 16);
      if (n == 1) s.texpage = static_cast<u16>(uv >> 16);
    }
  }
  if (s.textured) {
    // A textured polygon's page also becomes the current draw-mode page.
    state_.texpage = static_cast<u16>((state_.texpage & ~0x9FF) | (s.texpage & 0x9FF));
  } else {
    s.raw = false;
  }
  if (!s.gouraud)
    for (int n = 1; n < count; ++n) v[n].r = v[0].r, v[n].g = v[0].g, v[n].b = v[0].b;

  if (count == 3) {
    draw_triangle(v[0], v[1], v[2], s);
    return;
  }
  // A quad is the triangles 0-1-2 and 2-1-3 (the vertex order matters: the plane
  // origin is the leftmost vertex, the later one on ties). It is dropped whole when any
  // of their five edges is too long; 0-3 is an edge of neither, so it may be (PCSX-Redux's
  // notes: checked on SCPH-1000, -5501 and -7001).
  auto too_long = [&](int p0, int p1) { return std::abs(v[p0].x - v[p1].x) >= 1024 || std::abs(v[p0].y - v[p1].y) >= 512; };
  if (too_long(0, 1) || too_long(1, 3) || too_long(3, 2) || too_long(2, 0) || too_long(1, 2)) return;
  draw_triangle(v[0], v[1], v[2], s);
  draw_triangle(v[2], v[1], v[3], s);
}

void Gpu::draw_triangle(Vertex a, Vertex b, Vertex c, const Shading& s) {
  // The GPU skips polygons with any edge spanning 1024+ columns or 512+ rows.
  const int min_x = std::min({a.x, b.x, c.x}), max_x = std::max({a.x, b.x, c.x});
  const int min_y = std::min({a.y, b.y, c.y}), max_y = std::max({a.y, b.y, c.y});
  if (max_x - min_x >= 1024 || max_y - min_y >= 512) return;

  // Attributes (u, v, gouraud colour) come from planes, as the hardware computes them:
  // per-triangle gradients with 12 fractional bits (truncated), from the leftmost
  // vertex's value plus half a unit, 8 bits taken (wrapping). The vertex the planes start
  // from is chosen before sorting, the later vertex winning ties as the hardware does.
  // This is Mednafen's / DuckStation's model; it reproduces the hardware VRAM dumps of
  // JaCzekanski's ps1-tests gpu/triangle and gpu/uv-interpolation exactly, where an exact
  // rational interpolation (this code before) and PCSX-Redux's edge walker do not.
  const Vertex* left = &a;
  if (b.x <= a.x) left = c.x <= b.x ? &c : &b;
  else if (c.x < a.x) left = &c;
  const Vertex* sorted[3] = {&a, &b, &c};
  if (sorted[2]->y < sorted[1]->y) std::swap(sorted[1], sorted[2]);
  if (sorted[1]->y < sorted[0]->y) std::swap(sorted[0], sorted[1]);
  if (sorted[2]->y < sorted[1]->y) std::swap(sorted[1], sorted[2]);
  const Vertex& p0 = *sorted[0];
  const Vertex& p1 = *sorted[1];
  const Vertex& p2 = *sorted[2];
  const s64 det = static_cast<s64>(p1.x - p0.x) * (p2.y - p1.y) - static_cast<s64>(p2.x - p1.x) * (p1.y - p0.y);
  if (det == 0) return;
  struct Plane {
    u32 dx = 0, dy = 0, origin = 0;
    int at(int x, int y) const { return static_cast<int>((origin + dx * static_cast<u32>(x) + dy * static_cast<u32>(y)) >> 24 & 0xFF); }
  };
  auto plane = [&](int Vertex::*attr) {
    const s64 a0 = p0.*attr, a1 = p1.*attr, a2 = p2.*attr;
    Plane pl;
    pl.dx = static_cast<u32>(((a1 - a0) * (p2.y - p1.y) - (a2 - a1) * (p1.y - p0.y)) * 4096 / det) << 12;
    pl.dy = static_cast<u32>(((p1.x - p0.x) * (a2 - a1) - (p2.x - p1.x) * (a1 - a0)) * 4096 / det) << 12;
    pl.origin = ((static_cast<u32>(left->*attr) << 12) + 2048) << 12;
    pl.origin -= pl.dx * static_cast<u32>(left->x) + pl.dy * static_cast<u32>(left->y);
    return pl;
  };
  Plane pr, pg, pb, pu, pv;
  if (s.gouraud) pr = plane(&Vertex::r), pg = plane(&Vertex::g), pb = plane(&Vertex::b);
  if (s.textured) pu = plane(&Vertex::u), pv = plane(&Vertex::v);

  s64 area = static_cast<s64>(b.x - a.x) * (c.y - a.y) - static_cast<s64>(b.y - a.y) * (c.x - a.x);
  if (area < 0) {
    std::swap(b, c);
    area = -area;
  }

  // Coverage: edge functions; pixels are sampled at integer coordinates and edges that
  // are not top or left are exclusive (the console's fill convention).
  struct Edge {
    s64 a, b, c;
    bool inclusive;
  };
  auto make_edge = [](const Vertex& p, const Vertex& q) {
    Edge e{static_cast<s64>(p.y) - q.y, static_cast<s64>(q.x) - p.x,
           static_cast<s64>(p.x) * q.y - static_cast<s64>(p.y) * q.x, false};
    // Triangles are normalised to positive area, which in y-down screen space
    // walks the top edge left-to-right and the left edge upward.
    e.inclusive = (p.y == q.y && q.x > p.x) || (q.y < p.y);
    return e;
  };
  const Edge e0 = make_edge(b, c), e1 = make_edge(c, a), e2 = make_edge(a, b);

  const int x0 = std::max(min_x, state_.area_left), x1 = std::min(max_x, state_.area_right);
  const int y0 = std::max(min_y, state_.area_top), y1 = std::min(max_y, state_.area_bottom);
  const bool dither = dithering_enabled && state_.dither && (s.gouraud || (dither_textured && s.textured && !s.raw));

  for (int y = y0; y <= y1; ++y) {
    for (int x = x0; x <= x1; ++x) {
      const s64 w0 = e0.a * x + e0.b * y + e0.c;
      const s64 w1 = e1.a * x + e1.b * y + e1.c;
      const s64 w2 = e2.a * x + e2.b * y + e2.c;
      if (w0 < 0 || w1 < 0 || w2 < 0) continue;
      if ((w0 == 0 && !e0.inclusive) || (w1 == 0 && !e1.inclusive) || (w2 == 0 && !e2.inclusive)) continue;
      const int r = s.gouraud ? pr.at(x, y) : a.r;
      const int g = s.gouraud ? pg.at(x, y) : a.g;
      const int bl = s.gouraud ? pb.at(x, y) : a.b;
      const int u = s.textured ? pu.at(x, y) : 0;
      const int v = s.textured ? pv.at(x, y) : 0;
      shade_pixel(x, y, r, g, bl, u, v, s, dither);
    }
  }
}

// ---------------------------------------------------------------- lines

void Gpu::draw_line(std::span<const u32> p) {
  const u32 cmd = p[0] >> 24;
  Shading s;
  s.gouraud = cmd & 0x10;
  s.semi = cmd & 0x02;
  const bool poly = cmd & 0x08;
  // Packet: colour|cmd, xy, then per further vertex [colour if gouraud], xy.
  // Polylines end with a word matching 0x5xxx5xxx where the next vertex would start.
  std::vector<Vertex> points;
  u32 color = p[0];
  size_t i = 1;
  while (i < p.size()) {
    if (!points.empty()) {
      if (poly && (p[i] & 0xF000F000) == 0x50005000) break;
      if (s.gouraud) {
        color = p[i++];
        if (i >= p.size()) break;
      }
    }
    Vertex v;
    v.r = color & 0xFF;
    v.g = color >> 8 & 0xFF;
    v.b = color >> 16 & 0xFF;
    v.x = vx(p[i]) + state_.offset_x;
    v.y = vy(p[i]) + state_.offset_y;
    ++i;
    points.push_back(v);
    if (!poly && points.size() == 2) break;
  }
  for (size_t n = 1; n < points.size(); ++n) draw_segment(points[n - 1], points[n], s);
}

void Gpu::draw_segment(Vertex a, Vertex b, const Shading& s) {
  // The hardware's line walk (Mednafen's / DuckStation's model, identical to the hardware
  // VRAM dump of JaCzekanski's ps1-tests gpu/lines): k = the longer axis' length; k + 1
  // pixels from the left end; x and y step in 32.32 from the pixel centre (x biased
  // back by 1024 / 2^32, y too when it steps up), each step rounded away from zero;
  // colours step in 12-bit fixed point from the centre of the start value. Every line
  // is dithered when dithering is on (PCSX-Redux dithers none: dither_lines false).
  const int adx = std::abs(b.x - a.x), ady = std::abs(b.y - a.y);
  if (adx >= 1024 || ady >= 512) return;
  const int k = std::max(adx, ady);
  if (a.x >= b.x && k > 0) std::swap(a, b);
  auto div_xy = [](s64 delta, s32 dk) { return ((delta << 32) - (delta < 0 ? dk - 1 : 0) + (delta > 0 ? dk - 1 : 0)) / dk; };
  s64 dxdk = 0, dydk = 0;
  s32 drdk = 0, dgdk = 0, dbdk = 0;
  if (k != 0) {
    dxdk = div_xy(b.x - a.x, k);
    dydk = div_xy(b.y - a.y, k);
    drdk = ((b.r - a.r) * 4096) / k;
    dgdk = ((b.g - a.g) * 4096) / k;
    dbdk = ((b.b - a.b) * 4096) / k;
  }
  s64 cur_x = ((static_cast<s64>(a.x) << 32) | (s64{1} << 31)) - 1024;
  s64 cur_y = ((static_cast<s64>(a.y) << 32) | (s64{1} << 31)) - (dydk < 0 ? 1024 : 0);
  s32 cr = a.r << 12 | 1 << 11, cg = a.g << 12 | 1 << 11, cb = a.b << 12 | 1 << 11;
  const bool dither = dithering_enabled && dither_lines && state_.dither;
  for (int i = 0; i <= k; ++i) {
    const int x = sext11(static_cast<u32>(cur_x >> 32));
    const int y = sext11(static_cast<u32>(cur_y >> 32));
    if (in_area(x, y)) {
      const int r = s.gouraud ? (cr >> 12) & 0xFF : a.r;
      const int g = s.gouraud ? (cg >> 12) & 0xFF : a.g;
      const int bl = s.gouraud ? (cb >> 12) & 0xFF : a.b;
      shade_pixel(x, y, r, g, bl, 0, 0, s, dither);
    }
    cur_x += dxdk;
    cur_y += dydk;
    cr += drdk;
    cg += dgdk;
    cb += dbdk;
  }
}

// ---------------------------------------------------------------- rectangles

void Gpu::draw_rectangle(std::span<const u32> p) {
  const u32 cmd = p[0] >> 24;
  if (static_cast<int>(p.size()) < gp0_packet_words(p[0])) return;
  Shading s;
  s.textured = cmd & 0x04;
  s.semi = cmd & 0x02;
  s.raw = s.textured && (cmd & 0x01);
  s.texpage = state_.texpage;
  size_t i = 1;
  const int x = vx(p[i]) + state_.offset_x, y = vy(p[i]) + state_.offset_y;
  ++i;
  int u0 = 0, v0 = 0;
  if (s.textured) {
    u0 = p[i] & 0xFF;
    v0 = p[i] >> 8 & 0xFF;
    s.clut = static_cast<u16>(p[i] >> 16);
    ++i;
  }
  int w, h;
  switch (cmd >> 3 & 3) {
    case 1: w = h = 1; break;
    case 2: w = h = 8; break;
    case 3: w = h = 16; break;
    default: w = p[i] & 0x3FF; h = p[i] >> 16 & 0x1FF; break;
  }
  const int r = p[0] & 0xFF, g = p[0] >> 8 & 0xFF, b = p[0] >> 16 & 0xFF;
  for (int dy = 0; dy < h; ++dy) {
    for (int dx = 0; dx < w; ++dx) {
      const int px = x + dx, py = y + dy;
      if (!in_area(px, py)) continue;
      const int u = state_.rect_flip_x ? u0 - dx : u0 + dx;
      const int v = state_.rect_flip_y ? v0 - dy : v0 + dy;
      shade_pixel(px, py, r, g, b, u, v, s, false);
    }
  }
}

// ---------------------------------------------------------------- output

void Gpu::display_to_rgba(std::vector<u32>& out, int& width, int& height) const {
  width = display_.width;
  height = display_.lines > 0 && display_.lines < display_.height ? display_.lines : display_.height;
  out.assign(static_cast<size_t>(width) * height, 0xFF000000u);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      if (display_.color24) {
        // 24-bit mode packs three bytes per pixel across the 16-bit VRAM row.
        const int byte = x * 3;
        auto vram_byte = [&](int n) {
          const u16 word = pixel(display_.x + n / 2, display_.y + y);
          return n & 1 ? word >> 8 : word & 0xFF;
        };
        out[y * width + x] = vram_byte(byte) | vram_byte(byte + 1) << 8 | vram_byte(byte + 2) << 16 | 0xFF000000u;
      } else {
        out[y * width + x] = to_rgba(pixel(display_.x + x, display_.y + y));
      }
    }
  }
}

void Gpu::vram_to_rgba(std::vector<u32>& out) const {
  out.resize(vram_.size());
  for (size_t i = 0; i < vram_.size(); ++i) out[i] = to_rgba(vram_[i]);
}

}  // namespace ss::psx
