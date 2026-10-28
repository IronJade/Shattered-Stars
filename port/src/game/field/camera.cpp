#include "game/field/camera.h"

#include <cstring>
#include <memory>

#include "game/field/actor_base.h"
#include "game/field/actor_field.h"
#include "game/field/actors.h"
#include "game/field/bg.h"
#include "game/field/fade.h"
#include "game/field/field.h"
#include "game/field/map.h"
#include "game/field/script_vm.h"

namespace ss::game::field {
namespace {

CameraBlock g_camera;
std::unique_ptr<CamScript> g_script;  // 0x800752b4

const u8* scene() { return map_data().scene.data(); }
s32 mul(s32 a, s32 b) { return static_cast<s32>(static_cast<u32>(a) * static_cast<u32>(b)); }

}  // namespace

CameraBlock& camera_block() { return g_camera; }

void camera_reset() {
  // On the world map the original first sets up the GTE (H = 0x80075764 * zoom, offset
  // (160, 112), far colour, fog 0xe00..50000). The port does that in world_camera_reset
  // (worldmap_camera.cpp), which then calls this. The original also gets here after a
  // menu on the world map (actor_sys_setup_view with no scene); PCSX-Redux shows no
  // lasting change there (the world camera rewrites the block every frame), and the
  // port's actor_sys_setup_view returns early without a scene.
  const bool world = globals().field_flags != nullptr && globals().field_flags[0x68] != 0;
  g_camera.look_at = 0;
  for (int i = 0; i < 3; ++i) g_camera.eye[i] = g_camera.target[i] = 0;
  g_camera.zoom = world ? 0xCCC : 0x1000;
  // 0x80075aba..0x80075abe: the BG tint colour (the fade module's copy).
  for (s16& t : g_fade_targets.bg_tint) t = 0x80;
  for (u8& c : g_camera.ambient) c = 0x1E;
  g_camera.distance = 0;
  for (int i = 0; i < 3; ++i) {
    g_camera.fx_scale[i] = 0x1000;     // 0x800746fc
    g_camera.fx_light_params[i] = 0;   // 0x800746ec
  }
}

// camera_update_matrices (0x800603a4) is in effects_camera.cpp with the rest of the
// camera block's matrix code.

CamScript* camscript() { return g_script.get(); }

CamScript* camscript_create() {
  if (g_script) camscript_destroy(g_script.get(), 0);
  g_script = std::make_unique<CamScript>();
  camscript_construct(g_script.get());
  camscript_reset(g_script.get());
  return g_script.get();
}

CamScript* camscript_construct(CamScript* c) {
  for (int i = 0; i < 3; ++i) c->cur[i] = c->step[i] = c->target[i] = 0;
  c->read = c->write = 0;
  c->remaining = 0;
  task_register(c, 1);
  return c;
}

void camscript_destroy(CamScript* c, int flags) {
  task_unregister(c);
  if ((flags & 1) && c == g_script.get()) g_script.reset();
}

void camscript_reset(CamScript* c) {
  c->read = c->write = 0;
  c->remaining = 0;
}

void camscript_push_point(CamScript* c, s32 x, s32 y, s32 z, s32 frames, s32 speed, s32 accel, s32 min_speed,
                              s32 max_speed) {
  camscript_push(c, x << 12, y << 12, z << 12, frames, speed, accel, min_speed, max_speed, 1);
}

void camscript_push_bg(CamScript* c, s32 x, s32 y, s32 frames, s32 speed, s32 accel, s32 min_speed,
                           s32 max_speed) {
  camscript_push(c, x, y, 0, frames, speed, accel, min_speed, max_speed, 0);
}

void camscript_push_actor(CamScript* c, int slot, s32 frames, s32 speed, s32 accel, s32 min_speed,
                              s32 max_speed) {
  const Actor* a = field_actor(slot);
  if (a == nullptr) return;
  camscript_push(c, a->x, a->y, a->z, frames, speed, accel, min_speed, max_speed, 1);
}

void camscript_push(CamScript* c, s32 x, s32 y, s32 z, s32 frames, s32 speed, s32 accel, s32 min_speed,
                        s32 max_speed, s32 mode) {
  if (((c->write + 1) & 7) == c->read) return;
  CamMove& m = c->moves[c->write];
  m.mode = mode;
  m.max_speed = max_speed;
  m.frames = frames;
  m.speed = speed;
  m.accel = accel;
  m.min_speed = min_speed;
  m.x = x;
  m.y = y;
  m.z = z;
  c->write = static_cast<u8>((c->write + 1) & 7);
}

void camscript_start_move(CamScript* c) {
  const CamMove& m = c->moves[c->read];
  BgRenderer* bg = bg_renderer();
  c->speed = m.speed;
  c->min_speed = m.min_speed != 0 ? m.min_speed : m.speed;
  c->accel = m.accel;
  c->braking = static_cast<s32>(static_cast<u32>(m.accel) >> 31);
  c->remaining = m.frames << 8;
  c->half = m.frames << 7;
  c->brake_at = -1;
  c->max_speed = m.max_speed;
  const s32 centre_y = bg != nullptr && bg->vertical_scroll ? 0x70 : 0x78;
  s16 cam[2] = {0, 0};
  if (m.mode == 1) {
    bg_get_view_origin(bg_renderer(), cam);
    c->cur[0] = (cam[0] + 0xA0) << 8;
    c->cur[1] = (cam[1] + centre_y) << 8;
    if (bg != nullptr) bg->floor_y = m.y;
    VmVector in{m.x, m.y, m.z, 0}, out{};
    s32 flag;
    TransRot_32(&in, &out, &flag);
    const s32 d = out.vz >> 4;
    const u16 w = read_u16(scene() + 0x34), h = read_u16(scene() + 0x36);
    const s32 H = read_u16(scene() + 0x6A);
    s32 sx, sy;
    if (d == 0) {
      sx = w >> 1;
      sy = h >> 1;
    } else {
      sx = ((out.vx >> 4) * H + (out.vz >> 5)) / d + (w >> 1);
      sy = ((out.vy >> 4) * H + (out.vz >> 5)) / d + (h >> 1);
    }
    c->target[0] = sx;
    c->target[1] = sy - 0x10;
    c->target[2] = 0;
  } else {
    c->target[0] = m.x;
    c->target[1] = m.y;
    c->target[2] = 0;
    bg_get_view_origin(bg_renderer(), cam);
    c->cur[0] = static_cast<s16>(cam[0] + 0xA0) << 8;
    c->cur[1] = static_cast<s16>(cam[1] + centre_y) << 8;
    c->cur[2] = 0;
  }
  const s32 ox = static_cast<s32>(read_u32(scene() + 0x28)), oy = static_cast<s32>(read_u32(scene() + 0x2C));
  if (c->target[0] < ox + 0xA0) c->target[0] = ox + 0xA0;
  if (c->target[1] < oy + centre_y) c->target[1] = oy + centre_y;
  const s32 max_x = ox + read_u16(scene() + 4) - 0xA0;
  if (max_x < c->target[0]) c->target[0] = max_x;
  const s32 max_y = (oy + read_u16(scene() + 6)) - centre_y;
  if (max_y < c->target[1]) c->target[1] = max_y;
  c->target[0] <<= 8;
  c->target[1] <<= 8;
  const s32 n = m.frames != 0 ? m.frames : 1;
  for (int i = 0; i < 3; ++i) c->step[i] = (c->target[i] - c->cur[i]) / n;
}

void camscript_update(CamScript* c) {
  const s32 ticks = globals().frame_ticks;
  if (ticks == 0 || c->write == c->read) return;
  if (c->remaining < 1) {
    camscript_start_move(c);
    const s16 none = -1;
    std::memcpy(globals().field_flags + 0xE, &none, 2);
  }
  c->remaining -= ticks * c->speed;
  const s32 brake_point = c->remaining - (c->half - c->brake_at);
  if (c->braking != 0) {
    c->speed += c->accel * ticks;
  } else if (c->speed < c->max_speed || c->remaining < brake_point) {
    if (c->min_speed < 0) {
      if (c->half < c->remaining) c->speed += c->accel * ticks;
    } else if (c->half < c->remaining) {
      c->speed += c->accel * ticks;
    } else {
      c->speed += -c->accel * ticks;
    }
  }
  if (c->speed >= c->max_speed) {
    c->speed = c->max_speed;
    if (c->brake_at == -1) c->brake_at = c->remaining;
  }
  if (c->speed < c->min_speed) c->speed = c->min_speed;
  s16 fx, fy;
  if (c->remaining > 0) {
    const s32 d = c->speed * ticks;
    c->cur[0] += mul(c->step[0], d) >> 8;
    c->cur[1] += mul(c->step[1], d) >> 8;
    fx = static_cast<s16>(c->cur[0] >> 8);
    fy = static_cast<s16>(c->cur[1] >> 8);
  } else {
    fx = static_cast<s16>(c->target[0] >> 8);
    fy = static_cast<s16>(c->target[1] >> 8);
    c->read = static_cast<u8>((c->read + 1) & 7);
  }
  if (BgRenderer* bg = bg_renderer()) {
    bg->focus_x = fx;
    bg->focus_y = fy;
    bg_update_scroll(bg);
  }
}

void CamScript::update() { camscript_update(this); }

int camscript_busy(const CamScript* c) { return c->write != c->read ? 1 : 0; }

}  // namespace ss::game::field
