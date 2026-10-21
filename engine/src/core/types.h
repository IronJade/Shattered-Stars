#pragma once

#include <cstddef>
#include <cstdint>

namespace ss {

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using s8 = std::int8_t;
using s16 = std::int16_t;
using s32 = std::int32_t;
using s64 = std::int64_t;

// Little-endian reads from game data. The PS1 is little-endian and so are the
// supported hosts, but going through bytes keeps unaligned reads well-defined.
inline u16 read_u16(const u8* p) { return static_cast<u16>(p[0] | p[1] << 8); }
inline u32 read_u32(const u8* p) {
  return static_cast<u32>(p[0]) | static_cast<u32>(p[1]) << 8 | static_cast<u32>(p[2]) << 16 |
         static_cast<u32>(p[3]) << 24;
}

}  // namespace ss
