// Spell code module d00581e30f (entries 0x8ea / 0x8eb): Shadow Bolt (spell 0x20): a dark
// circle (the data's model) spreads under the target while the camera circles it; for 120
// ticks shadows (the cast's sprite) rise round its edge and fly up into a point above it (or
// the combo point); then the target is hit (element 0x100). Functions 0x8008cc00 (the
// handler) and 0x8008d1dc (a shadow, a kind-0x21 callback).

#include "game/battle/bsfx.h"
#include "game/battle/camera.h"
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

// The SpellCast's fields: s20 the circle's turn (and the camera's yaw * 2), s24 its
// brightness, s28 set when it fades, s2c the ticks of step 1, s30 the shadows' spawn
// accumulator (20.12); obj the data's sprite set (anims 0-8 the shadows by +0x130).

// 0x8008d1dc: a shadow of kind +0x130 (0-2): it plays its rising anim (kind * 3 + 1) once,
// then (+0x128 1) flies at +0x118 (anim kind * 3), speeding up to 16 units a tick; there
// (+0x10c 3 first turns to the point kept in its facing, sfx 0x4f) a burst (anim kind * 3 +
// 3 of its set), sfx 0x4d, and it goes.
void shadow(Shot* s) {
  BattleGlobals& g = bg();
  s->anim = static_cast<u16>(static_cast<s16>(s->p130) * 3 + 1 - static_cast<s16>(s->p128));
  if (s->p128 == 0) s->loop_limit = 1;
  bobj_project(s);
  const int r = bobj_anim_draw(s, g.flags & 0xBF);
  if (static_cast<s16>(r) == -1 && s->p128 == 0) {
    s->p128 = 1;
    s->loop_limit = 0;
  }
  if ((g.flags & 0xBF) != 0) return;
  if (s->p128 != 0) {
    const Vec4 d = vec_direction(s->pos, s->point);
    s->vel[0] = d.x, s->vel[1] = d.y, s->vel[2] = d.z, s->extra34 = d.w;
    const s32 k = s->p134 / 2;
    s->vel[0] *= k;
    s->vel[1] *= k;
    s->p134 += g.frame_ticks;
    s->vel[2] *= k;
    if (0x20 < s->p134) s->p134 = 0x20;
  }
  bobj_move(s, s->vel);
  if (!(vec_distance_fx(s->pos, s->point) < 0x44000)) return;
  if (s->aux == 3) {
    s->aux = 0;
    s->point[0] = s->facing[0], s->point[1] = s->facing[1], s->point[2] = s->facing[2];
    bsfx_play_at(0x4F, s);
    return;
  }
  Sprite* sp = sprite_spawn_from(s->p130 * 3 + 3, 1, s, nullptr);
  if (sp != nullptr) sprite_set_pos_v(sp, s->pos);
  bsfx_play_at(0x4D, sp);
  shot_kill(s);
}

void shadow_bolt(SpellCast* sc) {
  BattleGlobals& g = bg();
  Unit* c = sc->caster;
  Unit* t = sc->target;
  sc->centre[0] = t->pos[0];
  sc->centre[1] = t->pos[1] - 0x60000;
  sc->centre[2] = t->pos[2];
  s32 i8 = c->st()->intel[2];
  if (i8 < 0) i8 += 7;
  s32 p8 = sc->power;
  if (p8 < 0) p8 += 7;
  sc->eff_power = (i8 >> 3) + (p8 >> 3) + 0x78;
  if (sc->data == nullptr) {
    // The circle: the data's model 0 flat under the target (x / z scale its size / 40),
    // turned by s20, at grey s24.
    const s32 wide = (t->size[0] << 12) / 0x28;
    stage_colour_set_grey(sc->s24);
    const s32 light[3] = {0, sc->s20, 0};
    lights_build_oriented(light);
    s32 orient[3] = {0, sc->s20, 0};
    const s32 scale[3] = {wide, 0x1000, wide};
    obj_set_matrix(t->pos, orient, scale);
    if (const u8* m = spellcast_model(sc, 0)) model_draw(m, sc->obj.animset->clut[0], sc->obj.animset->tpage[0], 6);
    stage_colour_restore();
  }
  if ((g.flags & 4) != 0) return;
  if (sc->data == nullptr) {
    // It turns; it brightens and hides the target's shadow (+0x14e up to 0x200), or fades.
    sc->s20 = (sc->s20 + g.frame_ticks * 0x14) & 0x1FFF;
    if (sc->s28 == 0) {
      sc->s24 += g.frame_ticks * 3;
      if (0x80 < sc->s24) sc->s24 = 0x80;
      if (t->shadow_scale < 0x200) t->shadow_scale = static_cast<s16>(t->shadow_scale + g.frame_ticks * 8);
    } else {
      sc->s24 += g.frame_ticks * -3;
      if (sc->s24 < 0) sc->s24 = 0;
      t->shadow_scale = static_cast<s16>(t->shadow_scale + g.frame_ticks * -8);
      if (t->shadow_scale < 0x100) t->shadow_scale = 0x100;
    }
  }
  if (spellcast_leads_camera(sc)) camera_move_to(g.camera, t->pos, sc->s20 / 2, 0xAA, 0x800, 0, 0x80, 5);
  if (spellcast_timer_tick(sc) != 0) return;
  switch (sc->step) {
    case 0:
      spellcast_wait(sc, 0x1E);
      return;
    case 1: {
      // For 120 ticks (data loaded) (power * 4 + 0x200) / 0x1000 shadows a tick (at most 2
      // a frame) round the circle's edge, aimed 0xc0 above themselves or at the combo point.
      if (sc->data != nullptr) return;
      sc->s2c += g.frame_ticks;
      if (0x78 < sc->s2c) {
        sc->s28 = 1;
        ++sc->step;
      }
      const u32 acc = static_cast<u32>(sc->s30 + (sc->power * 4 + 0x200) * g.frame_ticks);
      s32 n = static_cast<s32>(acc);
      if (n < 0) n += 0xFFF;
      n >>= 12;
      sc->s30 = static_cast<s32>(acc & 0xFFF);
      if (2 < n) n = 2;
      for (int i = 0; i < n; ++i) {
        Shot* s = shot_spawn_callback_always(shadow);
        const u32 a = rng_range(0x1000);
        const s32 r = t->size[0] * 3;
        if (s == nullptr) continue;
        s->pos[0] = t->pos[0] + bcos(a) * r;
        s->pos[1] = t->pos[1];
        s->pos[2] = t->pos[2] + bsin(a) * r;
        s->p130 = static_cast<s32>(rng_range(3));
        bobj_share_sprite(&sc->obj, s);
        bsfx_play_at(0x31, s);
        s32 point[4];
        const int k = spellcast_combo_point(sc, point);
        if (k == 0) {
          point[0] = s->pos[0];
          point[2] = s->pos[2];
          point[1] = s->pos[1] - 0xC0000;
        }
        s->point[0] = point[0], s->point[1] = point[1], s->point[2] = point[2];
        if (k == 3) {
          // The caster's position, kept in the shadow's facing for its second leg.
          s->facing[0] = c->pos[0], s->facing[1] = c->pos[1], s->facing[2] = c->pos[2];
        }
        s->aux = static_cast<s16>(k);
      }
      return;
    }
    case 2:
      if (spellcast_effects_gone(sc) != 0) ++sc->step;
      return;
    case 3:
      bsfx_play_at(0x4E, t);
      if (spellcast_leads_camera(sc)) {
        camera_reset_mode(g.camera);
        camera_move_to(g.camera, t->pos, 0xC00, 0xE3, 0x1000, 0x3C, 0x80, 5);
      }
      spell_hit_target(sc, 0x100, 0x10);
      sc->finished = 1;
      return;
    default: return;
  }
}

}  // namespace

void spell_mod_shadow_bolt(SpellCast* sc) { shadow_bolt(sc); }

}  // namespace ss::game::battle
