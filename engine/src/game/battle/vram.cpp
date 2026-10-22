#include "game/battle/vram.h"

#include <algorithm>
#include <cstring>
#include <vector>

#include "game/common/texture.h"
#include "runtime/runtime.h"

namespace ss::game::battle {

u16 upload_clut256(const u16* colours, int x, int y) {
  return ss::game::upload_clut256(runtime::Runtime::get().gpu, colours, x, y);
}

u16 upload_clut256(std::span<const u8> data, size_t offset, int x, int y) {
  u16 colours[256] = {};
  if (offset < data.size()) std::memcpy(colours, data.data() + offset, std::min<size_t>(512, data.size() - offset));
  return upload_clut256(colours, x, y);
}

u16 upload_texture(std::span<const u8> data, size_t offset, int depth, int semi, int x, int y, int width_px,
                   int height) {
  int vram_width = width_px;
  if (depth == 0) vram_width >>= 2;
  if (depth == 1) vram_width >>= 1;
  const size_t bytes = static_cast<size_t>(std::max(vram_width, 0)) * static_cast<size_t>(std::max(height, 0)) * 2;
  std::vector<u16> pixels((bytes + 1) / 2, 0);
  if (offset < data.size()) std::memcpy(pixels.data(), data.data() + offset, std::min(bytes, data.size() - offset));
  return ss::game::upload_texture(runtime::Runtime::get().gpu, pixels.data(), depth, semi, x, y, width_px, height);
}

void clut_build_pair(u16 dst[256], std::span<const u8> src, size_t offset, int count) {
  if (count > 0x80) count = 0x80;
  int i = 0;
  for (; i < count; ++i) {
    u16 c = 0;
    if (offset + 2 * static_cast<size_t>(i) + 2 <= src.size()) std::memcpy(&c, src.data() + offset + 2 * i, 2);
    dst[i] = c;
    dst[0x80 + i] = c == 0 ? 0 : static_cast<u16>(c | 0x8000);
  }
  for (; i < 0x80; ++i) dst[i] = dst[0x80 + i] = 0;
}

}  // namespace ss::game::battle
