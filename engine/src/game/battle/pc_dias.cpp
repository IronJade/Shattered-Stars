// Dias's party-character module (#2143 = #2145, slot-0 link 0x801ee000-0x801f13ab): the
// PartyUnit subclass (0x360 bytes, vtable 0x801f11d4: slots 1, 35, 36, 37, 46 and three of
// its own, 47-49, which are moves 0-2), his 10 killer moves (states 0x10-0x19, table
// 0x801f134c) and the Shot callbacks they spawn. Move ids (record +0xcc byte - 1); names
// from what the code does:
//   0 a jumping slash that leaves a slash mark
//   1 a charged sphere that may kill the target outright
//   2 a sword wave
//   3 a lunge with a wave of its own
//   4 stepping slashes: spikes round him (near) or a ground wave chain (far)
//   5 a crescent jump: up, a dive and a landing, hitting all the way
//   6 a dash with growing slash marks
//   7 a slash (near) or a volley of flying slashes (far)
//   8 a three-part combo
//   9 a combo of level + 2 parts
// Slot 1 (0x801ee0f8) only calls pc_update; the port keeps the base update.
#include <cstring>

#include "game/battle/battle.h"
#include "game/battle/bobj.h"
#include "game/battle/bsfx.h"
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
#include "game/battle/unit_move.h"

namespace ss::game::battle {
namespace {

const s32 kZero[3] = {0, 0, 0};                     // 0x8006e33c
const s32 kUnitScale[3] = {0x1000, 0x1000, 0x1000};  // 0x8006e34c
constexpr u32 kTrailRgb = 0x102080;

bool frame_is_end(int f) { return static_cast<s16>(f) == -1; }
void copy_vel_to_facing(BObj* o) {
  o->facing[0] = o->vel[0];
  o->facing[1] = o->vel[1];
  o->facing[2] = o->vel[2];
}
void set_vel(BObj* o, const Vec4& v) {
  o->vel[0] = v.x;
  o->vel[1] = v.y;
  o->vel[2] = v.z;
  o->extra34 = v.w;
}
// The Shot's afterimage (the console's +0x130) ends.
void end_image(Shot* s) { shot_image_end(s); }
s32 abs32(s32 v) { return v < 0 ? -v : v; }

// ---- Shot callbacks ------------------------------------------------------------------------

// 0x801f053c (move 0): the slash mark where the jumping slash hit (a frame quad; +0x128
// set: the late, mirrored stroke).
void cb_slash_mark(Shot* s) {
  s->layer = 1;
  s->anim = 1;
  s->loop_limit = 1;
  s32 c0[3], c1[3];
  if (s->p128 == 0) {
    c0[0] = bobj_facing_fx(s, 0xF0);
    c1[0] = bobj_facing_fx(s, -0xF0);
  } else {
    c0[0] = bobj_facing_fx(s, -0xF0);
    c1[0] = bobj_facing_fx(s, 0xF0);
  }
  c0[1] = static_cast<s32>(0xFFF24000);
  c0[2] = 0xC8000;
  c1[1] = 0;
  c1[2] = static_cast<s32>(0xFFF38000);
  if (shot_draw_frame_quad(s, c0, c1, 5, 3) == -1) shot_kill(s);
}

// 0x801f0164 (move 1): the sphere over its owner, growing (twice as fast from level 1) to
// 0x60 while he charges (action 0x11); then rings, and the target either dies outright
// (resist roll 0, chance power * 8 + 30: death 4 at the sphere) or takes a hit with a ring.
// Once done it shrinks away.
void cb_sphere(Shot* s) {
  BattleGlobals& g = bg();
  stage_colour_set(0x80, 0x80, 0x80);
  lights_build_oriented(kZero);
  {
    s32 orient[3] = {0, 0, 0};
    obj_set_matrix(s->pos, orient, kUnitScale);
  }
  draw_sphere(10, 10, s->p128 << 1, 0xC08020, (s->p128 << 8) / 0x60, 0, 2, 0x1000);
  stage_colour_restore();
  if ((s->p12c == 0 && s->owner->action != 0x11) || g.state != kStateFighting) s->p12c = 1;
  if (g.flags != 0) return;
  if (s->p12c != 0) {
    s->p128 -= g.frame_ticks;
    if (s->p128 < 0) shot_kill(s);
    return;
  }
  s->p128 += s->power == 0 ? g.frame_ticks : g.frame_ticks * 2;
  if (s->p128 <= 0x60) return;
  s->p128 = 0x60;
  particle_spawn_ring(s->pos, 0xE, 0x60, 0x28, 0x3C, 0x805000, 0x200800, 0x20, 0);
  particle_spawn_ring(s->pos, 0xE, 0x60, 0x14, 0x3C, 0x805000, 0x200800, 0x20, 0x400);
  s->p12c = 1;
  if (s->p130 != 0) return;
  const s16 power = s->power;
  s->p130 = 1;
  Unit* t = s->target;
  if (unit_is_down(t)) return;
  int sfx;
  if (t->resist_roll(0, power * 8 + 0x1E) == 0) {
    const s32 pt[4] = {t->pos[0], t->pos[1] + t->size[1] * -0x2000, t->pos[2], 0};
    Vec4 pts[1];
    pts[0].x = pt[0];
    pts[0].y = pt[1];
    pts[0].z = pt[2];
    if (unit_attack_targets(s->owner, 1, &s->target, pts, 0x100, t->pos[0] < s->owner->pos[0] ? 0x800 : 0, 0) == 0)
      return;
    t = s->target;
    const s32 centre[4] = {t->pos[0], t->pos[1] + t->size[1] * -0x2000, t->pos[2], 0};
    particle_spawn_ring(centre, 0x10, 0x80, -0x14, 0x3C, 0x200800, 0x805000, 0x280, 0);
    sfx = 0x48;
  } else {
    t->death = 4;
    t->vanish_target[0] = s->pos[0];
    t->vanish_target[1] = s->pos[1];
    t->vanish_target[2] = s->pos[2];
    bsfx_play_at(0x33, t);
    sfx = 0x2A;
  }
  bsfx_play_at(sfx, s->target);
}

// 0x801f0b0c (move 2): the sword wave (from level 1 with an afterimage); its first hit
// (power (+0x10a) * 0x80 + 0x180) bursts and lets its owner act again at once.
void cb_wave(Shot* s) {
  BattleGlobals& g = bg();
  s->anim = 1;
  s->layer = 2;
  s->size[2] = static_cast<s16>(s->owner->size[2] << 1);
  shot_draw_and_box(s, 2, 0x2080);
  if (g.flags != 0) return;
  if (shot_image(s) == nullptr && s->power != 0) shot_image_set(s, afterimage_create(s, 6, kTrailRgb, 0));
  if (shot_fade_out(s) != 0) {
    end_image(s);
    return;
  }
  if (s->p12c == 0 && 0x18 < s->p134) s->p12c = 1;
  bobj_move(s, s->vel);
  s->p134 += g.frame_ticks;
  copy_vel_to_facing(s);
  if (s->hit_landed != 0) return;
  Unit* found[8] = {};
  Vec4 pts[8] = {};
  const int n = collider_attack_overlaps(s, found, pts, 0);
  if (n == 0) return;
  s32 dir = s->vel[0];
  s->hit_landed = 1;
  if (dir == 0) dir = s->owner->facing[0];
  unit_attack_targets(s->owner, n, found, pts, s->power * 0x80 + 0x180, dir < 0 ? 0x800 : 0, 0);
  const s32 spread[3] = {0x20000, 0x20000, 0x20000};
  effect_debris_burst(pts[0].data(), spread, 3, 1, 2, 0x2000, 0xD);
  if (Sprite* sp = shot_spawn_sprite_at(s, s->anim + 1, 0)) {
    sprite_set_pos_v(sp, s->pos);
    sprite_set_quad(sp, static_cast<s16>(abs32(s->ext_x1) << 1), 0x2080);
    sprite_set_fade(sp, 0x80, -2, 4);
    sp->facing[0] = s->facing[0];
    sp->facing[1] = s->facing[1];
    sp->facing[2] = s->facing[2];
  }
  s->owner->attack_recovery = 0;
}

// 0x801f062c (move 3): the lunge's wave (anim power + 2, an afterimage of 6); its first
// hit ((power + 1) * 0x100) bursts and ends it.
void cb_lunge_wave(Shot* s) {
  BattleGlobals& g = bg();
  s->anim = static_cast<u16>(s->power + 2);
  s->layer = 2;
  s->size[2] = static_cast<s16>(s->owner->size[2] << 1);
  shot_draw_and_box(s, 4, 0xFFFFFFFF);
  if (g.flags != 0) return;
  if (shot_image(s) == nullptr) shot_image_set(s, afterimage_create(s, 6, kTrailRgb, 0));
  if (shot_fade_out(s) != 0) {
    end_image(s);
    return;
  }
  if (s->p12c == 0 && 0x18 < s->p134) s->p12c = 1;
  bobj_move(s, s->vel);
  s->p134 += g.frame_ticks;
  copy_vel_to_facing(s);
  if (s->hit_landed != 0) return;
  if (shot_hit_melee(s, (s->power + 1) * 0x100, 0) == 0) return;
  if (Sprite* sp = shot_spawn_sprite_at(s, s->anim + 1, 0)) {
    sprite_set_pos_v(sp, s->pos);
    sprite_set_quad(sp, static_cast<s16>(abs32(s->ext_x1) << 2));
    sprite_set_fade(sp, 0x80, -4, 4);
    sp->facing[0] = s->facing[0];
    sp->facing[1] = s->facing[1];
    sp->facing[2] = s->facing[2];
  }
  const s32 at[3] = {s->pos[0], s->pos[1] - 0x60000, s->pos[2]};
  const s32 spread[3] = {0x20000, 0x20000, 0x20000};
  effect_debris_burst(at, spread, 3, 1, 2, 0x1000, 0xF);
  end_image(s);
  shot_kill(s);
}

// 0x801f0880 (move 4, near): a spike out of the ground round him (power +0x130).
void cb_spike(Shot* s) {
  BattleGlobals& g = bg();
  s->anim = 1;
  s->size[2] = s->owner->size[2];
  if (frame_is_end(shot_draw_and_box(s, 6, 0xFFFFFFFF))) {
    shot_kill(s);
    return;
  }
  if (g.flags != 0) return;
  shot_fade_out(s);
  s->pos[1] = bobj_ground_height(s);
  if (s->hit_landed == 0) shot_hit_melee(s, s->p130, 0);
}

// 0x801f0928 (move 4, far): a ground wave that spawns the next one 4 ticks of its velocity
// ahead once it is 4 ticks old, until one leaves the arena; each hits once (power +0x130).
void cb_ground_wave(Shot* s) {
  BattleGlobals& g = bg();
  s->anim = 2;
  s->loop_limit = 1;
  s->size[2] = s->owner->size[2];
  if (frame_is_end(shot_draw_and_box(s, 6, 0xFFFFFFFF))) {
    shot_kill(s);
    return;
  }
  if (g.flags != 0) return;
  shot_fade_out(s);
  bool advance = true;
  if (s->hit_landed == 0) {
    if (s->p12c != 0) {
      advance = false;
    } else if (3 < s->p128 && s->p134 == 0) {
      s->p134 = 1;
      if (Shot* n = shot_spawn_callback(cb_ground_wave)) {
        n->pos[0] = s->pos[0] + s->vel[0] * 4;
        n->pos[1] = s->pos[1];
        n->pos[2] = s->pos[2] + s->vel[2] * 4;
        n->vel[0] = s->vel[0];
        n->vel[1] = s->vel[1];
        n->vel[2] = s->vel[2];
        n->p130 = s->p130;
        n->p12c = s->p12c;
        n->power = s->power;
        n->owner = s->owner;
        n->hit_landed = s->hit_landed;
        bobj_share_sprite(s, n);
      }
    }
  }
  if (advance && s->p12c == 0) {
    if (bobj_outside_arena(s)) {
      s->p12c = 1;
      return;
    }
    s->p128 += g.frame_ticks;
  }
  s->pos[1] = bobj_ground_height(s);
  if (s->hit_landed == 0) shot_hit_melee(s, s->p130, 0);
}

// 0x801f0dc8 (move 7, near): the slash's box (from level 1: one hit, power (+0x10a) *
// 0x80 + 0x180); it lets its owner act again at once.
void cb_slash(Shot* s) {
  BattleGlobals& g = bg();
  s->anim = 2;
  s->layer = 2;
  s->loop_limit = 1;
  s->size[2] = static_cast<s16>(s->owner->size[2] << 1);
  if (frame_is_end(shot_draw_and_box(s, 0, 0xFFFFFFFF))) {
    shot_kill(s);
    return;
  }
  if (g.flags != 0 || shot_fade_out(s) != 0 || s->hit_landed != 0 || s->power == 0) return;
  Unit* found[8] = {};
  Vec4 pts[8] = {};
  const int n = collider_attack_overlaps(s, found, pts, 0);
  if (n == 0) return;
  s->hit_landed = 1;
  unit_attack_targets(s->owner, n, found, pts, s->power * 0x80 + 0x180, s->owner->facing[0] < 0 ? 0x800 : 0, 0);
  s->owner->attack_recovery = 0;
}

// 0x801f0f28 (move 7, far): a flying slash (an afterimage of power * 3 + 3; growing copies
// from level 1); its first hit (0x100) leaves a sprite, lets its owner act again and ends it.
void cb_flying_slash(Shot* s) {
  BattleGlobals& g = bg();
  s->anim = 4;
  s->layer = 2;
  s->size[2] = static_cast<s16>(s->owner->size[2] << 1);
  shot_draw_and_box(s, 2, 0x2080);
  if (g.flags != 0) return;
  bool grow = true;
  if (shot_image(s) == nullptr) {
    Afterimage* a = afterimage_create(s, s->power * 3 + 3, kTrailRgb, 0);
    shot_image_set(s, a);
    if (a == nullptr) grow = false;
  }
  if (grow && s->power != 0 && shot_image(s) != nullptr) shot_image(s)->next_kind = 1;
  if (shot_fade_out(s) != 0) {
    end_image(s);
    return;
  }
  if (s->p12c == 0 && 0x18 < s->p134) s->p12c = 1;
  bobj_move(s, s->vel);
  s->p134 += g.frame_ticks;
  copy_vel_to_facing(s);
  if (s->hit_landed != 0) return;
  Unit* found[8] = {};
  Vec4 pts[8] = {};
  const int n = collider_attack_overlaps(s, found, pts, 0);
  if (n == 0) return;
  s32 dir = s->vel[0];
  s->hit_landed = 1;
  if (dir == 0) dir = s->owner->facing[0];
  if (unit_attack_targets(s->owner, n, found, pts, 0x100, dir < 0 ? 0x800 : 0, 0) != 0) {
    if (Sprite* sp = shot_spawn_sprite_at(s, 6, 1)) {
      sprite_set_pos_v(sp, s->pos);
      sp->layer = 3;
    }
  }
  s->owner->attack_recovery = 0;
  end_image(s);
  shot_kill(s);
}

// ---- The unit ---------------------------------------------------------------------------

struct DiasUnit : PartyUnit {
  DiasUnit() {  // 0x801ee0a0
    v2d8 = 1;
    v2da = 1;
    clut_row_check = 0x11;
    jump_threshold = static_cast<s32>(0xFFFBA000);
  }
  int killer_move_cost(int which) override {  // [35] 0x801ee118
    int total = 0;
    for (int c = 0; c < 2; ++c) {
      const u8 move = record[0xCC + c * 2 + which];
      if (move != 0) total += unit_mp_cost(this, *battle_data(0x8006B27F + move));
    }
    return total;
  }
  int move_mp_cost(int index) override {  // [36] 0x801ee1b0
    const u8 move = record[0xCC + (index >> 1) * 2 + (index & 1)];
    return unit_mp_cost(this, move == 0 ? 0 : *battle_data(0x8006B27F + move));
  }
  void killer_move_state(int i) override {  // [37] 0x801ee028 (table 0x801f134c)
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

  void move0();  // [47] 0x801ee210
  void move1();  // [48] 0x801ee524
  void move2();  // [49] 0x801ee7e0
  void move3();  // 0x801ee9a8
  void move4();  // 0x801eebc4
  void move5();  // 0x801eef9c
  void move6();  // 0x801ef460
  void move7();  // 0x801ef79c
  void move8();  // 0x801ef9a8
  void move9();  // 0x801efd68

  void face_target() {
    if (target != nullptr) facing[0] = pos[0] < target->pos[0] ? 0x40000 : static_cast<s32>(0xFFFC0000);
  }
  void trail(int copies) {
    if (effects[0] == nullptr) effects[0] = afterimage_create(this, copies, kTrailRgb, 1);
  }
  void end_trail() {
    if (effects[0] != nullptr) effects[0]->ending = 1;
    effects[0] = nullptr;
  }
  // 0x801f0028: pc_jump_step's copy (gravity as an argument).
  int jump_step(s32 gravity) { return pc_jump_step(this, gravity); }
  void dust() {
    for (int i = 0; i < 8; ++i) effect_dust_puff(pos, 4);
  }
  // shot_place_at_unit_thunk 0x800553f0: shot_place_at_unit with dz 0.
  void place(Shot* s, s32 dx, s32 dy) { shot_place_at_unit(s, this, dx, dy, 0); }
  // vx = facing * (v / ticks) (the steps of the stepping moves).
  s32 step(s32 v) { return bobj_facing_fx(this, v / bg().frame_ticks); }
  void walk() {
    if (unit_try_move_scaled(this, vel, 7) == nullptr) bobj_move(this, vel);
  }
};

// Move 0: a jumping slash (vy -0x5000, -0x7000 from level 1; gravity 0x300); its hits
// (0x100, 0x180 from level 1; rising angle past frame 8) leave a slash mark.
void DiasUnit::move0() {
  BattleGlobals& g = bg();
  const int lvl = pc_move_charge_level(this, killer_index);
  if (counters[2] == 0 && 2 < frame) {
    face_target();
    counters[2] = 1;
    vel[1] = lvl == 0 ? static_cast<s32>(0xFFFFB000) : static_cast<s32>(0xFFFF9000);
    jumping = 1;
    airborne = 1;
  }
  loop_limit = static_cast<u8>(lvl + 1);
  vel[2] = 0;
  bobj_texcache_reset(this);
  camera_follow_start();
  anim = 0;
  if (effects[0] == nullptr && lvl != 0) trail(2);
  if (g.flags == 0) {
    Unit* found[8] = {};
    Vec4 pts[8] = {};
    const int n = collider_attack_overlaps(this, found, pts, 0);
    bool check = true;
    if (n == 0) {
      check = hit_landed != 0;
    } else if (hit_landed == 0) {
      int angle = 9999;
      if (8 < frame) angle = 0 <= facing[0] ? -0x1C7 : 0x9C7;
      unit_attack_targets(this, n, found, pts, lvl == 0 ? 0x100 : 0x180, angle, 0);
      if (Shot* s = shot_spawn_callback(cb_slash_mark)) {
        shot_image_reset(s);
        s->p128 = frame < 9 ? 0 : 1;
        s->pos[0] = pts[0].x;
        s->pos[2] = pts[0].z;
        s->pos[1] = bobj_ground_height(s);
        s->facing[0] = facing[0];
        pc_bind_shot(this, s, static_cast<s16>(lvl));
      }
      if (bobj_on_screen(this)) g.hitstop_frames = 4;
      check = hit_landed != 0;
    }
    if (check && (box_mask & 0xC) == 0) hit_landed = 0;
  }
  vel[0] = jumping != 0 ? (0 < facing[0] ? 0x7000 : static_cast<s32>(0xFFFF9000)) : 0;
  const int r = jump_step(0x300);
  if (r != 0) {
    bsfx_play_at(0x1E, this);
    if (r == 2) dust();
  }
  pc_attack_anim_step(this);
}

// Move 1: the charged sphere above him (callback 0x801f0164); at the end of each loop of
// the charge (the first one) 0x30 streaks gather to it with sparks on the ground.
void DiasUnit::move1() {
  BattleGlobals& g = bg();
  const int lvl = pc_move_charge_level(this, killer_index);
  anim = static_cast<u16>(lvl);
  loop_limit = 8;
  camera_follow_start();
  if (pc_attack_anim_step(this) != 0 || g.flags != 0 || g.state != kStateFighting) return;
  if (effects[0] == nullptr && lvl != 0) trail(4);
  camera_follow_start();
  pc_attack_hit(this, 0x100, 0);
  if (counters[1] == 0) {
    counters[1] = 1;
    if (Shot* s = shot_spawn_callback(cb_sphere)) {
      shot_image_reset(s);
      place(s, 0, 0x1E0);
      pc_bind_shot(this, s, static_cast<s16>(lvl));
    }
  }
  if (loops != 0 && counters[1] == 1) {
    counters[1] = 2;
    sound_handle_pitch_up(bsfx_play_at(0x28, this), 0x3C);
    bsfx_play_at(0x29, this);
    const s32 centre[4] = {pos[0], pos[1] - 0xA0000, pos[2], 0};
    for (int i = 0; i < 0x30; ++i) {
      const Vec4 to = vec_random_around(centre, 0x578);
      particle_spawn_homing_line(centre, to.data(), 0x201000, 0xC07010, 0xA0, 0x12000, -0x80);
    }
    const s32 ground[3] = {pos[0], bobj_ground_height(this), pos[2]};
    effect_spawn_sparks_at(ground, 0xC, 10, 0x28, 0xB, 0x3C, 8, 1, 0x28, 0);
    pc_camera_frame_target(this, 0x3C);
  }
}

// Move 2: from frame 8 the sword wave (at a far target, or from level 1), speed 0x20.
void DiasUnit::move2() {
  BattleGlobals& g = bg();
  const int lvl = pc_move_charge_level(this, killer_index);
  loop_limit = 1;
  anim = 0;
  if (lvl != 0) bobj_texcache_reset(this);
  if (pc_attack_anim_step(this) != 0) return;
  if (g.flags == 0) {
    if (effects[0] == nullptr && lvl != 0) trail(2);
    if (effects[0] != nullptr) effects[0]->next_kind = 1;
    camera_follow_start();
    if (counters[1] == 0 && 7 < frame && (target_far != 0 || lvl != 0)) {
      counters[1] = 1;
      if (Shot* s = shot_spawn_callback(cb_wave)) {
        shot_image_reset(s);
        place(s, 0x20, 0);
        s->vel[0] = bobj_facing_fx(this, 0x20);
        s->facing[0] = facing[0];
        pc_bind_shot(this, s, static_cast<s16>(lvl));
      }
      pc_camera_frame_target(this, 0x28);
    }
  }
  pc_attack_hit(this, 0x100, 0);
  if (hit_landed != 0 && (box_mask & 0xC) == 0) hit_landed = 0;
}

// Move 3: steps back, lunges (frames 2 / 3) with a sprite at frame 4, and from frame 3
// sends the lunge's wave (speed 0x30 / ticks).
void DiasUnit::move3() {
  BattleGlobals& g = bg();
  const int lvl = pc_move_charge_level(this, killer_index);
  loop_limit = 1;
  anim = 0;
  if (g.flags == 0) {
    camera_follow_start();
    vel[0] = vel[1] = vel[2] = 0;
    if (counters[2] == 0) {
      vel[0] = step(-0x30);
      counters[2] = 1;
    }
    if (ticks_left < 1) {
      if (frame == 2) vel[0] = step(0x20);
      if (frame == 3) vel[0] = step(0x40);
      if (frame == 4) {
        if (Sprite* sp = unit_play_anim_ex(this, 2, 1)) {
          sprite_set_pos_v(sp, pos);
          sp->layer = 2;
          sp->facing[0] = facing[0];
        }
      }
    }
    walk();
    if (counters[1] == 0 && 2 < frame) {
      counters[1] = 1;
      if (Shot* s = shot_spawn_callback(cb_lunge_wave)) {
        shot_image_reset(s);
        place(s, 0x10, 0);
        s->vel[0] = step(0x30);
        s->facing[0] = facing[0];
        pc_bind_shot(this, s, static_cast<s16>(lvl));
      }
    }
  }
  pc_attack_anim_step(this);
}

// Move 4: stepping slashes (frames 2-5); from level 1 his own hits; near targets get a
// spike round him each frame 2-5 (power level * 0x80 + 0x80), far ones a ground wave chain
// (speed 0x30 at the target, power level * 0x80 + 0x100) from frame 5.
void DiasUnit::move4() {
  BattleGlobals& g = bg();
  anim = 0;
  loop_limit = 1;
  bobj_texcache_reset(this);
  vel[0] = vel[1] = vel[2] = 0;
  if (counters[2] == 0) {
    vel[0] = step(-0x30);
    counters[2] = 1;
  }
  if (g.flags == 0 && ticks_left < 1) {
    if (frame == 2) vel[0] = step(0x20);
    if (frame == 3) vel[0] = step(0x10);
    if (frame == 4) vel[0] = step(0x20);
    if (frame == 5) vel[0] = step(0x40);
  }
  walk();
  if (pc_attack_anim_step(this) != 0) return;
  const int lvl = pc_move_charge_level(this, killer_index);
  if (g.flags != 0) return;
  camera_follow_start();
  if (lvl != 0) pc_attack_hit(this, 0x100, 0);
  if (target_far == 0) {
    const int f = frame;
    if (1 < f && counters[1] < f && f < 6 && g.state == kStateFighting) {
      counters[1] = f;
      const s32 a = facing[0] < 0 ? (f - 1) * 0x200 + 0x600 : (f - 1) * -0x200 + 0x200;
      if (Shot* s = shot_spawn_callback(cb_spike)) {
        shot_image_reset(s);
        s->pos[0] = pos[0] + bcos(static_cast<u32>(a)) * 0xE0;
        s->pos[1] = pos[1];
        s->p130 = lvl * 0x80 + 0x80;
        s->p134 = lvl + 1;
        s->loop_limit = 1;
        s->pos[2] = pos[2] + bsin(static_cast<u32>(a)) * 0xE0;
        pc_bind_shot(this, s, static_cast<s16>(lvl));
        bsfx_play_at(0x34, s);
      }
    }
  } else if (counters[1] == 0 && 4 < frame && g.state == kStateFighting && target != nullptr) {
    if (Shot* s = shot_spawn_callback(cb_ground_wave)) {
      shot_image_reset(s);
      s->pos[0] = pos[0];
      s->pos[1] = pos[1];
      s->pos[2] = pos[2];
      s->pos_w = pos_w;
      set_vel(s, vec_velocity_toward_xz(pos, target->pos, 0x30));
      s->p130 = lvl * 0x80 + 0x100;
      pc_camera_frame_target(this, 0x14);
      pc_bind_shot(this, s, static_cast<s16>(lvl));
      bsfx_play_at(0x34, s);
    }
    counters[1] = 1;
  }
}

// Move 5: the crescent jump (+0x180 phase: 0 up (vy -0xc000, gravity 0x300; past frame 8
// a falling sound), 1 the dive (vx 0x28, vy 0x10000, no gravity), 2 the bounce back up and
// the landing (gravity 0x800)); hits all the way (0x100, element 4) with debris, up to
// level + 2 times (+0x178), rearmed every 8 ticks.
void DiasUnit::move5() {
  BattleGlobals& g = bg();
  const int lvl = pc_move_charge_level(this, killer_index);
  loop_limit = 0;
  if (counters[3] == 2) {
    model_default();
    anim = 2;
  } else {
    anim = 0;
  }
  vel[2] = 0;
  camera_follow_start();
  if (g.flags == 0) {
    if (effects[0] == nullptr && lvl != 0) trail(lvl << 1);
    if (lvl == 2 && effects[0] != nullptr) effects[0]->next_kind = 1;
    if (counters[3] <= 1) {
      if (counters[3] == 0 && counters[2] == 1 && 8 < frame) {
        sound_handle_pitch_down(bsfx_play_ui(0x22), 0x3C);
        counters[3] = 1;
      }
      if (counters[2] == 0 && 2 < frame) {
        face_target();
        counters[2] = 1;
        vel[0] = bobj_facing_fx(this, -4);
        vel[1] = static_cast<s32>(0xFFFF4000);
        jumping = 1;
        airborne = 1;
      }
      Unit* found[8] = {};
      Vec4 pts[8] = {};
      const int n = collider_attack_overlaps(this, found, pts, 0);
      bool check = true;
      if (n == 0) {
        check = hit_landed != 0;
      } else if (hit_landed == 0) {
        unit_attack_targets(this, n, found, pts, 0x100, 9999, 4);
        const s32 spread[3] = {0x20000, 0x20000, 0x20000};
        effect_debris_burst(pts[0].data(), spread, 3, 2, 2, 0x2000, 5);
        if (bobj_on_screen(this)) g.hitstop_frames = 4;
        ++counters[1];
        check = hit_landed != 0;
      }
      if (check && attack_recovery == 0) {
        const s32 c = counters[0] + g.frame_ticks;
        counters[0] = c;
        if (counters[1] < lvl + 2 && 8 < c) {
          counters[0] = c - 8;
          hit_landed = 0;
        }
      }
    }
  }
  switch (counters[3]) {
    case 1:
      vel[0] = bobj_facing_fx(this, 0x28);
      vel[1] = 0x10000;
      if (jump_step(0) != 0 || jumping == 0) {
        counters[3] = 2;
        vel[0] = bobj_facing_fx(this, -8);
        vel[1] = static_cast<s32>(0xFFFFC000);
        airborne = 1;
        jumping = 1;
      }
      break;
    case 0: {
      const int r = jump_step(0x300);
      if (r != 0) {
        bsfx_play_at(0x1E, this);
        if (r == 2) dust();
      }
      break;
    }
    case 2: {
      const int r = jump_step(0x800);
      if (r != 0 || jumping == 0) {
        bsfx_play_at(0x1E, this);
        if (r == 2) dust();
        action_kind = 0x12;
        begin_action();
        jumping = 0;
        end_trail();
      }
      break;
    }
    default:
      break;
  }
  bobj_texcache_begin_frame(this);
  pc_attack_anim_step(this);
}

// Move 6: a dash (steps at frames 2 / 3, back at 10 / 11) leaving three growing slash
// sprites from frame 5 (every 6 ticks), hitting (0x180, 0x200 from level 1) on the way.
void DiasUnit::move6() {
  BattleGlobals& g = bg();
  const int lvl = pc_move_charge_level(this, killer_index);
  loop_limit = 1;
  anim = 0;
  bobj_texcache_reset(this);
  if (pc_attack_anim_step(this) != 0 || g.flags != 0) return;
  camera_follow_start();
  vel[0] = vel[1] = vel[2] = 0;
  if (counters[2] == 0) {
    vel[0] = step(-0x10);
    counters[2] = 1;
    counters[0] = 0;
  }
  if (ticks_left < 1) {
    if (frame == 2) vel[0] = step(0x20);
    if (frame == 3) vel[0] = step(0x40);
    if (frame == 10) vel[0] = step(-0x40);
    if (frame == 11) vel[0] = step(-0x20);
  }
  if (4 < frame && counters[3] < 3) {
    const s32 c = counters[0] - g.frame_ticks;
    counters[0] = c;
    if (c < 1) {
      counters[0] = c + 6;
      ++counters[3];
      if (Sprite* sp = unit_play_anim(this, 2)) {
        sprite_set_pos(sp, bobj_front_x(this, 0x30), pos[1] - 0xB4000, pos[2]);
        sp->layer = 3;
        sp->facing[0] = facing[0];
        sprite_set_fade(sp, 0x80, -4, 0);
        const s32 grow = lvl * 0x80 + 0x80;
        sp->scale_vel[0] = grow;
        sp->scale_vel[1] = grow;
      }
    }
  }
  walk();
  Unit* found[8] = {};
  Vec4 pts[8] = {};
  const int n = collider_attack_overlaps(this, found, pts, 0);
  if (n != 0 && hit_landed == 0)
    unit_attack_targets(this, n, found, pts, lvl == 0 ? 0x180 : 0x200, 0 <= facing[0] ? -0x1C7 : 0x9C7, 0);
}

// Move 7: near, one slash (callback 0x801f0dc8); far, level + 1 flying slashes (speed
// 0x18) one every 8 ticks from frame 3, after a sprite.
void DiasUnit::move7() {
  BattleGlobals& g = bg();
  const int lvl = pc_move_charge_level(this, killer_index);
  loop_limit = 1;
  anim = static_cast<u16>(target_far);
  if (lvl != 0) bobj_texcache_reset(this);
  if (pc_attack_anim_step(this) != 0 || g.flags != 0) return;
  camera_follow_start();
  if (target_far == 0) {
    if (counters[1] == 0) {
      counters[1] = 1;
      if (Shot* s = shot_spawn_callback(cb_slash)) {
        shot_image_reset(s);
        place(s, 0, 0);
        s->facing[0] = facing[0];
        pc_bind_shot(this, s, static_cast<s16>(lvl));
      }
    }
  } else {
    if (counters[1] == 0) {
      counters[1] = 1;
      if (Sprite* sp = unit_play_anim_ex(this, 4, 1)) sprite_set_pos_v(sp, pos);
    }
    if (counters[1] <= lvl + 1 && 2 < frame) {
      counters[2] -= g.frame_ticks;
      if (counters[2] < 1) {
        ++counters[1];
        counters[2] += 8;
        if (Shot* s = shot_spawn_callback(cb_flying_slash)) {
          shot_image_reset(s);
          s->vel[0] = bobj_facing_fx(this, 0x18);
          place(s, 0, 0);
          s->facing[0] = facing[0];
          pc_bind_shot(this, s, static_cast<s16>(lvl));
        }
        pc_camera_frame_target(this, 0x2D);
      }
    }
  }
  pc_attack_hit(this, 0x100, 0);
}

// Move 8: a three-part combo (+0x178 part; anims 0 1 2, the second looped three times):
// steps in (frame 2 of part 0, all of part 1, frame 2 of part 2); hits (level * 0x80 +
// 0x200) with a sprite; part 2 starts with a big slash sprite.
void DiasUnit::move8() {
  BattleGlobals& g = bg();
  const int lvl = pc_move_charge_level(this, killer_index);
  vel[2] = 0;
  camera_follow_start();
  if (g.flags == 0) {
    if (effects[0] == nullptr && lvl != 0) trail(3);
    if (lvl == 1 && effects[0] != nullptr) effects[0]->next_kind = 1;
    camera_follow_start();
    vel[0] = vel[1] = vel[2] = 0;
    if (ticks_left < 1) {
      if (counters[1] == 0 && frame == 2) vel[0] = step(0x20);
      if (counters[1] == 1) vel[0] = bobj_facing_fx(this, 0x20);
      if (counters[1] == 2 && frame == 2) vel[0] = step(0x30);
    }
    walk();
    Unit* found[8] = {};
    Vec4 pts[8] = {};
    const int n = collider_attack_overlaps(this, found, pts, 0);
    if (n != 0 && hit_landed == 0) {
      unit_attack_targets(this, n, found, pts, lvl * 0x80 + 0x200, 9999, 0);
      if (Sprite* sp = unit_play_anim(this, 4)) {
        sprite_set_pos(sp, pts[0].x, pos[1], pos[2]);
        sprite_set_fade(sp, 0x80, -4, 8);
        sp->layer = 3;
        sp->facing[0] = facing[0];
      }
    }
  }
  for (;;) {
    anim = static_cast<u16>(counters[1]);
    loop_limit = counters[1] == 1 ? 3 : 1;
    if (counters[1] == 2 || g.state != kStateFighting) break;
    if (static_cast<s16>(animate(0)) != -1) return;
    hit_landed = 0;
    counters[3] = 0;
    counters[2] = 0;
    ++counters[1];
    bobj_anim_reset(this);
    if (counters[1] == 2) {
      if (Sprite* sp = unit_play_anim(this, 5)) {
        sprite_set_pos(sp, bobj_front_x(this, 0xF0), pos[1] - 0x48000, pos[2]);
        sprite_set_fade(sp, 0x80, -4, 4);
        sp->layer = 3;
        sp->facing[0] = facing[0];
      }
    }
  }
  pc_attack_anim_step(this);
}

// Move 9: a combo of level + 2 parts (+0x178; anim = the part, a shout at each), stepping
// in at the start of parts 1 and 2 and through part 3 past its first frame; hits 0x100.
void DiasUnit::move9() {
  BattleGlobals& g = bg();
  const int lvl = pc_move_charge_level(this, killer_index);
  loop_limit = 1;
  camera_follow_start();
  if (effects[0] == nullptr) trail(lvl << 1 | 1);
  if (effects[0] != nullptr) effects[0]->next_kind = 1;
  if (g.flags == 0) {
    Unit* found[8] = {};
    Vec4 pts[8] = {};
    const int n = collider_attack_overlaps(this, found, pts, 0);
    if (n != 0 && hit_landed == 0) unit_attack_targets(this, n, found, pts, 0x100, 9999, 0);
    switch (counters[1]) {
      case 0:
        vel[0] = vel[1] = vel[2] = 0;
        break;
      case 1:
        vel[0] = 0;
        if (counters[2] == 0) {
          counters[2] = 1;
          vel[0] = bobj_facing_fx(this, 8);
        }
        break;
      case 2:
        vel[0] = 0;
        if (counters[3] == 0) {
          counters[3] = 1;
          vel[0] = bobj_facing_fx(this, 8);
        }
        break;
      case 3:
        if (0 < frame) vel[0] = bobj_facing_fx(this, 0x20);
        break;
      case 4:
        vel[0] = 0;
        break;
      default:
        break;
    }
    walk();
  }
  for (;;) {
    anim = static_cast<u16>(counters[1]);
    if (counters[1] == lvl + 2 || g.state != kStateFighting) break;
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

}  // namespace

std::unique_ptr<PartyUnit> pc_dias_create() { return std::make_unique<DiasUnit>(); }

}  // namespace ss::game::battle
