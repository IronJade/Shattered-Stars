// Shigeo's enemy-code module: block fb0dbca9b4b2 (4532 bytes, slot A linked at 0x80088110),
// vtable 0x8008919c, monster type 0xc6 (Shigeo), encounter 533 (0x2f4-byte objects: a second
// BObj for its shadow cel). A floating boss: a textured shadow under it, a trail while it
// moves, a charged piercing energy ball, a grab that holds a member up for a point-blank
// ball, and a rain of rocks on random members.

#include <cstring>
#include <unordered_map>

#include "game/battle/bsfx.h"
#include "game/battle/combat.h"
#include "game/battle/enemy_classes.h"
#include "game/battle/enemy_modules.h"
#include "game/battle/enemy_support.h"
#include "game/battle/killer.h"
#include "game/battle/render.h"
#include "game/battle/shot.h"
#include "game/battle/sprite.h"
#include "game/boot/rng.h"

namespace ss::game::battle {
namespace {

u32 rng15() { return static_cast<u32>(rng_next() & 0x7FFF); }  // rng_next15 0x800103b4

// The target's centre (its body's mid height).
Vec4 centre_of(const Unit* u) { return {u->pos[0], u->pos[1] + u->size[1] * -0x2000, u->pos[2], 0}; }

// The descriptor's +0x72 (the high half of the element word): nonzero makes the projectile
// pierce.
void set_hi16(s32& w, u16 v) { w = static_cast<s32>((static_cast<u32>(w) & 0xFFFFu) | static_cast<u32>(v) << 16); }

void shot_rock(Shot* s);

struct EnemyShigeo : EnemyUnit {
  BObj shadow_;  // +0x26c: draws nothing itself; its anim 0x12 cel textures the shadow

  // 0x80088110: enemy_create, the module's vtable, the shadow BObj (bobj_create) drawing
  // into pool A.
  EnemyShigeo() { shadow_.pool = bg().prim_pool_a; }

  // 0x8008815c [22]: enemy_animate with its own shadow: instead of unit_draw_shadow a 3x3
  // textured patch (the anim 0x12 cel) on the ground, sized from the sprite's extents less
  // the hover height, scaled by +0x14e / +0x152.
  int animate(u32 anim_flags) override {
    BattleGlobals& g = bg();
    const s32 height = unit_ground_follow(this);
    const s32 y = pos[1];
    pos[1] += bsin(static_cast<u32>((hover_phase & 0x1F) << 7)) * hover_amp;
    if (g.flags == 0) hover_phase = static_cast<s16>(hover_phase + g.frame_ticks);
    unit_draw_model(this);
    pos[1] = y;
    const int f = static_cast<s16>(bobj_anim_draw(this, anim_flags | (g.flags & 0xFE)));
    if (f != -1) draw_shadow(height);
    return f;
  }
  void draw_shadow(s32 height) {
    bobj_share_sprite(this, &shadow_);
    shadow_.anim = 0x12;
    bobj_anim_draw(&shadow_, bg().flags | 0x9800u);
    const FrameUv& t = frame_uv();
    const s32 uv[4] = {t.u0, t.v0, t.u1, t.v1};
    const u16 cel_tpage = t.tpage, cel_clut = t.clut;
    s32 a = ext_x1 < 0 ? -ext_x1 : ext_x1;
    const s32 b = ext_x0 < 0 ? -ext_x0 : ext_x0;
    if (a < b) a = b;
    s32 r = a * 4 + 0x20 - height;
    if (r < 4) r = 4;
    s32 m = r * shadow_scale;
    r = (m < 0 ? m + 0xFF : m) >> 8;
    m = r * shadow_scale2;
    r = ((m < 0 ? m + 0xFF : m) >> 8) << 12;
    const s32 ground = bobj_ground_height(this);
    const s32 corners[4][4] = {{pos[0] - r, ground, pos[2] - r, 0},
                               {pos[0] + r, ground, pos[2] - r, 0},
                               {pos[0] - r, ground, pos[2] + r, 0},
                               {pos[0] + r, ground, pos[2] + r, 0}};
    draw_textured_patch(uv, corners, cel_clut, cel_tpage, 5, bobj_poly_tag(this, cel_tpage), 3);
  }

  // 0x800883f0 [23]: enemy_state_approach plus a fading trail sprite (anim 0x14) each
  // unpaused frame: behind it when it faces sideways on screen, else under it in front
  // (layer 2, facing up the screen) or behind (layer -6).
  void state_approach() override {
    EnemyUnit::state_approach();
    if (bg().flags != 0) return;
    const s32 fy = view_fy < 0 ? -view_fy : view_fy;
    const s32 fx = view_fx < 0 ? -view_fx : view_fx;
    u32 dir = 0;
    if (!(fy < fx)) dir = view_fy < 0 ? 1 : 2;
    Sprite* sp = unit_play_anim(this, 0x14);
    if (sp == nullptr) return;
    if (dir == 1) {
      sprite_place_near(sp, this, 0, 0x90, 0);
      sp->layer = 2;
    } else if (dir == 0) {
      sprite_place_near(sp, this, -0x60, 0x90, 0);
    } else {
      sprite_place_near(sp, this, 0, 0x90, 0);
      sp->layer = -6;
    }
    sprite_set_fade(sp, 0x80, -8, 0x10);
  }

  // 0x80088520 [26]: from 400 away (always: the chance 0x291c0 is above 0x7fff) the rocks
  // (attack 3) start at once where it stands; nearer the grab (2); else the default.
  void choose_target() override {
    if (enemy_try_approach(this, 400, 0x291C0, 3, 0x40) != 0) {
      counters[0] = 0;
      counters[1] = 0;
      action_kind = 3;
      begin_action();
      battle_message_show(0x271E);
    } else if (enemy_try_approach(this, 0xFA, 0x4000, 2, 0x40) == 0) {
      EnemyUnit::choose_target();
    }
  }

  // 0x800885cc [27]: the charge (anim 4: a humming clip in +0x17c fading out, 35 ticks of
  // sparks drawn in above its hand), then the shot (anim 5): a big piercing ball (anim 7,
  // trail 0x90a0ff, speed 0x30, life 0x30, box 0x48). The switch to anim 5 animates again
  // in the same frame.
  void attack1() override {
    BattleGlobals& g = bg();
    loop_limit = 1;
    for (;;) {
      anim = static_cast<u16>(counters[0] + 4);
      if (static_cast<s16>(animate(static_cast<u32>(attack_recovery))) != -1) break;
      if (counters[0] != 0) {
        action_kind = 0;
        begin_action();
        return;
      }
      counters[0] = 1;
      bsfx_play_at(0x34, this);
      bsfx_play_at(0x3C, this);
      ShotDesc* d = shot_desc_init(7);
      d->trail_rgb = 0x90A0FF;
      d->speed = 0x30;
      d->life = 0x30;
      set_hi16(d->element, 1);
      d->flags = 0x100006;
      d->target = target;
      const s32 from[3] = {bobj_front_x(this, 0x88), pos[1] - 0x8C000, pos[2]};
      if (Shot* s = shot_spawn_projectile(this, from, d, this)) {
        s->size[0] = 0x48;
        s->size[1] = 0x48;
        s->size[2] = 0x48;
      }
    }
    if (g.flags == 0) {
      if (counters[1] == 0) counters[2] = static_cast<s32>(bsfx_play_at(0x46, this));
      counters[1] += g.frame_ticks;
      if (counters[1] < 0x23) {
        if (Sprite* sp = unit_play_anim(this, 7)) {
          const s32 hand[3] = {bobj_front_x(this, 0x48), pos[1] - 0x1A4000, pos[2]};
          const Vec4 r = vec_random_around(hand, 0x140);
          sprite_set_pos_v(sp, r.data());
          const Vec4 v = vec_velocity_toward(sp->pos, hand, 8);
          std::memcpy(sp->vel, &v, sizeof(sp->vel));
          sprite_set_fade(sp, 0x80, -4, 8);
        }
      }
    }
    if (counters[1] != 0) {
      sound_handle_fade_out(static_cast<u32>(counters[2]), 0x1E, 10);
      if (g.flags != 0) sound_handle_duck(static_cast<u32>(counters[2]), 8);
    }
  }

  // 0x80088848 [28]: the grab. Step 0 (anim 8): glides 4 a tick toward the target (up while
  // below its head) for up to 24 ticks; its attack box catching a member starts step 1
  // (anim 9): the target is held 0x78 in front, raised, frozen (+0x154 = 12) for 30 ticks,
  // then let go and shot point-blank with a piercing ball (anim 0xb, speed 0x20, life 0x10,
  // box 0x24). The target going down ends it.
  void attack2() override {
    BattleGlobals& g = bg();
    loop_limit = 0;
    anim = static_cast<u16>(counters[0] + 8);
    const bool ended = static_cast<s16>(animate(static_cast<u32>(attack_recovery))) == -1 || unit_is_down(target);
    if (!ended) {
      vel[2] = 0;
      if (g.flags != 0) return;
      if (counters[0] == 0) {
        const s32 head = target != nullptr ? target->pos[1] + target->size[1] * -0x4000 : 0;
        vel[1] = head < pos[1] ? -0x4000 : 0;
        vel[0] = (target != nullptr && target->pos[0] <= pos[0]) ? -0x4000 : 0x4000;
        if (unit_try_move_scaled(this, vel, 7) == nullptr) bobj_move(this, vel);
        Unit* units[8] = {};
        Vec4 points[8];
        if (collider_attack_overlaps(this, units, points, 1) != 0) {
          counters[0] = 1;
          bobj_anim_reset(this);
          counters[1] = 0;
          counters[2] = 0;
          return;
        }
        counters[1] += g.frame_ticks;
        if (counters[1] < 0x19) return;
      } else if (counters[0] == 1) {
        if (target != nullptr) {
          target->pos[0] = bobj_front_x(this, 0x78);
          target->pos[1] = pos[1] + target->size[1] * 0x4000 - 0x1A4000;
          target->pos[2] = pos[2];
          target->airborne = 2;
          target->jumping = 0;
          target->super_armour = 0xC;
        }
        counters[1] += g.frame_ticks;
        if (counters[1] < 0x1F) return;
        bsfx_play_at(0x34, this);
        bsfx_play_at(0x3C, this);
        if (target != nullptr) target->super_armour = 0;
        ShotDesc* d = shot_desc_init(0xB);
        d->trail_rgb = 0x90A0FF;
        d->speed = 0x20;
        d->life = 0x10;
        set_hi16(d->element, 1);
        d->flags = 0x100006;
        d->target = target;
        const s32 from[3] = {bobj_front_x(this, 0x60), pos[1] - 0xD8000, pos[2]};
        if (Shot* s = shot_spawn_projectile(this, from, d, this)) {
          s->size[0] = 0x24;
          s->size[1] = 0x24;
          s->size[2] = 0x24;
        }
      } else {
        return;
      }
    }
    action_kind = 0;
    begin_action();
  }

  // 0x80088bb0 [29]: the rock rain (anim 0xc): from frame 2 each unpaused frame a rock falls
  // from 0x320 above a random standing member (up to 100 draws to find one), +-0x20 off.
  void attack3() override {
    anim = 0xC;
    loop_limit = 1;
    if (static_cast<s16>(animate(static_cast<u32>(attack_recovery))) == -1 || unit_is_down(target)) {
      action_kind = 0;
      begin_action();
      return;
    }
    if (bg().flags != 0 || frame < 2) return;
    PartyUnit* m = nullptr;
    bool found = false;
    for (int i = 0; i < 100; ++i) {
      m = bg().party_units[rng_range(4)];
      if (!unit_is_down(m)) {
        found = true;
        break;
      }
    }
    if (!found) return;
    Shot* s = shot_spawn_callback(shot_rock);
    if (s == nullptr) return;
    if (rng15() < 0x4000) bsfx_play_at(0x34, s);
    s->pos[0] = m->pos[0] + rng_spread_fixed(0x20);
    s->pos[1] = m->pos[1] - 0x320000;
    s->pos[2] = m->pos[2] + rng_spread_fixed(0x20);
    s->vel[1] = 0x18000;
    s->owner = this;
    bobj_share_sprite(this, s);
  }

  // 0x80089074 [25]: once: 48 beams out of its centre; then the usual fade.
  void state_dying() override {
    if (counters[0] == 0) {
      for (int i = 0; i < 0x30; ++i) {
        const Vec4 c = centre_of(this);
        const Vec4 d = vec_random_around(c.data(), 0x500);
        const Vec4 c2 = centre_of(this);
        effect_spawn_beam_at(c2.data(), d.data(), 4, 0x140, 0x18, 0xA000, 0x400, 0);
      }
      bsfx_play_at(0x21, this);
      bsfx_play_at(0x22, this);
      counters[0] = 1;
    }
    EnemyUnit::state_dying();
  }
};

// The rock's afterimage lives in the Shot's +0x130; the port keeps p130 as the "bound"
// flag (shot_reset clears it) and the pointer here.
std::unordered_map<const Shot*, Afterimage*>& rock_trails() {
  static std::unordered_map<const Shot*, Afterimage*> trails;
  return trails;
}
void rock_trail_end(const Shot* s) {
  if (s->p130 == 0) return;
  if (Afterimage* a = rock_trails()[s]) a->ending = 1;
}

// 0x80088d6c: a falling rock (anim 0xe at 1.5x, afterimage of 3, 0x102080): falls 0x18 a
// tick until the ground (it fades after 24 ticks if the battle ends); its first contact
// with the party hits for the owner at power 0x80 (debris, a ring at the victim's feet) and
// ends it.
void shot_rock(Shot* s) {
  BattleGlobals& g = bg();
  s->anim = 0xE;
  s->layer = 2;
  s->scale_y = 0x1800;
  s->scale_x = 0x1800;
  s->size[2] = s->owner->size[2];
  shot_draw_and_box(s, 4, 0xFFFFFFFF);
  if (g.flags != 0) return;
  if (s->p130 == 0) {
    rock_trails()[s] = afterimage_create(s, 3, 0x102080, 0);
    s->p130 = 1;
  }
  if (shot_fade_out(s) != 0) {
    rock_trail_end(s);
    return;
  }
  if (s->pos[1] < bobj_ground_height(s)) {
    if (s->p12c == 0 && 0x18 < s->p134) s->p12c = 1;
    bobj_move(s, s->vel);
    s->p134 += g.frame_ticks;
    if (s->hit_landed != 0) return;
    Unit* units[8] = {};
    Vec4 points[8];
    const int n = collider_attack_overlaps(s, units, points, 1);
    if (n == 0) return;
    s32 vx = s->vel[0];
    s->hit_landed = 1;
    if (vx == 0) vx = s->owner->facing[0];
    if (unit_attack_targets(s->owner, n, units, points, 0x80, vx < 0 ? 0x800 : 0, 0) != 0) {
      bsfx_play_at_pos(0x50, s->pos);
      for (int i = 0; i < 8; ++i) {
        Sprite* sp = shot_spawn_sprite_at(s, static_cast<int>(rng_range(3)) + 0x10, 1);
        if (sp == nullptr) continue;
        const Vec4 r = vec_random_around(points[0].data(), static_cast<s32>(rng_range(200)));
        sprite_set_pos_v(sp, r.data());
        const s32 sc = rng_between(0x1800, 12000);
        sp->scale_y = sc;
        sp->scale_x = sc;
        sp->layer = 3;
      }
      const s32 feet[3] = {units[0]->pos[0], bobj_ground_height(units[0]), units[0]->pos[2]};
      effect_spawn_sparks_at(feet, 0x10, 0xC0, 0x20, 0x1E, 0x3C, 0xE, 1, 0x30, 1);
    }
    s->owner->attack_recovery = 0;
  }
  rock_trail_end(s);
  shot_kill(s);
}

}  // namespace

std::unique_ptr<EnemyUnit> enemy_mod_shigeo_create() { return std::make_unique<EnemyShigeo>(); }

}  // namespace ss::game::battle
