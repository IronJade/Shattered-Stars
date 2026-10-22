// Visseyer's enemy-code module: block 1b2600bcb2ff (2180 bytes, slot B linked at 0x80089f18, to
// 0x8008a79c), vtable 0x8008a674, 0x27c-byte objects; monster types 0x51 Visseyer, 0x52
// Kidnier, 0x53 Riverside, 0x9e Ghastricgel, 0x9f Weirdmalesk, 0xa0 E-beast (encounters 174
// 175 182 183 184 209 ...). The bite can swallow a party member (removed from the battle);
// the victim comes back out in front of the monster when it spits (attack 2), is hit
// (types below 0x9e always, the others 20 %), is interrupted while dying or dies. Inside,
// Ghastricgel digests (1 HP a tick), Weirdmalesk paralyses (status 4) and E-beast kills
// after 300 ticks, and then spits.

#include "game/battle/bsfx.h"
#include "game/battle/combat.h"
#include "game/battle/enemy_modules.h"
#include "game/battle/enemy_support.h"
#include "game/boot/rng.h"

namespace ss::game::battle {
namespace {

u32 rng15() { return static_cast<u32>(rng_next() & 0x7FFF); }  // rng_next15 0x800103b4

struct EnemyVisseyer : EnemyUnit {
  // +0x26c..+0x278: the swallowed party members (null: a free slot). The bite only
  // swallows what collider_attack_overlaps found on the party's side.
  PartyUnit* swallowed[4] = {};

  // 0x80089f18: enemy_create, the module's vtable, the four slots cleared.

  int first_swallowed() const {
    int i = 0;
    while (i < 4 && swallowed[i] == nullptr) ++i;
    return i;
  }
  bool holds_someone() const { return first_swallowed() < 4; }

  // 0x8008a340: every swallowed member comes out 0x90 in front of the monster at its
  // height, at the first free z of a zigzag search (the try counter runs on over all
  // victims and restarts at try 98); facing reset, +0x200 bit 2 cleared; then sfx 0x24.
  void release_swallowed() {
    static const s32 kZero[3] = {0, 0, 0};  // g_zero_vector 0x8006e33c
    s32 step = 0;
    for (int i = 0; i < 4; ++i) {
      PartyUnit* v = swallowed[i];
      if (v == nullptr) continue;
      v->removed = 0;
      v->pos[0] = bobj_front_x(this, 0x90);
      v->pos[1] = pos[1];
      for (int tries = 0; tries < 100; ++tries) {
        if (step == 0) {
          v->pos[2] = pos[2];
        } else if ((step & 1) == 0) {
          // ORIGINAL BUG (original_bugs.md E3): the even (nearer) steps store into +0x30
          // (vel z), not pos z, so they test the previous odd step's z again:
          //   v->vel[2] = pos[2] - step * 0x40000;
          v->pos[2] = pos[2] - step * 0x40000;
        } else {
          v->pos[2] = pos[2] + (step + 1) * 0x40000;
        }
        if (unit_check_move(v, kZero, 7) == nullptr) break;
        step = tries == 0x62 ? 0 : step + 1;
      }
      // (Facing x from the member's own -vel x: they come out facing any way; intended,
      // original_bugs.md E14.)
      v->facing[0] = -v->vel[0];
      v->facing[1] = 0;
      v->facing[2] = 0;
      ++step;
      v->appear &= 0xFB;
      swallowed[i] = nullptr;
    }
    bsfx_play_at(0x24, this);
  }

  // 0x80089f68 [27]: the bite; a hit that dealt damage swallows the first member hit
  // (rng_next15 < 0x4000, unless it is down): interrupted, removed, the digest timer
  // (+0x180) restarted.
  void attack1() override {
    anim = 4;
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
    bool swallow = false;
    if (unit_attack_targets(this, n, units, points, 0x100, 9999, 0) != 0) swallow = rng15() < 0x4000;
    if (!swallow || unit_is_down(units[0])) return;
    units[0]->interrupt();
    units[0]->removed = 1;
    for (int i = 0; i < 4; ++i) {
      if (swallowed[i] == nullptr) {
        swallowed[i] = static_cast<PartyUnit*>(units[0]);
        break;
      }
    }
    counters[3] = 0;
  }

  // 0x8008a120 [28]: the spit: at the end of the animation the swallowed come out.
  void attack2() override {
    anim = 5;
    if (static_cast<s16>(animate(static_cast<u32>(attack_recovery))) != -1) return;
    release_swallowed();
    action_kind = 0;
    begin_action();
  }

  // 0x8008a19c [4]: interrupted while dying (+0x128 > 1) it lets them out.
  void interrupt() override {
    if (death < 2) return;
    if (holds_someone()) release_swallowed();
  }

  // 0x8008a200 [24]: knocked back (state 3, +0x174 clear) with someone inside it spits
  // instead (types from 0x9e only 20 % of the time).
  void state_hit() override {
    if (bg().flags == 0 && action == 3 && counters[0] == 0 && holds_someone() &&
        (static_cast<s16>(st()->id) < 0x9E || rng_range(100) < 0x14)) {
      action_kind = 2;
      begin_action();
      return;
    }
    EnemyUnit::state_hit();
  }

  // 0x8008a2dc [25]: dying, it lets them out first.
  void state_dying() override {
    if (holds_someone()) release_swallowed();
    EnemyUnit::state_dying();
  }

  // 0x8008a4c0 [32]: digestion (unless a flag other than 0x40 is up; the timer +0x180 only
  // runs with no flag): Ghastricgel drains 1 HP a tick, Weirdmalesk paralyses (status 4)
  // and E-beast kills (HP 0, +0x128 = 1) after 300 ticks; those two then spit (attack 2)
  // when idle or approaching.
  int pre_think() override {
    BattleGlobals& g = bg();
    if ((g.flags & 0xBF) != 0) return 0;
    if (g.flags == 0) counters[3] += g.frame_ticks;
    bool spit = false;
    for (int i = 0; i < 4; ++i) {
      PartyUnit* v = swallowed[i];
      if (v == nullptr) continue;
      const s16 type = static_cast<s16>(st()->id);
      if (type == 0x9F) {
        if (299 < counters[3]) {
          spit = true;
          v->st()->status |= 4;
        }
      } else if (type < 0xA0) {
        if (type == 0x9E) unit_damage_hp(v, g.frame_ticks);
      } else if (type == 0xA0 && 299 < counters[3]) {
        spit = true;
        v->st()->hp = 0;
        v->death = 1;
      }
    }
    if (spit && action < 2) {
      action_kind = 2;
      begin_action();
    }
    return 0;
  }
};

}  // namespace

std::unique_ptr<EnemyUnit> enemy_mod_visseyer_create() { return std::make_unique<EnemyVisseyer>(); }

}  // namespace ss::game::battle
