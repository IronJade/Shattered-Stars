// World-map camera: WorldCam (overlay #3102 0x8007e000-0x8007ec38), the field camera
// block functions in their world-map form (camera_reset 0x8005f8d8, camera_compute_view
// 0x8005fb2c, camera_init_light_matrices 0x8005fa54, camera_update_matrices
// 0x800603a4), the per-object matrices (0x8007ec38) and the follow camera (0x8008bed8,
// 0x8008c644).
#include <cstring>

#include "game/field/actors.h"
#include "game/field/camera.h"
#include "game/field/fade.h"
#include "game/field/field.h"
#include "game/field/map.h"
#include "game/field/worldmap.h"
#include "game/field/worldmap_internal.h"

namespace ss::game::field {
using namespace wm;

namespace {

WorldView& view() { return world().view; }

// field 0x8006ec84: rotation about x (m12 = sin, m21 = -sin); translation untouched.
void rot_x(s32 a, psyq::MATRIX* m) {
  const s32 c = rcos(a), s = rsin(a);
  m->m[0][0] = 0x1000;
  m->m[0][1] = 0;
  m->m[0][2] = 0;
  m->m[1][0] = 0;
  m->m[1][1] = static_cast<s16>(c);
  m->m[1][2] = static_cast<s16>(s);
  m->m[2][0] = 0;
  m->m[2][1] = static_cast<s16>(-s);
  m->m[2][2] = static_cast<s16>(c);
}

// field 0x8006ed5c: rotation by 'angle' about the unit axis v (Rodrigues).
void rot_axis(const psyq::VECTOR& v, s32 angle, psyq::MATRIX* m) {
  const s32 c = rcos(angle), s = rsin(angle);
  // gte ldlvl + sqr12: IR = v (16-bit), MAC = IR * IR >> 12.
  ld_ir(v.vx, v.vy, v.vz);
  run(G::kSqr12);
  const s32 sx = mac(1), sy = mac(2), sz = mac(3);
  const s32 x = v.vx, y = v.vy, z = v.vz;
  const s32 omc = 0x1000 - c;
  m->m[0][0] = static_cast<s16>(sx + (mul(0x1000 - sx, c) >> 12));
  const s16 xy = static_cast<s16>(mul(mul(x, y) >> 12, omc) >> 12);
  const s16 zs = static_cast<s16>(mul(z, s) >> 12);
  m->m[1][0] = static_cast<s16>(xy - zs);
  m->m[0][1] = static_cast<s16>(xy + zs);
  const s16 xz = static_cast<s16>(mul(mul(x, z) >> 12, omc) >> 12);
  const s16 ys = static_cast<s16>(mul(y, s) >> 12);
  m->m[2][0] = static_cast<s16>(xz + ys);
  m->m[0][2] = static_cast<s16>(xz - ys);
  const s16 xs = static_cast<s16>(mul(x, s) >> 12);
  const s16 yz = static_cast<s16>(mul(mul(y, z) >> 12, omc) >> 12);
  m->m[2][1] = static_cast<s16>(yz - xs);
  m->m[1][2] = static_cast<s16>(yz + xs);
  m->m[1][1] = static_cast<s16>(sy + (mul(0x1000 - sy, c) >> 12));
  m->m[2][2] = static_cast<s16>(sz + (mul(0x1000 - sz, c) >> 12));
}

// field 0x8005fa54 (camera_init_light_matrices): light 1 points along +y, its colour is
// the terrain light (0x80075ac0..ac4) << 5.
void init_light_matrices() {
  WorldView& v = view();
  std::memset(&v.colour_b64, 0, sizeof(v.colour_b64));
  std::memset(&v.colour, 0, sizeof(v.colour));
  std::memset(&v.light_m, 0, sizeof(v.light_m));
  v.light_m.m[0][1] = 0x1000;
  v.colour.m[0][0] = static_cast<s16>(v.light[0] << 5);
  v.colour.m[1][0] = static_cast<s16>(v.light[1] << 5);
  v.colour.m[2][0] = static_cast<s16>(v.light[2] << 5);
}

// field 0x8005fb2c (camera_compute_view), look-at form (the world camera always is).
void compute_view() {
  WorldView& v = view();
  if (v.look_at != 0) {
    s32 d[3];
    for (int i = 0; i < 3; ++i) d[i] = (v.target[i] - v.eye[i]) >> 12;
    ld_ir(d[0], d[1], d[2]);
    run(G::kSqr0);
    const s32 sq[3] = {mac(1), mac(2), mac(3)};
    v.yaw2 = static_cast<s16>(world_ratan2(-d[0], d[2]));
    const s32 h = world_sqrt(sq[0] + sq[2]);
    v.pitch2 = static_cast<s16>(world_ratan2(d[1], h));
    v.pitch = v.pitch2;
    v.b38 = 0;
    v.yaw = v.yaw2;
    s32 len = world_sqrt(sq[0] + sq[1] + sq[2]);
    if (len == 0) len = 1;
    const s32 n[3] = {(d[0] << 12) / len, (d[1] << 12) / len, (d[2] << 12) / len};
    v.roll2 = v.roll;
    for (int i = 0; i < 3; ++i) v.offset[i] = static_cast<s16>(mul(v.roll, n[i]) >> 12);
  } else {
    // Angle form (not used on the world map): view from the negated target angles.
    psyq::SVECTOR r{static_cast<s16>(-v.target[0]), static_cast<s16>(-v.target[1]), static_cast<s16>(-v.target[2]), 0};
    psyq::MATRIX m{};
    field_rot_matrix(&r, &m);  // the field's own RotMatrix (0x8006e52c), as below
    psyq::SetRotMatrix(&m);
    ldv(0, 0x1000, 0, 0);
    run(G::kRtv0);
    const s32 f[3] = {mac(1), mac(2), mac(3)};
    ld_ir(f[0], f[1], f[2]);
    run(G::kSqr12);
    const s32 sq[3] = {mac(1), mac(2), mac(3)};
    v.yaw2 = static_cast<s16>(world_ratan2(-f[0], f[2]));
    v.pitch2 = static_cast<s16>(world_ratan2(f[1], world_sqrt(sq[0] + sq[2])));
    v.pitch = v.pitch2;
    v.yaw = v.yaw2;
    v.b38 = 0;
    v.roll2 = 0;
    const s32 o[3] = {mul(-v.target[0], f[0]) >> 12, mul(-v.target[1], f[1]) >> 12, mul(-v.target[2], f[2]) >> 12};
    for (int i = 0; i < 3; ++i) v.offset[i] = static_cast<s16>(o[i]);
    if (f[0] != 0) v.roll2 = static_cast<s16>(((o[0] << 16) >> 4) / f[0]);
    if (f[1] != 0) v.roll2 = static_cast<s16>(v.roll2 + ((o[1] << 16) >> 4) / f[1]);
    if (f[2] != 0) v.roll2 = static_cast<s16>(v.roll2 + ((o[2] << 16) >> 4) / f[2]);
  }
  psyq::SVECTOR r{v.pitch, v.yaw, v.b38, 0};
  field_rot_matrix(&r, &v.view);
  psyq::SetGeomOffset(v.geom_ofs_x + 0xa0, v.geom_ofs_y + 0x80);
}

// ---- WorldCam ----
std::unique_ptr<WorldCam> g_cam;

void cam_step_eye(WorldCam* c) {  // 0x8007e4e0
  WorldView& v = view();
  const s32 t = globals().frame_ticks;
  for (int i = 0; i < 3; ++i) v.eye_dest[i] += mul(mul(c->eye_step[i], t), c->speed) >> 8;
}
void cam_step_target(WorldCam* c) {  // 0x8007e598
  WorldView& v = view();
  const s32 t = globals().frame_ticks;
  for (int i = 0; i < 3; ++i) v.target_dest[i] += mul(mul(c->target_step[i], t), c->speed) >> 8;
}

// 0x8007e650: frames, speed, accel (< 0 brakes from the start), min, max.
void cam_start_ramp(WorldCam* c, s32 frames, s32 speed, s32 accel, s32 min_speed, s32 max_speed) {
  c->speed = speed;
  c->min_speed = min_speed != 0 ? min_speed : speed;
  c->braking = static_cast<u8>(static_cast<u32>(accel) >> 31);
  c->remaining = frames << 8;
  c->half = frames << 7;
  c->accel = accel;
  c->max_speed = max_speed;
  c->brake_mark = -1;
}

// 0x8007e694: one frame of the speed ramp; true when the move has run out (bit 0 snaps
// the eye, bit 1 the target onto the destination).
bool cam_ramp_step(WorldCam* c, u32 snap) {
  const s32 t = globals().frame_ticks;
  const s32 half = c->half;
  s32 rem = c->remaining - mul(t, c->speed);
  c->remaining = rem;
  if (c->braking == 0) {
    bool hold = c->max_speed <= c->speed && rem - (half - c->brake_mark) <= rem;
    if (!hold) {
      s32 a;
      bool apply = true;
      if (c->min_speed < 0) {
        if (half >= rem) apply = false;
        a = c->accel;
      } else {
        if (half < rem) {
          a = c->accel;
        } else {
          a = -c->accel;
        }
      }
      if (apply) c->speed += mul(a, t);
    }
  } else {
    c->speed += mul(c->accel, t);
  }
  if (c->max_speed <= c->speed) {
    c->speed = c->max_speed;
    if (c->brake_mark == -1) c->brake_mark = static_cast<s16>(c->remaining);
  }
  if (c->speed < c->min_speed) c->speed = c->min_speed;
  if (c->remaining >= 1) return false;
  WorldView& v = view();
  if (snap & 1)
    for (int i = 0; i < 3; ++i) v.eye_dest[i] = c->eye[i];
  if (snap & 2)
    for (int i = 0; i < 3; ++i) v.target_dest[i] = c->target[i];
  return true;
}

// 0x8007e824: the point at distance 'dist' from 'centre' in direction (yaw, pitch).
void cam_orbit_point(s32 out[4], const s32 centre[4], s32 yaw, s32 pitch, s32 dist) {
  const s32 cp = rcos(pitch), cy = rcos(yaw), sp = rsin(pitch), cp2 = rcos(pitch), sy = rsin(yaw);
  out[0] = mul(mul(cp, cy) >> 12, dist) + centre[0];
  out[1] = centre[1] - mul(sp, dist);
  out[2] = mul(mul(cp2, sy) >> 12, dist) + centre[2];
}

void cam_eye_step(WorldCam* c, s32 frames) {  // 0x8007e93c
  if (frames == 0) frames = 1;
  const WorldView& v = view();
  for (int i = 0; i < 3; ++i) c->eye_step[i] = (c->eye[i] - v.eye_dest[i]) / frames;
}
void cam_target_step(WorldCam* c, s32 frames) {  // 0x8007ea1c
  if (frames == 0) frames = 1;
  const WorldView& v = view();
  for (int i = 0; i < 3; ++i) c->target_step[i] = (c->target[i] - v.target_dest[i]) / frames;
}

// 0x8007e120: starts move 'mode' (priority from the overlay's byte table at 0x8008f42b).
int cam_set_mode(WorldCam* c, int mode, s32 arg) {
  if (mode == 0) return 0;
  const u8* prio_table = world_ovl_data(0x8008f42b + static_cast<u32>(mode));
  const u8 prio = prio_table != nullptr ? *prio_table : 0;
  if (c->mode != 0 && prio < c->mode_prio) return 0;
  if (arg != 0) c->mode_arg = arg;
  c->mode = static_cast<s16>(mode);
  c->frames_run = 0;
  c->mode_prio = prio;
  return 1;
}

// 0x8007e0b4.
int cam_request_mode(WorldCam* c, int mode, s32 arg) {
  if (mode == 0) return 0;
  if (c->mode != mode) return cam_set_mode(c, mode, arg);
  if (mode == 5 && c->mode_arg != 0 && arg != c->mode_arg) return cam_set_mode(c, 5, arg);
  return 5;
}

}  // namespace

WorldCam* world_cam() { return g_cam.get(); }

// 0x8007e000.
WorldCam* worldcam_construct(WorldCam* c) {
  WorldView& v = view();
  c->mode_prio = 0;
  v.look_at = 1;
  for (int i = 0; i < 3; ++i) c->eye_step[i] = c->target_step[i] = 0;
  c->eye_step[0] = c->eye_step[1] = c->eye_step[2] = 0;
  c->mode = 0;
  v.eye_dest[0] = 0;
  v.eye_dest[1] = static_cast<s32>(0xffbe6000u);
  v.eye_dest[2] = static_cast<s32>(0xfeda4000u);
  v.target_dest[0] = 0;
  v.target_dest[1] = static_cast<s32>(0xffc18000u);
  v.target_dest[2] = 0;
  v.zoom = 0x1000;
  c->zoom_target = 0x1000;
  c->zoom_speed = 0;
  return c;
}

void world_cam_create() {
  g_cam = std::make_unique<WorldCam>();
  worldcam_construct(g_cam.get());
}
void world_cam_destroy() { g_cam.reset(); }

// 0x8007e188 (vtable[1]): the eye / target the camera block uses this frame, the zoom ramp,
// and the running move.
void worldcam_update(WorldCam* c) {
  WorldView& v = view();
  for (int i = 0; i < 3; ++i) {
    v.eye[i] = v.eye_dest[i];
    v.target[i] = v.target_dest[i];
  }
  if (c->zoom_speed != 0) {
    bool done;
    const s32 t = globals().frame_ticks;
    if (v.zoom < c->zoom_target) {
      v.zoom += mul(c->zoom_speed, t);
      done = c->zoom_target < v.zoom;
    } else {
      v.zoom -= mul(c->zoom_speed, t);
      done = v.zoom < c->zoom_target;
    }
    if (done) {
      v.zoom = c->zoom_target;
      c->zoom_speed = 0;
    }
  }
  if (c->mode != 0) {
    // 0x8007e41c: step eye and target, stop when the ramp has run out.
    cam_step_eye(c);
    cam_step_target(c);
    if (cam_ramp_step(c, 0)) c->mode = 0;
    c->frames_run += globals().frame_ticks;
  }
}

// 0x8007e2cc: orbit 'centre' at (yaw, pitch, dist); frames 0 = jump there, else move
// there with the speed ramp (speed, accel, max 0x10000).
void worldcam_set_orbit(WorldCam* c, const s32 centre[4], s32 yaw, s32 pitch, s32 dist, s32 frames, s32 speed,
                        s32 accel) {
  WorldView& v = view();
  v.look_at = 1;
  s32 eye[4] = {};
  cam_orbit_point(eye, centre, yaw, pitch, dist);
  for (int i = 0; i < 4; ++i) {
    c->eye[i] = eye[i];
    c->target[i] = centre[i];
  }
  if (frames == 0) {
    for (int i = 0; i < 3; ++i) {
      v.eye_dest[i] = c->eye[i];
      v.target_dest[i] = c->target[i];
    }
    c->mode = 0;
  } else {
    cam_request_mode(c, 10, 0);
    c->field_0e = 0;
    cam_start_ramp(c, frames, speed, accel, 0, 0x10000);
    cam_eye_step(c, frames);
    cam_target_step(c, frames);
  }
}

// 0x8007eafc: the world wrapped (the leader crossed the torus seam): shift every camera
// point by (dx, dz).
void worldcam_shift(WorldCam* c, s32 dx, s32 dz) {
  WorldView& v = view();
  v.eye_dest[0] += dx;
  v.eye_dest[2] += dz;
  v.target_dest[0] += dx;
  v.target_dest[2] += dz;
  v.eye[0] += dx;
  v.eye[2] += dz;
  v.target[0] += dx;
  v.target[2] += dz;
  c->eye[0] += dx;
  c->eye[2] += dz;
  c->target[0] += dx;
  c->target[2] += dz;
}

int worldcam_request(int mode) { return g_cam ? cam_request_mode(g_cam.get(), mode, 0) : 0; }
int worldcam_set_mode(int mode) { return g_cam ? cam_set_mode(g_cam.get(), mode, 0) : 0; }

s32 world_camera_yaw() {  // 0x8007ebec
  const WorldView& v = view();
  return world_ratan2((v.target[2] - v.eye[2]) >> 12, (v.target[0] - v.eye[0]) >> 12);
}

// camera_reset (0x8005f8d8) on the world map.
void world_camera_reset() {
  WorldView& v = view();
  const s32 h = mul(map_data().projection_h, v.zoom) >> 12;
  psyq::SetGeomScreen(h);
  psyq::SetGeomOffset(0xa0, 0x70);
  psyq::SetFarColor(v.far_colour[0], v.far_colour[1], v.far_colour[2]);
  psyq::SetFogNearFar(0xe00, 50000, h);
  v.look_at = 0;
  for (int i = 0; i < 3; ++i) v.eye[i] = v.target[i] = 0;
  v.zoom = 0xccc;
  for (s16& t : g_fade_targets.bg_tint) t = 0x80;
  for (u8& a : v.ambient) a = 0x1e;
  v.roll = 0;
  camera_reset();  // the field's own copy of the block
}

// camera_update_matrices (0x800603a4): view, light matrices and the rotated offset.
void world_camera_update_matrices() {
  WorldView& v = view();
  compute_view();
  init_light_matrices();
  psyq::SVECTOR r{v.offset[0], v.offset[1], v.offset[2], 0};
  psyq::MATRIX m{};
  field_rot_matrix(&r, &m);
  psyq::PushMatrix();
  v.rot_b44 = m;
  rot_columns(v.view, &v.rot_b44);
  psyq::PopMatrix();
}

// 0x8007ec38: matrices for an object (a terrain block, an actor) at 'pos' (20.12, block
// relative): GTE RT / TR = view * Raxis(angles[2]) * Rx(angles[0]) + view * (pos - eye);
// world = Ry(yaw) + (pos - eye) with absolute y (the bend pass), proj = Rx(pitch) Rz(roll)
// + (0, -eye.y, 0) (the projection pass); H from the zoom; fog; the curvature offset.
void world_set_object_matrix(const s32 pos[3], const s32 angles[3], const s32 scale[3]) {
  WorldView& v = view();
  const psyq::VECTOR axis{0, -rsin(angles[0]), -rcos(angles[0]), 0};
  psyq::MATRIX mx{}, ma{}, t{};
  rot_x(angles[0], &mx);
  rot_axis(axis, angles[2], &ma);
  t = ma;
  rot_columns(v.view, &t);   // t = view * ma
  {
    psyq::MATRIX left = t;
    t = mx;
    rot_columns(left, &t);   // t = (view * ma) * mx
  }
  psyq::SVECTOR rel{static_cast<s16>((pos[0] - v.eye[0]) >> 12), static_cast<s16>((pos[1] - v.eye[1]) >> 12),
                    static_cast<s16>((pos[2] - v.eye[2]) >> 12), 0};
  psyq::SetRotMatrix(&v.view);
  ldv(0, rel.vx, rel.vy, rel.vz);
  run(G::kRtv0);
  psyq::VECTOR tr{mac(1), mac(2), mac(3), 0};
  psyq::TransMatrix(&t, &tr);
  const psyq::VECTOR sc{scale[0], scale[1], scale[2], 0};
  psyq::ScaleMatrix(&t, &sc);

  psyq::SVECTOR ry{0, v.yaw2, 0, 0};
  field_rot_matrix(&ry, &v.world);  // overlay 0x8007ef78 / 0x8007efa8 call the field's 0x8006e52c
  psyq::SVECTOR rp{v.pitch2, 0, v.roll, 0};
  field_rot_matrix(&rp, &v.proj);
  psyq::SVECTOR p2{static_cast<s16>((pos[0] - v.eye[0]) >> 12), static_cast<s16>(pos[1] >> 12),
                   static_cast<s16>((pos[2] - v.eye[2]) >> 12), 0};
  psyq::VECTOR out{};
  psyq::ApplyMatrix(&v.world, &p2, &out);
  psyq::TransMatrix(&v.world, &out);
  psyq::ScaleMatrix(&v.world, &sc);
  psyq::SVECTOR p3{0, static_cast<s16>(-v.eye[1] >> 12), 0, 0};
  psyq::ApplyMatrix(&v.proj, &p3, &out);
  psyq::TransMatrix(&v.proj, &out);
  psyq::ScaleMatrix(&v.proj, &sc);
  const s32 h = mul(map_data().projection_h, v.zoom) >> 12;
  psyq::SetGeomScreen(h);
  psyq::SetFogNearFar(0xe00, 50000, h);
  psyq::SetRotMatrix(&t);
  psyq::SetTransMatrix(&t);
  WorldGlobals& w = world();
  const s32 c1 = rcos((w.cam_pitch_deg << 12) / 360);
  const s32 c2 = rcos(w.pitch_copy >> 12);
  w.curve_offset = (mul(c2, 0xaf0) - mul(c1, 0xaf0)) >> 12;
}

namespace {

// 0x8008c644: the camera when nobody walks (or the leader stands): L1 / R1 turn it.
void camera_manual(WorldBg* bg) {
  FieldGlobals& g = globals();
  const bool vehicle = (g.script_flags[1] >> 4 & 1) != 0;
  const s32 max_speed = vehicle ? 0x14000 : 0x19000;
  const s32 accel = vehicle ? 0x1000 : 0x2000;
  const s32 brake = vehicle ? 0x380 : 0x600;
  const s32 t = g.frame_ticks;
  const u16 held = g.held[0];
  if ((held & 0xc) != 0xc && static_cast<s16>(read_u16(g.field_flags + 0x10)) != -1) {
    if (held & 4) bg->turn += mul(accel, t);
    if (held & 8) bg->turn -= mul(accel, t);
  }
  const bool neg = bg->turn_speed < 0;
  if (neg) {
    bg->turn = -bg->turn;
    bg->turn_speed = -bg->turn_speed;
  }
  if (bg->turn == 0) {
    if (bg->turn_speed != 0) {
      bg->turn_speed -= bg->turn_brake;
      if (bg->turn_speed < 0) bg->turn_speed = 0;
    }
  } else {
    s32 sign = bg->turn_speed == 0 ? 0 : (bg->turn_speed > 0 ? 1 : -1);
    if (bg->turn != 0) sign = bg->turn < 1 ? ~sign : sign ^ 1;
    if (sign != 0) bg->turn_speed -= brake;
    if (bg->turn_speed < 0) bg->turn_speed = 0;
    bg->turn_speed += bg->turn;
  }
  if (max_speed < bg->turn_speed) bg->turn_speed = max_speed;
  if (neg) bg->turn_speed = -bg->turn_speed;
  WorldGlobals& w = world();
  w.pitch_copy = bg->pitch;
  bg->yaw += bg->turn_speed;
  if (WorldCam* c = world_cam()) {
    worldcam_set_orbit(c, bg->focus, bg->yaw >> 12, bg->pitch >> 12, 0xaf0, 0, 0x80, 5);
    worldcam_update(c);
  }
  world_camera_update_matrices();
}

}  // namespace

void world_camera_manual(WorldBg* bg) { camera_manual(bg); }

// 0x8008bed8: follows 'a': raises the pitch over high ground ahead (camera floor probe
// along three lines), recentres when the leader changes block, then turns by 'turn'.
void world_camera_follow(WorldBg* bg, Actor* a, s32 turn, s32 turn_brake, int scripted) {
  WorldView& v = view();
  WorldGlobals& w = world();
  const s32 yaw = world_ratan2((v.target[2] - v.eye[2]) >> 12, (v.target[0] - v.eye[0]) >> 12);
  const s32 base_pitch = (w.cam_pitch_deg * 0x1000) / 360;
  const s32 reach = mul(rcos(base_pitch), 0xaf0) >> 12;
  const s32 c0 = rcos(yaw + 0x800), s0 = rsin(yaw + 0x800);
  const s32 cx = (bg->centre_x / 0x3000) * 0x3000000 + v.target[0];
  const s32 cz = (bg->centre_z / 0x3000) * 0x3000000 + v.target[2];
  const s32 cl = rcos(yaw + 0x400), sl = rsin(yaw + 0x400);
  const s32 cr = rcos(yaw + 0xc00), sr = rsin(yaw + 0xc00);
  s32 dist16 = reach << 4;
  const s32 eye_y = v.target[1] >> 12;
  const s32 sx = mul(c0, reach) / 10, sz = mul(s0, reach) / 10;
  s32 offz = sz * 0x10;
  s32 offx = sx * 0x10;
  s32 lz = offz + cz + sl * 200;
  s32 lx = offx + cx + cl * 200;
  s32 mz = offz + cz;
  s32 mx = offx + cx;
  s32 pitch = base_pitch;
  for (int i = 0x10; i > 0; --i) {
    s32 floor = world_terrain_camera_floor(bg, mx, mz);
    s32 f2 = world_terrain_camera_floor(bg, lx, lz);
    if (f2 < floor) floor = f2;
    f2 = world_terrain_camera_floor(bg, cx + cr * 200 + offx, cz + sr * 200 + offz);
    if (f2 < floor) floor = f2;
    const s32 p = world_ratan2(eye_y - floor, dist16 / 10);
    if (pitch < p) pitch = p;
    dist16 -= reach;
    mz -= sz;
    lz -= sz;
    offz -= sz;
    mx -= sx;
    lx -= sx;
    offx -= sx;
  }
  if (pitch != base_pitch) pitch += 0x22;
  if (pitch > 0x2aa) pitch = 0x2aa;
  s32 diff = pitch * 0x1000 - bg->pitch;
  if (bg->pitch == pitch * 0x1000) {
    bg->pitch_speed = 0;
  } else {
    const bool down = diff < 1;
    if (down) {
      diff = -diff;
      bg->pitch_speed = -bg->pitch_speed;
    }
    const s32 acc = globals().frame_ticks * 0x800;
    bg->pitch_speed += acc;
    bg->pitch_step = acc;
    if (bg->pitch_speed > 0x8000) bg->pitch_speed = 0x8000;
    if (diff < bg->pitch_speed) bg->pitch_speed = diff;
    if (down) bg->pitch_speed = -bg->pitch_speed;
  }
  bg->pitch += bg->pitch_speed;
  bg->focus[0] = a->x % 0x3000000;
  bg->focus[1] = a->y;
  bg->focus[2] = a->z % 0x3000000;
  if (bg->centre_x != -1) {
    s32 dx = bg->centre_x / 0x3000 - a->x / 0x3000000;
    s32 dz = bg->centre_z / 0x3000 - a->z / 0x3000000;
    if (dx == 8) {
      dx = -1;
    } else if (dx == -8) {
      dx = 1;
    }
    if (dz == 6) {
      dz = -1;
    } else if (dz == -6) {
      dz = 1;
    }
    if ((dx != 0 || dz != 0) && world_cam() != nullptr) worldcam_shift(world_cam(), dx * 0x3000000, dz * 0x3000000);
  }
  bg->centre_x = a->x >> 12;
  bg->centre_z = a->z >> 12;
  if (scripted == 0) {
    bg->turn = mul(turn, globals().frame_ticks);
    bg->turn_brake = mul(turn_brake + 0x600, globals().frame_ticks);
    camera_manual(bg);
  } else {
    w.pitch_copy = bg->pitch;
    bg->yaw += mul(turn, globals().frame_ticks);
    if (WorldCam* c = world_cam()) {
      worldcam_set_orbit(c, bg->focus, bg->yaw >> 12, bg->pitch >> 12, 0xaf0, w.cam_frames, 0x80, 5);
      worldcam_update(c);
    }
    world_camera_update_matrices();
  }
}

}  // namespace ss::game::field
