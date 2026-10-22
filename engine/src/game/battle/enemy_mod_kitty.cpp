// The cats' enemy-code module: block ef67afba2e66 (1172 bytes, slot A linked at 0x80088110),
// vtable 0x8008847c, monster type 0x5a Kitty (0x26c-byte objects), encounters 107 110. It
// hovers (+0x160 = 8); a double-hit swipe, a hop forward with double ATK (types other than
// the Kitty) and a dash at 0x10 a tick from afar, both ending at the first hit.

#include "game/battle/enemy_classes.h"

namespace ss::game::battle {
namespace {

struct EnemyKitty : EnemyUnit {
  // 0x80088110: enemy_create, the module's vtable and the hover amplitude.
  EnemyKitty() { hover_amp = 8; }

  // 0x80088150 [26]: from afar the dash (attack 3); nearer the hop (2), except the Kitty
  // itself; else the default choice.
  void choose_target() override {
    if (enemy_try_approach(this, 700, 0x2000, 3, 0x180) != 0) return;
    if (static_cast<s16>(st()->id) != 0x5A && enemy_try_approach(this, 400, 0x2000, 2, 0x100) != 0) return;
    EnemyUnit::choose_target();
  }

  // 0x800881d4 [27]: the swipe: the melee step, the landed flag cleared once past frame 5
  // (a second hit).
  void attack1() override {
    anim = 4;
    enemy_attack_step(this);
    if (frame <= 5 || counters[1] != 0) return;
    counters[1] = 1;
    hit_landed = 0;
  }

  // 0x80088230 [28]: the hop: the melee step with double battle ATK; 9 ticks up and
  // forward (x 1, y -8), then forward and down (x 0x10, y 1) until 0x19 ticks; ends early
  // on a hit.
  void attack2() override {
    BattleGlobals& g = bg();
    loop_limit = 0;
    anim = 5;
    Stats* s = st();
    const s16 atk = s->atk[2];
    s->atk[2] = static_cast<s16>(atk << 1);
    enemy_attack_step(this);
    s->atk[2] = atk;
    if (g.flags == 0) {
      counters[1] += g.frame_ticks;
      if (counters[0] == 0) {
        vel[0] = bobj_facing_fx(this, 1);
        vel[1] = -0x8000;
        vel[2] = 0;
        bobj_move(this, vel);
        if (8 < counters[1]) {
          counters[1] = 0;
          counters[0] = 1;
        }
      } else {
        vel[0] = bobj_facing_fx(this, 0x10);
        vel[1] = 0x1000;
        vel[2] = 0;
        bobj_move(this, vel);
        if (0x18 < counters[1]) {
          action_kind = 0;
          begin_action();
        }
      }
    }
    if (hit_landed == 0) return;
    action_kind = 0;
    begin_action();
  }

  // 0x80088380 [29]: the dash: the melee step moving 0x10 a tick forward for 0x20 ticks;
  // ends early on a hit.
  void attack3() override {
    BattleGlobals& g = bg();
    loop_limit = 0;
    anim = 6;
    enemy_attack_step(this);
    if (g.flags != 0) return;
    vel[0] = bobj_facing_fx(this, 0x10);
    vel[1] = 0;
    vel[2] = 0;
    bobj_move(this, vel);
    counters[1] += g.frame_ticks;
    if (0x20 < counters[1]) {
      action_kind = 0;
      begin_action();
    }
    if (hit_landed == 0) return;
    action_kind = 0;
    begin_action();
  }
};

}  // namespace

std::unique_ptr<EnemyUnit> enemy_mod_kitty_create() { return std::make_unique<EnemyKitty>(); }

}  // namespace ss::game::battle
