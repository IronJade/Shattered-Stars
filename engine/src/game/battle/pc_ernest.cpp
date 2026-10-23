// Ernest's party-character module (#2151, slot-0 link 0x801ee000-0x801f05ab): the
// PartyUnit subclass (0x370 bytes, vtable 0x801f03e4: slots 35, 36, 37, 45, 46 and three of
// its own, 47-49, which are moves 0-2), its 8 killer moves (states 0x10-0x17, table
// 0x801f055c) and the Shot callbacks they spawn. Move ids (record +0xcc byte - 1); names
// from what the code does: 0 whip lash at the target, 1 the evasion buff, 2 the leaping
// whip, 3 a spinning strike, 4 the energy ball, 5 rapid lashes, 6 the thrown blade, 7 the
// charged storm of blades.
#include <cstring>

#include "game/battle/battle.h"
#include "game/battle/bobj.h"
#include "game/battle/bsfx.h"
#include "game/battle/camera.h"
#include "game/battle/combat.h"
#include "game/battle/enemy_support.h"
#include "game/battle/fx_draw.h"
#include "game/battle/killer.h"
#include "game/battle/particle.h"
#include "game/battle/pc_modules.h"
#include "game/battle/render.h"
#include "game/battle/shot.h"
#include "game/battle/sprite.h"
#include "game/battle/unit_move.h"
#include "game/boot/rng.h"

namespace ss::game::battle {
namespace {

constexpr int kErnest = 10;
const s32 kZero[3] = {0, 0, 0};                     // 0x8006e33c
const s32 kUnitScale[3] = {0x1000, 0x1000, 0x1000};  // 0x8006e34c
constexpr u32 kTrailRgb = 0x207080;

s8 module_s8(u32 address) { return static_cast<s8>(pc_module_u8(kErnest, address)); }
u32 rng_next15() { return static_cast<u32>(rng_next()) & 0x7FFF; }

// ---- Shot callbacks ------------------------------------------------------------------------

// The common whip-hit tail of 0x801ef864 / 0x801efebc: the owner's attack on what the
// Shot meets (angle 0x800 unless it faces left).
int whip_hit(Shot* s, Vec4 pts[8]) {
  Unit* found[8] = {};
  const int n = collider_attack_overlaps(s, found, pts, 0);
  if (n == 0) return 0;
  s->hit_landed = 1;
  unit_attack_targets(s->owner, n, found, pts, 0x100, s->facing[0] < 0 ? 0 : 0x800, 0);
  return n;
}

// 0x801ef864 (move 0): the lash at the target; sparks around the hit.
void cb_lash(Shot* s) {
  BattleGlobals& g = bg();
  s->anim = 1;
  s->loop_limit = 1;
  s->size[2] = s->owner->size[2];
  if (static_cast<u16>(shot_draw_with_shadow(s, 5, 0xFFFFFFFF)) == 0xFFFF) {
    shot_kill(s);
    return;
  }
  if (g.flags != 0) return;
  shot_fade_out(s);
  if (s->hit_landed != 0) return;
  Vec4 pts[8] = {};
  if (whip_hit(s, pts) == 0) return;
  for (int i = 0; i < 2; ++i) {
    if (Sprite* sp = shot_spawn_sprite(s, 4)) {
      const s32 x = pts[0].x + rng_between_fixed(-0x30, 0x30);
      const s32 y = pts[0].y + rng_between_fixed(-0x30, 0x30);
      sprite_set_pos(sp, x, y, pts[0].z);
      sp->layer = 3;
      sprite_set_fade(sp, 0x80, -4, 0xC);
    }
  }
  if (Sprite* sp = shot_spawn_sprite(s, 3)) {
    sprite_set_pos(sp, pts[0].x - 0x60000, pts[0].y, pts[0].z);
    sp->layer = 3;
    sprite_set_fade(sp, 0x80, -4, 8);
  }
  if (Sprite* sp = shot_spawn_sprite(s, 5)) {
    sprite_set_pos(sp, pts[0].x + 0x60000, pts[0].y, pts[0].z);
    sp->layer = 3;
    sprite_set_fade(sp, 0x80, -4, 8);
  }
}

// 0x801efaac (move 4): the energy ball (two lit spheres growing to (+0x10a << 6) + 0x180;
// +0x130 its age, +0x134 the sparkle timer, +0x12c set once it stops: then it fades out
// through +0x10c and dies; hits with power (+0x10a << 7) + 0x200).
void cb_energy_ball(Shot* s) {
  BattleGlobals& g = bg();
  stage_colour_set(0x80, 0x80, 0x80);
  lights_build_oriented(kZero);
  {
    s32 orient[3] = {0, 0, 0};
    obj_set_matrix(s->pos, orient, kUnitScale);
  }
  draw_sphere(7, 7, s->p128 / 2, 0xA06030, 0x100 - s->aux / 2, 0, 1, 0x1000);
  draw_sphere(9, 9, s->p128, 0xA06030, 0x100 - s->aux, 0, 2, 0x1000);
  stage_colour_restore();
  if (s->p12c == 0 && (0x80 < s->p130 || g.state != kStateFighting)) s->p12c = 1;
  if (g.flags != 0) return;
  if (s->p12c == 0) bobj_move(s, s->vel);
  s->p130 += g.frame_ticks;
  s->p134 -= g.frame_ticks;
  if (s->p134 < 1) {
    s->p134 += 4;
    if (Sprite* sp = shot_spawn_sprite(s, 2)) {
      sprite_attach(sp, s, 0, 0, 0);
      const s32 yaw = static_cast<s32>(rng_range(0x1000));
      const s32 r = (s->power << 6) + 0x180;
      sprite_set_orbit(sp, yaw, 0, r, 0, r, 0x38, 0, -2);
      sprite_set_fade(sp, 0x80, -4, 8);
      sp->layer = 1;
    }
    if (Sprite* sp = shot_spawn_sprite(s, 3)) {
      sprite_set_pos(sp, s->pos[0], bobj_ground_height(s), s->pos[2]);
      sprite_set_fade(sp, 0x80, -4, 4);
      sp->layer = 1;
    }
  }
  if (s->p12c != 0) {
    s->aux = static_cast<s16>(s->aux + 0x10);
    if (0xFF < s->aux) {
      shot_kill(s);
      return;
    }
  } else {
    s->p128 += g.frame_ticks << 5;
    const s32 lim = (s->power << 6) + 0x180;
    if (lim < s->p128) s->p128 = lim;
  }
  const s32 half = s->p128 / 2;
  collider_set_attack_box(s, half, half, half);
  if (s->hit_landed != 0) return;
  const int h = shot_hit_melee(s, (s->power << 7) + 0x200, 0);
  if (h == 0) return;
  if (h == 2) {
    const s32 spread[3] = {0x40000, 0x20000, 0x20000};
    effect_debris_burst(s->pos, spread, 3, 2, 1, 0x2000, 4);
    bsfx_play_at(0x48, s);
  }
  if (s->p12c == 0) s->p12c = 1;
  const s32 at[4] = {s->pos[0], s->pos[1], s->pos[2], 0};
  particle_spawn_ring(at, 0xE, 0x90, 0x18, 0x40, 0x402010, 0xC08040, s->p128 / 2, 0);
  sound_handle_pitch_up(bsfx_play_at(0x21, s), 0x1E);
}

// 0x801efebc (move 5): one rapid lash.
void cb_rapid_lash(Shot* s) {
  BattleGlobals& g = bg();
  s->anim = 1;
  s->loop_limit = 1;
  s->size[2] = s->owner->size[2];
  if (static_cast<u16>(shot_draw_with_shadow(s, 5, 0xFFFFFFFF)) == 0xFFFF) {
    shot_kill(s);
    return;
  }
  if (g.flags != 0) return;
  shot_fade_out(s);
  if (s->hit_landed != 0) return;
  Vec4 pts[8] = {};
  if (whip_hit(s, pts) == 0) return;
  if (Sprite* sp = shot_spawn_sprite_at(s, 3, 1)) {
    const s32 x = pts[0].x + rng_between_fixed(-0x20, 0x20);
    const s32 y = pts[0].y + rng_between_fixed(-0x20, 0x20);
    sprite_set_pos(sp, x, y, pts[0].z);
    sp->layer = 3;
    sp->scale_vel[0] = 0x40;
    sp->scale_vel[1] = 0x40;
  }
}

// 0x801f0048 (move 6): the thrown blade (+0x134 its age: past 0x18 it starts fading;
// power ((+0x10a) << 7) + 0x180; debris on the hit).
void cb_blade(Shot* s) {
  BattleGlobals& g = bg();
  s->anim = 1;
  s->layer = 3;
  s->size[2] = static_cast<s16>(s->owner->size[2] * 2);
  shot_draw_with_shadow(s, 2, 0xFFFFFFFF);
  if (g.flags != 0) return;
  if (shot_fade_out(s) != 0) return;
  if (s->p12c == 0 && 0x18 < s->p134) s->p12c = 1;
  bobj_move(s, s->vel);
  s->p134 += g.frame_ticks;
  s->facing[0] = s->vel[0], s->facing[1] = s->vel[1], s->facing[2] = s->vel[2];
  if (s->hit_landed != 0) return;
  Unit* found[8] = {};
  Vec4 pts[8] = {};
  const int n = collider_attack_overlaps(s, found, pts, 0);
  if (n == 0) return;
  s->hit_landed = 1;
  s32 dir = s->vel[0];
  if (dir == 0) dir = s->owner->facing[0];
  unit_attack_targets(s->owner, n, found, pts, (s->power << 7) + 0x180, dir < 0 ? 0x800 : 0, 0);
  const s32 at[3] = {pts[0].x, pts[0].y, pts[0].z};
  const s32 spread[3] = {0x20000, 0x20000, 0x20000};
  effect_debris_burst(at, spread, 3, 1, 2, 0x2000, 0xA);
  s->owner->attack_recovery = 0;
}

// 0x801f022c (move 7): a blade of the storm (fades over 0x40 ticks; a 75.0-long attack box
// in front, hits with the owner's weapon element bit 0x10).
void cb_storm_blade(Shot* s) {
  BattleGlobals& g = bg();
  s->anim = 3;
  s->size[2] = 0x40;
  s->colour_mode = 2;
  s->rgb[0] = static_cast<u8>(-0x80 - s->p130);
  bobj_project(s);
  bobj_anim_draw(s, g.flags);
  if (g.flags != 0) return;
  s->p130 += g.frame_ticks * 2;
  if (0x3F < s->p130) {
    shot_kill(s);
    return;
  }
  s->box_mask = static_cast<u8>(s->box_mask | 4);
  if (0 <= s->facing[0]) {
    s->attack_min[0][0] = s->pos[0];
    s->attack_max[0][0] = s->pos[0] + 0x4B0000;
  } else {
    s->attack_min[0][0] = s->pos[0] - 0x4B0000;
    s->attack_max[0][0] = s->pos[0];
  }
  s->attack_min[0][1] = s->pos[1] - 0xC0000;
  s->attack_max[0][1] = s->pos[1];
  s->attack_min[0][2] = s->pos[2] - 0x40000;
  s->attack_max[0][2] = s->pos[2] + 0x40000;
  if (s->hit_landed != 0) return;
  u8* st = s->owner->stats;
  u16 saved;
  std::memcpy(&saved, st + 0x5C, 2);
  const u16 el = static_cast<u16>(saved | 0x10);
  std::memcpy(st + 0x5C, &el, 2);
  shot_hit_melee(s, 0x100, 0);
  std::memcpy(st + 0x5C, &saved, 2);
}

// ---- The unit ---------------------------------------------------------------------------

struct ErnestUnit : PartyUnit {
  s32 leap[4] = {};  // +0x360: move 2's leap origin x / y and the last free position x / y

  ErnestUnit() {  // 0x801ee0a0
    v2d8 = 1;
    v2da = 1;
    sfx_hit = 0x2F;
    sfx_crit = 0x4D;
  }
  int killer_move_cost(int which) override {  // [35] 0x801ee104
    int total = 0;
    for (int c = 0; c < 2; ++c) {
      const u8 move = record[0xCC + c * 2 + which];
      if (move != 0) total += unit_mp_cost(this, *battle_data(0x8006B327 + move));
    }
    return total;
  }
  int move_mp_cost(int index) override {  // [36] 0x801ee19c
    const u8 move = record[0xCC + (index >> 1) * 2 + (index & 1)];
    return unit_mp_cost(this, move == 0 ? 0 : *battle_data(0x8006B327 + move));
  }
  void killer_move_state(int i) override {  // [37] 0x801ee028 (table 0x801f055c)
    switch (i) {
      case 0: move0(); break;
      case 1: move1(); break;
      case 2: move2(); break;
      case 3: move3(); break;
      case 4: move4(); break;
      case 5: move5(); break;
      case 6: move6(); break;
      case 7: move7(); break;
      default: break;
    }
  }
  int forced_attack_choice() override;  // [45] 0x801ef628

  void move0();  // [47] 0x801ee1fc
  void move1();  // [48] 0x801ee36c
  void move2();  // [49] 0x801ee604
  void move3();  // 0x801eea98
  void move4();  // 0x801eec90
  void move5();  // 0x801eee00
  void move6();  // 0x801eef4c
  void move7();  // 0x801ef110

  void trail_on(int copies) {
    if (effects[0] == nullptr) effects[0] = afterimage_create(this, copies, kTrailRgb, 1);
  }
  void trail_grows() {
    if (effects[0] != nullptr) effects[0]->next_kind = 1;
  }
  void clear_hit_when_boxes_off() {
    if (hit_landed != 0 && (box_mask & 0xC) == 0) hit_landed = 0;
  }
};

// With the MP, a move-2 (byte 2) assignment is taken at 1 in 8 while no evasion buff runs.
int ErnestUnit::forced_attack_choice() {
  if (wait_after_spell != 0) return -1;
  for (int which = 0; which < 2; ++which) {
    if (st()->mp < killer_move_cost(which)) continue;
    for (int c = 0; c < 2; ++c) {
      if (record[0xCC + c * 2 + which] != 2) continue;
      if (rng_next15() < 0x1000 && eva_bonus == 0) return which;
    }
  }
  return -1;
}

// Move 0: lashes at the target, level + 1 of them, from frame 2.
void ErnestUnit::move0() {
  BattleGlobals& g = bg();
  const int lvl = pc_move_charge_level(this, killer_index);
  loop_limit = 4;
  anim = 0;
  if (loops == 0) {
    if (effects[0] == nullptr) effects[0] = afterimage_create(this, lvl + 1, kTrailRgb, 1);
    else effects[0]->next_kind = 1;
  } else {
    if (effects[0] != nullptr) effects[0]->ending = 1;
    effects[0] = nullptr;
  }
  if (g.flags == 0 && 2 <= frame && counters[1] < lvl + 1) {
    ++counters[1];
    if (target != nullptr) {
      if (Shot* s = shot_spawn_callback(cb_lash)) {
        shot_place_at_unit(s, target, counters[1] * 32 - 0x40 - lvl * 16, (target->size[1] << 2) + 0xC0, 0);
        s->facing[0] = -target->facing[0];
        pc_bind_shot(this, s, static_cast<s16>(lvl));
      }
    }
  }
  pc_attack_anim_step(this);
}

// Move 1: the evasion buff (eva_bonus (level + 1) * 15 once the move ends) with sparkles.
void ErnestUnit::move1() {
  BattleGlobals& g = bg();
  const int lvl = pc_move_charge_level(this, killer_index);
  anim = 0;
  loop_limit = 4;
  bobj_texcache_reset(this);
  if (g.flags == 0) {
    camera_follow_start();
    if (effects[0] == nullptr && lvl != 0) effects[0] = afterimage_create(this, 1, kTrailRgb, 1);
    if (5 <= frame) {
      if (counters[1] == 0) {
        counters[1] = 1;
        if (Sprite* sp = unit_spawn_sprite(this, 5)) {
          sprite_set_pos(sp, pos[0], pos[1] - 0x60000, pos[2]);
          sprite_set_fade(sp, 0x80, -1, 0x3C);
          sp->layer = 2;
        }
      }
      s32 n = static_cast<s32>(rng_next15()) * (lvl + 1);
      while (0x5DC0 < n) {
        n -= 0x5DC0;
        if (Sprite* sp = unit_spawn_sprite(this, 2)) {
          const s32 x = pos[0] + rng_spread_fixed(0x80);
          const s32 z = pos[2] + rng_spread_fixed(0x40);
          sprite_set_pos(sp, x, pos[1], z);
          sprite_set_fade(sp, 0x80, -2, 4);
          sp->layer = 3;
        }
        if (Sprite* sp = unit_spawn_sprite(this, 4)) {
          const s32 y = pos[1] - static_cast<s32>(rng_range(0xC0000));
          const s32 z = pos[2] + rng_spread_fixed(0x40);
          sprite_set_pos(sp, pos[0], y, z);
          sprite_set_fade(sp, 0x80, -4, 8);
          sp->vel[0] = rng_between_fixed(-4, 4);
          sp->layer = 4;
        }
      }
    }
  }
  if (pc_attack_anim_step(this) != 0 && g.state == kStateFighting && eva_bonus == 0) {
    eva_bonus = static_cast<u8>((lvl + 1) * 15);
    bsfx_play_at(0xA, this);
  }
}

// Move 2: the leaping whip (counters[1] phase): jumps back (gravity 0xaf0), lands into the
// leap arc of table 0x801f0594 (frames 4-8) and swings down, then drops (gravity 0x80 / 0x400)
// and lands with dust.
void ErnestUnit::move2() {
  BattleGlobals& g = bg();
  const int lvl = pc_move_charge_level(this, killer_index);
  s32 gravity = 0x80;
  switch (counters[1]) {
    case 0:
      if (lvl != 0 && effects[0] == nullptr) effects[0] = afterimage_create(this, lvl, kTrailRgb, 1);
      loop_limit = 2;
      bsfx_play_at(0x26, this);
      camera_follow_start();
      anim = static_cast<u16>(lvl);
      vel[0] = bobj_facing_fx(this, -8);
      vel[1] = static_cast<s32>(0xFFFF0000);
      jumping = 1;
      airborne = 1;
      counters[1] = 1;
      [[fallthrough]];
    case 1:
      if (!(ticks_left <= 0 && frame == 3)) {
        pc_attack_anim_step(this);
        pc_jump_step(this, 0xAF0);
        return;
      }
      counters[1] = 2;
      vel[0] = 0;
      vel[1] = 0;
      leap[0] = bobj_front_x(this, -(module_s8(0x801F059C) << 2));
      leap[1] = pos[1] + (module_s8(0x801F059D) << 14);
      leap[2] = pos[0];
      leap[3] = pos[1];
      [[fallthrough]];
    case 2: {
      const int idx = frame + (ticks_left < 1 ? 1 : 0);
      if (idx < 9) {
        const u32 t = 0x801F0594 + static_cast<u32>(idx) * 2;
        pos[0] = leap[0] + bobj_facing_fx(this, module_s8(t) << 2);
        pos[1] = leap[1] - (module_s8(t + 1) << 14);
        if (unit_check_move(this, kZero, 7) == nullptr) {
          leap[2] = pos[0];
          leap[3] = pos[1];
          pc_attack_anim_step(this);
          return;
        }
        pos[0] = leap[2];
        pos[1] = leap[3];
        bobj_anim_reset(this);
        frame = 9;
      }
      pc_voice_attack_shout(this);
      counters[1] = 3;
      vel[0] = bobj_facing_fx(this, 8);
      vel[1] = 0;
    }
      [[fallthrough]];
    case 3:
      if (counters[2] == 0 && pc_attack_anim_step(this) != 0) {
        counters[2] = 1;
        counters[1] = 4;
      }
      if (g.flags == 0) {
        sfx_hit = 0x4C;
        sfx_crit = 0x4E;
        Unit* found[8] = {};
        Vec4 pts[8] = {};
        const int n = collider_attack_overlaps(this, found, pts, 0);
        if (n != 0 && hit_landed == 0) {
          unit_attack_targets(this, n, found, pts, 0x100, 9999, 0);
          if (Sprite* sp = unit_spawn_sprite_ex(this, static_cast<int>(rng_range(2)) + 4, 1)) {
            sp->layer = 3;
            const s32 at[3] = {pts[0].x, pts[0].y, pts[0].z};
            sprite_set_pos_v(sp, at);
          }
        }
        sfx_hit = 0x2F;
        sfx_crit = 0x4D;
        if (hit_landed != 0 && (box_mask & 0xC) == 0) {
          pc_voice_attack_shout(this);
          hit_landed = 0;
        }
      }
      [[fallthrough]];
    case 4: {
      if (counters[2] != 0) {
        gravity = 0x400;
        model_default();
        anim = 2;
        animate(0);
      }
      const int r = pc_jump_step(this, gravity);
      if (jumping != 0) return;
      if (airborne != 0 && counters[2] == 0) return;
      if (r == 2) {
        for (int i = 0; i < 8; ++i) effect_dust_puff(pos, 4);
      }
      jumping = 0;
      if (effects[0] != nullptr) {
        effects[0]->ending = 1;
        effects[0] = nullptr;
      }
      action_kind = 0x12;
      begin_action();
      return;
    }
    default: return;
  }
}

// Move 3: a spinning strike (sparks at each hit).
void ErnestUnit::move3() {
  BattleGlobals& g = bg();
  const int lvl = pc_move_charge_level(this, killer_index);
  anim = static_cast<u16>(lvl);
  if (lvl != 0) bobj_texcache_reset(this);
  if (pc_attack_anim_step(this) != 0) return;
  if (g.flags != 0) return;
  if (lvl != 0) trail_on(1);
  trail_grows();
  Unit* found[8] = {};
  Vec4 pts[8] = {};
  const int n = collider_attack_overlaps(this, found, pts, 0);
  if (n != 0 && hit_landed == 0) {
    unit_attack_targets(this, n, found, pts, 0x100, 9999, 0);
    for (int i = 0; i < 2; ++i) {
      if (Sprite* sp = unit_spawn_sprite_ex(this, static_cast<int>(rng_range(3)) + 3, 1)) {
        sp->layer = 3;
        const s32 x = pts[0].x + rng_between_fixed(-0x20, 0x20);
        const s32 y = pts[0].y + rng_between_fixed(-0x20, 0x20);
        sprite_set_pos(sp, x, y, pts[0].z);
      }
    }
  }
  clear_hit_when_boxes_off();
}

// Move 4: fires the energy ball from frame 7 (once).
void ErnestUnit::move4() {
  BattleGlobals& g = bg();
  const int lvl = pc_move_charge_level(this, killer_index);
  anim = 0;
  if (lvl != 0) bobj_texcache_reset(this);
  if (pc_attack_anim_step(this) != 0) return;
  if (g.flags != 0) return;
  if (lvl != 0) trail_on(1);
  trail_grows();
  if (counters[1] != 0 || frame < 7) return;
  counters[1] = 1;
  sound_handle_pitch_up(bsfx_play_at(0x34, this), 0x2D);
  bsfx_play_at(0x1F, this);
  if (Shot* s = shot_spawn_callback(cb_energy_ball)) {
    shot_place_at_unit(s, this, 0x60, 0x60, 0);
    s->vel[0] = bobj_facing_fx(this, 0x10);
    pc_bind_shot(this, s, static_cast<s16>(lvl));
    pc_camera_frame_target(this, 0x3C);
  }
}

// Move 5: level + 2 rapid lashes at the target, one every 12 ticks from frame 2.
void ErnestUnit::move5() {
  BattleGlobals& g = bg();
  const int lvl = pc_move_charge_level(this, killer_index);
  anim = 0;
  loop_limit = static_cast<u8>(lvl + 4);
  bobj_texcache_reset(this);
  if (pc_attack_anim_step(this) != 0) return;
  if (g.flags != 0) return;
  counters[2] -= g.frame_ticks;
  if (0 < counters[2]) return;
  if (lvl + 2 <= counters[1] || frame < 2) return;
  counters[2] += 0xC;
  ++counters[1];
  sound_handle_pitch_down(bsfx_play_at(0x34, this), 0x1E);
  bsfx_play_at(0x21, this);
  if (Shot* s = shot_spawn_callback(cb_rapid_lash)) {
    s->facing[0] = facing[0];
    shot_place_at_unit(s, target, rng_spread(0x40), 0, 0);
    pc_bind_shot(this, s, static_cast<s16>(lvl));
    pc_camera_frame_target(this, 0x3C);
  }
}

// Move 6: throws the blade at frame 14 (at a far target, or from level 1); hits up close.
void ErnestUnit::move6() {
  BattleGlobals& g = bg();
  const int lvl = pc_move_charge_level(this, killer_index);
  loop_limit = 1;
  anim = 0;
  bobj_texcache_reset(this);
  if (pc_attack_anim_step(this) != 0) return;
  if (g.flags == 0) {
    if (effects[0] == nullptr && lvl != 0) effects[0] = afterimage_create(this, 1, kTrailRgb, 1);
    trail_grows();
    camera_follow_start();
    if (counters[1] == 0 && 0xE <= frame && (target_far != 0 || lvl != 0)) {
      counters[1] = 1;
      if (Shot* s = shot_spawn_callback(cb_blade)) {
        shot_place_at_unit(s, this, 0x60, 0, 0);
        s->vel[0] = bobj_facing_fx(this, 0x10);
        s->facing[0] = facing[0];
        pc_bind_shot(this, s, static_cast<s16>(lvl));
      }
      pc_camera_frame_target(this, 0x32);
    }
  }
  pc_attack_hit(this, 0x100, 0);
  clear_hit_when_boxes_off();
}

// Move 7: the charged storm (counters[2] phase: 0 charging (60 ticks, a rising sound and
// a burst sprite), 1 storming: 40 homing streaks into the front and, from level 1, two
// wings; every 8 ticks level + 1 blades (heights from 0x801f05a8), level + 1 rounds).
void ErnestUnit::move7() {
  BattleGlobals& g = bg();
  const int lvl = pc_move_charge_level(this, killer_index);
  bobj_texcache_reset(this);
  if (counters[2] == 0) {
    loop_limit = 0;
    anim = 0;
    animate(0);
  } else if (0 <= counters[2] && counters[2] < 3) {
    loop_limit = 2;
    anim = 1;
    if (pc_attack_anim_step(this) != 0) return;
  }
  if (g.flags == 0) {
    if (effects[0] == nullptr && lvl != 0) effects[0] = afterimage_create(this, lvl, kTrailRgb, 1);
    trail_grows();
    camera_follow_start();
    if (counters[2] == 0) {
      if (counters[1] == 0 && 3 <= frame) {
        counters[1] = 1;
        if (Sprite* sp = unit_spawn_sprite_ex(this, 3, 2)) {
          sprite_set_pos(sp, bobj_front_x(this, -0x40), pos[1] - 0xA0000, pos[2]);
          sp->layer = 3;
        }
        bsfx_play_at(0x52, this);
      }
      counters[3] += g.frame_ticks;
      if (0x3C < counters[3]) {
        counters[2] = 1;
        counters[3] = 0;
        counters[0] = 0;
        bobj_anim_reset(this);
      }
    } else if (counters[2] == 1) {
      if (counters[3] == 0) {
        if (camera_focus != 0) {
          const s32 ofs[3] = {bobj_facing_fx(this, 0x140), 0, 0};
          camera_set_follow_offset(g.camera, ofs);
        }
        const s32 c[4] = {bobj_front_x(this, 0x70), pos[1] - 0x60000, pos[2], 0};
        for (int i = 0; i < 0x28; ++i) {
          const s32 dx = bobj_facing_fx(this, 0x384);
          const s32 dy = rng_between_fixed(-0x4B0, 0x190);
          const s32 dz = rng_between_fixed(-0x280, 0x280);
          const s32 to[4] = {c[0] + dx, pos[1] + dy, pos[2] + dz, 0};
          particle_spawn_homing_line(c, to, 0x1020, 0xC0D0, 0x3C, 0xC000, -0x40);
        }
        sound_handle_fade_out(bsfx_play_at(0x2D, this), 0x50, 0);
        if (lvl != 0) {
          for (int k = 0; k < 2; ++k) {
            if (Sprite* sp = unit_spawn_sprite(this, 4)) {
              sprite_set_pos(sp, bobj_front_x(this, 0x70), pos[1] - 0x60000, pos[2]);
              sprite_set_fade(sp, 0x80, -2, 8);
              sp->vel[1] = k != 0 ? 0x2000 : -0x2000;
              const s16 r = static_cast<s16>(facing[0] < 0 ? 0x400 : -0x400);
              sp->roll = r;
              sp->roll_speed = static_cast<s16>(k != 0 ? r / 90 : -r / 90);
            }
          }
        }
      }
      counters[0] -= g.frame_ticks;
      if (counters[0] < 1) {
        counters[0] += 8;
        for (int k = 0; k <= lvl; ++k) {
          if (Shot* s = shot_spawn_callback(cb_storm_blade)) {
            shot_place_at_unit(s, this, 0x70, (module_s8(0x801F05A8 + static_cast<u32>(k)) << 12) | 0x60, 0);
            s->roll = static_cast<s16>(facing[0] < 0 ? 0x400 : -0x400);
            s->facing[0] = facing[0];
            pc_bind_shot(this, s, static_cast<s16>(lvl));
          }
        }
        ++counters[3];
        if (lvl < counters[3]) ++counters[2];
      }
    }
  }
  pc_attack_hit(this, 0x100, 0);
}

}  // namespace

std::unique_ptr<PartyUnit> pc_ernest_create() { return std::make_unique<ErnestUnit>(); }

}  // namespace ss::game::battle
