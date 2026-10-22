#include "game/boot/pad.h"
#include "test.h"

using namespace ss;

TEST(pad_stick_and_stick_dpad) {
  // 0x80013cb8 / the stick part of 0x80013578, with the title's set-up (dead zone 0x20,
  // range 0x50, mode 1).
  game::Pad* pad = nullptr;
  game::pad_init(pad);
  game::pad_set_port_mask(pad, 1);
  s32 rx, ry, lx, ly;
  game::vsync_callback_with(0);  // a digital pad: no stick
  CHECK(!game::pad_stick(pad, 0, &rx, &ry, &lx, &ly));
  CHECK(!game::pad_is_dualshock(pad, 0));
  // A gamepad appearing reads as an identified DualShock: the stick is set up.
  const u8 centred[4] = {0x80, 0x80, 0x80, 0x80};
  game::vsync_callback_with(0, centred);
  CHECK(game::pad_is_dualshock(pad, 0));
  CHECK(game::pad_stick(pad, 0, &rx, &ry, &lx, &ly));
  CHECK_EQ(lx, 0);
  CHECK_EQ(game::pad_current(pad, 0), 0);
  // Inside the dead zone; past it less the dead zone; clamped to the range.
  const u8 tilt[4] = {0x80 + 0x1F, 0x80 - 0x30, 0x00, 0xFF};
  game::vsync_callback_with(0, tilt);
  game::pad_stick(pad, 0, &rx, &ry, &lx, &ly);
  CHECK_EQ(rx, 0);
  CHECK_EQ(ry, -0x10);
  CHECK_EQ(lx, -0x50);
  CHECK_EQ(ly, 0x50);
  CHECK_EQ(game::pad_current(pad, 0), 0x8000 | 0x4000);  // left + down
  // Half the range is the d-pad threshold: 0x20 + 0x28 from the centre.
  const u8 edge[4] = {0x80, 0x80, 0x80 + 0x48, 0x80 - 0x47};
  game::vsync_callback_with(0x0008, edge);
  CHECK_EQ(game::pad_current(pad, 0), 0x2000 | 0x0008);  // right, not up
  // Mode 2 drops the d-pad's own bits.
  game::pad_set_stick_dpad(pad, 0, 2);
  game::vsync_callback_with(0x1000, centred);
  CHECK_EQ(game::pad_current(pad, 0), 0);
  game::vsync_callback_with(0);
  CHECK(!game::pad_is_dualshock(pad, 0));
  game::pad_init(pad);
}
