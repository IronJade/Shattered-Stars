// Spell code module 32406ffe6a (entries 0x946 / 0x947): Energy Net (spell 0x29): glowing
// motes drift in high above, then a net of light (two funnels round a falling point) drops
// onto the field and bursts; every unit of the other side that fails its resistance is
// stunned, knocked about and showered with sparks. One function at 0x8008cc00 (block 0,
// 2600 bytes; its step jump table at 0x8008d610).

#include "game/battle/bsfx.h"
#include "game/battle/camera.h"
#include "game/battle/combat.h"
#include "game/battle/fx_draw.h"
#include "game/battle/render.h"
#include "game/battle/shot.h"
#include "game/battle/spell_fx.h"
#include "game/battle/spell_modules.h"
#include "game/battle/spellcast.h"
#include "game/battle/sprite.h"
#include "game/battle/stage.h"
#include "game/battle/unit.h"
#include "game/battle/unit_move.h"
#include "game/boot/rng.h"
#include "game/title/new_game.h"

namespace ss::game::battle {
namespace {

// The SpellCast's fields this module uses: s20 the net is drawn, s24 counts down (unused),
// s28 the net's ticks (its colour phase), s2c its fade (step 3, 0..0x100), s30 its fall
// speed; obj the data's sprite set (anims 3-7 the motes, 1 the sparks, 8 the burst's
// rings) and the net's position (obj.pos).

const s32 kUnitScale[3] = {0x1000, 0x1000, 0x1000};  // 0x8006e34c

// While s20 is set: two funnels (draw_vortex, 10 rings x 8 segments, additive) pointing
// down at the net's position: blue 0x4060a0 (width 0x76c, 0x60 a ring) and gold 0xffc080
// (width 0x578, 0xdc a ring), fading with s2c, their colours running with s28.
void draw_net(SpellCast* sc) {
  BattleGlobals& g = bg();
  const s32 level = sc->s2c * -2 + 0x100;
  VortexParams p;
  p.base = -0x300;
  p.width = 0x76C;
  p.radius = 0x4000;
  p.rings = 10;
  p.step = 0x60;
  p.segments = 8;
  p.colour_a = scale_colour(0x4060A0, level);
  p.colour_b = scale_colour(0, level);
  p.phase_span = 4;
  p.twist_freq = 1;
  p.twist_amp = 0x1000;
  p.blend = 2;
  p.ot_bias = 5;
  p.phase = (static_cast<u32>(sc->s28) & 0x1F) * -0x100 + 0x1FFF;
  const s32 down[3] = {0, -0x1000, 0};
  s32 orient[4] = {};
  vec_to_angles(orient, down);
  obj_set_matrix(sc->obj.pos, orient, kUnitScale);
  draw_vortex(p);
  p.base = -0x2A0;
  p.width = 0x578;
  p.radius = 0x4000;
  p.rings = 10;
  p.step = 0xDC;
  p.segments = 8;
  p.colour_a = scale_colour(0xFFC080, level);
  p.colour_b = scale_colour(0, level);
  p.phase_span = 4;
  p.twist_freq = 1;
  p.twist_amp = 0x1000;
  p.blend = 2;
  p.ot_bias = 5;
  p.phase = 0x1FFF - ((sc->s28 * 0x180) & 0x1FFF);
  draw_vortex(p);
  if ((g.flags & 0xC) == 0) {
    sc->s24 -= g.frame_ticks;
    sc->s28 += g.frame_ticks;
  }
}

// 0x8008cc00: the handler (power: the resistance roll's chance, power / 16 + 160). 0: flash
// 0x40, dim, the camera. 1 (data loaded): 20 motes drifting high above, the net at -0x1770,
// sounds. 2: it falls (accelerating 0x1000 a tick; the camera follows it between -0xbb8 and
// -0x3e8); from -0x190 a shake, the camera on the target, sounds, two spheres, three spark
// rings. 3: the net fades. 4: every unit of the other side on the stage rolls kind 1:
// resisted, a rising icon; else knocked (+0x127 1, a random push), stunned, seven bouncing
// sparks, pushed again and a sound; flash back, undim, wait 10. 5: the effects end, done.
void energy_net(SpellCast* sc) {
  BattleGlobals& g = bg();
  if (sc->s20 != 0) draw_net(sc);
  if ((g.flags & 4) != 0) return;
  if (spellcast_timer_tick(sc) != 0) return;
  const s32 t = g.frame_ticks;
  BObj* o = &sc->obj;
  switch (sc->step) {
    case 0:
      battle_flash_set(sc, 0x40, 0x40, 0x40);
      spellcast_dim_begin(sc, sc->caster->is_enemy);
      if (spellcast_leads_camera(sc)) {
        const s32 at[3] = {0, static_cast<s32>(0xFF448000), static_cast<s32>(0xFF830000)};
        camera_move_to(g.camera, at, 0xC00, -0x22, 0x1000, 0x1E, 0x80, 5);
      }
      ++sc->step;
      return;
    case 1:
      if (sc->data != nullptr) return;
      for (int i = 0; i < 0x14; ++i) {
        Sprite* sp = spellcast_sprite_spawn(sc, static_cast<int>(rng_range(5)) + 3);
        if (sp == nullptr) continue;
        const s32 x = rng_between_fixed(-4000, 5000);
        const s32 y = rng_between_fixed(-8000, -0x9C4);
        const s32 z = rng_between_fixed(4000, 6000);
        sprite_set_pos(sp, x, y, z);
        sp->vel[0] = rng_between_fixed(-8, -1);
        sp->scale_y = 24000;
        sp->scale_x = 24000;
        sprite_set_fade(sp, 0x80, -4, 0xB4);
      }
      o->pos[0] = 0;
      o->pos[1] = static_cast<s32>(0xFE890000);
      o->pos[2] = 0;
      sc->s30 = 0x1C000;
      ++sc->step;
      bsfx_play_at(0x39, o);
      {
        const u32 h = bsfx_play_at(0x2E, o);
        sound_handle_fade_out(h, 0xF0, 0);
        sound_handle_pitch_down(h, 0xF0);
      }
      return;
    case 2:
      sc->s20 = 1;
      o->pos[1] += sc->s30 * t;
      sc->s30 += t * 0x1000;
      if (static_cast<u32>(o->pos[1] + 0xBB7FFF) < 0x7CFFFF) {
        const s32 at[3] = {o->pos[0], o->pos[1] + 0x3E8000, o->pos[2] - 0x1F4000};
        camera_move_to(g.camera, at, 0xC00, 0x18E, 0x1000, 5, 0x80, 5);
      }
      if (o->pos[1] < -0x190000) return;
      screen_shake(6, 3, 2, 2, 0x10);
      camera_reset(g.camera);
      camera_move_to(g.camera, sc->target->pos, 0xC00, 0xE3, 0xC00, 0x78, 0x80, 5);
      ++sc->step;
      bsfx_play_at_pos(0x50, kZeroVector);
      bsfx_play_at_pos(0x21, kZeroVector);
      bsfx_play_at_pos(0x22, kZeroVector);
      effect_spawn_sphere_at_default(kZeroVector, 8, 8, 0xC, 100, 0xC07020, 0x140, 0);
      effect_spawn_sphere_at_default(kZeroVector, 10, 10, 0x14, 100, 0x204080, 400, 0);
      for (int i = 0; i < 3; ++i) {
        Shot* r;
        if (i == 0) {
          r = effect_spawn_sparks_at(kZeroVector, 0x10, 0x1E0, 0xF0, 0x28, 100, 1, 2, 0x1E0, 0);
        } else if (i == 1) {
          r = effect_spawn_sparks_at(kZeroVector, 0x10, 0x140, 400, 0x1C, 100, 1, 2, 400, 0);
        } else {
          r = effect_spawn_sparks_at(kZeroVector, 0x10, 0x80, 0x3C0, 0x14, 100, 1, 2, 0x140, 0);
        }
        if (r == nullptr) continue;
        r->vel[0] = 0x16;
        r->anim = 8;
        bobj_share_sprite(o, r);
      }
      return;
    case 3:
      sc->s2c += t * 4;
      if (sc->s2c < 0x101) return;
      sc->s2c = 0x100;
      ++sc->step;
      return;
    case 4: {
      Unit* list[8] = {};
      const int n = units_in_rect(sc, stage().bounds, list, sc->caster->is_enemy);
      for (int i = 0; i < n; ++i) {
        Unit* u = list[i];
        if (u->resist_roll(1, sc->power / 16 + 0xA0) == 0) {  // [13]
          effect_rise_icon(u, 0);  // 0x800308a0
          continue;
        }
        u->reaction = 1;
        u->knockback[0] = rng_spread_fixed(4);
        u->knockback[1] = 0;
        u->knockback[2] = 0;
        unit_apply_stun(u);
        s32 turn = 0, vx = -0x12000;
        for (int k = 0; k < 7; ++k) {
          if (Sprite* sp = spellcast_sprite_spawn(sc, 1)) {
            const s32 at[3] = {u->pos[0], u->pos[1] - (u->size[1] << 14), u->pos[2]};
            sprite_set_pos_v(sp, at);
            sp->vel[0] = vx;
            sp->vel[1] = bsin(static_cast<u32>(turn / 360)) * -8;
            sp->accel[1] = 0x400;
            sp->scale_y = 12000;
            sp->scale_x = 12000;
            sp->roll_speed = -0x71;
            sprite_set_fade(sp, 0x80, -2, 0x10);
            sp->layer = 2;
            sp->mode |= 8;
          }
          turn += 0x1E000;
          vx += 0x6000;
        }
        u->reaction = 1;
        u->knockback[0] = rng_between_fixed(-4, 4);
        u->knockback[1] = 0;
        u->knockback[2] = 0;
        bsfx_play_at(0x1C, u);
      }
      battle_flash_set(sc, 0x80, 0x80, 0x80);
      spellcast_dim_end(sc);
      spellcast_wait(sc, 10);
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

void spell_mod_energy_net(SpellCast* sc) { energy_net(sc); }

}  // namespace ss::game::battle
