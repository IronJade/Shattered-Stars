#include "game/common/party.h"

#include <cstring>

#include "game/common/program_image.h"

namespace ss::game {
namespace {

struct Party {
  u8* config = nullptr;
  u8* inventory = nullptr;
  u8* party = nullptr;
  u8* records = nullptr;
  int selected = 0;               // title 0x8004ddc8 / field 0x80074078
  u8* slot = nullptr;             // title 0x8004ddcc / field 0x8007407c
  u8* record = nullptr;           // title 0x8004ddd0 / field 0x80074080
} g;

// The library's constant tables are the same bytes in the title and field images;
// they are read from the field's copies.
const u8* field_table(u32 address) { return program_data(3100, address); }

s16 rd16s(const u8* p) {
  s16 v;
  std::memcpy(&v, p, 2);
  return v;
}

}  // namespace

void (*g_voice_reload_member)(int slot) = nullptr;

void party_bind(u8* config, u8* inventory, u8* party, u8* records) {
  g.config = config;
  g.inventory = inventory;
  g.party = party;
  g.records = records;
  party_select(g.selected);
}

u8* party_config() { return g.config; }
u8* party_inventory() { return g.inventory; }
u8* party_slots() { return g.party; }
u8* party_records() { return g.records; }

int party_get_selected() { return g.selected; }

void party_select(int slot) {
  g.slot = g.party ? g.party + slot * 0x60 : nullptr;
  g.selected = slot;
  g.record = g.records ? g.records + slot * 0xD0 : nullptr;
}

u8* party_selected_slot() { return g.slot; }
u8* party_selected_record() { return g.record; }

int stat_group_offset(int group) {
  switch (group) {
    case 1: return 0x14;
    case 2: return 0x20;
    case 3: return 0x2A;
    case 4: return 0x30;
    case 5: return 0x36;
    case 6: return 0x3C;
    case 7: return 0x42;
    case 8: return 0x48;
    case 9: return 6;
    case 10: return 0;
    case 0xB: return 0xC;
    case 0xC: return 0xE;
    case 0xD: return 0x10;
    case 0xE: return 0x12;
    case 0xF: return 0x14;
    case 0x10: return 0x16;
    case 0x11: return 0x18;
    default: return 0;  // the original leaves its output untouched (0)
  }
}

s32 stat_get(u32 id) {
  const u8* kinds = field_table(0x80071968);
  const int group = static_cast<int>(id & 0x1F);
  const u8 kind = kinds ? kinds[group] : 0;
  const int index = static_cast<int>((id & 0xF00) >> 8);
  const u8* base = (kind & 1) ? g.record : g.slot;
  if (!base) return 0;
  const int off = stat_group_offset(group);
  if (kind & 2) {
    s32 v;
    std::memcpy(&v, base + off + index * 4, 4);
    return v;
  }
  return rd16s(base + off + index * 2);
}

void stat_set(u32 id, s32 value) {
  const u8* kinds = field_table(0x80071968);
  const int group = static_cast<int>(id & 0x1F);
  const u8 kind = kinds ? kinds[group] : 0;
  const int index = static_cast<int>((id & 0xF00) >> 8);
  u8* base = (kind & 1) ? g.record : g.slot;
  if (!base) return;
  const int off = stat_group_offset(group);
  if (kind & 2) {
    std::memcpy(base + off + index * 4, &value, 4);
  } else {
    const s16 v = static_cast<s16>(value);
    std::memcpy(base + off + index * 2, &v, 2);
  }
}

int party_selected_char() {
  if (!g.slot) return 0;
  const s16 id = rd16s(g.slot);
  return id < 1 ? 0 : id;
}

int party_slot_active(int slot) {
  const int saved = party_get_selected();
  party_select(slot);
  const int id = party_selected_char();
  party_select(saved);
  return id;
}

u32 char_equip_mask() {
  const int id = party_selected_char();
  if (id == 0) return 0;
  u32 mask = 1;
  for (int i = 1; i < id; ++i) mask <<= 1;
  return mask;
}

u16 config_get_button(int index) {
  if (index < 0 || index > 7 || !g.config) return 0;
  u16 v;
  std::memcpy(&v, g.config + index * 2, 2);
  return v;
}

s32 char_get_equip(int slot) { return stat_get(static_cast<u32>(slot + 0xB)); }
void char_set_equip(int slot, s32 item) { stat_set(static_cast<u32>(slot + 0xB), item); }

int char_skill_level(int skill) {
  if (!g.record) return 0;
  const int v = static_cast<s8>(g.record[0x5C + skill]);
  return v < 0 ? -v : v;
}

void char_specialty_info(int specialty, u16 out[6]) {
  const u8* table = field_table(0x8007197C);
  int needed = 0, learnt = 0, sum = 0;
  for (int i = 0; i < 3; ++i) {
    const u8 skill = table ? table[(specialty - 1) * 3 + i] : 0;
    out[i] = skill;
    int level = 0;
    if (skill != 0) {
      ++needed;
      level = char_skill_level(skill);
      if (level != 0) ++learnt;
    }
    sum += level;
  }
  int result = -1;
  if (sum != 0) {
    result = 0;
    if (needed == learnt) result = sum / needed;
  }
  if (result > 0) {
    if (char_get_equip(5) == 0x1B4) ++result;
    if (char_get_equip(6) == 0x1B4) ++result;
    if (result > 10) result = 10;
  }
  out[3] = static_cast<u16>(learnt);
  out[4] = static_cast<u16>(needed);
  out[5] = static_cast<u16>(result);
}

int char_specialty_level(int specialty) {
  u16 out[6];
  char_specialty_info(specialty, out);
  return static_cast<s16>(out[5]);
}

void party_super_specialty_info(int id, u16 out[5]) {
  const u8* table = field_table(0x800719AE);
  const u8 a = table ? table[id * 2] : 0;
  const u8 b = table ? table[id * 2 + 1] : 0;
  int skilled = 0, sum = 0;
  bool has_b = false;
  const int saved = party_get_selected();
  for (int i = 0; i < 8; ++i) {
    party_select(i);
    if (party_selected_char() == 0) continue;
    const int la = char_specialty_level(a);
    if (la > 0) sum += la;
    if (la > 3) ++skilled;
    if (char_specialty_level(b) > 0) has_b = true;
  }
  party_select(saved);
  u16 state = skilled < 2 ? 0 : 1;
  if (has_b) ++state;
  int level = 0;
  if (state == 0) level = -1;
  if (state == 2) {
    level = sum / 3;
    if (level > 10) level = 10;
  }
  out[2] = state;
  out[0] = a;
  out[1] = b;
  out[3] = 2;
  out[4] = static_cast<u16>(level);
}

int party_super_specialty_level(int id) {
  u16 out[5];
  party_super_specialty_info(id, out);
  return static_cast<s16>(out[4]);
}

int party_find_bonded(int character) {
  const int saved = party_get_selected();
  int found = 0;
  for (int i = 0; i < 4; ++i) {
    party_select(i);
    const int id = party_selected_char();
    if (id != 0 && g.record[0x3B] == static_cast<u32>(character)) found = id;
  }
  party_select(saved);
  return found;
}

void party_update_bonds() {
  const int saved = party_get_selected();
  for (int i = 0; i < 4; ++i) {
    party_select(i);
    if (party_selected_char() == 0) continue;
    bool moved = false;
    if (g.record[0xCE] != 0 && g.record[0xCC] == 0) {
      g.record[0xCC] = g.record[0xCE];
      moved = true;
      g.record[0xCE] = 0;
    }
    if (g.record[0xCF] != 0 && g.record[0xCD] == 0) {
      g.record[0xCD] = g.record[0xCF];
      moved = true;
      g.record[0xCF] = 0;
    }
    if (moved && g_voice_reload_member) g_voice_reload_member(i);
  }
  for (int i = 0; i < 4; ++i) {
    party_select(i);
    if (party_selected_char() == 0) continue;
    const s32 acc5 = char_get_equip(5);
    const s32 acc6 = char_get_equip(6);
    bool keep = acc6 == 0x15C || acc5 == 0x15C;
    const u8 partner = g.record[0x3B];
    if (partner == 0) {
      keep = false;
    } else {
      bool in_front = false;
      for (int j = 0; j < 4; ++j)
        if (party_slot_active(j) == partner) in_front = true;
      if (partner == 0x10) in_front = true;
      if (!in_front) keep = false;
    }
    if (!keep) {
      g.record[0x3B] = 0;
      g.record[0xCE] = 0;
      g.record[0xCF] = 0;
    }
  }
  for (int i = 0; i < 4; ++i) {
    party_select(i);
    const int id = party_selected_char();
    if (id != 0 && party_find_bonded(id) != 0) {
      g.record[0xCC] = g.record[0xCD] = g.record[0xCE] = g.record[0xCF] = 0;
    }
  }
  party_select(saved);
}

int inventory_find(const u8* inventory, int item) {
  for (int i = 0; i < 0x400; ++i) {
    u16 v;
    std::memcpy(&v, inventory + i * 2, 2);
    if ((v & 0x3FF) == static_cast<u32>(item)) return i;
  }
  return -1;
}

int inventory_count(int item) {
  if (!g.inventory) return 0;
  const int i = inventory_find(g.inventory, item);
  if (i < 0) return 0;
  u16 v;
  std::memcpy(&v, g.inventory + i * 2, 2);
  return v >> 10 & 0x1F;
}

u8 party_selected_status() { return g.slot ? g.slot[2] : 0; }

// ---- StatChange ------------------------------------------------------------------------

s32 clamp_max(s32 value, s32 max) {
  bool over = max < value;
  if (value < 0) {
    value = 0;
    over = max < 0;
  }
  if (over) value = max;
  return value;
}

s32 percent_of(s32 value, s32 pct) { return value * pct / 100; }

void stat_change_clear(StatChange* sc) { *sc = StatChange{}; }

StatChange* stat_change_begin(StatChange* sc) {
  stat_change_clear(sc);
  sc->apply = 1;
  return sc;
}

s32 stat_change_get(StatChange*, int which) {
  if (which == 7) {
    s32 v = 0;
    if (g.config) std::memcpy(&v, g.config + 0x18, 4);
    return v;
  }
  if (!g.slot) return 0;
  s32 v = 0;
  switch (which) {
    case 1: std::memcpy(&v, g.slot + 0x14, 4); break;
    case 2: std::memcpy(&v, g.slot + 0x18, 4); break;
    case 3: std::memcpy(&v, g.slot + 0x1C, 4); break;
    case 4: v = rd16s(g.slot + 0x20); break;
    case 5: v = rd16s(g.slot + 0x22); break;
    case 6: v = rd16s(g.slot + 0x24); break;
    default: break;
  }
  return v;
}

void stat_change_set(StatChange* sc, int which, s32 value) {
  if (which >= 1 && which <= 3) {
    const s32 v = clamp_max(value, 9999);
    if (stat_change_get(sc, which) != v) {
      sc->hp_changed = 1;
      if (sc->apply && g.slot) std::memcpy(g.slot + 0x14 + (which - 1) * 4, &v, 4);
    }
  } else if (which >= 4 && which <= 6) {
    const s32 v = clamp_max(value, 999);
    if (stat_change_get(sc, which) != v) {
      sc->mp_changed = 1;
      const s16 h = static_cast<s16>(v);
      if (sc->apply && g.slot) std::memcpy(g.slot + 0x20 + (which - 4) * 2, &h, 2);
    }
  } else if (which == 7) {
    const s32 v = clamp_max(value, 999999999);
    if (stat_change_get(sc, which) != v) {
      sc->money_changed = 1;
      if (sc->apply && g.config) std::memcpy(g.config + 0x18, &v, 4);
    }
  }
}

void stat_change_heal_pct(StatChange* sc, int is_mp, int pct) {
  if (party_selected_status() & 7) return;
  const int max_id = is_mp ? 5 : 2;
  const int cur_id = is_mp ? 6 : 3;
  const s32 max = stat_change_get(sc, max_id);
  const s32 cur = stat_change_get(sc, cur_id);
  stat_change_set(sc, cur_id, clamp_max(cur + percent_of(max, pct), max));
}

void stat_change_heal_hp_pct(StatChange* sc, int pct) { stat_change_heal_pct(sc, 0, pct); }

void stat_change_heal(StatChange* sc, int is_mp, int amount) {
  if (party_selected_status() & 7) return;
  const int max_id = is_mp ? 5 : 2;
  const int cur_id = is_mp ? 6 : 3;
  const s32 max = stat_change_get(sc, max_id);
  const s32 cur = stat_change_get(sc, cur_id);
  s32 v = clamp_max(cur + amount, max);
  if (v < 1) v = 1;
  stat_change_set(sc, cur_id, v);
}

void stat_change_add_money(StatChange* sc, int delta) { stat_change_set(sc, 7, stat_change_get(sc, 7) + delta); }

s32 stat_change_get_attr(StatChange*, int i) { return stat_get(static_cast<u32>(i + 3)); }

void party_heal_all(StatChange* sc, int is_mp, int amount) {
  const int saved = party_get_selected();
  for (int i = 0; i < 8; ++i) {
    party_select(i);
    if (party_selected_char() != 0) stat_change_heal(sc, is_mp, amount);
  }
  party_select(saved);
}

}  // namespace ss::game
