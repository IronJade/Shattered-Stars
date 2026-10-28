// The spells' Shot kinds (0x80055d10-0x8005aee0) and the spawners of the ten spells
// spell_cast runs itself (0x80062f20-0x80063690). parts/p4.md "Shot kinds".

#include <cstring>

#include "game/battle/bsfx.h"
#include "game/battle/camera.h"
#include "game/battle/combat.h"
#include "game/battle/enemy_support.h"
#include "game/battle/loaders.h"
#include "game/battle/fx_draw.h"
#include "game/battle/render.h"
#include "game/battle/shot.h"
#include "game/battle/items.h"
#include "game/battle/spell_fx.h"
#include "game/battle/spells.h"
#include "game/battle/sprite.h"
#include "game/battle/stage.h"
#include "game/battle/unit.h"
#include "game/battle/unit_move.h"
#include "game/boot/rng.h"
#include "game/common/screen.h"
#include "game/title/new_game.h"
#include "psx/gte.h"
#include "runtime/libgpu.h"
#include "runtime/libgte.h"
#include "runtime/runtime.h"

namespace ss::game::battle {
namespace {

u8 image_u8(u32 address) {
  const u8* p = battle_data(address);
  return p != nullptr ? *p : 0;
}
u32 image_u32(u32 address) { return static_cast<u32>(battle_s32(address)); }

bool controlled_is(const Unit* u) {
  BattleGlobals& g = bg();
  return static_cast<const Unit*>(g.party_units[g.controlled_slot & 3]) == u;
}

void set_vel(BObj* o, const Vec4& v) {
  o->vel[0] = v.x;
  o->vel[1] = v.y;
  o->vel[2] = v.z;
  o->extra34 = v.w;
}

// INT / 8 and power / 16 (the spell Shots' power terms, rounded toward zero).
s32 int8(const Unit* u) {
  s32 v = u->st()->intel[2];
  if (v < 0) v += 7;
  return v >> 3;
}
s32 div_pow2(s32 v, int shift) {
  if (v < 0) v += (1 << shift) - 1;
  return v >> shift;
}

// 0x800567c4: a point on the sphere of radius r round c (angles a, b).
Vec4 sphere_point(const s32 c[3], s32 r, s32 a, s32 b) {
  const s32 ca = bcos(static_cast<u32>(a)), sa = bsin(static_cast<u32>(a));
  const s32 cb = bcos(static_cast<u32>(b)), sb = bsin(static_cast<u32>(b));
  Vec4 out;
  out.x = c[0] + ca * r;
  out.y = c[1] + ((sa * cb) >> 12) * r;
  out.z = c[2] + sb * r;
  out.w = 0;
  return out;
}

// 0x80056984: the skill camera on the controlled caster.
void shot_skill_camera_start(Shot* s) {
  BattleGlobals& g = bg();
  if (!controlled_is(s->owner) || g.flags != 0) return;
  g.director_timer = 0;
  s->owner->camera_focus = 1;
  camera_end_scheduled_now();
  camera_request_mode(g.camera, 7, s->owner);
}

// Kind 3 (0x80055d10): the cast aura in front of the caster (fx2 set, CLUT by element),
// growing to 0x1800; it fades once the caster's chant is over.
void shot_k03_cast_aura(Shot* s) {
  BattleGlobals& g = bg();
  bobj_share_sprite(g.sprite_bank_fx2.get(), s);
  s->anim = static_cast<u16>(s->p128 == 0 ? 1 : 0);
  s->clut_offset = image_u8(0x8006CEA8u + static_cast<u32>(s->power));
  Unit* o = s->owner;
  s->pos[0] = bobj_front_x(o, o->size[0] * 6);
  s->pos[1] = o->pos[1] + o->size[1] * -0x2000;
  s->pos[2] = o->pos[2] - 0x4000;
  if (s->p12c == 0 && (o->removed != 0 || o->action != 0xF || g.state != kStateFighting || o->counters[0] < 0))
    s->p12c = 1;
  bobj_project(s);
  bobj_anim_draw(s, g.flags);
  if (g.flags != 0) return;
  const s32 d = g.frame_ticks * 0x40;
  s->scale_x += d;
  s->scale_y += d;
  if (0x1800 < s->scale_x) {
    s->scale_x = 0x1800;
    s->scale_y = 0x1800;
  }
  shot_fade_out(s);
}

// Kind 4 (0x80055e74): a mote spiralling into +0x118 on a shrinking sphere, leaving
// sprites.
void shot_k04_orbit_trail(Shot* s) {
  BattleGlobals& g = bg();
  shot_use_anims_f920(s);
  s->anim = static_cast<u16>(s->p128 + 2);
  if (g.flags == 0) {
    const Vec4 p = sphere_point(s->point, s->p12c, s->p130, s->p134);
    s->pos[0] = p.x, s->pos[1] = p.y, s->pos[2] = p.z, s->pos_w = p.w;
    s->p12c += g.frame_ticks * -5;
    s->p130 += g.frame_ticks * 0x30;
    s->p134 += g.frame_ticks * 0x30;
    if (Sprite* sp = shot_spawn_sprite(s, s->anim + 1)) {
      sprite_set_pos_v(sp, s->pos);
      sprite_set_fade(sp, 0x80, -0x10, 0);
      sp->freeze_class = 1;
    }
    if (s->p12c < 1) {
      shot_kill(s);
      return;
    }
  }
  bobj_project(s);
  bobj_anim_draw(s, g.flags);
}

// Kind 5 (0x80055fc4): the release ring: 12 segments of two gouraud quads in the
// element's colour, growing and fading out.
void shot_k05_ring_burst(Shot* s) {
  BattleGlobals& g = bg();
  gte_set_view_matrix();
  const u32 rgb = image_u32(0x8006CEB4u + static_cast<u32>(s->power) * 4);
  for (int i = 0; i < 0xC; ++i) {
    const s32 a = (i << 12) / 0xC + s->p128;
    // ring_point_sv 0x800568b4: the corners on the ring round the Shot (x, y; z its own).
    const s32 rad[4] = {s->p12c, s->p12c << 3, s->p12c << 3, s->p12c * 0xC};
    const s32 ang[4] = {a, a - 0x71, a + 0x71, a};
    s16 pts[4][4];
    for (int k = 0; k < 4; ++k) {
      pts[k][0] = static_cast<s16>((s->pos[0] + bcos(static_cast<u32>(ang[k])) * rad[k]) >> 12);
      pts[k][1] = static_cast<s16>((s->pos[1] + bsin(static_cast<u32>(ang[k])) * rad[k]) >> 12);
      pts[k][2] = static_cast<s16>(s->pos[2] >> 12);
      pts[k][3] = 0;
    }
    s32 lvl = 0x100 - s->p130;
    if (lvl < 0) lvl = 0x103 - s->p130;
    u32 col[4];
    col[0] = scale_colour(rgb, lvl >> 2) | 0x3A000000u;
    col[1] = scale_colour(rgb, (0x100 - s->p130) / 2);
    col[2] = col[1];
    col[3] = scale_colour(rgb, 0x100 - s->p130);
    draw_quad_g4(pts, col, 1);
  }
  if (g.flags == 0 || s->owner->action != 0xF) {
    const s32 d = g.frame_ticks * 0x20;
    s->p12c += g.frame_ticks;
    s->p128 = (s->p128 + d) & 0xFFF;
    if (0x18 < s->p12c) {
      s->p130 += d;
      if (0x100 < s->p130) s->p130 = 0x100;
    }
    if (0x20 < s->p12c) shot_kill(s);
  }
}

// Kind 6 (0x800562a4): Antidote: the effect model (g_effect_data[0]) swelling over the
// target with rising sparkles; at the end the poison (status bit 3) is gone.
void shot_k06_antidote(Shot* s) {
  BattleGlobals& g = bg();
  Unit* t = s->target;
  s->pos[0] = t->pos[0];
  s->pos[1] = t->pos[1] - 0x8000;
  s->pos[2] = t->pos[2];
  s32 scale[3] = {s->p128, s->p128, s->p128};
  s32 orient[3] = {0, 0, 0};
  obj_set_matrix(s->pos, orient, scale);
  model_draw(g.effect_data[0], 0x7D89, 0x1A, 6);
  t->shadow_scale = 0;
  if (2 < g.state && s->p12c == 0) s->p12c = 1;
  if (g.flags != 0) return;
  bool shrink = s->p12c != 0;
  if (s->p128 < 0x2000 && !shrink) s->p128 += g.frame_ticks * 0x100;
  if (shrink) {
    s->p128 += g.frame_ticks * -0x100;
    if (s->p128 < 1) {
      shot_kill(s);
      t->st()->status &= 0xF7;
      t->shadow_scale = 0x100;
      shot_skill_camera_end(s);
      return;
    }
  } else {
    shot_use_anims_f920(s);
    if (Sprite* sp = shot_spawn_sprite(s, static_cast<int>(rng_range(2)) + 6)) {
      const s32 dx = rng_spread_fixed(100);
      const s32 dz = rng_spread_fixed(100);
      sprite_set_pos(sp, s->pos[0] + dx, s->pos[1], s->pos[2] + dz);
      sp->vel[1] = static_cast<s32>(0xFFFFF000);
      sp->accel[1] = static_cast<s32>(0xFFFFFC00);
      sprite_set_fade(sp, 0x80, -8, 0x14);
    }
  }
  if (controlled_is(s->owner)) camera_request_mode(g.camera, 5, s->target);
  s->p130 += g.frame_ticks;
  if (0x5A < s->p130 && s->p12c == 0) s->p12c = 1;
}

// Kind 7 (0x8005652c): a Firebolt charge: a fire ball spiralling in on a shrinking
// sphere; at the centre the first one (+0x128 == 0) launches the bolt (kind 8).
void shot_k07_firebolt_orbit(Shot* s) {
  BattleGlobals& g = bg();
  shot_use_anims_f920(s);
  s->anim = 0xB;
  shot_draw_with_shadow(s, 6, 0x60FF);
  if (g.flags != 0) return;
  const Vec4 p = sphere_point(s->point, s->p12c, s->p130, s->p134);
  s->pos[0] = p.x, s->pos[1] = p.y, s->pos[2] = p.z, s->pos_w = p.w;
  if (s->p128 == 0) shot_skill_camera_start(s);
  const s32 d = g.frame_ticks * 0x30;
  s->p12c += g.frame_ticks * -3;
  s->p130 += d;
  s->p134 += d;
  if (g.state == kStateFighting) {
    Sprite* sp = shot_spawn_sprite(s, static_cast<int>(rng_range(2)) + 8);
    sprite_set_pos_v(sp, s->pos);
    sprite_set_fade(sp, 0x80, -0x10, 0);
  }
  if (2 < g.state && g.end_countdown < 0x18 && s->p12c == 0) {
    shot_skill_camera_end(s);
    shot_kill(s);
    return;
  }
  if (0 < s->p12c) return;
  if (s->p128 == 0) {
    Shot* b = shot_spawn(8);
    if (b != nullptr) {
      shot_place_at_unit(b, s, 0, 0, 0);
      set_vel(b, vec_velocity_toward(s->pos, s->target->pos, 0x18));
      b->power = s->power;
      b->target = s->target;
      b->owner = s->owner;
    }
    if (s->owner->camera_focus == 0) {
      shot_skill_camera_end(s);
    } else {
      const s32 dist = vec_distance_xz_fx(s->target->pos, s->pos);
      camera_set_focus_unit(g.camera, s->owner, s->target);
      camera_schedule_end(7, s->owner, static_cast<s16>(dist / 0x18000 + 0x28));
    }
    bsfx_play_at(0x1F, b);
  }
  shot_kill(s);
}

// Kind 8 (0x80056a44): the Firebolt bolt: flies, hits (INT / 8 + power / 16 + 15, fire),
// leaves a scorch.
void shot_k08_firebolt(Shot* s) {
  BattleGlobals& g = bg();
  shot_use_anims_f920(s);
  s->anim = 10;
  const s32 i8 = int8(s->owner), p16 = div_pow2(s->power, 4);
  s->size[2] = 0x40;
  s->roll = static_cast<s16>(s->roll + static_cast<s16>(g.frame_ticks) * 0x20);
  if (static_cast<s16>(shot_draw_with_shadow(s, 6, 0x40FF)) == -1) {
    shot_skill_camera_end(s);
    shot_kill(s);
    return;
  }
  shot_handle_command(s, 0);
  if (g.flags != 0) return;
  if (shot_fade_out(s) != 0) {
    shot_skill_camera_end(s);
    return;
  }
  if (g.state == kStateFighting) {
    if (Sprite* sp = shot_spawn_sprite(s, static_cast<int>(rng_range(2)) + 8)) {
      sprite_set_pos_v(sp, s->pos);
      sprite_set_fade(sp, 0x80, -0x10, 0);
      sp->vel[1] = 0x1000;
    }
  }
  if (s->p12c == 0) {
    if (bobj_outside_arena(s)) {
      s->p12c = 1;
      return;
    }
    s->p128 += g.frame_ticks;
  }
  bobj_move(s, s->vel);
  if (s->hit_landed != 0) return;
  if (shot_hit_magic(s, i8 + p16 + 0xF, s->owner->is_enemy, 0x23, 4, 0, 0x10) == 0) return;
  shot_skill_camera_end(s);
  s32 anim = static_cast<s16>(s->power / 100) + 0xD;
  if (0xF < anim) anim = 0xF;
  if (Sprite* sp = sprite_spawn_b(1, 1, nullptr)) {
    sprite_set_pos_v(sp, s->pos);
    sp->layer = 1;
  }
  if (Sprite* sp = shot_spawn_sprite(s, anim)) {
    sprite_set_pos(sp, s->pos[0], bobj_ground_height(s), s->pos[2]);
    s32 w = s->ext_x1;
    if (w < 0) w = -w;
    sprite_set_quad(sp, static_cast<s16>(w << 3), 0x200040FF);
    sprite_set_fade(sp, 0x80, -2, 0);
    sp->layer = 1;
  }
  shot_kill(s);
}

// Kind 9 (0x80056d24): Ice Needle: forms at the caster (anim 0x10 once), then flies at
// the target (INT / 8 + power / 16 + 20, water) and shatters into a frost ring.
void shot_k09_ice_needle(Shot* s) {
  BattleGlobals& g = bg();
  shot_use_anims_f920(s);
  s->anim = static_cast<u16>(s->p128 != 0 ? 0xF : 0x10);
  s->loop_limit = s->p128 == 0 ? 1 : 0;
  if (static_cast<s16>(shot_draw_with_shadow(s, 5, 0xFFFFFFFF)) == -1 && s->p128 == 0) {
    bobj_anim_reset(s);
    s->anim = 0xF;
    shot_draw_with_shadow(s, 5, 0xFFFFFFFF);
    s->p128 = 1;
    set_vel(s, vec_velocity_toward_xz(s->pos, s->target->pos, 0x20));
    s->facing[0] = s->vel[0];
    if (s->owner->camera_focus != 0) {
      s32 dist = vec_distance_xz_fx(s->target->pos, s->pos);
      if (dist < 0) dist += 0x1FFFF;
      camera_set_focus_unit(g.camera, s->owner, s->target);
      camera_schedule_end(7, s->owner, static_cast<s16>((dist >> 0x11) + 0x28));
    }
  }
  if (2 < g.state && g.end_countdown < 0x18 && s->p12c == 0) {
    shot_skill_camera_end(s);
    shot_kill(s);
    return;
  }
  if (shot_fade_out(s) != 0) {
    shot_skill_camera_end(s);
    return;
  }
  shot_handle_command(s, 0);
  const s32 i8 = int8(s->owner), p16 = div_pow2(s->power, 4);
  if (g.flags != 0) return;
  if (s->p12c == 0) {
    if (bobj_outside_arena(s)) {
      s->p12c = 1;
      return;
    }
    if (s->p12c == 0) bobj_move(s, s->vel);
  }
  s->size[2] = 0x40;
  if (g.state == kStateFighting && s->p128 != 0) {
    if (Sprite* sp = shot_spawn_sprite(s, 0x10)) {
      sprite_set_pos_v(sp, s->pos);
      sprite_set_fade(sp, 0x80, -0x10, 0);
    }
  }
  if (s->hit_landed != 0) return;
  if (shot_hit_magic(s, i8 + p16 + 0x14, s->owner->is_enemy, 0x4C, 2, 0, 0x10) == 0) return;
  shot_skill_camera_end(s);
  if (Sprite* sp = shot_spawn_sprite(s, 0x12)) {
    sprite_set_pos(sp, s->pos[0], bobj_ground_height(s), s->pos[2]);
    s32 w = s->ext_x1;
    if (w < 0) w = -w;
    sprite_set_quad(sp, static_cast<s16>(w * 5));
    sprite_set_fade(sp, 0x80, -4, 0);
    sp->scale_vel[0] = 0x40;
    sp->scale_vel[1] = 0x40;
    sp->layer = 1;
  }
  s->p12c = 1;
  s->hit_landed = 1;
}

// Kind 0xa (0x800570a0): Thunderbolt: a bolt above the target, a spark ring at frame
// 0x14, the hit (INT / 8 + power / 16 + 35, thunder) and its burst.
void shot_k0a_thunderbolt(Shot* s) {
  BattleGlobals& g = bg();
  shot_use_anims_f920(s);
  s->anim = 0;
  s->loop_limit = 0;
  Unit* t = s->target;
  if (s->hit_landed == 0) {
    s->pos[0] = t->pos[0];
    s->pos[1] = t->pos[1] - 0x1A0000;
    s->pos[2] = t->pos[2];
  }
  s->size[2] = 0x80;
  bobj_project(s);
  bobj_anim_draw(s, 1);
  s->p128 += g.frame_ticks;
  if (0x10 < s->p128) {
    s->anim = 0x12;
    shot_fade_out(s);
    shot_draw_with_shadow(s, 8, 0x2000FFFF);
  }
  if (s->p130 == 0 && 0x14 < s->p128) {
    s->p130 = 1;
    const s32 at[3] = {s->pos[0], t->pos[1] - 0x10000, s->pos[2]};
    effect_spawn_sparks_at(at, 0x10, 0x28, 0x50, 0x10, 0x32, 2, 1, 0x10, 0);
  }
  if (0x16 < s->p128 && s->p12c == 0) s->p12c = 1;
  if (2 < g.state && g.end_countdown < 0x18 && s->p12c == 0) {
    shot_skill_camera_end(s);
    shot_kill(s);
    return;
  }
  shot_handle_command(s, 1);
  const s32 i8 = int8(s->owner), p16 = div_pow2(s->power, 4);
  if (g.flags != 0) return;
  if (controlled_is(s->owner)) camera_request_mode(g.camera, 5, s->target);
  if (s->hit_landed != 0) return;
  if (shot_hit_magic(s, i8 + p16 + 0x23, s->owner->is_enemy, 0x4C, 0x10, 0, 0x10) == 0) return;
  screen_shake_vertical(2, 2, 0x10);
  shot_skill_camera_end(s);
  shot_hit_burst(s, 0);
  s->p12c = 1;
  s->hit_landed = 1;
}

// Kind 0xb (0x800577e8): Grave: an earth column on the target throwing up rocks for
// power / 40 + 8 ticks; the hit (INT / 8 + power / 16 + 45, earth).
void shot_k0b_grave(Shot* s) {
  BattleGlobals& g = bg();
  shot_use_anims_f920(s);
  s->anim = 0x18;
  s->layer = -1;
  s->loop_limit = 1;
  Unit* t = s->target;
  if (s->hit_landed == 0) {
    s->pos[0] = t->pos[0];
    s->pos[1] = t->pos[1];
    s->pos[2] = t->pos[2];
  }
  s->p128 += g.frame_ticks;
  if (s->p128 <= static_cast<s16>(s->power / 0x28) + 8) {
    if (Sprite* sp = shot_spawn_sprite_at(s, static_cast<int>(rng_range(2)) + 0x1B, 0x10)) {
      sprite_set_pos_v(sp, t->pos);
      sprite_set_quad(sp, 0x10);
      sp->vel[0] = rng_spread_fixed(0x10);
      const s32 r = static_cast<s32>(rng_range(0x4000));
      sp->accel[1] = 0x1000;
      sp->vel[1] = -0xC000 - r;
      sp->roll_speed = 0x100;
      sp->layer = 1;
    }
  }
  s->size[2] = 0xA0;
  bobj_project(s);
  bobj_anim_draw(s, 1);
  s->anim = 0x19;
  s->layer = 1;
  const s16 f = static_cast<s16>(shot_draw_with_shadow(s, 8, 0xFFFFFFFF));
  if (f == -1 || s->p12c != 0 || (2 < g.state && g.end_countdown < 0x18)) {
    shot_skill_camera_end(s);
    shot_kill(s);
    return;
  }
  shot_handle_command(s, 1);
  const s32 i8 = int8(s->owner), p16 = div_pow2(s->power, 4);
  if (g.flags != 0) return;
  if (controlled_is(s->owner)) camera_request_mode(g.camera, 5, s->target);
  if (s->hit_landed != 0) return;
  if (shot_hit_magic(s, i8 + p16 + 0x2D, s->owner->is_enemy, 0x48, 1, 0, 0x10) == 0) return;
  screen_shake_vertical(2, 2, 0x10);
  shot_skill_camera_end(s);
  s->hit_landed = 1;
}

// Kind 0xc (0x80057ad0): Wounds: the target's animation speeds up (+0x14e to 0x200),
// then the wound mark (INT / 8 + power / 8 + 60, dark).
void shot_k0c_wounds(Shot* s) {
  BattleGlobals& g = bg();
  Unit* t = s->target;
  if (unit_is_down(t) && s->p12c == 0) s->p12c = 1;
  shot_use_anims_f920(s);
  s->anim = 0x1C;
  s->layer = -1;
  s->loop_limit = 0;
  if (s->hit_landed == 0) {
    s->pos[0] = t->pos[0];
    s->pos[1] = t->pos[1];
    s->pos[2] = t->pos[2];
  }
  if (t->shadow_scale < 0x200) t->shadow_scale = static_cast<s16>(t->shadow_scale + static_cast<s16>(g.frame_ticks) * 8);
  else s->p130 = 1;
  if (s->p130 != 0) {
    if (g.flags == 0) {
      s->p128 += g.frame_ticks;
      if (0x37 < s->p128 && s->p134 == 0) {
        s->p134 = 1;
        if (Sprite* sp = shot_spawn_sprite(s, 0x1E)) {
          sprite_attach_screen(sp, s, 0, 0);
          sprite_set_fade(sp, 0x90, -4, 0);
          sp->layer = 1;
        }
      }
      s->size[2] = 0x80;
      if (0x40 < s->p128 && s->p12c == 0) s->p12c = 1;
    }
    bobj_project(s);
    bobj_anim_draw(s, g.flags);
  }
  if (s->p12c == 0 || g.flags != 0) {
    if (2 < g.state && s->p12c == 0) s->p12c = 1;
    shot_handle_command(s, 1);
    const s32 i8 = int8(s->owner), p8 = div_pow2(s->power, 3);
    if (g.flags != 0) return;
    if (controlled_is(s->owner)) camera_request_mode(g.camera, 5, s->target);
    if (s->hit_landed == 0 && shot_hit_magic(s, i8 + p8 + 0x3C, s->owner->is_enemy, 0x48, 0x100, 0, 0x10) != 0)
      s->hit_landed = 1;
    return;
  }
  s->colour_mode = 2;
  s->rgb[0] = static_cast<u8>(static_cast<s8>(s->p12c) * -2 - 0x80);
  s->p12c += g.frame_ticks;
  if (0x38 < s->p12c) {
    t->shadow_scale = 0x100;
    shot_kill(s);
  }
}

// Kind 0xd (0x80057e20): Word of Death: two effect models over the target, ten rays,
// then the death roll (resist_roll kind 0 at power / 32 + 100: death +0x128 = 3).
void shot_k0d_word_of_death(Shot* s) {
  BattleGlobals& g = bg();
  Unit* t = s->target;
  if (unit_is_down(t) && s->p12c == 0) s->p12c = 1;
  shot_use_anims_f920(s);
  s->anim = 0x1E;
  s->loop_limit = 0;
  if (s->p138 == 0 && s->p12c == 0) {
    s->pos[0] = t->pos[0];
    s->pos[1] = t->pos[1] + (t->size[1] * 4 + 0xC0) * -0x1000;
    s->pos[2] = t->pos[2];
  }
  s32 scale[3] = {s->p130, s->p130, s->p130};
  s32 orient[3] = {0, 0, 0};
  obj_set_matrix(s->pos, orient, scale);
  model_draw(g.effect_data[1], 0x7DC7, 0x1A, 1);
  model_draw(g.effect_data[2], 0x7DC7, 0x1A, 3);
  shot_fade_out(s);
  if (s->p134 != 0) {
    bobj_project(s);
    bobj_anim_draw(s, g.flags);
  }
  if (g.flags != 0) return;
  if (s->p12c == 0) {
    s->p130 += g.frame_ticks * 0x100;
    if (0x1000 < s->p130) s->p130 = 0x1000;
  } else {
    s->p130 += g.frame_ticks * -0x80;
    if (s->p130 < 0) s->p130 = 0;
  }
  bool rays = s->p134 != 0;
  if (!rays && s->p130 == 0x1000) {
    s->p134 = 1;
    rays = true;
    s32 deg = 0;
    for (int i = 0; i < 10; ++i) {
      if (Sprite* sp = shot_spawn_sprite(s, i / 2 + 0x20)) {
        const s32 a = ((0x5A - deg) * 0x1000) / 0x168;
        const s32 c = bcos(static_cast<u32>(a)), sn = bsin(static_cast<u32>(a));
        sp->facing[0] = (i & 1) == 0 ? 0x1000 : static_cast<s32>(0xFFFFF000);
        sprite_attach(sp, s, c * 200, sn * 200, static_cast<s32>(0xFFFE0000));
        sprite_set_fade(sp, 0x80, -4, 0x1E);
        sp->layer = 1;
      }
      deg += 0x24;
    }
  }
  if (rays && s->p12c == 0) {
    s->p134 += g.frame_ticks;
    if (10 < s->p134 && s->p138 == 0) {
      s32 p = s->power;
      if (p < 0) p += 0x1F;
      int sfx;
      if (t->resist_roll(0, (p >> 5) + 100) == 0) {
        effect_rise_icon(t, 0);
        sfx = 0x1E;
      } else {
        t->death = 3;
        t->vanish_target[0] = s->pos[0];
        t->vanish_target[1] = s->pos[1];
        t->vanish_target[2] = s->pos[2];
        t->vanish_target[3] = s->pos_w;
        sfx = 0x33;
      }
      bsfx_play_at(sfx, t);
      s->p138 = 1;
    }
    if (0x32 < s->p134) s->p12c = 1;
  }
  if (controlled_is(s->owner)) camera_request_mode(g.camera, 5, s->target);
}

// Kind 0xe (0x80058270): Heal: sparkles circling the target as they rise; the first one
// sends p138 pairs more; at its fade the heal (INT / 4 + power / 2 + 100).
void shot_k0e_heal(Shot* s) {
  BattleGlobals& g = bg();
  Unit* t = s->target;
  bobj_share_sprite(g.sprite_bank_fx2.get(), s);
  s->clut_offset = 10;
  s->anim = static_cast<u16>(s->p128 + 2);
  if ((g.flags & 0xBF) == 0) {
    if (s->p134 == 0) {
      s->pos[0] = t->pos[0] + bcos(static_cast<u32>(s->p130)) * t->size[0] * 3;
      const s32 sn = bsin(static_cast<u32>(s->p130));
      s->p130 += g.frame_ticks * 0x40;
      s->vel[1] = 0x3000;
      s->pos[2] = t->pos[2] + sn * t->size[0] * 3;
      bobj_move(s, s->vel);
      if (t->pos[1] < s->pos[1]) {
        s->vel[1] = 0;
        s->p134 = 1;
        if (s->p12c == 0) s->p12c = 1;
      }
    } else {
      bobj_move(s, s->vel);
      s->vel[1] += g.frame_ticks * -0x1000;
    }
    if (controlled_is(s->owner) && g.flags == 0) camera_request_mode(g.camera, 5, s->target);
    if (0 < s->p138) {
      for (int i = 0; i < 2; ++i) {
        Shot* n = shot_spawn(0xE);
        if (n == nullptr) continue;
        n->owner = s->owner;
        n->target = s->target;
        n->pos[1] = t->pos[1] + t->size[1] * -0x4000;
        n->p130 = static_cast<s32>(rng_range(0x1000));
        n->p138 = -1;
        const s32 r = static_cast<s32>(rng_range(5));
        n->p128 = (r < 2 && -1 < r) ? r : r + 5;
      }
      --s->p138;
    }
  }
  if (shot_fade_out(s) == 0 || s->p138 < 0) {
    bobj_project(s);
    bobj_anim_draw(s, g.flags);
    return;
  }
  if (unit_is_down(t) || t->st()->hp == 0) return;
  s32 i4 = s->owner->st()->intel[2];
  if (i4 < 0) i4 += 3;
  const s32 heal = -((i4 >> 2) + div_pow2(s->power, 1) + 100);
  damage_popup_spawn(t, heal, static_cast<s32>(0xFFFFE000), 0);
  unit_damage_hp(t, heal);
  bsfx_play_at(0x13, t);
}

// Kind 0xf (0x800585cc): Cure Light: as Heal, a widening spiral for 60 ticks; heals
// INT / 2 + power + 800.
void shot_k0f_cure_light(Shot* s) {
  BattleGlobals& g = bg();
  Unit* t = s->target;
  bobj_share_sprite(g.sprite_bank_fx2.get(), s);
  s->clut_offset = 10;
  s->layer = 1;
  s->anim = static_cast<u16>(s->p128 + 2);
  if (g.flags == 0) {
    s->pos[0] = t->pos[0] + (bcos(static_cast<u32>(s->p130)) * t->size[0] * s->p134 * 4) / 0x3C;
    const s32 sn = bsin(static_cast<u32>(s->p130));
    const s32 w = t->size[0], k = s->p134;
    const s16 h = t->size[1];
    const s32 y = t->pos[1];
    s->p134 += g.frame_ticks;
    s->p130 += g.frame_ticks * -0x40;
    s->pos[1] = y + h * -0x2000 + (sn * w * k * 4) / 0x3C;
    if (0x3C < s->p134 && s->p12c == 0) s->p12c = 1;
    bobj_move(s, s->vel);
    if (controlled_is(s->owner)) camera_request_mode(g.camera, 5, s->target);
    if (0 < s->p138) {
      if (Shot* n = shot_spawn(0xF)) {
        n->owner = s->owner;
        n->target = s->target;
        n->pos[2] = t->pos[2] + rng_spread_fixed(0x80);
        n->p130 = static_cast<s32>(rng_range(0x1000));
        n->p138 = -1;
        const s32 r = static_cast<s32>(rng_range(6));
        n->p128 = r == 0 ? 0 : r + 1;
      }
      --s->p138;
    }
  }
  if (shot_fade_out(s) == 0 || s->p138 < 0) {
    bobj_project(s);
    bobj_anim_draw(s, g.flags);
    return;
  }
  if (unit_is_down(t) || t->st()->hp == 0) return;
  const s32 heal = -(div_pow2(s->owner->st()->intel[2], 1) + s->power + 800);
  damage_popup_spawn(t, heal, static_cast<s32>(0xFFFFE000), 0);
  unit_damage_hp(t, heal);
  bsfx_play_at(0x13, t);
}

// Kind 0x12 (0x80059310): a light streak (additive LINE_G2, head colour +0x12c, tail
// +0x130, length +0x138) homing on +0x118 at speed +0x134 growing by +0x128; at the end
// a puff by +0x10c (1 stage sprite, 2 / 3 effect sprites).
void shot_k12_line_streak(Shot* s) {
  BattleGlobals& g = bg();
  shot_set_gte_origin(s);
  set_vel(s, vec_velocity_toward(s->pos, s->point, 1));
  RenderContext* pool = s->pool != nullptr ? s->pool : g.prim_pool_a;
  if (arena_has_room(pool, 0x20)) {
    psx::Gte& q = psyq::gte();
    q.write_data(psx::Gte::VXY0, 0);
    q.write_data(psx::Gte::VZ0, 0);
    q.execute(psx::Gte::kRtps);
    const u32 head = q.read_data(psx::Gte::SXY2);
    auto on_screen = [](u32 sxy) {
      return static_cast<u16>(static_cast<s16>(sxy & 0xFFFF) + 0x28) < 0x1A4 && static_cast<u16>(static_cast<s16>(sxy >> 16) + 0x3C) < 300;
    };
    const s16 tx = static_cast<s16>((s->vel[0] * s->p138) >> 12);
    const s16 ty = static_cast<s16>((s->vel[1] * s->p138) >> 12);
    const s16 tz = static_cast<s16>((s->vel[2] * s->p138) >> 12);
    if (on_screen(head)) {
      q.write_data(psx::Gte::VXY0, static_cast<u32>(static_cast<u16>(tx)) | static_cast<u32>(static_cast<u16>(ty)) << 16);
      q.write_data(psx::Gte::VZ0, static_cast<u32>(static_cast<s32>(tz)));
      q.execute(psx::Gte::kRtps);
      const s32 flag = static_cast<s32>(q.read_data(psx::Gte::FLAG));
      const u32 tail = q.read_data(psx::Gte::SXY2);
      if (flag >= 0 && on_screen(tail)) {
        const s32 otz = static_cast<s32>(q.read_data(psx::Gte::OTZ));
        if ((otz >> 5) < 0x5FA) {
          auto* line = screen_alloc_prims<psyq::LINE_G2>(pool->arena, 1);
          auto* dm = screen_alloc_prims<psyq::DR_MODE>(pool->arena, 1);
          if (line != nullptr && dm != nullptr) {
            psyq::setLineG2(line);
            const u32 c0 = static_cast<u32>(s->p12c), c1 = static_cast<u32>(s->p130);
            std::memcpy(&line->r0, &c0, 3);
            line->code = 0x52;
            std::memcpy(&line->r1, &c1, 3);
            line->x0 = static_cast<s16>(head & 0xFFFF), line->y0 = static_cast<s16>(head >> 16);
            line->x1 = static_cast<s16>(tail & 0xFFFF), line->y1 = static_cast<s16>(tail >> 16);
            psyq::P_TAG* ot = &pool->ot_head[0x616 - (otz >> 5)];
            psyq::addPrim(ot, line);
            psyq::SetDrawMode(dm, 0, 1, 0x20, nullptr);
            psyq::addPrim(ot, dm);
            if (pool == g.prim_pool_a) pool_a_bytes_used() += 0x20;
          }
        }
      }
    }
  }
  if ((g.flags & 0xC) != 0) return;
  bool arrived = false;
  if (s->aux == 1) {
    // A 0x10-unit box: the streak ends where it runs into a unit, an obstacle or the arena
    // edge (the stage-8 lightning; battle agent).
    s->size[0] = s->size[1] = s->size[2] = 0x10;
    arrived = unit_check_move(s, s->vel, 7) != nullptr;
  }
  if (!arrived) {
    s->vel[0] = (s->vel[0] * s->p134) >> 12;
    s->vel[1] = (s->vel[1] * s->p134) >> 12;
    s->vel[2] = (s->vel[2] * s->p134) >> 12;
    bobj_move(s, s->vel);
    s->p134 += s->p128 * g.frame_ticks;
    s32 d = vec_distance_fx(s->pos, s->point);
    if (d < 0) d = -d;
    if (d > s->p134 * g.frame_ticks + s->p138) return;
    s->pos[0] = s->point[0];
    s->pos[1] = s->point[1];
    s->pos[2] = s->point[2];
  }
  Sprite* sp = nullptr;
  if (s->aux == 2) sp = shot_spawn_sprite_at(s, 2, 1);
  else if (s->aux == 3) sp = shot_spawn_sprite_at(s, 5, 1);
  else if (s->aux == 1) sp = sprite_spawn_c(1, 1, nullptr);
  if (sp != nullptr) {
    sprite_set_pos_v(sp, s->pos);
    sp->freeze_class = 2;
  }
  shot_kill(s);
}

// Kind 0x13 (0x8005978c): a falling meteor (a beam trail) accelerating to +0x118; the
// landing throws debris and sparks (+0x138 = the combo point kind: 2 nothing, 3 aim again).
void shot_k13_meteor(Shot* s) {
  BattleGlobals& g = bg();
  set_vel(s, vec_velocity_toward(s->pos, s->point, 1));
  const s32 dist = vec_distance_fx(s->pos, s->point);
  const s32 units = dist >> 12;
  draw_beam(s->pos, s->vel, 0x1020, 0x80B0, s->p12c < units ? s->p12c : units, 8, 0, 3);
  if ((g.flags & 0xC) != 0) return;
  s->vel[0] = (s->vel[0] * s->p134) >> 12;
  s->vel[1] = (s->vel[1] * s->p134) >> 12;
  s->vel[2] = (s->vel[2] * s->p134) >> 12;
  bobj_move(s, s->vel);
  if (s->p128 == 0) {
    s->p12c += g.frame_ticks * 0x100;
  } else {
    s->p12c -= (s->p134 * g.frame_ticks) >> 12;
    if (s->p12c < 0x80) s->p12c = 0x80;
    s->p134 += 0x10000;
    if (0x80000 < s->p134) s->p134 = 0x80000;
  }
  if (units <= s->p12c) s->p128 = 1;
  if (!(dist < s->p134 << 1)) return;
  if (s->p138 == 3) {
    s->p128 = 0;
    s->p12c = 0;
    s->p134 = 0;
    s->p138 = 0;
    s32 p[4];
    stage_random_point(p, 1);
    std::memcpy(s->point, p, 12);
    return;
  }
  if (s->p138 != 2) {
    const s32 ground = bobj_ground_height(s);
    if (s->p138 == 0) {
      const s32 at[3] = {s->pos[0], ground - 0x10000, s->pos[2]};
      effect_spawn_sparks_at(at, 10, 0x90, 0x120, 0x22, 0x3C, 0xE, 1, 0x20, 0);
      effect_spawn_sparks_at(at, 0xC, 0x100, 0x80, 0x28, 0x3C, 0, 1, 0x30, 0);
    }
    Sprite* last = nullptr;
    for (int i = 0; i < 5; ++i) {
      const int anim = i == 0 ? 2 : static_cast<int>(rng_range(3)) + 3;
      last = sprite_spawn_b(anim, 1, nullptr);
      if (last == nullptr) continue;
      const s32 dx = rng_spread_fixed(0x40);
      const s32 dy = static_cast<s32>(rng_range(0x10000));
      const s32 dz = rng_spread_fixed(0x40);
      sprite_set_pos(last, s->pos[0] + dx, (ground - dy) - 0x40000, s->pos[2] + dz);
      last->freeze_class = 1;
      if (i == 0) {
        const s32 sc = rng_between(0x800, 0x2000);
        last->scale_y = sc;
        last->scale_x = sc;
      } else {
        last->vel[0] = rng_spread_fixed(6);
        last->vel[2] = rng_spread_fixed(6);
        sprite_set_fade(last, 0x80, -4, 0x1E);
        const s32 sc = rng_between(0x1000, 0x5000);
        last->scale_y = sc;
        last->scale_x = sc;
        last->clut_offset = 0xB;
        last->size[2] = static_cast<s16>(rng_range(static_cast<u32>(i * 8 + 8)));
      }
    }
    bsfx_play_at(0x50, last);
    if (static_cast<u32>(rng_next() & 0x7FFF) < 0x1000) {
      ViewState& v = view();
      v.stage_colour[0] = v.stage_colour[1] = v.stage_colour[2] = 0x180;
      g.flash_active = 1;
      g.flash_delay = 2;
      g.flash_speed = 0x10;
    }
    screen_shake(3, 1, 2, 2, 4);
  }
  shot_kill(s);
}

// Kind 0x15 (0x80059be0): a healing orb (a turning star) homing on its target; at it, a
// burst of sparkles and the heal (power * 5 + 200).
void shot_k15_heal_orb(Shot* s) {
  BattleGlobals& g = bg();
  if ((g.flags & 0xBF) == 0) s->p130 += 0x71;
  s32 orient[3] = {0, 0, s->p130};
  const s32 grow = 0x1000 - s->p134;
  s32 scale[3] = {grow, grow, grow};
  if (s->p12c != 0) {
    s->p134 += g.frame_ticks * 0x80;
    if (0xF3C < s->p134) shot_kill(s);
  }
  obj_set_matrix(s->pos, orient, scale);
  s32 lvl = grow;
  if (lvl < 0) lvl = 0x100F - s->p134;
  draw_star(0xA0, 0x50, 0x20, scale_colour(0x9090, lvl >> 4), scale_colour(0x6060, lvl >> 4));
  if ((g.flags & 0xC) != 0) return;
  s32 big = static_cast<s16>(s->power / 100);
  if (2 < big) big = 2;
  if (Sprite* sp = shot_spawn_sprite(s, big * 2 + static_cast<int>(rng_range(2)) + 6)) {
    sprite_set_pos_v(sp, s->pos);
    sprite_set_fade(sp, 0x80, -4, 2);
  }
  if (s->p12c == 0) {
    Unit* t = s->target;
    const s32 aim[4] = {t->pos[0], t->pos[1] - 0x60000, t->pos[2] - 0x20000, t->pos_w};
    const s32 d = vec_distance_fx(s->pos, aim);
    const s32 acc = d < 0x3E8000 ? (d < 0x1F4000 ? 0x6000 : 0x3000) : 0x2000;
    for (int k = 0; k < 3; ++k) {
      if (s->pos[k] < aim[k]) {
        s->vel[k] += acc;
        if (0x18000 < s->vel[k]) s->vel[k] = 0x18000;
      } else {
        s->vel[k] -= acc;
        if (s->vel[k] < -0x18000) s->vel[k] = -0x18000;
      }
    }
    bobj_move(s, s->vel);
    if (d < 0xC8000 || 300 < s->p128) {
      s->p12c = 1;
      if (s->p138 == 0) {
        for (int i = 0; i < 4; ++i) {
          if (Sprite* sp = shot_spawn_sprite(s, 3)) {
            const s32 dx = rng_spread_fixed(0x60);
            const s32 dy = rng_between_fixed(-0xC0, 0);
            sprite_set_pos(sp, t->pos[0] + dx, t->pos[1] + dy, t->pos[2]);
            sp->roll_speed = 0x38;
            sprite_set_fade(sp, 0x80, -2, 0x10);
            sp->layer = 2;
          }
        }
        for (int i = 0; i < 8; ++i) {
          if (Sprite* sp = shot_spawn_sprite(s, static_cast<int>(rng_range(2)) + 4)) {
            const s32 dx = rng_spread_fixed(0x60);
            const s32 dy = rng_between_fixed(-0xE0, -0xC0);
            sprite_set_pos(sp, t->pos[0] + dx, t->pos[1] + dy, t->pos[2]);
            sp->vel[1] = rng_between_fixed(1, 2);
            sprite_set_fade(sp, 0x80, -2, 8);
            sp->layer = 3;
          }
        }
        if (!unit_is_down(t) && t->st()->hp != 0) {
          const s32 heal = -(s->power * 5 + 200);
          damage_popup_spawn(t, heal, static_cast<s32>(0xFFFFE000), 0);
          unit_damage_hp(t, heal);
          bsfx_play_at(0x13, t);
          bsfx_play_at(0x16, t);
        }
      }
    }
  }
  s->p128 += g.frame_ticks;
}

// Kind 0x1d (0x8005aee0): Wind Blade: a blade flying at the target (INT / 8 + power / 16
// + 25, wind), leaving a gust and power / 30 + 1 leaves at the hit.
void shot_k1d_wind_blade(Shot* s) {
  BattleGlobals& g = bg();
  shot_use_anims_f920(s);
  s->anim = 0x35;
  const s32 i8 = int8(s->owner), p16 = div_pow2(s->power, 4);
  s->size[2] = 0x60;
  bobj_project(s);
  bobj_anim_draw(s, g.flags);
  shot_handle_command(s, 0);
  if (g.flags != 0) return;
  if (shot_fade_out(s) != 0) {
    shot_skill_camera_end(s);
    return;
  }
  if (g.state == kStateFighting) {
    if (Sprite* sp = shot_spawn_sprite(s, 0x2B)) {
      sprite_set_pos_v(sp, s->pos);
      sprite_set_fade(sp, 0x80, -8, 8);
    }
  }
  if (s->p12c == 0 && bobj_outside_arena(s)) {
    s->p12c = 1;
    return;
  }
  bobj_move(s, s->vel);
  s->p128 -= g.frame_ticks;
  if (s->owner->camera_focus != 0 && 0 < s->p128) {
    s32 dist = vec_distance_xz_fx(s->target->pos, s->pos);
    if (dist < 0) dist += 0x1FFFF;
    camera_set_focus_unit(g.camera, s->owner, s->target);
    camera_schedule_end(7, s->owner, static_cast<s16>((dist >> 0x11) + 0x28));
  }
  if (s->hit_landed != 0) return;
  if (shot_hit_magic(s, i8 + p16 + 0x19, s->owner->is_enemy, 0x4C, 8, 0, 0x10) == 0) return;
  shot_skill_camera_end(s);
  if (Sprite* sp = shot_spawn_sprite(s, 0x36)) {
    sp->scale_vel[0] = 0x18;
    sp->scale_vel[1] = 0x18;
    sp->layer = 1;
    sprite_set_pos_v(sp, s->pos);
    sprite_set_fade(sp, 0x80, -4, 0);
  }
  s32 leaves = static_cast<s16>(s->power / 0x1E) + 1;
  if (8 < leaves) leaves = 8;
  for (int i = 0; i < leaves; ++i) {
    Sprite* sp = shot_spawn_sprite(s, 0x35);
    if (sp == nullptr) continue;
    const s32 w = s->target->size[0];
    const s32 dx = rng_between_fixed(w * -2, w << 1);
    const s32 dy = rng_between_fixed(0x30, 0xA0);
    sprite_set_pos(sp, s->pos[0] + dx, s->pos[1] - dy, s->pos[2] - 0x4000);
    sprite_set_fade(sp, 0x80, -2, 4);
    sp->layer = 2;
  }
  shot_kill(s);
}

// Kind 0x31 (0x8005d110): a vortex mesh (draw_vortex) round the x axis turned upright,
// rings +0x128 x segments +0x12c, width +0x138 growing by +0x130, step vel x, colours
// +0x118 / +0x11c; the stage greys while it lives, back over the last 32 ticks.
void shot_k31_vortex(Shot* s) {
  BattleGlobals& g = bg();
  stage_colour_set_grey(s->p134 < 0x20 ? s->p134 << 3 : 0x100);
  VortexParams p;
  p.radius = 0x4000;
  p.base = 0;
  p.width = s->p138;
  p.step = s->vel[0];
  p.rings = s->p128;
  p.segments = s->p12c;
  p.colour_a = static_cast<u32>(s->point[0]);
  p.colour_b = static_cast<u32>(s->point[1]);
  p.phase_span = 2;
  p.phase = (s->point[2] & 0x3F) * -0x80 + 0x1FFF;
  p.twist_freq = 1;
  p.twist_amp = 0x1000;
  p.blend = 2;
  p.ot_bias = 8;
  const s32 up[3] = {0, static_cast<s32>(0xFFF9C000), 0};
  s32 angles[3];
  vec_to_angles(angles, up);
  static const s32 kUnitScale[3] = {0x1000, 0x1000, 0x1000};  // g_unit_scale 0x8006e34c
  obj_set_matrix(s->pos, angles, kUnitScale);
  draw_vortex(p);
  stage_colour_restore();
  if (s->power != 0 && (g.flags & 0x40) != 0) return;
  if ((g.flags & 0xBF) != 0) return;
  s->p138 += s->p130;
  s->p134 -= g.frame_ticks;
  s->point[2] += g.frame_ticks;
  if (s->p134 < 1) shot_kill(s);
}

}  // namespace

bool spell_shot_update(Shot* s) {
  switch (s->kind) {
    case 3: shot_k03_cast_aura(s); return true;
    case 4: shot_k04_orbit_trail(s); return true;
    case 5: shot_k05_ring_burst(s); return true;
    case 6: shot_k06_antidote(s); return true;
    case 7: shot_k07_firebolt_orbit(s); return true;
    case 8: shot_k08_firebolt(s); return true;
    case 9: shot_k09_ice_needle(s); return true;
    case 0xA: shot_k0a_thunderbolt(s); return true;
    case 0xB: shot_k0b_grave(s); return true;
    case 0xC: shot_k0c_wounds(s); return true;
    case 0xD: shot_k0d_word_of_death(s); return true;
    case 0xE: shot_k0e_heal(s); return true;
    case 0xF: shot_k0f_cure_light(s); return true;
    case 0x12: shot_k12_line_streak(s); return true;
    case 0x13: shot_k13_meteor(s); return true;
    case 0x15: shot_k15_heal_orb(s); return true;
    case 0x1D: shot_k1d_wind_blade(s); return true;
    case 0x2E: shot_k2e_item_effect(s); return true;
    case 0x2F: shot_k2f_bouncing_ball(s); return true;
    case 0x30: shot_k30_bomb(s); return true;
    case 0x31: shot_k31_vortex(s); return true;
    default: return false;
  }
}

void shot_hit_burst(Shot* s, int level) {
  Unit* t = s->target;
  s32 n = static_cast<s16>(s->power / 0x32) + 2;
  if (8 < n) n = 8;
  for (int i = 0; i < n; ++i) {
    Sprite* sp = shot_spawn_sprite_at(s, static_cast<int>(rng_range(2)) + 0x14, 2);
    if (sp == nullptr) continue;
    const s32 dx = rng_spread_fixed(0x40);
    const s32 dy = static_cast<s32>(rng_range(0x7FFFF));
    sprite_set_pos(sp, t->pos[0] + dx, (t->pos[1] - dy) - 0x40000, t->pos[2]);
    sp->layer = 1;
    sp->roll_speed = 0x80;
    sp->flash = 0xE0;
  }
  n = static_cast<s16>(s->power / 0x28) + 4;
  if (0xC < n) n = 0xC;
  for (int i = 0; i < n; ++i) {
    Sprite* sp = shot_spawn_sprite(s, static_cast<int>(rng_range(2)) + 9);
    if (sp == nullptr) continue;
    sprite_set_pos(sp, t->pos[0], t->pos[1] + t->size[1] * -0x2000, t->pos[2]);
    sprite_set_fade(sp, 0x80, -4, 0);
    sp->layer = 1;
    sp->vel[0] = rng_spread_fixed(8);
    sp->vel[1] = rng_spread_fixed(8);
    sp->vel[2] = rng_spread_fixed(8);
  }
  if (level == 0) return;
  n = static_cast<s16>(s->power / 0x28) + 2;
  if (8 < n) n = 8;
  for (int i = 0; i < n; ++i) {
    Sprite* sp = shot_spawn_sprite(s, static_cast<int>(rng_range(2)) + 0x16);
    if (sp == nullptr) continue;
    sprite_set_pos(sp, t->pos[0], t->pos[1] + t->size[1] * -0x2000, t->pos[2]);
    sprite_set_fade(sp, 0x80, -4, 0);
    sp->layer = 1;
    const u32 a = rng_range(0x1000);
    sp->vel[0] = bcos(a) << 2;
    sp->vel[1] = bsin(a) << 2;
    sp->vel[2] = 0;
    sp->roll = static_cast<s16>(a);
  }
  if (level < 2) return;
  n = static_cast<s16>(s->power / 0x32) + 2;
  if (8 < n) n = 8;
  for (int i = 0; i < n; ++i) {
    Sprite* sp = shot_spawn_sprite(s, 0x16);
    if (sp == nullptr) continue;
    const s32 dx = rng_spread_fixed(0x40);
    const s32 dy = rng_spread_fixed(0x40);
    sprite_set_pos(sp, t->pos[0] + dx, t->pos[1] - dy, t->pos[2]);
    sp->roll_speed = 0x100;
    sprite_set_fade(sp, 0x80, -3, 0);
    sp->layer = 1;
  }
}

void shot_handle_command(Shot* s, int stationary) {
  switch (s->command) {
    case 1: s->p12c = 1; break;
    case 2: s->power = static_cast<s16>(s->power + s->aux); break;
    case 3:
      if (stationary != 0) {
        s->p12c = 1;
        break;
      }
      s->vel[0] = -s->vel[0];
      s->vel[2] = -s->vel[2];
      s->vel[1] = -s->vel[1];
      break;
    default: break;
  }
  s->command = 0;
}

// ---- The ten spells' spawners ----

// 0x80080c7c (#1984.0) voice_thank_healer: when a member heals another member it likes
// enough (affection config +0x4b + target * 12 + healer, plus twice the +0xdb byte, above 7),
// the target thanks the healer at chance affection * 0x280 / 0x8000: one of 3 lines (2 for
// Claude) from the target's voice table (battle_voice_table 0x80080b88: #1984.0 0x80081608 +
// 0x18 * (character - 1)) for the healer.
void voice_thank_healer(Unit* healer, Unit* target) {
  BattleGlobals& g = bg();
  if (unit_is_down(target) || unit_is_down(healer) || healer == target) return;
  if (target->is_enemy != 0 || healer->is_enemy != 0) return;
  const int tid = target->st()->id, hid = healer->st()->id;
  const u8* row = g.config + tid * 0xC + hid;
  const u32 affection = static_cast<u32>(row[0x4B]) + static_cast<u32>(row[0xDB]) * 2;
  if (affection < 8 || affection * 0x280 < static_cast<u32>(rng_next() & 0x7FFF)) return;
  const int pick = static_cast<int>(rng_range(tid == 1 ? 2u : 3u));
  const u8* table = (tid < 1 || 0xC < tid) ? nullptr : ovl1984_data(0x80081608u + static_cast<u32>(tid - 1) * 0x18);
  if (table == nullptr) return;  // (the console reads a null table)
  const int entry = (table[(hid - 1) * 2] | table[(hid - 1) * 2 + 1] << 8) + pick;
  voice_play_entry(entry, target);
}

void spell_firebolt(Unit* caster, Unit* target, int power) {
  s32 n = power / 0x32 + 2;
  if (8 < n) n = 8;
  for (int i = 0; i < n; ++i) {
    Shot* s = shot_spawn(7);
    if (s == nullptr) continue;
    s->point[0] = bobj_front_x(caster, caster->size[0] * 6);
    s->point[1] = caster->pos[1] + caster->size[1] * -0x2000;
    s->owner = caster;
    s->target = target;
    s->p128 = i;
    s->p12c = 0xC0;
    s->point[2] = caster->pos[2] - 0x4000;
    s->p130 = static_cast<s32>(rng_range(0x1000));
    s->p134 = static_cast<s32>(rng_range(0x1000));
    s->power = static_cast<s16>(power);
  }
}

void spell_ice_needle(Unit* caster, Unit* target, int power) {
  Shot* s = shot_spawn(9);
  if (s == nullptr) return;
  shot_place_at_unit(s, caster, caster->size[0] * 6, caster->size[1] << 1, 0);
  s->owner = caster;
  s->target = target;
  s->power = static_cast<s16>(power);
  s->pos[2] -= 0x4000;
  bsfx_play_at(0x51, s);
}

void spell_thunderbolt(Unit* caster, Unit* target, int power) {
  Shot* s = shot_spawn(10);
  if (s == nullptr) return;
  s->owner = caster;
  s->target = target;
  s->power = static_cast<s16>(power);
  spell_camera_focus(caster, target);
  bsfx_play_at(0x52, s);
}

void spell_grave(Unit* caster, Unit* target, int power) {
  Shot* s = shot_spawn(0xB);
  if (s == nullptr) return;
  s->owner = caster;
  s->target = target;
  s->power = static_cast<s16>(power);
  spell_camera_focus(caster, target);
}

void spell_wounds(Unit* caster, Unit* target, int power) {
  Shot* s = shot_spawn(0xC);
  if (s == nullptr) return;
  s->owner = caster;
  s->target = target;
  s->power = static_cast<s16>(power);
  bsfx_play_at(0x31, s);
}

void spell_word_of_death(Unit* caster, Unit* target, int power) {
  Shot* s = shot_spawn(0xD);
  if (s == nullptr) return;
  s->owner = caster;
  s->target = target;
  s->power = static_cast<s16>(power);
  bsfx_play_at(0x32, s);
}

void spell_heal(Unit* caster, Unit* target, int power) {
  Shot* s = shot_spawn(0xE);
  s32 n = power / 0x28 + 0x10;
  if (0x20 < n) n = 0x20;
  if (s == nullptr) return;
  bsfx_play_at(0x16, s);
  voice_thank_healer(caster, target);
  s->owner = caster;
  s->target = target;
  s->power = static_cast<s16>(power);
  s->pos[1] = caster->pos[1] + caster->size[1] * -0x4000;
  s->p130 = static_cast<s32>(rng_range(0x1000));
  s->p138 = n;
  const s32 r = static_cast<s32>(rng_range(5));
  s->p128 = (r < 2 && -1 < r) ? r : r + 5;
}

void spell_cure_light(Unit* caster, Unit* target, int power) {
  BattleGlobals& g = bg();
  Shot* s = shot_spawn(0xF);
  s32 n = power / 0x28 + 0x10;
  if (0x20 < n) n = 0x20;
  if (s == nullptr) return;
  voice_thank_healer(caster, target);
  s->owner = caster;
  s->target = target;
  s->power = static_cast<s16>(power);
  s->pos[2] = caster->pos[2];
  s->p130 = static_cast<s32>(rng_range(0x1000));
  s->p138 = n;
  const s32 r = static_cast<s32>(rng_range(5));
  s->p128 = (r < 2 && -1 < r) ? r : r + 5;
  s32 dy = -0x80;
  for (int i = 0; i < 3; ++i) {
    if (Sprite* sp = sprite_spawn_from(i + 0x25, 0, g.sprite_bank_fx.get(), nullptr)) {
      sp->loop_limit = 0;
      sprite_set_fade(sp, 0x80, -2, 0x1E);
      sprite_set_pos(sp, target->pos[0], target->pos[1] + (target->size[1] * 4 - dy) * -0x1000, target->pos[2]);
      sp->vel[0] = i << 0xD;
    }
    dy += 0x40;
  }
  bsfx_play_at(0x30, s);
}

void spell_antidote(Unit* caster, Unit* target) {
  Shot* s = shot_spawn(6);
  if (s != nullptr) {
    s->owner = caster;
    s->target = target;
  }
  bsfx_play_at(0x16, s);
  effect_spawn_sparks_on(target, 0x10, 8, 0x60, 5, 0x78, 3, 0, 0x1E, 0);
  effect_spawn_sparks_on(target, 0x10, 8, 0x40, 7, 100, 3, 0, 0x28, 0);
}

void spell_wind_blade(Unit* caster, Unit* target, int power) {
  Shot* s = shot_spawn(0x1D);
  if (s == nullptr) return;
  s->owner = caster;
  set_vel(s, vec_velocity_toward(caster->pos, target->pos, 0x10));
  s32 d = vec_distance_fx(caster->pos, target->pos);
  if (d < 0) d += 0xFFFF;
  s->p128 = d >> 0x10;
  s->pos[0] = caster->pos[0];
  s->pos[1] = caster->pos[1];
  s->pos[2] = caster->pos[2];
  s->pos_w = caster->pos_w;
  s->target = target;
  s->power = static_cast<s16>(power);
  bsfx_play_at(0x34, s);
}

}  // namespace ss::game::battle
