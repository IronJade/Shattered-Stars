// Spell code module ae70313b32 (entries 0x92a-0x92c): Shadow Flare (spell 0x21) and Judgement
// Day (spell 0x42): a dark star burns 3000 units above the field (the data's sprite set,
// anims 1 and 0, with the stage's lens flare) and drops fiery meteors on random points of the
// arena (or the combo point); then a dark shimmer (anim 2 in wave-distorted grids) hangs over
// the screen and everything on the other side is hit (element 0x100). Functions 0x8008cc00
// (the handler) and 0x8008d440 (a meteor, a kind-0x21 callback); the jump table of the
// handler's steps at 0x8008d6d0. Resident 0x8007d9e0 draw_wave_grid (#1984.0): spell_fx.h.

#include <cstring>

#include "game/battle/bsfx.h"
#include "game/battle/camera.h"
#include "game/battle/combat.h"
#include "game/battle/fx_draw.h"
#include "game/battle/render.h"
#include "game/battle/shot.h"
#include "game/battle/spell_fx.h"
#include "game/battle/spell_modules.h"
#include "game/battle/spellcast.h"
#include "game/battle/sprite.h"
#include "game/battle/stage.h"
#include "game/battle/unit.h"
#include "game/battle/unit_move.h"
#include "game/boot/rng.h"
#include "game/common/screen.h"
#include "game/title/new_game.h"
#include "runtime/libgpu.h"

namespace ss::game::battle {
namespace {

// The SpellCast's fields: s20 ticks (step 2), s24 / s30 the shimmer's x / y wave phases, s2c
// the star's grey (step 2), s34 the shimmer's brightness (1 once the star is lit in step 2,
// 0x100 at step 3, fading in step 4), s38 the meteor timer, s3c the handle of the hum of
// steps 3-6; obj the data's sprite set.

constexpr s32 kStarY = static_cast<s32>(0xFF448000);  // -3000 units
const s32 kUnitScale[3] = {0x1000, 0x1000, 0x1000};    // 0x8006e34c


// 0x8008d440: a meteor (a lit orange sphere of radius 0xa0, grey +0x64 when +0x63 is set)
// flying at +0x28 toward +0x118. Near the point with +0x10c 3 it takes a new random point of
// the arena (the module never sets +0x10c); at or below the ground: a shake, sfx 0x50,
// debris, an explosion sprite (fx bank anim 0x34), a spark ring, and it goes.
void meteor(Shot* s) {
  BattleGlobals& g = bg();
  stage_colour_set_grey(s->colour_mode != 0 ? s->rgb[0] : 0x80);
  s32 orient[3] = {0, 0, 0};  // g_zero_vector
  lights_build_oriented(orient);
  obj_set_matrix(s->pos, orient, kUnitScale);
  draw_sphere(3, 4, 0xA0, 0xC04000, 0xFF, 0, 2, 0x1000);
  stage_colour_restore();
  if ((g.flags & 4) != 0) return;
  const s32 d = vec_distance_fx(s->pos, s->point);
  bobj_move(s, s->vel);
  if (d < g.frame_ticks * 0x30000 && s->aux == 3) {
    s->p12c = 0;
    s->aux = 0;
    s32 p[4];
    stage_random_point(p, 1);
    s->point[0] = p[0], s->point[1] = p[1], s->point[2] = p[2];
    const Vec4 v = vec_velocity_toward(s->pos, s->point, 0x1E);
    s->vel[0] = v.x, s->vel[1] = v.y, s->vel[2] = v.z, s->extra34 = v.w;
    return;
  }
  const s32 ground = bobj_ground_height(s);
  if (s->pos[1] < ground) return;
  s->pos[1] = ground;
  screen_shake2(4, 6, 4, 6, 0xE);
  bsfx_play_at_pos(0x50, s->pos);
  const s32 spread[3] = {0x30000, 0x30000, 0x18000};
  effect_debris_burst(s->pos, spread, 2, 2, 1, 0x4000, 4);
  if (Sprite* sp = sprite_spawn_from(0x34, 0, g.sprite_bank_fx.get(), nullptr)) {
    sprite_set_pos_v(sp, s->pos);
    sprite_set_fade(sp, 0x80, -2, 0xC);
    const s32 scale = rng_between(12000, 0x4000);
    sp->scale_y = scale;
    sp->scale_x = scale;
    sp->layer = 3;
    sp->clut_offset = 4;
  }
  effect_spawn_sparks_at(s->pos, 0x10, 400, 200, 0x12, 0x78, 7, 1, 0x20, 0);
  shot_kill(s);
}

// Steps 2-3: the star: anim 1 (grey s2c) and anim 0 (double size) at (0, -3000, 0), and the
// stage's lens flare from it.
void draw_dark_star(SpellCast* sc) {
  BObj* o = &sc->obj;
  o->anim = 1;
  o->colour_mode = 2;
  o->pos[0] = 0;
  o->pos[1] = kStarY;
  o->pos[2] = 0;
  o->scale_y = 0x1000;
  o->scale_x = 0x1000;
  o->rgb[0] = static_cast<u8>(sc->s2c);
  bobj_project(o);
  bobj_anim_draw(o, 0);
  o->pos[0] = 0;
  o->pos[1] = kStarY;
  o->pos[2] = 0;
  o->anim = 0;
  o->scale_y = 0x2000;
  o->scale_x = 0x2000;
  o->colour_mode = 0;
  bobj_project(o);
  bobj_anim_draw(o, 0);
  stage_colour_set_grey(0x100);
  const s32 sun[3] = {0, kStarY, 0};
  lens_flare_draw(sun, 0x400, 0x600000, 0x80, 0x46);
  stage_colour_restore();
}

// Step 4: the shimmer: anim 2's cel as two rows (y 0x78 and 0xc8) of 2 x 2 wave-distorted
// 6 x 6 grids round x 0xa0 (the second row at half brightness); false when the frame is
// paused after it.
bool draw_shimmer(SpellCast* sc) {
  BattleGlobals& g = bg();
  u32 code = scale_colour(0x808080, sc->s34) | 0x2E000000;
  s32 wave_x[3] = {0x18000, sc->s24, 0x4000};
  s32 wave_y[3] = {0x4000, sc->s30, 0x2000};
  if (sc->s34 == 0) return true;
  BObj* o = &sc->obj;
  o->anim = 2;
  bobj_anim_draw(o, 0x9800);
  const FrameUv& f = frame_uv();
  const s32 uv[4] = {f.u0, f.v0, f.u1, f.v1};
  const u16 tpage = f.tpage, clut = f.clut;
  s32 base_y = 0x78;
  for (int i = 0; i < 2; ++i) {
    for (int j = 0; j < 2; ++j) {
      for (int k = 0; k < 2; ++k) {
        s32 rect[4];
        rect[0] = (k - 1) * (uv[2] - uv[0]) + 0xA0;
        rect[2] = rect[0] + (uv[2] - uv[0]);
        rect[1] = (j - 1) * (uv[3] - uv[1]) + base_y;
        rect[3] = rect[1] + (uv[3] - uv[1]);
        draw_wave_grid(uv, rect, wave_x, wave_y, clut, static_cast<u16>(tpage | 0x20), 4, code, 6);
      }
    }
    wave_x[1] = sc->s24 + 0x400;
    base_y += 0x50;
    // ORIGINAL BUG? (to review): the second row's y phase comes from the x phase (s24 + 0x200),
    // not from s30 as the first row's does.
    wave_y[1] = sc->s24 + 0x200;
    code = scale_colour(0x808080, sc->s34 / 2) | 0x2E000000;
  }
  if ((g.flags & 4) != 0) return false;
  sc->s24 += g.frame_ticks * 0x14;
  sc->s30 += g.frame_ticks * 0x28;
  return true;
}

void shadow_flare(SpellCast* sc) {
  BattleGlobals& g = bg();
  const Unit* t = sc->target;
  sc->centre[0] = t->pos[0];
  sc->centre[1] = t->pos[1];
  sc->centre[2] = t->pos[2];
  sc->eff_power = sc->caster->st()->intel[2] + sc->power + 0x60E;
  if (static_cast<u32>(sc->step - 2) < 2) draw_dark_star(sc);
  if (sc->step == 4 && !draw_shimmer(sc)) return;
  if ((g.flags & 4) != 0) return;
  if (spellcast_timer_tick(sc) != 0) return;
  switch (sc->step) {
    case 0:
      spellcast_dim_begin(sc, sc->caster->is_enemy);
      if (spellcast_leads_camera(sc)) {
        const s32 at[3] = {0x320000, -0x600000, static_cast<s32>(0xFFE70000)};
        camera_move_to(g.camera, at, 0xAAA, -0x471, 0x200, 0x1E, 0x100, 5);
      }
      battle_flash_set(sc, 8, 0x10, 0x40);
      spellcast_wait(sc, 0x14);
      return;
    case 1:
      if (sc->data != nullptr) return;
      ++sc->step;
      if (spellcast_leads_camera(sc)) {
        const s32 at[3] = {0, static_cast<s32>(0xFFA00000), 0};
        camera_move_to(g.camera, at, 0xC00, -0x400, 0x200, 0x78, 0x80, 5);
      }
      return;
    case 2: {
      // The star lights up after 60 ticks; meteors every 12 - power / 20 (at least 4) ticks.
      sc->s20 += g.frame_ticks;
      if (0x3C < sc->s20) sc->s2c = (sc->s20 + -0x3C) * 2;
      if (0x67 < sc->s20 && sc->s34 == 0) {
        sc->s34 = 1;
        bsfx_play_ui(0x29);
        bsfx_play_ui(0x21);
        bsfx_play_ui(0x22);
      }
      sc->s38 += g.frame_ticks;
      s32 every = 0xC - sc->power / 0x14;
      if (every < 4) every = 4;
      if (every < sc->s38) {
        sc->s38 -= every;
        if (Shot* s = shot_spawn_callback_always(meteor)) {
          s32 from[3] = {0, 0, 0};
          s32 point[4];
          if (spellcast_combo_point(sc, point) == 0) stage_random_point(point, 1);
          s->point[0] = point[0], s->point[1] = point[1], s->point[2] = point[2];
          from[1] = static_cast<s32>(0xFF060000);
          s->pos[0] = from[0], s->pos[1] = from[1], s->pos[2] = from[2];
          const Vec4 v = vec_velocity_toward(from, s->point, 0x1E);
          s->vel[0] = v.x, s->vel[1] = v.y, s->vel[2] = v.z, s->extra34 = v.w;
          bsfx_play_at(0x38, s);
        }
      }
      if (0x78 < sc->s20) spellcast_wait(sc, 4);
      return;
    }
    case 3: {
      // The shimmer at full brightness, the hum, wait 90.
      battle_flash_set(sc, 0x20, 0x20, 0x50);
      if (spellcast_leads_camera(sc)) {
        const s32 yaw = rng_range(2) != 0 ? 0x4E3 : 0x31C;
        camera_move_to(g.camera, kZeroVector, yaw, 0x155, 0x1800, 0, 0x80, 5);
        s32 unused[4];
        spellcast_combo_point(sc, unused);  // (its result is not used)
        camera_move_to(g.camera, sc->centre, 0xC00, 0x88, 0x12C0, 0xA0, 0x80, 5);
      }
      sc->s34 = 0x100;
      sc->s3c = static_cast<s32>(bsfx_play_ui(0x2D));
      sc->s20 = 0;
      spellcast_wait(sc, 0x5A);
      return;
    }
    case 4:
      sc->s34 += g.frame_ticks * -2;
      if (-1 < sc->s34) return;
      ++sc->step;
      return;
    case 5:
      if (spellcast_effects_gone(sc) == 0) return;
      ++sc->step;
      return;
    case 6:
      sound_handle_fade_out(static_cast<u32>(sc->s3c), 0x1E, 0);
      if (sc->combo != 1 && sc->combo != 4) bsfx_play_ui(0x4B);
      spell_hit_all(sc, 0x100, 0x10);
      sc->finished = 1;
      return;
    default: return;
  }
}

}  // namespace

void spell_mod_shadow_flare(SpellCast* sc) { shadow_flare(sc); }

}  // namespace ss::game::battle
