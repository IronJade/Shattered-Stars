// Spell code module 6d0c9bf130 (entries 0x908 / 0x909): Press (spell 0x27): the caster
// conjures a weight (the data's sprite) that drops on the target's head with a thud (dust,
// a ring, a shake); dizzy stars circle the unit and the target is hit (element 0x200,
// knockback 4). Functions 0x8008cc00 (the handler; its step jump table at 0x8008d62c),
// 0x8008d040 (the weight, a kind-0x21 callback) and 0x8008d418 (the stars, kind 0x21).

#include <cstring>
#include <unordered_map>

#include "game/battle/bsfx.h"
#include "game/battle/camera.h"
#include "game/battle/combat.h"
#include "game/battle/particle.h"
#include "game/battle/render.h"
#include "game/battle/shot.h"
#include "game/battle/spell_modules.h"
#include "game/battle/spellcast.h"
#include "game/battle/sprite.h"
#include "game/battle/unit.h"
#include "game/boot/rng.h"

namespace ss::game::battle {
namespace {

// The SpellCast's fields this module uses: s20 the weight has landed (set by the weight
// through its +0x128), s24 the weight (a Shot*: g_weight here); obj the data's sprite set
// (anim 0 the weight, 2 / 3 the dust, 4 the stars and the caster's glow).
std::unordered_map<const SpellCast*, Shot*> g_weight;

// The weight's cast (its +0x128 points at the cast's s20).
std::unordered_map<const Shot*, SpellCast*> g_cast;

// 0x8008d418: the dizzy stars (anim 4): three round the head of their unit (+0x114) on an
// ellipse (width * 2 + 16 by depth * 2), turning 0x3000 / 360 a tick (+0x12c); after 60
// ticks (+0x128) they shrink, after 120 they go. Animated with flags 0, so they run on in
// the pauses.
void dizzy_stars(Shot* s) {
  BattleGlobals& g = bg();
  s->anim = 4;
  if (0x77 < s->p128) {
    shot_kill(s);
    return;
  }
  if (0x3C < s->p128) {
    const s32 scale = 0x1000 - (s->p128 - 0x3C) * 0x40;
    s->scale_y = scale;
    s->scale_x = scale;
  }
  const Unit* t = s->target;
  const s32 rx = t->size[0] * 2 + 0x10, rz = t->size[2] * 2;
  const s32 cx = t->pos[0], cz = t->pos[2];
  s->pos[0] = cx + bcos(static_cast<u32>(s->p12c)) * rx;
  s->pos[2] = cz + bsin(static_cast<u32>(s->p12c)) * rz;
  s->pos[1] = t->pos[1] - (t->size[1] << 14);
  // ORIGINAL BUG? (to review): the loop draws the star at 120 degrees twice (0, 120, 120,
  // 240), so it is doubled and the animation steps four times a frame.
  for (s32 a = 0x78; a < 0xF1; a += 0x78) {
    bobj_project(s);
    bobj_anim_draw(s, 0);
    const s32 d = (a * 0x1000) / 360;
    s->pos[0] = cx + bcos(static_cast<u32>(s->p12c + d)) * rx;
    s->pos[2] = cz + bsin(static_cast<u32>(s->p12c + d)) * rz;
    bobj_project(s);
    bobj_anim_draw(s, 0);
  }
  if ((g.flags & 8) != 0) return;
  s->p12c = (s->p12c + (g.frame_ticks * 0x3000) / 360) & 0xFFF;
  if ((g.flags & 0xC) == 0) s->p128 += g.frame_ticks;
}

// 0x8008d040: the weight (anim 0, layer 2, a shadow sized by its cel and height) falling
// with gravity (0x400 a tick, at most 0x40 a tick) onto its unit's head (+0x114: a tall
// unit's (width >= 0x39) top, else its feet; none: the ground). There: three sounds, four
// dust sprites, a ring, a shake, the cast's s20 set and, on a unit, the dizzy stars; then
// after 48 ticks it shrinks and after 112 it goes.
void weight(Shot* s) {
  BattleGlobals& g = bg();
  s->anim = 0;
  s->layer = 2;
  bobj_project(s);
  bobj_anim_draw(s, g.flags & 0xC);
  const s32 h = (s->pos[1] - bobj_ground_height(s)) / 16;
  s32 ext = s->ext_x1;
  if (ext < 0) ext = -ext;
  s32 r = ((ext * 4 + h) * s->scale_x) / 0x1000;
  if (r < 0x10) r = 0x10;
  bobj_draw_shadow(s, -r, r, -r, r, 0xFFFFFFFF);
  if ((g.flags & 0xC) != 0) return;
  if (s->p12c != 0) {
    s->p12c += g.frame_ticks;
    if (0x30 < s->p12c) {
      const s32 scale = (s->p12c - 0x30) * -0x40 + 0x1000;
      s->scale_y = scale;
      s->scale_x = scale;
    }
    if (0x6F < s->p12c) {
      g_cast.erase(s);
      shot_kill(s);
    }
    return;
  }
  bobj_move(s, s->vel);
  s->vel[1] += g.frame_ticks * 0x400;
  if (0x40000 < s->vel[1]) s->vel[1] = 0x40000;
  const Unit* t = s->target;
  s32 floor;
  if (t == nullptr) {
    floor = bobj_ground_height(s);
  } else if (t->size[0] < 0x39) {
    floor = t->pos[1];
  } else {
    floor = t->pos[1] - (t->size[1] << 14);
  }
  if (!(floor < s->pos[1])) return;
  s->pos[1] = floor;
  bsfx_play_at(0x25, s);
  bsfx_play_at(0x23, s);
  bsfx_play_at(0x21, s);
  s->p12c = 1;
  for (int k = 0; k < 4; ++k) {
    Sprite* sp = sprite_spawn_from(k < 2 ? 2 : 3, 1, s, nullptr);
    if (sp == nullptr) continue;
    const s32 dx = (k & 1) == 0 ? -0x20000 : 0x20000;
    const s32 dz = k < 2 ? 0x60000 : -0x60000;
    sprite_set_pos(sp, s->pos[0] + dx, s->pos[1], s->pos[2] + dz);
    if ((k & 1) != 0) sp->flags |= 0x20;
  }
  const s32 at[4] = {s->pos[0], s->pos[1], s->pos[2], s->pos_w};
  particle_spawn_ring(at, 0x10, 0x30, 0x30, 0x1E, 0xC0A0A0, 0x302020, 0x30, 0);
  screen_shake2(3, 2, 4, 4, 0x1E);
  {
    const auto it = g_cast.find(s);
    // (The console writes through the pointer whatever became of the cast.)
    if (it != g_cast.end() && (it->second == spellcast_slot(0) || it->second == spellcast_slot(1))) it->second->s20 = 1;
  }
  if (s->target != nullptr) {
    if (Shot* st = shot_spawn_callback_always(dizzy_stars)) {
      st->target = s->target;
      bobj_share_sprite(s, st);
    }
  }
}

// 0x8008cc00: the handler. Power = INT / 8 + power / 16 + 50; the centre 0x514 above the
// target. 0: flash 0x60, the camera on the caster (mode 7). 1 (data loaded): a sound and a
// glow at the caster's hand, wait 115. 2: a sound, wait 20. 3: the weight 0x514 over the
// target (the caster in a combo of kind 3, the combo point in kinds 1 / 2, with no unit),
// the camera orbiting it (none: step 5). 4: the centre follows the weight; once it lands
// the hit on the target (element 0x200, knockback 4). 5: the effects end, done.
void press(SpellCast* sc) {
  BattleGlobals& g = bg();
  Unit* c = sc->caster;
  Unit* t = sc->target;
  sc->centre[0] = t->pos[0];
  sc->centre[1] = t->pos[1] - 0x514000;
  sc->centre[2] = t->pos[2];
  sc->eff_power = c->st()->intel[2] / 8 + sc->power / 16 + 0x32;
  if ((g.flags & 4) != 0) return;
  if (spellcast_timer_tick(sc) != 0) return;
  switch (sc->step) {
    case 0:
      battle_flash_set(sc, 0x60, 0x60, 0x60);
      if (spellcast_leads_camera(sc)) camera_request_mode(g.camera, 7, c);
      ++sc->step;
      return;
    case 1:
      if (sc->data != nullptr) return;
      bsfx_play_at(0x27, c);
      if (Sprite* sp = sprite_spawn_from(4, 1, &sc->obj, nullptr)) {
        const s32 dx = c->facing[0] < 0 ? c->size[0] * -0x4000 : c->size[0] * 0x4000;
        sprite_set_pos(sp, c->pos[0] + dx, c->pos[1] + c->size[1] * -0x2000, c->pos[2]);
        sp->layer = 1;
      }
      spellcast_wait(sc, 0x73);
      return;
    case 2:
      bsfx_play_at(0x32, c);
      spellcast_wait(sc, 0x14);
      return;
    case 3: {
      Shot* s = shot_spawn_callback_always(weight);
      if (s == nullptr) {
        sc->step += 2;
        return;
      }
      if (static_cast<u8>(sc->combo - 1) < 2) {
        s32 p[4] = {s->pos[0], s->pos[1], s->pos[2], s->pos_w};
        spellcast_combo_point(sc, p);
        s->pos[0] = p[0], s->pos[1] = p[1], s->pos[2] = p[2], s->pos_w = p[3];
        s->pos[1] = static_cast<s32>(0xFFAEC000);
        s->target = nullptr;
      } else {
        Unit* u = sc->combo == 3 ? c : t;
        s->pos[0] = u->pos[0];
        s->pos[1] = u->pos[1] - 0x514000;
        s->pos[2] = u->pos[2];
        s->target = u;
      }
      g_cast[s] = sc;  // (+0x128 = &sc->s20)
      g_weight[sc] = s;
      bobj_share_sprite(&sc->obj, s);
      bsfx_play_at(0x38, s);
      if (spellcast_leads_camera(sc)) {
        camera_reset_mode(g.camera);
        camera_orbit_unit(g.camera, s, 0xC00, -0xE3, 0x800, 0, 0x80, 5);
      }
      ++sc->step;
      return;
    }
    case 4: {
      const auto it = g_weight.find(sc);
      if (it != g_weight.end()) {
        const Shot* s = it->second;
        sc->centre[0] = s->pos[0];
        sc->centre[1] = s->pos[1];
        sc->centre[2] = s->pos[2];
        sc->centre[3] = s->pos_w;
      }
      if (sc->s20 != 0) spell_hit_target(sc, 0x200, 4);
      return;
    }
    case 5:
      if (spellcast_effects_gone(sc) == 0) return;
      sc->finished = 1;
      ++sc->step;
      g_weight.erase(sc);
      return;
    default: return;
  }
}

}  // namespace

void spell_mod_press(SpellCast* sc) { press(sc); }

}  // namespace ss::game::battle
