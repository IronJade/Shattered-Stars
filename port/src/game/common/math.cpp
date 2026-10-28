#include "game/common/math.h"

#include <array>
#include <cmath>

namespace ss::game {
namespace {

const std::array<u16, 1025>& quarter_table() {
  static const auto table = [] {
    std::array<u16, 1025> t{};
    for (int i = 0; i <= 1024; ++i)
      t[i] = static_cast<u16>(std::lround(4096.0 * std::sin(3.14159265358979323846 / 2.0 * i / 1024.0)));
    return t;
  }();
  return table;
}

}  // namespace

s32 sin4096(u32 angle) {
  const auto& t = quarter_table();
  const u32 a = angle & 0xFFF;
  if (a <= 0x400) return t[a];
  if (a < 0x800) return t[0x800 - a];
  if (a <= 0xC00) return -t[a - 0x800];
  return -t[0x1000 - a];
}

s32 cos4096(u32 angle) {
  const auto& t = quarter_table();
  const u32 a = angle & 0xFFF;
  if (a < 0x400) return t[0x400 - a];
  if (a < 0x800) return -t[a - 0x400];
  if (a < 0xC00) return -t[0xC00 - a];
  return t[a - 0xC00];
}

}  // namespace ss::game
