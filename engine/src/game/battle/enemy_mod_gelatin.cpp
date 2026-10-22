// The gelatins' enemy-code module: block ff10592f79db (2284 bytes, slot A linked at 0x80088110),
// vtable 0x800888d4, monster types 0x63 Gelatincube, 0x64 Gelatinfloat, 0x65 Gelatinblock
// (0x26c-byte objects), encounters 45 46 562 563. A spit, and reactions to being hit: the
// first light hit may split it in two (each with half the HP), a knock-down makes it
// bounce (attack 2), and now and then (often after a spell) it bursts into 32 globs.

#include <array>
#include <memory>

#include "game/battle/bsfx.h"
#include "game/battle/enemy_classes.h"
#include "game/battle/enemy_support.h"
#include "game/battle/particle.h"
#include "game/battle/render.h"
#include "game/battle/shot.h"
#include "game/boot/rng.h"

namespace ss::game::battle {
namespace {

u32 rng15() { return static_cast<u32>(rng_next() & 0x7FFF); }  // rng_next15 0x800103b4

// g_spell_landed (0x8006fb2f): set when a spell's finishing hit resolves.
u8& spell_landed() { return spell_landed_flag(); }

struct EnemyGelatin : EnemyUnit {
  // A split-off gelatin's own stats block (the original's __builtin_new(0x60)).
  std::array<u8, 0x60> own_stats{};

  // 0x80088110: enemy_create, the module's vtable and the hit sounds.
  EnemyGelatin() {
    sfx_hit = 0x4C;
    sfx_crit = 0x4E;
  }
  EnemyGelatin(const EnemyGelatin&) = default;

  // 0x800884c4 [24]: the hit reactions. A light hit (action 3), while +0x174 is clear:
  // 1/32 (or 12000/32768 after a spell landed, which it consumes) the burst (attack 4);
  // the first hit then marks +0x174 and, half the time with 0x80 units free on both sides,
  // splits (attack 3). A knock-down (action 4) after that bounces (attack 2).
  void state_hit() override {
    if (bg().flags != 0) {
      EnemyUnit::state_hit();
      return;
    }
    if (action == 3 && counters[0] == 0) {
      bool burst = false;
      if (rng15() < 0x400 || (spell_landed() != 0 && rng15() < 12000)) burst = true;
      if (burst) {
        spell_landed() = 0;
        action_kind = 4;
        begin_action();
        return;
      }
    }
    if (counters[0] == 0) {
      counters[0] = 1;
      const s32 left[3] = {-0x80000, 0, 0};
      const s32 right[3] = {0x80000, 0, 0};
      bool room = false;
      if (rng15() < 0x4000 && unit_check_move(this, left, 7) == nullptr) room = unit_check_move(this, right, 7) == nullptr;
      if (room) {
        action_kind = 3;
        begin_action();
        return;
      }
    }
    if (action != 4) {
      EnemyUnit::state_hit();
      return;
    }
    action_kind = 2;
    begin_action();
  }

  // 0x80088158 [27]: one homing spit (anim 7, speed 0x20, 40 ticks) past frame 2.
  void attack1() override {
    anim = 4;
    if (static_cast<s16>(animate(static_cast<u32>(attack_recovery))) == -1) {
      action_kind = 0;
      begin_action();
      return;
    }
    if (bg().flags != 0 || counters[1] != 0 || frame <= 2) return;
    bsfx_play_at(0x3C, this);
    ShotDesc* d = shot_desc_init(7);
    d->speed = 0x20;
    d->life = 0x28;
    d->flags = 0x100006;
    d->target = target;
    const s32 from[3] = {pos[0] + (facing[0] > 0 ? 0x40000 : -0x40000), pos[1] - 0x50000, pos[2]};
    shot_spawn_projectile(this, from, d, this);
    counters[1] = 1;
  }

  // 0x8008829c [28]: the bounce after a knock-down (anim 3, the usual melee step).
  void attack2() override {
    anim = 3;
    enemy_attack_step(this);
  }

  // 0x8008864c [29]: the split. At the end of anim 5 (animated with the battle flags as
  // its argument: it stops while any is set) back to idle, then into the first free enemy
  // slot an exact copy of itself with a copy of its stats, 0x80 to its left while it moves
  // 0x80 right; both keep half the HP.
  void attack3() override {
    BattleGlobals& g = bg();
    anim = 5;
    if (static_cast<s16>(animate(g.flags)) != -1) return;
    action_kind = 0;
    begin_action();
    for (int i = 0; i < 8; ++i) {
      if (g.enemy_units[i] != nullptr) continue;
      bsfx_play_at(0xF, this);
      // (The console builds it with the module constructor, copies the 0x26c bytes over it
      // and puts the vtable back; if the stats block cannot be allocated it deletes it
      // (slot 36, flag 3) and clears the slot again. Allocation does not fail here.)
      auto copy = std::make_unique<EnemyGelatin>(*this);
      auto* c = copy.get();
      enemy_adopt(std::move(copy));  // (battle_free_all frees it with the other enemies)
      g.enemy_units[i] = c;
      c->stats = c->own_stats.data();
      *c->st() = *st();
      c->slot = static_cast<u8>(i);
      g.enemy_stats[i] = c->stats;
      task_register(c, 2);
      c->pos[0] -= 0x80000;
      pos[0] += 0x80000;
      c->st()->hp /= 2;
      ++g.enemy_count;
      st()->hp /= 2;
      return;
    }
  }

  // 0x800882c0 [30]: the burst: past frame 3 a violet ring and 32 globs (anim 7, speed 8,
  // 0x200 ticks) from its centre toward a circle of radius 0x100 round it; at the end of
  // anim 6 it dies (death 2, HP 0).
  void attack4() override {
    anim = 6;
    if (static_cast<s16>(animate(static_cast<u32>(attack_recovery))) == -1) {
      death = 2;
      st()->hp = 0;
      return;
    }
    if (bg().flags != 0 || counters[1] != 0 || frame <= 3) return;
    counters[1] = 1;
    bsfx_play_at(0x50, this);
    bsfx_play_at(0x34, this);
    bsfx_play_at(0x29, this);
    const s32 centre[4] = {pos[0], pos[1] + size[1] * -0x2000, pos[2], 0};
    particle_spawn_ring(centre, 0x18, 0x60, 0xC, 0x1C0, 0x800040, 0xFF00FF, 0, 0);
    for (int i = 0; i < 0x20; ++i) {
      ShotDesc* d = shot_desc_init(7);
      const u32 a = static_cast<u32>(i << 7);
      d->speed = 8;
      d->point[0] = pos[0] + bcos(a) * 0x100;
      d->point[1] = pos[1] + size[1] * -0x2000;
      d->point[2] = pos[2] + bsin(a) * 0x100;
      d->life = 0x200;
      d->flags = 0x100006;
      const s32 from[3] = {pos[0], pos[1] + size[1] * -0x2000, pos[2]};
      shot_spawn_projectile(this, from, d, this);
    }
  }
};

}  // namespace

std::unique_ptr<EnemyUnit> enemy_mod_gelatin_create() { return std::make_unique<EnemyGelatin>(); }

}  // namespace ss::game::battle
