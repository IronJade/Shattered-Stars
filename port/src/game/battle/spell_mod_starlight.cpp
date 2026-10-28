// Spell code module af4ef66c53 (entries 0x8e7-0x8e9): Starlight (spell 0x12): stars (the
// data's seven star models) rise round the caster to a point 0x640 above the target, then
// a meteor of light falls on every unit of the other side. Functions 0x8008cc00 (the
// handler), 0x8008d1d0 (a rising star, a kind-0x21 callback) and 0x8008d440 (a falling
// beam, kind 0x21).

#include <unordered_map>

#include "game/battle/bsfx.h"
#include "game/battle/camera.h"
#include "game/battle/combat.h"
#include "game/battle/enemy_support.h"
#include "game/battle/render.h"
#include "game/battle/shot.h"
#include "game/battle/spell_fx.h"
#include "game/battle/spell_modules.h"
#include "game/battle/spellcast.h"
#include "game/battle/sprite.h"
#include "game/battle/unit.h"
#include "game/boot/rng.h"
#include "game/title/new_game.h"

namespace ss::game::battle {
namespace {

// A rising star's model (Shot +0x128 holds the pointer on the console).
std::unordered_map<const Shot*, const u8*> g_star_model;

// 0x8008d1d0: a star model spinning (+0x134 the turn, then the speed) toward +0x118,
// faster and faster; near it, it fades (+0x12c); sparkles behind it.
void rising_star(Shot* s) {
  BattleGlobals& g = bg();
  s32 orient[3] = {0, s->p134, 0};
  const s32 scale[3] = {0x2000, 0x2000, 0x2000};
  shot_fade_out(s);
  stage_colour_set_grey(s->colour_mode != 0 ? s->rgb[0] : 0x80);
  lights_build_oriented(orient);
  obj_set_matrix(s->pos, orient, scale);
  const auto it = g_star_model.find(s);
  const u8* model = it != g_star_model.end() ? it->second : nullptr;
  const u16 clut = static_cast<u16>(g.sprite_bank_fx->animset->clut[0] + 7);
  if (model != nullptr) model_draw(model, clut, 0x1A, 3);
  stage_colour_restore();
  if ((g.flags & 4) != 0) return;
  const Vec4 d = vec_direction(s->pos, s->point);
  s->vel[0] = (d.x * s->p134) >> 12;
  s->vel[1] = (d.y * s->p134) >> 12;
  s->vel[2] = (d.z * s->p134) >> 12;
  s->extra34 = d.w;
  bobj_move(s, s->vel);
  s->p134 += (g.frame_ticks << 12) >> 1;
  if (vec_distance_fx(s->pos, s->point) < s->p134 * 0x1C && s->p12c == 0) s->p12c = 1;
  s->p134 += (g.frame_ticks * 0xA000) / 0x168;
  if (Sprite* sp = sprite_spawn_from(static_cast<int>(rng_range(3)) + 8, 0, g.sprite_bank_fx.get(), nullptr)) {
    const s32 dx = rng_between(static_cast<s32>(0xFFFD0000), 0x30000);
    sprite_set_pos(sp, s->pos[0] + dx, s->pos[1], s->pos[2]);
    sprite_set_fade(sp, 0x80, -4, 0);
    sp->accel[1] = 0x400;
  }
}

// 0x8008d440: a beam falling from the stars' point on a unit (+0x118), its length growing
// then shortening as it speeds up; at the unit (+0x10c the combo kind: 2 nothing, 0 the
// full impact) a sphere, spark rings, a vortex, a sound and a shake; then it fades.
void falling_beam(Shot* s) {
  BattleGlobals& g = bg();
  const Vec4 d = vec_direction(s->pos, s->point);
  s->vel[0] = d.x, s->vel[1] = d.y, s->vel[2] = d.z, s->extra34 = d.w;
  const s32 dist = vec_distance_fx(s->pos, s->point);
  const s32 units = dist >> 12;
  const s32 len = s->p12c < units ? s->p12c : units;
  const u32 a = scale_colour(0x1020, s->p130 * -8 + 0x100);
  const u32 b = scale_colour(0xFF0C040, s->p130 * -8 + 0x100);
  draw_beam(s->pos, s->vel, a, b, len, 1, 0x18, 3);
  if ((g.flags & 0xC) != 0) return;
  if (s->p130 != 0) {
    s->p130 += g.frame_ticks;
    if (0x20 < s->p130) shot_kill(s);
    return;
  }
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
  if (!(dist < s->p134 * 0xC)) return;
  s->p130 = 1;
  if (s->aux == 2) return;
  const s32 ground = bobj_ground_height(s);
  if (s->aux == 0) {
    s32 at[3] = {s->point[0], ground - 0x10000, s->point[2]};
    effect_spawn_sphere_at_default(at, 7, 7, 0x20, 0x5A, 0xFFC040, 8, 0);
    if (Shot* r = effect_spawn_sparks_at(at, 0x12, 0xA0, 0x17C, 0x24, 0x78, 4, 1, 0x20, 0)) r->vel[0] = 0xB;
    if (Shot* r = effect_spawn_sparks_at(at, 0x12, 0x168, 0x3C, 0x24, 0x78, 0xB, 3, 0x60, 0))
      r->vel[0] = static_cast<s32>(0xFFFFFFF5);
    at[1] = ground;
    effect_spawn_vortex_at(at, 6, 7, 0x20, 0x5A, 0xFF4020, 0x403010, 0x78, 0x14, 0);
  }
  bsfx_play_at(0x48, s->target);
  screen_shake(1, 4, 2, 2, 6);
}

void starlight(SpellCast* sc) {
  BattleGlobals& g = bg();
  Unit* c = sc->caster;
  sc->centre[0] = sc->target->pos[0];
  sc->centre[1] = sc->target->pos[1];
  sc->centre[2] = sc->target->pos[2];
  s32 i8 = c->st()->intel[2];
  if (i8 < 0) i8 += 7;
  s32 p4 = sc->power;
  if (p4 < 0) p4 += 3;
  sc->eff_power = (i8 >> 3) + (p4 >> 2) + 0x82;
  if ((g.flags & 4) != 0) return;
  if (spellcast_timer_tick(sc) != 0) return;
  switch (sc->step) {
    case 0:
      spellcast_dim_begin(sc, c->is_enemy);
      battle_flash_set(sc, 0x40, 0x40, 0x40);
      if (spellcast_leads_camera(sc)) camera_request_mode(g.camera, 7, c);
      spellcast_wait(sc, 0x1E);
      return;
    case 1: {
      if (sc->data != nullptr) return;
      s32 p16 = sc->power;
      if (p16 < 0) p16 += 0xF;
      s32 n = (p16 >> 4) + 8;
      if (0x10 < n) n = 0x10;
      for (int i = 0; i < n; ++i) {
        Shot* s = shot_spawn_callback_always(rising_star);
        if (s != nullptr) {
          s->pos[0] = c->pos[0] + rng_between(c->size[0] * -0x6000, c->size[0] * 0x6000);
          s->pos[1] = c->pos[1] - static_cast<s32>(rng_range(static_cast<u32>(c->size[1] << 14)));
          s->pos[2] = c->pos[2] + rng_between(c->size[2] * -0x4000, c->size[2] << 14);
          sc->s20 = spellcast_combo_point(sc, sc->aux_point);
          if (sc->s20 == 0) {
            sc->aux_point[0] = s->pos[0];
            sc->aux_point[1] = s->pos[1] - 0x640000;
            sc->aux_point[2] = s->pos[2];
          }
          s->point[0] = sc->aux_point[0];
          s->point[1] = sc->aux_point[1];
          s->point[2] = sc->aux_point[2];
          g_star_model[s] = spellcast_model(sc, static_cast<int>(rng_range(7)));
          s->p134 = static_cast<s32>(rng_range(0x2000));
        }
        if (i == 0 && sc->slot == 0) {
          camera_reset(g.camera);
          camera_request_mode(g.camera, 7, c);
          camera_set_lookat_unit(g.camera, c, s);
        }
      }
      bsfx_play_ui(0x26);
      ++sc->step;
      return;
    }
    case 2:
      if (shot_kind21_absent(rising_star) == 0) return;
      ++sc->step;
      return;
    case 3: {
      bsfx_play_ui(0x3F);
      bsfx_play_ui(0x22);
      if (sc->s20 == 3 || sc->s20 == 0) {
        int side = c->is_enemy;
        if (sc->s20 == 3) side ^= 1;
        const int count = side != 0 ? 4 : 8;
        for (int i = 0; i < count; ++i) {
          // (The original walks the enemy list, or the party list when the side is the party.)
          Unit* u = side == 0 ? static_cast<Unit*>(g.enemy_units[static_cast<size_t>(i)])
                              : static_cast<Unit*>(g.party_units[static_cast<size_t>(i)]);
          if (unit_is_down(u)) continue;
          Shot* s = shot_spawn_callback_always(falling_beam);
          if (s == nullptr) continue;
          s->pos[0] = sc->aux_point[0];
          s->pos[1] = sc->aux_point[1];
          s->pos[2] = sc->aux_point[2];
          s->pos_w = sc->aux_point[3];
          s->point[0] = u->pos[0];
          s->point[1] = u->pos[1] + u->size[1] * -0x2000;
          s->point[2] = u->pos[2];
          s->power = static_cast<s16>(sc->power);
          s->target = u;
          s->aux = static_cast<s16>(sc->s20);
        }
        spellcast_wait(sc, 0x3C);
      } else {
        ++sc->step;
      }
      if (spellcast_leads_camera(sc)) {
        camera_move_to(g.camera, sc->target->pos, 0xC00, 0x1C7, 0x800, 0, 0x80, 5);
        camera_reset(g.camera);
        const s32 yaw = rng_range(2) != 0 ? 0xCE3 : 0xB1C;
        camera_move_to(g.camera, sc->target->pos, yaw, 0xAA, 0x1000, 0x3C, 0x80, 5);
      }
      return;
    }
    case 4:
      spell_hit_all(sc, 0x20, 0x10);
      sc->finished = 1;
      return;
    default: return;
  }
}

}  // namespace

void spell_mod_starlight(SpellCast* sc) { starlight(sc); }

}  // namespace ss::game::battle
