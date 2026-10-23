// PartyUnit behaviour: pc_think's state machine (0x80035508) and the states it runs
// (approach, leap, attack, knockback, KO, victory, flee), the action starter
// pc_begin_action, the status-effect rebuild and the idle AI hook. The character
// modules' killer moves (slot 37) and the party AI per class are not ported yet.
// parts/p1.md "PartyUnit".

#include <cstring>

#include "game/battle/bsfx.h"
#include "game/battle/camera.h"
#include "game/battle/combat.h"
#include "game/battle/enemy_support.h"
#include "game/battle/killer.h"
#include "game/battle/party_ai.h"
#include "game/battle/party_talk.h"
#include "game/battle/render.h"
#include "game/battle/sprite.h"
#include "game/battle/stage.h"
#include "game/battle/spells.h"
#include "game/battle/unit.h"
#include "game/battle/unit_move.h"
#include "game/battle/voice_player.h"
#include "game/boot/rng.h"
#include "runtime/runtime.h"

namespace ss::game::battle {
namespace {

[[maybe_unused]] void todo(const char* what) {
  runtime::Runtime::get().log_once(std::string("battle-party-") + what, std::string("battle: not ported yet: ") + what);
}

u32 rng15() { return static_cast<u32>(rng_next() & 0x7FFF); }

void face_target(Unit* u) {
  if (u->target != nullptr) u->facing[0] = u->pos[0] < u->target->pos[0] ? 0x64000 : static_cast<s32>(0xFFF9C000);
}

}  // namespace

// ---- Helpers ---------------------------------------------------------------------------

void pc_record_counter_dec(PartyUnit* u, s16 n) {
  if (u->record == nullptr) return;
  s16 v;
  std::memcpy(&v, u->record + 0xA, 2);
  v = static_cast<s16>(v - n);
  if (v < 0) v = 0;
  std::memcpy(u->record + 0xA, &v, 2);
}

void pc_voice_attack_shout(PartyUnit* u) {
  int v = 0xD;
  if (rng15() < 0x4000) {
    v = 10;
  } else if (rng15() < 24000) {
    v = 0xB;
  } else if (rng15() < 24000) {
    v = 0xC;
  }
  u->play_voice(v);
}

void pc_camera_follow_end(PartyUnit* u) {
  if (u->camera_focus == 1) {
    camera_schedule_end(7, u, 0x3C);
  } else if (u->camera_focus == 2) {
    camera_schedule_end(0xF, u, 0x3C);
  }
  u->camera_focus = 0;
}

int unit_target_is_far(const Unit* u, const Unit* target) {
  if (target == nullptr) return 0;
  return 0x370000 < vec_distance_fx(u->pos, target->pos) ? 1 : 0;
}

int pc_try_ambush_skill(PartyUnit* u) {
  if (u->approach_side != 0) return 0;
  const Unit* t = u->target;
  if (t != nullptr) {
    const bool facing_away = t->facing[0] < 0 ? t->pos[0] < u->pos[0] : u->pos[0] < t->pos[0];
    if (facing_away) return 0;
  }
  const StageData& st = stage();
  s32 room = u->pos[0] - st.bounds[0];
  if (0x2710000 <= room) room = 0x2710000;
  if (st.bounds[2] - u->pos[0] < room) room = st.bounds[2] - u->pos[0];
  if (0x17FFFF < room) {
    const u32 lvl = static_cast<u32>(u->skill_level(0x23));
    if (rng_range(0x14) < lvl) {
      u->approach_side = 1;
      if (unit_can_reach_target(u, u->target, 0) != 0) {
        effect_skill_popup(u, 0x23);
        return 1;
      }
      u->approach_side = 0;
    }
  }
  return 0;
}

int unit_start_sidestep(Unit* u, Unit* from) {
  u->vel[1] = 0;
  u->vel[2] = 0;
  s32 dz;
  if (from == kWallBlock) {
    dz = 0x40000;
    u->facing[0] = 0x40000;
    u->vel[0] = static_cast<s32>(0xFFFC0000);
  } else {
    const s32 dx = from->pos[0] < u->pos[0] ? 0x40000 : -0x40000;
    u->vel[0] = dx;
    u->facing[0] = -dx;
    s32 d = from->pos[0] - u->pos[0];
    if (d < 0) d = -d;
    if (d < 0x2000) u->vel[0] = 0;
    dz = from->pos[2] < u->pos[2] ? 0x40000 : -0x40000;
  }
  int dir = 0;
  bool along_x_free = false;
  if (u->vel[0] != 0) along_x_free = unit_check_move(u, u->vel, 4) == nullptr;
  if (along_x_free) {
    dir = 1;
  } else {
    const s32 vx = u->vel[0];
    u->vel[0] = -vx;
    u->facing[0] = -u->facing[0];
    if (u->vel[0] != 0 && unit_check_move(u, u->vel, 4) == nullptr) {
      dir = 1;
    } else {
      u->vel[0] = 0;
      u->vel[2] = dz;
      if (unit_check_move(u, u->vel, 4) == nullptr) {
        dir = 2;
      } else {
        u->vel[2] = -dz;
        if (unit_check_move(u, u->vel, 4) == nullptr) dir = 3;
      }
    }
  }
  if (dir != 0) {
    u->action = 2;
    u->leap_ticks = 0x12;
    u->target = nullptr;
    u->step_dest[0] = u->pos[0] + u->vel[0];
    u->step_dest[1] = u->pos[1];
    u->step_dest[2] = u->pos[2] + u->vel[2];
    bobj_anim_reset(u);
    u->stuck_ticks = 0;
  }
  return dir;
}

void unit_update_status_effects(Unit* u) {
  BattleGlobals& g = bg();
  Stats* s = u->st();
  if ((s->status & 2) == 0) {
    u->status_icons[0] = u->status_icons[1] = nullptr;
  } else {
    // Incapacitated: two icons circling above the head.
    sprite_free(u->status_icons[0]);
    Sprite* old1 = u->status_icons[1];
    sprite_free(old1);
    Sprite* a = sprite_spawn_a(0x25, 1, nullptr);
    u->status_icons[0] = a;
    const s32 h = u->size[1] * 4 + ((static_cast<s32>(g.battle_frames & 0xF)) >> 3) * 0x18;
    if (a != nullptr) {
      a->freeze_class = 2;
      sprite_set_pos(a, u->pos[0] + 0x30000, u->pos[1] + h * -0x1000, u->pos[2]);
      if (old1 != nullptr) old1->layer = 1;  // the console writes the old icon's slot
    }
    Sprite* b = sprite_spawn_a(0x26, 1, nullptr);
    u->status_icons[1] = b;
    if (b != nullptr) {
      b->freeze_class = 2;
      sprite_set_pos(b, u->pos[0] - 0x30000, u->pos[1] + h * -0x1000, u->pos[2]);
      b->layer = 1;
    }
  }
  if (g.flags != 0) return;
  const s32 r = static_cast<u16>(u->attack_recovery) - g.frame_ticks;
  u->attack_recovery = static_cast<s16>(r);
  if (static_cast<s16>(r) < 0) u->attack_recovery = 0;
  if (g.state != kStateFighting) return;
  if ((s->status & 8) == 0 || unit_is_down(u)) {
    u->poison_tick = 0;
  } else {
    if (rng15() < 12000) {
      if (Sprite* p = sprite_spawn_a(0x24, 1, nullptr)) {
        const s32 x = u->pos[0];
        const s32 j = rng_spread_fixed(u->size[0] << 1);
        sprite_set_pos(p, x + j, u->pos[1] + u->size[1] * -0x4000, u->pos[2]);
        p->vel[1] = static_cast<s32>(0xFFFFE000);
        p->layer = 1;
      }
    }
    const s32 t = static_cast<u16>(u->poison_tick) + g.frame_ticks;
    u->poison_tick = static_cast<s16>(t);
    if (0xB3 < static_cast<s16>(t)) {
      const s32 d = s->hp_max / 0x21;
      damage_popup_spawn(u, d, 0, 0);
      unit_damage_hp(u, d);
      u->poison_tick = 0;
    }
  }
  if ((u->status_bits & 0x10) == 0) {
    u->dot_tick = 0;
  } else {
    const s32 t = static_cast<u16>(u->dot_tick) + g.frame_ticks;
    u->dot_tick = static_cast<s16>(t);
    if (0xB3 < static_cast<s16>(t)) {
      const s32 d = s->hp_max / 0x14;
      damage_popup_spawn(u, d, 0, 0);
      unit_damage_hp(u, d);
      u->dot_tick = 0;
    }
  }
  // The battle values come back from the equipped ones, then the buffs apply.
  s->atk[2] = s->atk[1];
  s->def[2] = s->def[1];
  s->eva[2] = s->eva[1];
  s->acc[2] = s->acc[1];
  s->guts[2] = s->guts[1];
  s->intel[2] = s->intel[1];
  u->run_speed = u->base_speed;
  const u32 b = u->status_bits;
  if (b & 0x1000) {
    const s16 v = s->atk[2];
    s->atk[2] = static_cast<s16>(v + ((v - (v >> 15)) >> 1));
  } else if (b & 0x2000) {
    s32 v = s->atk[2];
    if (v < 0) v += 3;
    s->atk[2] = static_cast<s16>(s->atk[2] + (v >> 2));
  }
  if (b & 0x500) {
    s32 v = s->def[2];
    if (v < 0) v += 3;
    s->def[2] = static_cast<s16>(s->def[2] + (v >> 2));
    if (b & 0x2000) s->eva[2] = static_cast<s16>(s->eva[2] + 0x40);
  } else if (b & 0x2000) {
    s->def[2] = static_cast<s16>(s->def[2] + s->def[2] / 6);
    s->eva[2] = static_cast<s16>(s->eva[2] + 0x40);
  }
  if (b & 0x4000) {
    s->acc[2] = static_cast<s16>(s->acc[2] + 0x60);
  } else if (b & 0x2000) {
    s->acc[2] = static_cast<s16>(s->acc[2] + 0x30);
  }
  if (b & 0x200) {
    s32 v = s->eva[2];
    if (v < 0) v += 3;
    s->eva[2] = static_cast<s16>(s->eva[2] - (v >> 2));
  }
  if (u->status_bits & 8) {
    u->colour_mode = 1;
    u->rgb[1] = 0x60;
    u->rgb[0] = 0;
    u->rgb[2] = 0xA8;
    if (rng15() < 0xC0) {
      u->colour_mode = 0;
      u->status_bits &= ~8u;
      u->status_recovered();
    }
  }
  if (u->status_bits & 0x40) {
    u->colour_mode = 1;
    u->rgb[0] = 0x90;
    u->rgb[1] = 0x70;
    u->rgb[2] = 0;
    if (rng15() < 0xC0) {
      u->colour_mode = 0;
      u->status_bits &= ~0x40u;
      u->status_recovered();
    }
  }
  if (u->status_bits & 1) {
    const s16 v = s->def[2];
    s->def[2] = static_cast<s16>((v - (v >> 15)) >> 1);
  }
  if (u->status_bits & 0x20) u->run_speed >>= 1;
  if (u->status_test(0, 0x20) != 0 || u->status_test(3, 0x400) != 0) {
    u->run_speed <<= 1;
  } else if (u->status_test(2, 0x40000000) != 0) {
    u->run_speed += u->run_speed >> 2;
  } else if (u->is_enemy == 0 && g.config[0x4D] != 0) {
    s16 pct;
    std::memcpy(&pct, g.config + 400, 2);
    u->run_speed += (pct * u->run_speed) / 100;
  } else if (u->status_bits & 0x800) {
    u->run_speed += u->run_speed / 2;
  }
}

// ---- PartyUnit virtuals ----------------------------------------------------------------

int PartyUnit::start_reaction() {
  if (reaction == 2) {
    // pc_start_reaction 0x8003bb84: equipment that throws weapons when the member is knocked back.
    if (status_test(2, 0x4000) != 0) {
      talk_trace("throw", this, 0);
      unit_throw_weapons(this, 0);
    }
    if (status_test(2, 0x2000) != 0) {
      talk_trace("throw", this, 2);
      unit_throw_weapons(this, 2);
    }
  }
  return unit_start_reaction(this);
}

// 0x8003adc8 pc_update_effects (slot 10): the status update, then while fighting and
// unpaused the equipment / talent effects on the battle stats (which are rebuilt every
// frame): fury and the attack boosts, the drains, the regenerations and the timed effects.
void PartyUnit::update_status_effects() {
  BattleGlobals& g = bg();
  unit_update_status_effects(this);
  if (g.state != kStateFighting || g.flags != 0) return;
  Stats* s = st();
  const s32 t = g.frame_ticks;
  if ((s->status & 0x10) == 0) {
    if (status_test(0, 0x8000000) != 0) s->atk[2] = static_cast<s16>(s->atk[2] << 1);
  } else {
    // Fury: double attack until +0x22c runs out.
    s->atk[2] = static_cast<s16>(s->atk[2] << 1);
    fury_ticks -= t;
    if (fury_ticks < 0) {
      fury_ticks = 0;
      s->status = static_cast<u8>(s->status & 0xEF);
    }
  }
  if (status_test(2, 0x80000000) != 0 || status_test(3, 0x40) != 0) {
    s->atk[2] = static_cast<s16>(s->atk[2] + (s->atk[2] >> 1));
  } else if (status_test(3, 1) != 0) {
    s->atk[2] = static_cast<s16>(s->atk[2] * 0xE / 10);
  }
  if (status_test(0, 0x4000000) != 0) {
    special_timer = 6;
    unit_apply_stun(this);
  }
  if (0 < s->mp && status_test(2, 0x100) != 0) {
    // MP drain: 2 MP every 2 ticks.
    special_timer = 6;
    mp_drain_ticks = static_cast<s16>(mp_drain_ticks + t);
    if (1 < mp_drain_ticks) {
      unit_damage_mp(this, mp_drain_ticks << 1);
      mp_drain_ticks = 0;
    }
  }
  if (0 < s->hp && status_test(2, 0x1000) != 0) {
    special_timer = 6;
    unit_damage_hp(this, t * 0xC);
  }
  if (status_test(2, 0x2000000) != 0) {
    s->guts[2] = static_cast<s16>(s->guts[2] + (s->guts[2] >> 1));
    if (0xFE < s->guts[2]) s->guts[2] = 0xFF;
  }
  if (status_test(2, 0x10000000) != 0) s->eva[2] = static_cast<s16>(s->eva[2] + 0x40);
  if (status_test(2, 0x20000000) != 0) {
    s->intel[2] = static_cast<s16>(s->intel[2] + s->intel[2] / 3);
    effect_timer[0] = static_cast<s16>(effect_timer[0] + t);
    if (0x708 < effect_timer[0]) clear_status(2, 0x20000000);
  }
  s->eva[2] = static_cast<s16>(s->eva[2] + eva_bonus);
  regen_ticks = static_cast<s16>(regen_ticks + t);
  if (300 < regen_ticks) {
    regen_ticks = static_cast<s16>(regen_ticks - 300);
    if (status_test(0, 0x80) != 0 || status_test(3, 0x10) != 0) {
      const s32 heal = -s->hp_max / 0x21;
      unit_damage_hp(this, heal);
      damage_popup_spawn(this, heal, 0, 0);
    }
    if (status_test(0, 0x10000) != 0) {
      const s32 heal = -static_cast<s32>(s->mp_max) / 0x32;
      unit_damage_mp(this, heal);
      damage_popup_spawn(this, heal, 0, 1);
    }
  }
  regen_mp_ticks = static_cast<s16>(regen_mp_ticks + t);
  if (600 < regen_mp_ticks) {
    regen_mp_ticks = static_cast<s16>(regen_mp_ticks - 600);
    if (status_test(1, 0x40000) != 0 || status_test(1, 0x10000000) != 0 || status_test(2, 8) != 0) {
      const s32 heal = -static_cast<s32>(s->mp_max) / 0x32;
      unit_damage_mp(this, heal);
      damage_popup_spawn(this, heal, 0, 1);
    }
  }
  if (status_test(3, 2) != 0) {
    effect_timer[1] = static_cast<s16>(effect_timer[1] + t);
    if (0x708 < effect_timer[1]) clear_status(3, 2);
  }
  if (status_test(3, 4) != 0 || status_test(3, 0x800) != 0) {
    effect_timer[2] = static_cast<s16>(effect_timer[2] + t);
    if (0x5DC < effect_timer[2]) {
      clear_status(3, 4);
      clear_status(3, 0x800);
    }
  }
}

void PartyUnit::set_effect_timer(int which, s16 ticks) {
  if (which == 2) effect_timer[1] = ticks;
  else if (which == 1) effect_timer[0] = ticks;
  else if (which == 3) effect_timer[2] = ticks;
}

void PartyUnit::accessory_proc(int item, int word, u32 mask, int chance) {
  if (rng_range(100) >= static_cast<u32>(chance)) return;
  bsfx_play_ui(0x30);
  // The accessory with this item id breaks (record +0x16 / +0x18).
  int which = 0;
  s16 a1, a2;
  std::memcpy(&a1, record + 0x16, 2);
  if (a1 == item) {
    const s16 z = 0;
    std::memcpy(record + 0x16, &z, 2);
    which = 1;
  }
  std::memcpy(&a2, record + 0x18, 2);
  if (a2 == item) {
    if (which == 0) {
      const s16 z = 0;
      std::memcpy(record + 0x18, &z, 2);
    }
    ++which;
  }
  if (which != 0 && word == 0 && mask == 0x20000) {
    Stats* s = st();
    s->hp_max = acc_hp_max[which - 1];
    s->mp_max = acc_mp_max[which - 1];
    if (s->hp_max < s->hp) s->hp = s->hp_max;
    if (s->mp_max < s->mp) s->mp = s->mp_max;
  }
  if (which == 1) effect_bits[word & 3] &= ~mask;
  // 16 confetti particles (the Particle pool is not ported) and the "broken" icon.
  for (int i = 0; i < 0x10; ++i) {
    rng_spread_fixed(8);
    rng_between_fixed(-0x10, -8);
  }
  if (Sprite* s = sprite_spawn_ui(0x38, 0x18, nullptr)) sprite_attach_screen(s, this, -0x18, 4);
}

void PartyUnit::clear_status(int word, u32 mask) { effect_bits[word & 3] &= ~mask; }
void PartyUnit::set_status(int word, u32 mask) { effect_bits[word & 3] |= mask; }

void PartyUnit::model_select(int n) {
  // pc_model_select 0x8003522c: alternate model set n (+0x240..) with the default set's
  // tpages / CLUTs and the alternate's killer-move blocks.
  if (n < 0 || n > 3 || model_sets[n + 1] == nullptr) return;
  animset = model_sets[n + 1];
  model_apply();
  AnimSet* a = animset;
  for (int i = 0; i < 4; ++i) {
    a->tpage[i] = model_sets[0]->tpage[i];
    a->clut[i] = model_sets[0]->clut[i];
  }
  a->layout = alt_layout[n];
  a->anims = alt_anims[n];
  a->sheets[3] = alt_sheet[n];
  a->frames[3] = alt_frames[n];
  a->clut[3] = alt_clut[n];
}

void PartyUnit::camera_follow_start() {
  BattleGlobals& g = bg();
  if (g.flags == 0 && g.controlled_slot == slot && camera_focus == 0 && g.camera_special == nullptr) {
    camera_end_scheduled_now();
    camera_request_mode(g.camera, 7, this);
    camera_focus = 1;
  }
}

void PartyUnit::think() {
  BattleGlobals& g = bg();
  if (g.effects_suppressed != 0) return;
  if (appear & 1) return;
  if (appear & 2) {
    appear_update();
    update_status_effects();
    return;
  }
  if (tex_timer != 0) {
    bobj_texcache_reset(this);
    if (g.flags == 0) {
      const s32 t = static_cast<u16>(tex_timer) - g.frame_ticks;
      tex_timer = static_cast<s16>(t);
      if (static_cast<s16>(t) < 0) tex_timer = 0;
    }
  }
  if (death != 0 || reaction != 0) {
    counter_ticks = 0;
    unit_release_spell_data(this);
    if (death != 0 || reaction == 1) {
      g.hud_member_flash[slot & 3] = 0xFF;  // g_hud +0x70 + slot * 4
      pc_record_counter_dec(this, 1);
    }
  }
  if (death == 1) {
    counters[3] = 0;
    if (Sprite* s = sprite_spawn_a(0x14, 1, nullptr)) {
      sprite_set_pos_v(s, pos);
      s->layer = 1;
    }
  }
  if (death == 0) {
    if (reaction == 1) {
      // Hurt voices: crossing a quarter of the HP, else a random one.
      Stats* s = st();
      s32 q = s->hp_max;
      if (q < 0) q += 3;
      int v;
      if ((q >> 2) < hp_at_voice && s->hp <= (q >> 2)) {
        v = 0x12;
      } else if (rng15() < 0x4000) {
        v = 0xE;
      } else if (rng15() < 22000) {
        v = 0xF;
      } else {
        v = rng15() < 20000 ? 0x10 : 0x11;
      }
      play_voice(v);
      hp_at_voice = static_cast<s16>(st()->hp);
    }
  } else {
    play_voice(0x13);
    voice_ally_reaction(this);  // 0x80080974 (party_talk.cpp): an ally calls out, in a fury
    status_bits = 0;
    effect_bits[2] &= 0x9FFFFFF;
    pc_camera_follow_end(this);
  }
  if (g.flags == 0 && action != 0xE && wait_after_spell != 0) {
    const s32 t = wait_after_spell - g.frame_ticks;
    wait_after_spell = static_cast<s16>(t);
    if (static_cast<s16>(t) < 1) {
      wait_after_spell = 0;
      ai_spell = 0;
    }
  }
  start_reaction();
  if (action == 5) {
    // Knocked out: auto-revive accessories after 60 ticks, else lying down.
    if (low_hp_marker != nullptr) {
      sprite_free(low_hp_marker);
      low_hp_marker = nullptr;
    }
    if (status_test(2, 0x8000000) == 0) {
      if (status_test(0, 0x40) != 0 && g.flags == 0 && g.state == kStateFighting) {
        counters[3] += g.frame_ticks;
        if (0x3C < counters[3]) {
          accessory_proc(0x3D, 0, 0x40, 99);
          st()->status &= 0xFE;
          const s32 d = -st()->hp_max / 10;
          unit_damage_hp(this, d);
          damage_popup_spawn(this, d, 0, 0);
          action_kind = 0;
          begin_action();
          return;
        }
      }
    } else if (g.flags == 0 && g.state == kStateFighting) {
      counters[3] += g.frame_ticks;
      if (0x3C < counters[3]) {
        bsfx_play_at(0x17, this);
        clear_status(2, 0x8000000);
        st()->status &= 0xFE;
        const s32 d = -st()->hp_max;
        unit_damage_hp(this, d);
        damage_popup_spawn(this, d, 0, 0);
        action_kind = 0;
        begin_action();
        return;
      }
    }
    model_default();
    s32 fx = facing[0];
    anim = 7;
    facing[2] = facing[1] = 0;
    if (fx < 0) fx = -fx;
    ai_spell = 0;
    if (fx < 0x1000) facing[0] = static_cast<s32>(0xFFFFF000);
    animate(0);
    return;
  }
  if (action == 6) {
    if (low_hp_marker != nullptr) {
      sprite_free(low_hp_marker);
      low_hp_marker = nullptr;
    }
    update_vanish();
    animate(0);
    return;
  }
  {
    s32 q = st()->hp_max;
    if (q < 0) q += 3;
    if (st()->hp < (q >> 2) && !bobj_on_screen(this) && g.state == kStateFighting) {
      // pc_draw_low_hp_marker 0x80037b00: an arrow to the member off screen.
      u32 k = static_cast<u32>(g.battle_frames & 0x7F);
      if (0x3F < k) k = 0x7F - k;
      ui_draw_target_arrow(&low_hp_marker, screen_x, screen_y, k << 16 | static_cast<s32>(k) >> 1 | (k + 0x40) * 0x200,
                           0x21);
    } else if (low_hp_marker != nullptr) {
      sprite_free(low_hp_marker);
      low_hp_marker = nullptr;
    }
  }
  update_status_effects();
  if (static_cast<u8>(action - 3) < 2) {
    attack_recovery = 0;
    busy = 1;
    state_knockback();
    return;
  }
  const u8 status = st()->status;
  if (status & 0x40) {
    // Stunned.
    model_default();
    anim = 5;
    loop_limit = 0;
    busy = 1;
    animate(0);
    return;
  }
  if ((status & 0xE6) == 0 && (status_bits & 0x48) == 0 && super_armour == 0) {
    if (static_cast<u8>(action - 0x10) < 0xB) model_select(killer_index);
    switch (action) {
      case 1: state_approach(); return;
      case 2: state_leap(); return;
      case 7: state_victory(); return;
      case 8: state_flee(); return;
      case 9:
      case 0xC: state_attack(); return;
      case 0xA:
      case 0xB: state_air_attack(); return;
      case 0xE: state_cast(); return;            // [40] (spell_units.cpp)
      case 0xF: state_spell_release(); return;   // [41]
      case 0x10: case 0x11: case 0x12: case 0x13: case 0x14: case 0x15:
      case 0x16: case 0x17: case 0x18: case 0x19: case 0x1A:
        killer_move_state(action - 0x10);
        return;
      default: break;
    }
    // Idle: commands from the input / AI.
    if (g.party_escaping != 0) {
      action = 8;
      busy = 1;
      command = 0;
    }
    const u8 cmd = command;
    if (cmd != 0) {
      if (cmd == 1) {
        action = 1;
        move_mode = 0;
        start_approach();
        busy = 1;
        camera_focus = 0;
        action_kind = 1;
        chain = 0;
        approach_side = 0;
      } else if (cmd == 2 || cmd == 3) {
        target_far = static_cast<s16>(unit_target_is_far(this, target));
        killer_index = static_cast<s16>((cmd - 2) + combo * 2);
        const int ri = combo * 4 + cmd * 2 + target_far;  // s16 index from +0x2d6
        reach = ri >= 4 && ri < 12 ? killer_range[ri - 4] : 0;
        const u8 move = record != nullptr ? record[combo * 2 + cmd + 0xCA] : 0;
        busy = 1;
        camera_focus = 0;
        action_kind = static_cast<u8>(move + 6);
        if (reach == 0) {
          if (target != nullptr) facing[0] = pos[0] < target->pos[0] ? static_cast<s32>(0xFFF9C000) : 0x64000;
          begin_action();
        } else {
          action = 1;
          move_mode = 0;
          start_approach();
        }
      } else if (cmd == 4) {
        unit_begin_cast(this);
        busy = 1;
        camera_focus = 0;
      }
      command = 0;
    }
    if (3 < g.state && g.party_escaping == 0 && g.outcome == kVictory) {
      animate(0);
      action = 7;
      blend = 0;
      colour_mode = 0;
      counters[0] = 0;
      if (victory_voice != 0) voice_play_entry(victory_voice, this, -1);  // 0x800360a8 (preloaded by voice_pick_victory)
      bobj_anim_reset(this);
      if (g.victory_speaker != slot) return;
      camera_set_mode(g.camera, 6, this);
      return;
    }
    model_default();
    if ((appear & 4) == 0) {
      anim = 0;
      if (talk_voice != 0) anim = fury_ticks != 0 ? 0x10 : 0xF;
    }
    if (action == 0 && super_armour == 0) {
      static const s32 kZero[3] = {0, 0, 0};
      Unit* blocker = unit_check_move(this, kZero, 7);
      if (blocker == nullptr) {
        ignore_units = 0;
        stuck_ticks = 0;
        pc_ai_update(this);
      } else {
        const s32 t = static_cast<u16>(stuck_ticks) + g.frame_ticks;
        stuck_ticks = static_cast<s16>(t);
        if (0x1D < static_cast<s16>(t)) {
          leap_dir = static_cast<u8>(unit_start_sidestep(this, blocker));
          if (leap_dir != 0) {
            facing[1] = 0;
            facing[2] = 0;
            leaping = 1;
            const s32 h = stage_ground_height(step_dest[0] >> 12, step_dest[2] >> 12);
            vel[0] /= 0x12;
            vel[2] /= 0x12;
            vel[1] = (h - pos[1]) / 0x12 - 0x9000;
          }
          ignore_units = 1;
        }
      }
    }
    animate(0);
    return;
  }
  // Incapacitated (status 0xe6 / buff bits 0x48) or in super armour.
  if (g.state != kStateFighting) anim = 0;
  busy = 1;
  if (super_armour != 0) {
    const s32 t = super_armour - g.frame_ticks;
    super_armour = static_cast<s16>(t);
    if (static_cast<s16>(t) < 1) {
      interrupt();
      jumping = 0;
      super_armour = 0;
      busy = 0;
      action_kind = 0;
      begin_action();
      ignore_units = 0;
      target = nullptr;
      release_effects();
    }
  }
  animate(1);
}

void PartyUnit::begin_action() {
  if (target != nullptr && !unit_is_down(target)) {
    face_target(this);
    facing[1] = 0;
    facing[2] = 0;
  }
  straight = 0;
  u8 next = 0;
  switch (action_kind) {
    case 1:
      pc_record_counter_dec(this, 1);
      next = 9;
      break;
    case 2:
    case 3:
      pc_record_counter_dec(this, 1);
      action = static_cast<u8>(action_kind + 8);
      animate(0);
      bobj_anim_reset(this);
      layer = 1;
      pc_voice_attack_shout(this);
      return;
    case 4:
      pc_record_counter_dec(this, 1);
      next = 0xC;
      break;
    case 6:
      pc_record_counter_dec(this, 3);
      battle_message_show(spell + 999);
      action = 0xF;
      counters[0] = 0;
      counters[3] = 0;
      animate(0);
      bobj_anim_reset(this);
      return;
    case 7: case 8: case 9: case 0xA: case 0xB: case 0xC: case 0xD: case 0xE: case 0xF: case 0x10: case 0x11: {
      // A killer move: its voice (slot 39), the MP cost (slot 36), state kind + 9
      // (0x10..0x1a, run by the character module's slot 37) and the use counter.
      if (target == nullptr) break;
      pc_record_counter_dec(this, 2);
      play_move_voice(killer_index);
      const int cost = move_mp_cost(killer_index);
      vel[0] = vel[1] = vel[2] = 0;
      if (st()->mp < cost) break;
      half_damage = 0;
      st()->mp = static_cast<s16>(st()->mp - cost);
      action = static_cast<u8>(action_kind + 9);
      u8* uses = record + 0x7E + action_kind * 2;
      s16 n;
      std::memcpy(&n, uses, 2);
      if (n < 999) {
        ++n;
        std::memcpy(uses, &n, 2);
      }
      animate(0);
      bobj_anim_reset(this);
      counters[1] = counters[2] = counters[3] = 0;
      loop_limit = 1;
      layer = 1;
      ++action_count;
      return;
    }
    case 0x12:
      if (jumping != 0) jumping = 0;
      release_effects();
      action_kind = 0;
      if (combo == 0) {
        if (killer_index < 2 && record != nullptr && record[killer_index + 0xCE] != 0) {
          combo = 1;
          command = static_cast<u8>(killer_index + 2);
        }
      } else {
        combo = 0;
      }
      break;
    default: break;
  }
  if (next == 0) {
    unit_release_spell_data(this);
    busy = 0;
    action = 0;
    pc_camera_follow_end(this);
    bobj_anim_reset(this);
    anim = 0;
    animate(0);
    return;
  }
  action = next;
  animate(0);
  bobj_anim_reset(this);
  loop_limit = 1;
  half_damage = 0;
  layer = 1;
  pc_voice_attack_shout(this);
  ++action_count;
}

void PartyUnit::start_approach() {
  unit_begin_approach(this);
  Vec4 ap{step_dest[0], step_dest[1], step_dest[2], step_dest[3]};
  const bool ok = unit_find_approach_point(this, &ap);
  if (ok) {
    step_dest[0] = ap.x;
    step_dest[1] = ap.y;
    step_dest[2] = ap.z;
    step_dest[3] = ap.w;
  }
  heading = static_cast<s16>(ratan2(step_dest[2] - pos[2], step_dest[0] - pos[0]) & 0xFFF);
  pc_record_counter_dec(this, 1);
  const s32 d = vec_distance_fx(pos, step_dest);
  if (!(static_cast<u32>(d - 0xA001) < 0x175FFF && ok)) return;
  if (target != nullptr && (target->airborne != 0 || target->flying != 0)) return;
  leap_dir = 0;
  const u32 h = static_cast<u16>(heading);
  if (0xC00 < h - 0x200 && target != nullptr && target->pos[0] < pos[0]) leap_dir = 1;
  if (h - 0x600 < 0x38F && target != nullptr && pos[0] < target->pos[0]) leap_dir = 1;
  if (h - 0x200 < 0x400) leap_dir = 2;
  if (h - 0x98F < 0x472) leap_dir = 3;
  if (leap_dir == 0) return;
  vel[0] = bcos(static_cast<u32>(heading)) * run_speed >> 12;
  vel[2] = bsin(static_cast<u32>(heading)) * run_speed >> 12;
  const Vec4 pr = unit_probe_path(this, vel[0], vel[2], d);
  if (pr.x != kPathClear) return;
  // A leap toward the approach point (14 ticks).
  action = 2;
  leaping = 0;
  leap_ticks = 0xE;
  const s32 gh = stage_ground_height(step_dest[0] >> 12, step_dest[2] >> 12);
  vel[1] = (gh - pos[1]) / 0xE - 0x9000;
  vel[0] = (bcos(static_cast<u32>(heading)) * (d >> 12)) / 0xE;
  vel[2] = (bsin(static_cast<u32>(heading)) * (d >> 12)) / 0xE;
  bobj_anim_reset(this);
}

void PartyUnit::state_approach() {
  BattleGlobals& g = bg();
  model_default();
  if (airborne == 1) {
    animate(0);
    return;
  }
  Unit* t = target;
  bool reached = false;
  jumping = 0;
  bool jump = false;
  if (t != nullptr && (t->flying != 0 || t->airborne != 0) && t->pos[1] < jump_threshold + bobj_ground_height(this) &&
      action_kind < 7 && -0x190001 < pos[1])
    jump = true;
  if (g.flags == 0) {
    camera_follow_start();
    const int r = unit_move_toward_goal(this, 1);
    if (r != 0) {
      if (r == 2) action_kind = 0;
      if (!jump || action_kind != 1) {
        if (pc_try_ambush_skill(this) != 0) return;
        begin_action();
        return;
      }
      reached = true;
    }
    if (jump && target != nullptr) {
      const s32 tj = jump_time(v234, target->pos) - 0x18000;  // vtable [6]
      Vec4 ap;
      unit_find_approach_point(this, &ap);
      const Vec4 dir = vec_direction_xz(pos, target->pos);
      vel[0] = dir.x;
      vel[1] = dir.y;
      vel[2] = dir.z;
      extra34 = dir.w;
      const s32 d = vec_distance_xz_fx(pos, ap.data());
      auto scaled = [&](s32 v, int shift) {
        s32 p = v * run_speed;
        if (p < 0) p += (1 << shift) - 1;
        return p >> shift;
      };
      if (0x80000 < d && d <= tj && tj <= last_dist && straight == 0) {
        vel[0] = scaled(vel[0], 12);
        vel[1] = scaled(vel[1], 12);
        vel[2] = scaled(vel[2], 12);
        const s16 h = size[1];
        size[1] = static_cast<s16>(target->pos[1] >> 12);
        const Vec4 pr = unit_probe_path(this, vel[0], vel[2], d);
        size[1] = h;
        if (unit_can_reach_target(this, target, 0) != 0 && pr.x == kPathClear) {
          action_kind = 2;
          vel[1] = v234 * -0x1000;
          begin_action();
          return;
        }
      }
      const s32 tr = unit_jump_rise_time(this, v234, target->pos);
      if (reached || (d <= tr && tr <= last_dist && straight == 0)) {
        vel[0] = scaled(vel[0], 14);
        vel[1] = scaled(vel[1], 14);
        vel[2] = scaled(vel[2], 14);
        const s16 h = size[1];
        size[1] = static_cast<s16>(target->pos[1] >> 12);
        const Vec4 pr = unit_probe_path(this, vel[0], vel[2], d);
        size[1] = h;
        if (unit_can_reach_target(this, target, 0) != 0 && pr.x == kPathClear) {
          action_kind = 3;
          vel[1] = v234 * -0x1000;
          begin_action();
          return;
        }
      }
      if (d < tr) {
        Unit* tt = target;
        if (size[1] * -0x4000 - 0x30000 < tt->pos[1]) {
          const s32 dd[3] = {tt->pos[0] - pos[0], 0, tt->pos[2] - pos[2]};
          if (unit_check_move(this, dd, 7) == nullptr) {
            straight = 1;
            action_kind = 4;
          }
        }
      }
      last_dist = d;
    }
  }
  anim = 1;
  animate(0);
}

void PartyUnit::state_leap() {
  BattleGlobals& g = bg();
  model_default();
  jumping = 1;
  airborne = 1;
  if (g.flags == 0) {
    if (g.controlled_slot == slot && camera_focus == 0) {
      camera_end_scheduled_now();
      camera_set_mode(g.camera, 7, this);
      camera_focus = 1;
    }
    if (unit_try_move_scaled(this, vel, 7) == nullptr || leaping != 0) {
      bobj_move(this, vel);
      vel[1] += g.frame_ticks * 0x1000;
      leap_ticks -= g.frame_ticks;
      if (0 < leap_ticks) {
        face_target(this);
        anim = static_cast<u16>(leap_dir + 1);
        animate(0);
        return;
      }
      jumping = 0;
      airborne = 0;
      pos[1] = bobj_ground_height(this);
      bsfx_play_at(0x1E, this);
      for (int i = 0; i < 8; ++i) effect_dust_puff(pos, 4);
      bool done = true;
      if (target != nullptr) {
        Vec4 ap;
        unit_find_approach_point(this, &ap);
        s32 dx = ap.x - pos[0];
        if (dx < 0) dx = -dx;
        s32 dz = ap.z - pos[2];
        if (dz < 0) dz = -dz;
        if (dx <= run_speed / 2 && dz <= run_speed / 2) {
          pos[0] = ap.x;
          pos[2] = ap.z;
        } else {
          done = false;
        }
      }
      if (done) {
        if (pc_try_ambush_skill(this) == 0) {
          begin_action();
          return;
        }
      }
    } else {
      jumping = 0;
    }
    // Not there yet: back to running.
    action = 1;
    unit_begin_approach(this);
    busy = 1;
    camera_focus = 0;
    anim = 1;
    animate(0);
    return;
  }
  face_target(this);
  anim = static_cast<u16>(leap_dir + 1);
  animate(0);
}

void pc_attack_animate(PartyUnit* u) {
  u->model_default();
  u->anim = static_cast<u16>(u->action + 2);
  if (static_cast<s16>(u->animate(0)) == -1) {
    u->action_kind = 0;
    u->begin_action();
    return;
  }
  if (u->hit_landed != 0) {
    // Multi-hit talents re-arm the swing.
    bool again = false;
    if (u->status_test(0, 0x2000000) != 0 && u->half_damage < 2) again = true;
    else if (u->status_test(1, 0x200) != 0 && u->half_damage == 0) again = true;
    if (again) {
      u->hit_landed = 0;
      ++u->half_damage;
    }
  }
  if ((u->box_mask & 0xC) == 0) {
    if (u->chain == 1 && u->hit_landed != 0) u->chain = 2;
  } else if (u->chain == 0) {
    u->chain = 1;
  }
  if (u->chain == 0) return;
  if (u->hit_landed != 0 && u->chain < 10) u->chain = static_cast<u8>(u->chain + bg().frame_ticks);
}

void pc_attack_hit_check(PartyUnit* u) {
  if (bg().flags != 0) return;
  Unit* units[8] = {};
  Vec4 points[8];
  const int n = collider_attack_overlaps(u, units, points, 0);
  if (n == 0 || u->hit_landed != 0) return;
  s32 angle = 9999;
  const u32 lvl = static_cast<u32>(u->skill_level(0x2E));
  if (rng_range(0x19) < lvl) {
    angle = u->facing[0] < 0 ? 0xB8E : -0x38E;
    effect_skill_popup(u, 0x2E);
  }
  unit_attack_targets(u, n, units, points, 0x100, angle, 0);
  if (u->status_test(1, 0x800000) != 0) {  // 0x80037784: equipment that throws weapons on a hit
    talk_trace("throw", u, 0);
    unit_throw_weapons(u, 0);
  }
}

void PartyUnit::state_attack() {
  pc_attack_animate(this);
  pc_attack_hit_check(this);
}

// 0x80037d08 pc_state_air_attack (slot 31, states 10 / 11): from frame +0x2d8 (state 10) /
// +0x2da (11) the member is airborne: vertical move with gravity (blocked: the move ends),
// horizontal move, landing ends it; hits enemies on the way (power 0x100); the frame
// follows the height (+0x234 the jump height).
void PartyUnit::state_air_attack() {
  BattleGlobals& g = bg();
  model_default();
  anim = static_cast<u16>(action + 2);
  const s32 limit = action == 0xA ? v2d8 : v2da;
  if (limit <= frame && g.flags == 0) {
    jumping = 1;
    airborne = 1;
    s32 d[3] = {0, vel[1], 0};
    if (unit_try_move_scaled(this, d, 7) != nullptr) {
      jumping = 0;
      action_kind = 0;
      begin_action();
      return;
    }
    bobj_move(this, d);
    vel[1] += g.frame_ticks << 12;
    d[0] = vel[0], d[1] = 0, d[2] = vel[2];
    if (unit_try_move_scaled(this, d, 7) == nullptr) bobj_move(this, d);
    const s32 gy = bobj_ground_height(this);
    if (!(pos[1] < gy)) {
      pos[1] = gy;
      jumping = 0;
      airborne = 0;
      action_kind = 0;
      begin_action();
      return;
    }
    Unit* found[8] = {};
    Vec4 pts[8] = {};
    const int n = collider_attack_overlaps(this, found, pts, 0);
    if (n != 0 && hit_landed == 0) unit_attack_targets(this, n, found, pts, 0x100, 9999, 0);
    s32 h = (v234 << 12) + vel[1];
    const s32 top = v234 << 13;
    if (!(h < top)) h = top;
    const s32 t = bobj_anim_ticks(this, limit, 1000);
    const s32 num = static_cast<s32>(static_cast<u32>(t) * static_cast<u32>(h));
    // (A zero jump height divides by zero; the R3000 then leaves -1 / 1 in LO.)
    const s32 q = top == 0 ? (num >= 0 ? -1 : 1) : (num == INT32_MIN && top == -1 ? num : num / top);
    ticks_left = static_cast<s16>(-q + 1);
    frame = static_cast<s8>(limit - 1);
    if (frame < 0) frame = 0;
  }
  animate(0);
  attack_recovery = 0;
}

void PartyUnit::state_knockback() {
  model_default();
  ai_spell = 0;
  counter_ticks = static_cast<u8>(counter_ticks + bg().frame_ticks);
  anim = static_cast<u16>(action + 2);
  if (bg().flags == 0 && unit_knockback_slide(this)) {
    action = 0;
    busy = 0;
    pc_camera_follow_end(this);
  }
  animate(0);
}

void PartyUnit::state_victory() {
  model_default();
  anim = 8;
  animate(0);
}

void PartyUnit::state_flee() {
  BattleGlobals& g = bg();
  facing[0] = pos[0] < 0 ? static_cast<s32>(0xFFF9C000) : 0x64000;
  facing[2] = facing[1] = 0;
  model_default();
  if (g.state != kStateFighting) {
    action_kind = 0;
    flee_timer = 0;
    begin_action();
    return;
  }
  if (g.flags == 0) {
    if (flee_timer < 1) {
      const s32 sp = facing[0] < 0 ? -run_speed : run_speed;
      vel[0] = sp / 2;
      vel[1] = 0;
      vel[2] = 0;
      bobj_move(this, vel);
      blend = 2;
      colour_mode = 2;
      rgb[0] = static_cast<u8>(static_cast<u8>(flee_timer) * 2 + '`');
      if (flee_timer < -0x2E) rgb[0] = 0;
      if (flee_timer < -0x2F) removed = 2;
    }
    if (flee_voice == 0 && flee_timer < 0x3D) {
      flee_voice = 1;
      voice_play_extra(this);  // 0x80080878 (party_talk.cpp)
    }
    flee_timer = static_cast<s16>(flee_timer - g.frame_ticks);
  }
  anim = 1;
  animate(0);
}

void pc_ai_update(PartyUnit* u) {
  BattleGlobals& g = bg();
  if ((u->status_bits & 0x10) == 0) {
    const bool ai = u->status_test(3, 0x2000) == 1 || g.config[0x181] == 1 || g.controlled_slot != u->slot;
    if (ai && u->airborne != 1) {
      const u8 k = u->st()->klass;
      if (k < 9) pc_ai_fighter(u);
      else if (k == 9) pc_ai_healer(u);
      else if (k <= 0xC) pc_ai_caster(u);
    }
  } else {
    // Forced to move (+0x11c bit 4): a run to a random point within 0x300.
    u->status_bits &= ~0x10u;
    if (!unit_pick_random_destination(u, 0x300, 0)) {
      u->move_mode = 1;
      u->action = 1;
      u->start_approach();
      u->action_kind = 0;
    }
  }
}

}  // namespace ss::game::battle
