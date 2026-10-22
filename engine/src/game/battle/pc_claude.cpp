// Claude's party-character module (#2131 / #2133, slot-0 link 0x801ee000-0x801f1950): the
// PartyUnit subclass (vtable 0x801f178c: slots 23, 25, 35, 36, 37, 45, 46), its 11 killer
// moves (states 0x10-0x1a, table 0x801f1924) and the Shot callbacks they spawn.
// Move ids (record +0xcc byte - 1): 0 wave slash, 1 rising combo, 2 Phase Gun, 3 leap
// strike, 4 jump kick, 5 HP-cost burst, 6 ground rings, 7 thrust / far wave, 8 sword
// storm, 9 dive slashes, 10 sweeping combo. (Names from what the code does.)
#include <cstring>

#include "game/battle/bsfx.h"
#include "game/battle/camera.h"
#include "game/battle/combat.h"
#include "game/battle/fx_draw.h"
#include "game/battle/killer.h"
#include "game/battle/particle.h"
#include "game/battle/pc_modules.h"
#include "game/battle/render.h"
#include "game/battle/shot.h"
#include "game/battle/sprite.h"
#include "game/battle/unit_move.h"
#include "game/boot/rng.h"
#include "runtime/runtime.h"

namespace ss::game::battle {

namespace {

const s32 kUnitScale[3] = {0x1000, 0x1000, 0x1000};  // 0x8006e34c

constexpr int kClaude = 1;

s16 rds16(const u8* p) {
  s16 v;
  std::memcpy(&v, p, 2);
  return v;
}
void wrs16(u8* p, s32 v) {
  const s16 s = static_cast<s16>(v);
  std::memcpy(p, &s, 2);
}
u16 rdu16(const u8* p) { return static_cast<u16>(rds16(p)); }

u32 rng_next15() { return static_cast<u32>(rng_next()) & 0x7FFF; }

void face_target(PartyUnit* u, s32 dist) {
  if (u->target != nullptr) u->facing[0] = u->pos[0] < u->target->pos[0] ? dist : -dist;
}

// The stage flash (0x8006f03e..), screen shake and the zoom kick of a big hit.
void big_flash(BObj* at, s32 zoom) {
  BattleGlobals& g = bg();
  ViewState& v = view();
  v.stage_colour[0] = v.stage_colour[1] = v.stage_colour[2] = 0x200;
  g.flash_active = 1;
  g.flash_delay = 2;
  g.flash_speed = 0x20;
  if (bobj_on_screen(at)) {
    screen_shake2(4, 4, 4, 6, 0xE);
    g.zoom_kick = zoom;
    g.zoom_kick_decay = 200;
  }
}

// 0x801f0640: one step of a jump arc (gravity a tick) for a unit in the air: 1 bumped into
// something (lands), 2 touched the ground, 0 still flying.
int claude_jump_step(PartyUnit* u, s32 gravity) {
  BattleGlobals& g = bg();
  if (g.flags != 0 || u->jumping == 0) return 0;
  s32 d[3] = {u->vel[0], 0, 0};
  if (unit_try_move_scaled(u, d, 7) == nullptr) bobj_move(u, d);
  d[0] = 0;
  d[1] = u->vel[1];
  int r;
  if (unit_try_move_scaled(u, d, 7) == nullptr) {
    bobj_move(u, d);
    u->vel[1] += gravity * g.frame_ticks;
    if (u->pos[1] <= bobj_ground_height(u)) return 0;
    u->pos[1] = bobj_ground_height(u);
    u->vel[1] = 0;
    bsfx_play_at(0x1E, u);
    r = 2;
    u->airborne = 0;
  } else {
    bsfx_play_at(0x1E, u);
    r = 1;
    u->airborne = 2;
  }
  u->jumping = 0;
  return r;
}

s32 abs_ext(const BObj* o) { return o->ext_x1 < 0 ? -o->ext_x1 : o->ext_x1; }

// ---- Shot callbacks ----------------------------------------------------------------------

// 0x801f0778 (move 0): the ground wave (+0x134 its size 1..; +0x130 power).
void cb_wave(Shot* s) {
  BattleGlobals& g = bg();
  s->anim = static_cast<u16>(s->p134);
  const u32 colour = s->power == 0 ? 0x20FFC020u : 0x2010C0FFu;
  s->size[2] = s->owner->size[2];
  const s16 frame = static_cast<s16>(shot_draw_and_box(s, 8, colour));
  if (frame != -1) {
    if (g.flags != 0) return;
    shot_fade_out(s);
    if (2 < s->p134 && s->p12c == 0) {
      if (6 < s->p128 && g.state == kStateFighting) {
        const int anim = s->p134 == 3 ? 2 : 3;
        if (Sprite* sp = sprite_spawn_from(anim, 1, s, nullptr)) {
          sprite_set_pos_v(sp, s->pos);
          sprite_set_quad(sp, static_cast<s16>(abs_ext(s) << 3), colour);
          s->p128 -= 6;
        }
      }
      if (bobj_outside_arena(s)) {
        s->p12c = 1;
        return;
      }
      s->p128 += g.frame_ticks;
    }
    bobj_move(s, s->vel);
    s->pos[1] = bobj_ground_height(s);
    if (s->hit_landed != 0) return;
    if (shot_hit_melee(s, s->p130, 0) == 0) return;
    if (s->p134 < 3) return;
    if (Sprite* sp = sprite_spawn_from(s->p134 + 2, 1, s, nullptr)) {
      sprite_set_pos_v(sp, s->pos);
      sprite_set_quad(sp, static_cast<s16>(abs_ext(s) << 2));
    }
  }
  shot_kill(s);
}

// 0x801f0984 (move 1, far): the flying slash.
void cb_slash(Shot* s) {
  BattleGlobals& g = bg();
  s->anim = 4;
  s->size[2] = s->owner->size[2];
  const s16 frame = static_cast<s16>(shot_draw_and_box(s, 5, 0xFFFFFFFF));
  if (frame != -1) {
    if (g.flags != 0) return;
    shot_fade_out(s);
    if (s->p12c == 0 && 0x14 < s->p134) s->p12c = 1;
    bobj_move(s, s->vel);
    s->p134 += g.frame_ticks;
    s->facing[0] = s->vel[0], s->facing[1] = s->vel[1], s->facing[2] = s->vel[2];
    if (s->hit_landed != 0) return;
    if (shot_hit_melee(s, s->p130, 0) == 0) return;
    if (Sprite* sp = sprite_spawn_from(6, 1, s, nullptr)) {
      sprite_set_pos_v(sp, s->pos);
      sprite_set_quad(sp, static_cast<s16>(abs_ext(s) << 2));
      sprite_set_fade(sp, 0x80, -4, 8);
      sp->facing[0] = s->facing[0], sp->facing[1] = s->facing[1], sp->facing[2] = s->facing[2];
    }
  }
  shot_kill(s);
}

// 0x801f0b0c (move 6): a ground ring segment (hits once with the owner's weapon element 1).
void cb_ring(Shot* s) {
  BattleGlobals& g = bg();
  s->size[2] = 0x40;
  s->loop_limit = 1;
  bobj_project(s);
  const s16 frame = static_cast<s16>(bobj_anim_draw(s, g.flags));
  if (frame == -1) {
    shot_kill(s);
    return;
  }
  if (g.flags != 0) return;
  shot_fade_out(s);
  if (s->hit_landed != 0) return;
  u8* st = s->owner->stats;
  const u16 el = rdu16(st + 0x5C);
  wrs16(st + 0x5C, el | 1);
  if (shot_hit_melee(s, 0x100, 0) != 0) {
    const s32 spread[3] = {0x20000, 0x20000, 0x20000};
    effect_debris_burst(s->pos, spread, 3, 0, 1, 0x1800, 0);
    bsfx_play_at(0x50, s);
  }
  wrs16(st + 0x5C, el);
}

// 0x801f0c2c (move 7, far): the thrust wave (element 4).
void cb_thrust(Shot* s) {
  BattleGlobals& g = bg();
  s->anim = 3;
  s->layer = 2;
  s->size[2] = s->owner->size[2];
  const s16 frame = static_cast<s16>(shot_draw_and_box(s, 5, 0x2050));
  if (frame == -1) {
    shot_kill(s);
    return;
  }
  if (g.flags != 0) return;
  shot_fade_out(s);
  if (s->p12c == 0 && 0x18 < s->p134) s->p12c = 1;
  bobj_move(s, s->vel);
  s->p134 += g.frame_ticks;
  s->facing[0] = s->vel[0], s->facing[1] = s->vel[1], s->facing[2] = s->vel[2];
  if (s->hit_landed != 0) return;
  u8* st = s->owner->stats;
  const u16 el = rdu16(st + 0x5C);
  wrs16(st + 0x5C, el | 4);
  if (shot_hit_melee(s, s->p130, 0) != 0) {
    if (s->power != 0) {
      if (Sprite* sp = sprite_spawn_from(5, 1, s, nullptr)) {
        sprite_set_pos_v(sp, s->pos);
        sp->layer = 4;
        sprite_set_fade(sp, 0x80, -2, 0x14);
        sp->facing[0] = s->facing[0];
      }
    }
    for (int i = 0; i < 3; ++i) {
      if (Sprite* sp = sprite_spawn_from(3, 1, s, nullptr)) {
        const s32 dx = rng_spread_fixed(0x20);
        const s32 dy = rng_between(static_cast<s32>(0xFFFA0000), static_cast<s32>(0xFFFE0000));
        sprite_set_pos(sp, s->pos[0] + dx, s->pos[1] + dy, s->pos[2]);
        sp->layer = 2;
        sprite_set_fade(sp, 0x80, -4, 0x10);
      }
    }
    bsfx_play_at_pos(0x23, s->pos);
    shot_kill(s);
  }
  wrs16(st + 0x5C, el);
}

// 0x801f11ec (move 8's child): a falling blade.
void cb_blade(Shot* s) {
  BattleGlobals& g = bg();
  s->layer = 2;
  s->size[2] = 0x20;
  s->anim = 6;
  bobj_project(s);
  bobj_anim_draw(s, g.flags);
  if (shot_fade_out(s) != 0) {
    shot_kill(s);
    return;
  }
  if (g.flags != 0) return;
  if (s->p12c == 0 && 0x14 < s->p128) s->p12c = 1;
  bobj_move(s, s->vel);
  s->p128 += g.frame_ticks;
  if (s->hit_landed != 0) return;
  if (shot_hit_melee(s, 0x80, 0) != 0) {
    const s32 spread[3] = {0x40000, 0x20000, 0x20000};
    effect_debris_burst(s->pos, spread, 3, 2, 1, 0x2000, 4);
  }
}

// 0x801f0e90 (move 8): the sword storm (+0x128 phase; +0x130 / +0x134 timers; +0x10c the
// blade countdown; +0x134 later the sound handle).
void cb_storm(Shot* s) {
  BattleGlobals& g = bg();
  s->layer = 1;
  static_cast<PartyUnit*>(s->owner)->tex_timer = 4;
  if (s->p128 == 0) {
    s->anim = 3;
    s->vel[1] = -0x2000;
    if (g.flags == 0) {
      s->p134 += g.frame_ticks;
      if (0xF < s->p134) {
        if (s->p130 != 0) {
          s->vel[0] = s->facing[0] < 0 ? -0x10000 : 0x10000;
          s->vel[1] = 0x100;
        } else {
          s->vel[1] = 0;
        }
        s->p134 = 0;
        ++s->p128;
      }
    }
  } else if (s->p128 == 1) {
    s->anim = 2;
    if (g.flags == 0) {
      s->p130 += g.frame_ticks;
      if (0x1A < s->p130) {
        s->p12c = 1;
        ++s->p128;
      }
      if (s->p134 == 0) {
        s->p134 = static_cast<s32>(bsfx_play_at(0x2D, s));
      } else {
        // sound_handle_fade_out / sound_handle_duck (#1984.0 0x8007fdec / 0x8007fd98):
        // the clip handles are not tracked by the port's bsfx.
      }
      const s32 left = static_cast<u16>(s->aux) - g.frame_ticks;
      s->aux = static_cast<s16>(left);
      if (static_cast<s32>(static_cast<u32>(left) << 16) < 1 && g.state == kStateFighting) {
        s->aux = static_cast<s16>(left + 10 - s->power * 2);
        if (Shot* c = shot_spawn_callback(cb_blade)) {
          c->pos[0] = s->pos[0] + (s->facing[0] < 0 ? -0xA0000 : 0xA0000);
          c->pos[1] = s->pos[1] - 0x40000;
          c->pos[2] = s->pos[2];
          c->vel[0] = s->vel[0] + (s->facing[0] < 0 ? -0x10000 : 0x10000);
          c->vel[1] = rng_between(0x2000, 0x8000);
          c->owner = s->owner;
          bobj_share_sprite(s, c);
        }
      }
      if (Sprite* sp = sprite_spawn_from(static_cast<int>(rng_range(2)) + 5, 0, s, nullptr)) {
        sprite_set_pos(sp, s->pos[0] + (s->facing[0] < 0 ? -0xA0000 : 0xA0000), s->pos[1] - 0x40000, s->pos[2]);
        sp->vel[0] = s->vel[0] + (s->facing[0] < 0 ? -0x10000 : 0x10000);
        sp->vel[1] = rng_between(0, 0xC000);
        sprite_set_fade(sp, 0x80, -2, 8);
      }
    }
  }
  shot_draw_and_box(s, 4, 0xFFFFFFFF);
  if (g.flags != 0) return;
  if (s->p12c != 0) {
    s->scale_x += g.frame_ticks * 0x100;
    s->scale_y += g.frame_ticks * 0x100;
  }
  shot_fade_out(s);
  bobj_move(s, s->vel);
}

// 0x801f1314 (move 9): the diving vortex.
void cb_vortex(Shot* s) {
  BattleGlobals& g = bg();
  s->anim = 4;
  s->size[2] = 0x30;
  shot_draw_and_box(s, 3, 0xFFFFFFFF);
  if (shot_fade_out(s) != 0) {
    shot_kill(s);
    return;
  }
  VortexParams vp;
  vp.radius = 0x4000, vp.base = -0x40, vp.width = 0x100, vp.step = 0x38;
  vp.rings = 6, vp.segments = 6;
  vp.colour_a = 0x4080FF, vp.colour_b = 0x1020;
  vp.phase_span = 2, vp.phase = 0x1FFF - ((s->p128 << 6) & 0x1FFF);
  vp.twist_freq = 1, vp.twist_amp = 0x1000, vp.blend = 2, vp.ot_bias = 5;
  const s32 back[3] = {-s->vel[0], -s->vel[1], s->vel[2]};
  s32 orient[3];
  vec_to_angles(orient, back);
  obj_set_matrix(s->pos, orient, kUnitScale);
  draw_vortex(vp);
  lens_flare_draw_small(s->pos, 0x400, 0x3090E0, 0x18, 0x46);
  if (g.flags != 0) return;
  s->p128 += g.frame_ticks;
  bobj_move(s, s->vel);
  bool landed = false;
  if (s->hit_landed == 0) {
    u8* st = s->owner->stats;
    const u16 el = rdu16(st + 0x5C);
    wrs16(st + 0x5C, el | 4);
    shot_hit_melee(s, 0x100, 0);
    const int hit = shot_hit_melee(s, 0x100, 0);
    wrs16(st + 0x5C, el);
    if (hit != 0) {
      if (bobj_on_screen(s)) screen_shake2(4, 4, 4, 6, 0xE);
      const s32 spread[3] = {0x80000, 0x80000, 0x80000};
      effect_debris_burst(s->pos, spread, 3, 2, 1, 0x1800, 5);
      landed = true;
    }
  }
  if (!landed) {
    if (s->pos[1] <= bobj_ground_height(s)) return;
    if (bobj_on_screen(s)) screen_shake(2, 2, 10);
  }
  bsfx_play_at_pos(0x50, s->pos);
  if (-0x12C000 < s->pos[1]) {
    const s32 at[3] = {s->pos[0], bobj_ground_height(s), s->pos[2]};
    if (Shot* r = effect_spawn_sparks_at(at, 10, 0x40, 0xE0, 0x14, 0x46, 0, 2, 0x30, 0)) {
      r->anim = 5;
      bobj_share_sprite(s, r);
    }
    if (Shot* r = effect_spawn_sparks_at(at, 0xC, 0x30, 0x100, 0x20, 0x3C, 0, 2, 0x20, 0)) {
      r->anim = 5;
      bobj_share_sprite(s, r);
    }
    if (Shot* r = effect_spawn_sparks_at(at, 8, 0x38, 0x140, 10, 0x5A, 0, 2, 0x10, 0)) {
      r->anim = 5;
      bobj_share_sprite(s, r);
    }
  }
  shot_kill(s);
}

// ---- The unit ---------------------------------------------------------------------------

struct ClaudeUnit : PartyUnit {
  ClaudeUnit() {  // 0x801ee05c
    v2d8 = 1;
    v2da = 1;
    clut_row_check = 1;
  }
  void model_default() override {  // [23] 0x801ee108
    clut_row_check = 1;
    PartyUnit::model_default();
  }
  void model_select(int n) override {  // [25] 0x801ee0a8
    switch (action) {
      case 0x11: case 0x14: case 0x16: case 0x17: case 0x19: clut_row_check = 1; break;
      case 0x12: clut_row_check = 0x11; break;
      default: break;
    }
    PartyUnit::model_select(n);
  }
  int killer_move_cost(int which) override {  // [35] 0x801ee12c: both combos' moves on the button
    int total = 0;
    for (int c = 0; c < 2; ++c) {
      const u8 move = record[0xCC + c * 2 + which];
      if (move != 0) total += unit_mp_cost(this, *battle_data(0x8006B213 + move));
    }
    return total;
  }
  int move_mp_cost(int index) override {  // [36] 0x801ee1c4
    const u8 move = record[0xCC + (index >> 1) * 2 + (index & 1)];
    return unit_mp_cost(this, move == 0 ? 0 : *battle_data(0x8006B213 + move));
  }
  void killer_move_state(int i) override;  // [37] 0x801ee028 (table 0x801f1924)
  int forced_attack_choice() override;     // [45] 0x801f0544

  void move0();   // 0x801ee224
  void move1();   // 0x801ee4ec
  void move2();   // 0x801ee878
  void move3();   // 0x801eec04
  void move4();   // 0x801ef1dc
  void move5();   // 0x801ef468
  void move6();   // 0x801ef6b4
  void move7();   // 0x801ef91c
  void move8();   // 0x801efc80
  void move9();   // 0x801efe14
  void move10();  // 0x801f0190

  void afterimage_on(int copies) {
    if (effects[0] == nullptr) effects[0] = afterimage_create(this, copies, 0x602008, 1);
  }
};

void ClaudeUnit::killer_move_state(int i) {
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
    case 10: move10(); break;
    default: break;
  }
}

int ClaudeUnit::forced_attack_choice() {
  // With the MP, the move-6 (byte 6) assignment is chosen while HP is under a quarter.
  if (wait_after_spell != 0) return -1;
  for (int which = 0; which < 2; ++which) {
    if (st()->mp < killer_move_cost(which)) continue;
    for (int c = 0; c < 2; ++c) {
      if (record[which + c * 2 + 0xCC] != 6) continue;
      s32 q = st()->hp_max;
      if (q < 0) q += 3;
      if (st()->hp < (q >> 2)) return which;
    }
  }
  return -1;
}

// Move 0: the wave slash (near: waves fanning out with the frames; far: one wave at the target).
void ClaudeUnit::move0() {
  BattleGlobals& g = bg();
  anim = 0;
  loop_limit = 3;
  if (pc_attack_anim_step(this) != 0) return;
  const int lvl = pc_move_charge_level(this, killer_index);
  if (g.flags != 0) return;
  if (effects[0] == nullptr && lvl != 0) effects[0] = afterimage_create(this, 4, 0x602008, 1);
  if (effects[0] != nullptr) effects[0]->next_kind = 1;
  camera_follow_start();
  if (target_far != 0) {
    if (counters[1] != 0 || frame < 4 || g.state != kStateFighting || target == nullptr) return;
    if (Shot* s = shot_spawn_callback(cb_wave)) {
      s->pos[0] = pos[0], s->pos[1] = pos[1], s->pos[2] = pos[2], s->pos_w = pos_w;
      const Vec4 v = vec_velocity_toward_xz(pos, target->pos, 0x14);
      s->vel[0] = v.x, s->vel[1] = v.y, s->vel[2] = v.z, s->extra34 = v.w;
      s->p130 = lvl * 0x80 + 0x100;
      s->p134 = lvl * 2 + 3;
      pc_camera_frame_target(this, 0x14);
      pc_bind_shot(this, s, static_cast<s16>(lvl));
      bsfx_play_at(0x34, s);
    }
    counters[1] = 1;
    return;
  }
  const int fr = frame;
  if (!(counters[1] < fr && fr < 6) || g.state != kStateFighting) return;
  counters[1] = fr;
  const s32 a = facing[0] < 0 ? (fr - 1) * 0x200 + 0x600 : 0x200 - (fr - 1) * 0x200;
  if (Shot* s = shot_spawn_callback(cb_wave)) {
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

// Move 1: the rising combo (near: hits with sparks flying; far: the flying slash).
void ClaudeUnit::move1() {
  BattleGlobals& g = bg();
  face_target(this, 0x40000);
  loop_limit = 1;
  s32 lvl = static_cast<s16>(killer_charge[killer_index & 3] / 100);
  if (3 < lvl) lvl = 3;
  if (target_far == 0) {
    anim = static_cast<u16>(lvl & 1);
    if (g.flags == 0 && attack_recovery == 0) {
      camera_follow_start();
      pc_attack_hit(this, 0x80, 0);
      if ((frame & 1) == 0 && rng_next15() < static_cast<u32>(lvl * 7000)) {
        const s32 a = facing[0] < 1 ? rng_between(0x638, 0x9C7) : rng_spread(0x1C7);
        if (Sprite* sp = sprite_spawn_from(3, 2, this, nullptr)) {
          sp->vel[0] = bcos(static_cast<u32>(a)) << 4;
          const s32 sn = bsin(static_cast<u32>(a));
          sp->vel[1] = sn * -0x10;
          sprite_set_pos(sp, pos[0] + facing[0] + sp->vel[0] * 3, pos[1] + sn * -0x30 - 0x50000, pos[2]);
          sp->roll = static_cast<s16>(a + 0x800);
        }
      }
    }
    const s8 before = frame;
    pc_attack_anim_step(this);
    if (g.flags == 0 && attack_recovery == 0 && frame != before && hit_landed != 0 && (frame & 1) == 0)
      hit_landed = 0;
    return;
  }
  anim = 3;
  if (g.flags == 0) {
    camera_follow_start();
    if (counters[1] == 0 && 3 < frame && g.state == kStateFighting) {
      if (Shot* s = shot_spawn_callback(cb_slash)) {
        s->pos[0] = pos[0] + facing[0];
        s->pos[1] = pos[1] - 0x50000;
        s->pos[2] = pos[2];
        s->vel[0] = 0 < facing[0] ? 0x18000 : static_cast<s32>(0xFFFE8000);
        s->p130 = lvl * 0x20 + 0x100;
        pc_camera_frame_target(this, 0x18);
        pc_bind_shot(this, s, static_cast<s16>(lvl));
        bsfx_play_at(0x23, s);
      }
      counters[1] = 1;
    }
  }
  pc_attack_anim_step(this);
}

// Move 2: Phase Gun (a flash, a strong hit with the attack stat raised to 0x2a8, sparks and
// 48 homing streaks into the impact).
void ClaudeUnit::move2() {
  BattleGlobals& g = bg();
  face_target(this, 0x40000);
  if (counters[1] == 0 && 6 < frame) {
    if (camera_focus != 0) {
      const s32 ofs[3] = {bobj_facing_fx(this, 0x140), 0, 0};
      camera_set_lookat_offset(g.camera, ofs);
    }
    counters[1] = 1;
    bsfx_play_at(0x3C, this);
    big_flash(this, 0x1450);
    pc_camera_frame_target(this, 0x14);
  }
  loop_limit = 1;
  anim = 0;
  const s16 atk_saved = rds16(stats + 0x2E);
  wrs16(stats + 0x2E, 0x2A8);
  if (g.flags == 0) {
    Unit* found[8] = {};
    Vec4 pts[8] = {};
    const int n = collider_attack_overlaps(this, found, pts, 0);
    if (n != 0 && hit_landed == 0) {
      unit_attack_targets(this, n, found, pts, 0x100, 9999, 0);
      bsfx_play_at(0x34, found[0]);
      if (g.state == kStateFighting) {
        s32 scale = 0x1000;
        for (int i = 0; i < 8; ++i) {
          if (Sprite* sp = sprite_spawn_from(2, 0, this, nullptr)) {
            const s32 x = pts[0].x + rng_spread_fixed(0xA0);
            const s32 y = pts[0].y + rng_spread(0x20);
            sprite_set_pos(sp, x, y, pts[0].z);
            sprite_set_fade(sp, 0x80, -6, static_cast<s16>(rng_between(0, 0x1E)));
            sp->scale_x = scale;
            sp->scale_y = scale;
            sp->layer = 6;
            sp->roll_speed = static_cast<s16>(rng_spread(0x40));
            sp->scale_vel[0] = 0x80;
            sp->scale_vel[1] = 0x80;
          }
          scale += 0x800;
        }
        const s32 centre[4] = {pts[0].x, pts[0].y, pts[0].z, pts[0].w};
        for (int i = 0; i < 0x30; ++i) {
          const Vec4 r = vec_random_around(centre, 0x708);
          const s32 to[4] = {r.x, r.y, r.z, r.w};
          particle_spawn_homing_line(centre, to, 0x401000, 0xFF8000, 0xA0, 0x12000, -0x80);
        }
      }
    }
  }
  wrs16(stats + 0x2E, atk_saved);
  pc_attack_anim_step(this);
}

// Move 3: the leap strike (jumps at the target; +0x178 phase, +0x17c flight ticks).
void ClaudeUnit::move3() {
  BattleGlobals& g = bg();
  pc_camera_behind_start(this);
  anim = 0;
  loop_limit = 0;
  const int lvl = pc_move_charge_level(this, killer_index);
  switch (counters[1]) {
    case 0: {
      if (1 < lvl) effects[0] = afterimage_create(this, 4, 0x602008, 1);
      face_target(this, 0x40000);
      if (airborne == 0) particle_spawn_ring(pos, 0x10, 0x20, 0x12, 0x3C, 0x202040, 0x80C0FF, 0x20, 0);
      if (target == nullptr) {
        vel[0] = vel[1] = vel[2] = 0;
        counters[2] = 0;
      } else {
        for (int i = 0; i < 8; ++i) effect_dust_puff(pos, 2);
        bsfx_play_at(0x26, this);
        s32 d = vec_distance_xz_fx(target->pos, pos);
        if (1 < lvl) {
          d += (target->size[0] * 2 + 0xB0) * -0x1000;
          if (d < 0x20000) d = 0x20000;
        }
        const s32 div = target_far != 0 ? 0x18 : (0xE6000 < d ? 10 : 4);
        s32 ticks = (d / div) >> 12;
        counters[2] = ticks;
        if (ticks == 0) ticks = 1;
        s32 top = target->pos[1] + target->size[1] * -0x4000;
        if (top < -0x1F4000) top = -0x1F4000;
        const Vec4 dir = vec_direction_xz(pos, target->pos);
        vel[0] = dir.x, vel[1] = dir.y, vel[2] = dir.z, extra34 = dir.w;
        vel[1] = (top - pos[1]) / ticks - (d / div) / 2;
        vel[0] *= div;
        vel[2] *= div;
      }
      jumping = 1;
      airborne = 1;
      ++counters[1];
      break;
    }
    case 1: {
      counters[2] -= g.frame_ticks;
      if (lvl < 2) {
        if (-1 < vel[1]) anim = static_cast<u16>(lvl + 1);
      } else if (counters[2] < 0x19) {
        anim = 3;
      }
      if (g.flags == 0) {
        s32 d[3] = {vel[0], 0, vel[2]};
        if (unit_try_move_scaled(this, d, 7) == nullptr) bobj_move(this, d);
        d[0] = 0, d[2] = 0;
        d[1] = vel[1];
        if (unit_try_move_scaled(this, d, 7) == nullptr) {
          bobj_move(this, d);
        } else {
          s32 vy = vel[1] < 0 ? -vel[1] : vel[1];
          if (0xFFF < vy) {
            airborne = 2;
            counters[1] = 2;
            vel[1] /= 2;
          }
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
    }
    case 2:
      vel[1] = static_cast<s32>(0xFFFF7000);
      vel[0] = vel[0] < 1 ? 0xC000 : static_cast<s32>(0xFFFF4000);
      vel[2] = 0;
      counters[1] = 3;
      break;
    case 3: {
      anim = 4;
      if (g.flags == 0) {
        const int r = claude_jump_step(this, 0x1000);
        if (r != 0) {
          counters[1] = 4;
          if (r == 2)
            for (int i = 0; i < 8; ++i) effect_dust_puff(pos, 4);
        }
      }
      break;
    }
    case 4:
      jumping = 0;
      action_kind = 0x12;
      if (effects[0] != nullptr) {
        effects[0]->ending = 1;
        effects[0] = nullptr;
      }
      begin_action();
      return;
    default: break;
  }
  if (g.state != kStateFighting) {
    model_default();
    anim = 2;
  }
  animate(static_cast<u32>(attack_recovery));
  if (g.flags == 0 && pc_attack_hit(this, lvl * 0x80 + 0x180, 0) != 0 && counters[1] == 1) counters[1] = 2;
}

// Move 4: the jump kick (bouncing hits; the hit-stop on screen).
void ClaudeUnit::move4() {
  BattleGlobals& g = bg();
  const int lvl = pc_move_charge_level(this, killer_index);
  if (counters[2] == 0 && 0 < frame) {
    face_target(this, 0x40000);
    counters[2] = 1;
    vel[1] = lvl == 0 ? static_cast<s32>(0xFFFFD800) : static_cast<s32>(0xFFFFB000);
    jumping = 1;
    airborne = 1;
  }
  loop_limit = static_cast<u8>(lvl + 1);
  vel[2] = 0;
  camera_follow_start();
  anim = 0;
  if (effects[0] == nullptr && lvl != 0) effects[0] = afterimage_create(this, 4, 0x602008, 1);
  if (g.flags == 0) {
    Unit* units[8] = {};
    Vec4 points[8] = {};
    const int n = collider_attack_overlaps(this, units, points, 0);
    bool clear_hit = false;
    if (n == 0) {
      clear_hit = hit_landed != 0;
    } else if (hit_landed == 0) {
      s32 angle = 9999;
      if (frame < 9) angle = facing[0] < 0 ? 0x9C7 : static_cast<s32>(0xFFFFFE39);
      unit_attack_targets(this, n, units, points, 0x100, angle, 0);
      if (bobj_on_screen(this)) g.hitstop_frames = 4;
      clear_hit = hit_landed != 0;
    } else {
      clear_hit = true;
    }
    if (clear_hit && (box_mask & 0xC) == 0) hit_landed = 0;
  }
  if (jumping == 0) vel[0] = 0;
  else vel[0] = 0 < facing[0] ? 0x8000 : static_cast<s32>(0xFFFF8000);
  const int r = claude_jump_step(this, 0x1C2);
  if (r != 0) {
    bsfx_play_at(0x1E, this);
    if (r == 2)
      for (int i = 0; i < 8; ++i) effect_dust_puff(pos, 4);
  }
  pc_attack_anim_step(this);
}

// Move 5: the HP-cost burst (sparkles rising, two rings; loses HP max * (lvl + 1) / 5).
void ClaudeUnit::move5() {
  BattleGlobals& g = bg();
  const int lvl = pc_move_charge_level(this, killer_index);
  anim = static_cast<u16>(lvl);
  loop_limit = 3;
  camera_follow_start();
  if (counters[1] == 0 && 7 < frame) {
    counters[1] = 1;
    for (int i = 0; i < lvl * 0xC + 0xC; ++i) {
      const int anim_id = static_cast<int>(rng_range(4)) + 0x2C;
      if (Sprite* sp = sprite_spawn_from(anim_id, 0, g.sprite_bank_fx.get(), nullptr)) {
        sprite_set_pos(sp, pos[0] + rng_spread_fixed(0x60), pos[1] - 0xA0000, pos[2]);
        sp->layer = 2;
        sprite_set_fade(sp, 0x80, -2, 0xC);
        const s32 sc = rng_between(0x1800, 0x2000);
        sp->scale_y = sc;
        sp->scale_x = sc;
        sp->vel[1] = rng_between(0x1000, 0x2000);
        sp->roll_speed = 0x5B;
      }
    }
    effect_spawn_sparks_on(this, 0xC, 0x18, lvl * 0x10 + 0x3C, 9, 0x3C, 6, 0, 0x1E, 0);
    effect_spawn_sparks_on(this, 0xC, 8, lvl * 0x20 + 0x4C, 5, 0x3C, 7, 1, 0x28, 0);
    bsfx_play_at(0x16, this);
    const s32 hp = -((st()->hp_max * (lvl + 1)) / 5);
    damage_popup_spawn(this, hp, -0x2000, 0);
    unit_damage_hp(this, hp);
  }
  pc_attack_anim_step(this);
}

// Move 6: the ground rings (waves of ring segments around the unit).
void ClaudeUnit::move6() {
  BattleGlobals& g = bg();
  anim = 0;
  loop_limit = 0;
  camera_follow_start();
  const int lvl = pc_move_charge_level(this, killer_index);
  if (g.flags == 0) {
    counters[1] -= g.frame_ticks;
    if (3 < frame && counters[1] < 0) {
      if (counters[2] == 0) screen_shake2(6, 4, 4, 4, 0x10);
      counters[1] += 0x10;
      int first, last;
      if (target_far == 0) {
        first = last = facing[0] < 0 ? 9 : 3;
      } else {
        first = facing[0] < 0 ? 6 : 0;
        last = facing[0] < 0 ? 0xC : 6;
      }
      for (int k = first; k <= last; ++k) {
        const s32 a = (k << 12) / 0xC;
        const s32 r = counters[2] * 0xC0 + 0xC0;
        if (Shot* s = shot_spawn_callback(cb_ring)) {
          s->pos[0] = pos[0] + bsin(static_cast<u32>(a)) * r;
          s->pos[2] = pos[2] + bcos(static_cast<u32>(a)) * r;
          s->pos[1] = bobj_ground_height(s);
          s->anim = pc_module_u8(kClaude, 0x801F1904 + static_cast<u32>(k));
          s->facing[0] = pc_module_u8(kClaude, 0x801F1914 + static_cast<u32>(k)) == 0 ? -0x40000 : 0x40000;
          pc_bind_shot(this, s, static_cast<s16>(lvl));
        }
      }
      ++counters[2];
      if (lvl + 2 <= counters[2]) {
        action_kind = 0x12;
        begin_action();
      }
    }
  }
  bobj_texcache_reset(this);
  pc_attack_anim_step(this);
}

// Move 7: the thrust (near: element-4 hits with sparks; far: the thrust wave).
void ClaudeUnit::move7() {
  BattleGlobals& g = bg();
  face_target(this, 0x80000);
  loop_limit = 1;
  const int lvl = pc_move_charge_level(this, killer_index);
  if (target_far == 0) {
    anim = 0;
    if (g.flags == 0) {
      camera_follow_start();
      Unit* units[8] = {};
      Vec4 points[8] = {};
      const int n = collider_attack_overlaps(this, units, points, 0);
      if (n != 0 && hit_landed == 0) {
        if (unit_attack_targets(this, n, units, points, lvl == 0 ? 0x180 : 0x200, 9999, 4) != 0) {
          bsfx_play_at(0x23, this);
          for (int i = 0; i < n; ++i) {
            for (int k = 0; k < 3; ++k) {
              if (Sprite* sp = sprite_spawn_from(3, 0, this, nullptr)) {
                const s32 dx = rng_spread_fixed(0x40);
                const s32 dy = rng_spread_fixed(0x40);
                sprite_set_pos(sp, points[i].x + dx, points[i].y + dy, points[i].z);
                sprite_set_fade(sp, 0x80, -4, 0x10);
                sp->layer = 2;
                const s32 sc = rng_between(0x1000, 0x1800) + lvl * 0x800;
                sp->scale_y = sc;
                sp->scale_x = sc;
              }
            }
          }
        }
      }
    }
  } else {
    anim = 1;
    if (g.flags == 0) {
      camera_follow_start();
      if (counters[1] == 0 && 7 < frame && g.state == kStateFighting) {
        if (Shot* s = shot_spawn_callback(cb_thrust)) {
          s->pos[0] = pos[0] + facing[0];
          s->pos[1] = pos[1] - 0x20000;
          s->pos[2] = pos[2];
          s->vel[0] = 0 < facing[0] ? 0x18000 : static_cast<s32>(0xFFFE8000);
          s->p130 = lvl * 0x80 + 0x100;
          pc_camera_frame_target(this, 0x18);
          pc_bind_shot(this, s, static_cast<s16>(lvl));
          bsfx_play_at(0x34, s);
        }
        counters[1] = 1;
      }
    }
  }
  pc_attack_anim_step(this);
}

// Move 8: the sword storm (one storm shot raining blades).
void ClaudeUnit::move8() {
  BattleGlobals& g = bg();
  face_target(this, 0x80000);
  anim = 0;
  loop_limit = 1;
  bobj_texcache_reset(this);
  const int lvl = pc_move_charge_level(this, killer_index);
  if (g.flags == 0) {
    camera_follow_start();
    if (counters[1] == 0 && 2 < frame && g.state == kStateFighting) {
      if (Shot* s = shot_spawn_callback(cb_storm)) {
        s->pos[0] = pos[0] + (facing[0] < 0 ? 0x50000 : -0x50000);
        s->pos[1] = pos[1] - 0xC0000;
        s->pos[2] = pos[2];
        s->facing[0] = facing[0], s->facing[1] = facing[1], s->facing[2] = facing[2];
        s->p130 = target_far;
        pc_bind_shot(this, s, static_cast<s16>(lvl));
      }
      bsfx_play_at(0x29, this);
      counters[1] = 1;
      pc_camera_frame_target(this, 0x3C);
    }
  }
  pc_attack_anim_step(this);
}

// Move 9: the dive slashes (jumps, then fires vortex shots down at an angle).
void ClaudeUnit::move9() {
  BattleGlobals& g = bg();
  const int lvl = pc_move_charge_level(this, killer_index);
  Unit* blocked = unit_try_move_scaled(this, vel, 7);
  if (g.flags == 0 && static_cast<u32>(counters[1] - 1) < 3 && blocked == nullptr) bobj_move(this, vel);
  const s32 phase = counters[1];
  if (phase == 0) {
    loop_limit = 0;
    bsfx_play_at(0x26, this);
    camera_follow_start();
    vel[1] = static_cast<s32>(0xFFFEC000);
    anim = 0;
    jumping = 1;
    airborne = 1;
    counters[1] = 1;
  }
  if (phase == 0 || phase == 1) {
    if (g.flags == 0) {
      vel[1] += g.frame_ticks * 0x800;
      if (-1 < vel[1]) counters[1] = 2;
    }
  } else if (phase == 2) {
    anim = 1;
    if (g.flags == 0) {
      vel[1] += g.frame_ticks * 0x800;
      if (0x1000 < vel[1]) vel[1] = static_cast<s32>(0xFFFFF000);
      if (vel[0] < 1) {
        if (vel[0] < 0) {
          vel[0] += g.frame_ticks * 0x800;
          if (0 < vel[0]) vel[0] = 0;
        }
      } else {
        vel[0] -= g.frame_ticks * 0x800;
        if (vel[0] < 0) vel[0] = 0;
      }
      if (0 < frame) {
        counters[2] -= g.frame_ticks;
        if (counters[2] < 0) {
          counters[2] += 0xC;
          vel[0] = bobj_facing_fx(this, -4);
          bsfx_play_at(0x23, this);
          if (Shot* s = shot_spawn_callback(cb_vortex)) {
            shot_place_at_unit(s, this, 0x80, 0, 0);
            s->vel[0] = bobj_facing_fx(this, 0x18);
            s->vel[1] = 0xC000;
            s->facing[0] = facing[0];
            pc_bind_shot(this, s, static_cast<s16>(lvl));
            pc_camera_frame_target(this, 0x24);
          }
          ++counters[3];
          if (lvl + 1 < counters[3]) counters[1] = 3;
        }
      }
    }
  } else if (phase == 3) {
    anim = 2;
    if (g.flags == 0) {
      vel[1] += g.frame_ticks * 0x800;
      const s32 ground = bobj_ground_height(this);
      bool landed = false;
      if (ground < pos[1]) {
        pos[1] = ground;
        landed = true;
      } else if (blocked != nullptr) {
        landed = true;
      }
      if (landed) {
        for (int i = 0; i < 8; ++i) effect_dust_puff(pos, 2);
        bsfx_play_at(0x1E, this);
        counters[1] = 4;
        anim = 3;
        bobj_anim_reset(this);
        loop_limit = 1;
      }
    }
  }
  pc_attack_anim_step(this);
}

// Move 10: the sweeping combo (an afterimage trail; the attack stat lowered for the
// knock-back hits; a hop at the end).
void ClaudeUnit::move10() {
  BattleGlobals& g = bg();
  const int lvl = pc_move_charge_level(this, killer_index);
  loop_limit = 1;
  camera_follow_start();
  anim = static_cast<u16>(counters[1] == 0 ? lvl : 3);
  if (counters[1] == 0 && g.state == kStateFighting) {
    if (static_cast<s16>(animate(0)) == -1) {
      counters[1] = 1;
      hit_landed = 0;
      counters[3] = 0;
      counters[2] = 0;
      bobj_anim_reset(this);
      pc_voice_attack_shout(this);
    }
  } else if (pc_attack_anim_step(this) != 0) {
    return;
  }
  if (effects[0] == nullptr) effects[0] = afterimage_create(this, (lvl + 1) * 2, 0x602008, 1);
  if (g.flags != 0) return;
  Unit* units[8] = {};
  Vec4 points[8] = {};
  const int n = collider_attack_overlaps(this, units, points, 0);
  bool check_clear = hit_landed != 0;
  if (n != 0 && hit_landed == 0) {
    const s16 kb = rds16(stats + 0x52);
    s32 angle = 9999;
    const int fr = frame;
    if (counters[1] == 1 || fr < 8 || static_cast<u32>(fr - 0xD) % 0x100 < 3 || 0x1B < fr) {
      angle = facing[0] < 0 ? 0xB8E : static_cast<s32>(0xFFFFFC72);
      wrs16(stats + 0x52, rds16(stats + 0x52) - 3);
    }
    wrs16(stats + 0x52, rds16(stats + 0x52) - 2);
    if (effects[0] != nullptr) effects[0]->next_kind = 1;
    unit_attack_targets(this, n, units, points, 0x100, angle, 0);
    wrs16(stats + 0x52, kb);
    if (Sprite* sp = unit_play_anim(this, static_cast<int>(rng_range(2)) + 5)) {
      sp->facing[0] = facing[0];
      sprite_set_pos_v(sp, &points[0].x);
      sprite_set_fade(sp, 0x80, -4, 4);
      sp->layer = 3;
    }
    check_clear = hit_landed != 0;
  }
  if (check_clear && (box_mask & 0xC) == 0) hit_landed = 0;
  if (counters[1] == 0) {
    vel[1] = 0;
    vel[2] = 0;
    vel[0] = bobj_facing_fx(this, 2);
  } else if (counters[1] == 1) {
    if (0 < frame && counters[2] == 0) {
      counters[2] = 1;
      vel[1] = static_cast<s32>(0xFFFF8000);
      vel[0] = bobj_facing_fx(this, 4);
      airborne = 1;
      jumping = 1;
    }
    if (counters[2] != 0) claude_jump_step(this, 0xC00);
  }
  if (g.flags == 0 && unit_try_move_scaled(this, vel, 7) == nullptr) bobj_move(this, vel);
}

}  // namespace

std::unique_ptr<PartyUnit> pc_claude_create() { return std::make_unique<ClaudeUnit>(); }

}  // namespace ss::game::battle
