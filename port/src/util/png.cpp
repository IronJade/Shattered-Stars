#include "util/png.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <vector>

namespace ss::util {
namespace {

u32 crc32(const u8* data, size_t len, u32 crc = 0) {
  static const auto table = [] {
    std::array<u32, 256> t{};
    for (u32 i = 0; i < 256; ++i) {
      u32 c = i;
      for (int k = 0; k < 8; ++k) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      t[i] = c;
    }
    return t;
  }();
  crc = ~crc;
  for (size_t i = 0; i < len; ++i) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
  return ~crc;
}

void put_u32be(std::vector<u8>& out, u32 v) {
  out.push_back(v >> 24);
  out.push_back(v >> 16 & 0xFF);
  out.push_back(v >> 8 & 0xFF);
  out.push_back(v & 0xFF);
}

void chunk(std::vector<u8>& out, const char* type, const std::vector<u8>& body) {
  put_u32be(out, static_cast<u32>(body.size()));
  const size_t start = out.size();
  out.insert(out.end(), type, type + 4);
  out.insert(out.end(), body.begin(), body.end());
  put_u32be(out, crc32(out.data() + start, out.size() - start));
}

}  // namespace

bool write_png(const std::filesystem::path& path, std::span<const u32> pixels, int width, int height) {
  std::vector<u8> raw;
  raw.reserve(static_cast<size_t>(height) * (width * 4 + 1));
  for (int y = 0; y < height; ++y) {
    raw.push_back(0);  // filter: none
    for (int x = 0; x < width; ++x) {
      const u32 p = pixels[static_cast<size_t>(y) * width + x];
      raw.push_back(p & 0xFF);
      raw.push_back(p >> 8 & 0xFF);
      raw.push_back(p >> 16 & 0xFF);
      raw.push_back(p >> 24 & 0xFF);
    }
  }
  // zlib stream of stored (uncompressed) deflate blocks.
  std::vector<u8> z = {0x78, 0x01};
  for (size_t at = 0; at < raw.size() || raw.empty();) {
    const size_t n = std::min<size_t>(65535, raw.size() - at);
    const bool last = at + n == raw.size();
    z.push_back(last ? 1 : 0);
    z.push_back(n & 0xFF);
    z.push_back(n >> 8);
    z.push_back(~n & 0xFF);
    z.push_back(~n >> 8 & 0xFF);
    z.insert(z.end(), raw.begin() + at, raw.begin() + at + n);
    at += n;
    if (last) break;
  }
  u32 a = 1, b = 0;
  for (u8 c : raw) {
    a = (a + c) % 65521;
    b = (b + a) % 65521;
  }
  put_u32be(z, b << 16 | a);

  std::vector<u8> png = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
  std::vector<u8> ihdr;
  put_u32be(ihdr, width);
  put_u32be(ihdr, height);
  ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0});  // 8-bit RGBA, no interlace
  chunk(png, "IHDR", ihdr);
  chunk(png, "IDAT", z);
  chunk(png, "IEND", {});
  std::ofstream f(path, std::ios::binary);
  f.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
  return static_cast<bool>(f);
}

}  // namespace ss::util
