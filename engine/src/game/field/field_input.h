#pragma once

#include "core/types.h"

// Player input as the field's actor code reads it: the d-pad from the pad state the
// main loop latches each frame (globals().held[0], 0x80075748), and the analog stick
// the main loop reads through the pad module (FUN_80013cb8: 0x800762fc..0x8007630c).
namespace ss::game::field {

struct FieldAnalog {
  u8 enabled = 0;   // 0x800762fc: an analog pad in analog mode
  s32 rx = 0;       // 0x80076300: right stick
  s32 ry = 0;       // 0x80076304
  s32 x = 0;        // 0x80076308: left stick deflection (dead zone applied by the pad module)
  s32 y = 0;        // 0x8007630c
};
FieldAnalog& field_analog();

// 0x80042d90: the held d-pad as one of 8 directions (0 up, 1 up-right, 2 right,
// 3 down-right, 4 down, 5 down-left, 6 left, 7 up-left), -1 for none or other
// combinations. The direction indexes the scene's control angles (MapScene +0xcc).
int field_pad_direction();

}  // namespace ss::game::field
