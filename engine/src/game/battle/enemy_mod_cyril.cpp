// Cyril's enemy-code module: block 85cba1112ca5 (5348 bytes, slot A linked at 0x80088110),
// vtable 0x800894cc, monster type 0xcd Cyril (0x2f8-byte objects), encounters 0 (the test
// fight) and 538. A caster who never walks: every approach is a warp (he shrinks into a
// ring of light, reappears by his target and grows back); attack 1 strikes the target
// where it stands, attack 2 is spell 0x45, after which 20 target choices go by before he
// tries it again. A second sprite (anim 7, his aura) is drawn over him every frame.

#include "game/battle/bsfx.h"
#include "game/battle/camera.h"
#include "game/battle/combat.h"
#include "game/battle/enemy_classes.h"
#include "game/battle/enemy_modules.h"
#include "game/battle/enemy_support.h"
#include "game/battle/render.h"
#include "game/battle/shot.h"
#include "game/battle/sprite.h"
#include "game/boot/rng.h"

namespace ss::game::battle {
namespace {

// The target's / unit's centre (its body's mid height).
Vec4 centre_of(const Unit* u) { return {u->pos[0], u->pos[1] + u->size[1] * -0x2000, u->pos[2], 0}; }

void shot_strike(Shot* s);
void shot_ring(Shot* s);

struct EnemyCyril : EnemyUnit {
  s32 spell_cooldown = 0;  // +0x26c: target choices left before attack 2 may come again
  BObj aura;               // +0x270: the overlay sprite (anim 7 of his set), 0x88 bytes

  // 0x80088110: enemy_create, the module's vtable, the aura's bobj_create, the intro voice.
  EnemyCyril() {
    intro_voice = 0x4D5;
    voice_variants = 2;
  }

  // 0x80088164 [22]: the usual animation, then the aura over the body at its hover height
  // (and shake), facing and scale.
  int animate(u32 anim_flags) override {
    BattleGlobals& g = bg();
    const int f = EnemyUnit::animate(anim_flags);
    aura.pool = g.prim_pool_a;
    bobj_share_sprite(this, &aura);
    aura.anim = 7;
    aura.pos[0] = pos[0];
    const s32 bob = bsin(static_cast<u32>((hover_phase & 0x1F) << 7));
    aura.pos[2] = pos[2];
    aura.facing[1] = facing[1];
    aura.facing[2] = facing[2];
    aura.scale_x = scale_x;
    aura.scale_y = scale_y;
    aura.facing[0] = facing[0];
    aura.pos[1] = pos[1] + bob * hover_amp;
    if (shake_frames != 0) aura.pos[0] += shake_amp * 0x1000;
    bobj_project(&aura);
    bobj_anim_draw(&aura, anim_flags | (g.flags & 0xFE));
    return f;
  }

  // 0x80088270 [4]: interrupted while chanting (state 10, not held) the spell data goes;
  // a warp's squash is undone.
  void interrupt() override {
    if (action == 10 && special_timer == 0) spell_data_cancel_owner(this);
    if (scale_x < 0x1000) {
      scale_x = 0x1000;
      scale_y = 0x1000;
    }
  }

  // 0x800882d8 [34]: a voice clip from the pack's extra data: hit (0) the second, attack 1
  // the first.
  void on_attack_start(int attack) override {
    const void* clip;
    if (attack == 0) {
      clip = enemy_extra_data(1);
    } else if (attack == 1) {
      clip = enemy_extra_data(0);
    } else {
      return;
    }
    // (The console plays unconditionally; the port has no clip until the table is exposed.)
    if (clip != nullptr) bsfx_play(0, clip, this, 1);
  }

  // The warp's ring of light (state 0 shrinking from 0x140, state 3 growing from 0x40).
  void spawn_ring(s32 radius, s32 growth) {
    Shot* s = shot_spawn_callback(shot_ring);
    if (s == nullptr) return;
    s->pos[0] = pos[0];
    s->pos[1] = pos[1];
    s->p130 = radius;
    s->p134 = growth;
    s->pos[2] = pos[2];
    bobj_share_sprite(this, s);
  }
  // 8 sparks (anim 0xf) orbiting the body at a random height (radius 'r', growing 'growth').
  void orbit_sparks(s32 r, s32 growth) {
    for (int i = 0; i < 8; ++i) {
      Sprite* sp = unit_play_anim(this, 0xF);
      if (sp == nullptr) continue;
      const s32 h = static_cast<s32>(rng_range(0x40000));
      sprite_set_pos(sp, pos[0], pos[1] - h, pos[2]);
      sprite_set_orbit(sp, static_cast<s32>(rng_range(0x1000)), 0, r, 0, r, 0x5B, 0, growth);
      sprite_set_fade(sp, 0x80, -4, 4);
    }
  }
  // 8 flecks (anim 'anim') on the ground disc of a random radius below 'r'.
  void ground_flecks(int anim_no, u32 r, s16 hold) {
    for (int i = 0; i < 8; ++i) {
      Sprite* sp = unit_play_anim(this, anim_no);
      if (sp == nullptr) continue;
      const Vec4 p = vec_random_in_disc_xz(pos, static_cast<s32>(rng_range(r)));
      sprite_set_pos_v(sp, p.data());
      sprite_set_fade(sp, 0x80, -4, hold);
    }
  }

  // 0x8008832c [23]: the warp (+0x13e step, +0x18c ticks): 0 burst of sparks out and the
  // ring; 1 squashes for 30 ticks, then jumps to the approach point beside the target (no
  // point: the attack is dropped); 2 stretches back for 30 ticks; 3 sparks fly in, the ring
  // grows, full size, and the attack starts.
  void state_approach() override {
    BattleGlobals& g = bg();
    if (g.flags == 0) {
      const s16 step = detour;
      if (step == 1) {
        if (0x1E < detour_timer) {
          ground_flecks(0x14, 0x280, 2);
          detour = 2;
          bobj_anim_reset(this);
          Vec4 ap;
          if (unit_find_approach_point(this, &ap)) {
            pos[0] = ap.x;
            pos[2] = ap.z;
            pos[1] = bobj_ground_height(this);
          } else {
            action_kind = 0;
          }
          detour_timer = 0;
          ground_flecks(0x15, 0x140, 4);
        }
        scale_x -= g.frame_ticks * 0x40;
        scale_y += g.frame_ticks * 0x80;
      } else if (step == 0) {
        for (int i = 0; i < 0x10; ++i) {
          Sprite* sp = unit_play_anim(this, static_cast<int>(rng_range(4)) + 0x10);
          if (sp == nullptr) continue;
          const Vec4 c = centre_of(this);
          sprite_set_pos_v(sp, c.data());
          const Vec4 v = vec_random_around(kZeroVector, 0x18);
          sp->vel[0] = v.x;
          sp->vel[1] = v.y;
          sp->vel[2] = v.z;
          sprite_set_fade(sp, 0x80, -4, 4);
        }
        orbit_sparks(0x140, -4);
        spawn_ring(0x140, -0xC);
        bsfx_play_at(0x26, this);
        detour = 1;
      } else if (step == 2) {
        if (0x1E < detour_timer) detour = 3;
        scale_x += g.frame_ticks * 0x40;
        scale_y -= g.frame_ticks * 0x80;
      } else if (step == 3) {
        for (int i = 0; i < 0x10; ++i) {
          Sprite* sp = unit_play_anim(this, static_cast<int>(rng_range(4)) + 0x10);
          if (sp == nullptr) continue;
          const Vec4 c = centre_of(this);
          const Vec4 p = vec_random_around(c.data(), 0x300);
          sprite_set_pos_v(sp, p.data());
          const Vec4 c2 = centre_of(this);
          const Vec4 v = vec_velocity_toward(sp->pos, c2.data(), 0x10);
          sp->vel[0] = v.x;
          sp->vel[1] = v.y;
          sp->vel[2] = v.z;
          sprite_set_fade(sp, 0x80, -4, 4);
        }
        orbit_sparks(0x40, 8);
        spawn_ring(0x40, 0xC);
        bsfx_play_at(0x28, this);
        bsfx_play_at(0x21, this);
        scale_y = 0x1000;
        scale_x = 0x1000;
        begin_action();
        return;
      }
      if (g.flags == 0) detour_timer += g.frame_ticks;
    }
    anim = 0;
    loop_limit = 0;
    animate(0);
  }

  // 0x80088940 [26]: once the cooldown has run out, the spell (attack 2) when the approach
  // roll succeeds (message 0x42c and a voice); else the usual choice.
  void choose_target() override {
    const s32 left = spell_cooldown;
    bool ready = left == 0;
    if (!ready) {
      spell_cooldown = left - 1;
      ready = spell_cooldown == 0;
    }
    if (ready && enemy_try_approach(this, 500, 0x2000, 2, 0x280) != 0) {
      counters[2] = 0;
      counters[3] = 0;
      battle_message_show(0x42C);
      voice_play_entry(static_cast<int>(rng_range(2)) + 0x4E8, this);
      action_kind = 2;
      begin_action();
      return;
    }
    EnemyUnit::choose_target();
  }

  // 0x800889f4 [27]: past frame 3 a strike appears on the target (a Shot at its feet) with
  // a flash 0x300 above it.
  void attack1() override {
    anim = 4;
    loop_limit = 1;
    if (static_cast<s16>(animate(static_cast<u32>(attack_recovery))) == -1) {
      action_kind = 0;
      begin_action();
      return;
    }
    if (bg().flags != 0 || frame < 4 || counters[1] != 0) return;
    counters[1] = 1;
    bsfx_play_at(0x52, this);
    if (Shot* s = shot_spawn_callback(shot_strike)) {
      shot_place_at_unit(s, target, 0, 0, 0);
      s->owner = this;
      bobj_share_sprite(this, s);
    }
    if (Sprite* sp = unit_play_anim(this, 0x1C)) {
      sprite_place_near(sp, target, 0, 0x300, 0);
      sprite_set_fade(sp, 0x80, -4, 4);
      sp->scale_y = 0x1800;
      sp->scale_x = 0x1800;
      sp->layer = 4;
    }
  }

  // 0x80088b54 [28]: spell 0x45 (+0x178 data requested, +0x17c step: 0 loading, 1 cast
  // at the next loop of anim 9, 2 cast, 3 waiting for its effect, +0x180 ticks). The data
  // is requested at once and the cast comes after 40 ticks once it has loaded. While a
  // kind-0x39 SpellCast runs, the special-move pause before its 6th tick sets step 3, and
  // step 3 ends the attack once that cast is past tick 5. Late in the end countdown the
  // attack is interrupted.
  void attack2() override {
    BattleGlobals& g = bg();
    if (!(g.state < 3 || 0x13 < g.end_countdown)) {
      interrupt();
      end_attack();
      return;
    }
    special_timer = 8;
    if (const SpellCast* sc = spellcast_slot0(); sc != nullptr && sc->kind == 0x39) {
      if (counters[2] == 3 && 5 < sc->step) {
        end_attack();
        return;
      }
      if ((g.flags & 0x40) != 0 && static_cast<u32>(sc->step) < 6) {
        counters[2] = 3;
        return;
      }
    }
    anim = 9;
    loop_limit = 0;
    animate(0);
    if (g.flags != 0) return;
    if (counters[2] == 1 && loops != 0) {
      spell_effect_started_flag() = 0;
      spell_cast(0x45, this, target, 0);
      const bool started = spell_effect_started_flag() != 0;
      spell_cooldown = 0x14;
      if (started) counters[2] = 2;
      return;
    }
    if (counters[1] == 0) {
      counters[1] = 1;
      spell_data_request(0x45, 0x3C, this);
      if (counters[1] == 0) return;
    }
    counters[3] += g.frame_ticks;
    if (0x28 < counters[3] && counters[2] == 0 && spell_data_status(0x45, this) != 1) {
      counters[2] = 1;
      bobj_anim_reset(this);
    }
  }
  void end_attack() {
    action_kind = 0;
    begin_action();
  }

  // 0x80089204 [25]: once: 96 beams out of the body, two spark rings at its centre (the
  // second turning), one on the ground, a voice and three sounds; then the usual fade.
  void state_dying() override {
    if (counters[0] == 0) {
      counters[0] = 1;
      for (int i = 0; i < 0x60; ++i) {
        const Vec4 c = centre_of(this);
        const Vec4 d = vec_random_around(c.data(), 0x3C0);
        const Vec4 from = centre_of(this);
        const s32 len = rng_between(0x140, 0x280);
        const s32 speed = rng_between_fixed(1, 4);
        const s32 width = rng_between(0x400, 0x1000);
        effect_spawn_beam_at(from.data(), d.data(), 5, len, 0x20, speed, width, 0);
      }
      const Vec4 c1 = centre_of(this);
      effect_spawn_sparks_at(c1.data(), 0x14, 0x100, 0, 0x60, 0x3C, 5, 3, 0x40, 1);
      const Vec4 c2 = centre_of(this);
      if (Shot* ring = effect_spawn_sparks_at(c2.data(), 0x14, 0x100, 0, 0x60, 0x3C, 5, 3, 0x40, 1))
        ring->vel[2] = 0x400;
      const s32 ground[3] = {pos[0], bobj_ground_height(this), pos[2]};
      effect_spawn_sparks_at(ground, 0x14, 0x200, 0x140, 0x10, 0x5A, 5, 0, 0x40, 1);
      voice_play_entry(static_cast<int>(rng_range(3)) + 0x4D7, this);
      bsfx_play_at(0x50, this);
      bsfx_play_at(0x21, this);
      bsfx_play_at(0x22, this);
    }
    EnemyUnit::state_dying();
  }

  // 0x80089490 [9]: chants spell 0x27 at level 15.
  int skill_level(int skill) override { return skill == 0x27 ? 0xF : 0; }

  // 0x800894a4 [36]: the dtor (EnemyUnit vtable, unit_destroy).
};

// 0x80088d8c: attack 1's strike (anim 0x1a, one loop, the owner's depth): on its first
// contact the owner hits the first unit met (power 0x100, knocked along its flight,
// element bit 4), with a flash on it; dies at the end of the animation.
void shot_strike(Shot* s) {
  s->anim = 0x1A;
  s->loop_limit = 1;
  s->size[2] = s->owner->size[2];
  if (static_cast<s16>(shot_draw_and_box(s, 4, 0xFFFFFFFF)) == -1) {
    shot_kill(s);
    return;
  }
  Unit* units[8] = {};
  Vec4 points[8];
  int hit = 0;
  if (collider_attack_overlaps(s, units, points, s->owner->is_enemy) != 0) hit = s->hit_landed == 0 ? 1 : 0;
  if (hit == 0) return;
  s->hit_landed = 1;
  // (The count passed is the landed flag, 1, not the number of units met.)
  unit_attack_targets(s->owner, hit, units, points, 0x100, s->vel[0] < 0 ? 0x800 : 0, 0x10);
  for (int i = 0; i < hit; ++i) {
    Sprite* sp = shot_spawn_sprite(s, 0x14);
    if (sp == nullptr) continue;
    sprite_set_pos_v(sp, units[i]->pos);
    sprite_set_fade(sp, 0x80, -4, 8);
    sp->scale_y = 0x1800;
    sp->scale_x = 0x1800;
  }
}

// 0x80088f28: the warp's ring of light: a 6-band x 8-segment cylinder of textured quads
// (anim 0xd's cel, bands 0x80 high) of radius +0x130 turning by +0x128, growing by +0x134 a
// tick; it greys out (fade +0x12c) over 32 ticks and dies.
void shot_ring(Shot* s) {
  BattleGlobals& g = bg();
  s->layer = 3;
  s->anim = 0xD;
  bobj_anim_draw(s, g.flags | 0x9800u);
  const FrameUv& f = frame_uv();
  const s32 uv[4] = {f.u0, f.v0, f.u1, f.v1};
  const u16 tpage = f.tpage, clut = f.clut;
  if (uv[1] != uv[3]) {
    const u32 code = bobj_poly_tag(s, tpage);
    const s32 r = s->p130;
    s32 bottom = 0x80000;
    for (int band = 0; band < 6; ++band, bottom += 0x80000) {
      s32 c[4][4] = {};
      c[0][1] = c[1][1] = s->pos[1] - band * 0x80000;
      c[2][1] = c[3][1] = s->pos[1] - bottom;
      for (int seg = 0; seg < 8;) {
        const u32 a0 = static_cast<u32>(seg * 0x200 + s->p128);
        ++seg;
        const u32 a1 = static_cast<u32>(seg * 0x200 + s->p128);
        const s32 cos0 = bcos(a0), sin0 = bsin(a0), cos1 = bcos(a1), sin1 = bsin(a1);
        c[0][0] = c[2][0] = s->pos[0] + cos0 * r;
        c[0][2] = c[2][2] = s->pos[2] + sin0 * r;
        c[1][0] = c[3][0] = s->pos[0] + cos1 * r;
        c[1][2] = c[3][2] = s->pos[2] + sin1 * r;
        draw_textured_patch(uv, c, clut, tpage, s->layer, code, 1);
      }
    }
  }
  if (g.flags != 0) return;
  s->colour_mode = 2;
  s->rgb[0] = static_cast<u8>(0x80 - s->p12c);
  s->p12c += g.frame_ticks * 4;
  if (0x80 < s->p12c) {
    shot_kill(s);
    return;
  }
  s->p128 += g.frame_ticks * 0x14;
  s->p130 += s->p134 * g.frame_ticks;
}

}  // namespace

std::unique_ptr<EnemyUnit> enemy_mod_cyril_create() { return std::make_unique<EnemyCyril>(); }

}  // namespace ss::game::battle
