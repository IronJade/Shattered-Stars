#include "disc/slz.h"

#include <cstring>

namespace ss::disc::slz {
namespace {

bool fail(std::string* error, const char* message) {
  if (error) *error = message;
  return false;
}

// Shared decoder. Method 1 ends on a zero distance; method 2 adds run-length fills
// (length nibble 0xF) and ends once 'size' output bytes exist. The original copies
// with unrolled word moves when the distance is at least 16; the result equals a
// forward byte copy, which is what this does. Neither method checks the output
// against the declared size: a stream may run a few bytes past it (archive entry
// #2674's sequence block ends 3 bytes beyond; the console writes them into the heap
// slack after the buffer). Those bytes are kept here, so 'out' can be longer than
// 'size'; a method 1 stream that ends early leaves the rest zero.
bool lzss(const u8* src, size_t src_len, std::vector<u8>& out, size_t size, bool rle, std::string* error) {
  out.assign(size, 0);
  size_t s = 0, d = 0;
  unsigned flags = 0, bits = 0;
  auto room = [&](size_t n) {
    if (d + n > out.size()) out.resize(d + n, 0);
  };
  for (;;) {
    if (rle && d >= size) return true;
    if (bits == 0) {
      if (s >= src_len) return fail(error, "SLZ: input ended inside the stream");
      flags = src[s++];
      bits = 8;
    }
    const bool literal = flags & 1;
    flags >>= 1;
    --bits;
    if (literal) {
      if (s >= src_len) return fail(error, "SLZ: input ended inside a literal");
      room(1);
      out[d++] = src[s++];
      continue;
    }
    if (s + 1 >= src_len) return fail(error, "SLZ: input ended inside a reference");
    const u8 b0 = src[s], b1 = src[s + 1];
    const size_t distance = b0 | (b1 & 0x0F) << 8;
    const unsigned nibble = b1 >> 4;
    if (!rle && distance == 0) return true;
    if (rle && nibble == 0x0F) {
      size_t count;
      u8 value;
      if (distance < 0x100) {
        if (s + 2 >= src_len) return fail(error, "SLZ: input ended inside a fill");
        value = src[s + 2];
        count = b0 + 0x13;
        s += 3;
      } else {
        value = b0;
        count = (b1 & 0x0F) + 3;
        s += 2;
      }
      room(count);
      std::memset(out.data() + d, value, count);
      d += count;
      continue;
    }
    const size_t length = nibble + 3;
    if (distance > d) return fail(error, "SLZ: reference before the start of the output");
    room(length);
    for (size_t k = 0; k < length; ++k, ++d) out[d] = out[d - distance];
    s += 2;
  }
}

}  // namespace

std::optional<BlockHeader> header_at(std::span<const u8> data, size_t offset) {
  if (offset + 16 > data.size()) return std::nullopt;
  const u8* p = data.data() + offset;
  if (p[0] != 'S' || p[1] != 'L' || p[2] != 'Z' || p[3] > 2) return std::nullopt;
  return BlockHeader{offset, p[3], read_u32(p + 4), read_u32(p + 8), read_u32(p + 12)};
}

std::optional<BlockHeader> find_block(std::span<const u8> data, size_t offset, int block) {
  auto header = header_at(data, offset);
  for (int i = 0; header && i < block; ++i) {
    if (header->next == 0) return std::nullopt;
    header = header_at(data, header->offset + header->next);
  }
  return header;
}

bool unpack(std::span<const u8> data, size_t offset, std::vector<u8>& out, std::string* error) {
  const auto header = header_at(data, offset);
  if (!header) return fail(error, "SLZ: no block header");
  const u8* body = data.data() + offset + 16;
  const size_t available = data.size() - offset - 16;
  switch (header->method) {
    case 0:
      if (header->unpacked > available) return fail(error, "SLZ: stored block is truncated");
      out.assign(body, body + header->unpacked);
      return true;
    case 1:
      return lzss(body, available, out, header->unpacked, false, error);
    default:
      return lzss(body, available, out, header->unpacked, true, error);
  }
}

std::optional<std::vector<u8>> unpack_block(std::span<const u8> data, size_t offset, int block,
                                            std::string* error) {
  const auto header = find_block(data, offset, block);
  if (!header) {
    if (error) *error = "SLZ: block not found";
    return std::nullopt;
  }
  std::vector<u8> out;
  if (!unpack(data, header->offset, out, error)) return std::nullopt;
  return out;
}

}  // namespace ss::disc::slz
