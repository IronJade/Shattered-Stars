#include "game/battle/render.h"

#include <cstring>

#include "game/battle/battle.h"
#include "psx/gte.h"

namespace ss::game::battle {
namespace {

ViewState g_view;

psx::Gte& gte() { return psyq::gte(); }

// gte_ldclmv / gte_rtir / gte_stclmv: one column of 'in' through RT, into 'out'.
void rt_column(const psyq::MATRIX& in, psyq::MATRIX* out, int col) {
  psx::Gte& g = gte();
  g.write_data(psx::Gte::IR1, static_cast<u32>(static_cast<s32>(in.m[0][col])));
  g.write_data(psx::Gte::IR2, static_cast<u32>(static_cast<s32>(in.m[1][col])));
  g.write_data(psx::Gte::IR3, static_cast<u32>(static_cast<s32>(in.m[2][col])));
  g.execute(psx::Gte::kRtir);
  out->m[0][col] = g.ir(1);
  out->m[1][col] = g.ir(2);
  out->m[2][col] = g.ir(3);
}

}  // namespace

ViewState& view() { return g_view; }

s32 bsin(u32 angle) {
  angle &= 0xFFF;
  if (angle < 0x401) return battle_u16(0x80070078 + angle * 2);
  if (angle < 0x800) return battle_u16(0x80070078 + (0x800 - angle) * 2);
  if (angle < 0xC01) return -static_cast<s32>(battle_u16(0x80070078 + (angle - 0x800) * 2));
  return -static_cast<s32>(battle_u16(0x80070078 + (0x1000 - angle) * 2));
}

s32 bcos(u32 angle) { return bsin(angle + 0x400); }

void rot_matrix(const s16 angles[3], psyq::MATRIX* m) {
  const s32 cx = static_cast<s16>(bcos(static_cast<u32>(angles[0])));
  const s32 sx = static_cast<s16>(bsin(static_cast<u32>(angles[0])));
  const s32 cy = static_cast<s16>(bcos(static_cast<u32>(angles[1])));
  const s32 sy = static_cast<s16>(bsin(static_cast<u32>(angles[1])));
  const s32 cz = static_cast<s16>(bcos(static_cast<u32>(angles[2])));
  const s32 sz = static_cast<s16>(bsin(static_cast<u32>(angles[2])));
  const s32 a = cz * -sy >> 12;
  const s32 b = sz * -sy >> 12;
  m->m[0][2] = static_cast<s16>(sy);
  m->m[0][0] = static_cast<s16>(cy * cz >> 12);
  m->m[0][1] = static_cast<s16>(-(sz * cy) >> 12);
  m->m[1][2] = static_cast<s16>(-(cy * sx) >> 12);
  m->m[2][2] = static_cast<s16>(cy * cx >> 12);
  m->m[1][0] = static_cast<s16>(static_cast<s16>(sz * cx >> 12) - static_cast<s16>(a * sx >> 12));
  m->m[2][0] = static_cast<s16>(static_cast<s16>(sz * sx >> 12) + static_cast<s16>(a * cx >> 12));
  m->m[1][1] = static_cast<s16>(static_cast<s16>(cz * cx >> 12) + static_cast<s16>(b * sx >> 12));
  m->m[2][1] = static_cast<s16>(static_cast<s16>(cz * sx >> 12) + static_cast<s16>(b * cx >> 12));
}

void rot_matrix_y(s32 angle, psyq::MATRIX* m) {
  const s16 c = static_cast<s16>(bcos(static_cast<u32>(angle)));
  const s16 s = static_cast<s16>(bsin(static_cast<u32>(angle)));
  m->m[2][0] = static_cast<s16>(-s);
  m->m[0][0] = c;
  m->m[1][0] = 0;
  m->m[0][1] = 0;
  m->m[1][1] = 0x1000;
  m->m[2][1] = 0;
  m->m[0][2] = s;
  m->m[1][2] = 0;
  m->m[2][2] = c;
}

void rot_matrix_axis(const s32 axis[3], s32 angle, psyq::MATRIX* m) {
  const s32 c = static_cast<s16>(bcos(static_cast<u32>(angle)));
  const s32 s = static_cast<s16>(bsin(static_cast<u32>(angle)));
  // gte_ldlvl / gte_sqr12 / gte_stlvnl: the squares (>> 12) of the axis.
  psx::Gte& g = gte();
  g.write_data(psx::Gte::IR1, static_cast<u32>(axis[0]));
  g.write_data(psx::Gte::IR2, static_cast<u32>(axis[1]));
  g.write_data(psx::Gte::IR3, static_cast<u32>(axis[2]));
  g.execute(psx::Gte::kSqr12);
  const s32 sq[3] = {g.mac(1), g.mac(2), g.mac(3)};
  const s32 x = axis[0], y = axis[1], z = axis[2];
  const s32 t = 0x1000 - c;
  m->m[0][0] = static_cast<s16>(static_cast<s16>(sq[0]) + static_cast<s16>((0x1000 - sq[0]) * c >> 12));
  m->m[1][0] = static_cast<s16>(static_cast<s16>((x * y >> 12) * t >> 12) - static_cast<s16>(z * s >> 12));
  m->m[2][0] = static_cast<s16>(static_cast<s16>((x * z >> 12) * t >> 12) + static_cast<s16>(y * s >> 12));
  m->m[0][1] = static_cast<s16>(static_cast<s16>((x * y >> 12) * t >> 12) + static_cast<s16>(z * s >> 12));
  m->m[1][1] = static_cast<s16>(static_cast<s16>(sq[1]) + static_cast<s16>((0x1000 - sq[1]) * c >> 12));
  m->m[2][1] = static_cast<s16>(static_cast<s16>((y * z >> 12) * t >> 12) - static_cast<s16>(x * s >> 12));
  m->m[0][2] = static_cast<s16>(static_cast<s16>((x * z >> 12) * t >> 12) - static_cast<s16>(y * s >> 12));
  m->m[1][2] = static_cast<s16>(static_cast<s16>((y * z >> 12) * t >> 12) + static_cast<s16>(x * s >> 12));
  m->m[2][2] = static_cast<s16>(static_cast<s16>(sq[2]) + static_cast<s16>((0x1000 - sq[2]) * c >> 12));
}

s32 square_root0(s32 a) {
  // LZCS / LZCR: leading zeros (or ones for a negative a).
  u32 v = static_cast<u32>(a);
  if (a < 0) v = ~v;
  u32 lz = 0;
  while (lz < 32 && (v & 0x80000000u) == 0) {
    v <<= 1;
    ++lz;
  }
  if (lz == 32) return 0;
  const u32 even = lz & ~1u;
  const u32 shift = (31 - even) >> 1;
  const s32 index = static_cast<s32>(even) - 24 >= 0 ? static_cast<s32>(static_cast<u32>(a) << (even - 24))
                                                      : a >> (24 - even);
  const s32 root = battle_s16(0x80070C30 + static_cast<u32>((index - 0x40) * 2));
  return static_cast<s32>((static_cast<u32>(root) << shift) >> 12);
}

// 0x8003c838 colour_lerp: per channel from + (to - from) * t >> 12 (t 0..0x1000).
u32 colour_lerp(u32 from, u32 to, s32 t) {
  const u32 r0 = (from & 0xFF) * 0x10, g0 = from >> 4 & 0xFF0, b0 = from >> 0xC & 0xFF0;
  const u32 r1 = (to & 0xFF) * 0x10, g1 = to >> 4 & 0xFF0, b1 = to >> 0xC & 0xFF0;
  const u32 r = static_cast<u32>(static_cast<s32>(r0) + (static_cast<s32>((r1 - r0) * static_cast<u32>(t)) >> 12)) >> 4;
  const u32 g = (g0 + (static_cast<s32>((g1 - g0) * static_cast<u32>(t)) >> 12)) * 0x10 & 0xFF00;
  const u32 b = (b0 + (static_cast<s32>((b1 - b0) * static_cast<u32>(t)) >> 12)) * 0x1000 & 0xFF0000;
  return r | g | b;
}

s32 ratan2(s32 y, s32 x) {
  const bool neg_x = x < 0;
  if (neg_x) x = -x;
  const bool neg_y = y < 0;
  if (neg_y) y = -y;
  if (x == 0 && y == 0) return 0;
  s32 v;
  if (y < x) {
    const s32 idx = (y & 0x7FE00000) != 0 ? y / (x >> 10) : (y << 10) / x;
    v = battle_s16(0x80070DB0 + static_cast<u32>(idx * 2));
  } else {
    const s32 idx = (x & 0x7FE00000) != 0 ? x / (y >> 10) : (x << 10) / y;
    v = 0x400 - battle_s16(0x80070DB0 + static_cast<u32>(idx * 2));
  }
  if (neg_x) v = 0x800 - v;
  if (neg_y) v = -v;
  return v;
}

void set_fog_near(s32 a, s32 h) {
  psx::Gte& g = gte();
  g.write_ctrl(psx::Gte::DQA, static_cast<u32>((a * -0x140) / h));
  g.write_ctrl(psx::Gte::DQB, 0x1400000);
}

void gte_setup() {
  psx::Gte& g = gte();
  g.write_ctrl(psx::Gte::ZSF3, 0x155);
  g.write_ctrl(psx::Gte::ZSF4, 0x100);
  g.write_ctrl(psx::Gte::H, 1000);
  g.write_ctrl(psx::Gte::DQA, static_cast<u32>(-4194));
  g.write_ctrl(psx::Gte::DQB, 0x1400000);
  g.write_ctrl(psx::Gte::OFX, 0);
  g.write_ctrl(psx::Gte::OFY, 0);
}

void camera_reset() {
  ViewState& v = g_view;
  psx::Gte& g = gte();
  g.write_ctrl(psx::Gte::H, 0x200);
  g.write_ctrl(psx::Gte::OFX, 0xA0u << 16);
  g.write_ctrl(psx::Gte::OFY, 0x78u << 16);
  g.write_ctrl(psx::Gte::RFC, static_cast<u32>(v.fog_rgb[0]) << 4);
  g.write_ctrl(psx::Gte::GFC, static_cast<u32>(v.fog_rgb[1]) << 4);
  g.write_ctrl(psx::Gte::BFC, static_cast<u32>(v.fog_rgb[2]) << 4);
  set_fog_near(v.fog_near, 0x200);
  v.zoom = 0x1000;
  v.view_mode = 0;
  for (int i = 0; i < 3; ++i) {
    v.view_eye[i] = 0;
    v.view_target[i] = 0;
    v.stage_colour[i] = 0x80;
    v.stage_colour_target[i] = 0x80;
  }
  v.roll_input = 0;
}

void camera_compute_view() {
  ViewState& v = g_view;
  psx::Gte& g = gte();
  if (v.view_mode == 0) {
    // Angle mode: 0x8006f014 holds the camera angles.
    const s16 angles[3] = {static_cast<s16>(-v.view_target[0]), static_cast<s16>(-v.view_target[1]),
                           static_cast<s16>(-v.view_target[2])};
    psyq::MATRIX m{};
    rot_matrix(angles, &m);
    psyq::SetRotMatrix(&m);
    g.write_data(psx::Gte::VXY0, 0x1000);
    g.write_data(psx::Gte::VZ0, 0);
    g.execute(psx::Gte::kRtv0);
    const s32 d[3] = {g.mac(1), g.mac(2), g.mac(3)};
    g.write_data(psx::Gte::IR1, static_cast<u32>(d[0]));
    g.write_data(psx::Gte::IR2, static_cast<u32>(d[1]));
    g.write_data(psx::Gte::IR3, static_cast<u32>(d[2]));
    g.execute(psx::Gte::kSqr12);
    const s32 sq[3] = {g.mac(1), g.mac(2), g.mac(3)};
    v.yaw = static_cast<s16>(ratan2(-d[0], d[2]));
    v.pitch = static_cast<s16>(ratan2(d[1], square_root0(sq[0] + sq[2])));
    v.view_angles[0] = v.pitch;
    v.view_angles[2] = 0;
    v.camera_roll = 0;
    const s32 lr0 = -v.view_target[0] * d[0] >> 12;
    const s32 lr1 = -v.view_target[1] * d[1] >> 12;
    const s32 lr2 = -v.view_target[2] * d[2] >> 12;
    v.light_rotation[0] = static_cast<s16>(lr0);
    v.view_angles[1] = v.yaw;
    v.light_rotation[1] = static_cast<s16>(lr1);
    v.light_rotation[2] = static_cast<s16>(lr2);
    if (d[0] != 0) v.camera_roll = static_cast<s16>(((lr0 << 16) >> 4) / d[0]);
    if (d[1] != 0) v.camera_roll = static_cast<s16>(((lr1 << 16) >> 4) / d[1]);
    if (d[2] != 0) v.camera_roll = static_cast<s16>(((lr2 << 16) >> 4) / d[2]);
  } else {
    // Look-at mode.
    s32 d[3] = {(v.view_target[0] - v.view_eye[0]) >> 12, (v.view_target[1] - v.view_eye[1]) >> 12,
                (v.view_target[2] - v.view_eye[2]) >> 12};
    g.write_data(psx::Gte::IR1, static_cast<u32>(d[0]));
    g.write_data(psx::Gte::IR2, static_cast<u32>(d[1]));
    g.write_data(psx::Gte::IR3, static_cast<u32>(d[2]));
    g.execute(psx::Gte::kSqr0);
    const s32 sq[3] = {g.mac(1), g.mac(2), g.mac(3)};
    v.yaw = static_cast<s16>(ratan2(-d[0], d[2]));
    const s16 pitch = static_cast<s16>(ratan2(d[1], square_root0(sq[0] + sq[2])));
    v.pitch = pitch;
    v.view_angles[0] = pitch;
    v.view_angles[2] = 0;
    v.view_angles[1] = v.yaw;
    s32 len = square_root0(sq[0] + sq[1] + sq[2]);
    if (len == 0) len = 1;
    for (s32& c : d) c = (c << 12) / len;
    v.camera_roll = v.roll_input;
    const s32 roll = v.roll_input;
    v.light_rotation[0] = static_cast<s16>(roll * d[0] >> 12);
    v.light_rotation[1] = static_cast<s16>(roll * d[1] >> 12);
    v.light_rotation[2] = static_cast<s16>(roll * d[2] >> 12);
  }
  rot_matrix(v.view_angles, &v.view_matrix);
  const s32 c = bcos(static_cast<u32>(static_cast<s32>(v.pitch)));
  const s32 y = (c >> 6) < 0 ? 0x78 : (c >> 6) + 0x78;
  g.write_ctrl(psx::Gte::OFX, static_cast<u32>((v.shake_x + 0xA0) << 16));
  g.write_ctrl(psx::Gte::OFY, static_cast<u32>((v.shake_y + y) << 16));
}

void lights_build(const s32 orient[3]) {
  ViewState& v = g_view;
  v.colour_matrix = {};
  v.light_matrix = {};
  v.unlit_colour = {};
  v.unlit_light = {};
  const s32 r = v.stage_colour[0], gc = v.stage_colour[1], b = v.stage_colour[2];
  v.unlit_light.m[0][1] = 0x1000;
  v.unlit_colour.m[0][0] = static_cast<s16>(r << 5);
  v.unlit_colour.m[1][0] = static_cast<s16>(gc << 5);
  v.unlit_colour.m[2][0] = static_cast<s16>(b << 5);
  for (int i = 0; i < 3; ++i) {
    if (((v.light_mask >> i) & 1) == 0) continue;
    v.light_matrix.m[i][0] = v.light_dir[i][0];
    v.light_matrix.m[i][1] = v.light_dir[i][1];
    v.light_matrix.m[i][2] = v.light_dir[i][2];
    v.colour_matrix.m[0][i] = static_cast<s16>(v.light_colour[i][0] * r >> 7);
    v.colour_matrix.m[1][i] = static_cast<s16>(v.light_colour[i][1] * gc >> 7);
    v.colour_matrix.m[2][i] = static_cast<s16>(v.light_colour[i][2] * b >> 7);
  }
  const s16 angles[3] = {static_cast<s16>(orient[0]), static_cast<s16>(orient[1]), static_cast<s16>(orient[2])};
  psyq::MATRIX m{};
  rot_matrix(angles, &m);
  psyq::MulMatrix(&v.light_matrix, &m);
}

void gte_set_zoom() {
  ViewState& v = g_view;
  const s32 h = (v.zoom << 9) >> 12;
  gte().write_ctrl(psx::Gte::H, static_cast<u32>(h));
  set_fog_near(v.fog_near, h);
}

void gte_set_back_colour() {
  ViewState& v = g_view;
  s32 r = static_cast<s32>(v.ambient[0]) * v.stage_colour[0] >> 7;
  s32 gc = static_cast<s32>(v.ambient[1]) * v.stage_colour[1] >> 7;
  s32 b = static_cast<s32>(v.ambient[2]) * v.stage_colour[2] >> 7;
  if (r > 0xFF) r = 0xFF;
  if (gc > 0xFF) gc = 0xFF;
  if (b > 0xFF) b = 0xFF;
  psyq::SetBackColor(r, gc, b);
}

void obj_set_matrix(const s32 pos[3], s32 orient[3], const s32 scale[3]) {
  ViewState& v = g_view;
  s32 axis[3];
  if (orient[0] == 0) {
    axis[0] = bcos(static_cast<u32>(orient[1]));
    axis[1] = 0;
    axis[2] = bsin(static_cast<u32>(orient[1]));
  } else {
    orient[2] = orient[0];
    axis[0] = -bsin(static_cast<u32>(orient[1]));
    axis[1] = 0;
    axis[2] = bcos(static_cast<u32>(orient[1]));
  }
  axis[2] = -axis[2];
  psyq::MATRIX ry{}, ra{}, t{};
  rot_matrix_y(orient[1], &ry);
  rot_matrix_axis(axis, orient[2], &ra);
  psyq::SetRotMatrix(&v.view_matrix);
  for (int c = 0; c < 3; ++c) rt_column(ra, &t, c);
  psyq::SetRotMatrix(&t);
  for (int c = 0; c < 3; ++c) rt_column(ry, &t, c);
  psx::Gte& g = gte();
  const s16 dx = static_cast<s16>((pos[0] - v.view_eye[0]) >> 12);
  const s16 dy = static_cast<s16>((pos[1] - v.view_eye[1]) >> 12);
  const s16 dz = static_cast<s16>((pos[2] - v.view_eye[2]) >> 12);
  psyq::SetRotMatrix(&v.view_matrix);
  g.write_data(psx::Gte::VXY0, static_cast<u32>(static_cast<u16>(dx)) | static_cast<u32>(static_cast<u16>(dy)) << 16);
  g.write_data(psx::Gte::VZ0, static_cast<u32>(static_cast<s32>(dz)));
  g.execute(psx::Gte::kRtv0);
  const psyq::VECTOR tr{g.mac(1), g.mac(2), g.mac(3), 0};
  psyq::TransMatrix(&t, &tr);
  const psyq::VECTOR sc{scale[0], scale[1], scale[2], 0};
  psyq::ScaleMatrix(&t, &sc);
  gte_set_zoom();
  psyq::SetRotMatrix(&t);
  psyq::SetTransMatrix(&t);
}

void camera_update_matrices() {
  ViewState& v = g_view;
  camera_compute_view();
  lights_build(v.stage_orient);
  psyq::MATRIX m{};
  rot_matrix(v.light_rotation, &m);
  psyq::SetRotMatrix(&v.view_matrix);
  for (int c = 0; c < 3; ++c) rt_column(m, &v.render_view, c);
}

}  // namespace ss::game::battle
