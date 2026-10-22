// World-map actor class (overlay #3102 0x80082d58-0x800888e0): 0x44c bytes, vtable
// 0x8008f460. Sprite draw with a terrain-conforming shadow, walking on the terrain
// (wall slide, water, the encounter area of each 0x1000 square), the follow camera and
// scripted moves.
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "game/boot/rng.h"
#include "game/field/actors.h"
#include "game/field/field.h"
#include "game/field/field_input.h"
#include "game/field/load_queue.h"
#include "game/field/map.h"
#include "game/field/screen.h"
#include "game/field/sprite_render.h"
#include "game/field/worldmap.h"
#include "game/field/worldmap_internal.h"
#include "runtime/runtime.h"

namespace ss::game::field {
using namespace wm;

// worldmap_render.cpp
void world_bend_project(const s16 (*pts)[4], int n, u32* sxy_out, s32* flag_out);
u32 world_project_point(const s16 pt[3], s32* sz);
void world_bend_to_world(const s16 pt[3], s16 out[3]);

namespace {

WorldActor* W(Actor* a) { return static_cast<WorldActor*>(a); }
FieldGlobals& fg() { return globals(); }
s32 ticks() { return fg().frame_ticks; }
WorldView& view() { return world().view; }
bool sflag(int byte, int bit) { return ((fg().script_flags[byte] >> bit) & 1) != 0; }

void wa_draw(Actor* a, int arg);
void wa_control(Actor* a, int camera);
void wa_follow(Actor* a);
void wa_move_step(Actor* a);

// 0x80084978: colour word * rgb / 128 per channel (saturating), the raw-texture bit cleared.
u32 colour_mul(u32 c, const u8* rgb) {
  c &= 0xfeffffffu;
  for (int i = 0; i < 3; ++i) {
    const u32 sh = static_cast<u32>(i) * 8;
    u32 v = (((c >> sh) & 0xff) * rgb[i]) >> 7;
    if (v > 0xff) v = 0xff;
    c = (c & ~(0xffu << sh)) | v << sh;
  }
  return c;
}

// 0x800849f0: the automatic animation: 12 view-relative directions; stand / walk / run.
void auto_anim(Actor* a, bool walked) {
  if (a->anim != -1) {
    a->auto_anim = static_cast<u16>(a->anim);
    return;
  }
  if ((a->flags & 0x1000000) != 0 && !sflag(1, 4)) {
    // Smoothed turning: at most 0x155 (0x38 for vehicle 4) per frame.
    s16 ang = static_cast<s16>(a->angle & 0xfff);
    if (ang > 0x7ff) ang = static_cast<s16>(ang - 0x1000);
    a->angle = ang;
    s16 prev = static_cast<s16>(a->prev_angle & 0xfff);
    if (prev > 0x7ff) prev = static_cast<s16>(prev - 0x1000);
    a->prev_angle = prev;
    if (a->angle != a->prev_angle) {
      s32 d = a->angle - a->prev_angle;
      if (d >= 0x800) {
        d -= 0x1000;
      } else if (d < -0x800) {
        d += 0x1000;
      }
      const s32 lim = a->vehicle == 4 ? 0x38 : 0x155;
      if (std::abs(d) > lim) a->angle = static_cast<s16>(a->prev_angle + (d < 1 ? -lim : lim));
    }
  }
  const s32 yaw = world_camera_yaw();
  const u32 dir = static_cast<u32>((yaw - a->angle) + 0xaa) & 0xfff;
  const u32 d12 = dir / 0x156;
  if (!walked) {
    a->anim_forward = 1;
    a->anim_timer = 0;
    a->anim_frame = 0;
    a->auto_anim = static_cast<u16>(d12 % 12 + 12);
    return;
  }
  if ((a->flags & 0x400000) != 0 && !sflag(1, 6)) {
    a->auto_anim = static_cast<u16>(d12 + 0x18);
    return;
  }
  if (a->auto_anim > 0x17) a->anim_frame = static_cast<s16>(a->anim_frame / 2);
  a->auto_anim = static_cast<u16>(d12);
}

// 0x80084ca0: projects the actor (block-relative) with the curvature: screen position
// (+0x3b6 / +0x74), depth (+0x3fc = SZ3 >> 3).
void project(Actor* a) {
  WorldBg* bg = world_bg();
  s16 org[2] = {0, 0};
  if (bg != nullptr) world_block_origin(bg, org);
  const s16 p[3] = {static_cast<s16>((a->x - org[0] * 0x1000) >> 12), static_cast<s16>(a->y >> 12),
                    static_cast<s16>((a->z - org[1] * 0x1000) >> 12)};
  const s32 zero[3] = {0, 0, 0}, scale[3] = {0x1000, 0x1000, 0x1000};
  world_set_object_matrix(zero, view().light_angle, scale);
  s32 sz = 0;
  const u32 s = world_project_point(p, &sz);
  a->origin_x = static_cast<s16>(s);
  a->origin_y = static_cast<s16>(s >> 16);
  a->screen_x = a->origin_x;
  a->screen_y = a->origin_y;
  W(a)->depth8 = sz >> 3;
}

// Wading: the deeper of the polygon's corners over the ground (water polygons), 0..0x20.
void update_wade(Actor* a, s32 h, const TerrainHit& hit) {
  WorldActor* w = W(a);
  if ((hit.attr & 4) == 0) {
    if ((hit.attr & 2) == 0) {
      w->wade = 0;
      return;
    }
    w->wade = 0x20;
    return;
  }
  s32 top = hit.v[0] != nullptr ? rd16(hit.v[0] + 2) : h;
  for (int i = 1; i < hit.corners; ++i) {
    if (hit.v[i] != nullptr && top < rd16(hit.v[i] + 2)) top = rd16(hit.v[i] + 2);
    w->wade = static_cast<u8>(((top - h) * 0x20) / 0xfa);
  }
  if (w->wade < 0x21) return;
  w->wade = 0x20;
}

// 0x80085758: the terrain height under the actor (+0x43c) and wading.
void ground_snap(Actor* a) {
  WorldBg* bg = world_bg();
  if (bg == nullptr) return;
  TerrainHit hit;
  const s32 h = world_terrain_query(bg, a->x, a->z, &hit);
  if (h == -0x8000) return;
  W(a)->ground = h;
  update_wade(a, h, hit);
}

// Texture edge fix-ups of a sprite quad (the rasteriser's last column / row).
void fix_edges(const s16 xy[8], s32* u0, s32* v0, s32* u1, s32* v1) {
  // xy: x0 y0 x1 y1 x2 y2 x3 y3
  s32 dx = xy[6] - xy[0];
  if (dx < 0) {
    dx = -dx;
    if (dx == std::abs(*u0 - *u1)) *u0 -= 1;
    *u1 -= 1;
  } else {
    const s32 dx2 = std::abs(xy[4] - xy[2]);
    const s32 du = *u1 - *u0;
    bool dec = true;
    if (dx == std::abs(du) && dx2 == std::abs(du)) dec = false;
    if (dec) *u1 -= 1;
  }
  s32 dy = xy[7] - xy[1];
  if (dy < 0 && xy[1] <= xy[3]) {
    dy = -dy;
    if (dy == std::abs(*v0 - *v1)) *v0 -= 1;
    *v1 -= 1;
  } else {
    dy = std::abs(dy);
    const s32 dy2 = std::abs(xy[5] - xy[3]);
    const s32 dv = *v1 - *v0;
    bool dec = true;
    if (dy == std::abs(dv) && dy2 == std::abs(dv)) dec = false;
    if (dec) *v1 -= 1;
  }
}

psyq::P_TAG* alloc_prim(int words) {
  auto* t = static_cast<psyq::P_TAG*>(field_screen()->alloc(sizeof(psyq::P_TAG) + static_cast<size_t>(words) * 4));
  if (t != nullptr) t->len = static_cast<u32>(words);
  return t;
}
inline u8* at(psyq::P_TAG* t, int o) { return reinterpret_cast<u8*>(psyq::prim_words(t)) + (o - 4); }
inline void put32(psyq::P_TAG* t, int o, u32 v) { std::memcpy(at(t, o), &v, 4); }
inline void put16(psyq::P_TAG* t, int o, u16 v) { std::memcpy(at(t, o), &v, 2); }
inline void put8(psyq::P_TAG* t, int o, u8 v) { *at(t, o) = v; }

// 0x80087ed4: actors drawn flat at +0x74 / +0x76 (flag 0x200).
void draw_flat(Actor* a, const u8* frame, const u8* cel) {
  const s32 sw = mul(frame[1], a->scale_x), sh = mul(frame[2], a->scale_y);
  const s32 x0 = static_cast<s16>(a->screen_x - (sw >> 12));
  const s32 x1 = static_cast<s16>(a->screen_x + (sw >> 12));
  const s32 cxm = (x1 + x0 + 1) >> 1;
  s32 off = cxm - 0xa0;
  if (off < 0) off = 0xa0 - cxm;
  if (off >= ((x1 - x0) + 0x141) >> 1) return;
  const s32 y0 = static_cast<s16>(a->screen_y + (sh >> 12));
  const s32 y1 = static_cast<s16>(a->screen_y - (sh >> 12));
  const s32 cym = (y1 + y0 + 1) >> 1;
  off = cym - 0x78;
  if (off < 0) off = 0x78 - cym;
  if (((y0 - y1) + 0xf1) >> 1 <= off) return;
  u32 flip_x = (a->flags >> 19) & 1, flip_y = (a->flags >> 20) & 1;
  if (cel[3] & 1) flip_x ^= 1;
  if (cel[3] & 2) flip_y ^= 1;
  psyq::P_TAG* p = alloc_prim(9);
  if (p == nullptr) return;
  const s32 su = (static_cast<s32>(frame[1]) << 24) / sw;
  const s32 tw = frame[1] * 0x1000, th = frame[2] << 12;
  const s32 sv = (static_cast<s32>(frame[2]) << 24) / sh;
  if (su == 0 || sv == 0) return;
  s32 u0 = 0, v0 = 0, u1 = 0, v1 = 0;
  u8* work = field_screen()->extra.get() + field_screen()->extra_cursor;
  const int slot = sprite_cache_a_alloc(actor_system(), &u0, &v0, &u1, &v1, &work, a->pixels + read_u32(frame + 8),
                                        a->palettes + frame[0] * 0x20 + 4, sw, sh, tw, th, (tw + su - 1) / su, th / sv,
                                        0, 0);
  a->cache_slot[0] = static_cast<s8>(slot);
  const u32 abr = a->blend != 0 ? a->blend - 1u : 0u;
  put16(p, 0x16, static_cast<u16>((abr & 3) << 5 | 0x105));
  a->cache_slots = 1;
  s32 left = -rd16(frame + 4), right = left + (u1 - u0);
  s32 top = -rd16(frame + 6), bottom = top + (v1 - v0);
  if (flip_x) {
    left = rd16(frame + 4) + static_cast<s8>(cel[1]);
    right = static_cast<s8>(cel[1]) - right;
  }
  if (flip_y) {
    top = rd16(frame + 6) + static_cast<s8>(cel[2]);
    bottom = static_cast<s8>(cel[2]) - bottom;
  }
  const s32 r = a->spin_angle - view().roll2;
  const s32 c = rcos(r), s = rsin(r);
  a->origin_y = a->screen_y;
  a->origin_x = a->screen_x;
  s16 xy[8];
  auto corner = [&](s32 x, s32 y, int k) {
    xy[k * 2] = static_cast<s16>(a->origin_x + static_cast<s16>((mul((x * c + y * s) >> 7, a->scale_x)) >> 17));
    xy[k * 2 + 1] = static_cast<s16>(a->origin_y + static_cast<s16>((mul((x * -s + y * c) >> 7, a->scale_y)) >> 17));
  };
  corner(left, top, 0);
  corner(right, top, 1);
  corner(left, bottom, 2);
  corner(right, bottom, 3);
  fix_edges(xy, &u0, &v0, &u1, &v1);
  for (int k = 0; k < 4; ++k) {
    put16(p, 8 + k * 8, static_cast<u16>(xy[k * 2]));
    put16(p, 0xa + k * 8, static_cast<u16>(xy[k * 2 + 1]));
  }
  put16(p, 0xc, static_cast<u16>((u0 & 0xff) | (v0 & 0xff) << 8));
  put16(p, 0x14, static_cast<u16>((u1 & 0xff) | (v0 & 0xff) << 8));
  put16(p, 0x1c, static_cast<u16>((u0 & 0xff) | (v1 & 0xff) << 8));
  put16(p, 0x24, static_cast<u16>((u1 & 0xff) | (v1 & 0xff) << 8));
  u32 col = (cel[3] & 0x80) == 0 ? 0x2d000000u : 0x2f000000u;
  if (cel[3] & 0x80) put16(p, 0x16, static_cast<u16>(read_u16(at(p, 0x16)) | (cel[3] & 0x60)));
  col |= 0x808080;
  if ((static_cast<u32>(a->rgb[0] | a->rgb[1] << 8 | a->rgb[2] << 16)) != 0x808080) col = colour_mul(col, a->rgb);
  if (WorldBg* bg = world_bg()) col = colour_mul(col, bg->light_rgb);
  ActorSystem* sys = actor_system();
  if (sys != nullptr && sys->tint_on != 0 && a->tinted != 0) col = colour_mul(col, sys->tint);
  if (a->blend != 0) col |= 0x2000000;
  put32(p, 4, col);
  const int ot = (W(a)->ot_bias + 0x83a) - W(a)->depth8 + 0x10;
  if (ot >= 0 && ot < kOtEntries) psyq::addPrim(field_screen()->ot_entry(ot), p);
}

// vtable[1] 0x80082f44.
void wa_draw(Actor* a, int arg) {
  WorldActor* w = W(a);
  if ((a->flags & 0x200) == 0 && w->ground == -0x8000) ground_snap(a);
  if (a->vehicle != 0xff) {
    // Vehicles (Hoverboard, Dragon, boats) are 3D models drawn by field 0x8006053c.
    runtime::Runtime::get().log_once("world:vehicle", "world: vehicle models (0x80082f44 / 0x8006053c) not ported yet");
    return;
  }
  a->prim_count = 0;
  if (a->flags & 0x800000) arg = 0;
  if ((a->flags & 0x80) == 0) auto_anim(a, a->walked != 0);
  if (ticks() != 0) {
    a->walked_last = a->walked;
    a->walked = 0;
  }
  actor_resolve_anim(a);
  if (a->cel_header == nullptr || a->cels == nullptr || a->frames == nullptr) return;
  const u8* cel = a->cel_header[2] == 1 ? a->cels + a->cel * 4 : a->cels + a->cel * 8;
  const u8* frame = a->frames + cel[0] * 0xc;
  if (a->flags & 0x200) {
    draw_flat(a, frame, cel);
    return;
  }
  project(a);
  // Debugging: SS_WORLD_DBG=N prints the leader's and the camera's state every N vblanks
  // (the fields the PCSX-Redux world-map script logs, for frame-by-frame comparisons).
  if (const char* dbg = std::getenv("SS_WORLD_DBG");
      dbg != nullptr && actor_slot(a) == 0 && (runtime::Runtime::get().vblanks() % std::max(2, std::atoi(dbg))) < 2) {
    const WorldView& v = view();
    std::printf("[world] v%llu actor %d pos %d %d %d screen %d %d depth8 %d eye %d %d %d target %d %d %d zoom %d\n",
                static_cast<unsigned long long>(runtime::Runtime::get().vblanks()), actor_slot(a), a->x >> 12,
                a->y >> 12, a->z >> 12, a->origin_x, a->origin_y, w->depth8, v.eye[0] >> 12,
                v.eye[1] >> 12, v.eye[2] >> 12, v.target[0] >> 12, v.target[1] >> 12, v.target[2] >> 12, v.zoom);
    if (const WorldBg* bg = world_bg())
      std::printf("[world] v%llu a0 ang %d spd %d walk %d walked %d | bg yaw %d pitch %d turn %d brake %d tspd %d ticks %d\n",
                  static_cast<unsigned long long>(runtime::Runtime::get().vblanks()), static_cast<u16>(a->angle), a->speed,
                  a->walk_speed, a->walked, bg->yaw, bg->pitch, bg->turn, bg->turn_brake, bg->turn_speed, ticks());
  }
  if (w->depth8 < 9) return;
  if (static_cast<u16>(a->origin_x + 0x140) > 0x3c0) return;
  if (static_cast<u16>(a->origin_y + 0xf0) > 0x2d0) return;
  s32 ofs_x = 0, ofs_y = 0, sx, sy;
  if ((a->flags & 0x20000) == 0) {
    sx = w->sprite_scale_xz[0];
    sy = w->sprite_scale_xz[1];
    ofs_x = view().geom_ofs_x;
    ofs_y = view().geom_ofs_y;
  } else {
    sy = 0x40000 / w->depth8;
    sx = (sy * w->sprite_scale_xz[0]) >> 10;
    sy = (sy * w->sprite_scale_xz[1]) >> 10;
  }
  sy = mul(sy, view().zoom) >> 12;
  sx = mul(sx, view().zoom) >> 12;
  const s32 fw = frame[1], fh = frame[2];
  s32 tw = fw * 0x1000;
  s32 th = fh << 12;
  s32 d0, d1;
  if ((a->flags & 0x20000) == 0) {
    d0 = fw * sx;
    d1 = fh * sy;
  } else if ((a->flags & 0x100) == 0) {
    d0 = fw;
    d1 = fh;
  } else {
    d0 = fw * a->scale_x;
    d1 = fh * a->scale_y;
  }
  if (d0 == 0) d0 = 0x1000;
  if (d1 == 0) d1 = 0x1000;
  if (d0 > 0x3f000) {
    if ((a->flags & 0x20) == 0) {
      arg = 0;
    } else {
      d0 = 0x3f000;
      d1 = (fh * 0x3f000) / fw;
    }
  }
  const s32 step_u = (fw << 24) / d0;
  const s32 step_v = (fh << 24) / d1;
  if (step_u == 0 || step_v == 0) return;
  const s32 screen_w = static_cast<s32>(static_cast<u32>(tw + step_u - 1) / static_cast<u32>(step_u));
  const s32 screen_h = static_cast<s32>(static_cast<u32>(th) / static_cast<u32>(step_v));
  psyq::P_TAG* p = alloc_prim(9);
  if (p == nullptr) return;
  s32 left = 0, top = 0, u0 = 0, v0 = 0, u1 = 0, v1 = 0;
  const u8* pixels = a->pixels + read_u32(frame + 8);
  const u8* palette = a->palettes + a->frames[0] * 0x20 + 4;
  ActorSystem* sys = actor_system();
  if (arg != 0) {
    left = -(rd16(frame + 4) * d0) / (fw << 12);
    top = -(rd16(frame + 6) * d1) / (fh << 12);
    u8* work = field_screen()->extra.get() + field_screen()->extra_cursor;
    const int slot = sprite_cache_a_alloc(sys, &u0, &v0, &u1, &v1, &work, pixels, palette, d0, d1, tw, th, screen_w,
                                          screen_h, 0, 0);
    a->cache_slot[0] = static_cast<s8>(slot);
    if (slot == -1) {
      arg = 0;
    } else {
      const u32 abr = a->blend != 0 ? a->blend - 1u : 0u;
      put16(p, 0x16, static_cast<u16>((abr & 3) << 5 | 0x105));
    }
  }
  if (arg == 0) {
    u32 uv = 0;
    u16 clut = 0;
    const int slot = sprite_cache_b_alloc(sys, &uv, read_u16(a->palettes) >> 15, &clut, pixels,
                                          const_cast<u8*>(palette), fw, fh, a->blend);
    put16(p, 0xe, clut);
    a->cache_slot[0] = static_cast<s8>(slot + 0x20);
    u0 = static_cast<s32>((uv & 0xff) << 2);
    v0 = static_cast<s32>(uv >> 8 & 0xff);
    u1 = static_cast<s32>((uv >> 16 & 0xff) << 2);
    v1 = static_cast<s32>(uv >> 24);
    const u32 abr = a->blend != 0 ? a->blend - 1u : 0u;
    put16(p, 0x16, static_cast<u16>((abr & 3) << 5 | 0x15));
  }
  s32 clip = 0;
  if (w->wade != 0) {
    s32 t = (v1 - v0) * (0x80 - w->wade);
    if (t < 0) t += 0x7f;
    const s32 nv1 = v0 + (t >> 7);
    clip = v1 - nv1;
    v1 = nv1;
  }
  a->cache_slots = 1;
  s32 right, bottom;
  if (arg != 0) {
    right = left + (u1 - u0);
    bottom = top + (v1 - v0);
  } else {
    left = -rd16(frame + 4);
    right = left + (u1 - u0);
    top = -rd16(frame + 6);
    bottom = top + (v1 - v0);
  }
  u32 flip_x = (a->flags >> 19) & 1, flip_y = (a->flags >> 20) & 1;
  if (a->anim < 0x24) {
    const u32 d = a->auto_anim % 12;
    if (d != 0 && d < 6) flip_x ^= 1;
  }
  if (cel[3] & 1) flip_x ^= 1;
  if (cel[3] & 2) flip_y ^= 1;
  if (flip_x) {
    left = static_cast<s8>(cel[1]) - left;
    right = static_cast<s8>(cel[1]) - right;
  }
  if (flip_y) {
    top = static_cast<s8>(cel[2]) - top;
    bottom = static_cast<s8>(cel[2]) - bottom;
  }
  top += clip;
  bottom += clip;
  u32 col;
  if ((cel[3] & 4) == 0) {
    col = (cel[3] & 0x80) == 0 ? 0x2d000000u : 0x2f000000u;
    col |= 0x808080;
  } else {
    col = (cel[3] & 0x80) == 0 ? 0x2c000000u : 0x2e000000u;
    if (sys != nullptr) col |= static_cast<u32>(sys->light[0] | sys->light[1] << 8 | sys->light[2] << 16);
  }
  if (cel[3] & 0x80) put16(p, 0x16, static_cast<u16>(read_u16(at(p, 0x16)) | (cel[3] & 0x60)));
  if ((static_cast<u32>(a->rgb[0] | a->rgb[1] << 8 | a->rgb[2] << 16)) != 0x808080) col = colour_mul(col, a->rgb);
  if (WorldBg* bg = world_bg()) col = colour_mul(col, bg->light_rgb);
  if (sys != nullptr && sys->tint_on != 0 && a->tinted != 0) col = colour_mul(col, sys->tint);
  if (a->blend != 0) {
    col |= 0x2000000;
    put16(p, 0x16, static_cast<u16>(read_u16(at(p, 0x16)) | (a->blend - 1) << 5));
  }
  w->sprite_scale_draw[0] = sx;
  w->sprite_scale_draw[1] = sy;
  if (!sflag(1, 4) && read_u16(fg().game_state + 0x1a) == 1) {
    const s32 dx = (a->x >> 12) - 0xe1bb, dz = (a->z >> 12) - 0x9e4e;
    const s32 d = world_sqrt(dx * dx + dz * dz);
    if (d < 0x15e) w->ot_bias = static_cast<s8>(-(((0x15e - d) * 0x1b) / 0x15e));
  }
  s16 xy[8];
  xy[0] = static_cast<s16>(a->origin_x + (mul(left, sx) >> 12) + ofs_x);
  xy[1] = static_cast<s16>(a->origin_y + (mul(top, sy) >> 12) + ofs_y);
  xy[2] = static_cast<s16>(a->origin_x + (mul(right, sx) >> 12) + ofs_x);
  xy[3] = xy[1];
  xy[4] = xy[0];
  xy[5] = static_cast<s16>(a->origin_y + (mul(bottom, sy) >> 12) + ofs_y);
  xy[6] = xy[2];
  xy[7] = xy[5];
  fix_edges(xy, &u0, &v0, &u1, &v1);
  a->quad[1] = xy[3];
  a->quad[3] = xy[5];
  for (int k = 0; k < 4; ++k) {
    put16(p, 8 + k * 8, static_cast<u16>(xy[k * 2]));
    put16(p, 0xa + k * 8, static_cast<u16>(xy[k * 2 + 1]));
  }
  put8(p, 0xc, static_cast<u8>(u0));
  put8(p, 0xd, static_cast<u8>(v0));
  put8(p, 0x14, static_cast<u8>(u1));
  put8(p, 0x15, static_cast<u8>(v0));
  put8(p, 0x1c, static_cast<u8>(u0));
  put8(p, 0x1d, static_cast<u8>(v1));
  put8(p, 0x24, static_cast<u8>(u1));
  put8(p, 0x25, static_cast<u8>(v1));
  put32(p, 4, col);
  const u32 ot = static_cast<u32>((w->ot_bias + 0x83a) - w->depth8);
  if (ot >= 0x821) return;
  psyq::addPrim(field_screen()->ot_entry(static_cast<int>(ot)), p);
  if (w->wade != 0) return;
  // The shadow: a 3 x 3 grid on the ground plane of the floor polygon, bent with the
  // world, as four quarters of the blob shadow texture (#3110 icons).
  const s32 r = a->model == 199 ? 0x5a000 : (static_cast<u32>(a->auto_anim - 0x18) < 0xc ? 0x37000 : 0x32000);
  const s32 diag = (r * 0x23 * 4 + r) >> 12;
  const s32 base_y = a->y >> 12;
  const s32 bx = (a->x / 0x3000000) * 0x3000000, bz = (a->z / 0x3000000) * 0x3000000;
  s16 pts[9][4]{};
  int k = 0;
  for (s32 dz = -r, row = -1; row < 2; ++row, dz += r) {
    for (s32 dx = -r, col2 = -1; col2 < 2; ++col2, dx += r, ++k) {
      s32 ix = dx >> 12, iz = dz >> 12, iy;
      if (w->normal[1] == 0) {
        iy = a->y >> 12;
      } else {
        iy = (-w->normal[0] * ix - w->normal[2] * iz) / w->normal[1];
      }
      ld_ir(ix, iy, iz);
      run(G::kSqr0);
      const s32 len = world_sqrt(mac(1) + mac(2) + mac(3));
      if (len != 0) {
        if (ix == 0 || iz == 0) {
          const s32 kk = r >> 12;
          ix = (ix * kk) / len;
          iy = (iy * kk) / len;
          iz = (iz * kk) / len;
        } else {
          const s32 kk = len * 100;
          ix = (ix * diag) / kk;
          iy = (iy * diag) / kk;
          iz = (iz * diag) / kk;
        }
      }
      pts[k][0] = static_cast<s16>((a->x + ix * 0x1000 - bx) >> 12);
      pts[k][1] = static_cast<s16>(iy + base_y);
      pts[k][2] = static_cast<s16>((a->z + iz * 0x1000 - bz) >> 12);
    }
  }
  u32 sxy[9];
  s32 flags[9];
  world_bend_project(pts, 9, sxy, flags);
  // g_ui_atlas (0x80075328) +0x162..+0x165: the shadow's UV rectangle.
  const u8* sys_icons = fg().ui_atlas;
  if (sys_icons == nullptr) return;
  const u8 su0 = sys_icons[0x162], sv0 = sys_icons[0x163], su1 = sys_icons[0x164], sv1 = sys_icons[0x165];
  u32 scol = 0x2f000000u;
  if ((static_cast<u32>(a->rgb[0] | a->rgb[1] << 8 | a->rgb[2] << 16)) != 0x808080) {
    scol = 0x2e000000u | static_cast<u32>(a->rgb[0] | a->rgb[1] << 8 | a->rgb[2] << 16);
  }
  const s16 yoff = static_cast<s16>(ofs_y + 1);
  for (int q = 0; q < 4; ++q) {
    psyq::P_TAG* s = alloc_prim(9);
    if (s == nullptr) return;
    const int bit = q & 1, rowq = q >> 1;
    const s32 hu = (static_cast<s32>(su1) - su0) / 2, hv = (static_cast<s32>(sv1) - sv0) / 2;
    const u8 ua = static_cast<u8>(su0 + hu * bit), ub = static_cast<u8>(su0 + hu * bit + (hu - bit));
    const u8 va = static_cast<u8>(sv0 + hv * rowq), vb = static_cast<u8>(sv0 + hv * rowq + (hv - rowq));
    put8(s, 0xc, ua);
    put8(s, 0xd, va);
    put8(s, 0x14, ub);
    put8(s, 0x15, va);
    put8(s, 0x1c, ua);
    put8(s, 0x1d, vb);
    put8(s, 0x24, ub);
    put8(s, 0x25, vb);
    put16(s, 0x16, 0x56);
    put16(s, 0xe, 0x7fcd);
    const int i0 = q + q / 2;
    const int idx[4] = {i0, i0 + 1, i0 + 3, i0 + 4};
    for (int c = 0; c < 4; ++c) {
      put16(s, 8 + c * 8, static_cast<u16>(static_cast<s16>(sxy[idx[c]]) + ofs_x));
      put16(s, 0xa + c * 8, static_cast<u16>(static_cast<s16>(sxy[idx[c]] >> 16) + yoff));
    }
    put32(s, 4, scol);
    psyq::addPrim(field_screen()->ot_entry(static_cast<int>(ot)), s);
  }
}

// ---- Walking ----

struct Probe {
  s32 h;
  TerrainHit hit;
};

// 0x80087ae0: the smallest turn (searched in 7 halvings from 90 degrees, either way)
// that keeps the point 'dist' ahead at least 0x3c from the given blocked polygons.
s32 avoid_polygons(Actor* a, const Probe* hits, int count, s32 dist) {
  s32 result[2];
  for (int pass = 0; pass < 2; ++pass) {
    s32 last_bad = 0;
    s32 good = pass == 0 ? 0x400 : -0x400;
    s32 cur = good;
    for (int it = 0; it < 7; ++it) {
      const s32 ang = a->angle + cur;
      const s32 px = ((rcos(ang) * dist + a->x) >> 12) - 0x1800;
      const s32 pz = ((rsin(ang) * dist + a->z) >> 12) - 0x1800;
      int i = 0;
      for (; i < count; ++i) {
        const TerrainHit& h = hits[i].hit;
        const s32 rx = px - h.base_x, rz = pz - h.base_z;
        int k = 0;
        for (; k < h.corners; ++k) {
          const u32 xz = ru32(h.v[k] + 4);
          const s32 dx = rx - (static_cast<s32>(xz) >> 16), dz = rz - static_cast<s16>(xz);
          if (world_sqrt(dx * dx + dz * dz) < 0x3c) break;
        }
        if (k != h.corners) break;
        int e = 0;
        for (; e < h.corners; ++e) {
          const u32 p0 = ru32(h.v[e] + 4), p1 = ru32(h.v[(e + 1) % h.corners] + 4);
          const s32 x0 = static_cast<s32>(p0) >> 16, z0 = static_cast<s16>(p0);
          const s32 x1 = static_cast<s32>(p1) >> 16, z1 = static_cast<s16>(p1);
          if (x0 == x1 && z0 == z1) continue;
          const s32 ex = x1 - x0, ez = z1 - z0;
          const u32 t = static_cast<u32>((((rx - x0) * ex + (rz - z0) * ez) * 0x1000) / (ex * ex + ez * ez));
          if (t < 0x1001) {
            s32 fx = static_cast<s32>(ex * static_cast<s32>(t));
            s32 fz = static_cast<s32>(ez * static_cast<s32>(t));
            if (fx < 0) fx += 0xfff;
            if (fz < 0) fz += 0xfff;
            const s32 qx = ((fx >> 12) + x0) - rx, qz = ((fz >> 12) + z0) - rz;
            if (world_sqrt(qx * qx + qz * qz) < 0x3c) break;
          }
        }
        if (e != h.corners) break;
      }
      s32 next_good = cur;
      if (i != count) {
        next_good = good;
        last_bad = cur;
        if (std::abs(cur) == 0x400) break;
      }
      cur = (last_bad + next_good) / 2;
      good = next_good;
    }
    result[pass] = good;
  }
  if (result[0] == -result[1]) return result[0] != 0x400 ? result[0] : 0;
  return result[0] < -result[1] ? result[0] : result[1];
}

// 0x800875b0: the smallest turn (7 halvings from 90 degrees, either way) after which the
// point 'dist' ahead lies inside none of the blocked polygons.
s32 slide_turn(Actor* a, const Probe* hits, int count, s32 dist) {
  s32 result[2];
  for (int pass = 0; pass < 2; ++pass) {
    s32 last_bad = 0;
    s32 good = pass == 0 ? 0x400 : -0x400;
    s32 cur = good;
    for (int it = 0; it < 7; ++it) {
      const s32 ang = a->angle + cur;
      const s32 px = ((rcos(ang) * dist + a->x) >> 12) - 0x1800;
      const s32 pz = ((rsin(ang) * dist + a->z) >> 12) - 0x1800;
      int i = 0;
      for (; i < count; ++i) {
        const TerrainHit& h = hits[i].hit;
        const s16 rx = static_cast<s16>(px - h.base_x), rz = static_cast<s16>(pz - h.base_z);
        bool inside = true;
        const int n = h.corners == 3 ? 3 : 4;
        // Edges in winding order (a b c) or (a b d c) as the polygon stores them.
        const int order3[3] = {0, 1, 2}, order4[4] = {0, 1, 2, 3};
        const int* ord = n == 3 ? order3 : order4;
        for (int e = 0; e < n && inside; ++e) {
          const u32 p0 = ru32(h.v[ord[e]] + 4), p1 = ru32(h.v[ord[(e + 1) % n]] + 4);
          const s16 x0 = static_cast<s16>(p0 >> 16), z0 = static_cast<s16>(p0);
          const s16 x1 = static_cast<s16>(p1 >> 16), z1 = static_cast<s16>(p1);
          s32 out[3];
          G& g = gte();
          g.write_ctrl(G::R11R12, static_cast<u16>(static_cast<s16>(x1 - x0)));
          g.write_ctrl(G::R22R23, 0);
          g.write_ctrl(G::R33, static_cast<u32>(static_cast<s32>(static_cast<s16>(z1 - z0))));
          ld_ir(static_cast<s16>(rx - x1), 0, static_cast<s16>(rz - z1));
          run(G::kOp0);
          out[1] = mac(2);
          if (out[1] < 0) inside = false;
        }
        if (inside) break;
      }
      s32 next_good = cur;
      if (i != count) {
        next_good = good;
        last_bad = cur;
        if (std::abs(cur) == 0x400) break;
      }
      cur = (last_bad + next_good) / 2;
      good = next_good;
    }
    result[pass] = good;
  }
  if (result[0] == -result[1]) return result[0] != 0x400 ? result[0] : 0;
  return result[0] < -result[1] ? result[0] : result[1];
}

bool probe_blocked(Probe& p, const Actor*, s32 x, s32 z) {
  p.h = world_terrain_query(world_bg(), x, z, &p.hit);
  return (p.h != -0x8000 && (p.hit.attr & 1) == 0) || p.hit.off_map != 0;
}

// The encounter group of the leader's 0x1000 square (field tables 0x80074ef0 map 1 /
// 0x80074f70 map 2: 7 rows x 9 blocks; negative = a 3 x 3 sub-table).
s32 encounter_group(const Actor* a, int terrain) {
  const int map = read_u16(fg().game_state + 0x1a);
  const s32 bx = a->x / 0x3000000, bz = a->z / 0x3000000;
  s32 lx = (a->x >> 12) % 0x3000, lz = (a->z >> 12) % 0x3000;
  if (lx < 0) lx += 0xfff;
  if (lz < 0) lz += 0xfff;
  const u32 table = map == 1 ? 0x80074ef0u : 0x80074f70u;
  const u32 subs = map == 1 ? 0x80074f5cu : 0x80074fd0u;
  const u8* t = field_data(table + static_cast<u32>((6 - bz) * 9 + bx));
  s32 g = t != nullptr ? static_cast<s8>(*t) : 0;
  if (g < 0) {
    const u8* pp = field_data(subs + static_cast<u32>(-g * 4));
    if (pp != nullptr) {
      const u8* sub = field_data(read_u32(pp) + static_cast<u32>((2 - (lz >> 12)) * 3 + (lx >> 12)));
      g = sub != nullptr ? static_cast<s8>(*sub) : 0;
    }
  }
  if (terrain == 5) return g + 1;
  if (map != 1 && terrain > 9 && terrain < 13) return g + 2;
  return g;
}

// 0x80086488: walking on foot. Pad (or stick) direction relative to the camera, probes
// 90 units ahead (and +-34 degrees on the first try) for unwalkable ground and turns
// along it, wraps the torus, keeps the feet on walkable polygons, accumulates the
// encounter distance and picks the encounter settings of the ground under the leader.
void walk(Actor* a, int camera, s32 mag) {
  FieldGlobals& g = fg();
  WorldGlobals& w = world();
  WorldActor* wa = W(a);
  const FieldAnalog& an = field_analog();
  if (a->analog == 0 || mag == 0) {
    if (!sflag(1, 6)) {
      if ((g.held[0] & read_u16(g.config + 2)) == 0 || sflag(1, 2)) {
        a->speed = a->walk_speed << 1;
        a->flags &= 0xffbfffffu;
      } else {
        a->speed = a->walk_speed * 6;
        a->flags |= 0x400000;
      }
    } else {
      a->speed = a->walk_speed * 0xc;
    }
  } else {
    a->speed = (mag * a->walk_speed * 6) / 0x50;
    if (mag < 0x28 || sflag(1, 2)) {
      a->flags &= 0xffbfffffu;
    } else {
      a->flags |= 0x400000;
    }
    if (sflag(1, 6)) a->speed <<= 1;
  }
  int moving = 0;
  const s32 yaw = world_camera_yaw();
  const s32 saved[4] = {a->x, a->y, a->z, a->w};
  s32 turn_in = 0;
  if (a->analog == 0) {
    const int dir = field_pad_direction();
    if (dir != -1) {
      turn_in = dir * 0x200;
      moving = 1;
    }
  } else if (mag != 0) {
    turn_in = world_ratan2(an.y, an.x) + 0x400;
    moving = 1;
  }
  if (moving) a->angle = static_cast<s16>(yaw - turn_in);
  const s16 wanted = a->angle;
  if (moving) {
    Probe hits[16];
    int n = 0;
    s32 off = 0;
    s16 final_angle = wanted;
    bool go = true;
    int attempt = 0;
    for (;;) {
      if (attempt > 3) break;
      const s32 ang = a->angle + off;
      final_angle = static_cast<s16>(ang);
      bool blocked = false;
      if (n < 16 && probe_blocked(hits[n], a, a->x + rcos(ang) * 0x5a, a->z + rsin(ang) * 0x5a)) {
        blocked = true;
        ++n;
      }
      if (attempt == 0) {
        const s32 side = world_ratan2(0x3c, 0x5a);
        if (n < 16 && probe_blocked(hits[n], a, a->x + rcos(ang + side) * 0x5a, a->z + rsin(ang + side) * 0x5a)) {
          blocked = true;
          ++n;
        }
        if (n < 16 && probe_blocked(hits[n], a, a->x + rcos(ang - side) * 0x5a, a->z + rsin(ang - side) * 0x5a)) {
          blocked = true;
          ++n;
        }
      }
      if (!blocked) {
        const s32 adj = avoid_polygons(a, hits, n, 0x5a);
        if (adj != 0) {
          final_angle = static_cast<s16>(a->angle + adj);
          goto apply;
        }
        go = false;
        break;
      }
      // The same polygon twice in a row: count it once.
      if (n >= 2) {
        const TerrainHit& h1 = hits[n - 1].hit;
        const TerrainHit& h0 = hits[n - 2].hit;
        if (h1.corners == h0.corners && h1.v[0] == h0.v[0] && h1.v[1] == h0.v[1] && h1.v[2] == h0.v[2] &&
            h1.corners == 4 && h1.v[3] == h0.v[3])
          --n;
      }
      off = slide_turn(a, hits, n, 0x5a);
      ++attempt;
      if (off == 0) {
        go = false;
        break;
      }
    }
    if (!go) {
      final_angle = wanted;
      moving = 0;
    }
  apply:
    a->angle = final_angle;
    if (moving) {
      s32 v = mul(rcos(a->angle), a->speed);
      if (v < 0) v += 0x3ff;
      v = (v >> 10) * ticks();
      if (v < 0) v += 0xfff;
      a->x += (v >> 12) * 0x400;
      v = mul(rsin(a->angle), a->speed);
      if (v < 0) v += 0x3ff;
      v = (v >> 10) * ticks();
      if (v < 0) v += 0xfff;
      a->z += (v >> 12) * 0x400;
    }
  }
  // The torus.
  if (a->x < 0) {
    a->x += 0x1b000000;
  } else if (a->x > 0x1affffff) {
    a->x -= 0x1b000000;
  }
  if (a->z < 0) {
    a->z += 0x15000000;
  } else if (a->z > 0x14ffffff) {
    a->z -= 0x15000000;
  }
  // Both shoulders (45 degrees either side, 0x3c out) must stand on walkable ground.
  TerrainHit hit;
  bool revert = false;
  {
    s32 h = world_terrain_query(world_bg(), a->x + rcos(a->angle + 0x200) * 0x3c, a->z + rsin(a->angle + 0x200) * 0x3c,
                                &hit);
    if ((h == -0x8000 || (hit.attr & 1) != 0) && hit.off_map == 0) {
      h = world_terrain_query(world_bg(), a->x + rcos(a->angle - 0x200) * 0x3c,
                              a->z + rsin(a->angle - 0x200) * 0x3c, &hit);
      if ((h != -0x8000 && (hit.attr & 1) == 0) || hit.off_map != 0) revert = true;
    } else {
      revert = true;
    }
  }
  if (revert) {
    a->x = saved[0];
    a->y = saved[1];
    a->z = saved[2];
    a->w = saved[3];
    a->angle = wanted;
    moving = 0;
  }
  const s32 h = world_terrain_query(world_bg(), a->x, a->z, &hit);
  if (h != -0x8000) wa->ground = h;
  // The camera roll settles back to 0 (8 per frame).
  {
    s16& roll = view().roll;
    s16 next = 0;
    if (roll != 0) next = roll < 1 ? static_cast<s16>(roll + 8) : static_cast<s16>(roll - 8);
    const bool crossed = roll != 0 && next != 0 && ((roll > 0) != (next > 0));
    roll = crossed || next == 0 ? 0 : next;
  }
  w.leader_hit = hit;
  bool settle = h != -0x8000;
  if (settle) {
    update_wade(a, h, hit);
    a->y = h << 12;
    if ((hit.attr & 1) == 0) {
      a->x = saved[0];
      a->y = saved[1];
      a->z = saved[2];
      a->w = saved[3];
      a->angle = wanted;
      moving = 0;
      settle = false;
    }
  }
  if (settle) {
    if (moving) {
      a->walked = 1;
      s32 dist;
      std::memcpy(&dist, g.field_flags + 0x18, 4);
      dist += mul(a->speed, ticks());
      std::memcpy(g.field_flags + 0x18, &dist, 4);
    }
    for (int i = 0; i < 4; ++i) wa->normal[i] = w.tri_normal[i];
    const int terrain = (hit.attr >> 4) & 0xf;
    const s32 group = encounter_group(a, terrain);
    const s16 cur_group = static_cast<s16>(read_u16(g.field_flags + 0x14));
    if (!(terrain == w.last_terrain && cur_group == group && w.in_region == w.in_region_prev)) {
      w.in_region_prev = w.in_region;
      w.last_terrain = terrain;
      s16 stage;
      u8 kind;
      const u8 reg = w.in_region;
      switch (terrain) {
        case 1:
        case 6:
          stage = reg == 0 ? 5 : 0x14;
          kind = 5;
          break;
        case 2:
        case 7:
          if (reg == 0) {
            stage = (rng_next() & 1) != 0 ? 2 : 4;
          } else {
            stage = (rng_next() & 1) != 0 ? 0x11 : 0x12;
          }
          kind = 1;
          break;
        case 3:
          stage = reg == 0 ? 0xd : (reg == 1 ? 0x38 : 0x39);
          kind = 6;
          break;
        case 4:
          stage = 6;
          kind = 10;
          break;
        case 5:
        case 8:
          stage = reg == 0 ? 9 : 0x13;
          kind = 2;
          break;
        case 9:
          stage = 0x13;
          kind = 1;
          break;
        case 10:
          stage = 0x15;
          kind = 0xb;
          break;
        case 11:
          stage = 0x28;
          kind = 0xb;
          break;
        case 12:
          stage = 0x29;
          kind = 0xb;
          break;
        default:
          stage = 2;
          kind = 1;
          break;
      }
      g.field_flags[0x31] = kind;
      g.battle_preloaded = 0;
      g.battle_preload_requested = 0;
      std::memcpy(g.field_flags + 0x12, &stage, 2);
      const s16 grp = static_cast<s16>(group);
      std::memcpy(g.field_flags + 0x14, &grp, 2);
      preload_release(1);
      runtime::Runtime::get().trace(("world: terrain " + std::to_string(terrain) + " group " + std::to_string(group) +
                                     " stage " + std::to_string(stage))
                                        .c_str());
      if (read_u16(g.game_state + 0x1a) == 1) {
        if (static_cast<u32>(group - 0x20) < 9) {
          g.field_flags[0x30] = 1;
        } else if (static_cast<u32>(group - 0x28) < 5) {
          g.field_flags[0x30] = 2;
        } else if (static_cast<u32>(group - 0x2c) < 2) {
          g.field_flags[0x30] = 3;
        } else if (static_cast<u32>(group - 0x2e) < 2) {
          g.field_flags[0x30] = 4;
        } else if (group == 0x30) {
          g.field_flags[0x30] = 9;
        }
      }
    }
  }
  // The camera: turn with the stick / pad direction relative to the view.
  if (camera != 0) {
    s32 turn = 0, pitch_in = 0;
    u32 dir = static_cast<u32>(turn_in) & 0xfff;
    if (a->walked == 0) {
      a->speed = 0;
    } else {
      if (dir - 0x180 < 0xd01) {
        if (dir - 0x601 < 0x3ff) {
          const u32 base = 0x800;
          const u32 rel = dir < 0x801 ? base - dir : dir - 0x800;
          pitch_in = static_cast<s32>((0x200 - rel) * 2);
        }
      } else {
        const u32 rel = dir > 0x7ff ? 0x1000 - dir : dir;
        pitch_in = static_cast<s32>((0x200 - rel) * 2);
      }
      if (dir < 0x800) {
        if (dir < 0x500) {
          turn = (static_cast<s32>(dir) * -0xa00) / 0x500;
        } else {
          turn = (static_cast<s32>(0x800 - dir) * 0x800) / 0x300;
        }
      } else if (dir - 0xb01 > 0x4fe) {
        turn = (static_cast<s32>(dir - 0x800) * -0x800) / 0x300;
      } else {
        turn = static_cast<s32>(0x1000 - dir) * 2;
      }
    }
    if (WorldBg* bg = world_bg()) {
      const s32 div = a->walk_speed << 3;
      world_camera_follow(bg, a, div != 0 ? (turn * a->speed) / div : 0, div != 0 ? (pitch_in * a->speed) / div : 0,
                          0);
    }
  }
  if (moving == 0 && sflag(1, 6)) a->walked = 1;
}

// vtable[2] 0x80085868.
void wa_control(Actor* a, int camera) {
  if (ticks() == 0) return;
  a->walked = 0;
  const FieldAnalog& an = field_analog();
  a->analog = an.enabled;
  if (an.enabled != 0 && an.x == 0 && an.y == 0) a->analog = 0;
  s32 mag = std::abs(an.x);
  const s32 my = std::abs(an.y);
  if (mag < my) mag = my;
  if (!sflag(1, 4)) {
    walk(a, camera, mag);
  } else {
    runtime::Runtime::get().log_once("world:vehicle-control", "world: vehicle control (0x80085944) not ported yet");
  }
  actor_anim_advance(a);
}

// vtable[3] 0x80084da8: an actor the camera follows without controlling it.
void wa_follow(Actor* a) {
  const s16 controlled = static_cast<s16>(read_u16(fg().field_flags + 0x10));
  if (controlled != actor_slot(a)) {
    if (WorldBg* bg = world_bg()) world_camera_follow(bg, a, 0, 0, 0);
  }
}

// vtable[4] 0x80084e04: one frame of a scripted move (+0x27: 1 xyz, 2 xz on the ground;
// screen-space actors interpolate +0x74 / +0x76).
void wa_move_step(Actor* a) {
  if (ticks() == 0) return;
  WorldActor* w = W(a);
  const u8 mode = a->move_mode;
  if ((a->flags & 0x20000) == 0) {
    if (mode >= 3 || mode == 0) return;
    s32 step = mul(a->speed, ticks());
    s32 dx = a->target[0] - a->x;
    if (dx < 0) dx += 0x7f;
    dx >>= 7;
    s32 dz = a->target[2] - a->z;
    if (dz < 0) dz += 0x7f;
    dz >>= 7;
    const s32 heading = world_ratan2(dz, dx);
    s32 dy = a->target[1] - a->y;
    if (dy < 0) dy += 0x7f;
    dy >>= 7;
    s32 dist;
    s32 vy3 = 0;
    if (a->move_mode == 1) {
      dist = dx * dx + dy * dy + dz * dz;
      vy3 = dy;
    } else {
      dist = dx * dx + dz * dz;
    }
    dist = world_sqrt(dist);
    if (a->move_mode == 2 && (a->flags & 0x200000) != 0) {
      s32 side = 0;
      for (int k = 0; k < 3;) {
        side = -side;
        if (k == 1) side = world_ratan2(0x3c, 0x5a);
        Probe p;
        if (probe_blocked(p, a, a->x + rcos(heading + side) * 0x5a, a->z + rsin(heading + side) * 0x5a)) {
          a->move_mode = 0;
          return;
        }
        ++k;
      }
    }
    s32 s8 = step;
    if (s8 < 0) s8 += 0xff;
    if (dist <= (s8 >> 8)) a->move_mode = 0;
    s32 mx, my = 0, mz;
    if (vy3 == 0) {
      s32 st = step;
      if (st < 0) st += 0xff;
      mx = rcos(heading) * (st >> 8);
      if (mx < 0) mx += 0xf;
      mx >>= 4;
      mz = rsin(heading) * (st >> 8);
      if (mz < 0) mz += 0xf;
      mz >>= 4;
    } else {
      s32 v[3] = {dx >> 8, vy3 >> 8, dz >> 8};
      ld_ir(v[0], v[1], v[2]);
      run(G::kSqr0);
      s32 len = world_sqrt(mac(1) + mac(2) + mac(3));
      if (len == 0) {
        len = world_sqrt(dx * dx + vy3 * vy3 + dz * dz);
        mx = len != 0 ? (dx * step) / len : 0;
        my = len != 0 ? (vy3 * step) / len : 0;
        mz = len != 0 ? (dz * step) / len : 0;
      } else {
        s32 st = step;
        if (st < 0) st += 0xff;
        st >>= 8;
        mx = (dx * st) / len;
        my = (vy3 * st) / len;
        mz = (dz * st) / len;
      }
    }
    if (a->move_mode != 0 || mx != 0 || mz != 0 || my != 0) {
      if (a->move_mode == 0) {
        if (w->face_heading != 0) a->angle = a->heading;
        a->x = a->target[0];
        a->z = a->target[2];
        if (mode == 1) a->y = a->target[1];
      } else {
        a->x += mx;
        a->y += my;
        a->z += mz;
        if (w->face_heading != 0) a->angle = a->heading;
      }
      a->walked = a->move_mode;
      if (a->wait_state == 3) a->walked = 1;
    } else {
      a->walked = 0;
    }
    if (mode == 2) {
      ground_snap(a);
      a->y = w->ground << 12;
    }
  } else {
    s16 left = static_cast<s16>(a->move_left - ticks());
    a->move_left = left;
    if (left < 1) {
      a->move_mode = 0;
      a->screen_x = static_cast<s16>(a->target[0]);
      a->screen_y = static_cast<s16>(a->target[1]);
    } else {
      a->screen_x = static_cast<s16>(a->vel_x + ((a->target[0] - a->vel_x) * (a->move_frames - left)) / a->move_frames);
      a->screen_y = static_cast<s16>(a->vel_z + ((a->target[1] - a->vel_z) * (a->move_frames - left)) / a->move_frames);
    }
  }
  if (a->walked == 0) {
    a->still_time = static_cast<s16>(a->still_time + ticks());
  } else {
    a->still_time = 0;
  }
  // Patrols (flag 0x2000000): reverse after 120 blocked frames.
  if ((a->flags & 0x2000000) != 0 && a->still_time > 0x78 && static_cast<u8>(a->move_mode - 1) < 2) {
    const s32 ex = (a->x - a->start[0]) >> 12, ey = (a->y - a->start[1]) >> 12, ez = (a->z - a->start[2]) >> 12;
    if ((a->collide_radius << 1) / 3 < world_sqrt(ex * ex + ey * ey + ez * ez)) {
      a->shuttle = a->shuttle == 0 ? a->move_mode : 0;
      a->still_time = 0;
      for (int i = 0; i < 4; ++i) std::swap(a->target[i], a->start[i]);
      a->heading = static_cast<s16>(world_ratan2(a->target[2] - a->z, a->target[0] - a->x));
      actor_save_anim(a);
      const int delay = actor_walk_frame_delay(a);
      actor_turn_start(a, a->heading, static_cast<u16>((delay * 0x1000) / 360), 0);
      a->turning = 1;
      if ((a->flags & 0x4000000) != 0) {
        ++a->laps;
        if (a->lap_limit <= a->laps && a->emote == nullptr) {
          actor_create_attachment(a);
          actor_cmd_set_anim(a->emote, a->lap_emote);
          a->emote_timer = 300;
          actor_mark_dirty(a);
          a->laps = 0;
        }
      }
    }
  }
}

}  // namespace

const ActorVtable kWorldActorVtable = {wa_draw, wa_control, wa_follow, wa_move_step};

bool is_world_actor(const Actor* a) { return a != nullptr && a->vtbl == &kWorldActorVtable; }

Actor* worldactor_new() { return new WorldActor; }

// 0x80082d58: the class's own members.
void worldactor_defaults(WorldActor* w) {
  w->face_heading = 1;
  w->sprite_scale_xz[0] = 0xe80;
  w->sprite_scale_xz[1] = 0xe80;
  w->ot_bias = 0;
  w->vel[0] = w->vel[1] = w->vel[2] = 0;
  w->field_438 = 0;
  w->ground = -0x8000;
}

Actor* worldactor_construct(Actor* self) {  // 0x80082d8c
  actor_construct_base(self);
  self->vtbl = &kWorldActorVtable;
  actor_set_defaults(self);
  worldactor_defaults(W(self));
  return self;
}

Actor* worldactor_construct_screen(Actor* self, s16 model, s16 x, s16 y, int layer) {  // 0x80082dd4
  actor_construct_base(self);
  self->vtbl = &kWorldActorVtable;
  actor_set_defaults(self);
  actor_init_screen(self, model, x, y, layer);
  return self;
}

Actor* worldactor_construct_full(Actor* self, s16 model, const s32 pos[3], int angle_deg, int layer, s16 talk_entry,
                                 s16 pickpocket_entry, u32 flags) {  // 0x80082e5c
  actor_construct_base(self);
  self->vtbl = &kWorldActorVtable;
  actor_set_defaults(self);
  actor_init(self, model, pos, angle_deg, layer, talk_entry, pickpocket_entry, flags);
  worldactor_defaults(W(self));
  return self;
}

}  // namespace ss::game::field
