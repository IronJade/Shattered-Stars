#include "runtime/region.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <mutex>
#include <random>
#include <set>
#include <string>
#include <utility>


namespace ss::region {

// The generated tables (tools/region_map.py): NA's module identities, then one namespace of
// address maps per region.
namespace {
struct NaModule {
  ImageKind kind;
  u64 fnv;
  u64 hash;
  u32 size;
};
#include "../../data/regions/na_modules.inc"
}  // namespace
namespace eu {
#include "../../data/regions/eu.inc"
}  // namespace eu
namespace fr {
#include "../../data/regions/fr.inc"
}  // namespace fr
namespace de {
#include "../../data/regions/de.inc"
}  // namespace de

namespace {

constexpr Profile kProfiles[] = {
    {Id::kNorthAmerica, "na", "North America", "North American", {"SCUS_944.21", "SCUS_944.22"}, "BASCUS-94421",
     Text::kNorthAmerica, 60, true},
    {Id::kEurope, "eu", "Europe", "European", {"SCES_021.59", "SCES_121.59"}, "BASCES-02159", Text::kNorthAmerica, 50, true},
    {Id::kFrance, "fr", "France", "French", {"SCES_021.60", "SCES_121.60"}, "BESCES-02160", Text::kFrench, 50, false},
    {Id::kGermany, "de", "Germany", "German", {"SCES_021.61", "SCES_121.61"}, "BESCES-02161", Text::kGerman, 50, false},
    {Id::kJapan, "jp", "Japan", "Japanese", {"SLPM_861.05", "SLPM_861.06"}, "BISLPM-86105", Text::kJapanese, 60, false},
};

Id g_current = Id::kNorthAmerica;

struct Maps {
  const ImageMap* images = nullptr;
  size_t count = 0;
  const ScriptMap* scripts = nullptr;
  size_t nscripts = 0;
};

Maps maps_of(Id id) {
  switch (id) {
    case Id::kEurope: return {eu::kImages, std::size(eu::kImages), eu::kScripts, std::size(eu::kScripts)};
    case Id::kFrance: return {fr::kImages, std::size(fr::kImages), fr::kScripts, std::size(fr::kScripts)};
    case Id::kGermany: return {de::kImages, std::size(de::kImages), de::kScripts, std::size(de::kScripts)};
    default: return {};
  }
}

const ScriptMap* script_map(int map) {
  const Maps m = maps_of(g_current);
  for (size_t i = 0; i < m.nscripts; ++i)
    if (m.scripts[i].map == map) return &m.scripts[i];
  return nullptr;
}

// "[port]" lines once per key, as runtime::Runtime::log_once (which this layer can't link:
// ss_core's tests have no window).
void log_once(const std::string& key, const std::string& message) {
  static std::mutex lock;
  static std::set<std::string> logged;
  std::lock_guard<std::mutex> guard(lock);
  if (logged.insert(key).second) std::fprintf(stderr, "[port] %s\n", message.c_str());
}

// A read of an NA address the region's image has no counterpart for. The first few per
// image are named; then one line says the rest are not, so a reader stuck in a loop over
// unmapped data can't grow the log (or this table) without bound.
void log_miss(const char* key, u32 address) {
  constexpr size_t kNamed = 8;
  struct Misses {
    std::set<u32> named;
    bool more = false;
  };
  static std::mutex lock;
  static std::map<std::string, Misses> misses;
  std::lock_guard<std::mutex> guard(lock);
  Misses& m = misses[key];
  if (m.more || m.named.count(address) != 0) return;
  if (m.named.size() < kNamed) {
    m.named.insert(address);
    std::fprintf(stderr, "[port] region: NA address %08x of image %s has no counterpart\n", address, key);
  } else {
    m.more = true;
    std::fprintf(stderr, "[port] region: more NA addresses of image %s have no counterpart (not listed)\n", key);
  }
}

bool iequal(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i)
    if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
  return true;
}

// ---- SS_REGION_READ_LOG ----

struct ReadLog {
  std::mutex lock;
  std::set<std::pair<std::string, u32>> seen;
  std::string path;
  bool on = false;
  bool full = false;
  ReadLog() {
    if (const char* p = std::getenv("SS_REGION_READ_LOG"); p != nullptr && *p != 0) {
      path = p;
      on = true;
    }
  }
  ~ReadLog() {
    if (!on || seen.empty()) return;
    // A folder: one file per process (sweeps run several at once).
    std::string file = path;
    if (!file.empty() && (file.back() == '/' || file.back() == '\\'))
      file += "reads." + std::to_string(std::random_device{}()) + ".txt";
    if (std::FILE* f = std::fopen(file.c_str(), "a")) {
      for (const auto& [key, address] : seen) std::fprintf(f, "%s %08x\n", key.c_str(), address);
      std::fclose(f);
    }
  }
};
ReadLog& read_log() {
  static ReadLog log;
  return log;
}

}  // namespace

std::span<const Profile> profiles() { return kProfiles; }

bool playable(const Profile& p) {
  if (p.supported) return true;
  const char* t = std::getenv("SS_REGION_TRY");
  if (t == nullptr) return false;
  const std::string list = std::string(",") + t + ",";
  return list.find(std::string(",") + p.key + ",") != std::string::npos;
}

const Profile& profile(Id id) {
  for (const Profile& p : kProfiles)
    if (p.id == id) return p;
  return kProfiles[0];
}

const Profile* from_serial(std::string_view game_id, int* disc) {
  for (const Profile& p : kProfiles) {
    for (int n = 0; n < 2; ++n) {
      if (iequal(game_id, p.serial[n])) {
        if (disc != nullptr) *disc = n + 1;
        return &p;
      }
    }
  }
  if (disc != nullptr) *disc = 0;
  return nullptr;
}

const Profile& current() { return profile(g_current); }
void select(Id id) { g_current = id; }

bool has_maps() { return maps_of(g_current).count != 0; }

const ImageMap* entry_map(int entry, int block) {
  const Maps m = maps_of(g_current);
  for (size_t i = 0; i < m.count; ++i) {
    const ImageMap& im = m.images[i];
    if (im.kind == ImageKind::kEntry && im.entry == entry && im.block == block) return &im;
  }
  return nullptr;
}

const ImageMap* boot_map() {
  const Maps m = maps_of(g_current);
  for (size_t i = 0; i < m.count; ++i)
    if (m.images[i].kind == ImageKind::kBoot) return &m.images[i];
  return nullptr;
}

const ImageMap* module_map(ImageKind kind, std::span<const u8> block) {
  const Maps m = maps_of(g_current);
  if (m.count == 0) return nullptr;
  const u64 exact = fnv1a64(block);
  for (size_t i = 0; i < m.count; ++i)
    if (m.images[i].kind == kind && m.images[i].rg_fnv == exact && m.images[i].rg_size == block.size())
      return &m.images[i];
  const u64 h = module_hash(block);
  for (size_t i = 0; i < m.count; ++i)
    if (m.images[i].kind == kind && m.images[i].rg_module_hash == h && m.images[i].rg_size == block.size())
      return &m.images[i];
  return nullptr;
}

u64 module_identity(ImageKind kind, std::span<const u8> block, const ImageMap** map) {
  const ImageMap* m = is_na() ? nullptr : module_map(kind, block);
  if (map != nullptr) *map = m;
  const u64 h = module_hash(block);
  for (const NaModule& n : kNaModules)
    if (n.kind == kind && n.hash == h) return n.fnv;
  return m != nullptr ? m->na_fnv : 0;
}

s64 translate(const ImageMap* map, u32 na_off) {
  if (map == nullptr) return na_off;
  if (na_off == map->na_size) return map->rg_size;  // one past the end
  // The segments are sorted by NA offset.
  const Segment* begin = map->seg;
  const Segment* end = map->seg + map->nseg;
  const Segment* s = std::upper_bound(begin, end, na_off, [](u32 off, const Segment& seg) { return off < seg.na_off; });
  if (s == begin) return -1;
  --s;
  if (na_off - s->na_off >= s->size) return -1;
  return static_cast<s64>(na_off) + s->delta;
}

u32 to_na_address(const ImageMap* map, u32 region_address) {
  if (map == nullptr) return region_address;
  const u32 roff = region_address - map->rg_base;
  if (roff == map->rg_size) return map->na_base + map->na_size;
  if (roff > map->rg_size) return 0;
  // Region offsets rise with the NA ones (the alignment keeps the order).
  const Segment* begin = map->seg;
  const Segment* end = map->seg + map->nseg;
  const Segment* s = std::upper_bound(begin, end, roff, [](u32 off, const Segment& seg) {
    return static_cast<s64>(off) < static_cast<s64>(seg.na_off) + seg.delta;
  });
  if (s == begin) return 0;
  --s;
  const s64 start = static_cast<s64>(s->na_off) + s->delta;
  if (static_cast<s64>(roff) - start >= s->size) return 0;
  return map->na_base + static_cast<u32>(static_cast<s64>(roff) - s->delta);
}

bool prepare(const ImageMap* map, std::vector<u8>& image) {
  if (map == nullptr) return true;
  if (image.size() != map->rg_size || fnv1a64(image) != map->rg_fnv) {
    log_once(
        "region-image-" + std::to_string(map->entry) + "-" + std::to_string(map->block),
        std::string("region: the ") + current().adjective + " disc's image of entry " + std::to_string(map->entry) +
            " is not the one the address map was made from (another revision of the disc?); its tables can't be read");
    image.clear();
    return false;
  }
  const Maps m = maps_of(g_current);
  for (u32 i = 0; i < map->nptr; ++i) {
    const Pointers& p = map->ptr[i];
    const ImageMap* target = p.target < m.count ? &m.images[p.target] : nullptr;
    for (u32 k = 0; k < p.count; ++k) {
      const s64 at = translate(map, p.na_off + 4 * k);
      if (at < 0 || static_cast<size_t>(at) + 4 > image.size()) continue;
      u8* w = image.data() + at;
      const u32 na = to_na_address(target, read_u32(w));
      if (na == 0) continue;
      w[0] = static_cast<u8>(na);
      w[1] = static_cast<u8>(na >> 8);
      w[2] = static_cast<u8>(na >> 16);
      w[3] = static_cast<u8>(na >> 24);
    }
  }
  return true;
}

const u8* image_byte(const ImageMap* map, const std::vector<u8>& image, u32 na_base, u32 address, const char* log_key) {
  if (read_log().on && log_key != nullptr) log_read(log_key, address);
  const u32 na_off = address - na_base;
  if (map == nullptr) return na_off < image.size() ? image.data() + na_off : nullptr;
  const s64 at = translate(map, na_off);
  if (at < 0 || static_cast<size_t>(at) >= image.size()) {
    if (na_off < map->na_size) log_miss(log_key != nullptr ? log_key : "?", address);
    return nullptr;
  }
  return image.data() + at;
}

std::string entry_key(int entry, int block) {
  switch (entry) {
    case -1: return "boot";
    case 1: return "title";
    case 3100: return "field";
    case 2129: return "battle";
    case 3107: return "credits";
    case 1981: return "7bd";
    case 1984: return "7c0." + std::to_string(block);
    default: break;
  }
  char buf[16];
  std::snprintf(buf, sizeof buf, "%x", entry);
  return buf;
}

u64 fnv1a64(std::span<const u8> bytes) {
  u64 h = 0xcbf29ce484222325ull;
  for (u8 x : bytes) {
    h ^= x;
    h *= 0x100000001b3ull;
  }
  return h;
}

namespace {

// tools/region_map.py normalize(): mask what relinking changes.
bool is_loadstore(u32 op) { return (op >= 0x20 && op <= 0x26) || (op >= 0x28 && op <= 0x2E) || op == 0x32 || op == 0x3A; }

std::vector<u32> normalize_words(const std::vector<u32>& ws) {
  u32 lui = 0;  // registers holding a lui-based address (bit per register)
  auto has = [&](u32 r) { return (lui >> r) & 1u; };
  auto add = [&](u32 r) { lui |= 1u << r; };
  auto drop = [&](u32 r) { lui &= ~(1u << r); };
  std::vector<u32> out;
  out.reserve(ws.size());
  for (u32 w : ws) {
    if (w >= 0x80010000u && w < 0x80200000u) {
      out.push_back(0x80000000u);
      continue;
    }
    const u32 op = w >> 26, rs = (w >> 21) & 31, rt = (w >> 16) & 31, rd = (w >> 11) & 31;
    if (op == 2 || op == 3) {
      out.push_back(w & 0xFC000000u);
      continue;
    }
    if (op == 0xF) {
      out.push_back(w & 0xFFFF0000u);
      add(rt);
      continue;
    }
    if ((op == 0x09 || op == 0x0D) && (has(rs) || rs == 28)) {
      out.push_back(w & 0xFFFF0000u);
      add(rt);
      continue;
    }
    if (is_loadstore(op) && (has(rs) || rs == 28)) {
      out.push_back(w & 0xFFFF0000u);
      if (op < 0x28) drop(rt);
      continue;
    }
    if (op == 0 && ((w & 0x3F) == 0x21 || (w & 0x3F) == 0x20) && (has(rs) || has(rt))) {
      out.push_back(w);
      add(rd);
      continue;
    }
    out.push_back(w);
    if (op == 0) drop(rd);
    else if (is_loadstore(op) && op < 0x28) drop(rt);
    else if (op >= 0x08 && op <= 0x0F) drop(rt);
    else if (op == 3) drop(31);
  }
  return out;
}

}  // namespace

u64 module_hash(std::span<const u8> block) {
  std::vector<u32> ws(block.size() / 4);
  for (size_t i = 0; i < ws.size(); ++i) ws[i] = read_u32(block.data() + 4 * i);
  // Set the division traps aside (tools/region_map.py trap_kept).
  std::vector<u32> kept;
  kept.reserve(ws.size());
  for (size_t i = 0; i < ws.size();) {
    const u32 w = ws[i];
    if ((w & 0xFC1FFFFFu) == 0x14000002u && i + 2 < ws.size() && ws[i + 2] == 0x0007000Du) {
      if (i + 8 < ws.size() && ws[i + 8] == 0x0006000Du && ws[i + 3] == 0x2401FFFFu) {
        i += 9;
        continue;
      }
      i += 3;
      continue;
    }
    kept.push_back(w);
    ++i;
  }
  const std::vector<u32> n = normalize_words(kept);
  u64 h = 0xcbf29ce484222325ull;
  for (u32 w : n) {
    for (int b = 0; b < 4; ++b) {
      h ^= static_cast<u8>(w >> (8 * b));
      h *= 0x100000001b3ull;
    }
  }
  return h ^ static_cast<u64>(n.size());
}

s64 script_offset(int map, u32 na_offset) {
  const ScriptMap* sm = script_map(map);
  if (sm == nullptr) return na_offset;
  for (u32 i = 0; i < sm->nseg; ++i) {
    const Segment& seg = sm->seg[i];
    if (na_offset >= seg.na_off && na_offset - seg.na_off < seg.size) return static_cast<s64>(na_offset) + seg.delta;
  }
  return -1;
}

s64 script_na_offset(int map, u32 offset) {
  const ScriptMap* sm = script_map(map);
  if (sm == nullptr) return offset;
  for (u32 i = 0; i < sm->nseg; ++i) {
    const Segment& seg = sm->seg[i];
    const s64 start = static_cast<s64>(seg.na_off) + seg.delta;
    if (static_cast<s64>(offset) >= start && static_cast<s64>(offset) - start < seg.size)
      return static_cast<s64>(offset) - seg.delta;
  }
  return -1;
}

void log_read(const std::string& key, u32 address) {
  ReadLog& log = read_log();
  if (!log.on) return;
  std::lock_guard<std::mutex> guard(log.lock);
  // Bounded: the images hold a few hundred thousand distinct addresses at most, so more means
  // a reader running away over unmapped data (it must not take the machine's memory with it).
  constexpr size_t kMaxReads = 1000000;
  if (log.seen.size() >= kMaxReads) {
    if (!log.full) std::fprintf(stderr, "[port] region: SS_REGION_READ_LOG is full; further reads are not logged\n");
    log.full = true;
    return;
  }
  log.seen.emplace(key, address);
}

}  // namespace ss::region
