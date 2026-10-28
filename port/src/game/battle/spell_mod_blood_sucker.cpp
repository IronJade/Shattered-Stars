// Spell code module 6d3d24cf0b (entries 0x8ec-0x8f2): Blood Sucker (spell 0x2d) and Mind
// Absorber (spell 0x2e; spell_cast sets SpellCast +0x3c to 1): a sphere of light swells on
// the target, the drained HP (or MP, when the target's resist roll lets it through) flows back
// to the caster as a stream of projectiles, and the caster gains what was taken. On combo 3 the
// caster is the one drained. One function at 0x8008cc00 (block 0, 2928 bytes; its step jump
// table at 0x8008d754).

#include "game/battle/bsfx.h"
#include "game/battle/camera.h"
#include "game/battle/combat.h"
#include "game/battle/fx_draw.h"
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

// The SpellCast's fields: s20 the sphere's radius (step 2), s24 the projectiles still to
// send (step 4), s28 the glow on the caster (step 5), s2c the glow's fade begun, s30 the
// victim's HP (MP) at the start (the drain's cap), s34 unused, s38 the HP actually taken,
// s3c 0 Blood Sucker (HP) / 1 Mind Absorber (MP); centre the victim's chest; obj the data's
// sprite set (anim 0 the glow; the projectiles' sprites).

void blood_sucker(SpellCast* sc) {
  BattleGlobals& g = bg();
  // The victim and the one who gains (swapped on combo 3).
  Unit* victim = sc->target;
  Unit* gainer = sc->caster;
  if (sc->combo == 3) {
    gainer = sc->target;
    victim = sc->caster;
  }
  sc->centre[0] = victim->pos[0];
  sc->centre[1] = victim->pos[1] + victim->size[1] * -0x2000;
  sc->centre[2] = victim->pos[2];
  const s32 intel = sc->caster->st()->intel[2];
  const s32 power = sc->s3c == 0 ? intel / 4 + (sc->power / 4 + 300) : intel / 16 + (sc->power / 16 + 0x32);
  sc->eff_power = power;
  if (sc->s30 < power) sc->eff_power = sc->s30;
  if (sc->combo == 4) {
    const s32 other = spellcast_slot(sc->slot ^ 1)->eff_power;
    sc->eff_power += sc->s3c == 0 ? other : other / 10;
  }
  if (sc->combo == 1) sc->eff_power = 0;
  if (sc->step == 5) {
    // The glow on the gainer.
    BObj* o = &sc->obj;
    o->pos[0] = gainer->pos[0];
    // Original quirk (original_bugs.md S7; kept by the user's decision, 2026-10-22): the height is y - (y << 13) (y * -0x1fff), surely meant as
    // y - (size y << 13) (the chest); right only for a unit standing at y 0.
    o->pos[1] = static_cast<s32>(static_cast<u32>(gainer->pos[1]) - (static_cast<u32>(gainer->pos[1]) << 13));
    o->colour_mode = 2;
    o->anim = 0;
    o->layer = 1;
    o->rgb[0] = static_cast<u8>(sc->s28);
    o->pos[2] = gainer->pos[2];
    bobj_project(o);
    bobj_anim_draw(o, 0);
  }
  if ((g.flags & 4) != 0) return;
  if (spellcast_timer_tick(sc) != 0) return;
  switch (sc->step) {
    case 0:
      battle_flash_set(sc, 0x40, 0x40, 0x40);
      if (spellcast_leads_camera(sc)) camera_move_to(g.camera, sc->centre, 0xC00, 0xAA, 0x800, 0x19, 0x80, 5);
      spellcast_wait(sc, 0x1E);
      sc->s30 = sc->s3c != 0 ? victim->st()->mp : victim->st()->hp;
      return;
    case 1:
      // Two spark rings at the target's chest.
      if (sc->data != nullptr) return;
      if (spellcast_leads_camera(sc)) camera_move_to(g.camera, sc->centre, 0xC00, 0x155, 0x1000, 0x3C, 0x80, 5);
      bsfx_play_ui(0x21);
      bsfx_play_ui(0x22);
      for (int i = 0; i < 2; ++i) {
        const Unit* t = sc->target;
        const s32 at[3] = {t->pos[0], t->pos[1] + t->size[1] * -0x2000, t->pos[2]};
        Shot* r = effect_spawn_sparks_at(at, 0x14, 0x1E0, 0, 0x3C, 0x78, sc->s3c == 0 ? 3 : 5, 3, 0, 0);
        if (r != nullptr) r->vel[2] = i << 10;
      }
      ++sc->step;
      return;
    case 2: {
      // The sphere swells on the target (three nested lit spheres) to radius 0xe10.
      g.semi_tpage = 0x20;
      sc->s20 += g.frame_ticks * 0x30;
      lights_build_oriented(kZeroVector);
      const Unit* t = sc->target;
      const s32 at[3] = {t->pos[0], t->pos[1] + t->size[1] * -0x2000, t->pos[2]};
      const s32 scale[3] = {sc->s20, sc->s20, sc->s20};
      s32 orient[3] = {0, 0, 0};
      obj_set_matrix(at, orient, scale);
      const u32 rgb = sc->s3c != 0 ? 0x4060FF : 0x70FFA0;
      draw_sphere(0xB, 0xB, sc->s20, rgb, 0x100, 0, 2, 0x1000);
      draw_sphere(8, 8, sc->s20 / 2, rgb, 0x80, 0, 2, 0x800);
      draw_sphere(8, 8, sc->s20 / 4, rgb, 0x80, 0, 2, 0x1800);
      if (sc->s20 < 0xE11) return;
      ++sc->step;
      return;
    }
    case 3: {
      // The drain: HP through a temporary Shot of the gainer's (element 0x40, knockback 8),
      // or MP when the victim's resist_roll (kind 1, power / 16 + 220) returns 1. Otherwise,
      // or on combo 1: a rising icon and the cast ends.
      const bool lands = (sc->s3c == 0 || victim->resist_roll(1, sc->power / 16 + 0xDC) == 1) && sc->combo != 1;
      if (!lands) {
        battle_flash_set(sc, 0x80, 0x80, 0x80);
        sc->step = 7;
        sc->finished = 1;
        effect_rise_icon(victim, 0);
        return;
      }
      bsfx_play_at(0x4C, victim);
      if (sc->s3c == 0) {
        // (A heap Shot on the console: owner the gainer, x far on the caster's side.)
        Shot tmp;
        tmp.owner = gainer;
        tmp.pos[0] = sc->caster->is_enemy != 0 ? static_cast<s32>(0xFD8F0000) : 0x2710000;
        tmp.pos[1] = 0;
        tmp.pos[2] = 0;
        sc->s38 = victim->st()->hp;
        Unit* list[1] = {victim};
        shot_apply_damage(&tmp, 1, list, nullptr, sc->eff_power, 0, 0x40, 0, 8);
        sc->s38 -= victim->st()->hp;
      } else {
        damage_popup_spawn(victim, sc->eff_power, 0, 1);
        unit_damage_mp(victim, sc->eff_power);
      }
      ++sc->step;
      if (spellcast_leads_camera(sc)) {
        const s32 yaw = rng_range(2) != 0 ? 0xD55 : 0xAAA;
        camera_move_to(g.camera, gainer->pos, yaw, 0x18E, 0x800, 0x1E, 0x80, 5);
      }
      sc->s24 = sc->power / 0x18 + 0x18;
      if (0x30 < sc->s24) sc->s24 = 0x30;
      bsfx_play_ui(0x2C);
      return;
    }
    case 4:
      // Two projectiles a frame from 0x4b0 round the gainer homing on it, s24 / 2 frames.
      if (0 < sc->s24) {
        sc->s24 -= 2;
        for (int i = 0; i < 2; ++i) {
          ShotDesc* d = shot_desc_init(rng_between(1, 0xE));
          d->speed = 0x14;
          d->target = gainer;
          d->flags = 0x6200000;
          const u32 a = rng_range(0x1000);
          s32 from[3];
          from[0] = gainer->pos[0] + bcos(a) * 0x4B0;
          from[1] = gainer->pos[1] + rng_between(static_cast<s32>(0xFFFA0000), 0x10000);
          from[2] = gainer->pos[2] + bsin(a) * 0x4B0;
          d->life = vec_distance_fx(from, gainer->pos) / 0x14000;
          shot_spawn_projectile(gainer, from, d, &sc->obj);
        }
        return;
      }
      ++sc->step;
      return;
    case 5: {
      // The glow on the gainer pulses until every effect is gone, then fades.
      const bool gone = spellcast_effects_gone(sc) != 0;
      if (gone && sc->s2c == 0) {
        sc->s2c = 1;
        battle_flash_set(sc, 0x80, 0x80, 0x80);
      }
      if (sc->s2c == 0) {
        sc->s28 += g.frame_ticks * 4;
        if (0xC0 < sc->s28) sc->s28 = 0x80;
        return;
      }
      sc->s28 += g.frame_ticks * -0x10;
      if (-1 < sc->s28) return;
      ++sc->step;
      return;
    }
    case 6:
      // The gainer gets what was taken.
      if (!unit_is_down(gainer)) {
        if (sc->s3c == 0) {
          damage_popup_spawn(gainer, -sc->s38, 0, 0);
          unit_damage_hp(gainer, -sc->s38);
        } else {
          damage_popup_spawn(gainer, -sc->eff_power, 0, 1);
          unit_damage_mp(gainer, -sc->eff_power);
        }
        bsfx_play_at(0x13, gainer);
      }
      sc->finished = 1;
      ++sc->step;
      return;
    default: return;
  }
}

}  // namespace

void spell_mod_blood_sucker(SpellCast* sc) { blood_sucker(sc); }

}  // namespace ss::game::battle
