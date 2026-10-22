#include "game/battle/killer.h"

#include <cstring>

#include "game/battle/bsfx.h"
#include "game/battle/camera.h"
#include "game/battle/combat.h"
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
    // Oldest first. (The console steps two slots at a time: (head + 2i + 2) % count.)
    for (int i = 0; i < count; ++i) {
      const int k = (head + i * 2 + 2) % count;
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
