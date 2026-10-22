// Spell code module 53455d80ad (entries 0x906 / 0x907): Magnum Tornado (spell 0xb): a
// whirlwind of the data's sprite (anim 1) rises at the target for 240 ticks, then everything
// of the other side within its reach is hit. One function at 0x8008cc00 (block 0, 3904 bytes).

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

namespace ss::game::battle {
namespace {

// The SpellCast's fields: s20 ticks of the whirl (step 2), s24 its turn, s30 the camera
// moved, s34 the fade (steps 3-4), s38 / s3c the two wind sounds' handles; obj the data's
// sprite set (anim 1; colour mode 2, grey +0x64 the brightness).

// Steps 2-4: 10 rings x 10 segments of anim 1's cel as one-quad patches round the centre;
// ring k runs from radius k * w (height falling with (10 - k)^4) to (k + 1) * w ((9 - k)^4),
// heights scaled by the ticks, turned by s24 plus 30 degrees a ring.
void draw_tornado(SpellCast* sc, const s32 centre[3], s32 w) {
  BattleGlobals& g = bg();
  BObj* o = &sc->obj;
  o->anim = 1;
  bobj_anim_draw(o, (g.flags & 0xC) | 0x9800);
  const FrameUv& f = frame_uv();
  const s32 uv[4] = {f.u0, f.v0, f.u1, f.v1};
  const u16 tpage = f.tpage, clut = f.clut;
  const u32 code = bobj_poly_tag(o, tpage);
  s32 t8 = sc->s20;
  if (t8 < 0) t8 += 7;
  t8 >>= 3;
  for (int k = 0; k < 10; ++k) {
    const s32 a = 10 - k, b = 9 - k;
    const s32 y_top = -0x64000 - ((t8 * a * a * a * a) / 100) * 0x1000;
    const s32 y_low = -0x64000 - ((t8 * b * b * b * b) / 100) * 0x1000;
    const s32 ang_a = (k * 0xC000) / 360 + sc->s24;
    const s32 ang_b = ((k + 1) * 0xC000) / 360 + sc->s24;
    const s32 r_a = k * w, r_b = (k + 1) * w;
    for (int j = 0; j < 10; ++j) {
      const s32 lo = (j * 0x24000) / 360, hi = ((j + 1) * 0x24000) / 360;
      s32 c[4][4] = {};
      c[0][0] = centre[0] + bcos(static_cast<u32>(ang_a + lo)) * r_a;
      c[0][1] = centre[1] + y_top;
      c[0][2] = centre[2] + bsin(static_cast<u32>(ang_a + lo)) * r_a;
      c[1][0] = centre[0] + bcos(static_cast<u32>(ang_a + hi)) * r_a;
      c[1][1] = centre[1] + y_top;
      c[1][2] = centre[2] + bsin(static_cast<u32>(ang_a + hi)) * r_a;
      c[2][0] = centre[0] + bcos(static_cast<u32>(ang_b + lo)) * r_b;
      c[2][1] = centre[1] + y_low;
      c[2][2] = centre[2] + bsin(static_cast<u32>(ang_b + lo)) * r_b;
      c[3][0] = centre[0] + bcos(static_cast<u32>(ang_b + hi)) * r_b;
      c[3][1] = centre[1] + y_low;
      c[3][2] = centre[2] + bsin(static_cast<u32>(ang_b + hi)) * r_b;
      draw_textured_patch(uv, c, clut, tpage, 8, code, 1);
    }
  }
}

void magnum_tornado(SpellCast* sc) {
  BattleGlobals& g = bg();
  Unit* t = sc->target;
  sc->centre[0] = t->pos[0];
  sc->centre[1] = t->pos[1] + t->size[1] * -0x2000;
  sc->centre[2] = t->pos[2];
  sc->centre[3] = 0;
  Unit* at = sc->combo == 3 ? sc->caster : sc->target;
  const s32 centre[4] = {at->pos[0], bobj_ground_height(at), at->pos[2], 0};
  s32 i4 = sc->caster->st()->intel[2];
  if (i4 < 0) i4 += 3;
  s32 p4 = sc->power;
  if (p4 < 0) p4 += 3;
  sc->eff_power = (i4 >> 2) + (p4 >> 2) + 400;
  s32 w = sc->power / 2 + 100;
  if (200 < w) w = 200;
  if (static_cast<u32>(sc->step - 2) < 3) draw_tornado(sc, centre, w);
  if ((g.flags & 4) != 0 || spellcast_timer_tick(sc) != 0) return;
  switch (sc->step) {
    case 0:
      spellcast_dim_begin(sc, sc->caster->is_enemy);
      battle_flash_set(sc, 0x30, 0x30, 0x30);
      if (spellcast_leads_camera(sc)) {
        camera_reset_mode(g.camera);
        camera_move_to(g.camera, centre, 0xC00, 0xE3, 0x800, 0x14, 0x80, 5);
      }
      spellcast_wait(sc, 0x14);
      return;
    case 1:
      if (sc->data != nullptr) return;
      if (spellcast_leads_camera(sc)) {
        const s32 yaw = rng_range(2) != 0 ? 0xAAA : 0xD55;
        camera_move_to(g.camera, centre, yaw, 0x71, 0xC00, 0x3C, 0x80, 5);
      }
      sc->obj.rgb[0] = 0;
      sc->obj.colour_mode = 2;
      ++sc->step;
      sc->s3c = bsfx_play_at_pos(0x2D, centre);
      sc->s38 = bsfx_play_at_pos(0x2E, centre);
      sound_handle_pitch_up(static_cast<u32>(sc->s38), 0xF0);
      effect_spawn_sphere_at_default(centre, 6, 8, 5, 300, 0xA06020, 0x14, 0);
      effect_spawn_sphere_at_default(centre, 6, 8, 8, 300, 0xC06040, 0x14, 0);
      bsfx_play_at_pos(0x22, centre);
      screen_shake(3, 4, 0xF0);
      return;
    case 2: {
      sc->s20 += g.frame_ticks;
      const u32 lit = static_cast<u32>(sc->obj.rgb[0]) + static_cast<u32>(g.frame_ticks * 2);
      sc->obj.rgb[0] = static_cast<u8>(lit);
      if (0x60 < (lit & 0xFF)) sc->obj.rgb[0] = 0x60;
      if (0x78 < sc->s20 && sc->s30 == 0) {
        sc->s30 = 1;
        if (spellcast_leads_camera(sc)) {
          const s32 yaw = rng_range(2) != 0 ? 0x8E3 : 0xF1C;
          camera_move_to(g.camera, centre, yaw, 0x1C7, 0x1000, 0xB4, 0x80, 5);
        }
      }
      if (Sprite* sp = spellcast_sprite_spawn2(sc, 3, 1)) {
        const Vec4 p = vec_random_in_disc_xz(centre, w * 5);
        sprite_set_pos_v(sp, p.data());
      }
      if (0xF0 < sc->s20) ++sc->step;
      sc->s24 += (g.frame_ticks * 0x5000) / 0x168;
      return;
    }
    case 3: {
      if (1 < static_cast<u8>(sc->combo - 1)) {
        int side = sc->caster->is_enemy;
        if (sc->combo == 3) side ^= 1;
        if (sc->combo == 4) sc->eff_power += spellcast_slot(sc->slot ^ 1)->eff_power;
        const int count = side != 0 ? 4 : 8;
        Unit* list[8] = {};
        int n = 0;
        for (int i = 0; i < count; ++i) {
          Unit* u = side == 0 ? static_cast<Unit*>(g.enemy_units[static_cast<size_t>(i)])
                              : static_cast<Unit*>(g.party_units[static_cast<size_t>(i)]);
          if (unit_is_down(u)) continue;
          if (vec_distance_xz_fx(u->pos, centre) < w * 0x5000) list[n++] = u;
        }
        if (n != 0) {
          bsfx_play_at_pos(0x34, centre);
          bsfx_play_at_pos(0x22, centre);
          bsfx_play_at_pos(0x4B, centre);
          spellcast_damage_units(sc, n, list, sc->eff_power, 8, 0x10, side);
          s32 bits = sc->power / 0x28 + 6;
          if (0xC < bits) bits = 0xC;
          for (int i = 0; i < n; ++i) {
            Unit* u = list[i];
            effect_spawn_sparks_at(u->pos, 10, 0x6E, 200, 0xC, 0x78, 4, 0, 0x28, 0);
            for (int k = 0; k < bits; ++k) {
              Sprite* sp = spellcast_sprite_spawn(sc, 1);
              if (sp == nullptr) continue;
              const s32 dx = rng_between_fixed(-0x100, 0x100);
              const s32 dy = rng_between_fixed(-0x100, 0);
              const s32 dz = rng_between_fixed(-0xC, 0x80);
              sprite_set_pos(sp, u->pos[0] + dx, u->pos[1] + dy, u->pos[2] + dz);
              sp->vel[0] = rng_between_fixed(-8, 8);
              sp->vel[1] = rng_between_fixed(-8, 8);
              sp->vel[2] = rng_between_fixed(-8, 8);
              sprite_set_fade(sp, 0x80, -3, 8);
              sp->size[2] = static_cast<s16>(rng_range(8));
              const s32 scale = rng_between(0x1000, 0x2000);
              sp->scale_y = scale;
              sp->scale_x = scale;
              sp->layer = 3;
            }
          }
        }
      }
      if (spellcast_leads_camera(sc)) camera_move_to(g.camera, centre, 0xC00, 0xE3, 0x800, 0xB4, 0x80, 5);
      sc->s34 = 0x60;
      ++sc->step;
      sound_handle_fade_out(static_cast<u32>(sc->s3c), 0x3C, 0);
      sound_handle_fade_out(static_cast<u32>(sc->s38), 0x3C, 0);
      [[fallthrough]];
    }
    case 4:
      sc->s34 -= g.frame_ticks;
      sc->s24 += (g.frame_ticks * 0x5000) / 0x168;
      if (sc->s34 < 0) {
        ++sc->step;
        battle_flash_set(sc, 0x80, 0x80, 0x80);
        spellcast_dim_end(sc);
      }
      sc->obj.rgb[0] = static_cast<u8>(sc->s34);
      return;
    case 5:
      if (spellcast_effects_gone(sc) != 0) {
        sc->finished = 1;
        ++sc->step;
      }
      return;
    default: return;
  }
}

}  // namespace

void spell_mod_magnum_tornado(SpellCast* sc) { magnum_tornado(sc); }

}  // namespace ss::game::battle
