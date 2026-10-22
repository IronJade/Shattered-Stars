#pragma once

#include <span>

#include "core/types.h"

// VRAM uploads of the battle loaders. The console passes raw pointers into the
// unpacked blocks and lets the GPU read past short CLUT tables; the port copies what
// the block holds and zero-fills the rest so a short block never reads outside its
// buffer.
namespace ss::game::battle {

// boot 0x80012ec4 upload_clut256: 256 colours from data[offset] to (x, y); the CLUT id.
u16 upload_clut256(std::span<const u8> data, size_t offset, int x, int y);
u16 upload_clut256(const u16* colours, int x, int y);
// boot 0x80012e04 upload_texture: depth 0/1/2, width in pixels; the tpage id.
u16 upload_texture(std::span<const u8> data, size_t offset, int depth, int semi, int x, int y, int width_px,
                   int height);
// 0x80084a84 clut_build_pair: dst[i] = src[i], dst[0x80 + i] = src[i] | 0x8000 (0 stays 0)
// for i < min(count, 128); the rest zero.
void clut_build_pair(u16 dst[256], std::span<const u8> src, size_t offset, int count);

}  // namespace ss::game::battle
