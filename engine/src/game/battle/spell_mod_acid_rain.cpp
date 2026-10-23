// Spell code module 63f77d1822 (entries 0x8e3 / 0x8e4): Acid Rain (spell 0x6): a cloud layer
// (the data's sprite, 8 x 6 patches) fades in over the field, purple rain streaks fall on
// random points of the arena, and every unit of the other side may be poisoned (status mask
// 1, chance power / 16 + 200, icon 4). One function at 0x8008cc00 (block 0, 1632 bytes; its
// step jump table at 0x8008d248). Resident 0x8007fd54 voice_release (#1984.0):
// enemy_support.h sound_handle_release.

#include "game/battle/bsfx.h"
#include "game/battle/camera.h"
#include "game/battle/enemy_support.h"
#include "game/battle/shot.h"
#include "game/battle/spell_fx.h"
#include "game/battle/spell_modules.h"
#include "game/battle/spellcast.h"
#include "game/battle/sprite.h"
#include "game/battle/unit.h"
#include "game/boot/rng.h"
#include "game/boot/sound.h"

namespace ss::game::battle {
namespace {

// The SpellCast's fields: s20 the cloud's brightness (rising 4 a tick to 0x80), s24 the rain
// on, s28 the cloud on, s2c the streak timer (0x1000 a streak), s3c the rain sound's handle;
// obj the data's sprite set (anim 0 the cloud's cel).

// The cloud (once s28 is set and the data is loaded): 8 rows of 6 one-quad patches (2 x 2)
// of anim 0's cel, 0x9c5 square every 0x9c4 from x -0x2134 / z -0x2328, at y -0xfa0.
void draw_cloud(SpellCast* sc) {
  BattleGlobals& g = bg();
  BObj* o = &sc->obj;
  o->anim = 0;
  bobj_anim_draw(o, 0x9800);
  const FrameUv& f = frame_uv();
  const s32 uv[4] = {f.u0, f.v0, f.u1, f.v1};
  u32 code;
  const s32 lit = sc->s20;
  if (lit < 0x80) {
    code = static_cast<u32>(lit) | static_cast<u32>(lit) << 8 | static_cast<u32>(lit) << 16 | 0x2C000000;
    sc->s20 = lit + g.frame_ticks * 4;
  } else {
    code = 0x2C808080;
  }
  const AnimSet* set = o->animset;
  const u16 clut = set != nullptr ? set->clut[0] : 0;
  const u16 tpage = set != nullptr ? set->tpage[0] : 0;
  const s32 y = static_cast<s32>(0xFF060000);
  for (int row = 0; row < 8; ++row) {
    for (int col = 0; col < 6; ++col) {
      const s32 x0 = col * 0x9C4000 + -0x2134000, x1 = x0 + 0x9C5000;
      const s32 z0 = row * 0x9C4000 + -0x2328000, z1 = z0 + 0x9C5000;
      const s32 c[4][4] = {{x0, y, z0, 0}, {x1, y, z0, 0}, {x0, y, z1, 0}, {x1, y, z1, 0}};
      draw_textured_patch(uv, c, clut, tpage, 0, code, 2);
    }
  }
}

void acid_rain(SpellCast* sc) {
  BattleGlobals& g = bg();
  if (sc->s28 != 0 && sc->data == nullptr) draw_cloud(sc);
  if ((g.flags & 4) != 0) return;
  if (sc->s24 != 0) {
    // The rain: power-scaled streaks (at most 3 a frame) from 0x7d0 above random points.
    s32 rate = sc->power * 0x1E + 0xC00;
    if (0x1000 < rate) rate = 0x1000;
    sc->s2c += rate * g.frame_ticks;
    s32 n = sc->s2c / 0x1000;
    if (3 < n) n = 3;
    for (int i = 0; i < n; ++i) {
      s32 p[4];
      stage_random_point(p, sc->step < 2 ? 2 : 1);
      const s32 from[3] = {p[0], p[1] + -0x7D0000, p[2]};
      Shot* r = effect_spawn_streak_at(from, p, 0x10, 0x4000FF, 0xF0, 0x10000, 0x1000, 2);
      // (The console shares the sprite even when no Shot was free; the port skips it then.)
      if (r != nullptr) bobj_share_sprite(&sc->obj, r);
    }
    sc->s2c &= 0xFFF;
  }
  if (spellcast_timer_tick(sc) != 0) return;
  switch (sc->step) {
    case 0: {
      // Dim, the camera up into the sky, zoomed in.
      spellcast_dim_begin(sc, sc->caster->is_enemy);
      battle_flash_set(sc, 0x20, 0x20, 0x40);
      const s32 up[3] = {0, static_cast<s32>(0xFFC00000), 0};
      camera_move_to(g.camera, up, 0xC00, -0x38E, 0x200, 0x19, 0x100, 5);
      g.camera->zoom_target = 0x800;
      g.camera->zoom_speed = 0x80;
      spellcast_wait(sc, 0x12);
      return;
    }
    case 1:
      sc->s28 = 1;
      spellcast_wait(sc, 0xC);
      return;
    case 2: {
      // The rain begins; the sound.
      sc->s24 = 1;
      const s32 at[3] = {-0xC8000, static_cast<s32>(0xFFBA0000), 0};
      camera_move_to(g.camera, at, 0xB8E, -0x400, 0x300, 0x5A, 0x40, 5);
      sc->s3c = static_cast<s32>(bsfx_play_ui(0x2E));
      spellcast_wait(sc, 0x5A);
      return;
    }
    case 3: {
      // The camera back down: on the target from a random angle, then an overview.
      g.camera->zoom_target = 0x1000;
      g.camera->zoom_speed = 0x1000;
      const s32 deg = rng_spread(0x3C);
      camera_move_to(g.camera, sc->target->pos, ((deg + 0x10E) * 0x1000) / 0x168, 0x18E, 0x1000, 0, 0x80, 5);
      camera_reset_mode(g.camera);
      camera_setup_overview(g.camera, 0x5A, sc->caster->is_enemy ^ 1);
      spellcast_wait(sc, 0x5A);
      return;
    }
    case 4:
      sc->s24 = 0;
      if (spellcast_effects_gone(sc) != 0) ++sc->step;
      return;
    case 5:
      sound_handle_release(static_cast<u32>(sc->s3c));
      spell_status_all(sc, 1, sc->power / 16 + 200, 4);
      sc->finished = 1;
      return;
    default: return;
  }
}

}  // namespace

void spell_mod_acid_rain(SpellCast* sc) { acid_rain(sc); }

}  // namespace ss::game::battle
