// The Guardbox module: block 96e7480aef50 (1048 bytes, slot B linked at 0x80089f18), vtable
// 0x8008a208 (the factory stores it over the EnemyUnit one: no constructor), monster types
// 0x57 Guardbox, 0x58 Magicbox, 0x59 Workbox (0x26c-byte objects; encounter 524, with the
// Magichand). A chest that always faces left, only shakes when hit, and tosses items (Shot
// kind 0x27, resident) to an idle hand (types 0x54-0x56), which then acts on them.

#include "game/battle/bsfx.h"
#include "game/battle/enemy_classes.h"
#include "game/battle/enemy_modules.h"
#include "game/battle/enemy_support.h"
#include "game/battle/shot.h"
#include "game/boot/rng.h"

namespace ss::game::battle {
namespace {

const s32 kZero[3] = {0, 0, 0};

struct EnemyGuardbox : EnemyUnit {
  // +0x180 holds the hand the item goes to (a Unit pointer); the port keeps it here and
  // +0x180 nonzero while it is set (enemy_begin_action's spell path clears +0x180).
  Unit* hand_ = nullptr;
  Unit* hand() const { return counters[3] != 0 ? hand_ : nullptr; }

  // 0x80089f18 [26]: among the standing idle (action 0) hands (types 0x54-0x56) one at
  // random becomes the item's receiver: attack 1 at once. No hand: nothing.
  void choose_target() override {
    std::array<EnemyUnit*, 8> hands{};
    u32 n = 0;
    for (EnemyUnit* u : bg().enemy_units) {
      if (unit_is_down(u)) continue;
      if (static_cast<u16>(u->st()->id - 0x54) < 3 && u->action == 0) hands[n++] = u;
    }
    if (n == 0) return;
    EnemyUnit* h = hands[rng_range(n)];
    action_kind = 1;
    hand_ = h;
    counters[3] = 1;
    begin_action();
  }

  // 0x8008a01c [32]: it always faces left.
  int pre_think() override {
    facing[0] = static_cast<s32>(0xFFF9C000);
    return 0;
  }

  // 0x8008a030 [24]: a hit only shakes it (up to 16, 20 ticks of super armour) and it goes
  // back to idle.
  void state_hit() override {
    const s32 kx = knockback[0] < 0 ? -knockback[0] : knockback[0];
    shake_amp = static_cast<s16>((kx >> 12) << 3);
    if (0x10 < static_cast<s16>((kx >> 12) << 3)) shake_amp = 0x10;
    shake_frames = 0x14;
    super_armour = 0x14;
    action_kind = 0;
    begin_action();
  }

  // 0x8008a0ac [27]: once (unfrozen), sfx 0x20 and an item (Shot kind 0x27, anim 5-8,
  // growing from scale 0) tossed from its position at the hand in an arc: speed 0xc toward
  // it, rising at -(((distance + 0x20000) / 2) >> 4) / 2; then enemy_attack_step (anim 4).
  void attack1() override {
    if (counters[1] == 0 && bg().flags == 0) {
      counters[1] = 1;
      if (Shot* s = shot_spawn(0x27)) {
        Unit* h = hand();
        // (Without a receiver the console reads its "position" from low RAM; the port uses
        // the origin.)
        const s32* to = h != nullptr ? h->pos : kZero;
        bobj_set_pos(s, pos);
        s->target = h;
        s->p128 = static_cast<s32>(rng_range(4)) + 5;
        s->scale_y = 0;
        s->scale_x = 0;
        s32 rise = (vec_distance_fx(to, pos) + 0x20000) / 2;
        if (rise < 0) rise += 0xF;
        rise = -(rise >> 4) / 2;
        const Vec4 v = vec_velocity_toward(pos, to, 0xC);
        s->vel[0] = v.x;
        s->vel[1] = v.y;
        s->vel[2] = v.z;
        s->extra34 = v.w;
        s->vel[1] = rise;
        bsfx_play_at(0x20, this);
        bobj_share_sprite(this, s);
      }
    }
    anim = 4;
    enemy_attack_step(this);
  }
};

}  // namespace

std::unique_ptr<EnemyUnit> enemy_mod_guardbox_create() { return std::make_unique<EnemyGuardbox>(); }

}  // namespace ss::game::battle
