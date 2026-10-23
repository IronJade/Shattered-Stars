// Spell code module bf76cb13f3 (entries 0x90a / 0x90b): Gravity Press (spell 0x28): sparks
// spiral out high above the target (or the combo point), then heavy blocks drop one by one
// into an area round it, each landing with a shake; rings mark every unit of the other side
// in the area before the hit on it (element 0x200). Functions 0x8008cc00 (the handler),
// 0x8008d1f4 (a spiralling spark, a kind-0x21 callback), 0x8008d3b0 (a falling block, kind
// 0x21) and 0x8008d6e8 (a ring of three sprites round a unit, kind 0x21); the jump table of
// the handler's steps at 0x8008d8fc.

#include "game/battle/bsfx.h"
#include "game/battle/camera.h"
#include "game/battle/combat.h"
#include "game/battle/enemy_support.h"
#include "game/battle/particle.h"
#include "game/battle/render.h"
#include "game/battle/shot.h"
#include "game/battle/spell_modules.h"
#include "game/battle/spellcast.h"
#include "game/battle/sprite.h"
#include "game/battle/unit.h"
#include "game/battle/unit_move.h"
#include "game/boot/rng.h"

namespace ss::game::battle {
namespace {

// The SpellCast's fields: centre the point 0x514 above the target (the caster on combo 3,
// the combo point on 1 / 2), s20 the blocks still to drop, s24 the block timer, s28 the
// rumble's sound handle; obj the data's sprite set (the Shots share it).

// 0x8008d1f4: a spark (anim 3) spiralling out round +0x118 (radius +0x12c growing 8 a tick
// to +0x130, angle +0x128); a trail sprite (anim 5) every 4 ticks (+0x134); +0x10c counts
// the ticks: it fades after 240 and goes after 320.
void spiral_spark(Shot* s) {
  BattleGlobals& g = bg();
  if (0xF0 < s->aux) {
    s->colour_mode = 2;
    s->rgb[0] = static_cast<u8>(0x70 - static_cast<u8>(s->aux));
  }
  s->anim = 3;
  bobj_project(s);
  bobj_anim_draw(s, g.flags & 0xC);
  if ((g.flags & 0xC) != 0) return;
  s->pos[1] = s->point[1];
  s->pos[0] = s->point[0] + bcos(static_cast<u32>(s->p128)) * s->p12c;
  const s32 r = s->p12c;
  s->p12c += g.frame_ticks * 8;
  s->pos[2] = s->point[2] + bsin(static_cast<u32>(s->p128)) * r;
  if (s->p130 < s->p12c) s->p12c = s->p130;
  s->p128 = (s->p128 + g.frame_ticks * 0x20) & 0xFFF;
  s->p134 += g.frame_ticks;
  s->aux = static_cast<s16>(s->aux + g.frame_ticks);
  if (3 < s->p134) {
    s->p134 -= 4;
    if (Sprite* sp = sprite_spawn_from(5, 1, s, nullptr)) {
      sprite_set_pos_v(sp, s->pos);
      sprite_set_fade(sp, 0x80, -8, 0);
      sp->scale_y = s->scale_x;
      sp->scale_x = s->scale_x;
    }
  }
  if (0x140 < s->aux) shot_kill(s);
}

// 0x8008d3b0: a block (anim 0, layer 1) with a ground shadow growing as it nears the ground,
// falling with gravity 0x400 a tick (at most 0x40000); on the ground: sfx 0x23 / 0x25, four
// dust sprites (anims 2 / 3, the right ones flipped), a ring and a shake; then (+0x12c the
// ticks) it shrinks after 32 and goes at 64.
void falling_block(Shot* s) {
  BattleGlobals& g = bg();
  s->anim = 0;
  s->layer = 1;
  bobj_project(s);
  bobj_anim_draw(s, g.flags & 0xC);
  const s32 ground = bobj_ground_height(s) + -0x10000;
  s32 w = s->ext_x1;
  if (w < 0) w = -w;
  w = static_cast<s32>(static_cast<u32>(w * 4 + (s->pos[1] - ground) / 16) * static_cast<u32>(s->scale_x)) / 0x1000;
  if (w < 0x10) w = 0x10;
  bobj_draw_shadow(s, -w, w, -w, w, 0xFFFFFFFF);
  if ((g.flags & 0xC) != 0) return;
  if (s->p12c == 0) {
    bobj_move(s, s->vel);
    s->vel[1] += g.frame_ticks * 0x400;
    if (0x40000 < s->vel[1]) s->vel[1] = 0x40000;
    if (!(ground < s->pos[1])) return;
    s->pos[1] = ground;
    bsfx_play_at(0x23, s);
    bsfx_play_at(0x25, s);
    s->p12c = 1;
    if (Sprite* sp = sprite_spawn_from(2, 1, s, nullptr)) sprite_set_pos(sp, s->pos[0] + -0x20000, s->pos[1], s->pos[2] + 0x60000);
    if (Sprite* sp = sprite_spawn_from(2, 1, s, nullptr)) {
      sprite_set_pos(sp, s->pos[0] + 0x20000, s->pos[1], s->pos[2] + 0x60000);
      sp->flags = static_cast<u8>(sp->flags | 0x20);
    }
    if (Sprite* sp = sprite_spawn_from(3, 1, s, nullptr)) sprite_set_pos(sp, s->pos[0] + -0x20000, s->pos[1], s->pos[2] + -0x60000);
    if (Sprite* sp = sprite_spawn_from(3, 1, s, nullptr)) {
      sprite_set_pos(sp, s->pos[0] + 0x20000, s->pos[1], s->pos[2] + -0x60000);
      sp->flags = static_cast<u8>(sp->flags | 0x20);
    }
    const s32 at[4] = {s->pos[0], s->pos[1], s->pos[2], s->pos_w};
    particle_spawn_ring(at, 0xC, 0x90, 0x38, 0x3C, 0xD0B090, 0x301010, 0x60, 0);
    screen_shake2(2, 8, 4, 4, 0x10);
    return;
  }
  s->p12c += g.frame_ticks;
  if (0x20 < s->p12c) {
    const s32 scale = (s->p12c - 0x20) * -0x80 + 0x1000;
    s->scale_y = scale;
    s->scale_x = scale;
  }
  if (0x3F < s->p12c) shot_kill(s);
}

// 0x8008d6e8: a ring of anim 6 round its unit (+0x114) at chest height, ellipse 2 * size + 16
// by 2 * size, turning (+0x12c) 30 degrees a tick; drawn four times a frame (at 0, 120, 120
// and 240 degrees, each draw animating); it shrinks after 60 ticks (+0x128) and goes at 120.
void unit_ring(Shot* s) {
  BattleGlobals& g = bg();
  s->anim = 6;
  if (!(s->p128 < 0x78)) {
    shot_kill(s);
    return;
  }
  if (0x3C < s->p128) {
    const s32 scale = (s->p128 + -0x3C) * -0x40 + 0x1000;
    s->scale_y = scale;
    s->scale_x = scale;
  }
  const Unit* u = s->target;
  const s32 ux = u->pos[0], uz = u->pos[2];
  const s32 rx = u->size[0] * 2 + 0x10;
  const s32 rz = u->size[2] * 2;
  s->pos[0] = ux + bcos(static_cast<u32>(s->p12c)) * rx;
  s->pos[2] = uz + bsin(static_cast<u32>(s->p12c)) * rz;
  s->pos[1] = u->pos[1] + u->size[1] * -0x4000;
  // Original quirk (original_bugs.md S10, to decide): each pass draws before and after moving, so the 120-degree
  // copy is drawn twice and the animation steps four times a frame (three copies meant?).
  for (s32 deg = 0x78; deg < 0xF1;) {
    bobj_project(s);
    bobj_anim_draw(s, 0);
    const s32 a = (deg * 0x1000) / 0x168;
    s->pos[0] = ux + bcos(static_cast<u32>(s->p12c + a)) * rx;
    deg += 0x78;
    s->pos[2] = uz + bsin(static_cast<u32>(s->p12c + a)) * rz;
    bobj_project(s);
    bobj_anim_draw(s, 0);
  }
  if ((g.flags & 8) != 0) return;
  s->p12c = (s->p12c + (g.frame_ticks * 0x3000) / 0x168) & 0xFFF;
  if ((g.flags & 0xC) == 0) s->p128 += g.frame_ticks;
}

void gravity_press(SpellCast* sc) {
  BattleGlobals& g = bg();
  const Unit* t = sc->target;
  sc->centre[0] = t->pos[0];
  sc->centre[1] = t->pos[1] + -0x514000;
  sc->centre[2] = t->pos[2];
  if (static_cast<u8>(sc->combo - 1) < 2) {
    spellcast_combo_point(sc, sc->centre);
    sc->centre[1] = static_cast<s32>(0xFFAEC000);
  } else {
    const Unit* at = sc->combo == 3 ? sc->caster : t;
    sc->centre[0] = at->pos[0];
    sc->centre[1] = at->pos[1] + -0x514000;
    sc->centre[2] = at->pos[2];
  }
  sc->eff_power = sc->caster->st()->intel[2] / 2 + sc->power / 4 + 0x226;
  if ((g.flags & 4) != 0) return;
  if (spellcast_timer_tick(sc) != 0) return;
  // The area: 0x384 units round the centre (x0, z0, x1, z1).
  const s32 rect[4] = {sc->centre[0] + -0x384000, sc->centre[2] + -0x384000, sc->centre[0] + 0x384000,
                       sc->centre[2] + 0x384000};
  switch (sc->step) {
    case 0:
      spellcast_dim_begin(sc, sc->caster->is_enemy);
      battle_flash_set(sc, 0x40, 0x40, 0x40);
      if (spellcast_leads_camera(sc)) {
        const s32 yaw = rng_range(2) != 0 ? 0xDC7 : 0xA39;
        camera_move_to(g.camera, sc->centre, yaw, -0x155, 0x800, 0x5A, 0x80, 5);
      }
      ++sc->step;
      return;
    case 1:
      if (sc->data != nullptr) return;
      ++sc->step;
      return;
    case 2: {
      // 24 sparks spiralling out to radii 0x80, 0xb0, ... ; the rumble; power / 32 + 16 blocks.
      s32 radius = 0x80;
      for (int i = 0; i < 0x18; ++i) {
        if (Shot* s = shot_spawn_callback_always(spiral_spark)) {
          s->point[0] = sc->centre[0], s->point[1] = sc->centre[1], s->point[2] = sc->centre[2];
          s->pos[0] = sc->centre[0], s->pos[1] = sc->centre[1], s->pos[2] = sc->centre[2];
          s->pos_w = sc->centre[3];
          s->p130 = radius;
          s->p128 = static_cast<s32>(rng_range(0x1000));
          const s32 scale = rng_between(0x2000, 0x3000);
          s->scale_y = scale;
          s->scale_x = scale;
          bobj_share_sprite(&sc->obj, s);
        }
        radius += 0x30;
      }
      sc->s28 = bsfx_play_at_pos(0x35, sc->centre);
      sc->s20 = sc->power / 32 + 0x10;
      if (0x20 < sc->s20) sc->s20 = 0x20;
      spellcast_wait(sc, 0x78);
      return;
    }
    case 3:
      if (spellcast_leads_camera(sc)) {
        camera_reset_mode(g.camera);
        const s32 at[4] = {sc->centre[0], 0, sc->centre[2], sc->centre[3]};
        camera_move_to(g.camera, at, 0xC00, 0x11C, 0x1000, 600, 0x80, 5);
      }
      ++sc->step;
      return;
    case 4:
      // A block every 16 ticks at a random point of the area, until s20 have fallen.
      sc->s24 += g.frame_ticks * 0x1000;
      if (sc->s24 < 0x10000) return;
      sc->s24 += -0x10000;
      if (Shot* s = shot_spawn_callback_always(falling_block)) {
        const Vec4 p = vec_random_in_disc_xz(sc->centre, 0x490);
        s->pos[0] = p.x, s->pos[1] = p.y, s->pos[2] = p.z, s->pos_w = p.w;
        s->pos[1] = static_cast<s32>(0xFFAEC000);
        bobj_share_sprite(&sc->obj, s);
        bsfx_play_at(0x38, s);
      }
      if (--sc->s20 != 0) return;
      sound_handle_fade_out(static_cast<u32>(sc->s28), 0x78, 0);
      spellcast_wait(sc, 0x78);
      return;
    case 5: {
      // A ring round every standing unit of the other side in the area (none on combos 1 / 2).
      if (1 < static_cast<u8>(sc->combo - 1)) {
        const int side = sc->combo == 3 ? sc->caster->is_enemy ^ 1 : sc->caster->is_enemy;
        Unit* list[8] = {};
        const int n = units_in_rect(sc, rect, list, side);
        for (int i = 0; i < n; ++i) {
          Shot* s = shot_spawn_callback_always(unit_ring);
          if (s == nullptr) continue;
          s->target = list[i];
          bobj_share_sprite(&sc->obj, s);
        }
      }
      spellcast_wait(sc, 0x3C);
      return;
    }
    case 6:
      spell_hit_rect(sc, rect, 0x200, 8);
      return;
    case 7:
      if (spellcast_effects_gone(sc) == 0) return;
      sc->finished = 1;
      ++sc->step;
      return;
    default: return;
  }
}

}  // namespace

void spell_mod_gravity_press(SpellCast* sc) { gravity_press(sc); }

}  // namespace ss::game::battle
