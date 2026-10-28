#include "game/field/mods.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

#include "game/ap/ap.h"
#include "mods/catalog.h"
#include "runtime/region.h"
#include "runtime/runtime.h"
#include "util/log.h"
#include "util/utf8.h"

namespace ss::game::field {
namespace {

using ss::mods::Kind;
using ss::mods::kEnemyRecord;
using ss::mods::kItemRecord;

struct Patch : ss::mods::PatchLine {
  std::string at;             // file:line (loose), id/file:line (package), seed:N, for the log
  int source = -1;            // the catalog entry; -1 for the seed
  int index = 0;              // in the list
  std::string overridden_by;  // a later mod's line replaces this one (docs/port/notes/mods.md section 4)
  std::string same_as;        // an earlier line makes the same change
  std::vector<int> same_place;  // the other lines at this place, in order
};

u32 read_le(const u8* p, int size) {
  u32 v = 0;
  for (int i = 0; i < size; ++i) v |= static_cast<u32>(p[i]) << (8 * i);
  return v;
}
void write_le(u8* p, int size, u32 v) {
  for (int i = 0; i < size; ++i) p[i] = static_cast<u8>(v >> (8 * i));
}

std::filesystem::path utf8_path(const std::string& s) { return util::utf8_path(s); }

// The game's log of the load order (only when there are packages: with loose patches
// alone the log is as it always was).
void log_order(const ss::mods::Catalog& catalog, const std::vector<ss::mods::Slot>& order) {
  std::string text;
  for (const ss::mods::Slot& s : order) {
    const ss::mods::Entry& e = catalog.entries[static_cast<size_t>(s.entry)];
    if (!s.enabled) continue;
    if (!text.empty()) text += ", ";
    text += e.loose ? std::string(ss::mods::kLooseId) + " " + std::to_string(e.files.size()) + " file(s)"
                    : e.id + " " + e.manifest.version;
  }
  util::log_printf("[mods] load order: %s\n", text.empty() ? "(nothing enabled)" : text.c_str());
  for (const ss::mods::Slot& s : order) {
    const ss::mods::Entry& e = catalog.entries[static_cast<size_t>(s.entry)];
    if (e.loose) continue;
    if (!s.enabled)
      util::log_printf("[mods] %s %s is installed but not enabled (setup's Mods page enables it)\n", e.id.c_str(),
                   e.manifest.version.c_str());
    else if (!e.usable())
      util::log_printf("[mods] %s: %s; not loaded\n", e.id.c_str(), e.error.c_str());
  }
}

void log_conflicts(const std::vector<ss::mods::Line>& lines, const ss::mods::Analysis& an) {
  using ss::mods::Relation;
  for (const ss::mods::Conflict& c : an.conflicts) {
    const ss::mods::Line& a = lines[c.first];
    const ss::mods::Line& b = lines[c.second];
    const std::string place = ss::mods::describe_place(a.patch);
    switch (c.relation) {
      case Relation::kOverride:
        util::log_printf("[mods] conflict: %s: %s is overridden by %s (later in the load order)\n", place.c_str(),
                     a.label.c_str(), b.label.c_str());
        break;
      case Relation::kMismatch:
        util::log_printf("[mods] conflict: %s: %s and %s expect different values (%#x, %#x); at most one applies\n",
                     place.c_str(), a.label.c_str(), b.label.c_str(), a.patch.expected, b.patch.expected);
        break;
      case Relation::kOverlap:
        util::log_printf("[mods] conflict: %s (%s) and %s (%s) overlap; the first applies first\n",
                     a.label.c_str(), place.c_str(), b.label.c_str(), ss::mods::describe_place(b.patch).c_str());
        break;
      case Relation::kSeedWins:
        util::log_printf("[mods] conflict: %s: the Archipelago seed changes it too (%s); %s is skipped\n",
                     place.c_str(), a.label.c_str(), b.label.c_str());
        break;
      default: break;  // the same change, a line building on another, two lines of one mod: no news
    }
  }
}

const std::vector<Patch>& patches() {
  static std::vector<Patch> list;
  static bool loaded = false;
  if (loaded) return list;
  loaded = true;
  std::vector<ss::mods::Line> lines;
  std::vector<ss::mods::BadLine> bad;
  // An Archipelago seed's edits (game/ap: --ap-seed / config ap_seed) come first.
  const std::vector<std::string>& seed_lines = ap::seed_patch_lines();
  for (size_t i = 0; i < seed_lines.size(); ++i)
    ss::mods::add_line(seed_lines[i], "seed:" + std::to_string(i + 1), -1, &lines, &bad);
  // Then the mods folder's enabled entries in load order (mods/catalog.h): with no
  // packages, the loose *.patch files in name order, as always.
  auto& rt = runtime::Runtime::get();
  std::string dir = rt.mods_dir;
  if (util::env_set("SS_MODS_DIR")) dir = *util::env("SS_MODS_DIR");
  std::string order_text = rt.mod_order;
  if (const auto env = util::env("SS_MOD_ORDER")) order_text = *env;
  const ss::mods::Catalog catalog = ss::mods::scan(dir.empty() ? std::filesystem::path() : utf8_path(dir), SS_VERSION);
  const bool dir_ok = catalog.folder_exists;
  const std::vector<ss::mods::Slot> order = ss::mods::resolve_order(catalog, order_text);
  if (catalog.has_packages()) log_order(catalog, order);
  ss::mods::read_lines(catalog, order, &lines, &bad);
  for (const ss::mods::BadLine& b : bad)
    util::log_printf("[mods] %s: not understood: %s\n", b.label.c_str(), b.text.c_str());
  const ss::mods::Analysis an = ss::mods::analyse(lines);
  list.reserve(lines.size());
  for (size_t i = 0; i < lines.size(); ++i) {
    Patch p;
    static_cast<ss::mods::PatchLine&>(p) = lines[i].patch;
    p.at = lines[i].label;
    p.source = lines[i].source;
    p.index = static_cast<int>(i);
    if (an.overridden_by[i] >= 0) p.overridden_by = lines[static_cast<size_t>(an.overridden_by[i])].label;
    if (an.redundant_with[i] >= 0) p.same_as = lines[static_cast<size_t>(an.redundant_with[i])].label;
    p.same_place = an.same_place[i];
    list.push_back(std::move(p));
  }
  log_conflicts(lines, an);
  if (!dir_ok && seed_lines.empty()) return list;
  const ModsSummary s = mods_summary();
  std::string from = dir_ok ? dir : "";
  if (!seed_lines.empty()) from = dir_ok ? "the seed and " + dir : "the seed";
  util::log_printf("[mods] %d script, %d item, %d shop, %d enemy patch(es) from %s\n", s.script, s.item, s.shop,
               s.enemy, from.c_str());
  return list;
}

// A line another one replaces (a later mod's change) or makes unnecessary (the same
// change earlier): logged once, never applied.
bool resolved_elsewhere(const Patch& p) {
  if (!p.overridden_by.empty()) {
    runtime::Runtime::get().log_once("mods:" + p.at, "[mods] " + p.at + ": overridden by " + p.overridden_by +
                                                         " (later in the load order); not applied");
    return true;
  }
  if (!p.same_as.empty()) {
    runtime::Runtime::get().log_once("mods:" + p.at, "[mods] " + p.at + ": the same change as " + p.same_as +
                                                         "; nothing to do");
    return true;
  }
  return false;
}

// A later line at the same place leaves 'found' there (a chain of mods): applied already.
bool later_holds(const Patch& p, u32 found) {
  const std::vector<Patch>& all = patches();
  for (int j : p.same_place)
    if (j > p.index && all[static_cast<size_t>(j)].value == found) return true;
  return false;
}

void note_skip(const Patch& p, const std::string& what, u32 found) {
  // Name the earlier line that put the value there, when one did (a conflict between mods).
  std::string why;
  const std::vector<Patch>& all = patches();
  for (int j : p.same_place) {
    const Patch& q = all[static_cast<size_t>(j)];
    if (j < p.index && q.value == found && q.overridden_by.empty() && q.same_as.empty())
      why = ": set by " + q.at + " earlier in the load order";
  }
  char msg[200];
  std::snprintf(msg, sizeof msg, "[mods] %s: %s holds %#x, not %#x", p.at.c_str(), what.c_str(), found, p.expected);
  runtime::Runtime::get().log_once("mods:" + p.at, msg + why + "; skipped");
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
      case Kind::kScript: ++s.script; break;
      case Kind::kItem: ++s.item; break;
      case Kind::kShop: ++s.shop; break;
      case Kind::kEnemy: ++s.enemy; break;
    }
  }
  return s;
}

int mods_apply_script(int map, std::vector<u8>& script) {
  int changed = 0;
  for (const Patch& p : patches()) {
    if (p.kind != Kind::kScript || p.target != map) continue;
    if (resolved_elsewhere(p)) continue;
    // WORD is NA's; a release that laid this map's script out otherwise has it elsewhere
    // (runtime/region.h script_offset), or not at all.
    const s64 code_at = region::script_offset(map, static_cast<u32>(p.where) * 4);
    if (code_at < 0) {
      runtime::Runtime::get().log_once("mods:" + p.at, "[mods] " + p.at + ": map " + std::to_string(map) +
                                                            "'s script has no such word in this release; skipped");
      continue;
    }
    const size_t at = 0x1C + static_cast<size_t>(code_at);
    if (at + 4 > script.size()) {
      runtime::Runtime::get().log_once("mods:" + p.at, "[mods] " + p.at + ": word past the end of map " +
                                                            std::to_string(map) + "'s script; skipped");
      continue;
    }
    u32 cur;
    std::memcpy(&cur, script.data() + at, 4);
    const std::string what = "map " + std::to_string(map) + " word " + std::to_string(p.where);
    if (cur != p.expected) {
      note_skip(p, what, cur);
      continue;
    }
    std::memcpy(script.data() + at, &p.value, 4);
    if (p.source >= 0) note_applied(p, what);  // mods' lines (a seed's are too many to list)
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
    if (p.kind != Kind::kItem) continue;
    if (resolved_elsewhere(p)) continue;
    const size_t at = static_cast<size_t>(p.target - 1) * kItemRecord + p.where;
    const std::string what = describe("item", p.target, p.where);
    if (records == nullptr || at + static_cast<size_t>(p.size) > bytes) continue;  // a shorter table
    const u32 cur = read_le(records + at, p.size);
    if (cur == p.value && cur != p.expected) continue;  // applied already
    if (cur != p.expected && later_holds(p, cur)) continue;  // applied already, and a later mod's line after it
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
    if (p.kind != Kind::kShop || p.target != shop) continue;
    if (resolved_elsewhere(p)) continue;
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
    if (p.kind != Kind::kEnemy || p.target != type) continue;
    if (!any) {
      std::memcpy(copy, record, kEnemyRecord);
      any = true;
    }
    if (resolved_elsewhere(p)) continue;
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
