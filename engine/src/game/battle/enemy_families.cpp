// The 16 resident monster families (0x8003e248-0x80041600): EnemyUnit subclasses whose
// code and vtables live in the #2129 image. The factory 0x80075ad8 stores a family vtable
// over the EnemyUnit one (or calls a family constructor, which also changes the hit sounds).
// analysis/battle_catalog/parts/p2.md "Family behaviour summary".

#include <cstring>

#include "game/battle/combat.h"
#include "game/battle/enemy_classes.h"
#include "game/battle/enemy_support.h"
#include "game/battle/killer.h"
#include "game/battle/render.h"
#include "game/battle/shot.h"
#include "game/battle/sprite.h"
#include "game/battle/bsfx.h"
#include "game/battle/unit_move.h"
#include "game/boot/rng.h"
#include "game/title/new_game.h"

namespace ss::game::battle {
namespace {

u32 rng15() { return static_cast<u32>(rng_next() & 0x7FFF); }  // rng_next15 0x800103b4

s16 type_of(const EnemyUnit* e) { return static_cast<s16>(e->st()->id); }

// The attack slots' common tail: when the animation reached its loop limit, back to idle
// (action kind 'next', usually 0) through slot 12.
bool anim_done(EnemyUnit* e, u32 flags, u8 next = 0) {
  if (static_cast<s16>(e->animate(flags)) != -1) return false;
  e->action_kind = next;
  e->begin_action();
  return true;
}

// The melee check the multi-hit attacks repeat by hand (enemy_attack_step without the
// animation): the frame's attack boxes against the party, damage once per landed flag.
int melee_check(EnemyUnit* e) {
  Unit* units[8] = {};
  Vec4 points[8];
  const int n = collider_attack_overlaps(e, units, points, 1);
  if (n == 0 || e->hit_landed != 0) return 0;
  return unit_attack_targets(e, n, units, points, 0x100, 9999, 0);
}

// ---- Knight (vtable 0x8006c5c0): Armedknight 1, Gladiator 2, Sargwen 3, Darkcrusader 0x79,
// Insaneload 0x7a, Weirdknight 0x7b, Lastavenger 0x7c.
struct EnemyKnight : EnemyUnit {
  // 0x8003e8d0 [26]
  void choose_target() override {
    counters[2] = 0;
    if (enemy_try_approach(this, 1000, 0x2000, 2, 0x200) == 0) EnemyUnit::choose_target();
  }
  // 0x8003e920 [27]: the late knights chain their swings and leave afterimages.
  void attack1() override {
    int rank = 0;
    switch (type_of(this)) {
      case 0x7A: rank = 1; break;
      case 0x7B: rank = 2; break;
      case 0x7C: rank = 3; break;
      default: break;
    }
    if (rank != 0 && effects[0] == nullptr)
      effects[0] = afterimage_create(this, 3, static_cast<u32>(battle_s32(0x8006D4DC + 4 * rank)), 1);
    EnemyUnit::attack1();
    if (hit_landed != 0 && rank != 0) {
      // A landed swing chains: the first into another attack 1 (Insaneload: the wave), the
      // second always into the wave.
      action_kind = (counters[2] == 0 && rank != 1) ? 1 : 2;
      begin_action();
      counters[2] = 1;
    }
  }
  // 0x8003ea4c [28]: sword waves (Shot kind 2) every 8 ticks from frame 10, one (two for
  // Darkcrusader / Weirdknight / Lastavenger); then the guard stance.
  void attack2() override {
    BattleGlobals& g = bg();
    anim = 5;
    animate(0);
    if (g.flags != 0) return;
    const s16 t = type_of(this);
    const int extra = (t == 0x79 || static_cast<u32>(t - 0x7B) < 2) ? 1 : 0;
    if (frame < 10) {
      counters[3] = 0;
      return;
    }
    counters[3] -= g.frame_ticks;
    if (counters[3] > 0 || extra < counters[0] || target == nullptr) return;
    counters[3] += 8;
    Shot* s = shot_spawn(2);
    if (s == nullptr) return;
    ++counters[0];
    s->owner = this;
    shot_place_at_unit(s, this, 0, size[1] * 2, 0);
    const Vec4 dir = vec_velocity_toward(pos, target->pos, 1);
    std::memcpy(s->point, &dir, sizeof(s->point));
    const Vec4 v = vec_velocity_toward(pos, target->pos, 0x18);
    std::memcpy(s->vel, &v, sizeof(s->vel));
    s->layer = 1;
    s->p128 = vec_distance_fx(pos, target->pos) >> 12;
    bsfx_play_at(0x1F, this);
    bobj_share_sprite(this, s);
    if (extra < counters[0]) {
      ++counters[1];
      st()->status |= 0x80;
      action_kind = 0;
      begin_action();
    }
  }
  // 0x8003ec40 [32]: after the waves the knight holds its stance (status 0x80) until hit.
  int pre_think() override {
    if (counters[1] == 0) {
      st()->status &= 0x7F;
      return 0;
    }
    if (static_cast<u8>(action - 3) < 2) {
      counter_window = 6;
      anim = 6;
      state_hit();
      return 1;
    }
    const bool stunned = (st()->status & 0x40) != 0;
    if (stunned) {
      anim = 6;
    } else {
      anim = 5;
      frame = 10;
    }
    animate(stunned ? 0 : 1);
    return 1;
  }
};

// ---- Guard (vtable 0x8006c498): Guarder 7, Shielder 8, Defender 9, Livingarmor 0x7d,
// Reflectguard 0x7e, Starguarder 0x7f.
struct EnemyGuard : EnemyUnit {
  // 0x8003edbc [5]: the Starguarder throws its weapons when knocked back.
  int start_reaction() override {
    if (reaction == 2 && type_of(this) == 0x7F) unit_throw_weapons(this, 0);
    return unit_start_reaction(this);
  }
  // 0x8003ed28 [26]: Livingarmor / Starguarder close in and take the stance at once.
  void choose_target() override {
    const s16 t = type_of(this);
    if ((t == 0x7D || t == 0x7F) && enemy_try_approach(this, 300, 0x2000, 2, 0x100) != 0) {
      action_kind = 2;
      begin_action();
      return;
    }
    EnemyUnit::choose_target();
  }
  // 0x8003ee18 [28]: a flashing defensive stance for 300 ticks, immune meanwhile.
  void attack2() override {
    BattleGlobals& g = bg();
    anim = 3;
    loop_limit = 0;
    colour_mode = 1;
    const bool lit = counters[1] != 0;
    rgb[0] = lit ? 0xFF : 0xC0;
    rgb[1] = lit ? 0x90 : 0x60;
    rgb[2] = lit ? 0x40 : 0x80;
    animate(0);
    special_timer = 4;
    if (g.flags != 0) return;
    counters[0] += g.frame_ticks;
    counters[1] ^= 1;
    if (300 < counters[0]) {
      colour_mode = 0;
      action_kind = 0;
      begin_action();
    }
  }
};

// ---- Plant (vtable 0x8006c370): Alraune 0xd, Shadowflower 0xe, Mandrake 0xf.
struct EnemyPlant : EnemyUnit {
  // 0x8003ef04 [26]
  void choose_target() override {
    if (enemy_try_approach(this, 700, 12000, 2, 0x140) == 0) EnemyUnit::choose_target();
  }
  // 0x8003ef50 [28]: a ring of 8 spores from above the plant, once past frame 5.
  void attack2() override {
    BattleGlobals& g = bg();
    anim = 5;
    if (anim_done(this, static_cast<u32>(attack_recovery))) return;
    if (g.flags != 0 || counters[1] != 0 || frame <= 5) return;
    for (int i = 0; i < 8; ++i) {
      ShotDesc* d = shot_desc_init(6);
      const s32 from[3] = {pos[0], pos[1] - 0x30000, pos[2]};
      d->speed = 0xC;
      d->point[0] = pos[0] + bcos(static_cast<u32>(i << 9)) * 0x60;
      d->point[1] = from[1];
      d->life = 0x26;
      d->flags = 0x100006;
      d->point[2] = pos[2] + bsin(static_cast<u32>(i << 9)) * 0x60;
      shot_spawn_projectile(this, from, d, this);
    }
    counters[1] = 1;
  }
};

// ---- Thief (vtable 0x8006c248): FunnyThief 0x16, Bandit 0x17, Lessassassin 0x18,
// MetalFunny 0x84, F-thiefL99 0x85.
struct EnemyThief : EnemyUnit {
  // 0x8003f0ec [26]: the MetalFunny runs off now and then (no EXP, no Fol).
  void choose_target() override {
    if (type_of(this) == 0x84 && rng15() < 0x2400) {
      battle_message_show(0x2714);
      facing[0] = -facing[0];
      bsfx_play_at(0x1D, this);
      st()->exp = 0;
      fol = 0;
      death = 5;
      return;
    }
    EnemyUnit::choose_target();
  }
  // 0x8003f180 [27]: F-thiefL99 leaves an afterimage and hits a second time per swing.
  // (The animation advances twice a frame: enemy_attack_step's and its own.)
  void attack1() override {
    BattleGlobals& g = bg();
    loop_limit = 1;
    anim = 4;
    enemy_attack_step(this);
    const bool l99 = type_of(this) == 0x85;
    if (effects[0] == nullptr && l99) effects[0] = afterimage_create(this, 4, 0x802010, 1);
    if (effects[0] != nullptr) effects[0]->next_kind = 1;
    anim_done(this, static_cast<u32>(attack_recovery));
    if (g.flags != 0) return;
    if (l99 && counters[1] == 0 && hit_landed != 0) {
      counters[1] = 1;
      hit_landed = 0;
    }
    Unit* units[8] = {};
    Vec4 points[8];
    const int n = collider_attack_overlaps(this, units, points, 1);
    if (n == 0 || hit_landed != 0 || !l99) return;
    if (unit_attack_targets(this, n, units, points, 0x100, 9999, 0) != 0) {
      const s32 spread[3] = {0x20000, 0x20000, 0x20000};
      effect_debris_burst(points[0].data(), spread, 3, 1, 1, 0x2000, 0xD);
    }
  }
};

// ---- Axe (vtable 0x8006c120): Doomaxe 0x1c, Lizardaxe 0x1d, Robberaxe 0x1e, Dragonaxe 0x88,
// Weirdaxe 0x89.
struct EnemyAxe : EnemyUnit {
  // 0x8003f37c [5]: the Weirdaxe sprays 8 pieces (kind 0x2c) when knocked back.
  int start_reaction() override {
    if (reaction == 2 && type_of(this) == 0x89) {
      for (int i = 0; i < 8; ++i) {
        Shot* s = shot_spawn(0x2C);
        if (s == nullptr) continue;
        shot_place_at_unit(s, this, 0x20, 0x60, 0);
        s->vel[0] = bobj_facing_fx(this, rng_between(8, 0x10));
        s->vel[1] = rng_between_fixed(-12, 3);
        s->vel[2] = rng_spread_fixed(0xC);
        s->owner = this;
        bobj_share_sprite(this, s);
      }
    }
    return unit_start_reaction(this);
  }
};

// ---- Kobold (vtable 0x8006bff8): Kobold 0x13, Bugbear 0x14, Koboldking 0x15.
struct EnemyKobold : EnemyUnit {
  // 0x8003f460 [26]: a quarter of the approaches end in the flurry.
  void choose_target() override {
    EnemyUnit::choose_target();
    if (action == 1 && action_kind == 1 && rng15() < 0x2000) action_kind = 2;
  }
  // 0x8003f4c0 [28]: 4 loops, each able to hit; then the finisher (attack 3).
  void attack2() override {
    BattleGlobals& g = bg();
    const s8 before = frame;
    anim = 5;
    loop_limit = 4;
    if (anim_done(this, static_cast<u32>(attack_recovery), 3)) return;
    if (frame < before) hit_landed = 0;
    if (g.flags != 0) return;
    melee_check(this);
  }
  // 0x8003f5f8 [29]: 4 loops, no hit check of its own.
  void attack3() override {
    anim = 6;
    loop_limit = 4;
    anim_done(this, static_cast<u32>(attack_recovery));
  }
};

// ---- Magius (vtable 0x8006bed0): Eldermagius 0x19, Pilesherry 0x1a, Magius 0x1b, Warlock 0x86,
// Meigus 0x87.
struct EnemyMagius : EnemyUnit {
  // 0x8003f9d0 [9]: the Meigus chants fast (skill 0x27 at level 9).
  int skill_level(int skill) override { return (type_of(this) == 0x87 && skill == 0x27) ? 9 : 0; }
  // 0x8003f674 [26]: an attack-1 approach becomes a drift in a random direction for
  // 300-600 ticks.
  void choose_target() override {
    EnemyUnit::choose_target();
    if (action != 1 || action_kind != 1) return;
    begin_action();
    const u32 a = rng_range(0x1000);
    vel[1] = 0;
    vel[0] = ((bcos(a) * run_speed) >> 12) << 2;
    vel[2] = ((bsin(a) * run_speed) >> 12) << 2;
    counters[1] = rng_between(300, 600);
  }
  // 0x8003f748 [27]: drifts, bouncing off walls and obstacles, hurting whoever it touches
  // (once per new victim); attack 2 when the time is up and the spot is free.
  void attack1() override {
    BattleGlobals& g = bg();
    loop_limit = 0;
    anim = 4;
    counters[1] -= g.frame_ticks;
    s32 d[3] = {0, 0, 0};
    animate(0);
    if (counters[1] < 1 && unit_check_move(this, d, 7) == nullptr) {
      action_kind = 2;
      begin_action();
      return;
    }
    if (g.flags != 0) return;
    d[0] = vel[0] * g.frame_ticks;
    if (unit_check_move(this, d, 7) == kWallBlock) vel[0] = -vel[0];
    d[0] = 0;
    d[2] = vel[2] * g.frame_ticks;
    if (unit_check_move(this, d, 7) == kWallBlock) vel[2] = -vel[2];
    bobj_move(this, vel);
    Unit* units[8] = {};
    Vec4 points[8];
    const int n = collider_attack_overlaps(this, units, points, 1);
    // (+0x174 holds the last victim; slot 12 clears it when the attack starts.)
    const Unit* last = counters[0] != 0 ? last_victim_ : nullptr;
    if (n == 0) {
      counters[0] = 0;
    } else if (last != units[0]) {
      counters[0] = 1;
      last_victim_ = units[0];
      unit_attack_targets(this, 1, units, points, 0x100, 9999, 0);
    }
  }
  // 0x8003f954 [28]
  void attack2() override {
    anim = 5;
    loop_limit = 1;
    anim_done(this, static_cast<u32>(attack_recovery));
  }
  Unit* last_victim_ = nullptr;  // +0x174 (valid while counters[0] is set)
};

// ---- Sandglass (vtable 0x8006bda8): Sandglass 0x29, Timekeeper 0x2a, Rolesher 0x2b,
// Dreamshade 0x92.
struct EnemySandglass : EnemyUnit {
  // 0x8003fa00 [26]: below half HP the late ones heal (30 %); the Dreamshade stops time.
  void choose_target() override {
    const Stats* s = st();
    if (type_of(this) != 0x29 && rng15() < 10000 && s->hp < s->hp_max / 2) {
      action_kind = 2;
    } else if (type_of(this) == 0x92 && rng15() < 0x1800) {
      battle_message_show(0x2715);
      action_kind = 3;
    } else {
      EnemyUnit::choose_target();
      return;
    }
    begin_action();
  }
  // 0x8003faec [32]: the sand level (animation table) follows the HP.
  int pre_think() override {
    const s32 hp = st()->hp, max = st()->hp_max;
    if (hp == 0) animset->layout = model_layout + 0x70;
    if (hp < max / 4) animset->layout = model_layout + 0x54;
    if (max / 4 < hp) animset->layout = model_layout + 0x38;
    if (max / 2 < hp) animset->layout = model_layout + 0x1C;
    if (max * 3 / 4 < hp) animset->layout = model_layout;
    return 0;
  }
  // 0x8003fbc0 [28]: heals a quarter of its max HP once past frame 9.
  void attack2() override {
    anim = facing[0] > 0 ? 6 : 5;
    if (anim_done(this, static_cast<u32>(attack_recovery))) return;
    if (counters[0] != 0 || frame <= 9) return;
    counters[0] = 1;
    const s32 heal = -st()->hp_max / 4;
    damage_popup_spawn(this, heal, -0x2000, 0);
    unit_damage_hp(this, heal);
    bsfx_play_at(0x13, this);
  }
  // 0x8003fcc4 [29]: time stop: every standing member is frozen for 180 ticks.
  void attack3() override {
    anim = facing[0] > 0 ? 6 : 5;
    if (anim_done(this, static_cast<u32>(attack_recovery))) return;
    if (counters[0] != 0 || frame <= 9) return;
    for (PartyUnit* u : bg().party_units)
      if (!unit_is_down(u)) u->super_armour = 0xB4;
    sound_handle_pitch_down(bsfx_play_at(0x33, this), 0x1E);
  }
  // 0x8003fde4 [25]: the glass shatters once and the body stays.
  void state_dying() override {
    if (counters[0] != 0) return;
    const s32 ground[3] = {pos[0], bobj_ground_height(this), pos[2]};
    effect_spawn_sparks_at(ground, 0x10, 0x20, 0x30, 0x10, 0x3C, 5, 1, 0x10, 0);
    const s32 at[3] = {pos[0], pos[1] + size[1] * -0x2000, pos[2]};
    const s32 spread[3] = {0x10000, 0x10000, 0x10000};
    effect_debris_burst(at, spread, 4, 0, 1, 0x1000, 0);
    bsfx_play_at(0x50, this);
    counters[0] = 1;
  }
};

// ---- Ooze (vtable 0x8006bc80, ctor 0x8003fedc): Ooze 0x32, Evilwater 0x33, Slimepool 0x34.
struct EnemyOoze : EnemyUnit {
  EnemyOoze() {
    sfx_hit = 0x4C;
    sfx_crit = 0x4D;
  }
  // 0x8003ff24 [26]
  void choose_target() override {
    if (enemy_try_approach(this, 900, 0x2000, 2, 0x200) == 0) EnemyUnit::choose_target();
  }
  // 0x8003ff70 [28]: one homing spit past frame 3.
  void attack2() override {
    BattleGlobals& g = bg();
    anim = 5;
    if (anim_done(this, static_cast<u32>(attack_recovery))) return;
    if (g.flags != 0 || counters[1] != 0 || frame <= 3) return;
    bsfx_play_at(0x1F, this);
    ShotDesc* d = shot_desc_init(6);
    d->speed = 0x1A;
    d->life = 0x1E;
    d->flags = 0x100006;
    d->target = target;
    const s32 from[3] = {bobj_front_x(this, 0x18), pos[1] - 0x3C000, pos[2]};
    shot_spawn_projectile(this, from, d, this);
    counters[1] = 1;
  }
};

// ---- Beastmaster (vtable 0x8006bb58, ctor 0x800400ac): Shynesslady 0x41, Beastmaster 0x42,
// type 0x43; it heals and commands the wolves (types 0x44-0x46).
struct EnemyBeastmaster : EnemyUnit {
  EnemyBeastmaster() {
    sfx_hit = 0x4D;
    sfx_crit = 0x4E;
  }
  static bool is_wolf(const EnemyUnit* u) { return static_cast<u16>(u->st()->id - 0x44) < 3; }
  // 0x800400f4 [26]
  void choose_target() override;
  // 0x80040308 [29]: the command: every wolf attacks at once with double ATK for 1200 ticks.
  void attack3() override {
    anim = 6;
    if (anim_done(this, static_cast<u32>(attack_recovery))) return;
    if (counters[1] != 0 || frame <= 1) return;
    counters[1] = 1;
    bsfx_play_at(0x4D, this);
    battle_message_show(0x2711);
    for (EnemyUnit* u : bg().enemy_units) {
      if (unit_is_down(u) || !is_wolf(u)) continue;
      u->counters[1] = 1;
      u->counters[2] = 0x4B0;
    }
  }
};

// ---- Wolf (vtable 0x8006ba30, ctor 0x8004044c): Werewolf 0x44, Wolfhead 0x45, type 0x46.
struct EnemyWolf : EnemyUnit {
  EnemyWolf() {
    sfx_hit = 0x4C;
    sfx_crit = 0x4E;
  }
  // 0x80040494 [32]: the beastmaster's orders: double ATK while +0x17c runs; +0x178 == 1
  // attacks (3) at once, >= 3 counts down to attack 4 (a heal's reply).
  int pre_think() override {
    BattleGlobals& g = bg();
    Stats* s = st();
    s->atk[2] = s->atk[0];
    if (counters[2] != 0) {
      counters[2] -= g.frame_ticks;
      if (counters[2] < 0) counters[2] = 0;
      s->atk[2] = static_cast<s16>(s->atk[2] << 1);
    }
    if (1 < action || counters[1] == 0) return 0;
    if (counters[1] == 1) {
      action_kind = 3;
    } else if (counters[1] >= 3) {
      counters[1] -= g.frame_ticks;
      if (2 < counters[1]) return 0;
      action_kind = 4;
    }
    counters[1] = 0;
    begin_action();
    return 0;
  }
  // 0x80040588 [26]
  void choose_target() override {
    if (enemy_try_approach(this, 400, 0x2000, 2, 0x30) == 0) EnemyUnit::choose_target();
  }
  // 0x800405d4 [28]: a 4-loop bite, each loop able to hit.
  void attack2() override {
    anim = 5;
    const s8 before = frame;
    loop_limit = 4;
    enemy_attack_step(this);
    if (frame < before) hit_landed = 0;
  }
};

void EnemyBeastmaster::choose_target() {
  std::array<EnemyUnit*, 8> wolves{};
  int n = 0;
  for (EnemyUnit* u : bg().enemy_units)
    if (!unit_is_down(u) && is_wolf(u)) wolves[n++] = u;
  if (n != 0) {
    if (rng15() < 0x4000) {
      for (int i = 0; i < n; ++i) {
        const Stats* s = wolves[i]->st();
        if (s->hp_max / 2 <= s->hp) continue;
        command = 4;
        target = wolves[i];
        spell = s->hp_max > 1000 ? 0x30 : 0x2F;
        wolves[i]->counters[1] = 0x5A;
        return;
      }
    }
    if (rng15() < 0x2000) {
      int i = 0;
      while (i < n && wolves[i]->counters[2] != 0) ++i;
      if (i != n) {
        action_kind = 3;
        counters[1] = 0;
        begin_action();
        return;
      }
    }
  }
  if (enemy_try_approach(this, 200, 0x1000, 2, 0x60) == 0) EnemyUnit::choose_target();
}

// ---- Giant (vtable 0x8006cb90, ctor 0x800407e8): Ghark 0x4a, Troll 0x4b, Yety 0x4c,
// Mountsnow 0x4d, Giant 0x99, Atlus 0x9a, Killergigant 0x9b.
struct EnemyGiant : EnemyUnit {
  EnemyGiant() {
    sfx_hit = 0x4C;
    sfx_crit = 0x4E;
  }
  // 0x80040830 [26]
  void choose_target() override {
    if (enemy_try_approach(this, 300, 0x2000, 2, 0x20) == 0) EnemyUnit::choose_target();
  }
  // 0x8004087c [28]: a smash with double ATK and a shockwave sprite past frame 1.
  void attack2() override {
    anim = 5;
    Stats* s = st();
    const s16 atk = s->atk[2];
    s->atk[2] = static_cast<s16>(atk << 1);
    enemy_attack_step(this);
    s->atk[2] = atk;
    if (counters[1] != 0 || frame <= 1) return;
    counters[1] = 1;
    Sprite* fx = unit_spawn_sprite_ex(this, 7, 1);
    if (fx == nullptr) return;
    sprite_set_pos(fx, pos[0], pos[1], pos[2] - 0x10000);
    fx->layer = -1;
    std::memcpy(fx->facing, facing, sizeof(facing));
  }
};

// ---- Goat (vtable 0x8006ca60, ctor 0x80040958): Goathead 0x4e, Harfainx 0x4f, Breakwing 0x50,
// Greatergoat 0x9c, Weirdgoat 0x9d. Hovers (+0x160 = 8).
struct EnemyGoat : EnemyUnit {
  EnemyGoat() { hover_amp = 8; }
  // 0x80040998 [26]
  void choose_target() override {
    if (enemy_try_approach(this, 300, 0x2000, 2, 0x80) == 0) EnemyUnit::choose_target();
  }
  // 0x80040a38 [24]: knocked back, it raises the blue barrier.
  void state_hit() override {
    EnemyUnit::state_hit();
    if (action != 4) return;
    counters[1] = 0;
    draw_barrier();
  }
  // 0x800409e4: slot 37, enemy_state_chant with the turning barrier. Nothing calls slot 37
  // (resident code, #1984 and the modules), so it stays a plain member.
  [[maybe_unused]] void chant_with_barrier();
  // 0x80040a7c: a cone of 4 additive gouraud triangles from a ring of radius 0x100 round
  // the feet (turned by +0x178) up to 0x180 above, pulsing with the frame count.
  void draw_barrier() {
    gte_set_view_matrix();
    for (int i = 0; i < 4; ++i) {
      psyq::SVECTOR v[3]{};
      const u32 a0 = static_cast<u32>(counters[1] + i * 0x400), a1 = static_cast<u32>(counters[1] + (i + 1) * 0x400);
      v[0].vx = static_cast<s16>((pos[0] + bcos(a0) * 0x100) >> 12);
      v[0].vy = static_cast<s16>(pos[1] >> 12);
      v[0].vz = static_cast<s16>((pos[2] + bsin(a0) * 0x100) >> 12);
      v[1].vx = static_cast<s16>((pos[0] + bcos(a1) * 0x100) >> 12);
      v[1].vy = static_cast<s16>(pos[1] >> 12);
      v[1].vz = static_cast<s16>((pos[2] + bsin(a1) * 0x100) >> 12);
      v[2].vx = static_cast<s16>(pos[0] >> 12);
      v[2].vy = static_cast<s16>((pos[1] >> 12) - 0x180);
      v[2].vz = static_cast<s16>(pos[2] >> 12);
      u32 pulse = static_cast<u32>(bg().battle_frames) & 0x1F;
      if (0xF < pulse) pulse = 0x1F - pulse;
      const s32 level = i * 0x40 + static_cast<s32>(pulse) * 4;
      u32 c[3];
      c[0] = scale_colour(0x3080FF, level) | 0x32000000;
      c[1] = scale_colour(0x2070E0, level);
      c[2] = c[0];
      draw_tri_g3(v, c, 0);
    }
  }
};

void EnemyGoat::chant_with_barrier() {
  // [port partial: enemy_state_chant belongs to the spells (the battle program's stand-in
  // runs in enemy_think)]
  counters[1] = (counters[1] + bg().frame_ticks * 0x14) & 0xFFF;
  draw_barrier();
}

// ---- Archer (vtable 0x8006c938, ctor 0x80040c50): Archer 0x60, Giantbow 0x61, type 0x62,
// Robinfake 0xa1, Robinmaster 0xa2.
struct EnemyArcher : EnemyUnit {
  EnemyArcher() {
    sfx_hit = 0x4B;
    sfx_crit = 0x49;
  }
  // 0x80040c98 [26]: far off (or a quarter of the time) the volley, nearer one arrow.
  void choose_target() override {
    if (type_of(this) == 0xA1) {
      if (enemy_try_approach(this, 0x140, 0x4000, 3, 0x200) != 0) {
        action_kind = 3;
        begin_action();
      }
      return;
    }
    if (enemy_try_approach(this, 0x280, 0x2000, 3, 0x200) != 0) return;
    if (enemy_try_approach(this, 0x180, 20000, 2, 0x280) != 0) return;
    EnemyUnit::choose_target();
  }
  // 0x80040d5c [28]: one homing arrow past frame 3.
  void attack2() override {
    BattleGlobals& g = bg();
    anim = 5;
    if (anim_done(this, static_cast<u32>(attack_recovery))) return;
    if (g.flags != 0 || counters[1] != 0 || frame <= 3) return;
    bsfx_play_at(0x1F, this);
    bsfx_play_at(0x3C, this);
    ShotDesc* d = shot_desc_init(7);
    d->speed = type_of(this) == 0xA2 ? 0x40 : 0x20;
    d->life = 0x28;
    d->flags = 0x100006;
    d->hit_effect = 9;
    d->target = target;
    const s32 from[3] = {bobj_front_x(this, 0x40), pos[1] - 0x80000, pos[2]};
    shot_spawn_projectile(this, from, d, this);
    counters[1] = 1;
  }
  // 0x80040ebc [29]: a fan of type*2 - 0xbd arrows (Robins 5) in 7.5 degree steps,
  // alternating sides (the Robinfake's spread at random).
  void attack3() override {
    BattleGlobals& g = bg();
    anim = 6;
    if (anim_done(this, static_cast<u32>(attack_recovery))) return;
    if (g.flags != 0 || counters[1] != 0 || frame <= 3) return;
    bsfx_play_at(0x1F, this);
    bsfx_play_at(0x3C, this);
    const s16 t = type_of(this);
    const int count = t < 0xA1 ? t * 2 - 0xBD : 5;
    s32 left = 0, right = 0xF;
    for (int i = 0; i < count; ++i) {
      ShotDesc* d = shot_desc_init(7);
      d->trail_rgb = 0x408080;
      d->speed = type_of(this) == 0xA2 ? 0x40 : 0x20;
      d->life = 0x30;
      d->hit_effect = 9;
      d->flags = 0x100006;
      d->target = target;
      if (0 < i) {
        const s32 deg = (i & 1) == 0 ? left : right;
        d->angle = ((deg / 2) * 0x1000) / 0x168;
      }
      if (type_of(this) == 0xA1) d->angle = rng_spread(0x400);
      const s32 from[3] = {bobj_front_x(this, 0x40), pos[1] - 0x80000, pos[2]};
      Shot* s = shot_spawn_projectile(this, from, d, this);
      if (s != nullptr) {
        s->size[0] = 0x18;
        s->size[1] = 10;
      }
      left -= 0xF;
      right += 0xF;
    }
    counters[1] = 1;
  }
};

// ---- Shout (vtable 0x8006c810): Shout 0x6e, Burst 0x6f, Bang 0x70.
struct EnemyShout : EnemyUnit {
  // 0x80041128 [27]: for 60 ticks a stream of particles from the target; at 90 it takes
  // up to its ATK in MP.
  void attack1() override {
    BattleGlobals& g = bg();
    loop_limit = 0;
    anim = 4;
    animate(static_cast<u32>(attack_recovery));
    if (g.flags != 0) return;
    counters[0] += g.frame_ticks;
    counters[1] += g.frame_ticks;
    if (0x5A < counters[0]) {
      action_kind = 0;
      begin_action();
      s32 mp = st()->atk[2];
      if (target != nullptr) {
        const s32 has = target->st()->mp;
        if (has < mp) mp = has;
        damage_popup_spawn(target, mp, 0, 1);
        unit_damage_mp(target, mp);
        damage_popup_spawn(this, -mp, 0, 1);
        unit_damage_mp(this, -mp);
      }
    }
    if (counters[0] >= 0x3C || target == nullptr) return;
    if (4 < counters[1]) {
      bsfx_play_at(0x3A, this);
      counters[1] -= 4;
    }
    ShotDesc* d = shot_desc_init(rng_between(5, 9));
    d->speed = 0xC;
    d->target = this;
    const s32 dist = vec_distance_fx(pos, target->pos);
    d->flags = 2;
    d->life = (dist / 0xC) >> 12;
    s32 from[3];
    from[0] = target->pos[0] + rng_spread_fixed(0x40);
    from[1] = target->pos[1] + rng_between_fixed(-0xC0, 0);
    from[2] = target->pos[2] + rng_spread_fixed(0x20);
    shot_spawn_projectile(this, from, d, this);
  }
};

// ---- Wizard (vtable 0x8006c6e8, ctor 0x80041368): Wizard 0xb3, Masterwizard 0xb4,
// Soulmaster 0xb5, Weird-mage 0xb6, Wisesorcerer 0xb7.
struct EnemyWizard : EnemyUnit {
  EnemyWizard() {
    sfx_hit = 0x4C;
    sfx_crit = 0x4E;
  }
  // 0x800413b0 [27]: a homing energy ball past frame 3, with a fading flash at the hand.
  void attack1() override {
    BattleGlobals& g = bg();
    anim = 4;
    if (anim_done(this, static_cast<u32>(attack_recovery))) return;
    if (g.flags != 0 || counters[1] != 0 || frame <= 3) return;
    ShotDesc* d = shot_desc_init(7);
    d->speed = 0x18;
    d->life = 0x1E;
    d->flags = 0x100006;
    d->hit_effect = 7;
    d->hit_bits[2] = 1;
    d->target = target;
    bsfx_play_at(0x34, this);
    const s32 from[3] = {bobj_front_x(this, 0x40), pos[1] - 0xA0000, pos[2]};
    shot_spawn_projectile(this, from, d, this);
    counters[1] = 1;
    Sprite* fx = unit_spawn_sprite(this, 6);
    if (fx == nullptr) return;
    sprite_place_near(fx, this, 0x40, 0xA0, 0);
    sprite_set_fade(fx, 0x80, -2, 8);
    fx->scale_vel[1] = 0x80;
    fx->scale_vel[0] = 0x80;
    fx->roll_speed = 0x28;
    fx->layer = -2;
  }
};

}  // namespace

int enemy_try_approach(EnemyUnit* e, s32 min_dist, u32 chance, u8 action, s16 speed) {
  if ((e->status_bits & 0x80) != 0) return 0;
  if (bg().state != kStateFighting) return 0;
  if (e->airborne == 1) return 1;
  const int idx = enemy_nearest_party_index(e);
  if (idx < 0) return 0;
  PartyUnit* near = bg().party_units[idx];
  const bool far = (min_dist << 12) <= vec_distance_fx(e->pos, near->pos);
  if (!far && !(rng15() < chance)) return 0;
  if (!(rng15() < 25000)) return 0;
  e->reach = speed;
  e->target = near;
  if (Unit* p = enemy_find_provoker()) e->target = p;
  e->action = 1;
  e->move_mode = 0;
  e->action_kind = action;
  e->begin_move();
  e->counters[1] = 0;
  return 1;
}

// The families of the factory's switch (0x80075ad8); null for the other types.
std::unique_ptr<EnemyUnit> enemy_family_create(int type) {
  switch (type) {
    case 1: case 2: case 3: case 0x79: case 0x7A: case 0x7B: case 0x7C: return std::make_unique<EnemyKnight>();
    case 7: case 8: case 9: case 0x7D: case 0x7E: case 0x7F: return std::make_unique<EnemyGuard>();
    case 0xD: case 0xE: case 0xF: return std::make_unique<EnemyPlant>();
    case 0x13: case 0x14: case 0x15: return std::make_unique<EnemyKobold>();
    case 0x16: case 0x17: case 0x18: case 0x84: case 0x85: return std::make_unique<EnemyThief>();
    case 0x19: case 0x1A: case 0x1B: case 0x86: case 0x87: return std::make_unique<EnemyMagius>();
    case 0x1C: case 0x1D: case 0x1E: case 0x88: case 0x89: return std::make_unique<EnemyAxe>();
    case 0x29: case 0x2A: case 0x2B: case 0x92: return std::make_unique<EnemySandglass>();
    case 0x32: case 0x33: case 0x34: return std::make_unique<EnemyOoze>();
    case 0x41: case 0x42: case 0x43: return std::make_unique<EnemyBeastmaster>();
    case 0x44: case 0x45: case 0x46: return std::make_unique<EnemyWolf>();
    case 0x4A: case 0x4B: case 0x4C: case 0x4D: case 0x99: case 0x9A: case 0x9B: return std::make_unique<EnemyGiant>();
    case 0x4E: case 0x4F: case 0x50: case 0x9C: case 0x9D: return std::make_unique<EnemyGoat>();
    case 0x60: case 0x61: case 0x62: case 0xA1: case 0xA2: return std::make_unique<EnemyArcher>();
    case 0x6E: case 0x6F: case 0x70: return std::make_unique<EnemyShout>();
    case 0xB3: case 0xB4: case 0xB5: case 0xB6: case 0xB7: return std::make_unique<EnemyWizard>();
    default: return nullptr;
  }
}

}  // namespace ss::game::battle
