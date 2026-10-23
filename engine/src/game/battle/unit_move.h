#pragma once

#include "core/types.h"
#include "game/battle/unit.h"

// Unit movement: the swept-box path probe, approach points beside the target, the
// walking step toward them and the vector helpers they use (0x8004e24c..0x80050190,
// 0x8005292c, 0x80052a3c.., 0x80067164..). parts/p3.md "Unit".
namespace ss::game::battle {

struct Vec4 {
  s32 x = 0, y = 0, z = 0, w = 0;
  s32* data() { return &x; }
  const s32* data() const { return &x; }
};

// The probe's "clear" marker in x (-2500.0).
constexpr s32 kPathClear = -0x9C40000;

s32 vec_distance_fx(const s32 a[3], const s32 b[3]);     // 0x80067164: 3-D, 20.12
s32 vec_distance_xz_fx(const s32 a[3], const s32 b[3]);  // 0x80067204: ground plane
Vec4 vec_direction(const s32 from[3], const s32 to[3]);      // 0x80067284: unit vector (4.12)
Vec4 vec_direction_xz(const s32 from[3], const s32 to[3]);   // 0x80067484: y = 0
Vec4 vec_velocity_toward(const s32 from[3], const s32 to[3], s32 speed);  // 0x80048d94
Vec4 vec_velocity_toward_xz(const s32 from[3], const s32 to[3], s32 speed);  // 0x80048e28 (y = 0)
// 0x8004906c: a jump from 'from' to land on 'to' (aim 1: half its height up, 2: its top;
// else its feet): t = ground distance / speed; out = the xz direction times speed (the
// console scales z by 24: original_bugs.md B9, fixed); out[1] = (to.y - aim - from.y) /
// max(t >> 12, 1) - t / 2. Returns t.
s32 projectile_arc_velocity(s32 out[4], const BObj* from, const BObj* to, s32 speed, int aim);
// 0x80048f60: a random point on a sphere of 'radius' units around centre (yaw rng_range(0x1000),
// tilt rng_spread(0x400)).
Vec4 vec_random_around(const s32 centre[3], s32 radius);
// 0x800491b4: out = {pitch ratan2(y, |v.xz|), yaw ratan2(z, x), 0}. The length squares the
// low 16 bits of x and z (GTE SQR on IR1 / IR3).
void vec_to_angles(s32 out[3], const s32 v[3]);

// 0x8004e7ac: sweeps u's box by (vx, vz) x look-ahead (+0x162) against the living
// units, the stage obstacles and the arena edge; the nearest blocking point (a cell
// centre of the blocking box, or a point beyond the wall), or x = kPathClear. 'dist'
// limits how far away blockers count. Sets +0x12c when an arena wall blocked.
Vec4 unit_probe_path(Unit* u, s32 vx, s32 vz, s32 dist);
bool unit_find_approach_point(Unit* u, Vec4* out);     // 0x8004fe2c
void unit_begin_approach(Unit* u);                     // 0x8004e24c
// 0x8004e2b0: one path step (moves when 'apply'): 1 arrived, 0 walking, 2 no point /
// out of reach, 3 within reach without a point.
int unit_move_toward_goal(Unit* u, int apply);
int unit_can_reach_target(Unit* u, Unit* target, int allow_straight);  // 0x8005292c
s32 unit_jump_time(const Unit* u, s32 v, const s32 dest[3]);          // [6] 0x80052a3c
s32 unit_jump_rise_time(const Unit* u, s32 v, const s32 dest[3]);     // [7] 0x80052ae0

}  // namespace ss::game::battle
