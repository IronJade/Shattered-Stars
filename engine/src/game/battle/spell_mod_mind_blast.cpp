// Spell code module c5209066cb (entry 0x97e): Mind Blast (spell 0x43), Vesper's boss spell.
// Sparks and a swirl of particles gather at a point in front of the caster for 150 ticks,
// then a turning tube of the data's sprite opens along x in front of him for 120 ticks and
// closes; every standing party member in the band it covers, near its axis, takes a
// physical hit (power 0x200). The module draws the caster itself from step 3 on. One
// function at 0x8008cc00 (block 0, 2908 bytes; its step jump table at 0x8008d740).

#include "game/battle/bsfx.h"
#include "game/battle/camera.h"
#include "game/battle/combat.h"
#include "game/battle/enemy_support.h"
#include "game/battle/render.h"
#include "game/battle/shot.h"
#include "game/battle/spellcast.h"
#include "game/battle/sprite.h"
#include "game/battle/unit.h"
#include "game/battle/unit_move.h"
#include "game/boot/rng.h"

namespace ss::game::battle {
namespace {

// The SpellCast's fields this module uses: s20 the gathering ticks (step 2), then the tube's
// opening 0..0x100 (steps 3-4); s24 the tube's turn; s28 the ticks of step 3; s3c the charge
// sound's handle; obj the data's sprite set (anim 0, the tube) and the tube's origin
// (obj.pos: 0x5c in front of the caster, 0xb4 up; obj.facing[0] the caster's).

// Steps >= 3: the caster drawn by the module (projected, its cel, a shadow as wide as its
// last frame's extents).
void draw_caster(Unit* u) {
  BattleGlobals& g = bg();
  bobj_project(u);
  bobj_anim_draw(u, g.flags & 0xC);
  s32 a = u->ext_x1, b = u->ext_x0;
  if (a < 0) a = -a;
  if (b < 0) b = -b;
  if (a < b) a = b;
  const s32 r = a * 4 + 0x20;
  bobj_draw_shadow(u, -r, r, -r, r, 0xFFFFFFFF);
}

// Steps 3-4: 16 segments of 0xf0 along x from the origin in the caster's facing, each a ring
// of 8 one-quad patches round the axis (y / z) turned by s24; radius (s20 * 500 / 256) *
// s20 >> 8; the last segment's near end is a point.
void draw_tube(SpellCast* sc) {
  BattleGlobals& g = bg();
  BObj* o = &sc->obj;
  o->anim = 0;
  // (looks like an original bug: the flags are not masked with 0xc as everywhere else, so
  // bit 0x40, set while a spell runs, keeps the tube's animation from advancing)
  bobj_anim_draw(o, g.flags | 0x9800u);
  const FrameUv& f = frame_uv();
  const s32 uv[4] = {f.u0, f.v0, f.u1, f.v1};
  const u16 tpage = f.tpage, clut = f.clut;
  s32 scale = sc->s20 * 500;
  if (scale < 0) scale += 0xFF;
  scale >>= 8;
  if (f.v0 == f.v1) return;
  const u32 code = bobj_poly_tag(o, tpage);
  s32 c[4][4] = {};
  s32 x = 0;
  for (int row = 0; row < 16; ++row, x += 0xF0) {
    const s32 r_far = (scale * sc->s20) >> 8;
    const s32 r_near = row == 15 ? 0 : r_far;
    s32 cs = bcos(static_cast<u32>(sc->s24));
    s32 sn = bsin(static_cast<u32>(sc->s24));
    c[1][0] = bobj_front_x(o, x + 0xF0);
    c[1][1] = o->pos[1] + cs * r_far;
    c[1][2] = o->pos[2] + sn * r_far;
    c[0][0] = bobj_front_x(o, x);
    c[0][1] = o->pos[1] + cs * r_near;
    c[0][2] = o->pos[2] + sn * r_near;
    s32 turn = 0x200;
    for (int k = 0; k < 8; ++k, turn += 0x200) {
      const u32 ang = static_cast<u32>(turn + sc->s24);
      cs = bcos(ang);
      sn = bsin(ang);
      for (int m = 0; m < 2; ++m) {
        c[m + 2][0] = c[m][0];
        c[m + 2][1] = c[m][1];
        c[m + 2][2] = c[m][2];
      }
      c[1][1] = o->pos[1] + cs * r_far;
      c[1][2] = o->pos[2] + sn * r_far;
      c[0][1] = o->pos[1] + cs * r_near;
      c[0][2] = o->pos[2] + sn * r_near;
      draw_textured_patch(uv, c, clut, tpage, 8, code, 1);
    }
  }
}

// 0x8008cc00: the handler (nothing runs while the data is still loading).
void mind_blast(SpellCast* sc) {
  BattleGlobals& g = bg();
  if (sc->data != nullptr) return;
  Unit* c = sc->caster;
  if (2 < sc->step) {
    c->anim = 0xF;
    draw_caster(c);
  }
  if (static_cast<u32>(sc->step - 3) < 2) draw_tube(sc);
  if ((g.flags & 4) != 0) return;
  if (spellcast_timer_tick(sc) != 0) return;
  // The gathering point: 0x5c in front of the caster, 0x80 up.
  const s32 point[3] = {bobj_front_x(c, 0x5C), c->pos[1] - 0x80000, c->pos[2]};
  switch (sc->step) {
    case 0:
      battle_flash_set(sc, 0x40, 0x40, 0x40);
      camera_move_to(g.camera, c->pos, 0xC00, 0x155, 0x1000, 0xF, 0x100, 5);
      spellcast_wait(sc, 0x12);
      return;
    case 1: {
      if (Sprite* sp = spellcast_sprite_spawn(sc, 0xD)) {
        sp->scale_y = 0;
        sp->scale_x = 0;
        sp->scale_vel[1] = 0x28;
        sp->scale_vel[0] = 0x28;
        sprite_set_pos_v(sp, point);
        sp->roll_speed = -0x2D;
        sprite_set_fade(sp, 0x80, -4, 300);
        sp->layer = 10;
        sp->mode = static_cast<u8>(sp->mode | 4);
      }
      s32 p138 = 0x1E0;
      for (int i = 0; i < 3; ++i, p138 += 0xB4) {
        Shot* s = effect_spawn_sparks_at(point, 0x14, 0x90, 0, i * -2 - 6, 0xB4, 0, 2, p138, 0);
        if (s != nullptr) {
          bobj_share_sprite(c, s);
          s->anim = 0x13;
        }
      }
      const u32 h = bsfx_play_at(0x43, c);
      sc->s3c = static_cast<s32>(h);
      sound_handle_fade_out(h, 0x3C, 0x96);
      sound_handle_pitch_up(static_cast<u32>(sc->s3c), 0xB4);
      ++sc->step;
      sc->obj.facing[0] = c->facing[0];
      sc->obj.pos[0] = bobj_front_x(c, 0x5C);
      sc->obj.pos[1] = c->pos[1] - 0xB4000;
      sc->obj.pos[2] = c->pos[2];
      camera_move_to(g.camera, c->pos, 0xC00, 0x71, 0x800, 0x3C, 0x80, 5);
      [[fallthrough]];
    }
    case 2:
      for (int i = 0; i < g.frame_ticks * 2; ++i) {
        Sprite* sp = spellcast_sprite_spawn(sc, static_cast<int>(rng_range(10)) + 3);
        if (sp == nullptr) continue;
        const Vec4 at = vec_random_around(point, 800);
        sprite_set_pos_v(sp, at.data());
        sprite_set_fade(sp, 0x80, -4, 0x10);
        const Vec4 v = vec_velocity_toward(sp->pos, point, 0x10);
        sp->vel[0] = v.x;
        sp->vel[1] = v.y;
        sp->vel[2] = v.z;
        sp->extra34 = v.w;
        sp->roll_speed = 0x38;
        sp->layer = 4;
      }
      sc->s20 += g.frame_ticks;
      if (sc->s20 < 0x97) return;
      ++sc->step;
      sound_handle_fade_out(bsfx_play_at(0x2D, c), 0x3C, 0x78);
      sound_handle_fade_out(bsfx_play_at(0x45, c), 0x3C, 0x78);
      sound_handle_fade_out(bsfx_play_at(0x46, c), 0x3C, 0x78);
      bsfx_play_at(0x21, c);
      bsfx_play_at(0x22, c);
      camera_reset_mode(g.camera);
      camera_move_to(g.camera, c->pos, c->facing[0] < 0 ? 0xEAA : 0x955, 0x4F, 0x800, 0xF, 0x80, 5);
      screen_shake2(4, 4, 4, 4, 0xB4);
      return;
    case 3: {
      sc->s20 += g.frame_ticks * 0x10;
      if (0x100 < sc->s20) sc->s20 = 0x100;
      sc->s24 = (sc->s24 + 0x20) & 0xFFF;
      sc->s28 += g.frame_ticks;
      for (int i = 0; i < g.frame_ticks; ++i) {
        Sprite* sp = spellcast_sprite_spawn(sc, 2);
        if (sp == nullptr) continue;
        const s32 k = rng_between(0x1000, 0x2000);
        sp->scale_y = k;
        sp->scale_x = k;
        const s32 x = sc->obj.pos[0] + bobj_facing_fx(c, static_cast<s32>(rng_range(0xF00)));
        const s32 y = sc->obj.pos[1] + rng_spread_fixed(0x78);
        const s32 z = sc->obj.pos[2] + rng_spread_fixed(0xF0);
        sprite_set_pos(sp, x, y, z);
        sprite_set_fade(sp, 0x80, -4, 8);
      }
      if (sc->s28 < 0x79) return;
      ++sc->step;
      return;
    }
    case 4:
      sc->s24 = (sc->s24 + 0x20) & 0xFFF;
      sc->s20 -= g.frame_ticks * 0x10;
      if (-1 < sc->s20) return;
      ++sc->step;
      return;
    case 5:
      if (spellcast_effects_gone(sc) == 0) return;
      ++sc->step;
      return;
    case 6: {
      // The hit: standing party members with x in the 0xf00 band in front of the caster whose
      // centre lies within 0x2bc of the tube's axis (the distance to (origin x, y, z)).
      Unit* list[8] = {};
      Vec4 points[8] = {};
      s32 x0 = c->pos[0], x1;
      if (c->facing[0] < 0) {
        x1 = x0;
        x0 = x1 - 0xF00000;
      } else {
        x1 = x0 + 0xF00000;
      }
      int n = 0;
      for (int i = 0; i < 4; ++i) {
        PartyUnit* u = g.party_units[static_cast<size_t>(i)];
        if (unit_is_down(u)) continue;
        if (u->pos[0] < x0 || !(u->pos[0] < x1)) continue;
        const s32 at[3] = {sc->obj.pos[0], u->pos[1] + u->size[1] * -0x2000, u->pos[2]};
        if (0x2BBFFF < vec_distance_fx(at, sc->obj.pos)) continue;
        list[n] = u;
        // (the point's w is stack garbage on the console)
        points[n] = Vec4{u->pos[0], u->pos[1] + u->size[1] * -0x2000, u->pos[2], 0};
        ++n;
      }
      if (n != 0) {
        unit_attack_targets(c, n, list, points, 0x200, 9999, 0);
        c->attack_recovery = 0;
      }
      battle_flash_set(sc, 0x80, 0x80, 0x80);
      sc->finished = 1;
      ++sc->step;
      return;
    }
    default: return;
  }
}

}  // namespace

void spell_mod_mind_blast(SpellCast* sc) { mind_blast(sc); }

}  // namespace ss::game::battle
