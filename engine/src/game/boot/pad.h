#pragma once

#include "core/types.h"

// Controller state and the per-vblank clock kept by the boot executable
// (System + 4; globals around 0x8002b620-0x8002b6c0). Buttons use the game's
// layout (see platform::PadButton). Callers pass the pad object as the original
// does; there is only one, so it is unused here.
namespace ss::game {

struct Pad;

void pad_init(Pad* pad);                                      // 0x80013fb8
void pad_set_port_mask(Pad* pad, u8 mask);                    // 0x80014334 (bit n = port n polled)
void pad_set_repeat_delay(Pad* pad, u8 frames);               // 0x80014344
u16 pad_held(Pad* pad, int port);                             // 0x8001436c: buttons down since the last clear
u16 pad_pressed(Pad* pad, int port);                          // 0x80014384: new presses since the last clear
u16 pad_current(Pad* pad, int port);                          // 0x80014354: buttons down this vblank
u16 pad_pressed_repeat(Pad* pad, int port);                   // 0x8001439c: new presses, auto-repeating after the delay
void pad_clear(Pad* pad, int port);                           // 0x800143e8
void pad_set_option(Pad* pad, u8 value);                      // 0x8001440c (purpose unknown)
void clock_reset(Pad* pad);                                   // 0x8001441c: play-time clock to zero
void clock_set(Pad* pad, u32 seconds);                        // 0x80014444: play-time clock (a loaded game's)

// Vibration (DualShock actuators). The host has no rumble output yet; the settings are kept.
void pad_enable_vibration(Pad* pad, int port, bool on);        // 0x80013f84: the player's Vibration ON/OFF
bool pad_is_dualshock(Pad* pad, int port);                     // 0x80013f90: PadInfoMode(port, InfoModeCurExID) != 0
void pad_vibrate(Pad* pad, int port, int small, int large);   // 0x80013e14: actuator levels (0 / 0 stops)

// The console's VSync callback (0x80013578): runs vblank handlers, advances the
// clock and samples the controllers. 'buttons' is the host controller state.
void vsync_callback_with(u16 port0_buttons);
u32 clock_seconds();  // 0x8002b620

}  // namespace ss::game
