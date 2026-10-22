#include "game/battle/unit_move.h"

#include <utility>

#include "game/battle/render.h"
#include "game/battle/sprite.h"
#include "game/battle/stage.h"
#include "game/boot/rng.h"

namespace ss::game::battle {
namespace {

s32 iabs32(s32 v) { return v < 0 ? -v : v; }

// The GTE's 16-bit view of a 32-bit value (IR / RT registers).
s32 lo16(s32 v) { return static_cast<s16>(static_cast<u16>(v)); }

// a * b + c * d + e * f with the CPU's 32-bit wrap-around.
s32 dot3_wrap(s32 a, s32 b, s32 c, s32 d, s32 e, s32 f) {
  const u32 r = static_cast<u32>(a) * static_cast<u32>(b) + static_cast<u32>(c) * static_cast<u32>(d) +
                static_cast<u32>(e) * static_cast<u32>(f);
  return static_cast<s32>(r);
}

// Unit i of the 12 (party 0-3, enemies 4-11).
Unit* unit_slot(int i) {
  const BattleGlobals& g = bg();
  return i < 4 ? static_cast<Unit*>(g.party_units[i]) : g.enemy_units[i - 4];
}

bool overlaps(s32 c0, s32 c1, s32 b0, s32 b1) {
  return (c0 <= b0 && b0 < c1) || (c0 < b1 && b1 < c1) || (b0 <= c0 && c1 <= b1);
}

}  // namespace

s32 vec_distance_fx(const s32 a[3], const s32 b[3]) {
  // gte_ldlvl + Square0: the components go through the 16-bit IR registers.
  const s32 x = lo16((a[0] - b[0]) >> 12), y = lo16((a[1] - b[1]) >> 12), z = lo16((a[2] - b[2]) >> 12);
  return square_root0(x * x + y * y + z * z) << 12;
}

s32 vec_distance_xz_fx(const s32 a[3], const s32 b[3]) {
  const s32 x = lo16((a[0] - b[0]) >> 12), z = lo16((a[2] - b[2]) >> 12);
  return square_root0(x * x + z * z) << 12;
}

Vec4 vec_direction(const s32 from[3], const s32 to[3]) {
  s32 d = vec_distance_fx(from, to) >> 12;
  if (d == 0) d = 1;
  Vec4 v;
  v.x = (to[0] - from[0]) / d;
  v.y = (to[1] - from[1]) / d;
  v.z = (to[2] - from[2]) / d;
  return v;
}

Vec4 vec_direction_xz(const s32 from[3], const s32 to[3]) {
  s32 d = vec_distance_xz_fx(from, to) >> 12;
  if (d == 0) d = 1;
  Vec4 v;
  v.x = (to[0] - from[0]) / d;
  v.y = 0;
  v.z = (to[2] - from[2]) / d;
  return v;
}

Vec4 vec_velocity_toward_xz(const s32 from[3], const s32 to[3], s32 speed) {
  Vec4 v = vec_direction_xz(from, to);
  v.x *= speed;
  v.y *= speed;
  v.z *= speed;
  return v;
}

void vec_to_angles(s32 out[3], const s32 v[3]) {
  const s32 x = static_cast<s16>(v[0]), z = static_cast<s16>(v[2]);
  const s32 yaw = ratan2(v[2], v[0]);
  const s32 len = square_root0(static_cast<s32>(static_cast<u32>(x * x) + static_cast<u32>(z * z)));
  out[0] = ratan2(v[1], len);
  out[1] = yaw;
  out[2] = 0;
}

Vec4 vec_random_around(const s32 centre[3], s32 radius) {
  const u32 a = rng_range(0x1000);
  const s32 b = rng_spread(0x400);
  Vec4 v;
  v.x = ((bcos(a) * bcos(static_cast<u32>(b))) >> 12) * radius + centre[0];
  v.y = centre[1] - bsin(a) * radius;
  v.z = ((bcos(a) * bsin(static_cast<u32>(b))) >> 12) * radius + centre[2];
  return v;
}

Vec4 vec_velocity_toward(const s32 from[3], const s32 to[3], s32 speed) {
  Vec4 v = vec_direction(from, to);
  v.x *= speed;
  v.y *= speed;
  v.z *= speed;
  return v;
}

Vec4 unit_probe_path(Unit* u, s32 vx, s32 vz, s32 dist) {
  const BattleGlobals& g = bg();
  const StageData& st = stage();
  s32 step = 0;
  if (u->airborne == 0 && u->flying == 0)
    step = stage_ground_height((u->pos[0] + vx * 0x20) >> 12, (u->pos[2] + vz * 0x20) >> 12) - u->pos[1];
  if (step > 0) step = 0;
  u->hit_wall = 0;
  // The swept box: two opposite corners now (P0 top / P1 foot) and after the look-ahead
  // (P2 / P3, lifted by the ground step), ordered by x.
  s32 P[4][3];
  const s32 la = u->lookahead;
  const s32 ox = vx * 3 - 0x4000, oz = vz * 3 - 0x4000;
  const s32 x0 = u->pos[0] + u->size[0] * -0x2000 + ox, x1 = u->pos[0] + u->size[0] * 0x2000 + ox;
  const s32 z0 = u->pos[2] + u->size[2] * -0x2000 + oz, z1 = u->pos[2] + u->size[2] * 0x2000 + oz;
  const s32 ytop = u->pos[1] + u->size[1] * -0x4000, yfoot = u->pos[1];
  const s32 x2 = x0 + vx * la + 0x8000, x3 = x1 + vx * la + 0x8000;
  const s32 z2 = z0 + vz * la + 0x8000, z3 = z1 + vz * la + 0x8000;
  if (x2 < x0) {
    P[0][0] = x2, P[0][1] = ytop + step, P[0][2] = z2;
    P[1][0] = x3, P[1][1] = yfoot + step, P[1][2] = z3;
    P[2][0] = x0, P[2][1] = ytop, P[2][2] = z0;
    P[3][0] = x1, P[3][1] = yfoot, P[3][2] = z1;
  } else {
    P[0][0] = x0, P[0][1] = ytop, P[0][2] = z0;
    P[1][0] = x1, P[1][1] = yfoot, P[1][2] = z1;
    P[2][0] = x2, P[2][1] = ytop + step, P[2][2] = z2;
    P[3][0] = x3, P[3][1] = yfoot + step, P[3][2] = z3;
  }
  s32 bmin[3] = {0x4E20000, 0x4E20000, 0x4E20000}, bmax[3] = {-0x4E20000, -0x4E20000, -0x4E20000};
  for (const auto& p : P)
    for (int k = 0; k < 3; ++k) {
      if (p[k] < bmin[k]) bmin[k] = p[k];
      if (bmax[k] < p[k]) bmax[k] = p[k];
    }
  for (int k = 0; k < 3; ++k) {
    bmin[k] -= 0x12C000;
    bmax[k] += 0x12C000;
    if (bmax[k] < bmin[k]) std::swap(bmin[k], bmax[k]);
  }
  if (u->ignore_units == 0) {
    for (int i = 0; i < 0x24; ++i) {
      s32 X0, Y0, Z0, X1, Y1, Z1;
      const Unit* other = nullptr;
      if (i < 12) {
        other = unit_slot(i);
        if (unit_is_down(other) || other == u) continue;
        X0 = other->pos[0] + other->size[0] * -0x2000;
        X1 = other->pos[0] + other->size[0] * 0x2000;
        Z0 = other->pos[2] + other->size[2] * -0x2000;
        Z1 = other->pos[2] + other->size[2] * 0x2000;
        Y0 = other->pos[1] + other->size[1] * -0x4000 + 0x1000;
        Y1 = other->pos[1] - 0x1000;
      } else {
        const auto& o = st.obstacles[i - 12];
        if (o.kind == 0) continue;
        X0 = o.min[0], Y0 = o.min[1], Z0 = o.min[2];
        X1 = o.max[0], Y1 = o.max[1], Z1 = o.max[2];
      }
      // The box in cells of at most 400 units.
      s32 nx = (X1 - X0) / 0x190000, nz = (Z1 - Z0) / 0x190000;
      if (nx == 0) nx = 1;
      if (nz == 0) nz = 1;
      for (s32 iz = 0; iz < nz; ++iz) {
        for (s32 ix = 0; ix < nx; ++ix) {
          const s32 W = X1 - X0, D = Z1 - Z0;
          const s32 cx0 = (W * ix) / nx + X0, cx1 = (W * (ix + 1)) / nx + X0;
          const s32 cz0 = (D * iz) / nz + Z0, cz1 = (D * (iz + 1)) / nz + Z0;
          const s32 cy0 = Y0, cy1 = Y1;
          s32 C[3] = {(cx1 - cx0) / 2 + cx0, (cy1 - cy0) / 2 + cy0, (cz1 - cz0) / 2 + cz0};
          if (other != nullptr) C[1] = other->pos[1];
          if (vec_distance_xz_fx(u->pos, C) > dist) continue;
          if (!overlaps(cx0, cx1, bmin[0], bmax[0]) || !overlaps(cz0, cz1, bmin[2], bmax[2]) ||
              !overlaps(cy0, cy1, bmin[1], bmax[1]))
            continue;
          bool inside = false;
          for (const auto& p : P)
            if (cx0 < p[0] && p[0] < cx1 && cy0 < p[1] && p[1] < cy1 && cz0 < p[2] && p[2] < cz1) {
              inside = true;
              break;
            }
          if (!inside) {
            // The cell centre against the 8 faces of the swept box grown by the cell's
            // half size (plane normals by the GTE outer product).
            s32 hx = (cx1 - cx0) / 2;
            const s32 ccx = hx + cx0;
            if (cx1 - cx0 < 0) hx = (cx0 - cx1) / 2;
            s32 hy = (cy1 - cy0) / 2;
            const s32 ccy = hy + cy0;
            if (cy1 - cy0 < 0) hy = (cy0 - cy1) / 2;
            s32 hz = (cz1 - cz0) / 2;
            const s32 ccz = hz + cz0;
            if (cz1 - cz0 < 0) hz = (cz0 - cz1) / 2;
            const s32 a0 = P[0][0] - hx, c0 = P[0][2] - hz, a1 = P[1][0] + hx, c1 = P[1][2] + hz;
            const s32 a2 = P[2][0] - hx, c2 = P[2][2] - hz, a3 = P[3][0] + hx, c3 = P[3][2] + hz;
            s32 b0, b1, b2, b3;
            if (P[1][1] < P[0][1]) {
              b0 = P[0][1] + hy, b1 = P[1][1] - hy;
            } else {
              b0 = P[0][1] - hy, b1 = P[1][1] + hy;
            }
            if (P[3][1] < P[2][1]) {
              b2 = P[2][1] + hy, b3 = P[3][1] - hy;
            } else {
              b2 = P[2][1] - hy, b3 = P[3][1] + hy;
            }
            const bool f = c0 < c2;
            // Face point A and edges V1, V2 (>> 12); the cases leave values of earlier
            // cases in place, as the scratchpad does.
            s32 A[3] = {}, V1[3] = {}, V2[3] = {};
            int face = 0;
            for (; face < 8; ++face) {
              if (!f) {
                switch (face) {
                  case 0:
                    A[0] = a0, A[1] = b0, A[2] = c0;
                    V1[0] = (a0 - a1) >> 12, V1[1] = 0, V1[2] = (c0 - c1) >> 12;
                    V2[0] = (a2 - a0) >> 12, V2[1] = (b2 - b0) >> 12, V2[2] = (c2 - c0) >> 12;
                    break;
                  case 1:
                    A[1] = b1;
                    V1[0] = (a0 - a2) >> 12, V1[1] = (b1 - b3) >> 12, V1[2] = (c0 - c2) >> 12;
                    V2[0] = (a1 - a0) >> 12, V2[1] = 0, V2[2] = (c1 - c0) >> 12;
                    break;
                  case 2:
                    A[2] = c1;
                    V1[0] = 0, V1[1] = (b1 - b0) >> 12, V1[2] = 0;
                    V2[0] = 0, V2[2] = (c0 - c1) >> 12;
                    break;
                  case 3:
                    A[2] = c0;
                    V2[0] = (a2 - a0) >> 12, V2[1] = (b3 - b1) >> 12, V2[2] = (c2 - c0) >> 12;
                    break;
                  case 4:
                    A[0] = a2, A[1] = b3, A[2] = c2;
                    V1[1] = (b3 - b2) >> 12;
                    V2[0] = (a3 - a2) >> 12, V2[1] = 0, V2[2] = 0;
                    break;
                  case 5:
                    A[0] = a3;
                    V2[0] = 0, V2[2] = (c3 - c2) >> 12;
                    break;
                  case 6:
                    A[2] = c3;
                    V2[0] = (a1 - a3) >> 12, V2[1] = (b1 - b3) >> 12, V2[2] = (c1 - c3) >> 12;
                    break;
                  default:
                    A[0] = a1, A[1] = b1, A[2] = c1;
                    V1[1] = (b1 - b0) >> 12;
                    V2[0] = (a0 - a1) >> 12, V2[1] = 0, V2[2] = 0;
                    break;
                }
              } else {
                switch (face) {
                  case 0:
                    A[0] = a0, A[1] = b0, A[2] = c1;
                    V1[0] = (a0 - a2) >> 12, V1[1] = (b0 - b2) >> 12, V1[2] = (c1 - c3) >> 12;
                    V2[0] = (a1 - a0) >> 12, V2[1] = 0, V2[2] = (c0 - c1) >> 12;
                    break;
                  case 1:
                    A[1] = b1;
                    V1[0] = (a0 - a1) >> 12, V1[1] = 0, V1[2] = (c1 - c0) >> 12;
                    V2[0] = (a2 - a0) >> 12, V2[1] = (b3 - b1) >> 12, V2[2] = (c3 - c1) >> 12;
                    break;
                  case 2:
                    A[2] = c1;
                    V1[0] = 0, V1[1] = (b1 - b0) >> 12, V1[2] = 0;
                    V2[0] = 0, V2[1] = 0, V2[2] = (c0 - c1) >> 12;
                    break;
                  case 3:
                    A[0] = a2, A[1] = b3, A[2] = c3;
                    V1[1] = (b3 - b2) >> 12;
                    V2[0] = (a0 - a2) >> 12, V2[1] = (b1 - b3) >> 12, V2[2] = (c1 - c3) >> 12;
                    break;
                  case 4:
                    A[0] = a3;
                    V2[0] = (a2 - a3) >> 12, V2[1] = 0, V2[2] = 0;
                    break;
                  case 5:
                    A[2] = c2;
                    V2[0] = 0, V2[2] = (c3 - c2) >> 12;
                    break;
                  case 6:
                    A[0] = a1, A[1] = b1, A[2] = c0;
                    V1[1] = (b1 - b0) >> 12;
                    V2[0] = (a3 - a1) >> 12, V2[1] = (b3 - b1) >> 12, V2[2] = (c2 - c0) >> 12;
                    break;
                  default:
                    A[0] = a0;
                    V2[1] = 0, V2[2] = 0;
                    break;
                }
              }
              // OP sf=0: N = D x IR with D = V1 (RT diagonal) and IR = V2, 16-bit inputs.
              const s32 d1 = lo16(V1[0]), d2 = lo16(V1[1]), d3 = lo16(V1[2]);
              const s32 i1 = lo16(V2[0]), i2 = lo16(V2[1]), i3 = lo16(V2[2]);
              const s32 n1 = static_cast<s32>(static_cast<s64>(i3) * d2 - static_cast<s64>(i2) * d3);
              const s32 n2 = static_cast<s32>(static_cast<s64>(i1) * d3 - static_cast<s64>(i3) * d1);
              const s32 n3 = static_cast<s32>(static_cast<s64>(i2) * d1 - static_cast<s64>(i1) * d2);
              const s32 dx = (A[0] - ccx) >> 12, dy = (A[1] - ccy) >> 12, dz = (A[2] - ccz) >> 12;
              if (dot3_wrap(n1, dx, n2, dy, n3, dz) < 0) break;
            }
            if (face != 8) continue;
          }
          return Vec4{C[0], C[1], C[2], 0};
        }
      }
    }
  }
  // The arena edge (z widened by the depth and a quarter unit).
  const s32 ex0 = st.bounds[0], ex1 = st.bounds[2];
  const s32 ez0 = st.bounds[1] + u->size[2] * -0x2000 - 0x4000;
  const s32 ez1 = st.bounds[3] + u->size[2] * 0x2000 + 0x4000;
  Vec4 out{u->pos[0], u->pos[1], u->pos[2], 0};  // the console's x / z start as stack garbage
  for (int i = 0; i < 4;) {
    const s32* p = P[i];
    if (!(p[0] < ex0 || ex1 < p[0] || p[2] < ez0 || ez1 < p[2])) {
      ++i;
      continue;
    }
    out.y = u->pos[1];
    if (vz != 0) out.z = vz < 0 ? ez0 - 0xA000 : ez1 + 0xA000;
    s32 wx = ex1;
    if (vx != 0) {
      out.x = vx < 0 ? ex0 - 0xA000 : ex1 + 0xA000;
      if (vx < 0) wx = ex0;
    }
    const s32 dxw = iabs32(u->pos[0] - wx);
    const s32 dzw = iabs32(u->pos[2] - (vz < 0 ? ez0 : ez1));
    if (dxw <= dzw || vz == 0) {
      if (vx == 0) return out;
      ++i;
      if (dxw <= dist) {
        out.z = u->pos[2];
        u->hit_wall = 1;
        return out;
      }
    } else {
      ++i;
      if (dzw <= dist) {
        out.x = u->pos[0];
        u->hit_wall = 1;
        return out;
      }
    }
  }
  (void)g;
  return Vec4{kPathClear, 0, 0, out.w};
}

bool unit_find_approach_point(Unit* u, Vec4* out) {
  if (u->move_mode != 0) {
    const s32 d[3] = {u->walk_dest[0] - u->pos[0], u->walk_dest[1] - u->pos[1], u->walk_dest[2] - u->pos[2]};
    const bool blocked = unit_check_move(u, d, 7) != nullptr;
    if (!blocked) *out = Vec4{u->walk_dest[0], u->walk_dest[1], u->walk_dest[2], u->walk_dest[3]};
    return !blocked;
  }
  Unit* t = u->target;
  if (t == nullptr || unit_is_down(t)) return false;
  out->x = t->pos[0];
  out->y = u->pos[1];
  if (u->flying == 0) out->y = bobj_ground_height(t);
  if (u->straight != 0) {
    out->z = t->pos[2];
    const s32 d[3] = {out->x - u->pos[0], out->y - u->pos[1], out->z - u->pos[2]};
    return unit_check_move(u, d, 7) == nullptr;
  }
  const s32 r = u->reach + t->size[0] * 2 + u->size[0] * 2;
  const s32 R = r * 0x1000;
  const u8 side = u->approach_side;
  if (side == 0) {
    out->x = out->x <= u->pos[0] ? out->x + R : out->x - R;
  } else if (side == 1) {
    if (out->x < u->pos[0]) {
      out->x -= R;
      u->approach_side = 2;
    } else {
      out->x += R;
      u->approach_side = 3;
    }
  } else {
    out->x = side == 2 ? out->x - R : out->x + R;
  }
  for (int attempt = 0;; ++attempt) {
    if (u->approach_side == 0) {
      if (attempt > 1) return false;
    } else if (attempt > 0) {
      return false;
    }
    s32 sign = 0x1000;
    s32 k = 1;
    const s32 tz = t->pos[2];
    out->z = tz;
    for (int j = 0; j < 12; ++j) {
      const s32 d[3] = {out->x - u->pos[0], out->y - u->pos[1], out->z - u->pos[2]};
      if (unit_check_move(u, d, 7) == nullptr) return true;
      const s32 off = k * u->size[2] * 2;
      const s32 dz = off * sign;
      if ((t->size[2] + u->size[2]) * 2 <= off) break;
      sign = -sign;
      out->z = tz + dz;
      if (sign > 0) ++k;
    }
    const s32 tx = t->pos[0];
    out->x = tx + (tx <= u->pos[0] ? -R : R);
  }
}

void unit_begin_approach(Unit* u) {
  Vec4 p{u->pos[0], u->pos[1], u->pos[2], 0};
  unit_find_approach_point(u, &p);
  u->straight = 0;
  u->lookahead = 0x20;
  u->heading = static_cast<s16>(ratan2(p.z - u->pos[2], p.x - u->pos[0]));
  u->detour = 0;
  u->detour_timer = 0;
  u->dist_left = 0;
}

int unit_move_toward_goal(Unit* u, int apply) {
  const s32 ticks = bg().frame_ticks;
  Vec4 goal;
  if (unit_find_approach_point(u, &goal)) {
    const s32 reach = u->run_speed * ticks;
    if (iabs32(goal.x - u->pos[0]) <= reach && iabs32(goal.z - u->pos[2]) <= reach) {
      u->pos[0] = goal.x;
      u->pos[2] = goal.z;
      return 1;
    }
    s32 dist = vec_distance_fx(u->pos, goal.data());
    const s32 ang = ratan2(goal.z - u->pos[2], goal.x - u->pos[0]);
    bool walk = false;
    if (u->detour != 0) {
      s32 diff = u->heading - ang;
      if (0x7FF < diff) diff -= 0x1000;
      s16 sd = static_cast<s16>(diff);
      if (diff < -0x7FF) sd = static_cast<s16>(sd + 0x1000);
      u->detour = static_cast<s16>(u->detour + sd);
      u->heading = static_cast<s16>(ang);
      for (;;) {
        const s32 dv = u->detour;
        s32 next = dv - 0x71;
        bool reset = false;
        if (dv < 1) {
          next = dv + 0x71;
          if (-1 < next) reset = true;
        } else if (next < 1) {
          reset = true;
        }
        if (reset) {
          u->detour = 0;
          u->lookahead = 0x20;
          break;
        }
        const s32 a = u->heading + next;
        const Vec4 pr =
            unit_probe_path(u, bcos(static_cast<u32>(a)) * u->run_speed >> 12, bsin(static_cast<u32>(a)) * u->run_speed >> 12, dist);
        if (pr.x != kPathClear) break;
        u->detour = static_cast<s16>(next);
      }
      walk = true;
    }
    if (!walk) u->heading = static_cast<s16>(ang);
    u->detour_timer += ticks;
    if (600 < u->detour_timer) {
      u->detour_timer = 0;
      u->detour = 0;
    }
    const s16 saved = u->detour;
    s32 turn = 0;
    bool flipped = false, walled = false;
    u8 wall_seen = 0;
    for (;;) {
      const s32 a = u->heading + u->detour;
      u->vel[0] = bcos(static_cast<u32>(a)) * u->run_speed >> 12;
      u->vel[1] = 0;
      u->vel[2] = bsin(static_cast<u32>(a)) * u->run_speed >> 12;
      const Vec4 pr = unit_probe_path(u, u->vel[0], u->vel[2], dist);
      if (pr.x == kPathClear) break;
      bool choose = false;
      if (u->hit_wall == 0) {
        if (turn == 0) choose = true;
      } else {
        walled = true;
        if (turn != 0) {
          if (!flipped && wall_seen == 0) {
            flipped = true;
            turn = -turn;
          }
        } else {
          choose = true;
        }
      }
      if (choose) {
        wall_seen = u->hit_wall;
        dist = vec_distance_fx(u->pos, pr.data());
        const Vec4 dir = vec_velocity_toward(u->pos, pr.data(), 1);
        const u32 pa = static_cast<u32>(ratan2(dir.z, dir.x));
        const s32 diff = static_cast<s32>((static_cast<u32>(u->heading + u->detour) & 0xFFF) - (pa & 0xFFF));
        turn = 0x71;
        if (static_cast<u32>(diff + 0x800) < 0x800 || 0x800 < diff) turn = -0x71;
      }
      u->detour = static_cast<s16>(u->detour + turn);
      if (0x8E2 < iabs32(u->detour)) {
        if (u->lookahead < 0xD) goto fail;
        s32 la = u->lookahead - 8;
        if (la < 0xC) la = 0xC;
        u->lookahead = static_cast<s16>(la);
        u->detour = walled ? 0 : saved;
        walled = false;
        wall_seen = 0;
        flipped = false;
        turn = 0;
      }
    }
    u->facing[0] = u->vel[0];
    u->facing[1] = u->vel[1];
    u->facing[2] = u->vel[2];
    if (apply != 0) bobj_move(u, u->vel);
    u->dist_left = dist;
    return 0;
  }
fail:
  if (u->target != nullptr && !unit_is_down(u->target)) {
    const s32 r = (u->reach + u->target->size[0] * 2 + u->size[0] * 2) * 0x1000;
    if (r <= vec_distance_fx(u->pos, u->target->pos)) return 2;
    if (u->move_mode == 0) return 3;
  }
  return 2;
}

int unit_can_reach_target(Unit* u, Unit* target, int allow_straight) {
  if (target == nullptr || u->reach == 0) return 1;
  u->straight = 0;
  Vec4 p;
  bool far = false;
  if (!unit_find_approach_point(u, &p)) far = (static_cast<s32>(u->reach) << 12) <= vec_distance_fx(u->pos, target->pos);
  if (!far) return 1;
  if (target->flying == 0) return 0;
  const s32 h = u->size[1];
  if (target->pos[1] < ((h - (h >> 31)) >> 1) * -0x1000 && allow_straight != 0) {
    u->straight = 1;
    if (unit_find_approach_point(u, &p)) {
      u->straight = 0;
      return 1;
    }
    u->straight = 0;
  }
  return 0;
}

s32 unit_jump_time(const Unit* u, s32 v, const s32 dest[3]) {
  const s32 v1 = v + 1;
  s32 t = (v1 + square_root0(v1 * v1 + ((dest[1] << 1) >> 12))) * u->run_speed;
  if (t < 1) {
    const s32 v5 = v + 5;
    t = (v5 + square_root0(v5 * v5 + ((dest[1] << 1) >> 12))) * u->run_speed;
  }
  return t;
}

s32 unit_jump_rise_time(const Unit* u, s32 v, const s32 dest[3]) {
  const s32 v1 = v + 1;
  s32 t = ((v1 - square_root0(v1 * v1 + ((dest[1] << 1) >> 12))) * u->run_speed) / 5;
  if (t < 1) t = 1;
  return t;
}

}  // namespace ss::game::battle
