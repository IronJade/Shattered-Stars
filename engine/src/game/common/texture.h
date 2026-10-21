#pragma once

#include <optional>
#include <span>

#include "core/types.h"
#include "psx/gpu.h"

namespace ss::game {

// boot 0x80012ec4: uploads a 256x1 palette row and returns its CLUT id.
u16 upload_clut256(psx::Gpu& gpu, const u16* clut, int x, int y);

// boot 0x80012e04: uploads an image whose width is given in pixels (depth 0 = 4-bit,
// 1 = 8-bit, 2 = 16-bit) and returns the texture page id (PsyQ GetTPage packing).
u16 upload_texture(psx::Gpu& gpu, const u16* pixels, int depth, int semi, int x, int y, int width_px,
                   int height);

// Parsed texture block (the unpacked contents of one SLZ block in a texture pack).
struct TextureBlock {
  int depth = 0;           // 0 = 4-bit, 1 = 8-bit, 2 = 16-bit
  u16 flags = 0;
  std::span<const u16> clut;  // empty when the block carries no palette
  int vram_width = 0;      // in 16-bit VRAM units
  int width_px = 0;
  int height = 0;
  std::span<const u16> pixels;
};
std::optional<TextureBlock> parse_texture_block(std::span<const u8> unpacked);

// title 0x8003ca94: unpacks block 'block' of a texture pack, uploads its palette to
// (0, clut_y) and its pixels to (x, y); returns the texture page id.
std::optional<u16> vram_upload_packed_block(psx::Gpu& gpu, std::span<const u8> packed, int block, int x, int y,
                                            int clut_y);

// title 0x8003cb98: block 'block' is a TIM; uploads only its palette (at TIM offset
// 0x14) to (0, clut_y) and returns the CLUT id.
std::optional<u16> vram_upload_packed_clut(psx::Gpu& gpu, std::span<const u8> packed, int block, int clut_y);

}  // namespace ss::game
