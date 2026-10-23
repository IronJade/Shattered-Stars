// Spell code module 43d5518828 (entries 0x974-0x97b): Earthquake (spell 0x19): a rock giant
// (the data's five models, posed by a keyframe table) drops onto the field and stamps three
// times; each stamp sends a ground wave across the stage model (its vertices near the
// centre rise and fall with the wave) and the third hits every grounded unit of the other
// side. Functions 0x8008cc00 (the handler), 0x8008dfe8 (the pose blend), 0x8008e0c0 (the
// ground-wave height hook) and 0x8008e1c8 (dust round the giant); the module's writable data
// 0x8008e3a8..0x8008e3d8 (the pose, the waves, the stage backup) is QuakeState here.

#include <unordered_map>
#include <vector>

#include "game/battle/battle.h"
#include "game/battle/bsfx.h"
#include "game/battle/camera.h"
#include "game/battle/combat.h"
#include "game/battle/enemy_support.h"
#include "game/battle/particle.h"
#include "game/battle/render.h"
#include "game/battle/shot.h"
#include "game/battle/spell_fx.h"
#include "game/battle/spell_modules.h"
#include "game/battle/spellcast.h"
#include "game/battle/sprite.h"
#include "game/battle/stage.h"
#include "game/battle/unit.h"
#include "game/battle/unit_move.h"
#include "game/boot/rng.h"
#include "runtime/libgte.h"

namespace ss::game::battle {
namespace {

// The SpellCast's fields: s20 the giant's scale (growing to 0x2000), s24 / s28 the pose
// blended from / to (rows of the table 0x8008e2e0), s2c the blend (0..0x1000), s30 its speed,
// s34 the beat, s38 the speed's change, s3c the camera moved (step 3); obj the data's sprite
// set (the models' CLUT / tpage, the dust) and the giant's position.
constexpr u32 kPoseTable = 0x8008E2E0;  // 11 poses x 9 angles (u16)
constexpr u32 kCentres = 0x8008E3D8;    // the three waves' centres (SVECTOR, units)

// A vertex of the stage model near the centres (model_find_vertices_near's 12-byte entry).
struct NearVertex {
  s32 offset = 0;    // the vertex's word offset in the model
  s16 dist[4] = {};  // its xz distance to each point
};

// The module's writable data.
struct QuakeState {
  u16 angle[10] = {};            // 0x8008e3a8: the pose (angle 1 is never blended)
  s32 near_count = 0;            // 0x8008e3bc
  s32 radius[3] = {};            // 0x8008e3c0: the waves' radii (0 idle)
  bool backed_up = false;        // 0x8008e3d0 != 0
  std::vector<u8> backup;        // 0x8008e3d0: the stage model as it was (size 0x8008e3cc)
  std::vector<NearVertex> near;  // 0x8008e3d4 (0xf000 bytes on the console)
};
std::unordered_map<const SpellCast*, QuakeState> g_state;

// g_stage_height_fn 0x8006fe04 (spell_fx.h) is set to ground_wave while the waves run;
// g_wave_cast is the cast whose waves it reads (the module copy the console's pointer leads
// to).
const SpellCast* g_wave_cast = nullptr;

// The parent matrix obj_set_matrix_parent keeps for obj_set_matrix_child (0x80081130).
psyq::MATRIX g_parent_matrix{};

u16 rd16(const std::vector<u8>& b, size_t at) { return static_cast<u16>(b[at] | b[at + 1] << 8); }
u32 rd32(const u8* p) {
  return static_cast<u32>(p[0]) | static_cast<u32>(p[1]) << 8 | static_cast<u32>(p[2]) << 16 |
         static_cast<u32>(p[3]) << 24;
}

// The rotations obj_set_matrix builds: yaw orient[1], then a tilt of orient[2] about the
// horizontal axis (orient[0] != 0: the other axis, and orient[2] = orient[0]).
void object_rotations(s32 orient[3], psyq::MATRIX* ry, psyq::MATRIX* ra) {
  s32 axis[3];
  if (orient[0] != 0) {
    orient[2] = orient[0];
    axis[0] = -bsin(static_cast<u32>(orient[1]));
    axis[1] = 0;
    axis[2] = -bcos(static_cast<u32>(orient[1]));
  } else {
    axis[0] = bcos(static_cast<u32>(orient[1]));
    axis[1] = 0;
    axis[2] = -bsin(static_cast<u32>(orient[1]));
  }
  rot_matrix_y(orient[1], ry);
  rot_matrix_axis(axis, orient[2], ra);
}

// 0x800779c4 obj_set_matrix_parent (#1984.0): obj_set_matrix that keeps the object's
// rotation (tilt * yaw) and position (units) as the parent of the next child.
void obj_set_matrix_parent(const s32 pos[3], s32 orient[3], const s32 scale[3]) {
  ViewState& v = view();
  psyq::MATRIX ry{}, ra{}, t{};
  object_rotations(orient, &ry, &ra);
  psyq::MulMatrix0(&ra, &ry, &g_parent_matrix);
  psyq::MulMatrix0(&v.view_matrix, &g_parent_matrix, &t);
  g_parent_matrix.t[0] = pos[0] >> 12;
  g_parent_matrix.t[1] = pos[1] >> 12;
  g_parent_matrix.t[2] = pos[2] >> 12;
  const psyq::SVECTOR d{static_cast<s16>((pos[0] - v.view_eye[0]) >> 12),
                        static_cast<s16>((pos[1] - v.view_eye[1]) >> 12),
                        static_cast<s16>((pos[2] - v.view_eye[2]) >> 12), 0};
  psyq::VECTOR tr{};
  psyq::ApplyMatrix(&v.view_matrix, &d, &tr);
  psyq::TransMatrix(&t, &tr);
  const psyq::VECTOR sc{scale[0], scale[1], scale[2], 0};
  psyq::ScaleMatrix(&t, &sc);
  gte_set_zoom();
  psyq::SetRotMatrix(&t);
  psyq::SetTransMatrix(&t);
}

// 0x80077dcc obj_set_matrix_child (#1984.0): a child at pos (scaled, in the parent's frame)
// turned by orient relative to the parent; it becomes the parent of the next child.
void obj_set_matrix_child(const s32 pos[3], s32 orient[3], const s32 scale[3]) {
  ViewState& v = view();
  psyq::MATRIX ry{}, ra{}, tmp{}, t{};
  object_rotations(orient, &ry, &ra);
  const psyq::SVECTOR d{static_cast<s16>(((pos[0] >> 12) * scale[0]) >> 12),
                        static_cast<s16>(((pos[1] >> 12) * scale[1]) >> 12),
                        static_cast<s16>(((pos[2] >> 12) * scale[2]) >> 12), 0};
  psyq::VECTOR off{};
  psyq::ApplyMatrix(&g_parent_matrix, &d, &off);
  psyq::MulMatrix0(&g_parent_matrix, &ra, &tmp);
  psyq::MulMatrix0(&tmp, &ry, &g_parent_matrix);
  psyq::MulMatrix0(&v.view_matrix, &g_parent_matrix, &t);
  g_parent_matrix.t[0] += off.vx;
  g_parent_matrix.t[1] += off.vy;
  g_parent_matrix.t[2] += off.vz;
  const psyq::SVECTOR e{static_cast<s16>(g_parent_matrix.t[0] - (v.view_eye[0] >> 12)),
                        static_cast<s16>(g_parent_matrix.t[1] - (v.view_eye[1] >> 12)),
                        static_cast<s16>(g_parent_matrix.t[2] - (v.view_eye[2] >> 12)), 0};
  psyq::VECTOR tr{};
  psyq::ApplyMatrix(&v.view_matrix, &e, &tr);
  psyq::TransMatrix(&t, &tr);
  const psyq::VECTOR sc{scale[0], scale[1], scale[2], 0};
  psyq::ScaleMatrix(&t, &sc);
  gte_set_zoom();
  psyq::SetRotMatrix(&t);
  psyq::SetTransMatrix(&t);
}

// 0x800627c0 sv_distance_xz: sqrt(dx^2 + dz^2) of two SVECTORs (GTE SQR: 16-bit inputs).
s32 sv_distance_xz(const u8* a, const psyq::SVECTOR& b) {
  const s32 dx = static_cast<s16>(a[0] | a[1] << 8) - b.vx;
  const s32 dz = static_cast<s16>(a[4] | a[5] << 8) - b.vz;
  const psyq::VECTOR in{dx, 0, dz, 0};
  psyq::VECTOR sq{};
  psyq::Square0(&in, &sq);
  return square_root0(static_cast<s32>(static_cast<u32>(sq.vx) + static_cast<u32>(sq.vy) + static_cast<u32>(sq.vz)));
}

// 0x80061694 model_find_vertices_near: walks every part's 20 primitive sections of a battle
// model (the vertices are inline SVECTORs of each primitive) and lists each vertex whose xz
// distance to points[0] is at most 12000, with its distance to each point. Returns the count.
int model_find_vertices_near(std::vector<NearVertex>& out, const std::vector<u8>& model,
                             const psyq::SVECTOR* points, int npoints) {
  // Per section: the first vertex's offset in a primitive, the vertices tested, the stride
  // (section 9 is skipped; section 18 tests 3 of its quads' 4 vertices).
  struct Section {
    u8 first, verts, stride;
  };
  static constexpr Section kSections[20] = {
      {12, 3, 36}, {28, 3, 52}, {20, 3, 44}, {36, 3, 60}, {20, 3, 44}, {36, 3, 60}, {12, 4, 44},
      {36, 4, 68}, {0, 0, 48},  {48, 4, 80}, {20, 4, 52}, {44, 4, 76}, {4, 3, 28},  {4, 3, 36},
      {16, 3, 40}, {24, 3, 48}, {4, 4, 36},  {16, 3, 48}, {16, 4, 48}, {28, 4, 60}};
  out.clear();
  const u8* base = model.data();
  const u8* end = base + model.size();
  const u8* p = base;
  if (model.size() < 4) return 0;  // (port: bounds checks the console does not make)
  const u32 parts = rd32(p);
  p += 4;
  NearVertex slot;
  for (u32 part = 0; part < parts; ++part) {
    p += 4;
    for (const Section& s : kSections) {
      if (end - p < 4) return static_cast<int>(out.size());
      const u32 count = rd32(p);
      p += 4;
      if (s.verts == 0) {
        if (static_cast<size_t>(end - p) < static_cast<size_t>(count) * s.stride) return static_cast<int>(out.size());
        p += static_cast<size_t>(count) * s.stride;
        continue;
      }
      for (u32 i = 0; i < count && end - p >= s.stride; ++i, p += s.stride) {
        for (int k = 0; k < s.verts; ++k) {
          const u8* vtx = p + s.first + k * 8;
          for (int j = 0; j < npoints; ++j) slot.dist[j] = static_cast<s16>(sv_distance_xz(vtx, points[j]));
          if (slot.dist[0] < 12001) {
            slot.offset = static_cast<s32>(vtx - base) >> 2;
            out.push_back(slot);
          }
        }
      }
    }
  }
  return static_cast<int>(out.size());
}

// 0x8008e0c0: the stage height hook: inside a wave's crest (its radius minus the distance
// to its centre between 0 and min(radius, 0x1000)) the height is sin(that) / 8 units.
s32 ground_wave(s32 x, s32 y, s32 z) {
  const auto it = g_state.find(g_wave_cast);
  if (it == g_state.end()) return y;
  for (int j = 0; j < 3; ++j) {
    const s32 dx = x - spell_module_s16(g_wave_cast, kCentres + j * 8);
    const s32 dz = z - spell_module_s16(g_wave_cast, kCentres + j * 8 + 4);
    const u32 xx = static_cast<u32>(dx) * static_cast<u32>(dx), zz = static_cast<u32>(dz) * static_cast<u32>(dz);
    const s32 d = square_root0(static_cast<s32>(xx + zz));
    const s32 r = it->second.radius[j];
    const s32 lim = r < 0x1001 ? r : 0x1000;
    const s32 a = r - d;
    if (0 <= a && a <= lim) y = (bsin(static_cast<u32>(a)) / 8) << 12;
  }
  return y;
}

// 0x8008dfe8: the pose: each angle (but 1) the shorter way from pose 'from' to pose 'to'
// by t / 0x1000.
void set_pose(const SpellCast* sc, QuakeState& st, s32 from, s32 to, s32 t) {
  for (int i = 0; i < 10; ++i) {
    if (i == 1) continue;
    const int k = i > 0 ? i - 1 : 0;
    const s32 b = static_cast<u16>(spell_module_s16(sc, kPoseTable + static_cast<u32>(to * 0x12 + k * 2))) & 0xFFF;
    const s32 a = static_cast<u16>(spell_module_s16(sc, kPoseTable + static_cast<u32>(from * 0x12 + k * 2))) & 0xFFF;
    s32 dist = b - a;
    if (dist < 0) dist = a - b;
    const s32 wrapped = a < b ? b - (a + 0x1000) : (b + 0x1000) - a;
    const s32 delta = dist < 0x801 ? b - a : wrapped;
    st.angle[i] = static_cast<u16>((((delta * t) >> 12) + a) & 0xFFF);
  }
}

// 0x8008e1c8: two dust sprites (anim 1) a tick on the ground round the giant.
void spawn_dust(SpellCast* sc) {
  for (int i = 0; i < bg().frame_ticks << 1; ++i) {
    Sprite* sp = spellcast_sprite_spawn(sc, 1);
    if (sp == nullptr) continue;
    Vec4 p = vec_random_in_disc_xz(sc->obj.pos, 1000);
    p.y = bobj_ground_height(&sc->obj);
    sprite_set_pos_v(sp, p.data());
    sprite_set_fade(sp, 0x80, -4, 8);
    const s32 scale = rng_between(0x2000, 20000);
    sp->scale_y = scale;
    sp->scale_x = scale;
    sp->layer = 8;
  }
}

// Steps 2-5: the giant, a body (model 0) with two limbs of two parts each (models 1-2 and
// 3-4) as child matrices, at the cast's position scaled by s20. (The tilt orient[2] carries
// over between the calls as on the console.)
void draw_giant(SpellCast* sc, const QuakeState& st) {
  const auto model = [&](int k, bool culled) {
    const u8* m = spellcast_model(sc, k);
    if (m == nullptr) return;
    const AnimSet* set = sc->obj.animset;
    if (culled) {
      model_draw_culled(m, set->clut[0], set->tpage[0], 4);
    } else {
      model_draw(m, set->clut[0], set->tpage[0], 4);
    }
  };
  stage_colour_set_grey(0x100);
  s32 orient[3] = {static_cast<s16>(st.angle[0]), static_cast<s16>(st.angle[1]), 0};
  const s32 scale[3] = {sc->s20, sc->s20, sc->s20};
  lights_build_oriented(orient);
  obj_set_matrix_parent(sc->obj.pos, orient, scale);
  model(0, false);
  orient[0] = static_cast<s16>(st.angle[2]), orient[1] = static_cast<s16>(st.angle[3]), orient[2] = 0;
  const s32 limb_a0[3] = {0, static_cast<s32>(0xFFEB6000), 0xC8000};
  obj_set_matrix_child(limb_a0, orient, scale);
  model(1, true);
  orient[0] = static_cast<s16>(st.angle[4]), orient[1] = static_cast<s16>(st.angle[5]);
  const s32 limb_a1[3] = {static_cast<s32>(0xFFFEC000), 0x12C000, 0xF000};
  obj_set_matrix_child(limb_a1, orient, scale);
  model(2, true);
  orient[0] = static_cast<s16>(st.angle[0]), orient[1] = static_cast<s16>(st.angle[1]);
  obj_set_matrix_parent(sc->obj.pos, orient, scale);
  orient[0] = static_cast<s16>(st.angle[6]), orient[1] = static_cast<s16>(st.angle[7]);
  const s32 limb_b0[3] = {0, static_cast<s32>(0xFFEB6000), static_cast<s32>(0xFFF38000)};
  obj_set_matrix_child(limb_b0, orient, scale);
  model(3, true);
  orient[0] = static_cast<s16>(st.angle[8]), orient[1] = static_cast<s16>(st.angle[9]);
  const s32 limb_b1[3] = {static_cast<s32>(0xFFFEC000), 0x12C000, static_cast<s32>(0xFFFF1000)};
  obj_set_matrix_child(limb_b1, orient, scale);
  model(4, true);
  stage_colour_restore();
}

// A stamp: the shake, wave k, debris, a sphere, two rings, sparks, 8 rock sprites (anim 1);
// the third stamp hits every grounded unit of the other side inside the arena (element 1).
void stamp(SpellCast* sc, QuakeState& st, int k, const Vec4 centres[3]) {
  BattleGlobals& g = bg();
  screen_shake2(6, 4, 4, 4, 0x20);
  st.radius[k] = 0xC0;
  bsfx_play_at(0x50, &sc->obj);
  bsfx_play_at(0x4C, &sc->obj);
  bsfx_play_at(0x22, &sc->obj);
  const s32* at = centres[k].data();
  const s32 spread[3] = {0x200, 0x200, 0x200};
  effect_debris_burst(at, spread, 6, 1, 1, 0x8000, 0xD);
  effect_spawn_sphere_at_default(at, 7, 8, 0x50, 0x50, 0xFF40010, 400, 0);
  for (int i = 0; i < 2; ++i) particle_spawn_ring(at, 0x10, 0x90, 0xC0, 0x5A, 0x1010, 0x2040FF, 0x100, i << 10);
  effect_spawn_sparks_at(at, 0x10, 0x60, 0x100, 0x40, 0x5A, 2, 1, 0x80, 0);
  for (int i = 0; i < 8; ++i) {
    Sprite* sp = spellcast_sprite_spawn(sc, 1);
    if (sp == nullptr) continue;
    const Vec4 p = vec_random_around(at, rng_between(0x32, 400));
    sprite_set_pos_v(sp, p.data());
    sprite_set_fade(sp, 0x80, -2, 0x1E);
    const s32 scale = rng_between(0x41C2, 0x8000);
    sp->scale_y = scale;
    sp->scale_x = scale;
    sp->layer = 8;
  }
  g.zoom_kick = 6000;
  g.zoom_kick_decay = 200;
  if (k != 2 || static_cast<u8>(sc->combo - 1) <= 1) return;
  const int side = sc->combo == 3 ? sc->caster->is_enemy ^ 1 : sc->caster->is_enemy;
  if (sc->combo == 4) sc->eff_power += spellcast_slot(sc->slot ^ 1)->eff_power;
  const int count = side != 0 ? 4 : 8;
  const s32* b = stage().bounds;
  Unit* list[8] = {};
  int n = 0;
  for (int i = 0; i < count; ++i) {
    Unit* u = side == 0 ? static_cast<Unit*>(g.enemy_units[static_cast<size_t>(i)])
                        : static_cast<Unit*>(g.party_units[static_cast<size_t>(i)]);
    if (unit_is_down(u) || u->airborne == 1 || u->flying != 0) continue;
    if (u->pos[0] < b[0] || !(u->pos[0] < b[2])) continue;
    if (u->pos[2] < b[1] || !(u->pos[2] < b[3])) continue;
    list[n++] = u;
  }
  if (n != 0) spellcast_damage_units(sc, n, list, sc->eff_power, 1, 0x10, side);
}

// Step 4 (also the end of a blend in step 3): the next beat s34 picks the next pose and its
// speed, camera moves, waits and the stamps (beats 2, 5 and 9); past beat 10 the giant
// rises (step 5).
void next_beat(SpellCast* sc, QuakeState& st, const Vec4 centres[3]) {
  BattleGlobals& g = bg();
  int stamp_no = 0;
  switch (sc->s34) {
    case 1:
      sc->s28 = 2;
      sc->s30 = 0xC0;
      sc->s38 = 0x20;
      spellcast_wait(sc, 0xF);
      bsfx_play_ui(0x21);
      break;
    case 2: {
      const s32 yaw = rng_range(2) != 0 ? 0xCE3 : 0x71C;
      stamp_no = 1;
      camera_move_to(g.camera, kZeroVector, yaw, 0x155, 0x1BD0, 0, 0x80, 5);
      g.camera->zoom_target = 0x1000;
      g.camera->zoom_speed = 0xC00;
      sc->s28 = 3;
      sc->s30 = 0x80;
      sc->s38 = 0;
      spellcast_wait(sc, 0x14);
      break;
    }
    case 3: sc->s28 = 4; break;
    case 4:
      sc->s28 = 5;
      sc->s30 = 0x100;
      spellcast_wait(sc, 0xF);
      sc->step = 3;
      bsfx_play_ui(0x21);
      break;
    case 5:
      sc->s28 = 6;
      sc->s30 = 0x80;
      spellcast_wait(sc, 0xF);
      stamp_no = 2;
      sc->step = 3;
      break;
    case 6: {
      const s32 yaw = rng_range(2) != 0 ? 0xEAA : 0xE3;
      const s32 dist = rng_range(2) != 0 ? 0x1400 : 0x1000;
      camera_move_to(g.camera, kZeroVector, yaw, 0x9F, dist, 0, 0x80, 5);
      sc->s28 = 7;
      break;
    }
    case 7:
      sc->s28 = 6;
      sc->s30 = 0x180;
      bsfx_play_ui(0x21);
      break;
    case 8: sc->s28 = 8; break;
    case 9:
      stamp_no = 3;
      sc->s28 = 9;
      sc->s30 = 0x80;
      bsfx_play_at(0x1E, &sc->obj);
      spellcast_wait(sc, 0x1E);
      break;
    case 10:
      camera_move_to(g.camera, kZeroVector, 0xC00, 0x93, 0x1000, 0xB4, 0x80, 5);
      sc->s28 = 10;
      break;
    default:
      sc->step = 5;
      sound_handle_fade_out(bsfx_play_at(0x2D, &sc->obj), 0x3C, 0x78);
      break;
  }
  if (sc->s34 < 0xB) sc->step = 3;
  if (stamp_no != 0) stamp(sc, st, stamp_no - 1, centres);
  sc->s2c += g.frame_ticks * sc->s30;
}

void earthquake(SpellCast* sc) {
  BattleGlobals& g = bg();
  Unit* c = sc->caster;
  const Unit* t = sc->target;
  // The module's data is fresh at each cast (it is unpacked with the cast); nothing reads
  // it before step 1.
  if (sc->step == 0) {
    QuakeState fresh;
    for (int i = 0; i < 10; ++i) fresh.angle[i] = static_cast<u16>(spell_module_s16(sc, 0x8008E3A8 + i * 2));
    fresh.near_count = spell_module_s32(sc, 0x8008E3BC);
    for (int j = 0; j < 3; ++j) fresh.radius[j] = spell_module_s32(sc, 0x8008E3C0 + j * 4);
    g_state[sc] = std::move(fresh);
  }
  QuakeState& st = g_state[sc];
  sc->centre[0] = t->pos[0];
  sc->centre[1] = t->pos[1] + t->size[1] * -0x2000;
  sc->centre[2] = t->pos[2];
  sc->centre[3] = 0;
  Vec4 centres[3];
  for (int k = 0; k < 3; ++k) {
    centres[k].x = spell_module_s16(sc, kCentres + k * 8) << 12;
    centres[k].y = (spell_module_s16(sc, kCentres + k * 8 + 2) - 0x32) * 0x1000;
    centres[k].z = spell_module_s16(sc, kCentres + k * 8 + 4) << 12;
  }
  if (static_cast<u32>(sc->step - 2) < 4) draw_giant(sc, st);
  // (The original first stores INT * 4 + power * 2 + 2300 here, overwritten unread.)
  sc->eff_power = c->st()->intel[2] * 4 + sc->power * 4 + 0x8FC;
  if ((g.flags & 4) != 0) return;
  // The waves move the stage model's vertices near the centres (from the backup).
  if (!st.backed_up || (st.radius[0] == 0 && st.radius[1] == 0 && st.radius[2] == 0)) {
    g_stage_height_fn = nullptr;
  } else {
    g_stage_height_fn = ground_wave;
    g_wave_cast = sc;
    std::vector<u8>& model = stage().blocks[0];
    for (int i = 0; i < st.near_count; ++i) {
      const NearVertex& v = st.near[static_cast<size_t>(i)];
      const size_t at = static_cast<size_t>(v.offset) * 4 + 2;
      s16 y = static_cast<s16>(rd16(st.backup, at));
      for (int j = 0; j < 3; ++j) {
        const s32 r = st.radius[j];
        const s32 lim = r < 0x1001 ? r : 0x1000;
        const s32 a = r - v.dist[j];
        if (0 <= a && a <= lim) y = static_cast<s16>(y + bsin(static_cast<u32>(a) & 0xFFF) / 8);
      }
      model[at] = static_cast<u8>(y);
      model[at + 1] = static_cast<u8>(static_cast<u16>(y) >> 8);
    }
  }
  for (int j = 0; j < 3; ++j) {
    if (st.radius[j] == 0) continue;
    st.radius[j] += g.frame_ticks * 0x30;
    if (9999 < st.radius[j]) st.radius[j] = 0;
  }
  if (spellcast_timer_tick(sc) != 0) return;
  switch (sc->step) {
    case 0:
      // Waits for the camera.
      spellcast_dim_begin(sc, c->is_enemy);
      battle_flash_set(sc, 0x40, 0x40, 0x40);
      if (spellcast_leads_camera(sc)) {
        camera_reset_mode(g.camera);
        sc->obj.pos[1] = 0x3E8000;
        sc->obj.pos[0] = 0;
        sc->obj.pos[2] = 0;
        st.radius[0] = st.radius[1] = st.radius[2] = 0;
        sc->step = 1;
      }
      return;
    case 1: {
      // The data loaded: the first pose, the stage model backed up and its vertices near
      // the centres listed, the camera, the rumble.
      if (sc->data != nullptr) return;
      sc->step = 2;
      set_pose(sc, st, 0, 0, 0);
      if (!stage().blocks.empty()) {  // (port: the console always has a stage model)
        const std::vector<u8>& model = stage().blocks[0];
        st.backup = model;
        st.backed_up = true;
        // ORIGINAL BUG? (to review): the copy loop fills two of the three points; the third
        // is whatever the scratchpad holds at 0x1f800010 (here 0, as the module's centres).
        psyq::SVECTOR points[3] = {};
        for (int k = 0; k < 2; ++k) {
          points[k].vx = spell_module_s16(sc, kCentres + k * 8);
          points[k].vy = spell_module_s16(sc, kCentres + k * 8 + 2);
          points[k].vz = spell_module_s16(sc, kCentres + k * 8 + 4);
        }
        st.near_count = model_find_vertices_near(st.near, model, points, 3);
      }
      camera_move_to(g.camera, kZeroVector, 0xC00, 0x71, 0x1400, 0, 0x80, 5);
      camera_move_to(g.camera, kZeroVector, 0xF8E, 0x155, 0xC00, 0x3C, 0x80, 5);
      sound_handle_fade_out(bsfx_play_at(0x2D, &sc->obj), 0x5A, 0x78);
      return;
    }
    case 2: {
      // The giant drops in, growing; landed, the first blend starts.
      spawn_dust(sc);
      sc->s20 += g.frame_ticks * 0x200;
      if (0x2000 < sc->s20) sc->s20 = 0x2000;
      sc->obj.pos[1] -= g.frame_ticks << 16;
      if (!(sc->obj.pos[1] < -0x64000)) return;
      sc->obj.pos[1] = -0x64000;
      const s32 yaw = rng_range(2) != 0 ? 0xEAA : 0x155;
      camera_move_to(g.camera, kZeroVector, yaw, -0xE3, 0xC00, 0x32, 0x80, 5);
      spellcast_wait(sc, 0x3C);
      sc->s28 = 1;
      sc->s24 = 0;
      sc->s2c = 0;
      sc->s30 = 0x40;
      sc->s34 = 0;
      sc->s38 = 0;
      bsfx_play_at(0x34, &sc->obj);
      bsfx_play_at(0x28, &sc->obj);
      bsfx_play_at(0x29, &sc->obj);
      [[fallthrough]];
    }
    case 3:
      // Blends pose s24 into s28; at the end the next beat.
      if (sc->s34 == 0 && sc->s3c == 0) {
        sc->s3c = 1;
        const s32 above[3] = {0, -0x258000, 0};
        const s32 yaw = rng_range(2) != 0 ? 0xE3 : -0xE3;
        camera_move_to(g.camera, above, yaw, -0xE3, 800, 0, 0x80, 5);
        g.camera->zoom_target = 0x400;
        g.camera->zoom_speed = 0xC00;
      }
      set_pose(sc, st, sc->s24, sc->s28, sc->s2c);
      sc->s2c += g.frame_ticks * sc->s30;
      sc->s30 += sc->s38;
      if (sc->s2c < 0x1000) return;
      set_pose(sc, st, sc->s28, sc->s28, 0);
      sc->step = 4;
      sc->s2c = 0;
      sc->s24 = sc->s28;
      ++sc->s34;
      [[fallthrough]];
    case 4:
      next_beat(sc, st, centres);
      return;
    case 5:
      // The giant rises out of view.
      spawn_dust(sc);
      sc->obj.pos[1] += g.frame_ticks << 16;
      if (sc->obj.pos[1] <= 0x3E8000) return;
      sc->step = 6;
      battle_flash_set(sc, 0x80, 0x80, 0x80);
      spellcast_dim_end(sc);
      bsfx_play_at(0x34, &sc->obj);
      bsfx_play_at(0x21, &sc->obj);
      return;
    case 6:
      if (spellcast_effects_gone(sc) != 0 && st.radius[2] == 0) sc->step = 7;
      return;
    case 7:
      // The stage model restored.
      if (st.backed_up) {
        g_stage_height_fn = nullptr;
        stage().blocks[0] = st.backup;
        st.backup.clear();
        st.backed_up = false;
        st.near.clear();
      }
      sc->step = 8;
      spell_landed_flag() = 1;
      g_stage_height_fn = nullptr;
      sc->finished = 1;
      g_state.erase(sc);
      return;
    default: return;
  }
}

}  // namespace

void spell_mod_earthquake(SpellCast* sc) { earthquake(sc); }

}  // namespace ss::game::battle
