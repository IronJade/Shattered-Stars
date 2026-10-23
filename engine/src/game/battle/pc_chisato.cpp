// Chisato's party-character module (#2159, slot-0 link 0x801ee000-0x801f0abc): the
// PartyUnit subclass (0x368 bytes, vtable 0x801f0904: slots 1, 4, 6, 30, 33, 35, 36, 37,
// 39, 46 and eight of its own, 47-54, which are the moves), her 8 killer moves (states
// 0x10-0x17, table 0x801f0a7c) and the Shot callbacks they spawn. Move ids (record +0xcc
// byte - 1); names from what the code does: 0 a jumping kick (one more hit a level), 1
// a combo of up to three strikes, 2 flying blades with ribbon trails, 3 rapid needles
// (level 2: blades raining on the target), 4 a beam of shots between two summoned
// sprites, 5 a fan of shots with afterimages, 6 stars falling on the target, 7 a single
// strike. Her normal attack can add sparks and thrown weapons (status bits) and a
// skill-46 knock-back.
#include <cstring>

#include "game/battle/battle.h"
#include "game/battle/bobj.h"
#include "game/battle/bsfx.h"
#include "game/battle/camera.h"
#include "game/battle/combat.h"
#include "game/battle/enemy_support.h"
#include "game/battle/killer.h"
#include "game/battle/pc_module_util.h"
#include "game/battle/pc_modules.h"
#include "game/battle/render.h"
#include "game/battle/shot.h"
#include "game/battle/sprite.h"
#include "game/battle/stage.h"
#include "game/battle/unit_move.h"
#include "game/boot/rng.h"

namespace ss::game::battle {
namespace {

const s32 kZero[3] = {0, 0, 0};  // 0x8006e33c

s16 rds16(const u8* p) {
  s16 v;
  std::memcpy(&v, p, 2);
  return v;
}
void wrs16(u8* p, s32 v) {
  const s16 s = static_cast<s16>(v);
  std::memcpy(p, &s, 2);
}

// The knock-back angle of a sweeping hit: up and away from the side the unit faces.
s32 sweep_angle(const BObj* u) { return u->facing[0] < 0 ? 0xB8E : static_cast<s32>(0xFFFFFC72); }

// The owner's weapon element ORed with 'bits' around a hit (+0x5c of its stats).
struct ElementBits {
  u8* st;
  u16 saved;
  ElementBits(Unit* owner, u16 bits) : st(owner->stats), saved(static_cast<u16>(rds16(owner->stats + 0x5C))) {
    wrs16(st + 0x5C, saved | bits);
  }
  ~ElementBits() { wrs16(st + 0x5C, saved); }
};

// (The console keeps a callback Shot's afterimage at its +0x130 and the ribbon trail of
// move 2's blades at +0x134; the port's afterimages are in pc_module_util's table.)
void shot_trail_end(Shot* s) {
  if (s->trail != nullptr) s->trail->stop = 1;
}

// The hit point of what the Shot's attack boxes meet now (after a hit).
Vec4 shot_hit_point(Shot* s) {
  Unit* found[8] = {};
  Vec4 pts[8] = {};
  collider_attack_overlaps(s, found, pts, 0);
  return pts[0];
}

// ---- Shot callbacks ------------------------------------------------------------------------

// 0x801efd34 (move 2): a flying blade with a ribbon trail; ends on a hit or after 20 ticks.
void cb_blade(Shot* s) {
  BattleGlobals& g = bg();
  s->size[0] = 8;
  s->size[2] = 4;
  s->size[1] = 3;
  s->anim = 2;
  shot_draw_and_box(s, 3, 0xFFFFFFFF);
  if (g.flags != 0) return;
  s->p130 += g.frame_ticks;
  if (s->p12c == 0 && 0x14 < s->p130) s->p12c = 1;
  if (s->p134 == 0) {  // (the console keeps the trail pointer there)
    s->trail = ribbon_trail_create(s, 0x18, 0x306020, 0xA0C080);
    if (s->trail != nullptr) {
      s->trail->back = &s->trail;
      s->p134 = 1;
    }
  }
  if (shot_fade_out(s) != 0) {
    shot_trail_end(s);
    return;
  }
  if (shot_hit_melee(s, 0x100, 0) == 0) {
    bobj_move(s, s->vel);
    return;
  }
  const Vec4 at = shot_hit_point(s);
  if (Sprite* sp = sprite_spawn_from(4, 0, s, nullptr)) {
    sprite_set_pos_v(sp, &at.x);
    sprite_set_fade(sp, 0x80, -4, 8);
    sp->layer = 3;
  }
  shot_trail_end(s);
  shot_kill(s);
}

// 0x801eff04 (move 3, levels 0-1): a needle (element 4).
void cb_needle(Shot* s) {
  BattleGlobals& g = bg();
  s->size[2] = 0x30;
  s->anim = 2;
  s->loop_limit = 1;
  if (static_cast<s16>(shot_draw_and_box(s, 3, 0x6090)) == -1) {
    shot_kill(s);
    return;
  }
  if (g.flags != 0 || shot_fade_out(s) != 0) return;
  {
    ElementBits el(s->owner, 4);
    if (shot_hit_melee(s, 0x40, 0) != 0) {
      const Vec4 at = shot_hit_point(s);
      if (Sprite* sp = shot_spawn_sprite(s, 10)) {
        sprite_set_pos_v(sp, &at.x);
        sprite_set_fade(sp, 0x80, -4, 8);
        sp->layer = 3;
      }
    }
  }
  bobj_move(s, s->vel);
}

// 0x801f006c (move 3, level 2): a blade falling on the target (element 4); a flash and the
// owner's texture timer when it lands or hits.
void cb_falling_blade(Shot* s) {
  BattleGlobals& g = bg();
  s->size[2] = 0x30;
  s->anim = 2;
  bobj_project(s);
  bobj_anim_draw(s, g.flags);
  if (g.flags != 0 || shot_fade_out(s) != 0) return;
  bool hit;
  {
    ElementBits el(s->owner, 4);
    hit = shot_hit_melee(s, 0xC0, 0) != 0;
    if (hit) {
      const Vec4 at = shot_hit_point(s);
      if (Sprite* sp = shot_spawn_sprite(s, 10)) {
        sprite_set_pos_v(sp, &at.x);
        sprite_set_fade(sp, 0x80, -4, 8);
        sp->layer = 3;
      }
    }
  }
  if (!hit) {
    s->vel[1] += g.frame_ticks * 0x1000;
    if (0x18000 < s->vel[1]) s->vel[1] = 0x18000;
    const s32 ground = bobj_ground_height(s);
    if (s->pos[1] != ground) {
      bobj_move(s, s->vel);
      if (ground < s->pos[1]) s->pos[1] = ground;
      return;
    }
  }
  static_cast<PartyUnit*>(s->owner)->tex_timer = 0x1E;
  if (Sprite* sp = shot_spawn_sprite(s, 7)) {
    sprite_set_pos_v(sp, s->pos);
    sprite_set_fade(sp, 0x80, -4, 8);
    sp->layer = 3;
  }
  shot_kill(s);
}

// 0x801f027c (move 4): a beam shot (25 ticks).
void cb_beam(Shot* s) {
  BattleGlobals& g = bg();
  s->size[2] = 0x30;
  s->anim = 2;
  s->layer = 4;
  shot_draw_and_box(s, 3, 0x208010);
  if (0x18 < s->p128) {
    shot_kill(s);
    return;
  }
  if (g.flags != 0 || shot_fade_out(s) != 0) return;
  s->p128 += g.frame_ticks;
  shot_hit_melee(s, 0x60, 0);
  bobj_move(s, s->vel);
}

// 0x801f033c (move 5): a shot of the fan with an afterimage (element 0x10, one hit).
void cb_fan(Shot* s) {
  BattleGlobals& g = bg();
  s->size[2] = 0x30;
  s->anim = 2;
  s->layer = 3;
  shot_draw_and_box(s, 3, 0x904020);
  if (0x30 < s->p128 && s->p12c == 0) {
    s->p12c = 1;
    return;
  }
  static_cast<PartyUnit*>(s->owner)->tex_timer = 10;
  Afterimage* img = shot_image(s);
  if (img == nullptr) {
    img = afterimage_create(s, s->power + 2, 0x602010, 1);
    shot_image_set(s, img);
  }
  if (img != nullptr) img->next_kind = 1;
  if (g.flags != 0) return;
  if (shot_fade_out(s) != 0) {
    shot_image_end(s);
    return;
  }
  s->p128 += g.frame_ticks;
  if (s->hit_landed == 0) {
    ElementBits el(s->owner, 0x10);
    shot_hit_melee(s, 0x100, 0);
  }
  bobj_move(s, s->vel);
}

// 0x801f04c0 (move 6): a star falling on the target; on a hit, sparks, a sphere and a
// spray; on the ground, a smaller shake; debris either way.
void cb_star(Shot* s) {
  BattleGlobals& g = bg();
  s->size[2] = 0x60;
  s->anim = 1;
  s->layer = 3;
  shot_draw_and_box(s, 3, 0xFFFFFFFF);
  if (shot_image(s) == nullptr && s->power != 0) shot_image_set(s, afterimage_create(s, s->power, 0x605030, 1));
  if (g.flags != 0) return;
  if (shot_fade_out(s) != 0) {
    shot_image_end(s);
    return;
  }
  bobj_move(s, s->vel);
  s->facing[0] = s->vel[0], s->facing[1] = s->vel[1], s->facing[2] = s->vel[2];
  s->vel[1] += g.frame_ticks * 0x1000;
  if (s->hit_landed == 0 && shot_hit_melee(s, 0x180, 0) != 0) {
    screen_shake2(6, 6, 4, 4, 0xC);
    bsfx_play_at(0x21, s);
    const Vec4 at = shot_hit_point(s);
    for (int i = 0; i < 4; ++i) {
      if (Sprite* sp = shot_spawn_sprite(s, static_cast<int>(rng_range(2)) + 3)) {
        sprite_set_pos_v(sp, &at.x);
        sprite_set_fade(sp, 0x80, -2, 8);
        const Vec4 v = vec_random_in_disc_xz(kZero, 8);
        sp->vel[0] = v.x, sp->vel[1] = v.y, sp->vel[2] = v.z, sp->extra34 = v.w;
        sp->layer = 5;
      }
    }
    const s32 ground_at[3] = {s->pos[0], bobj_ground_height(s), s->pos[2]};
    effect_spawn_sphere_at_default(ground_at, 7, 8, 0x1E, 0x50, 0x2040FF, 0, 1);
    if (Shot* r = effect_spawn_sparks_at(ground_at, 10, 0x40, 200, 0x1E, 0x3C, 0, 2, 0x28, 0)) {
      r->anim = 4;
      bobj_share_sprite(s, r);
    }
  } else {
    const s32 ground = bobj_ground_height(s);
    if (ground != s->pos[1]) {
      if (s->pos[1] <= ground) return;
      s->pos[1] = ground;
      s->vel[0] = s->vel[1] = s->vel[2] = 0;
      return;
    }
    screen_shake(4, 4, 8);
  }
  bsfx_play_at(0x50, s);
  const s32 spread[3] = {0x180000, 0x180000, 0x180000};
  effect_debris_burst(s->pos, spread, 3, 0, 1, 0x1000, 0);
  effect_debris_burst(s->pos, spread, 4, 1, 2, 12000, 0xD);
  effect_debris_burst(s->pos, spread, 3, 2, 2, 0x2000, 6);
  effect_debris_burst(s->pos, spread, 3, 2, 2, 12000, 5);
  shot_image_end(s);
  shot_kill(s);
}

// ---- The unit ---------------------------------------------------------------------------

constexpr u32 kTrailRgb = 0x608050;

struct ChisatoUnit : PartyUnit {
  Sprite* summon[2] = {};  // +0x360 / +0x364: move 4's two sprites

  ChisatoUnit() {  // 0x801ee0a0
    v2d8 = 1;
    v2da = 1;
  }
  // [1] 0x801ee0ec: pc_update.
  // [4] 0x801ee10c: an interrupted move 4 lets its sprites fade out.
  void interrupt() override {
    if (summon[0] == nullptr || counters[1] == 0) return;
    sprite_set_fade(summon[0], 0x80, -2, 0);
    summon[0]->blend = 2;
    summon[0] = nullptr;
    if (counters[1] != 0 && summon[1] != nullptr) {
      sprite_set_fade(summon[1], 0x80, -2, 0);
      summon[1]->blend = 2;
      summon[1] = nullptr;
    }
  }
  // [6] 0x801efcfc: the leap's flight time, 0x68000 shorter (at least 1).
  s32 jump_time(s32 v, const s32 dest[3]) override {
    const s32 t = Unit::jump_time(v, dest) - 0x68000;
    return t < 1 ? 1 : t;
  }
  void state_attack() override;  // [30] 0x801ee304
  void state_victory() override {  // [33] 0x801efc84: anim 8
    if (counters[0] == 0) {
      anim = 8;
      counters[0] = 1;
    }
    model_default();
    animate(0);
  }
  int killer_move_cost(int which) override {  // [35] 0x801ee1ac
    int total = 0;
    for (int c = 0; c < 2; ++c) {
      const u8 move = record[0xCC + c * 2 + which];
      if (move != 0) total += unit_mp_cost(this, *battle_data(0x8006B34F + move));
    }
    return total;
  }
  int move_mp_cost(int index) override {  // [36] 0x801ee244
    const u8 move = record[0xCC + (index >> 1) * 2 + (index & 1)];
    return unit_mp_cost(this, move == 0 ? 0 : *battle_data(0x8006B34F + move));
  }
  void killer_move_state(int i) override {  // [37] 0x801ee028 (table 0x801f0a7c)
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
  void play_move_voice(int index) override {  // [39] 0x801ee2a4: not for move byte 7
    if (record[0xCC + (index >> 1) * 2 + (index & 1)] != 7) play_voice(index * 2);
  }

  void move0();  // [47] 0x801ee57c
  void move1();  // [48] 0x801ee7a4
  void move2();  // [49] 0x801eebac
  void move3();  // [50] 0x801eee08
  void move4();  // [51] 0x801ef1a4
  void move5();  // [52] 0x801ef460
  void move6();  // [53] 0x801ef718
  void move7();  // [54] 0x801ef95c

  void trail_on(int copies) {
    if (effects[0] == nullptr) effects[0] = afterimage_create(this, copies, kTrailRgb, 1);
  }
  // A strike with the knock-back of a sweep when 'sweep' (stats +0x52 lowered by 4 for
  // it) and the hit flash; the hit flag clears once the attack boxes are off.
  void strike(bool sweep, bool flash_scale);
};

void ChisatoUnit::strike(bool sweep, bool flash_scale) {
  Unit* found[8] = {};
  Vec4 pts[8] = {};
  const int n = collider_attack_overlaps(this, found, pts, 0);
  bool check = hit_landed != 0;
  if (n != 0 && hit_landed == 0) {
    s32 angle = 9999;
    const s16 kb = rds16(stats + 0x52);
    if (sweep) {
      angle = sweep_angle(this);
      wrs16(stats + 0x52, kb - 4);
    }
    unit_attack_targets(this, n, found, pts, 0x100, angle, 0);
    wrs16(stats + 0x52, kb);
    if (Sprite* sp = unit_play_anim(this, 4)) {
      sprite_set_pos_v(sp, &pts[0].x);
      sprite_set_fade(sp, 0x60, -4, 0xC);
      sp->layer = 3;
      if (flash_scale) sp->scale_vel[0] = sp->scale_vel[1] = 0x30;
    }
    check = hit_landed != 0;
  }
  if (check && (box_mask & 0xC) == 0) hit_landed = 0;
}

// [30] 0x801ee304: the normal attack. A hit may get skill 46's knock-back (a skill-level
// in 25 chance, with its notice); weapon status 0x10 adds sparks on the ground under the
// hit, 0x20 eight thrown weapons flying off it.
void ChisatoUnit::state_attack() {
  pc_attack_animate(this);
  if (bg().flags != 0) return;
  Unit* found[8] = {};
  Vec4 pts[8] = {};
  const int n = collider_attack_overlaps(this, found, pts, 0);
  if (n == 0 || hit_landed != 0) return;
  s32 angle = 9999;
  const u32 skill = static_cast<u32>(skill_level(0x2E));
  if (rng_range(0x19) < skill) {
    angle = sweep_angle(this);
    effect_skill_popup(this, 0x2E);
  }
  unit_attack_targets(this, n, found, pts, 0x100, angle, 0);
  if (status_test(2, 0x10) != 0) {
    const s32 at[3] = {pts[0].x, stage_ground_height(pts[0].x >> 12, pts[0].z >> 12), pts[0].z};
    effect_spawn_sparks_at(at, 0xC, 0x10, 100, 0xC, 0x2D, 2, 1, 0x10, 0);
  }
  if (status_test(2, 0x20) == 0) return;
  for (int i = 0; i < 8; ++i) {
    Shot* s = shot_spawn(0x2C);
    if (s == nullptr) continue;
    s->p130 = 1;
    s->pos[0] = pts[0].x, s->pos[1] = pts[0].y, s->pos[2] = pts[0].z, s->pos_w = pts[0].w;
    s->vel[0] = bobj_facing_fx(this, rng_between(8, 0x10));
    s->vel[1] = rng_between_fixed(-12, 3);
    s->vel[2] = rng_between_fixed(-12, 12);
    s->owner = this;
    bobj_share_sprite(this, s);
  }
}

// Move 0: a jumping kick that hits once more a level.
void ChisatoUnit::move0() {
  BattleGlobals& g = bg();
  const int lvl = pc_move_charge_level(this, killer_index);
  loop_limit = static_cast<u8>(lvl + 1);
  camera_follow_start();
  anim = 0;
  if (effects[0] == nullptr && lvl != 0) effects[0] = afterimage_create(this, lvl, kTrailRgb, 1);
  bool fly = true;
  if (g.flags == 0) {
    if (counters[1] == 0 && 0 < frame) {
      if (target != nullptr) facing[0] = pos[0] < target->pos[0] ? 0x40000 : static_cast<s32>(0xFFFC0000);
      counters[1] = 1;
      vel[1] = static_cast<s32>(0xFFFFB000);
      airborne = 1;
      jumping = 1;
    }
    pc_attack_hit(this, 0x100, 0);
    if (hit_landed != 0 && counters[2] < lvl + 1) {
      if (counters[3] == 0 && effects[0] != nullptr) effects[0]->next_kind = 1;
      counters[3] += g.frame_ticks;
      if (6 < counters[3]) {
        counters[3] = 0;
        hit_landed = 0;
        ++counters[2];
      }
    }
    if (counters[1] == 0) {
      fly = false;
    } else if (unit_try_move_scaled(this, vel, 7) == nullptr) {
      bobj_move(this, vel);
    }
  }
  if (fly && counters[1] != 0) vel[0] = bobj_facing_fx(this, 8);
  pc_jump_step(this, 0x528);
  pc_attack_anim_step(this);
}

// Move 1: a combo of lvl + 1 strikes (the second with a sweep), stepping in; the third
// is a hop.
void ChisatoUnit::move1() {
  BattleGlobals& g = bg();
  const int lvl = pc_move_charge_level(this, killer_index);
  loop_limit = 1;
  camera_follow_start();
  if (effects[0] == nullptr && lvl != 0) effects[0] = afterimage_create(this, lvl * 2, kTrailRgb, 1);
  if (g.flags == 0) {
    strike(counters[1] == 1, true);
    const s32 step = counters[1];
    if (step == 1) {
      vel[0] = 0;
      if (frame == 0) vel[0] = bobj_facing_fx(this, 0x20 / g.frame_ticks);
      if (ticks_left < 1 && frame == 2) vel[0] = bobj_facing_fx(this, -0x20 / g.frame_ticks);
    } else if (step == 0) {
      vel[0] = vel[1] = vel[2] = 0;
      if (hit_landed == 0 && (box_mask & 0xC) != 0) vel[0] = bobj_facing_fx(this, 0x20 / g.frame_ticks);
    } else if (step == 2) {
      if (0 < frame && counters[2] == 0) {
        counters[2] = 1;
        vel[1] = static_cast<s32>(0xFFFF8000);
        vel[0] = bobj_facing_fx(this, 8 / g.frame_ticks);
        airborne = 1;
        jumping = 1;
      }
      if (counters[2] != 0) pc_jump_step(this, 0x800);
    }
    if (g.flags == 0 && unit_try_move_scaled(this, vel, 7) == nullptr) bobj_move(this, vel);
  }
  // Each strike but the last runs to its end, then the next starts.
  for (;;) {
    anim = static_cast<u16>(counters[1]);
    if (counters[1] == lvl || g.state != kStateFighting) break;
    if (static_cast<s16>(animate(0)) != -1) return;
    hit_landed = 0;
    counters[3] = 0;
    counters[2] = 0;
    ++counters[1];
    bobj_anim_reset(this);
    pc_voice_attack_shout(this);
  }
  pc_attack_anim_step(this);
}

// Move 2: flying blades in a fan (3, or 5 at level 1; level 2: two volleys, 3 then 5).
void ChisatoUnit::move2() {
  BattleGlobals& g = bg();
  const int lvl = pc_move_charge_level(this, killer_index);
  loop_limit = 1;
  camera_follow_start();
  anim = lvl == 2 ? 1 : 0;
  if (pc_attack_anim_step(this) != 0 || g.flags != 0) return;
  if (effects[0] == nullptr && lvl != 0) effects[0] = afterimage_create(this, lvl * 2, kTrailRgb, 1);
  if (effects[0] != nullptr) effects[0]->next_kind = 1;
  const s8 at_frame = lvl == 2 ? (counters[1] == 0 ? 7 : 0xB) : 6;
  const int count = lvl == 2 ? (counters[1] == 0 ? 3 : 5) : (lvl == 1 ? 5 : 3);
  if (counters[1] > (lvl == 2 ? 1 : 0) || frame < at_frame) return;
  ++counters[1];
  bsfx_play_at(0x1F, this);
  pc_camera_frame_target(this, 0x3C);
  s32 a = 0;
  for (int i = 0; i < count; ++i, a += 0x155) {
    Shot* s = shot_spawn_callback(cb_blade);
    if (s == nullptr) continue;
    shot_place_at_unit(s, this, 0x20, 0x70, 0);
    const s32 ang = count == 3 ? a - 0x155 : a - 0x2AA;
    s->vel[0] = bobj_facing_fx(this, (bcos(static_cast<u32>(ang)) * 0x18) >> 12);
    s->vel[2] = bsin(static_cast<u32>(ang)) * 0x18;
    s->vel[1] = 0;
    pc_bind_shot(this, s, static_cast<s16>(lvl));
  }
}

// Move 3: needles every 4 ticks from frame 13 (levels 0-1, a looping sound); level 2: a
// flash, then every 4 ticks from frame 16 three sparkles and a blade falling on the target.
void ChisatoUnit::move3() {
  BattleGlobals& g = bg();
  const int lvl = pc_move_charge_level(this, killer_index);
  loop_limit = lvl == 1 ? 3 : 2;
  camera_follow_start();
  anim = lvl == 2 ? 1 : 0;
  bobj_texcache_reset(this);
  if (pc_attack_anim_step(this) != 0 || g.flags != 0) return;
  if (lvl < 2) {
    if (0xC < frame) {
      counters[2] -= g.frame_ticks;
      if (counters[2] < 1) {
        if (counters[3] == 0) counters[3] = static_cast<s32>(bsfx_play_at(0x2D, this));
        counters[2] += 4;
        if (Shot* s = shot_spawn_callback(cb_needle)) {
          shot_place_at_unit(s, this, 0xA0, 0x30, 0);
          s->vel[0] = bobj_facing_fx(this, 0x28);
          s->vel[2] = rng_between_fixed(-4, 4);
          s->vel[1] = 0;
          pc_bind_shot(this, s, static_cast<s16>(lvl));
        }
      }
    }
    if (counters[3] != 0) {
      sound_handle_fade_out(static_cast<u32>(counters[3]), 0x1E, 8);
      if (g.flags != 0) sound_handle_duck(static_cast<u32>(counters[3]), 8);  // (never: flags are 0 here)
    }
    return;
  }
  if (frame <= 0xF) return;
  if (counters[1] == 0) {
    bsfx_play_at(0x50, this);
    counters[1] = 1;
    if (Sprite* sp = unit_play_anim(this, 4)) {
      sprite_set_pos(sp, bobj_front_x(this, 0xC), pos[1] - 0xE0000, pos[2]);
      sprite_set_fade(sp, 0x80, -4, 4);
      sp->scale_vel[1] = 0x80;
      sp->layer = 2;
    }
  }
  counters[2] -= g.frame_ticks;
  if (counters[2] >= 1) return;
  counters[2] += 4;
  pc_camera_frame_target(this, 0x3C);
  if (target == nullptr) return;
  for (int i = 0; i < 3; ++i) {
    if (Sprite* sp = unit_play_anim(this, rng_between(6, 9))) {
      const s32 dx = rng_between_fixed(-0x180, 0x180);
      const s32 y = rng_between_fixed(-0x258, -0x1F4);
      const s32 dz = rng_between_fixed(-0x180, 0x180);
      sprite_set_pos(sp, target->pos[0] + dx, y, target->pos[2] + dz);
      sp->accel[1] = 0x1000;
      sprite_set_fade(sp, 0x80, -4, 8);
    }
  }
  if (Shot* s = shot_spawn_callback(cb_falling_blade)) {
    const s32 dx = rng_between_fixed(-0x60, 0x60);
    s->pos[1] = static_cast<s32>(0xFFDA8000);
    s->pos[0] = target->pos[0] + dx;
    const s32 dz = rng_between_fixed(-0x60, 0x60);
    s->pos[2] = target->pos[2] + dz;
    pc_bind_shot(this, s, static_cast<s16>(lvl));
  }
}

// Move 4: from frame 9 two sprites in front of her, and while the animation loops a beam
// shot every 10 ticks between them.
void ChisatoUnit::move4() {
  BattleGlobals& g = bg();
  const int lvl = pc_move_charge_level(this, killer_index);
  loop_limit = static_cast<u8>(lvl + 5);
  camera_follow_start();
  anim = 0;
  bobj_texcache_reset(this);
  if (pc_attack_anim_step(this) != 0) {
    interrupt();
    return;
  }
  if (g.flags != 0) return;
  if (counters[2] == 0 && 7 < frame) {
    counters[2] = 1;
    sound_handle_fade_out(bsfx_play_at(0x42, this), 8, 0x10);
  }
  if (frame <= 8) return;
  if (counters[1] == 0) {
    if (camera_focus != 0) {
      const s32 ofs[3] = {bobj_facing_fx(this, 0x21C), 0, 0};
      camera_set_lookat_offset(g.camera, ofs);
    }
    counters[1] = 1;
    summon[0] = unit_play_anim_ex(this, 2, 7);
    if (summon[0] != nullptr) {
      summon[0]->size[2] = 4;
      sprite_set_pos(summon[0], bobj_front_x(this, 0x100), pos[1], pos[2]);
      summon[0]->layer = 2;
      summon[0]->facing[0] = facing[0];
    }
    summon[1] = unit_play_anim_ex(this, 5, 7);
    if (summon[1] != nullptr) {
      sprite_set_pos(summon[1], bobj_front_x(this, 0x100), pos[1], pos[2]);
      summon[1]->layer = 4;
      summon[1]->facing[0] = facing[0];
    }
  }
  if (loops == 0) return;
  counters[2] -= g.frame_ticks;
  if (counters[2] >= 1) return;
  counters[2] += 10;
  bsfx_play_at(0x3A, this);
  if (Shot* s = shot_spawn_callback(cb_beam)) {
    shot_place_at_unit(s, this, 0x1A4, 0xA4, 0);
    s->vel[0] = bobj_facing_fx(this, 0x18);
    s->vel[2] = rng_between_fixed(-4, 4);
    s->vel[1] = 0;
    pc_bind_shot(this, s, static_cast<s16>(lvl));
  }
  pc_camera_frame_target(this, 0x28);
}

// Move 5: at frame 20 a burst of sparkles and a fan of lvl * 2 + 3 shots.
void ChisatoUnit::move5() {
  BattleGlobals& g = bg();
  const int lvl = pc_move_charge_level(this, killer_index);
  loop_limit = 1;
  anim = lvl == 2 ? 1 : 0;
  camera_follow_start();
  pc_attack_anim_step(this);
  if (g.flags != 0 || frame <= 0x13 || counters[1] != 0) return;
  for (int i = 0; i < lvl * 4 + 8; ++i) {
    if (Sprite* sp = unit_play_anim(this, static_cast<int>(rng_range(5)) + 4)) {
      const s32 x = bobj_front_x(this, 0x60) + rng_spread_fixed(0x20);
      const s32 dy = static_cast<s32>(rng_range(0x80000));
      const s32 y = pos[1];
      const s32 dz = rng_spread_fixed(0x60);
      sprite_set_pos(sp, x, y - dy, pos[2] + dz);
      sp->vel[0] = rng_between_fixed(-4, 4);
      sp->vel[2] = rng_between_fixed(-4, 4);
      sp->vel[1] = rng_between_fixed(-4, 3);
      sprite_set_fade(sp, 0x80, -2, 4);
      sp->size[2] = static_cast<s16>(rng_range(8));
      sp->roll = static_cast<s16>(rng_range(0x1000));
    }
  }
  counters[1] = 1;
  bsfx_play_at(0x52, this);
  const s32 first = lvl == 1 ? -0x1C7 : (lvl == 2 ? -0x2AA : -0xE3);
  s32 a = 0;
  for (int i = 0; i < lvl * 2 + 3; ++i, a += 0x14000) {
    Shot* s = shot_spawn_callback(cb_fan);
    if (s == nullptr) continue;
    shot_image_reset(s);
    shot_place_at_unit(s, this, 0x60, 0, 0);
    pc_bind_shot(this, s, static_cast<s16>(lvl));
    const s32 ang = a / 0x168 + first;
    s->vel[0] = bobj_facing_fx(this, (bcos(static_cast<u32>(ang)) * 0xC) >> 12);
    s->vel[2] = bsin(static_cast<u32>(ang)) * 0xC;
  }
  pc_camera_frame_target(this, 0x3C);
}

// Move 6: a sound at frame 10, her voice at 13, then from frame 37 every 8 ticks a star
// falling on the target (lvl + 3 of them, scattered more each time).
void ChisatoUnit::move6() {
  BattleGlobals& g = bg();
  const int lvl = pc_move_charge_level(this, killer_index);
  loop_limit = 4;
  anim = 0;
  camera_follow_start();
  if (pc_attack_anim_step(this) != 0 || g.flags != 0) return;
  if (counters[2] == 0 && 9 < frame) {
    counters[2] = 1;
    sound_handle_fade_out(bsfx_play_at(0x42, this), 9, 0xC);
  }
  if (counters[2] == 1 && 0xC < frame) {
    counters[2] = 2;
    play_voice(killer_index * 2);
  }
  if (frame <= 0x24 || counters[1] >= lvl + 3 || g.state != kStateFighting) return;
  counters[3] -= g.frame_ticks;
  if (counters[3] < 1) {
    counters[3] += 8;
    ++counters[1];
    if (target != nullptr) {
      if (Shot* s = shot_spawn_callback(cb_star)) {
        shot_image_reset(s);
        if (counters[1] == 1) {
          shot_place_at_unit(s, target, -0x80, 800, 0);
        } else {
          shot_place_at_unit(s, target, rng_spread(counters[1] << 7) - 0x80, 800, 0);
          s->pos[2] += rng_spread(counters[1] << 6);
        }
        bsfx_play_at(0x38, s);
        pc_bind_shot(this, s, static_cast<s16>(lvl));
        s->vel[0] = bobj_facing_fx(target, 6);
      }
    }
  }
  pc_camera_frame_target(this, 0x3C);
}

// Move 7: one strike, with a sweep outside frames 8-15.
void ChisatoUnit::move7() {
  BattleGlobals& g = bg();
  const int lvl = pc_move_charge_level(this, killer_index);
  loop_limit = 1;
  anim = static_cast<u16>(lvl);
  camera_follow_start();
  if (effects[0] == nullptr && lvl != 0) effects[0] = afterimage_create(this, lvl * 2, kTrailRgb, 1);
  if (g.flags == 0) strike(7 < static_cast<u32>(static_cast<u8>(frame)) - 8, false);
  pc_attack_anim_step(this);
}

}  // namespace

std::unique_ptr<PartyUnit> pc_chisato_create() { return std::make_unique<ChisatoUnit>(); }

}  // namespace ss::game::battle
