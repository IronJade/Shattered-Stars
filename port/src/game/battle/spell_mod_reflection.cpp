// Spell code module fb4ba65cff (entries 0x91c / 0x91d): Reflection (spell 0xf): two twisted
// cylinders of the data's sprite (anim 0) turn round the target while sparks spiral up it
// trailing lines; then a burst of light streaks and spark rings, and the target gets the
// reflect status (Unit +0x11c bit 0x100; an icon rises, sfx 10; if it already had it, the
// plain icon and sfx 11). Functions 0x8008cc00 (the handler) and 0x8008d5f8 (a rising spark,
// a kind-0x21 callback); the jump table of the handler's steps at 0x8008d710. Resident
// 0x80076454 line_trail_create / 0x80076514 line_trail_destroy / 0x80076578 line_trail_update
// (#1984.0) are not in the port and are ported here.

#include <cstring>
#include <unordered_map>

#include "game/battle/bsfx.h"
#include "game/battle/camera.h"
#include "game/battle/enemy_support.h"
#include "game/battle/particle.h"
#include "game/battle/render.h"
#include "game/battle/shot.h"
#include "game/battle/spell_modules.h"
#include "game/battle/spellcast.h"
#include "game/battle/sprite.h"
#include "game/battle/stage.h"
#include "game/battle/unit.h"
#include "game/battle/unit_move.h"
#include "game/boot/rng.h"
#include "game/common/screen.h"
#include "game/title/new_game.h"
#include "psx/gte.h"
#include "runtime/libgpu.h"
#include "runtime/libgte.h"

namespace ss::game::battle {
namespace {

// The SpellCast's fields: s20 the spark ticks (step 2), s24 the cylinders' turn, s28 their
// fade (step 3), s3c the camera side picked in step 0; centre the target's feet; obj the
// data's sprite set (anim 0 the cylinders' cel).

u32 svec_xy(s32 x, s32 y) { return static_cast<u32>(static_cast<u16>(x)) | static_cast<u32>(static_cast<u16>(y)) << 16; }

// 0x29c-byte task (#1984.0, vtable 0x80080fac, priority 3): a trail of gouraud lines through
// the last 'length' positions of an object (history of 32), fading 0x100 / length a tick;
// additive. (A RibbonTrail drawn as lines, one colour.)
struct LineTrail : Task {
  u8 length = 0;                 // +0x08
  s8 head = 0;                   // +0x09: next history slot (counts down)
  u8 tpage = 0x20;               // +0x0a: the DR_MODE's semi-transparency bits
  u8 in_pause = 0;               // +0x0b: created during the 0x40 pause; keeps running
  u8 stop = 0;                   // +0x0c: the owner is done; fade out and delete
  LineTrail** back = nullptr;    // +0x10: cleared when the task goes
  u32 rgb = 0;                   // +0x14
  const BObj* source = nullptr;  // +0x18
  s32 alpha[32] = {};            // +0x1c
  s32 hist[32][4] = {};          // +0x9c: positions (x, y, z, w)

  // 0x80076454 line_trail_create.
  LineTrail(const BObj* src, u8 len, u32 colour) {
    length = len;
    head = 0;
    tpage = 0x20;
    in_pause = 0;
    stop = 0;
    source = src;
    rgb = colour;
    back = nullptr;
    if ((bg().flags & 0x40) != 0) in_pause = 1;
    task_register(this, 3);
  }
  // 0x80076514 line_trail_destroy.
  ~LineTrail() override {
    if (back != nullptr) *back = nullptr;
    task_unregister(this);
  }
  void update() override;
};

// 0x80076578 line_trail_update: ages the history and (in the fight) records the source's
// position, then draws a LINE_G2 (code 0x52) plus a DR_MODE between each pair of live
// positions, 0x10000 + the camera pitch's lift above them, at OT 0x616 - (sz0 + sz1) / 64.
void LineTrail::update() {
  BattleGlobals& g = bg();
  if (g.effects_suppressed != 0 && in_pause == 0) return;
  if (kStateResult <= g.state) {
    delete this;
    return;
  }
  const u8 paused = in_pause != 0 ? (g.flags & 0xBF) : g.flags;
  if (paused == 0) {
    int alive = 32;
    for (s32& a : alpha) {
      if (a == 0) {
        --alive;
        continue;
      }
      a -= 0x100 / length;
      if (a < 0) a = 0;
      if (a == 0) --alive;
    }
    if (stop != 0 && alive == 0) {
      delete this;
      return;
    }
    if (g.state == kStateFighting && stop == 0) {
      const int i = head;
      hist[i][0] = source->pos[0];
      hist[i][1] = source->pos[1];
      hist[i][2] = source->pos[2];
      hist[i][3] = source->pos_w;
      alpha[i] = 0x100;
      head = static_cast<s8>(head - 1);
      if (head < 0) head = 0x1F;
    }
  }
  RenderContext* pool = g.prim_pool_a;
  if (pool == nullptr || !arena_has_room(pool, length * 0x20)) return;
  ViewState& v = view();
  psx::Gte& q = psyq::gte();
  s32 lift = bsin(static_cast<u32>(v.pitch)) << 4;
  if (lift < 0) lift = 0;
  lift += 0x10000;
  s32 used = 0;
  for (int k = 0; k < length; ++k) {
    const int idx[2] = {(head + k) & 0x1F, (head + k + 1) & 0x1F};
    if (alpha[idx[0]] == 0 || alpha[idx[1]] == 0) continue;
    s16 sx[2] = {}, sy[2] = {};
    s32 sz[2] = {};
    for (int j = 0; j < 2; ++j) {
      const s32* p = hist[idx[j]];
      const s16 dx = static_cast<s16>((p[0] - v.view_eye[0]) >> 12);
      const s16 dy = static_cast<s16>((p[1] - lift - v.view_eye[1]) >> 12);
      const s16 dz = static_cast<s16>((p[2] - v.view_eye[2]) >> 12);
      psyq::SetRotMatrix(&v.render_view);
      q.write_data(psx::Gte::VXY0, svec_xy(dx, dy));
      q.write_data(psx::Gte::VZ0, static_cast<u32>(static_cast<s32>(dz)));
      q.execute(psx::Gte::kRtv0);
      psyq::MATRIX m = v.render_view;
      m.t[0] = static_cast<s32>(q.read_data(psx::Gte::MAC1));
      m.t[1] = static_cast<s32>(q.read_data(psx::Gte::MAC2));
      m.t[2] = static_cast<s32>(q.read_data(psx::Gte::MAC3));
      psyq::SetTransMatrix(&m);
      psyq::SetRotMatrix(&v.render_view);
      q.write_data(psx::Gte::VXY0, 0);
      q.write_data(psx::Gte::VZ0, 0);
      q.execute(psx::Gte::kRtps);
      const u32 sxy = q.read_data(psx::Gte::SXY2);
      sx[j] = static_cast<s16>(sxy);
      sy[j] = static_cast<s16>(sxy >> 16);
      sz[j] = static_cast<s32>(q.read_data(psx::Gte::SZ3));
    }
    const s32 otz = (sz[1] + sz[0]) >> 6;
    if (0x5FE < static_cast<u32>(otz - 7)) continue;
    auto* line = screen_alloc_prims<psyq::LINE_G2>(pool->arena, 1);
    auto* dm = screen_alloc_prims<psyq::DR_MODE>(pool->arena, 1);
    if (line == nullptr || dm == nullptr) return;
    psyq::setLineG2(line);
    const u32 c0 = scale_colour(rgb, alpha[idx[0]]) | 0x52000000u;
    const u32 c1 = scale_colour(rgb, alpha[idx[1]]) | 0x52000000u;
    std::memcpy(&line->r0, &c0, 4);
    std::memcpy(&line->r1, &c1, 4);
    line->x0 = sx[0], line->y0 = sy[0];
    line->x1 = sx[1], line->y1 = sy[1];
    psyq::P_TAG* ot = &pool->ot_head[0x616 - otz];
    psyq::addPrim(ot, line);
    psyq::SetDrawMode(dm, 0, 1, tpage, nullptr);
    psyq::addPrim(ot, dm);
    used += 0x20;
  }
  // (The console takes the arena's cursor with a 4-byte allocation and commits the rest.)
  pool_a_bytes_used() += used < 4 ? 4 : used;
}

// A spark's line trail (Shot +0x134 holds the pointer on the console).
std::unordered_map<const Shot*, LineTrail*> g_trail;

// (Port) drops the Shot's entry; a trail still running stops pointing back at it.
void forget_trail(const Shot* s) {
  const auto it = g_trail.find(s);
  if (it == g_trail.end()) return;
  if (it->second != nullptr) it->second->back = nullptr;
  g_trail.erase(it);
}

// 0x8008d5f8: an invisible spark circling +0x118 (radius +0x12c, angle +0x128 turning 0x28 a
// tick) while it rises 4 a tick, drawn only by its line trail (16, 0x00c0ff); it goes after
// 60 ticks (+0x130).
void rising_spark(Shot* s) {
  BattleGlobals& g = bg();
  if ((g.flags & 4) != 0) return;
  LineTrail*& trail = g_trail[s];
  if (trail == nullptr) {
    trail = new LineTrail(s, 0x10, 0xC0FF);
    trail->back = &trail;  // (port: the console never sets it; the map's slot is cleared if the task goes first)
  }
  const s32 r = s->p12c;
  const u32 a = static_cast<u32>(s->p128);
  s->pos[1] = s->point[1];
  s->pos[0] = s->point[0] + bcos(a) * r;
  s->p128 = (s->p128 + g.frame_ticks * 0x28) & 0xFFF;
  s->point[1] += g.frame_ticks * -0x4000;
  s->p130 += g.frame_ticks;
  s->pos[2] = s->point[2] + bsin(a) * r;
  if (0x3C < s->p130) {
    if (trail != nullptr) trail->stop = 1;
    forget_trail(s);
    shot_kill(s);
  }
}

// Steps 2-3: two cylinders round the target (radius 6 and 9 times its width, 6 rows to 1.5
// times its height, 8 patches a row, each row turned 30 degrees further), turning s24 one
// way and the other.
void draw_cylinders(SpellCast* sc) {
  BattleGlobals& g = bg();
  const Unit* t = sc->target;
  BObj* o = &sc->obj;
  for (int pass = 0; pass < 2; ++pass) {
    const s32 r = pass == 0 ? t->size[0] * 6 : t->size[0] * 9;
    o->anim = 0;
    bobj_anim_draw(o, (g.flags & 0xC) | 0x9800);
    const FrameUv& f = frame_uv();
    const s32 uv[4] = {f.u0, f.v0, f.u1, f.v1};
    const u16 tpage = f.tpage, clut = f.clut;
    const u32 code = bobj_poly_tag(o, tpage);
    for (int row = 0; row < 6; ++row) {
      const s32 h = t->size[1] * -6;
      const s32 y_top = ((h * row) / 4) << 12;
      const s32 y_low = ((h * (row + 1)) / 4) << 12;
      const s32 turn = pass == 0 ? sc->s24 : -sc->s24;
      const s32 a_top = (row * 0xC000) / 0x168 + turn;
      const s32 a_low = ((row + 1) * 0xC000) / 0x168 + turn;
      for (int k = 0; k < 8; ++k) {
        const u32 t0 = static_cast<u32>(a_top + k * 0x200), t1 = static_cast<u32>(a_top + (k + 1) * 0x200);
        const u32 l0 = static_cast<u32>(a_low + k * 0x200), l1 = static_cast<u32>(a_low + (k + 1) * 0x200);
        const s32 c[4][4] = {
            {sc->centre[0] + bcos(t0) * r, sc->centre[1] + y_top, sc->centre[2] + bsin(t0) * r, 0},
            {sc->centre[0] + bcos(t1) * r, sc->centre[1] + y_top, sc->centre[2] + bsin(t1) * r, 0},
            {sc->centre[0] + bcos(l0) * r, sc->centre[1] + y_low, sc->centre[2] + bsin(l0) * r, 0},
            {sc->centre[0] + bcos(l1) * r, sc->centre[1] + y_low, sc->centre[2] + bsin(l1) * r, 0}};
        draw_textured_patch(uv, c, clut, tpage, 8, code, 1);
      }
    }
  }
}

void reflection(SpellCast* sc) {
  BattleGlobals& g = bg();
  Unit* t = sc->target;
  sc->centre[0] = t->pos[0];
  sc->centre[1] = t->pos[1];
  sc->centre[2] = t->pos[2];
  if (static_cast<u32>(sc->step - 2) < 2) draw_cylinders(sc);
  if ((g.flags & 4) != 0) return;
  if (spellcast_timer_tick(sc) != 0) return;
  switch (sc->step) {
    case 0:
      battle_flash_set(sc, 0x40, 0x40, 0x40);
      if (spellcast_leads_camera(sc)) {
        camera_reset(g.camera);
        sc->s3c = static_cast<s32>(rng_range(2));
        camera_move_to(g.camera, sc->centre, sc->s3c != 0 ? 0xCE3 : 0xAAA, 0xE3, 0x800, 0x14, 0x80, 5);
      }
      spellcast_wait(sc, 0x14);
      return;
    case 1:
      // (The camera move here has no lead test.)
      if (sc->data != nullptr) return;
      ++sc->step;
      bsfx_play_at_pos(0x21, sc->centre);
      bsfx_play_at_pos(0x22, sc->centre);
      camera_move_to(g.camera, sc->centre, sc->s3c != 0 ? 0xAAA : 0xCE3, 0xE3, 0x800, 0x5A, 0x80, 5);
      return;
    case 2:
      // A rising spark a tick for 46 ticks; the cylinders turn.
      sc->s20 += g.frame_ticks;
      sc->s24 = (sc->s24 + (g.frame_ticks * 0x14000) / 0x168) & 0xFFF;
      for (int i = 0; i < g.frame_ticks; ++i) {
        Shot* s = shot_spawn_callback_always(rising_spark);
        if (s == nullptr) continue;
        forget_trail(s);
        s->point[0] = sc->centre[0], s->point[1] = sc->centre[1], s->point[2] = sc->centre[2];
        s->p128 = static_cast<s32>(rng_range(0x1000));
        // (The radius comes from the caster's width, though the sparks circle the target.)
        const s32 w = sc->caster->size[0];
        s->p12c = rng_between(w << 2, w << 3);
      }
      if (sc->s20 < 0x2E) return;
      ++sc->step;
      return;
    case 3:
      // The cylinders fade; at 100: sfx 0x29 / 0x2a, a spark ring, 64 streaks, two rings.
      sc->obj.colour_mode = 2;
      sc->obj.rgb[0] = static_cast<u8>(-0x80 - sc->s28);
      sc->s28 += g.frame_ticks * 2;
      sc->s24 = (sc->s24 + (g.frame_ticks * 0x14000) / 0x168) & 0xFFF;
      if (!(99 < sc->s28)) return;
      ++sc->step;
      bsfx_play_at_pos(0x29, sc->centre);
      bsfx_play_at_pos(0x2A, sc->centre);
      effect_spawn_sparks_at(sc->centre, 0xC, 0x10, 0x80, 0x12, 0x3C, 2, 0, 0, 0);
      for (int i = 0; i < 0x40; ++i) {
        const s32 from[4] = {sc->centre[0], sc->centre[1], sc->centre[2], sc->centre[3]};
        const Vec4 to = vec_random_around(sc->centre, 800);
        particle_spawn_homing_line(from, to.data(), 0x104080, 0x2080F0, 0x90, 0xA000, 0x80);
      }
      {
        const s32 at[3] = {t->pos[0], t->pos[1] + t->size[1] * -0x2000, t->pos[2]};
        effect_spawn_sparks_at(at, 0x14, 0x78, 0, 0x80, 0x2D, 2, 3, 0x80, 0);
      }
      {
        const s32 at[3] = {t->pos[0], t->pos[1] + t->size[1] * -0x2000, t->pos[2]};
        effect_spawn_sparks_at(at, 0x18, 0xF0, 0, 0x20, 0x5A, 0, 3, 0x80, 0);
      }
      return;
    case 4:
      // The status.
      battle_flash_set(sc, 0x80, 0x80, 0x80);
      ++sc->step;
      if (unit_is_down(t)) return;
      if ((t->status_bits & 0x100) == 0) {
        effect_rise_icon(t, 3);
        t->status_bits |= 0x100;
        bsfx_play_ui(10);
      } else {
        effect_rise_icon(t, 0);
        bsfx_play_ui(0xB);
      }
      return;
    case 5:
      if (shot_kind21_absent(rising_spark) == 0) return;
      sc->finished = 1;
      ++sc->step;
      return;
    default: return;
  }
}

}  // namespace

void spell_mod_reflection(SpellCast* sc) { reflection(sc); }

}  // namespace ss::game::battle
