// The rickis' enemy-code module: block 93f597beef0e (1260 bytes, slot A linked at 0x80088110),
// vtable 0x800884d4, monster types 0x5d Ricki, 0x5e Rikilo, 0x5f Rikiha (0x26c-byte objects),
// encounters 131 132 136 138-140 ... (13). They hover (+0x160 = 8); a 4-loop peck that
// marks whoever it hits with an effect sprite, and a rise-and-swoop at the target with
// double ATK.

#include "game/battle/combat.h"
#include "game/battle/enemy_classes.h"
#include "game/battle/enemy_support.h"
#include "game/battle/sprite.h"
#include "game/battle/unit_move.h"

namespace ss::game::battle {
namespace {

struct EnemyRicki : EnemyUnit {
  // 0x80088110: enemy_create, the module's vtable, the hover amplitude and the hit sounds.
  EnemyRicki() {
    hover_amp = 8;
    sfx_hit = 0x4B;
    sfx_crit = 0x49;
  }

  // 0x80088160 [26]: the swoop (attack 2) from 0x200 away, else the default choice.
  void choose_target() override {
    if (enemy_try_approach(this, 0x200, 0x2000, 2, 0x200) == 0) EnemyUnit::choose_target();
  }

  // 0x800881ac [27]: the peck (anim 4, 4 loops): a landed hit puts an effect sprite (anim
  // 7, 8 loops) on each member hit, at its centre, in front of it.
  void attack1() override {
    anim = 4;
    loop_limit = 4;
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
    if (unit_attack_targets(this, n, units, points, 0x100, 9999, 0) == 0) return;
    for (int i = 0; i < n; ++i) {
      Sprite* sp = unit_play_anim_ex(this, 7, 8);
      if (sp == nullptr) continue;
      sprite_attach(sp, units[i], 0, -(units[i]->size[1] << 13), 0);
      sp->layer = -2;
    }
  }

  // 0x8008834c [28]: the swoop: the melee step with double battle ATK (a landed hit ends
  // it); 0x11 ticks rising (x 1, y -4), then a dive at 0x18 toward where the target stood
  // until 0x19 ticks.
  void attack2() override {
    BattleGlobals& g = bg();
    loop_limit = 0;
    anim = 5;
    Stats* s = st();
    const s16 atk = s->atk[2];
    s->atk[2] = static_cast<s16>(atk << 1);
    enemy_attack_step(this);
    s->atk[2] = atk;
    if (hit_landed != 0) {
      action_kind = 0;
      begin_action();
    }
    if (g.flags != 0) return;
    counters[1] += g.frame_ticks;
    if (counters[0] == 0) {
      vel[0] = bobj_facing_fx(this, 1);
      vel[1] = -0x4000;
      vel[2] = 0;
      bobj_move(this, vel);
      if (counters[1] < 0x11) return;
      counters[1] = 0;
      counters[0] = 1;
      if (target == nullptr) return;
      const Vec4 v = vec_velocity_toward(pos, target->pos, 0x18);
      vel[0] = v.x;
      vel[1] = v.y;
      vel[2] = v.z;
      return;
    }
    bobj_move(this, vel);
    if (counters[1] < 0x19) return;
    action_kind = 0;
    begin_action();
  }
};

}  // namespace

std::unique_ptr<EnemyUnit> enemy_mod_ricki_create() { return std::make_unique<EnemyRicki>(); }

}  // namespace ss::game::battle
