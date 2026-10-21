#include "game/common/texture.h"

#include <vector>

#include "disc/slz.h"

namespace ss::game {

u16 upload_clut256(psx::Gpu& gpu, const u16* clut, int x, int y) {
  gpu.load_image(psx::Rect{static_cast<s16>(x), static_cast<s16>(y), 256, 1}, clut);
  return static_cast<u16>(y << 6 | x >> 4);
}

u16 upload_texture(psx::Gpu& gpu, const u16* pixels, int depth, int semi, int x, int y, int width_px, int height) {
  int vram_width = width_px;
  if (depth == 0) vram_width >>= 2;
  if (depth == 1) vram_width >>= 1;
  gpu.load_image(psx::Rect{static_cast<s16>(x), static_cast<s16>(y), static_cast<s16>(vram_width),
                           static_cast<s16>(height)},
                 pixels);
  return static_cast<u16>((depth & 3) << 7 | (semi & 3) << 5 | (y & 0x100) >> 4 | (x & 0x3FF) >> 6 |
                          (y & 0x200) << 2);
}

std::optional<TextureBlock> parse_texture_block(std::span<const u8> b) {
  // The data is a u16 stream; copy-free views need 2-byte alignment, which the
  // vector allocations used for unpacked blocks provide.
  if (b.size() < 8 || reinterpret_cast<uintptr_t>(b.data()) % 2) return std::nullopt;
  const u16* w = reinterpret_cast<const u16*>(b.data());
  const size_t words = b.size() / 2;
  TextureBlock t;
  t.flags = w[0];
  t.depth = t.flags & 3;
  const size_t clut_len = w[1];
  size_t at = 2;
  if (at + clut_len + 2 > words) return std::nullopt;
  t.clut = std::span<const u16>(w + at, clut_len);
  at += clut_len;
  t.vram_width = w[at];
  t.height = w[at + 1];
  t.width_px = t.vram_width << (2 - (t.depth > 2 ? 2 : t.depth));
  at += 2;
  if (at + static_cast<size_t>(t.vram_width) * t.height > words) return std::nullopt;
  t.pixels = std::span<const u16>(w + at, static_cast<size_t>(t.vram_width) * t.height);
  return t;
}

std::optional<u16> vram_upload_packed_block(psx::Gpu& gpu, std::span<const u8> packed, int block, int x, int y,
                                            int clut_y) {
  auto unpacked = disc::slz::unpack_block(packed, 0, block, nullptr);
  if (!unpacked) return std::nullopt;
  const auto t = parse_texture_block(*unpacked);
  if (!t) return std::nullopt;
  // The original uploads a full 256-entry row whenever a palette is present.
  if (!t->clut.empty()) {
    std::vector<u16> row(256, 0);
    for (size_t i = 0; i < row.size() && i < t->clut.size(); ++i) row[i] = t->clut[i];
    upload_clut256(gpu, row.data(), 0, clut_y);
  }
  return upload_texture(gpu, t->pixels.data(), t->depth, 0, x, y, t->width_px, t->height);
}

std::optional<u16> vram_upload_packed_clut(psx::Gpu& gpu, std::span<const u8> packed, int block, int clut_y) {
  auto unpacked = disc::slz::unpack_block(packed, 0, block, nullptr);
  if (!unpacked || unpacked->size() < 0x14 + 512) return std::nullopt;
  std::vector<u16> row(256);
  for (size_t i = 0; i < row.size(); ++i) row[i] = read_u16(unpacked->data() + 0x14 + i * 2);
  return upload_clut256(gpu, row.data(), 0, clut_y);
}

}  // namespace ss::game
