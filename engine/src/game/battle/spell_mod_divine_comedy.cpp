// Spell code module 476afdf4ce (entry 0x981): Divine Comedy (spell 0x46), Indalecio's last
// spell. The screen goes black and the stage is hidden; on a floor of light two spirals of
// the caster's cels close in on the centre while seven lights rise and sparkles fall; a
// column of light grows there with motes, bursts in a white flash, a dome of light swells
// over the field for 480 ticks and fades; then every standing party member takes a physical
// hit of power 0x380. All along two CLUT rows of VRAM (0 / 0x20, 0x1eb) cycle between two
// colour tables of the module. Functions 0x8008cc00 (the handler; its step jump table at
// 0x8008ec34) and 0x8008eadc (a rising light, a kind-0x21 callback); data: the CLUT tables
// 0x8008ec5c / 0x8008ec9c (32 u16 each), the body object's pointer 0x8008ecdc and the
// uploaded CLUT 0x8008ece0.

#include <memory>
#include <unordered_map>

#include "game/battle/bsfx.h"
#include "game/battle/camera.h"
#include "game/battle/combat.h"
#include "game/battle/enemy_support.h"
#include "game/battle/particle.h"
#include "game/battle/render.h"
#include "game/battle/shot.h"
#include "game/battle/spell_fx.h"
#include "game/battle/spell_modules.h"
#include "game/battle/spellcast.h"
#include "game/battle/sprite.h"
#include "game/battle/unit.h"
#include "game/battle/unit_move.h"
#include "game/boot/rng.h"
#include "game/title/new_game.h"
#include "runtime/libgpu.h"

namespace ss::game::battle {
namespace {

constexpr u32 kClutFrom = 0x8008EC5C;  // u16[32]: the cycled CLUT entries at phase 0
constexpr u32 kClutTo = 0x8008EC9C;    // u16[32]: ... at phase 0x1000

// The SpellCast's fields this module uses: s20 the CLUT cycle's phase (0..0x1fff), s24 the
// spirals' / the column's turn, s28 the first spiral's radius (step 2), then the column's
// radius (steps 3-7), s2c the second spiral's radius, s30 the rising lights spawned + 1,
// s34 the ticks of a step, then the dome's radius (steps 6-7), s38 a flag (step 2), the
// ticks of step 6, then the fade (step 7); obj the data's sprite set (anims 8 and 0xb),
// obj2 the set again (anim 9).

// The module's body object (0x8008ecdc: a heap BObj of bobj_create sharing the caster's
// sprite set, for the UVs of his cels 0x25-0x29), from step 0 to step 9.
std::unordered_map<const SpellCast*, std::unique_ptr<BObj>> g_body;

BObj* body_of(const SpellCast* sc) {
  const auto it = g_body.find(sc);
  return it != g_body.end() ? it->second.get() : nullptr;
}

// Indalecio's +0x2f8 (EnemyIndalecio::transformed: the second form; enemy_mod_indalecio.cpp).
bool caster_second_form(const Unit* u) { return enemy_indalecio_transformed(u); }

// 0x8008eadc: a light (the caster's anim 0x14 at scale 4, turning 20 a tick) rising at its
// velocity; above -0x9c4 it sheds shards (one frame in two); gone above -0x1770.
void rising_light(Shot* s) {
  BattleGlobals& g = bg();
  s->anim = 0x14;
  s->scale_y = 0x4000;
  s->scale_x = 0x4000;
  bobj_project(s);
  bobj_anim_draw(s, g.flags & 0xC);
  if ((g.flags & 0xC) != 0) return;
  s->roll = static_cast<s16>((static_cast<u16>(s->roll) + g.frame_ticks * 0x14) & 0xFFF);
  bobj_move(s, s->vel);
  if (s->pos[1] < -0x9C4000 && static_cast<u32>(rng_next() & 0x7FFF) < 0x4000) {
    const s32 yaw = static_cast<s32>(rng_range(0x1000));
    const s32 pitch = static_cast<s32>(rng_range(0x1000));
    const s32 yaw_speed = rng_between(-0x1E, 0x1E);
    const s32 pitch_speed = rng_between(-0x1E, 0x1E);
    const s32 at[4] = {s->pos[0], s->pos[1], s->pos[2], s->pos_w};
    particle_spawn_shard(at, yaw, pitch, 0x800, yaw_speed, pitch_speed, 0xFFD0C0, 0x28);
  }
  if (s->pos[1] < -0x1770000) shot_kill(s);
}

// Steps 2-4: the floor of light, 12 x 10 gouraud-textured quads 800 apart from (-4000,
// -4000): obj's anim 8 at y -0xdac and, in step 2 only, obj2's anim 9 at -0x8b8 (semi-
// transparent); each corner's colour dims with its distance from the axis (0x110 - d / 16).
void draw_floor(SpellCast* sc) {
  BattleGlobals& g = bg();
  s32 y = -0xDAC;
  for (int layer = 0; layer < 2 && (layer != 1 || sc->step < 3); ++layer) {
    BObj* o = layer == 0 ? &sc->obj : &sc->obj2;
    o->anim = static_cast<u16>(layer == 0 ? 8 : 9);
    bobj_anim_draw(o, (g.flags & 0xC) | 0x9800);
    const FrameUv& f = frame_uv();
    const u16 tpage = f.tpage, clut = f.clut;
    const u32 tag = bobj_poly_tag(o, tpage);
    const s32 uv[4] = {f.u0, f.v0, f.u1, f.v1};
    const u32 code = layer == 0 ? 0x3C000000u : 0x3E000000u;
    s32 z0 = -4000, z1 = -0xC80;
    for (int row = 0; row < 12; ++row) {
      s32 x0 = -4000, x1 = -0xC80;
      for (int col = 0; col < 10; ++col) {
        const auto sv = [y](s32 x, s32 z) {
          return psyq::SVECTOR{static_cast<s16>(x), static_cast<s16>(y), static_cast<s16>(z), 0};
        };
        const psyq::SVECTOR v[4] = {sv(x0, z0), sv(x1, z0), sv(x0, z1), sv(x1, z1)};
        u32 rgb[4];
        for (int k = 0; k < 4; ++k) {
          const s32 vx = v[k].vx, vz = v[k].vz;
          s32 level = 0x110 - (square_root0(vx * vx + vz * vz) >> 4);
          if (level < 0) level = 0;
          rgb[k] = scale_colour(tag, level) | code;
        }
        x1 += 800;
        x0 += 800;
        draw_quad_gt4(uv, v, clut, tpage, 0, rgb);
      }
      z1 += 800;
      z0 += 800;
    }
    y += 500;
  }
}

// Step 2: two spirals of 7 rows x 16 segments of the caster's cels (anims 0x25 / 0x27 the
// segments, 0x26 / 0x28 the first; 0x27 / 0x28 the top row): the first of radius s28 turned
// by +s24, the second (once s2c is set) of radius s2c turned by -s24; each segment 0x180
// further round and 0x50 nearer the axis, the rows 0x140 high from y -0x528 up.
void draw_spirals(SpellCast* sc, BObj* body) {
  for (int ring = 0; ring < 2; ++ring) {
    if ((ring == 0 ? sc->s28 : sc->s2c) == 0) continue;
    s32 y_top = -0x528000, y_low = -0x3E8000;
    s32 c[4][4] = {};
    for (int row = 0; row < 7; ++row) {
      s32 inset = 0, turn = 0;
      c[0][1] = c[1][1] = y_top;
      c[2][1] = c[3][1] = y_low;
      for (int seg = 0; seg < 16; ++seg) {
        if (seg == 0) {
          body->anim = static_cast<u16>(row == 0 ? 0x28 : 0x26);
        } else {
          body->anim = static_cast<u16>(row == 0 ? 0x27 : 0x25);
        }
        bobj_anim_draw(body, 0x9800);
        const FrameUv& f = frame_uv();
        const u16 tpage = f.tpage, clut = f.clut;
        const s32 uv[4] = {f.u0, f.v0, f.u1, f.v1};
        const u32 code = bobj_poly_tag(body, tpage);
        const s32 a = turn + (ring == 0 ? sc->s24 : -sc->s24);
        s32 r = (ring == 0 ? sc->s28 : sc->s2c) - inset;
        c[0][0] = c[2][0] = bcos(static_cast<u32>(a)) * r;
        c[0][2] = c[2][2] = bsin(static_cast<u32>(a)) * r;
        r -= 0x50;
        c[1][0] = c[3][0] = bcos(static_cast<u32>(a + 0x180)) * r;
        inset += 0x50;
        c[1][2] = c[3][2] = bsin(static_cast<u32>(a + 0x180)) * r;
        turn += 0x180;
        draw_textured_patch(uv, c, clut, tpage, 0, code, 1);
      }
      y_top -= 0x140000;
      y_low -= 0x140000;
    }
  }
}

// Steps 3, 4, 6, 7: the column of light, 7 rows x 16 segments of the caster's anim 0x29 of
// radius s28 turned by -s24, the rows 0x140 high from the ground (step 6) or from y -0xdac
// up; outside the pauses it turns 10 a tick and widens 0x10.
void draw_column(SpellCast* sc, BObj* body) {
  BattleGlobals& g = bg();
  body->anim = 0x29;
  bobj_anim_draw(body, 0x9800);
  const FrameUv& f = frame_uv();
  const u16 tpage = f.tpage, clut = f.clut;
  const s32 uv[4] = {f.u0, f.v0, f.u1, f.v1};
  const u32 code = bobj_poly_tag(body, tpage);
  s32 top = -0x140, low = 0;
  s32 c[4][4] = {};
  for (int row = 0; row < 7; ++row) {
    const s32 lift = sc->step != 6 ? 0xDAC : 0;
    c[0][1] = c[1][1] = (top - lift) * 0x1000;
    c[2][1] = c[3][1] = (low - lift) * 0x1000;
    const s32 r = sc->s28;
    for (int seg = 0; seg < 16; ++seg) {
      const s32 a0 = seg * 0x180 - sc->s24;
      const s32 a1 = (seg + 1) * 0x180 - sc->s24;
      c[0][0] = c[2][0] = bcos(static_cast<u32>(a0)) * r;
      c[0][2] = c[2][2] = bsin(static_cast<u32>(a0)) * r;
      c[1][0] = c[3][0] = bcos(static_cast<u32>(a1)) * r;
      c[1][2] = c[3][2] = bsin(static_cast<u32>(a1)) * r;
      draw_textured_patch(uv, c, clut, tpage, 6, code, 1);
    }
    low -= 0x140;
    top -= 0x140;
  }
  if ((g.flags & 0xC) == 0) {
    sc->s24 = (sc->s24 + g.frame_ticks * 10) & 0xFFF;
    sc->s28 += g.frame_ticks * 0x10;
  }
}

// Steps 6-7: the dome, a sphere of 10 x 10 patches of obj's anim 0xb of radius s34 round
// (0, -0x3e8, 0).
void draw_dome(SpellCast* sc) {
  BattleGlobals& g = bg();
  BObj* o = &sc->obj;
  const s32 y = -0x3E8000;
  o->anim = 0xB;
  bobj_anim_draw(o, (g.flags & 0xC) | 0x9800);
  const FrameUv& f = frame_uv();
  const u16 tpage = f.tpage, clut = f.clut;
  const s32 uv[4] = {f.u0, f.v0, f.u1, f.v1};
  const u32 code = bobj_poly_tag(o, tpage);
  const s32 r = sc->s34;
  s32 c[4][4] = {};
  for (int i = 0; i < 10; ++i) {
    for (int j = 0; j < 10; ++j) {
      const s32 lat0 = (i << 11) / 10 - 0x400;
      const s32 cl0 = bcos(static_cast<u32>(lat0)), sl0 = bsin(static_cast<u32>(lat0));
      const s32 lon0 = (j << 12) / 10;
      const s32 co0 = bcos(static_cast<u32>(lon0)), so0 = bsin(static_cast<u32>(lon0));
      const s32 lat1 = ((i + 1) * 0x800) / 10 - 0x400;
      const s32 cl1 = bcos(static_cast<u32>(lat1)), sl1 = bsin(static_cast<u32>(lat1));
      const s32 lon1 = ((j + 1) * 0x1000) / 10;
      const s32 co1 = bcos(static_cast<u32>(lon1)), so1 = bsin(static_cast<u32>(lon1));
      c[0][0] = (cl0 * co0 >> 12) * r;
      c[0][1] = -sl0 * r + y;
      c[0][2] = (cl0 * so0 >> 12) * r;
      c[1][0] = (cl0 * co1 >> 12) * r;
      c[1][1] = c[0][1];
      c[1][2] = (cl0 * so1 >> 12) * r;
      c[2][0] = (cl1 * co0 >> 12) * r;
      c[2][1] = -sl1 * r + y;
      c[2][2] = (cl1 * so0 >> 12) * r;
      c[3][0] = (cl1 * co1 >> 12) * r;
      c[3][1] = c[2][1];
      c[3][2] = (cl1 * so1 >> 12) * r;
      draw_textured_patch(uv, c, clut, tpage, 8, code, 1);
    }
  }
}

// Every frame outside the pauses: the 32 CLUT entries blended between the two tables by
// the phase (a triangle wave of s20, 0..0x1000) and uploaded as two 16-entry rows at
// (0, 0x1eb) and (0x20, 0x1eb); the phase runs 0x40 a tick.
void cycle_clut(SpellCast* sc) {
  s32 t = sc->s20;
  if (0x1000 < t) t = 0x2000 - t;
  alignas(4) u16 clut[32];
  for (u32 i = 0; i < 32; ++i) {
    const u32 a = static_cast<u16>(spell_module_s16(sc, kClutFrom + i * 2));
    const u32 b = static_cast<u16>(spell_module_s16(sc, kClutTo + i * 2));
    const s32 r0 = static_cast<s32>(a & 0x1F), g0 = static_cast<s32>((a >> 5) & 0x1F);
    const s32 b0 = static_cast<s32>((a >> 10) & 0x1F);
    const s32 r = r0 + (((static_cast<s32>(b & 0x1F) - r0) * t) >> 12);
    const s32 gg = g0 + (((static_cast<s32>((b >> 5) & 0x1F) - g0) * t) >> 12);
    const s32 bb = b0 + (((static_cast<s32>((b >> 10) & 0x1F) - b0) * t) >> 12);
    clut[i] = static_cast<u16>(r | gg << 5 | bb << 10 | 0x8000);
  }
  psyq::RECT rect;
  rect.x = 0;
  rect.y = 0x1EB;
  rect.w = 0x10;
  rect.h = 1;
  psyq::LoadImage(&rect, clut);  // 0x80021108
  rect.x = 0x20;
  psyq::LoadImage(&rect, clut + 16);
  sc->s20 = (sc->s20 + bg().frame_ticks * 0x40) & 0x1FFF;
}

// 0x8008cc00: the handler (no power of its own: the hit is a physical one of 0x380).
// 0: a fade to black, the flash off, the body object, wait 30. 1 (data loaded): the stage
// and the effects hidden, obj2 shares obj's set, the body the caster's, the camera far
// above, the hum. 2: the spirals close in, seven rising lights once the first is within
// 0xaf0, sparkles fall; at 400 a fade and the camera; at 440 the spark ring, a long shake,
// step 3. 3: motes rise in the column for 300 ticks; the camera, a fade, wait 45. 4: the
// stage back, the camera, two rising sounds, the burst's sprites, his voice, wait 30. 5:
// sparks drawn in for 120 ticks; then the camera, sounds, a shake, a white flash, 64 sparks,
// five spark rings. 6: the dome swells, stage-wide motes, 480 ticks. 7: the column and the
// body fade. 8: the effects end. 9: the hit on every standing party member.
void divine_comedy(SpellCast* sc) {
  BattleGlobals& g = bg();
  Unit* c = sc->caster;
  if (static_cast<u32>(sc->step - 2) < 3) draw_floor(sc);
  BObj* body = body_of(sc);
  if (sc->step == 2) {
    if (body != nullptr) draw_spirals(sc, body);
  } else if (static_cast<u32>(sc->step - 3) < 2 || static_cast<u32>(sc->step - 6) < 2) {
    if (body != nullptr) draw_column(sc, body);
    if (static_cast<u32>(sc->step - 6) < 2) draw_dome(sc);
  }
  if ((g.flags & 0xC) != 0) return;
  cycle_clut(sc);
  if (spellcast_timer_tick(sc) != 0) return;
  const s32 ring_at[3] = {0, static_cast<s32>(0xFF18C000), 0};
  const s32 centre[3] = {0, -0x3E8000, 0};
  switch (sc->step) {
    case 0:
      // (the console allocates the fade's 0x1c-byte task with 0x8001fd94 and builds it with
      // 0x8003c3b0; the port's screen_fade_create does both)
      screen_fade_create(0, 0x20, -8, 0x1E);
      battle_flash_set(sc, 0, 0, 0);
      g_body[sc] = std::make_unique<BObj>();  // 0x8001fd94(0x88) + bobj_create
      spellcast_wait(sc, 0x1E);
      return;
    case 1: {
      if (sc->data != nullptr) return;
      g.stage_hidden = 1;
      g.effects_suppressed = 1;
      ++sc->step;
      sc->obj2.pool = g.prim_pool_a;
      bobj_share_sprite(&sc->obj, &sc->obj2);
      bobj_share_sprite(c, body);
      const s32 at0[3] = {0, 0, 0};
      camera_move_to(g.camera, at0, 0xC00, -0x71, 7000, 0, 0x80, 5);
      const s32 at1[3] = {0, static_cast<s32>(0xFF448000), 0};
      camera_move_to(g.camera, at1, 0xC00, -0x11C, 0x16A8, 0x2A8, 0x80, 5);
      sc->s28 = 4000;
      sound_handle_fade_out(bsfx_play_ui(0x44), 0x78, 600);
      return;
    }
    case 2: {
      const s32 t = g.frame_ticks;
      sc->s24 = (sc->s24 + t * -0x1E) & 0xFFF;
      sc->s28 += t * -0x10;
      if (0 < sc->s2c) sc->s2c += t * -0x10;
      if (sc->s30 == 0 && sc->s28 < 0xAF0) {
        sc->s2c = 4000;
        sc->s30 = 1;
      }
      if (static_cast<u32>(sc->s30 - 1) < 7) {
        ++sc->s30;
        if (Shot* s = shot_spawn_callback_always(rising_light)) {
          bsfx_play_at(0x34, s);
          const Vec4 at = vec_random_in_disc_xz(kZeroVector, 400);
          s->pos[0] = at.x, s->pos[1] = at.y, s->pos[2] = at.z, s->pos_w = at.w;
          s->vel[1] = static_cast<s32>(0xFFFF0000);
          bobj_share_sprite(c, s);
        }
      }
      for (int i = 0; i < g.frame_ticks << 1; ++i) {
        Sprite* sp = unit_spawn_sprite(c, static_cast<int>(rng_range(8)) + 0x18);
        if (sp == nullptr) continue;
        const Vec4 at = vec_random_in_disc_xz(kZeroVector, 2000);
        sprite_set_pos_v(sp, at.data());
        sp->vel[1] = static_cast<s32>(0xFFFF0000);
        sp->scale_y = 12000;
        sp->scale_x = 12000;
        sp->accel[1] = -0x200;
        sprite_set_fade(sp, 0x80, -2, 0x3C);
        if (static_cast<u32>(rng_next() & 0x7FFF) < 0x800) bsfx_play_at(0x3A, sp);
      }
      sc->s34 += g.frame_ticks;
      if (400 < sc->s34 && sc->s38 == 0) {
        sc->s38 = 1;
        screen_fade_create(0, 0x20, -8, 0x3C);
        camera_reset(g.camera);
        const s32 at[3] = {0, static_cast<s32>(0xFF448000), 0};
        const s32 yaw = rng_range(2) != 0 ? 0xAAA : 0xD55;
        camera_move_to(g.camera, at, yaw, 0xE3, 0x1BBC, 0xB4, 0x80, 5);
        bsfx_play_ui(0x2A);
      }
      if (0x1B8 < sc->s34) {
        sound_handle_fade_out(bsfx_play_ui(0x2D), 0x78, 0x1C2);
        bsfx_play_ui(0x29);
        sc->step = 3;
        sc->s28 = 0;
        sc->s30 = 0;
        sc->s34 = 0;
        sc->s38 = 0;
        if (Shot* r = effect_spawn_sparks_at(ring_at, 0x18, 800, 0, 0x20, 300, 0, 2, 0x78, 0)) {
          r->command = 0xA5;
          r->anim = 0x12;
          bobj_share_sprite(c, r);
        }
        screen_shake(4, 2, 2, 2, 0x1A4);
      }
      return;
    }
    case 3:
      if (Sprite* sp = unit_spawn_sprite(c, 0x20)) {
        Vec4 at = vec_random_in_disc_xz(kZeroVector, sc->s28);
        at.y = static_cast<s32>(0xFF254000);
        sprite_set_pos_v(sp, at.data());
        sp->scale_y = 12000;
        sp->scale_x = 12000;
        sprite_set_fade(sp, 0x80, -2, 8);
        sp->layer = 0xC;
      }
      if (Sprite* sp = unit_spawn_sprite(c, 0x21)) {
        Vec4 at = vec_random_in_disc_xz(kZeroVector, sc->s28);
        at.y = static_cast<s32>(0xFF254000);
        sprite_set_pos_v(sp, at.data());
        sp->scale_y = 12000;
        sp->scale_x = 12000;
        sp->vel[1] = static_cast<s32>(0xFFFF0000);
        sp->accel[1] = -0x200;
        sp->layer = 0xC;
        sprite_set_fade(sp, 0x80, -2, 8);
      }
      sc->s34 += g.frame_ticks;
      if (300 < sc->s34) {
        const s32 at[3] = {0, static_cast<s32>(0xFF448000), 0};
        const s32 yaw = rng_range(2) != 0 ? 0xAAA : 0xD55;
        camera_move_to(g.camera, at, yaw, 0xE3, 0x640, 0x1E, 0x80, 5);
        screen_fade_create(0, 0x20, -8, 0x3C);
        sc->s34 = 0;
        spellcast_wait(sc, 0x2D);
        bsfx_play_ui(0x21);
        bsfx_play_ui(0x22);
        bsfx_play_ui(0x50);
      }
      return;
    case 4: {
      battle_flash_set(sc, 0x20, 0x20, 0x20);
      g.stage_hidden = 0;
      g.effects_suppressed = 0;
      camera_move_to(g.camera, centre, 0x1000, 0x18E, 0xC00, 0, 0x80, 5);
      camera_move_to(g.camera, centre, 0xE38, 0x71, 0x1400, 0x8C, 0x80, 5);
      spellcast_wait(sc, 0x1E);
      u32 h = static_cast<u32>(bsfx_play_at_pos(0x46, centre));
      sound_handle_pitch_up(h, 0xF0);
      sound_handle_fade_out(h, 0x3C, 0xD2);
      h = static_cast<u32>(bsfx_play_at_pos(0x45, centre));
      sound_handle_pitch_up(h, 400);
      sound_handle_fade_out(h, 0x3C, 0xD2);
      sc->s28 = 0;
      if (Sprite* sp = spellcast_sprite_spawn(sc, 1)) {
        sprite_set_pos_v(sp, centre);
        sp->layer = 0xC;
        sprite_set_fade(sp, 0x80, -2, 0x78);
        sp->scale_y = 0x2000;
        sp->scale_x = 0x2000;
      }
      if (Sprite* sp = unit_spawn_sprite(c, 0x23)) {
        sprite_set_pos_v(sp, centre);
        sp->layer = 0xE;
        sprite_set_fade(sp, 0x80, -2, 0xB4);
        sp->scale_vel[1] = 0x80;
        sp->scale_vel[0] = 0x80;
      }
      int voice;
      if (caster_second_form(c)) {
        voice = rng_range(2) != 0 ? 0x52E : 0x530;
      } else {
        voice = rng_range(2) != 0 ? 0x505 : 0x507;
      }
      voice_play_entry(voice, c);  // (third argument -1)
      return;
    }
    case 5:
      for (int i = 0; i < 2; ++i) {
        Sprite* sp = spellcast_sprite_spawn(sc, static_cast<int>(rng_range(3)) + 3);
        if (sp == nullptr) continue;
        const Vec4 at = vec_random_around(centre, 2000);
        sprite_set_pos_v(sp, at.data());
        sprite_set_fade(sp, 0x80, -2, 0xC);
        const Vec4 v = vec_velocity_toward(sp->pos, centre, 0x10);
        sp->vel[0] = v.x, sp->vel[1] = v.y, sp->vel[2] = v.z, sp->extra34 = v.w;
        sp->layer = 4;
      }
      sc->s34 += g.frame_ticks;
      if (0x78 < sc->s34) {
        camera_move_to(g.camera, kZeroVector, 0xC00, 0x38, 0x800, 0, 0x80, 5);
        const s32 yaw = rng_range(2) != 0 ? 0xB1C : 0xD55;
        camera_move_to(g.camera, kZeroVector, yaw, 0x155, 7000, 800, 0x80, 5);
        sound_handle_fade_out(bsfx_play_ui(0x2D), 0x78, 600);
        sound_handle_fade_out(static_cast<u32>(bsfx_play_at_pos(0x44, kZeroVector)), 0x78, 600);
        bsfx_play_ui(0x21);
        bsfx_play_ui(0x50);
        bsfx_play_ui(0x52);
        screen_shake(5, 6, 4, 2, 600);
        screen_fade_create(0xFFFFFF, 0x20, 8, 0);
        sc->s34 = 0;
        sc->step = 6;
        sc->s28 = 0;
        for (int i = 0; i < 0x40; ++i) {
          Sprite* sp = spellcast_sprite_spawn(sc, static_cast<int>(rng_range(3)) + 3);
          if (sp == nullptr) continue;
          sprite_set_pos_v(sp, centre);
          sprite_set_fade(sp, 0x80, -4, 0xC);
          const Vec4 v = vec_random_around(kZeroVector, 0x28);
          sp->vel[0] = v.x, sp->vel[1] = v.y, sp->vel[2] = v.z, sp->extra34 = v.w;
          sp->layer = 4;
        }
        if (Sprite* sp = spellcast_sprite_spawn(sc, 2)) {
          sprite_set_pos_v(sp, centre);
          sp->layer = 0xE;
          sprite_set_fade(sp, 0x80, -2, 0x208);
          sp->scale_y = 0x2000;
          sp->scale_x = 0x2000;
        }
        if (Sprite* sp = unit_spawn_sprite(c, 0x22)) {
          sprite_set_pos_v(sp, centre);
          sp->layer = 0xE;
          sprite_set_fade(sp, 0x80, -2, 0x78);
          sp->scale_y = 24000;
          sp->scale_x = 24000;
        }
        sc->s34 = 0;
        sc->s38 = 0;
        effect_spawn_sparks_at(centre, 0x18, 800, 0, 0x10, 0x21C, 0xB, 3, 0x78, 0);
        const s32 at1[3] = {0, -0x3E8000, 0};
        effect_spawn_sparks_at(at1, 0x18, 800, 0, 0x80, 0x78, 5, 3, 0x78, 0);
        const s32 at2[3] = {0, static_cast<s32>(0xFFF9C000), 0};
        effect_spawn_sparks_at(at2, 0x18, 800, 0, 0x80, 0x78, 5, 3, 0x78, 0);
        const s32 at3[3] = {0, 0, 0};
        effect_spawn_sparks_at(at3, 0x18, 600, 0x78, 0x18, 0x21C, 0xE, 3, 0x78, 0);
        const s32 at4[3] = {0, static_cast<s32>(0xFFCE0000), 0};
        effect_spawn_sparks_at(at4, 0x18, 800, 0, 0x40, 0x21C, 0xD, 4, 0x78, 0);
      }
      return;
    case 6:
      sc->s34 += g.frame_ticks * 8;
      for (int i = 0; i < 2; ++i) {
        const int anim = rng_range(2) != 0 ? 0x17 : 0x20;
        Sprite* sp = unit_spawn_sprite_ex(c, anim, 1);
        if (sp == nullptr) continue;
        s32 p[4];
        stage_random_point(p, 1);
        p[1] = rng_between_fixed(-400, 0);
        sprite_set_pos_v(sp, p);
        const s32 scale = rng_between(0x2000, 0x4000);
        sp->scale_y = scale;
        sp->scale_x = scale;
        const s32 speed = rng_between(4, 8);
        const Vec4 v = vec_random_around(kZeroVector, speed);
        sp->vel[0] = v.x, sp->vel[1] = v.y, sp->vel[2] = v.z, sp->extra34 = v.w;
        if (static_cast<u32>(rng_next() & 0x7FFF) < 0x1000) bsfx_play_at(0x21, sp);
      }
      sc->s38 += g.frame_ticks;
      if (sc->s38 < 0x1E1) return;
      sc->s38 = 0;
      ++sc->step;
      return;
    case 7:
      sc->s34 += g.frame_ticks * 8;
      sc->s38 += g.frame_ticks * 4;
      if (0x7F < sc->s38) ++sc->step;
      sc->obj.colour_mode = 2;
      sc->obj.rgb[0] = static_cast<u8>(-0x80 - sc->s38);
      if (body != nullptr) {
        body->colour_mode = 2;
        body->rgb[0] = static_cast<u8>(-0x80 - sc->s38);
      }
      return;
    case 8:
      if (spellcast_effects_gone(sc) != 0) ++sc->step;
      return;
    case 9: {
      g_body.erase(sc);  // 0x8001fd70
      Unit* list[8] = {};
      Vec4 points[8] = {};
      int n = 0;
      for (int i = 0; i < 4; ++i) {
        Unit* u = g.party_units[static_cast<size_t>(i)];
        if (unit_is_down(u)) continue;
        list[n] = u;
        // (w: an uninitialised stack word on the console)
        points[n] = Vec4{u->pos[0], u->pos[1] + u->size[1] * -0x2000, u->pos[2], 0};
        ++n;
      }
      if (n != 0) {
        unit_attack_targets(c, n, list, points, 0x380, 9999, 0);
        c->attack_recovery = 0;
      }
      battle_flash_set(sc, 0x80, 0x80, 0x80);
      sc->finished = 1;
      ++sc->step;
      return;
    }
    default: return;
  }
}

}  // namespace

void spell_mod_divine_comedy(SpellCast* sc) { divine_comedy(sc); }

}  // namespace ss::game::battle
