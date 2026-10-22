// Spell code module cbdc2116f3 (entries 0x922-0x925): Star Flare (spell 0x1c). A star burns
// 3000 units above the field (the data's sprite set, anims 1 and 0, with the stage's lens
// flare) and drops meteors on random points of the arena (or the combo point); the screen
// flashes white, the data's second command list (its command-8 block) is loaded over the
// first, and its sprite (anim 0) shimmers over the whole screen in a wave-distorted grid,
// then everything on the other side is hit (element 0x80). Functions 0x8008cc00 (the
// handler) and 0x8008d4b0 (a meteor, a kind-0x21 callback); the jump table of the handler's
// steps at 0x8008d738. Resident 0x8007d9e0 draw_wave_grid and 0x8007fd54 voice_release are
// not in the port elsewhere and are ported here.

#include <array>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>

#include "game/battle/bsfx.h"
#include "game/battle/camera.h"
#include "game/battle/combat.h"
#include "game/battle/enemy_support.h"
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
#include "game/common/screen.h"
#include "game/title/new_game.h"
#include "runtime/libgpu.h"

namespace ss::game::battle {
namespace {

// The SpellCast's fields this module uses: s20 ticks (steps 2 and 5), s24 / s30 the
// shimmer's x / y wave phases, s2c the star's grey (step 2), s34 the shimmer's brightness
// (set to 1 in step 2 once the white flash went), s38 the meteor timer, s3c the handle of
// the sound of steps 5-8; obj the data's sprite set (the first list's star, then the
// second list's shimmer).

constexpr s32 kStarY = static_cast<s32>(0xFF448000);  // -3000 units
const s32 kUnitScale[3] = {0x1000, 0x1000, 0x1000};    // g_unit_scale 0x8006e34c

// The first command list's bytes per slot, kept when step 3 loads the second list over it:
// clips it started may still play from them (the port's SpellCast keeps only the list it
// dropped last, data_retired). Replaced by the slot's next Star Flare.
std::array<SpellData, 2> g_first_list;

s32 mul_lo(s32 a, s32 b) { return static_cast<s32>(static_cast<u32>(a) * static_cast<u32>(b)); }

// 0x8007d9e0 draw_wave_grid (#1984.0): an n x n grid of screen-space POLY_FT4 over rect
// {x0, y0, x1, y1} with the UVs of uv {u0, v0, u1, v1} spread over it, every vertex row
// shifted in x by wave_x {amp, phase, freq} (amp * sin(phase + freq * row / n) >> 24) and
// every vertex column in y by wave_y alike; all at OT entry 0x610 - ot of pool A, colour
// word code_rgb. The UV words are built as the console builds them (u | v << 8 | clut or
// tpage << 16, unmasked).
void draw_wave_grid(const s32 uv[4], const s32 rect[4], const s32 wave_x[3], const s32 wave_y[3], u16 clut,
                    u16 tpage, s32 ot, u32 code_rgb, s32 n) {
  BattleGlobals& g = bg();
  RenderContext* pool = g.prim_pool_a;
  // (screen_alloc 0x80069fd4 of n * n * 0x28 bytes at once; none: nothing is drawn.)
  const s32 bytes = mul_lo(n, n * 0x28);
  if (pool == nullptr || !arena_has_room(pool, bytes)) return;
  pool_a_bytes_used() += bytes;
  const s32 u0 = static_cast<s16>(uv[0]);  // (the console keeps uv[0] << 16 and shifts it back)
  const s32 v0 = static_cast<s16>(uv[1]);
  const s32 du = static_cast<s16>(uv[2]) - u0;
  const s32 dv = static_cast<s16>(uv[3]) - v0;
  const s32 w = rect[2] - rect[0];
  const s32 h = rect[3] - rect[1];
  const u32 clut_hi = static_cast<u32>(clut) << 16;
  const u32 tpage_hi = static_cast<u32>(tpage) << 16;
  s32 xd_low = mul_lo(wave_x[0], bsin(static_cast<u32>(wave_x[1]))) >> 24;
  s32 v_low = v0 << 8;
  for (s32 row = 0; row < n; ++row) {
    const s32 v_high = v_low;
    v_low = (mul_lo(dv, row + 1) / n + v0) << 8;
    s32 yd_next = mul_lo(wave_y[0], bsin(static_cast<u32>(wave_y[1]))) >> 24;
    const s32 xd_high = xd_low;
    xd_low = mul_lo(wave_x[0], bsin(static_cast<u32>(mul_lo(wave_x[2], row + 1) / n + wave_x[1]))) >> 24;
    const s32 y_high = mul_lo(h, row) / n + rect[1];
    const s32 y_low = mul_lo(h, row + 1) / n + rect[1];
    s32 u_next = u0;
    for (s32 col = 0; col < n; ++col) {
      const s32 yd = yd_next;
      yd_next = mul_lo(wave_y[0], bsin(static_cast<u32>(mul_lo(wave_y[2], col + 1) / n + wave_y[1]))) >> 24;
      const s32 u = u_next;
      u_next = mul_lo(du, col + 1) / n + u0;
      const s32 x_a = mul_lo(w, col) / n + rect[0];
      const s32 x_b = mul_lo(w, col + 1) / n + rect[0];
      auto* p = screen_alloc_prims<psyq::POLY_FT4>(pool->arena, 1);
      if (p == nullptr) return;
      psyq::setPolyFT4(p);
      std::memcpy(&p->r0, &code_rgb, 4);
      p->x0 = static_cast<s16>(x_a + xd_high), p->y0 = static_cast<s16>(y_high + yd);
      p->x1 = static_cast<s16>(x_b + xd_high), p->y1 = static_cast<s16>(y_high + yd_next);
      p->x2 = static_cast<s16>(x_a + xd_low), p->y2 = static_cast<s16>(y_low + yd);
      p->x3 = static_cast<s16>(x_b + xd_low), p->y3 = static_cast<s16>(y_low + yd_next);
      const u32 uv0 = static_cast<u32>(u | v_high) | clut_hi;
      const u32 uv1 = static_cast<u32>(u_next | v_high) | tpage_hi;
      const u32 uv2 = static_cast<u32>(u | v_low);
      const u32 uv3 = static_cast<u32>(u_next | v_low);
      p->u0 = static_cast<u8>(uv0), p->v0 = static_cast<u8>(uv0 >> 8), p->clut = static_cast<u16>(uv0 >> 16);
      p->u1 = static_cast<u8>(uv1), p->v1 = static_cast<u8>(uv1 >> 8), p->tpage = static_cast<u16>(uv1 >> 16);
      p->u2 = static_cast<u8>(uv2), p->v2 = static_cast<u8>(uv2 >> 8);
      p->u3 = static_cast<u8>(uv3), p->v3 = static_cast<u8>(uv3 >> 8);
      psyq::addPrim(&pool->ot_head[0x610 - ot], p);
    }
  }
}

// 0x8008d4b0: a meteor (a lit orange sphere of radius 0xa0, grey +0x64 when +0x63 is set)
// flying at +0x28 toward +0x118. Near the point with +0x10c 3 it takes a new random point
// of the arena (the module never sets +0x10c, so this never happens); once at or below the
// ground: a shake, sfx 0x50, debris, an explosion sprite (fx bank anim 0x34), a spark ring,
// and it goes.
void meteor(Shot* s) {
  BattleGlobals& g = bg();
  stage_colour_set_grey(s->colour_mode != 0 ? s->rgb[0] : 0x80);
  s32 orient[3] = {0, 0, 0};  // g_zero_vector
  lights_build_oriented(orient);
  obj_set_matrix(s->pos, orient, kUnitScale);
  draw_sphere(3, 3, 0xA0, 0x4060C0, 0xFF, 0, 2, 0x1000);
  stage_colour_restore();
  if ((g.flags & 4) != 0) return;
  const s32 d = vec_distance_fx(s->pos, s->point);
  bobj_move(s, s->vel);
  if (d < g.frame_ticks * 0x30000 && s->aux == 3) {
    s->p12c = 0;
    s->aux = 0;
    s32 to[4] = {s->point[0], s->point[1], s->point[2], 0};
    stage_random_point(to, 1);
    s->point[0] = to[0], s->point[1] = to[1], s->point[2] = to[2];
    const Vec4 v = vec_velocity_toward(s->pos, s->point, 0x1E);
    s->vel[0] = v.x, s->vel[1] = v.y, s->vel[2] = v.z;
    return;
  }
  const s32 ground = bobj_ground_height(s);
  if (s->pos[1] < ground) return;
  s->pos[1] = ground;
  screen_shake2(4, 6, 4, 6, 0xE);
  bsfx_play_at_pos(0x50, s->pos);
  const s32 spread[3] = {0x30000, 0x30000, 0x18000};
  effect_debris_burst(s->pos, spread, 2, 2, 1, 20000, 5);
  if (Sprite* sp = sprite_spawn_from(0x34, 0, g.sprite_bank_fx.get(), nullptr)) {
    sprite_set_pos_v(sp, s->pos);
    sprite_set_fade(sp, 0x80, -2, 0xC);
    const s32 scale = rng_between(12000, 0x4000);
    sp->scale_y = scale;
    sp->scale_x = scale;
    sp->layer = 3;
    sp->clut_offset = 3;
  }
  effect_spawn_sparks_at(s->pos, 0x10, 0x168, 0xF0, 0x20, 100, 0xD, 1, 0x20, 0);
  shot_kill(s);
}

// Step 2: the star: anim 1 in grey s2c, then anim 0 over it, at (0, -3000, 0), and the lens
// flare from there at full stage colour.
void draw_burning_star(SpellCast* sc) {
  BObj* o = &sc->obj;
  o->anim = 1;
  o->pos[0] = 0, o->pos[1] = kStarY, o->pos[2] = 0;
  o->colour_mode = 2;
  o->rgb[0] = static_cast<u8>(sc->s2c);
  bobj_project(o);
  bobj_anim_draw(o, 0);
  o->pos[0] = 0, o->pos[1] = kStarY, o->pos[2] = 0;
  o->anim = 0;
  o->colour_mode = 0;
  bobj_project(o);
  bobj_anim_draw(o, 0);
  stage_colour_set_grey(0x100);
  const s32 sun[3] = {0, kStarY, 0};
  lens_flare_draw(sun, 0x400, 0x102040, 0x80, 0x46);
  stage_colour_restore();
}

// Steps 5-6: the shimmer: two passes of 2 x 2 wave grids (6 x 6 quads each) of anim 0's
// cel round screen (0xa0, 0x78), then (0xa0, 0xc8), additive, brightness s34 (the second
// pass half); the phases move on.
void draw_shimmer(SpellCast* sc) {
  BattleGlobals& g = bg();
  u32 code = scale_colour(0x808080, sc->s34) | 0x2E000000;
  s32 wave_x[3] = {0x10000, sc->s24, 0x4000};
  s32 wave_y[3] = {0xC000, sc->s30, 0x2000};
  if (sc->s34 == 0) return;
  BObj* o = &sc->obj;
  o->anim = 0;
  bobj_anim_draw(o, 0x9800);
  const FrameUv& f = frame_uv();
  const s32 uv[4] = {f.u0, f.v0, f.u1, f.v1};
  s32 y = 0x78;
  for (int pass = 0; pass < 2; ++pass) {
    for (int j = 0; j < 2; ++j) {
      for (int i = 0; i < 2; ++i) {
        const s32 w = uv[2] - uv[0], h = uv[3] - uv[1];
        s32 rect[4];
        rect[0] = (i - 1) * w + 0xA0;
        rect[2] = rect[0] + w;
        rect[1] = (j - 1) * h + y;
        rect[3] = rect[1] + h;
        const AnimSet* set = o->animset;
        draw_wave_grid(uv, rect, wave_x, wave_y, set->clut[0], static_cast<u16>(set->tpage[0] | 0x20), 4, code, 6);
      }
    }
    wave_x[1] = sc->s24 + 0x400;
    y += 0x50;
    // (looks like an original bug: the second pass's y phase is s24 + 0x200 where s30 +
    // 0x200 was probably meant, as the first pass uses s30.)
    wave_y[1] = sc->s24 + 0x200;
    code = scale_colour(0x808080, sc->s34 / 2) | 0x2E000000;
  }
  if ((g.flags & 4) != 0) return;
  sc->s24 += g.frame_ticks * 0x14;
  sc->s30 += g.frame_ticks * 0x28;
}

// 0x8008cc00: the handler. Power = INT + power + 1300.
void star_flare(SpellCast* sc) {
  BattleGlobals& g = bg();
  const Unit* t = sc->target;
  sc->centre[0] = t->pos[0];
  sc->centre[1] = t->pos[1];
  sc->centre[2] = t->pos[2];
  sc->eff_power = sc->caster->st()->intel[2] + sc->power + 0x514;
  if (sc->step == 2) draw_burning_star(sc);
  if (static_cast<u32>(sc->step - 5) < 2) draw_shimmer(sc);
  if ((g.flags & 4) != 0 || spellcast_timer_tick(sc) != 0) return;
  switch (sc->step) {
    case 0:
      spellcast_dim_begin(sc, sc->caster->is_enemy);
      if (spellcast_leads_camera(sc)) {
        const s32 at[3] = {static_cast<s32>(0xFFF38000), static_cast<s32>(0xFFA00000), 0};
        camera_move_to(g.camera, at, 0xC00, -0x38E, 0x200, 0x1E, 0x100, 5);
      }
      battle_flash_set(sc, 0x10, 0x10, 0x10);
      spellcast_wait(sc, 0x14);
      return;
    case 1:
      if (sc->data != nullptr) return;
      ++sc->step;
      if (spellcast_leads_camera(sc)) {
        const s32 at[3] = {0, static_cast<s32>(0xFFA00000), 0};
        camera_move_to(g.camera, at, 0xC00, -0x400, 0x200, 0x78, 0x80, 5);
      }
      return;
    case 2: {
      // The star brightens from tick 60; at tick 104 three sounds and a white flash; a
      // meteor every max(4, 12 - power / 20) ticks; after 120 ticks, 4 more.
      sc->s20 += g.frame_ticks;
      if (0x3C < sc->s20) sc->s2c = (sc->s20 - 0x3C) * 2;
      if (0x67 < sc->s20 && sc->s34 == 0) {
        sc->s34 = 1;
        bsfx_play_ui(0x28);
        bsfx_play_ui(0x29);
        bsfx_play_ui(0x2A);
        screen_fade_create(0, 0x20, -4, 8);  // (0x8001fd94 allocates its 0x1c bytes)
      }
      sc->s38 += g.frame_ticks;
      s32 every = 0xC - sc->power / 0x14;
      if (every < 4) every = 4;
      if (every < sc->s38) {
        sc->s38 -= every;
        if (Shot* s = shot_spawn_callback_always(meteor)) {
          s32 to[4] = {s->point[0], s->point[1], s->point[2], 0};
          if (spellcast_combo_point(sc, to) == 0) stage_random_point(to, 1);
          // (The point is Shot +0x118..+0x124 on the console: +0x124 takes to[3], which a
          // kind-0x21 Shot never reads.)
          s->point[0] = to[0], s->point[1] = to[1], s->point[2] = to[2];
          const s32 from[3] = {0, static_cast<s32>(0xFF060000), 0};
          s->pos[0] = from[0], s->pos[1] = from[1], s->pos[2] = from[2];
          s->pos_w = 0;  // (the console copies a stack word it never set)
          const Vec4 v = vec_velocity_toward(from, s->point, 0x1E);
          s->vel[0] = v.x, s->vel[1] = v.y, s->vel[2] = v.z;
          bsfx_play_at(0x2C, s);
        }
      }
      if (0x78 < sc->s20) spellcast_wait(sc, 4);
      return;
    }
    case 3: {
      // The command-8 block becomes the command list (+0x54 / +0x58 = +0xc8, +0xc8 = 0) and
      // the sprite set is dropped (part count 0; the layout and directory freed, the cel and
      // texture banks freed and cleared) for the list to load a new one. (Port: the vectors
      // keep their storage, as the console's freed blocks stay readable, since the AnimSet
      // still points into them until the new list's commands 4 / 7 replace them.)
      g_first_list[sc->slot & 1] = std::move(sc->data_retired);
      if (sc->raw.empty()) {
        sc->data = nullptr;
      } else {
        sc->data = std::make_unique<std::vector<u8>>(std::move(sc->raw));
      }
      sc->raw.clear();
      sc->cmd = 0;
      sc->part = 0;
      sc->header.clear();
      sc->anims.clear();
      for (size_t k = 0; k < 8; ++k) {
        sc->frames[k].clear();
        sc->sheets[k].clear();
      }
      ++sc->step;
      return;
    }
    case 4:
      if (sc->data != nullptr) return;
      ++sc->step;
      if (spellcast_leads_camera(sc)) {
        const s32 yaw = rng_range(2) != 0 ? 0x4E3 : 0x31C;
        camera_move_to(g.camera, kZeroVector, yaw, 0x155, 0x1800, 0, 0x80, 5);
        s32 unused[4] = {};
        spellcast_combo_point(sc, unused);  // (the point is not used; a combo still draws RNG)
        camera_move_to(g.camera, sc->centre, 0xC00, 0x88, 0x12C0, 0xA0, 0x80, 5);
      }
      sc->s34 = 0x100;
      sc->s3c = static_cast<s32>(bsfx_play_ui(0x2D));
      sc->s20 = 0;
      return;
    case 5:
      sc->s20 += g.frame_ticks;
      if (sc->s20 < 0x3D) return;
      ++sc->step;
      return;
    case 6:
      sc->s34 -= g.frame_ticks * 2;
      if (-1 < sc->s34) return;
      ++sc->step;
      return;
    case 7:
      if (spellcast_effects_gone(sc) == 0) return;
      ++sc->step;
      return;
    case 8:
      sound_handle_release(static_cast<u32>(sc->s3c));
      if (sc->combo != 1 && sc->combo != 4) bsfx_play_ui(0x4B);
      spell_hit_all(sc, 0x80, 0x10);
      sc->finished = 1;
      return;
    default: return;
  }
}

}  // namespace

void spell_mod_star_flare(SpellCast* sc) { star_flare(sc); }

}  // namespace ss::game::battle
