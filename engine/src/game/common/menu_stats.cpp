#include "game/common/menu_stats.h"

#include <cstdio>
#include <cstring>

#include "disc/slz.h"
#include "game/common/menu_core.h"

namespace ss::game::menu {
namespace {

s16 get16(const u8* p, int off) {
  s16 v;
  std::memcpy(&v, p + off, 2);
  return v;
}
void put16(u8* p, int off, s16 v) { std::memcpy(p + off, &v, 2); }
s32 get32(const u8* p, int off) {
  s32 v;
  std::memcpy(&v, p + off, 4);
  return v;
}
void put32(u8* p, int off, s32 v) { std::memcpy(p + off, &v, 4); }

// title 0x8003b1a4 family: field += field * pct / 100, clamped to 0..9999.
void mod_percent(u8* slot, int off, int pct) {
  const s16 v = get16(slot, off);
  s16 r = static_cast<s16>(v + v * pct / 100);
  if (r > 9999) r = 9999;
  if (r < 0) r = 0;
  put16(slot, off, r);
}

}  // namespace

StatCalc::StatCalc(bool adjust_levels) {
  const int saved = party_get_selected();
  for (int i = 0; i < 8 && adjust_levels; ++i) {
    party_select(i);
    if (party_slot_active() == 0) continue;
    u8* slot = globals().selected_slot;
    put16(slot, 0x26, get16(slot, 0x28));
    // Accessory 0x98 in either accessory slot raises it by one.
    if (char_attr(5) == 0x98 || char_attr(6) == 0x98) {
      if (get16(slot, 0x26) < 0xFF) put16(slot, 0x26, static_cast<s16>(get16(slot, 0x26) + 1));
    }
  }
  party_select(saved);
  items_.clear();
  if (globals().item_data.empty()) load_items();
}

StatCalc::~StatCalc() { all(); }

void StatCalc::load_items() {
  // A packed table of 0x338 items x 14 bytes in the program image (title 0x8004bcc4).
  const u8* packed = table(0x8004BCC4, 0);
  items_.assign(0x338 * 8, 0);
  if (!packed) return;
  auto raw = disc::slz::unpack_block(std::span<const u8>(packed, read_u32(packed + 4) + 0x10), 0, 0, nullptr);
  if (!raw || raw->size() < 0x338 * 14) return;
  const u8* p = raw->data();
  for (int i = 0; i < 0x338; ++i, p += 14) {
    u16* e = items_.data() + i * 8;
    e[0] = static_cast<u16>(p[0] + p[1] * 0x100);
    e[1] = static_cast<u16>(p[2] + p[3] * 0x100);
    e[2] = static_cast<u16>(p[4] + p[5] * 0x100);
    e[3] = static_cast<u16>(p[6] + p[7] * 0x100);
    e[4] = static_cast<u16>(p[8] + p[9] * 0x100);
    e[5] = static_cast<u16>(p[10] + p[11] * 0x100);
    e[6] = p[12];
    e[7] = p[13];
  }
}

void StatCalc::all() {
  const int saved = party_get_selected();
  for (int i = 0; i < 8; ++i) {
    party_select(i);
    if (party_slot_active() != 0) member();
  }
  party_select(saved);
}

void StatCalc::member() {
  for (u32 i = 0; i < 8; ++i) stat_set(i + 0x103, stat_get(i + 3));
  const u8* config = globals().config;
  const s32 hp = stat_get(1);
  stat_set(0x101, hp + hp * get16(config, 0x184) / 100);
  const s32 mp = stat_get(2);
  stat_set(0x102, mp + mp * get16(config, 0x186) / 100);
  equip_effects();
  for (u32 i = 0; i < 8; ++i) stat_set(i + 0x203, stat_get(i + 0x103));
  if (!globals().item_data.empty()) item_bonuses();
  for (u32 i = 0; i < 2; ++i) {
    const s32 max = stat_get(i + 0x101);
    const s32 cur = stat_get(i + 0x201);
    if (max < cur) stat_set(i + 0x201, max);
  }
}

void StatCalc::item_bonuses() {
  u8* slot = globals().selected_slot;
  u8* record = globals().selected_record;
  for (int i = 0; i < 7; ++i) {
    const int item = char_attr(i);
    if (item == 0) continue;
    put16(slot, 0x4C, static_cast<s16>(get16(slot, 0x4C) + item_data_get(item, 0x2C)));
    put16(record, 10, static_cast<s16>(get16(record, 10) + item_data_get(item, 0x14)));
    put16(record, 4, static_cast<s16>(get16(record, 4) + item_data_get(item, 0x16)));
  }
  if (get16(slot, 0x4C) > 0xFF) put16(slot, 0x4C, 0xFF);
  if (get16(record, 10) > 9999) put16(record, 10, 9999);
  if (get16(record, 4) > 0xFF) put16(record, 4, 0xFF);
}

u32 StatCalc::item_stat(int item, int field) const {
  if (item == 0) return 0;
  if (items_.empty()) {
    static const int offsets[8] = {4, 0x2A, 0x28, 8, 0x10, 0xC, 0xE, 0x12};
    if (field < 0 || field > 7) return 0;
    return static_cast<u32>(item_data_get(item, offsets[field]));
  }
  // Entry layout: +0 field 0, +2 field 3, +4 field 4, +6 field 5, +8 field 6, +0xa field 7,
  // +0xc field 2, +0xe field 1.
  static const int words[8] = {0, 7, 6, 1, 2, 3, 4, 5};
  if (field < 0 || field > 7) return 0;
  return items_[static_cast<size_t>((item - 1) * 8 + words[field])];
}

void StatCalc::equip_effects() {
  u8* slot = globals().selected_slot;
  int doubled = 0;
  for (int i = 0; i < 7; ++i) {
    const int item = char_attr(i);
    if (item == 0) continue;
    int hp = 0;
    switch (item_stat(item, 2)) {
      case 0x6F:
        stat_mod_hp_max(slot, 0x14);
        stat_mod_mp_max(slot, 0x14);
        continue;
      case 0x72: case 0xD1: stat_mod_mp_max(slot, 0x1E); continue;
      case 0x78: stat_mod_attr_2c(slot, 0x1E); continue;
      case 0x7A: case 0x7F: stat_mod_attr_32(slot, 0x1E); continue;
      case 0x80: case 0x9E: stat_mod_attr_32(slot, 10); continue;
      case 0x81: hp = 0x14; break;
      case 0x83: stat_mod_mp_max(slot, -100); continue;
      case 0x86: ++doubled; continue;
      case 0x87:
        stat_mod_attr_2c(slot, 0x14);
        stat_mod_attr_3e(slot, -5);
        continue;
      case 0x88: stat_mod_attr_2c(slot, 100); continue;
      case 0x91:
        stat_mod_attr_32(slot, 0x1E);
        stat_mod_attr_2c(slot, -0x1E);
        continue;
      case 0x96:
        stat_mod_attr_2c(slot, 0x28);
        stat_mod_attr_3e(slot, 0x14);
        continue;
      case 0xA7: stat_mod_attr_38(slot, 10); continue;
      case 0xAD:
        stat_mod_attr_2c(slot, 0x1E);
        stat_mod_attr_3e(slot, -10);
        continue;
      case 0xB2:
        stat_mod_attr_2c(slot, 100);
        stat_mod_attr_3e(slot, -0x32);
        continue;
      case 0xBC:
        stat_mod_attr_2c(slot, 10);
        stat_mod_attr_3e(slot, 0x1E);
        continue;
      case 0xBD: hp = 0x14; break;
      case 0xBE: stat_mod_attr_44(slot, 10); continue;
      case 0xCD: case 0xD4: hp = 0x1E; break;
      case 0xDB: hp = -10; break;
      case 0xEC: stat_mod_mp_max(slot, 10); continue;
      case 0xEE: stat_mod_mp_max(slot, 100); continue;
      case 0xEF: hp = -1000; break;
      default: continue;
    }
    stat_mod_hp_max(slot, hp);
  }
  for (; doubled != 0; --doubled) {
    stat_mod_hp_max(slot, 0x14);
    stat_mod_mp_max(slot, 0x14);
  }
}

void stat_mod_hp_max(u8* slot, int amount) {
  s32 v = get32(slot, 0x18);
  if (amount < 0) {
    v -= amount;
  } else {
    v += v * amount / 100;
  }
  if (v > 9999) v = 9999;
  put32(slot, 0x18, v);
}

void stat_mod_mp_max(u8* slot, int amount) {
  s16 v = get16(slot, 0x22);
  if (amount < 0) {
    v = static_cast<s16>(v - amount);
  } else {
    v = static_cast<s16>(v + static_cast<s16>(v * amount / 100));
  }
  if (v > 999) v = 999;
  put16(slot, 0x22, v);
}

void stat_mod_attr_2c(u8* slot, int pct) { mod_percent(slot, 0x2C, pct); }
void stat_mod_attr_32(u8* slot, int pct) { mod_percent(slot, 0x32, pct); }
void stat_mod_attr_44(u8* slot, int pct) { mod_percent(slot, 0x44, pct); }
void stat_mod_attr_38(u8* slot, int pct) { mod_percent(slot, 0x38, pct); }

void stat_mod_attr_3e(u8* slot, int pct) {
  // The original changes +0x3e but clamps +0x40.
  const s16 v = get16(slot, 0x3E);
  put16(slot, 0x3E, static_cast<s16>(v + static_cast<s16>(v * pct / 100)));
  if (get16(slot, 0x40) > 9999) put16(slot, 0x40, 9999);
  if (get16(slot, 0x40) < 0) put16(slot, 0x40, 0);
}

s32 item_data_get(int item, int field) {
  const auto& data = globals().item_data;
  if (data.empty()) {
    std::fprintf(stderr, "ERROR ReduceGetItemData %d %d\n", item, field);
    return 0;
  }
  if (item == 0) return 0;
  const size_t off = static_cast<size_t>(item - 1) * 0x30;
  if (off + 0x30 > data.size()) return 0;
  const u8* p = data.data() + off;
  if (field == 0) return get32(p, 0);
  if (static_cast<u32>(field - 4) < 0xF) return get16(p, field);
  return p[field];
}

}  // namespace ss::game::menu
