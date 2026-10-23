// Spell code module 155b3550d0 (entry 0x980): Wind of Destruction (spell 0x45), Cyril's
// boss spell. Cyril (drawn here with his aura while the spell runs) is lifted 5000 units
// over the field's centre under two walls of storm cloud lit by lightning; 12 sparks orbit
// him, then 12 sparks spiral down trailing ribbons while a whirlwind turns at the centre and
// sparks spiral to the ground; the screen flashes white, three spark rings and a ground ring
// burst, he is put back, a dome of storm (a sphere and a funnel of the data's anim 0) turns
// over the field for 240 ticks with debris flying, fades, and every standing party member
// takes a physical hit (power 0x380, knocked along x, element 8).
// Functions (block 0, 7771 bytes): 0x8008cc00 the handler, 0x8008e55c an orbiting spark
// and 0x8008e718 a sinking spark (kind-0x21 callbacks, placed by 0x8008e628), 0x8008e8c0 a
// ground spark (kind 0x21); tables 0x8008ea14 (the step switch) and 0x8008ea3c-0x8008ea5a
// (the three spark rings of step 5).

#include <unordered_map>

#include "game/battle/bsfx.h"
#include "game/battle/camera.h"
#include "game/battle/combat.h"
#include "game/battle/enemy_modules.h"
#include "game/battle/enemy_support.h"
#include "game/battle/killer.h"
#include "game/battle/render.h"
#include "game/battle/shot.h"
#include "game/battle/spell_fx.h"
#include "game/battle/spell_modules.h"
#include "game/battle/spellcast.h"
#include "game/battle/sprite.h"
#include "game/battle/unit.h"
#include "game/boot/rng.h"
#include "game/title/new_game.h"
#include "runtime/libgte.h"

namespace ss::game::battle {
namespace {

// The SpellCast's fields this module uses: s20 the lightning on the clouds (0xc0 at a bolt,
// -8 a tick), s24 the step's ticks, s2c the clouds' brightness (0x100, fading in step 4),
// s30 the whirlwind's turn, s34 the dome's turn, s38 the clouds' scroll (0..0x4af), s3c the
// step-1 voice played; obj the data's sprite set (anim 1 the clouds, 0 the dome; obj.pos
// keeps the caster's place while he is in the sky), obj2 the caster's set (anim 0x1c, the
// whirlwind).

// The caster's height in the sky (-5000 units).
constexpr s32 kSkyY = static_cast<s32>(0xFEC78000);

// Step 5's three spark rings (block tables, one entry per ring).
constexpr u32 kRingY = 0x8008EA3C;      // s16: the ring's height (units)
constexpr u32 kRingWidth = 0x8008EA44;  // s16: quad width (+0x118)
constexpr u32 kRingRadius = 0x8008EA4C; // s16: radius (+0x138)
constexpr u32 kRingGrowth = 0x8008EA54; // u8: growth a tick (+0x130)
constexpr u32 kRingClut = 0x8008EA58;   // u8: CLUT offset (+0x12c)

u32 rng15() { return static_cast<u32>(rng_next() & 0x7FFF); }  // rng_next15 0x800103b4

// The caster's aura, the BObj at Cyril's +0x270 (enemy_mod_cyril.cpp). (port) Any other
// caster (the console would read whatever lies at its +0x270) gets a BObj of this file per
// cast, given the pool and the caster's set each frame as Cyril's animate (0x80088164)
// does; erased when the cast finishes.
std::unordered_map<const SpellCast*, BObj> g_aura;

BObj& caster_aura(SpellCast* sc) {
  if (BObj* a = enemy_cyril_aura(sc->caster)) return *a;
  BObj& a = g_aura[sc];
  a.pool = bg().prim_pool_a;
  bobj_share_sprite(sc->caster, &a);
  return a;
}

// (port) A sinking spark's ribbon trail (Shot +0x134 holds the pointer on the console,
// allocated with __builtin_new 0x8001fd94; ribbon_trail_create allocates it here). The
// trail's back pointer is the map entry, so a trail that ends on its own clears it.
std::unordered_map<const Shot*, RibbonTrail*> g_trail;

void trail_release(const Shot* s) {
  const auto it = g_trail.find(s);
  if (it == g_trail.end()) return;
  if (it->second != nullptr) it->second->back = nullptr;
  g_trail.erase(it);
}

// 0x8008e628: a spark (anim 0x19 of the caster's set) on the sphere of radius +0x130 round
// +0x118 at the angles +0x128 / +0x12c; projected and drawn.
void spark_place(Shot* s) {
  BattleGlobals& g = bg();
  s->anim = 0x19;
  const s32 ca = bcos(static_cast<u32>(s->p128));
  const s32 sa = bsin(static_cast<u32>(s->p128));
  const s32 dz = sa * s->p130;
  const s32 cb = bcos(static_cast<u32>(s->p12c));
  const s32 dx = ((ca * cb) >> 12) * s->p130;
  const s32 sb = bsin(static_cast<u32>(s->p12c));
  const s32 dy = ((-ca * sb) >> 12) * s->p130;
  s->pos[2] = s->point[2] + dz;
  s->pos[0] = s->point[0] + dx;
  s->pos[1] = s->point[1] + dy;
  bobj_project(s);
  bobj_anim_draw(s, g.flags & 0xCu);
}

// 0x8008e55c: step 2's spark circling the caster in the sky, its sphere shrinking from
// 0x3c0 by 4 a tick; it dies below 0x20.
void orbiting_spark(Shot* s) {
  BattleGlobals& g = bg();
  spark_place(s);
  if ((g.flags & 0xC) != 0) return;
  const s32 t = g.frame_ticks;
  s->p130 -= t * 4;
  s->p128 = (s->p128 + (t * 0x6000) / 360) & 0xFFF;
  s->p12c = (s->p12c + (t * 0x4000) / 360) & 0xFFF;
  if (s->p130 < 0x20) shot_kill(s);
}

// 0x8008e718: step 3's spark (CLUT +5, size 0x28) with a ribbon trail (16, 0x80c050): its
// centre sinks 16 units a tick from the caster in the sky; above 1500 units up its sphere
// grows from 0x20 to 0x3c0 (4 a tick), below it shrinks by 12; past 500 units up the trail
// ends and the spark dies.
void sinking_spark(Shot* s) {
  BattleGlobals& g = bg();
  s->clut_offset = 5;
  s->size[0] = 0x28;
  s->size[1] = 0x28;
  s->size[2] = 0x28;
  spark_place(s);
  RibbonTrail*& trail = g_trail[s];
  if (trail == nullptr) {
    trail = ribbon_trail_create(s, 0x10, 0x80C050, 0x80C050);
    if (trail != nullptr) {
      trail->no_lift = 1;
      trail->back = &trail;  // (port: the console leaves +0x10 clear)
    }
  }
  if ((g.flags & 0xC) != 0) return;
  const s32 t = g.frame_ticks;
  s->p128 = (s->p128 + (t * 0x4000) / 360) & 0xFFF;
  s->p12c = (s->p12c + (t * 0x2000) / 360) & 0xFFF;
  if (-0x5DC000 < s->point[1]) {
    s->p130 -= t * 12;
  } else {
    s->p130 += t * 4;
    if (0x3C0 < s->p130) s->p130 = 0x3C0;
  }
  s->point[1] += t * 0x10000;
  if (-0x1F4000 < s->point[1]) {
    if (trail != nullptr) trail->stop = 1;
    trail_release(s);
    shot_kill(s);
  }
}

// 0x8008e8c0: step 4's spark (anim 0x13 at double size) circling the field's centre at
// radius +0x130 (from 0x500, -12 a tick), sinking from about 944 units up to the ground as
// the circle closes ((0x1b00 - 3 r)^2 / 0x9000 - 1200); it dies below 0x40.
void ground_spark(Shot* s) {
  BattleGlobals& g = bg();
  s->anim = 0x13;
  s->scale_y = 0x2000;
  s->scale_x = 0x2000;
  const s32 dx = bcos(static_cast<u32>(s->p128)) * s->p130;
  const s32 dz = bsin(static_cast<u32>(s->p128)) * s->p130;
  const s32 h = 0x1B00 - s->p130 * 3;
  s->pos[2] = s->point[2] + dz;
  s->pos[0] = s->point[0] + dx;
  s->pos[1] = s->point[1] + ((h * h) / 0x9000 - 0x4B0) * 0x1000;
  bobj_project(s);
  bobj_anim_draw(s, g.flags & 0xCu);
  if ((g.flags & 0xC) != 0) return;
  s->p128 = (s->p128 + g.frame_ticks * 5) & 0xFFF;
  s->p130 -= g.frame_ticks * 12;
  if (s->p130 < 0x40) shot_kill(s);
}

// Steps 0-5: the caster with a shadow as wide as his sprite, and his aura (anim 8) over
// him (his own update draws neither while the spell's first steps run).
void draw_caster(SpellCast* sc) {
  BattleGlobals& g = bg();
  Unit* c = sc->caster;
  bobj_project(c);
  bobj_anim_draw(c, g.flags & 0xCu);
  s32 r = c->ext_x1 < 0 ? -c->ext_x1 : c->ext_x1;
  const s32 r0 = c->ext_x0 < 0 ? -c->ext_x0 : c->ext_x0;
  if (r < r0) r = r0;
  r = r * 4 + 0x20;
  bobj_draw_shadow(c, -r, r, -r, r, 0xFFFFFFFF);
  BObj& aura = caster_aura(sc);
  aura.anim = 8;
  aura.pos[0] = c->pos[0];
  aura.pos[1] = c->pos[1];
  aura.pos[2] = c->pos[2];
  aura.facing[0] = c->facing[0];
  aura.facing[1] = c->facing[1];
  aura.facing[2] = c->facing[2];
  aura.scale_x = c->scale_x;
  aura.scale_y = c->scale_y;
  bobj_project(&aura);
  bobj_anim_draw(&aura, g.flags & 0xCu);
}

// From step 1 while s2c is up: two walls of cloud (anim 1 of the data's set, opaque
// gouraud quads) 2000 units apart in x, each 12 rows x 6 slanted tiles of 1200 units from
// 17400 units up, scrolling down by s38; row colours scale with s2c and the lightning s20,
// brighter downward. The scroll runs while drawn (even in a pause).
void draw_clouds(SpellCast* sc) {
  BattleGlobals& g = bg();
  BObj* o = &sc->obj;
  o->anim = 1;
  bobj_anim_draw(o, (g.flags & 0xCu) | 0x9800u);
  const FrameUv& f = frame_uv();
  const s32 uv[4] = {f.u0, f.v0, f.u1, f.v1};
  const u16 tpage = f.tpage, clut = f.clut;
  const u32 code = bobj_poly_tag(o, tpage);
  s32 x_wall = 0;
  for (int wall = 0; wall < 2; ++wall, x_wall += 2000) {
    for (int row = 0; row < 12; ++row) {
      const u32 top = scale_colour(code, ((sc->s20 + (row * 0xE0) / 12 + 0x20) * sc->s2c) / 0x100);
      const u32 low = scale_colour(code, ((sc->s20 + ((row + 1) * 0xE0) / 12 + 0x20) * sc->s2c) / 0x100);
      const u32 rgb[4] = {top | 0x3C000000u, low, top | 0x3C000000u, low};
      const s32 y = row * 0x4B0 + sc->s38 - 0x43F8;
      for (int i = 0; i < 6; ++i) {
        const s32 x = x_wall - 1000 + i * 200;
        const s32 z = -0xE10 + i * 0x4B0;
        const psyq::SVECTOR v[4] = {
            {static_cast<s16>(x - 0x258), static_cast<s16>(y), static_cast<s16>(z), 0},
            {static_cast<s16>(x - 0x258), static_cast<s16>(y + 0x4B0), static_cast<s16>(z), 0},
            {static_cast<s16>(x - 0x190), static_cast<s16>(y), static_cast<s16>(z + 0x4B0), 0},
            {static_cast<s16>(x - 0x190), static_cast<s16>(y + 0x4B0), static_cast<s16>(z + 0x4B0), 0},
        };
        draw_quad_gt4(uv, v, clut, tpage, 0, rgb);
      }
    }
  }
  sc->s38 = (sc->s38 + g.frame_ticks * 0x40) % 0x4B0;
}

// Steps 4-5: a whirlwind round the field's centre, 12 bands of 12 one-quad patches of anim
// 0x1c of the caster's set (obj2); band b runs from radius 0xc0 b - 0x80 at height
// (36 - 3b)^2 - 1200 to 0xc0 b + 0x40 at (33 - 3b)^2 - 1200, turned by s30.
void draw_whirlwind(SpellCast* sc) {
  BattleGlobals& g = bg();
  BObj* o = &sc->obj2;
  o->anim = 0x1C;
  bobj_anim_draw(o, (g.flags & 0xCu) | 0x9800u);
  const FrameUv& f = frame_uv();
  const s32 uv[4] = {f.u0, f.v0, f.u1, f.v1};
  const u16 tpage = f.tpage, clut = f.clut;
  const u32 code = bobj_poly_tag(o, tpage);
  s32 c[4][4] = {};
  s32 r_in = -0x80, r_out = 0x40;
  for (int band = 0, k = 0; band < 12; ++band, k += 3, r_in += 0xC0, r_out += 0xC0) {
    const s32 r[2] = {r_in, r_out};
    const s32 a = 0x24 - k, b = 0x21 - k;
    c[0][1] = c[1][1] = (a * a - 0x4B0) * 0x1000;
    c[2][1] = c[3][1] = (b * b - 0x4B0) * 0x1000;
    for (int seg = 0; seg < 12; ++seg) {
      const s32 ang[2] = {(seg << 12) / 12 + sc->s30, ((seg + 1) << 12) / 12 + sc->s30};
      for (int i = 0; i < 4; ++i) {
        c[i][0] = bcos(static_cast<u32>(ang[i & 1])) * r[i >> 1];
        c[i][2] = bsin(static_cast<u32>(ang[i & 1])) * r[i >> 1];
      }
      draw_textured_patch(uv, c, clut, tpage, 0x10, code, 1);
    }
  }
}

// From step 6: anim 0 of the data's set as a sphere of 10 x 10 one-quad patches (radius
// 0xaf0 round a point 2000 units up), then a funnel of 10 rings x 10 patches at the centre
// (ring n from radius 0x80 n at height 100 + 30 (10 - n)^4 / 100 to 0x80 (n + 1) at
// 100 + 30 (9 - n)^4 / 100, the outer edge 0x88 further round), turned by s34.
void draw_storm(SpellCast* sc) {
  BattleGlobals& g = bg();
  BObj* o = &sc->obj;
  o->anim = 0;
  bobj_anim_draw(o, (g.flags & 0xCu) | 0x9800u);
  const FrameUv& f = frame_uv();
  const s32 uv[4] = {f.u0, f.v0, f.u1, f.v1};
  const u16 tpage = f.tpage, clut = f.clut;
  const u32 code = bobj_poly_tag(o, tpage);
  s32 c[4][4] = {};
  for (int ring = 0; ring < 10; ++ring) {
    const s32 pitch_a = (ring << 11) / 10 - 0x400;
    const s32 pitch_b = ((ring + 1) * 0x800) / 10 - 0x400;
    for (int seg = 0; seg < 10; ++seg) {
      const s32 cpa = bcos(static_cast<u32>(pitch_a)), spa = bsin(static_cast<u32>(pitch_a));
      const s32 yaw_a = (seg << 12) / 10;
      const s32 cya = bcos(static_cast<u32>(yaw_a)), sya = bsin(static_cast<u32>(yaw_a));
      const s32 cpb = bcos(static_cast<u32>(pitch_b)), spb = bsin(static_cast<u32>(pitch_b));
      const s32 yaw_b = ((seg + 1) << 12) / 10;
      const s32 cyb = bcos(static_cast<u32>(yaw_b)), syb = bsin(static_cast<u32>(yaw_b));
      c[0][0] = ((cpa * cya) >> 12) * 0xAF0;
      c[0][1] = -spa * 0xAF0 - 0x7D0000;
      c[0][2] = ((cpa * sya) >> 12) * 0xAF0;
      c[1][0] = ((cpa * cyb) >> 12) * 0xAF0;
      c[1][1] = c[0][1];
      c[1][2] = ((cpa * syb) >> 12) * 0xAF0;
      c[2][0] = ((cpb * cya) >> 12) * 0xAF0;
      c[2][1] = -spb * 0xAF0 - 0x7D0000;
      c[2][2] = ((cpb * sya) >> 12) * 0xAF0;
      c[3][0] = ((cpb * cyb) >> 12) * 0xAF0;
      c[3][1] = c[2][1];
      c[3][2] = ((cpb * syb) >> 12) * 0xAF0;
      draw_textured_patch(uv, c, clut, tpage, 8, code, 1);
    }
  }
  for (int n = 0; n < 10; ++n) {
    const s32 a = 10 - n, b = 9 - n;
    const s32 r[2] = {n * 0x80, n * 0x80 + 0x80};
    const s32 ang0 = (n * 0xC000) / 360 + sc->s34;
    const s32 ang[2] = {ang0, ang0 + 0x88};
    const s32 y_a = -0x64000 - ((a * (a * 30) * a * a) / 100) * 0x1000;
    const s32 y_b = -0x64000 - ((b * (b * 30) * b * b) / 100) * 0x1000;
    for (int seg = 0; seg < 10; ++seg) {
      for (int i = 0; i < 4; ++i) {
        const s32 t = ang[i >> 1] + ((seg + (i & 1)) * 0x24000) / 360;
        c[i][0] = bcos(static_cast<u32>(t)) * r[i >> 1];
        c[i][1] = (i >> 1) == 0 ? y_a : y_b;
        c[i][2] = bsin(static_cast<u32>(t)) * r[i >> 1];
      }
      draw_textured_patch(uv, c, clut, tpage, 8, code, 1);
    }
  }
}

// 0x8008cc00: the handler. Steps: 0 black flash, then (data loaded) sfx 0x28, the caster's
// place kept in obj.pos, the camera looks up at the sky, obj2 takes the caster's set, the
// clouds come in; 1 lightning bolts (3 sprites, 3/16 of the frames) in the clouds, a voice
// after 60 ticks, after 120 the caster (anim 5, black) is lifted into the sky; 2 he
// brightens, two wind sounds, 12 orbiting sparks; 3 after 240 ticks anim 6, a voice, three
// sounds, 12 sinking sparks, 56 burst sprites, the camera pulls back; 4 the clouds fade,
// a ground spark a frame, after 240 ticks sfx 0x29, a white flash and wait 60; 5 spark
// rings, the camera, the caster back in his place; 6 for 240 ticks the storm dome turns and
// debris sprites fly over the arena; 7 it fades; 8 waits for the sprites; 9 the hit.
void wind_of_destruction(SpellCast* sc) {
  BattleGlobals& g = bg();
  if (static_cast<u32>(sc->step) < 6) draw_caster(sc);
  if (0 < sc->step && sc->s2c != 0) draw_clouds(sc);
  if (static_cast<u32>(sc->step - 4) < 2) draw_whirlwind(sc);
  if (5 < sc->step) draw_storm(sc);
  if ((g.flags & 0xC) != 0) return;
  sc->s30 = (sc->s30 + g.frame_ticks * 5) & 0xFFF;
  if (spellcast_timer_tick(sc) != 0) return;
  sc->s20 -= g.frame_ticks * 8;
  if (sc->s20 < 0) sc->s20 = 0;
  Unit* c = sc->caster;
  switch (sc->step) {
    case 0: {
      battle_flash_set(sc, 0, 0, 0);
      if (sc->data != nullptr) return;
      bsfx_play_ui(0x28);
      sc->obj.pos[0] = c->pos[0];
      sc->obj.pos[1] = c->pos[1];
      sc->obj.pos[2] = c->pos[2];
      ++sc->step;
      const s32 sky[3] = {0, kSkyY, 0};
      camera_move_to(g.camera, sky, 0xAAA, -0x400, 0x1194, 0, 0x80, 5);
      camera_move_to(g.camera, sky, 0xAAA, -0x400, 0x1000, 0x3C, 0x80, 5);
      sc->obj2.pool = g.prim_pool_a;
      bobj_share_sprite(c, &sc->obj2);
      sc->s2c = 0x100;
      return;
    }
    case 1: {
      if (rng15() < 0x1800) {
        sc->s20 = 0xC0;
        const s32 y = rng_between_fixed(-7000, -4000);
        const s32 z = rng_between_fixed(-1500, 1500);
        s32 at[3] = {0, y, z};
        if (rng15() < 0x4000) bsfx_play_at_pos(0x52, at);
        for (int i = 0; i < 3; ++i) {
          int anim = 0x19;
          if (i == 0) anim = static_cast<int>(rng_range(3)) + 0x16;
          if (Sprite* sp = unit_play_anim(c, anim)) {
            sprite_set_pos_v(sp, at);
            sprite_set_fade(sp, 0x80, -2, 8);
            const s32 scale = i == 2 ? 12000 : 0x2000;
            sp->scale_y = scale;
            sp->scale_x = scale;
          }
          if (i == 1) at[0] = static_cast<s32>(0xFFCE0000);  // -800 units
          // (looks like an original bug: x = 8000 units is set after the last bolt sprite and
          // never used; the -800 above moves only the third one, so the offsets were
          // probably meant for sprites 1 and 2.)
          if (i == 2) at[0] = 0x1F40000;
        }
      }
      sc->s24 += g.frame_ticks;
      if (0x3C < sc->s24 && sc->s3c == 0) {
        sc->s3c = 1;
        voice_play_entry(static_cast<int>(rng_range(4)) + 0x4DA, nullptr, -1);
      }
      if (sc->s24 < 0x79) return;
      ++sc->step;
      c->anim = 5;
      c->colour_mode = 2;
      c->rgb[0] = 0;
      c->pos[0] = 0;
      c->pos[1] = kSkyY;
      c->pos[2] = 0;
      const s32 sky[3] = {0, kSkyY, 0};
      camera_move_to(g.camera, sky, 0xAAA, -0x38E, 0xA00, 0x78, 0x80, 5);
      bsfx_play_at(0x21, c);
      return;
    }
    case 2: {
      c->rgb[0] = static_cast<u8>(c->rgb[0] + g.frame_ticks * 4);
      if (c->rgb[0] < 0x81) return;
      c->rgb[0] = 0x80;
      u32 h = bsfx_play_ui(0x46);
      sound_handle_pitch_up(h, 0xF0);
      sound_handle_fade_out(h, 0x3C, 0xD2);
      h = bsfx_play_ui(0x45);
      sound_handle_pitch_up(h, 0xB4);
      sound_handle_fade_out(h, 0x1E, 0xB4);
      for (int i = 0; i < 12; ++i) {
        Shot* s = shot_spawn_callback_always(orbiting_spark);
        if (s == nullptr) continue;
        s->point[0] = c->pos[0];
        s->point[1] = c->pos[1];
        s->point[2] = c->pos[2];
        s->p130 = 0x3C0;
        s->p128 = (i << 12) / 12;
        bobj_share_sprite(c, s);
      }
      sc->s24 = 0;
      ++sc->step;
      return;
    }
    case 3: {
      sc->s24 += g.frame_ticks;
      if (sc->s24 < 0xF1) return;
      sc->s24 = 0;
      ++sc->step;
      c->anim = 6;
      voice_play_entry(static_cast<int>(rng_range(4)) + 0x4DF, nullptr, -1);
      u32 h = bsfx_play_ui(0x2D);
      sound_handle_fade_out(h, 0x78, 0x3C);
      h = bsfx_play_ui(0x2D);
      sound_handle_fade_out(h, 0x78, 600);
      h = bsfx_play_ui(0x44);
      sound_handle_fade_out(h, 0x78, 600);
      for (int i = 0; i < 12; ++i) {
        Shot* s = shot_spawn_callback_always(sinking_spark);
        if (s == nullptr) continue;
        trail_release(s);  // (port: a fresh Shot's +0x134 is 0 on the console)
        s->point[0] = c->pos[0];
        s->point[1] = c->pos[1];
        s->point[2] = c->pos[2];
        s->p130 = 0x20;
        s->p128 = (i << 12) / 12;
        bobj_share_sprite(c, s);
        if (i == 0) {
          bsfx_play_at(0x21, s);
          bsfx_play_at(0x22, s);
          bsfx_play_at(0x2A, s);
        }
      }
      const s32 at[3] = {0, static_cast<s32>(0xFFC18000), static_cast<s32>(0xFFE0C000)};
      camera_move_to(g.camera, at, 0xB1C, 0x1C7, 0x1000, 0x168, 0x80, 5);
      for (int i = 0; i < 0x38; ++i) {
        Sprite* sp = unit_play_anim(c, static_cast<int>(rng_range(4)) + 0x10);
        if (sp == nullptr) continue;
        sprite_set_pos_v(sp, c->pos);
        const Vec4 v = vec_random_around(kZeroVector, 0x18);
        sp->vel[0] = v.x;
        sp->vel[1] = v.y;
        sp->vel[2] = v.z;
        sp->extra34 = v.w;
        sp->scale_y = 0x4000;
        sp->scale_x = 0x4000;
        sprite_set_fade(sp, 0x80, -4, 8);
      }
      return;
    }
    case 4: {
      sc->s2c -= g.frame_ticks * 4;
      if (sc->s2c < 0) {
        sc->s2c = 0;
        battle_flash_set(sc, 0x38, 0x38, 0x38);
      }
      if (Shot* s = shot_spawn_callback_always(ground_spark)) {
        s->point[0] = 0;
        s->point[2] = 0;
        s->p128 = static_cast<s32>(rng_range(0x1000));
        s->p130 = 0x500;
        bobj_share_sprite(c, s);
      }
      sc->s24 += g.frame_ticks;
      if (sc->s24 < 0xF1) return;
      bsfx_play_ui(0x29);
      // (The console allocates the fade's 0x1c bytes first: __builtin_new 0x8001fd94.)
      screen_fade_create(0, 0x20, -4, 0x1E);
      spellcast_wait(sc, 0x3C);
      return;
    }
    case 5: {
      sc->s24 = 0;
      ++sc->step;
      for (u32 i = 0; i < 3; ++i) {
        const s32 at[3] = {0, spell_module_s16(sc, kRingY + 2 * i) * 0x1000, 0};
        effect_spawn_sparks_at(at, 0x14, spell_module_s16(sc, kRingWidth + 2 * i), 0,
                               spell_module_u8(sc, kRingGrowth + i), 300, spell_module_u8(sc, kRingClut + i), 3,
                               spell_module_s16(sc, kRingRadius + 2 * i), 0);
      }
      const s32 ground[3] = {0, -0x32000, 0};
      effect_spawn_sparks_at(ground, 0x14, 500, 0x148, 0x20, 300, 4, 1, 0x1E0, 0);
      camera_move_to(g.camera, kZeroVector, 0xB1C, 0x71, 0xC00, 0, 0x80, 5);
      const s32 at[3] = {0, 0, static_cast<s32>(0xFFE0C000)};
      camera_move_to(g.camera, at, 0xC71, 0xFA, 0x1800, 0x140, 0x80, 5);
      c->pos[0] = sc->obj.pos[0];
      c->pos[1] = sc->obj.pos[1];
      c->pos[2] = sc->obj.pos[2];
      return;
    }
    case 6:
      sc->s34 = (sc->s34 + g.frame_ticks * 0x10) & 0xFFF;
      sc->s24 += g.frame_ticks;
      if (0xF0 < sc->s24) {
        sc->s24 = 0;
        ++sc->step;
      }
      for (int i = 0; i < 2; ++i) {
        Sprite* sp = spellcast_sprite_spawn(sc, static_cast<int>(rng_range(2)) + 3);
        if (sp == nullptr) continue;
        s32 at[4] = {};
        stage_random_point(at, 1);
        sprite_set_pos_v(sp, at);
        sprite_set_fade(sp, 0x80, -2, 0x18);
        const s32 scale = rng_between(0x2000, 0x8000);
        sp->scale_y = scale;
        sp->scale_x = scale;
        sp->layer = 0x10;
        if (rng15() < 0x1800) bsfx_play_at(0x50, sp);
      }
      return;
    case 7:
      sc->obj.colour_mode = 2;
      sc->s34 = (sc->s34 + g.frame_ticks * 0x10) & 0xFFF;
      sc->obj.rgb[0] = static_cast<u8>(0x80 - sc->s24);
      sc->s24 += g.frame_ticks * 2;
      if (sc->s24 < 0x80) return;
      ++sc->step;
      return;
    case 8:
      if (spellcast_effects_gone(sc) == 0) return;
      ++sc->step;
      return;
    case 9: {
      Unit* list[4] = {};
      Vec4 points[8] = {};
      int n = 0;
      for (int i = 0; i < 4; ++i) {
        Unit* u = g.party_units[static_cast<size_t>(i)];
        if (unit_is_down(u)) continue;
        list[n] = u;
        // (The console copies a fourth word of stack garbage; nothing reads it.)
        points[n] = {u->pos[0], u->pos[1] + u->size[1] * -0x2000, u->pos[2], 0};
        ++n;
      }
      if (n != 0) {
        unit_attack_targets(c, n, list, points, 0x380, 9999, 8);
        c->attack_recovery = 0;
      }
      battle_flash_set(sc, 0x80, 0x80, 0x80);
      sc->finished = 1;
      ++sc->step;
      g_aura.erase(sc);
      return;
    }
    default: return;
  }
}

}  // namespace

void spell_mod_wind_of_destruction(SpellCast* sc) { wind_of_destruction(sc); }

}  // namespace ss::game::battle
