// Spell code module a639d47ca2 (entries 0x917-0x91b): Explode (spell 3): energy gathers at a
// point 400 units above the field's centre, the screen flashes white and a fireball (the
// data's anim 0 on a 10 x 10 sphere) swells over the whole field, lifting the stage model's
// ground round the centre, with debris and blasts; as it fades everything of the other side
// is hit (element 4). Functions 0x8008cc00 (the handler) and 0x8008e138 (the ground height
// hook); the module's writable data 0x8008e208..0x8008e21c (the swell, the stage backup) is
// BlastState here.

#include <unordered_map>
#include <vector>

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
#include "game/battle/stage.h"
#include "game/battle/unit.h"
#include "game/battle/unit_move.h"
#include "game/boot/rng.h"
#include "runtime/libgte.h"

namespace ss::game::battle {
namespace {

// The SpellCast's fields: s20 the gathering's ticks (step 2), s24 / s28 its ring / white
// flash done, s2c the fireball's radius (units), s30 its fade (step 3), s34 the camera beat;
// obj the data's sprite set (anim 0 the fireball, the others the sparks and debris).

// The module's writable data.
struct BlastState {
  s32 near_count = 0;            // 0x8008e208
  s32 swell = 0;                 // 0x8008e20c: the fireball's radius, then dying away
  bool backed_up = false;        // 0x8008e214 != 0
  std::vector<u8> backup;        // 0x8008e214: the stage model as it was (size 0x8008e210)
  std::vector<NearVertex> near;  // 0x8008e218 (0xf000 bytes on the console)
};
std::unordered_map<const SpellCast*, BlastState> g_state;

// g_stage_height_fn 0x8006fe04 (spell_fx.h) is set to ground_swell while the ground swells;
// g_swell_cast is the cast whose swell it reads.
const SpellCast* g_swell_cast = nullptr;

u16 rd16(const std::vector<u8>& b, size_t at) { return static_cast<u16>(b[at] | b[at + 1] << 8); }

// The ground's lift at distance d / 8 from the centre: swell - (d / 8)^2 / 256 + 0x40 inside
// 0x80, then swell * 0x80 / (d / 8), out to twice the swell.
s32 swell_lift(s32 swell, s32 d8) {
  if (!(d8 < swell << 1)) return 0;
  if (d8 < 0x80) return swell - ((d8 * d8) >> 8) + 0x40;
  return (swell << 7) / d8;
}

// 0x8008e138: the stage height hook (x, z in units): the lift there (y is not used).
s32 ground_swell(s32 x, s32, s32 z) {
  const auto it = g_state.find(g_swell_cast);
  const s32 swell = it != g_state.end() ? it->second.swell : 0;
  const u32 xx = static_cast<u32>(x) * static_cast<u32>(x), zz = static_cast<u32>(z) * static_cast<u32>(z);
  const s32 d8 = square_root0(static_cast<s32>(xx + zz)) >> 3;
  return swell_lift(swell, d8) << 12;
}

// Step 3: the fireball, anim 0's cel on 10 x 10 one-quad patches of a sphere of radius r
// round centre.
void draw_fireball(SpellCast* sc, const s32 centre[3], s32 r) {
  BObj* o = &sc->obj;
  o->anim = 0;
  bobj_anim_draw(o, (bg().flags & 0xC) | 0x9800);
  const FrameUv& f = frame_uv();
  const s32 uv[4] = {f.u0, f.v0, f.u1, f.v1};
  const u16 tpage = f.tpage, clut = f.clut;
  const u32 code = bobj_poly_tag(o, tpage);
  s32 lat_next = 0x800;
  for (int i = 0; i < 10; ++i, lat_next += 0x800) {
    for (int j = 0; j < 10;) {
      const s32 lat0 = (i << 11) / 10 - 0x400;
      const s32 c0 = bcos(static_cast<u32>(lat0)), s0 = bsin(static_cast<u32>(lat0));
      const s32 lon0 = (j << 12) / 10;
      const s32 cl0 = bcos(static_cast<u32>(lon0)), sl0 = bsin(static_cast<u32>(lon0));
      const s32 lat1 = lat_next / 10 - 0x400;
      const s32 c1 = bcos(static_cast<u32>(lat1)), s1 = bsin(static_cast<u32>(lat1));
      ++j;
      const s32 lon1 = (j << 12) / 10;
      const s32 cl1 = bcos(static_cast<u32>(lon1)), sl1 = bsin(static_cast<u32>(lon1));
      const s32 y0 = -s0 * r + centre[1], y1 = -s1 * r + centre[1];
      const s32 c[4][4] = {{((c0 * cl0) >> 12) * r + centre[0], y0, ((c0 * sl0) >> 12) * r + centre[2], 0},
                           {((c0 * cl1) >> 12) * r + centre[0], y0, ((c0 * sl1) >> 12) * r + centre[2], 0},
                           {((c1 * cl0) >> 12) * r + centre[0], y1, ((c1 * sl0) >> 12) * r + centre[2], 0},
                           {((c1 * cl1) >> 12) * r + centre[0], y1, ((c1 * sl1) >> 12) * r + centre[2], 0}};
      draw_textured_patch(uv, c, clut, tpage, 8, code, 1);
    }
  }
}

void explode(SpellCast* sc) {
  BattleGlobals& g = bg();
  Unit* c = sc->caster;
  Unit* t = sc->target;
  // The module's data is fresh at each cast (it is unpacked with the cast).
  if (sc->step == 0) {
    BlastState fresh;
    fresh.near_count = spell_module_s32(sc, 0x8008E208);
    fresh.swell = spell_module_s32(sc, 0x8008E20C);
    g_state[sc] = std::move(fresh);
  }
  BlastState& st = g_state[sc];
  sc->centre[0] = t->pos[0];
  sc->centre[1] = t->pos[1] + t->size[1] * -0x2000;
  sc->centre[2] = t->pos[2];
  sc->centre[3] = 0;
  sc->centre[1] = bobj_ground_height(t);
  sc->eff_power = c->st()->intel[2] * 4 + sc->power * 2 + 0x960;
  const s32 centre[4] = {0, -0x190000, 0, 0};
  const s32 r = sc->s2c;
  if (sc->step == 3) {
    st.swell = r;
    draw_fireball(sc, centre, r);
  } else if (0 < st.swell) {
    st.swell -= g.frame_ticks << 5;
    if (st.swell < 0) st.swell = 0;
  }
  if ((g.flags & 4) != 0) return;
  // The swell lifts the stage model's vertices near the centre (from the backup).
  if (!st.backed_up || st.swell == 0) {
    g_stage_height_fn = nullptr;
  } else {
    g_stage_height_fn = ground_swell;
    g_swell_cast = sc;
    std::vector<u8>& model = stage().blocks[0];
    for (int i = 0; i < st.near_count; ++i) {
      const NearVertex& v = st.near[static_cast<size_t>(i)];
      const size_t at = static_cast<size_t>(v.offset) * 4 + 2;
      const s32 d8 = v.dist[0] >> 3;
      const s16 y = static_cast<s16>(static_cast<s16>(rd16(st.backup, at)) + swell_lift(st.swell, d8));
      model[at] = static_cast<u8>(y);
      model[at + 1] = static_cast<u8>(static_cast<u16>(y) >> 8);
    }
  }
  if (spellcast_timer_tick(sc) != 0) return;
  switch (sc->step) {
    case 0:
      spellcast_dim_begin(sc, c->is_enemy);
      battle_flash_set(sc, 0x38, 0x38, 0x38);
      ++sc->step;
      return;
    case 1: {
      // The data loaded: camera, the gathering sounds, the stage model backed up and its
      // vertices near the centre listed.
      if (sc->data != nullptr) return;
      if (spellcast_leads_camera(sc)) {
        camera_reset_mode(g.camera);
        camera_move_to(g.camera, kZeroVector, 0xC00, 0x71, 0x1000, 0x3C, 0x80, 5);
      }
      ++sc->step;
      u32 h = static_cast<u32>(bsfx_play_at_pos(0x40, centre));
      sound_handle_fade_out(h, 0x1E, 0xA0);
      sound_handle_pitch_up(h, 0xA0);
      h = static_cast<u32>(bsfx_play_at_pos(0x45, centre));
      sound_handle_fade_out(h, 0x1E, 0xA0);
      sound_handle_pitch_up(h, 0x140);
      if (!stage().blocks.empty()) {  // (port: the console always has a stage model)
        const std::vector<u8>& model = stage().blocks[0];
        st.backup = model;
        st.backed_up = true;
        const psyq::SVECTOR origin{0, 0, 0, 0};
        st.near_count = model_find_vertices_near(st.near, model, &origin, 1);
      }
      return;
    }
    case 2: {
      // Gathering: beams and streaks into the point, sparks (120 ticks), a ring, the white
      // flash (155), then (past 160) the burst.
      for (int i = 0; i < g.frame_ticks; ++i) {
        const Vec4 from = vec_random_around(centre, 0x600);
        const s32 width = rng_range(2) != 0 ? 0xE : 0xC;
        effect_spawn_beam_at(from.data(), centre, width, 0xF0, 0x30, 0x6000, 0x400, 0);
      }
      const Vec4 from = vec_random_around(centre, 0x600);
      particle_spawn_homing_line(from.data(), centre, 0x1040, 0x40FF, 0xA0, 0x6000, 0x400);
      const Vec4 from2 = vec_random_around(centre, 0x400);
      particle_spawn_homing_line(from2.data(), centre, 0x3040, 0xC0FF, 0x20, 0x8000, 0x800);
      if (sc->s20 < 0x78) {
        const s32 a = static_cast<s32>(rng_range(0x1000));
        const s32 b = static_cast<s32>(rng_range(0x1000));
        const s32 da = rng_between(-30, 30);
        const s32 db = rng_between(-30, 30);
        particle_spawn_shard(centre, a, b, sc->s20 * 8 + 0xF0, da, db, 0xFF0080, 0x28);
      }
      sc->s20 += g.frame_ticks;
      if (0x78 < sc->s20 && sc->s24 == 0) {
        sc->s24 = 1;
        particle_spawn_ring(centre, 0x10, 0x100, -0x48, 0x40, 0x40FF, 0x2060, 0x500, 0);
        particle_spawn_ring(centre, 0x10, 0x100, -0x48, 0x40, 0x40FF, 0x2060, 0x500, 0x400);
        bsfx_play_at_pos(0x21, centre);
      }
      if (0x9A < sc->s20 && sc->s28 == 0) {
        sc->s28 = 1;
        screen_fade_create(0xFFFFFF, 0x20, 4, 0x1E);
      }
      if (!(0xA0 < sc->s20)) return;
      screen_shake2(8, 8, 4, 4, 0x78);
      bsfx_play_ui(0x50);
      bsfx_play_ui(0x52);
      bsfx_play_ui(0x21);
      bsfx_play_ui(0x22);
      sound_handle_fade_out(static_cast<u32>(bsfx_play_at_pos(0x2D, centre)), 0x78, 800);
      sound_handle_fade_out(static_cast<u32>(bsfx_play_at_pos(0x44, centre)), 0x78, 800);
      if (spellcast_leads_camera(sc)) {
        const s32 yaw = rng_range(2) != 0 ? 0x8E3 : 0x471;
        camera_move_to(g.camera, kZeroVector, yaw, 0x1C7, 0x2000, 0, 0x80, 5);
      }
      battle_flash_set(sc, 0xA0, 0x38, 0x38);
      ++sc->step;
      s32 n = sc->power / 6 + 0x20;
      if (0x40 < n) n = 0x40;
      for (int i = 0; i < n; ++i) {
        if (Sprite* sp = spellcast_sprite_spawn(sc, 2)) {
          s32 p[4] = {};
          stage_random_point(p, 1);
          sprite_set_pos_v(sp, p);
          sprite_set_fade(sp, 0x80, -1, 0x2D0);
          const s32 scale = rng_between(0x1000, 12000);
          sp->scale_y = scale;
          sp->scale_x = scale;
          sp->size[2] = static_cast<s16>(rng_between(8, 0x18));
          sp->layer = 2;
        }
        sc->s2c = 0x80;
      }
      particle_spawn_ring(centre, 0x10, 0x100, 0x60, 0x78, 0x20, 0x60FF, 0, 0);
      particle_spawn_ring(centre, 0x10, 0x100, 0x60, 0x78, 0x102040, 0x80C0FF, 0, 0x400);
      particle_spawn_ring(centre, 0x10, 0x100, 0x60, 0x78, 0x102040, 0x80C0FF, 0, 0x200);
      particle_spawn_ring(centre, 0x10, 0x100, 0x60, 0x78, 0x102040, 0x80C0FF, 0, 0x600);
      effect_spawn_sparks_at(centre, 0x18, 800, 0, 0x28, 0x1E0, 0, 3, 0, 0);
      if (Shot* r2 = effect_spawn_sparks_at(centre, 0x18, 800, 0, 0x28, 0x1E0, 2, 3, 0, 0)) r2->vel[2] = 0x400;
      effect_spawn_sparks_at(centre, 0x10, 0, 0x28, 0x26, 0x1E0, 0xE, 4, 0, 0);
      effect_spawn_t31_at(centre, 9, 9, 0x38, 0x168, 0x2080FF, 0x40, 0x18, 0x50, 0);
      return;
    }
    case 3: {
      // The fireball swells 4 a tick with flames, debris and blasts round the field; past
      // 0xaf0 it fades over 128 ticks and then hits; the camera moves at 1401, 1901, 2301.
      sc->s2c += g.frame_ticks * 4;
      for (int i = 0; i < g.frame_ticks; ++i) {
        Sprite* sp = spellcast_sprite_spawn(sc, 0xB);
        if (sp == nullptr) continue;
        const Vec4 p = vec_random_around(centre, static_cast<s32>(rng_range(0x500)));
        sprite_set_pos_v(sp, p.data());
        sprite_set_fade(sp, 0x80, -8, 0x14);
        const s32 scale = rng_between(0x2000, 0x4000);
        sp->scale_y = scale;
        sp->scale_x = scale;
        sp->layer = 6;
      }
      if (Sprite* sp = spellcast_sprite_spawn(sc, static_cast<int>(rng_range(7)) + 3)) {
        const Vec4 v = vec_random_around(kZeroVector, rng_between(4, 0x30));
        sp->vel[0] = v.x, sp->vel[1] = v.y, sp->vel[2] = v.z, sp->extra34 = v.w;
        sprite_set_pos_v(sp, centre);
        sprite_set_fade(sp, 0x80, -2, 0x20);
        const s32 scale = rng_between(0x2000, 0x4000);
        sp->scale_y = scale;
        sp->scale_x = scale;
        sp->layer = 6;
      }
      if (static_cast<u32>(rng_next() & 0x7FFF) < static_cast<u32>(sc->power * 5 + 0x1800)) {
        screen_shake2(4, 3, 4, 6, 0x3C);
        s32 at[4] = {};
        stage_random_point(at, 1);
        at[1] = rng_between_fixed(-1000, -100);
        bsfx_play_at_pos(0x50, at);
        for (int i = 0; i < 8; ++i) {
          Sprite* sp = spellcast_sprite_spawn(sc, 10);
          if (sp == nullptr) continue;
          const Vec4 p = vec_random_around(at, static_cast<s32>(rng_range(0x230)));
          sprite_set_pos_v(sp, p.data());
          sprite_set_fade(sp, 0x80, -4, 0x1E);
          const s32 scale = rng_between(0x2000, 20000);
          sp->scale_y = scale;
          sp->scale_x = scale;
          sp->layer = 6;
        }
      }
      if (0xAF0 < sc->s2c) {
        sc->obj.colour_mode = 2;
        sc->s30 += g.frame_ticks;
        sc->obj.rgb[0] = static_cast<u8>(-0x80 - sc->s30);
        if (0x7F < sc->s30) {
          ++sc->step;
          if (1 < static_cast<u8>(sc->combo - 1)) {
            if (sc->combo == 4) sc->eff_power += spellcast_slot(sc->slot ^ 1)->eff_power;
            // ORIGINAL BUG (original_bugs.md S12, held): the side is the caster's own flag,
            // not flipped for combo 3 as the resident hit functions do (spell_hit_rect). The
            // fix changes who is hit: held (SS_HELD_FIXES=S12 turns it on).
            int side = c->is_enemy;
            if (sc->combo == 3 && held_fix_enabled("S12")) side ^= 1;
            spell_damage_area(sc, stage().bounds, sc->eff_power, 4, 0x10, side);
          }
          battle_flash_set(sc, 0x80, 0x80, 0x80);
          spellcast_dim_end(sc);
          spell_landed_flag() = 1;
        }
      }
      switch (sc->s34) {
        case 0: {
          if (sc->s2c < 0x579) return;
          if (spellcast_leads_camera(sc)) {
            const s32 yaw = rng_range(2) != 0 ? 0x2AA : 0x955;
            camera_move_to(g.camera, kZeroVector, yaw, 0xE3, 0x1400, 0, 0x80, 5);
            camera_move_to(g.camera, kZeroVector, yaw, 0x71, 0x1800, 0x5A, 0x80, 5);
          }
          break;
        }
        case 1: {
          if (sc->s2c < 0x76D) return;
          if (spellcast_leads_camera(sc)) {
            const s32 yaw = rng_range(2) != 0 ? 0x71C : 0x8E3;
            camera_move_to(g.camera, sc->centre, yaw, 0x71, 0x800, 0, 0x80, 5);
          }
          break;
        }
        case 2: {
          if (sc->s2c < 0x8FD) return;
          if (spellcast_leads_camera(sc)) {
            const s32 yaw = rng_range(2) != 0 ? 0x9C7 : 0x638;
            camera_move_to(g.camera, kZeroVector, yaw, 0x18E, 0x1000, 0xB4, 0x80, 5);
          }
          break;
        }
        default: return;
      }
      ++sc->s34;
      return;
    }
    case 4:
      // Waits for the effects and the ground to settle.
      if (spellcast_effects_gone(sc) == 0 || st.swell != 0) return;
      ++sc->step;
      return;
    case 5:
      // The stage model restored.
      if (st.backed_up) {
        g_stage_height_fn = nullptr;
        stage().blocks[0] = st.backup;
        st.backup.clear();
        st.backed_up = false;
        st.near.clear();
      }
      sc->finished = 1;
      ++sc->step;
      g_state.erase(sc);
      return;
    default: return;
  }
}

}  // namespace

void spell_mod_explode(SpellCast* sc) { explode(sc); }

}  // namespace ss::game::battle
