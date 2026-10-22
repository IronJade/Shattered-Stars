// Vesper's enemy-code module: block a76157ce87aa (4812 bytes, slot A linked at 0x80088110),
// vtable 0x800892b4, monster type 0xcb (Vesper), encounter 537 (with Decus; 0x270-byte
// objects). A sorcerer: a charged beam (a textured cylinder 0x3c0 long in front of it that
// hits whoever stands in it every 4 ticks), an energy bolt, and spell 0x43 cast after a
// chant (every 10 thinks at most); voices from the pack (intro 0x4b1, two variants).
// (The battle program's stale rows enemy_vesper_*_stale 0x8007b810.. are an old copy.)

#include <cstring>

#include "game/battle/bsfx.h"
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

// The target's centre (its body's mid height).
Vec4 centre_of(const Unit* u) { return {u->pos[0], u->pos[1] + u->size[1] * -0x2000, u->pos[2], 0}; }

void shot_beam(Shot* s);

struct EnemyVesper : EnemyUnit {
  s32 spell_cooldown = 0;  // +0x26c: thinks before spell 0x43 may be chosen again

  // 0x80088110: enemy_create, the module's vtable, intro voice 0x4b1 with 2 variants.
  EnemyVesper() {
    intro_voice = 0x4B1;
    voice_variants = 2;
  }

  // 0x8008815c [4]: interrupted while chanting (state 11, attack 3), its spell data request
  // is dropped.
  void interrupt() override {
    if (action == 0xB) spell_data_cancel_owner(this);
  }

  // 0x80089278 [9]: chants spell skill 0x27 at level 15.
  int skill_level(int skill) override { return skill == 0x27 ? 0xF : 0; }

  // 0x8008818c [23]: enemy_state_approach plus a fading sprite (anim 0x16) at its feet each
  // unpaused frame.
  void state_approach() override {
    EnemyUnit::state_approach();
    if (bg().flags != 0) return;
    Sprite* sp = unit_play_anim(this, 0x16);
    if (sp == nullptr) return;
    const s32 feet[3] = {pos[0], bobj_ground_height(this), pos[2]};
    sprite_set_pos_v(sp, feet);
    sp->layer = 1;
    sprite_set_fade(sp, 0x80, -4, 0x10);
  }

  // 0x80088228 [32]: nothing (returns 0, as the default).
  int pre_think() override { return 0; }

  // 0x80088230 [34]: a voice clip from the pack's extra data: hit (0) the third, attack 1
  // the first, attack 2 the second.
  void on_attack_start(int attack) override {
    const void* clip;
    switch (attack) {
      case 0: clip = enemy_extra_data(2); break;
      case 1: clip = enemy_extra_data(0); break;
      case 2: clip = enemy_extra_data(1); break;
      default: return;
    }
    // (The console plays unconditionally; the port has no clip until the table is exposed.)
    if (clip != nullptr) bsfx_play(0, clip, this, 1);
  }

  // 0x800882b4 [26]: once the cooldown has run out, the spell (attack 3: message 0x42a, one
  // of voices 0x4ba-0x4bc); else the bolt (2) from 500 away (nearer a quarter of the
  // time); else the default.
  void choose_target() override {
    if ((spell_cooldown == 0 || --spell_cooldown == 0) && enemy_try_approach(this, 500, 10000, 3, 0x280) != 0) {
      counters[2] = 0;
      counters[3] = 0;
      battle_message_show(0x42A);
      voice_play_entry(static_cast<int>(rng_range(3)) + 0x4BA, this);  // (tag -1)
      return;
    }
    if (enemy_try_approach(this, 500, 0x2000, 2, 0x1E0) == 0) EnemyUnit::choose_target();
  }

  // 0x8008836c [27]: the beam. The raise (anim 4, looping) until past frame 5: a humming
  // clip (+0x17c, fading) and the beam Shot 0xe0 in front; then for 70 ticks sparks gather
  // at its tip and every 16 ticks a ring (anim 0x14) drifts off; then the release (anim 5,
  // once) to the end. (The animation's end does not return: the rest still runs.)
  void attack1() override {
    BattleGlobals& g = bg();
    anim = counters[0] == 2 ? 5 : 4;
    loop_limit = counters[0] == 2 ? 1 : 0;
    if (static_cast<s16>(animate(0)) == -1) {
      action_kind = 0;
      begin_action();
    }
    if (g.flags != 0) return;
    if (counters[0] == 0 && frame > 5) {
      const u32 h = bsfx_play_at(0x46, this);
      counters[2] = static_cast<s32>(h);
      sound_handle_fade_out(h, 0x1E, 8);
      counters[0] = 1;
      counters[3] = 0;
      if (Shot* s = shot_spawn_callback(shot_beam)) {
        shot_place_at_unit(s, this, 0xE0, 0x100, 0);
        s->owner = this;
        s->facing[0] = facing[0];
        bobj_share_sprite(this, s);
        bsfx_play_at(0x34, s);
      }
    }
    if (counters[0] != 1) return;
    const s32 tip[3] = {bobj_front_x(this, 0xE0), pos[1] - 0x100000, pos[2]};
    if (Sprite* sp = unit_play_anim(this, 0xE)) {
      const Vec4 r = vec_random_around(tip, static_cast<s32>(rng_range(0x100)));
      sprite_set_pos_v(sp, r.data());
      sprite_set_fade(sp, 0x80, -4, 8);
      sp->layer = 3;
    }
    counters[1] += g.frame_ticks;
    sound_handle_fade_out(static_cast<u32>(counters[2]), 0x1E, 8);
    if (0x46 < counters[1]) counters[0] = 2;
    if (g.state != kStateFighting) return;
    counters[3] += g.frame_ticks;
    if (counters[3] < 0x10) return;
    counters[3] -= 0x10;
    if (Shot* ring = effect_spawn_sparks_at(tip, 0xC, 0xE0, 0, 2, 0x5A, 0, 2, 0x78, 1)) {
      ring->vel[0] = 0xB;
      ring->vel[1] = bobj_facing_fx(this, 0xE);
      ring->vel[2] = 0x400;
      ring->anim = 0x14;
      bobj_share_sprite(this, ring);
    }
  }

  // 0x8008865c [28]: the bolt (anim 6, once): past frame 6 a flash (anim 0x12) at its hand
  // and a projectile (shot_desc_init(7), trail 0x804000, speed 0x20, life 0x38, hit sprite
  // 0xd, box 0x20 x 0x18) at the target. (The animation's end does not return.)
  void attack2() override {
    anim = 6;
    loop_limit = 1;
    if (static_cast<s16>(animate(0)) == -1) {
      action_kind = 0;
      begin_action();
    }
    const s32 hand[3] = {bobj_front_x(this, 0x9C), pos[1] - 0x120000, pos[2]};
    if (bg().flags != 0) return;
    if (counters[1] == 0 && frame > 6) {
      counters[1] = 1;
      if (Sprite* sp = unit_play_anim(this, 0x12)) {
        sprite_set_pos_v(sp, hand);
        sprite_set_fade(sp, 0x80, -2, 0x10);
        sp->layer = 3;
      }
    }
    if (counters[0] != 0 || frame <= 6) return;
    sound_handle_fade_out(bsfx_play_at(0x2D, this), 0x2D, 0);
    bsfx_play_at(0x34, this);
    counters[0] = 1;
    ShotDesc* d = shot_desc_init(7);
    d->trail_rgb = 0x804000;
    d->speed = 0x20;
    d->life = 0x38;
    d->hit_effect = 0xD;
    d->flags = 0x100006;
    d->target = target;
    if (Shot* s = shot_spawn_projectile(this, hand, d, this)) {
      s->size[0] = 0x20;
      s->size[1] = 0x18;
    }
  }

  // 0x80088840 [29]: spell 0x43. The chant (anim 0, endless): the spell data is requested
  // (priority 0x3c) on the first frame; after 60 ticks, once it has loaded, the animation
  // restarts (+0x17c = 1) and at its first loop the spell is cast at the target (+0x17c =
  // 2, cooldown 10). When the battle is ending (state 3+, countdown below 20) it stops
  // (slot 4, then idle). While the party's spell 0x37 runs its steps 3-4 in the special
  // pause it is held (+0x17c = 3, anim 0xe) and goes back to idle when the pause ends.
  void attack3() override {
    BattleGlobals& g = bg();
    if (kStateFighting < g.state && g.end_countdown < 0x14) {
      interrupt();
    } else if (counters[2] != 3 || (g.flags & 0x40) != 0) {
      const SpellCast* sc = spellcast_slot0();
      if (sc != nullptr && sc->kind == 0x37 && (g.flags & 0x40) != 0 && static_cast<u32>(sc->step - 3) < 2) {
        counters[2] = 3;
        return;
      }
      loop_limit = 0;
      anim = counters[2] == 3 ? 0xE : 0;
      animate(0);
      if (g.flags != 0) return;
      if (counters[2] == 1 && loops != 0) {
        counters[2] = 2;
        spell_cooldown = 10;
        spell_cast(0x43, this, target, 0);
        return;
      }
      if (counters[1] == 0) {
        counters[1] = 1;
        spell_data_request(0x43, 0x3C, this);
        if (counters[1] == 0) return;
      }
      counters[3] += g.frame_ticks;
      if (counters[3] < 0x3D || counters[2] != 0) return;
      if (spell_data_status(0x43, this) == 1) return;
      counters[2] = 1;
      bobj_anim_reset(this);
      return;
    }
    action_kind = 0;
    begin_action();
  }

  // 0x80089098 [25]: once: 64 beams out of its centre, rings at its centre and its feet, a
  // dying voice (0x4b3-0x4b5); then the usual fade.
  void state_dying() override {
    if (counters[0] == 0) {
      counters[0] = 1;
      for (int i = 0; i < 0x40; ++i) {
        const Vec4 c = centre_of(this);
        const Vec4 d = vec_random_around(c.data(), 0x3C0);
        const Vec4 c2 = centre_of(this);
        effect_spawn_beam_at(c2.data(), d.data(), 4, 0x280, 0x20, 0x4000, 0x800, 0);
      }
      const Vec4 c = centre_of(this);
      effect_spawn_sparks_at(c.data(), 0x14, 0x60, 0, 0x60, 0x3C, 4, 3, 0x40, 1);
      const s32 feet[3] = {pos[0], bobj_ground_height(this), pos[2]};
      effect_spawn_sparks_at(feet, 0x14, 0x100, 0x40, 0x10, 0x5A, 4, 0, 0x40, 1);
      voice_play_entry(static_cast<int>(rng_range(3)) + 0x4B3, this);  // (tag 0)
      bsfx_play_at(0x21, this);
      bsfx_play_at(0x22, this);
    }
    EnemyUnit::state_dying();
  }
};

// 0x80088a78: the beam: a textured cylinder (the anim 0xb cel) of 6 x 8 single quads along
// x in front of it (0xa0 a ring, tapering to a point at the far end), radius +0x10c^2 *
// 0xa0 >> 16, turning 0x28 a tick (+0x12c). For 70 ticks while the owner is still in
// attack 1 (state 9) the strength +0x10c grows to 0x100, a glow sprite (anim 0x11) every
// 12 ticks, and every 4 ticks a hit (power 0x80, along its facing) on each standing member
// from 0x40 behind to 0x3c0 in front within 0x190 of its line; then it shrinks away.
void shot_beam(Shot* s) {
  BattleGlobals& g = bg();
  s->layer = 6;
  s->anim = 0xB;
  bobj_anim_draw(s, g.flags | 0x9800u);
  const FrameUv& t = frame_uv();
  const s32 uv[4] = {t.u0, t.v0, t.u1, t.v1};
  const u16 cel_tpage = t.tpage, cel_clut = t.clut;
  const s32 m = s->aux * 0xA0;
  const s32 strength = (m < 0 ? m + 0xFF : m) >> 8;
  if (uv[1] != uv[3]) {
    const u32 tag = bobj_poly_tag(s, cel_tpage);
    s32 c[4][4] = {};  // scratchpad 0x1f8003c0: far / near at this angle, then at the previous
    s32 x = 0;
    for (int i = 0; i < 6; ++i, x += 0xA0) {
      const s32 r = (strength * s->aux) >> 8;
      const s32 r_far = i == 5 ? 0 : r;
      s32 cs = bcos(static_cast<u32>(s->p12c)), sn = bsin(static_cast<u32>(s->p12c));
      c[1][0] = bobj_front_x(s, x);
      c[1][1] = s->pos[1] + cs * r;
      c[1][2] = s->pos[2] + sn * r;
      c[0][0] = bobj_front_x(s, x + 0xA0);
      c[0][1] = s->pos[1] + cs * r_far;
      c[0][2] = s->pos[2] + sn * r_far;
      for (int j = 0, a = 0x200; j < 8; ++j, a += 0x200) {
        cs = bcos(static_cast<u32>(a + s->p12c));
        sn = bsin(static_cast<u32>(a + s->p12c));
        for (int k = 0; k < 2; ++k) {
          c[k + 2][0] = c[k][0];
          c[k + 2][1] = c[k][1];
          c[k + 2][2] = c[k][2];
        }
        c[1][1] = s->pos[1] + cs * r;
        c[1][2] = s->pos[2] + sn * r;
        c[0][1] = s->pos[1] + cs * r_far;
        c[0][2] = s->pos[2] + sn * r_far;
        draw_textured_patch(uv, c, cel_clut, cel_tpage, s->layer, tag, 1);
      }
    }
  }
  if (g.flags != 0) return;
  s->p12c = (s->p12c + g.frame_ticks * 0x28) & 0xFFF;
  s->p130 += g.frame_ticks;
  if (s->p130 < 0x47 && g.state == kStateFighting && s->owner->action == 9) {
    s->aux = static_cast<s16>(s->aux + g.frame_ticks * 0x10);
    if (0x100 < s->aux) s->aux = 0x100;
    Unit* units[8] = {};
    Vec4 points[8];
    s32 x0, x1;
    if (s->facing[0] < 0) {
      x1 = s->pos[0] + 0x40000;
      x0 = s->pos[0] - 0x3C0000;
    } else {
      x1 = s->pos[0] + 0x3C0000;
      x0 = s->pos[0] - 0x40000;
    }
    int n = 0;
    s->p128 -= g.frame_ticks;
    if (s->p128 <= 0) {
      s->p128 += 0xC;
      if (Sprite* sp = shot_spawn_sprite(s, 0x11)) {
        sprite_set_pos_v(sp, s->pos);
        sprite_set_fade(sp, 0x80, -4, 8);
        sp->layer = 0xC;
        sp->scale_y = 0x1800;
        sp->scale_x = 0x1800;
      }
    }
    s->p134 -= g.frame_ticks;
    if (s->p134 >= 0) return;
    s->p134 += 4;
    for (PartyUnit* u : g.party_units) {
      if (unit_is_down(u)) continue;
      if (u->pos[0] < x0 || x1 <= u->pos[0]) continue;
      const s32 on_line[3] = {s->pos[0], u->pos[1] + u->size[1] * -0x2000, u->pos[2]};
      if (0x18FFFF < vec_distance_fx(on_line, s->pos)) continue;
      units[n] = u;
      points[n] = centre_of(u);
      ++n;
    }
    unit_attack_targets(s->owner, n, units, points, 0x80, s->facing[0] < 0 ? 0x800 : 0, 0);
  } else {
    s->aux = static_cast<s16>(s->aux - g.frame_ticks * 0x10);
    if (s->aux < 0) shot_kill(s);
  }
}

}  // namespace

std::unique_ptr<EnemyUnit> enemy_mod_vesper_create() { return std::make_unique<EnemyVesper>(); }

}  // namespace ss::game::battle
