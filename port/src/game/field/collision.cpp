#include "game/field/collision.h"

#include <cstring>

#include "game/common/math.h"
#include "game/field/actor_base.h"
#include "game/field/actors.h"
#include "game/field/field.h"
#include "game/field/map.h"
#include "game/field/script_vm.h"
#include "runtime/libgte.h"

namespace ss::game::field {
namespace {

s16 rd16s(const u8* p) { return static_cast<s16>(read_u16(p)); }
s32 rd32s(const u8* p) { return static_cast<s32>(read_u32(p)); }
// 32-bit products wrap as on the R3000 (mult / mflo).
s32 mul(s32 a, s32 b) { return static_cast<s32>(static_cast<u32>(a) * static_cast<u32>(b)); }
// x >> 8 rounding toward zero, as the compiler emits for signed division by 256.
s32 div256(s32 v) { return (v < 0 ? v + 0xFF : v) >> 8; }
s32 div16(s32 v) { return (v < 0 ? v + 0xF : v) >> 4; }
s32 div4096(s32 v) { return (v < 0 ? v + 0xFFF : v) >> 12; }

const u8* scene() { return map_data().scene.data(); }
s32 ticks() { return globals().frame_ticks; }

// A world point is within half the radius of a wall polygon's vertices or edges.
bool touches_polygon(const u8* wall, s32 px, s32 pz, s32 radius) {
  const u32 n = wall[0xE];
  const s32 half = radius / 2;
  u32 k = 0;
  for (; k < n; ++k) {
    const s32 dx = px - rd16s(wall + 0x10 + k * 8), dz = pz - rd16s(wall + 0x14 + k * 8);
    if (SquareRoot0(mul(dx, dx) + mul(dz, dz)) < half) break;
  }
  if (k != n) return true;
  for (k = 0; k < n; ++k) {
    const u32 j = (k + 1) % n;
    const s32 x0 = rd16s(wall + 0x10 + k * 8), z0 = rd16s(wall + 0x14 + k * 8);
    const s32 x1 = rd16s(wall + 0x10 + j * 8), z1 = rd16s(wall + 0x14 + j * 8);
    if (x0 == x1 && z0 == z1) continue;
    const s32 dx = x1 - x0, dz = z1 - z0;
    const s32 t = (mul(mul(px - x0, dx) + mul(pz - z0, dz), 0x100)) / (mul(dx, dx) + mul(dz, dz));
    if (static_cast<u32>(t) < 0x101) {
      const s32 cx = (div256(mul(dx, t)) + x0) - px;
      const s32 cz = (div256(mul(dz, t)) + z0) - pz;
      if (SquareRoot0(mul(cx, cx) + mul(cz, cz)) < half) break;
    }
  }
  return k != n;
}

// The wall's bounding box grown by the radius contains the point (and y its range).
bool near_wall(const u8* wall, s32 px, s32 y, s32 pz, s32 radius) {
  if (wall[0xC] == 0) return false;
  if (y < rd16s(wall) || rd16s(wall + 2) < y) return false;
  if (px < rd16s(wall + 4) - radius || rd16s(wall + 8) + radius < px) return false;
  return rd16s(wall + 6) - radius <= pz && pz <= rd16s(wall + 0xA) + radius;
}

}  // namespace

int scene_cell_count(int cell, int list) { return read_u16(map_data().cell_table + cell * 0x60 + list * 2); }

const u8* scene_cell_list(int cell, int list) {
  const MapData& m = map_data();
  return m.cells + read_u32(m.cell_table + cell * 0x60 + 0x20 + list * 4);
}

int zone_cell_at_xy(const Actor*, int sx, int sy) {
  const u8* grid = map_data().cells;
  sx -= rd32s(scene() + 0x28);
  if ((grid[0] - 1) * 0x80 <= sx) return -1;
  sy -= rd32s(scene() + 0x2C);
  if (sy >= (grid[1] - 1) * 0x80) return -1;
  if (sx < -0x80) return -1;
  if (sy <= -0x81) return -1;
  if (sy < 0) sy += 0x7F;
  if (sx < 0) sx += 0x7F;
  return ((sy >> 7) + 1) * grid[0] + (sx >> 7) + 1;
}

int zone_cell_at(const Actor* a, const s16* xy) { return zone_cell_at_xy(a, xy[0], xy[1]); }

bool zone_test_point(const Actor*, const u8* item, const s16* xy, s32 y) {
  if (item[0x10] == 0) return false;
  if (y < rd16s(item + 4) || rd16s(item + 6) < y) return false;
  const s32 x = xy[0];
  if (x < rd16s(item + 8) || rd16s(item + 0xC) < x) return false;
  const s32 py = xy[1];
  if (py < rd16s(item + 0xA) || rd16s(item + 0xE) < py) return false;
  auto edge = [&](int a, int b) {
    const s32 ax = rd16s(item + 0x14 + a * 4), ay = rd16s(item + 0x16 + a * 4);
    const s32 bx = rd16s(item + 0x14 + b * 4), by = rd16s(item + 0x16 + b * 4);
    return mul(bx - ax, py - ay) - mul(by - ay, x - ax);
  };
  switch (item[0]) {
    case 0:  // triangle
      return edge(0, 1) <= 0 && edge(1, 2) <= 0 && edge(2, 0) <= 0;
    case 1:  // quad
      return edge(0, 1) <= 0 && edge(1, 2) <= 0 && edge(2, 3) <= 0 && edge(3, 0) <= 0;
    case 2:  // the bounding box
      return true;
    case 3: {  // ellipse: centre (+0x14, +0x16), half width +0x18, half height +0x1a
      s32 dy = rd16s(item + 0x16) - py;
      if (dy < 0) dy = -dy;
      s32 dx = x - rd16s(item + 0x14);
      if (dx < 0) dx = -dx;
      const s32 f = (dy << 8) / static_cast<s32>(read_u16(item + 0x1A));
      const s32 w = mul(SquareRoot0(0x10000 - mul(f, f)), rd16s(item + 0x18));
      return dx <= div256(w);
    }
    default:
      return false;
  }
}

bool walk_point_in_tri(const Actor*, const u8* item, const s32* pos) {
  if (item[0xC] == 0) return false;
  const s32 y = pos[1] >> 12;
  if (y < rd16s(item) || rd16s(item + 2) < y) return false;
  const s32 x = pos[0] >> 12, z = pos[2] >> 12;
  if (x < rd16s(item + 4) || rd16s(item + 8) < x || z < rd16s(item + 6) || rd16s(item + 0xA) < z) return false;
  const s32 x0 = rd16s(item + 0x10), z0 = rd16s(item + 0x14);
  const s32 x1 = rd16s(item + 0x18), z1 = rd16s(item + 0x1C);
  const s32 x2 = rd16s(item + 0x20), z2 = rd16s(item + 0x24);
  if (mul(x1 - x0, z - z0) - mul(z1 - z0, x - x0) >= 1) return false;
  if (mul(x2 - x1, z - z1) - mul(z2 - z1, x - x1) >= 1) return false;
  return mul(x0 - x2, z - z2) - mul(z0 - z2, x - x2) < 1;
}

void walk_snap_to_floor(Actor* a) {
  a->zone = nullptr;
  if ((a->flags & kActorZones) == 0) return;
  const int count = scene_cell_count(0, 14);
  const u8* zones = scene_cell_list(0, 14);
  int i = 0;
  for (; i < count; ++i)
    if (walk_point_in_tri(a, zones + i * 0x58, &a->x)) break;
  if (i == count) return;
  const u8* z = zones + i * 0x58;
  const s32 b = rd32s(z + 0x34);
  if (b == 0) {
    a->y = rd16s(z + 0x12);
  } else {
    a->y = (mul(-rd32s(z + 0x30), a->x + mul(rd16s(z + 0x10), -0x1000)) - mul(rd32s(z + 0x38), a->z + mul(rd16s(z + 0x14), -0x1000)) +
            mul(mul(b, rd16s(z + 0x12)), 0x1000)) /
           b;
    s32 hi = rd16s(z + 0x12), lo = hi;
    for (int k = 1; k < 3; ++k) {
      const s32 v = rd16s(z + 0x12 + k * 8);
      if (v < lo) lo = v;
      if (hi < v) hi = v;
    }
    if (hi << 12 < a->y) {
      a->y = hi << 12;
    } else if (a->y < lo << 12) {
      a->y = lo << 12;
    }
  }
  a->zone = z;
  if (z[0xF] != 0 && a->layer != 0 && (a->flags & kActorFixedLayer) == 0) a->layer = z[0x54];
}

void walk_update_layer(Actor* a) {
  if (a->layer == 0 || (a->flags & kActorFixedLayer) != 0) return;
  const int cell = zone_cell_at(a, &a->screen_x);
  if (cell == -1) return;
  const int count = scene_cell_count(cell, 2);
  const u8* item = scene_cell_list(cell, 2);
  for (int i = 0; i < count; ++i, item += 0x28) {
    if (zone_test_point(a, item, &a->screen_x, a->y >> 12)) {
      a->layer = item[0x24];
      return;
    }
  }
}

bool walk_test_obstacles(const Actor* a, const s32* pos, u32* hit) {
  const s32 px = pos[0] >> 12, pz = pos[2] >> 12, y = pos[1] >> 12;
  const u32 count = static_cast<u32>(scene_cell_count(0, 15));
  const u8* wall = scene_cell_list(0, 15);
  u32 i = 0;
  for (; i < count; ++i, wall += 0x30) {
    if (!near_wall(wall, px, y, pz, a->collide_radius)) continue;
    if (touches_polygon(wall, px, pz, a->collide_radius)) break;
  }
  *hit = i;
  return i != count;
}

s32 fieldactor_wall_slide(const Actor* a, s32 step, s32 range) {
  s32 result[2];
  const s32 y = a->y >> 12;
  const u32 count = static_cast<u32>(scene_cell_count(0, 15));
  const u8* walls = scene_cell_list(0, 15);
  for (int pass = 0; pass < 2; ++pass) {
    s32 lo = 0;
    s32 hi = pass == 0 ? range : -range;
    s32 cand = hi;
    for (int it = 0; it < 7; ++it) {
      const s32 angle = a->angle + cand;
      const s32 px = (a->x + div16(mul(cos4096(static_cast<u32>(angle)), div256(step)))) >> 12;
      const s32 pz = (a->z + div16(mul(sin4096(static_cast<u32>(angle)), div256(step)))) >> 12;
      u32 w = 0;
      for (; w < count; ++w) {
        const u8* wall = walls + w * 0x30;
        if (!near_wall(wall, px, y, pz, a->collide_radius)) continue;
        if (touches_polygon(wall, px, pz, a->collide_radius)) break;
      }
      if (w == count) {
        hi = cand;
      } else {
        if ((cand < 0 ? -cand : cand) == range) break;
        lo = cand;
      }
      cand = (lo + hi) / 2;
    }
    result[pass] = hi;
  }
  if (result[0] == -result[1]) return result[0] != range ? result[0] : 0;
  return result[0] < -result[1] ? result[0] : result[1];
}

void fieldactor_walk_step(Actor* a, int dir, int move) {
  const bool was_blocked = a->blocked != 0;
  a->blocked = 0;
  if (move == 0) return;
  const u8* zone = nullptr;
  a->prev_angle = a->angle;
  const u8* zones = scene_cell_list(0, 14);
  const int zone_count = scene_cell_count(0, 14);
  if (a->analog != 0) {
    a->angle = static_cast<s16>(dir);
  } else {
    // Pad direction -> world angle: the scene's table, or the zone's own.
    u16 angle = read_u16(scene() + 0xCC + dir * 2);
    int i = 0;
    for (; i < zone_count; ++i) {
      const u8* z = zones + i * 0x58;
      if (walk_point_in_tri(a, z, &a->x)) {
        if (z[0xE] != 0) angle = read_u16(z + 0x44 + dir * 2);
        break;
      }
    }
    a->angle = static_cast<s16>(angle);
    if (i != zone_count && (a->flags & kActorZones)) zone = zones + i * 0x58;
  }
  const s16 start_angle = a->angle;
  a->walked = 0;
  s32 slid = 0;
  const u8* walls = scene_cell_list(0, 15);
  const int wall_count = scene_cell_count(0, 15);

  // Probe the step (four walking steps when the last one hit a wall).
  s32 step = was_blocked ? a->walk_speed << 2 : a->speed * ticks();
  s32 probe[4] = {0, 0, 0, 0};
  probe[0] = a->x + div16(mul(cos4096(static_cast<u32>(a->angle)), div256(step)));
  probe[2] = a->z + div16(mul(sin4096(static_cast<u32>(a->angle)), div256(step)));
  probe[1] = a->y;
  if (a->flags & kActorWalls) {
    u32 hit;
    if (walk_test_obstacles(a, probe, &hit)) {
      const s32 range = walls[hit * 0x30 + 0xD] == '2' ? 0x180 : 0x380;
      slid = fieldactor_wall_slide(a, step, range);
      if (slid == 0) return;
      a->angle = static_cast<s16>(a->angle + slid);
    }
  }
  step = a->speed * ticks();
  probe[0] = a->x + div16(mul(cos4096(static_cast<u32>(a->angle)), div256(step)));
  probe[2] = a->z + div16(mul(sin4096(static_cast<u32>(a->angle)), div256(step)));
  if (a->flags & kActorWalls) {
    // Walls are also no-go triangles.
    for (int i = 0; i < wall_count; ++i) {
      if (walk_point_in_tri(a, walls + i * 0x30, probe)) {
        a->angle = start_angle;
        return;
      }
    }
  }
  for (int i = 0; i < zone_count; ++i) {
    const u8* z = zones + i * 0x58;
    if (walk_point_in_tri(a, z, probe)) {
      if (rd32s(z + 0x34) != 0x80) zone = z;
      break;
    }
  }

  // Sloped zones: walk along the plane.
  const s32 ux = cos4096(static_cast<u32>(a->angle)) << 8;
  const s32 uz = sin4096(static_cast<u32>(a->angle)) << 8;
  s32 uy = 0;
  if (zone != nullptr && rd32s(zone + 0x34) != 0) {
    const s32 b = rd32s(zone + 0x34);
    const s32 px = div4096(ux) + (a->x >> 12);
    const s32 pz = div4096(uz) + (a->z >> 12);
    const s32 plane = -(mul(rd32s(zone + 0x30), px - rd16s(zone + 0x10)) + mul(b, -rd16s(zone + 0x12)) +
                        mul(rd32s(zone + 0x38), pz - rd16s(zone + 0x14))) /
                      b;
    uy = (plane - (a->y >> 12)) << 12;
  }
  const s32 saved[4] = {a->x, a->y, a->z, a->w};
  if (uy == 0) {
    a->x += div16(mul(cos4096(static_cast<u32>(a->angle)), div256(step)));
    a->z += div16(mul(sin4096(static_cast<u32>(a->angle)), div256(step)));
  } else {
    psyq::VECTOR v{div256(ux), div256(uy), div256(uz), 0};
    psyq::Square0(&v, &v);
    const s32 len = SquareRoot0(v.vx + v.vy + v.vz);
    if (len != 0) {
      a->x += mul(ux, div256(step)) / len;
      a->z += mul(uz, div256(step)) / len;
    } else {
      const s32 len2 = SquareRoot0(mul(ux, ux) + mul(uy, uy) + mul(uz, uz));
      a->x += mul(ux, step) / len2;
      a->z += mul(uz, step) / len2;
    }
  }
  const u16 layer = a->layer;
  walk_snap_to_floor(a);
  if ((a->flags & kActorBlockedByActors) && actor_collides(a, &a->x, 1)) {
    a->x = saved[0];
    a->y = saved[1];
    a->z = saved[2];
    a->w = saved[3];
    a->layer = layer;
    a->angle = start_angle;
    return;
  }
  a->walked = 1;
  if (slid != 0) {
    a->blocked = 1;
    if (was_blocked) {
      // Sliding along a wall for a second frame: keep the facing steady unless the
      // camera-relative direction changed.
      const u32 d = (((a->angle - actor_system()->camera_yaw) + 0x8AA) & 0xFFF) / 0x156;
      const u32 old = a->zone_dir;
      s32 diff = static_cast<s32>(old) - static_cast<s32>(d);
      if (diff < 0) diff = -diff;
      if (old == 0x80 || static_cast<u32>(diff - 2) < 8) {
        a->zone_dir = static_cast<u8>(d);
      } else if (a->zone_dir_prev != d) {
        a->angle = a->prev_angle;
      }
      a->zone_dir_prev = static_cast<u8>(d);
    } else if (a->walked_last != 0) {
      a->angle = a->prev_angle;
    }
  } else {
    a->zone_dir = 0x80;
    if (was_blocked) a->angle = a->prev_angle;
  }
  s32 walked;
  std::memcpy(&walked, globals().field_flags + 0x18, 4);
  walked += a->speed * ticks();
  std::memcpy(globals().field_flags + 0x18, &walked, 4);
}

void walk_set_group_enabled(int tag, int enabled) {
  u8* ff = globals().field_flags;
  const s8 count = static_cast<s8>(ff[0x91]);
  if (count < 8) {
    int i = 0;
    for (; i < count; ++i) {
      if ((ff[0x89 + i] & 0x7F) == static_cast<u32>(tag)) {
        ff[0x89 + i] = static_cast<u8>(tag);
        if (enabled == 0) ff[0x89 + i] = static_cast<u8>(tag | 0x80);
        break;
      }
    }
    if (i == count) {
      ff[0x91] = static_cast<u8>(count + 1);
      ff[0x89 + i] = static_cast<u8>(tag);
      if (enabled == 0) ff[0x89 + i] = static_cast<u8>(tag | 0x80);
    }
  }
  for (int list = 14; list < 16; ++list) {
    const int size = list == 14 ? 0x58 : 0x30;
    const int n = scene_cell_count(0, list);
    u8* item = const_cast<u8*>(scene_cell_list(0, list));
    for (int k = 0; k < n; ++k, item += size)
      if (static_cast<s8>(item[0xD]) == tag) item[0xC] = static_cast<u8>(enabled);
  }
}

}  // namespace ss::game::field
