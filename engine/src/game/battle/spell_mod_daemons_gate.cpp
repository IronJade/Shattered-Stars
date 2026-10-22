// Spell code module a6a642e880 (entries 0x971-0x973): Daemon's Gate (spell 0x24) and The
// Scream (spell 0x41), one module for both. A gate (the data's three models: the frame and
// two doors) grows at a point behind the target; its doors swing open to the data's clip
// (its command-8 block) amid rising sprites, and a daemon (the cast's sprite) comes out,
// grows, and (anim 1, with an afterimage) strikes every standing unit of the target's side
// at once (element 0x200, power INT * 2 + power * 2 + 1850); it goes back in, the doors
// close and the gate shrinks away. Functions 0x8008cc00 (the handler) and 0x8008d27c (the
// daemon, a kind-0x21 callback); jump tables at 0x8008d968 (the handler's steps) and
// 0x8008d990 (the daemon's states); the daemon's "gone" byte at 0x8008d9a4.

#include <array>
#include <unordered_map>

#include "game/battle/bsfx.h"
#include "game/battle/camera.h"
#include "game/battle/killer.h"
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

// The SpellCast's fields this module uses: s20 the gate's scale (0 to 10000, steps 2 and
// 6), s24 the doors' swing (0 to 0x400, steps 3 and 5); obj the data's sprite set, its
// position the gate's point (set in step 0).

// The module's byte 0x8008d9a4: the daemon is gone (step 4 waits for it). One per slot, as
// each slot runs its own copy of the block; the daemon keeps its cast's slot in user[0].
std::array<u8, 2> g_daemon_gone{};
s32& daemon_slot(Shot* s) { return s->user[0]; }

// The daemon's afterimage (Shot +0x130 holds the pointer on the console). Its back link
// points at the map's entry, so a trail deleted by itself (the battle's end) leaves null.
std::unordered_map<const Shot*, Afterimage*> g_trail;

Afterimage* trail_of(const Shot* s) {
  const auto it = g_trail.find(s);
  return it != g_trail.end() ? it->second : nullptr;
}

// +0x130 = 0, after +0xc (the trail's ending flag) = 1 when 'end'.
void drop_trail(const Shot* s, bool end) {
  const auto it = g_trail.find(s);
  if (it == g_trail.end()) return;
  if (it->second != nullptr) {
    if (end) it->second->ending = 1;
    it->second->back = nullptr;
  }
  g_trail.erase(it);
}

// The data's clip (the command-8 block): bsfx_play 0x8007f3f0 in clip mode, no emitter.
void play_clip(SpellCast* sc) {
  if (!sc->raw.empty()) bsfx_play(0, sc->raw.data(), nullptr, 1);
}

// 0x8008d27c: the daemon (Shot +0x128 the state, +0x12c its ticks, +0x134 the power, +0x110
// / +0x114 the caster / target; anim 1 in state 2, else 0). 0: grows to full size moving
// 0x4000 a tick toward -z for 80 ticks; 1: after 30 ticks sfx 0x34 / 0x21, the camera to
// one side and a 5-copy afterimage (0x400060); 2: after 30 ticks (its copies growing) the
// hit on every standing unit of the target's side (sfx 0x4b, a sprite (anim 3) and two
// debris bursts on each); 3: after 30 ticks the camera on it and the afterimage ends; 4:
// shrinks moving back toward +z and after 120 ticks goes (the "gone" byte).
void daemon(Shot* s) {
  BattleGlobals& g = bg();
  u8& gone = g_daemon_gone[static_cast<size_t>(daemon_slot(s) & 1)];
  s->anim = static_cast<u16>(s->p128 == 2 ? 1 : 0);
  bobj_project(s);
  bobj_anim_draw(s, 0);
  if ((g.flags & 0xC) != 0) return;
  switch (s->p128) {
    case 0:
      if (s->scale_x < 0x1000) {
        s->scale_x += g.frame_ticks * 0x100;
        s->scale_y += g.frame_ticks * 0x100;
      } else {
        s->scale_y = 0x1000;
        s->scale_x = 0x1000;
      }
      s->vel[2] = -0x4000;
      s->p12c += g.frame_ticks;
      bobj_move(s, s->vel);
      if (0x50 < s->p12c) {
        s->p128 = 1;
        s->p12c = 0;
      }
      break;
    case 1: {
      s->p12c += g.frame_ticks;
      if (s->p12c < 0x1F) break;
      s->p128 = 2;
      s->p12c = 0;
      bsfx_play_ui(0x34);
      bsfx_play_ui(0x21);
      const u32 side = rng_range(2);
      const s32 x = s->pos[0] + (side != 0 ? -0x12C000 : 0x12C000);
      const s32 at[3] = {x, bobj_ground_height(s), 0};
      camera_move_to(g.camera, at, side != 0 ? 0xB8E : 0xC71, 0x155, 0x1400, 0, 0x80, 5);
      bobj_anim_reset(s);
      if (trail_of(s) == nullptr) {
        // (0x8001fd94 allocates the trail's 0x30 bytes.)
        Afterimage*& slot = g_trail[s];
        slot = afterimage_create(s, 5, 0x400060, 2);
        slot->back = &slot;
      }
      break;
    }
    case 2: {
      if (Afterimage* a = trail_of(s)) a->next_kind = 1;
      s->p12c += g.frame_ticks;
      if (s->p12c < 0x1F) break;
      s->p128 = 3;
      s->p12c = 0;
      const int count = s->target->is_enemy != 0 ? 8 : 4;
      Unit* list[8] = {};
      int n = 0;
      for (int i = 0; i < count; ++i) {
        Unit* u = s->target->is_enemy != 0 ? static_cast<Unit*>(g.enemy_units[static_cast<size_t>(i)])
                                           : static_cast<Unit*>(g.party_units[static_cast<size_t>(i)]);
        if (unit_is_down(u)) continue;
        list[n++] = u;
        bsfx_play_at(0x4B, u);
        if (Sprite* sp = shot_spawn_sprite(s, 3)) {
          const s32 at[3] = {u->pos[0], u->pos[1] + u->size[1] * -0x2000, u->pos[2]};
          sprite_set_pos_v(sp, at);
          sprite_set_fade(sp, 0x80, -2, 0x10);
          sp->layer = 8;
        }
        for (int k = 0; k < 2; ++k) {
          const s32 at[3] = {u->pos[0], u->pos[1] + u->size[1] * -0x2000, u->pos[2]};
          const s32 spread[3] = {u->size[0] * 4, u->size[1] * 4, u->size[2]};
          effect_debris_burst(at, spread, 3, k + 1, 2, 0x8000, static_cast<u8>(k == 0 ? 0xD : 5));
        }
      }
      if (n != 0) {
        // The temporary Shot (0x13c bytes from the heap on the console, freed after): the
        // caster's, x far to the side the blow comes from.
        Shot tmp;
        tmp.owner = s->owner;
        tmp.pos[0] = s->target->is_enemy != 0 ? static_cast<s32>(0xFD8F0000) : 0x2710000;
        tmp.pos[1] = 0;
        tmp.pos[2] = 0;
        shot_apply_damage(&tmp, n, list, nullptr, s->p134, 0, 0x200, 0, 0x10);
      }
      break;
    }
    case 3:
      if (Afterimage* a = trail_of(s)) a->next_kind = 1;
      s->p12c += g.frame_ticks;
      if (s->p12c < 0x1F) break;
      s->p128 = 4;
      s->p12c = 0;
      camera_move_to(g.camera, s->pos, 0xC00, 0xAA, 0xA00, 0x1E, 0x80, 5);
      drop_trail(s, true);
      break;
    case 4:
      s->scale_x -= g.frame_ticks * 0x40;
      s->scale_y -= g.frame_ticks * 0x40;
      if (s->scale_x < 0) {
        s->scale_x = 0;
        s->scale_y = 0;
      }
      s->vel[2] = 0x8000;
      s->p12c += g.frame_ticks;
      bobj_move(s, s->vel);
      if (0x78 < s->p12c) {
        shot_kill(s);
        gone = 1;
      }
      break;
    default: break;
  }
  if (4 < static_cast<u32>(s->p128)) {
    // (Never reached: the states end at 4.)
    drop_trail(s, true);
    shot_kill(s);
    gone = 1;
  }
}

// Steps 2-6: the gate, lit, at full stage colour, scaled s20 / 0x1000: model 0 (the frame)
// 0x91 units toward -z of the gate's point (OT bias 4), models 1 and 2 (the doors) 0xeb
// units (times the scale) to the left / right, turned by s24 / -s24 (bias 8); CLUT and
// tpage of the set's part 1.
void draw_gate(SpellCast* sc) {
  const BObj& o = sc->obj;
  const AnimSet* set = o.animset;
  const u16 clut = set->clut[1], tpage = set->tpage[1];
  auto draw = [&](int k, s32 bias) {
    if (const u8* model = spellcast_model(sc, k)) model_draw_culled(model, clut, tpage, bias);
  };
  stage_colour_set_grey(0x100);
  s32 orient[3] = {0, 0, 0};
  const s32 scale[3] = {sc->s20, sc->s20, sc->s20};
  s32 pos[3] = {o.pos[0], o.pos[1], o.pos[2] - 0x91000};
  lights_build_oriented(orient);
  obj_set_matrix(pos, orient, scale);
  draw(0, 4);
  orient[1] = sc->s24;
  pos[0] = o.pos[0] - sc->s20 * 0xEB;
  pos[2] = o.pos[2];
  lights_build_oriented(orient);
  obj_set_matrix(pos, orient, scale);
  draw(1, 8);
  orient[1] = -sc->s24;
  pos[0] = o.pos[0] + sc->s20 * 0xEB;
  lights_build_oriented(orient);
  obj_set_matrix(pos, orient, scale);
  draw(2, 8);
  stage_colour_restore();
}

// 0x8008cc00: the handler. Step 0 dim, flash 0x30, (leading the camera) the gate's point
// 240 up and 320 toward +z of the target and the camera on it, wait 20; 1 waits for the
// data, sfx 0x21 / 0x22; 2 the gate grows (0x80 a tick to 10000); 3 the doors open (0x10 a
// tick to 0x400), then the clip and the daemon; 4 waits for the daemon to go, camera, sfx
// 0x26 and 0x2d (fading); 5 the doors close, the clip again, sfx 0x21 / 0x22; 6 the gate
// shrinks (0x100 a tick), flash back, dim end; 7 waits for the sprites; 8 done. Steps 3-5
// spawn a rising sprite (anim 4) round the gate each frame.
void daemons_gate(SpellCast* sc) {
  BattleGlobals& g = bg();
  BObj* o = &sc->obj;
  u8& gone = g_daemon_gone[static_cast<size_t>(sc->slot & 1)];
  if (static_cast<u32>(sc->step - 2) < 5) draw_gate(sc);
  sc->eff_power = sc->caster->st()->intel[2] * 2 + sc->power * 2 + 0x73A;
  if ((g.flags & 4) != 0 || spellcast_timer_tick(sc) != 0) return;
  if (static_cast<u32>(sc->step - 3) < 3) {
    if (Sprite* sp = spellcast_sprite_spawn(sc, 4)) {
      const s32 dx = rng_between_fixed(-500, 500);
      const s32 x = o->pos[0] + dx;
      const s32 dy = rng_between_fixed(300, 1000);
      const s32 y = o->pos[1] - dy;
      const s32 dz = rng_between_fixed(200, 400);
      sprite_set_pos(sp, x, y, o->pos[2] + dz);
      sprite_set_fade(sp, 0x80, -2, 0x10);
      const s32 scale = rng_between(0x2000, 12000);
      sp->scale_y = scale;
      sp->scale_x = scale;
      const s32 shrink = rng_between(-0x200, -0x80);
      sp->scale_vel[1] = shrink;
      sp->scale_vel[0] = shrink;
      sp->roll = static_cast<s16>(rng_range(0x1000));
      sp->roll_speed = static_cast<s16>(rng_between(-0x28, 0x28));
      if (static_cast<u32>(rng_next() & 0x7FFF) < 0x400) bsfx_play_at(0x52, sp);
    }
  }
  switch (sc->step) {
    case 0:
      spellcast_dim_begin(sc, sc->caster->is_enemy);
      battle_flash_set(sc, 0x30, 0x30, 0x30);
      if (spellcast_leads_camera(sc)) {
        camera_reset_mode(g.camera);
        // (looks like an original bug: the gate's point is set only by the cast that leads
        // the camera; the second cast of a clash keeps its set's position.)
        const Unit* t = sc->target;
        o->pos[0] = t->pos[0];
        o->pos[1] = t->pos[1] - 0xF0000;
        o->pos[2] = t->pos[2] + 0x140000;
        camera_move_to(g.camera, o->pos, 0xC00, 0x71, 0x1194, 0x14, 0x80, 5);
      }
      gone = 0;
      spellcast_wait(sc, 0x14);
      return;
    case 1:
      if (sc->data != nullptr) return;
      ++sc->step;
      bsfx_play_at(0x21, o);
      bsfx_play_at(0x22, o);
      return;
    case 2:
      sc->s20 += g.frame_ticks * 0x80;
      if (sc->s20 < 0x2711) return;
      sc->s20 = 10000;
      ++sc->step;
      return;
    case 3:
      sc->s24 += g.frame_ticks * 0x10;
      if (sc->s24 < 0x401) return;
      play_clip(sc);
      sc->s24 = 0x400;
      if (Shot* s = shot_spawn_callback_always(daemon)) {
        drop_trail(s, false);  // (a new Shot's +0x130 is 0)
        daemon_slot(s) = sc->slot;
        s->pos[0] = o->pos[0];
        s->pos[1] = o->pos[1] - 0x1E0000;
        s->scale_x = 0x100;
        s->scale_y = 0x100;
        s->pos[2] = o->pos[2] + 0x80000;
        s->p134 = sc->eff_power;
        s->owner = sc->caster;
        s->target = sc->target;
        bobj_share_sprite(o, s);
      }
      ++sc->step;
      return;
    case 4:
      if (gone == 0) return;
      ++sc->step;
      // (Not only when leading the camera, unlike step 0.)
      camera_move_to(g.camera, o->pos, 0xC00, 0xE3, 0x1000, 0x78, 0x80, 5);
      bsfx_play_at(0x26, o);
      sound_handle_fade_out(bsfx_play_at(0x2D, o), 0x1E, 0x1E);
      return;
    case 5:
      sc->s24 -= g.frame_ticks * 0x10;
      if (-1 < sc->s24) return;
      play_clip(sc);
      sc->s24 = 0;
      ++sc->step;
      bsfx_play_at(0x21, o);
      bsfx_play_at(0x22, o);
      return;
    case 6:
      sc->s20 -= g.frame_ticks * 0x100;
      if (-1 < sc->s20) return;
      sc->s20 = 0;
      ++sc->step;
      battle_flash_set(sc, 0x80, 0x80, 0x80);
      spellcast_dim_end(sc);
      return;
    case 7:
      if (spellcast_effects_gone(sc) == 0) return;
      ++sc->step;
      return;
    case 8:
      sc->finished = 1;
      ++sc->step;
      return;
    default: return;
  }
}

}  // namespace

void spell_mod_daemons_gate(SpellCast* sc) { daemons_gate(sc); }

}  // namespace ss::game::battle
