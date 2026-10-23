// Spell code module cfbae89db2 (entries 0x954-0x958): Angel Feather (spell 0x3b): a sheet of
// cloud (the data's anim 3) spreads overhead and an angel (the cast's sprite) descends to
// the target, glows and rises away; two wing models open behind the target, which gets the
// angel's protection (status bit 0x2000; a miss marker if it already has it). Functions
// 0x8008cc00 (the handler) and 0x8008d308 (the angel, a kind-0x21 callback).

#include <unordered_map>

#include "game/battle/items.h"
#include "game/battle/bsfx.h"
#include "game/battle/camera.h"
#include "game/battle/enemy_support.h"
#include "game/battle/killer.h"
#include "game/battle/render.h"
#include "game/battle/shot.h"
#include "game/battle/spell_fx.h"
#include "game/battle/spell_modules.h"
#include "game/battle/spellcast.h"
#include "game/battle/sprite.h"
#include "game/battle/unit.h"
#include "game/battle/unit_move.h"
#include "game/boot/rng.h"

namespace ss::game::battle {
namespace {

// The SpellCast's fields: s28 the wings' fade (step 4); obj the data's sprite set (anim 3
// the cloud; the angel shares it).

// The angel's afterimage (Shot +0x134 holds the pointer on the console).
std::unordered_map<const Shot*, Afterimage*> g_trail;

// One of the fx bank's sparkles: anims 8-10 or 0x2c-0x2f.
Sprite* fx_sparkle() {
  const int anim = rng_range(2) == 0 ? static_cast<int>(rng_range(3)) + 8 : static_cast<int>(rng_range(4)) + 0x2C;
  return sprite_spawn_from(anim, 0, bg().sprite_bank_fx.get(), nullptr);
}

// 0x8008d308: the angel (+0x114 the target), with an afterimage once opaque. +0x128 the
// phase: 0 it sinks 8 units a tick (anim 0) fading in from +0x12c + 20, sparkles round it,
// the camera looking at it; after 360 / 2 ticks a burst of sparkles and the camera on the
// target; 1 it hovers (anim 1) for 60 ticks; 2 it rises (anim 2), fading, with sparkles of
// its own set, the camera turning; after 60 ticks it goes.
void angel(Shot* s) {
  BattleGlobals& g = bg();
  bobj_project(s);
  bobj_anim_draw(s, g.flags & 0xC);
  if ((g.flags & 0xC) != 0) return;
  Afterimage*& trail = g_trail[s];
  if (trail == nullptr && s->blend == 0) trail = afterimage_create(s, 6, 0x3050, 2);
  if (trail != nullptr) trail->next_kind = 1;
  if (0x1E < s->p12c && s->p128 == 0) {
    ViewState& v = view();
    v.cam_lookat[0] = s->pos[0];
    v.cam_lookat[2] = s->pos[2];
    v.cam_lookat[1] = s->pos[1] + 0x12C000;
  }
  if (s->p128 == 0) {
    s->vel[1] = 0x8000;
    s->anim = 0;
    const u32 lit = static_cast<u32>(s->p12c + 0x14);
    s->rgb[0] = static_cast<u8>(lit);
    if (0x80 < (lit & 0xFF)) {
      s->rgb[0] = 0x80;
      s->blend = 0;
    }
    for (int i = 0; i < g.frame_ticks << 2; ++i) {
      Sprite* sp = fx_sparkle();
      if (sp == nullptr) continue;
      const Vec4 p = vec_random_around(s->pos, rng_between(0x32, 600));
      sprite_set_pos_v(sp, p.data());
      sprite_set_fade(sp, 0x80, -2, 8);
      sp->vel[1] = rng_between_fixed(0, 2);
      const s32 scale = rng_between(0x1000, 12000);
      sp->scale_y = scale;
      sp->scale_x = scale;
      sp->layer = 1;
    }
    s->p12c += g.frame_ticks * 2;
    if (0x168 < s->p12c) {
      s->p12c = 0;
      ++s->p128;
      bsfx_play_at(0x28, s);
      bsfx_play_at(0x29, s);
      bsfx_play_at(0x24, s);
      for (int i = 0; i < 0x30; ++i) {
        Sprite* sp = fx_sparkle();
        if (sp == nullptr) continue;
        const Vec4 p = vec_random_around(s->pos, rng_between(0x32, 400));
        sprite_set_pos_v(sp, p.data());
        sprite_set_fade(sp, 0x80, -2, 0xC);
        const Vec4 v = vec_random_around(kZeroVector, rng_between(0x10, 0x18));
        sp->vel[0] = v.x, sp->vel[1] = v.y, sp->vel[2] = v.z, sp->extra34 = v.w;
        const s32 scale = rng_between(0x1000, 0x2000);
        sp->scale_y = scale;
        sp->scale_x = scale;
        sp->layer = 1;
      }
      camera_move_to(g.camera, s->target->pos, 0xC00, 0x11C, 0xC00, 0x1E, 0x80, 5);
    }
  } else if (s->p128 == 1) {
    s->vel[1] = 0;
    s->anim = 1;
    s->p12c += g.frame_ticks;
    if (0x3C < s->p12c) {
      bsfx_play_at(0x21, s);
      bsfx_play_at(0x22, s);
      bsfx_play_at(0x26, s);
      s->p12c = 0;
      ++s->p128;
    }
  } else if (s->p128 == 2) {
    for (int i = 0; i < g.frame_ticks << 2; ++i) {
      Sprite* sp = shot_spawn_sprite(s, static_cast<int>(rng_range(10)) + 6);
      if (sp == nullptr) continue;
      const Vec4 p = vec_random_around(s->pos, rng_between(0x32, 600));
      sprite_set_pos_v(sp, p.data());
      sprite_set_fade(sp, 0x80, -2, 0xC);
      sp->vel[1] = rng_between_fixed(0, 4);
      sp->layer = 1;
    }
    s->anim = 2;
    s->vel[1] = -0x2000;
    s->p12c += g.frame_ticks;
    s->rgb[0] = static_cast<u8>(-0x80 - s->p12c);
    if (0x1E < s->p12c && s->p130 == 0) {
      s->p130 = 1;
      const s32 yaw = s->target->facing[0] < 0 ? 0x800 : 0x1000;
      camera_move_to(g.camera, s->target->pos, yaw, 0xE3, 0x600, 0x3C, 0x80, 5);
    }
    if (0x3C < s->p12c) {
      if (trail != nullptr) trail->ending = 1;
      g_trail.erase(s);
      shot_kill(s);
    }
  }
  bobj_move(s, s->vel);
}

// Steps 2-3: the cloud, 16 x 14 one-quad patches of anim 3's cel 0x578 square at y -0xfa0,
// from the target's x - 0x2648 and its z on.
void draw_cloud(SpellCast* sc) {
  BObj* o = &sc->obj;
  o->anim = 3;
  bobj_anim_draw(o, 0x9800);
  const FrameUv& f = frame_uv();
  const s32 uv[4] = {f.u0, f.v0, f.u1, f.v1};
  const u16 tpage = f.tpage, clut = f.clut;
  const u32 code = bobj_poly_tag(o, tpage);
  const s32 y = static_cast<s32>(0xFF060000);
  for (int row = 0, dz = 0; row < 0x10; ++row, dz += 0x578000) {
    for (int col = 0, dx = 0; col < 0xE; ++col, dx += 0x578000) {
      const Unit* t = sc->target;
      const s32 x0 = dx + t->pos[0] - 0x2648000, x1 = x0 + 0x578000;
      const s32 z0 = dz + t->pos[2], z1 = z0 + 0x578000;
      const s32 c[4][4] = {{x0, y, z0, 0}, {x1, y, z0, 0}, {x0, y, z1, 0}, {x1, y, z1, 0}};
      draw_textured_patch(uv, c, clut, tpage, 0, code, 1);
    }
  }
}

// Step 4: the wings (models 0 and 1, scale 2.0, the fx bank's CLUT + 7) behind the target,
// turned to its facing, darkening as s28 grows.
void draw_wings(SpellCast* sc) {
  const BattleGlobals& g = bg();
  const Unit* t = sc->target;
  stage_colour_set_grey(0x100 - sc->s28);
  s32 orient[3] = {0, 0 < t->facing[0] ? 0x400 : -0x400, 0};
  const s32 scale[3] = {0x2000, 0x2000, 0x2000};
  const s32 pos[3] = {bobj_front_x(t, t->size[0] * -2), t->pos[1] + t->size[1] * -0x3000, t->pos[2]};
  const AnimSet* set = g.sprite_bank_fx->animset;
  const u16 clut = static_cast<u16>(set->clut[0] + 7);
  for (int k = 0; k < 2; ++k) {
    lights_build_oriented(orient);
    obj_set_matrix(pos, orient, scale);
    if (const u8* m = spellcast_model(sc, k)) model_draw(m, clut, set->tpage[0], 4);
  }
  stage_colour_restore();
}

void angel_feather(SpellCast* sc) {
  BattleGlobals& g = bg();
  Unit* t = sc->target;
  if (static_cast<u32>(sc->step - 2) < 2) draw_cloud(sc);
  if (sc->step == 4) draw_wings(sc);
  if ((g.flags & 4) != 0) return;
  if (spellcast_timer_tick(sc) != 0) return;
  switch (sc->step) {
    case 0:
      battle_flash_set(sc, 0x38, 0x40, 0x48);
      if (spellcast_leads_camera(sc)) camera_move_to(g.camera, t->pos, 0xC00, 0x93, 0x800, 0x14, 0x80, 5);
      spellcast_wait(sc, 0x14);
      return;
    case 1: {
      if (sc->data != nullptr) return;
      spellcast_wait(sc, 0x2D);
      const s32 above[3] = {t->pos[0], t->pos[1] - 0x708000, t->pos[2]};
      camera_move_to(g.camera, above, 0xC00, -0xAA, 0x800, 0x28, 0x80, 5);
      bsfx_play_at(0x30, t);
      return;
    }
    case 2: {
      // The angel, 0x834 above the target.
      spellcast_wait(sc, 0xFA);
      Shot* s = shot_spawn_callback_always(angel);
      if (s == nullptr) return;
      g_trail.erase(s);
      bsfx_play_at(0x34, s);
      const u32 h = bsfx_play_at(0x44, s);
      sound_handle_pitch_down(h, 0xF0);
      sound_handle_fade_out(h, 0x78, 0x78);
      s->pos[0] = t->pos[0];
      s->pos[1] = t->pos[1] - 0x834000;
      s->blend = 0x82;
      s->anim = 0;
      s->colour_mode = 2;
      s->rgb[0] = 0;
      s->pos[2] = t->pos[2];
      s->target = t;
      bobj_share_sprite(&sc->obj, s);
      return;
    }
    case 3: ++sc->step; return;
    case 4:
      sc->s28 += g.frame_ticks * 3;
      if (sc->s28 < 0x100) return;
      ++sc->step;
      return;
    case 5:
      // The protection (or the miss marker).
      if ((t->status_bits & 0x2000) == 0) {
        item_fx_sparkle(t);
        t->status_bits |= 0x2000;
        bsfx_play_ui(10);
      } else {
        effect_rise_icon(t, 0);
        bsfx_play_ui(0xB);
      }
      ++sc->step;
      battle_flash_set(sc, 0x80, 0x80, 0x80);
      [[fallthrough]];
    case 6:
      if (spellcast_effects_gone(sc) == 0) return;
      ++sc->step;
      return;
    case 7:
      camera_move_to(g.camera, t->pos, 0xC00, 0x93, 0xC00, 0x3C, 0x80, 5);
      sc->finished = 1;
      ++sc->step;
      return;
    default: return;
  }
}

}  // namespace

void spell_mod_angel_feather(SpellCast* sc) { angel_feather(sc); }

}  // namespace ss::game::battle
