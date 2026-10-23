// Spell code module 569e24f878 (entries 0x920 / 0x921): Thundercloud (spell 0x10): a cloud
// layer (the data's sprite, 6 x 6 patches) gathers over the field with rain streaks and
// lightning, then bolts strike random units of the other side for 180 ticks before the hit
// on the whole field. Functions 0x8008cc00 (the handler) and 0x8008d730 (a bolt, a kind-0x21
// callback).

#include <unordered_map>

#include "game/battle/bsfx.h"
#include "game/battle/camera.h"
#include "game/battle/combat.h"
#include "game/battle/enemy_support.h"
#include "game/battle/killer.h"
#include "game/battle/render.h"
#include "game/battle/shot.h"
#include "game/battle/spell_fx.h"
#include "game/battle/spell_modules.h"
#include "game/battle/spellcast.h"
#include "game/battle/sprite.h"
#include "game/battle/unit.h"
#include "game/boot/rng.h"

namespace ss::game::battle {
namespace {

// A bolt's afterimage (Shot +0x134 holds the pointer on the console).
std::unordered_map<const Shot*, Afterimage*> g_trail;

// 0x8008d730: a bolt (the cast's sprite, anim 6) flying at +0x118 with an afterimage; at it
// (+0x10c the combo kind: 3 first turns to its unit, 0 the full strike: four spark rings, a
// shake, 12 beams, sfx 0x4b) sfx 0x50, 4 sparks, and it goes.
void bolt(Shot* s) {
  BattleGlobals& g = bg();
  s->anim = 6;
  bobj_project(s);
  bobj_anim_draw(s, g.flags & 0xC);
  if ((g.flags & 0xC) != 0) return;
  Afterimage*& trail = g_trail[s];
  if (trail == nullptr && s->blend == 0) trail = afterimage_create(s, 3, 0x600000, 2);
  if (vec_distance_fx(s->pos, s->point) < 0xA0000) {
    Unit* t = s->target;
    if (s->aux == 3) {
      s->aux = 0;
      s->point[0] = t->pos[0];
      s->point[1] = t->pos[1] + t->size[1] * -0x2000;
      s->point[2] = t->pos[2];
      const Vec4 v = vec_velocity_toward(s->pos, s->point, 0x30);
      s->vel[0] = v.x, s->vel[1] = v.y, s->vel[2] = v.z, s->extra34 = v.w;
      return;
    }
    if (trail != nullptr) trail->ending = 1;
    g_trail.erase(s);
    if (s->aux == 0) {
      s32 at[3] = {t->pos[0], bobj_ground_height(t), t->pos[2]};
      effect_spawn_sparks_at(at, 0xC, 0x8C, 0x140, 0x40, 0x5A, 5, 1, 0x100, 0);
      at[1] = bobj_ground_height(t);
      effect_spawn_sparks_at(at, 0x10, 0x140, 0, 0x20, 0x5A, 4, 3, 0x100, 0);
      at[1] = bobj_ground_height(t);
      if (Shot* r = effect_spawn_sparks_at(at, 0x20, 0x140, 0, 0x10, 0x5A, 4, 3, 0x100, 0)) r->vel[2] = 0x400;
      at[1] = bobj_ground_height(t);
      effect_spawn_sparks_at(at, 0x14, 0xF0, 0, 0x10, 0x3C, 0xB, 4, 0x100, 0);
      screen_shake(4, 6, 4, 4, 10);
      for (int i = 0; i < 0xC; ++i) {
        const s32 c[3] = {t->pos[0], t->pos[1] + t->size[1] * -0x2000, t->pos[2]};
        const Vec4 end = vec_random_around(c, 1000);
        effect_spawn_beam_at(c, end.data(), 0xD, 0x140, 0x18, 0x14000, 0x800, 0);
      }
      bsfx_play_at(0x4B, t);
    }
    bsfx_play_at(0x50, t);
    for (int i = 0; i < 4; ++i) {
      Sprite* sp = shot_spawn_sprite(s, 6);
      if (sp == nullptr) continue;
      const s32 dx = rng_between_fixed(t->size[0] * -10, t->size[0] * 10);
      const s32 dy = rng_between_fixed(t->size[1] * -8, 0);
      const s32 dz = rng_between_fixed(t->size[2] * -6, t->size[2] * 6);
      sprite_set_pos(sp, t->pos[0] + dx, t->pos[1] + dy, t->pos[2] + dz);
      sprite_set_fade(sp, 0x80, -2, static_cast<s16>(rng_between(4, 0xC)));
      sp->layer = 3;
      const s32 scale = rng_between(0x2000, 20000);
      sp->scale_y = scale;
      sp->scale_x = scale;
    }
    shot_kill(s);
  }
  bobj_move(s, s->vel);
}

// The cloud (s28 == 1, once the data is loaded): 6 x 6 patches (3 x 3 quads each) of anim
// 0's cel, 0x1388 square, at y -0xfa0 from x -0x2ee0 / z -0x3e8.
void draw_cloud(SpellCast* sc) {
  BObj* o = &sc->obj;
  o->anim = 0;
  bobj_anim_draw(o, 0x9800);
  const FrameUv& f = frame_uv();
  const s32 uv[4] = {f.u0, f.v0, f.u1, f.v1};
  const u16 tpage = f.tpage, clut = f.clut;
  const u32 code = bobj_poly_tag(o, tpage);
  for (int row = 0; row < 6; ++row) {
    const s32 z0 = row * 0x1388000 - 0x3E8000, z1 = row * 0x1388000 + 0xFA0000;
    for (int col = 0; col < 6; ++col) {
      const s32 x0 = col * 0x1388000 - 0x2EE0000, x1 = col * 0x1388000 - 0x1B58000;
      const s32 y = static_cast<s32>(0xFF060000);
      const s32 c[4][4] = {{x0, y, z0, 0}, {x1, y, z0, 0}, {x0, y, z1, 0}, {x1, y, z1, 0}};
      draw_textured_patch(uv, c, clut, tpage, 0, code, 3);
    }
  }
}

void thundercloud(SpellCast* sc) {
  BattleGlobals& g = bg();
  Unit* c = sc->caster;
  Unit* t = sc->target;
  sc->centre[0] = t->pos[0];
  sc->centre[1] = t->pos[1] - 0x140000;
  sc->centre[2] = t->pos[2];
  sc->eff_power = c->st()->intel[2] + sc->power + 0x4B0;
  if (sc->s28 == 1 && sc->data == nullptr) draw_cloud(sc);
  if ((g.flags & 4) != 0) return;
  if (sc->s24 != 0) {
    // Rain: power-scaled streaks (at most 3 a frame) from 0xbb8 above random points.
    s32 rate = sc->power * 0x1E + 0xC00;
    if (0x1000 < rate) rate = 0x1000;
    sc->s2c += rate * g.frame_ticks;
    s32 n = sc->s2c;
    if (n < 0) n += 0xFFF;
    n >>= 12;
    if (3 < n) n = 3;
    for (int i = 0; i < n; ++i) {
      s32 p[4];
      stage_random_point(p, sc->step < 2 ? 2 : 1);
      const s32 from[3] = {p[0], p[1] - 0xBB8000, p[2]};
      Shot* r = effect_spawn_streak_at(from, p, 0x404040, 0x808080, 0xF0, 0x10000, 0x1000, 3);
      if (r != nullptr) bobj_share_sprite(&sc->obj, r);
    }
    sc->s2c &= 0xFFF;
  }
  if (0x40 < sc->obj.rgb[0]) {
    const u32 v = static_cast<u32>(sc->obj.rgb[0]) - static_cast<u32>(g.frame_ticks * 4);
    sc->obj.rgb[0] = static_cast<u8>(v);
    if ((v & 0xFF) < 0x40) sc->obj.rgb[0] = 0x40;
  }
  if (spellcast_timer_tick(sc) != 0) return;
  switch (sc->step) {
    case 0:
      spellcast_dim_begin(sc, c->is_enemy);
      battle_flash_set(sc, 0x20, 0x30, 0x40);
      sc->obj.colour_mode = 2;
      sc->obj.rgb[0] = 0x40;
      ++sc->step;
      return;
    case 1:
      if (sc->data != nullptr) return;
      if (spellcast_leads_camera(sc)) {
        const s32 up[3] = {0, -0x400000, 0};
        camera_move_to(g.camera, up, 0xC00, static_cast<s32>(0xFFFFFCE4), 0x200, 0x19, 0x100, 0);
        g.camera->zoom_target = 0x800;
        g.camera->zoom_speed = 0x80;
      }
      sc->s28 = 1;
      spellcast_wait(sc, 0xC);
      return;
    case 2:
      sc->s24 = 1;
      if (spellcast_leads_camera(sc)) {
        const s32 at[3] = {static_cast<s32>(0xFFC18000), -0x460000, -0x190000};
        camera_move_to(g.camera, at, 0xAAA, static_cast<s32>(0xFFFFFEE4), 0x300, 300, 0x40, 5);
      }
      sc->s3c = static_cast<s32>(bsfx_play_ui(0x2E));
      ++sc->step;
      return;
    case 3: {
      sc->s20 += g.frame_ticks;
      if (300 < sc->s20) ++sc->step;
      sc->s30 += g.frame_ticks;
      const u32 roll = static_cast<u32>(rng_next() & 0x7FFF);
      if (roll < static_cast<u32>(sc->power * 10 + 0x1000) || 0x78 < sc->s30) {
        // A lightning flash.
        ViewState& v = view();
        v.stage_colour[0] = v.stage_colour[1] = v.stage_colour[2] = 0x200;
        g.flash_active = 1;
        g.flash_delay = 2;
        g.flash_speed = 0x10;
        sc->s30 = 0;
        sc->obj.rgb[0] = 0xFF;
        bsfx_play_ui(0x52);
        sound_handle_fade_out(bsfx_play_ui(0x2D), 0x32, 0);
        const s32 n = rng_between(3, 6);
        for (int i = 0; i < n; ++i) {
          Sprite* sp = spellcast_sprite_spawn(sc, static_cast<int>(rng_range(3)) + 2);
          if (sp == nullptr) continue;
          const s32 x = rng_between_fixed(-2000, 2000);
          const s32 z = rng_between_fixed(-1000, 4000);
          sprite_set_pos(sp, x, static_cast<s32>(0xFF254000), z);
          const s32 scale = rng_between(12000, 20000);
          sp->scale_y = scale;
          sp->scale_x = scale;
          sp->roll = static_cast<s16>(rng_between(-0x155, 0x155));
          sprite_set_fade(sp, 0x80, -2, 10);
        }
      }
      return;
    }
    case 4:
      if (spellcast_leads_camera(sc)) {
        g.camera->zoom_target = 0x1000;
        g.camera->zoom_speed = 0x40;
        camera_move_to(g.camera, kZeroVector, 0xD55, 0x155, 0x1800, 0, 0x80, 5);
        Unit* at = sc->combo == 3 ? c : t;
        camera_move_to(g.camera, at->pos, 0xAAA, 0xAA, 0xA00, 0xF0, 0x80, 5);
      }
      sc->s28 = 2;
      ++sc->step;
      return;
    case 5: {
      sc->s38 += g.frame_ticks;
      s32 every = 8 - sc->power / 0x1E;
      if (every < 2) every = 2;
      if (every <= sc->s38) {
        sc->s38 -= every;
        int side = c->is_enemy;
        if (sc->combo == 3) side ^= 1;
        Unit* u = nullptr;
        for (int tries = 0;;) {
          u = side == 0 ? static_cast<Unit*>(g.enemy_units[rng_range(8)]) : static_cast<Unit*>(g.party_units[rng_range(4)]);
          if (!unit_is_down(u) || ++tries >= 0x32) break;  // (50 picks at most)
        }
        if (!unit_is_down(u)) {
          if (Shot* b = shot_spawn_callback_always(bolt)) {
            g_trail.erase(b);
            if (static_cast<u32>(rng_next() & 0x7FFF) < 0x1000) bsfx_play_at(0x52, b);
            if (static_cast<u32>(rng_next() & 0x7FFF) < 0x2000) bsfx_play_at(0x34, b);
            s32 p[4];
            stage_random_point(p, 1);
            b->pos[0] = p[0], b->pos[1] = p[1], b->pos[2] = p[2];
            b->pos[1] = static_cast<s32>(0xFF448000);
            s32 point[4];
            const int r = spellcast_combo_point(sc, point);
            if (r == 0) {
              point[0] = u->pos[0];
              point[1] = u->pos[1] + u->size[1] * -0x2000;
              point[2] = u->pos[2];
            }
            b->point[0] = point[0], b->point[1] = point[1], b->point[2] = point[2];
            const Vec4 v = vec_velocity_toward(b->pos, b->point, 0x30);
            b->vel[0] = v.x, b->vel[1] = v.y, b->vel[2] = v.z, b->extra34 = v.w;
            b->aux = static_cast<s16>(r);
            b->target = u;
            b->scale_y = 0x2000;
            b->scale_x = 0x2000;
            bobj_share_sprite(&sc->obj, b);
          }
        }
      }
      sc->s34 += g.frame_ticks;
      if (0xB4 < sc->s34) {
        ++sc->step;
        sound_handle_fade_out(static_cast<u32>(sc->s3c), 0x3C, 0);
      }
      return;
    }
    case 6:
      sc->s24 = 0;
      if (shot_kind21_absent(bolt) == 0 || spellcast_effects_gone(sc) == 0) return;
      ++sc->step;
      return;
    case 7:
      spell_hit_all(sc, 0x10, 0x10);
      sc->finished = 1;
      return;
    default: return;
  }
}

}  // namespace

void spell_mod_thundercloud(SpellCast* sc) { thundercloud(sc); }

}  // namespace ss::game::battle
