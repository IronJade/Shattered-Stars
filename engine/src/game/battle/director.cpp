// battle_frame_director (#1984 block 0, 0x800715d0): the per-frame overlay update the
// main loop runs after the camera. It cycles the UI palette, directs the camera between
// the controlled member, the action and the overview, runs down the scheduled camera
// end, the stage flash, the screen shake and pad vibration, the zoom kick and the
// assist / provoke timers, and draws the arrow to off-screen targets.

#include <cstring>

#include "game/battle/battle.h"
#include "game/battle/camera.h"
#include "game/battle/combat.h"
#include "game/battle/input.h"
#include "game/battle/render.h"
#include "game/battle/spell_fx.h"
#include "game/battle/sprite.h"
#include "game/battle/stage.h"
#include "game/battle/stage_objects.h"
#include "game/battle/unit.h"
#include "game/battle/vram.h"
#include "game/boot/pad.h"
#include "game/boot/rng.h"
#include "runtime/libgpu.h"
#include "runtime/runtime.h"

namespace ss::game::battle {
namespace {

Sprite* g_target_arrow = nullptr;  // 0x8006faec

// v / 409 the way the compiler's magic multiply rounds it (0xa03c1689 >> 40).
s32 div409(s32 v) { return static_cast<s32>((static_cast<s64>(v) * 2688292489LL) >> 40) - (v >> 31); }

void run_toward(s16& v, s16 target, s32 step) {
  if (target < v) {
    v = static_cast<s16>(v - step);
    if (v < target) v = target;
  } else if (v < target) {
    v = static_cast<s16>(v + step);
    if (target < v) v = target;
  }
}

}  // namespace

void ui_draw_target_arrow(Sprite** icon, s32 sx, s32 sy, u32 rgb, int icon_id) {
  BattleGlobals& g = bg();
  s32 a = ratan2(sy - 0x8C, sx - 0xA0);
  const s32 c = bcos(static_cast<u32>(a)), s = bsin(static_cast<u32>(a));
  s32 m = c < 0 ? -c : c;
  const s32 as = s < 0 ? -s : s;
  if (m < as) m = as;
  const s32 x = (c * 0x84) / m + 0xA0;
  const s32 dy = (s * 0x4C) / m;
  const s32 y = dy + 0x84;
  a = ratan2(sy - y, sx - x);
  RenderContext* pool = g.prim_pool_a;
  pool_a_bytes_used() += 0x20;
  if (auto* p = screen_alloc_prims<psyq::POLY_F3>(pool->arena, 1)) {
    psyq::setPolyF3(p);
    psyq::setSemiTrans(p, 1);
    p->r0 = static_cast<u8>(rgb), p->g0 = static_cast<u8>(rgb >> 8), p->b0 = static_cast<u8>(rgb >> 16);
    p->x0 = static_cast<s16>(x + bcos(static_cast<u32>(a)) / 0xE3);
    p->y0 = static_cast<s16>(y + bsin(static_cast<u32>(a)) / 0xE3);
    p->x1 = static_cast<s16>(x + div409(bcos(static_cast<u32>(a + 0x555))));
    p->y1 = static_cast<s16>(y + div409(bsin(static_cast<u32>(a + 0x555))));
    p->x2 = static_cast<s16>(x + div409(bcos(static_cast<u32>(a + 0xAAA))));
    p->y2 = static_cast<s16>(y + div409(bsin(static_cast<u32>(a + 0xAAA))));
    psyq::addPrim(&pool->ot_head[0x612], p);
  }
  if (auto* dm = screen_alloc_prims<psyq::DR_MODE>(pool->arena, 1)) {
    psyq::SetDrawMode(dm, 0, 1, 0, nullptr);
    psyq::addPrim(&pool->ot_head[0x612], dm);
  }
  if (*icon == nullptr) *icon = sprite_spawn_ui(icon_id, 0, nullptr);
  sprite_set_screen_pos(*icon, static_cast<s16>(x), static_cast<s16>(y < 0x88 ? dy + 0x94 : dy + 0x74));
}

void stage_colour_set(s16 r, s16 gr, s16 b) {
  ViewState& v = view();
  BattleGlobals& g = bg();
  g.stage_colour_saved[0] = v.stage_colour[0];
  g.stage_colour_saved[1] = v.stage_colour[1];
  g.stage_colour_saved[2] = v.stage_colour[2];
  v.stage_colour[0] = r;
  v.stage_colour[1] = gr;
  v.stage_colour[2] = b;
}

void stage_colour_restore() {
  ViewState& v = view();
  BattleGlobals& g = bg();
  v.stage_colour[0] = g.stage_colour_saved[0];
  v.stage_colour[1] = g.stage_colour_saved[1];
  v.stage_colour[2] = g.stage_colour_saved[2];
}

void director_reset() { g_target_arrow = nullptr; }

void battle_frame_director() {
  BattleGlobals& g = bg();
  ViewState& v = view();
  Camera* cam = g.camera;
  BattleInput& in = battle_input();
  // The animated UI palette: two runs of nine colours rotate every 4 ticks.
  if ((g.flags & 4) == 0) {
    const s32 t = g.palette_cycle_timer - g.frame_ticks;
    g.palette_cycle_timer = static_cast<u8>(t);
    if (static_cast<s32>(static_cast<u32>(t) << 24) < 1) {
      g.palette_cycle_timer = static_cast<u8>(g.palette_cycle_timer + 4);
      const u16 a = g.ui_palette[0x57], b = g.ui_palette[0x67];
      for (int i = 0; i < 8; ++i) {
        g.ui_palette[0x57 + i] = g.ui_palette[0x58 + i];
        g.ui_palette[0x67 + i] = g.ui_palette[0x68 + i];
      }
      g.ui_palette[0x5F] = a;
      g.ui_palette[0x6F] = b;
    }
    upload_clut256(g.ui_palette.data(), 0, 0x1FE);
  }
  if ((g.flags & 0xC5) == 0) {
    g.director_idle = 0;
    if ((cam->mode == 0 || cam->mode == 3 || g.camera_end_mode != 0) && g.state < 3 && cam->mode != 7) {
      if (g.camera_special != nullptr && g.camera_special->action != 0xF) g.camera_special = nullptr;
      g.director_idle = 1;
      const u32 mode = in.mode;
      if (mode == 1 || mode - 3 < 2) {
        camera_end_scheduled();
        camera_set_mode(cam, 2, nullptr);
      } else if (mode == 2 && 0xF < in.target_cam_t) {
        camera_end_scheduled();
        camera_set_mode(cam, 4, nullptr);
      } else if (g.camera_special == nullptr) {
        PartyUnit* c = g.party_units[g.controlled_slot & 3];
        if (unit_is_down(c) || c->action == 0 || c->camera_focus != 0) {
          if (g.camera_next_target == nullptr) {
            // Watch whoever is acting (a party member reacting counts too).
            g.director_idle = 0;
            for (int i = 0; i < 0xC; ++i) {
              if (i == g.controlled_slot) continue;
              Unit* u = i < 4 ? static_cast<Unit*>(g.party_units[i]) : g.enemy_units[i - 4];
              if (unit_is_down(u)) continue;
              if (7 < u->action || (i < 4 && u->action == 3)) {
                if (0x5A < g.director_timer) {
                  camera_end_scheduled();
                  // The console passes the party slot pointer of the same index here.
                  Unit* who = i < 4 ? static_cast<Unit*>(g.party_units[i]) : g.enemy_units[i - 4];
                  camera_set_mode(cam, 5, who);
                  who->camera_focus = 1;
                  g.director_timer = 0;
                }
                break;
              }
            }
          } else {
            camera_end_scheduled();
            camera_set_mode(cam, 5, g.camera_next_target);
            g.camera_next_target = nullptr;
          }
        } else if (0x3C < g.director_timer) {
          camera_end_scheduled();
          camera_set_mode(cam, 7, c);
          c->camera_focus = 1;
          g.camera_next_target = nullptr;
        }
      }
      if (g.director_idle == 0) g.director_timer = static_cast<s16>(g.director_timer + g.frame_ticks);
      else g.director_timer = 0;
    }
    if (cam->mode == 5) {
      BObj* tb = cam->target;
      if (tb != g.party_units[g.controlled_slot & 3] && g.camera_end_mode == 0) {
        Unit* t = nullptr;  // (the target may be a Shot: only units are looked at)
        for (PartyUnit* u : g.party_units)
          if (u != nullptr && u == tb) t = u;
        for (EnemyUnit* u : g.enemy_units)
          if (u != nullptr && u == tb) t = u;
        if (t != nullptr && t->action < 8) camera_schedule_end(cam->mode, t, 0x5A);
      }
    }
    if (g.camera_end_mode != 0) {
      const s32 left = static_cast<u16>(g.camera_end_timer) - g.frame_ticks;
      g.camera_end_timer = static_cast<s16>(left);
      if (static_cast<s32>(static_cast<u32>(left) << 16) < 1) {
        camera_end_mode(cam, g.camera_end_mode, 1, g.camera_end_unit);
        g.camera_end_mode = 0;
      }
    }
  }
  // The stage colour returns from a flash.
  g.flash_running = 0;
  if (g.flash_active != 0) {
    g.flash_running = 1;
    if (g.flash_delay == 0) {
      const s32 step = g.flash_speed * g.frame_ticks;
      run_toward(v.stage_colour[0], v.stage_colour_target[0], step);
      run_toward(v.stage_colour[1], v.stage_colour_target[1], step);
      run_toward(v.stage_colour[2], v.stage_colour_target[2], step);
      if (v.stage_colour[0] == v.stage_colour_target[0] && v.stage_colour[1] == v.stage_colour_target[1] &&
          v.stage_colour[2] == v.stage_colour_target[2]) {
        g.flash_active = 0;
        g.flash_running = 0;
      }
    } else {
      g.flash_delay = static_cast<s16>(g.flash_delay - g.frame_ticks);
      if (g.flash_delay < 0) g.flash_delay = 0;
    }
  }
  // Screen shake with the pad vibration.
  if (g.shake_frames != 0) {
    int big;
    s32 level;
    if (g.shake_amp[0] == 0 || g.shake_amp[1] == 0) {
      level = (g.shake_amp[0] + g.shake_amp[1]) * 10 + 0x80;
      s8 p = g.shake_period[0];
      if (p < g.shake_period[1]) p = g.shake_period[1];
      if (p < 3) {
        big = 1;
        level = 0;
      } else {
        big = 0;
      }
    } else {
      level = (g.shake_amp[0] + g.shake_amp[1]) * 10 + 0x80;
      if (0xFF < level) level = 0xFF;
      big = 1;
    }
    pad_vibrate(g.pad, 0, big, level);
    if (g.shake_phase[0] < 1) {
      v.shake_x = -1 < v.shake_x ? -g.shake_amp[0] : g.shake_amp[0];
      g.shake_phase[0] = static_cast<s8>(g.shake_phase[0] + g.shake_period[0]);
    }
    if (g.shake_phase[1] < 1) {
      v.shake_y = -1 < v.shake_y ? -g.shake_amp[1] : g.shake_amp[1];
      g.shake_phase[1] = static_cast<s8>(g.shake_phase[1] + g.shake_period[1]);
    }
    g.shake_phase[0] = static_cast<s8>(g.shake_phase[0] - g.frame_ticks);
    const s32 left = static_cast<u16>(g.shake_frames) - g.frame_ticks;
    g.shake_frames = static_cast<s16>(left);
    g.shake_phase[1] = static_cast<s8>(g.shake_phase[1] - g.frame_ticks);
    if (static_cast<s32>(static_cast<u32>(left) << 16) < 1) {
      g.shake_frames = 0;
      v.shake_x = 0;
      v.shake_y = 0;
      pad_vibrate(g.pad, 0, 0, 0);
    }
  }
  // The zoom kick of a critical hit.
  if (g.zoom_kick_decay != 0) {
    v.zoom = g.zoom_kick;
    const s32 z = v.zoom - g.zoom_kick_decay * g.frame_ticks;
    g.zoom_kick = static_cast<s16>(z);
    if (static_cast<s16>(z) < 0x1000) {
      v.zoom = 0x1000;
      g.zoom_kick_decay = 0;
    }
  }
  if (g.flags == 0) {
    if (g.assist_timer != 0) {
      const s32 t = g.assist_timer - g.frame_ticks;
      g.assist_timer = static_cast<s16>(t);
      if (static_cast<s16>(t) < 1) {
        g.assist_timer = 0;
        g.assist_request = 0;
      }
    }
    if (g.provoker != nullptr) {
      const s32 t = static_cast<u16>(g.provoke_timer) - g.frame_ticks;
      g.provoke_timer = static_cast<s16>(t);
      if (static_cast<s32>(static_cast<u32>(t) << 16) < 1 || unit_is_down(g.provoker)) g.provoker = nullptr;
    }
  }
  if ((g.flags & 0xC) == 0 && g.stage_id == 8 && static_cast<u32>(rng_next() & 0x7FFF) < 0x300) {
    // Stage 8's lightning: a white streak from 0x76c units above a random point of the
    // arena down to it, ending in the stage sprite's flash (kind 0x12, puff 1).
    s32 to[4];
    stage_random_point(to, 1);
    const s32 from[3] = {to[0], to[1] - 0x76C000, to[2]};
    effect_spawn_streak_at(from, to, 0x101010, 0xFFFFFF, 0x60, 0x1000, 0x1000, 1);
  }
  // The stage code's per-frame hook (#1984 blocks 5 / 7 / 2 / 8 / 9 at 0x8008c1f4).
  if ((g.flags & 0xC) == 0) stage_code_update();
  // TODO(port): the opening voices (0x8006fb31).
  if (g.flags == 0) {
    const s32 t = static_cast<u16>(g.item_cooldown) - g.frame_ticks;
    g.item_cooldown = static_cast<s16>(t);
    if (static_cast<s32>(static_cast<u32>(t) << 16) < 0) g.item_cooldown = 0;
  }
  if (0 < g.intro_voice_gate) {
    const s32 t = g.intro_voice_gate - g.frame_ticks;
    g.intro_voice_gate = static_cast<u8>(t);
    if (static_cast<s32>(static_cast<u32>(t) << 16) < 0) g.intro_voice_gate = 0;
  }
  // ui_update_target_arrow 0x80072aa4: no enemy on screen -> an arrow to the nearest one.
  if (g.state != kStateLeave) {
    bool any = false;
    for (const EnemyUnit* e : g.enemy_units)
      if (!unit_is_down(e) && bobj_on_screen(e)) {
        any = true;
        break;
      }
    PartyUnit* c = g.party_units[g.controlled_slot & 3];
    const int near = c != nullptr ? pc_nearest_enemy_index(c) : -1;
    if (!any && !unit_is_down(c) && g.state == kStateFighting && near != -1 && (g.flags & 0x40) == 0) {
      u32 k = static_cast<u32>(g.battle_frames & 0x7F);
      if (0x3F < k) k = 0x7F - k;
      const EnemyUnit* e = g.enemy_units[near];
      ui_draw_target_arrow(&g_target_arrow, e->screen_x, e->screen_y, (k + 0x30) * 0x200 | (k + 0x40) * 2, 0x2D);
    } else if (g_target_arrow != nullptr) {
      sprite_free(g_target_arrow);
      g_target_arrow = nullptr;
    }
  }
}

}  // namespace ss::game::battle
