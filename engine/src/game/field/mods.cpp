#include "game/field/mods.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <span>
#include <sstream>
#include <string>

#include "game/ap/ap.h"
#include "runtime/runtime.h"

namespace ss::game::field {
namespace {

enum class Kind { Script, Item, Shop, Enemy };

struct Patch {
  Kind kind = Kind::Script;
  int target = 0;    // map, item, shop or enemy type
  u32 where = 0;     // script word, record byte offset, or stock index
  int size = 4;      // bytes compared and written (records)
  u32 expected = 0;
  u32 value = 0;
  std::string at;    // file:line, for the log
};

constexpr size_t kItemRecord = 0x30;
constexpr size_t kEnemyRecord = 0x5C;

bool parse_number(const std::string& s, u32* out) {
  if (s.empty()) return false;
  char* end = nullptr;
  const long long v = std::strtoll(s.c_str(), &end, 0);
  if (end == nullptr || *end != 0) return false;
  *out = static_cast<u32>(v);
  return true;
}

struct FieldName {
  const char* name;
  u32 offset;
  int size;
};
constexpr FieldName kItemFields[] = {{"price", 0x00, 4}, {"atk", 0x08, 2},    {"def", 0x10, 2},  {"amount", 0x19, 1},
                                     {"effect", 0x28, 1}, {"type", 0x2A, 1}, {"sell", 0x2D, 1}};
constexpr FieldName kEnemyFields[] = {{"hp", 0x00, 4},     {"mp", 0x04, 4},         {"exp", 0x08, 4},
                                      {"fol", 0x0C, 4},    {"speed", 0x16, 2},      {"drop1", 0x18, 2},
                                      {"drop2", 0x1A, 2},  {"atk", 0x1C, 2},        {"def", 0x1E, 2},
                                      {"int", 0x20, 2},    {"level", 0x24, 2},      {"eva", 0x26, 2},
                                      {"acc", 0x28, 2},    {"guts", 0x36, 1},       {"drop1_odds", 0x37, 1},
                                      {"drop2_odds", 0x38, 1}};

// A field name, "OFFSET" or "OFFSET:SIZE"; the default size by record layout.
bool parse_field(const std::string& s, Kind kind, u32* offset, int* size) {
  for (const FieldName& f : kind == Kind::Item ? std::span<const FieldName>(kItemFields)
                                                : std::span<const FieldName>(kEnemyFields)) {
    if (s == f.name) {
      *offset = f.offset;
      *size = f.size;
      return true;
    }
  }
  const auto colon = s.find(':');
  if (!parse_number(s.substr(0, colon), offset)) return false;
  if (colon != std::string::npos) {
    u32 n = 0;
    if (!parse_number(s.substr(colon + 1), &n) || (n != 1 && n != 2 && n != 4)) return false;
    *size = static_cast<int>(n);
  } else if (kind == Kind::Item) {
    *size = *offset == 0 ? 4 : (*offset >= 4 && *offset < 0x14) ? 2 : 1;
  } else {
    *size = *offset < 0x10 ? 4 : ((*offset < 0x2A) || *offset == 0x3A) ? 2 : 1;
  }
  const size_t limit = kind == Kind::Item ? kItemRecord : kEnemyRecord;
  return *offset + static_cast<u32>(*size) <= limit;
}

u32 mask(int size) { return size == 4 ? 0xFFFFFFFFu : (1u << (size * 8)) - 1; }
u32 read_le(const u8* p, int size) {
  u32 v = 0;
  for (int i = 0; i < size; ++i) v |= static_cast<u32>(p[i]) << (8 * i);
  return v;
}
void write_le(u8* p, int size, u32 v) {
  for (int i = 0; i < size; ++i) p[i] = static_cast<u8>(v >> (8 * i));
}

// One patch line (comment already stripped); false when it is not understood.
bool parse_line(const std::string& line, const std::string& at, Patch* out) {
  std::istringstream fields(line);
  std::string kind, a, b, c, d, extra;
  if (!(fields >> kind)) return false;
  Patch p;
  p.at = at;
  u32 target = 0;
  bool ok = static_cast<bool>(fields >> a >> b >> c >> d) && !(fields >> extra) && parse_number(a, &target) &&
            parse_number(c, &p.expected) && parse_number(d, &p.value);
  if (ok) {
    if (kind == "script") {
      p.kind = Kind::Script;
      ok = parse_number(b, &p.where);
    } else if (kind == "item") {
      p.kind = Kind::Item;
      ok = parse_field(b, p.kind, &p.where, &p.size) && target >= 1 && target < 0x400;
    } else if (kind == "shop") {
      p.kind = Kind::Shop;
      ok = parse_number(b, &p.where) && p.where < 0x100;
    } else if (kind == "enemy") {
      p.kind = Kind::Enemy;
      ok = parse_field(b, p.kind, &p.where, &p.size);
    } else {
      ok = false;
    }
  }
  if (!ok) return false;
  p.target = static_cast<int>(target);
  if (p.kind == Kind::Item || p.kind == Kind::Enemy) {
    p.expected &= mask(p.size);
    p.value &= mask(p.size);
  }
  *out = p;
  return true;
}

const std::vector<Patch>& patches() {
  static std::vector<Patch> list;
  static bool loaded = false;
  if (loaded) return list;
  loaded = true;
  auto add = [&](std::string line, const std::string& at) {
    if (const auto hash = line.find('#'); hash != std::string::npos) line.resize(hash);
    if (line.find_first_not_of(" \t\r") == std::string::npos) return;
    Patch p;
    if (!parse_line(line, at, &p)) {
      std::fprintf(stderr, "[mods] %s: not understood: %s\n", at.c_str(), line.c_str());
      return;
    }
    list.push_back(p);
  };
  // An Archipelago seed's edits (game/ap: --ap-seed / config ap_seed) come first.
  const std::vector<std::string>& seed_lines = ap::seed_patch_lines();
  for (size_t i = 0; i < seed_lines.size(); ++i) add(seed_lines[i], "seed:" + std::to_string(i + 1));
  auto& rt = runtime::Runtime::get();
  std::string dir = rt.mods_dir;
  if (const char* env = std::getenv("SS_MODS_DIR"); env && *env) dir = env;
  std::error_code ec;
  const bool dir_ok = !dir.empty() && std::filesystem::is_directory(dir, ec);
  if (dir_ok) {
    std::vector<std::filesystem::path> files;
    for (const auto& e : std::filesystem::directory_iterator(dir, ec))
      if (e.is_regular_file() && e.path().extension() == ".patch") files.push_back(e.path());
    std::sort(files.begin(), files.end());
    for (const auto& path : files) {
      std::ifstream in(path);
      int line_no = 0;
      for (std::string line; std::getline(in, line);) {
        ++line_no;
        add(line, path.filename().string() + ":" + std::to_string(line_no));
      }
    }
  }
  if (!dir_ok && seed_lines.empty()) return list;
  const ModsSummary s = mods_summary();
  std::string from = dir_ok ? dir : "";
  if (!seed_lines.empty()) from = dir_ok ? "the seed and " + dir : "the seed";
  std::fprintf(stderr, "[mods] %d script, %d item, %d shop, %d enemy patch(es) from %s\n", s.script, s.item, s.shop,
               s.enemy, from.c_str());
  return list;
}

void note_skip(const Patch& p, const std::string& what, u32 found) {
  char msg[200];
  std::snprintf(msg, sizeof msg, "[mods] %s: %s holds %#x, not %#x; skipped", p.at.c_str(), what.c_str(), found,
                p.expected);
  runtime::Runtime::get().log_once("mods:" + p.at, msg);
}

void note_applied(const Patch& p, const std::string& what) {
  char msg[200];
  std::snprintf(msg, sizeof msg, "[mods] %s: %s %#x -> %#x", p.at.c_str(), what.c_str(), p.expected, p.value);
  runtime::Runtime::get().log_once("mods+" + p.at, msg);
}

std::string describe(const char* kind, int target, u32 where) {
  char buf[64];
  std::snprintf(buf, sizeof buf, "%s %d +%#x", kind, target, where);
  return buf;
}

}  // namespace

ModsSummary mods_summary() {
  ModsSummary s;
  for (const Patch& p : patches()) {
    switch (p.kind) {
      case Kind::Script: ++s.script; break;
      case Kind::Item: ++s.item; break;
      case Kind::Shop: ++s.shop; break;
      case Kind::Enemy: ++s.enemy; break;
    }
  }
  return s;
}

int mods_apply_script(int map, std::vector<u8>& script) {
  int changed = 0;
  for (const Patch& p : patches()) {
    if (p.kind != Kind::Script || p.target != map) continue;
    const size_t at = 0x1C + static_cast<size_t>(p.where) * 4;
    if (at + 4 > script.size()) {
      runtime::Runtime::get().log_once("mods:" + p.at, "[mods] " + p.at + ": word past the end of map " +
                                                            std::to_string(map) + "'s script; skipped");
      continue;
    }
    u32 cur;
    std::memcpy(&cur, script.data() + at, 4);
    if (cur != p.expected) {
      note_skip(p, "map " + std::to_string(map) + " word " + std::to_string(p.where), cur);
      continue;
    }
    std::memcpy(script.data() + at, &p.value, 4);
    ++changed;
  }
  if (changed != 0) {
    char msg[64];
    std::snprintf(msg, sizeof msg, "mods: %d script word(s) patched on map %d", changed, map);
    runtime::Runtime::get().trace(msg);
  }
  return changed;
}

int mods_apply_items(u8* records, size_t bytes) {
  int changed = 0;
  for (const Patch& p : patches()) {
    if (p.kind != Kind::Item) continue;
    const size_t at = static_cast<size_t>(p.target - 1) * kItemRecord + p.where;
    const std::string what = describe("item", p.target, p.where);
    if (records == nullptr || at + static_cast<size_t>(p.size) > bytes) continue;  // a shorter table
    const u32 cur = read_le(records + at, p.size);
    if (cur == p.value && cur != p.expected) continue;  // applied already
    if (cur != p.expected) {
      note_skip(p, what, cur);
      continue;
    }
    write_le(records + at, p.size, p.value);
    note_applied(p, what);
    ++changed;
  }
  return changed;
}

std::vector<u8> mods_shop_stock(int shop, const u8* list) {
  std::vector<u16> items;
  for (const u8* q = list; q != nullptr; q += 2) {
    const u16 v = static_cast<u16>(q[0] | q[1] << 8);
    if (v == 0) break;
    items.push_back(v);
  }
  bool changed = false;
  std::vector<bool> removed(items.size() + 0x100, false);
  const size_t original = items.size();
  for (const Patch& p : patches()) {
    if (p.kind != Kind::Shop || p.target != shop) continue;
    const std::string what = describe("shop", shop, p.where);
    if (p.where == original && p.expected == 0) {
      items.push_back(static_cast<u16>(p.value));
    } else if (p.where < original && items[p.where] == p.expected) {
      if (p.value == 0) {
        removed[p.where] = true;
      } else {
        items[p.where] = static_cast<u16>(p.value);
      }
    } else {
      note_skip(p, what, p.where < original ? items[p.where] : 0);
      continue;
    }
    note_applied(p, what);
    changed = true;
  }
  std::vector<u8> out;
  for (size_t i = 0; i < items.size(); ++i) {
    if (i < original && removed[i]) continue;
    out.push_back(static_cast<u8>(items[i]));
    out.push_back(static_cast<u8>(items[i] >> 8));
  }
  out.push_back(0);
  out.push_back(0);
  if (changed) runtime::Runtime::get().trace(("mods: shop " + std::to_string(shop) + " stock patched").c_str());
  return out;
}

const u8* mods_enemy_stats(int type, const u8* record) {
  static u8 copy[kEnemyRecord];
  bool any = false;
  for (const Patch& p : patches()) {
    if (p.kind != Kind::Enemy || p.target != type) continue;
    if (!any) {
      std::memcpy(copy, record, kEnemyRecord);
      any = true;
    }
    const std::string what = describe("enemy", type, p.where);
    const u32 cur = read_le(copy + p.where, p.size);
    if (cur != p.expected) {
      note_skip(p, what, cur);
      continue;
    }
    write_le(copy + p.where, p.size, p.value);
    note_applied(p, what);
  }
  return any ? copy : record;
}

}  // namespace ss::game::field
