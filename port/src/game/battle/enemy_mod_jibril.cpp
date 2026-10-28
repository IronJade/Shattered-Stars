// The #2129 image's default enemy-code module in slot C (0x8008b1f0-0x8008c1f4, 4100
// bytes, vtable 0x8008c0c4): Jibril 0xca (0x294-byte objects; encounter 536 loads no slot
// C code and uses it). It never walks: an approach is a leap that lands it beside its
// target. Its bite can grab the member it hurts and swallow him (he leaves the field until
// it spits him out: when staggered, when told to, or when it dies); attack 3 is a ground
// wave.

#include "game/battle/bsfx.h"
#include "game/battle/camera.h"
#include "game/battle/combat.h"
#include "game/battle/enemy_classes.h"
#include "game/battle/enemy_support.h"
#include "game/battle/shot.h"
#include "game/battle/sprite.h"
#include "game/boot/rng.h"

namespace ss::game::battle {
namespace {

u32 rng15() { return static_cast<u32>(rng_next() & 0x7FFF); }  // rng_next15 0x800103b4

// The unit's centre (its body's mid height).
Vec4 centre_of(const Unit* u) { return {u->pos[0], u->pos[1] + u->size[1] * -0x2000, u->pos[2], 0}; }

void shot_quake(Shot* s);

struct EnemyJibril : EnemyUnit {
  Unit* swallowed[4] = {};  // +0x26c: members inside it (removed from the field)
  Unit* grabbed[4] = {};    // +0x27c: members being pulled in by the bite
  u8 grab_step[4] = {};     // +0x28c: 0 held, 1 dragged, 2 swallowed next
  u8 grab_timer[4] = {};    // +0x290: ticks in the step

  // 0x8008b1f0: enemy_create, the module's vtable, no one swallowed.

  // 0x8008b240 [32]: frees the streamed-cel cache entries every frame, then thinks as usual.
  int pre_think() override {
    bobj_texcache_reset(this);
    return 0;
  }

  // 0x8008b260 [23]: the approach is a leap (+0x13e ticks): its height follows
  // -|ticks| << 18 (anim 1, airborne); after 8 ticks up it lands by the target (the approach
  // point; none: the attack is dropped) 8 ticks above the ground, comes down, and the attack
  // starts when the count passes 0.
  void state_approach() override {
    BattleGlobals& g = bg();
    s32 t = detour;
    anim = 1;
    loop_limit = 0;
    jumping = 1;
    airborne = 1;
    if (0 < t) t = -t;
    pos[1] = t << 18;
    animate(0);
    const s16 before = detour;
    detour = static_cast<s16>(before + g.frame_ticks);
    if (before < 0 && 0 <= detour) {
      jumping = 0;
      airborne = 0;
      pos[1] = bobj_ground_height(this);
      begin_action();
    } else if (7 < detour) {
      bsfx_play_at(0x26, this);
      bobj_anim_reset(this);
      detour = -8;
      Vec4 ap;
      if (unit_find_approach_point(this, &ap)) {
        pos[0] = ap.x;
        pos[2] = ap.z;
        pos[1] = bobj_ground_height(this);
      } else {
        action_kind = 0;
      }
    }
  }

  // 0x8008b390 [26]: attack 3 (the wave) from up to 700 away; else the usual choice.
  void choose_target() override {
    if (enemy_try_approach(this, 700, 0x1000, 3, 0x140) == 0) EnemyUnit::choose_target();
  }

  // 0x8008b3dc [27]: the bite. A hit that does damage grabs the first member hit half the
  // time (frozen 8 ticks); each grab: 7 ticks held (a flash on him), 8 ticks later dragged
  // 0x58 toward the mouth, 9 ticks later swallowed (his slot 4, removed from the field).
  void attack1() override {
    BattleGlobals& g = bg();
    anim = 4;
    if (counters[0] == 0) {
      for (int i = 0; i < 4; ++i) {
        grabbed[i] = nullptr;
        grab_step[i] = 0;
      }
      counters[0] = 1;
    }
    if (static_cast<s16>(animate(static_cast<u32>(attack_recovery))) == -1) {
      action_kind = 0;
      begin_action();
      return;
    }
    if (g.flags != 0) return;
    Unit* units[8] = {};
    Vec4 points[8];
    const int n = collider_attack_overlaps(this, units, points, 1);
    if (n != 0 && hit_landed == 0) {
      bool grab = false;
      if (unit_attack_targets(this, n, units, points, 0x100, 9999, 0) != 0) grab = rng15() < 0x4000;
      if (grab && !unit_is_down(units[0])) {
        for (int i = 0; i < 4; ++i) {
          if (grabbed[i] != nullptr) continue;
          grabbed[i] = units[0];
          grab_timer[i] = 0;
          units[0]->super_armour = 8;
          break;
        }
        counters[3] = 0;
      }
    }
    for (int i = 0; i < 4; ++i) {
      Unit* u = grabbed[i];
      if (u == nullptr) continue;
      if (grab_step[i] == 1) {
        grab_timer[i] = static_cast<u8>(grab_timer[i] + g.frame_ticks);
        if (grab_timer[i] < 8) continue;
        u->super_armour = 0x10;
        u->pos[0] += bobj_facing_fx(this, -0x58);
        grab_step[i] = 2;
        grab_timer[i] = 0;
      } else if (grab_step[i] == 0) {
        grab_timer[i] = static_cast<u8>(grab_timer[i] + g.frame_ticks);
        if (grab_timer[i] < 7) continue;
        if (Sprite* sp = unit_spawn_sprite_ex(this, 0xD, 1)) {
          sprite_set_pos(sp, u->pos[0] + bobj_facing_fx(this, -0x80), u->pos[1], u->pos[2]);
          sp->layer = 4;
          sp->facing[0] = facing[0];
        }
        u->super_armour = 0x10;
        grab_step[i] = 1;
        grab_timer[i] = 0;
      } else if (grab_step[i] == 2) {
        grab_timer[i] = static_cast<u8>(grab_timer[i] + g.frame_ticks);
        if (grab_timer[i] < 9) continue;
        for (int k = 0; k < 4; ++k) {
          if (swallowed[k] != nullptr) continue;
          swallowed[k] = u;
          u->interrupt();
          swallowed[k]->removed = 1;
          break;
        }
        // (With all four places taken the member is simply let go of, still removed.)
        grabbed[i] = nullptr;
        grab_step[i] = 0;
      }
    }
  }

  // 0x8008b7b8 [28]: spits (anim 7): from frame 1 everybody inside comes out.
  void attack2() override {
    anim = 7;
    if (static_cast<s16>(animate(static_cast<u32>(attack_recovery))) == -1) {
      action_kind = 0;
      begin_action();
      return;
    }
    if (frame < 1) return;
    release_swallowed();
  }

  // 0x8008b854 [29]: the wave (anim 5, one loop): at once three sounds and a ground wave
  // Shot at its feet.
  void attack3() override {
    anim = 5;
    loop_limit = 1;
    if (counters[0] == 0) {
      counters[0] = 1;
      bsfx_play_at(0x2A, this);
      bsfx_play_at(0x21, this);
      bsfx_play_at(0x22, this);
      if (Shot* s = shot_spawn_callback(shot_quake)) {
        shot_place_at_unit(s, this, 0, 0xC0, 0);
        s->owner = this;
        bobj_share_sprite(this, s);
      }
    }
    if (static_cast<s16>(animate(static_cast<u32>(attack_recovery))) == -1) {
      action_kind = 0;
      begin_action();
    }
  }

  // 0x8008b948 [24]: staggered (state 3, not suppressed, +0x174 clear) with somebody
  // inside, it spits instead (attack 2): always for types below 0x9e, else 1 in 5 (Jibril
  // 0xca is the only type using this module, so always the 1 in 5).
  void state_hit() override {
    if (bg().flags == 0 && action == 3 && counters[0] == 0 && anyone_inside() &&
        (static_cast<s16>(st()->id) < 0x9E || rng_range(100) < 0x14)) {
      action_kind = 2;
      begin_action();
      return;
    }
    EnemyUnit::state_hit();
  }
  bool anyone_inside() const {
    for (const Unit* u : swallowed)
      if (u != nullptr) return true;
    return false;
  }

  // 0x8008ba24 [4]: interrupted with its death pending, it lets everybody out.
  void interrupt() override {
    if (death != 0 && anyone_inside()) release_swallowed();
  }

  // 0x8008ba98 [25]: once: 48 beams out of the body, a spark ring at its centre and one on
  // the ground, two sounds; then the usual fade.
  void state_dying() override {
    if (counters[0] == 0) {
      for (int i = 0; i < 0x30; ++i) {
        const Vec4 c = centre_of(this);
        const Vec4 d = vec_random_around(c.data(), 0x500);
        const Vec4 from = centre_of(this);
        effect_spawn_beam_at(from.data(), d.data(), 1, 0x140, 0x18, 0x4000, 0x400, 0);
      }
      const Vec4 c = centre_of(this);
      effect_spawn_sparks_at(c.data(), 0x14, 0x60, 0, 0x60, 0x3C, 3, 3, 0x40, 1);
      const s32 ground[3] = {pos[0], bobj_ground_height(this), pos[2]};
      effect_spawn_sparks_at(ground, 0x14, 0xC0, 0x40, 0x40, 0x5A, 9, 0, 0x40, 1);
      bsfx_play_at(0x21, this);
      bsfx_play_at(0x22, this);
      counters[0] = 1;
    }
    EnemyUnit::state_dying();
  }

  // 0x8008bc50 (slot 37, an extra virtual only this module has): every member inside comes
  // out 0x90 in front of it at its height, at the first free z of a search that alternates
  // sides in 0x40 steps (one counter for all of them, reset after 99 tries); facing back
  // along his velocity, visible again; one left at 0 HP (not KO) dies.
  void release_swallowed() {
    s32 n = 0;
    if (swallowed[0] != nullptr) bsfx_play_at(0x24, this);
    for (int i = 0; i < 4; ++i) {
      Unit* u = swallowed[i];
      if (u == nullptr) continue;
      u->removed = 0;
      u->pos[0] = bobj_front_x(this, 0x90);
      u->pos[1] = pos[1];
      for (int k = 0; k < 100; ++k) {
        if (n == 0) {
          u->pos[2] = pos[2];
        } else if ((n & 1) == 0) {
          // ORIGINAL BUG (original_bugs.md E3): the even steps store the z into +0x30, his
          // z velocity, so the spot tried keeps the previous z:
          //   u->vel[2] = pos[2] - n * 0x40000;
          u->pos[2] = pos[2] - n * 0x40000;
        } else {
          u->pos[2] = pos[2] + (n + 1) * 0x40000;
        }
        if (unit_check_move(u, kZeroVector, 7) == nullptr) break;
        ++n;
        if (k == 0x62) n = 0;
      }
      // (Facing x from his own -vel x: they come out facing any way; intended,
      // original_bugs.md E14.)
      u->facing[0] = -u->vel[0];
      u->facing[1] = 0;
      u->facing[2] = 0;
      // (Only party members are ever swallowed: the bite hits the party.)
      static_cast<PartyUnit*>(u)->appear &= 0xFB;
      const Stats* s = u->st();
      if (s->hp == 0 && (s->status & 1) == 0) u->death = 1;
      swallowed[i] = nullptr;
      ++n;
    }
  }

  // 0x8008c09c [36]: the dtor (EnemyUnit vtable, unit_destroy).
};

// 0x8008be1c: attack 3's ground wave (a kind-0x33 Shot): a spark ring (anim 8) once; a
// band from radius +0x12c (0xf0, growing 8 a tick) 0x40 a tick wide hits every standing
// member whose distance falls in it and who is not above -0x64 (power 0x100, knocked along
// its facing); 100 ticks (+0x130).
void shot_quake(Shot* s) {
  BattleGlobals& g = bg();
  if (s->p128 == 0) {
    s->p128 = 1;
    if (Shot* ring = effect_spawn_sparks_at(s->pos, 0x12, 0x120, 0, 0x10, 0x78, 0, 2, 0x30, 1)) {
      ring->anim = 8;
      bobj_share_sprite(s, ring);
    }
    s->p12c = 0xF0;
  }
  if (g.flags != 0) return;
  s->p130 += g.frame_ticks;
  if (0x64 < s->p130) {
    shot_kill(s);
    return;
  }
  Unit* units[8] = {};
  Vec4 points[8] = {};
  int n = 0;
  for (PartyUnit* u : g.party_units) {
    if (unit_is_down(u)) continue;
    const s32 d = vec_distance_fx(s->pos, u->pos) >> 12;
    if (d < s->p12c || s->p12c + g.frame_ticks * 0x40 <= d || u->pos[1] <= -0x64000) continue;
    units[n] = u;
    points[n] = centre_of(u);
    ++n;
  }
  if (n != 0) unit_attack_targets(s->owner, n, units, points, 0x100, s->facing[0] < 0 ? 0x800 : 0, 0);
  s->p12c += g.frame_ticks * 8;
}

}  // namespace

std::unique_ptr<EnemyUnit> enemy_mod_jibril_create() { return std::make_unique<EnemyJibril>(); }

}  // namespace ss::game::battle
