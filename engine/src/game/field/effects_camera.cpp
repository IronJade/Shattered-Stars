// The camera block's matrix code (field 0x8005fa54-0x800617d8 and the field program's
// own matrix helpers 0x8006e52c / 0x8006ecf0 / 0x8006ed5c): the view rotation built from
// the eye and target (camera_compute_view), the copy the 3D effects draw with
// (0x80075b44), the light / colour matrices, and the per-object matrix the effects'
// rings and spinners use (0x8005ff84). On field maps actor_sys_setup_view runs
// camera_reset + camera_update_matrices after each map load; the world map has its own
// versions (worldmap_camera.cpp).
#include <cstring>

#include "game/common/math.h"
#include "game/field/camera.h"
#include "game/field/effects_internal.h"
#include "game/field/fade.h"
#include "game/field/field.h"
#include "game/field/map.h"
#include "game/field/script_vm.h"

namespace ss::game::field {
namespace {

using fx::G;

s32 mul(s32 a, s32 b) { return static_cast<s32>(static_cast<u32>(a) * static_cast<u32>(b)); }
s16 cos16(s32 a) { return static_cast<s16>(cos4096(static_cast<u32>(a))); }
s16 sin16(s32 a) { return static_cast<s16>(sin4096(static_cast<u32>(a))); }

// m[:, j] = RT * src[:, j] for each column (gte ldclmv / rtir / stclmv): IR saturates.
void rt_columns(const psyq::MATRIX& src, psyq::MATRIX* dst) {
  psyq::MATRIX out = *dst;
  for (int j = 0; j < 3; ++j) {
    fx::ld_ir(src.m[0][j], src.m[1][j], src.m[2][j]);
    fx::run(G::kRtir);
    out.m[0][j] = fx::ir(1);
    out.m[1][j] = fx::ir(2);
    out.m[2][j] = fx::ir(3);
  }
  *dst = out;
}

// gte ldlvl + sqr0 (sf 0) / sqr12 (sf 1): each component truncated to 16 bits.
void square(const s32 v[3], s32 out[3], bool shift12) {
  fx::ld_ir(v[0], v[1], v[2]);
  fx::run(shift12 ? G::kSqr12 : G::kSqr0);
  for (int i = 0; i < 3; ++i) out[i] = fx::mac(i + 1);
}

}  // namespace

void field_rot_matrix(const psyq::SVECTOR* r, psyq::MATRIX* m) {
  const s32 cx = cos16(r->vx), sx = sin16(r->vx);
  const s32 cy = cos16(r->vy), sy = sin16(r->vy);
  const s32 cz = cos16(r->vz), sz = sin16(r->vz);
  const s32 nsy = static_cast<s16>(-sy);
  const s32 a = mul(cz, nsy) >> 12;
  const s32 b = mul(sz, nsy) >> 12;
  m->m[0][2] = static_cast<s16>(sy);
  m->m[0][0] = static_cast<s16>(mul(cy, cz) >> 12);
  m->m[0][1] = static_cast<s16>(static_cast<s32>(0u - static_cast<u32>(mul(sz, cy))) >> 12);
  m->m[1][2] = static_cast<s16>(static_cast<s32>(0u - static_cast<u32>(mul(cy, sx))) >> 12);
  m->m[2][2] = static_cast<s16>(mul(cy, cx) >> 12);
  m->m[1][0] = static_cast<s16>((mul(sz, cx) >> 12) - (mul(a, sx) >> 12));
  m->m[2][0] = static_cast<s16>((mul(sz, sx) >> 12) + (mul(a, cx) >> 12));
  m->m[1][1] = static_cast<s16>((mul(cz, cx) >> 12) + (mul(b, sx) >> 12));
  // libgte's RotMatrix subtracts here; the field program's C copy adds.
  m->m[2][1] = static_cast<s16>((mul(cz, sx) >> 12) + (mul(b, cx) >> 12));
}

void rot_matrix_y(s32 angle, psyq::MATRIX* m) {
  const s16 c = cos16(angle), s = sin16(angle);
  m->m[0][0] = c;
  m->m[0][1] = 0;
  m->m[0][2] = s;
  m->m[1][0] = 0;
  m->m[1][1] = 0x1000;
  m->m[1][2] = 0;
  m->m[2][0] = static_cast<s16>(-s);
  m->m[2][1] = 0;
  m->m[2][2] = c;
}

void rot_matrix_axis(const s32 axis[3], s32 angle, psyq::MATRIX* m) {
  const s32 c = cos16(angle), s = sin16(angle);
  s32 sq[3];
  square(axis, sq, true);
  const s32 x = axis[0], y = axis[1], z = axis[2];
  const s32 omc = 0x1000 - c;
  m->m[0][0] = static_cast<s16>(sq[0] + (mul(0x1000 - sq[0], c) >> 12));
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
  m->m[1][1] = static_cast<s16>(sq[1] + (mul(0x1000 - sq[1], c) >> 12));
  m->m[2][2] = static_cast<s16>(sq[2] + (mul(0x1000 - sq[2], c) >> 12));
}

void camera_init_light_matrices(const s32 angles[3]) {
  CameraBlock& cam = camera_block();
  cam.fx_colour = psyq::MATRIX{};
  cam.colour = psyq::MATRIX{};
  cam.light = psyq::MATRIX{};
  cam.light.m[0][1] = 0x1000;
  cam.colour.m[0][0] = static_cast<s16>(cam.light_rgb[0] << 5);
  cam.colour.m[2][0] = static_cast<s16>(cam.light_rgb[2] << 5);
  cam.colour.m[1][0] = static_cast<s16>(cam.light_rgb[1] << 5);
  // The original also builds RotMatrix(angles) into a local it never uses.
  (void)angles;
}

s32 camera_compute_view() {
  CameraBlock& cam = camera_block();
  if (cam.look_at == 0) {
    // Angle form: the view direction of the negated target angles.
    const psyq::SVECTOR r{static_cast<s16>(-cam.target[0]), static_cast<s16>(-cam.target[1]),
                          static_cast<s16>(-cam.target[2]), 0};
    psyq::MATRIX m{};
    field_rot_matrix(&r, &m);
    psyq::SetRotMatrix(&m);
    fx::ldv(0, 0x1000, 0, 0);
    fx::run(G::kRtv0);
    const s32 f[3] = {fx::mac(1), fx::mac(2), fx::mac(3)};
    s32 sq[3];
    square(f, sq, true);
    cam.yaw2 = static_cast<s16>(ratan2(-f[0], f[2]));
    cam.angles[0] = static_cast<s16>(ratan2(f[1], SquareRoot0(sq[0] + sq[2])));
    cam.angles[2] = 0;
    cam.distance2 = 0;
    const s32 o[3] = {mul(-cam.target[0], f[0]) >> 12, mul(-cam.target[1], f[1]) >> 12,
                      mul(-cam.target[2], f[2]) >> 12};
    for (int i = 0; i < 3; ++i) cam.offset[i] = static_cast<s16>(o[i]);
    cam.angles[1] = cam.yaw2;
    for (int i = 0; i < 3; ++i)
      if (f[i] != 0)
        cam.distance2 = static_cast<s16>(cam.distance2 + (static_cast<s32>(static_cast<u32>(o[i]) << 16) >> 4) / f[i]);
    cam.pitch2 = cam.angles[0];
  } else {
    s32 d[3];
    for (int i = 0; i < 3; ++i) d[i] = (cam.target[i] - cam.eye[i]) >> 12;
    s32 sq[3];
    square(d, sq, false);
    cam.yaw2 = static_cast<s16>(ratan2(-d[0], d[2]));
    cam.angles[0] = static_cast<s16>(ratan2(d[1], SquareRoot0(sq[0] + sq[2])));
    cam.angles[2] = 0;
    cam.angles[1] = cam.yaw2;
    cam.pitch2 = cam.angles[0];
    s32 len = SquareRoot0(sq[0] + sq[1] + sq[2]);
    if (len == 0) len = 1;
    cam.distance2 = cam.distance;
    for (int i = 0; i < 3; ++i) cam.offset[i] = static_cast<s16>(mul(cam.distance, static_cast<s32>(static_cast<u32>(d[i]) << 12) / len) >> 12);
  }
  const psyq::SVECTOR r{cam.angles[0], cam.angles[1], cam.angles[2], 0};
  field_rot_matrix(&r, &cam.view);
  // Field maps turn it half round about z (RotMatrixZ 0x800); the world map moves the
  // GTE offset instead (world_camera_update_matrices).
  psyq::RotMatrixZ(0x800, &cam.view);
  return 0;
}

void camera_update_matrices() {
  CameraBlock& cam = camera_block();
  camera_compute_view();
  camera_init_light_matrices(cam.light_angles);
  const psyq::SVECTOR r{cam.offset[0], cam.offset[1], cam.offset[2], 0};
  psyq::MATRIX m{};
  field_rot_matrix(&r, &m);
  psyq::PushMatrix();
  psyq::SetRotMatrix(&cam.view);
  rt_columns(m, &cam.rot);
  psyq::PopMatrix();
}

void camera_set_object_matrix(const s32 pos[3], s32 rot[3], const s32 scale[3]) {
  const CameraBlock& cam = camera_block();
  s32 axis[3] = {0, 0, 0};
  if (rot[0] != 0) {
    rot[2] = rot[0];
    axis[0] = -sin4096(static_cast<u32>(rot[1]));
    axis[2] = -cos4096(static_cast<u32>(rot[1]));
  } else {
    axis[0] = cos4096(static_cast<u32>(rot[1]));
    axis[2] = -sin4096(static_cast<u32>(rot[1]));
  }
  psyq::MATRIX ry{}, ra{};
  rot_matrix_y(rot[1], &ry);
  rot_matrix_axis(axis, rot[2], &ra);
  psyq::MATRIX m{};
  psyq::SetRotMatrix(&cam.view);
  rt_columns(ra, &m);  // view * Raxis
  psyq::SetRotMatrix(&m);
  rt_columns(ry, &m);  // * Ry
  const s16 rel[3] = {static_cast<s16>((pos[0] - cam.eye[0]) >> 12), static_cast<s16>((pos[1] - cam.eye[1]) >> 12),
                      static_cast<s16>((pos[2] - cam.eye[2]) >> 12)};
  psyq::SetRotMatrix(&cam.view);
  fx::ldv(0, rel[0], rel[1], rel[2]);
  fx::run(G::kRtv0);
  const psyq::VECTOR t{fx::mac(1), fx::mac(2), fx::mac(3), 0};
  psyq::TransMatrix(&m, &t);
  const psyq::VECTOR s{scale[0], scale[1], scale[2], 0};
  psyq::ScaleMatrix(&m, &s);
  const u8* flags = globals().field_flags;
  const std::vector<u8>& scene = map_data().scene;
  s32 h;
  if (flags != nullptr && flags[0x68] != 0) {
    h = mul(map_data().projection_h, cam.zoom) >> 12;
  } else {
    u16 sh = 0;
    if (scene.size() >= 0x6C) std::memcpy(&sh, scene.data() + 0x6A, 2);
    h = mul(sh, cam.zoom) >> 12;
  }
  fx::gte().write_ctrl(G::H, static_cast<u32>(h));
  psyq::SetRotMatrix(&m);
  psyq::SetTransMatrix(&m);
}

void gte_set_back_colour() {
  const CameraBlock& cam = camera_block();
  s32 c[3];
  for (int i = 0; i < 3; ++i) {
    c[i] = static_cast<s32>(static_cast<u32>(cam.ambient[i]) * static_cast<u32>(static_cast<s32>(g_fade_targets.bg_tint[i]))) >> 7;
    if (c[i] > 0xFF) c[i] = 0xFF;
  }
  psyq::SetBackColor(c[0], c[1], c[2]);
}

}  // namespace ss::game::field
