// The birds' enemy-code module: block 9e1595d12d40 (2394 bytes, slot A linked at 0x80088110),
// vtable 0x80088934, monster types 0x26 Carlaeagle, 0x27 Cockatrice, 0x28 Periton,
// 0x90 Gloomwing, 0x91 Cokatricking (0x26c-byte objects), encounters 35 37 79-82 ... (23).
// Two attacks, both from the air: rise, then a fan of 5 (7 for the last two) feathers and a
// glide down; or rise, then a dive at where the target will be, bouncing off what it hits.

#include "game/battle/combat.h"
#include "game/battle/enemy_classes.h"
#include "game/battle/enemy_modules.h"
#include "game/battle/shot.h"
#include "game/battle/bsfx.h"
#include "game/battle/unit_move.h"

namespace ss::game::battle {
namespace {

// 0x80088a5c: the feathers' yaw offsets (s16: 0, +-0xe3, +-0x1c7, +-0x2aa; 20 degree steps).
constexpr u32 kFeatherAngles = 0x80088A5C;

struct EnemyCarlaeagle : EnemyUnit {
  // 0x80088110: enemy_create, the module's vtable and the hit sounds.
  EnemyCarlaeagle() {
    sfx_hit = 0x4C;
    sfx_crit = 0x4D;
  }

  // 0x80088158 [26]: far off the feathers (attack 1), else now and then the dive (2).
  void choose_target() override {
    if (enemy_try_approach(this, 800, 0x2000, 1, 0x200) == 0) enemy_try_approach(this, 200, 0x4000, 2, 600);
  }

  // The rise both attacks start with (+0x174 step 0): straight up at 'speed' for 17 ticks
  // (+0x178 counts); true when it is over (+0x174 moved to the next step).
  bool rise(s32 speed) {
    airborne = 1;
    jumping = 1;
    anim = 6;
    animate(static_cast<u32>(attack_recovery));
    if (bg().flags != 0) return false;
    vel[0] = 0;
    vel[1] = speed;
    vel[2] = 0;
    loop_limit = 0;
    bobj_move(this, vel);
    counters[1] += bg().frame_ticks;
    if (counters[1] < 0x11) return false;
    counters[1] = 0;
    ++counters[0];
    return true;
  }

  // 0x800881b4 [27]: rise; on frame 1 of anim 4 a fan of homing feathers (anim 9) from in
  // front of it; then a glide down (0xa000 a tick) to the ground and back to idle.
  void attack1() override {
    switch (counters[0]) {
      case 0:
        if (rise(-0x8000)) bobj_anim_reset(this);
        return;
      case 1: {
        anim = 4;
        loop_limit = 1;
        if (counters[1] == 0 && frame == 1) {
          counters[1] = 1;
          bsfx_play_at(0x1F, this);
          const int count = static_cast<u32>(st()->id - 0x90) < 2 ? 7 : 5;
          for (int i = 0; i < count; ++i) {
            ShotDesc* d = shot_desc_init(9);
            d->speed = 0x23;
            d->target = target;
            d->life = 0x30;
            d->flags = 0x100006;
            d->angle = enemy_module_s16(kSlotA, kFeatherAngles + 2 * static_cast<u32>(i));
            // ORIGINAL BUG (original_bugs.md E9): the height is the descriptor's point y (0
            // after shot_desc_init), not the bird's, so every feather starts 0x78 above y 0:
            //   from[1] = d->point[1] - 0x78000;
            // The feathers keep the console's path, so each hits when and where it did there
            // (and aims as it did); only the drawing starts above the bird (draw_from_bird).
            const s32 from[3] = {pos[0] + (facing[0] > 0 ? 0x30000 : -0x30000), d->point[1] - 0x78000, pos[2]};
            if (Shot* s = shot_spawn_projectile(this, from, d, this)) draw_from_bird(s, d);
          }
        }
        if (static_cast<s16>(animate(static_cast<u32>(attack_recovery))) != -1) return;
        ++counters[0];
        bobj_anim_reset(this);
        return;
      }
      case 2:
        anim = 6;
        vel[1] = 0xA000;
        loop_limit = 0;
        animate(static_cast<u32>(attack_recovery));
        if (bg().flags != 0) return;
        bobj_move(this, vel);
        if (pos[1] <= bobj_ground_height(this)) return;
        // (+0x121 stays set; the dive clears it.)
        action_kind = 0;
        airborne = 0;
        begin_action();
        return;
      default:
        return;
    }
  }

  // (Port, original_bugs.md E9) a feather is drawn starting 0x78 above the bird, as the
  // module means (from[1] = pos[1] - 0x78000), and slides onto its real path, which it
  // joins at the point nearest the aim (the target's centre at the throw, as in
  // shot_spawn_projectile), within its life. Its hits stay the console's.
  void draw_from_bird(Shot* s, const ShotDesc* d) {
    s->draw_offset[1] = pos[1] - d->point[1];
    s32 aim[3] = {d->point[0], d->point[1], d->point[2]};
    if (const Unit* t = d->target; t != nullptr) {
      aim[0] = t->pos[0] + (t->size[0] << 13);
      aim[1] = t->pos[1] - (t->size[1] << 13);
      aim[2] = t->pos[2];
    }
    s64 along = 0, speed2 = 0;
    for (int i = 0; i < 3; ++i) {
      along += static_cast<s64>(aim[i] - s->pos[i]) * s->vel[i];
      speed2 += static_cast<s64>(s->vel[i]) * s->vel[i];
    }
    s64 ticks = speed2 > 0 ? along / speed2 : 1;
    if (ticks < 1) ticks = 1;
    if (ticks > d->life) ticks = d->life;
    s->draw_ticks = static_cast<s32>(ticks);
  }

  // 0x80088490 [28]: rise (faster); aim at the target's centre 24 ticks ahead (no higher
  // than y -0x12c) and dive at 32x the unit direction until the ground, a wall, the end
  // of anim 5 or a hit; then bounce back up and fall (gravity 0x1000) to the ground.
  void attack2() override {
    BattleGlobals& g = bg();
    switch (counters[0]) {
      case 0:
        if (!rise(-0x10000)) return;
        aim_dive();
        bobj_anim_reset(this);
        return;
      case 1: {
        bool stop = false;
        if (g.flags == 0) {
          if (unit_try_move_scaled(this, vel, 7) == kWallBlock)
            stop = true;
          else
            bobj_move(this, vel);
          anim = 5;
          loop_limit = 1;
          const s32 ground = bobj_ground_height(this);
          if (ground < pos[1]) {
            pos[1] = ground;
            stop = true;
          }
        }
        if (static_cast<s16>(animate(static_cast<u32>(attack_recovery))) == -1) stop = true;
        if (g.flags == 0) {
          Unit* units[8] = {};
          Vec4 points[8];
          const int n = collider_attack_overlaps(this, units, points, 1);
          if (n != 0 && hit_landed == 0) {
            unit_attack_targets(this, n, units, points, 0x100, 9999, 0);
            stop = true;
          }
        }
        if (!stop) return;
        ++counters[0];
        vel[0] = facing[0] > 0 ? -0x8000 : 0x8000;
        vel[1] = -0x14000;
        bobj_anim_reset(this);
        anim = 6;
        animate(static_cast<u32>(attack_recovery));
        return;
      }
      case 2: {
        Unit* b = unit_try_move_scaled(this, vel, 7);
        if (b == kWallBlock || (b != nullptr && 0 < vel[1])) {
          // (+0x6b stays set here.)
          action_kind = 0;
        } else {
          loop_limit = 0;
          vel[1] += g.frame_ticks * 0x1000;
          animate(static_cast<u32>(attack_recovery));
          if (g.flags != 0) return;
          bobj_move(this, vel);
          if (pos[1] <= bobj_ground_height(this)) return;
          action_kind = 0;
          airborne = 0;
        }
        jumping = 0;
        begin_action();
        return;
      }
      default:
        return;
    }
  }

  // The dive's velocity (end of step 0): toward the target's centre plus 24 ticks of its
  // velocity, y clamped to >= -0x12c000, the unit direction << 5; straight down 0x20000
  // without a target. (The original copies the direction's 4th word to +0x34 too.)
  void aim_dive() {
    if (target == nullptr) {
      vel[1] = 0x20000;
      return;
    }
    s32 aim[3];
    aim[0] = target->pos[0] + target->vel[0] * 0x18;
    aim[1] = target->pos[1] + target->size[1] * -0x2000 + target->vel[1] * 0x18;
    if (aim[1] < -0x12C000) aim[1] = -0x12C000;
    aim[2] = target->pos[2] + target->vel[2] * 0x18;
    const Vec4 d = vec_direction(pos, aim);
    vel[0] = d.x << 5;
    vel[1] = d.y << 5;
    vel[2] = d.z << 5;
  }
};

}  // namespace

std::unique_ptr<EnemyUnit> enemy_mod_carlaeagle_create() { return std::make_unique<EnemyCarlaeagle>(); }

}  // namespace ss::game::battle
