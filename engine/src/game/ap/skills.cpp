#include "game/ap/skills.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>

#include "game/ap/ap.h"
#include "game/ap/seed.h"
#include "game/battle/result.h"
#include "game/boot/loader.h"
#include "game/boot/system.h"
#include "game/common/program_image.h"
#include "game/field/field.h"

namespace ss::game::ap {
namespace {

void log(const std::string& line) { std::fprintf(stderr, "[ap] %s\n", line.c_str()); }

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
void wr16(u8* p, s32 v) {
  const s16 x = static_cast<s16>(v);
  std::memcpy(p, &x, 2);
}
void wr32(u8* p, s32 v) { std::memcpy(p, &v, 4); }

// The seed's skillsanity locations (skills_index).
std::map<int, int64_t> g_learn;           // character * 256 + record index -> learn location
std::vector<int64_t> g_guild[12];         // set -> its guild locations
int64_t g_float = 0;                      // the Float prize's learn location (0: none)

int move_key(int character, int index) { return character * 256 + index; }

int moves_known(const u8* rec) {
  int n = 0;
  for (int i = 0; i < 0x20; ++i) n += rec[0x3C + i] != 0;
  return n;
}

bool move_received(int character, int index) {
  const std::vector<int>& m = mutable_state().moves;
  return std::find(m.begin(), m.end(), move_key(character, index)) != m.end();
}

int64_t learn_location(int character, int index) {
  const auto it = g_learn.find(move_key(character, index));
  return it == g_learn.end() ? 0 : it->second;
}

// A move known / forgotten (record +0x3c + index; its use count +0x8c + 2 * index zeroed as a
// learn does; a forgotten killer move leaves the move slots +0xcc..+0xcf, as ext 0xfc).
void know(u8* rec, int index) {
  if (rec[0x3C + index] != 0) return;
  rec[0x3C + index] = 1;
  wr16(rec + 0x8C + index * 2, 0);
}
void forget(u8* rec, int index) {
  rec[0x3C + index] = 0;
  wr16(rec + 0x8C + index * 2, 0);
  for (int i = 0; i < 4; ++i)
    if (rec[0xCC + i] == index + 1) rec[0xCC + i] = 0;
}

u8* party_base() { return field::globals().party; }

// The disc's tables are read only with the disc open (program_data keeps an image it found empty,
// which would hide the disc from a later reader: the unit tests run without it).
bool disc_ready() { return get_system() != nullptr && get_system()->loader.archive() != nullptr; }
u8* records_base() { return field::globals().records; }

// The slot of a member (party id), active or in reserve; -1 none.
int slot_of(int character) {
  u8* party = party_base();
  if (party == nullptr) return -1;
  for (int i = 0; i < 8; ++i) {
    const s16 id = rd16s(party + i * 0x60);
    if ((id < 0 ? -id : id) == character) return i;
  }
  return -1;
}

// Skillsanity at a member's join (and the heroes' at New Game), at level 'level': each learn
// point the seed lists is a check (those up to the level fire now) and its move is not known
// unless received; the first move is known; the received moves are known.
void special_pass(u8* rec, int character, int level) {
  const std::vector<battle::LearnEntry> table = disc_ready() ? battle::learn_table(character) : std::vector<battle::LearnEntry>{};
  const int first = table.empty() ? -1 : table.front().index;
  int fired = 0, forgotten = 0;
  for (const battle::LearnEntry& e : table) {
    const int64_t loc = learn_location(character, e.index);
    if (loc == 0) {
      if (e.index == first) know(rec, e.index);
      continue;
    }
    if (!move_received(character, e.index) && rec[0x3C + e.index] != 0) {
      forget(rec, e.index);
      ++forgotten;
    }
    if (e.level <= level && !is_checked(loc)) {
      deliver_check(loc);
      ++fired;
    }
  }
  int received = 0;
  for (const int k : mutable_state().moves) {
    if (k / 256 != character) continue;
    know(rec, k % 256);
    ++received;
  }
  log(std::string("skillsanity: ") + character_name(character) + " at level " + std::to_string(level) + ": " +
      std::to_string(fired) + " learn points checked, " + std::to_string(forgotten) + " moves to find, " +
      std::to_string(received) + " received known");
}

}  // namespace

// ---- The join level ------------------------------------------------------------------------

int join_target_level(const u8* party, int slot) {
  std::vector<int> levels;
  for (int i = 0; i < 8; ++i) {
    if (i == slot) continue;
    const s16 id = rd16s(party + i * 0x60);
    if (id < 1 || id > kCharacterCount) continue;
    levels.push_back(rd16s(party + i * 0x60 + 0x28));
  }
  if (levels.empty()) return 0;
  std::sort(levels.begin(), levels.end());
  const size_t n = levels.size();
  const int median = n % 2 == 1 ? levels[n / 2] : (levels[n / 2 - 1] + levels[n / 2]) / 2;
  return std::clamp(median, 1, 255);
}

Growth growth_row(int character) {
  Growth g;
  if (character < 1 || character > kCharacterCount) return g;
  const u32 row = 0x80087F94 + static_cast<u32>((character - 1) * 0xC);
  for (int k = 0; k < 6; ++k) {
    const u8* range = program_data_at(1981, 0x80085518u, row + static_cast<u32>(k * 2));
    const u8* base = program_data_at(1981, 0x80085518u, row + static_cast<u32>(k * 2 + 1));
    g.range[k] = range != nullptr ? *range : 0;
    g.base[k] = base != nullptr ? *base : 0;
  }
  return g;
}

void scale_down_stats(u8* slot, u8* rec, int target, const Growth& g) {
  const int own = rd16s(slot + 0x28);
  if (target < 1 || target >= own) return;
  const int n = own - target;
  // Never below the floor, never raised by it (a value under it stays).
  auto lower16 = [](u8* p, s32 total, s32 floor) {
    const s32 v = rd16s(p);
    if (v > floor) wr16(p, std::max(floor, v - total));
  };
  s32 hp = 0;
  for (int level = target; level < own; ++level) hp += level < 50 ? level * 3 / 2 + 32 : 94;
  for (const int off : {0x14, 0x18, 0x1C}) {
    const s32 v = rd32s(slot + off);
    if (v > 100) wr32(slot + off, std::max<s32>(100, v - hp));
  }
  for (const int off : {0x20, 0x22, 0x24}) lower16(slot + off, 5 * n, 20);
  // The attributes as add_attr raises them (battle/result.cpp): the mean gain per level is
  // base + (range - 1) / 2 (rng_range(0) and (1) are 0), summed over n levels, rounded down.
  static const s32 kFloor[6] = {5, 0, 10, 2, 20, 10};
  for (int k = 0; k < 6; ++k) {
    const s32 total = n * (2 * g.base[k] + std::max<int>(g.range[k], 1) - 1) / 2;
    switch (k) {
      case 0: lower16(slot + 0x2A, total, kFloor[k]); break;
      case 1: lower16(slot + 0x42, total, kFloor[k]), lower16(slot + 0x44, total, kFloor[k]); break;
      case 2: lower16(slot + 0x3C, total, kFloor[k]), lower16(slot + 0x3E, total, kFloor[k]); break;
      case 3: lower16(slot + 0x30, total, kFloor[k]), lower16(slot + 0x32, total, kFloor[k]); break;
      case 4: lower16(slot + 0x48, total, kFloor[k]), lower16(slot + 0x4A, total, kFloor[k]); break;
      default: lower16(rec + 6, total, kFloor[k]), lower16(rec + 8, total, kFloor[k]); break;
    }
  }
  battle::LevelUpCtx ctx;
  battle::exp_table_build(ctx.table);
  wr32(slot + 0x10, target >= 2 ? ctx.table[target - 1] : 0);
  wr16(slot + 0x28, target);
  wr16(slot + 0x26, target);
}

void on_member_init(int slot) {
  const Seed* s = seed();
  if (!enabled() || s == nullptr || slot < 0 || slot > 7 || party_base() == nullptr || records_base() == nullptr) return;
  u8* sl = party_base() + slot * 0x60;
  u8* rec = records_base() + slot * 0xD0;
  const int character = rd16s(sl);
  if (character < 1 || character > kCharacterCount) return;
  const int own = rd16s(sl + 0x28);
  if (own < 1) return;  // (no starting data: the unit tests' empty slots)
  int level = own;
  if ((s->open_world || s->hub) && disc_ready()) {
    const int target = join_target_level(party_base(), slot);
    if (target == 0) {
      log(std::string("join level: ") + character_name(character) + " joins alone: their own level " + std::to_string(own));
    } else if (target > own) {
      // Up: the game's level-up routine (battle/result.cpp levelup_apply), from the EXP the
      // target level starts at; the moves the learn table teaches up to it (not skillsanity's).
      battle::LevelUpCtx ctx;
      battle::exp_table_build(ctx.table);
      ctx.config = field::globals().config;
      const s32 hp = rd32s(sl + 0x14);
      wr16(sl + 0x26, own);
      wr32(sl + 0x10, ctx.table[target - 1]);
      battle::levelup_apply(&ctx, sl, rec);
      if (!specials_shuffled()) battle::levelup_learn_skills(&ctx, sl, rec);
      level = rd16s(sl + 0x28);
      log(std::string("join level: ") + character_name(character) + " joins at level " + std::to_string(level) +
          " (the party's median; their own " + std::to_string(own) + "): up, HP " + std::to_string(hp) + " -> " +
          std::to_string(rd32s(sl + 0x14)) + ", SP " + std::to_string(rd16s(rec + 0x1A)) + ", " +
          std::to_string(moves_known(rec)) + " moves known");
    } else if (target < own) {
      const s32 hp = rd32s(sl + 0x14);
      scale_down_stats(sl, rec, target, growth_row(character));
      if (!specials_shuffled()) {
        for (const battle::LearnEntry& e : battle::learn_table(character))
          if (e.level > target && rec[0x3C + e.index] != 0) forget(rec, e.index);
      }
      level = target;
      log(std::string("join level: ") + character_name(character) + " joins at level " + std::to_string(level) +
          " (the party's median; their own " + std::to_string(own) + "): down, HP " + std::to_string(hp) + " -> " +
          std::to_string(rd32s(sl + 0x14)) + ", " + std::to_string(moves_known(rec)) + " moves known");
    } else {
      log(std::string("join level: ") + character_name(character) + " joins at their own level " + std::to_string(own) +
          " (the party's median)");
    }
  }
  if (specials_shuffled()) special_pass(rec, character, level);
}

// ---- Skillsanity -----------------------------------------------------------------------------

void skills_index(const Seed& seed) {
  g_learn.clear();
  for (auto& v : g_guild) v.clear();
  g_float = 0;
  for (const Location& l : seed.locations) {
    if (l.type == "guild" && l.skill_set >= 0 && l.skill_set < 12) g_guild[l.skill_set].push_back(l.id);
    if (l.type != "learn") continue;
    if (l.skill == 46) g_float = l.id;
    else if (l.character >= 1 && l.move_index >= 0) g_learn[move_key(l.character, l.move_index)] = l.id;
  }
}

bool skills_shuffled() {
  const Seed* s = seed();
  return enabled() && s != nullptr && (s->skillsanity == 1 || s->skillsanity == 3);
}

bool specials_shuffled() {
  const Seed* s = seed();
  return enabled() && s != nullptr && (s->skillsanity == 2 || s->skillsanity == 3);
}

bool skill_received(int skill) {
  const std::vector<int>& v = mutable_state().skills;
  return std::find(v.begin(), v.end(), skill) != v.end();
}

bool on_learn_point(int character, int index) {
  if (!specials_shuffled()) return false;
  const int64_t loc = learn_location(character, index);
  if (loc == 0) return false;
  if (!is_checked(loc)) {
    log(std::string("skillsanity: ") + character_name(character) + "'s learn point " + std::to_string(index) + " reached");
    deliver_check(loc);
  }
  return true;
}

int learn_point_state(int character, int index) {
  if (!specials_shuffled()) return -1;
  const int64_t loc = learn_location(character, index);
  if (loc == 0) return -1;
  return is_checked(loc) ? 0 : 1;
}

void on_item_learn_point(int character, int index) {
  if (learn_point_state(character, index) != 1) return;
  log(std::string("skillsanity: ") + character_name(character) + "'s item learn point " + std::to_string(index) + " used");
  deliver_check(learn_location(character, index));
}

bool on_float_prize() {
  if (!skills_shuffled() || g_float == 0) return false;
  log("skillsanity: the Cave of Trials prize (Float) is a check");
  deliver_check(g_float);
  return true;
}

void on_guild_flag(int flag) {
  if (!skills_shuffled() || flag < 700 || flag > 711) return;
  const std::vector<int64_t>& ids = g_guild[flag - 700];
  log("skillsanity: guild set " + std::to_string(flag - 700) + " bought (flag " + std::to_string(flag) + "): " +
      std::to_string(ids.size()) + " checks");
  for (const int64_t id : ids) deliver_check(id);
}

void receive_skill(int skill) {
  std::vector<int>& v = mutable_state().skills;
  if (std::find(v.begin(), v.end(), skill) == v.end()) v.push_back(skill);
  log("skillsanity: skill " + std::to_string(skill) + " may be learned");
}

void receive_move(int character, int index) {
  if (character < 1 || character > kCharacterCount || index < 0 || index > 31) return;
  std::vector<int>& v = mutable_state().moves;
  if (std::find(v.begin(), v.end(), move_key(character, index)) == v.end()) v.push_back(move_key(character, index));
  const int slot = slot_of(character);
  if (slot >= 0 && records_base() != nullptr) {
    know(records_base() + slot * 0xD0, index);
    log(std::string("skillsanity: ") + character_name(character) + " knows move " + std::to_string(index));
  } else {
    log(std::string("skillsanity: ") + character_name(character) + "'s move " + std::to_string(index) + " waits for their join");
  }
}

void skills_new_game() {
  if (!specials_shuffled() || party_base() == nullptr || records_base() == nullptr) return;
  for (int i = 0; i < 8; ++i) {
    const s16 id = rd16s(party_base() + i * 0x60);
    if (id < 1 || id > kCharacterCount) continue;
    special_pass(records_base() + i * 0xD0, id, rd16s(party_base() + i * 0x60 + 0x28));
  }
}

bool shop_ap_item(int shop, int index, int item, std::string* name, s32* price) {
  const Seed* s = seed();
  if (!enabled() || s == nullptr || !shop_slot_open(shop, index, item)) return false;
  for (const Location& l : s->locations) {
    if (l.type != "shop" || l.shop != shop || l.index != index) continue;
    const ItemDef& it = l.item.item;
    if (l.price < 0 || !l.item.own || (it.kind != "skill" && it.kind != "move")) return false;
    std::string n = it.name;
    if (n.rfind("Skill: ", 0) == 0) n = n.substr(7);
    if (name != nullptr) *name = n;
    if (price != nullptr) *price = l.price;
    return true;
  }
  return false;
}

int first_move(int character) {
  if (!disc_ready()) return -1;
  const std::vector<battle::LearnEntry> table = battle::learn_table(character);
  return table.empty() ? -1 : table.front().index;
}

}  // namespace ss::game::ap
