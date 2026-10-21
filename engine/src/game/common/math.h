#pragma once

#include "core/types.h"

namespace ss::game {

// Sine and cosine over a 4096-step circle, scaled by 4096 (title 0x8002f8e0 and
// 0x8002f974). The game reads a quarter-wave table of 1025 entries that equals
// round(4096 * sin(i * pi / 2048)) exactly, so it is computed here.
s32 sin4096(u32 angle);
s32 cos4096(u32 angle);

}  // namespace ss::game
