// Spell code module efdb20dc3f (entries 0x959-0x95d): Curse (spell 0x3d): a dark column stands
// over the target while skulls drop round it and bounce, then the column fades and the
// target's status word (Unit +0x11c) is cleared (a target that had bits 0x48 recovers first).
// Functions 0x8008cc00 (the handler) and 0x8008d02c (a bouncing skull, a kind-0x21 callback);
// the jump table of the handler's steps at 0x8008d24c.

#include <unordered_map>

#include "game/battle/bsfx.h"
#include "game/battle/camera.h"
#include "game/battle/enemy_support.h"
#include "game/battle/killer.h"
#include "game/battle/shot.h"
#include "game/battle/spell_modules.h"
#include "game/battle/spellcast.h"
#include "game/battle/sprite.h"
#include "game/battle/unit.h"
#include "game/battle/unit_move.h"
#include "game/boot/rng.h"
#include "game/title/new_game.h"

namespace ss::game::battle {
namespace {

// The SpellCast's fields: s20 the skull timer, s24 the ticks of step 2, s28 the column's
// radius (capped at 6 times the target's width), s2c its fade (step 3); obj the data's sprite
// set (the skulls share it).

// A skull's afterimage (Shot +0x134 holds the pointer on the console).
std::unordered_map<const Shot*, Afterimage*> g_trail;

// 0x8008d02c: a skull (anim 1, layer 2) falling with gravity 0x1000 a tick and a 3-copy
// afterimage; on the ground it bounces (half the speed back, a random sideways drift on the
// first bounce, sfx 0x53) until the bounce is weak (above -0x8000): four puffs (anim 1),
// sfx 0x31, and it goes.
void skull(Shot* s) {
  BattleGlobals& g = bg();
  s->anim = 1;
  s->layer = 2;
  bobj_project(s);
  bobj_anim_draw(s, g.flags & 0xC);
  if ((g.flags & 0xC) != 0) return;
  Afterimage*& trail = g_trail[s];
  if (trail == nullptr) trail = afterimage_create(s, 3, 0x400050, 2);
  bobj_move(s, s->vel);
  s->vel[1] += g.frame_ticks * 0x1000;
  const s32 ground = bobj_ground_height(s);
  if (s->pos[1] < ground) return;
  s->pos[1] = ground;
  if (s->vel[0] == 0) s->vel[0] = rng_between_fixed(-8, 8);
  s->vel[1] = -s->vel[1] / 2;
  if (s->vel[1] < -0x8000) {
    bsfx_play_at(0x53, s);
    return;
  }
  for (int i = 0; i < 4; ++i) {
    Sprite* sp = shot_spawn_sprite(s, 1);
    if (sp == nullptr) continue;
    const s32 c[4] = {s->pos[0], s->pos[1] + -0x40000, s->pos[2], s->pos_w};
    const Vec4 p = vec_random_around(c, 0x80);
    sprite_set_pos_v(sp, p.data());
    sprite_set_fade(sp, 0x80, -2, 8);
    const s32 scale = rng_between(0x1000, 0x2000);
    sp->scale_y = scale;
    sp->scale_x = scale;
    sp->layer = 3;
  }
  bsfx_play_at(0x31, s);
  if (trail != nullptr) trail->ending = 1;
  g_trail.erase(s);
  shot_kill(s);
}

void curse(SpellCast* sc) {
  BattleGlobals& g = bg();
  Unit* t = sc->target;
  if (static_cast<u32>(sc->step - 1) < 6) {
    // The column: an 8-sided beam 0x500 long from 0x500 above the target downwards.
    s32 r = t->size[0] * 6;
    if (sc->s28 <= r) r = sc->s28;
    const s32 from[3] = {t->pos[0], t->pos[1] + -0x500000, t->pos[2]};
    const s32 dir[3] = {0, 0x10000, 0};
    const u32 a = scale_colour(0xC06050, 0x100 - sc->s2c);
    const u32 b = scale_colour(0x403010, 0x100 - sc->s2c);
    draw_beam(from, dir, a, b, 0x500, r, r, 8);
  }
  if ((g.flags & 4) != 0) return;
  if (spellcast_timer_tick(sc) != 0) return;
  switch (sc->step) {
    case 0:
      battle_flash_set(sc, 0x38, 0x40, 0x48);
      if (spellcast_leads_camera(sc)) camera_move_to(g.camera, t->pos, 0xC00, 0x155, 0x800, 0x14, 0x80, 5);
      spellcast_wait(sc, 0x1E);
      return;
    case 1:
      // (The camera move here has no lead test.)
      if (sc->data != nullptr) return;
      ++sc->step;
      camera_move_to(g.camera, t->pos, 0xC00, 0x93, 0x800, 0x78, 0x80, 5);
      return;
    case 2: {
      // For 90 ticks the column widens; a skull every 12 ticks from 0x140..0x190 above a
      // random point round the target.
      sc->s24 += g.frame_ticks;
      sc->s28 += g.frame_ticks * 4;
      if (0x5A < sc->s24) ++sc->step;
      sc->s20 += g.frame_ticks;
      if (!(0xB < sc->s20)) return;
      sc->s20 += -0xC;
      Shot* s = shot_spawn_callback_always(skull);
      if (s == nullptr) return;
      g_trail.erase(s);
      bsfx_play_at(0x2C, s);
      const Vec4 p = vec_random_in_disc_xz(t->pos, t->size[0] << 2);
      s->pos[0] = p.x, s->pos[1] = p.y, s->pos[2] = p.z, s->pos_w = p.w;
      const s32 dy = rng_between_fixed(0x140, 400);
      s->vel[1] = 0x2000;
      // Original quirk (original_bugs.md S15; kept by the user's decision, 2026-10-22): the width term is size[0] * 4 without the << 12 (a
      // negligible offset), not a height.
      s->pos[1] = (t->pos[1] - (t->size[0] << 2)) - dy;
      s->target = t;
      bobj_share_sprite(&sc->obj, s);
      return;
    }
    case 3:
      // The column fades; the status word is cleared.
      sc->s2c += g.frame_ticks * 4;
      if (!(0xFF < sc->s2c)) return;
      ++sc->step;
      if (!unit_is_down(t) && (t->status_bits & 0x48) != 0) t->status_recovered();  // [21] (0x80)
      t->status_bits = 0;
      battle_flash_set(sc, 0x80, 0x80, 0x80);
      bsfx_play_at(0xB, t);
      return;
    case 4:
      if (spellcast_effects_gone(sc) == 0) return;
      ++sc->step;
      return;
    case 5:
      sc->finished = 1;
      ++sc->step;
      return;
    default: return;
  }
}

}  // namespace

void spell_mod_curse(SpellCast* sc) { curse(sc); }

}  // namespace ss::game::battle
