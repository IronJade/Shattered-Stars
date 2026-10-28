// Spell code module fa606854ed (entries 0x93e / 0x93f): Dark Circle (spell 0x26): darkness
// streams into a point in front of the caster, then a black vortex opens 800 units above the
// target; every unit of the other side below a quarter of its HP that fails a resist roll
// is drawn spiralling into it and dies. Functions 0x8008cc00 (the handler), 0x8008d6c0 (a
// stream of darkness, a kind-0x21 callback) and 0x8008d7b8 (the vortex, kind 0x21); the
// module's writable data 0x8008da2c..0x8008da90 (the victims, the vortex) is CircleState.

#include <unordered_map>

#include "game/battle/bsfx.h"
#include "game/battle/camera.h"
#include "game/battle/enemy_support.h"
#include "game/battle/killer.h"
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

namespace ss::game::battle {
namespace {

// The SpellCast's fields: s20 the streams' ticks (step 2), s24 the glow spawned, s38 / s3c
// the sounds' handles; obj the data's sprite set and (from step 1) the target's position:
// the vortex is at its x / z, 800 units up.

// The module's writable data.
struct CircleState {
  Unit* victim[8] = {};  // 0x8008da2c: units_in_rect's list, cleared where spared
  s32 dist[8] = {};      // 0x8008da4c: each victim's distance to the vortex (units)
  s16 yaw[8] = {};       // 0x8008da6c: its angle round the vortex in x / z
  s16 pitch[8] = {};     // 0x8008da7c: its angle in x / y (turning 40 a tick)
  Shot* vortex = nullptr;  // 0x8008da8c
};
std::unordered_map<const SpellCast*, CircleState> g_state;

// A stream's afterimage (Shot +0x130 holds the pointer on the console).
std::unordered_map<const Shot*, Afterimage*> g_trail;

// 0x8008d6c0: a stream of darkness (anim 0) flying at the gathering point, rolled along its
// velocity, with an afterimage; +0x128 the distance left (32 units a tick), 4 past it goes.
void dark_stream(Shot* s) {
  BattleGlobals& g = bg();
  s->roll = static_cast<s16>(ratan2(s->vel[1], -s->vel[0]));
  bobj_project(s);
  bobj_anim_draw(s, g.flags & 0xC);
  s->layer = 3;
  if ((g.flags & 0xC) != 0) return;
  Afterimage*& trail = g_trail[s];
  if (trail == nullptr) trail = afterimage_create(s, 6, 0x301060, 0);
  bobj_move(s, s->vel);
  s->p128 += g.frame_ticks * -0x20000;
  if (-0x40000 < s->p128) return;
  if (trail != nullptr) trail->ending = 1;
  g_trail.erase(s);
  shot_kill(s);
}

// 0x8008d7b8: the vortex (anim 1) turning 30 a tick, throwing sparks (anims 4-9) half the
// time until +0x12c reaches 64, with dark flashes 1 in 16; +0x128 0 it grows to 20000 wide,
// 2 it shrinks and fades out over 128 ticks.
void vortex(Shot* s) {
  BattleGlobals& g = bg();
  s->anim = 1;
  bobj_project(s);
  bobj_anim_draw(s, g.flags & 0xC);
  if ((g.flags & 0xC) != 0) return;
  s->roll = static_cast<s16>((s->roll + g.frame_ticks * 0x1E) & 0xFFF);
  if (static_cast<u32>(rng_next() & 0x7FFF) < 0x4000 && s->p12c < 0x40) {
    if (Sprite* sp = shot_spawn_sprite(s, static_cast<int>(rng_range(6)) + 4)) {
      sprite_set_pos_v(sp, s->pos);
      sp->layer = static_cast<s8>(rng_between(-2, 3));
      const s32 scale = rng_between(0x1000, 12000);
      sp->scale_y = scale;
      sp->scale_x = scale;
      sp->roll = static_cast<s16>(rng_range(0x1000));
      sprite_set_fade(sp, 0x80, -2, 10);
    }
    if (static_cast<u32>(rng_next() & 0x7FFF) < 0x1000) bsfx_play_at(0x52, s);
  }
  if (static_cast<u32>(rng_next() & 0x7FFF) < 0x800) {
    ViewState& v = view();
    v.stage_colour[2] = v.stage_colour[1] = v.stage_colour[0] = 0x1B8;
    g.flash_active = 1;
    g.flash_delay = 2;
    g.flash_speed = 0x20;
    bsfx_play_at(0x1F, s);
  }
  if (s->p128 == 0) {
    s->scale_x += g.frame_ticks * 0x80;
    s->scale_y += g.frame_ticks * 0x40;
    if (20000 < s->scale_x) s->p128 = 1;
  } else if (s->p128 == 2) {
    s->colour_mode = 2;
    s->scale_x -= g.frame_ticks * 0x80;
    s->rgb[0] = static_cast<u8>(-0x80 - s->p12c);
    s->p12c += g.frame_ticks;
    s->scale_y -= g.frame_ticks * 0x40;
    if (0x7F < s->p12c) shot_kill(s);
  }
}

void dark_circle(SpellCast* sc) {
  BattleGlobals& g = bg();
  Unit* c = sc->caster;
  if ((g.flags & 4) != 0) return;
  if (spellcast_timer_tick(sc) != 0) return;
  if (sc->step == 0) g_state[sc] = CircleState{};  // (the module's data is fresh at each cast)
  CircleState& st = g_state[sc];
  const s32 centre[4] = {sc->obj.pos[0], -0x320000, sc->obj.pos[2], 0};
  switch (sc->step) {
    case 0:
      spellcast_dim_begin(sc, c->is_enemy);
      battle_flash_set(sc, 0x40, 0x40, 0x40);
      if (spellcast_leads_camera(sc)) camera_move_to(g.camera, c->pos, 0xC00, 0xAA, 0x800, 0xF, 0x80, 5);
      spellcast_wait(sc, 0x1E);
      return;
    case 1: {
      // The data loaded: camera, the vortex's place, the rising hums.
      if (sc->data != nullptr) return;
      if (spellcast_leads_camera(sc)) {
        camera_reset(g.camera);
        const s32 yaw = rng_range(2) != 0 ? 0xD55 : 0xAAA;
        camera_move_to(g.camera, c->pos, yaw, 0x111, 0xC00, 0x5A, 0x80, 5);
      }
      ++sc->step;
      const Unit* t = sc->target;
      sc->obj.pos[0] = t->pos[0];
      sc->obj.pos[1] = t->pos[1];
      sc->obj.pos[2] = t->pos[2];
      sc->obj.pos_w = t->pos_w;
      sc->s3c = static_cast<s32>(bsfx_play_at(0x45, &sc->obj));
      sc->s38 = static_cast<s32>(bsfx_play_at(0x46, &sc->obj));
      sound_handle_pitch_up(static_cast<u32>(sc->s3c), 0x78);
      sound_handle_pitch_up(static_cast<u32>(sc->s38), 0xB4);
      [[fallthrough]];
    }
    case 2: {
      // Darkness streams (one a tick) into a point in front of the caster; a glow there
      // (anim 10) after 30 ticks; past 90 ticks the vortex.
      const s32 at[4] = {bobj_front_x(c, 0xC0), c->pos[1] + c->size[1] * -0x2000, c->pos[2], 0};
      sc->s20 += g.frame_ticks;
      if (0x1E < sc->s20 && sc->s24 == 0) {
        sc->s24 = 1;
        if (Sprite* sp = spellcast_sprite_spawn(sc, 10)) {
          sprite_set_pos_v(sp, at);
          sprite_set_fade(sp, 0x80, -2, 0x5A);
          sp->layer = 4;
          sp->scale_vel[1] = 0x80;
          sp->scale_vel[0] = 0x80;
          sp->roll_speed = -0x14;
        }
      }
      if (0x5A < sc->s20) spellcast_wait(sc, 0x14);
      for (int i = 0; i < g.frame_ticks; ++i) {
        Shot* s = shot_spawn_callback_always(dark_stream);
        if (s == nullptr) continue;
        g_trail.erase(s);
        const Vec4 p = vec_random_around(at, 0x500);
        s->pos[0] = p.x, s->pos[1] = p.y, s->pos[2] = p.z, s->pos_w = p.w;
        const Vec4 v = vec_velocity_toward(s->pos, at, 0x20);
        s->vel[0] = v.x, s->vel[1] = v.y, s->vel[2] = v.z, s->extra34 = v.w;
        s->p128 = vec_distance_fx(s->pos, at);
        s->anim = 0;
        s->owner = c;
        bobj_share_sprite(&sc->obj, s);
      }
      return;
    }
    case 3: {
      // The vortex opens: camera, sounds, a white fade, flash 0x60; the victims are those of
      // the other side below a quarter of their HP that fail the resist roll (each gets an
      // afterimage).
      if (spellcast_leads_camera(sc)) {
        const s32 yaw = (rng_range(2) != 0 ? 1 : 0) << 11;
        camera_move_to(g.camera, centre, yaw, 0x38, 0xC00, 0, 0x80, 5);
        camera_move_to(g.camera, centre, 0xC00, 0xE3, 0x1000, 0x3C, 0x80, 5);
      }
      sound_handle_fade_out(static_cast<u32>(sc->s3c), 0x1E, 0);
      sound_handle_fade_out(static_cast<u32>(sc->s38), 0x1E, 0);
      sc->s3c = static_cast<s32>(bsfx_play_ui(0x2D));
      bsfx_play_at(0x28, &sc->obj);
      bsfx_play_at(0x29, &sc->obj);
      bsfx_play_at(0x22, &sc->obj);
      screen_fade_create(0xFFFFFF, 0x40, 4, 0x1E);
      st.vortex = shot_spawn_callback_always(vortex);
      if (st.vortex != nullptr) {
        st.vortex->pos[0] = centre[0], st.vortex->pos[1] = centre[1], st.vortex->pos[2] = centre[2];
        bobj_share_sprite(&sc->obj, st.vortex);
      }
      battle_flash_set(sc, 0x60, 0x60, 0x60);
      const int n = units_in_rect(sc, stage().bounds, st.victim, c->is_enemy);
      for (int i = 0; i < 8; ++i) {
        Unit* u = st.victim[i];
        if (!(i < n) || !(u->st()->hp < u->st()->hp_max / 4) || u->resist_roll(0, 0x200) != 1) {
          st.victim[i] = nullptr;
          continue;
        }
        st.dist[i] = vec_distance_fx(u->pos, centre) >> 12;
        st.pitch[i] = static_cast<s16>(ratan2(u->pos[1] - centre[1], u->pos[0] - centre[0]));
        st.yaw[i] = static_cast<s16>(ratan2(u->pos[2] - centre[2], u->pos[0] - centre[0]));
        if (u->effects[0] == nullptr) u->effects[0] = afterimage_create(u, 6, 0x305000, 1);
      }
      spellcast_wait(sc, 0x5A);
      return;
    }
    case 4: {
      if (spellcast_leads_camera(sc)) {
        const s32 above[3] = {centre[0], -0x190000, centre[2]};
        camera_move_to(g.camera, above, 0xC00, 0x11C, 0xC00, 0x78, 0x80, 5);
      }
      ++sc->step;
      return;
    }
    case 5: {
      // The victims spiral in (radius 4 a tick, shrinking near the end); at the centre each
      // dies (HP 0, death 2) with sfx 0x34 / 0x24 / 0x4b. When none is left the vortex fades.
      int gone = 0;
      for (int i = 0; i < 8; ++i) {
        Unit* u = st.victim[i];
        if (u == nullptr) {
          ++gone;
          continue;
        }
        u->pos[0] = centre[0] + ((bcos(static_cast<u32>(st.yaw[i])) * bcos(static_cast<u32>(st.pitch[i]))) >> 12) * st.dist[i];
        u->pos[1] = centre[1] + bsin(static_cast<u32>(st.pitch[i])) * st.dist[i];
        u->pos[2] = centre[2] + ((bsin(static_cast<u32>(st.yaw[i])) * bcos(static_cast<u32>(st.pitch[i]))) >> 12) * st.dist[i];
        if ((g.flags & 0xC) != 0) continue;
        st.pitch[i] = static_cast<s16>((st.pitch[i] + g.frame_ticks * 0x28) & 0xFFF);
        st.dist[i] -= g.frame_ticks * 4;
        if (st.dist[i] < 0x40) {
          u->scale_x -= g.frame_ticks * 0x80;
          u->scale_y -= g.frame_ticks * 0x80;
        }
        if (0 < st.dist[i]) continue;
        bsfx_play_at(0x34, &sc->obj);
        bsfx_play_at(0x24, &sc->obj);
        bsfx_play_at(0x4B, &sc->obj);
        u->st()->hp = 0;
        u->death = 2;
        st.victim[i] = nullptr;
        g.last_attacker = c;
      }
      if (gone != 8) return;
      if (st.vortex != nullptr) st.vortex->p128 = 2;
      ++sc->step;
      sound_handle_fade_out(static_cast<u32>(sc->s3c), 0x5A, 0);
      bsfx_play_at(0x26, &sc->obj);
      return;
    }
    case 6:
      if (spellcast_effects_gone(sc) == 0 || shot_kind21_absent(vortex) == 0) return;
      ++sc->step;
      return;
    case 7:
      if (spellcast_leads_camera(sc)) camera_move_to(g.camera, sc->obj.pos, 0xC00, 0xAA, 0x1000, 0x1E, 0x80, 5);
      battle_flash_set(sc, 0x80, 0x80, 0x80);
      spellcast_dim_end(sc);
      sc->finished = 1;
      ++sc->step;
      g_state.erase(sc);
      return;
    default: return;
  }
}

}  // namespace

void spell_mod_dark_circle(SpellCast* sc) { dark_circle(sc); }

}  // namespace ss::game::battle
