// Battle spells: the tables, the spell-data cache, spell_cast and the casting side of a
// unit. See spells.h.

#include "game/battle/spells.h"

#include <array>
#include <cstring>
#include <string>

#include "disc/slz.h"
#include "game/battle/camera.h"
#include "game/battle/shot.h"
#include "game/battle/spell_fx.h"
#include "game/battle/spellcast.h"
#include "game/battle/sprite.h"
#include "game/battle/unit.h"
#include "game/boot/loader.h"
#include "game/boot/rng.h"
#include "runtime/runtime.h"

namespace ss::game::battle {
namespace {

u8 image_u8(u32 address) {
  const u8* p = battle_data(address);
  return p != nullptr ? *p : 0;
}

// g_spell_data_entry 0x8006d1f2, a battle-long copy (Indalecio rewrites rows).
std::array<s16, kSpellCount * 5> g_entry{};
bool g_entry_loaded = false;

void entry_table_load() {
  if (g_entry_loaded) return;
  g_entry_loaded = true;
  for (int i = 0; i < kSpellCount * 5; ++i) g_entry[static_cast<size_t>(i)] = battle_s16(0x8006D1F2u + static_cast<u32>(i) * 2);
}

std::vector<u8> g_ovl1984b1;  // #1984 block 1 (0x80081728..0x80085518)
bool g_ovl1984b1_loaded = false;

// The cache (g_spell_req_* 0x8006fc7c.., g_spell_slot_* 0x8006fcec..).
struct SpellCache {
  std::array<s16, 16> req_priority{};
  std::array<u8, 16> req_spell{};
  std::array<Unit*, 16> req_caster{};
  std::array<u8, 16> slot_spell{};
  std::array<Unit*, 16> slot_owner{};
  std::array<u8, 16> slot_ready{};
  std::array<SpellData, 16> slot_buffer{};
  // Port: the load queue's state of each slot's read (the console's bload request; here
  // the entry is read when the slot is filled, and bload_done holds off one frame, as the
  // earliest a console read can finish).
  std::array<u8, 16> slot_reading{};
};
SpellCache g_cache;

u8 g_spell_landed = 0;          // 0x8006fb2f
u8 g_spell_effect_started = 0;  // 0x8006f6d1

s16 entry_for(int spell, const Unit* caster) {
  if (spell < 0 || spell >= kSpellCount) return 0;
  return spell_data_entry(spell, unit_caster_variant(caster));
}

void cache_compact() {
  // 0x80048b04: both lists closed up in order.
  SpellCache& c = g_cache;
  int n = 0, m = 0;
  for (int i = 0; i < 16; ++i) {
    if (c.req_spell[i] != 0) {
      c.req_spell[n] = c.req_spell[i];
      c.req_priority[n] = c.req_priority[i];
      c.req_caster[n] = c.req_caster[i];
      ++n;
    }
    if (c.slot_spell[i] != 0) {
      c.slot_spell[m] = c.slot_spell[i];
      c.slot_owner[m] = c.slot_owner[i];
      c.slot_ready[m] = c.slot_ready[i];
      c.slot_reading[m] = c.slot_reading[i];
      if (m != i) c.slot_buffer[m] = std::move(c.slot_buffer[i]);
      ++m;
    }
  }
  for (; n < 16; ++n) c.req_spell[n] = 0;
  for (; m < 16; ++m) {
    c.slot_spell[m] = 0;
    c.slot_buffer[m].reset();
    c.slot_reading[m] = 0;
  }
}

SpellData load_entry(s16 entry) {
  BattleGlobals& g = bg();
  if (entry <= 0 || g.loader == nullptr) return nullptr;
  const int bytes = g.loader->entry_size(entry);
  if (bytes <= 0) return nullptr;
  auto data = std::make_unique<std::vector<u8>>(static_cast<size_t>(bytes));
  g.loader->load_sync(data->data(), entry, 0);
  return data;
}

}  // namespace

// ---- Tables ----

int spell_mp_cost_base(int spell) { return image_u8(0x8006B053u + static_cast<u32>(spell)); }
int spell_cast_ticks(int spell) { return image_u8(0x8006B09Bu + static_cast<u32>(spell)); }
int spell_target_class(int spell) { return image_u8(0x8006B0E3u + static_cast<u32>(spell)); }
int spell_needs_data(int spell) { return image_u8(0x8006B12Bu + static_cast<u32>(spell)); }
int spell_element(int spell) { return image_u8(0x8006B173u + static_cast<u32>(spell)); }
int spell_effect_kind(int spell) { return image_u8(0x8007002Fu + static_cast<u32>(spell)); }

s16 spell_data_entry(int spell, int variant) {
  entry_table_load();
  const int i = spell * 5 + variant;
  return i >= 0 && i < kSpellCount * 5 ? g_entry[static_cast<size_t>(i)] : 0;
}

void spell_data_entry_set(int spell, int variant, s16 entry) {
  entry_table_load();
  const int i = spell * 5 + variant;
  if (i >= 0 && i < kSpellCount * 5) g_entry[static_cast<size_t>(i)] = entry;
}

const u8* ovl1984b1_data(u32 address) {
  if (!g_ovl1984b1_loaded) {
    g_ovl1984b1_loaded = true;
    BattleGlobals& g = bg();
    if (g.loader != nullptr) {
      const int bytes = g.loader->entry_size(1984);
      if (bytes > 0) {
        std::vector<u8> packed(static_cast<size_t>(bytes));
        g.loader->load_sync(packed.data(), 1984, 0);
        if (auto b = disc::slz::unpack_block(packed, 0, 1, nullptr)) g_ovl1984b1 = std::move(*b);
      }
    }
  }
  const u32 off = address - 0x80081728u;
  return off < g_ovl1984b1.size() ? g_ovl1984b1.data() + off : nullptr;
}

u8& spell_landed_flag() { return g_spell_landed; }
u8& spell_effect_started_flag() { return g_spell_effect_started; }

void spells_reset() {
  for (int i = 0; i < 16; ++i) g_cache.slot_buffer[static_cast<size_t>(i)].reset();
  g_cache = SpellCache{};
  g_entry_loaded = false;
  g_ovl1984b1.clear();
  g_ovl1984b1_loaded = false;
  g_spell_landed = 0;
  g_spell_effect_started = 0;
  spellcast_reset();
  spell_fx_reset();
}

// ---- The spell-data cache ----

int unit_caster_variant(const Unit* u) {
  if (u == nullptr || u->is_enemy != 0) return 0;
  const u8 klass = u->st()->klass;
  return 8 < klass ? klass - 8 : 0;
}

void spell_data_request(int spell, int priority, Unit* caster) {
  if (entry_for(spell, caster) == 0) return;
  SpellCache& c = g_cache;
  const s16 prio = static_cast<s16>(priority);
  int at = 15;
  for (int i = 0; i < 16; ++i) {
    if (c.req_spell[static_cast<size_t>(i)] == 0 || prio < c.req_priority[static_cast<size_t>(i)]) {
      at = i;
      break;
    }
  }
  if (c.req_spell[static_cast<size_t>(at)] != 0) {
    for (int k = 15; at < k; --k) {
      c.req_spell[static_cast<size_t>(k)] = c.req_spell[static_cast<size_t>(k - 1)];
      c.req_priority[static_cast<size_t>(k)] = c.req_priority[static_cast<size_t>(k - 1)];
      c.req_caster[static_cast<size_t>(k)] = c.req_caster[static_cast<size_t>(k - 1)];
    }
  }
  c.req_priority[static_cast<size_t>(at)] = prio;
  c.req_spell[static_cast<size_t>(at)] = static_cast<u8>(spell);
  c.req_caster[static_cast<size_t>(at)] = caster;
}

void spell_data_update() {
  BattleGlobals& g = bg();
  SpellCache& c = g_cache;
  for (int i = 0; i < 16; ++i) {
    if (c.slot_spell[static_cast<size_t>(i)] == 0 || c.slot_ready[static_cast<size_t>(i)] != 0) continue;
    // bload_done: the read is over (port: one frame after the request).
    if (c.slot_reading[static_cast<size_t>(i)] != 0) {
      c.slot_reading[static_cast<size_t>(i)] = 0;
    } else {
      c.slot_ready[static_cast<size_t>(i)] = 1;
    }
  }
  if (c.req_spell[0] == 0) return;
  int used = 0;
  for (int i = 0; i < 16; ++i)
    if (c.slot_spell[static_cast<size_t>(i)] != 0) ++used;
  if (2 < used) return;
  for (int i = 0; i < 16 && c.req_spell[static_cast<size_t>(i)] != 0; ++i) {
    const s32 p = static_cast<s32>(static_cast<u16>(c.req_priority[static_cast<size_t>(i)])) - g.frame_ticks;
    c.req_priority[static_cast<size_t>(i)] = static_cast<s16>(p);
    if (static_cast<s16>(p) < 0) c.req_priority[static_cast<size_t>(i)] = 0;
  }
  // (Port: the heap always has room for the entry; the console waits for entry size <=
  // free - 0x1b000.)
  if ((g.flags & 0x40) != 0) return;
  const s16 entry = entry_for(c.req_spell[0], c.req_caster[0]);
  for (int i = 0; i < 16; ++i) {
    if (c.slot_spell[static_cast<size_t>(i)] != 0) continue;
    c.slot_buffer[static_cast<size_t>(i)] = load_entry(entry);
    c.slot_ready[static_cast<size_t>(i)] = 0;
    c.slot_reading[static_cast<size_t>(i)] = 1;
    c.slot_spell[static_cast<size_t>(i)] = c.req_spell[0];
    c.slot_owner[static_cast<size_t>(i)] = c.req_caster[0];
    break;
  }
  c.req_spell[0] = 0;
  for (int k = 0; k < 15; ++k) {
    if (c.req_spell[static_cast<size_t>(k + 1)] == 0) continue;
    c.req_spell[static_cast<size_t>(k)] = c.req_spell[static_cast<size_t>(k + 1)];
    c.req_priority[static_cast<size_t>(k)] = c.req_priority[static_cast<size_t>(k + 1)];
    c.req_caster[static_cast<size_t>(k)] = c.req_caster[static_cast<size_t>(k + 1)];
    c.req_spell[static_cast<size_t>(k + 1)] = 0;
  }
}

int spell_data_status(int spell, Unit* caster) {
  if (entry_for(spell, caster) == 0) return 0;
  // (bload_done of the entry: the port's reads are never in flight at this point.)
  const SpellCache& c = g_cache;
  for (int i = 0; i < 16; ++i)
    if (c.req_spell[static_cast<size_t>(i)] == static_cast<u8>(spell) && c.req_caster[static_cast<size_t>(i)] == caster) return 1;
  for (int i = 0; i < 16; ++i)
    if (c.slot_spell[static_cast<size_t>(i)] == static_cast<u8>(spell) && c.slot_owner[static_cast<size_t>(i)] == caster)
      return c.slot_ready[static_cast<size_t>(i)] + 1;
  return 0;
}

SpellData spell_data_take(int spell, Unit* caster) {
  SpellCache& c = g_cache;
  for (int i = 0; i < 16; ++i) {
    if (c.slot_spell[static_cast<size_t>(i)] != static_cast<u8>(spell) || c.slot_owner[static_cast<size_t>(i)] != caster) continue;
    SpellData d = std::move(c.slot_buffer[static_cast<size_t>(i)]);
    c.slot_spell[static_cast<size_t>(i)] = 0;
    cache_compact();
    return d;
  }
  return nullptr;
}

void spell_data_clear_all() {
  SpellCache& c = g_cache;
  for (int i = 0; i < 16; ++i) {
    c.req_spell[static_cast<size_t>(i)] = 0;
    if (c.slot_buffer[static_cast<size_t>(i)] != nullptr) {
      c.slot_buffer[static_cast<size_t>(i)].reset();
      c.slot_ready[static_cast<size_t>(i)] = 0;
      c.slot_spell[static_cast<size_t>(i)] = 0;
    }
  }
}

void spell_data_cancel_owner(Unit* owner) {
  SpellCache& c = g_cache;
  for (int i = 0; i < 16; ++i) {
    if (c.slot_owner[static_cast<size_t>(i)] != owner) continue;
    c.slot_buffer[static_cast<size_t>(i)].reset();
    c.slot_spell[static_cast<size_t>(i)] = 0;
    cache_compact();
    return;
  }
  for (int i = 0; i < 16; ++i) {
    if (c.req_caster[static_cast<size_t>(i)] != owner) continue;
    c.req_spell[static_cast<size_t>(i)] = 0;
    cache_compact();
  }
}

// ---- spell_cast ----

void spell_cast(int spell, Unit* caster, Unit* target, int power) {
  if (spell < 0 || spell > 0x47 || caster == nullptr) return;
  SpellData data = spell_data_take(spell, caster);
  if (spell_needs_data(spell) == 1 && entry_for(spell, caster) != 0 && data == nullptr) return;
  if (spell < 0x40) {
    const int cost = unit_mp_cost(caster, spell_mp_cost_base(spell));
    Stats* s = caster->st();
    const s16 mp = s->mp;
    if (mp < cost) {
      battle_message_show(0xB);
      return;
    }
    if ((caster->status_bits & 2) != 0) {
      battle_message_show(0xE);
      return;
    }
    if ((caster->status_bits & 4) != 0) {
      battle_message_show(0xF);
      return;
    }
    s->mp = static_cast<s16>(mp - cost);
  }
  switch (spell) {
    case 1: spell_firebolt(caster, target, power); break;
    case 4: spell_ice_needle(caster, target, power); break;
    case 8: spell_wind_blade(caster, target, power); break;
    case 0xD: spell_thunderbolt(caster, target, power); break;
    case 0x17: spell_grave(caster, target, power); break;
    case 0x1E: spell_wounds(caster, target, power); break;
    case 0x2B: spell_word_of_death(caster, target, power); break;
    case 0x2E:
      if (SpellCast* sc = spellcast_create(7, caster, target, power, std::move(data))) {
        sc->spell = static_cast<u8>(spell);
        sc->s3c = 1;
      }
      break;
    case 0x2F: spell_heal(caster, target, power); break;
    case 0x30: spell_cure_light(caster, target, power); break;
    case 0x35: spell_antidote(caster, target); break;
    default: {
      g_spell_effect_started = 1;
      const int kind = spell_effect_kind(spell);
      if (kind == 0 || spell < 1) break;
      if (SpellCast* sc = spellcast_create(kind, caster, target, power, std::move(data))) sc->spell = static_cast<u8>(spell);
      break;
    }
  }
}

void spell_camera_focus(Unit* caster, Unit* target) {
  BattleGlobals& g = bg();
  PartyUnit* u = g.party_units[g.controlled_slot & 3];
  g.camera_next_target = target;
  if (static_cast<Unit*>(u) != caster) return;
  camera_end_scheduled();
  camera_end_mode(g.camera, 7, 0, caster);
  camera_request_mode(g.camera, 5, target);
}

// ---- The unit side ----

int unit_can_cast(Unit* u) {
  const Stats* s = u->st();
  if (s->mp == 0 || unit_is_down(u) || u->recovery_gauge != 0 || (u->status_bits & 6) != 0) return 0;
  return 1 < static_cast<u8>(u->action - 0xE) ? 1 : 0;
}

int Unit::can_cast() { return unit_can_cast(this); }

// (unit_mp_cost 0x80053f6c: killer.cpp.)

int unit_spell_mp_cost(Unit* u, int index) { return unit_mp_cost(u, spell_mp_cost_base(index + 1)); }

void unit_request_spell_data(Unit* u, int priority) {
  if (spell_needs_data(u->spell) == 1) spell_data_request(u->spell, priority, u);
}

void unit_begin_cast(Unit* u) {
  u->cast_time = static_cast<s16>(static_cast<u8>(spell_cast_ticks(u->spell)));
  if (u->status_test(1, 0x100000) != 0) {
    s32 t = u->cast_time * 3;
    if (t < 0) t += 3;
    u->cast_time = static_cast<s16>(t >> 2);
  }
  const u32 lvl = static_cast<u32>(u->skill_level(0x27));
  if (rng_range(0x14) < lvl) {
    u->cast_time = static_cast<s16>((u->cast_time * 10) / static_cast<s32>(lvl + 10));
    effect_skill_popup(u, 0x27);
  }
  unit_request_spell_data(u, u->cast_time);
  u->action = 0xE;
  u->clash = 0;
  u->counters[0] = 0;
  u->action_kind = 6;
}

void unit_cast_effects(Unit* u) {
  BattleGlobals& g = bg();
  const s32 y = u->pos[1] + u->size[1] * -0x2000;
  const s32 z = u->pos[2] - 0x4000;
  const u32 elem = static_cast<u32>(spell_element(u->spell));
  const s32 side = u->facing[0] < 1 ? -(u->size[0] * 0x3000) : u->size[0] * 0x3000;
  const s32 x = u->pos[0] + side * 2;
  const bool bright = elem == 0 || elem - 8 < 2 || elem == 6;
  const s32 t = u->counters[0];
  if (t < 0) {
    if (t != -1) return;
    u->counters[0] = -2;
    Shot* s = shot_spawn(5);
    if (s == nullptr) return;
    s->pos[0] = x;
    s->pos[1] = y;
    s->pos[2] = z;
    s->power = static_cast<s16>(elem);
    s->owner = u;
    return;
  }
  if (t == 0) {
    if (Shot* s = shot_spawn(3)) {
      s->scale_x = 0;
      s->scale_y = 0;
      s->owner = u;
      s->p128 = bright ? 1 : 0;
      s->power = static_cast<s16>(elem);
    }
  }
  if (u->counters[3] < 1) {
    if (Shot* s = shot_spawn(4)) {
      s->point[0] = x;
      s->point[1] = y;
      s->point[2] = z;
      if (bright) {
        s->p128 = static_cast<s32>(rng_range(4)) + 4;
      } else {
        const s32 r = static_cast<s32>(rng_range(3));
        s->p128 = r == 0 ? 0 : r + 5;
      }
      s->p12c = 0xC0;
      s->p130 = static_cast<s32>(rng_range(0x1000));
      s->p134 = static_cast<s32>(rng_range(0x1000));
      s->power = static_cast<s16>(elem);
    }
    u->counters[3] += 4;
  }
  if (g.flags == 0) {
    u->counters[0] += g.frame_ticks;
    u->counters[3] -= g.frame_ticks;
  }
}

int unit_update_casting(Unit* u) {
  BattleGlobals& g = bg();
  if (g.state != kStateFighting) {
    u->action_kind = 0;
    u->begin_action();
    return 1;
  }
  if (g.flags != 0) return 0;
  if (0x18 < u->counters[0]) {
    if (spell_needs_data(u->spell) != 1 || spell_data_status(u->spell, u) != 1) u->counters[0] = -1;
  }
  if (u->counters[0] < -1) {
    u->counters[0] -= g.frame_ticks;
    if (u->counters[0] < -0x24) {
      Unit* partner = unit_find_spell_clash(u);
      u->release_spell();
      if (partner != nullptr) partner->release_spell();
      g.camera_special = nullptr;
      return 1;
    }
  }
  unit_cast_effects(u);
  return 0;
}

Unit* unit_find_spell_clash(Unit* caster) {
  BattleGlobals& g = bg();
  Unit* best = nullptr;
  s32 longest = 0;
  for (int i = 0; i < 12; ++i) {
    Unit* u = i < 4 ? static_cast<Unit*>(g.party_units[static_cast<size_t>(i)])
                    : static_cast<Unit*>(g.enemy_units[static_cast<size_t>(i - 4)]);
    if (unit_is_down(u) || u == caster || u->action != 0xF) continue;
    const s32 t = u->counters[0];
    if (t < -10 && t < longest) {
      longest = t;
      best = u;
    }
  }
  if (best == nullptr) return nullptr;
  const u32 r = spell_clash_check(caster, best);
  if (r == 0) return nullptr;
  caster->clash = static_cast<s16>(r);
  best->clash = static_cast<s16>(r >> 16);
  return best;
}

u32 spell_clash_check(Unit* a, Unit* b) {
  auto b1 = [](u32 address) -> u8 {
    const u8* p = ovl1984b1_data(address);
    return p != nullptr ? *p : 0;
  };
  for (int i = 0; i < 7; ++i) {
    const u8 never = b1(0x80085504u + static_cast<u32>(i));
    if (a->spell == never || b->spell == never) return 0;
  }
  Unit* u[2] = {a, b};
  s32 elem[2] = {}, weight[2] = {};
  for (int k = 0; k < 2; ++k) {
    const int sp = u[k]->spell;
    const int idx = sp - 1;
    elem[k] = spell_element(sp);
    if (image_u8(0x8006B12Cu + static_cast<u32>(idx)) == 0) return 0;
    const u32 row = 0x8006B5BCu + static_cast<u32>(idx) * 6;
    s32 t = battle_s16(row + 2) * u[k]->st()->intel[2];
    if (t < 0) t += 0xFF;
    weight[k] = battle_s16(row) + (t >> 8);
    if (u[k]->is_enemy == 0) {
      const PartyUnit* p = static_cast<const PartyUnit*>(u[k]);
      s16 prof = 0;
      if (p->record != nullptr) std::memcpy(&prof, p->record + 0x8C + idx * 2, 2);
      s32 q = battle_s16(row + 4) * prof;
      if (q < 0) q += 0xFF;
      weight[k] += q >> 8;
    }
  }
  if (b1(0x80085358u + static_cast<u32>(elem[0])) == 0 || b1(0x80085358u + static_cast<u32>(elem[1])) == 0) return 0;
  const s32 tenth = (weight[0] < weight[1] ? weight[0] : weight[1]) / 10;
  const s32 diff = weight[0] - weight[1] < 0 ? weight[1] - weight[0] : weight[0] - weight[1];
  const bool decisive = tenth <= diff;
  if (elem[0] == elem[1] || b1(0x80085364u + static_cast<u32>(elem[0]) * 2) == elem[1] ||
      b1(0x80085365u + static_cast<u32>(elem[0]) * 2) == elem[1]) {
    if (a->is_enemy != b->is_enemy && !decisive) return 0x30003;
  } else {
    if (b1(0x80085358u + static_cast<u32>(elem[0])) != elem[1]) return 0;
    if (!decisive) return 0x10001;
  }
  return weight[1] < weight[0] ? 0x20004 : 0x40002;
}

}  // namespace ss::game::battle
