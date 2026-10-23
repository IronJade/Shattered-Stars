#include "launcher/title_art.h"

#include <algorithm>
#include <string>

#include "disc/archive.h"
#include "disc/disc_image.h"
#include "disc/slz.h"
#include "game/common/texture.h"

namespace ss::launcher {
namespace {

constexpr u32 kTitleEntry = 0x0C;  // title_program.cpp loads it as entry12

// A PlayStation colour (5 bits each of red, green, blue) as RGBA8888. Colour 0 draws as
// black on the title (it is the clear colour behind the picture).
u32 to_rgba(u16 c) {
  auto expand = [](u32 v) { return (v << 3) | (v >> 2); };
  const u32 r = expand(c & 0x1F), g = expand((c >> 5) & 0x1F), b = expand((c >> 10) & 0x1F);
  return r | g << 8 | b << 16 | 0xFF000000u;
}

}  // namespace

std::optional<Picture> read_title_picture(const std::filesystem::path& disc1) {
  std::string error;
  auto disc = disc::DiscImage::open(disc1, &error);
  if (!disc) return std::nullopt;
  disc::Archive archive;
  if (!archive.open(*disc, &error) || !archive.has_entry(kTitleEntry)) return std::nullopt;
  const auto packed = archive.read_entry(kTitleEntry);
  if (!packed) return std::nullopt;
  const auto unpacked = disc::slz::unpack_block(*packed, 0, 0, &error);
  if (!unpacked) return std::nullopt;
  const auto block = game::parse_texture_block(*unpacked);
  if (!block || block->width_px <= 0 || block->height <= 0) return std::nullopt;

  Picture p;
  p.width = block->width_px;
  p.height = block->height;
  p.rgba.resize(static_cast<size_t>(p.width) * p.height);
  const auto& px = block->pixels;
  const int stride = block->vram_width;  // u16 words per row
  for (int y = 0; y < p.height; ++y) {
    for (int x = 0; x < p.width; ++x) {
      u16 colour = 0;
      if (block->depth == 2) {  // 16-bit direct colour
        colour = px[static_cast<size_t>(y) * stride + x];
      } else {
        const int per_word = block->depth == 0 ? 4 : 2;  // 4-bit or 8-bit indices
        const int bits = 16 / per_word;
        const u16 word = px[static_cast<size_t>(y) * stride + x / per_word];
        const u32 index = (word >> ((x % per_word) * bits)) & ((1u << bits) - 1);
        colour = index < block->clut.size() ? block->clut[index] : 0;
      }
      p.rgba[static_cast<size_t>(y) * p.width + x] = to_rgba(colour);
    }
  }
  return p;
}

Picture crop(const Picture& picture, int x, int y, int width, int height) {
  Picture out;
  x = std::clamp(x, 0, picture.width);
  y = std::clamp(y, 0, picture.height);
  out.width = std::clamp(width, 0, picture.width - x);
  out.height = std::clamp(height, 0, picture.height - y);
  out.rgba.resize(static_cast<size_t>(out.width) * out.height);
  for (int row = 0; row < out.height; ++row)
    std::copy_n(picture.rgba.begin() + static_cast<std::ptrdiff_t>((y + row) * picture.width + x), out.width,
                out.rgba.begin() + static_cast<std::ptrdiff_t>(row * out.width));
  return out;
}

}  // namespace ss::launcher
