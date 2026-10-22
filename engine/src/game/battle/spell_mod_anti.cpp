// Spell code module b02674cbb8 (entries 0x964-0x966): Anti (spell 0x2a): a turning ring of
// light under the caster, a smaller one under each standing unit of the caster's side, and
// motes drawn into them for 120 ticks; then each of them not yet protected gets status bit
// 0x8000 (with a sparkle), the others a rising icon. Functions 0x8008cc00 (the handler;
// its step jump table at 0x8008dad0) and 0x8008d850 (a unit's ring, a kind-0x21 callback).

#include "game/battle/bsfx.h"
#include "game/battle/camera.h"
#include "game/battle/combat.h"
#include "game/battle/enemy_support.h"
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

// The SpellCast's fields this module uses: s20 the caster's ring's brightness (0..0x80; the
// ring is drawn while it is above 0), s24 the ticks of step 3, s2c the ring's turn, s34 the
// camera's yaw; obj the data's sprite set (anim 7 the ring, 6 the units' rings, 1-5 motes).

// 0x80084d48 item_fx_sparkle (#1984.1): a rising sparkle (set A anim 0x3a, 16 loops) 3
// units of height above u. (items.cpp has it in its anonymous namespace; a copy.)
void item_fx_sparkle(const Unit* u) {
  Sprite* sp = sprite_spawn_a(0x3A, 0x10, nullptr);
  if (sp == nullptr) return;
  sprite_set_pos(sp, u->pos[0], u->pos[1] + u->size[1] * -0x3000, u->pos[2]);
  sp->vel[1] = static_cast<s32>(0xFFFFB000);
  sp->accel[1] = 0x200;
  sp->layer = 2;
  sp->freeze_class = 2;
  sp->keep_screen = 1;
}

// A flat square of 'o''s cel ('anim') turned by 'turn', its corners 'r' units (20.12 per
// unit of the sine) from u on the ground (lifted by the view pitch's sine), as one n x n
// patch at OT bias 'ot'.
void draw_ground_ring(BObj* o, int anim, const Unit* u, s32 turn, s32 r, s32 ot, s32 n) {
  o->anim = static_cast<u16>(anim);
  bobj_anim_draw(o, 0x9800);
  const FrameUv& f = frame_uv();
  const u16 tpage = f.tpage, clut = f.clut;
  const s32 uv[4] = {f.u0, f.v0, f.u1, f.v1};
  const u32 code = bobj_poly_tag(o, tpage);
  s32 c[4][4] = {};
  s32 lift = bsin(static_cast<u32>(static_cast<s32>(view().pitch))) << 4;
  if (lift < 0) lift = 0;
  const s32 y = bobj_ground_height(u) - (lift + 0x10000);
  for (int i = 0; i < 4; ++i) {
    const s32 x = u->pos[0] + bcos(static_cast<u32>(turn + i * 0x400)) * r;
    const s32 z = u->pos[2] + bsin(static_cast<u32>(turn + i * 0x400)) * r;
    const int k = i < 2 ? i : 5 - i;  // (corners 0, 1, 3, 2: round the square)
    c[k][0] = x;
    c[k][1] = y;
    c[k][2] = z;
  }
  draw_textured_patch(uv, c, clut, tpage, ot, code, n);
}

// 0x8008d850: a ring (anim 6, 800 units) under its unit (+0x114) turning 20 a tick
// (+0x128); after 150 ticks (+0x12c) it fades, after 180 it goes.
void unit_ring(Shot* s) {
  BattleGlobals& g = bg();
  draw_ground_ring(s, 6, s->target, s->p128, 800, 0xF, 3);
  if ((g.flags & 0xC) != 0) return;
  s->p128 = (s->p128 + g.frame_ticks * 0x14) & 0xFFF;
  s->p12c += g.frame_ticks;
  if (0x96 < s->p12c) {
    s->colour_mode = 2;
    s->rgb[0] = static_cast<u8>(-0x80 - (s->p12c - 0x96) * 4);
  }
  if (0xB4 < s->p12c) shot_kill(s);
}

// 0x8008cc00: the handler (no power: it only sets a status). Steps 2-4 draw the caster's
// ring (anim 7, 0x600 units, 8 x 8, turning -2 a tick). 0: a yaw, dim the own side, flash
// 0x30, the camera on the caster, wait 20. 1 (data loaded): the ring's colour from 0,
// sounds at the target. 2: it brightens 2 a tick; at 0x80 sounds, a ring and a sprite on
// every standing unit of the caster's side, the camera. 3: for 120 ticks motes round them
// (rising and drawn in); then sounds and the camera on another random standing unit of the
// side. 4: sparks, the ring fades; at 0 the status (bit 0x8000 of +0x11c) on the units
// without it, sfx 10 if any, flash back, undim. 5: the effects end. 6: done.
void anti(SpellCast* sc) {
  BattleGlobals& g = bg();
  Unit* c = sc->caster;
  const int n = c->is_enemy != 0 ? 8 : 4;
  auto side_unit = [&](int i) -> Unit* {
    return c->is_enemy == 0 ? static_cast<Unit*>(g.party_units[static_cast<size_t>(i)])
                            : static_cast<Unit*>(g.enemy_units[static_cast<size_t>(i)]);
  };
  if (static_cast<u32>(sc->step - 2) < 3 && 0 < sc->s20) {
    draw_ground_ring(&sc->obj, 7, c, sc->s2c, 0x600, 7, 8);
    if ((g.flags & 0xC) == 0) sc->s2c = (sc->s2c + g.frame_ticks * -2) & 0xFFF;
  }
  if ((g.flags & 4) != 0) return;
  if (spellcast_timer_tick(sc) != 0) return;
  switch (sc->step) {
    case 0:
      sc->s34 = rng_range(2) != 0 ? 0xAAA : 0xD55;
      spellcast_dim_begin(sc, c->is_enemy ^ 1);
      battle_flash_set(sc, 0x30, 0x30, 0x30);
      if (spellcast_leads_camera(sc)) camera_move_to(g.camera, c->pos, sc->s34, 0x155, 0xC00, 0x12, 0x80, 5);
      spellcast_wait(sc, 0x14);
      return;
    case 1:
      if (sc->data != nullptr) return;
      sc->obj.colour_mode = 2;
      sc->obj.rgb[0] = 0;
      ++sc->step;
      bsfx_play_at(0x32, sc->target);
      bsfx_play_at(0x32, sc->target);
      bsfx_play_at(0x24, sc->target);
      return;
    case 2:
      sc->s20 += g.frame_ticks * 2;
      if (0x7F < sc->s20) {
        bsfx_play_at(0x34, sc->target);
        sound_handle_fade_out(bsfx_play_at(0x2D, c), 0x5A, 0);
        sound_handle_fade_out(bsfx_play_at(0x44, c), 0x5A, 0xB4);
        sc->s20 = 0x80;
        ++sc->step;
        for (int i = 0; i < n; ++i) {
          Unit* u = side_unit(i);
          if (unit_is_down(u)) continue;
          if (Shot* s = shot_spawn_callback_always(unit_ring)) {
            s->target = u;
            s->p128 = 0x200;
            bobj_share_sprite(&sc->obj, s);
          }
          if (Sprite* sp = spellcast_sprite_spawn(sc, 1)) {
            sprite_set_pos_v(sp, u->pos);
            sprite_set_fade(sp, 0x80, -2, 0x78);
            sp->layer = 8;
          }
        }
        if (spellcast_leads_camera(sc)) camera_move_to(g.camera, c->pos, sc->s34, 0xAA, 0xA00, 0x5A, 0x80, 5);
      }
      sc->obj.rgb[0] = static_cast<u8>(sc->s20);
      return;
    case 3:
      sc->s24 += g.frame_ticks;
      if (0x78 < sc->s24) {
        ++sc->step;
        bsfx_play_at(0x21, c);
        bsfx_play_at(0x2A, c);
        // The camera turns to another standing unit of the side, if there are two.
        const bool party = (c->is_enemy ^ 1) != 0;
        int standing = 0;
        for (int i = 0; i < (party ? 4 : 8); ++i) {
          Unit* u = party ? static_cast<Unit*>(g.party_units[static_cast<size_t>(i)])
                          : static_cast<Unit*>(g.enemy_units[static_cast<size_t>(i)]);
          if (!unit_is_down(u)) ++standing;
        }
        if (1 < standing) {
          Unit* u = nullptr;
          for (int tries = 0;;) {
            u = party ? static_cast<Unit*>(g.party_units[rng_range(4)])
                      : static_cast<Unit*>(g.enemy_units[rng_range(8)]);
            if (u != c && !unit_is_down(u)) break;
            if (0x32 <= ++tries) break;  // (50 picks at most)
          }
          if (!unit_is_down(u) && spellcast_leads_camera(sc)) {
            const s32 yaw = rng_range(2) != 0 ? 0xCE3 : 0xB1C;
            camera_move_to(g.camera, u->pos, yaw, 0xE3, 0x800, 0xA0, 0x80, 5);
          }
        }
      }
      for (int i = 0; i < n; ++i) {
        Unit* u = side_unit(i);
        if (unit_is_down(u)) continue;
        if (0x78 < sc->s24) bsfx_play_at(0x22, u);
        if (Sprite* sp = spellcast_sprite_spawn(sc, 2)) {
          const s32 dx = rng_between_fixed(u->size[0] * -6, u->size[0] * 6);
          const s32 dz = rng_between_fixed(u->size[2] * -4, u->size[2] << 2);
          sprite_set_pos(sp, u->pos[0] + dx, u->pos[1], u->pos[2] + dz);
          sprite_set_fade(sp, 0x80, -2, 8);
          sp->layer = 6;
          const s32 scale = rng_between(0x1000, 0x2000);
          sp->scale_y = scale;
          sp->scale_x = scale;
          sp->accel[1] = -0xE0;
        }
        if (Sprite* sp = spellcast_sprite_spawn(sc, 3)) {
          const u32 yaw = rng_range(0x1000);
          const u32 pitch = static_cast<u32>(rng_between(0, 0x400));
          const s32 x = ((bcos(yaw) * bcos(pitch)) >> 12) * 800 + u->pos[0];
          const s32 y = u->pos[1] + u->size[1] * -0x2000 - bsin(yaw) * 800;
          const s32 z = ((bcos(yaw) * bsin(pitch)) >> 12) * 800 + u->pos[2];
          sprite_set_pos(sp, x, y, z);
          const s32 mid[3] = {u->pos[0], u->pos[1] + u->size[1] * -0x2000, u->pos[2]};
          const Vec4 v = vec_velocity_toward(sp->pos, mid, 0xE);
          sp->vel[0] = v.x, sp->vel[1] = v.y, sp->vel[2] = v.z, sp->extra34 = v.w;
          sprite_set_fade(sp, 0x80, -6, 10);
          sp->layer = 6;
        }
      }
      return;
    case 4:
      for (int i = 0; i < n; ++i) {
        Unit* u = side_unit(i);
        if (unit_is_down(u)) continue;
        Sprite* sp = spellcast_sprite_spawn(sc, static_cast<int>(rng_range(3)) + 4);
        if (sp == nullptr) continue;
        const s32 dx = rng_between_fixed(u->size[0] * -8, u->size[0] << 3);
        const s32 dy = rng_between_fixed(u->size[1] * -6, 0);
        const s32 dz = rng_between_fixed(u->size[2] * -4, u->size[2] << 2);
        sprite_set_pos(sp, u->pos[0] + dx, u->pos[1] + dy, u->pos[2] + dz);
        sprite_set_fade(sp, 0x80, -2, 8);
        sp->layer = 8;
        const s32 scale = rng_between(0x1800, 10000);
        sp->scale_y = scale;
        sp->scale_x = scale;
      }
      sc->obj.rgb[0] = static_cast<u8>(sc->s20);
      sc->s20 -= g.frame_ticks;
      if (0 < sc->s20) return;
      sc->s20 = 0;
      ++sc->step;
      {
        int given = 0;
        for (int i = 0; i < n; ++i) {
          Unit* u = side_unit(i);
          if (unit_is_down(u)) continue;
          if ((u->status_bits & 0x8000) == 0) {
            item_fx_sparkle(u);
            ++given;
            u->status_bits |= 0x8000;
          } else {
            effect_rise_icon(u, 0);  // 0x800308a0
          }
        }
        if (given != 0) bsfx_play_ui(10);
      }
      battle_flash_set(sc, 0x80, 0x80, 0x80);
      spellcast_dim_end(sc);
      return;
    case 5:
      if (spellcast_effects_gone(sc) == 0) return;
      ++sc->step;
      return;
    case 6:
      sc->finished = 1;
      ++sc->step;
      return;
    default: return;
  }
}

}  // namespace

void spell_mod_anti(SpellCast* sc) { anti(sc); }

}  // namespace ss::game::battle
