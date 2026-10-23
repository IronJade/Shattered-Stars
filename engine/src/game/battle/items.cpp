// Battle item use (#1984.1 0x80082dec-0x80085517): item_use_battle and its helpers, and
// the item Shot kinds 0x2e (the healing / curing effect), 0x2f (balls) and 0x30 (bombs).
// An item works at once: its effect kind (g_item_effects +0x80 + item) picks a case, the
// power (+0x480 + item) is its strength. The helpers work on the globals g_item_user /
// g_item_target (0x80085510 / 0x80085514) and the target iterator 0x8008550c.

#include "game/battle/items.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "game/battle/battle.h"
#include "game/battle/bsfx.h"
#include "game/battle/camera.h"
#include "game/battle/combat.h"
#include "game/battle/command_menu.h"
#include "game/battle/enemy_support.h"
#include "game/battle/fx_draw.h"
#include "game/battle/particle.h"
#include "game/battle/render.h"
#include "game/battle/shot.h"
#include "game/battle/spell_fx.h"
#include "game/battle/spell_windows.h"
#include "game/battle/sprite.h"
#include "game/battle/unit.h"
#include "game/battle/unit_move.h"
#include "game/boot/rng.h"
#include "game/field/script_vm.h"
#include "game/title/new_game.h"

namespace ss::game::battle {

namespace {

Unit* g_item_user = nullptr;    // 0x80085510
Unit* g_item_target = nullptr;  // 0x80085514
s8 g_item_iter = -1;            // 0x8008550c: the last unit index item_next_*_target gave

const s32 kUnitScale[3] = {0x1000, 0x1000, 0x1000};  // g_unit_scale 0x8006e34c
const s32 kDebrisSpread[3] = {0x30000, 0x30000, 0x18000};

// The R3000's div: no trap on a zero divisor (LO = -1 for a positive dividend, else 1).
s32 mips_div(s32 a, s32 b) {
  if (b == 0) return a < 0 ? 1 : -1;
  return a / b;
}

// A unit's chest (feet - height / 2) and its feet on the ground.
void unit_mid(const Unit* u, s32 out[4]) {
  out[0] = u->pos[0];
  out[1] = u->pos[1] + u->size[1] * -0x2000;
  out[2] = u->pos[2];
  out[3] = 0;
}
void unit_ground(const Unit* u, s32 out[4]) {
  out[1] = bobj_ground_height(u);
  out[0] = u->pos[0];
  out[2] = u->pos[2];
  out[3] = 0;
}

// 0x800848ec: the next standing party member after the iterator as the target; 1 when none.
int item_next_party_target() {
  BattleGlobals& g = bg();
  do {
    g_item_iter = static_cast<s8>(g_item_iter + 1);
    if (3 < g_item_iter) return 1;
  } while (unit_is_down(g.party_units[static_cast<size_t>(g_item_iter)]));
  g_item_target = g.party_units[static_cast<size_t>(g_item_iter)];
  return 0;
}

// 0x80084988: the same over the 8 enemies.
int item_next_enemy_target() {
  BattleGlobals& g = bg();
  do {
    g_item_iter = static_cast<s8>(g_item_iter + 1);
    if (7 < g_item_iter) return 1;
  } while (unit_is_down(g.enemy_units[static_cast<size_t>(g_item_iter)]));
  g_item_target = g.enemy_units[static_cast<size_t>(g_item_iter)];
  return 0;
}

// 0x80084860: a kind-0x2e Shot on the target (+0x128 HP %, +0x134 MP %, +0x130 status
// flags), then the target may thank the user.
void item_spawn_heal(s32 hp_pct, s32 mp_pct, u32 flags) {
  if (Shot* s = shot_spawn(0x2E)) {
    s->p128 = hp_pct;
    s->p134 = mp_pct;
    s->p130 = static_cast<s32>(flags);
    s->owner = g_item_user;
    s->target = g_item_target;
  }
  voice_thank_healer(g_item_user, g_item_target);
}

// 0x80084a24: n debris bursts (CLUT clut) at the target's chest, n - 1 spark rings (kind
// sparks) on the ground, sound 0x12 if asked, and the sparkle.
void item_fx_burst(int n, int clut, int sparks, int sound) {
  for (int i = 1; i <= n; ++i) {
    s32 mid[4];
    unit_mid(g_item_target, mid);
    effect_debris_burst(mid, kDebrisSpread, 4, 1, 1, i * 0x1000, static_cast<u8>(clut));
  }
  for (int i = 1; i < n; ++i) {
    s32 at[4];
    unit_ground(g_item_target, at);
    effect_spawn_sparks_at(at, 10, 0x10, 0x90 + (i - 1) * 0x30, -9 - i, 0x3C, sparks, 0, i * -0x10 + 0x154, 0);
  }
  if (sound != 0) bsfx_play_at(0x12, g_item_target);
  item_fx_sparkle(g_item_target);
}

// 0x80084bbc: n debris bursts at the target's chest and 12n streaks out of it (rgb), sound
// 0x24.
void item_fx_aura(int n, int clut, u32 rgb) {
  for (int i = 1; i <= n; ++i) {
    s32 mid[4];
    unit_mid(g_item_target, mid);
    effect_debris_burst(mid, kDebrisSpread, 3, 2, 1, i * 0x1000, static_cast<u8>(clut));
  }
  if (n != 0) {
    for (int i = 0; i < n * 12; ++i) {
      s32 mid[4];
      unit_mid(g_item_target, mid);
      const Vec4 p = vec_random_around(mid, 400);
      particle_spawn_homing_line(mid, p.data(), scale_colour(rgb, 0x20), rgb, 0x90, 0x9000, 0x80);
    }
  }
  bsfx_play_at(0x24, g_item_target);
}

// 0x80084dd8: status bit 4 (n dark streaks rising round the target, a ring) unless it has
// it or resists (roll kind 1 at chance); else the "missed" icon and sound 0xb.
void item_try_status_4(int n, int chance) {
  Unit* t = g_item_target;
  if ((t->status_bits & 4) == 0 && t->resist_roll(1, chance) != 0) {
    t->status_bits |= 4;
    item_fx_burst(1, 10, 0, 1);
    for (int i = 0; i < n; ++i) {
      t = g_item_target;
      s32 at[4];
      unit_ground(t, at);
      const Vec4 p = vec_random_in_disc_xz(at, t->size[0] * 3);
      const s32 top[4] = {p.x, p.y - 0x50000, p.z, 0};
      particle_spawn_homing_line(p.data(), top, 0x202020, 0x8000FF, 0xA0, 0x4000, 0x800);
    }
    s32 mid[4];
    unit_mid(g_item_target, mid);
    particle_spawn_ring(mid, 6, 0x30, 8, 0x3C, 0x202020, 0x800FF, 0x10, 0);
    return;
  }
  effect_rise_icon(g_item_target, 0);
  bsfx_play_ui(0xB);
}

// 0x80084fac: the target dies outright (HP 0, death flag) unless it resists (kind 0).
void item_try_instant_kill(int chance) {
  Unit* t = g_item_target;
  if (t->resist_roll(0, chance) == 0) {
    effect_rise_icon(g_item_target, 0);
    bsfx_play_at(0xB, g_item_target);
    return;
  }
  item_fx_aura(3, 5, 0x80C0F0);
  bsfx_play_at(0x50, g_item_target);
  g_item_target->st()->hp = 0;
  g_item_target->death = 1;
}

// 0x80085058 / 0x80085100: status bit 0x20 / 0x40 unless the target resists (kind 1).
void item_try_status_20(int chance) {
  if (g_item_target->resist_roll(1, chance) == 0) {
    effect_rise_icon(g_item_target, 0);
    bsfx_play_at(0xB, g_item_target);
    return;
  }
  item_fx_aura(2, 4, 0x80FF20);
  bsfx_play_at(10, g_item_target);
  g_item_target->status_bits |= 0x20;
}
void item_try_status_40(int chance) {
  if (g_item_target->resist_roll(1, chance) == 0) {
    effect_rise_icon(g_item_target, 0);
    bsfx_play_at(0xB, g_item_target);
    return;
  }
  item_fx_aura(2, 6, 0x40FFC0);
  bsfx_play_at(0x34, g_item_target);
  g_item_target->status_bits |= 0x40;
}

// 0x800851a8: n kind-0x2f balls (+0x128 mode) from the user's front, fanned 0xe3 apart
// round the line to the target.
void item_throw_projectiles(int n, int mode) {
  Unit* u = g_item_user;
  Unit* t = g_item_target;
  s32 angle = ratan2(t->pos[2] - u->pos[2], t->pos[0] - bobj_front_x(u, 0x60));
  angle += n == 8 ? -0x31C : -0xE3;
  bsfx_play_at(0x34, g_item_user);
  for (int i = 0; i < n; ++i) {
    if (Shot* s = shot_spawn(0x2F)) {
      shot_place_at_unit(s, g_item_user, 0x60, 0x80, 0);
      s->p128 = mode;
      s->p130 = n == 8 ? 0x20C060 : 0xFF8000;
      if (mode == 2) s->p130 = 0x2060C0;
      s32 aim[4];
      unit_mid(g_item_target, aim);
      const Vec4 v = vec_velocity_toward(s->pos, aim, 0x18);
      s->vel[0] = v.x;
      s->vel[1] = v.y;
      s->vel[2] = v.z;
      s->vel[0] = bcos(static_cast<u32>(angle)) * 0x18;
      s->vel[2] = bsin(static_cast<u32>(angle)) * 0x18;
    }
    angle += 0xE3;
  }
}

// A bomb (kind 0x30) dropped from high above the target, spread +-spread units.
Shot* item_drop_bomb(s32 spread) {
  Shot* s = shot_spawn(0x30);
  if (s == nullptr) return nullptr;
  Unit* t = g_item_target;
  s->pos[0] = t->pos[0] + rng_spread_fixed(spread);
  s->pos[1] = t->pos[1] - 0x578000;
  s->pos[2] = g_item_target->pos[2] + rng_spread_fixed(spread);
  return s;
}

void set_vel(Shot* s, const Vec4& v) {
  s->vel[0] = v.x;
  s->vel[1] = v.y;
  s->vel[2] = v.z;
}

}  // namespace

// 0x80084d48: a rising sparkle (set A anim 0x3a, 16 loops) above u's head.
void item_fx_sparkle(const Unit* u) {
  Sprite* sp = sprite_spawn_a(0x3A, 0x10, nullptr);
  if (sp == nullptr) return;
  sprite_set_pos(sp, u->pos[0], u->pos[1] + u->size[1] * -0x3000, u->pos[2]);
  sp->vel[1] = static_cast<s32>(0xFFFFB000);
  sp->accel[1] = 0x200;
  sp->layer = 2;
  sp->freeze_class = 2;
  sp->keep_screen = 1;
}

// ---- item_use_battle ----

void item_use_battle(Unit* user, Unit* target, int index) {
  BattleGlobals& g = bg();
  u8* inv = g.inventory;
  g.item_cooldown = 0xF0;
  g_item_iter = -1;
  g_item_user = user;
  g_item_target = target;
  u32 item;
  if (!field::inventory_verify(inv, static_cast<u32>(index))) {
    std::memset(inv + index * 2, 0, 2);
    item = 0;
  } else {
    u16 e;
    std::memcpy(&e, inv + index * 2, 2);
    item = e & 0x3FFu;
  }
  const u8 kind = g.item_effects[0x80 + item];
  const s32 power = g.item_effects[0x480 + item];
  field::inventory_remove_at(g.inventory, index, 1);
  camera_set_mode(g.camera, target == nullptr ? 4 : 7, target);
  Unit* const t0 = g_item_target;  // the chosen target (iVar5 at the switch)
  s32 hp_pct = power, mp_pct = power;
  u32 flags = 0;
  // The status words of the shared tail LAB_80083cdc (Unit [20] set_status).
  auto set_status = [](int word, u32 mask) { g_item_target->set_status(word, mask); };
  auto weapon_status = [](u8 bit) {
    g_item_target->weapon_chance = 0x19;
    g_item_target->weapon_status = static_cast<u8>(g_item_target->weapon_status | bit);
  };
  auto resisted = [](int roll, int chance) { return g_item_target->resist_roll(roll, chance) == 0; };
  switch (kind) {
    case 1:  // HP %
      mp_pct = 0;
      break;
    case 2:
      while (item_next_party_target() == 0) item_spawn_heal(power, 0, 0);
      return;
    case 3:  // MP %
      hp_pct = 0;
      break;
    case 4:
      while (item_next_party_target() == 0) item_spawn_heal(0, power, 0);
      return;
    case 5:  // HP and MP %
      break;
    case 6:  // HP %, revives
      flags = 1;
      mp_pct = 0;
      break;
    case 0xB:
    case 0xC:  // the user casts spell 0x41 / 0x42 at the target
      user->command = 4;
      user->target = target;
      user->spell = kind == 0xB ? 0x41 : 0x42;
      return;
    case 0xD:  // the user falls, the others are fully healed
      unit_damage_hp(user, 10000);
      while (item_next_party_target() == 0) {
        if (g_item_target != user) item_spawn_heal(100, 100, 0);
      }
      return;
    case 0xE:
      bsfx_play_ui(0x12);
      while (item_next_party_target() == 0) {
        item_fx_burst(1, 0xE, 0, 0);
        set_status(2, 0x2000000);
      }
      return;
    case 0xF:
      item_fx_burst(3, 0xF, 4, 1);
      set_status(2, 0x4000000);
      return;
    case 0x10:
      item_fx_burst(3, 0xE, 3, 1);
      set_status(2, 0x8000000);
      return;
    case 0x11:
    case 0x12:
    case 0x13: {  // Fol / drops / EXP doubled
      const int bit = kind - 0x11;
      if (bit == 0) g.reward_flags = static_cast<u8>(g.reward_flags | 1);
      bsfx_play_ui(0x12);
      if (bit != 0) g.reward_flags = static_cast<u8>(g.reward_flags | (1 << bit));
      while (item_next_party_target() == 0) item_fx_burst(1, 10 + bit, 0, 0);
      return;
    }
    case 0x14:
      while (item_next_party_target() == 0) item_spawn_heal(0x32, 0, 0);
      return;
    case 0x16:
      item_try_status_4(0x18, 200);
      return;
    case 0x17:
      while (item_next_enemy_target() == 0) item_try_status_4(0x10, 100);
      return;
    case 0x18:  // a ball rolling at the target
      bsfx_play_at(0x24, user);
      if (Shot* s = shot_spawn(0x2F)) {
        shot_place_at_unit(s, user, 0xE0, 0x118, 0);
        s->p130 = 0x103080;
        set_vel(s, vec_velocity_toward_xz(s->pos, g_item_target->pos, 8));
      }
      return;
    case 0x19:
      item_throw_projectiles(3, 1);
      return;
    case 0x1A:
      item_throw_projectiles(8, 1);
      return;
    case 0x1B:
      item_throw_projectiles(3, 2);
      return;
    case 0x1C:
      item_try_instant_kill(0x90);
      return;
    case 0x1D:
      item_fx_burst(3, 0xF, 4, 1);
      set_status(2, 0x10000000);
      return;
    case 0x1E:
      while (item_next_enemy_target() == 0) item_try_instant_kill(0x50);
      return;
    case 0x1F:  // escape
      if (g.can_escape == 0 || g.party_escaping != 0) {
        bsfx_play_ui(7);
        return;
      }
      for (PartyUnit* p : g.party_units) {
        if (unit_is_down(p)) continue;
        p->flee_timer = 4;
        bsfx_play_at(0x1D, p);
      }
      g.party_escaping = 1;
      return;
    case 0x22:
      bsfx_play_ui(0x12);
      while (item_next_party_target() == 0) {
        item_fx_burst(1, 0xD, 0, 0);
        set_status(2, 0x20000000);
        g_item_target->set_effect_timer(1, 0);
      }
      return;
    case 0x25:
      item_fx_aura(3, 5, 0x2040FF);
      unit_damage_hp(g_item_target, 9999);
      return;
    case 0x26:
    case 0x52:  // cures statuses 2 / 4 / 8
      hp_pct = 0, mp_pct = 0, flags = 0xE;
      break;
    case 0x27:  // poison
      item_fx_aura(2, 6, 0x60FF40);
      g_item_target->st()->status = static_cast<u8>(g_item_target->st()->status | 8);
      return;
    case 0x28:
      item_fx_burst(3, 10, 1, 1);
      set_status(2, 0x40000000);
      return;
    case 0x29:
      item_fx_burst(2, 0xD, 5, 1);
      set_status(2, 0x80000000);
      return;
    case 0x2A: {  // HP and MP swap
      item_fx_burst(2, 9, 0xF, 1);
      Stats* st = g_item_target->st();
      const s32 hp = st->hp;
      s32 new_hp = st->mp;
      if (st->hp_max < st->mp) new_hp = st->hp_max;
      s32 new_mp = hp;
      if (st->mp_max < hp) new_mp = st->mp_max;
      unit_damage_hp(g_item_target, hp - new_hp);
      g_item_target->st()->mp = static_cast<s16>(new_mp);
      return;
    }
    case 0x2B:
      item_fx_burst(3, 0xD, 5, 1);
      set_status(3, 1);
      return;
    case 0x2C:
      hp_pct = 0x1E, mp_pct = 0x1E, flags = 1;
      break;
    case 0x2D: {  // drains the MP of the enemies near the user
      int n = 0;
      while (item_next_enemy_target() == 0) {
        if (vec_distance_fx(user->pos, g_item_target->pos) >= 0x200000) continue;
        if (resisted(1, 0x80)) {
          effect_rise_icon(g_item_target, 0);
        } else {
          item_fx_aura(3, 4, 0x4876C2);
          const s32 mp = g_item_target->st()->mp;
          ++n;
          unit_damage_mp(g_item_target, mp);
          damage_popup_spawn(g_item_target, mp, 0, 1);
        }
      }
      if (n == 0) bsfx_play_ui(0xB);
      return;
    }
    case 0x2E:
      item_try_status_20(0xC0);
      return;
    case 0x30:
      hp_pct = 0, mp_pct = 0x1E, flags = 0;
      break;
    case 0x31:
      item_fx_burst(3, 10, 6, 1);
      weapon_status(4);
      return;
    case 0x32:
      item_try_status_40(0xC0);
      return;
    case 0x33:
      item_fx_burst(3, 0xC, 2, 1);
      weapon_status(2);
      return;
    case 0x34:
      item_fx_burst(3, 0xF, 3, 1);
      set_status(3, 2);
      g_item_target->set_effect_timer(2, 0);
      return;
    case 0x35:
      item_fx_burst(2, 0xD, 7, 1);
      set_status(3, 4);
      g_item_target->clear_status(3, 0x800);
      g_item_target->set_effect_timer(3, 0);
      return;
    case 0x36: {  // MP doubled (to its maximum)
      const Stats* st = g_item_target->st();
      item_spawn_heal(0, mips_div(st->mp * 100, st->mp_max), 0);
      item_fx_aura(3, 5, 0xA020FF);
      set_status(3, 8);
      return;
    }
    case 0x37:
      item_fx_burst(3, 10, 4, 1);
      set_status(3, 0x10);
      return;
    case 0x38:
      item_fx_burst(3, 0xF, 4, 1);
      set_status(3, 0x20);
      return;
    case 0x39:
      hp_pct = 100, mp_pct = 0, flags = 0x40000001;
      break;
    case 0x3A:
      item_fx_burst(3, 0xD, 5, 1);
      set_status(3, 0x2040);
      return;
    case 0x3B:
      hp_pct = 0x1E, mp_pct = 0, flags = 0x40000000;
      break;
    case 0x3C:
      if ((g_item_target->st()->status & 8) == 0) {
        item_fx_aura(3, 6, 0x2040FF);
        g_item_target->st()->status = static_cast<u8>(g_item_target->st()->status | 4);
        return;
      }
      hp_pct = 0x1E, mp_pct = 0, flags = 0;
      break;
    case 0x3F:
      item_fx_burst(3, 0xB, 5, 1);
      set_status(3, 0x80);
      return;
    case 0x40:
      if (rng_range(2) == 0) {
        hp_pct = 0, mp_pct = 0x46;
      } else {
        hp_pct = 0x46, mp_pct = 0;
      }
      flags = 0;
      break;
    case 0x41:
    case 0x5D:
      hp_pct = 10, mp_pct = 0, flags = 8;
      break;
    case 0x42:
      hp_pct = 10, mp_pct = 0, flags = 2;
      break;
    case 0x43:
      hp_pct = 10, mp_pct = 0, flags = 0xF;
      break;
    case 0x44:
      item_fx_burst(3, 10, 2, 1);
      weapon_status(4);
      set_status(3, 0x2000);
      return;
    case 0x45: {  // the target's HP as damage to it and every enemy near it
      const s32 dmg = t0->st()->hp;
      while (item_next_enemy_target() == 0) {
        if (vec_distance_fx(t0->pos, g_item_target->pos) >= 0x300000) continue;
        bsfx_play_at(0x50, g_item_target);
        item_fx_aura(2, 5, 0x738062);
        unit_damage_hp(g_item_target, dmg);
        damage_popup_spawn(g_item_target, dmg, 0, 0);
      }
      bsfx_play_at(0x50, t0);
      g_item_target = t0;
      item_fx_aura(3, 6, 0x738062);
      unit_damage_hp(t0, dmg);
      damage_popup_spawn(t0, dmg, 0, 0);
      return;
    }
    case 0x46:
      item_fx_burst(3, 0xE, 3, 1);
      set_status(3, 0x4000);
      return;
    case 0x47:
      if (59 < rng_range(100)) {
        item_fx_aura(3, 6, 0xC05060);
        unit_damage_hp(g_item_target, 9999);
        return;
      }
      hp_pct = 100, mp_pct = 100, flags = 0;
      break;
    case 0x48:
      item_spawn_heal(10, 0, 1);
      item_fx_burst(3, 0xD, 5, 1);
      set_status(3, 0x40);
      return;
    case 0x49:
      bsfx_play_ui(0x12);
      while (item_next_party_target() == 0) {
        item_fx_burst(1, 10, 0, 0);
        set_status(3, 0x400);
      }
      return;
    case 0x4A:
      item_fx_burst(3, 0xE, 8, 1);
      weapon_status(1);
      return;
    case 0x4B:
      item_fx_aura(2, 4, 0xFF00C0);
      g_item_target->clear_status(3, 4);
      set_status(3, 0x800);
      g_item_target->set_effect_timer(3, 0);
      return;
    case 0x4D:
      item_fx_burst(2, 9, 7, 1);
      set_status(3, 0x1000);
      set_status(3, 0x2000);
      return;
    case 0x4E:
      while (item_next_enemy_target() == 0) item_try_status_20(100);
      return;
    case 0x4F:
      while (item_next_enemy_target() == 0) item_try_status_40(0x60);
      return;
    case 0x50:
      bsfx_play_ui(0x12);
      while (item_next_party_target() == 0) {
        item_fx_burst(2, 0xF, 3, 0);
        set_status(3, 2);
        g_item_target->set_effect_timer(2, 0);
      }
      return;
    case 0x51:
      while (item_next_enemy_target() == 0) {
        if (resisted(1, 0x8C)) {
          effect_rise_icon(g_item_target, 0);
        } else {
          item_fx_aura(2, 4, 0x40C060);
          bsfx_play_at(0x27, g_item_target);
          g_item_target->status_bits |= 0x80;
        }
      }
      return;
    case 0x53:
      while (item_next_enemy_target() == 0) {
        if (resisted(1, 0xA0)) {
          effect_rise_icon(g_item_target, 0);
        } else {
          item_fx_aura(2, 5, 0x20FF20);
          bsfx_play_at(0x24, g_item_target);
          g_item_target->st()->status = static_cast<u8>(g_item_target->st()->status | 8);
        }
      }
      return;
    case 0x54:
      while (item_next_enemy_target() == 0) {
        if (resisted(1, 200)) {
          effect_rise_icon(g_item_target, 0);
        } else {
          item_fx_aura(2, 6, 0x2020FF);
          bsfx_play_at(0x1C, g_item_target);
          g_item_target->reaction = 1;
          const s32 kb = rng_spread_fixed(4);
          g_item_target->knockback[0] = kb;
          g_item_target->knockback[1] = 0;
          g_item_target->knockback[2] = 0;
          unit_apply_stun(g_item_target);
        }
      }
      return;
    case 0x55:
      while (item_next_enemy_target() == 0) {
        item_fx_aura(2, 5, 0xFF00FF);
        bsfx_play_at(0x3A, g_item_target);
        unit_damage_mp(g_item_target, 0x32);
        damage_popup_spawn(g_item_target, 0x32, 0, 1);
      }
      return;
    case 0x56: {  // 400 damage to the enemies near the user
      int n = 0;
      while (item_next_enemy_target() == 0) {
        if (vec_distance_fx(user->pos, g_item_target->pos) >= 0x300000) continue;
        if (Sprite* sp = sprite_spawn_from(0xF, 0, g.sprite_bank_fx.get(), nullptr)) {
          sprite_set_pos(sp, g_item_target->pos[0], bobj_ground_height(g_item_target), g_item_target->pos[2]);
          sprite_set_quad(sp, 0x40, 0x200040FF);
          sprite_set_fade(sp, 0x80, -2, 10);
          sp->layer = 1;
        }
        ++n;
        unit_damage_hp(g_item_target, 400);
        damage_popup_spawn(g_item_target, 400, 0, 0);
        bsfx_play_at(0x50, g_item_target);
      }
      if (n == 0) bsfx_play_ui(0xB);
      return;
    }
    case 0x57: {  // four bombs fanned out from the user
      s32 angle = ratan2(g_item_target->pos[2] - user->pos[2], g_item_target->pos[0] - bobj_front_x(user, 0x60));
      angle -= 0x155;
      bsfx_play_at(0x34, user);
      bsfx_play_at(0x21, user);
      for (int i = 0; i < 4; ++i) {
        Shot* s = shot_spawn(0x30);
        if (s == nullptr) continue;
        shot_place_at_unit(s, user, 0x60, 0x80, 0);
        s32 aim[4];
        unit_mid(g_item_target, aim);
        set_vel(s, vec_velocity_toward(s->pos, aim, 0x18));
        s->vel[0] = bcos(static_cast<u32>(angle)) * 0x18;
        const s32 sn = bsin(static_cast<u32>(angle));
        angle += 0xE3;
        s->p134 = 3;
        s->p138 = 6;
        s->vel[2] = sn * 0x18;
        s->p128 = 0x1060D0;
        s->aux = 7;
      }
      return;
    }
    case 0x58:
    case 0x59:
    case 0x5A: {  // a bomb falling on the target
      const bool small = kind == 0x58;
      Shot* s = item_drop_bomb(small ? 300 : 600);
      if (s == nullptr) return;
      if (kind == 0x58) {
        s->p130 = 0x204010, s->p138 = 5, s->p128 = 0x810D0, s->p134 = 0, s->aux = 5;
      } else if (kind == 0x59) {
        s->p130 = 0x103080, s->p134 = 1, s->p138 = 4, s->p128 = 0xFF6010, s->aux = 4;
      } else {
        s->p130 = 0xC04020, s->p134 = 2, s->p138 = 6, s->p128 = 0x80FFA0, s->aux = 2;
      }
      set_vel(s, vec_velocity_toward(s->pos, g_item_target->pos, small ? 0x14 : 0x18));
      bsfx_play_at(kind == 0x58 ? 0x2C : kind == 0x59 ? 0x38 : 0x39, s);
      bsfx_play_at(kind == 0x58 ? 0x21 : 0x22, s);
      return;
    }
    case 0x5C: {  // explosions all over the enemies; the party left at 1 HP; enemies may die
      for (int i = 0; i < 0x1C; ++i) {
        Unit* e = nullptr;
        int tries = 0;
        do {
          e = g.enemy_units[rng_range(8)];
          if (!unit_is_down(e)) break;
          ++tries;
        } while (tries < 200);
        if (unit_is_down(e)) continue;
        const s32 dx = rng_spread_fixed(700);
        const s32 y = rng_between_fixed(-1000, 0);
        const s32 dz = rng_spread_fixed(700);
        // ORIGINAL BUG (original_bugs.md S2): the depth of the explosion is the enemy's
        // height plus the spread, so every blast lands on the arena's middle line:
        // const s32 at[3] = {e->pos[0] + dx, y, e->pos[1] + dz};
        const s32 at[3] = {e->pos[0] + dx, y, e->pos[2] + dz};
        const s32 scale = rng_between(12000, 28000);
        const int clut = static_cast<int>(rng_range(3)) + 4;
        effect_debris_burst(at, kDebrisSpread, 2, 2, 1, scale, static_cast<u8>(clut));
      }
      bsfx_play_ui(0x50);
      bsfx_play_ui(0x21);
      bsfx_play_ui(0x22);
      bsfx_play_ui(0x34);
      for (PartyUnit* p : g.party_units) {
        if (!unit_is_down(p)) p->st()->hp = 1;
      }
      while (item_next_enemy_target() == 0) {
        if (resisted(0, 200)) {
          effect_rise_icon(g_item_target, 0);
          continue;
        }
        g_item_target->st()->hp = 0;
        g_item_target->death = 1;
        camera_reset_mode(g.camera);
        camera_move_to(g.camera, g_item_target->pos, 0xC00, 0xE3, 0x1000, 0x14, 0x180, 5);
        s32 at[4];
        unit_ground(g_item_target, at);
        effect_spawn_sparks_at(at, 0xC, 0x60, 0x1E0, 0x10, 0x78, 0, 1, 0x30, 0);
      }
      return;
    }
    case 0x5E:
      hp_pct = 0x1E, mp_pct = 0xF, flags = 2;
      break;
    case 0x5F:
      while (item_next_enemy_target() == 0) {
        if (resisted(1, 0xFF)) {
          effect_rise_icon(g_item_target, 0);
        } else {
          item_fx_aura(3, 4, 0xFFFFFF);
          bsfx_play_at(0x3A, g_item_target);
          g_item_target->super_armour = 0xB4;
        }
      }
      return;
    case 0x61:
      scan_window_show(g_item_target);
      return;
    case 0x62:
      hp_pct = 10, mp_pct = 0, flags = 4;
      break;
    default:
      return;
  }
  // LAB_8008482c
  item_spawn_heal(hp_pct, mp_pct, flags);
}

// ---- The Shot kinds ----

void shot_spawn_sparkle_on_target(Shot* s, int anim) {
  Sprite* sp = shot_spawn_sprite(s, anim);
  if (sp == nullptr) return;
  const Unit* t = s->target;
  const s32 dx = rng_spread_fixed(t->size[0] * 3);
  const s32 x = t->pos[0];
  const s32 dz = rng_spread_fixed(s->target->size[2] * 3);
  t = s->target;
  sprite_set_pos(sp, x + dx, t->pos[1] + t->size[1] * -0x4000, t->pos[2] + dz);
  sprite_set_fade(sp, 0x80, -2, 8);
  sp->vel[1] = rng_between_fixed(1, 3);
}

void shot_k2e_item_effect(Shot* s) {
  BattleGlobals& g = bg();
  shot_use_anims_f920(s);
  s32 mid[4], at[4];
  unit_mid(s->target, mid);
  unit_ground(s->target, at);
  if (g.flags != 0) return;
  if (g.party_units[g.controlled_slot] == s->target) camera_request_mode(g.camera, 5, s->target);
  s32 n = s->p128 / 0x1E;
  if (n < 1) n = 1;
  if (0 < s->p128) {
    for (int i = 0; i < n; ++i) shot_spawn_sparkle_on_target(s, static_cast<int>(rng_range(static_cast<u32>(n + 5))) + 3);
  }
  if (0 < s->p134) {
    for (int i = 0; i < n; ++i) shot_spawn_sparkle_on_target(s, static_cast<int>(rng_range(static_cast<u32>(n + 3))) + 0x2C);
  }
  if ((s->p130 & 1) != 0) {
    for (int i = 0; i < 2; ++i) {
      const Vec4 p = vec_random_around(mid, 400);
      particle_spawn_homing_line(p.data(), mid, 0x101000, 0xF080020, 0x80, 0x2000, 0x400);
    }
  }
  if (s->aux == 0) {
    Unit* t = s->target;
    if (s->p128 != 0) bsfx_play_at(0x17, t);
    if (s->p134 != 0) bsfx_play_at(0x14, s->target);
    if ((s->p130 & 8) != 0) {  // poison cured
      bsfx_play_at(0x15, s->target);
      effect_spawn_sparks_at(at, 0xE, 8, 0x60, 5, 0x78, 3, 0, 0x1E, 0);
      effect_spawn_sparks_at(at, 0xE, 8, 0x40, 7, 100, 3, 0, 0x28, 0);
      for (int i = 0; i < 0x14; ++i) {
        const s32 vx = rng_spread_fixed(0xC);
        const s32 vy = rng_between_fixed(-16, -8);
        const s32 vz = rng_spread_fixed(0xC);
        const s32 v[4] = {vx, vy, vz, 0};
        particle_spawn_spark_line(mid, v, 0x402000, 0x60FF20, 0x20, 0x3C);
      }
    }
    if ((s->p130 & 2) != 0) {
      bsfx_play_at(0x16, s->target);
      for (int i = 0; i < 0x14; ++i) {
        const s32 vx = rng_spread_fixed(0xC);
        const s32 vy = rng_between_fixed(-16, -8);
        const s32 vz = rng_spread_fixed(0xC);
        const s32 v[4] = {vx, vy, vz, 0};
        particle_spawn_confetti(mid, v, 0x1040F0, 0x20, 0x3C);
      }
      particle_spawn_ring(at, 0xC, 0x30, 8, 0x3C, 0x840, 0x1040FF, 0x10, 0);
      particle_spawn_ring(at, 0xC, 0x80, 0x18, 0x32, 0x840, 0x1040FF, 0x20, 0);
    }
    if ((s->p130 & 4) != 0) {
      bsfx_play_at(0x13, s->target);
      for (int i = 0; i < 0x14; ++i) {
        const Vec4 p = vec_random_around(mid, 800);
        particle_spawn_homing_line(mid, p.data(), 0x101010, 0xC0C0C0, 0x28, 0x4000, 0x800);
      }
      effect_spawn_sphere_at_default(at, 8, 10, 10, 0x3C, 0x909090, 0x20, 1);
      effect_spawn_sparks_at(at, 0xC, 0x10, 0x80, 0xC, 0x3C, 1, 0, 0x28, 0);
      effect_spawn_sparks_at(at, 0xC, 8, 0x40, -8, 0x3C, 1, 0, 0x140, 0);
    }
    if ((s->p130 & 1) != 0) {  // revival
      bsfx_play_at(0x16, s->target);
      for (int k = 0; k < 10; ++k) {
        particle_spawn_ring(mid, 8, 0x40, -0xC, 0x3C, 0x3016000, 0xFF8010, 0x140, (k * 0xF000) / 0x168);
      }
      for (int i = 0; i < 0x14; ++i) {
        Sprite* sp = shot_spawn_sprite(s, 0x2A);
        if (sp == nullptr) continue;
        sprite_set_pos_v(sp, mid);
        sprite_set_fade(sp, 0x80, -2, 0);
        const Vec4 v = vec_random_around(kZeroVector, 8);
        sp->vel[0] = v.x;
        sp->vel[1] = v.y;
        sp->vel[2] = v.z;
        sp->extra34 = v.w;
      }
    }
    s->aux = 1;
  }
  s->p138 += g.frame_ticks;
  if (shot_fade_out(s) == 0 && s->p138 < 0x2D) return;
  const bool curse = (s->p130 & 0x40000000) != 0;
  if (curse) s->p130 &= 0x3FFFFFFF;
  if (g.state != kStateFighting) s->p130 &= ~8;
  Unit* t = s->target;
  bool heal = true;
  if (s->p130 != 0) {
    const u8 before = t->st()->status;
    const bool was_down = unit_is_down(t);
    t->st()->status = static_cast<u8>(t->st()->status & ~static_cast<u32>(s->p130));
    bool begin = false;
    if (was_down && !unit_is_down(s->target)) {
      s->target->status_recovered();
      s->target->action_kind = 0;
      begin = true;
    } else if ((before & 1) != 0) {
      if ((s->target->st()->status & 1) != 0) {
        heal = false;  // still KO'd: no HP
      } else {
        s->target->action_kind = 0;
        begin = true;
      }
    }
    if (begin) s->target->begin_action();
  }
  t = s->target;
  if (heal && (t->st()->status & 1) == 0 && s->p128 != 0) {
    const s32 v = -((t->st()->hp_max * s->p128) / 100);
    damage_popup_spawn(t, v, static_cast<s32>(0xFFFFE000), 0);
    unit_damage_hp(s->target, v);
  }
  if (!unit_is_down(s->target) && s->p134 != 0) {
    const s32 v = -((s->target->st()->mp_max * s->p134) / 100);
    damage_popup_spawn(s->target, v, 0x2000, 1);
    unit_damage_mp(s->target, v);
  }
  if (curse) {
    bsfx_play_at(0x24, s->target);
    const u32 r = rng_range(3);
    Stats* st = s->target->st();
    if (r == 1) st->status = static_cast<u8>(st->status | 2);
    else if (r == 0) st->status = static_cast<u8>(st->status | 8);
    else if (r == 2) st->status = static_cast<u8>(st->status | 4);
  }
  shot_kill(s);
}

void shot_k2f_bouncing_ball(Shot* s) {
  BattleGlobals& g = bg();
  s32 orient[3] = {0, s->p138, s->p134};
  s->size[0] = s->size[1] = s->size[2] = 100;
  stage_colour_set_grey(s->colour_mode != 0 ? s->rgb[0] : 0x80);
  lights_build_oriented(orient);
  obj_set_matrix(s->pos, orient, kUnitScale);
  const s32 segs = s->p128 != 0 ? 6 : 8;
  const s32 r = s->p128 != 0 ? 0x80 : 0x100;
  draw_sphere(segs, segs, r, static_cast<u32>(s->p130), 0xFF, 0, s->p128 != 0 ? 2 : 0, 0x1000);
  stage_colour_restore();
  const s32 rest = bobj_ground_height(s) + (r + 0x18) * -0x1000;
  s32 d = s->pos[1] - rest;
  if (d < 0) d += 3;
  s32 shadow = r + (d >> 14);
  if (shadow < 0x10) shadow = 0x10;
  if (s->p12c == 0) bobj_draw_shadow(s, -shadow, shadow, -shadow, shadow, 0xFFFFFFFF);
  if (g.flags != 0) return;
  bobj_move(s, s->vel);
  if (s->p128 == 0) {
    bool landed = rest <= s->pos[1];
    if (!landed) {
      s->vel[1] += g.frame_ticks * 0x1000;
      landed = rest <= s->pos[1];
    }
    if (landed) {
      s32 v = s->vel[1];
      if (v < 0) v = -v;
      s->vel[1] = -v / 3;
      if (s->vel[1] < -0x1FFF) bsfx_play_at(0x25, s);
      else s->vel[1] = 0;
      s->pos[1] = rest;
    }
  }
  if (s->p128 == 2) {
    s32 dv[3] = {s->vel[0] * g.frame_ticks, 0, 0};
    if (unit_check_move(s, dv, 7) == kWallBlock) s->vel[0] = -s->vel[0];
    dv[0] = 0;
    dv[2] = s->vel[2] * g.frame_ticks;
    if (unit_check_move(s, dv, 7) == kWallBlock) s->vel[2] = -s->vel[2];
    if (rest <= s->pos[1]) s->vel[1] = -s->vel[1];
  }
  s->aux = static_cast<s16>(static_cast<u16>(s->aux) + g.frame_ticks);
  if (s->p12c == 0 && 0xB4 < s->aux && s->p128 != 0) s->p12c = 1;
  if (shot_fade_out(s) != 0) return;
  if (s->p12c == 0 && bobj_outside_arena(s)) s->p12c = 1;
  const s32 dz = s->vel[2] * g.frame_ticks;
  s->p134 = (s->p134 + ((s->vel[0] * g.frame_ticks) >> 12)) & 0xFFF;
  s->p138 = (s->p138 + (dz >> 12)) & 0xFFF;
  if (s->p12c != 0) return;
  Unit* units[8] = {};
  Vec4 points[8] = {};
  const s32 box = (r * 3) >> 2;
  collider_set_attack_box(s, box, box, box);
  const int n = collider_attack_overlaps(s, units, points, 0);
  if (n == 0) return;
  s->hit_landed = 1;
  for (int i = 0; i < n; ++i) {
    Unit* u = units[i];
    const s32 dmg = s->p128 == 0 ? 0x32 : 300;
    damage_popup_spawn(u, dmg, 0, 0);
    unit_damage_hp(u, dmg);
    u->knockback[1] = 0;
    u->knockback[2] = s->vel[2] << 1;
    u->knockback[0] = s->vel[0] << 1;
    u->reaction = 1;
    bsfx_play_at(0x4C, u);
    if (Sprite* sp = sprite_spawn_a(0x11, 1, nullptr)) {
      sprite_set_pos_v(sp, points[i].data());
      sp->layer = 2;
    }
  }
  if (s->p128 != 0) s->p12c = 1;
}

void shot_k30_bomb(Shot* s) {
  BattleGlobals& g = bg();
  VortexParams p;
  p.radius = 0x4000;
  p.base = -0xC0;
  p.width = 0x140;
  p.step = 0x40;
  p.rings = 8;
  p.segments = 6;
  p.colour_a = scale_colour(static_cast<u32>(s->p128), 0x100 - s->p130);
  p.colour_b = scale_colour(static_cast<u32>(s->p128), (0x100 - s->p130) >> 1);
  p.phase_span = 2;
  p.twist_freq = 1;
  p.twist_amp = 0x1000;
  p.blend = 2;
  p.ot_bias = 5;
  p.phase = 0x1FFF - ((s->p128 * 0x60) & 0x1FFF);
  const s32 back[3] = {-s->vel[0], -s->vel[1], s->vel[2]};
  s32 angles[3];
  vec_to_angles(angles, back);
  obj_set_matrix(s->pos, angles, kUnitScale);
  draw_vortex(p);
  if (s->p134 != 3) {
    stage_colour_set_grey(s->colour_mode != 0 ? s->rgb[0] : 0x80);
    s32 zero[3] = {kZeroVector[0], kZeroVector[1], kZeroVector[2]};
    lights_build_oriented(zero);
    obj_set_matrix(s->pos, zero, kUnitScale);
    draw_sphere(8, 8, 100, static_cast<u32>(s->p130), 0xFF, 0, 0, 0x1000);
    stage_colour_restore();
  }
  if (g.flags != 0) return;
  bobj_move(s, s->vel);
  if (shot_fade_out(s) != 0) return;
  if (s->p134 == 3 && s->p12c == 0 && bobj_outside_arena(s)) s->p12c = 1;
  if (bobj_ground_height(s) <= s->pos[1]) {  // on the ground: it goes off there
    screen_shake2(4, 6, 4, 6, 0xE);
    bsfx_play_at_pos(0x50, s->pos);
    effect_debris_burst(s->pos, kDebrisSpread, 4, 2, 1, 0x2000, static_cast<u8>(s->p138));
    effect_spawn_sparks_at(s->pos, 10, 0x30, 0x90, 0xC, 0x50, s->aux, 1, 0x20, 0);
    shot_kill(s);
    return;
  }
  if (s->p12c != 0) return;
  Vec4 points[8] = {};
  collider_set_attack_box(s, 100, 100, 100);
  s32 dmg = 0;  // (the console keeps the last value in a register; only kinds 0-3 occur)
  for (int side = 0;; ++side) {
    if (s->p134 == 2 ? 1 < side : 0 < side) return;
    Unit* units[8] = {};
    const int n = collider_attack_overlaps(s, units, points, side);
    if (n == 0) continue;
    s->hit_landed = 1;
    for (int i = 0; i < n; ++i) {
      Unit* u = units[i];
      switch (s->p134) {
        case 0: dmg = u->st()->atk[2]; break;
        case 1: dmg = u->st()->def[2]; break;
        case 2: dmg = u->st()->hp / 2; break;
        case 3: dmg = 500; break;
        default: break;
      }
      if (9999 < dmg) dmg = 9999;
      damage_popup_spawn(u, dmg, 0, 0);
      unit_damage_hp(u, dmg);
      u->knockback[0] = s->vel[0] < 0 ? -0x8000 : 0x8000;
      u->reaction = 1;
      if (Sprite* sp = sprite_spawn_a(0x11, 1, nullptr)) {
        sprite_set_pos_v(sp, points[i].data());
        sp->layer = 2;
      }
      screen_shake2(4, 6, 4, 6, 0xE);
      bsfx_play_at(0x50, u);
      for (int k = 0; k < 2; ++k) {
        effect_debris_burst(points[i].data(), kDebrisSpread, 4 - k, 2, 1, 0x2000 + k * 0x1000,
                            static_cast<u8>(s->p138));
      }
      s32 at[4];
      unit_ground(u, at);
      effect_spawn_sparks_at(at, 10, 0x30, 0x90, 0xC, 0x50, s->aux, 1, 0x20, 0);
      shot_kill(s);
    }
  }
}

// ---- Debugging ----

void debug_forced_items() {
  struct Use {
    s32 frames;
    int slot, index, target;
    char side;
    bool done;
  };
  static std::vector<Use> uses;
  static bool parsed = false;
  BattleGlobals& g = bg();
  if (!parsed) {
    parsed = true;
    if (const char* env = std::getenv("SS_BATTLE_ITEM")) {
      std::string list(env);
      size_t at = 0;
      while (at < list.size()) {
        size_t stop = list.find(';', at);
        if (stop == std::string::npos) stop = list.size();
        Use c{};
        if (std::sscanf(list.substr(at, stop - at).c_str(), "%d:%d:%i:%c%d", &c.frames, &c.slot, &c.index, &c.side,
                        &c.target) == 5)
          uses.push_back(c);
        at = stop + 1;
      }
    }
  }
  for (Use& c : uses) {
    if (c.done || g.battle_frames < c.frames) continue;
    c.done = true;
    Unit* t = nullptr;
    if (c.side == 'p') t = g.party_units[static_cast<size_t>(c.target & 3)];
    if (c.side == 'e') t = g.enemy_units[static_cast<size_t>(c.target & 7)];
    std::fprintf(stderr, "[item] forced item %d by p%d at %c%d, battle frames %d\n", c.index, c.slot, c.side, c.target,
                 g.battle_frames);
    item_use_battle(g.party_units[static_cast<size_t>(c.slot & 3)], t, c.index);
  }
}

}  // namespace ss::game::battle
