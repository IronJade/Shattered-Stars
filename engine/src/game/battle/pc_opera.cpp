// Opera's party-character module (#2153, slot-0 link 0x801ee000): the PartyUnit subclass
// (0x360 bytes, vtable 0x801f1e10: slots 31, 35, 36, 37, 45, 46), her 10 killer moves
// (states 0x10-0x19, direct pointers in the table 0x801f1f88) and the Shot callbacks they
// spawn, with three from #1984 block 1 that only her moves use (0x80082084 / 0x8008224c /
// 0x80082490, and their band drawer 0x80082b08). Move ids (record +0xcc byte - 1); names
// from what the code does: 0 an orb rising to burst into kind-2 spell orbs (spellcast 2),
// 1 a whirlwind of textured cones hitting every 16 ticks, 2 a bolt that freezes the first
// unit it meets (a disc under it), 3 homing bits with ribbon trails (lvl + 3), 4 near: a
// burst of ground waves, far: a shell lobbed at the target that splits into three waves,
// 5 near: rings spiralling out, far: a twister, 6 a hop shedding falling rocks, 7 a wisp
// and a shot that bursts into shards, 8 a lobbed ball calling a light sphere down on the
// unit it hits, 9 a long tunnel vortex hitting every 16 ticks. Her jump attacks (states 10
// and 11) fire a projectile; slot 45 picks move byte 1 when party members are low.
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
#include "game/battle/spell_fx.h"
#include "game/battle/spellcast.h"
#include "game/battle/sprite.h"
#include "game/battle/stage.h"
#include "game/battle/unit_move.h"
#include "game/boot/rng.h"
#include "game/title/new_game.h"

namespace ss::game::battle {
namespace {

const s32 kUnitScale[3] = {0x1000, 0x1000, 0x1000};  // 0x8006e34c

// A ribbon trail for the Shot (the console keeps it at +0x134), its source followed
// without the lift (+0xd = 1).
void shot_trail_on(Shot* s, u32 rgb_a, u32 rgb_b) {
  if (s->p134 != 0) return;
  s->trail = ribbon_trail_create(s, 0x14, rgb_a, rgb_b);
  if (s->trail == nullptr) return;
  s->trail->back = &s->trail;
  s->trail->no_lift = 1;
  s->p134 = 1;
}
void shot_trail_end(Shot* s) {
  if (s->p134 != 0 && s->trail != nullptr) s->trail->stop = 1;
}

// The point a unit is hit at: its middle.
void unit_middle(const Unit* u, s32 out[3]) {
  out[0] = u->pos[0];
  out[1] = u->pos[1] - u->size[1] * 0x2000;
  out[2] = u->pos[2];
}

// The enemies within 'reach' of the Shot along its facing (x in [x, x + reach) or
// (x - reach, x]) whose distance at the Shot's x is below 'radius' (20.12): the targets
// of the area moves, with their middles.
int area_targets(const Shot* s, s32 reach, s32 radius, Unit* found[8], Vec4 pts[8]) {
  BattleGlobals& g = bg();
  s32 lo = s->pos[0], hi = s->pos[0];
  if (s->facing[0] < 0) {
    lo -= reach;
  } else {
    hi += reach;
  }
  int n = 0;
  for (EnemyUnit* e : g.enemy_units) {
    Unit* u = e;
    if (unit_is_down(u)) continue;
    if (u->pos[0] < lo || hi <= u->pos[0]) continue;
    const s32 at[3] = {s->pos[0], u->pos[1], u->pos[2]};
    if (radius <= vec_distance_fx(at, s->pos)) continue;
    found[n] = u;
    s32 mid[3];
    unit_middle(u, mid);
    pts[n].x = mid[0], pts[n].y = mid[1], pts[n].z = mid[2];
    ++n;
  }
  return n;
}

// rng_next15 0x800103b4: rng_next() & 0x7fff.
u32 rng_next15() { return static_cast<u32>(rng_next()) & 0x7FFF; }

// 20.12 * 256ths, rounded toward zero (the console's (v + (v < 0 ? 0xff : 0)) >> 8).
s32 div256(s32 v) { return (v < 0 ? v + 0xFF : v) >> 8; }

// ---- Shot callbacks ------------------------------------------------------------------------

// 0x801efa88 (move 0): the orb rising from her; past y -1000 it becomes spellcast kind 2
// (the orbs that fall on the party) at its position, drawn with her sprite set.
void cb_orb(Shot* s) {
  BattleGlobals& g = bg();
  s->anim = 1;
  bobj_project(s);
  bobj_anim_draw(s, g.flags);
  if (g.flags != 0) return;
  shot_fade_out(s);
  bobj_move(s, s->vel);
  if (s->pos[1] >= -0x3E8000 || s->p12c != 0) return;
  if (SpellCast* sc = spellcast_create(2, s->owner, s->target, s->power, nullptr)) {
    sc->centre[0] = s->pos[0], sc->centre[1] = s->pos[1], sc->centre[2] = s->pos[2], sc->centre[3] = s->pos_w;
    // (+0x3c = the Shot's +0x134: the AnimSet the move stored there, Opera's own.)
    sc->s3c = 1;
    sc->orb_set = s->owner->animset;
  }
  shot_kill(s);
}

// 0x801f0b40 (move 1): a cone of 6 bands x 8 segments of the current frame's texture
// along her facing, radius growing with the band (length 'len', scaled by +0x10c / 256),
// turning with +0x12c.
void draw_cone(Shot* s, s32 len, const s32 uv[4], u16 clut, u16 tpage) {
  if (uv[1] == uv[3]) return;
  const u32 code = bobj_poly_tag(s, tpage);
  s32 near = 0, far = 0x80;
  for (int band = 0; band < 6; ++band, near += 0x80, far += 0x80) {
    const s32 a = near / 0x38, b = far / 0x38;
    const s32 ra = div256((a * a * len / 0xA4 + len / 2) * s->aux);
    const s32 rb = div256((b * b * len / 0xA4 + len / 2) * s->aux);
    s32 c[4][4] = {};
    const u32 ang0 = static_cast<u32>(s->p12c + band * band);
    c[1][0] = bobj_front_x(s, near);
    c[1][1] = s->pos[1] + bcos(ang0) * ra;
    c[1][2] = s->pos[2] + bsin(ang0) * ra;
    c[0][0] = bobj_front_x(s, far);
    c[0][1] = s->pos[1] + bcos(ang0) * rb;
    c[0][2] = s->pos[2] + bsin(ang0) * rb;
    c[3][0] = c[1][0];
    c[2][0] = c[0][0];
    for (int seg = 1; seg <= 8; ++seg) {
      const u32 ang = static_cast<u32>(seg * 0x200 + s->p12c + band * band);
      const s32 cs = bcos(ang), sn = bsin(ang);
      c[3][1] = c[1][1], c[3][2] = c[1][2];
      c[2][1] = c[0][1], c[2][2] = c[0][2];
      c[1][1] = s->pos[1] + cs * ra, c[1][2] = s->pos[2] + sn * ra;
      c[0][1] = s->pos[1] + cs * rb, c[0][2] = s->pos[2] + sn * rb;
      draw_textured_patch(uv, c, clut, tpage, s->layer, code, 1);
    }
  }
}

// 0x801f03d8 (move 1): the whirlwind: two cones (anims 6 and 5) and a spiral of patches
// (anim 7) along her facing; it grows while she holds the move (state 0x11, at most
// lvl * 8 + 70 ticks), shedding orbiting sparkles, and every 16 ticks hits the enemies
// within 0x300 ahead (power 0x40); then it shrinks away.
void cb_whirlwind(Shot* s) {
  BattleGlobals& g = bg();
  s->layer = 2;
  auto frame_of = [&](int anim, s32 uv[4], u16& clut, u16& tpage) {
    s->anim = static_cast<u16>(anim);
    bobj_anim_draw(s, g.flags | 0x9800u);
    const FrameUv& f = frame_uv();
    uv[0] = f.u0, uv[1] = f.v0, uv[2] = f.u1, uv[3] = f.v1;
    clut = f.clut, tpage = f.tpage;
  };
  s32 uv[4];
  u16 clut = 0, tpage = 0;
  frame_of(6, uv, clut, tpage);
  draw_cone(s, s->power * 4 + 0x3C, uv, clut, tpage);
  frame_of(5, uv, clut, tpage);
  draw_cone(s, s->power * 4 + 0xA0, uv, clut, tpage);
  frame_of(7, uv, clut, tpage);
  const s32 power = s->power;
  if (uv[1] != uv[3]) {
    const u32 code = bobj_poly_tag(s, tpage);
    const s32 r0 = div256(s->aux * 0x3C);
    const u32 a0 = static_cast<u32>(s->p12c << 1);
    s32 c[4][4] = {};
    c[2][0] = s->pos[0];
    c[2][1] = s->pos[1] + bcos(a0) * r0;
    c[2][2] = s->pos[2] + bsin(a0) * r0;
    c[0][0] = bobj_front_x(s, 0x40);
    c[0][1] = c[2][1];
    c[0][2] = c[2][2];
    s32 x = 0x20;
    for (int i = 1; i < 0x18; ++i, x += 0x20) {
      const s32 k = x / 0x38;
      const s32 r = div256((k * k * (power * 4 + 200) / 0xA4 + 0x5A) * s->aux);
      const u32 ang = static_cast<u32>(s->p12c + i * 0x200);
      const s32 cs = bcos(ang) * r, sn = bsin(ang) * r;
      c[3][0] = c[2][0], c[3][1] = c[2][1], c[3][2] = c[2][2];
      c[1][0] = c[0][0], c[1][1] = c[0][1], c[1][2] = c[0][2];
      c[2][0] = bobj_front_x(s, x);
      c[2][1] = s->pos[1] + cs;
      c[2][2] = s->pos[2] + sn;
      c[0][0] = bobj_front_x(s, x + 0xC0);
      c[0][1] = s->pos[1] + cs;
      c[0][2] = s->pos[2] + sn;
      draw_textured_patch(uv, c, clut, tpage, s->layer, code, 1);
    }
  }
  if (g.flags != 0) return;
  const s32 ticks = g.frame_ticks;
  s->p12c = (s->p12c + ticks * 0x28) & 0xFFF;
  s->p130 += ticks;
  const s32 life = power * 8 + 0x46;
  if (life < s->p130 || g.state != kStateFighting || s->owner->action != 0x11) {
    s->aux = static_cast<s16>(s->aux - ticks * 0x10);
    if (s->aux < 0) shot_kill(s);
    return;
  }
  s->aux = static_cast<s16>(s->aux + ticks * 0x10);
  if (0x100 < s->aux) s->aux = 0x100;
  if (rng_next15() < 0x4000 && s->p130 < life) {
    const s32 x = bobj_front_x(s, static_cast<s32>(rng_range(0x240)));
    if (Sprite* sp = shot_spawn_sprite(s, static_cast<int>(rng_range(2)) + 4)) {
      sprite_set_pos(sp, x, s->pos[1], s->pos[2]);
      sp->layer = 2;
      sprite_set_orbit(sp, 0, static_cast<s32>(rng_range(0x1000)), 0, power * 4 + 200, power * 4 + 200, 0, 0x38, 0);
      sprite_set_fade(sp, 0x80, -4, 0);
      sp->roll_speed = 0x5B;
    }
  }
  s->p134 -= ticks;
  if (s->p134 >= 0) return;
  s->p134 += 0x10;
  Unit* found[8] = {};
  Vec4 pts[8] = {};
  const int n = area_targets(s, 0x300000, 0xC8000, found, pts);
  if (unit_attack_targets(s->owner, n, found, pts, 0x40, s->facing[0] < 0 ? 0x800 : 0, 2) == 0) return;
  for (int i = 0; i < n; ++i) {
    if (Sprite* sp = shot_spawn_sprite(s, 0xB)) {
      sprite_set_pos_v(sp, &pts[i].x);
      const s32 sc = rng_between(0x1000, 0x2000);
      sp->scale_y = sc;
      sp->scale_x = sc;
      sprite_set_fade(sp, 0x80, -4, 8);
      sp->layer = 2;
    }
  }
}

// 0x801efb70 (move 2): the bolt; the first unit it meets resists or is frozen (super
// armour 8 for 90 ticks, a glowing disc under it, ice sprites); a resisted bolt shows the
// unit's "resist" icon.
void cb_bolt(Shot* s) {
  BattleGlobals& g = bg();
  s->anim = 2;
  s->size[2] = 0x20;
  if (s->hit_landed == 0 || s->p12c != 0) {
    shot_draw_and_box(s, 3, 0xFFFFFFFF);
    if (g.flags != 0) return;
    shot_fade_out(s);
    if (s->p12c == 0 && bobj_outside_arena(s)) s->p12c = 1;
    if (s->hit_landed == 0) bobj_move(s, s->vel);
    if (s->p12c != 0 || s->hit_landed != 0) return;
    Unit* found[8] = {};
    Vec4 pts[8] = {};
    if (collider_attack_overlaps(s, found, pts, 0) == 0) return;
    Unit* t = found[0];
    s->target = t;
    if (t->resist_roll(1, s->power + 0x78) != 0) {
      static_cast<PartyUnit*>(s->owner)->tex_timer = 0x80;
      s->hit_landed = 1;
      bsfx_play_at(0x51, t);
      if (Sprite* sp = shot_spawn_sprite(s, 6)) {
        sprite_set_pos_v(sp, t->pos);
        sp->layer = 2;
        sp->scale_x = t->size[0] << 6;
        sp->scale_y = t->size[1] << 6;
        sprite_set_fade(sp, 0x80, -2, 0);
      }
      for (int i = 0; i < s->power + 3; ++i) {
        int k = static_cast<int>(rng_range(3));
        if (k == 2) k = 3;
        if (Sprite* sp = shot_spawn_sprite(s, k + 4)) {
          const s32 dx = rng_spread_fixed(t->size[0] << 1);
          sprite_set_pos(sp, t->pos[0] + dx, bobj_ground_height(s), t->pos[2]);
          sp->layer = 2;
          sp->scale_y = 0;
          sp->scale_vel[1] = 0x100;
          sp->flags = static_cast<u8>(sp->flags | 4);
          sprite_set_fade(sp, 0x80, -2, 0);
        }
      }
      return;
    }
    effect_rise_icon(t, 0);
  } else {
    if (g.flags == 0) s->p134 += g.frame_ticks;
    Unit* t = s->target;
    if (s->p134 < 0x5B && !unit_is_down(t)) {
      t->super_armour = 8;
      const s32 at[3] = {t->pos[0], bobj_ground_height(t), t->pos[2]};
      s32 orient[3] = {0, 0, 0};
      obj_set_matrix(at, orient, kUnitScale);
      const u32 c0 = scale_colour(0xFFE0B0, 0xF0 - s->p134 * 2);
      const u32 c1 = scale_colour(0xF08020, 0xF0 - s->p134 * 2);
      draw_disc(0x20, t->size[0] * 5, c0, c1, 2, 4);
      return;
    }
    bsfx_play_at(0x29, t);
  }
  shot_kill(s);
}

// 0x801eff4c (move 3): a homing bit with a ribbon trail: steered at the target's middle
// (directly within 20 units, else accelerated 1 a tick on each axis); fades after 300
// ticks or when the target is down.
void cb_bit(Shot* s) {
  BattleGlobals& g = bg();
  s->anim = 3;
  s->size[0] = s->size[1] = s->size[2] = 0x14;
  s->layer = 2;
  shot_trail_on(s, 0x9020D0, 0x300890);
  if (static_cast<s16>(shot_draw_and_box(s, 4, 0xFFFFFFFF)) != -1) {
    if (g.flags != 0) return;
    s->p130 += g.frame_ticks;
    if (300 < s->p130 && s->p12c == 0) s->p12c = 1;
    if (shot_fade_out(s) == 0) {
      bobj_move(s, s->vel);
      if (!unit_is_down(s->target)) {
        s32 aim[3];
        unit_middle(s->target, aim);
        if (vec_distance_fx(aim, s->pos) < 0x140000) {
          const Vec4 v = vec_velocity_toward(s->pos, aim, 0xC);
          s->vel[0] = v.x, s->vel[1] = v.y, s->vel[2] = v.z, s->extra34 = v.w;
        } else {
          const s32 step = g.frame_ticks * 0x1000;
          s->vel[0] += aim[0] < s->pos[0] ? -step : step;
          s->vel[1] += aim[1] < s->pos[1] ? -step : step;
          s->vel[2] += aim[2] < s->pos[2] ? -step : step;
        }
      } else if (s->p12c == 0) {
        s->p12c = 1;
      }
      if (s->hit_landed != 0 || shot_hit_melee(s, 0x100, 0) == 0) return;
      static_cast<PartyUnit*>(s->owner)->tex_timer = 0x20;
      for (int i = 0; i < 2; ++i) {
        if (Sprite* sp = shot_spawn_sprite(s, 6)) {
          const s32 dx = rng_spread_fixed(0x60);
          const s32 dy = rng_between_fixed(-0xC0, 0);
          sprite_set_pos(sp, s->pos[0] + dx, s->pos[1] + dy, s->pos[2]);
          const s32 sc = rng_between(0x1800, 0x2800);
          sp->scale_y = sc;
          sp->scale_x = sc;
          sprite_set_quad(sp, static_cast<s16>(std::abs(static_cast<s32>(s->ext_x1)) << 2), 0x86080);
          sprite_set_fade(sp, 0x80, -4, 8);
          sp->layer = 2;
        }
      }
    }
  }
  shot_trail_end(s);
  shot_kill(s);
}

// 0x801f02ec (moves 4): a ground wave playing its animation twice, hitting once
// (element 4).
void cb_wave(Shot* s) {
  BattleGlobals& g = bg();
  s->layer = 2;
  s->loop_limit = 2;
  s->size[2] = 0x20;
  bobj_project(s);
  if (static_cast<s16>(bobj_anim_draw(s, g.flags)) == -1) {
    shot_kill(s);
    return;
  }
  if (shot_fade_out(s) != 0 || g.flags != 0) return;
  bobj_move(s, s->vel);
  if (s->hit_landed != 0) return;
  WeaponElementBits el(s->owner, 4);
  shot_hit_melee(s, 0x100, 0);
}

// 0x801f1bb4 (move 4, far): the lobbed shell; where it lands three waves fan out (120
// degrees apart, anim lvl + 9, scaled 2 x 3).
void cb_shell(Shot* s) {
  BattleGlobals& g = bg();
  s->layer = 2;
  s->anim = 5;
  s->size[0] = s->size[1] = s->size[2] = 0x14;
  shot_draw_and_box(s, 4, 0xFFFFFFFF);
  shot_trail_on(s, 0x9020D0, 0x300890);
  if (shot_fade_out(s) == 0) {
    if (g.flags != 0) return;
    bobj_move(s, s->vel);
    s->vel[1] += g.frame_ticks * 0x1000;
    if (s->pos[1] <= bobj_ground_height(s)) return;
    bsfx_play_at(0x50, s);
    s32 a = 0;
    for (int i = 0; i < 3; ++i, a += 0x78000) {
      Shot* w = shot_spawn_callback(cb_wave);
      if (w == nullptr) continue;
      w->pos[0] = s->pos[0];
      w->pos[1] = bobj_ground_height(s);
      w->pos[2] = s->pos[2];
      w->vel[0] = bcos(static_cast<u32>(a / 0x168)) * 6;
      w->vel[2] = bsin(static_cast<u32>(a / 0x168)) * 6;
      w->anim = static_cast<u16>(s->power + 9);
      w->power = s->power;
      w->scale_x = 0x2000;
      w->scale_y = 0x3000;
      w->owner = s->owner;
      bobj_share_sprite(s, w);
    }
  }
  shot_trail_end(s);
  shot_kill(s);
}

// 0x80082084 (#1984.1 shot_spiral_ring, move 5 near): a ring spiralling out from the
// point +0x118 / +0x120 (radius +0x12c growing 8 a tick, angle +0x128), fading (grey
// level 0x80 - +0x130) over 64 ticks, leaving a sprite every 6 ticks; hits once
// (element 0x10).
void cb_spiral_ring(Shot* s) {
  BattleGlobals& g = bg();
  s->anim = 7;
  s->size[2] = 0x30;
  s->colour_mode = 2;
  s->rgb[0] = static_cast<u8>(0x80 - s->p130);
  s->pos[0] = s->point[0] + bcos(static_cast<u32>(s->p128)) * s->p12c;
  s->pos[2] = s->point[2] + bsin(static_cast<u32>(s->p128)) * s->p12c;
  bobj_project(s);
  bobj_anim_draw(s, g.flags);
  if (g.flags != 0) return;
  s->p134 -= g.frame_ticks;
  if (s->p134 < 1) {
    s->p134 += 6;
    if (Sprite* sp = shot_spawn_sprite(s, 7)) {
      sprite_set_pos_v(sp, s->pos);
      sprite_set_fade(sp, 0x80, -8, 2);
    }
  }
  const s32 ticks = g.frame_ticks;
  s->p12c += ticks * 8;
  s->p128 = (s->p128 + ticks * 0x38) & 0xFFF;
  s->p130 += ticks * 2;
  if (0x7F < s->p130) {
    shot_kill(s);
    return;
  }
  if (s->hit_landed != 0) return;
  WeaponElementBits el(s->owner, 0x10);
  shot_hit_melee(s, 0x100, 0);
}

// 0x801f14bc (move 5 far): the twister, fading over 64 ticks; it hits up to lvl + 2
// times (element 0x10).
void cb_twister(Shot* s) {
  BattleGlobals& g = bg();
  s->anim = 5;
  s->size[2] = 0x40;
  s->colour_mode = 2;
  s->rgb[0] = static_cast<u8>(0x80 - s->p130);
  bobj_project(s);
  bobj_anim_draw(s, g.flags);
  if (g.flags != 0) return;
  s->p130 += g.frame_ticks * 2;
  if (0x7F < s->p130) {
    shot_kill(s);
    return;
  }
  if (s->hit_landed == 0) {
    WeaponElementBits el(s->owner, 0x10);
    shot_hit_melee(s, 0x100, 0);
  } else if (s->p12c < s->power + 1) {
    s->hit_landed = 0;
    ++s->p12c;
  }
}

// 0x8008224c (#1984.1 shot_falling_rock, move 6): a rock thrown forward and down; on a
// hit two sprites and her texture timer; on a hit or the ground (unless above y -300) two
// bursts of sparks.
void cb_rock(Shot* s) {
  BattleGlobals& g = bg();
  s->anim = 4;
  s->size[2] = 0x30;
  shot_draw_and_box(s, 3, 0xFFFFFFFF);
  if (shot_fade_out(s) != 0) {
    shot_kill(s);
    return;
  }
  if (g.flags != 0) return;
  bobj_move(s, s->vel);
  bool hit = false;
  if (s->hit_landed == 0 && shot_hit_melee(s, 0x100, 0) != 0) {
    hit = true;
    static_cast<PartyUnit*>(s->owner)->tex_timer = 0x2D;
    for (int i = 0; i < 2; ++i) {
      if (Sprite* sp = shot_spawn_sprite(s, 6)) {
        const s32 dx = rng_spread_fixed(0x60);
        const s32 dy = rng_between_fixed(-0xC0, 0);
        sprite_set_pos(sp, s->pos[0] + dx, s->pos[1] + dy, s->pos[2]);
        const s32 sc = rng_between(0x1000, 0x2000);
        sp->scale_y = sc;
        sp->scale_x = sc;
        sprite_set_fade(sp, 0x80, -4, 8);
        sp->layer = 2;
      }
    }
  }
  if (!hit && s->pos[1] <= bobj_ground_height(s)) return;
  if (-0x12C000 < s->pos[1]) {
    const s32 at[3] = {s->pos[0], bobj_ground_height(s), s->pos[2]};
    if (Shot* r = effect_spawn_sparks_at(at, 0xC, 0x40, 0xC0, 0x14, 0x2D, 0, 2, 0x30, 0)) {
      r->anim = 6;
      bobj_share_sprite(s, r);
    }
    if (Shot* r = effect_spawn_sparks_at(at, 0xC, 0x18, 0xE0, 0x18, 0x2D, 0, 2, 0x10, 0)) {
      r->anim = 7;
      bobj_share_sprite(s, r);
    }
  }
  shot_kill(s);
}

// 0x801f0f04 (move 7): the wisp: it flies 12 ticks with an afterimage, then hovers,
// bobbing, for 45 ticks (no hit).
void cb_wisp(Shot* s) {
  BattleGlobals& g = bg();
  s->anim = static_cast<u16>(s->p130 + 2);
  shot_draw_and_box(s, 3, 0xFFFFFFFF);
  if (shot_image(s) == nullptr && s->p130 == 0) shot_image_set(s, afterimage_create(s, 6, 0x703080, 1));
  if (shot_fade_out(s) != 0) {
    shot_image_end(s);
    shot_kill(s);
    return;
  }
  if (g.flags != 0) return;
  bobj_move(s, s->vel);
  if (s->p130 == 0) {
    s->p134 += g.frame_ticks;
    if (s->p134 < 0xC) return;
    s->p130 = 1;
    s->vel[0] = 0;
    s->vel[1] = 0x2000;
    s->p134 = s->pos[1];
    shot_image_reset(s);  // (+0x118: the afterimage ends and is let go)
    return;
  }
  s->pos[1] = s->p134 + bsin(static_cast<u32>(s->p128)) * 0x10;
  s->p128 = (s->p128 + g.frame_ticks * 0x40) & 0xFFF;
  s->aux = static_cast<s16>(s->aux + g.frame_ticks);
  if (s->aux < 0x2D) return;
  shot_kill(s);
}

// 0x801f129c (move 7): a shard of the burst falling with a ribbon trail; on a hit two
// sprites; on a hit or the ground sfx 0x50 and debris.
void cb_shard(Shot* s) {
  BattleGlobals& g = bg();
  s->anim = 5;
  s->size[0] = s->size[1] = s->size[2] = 0x18;
  shot_draw_and_box(s, 3, 0xFFFFFFFF);
  shot_trail_on(s, 0xC040F0, 0x4010A0);
  if (shot_fade_out(s) == 0) {
    if (g.flags != 0) return;
    bobj_move(s, s->vel);
    s->vel[1] += g.frame_ticks * 0x1000;
    bool hit = false;
    if (s->hit_landed == 0 && shot_hit_melee(s, 0x100, 0) != 0) {
      hit = true;
      for (int i = 0; i < 2; ++i) {
        if (Sprite* sp = shot_spawn_sprite(s, 7)) {
          const s32 dx = rng_spread_fixed(0x60);
          // ORIGINAL BUG (original_bugs.md F14): the y jitter is taken raw (under 0.05 of a
          // unit in 20.12) where the other shots' hit sprites use 192 units:
          //   const s32 dy = rng_between(-0xC0, 0);
          const s32 dy = rng_between_fixed(-0xC0, 0);
          sprite_set_pos(sp, s->pos[0] + dx, s->pos[1] + dy, s->pos[2]);
          const s32 sc = rng_between(0x1000, 0x2000);
          sp->scale_y = sc;
          sp->scale_x = sc;
          sprite_set_fade(sp, 0x80, -4, 8);
          sp->layer = 2;
        }
      }
    }
    if (!hit && s->pos[1] <= bobj_ground_height(s)) return;
    bsfx_play_at_pos(0x50, s->pos);
    const s32 spread[3] = {0x40000, 0x40000, 0x40000};
    effect_debris_burst(s->pos, spread, 3, 0, 1, 0x1000, 0);
  }
  shot_trail_end(s);
  shot_kill(s);
}

// 0x801f1098 (move 7): the shot that bursts after 12 ticks into lvl + 4 shards (lvl + 3
// in a ring, thrown up and out; the last one drops where it bursts).
void cb_burst(Shot* s) {
  BattleGlobals& g = bg();
  s->anim = 5;
  s->size[0] = s->size[1] = s->size[2] = 0x18;
  shot_draw_and_box(s, 3, 0xFFFFFFFF);
  shot_trail_on(s, 0x9020D0, 0x300890);
  if (shot_fade_out(s) == 0) {
    if (g.flags != 0) return;
    bobj_move(s, s->vel);
    s->p130 += g.frame_ticks;
    if (s->p130 < 0xC) return;
    shot_trail_end(s);
    s->p134 = 0;
    const int n = s->power + 3;
    bsfx_play_at_pos(0x28, s->pos);
    for (int i = 0; i < s->power + 4; ++i) {
      Shot* c = shot_spawn_callback(cb_shard);
      if (c == nullptr) continue;
      bobj_set_pos(c, s->pos);
      if (i == n) {
        // Original quirk (original_bugs.md F13, kept: the play-state rule): the last shard's
        // upward velocity is stored into the burst's own, so that shard keeps 0 and drops
        // from the burst point. Giving it the velocity moves the move's hits earlier.
        s->vel[1] = static_cast<s32>(0xFFFEC000);
      } else {
        const s32 a = (i << 12) / n;
        c->vel[0] = bcos(static_cast<u32>(a)) * 9;
        c->vel[1] = rng_between_fixed(-0x18, -0x16);
        c->vel[2] = bsin(static_cast<u32>(a)) * 9;
      }
      c->owner = s->owner;
      bobj_share_sprite(s, c);
    }
  }
  shot_trail_end(s);
  shot_kill(s);
}

// 0x801f1784 (move 8): the light sphere over the unit the ball hit: it grows for 70 ticks
// (sparkles and flashes while it is large), and at tick 46 the unit resists (status 0,
// chance lvl * 4 + 25) or is struck (power 0x200 + 0x10 a level); then it shrinks away.
void cb_light(Shot* s) {
  BattleGlobals& g = bg();
  Unit* t = s->target;
  s->pos[0] = t->pos[0];
  s->pos[1] = t->pos[1] - 0x1F4000;
  s->pos[2] = t->pos[2];
  stage_colour_set_grey(0x80);
  s32 zero[3] = {0, 0, 0};
  lights_build_oriented(zero);
  s32 orient[3] = {0, s->p134 << 4, 0};
  obj_set_matrix(s->pos, orient, kUnitScale);
  draw_sphere(0xC, 0xC, s->p128 << 1, 0x808080, s->p128, 0, 3, 0x1000);
  stage_colour_restore();
  if (g.flags != 0) return;
  if (rng_next15() < 12000 && 0x80 < s->p128 && g.state == kStateFighting) {
    static_cast<PartyUnit*>(s->owner)->tex_timer = 0x40;
    if (Sprite* sp = shot_spawn_sprite(s, static_cast<int>(rng_range(2)) + 3)) {
      const u32 a = rng_range(0x1000);
      const s32 r = static_cast<s32>(rng_range(0x50));
      sprite_set_pos(sp, s->pos[0] + bcos(a) * r, s->pos[1] + bsin(a) * r, s->pos[2]);
      sp->layer = 2;
      sprite_set_fade(sp, 0x80, -4, 8);
      sp->roll = static_cast<s16>(a);
      sp->scale_vel[0] = rng_between(0, 0x40);
      sp->scale_vel[1] = rng_between(0, 0x40);
      if (rng_next15() < 0x4000) {
        bsfx_play_at(0x1D, sp);
        ViewState& v = view();
        v.stage_colour[0] = v.stage_colour[1] = v.stage_colour[2] = 0x180;
        g.flash_active = 1;
        g.flash_delay = 2;
        g.flash_speed = 0x20;
      }
    }
  }
  s->p134 += g.frame_ticks;
  if (0x2D < s->p134 && s->p130 == 0) {
    const s16 power = s->power;
    s->p130 = 1;
    if (!unit_is_down(t)) {
      int sfx;
      if (t->resist_roll(0, power * 4 + 0x19) == 0) {
        s32 mid[3];
        unit_middle(t, mid);
        Vec4 pt{mid[0], mid[1], mid[2], 0};
        Unit* one[1] = {t};
        const int angle = t->pos[0] < s->owner->pos[0] ? 0x800 : 0;
        sfx = 0x48;
        if (unit_attack_targets(s->owner, 1, one, &pt, power * 0x10 + 0x200, angle, 0x100) == 0) sfx = 0;
      } else {
        t->death = 3;
        t->vanish_target[0] = s->pos[0];
        t->vanish_target[1] = s->pos[1];
        t->vanish_target[2] = s->pos[2];
        bsfx_play_at(0x33, t);
        sfx = 0x2A;
      }
      if (sfx != 0) bsfx_play_at(sfx, t);
    }
  }
  if (s->p134 < 0x47) {
    s->p128 += g.frame_ticks * 8;
    if (0x100 < s->p128) s->p128 = 0x100;
  } else {
    s->p128 -= g.frame_ticks * 8;
    if (s->p128 < 0) shot_kill(s);
  }
}

// 0x801f15e0 (move 8): the lobbed ball; the first unit it meets gets the light sphere.
void cb_ball(Shot* s) {
  BattleGlobals& g = bg();
  s->layer = 2;
  s->anim = 1;
  s->size[0] = s->size[1] = s->size[2] = 0x14;
  if (shot_fade_out(s) == 0 && bobj_ground_height(s) >= s->pos[1]) {
    shot_draw_and_box(s, 4, 0xFFFFFFFF);
    if (g.flags != 0) return;
    bobj_move(s, s->vel);
    s->vel[1] += g.frame_ticks * 0x1000;
    if (s->p12c != 0 || s->hit_landed != 0) return;
    Unit* found[8] = {};
    Vec4 pts[8] = {};
    if (collider_attack_overlaps(s, found, pts, 0) == 0) return;
    bsfx_play_at(0x30, s);
    bsfx_play_at(0x32, s);
    bsfx_play_at(0x52, s);
    if (Shot* l = shot_spawn_callback(cb_light)) {
      bobj_share_sprite(s, l);
      l->owner = s->owner;
      l->target = found[0];
      l->power = s->power;
    }
    shot_trail_end(s);
  }
  shot_kill(s);
}

// 0x80082b08 (#1984.1 shot_draw_ring_bands): a tube of 6 bands x 8 segments of the given
// texture rectangle along the Shot's facing (bands 0xa0 long; the first narrows to a
// tenth at the Shot), radius len * +0x10c / 256, turning with +0x12c.
void draw_ring_bands(Shot* s, const s32 uv[4], u16 clut, u16 tpage, s32 len) {
  if (uv[1] == uv[3]) return;
  const u32 code = bobj_poly_tag(s, tpage);
  s32 near = 0, far = 0xA0;
  s32 r_in = (len * s->aux) / 0xA00;
  for (int band = 0; band < 6; ++band, near += 0xA0, far += 0xA0) {
    const s32 r_out = (len * s->aux) >> 8;
    const u32 ang0 = static_cast<u32>(s->p12c + band * band);
    s32 c[4][4] = {};
    c[1][0] = bobj_front_x(s, near);
    c[1][1] = s->pos[1] + bcos(ang0) * r_in;
    c[1][2] = s->pos[2] + bsin(ang0) * r_in;
    c[0][0] = bobj_front_x(s, far);
    c[0][1] = s->pos[1] + bcos(ang0) * r_out;
    c[0][2] = s->pos[2] + bsin(ang0) * r_out;
    for (int seg = 1; seg <= 8; ++seg) {
      const u32 ang = static_cast<u32>(seg * 0x200 + s->p12c + band * band);
      const s32 cs = bcos(ang), sn = bsin(ang);
      for (int k = 0; k < 3; ++k) c[2][k] = c[0][k], c[3][k] = c[1][k];
      c[1][1] = s->pos[1] + cs * r_in, c[1][2] = s->pos[2] + sn * r_in;
      c[0][1] = s->pos[1] + cs * r_out, c[0][2] = s->pos[2] + sn * r_out;
      draw_textured_patch(uv, c, clut, tpage, s->layer, code, 1);
    }
    r_in = r_out;
  }
}

// 0x80082490 (#1984.1 shot_vortex_area, move 9): the tunnel vortex (two tubes of the
// frame's texture, scrolling) along her facing; it grows while she holds the move (state
// 0x19, at most lvl * 8 + 90 ticks), shedding sprites, and every 16 ticks hits the
// enemies within 0x3c0 ahead and lvl * 10 + 180 of its axis (power 0x80); then it
// shrinks away.
void cb_vortex_area(Shot* s) {
  BattleGlobals& g = bg();
  s->layer = 3;
  s->anim = 4;
  bobj_anim_draw(s, g.flags | 0x9800u);
  const FrameUv& f = frame_uv();
  s32 half = (f.u1 - f.u0) / 2;
  const s32 u0 = f.u0 + div256(s->p128 * half);
  const s32 uv[4] = {u0, f.v0, u0 + half, f.v1};
  draw_ring_bands(s, uv, f.clut, f.tpage, s->power * 5 + 100);
  draw_ring_bands(s, uv, f.clut, f.tpage, s->power * 10 + 0xA0);
  if (g.flags != 0) return;
  const s32 life = s->power * 8 + 0x5A;
  const s32 r9 = s->power * 10 + 200;
  const u32 a = rng_range(0x1000);
  const s32 x = s->pos[0];
  const s32 y = s->pos[1] + bsin(a) * r9;
  const s32 z = s->pos[2] + bcos(a) * r9;
  if (rng_next15() < 0x4000 && s->p130 < life) {
    for (int k = 0; k < 2; ++k) {
      if (Sprite* sp = shot_spawn_sprite(s, k != 0 ? 7 : 4)) {
        sprite_set_pos(sp, x, y, z);
        sprite_set_fade(sp, 0x80, -4, 2);
        sp->layer = 3;
        sp->facing[0] = s->facing[0];
      }
    }
  }
  if (rng_next15() < 0x4000 && s->p130 < life) {
    Sprite* sp = shot_spawn_sprite(s, 6);
    const s32 xx = bobj_front_x(s, rng_between(0, 0x3C0));
    if (sp != nullptr) {
      sprite_set_pos(sp, xx, y, z);
      sprite_set_fade(sp, 0x80, -4, 8);
      sp->layer = 3;
    }
  }
  for (int i = 0; i < g.frame_ticks; ++i) {
    const u32 a2 = rng_range(0x1000);
    if (life < s->p130) {
      const s32 xx = bobj_front_x(s, rng_between(0, 0x3C0));
      const s32 rr = s->power * s->aux * 10 + 0xA0;
      const s32 dy = div256(bsin(a2) * rr);
      const s32 dz = div256(bcos(a2) * rr);
      if (Sprite* sp = shot_spawn_sprite(s, 8)) {
        sprite_set_pos(sp, xx, s->pos[1] + dy, s->pos[2] + dz);
        sprite_set_fade(sp, 0x80, -4, 8);
        sp->layer = 3;
      }
    }
  }
  const s32 ticks = g.frame_ticks;
  s->p130 += ticks;
  s->p128 = (s->p128 + ticks * 0x10) & 0xFF;
  if (life < s->p130 || g.state != kStateFighting || s->owner->action != 0x19) {
    s->aux = static_cast<s16>(s->aux - ticks * 0x10);
    if (s->aux < 0) shot_kill(s);
    return;
  }
  s->aux = static_cast<s16>(s->aux + ticks * 0x10);
  if (0x100 < s->aux) s->aux = 0x100;
  s->p134 -= ticks;
  if (s->p134 >= 1) return;
  s->p134 += 0x10;
  Unit* found[8] = {};
  Vec4 pts[8] = {};
  const int n = area_targets(s, 0x3C0000, (s->power * 10 + 0xB4) * 0x1000, found, pts);
  unit_attack_targets(s->owner, n, found, pts, 0x80, s->facing[0] < 0 ? 0x800 : 0, 0x80);
}

// ---- The unit ---------------------------------------------------------------------------

struct OperaUnit : PartyUnit {
  OperaUnit() {  // 0x801ee05c
    v2d8 = 1;
    v2da = 1;
    clut_row_check = 1;
  }
  void state_air_attack() override;  // [31] 0x801ef6b4
  int killer_move_cost(int which) override {  // [35] 0x801ee0a8
    int total = 0;
    for (int c = 0; c < 2; ++c) {
      const u8 move = record[0xCC + c * 2 + which];
      if (move != 0) total += unit_mp_cost(this, *battle_data(0x8006B2F3 + move));
    }
    return total;
  }
  int move_mp_cost(int index) override {  // [36] 0x801ee140
    const u8 move = record[0xCC + (index >> 1) * 2 + (index & 1)];
    return unit_mp_cost(this, move == 0 ? 0 : *battle_data(0x8006B2F3 + move));
  }
  void killer_move_state(int i) override {  // [37] 0x801ee028 (table 0x801f1f88)
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
  int forced_attack_choice() override;  // [45] 0x801ef8f4

  void move0();  // 0x801ee1fc
  void move1();  // 0x801ee34c
  void move2();  // 0x801ee524
  void move3();  // 0x801ee69c
  void move4();  // 0x801ee7c8
  void move5();  // 0x801eeab4
  void move6();  // 0x801eee20
  void move7();  // 0x801ef198
  void move8();  // 0x801ef3c8
  void move9();  // 0x801ef4e0

  // 0x801ee1a0: once per move (counters[1]), at frame f while the battle is on.
  bool once_at(int f) {
    BattleGlobals& g = bg();
    if (g.flags != 0 || counters[1] != 0 || frame < f || g.state != kStateFighting) return false;
    counters[1] = 1;
    return true;
  }
  // 0x801ef894: her effect animation (anim, mode) just behind her, facing her way.
  void effect_behind(int anim_no, int mode) {
    if (Sprite* sp = unit_play_anim_ex(this, anim_no, mode)) {
      sprite_place_near(sp, this, -0x60, 0, 0);
      sp->layer = 2;
      sp->facing[0] = facing[0];
    }
  }
  void face_target(s32 speed) {
    if (target != nullptr) facing[0] = pos[0] < target->pos[0] ? speed : -speed;
  }
};

// [31] 0x801ef6b4: both jump attacks fire a projectile at the target (state 10 past frame
// 3 with a ring of light, state 11 past frame 2).
void OperaUnit::state_air_attack() {
  BattleGlobals& g = bg();
  loop_limit = 1;
  anim = static_cast<u16>(action + 2);
  const s8 at_frame = action == 10 ? 3 : 2;
  if (static_cast<u16>(animate(0)) == 0xFFFF) {
    action_kind = 0;
    begin_action();
  }
  attack_recovery = 0;
  if (frame < at_frame) counters[1] = 0;
  if (g.flags != 0 || counters[1] != 0 || frame < at_frame) return;
  counters[1] = 1;
  bsfx_play_at(0x37, this);
  ShotDesc* d = shot_desc_init(0x13);
  d->speed = 0x20;
  d->life = 0x24;
  d->flags = 0x100006;
  d->target = target;
  const s32 from[4] = {bobj_front_x(this, 0x30), pos[1] - ((size[1] * 5) / 2) * 0x1000, pos[2], 0};
  if (at_frame == 3) particle_spawn_ring(from, 0xC, 8, 8, 0x1E, 0x808080, 0xFFFFFF, 8, 0x400);
  shot_spawn_projectile(this, from, d, this);
}

// [45] 0x801ef8f4: unless she waits after a spell, a button holding move byte 1 she has
// the MP for, when party members are low (one point under a third of their HP, another
// under a quarter; more than one point in all).
int OperaUnit::forced_attack_choice() {
  BattleGlobals& g = bg();
  if (wait_after_spell != 0) return -1;
  for (int which = 0; which < 2; ++which) {
    if (st()->mp < killer_move_cost(which)) continue;
    for (int c = 0; c < 2; ++c) {
      if (record[0xCC + c * 2 + which] != 1) continue;
      int low = 0;
      for (int i = 0; i < 4; ++i) {
        if (unit_is_down(g.party_units[static_cast<size_t>(i)])) continue;
        const Stats* ps = stats_of(g.party_slot[static_cast<size_t>(i)]);
        if (ps->hp < ps->hp_max / 3) ++low;
        if (ps->hp < ps->hp_max / 4) ++low;
      }
      if (1 < low) return which;
    }
  }
  return -1;
}

// Move 0: at frame 9 the orb rises (the camera watches it for 72 ticks); its power is the
// move's use count.
void OperaUnit::move0() {
  BattleGlobals& g = bg();
  anim = 0;
  loop_limit = 1;
  pc_attack_anim_step(this);
  const s16 uses = killer_charge[static_cast<size_t>(killer_index & 3)];
  if (once_at(9)) {
    if (Shot* s = shot_spawn_callback(cb_orb)) {
      shot_place_at_unit(s, this, 0, 0x100, 0);
      s->vel[1] = static_cast<s32>(0xFFFE0000);
      if (g.controlled_slot == slot) {
        camera_focus = 1;
        camera_request_mode(g.camera, 7, this);
        camera_set_lookat_unit(g.camera, this, s);
        camera_schedule_end(7, this, 0x48);
      }
      pc_bind_shot(this, s, uses);
      bsfx_play_at(0x37, this);
    }
  }
  if (g.flags == 0) camera_follow_start();
}

// Move 1: at frame 4 the whirlwind (with a flash sprite; the camera looks ahead) and a
// looping sound; the animation plays 8 times.
void OperaUnit::move1() {
  BattleGlobals& g = bg();
  face_target(0x20000);
  anim = 0;
  loop_limit = 8;
  bobj_texcache_reset(this);
  pc_attack_anim_step(this);
  const int lvl = pc_move_charge_level(this, killer_index);
  if (once_at(4)) {
    if (camera_focus != 0) {
      const s32 ofs[3] = {bobj_facing_fx(this, 0xF0), 0, 0};
      camera_set_lookat_offset(g.camera, ofs);
    }
    if (Shot* s = shot_spawn_callback(cb_whirlwind)) {
      shot_place_at_unit(s, this, 0x40, 0x80, 0);
      pc_bind_shot(this, s, static_cast<s16>(lvl));
      if (Sprite* sp = unit_play_anim(this, 9)) {
        sprite_set_pos_v(sp, s->pos);
        sprite_set_fade(sp, 0x80, -4, lvl * 8 + 0x28);
        sp->layer = 6;
      }
    }
    counters[2] = static_cast<s32>(bsfx_play_at(0x2D, this));
    pc_camera_frame_target(this, 0x50);
  }
  sound_handle_fade_out(static_cast<u32>(counters[2]), 0x1E, 8);
  if (g.flags != 0) {
    sound_handle_duck(static_cast<u32>(counters[2]), 8);
    if (g.flags != 0) return;
  }
  camera_follow_start();
}

// Move 2: at frame 2 a flash and the bolt at the target's middle.
void OperaUnit::move2() {
  BattleGlobals& g = bg();
  anim = 0;
  loop_limit = 1;
  pc_attack_anim_step(this);
  const int lvl = pc_move_charge_level(this, killer_index);
  if (once_at(2)) {
    if (Sprite* sp = unit_play_anim(this, 2)) {
      sprite_place_near(sp, this, 0x40, 0x80, 0);
      sprite_set_fade(sp, 0x80, -8, 2);
      sp->layer = 2;
    }
    if (target != nullptr) {
      if (Shot* s = shot_spawn_callback(cb_bolt)) {
        shot_place_at_unit(s, this, 0, 0x80, 0);
        s32 aim[3];
        unit_middle(target, aim);
        const Vec4 v = vec_velocity_toward(s->pos, aim, 0x18);
        s->vel[0] = v.x, s->vel[1] = v.y, s->vel[2] = v.z, s->extra34 = v.w;
        pc_bind_shot(this, s, static_cast<s16>(lvl));
        pc_camera_frame_target(this, 0x18);
        bsfx_play_at(0x37, this);
      }
    }
  }
  if (g.flags == 0) camera_follow_start();
}

// Move 3: lvl + 3 homing bits, one each 3 frames from frame 3 (anim 1 from level 1).
void OperaUnit::move3() {
  const int lvl = pc_move_charge_level(this, killer_index);
  anim = lvl != 0 ? 1 : 0;
  loop_limit = 1;
  pc_attack_anim_step(this);
  const s32 c = counters[1];
  if (frame <= c * 3 + 2 || lvl + 3 <= c) return;
  counters[1] = c + 1;
  bsfx_play_at(0x37, this);
  effect_behind(3, 1);
  if (Shot* s = shot_spawn_callback(cb_bit)) {
    shot_place_at_unit(s, this, 0xC0, 0x70, 0);
    s->vel[1] = rng_between_fixed(-0x18, 8);
    s->vel[0] = bobj_facing_fx(this, 4);
    s->vel[2] = rng_spread_fixed(0xC);
    pc_bind_shot(this, s, static_cast<s16>(lvl));
    bsfx_play_at(0x37, this);
  }
  pc_camera_frame_target(this, 0x18);
}

// Move 4: near the target, a ground wave every 16 ticks over frames 6-15 (a looping
// sound); far, at frame 8 a shell lobbed at the target.
void OperaUnit::move4() {
  BattleGlobals& g = bg();
  const int lvl = pc_move_charge_level(this, killer_index);
  loop_limit = 1;
  anim = static_cast<u16>(target_far);
  pc_attack_anim_step(this);
  if (target_far == 0) {
    camera_follow_start();
    if (g.flags == 0) {
      counters[1] -= g.frame_ticks;
      if (counters[1] < 1 && static_cast<u32>(static_cast<u8>(frame)) - 6 < 10) {
        if (counters[2] == 0) counters[2] = static_cast<s32>(bsfx_play_at(0x2D, this));
        counters[1] += 0x10;
        effect_behind(3, 1);
        if (Shot* s = shot_spawn_callback(cb_wave)) {
          shot_place_at_unit(s, this, 0x110, 0x40, 0);
          s->vel[1] = rng_between(-0x1000, 0x2000);
          s->vel[0] = bobj_facing_fx(this, 2);
          s->vel[2] = 0;
          s->anim = static_cast<u16>(lvl + 6);
          pc_bind_shot(this, s, static_cast<s16>(lvl));
        }
      }
    }
    sound_handle_fade_out(static_cast<u32>(counters[2]), 0x1E, 8);
    if (g.flags != 0) sound_handle_duck(static_cast<u32>(counters[2]), 8);
    return;
  }
  if (once_at(8)) {
    face_target(0x40000);
    effect_behind(4, 1);
    if (Sprite* sp = unit_play_anim(this, 5)) {
      sprite_place_near(sp, this, 0x70, 0x90, 0);
      sprite_set_fade(sp, 0x80, -4, 4);
      sp->layer = 2;
    }
    bsfx_play_at(0x1F, this);
    if (Shot* s = shot_spawn_callback(cb_shell)) {
      shot_place_at_unit(s, this, 0xC0, 0xA0, 0);
      s32 v[4];
      const s32 t = projectile_arc_velocity(v, s, target, 0x18, 0);
      s->vel[0] = v[0], s->vel[1] = v[1], s->vel[2] = v[2], s->extra34 = v[3];
      pc_bind_shot(this, s, static_cast<s16>(lvl));
      pc_camera_frame_target(this, (t >> 12) + 0x24);
    }
  }
  if (counters[1] == 0) camera_follow_start();
}

// Move 5: a flash and sparks, the wind-up (4 times), then near the target lvl + 3 rings
// spiralling out, far two sparkles and the twister.
void OperaUnit::move5() {
  const int lvl = pc_move_charge_level(this, killer_index);
  if (counters[1] == 0) {
    bobj_texcache_reset(this);
    if (counters[2] == 0) {
      counters[2] = 1;
      if (Sprite* sp = unit_play_anim(this, 5)) {
        sprite_set_pos_v(sp, pos);
        sprite_set_fade(sp, 0x80, -6, 4);
        sp->layer = 2;
      }
      if (Shot* r = effect_spawn_sparks_at(pos, 8, 0x20, 0xE0, 0, 0x32, 0, 2, 0xAA, 0)) {
        r->vel[0] = 0x16;
        r->anim = 9;
        bobj_share_sprite(this, r);
      }
      bsfx_play_at(0x52, this);
    }
    anim = 0;
    loop_limit = 4;
    if (static_cast<u16>(animate(0)) != 0xFFFF) return;
    counters[1] = 1;
    counters[2] = 0;
    bobj_anim_reset(this);
  } else if (counters[1] != 1) {
    pc_attack_anim_step(this);
    return;
  }
  loop_limit = 1;
  anim = static_cast<u16>(target_far + 1);
  if (target_far == 0) {
    for (int i = 0; i < lvl + 3; ++i) {
      if (Shot* s = shot_spawn_callback(cb_spiral_ring)) {
        s->point[0] = pos[0];
        s->pos[1] = pos[1];
        s->point[2] = pos[2];
        s->p128 = (i << 12) / (lvl + 3);
        pc_bind_shot(this, s, static_cast<s16>(lvl));
      }
    }
  } else {
    const s32 x = bobj_front_x(this, 0x70), y = pos[1] - 0x60000, z = pos[2];
    for (int i = 0; i < 3; i += 2) {
      if (Sprite* sp = unit_play_anim(this, i + 7)) {
        sprite_set_pos(sp, x, y, z);
        sprite_set_fade(sp, 0x80, -2, 0);
        sp->layer = static_cast<s8>(i + 2);
      }
    }
    effect_behind(4, 1);
    if (Shot* s = shot_spawn_callback(cb_twister)) {
      s->pos[0] = x, s->pos[1] = y, s->pos[2] = z;
      s->roll = static_cast<s16>(facing[0] < 0 ? 0x400 : 0xFC00);
      s->facing[0] = facing[0];
      pc_bind_shot(this, s, static_cast<s16>(lvl));
    }
  }
  bsfx_play_at(0x52, this);
  counters[1] = 2;
  pc_attack_anim_step(this);
}

// Move 6: a hop (she hovers, bobbing, drifting back as she throws) shedding a falling
// rock every 12 ticks from frame 7, lvl + 2 of them, then she drops (dust where she lands).
void OperaUnit::move6() {
  BattleGlobals& g = bg();
  const int lvl = pc_move_charge_level(this, killer_index);
  const bool blocked = unit_try_move_scaled(this, vel, 7) != nullptr;
  if (g.flags == 0 && static_cast<u32>(counters[1] - 1) < 3 && !blocked) bobj_move(this, vel);
  switch (counters[1]) {
    case 0:
      loop_limit = 0;
      bsfx_play_at(0x26, this);
      camera_follow_start();
      vel[1] = static_cast<s32>(0xFFFEC000);
      anim = 0;
      jumping = 1;
      airborne = 1;
      counters[1] = 1;
      [[fallthrough]];
    case 1:
      if (g.flags == 0) {
        vel[1] += g.frame_ticks * 0x800;
        if (vel[1] >= 0) counters[1] = 2;
      }
      break;
    case 2: {
      anim = 1;
      if (g.flags != 0) break;
      vel[1] += g.frame_ticks * 0x800;
      if (0x1000 < vel[1]) vel[1] = static_cast<s32>(0xFFFFF000);
      if (0 < vel[0]) {
        vel[0] -= g.frame_ticks * 0x800;
        if (vel[0] < 0) vel[0] = 0;
      } else if (vel[0] < 0) {
        vel[0] += g.frame_ticks * 0x800;
        if (0 < vel[0]) vel[0] = 0;
      }
      if (frame <= 6) break;
      counters[2] -= g.frame_ticks;
      if (counters[2] >= 0) break;
      counters[2] += 0xC;
      vel[0] = bobj_facing_fx(this, -4);
      bsfx_play_at(0x37, this);
      if (Shot* s = shot_spawn_callback(cb_rock)) {
        shot_place_at_unit(s, this, 0x80, 0, 0);
        s->vel[0] = bobj_facing_fx(this, 0x18);
        s->vel[1] = 0xC000;
        pc_bind_shot(this, s, static_cast<s16>(lvl));
        pc_camera_frame_target(this, 0x24);
      }
      ++counters[3];
      if (lvl + 1 < counters[3]) counters[1] = 3;
      break;
    }
    case 3: {
      anim = 2;
      if (g.flags != 0) break;
      vel[1] += g.frame_ticks * 0x800;
      const s32 ground = bobj_ground_height(this);
      if (ground < pos[1]) {
        pos[1] = ground;
      } else if (!blocked) {
        break;
      }
      for (int i = 0; i < 8; ++i) effect_dust_puff(pos, 2);
      bsfx_play_at(0x1E, this);
      counters[1] = 4;
      anim = 3;
      bobj_anim_reset(this);
      loop_limit = 1;
      break;
    }
    default:
      break;
  }
  pc_attack_anim_step(this);
}

// Move 7: past frame 4 the wisp; at frame 6 a 12-tick pause; then past frame 3 of anim 1
// the bursting shot.
void OperaUnit::move7() {
  BattleGlobals& g = bg();
  const int lvl = pc_move_charge_level(this, killer_index);
  switch (counters[1]) {
    case 0:
      loop_limit = 0;
      anim = 0;
      if (g.flags != 0) break;
      if (frame < 6) {
        if (counters[2] == 0 && 4 < frame) {
          counters[2] = 1;
          bsfx_play_at(0x1F, this);
          if (Shot* s = shot_spawn_callback(cb_wisp)) {
            shot_image_reset(s);
            shot_place_at_unit(s, this, 0x60, 0x80, 0);
            s->vel[0] = bobj_facing_fx(this, 0x18);
            pc_bind_shot(this, s, static_cast<s16>(lvl));
          }
        }
      } else {
        counters[1] = 1;
        counters[2] = 0;
      }
      break;
    case 1:
      if (g.flags == 0) {
        counters[2] += g.frame_ticks;
        if (0xB < counters[2]) {
          counters[1] = 2;
          counters[2] = 0;
          bobj_anim_reset(this);
        }
      }
      break;
    case 2:
      loop_limit = 1;
      anim = 1;
      if (g.flags == 0 && counters[2] == 0 && 3 < frame) {
        counters[2] = 1;
        bsfx_play_at(0x37, this);
        if (Shot* s = shot_spawn_callback(cb_burst)) {
          shot_place_at_unit(s, this, 0x60, 0x80, 0);
          s->vel[0] = bobj_facing_fx(this, 0x18);
          pc_bind_shot(this, s, static_cast<s16>(lvl));
          pc_camera_frame_target(this, 0x24);
        }
      }
      break;
    default:
      break;
  }
  pc_attack_anim_step(this);
}

// Move 8: past frame 1 (framing the target each frame) the ball lobbed at the target;
// the animation plays 4 times.
void OperaUnit::move8() {
  BattleGlobals& g = bg();
  const int lvl = pc_move_charge_level(this, killer_index);
  anim = 0;
  loop_limit = 4;
  pc_attack_anim_step(this);
  if (g.flags == 0) camera_follow_start();
  if (frame <= 1) return;
  pc_camera_frame_target(this, 0x1E);
  if (counters[1] != 0) return;
  counters[1] = 1;
  bsfx_play_at(0x1F, this);
  if (Shot* s = shot_spawn_callback(cb_ball)) {
    shot_place_at_unit(s, this, 0xA0, 0xA0, 0);
    s32 v[4];
    projectile_arc_velocity(v, s, target, 0x18, 0);
    s->vel[0] = v[0], s->vel[1] = v[1], s->vel[2] = v[2], s->extra34 = v[3];
    pc_bind_shot(this, s, static_cast<s16>(lvl));
  }
}

// Move 9: at frame 11 the tunnel vortex (the camera looks far ahead and frames the target
// for 110 ticks; a looping sound and a falling tone); she holds anim 0 for lvl * 8 + 90
// ticks, then plays anim 1 once.
void OperaUnit::move9() {
  BattleGlobals& g = bg();
  const int lvl = pc_move_charge_level(this, killer_index);
  if (lvl * 8 + 0x5A < counters[2]) {
    anim = 1;
    loop_limit = 1;
  } else {
    anim = 0;
    loop_limit = 0;
  }
  bobj_texcache_reset(this);
  pc_attack_anim_step(this);
  if (g.flags == 0) {
    camera_follow_start();
    if (counters[1] != 0) counters[2] += g.frame_ticks;
    if (once_at(0xB)) {
      if (camera_focus != 0) {
        const s32 ofs[3] = {bobj_facing_fx(this, 500), 0, 0};
        camera_set_lookat_offset(g.camera, ofs);
      }
      effect_behind(3, 0xC);
      if (Shot* s = shot_spawn_callback(cb_vortex_area)) {
        shot_place_at_unit(s, this, 0x100, 0x60, 0);
        pc_bind_shot(this, s, static_cast<s16>(lvl));
      }
      pc_camera_frame_target_for(this, 0x6E);
      counters[3] = static_cast<s32>(bsfx_play_at(0x44, this));
      sound_handle_pitch_down(static_cast<u32>(bsfx_play_at(0x34, this)), 0x1E);
    }
  }
  sound_handle_fade_out(static_cast<u32>(counters[3]), 0x1E, 8);
  if (g.flags != 0) sound_handle_duck(static_cast<u32>(counters[3]), 8);
}

}  // namespace

std::unique_ptr<PartyUnit> pc_opera_create() { return std::make_unique<OperaUnit>(); }

}  // namespace ss::game::battle
