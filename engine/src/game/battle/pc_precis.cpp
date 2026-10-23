// Precis's party-character module (#2155, slot-0 link 0x801ee000): the PartyUnit subclass
// (0x364 bytes, vtable 0x801f1754: slots 31, 33, 35, 36, 37, 46 and ten of her own, 47-56,
// which are the moves), her 10 killer moves (states 0x10-0x19; the table 0x801f191c sends
// move i to vtable slot 47 + i) and the Shot callbacks they spawn. Move ids (record +0xcc
// byte - 1); names from what the code does: 0 a dash punch, 1 a volley of shells from the
// arm, 2 a punch thrown at the target (level 1 on: a ground wave and a second, spinning
// punch), 3 a cannon whose shot opens into a vortex, 4 a rush behind a vortex hitting
// every 16 ticks, 5 a spin, a dash to the target and a strike, 6 a projectile at the
// target, 7 a leap at the target ending in a dive, 8 the arm punching out on a stretching
// beam, 9 a ball thrown in an arc that bursts into a growing sphere. In state 10 (the
// ranged attack) she fires a projectile instead of the base jump attack.
#include <cstdlib>

#include "game/battle/battle.h"
#include "game/battle/bobj.h"
#include "game/battle/bsfx.h"
#include "game/battle/camera.h"
#include "game/battle/combat.h"
#include "game/battle/enemy_support.h"
#include "game/battle/fx_draw.h"
#include "game/battle/killer.h"
#include "game/battle/particle.h"
#include "game/battle/pc_module_util.h"
#include "game/battle/pc_modules.h"
#include "game/battle/render.h"
#include "game/battle/shot.h"
#include "game/battle/sprite.h"
#include "game/battle/stage.h"
#include "game/battle/unit_move.h"
#include "game/boot/rng.h"
#include "game/title/new_game.h"

namespace ss::game::battle {
namespace {

constexpr u32 kTrailRgb = 0x208030;                      // every afterimage of the module
const s32 kZero[3] = {0, 0, 0};                          // 0x8006e33c
const s32 kUnitScale[3] = {0x1000, 0x1000, 0x1000};      // 0x8006e34c

void face_vel(Shot* s) { s->facing[0] = s->vel[0], s->facing[1] = s->vel[1], s->facing[2] = s->vel[2]; }

// One turn step of a spinning Shot: +0x58 += a / 360 (a 20.12 angle a tick), 12 bits.
void spin(Shot* s, s32 a) { s->roll = static_cast<s16>((s->roll + a / 360) & 0xFFF); }

// ---- Shot callbacks ------------------------------------------------------------------------

// 0x801f01d4 (move 1): a shell from the arm, growing, with an afterimage; ends on a hit
// (a hit sprite of the next animation, as large) or after 24 ticks.
void cb_shell(Shot* s) {
  BattleGlobals& g = bg();
  s->size[2] = 0x30;
  s->anim = 2;
  if (shot_image(s) == nullptr) shot_image_set(s, afterimage_create(s, 6, kTrailRgb, 1));
  shot_draw_and_box(s, 4, 0xFFFFFFFF);
  if (g.flags != 0) return;
  if (shot_fade_out(s) == 0) {
    if (s->p12c == 0 && 0x18 < s->p134) s->p12c = 1;
    bobj_move(s, s->vel);
    s->p134 += g.frame_ticks;
    face_vel(s);
    s->scale_x += g.frame_ticks * 0x18;
    s->scale_y += g.frame_ticks * 0x30;
    if (s->hit_landed != 0 || shot_hit_melee(s, 0x80, 0) == 0) return;
    if (Sprite* sp = shot_spawn_sprite(s, s->anim + 1)) {
      sprite_set_pos_v(sp, s->pos);
      sprite_set_fade(sp, 0x80, -4, 8);
      sp->scale_vel[1] = 0x80;
      sp->scale_vel[0] = 0x80;
      sp->scale_x = s->scale_x;
      sp->scale_y = s->scale_y;
    }
  }
  shot_image_end(s);
  shot_kill(s);
}

// 0x801f03b0 (move 2): the thrown punch (anim 2, or 3 spinning); an afterimage from level
// 1; ends on a hit (a spray sprite) or after 20 ticks.
void cb_fist(Shot* s) {
  BattleGlobals& g = bg();
  s->size[2] = s->owner->size[2];
  shot_draw_and_box(s, 4, 0xFFFFFFFF);
  if (g.flags != 0) return;
  if (shot_image(s) == nullptr && s->power != 0) shot_image_set(s, afterimage_create(s, 2, kTrailRgb, 1));
  if (shot_fade_out(s) != 0) {
    shot_image_end(s);
    return;
  }
  if (s->anim == 3) spin(s, g.frame_ticks * 0x14000);
  if (s->p12c == 0 && 0x14 < s->p134) s->p12c = 1;
  bobj_move(s, s->vel);
  s->p134 += g.frame_ticks;
  face_vel(s);
  if (s->hit_landed != 0 || shot_hit_melee(s, 0x100, 0) == 0) return;
  if (Sprite* sp = shot_spawn_sprite_at(s, 7, 0x28)) {
    sprite_set_pos_v(sp, s->pos);
    sp->vel[0] = rng_spread_fixed(6);
    sp->vel[1] = rng_between_fixed(-8, 0);
    sp->vel[2] = rng_between_fixed(-0x18, -0xC);
    sp->roll_speed = 0x71;
  }
  shot_image_end(s);
  shot_kill(s);
}

// 0x801f05cc (move 2, level 1 on): a wave along the ground (no hit), 18 ticks.
void cb_wave(Shot* s) {
  BattleGlobals& g = bg();
  s->layer = 2;
  s->anim = 0xB < s->p134 ? 5 : 4;
  shot_draw_and_box(s, 4, 0xFFFFFFFF);
  if (g.flags != 0) return;
  shot_fade_out(s);
  if (s->p134 < 0xC) bobj_move(s, s->vel);
  if (s->p134 < 0x12) {
    face_vel(s);
    s->p134 += g.frame_ticks;
  } else {
    shot_kill(s);
  }
}

// 0x801f08e0 (move 3): the cannon's vortex (a second, narrower one from level 1) flying
// on; its attack box hits once (knock-back away from the way it flies, a shake, debris),
// then it stops and fades. After 48 ticks it fades anyway.
void cb_vortex(Shot* s) {
  BattleGlobals& g = bg();
  s->size[2] = 0x40;
  if (shot_fade_out(s) != 0) {
    shot_kill(s);
    return;
  }
  const s32 width = s->power * 0x80 + 0x140;
  VortexParams vp;
  vp.radius = 0x4000, vp.base = -0xC0, vp.width = width, vp.step = 0x40;
  vp.rings = 8, vp.segments = 6;
  vp.colour_a = scale_colour(0x60FF40, 0x100 - s->p130);
  vp.colour_b = scale_colour(0x204030, 0x100 - s->p130);
  vp.phase_span = 2, vp.phase = 0x1FFF - ((s->p128 * 0x60) & 0x1FFF);
  vp.twist_freq = 1, vp.twist_amp = 0x1000, vp.blend = 2, vp.ot_bias = 5;
  const s32 back[3] = {-s->vel[0], -s->vel[1], s->vel[2]};
  s32 orient[3];
  vec_to_angles(orient, back);
  obj_set_matrix(s->pos, orient, kUnitScale);
  draw_vortex(vp);
  if (s->power != 0) {
    VortexParams in;
    in.radius = 0x2000, in.base = -0x100, in.width = 0x280, in.step = 0x30;
    in.rings = 6, in.segments = 6;
    in.colour_a = scale_colour(0x8040FF, 0x100 - s->p130);
    in.colour_b = scale_colour(0x206060, 0x100 - s->p130);
    in.phase_span = 2, in.phase = 0x1FFF - (s->p128 & 0x3F) * 0x80;
    in.twist_freq = 1, in.twist_amp = 0x800, in.blend = 2, in.ot_bias = 5;
    draw_vortex(in);
  }
  if (g.flags != 0) return;
  const s32 ticks = g.frame_ticks;
  s->p128 += ticks;
  if (0x30 < s->p128) {
    s->p130 += ticks * 4;
    if (0xFF < s->p130) {
      shot_kill(s);
      return;
    }
  }
  if (s->hit_landed != 0) return;
  bobj_move(s, s->vel);
  collider_set_attack_box(s, 0xE0, width >> 1, s->size[2]);
  Unit* found[8] = {};
  Vec4 pts[8] = {};
  const int n = collider_attack_overlaps(s, found, pts, 0);
  if (n == 0) return;
  s->hit_landed = 1;
  unit_attack_targets(s->owner, n, found, pts, s->power * 0x80 + 0x180, s->vel[0] < 0 ? 0x800 : 0, 0x80);
  s->owner->attack_recovery = 0;
  if (bobj_on_screen(s)) screen_shake2(4, 4, 4, 6, 0xE);
  const s32 spread[3] = {0x80000, 0x80000, 0x80000};
  effect_debris_burst(&pts[0].x, spread, 3, 2, 1, 0x1800, 6);
  bsfx_play_at_pos(0x50, s->pos);
  s->p128 = 0x30;
}

// 0x801f06b4 (move 3): the cannon. It flies 16 ticks (anim 4), then plays anim 5 once;
// past its frame 17 it fires the vortex (a shake when on screen). Once its owner leaves
// the move it fades (semi-transparent).
void cb_cannon(Shot* s) {
  BattleGlobals& g = bg();
  s->layer = 2;
  s->anim = s->p128 != 0 ? 5 : 4;
  if (static_cast<s16>(shot_draw_and_box(s, 4, 0xFFFFFFFF)) == -1) {
    shot_kill(s);
    return;
  }
  if (g.flags != 0) return;
  if (s->p12c != 0) {
    s->blend = 2;
  } else if (s->owner->action != 0x13) {
    s->p12c = 1;
  }
  shot_fade_out(s);
  if (s->p128 == 0) {
    s->loop_limit = 0;
    if (s->p130 < 0x10) {
      bobj_move(s, s->vel);
    } else {
      s->p128 = 1;
      bobj_anim_reset(s);
    }
  } else if (s->p128 == 1) {
    s->loop_limit = 1;
    if (s->p134 == 0 && 0x11 < s->frame) {
      s->p134 = 1;
      if (Shot* v = shot_spawn_callback(cb_vortex)) {
        shot_place_at_unit(v, s, 0xA0, 0xA0, 0);
        v->vel[0] = bobj_facing_fx(s, 0x20);
        bobj_share_sprite(s, v);
        v->power = s->power;
        v->owner = s->owner;
      }
      bsfx_play_at(0x23, s);
      sound_handle_fade_out(static_cast<u32>(bsfx_play_at(0x2D, s)), 0x24, 0);
      if (bobj_on_screen(s)) screen_shake(4, 2, 10);
    }
  }
  face_vel(s);
  s->p130 += g.frame_ticks;
}

// 0x801f0d08 (move 8): the arm. It rises from the ground (anims 1, 2, and 3 from level 1)
// and punches (anim 4, hitting again each time the attack boxes come back); when its
// animation has looped once, or its owner has left the move, it fades, and while the move
// runs that ends it (owner +0x17c). Once punched out it is knocked back up and falls (dust
// where it lands).
void cb_arm(Shot* s) {
  BattleGlobals& g = bg();
  s->size[2] = 0x20;
  s->layer = 3;
  if (s->p12c == 0 && s->owner->action != 0x18) s->p12c = 1;
  if (shot_image(s) == nullptr && s->power != 0) shot_image_set(s, afterimage_create(s, 3, kTrailRgb, 1));
  if (Afterimage* img = shot_image(s)) img->next_kind = 1;
  bool punch = false;
  switch (s->p128) {
    case 0:
      s->pos[1] = bobj_ground_height(s);
      s->anim = 1;
      s->loop_limit = 1;
      if (static_cast<s16>(shot_draw_and_box(s, 4, 0xFFFFFFFF)) != -1) break;
      s->p128 = 1;
      bobj_anim_reset(s);
      [[fallthrough]];
    case 1:
      s->anim = 2;
      s->loop_limit = 1;
      if (static_cast<s16>(shot_draw_and_box(s, 4, 0xFFFFFFFF)) != -1) break;
      s->p128 = s->power != 0 ? 2 : 3;
      bobj_anim_reset(s);
      if (s->p128 == 3) {
        punch = true;
        break;
      }
      [[fallthrough]];
    case 2:
      s->anim = 3;
      s->loop_limit = 1;
      if (static_cast<s16>(shot_draw_and_box(s, 4, 0xFFFFFFFF)) != -1) break;
      s->p128 = 3;
      bobj_anim_reset(s);
      punch = true;
      break;
    case 3:
      punch = true;
      break;
    default:
      break;
  }
  if (punch) {
    if (s->p134 == 0) {
      s->vel[1] = static_cast<s32>(0xFFFF0000);
      s->vel[0] = s->facing[0] < 0 ? static_cast<s32>(0xFFFFA000) : 0x6000;
      s->p134 = 1;
    }
    s->anim = 4;
    if (s->loops != 0 && s->p12c == 0) s->p12c = 1;
    shot_draw_and_box(s, 4, 0xFFFFFFFF);
    if (g.flags != 0) return;
    if (s->p134 == 1) {
      bobj_move(s, s->vel);
      const s32 ground = bobj_ground_height(s);
      if (ground <= s->pos[1]) {
        s->pos[1] = ground;
        for (int i = 0; i < 8; ++i) effect_dust_puff(s->pos, 4);
        s->p134 = 2;
      }
      s->vel[1] += g.frame_ticks * 0x1000;
    }
  }
  if (g.flags != 0) return;
  if (shot_fade_out(s) == 0) {
    if (s->hit_landed == 0) {
      shot_hit_melee(s, 0x100, 0);
    } else if ((s->box_mask & 0xC) == 0) {
      s->hit_landed = 0;
    }
    return;
  }
  shot_image_end(s);
  if (s->owner->action == 0x18) static_cast<PartyUnit*>(s->owner)->counters[2] = 1;
}

// 0x801f13a0 (move 9): the sphere the ball bursts into: a lit additive hemisphere growing
// 10 a tick from radius 10 for 70 ticks (fading in its last 16); every 12 ticks it hits
// each enemy whose distance is within 0x50 of its radius (power 0x180 + 0x80 a level).
void cb_sphere(Shot* s) {
  BattleGlobals& g = bg();
  stage_colour_set_grey(0x80);
  lights_build_oriented(kZero);
  gte_origin_at(s->pos);
  const s32 level = 0x3F < s->p134 ? 0x100 : s->p134 << 2;
  draw_sphere(10, 7, s->p130, 0xC0FF40, level, 1, 2, 0x1000);
  stage_colour_restore();
  if ((g.flags & 0xBF) != 0) return;
  const s32 ticks = g.frame_ticks;
  s->p134 -= ticks;
  s->p130 += ticks * 10;
  if (s->p134 < 1) {
    shot_kill(s);
    return;
  }
  s->p12c -= ticks;
  if (s->p12c >= 0) return;
  s->p12c += 0xC;
  Unit* found[8] = {};
  Vec4 pts[8] = {};
  int n = 0;
  for (EnemyUnit* e : g.enemy_units) {
    Unit* u = e;
    if (unit_is_down(u)) continue;
    const s32 d = vec_distance_fx(u->pos, s->pos);
    if ((s->p130 - 0x50) * 0x1000 < d && d < (s->p130 + 0x50) * 0x1000) {
      found[n] = u;
      pts[n].x = u->pos[0];
      pts[n].y = u->pos[1] - u->size[1] * 0x2000;
      pts[n].z = u->pos[2];
      ++n;
    }
  }
  unit_attack_targets(s->owner, n, found, pts, s->power * 0x80 + 0x180, 9999, 0);
}

// 0x801f1084 (move 9): the ball, thrown in a spinning arc; where it lands: sfx 0x50, two
// spark bursts, the sphere and a flash.
void cb_ball(Shot* s) {
  BattleGlobals& g = bg();
  s->anim = 1;
  s->layer = 1;
  if (shot_image(s) == nullptr && s->power != 0) shot_image_set(s, afterimage_create(s, 3, kTrailRgb, 1));
  if (Afterimage* img = shot_image(s)) img->next_kind = 1;
  if (static_cast<s16>(shot_draw_and_box(s, 4, 0xFFFFFFFF)) != -1) {
    if (g.flags != 0) return;
    if (shot_fade_out(s) != 0) {
      shot_image_end(s);
      return;
    }
    bobj_move(s, s->vel);
    face_vel(s);
    s->vel[1] += g.frame_ticks * 0x1000;
    spin(s, s->facing[0] < 0 ? g.frame_ticks * 0xA000 : g.frame_ticks * -0xA000);
    const s32 ground = bobj_ground_height(s);
    if (s->pos[1] <= ground) return;
    bsfx_play_at(0x50, s);
    s->pos[1] = ground;
    if (Shot* r = effect_spawn_sparks_at(s->pos, 0xC, 0x18, 100, 0xE, 0x46, 0, 2, 0x5A, 0)) {
      r->anim = 3;
      bobj_share_sprite(s, r);
    }
    if (Shot* r = effect_spawn_sparks_at(s->pos, 0xC, 0x20, 0x8C, 10, 0x46, 0, 2, 0x3C, 0)) {
      r->anim = 3;
      bobj_share_sprite(s, r);
    }
    if (Shot* sph = shot_spawn_callback(cb_sphere)) {
      bobj_set_pos(sph, s->pos);
      sph->p134 = 0x46;
      sph->p130 = 10;
      sph->power = s->power;
      sph->owner = s->owner;
    }
    if (Sprite* sp = shot_spawn_sprite_at(s, 3, 1)) {
      sprite_set_pos_v(sp, s->pos);
      sp->layer = 1;
    }
  }
  shot_image_end(s);
  shot_kill(s);
}

// ---- The unit ---------------------------------------------------------------------------

struct PrecisUnit : PartyUnit {
  // +0x360: move 8's arm. Never cleared (a new use spawns another); a dead arm is still
  // read until the move ends, as on the console.
  Shot* arm = nullptr;

  PrecisUnit() {  // 0x801ee0a0
    v2d8 = 1;
    v2da = 1;
    clut_row_check = 1;
    sfx_hit = 0x4C;
    sfx_crit = 0x4E;
  }
  void state_air_attack() override;  // [31] 0x801eff24
  void state_victory() override {    // [33] 0x801ee1f4: the pose by the victory voice tier
    if (counters[0] == 0) {
      const u8 tier = bg().victory_voice_tier;
      if (tier == 0xFF) anim = 0;
      if (tier < 2) anim = 8;
      if (tier == 2) anim = 0x11;
      counters[0] = 1;
    }
    model_default();
    animate(0);
  }
  int killer_move_cost(int which) override {  // [35] 0x801ee0fc
    int total = 0;
    for (int c = 0; c < 2; ++c) {
      const u8 move = record[0xCC + c * 2 + which];
      if (move != 0) total += unit_mp_cost(this, *battle_data(0x8006B28B + move));
    }
    return total;
  }
  int move_mp_cost(int index) override {  // [36] 0x801ee194
    const u8 move = record[0xCC + (index >> 1) * 2 + (index & 1)];
    return unit_mp_cost(this, move == 0 ? 0 : *battle_data(0x8006B28B + move));
  }
  void killer_move_state(int i) override {  // [37] 0x801ee028 (table 0x801f191c: slot 47 + i)
    switch (i) {
      case 0: move0(); break;
      case 1: move1(); break;
      case 2: move2(); break;
      case 3: move3(); break;
      case 4: move4(); break;
      case 5: move5(); break;
      case 6: move6(); break;
      case 7: move7(); break;
      case 8: move8(); break;
      case 9: move9(); break;
      default: break;
    }
  }

  void move0();  // [47] 0x801ee2b8
  void move1();  // [48] 0x801ee3e4
  void move2();  // [49] 0x801ee5bc
  void move3();  // [50] 0x801ee900
  void move4();  // [51] 0x801eeb6c
  void move5();  // [52] 0x801eee88
  void move6();  // [53] 0x801ef2f8
  void move7();  // [54] 0x801ef584
  void move8();  // [55] 0x801efb1c
  void move9();  // [56] 0x801efdf4

  void trail_on(int copies) {
    if (effects[0] == nullptr) effects[0] = afterimage_create(this, copies, kTrailRgb, 1);
  }
  void trail_grow() {
    if (effects[0] != nullptr) effects[0]->next_kind = 1;
  }
  void step(s32* v) {
    if (unit_try_move_scaled(this, v, 7) == nullptr) bobj_move(this, v);
  }
  void throw_fist(int lvl, int anim_no, s32 dy, int sfx);
};

// [31] 0x801eff24: state 11 is the base jump attack; state 10 fires a projectile at the
// target past frame 4 (once).
void PrecisUnit::state_air_attack() {
  if (action == 0xB) {
    PartyUnit::state_air_attack();
    return;
  }
  BattleGlobals& g = bg();
  anim = 0xC;
  loop_limit = 1;
  if (static_cast<u16>(animate(0)) == 0xFFFF) {
    action_kind = 0;
    begin_action();
  }
  attack_recovery = 0;
  if (frame < 5) counters[1] = 0;
  if (g.flags != 0 || counters[1] != 0 || frame < 5) return;
  counters[1] = 1;
  bsfx_play_at(0x23, this);
  ShotDesc* d = shot_desc_init(0x12);
  d->speed = 0x1C;
  d->life = 0x28;
  d->flags = 0x100006;
  d->spin = -4;
  d->trail_anim = static_cast<s32>(0xFFFBFFF6);
  d->target = target;
  const s32 from[3] = {pos[0], pos[1] - size[1] * 0x4000, pos[2]};
  shot_spawn_projectile(this, from, d, this);
}

// Move 0: a dash punch: from frame 10 she moves 2 a tick; the animation plays lvl + 1 times.
void PrecisUnit::move0() {
  BattleGlobals& g = bg();
  const int lvl = pc_move_charge_level(this, killer_index);
  loop_limit = static_cast<u8>(lvl + 1);
  anim = 0;
  if (g.flags == 0) {
    if (lvl != 0) trail_on(lvl);
    if (9 < frame) {
      vel[0] = facing[0] < 0 ? static_cast<s32>(0xFFFFE000) : 0x2000;
      step(vel);
    }
  }
  pc_attack_anim_step(this);
  pc_attack_hit(this, 0x100, 0);
  if (hit_landed != 0 && (box_mask & 0xC) == 0) hit_landed = 0;
}

// Move 1: after the wind-up (cut short when the battle is ending), a shell every 12 ticks,
// lvl + 2 of them.
void PrecisUnit::move1() {
  BattleGlobals& g = bg();
  const int lvl = pc_move_charge_level(this, killer_index);
  if (g.flags == 0) camera_follow_start();
  if (counters[1] == 0) {
    anim = 0;
    loop_limit = 1;
    if (static_cast<u16>(animate(0)) != 0xFFFF && (g.state < 3 || 0x13 < g.end_countdown)) return;
    counters[1] = 1;
    bobj_anim_reset(this);
  } else if (counters[1] != 1) {
    return;
  }
  loop_limit = 0;
  anim = 1;
  counters[2] -= g.frame_ticks;
  if (counters[2] < 0) {
    counters[2] += 0xC;
    if (Shot* s = shot_spawn_callback(cb_shell)) {
      shot_image_reset(s);
      shot_place_at_unit(s, this, 0x68, 0x80, 0);
      s->vel[0] = bobj_facing_fx(this, 0x18);
      pc_bind_shot(this, s, static_cast<s16>(lvl));
      pc_camera_frame_target(this, 0x20);
      bsfx_play_at(0x3C, s);
    }
    ++counters[3];
    if (lvl + 1 < counters[3]) {
      action_kind = 0x12;
      begin_action();
      return;
    }
  }
  pc_attack_anim_step(this);
}

// A punch thrown at the target's middle (move 2).
void PrecisUnit::throw_fist(int lvl, int anim_no, s32 dy, int sfx) {
  Shot* s = shot_spawn_callback(cb_fist);
  if (s == nullptr) return;
  shot_image_reset(s);
  s->anim = static_cast<u16>(anim_no);
  shot_place_at_unit(s, this, 0x10, dy, 0);
  s->facing[0] = facing[0];
  const s32 aim[3] = {target->pos[0], target->pos[1] - target->size[1] * 0x2000, target->pos[2]};
  const Vec4 v = vec_velocity_toward(s->pos, aim, 0x18);
  s->vel[0] = v.x, s->vel[1] = v.y, s->vel[2] = v.z, s->extra34 = v.w;
  pc_bind_shot(this, s, static_cast<s16>(lvl));
  pc_camera_frame_target(this, 0x20);
  bsfx_play_at(sfx, s);
}

// Move 2: past frame 8 a punch thrown at the target; from level 1 a ground wave past
// frame 13 and a spinning punch past frame 22 (with her shout); she steps forward 8 a
// tick over frames 18-23.
void PrecisUnit::move2() {
  BattleGlobals& g = bg();
  const int lvl = pc_move_charge_level(this, killer_index);
  loop_limit = 1;
  anim = static_cast<u16>(lvl);
  if (g.flags == 0) {
    if (lvl != 0) trail_on(2);
    trail_grow();
    if (counters[1] == 0 && 8 < frame && target != nullptr) {
      counters[1] = 1;
      throw_fist(lvl, 2, 0xA0, 0x1F);
    }
    if (lvl != 0 && counters[1] == 1 && 0xD < frame) {
      counters[1] = 2;
      if (Shot* s = shot_spawn_callback(cb_wave)) {
        s->vel[0] = bobj_facing_fx(this, 0xC);
        shot_place_at_unit(s, this, 0, -0x18, 0);
        s->facing[0] = facing[0];
        pc_bind_shot(this, s, static_cast<s16>(lvl));
      }
    }
    if (counters[1] == 2 && 0x16 < frame && target != nullptr) {
      pc_voice_attack_shout(this);
      counters[1] = 3;
      throw_fist(lvl, 3, 4, 0x4C);
    }
    if (static_cast<u32>(static_cast<u8>(frame)) - 0x12 < 6) {
      vel[0] = facing[0] < 0 ? static_cast<s32>(0xFFFF8000) : 0x8000;
      step(vel);
    }
  }
  pc_attack_anim_step(this);
}

// Move 3: after the wind-up the cannon goes out; she holds for 66 ticks (then frames the
// target), 24 and 36 more, and the move ends.
void PrecisUnit::move3() {
  BattleGlobals& g = bg();
  const int lvl = pc_move_charge_level(this, killer_index);
  loop_limit = 0;
  switch (counters[1]) {
    case 0:
      anim = 0;
      loop_limit = 1;
      if (static_cast<u16>(animate(0)) != 0xFFFF) return;
      if (Shot* s = shot_spawn_callback(cb_cannon)) {
        s->vel[0] = bobj_facing_fx(this, 0xC);
        shot_place_at_unit(s, this, 0, -0x18, 0);
        s->facing[0] = facing[0];
        pc_bind_shot(this, s, static_cast<s16>(lvl));
      }
      bobj_anim_reset(this);
      counters[1] = 1;
      [[fallthrough]];
    case 1:
      loop_limit = 0;
      anim = 1;
      if (g.flags == 0) {
        counters[2] += g.frame_ticks;
        if (0x42 < counters[2]) {
          counters[1] = 2;
          counters[2] = 0;
          if (camera_focus != 0) {
            const s32 ofs[3] = {bobj_facing_fx(this, 0x21C), 0, 0};
            camera_set_lookat_offset(g.camera, ofs);
          }
          pc_camera_frame_target(this, 0x50);
        }
      }
      break;
    case 2:
      anim = 2;
      if (g.flags == 0) {
        counters[2] += g.frame_ticks;
        if (0x18 < counters[2]) {
          counters[1] = 3;
          counters[2] = 0;
        }
      }
      break;
    case 3:
      anim = 3;
      if (g.flags == 0) {
        counters[2] += g.frame_ticks;
        if (0x24 < counters[2]) {
          action_kind = 0x12;
          begin_action();
          return;
        }
      }
      break;
    default:
      return;
  }
  animate(0);
}

// Move 4: a rush (a burst over frames 11-13, then 3 a tick) behind a vortex from frame 15;
// her attack hits again every 16 ticks (the vortex dims as the timer runs); the animation
// plays lvl * 2 + 2 times.
void PrecisUnit::move4() {
  BattleGlobals& g = bg();
  const int lvl = pc_move_charge_level(this, killer_index);
  loop_limit = static_cast<u8>(lvl * 2 + 2);
  anim = 0;
  if (g.flags == 0) {
    if (lvl != 0) trail_on(lvl * 2);
    trail_grow();
    vel[0] = vel[1] = vel[2] = 0;
    if (static_cast<u32>(static_cast<u8>(frame)) - 0xB < 3) vel[0] = bobj_facing_fx(this, 0x10 / g.frame_ticks);
    if (0xE < frame) vel[0] = bobj_facing_fx(this, 0xC);
    if (attack_recovery == 0) step(vel);
    pc_attack_hit(this, 0xC0, 0);
    if (hit_landed != 0 && attack_recovery == 0) {
      counters[2] += g.frame_ticks;
      if (0xF < counters[2]) {
        counters[2] -= 0x10;
        hit_landed = 0;
      }
    }
    counters[3] += g.frame_ticks;
  }
  if (0xE < frame) {
    VortexParams vp;
    vp.radius = 0x4000, vp.base = -0xC0, vp.width = 0xE0, vp.step = 0x40;
    vp.rings = 8, vp.segments = 6;
    vp.colour_a = scale_colour(0x60FF40, 0x100 - counters[2]);
    vp.colour_b = scale_colour(0x103030, 0x100 - counters[2]);
    vp.phase_span = 4, vp.phase = 0x1FFF - ((counters[3] * 0x60) & 0x1FFF);
    vp.twist_freq = 1, vp.twist_amp = 0x1000, vp.blend = 2, vp.ot_bias = 7;
    const s32 dir[3] = {-facing[0], 0, 0};
    const s32 at[3] = {bobj_front_x(this, 0xC0), pos[1] - 0x88000, pos[2]};
    s32 orient[3];
    vec_to_angles(orient, dir);
    obj_set_matrix(at, orient, kUnitScale);
    draw_vortex(vp);
  }
  pc_attack_anim_step(this);
}

// Move 5: a spin (anims 0, 1: a hop forward 0x98), a burst of sparks (anim 2), then a dash
// to the target (lvl + 1 approach steps a tick, a looping sound, dust) and a strike (anim
// 3; from level 1 it can hit twice) leaving a fading image. Cut short when the battle is
// ending.
void PrecisUnit::move5() {
  BattleGlobals& g = bg();
  const int lvl = pc_move_charge_level(this, killer_index);
  loop_limit = 0;
  if (g.flags == 0) camera_follow_start();
  if (2 < g.state && g.end_countdown < 0x14) {
    action_kind = 0x12;
    begin_action();
    return;
  }
  switch (counters[1]) {
    case 0:
      anim = 0;
      loop_limit = 1;
      if (static_cast<u16>(animate(0)) != 0xFFFF) return;
      bobj_anim_reset(this);
      counters[1] = 1;
      vel[0] = bobj_facing_fx(this, 0x98 / g.frame_ticks);
      step(vel);
      [[fallthrough]];
    case 1:
      anim = 1;
      loop_limit = 1;
      if (static_cast<u16>(animate(0)) != 0xFFFF) return;
      bobj_anim_reset(this);
      counters[1] = 2;
      [[fallthrough]];
    case 2:
      loop_limit = 1;
      anim = 2;
      if (g.flags == 0 && counters[2] == 0 && 4 < frame) {
        bsfx_play_at(0x34, this);
        counters[2] = 1;
        if (Shot* r = effect_spawn_sparks_at(pos, 10, 0x18, 100, 10, 0x2D, 0, 2, 0x5A, 0)) {
          r->anim = 5;
          bobj_share_sprite(this, r);
        }
      }
      if (static_cast<u16>(animate(0)) != 0xFFFF) return;
      counters[0] = static_cast<s32>(bsfx_play_at(0x2D, this));
      bobj_anim_reset(this);
      counters[1] = 3;
      reach = 0x18;
      unit_begin_approach(this);
      box_mask = 0;
      [[fallthrough]];
    case 3:
      sound_handle_fade_out(static_cast<u32>(counters[0]), 0x14, 8);
      if (g.flags != 0) {
        sound_handle_duck(static_cast<u32>(counters[0]), 8);
        if (g.flags != 0) return;
      }
      if (target == nullptr) {
        counters[1] = 4;
        bobj_anim_reset(this);
      } else {
        for (int i = 0; i <= lvl; ++i) {
          if (unit_move_toward_goal(this, 1) == 0) continue;
          counters[1] = 4;
          bobj_anim_reset(this);
          if (target != nullptr) facing[0] = target->pos[0] < pos[0] ? static_cast<s32>(0xFFFC0000) : 0x40000;
          break;
        }
      }
      for (int i = 0; i < 2; ++i) effect_dust_puff(pos, 6);
      return;
    case 4:
      anim = 3;
      loop_limit = 1;
      pc_attack_hit(this, 0x100, 0);
      if (hit_landed != 0 && counters[3] == 0 && lvl != 0) {
        counters[3] = 1;
        hit_landed = 0;
      }
      if (pc_attack_anim_step(this) == 0) return;
      if (Sprite* sp = unit_play_anim(this, 5)) {
        sp->facing[0] = facing[0];
        sprite_set_pos_v(sp, pos);
        sprite_set_fade(sp, 0x80, -8, 0);
      }
      return;
    default:
      return;
  }
}

// Move 6: a projectile (anim lvl + 2, or lvl + 4 from the lower stance) at the target's
// middle past frame 9; the stance (anim 0 when the target is well above her) is chosen on
// the first frame.
void PrecisUnit::move6() {
  BattleGlobals& g = bg();
  const int lvl = pc_move_charge_level(this, killer_index);
  loop_limit = 1;
  s32 aim[3] = {0, 0, 0};
  if (target != nullptr) {
    aim[0] = target->pos[0];
    aim[1] = target->pos[1] - target->size[1] * 0x2000;
    aim[2] = target->pos[2];
  }
  const Vec4 d = vec_direction(pos, aim);
  if (counters[2] == 0) {
    counters[2] = 1;
    anim = (d.y << 4) < -0x8000 ? 0 : 1;
  }
  pc_attack_anim_step(this);
  if (g.flags != 0) return;
  camera_follow_start();
  if (counters[1] != 0 || frame <= 9) return;
  bsfx_play_at(0x23, this);
  counters[1] = 1;
  const s32 from[3] = {bobj_front_x(this, 0xC0), pos[1] - (anim == 0 ? 0x100000 : 0x80000), pos[2]};
  ShotDesc* sd = shot_desc_init(anim == 0 ? lvl + 2 : lvl + 4);
  sd->speed = 0x10;
  sd->point[0] = aim[0], sd->point[1] = aim[1], sd->point[2] = aim[2];
  sd->life = 0x28;
  sd->power = lvl * 0x80 + 0x100;
  sd->flags = 0x100006;
  shot_spawn_projectile(this, from, sd, this);
  pc_camera_frame_target_for(this, 0x2D);
}

// Move 7: a leap at the target from frame 2 (a ring and dust on take-off; the flight time
// from the distance, the arc peaking over the target's top), hitting (power 0x180 + 0x80
// a level) on the way down; on a hit or a bump she bounces back and falls, dust where she
// lands.
void PrecisUnit::move7() {
  BattleGlobals& g = bg();
  pc_camera_behind_start(this);
  anim = 0;
  loop_limit = 0;
  const int lvl = pc_move_charge_level(this, killer_index);
  switch (counters[1]) {
    case 0:
      if (lvl != 0) trail_on(4);
      if (1 < frame) {
        if (target == nullptr) {
          vel[0] = vel[1] = vel[2] = 0;
          counters[2] = 0;
        } else {
          if (airborne == 0) {
            const s32 at[4] = {pos[0], pos[1], pos[2], pos_w};
            particle_spawn_ring(at, 0x10, 0x10, 0xE, 0x32, 0x204030, 0x80FF60, 0x20, 0);
          }
          for (int i = 0; i < 8; ++i) effect_dust_puff(pos, 2);
          bsfx_play_at(0x26, this);
          s32 dist = vec_distance_xz_fx(target->pos, pos) - (target->size[0] * 2 + 0xB0) * 0x1000;
          if (dist < 0x20000) dist = 0x20000;
          const s32 speed = target_far != 0 ? 0x18 : (0xE6000 < dist ? 10 : 4);
          s32 t = (dist / speed) >> 12;
          counters[2] = t;
          if (t == 0) t = 1;
          s32 top = target->pos[1] - target->size[1] * 0x4000;
          if (top < -0x1F4000) top = -0x1F4000;
          const s32 y = pos[1];
          const Vec4 dir = vec_direction_xz(pos, target->pos);
          vel[0] = dir.x, vel[1] = dir.y, vel[2] = dir.z, extra34 = dir.w;
          vel[1] = (top - y) / t - (dist / speed) / 2;
          vel[0] *= speed;
          vel[2] *= speed;
        }
        counters[1] = 1;
        jumping = 1;
        airborne = 1;
      }
      break;
    case 1:
      counters[2] -= g.frame_ticks;
      if (counters[2] < 0xB) anim = 1;
      if (g.flags == 0) {
        s32 d[3] = {vel[0], 0, vel[2]};
        step(d);
        d[0] = 0, d[1] = vel[1], d[2] = 0;
        if (unit_try_move_scaled(this, d, 7) == nullptr) {
          bobj_move(this, d);
        } else if (0xFFF < std::abs(vel[1])) {
          airborne = 2;
          counters[1] = 2;
          vel[1] /= 2;
        }
        vel[1] += g.frame_ticks * 0x1000;
        if (bobj_ground_height(this) < pos[1]) {
          pos[1] = bobj_ground_height(this);
          bsfx_play_at(0x1E, this);
          for (int i = 0; i < 8; ++i) effect_dust_puff(pos, 4);
          airborne = 0;
          counters[1] = 4;
        }
      }
      break;
    case 2:
      vel[0] = vel[0] < 1 ? 0xC000 : static_cast<s32>(0xFFFF4000);
      vel[1] = static_cast<s32>(0xFFFF7000);
      vel[2] = 0;
      counters[1] = 3;
      break;
    case 3:
      anim = 4;
      if (g.flags == 0) {
        const int r = pc_jump_step(this, 0x1000);
        if (r != 0) {
          counters[1] = 4;
          if (r == 2) {
            for (int i = 0; i < 8; ++i) effect_dust_puff(pos, 4);
          }
        }
      }
      break;
    case 4:
      anim = 2;
      jumping = 0;
      loop_limit = 1;
      pc_attack_anim_step(this);
      return;
    default:
      break;
  }
  trail_grow();
  if (g.state != kStateFighting || counters[1] == 3) {
    model_default();
    anim = 2;
  }
  animate(static_cast<u32>(static_cast<s32>(attack_recovery)));
  if (g.flags == 0 && pc_attack_hit(this, lvl * 0x80 + 0x180, 0) != 0 && counters[1] == 1) counters[1] = 2;
}

// Move 8: past frame 4 the arm goes out (the camera looks ahead); a beam (an additive
// gouraud triangle, pulsing with the frame count) joins her hand to it while the move
// runs. The animation loops until the arm is done.
void PrecisUnit::move8() {
  BattleGlobals& g = bg();
  const int lvl = pc_move_charge_level(this, killer_index);
  anim = 0;
  loop_limit = 0;
  layer = 0;
  if (counters[2] != 0 || (counters[1] != 0 && arm == nullptr)) loop_limit = 1;
  pc_attack_anim_step(this);
  if (g.flags == 0 && counters[1] == 0) {
    if (frame <= 4) return;
    if (camera_focus != 0) {
      const s32 ofs[3] = {bobj_facing_fx(this, 300), 0, 0};
      camera_set_lookat_offset(g.camera, ofs);
    }
    counters[1] = 1;
    bsfx_play_at(0x28, this);
    bsfx_play_at(0x2A, this);
    arm = shot_spawn_callback(cb_arm);
    if (arm != nullptr) {
      shot_image_reset(arm);
      shot_place_at_unit(arm, this, 0x140, 0, 0);
      arm->facing[0] = facing[0];
      pc_bind_shot(this, arm, static_cast<s16>(lvl));
    }
  }
  if (counters[1] == 0) return;
  // ORIGINAL BUG (original_bugs.md F11): when the arm could not be spawned (a full Shot
  // pool) the console still draws the beam, to a "Shot" read from address 0:
  //   (no check: arm->pos and arm->colour_mode below are read through the null pointer)
  if (arm == nullptr) return;
  psyq::SVECTOR v[3] = {};
  gte_set_view_matrix();
  v[0].vx = static_cast<s16>(bobj_front_x(this, 0xA0) >> 12);
  v[0].vy = static_cast<s16>((pos[1] - 0x8C000) >> 12);
  v[0].vz = static_cast<s16>(pos[2] >> 12);
  const s32 fx = bobj_facing_fx(this, 0x18);
  v[1].vx = static_cast<s16>((arm->pos[0] + fx) >> 12);
  v[1].vy = static_cast<s16>((arm->pos[1] - 0xA0000) >> 12);
  v[2].vy = static_cast<s16>((arm->pos[1] - 0x20000) >> 12);
  v[1].vz = static_cast<s16>(arm->pos[2] >> 12);
  v[2].vx = v[1].vx;
  v[2].vz = v[1].vz;
  s32 level = (g.battle_frames & 7) * 0x10 + 0x90;
  if (arm->colour_mode != 0) level = static_cast<s32>(static_cast<u32>(level) * arm->rgb[0]) >> 7;
  u32 c[3];
  c[0] = scale_colour(0x50A010, level) | 0x32000000;
  c[1] = scale_colour(0xFFC040, level);
  c[2] = c[1];
  draw_tri_g3(v, c, 0);
}

// Move 9: past frame 6 the ball is thrown (an arc of 0xe forward, 0x12 up).
void PrecisUnit::move9() {
  BattleGlobals& g = bg();
  const int lvl = pc_move_charge_level(this, killer_index);
  anim = 0;
  loop_limit = 1;
  if (pc_attack_anim_step(this) != 0 || g.flags != 0) return;
  camera_follow_start();
  if (counters[1] != 0 || frame <= 6) return;
  counters[1] = 1;
  bsfx_play_at(0x34, this);
  if (Shot* s = shot_spawn_callback(cb_ball)) {
    shot_image_reset(s);
    shot_place_at_unit(s, this, 0x20, 0xB4, 0);
    s->facing[0] = facing[0];
    s->vel[0] = bobj_facing_fx(this, 0xE);
    s->vel[1] = static_cast<s32>(0xFFFEE000);
    pc_bind_shot(this, s, static_cast<s16>(lvl));
  }
  pc_camera_frame_target_for(this, 0x50);
}

}  // namespace

std::unique_ptr<PartyUnit> pc_precis_create() { return std::make_unique<PrecisUnit>(); }

}  // namespace ss::game::battle
