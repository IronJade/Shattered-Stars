// The #2129 image's default enemy-code module in slot B (0x80089f18-0x8008b1f0, 4824
// bytes, vtable 0x8008b0c8): Nicolus 0xc9 (0x26c-byte objects; encounter 536 loads no slot
// B code and uses it). Attack 1 charges (sparks drawn in for 30 ticks) and sends a funnel
// of light along the ground that grinds whoever stands in it while he holds the pose;
// attack 2 sweeps two beams from his hands back and forth for 120 ticks.

#include "game/battle/bsfx.h"
#include "game/battle/combat.h"
#include "game/battle/enemy_classes.h"
#include "game/battle/enemy_support.h"
#include "game/battle/render.h"
#include "game/battle/shot.h"
#include "game/battle/sprite.h"
#include "game/boot/rng.h"

namespace ss::game::battle {
namespace {

u32 rng15() { return static_cast<u32>(rng_next() & 0x7FFF); }  // rng_next15 0x800103b4

// The unit's centre (its body's mid height).
Vec4 centre_of(const Unit* u) { return {u->pos[0], u->pos[1] + u->size[1] * -0x2000, u->pos[2], 0}; }

void set_vel(BObj* o, const Vec4& v) {
  o->vel[0] = v.x;
  o->vel[1] = v.y;
  o->vel[2] = v.z;
}

void shot_vortex(Shot* s);

struct EnemyNicolus : EnemyUnit {
  // 0x80089f18: enemy_create and the module's vtable (nothing else).

  // 0x80089f50 [32]: frees the streamed-cel cache entries every frame, then thinks as usual.
  int pre_think() override {
    bobj_texcache_reset(this);
    return 0;
  }

  // 0x80089f70 [26]: attack 2 (the beams) from up to 500 away; its sweep starts at -+0x155.
  // (+0x174 ticks, +0x178 / +0x17c the two beam angles, +0x180 the hit timer.)
  void choose_target() override {
    if (enemy_try_approach(this, 500, 0x2000, 2, 0x140) == 0) {
      EnemyUnit::choose_target();
      return;
    }
    counters[1] = -0x155;
    counters[0] = 0;
    counters[2] = 0x155;
    counters[3] = 0;
  }

  // 0x80089fd4 [27]: the funnel (+0x174 step, +0x178 ticks, +0x17c the charge sound): 0 a
  // flash at the hand and the rising charge sound; 1 sparks fly into the hand, at 30 ticks
  // the funnel goes off along the ground toward the target; 2 holds 70 ticks, then idle.
  void attack1() override {
    BattleGlobals& g = bg();
    anim = 4;
    loop_limit = 0;
    animate(0);
    const s32 hand[3] = {bobj_front_x(this, 0x40), pos[1] - 0x80000, pos[2]};
    if (g.flags != 0) return;
    if (counters[0] == 1) {
      if (Sprite* sp = unit_spawn_sprite_ex(this, 9, 1)) {
        const s32 at[3] = {bobj_front_x(this, 0x40), pos[1] - 0x80000, pos[2]};
        const Vec4 p = vec_random_around(at, 0x1E0);
        sprite_set_pos_v(sp, p.data());
        set_vel(sp, vec_velocity_toward(sp->pos, at, 0xC));
      }
      counters[1] += g.frame_ticks;
      if (counters[1] < 0x1E) return;
      const u32 h = bsfx_play_at(0x46, this);
      counters[2] = static_cast<s32>(h);
      sound_handle_fade_out(h, 0x1E, 8);
      counters[1] = 0;
      counters[0] = 2;
      if (target == nullptr) return;
      Shot* s = shot_spawn_callback(shot_vortex);
      if (s == nullptr) return;
      shot_place_at_unit(s, this, 0x30, 0x80, 0);
      set_vel(s, vec_velocity_toward_xz(pos, target->pos, 0x30));
      s->owner = this;
      bobj_share_sprite(this, s);
      bsfx_play_at(0x34, s);
    } else if (counters[0] == 0) {
      if (Sprite* sp = unit_spawn_sprite_ex(this, 0xF, 0)) {
        sprite_set_pos_v(sp, hand);
        sprite_set_fade(sp, 0x80, -4, 0x1E);
      }
      counters[0] = 1;
      const u32 h = bsfx_play_at(0x45, this);
      sound_handle_pitch_up(h, 0x5A);
      sound_handle_fade_out(h, 0x3C, 0x1E);
    } else if (counters[0] == 2) {
      counters[1] += g.frame_ticks;
      sound_handle_fade_out(static_cast<u32>(counters[2]), 0x1E, 8);
      if (0x46 < counters[1]) {
        action_kind = 0;
        begin_action();
      }
    }
  }

  // 0x8008a2a0 [28]: two beams (draw_beam 0x1e0 long, radius 8, 3 sides) from the hands
  // 0x10 and 0x60 in front, z -+0x20; every 4 ticks sprites at the tips and the two attack
  // boxes (+-0x80 round the tips) hit; the angles sweep 6 a tick, turning every 30 ticks;
  // 120 ticks, then idle (the sweep still runs that frame).
  void attack2() override {
    BattleGlobals& g = bg();
    anim = 5;
    loop_limit = 0;
    animate(0);
    s32 from[2][4] = {};
    s32 dir[2][4] = {};
    for (int i = 0, ahead = 0x10; i < 2; ++i, ahead += 0x50) {
      from[i][1] = pos[1] - 0xB4000;
      const u32 a = static_cast<u32>(i == 0 ? counters[1] : counters[2]);
      dir[i][0] = bcos(a);
      dir[i][1] = 0x511;
      dir[i][2] = bsin(a);
      from[i][0] = bobj_front_x(this, ahead);
      if (facing[0] < 0) dir[i][0] = -dir[i][0];
    }
    from[0][2] = pos[2] - 0x20000;
    from[1][2] = pos[2] + 0x20000;
    for (int i = 0; i < 2; ++i) draw_beam(from[i], dir[i], 0x20, 0xC000FF, 0x1E0, 8, 8, 3);
    if (g.flags != 0) return;
    s32 tip[2][4] = {};
    for (int i = 0; i < 2; ++i)
      for (int k = 0; k < 3; ++k) tip[i][k] = from[i][k] + dir[i][k] * 0x1E0;
    counters[3] += g.frame_ticks;
    if (3 < counters[3]) {
      counters[3] -= 4;
      for (int i = 0; i < 2; ++i) {
        Sprite* sp = unit_spawn_sprite(this, 0xD);
        if (sp == nullptr) continue;
        sprite_set_pos_v(sp, tip[i]);
        sprite_set_fade(sp, 0x80, -4, 0x10);
        sp->layer = 4;
        const s32 sc = rng_between(0x1000, 0x2000);
        sp->scale_y = sc;
        sp->scale_x = sc;
        if (rng15() < 0x1000) bsfx_play_at(0x50, sp);
      }
      box_mask |= 0xC;
      for (int i = 0; i < 2; ++i) {
        attack_min[i][0] = tip[i][0] - 0x80000;
        attack_min[i][1] = tip[i][1] - 0x80000;
        attack_min[i][2] = tip[i][2] - 0x80000;
        attack_max[i][0] = tip[i][0] + 0x80000;
        attack_max[i][1] = tip[i][1] + 0x80000;
        attack_max[i][2] = tip[i][2] + 0x80000;
      }
      Unit* units[8] = {};
      Vec4 points[8];
      const int n = collider_attack_overlaps(this, units, points, 1);
      if (n != 0) unit_attack_targets(this, n, units, points, 0x100, 9999, 0);
    }
    counters[0] += g.frame_ticks;
    if (0x78 < counters[0]) {
      action_kind = 0;
      begin_action();
    }
    if (((counters[0] / 0x1E) & 1) == 0) {
      counters[2] += g.frame_ticks * 6;
      counters[1] -= g.frame_ticks * 6;
    } else {
      counters[2] -= g.frame_ticks * 6;
      counters[1] += g.frame_ticks * 6;
    }
  }

  // 0x8008aee0 [25]: once: 48 beams out of the body, a spark ring at its centre and one on
  // the ground, two sounds; then the usual fade.
  void state_dying() override {
    if (counters[0] == 0) {
      for (int i = 0; i < 0x30; ++i) {
        const Vec4 c = centre_of(this);
        const Vec4 d = vec_random_around(c.data(), 0x500);
        const Vec4 from = centre_of(this);
        effect_spawn_beam_at(from.data(), d.data(), 7, 0x140, 0x18, 0x4000, 0x400, 0);
      }
      const Vec4 c = centre_of(this);
      effect_spawn_sparks_at(c.data(), 0x14, 0x60, 0, 0x60, 0x3C, 7, 3, 0x40, 1);
      const s32 ground[3] = {pos[0], bobj_ground_height(this), pos[2]};
      effect_spawn_sparks_at(ground, 0x14, 0xC0, 0x40, 0x40, 0x5A, 7, 0, 0x40, 1);
      bsfx_play_at(0x21, this);
      bsfx_play_at(0x22, this);
      counters[0] = 1;
    }
    EnemyUnit::state_dying();
  }

  // 0x8008b0a0 [36]: the dtor (EnemyUnit vtable, unit_destroy).
};

// 0x8008a7bc: attack 1's funnel (a kind-0x33 Shot): 6 rings x 8 segments of textured
// quads (anim 6's cel) along x in front of it (0xa0 per ring), radius growing with +0x10c
// (ring 0 from radius r * s / 0xa00, then r * s >> 8, r = s * 0xa0 / 0x100), turning by
// +0x12c (ring i offset i * i). While the owner holds attack 1 (state 9, fighting, the
// first 70 ticks of +0x130) +0x10c grows 16 a tick to 0x100, sprites flash at both ends
// every 12 ticks (+0x128), sparks at random, and every 4 ticks (+0x134) the owner hits the
// standing members within 0xc8 (y-z distance) inside its x span (power 0x80, knocked along
// its facing). Then +0x10c shrinks 16 a tick and it dies below 0.
void shot_vortex(Shot* s) {
  BattleGlobals& g = bg();
  s->layer = 6;
  s->anim = 6;
  bobj_anim_draw(s, g.flags | 0x9800u);
  const FrameUv& f = frame_uv();
  const s32 uv[4] = {f.u0, f.v0, f.u1, f.v1};
  const u16 tpage = f.tpage, clut = f.clut;
  const s32 r = (s->aux * 0xA0) / 0x100;
  if (uv[1] != uv[3]) {
    const u32 code = bobj_poly_tag(s, tpage);
    // (The console builds the corners in the scratchpad at 0x1f8003c0.)
    s32 c[4][4] = {};
    s32 radius[2];
    radius[0] = (r * s->aux) / 0xA00;
    for (int ring = 0; ring < 6; ++ring) {
      radius[1] = (r * s->aux) >> 8;
      const s32 base = s->p12c + ring * ring;
      const s32 cs = bcos(static_cast<u32>(base)), sn = bsin(static_cast<u32>(base));
      for (int k = 0, x = ring * 0xA0; k < 2; ++k, x += 0xA0) {
        s32* p = c[1 - k];
        p[0] = bobj_front_x(s, x);
        p[1] = s->pos[1] + cs * radius[k];
        p[2] = s->pos[2] + sn * radius[k];
      }
      for (int seg = 0, a = 0x200; seg < 8; ++seg, a += 0x200) {
        const s32 ang = a + s->p12c + ring * ring;
        const s32 cs2 = bcos(static_cast<u32>(ang)), sn2 = bsin(static_cast<u32>(ang));
        for (int k = 0; k < 2; ++k) {
          c[k + 2][0] = c[k][0];
          c[k + 2][1] = c[k][1];
          c[k + 2][2] = c[k][2];
        }
        for (int k = 0; k < 2; ++k) {
          c[1 - k][1] = s->pos[1] + cs2 * radius[k];
          c[1 - k][2] = s->pos[2] + sn2 * radius[k];
        }
        draw_textured_patch(uv, c, clut, tpage, s->layer, code, 1);
      }
      radius[0] = radius[1];
    }
  }
  if (g.flags != 0) return;
  s->p12c = (s->p12c + g.frame_ticks * 0x28) & 0xFFF;
  s->p130 += g.frame_ticks;
  if (!(s->p130 < 0x47 && g.state == kStateFighting && s->owner->action == 9)) {
    s->aux = static_cast<s16>(static_cast<u16>(s->aux) - g.frame_ticks * 0x10);
    if (s->aux < 0) shot_kill(s);
    return;
  }
  s->aux = static_cast<s16>(static_cast<u16>(s->aux) + g.frame_ticks * 0x10);
  if (0x100 < s->aux) s->aux = 0x100;
  Vec4 points[8] = {};
  s32 lo = s->pos[0], hi = s->pos[0];
  if (s->facing[0] < 0) {
    lo = hi - 0x3C0000;
  } else {
    hi = lo + 0x3C0000;
  }
  s->p128 += g.frame_ticks;
  if (0xC < s->p128) {
    s->p128 -= 0xC;
    for (int i = 0; i < 2; ++i) {
      Sprite* sp = shot_spawn_sprite(s, i != 0 ? 0xE : 8);
      if (sp == nullptr) continue;
      if (i == 0) {
        sprite_set_pos_v(sp, s->pos);
      } else {
        sprite_set_pos(sp, bobj_front_x(s, 0x3C0), s->pos[1], s->pos[2]);
      }
      sprite_set_fade(sp, 0x80, -4, 8);
      sp->scale_y = 0x1800;
      sp->scale_x = 0x1800;
      sp->layer = 10;
    }
  }
  if (rng15() < 0x4000) {
    if (Sprite* sp = shot_spawn_sprite(s, 0xC)) {
      const s32 x = bobj_front_x(s, static_cast<s32>(rng_range(0x3C0)));
      const s32 y = s->pos[1] + rng_spread_fixed(0x80);
      const s32 z = s->pos[2] + rng_spread(0x140);  // (not shifted: a tiny z offset)
      sprite_set_pos(sp, x, y, z);
      sprite_set_fade(sp, 0x80, -2, 0x10);
      const s32 sc = rng_between(0x1000, 0x2000);
      sp->scale_y = sc;
      sp->scale_x = sc;
      sp->layer = 3;
    }
  }
  s->p134 -= g.frame_ticks;
  if (s->p134 >= 0) return;
  s->p134 += 4;
  Unit* units[8] = {};
  int n = 0;
  for (PartyUnit* u : g.party_units) {
    if (unit_is_down(u)) continue;
    if (u->pos[0] < lo || hi <= u->pos[0]) continue;
    const s32 p[3] = {s->pos[0], u->pos[1], u->pos[2]};
    if (0xC7FFF < vec_distance_fx(p, s->pos)) continue;
    units[n] = u;
    points[n] = centre_of(u);
    ++n;
  }
  // (Called even when nobody was met.)
  unit_attack_targets(s->owner, n, units, points, 0x80, s->facing[0] < 0 ? 0x800 : 0, 0);
}

}  // namespace

std::unique_ptr<EnemyUnit> enemy_mod_nicolus_create() { return std::make_unique<EnemyNicolus>(); }

}  // namespace ss::game::battle
