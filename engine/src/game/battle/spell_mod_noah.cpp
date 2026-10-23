// Spell code module 6563c70f92 (entries 0x911-0x914): Noah (spell 7), a wall of water that
// sweeps the field from the far side to the near one, splashing every unit it passes, then
// one hit on the other side (element 2). One function at 0x8008cc00 (block 0, 2436 bytes);
// two data tables: the wall's row heights 0x8008d544 (13 s16) and its column offsets
// 0x8008d560 (6 s16). Cast by G-Celesta and Indalecio.

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

constexpr u32 kRowHeights = 0x8008D544;  // s16[13]: the wall's height at each of its 13 row edges
constexpr u32 kColumnLift = 0x8008D560;  // s16[6]: added at each of the 6 column edges

// The SpellCast's fields this module uses: s20 / s30 / s3c three sound handles, s28 the
// wall's brightness, s2c the wall's z (20.12), s34 / s38 the top row's animation frame and
// ticks (kept apart from the other rows'); obj the data's sprite set.

// Steps 2-3: six rows of the wall (anim 2 the top row, 0 the others), each drawn as two
// halves of five patches (3 x 3 quads), the texture upside down and split in two along v;
// the rows stand 0x3e8 apart in z from +0x2c on, each patch's corners raised by the row
// and column tables. Then, outside the pauses, twelve spark lines rising from the wall.
void draw_wall(SpellCast* sc) {
  BObj* o = &sc->obj;
  o->frame = static_cast<s8>(sc->s34);
  o->ticks_left = static_cast<s16>(sc->s38);
  for (int row = 0; row < 6; ++row) {
    o->colour_mode = 2;
    o->rgb[0] = static_cast<u8>(sc->s28);
    o->anim = static_cast<u16>(row == 0 ? 2 : 0);
    bobj_anim_draw(o, 0x9800);
    if (row == 0) {
      sc->s34 = o->frame;
      sc->s38 = o->ticks_left;
    }
    const FrameUv f = frame_uv();
    s32 uv[4] = {f.u0, 0, f.u1, 0};
    for (int half = 0; half < 2; ++half) {
      if (half == 0) {
        uv[1] = f.v1;
        uv[3] = (f.v0 - f.v1) / 2 + f.v1;
      } else {
        uv[1] = uv[3];
        uv[3] = f.v0;
      }
      const int edge = row * 2 + half;
      const s32 h0 = spell_module_s16(sc, kRowHeights + static_cast<u32>(edge) * 2);
      const s32 h1 = spell_module_s16(sc, kRowHeights + static_cast<u32>(edge + 1) * 2);
      const s32 z0 = sc->s2c + edge * 0x3E8000;
      const s32 z1 = sc->s2c + (edge + 1) * 0x3E8000;
      for (int col = 0; col < 5; ++col) {
        const s32 lift0 = spell_module_s16(sc, kColumnLift + static_cast<u32>(col) * 2);
        const s32 lift1 = spell_module_s16(sc, kColumnLift + static_cast<u32>(col + 1) * 2);
        const s32 x0 = (col * 2000 - 4000) * 0x1000;
        const s32 x1 = ((col + 1) * 2000 - 4000) * 0x1000;
        s32 c[4][4] = {};
        c[0][0] = x0;
        c[0][1] = (h0 + lift0 - 100) * 0x1000;
        c[0][2] = z0;
        c[1][0] = x1;
        c[1][1] = (h0 + lift1 - 100) * 0x1000;
        c[1][2] = z0;
        c[2][0] = x0;
        c[2][1] = (h1 + lift0 - 100) * 0x1000;
        c[2][2] = z1;
        c[3][0] = x1;
        c[3][1] = (h1 + lift1 - 100) * 0x1000;
        c[3][2] = z1;
        const u32 code = bobj_poly_tag(o, f.tpage);
        draw_textured_patch(uv, c, f.clut, static_cast<u16>(f.tpage | 0x20), 4, code, 3);
      }
    }
  }
  if ((bg().flags & 0xC) != 0) return;
  for (int i = 0; i < 12; ++i) {
    const s32 x = rng_between_fixed(-4000, 4000);
    const s32 dz = rng_between_fixed(0, 200);
    const s32 from[4] = {x, -0x190000, sc->s2c + dz, 0};
    const Vec4 dir = vec_random_around(kZeroVector, 0x18);
    particle_spawn_spark_line(from, dir.data(), 0x606060, 0xFFFFFF, 0x10, 0x5A);
  }
}

// 0x8008cc00: the handler. Power = INT + power + 1500.
void noah(SpellCast* sc) {
  BattleGlobals& g = bg();
  const Unit* t = sc->target;
  sc->centre[0] = t->pos[0];
  sc->centre[1] = t->pos[1] + t->size[1] * -0x2000;
  sc->centre[2] = t->pos[2];
  sc->centre[3] = 0;  // (the original copies an uninitialised stack word)
  sc->eff_power = sc->caster->st()->intel[2] + sc->power + 0x5DC;
  if (static_cast<u32>(sc->step - 2) < 2) draw_wall(sc);
  if ((g.flags & 4) != 0) return;
  if (spellcast_timer_tick(sc) != 0) return;
  switch (sc->step) {
    case 0:
      spellcast_dim_begin(sc, sc->caster->is_enemy);
      battle_flash_set(sc, 0x20, 0x20, 0x48);
      spellcast_wait(sc, 10);
      return;
    case 1:
      if (sc->data != nullptr) return;
      if (spellcast_leads_camera(sc)) {
        const s32 yaw = rng_range(2) != 0 ? 0xB1C : 0xCE3;
        camera_move_to(g.camera, kZeroVector, yaw, 0x38, 0xC00, 0, 0x80, 5);
        const s32 yaw2 = rng_range(2) != 0 ? 0x555 : 0x238;
        camera_move_to(g.camera, kZeroVector, yaw2, 0x1C7, 0x1400, 300, 0x80, 5);
      }
      sc->s30 = static_cast<s32>(bsfx_play_at(0x2D, &sc->obj));
      sc->s20 = static_cast<s32>(bsfx_play_at(0x44, &sc->obj));
      sc->s3c = static_cast<s32>(bsfx_play_ui(0x2D));
      sc->s2c = 0x1388000;
      ++sc->step;
      screen_shake_vertical(2, 2, 0x5A);
      return;
    case 2: {
      // The splashes on the units the wall passes this frame.
      int side = sc->caster->is_enemy;
      if (sc->combo == 3) side ^= 1;
      // ORIGINAL BUG (original_bugs.md E13, as Foehn's S1): the lists are swapped: side 0 (a
      // party caster) walks 8 entries of the party list, the 4 members and then monsters
      // 0-3, side 1 (a monster) the first 4 monsters, so the splashes play on the caster's
      // own side (the damage, spell_hit_all, is right):
      //   Unit* u = side == 0 ? (i < 4 ? party_units[i] : enemy_units[i - 4]) : enemy_units[i];
      const int count = side == 0 ? 8 : 4;
      for (int i = 0; i < count; ++i) {
        Unit* u = side == 0 ? static_cast<Unit*>(g.enemy_units[static_cast<size_t>(i)])
                            : static_cast<Unit*>(g.party_units[static_cast<size_t>(i)]);
        if (unit_is_down(u)) continue;
        if (!(u->pos[2] < sc->s2c && sc->s2c + g.frame_ticks * -0x3C000 <= u->pos[2])) continue;
        bsfx_play_at(0x4C, u);
        bsfx_play_at(0x34, u);
        const s32 at[3] = {u->pos[0], bobj_ground_height(u), u->pos[2]};
        effect_spawn_sparks_at(at, 0xC, 0x78, 0xF0, 0x20, 0x3C, 4, 1, 0xC0, 0);
        screen_shake(4, 6, 4, 4, 0x1E);
      }
      sc->s2c += g.frame_ticks * -0x3C000;
      sc->obj.pos[2] = sc->s2c;
      sc->s28 += g.frame_ticks * 4;
      if (0x80 < sc->s28) sc->s28 = 0x80;
      if (sc->s2c < -0xC80000) {
        ++sc->step;
        sound_handle_fade_out(static_cast<u32>(sc->s3c), 0x3C, 0);
        sound_handle_fade_out(static_cast<u32>(sc->s20), 0x3C, 0);
        sound_handle_fade_out(static_cast<u32>(sc->s30), 0x3C, 0);
        screen_shake_vertical(2, 2, 0x1E);
      }
      return;
    }
    case 3:
      sc->s2c += g.frame_ticks * -0x60000;
      sc->s28 += g.frame_ticks * -2;
      if (-1 < sc->s28) return;
      ++sc->step;
      return;
    case 4:
      if (spellcast_effects_gone(sc) == 0) return;
      ++sc->step;
      return;
    case 5:
      spellcast_dim_end(sc);
      spell_hit_all(sc, 2, 0x10);
      sc->finished = 1;
      return;
    default: return;
  }
}

}  // namespace

void spell_mod_noah(SpellCast* sc) { noah(sc); }

}  // namespace ss::game::battle
