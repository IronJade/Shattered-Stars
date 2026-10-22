#include <vector>

#include "psx/gpu.h"
#include "test.h"

using namespace ss;

namespace {

int count_pixels(const psx::Gpu& gpu, u16 color, int w = 64, int h = 64) {
  int n = 0;
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) n += gpu.pixel(x, y) == color;
  return n;
}

constexpr u32 xy(int x, int y) { return static_cast<u32>(y & 0xFFFF) << 16 | static_cast<u32>(x & 0xFFFF); }

}  // namespace

TEST(gpu_quad_covers_exactly_its_area) {
  // A flat white quad from (0,0) to (10,10) draws columns and rows 0..9 only.
  psx::Gpu gpu;
  const u32 quad[] = {0x28FFFFFF, xy(0, 0), xy(10, 0), xy(0, 10), xy(10, 10)};
  gpu.gp0(quad);
  CHECK_EQ(count_pixels(gpu, 0x7FFF), 100);
  CHECK(gpu.pixel(9, 9) == 0x7FFF);
  CHECK(gpu.pixel(10, 9) == 0);
  CHECK(gpu.pixel(9, 10) == 0);
}

TEST(gpu_adjacent_triangles_neither_overlap_nor_gap) {
  // Two triangles sharing a diagonal, drawn additively (semi mode 1): any pixel
  // drawn twice would come out brighter than one drawn once.
  psx::Gpu gpu;
  const u32 mode[] = {0xE1000020};  // semi-transparency mode 1 (B + F)
  gpu.gp0(mode);
  const u32 t1[] = {0x22080808, xy(0, 0), xy(17, 3), xy(5, 20)};
  const u32 t2[] = {0x22080808, xy(17, 3), xy(5, 20), xy(23, 25)};
  gpu.gp0(t1);
  gpu.gp0(t2);
  int doubled = 0, single = 0;
  for (int y = 0; y < 32; ++y)
    for (int x = 0; x < 32; ++x) {
      const u16 p = gpu.pixel(x, y);
      single += p == 0x0421;
      doubled += p == 0x0842;
    }
  CHECK(single > 200);
  CHECK_EQ(doubled, 0);
}

TEST(gpu_4bit_texture_and_clut) {
  psx::Gpu gpu;
  // Page at x = 64 halfwords; texels 0..3 of row 0 = indices 1,2,0,3. CLUT at (0, 480).
  const u16 texels = 0x3021;
  gpu.load_image(psx::Rect{64, 0, 1, 1}, &texels);
  const u16 clut[4] = {0x0000, 0x001F, 0x03E0, 0x7C00};
  gpu.load_image(psx::Rect{0, 480, 4, 1}, clut);
  const u32 tpage = 1;  // x base 64, 4-bit
  const u32 clut_id = 480 << 6;
  const u32 sprite[] = {0x65808080, xy(100, 100), clut_id << 16 | 0, (1u << 16) | 4};
  const u32 e1[] = {0xE1000000 | tpage};
  gpu.gp0(e1);
  gpu.gp0(sprite);
  CHECK(gpu.pixel(100, 100) == 0x001F);
  CHECK(gpu.pixel(101, 100) == 0x03E0);
  CHECK(gpu.pixel(102, 100) == 0);  // index 0 is colour 0x0000: transparent, not drawn
  CHECK(gpu.pixel(103, 100) == 0x7C00);
}

TEST(gpu_semi_transparency_average) {
  psx::Gpu gpu;
  const u32 fill_white[] = {0x02FFFFFF, xy(0, 0), xy(16, 1)};
  gpu.gp0(fill_white);
  const u32 mode0[] = {0xE1000000};
  gpu.gp0(mode0);
  const u32 black_semi[] = {0x62000000, xy(0, 0), xy(1, 1)};  // semi-transparent black 1x1 rect
  gpu.gp0(black_semi);
  CHECK(gpu.pixel(0, 0) == (15 | 15 << 5 | 15 << 10));
}

TEST(gpu_mask_check) {
  psx::Gpu gpu;
  const u16 masked = 0x8000;
  gpu.load_image(psx::Rect{5, 5, 1, 1}, &masked);
  const u32 check[] = {0xE6000002};
  gpu.gp0(check);
  const u32 dot[] = {0x68FFFFFF, xy(5, 5)};
  gpu.gp0(dot);
  CHECK(gpu.pixel(5, 5) == 0x8000);
}

TEST(gpu_draw_area_and_offset) {
  psx::Gpu gpu;
  const u32 setup[] = {0xE3000000 | (2 << 10) | 2, 0xE4000000 | (5 << 10) | 5, 0xE5000000 | (1 << 11) | 1};
  for (u32 w : setup) gpu.gp0(std::span<const u32>(&w, 1));
  const u32 rect[] = {0x60FFFFFF, xy(0, 0), xy(10, 10)};
  gpu.gp0(rect);
  // Offset moves the rect to (1,1); the area clips it to 2..5 inclusive.
  CHECK_EQ(count_pixels(gpu, 0x7FFF, 16, 16), 16);
  CHECK(gpu.pixel(2, 2) == 0x7FFF && gpu.pixel(5, 5) == 0x7FFF);
  CHECK(gpu.pixel(1, 1) == 0 && gpu.pixel(6, 6) == 0);
}

TEST(gpu_cpu_to_vram_packet) {
  psx::Gpu gpu;
  const u32 packet[] = {0xA0000000, xy(10, 20), xy(2, 1), 0x7C00001F};
  gpu.gp0(packet);
  CHECK(gpu.pixel(10, 20) == 0x001F);
  CHECK(gpu.pixel(11, 20) == 0x7C00);
}

// Probes from the hardware VRAM dumps of JaCzekanski's ps1-tests (gpu/triangle and
// gpu/uv-interpolation): attributes come from per-triangle planes with 12-bit gradients
// and a half-unit round, not from an exact interpolation.
TEST(gpu_gouraud_and_uv_match_hardware_dumps) {
  psx::Gpu gpu;
  // gpu/triangle: a gouraud triangle (red, green, blue) on white, no dithering.
  const u32 white[] = {0x02FFFFFF, xy(0, 0), xy(320, 240)};
  gpu.gp0(white);
  const u32 tri[] = {0x300000FF, xy(40, 223), 0x0000FF00, xy(280, 223), 0x00FF0000, xy(160, 16)};
  gpu.gp0(tri);
  struct Probe {
    int x, y;
    u16 colour;
  };
  constexpr Probe kTriangle[] = {{157, 23, 0x7801}, {166, 44, 0x6C61}, {139, 65, 0x6026}, {166, 65, 0x6083},
                                 {184, 65, 0x60E0}, {148, 86, 0x5467}, {157, 86, 0x54A5}, {130, 93, 0x5049}};
  for (const Probe& p : kTriangle) CHECK_EQ(gpu.pixel(p.x, p.y), p.colour);
  // gpu/uv-interpolation: row i is a quad i pixels wide with u running 0 -> 1 over a
  // red texel 0 and a green texel 1 (15-bit page at x 512).
  const u32 red_texel[] = {0xA0000000, xy(512, 0), xy(1, 1), 0x001F};
  const u32 green_texel[] = {0xA0000000, xy(513, 0), xy(1, 1), 0x03E0};
  gpu.gp0(red_texel);
  gpu.gp0(green_texel);
  for (int i = 0; i < 32; ++i) {
    const u32 tpage = 2 << 7 | 512 >> 6;
    const u32 quad[] = {0x2C808080, xy(0, 256 + i), 0x0000, xy(i, 256 + i), tpage << 16 | 1,
                        xy(0, 257 + i), 0x0000, xy(i, 257 + i), 1};
    gpu.gp0(quad);
  }
  // Hardware: the texel switches to green halfway along the row (3 of 5 pixels, ...).
  constexpr Probe kUv[] = {{3, 5, 0x03E0}, {6, 10, 0x03E0}, {9, 15, 0x03E0}, {12, 20, 0x03E0}, {2, 5, 0x001F}};
  for (const Probe& p : kUv) CHECK_EQ(gpu.pixel(p.x, 256 + p.y), p.colour);
}
