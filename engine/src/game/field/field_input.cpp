#include "game/field/field_input.h"

#include "game/field/field.h"

namespace ss::game::field {
namespace {

FieldAnalog g_analog;

}  // namespace

FieldAnalog& field_analog() { return g_analog; }

int field_pad_direction() {
  switch (globals().held[0] & 0xF000) {
    case 0x1000: return 0;  // up
    case 0x3000: return 1;  // up + right
    case 0x2000: return 2;  // right
    case 0x6000: return 3;  // right + down
    case 0x4000: return 4;  // down
    case 0xC000: return 5;  // down + left
    case 0x8000: return 6;  // left
    case 0x9000: return 7;  // left + up
    default: return -1;
  }
}

}  // namespace ss::game::field
