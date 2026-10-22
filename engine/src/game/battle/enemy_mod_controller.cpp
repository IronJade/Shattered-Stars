// The Controller module: block add9ce0ae096 (996 bytes, slot B linked at 0x80089f18), vtable
// 0x8008a1d4 (the factory stores it over the EnemyUnit one: no constructor), monster types
// 0x2c Controller, 0x2d Controlkey, 0x2e PADmaster (0x26c-byte objects; 7 encounters, 131
// 140 156 161 240 257 ...). Its swing, when it fails to do damage, latches it onto the unit
// it met: it clings beside that target (holding it, status bit 0x10) for up to 600 ticks.
// Now and then it switches between two animation tables.

#include "game/battle/combat.h"
#include "game/battle/enemy_classes.h"
#include "game/battle/enemy_modules.h"
#include "game/battle/enemy_support.h"
#include "game/boot/rng.h"

namespace ss::game::battle {
namespace {

u32 rng15() { return static_cast<u32>(rng_next() & 0x7FFF); }  // rng_next15 0x800103b4

struct EnemyController : EnemyUnit {
  // 0x80089f18 [32]: unfrozen, 1 / 32 of the frames it picks animation table 0 or 1 (0x14
  // bytes apart in its layout).
  int pre_think() override {
    if (bg().flags == 0 && rng15() < 0x400) animset->layout = model_layout + rng_range(2) * 0x14;
    return 0;
  }

  // 0x80089f84 [27]: anim 4. Not latched (+0x174 = 0): the swing's hit check; a hit that
  // did not land damage (unit_attack_targets != 1) latches it: +0x174 = 1, loop limit 0,
  // the unit met becomes its target. Latched: +0x174 counts the ticks, the target gets
  // status bit 0x10 and it sits on the target's approach point facing it, until there is
  // none, 600 ticks passed or (unlatched) the animation ends; then idle.
  void attack1() override {
    BattleGlobals& g = bg();
    anim = 4;
    const bool done = static_cast<s16>(animate(static_cast<u32>(attack_recovery))) == -1;
    if (counters[0] != 0) {
      counters[0] += g.frame_ticks;
      if (target != nullptr) target->status_bits |= 0x10;
      Vec4 p{};
      if (!unit_find_approach_point(this, &p) || 0x258 < counters[0]) {
        action_kind = 0;
        begin_action();
        return;
      }
      pos[0] = p.x;
      pos[1] = p.y;
      pos[2] = p.z;
      pos_w = p.w;
      if (target != nullptr) facing[0] = pos[0] < target->pos[0] ? 0x100000 : -0x100000;
    } else if (done) {
      action_kind = 0;
      begin_action();
      return;
    }
    if (g.flags != 0 || counters[0] != 0) return;
    Unit* units[8] = {};
    Vec4 points[8];
    const int n = collider_attack_overlaps(this, units, points, 1);
    if (n == 0 || hit_landed != 0) return;
    if (unit_attack_targets(this, 1, units, points, 0x100, 9999, 0) == 1) return;
    counters[0] = 1;
    loop_limit = 0;
    target = units[0];
  }
};

}  // namespace

std::unique_ptr<EnemyUnit> enemy_mod_controller_create() { return std::make_unique<EnemyController>(); }

}  // namespace ss::game::battle
