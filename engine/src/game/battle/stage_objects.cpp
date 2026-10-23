#include "game/battle/stage_objects.h"

#include <cstring>

#include "game/battle/battle.h"
#include "game/battle/bobj.h"
#include "game/battle/bsfx.h"
#include "game/battle/camera.h"
#include "game/battle/combat.h"
#include "game/battle/enemy_support.h"
#include "game/battle/render.h"
#include "game/battle/shatter.h"
#include "game/battle/shot.h"
#include "game/battle/spell_fx.h"
#include "game/battle/spells.h"
#include "game/battle/sprite.h"
#include "game/battle/stage.h"
#include "game/battle/unit.h"
#include "game/battle/unit_move.h"
#include "game/boot/rng.h"
#include "game/common/screen.h"
#include "runtime/libgpu.h"

namespace ss::game::battle {
namespace {

const s32 kUnitScale[3] = {0x1000, 0x1000, 0x1000};  // 0x8006e34c

u32 rng15() { return static_cast<u32>(rng_next() & 0x7FFF); }  // rng_next15 0x800103b4

// Stage model block n (0x8006ef88[n]; 0 is the stage itself).
const u8* stage_block(int n) {
  const StageData& st = stage();
  return n >= 0 && static_cast<size_t>(n) < st.blocks.size() && !st.blocks[n].empty() ? st.blocks[n].data() : nullptr;
}

// model_draw (0x8007d018) of stage block n at the current matrix.
void draw_block(int n, s32 ot_bias) {
  if (const u8* m = stage_block(n)) model_draw(m, stage().tex_clut[0], stage().model_tpage[0], ot_bias);
}

// The grey the object is drawn in: its own (+0x63 set: +0x64) or neutral.
s32 object_grey(const BObj* o) { return o->colour_mode != 0 ? o->rgb[0] : 0x80; }

StageData::Obstacle& obstacle(int slot) { return stage().obstacles[static_cast<size_t>(slot)]; }

// unit_try_move_scaled 0x800343b4 for a Shot: what blocks moving it by its velocity times
// the frame's ticks.
Unit* try_move_scaled(const Shot* s, u32 flags) {
  const s32 t = bg().frame_ticks;
  const s32 d[3] = {s->vel[0] * t, s->vel[1] * t, s->vel[2] * t};
  return unit_check_move(s, d, flags);
}

// The obstacle slot 23 that a rolling object fills while it rolls: its attack box 0.
void obstacle_from_attack_box(const Shot* s) {
  StageData::Obstacle& o = obstacle(23);
  o.kind = 1;
  for (int k = 0; k < 3; ++k) {
    o.min[k] = s->attack_min[0][k];
    o.max[k] = s->attack_max[0][k];
  }
}

// A rolling object's ground shadow: a square of 'base' plus its height above 'rest'.
void rolling_shadow(Shot* s, s32 rest, s32 base) {
  s32 d = s->pos[1] - rest;
  if (d < 0) d += 3;
  s32 r = (d >> 0xE) + base;
  if (r < 0x10) r = 0x10;
  if (s->p12c == 0) bobj_draw_shadow(s, -r, r, -r, r, 0xFFFFFFFFu);
}

// The units a rolling object runs into on either side: 'damage' HP (the popup shows it; see
// B13), knocked up and away from it, a hit reaction, sfx 0x4c and a spark (sprite 0x11).
int rolling_hits(Shot* s, s32 damage, u8 hit_flag) {
  int hits = 0;
  for (int side = 0; side < 2; ++side) {
    Unit* units[8] = {};
    Vec4 points[8] = {};
    const int n = collider_attack_overlaps(s, units, points, side);
    if (n == 0) continue;
    hits += n;
    s->hit_landed = hit_flag;
    for (int i = 0; i < n; ++i) {
      Unit* u = units[i];
      // ORIGINAL BUG (original_bugs.md B13): each stage block's popup is a constant 50, so
      // the stage 0x24 cart, which takes 100 HP, shows 50:
      //   damage_popup_spawn(u, 0x32, 0, 0);
      // The popup shows the damage taken (still 50 for the stage 0x1a and 0xd boulders).
      // Drawing only: a popup draws no random numbers and takes the same popup slot.
      damage_popup_spawn(u, damage, 0, 0);
      unit_damage_hp(u, damage);
      u->knockback[1] = 0;
      u->knockback[2] = static_cast<s32>(0xFFFE0000);
      u->knockback[0] = u->pos[0] < s->pos[0] ? -static_cast<s32>(rng_range(0x10000)) : static_cast<s32>(rng_range(0x10000));
      u->reaction = 1;
      bsfx_play_at(0x4C, u);
      if (Sprite* sp = sprite_spawn_a(0x11, 1, nullptr)) {
        const s32 p[3] = {points[i].x, points[i].y, points[i].z};
        sprite_set_pos_v(sp, p);
        sp->layer = 2;
      }
    }
  }
  return hits;
}

// Three pieces of rubble (debris sprites 3-5) scattered round x / z at height y, fading;
// 'wide' picks the hanging objects' spread (stage 0x10) over stage 0xe's.
void rubble(const Shot* s, s32 y, bool wide) {
  int delay = 8;
  for (int i = 0; i < 3; ++i, delay += 8) {
    Sprite* sp = sprite_spawn_b(static_cast<int>(rng_range(3)) + 3, 1, nullptr);
    if (sp == nullptr) continue;
    const s32 dx = wide ? rng_between(-0x80000, 0x80000) : rng_spread_fixed(0x80);
    const s32 dz = wide ? rng_between(-0x20000, 0x20000) : rng_spread_fixed(0x20);
    sprite_set_pos(sp, s->pos[0] + dx, y, s->pos[2] + dz);
    sp->freeze_class = 1;
    sp->vel[0] = wide ? rng_between(-0xC000, 0xC000) : rng_spread_fixed(0xC);
    sp->vel[2] = wide ? rng_between(-0x4000, 0x4000) : rng_spread_fixed(4);
    sprite_set_fade(sp, 0x80, -4, 0x1E);
    const s32 scale = wide ? static_cast<s32>(rng_range(0x1000)) + 0x3000 : rng_between(0x1000, 0x4000);
    sp->scale_y = scale;
    sp->scale_x = scale;
    sp->clut_offset = 0xB;
    sp->size[2] = static_cast<s16>(rng_range(static_cast<u32>(delay)));
  }
}

// ---- Block 2 (stage 0x1a) -----------------------------------------------------------------

// 0x8008c1f4 stage1a_update: a boulder (kind 0x1e) whenever none rolls.
void stage1a_update() {
  BattleGlobals& g = bg();
  if (g.flags != 0 || obstacle(23).kind != 0 || g.state != kStateFighting) return;
  Shot* s = shot_spawn(0x1E);
  if (s == nullptr) return;
  const u32 r = rng_range(3);
  if (r == 0) s->pos[0] = 0x96000;
  if (r == 1) s->pos[0] = -0x41A000;
  if (r == 2) s->pos[0] = 0x41A000;
  s->pos[2] = 0x41A000;
  s->vel[2] = -0x8000;
  s->pos[1] = -0x3CA000;
  s->vel[1] = 0x4000;
}

// 0x8008c2d0 kind 0x1e: the rolling boulder (block 1). A unit standing on it while jumping
// gains 100 HP once; units it runs into take 50; it shatters on hitting anything or a wall,
// otherwise it is obstacle 23 while it rolls; past the arena's near edge it falls and fades.
void shot_k1e_stage1a_boulder(Shot* s) {
  BattleGlobals& g = bg();
  s32 rot[3] = {0, 0, s->roll};
  stage_colour_set_grey(object_grey(s));
  lights_build_oriented(rot);
  obj_set_matrix(s->pos, rot, kUnitScale);
  draw_block(1, 7);
  stage_colour_restore();
  const s32 rest = bobj_ground_height(s) - 0x96000;
  rolling_shadow(s, rest, 0x5A);
  if (g.flags != 0) return;
  if (s->p12c == 0 && s->pos[2] < stage().bounds[1]) s->p12c = 1;
  bobj_move(s, s->vel);
  s->vel[2] += g.frame_ticks * -0x40;
  if (rest <= s->pos[1]) {
    const s32 v = s->vel[1] < 0 ? -s->vel[1] : s->vel[1];
    s->vel[1] = -v / 3;
    if (-0x2000 < -v / 3) s->vel[1] = 0;
    s->pos[1] = rest;
  } else {
    s->vel[1] += g.frame_ticks * 0x1000;
  }
  if (shot_fade_out(s) != 0) {
    obstacle(23).kind = 0;
    return;
  }
  s->roll = static_cast<s16>((s->roll + g.frame_ticks * -0x40) & 0xFFF);
  if (s->p12c != 0) return;
  obstacle(23).kind = 0;
  const s32 up[3] = {0, -0x140000, 0};
  Unit* top = unit_check_move(s, up, 3);
  if (s->hit_landed == 0 && top != nullptr && top != kWallBlock && top->jumping != 0) {
    damage_popup_spawn(top, -100, -0x2000, 0);
    unit_damage_hp(top, -100);
    bsfx_play_at(0x13, top);
    s->hit_landed = 1;
  }
  collider_set_attack_box(s, 0x4B, 0x46, 0x46);
  int hits = rolling_hits(s, 0x32, 0);
  s->size[1] = 0x82;
  s->size[0] = 0x41;
  s->size[2] = 0x3C;
  const s32 still[3] = {0, 0, 0};
  if (unit_check_move(s, still, 7) == kWallBlock) {
    ++hits;
    bsfx_play_at_pos(0x23, s->pos);
  }
  if (hits == 0) {
    obstacle_from_attack_box(s);
    return;
  }
  stage_colour_set_grey(0x80);
  obj_set_matrix(s->pos, rot, kUnitScale);
  if (const u8* m = stage_block(1))
    model_shatter(m, s->pos, rot, kUnitScale, stage().tex_clut[0], stage().model_tpage[0], 1, 0, 7, 10, 6);
  stage_colour_restore();
  shot_kill(s);
}

// ---- Block 3 (stage 0x1f) -----------------------------------------------------------------

// 0x8008c1f4 kind 0x1f: block 1 turning about y (tilted 0x155) and bobbing (+0x128 the
// vertical acceleration, flipped past +-0x4000).
void shot_k1f_stage1f_float_model(Shot* s) {
  BattleGlobals& g = bg();
  s32 rot[3] = {0, s->roll, 0x155};
  stage_colour_set_grey(object_grey(s));
  lights_build_oriented(rot);
  obj_set_matrix(s->pos, rot, kUnitScale);
  draw_block(1, 1);
  stage_colour_restore();
  if ((g.flags & 0xC) != 0) return;
  s->roll = static_cast<s16>((s->roll + g.frame_ticks * -4) & 0xFFF);
  bobj_move(s, s->vel);
  s->vel[1] += s->p128 * g.frame_ticks;
  if (0x4000 < s->vel[1]) s->p128 = -0x400;
  if (s->vel[1] < -0x4000) s->p128 = 0x400;
}

// 0x8008c320 kind 0x20: block 2 turning about y.
void shot_k20_stage1f_spin_model(Shot* s) {
  BattleGlobals& g = bg();
  s32 rot[3] = {0, s->roll, 0};
  stage_colour_set_grey(object_grey(s));
  lights_build_oriented(rot);
  obj_set_matrix(s->pos, rot, kUnitScale);
  draw_block(2, 1);
  stage_colour_restore();
  if ((g.flags & 0xC) == 0) s->roll = static_cast<s16>((s->roll + g.frame_ticks * -4) & 0xFFF);
}

// ---- Block 4 (stage 0x10) -----------------------------------------------------------------

// 0x8008c1f4 kind 0x1a: a hanging object (model +0x128, obstacle 23 - +0x12c, kind 2). When
// a spell's finishing hit lands (1 in 2), it drops if nothing is below it, and breaks into
// rubble on the ground.
void shot_k1a_stage10_hanging_object(Shot* s) {
  BattleGlobals& g = bg();
  s32 rot[3] = {0, 0, 0};
  lights_build_oriented(kZeroVector);
  obj_set_matrix(s->pos, rot, kUnitScale);
  draw_block(s->p128, 1);
  s->size[0] = 0x48;
  s->size[1] = 0xDB;
  s->size[2] = 0x47;
  StageData::Obstacle& own = obstacle(23 - s->p12c);
  own.kind = 2;
  own.min[0] = s->pos[0] - s->size[0] * 0x1000;
  own.min[1] = s->pos[1] - s->size[1] * 0x4000;
  own.min[2] = s->pos[2] - s->size[2] * 0x1000;
  own.max[0] = s->pos[0] + s->size[0] * 0x1000;
  own.max[1] = s->pos[1];
  own.max[2] = s->pos[2] + s->size[2] * 0x2000;
  // ORIGINAL BUG (original_bugs.md B11): the shadow is sized from obstacle 23 - +0x128 (the
  // model index, 1 / 1 / 2) instead of 23 - +0x12c, so the first object's shadow takes the
  // second object's box and stretches towards it:
  //   const StageData::Obstacle& box = obstacle(23 - s->p128);
  const StageData::Obstacle& box = own;
  {
    const s32 half = (s->pos[1] >> 12) / 2;
    s32 x0 = ((box.min[0] - s->pos[0]) >> 12) * 3 / 2 - half;
    s32 x1 = ((box.max[0] - s->pos[0]) >> 12) * 3 / 2 + half;
    const s32 z0 = ((box.min[2] - s->pos[2]) >> 12) * 3 / 2 - half;
    s32 z1 = ((box.max[2] - s->pos[2]) >> 12) * 3 / 2 + half;
    if (x1 < x0) x1 = x0;
    if (z1 < z0) z1 = z0;
    bobj_draw_shadow(s, x0, x1, z0, z1, 0xFFFFFFFFu);
  }
  if (g.flags != 0) return;
  s->lift = 0;
  // (The fall checks take obstacle 23 - +0x128 out of the way, the model index: the first
  // object meets its own box and never falls, and the second's kind goes from 2 to 1. Kept:
  // it decides whether the object falls. original_bugs.md B12.)
  StageData::Obstacle& toggled = obstacle(23 - s->p128);
  if (spell_landed_flag() != 0 && rng15() < 0x4000 && s->p134 == 0 && s->pos[1] < 0) {
    spell_landed_flag() = 0;
    const s32 drop = bobj_ground_height(s) - s->pos[1];
    bsfx_play_at(0x39, s);
    s->size[0] = 0x91;
    s->size[1] = 0x1B6;
    s->size[2] = 0x8F;
    const s32 d[3] = {0, drop, 0};
    toggled.kind = 0;
    Unit* below = unit_check_move(s, d, 0xF);
    toggled.kind = 1;
    if (below != nullptr) return;
    s->p134 = 1;
    return;
  }
  if (s->p134 != 1) return;
  toggled.kind = 0;
  Unit* hit = try_move_scaled(s, 0xF);
  toggled.kind = 1;
  if (hit == nullptr) {
    bobj_move(s, s->vel);
    if (s->pos[1] < 0) s->vel[1] += g.frame_ticks * 0x800;
  }
  if (s->pos[1] < 0x140001) return;
  s->pos[1] = 0x140000;
  rubble(s, bobj_ground_height(s), true);
  s->p134 = 2;
}

// ---- Block 5 (stages 0xd / 0x38 / 0x39) ---------------------------------------------------

// 0x8008c1f4 stage0d_update: now and then (0x90 in 0x8000 a frame) a boulder (kind 0x18)
// when none rolls.
void stage0d_update() {
  BattleGlobals& g = bg();
  if (g.flags != 0 || rng15() >= 0x90 || obstacle(23).kind != 0 || g.state != kStateFighting) return;
  Shot* s = shot_spawn(0x18);
  if (s == nullptr) return;
  s->pos[0] = static_cast<s32>(rng_range(0x20000));
  s->pos[1] = -0x7D0000;
  s->pos[2] = 0x1F40000;
  s->vel[0] = rng_between(-0x1000, 0x1000);
  s->vel[2] = -0x10000;
  s->vel[1] = 0x4000;
}

// 0x8008c2ac kind 0x18: the big boulder (block 1): never breaks; every unit it touches
// takes 50; obstacle 23 while it rolls.
void shot_k18_stage0d_boulder(Shot* s) {
  BattleGlobals& g = bg();
  s32 rot[3] = {0, 0, s->roll};
  stage_colour_set_grey(object_grey(s));
  lights_build_oriented(rot);
  obj_set_matrix(s->pos, rot, kUnitScale);
  draw_block(1, 7);
  stage_colour_restore();
  const s32 rest = bobj_ground_height(s) - 0xA2000;
  rolling_shadow(s, rest, 0xA2);
  if (g.flags != 0) return;
  if (s->p12c == 0 && s->pos[2] < stage().bounds[1]) s->p12c = 1;
  bobj_move(s, s->vel);
  s->vel[2] += g.frame_ticks * -0x40;
  if (rest <= s->pos[1]) {
    const s32 v = s->vel[1] < 0 ? -s->vel[1] : s->vel[1];
    s->vel[1] = -v / 3;
    if (-v / 3 < -0x1FFF) bsfx_play_at(0x25, s);
    else s->vel[1] = 0;
    s->pos[1] = rest;
  } else {
    s->vel[1] += g.frame_ticks * 0x1000;
  }
  if (shot_fade_out(s) != 0) {
    obstacle(23).kind = 0;
    return;
  }
  s->roll = static_cast<s16>((s->roll + g.frame_ticks * -0x40) & 0xFFF);
  if (s->p12c != 0) return;
  collider_set_attack_box(s, 0x8C, 0x8C, 0x8C);
  if (rolling_hits(s, 0x32, 1) == 0) s->hit_landed = 0;
  obstacle_from_attack_box(s);
}

// ---- Block 6 (stage 0x17) -----------------------------------------------------------------

// 0x8008c1f4 kind 0x1c: one half (+0x128) of a structure that, after 0x187 ticks, tips over
// (+0x134 turning 2 a tick) and falls, and fades out at 0x1a5.
void shot_k1c_stage17_tilting_half(Shot* s) {
  BattleGlobals& g = bg();
  s32 rot[3] = {s->p134, 0x800, 0};
  shot_fade_out(s);
  stage_colour_set_grey(object_grey(s));
  lights_build_oriented(rot);
  obj_set_matrix(s->pos, rot, kUnitScale);
  draw_block(1 + s->p128, 1);
  stage_colour_restore();
  if ((g.flags & 0xBF) != 0) return;
  bobj_move(s, s->vel);
  if (0x186 < s->p130) {
    s->p134 += s->p128 == 0 ? g.frame_ticks * -2 : g.frame_ticks * 2;
    s->vel[1] += g.frame_ticks * 0x1000;
  }
  s->p130 += g.frame_ticks;
  if (0x1A4 < s->p130 && s->p12c == 0) s->p12c = 1;
}

// ---- Block 7 (stages 0x15 / 0x28) ---------------------------------------------------------

// 0x8008c1f4 stage15_update: drops (kind 0x1b), 1 in 8 frames on stage 0x15, 1 in 4 on 0x28.
void stage15_update() {
  const u32 chance = bg().stage_id == 0x15 ? 0x1000 : 0x2000;
  if (rng15() >= chance) return;
  Shot* s = shot_spawn(0x1B);
  if (s == nullptr) return;
  s->pos[0] = rng_between(-0x7D0000, 0x7D0000);
  s->pos[1] = -0xBB8000;
  s->pos[2] = rng_between(-0xBB8000, 0x7D0000);
}

// 0x8008c280 kind 0x1b: a falling drop drawn from the stage sprites (anim 0); gone at the
// ground.
void shot_k1b_stage15_drop(Shot* s) {
  BattleGlobals& g = bg();
  if (g.sprite_set_c) bobj_share_sprite(g.sprite_set_c.get(), s);
  s->anim = 0;
  s->layer = 1;
  bobj_project(s);
  bobj_anim_draw(s, g.flags);
  if ((g.flags & 0xC) != 0) return;
  s->vel[1] += 0x800;
  if (0xEFFF < s->vel[1]) s->vel[1] = 0xF000;
  bobj_move(s, s->vel);
  s->size[0] = s->size[1] = s->size[2] = 8;
  if (-1 < s->pos[1]) shot_kill(s);
}

// ---- Block 8 (stage 0x24) -----------------------------------------------------------------

// 0x8008c1f4 stage24_update: now and then (0x90 in 0x8000 a frame) the cart (kind 0x23)
// when none runs.
void stage24_update() {
  BattleGlobals& g = bg();
  if (g.flags != 0 || rng15() >= 0x90 || obstacle(23).kind != 0 || g.state != kStateFighting) return;
  Shot* s = shot_spawn(0x23);
  if (s == nullptr) return;
  s->pos[2] = 0x1109000;
  s->pos[0] = 0x12C000;
  s->pos[1] = -0x1E000;
  s->vel[2] = -0x10000;
}

// 0x8008c290 kind 0x22: a breakable object (block 1); when a spell's finishing hit lands
// (1 in 2) it shatters.
void shot_k22_stage24_breakable(Shot* s) {
  BattleGlobals& g = bg();
  s32 rot[3] = {0, 0, 0};
  obj_set_matrix(s->pos, rot, kUnitScale);
  draw_block(1, 7);
  rolling_shadow(s, bobj_ground_height(s) - 0x96000, 0x5A);
  if (g.flags != 0 || spell_landed_flag() == 0 || rng15() >= 0x4000) return;
  spell_landed_flag() = 0;
  bsfx_play_at(0x50, s);
  obj_set_matrix(s->pos, rot, kUnitScale);
  if (const u8* m = stage_block(1))
    model_shatter(m, s->pos, rot, kUnitScale, stage().tex_clut[0], stage().model_tpage[0], 1, 0, 7, 10, 6);
  const s32 spread[3] = {0x20000, 0, 0x20000};
  effect_debris_burst(s->pos, spread, 3, 0, 1, 0x1000, 0);
  shot_kill(s);
}

// 0x8008c47c kind 0x23: the runaway cart: body (block 2) on four spinning wheels (block 3);
// units it runs into lose 100 HP; obstacle 23 while it runs; past the near edge it fades.
void shot_k23_stage24_cart(Shot* s) {
  BattleGlobals& g = bg();
  s32 rot[3] = {0, 0, s->roll};
  stage_colour_set_grey(object_grey(s));
  lights_build_oriented(rot);
  for (int i = 0; i < 4; ++i) {
    s32 wheel[3] = {s->pos[0] + ((i & 1) == 0 ? 0x8F000 : -0x8F000), s->pos[1],
                    s->pos[2] + ((i >> 1) == 0 ? -0x64000 : 0x64000)};
    obj_set_matrix(wheel, rot, kUnitScale);
    draw_block(3, 7);
  }
  s32 flat[3] = {0, 0, 0};
  obj_set_matrix(s->pos, flat, kUnitScale);
  draw_block(2, 7);
  stage_colour_restore();
  rolling_shadow(s, bobj_ground_height(s) - 0xF6000, 200);
  if (g.flags != 0) return;
  if (s->p12c == 0 && s->pos[2] < stage().bounds[1] - 0x140000) s->p12c = 1;
  bobj_move(s, s->vel);
  if (shot_fade_out(s) != 0) {
    obstacle(23).kind = 0;
    return;
  }
  s->roll = static_cast<s16>((s->roll + g.frame_ticks * -0x80) & 0xFFF);
  if (s->p12c != 0) return;
  // Attack box 0 from the cart's height up to its wheels (0x80034e70).
  s->box_mask |= 4;
  s->attack_min[0][0] = s->pos[0] - 0x8E * 0x1000;
  s->attack_min[0][1] = s->pos[1] - 0xF6 * 0x1000;
  s->attack_min[0][2] = s->pos[2] - 200 * 0x1000;
  s->attack_max[0][2] = s->pos[2] + 200 * 0x1000;
  s->attack_max[0][1] = s->pos[1];
  s->attack_max[0][0] = s->pos[0] + 0x8E * 0x1000;
  // (The popup says 50, the damage is 100: original_bugs.md B13.)
  if (rolling_hits(s, 100, 1) == 0) s->hit_landed = 0;
  obstacle_from_attack_box(s);
}

// ---- Block 9 (stage 0x29) -----------------------------------------------------------------

// 0x8008c1f4 stage29_update: a snow flake (kind 0x28) 1 frame in 8.
void stage29_update() {
  if (rng15() >= 0x1000) return;
  Shot* s = shot_spawn(0x28);
  if (s == nullptr) return;
  s->pos[0] = rng_between(-0xBB8000, 0xBB8000);
  s->pos[1] = -0x9C4000;
  s->pos[2] = rng_between(-0x6D6000, 0x708000);
}

// 0x8008c270 kind 0x28: a falling flake: one grey textured quad (5 x 5 texels at (0x28,
// 0xe0)) turned by +0x58 against the camera roll, sized by its scale, the zoom and depth.
void shot_k28_stage29_flake(Shot* s) {
  BattleGlobals& g = bg();
  bobj_project(s);
  if (7 < s->depth && static_cast<u16>(s->screen_x + 0x2F) < 0x197 && static_cast<u16>(s->screen_y + 0x2F) < 0x15B) {
    const ViewState& v = view();
    const u32 inv = 0x10000u / s->depth;
    const s32 sx = ((static_cast<s32>(inv * static_cast<u32>(s->scale_x)) >> 12) * v.zoom) >> 10;
    const s32 sy = ((static_cast<s32>(inv * static_cast<u32>(s->scale_y)) >> 12) * v.zoom) >> 10;
    const s32 a = s->roll - v.camera_roll;
    const s32 c = bcos(static_cast<u32>(a)), sn = bsin(static_cast<u32>(a));
    s->final_scale_x = sx;
    s->final_scale_y = sy;
    RenderContext* pool = s->pool != nullptr ? s->pool : g.prim_pool_a;
    if (auto* p = screen_alloc_prims<psyq::POLY_FT4>(pool->arena, 1)) {
      const s32 c2 = c * -2, c3 = c * 3;
      auto off = [](s32 k, s32 size) { return static_cast<s16>(static_cast<u32>((k >> 8) * size) >> 16); };
      psyq::setPolyFT4(p);
      p->r0 = p->g0 = p->b0 = 0x80;
      p->x0 = static_cast<s16>(s->screen_x + off(c2 + sn * -2, sx));
      p->y0 = static_cast<s16>(s->screen_y + off(sn * 2 + c2, sy));
      p->x1 = static_cast<s16>(s->screen_x + off(c3 + sn * -2, sx));
      p->y1 = static_cast<s16>(s->screen_y + off(sn * -3 + c2, sy));
      p->x2 = static_cast<s16>(s->screen_x + off(c2 + sn * 3, sx));
      p->y2 = static_cast<s16>(s->screen_y + off(sn * 2 + c3, sy));
      p->x3 = static_cast<s16>(s->screen_x + off(c3 + sn * 3, sx));
      p->y3 = static_cast<s16>(s->screen_y + off(sn * -3 + c3, sy));
      p->u0 = 0x28, p->v0 = 0xE0;
      p->u1 = 0x2D, p->v1 = 0xE0;
      p->u2 = 0x28, p->v2 = 0xE5;
      p->u3 = 0x2D, p->v3 = 0xE5;
      p->clut = 0x7804;
      p->tpage = 5;
      psyq::addPrim(&pool->ot_head[0x618 - s->depth], p);
      if (pool == g.prim_pool_a) pool_a_bytes_used() += 0x28;
    }
  }
  if ((g.flags & 0xC) != 0) return;
  s->vel[1] += 0x800;
  if (0xEFFF < s->vel[1]) s->vel[1] = 0xF000;
  bobj_move(s, s->vel);
  if (-1 < s->pos[1]) shot_kill(s);
}

// ---- Block 10 (stage 0x33) ----------------------------------------------------------------

// 0x8008c1f4 / 0x8008c2f8 kinds 0x2a / 0x2b: blocks 1 / 2 turning about y (+4 / -4 a tick),
// bobbing with +0x128 as kind 0x1f does (battle_run leaves it 0).
void shot_k2a_stage33_model(Shot* s, int block, s32 turn) {
  BattleGlobals& g = bg();
  s32 rot[3] = {0, s->roll, 0};
  lights_build_oriented(rot);
  obj_set_matrix(s->pos, rot, kUnitScale);
  draw_block(block, 1);
  if ((g.flags & 0xC) != 0) return;
  s->roll = static_cast<s16>((s->roll + g.frame_ticks * turn) & 0xFFF);
  bobj_move(s, s->vel);
  s->vel[1] += s->p128 * g.frame_ticks;
  if (0x4000 < s->vel[1]) s->p128 = -0x400;
  if (s->vel[1] < -0x4000) s->p128 = 0x400;
}

// ---- Stage 0xe (no block: the #2129 image's function at 0x8008c1f4) -------------------------

// 0x8008c1f4 kind 0x19: one of three stacked pieces (model 3 - +0x128, obstacle 23 - +0x128,
// kind 2) that topple when a spell's finishing hit lands: +0x12c the tilt (to 0x400), +0x138
// its speed, +0x134 1 falling / 2 down, point[2] (+0x120) the drawing's sideways shift.
void shot_k19_stage0e_object(Shot* s) {
  BattleGlobals& g = bg();
  s32 rot[3] = {-s->p12c, 0, 0};
  s32 at[3] = {s->pos[0] - 0xA5000 - s->point[2], s->pos[1], s->pos[2]};
  lights_build_oriented(rot);
  obj_set_matrix(at, rot, kUnitScale);
  draw_block(3 - s->p128, 4);
  const s32 sn = bsin(static_cast<u32>(s->p12c));
  s32 h = -(sn / 0xD) + 0x282;
  s->size[0] = static_cast<s16>((sn / 0x1E + 0xAF) / 2);
  if (h < 0) h = -(sn / 0xD) + 0x285;
  s->size[1] = static_cast<s16>(h >> 2);
  s->size[2] = 0x50;
  s->lift = 0;
  StageData::Obstacle& own = obstacle(23 - s->p128);
  own.kind = 2;
  own.min[0] = s->pos[0] - s->size[0] * 0x2000;
  own.min[1] = s->pos[1] - s->size[1] * 0x4000;
  own.min[2] = s->pos[2] - s->size[2] * 0x2000;
  own.max[0] = s->pos[0] + s->size[0] * 0x2000;
  own.max[1] = s->pos[1];
  own.max[2] = s->pos[2] + s->size[2] * 0x2000;
  {
    const s32 q = (s->pos[1] >> 12) / 4;
    bobj_draw_shadow(s, ((own.min[0] - s->pos[0]) >> 12) * 3 / 2 - q, ((own.max[0] - s->pos[0]) >> 12) * 3 / 2 + q,
                     ((own.min[2] - s->pos[2]) >> 12) * 3 / 2 - q, ((own.max[2] - s->pos[2]) >> 12) * 3 / 2 + q,
                     0xFFFFFFFFu);
  }
  if (g.flags != 0) return;
  if (spell_landed_flag() != 0 && s->p134 == 0) {
    s->p134 = 1;
    s->p138 = static_cast<s32>(rng_range(7));
    if (s->p128 == 1) s->vel[0] = -0x16000;
    else if (s->p128 == 2) s->vel[0] = -0x20000;
    if (s->p128 != 0) s->vel[2] = rng_spread_fixed(0xC);
  }
  if (s->p134 == 0) return;
  s->point[2] = (s->p12c * -0x2000) / 5;
  own.kind = 0;
  Unit* hit = try_move_scaled(s, 0xF);
  own.kind = 2;
  if (hit == nullptr) {
    bobj_move(s, s->vel);
    if (s->pos[1] < 0) s->vel[1] += g.frame_ticks * 0xC00;
  } else if (hit != kWallBlock) {
    s->vel[0] = 0;
  }
  if (hit == nullptr || hit == kWallBlock) {
    s->p12c += s->p138 * g.frame_ticks;
    if (0x400 < s->p12c) s->p12c = 0x400;
    if (s->p138 < 10 || s->p134 == 2) s->p138 += g.frame_ticks;
  }
  if (0 < s->pos[1] || hit != nullptr) {
    s32 vy = s->vel[1];
    if (0 < vy) {
      if (hit == nullptr) {
        rubble(s, s->pos[1], false);
        bsfx_play_at(0x53, s);
        vy = s->vel[1];
      }
      if (vy < 0x3000 && -1 < s->pos[1]) {
        s->vel[0] = s->vel[1] = s->vel[2] = 0;
        s->pos[1] = 0;
        s->p134 = 2;
      } else {
        s->vel[0] /= 2;
        s->vel[2] /= 2;
        s->vel[1] = -vy / 3;
        s->p138 /= 2;
      }
    }
  }
  if (0 < s->pos[1]) s->pos[1] = 0;
}

}  // namespace

void stage_objects_create() {
  BattleGlobals& g = bg();
  switch (g.stage_id) {
    case 0xE:
      for (int i = 0; i < 3; ++i) {
        Shot* s = shot_spawn(0x19);
        if (s == nullptr) break;
        s->pos[0] = 0x627000;
        s->p128 = i;
        s->pos[2] = static_cast<s32>(0xFFF66000);
        s->pos[1] = battle_s32(0x8007999C + 4 * static_cast<u32>(i)) << 12;
      }
      break;
    case 0x10:
      for (int i = 0; i < 3; ++i) {
        Shot* s = shot_spawn(0x1A);
        if (s == nullptr) break;
        s->pos[0] = battle_s32(0x800799A8 + 4 * static_cast<u32>(i)) << 12;
        s->pos[1] = battle_s32(0x800799B4 + 4 * static_cast<u32>(i)) << 12;
        s->pos[2] = battle_s32(0x800799C0 + 4 * static_cast<u32>(i)) << 12;
        s->p128 = 1 < i ? 2 : 1;
        s->p12c = i;
      }
      break;
    case 0x17: {
      s32 x = -0x4B0000;
      for (int i = 0; i < 2; ++i, x += 0x960000) {
        if (Sprite* sp = sprite_spawn_c(1, 0, nullptr)) {
          sprite_set_pos(sp, x, static_cast<s32>(0xFFC18000), 0x5DC000);
          sp->freeze_class = 2;
        }
      }
      for (int i = 0; i < 2; ++i) {
        Shot* s = shot_spawn(0x1C);
        if (s == nullptr) break;
        s->p128 = i;
        s->pos[1] = static_cast<s32>(0xFFC18000);
        s->pos[2] = 0x12C000;
        s->pos[0] = battle_s32(0x800799CC + 4 * static_cast<u32>(i)) << 12;
      }
      break;
    }
    case 0x1F:
      if (Shot* s = shot_spawn(0x1F)) {
        s->pos[1] = static_cast<s32>(0xFFB50000);
        s->pos[0] = 0;
        s->pos[2] = 0;
        s->p128 = 0x400;
      }
      if (Shot* s = shot_spawn(0x20)) {
        s->pos[0] = 0;
        s->pos[1] = static_cast<s32>(0xFFE3E000);
        s->pos[2] = 0;
      }
      break;
    case 0x24:
      for (s32 x : {static_cast<s32>(0xFFF6A000), static_cast<s32>(0xFFE3E000)}) {
        if (Shot* s = shot_spawn(0x22)) {
          s->pos[0] = x;
          s->pos[1] = 0;
          s->pos[2] = 0x439000;
        }
      }
      break;
    case 0x33:
      for (int kind : {0x2A, 0x2B}) {
        if (Shot* s = shot_spawn(kind)) {
          s->pos[0] = 0;
          s->pos[1] = static_cast<s32>(0xFF63C000);
          s->pos[2] = 0x1518000;
        }
      }
      break;
    default:
      break;
  }
}

void stage_code_update() {
  switch (bg().stage_id) {
    case 0xD: case 0x38: case 0x39: stage0d_update(); break;
    case 0x15: case 0x28: stage15_update(); break;
    case 0x1A: stage1a_update(); break;
    case 0x24: stage24_update(); break;
    case 0x29: stage29_update(); break;
    default: break;
  }
}

bool stage_shot_update(Shot* s) {
  switch (s->kind) {
    case 0x18: shot_k18_stage0d_boulder(s); return true;
    case 0x19: shot_k19_stage0e_object(s); return true;
    case 0x1A: shot_k1a_stage10_hanging_object(s); return true;
    case 0x1B: shot_k1b_stage15_drop(s); return true;
    case 0x1C: shot_k1c_stage17_tilting_half(s); return true;
    case 0x1E: shot_k1e_stage1a_boulder(s); return true;
    case 0x1F: shot_k1f_stage1f_float_model(s); return true;
    case 0x20: shot_k20_stage1f_spin_model(s); return true;
    case 0x22: shot_k22_stage24_breakable(s); return true;
    case 0x23: shot_k23_stage24_cart(s); return true;
    case 0x28: shot_k28_stage29_flake(s); return true;
    case 0x2A: shot_k2a_stage33_model(s, 1, 4); return true;
    case 0x2B: shot_k2a_stage33_model(s, 2, -4); return true;
    default: return false;
  }
}

}  // namespace ss::game::battle
