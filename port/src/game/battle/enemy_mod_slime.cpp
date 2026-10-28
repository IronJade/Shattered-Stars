// Slime's enemy-code module: block 525f043c2fcd (2048 bytes, slot C linked at 0x8008b1f0, to
// 0x8008b9f0), vtable 0x8008b8c8, ctor 0x8008b1f0 (0x26c-byte objects); monster types 0x1f
// Slime, 0x20 Hood, 0x21 Blackslime, 0x8b Huntinggel (encounters 22 23 24 25 26 55 ...).
// A spitting blob (attack 2) that splits in two (attack 3) when a formation slot is free:
// the copy takes half its HP and both jump apart. The plain Slime never splits.

#include <array>
#include <cstring>
#include <memory>

#include "game/battle/enemy_classes.h"
#include "game/battle/enemy_modules.h"
#include "game/battle/enemy_support.h"
#include "game/battle/shot.h"
#include "game/boot/rng.h"

namespace ss::game::battle {
namespace {

u32 rng15() { return static_cast<u32>(rng_next() & 0x7FFF); }  // rng_next15 0x800103b4

// The monster types the split waits for (the gerels and the slimes): none of them may be
// splitting (state 11) at the time.
bool is_splitter_type(s16 t) {
  return (0x10 <= t && t <= 0x12) || (0x1F <= t && t <= 0x21) || (0x81 <= t && t <= 0x83) || t == 0x8A ||
         t == 0x8B;
}

struct EnemySlime : EnemyUnit {
  // 0x8008b1f0: enemy_create, the module's vtable, hit sounds 0x4c / 0x4d.
  EnemySlime() {
    sfx_hit = 0x4C;
    sfx_crit = 0x4D;
  }
  // The split's copy (attack 3): the original copies the parent's 0x26c bytes over a
  // freshly constructed slime (keeping the new vtable). The port's own bookkeeping below is
  // not copied.
  EnemySlime(const EnemySlime& o) : EnemyUnit(o) {}
  EnemySlime& operator=(const EnemySlime&) = delete;

  // Port bookkeeping (not in the original object): a copy's own 0x60-byte stats record (the
  // console's __builtin_new(0x60)).
  std::array<u8, 0x60> own_stats_{};

  // 0x8008b238 [26]: spit from afar (attack 2); else 12000 / 32768 of the time (not the
  // plain Slime) split (attack 3) when a formation slot is free, no slime or gerel is
  // splitting and there is room 0x88 on both sides; else the usual choice.
  void choose_target() override {
    const s16 type = static_cast<s16>(st()->id);
    if (enemy_try_approach(this, 800, 12000, 2, type == 0x8B ? 800 : 500) != 0) return;
    if (rng15() < 12000 && static_cast<s16>(st()->id) != 0x1F) {
      bool free_slot = false;
      bool splitting = false;
      for (EnemyUnit* u : bg().enemy_units) {
        if (u == nullptr) {
          free_slot = true;
        } else if (is_splitter_type(static_cast<s16>(u->st()->id)) && u->action == 11) {
          splitting = true;
        }
      }
      if (free_slot && !splitting) {
        const s32 left[3] = {-0x88000, 0, 0};
        const s32 right[3] = {0x88000, 0, 0};
        bool room = false;
        if (unit_check_move(this, left, 7) == nullptr) room = unit_check_move(this, right, 7) == nullptr;
        if (room) {
          action_kind = 3;
          begin_action();
          counters[1] = 0;
          return;
        }
      }
    }
    EnemyUnit::choose_target();
  }

  // 0x8008b41c [28]: one spit past frame 3 at a point 0x60 beyond the mouth (life 0x24,
  // Huntinggel 0x48).
  void attack2() override {
    anim = 5;
    if (static_cast<s16>(animate(static_cast<u32>(attack_recovery))) == -1) {
      action_kind = 0;
      begin_action();
      return;
    }
    if (bg().flags != 0 || counters[1] != 0 || frame < 4) return;
    ShotDesc* d = shot_desc_init(6);
    const s32 from[3] = {bobj_front_x(this, 0x60), pos[1] - 0x80000, pos[2]};
    d->speed = 0x18;
    d->point[0] = from[0] + bobj_facing_fx(this, 0x60);
    d->point[1] = from[1];
    d->point[2] = from[2];
    d->life = static_cast<s16>(st()->id) == 0x8B ? 0x48 : 0x24;
    d->flags = 0x100006;
    shot_spawn_projectile(this, from, d, this);
    counters[1] = 1;
  }

  // 0x8008b598 [29]: the split, once past frame 7 (between frames): it hops (vel x 4,
  // vel y -8, airborne, 0x60 up) and a copy of it takes the first free formation slot,
  // hopping the other way, 0x60 to the left of it while it moves 0x60 right; both halve
  // their HP. Afterwards both fall back to the ground. The animation runs with the battle
  // flags as its flags (frozen while any is up).
  void attack3() override {
    BattleGlobals& g = bg();
    anim = 7;
    if (7 < frame && ticks_left < 1) {
      if (counters[1] == 0) {
        counters[1] = 1;
        split();
      } else {
        bobj_move(this, vel);
        vel[1] += g.frame_ticks * 0x1000;
        const s32 ground = bobj_ground_height(this);
        if (ground < pos[1]) {
          pos[1] = ground;
          vel[0] = 0;
          vel[1] = 0;
        }
      }
    }
    if (static_cast<s16>(animate(g.flags)) == -1) {
      action_kind = 0;
      begin_action();
    }
  }

  // (0x8008b598's split: the copy is made after the parent's hop is set, so it starts in
  // the same state, frame and +0x178 = 1.)
  void split() {
    BattleGlobals& g = bg();
    for (int i = 0; i < 8; ++i) {
      if (g.enemy_units[i] != nullptr) continue;
      vel[0] = 0x4000;
      vel[1] = -0x8000;
      airborne = 1;
      pos[1] -= 0x60000;
      // (The console's allocation failures, which give up or free the copy, cannot happen.)
      auto copy = std::make_unique<EnemySlime>(*this);
      EnemySlime* c = copy.get();
      enemy_adopt(std::move(copy));  // (battle_free_all frees it with the other enemies)
      g.enemy_units[i] = c;
      c->vel[0] = -0x4000;
      std::memcpy(c->own_stats_.data(), stats, c->own_stats_.size());
      c->stats = c->own_stats_.data();
      c->slot = static_cast<u8>(i);
      g.enemy_stats[i] = c->stats;
      task_register(c, 2);
      c->pos[0] -= 0x60000;
      pos[0] += 0x60000;
      c->st()->hp = c->st()->hp / 2;
      ++g.enemy_count;
      st()->hp = st()->hp / 2;
      return;
    }
  }
};

}  // namespace

std::unique_ptr<EnemyUnit> enemy_mod_slime_create() { return std::make_unique<EnemySlime>(); }

}  // namespace ss::game::battle
