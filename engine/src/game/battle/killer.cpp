#include "game/battle/killer.h"

#include <cstring>

#include "game/battle/bsfx.h"
#include "game/battle/camera.h"
#include "game/battle/combat.h"
#include "game/battle/render.h"
#include "game/battle/stage.h"
#include "game/common/screen.h"
#include "game/title/new_game.h"
#include "psx/gte.h"
#include "runtime/libgpu.h"
#include "runtime/libgte.h"
#include "game/battle/input.h"
#include "game/battle/shot.h"
#include "game/battle/unit_move.h"

namespace ss::game::battle {

// ---- Afterimage -------------------------------------------------------------------------

Afterimage::Afterimage(const BObj* src, int n, u32 rgb, u8 blend_mode) {
  if (8 < n) n = 8;
  count = static_cast<u8>(n);
  head = 0;
  in_pause = (bg().flags & 0x40) != 0 ? 1 : 0;
  blend = blend_mode;
  ending = 0;
  next_kind = 0;
  source = src;
  colour[0] = static_cast<u8>(rgb);
  colour[1] = static_cast<u8>(rgb >> 8);
  colour[2] = static_cast<u8>(rgb >> 16);
  back = nullptr;
  for (int i = 0; i < 8; ++i) kinds[i] = 0;
  copies = std::make_unique<BObj[]>(static_cast<size_t>(n > 0 ? n : 1));
  task_register(this, 3);
}

Afterimage::~Afterimage() {
  if (back != nullptr) *back = nullptr;
  task_unregister(this);
}

Afterimage* afterimage_create(const BObj* src, int n, u32 rgb, u8 blend_mode) {
  return new Afterimage(src, n, rgb, blend_mode);
}

void Afterimage::update() {
  BattleGlobals& g = bg();
  if (g.effects_suppressed != 0 && in_pause == 0) return;
  if (copies && (g.state < kStateOver || 0xF < g.end_countdown)) {
    // Newest first (head + 1 is the newest copy; the OT draws the last added first, so the
    // newest ends up on top).
    for (int i = 0; i < count; ++i) {
      // ORIGINAL BUG (original_bugs.md B2): the console steps two slots at a time, so with an
      // even count half the copies are drawn twice and the other half never:
      //   const int k = (head + i * 2 + 2) % count;
      const int k = (head + i + 1) % count;
      if (kinds[k] == 0) continue;
      BObj& c = copies[static_cast<size_t>(k)];
      c.blend = blend == 2 ? 0x82 : static_cast<u8>((blend != 0) << 1);
      c.colour_mode = 1;
      c.rgb[0] = static_cast<u8>((colour[0] * alpha[k]) >> 7);
      c.rgb[1] = static_cast<u8>((colour[1] * alpha[k]) >> 7);
      c.rgb[2] = static_cast<u8>((colour[2] * alpha[k]) >> 7);
      bobj_project(&c);
      bobj_anim_draw(&c, 1);
    }
    u8 frozen = g.flags;
    if (in_pause != 0) frozen = static_cast<u8>(frozen & 0xBF);
    if (frozen != 0) return;
    int alive = count;
    for (int i = 0; i < count; ++i) {
      if (kinds[i] == 0) {
        --alive;
        continue;
      }
      const int step = ending == 0 ? g.frame_ticks * -4 : -(0x80 / count);
      const int a = alpha[i] + step;
      if (a < 1) kinds[i] = 0;
      alpha[i] = static_cast<u8>(a);
      BObj& c = copies[static_cast<size_t>(i)];
      const u8 kind = kinds[i];
      if (kind == 2 || kind == 4) {
        c.scale_x += g.frame_ticks * 0x80;
        c.scale_y += g.frame_ticks * 0x80;
      }
      if (kind == 3 || kind == 4) c.roll = static_cast<s16>((c.roll + g.frame_ticks * 0x28) & 0xFFF);
    }
    if (alive != 0 || ending == 0) {
      if (kStateOver <= g.state && g.end_countdown < 0x21) return;
      if (ending != 0) return;
      // A new copy of the source in the head slot.
      kinds[head] = static_cast<u8>(next_kind + 1);
      next_kind = 0;
      BObj& c = copies[static_cast<size_t>(head)];
      c = *source;
      c.texcache_shared = 1;
      c.layer = -1;
      alpha[head] = 0x80;
      head = static_cast<s8>(head - 1);
      if (head < 0) head = static_cast<s8>(count - 1);
      return;
    }
  }
  delete this;
}

// ---- Ghost ------------------------------------------------------------------------------

Ghost::Ghost(const BObj* src, u32 rgb) {
  copy = *src;
  colour[0] = static_cast<u8>(rgb);
  colour[1] = static_cast<u8>(rgb >> 8);
  colour[2] = static_cast<u8>(rgb >> 16);
  copy.texcache_shared = 1;
  alpha = 0x80;
  task_register(this, 3);
}

Ghost::~Ghost() {
  if (back != nullptr) *back = nullptr;
  task_unregister(this);
}

Ghost* ghost_create(const BObj* src, u32 rgb) { return new Ghost(src, rgb); }

void Ghost::update() {
  BattleGlobals& g = bg();
  if (g.effects_suppressed != 0) return;
  if (g.state < kStateResult) {
    copy.blend = 2;
    copy.colour_mode = 1;
    copy.rgb[0] = static_cast<u8>((colour[0] * static_cast<u32>(alpha)) >> 7);
    copy.rgb[1] = static_cast<u8>((colour[1] * static_cast<u32>(alpha)) >> 7);
    copy.rgb[2] = static_cast<u8>((colour[2] * static_cast<u32>(alpha)) >> 7);
    bobj_project(&copy);
    const s16 frame = static_cast<s16>(bobj_anim_draw(&copy, g.flags));
    if (frame != -1) {
      if (g.flags != 0) return;
      alpha -= g.frame_ticks * 4;
      if (0 < alpha) return;
    }
  }
  delete this;
}

// ---- RibbonTrail -----------------------------------------------------------------------

RibbonTrail::RibbonTrail(const BObj* src, u8 len, u32 a, u32 b) {
  length = len;
  head = 0;
  tpage = 0x20;
  in_pause = (bg().flags & 0x40) != 0 ? 1 : 0;
  stop = 0;
  no_lift = 0;
  source = src;
  rgb_a = a;
  rgb_b = b;
  back = nullptr;
  task_register(this, 3);
}

RibbonTrail::~RibbonTrail() {
  if (back != nullptr) *back = nullptr;
  task_unregister(this);
}

RibbonTrail* ribbon_trail_create(const BObj* src, u8 len, u32 rgb_a, u32 rgb_b) {
  return new RibbonTrail(src, len, rgb_a, rgb_b);
}

namespace {

// 32-bit R3000 products and quotients (a zero divisor gives what the CPU leaves in LO).
s32 mul32(s32 a, s32 b) { return static_cast<s32>(static_cast<u32>(a) * static_cast<u32>(b)); }
s32 div_r3000(s32 n, s32 d) {
  if (d == 0) return n >= 0 ? -1 : 1;
  if (n == INT32_MIN && d == -1) return n;
  return n / d;
}
s32 abs_angle(s32 dy, s32 dx) {
  const s32 a = ratan2(dy, dx);
  return a < 0 ? -ratan2(dy, dx) : ratan2(dy, dx);
}
u32 svec_xy(s32 x, s32 y) { return static_cast<u32>(static_cast<u16>(x)) | static_cast<u32>(static_cast<u16>(y)) << 16; }

}  // namespace

void RibbonTrail::update() {
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
  const s32 w = source->size[0], h = source->size[1];
  RenderContext* pool = g.prim_pool_a;
  if (!arena_has_room(pool, length * 0x30)) return;
  ViewState& v = view();
  psx::Gte& q = psyq::gte();
  s32 lift = bsin(static_cast<u32>(v.pitch)) << 4;
  if (lift < 0) lift = 0;
  lift += 0x10000;
  s16 prev1[2] = {}, prev3[2] = {};  // the last quad's xy1 / xy3 (the next one's xy0 / xy2)
  for (int k = 0; k < length; ++k) {
    int idx[4];
    idx[0] = k > 0 ? (head + k) & 0x1F : -1;
    idx[1] = (head + k + 1) & 0x1F;
    idx[2] = (head + k + 2) & 0x1F;
    idx[3] = (head + k + 3) & 0x1F;
    if (alpha[idx[1]] == 0 || alpha[idx[2]] == 0) continue;
    s16 sx[4] = {}, sy[4] = {};
    s32 sz[4] = {};
    for (int j = 0; j < 4; ++j) {
      if (idx[j] < 0) continue;
      const s32* p = hist[idx[j]];
      const s16 dx = static_cast<s16>((p[0] - v.view_eye[0]) >> 12);
      s32 dyw = (p[1] - lift - v.view_eye[1]) >> 12;
      if (no_lift == 0) dyw -= h * 2;
      const s16 dy = static_cast<s16>(dyw);
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
    const s32 otz = (sz[1] + sz[2]) >> 6;
    if (0x5FE < static_cast<u32>(otz - 7)) continue;
    const s32 a12 = abs_angle(sy[2] - sy[1], sx[2] - sx[1]);
    s32 start = a12;
    if (k == 0) {
      if (0 <= idx[0] && alpha[idx[0]] != 0) {
        const s32 s = abs_angle(sy[1] - sy[0], sx[1] - sx[0]) + a12;
        start = (s < 0 ? s + 1 : s) >> 1;
      }
      start += 0x400;
    }
    s32 end = a12;
    if (alpha[idx[3]] != 0) {
      const s32 s = abs_angle(sy[3] - sy[2], sx[3] - sx[2]) + a12;
      end = (s < 0 ? s + 1 : s) >> 1;
    }
    end += 0x400;
    const s32 za = mul32(div_r3000(0x10000, sz[1] >> 5), v.zoom);
    const s32 zb = mul32(div_r3000(0x10000, sz[2] >> 5), v.zoom);
    const s32 s1 = mul32(w, za >> 10) >> 12, s2 = mul32(h, za >> 10) >> 12;
    const s32 e1 = mul32(w, zb >> 10) >> 12, e2 = mul32(h, zb >> 10) >> 12;
    const s32 cs = bcos(static_cast<u32>(start)), ss = bsin(static_cast<u32>(start));
    // ORIGINAL BUG (original_bugs.md B1): the console takes the start angle's cosine for the
    // far edge, which skews it:
    //   const s32 ce = bcos(static_cast<u32>(start));
    const s32 ce = bcos(static_cast<u32>(end));
    const s32 se = bsin(static_cast<u32>(end));
    auto* quad = screen_alloc_prims<psyq::POLY_G4>(pool->arena, 1);
    auto* dm = screen_alloc_prims<psyq::DR_MODE>(pool->arena, 1);
    if (quad == nullptr || dm == nullptr) return;
    psyq::setPolyG4(quad);
    const u32 lead = scale_colour(rgb_b, alpha[idx[1]]) | 0x3A000000u;
    const u32 trail = scale_colour(rgb_a, alpha[idx[2]]);
    std::memcpy(&quad->r0, &lead, 4);
    std::memcpy(&quad->r1, &lead, 3);
    std::memcpy(&quad->r2, &trail, 3);
    std::memcpy(&quad->r3, &trail, 3);
    if (k > 0) {
      quad->x0 = prev1[0], quad->y0 = prev1[1];
      quad->x2 = prev3[0], quad->y2 = prev3[1];
    } else {
      quad->x0 = static_cast<s16>(sx[1] + (mul32(cs, s1) >> 13));
      quad->y0 = static_cast<s16>(sy[1] - (mul32(ss, s2) >> 13));
      quad->x2 = static_cast<s16>(sx[1] - (mul32(cs, e1) >> 13));
      quad->y2 = static_cast<s16>(sy[1] + (mul32(ss, e2) >> 13));
    }
    quad->x1 = static_cast<s16>(sx[2] + (mul32(ce, s1) >> 13));
    quad->y1 = static_cast<s16>(sy[2] - (mul32(se, s2) >> 13));
    quad->x3 = static_cast<s16>(sx[2] - (mul32(ce, e1) >> 13));
    quad->y3 = static_cast<s16>(sy[2] + (mul32(se, e2) >> 13));
    prev1[0] = quad->x1, prev1[1] = quad->y1;
    prev3[0] = quad->x3, prev3[1] = quad->y3;
    psyq::P_TAG* ot = &pool->ot_head[0x616 - otz];
    psyq::addPrim(ot, quad);
    psyq::SetDrawMode(dm, 0, 1, tpage, nullptr);
    psyq::addPrim(ot, dm);
    pool_a_bytes_used() += 0x30;
  }
}

// ---- Jump step (module copies) ------------------------------------------------------

int pc_jump_step(PartyUnit* u, s32 gravity) {
  BattleGlobals& g = bg();
  if (g.flags != 0 || u->jumping == 0) return 0;
  s32 d[3] = {u->vel[0], 0, 0};
  if (unit_try_move_scaled(u, d, 7) == nullptr) bobj_move(u, d);
  d[0] = 0;
  d[1] = u->vel[1];
  int r;
  if (unit_try_move_scaled(u, d, 7) == nullptr) {
    bobj_move(u, d);
    u->vel[1] += gravity * g.frame_ticks;
    if (u->pos[1] <= bobj_ground_height(u)) return 0;
    u->pos[1] = bobj_ground_height(u);
    u->vel[1] = 0;
    bsfx_play_at(0x1E, u);
    r = 2;
    u->airborne = 0;
  } else {
    bsfx_play_at(0x1E, u);
    r = 1;
    u->airborne = 2;
  }
  u->jumping = 0;
  return r;
}

// ---- PartyUnit helpers -------------------------------------------------------------------

int pc_attack_anim_step(PartyUnit* u) {
  BattleGlobals& g = bg();
  const s16 frame = static_cast<s16>(u->animate(0));
  if (frame != -1 && !(kStateOver <= g.state && g.end_countdown < 0x14)) return 0;
  u->action_kind = 0x12;
  u->begin_action();
  if (u->jumping != 0) u->jumping = 0;
  u->release_effects();
  if (u->camera_focus != 0 && g.camera != nullptr) camera_set_lookat_offset(g.camera, kZeroVector);
  return 1;
}

void pc_bind_shot(PartyUnit* u, Shot* s, s16 power) {
  s->owner = u;
  s->layer = 1;
  s->power = power;
  s->target = u->target;
  bobj_share_sprite(u, s);
}

int pc_move_charge_level(const PartyUnit* u, int move) {
  const u8* md = u->move_data[move & 3];
  if (md == nullptr) return 0;
  s16 div, cap;
  std::memcpy(&div, md + 2, 2);
  std::memcpy(&cap, md + 4, 2);
  if (div == 0) return 0;
  int lvl = u->killer_charge[move & 3] / div;
  if (cap < lvl) lvl = cap;
  return lvl;
}

void pc_camera_frame_target(PartyUnit* u, int speed) {
  BattleGlobals& g = bg();
  if (u->camera_focus == 0 || g.camera == nullptr || g.camera->target != u) return;
  if (unit_is_down(u->target)) return;
  const s32 d = vec_distance_xz_fx(u->target->pos, u->pos);
  camera_set_focus_unit(g.camera, u, u->target);
  camera_schedule_end(7, u, static_cast<s16>(d / (speed << 12) + 0x28));
}

void pc_camera_behind_start(PartyUnit* u) {
  BattleGlobals& g = bg();
  if (g.controlled_slot != u->slot || u->camera_focus != 0 || g.camera_special != nullptr) return;
  camera_end_scheduled();
  camera_request_mode(g.camera, 0xF, u);
  u->camera_focus = 2;
}

int pc_attack_hit(PartyUnit* u, int power, u16 elements) {
  Unit* units[8] = {};
  Vec4 points[8] = {};
  const int n = collider_attack_overlaps(u, units, points, 0);
  if (n == 0 || u->hit_landed != 0) return 0;
  return unit_attack_targets(u, n, units, points, power, 9999, elements) + 1;
}

int unit_mp_cost(Unit* u, int cost) {
  if (cost == 0 || u->status_test(3, 2) != 0) return 0;
  if (u->status_test(1, 0x10) != 0 || u->status_test(2, 4) != 0 || u->status_test(2, 8) != 0) {
    cost >>= 1;
  } else if (u->status_test(1, 1) != 0 || u->status_test(1, 0x10000000) != 0) {
    cost = (cost << 1) / 3;
  }
  return cost == 0 ? 1 : cost;
}

int command_killer_move(int which) {
  BattleGlobals& g = bg();
  BattleInput& in = battle_input();
  PartyUnit* u = g.party_units[g.controlled_slot];
  if (which < 2 && u != nullptr && u->record[0xCC + which] != 0) {
    if (u->status_test(2, 0x80000000) == 0) {
      const int cost = u->killer_move_cost(which);
      if (cost <= u->st()->mp) {
        Unit* t = g.enemy_units[g.target_enemy & 7];
        u->target = t;
        const int far = unit_target_is_far(u, t);
        u->reach = u->killer_range[(which * 2 + far) & 7];
        u->approach_side = 0;
        if (unit_can_reach_target(u, t, 1) != 1) {
          bsfx_play_ui(7);
          in.xmark_t = 0x20;
          g.assist_request = 1;
          g.assist_timer = 0xB4;
          return 0;
        }
        u->command = static_cast<u8>(which + 2);
        u->combo = 0;
        bsfx_play_ui(2);
        return 1;
      }
    }
    bsfx_play_ui(7);
  }
  return 0;
}

void Unit::release_effects() {
  for (Afterimage*& e : effects) {
    if (e != nullptr) {
      e->ending = 1;
      e = nullptr;
    }
  }
}

}  // namespace ss::game::battle
