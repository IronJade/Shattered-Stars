// Spell code module 96440a6c9b (entries 0x937 / 0x938): Raise Dead (spell 0x34): the battle
// music gives way to the spell's own (the data's command-8 block); a funnel of light opens
// over the target with a glowing disc under it, motes spiral down into it, then a figure of
// the target's (the data's sprite set, by character) sinks into the body: a fallen target
// stands up again with half its maximum HP (a standing one is healed as much). Functions
// 0x8008cc00 (the handler; its step jump table at 0x8008db30), 0x8008d6fc (a spiralling
// mote, a kind-0x21 callback) and 0x8008d914 (the figure, kind 0x21).

#include <cstring>
#include <unordered_map>
#include <vector>

#include "disc/slz.h"
#include "game/battle/bsfx.h"
#include "game/battle/camera.h"
#include "game/battle/combat.h"
#include "game/battle/enemy_support.h"
#include "game/battle/killer.h"
#include "game/battle/render.h"
#include "game/battle/shot.h"
#include "game/battle/spell_modules.h"
#include "game/battle/spellcast.h"
#include "game/battle/sprite.h"
#include "game/battle/stage.h"
#include "game/battle/unit.h"
#include "game/boot/rng.h"
#include "game/boot/sound.h"
#include "game/common/screen.h"
#include "game/title/new_game.h"
#include "psx/gte.h"
#include "runtime/libgpu.h"
#include "runtime/libgte.h"

namespace ss::game::battle {
namespace {

// The SpellCast's fields this module uses: s20 the ticks of step 3, s24 the funnel's turn,
// s28 the spell's music (the unpacked sequence: g_music here), s2c the funnel's width
// (0..0x100), s30 the mote timer, s34 the figure was sent, s38 the battle music to resume
// and s3c the driver's save buffer (g_music); obj the data's sprite set (anim 0x10 the
// funnel; grey +0x198 the disc's brightness, 0..0x50).
struct Music {
  std::vector<u8> sequence;           // s28 (slz_alloc_output + slz_unpack of the +0xc8 block)
  const u8* resume = nullptr;         // s38: the sequence playing before (sound_state +0x14)
  SoundSavedState* saved = nullptr;   // s3c (mem_alloc 0xb14; the driver frees it on restore)
};
std::unordered_map<const SpellCast*, Music> g_music;

// The figure's afterimage (Shot +0x134 holds the pointer on the console).
std::unordered_map<const Shot*, Afterimage*> g_trail;

const s32 kUnitScale[3] = {0x1000, 0x1000, 0x1000};  // 0x8006e34c

s32 mul_lo(s32 a, s32 b) { return static_cast<s32>(static_cast<u32>(a) * static_cast<u32>(b)); }

// 0x8008d6fc: a mote (anim 0xe) spiralling down onto its unit (+0x114): round it at an
// angle (+0x128, 20 a tick) and a radius shrinking with the height left (from 0x40 + the
// unit's width), facing its motion; a trail sprite every 4 ticks; at the ground a flash of
// the next anim and it goes.
void spiral_mote(Shot* s) {
  BattleGlobals& g = bg();
  const Unit* t = s->target;
  const s32 width = t->size[0];
  const s32 ty = t->pos[1];
  s->facing[2] = s->pos[2];
  s->facing[0] = s->pos[0];
  const s32 r = (((ty - s->pos[1]) >> 12) * width * 8) / 0x2A0 + 0x40;
  s->pos[0] = t->pos[0] + bcos(static_cast<u32>(s->p128)) * r;
  s->pos[2] = t->pos[2] + bsin(static_cast<u32>(s->p128)) * r;
  s->facing[0] = s->pos[0] - s->facing[0];
  s->facing[2] = s->pos[2] - s->facing[2];
  bobj_project(s);
  bobj_anim_draw(s, g.flags & 0xC);
  if ((g.flags & 0xC) != 0) return;
  s->p130 += g.frame_ticks;
  if (3 < s->p130) {
    s->p130 -= 4;
    if (Sprite* sp = shot_spawn_sprite(s, 0xE)) {
      sprite_set_pos_v(sp, s->pos);
      sp->vel[1] = 0x1000;
      sprite_set_fade(sp, 0x80, -4, 0);
    }
  }
  s->pos[1] += g.frame_ticks * 0x4000;
  s->p128 = (s->p128 + g.frame_ticks * 0x14) & 0xFFF;
  if (bobj_ground_height(s) <= s->pos[1]) {
    if (Sprite* sp = shot_spawn_sprite(s, s->anim + 1)) {
      sprite_set_pos_v(sp, s->pos);
      sp->blend = 0x82;
      sp->layer = 2;
      sprite_set_fade(sp, 0x80, -4, 0);
    }
    shot_kill(s);
  }
}

// 0x8008d914: the figure (its anim set by the handler) sinking 4 a tick with a 6-copy
// afterimage; at its unit's height a flash of the next anim; a fallen unit loses its KO
// (status bit 0) and, standing again, recovers ([21]) and starts over ([12]); either way it
// gets half its maximum HP (a popup), and the figure goes.
void sinking_figure(Shot* s) {
  BattleGlobals& g = bg();
  s->layer = 1;
  bobj_project(s);
  bobj_anim_draw(s, g.flags & 0xC);
  if ((g.flags & 0xC) != 0) return;
  Afterimage*& trail = g_trail[s];
  if (trail == nullptr) trail = afterimage_create(s, 6, 0x4070, 2);
  if (trail != nullptr) trail->next_kind = 1;
  s->pos[1] += g.frame_ticks * 0x4000;
  Unit* t = s->target;
  if (!(t->pos[1] <= s->pos[1])) return;
  if (Sprite* sp = shot_spawn_sprite(s, s->anim + 1)) {
    sprite_set_pos_v(sp, s->pos);
    sp->blend = 0x82;
    sp->layer = 2;
    sprite_set_fade(sp, 0x80, -2, 0);
  }
  if (unit_is_down(t)) {
    t->st()->status &= 0xFE;
    if (!unit_is_down(t)) {
      t->status_recovered();  // [21]
      t->action_kind = 0;
      t->begin_action();      // [12]
    }
  }
  const s32 heal = -(t->st()->hp_max / 2);
  damage_popup_spawn(t, heal, static_cast<s32>(0xFFFFE000), 0);
  unit_damage_hp(t, heal);
  shot_kill(s);
  if (trail != nullptr) trail->ending = 1;
  g_trail.erase(s);
}

// Steps 2-4: the funnel over the target, 6 rings x 16 segments of anim 0x10 (the cel split
// in 16 across), ring k from radius k * w / 6 to (k + 1) * w / 6 (w = the target's width *
// s2c * 12 / 256) and from 0x70 * (6 - k) to 0x70 * (5 - k) up, each ring turned 0x8000 /
// 360 more, all by s24 (12 a tick); then the disc on the ground under it (radius w).
void draw_funnel(SpellCast* sc) {
  BattleGlobals& g = bg();
  BObj* o = &sc->obj;
  o->anim = 0x10;
  bobj_anim_draw(o, 0x9800);
  const FrameUv& f = frame_uv();
  const s32 u0 = f.u0, v0 = f.v0, u1 = f.u1, v1 = f.v1;
  const u16 tpage = f.tpage, clut = f.clut;
  const u32 code = bobj_poly_tag(o, tpage);
  const Unit* t = sc->target;
  const s32 w = (t->size[0] * sc->s2c * 0xC) / 0x100;
  const s32 tx = t->pos[0], tz = t->pos[2];
  s32 inner = 0, outer = w, turn_hi = 0x8000;
  s32 c[4][4] = {};
  for (int k = 0; k < 6; ++k) {
    const s32 r0 = inner / 6, r1 = outer / 6;
    // ORIGINAL BUG? (to review): the target's y is already 20.12 and is shifted again, so
    // the funnel is placed right only while the target stands at y 0 (the usual ground).
    const s32 ty = sc->target->pos[1];
    const s32 y0 = mul_lo(ty + (6 - k) * -0x70, 0x1000);
    const s32 y1 = mul_lo(ty + (5 - k) * -0x70, 0x1000);
    const s32 turn0 = sc->s24 + (k * 0x8000) / 360;
    const s32 turn1 = sc->s24 + turn_hi / 360;
    for (int j = 0; j < 16; ++j) {
      const s32 uv[4] = {((u1 - u0) * j) / 16 + u0, v0, ((u1 - u0) * (j + 1)) / 16 + u0, v1};
      const s32 a0 = (((j * 0x168) / 16) * 0x1000) / 360;
      const s32 a1 = ((((j + 1) * 0x168) / 16) * 0x1000) / 360;
      c[0][0] = bcos(static_cast<u32>(a0 + turn0)) * r0 + tx;
      c[0][1] = y0;
      c[0][2] = bsin(static_cast<u32>(a0 + turn0)) * r0 + tz;
      c[1][0] = bcos(static_cast<u32>(a1 + turn0)) * r0 + tx;
      c[1][1] = y0;
      c[1][2] = bsin(static_cast<u32>(a1 + turn0)) * r0 + tz;
      c[2][0] = bcos(static_cast<u32>(a0 + turn1)) * r1 + tx;
      c[2][1] = y1;
      c[2][2] = bsin(static_cast<u32>(a0 + turn1)) * r1 + tz;
      c[3][0] = bcos(static_cast<u32>(a1 + turn1)) * r1 + tx;
      c[3][1] = y1;
      c[3][2] = bsin(static_cast<u32>(a1 + turn1)) * r1 + tz;
      draw_textured_patch(uv, c, clut, tpage, 8, code, 1);
    }
    turn_hi += 0x8000;
    outer += w;
    inner += w;
  }
  if ((g.flags & 0xC) == 0) sc->s24 = (sc->s24 + g.frame_ticks * 0xC) & 0xFFF;
  const s32 at[3] = {t->pos[0], bobj_ground_height(t), t->pos[2]};
  s32 orient[3] = {0, 0, 0};  // (g_zero_vector)
  obj_set_matrix(at, orient, kUnitScale);
  const u32 centre = scale_colour(0xE0A060, sc->obj.rgb[0] << 1);
  const u32 rim = scale_colour(0xA06030, sc->obj.rgb[0] << 1);
  draw_disc(0x10, w, centre, rim, 2, 5);
}

// 0x8008cc00: the handler (no power: the figure gives half the maximum HP). 0: flash 0x30,
// the camera on the target, the music's state saved (a driver save buffer) and the music
// faded. 1 (data loaded): the disc's colour from 0, the spell's music unpacked from the
// data's command-8 block and played. 2: the funnel opens (3 a tick to 0x100), the disc
// brightens. 3: a mote every 16 ticks; at 60 the camera and the figure; at 90 wait 100. 4:
// the funnel closes (4 a tick); then flash back, undim, the saved music state restored. 5:
// the battle music resumed (fading in over 30); the effects end. 6: the spell's music freed,
// done.
void raise_dead(SpellCast* sc) {
  BattleGlobals& g = bg();
  Unit* t = sc->target;
  if (static_cast<u32>(sc->step - 2) < 3) draw_funnel(sc);
  if ((g.flags & 4) != 0) return;
  if (spellcast_timer_tick(sc) != 0) return;
  switch (sc->step) {
    case 0: {
      battle_flash_set(sc, 0x30, 0x30, 0x30);
      if (spellcast_leads_camera(sc)) camera_move_to(g.camera, t->pos, 0xC00, 0x1C7, 0x800, 0x1E, 0x80, 5);
      ++sc->step;
      Music& m = g_music[sc];
      m.saved = new SoundSavedState;  // mem_alloc(0xb14)
      m.resume = g.sound->previous_sequence;  // (sound_state +0x14)
      g.sound->request_save = 1;
      g.sound->saved_state = m.saved;
      sound_fade_out(g.sound, 0x14);
      return;
    }
    case 1: {
      if (sc->data != nullptr) return;
      sc->obj.colour_mode = 2;
      sc->obj.rgb[0] = 0;
      ++sc->step;
      Music& m = g_music[sc];
      if (auto seq = disc::slz::unpack_block(sc->raw, 0, 0, nullptr)) {
        m.sequence = std::move(*seq);
        sc->raw.clear();
        sc->raw.shrink_to_fit();  // (the block is freed)
        sound_play_sequence(g.sound, m.sequence.data());
      }
      return;
    }
    case 2: {
      const u32 lit = static_cast<u32>(sc->obj.rgb[0]) + static_cast<u32>(g.frame_ticks * 2);
      sc->obj.rgb[0] = static_cast<u8>(lit);
      sc->s2c += g.frame_ticks * 3;
      if (0x4F < (lit & 0xFF)) sc->obj.rgb[0] = 0x50;
      if (sc->s2c < 0x101) return;
      sc->s2c = 0x100;
      ++sc->step;
      return;
    }
    case 3:
      sc->s30 += g.frame_ticks;
      if (0xF < sc->s30) {
        sc->s30 -= 0x10;
        if (Shot* s = shot_spawn_callback_always(spiral_mote)) {
          s->target = t;
          s->pos[0] = t->pos[0];
          s->pos[1] = t->pos[1] - 0x2A0000;
          s->pos[2] = t->pos[2];
          s->p128 = static_cast<s32>(rng_range(0x1000));
          s->anim = 0xE;
          bobj_share_sprite(&sc->obj, s);
        }
      }
      sc->s20 += g.frame_ticks;
      if (sc->s34 == 0 && 0x3B < sc->s20) {
        if (spellcast_leads_camera(sc)) camera_move_to(g.camera, t->pos, 0xC00, 0x93, 0x800, 0x78, 0x80, 5);
        sc->s34 = 1;
        if (Shot* s = shot_spawn_callback_always(sinking_figure)) {
          g_trail.erase(s);
          s->target = t;
          s->pos[0] = t->pos[0];
          s->pos[1] = t->pos[1] - 0x280000;
          s->pos[2] = t->pos[2];
          s->anim = static_cast<u16>(t->is_enemy == 0 ? static_cast<s16>(t->st()->id) - 1 : 0xC);
          bobj_share_sprite(&sc->obj, s);
        }
      }
      if (0x59 < sc->s20) spellcast_wait(sc, 100);
      return;
    case 4:
      sc->s2c += g.frame_ticks * -4;
      if (0 < sc->s2c) return;
      ++sc->step;
      battle_flash_set(sc, 0x80, 0x80, 0x80);
      spellcast_dim_end(sc);
      if (Music& m = g_music[sc]; m.saved != nullptr) {
        sound_stop(g.sound);
        g.sound->saved_state = m.saved;
        g.sound->request_restore = 1;
      }
      return;
    case 5: {
      Music& m = g_music[sc];
      if (m.saved != nullptr && m.resume != nullptr) {
        sound_resume_sequence(g.sound, const_cast<u8*>(m.resume), 0x1E, 1);
        m.resume = nullptr;
      }
      if (spellcast_effects_gone(sc) == 0) return;
      ++sc->step;
      return;
    }
    case 6:
      g_music.erase(sc);  // 0x8001fd70 (s28)
      sc->finished = 1;
      ++sc->step;
      return;
    default: return;
  }
}

}  // namespace

void spell_mod_raise_dead(SpellCast* sc) { raise_dead(sc); }

}  // namespace ss::game::battle
