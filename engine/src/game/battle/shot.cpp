#include "game/battle/shot.h"

#include <cstring>
#include <memory>
#include <string>

#include "game/battle/bobj.h"
#include "game/battle/camera.h"
#include "game/battle/bsfx.h"
#include "game/battle/combat.h"
#include "game/battle/enemy_support.h"
#include "game/battle/killer.h"
#include "game/battle/particle.h"
#include "game/battle/render.h"
#include "game/battle/sprite.h"
#include "game/battle/stage.h"
#include "game/common/screen.h"
#include "game/title/new_game.h"
#include "psx/gte.h"
#include "runtime/libgpu.h"
#include "runtime/libgte.h"
#include "game/boot/rng.h"
#include "runtime/runtime.h"

namespace ss::game::battle {
namespace {

constexpr int kShots = 160;
std::unique_ptr<Shot[]> g_pool;  // g_pool_13c 0x8006fac4
ShotDesc g_shot_desc;            // s_shot_desc (scratchpad 0x1f800000)

s32 mul32(s32 a, s32 b) { return static_cast<s32>(static_cast<u32>(a) * static_cast<u32>(b)); }
s16 lo16(s32 v) { return static_cast<s16>(v); }
s16 hi16(s32 v) { return static_cast<s16>(static_cast<u32>(v) >> 16); }
u16 ulo16(s32 v) { return static_cast<u16>(v); }

// Kind 2 (0x800558d0): a spinning boomerang (the knights' sword waves): flies with its
// velocity for +0x128 / 24 ticks, slows by +0x118.. a tick for 0x18 ticks, then homes back
// to its owner (speed 0x18) and is gone on arrival; hits the party with the owner's attack.
// +0x130: 0 nothing hit, 1 returning, else the unit last hit (port: 2 and last_hit).
void shot_k02_boomerang(Shot* s) {
  BattleGlobals& g = bg();
  s->anim = 7;
  s->size[2] = 0x10;
  shot_draw_and_box(s, 4, 0xFFFFFFFF);
  if (g.flags != 0) return;
  Unit* o = s->owner;
  const s32 home[3] = {o->pos[0], o->pos[1] - (o->size[1] << 13), o->pos[2]};
  const s32 t = g.frame_ticks;
  auto gone = [s, o]() {
    if (--o->counters[1] < 0) o->counters[1] = 0;
    shot_kill(s);
  };
  if (kStateOver <= g.state && g.end_countdown < 0x10) {
    gone();
    return;
  }
  bool check_home = true;
  if (0 < s->p128) {
    s->p128 -= t * 24;
    check_home = false;
  } else if (s->p12c < 0x18) {
    s->vel[0] -= s->point[0] * t;
    s->vel[1] -= s->point[1] * t;
    s->vel[2] -= s->point[2] * t;
    s->p12c += t;
  } else {
    if (s->p130 == 0) {
      s->hit_landed = 0;
      s->p130 = 1;
      s->last_hit = nullptr;
    }
    const Vec4 v = vec_velocity_toward(s->pos, home, 0x18);
    s->vel[0] = v.x, s->vel[1] = v.y, s->vel[2] = v.z, s->extra34 = v.w;
  }
  if (check_home && bobj_near_point(s, home, t * 25)) {
    gone();
    return;
  }
  bobj_move(s, s->vel);
  if (s->vel[0] < 0) s->roll = static_cast<s16>(s->roll + (t << 8));
  else s->roll = static_cast<s16>(s->roll + (-(t << 9)) / 2);
  Unit* found[8] = {};
  Vec4 pts[8] = {};
  const int n = collider_attack_overlaps(s, found, pts, 1);
  if (n == 0) {
    s->p130 = 0;
    s->last_hit = nullptr;
    return;
  }
  if (s->p130 == 2 && s->last_hit == found[0]) return;
  s->p130 = 2;
  s->last_hit = found[0];
  const s32 saved[3] = {o->facing[0], o->facing[1], o->facing[2]};
  o->facing[0] = s->vel[0], o->facing[1] = s->vel[1], o->facing[2] = s->vel[2];
  if (unit_attack_targets(o, n, found, pts, 0x100, s->vel[0] < 0 ? 0x800 : 0, 0) != 0) {
    s->p128 = 0;
    s->p12c = 0x18;
  }
  o->facing[0] = saved[0], o->facing[1] = saved[1], o->facing[2] = saved[2];
}

// Kind 0x2c (0x8005b6d0): a spinning thrown weapon (variant +0x130: 0 anim 0x36, 1 anim
// 0x38 of sprite set A, 2 anim 10 of the effect bank) flying for 0x20 ticks, fading after
// 0x18; its first hit is the owner's attack (power 0x40, 0x20 for variants 1 / 2), except
// that variant 0 destroys a Starguarder (type 0x7f) outright.
void shot_k2c_thrown_weapon(Shot* s) {
  BattleGlobals& g = bg();
  s->layer = 2;
  s->size[2] = 0xE;
  u16 element = 0;
  switch (s->p130) {
    case 0:
      shot_use_anims_f928(s);
      s->anim = 0x36;
      element = 0x20;
      break;
    case 1:
      shot_use_anims_f928(s);
      s->anim = 0x38;
      element = 0;
      break;
    case 2:
      shot_use_anims_f920(s);
      s->anim = 0xA;
      element = 4;
      break;
    default: break;
  }
  bobj_project(s);
  bobj_anim_draw(s, g.flags);
  if (g.flags != 0) return;
  bobj_move(s, s->vel);
  if (0x20 < s->p128) shot_kill(s);  // (the console carries on with the frame)
  if (0x18 < s->p128) {
    s->colour_mode = 2;
    s->rgb[0] = static_cast<u8>(-0x80 - (s->p128 - 0x18) * 12);
  }
  s->p128 += g.frame_ticks;
  s->roll = static_cast<s16>(s->roll + g.frame_ticks * 0x71);
  if (s->hit_landed != 0) return;
  Unit* found[8] = {};
  Vec4 pts[8] = {};
  const int n = collider_attack_overlaps(s, found, pts, s->owner->is_enemy);
  if (n == 0) return;
  s->hit_landed = 1;
  Unit* t = found[0];
  int hit;
  if (t->is_enemy == 1 && t->st()->id == 0x7F && s->p130 == 0) {
    t->death = 1;
    t->st()->hp = 0;
    bsfx_play_at(0x33, t);
    bsfx_play_at(0x2A, t);
    hit = 1;
  } else {
    hit = unit_attack_targets(s->owner, n, found, pts, s->p130 != 0 ? 0x20 : 0x40, s->vel[0] < 0 ? 0x800 : 0,
                              element);
  }
  s->owner->attack_recovery = 0;
  if (hit != 0) {
    const s32 spread[3] = {0x40000, 0x20000, 0x20000};
    effect_debris_burst(s->pos, spread, 2, 2, 1, 0x1000, s->p130 == 2 ? 5 : 4);
  }
  shot_kill(s);
}

// Kind 0x32 (0x8005d2d0): a textured light streak (patch 1 x 1, UV (1, 0x60)-(7, 0x7f),
// CLUT 0x7d40 + +0x12c, tpage 0x2f) +0x130 wide and +0x138 long along its direction to
// +0x118, dimming as it nears; speed +0x134 grows by +0x128 a tick; gone on arrival.
void shot_k32_light_streak(Shot* s) {
  BattleGlobals& g = bg();
  shot_set_gte_origin(s);
  {
    const Vec4 d = vec_velocity_toward(s->pos, s->point, 1);
    s->vel[0] = d.x, s->vel[1] = d.y, s->vel[2] = d.z, s->extra34 = d.w;
  }
  const s32 uv[4] = {1, 0x60, 7, 0x7F};
  s32 c[4][4] = {};
  c[0][0] = s->pos[0] + mul32(s->vel[1], s->p130);
  c[0][1] = s->pos[1] + mul32(s->vel[0], s->p130);
  c[0][2] = s->pos[2];
  c[1][0] = s->pos[0] - mul32(s->vel[1], s->p130);
  c[1][1] = s->pos[1] - mul32(s->vel[0], s->p130);
  c[1][2] = s->pos[2];
  for (int k = 0; k < 2; ++k) {
    c[k + 2][0] = c[k][0] + mul32(s->vel[0], s->p138);
    c[k + 2][1] = c[k][1] + mul32(s->vel[1], s->p138);
    c[k + 2][2] = c[k][2] + mul32(s->vel[2], s->p138);
  }
  s32 level = vec_distance_fx(s->pos, s->point) >> 12;
  if (0x100 < level) level = 0x100;
  const u32 rgb = scale_colour(0x808080, level) | 0x2E000000u;
  draw_textured_patch(uv, c, static_cast<u16>(s->p12c + 0x7D40), 0x2F, 6, rgb, 1);
  if (s->power != 0 && (g.flags & 0x40) != 0) return;
  if ((g.flags & 0xBF) != 0) return;
  s->vel[0] = mul32(s->vel[0], s->p134) >> 12;
  s->vel[1] = mul32(s->vel[1], s->p134) >> 12;
  s->vel[2] = mul32(s->vel[2], s->p134) >> 12;
  bobj_move(s, s->vel);
  s->p134 += mul32(s->p128, g.frame_ticks);
  s32 d = vec_distance_fx(s->pos, s->point);
  if (d < 0) d = -d;
  if (mul32(s->p134, g.frame_ticks) + s->p138 < d) return;
  s->pos[0] = s->point[0], s->pos[1] = s->point[1], s->pos[2] = s->point[2];
  shot_kill(s);
}

// Kind 0x16 (0x8005a4d8): the descriptor projectile (shot_spawn_projectile). Flags +0x138:
// 1 trail sprites, 2 ends at the battle's end, 4 hits, bits 3-4 impact effect kind, 6-9
// its colour, 10-11 / 12-13 its sizes, 14-15 hit sprite scatter, 16-17 hit sprite set,
// 18-19 hit sprite scale, 0x20 two impact effects, 0x100000 ground shadow, 0x200000 runs
// in the 0x40 pause, 0x400000 hugs the ground, 0x800000 magic damage, 0x1000000 impact
// on the ground, 0x2000000 lifetime (+0x132), 0x4000000 flickers.
void shot_k16_projectile(Shot* s) {
  BattleGlobals& g = bg();
  s->facing[0] = s->vel[0], s->facing[1] = s->vel[1], s->facing[2] = s->vel[2];
  u32& flags = reinterpret_cast<u32&>(s->p138);
  if ((flags & 0x4000000) != 0 && s->p12c == 0) {
    s->colour_mode = 2;
    if ((flags & 0x8000000) != 0) {
      s->rgb[0] = 0x80;
      flags &= ~0x8000000u;
    } else {
      s->rgb[0] = 0xE0;
      flags |= 0x8000000;
    }
  }
  bobj_project(s);
  bobj_anim_draw(s, g.flags);
  if (static_cast<s32>(s->trail_rgb) > 0) {
    s->trail = ribbon_trail_create(s, 0xC, s->trail_rgb, s->trail_rgb);
    s->trail_rgb = 0;
    if (s->trail != nullptr) {
      s->trail->no_lift = 1;
      s->trail->back = &s->trail;  // (port: the console leaves +0x10 clear)
    }
  }
  auto kill = [s]() {
    if (s->trail != nullptr) {
      s->trail->stop = 1;
      s->trail->back = nullptr;
      s->trail = nullptr;
    }
    shot_kill(s);
  };
  if ((flags & 0x100000) != 0) {
    s32 e = s->ext_x1;
    if (e < 0) e = -e;
    const s32 v = e * 5;
    bobj_draw_shadow(s, -v, v, -v, v, static_cast<u32>(s->p128));
  }
  const u8 paused = (flags & 0x200000) != 0 ? (g.flags & 0xBF) : g.flags;
  if (paused != 0) return;
  if ((flags & 0x2000000) != 0) {
    const s32 life = hi16(s->p130) - g.frame_ticks;
    if (life < 0x14 && s->p12c == 0) s->p12c = 1;
    if (life < 0) {
      kill();
      return;
    }
    s->p130 = static_cast<s32>(ulo16(s->p130) | static_cast<u32>(life) << 16);
  }
  if (s->p12c == 0) {
    if (kStateOver <= g.state && (flags & 2) != 0) s->p12c = 1;
    if (bobj_outside_arena(s)) s->p12c = 1;
    if ((flags & 1) != 0) {
      Sprite* sp = nullptr;
      if (s->p134 < 0) {
        sp = sprite_spawn_b((-s->p134) >> 16, 1, nullptr);
        if (sp != nullptr) sp->clut_offset = static_cast<u8>(-s->p134);
      } else {
        sp = shot_spawn_sprite(s, s->p134 + 1);
      }
      if (sp != nullptr) {
        sprite_set_pos_v(sp, s->pos);
        sprite_set_fade(sp, 0x80, lo16(s->p130), 0);
      }
    }
  } else {
    const u8 mask = (flags & 0x200000) != 0 ? 0xBF : 0xFF;
    if ((g.flags & mask) == 0) {
      s->colour_mode = 2;
      s->rgb[0] = static_cast<u8>(-0x80 - s->p12c * 4);
      s->p12c += g.frame_ticks;
      if (0x18 < s->p12c) {
        kill();
        return;
      }
    }
  }
  if ((s->airborne & 0x7F) != 0) {
    if (Sprite* sp = sprite_spawn_b(4, 1, nullptr)) {
      sprite_set_pos_v(sp, s->pos);
      sp->freeze_class = (g.flags & 0x40) != 0 ? 2 : 1;
      sprite_set_fade(sp, 0x80, -4, 0x14);
      sp->clut_offset = static_cast<u8>(s->airborne & 0x7F);
    }
  }
  bobj_move(s, s->vel);
  if ((flags & 0x400000) != 0) s->pos[1] = bobj_ground_height(s);
  if (0xF < s->p12c || s->hit_landed != 0 || (flags & 4) == 0) return;
  Unit* found[8] = {};
  Vec4 pts[8] = {};
  const int n = collider_attack_overlaps(s, found, pts, s->owner->is_enemy);
  if (n == 0) return;
  s->hit_landed = 1;
  if ((flags & 0x800000) != 0) {
    shot_apply_damage(s, n, found, pts, s->point[0], s->aux, static_cast<u16>(s->power), 0, 0x12);
  } else {
    u8* st = s->owner->stats;
    u16 saved;
    std::memcpy(&saved, st + 0x5C, 2);
    const u16 el = static_cast<u16>(saved | static_cast<u16>(s->power));
    std::memcpy(st + 0x5C, &el, 2);
    unit_attack_targets(s->owner, n, found, pts, s->point[0], s->vel[0] < 0 ? 0x800 : 0, 0);
    std::memcpy(st + 0x5C, &saved, 2);
  }
  // Hit sprites.
  const u16 hit_anim = ulo16(s->point[1]);
  if (hit_anim != 0) {
    for (int i = 0; i < ulo16(s->point[2]); ++i) {
      Sprite* sp = nullptr;
      switch (hi16(static_cast<s32>(flags)) & 3) {
        case 0: sp = shot_spawn_sprite(s, hit_anim); break;
        case 1: sp = sprite_spawn_from(hit_anim, 0, g.sprite_bank_fx.get(), nullptr); break;
        case 2: sp = sprite_spawn_a(hit_anim, 0, nullptr); break;
        case 3: sp = sprite_spawn_b(hit_anim, 0, nullptr); break;
      }
      if (sp == nullptr) continue;
      const int j = static_cast<int>(flags >> 14) & 3;
      if (j == 0) {
        sprite_set_pos_v(sp, s->pos);
      } else {
        const s32 x = s->pos[0] + rng_spread(battle_s16(0x8006FFCC + static_cast<u32>(j) * 2));
        const s32 y = s->pos[1] + rng_spread(battle_s16(0x8006FFD4 + static_cast<u32>(j) * 2));
        const s32 z = s->pos[2] + rng_spread(battle_s16(0x8006FFCC + static_cast<u32>(j) * 2));
        sprite_set_pos(sp, x, y, z);
      }
      const u32 k = static_cast<u32>(hi16(static_cast<s32>(flags)) & 0xC);
      const s32 sc = rng_between(battle_s16(0x8006FFDC + k), battle_s16(0x8006FFDE + k));
      sp->scale_x = sp->scale_y = sc;
      sprite_set_fade(sp, 0x80, -4, 0x14);
      sp->clut_offset = static_cast<u8>(hi16(s->point[2]));
      sp->freeze_class = static_cast<u8>((flags >> 21) & 1);
      if (0 < i) {
        sp->size[2] = static_cast<s16>(rng_range(static_cast<u32>(i * 8 + 8)));
        sp->vel[0] = rng_spread_fixed(6);
        sp->vel[2] = rng_spread_fixed(6);
      }
    }
  }
  // A follow-up Shot.
  if (const s16 kind2 = hi16(s->point[1]); kind2 != 0) {
    if (Shot* n2 = shot_spawn(kind2)) {
      n2->owner = s->owner;
      n2->target = found[0];
      n2->pos[0] = s->pos[0], n2->pos[1] = s->pos[1], n2->pos[2] = s->pos[2];
      n2->vel[0] = s->vel[0], n2->vel[1] = s->vel[1], n2->vel[2] = s->vel[2];
    }
  }
  // Impact effects.
  if (((flags >> 3) & 3) != 0) {
    const u32 a = (flags >> 10) & 3, b = (flags >> 12) & 3;
    const s32 radius = *battle_data(0x8006FFB8 + a);
    const s32 width = *battle_data(0x8006FFBC + a);
    s32 grow = *battle_data(0x8006FFC4 + b);
    const s32 segs = *battle_data(0x8006FFC0 + b);
    const s32 life = *battle_data(0x8006FFC8 + b);
    s32 at[4] = {s->pos[0], s->pos[1], s->pos[2], 0};
    const s32 ci = static_cast<s32>(flags >> 6) & 0xF;
    if ((flags & 0x1000000) != 0) at[1] = bobj_ground_height(s);
    const int count = (flags & 0x20) != 0 ? 2 : 1;
    const u32 rgb_b = static_cast<u32>(battle_s32(0x8006CF24 + static_cast<u32>(ci) * 4));
    const u32 rgb_a = static_cast<u32>(battle_s32(0x8006CEE4 + static_cast<u32>(ci) * 4));
    for (int m = 0; m < count; ++m) {
      switch ((flags >> 3) & 3) {
        case 1: particle_spawn_ring(at, segs, width, grow, life, rgb_b, rgb_a, radius, 0); break;
        case 2:
          effect_spawn_sphere_at_default(at, segs, segs, grow, life, rgb_a, radius,
                                         static_cast<s16>(flags & 0x200000));
          break;
        case 3:
          effect_spawn_sparks_at(at, segs, width / 4, width + 0x10, grow, life, ci, 1, radius,
                                 static_cast<s16>(flags & 0x200000));
          break;
      }
      grow /= 2;
    }
  }
  if ((s->airborne & 0x80) != 0) return;
  kill();
}

// Kind 1 (0x8005566c): three stun stars circling the head of +0x114; keeps the target's
// stun bit (stats +2 0x40) set for +0x128 ticks.
void shot_k01_stun_orbit(Shot* s) {
  BattleGlobals& g = bg();
  shot_use_anims_f928(s);
  s->anim = 0x12;
  Unit* t = s->target;
  if (t == nullptr) {
    shot_kill(s);
    return;
  }
  if (s->p128 < 1 || unit_is_down(t)) {
    t->stats[2] = static_cast<u8>(t->stats[2] & 0xBF);
    if (t->is_enemy == 0) static_cast<PartyUnit*>(t)->busy = 0;
    shot_kill(s);
    return;
  }
  t->stats[2] = static_cast<u8>(t->stats[2] | 0x40);
  const s32 cx = t->pos[0], cz = t->pos[2];
  const s32 rx = t->size[0] * 2 + 0x10, rz = t->size[2] * 2;
  s->pos[0] = cx + bcos(static_cast<u32>(s->p12c)) * rx;
  s->pos[2] = cz + bsin(static_cast<u32>(s->p12c)) * rz;
  s->pos[1] = t->pos[1] + t->size[1] * -0x4000;
  // (The console draws the first star, then twice at 120 degrees and once at 240: four
  // draws, each advancing the animation.)
  for (int deg = 0x78; deg < 0xF1; deg += 0x78) {
    bobj_project(s);
    bobj_anim_draw(s, 0);
    const s32 a = (deg * 0x1000) / 0x168;
    s->pos[0] = cx + bcos(static_cast<u32>(s->p12c + a)) * rx;
    s->pos[2] = cz + bsin(static_cast<u32>(s->p12c + a)) * rz;
    bobj_project(s);
    bobj_anim_draw(s, 0);
  }
  if ((g.flags & 8) == 0) {
    const u32 a = static_cast<u32>(s->p12c + (g.frame_ticks * 0xA000) / 0x168) & 0xFFF;
    s->p12c = static_cast<s32>(a);
    s->roll = static_cast<s16>(a);
    if (g.flags == 0) s->p128 -= g.frame_ticks;
  }
}

// The quad UV table of kind 0x11 (0x8006ff78: 8 entries of uv2, uv3, uv0, uv1; entry 2 is
// rewritten from the current frame's UV box).
u16 g_ring_uv[8][4];
bool g_ring_uv_loaded = false;

void ring_uv_load() {
  if (g_ring_uv_loaded) return;
  g_ring_uv_loaded = true;
  for (int k = 0; k < 8; ++k)
    for (int i = 0; i < 4; ++i) {
      const u8* p = battle_data(0x8006FF78 + static_cast<u32>(k * 8 + i * 2));
      g_ring_uv[k][i] = p != nullptr ? static_cast<u16>(p[0] | p[1] << 8) : 0;
    }
}

u32 svec_xy(s32 x, s32 y) { return static_cast<u32>(static_cast<u16>(x)) | static_cast<u32>(static_cast<u16>(y)) << 16; }

// Kind 0x11 (0x80058b78): a ring of +0x128 textured quads (radius +0x138, width +0x118,
// height +0x11c; UV variant +0x120, cycling 4..7 when 4 or more; CLUT 0x7d40 + +0x12c)
// turning by +0x28 and growing by +0x130 for +0x134 ticks; following +0x110 when set.
void shot_k11_sprite_ring(Shot* s) {
  BattleGlobals& g = bg();
  static const s32 kUnitScale[3] = {0x1000, 0x1000, 0x1000};  // 0x8006e34c
  ring_uv_load();
  if (s->spark_owner != nullptr) {
    s->pos[0] = s->spark_owner->pos[0];
    s->pos[1] = s->spark_owner->pos[1];
    s->pos[2] = s->spark_owner->pos[2];
  }
  s32 orient[3] = {s->vel[2], 0, 0};
  obj_set_matrix(s->pos, orient, kUnitScale);
  bool draw = true;
  u16 tex_tpage = 0, tex_clut = 0;
  if (s->point[2] == 2) {
    const u32 f = s->power != 0 ? (g.flags & 0xC) : g.flags;
    bobj_anim_draw(s, f | 0x9800);
    const FrameUv& uv = frame_uv();
    if (uv.v0 == uv.v1) {
      draw = false;
    } else {
      tex_tpage = uv.tpage;
      tex_clut = uv.clut;
      g_ring_uv[2][0] = static_cast<u16>((uv.u0 & 0xFF) | (uv.v0 & 0xFF) << 8);
      g_ring_uv[2][1] = static_cast<u16>((uv.u1 & 0xFF) | (uv.v0 & 0xFF) << 8);
      g_ring_uv[2][2] = static_cast<u16>((uv.u0 & 0xFF) | (uv.v1 & 0xFF) << 8);
      g_ring_uv[2][3] = static_cast<u16>((uv.u1 & 0xFF) | (uv.v1 & 0xFF) << 8);
    }
  }
  RenderContext* pool = s->pool;
  if (draw && arena_has_room(pool, s->p128 * 0x28)) {
    psx::Gte& q = psyq::gte();
    s32 c0 = bcos(static_cast<u32>(s->aux)), s0 = bsin(static_cast<u32>(s->aux));
    u32 colour = 0x808080;
    if (s->p134 < 0x20) colour = scale_colour(0x808080, s->p134 << 2);
    for (int i = 0; i < s->p128; ++i) {
      if (3 < s->point[2]) {
        ++s->point[2];
        if (7 < s->point[2]) s->point[2] = 4;
      }
      const s32 a = ((i + 1) << 12) / s->p128;
      const s32 c1 = bcos(static_cast<u32>(a + s->aux)), s1 = bsin(static_cast<u32>(a + s->aux));
      const s32 r = s->p138, r2 = s->p138 + s->point[0];
      const s16 h = static_cast<s16>(-s->point[1]);
      q.write_data(psx::Gte::VXY0, svec_xy((c0 * r) >> 12, 0));
      q.write_data(psx::Gte::VZ0, static_cast<u32>(static_cast<s32>(static_cast<s16>((s0 * r) >> 12))));
      q.execute(psx::Gte::kRtps);
      const u32 v0 = q.read_data(psx::Gte::SXY2);
      q.write_data(psx::Gte::VXY0, svec_xy((c1 * r) >> 12, 0));
      q.write_data(psx::Gte::VZ0, static_cast<u32>(static_cast<s32>(static_cast<s16>((s1 * r) >> 12))));
      q.write_data(psx::Gte::VXY1, svec_xy((c0 * r2) >> 12, h));
      q.write_data(psx::Gte::VZ1, static_cast<u32>(static_cast<s32>(static_cast<s16>((s0 * r2) >> 12))));
      q.write_data(psx::Gte::VXY2, svec_xy((c1 * r2) >> 12, h));
      q.write_data(psx::Gte::VZ2, static_cast<u32>(static_cast<s32>(static_cast<s16>((s1 * r2) >> 12))));
      q.execute(psx::Gte::kRtpt);
      const u32 v1 = q.read_data(psx::Gte::SXY0), v2 = q.read_data(psx::Gte::SXY1), v3 = q.read_data(psx::Gte::SXY2);
      c0 = c1;
      s0 = s1;
      auto x_in = [](u32 v) { return static_cast<u16>(static_cast<u16>(v) + 0x28) < 0x1A4; };
      auto y_in = [](u32 v) { return static_cast<u16>(static_cast<u16>(v >> 16) + 0x3C) < 300; };
      if (!(x_in(v0) || x_in(v1) || x_in(v2) || x_in(v3))) continue;
      q.execute(psx::Gte::kAvsz4);
      if (!(y_in(v0) || y_in(v1) || y_in(v2) || y_in(v3))) continue;
      const s32 otz = static_cast<s32>(q.read_data(psx::Gte::OTZ)) >> 3;
      if (0x5F8 < otz) continue;
      auto* p = screen_alloc_prims<psyq::POLY_FT4>(pool->arena, 1);
      if (p == nullptr) break;
      psyq::setPolyFT4(p);
      const u32 rgbc = colour | 0x2E000000u;
      std::memcpy(&p->r0, &rgbc, 4);
      const int k = s->point[2] & 7;
      if (s->point[2] == 2) {
        p->clut = tex_clut;
        p->tpage = tex_tpage;
      } else {
        p->clut = static_cast<u16>(s->p12c + 0x7D40);
        p->tpage = s->point[2] < 4 ? 0x2F : 0x3F;
      }
      auto put_uv = [](u8* u, u16 uv) { u[0] = static_cast<u8>(uv), u[1] = static_cast<u8>(uv >> 8); };
      put_uv(&p->u0, g_ring_uv[k][2]);
      put_uv(&p->u1, g_ring_uv[k][3]);
      put_uv(&p->u2, g_ring_uv[k][0]);
      put_uv(&p->u3, g_ring_uv[k][1]);
      p->x0 = static_cast<s16>(v0), p->y0 = static_cast<s16>(v0 >> 16);
      p->x1 = static_cast<s16>(v1), p->y1 = static_cast<s16>(v1 >> 16);
      p->x2 = static_cast<s16>(v2), p->y2 = static_cast<s16>(v2 >> 16);
      p->x3 = static_cast<s16>(v3), p->y3 = static_cast<s16>(v3 >> 16);
      psyq::addPrim(&pool->ot_head[0x617 - otz], p);
      if (pool == g.prim_pool_a) pool_a_bytes_used() += 0x28;
    }
  }
  if (s->power == 1 && (g.flags & 0x40) != 0) return;
  if ((g.flags & 0xBF) != 0) return;
  const s32 t = g.frame_ticks;
  s->pos[0] += s->vel[1] * t;
  s->aux = static_cast<s16>((static_cast<u16>(s->aux) + s->vel[0] * t) & 0xFFF);
  if (s->power != 0) s->p138 += (s->p130 * t) / 2;
  else s->p138 += s->p130;
  s->p134 -= t;
  if (s->p134 <= 0 || (s->point[2] == 2 && kStateOver <= g.state && g.end_countdown < 0x18)) shot_kill(s);
}

// Kinds 0x21 / 0x33 (0x8005b530): the handler at +0x138.
void shot_k21_callback(Shot* s) {
  if (s->handler != nullptr) s->handler(s);
}

void unported_kind(Shot* s) {
  runtime::Runtime::get().log_once("battle-shot-" + std::to_string(s->kind),
                                   "battle: Shot kind " + std::to_string(s->kind) + " not ported (removed)");
  shot_kill(s);
}

}  // namespace

void shot_pool_create() {
  g_ring_uv_loaded = false;
  g_pool = std::make_unique<Shot[]>(kShots);
  for (int i = 0; i < kShots; ++i) g_pool[i].kind = 0;
}

void shot_pool_destroy() { g_pool.reset(); }

Shot* shot_at(int index) { return g_pool && index >= 0 && index < kShots ? &g_pool[index] : nullptr; }

void shot_pool_update() {
  if (!g_pool) return;
  for (int i = 0; i < kShots; ++i)
    if (g_pool[i].kind != 0) g_pool[i].update();
}

void Shot::update() {
  BattleGlobals& g = bg();
  if (g.effects_suppressed != 0 && kind != 0x21 && command != 0xA5) return;
  switch (kind) {
    case 1: shot_k01_stun_orbit(this); break;
    case 2: shot_k02_boomerang(this); break;
    case 0x11: shot_k11_sprite_ring(this); break;
    case 0x16: shot_k16_projectile(this); break;
    case 0x2C: shot_k2c_thrown_weapon(this); break;
    case 0x32: shot_k32_light_streak(this); break;
    case 0x21:
    case 0x33: shot_k21_callback(this); break;
    default: unported_kind(this); break;
  }
}

Shot* shot_spawn(int kind) {
  if (!g_pool) return nullptr;
  for (int i = 0; i < kShots; ++i) {
    Shot* s = &g_pool[i];
    if (s->kind != 0) continue;
    shot_reset(s);
    s->kind = static_cast<u16>(kind);
    return s;
  }
  return nullptr;
}

int shot_count_free() {
  if (!g_pool) return 0;
  int n = 0;
  for (int i = 0; i < kShots; ++i)
    if (g_pool[i].kind == 0) ++n;
  return n;
}

void shot_reset(Shot* s) {
  bobj_anim_reset(s);
  s->p128 = s->p12c = s->p130 = s->p134 = s->p138 = 0;
  s->handler = nullptr;
  s->spark_owner = nullptr;
  s->trail = nullptr;
  s->trail_rgb = 0;
  s->last_hit = nullptr;
  for (s32& v : s->user) v = 0;
  s->lift = 0;
  s->animset = nullptr;
  s->blend = 0;
  s->clut_offset = 0;
  s->loop_limit = 0;
  s->texcache = nullptr;
  s->colour_mode = 0;
  s->owner = nullptr;
  s->target = nullptr;
  s->scale_y = s->scale_x = 0x1000;
  s->facing[0] = s->facing[1] = s->facing[2] = 0;
  s->vel[0] = s->vel[1] = s->vel[2] = 0;
  s->point[0] = s->point[1] = s->point[2] = 0;
  s->roll = 0;
  s->aux = 0;
  s->command = 0;
  s->pool = bg().prim_pool_a;
}

void shot_kill(Shot* s) {
  BattleGlobals& g = bg();
  if (s == nullptr) return;
  s->kind = 0;
  if (g.camera == nullptr) return;
  camera_set_focus_timer(g.camera, s, 0x14);
  if (g.camera->mode == 0xC && g.camera->target == s) camera_reset_mode(g.camera);
}

void shot_use_anims_f928(BObj* o) { bobj_share_sprite(bg().sprite_set_a.get(), o); }
void shot_use_anims_f920(BObj* o) { bobj_share_sprite(bg().sprite_bank_fx.get(), o); }

Sprite* shot_spawn_sprite(Shot* s, int anim) { return sprite_spawn_from(anim, 0, s, nullptr); }
Sprite* shot_spawn_sprite_at(Shot* s, int anim, int arg) { return sprite_spawn_from(anim, arg, s, nullptr); }

int shot_hit_melee(Shot* s, int power, int vs_party) {
  Unit* units[8] = {};
  Vec4 points[8] = {};
  const int n = collider_attack_overlaps(s, units, points, vs_party);
  if (n == 0) return 0;
  s->hit_landed = 1;
  s32 dir = s->vel[0];
  if (dir == 0) dir = s->owner->facing[0];
  const int r = unit_attack_targets(s->owner, n, units, points, power, dir < 0 ? 0x800 : 0, 0) + 1;
  s->owner->attack_recovery = 0;
  return r;
}

void shot_place_at_unit(Shot* s, const BObj* u, s32 dx, s32 dy, s32 dz) {
  s->pos[0] = u->pos[0] + bobj_facing_fx(u, dx);
  s->pos[1] = u->pos[1] + dy * -0x1000;
  s->pos[2] = u->pos[2] + dz;
  s->facing[0] = u->facing[0];
}

int shot_draw_and_box(Shot* s, int size, u32 shadow_colour) {
  bobj_project(s);
  const int frame = bobj_anim_draw(s, bg().flags);
  s32 r = s->ext_x1;
  if (r < 0) r = -r;
  r *= size;
  bobj_draw_shadow(s, -r, r, -r, r, shadow_colour);
  return frame;
}

int shot_draw_and_box_ground(Shot* s, int size, u32 shadow_colour) {
  bobj_project(s);
  const int frame = bobj_anim_draw(s, bg().flags);
  s32 h = bobj_ground_height(s) - s->pos[1];
  if (h < 0) h += 0xFFFF;
  s32 r = s->ext_x1;
  if (r < 0) r = -r;
  r = r * size - (h >> 16);
  if (r < 0x10) r = 0x10;
  bobj_draw_shadow(s, -r, r, -r, r, shadow_colour);
  return frame;
}

int shot_fade_out(Shot* s) {
  BattleGlobals& g = bg();
  if (kStateOver <= g.state) {
    if (s->p12c == 0) s->p12c = 1;
  }
  if (s->p12c == 0) return 0;
  if ((g.flags & 0xBF) != 0) return 0;
  s->colour_mode = 2;
  s->rgb[0] = static_cast<u8>(s->p12c * -4 - 0x80);
  s->p12c += g.frame_ticks;
  if (0x18 < s->p12c) {
    shot_kill(s);
    return 1;
  }
  return 0;
}

Shot* shot_spawn_callback(ShotFn fn) {
  Shot* s = shot_spawn(0x33);
  if (s != nullptr) s->handler = fn;
  return s;
}

Shot* shot_spawn_callback_always(ShotFn fn) {
  Shot* s = shot_spawn(0x21);
  if (s != nullptr) s->handler = fn;
  return s;
}

int shot_kind_absent(int kind) {
  if (!g_pool) return 1;
  for (int i = 0; i < kShots; ++i)
    if (g_pool[i].kind == static_cast<u16>(kind)) return 0;
  return 1;
}

int shot_kind21_absent(ShotFn fn) {
  if (!g_pool) return 1;
  for (int i = 0; i < kShots; ++i)
    if (g_pool[i].kind == 0x21 && g_pool[i].handler == fn) return 0;
  return 1;
}

Shot* effect_spawn_sparks_at(const s32 pos[3], s32 count, s32 p118, s32 p11c, s32 p130, s32 p134, s32 p12c,
                             s32 p120, s32 p138, s16 power) {
  Shot* s = shot_spawn(0x11);
  if (s == nullptr) return nullptr;
  s->pos[0] = pos[0], s->pos[1] = pos[1], s->pos[2] = pos[2];
  s->p128 = count;
  s->p12c = p12c;
  s->p130 = p130;
  s->p134 = p134;
  s->p138 = p138;
  s->point[0] = p118;
  s->point[1] = p11c;
  s->point[2] = p120;
  s->power = power;
  return s;
}

Shot* effect_spawn_sparks_on(BObj* owner, s32 count, s32 p118, s32 p11c, s32 p130, s32 p134, s32 p12c, s32 p120,
                             s32 p138, s16 power) {
  Shot* s = shot_spawn(0x11);
  if (s == nullptr) return nullptr;
  // (The owner field holds any BObj here; only its position is read.)
  s->spark_owner = owner;
  s->p128 = count;
  s->p12c = p12c;
  s->p130 = p130;
  s->p134 = p134;
  s->p138 = p138;
  s->point[0] = p118;
  s->point[1] = p11c;
  s->point[2] = p120;
  s->power = power;
  return s;
}

// ---- unit_apply_stun (0x80055588) --------------------------------------------------------

void unit_apply_stun(Unit* u) {
  if (u->status_test(0, 0x1000000) != 0) return;
  if (g_pool) {
    for (int i = 0; i < kShots; ++i) {
      Shot* s = &g_pool[i];
      if (s->kind == 1 && s->target == u) {
        s->p128 = 0x78;
        return;
      }
    }
  }
  Shot* s = shot_spawn(1);
  if (s == nullptr) return;
  s->target = u;
  s->p128 = u->status_test(0, 0x800000) != 0 ? 0x3C : 0x78;
}

ShotDesc* shot_desc_init(int anim) {
  g_shot_desc = ShotDesc{};
  g_shot_desc.box = 0x60;
  g_shot_desc.sfx = 0x4A;
  g_shot_desc.flags = 0x1000000;
  g_shot_desc.anim = anim;
  g_shot_desc.trail_anim = anim;
  g_shot_desc.hit_bits[2] = 1;
  g_shot_desc.hits = 1;
  g_shot_desc.power = 0x100;
  return &g_shot_desc;
}

Shot* shot_spawn_projectile(Unit* owner, const s32 from[3], const ShotDesc* d, const BObj* anim_src) {
  Shot* s = shot_spawn(0x16);
  if (s == nullptr) return nullptr;
  s->anim = static_cast<u16>(d->anim);
  s->owner = owner;
  s->target = d->target;
  s->pos[0] = from[0], s->pos[1] = from[1], s->pos[2] = from[2];
  u32 flags = d->flags;
  auto set_vel = [s](const Vec4& v) { s->vel[0] = v.x, s->vel[1] = v.y, s->vel[2] = v.z, s->extra34 = v.w; };
  if (const Unit* t = d->target; t != nullptr) {
    const s32 aim[3] = {t->pos[0] + (t->size[0] << 13), t->pos[1] - (t->size[1] << 13), t->pos[2]};
    if (d->angle != 0) {
      const s32 yaw = ratan2(aim[2] - from[2], aim[0] - from[0]) + d->angle;
      set_vel(vec_velocity_toward(from, aim, d->speed));
      s->vel[0] = mul32(bcos(static_cast<u32>(yaw)), d->speed);
      s->vel[2] = mul32(bsin(static_cast<u32>(yaw)), d->speed);
      if ((flags & 0x400000) != 0) s->vel[1] = 0;
    } else if ((flags & 0x400000) != 0) {
      set_vel(vec_velocity_toward_xz(from, aim, d->speed));
    } else {
      set_vel(vec_velocity_toward(from, aim, d->speed));
    }
  } else if ((flags & 0x400000) != 0) {
    set_vel(vec_velocity_toward_xz(from, d->point, d->speed));
  } else {
    set_vel(vec_velocity_toward(from, d->point, d->speed));
  }
  s->p130 = d->spin;
  if (d->spin != 0) {
    flags |= 1;
    s->p134 = d->trail_anim;
  }
  s->p130 = static_cast<s32>(ulo16(s->p130) | static_cast<u32>(d->life) << 16);
  if (d->life != 0) flags |= 0x2000000;
  s->trail_rgb = static_cast<u32>(d->trail_rgb);
  if (d->trail_bits[0] != 0) {
    flags |= static_cast<u32>(d->trail_bits[0]) << 3 | static_cast<u32>(d->trail_bits[1]) << 6 |
             static_cast<u32>(d->trail_bits[2]) << 10 | static_cast<u32>(d->trail_bits[3]) << 12;
  }
  s->point[1] = static_cast<s32>(static_cast<u32>(d->hit_effect) | static_cast<u32>(d->hit_effect_hi) << 16);
  if (s->point[1] != 0) {
    s->point[2] = static_cast<s32>(static_cast<u32>(d->hits) | static_cast<u32>(d->hits_hi) << 16);
    flags |= static_cast<u32>(d->hit_bits[0]) << 14 | static_cast<u32>(d->hit_bits[1]) << 16 |
             static_cast<u32>(d->hit_bits[2]) << 18;
  }
  s->p138 = static_cast<s32>(flags);
  s->airborne = static_cast<u8>(d->airborne);
  if (hi16(d->element) != 0) s->airborne = static_cast<u8>(d->airborne | 0x80);
  s->point[0] = d->power;
  s->power = static_cast<s16>(d->element);
  s->size[2] = static_cast<s16>(d->box);
  s->layer = 1;
  s->aux = static_cast<s16>(d->sfx);
  bobj_share_sprite(anim_src, s);
  return s;
}

Shot* effect_spawn_sphere_at(const s32 pos[3], s32 lat, s32 lon, s32 grow, s32 life, u32 rgb, s32 radius, s16 power,
                             s16 scale) {
  Shot* s = shot_spawn(0x10);
  if (s == nullptr) return nullptr;
  s->pos[0] = pos[0], s->pos[1] = pos[1], s->pos[2] = pos[2];
  s->p128 = lat;
  s->p12c = lon;
  s->p130 = grow;
  s->p134 = life;
  s->p138 = radius;
  s->aux = scale;
  s->point[0] = static_cast<s32>(rgb);
  s->power = power;
  return s;
}

int shot_apply_damage(Shot* s, int n, Unit** targets, const Vec4* points, int power, int sfx, u16 element, int spark,
                      int knockback) {
  BattleGlobals& g = bg();
  Unit* owner = s->owner;
  int missed = 0;
  auto tenth = [](s32 v) { return v / 10; };
  if (owner->status_test(0, 0x8000) != 0) power += tenth(power);
  if (owner->status_test(1, 2) != 0 && (element & 2) != 0) power += tenth(power * 3);
  if (owner->status_test(1, 4) != 0 && (element & 4) != 0) power += tenth(power * 3);
  if (owner->status_test(1, 8) != 0 && (element & 0x10) != 0) power += tenth(power * 3);
  if (owner->status_test(1, 0x8000000) != 0 || owner->status_test(2, 0x80) != 0) power += power >> 1;
  if (owner->status_test(3, 0x80) != 0) power = tenth(power * 14);
  if (owner->status_test(3, 0x1000) != 0) power += power >> 1;
  const s32 base = power;
  for (int i = 0; i < n; ++i) {
    Unit* t = targets[i];
    const s32 dir = t->pos[0] < s->pos[0] ? -0x1000 : 0x1000;
    s32 kb = knockback - t->st()->stat54[2];
    if (kb <= 0) kb = 1;
    s32 p = base;
    if ((t->status_bits & 0x8000) != 0) p /= 2;
    if (t->status_test(0, 0x4000) != 0 || t->status_test(1, 0x20) != 0) {
      const s32 d = -p / 10;
      unit_damage_mp(t, d);
      damage_popup_spawn(t, d, 0, 1);
    }
    if (t->status_test(1, 0x100) != 0 && (element & 0x10) != 0) {
      const s32 d = -p / 20;
      unit_damage_mp(t, d);
      damage_popup_spawn(t, d, 0, 1);
    }
    s32 dmg = unit_calc_damage(owner, t, p, 1, element);
    t->last_attacker = owner;
    g.last_attacker = owner;
    if (t->status_test(0, 0x80000000) != 0 && rng_range(100) < 10) dmg = 0;
    if (t->status_test(1, 0x40000000) != 0) dmg >>= 1;
    t->knockback[1] = 0;
    t->knockback[2] = 0;
    if (dmg != 0) {
      t->reaction = 1;
      t->knockback[0] = mul32(dir, kb) * 2;
      const u32 guts = static_cast<u32>(static_cast<s32>(t->st()->guts[2]));
      if (rng_range(0x400) < guts) {
        dmg /= 2;
        unit_show_guts_effect(owner, t, owner->is_enemy != 0);
      }
      screen_shake(2, 2, 10);
    } else {
      missed = 1;
    }
    if (t->status_test(0, 0x100000) != 0) {
      dmg = -dmg;
      t->accessory_proc(0x65, 0, 0x100000, 8);
    }
    if (t->status_test(0, 0x4000000) != 0) t->accessory_proc(0x6F, 0, 0x4000000, 5);
    if (dmg != 0 && t->status_test(0, 0x20000000) != 0) {
      t->accessory_proc(0x72, 0, 0x20000000, 10);
      dmg /= 10;
    }
    if (t->status_test(1, 0x40) != 0 && rng_range(100) < 10) dmg = -dmg;
    // Reflected back at the caster (5 % / 10 %).
    if (dmg != 0 && t->status_test(1, 0x4000) != 0 && rng_range(100) < 10) {
      damage_popup_spawn(owner, dmg, 0, 0);
      unit_damage_hp(owner, dmg);
      dmg = 0;
    }
    if (dmg != 0 && t->status_test(1, 0x10000) != 0 && rng_range(100) < 5) {
      damage_popup_spawn(owner, dmg, 0, 0);
      unit_damage_hp(owner, dmg);
      dmg = 0;
    }
    // Passed on to another party member (10 %).
    if (dmg != 0 && t->status_test(1, 0x20000) != 0 && rng_range(100) < 10) {
      for (int tries = 0; tries < 100; ++tries) {
        Unit* other = g.party_units[rng_range(4)];
        // The console compares with targets[tries], walking past the list; the port stops
        // comparing at the list's end.
        const Unit* listed = tries < n ? targets[tries] : nullptr;
        if (other == listed) continue;
        if (!unit_is_down(other)) {
          targets[i] = other;
          break;
        }
      }
      t = targets[i];
    }
    if (dmg != 0 && t->status_test(2, 0x10000) != 0) {
      dmg = 0;
      t->clear_status(2, 0x10000);
    }
    if (9999 < dmg) dmg = 9999;
    if (dmg < -9999) dmg = -9999;
    s32 vx = mul32(dir, kb);
    vx = (vx < 0 ? vx + 3 : vx) >> 2;
    if (owner->hits_mp != 0) {
      damage_popup_spawn(t, dmg, vx, 1);
      unit_damage_mp(t, dmg);
    } else {
      damage_popup_spawn(t, dmg, vx, 0);
      unit_damage_hp(t, dmg);
    }
    if (t->status_test(0, 8) != 0) {
      const s32 d = -(dmg / 5);
      damage_popup_spawn(t, d, 0, 1);
      unit_damage_mp(t, d);
    }
    owner->last_damage = dmg;
    if (spark != 0) {
      s->hit_landed = 1;
      if (Sprite* sp = sprite_spawn_a(0x11, 1, nullptr)) {
        const s32 at[3] = {points[i].x, points[i].y, points[i].z};
        sprite_set_pos_v(sp, at);
        sp->layer = 2;
      }
    }
  }
  if (sfx != 0) bsfx_play_at(sfx, targets[0]);
  return missed;
}

void shot_set_gte_origin(const Shot* s) {
  ViewState& v = view();
  psx::Gte& q = psyq::gte();
  s16 d[3];
  if (s->owner != nullptr) {
    d[0] = static_cast<s16>((s->owner->pos[0] - v.view_eye[0]) >> 12);
    d[1] = static_cast<s16>((s->owner->pos[1] - v.view_eye[1] - 0x10000) >> 12);
    d[2] = static_cast<s16>((s->owner->pos[2] - v.view_eye[2]) >> 12);
  } else {
    d[0] = static_cast<s16>((s->pos[0] - v.view_eye[0]) >> 12);
    d[1] = static_cast<s16>((s->pos[1] - v.view_eye[1]) >> 12);
    d[2] = static_cast<s16>((s->pos[2] - v.view_eye[2]) >> 12);
  }
  psyq::SetRotMatrix(&v.render_view);
  q.write_data(psx::Gte::VXY0, static_cast<u32>(static_cast<u16>(d[0])) | static_cast<u32>(static_cast<u16>(d[1])) << 16);
  q.write_data(psx::Gte::VZ0, static_cast<u32>(static_cast<s32>(d[2])));
  q.execute(psx::Gte::kRtv0);
  psyq::MATRIX m = v.render_view;
  m.t[0] = static_cast<s32>(q.read_data(psx::Gte::MAC1));
  m.t[1] = static_cast<s32>(q.read_data(psx::Gte::MAC2));
  m.t[2] = static_cast<s32>(q.read_data(psx::Gte::MAC3));
  psyq::SetRotMatrix(&v.render_view);
  psyq::SetTransMatrix(&m);
}

Shot* effect_spawn_beam_at(const s32 pos[3], const s32 target[3], s32 p12c, s32 p138, s32 p130, s32 p134, s32 p128,
                           s16 power) {
  Shot* s = shot_spawn(0x32);
  if (s == nullptr) return nullptr;
  s->pos[0] = pos[0], s->pos[1] = pos[1], s->pos[2] = pos[2];
  s->point[0] = target[0], s->point[1] = target[1], s->point[2] = target[2];
  s->p128 = p128;
  s->p12c = p12c;
  s->p130 = p130;
  s->p134 = p134;
  s->p138 = p138;
  s->power = power;
  return s;
}

}  // namespace ss::game::battle
