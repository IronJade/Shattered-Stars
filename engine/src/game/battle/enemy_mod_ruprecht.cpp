// Ruprecht's enemy-code module: block e66c457273d3 (4304 bytes, slot A linked at 0x80088110),
// vtable 0x800890b8, monster type 0xc8 (Ruprecht), encounter 536. A warping sorcerer: it
// moves by vanishing and reappearing beside its target, casts a ground wave that runs at
// the target segment by segment, and a horizontal whirlwind 0x300 long in front of it that
// keeps hitting the party in it while the cast lasts.

#include <cstring>

#include "game/battle/bsfx.h"
#include "game/battle/combat.h"
#include "game/battle/enemy_classes.h"
#include "game/battle/enemy_modules.h"
#include "game/battle/enemy_support.h"
#include "game/battle/render.h"
#include "game/battle/shot.h"
#include "game/battle/sprite.h"
#include "runtime/libgte.h"

namespace ss::game::battle {
namespace {

// The target's centre (its body's mid height).
Vec4 centre_of(const Unit* u) { return {u->pos[0], u->pos[1] + u->size[1] * -0x2000, u->pos[2], 0}; }

void shot_wave(Shot* s);
void shot_whirlwind(Shot* s);

struct EnemyRuprecht : EnemyUnit {
  // 0x80088110: enemy_create and the module's vtable.

  // 0x80088148 [32]: drops its streamed-cel cache every frame (bobj_texcache_reset).
  int pre_think() override {
    bobj_texcache_reset(this);
    return 0;
  }

  // 0x80088168 [26]: the whirlwind (2) from 500 away (nearer a quarter of the time), else
  // the wave (1) cast at once where it stands (the 0x8000 chance always passes); no
  // default approach.
  void choose_target() override {
    if (enemy_try_approach(this, 500, 0x2000, 2, 0x140) != 0) return;
    if (enemy_try_approach(this, 500, 0x8000, 1, 0x140) != 0) {
      action_kind = 1;
      begin_action();
    }
  }

  // 0x800881ec [27]: a swing (enemy_attack_step) that from frame 1 sends the ground wave at
  // the target once (speed 0x30 on the ground plane).
  void attack1() override {
    anim = 4;
    loop_limit = 1;
    enemy_attack_step(this);
    if (bg().flags != 0 || counters[1] != 0 || frame <= 0 || bg().state != kStateFighting || target == nullptr)
      return;
    if (Shot* s = shot_spawn_callback(shot_wave)) {
      shot_place_at_unit(s, this, 0x40, 0, 0);
      const Vec4 v = vec_velocity_toward_xz(pos, target->pos, 0x30);
      std::memcpy(s->vel, &v, sizeof(s->vel));
      s->owner = this;
      bobj_share_sprite(this, s);
      bsfx_play_at(0x34, s);
    }
    counters[1] = 1;
  }

  // 0x800882ec [28]: 8 loops of the cast (enemy_attack_step). From frame 1 the whirlwind
  // once (also while paused), then every 16 unpaused ticks a ring of sparks (anim 0xc)
  // drifting off its hand.
  void attack2() override {
    BattleGlobals& g = bg();
    anim = 5;
    loop_limit = 8;
    enemy_attack_step(this);
    if (counters[1] == 0 && frame > 0 && g.state == kStateFighting && target != nullptr) {
      if (Shot* s = shot_spawn_callback(shot_whirlwind)) {
        shot_place_at_unit(s, this, 0x30, 0x80, 0);
        s->facing[0] = facing[0];
        const Vec4 v = vec_velocity_toward_xz(pos, target->pos, 0x30);
        std::memcpy(s->vel, &v, sizeof(s->vel));
        s->owner = this;
        bobj_share_sprite(this, s);
        bsfx_play_at(0x34, s);
      }
      counters[1] = 1;
    }
    if (g.flags != 0 || counters[1] == 0 || g.state != kStateFighting) return;
    counters[0] += g.frame_ticks;
    if (counters[0] < 0x10) return;
    counters[0] -= 0x10;
    const s32 hand[3] = {bobj_front_x(this, 0x30), pos[1] - 0x80000, pos[2]};
    if (Shot* ring = effect_spawn_sparks_at(hand, 0xE, 0x90, 0, 6, 0x5A, 0, 2, 0x60, 1)) {
      ring->vel[1] = bobj_facing_fx(this, 9);
      ring->vel[2] = 0x400;
      ring->anim = 0xC;
      bobj_share_sprite(this, ring);
    }
  }

  // 0x800884d8 [23]: the warp instead of walking: the vanish (anim 1) to its end, then (sfx
  // 0x26) it stands at an approach point by the target on the ground and reappears (anim 6;
  // +0x13e marks the second half); at that end slot 12. No point: action kind 0.
  void state_approach() override {
    anim = detour != 0 ? 6 : 1;
    loop_limit = 1;
    if (static_cast<s16>(animate(0)) != -1) return;
    if (detour != 0) {
      begin_action();
      return;
    }
    bsfx_play_at(0x26, this);
    bobj_anim_reset(this);
    detour = 1;
    Vec4 p;
    if (unit_find_approach_point(this, &p)) {
      pos[0] = p.x;
      pos[2] = p.z;
      pos[1] = bobj_ground_height(this);
    } else {
      action_kind = 0;
    }
  }

  // 0x80088ed4 [25]: once: 48 beams out of its centre, a ring at its centre and one at its
  // feet; then the usual fade.
  void state_dying() override {
    if (counters[0] == 0) {
      for (int i = 0; i < 0x30; ++i) {
        const Vec4 c = centre_of(this);
        const Vec4 d = vec_random_around(c.data(), 0x500);
        const Vec4 c2 = centre_of(this);
        effect_spawn_beam_at(c2.data(), d.data(), 3, 0x140, 0x18, 0x4000, 0x400, 0);
      }
      const Vec4 c = centre_of(this);
      effect_spawn_sparks_at(c.data(), 0x14, 0x60, 0, 0x60, 0x3C, 0xE, 3, 0x40, 1);
      const s32 feet[3] = {pos[0], bobj_ground_height(this), pos[2]};
      effect_spawn_sparks_at(feet, 0x14, 0xC0, 0x40, 0x40, 0x5A, 5, 0, 0x40, 1);
      bsfx_play_at(0x21, this);
      bsfx_play_at(0x22, this);
      counters[0] = 1;
    }
    EnemyUnit::state_dying();
  }
};

// 0x800885cc: a segment of the ground wave (anim 0xb once, on the ground): it does not
// move; after 4 ticks it spawns the next segment 4 ticks of its velocity ahead (passing
// on the velocity, the stop flag, the owner and the landed flag) until one leaves the
// arena; each hits the party once (shot_hit_melee 0x100). (shot_fade_out's result is
// ignored: a segment it kills still runs the rest of the frame.)
void shot_wave(Shot* s) {
  s->anim = 0xB;
  s->loop_limit = 1;
  s->layer = 2;
  s->size[2] = s->owner->size[2];
  if (static_cast<s16>(shot_draw_and_box(s, 6, 0xFFFFFFFF)) == -1) {
    shot_kill(s);
    return;
  }
  if (bg().flags != 0) return;
  shot_fade_out(s);
  if (s->hit_landed == 0 && s->p12c == 0 && 3 < s->p128 && s->p134 == 0) {
    s->p134 = 1;
    if (Shot* n = shot_spawn_callback(shot_wave)) {
      n->pos[0] = s->pos[0] + s->vel[0] * 4;
      n->pos[1] = s->pos[1];
      n->pos[2] = s->pos[2] + s->vel[2] * 4;
      n->vel[0] = s->vel[0];
      n->vel[1] = s->vel[1];
      n->vel[2] = s->vel[2];
      n->p12c = s->p12c;
      n->owner = s->owner;
      n->hit_landed = s->hit_landed;
      bobj_share_sprite(s, n);
    }
  }
  if (s->p12c == 0) {
    if (bobj_outside_arena(s)) {
      s->p12c = 1;
      return;
    }
    s->p128 += bg().frame_ticks;
  }
  s->pos[1] = bobj_ground_height(s);
  if (s->hit_landed == 0 && shot_hit_melee(s, 0x100, 1) != 0) s->hit_landed = 1;
}

// 0x800887ac: the whirlwind: a twisting funnel of 6 x 8 textured quads (the anim 8 cel)
// along x in front of it, radius growing with the square of the distance, scaled by the
// strength +0x10c, turning 0x28 a tick (+0x12c); the last ring's quads shade to black on
// their first two vertices (colour words 0 / 1). A rushing clip (+0x128) fades along. For
// 70 ticks while the owner is still in attack 2 (state 10) the strength grows to 0x100
// and every 4 ticks it hits (power 0x40, along its facing) each standing member within
// 0x300 in front and 0xc8 of its line; then it shrinks away.
void shot_whirlwind(Shot* s) {
  BattleGlobals& g = bg();
  s->layer = 6;
  s->anim = 8;
  bobj_anim_draw(s, g.flags | 0x9800u);
  const FrameUv& t = frame_uv();
  const s32 uv[4] = {t.u0, t.v0, t.u1, t.v1};
  const u16 cel_tpage = t.tpage, cel_clut = t.clut;
  s32 m = s->aux * 0xE6;
  const s32 strength = (m < 0 ? m + 0xFF : m) >> 8;
  if (s->p128 == 0) s->p128 = static_cast<s32>(bsfx_play_at(0x2D, s));
  sound_handle_fade_out(static_cast<u32>(s->p128), 0x1E, 8);
  if (g.flags != 0) sound_handle_duck(static_cast<u32>(s->p128), 8);
  if (uv[1] != uv[3]) {
    const u32 tag = bobj_poly_tag(s, cel_tpage);
    u32 rgb[4] = {(tag & 0xFFFFFF) | 0x3E000000u, tag, tag, tag};
    psyq::SVECTOR v[4]{};  // scratchpad 0x1f8003e0: far ring, near ring, then the previous angle's two
    s32 far_x = 0x80;
    for (int i = 0; i < 6; ++i, far_x += 0x80) {
      if (i == 5) rgb[1] = rgb[0] = 0x3E000000u;
      const s32 near_x = i * 0x80;
      const s32 a = near_x / 0xE0, b = far_x / 0xE0;
      const s32 base = strength / 2;
      s32 r_near = (a * a * strength) / 0xA4 + base;
      s32 r_far = (b * b * strength) / 0xA4 + base;
      m = r_near * s->aux;
      r_near = (m < 0 ? m + 0xFF : m) >> 8;
      m = r_far * s->aux;
      r_far = (m < 0 ? m + 0xFF : m) >> 8;
      const s32 ang0 = s->p12c + i * i;
      s32 c = bcos(static_cast<u32>(ang0)), sn = bsin(static_cast<u32>(ang0));
      v[1].vx = v[3].vx = static_cast<s16>(bobj_front_x(s, near_x) >> 12);
      v[1].vy = static_cast<s16>((s->pos[1] + c * r_near) >> 12);
      v[1].vz = static_cast<s16>((s->pos[2] + sn * r_near) >> 12);
      v[0].vx = v[2].vx = static_cast<s16>(bobj_front_x(s, far_x) >> 12);
      v[0].vy = static_cast<s16>((s->pos[1] + c * r_far) >> 12);
      v[0].vz = static_cast<s16>((s->pos[2] + sn * r_far) >> 12);
      for (int j = 1; j <= 8; ++j) {
        const s32 ang = j * 0x200 + s->p12c + i * i;
        c = bcos(static_cast<u32>(ang));
        sn = bsin(static_cast<u32>(ang));
        v[3].vy = v[1].vy;
        v[2].vy = v[0].vy;
        v[2].vz = v[0].vz;
        v[3].vz = v[1].vz;
        v[1].vy = static_cast<s16>((s->pos[1] + c * r_near) >> 12);
        v[1].vz = static_cast<s16>((s->pos[2] + sn * r_near) >> 12);
        v[0].vy = static_cast<s16>((s->pos[1] + c * r_far) >> 12);
        v[0].vz = static_cast<s16>((s->pos[2] + sn * r_far) >> 12);
        draw_quad_gt4(uv, v, cel_clut, cel_tpage, s->layer, rgb);
      }
    }
  }
  if (g.flags != 0) return;
  s->p12c = (s->p12c + g.frame_ticks * 0x28) & 0xFFF;
  s->p130 += g.frame_ticks;
  if (s->p130 < 0x47 && g.state == kStateFighting && s->owner->action == 10) {
    s->aux = static_cast<s16>(s->aux + g.frame_ticks * 0x10);
    if (0x100 < s->aux) s->aux = 0x100;
    Unit* units[8] = {};
    Vec4 points[8];
    s32 x0, x1;
    if (s->facing[0] < 0) {
      x1 = s->pos[0];
      x0 = x1 - 0x300000;
    } else {
      x0 = s->pos[0];
      x1 = x0 + 0x300000;
    }
    int n = 0;
    s->p134 -= g.frame_ticks;
    if (s->p134 >= 0) return;
    s->p134 += 4;
    for (PartyUnit* u : g.party_units) {
      if (unit_is_down(u)) continue;
      if (u->pos[0] < x0 || x1 <= u->pos[0]) continue;
      const s32 on_line[3] = {s->pos[0], u->pos[1], u->pos[2]};
      if (0xC7FFF < vec_distance_fx(on_line, s->pos)) continue;
      units[n] = u;
      points[n] = centre_of(u);
      ++n;
    }
    unit_attack_targets(s->owner, n, units, points, 0x40, s->facing[0] < 0 ? 0x800 : 0, 0);
  } else {
    s->aux = static_cast<s16>(s->aux - g.frame_ticks * 0x10);
    if (s->aux < 0) shot_kill(s);
  }
}

}  // namespace

std::unique_ptr<EnemyUnit> enemy_mod_ruprecht_create() { return std::make_unique<EnemyRuprecht>(); }

}  // namespace ss::game::battle
