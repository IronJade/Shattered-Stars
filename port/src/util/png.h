#pragma once

#include <filesystem>
#include <span>

#include "core/types.h"

namespace ss::util {

// Writes an 8-bit RGBA PNG (pixels are RGBA8888 with red in the low byte), using
// uncompressed deflate blocks so no compression library is needed.
bool write_png(const std::filesystem::path& path, std::span<const u32> pixels, int width, int height);

}  // namespace ss::util
