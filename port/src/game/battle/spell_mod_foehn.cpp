// Spell code module 2a9c041ec7 (entries 0x915 / 0x916): Foehn (spell 0xa), a wall of wind
// sweeping the field. One function at 0x8008cc00 (block 0, 2236 bytes).

#include <cstdlib>

#include "game/battle/bsfx.h"
#include "game/battle/camera.h"
#include "game/battle/enemy_support.h"
#include "game/battle/render.h"
#include "game/battle/spellcast.h"
#include "game/battle/spell_fx.h"
#include "game/battle/spell_modules.h"
#include "game/battle/sprite.h"
#include "game/battle/unit.h"
#include "game/boot/rng.h"

namespace ss::game::battle {
namespace {

// The SpellCast's fields this module uses: s20 the gust ticks (step 2), s28 the wall's
// brightness, s2c the wall's z (20.12), s30 the wind sound's handle; obj the data's sprite
// set (anim 1 the wall's top row, 0 the others).
bool lead_cast(const SpellCast* sc) {
  const SpellCast* first = spellcast_slot(0);
  return sc->slot == 0 || (first != nullptr && first->finished != 0);
}

// Steps 3-4: six rows of five patches (3 x 3 quads each) of the wall at z = +0x2c, from
// x -0xc80 to 0xc80, rows 0x96 + 2 * row^2 high.
void draw_wall(SpellCast* sc) {
  s32 z1 = 0x640000, z0 = 0;
  for (int row = 0; row < 6; ++row) {
    BObj* o = &sc->obj;
    o->colour_mode = 2;
    o->anim = static_cast<u16>(row == 0 ? 1 : 0);
    o->rgb[0] = static_cast<u8>(sc->s28);
    bobj_anim_draw(o, 0x9800);
    const FrameUv& f = frame_uv();
    const s32 uv[4] = {f.u0, f.v0, f.u1 - 1, f.v1 - 1};
    const u16 tpage = f.tpage, clut = f.clut;
    const s32 y_top = ((row + 1) * (row + 1) * -2 - 0x96) * 0x1000;
    const s32 y_low = (row * row * -2 - 0x96) * 0x1000;
    for (int k = 0; k < 5; ++k) {
      s32 c[4][4] = {};
      c[0][0] = (k * 0x640 - 0xC80) * 0x1000;
      c[0][1] = y_top;
      c[0][2] = sc->s2c + z1;
      c[1][0] = ((k + 1) * 0x640 - 0xC80) * 0x1000;
      c[1][1] = y_top;
      c[1][2] = c[0][2];
      c[2][0] = c[0][0];
      c[2][1] = y_low;
      c[2][2] = sc->s2c + z0;
      c[3][0] = c[1][0];
      c[3][1] = y_low;
      c[3][2] = c[2][2];
      const u32 code = bobj_poly_tag(o, tpage);
      draw_textured_patch(uv, c, clut, static_cast<u16>(tpage | 0x20), 4, code, 3);
    }
    z0 += 0x640000;
    z1 += 0x640000;
  }
}

void foehn(SpellCast* sc) {
  BattleGlobals& g = bg();
  const Unit* t = sc->target;
  sc->centre[0] = t->pos[0];
  sc->centre[1] = t->pos[1] + t->size[1] * -0x2000;
  sc->centre[2] = t->pos[2];
  sc->centre[3] = 0;
  sc->eff_power = sc->caster->st()->intel[2] + sc->power / 2 + 0x2EE;
  if (static_cast<u32>(sc->step - 3) < 2) draw_wall(sc);
  if ((g.flags & 4) != 0) return;
  if (spellcast_timer_tick(sc) != 0) return;
  switch (sc->step) {
    case 0:
      spellcast_dim_begin(sc, sc->caster->is_enemy);
      battle_flash_set(sc, 0x48, 0x30, 0x30);
      if (lead_cast(sc)) camera_move_to(g.camera, sc->caster->pos, 0xD55, 0x155, 0xC00, 0x3C, 0x80, 5);
      spellcast_wait(sc, 0x1E);
      return;
    case 1:
      if (sc->data != nullptr) return;
      ++sc->step;
      return;
    case 2:
      sc->s20 += g.frame_ticks;
      if (sc->s20 < 0x3D) {
        if (Sprite* sp = spellcast_sprite_spawn(sc, static_cast<int>(rng_range(2)) + 3)) {
          const Vec4 at = vec_random_in_disc_xz(sc->caster->pos, 1000);
          sprite_set_pos_v(sp, at.data());
          sp->accel[1] = static_cast<s32>(0xFFFFFC00);
          sp->vel[1] = static_cast<s32>(0xFFFFE000);
          sprite_set_fade(sp, 0x80, -2, 8);
          sp->scale_x = rng_between(0x800, 0x2000);
          sp->scale_y = rng_between(0x800, 0x2000);
          if (static_cast<u32>(rng_next() & 0x7FFF) < 0x2000) bsfx_play_at(0x21, sp);
        }
      } else {
        ++sc->step;
        if (lead_cast(sc)) {
          s32 yaw;
          if (rng_range(2) == 0) {
            camera_move_to(g.camera, kZeroVector, 0x4E3, 0xAA, 0x1800, 0, 0x80, 5);
            yaw = 0xA38;
          } else {
            camera_move_to(g.camera, kZeroVector, 0x2AA, 0xAA, 0x1800, 0, 0x80, 5);
            yaw = 0xD55;
          }
          camera_move_to(g.camera, kZeroVector, yaw, 0xE3, 0x1000, 0xB4, 0x80, 5);
        }
        sc->s30 = static_cast<s32>(bsfx_play_at(0x2D, &sc->obj));
      }
      sc->s2c = 0xBB8000;
      return;
    case 3: {
      // The wall's sounds and leaves on the units of the side it sweeps.
      int side = sc->caster->is_enemy;
      if (sc->combo == 3) side ^= 1;
      // ORIGINAL BUG (original_bugs.md S1): the lists are swapped: side 0 (a party caster)
      // walks 8 entries from the party list (the 4 members, then monsters 0-3), side 1 (a
      // monster) the first 4 monsters, so the effects play on the caster's own side.
      // Fixed visually (user, 2026-10-22): the console's walk stays, with its spawns and
      // random draws, but its leaves are drawn at zero size; the sounds and the leaves go to
      // the swept side, the leaves visual-only (visual_rng_*, not counted by
      // spellcast_effects_gone), so the game's RNG and the spell's length stay the original's.
      // SS_ORIGINAL_S1=1 shows the original leaves.
      static const bool original = std::getenv("SS_ORIGINAL_S1") != nullptr;
      const int count = side == 0 ? 8 : 4;
      const s32 big = sc->power * 10;
      s32 n = sc->power / 0x28 + 1;
      if (4 < n) n = 4;
      auto swept = [&](const Unit* u) {
        return !unit_is_down(u) && u->pos[2] < sc->s2c && sc->s2c + g.frame_ticks * -0x3C000 <= u->pos[2];
      };
      for (int i = 0; i < count; ++i) {
        Unit* u = side == 0 ? (i < 4 ? static_cast<Unit*>(g.party_units[static_cast<size_t>(i)])
                                     : static_cast<Unit*>(g.enemy_units[static_cast<size_t>(i - 4)]))
                            : static_cast<Unit*>(g.enemy_units[static_cast<size_t>(i)]);
        if (!swept(u)) continue;
        if (original) {
          bsfx_play_at(0x4C, u);
          bsfx_play_at(0x34, u);
        }
        for (int k = 0; k < n; ++k) {
          Sprite* sp = spellcast_sprite_spawn(sc, 5);
          if (sp == nullptr) continue;
          const s32 dx = rng_between_fixed(-0x80, 0x80);
          const s32 dy = rng_between_fixed(-0x80, 0x80);
          const s32 dz = rng_between_fixed(-0x80, 0x80);
          sprite_set_pos(sp, u->pos[0] + dx, u->pos[1] + dy, u->pos[2] + dz);
          sprite_set_fade(sp, 0x80, -4, 8);
          sp->size[2] = static_cast<s16>(rng_range(8));
          const s32 sc_ = rng_between(big + 6000, big + 12000);
          sp->scale_y = original ? sc_ : 0;
          sp->scale_x = original ? sc_ : 0;
          sp->layer = 3;
        }
      }
      if (!original) {
        const int other = side == 0 ? 8 : 4;
        for (int i = 0; i < other; ++i) {
          Unit* u = side == 0 ? static_cast<Unit*>(g.enemy_units[static_cast<size_t>(i)])
                              : static_cast<Unit*>(g.party_units[static_cast<size_t>(i)]);
          if (!swept(u)) continue;
          bsfx_play_at(0x4C, u);
          bsfx_play_at(0x34, u);
          for (int k = 0; k < n; ++k) {
            Sprite* sp = visual_sprite_spawn(&sc->obj, 5);
            if (sp == nullptr) continue;
            const s32 dx = visual_rng_between_fixed(-0x80, 0x80);
            const s32 dy = visual_rng_between_fixed(-0x80, 0x80);
            const s32 dz = visual_rng_between_fixed(-0x80, 0x80);
            sprite_set_pos(sp, u->pos[0] + dx, u->pos[1] + dy, u->pos[2] + dz);
            sprite_set_fade(sp, 0x80, -4, 8);
            sp->size[2] = static_cast<s16>(visual_rng_next() % 8);
            const s32 sc_ = big + 6000 + static_cast<s32>(visual_rng_next() % 6000);
            sp->scale_y = sc_;
            sp->scale_x = sc_;
            sp->layer = 3;
          }
        }
      }
      sc->s2c += g.frame_ticks * -0x3C000;
      sc->obj.pos[2] = sc->s2c;
      sc->s28 += g.frame_ticks * 4;
      if (0x80 < sc->s28) sc->s28 = 0x80;
      if (sc->s2c < -0x960000) {
        ++sc->step;
        sound_handle_fade_out(static_cast<u32>(sc->s30), 0x3C, 0);
      }
      return;
    }
    case 4:
      sc->s2c += g.frame_ticks * -0x60000;
      sc->s28 += g.frame_ticks * -2;
      if (-1 < sc->s28) return;
      ++sc->step;
      return;
    case 5:
      if (spellcast_effects_gone(sc) == 0) return;
      ++sc->step;
      return;
    case 6:
      spellcast_dim_end(sc);
      spell_hit_all(sc, 8, 0x10);
      sc->finished = 1;
      return;
    default: return;
  }
}

}  // namespace

void spell_mod_foehn(SpellCast* sc) { foehn(sc); }

}  // namespace ss::game::battle
