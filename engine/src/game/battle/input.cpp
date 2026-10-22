#include "game/battle/input.h"

#include <cstring>

#include "game/battle/bsfx.h"
#include "game/battle/camera.h"
#include "game/battle/combat.h"
#include "game/battle/render.h"
#include "game/battle/sprite.h"
#include "game/battle/stage.h"
#include "game/battle/unit.h"
#include "game/battle/unit_move.h"
#include "game/boot/pad.h"
#include "game/boot/rng.h"
#include "runtime/libgpu.h"
#include "runtime/runtime.h"

namespace ss::game::battle {
namespace {

BattleInput g_input;

void todo(const char* what) {
  runtime::Runtime::get().log_once(std::string("battle-input-") + what, std::string("battle: not ported yet: ") + what);
}

u16 key(int i) {
  u16 v;
  std::memcpy(&v, bg().config + i * 2, 2);
  return v;
}

PartyUnit* controlled() {
  BattleGlobals& g = bg();
  return g.party_units[g.controlled_slot & 3];
}

EnemyUnit* target_enemy() {
  BattleGlobals& g = bg();
  return g.target_enemy < 8 ? g.enemy_units[g.target_enemy] : nullptr;
}

bool marker_alive(const BattleInput* in) { return in->marker != nullptr && in->marker_owner != 1; }

void end_free_run(BattleInput* in, PartyUnit* u) {
  BattleGlobals& g = bg();
  if (g.free_run != 0) {
    u->appear = static_cast<u8>(u->appear & 0xFB);
    g.free_run = 0;
    in->camera_return = 0x1E;
  }
}

void camera_to_leader(BattleInput* in) {
  camera_set_lookat_offset(bg().camera, kZeroVector);
  camera_set_mode(bg().camera, 2, nullptr);
  in->camera_return = 0x3C;
}

// The controlled member's action-recovery gauge above its head (pool B).
void draw_recovery_gauge(const PartyUnit* u) {
  RenderContext* pool = bg().prim_pool_b;
  const s32 y = u->screen_y;
  const s32 x = u->screen_x + ((u->final_scale_x << 4) >> 12);
  const s32 w = (u->recovery_gauge * 0x10) / 0x3C;
  psyq::P_TAG* ot = &pool->ot_head[0];
  if (auto* t = screen_alloc_prims<psyq::TILE>(pool->arena, 1)) {
    psyq::setTile(t);
    t->r0 = 0x08, t->g0 = 0x50, t->b0 = 0x20;
    t->x0 = static_cast<s16>(x + w + 1);
    t->y0 = static_cast<s16>(y - 8);
    t->w = static_cast<s16>(0x10 - w);
    t->h = 6;
    psyq::addPrim(ot, t);
  }
  if (auto* p = screen_alloc_prims<psyq::POLY_G4>(pool->arena, 1)) {
    psyq::setPolyG4(p);
    p->r0 = 0x80, p->g0 = 0x40, p->b0 = 0;
    p->r1 = 0xE0, p->g1 = 0xC0, p->b1 = 0;
    p->r2 = 0x80, p->g2 = 0x40, p->b2 = 0;
    p->r3 = 0xE0, p->g3 = 0xC0, p->b3 = 0;
    p->x0 = static_cast<s16>(x + 1), p->y0 = static_cast<s16>(y - 8);
    p->x1 = static_cast<s16>(x + 0x11), p->y1 = static_cast<s16>(y - 8);
    p->x2 = static_cast<s16>(x + 1), p->y2 = static_cast<s16>(y - 2);
    p->x3 = static_cast<s16>(x + 0x11), p->y3 = static_cast<s16>(y - 2);
    psyq::addPrim(ot, p);
  }
  if (auto* t = screen_alloc_prims<psyq::TILE>(pool->arena, 1)) {
    psyq::setTile(t);
    t->r0 = 0x40, t->g0 = 0xC0, t->b0 = 0xFF;
    t->x0 = static_cast<s16>(x);
    t->y0 = static_cast<s16>(y - 9);
    t->w = 0x12;
    t->h = 8;
    psyq::addPrim(ot, t);
  }
  if (auto* t = screen_alloc_prims<psyq::TILE>(pool->arena, 1)) {
    psyq::setTile(t);
    psyq::setSemiTrans(t, 1);
    t->r0 = t->g0 = t->b0 = 0;
    t->x0 = static_cast<s16>(x + 2);
    t->y0 = static_cast<s16>(y - 7);
    t->w = 0x12;
    t->h = 8;
    psyq::addPrim(ot, t);
  }
  if (auto* dm = screen_alloc_prims<psyq::DR_MODE>(pool->arena, 1)) {
    psyq::SetDrawMode(dm, 0, 1, 0, nullptr);
    psyq::addPrim(ot, dm);
  }
}

// The range panel at the bottom left (two semi-transparent gradient bars).
void draw_hud_panel(const BattleInput* in) {
  RenderContext* pool = bg().prim_pool_b;
  psyq::P_TAG* ot = &pool->ot_head[0];
  const s16 x = in->hud_x;
  if (auto* p = screen_alloc_prims<psyq::POLY_G4>(pool->arena, 1)) {
    psyq::setPolyG4(p);
    psyq::setSemiTrans(p, 1);
    p->r0 = 0xFF, p->g0 = 0xFF, p->b0 = 0x60;
    p->r1 = 0xFF, p->g1 = 0xFF, p->b1 = 0x60;
    p->r2 = 0xD0, p->g2 = 0x40, p->b2 = 0;
    p->r3 = 0xD0, p->g3 = 0x40, p->b3 = 0;
    p->x0 = x, p->y0 = 0xD5;
    p->x1 = static_cast<s16>(x + 0x24), p->y1 = 0xD5;
    p->x2 = x, p->y2 = 0xE0;
    p->x3 = static_cast<s16>(x + 0x2F), p->y3 = 0xE0;
    psyq::addPrim(ot, p);
  }
  if (auto* p = screen_alloc_prims<psyq::POLY_G4>(pool->arena, 1)) {
    psyq::setPolyG4(p);
    psyq::setSemiTrans(p, 1);
    p->r0 = 0, p->g0 = 0x20, p->b0 = 0x40;
    p->r1 = 0x70, p->g1 = 0xE0, p->b1 = 0xFF;
    p->r2 = 0, p->g2 = 0x20, p->b2 = 0x40;
    p->r3 = 0x70, p->g3 = 0xE0, p->b3 = 0xFF;
    p->x0 = static_cast<s16>(x + 0x2D), p->y0 = 0xD8;
    p->x1 = static_cast<s16>(x + 0x4E), p->y1 = 0xD8;
    p->x2 = static_cast<s16>(x + 0x2D), p->y2 = 0xE0;
    p->x3 = static_cast<s16>(x + 0x4E), p->y3 = 0xE0;
    psyq::addPrim(ot, p);
  }
  if (auto* dm = screen_alloc_prims<psyq::DR_MODE>(pool->arena, 1)) {
    psyq::SetDrawMode(dm, 0, 1, 0, nullptr);
    psyq::addPrim(ot, dm);
  }
}

// 0x8004b3e8 input_free_run: the d-pad moves the controlled member (no analog stick).
void input_free_run(BattleInput* in) {
  BattleGlobals& g = bg();
  if (g.flags == 0 && g.camera->mode != 0xE) camera_request_mode(g.camera, 2, nullptr);
  PartyUnit* u = controlled();
  s32 vx = 0, vz = 0;
  if (u->airborne != 1) {
    const u32 h = in->held;
    if (h & 0x1000) vz = u->run_speed;
    if (h & 0x4000) vz = -u->run_speed;
    if (h & 0x8000) vx = -u->run_speed;
    if (h & 0x2000) vx = u->run_speed;
  }
  s32 dx = vx * 3;
  if (dx < 0) dx += 3;
  s32 d[3] = {dx >> 2, 0, 0};
  u->vel[0] = 0;
  if (unit_try_move_scaled(u, d, 7) == nullptr) {
    bobj_move(u, d);
    u->vel[0] = vx;
    u->appear = static_cast<u8>(u->appear | 4);
  }
  s32 dz = vz * 3;
  if (dz < 0) dz += 3;
  d[0] = 0;
  d[2] = dz >> 2;
  u->vel[2] = 0;
  if (unit_try_move_scaled(u, d, 7) == nullptr) {
    bobj_move(u, d);
    u->vel[2] = vz;
    u->appear = static_cast<u8>(u->appear | 4);
  }
  if (vx == 0 && vz == 0) {
    u->anim = 0;
  } else {
    u->facing[0] = vx;
    u->facing[1] = 0;
    u->facing[2] = vz;
    u->anim = 1;
  }
}

}  // namespace

BattleInput& battle_input() { return g_input; }

void input_reset() { g_input = BattleInput{}; }

int pc_nearest_enemy_index(const Unit* u) {
  const BattleGlobals& g = bg();
  s32 best = 0x1869F000;
  int idx = -1;
  for (int i = 0; i < 8; ++i) {
    const EnemyUnit* e = g.enemy_units[i];
    if (unit_is_down(e)) continue;
    const s32 d = vec_distance_fx(e->pos, u->pos);
    if (d < best) {
      best = d;
      idx = i;
    }
  }
  return idx;
}

void input_update(BattleInput* in) {
  BattleGlobals& g = bg();
  in->held = pad_held(g.pad, 0);
  in->repeat = pad_pressed_repeat(g.pad, 0);
  in->pressed = pad_pressed(g.pad, 0);
  pad_clear(g.pad, 0);
  if (g.flags & 4) in->held = in->repeat = in->pressed = 0;
  input_dispatch(in);
}

void input_hide_member_cursor(BattleInput* in) {
  in->member_t = 0;
  in->member_t2 = 0;
  sprite_free(in->member_cursor);
  in->member_cursor = nullptr;
  // TODO(port): the HUD window's member sprite (g_hud +0x28).
}

void input_hide_target_arrows(BattleInput* in) {
  in->arrow_t = 0;
  sprite_free(in->arrow_l);
  in->arrow_l = nullptr;
  sprite_free(in->arrow_r);
  in->arrow_r = nullptr;
}

void input_open_target_select(BattleInput* in) {
  if (in->arrow_t != 0) input_hide_target_arrows(in);
  in->mode = 2;
  if (in->arrow_l != nullptr) sprite_free(in->arrow_l);
  if (in->arrow_r != nullptr) sprite_free(in->arrow_r);
  in->arrow_l = sprite_spawn_ui(6, 0, nullptr);
  in->arrow_r = sprite_spawn_ui(7, 0, nullptr);
  in->xmark_l = nullptr;
  in->xmark_r = nullptr;
  in->repeat_delay = 0;
  if (0 < in->camera_return) {
    camera_end_mode(bg().camera, 2, 1, nullptr);
    in->camera_return = 0;
  }
  in->xmark_t = 0;
  in->bounce = 0;
  in->target_cam_t = 0;
}

void input_classify_targets(BattleInput* in, s32 x, s32 y) {
  const BattleGlobals& g = bg();
  for (int i = 0; i < 8; ++i) {
    in->octant[i] = 0;
    const EnemyUnit* e = g.enemy_units[i];
    if (unit_is_down(e)) continue;
    s32 dx = e->screen_x - x;
    if (dx < 0) dx = x - e->screen_x;
    s32 dy = e->screen_y - y;
    if (dy < 0) dy = y - e->screen_y;
    u8 oct;
    bool extra;
    if (dy < dx) {
      in->major[i] = static_cast<s16>(dx);
      in->minor[i] = static_cast<s16>(dy);
      oct = e->screen_x < x ? 7 : 3;
      extra = y < e->screen_y;
    } else {
      if (dx == 0 && dy == 0) continue;
      in->major[i] = static_cast<s16>(dy);
      in->minor[i] = static_cast<s16>(dx);
      oct = e->screen_y < y ? 1 : 5;
      extra = x < e->screen_x;
    }
    if (extra) ++oct;
    in->octant[i] = oct;
  }
}

void input_pick_target(BattleInput* in, u32 a, u32 b, int dir) {
  BattleGlobals& g = bg();
  s16 best_major = 0x7FFF, best_minor = 0x7FFF;
  int pick = -1;
  for (int i = 0; i < 8; ++i) {
    const u32 o = in->octant[i];
    if ((o == a || o == b) &&
        (in->major[i] < best_major || (in->major[i] == best_major && in->minor[i] < best_minor))) {
      best_minor = in->minor[i];
      pick = i;
      best_major = in->major[i];
    }
  }
  if (pick < 0) {
    const EnemyUnit* cur = target_enemy();
    if (cur == nullptr) return;
    s32 best = 0x7FFF;
    for (int i = 0; i < 8; ++i) {
      const EnemyUnit* e = g.enemy_units[i];
      if (unit_is_down(e) || i == g.target_enemy) continue;
      bool wrong_side = false;
      switch (dir) {
        case 0: wrong_side = cur->screen_x < e->screen_x; break;  // left
        case 1: wrong_side = e->screen_x < cur->screen_x; break;  // right
        case 2: wrong_side = cur->screen_y < e->screen_y; break;  // up
        case 3: wrong_side = e->screen_y < cur->screen_y; break;  // down
        default: break;
      }
      if (wrong_side) continue;
      const s32 dx = e->screen_x - cur->screen_x, dy = e->screen_y - cur->screen_y;
      const s32 d = square_root0(dx * dx + dy * dy);
      if (d < best) {
        pick = i;
        best = d;
      }
    }
    if (pick < 0) return;
  }
  g.target_enemy = static_cast<u8>(pick);
}

void input_target_select_update(BattleInput* in) {
  BattleGlobals& g = bg();
  bool cancel = false;
  if (in->repeat_delay != 0) {
    const s32 t = in->repeat_delay - g.frame_ticks;
    in->repeat_delay = static_cast<s8>(t);
    if (static_cast<s8>(t) < 0) in->repeat_delay = 0;
  }
  if (in->target_cam_t < 0xFFFF) {
    const u32 t = static_cast<u32>(in->target_cam_t) + static_cast<u32>(g.frame_ticks);
    in->target_cam_t = static_cast<u16>(t);
    if (0x11 < (t & 0xFFFF) || !bobj_on_screen(controlled())) {
      camera_set_mode(g.camera, 4, nullptr);
      in->target_cam_t = 0xFFFF;
    }
  }
  EnemyUnit* cur = target_enemy();
  if (cur != nullptr) input_classify_targets(in, cur->screen_x, cur->screen_y);
  const u8 before = g.target_enemy;
  if (in->repeat & 0xF000) {
    if (in->target_cam_t != 0xFFFF) in->target_cam_t = 0x32;
    in->xmark_t = 1;
    if ((in->repeat & 0x8000) && in->repeat_delay == 0) input_pick_target(in, 7, 8, 0);
    if ((in->repeat & 0x2000) && in->repeat_delay == 0) input_pick_target(in, 4, 3, 1);
    if ((in->repeat & 0x1000) && in->repeat_delay == 0) input_pick_target(in, 2, 1, 2);
    if ((in->repeat & 0x4000) && in->repeat_delay == 0) input_pick_target(in, 6, 5, 3);
    if (g.target_enemy != before) bsfx_play_ui(1);
  }
  bool done = false;
  if (in->pressed & key(0)) {
    PartyUnit* u = controlled();
    if (in->target_purpose == 1) {
      u->target = target_enemy();
      u->command = 4;
      todo("spell targeting (the spell menu)");
      bsfx_play_ui(2);
      done = true;
    } else if (in->target_purpose == 0) {
      u->target = target_enemy();
      u->reach = u->base_reach;
      u->approach_side = 0;
      if (unit_can_reach_target(u, target_enemy(), 1) != 0) {
        u->command = 1;
        bsfx_play_ui(2);
        done = true;
      } else {
        bsfx_play_ui(7);
        in->xmark_t = 0x20;
        g.assist_request = 1;
        g.assist_timer = 0xB4;
      }
    } else if (in->target_purpose == 2) {
      todo("item_use_battle (the item menu)");
      bsfx_play_ui(2);
      done = true;
    }
  }
  if (!done) {
    if (((in->pressed & key(4)) != 0 && in->target_purpose == 0) || ((in->pressed & key(5)) != 0 && in->target_purpose == 0)) {
      todo("command_killer_move (killer moves)");
    }
    const bool keep = (in->pressed & (key(1) | key(3))) == 0 && g.enemy_count != 0 && g.state < 3 &&
                      !unit_is_down(controlled()) && g.effects_suppressed == 0;
    if (keep) {
      if (in->bounce < 0xE) {
        const s16 sc = battle_s16(0x8006CD70 + (in->bounce & 0xFE));
        in->arrow_l->scale_x = sc;
        in->arrow_r->scale_x = sc;
        in->bounce = static_cast<u8>(in->bounce + g.frame_ticks);
      } else {
        in->arrow_l->scale_x = 0x1000;
        in->arrow_r->scale_x = 0x1000;
      }
      EnemyUnit* e = target_enemy();
      if (e != nullptr) {
        s32 s = bsin(static_cast<u32>((g.battle_frames & 0x3F) << 6));
        sprite_attach_screen(in->arrow_l, e, (-static_cast<s32>(e->size[0]) / 2) * e->final_scale_x >> 12,
                             (s * 2 + (8 - e->size[1]) * e->final_scale_y) >> 12);
        s = bsin(static_cast<u32>(((g.battle_frames & 0x3F) + 0x20) * 0x40));
        const s32 w = e->size[0];
        sprite_attach_screen(in->arrow_r, e, ((w - (w >> 31)) >> 1) * e->final_scale_x >> 12,
                             (s * 2 + (8 - e->size[1]) * e->final_scale_y) >> 12);
      }
      if (in->xmark_t == 0) return;
      const s32 t = in->xmark_t - g.frame_ticks;
      in->xmark_t = static_cast<s16>(t);
      if (0 < static_cast<s16>(t)) {
        // The "out of reach" crosses over the arrows.
        if (in->xmark_l == nullptr) {
          in->xmark_l = sprite_spawn_ui(0x16, 0, nullptr);
          sprite_attach_screen(in->xmark_l, in->arrow_l, -2, -4);
        }
        if (in->xmark_r == nullptr) {
          in->xmark_r = sprite_spawn_ui(0x16, 0, nullptr);
          sprite_attach_screen(in->xmark_r, in->arrow_r, 2, -4);
        }
        if (in->xmark_t < 0x10) {
          in->xmark_l->colour_mode = 1;
          in->xmark_l->rgb[0] = static_cast<u8>(static_cast<u8>(in->xmark_t) * 4 + '@');
          in->xmark_r->colour_mode = 1;
          in->xmark_r->rgb[0] = static_cast<u8>(static_cast<u8>(in->xmark_t) * 4 + '@');
        }
        in->xmark_l->layer = 1;
        in->xmark_r->layer = 1;
        return;
      }
      in->xmark_t = 0;
      if (in->xmark_l != nullptr) {
        sprite_free(in->xmark_l);
        in->xmark_l = nullptr;
      }
      if (in->xmark_r == nullptr) return;
      sprite_free(in->xmark_r);
      in->xmark_r = nullptr;
      return;
    }
    cancel = true;
  }
  in->mode = 0;
  g.flags = static_cast<u8>(g.flags & 0xFD);
  in->arrow_t = 8;
  camera_end_mode(g.camera, 4, 1, nullptr);
  if (in->xmark_l != nullptr) {
    sprite_free(in->xmark_l);
    in->xmark_l = nullptr;
    sprite_free(in->xmark_r);
    in->xmark_r = nullptr;
  }
  if (cancel) {
    bsfx_play_ui(6);
    if (in->target_purpose != 0) {
      g.flags = static_cast<u8>(g.flags | 2);
      in->mode = 4;
      todo("the spell / item list menus");
      in->mode = 0;
      g.flags = static_cast<u8>(g.flags & 0xFD);
    }
  }
}

void input_dispatch(BattleInput* in) {
  BattleGlobals& g = bg();
  Camera* cam = g.camera;
  if (in->camera_return != 0) {
    const s32 t = in->camera_return - g.frame_ticks;
    in->camera_return = static_cast<s8>(t);
    if (g.director_idle != 0 || static_cast<s8>(t) < 1) {
      in->camera_return = 0;
      camera_end_mode(cam, 2, 1, nullptr);
    }
  }
  if (in->tactic_t != 0) {
    const s32 t = in->tactic_t - g.frame_ticks;
    in->tactic_t = static_cast<s8>(t);
    if (static_cast<s8>(t) < 1) {
      in->tactic_t = 0;
      sprite_free(in->tactic_icon);
      in->tactic_icon = nullptr;
    }
  }
  if (controlled() != nullptr && controlled()->removed != 0 && g.state == kStateFighting) {
    for (int k = 0; k < 4; ++k) {
      g.controlled_slot = static_cast<u8>((g.controlled_slot + 1) & 3);
      if (g.party_units[g.controlled_slot] != nullptr && g.party_units[g.controlled_slot]->removed == 0) break;
    }
  }
  if (g.target_enemy != g.target_enemy_prev && g.target_enemy != 0xFF) {
    if (marker_alive(in)) sprite_free(in->marker);
    EnemyUnit* e = target_enemy();
    const int anim = controlled()->pos[0] < e->pos[0] ? 0x36 : 0x35;
    in->marker_owner = 0xFF;
    in->marker = sprite_spawn_ui(anim, 3, &in->marker_owner);
    const s32 w = e->size[0];
    s32 dx = ((w - (w >> 31)) >> 1) * e->final_scale_x >> 12;
    if (anim != 0x35) dx = -dx;
    sprite_attach_screen(in->marker, e, dx, -8);
  }
  g.target_enemy_prev = g.target_enemy;
  if (g.state == kStateFighting && (g.target_enemy == 0xFF || unit_is_down(target_enemy())) && g.enemy_count != 0) {
    const int i = pc_nearest_enemy_index(controlled());
    if (i >= 0) g.target_enemy = static_cast<u8>(i);
  }
  if (g.config[0x49] != 1 && g.free_run != 0 && g.enemy_count != 0) {
    const int i = pc_nearest_enemy_index(controlled());
    if (i >= 0 && g.target_enemy != i) {
      g.target_enemy = static_cast<u8>(i);
      bsfx_play_ui(1);
    }
  }
  if ((g.flags & 0x40) == 0 && !unit_is_down(controlled()) && controlled()->recovery_gauge != 0)
    draw_recovery_gauge(controlled());
  // The range panel.
  if (in->hud_state == 0 && g.state == kStateFighting) {
    in->hud_state = 1;
    in->hud_x = static_cast<s16>(0xFFAC);
    in->hud_panel = sprite_spawn_ui(0x27, 0, nullptr);
  }
  if (in->hud_state != 0) {
    if (2 < g.state) in->hud_state = 2;
    draw_hud_panel(in);
    sprite_set_screen_pos(in->hud_panel, static_cast<s16>(in->hud_x + 2), 0xCE);
    if (in->range_icon != nullptr) {
      sprite_free(in->range_icon);
      in->range_icon = nullptr;
    }
    if (g.target_enemy != 0xFF && !unit_is_down(controlled()) && !unit_is_down(target_enemy())) {
      const int far = unit_target_is_far(controlled(), target_enemy());
      in->range_icon = sprite_spawn_ui(0x29 - far, 0, nullptr);
      sprite_set_screen_pos(in->range_icon, static_cast<s16>(in->hud_x + (far != 0 ? 0x30 : 0x2E)), 0xD2);
    }
    if (in->hud_state == 1) {
      if (in->hud_x < 8) {
        in->hud_x = static_cast<s16>(in->hud_x + g.frame_ticks * 2);
        if (8 < in->hud_x) in->hud_x = 8;
      }
    } else if (in->hud_state == 2) {
      in->hud_x = static_cast<s16>(in->hud_x - g.frame_ticks * 2);
      if (in->hud_x < -0x54) {
        in->hud_state = 0;
        sprite_free(in->hud_panel);
        in->hud_panel = nullptr;
      }
    }
  }
  if (0 < in->member_t) {
    if (in->member_t2 == 0) {
      if (in->member_cursor != nullptr) in->member_cursor->scale_y = in->member_t << 9;
      const s32 t = static_cast<u8>(in->member_t) - g.frame_ticks;
      in->member_t = static_cast<s8>(t);
      if (static_cast<s8>(t) < 1) input_hide_member_cursor(in);
    } else {
      in->member_t2 = static_cast<s8>(in->member_t2 - g.frame_ticks);
      if (in->member_cursor != nullptr)
        sprite_attach_screen(in->member_cursor, controlled(), 0, controlled()->final_scale_y * -0x24 >> 12);
      if (in->member_t2 < 1) in->member_t2 = 0;
    }
  }
  if (0 < in->arrow_t) {
    in->arrow_l->scale_x = in->arrow_t << 9;
    in->arrow_r->scale_x = in->arrow_t << 9;
    const s32 t = static_cast<u8>(in->arrow_t) - g.frame_ticks;
    in->arrow_t = static_cast<s8>(t);
    if (static_cast<s8>(t) < 1) input_hide_target_arrows(in);
  }
  if (0 < in->ring_radius && in->mode != 3) todo("input_ring_menu_update (closing)");
  bool can_act = false;
  if (1 < g.state && g.config[0x181] != 1 &&
      (g.controlled_slot == 0xFF || controlled()->status_test(3, 0x2000) == 0))
    can_act = true;
  if ((g.flags & 0xC5) == 0 && g.party_escaping == 0 && g.state == kStateFighting && can_act) {
    if ((in->pressed & key(6)) != 0 && in->mode < 2 && g.free_run == 0) {
      // Swap control with the leader.
      bsfx_play_ui(2);
      g.member_target[g.controlled_slot & 3] = g.target_enemy;
      u8 next = g.leader_slot;
      if (g.controlled_slot != g.config[0x41]) {
        g.leader_slot = g.controlled_slot;
        next = g.config[0x41];
      }
      g.target_enemy = g.member_target[next & 3];
      g.controlled_slot = next;
      if (in->mode != 0) return;
      camera_to_leader(in);
      if (in->member_t != 0) input_hide_member_cursor(in);
      if (in->member_cursor != nullptr) sprite_free(in->member_cursor);
      in->member_cursor = sprite_spawn_ui(3, 0, nullptr);
      in->member_t = 8;
      in->member_t2 = 0x1E;
      return;
    }
    if ((in->pressed & key(7)) != 0 && in->mode != 4) {
      // Toggle the member's tactic (record +0x38 = 5: no orders).
      bsfx_play_ui(2);
      u8* rec = controlled()->record;
      if (rec != nullptr) rec[0x38] = rec[0x38] == 5 ? rec[0x39] : 5;
      if (in->mode == 0) camera_to_leader(in);
      if (in->tactic_icon != nullptr) sprite_free(in->tactic_icon);
      in->tactic_icon = sprite_spawn_ui(rec != nullptr && rec[0x38] == 5 ? 0x2B : 0x2A, 0, nullptr);
      sprite_attach_screen(in->tactic_icon, controlled(), -0x20, 4);
      in->tactic_t = 0x4B;
      return;
    }
  }
  if (in->mode == 2) {
    input_target_select_update(in);
    return;
  }
  if (in->mode == 1) {
    todo("input_member_select_update (mode 1)");
    in->mode = 0;
    g.flags = static_cast<u8>(g.flags & 0xFD);
    return;
  }
  if (in->mode == 3) {
    todo("input_ring_menu_update (mode 3)");
    in->mode = 0;
    g.flags = static_cast<u8>(g.flags & 0xFD);
    return;
  }
  if (in->mode != 0) return;
  PartyUnit* u = controlled();
  if (g.flags == 0 && g.party_escaping == 0 && g.state == kStateFighting && can_act) {
    const bool hold = (g.config[0x4B] == 0 && (in->held & key(3)) != 0) || (g.config[0x4B] == 1 && (in->held & 0xF000) != 0);
    if (hold && u->busy == 0 && (u->status_bits & 0x10) == 0) {
      if (g.free_run == 0 && u->airborne != 1 && u->action == 0) {
        g.free_run = 1;
        in->camera_return = 0;
        if (cam->mode != 0xE) {
          camera_set_lookat_offset(cam, kZeroVector);
          camera_set_mode(cam, 2, nullptr);
        }
        pc_record_counter_dec(u, 1);
      }
    } else if (g.free_run != 0) {
      end_free_run(in, u);
    }
  }
  if (g.config[0x4B] == 1 && can_act) {
    if ((in->held & key(3)) == 0 || (g.flags & 0x40) != 0) {
      if (cam->mode == 0xE && in->mode != 2) camera_end_mode(cam, 0xE, 1, nullptr);
    } else {
      camera_set_mode(cam, 0xE, nullptr);
      camera_set_lookat_offset(cam, kZeroVector);
    }
  }
  if (g.flags == 0 && g.free_run != 0 && u->busy == 0 && !unit_is_down(u) && g.state == kStateFighting && u->action == 0 &&
      (u->status_bits & 0x10) == 0 && can_act)
    input_free_run(in);
  if ((in->pressed & 0x100) != 0 && g.flags == 0 && g.party_escaping == 0 && g.state == kStateFighting && can_act) {
    const int lvl = u->skill_level(0x2D);
    if (lvl < 1 || g.free_run != 0 || u->busy != 0 || g.provoke_used != 0 || (u->status_bits & 0x10) != 0) {
      bsfx_play_ui(7);
    } else {
      // Provoke: the monsters go for this member for lvl * 3 s.
      effect_skill_popup(u, 0x2D);
      g.provoke_used = 1;
      const s32 r = static_cast<s32>(rng_range(10));
      g.provoker = u;
      g.provoke_timer = static_cast<s16>(lvl * 0xB4 + r);
      camera_end_scheduled();
      camera_set_mode(cam, 5, u);
      u->camera_focus = 1;
    }
  }
  if ((g.flags & 0xC5) != 0 || g.state != kStateFighting || g.party_escaping != 0 || g.config[0x181] == 1) return;
  if (g.free_run == 0 && (in->pressed & key(1)) != 0) {
    bsfx_play_ui(2);
    in->member_purpose = 0;
    todo("input_open_member_select (mode 1)");
    return;
  }
  if (u->status_test(3, 0x2000) != 1) {
    if ((in->pressed & key(2)) != 0) {
      end_free_run(in, u);
      bsfx_play_ui(2);
      todo("input_ring_menu_open (spells / items / escape / tactics)");
      return;
    }
    const bool chainable = !unit_is_down(u) && u->action == 9 && !unit_is_down(u->target);
    if (chainable) {
      if (in->chain_latch != 0) return;
      if (in->pressed & key(4)) in->chain_latch = 1;
      if (in->pressed & key(5)) in->chain_latch = 2;
      const int lvl = u->skill_level(0x2B);
      if (in->chain_latch == 0 || u->chain < 2 || lvl + 1 <= u->chain) return;
      todo("command_killer_move (chained killer moves)");
      return;
    }
    in->chain_latch = 0;
  }
  const bool counter = !unit_is_down(u) && u->action == 4 && !unit_is_down(u->last_attacker) && u->status_test(3, 0x2000) == 0;
  if (!counter) {
    in->counter_latch = 0;
    u32 mask = key(5) | key(0) | key(4);
    if (u->status_test(3, 0x2000) != 0) mask = 0;
    if (g.config[0x49] == 2) {
      if (u->busy != 0 || u->action != 0 || unit_is_down(u) || (u->status_bits & 0x10) != 0 || (in->pressed & mask) == 0)
        return;
      end_free_run(in, u);
      if (in->pressed & key(0)) {
        u->reach = u->base_reach;
        u->approach_side = 0;
        u->command = 1;
        u->target = target_enemy();
        bsfx_play_ui(2);
        return;
      }
      todo("command_killer_move (killer moves)");
      return;
    }
    if ((in->pressed & mask) == 0) return;
    if (u->action != 0 || u->busy != 0 || unit_is_down(u) || (u->status_bits & 0x10) != 0) return;
    end_free_run(in, u);
    bsfx_play_ui(3);
    in->target_purpose = 0;
    input_open_target_select(in);
    return;
  }
  if (in->counter_latch != 0) return;
  if (in->pressed & key(0)) in->counter_latch = 1;
  const int lvl = u->skill_level(0x24);
  if (in->counter_latch == 0 || u->counter_ticks == 0 || lvl < u->counter_ticks) return;
  u->reach = u->base_reach;
  u->approach_side = 0;
  u->command = 1;
  u->target = u->last_attacker;
  effect_skill_popup(u, 0x24);
  // TODO(port): the counter's afterimage (0x800756e0, colour 0x905050).
  u->attack_recovery = 0;
  u->action_kind = 0;
  u->begin_action();
}

}  // namespace ss::game::battle
