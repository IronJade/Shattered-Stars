#include "game/field/actor_field.h"

#include <cstring>

#include "game/common/math.h"
#include "game/field/actors.h"
#include "game/field/bg.h"
#include "game/field/collision.h"
#include "game/field/field.h"
#include "game/field/field_input.h"
#include "game/field/field_sub_overlays.h"
#include "game/field/map.h"
#include "game/field/message.h"
#include "game/field/screen.h"
#include "game/field/script_vm.h"
#include "game/field/sprite_pack.h"
#include "game/field/sprite_render.h"

namespace ss::game::field {
namespace {

FieldActorGlobals g_globals;

s32 ticks() { return globals().frame_ticks; }
const u8* scene() { return map_data().scene.data(); }
s16 rd16s(const u8* p) { return static_cast<s16>(read_u16(p)); }
s32 rd32s(const u8* p) { return static_cast<s32>(read_u32(p)); }
s32 mul(s32 a, s32 b) { return static_cast<s32>(static_cast<u32>(a) * static_cast<u32>(b)); }

// TransRot_32 (field 0x80070cf4) through the view matrix actor_sys_setup_view loaded.
void transrot(const s32* in, s32 out[3]) {
  const VmVector v{in[0], in[1], in[2], 0};
  VmVector r{};
  s32 flag;
  TransRot_32(&v, &r, &flag);
  out[0] = r.vx;
  out[1] = r.vy;
  out[2] = r.vz;
}

// The perspective divide the overlay inlines everywhere: screen = view * H / z + centre.
// Returns false when z >> 4 is 0 (the callers give up).
bool to_screen(const s32 v[3], s32 H, s32* sx, s32* sy) {
  const s32 d = v[2] >> 4;
  if (d == 0) return false;
  *sx = (mul(v[0] >> 4, H) + (v[2] >> 5)) / d + (read_u16(scene() + 0x34) >> 1);
  *sy = (mul(v[1] >> 4, H) + (v[2] >> 5)) / d + (read_u16(scene() + 0x36) >> 1);
  return true;
}

s32 depth_of(s32 vz) {
  s32 d = vz >> 12;
  if (d < 0) d += 3;
  return d >> 2;
}

void add_prim(Actor* a, psyq::P_TAG* p, u16 ot) {
  a->prim_ot[a->prim_count] = ot;
  a->prims[a->prim_count++] = p;
}

psyq::POLY_FT4* new_ft4() { return static_cast<psyq::POLY_FT4*>(field_screen()->alloc(sizeof(psyq::POLY_FT4))); }

// The UI atlas entry of the shadow (#3110 icon table + 0x160: u0, v0 at +2, u1, v1 at +4).
const u8* shadow_icon() {
  const u8* icons = field_sys_icons();
  return icons != nullptr ? icons + 0x160 : nullptr;
}

void set_light(psyq::POLY_FT4* p, const Actor* a) {
  p->r0 = a->light[0];
  p->g0 = a->light[1];
  p->b0 = a->light[2];
}

// The 9-vertex slope shadow and the 4-corner shadow (0x8007f004-0x8007fa0c).
void draw_shadow(Actor* a, const s32 pos[4], const s16 cam[2], s32 H) {
  // Shadow drop: 9 at a flat view down to 0 head-on, scaled by the projection.
  const s32 dx = rd32s(scene() + 8) - (a->x >> 12);
  const s32 dz = rd32s(scene() + 0x10) - (a->z >> 12);
  const s32 horiz = SquareRoot0(mul(dx, dx) + mul(dz, dz));
  const s32 elev = static_cast<s16>(ratan2(rd32s(scene() + 0xC) - (a->y >> 12), horiz));
  s32 drop = 9 - elev / 0x47;
  if (drop >= 10) drop = 9;
  if (drop < 0) drop = 0;
  if (a->depth == 0) return;
  drop = mul(mul(drop, read_u16(scene() + 0x68)), H) / (a->depth * 400);
  const s32 r = (a->auto_anim / 12 == 2 ? (a->collide_radius * 15) / 13 : a->collide_radius) << 11;
  const u8* icon = shadow_icon();
  const bool boxes = g_globals.shadow_boxes != 0 || a->shadow_w != 0x400 || a->shadow_h != 0x400;
  const u8* zone = a->zone;
  if (zone == nullptr || read_u16(zone + 0x40) != 0 || rd32s(zone + 0x34) == 0) {
    // Four corners at +-r around the position.
    psyq::POLY_FT4* p = new_ft4();
    if (p == nullptr) return;
    psyq::setlen(p, 9);
    p->code = 0x2E;
    set_light(p, a);
    s16* xy[4][2] = {{&p->x0, &p->y0}, {&p->x1, &p->y1}, {&p->x2, &p->y2}, {&p->x3, &p->y3}};
    for (int k = 0; k < 4; ++k) {
      const s32 corner[3] = {pos[0] + ((k & 1) ? r : -r), pos[1], pos[2] + ((k & 2) ? -r : r)};
      s32 v[3], sx, sy;
      transrot(corner, v);
      if (!to_screen(v, H, &sx, &sy)) return;
      *xy[k][0] = static_cast<s16>(sx - cam[0]);
      *xy[k][1] = static_cast<s16>(sy - (cam[1] - drop));
    }
    if (boxes) {
      // The original passes &p->x0: the corners are 8 bytes apart in the POLY_FT4, so
      // fieldactor_build_shadow reads every second (x, y) pair (points 0, 2, 4, 6).
      const s16 pts[14] = {p->x0, p->y0, 0, 0, p->x1, p->y1, 0, 0, p->x2, p->y2, 0, 0, p->x3, p->y3};
      fieldactor_build_shadow(a, p, pts, 4);
      return;
    }
    if (icon == nullptr) return;
    p->u0 = icon[2];
    p->v0 = icon[3];
    p->u1 = static_cast<u8>(icon[4] + 1);
    p->v1 = icon[3];
    p->u2 = icon[2];
    p->v2 = static_cast<u8>(icon[5] + 1);
    p->u3 = static_cast<u8>(icon[4] + 1);
    p->v3 = static_cast<u8>(icon[5] + 1);
    p->tpage = 0x56;
    p->clut = 0x7FCD;
    if (a->prim_count < 24) add_prim(a, &p->tag, static_cast<u16>(a->layer + 1));
    return;
  }

  // On a sloped zone: a 3x3 grid on the floor plane (corners at r * 1.41), four quads.
  s32 grid[9][3];
  const s32 diag = (mul(r * 0x23, 4) + r) >> 12;
  const s32 a_ = rd32s(zone + 0x30), b_ = rd32s(zone + 0x34), c_ = rd32s(zone + 0x38);
  int n = 0;
  s32 oz = -r;
  for (int row = -1; row < 2; ++row, oz += r) {
    s32 ox = -r;
    for (int col = -1; col < 2; ++col, ox += r) {
      s32 x = ox >> 12, z = oz >> 12;
      s32 y = (mul(-a_, x) - mul(c_, z)) / b_;
      psyq::VECTOR sq{x, y, z, 0};
      psyq::Square0(&sq, &sq);
      const s32 len = SquareRoot0(sq.vx + sq.vy + sq.vz);
      if (len != 0) {
        if (x == 0 || z == 0) {
          const s32 k = r >> 12;
          x = mul(x, k) / len;
          y = mul(y, k) / len;
          z = mul(z, k) / len;
        } else {
          const s32 d = len * 100;
          x = mul(x, diag) / d;
          y = mul(y, diag) / d;
          z = mul(z, diag) / d;
        }
      }
      grid[n][0] = a->x + (x << 12);
      grid[n][2] = a->z + (z << 12);
      grid[n][1] = a->y + (y << 12);
      ++n;
    }
  }
  s16 pts[9][2];
  for (int k = 0; k < 9; ++k) {
    s32 v[3], sx, sy;
    transrot(grid[k], v);
    if (!to_screen(v, H, &sx, &sy)) return;
    pts[k][0] = static_cast<s16>(sx - cam[0]);
    pts[k][1] = static_cast<s16>(sy - (cam[1] - drop));
  }
  if (boxes || icon == nullptr) {
    psyq::POLY_FT4* p = new_ft4();
    if (p == nullptr) return;
    fieldactor_build_shadow(a, p, &pts[0][0], 9);
    return;
  }
  const s32 du = (icon[4] - icon[2]) / 2, dv = (icon[5] - icon[3]) / 2;
  for (int q = 0; q < 4; ++q) {
    if (a->prim_count >= 24) continue;
    psyq::POLY_FT4* p = new_ft4();
    if (p == nullptr) return;
    const s32 col = q & 1, row = q >> 1;
    const s32 u = icon[2] + du * col, v = icon[3] + dv * row;
    psyq::setlen(p, 9);
    p->code = 0x2F;
    set_light(p, a);
    p->u0 = static_cast<u8>(u);
    p->v0 = static_cast<u8>(v);
    p->u1 = static_cast<u8>(u + du);
    p->v1 = static_cast<u8>(v);
    p->u2 = static_cast<u8>(u);
    p->v2 = static_cast<u8>(v + dv + row);
    p->u3 = static_cast<u8>(u + du);
    p->v3 = static_cast<u8>(v + dv + row);
    p->tpage = 0x56;
    p->clut = 0x7FCD;
    const int i = q + q / 2;
    p->x0 = pts[i][0], p->y0 = pts[i][1];
    p->x1 = pts[i + 1][0], p->y1 = pts[i + 1][1];
    p->x2 = pts[i + 3][0], p->y2 = pts[i + 3][1];
    p->x3 = pts[i + 4][0], p->y3 = pts[i + 4][1];
    add_prim(a, &p->tag, static_cast<u16>(a->layer + 1));
  }
}

// The UV fix-up of the main sprite (0x80080878-0x80080aa4): shrink the texture
// rectangle by a texel where the quad isn't drawn 1:1.
void fix_uvs(const psyq::POLY_FT4* p, s32& u0, s32& v0, s32& u1, s32& v1) {
  const s32 dx = p->x3 - p->x0;
  const s32 du = u1 - u0 < 0 ? u0 - u1 : u1 - u0;
  if (dx < 0) {
    if (-dx == du) --u0;
    --u1;
  } else {
    const s32 ex = p->x2 - p->x1;
    if (dx != du || (ex < 0 ? -ex : ex) != du) --u1;
  }
  const s32 dy = p->y3 - p->y0;
  const s32 dv = v1 - v0 < 0 ? v0 - v1 : v1 - v0;
  if (dy < 0 && p->y0 <= p->y1) {
    if (-dy == dv) --v0;
    --v1;
  } else {
    const s32 ey = p->y2 - p->y1;
    if ((dy < 0 ? -dy : dy) != dv || (ey < 0 ? -ey : ey) != dv) --v1;
  }
}

// --- shading regions (fieldactor_build_light_split 0x80083d70) ---

// A light polygon of cell lists 4 / 6 (0x4c bytes): the ZoneItem head (+0 type, 0 =
// triangle, else quad; +1..+3 colour; +4 / +6 world y range; +8..+0xe box; +0x10
// enabled), the corners at +0x14 closed by a copy of the first (5 x {s16 x, y}), each
// edge's line a x + b y + c = 0 (s16 a[4] at +0x28, s16 b[4] at +0x30, s32 c[4] at
// +0x38), and +0x48: only drawn with the colour of the list-6 region under the feet.
constexpr int kLightPolySize = 0x4C;
constexpr int kLightMaxVerts = 32;  // the original's stack holds 12 (a convex quad clips to 8)

// The R3000's quotient: this code divides without the compiler's zero checks, so a zero
// divisor gives -1 (1 for a negative dividend) instead of trapping.
s32 div_r3000(s32 a, s32 b) {
  if (b == 0) return a < 0 ? 1 : -1;
  return b == -1 ? static_cast<s32>(0u - static_cast<u32>(a)) : a / b;
}
s32 neg(s32 v) { return static_cast<s32>(0u - static_cast<u32>(v)); }

// Where edge e crosses the row y / the column x (the callers check the divisor).
s32 light_edge_x(const u8* poly, int e, s32 y) {
  return div_r3000(neg(mul(rd16s(poly + 0x30 + e * 2), y) + rd32s(poly + 0x38 + e * 4)), rd16s(poly + 0x28 + e * 2));
}
s32 light_edge_y(const u8* poly, int e, s32 x) {
  return div_r3000(neg(mul(rd16s(poly + 0x28 + e * 2), x) + rd32s(poly + 0x38 + e * 4)), rd16s(poly + 0x30 + e * 2));
}

// The sprite's screen box (BG pixels) the polygons are clipped to.
struct LightBox {
  s16 x0, y0, x1, y1;
};

// Clips a light polygon to the box (0x8008459c-0x80084aac): rows first, recording the
// polygon edge each vertex lies on, then columns, where a segment's crossing is taken
// on the edge of its end vertex (or kept level when both ends share a row). Returns the
// vertex count (the result is open: the last vertex joins the first).
int light_clip(const u8* poly, const LightBox& box, s32 out[kLightMaxVerts][2]) {
  s32 rows[kLightMaxVerts][2];
  int edge_of[kLightMaxVerts];
  int n = 0;
  const int edges = poly[0] == 0 ? 3 : 4;
  auto put_row = [&](int e, s32 x, s32 y) {
    if (n >= kLightMaxVerts - 1) return;
    edge_of[n] = e;
    rows[n][0] = x;
    rows[n][1] = y;
    ++n;
  };
  for (int e = 0; e < edges; ++e) {
    const s32 px = rd16s(poly + 0x14 + e * 4), py = rd16s(poly + 0x16 + e * 4);
    s32 qx = rd16s(poly + 0x18 + e * 4), qy = rd16s(poly + 0x1A + e * 4);
    // a == 0: a level edge, whose crossings keep the corner's x.
    const bool has_x = rd16s(poly + 0x28 + e * 2) != 0;
    if (qy < box.y0) {
      if (py < box.y0) continue;
      qy = box.y0;
      if (has_x) qx = light_edge_x(poly, e, box.y0);
    } else if (py < box.y0) {
      put_row(e, has_x ? light_edge_x(poly, e, box.y0) : px, box.y0);
    }
    if (box.y1 < qy) {
      if (box.y1 < py) continue;
      qy = box.y1;
      if (has_x) qx = light_edge_x(poly, e, box.y1);
    } else if (box.y1 < py) {
      put_row(e, has_x ? light_edge_x(poly, e, box.y1) : px, box.y1);
    }
    put_row(e, qx, qy);
  }
  if (n != 0) {
    // Closed with the first vertex (its edge recorded as 0).
    edge_of[n] = 0;
    rows[n][0] = rows[0][0];
    rows[n][1] = rows[0][1];
    ++n;
  }
  int m = 0;
  auto put = [&](s32 x, s32 y) {
    if (m >= kLightMaxVerts) return;
    out[m][0] = x;
    out[m][1] = y;
    ++m;
  };
  for (int i = 0; i + 1 < n; ++i) {
    const s32 px = rows[i][0], py = rows[i][1];
    s32 qx = rows[i + 1][0], qy = rows[i + 1][1];
    const int e = edge_of[i + 1];
    // A crossing moves along the edge unless the segment is level (it runs along a
    // clipped row) or the edge is vertical (b == 0): then it keeps a y.
    const bool along = rows[i + 1][1] != py && rd16s(poly + 0x30 + e * 2) != 0;
    if (qx < box.x0) {
      if (px < box.x0) continue;
      if (along) qy = light_edge_y(poly, e, box.x0);
      qx = box.x0;
    } else if (px < box.x0) {
      put(box.x0, along ? light_edge_y(poly, e, box.x0) : py);
    }
    if (box.x1 < qx) {
      if (box.x1 < px) continue;
      if (along) qy = light_edge_y(poly, e, box.x1);
      qx = box.x1;
    } else if (box.x1 < px) {
      put(box.x1, along ? light_edge_y(poly, e, box.x1) : py);
    }
    put(qx, qy);
  }
  return m;
}

// The texture coordinate of a piece's vertex (0x80084cb8-0x80085314): 'offset' is the
// distance from the box's left (or, mirrored, right) edge or top. Prescaled sprites
// (cache A) map pixels 1:1; unscaled ones scale by the main sprite's texel span over its
// size, and every form but the triangles' mirrored one divides the base in too
// ((base + span * offset) / size): kept.
u8 light_uv(u8 base, s32 span, s32 offset, s32 size, bool scaled, bool divide_base) {
  if (scaled) return static_cast<u8>(base + offset);
  if (divide_base) return static_cast<u8>(div_r3000(base + mul(span, offset), size));
  return static_cast<u8>(base + div_r3000(mul(span, offset), size));
}

}  // namespace

FieldActorGlobals& fieldactor_globals() { return g_globals; }

const ActorVtable kFieldActorVtable = {fieldactor_draw, fieldactor_player_control, fieldactor_camera_follow, fieldactor_move_step};

Actor* fieldactor_construct(Actor* self) {
  actor_construct_base(self);
  self->vtbl = &kFieldActorVtable;
  actor_set_defaults(self);
  return self;
}

Actor* fieldactor_construct_screen(Actor* self, s16 model, s16 x, s16 y, int layer) {
  actor_construct_base(self);
  self->vtbl = &kFieldActorVtable;
  actor_set_defaults(self);
  actor_init_screen(self, model, x, y, layer);
  return self;
}

Actor* fieldactor_construct_full(Actor* self, s16 model, const s32 pos[3], int angle_deg, int layer, s16 talk_entry,
                          s16 pickpocket_entry, u32 flags) {
  actor_construct_base(self);
  self->vtbl = &kFieldActorVtable;
  actor_set_defaults(self);
  actor_init(self, model, pos, angle_deg, layer, talk_entry, pickpocket_entry, flags);
  fieldactor_place(self);
  return self;
}

void fieldactor_destroy(Actor* self, int flags) {
  self->vtbl = &kFieldActorVtable;
  actor_destroy(self, flags);
}

void fieldactor_place(Actor* self) {
  const s32 pos[3] = {self->x, self->y, self->z};
  s32 v[3], sx, sy;
  transrot(pos, v);
  if (to_screen(v, read_u16(scene() + 0x6A), &sx, &sy)) {
    self->screen_x = static_cast<s16>(sx);
    self->screen_y = static_cast<s16>(sy);
  }
  walk_snap_to_floor(self);
}

void fieldactor_apply_light_zone(Actor* self) {
  if ((self->flags & (kActorScreenSpace | kActorShaded)) != kActorShaded || self->model == 0x7FFF) return;
  const int cell = zone_cell_at(self, &self->screen_x);
  if (cell == -1) return;
  const int count = scene_cell_count(cell, 8);
  const u8* item = scene_cell_list(cell, 8);
  for (int i = 0; i < count; ++i, item += 0x24) {
    if (!zone_test_point(self, item, &self->screen_x, self->y >> 12)) continue;
    for (int k = 0; k < 3; ++k) {
      s32 v = (self->light[k] + item[1 + k]) - scene()[0x6C + k];
      if (v < 0) {
        v = 0;
      } else if (v > 0xFF) {
        v = 0xFF;
      }
      self->light[k] = static_cast<u8>(v);
    }
    return;
  }
}

void fieldactor_select_auto_anim(Actor* self, int walked) {
  if (self->anim != -1) {
    self->auto_anim = static_cast<u16>(self->anim);
    return;
  }
  s32 yaw = actor_system()->camera_yaw;
  if (globals().field_flags[0x83] == 1) {
    // Perspective correction across a wide view: the angle to the actor's column.
    const s32 half = read_u16(scene() + 0x34) >> 1;
    const s32 fov = actor_system()->half_fov;
    if (half < self->screen_y) {
      yaw += ((self->screen_x - half) * fov) / half;
    } else {
      yaw += -(((half - self->screen_x) * fov) / half);
    }
  }
  const u32 dir = (((self->angle - yaw) + 0x8AAu) & 0xFFF) / 0x156;
  if (walked == 0) {
    self->anim_forward = 1;
    self->anim_timer = 0;
    self->anim_frame = 0;
    self->auto_anim = static_cast<u16>(dir - (dir / 12) * 12 + 12);
  } else if ((self->flags & kActorRunning) == 0) {
    if (self->auto_anim > 0x17) self->anim_frame = static_cast<s16>(self->anim_frame / 2);
    self->auto_anim = static_cast<u16>(dir);
  } else {
    self->auto_anim = static_cast<u16>(dir + 0x18);
  }
}

void fieldactor_build_light_split(Actor* self, s32 w, s32 h, psyq::POLY_FT4* prim, int flips, int scaled) {
  if ((self->flags & kActorShaded) == 0) return;
  s16 cam[2] = {0, 0};
  // 0x80086bbc. (kActorNoScroll is not applied here, unlike in fieldactor_draw.)
  if (BgRenderer* bg = bg_renderer(); bg != nullptr && !map_data().scene.empty()) bg_get_view_origin(bg, cam);
  const s16* q = self->quad;  // x0, y0 .. x3, y3: the main sprite's screen corners
  // The probe under the feet: the middle of the sprite's bottom edge, in BG pixels.
  s16 probe[2] = {static_cast<s16>(cam[0] + (q[4] + q[6]) / 2), static_cast<s16>(q[5] + cam[1])};
  const s32 ay = self->y >> 12;
  // sp+0x10: the colour of the list-6 region under the feet, left as it was when there
  // is none (FieldActorGlobals::shade_colour). Only list-4 polygons flagged +0x48 read it
  // then, and no disc 1 map has one.
  u8* colour = g_globals.shade_colour;
  s32 cells[4] = {0, 0, 0, 0};
  for (int pass = 0; pass < 2; ++pass) {
    int list;
    if (pass == 0) {
      // List 6 (flat shade under the feet): a region at the probe, then half a sprite
      // to the left, then half a sprite to the right.
      bool found = false;
      for (int k = 0; k < 3 && !found; ++k) {
        const int cell = zone_cell_at(self, probe);
        if (cell == -1) return;
        const int count = scene_cell_count(cell, 6);
        const u8* item = scene_cell_list(cell, 6);
        int i = 0;
        for (; i < count; ++i, item += kLightPolySize)
          if (zone_test_point(self, item, probe, self->y >> 12)) break;
        if (i != count) {
          colour[0] = item[1];
          colour[1] = item[2];
          colour[2] = item[3];
          found = true;
        } else if (k == 0) {
          probe[0] = static_cast<s16>(probe[0] - w / 2);
        } else if (k == 1) {
          probe[0] = static_cast<s16>(probe[0] + w);
        }
      }
      if (!found) continue;
      list = 6;
    } else {
      // List 4: any enabled polygon in a corner's cell, at the actor's height, whose box
      // comes near the sprite (centres closer than the sum of the sizes, not half).
      const s32 cx = (q[0] + q[2]) / 2 + cam[0], cy = (q[1] + q[5]) / 2 + cam[1];
      int k = 0;
      for (; k < 4; ++k) {
        const int cell = zone_cell_at_xy(self, q[k * 2] + cam[0], q[k * 2 + 1] + cam[1]);
        if (cell == -1) return;
        cells[k] = cell;
        int j = 0;
        while (j < k && cells[j] != cell) ++j;
        if (j != k) continue;
        const int count = scene_cell_count(cell, 4);
        const u8* item = scene_cell_list(cell, 4);
        int i = 0;
        for (; i < count; ++i, item += kLightPolySize) {
          if (item[0x10] == 0 || ay < rd16s(item + 4) || rd16s(item + 6) < ay) continue;
          const s32 x0 = rd16s(item + 8), y0 = rd16s(item + 0xA), x1 = rd16s(item + 0xC), y1 = rd16s(item + 0xE);
          s32 d = (x0 + x1) / 2 - cx;
          if (d < 0) d = -d;
          if (d >= (x1 - x0) + w) continue;
          d = (y0 + y1) / 2 - cy;
          if (d < 0) d = -d;
          if (d < (y1 - y0) + h) break;
        }
        if (i != count) break;
      }
      if (k == 4) return;
      list = 4;
    }

    // The cells under the sprite's corners.
    int ncells = 0;
    for (int k = 0; k < 4; ++k) {
      const int cell = zone_cell_at_xy(self, q[k * 2] + cam[0], q[k * 2 + 1] + cam[1]);
      if (cell == -1) continue;
      int j = 0;
      while (j < ncells && cells[j] != cell) ++j;
      if (j == ncells) cells[ncells++] = cell;
    }
    if (ncells == 0) continue;
    const s16 top = q[5] < q[1] ? q[5] : q[1];
    const s16 left = q[2] < q[0] ? q[2] : q[0];
    LightBox box;
    box.y0 = static_cast<s16>(cam[1] + top);
    box.x0 = static_cast<s16>(cam[0] + left);
    box.y1 = static_cast<s16>(box.y0 + h);
    box.x1 = static_cast<s16>(box.x0 + w);

    // Every polygon of the list near the box (enabled or not, at any height) is clipped
    // to it and drawn as tinted pieces of the sprite; one stored in two of the cells
    // (maps keep a copy per cell) is drawn twice.
    for (int ci = 0; ci < ncells; ++ci) {
      const int count = scene_cell_count(cells[ci], list);
      const u8* poly = scene_cell_list(cells[ci], list);
      for (int i = 0; i < count; ++i, poly += kLightPolySize) {
        {
          const s32 px0 = rd16s(poly + 8), pw = rd16s(poly + 0xC) - px0;
          s32 d = (left + cam[0] + (w >> 1)) - (px0 + pw / 2);
          if (d < 0) d = -d;
          if (d >= (pw + w) >> 1) continue;
          const s32 py0 = rd16s(poly + 0xA), ph = rd16s(poly + 0xE) - py0;
          d = (top + cam[1] + (h >> 1)) - (py0 + ph / 2);
          if (d < 0) d = -d;
          if (d >= (ph + h) >> 1) continue;
        }
        if (poly[0x48] != 0 && (colour[0] != poly[1] || colour[1] != poly[2] || colour[2] != poly[3])) continue;
        s32 v[kLightMaxVerts][2];
        const int nv = light_clip(poly, box, v);
        if (nv < 3) continue;

        // The piece colour: the sprite's plus the polygon's difference from the ambient.
        const u8 sprite_rgb[3] = {prim->r0, prim->g0, prim->b0};
        u8 rgb[3];
        for (int c = 0; c < 3; ++c) {
          s32 value = sprite_rgb[c] + (poly[1 + c] - scene()[0x6C + c]);
          if (value < 0) {
            value = 0;
          } else if (value > 0xFF) {
            value = 0xFF;
          }
          rgb[c] = static_cast<u8>(value);
        }
        for (int k = 0; k < nv; ++k) {
          if (v[k][1] < box.y0) v[k][1] = box.y0;
          if (box.y1 < v[k][1]) v[k][1] = box.y1;
          if (v[k][0] < box.x0) v[k][0] = box.x0;
          if (box.x1 < v[k][0]) v[k][0] = box.x1;
        }
        const s32 du = prim->u1 - prim->u0, dv = prim->v2 - prim->v0;
        // Texture coordinates only for upright sprites (flips 0) and mirrored ones (2):
        // vertically flipped pieces keep whatever the primitive memory held (zero here).
        const bool uvs = flips == 0 || flips == 2;
        auto uv = [&](const s32 vert[2], bool divide_base, u8* out) {
          const s32 off_x = flips == 2 ? box.x1 - vert[0] : vert[0] - box.x0;
          out[0] = light_uv(prim->u0, du, off_x, w, scaled != 0, divide_base);
          out[1] = light_uv(prim->v0, dv, vert[1] - box.y0, h, scaled != 0, divide_base);
        };

        // A fan from vertex 0: quads while four or more vertices remain, then a triangle.
        int base = 0;
        const int pieces = (nv - 1) / 2;
        for (int piece = 0; piece < pieces; ++piece) {
          // The original builds each piece at the primitive cursor and advances it only
          // when the actor has room for it (22 here, not 24).
          const bool room = self->prim_count < 0x16;
          if (nv - base < 4) {
            const s32* c[3] = {v[0], v[base + 1], v[base + 2]};
            auto* t = room ? static_cast<psyq::POLY_FT3*>(field_screen()->alloc(sizeof(psyq::POLY_FT3))) : nullptr;
            if (t == nullptr) continue;
            psyq::setlen(t, 7);
            t->code = static_cast<u8>(self->blend != 0 ? 0x26 : 0x24);
            t->r0 = rgb[0];
            t->g0 = rgb[1];
            t->b0 = rgb[2];
            if (uvs) {
              // Mirrored triangles alone scale only the offset.
              const bool divide_base = flips == 0;
              uv(c[0], divide_base, &t->u0);
              uv(c[1], divide_base, &t->u1);
              uv(c[2], divide_base, &t->u2);
            }
            t->x0 = static_cast<s16>(c[0][0] - cam[0]), t->y0 = static_cast<s16>(c[0][1] - cam[1]);
            t->x1 = static_cast<s16>(c[1][0] - cam[0]), t->y1 = static_cast<s16>(c[1][1] - cam[1]);
            t->x2 = static_cast<s16>(c[2][0] - cam[0]), t->y2 = static_cast<s16>(c[2][1] - cam[1]);
            t->tpage = prim->tpage;
            t->clut = prim->clut;
            add_prim(self, &t->tag, static_cast<u16>(self->layer + 1));
          } else {
            // Corners 0, 1, 3, 2 of the FT4 run round the fan's four vertices.
            const s32* c[4] = {v[0], v[base + 1], v[base + 3], v[base + 2]};
            base += 2;
            psyq::POLY_FT4* p = room ? new_ft4() : nullptr;
            if (p == nullptr) continue;
            psyq::setlen(p, 9);
            p->code = static_cast<u8>(self->blend != 0 ? 0x2E : 0x2C);
            p->r0 = rgb[0];
            p->g0 = rgb[1];
            p->b0 = rgb[2];
            if (uvs) {
              uv(c[0], true, &p->u0);
              uv(c[1], true, &p->u1);
              uv(c[2], true, &p->u2);
              uv(c[3], true, &p->u3);
            }
            p->x0 = static_cast<s16>(c[0][0] - cam[0]), p->y0 = static_cast<s16>(c[0][1] - cam[1]);
            p->x1 = static_cast<s16>(c[1][0] - cam[0]), p->y1 = static_cast<s16>(c[1][1] - cam[1]);
            p->x2 = static_cast<s16>(c[2][0] - cam[0]), p->y2 = static_cast<s16>(c[2][1] - cam[1]);
            p->x3 = static_cast<s16>(c[3][0] - cam[0]), p->y3 = static_cast<s16>(c[3][1] - cam[1]);
            p->tpage = prim->tpage;
            p->clut = prim->clut;
            add_prim(self, &p->tag, static_cast<u16>(self->layer + 1));
          }
        }
      }
    }
  }
}

void fieldactor_build_shadow(Actor* self, psyq::POLY_FT4* p, const s16* points, int count) {
  const u8* icon = shadow_icon();
  psyq::setlen(p, 9);
  p->code = 0x2F;
  if (icon != nullptr) {
    const s32 du = icon[4] - icon[2], dv = icon[5] - icon[3];
    p->u0 = icon[2];
    p->v0 = icon[3];
    p->u1 = static_cast<u8>(icon[2] + du);
    p->v1 = icon[3];
    p->u2 = icon[2];
    p->v2 = static_cast<u8>(icon[3] + dv);
    p->u3 = static_cast<u8>(icon[2] + du);
    p->v3 = static_cast<u8>(icon[3] + dv);
  }
  p->tpage = 0x56;
  p->clut = 0x7FCD;
  // Bounding box of the points: 9 packed (x, y) pairs, or 4 corners read in place from a
  // POLY_FT4 (points 0, 2, 4, 6 of the pairs from its x0: one corner every 8 bytes).
  const int stride = count == 9 ? 1 : 2;
  const int end = count == 9 ? 9 : count * 2;
  s32 x0 = points[0], x1 = points[0], y0 = points[1], y1 = points[1];
  for (int i = stride; i < end; i += stride) {
    const s32 x = points[i * 2], y = points[i * 2 + 1];
    if (x < x0) x0 = x;
    if (x1 < x) x1 = x;
    if (y < y0) y0 = y;
    if (y1 < y) y1 = y;
  }
  if (self->shadow_w != 0x400) {
    s32 w = ((x1 - x0) + 1) * self->shadow_w;
    if (w < 0) w += 0x3FF;
    w = ((w >> 10) - (w >> 31)) >> 1;
    const s32 c = (x1 + x0) / 2;
    x0 = c - w;
    x1 = c + w;
  }
  if (self->shadow_h != 0x400) {
    s32 h = ((y1 - y0) + 1) * self->shadow_h;
    if (h < 0) h += 0x3FF;
    h = ((h >> 10) - (h >> 31)) >> 1;
    const s32 c = (y1 + y0) / 2;
    y0 = c - h;
    y1 = c + h;
  }
  p->x0 = static_cast<s16>(x0), p->y0 = static_cast<s16>(y0);
  p->x1 = static_cast<s16>(x1), p->y1 = static_cast<s16>(y0);
  p->x2 = static_cast<s16>(x0), p->y2 = static_cast<s16>(y1);
  p->x3 = static_cast<s16>(x1), p->y3 = static_cast<s16>(y1);
  if (self->prim_count < 24) add_prim(self, &p->tag, static_cast<u16>(self->layer + 1));
}

void fieldactor_draw_attachment(Actor* self, const s32 pos[4], s32 h) {
  Actor* e = self->emote;
  if (e == nullptr) return;
  e->screen_x = self->screen_x;
  e->screen_y = static_cast<s16>(self->screen_y - (self->quad[5] - self->quad[3]));
  actor_mark_dirty(e);
  s32 scale = (h << 12) / (self->depth << 2);
  const s32 pct = read_u16(scene() + 0x68);
  if ((scale * pct) / 100 < 0x226) scale = 55000 / pct;
  e->scale_y = scale;
  e->scale_x = scale;
  actor_mark_dirty(e);
  e->x = pos[0];
  e->y = pos[1];
  e->z = pos[2];
  e->w = pos[3];
  actor_update(e, 0, 0);
  e->vtbl->draw(e, self->drawn_arg != 0 ? 1 : 0);
}

void fieldactor_draw_extra_children(Actor* self) {
  if (g_globals.children_pass == 0) return;
  for (Actor* c : self->children) {
    if (c == nullptr) continue;
    actor_mark_dirty(c);
    actor_update(c, 0, 0);
    c->vtbl->draw(c, self->drawn_arg != 0 ? 1 : 0);
  }
}

void fieldactor_draw_flat(Actor* self, const s16 cam[2], const u8* frame, const u8* cel) {
  self->prim_count = 0;
  const s32 hw = (frame[1] * self->scale_x) >> 12;
  const s16 hh = static_cast<s16>((frame[2] * self->scale_y) >> 12);
  {
    // Off-screen test against the 320x240 view around the camera.
    const s32 x1 = static_cast<s16>(self->screen_x + hw), x0 = static_cast<s16>(self->screen_x - hw);
    s32 c = ((x1 + x0 + 1) >> 1) - 0xA0;
    s32 d = c - cam[0];
    if (d < 0) d = cam[0] - c;
    if (d >= ((x1 - x0) + 0x141) >> 1) return;
    const s32 y1 = static_cast<s16>(self->screen_y + hh), y0 = static_cast<s16>(self->screen_y - hh);
    c = ((y0 + y1 + 1) >> 1) - 0x78;
    d = c - cam[1];
    if (d < 0) d = cam[1] - c;
    if (((y1 - y0) + 0xF1) >> 1 <= d) return;
  }
  u32 flip_x = self->flags >> 19 & 1, flip_y = self->flags >> 20 & 1;
  if (cel[3] & 1) flip_x ^= 1;
  if (cel[3] & 2) flip_y ^= 1;
  psyq::POLY_FT4* p = new_ft4();
  if (p == nullptr) return;
  psyq::setlen(p, 9);
  p->code = self->blend != 0 ? 0x2E : 0x2C;
  set_light(p, self);
  const u32 w = frame[1], h = frame[2];
  const u32 mode = rd16s(self->palettes) == 0x100 ? 1 : 0;
  u32 uv = 0;
  sprite_cache_b_alloc(actor_system(), &uv, static_cast<int>(mode), &p->clut, self->pixels + read_u32(frame + 8),
                       const_cast<u8*>(self->palettes) + frame[0] * 0x20 + 4, static_cast<s32>(w), static_cast<s32>(h),
                       self->blend);
  const u32 shift = 2 - mode;
  const s32 b0 = static_cast<s32>(uv & 0xFF), b1 = static_cast<s32>(uv >> 8 & 0xFF);
  const s32 b2 = static_cast<s32>(uv >> 16 & 0xFF), b3 = static_cast<s32>(uv >> 24);
  s32 u0 = b0 << shift, u1 = b2 << shift, v0 = b1, v1 = b3;
  const u32 abr = self->blend != 0 ? self->blend - 1u : 0u;
  p->tpage = static_cast<u16>(mode << 7 | (abr & 3) << 5 | 0x15);
  self->cache_slots = 1;
  s32 left = -rd16s(frame + 4), right = left + (u1 - u0);
  s32 top = -rd16s(frame + 6), bottom = top + (v1 - v0);
  if (flip_x) {
    left = rd16s(frame + 4) + static_cast<s8>(cel[1]);
    right = static_cast<s8>(cel[1]) - right;
  }
  if (flip_y) {
    top = rd16s(frame + 6) + static_cast<s8>(cel[2]);
    bottom = static_cast<s8>(cel[2]) - bottom;
  }
  self->origin_x = static_cast<s16>(self->screen_x - cam[0]);
  self->origin_y = static_cast<s16>(self->screen_y - cam[1]);
  p->x0 = static_cast<s16>(self->origin_x + (mul(left, self->scale_x) >> 12));
  p->y0 = static_cast<s16>(self->origin_y + (mul(top, self->scale_y) >> 12));
  p->x1 = static_cast<s16>(self->origin_x + (mul(right, self->scale_x) >> 12));
  p->y1 = p->y0;
  p->x2 = p->x0;
  p->y2 = static_cast<s16>(self->origin_y + (mul(bottom, self->scale_y) >> 12));
  p->x3 = p->x1;
  p->y3 = p->y2;
  // Texture edges (as the main sprite, with an extra check on u).
  const s32 dx = p->x3 - p->x0;
  const s32 du = u1 - u0 < 0 ? u0 - u1 : u1 - u0;
  if (dx < 0) {
    if (-dx == du) --u0;
    --u1;
  } else {
    const s32 ex = p->x2 - p->x1;
    if (dx != du || (ex < 0 ? -ex : ex) != du) --u1;
  }
  const s32 dy = p->y3 - p->y0;
  const s32 dv = v1 - v0 < 0 ? v0 - v1 : v1 - v0;
  if (dy < 0 && p->y0 <= p->y1) {
    if (-dy == dv) --v0;
  } else {
    const s32 ey = p->y2 - p->y1;
    if (!((dy < 0 ? -dy : dy) == dv && (ey < 0 ? -ey : ey) == dv)) --v1;
  }
  p->u0 = static_cast<u8>(u0), p->v0 = static_cast<u8>(v0);
  p->u1 = static_cast<u8>(u1), p->v1 = static_cast<u8>(v0);
  p->u2 = static_cast<u8>(u0), p->v2 = static_cast<u8>(v1);
  p->u3 = static_cast<u8>(u1), p->v3 = static_cast<u8>(v1);
  if (self->prim_count < 24) add_prim(self, &p->tag, static_cast<u16>(self->layer + 1));
}

void fieldactor_draw(Actor* self, int arg) {
  if (self->flags & kActorHidden) {
    self->prim_count = 0;
    return;
  }
  s32 draw_arg = arg;
  if (self->flags & kActorNoCache) draw_arg = 0;
  if ((self->flags & kActorAnimPaused) == 0) fieldactor_select_auto_anim(self, self->walked != 0);
  if (ticks() != 0) {
    self->walked_last = self->walked;
    self->walked = 0;
  }
  if (g_globals.children_pass != 0) mirror_update(self);  // 0x801f9800 (#3106: mirror images)
  const s32 old_sprite_anim = self->sprite_anim;
  actor_resolve_anim(self);
  s16 cam[2] = {0, 0};
  bg_get_view_origin(bg_renderer(), cam);
  if (self->flags & kActorNoScroll) cam[0] = cam[1] = 0;
  if (self->cel_header == nullptr || self->anim_data == nullptr) return;  // never resolved (host guard)
  const u8* cel = self->cels + static_cast<u16>(self->cel) * (self->cel_header[2] == 1 ? 4 : 8);
  const u8* frame = self->frames + cel[0] * 0xC;
  ActorSystem* sys = actor_system();
  if ((self->flags & kActorFixedLight) == 0) {
    if ((self->flags & kActorShaded) == 0 || scene()[0x98] == 0) {
      self->light[0] = scene()[0x6C];
      self->light[1] = scene()[0x6D];
      self->light[2] = scene()[0x6E];
    } else {
      self->light[0] = sys->light[0];
      self->light[1] = sys->light[1];
      self->light[2] = sys->light[2];
    }
    if (self->model == 0x7FFF) self->light[0] = self->light[1] = self->light[2] = 0x80;
  }
  if (self->flags & kActorScreen) {
    fieldactor_draw_flat(self, cam, frame, cel);
    return;
  }
  Screen* screen = field_screen();

  // Nothing moved: last frame's primitives, shifted by the scroll.
  if (reinterpret_cast<uintptr_t>(frame) == self->drawn_frame && self->drawn_pos[0] == self->x &&
      self->drawn_pos[1] == self->y && self->drawn_pos[2] == self->z && self->drawn_lift == self->lift &&
      sys->tint_on == 0 && static_cast<u8>(draw_arg) == self->drawn_arg) {
    const s16 sdx = static_cast<s16>(self->drawn_scroll[0] - cam[0]);
    const s16 sdy = static_cast<s16>(self->drawn_scroll[1] - cam[1]);
    self->drawn_scroll[0] = cam[0];
    self->drawn_scroll[1] = cam[1];
    for (s32 i = 0; i < self->prim_count; ++i) {
      auto* src = reinterpret_cast<psyq::POLY_FT4*>(self->prims[i]);
      const u8 kind = src->code & 0xFC;
      if (kind == 0x24) {
        auto* d = static_cast<psyq::POLY_FT3*>(screen->alloc(sizeof(psyq::POLY_FT3)));
        if (d == nullptr) continue;
        std::memcpy(static_cast<void*>(d), src, sizeof(psyq::POLY_FT3));
        self->prims[i] = &d->tag;
        if (sdx != 0) {
          d->x0 = static_cast<s16>(d->x0 + sdx);
          d->x2 = static_cast<s16>(d->x2 + sdx);
          d->x1 = static_cast<s16>(d->x1 + sdx);
        }
        if (sdy != 0) {
          d->y0 = static_cast<s16>(d->y0 + sdy);
          d->y2 = static_cast<s16>(d->y2 + sdy);
          d->y1 = static_cast<s16>(d->y1 + sdy);
        }
      } else if (kind == 0x2C) {
        auto* d = new_ft4();
        if (d == nullptr) continue;
        std::memcpy(d, src, sizeof(psyq::POLY_FT4));
        if (sdx != 0) {
          d->x0 = static_cast<s16>(d->x0 + sdx);
          d->x1 = static_cast<s16>(d->x1 + sdx);
          d->x2 = static_cast<s16>(d->x2 + sdx);
          d->x3 = static_cast<s16>(d->x3 + sdx);
        }
        if (sdy != 0) {
          d->y0 = static_cast<s16>(d->y0 + sdy);
          d->y1 = static_cast<s16>(d->y1 + sdy);
          d->y2 = static_cast<s16>(d->y2 + sdy);
          d->y3 = static_cast<s16>(d->y3 + sdy);
        }
        self->prims[i] = &d->tag;
      }
    }
    self->drawn_frame = reinterpret_cast<uintptr_t>(frame);
    for (int k = 0; k < 4; ++k) {
      self->quad[k * 2] = static_cast<s16>(self->quad[k * 2] + sdx);
      self->quad[k * 2 + 1] = static_cast<s16>(self->quad[k * 2 + 1] + sdy);
    }
    fieldactor_apply_light_zone(self);
    const s32 relit = (self->flags & kActorShadow) ? 2 : 1;
    for (s32 i = 0; i < relit && i < self->prim_count; ++i) {
      u8* rgb = reinterpret_cast<u8*>(psyq::prim_words(self->prims[i]));
      rgb[0] = self->light[0];
      rgb[1] = self->light[1];
      rgb[2] = self->light[2];
    }
    for (int i = 0; i < self->cache_slots; ++i) {
      const s32 slot = self->cache_slot[i];
      if (slot == -1) continue;
      if (slot < 0x20) {
        actor_sys_touch_cache_a(sys, slot);
      } else {
        actor_sys_touch_cache_b(sys, slot - 0x20);
      }
    }
    if (Actor* e = self->emote) {
      actor_mark_dirty(e);
      actor_update(e, 0, 0);
      e->vtbl->draw(e, draw_arg);
    }
    fieldactor_draw_extra_children(self);
    return;
  }

  self->drawn_frame = reinterpret_cast<uintptr_t>(frame);
  self->drawn_arg = static_cast<u8>(draw_arg);
  fieldactor_apply_light_zone(self);
  self->drawn_pos[0] = self->x;
  self->drawn_pos[1] = self->y;
  self->drawn_pos[2] = self->z;
  self->drawn_pos[3] = self->w;
  self->drawn_lift = self->lift;
  self->drawn_scroll[0] = cam[0];
  self->drawn_scroll[1] = cam[1];
  self->prim_count = 0;
  const s32 H = read_u16(scene() + 0x6A);
  s32 rotation = self->spin_angle == 0x7FFF ? 0 : sys->camera_rot.vz + self->spin_angle;
  if ((self->flags & kActorZones) == 0) self->zone = nullptr;
  const s32 old_depth = self->depth;
  s32 pos[4] = {self->x, self->y, self->z, self->w};

  if ((self->flags & kActorScreenSpace) == 0) {
    // Stairs: zones with a step height draw the actor on whole steps.
    const u8* zone = self->zone;
    if (zone != nullptr && read_u16(zone + 0x40) != 0) {
      const s32 step = read_u16(zone + 0x40);
      s32 lo = rd16s(zone + 0x12), hi = lo;
      for (int k = 1; k < 3; ++k) {
        const s32 v = rd16s(zone + 0x12 + k * 8);
        if (v < lo) lo = v;
        if (hi < v) hi = v;
      }
      const s32 y = (pos[1] < 0 ? pos[1] + 0xFFF : pos[1]) >> 12;
      const s32 stepped = (lo + ((y - lo) / step + 1) * step) << 12;
      pos[1] = (hi << 12) < stepped ? hi << 12 : stepped;
    }
    s32 v[3], sx, sy;
    transrot(pos, v);
    if (!to_screen(v, H, &sx, &sy)) return;
    self->screen_x = static_cast<s16>(sx);
    self->screen_y = static_cast<s16>(sy);
    walk_update_layer(self);
    self->depth = depth_of(v[2]);
    if (self->depth < 1) return;
  }
  s16 sx = self->screen_x, sy = self->screen_y;  // where the sprite goes (lifted below)

  if ((self->flags & (kActorShadow | kActorScreenSpace)) == kActorShadow) draw_shadow(self, pos, cam, H);

  // Reflections (cell list 9).
  const u8* mirror = nullptr;
  {
    const int cell = zone_cell_at(self, &self->screen_x);
    if (cell != -1) {
      const int count = scene_cell_count(cell, 9);
      const u8* item = scene_cell_list(cell, 9);
      for (int i = 0; i < count; ++i, item += 0x28) {
        if (zone_test_point(self, item, &self->screen_x, self->y >> 12)) {
          mirror = item;
          break;
        }
      }
    }
  }

  // Size on screen (20.12).
  const u32 w = frame[1], h = frame[2];
  const s32 tw = static_cast<s32>(w << 12), th = static_cast<s32>(h << 12);
  const s32 scale = map_data().actor_scale;
  s32 sw, sh;
  if (self->flags & kActorRawScale) {
    sw = static_cast<s32>(w) * self->scale_x;
    sh = static_cast<s32>(h) * self->scale_y;
  } else if ((self->flags & kActorScreenSpace) == 0) {
    const s32 d = (draw_arg != 0 && (self->flags & 0x40) && old_sprite_anim == self->sprite_anim) ? old_depth
                                                                                                 : self->depth;
    const s32 kx = ((mul(H, self->scale_x) >> 12) << 12) / (d << 2);
    const s32 ky = ((mul(H, self->scale_y) >> 12) << 12) / (d << 2);
    sw = (static_cast<s32>(w) * scale * 0x5A / 0x960) * kx;
    sh = (static_cast<s32>(h) * scale * 0xB4 / 0x12C0) * ky;
  } else {
    sw = (static_cast<s32>(w) * scale * 0x5A / 0x960) * self->scale_x;
    sh = (static_cast<s32>(h) * scale * 0xB4 / 0x12C0) * self->scale_y;
  }
  if (sw == 0) sw = 0x1000;
  if (sh == 0) sh = 0x1000;
  if (0x3F000 < sw) {
    if (self->flags & kActorClampScale) {
      sw = 0x3F000;
      sh = static_cast<s32>(h * 0x3F000) / static_cast<s32>(w);
    } else {
      draw_arg = 0;
    }
  }
  if (mirror == nullptr) {
    // Off screen: no primitives this frame.
    const s16 x0 = static_cast<s16>(self->screen_x - (sw >> 12)), x1 = static_cast<s16>(self->screen_x + (sw >> 12));
    const s16 y0 = static_cast<s16>(self->screen_y - (sh >> 12)), y1 = static_cast<s16>(self->screen_y + (sh >> 12));
    bool culled = true;
    s32 c = (x1 + x0 + 1) / 2 - 0xA0;
    s32 d = c - cam[0];
    if (d < 0) d = cam[0] - c;
    if (d < ((x1 - x0) + 0x141) >> 1) {
      c = (y0 + y1 + 1) / 2 - 0x78;
      d = c - cam[1];
      if (d < 0) d = cam[1] - c;
      culled = !(d < ((y1 - y0) + 0xF1) >> 1);
    }
    if (culled) {
      self->drawn_frame = 0;
      self->prim_count = 0;
      self->cache_slots = 0;
      fieldactor_draw_attachment(self, pos, H);
      fieldactor_draw_extra_children(self);
      return;
    }
  }

  // Facing: mirrored directions, the cel's own flips, the sprite rotation.
  u32 flip_x = self->flags >> 19 & 1, flip_y = self->flags >> 20 & 1;
  if (self->anim < 0x24) {
    const u32 d = self->auto_anim % 12;
    if (d != 0 && d < 6) flip_x ^= 1;
  }
  if (cel[3] & 1) flip_x ^= 1;
  if (cel[3] & 2) flip_y ^= 1;
  const s32 sin_r = sin4096(static_cast<u32>(-rotation)), cos_r = cos4096(static_cast<u32>(-rotation));
  if (self->lift != 0 && (self->flags & kActorScreenSpace) == 0) {
    pos[1] += self->lift;
    s32 v[3], lx, ly;
    transrot(pos, v);
    if (!to_screen(v, H, &lx, &ly)) return;
    sx = static_cast<s16>(lx);
    sy = static_cast<s16>(ly);
    self->depth = depth_of(v[2]);
    const s16 gx = self->screen_x, gy = self->screen_y;
    self->screen_x = sx;
    self->screen_y = sy;
    walk_update_layer(self);
    self->screen_x = gx;
    self->screen_y = gy;
  }
  s32 slot_b = -1, slot_c = -1;
  if (self->cel_header[2] == 0) {
    if (cel[5] != 0) slot_b = self->prim_count++;
    if (cel[6] != 0) {
      const u8* part = self->cel_header + read_u32(self->cel_header + 0xC) + (cel[6] - 1) * kSpriteRecordSize;
      if (part[0x1B] != 0) slot_c = self->prim_count++;
    }
  }

  // The main sprite.
  psyq::POLY_FT4* p = new_ft4();
  if (p == nullptr) return;
  psyq::setlen(p, 9);
  p->code = self->blend != 0 ? 0x2E : 0x2C;
  set_light(p, self);
  const u32 step_x = (static_cast<u32>(tw) << 12) / static_cast<u32>(sw);
  const u32 step_y = (static_cast<u32>(th) << 12) / static_cast<u32>(sh);
  if (step_x == 0 || step_y == 0) return;
  const s32 screen_w = (tw + static_cast<s32>(step_x) - 1) / static_cast<s32>(step_x);
  const s32 screen_h = th / static_cast<s32>(step_y);
  s32 u0 = 0, v0 = 0, u1 = 0, v1 = 0;
  s32 left = 0, top = 0;
  const u8* pixels = self->pixels + read_u32(frame + 8);
  u8* palette = const_cast<u8*>(self->palettes) + frame[0] * 0x20 + 4;
  if (draw_arg != 0) {
    left = -(rd16s(frame + 4) * sw) / static_cast<s32>(w << 12);
    top = -(rd16s(frame + 6) * sh) / static_cast<s32>(h << 12);
    const s32 phase_x = (rd16s(frame + 4) * sw) / static_cast<s32>(w) & 0xFFF;
    const s32 phase_y = (rd16s(frame + 6) * sh) / static_cast<s32>(h) & 0xFFF;
    u8* work = screen->extra.get() + screen->extra_cursor;
    const int slot = sprite_cache_a_alloc(sys, &u0, &v0, &u1, &v1, &work, pixels, palette, sw, sh, tw, th, screen_w,
                                          screen_h, phase_x, phase_y);
    self->cache_slot[0] = static_cast<s8>(slot);
    if (slot != -1) {
      const u32 abr = self->blend != 0 ? self->blend - 1u : 0u;
      p->tpage = static_cast<u16>((abr & 3) << 5 | 0x105);
    } else {
      draw_arg = 0;
    }
  }
  if (draw_arg == 0) {
    u32 uv = 0;
    const int slot = sprite_cache_b_alloc(sys, &uv, read_u16(self->palettes) >> 15, &p->clut, pixels, palette,
                                          static_cast<s32>(w), static_cast<s32>(h), self->blend);
    self->cache_slot[0] = static_cast<s8>(slot);
    if (slot == -1) return;
    self->cache_slot[0] = static_cast<s8>(slot + 0x20);
    u0 = static_cast<s32>((uv & 0xFF) << 2);
    v0 = static_cast<s32>(uv >> 8 & 0xFF);
    u1 = static_cast<s32>((uv >> 16 & 0xFF) << 2);
    v1 = static_cast<s32>(uv >> 24);
    const u32 abr = self->blend != 0 ? self->blend - 1u : 0u;
    p->tpage = static_cast<u16>((abr & 3) << 5 | 0x15);
  }
  self->cache_slots = 1;
  s32 right, bottom;
  if (draw_arg != 0) {
    right = left + (u1 - u0);
    bottom = top + (v1 - v0);
  } else {
    left = -rd16s(frame + 4);
    right = left + (u1 - u0);
    top = -rd16s(frame + 6);
    bottom = top + (v1 - v0);
  }
  if (flip_x) {
    const s32 k = draw_arg != 0 ? (screen_w * static_cast<s8>(cel[1])) / static_cast<s32>(w) : static_cast<s8>(cel[1]);
    right = k - right;
    left = k - left;
  }
  if (flip_y) {
    const s32 k = draw_arg != 0 ? (screen_h * static_cast<s8>(cel[2])) / static_cast<s32>(h) : static_cast<s8>(cel[2]);
    bottom = k - bottom;
    top = k - top;
  }
  self->origin_x = static_cast<s16>(sx - cam[0]);
  self->origin_y = static_cast<s16>(sy - cam[1]);
  const s32 xs = draw_arg != 0 ? 0x1000 : (screen_w << 12) / static_cast<s32>(w);
  const s32 ys = draw_arg != 0 ? 0x1000 : (screen_h << 12) / static_cast<s32>(h);
  auto px = [&](s32 x, s32 y) {
    return static_cast<s16>(self->origin_x + (mul((mul(x, cos_r) + mul(y, sin_r)) >> 7, xs) >> 17));
  };
  auto py = [&](s32 x, s32 y) {
    return static_cast<s16>(self->origin_y + (mul((mul(x, -sin_r) + mul(y, cos_r)) >> 7, ys) >> 17));
  };
  p->x0 = px(left, top), p->y0 = py(left, top);
  p->x1 = px(right, top), p->y1 = py(right, top);
  p->x2 = px(left, bottom), p->y2 = py(left, bottom);
  p->x3 = px(right, bottom), p->y3 = py(right, bottom);
  self->quad[0] = p->x0, self->quad[1] = p->y0;
  self->quad[2] = p->x1, self->quad[3] = p->y1;
  self->quad[4] = p->x2, self->quad[5] = p->y2;
  self->quad[6] = p->x3, self->quad[7] = p->y3;
  fix_uvs(p, u0, v0, u1, v1);
  p->u0 = static_cast<u8>(u0), p->v0 = static_cast<u8>(v0);
  p->u1 = static_cast<u8>(u1), p->v1 = static_cast<u8>(v0);
  p->u2 = static_cast<u8>(u0), p->v2 = static_cast<u8>(v1);
  p->u3 = static_cast<u8>(u1), p->v3 = static_cast<u8>(v1);
  if (self->prim_count < 24) add_prim(self, &p->tag, static_cast<u16>(self->layer + 1));

  if ((self->flags & kActorScreenSpace) == 0) {
    if (mirror != nullptr) {
      // The reflection: the sprite flipped about the mirror's line (item +0x26 height).
      const s32 mpos[3] = {self->x, (rd16s(mirror + 0x26) << 12) - self->lift, self->z};
      s32 v[3], mx, my;
      transrot(mpos, v);
      if (!to_screen(v, H, &mx, &my)) return;
      s16 height = static_cast<s16>(self->quad[5] - self->quad[1]);
      if (height < 0) height = static_cast<s16>(-height);
      psyq::POLY_FT4* m = new_ft4();
      if (m != nullptr) {
        std::memcpy(m, p, sizeof(psyq::POLY_FT4));
        psyq::setlen(m, 9);
        m->code = 0x2C;
        if (mirror[0x25] != 0) {
          m->code |= 2;
          m->tpage = static_cast<u16>(((mirror[0x25] - 1) & 3) << 5 | (draw_arg != 0 ? 0x105 : 0x15));
        }
        m->y2 = static_cast<s16>(my - cam[1]);
        m->y3 = static_cast<s16>(my - cam[1]);
        m->y0 = static_cast<s16>(m->y2 + height);
        m->y1 = static_cast<s16>(m->y3 + height);
        m->v0 = static_cast<u8>(m->v0 - 1);
        m->v1 = static_cast<u8>(m->v1 - 1);
        m->v2 = static_cast<u8>(m->v2 - 1);
        m->v3 = static_cast<u8>(m->v3 - 1);
        if (self->prim_count < 24) add_prim(self, &m->tag, mirror[0x24]);
      }
    }
    fieldactor_build_light_split(self, screen_w, screen_h, p, static_cast<int>(flip_x << 1 | flip_y), draw_arg);
  }

  // Parts of the cel: B behind the body, A in front, C behind or in front.
  if (self->cel_header[2] == 0) {
    if (cel[5] != 0 && slot_b >= 0) {
      const s32 count = self->prim_count;
      self->prim_count = slot_b;
      sprite_draw_part(self, cel, self->cel_header + read_u32(self->cel_header + 8) + (cel[5] - 1) * kSpriteRecordSize,
                       cos_r, sin_r, static_cast<int>(flip_x), static_cast<int>(flip_y), draw_arg);
      self->prim_count = count;
    }
    if (cel[4] != 0) {
      sprite_draw_part(self, cel, self->cel_header + read_u32(self->cel_header + 4) + (cel[4] - 1) * kSpriteRecordSize,
                       cos_r, sin_r, static_cast<int>(flip_x), static_cast<int>(flip_y), draw_arg);
    }
    if (cel[6] != 0) {
      const u8* part = self->cel_header + read_u32(self->cel_header + 0xC) + (cel[6] - 1) * kSpriteRecordSize;
      s32 count = 0;
      if (part[0x1B] != 0) {
        count = self->prim_count;
        self->prim_count = slot_c;
      }
      sprite_draw_part(self, cel, part, cos_r, sin_r, static_cast<int>(flip_x), static_cast<int>(flip_y), draw_arg);
      if (part[0x1B] != 0) self->prim_count = count;
    }
  }
  fieldactor_draw_attachment(self, pos, H);
  fieldactor_draw_extra_children(self);
}

void fieldactor_camera_follow(Actor* self) {
  const s32 pos[3] = {self->x, self->y, self->z};
  s32 v[3], sx, sy;
  transrot(pos, v);
  if (to_screen(v, read_u16(scene() + 0x6A), &sx, &sy)) {
    self->screen_x = static_cast<s16>(sx);
    self->screen_y = static_cast<s16>(sy);
  } else {
    self->screen_x = static_cast<s16>(read_u16(scene() + 0x34) >> 1);
    self->screen_y = static_cast<s16>(read_u16(scene() + 0x36) >> 1);
  }
  if (BgRenderer* bg = bg_renderer()) {
    bg->focus_x = self->screen_x;
    bg->focus_y = static_cast<s16>(self->screen_y - 0x10);
    bg_update_scroll(bg);
    bg->floor_y = self->y;
  }
}

void fieldactor_move_step(Actor* self) {
  const s32 t = ticks();
  if (t == 0) return;
  const u8 mode = self->move_mode;
  if ((self->flags & kActorScreenSpace) != 0) {
    // Screen-space move: interpolate +0x74 / +0x76 from the start (+0x6c / +0x70).
    self->move_left = static_cast<s16>(self->move_left - t);
    const s32 left = self->move_left;
    if (left < 1) {
      self->move_mode = 0;
      self->screen_x = static_cast<s16>(self->target[0]);
      self->screen_y = static_cast<s16>(self->target[1]);
    } else {
      self->screen_x = static_cast<s16>(self->vel_x + ((self->target[0] - self->vel_x) * (self->move_frames - left)) /
                                                          self->move_frames);
      self->screen_y = static_cast<s16>(self->vel_z + ((self->target[1] - self->vel_z) * (self->move_frames - self->move_left)) /
                                                          self->move_frames);
    }
  } else if (mode == 3) {
    // Hop on the spot (cmd 0x1a with y 0): the lift follows a sine.
    self->zone = nullptr;
    self->move_left = static_cast<s16>(self->move_left - t);
    if (self->move_left < 1) {
      self->move_mode = 0;
      self->x = self->target[0];
      self->z = self->target[2];
      self->lift = self->jump_angle_a;
    } else {
      self->x += self->vel_x * t;
      self->z += self->vel_z * t;
      self->lift = self->jump_angle_a + self->jump_height * sin4096(static_cast<u32>((self->move_left << 11) / self->move_frames));
    }
  } else if (mode == 4) {
    // Jump (cmd 0x19 / 0x1a): an arc between the two angles from actor_jump_setup.
    self->zone = nullptr;
    self->move_left = static_cast<s16>(self->move_left - t);
    if (self->move_left < 1) {
      self->move_mode = 0;
      self->x = self->target[0];
      self->y = self->target[1];
      self->z = self->target[2];
    } else {
      self->x += self->vel_x * t;
      self->z += self->vel_z * t;
      const s32 a = self->jump_angle_a + ((self->jump_angle_b - self->jump_angle_a) * (self->move_frames - self->move_left)) /
                                             self->move_frames;
      self->y = self->target[3] + self->jump_height * sin4096(static_cast<u32>(a));
    }
  } else if (mode != 0) {
    // Walk to the target (mode 1 in 3D, 2 on the floor).
    const s32 step = self->speed * t;
    s32 dx = self->target[0] - self->x;
    if (dx < 0) dx += 0xFF;
    dx >>= 8;
    s32 dz = self->target[2] - self->z;
    if (dz < 0) dz += 0xFF;
    dz >>= 8;
    const s32 heading = ratan2(dz, dx);
    s32 dy = self->target[1] - self->y;
    if (dy < 0) dy += 0xFF;
    dy >>= 8;
    s32 rise = 0;
    s32 len2;
    if (self->move_mode == 1) {
      len2 = mul(dx, dx) + mul(dy, dy) + mul(dz, dz);
      rise = dy;
    } else {
      const u8* zone = nullptr;
      if (self->flags & kActorZones) {
        const int count = scene_cell_count(0, 14);
        const u8* zones = scene_cell_list(0, 14);
        for (int i = 0; i < count; ++i) {
          if (walk_point_in_tri(self, zones + i * 0x58, &self->x)) {
            zone = zones + i * 0x58;
            break;
          }
        }
      }
      if (zone != nullptr && rd32s(zone + 0x34) != 0) {
        const s32 b = rd32s(zone + 0x34);
        const s32 fx = (dx < 0 ? dx + 0xF : dx) >> 4, fz = (dz < 0 ? dz + 0xF : dz) >> 4;
        rise = ((-(mul(rd32s(zone + 0x30), (fx + (self->x >> 12)) - rd16s(zone + 0x10)) + mul(b, -rd16s(zone + 0x12)) +
                   mul(rd32s(zone + 0x38), (fz + (self->z >> 12)) - rd16s(zone + 0x14))) /
                 b) -
                (self->y >> 12)) *
               0x10;
      }
      len2 = mul(dx, dx) + mul(dz, dz);
    }
    const s32 len = SquareRoot0(len2);
    bool stopped = false;
    if (len <= ((step < 0 ? step + 0xFF : step) >> 8)) {
      if (self->flags & kActorBlockedByActors) {
        s32 dest[3] = {self->target[0], mode == 1 ? self->target[1] : 0, self->target[2]};
        if (actor_collides(self, dest, 1)) stopped = true;
      }
      if (!stopped) self->move_mode = 0;
    }
    if (!stopped) {
      s32 mv[3] = {0, 0, 0};
      if (rise == 0) {
        const s32 s = (step < 0 ? step + 0xFF : step) >> 8;
        s32 c = mul(cos4096(static_cast<u32>(heading)), s);
        mv[0] = (c < 0 ? c + 0xF : c) >> 4;
        c = mul(sin4096(static_cast<u32>(heading)), s);
        mv[2] = (c < 0 ? c + 0xF : c) >> 4;
      } else {
        psyq::VECTOR v{dx >> 8, rise >> 8, dz >> 8, 0};
        psyq::Square0(&v, &v);
        const s32 l = SquareRoot0(v.vx + v.vy + v.vz);
        if (l == 0) {
          const s32 l2 = SquareRoot0(mul(dx, dx) + mul(rise, rise) + mul(dz, dz));
          mv[0] = mul(dx, step) / l2;
          mv[1] = mul(rise, step) / l2;
          mv[2] = mul(dz, step) / l2;
        } else {
          const s32 s = (step < 0 ? step + 0xFF : step) >> 8;
          mv[0] = mul(dx, s) / l;
          mv[1] = mul(rise, s) / l;
          mv[2] = mul(dz, s) / l;
        }
      }
      if (self->move_mode != 0 && (self->flags & kActorBlockedByActors) && actor_collides(self, mv, 0)) {
        mv[0] = mv[1] = mv[2] = 0;
      }
      if (self->move_mode == 0 || mv[0] != 0 || mv[2] != 0 || mv[1] != 0) {
        if (self->move_mode == 0) {
          self->x = self->target[0];
          self->angle = self->heading;
          self->z = self->target[2];
          if (mode == 1) self->y = self->target[1];
        } else {
          self->x += mv[0];
          self->y += mv[1];
          self->angle = self->heading;
          self->z += mv[2];
        }
        self->walked = self->move_mode;
        if (self->wait_state == 3) self->walked = 1;
      } else {
        self->walked = 0;
      }
      if (mode == 2) walk_snap_to_floor(self);
    }
  }
  if (self->walked == 0) {
    self->still_time = static_cast<s16>(self->still_time + t);
  } else {
    self->still_time = 0;
  }
  // Patrols turn round after standing for 120 frames away from the start.
  if ((self->flags & kActorPatrol) && 0x78 < self->still_time && static_cast<u32>(self->move_mode - 1) < 2) {
    const s32 dx = (self->x - self->start[0]) >> 12, dy = (self->y - self->start[1]) >> 12,
              dz = (self->z - self->start[2]) >> 12;
    if ((self->collide_radius << 1) / 3 < SquareRoot0(mul(dx, dx) + mul(dy, dy) + mul(dz, dz))) {
      self->shuttle = self->shuttle == 0 ? self->move_mode : 0;
      self->still_time = 0;
      for (int i = 0; i < 4; ++i) std::swap(self->target[i], self->start[i]);
      self->heading = static_cast<s16>(ratan2(self->target[2] - self->z, self->target[0] - self->x));
      actor_save_anim(self);
      const s32 delay = actor_walk_frame_delay(self);
      actor_turn_start(self, self->heading, static_cast<u16>((delay * 0x1000) / 360), 0);
      self->turning = 1;
      if ((self->flags & kActorPatrolEmote) && ++self->laps >= self->lap_limit && self->emote == nullptr) {
        actor_create_attachment(self);
        actor_cmd_set_anim(self->emote, self->lap_emote);
        self->emote_timer = 300;
        actor_mark_dirty(self);
        self->laps = 0;
      }
    }
  }
}

void fieldactor_player_control(Actor* self, int camera) {
  if (ticks() == 0) return;
  FieldAnalog& an = field_analog();
  self->analog = an.enabled;
  const s32 ax = an.x, ay = an.y;
  if (an.enabled && ax == 0 && ay == 0) self->analog = 0;
  bool move = false;
  s32 dir = 0;
  const u8* script_flags = globals().script_flags;
  if (self->analog == 0) {
    const u16 run_button = read_u16(globals().config + 2);
    if ((globals().held[0] & run_button) == 0 || ((script_flags[1] >> 2) & 1) != 0) {
      self->flags &= ~kActorRunning;
      self->speed = self->walk_speed;
    } else {
      self->speed = g_globals.vehicle_control == 0 ? self->walk_speed << 2 : self->walk_speed;
      self->flags |= kActorRunning;
    }
    dir = field_pad_direction();
    if (dir != -1) move = true;
  } else {
    s32 m = ax < 0 ? -ax : ax;
    s32 n = ay < 0 ? -ay : ay;
    if (m < 0) m = 0;
    if (n < 0) n = 0;
    if (m < n) m = n;
    if (m < 0x1E) {
      self->flags &= ~kActorRunning;
    } else if (((script_flags[1] >> 2) & 1) != 0) {
      m = 0x1D;
      self->flags &= ~kActorRunning;
    } else {
      self->flags |= kActorRunning;
    }
    const s32 v = mul(m, self->walk_speed);
    self->speed = g_globals.vehicle_control == 0 ? v / 0x14 : v / 0x50;
    move = m != 0;
    if (g_globals.screen_clamp == 0) {
      dir = ratan2(ay, ax) + read_u16(scene() + 0xCC) + 0x400;
    } else {
      s32 p[3];
      actor_sys_project_point(actor_system(), static_cast<s16>(self->screen_x - 0x1E), self->screen_y, self->y >> 12,
                              p);
      dir = ratan2(ay, ax) + ratan2(p[2] - (self->z >> 12), p[0] - (self->x >> 12)) + 0x800;
    }
  }
  if (g_globals.vehicle_control != 0) {
    path_control(self, ax, ay);  // 0x801f9800 (#3108: walking along a scripted path)
    return;
  }
  actor_anim_advance(self);
  fieldactor_walk_step(self, dir, move ? 1 : 0);
  const s32 pos[3] = {self->x, self->y, self->z};
  s32 v[3], sx, sy;
  transrot(pos, v);
  if (to_screen(v, read_u16(scene() + 0x6A), &sx, &sy)) {
    self->screen_x = static_cast<s16>(sx);
    self->screen_y = static_cast<s16>(sy);
  } else {
    self->screen_x = static_cast<s16>(read_u16(scene() + 0x34) >> 1);
    self->screen_y = static_cast<s16>(read_u16(scene() + 0x36) >> 1);
  }
  if (camera != 0) {
    if (BgRenderer* bg = bg_renderer()) {
      bg->focus_x = self->screen_x;
      bg->focus_y = static_cast<s16>(self->screen_y - 0x10);
      bg_update_scroll(bg);
      bg->floor_y = self->y;
    }
  }
  walk_update_layer(self);
}

}  // namespace ss::game::field
