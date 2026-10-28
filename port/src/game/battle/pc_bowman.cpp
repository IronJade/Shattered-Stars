// Bowman's party-character module (#2139, slot-0 link 0x801ee000-0x801f1f9b): the PartyUnit
// subclass (0x360 bytes, vtable 0x801f1dcc: slots 35, 36, 37, 45, 46 and three of its own,
// 47-49, which are moves 0-2), his 10 killer moves (states 0x10-0x19, table 0x801f1f44) and
// the Shot callbacks they spawn. Move ids (record +0xcc byte - 1); names from what the code
// does:
//   0 a punch wave (one, two or three blasts as the swing goes on)
//   1 a handful of pills thrown in an arc that may poison what they hit
//   2 a healing pill (heals himself)
//   3 a leap onto the target that may kill it outright
//   4 a jumping uppercut that also fires blasts from level 1
//   5 an orb thrown at the target (magic damage)
//   6 a vanishing dash: clones strike from four sides
//   7 pills that burst into falling shards
//   8 a tornado that carries enemies along
//   9 a charged punch (near) or a flying fist (far)
#include <cstring>

#include "game/battle/battle.h"
#include "game/battle/bobj.h"
#include "game/battle/bsfx.h"
#include "game/battle/camera.h"
#include "game/battle/combat.h"
#include "game/battle/enemy_support.h"
#include "game/battle/killer.h"
#include "game/battle/particle.h"
#include "game/battle/pc_modules.h"
#include "game/battle/render.h"
#include "game/battle/shot.h"
#include "game/battle/sprite.h"
#include "game/battle/unit_move.h"
#include "game/boot/rng.h"

namespace ss::game::battle {
namespace {

constexpr int kBowman = 4;
const s32 kZero[3] = {0, 0, 0};  // 0x8006e33c
constexpr u32 kTrailRgb = 0x83060;

u8 module_u8(u32 address) { return pc_module_u8(kBowman, address); }
u32 rng_next15() { return static_cast<u32>(rng_next()) & 0x7FFF; }
s16 st_s16(const Unit* u, int off) {
  s16 v;
  std::memcpy(&v, u->stats + off, 2);
  return v;
}
void st_set_s16(Unit* u, int off, s16 v) { std::memcpy(u->stats + off, &v, 2); }
bool frame_is_end(int f) { return static_cast<s16>(f) == -1; }
void copy_vel_to_facing(BObj* o) {
  o->facing[0] = o->vel[0];
  o->facing[1] = o->vel[1];
  o->facing[2] = o->vel[2];
}
void set_vel(BObj* o, const Vec4& v) {
  o->vel[0] = v.x;
  o->vel[1] = v.y;
  o->vel[2] = v.z;
  o->extra34 = v.w;
}

// ---- Shot callbacks ------------------------------------------------------------------------

// 0x801f02e8 (moves 0 and 4): a punch blast flying straight on; it fades after 0x14 ticks
// and bursts on its first hit (debris CLUT 4, or 5 for move 4's).
void cb_blast(Shot* s) {
  BattleGlobals& g = bg();
  s->size[2] = s->owner->size[2];
  if (!frame_is_end(shot_draw_with_shadow(s, 5, 0xFFFFFFFF))) {
    if (g.flags != 0) return;
    shot_fade_out(s);
    if (s->p12c == 0 && 0x14 < s->p134) s->p12c = 1;
    bobj_move(s, s->vel);
    s->p134 += g.frame_ticks;
    copy_vel_to_facing(s);
    if (s->hit_landed != 0) return;
    if (shot_hit_melee(s, 0x100, 0) == 0) return;
    const s32 spread[3] = {8, 8, 0x10};
    effect_debris_burst(s->pos, spread, 3, 2, 0, 0x1000, static_cast<u8>(s->p130 == 0 ? 4 : 5));
  }
  shot_kill(s);
}

// 0x801f0438 (move 1): a pill falling in an arc (+0x130 0); on the ground it bursts (anim
// +0x128 + 2) and what it hits may be poisoned (resist roll 1, chance 0x32: status bit 3).
void cb_pill(Shot* s) {
  BattleGlobals& g = bg();
  s->size[2] = 0x18;
  if (s->p130 == 0) {
    shot_draw_with_shadow(s, 5, 0xFFFFFFFF);
  } else {
    s->loop_limit = 1;
    if (frame_is_end(shot_draw_with_shadow(s, 5, 0xFFFFFFFF))) {
      shot_kill(s);
      return;
    }
  }
  if (g.flags != 0) return;
  shot_fade_out(s);
  if (s->p130 == 0) {
    bobj_move(s, s->vel);
    s->vel[1] += g.frame_ticks * 0x1000;
    const s32 ground = bobj_ground_height(s);
    if (s->pos[1] <= ground) return;
    bsfx_play_at(0x23, s);
    s->pos[1] = ground;
    s->p130 = 1;
    bobj_anim_reset(s);
    return;
  }
  s->anim = static_cast<u16>(s->p128 + 2);
  if (s->hit_landed != 0) return;
  if (shot_hit_melee(s, 0x100, 0) == 0) return;
  Unit* found[8] = {};
  Vec4 pts[8] = {};
  const int n = collider_attack_overlaps(s, found, pts, 0);
  for (int i = 0; i < n; ++i) {
    if (found[i]->resist_roll(1, 0x32) != 0) found[i]->stats[2] |= 8;
  }
  shot_kill(s);
}

// 0x801f062c (move 2): the healing pill tossed up beside its owner; when it comes down the
// owner (if standing) gets sparkles, a rising cloud and (power * 2 + 100) HP.
void cb_heal(Shot* s) {
  BattleGlobals& g = bg();
  Unit* o = s->owner;
  s->pos[0] = o->pos[0] + (0 < s->p130 ? 0x40000 : -0x40000);
  s->anim = 1;
  s->pos[2] = o->pos[2];
  shot_draw_with_shadow(s, 4, 0xFFFFFFFF);
  if (g.flags != 0) return;
  shot_fade_out(s);
  bobj_move(s, s->vel);
  s->vel[1] += g.frame_ticks * 0x1000;
  if (s->pos[1] <= bobj_ground_height(s)) return;
  if (!unit_is_down(o) && o->st()->hp != 0) {
    int n = (s->power >> 4) + 4;
    if (0xC < n) n = 0xC;
    for (int i = 0; i < n; ++i) {
      if (Sprite* sp = shot_spawn_sprite_at(s, 3, 1)) {
        sprite_set_pos_v(sp, s->pos);
        set_vel(sp, vec_random_in_disc_xz(kZero, 0xC));
      }
    }
    for (int i = 0; i < n + 2; ++i) {
      if (Sprite* sp = sprite_spawn_from(0x2A, 0, g.sprite_bank_fx.get(), nullptr)) {
        const Vec4 p = vec_random_in_disc_xz(o->pos, 0x60);
        sprite_set_pos_v(sp, p.data());
        sprite_set_fade(sp, 0x80, -2, 10);
        sp->scale_y = 0x1800;
        sp->scale_x = 0x1800;
        sp->accel[1] = rng_between(-0x200, -0x180);
      }
    }
    int m = (s->power >> 4) + 0xC;
    if (0x18 < m) m = 0x18;
    for (int i = 0; i < m; ++i) {
      if (Sprite* sp = shot_spawn_sprite_at(s, rng_between(4, 8), 3)) {
        sprite_set_pos_v(sp, s->pos);
        sp->vel[0] = rng_spread_fixed(8);
        sp->vel[1] = rng_between(-0x8000, -0x6000);
        sp->vel[2] = rng_spread_fixed(8);
        sp->accel[1] = 0x200;
      }
    }
    effect_spawn_sparks_on(o, 0x10, 0x18, 0x50, 7, 0x3C, 4, 0, 0x1E, 0);
    effect_spawn_sparks_on(o, 0x10, 8, 0x80, -8, 0x37, 7, 0, 0xF0, 0);
    bsfx_play_at(0x16, s);
    const s32 heal = -(s->power * 2 + 100);
    damage_popup_spawn(o, heal, -0x2000, 0);
    unit_damage_hp(o, heal);
  }
  shot_kill(s);
}

// 0x801f09b4 (move 5): the orb flying at the target, shedding sparkles; its hit is magic
// (ATK * (power * 0x80 + 0x100) >> 8, element 4, knockback 0x10) with a burst of sprites.
void cb_orb(Shot* s) {
  BattleGlobals& g = bg();
  s->size[2] = 0x30;
  s->anim = 1;
  shot_draw_with_shadow(s, 4, 0x70FF);
  if (g.flags != 0) return;
  shot_fade_out(s);
  bobj_move(s, s->vel);
  if (Sprite* sp = shot_spawn_sprite(s, 3)) {
    const Vec4 p = vec_random_around(s->pos, static_cast<s32>(rng_range(0x60)));
    sprite_set_pos_v(sp, p.data());
    sprite_set_fade(sp, 0x80, -4, 8);
  }
  if (s->hit_landed != 0) return;
  const int sfx = s->power != 0 ? 0x50 : 0x23;
  const int power = st_s16(s->owner, 0x2E) * (s->power * 0x80 + 0x100) >> 8;
  if (shot_hit_magic(s, power, 0, sfx, 4, 0, 0x10) == 0) return;
  for (int i = 0; i < 8; ++i) {
    if (Sprite* sp = shot_spawn_sprite(s, s->power + 4)) {
      const s32 dx = rng_spread_fixed(0x60);
      const s32 dy = rng_spread_fixed(0x40);
      sprite_set_pos(sp, s->pos[0] + dx, s->pos[1] + dy, s->pos[2]);
      if (i < 4) {
        sprite_set_fade(sp, 0x80, -4, 8);
        const s32 sc = rng_between(0x1000, s->power * 0x1000 + 0x2000);
        sp->scale_y = sc;
        sp->scale_x = sc;
      } else {
        sprite_set_fade(sp, 0x80, -2, 8);
        set_vel(sp, vec_random_around(kZero, rng_between(4, 8)));
      }
    }
  }
  shot_kill(s);
}

// 0x801f0be4 (move 6): a clone running at the target from one side (+0x12c phase: 0
// running with the owner's model, 1 striking with its sprite set once past the target, 2
// fading out); a ribbon trail follows it. It ends with the owner's dash (action 0x16).
void cb_clone(Shot* s) {
  BattleGlobals& g = bg();
  Unit* o = s->owner;
  auto end = [s]() {
    if (s->trail != nullptr) {
      s->trail->stop = 1;
      s->trail->back = nullptr;
      s->trail = nullptr;
    }
    shot_kill(s);
  };
  if (o->action != 0x16) {
    end();
    return;
  }
  if (s->trail == nullptr) {
    s->trail = ribbon_trail_create(s, 0x18, 0x2080FF, 0x4080);
    if (s->trail != nullptr) s->trail->back = &s->trail;  // (port: the console keeps the pointer)
  }
  s->size[0] = o->size[0];
  s->size[1] = o->size[1];
  s->size[2] = o->size[2];
  auto running = [s, o]() {
    s->loop_limit = 0;
    s->anim = 1;
    s->layer = 0;
    s->animset = static_cast<const PartyUnit*>(o)->model_sets[0];
  };
  if (s->p12c == 1) {
    s->layer = 4;
    s->loop_limit = 1;
    bobj_share_sprite(o, s);
    s->anim = static_cast<u16>(s->p128 + 1);
  } else if (s->p12c == 0) {
    running();
  } else if (s->p12c == 2) {
    s->blend = 2;
    s->rgb[0] = static_cast<u8>(0x60 - s->p130);
    if (g.flags == 0) s->p130 += g.frame_ticks * 4;
    running();
  }
  if (frame_is_end(shot_draw_with_shadow(s, 4, 0xFFFFFFFF))) ++s->p12c;
  if (g.flags != 0) return;
  const s32 before = vec_distance_fx(s->target->pos, s->pos);
  if (s->p12c != 1) bobj_move(s, s->vel);
  s->pos[1] = bobj_ground_height(s);
  copy_vel_to_facing(s);
  const s32 after = vec_distance_fx(s->target->pos, s->pos);
  if (before < after && s->p12c == 0) {
    s->p12c = 1;
    bobj_anim_reset(s);
  }
  if (kStateFighting < g.state && s->p12c < 2) s->p12c = 2;
  if (s->hit_landed == 0 && shot_hit_melee(s, 0x100, 0) != 0) {
    const s32 spread[3] = {0x10, 0x10, 0x18};
    effect_debris_burst(s->pos, spread, 3, 1, 1, 0x2000, 0xD);
  }
  if (s->p130 < 0x61) return;
  end();
}

// 0x801f0efc (move 7): a pill thrown in an arc; on the ground (+0x130 1) it bursts (anim
// power + 5) and for its first frames rains shards around; hits with the owner's weapon
// element plus bit 2.
void cb_burst_pill(Shot* s) {
  BattleGlobals& g = bg();
  static_cast<PartyUnit*>(s->owner)->tex_timer = 8;
  s->layer = 1;
  s->size[2] = static_cast<s16>((s->power + 1) * 0x30);
  if (s->p130 == 0) {
    shot_draw_with_shadow(s, 5, 0xFFFFFFFF);
  } else {
    s->loop_limit = 1;
    if (frame_is_end(shot_draw_with_shadow(s, 5, 0xFFFFFFFF))) {
      shot_kill(s);
      return;
    }
  }
  if (g.flags != 0) return;
  shot_fade_out(s);
  if (s->p130 == 0) {
    bobj_move(s, s->vel);
    s->vel[1] += g.frame_ticks * 0x1000;
    const s32 ground = bobj_ground_height(s);
    if (ground < s->pos[1]) {
      bsfx_play_at(0x23, s);
      sound_handle_fade_out(bsfx_play_at(0x2D, s), 0x3C, 0x1E);
      s->pos[1] = ground;
      s->p130 = 1;
      if (Sprite* sp = shot_spawn_sprite(s, 3)) {
        sprite_set_pos_v(sp, s->pos);
        sprite_set_fade(sp, 0x80, -2, 4);
      }
      bobj_anim_reset(s);
    }
    return;
  }
  if (s->frame < 2) {
    for (int i = 0; i < 2; ++i) {
      const Vec4 v = vec_random_in_disc_xz(kZero, 0xC);
      const Vec4 p = vec_random_in_disc_xz(s->pos, (s->power + 1) * 0x80);
      if (Sprite* sp = shot_spawn_sprite_at(s, v.x < 0 ? 5 : 4, 1)) {
        const s32 h = rng_between(0x64000, 0x190000);
        sprite_set_pos(sp, p.x, s->pos[1] - h, p.z);
        set_vel(sp, v);
        sp->vel[1] = static_cast<s32>(0xFFFF8000);
        sp->accel[1] = 0x800;
      }
    }
  }
  s->anim = static_cast<u16>(s->power + 5);
  if (s->hit_landed != 0) return;
  const s16 saved = st_s16(s->owner, 0x5C);
  st_set_s16(s->owner, 0x5C, static_cast<s16>(saved | 4));
  shot_hit_melee(s, 0x100, 0);
  st_set_s16(s->owner, 0x5C, saved);
}

// 0x801f18d4 (move 8): a piece of debris whirling round the tornado's track (+0x118 its x,
// +0x120 z, +0x128 angle, +0x12c radius); it rises to y -0x1e0000 and falls back.
void cb_debris(Shot* s) {
  BattleGlobals& g = bg();
  s->loop_limit = 4;
  bobj_project(s);
  const int f = bobj_anim_draw(s, g.flags);
  if (frame_is_end(f) || (kStateFighting < g.state && g.end_countdown < 0x14)) {
    shot_kill(s);
    return;
  }
  if (g.flags != 0) return;
  const s32 t = g.frame_ticks;
  s->point[0] += s->vel[0] * t;
  if (s->pos[1] < -0x1DFFFF) {
    s->vel[1] += t * 0x100;
  } else {
    s->pos[0] = s->point[0] + bcos(static_cast<u32>(s->p128)) * s->p12c;
    s->vel[1] -= t * 0x100;
    s->pos[2] = s->point[2] + bsin(static_cast<u32>(s->p128)) * s->p12c;
    if (s->vel[1] < -0x4000) s->vel[1] = -0x4000;
  }
  s->p128 = (s->p128 + t * 0x14) & 0xFFF;
  s->pos[1] += s->vel[1] * t;
}

// 0x801f11a0 (move 8): the tornado: a funnel of six textured rings in two layers (radius
// power * 10 + (+0x10c) * 0xa0 / 256, +0x10c growing 8 a tick to 0x100) drifting on; it
// hits the enemies inside it four times (power 0x80, the owner's +0x52 raised by 7) and
// throws debris up.
void cb_tornado(Shot* s) {
  BattleGlobals& g = bg();
  bobj_anim_draw(s, g.flags | 0x9800u);
  const FrameUv f = frame_uv();
  const s32 radius = s->power * 10 + s->aux * 0xA0 / 256;
  if (f.v0 != f.v1) {
    const s32 uv[4] = {f.u0, f.v0, f.u1, f.v1};
    const u32 code = bobj_poly_tag(s, f.tpage);
    for (int layer = 0; layer < 2; ++layer) {
      const s32 base = layer == 0 ? radius / 2 : radius;
      const s32 n = layer + 1;
      const int segs = layer != 0 ? 0xC : 8;
      s32 top = 0x50000, bottom = 0;
      for (int ring = 0; ring < 6; ++ring, top += 0x50000, bottom += 0x50000) {
        const s32 r_in = ring * ring * n + base;
        const s32 r_out = (ring + 1) * (ring + 1) * n + base;
        s32 c[4][4] = {};
        const u32 a0 = static_cast<u32>(s->p128 * n);
        s32 cs = bcos(a0), sn = bsin(a0);
        c[2][0] = s->pos[0] + cs * r_in;
        c[2][1] = s->pos[1] - bottom;
        c[2][2] = s->pos[2] + sn * r_in;
        c[0][0] = s->pos[0] + cs * r_out;
        c[0][1] = s->pos[1] - top;
        c[0][2] = s->pos[2] + sn * r_out;
        for (int i = 1; i <= segs; ++i) {
          std::memcpy(c[3], c[2], sizeof c[3]);
          std::memcpy(c[1], c[0], sizeof c[1]);
          const u32 a = static_cast<u32>((i * 0x1000) / segs + s->p128 * n);
          cs = bcos(a);
          sn = bsin(a);
          c[2][0] = s->pos[0] + cs * r_in;
          c[2][2] = s->pos[2] + sn * r_in;
          c[0][0] = s->pos[0] + cs * r_out;
          c[0][2] = s->pos[2] + sn * r_out;
          draw_textured_patch(uv, c, f.clut, f.tpage, s->layer, code, 1);
        }
      }
    }
  }
  if (g.flags != 0) return;
  shot_fade_out(s);
  bobj_move(s, s->vel);
  const s32 t = g.frame_ticks;
  s->p128 = (s->p128 + t * 0x14) & 0xFFF;
  s->p130 += t;
  s->aux = static_cast<s16>(static_cast<u16>(s->aux) + t * 8);
  if (0x100 < s->aux) s->aux = 0x100;
  if (s->p12c == 0 && 0x5A < s->p130) s->p12c = 1;
  if (s->p134 == 0) s->p134 = static_cast<s32>(bsfx_play_at(0x2D, s));
  sound_handle_fade_out(static_cast<u32>(s->p134), 0x1E, 8);
  if (s->point[0] < 4) {
    Unit* hit[8] = {};
    Vec4 pts[8] = {};
    int n = 0;
    for (EnemyUnit* e : g.enemy_units) {
      bool inside = false;
      if (!unit_is_down(e)) inside = vec_distance_xz_fx(s->pos, e->pos) < (radius << 12);
      if (inside && -0x1E0000 < e->pos[1]) {
        hit[n] = e;
        pts[n].x = e->pos[0];
        pts[n].y = e->pos[1] - e->size[1] * 0x2000;
        pts[n].z = e->pos[2];
        ++n;
      }
    }
    if (n != 0) {
      ++s->point[0];
      const s16 saved = st_s16(s->owner, 0x52);
      st_set_s16(s->owner, 0x52, static_cast<s16>(saved + 7));
      unit_attack_targets(s->owner, n, hit, pts, 0x80, 0 <= s->facing[0] ? -0x38E : 0xB8E, 0);
      st_set_s16(s->owner, 0x52, saved);
    }
  }
  if (s->p12c == 0 && rng_next15() < 0x4000) {
    if (Shot* d = shot_spawn_callback(cb_debris)) {
      d->anim = static_cast<u16>(rng_range(4) + 1);
      d->p128 = static_cast<s32>(rng_range(0x1000));
      d->p12c = static_cast<s32>(rng_range(static_cast<u32>(radius)));
      d->vel[1] = -0x4000;
      d->vel[0] = s->vel[0];
      d->point[0] = s->pos[0];
      d->pos[1] = s->pos[1] - static_cast<s32>(rng_range(0x190000));
      d->point[2] = s->pos[2];
      bobj_share_sprite(s, d);
    }
  }
}

// 0x801f1a74 (move 9, far): the flying fist; it fades after 0x14 ticks; its hit (power
// (+0x10a) * 0x80 + 0x200) bursts into a ring and sprites.
void cb_fist(Shot* s) {
  BattleGlobals& g = bg();
  s->size[1] = 0x30;
  s->size[2] = static_cast<s16>((s->power + 1) * 0x20);
  if (!frame_is_end(shot_draw_with_shadow(s, 5, 0xFFFFFFFF))) {
    if (g.flags != 0) return;
    shot_fade_out(s);
    if (s->p12c == 0 && 0x14 < s->p134) s->p12c = 1;
    bobj_move(s, s->vel);
    s->p134 += g.frame_ticks;
    copy_vel_to_facing(s);
    if (s->hit_landed != 0) return;
    if (shot_hit_melee(s, s->power * 0x80 + 0x200, 0) == 0) return;
    if (Sprite* sp = shot_spawn_sprite(s, s->anim + 1)) {
      sprite_set_pos_v(sp, s->pos);
      sprite_set_fade(sp, 0x80, -4, 8);
      sp->scale_vel[1] = 0x80;
      sp->scale_vel[0] = 0x80;
    }
    const s32 centre[4] = {s->pos[0], s->pos[1] - s->size[1] * 0x2000, s->pos[2], 0};
    particle_spawn_ring(centre, 0x10, 0x38, 0x10, 0x2D, 0x103060, 0x2080FF, 0, 0x400);
    for (int i = 0; i < s->power * 4 + 4; ++i) {
      if (Sprite* sp = shot_spawn_sprite_at(s, static_cast<int>(rng_range(2)) + 7, 1)) {
        sprite_set_pos_v(sp, centre);
        set_vel(sp, vec_random_around(kZero, 8));
      }
    }
    for (int i = 0; i < s->power * 2 + 2; ++i) {
      if (Sprite* sp = shot_spawn_sprite(s, 0xB)) {
        sprite_set_fade(sp, 0x80, -4, 8);
        sprite_set_pos(sp, s->pos[0], bobj_ground_height(s) - 0x20000, s->pos[2]);
        set_vel(sp, vec_random_in_disc_xz(kZero, 8));
      }
    }
  }
  shot_kill(s);
}

// ---- The unit ---------------------------------------------------------------------------

struct BowmanUnit : PartyUnit {
  BowmanUnit() {  // 0x801ee0a0
    v2d8 = 1;
    v2da = 1;
    sfx_hit = 0x4C;
    sfx_crit = 0x4E;
    jump_threshold = static_cast<s32>(0xFFFBA000);
  }
  int killer_move_cost(int which) override {  // [35] 0x801ee100
    int total = 0;
    for (int c = 0; c < 2; ++c) {
      const u8 move = record[0xCC + c * 2 + which];
      if (move != 0) total += unit_mp_cost(this, *battle_data(0x8006B24B + move));
    }
    return total;
  }
  int move_mp_cost(int index) override {  // [36] 0x801ee198
    const u8 move = record[0xCC + (index >> 1) * 2 + (index & 1)];
    return unit_mp_cost(this, move == 0 ? 0 : *battle_data(0x8006B24B + move));
  }
  void killer_move_state(int i) override {  // [37] 0x801ee028 (table 0x801f1f44)
    switch (i) {
      case 0: move0(); break;
      case 1: move1(); break;
      case 2: move2(); break;
      case 3: move3(); break;
      case 4: move4(); break;
      case 5: move5(); break;
      case 6: move6(); break;
      case 7: move7(); break;
      case 8: move8(); break;
      case 9: move9(); break;
      default: break;
    }
  }
  int forced_attack_choice() override;  // [45] 0x801f01ec

  void move0();  // [47] 0x801ee1f8
  void move1();  // [48] 0x801ee404
  void move2();  // [49] 0x801ee610
  void move3();  // 0x801ee728
  void move4();  // 0x801eeee0
  void move5();  // 0x801ef284
  void move6();  // 0x801ef470
  void move7();  // 0x801ef994
  void move8();  // 0x801efb98
  void move9();  // 0x801efcf4

  void face_target() {
    if (target != nullptr) facing[0] = pos[0] < target->pos[0] ? 0x40000 : static_cast<s32>(0xFFFC0000);
  }
  void end_trail() {
    if (effects[0] != nullptr) effects[0]->ending = 1;
    effects[0] = nullptr;
  }
  // shot_place_at_unit_thunk 0x800553f0: shot_place_at_unit with dz 0.
  void place(Shot* s, s32 dx, s32 dy) { shot_place_at_unit(s, this, dx, dy, 0); }
};

// With the MP for it and HP below a quarter, a slot holding move byte 3 (the healing pill)
// is taken (none while waiting after a spell).
int BowmanUnit::forced_attack_choice() {
  if (wait_after_spell != 0) return -1;
  for (int which = 0; which < 2; ++which) {
    if (st()->mp < killer_move_cost(which)) continue;
    for (int c = 0; c < 2; ++c) {
      if (record[0xCC + c * 2 + which] != 3) continue;
      if (st()->hp < st()->hp_max / 4) return which;
    }
  }
  return -1;
}

// Move 0: the punch; blasts fly from frames 3, 7 and 11 (anim = the level: how many).
void BowmanUnit::move0() {
  BattleGlobals& g = bg();
  face_target();
  const int lvl = pc_move_charge_level(this, killer_index);
  loop_limit = 1;
  anim = static_cast<u16>(lvl);
  if (g.flags == 0) {
    int shot = 0;
    camera_follow_start();
    switch (counters[1]) {
      case 0:
        if (2 < frame) shot = 1;
        break;
      case 1:
        if (6 < frame) shot = 2;
        break;
      case 2:
        if (10 < frame) shot = 3;
        break;
      default:
        break;
    }
    if (shot != 0 && g.state == kStateFighting) {
      ++counters[1];
      if (Shot* s = shot_spawn_callback(cb_blast)) {
        s->pos[0] = pos[0] + facing[0];
        s->pos[1] = pos[1] - (shot == 2 ? 0x78000 : 0x50000);
        s->pos[2] = pos[2];
        s->vel[0] = bobj_facing_fx(this, 0x18);
        s->anim = static_cast<u16>(shot + 2);
        pc_bind_shot(this, s, static_cast<s16>(lvl));
        pc_camera_frame_target(this, 0x18);
        bsfx_play_at(0x23, s);
      }
    }
  }
  pc_attack_anim_step(this);
}

// Move 1: from frame 3, level + 2 pills in a fan (the first two straight ahead).
void BowmanUnit::move1() {
  BattleGlobals& g = bg();
  face_target();
  int lvl = pc_move_charge_level(this, killer_index);
  if (2000 < lvl) lvl = 2000;
  loop_limit = 1;
  anim = 0;
  if (g.flags == 0) {
    camera_follow_start();
    if (counters[1] == 0 && 2 < frame && g.state == kStateFighting) {
      counters[1] = 1;
      for (int i = 0; i < lvl + 2; ++i) {
        Shot* s = shot_spawn_callback(cb_pill);
        if (s == nullptr) continue;
        place(s, 0x40, 0x50);
        s->vel[0] = bobj_facing_fx(this, 0x10);
        s->vel[1] = rng_between(-0x9000, -0x5000);
        if (1 < i) {
          const u32 a = static_cast<u32>((facing[0] < 1 ? 0x800 : 0) + rng_spread(0x155));
          s->vel[0] = bcos(a) << 4;
          s->vel[2] = bsin(a) << 4;
        }
        s->p128 = static_cast<s32>(rng_range(static_cast<u32>(i + 2 < 5 ? i + 2 : 4)));
        s->anim = 1;
        pc_bind_shot(this, s, static_cast<s16>(lvl));
        if (i == 0) bsfx_play_at(0x1F, s);
      }
      pc_camera_frame_target(this, 0x18);
    }
  }
  pc_attack_anim_step(this);
}

// Move 2: from frame 3 the healing pill goes up beside him (0x78 ticks before he may
// cast a spell).
void BowmanUnit::move2() {
  BattleGlobals& g = bg();
  const int lvl = pc_move_charge_level(this, killer_index);
  loop_limit = 1;
  anim = 0;
  if (g.flags == 0) {
    camera_follow_start();
    if (counters[1] == 0 && 2 < frame && g.state == kStateFighting) {
      wait_after_spell = 0x78;
      counters[1] = 1;
      if (Shot* s = shot_spawn_callback(cb_heal)) {
        s->p130 = facing[0];
        s->vel[1] = static_cast<s32>(0xFFFE8000);
        s->pos[1] = pos[1] - 0x50000;
        pc_bind_shot(this, s, static_cast<s16>(lvl));
        bsfx_play_at(0x1F, s);
      }
    }
  }
  pc_attack_anim_step(this);
}

// Move 3: the leap (+0x178 phase): 0 takes off (near: an arc of speed 10 at the target's
// top; far: after anim 3 he appears 24.0 above it), 1 flies (landing on an enemy: 2; on
// anything else: 3), 2 the stomp: the target may die outright (resist roll 0, chance
// level * 2 + 20), else a hit with ATK * (level * 8 + 0x180) >> 8; 3 / 4 bounces off and
// falls (jump step 0x1000), 5 lands.
void BowmanUnit::move3() {
  BattleGlobals& g = bg();
  pc_camera_behind_start(this);
  anim = 0;
  loop_limit = 0;
  int lvl = pc_move_charge_level(this, killer_index);
  const s32 t = g.frame_ticks;
  switch (counters[1]) {
    case 0: {
      if (lvl / 6 != 0 && effects[0] == nullptr) effects[0] = afterimage_create(this, lvl / 6, kTrailRgb, 1);
      face_target();
      if (target_far != 0) {
        anim = 3;
        loop_limit = 1;
        if (static_cast<s16>(animate(static_cast<u32>(attack_recovery))) != -1) return;
        bsfx_play_at(0x34, this);
        if (target != nullptr) {
          pos[0] = target->pos[0];
          pos[1] = target->pos[1] + target->size[1] * -0x4000;
          pos[2] = target->pos[2];
          pos[1] -= 0x180000;
        }
        vel[0] = vel[1] = vel[2] = 0;
        counters[1] = 1;
        jumping = 1;
        airborne = 1;
        return;
      }
      if (airborne == 0) effect_spawn_sparks_at(pos, 0x10, 0xC, 0x30, 0x18, 0x28, 1, 1, 0x20, 0);
      for (int i = 0; i < 8; ++i) effect_dust_puff(pos, 2);
      bsfx_play_at(0x26, this);
      s32 v[4];
      counters[2] = projectile_arc_velocity(v, this, target, 10, 2) >> 12;
      vel[0] = v[0];
      vel[1] = v[1];
      vel[2] = v[2];
      extra34 = v[3];
      if (vel[1] < -0x60000) vel[1] = static_cast<s32>(0xFFFA0000);
      counters[1] = 1;
      jumping = 1;
      airborne = 1;
      break;
    }
    case 1: {
      anim = static_cast<u16>(target_far != 0 ? 4 : 0);
      counters[2] -= t;
      if (g.flags == 0) {
        s32 d[3] = {vel[0], 0, vel[2]};
        if (unit_try_move_scaled(this, d, 7) == nullptr) bobj_move(this, d);
        d[0] = 0;
        d[2] = 0;
        d[1] = vel[1];
        Unit* below = unit_try_move_scaled(this, d, 7);
        d[1] = vel[1];
        if (unit_try_move_scaled(this, d, 7) == nullptr) {
          bobj_move(this, d);
        } else if (0x3FFF < (vel[1] < 0 ? -vel[1] : vel[1])) {
          airborne = 2;
          if (below == nullptr || below == kWallBlock || below->is_enemy != 1) {
            counters[1] = 3;
          } else {
            counters[1] = 2;
            target = below;
            bobj_anim_reset(this);
          }
          if (target == nullptr) counters[1] = 3;
          vel[1] -= 0x2000;
        }
        vel[1] += t * 0x1000;
        if (bobj_ground_height(this) < pos[1]) {
          pos[1] = bobj_ground_height(this);
          bsfx_play_at(0x1E, this);
          for (int i = 0; i < 8; ++i) effect_dust_puff(pos, 4);
          airborne = 0;
          counters[1] = 5;
        }
      }
      break;
    }
    case 2: {
      Unit* tg = target;
      anim = 1;
      loop_limit = 1;
      if (tg != nullptr && hit_landed == 0 && tg->st()->id != 0xCD) tg->super_armour = 2;
      if (3 < frame && hit_landed == 0 && target != nullptr) {
        if (target->resist_roll(0, lvl * 2 + 0x14) == 0) {
          Unit* hit[8] = {target};
          Vec4 pts[8] = {};
          const s16 atk = st_s16(this, 0x2E);
          st_set_s16(this, 0x2E, static_cast<s16>(static_cast<u32>(atk * (lvl * 8 + 0x180)) >> 8));
          pts[0].x = target->pos[0];
          pts[0].y = target->pos[1] + target->size[1] * -0x4000;
          pts[0].z = target->pos[2];
          unit_attack_targets(this, 1, hit, pts, 0x100, 9999, 0);
          st_set_s16(this, 0x2E, atk);
        } else {
          target->death = 1;
          bsfx_play_at(0x33, target);
          bsfx_play_at(0x53, target);
          hit_landed = 1;
        }
        if (Sprite* sp = unit_spawn_sprite_ex(this, 6, 1)) {
          sprite_set_pos(sp, target->pos[0], target->pos[1] + size[1] * -0x4000 + 0x50000, target->pos[2]);
        }
      }
      break;
    }
    case 3:
      if (vel[0] == 0) {
        vel[0] = rng_range(2) != 0 ? static_cast<s32>(0xFFFF6000) : 0xA000;
      } else {
        vel[0] = vel[0] < 0 ? static_cast<s32>(0xFFFF6000) : 0xA000;
      }
      vel[1] = static_cast<s32>(0xFFFF7000);
      vel[2] = 0;
      counters[1] = 4;
      break;
    case 4:
      anim = 2;
      if (g.flags == 0) {
        const int r = pc_jump_step(this, 0x1000);
        if (r != 0) {
          counters[1] = 5;
          if (r == 2)
            for (int i = 0; i < 8; ++i) effect_dust_puff(pos, 4);
        }
      }
      break;
    case 5:
      end_trail();
      jumping = 0;
      action_kind = 0x12;
      begin_action();
      return;
    default:
      break;
  }
  if (g.state != kStateFighting) {
    model_default();
    anim = 2;
  }
  if (static_cast<s16>(animate(static_cast<u32>(attack_recovery))) == -1) ++counters[1];
}

// Move 4: the jumping uppercut (+0x17c: took off); hits on the way up (angle 0x9c7 /
// -0x1c7 rising, else 9999) with debris and hit-stop; from level 1 blasts at the frames of
// table 0x801f1f94.
void BowmanUnit::move4() {
  BattleGlobals& g = bg();
  if (counters[2] == 0 && 0 < frame) {
    face_target();
    counters[2] = 1;
    vel[1] = static_cast<s32>(0xFFFFC950);
    jumping = 1;
    airborne = 1;
  }
  loop_limit = 1;
  vel[2] = 0;
  const int lvl = pc_move_charge_level(this, killer_index);
  camera_follow_start();
  anim = static_cast<u16>(lvl != 0 ? 1 : 0);
  if (effects[0] == nullptr) effects[0] = afterimage_create(this, lvl != 0 ? 4 : 1, kTrailRgb, 1);
  if (g.flags == 0) {
    Unit* found[8] = {};
    Vec4 pts[8] = {};
    const int n = collider_attack_overlaps(this, found, pts, 0);
    bool check = true;
    if (n == 0) {
      check = hit_landed != 0;
    } else if (hit_landed == 0) {
      int angle = 9999;
      if (vel[1] < 0) angle = 0 <= facing[0] ? -0x1C7 : 0x9C7;
      unit_attack_targets(this, n, found, pts, 0x100, angle, 0);
      const s32 spread[3] = {0x10, 0x10, 8};
      effect_debris_burst(pts[0].data(), spread, 3, 2, 0, 0x1000, 5);
      if (bobj_on_screen(this)) g.hitstop_frames = 4;
      check = hit_landed != 0;
    }
    if (check && (box_mask & 0xC) == 0) {
      pc_voice_attack_shout(this);
      hit_landed = 0;
    }
  }
  vel[0] = jumping != 0 ? bobj_facing_fx(this, 10) : 0;
  if (pc_jump_step(this, 0xE0) == 2)
    for (int i = 0; i < 8; ++i) effect_dust_puff(pos, 4);
  if (lvl != 0 && g.flags == 0) {
    const int phase = counters[1];
    int shot = 0;
    if (phase < 3 && static_cast<int>(module_u8(0x801F1F94 + static_cast<u32>(phase))) <= frame) shot = phase + 1;
    if (shot != 0 && g.state == kStateFighting) {
      ++counters[1];
      if (Shot* s = shot_spawn_callback(cb_blast)) {
        s->p130 = 1;
        place(s, 0x40, 0x50);
        s->vel[0] = bobj_facing_fx(this, 0x18);
        s->anim = 2;
        pc_bind_shot(this, s, static_cast<s16>(lvl));
        pc_camera_frame_target(this, 0x18);
        bsfx_play_at(0x23, s);
      }
    }
  }
  pc_attack_anim_step(this);
}

// Move 5: past frame 30 the orb flies at the target's middle (speed 0x18).
void BowmanUnit::move5() {
  BattleGlobals& g = bg();
  face_target();
  const int lvl = pc_move_charge_level(this, killer_index);
  loop_limit = 1;
  anim = 0;
  if (g.flags == 0) {
    camera_follow_start();
    if (counters[1] == 0 && 0x1E < frame && g.state == kStateFighting && target != nullptr) {
      counters[1] = 1;
      if (Shot* s = shot_spawn_callback(cb_orb)) {
        place(s, 0x40, 0x50);
        const s32 aim[3] = {target->pos[0], target->pos[1] + target->size[1] * -0x2000, target->pos[2]};
        set_vel(s, vec_direction(s->pos, aim));
        s->vel[0] *= 0x18;
        s->vel[1] *= 0x18;
        s->vel[2] *= 0x18;
        pc_bind_shot(this, s, static_cast<s16>(lvl));
        pc_camera_frame_target(this, 0x1E);
        bsfx_play_at(0x34, s);
        bsfx_play_at(0x23, s);
      }
    }
  }
  pc_attack_anim_step(this);
}

// Move 6: the vanishing dash (+0x178 phase): 0 hops back, 1 fades out in the air and on
// landing sends level + 2 clones at the target from its four corners (the camera on the
// target), 2 stays hidden for 0x6e ticks, 3 fades back in.
void BowmanUnit::move6() {
  BattleGlobals& g = bg();
  const int lvl = pc_move_charge_level(this, killer_index);
  loop_limit = 0;
  switch (counters[1]) {
    case 0:
      face_target();
      model_default();
      anim = 2;
      vel[0] = bobj_facing_fx(this, -10);
      airborne = 1;
      jumping = 1;
      vel[1] = static_cast<s32>(0xFFFF7000);
      vel[2] = 0;
      counters[1] = 1;
      break;
    case 1:
      if (g.flags == 0) camera_follow_start();
      blend = 2;
      colour_mode = 2;
      rgb[0] = static_cast<u8>(0x80 - counters[2]);
      counters[2] += g.frame_ticks * 4;
      if (0x80 < counters[2]) counters[2] = 0x80;
      model_default();
      if (g.flags == 0 && pc_jump_step(this, 0x1000) != 0) {
        if (target != nullptr) {
          for (int i = 0; i < lvl + 2; ++i) {
            Shot* s = shot_spawn_callback(cb_clone);
            if (s == nullptr) continue;
            const s32 tx = target->pos[0], tz = target->pos[2];
            switch (i) {
              case 0: s->pos[0] = tx + 0x180000; s->pos[2] = tz + 0x180000; break;
              case 1: s->pos[0] = tx - 0x180000; s->pos[2] = tz - 0x180000; break;
              case 2: s->pos[0] = tx + 0x180000; s->pos[2] = tz - 0x180000; break;
              case 3: s->pos[0] = tx - 0x180000; s->pos[2] = tz + 0x180000; break;
              default: break;  // (the console leaves the pool slot's old position)
            }
            s->p128 = i;
            set_vel(s, vec_direction(s->pos, target->pos));
            const s32 k = run_speed >> 12;
            s->vel[0] *= k;
            s->vel[1] *= k;
            s->vel[2] *= k;
            pc_bind_shot(this, s, static_cast<s16>(lvl));
          }
        }
        if (g.controlled_slot == slot && g.camera_special == nullptr) {
          camera_reset(g.camera);
          camera_set_mode(g.camera, 7, target);
          camera_focus = 0;
        }
        counters[1] = 2;
        counters[2] = 0;
        bsfx_play_at(0x34, this);
      }
      break;
    case 2:
      bobj_texcache_begin_frame(this);
      if (g.flags == 0) counters[2] += g.frame_ticks;
      if (0x6E < counters[2]) {
        counters[2] = 0;
        counters[1] = 3;
      }
      jumping = 0;
      box_mask = 0;
      return;
    case 3:
      model_default();
      rgb[0] = static_cast<u8>(counters[2]);
      anim = 0;
      if (g.flags == 0) {
        counters[2] += g.frame_ticks * 4;
        camera_follow_start();
      }
      if (0x80 < counters[2]) {
        blend = 0;
        colour_mode = 0;
        action_kind = 0x12;
        begin_action();
      }
      break;
    default:
      break;
  }
  if (pc_attack_anim_step(this) != 0) {
    blend = 0;
    colour_mode = 0;
  }
}

// Move 7: from frame 3, level + 2 bursting pills in a fan (odd ones with a random sideways
// push).
void BowmanUnit::move7() {
  BattleGlobals& g = bg();
  face_target();
  const int lvl = pc_move_charge_level(this, killer_index);
  loop_limit = 1;
  anim = 0;
  if (g.flags == 0) {
    camera_follow_start();
    if (counters[1] == 0 && 2 < frame && g.state == kStateFighting) {
      counters[1] = 1;
      for (int i = 0; i < lvl + 2; ++i) {
        Shot* s = shot_spawn_callback(cb_burst_pill);
        if (s == nullptr) continue;
        place(s, 0x40, 0x50);
        s->vel[0] = bobj_facing_fx(this, 0x14);
        s->vel[1] = static_cast<s32>(0xFFFF8000);
        if (0 < i) {
          const u32 a = static_cast<u32>((facing[0] < 1 ? 0x800 : 0) + rng_spread(0x155));
          s->vel[0] = bcos(a) * 0x14;
          s->vel[2] = bsin(a) * 0x14;
        }
        if ((i & 1) != 0) s->vel[0] += rng_spread_fixed(8);
        s->anim = 1;
        pc_bind_shot(this, s, static_cast<s16>(lvl));
        if (i == 0) bsfx_play_at(0x1F, s);
      }
      pc_camera_frame_target(this, 0x18);
    }
  }
  bobj_texcache_reset(this);
  pc_attack_anim_step(this);
}

// Move 8: past frame 24 the tornado sets off (speed 8; the camera frames it for 0x5a).
void BowmanUnit::move8() {
  BattleGlobals& g = bg();
  face_target();
  const int lvl = pc_move_charge_level(this, killer_index);
  loop_limit = 6;
  anim = 0;
  if (g.flags == 0) {
    camera_follow_start();
    if (counters[1] == 0 && 0x18 < frame && g.state == kStateFighting) {
      counters[1] = 1;
      Shot* s = shot_spawn_callback(cb_tornado);
      if (s != nullptr) {
        place(s, 0x40, 0);
        s->vel[0] = bobj_facing_fx(this, 8);
        s->anim = 5;
        pc_bind_shot(this, s, static_cast<s16>(lvl));
      }
      bsfx_play_at(0x1F, s);
      pc_camera_frame_target(this, 0x5A);
    }
  }
  bobj_texcache_reset(this);
  pc_attack_anim_step(this);
}

// Move 9: the charged punch: steps in over frames 12-19 and back after; near targets take
// the punch itself (power level * 0x80 + 0x200) with a ring and sprites; far ones get the
// flying fist from frame 19.
void BowmanUnit::move9() {
  BattleGlobals& g = bg();
  face_target();
  bobj_texcache_reset(this);
  const int lvl = pc_move_charge_level(this, killer_index);
  loop_limit = 1;
  anim = static_cast<u16>(lvl);
  if (g.flags == 0) {
    vel[0] = 0;
    if (static_cast<u8>(static_cast<u8>(frame) - 0xC) < 8) vel[0] = bobj_facing_fx(this, 0x10);
    if (0x13 < frame) vel[0] = bobj_facing_fx(this, -8);
    if (unit_try_move_scaled(this, vel, 7) == nullptr) bobj_move(this, vel);
    if (target_far == 0) {
      Unit* found[8] = {};
      Vec4 pts[8] = {};
      const int n = collider_attack_overlaps(this, found, pts, 0);
      if (n != 0 && hit_landed == 0 &&
          unit_attack_targets(this, n, found, pts, lvl * 0x80 + 0x200, 9999, 0) != 0) {
        particle_spawn_ring(pts[0].data(), 0x10, 0x38, 0x10, 0x2D, 0x103060, 0x2080FF, 0, 0x400);
        for (int i = 0; i < lvl * 4 + 4; ++i) {
          if (Sprite* sp = unit_spawn_sprite_ex(this, static_cast<int>(rng_range(4)) + 7, 1)) {
            sprite_set_pos_v(sp, pts[0].data());
            set_vel(sp, vec_random_around(kZero, 8));
          }
        }
        for (int i = 0; i < lvl * 2 + 2; ++i) {
          if (Sprite* sp = unit_spawn_sprite(this, static_cast<int>(rng_range(2)) + 0xB)) {
            sprite_set_fade(sp, 0x80, -4, 8);
            sprite_set_pos(sp, pts[0].x, bobj_ground_height(this) - 0x20000, pts[0].z);
            set_vel(sp, vec_random_in_disc_xz(kZero, 8));
          }
        }
      }
    } else if (counters[1] == 0 && 0x12 < frame && g.state == kStateFighting) {
      counters[1] = 1;
      if (Shot* s = shot_spawn_callback(cb_fist)) {
        place(s, 0x60, 0);
        s->vel[0] = bobj_facing_fx(this, 0x18);
        s->anim = static_cast<u16>(lvl + 3);
        pc_bind_shot(this, s, static_cast<s16>(lvl));
        pc_camera_frame_target(this, 0x18);
        bsfx_play_at(0x23, s);
      }
    }
  }
  pc_attack_anim_step(this);
}

}  // namespace

std::unique_ptr<PartyUnit> pc_bowman_create() { return std::make_unique<BowmanUnit>(); }

}  // namespace ss::game::battle
