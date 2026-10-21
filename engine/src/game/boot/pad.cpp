#include "game/boot/pad.h"

namespace ss::game {
namespace {

// Mirrors the boot globals; names follow docs/catalog/globals.csv.
struct PadState {
  u16 current[2] = {};   // 0x8002b628
  u16 previous[2] = {};  // 0x8002b62c
  u16 held[2] = {};      // 0x8002b630
  u16 pressed[2] = {};   // 0x8002b634
  u8 hold_frames[2] = {};  // 0x8002b638
  u8 repeat_delay = 0;   // 0x8002b63a
  u8 port_mask = 0;      // 0x8002b63b
  u8 option = 0;         // 0x8002b63c
  u8 frames = 0;         // 0x8002b63d
  u32 seconds = 0;       // 0x8002b620
  u8 vibration[2] = {};  // Pad + 8 + port
};
PadState g_pad;

}  // namespace

void pad_init(Pad*) {
  g_pad = PadState{};
}

void pad_set_port_mask(Pad*, u8 mask) { g_pad.port_mask = mask; }
void pad_set_repeat_delay(Pad*, u8 frames) { g_pad.repeat_delay = frames; }
u16 pad_held(Pad*, int port) { return g_pad.held[port]; }
u16 pad_pressed(Pad*, int port) { return g_pad.pressed[port]; }
u16 pad_current(Pad*, int port) { return g_pad.current[port]; }

u16 pad_pressed_repeat(Pad*, int port) {
  return g_pad.hold_frames[port] < g_pad.repeat_delay ? g_pad.pressed[port] : g_pad.current[port];
}

void pad_clear(Pad*, int port) {
  g_pad.held[port] = 0;
  g_pad.pressed[port] = 0;
}

void pad_set_option(Pad*, u8 value) { g_pad.option = value; }

void clock_reset(Pad*) {
  g_pad.frames = 0;
  g_pad.seconds = 0;
}

u32 clock_seconds() { return g_pad.seconds; }

void pad_enable_vibration(Pad*, int port, bool on) { g_pad.vibration[port] = on; }

// The port emulates a digital controller (as PCSX-Redux does by default), so the
// game offers Vibration OFF first.
bool pad_is_dualshock(Pad*, int) { return false; }

void pad_vibrate(Pad*, int port, int small, int large) {
  if (!g_pad.vibration[port]) return;
  // TODO(port): host rumble (SDL_RumbleGamepad) for DualShock controllers.
  (void)small;
  (void)large;
}

void vsync_callback_with(u16 port0_buttons) {
  // TODO(port): registered vblank handlers (0x8002b5f0 list: sound, movies).
  if (++g_pad.frames > 59) {
    g_pad.frames = 0;
    ++g_pad.seconds;
  }
  // Port 0 is the host controller; port 1 reads as connected with nothing pressed.
  int port = 0;
  for (u8 mask = g_pad.port_mask; port < 2 && mask != 0; mask >>= 1, ++port) {
    if (!(mask & 1)) {
      g_pad.current[port] = 0;
      continue;
    }
    g_pad.previous[port] = g_pad.current[port];
    g_pad.current[port] = port == 0 ? port0_buttons : 0;
    if (g_pad.previous[port] == g_pad.current[port] && g_pad.previous[port] != 0) {
      if (g_pad.hold_frames[port] < g_pad.repeat_delay) ++g_pad.hold_frames[port];
    } else {
      g_pad.hold_frames[port] = 0;
    }
    g_pad.held[port] |= g_pad.current[port];
    g_pad.pressed[port] |= static_cast<u16>(g_pad.current[port] & ~g_pad.previous[port]);
  }
}

}  // namespace ss::game
