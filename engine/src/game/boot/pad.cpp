#include "game/boot/pad.h"

#include <algorithm>
#include <cstring>

#include "runtime/runtime.h"

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
  u8 link[2] = {};       // 0x8002b640: libpad connection (1 searching, 2 identified)
  u8 analog[2] = {};     // 0x8002b69c: in analog mode this vblank
  u8 axes[2][4] = {};    // 0x8002b6a0: raw stick bytes (right X, right Y, left X, left Y)
  s8 centre[2][4] = {};  // 0x8002b6a8
  u8 dead_zone[2] = {};  // 0x8002b6b0
  u8 range[2][4] = {};   // 0x8002b6b4
  u8 stick_dpad[2] = {}; // 0x8002b6bc
  bool host_gamepad = false;  // port 0 reads as a DualShock (a host gamepad is open)
};
PadState g_pad;

// Port feature: the player's stick settings (setup's Controls page), kept over the game's
// own pad_set_stick_* calls; -1 leaves the game's value.
struct PlayerStick {
  int dead_zone = -1;
  int dpad = -1;
};
PlayerStick g_player_stick;

int stick_dpad_mode(int port) { return g_player_stick.dpad >= 0 ? g_player_stick.dpad : g_pad.stick_dpad[port]; }

// 0x8002b5ec / 0x8002b5f0 / 0x8002b608: the vblank handler list.
struct HandlerList {
  s32 count = 0;
  VblankHandler* handler[6] = {};
  s32 arg[6] = {};
};
HandlerList g_handlers;

}  // namespace

bool pad_add_vblank_handler(Pad*, VblankHandler* h, s32 arg) {
  if (g_handlers.count >= 6) return false;
  g_handlers.arg[g_handlers.count] = arg;
  g_handlers.handler[g_handlers.count] = h;
  ++g_handlers.count;
  h->added();
  return true;
}

void pad_remove_vblank_handler(Pad*, VblankHandler* h) {
  for (int i = 0; i < g_handlers.count; ++i) {
    if (g_handlers.handler[i] != h) continue;
    for (int j = i; j < g_handlers.count - 1; ++j) {
      g_handlers.arg[j] = g_handlers.arg[j + 1];
      g_handlers.handler[j] = g_handlers.handler[j + 1];
    }
    --g_handlers.count;
  }
}

void pad_init(Pad*) {
  g_pad = PadState{};
  g_handlers.count = 0;
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

void clock_set(Pad*, u32 seconds) {
  g_pad.seconds = seconds;
  g_pad.frames = 0;
}

u32 clock_seconds() { return g_pad.seconds; }

void pad_enable_vibration(Pad*, int port, bool on) { g_pad.vibration[port] = on; }

// Port 0 reads as a DualShock while the window has a gamepad open; otherwise (keyboard,
// headless runs) as a digital controller, as PCSX-Redux's default, so the game offers
// Vibration OFF first.
bool pad_is_dualshock(Pad*, int port) { return port == 0 && g_pad.host_gamepad; }

void pad_vibrate(Pad*, int port, int small, int large) {
  if (!g_pad.vibration[port] || port != 0) return;
  runtime::Runtime::get().host_rumble(small, large);
}

void pad_driver_start(Pad*) {
  for (int port = 0; port < 2; ++port) {
    g_pad.vibration[port] = 1;
    g_pad.stick_dpad[port] = 1;
    g_pad.link[port] = 1;
  }
}

void pad_set_stick_centre(Pad*, int port, s8 c0, s8 c1, s8 c2, s8 c3, u8 dead_zone) {
  const s8 c[4] = {c0, c1, c2, c3};
  std::memcpy(g_pad.centre[port], c, 4);
  g_pad.dead_zone[port] = dead_zone;
}

void pad_set_stick_range(Pad*, int port, u8 r0, u8 r1, u8 r2, u8 r3) {
  const u8 r[4] = {r0, r1, r2, r3};
  std::memcpy(g_pad.range[port], r, 4);
}

void pad_set_stick_dpad(Pad*, int port, u8 mode) { g_pad.stick_dpad[port] = mode; }

void pad_set_player_stick(int dead_zone, int dpad) {
  g_player_stick.dead_zone = dead_zone < 0 ? -1 : std::min(dead_zone, 0x7F);
  g_player_stick.dpad = dpad < 0 ? -1 : dpad != 0;
}

bool pad_stick(Pad*, int port, s32* rx, s32* ry, s32* lx, s32* ly) {
  s32* out[4] = {rx, ry, lx, ly};
  if (!g_pad.analog[port]) {
    for (s32* o : out) *o = 0;
    return false;
  }
  const s32 dead = g_player_stick.dead_zone >= 0 ? g_player_stick.dead_zone : g_pad.dead_zone[port];
  for (int i = 0; i < 4; ++i) {
    const s32 d = g_pad.axes[port][i] - (g_pad.centre[port][i] + 0x80);
    s32 v = (d < 0 ? -d : d) < dead ? 0 : d < 1 ? d + dead : d - dead;
    const s32 r = g_pad.range[port][i];
    if (r < v) v = r;
    if (v < -r) v = -r;
    *out[i] = v;
  }
  return true;
}

void vsync_callback_with(u16 port0_buttons, const u8* port0_sticks) {
  for (int i = 0; i < g_handlers.count; ++i) g_handlers.handler[i]->vblank(g_handlers.arg[i]);
  if (++g_pad.frames > 59) {
    g_pad.frames = 0;
    ++g_pad.seconds;
  }
  // A host gamepad reads as a DualShock in analog mode (ID 7), the keyboard alone as a
  // digital pad (ID 4). When libpad identifies a DualShock (state 6) the original sets up
  // the stick (for port 0, whichever port it was), so it happens here as a gamepad appears.
  if (port0_sticks == nullptr) {
    g_pad.host_gamepad = false;
  } else if (!g_pad.host_gamepad) {
    g_pad.host_gamepad = true;
    pad_set_stick_centre(nullptr, 0, 0, 0, 0, 0, 0x20);
    pad_set_stick_range(nullptr, 0, 0x50, 0x50, 0x50, 0x50);
    pad_set_stick_dpad(nullptr, 0, 1);
  }
  // Port 0 is the host controller; port 1 reads as connected with nothing pressed.
  int port = 0;
  for (u8 mask = g_pad.port_mask; port < 2 && mask != 0; mask >>= 1, ++port) {
    if (!(mask & 1)) {
      g_pad.current[port] = 0;
      g_pad.analog[port] = 0;
      continue;
    }
    g_pad.previous[port] = g_pad.current[port];
    u16 buttons = port == 0 ? port0_buttons : 0;
    const u8* sticks = port == 0 ? port0_sticks : nullptr;
    g_pad.analog[port] = sticks != nullptr;
    if (sticks != nullptr) {
      std::memcpy(g_pad.axes[port], sticks, 4);
      if (stick_dpad_mode(port) != 0) {
        // The left stick past half its range presses the d-pad (mode 2: only the stick).
        if (stick_dpad_mode(port) == 2) buttons &= 0x0FFF;
        s32 rx, ry, lx, ly;
        if (pad_stick(nullptr, port, &rx, &ry, &lx, &ly)) {
          const s32 hx = g_pad.range[port][2] >> 1, hy = g_pad.range[port][3] >> 1;
          if (lx <= -hx) buttons |= 0x8000;  // left
          if (hx <= lx) buttons |= 0x2000;   // right
          if (ly <= -hy) buttons |= 0x1000;  // up
          if (hy <= ly) buttons |= 0x4000;   // down
        }
      }
    }
    g_pad.current[port] = buttons;
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
