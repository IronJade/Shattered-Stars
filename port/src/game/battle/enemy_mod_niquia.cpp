// The Niquia module: block 701c156d523b (1648 bytes, slot B linked at 0x80089f18), vtable
// 0x8008a460, monster types 0x35 NiquiaHG, 0x36 NiquialM (0x26c-byte objects; 9 encounters,
// 135 153 154 172 173 177 ...). A parasite: it picks an idle Ericodus (0x38-0x3a), Miel
// (0x47-0x49) or Ricki (0x5d-0x5f) at random, flies onto its back and merges with it: the
// host gets its ATK, HP and MP and another animation table (the armed variant), and the
// Niquia leaves the battle. It never attacks on its own.

#include "game/battle/bsfx.h"
#include "game/battle/enemy_classes.h"
#include "game/battle/enemy_modules.h"
#include "game/battle/enemy_support.h"
#include "game/boot/rng.h"

namespace ss::game::battle {
namespace {

struct EnemyNiquia : EnemyUnit {
  // 0x80089f18: enemy_create, the module's vtable, a larger shadow (+0x152 = 0x180).
  EnemyNiquia() { shadow_scale2 = 0x180; }

  // +0x17c: the host kind (1 Ericodus, 2 Miel, 3 Ricki; 0 none).
  s32& host_kind() { return counters[2]; }

  // 0x80089f58 [32]: approaching or attaching (states 1 / 9): the walk goal is the host's
  // back (behind it by 0x1a / 0x58 / 0x20 units by host kind) at its own height, and
  // +0x174 the height to reach (0xf0 / 0x70 / 0x90 above the host's feet).
  int pre_think() override {
    if (action != 1 && action != 9) return 0;
    // (A wander walk (state 1) has no target: the console reads low RAM through the null
    // pointer; the port reads a zeroed object instead.)
    static const BObj kNoTarget;
    const BObj* tgt = target != nullptr ? static_cast<const BObj*>(target) : &kNoTarget;
    move_mode = 1;
    walk_dest[1] = pos[1];
    walk_dest[2] = tgt->pos[2];
    switch (host_kind()) {
      case 1:
        walk_dest[0] = tgt->pos[0] + bobj_facing_fx(tgt, -0x1A);
        counters[0] = tgt->pos[1] - 0xF0000;
        break;
      case 2:
        walk_dest[0] = tgt->pos[0] + bobj_facing_fx(tgt, -0x58);
        counters[0] = tgt->pos[1] - 0x70000;
        break;
      case 3:
        walk_dest[0] = tgt->pos[0] + bobj_facing_fx(tgt, -0x20);
        counters[0] = tgt->pos[1] - 0x90000;
        break;
      default:
        break;
    }
    return 0;
  }

  // 0x8008a084 [26]: a random enemy slot; a standing, idle or approaching (action < 2) host
  // type becomes the target: the host stops (kind 0 + slot 12, facing y cleared) and the
  // Niquia approaches it facing the same way (slot 33, then action kind 1).
  void choose_target() override {
    EnemyUnit* u = bg().enemy_units[rng_range(8)];
    if (unit_is_down(u) || 1 < u->action) return;
    host_kind() = 0;
    const u16 t = u->st()->id;
    if (static_cast<u16>(t - 0x38) < 3) host_kind() = 1;
    if (static_cast<u16>(t - 0x47) < 3) host_kind() = 2;
    if (static_cast<u16>(t - 0x5D) < 3) host_kind() = 3;
    if (host_kind() == 0) return;
    u->action_kind = 0;
    u->begin_action();
    u->facing[1] = 0;
    target = u;
    action = 1;
    facing[0] = u->facing[0];
    begin_move();
    action_kind = 1;
  }

  // 0x8008a1bc [27]: the attachment (anim 4, looping): it sinks 4 units a tick onto the host's
  // back; reaching the height (+0x174) it merges: sfx 0x28 / 0x25, it leaves the battle
  // (+0x12f = 1, one enemy fewer), the host gets +0x180 = 2, its armed animation table
  // (Ericodus +0x44 with the Niquia's +0x24e, Miel +0x24, Ricki +0x1c) and the Niquia's ATK
  // (all three), HP, max HP, MP and max MP added; banner 0x2712. A downed host ends it.
  void attack1() override {
    BattleGlobals& g = bg();
    anim = 4;
    loop_limit = 0;
    if (unit_is_down(target)) {
      action_kind = 0;
      begin_action();
      return;
    }
    vel[1] = 0x4000;
    if (g.flags != 0) {
      animate(0);
      return;
    }
    bobj_move(this, vel);
    pos[0] = walk_dest[0];
    pos[2] = walk_dest[2];
    if (pos[1] <= counters[0]) {
      animate(0);
      return;
    }
    bsfx_play_at(0x28, this);
    bsfx_play_at(0x25, this);
    removed = 1;
    Unit* host = target;
    host->counters[3] = 2;
    --g.enemy_count;
    switch (host_kind()) {
      case 1:
        host->animset->layout = host->model_layout + 0x44;
        // (+0x24e is EnemyUnit::odds[1]; the host is always an enemy unless a counter attack
        // retargeted it at its attacker, where the console writes into the PartyUnit.)
        if (host->is_enemy != 0) static_cast<EnemyUnit*>(host)->odds[1] = odds[1];
        break;
      case 2: host->animset->layout = host->model_layout + 0x24; break;
      case 3: host->animset->layout = host->model_layout + 0x1C; break;
      default: break;
    }
    Stats* h = host->st();
    const Stats* s = st();
    h->atk[2] = static_cast<s16>(h->atk[2] + s->atk[2]);
    h->atk[0] = static_cast<s16>(h->atk[0] + s->atk[0]);
    h->atk[1] = static_cast<s16>(h->atk[1] + s->atk[1]);
    h->hp += s->hp;
    h->hp_max += s->hp_max;
    h->mp = static_cast<s16>(h->mp + s->mp);
    h->mp_max = static_cast<s16>(h->mp_max + s->mp_max);
    battle_message_show(0x2712);
  }
};

}  // namespace

std::unique_ptr<EnemyUnit> enemy_mod_niquia_create() { return std::make_unique<EnemyNiquia>(); }

}  // namespace ss::game::battle
