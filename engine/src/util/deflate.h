#pragma once

#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "core/types.h"

// A small deflate compressor (port feature; no zlib in the build): LZ77 over a 32 KiB
// window with hash chains, dynamic Huffman blocks (RFC 1951), wrapped as a zlib stream
// (RFC 1950). Deterministic: the same input always gives the same bytes. Used for the
// compressed PNGs of the tracker maps (tools/tracker_maps.h); util/png.h stays the
// uncompressed writer the captures use.
namespace ss::util {

std::vector<u8> zlib_compress(std::span<const u8> data);

// An 8-bit RGB PNG (pixels RGBA8888 with red in the low byte; alpha is dropped), each row
// filtered with the PNG filter of the smallest sum of absolute differences, then deflated.
std::vector<u8> encode_png_rgb(std::span<const u32> pixels, int width, int height);
bool write_png_rgb(const std::filesystem::path& path, std::span<const u32> pixels, int width, int height,
                   std::string* error);

}  // namespace ss::util
