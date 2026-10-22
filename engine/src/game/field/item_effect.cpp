#include "game/field/item_effect.h"

#include <cstring>

#include "game/boot/loader.h"
#include "game/boot/rng.h"
#include "game/common/menu_core.h"
#include "game/common/menu_scene.h"
#include "game/common/menu_stats.h"
#include "game/common/program_image.h"
#include "game/field/callways.h"
#include "game/field/field.h"
#include "game/field/script_vm.h"
#include "runtime/runtime.h"

namespace ss::game::field {
namespace {

constexpr int kEffectCode = 3001;   // entry 3001 at 0x80088000

u8 effect_byte(u32 address) {
  const u8* p = program_data_at(kEffectCode, 0x80088000, address);
  return p ? *p : 0;
}
s16 effect_s16(u32 address) {
  const u8* p = program_data_at(kEffectCode, 0x80088000, address);
  if (!p) return 0;
  s16 v;
  std::memcpy(&v, p, 2);
  return v;
}

bool script_flag(int n) { return ((globals().script_flags[n >> 3] >> (n & 7)) & 1) != 0; }   // 0x80055ecc
void script_flag_set(int n) { globals().script_flags[n >> 3] |= static_cast<u8>(1 << (n & 7)); }   // 0x80055efc

s16 rd16s(const u8* p) {
  s16 v;
  std::memcpy(&v, p, 2);
  return v;
}
s32 rd32s(const u8* p) {
  s32 v;
  std::memcpy(&v, p, 4);
  return v;
}

// 0x8003363c: 'count' (1 when 0) of 'item' into the inventory, marked new.
void inventory_give(int item, int count) {
  if (item == 0) return;
  inventory_add(globals().inventory, item, count != 0 ? count : 1, 1);
}

// The generator's state while an item is tried (0x8008bb3c / 0x8008bb78).
struct RngSave {
  u32 table[56] = {};
  s32 index = 0;
  RngSave() { rng_save(table, &index); }
  ~RngSave() { rng_restore(table, index); }
};

}  // namespace

// ==== Entry points ========================================================================

int ItemEffect::kind(int item_id) {
  {
    RngSave saved;
    stat_change_clear(this);
    run(item_id, 0);
  }
  if (closes) return 5;
  if (field) return 4;
  if (all) return 3;
  if (front) return 2;
  return one != 0 ? 1 : 0;
}

int ItemEffect::usable(int item_id, int slot) {
  {
    RngSave saved;
    stat_change_clear(this);
    run(item_id, slot);
  }
  if (money_changed) return 4;
  if (status_changed) return 3;
  if (mp_changed) return 2;
  return hp_changed != 0 ? 1 : 0;
}

s16 ItemEffect::apply(int item_id, int slot) {
  stat_change_clear(this);
  this->apply_flag() = 1;
  run(item_id, slot);
  return sound;
}

// ==== The effect ==========================================================================

void ItemEffect::run(int item_id, int slot) {
  FieldGlobals& g = globals();
  const int saved = party_get_selected();
  sound = 0;
  item = static_cast<s16>(item_id);
  member = static_cast<s16>(slot);
  party_select(static_cast<s16>(slot));
  bool by_id = false;
  switch (menu::item_data_get(item_id, 0x28)) {
    case 1: heal(1, 1, 0); break;
    case 2: heal(1, 3, 0); break;
    case 3: heal(2, 1, 0); break;
    case 4: heal(2, 3, 0); break;
    case 5: heal(3, 1, 0); break;
    case 6:
      revive();
      if (status_changed) heal(1, 1, 0);
      break;
    case 0xA: {
      const int keep = party_get_selected();
      for (int i = 0; i < 8; ++i) {
        party_select(i);
        if (party_selected_char() != 0) revive();
      }
      party_select(keep);
      heal(3, 3, 100);
      break;
    }
    case 0x15: random_items(1); break;
    case 0x20: random_items(3); break;
    case 0x21: {
      // Half the time an item, else a battle (game_state +0x264 = 0x227: camp result 3).
      bool can = false;
      if (g.field_flags[0x25] != 0 && !script_flag(0xC)) can = !script_flag(0xE);
      if (!can) break;
      if (stat_change_chance(this, 0x32)) {
        random_items(1);
        break;
      }
      closes = 1;
      money_changed = 1;
      if (apply_flag()) {
        special = 2;
        const s16 v = 0x227;
        std::memcpy(g.game_state + 0x264, &v, 2);
      }
      break;
    }
    case 0x25:
      add_status(1);
      if (status_changed) {
        stat_change_set(this, 3, 0);
        sound = 0x23;
      }
      break;
    case 0x26:
    case 0x52: remove_status(0xE); break;
    case 0x27:
      add_status(8);
      if (status_changed) sound = 0x23;
      break;
    case 0x2A: {
      // HP and MP change places (each up to the other's maximum).
      one = 1;
      const s32 hp = stat_change_get(this, 3);
      const s32 mp = stat_change_get(this, 6);
      if (hp != mp && hp != 0 && mp != 0) {
        stat_change_set(this, 3, clamp_max(mp, stat_change_get(this, 2)));
        stat_change_set(this, 6, clamp_max(hp, stat_change_get(this, 5)));
      }
      break;
    }
    case 0x2C:
      revive();
      if (status_changed) heal(3, 1, 0x1E);
      break;
    case 0x2F: config_mark_selected(1); break;
    case 0x30: heal(2, 1, 0x1E); break;
    case 0x35: set_flag(0x26); break;
    case 0x39:
      revive();
      if (status_changed) {
        heal(1, 1, 100);
        const u32 r = rng_range(3);
        if (r == 1) {
          add_status(4);
        } else if (r == 0) {
          add_status(2);
        } else if (r == 2) {
          add_status(8);
        }
      }
      break;
    case 0x3B: {
      heal(1, 1, 0x1E);
      const u32 r = rng_range(3);
      if (r == 1) {
        add_status(4);
      } else if (r == 0) {
        add_status(2);
      } else if (r == 2) {
        add_status(8);
      }
      break;
    }
    case 0x3C:
      if ((party_selected_status() & 0xE) == 0) {
        add_status(4);
      } else {
        remove_status(0xE);
        heal(1, 1, 0x1E);
      }
      break;
    case 0x3D: {
      field = 1;
      menu::MenuSession* s = menu::globals().session;
      if (s && s->creation_aid == 0) {
        money_changed = 1;
        if (apply_flag()) s->creation_aid = 1;
      }
      break;
    }
    case 0x3E: {
      field = 1;
      menu::MenuSession* s = menu::globals().session;
      if (s && s->creation_bonus == 0) {
        money_changed = 1;
        if (apply_flag()) s->creation_bonus = 100;
      }
      break;
    }
    case 0x40: {
      const u32 r = rng_range(2);
      if (r == 0) {
        heal(1, 1, 0x46);
        if (!hp_changed) heal(2, 1, 0x46);
      } else if (r == 1) {
        heal(2, 1, 0x46);
        if (!mp_changed) heal(1, 1, 0x46);
      }
      break;
    }
    case 0x41:
    case 0x5D:
      remove_status(8);
      heal(1, 1, 10);
      break;
    case 0x42:
      remove_status(2);
      heal(1, 1, 10);
      break;
    case 0x43:
      revive();
      remove_status(0xE);
      break;
    case 0x47:
      if (stat_change_chance(this, 0x3C)) {
        heal(3, 1, 100);
      } else {
        add_status(1);
        if (status_changed) stat_change_set(this, 3, 0);
      }
      break;
    case 0x4B: set_flag(0x24); break;
    case 0x4C: config_mark_selected(2); break;
    case 0x5E:
      remove_status(2);
      heal(1, 1, 0x1E);
      heal(2, 1, 0xF);
      break;
    case 0x60: {
      // On maps 0x2ba-0x339 only: the menu closes with camp result 6 (a teleport).
      const s16 map = rd16s(g.game_state + 0x1A);
      if (static_cast<u32>(map - 0x2BA) < 0x80) {
        closes = 1;
        money_changed = 1;
        if (apply_flag()) {
          special = 3;
          if (menu::MenuSession* s = menu::globals().session) s->unknown35 = 1;
        }
      }
      break;
    }
    case 0x62: remove_status(4); break;
    default: by_id = true; break;
  }

  if (by_id) {
    switch (item) {
      case 0x1B9: play_song(); break;
      case 0x1D0: character_flag(6, 8); break;
      case 0x1D1: character_flag(6, 9); break;
      case 0x1D2: character_flag(6, 3); break;
      case 0x1D3: character_flag(9, 0); break;
      case 0x1D4: character_flag(9, 7); break;
      case 0x1D5: character_flag(9, 9); break;
      case 0x1DB: random_items(3); break;
      case 0x1DF: set_flag(0x2C9); break;
      case 0x1E1: {
        // One experience point short of the next level.
        one = 1;
        u8* slot_p = party_selected_slot();
        const s16 level = rd16s(slot_p + 0x26);
        if (level < 100) {
          const s32 target = exp_for_level(level) - 1;
          if (rd32s(slot_p + 0x10) != target) {
            money_changed = 1;
            if (apply_flag()) std::memcpy(slot_p + 0x10, &target, 4);
          }
        }
        break;
      }
      case 0x1E2: random_items(1); break;
      case 0x1E3: map_flag(); break;
      case 0x1E4:
        // Money for a member who is down: 1000 a level.
        one = 1;
        if (party_selected_status() & 1) {
          money_changed = 1;
          if (apply_flag()) stat_change_add_money(this, rd16s(party_selected_slot() + 0x26) * 1000);
        }
        break;
      case 0x2F2: set_flag(0x2C8); break;
      default: {
        const u32 id = static_cast<u16>(item);
        if (id - 0xA6 < 0x16) skill_book(effect_byte(0x8008D18A + id));
        if (id - 6 < 0xC) special_page(effect_byte(0x8008D242 + id));
        if (id - 0xC4 < 0xC) config_mark(1, party_selected_char(), effect_byte(0x8008D190 + id));
        if (id - 0xD0 < 0xC) config_mark(2, party_selected_char(), effect_byte(0x8008D184 + id));
        break;
      }
    }
  }

  party_select(saved);
  if (sound == 0) {
    if (hp_changed || mp_changed || status_changed) {
      sound = 0x13;
    } else if (money_changed || attr_changed) {
      sound = 0x28;
    }
  }
}

void ItemEffect::revive() {
  remove_status(1);
  if (status_changed) stat_change_set(this, 3, 1);
}

void ItemEffect::map_flag() {
  field = 1;
  const s16 map = rd16s(globals().game_state + 0x1A);
  for (int i = 0;; ++i) {
    const s16 lo = effect_s16(0x8008D260 + i * 4);
    if (lo == 0) return;
    const s16 hi = effect_s16(0x8008D262 + i * 4);
    if (lo <= map && map <= hi) {
      set_flag(i + 0x2CB);
      return;
    }
  }
}

void ItemEffect::add_status(u8 bits) {
  one = 1;
  const u8 s = party_selected_status();
  if ((s & bits) == 0) stat_change_set_status(this, static_cast<u8>(s | bits));
}

void ItemEffect::remove_status(u8 bits) {
  one = 1;
  const u8 s = party_selected_status();
  if ((s & bits) != 0) stat_change_set_status(this, static_cast<u8>(s & ~bits));
}

void ItemEffect::play_song() {
  field = 1;
  money_changed = 1;
  if (!apply_flag()) return;
  // The original loads entry 0xa9b with a background load and waits for it.
  std::vector<u8> song;
  if (Loader* loader = menu::menu_loader()) {
    const int size = loader->entry_size(0xA9B);
    if (size > 0) {
      song.assign(static_cast<size_t>(size), 0);
      loader->load_sync(song.data(), 0xA9B, size);
    }
  }
  if (!song.empty() && callways() != nullptr) callways_play_song(callways(), song, 10, 10);
}

void ItemEffect::random_items(int count) {
  field = 1;
  money_changed = 1;
  if (!apply_flag()) return;
  for (int i = 0; i < count; ++i) {
    int pick;
    do {
      pick = static_cast<int>(rng_range(0x3FF));
    } while (menu::item_data_get(pick + 1, 0x2B) < 0x1E);
    inventory_give(pick + 1, 0);
  }
}

void ItemEffect::character_flag(int character, int n) {
  one = 1;
  u8* record = party_selected_record();
  if (party_selected_char() == character && record != nullptr && record[n + 0x3C] == 0) {
    money_changed = 1;
    if (apply_flag()) record[n + 0x3C] = 1;
  }
}

void ItemEffect::set_flag(int flag) {
  field = 1;
  if (!script_flag(flag)) {
    money_changed = 1;
    if (apply_flag()) script_flag_set(flag);
  }
}

void ItemEffect::skill_book(int skill) {
  one = 1;
  const bool allowed = skill_book_allowed(skill);
  const int level = char_skill_level(skill);
  if ((level != 0 || allowed) && level < 5) {
    money_changed = 1;
    if (apply_flag()) {
      char_set_skill_level(skill, level + 1);
      StatChange bonus;
      stat_change_begin(&bonus);   // 0x8008b4fc
      skill_level_bonus(&bonus, skill);
    }
  }
}

void ItemEffect::special_page(int value) {
  closes = 1;
  money_changed = 1;
  if (apply_flag()) {
    special = 1;
    param = value;
  }
}

void ItemEffect::config_mark(int table, int character, int n) {
  one = 1;
  money_changed = 1;
  if (!apply_flag()) return;
  int offset;
  if (table == 1) {
    offset = (character - 1) * 0xC + 0xE8;
  } else if (table == 2) {
    offset = (character - 1) * 0xC + 0x58;
  } else {
    return;
  }
  if (u8* config = party_config()) config[offset + n - 1] = 8;
}

void ItemEffect::config_mark_selected(int table) {
  const u8* config = party_config();
  const int n = (config && config[0x45] != 0) ? 2 : 1;
  config_mark(table, party_selected_char(), n);
}

void ItemEffect::heal(u32 what, int target, int amount) {
  const int saved = party_get_selected();
  if (amount == 0) amount = menu::item_data_get(item, 0x19);
  for (int i = 0; i < 8; ++i) {
    party_select(i);
    bool hit = false;
    if (target == 2) {
      front = 1;
      hit = member <= i && i < member + 4;
    } else if (target == 1) {
      one = 1;
      hit = i == member;
    } else if (target == 3) {
      all = 1;
      hit = true;
    }
    const u32 id = static_cast<u16>(item);
    int value = amount;
    if (id - 0x306 < 0x30 || id - 0x2F6 < 0xC || id - 0x117 < 0x8C) value = amount + char_skill_level(0x12);
    if (item < 0x2F6 && item > 0x2F3) value += char_skill_level(6) * 3;
    if (!hit) continue;
    const int character = party_selected_char();
    if (character == 0) continue;
    if (effect_s16(0x8008D216 + character * 2) == item) {
      own_item();
      continue;
    }
    if (what & 1) stat_change_heal_hp_pct(this, value);
    if (what & 2) stat_change_heal_mp_pct(this, value);
  }
  party_select(saved);
}

void ItemEffect::own_item() {
  // A character's favourite: full recovery with enough of skill 0x11.
  sound = 0x16;
  if (u8* record = party_selected_record()) record[0x3A] |= 2;
  const int level = char_skill_level(0x11);
  const int hp = level > 0 ? 100 : 0x3C;
  const int mp = level > 4 ? 100 : 0x3C;
  if (level > 9) remove_status(0xE);
  stat_change_heal_hp_pct(this, hp);
  stat_change_heal_mp_pct(this, mp);
}

// ==== Skills ==============================================================================

bool skill_book_allowed(int skill) {
  for (int i = 0;; ++i) {
    const s8 v = static_cast<s8>(effect_byte(0x8008D2D8 + i));
    if (v == -1) return false;
    if (v == skill) return script_flag(i / 5 + 700);
  }
}

void skill_level_bonus(StatChange* sc, int skill) {
  const int level = char_skill_level(skill);
  int attr = -1;
  int amount = 0;
  switch (skill) {
    case 2:
    case 3: attr = 2, amount = level == 1 ? 2 : 1; break;
    case 5: attr = 4, amount = 3; break;
    case 7: attr = 2, amount = level == 1 ? 3 : 1; break;
    case 9: attr = 3, amount = level == 1 ? 3 : 1; break;
    case 0xA: {
      u8* slot = party_selected_slot();
      const s32 exp = rd32s(slot + 0x10);
      const s32 v = exp + ((exp_for_level(rd16s(slot + 0x26)) - exp) * level) / 25;
      std::memcpy(slot + 0x10, &v, 4);
      break;
    }
    case 0xC: attr = 1, amount = 2; break;
    case 0xD: attr = 6, amount = 3; break;
    case 0xE: stat_change_add_hp_mp(sc, 0, (level * 2 - 1) * 10); break;
    case 0xF: stat_change_add_hp_mp(sc, 1, 5); break;
    case 0x10: attr = 0, amount = 0x14; break;
    case 0x15: attr = 3, amount = 2; break;
    case 0x16: attr = 0, amount = 10; break;
    case 0x17: attr = 4, amount = level == 1 ? 2 : 1; break;
    case 0x18: {
      // A random present: 60 % materials, 30 % and 10 % rarer things.
      const u32 r = rng_range(100);
      const u32 pick = rng_range(5);
      if (r < 0x3C) {
        static constexpr int kCommon[5] = {0x2F3, 0x2F4, 0x2F5, 0x302, 0x36};
        if (pick < 3) {
          inventory_give(kCommon[pick], 5);
        } else if (pick < 5) {
          inventory_give(kCommon[pick], 0);
        }
      } else if (r < 0x5A) {
        static constexpr int kRare[5] = {0xAC, 0x1A7, 0x1A6, 0x1A9, 0x41};
        if (pick < 5) inventory_give(kRare[pick], 0);
      } else {
        static constexpr int kRarest[5] = {0x59, 0x5A, 0x6B, 0x6A, 0x9D};
        if (pick < 5) inventory_give(kRarest[pick], 0);
      }
      break;
    }
    case 0x19: {
      const int i = static_cast<int>(rng_range(8));
      const s32 v = stat_change_get_attr(sc, i);
      stat_change_set_attr(sc, i, v + percent_of(v, 3));
      break;
    }
    case 0x1A: {
      const s32 money = (level * level * level - (level - 1) * (level - 1) * (level - 1)) * 100;
      runtime::Runtime::get().log_once("menu:up_gold", "menu: skill 0x1a money bonus");  // "up gold %d"
      stat_change_add_money(sc, money);
      break;
    }
    case 0x1B:
      stat_change_add_attr(sc, 0, 6);
      stat_change_add_attr(sc, 4, 6);
      stat_change_add_attr(sc, 2, 6);
      stat_change_add_attr(sc, 3, 6);
      break;
    case 0x1D: attr = 5, amount = 3; break;
    default: break;
  }
  if (attr != -1) stat_change_add_attr(sc, attr, amount);
}

}  // namespace ss::game::field
