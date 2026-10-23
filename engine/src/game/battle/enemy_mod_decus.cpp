// Decus's enemy-code module: block 6c83a0ebeaad (2496 bytes, slot B linked at 0x80089f18),
// vtable 0x8008a7b0, monster type 0xcc (Decus), encounter 537 (with Vesper; 0x270-byte
// objects). A fighter: two swings (the second chains into the first when it meets
// someone) and spell 0x44 cast after a chant that draws sparks in (every 10 thinks at
// most); voices from the pack (intro 0x4c3, two variants).

#include <cstring>

#include "game/battle/bsfx.h"
#include "game/battle/combat.h"
#include "game/battle/enemy_classes.h"
#include "game/battle/enemy_modules.h"
#include "game/battle/enemy_support.h"
#include "game/battle/shot.h"
#include "game/battle/sprite.h"
#include "game/boot/rng.h"

namespace ss::game::battle {
namespace {

// The target's centre (its body's mid height).
Vec4 centre_of(const Unit* u) { return {u->pos[0], u->pos[1] + u->size[1] * -0x2000, u->pos[2], 0}; }

struct EnemyDecus : EnemyUnit {
  s32 spell_cooldown = 0;  // +0x26c: thinks before spell 0x44 may be chosen again

  // 0x80089f18: enemy_create, the module's vtable, intro voice 0x4c3 with 2 variants.
  EnemyDecus() {
    intro_voice = 0x4C3;
    voice_variants = 2;
  }

  // 0x80089f64 [4]: interrupted while chanting (state 11, attack 3), its spell data request
  // is dropped.
  void interrupt() override {
    if (action == 0xB) spell_data_cancel_owner(this);
  }

  // 0x8008a774 [9]: chants spell skill 0x27 at level 15.
  int skill_level(int skill) override { return skill == 0x27 ? 0xF : 0; }

  // 0x8008a278 [34]: a voice clip from the pack's extra data: hit (0) the fifth, attacks 1
  // and 2 the fourth.
  void on_attack_start(int attack) override {
    const void* clip;
    if (attack == 0) {
      clip = enemy_extra_data(4);
    } else if (attack >= 0 && attack < 3) {
      clip = enemy_extra_data(3);
    } else {
      return;
    }
    // (The console plays unconditionally; the port has no clip until the table is exposed.)
    if (clip != nullptr) bsfx_play(0, clip, this, 1);
  }

  // 0x80089f94 [26]: once the cooldown has run out, the spell (attack 3 at once where it
  // stands: message 0x42b, voice 0x4d0 or 0x4d1); else the second swing (2) from 300 away
  // (nearer a quarter of the time); else the default.
  void choose_target() override {
    if ((spell_cooldown == 0 || --spell_cooldown == 0) && enemy_try_approach(this, 500, 0x2400, 3, 0x280) != 0) {
      counters[2] = 0;
      counters[3] = 0;
      battle_message_show(0x42B);
      voice_play_entry(static_cast<int>(rng_range(2)) + 0x4D0, this, -1);
      action_kind = 3;
      begin_action();
      return;
    }
    if (enemy_try_approach(this, 300, 0x2000, 2, 0x10) == 0) EnemyUnit::choose_target();
  }

  // 0x8008a06c [27]: swing 1 (anim 4).
  void attack1() override {
    anim = 4;
    swing();
  }
  // 0x8008a090 [28]: swing 2 (anim 5).
  void attack2() override {
    anim = 5;
    swing();
  }
  // 0x8008a0b4: the swings' step: the animation (at its end idle); unpaused, the melee hit
  // once per landed flag with a hit sprite (anim 0xc) at each point when damage landed;
  // swing 2 meeting anyone chains into swing 1 (with its voice clip, slot 34).
  void swing() {
    if (static_cast<s16>(animate(static_cast<u32>(attack_recovery))) == -1) {
      action_kind = 0;
      begin_action();
      return;
    }
    if (bg().flags != 0) return;
    Unit* units[8] = {};
    Vec4 points[8];
    const int n = collider_attack_overlaps(this, units, points, 1);
    if (n == 0 || hit_landed != 0) return;
    if (unit_attack_targets(this, n, units, points, 0x100, 9999, 0) != 0) {
      for (int i = 0; i < n; ++i) {
        if (Sprite* sp = unit_play_anim_ex(this, 0xC, 1)) {
          sprite_set_pos_v(sp, points[i].data());
          sp->layer = 3;
        }
      }
    }
    if (anim == 5) {
      action_kind = 1;
      begin_action();
      on_attack_start(1);
    }
  }

  // 0x8008a2c8 [29]: spell 0x44. The chant (anim 9, endless) with a spark drawn in from a
  // 600 disc each tick: the spell data is requested (priority 0x2d) on the first frame;
  // after 46 ticks, once it has loaded, the animation restarts (+0x17c = 1) and at its
  // first loop the spell is cast at the target (+0x17c = 2, cooldown 10). When the battle is
  // ending (state 3+, countdown below 20) it stops (slot 4, then idle). While the party's
  // spell 0x38 runs its steps 1-5 in the special pause it is held (+0x17c = 3, no update);
  // past step 5 the held chant goes back to idle.
  void attack3() override {
    BattleGlobals& g = bg();
    if (kStateFighting < g.state && g.end_countdown < 0x14) {
      interrupt();
    } else {
      const SpellCast* sc = spellcast_slot0();
      bool held_over = false;
      if (sc != nullptr && sc->kind == 0x38) {
        if (counters[2] == 3 && !(sc->step < 6)) {
          held_over = true;
        } else if ((g.flags & 0x40) != 0 && static_cast<u32>(sc->step - 1) < 5) {
          counters[2] = 3;
          return;
        }
      }
      if (!held_over) {
        chant();
        return;
      }
    }
    action_kind = 0;
    begin_action();
  }
  void chant() {
    BattleGlobals& g = bg();
    anim = 9;
    loop_limit = 0;
    animate(0);
    if (g.flags != 0) return;
    if (counters[2] == 1 && loops != 0) {
      counters[2] = 2;
      spell_cooldown = 10;
      spell_cast(0x44, this, target, 0);
      return;
    }
    if (counters[1] == 0) {
      counters[1] = 1;
      spell_data_request(0x44, 0x2D, this);
      if (counters[1] == 0) return;
    }
    for (int i = 0; i < g.frame_ticks; ++i) {
      Sprite* sp = unit_play_anim(this, 0x10);
      if (sp == nullptr) continue;
      const Vec4 p = vec_random_in_disc_xz(pos, 600);
      sprite_set_pos_v(sp, p.data());
      sprite_set_fade(sp, 0x80, -4, 0xC);
      const Vec4 v = vec_velocity_toward(sp->pos, pos, 0xD);
      std::memcpy(sp->vel, &v, sizeof(sp->vel));
      sp->layer = 2;
      sp->freeze_class = 2;
    }
    counters[3] += g.frame_ticks;
    if (0x2D < counters[3] && counters[2] == 0 && spell_data_status(0x44, this) != 1) {
      counters[2] = 1;
      bobj_anim_reset(this);
    }
  }

  // 0x8008a594 [25]: once: 64 beams out of its centre, rings at its centre and its feet, a
  // dying voice (0x4c5 / 0x4c6); then the usual fade.
  void state_dying() override {
    if (counters[0] == 0) {
      counters[0] = 1;
      for (int i = 0; i < 0x40; ++i) {
        const Vec4 c = centre_of(this);
        const Vec4 d = vec_random_around(c.data(), 0x3C0);
        const Vec4 c2 = centre_of(this);
        effect_spawn_beam_at(c2.data(), d.data(), 5, 0x280, 0x20, 0x4000, 0x800, 0);
      }
      const Vec4 c = centre_of(this);
      effect_spawn_sparks_at(c.data(), 0x14, 0x60, 0, 0x60, 0x3C, 5, 3, 0x40, 1);
      const s32 feet[3] = {pos[0], bobj_ground_height(this), pos[2]};
      effect_spawn_sparks_at(feet, 0x14, 0x100, 0x40, 0x10, 0x5A, 5, 0, 0x40, 1);
      voice_play_entry(static_cast<int>(rng_range(2)) + 0x4C5, this, 0);
      bsfx_play_at(0x21, this);
      bsfx_play_at(0x22, this);
    }
    EnemyUnit::state_dying();
  }
};

}  // namespace

std::unique_ptr<EnemyUnit> enemy_mod_decus_create() { return std::make_unique<EnemyDecus>(); }

}  // namespace ss::game::battle
