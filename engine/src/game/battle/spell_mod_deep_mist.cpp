// Spell code module 60d0314477 (entries 0x92d-0x92f): Deep Mist (spell 0x22): a veil of
// mist (two rippling screen-wide grids of the data's sprites) spreads while wisps drift up
// round the target; then every unit of the other side on the stage not yet covered that
// fails its resistance gets status bit 0x200 (with a sparkle), the others a rising icon. One
// function at 0x8008cc00 (block 0, 1808 bytes; its step jump table at 0x8008d2f8).
// Resident 0x8007d9e0 draw_wave_grid (#1984.0): spell_fx.h.

#include <cstring>

#include "game/battle/items.h"
#include "game/battle/bsfx.h"
#include "game/battle/camera.h"
#include "game/battle/combat.h"
#include "game/battle/enemy_support.h"
#include "game/battle/render.h"
#include "game/battle/spell_modules.h"
#include "game/battle/spellcast.h"
#include "game/battle/sprite.h"
#include "game/battle/stage.h"
#include "game/battle/unit.h"
#include "game/boot/rng.h"
#include "game/common/screen.h"
#include "runtime/libgpu.h"

namespace ss::game::battle {
namespace {

// The SpellCast's fields this module uses: s20 the ticks of step 2, s24 the mist's wave
// phase (40 a tick), s28 its fade (step 3), s2c the wisp timer, s3c the hum's handle; obj
// the data's sprite set (anims 4 / 3 the mist, 1-2 wisps, 3 sparkles; grey +0x198 the
// mist's brightness).


// Steps 2-3: the mist, anims 4 and 3 side by side across the screen (each as wide as its
// cel, 16 apart, from x 0xfe - w - 16), as 10 x 10 wave grids rippling in x (amplitude
// 0x4000, phase s24, 0x2000 down the grid); the phase runs 40 a tick outside the pause.
void draw_mist(SpellCast* sc) {
  BObj* o = &sc->obj;
  const s32 wave_x[3] = {0x4000, sc->s24, 0x2000};
  const s32 wave_y[3] = {0, 0, 0};  // (0x80023f10: memset 12)
  for (int i = 0; i < 2; ++i) {
    o->anim = static_cast<u16>(4 - i);
    bobj_anim_draw(o, 0x9800);
    const FrameUv& f = frame_uv();
    const s32 uv[4] = {f.u0, f.v0, f.u1, f.v1};
    const u16 tpage = f.tpage, clut = f.clut;
    const u32 code = bobj_poly_tag(o, tpage);
    const s32 w = uv[2] - uv[0];
    const s32 d = (i - 1) * (w + 0x10);
    const s32 rect[4] = {d + 0xFE, 0, d + 0x10E + w, uv[3] - uv[1]};
    draw_wave_grid(uv, rect, wave_x, wave_y, clut, tpage, 8, code, 10);
  }
  if ((bg().flags & 4) == 0) sc->s24 += bg().frame_ticks * 0x28;
}

// 0x8008cc00: the handler (power: the roll's chance, power / 16 + 180).
// Original quirk (original_bugs.md S19, to decide): it never calls spellcast_timer_tick, so
// the wait of step 0 holds nothing (step 1 runs on the next frame).
// 0: dim, flash 0x40, the camera over the caster's side, wait 30. 1 (data loaded): the mist
// from 0, a hum. 2: it thickens (4 a tick to 0x80); a wisp round the target every 6 -
// power / 30 ticks (at least 2); after 120 ticks the hum fades. 3: the mist fades. 4:
// flash back, undim, the status on the other side; sfx 10 if anyone got it, else 11. 5:
// the effects end, done.
void deep_mist(SpellCast* sc) {
  BattleGlobals& g = bg();
  Unit* c = sc->caster;
  if (static_cast<u32>(sc->step - 2) < 2) draw_mist(sc);
  // ORIGINAL BUG (original_bugs.md S11): no pause test: the steps run on during the pause
  // (g_battle_flags 4), rolls and sprites included.
  if ((g.flags & 4) != 0) return;
  switch (sc->step) {
    case 0:
      spellcast_dim_begin(sc, c->is_enemy);
      battle_flash_set(sc, 0x40, 0x40, 0x40);
      if (spellcast_leads_camera(sc)) camera_setup_overview(g.camera, 0x1E, c->is_enemy ^ 1);
      spellcast_wait(sc, 0x1E);
      return;
    case 1:
      if (sc->data != nullptr) return;
      sc->obj.colour_mode = 2;
      sc->obj.rgb[0] = 0;
      ++sc->step;
      sc->s3c = static_cast<s32>(bsfx_play_ui(0x44));
      return;
    case 2: {
      const u32 lit = static_cast<u32>(sc->obj.rgb[0]) + static_cast<u32>(g.frame_ticks * 4);
      sc->obj.rgb[0] = static_cast<u8>(lit);
      if (0x80 < (lit & 0xFF)) sc->obj.rgb[0] = 0x80;
      s32 every = 6 - sc->power / 0x1E;
      if (every < 2) every = 2;
      sc->s2c += g.frame_ticks;
      if (every <= sc->s2c) {
        sc->s2c -= every;
        if (Sprite* sp = spellcast_sprite_spawn(sc, static_cast<int>(rng_range(2)) + 1)) {
          const Unit* t = sc->target;
          const s32 dx = rng_between_fixed(-0x400, 0x400);
          const s32 dy = rng_between_fixed(-800, -0x40);
          const s32 dz = rng_between_fixed(-0x400, 0x400);
          sprite_set_pos(sp, t->pos[0] + dx, t->pos[1] + dy, t->pos[2] + dz);
          sprite_set_fade(sp, 0x80, -2, 8);
          sp->layer = 1;
          sp->vel[0] = rng_between_fixed(-8, 8);
          sp->scale_y = 0x2000;
          sp->scale_x = 0x2000;
        }
      }
      sc->s20 += g.frame_ticks;
      if (sc->s20 < 0x79) return;
      sound_handle_fade_out(static_cast<u32>(sc->s3c), 0x3C, 0);
      ++sc->step;
      return;
    }
    case 3:
      sc->s28 += g.frame_ticks * 4;
      sc->obj.rgb[0] = static_cast<u8>(-0x80 - sc->s28);
      if (sc->s28 < 0x80) return;
      ++sc->step;
      return;
    case 4: {
      battle_flash_set(sc, 0x80, 0x80, 0x80);
      spellcast_dim_end(sc);
      bool given = false;
      ++sc->step;
      Unit* list[8] = {};
      const int n = units_in_rect(sc, stage().bounds, list, c->is_enemy);
      for (int i = 0; i < n; ++i) {
        Unit* u = list[i];
        if ((u->status_bits & 0x200) != 0 || u->resist_roll(1, sc->power / 16 + 0xB4) == 0) {  // [13]
          effect_rise_icon(u, 0);  // 0x800308a0
          continue;
        }
        item_fx_sparkle(u);
        u->status_bits |= 0x200;
        s32 k = sc->power / 0x23 + 2;
        if (8 < k) k = 8;
        for (int j = 0; j < k; ++j) {
          Sprite* sp = spellcast_sprite_spawn(sc, 3);
          if (sp == nullptr) continue;
          const s32 dx = rng_between_fixed(u->size[0] * -3, u->size[0] * 3);
          const s32 dy = rng_between_fixed(u->size[1] * -5, 0);
          const s32 dz = rng_between_fixed(u->size[2] * -2, 0);
          sprite_set_pos(sp, u->pos[0] + dx, u->pos[1] + dy, u->pos[2] + dz);
          sp->scale_y = 0x2000;
          sp->scale_x = 0x2000;
          sprite_set_fade(sp, 0x80, -4, 8);
          sp->layer = 2;
        }
        given = true;
      }
      bsfx_play_ui(given ? 10 : 0xB);
      return;
    }
    case 5:
      if (spellcast_effects_gone(sc) == 0) return;
      sc->finished = 1;
      ++sc->step;
      return;
    default: return;
  }
}

}  // namespace

void spell_mod_deep_mist(SpellCast* sc) { deep_mist(sc); }

}  // namespace ss::game::battle
