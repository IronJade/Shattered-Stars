// Region profiles and address maps (runtime/region.h). The first tests need no disc; the
// disc tests need the user's images: SS_DISC1 (North America), SS_DISC1_EU / _FR / _DE
// (Europe, France, Germany, disc 1); each is skipped when its variable is unset.

#include <cstdlib>
#include <set>
#include <string>
#include <vector>

#include "disc/archive.h"
#include "disc/slz.h"
#include "runtime/boot_exe.h"
#include "runtime/region.h"
#include "test.h"

using namespace ss;

namespace {

struct RegionGuard {  // restores North America after a test selects another region
  ~RegionGuard() { region::select(region::Id::kNorthAmerica); }
};

void put32(std::vector<u8>& b, u32 w) {
  for (int i = 0; i < 4; ++i) b.push_back(static_cast<u8>(w >> (8 * i)));
}

struct OpenDisc {
  std::unique_ptr<disc::DiscImage> disc;
  disc::Archive archive;
  bool ok = false;
};

OpenDisc open_disc(const char* env) {
  OpenDisc d;
  const char* path = std::getenv(env);
  if (!path || !*path) {
    std::printf("  (skipped: %s not set)\n", env);
    return d;
  }
  std::string error;
  d.disc = disc::DiscImage::open(path, &error);
  d.ok = d.disc && d.archive.open(*d.disc, &error);
  if (!d.ok) std::fprintf(stderr, "  cannot open %s: %s\n", path, error.c_str());
  return d;
}

std::vector<u8> unpack(disc::Archive& a, int entry, int block) {
  auto packed = a.read_entry(static_cast<u32>(entry));
  if (!packed) return {};
  auto un = disc::slz::unpack_block(*packed, 0, block, nullptr);
  return un ? std::move(*un) : std::vector<u8>{};
}

s32 rd32s(const std::vector<u8>& d, size_t at) { return at + 4 <= d.size() ? static_cast<s32>(read_u32(d.data() + at)) : -1; }

// An encounter pack's code blocks (game/battle/encounter.cpp): {slot kind, offset} after
// two -1-terminated lists of pairs.
std::vector<std::vector<u8>> pack_code_blocks(const std::vector<u8>& pack) {
  std::vector<std::vector<u8>> out;
  size_t p = 0;
  for (int list = 0; list < 2; ++list) {
    while (rd32s(pack, p) != -1 && p < pack.size()) p += 8;
    p += 4;
  }
  while (p + 8 <= pack.size() && rd32s(pack, p) != -1) {
    const u32 off = static_cast<u32>(rd32s(pack, p + 4));
    if (off < pack.size()) {
      if (auto b = disc::slz::unpack_block(std::span<const u8>(pack).subspan(off), 0, 0, nullptr))
        out.push_back(std::move(*b));
    }
    p += 8;
  }
  return out;
}

// A spell data entry's module (block 0 of its op-6 container), game/battle/spellcast.cpp.
std::vector<u8> spell_module(const std::vector<u8>& d) {
  size_t at = 0;
  u32 op = static_cast<u32>(rd32s(d, 0));
  if (op == 5) {
    at = 0xC;
    op = static_cast<u32>(rd32s(d, at));
  }
  if (op != 6) return {};
  const u32 off = static_cast<u32>(rd32s(d, at + 4));
  if (off >= d.size()) return {};
  auto b = disc::slz::unpack_block(std::span<const u8>(d).subspan(off), 0, 0, nullptr);
  return b ? std::move(*b) : std::vector<u8>{};
}

}  // namespace

TEST(region_profiles_by_serial) {
  int disc = 0;
  const region::Profile* na = region::from_serial("SCUS_944.21", &disc);
  CHECK(na != nullptr && na->id == region::Id::kNorthAmerica && disc == 1);
  CHECK(region::from_serial("scus_944.22", &disc) == na && disc == 2);
  const region::Profile* eu = region::from_serial("SCES_021.59", &disc);
  CHECK(eu != nullptr && eu->id == region::Id::kEurope && disc == 1);
  CHECK(region::from_serial("SCES_121.59", &disc) == eu && disc == 2);
  CHECK(region::from_serial("SCES_021.60")->id == region::Id::kFrance);
  CHECK(region::from_serial("SCES_121.61")->id == region::Id::kGermany);
  const region::Profile* jp = region::from_serial("SLPM_861.06", &disc);
  CHECK(jp != nullptr && jp->id == region::Id::kJapan && disc == 2 && !jp->supported);
  CHECK(region::from_serial("SLUS_123.45", &disc) == nullptr && disc == 0);
  CHECK(std::string(na->product) == "BASCUS-94421");
  CHECK(std::string(eu->product) == "BASCES-02159");
  CHECK(std::string(region::profile(region::Id::kFrance).product) == "BESCES-02160");
  CHECK(std::string(region::profile(region::Id::kGermany).product) == "BESCES-02161");
  CHECK(na->supported && eu->supported);
  CHECK(region::profile(region::Id::kFrance).supported && region::profile(region::Id::kGermany).supported);
  CHECK(region::playable(region::profile(region::Id::kFrance)) && !region::profile(region::Id::kJapan).supported);
  CHECK(eu->text == region::Text::kNorthAmerica);
  CHECK(region::profile(region::Id::kFrance).text == region::Text::kFrench);
  CHECK(region::profile(region::Id::kGermany).text == region::Text::kGerman);
  CHECK(region::profiles().front().id == region::Id::kNorthAmerica);
  CHECK(region::is_na());
  CHECK(!region::has_maps());  // NA reads its own addresses
  CHECK(region::entry_map(1) == nullptr);
}

// The generated maps hold together: segments sorted and disjoint, translate and
// to_na_address inverse to each other, every pointer run's target an image of the table.
TEST(region_europe_maps_are_consistent) {
  RegionGuard guard;
  for (region::Id id : {region::Id::kEurope, region::Id::kFrance, region::Id::kGermany}) {
    region::select(id);
    CHECK(region::has_maps());
    CHECK(region::boot_map() != nullptr);
    for (int e : {1, 3100, 2129, 3107, 0xC1D, 0xC1E, 0xBA5, 0xBB9, 1981}) CHECK(region::entry_map(e) != nullptr);
    CHECK(region::entry_map(1984, 0) != nullptr && region::entry_map(1984, 1) != nullptr);
    const region::ImageMap* title = region::entry_map(1);
    CHECK_EQ(title->na_base, 0x8002F810u);
    CHECK_EQ(title->rg_base, 0x8002F9C0u);  // the PAL programs load 0x1b0 higher
    for (int e : {1, 3100, 2129, 0xC1D, 1981}) {
      const region::ImageMap* m = region::entry_map(e);
      u32 prev_end = 0;
      s64 prev_rg_end = 0;
      for (u32 i = 0; i < m->nseg; ++i) {
        const region::Segment& s = m->seg[i];
        CHECK(s.na_off >= prev_end);
        CHECK(static_cast<s64>(s.na_off) + s.delta >= prev_rg_end);  // order kept: the inverse is a search
        CHECK(static_cast<s64>(s.na_off) + s.delta + s.size <= m->rg_size);
        prev_end = s.na_off + s.size;
        prev_rg_end = static_cast<s64>(s.na_off) + s.delta + s.size;
        for (u32 off : {s.na_off, s.na_off + s.size / 2, s.na_off + s.size - 1}) {
          const s64 r = region::translate(m, off);
          CHECK(r == static_cast<s64>(off) + s.delta);
          CHECK_EQ(region::to_na_address(m, m->rg_base + static_cast<u32>(r)), m->na_base + off);
        }
      }
      CHECK(prev_end <= m->na_size);
    }
    // A byte outside every segment has no counterpart, and reads as nothing.
    CHECK_EQ(region::translate(title, title->na_size + 4), -1);
  }
}

// Map scripts: offsets translate both ways in the maps Europe laid out otherwise (the world
// script shared by maps 0-3 / 826, maps 688 and 704); every other map, and NA, keep NA's;
// a PAL release's 50 Hz counts are converted, NA's kept.
TEST(region_europe_script_offsets) {
  RegionGuard guard;
  CHECK_EQ(region::script_offset(688, 0x9684), 0x9684);  // NA: as given
  CHECK_EQ(region::ntsc_vblanks(600), 600);
  region::select(region::Id::kEurope);
  CHECK_EQ(region::ntsc_vblanks(600), 720);
  CHECK_EQ(region::ntsc_vblanks(1000), 1200);
  CHECK_EQ(region::script_offset(18, 0x1234), 0x1234);  // laid out like NA
  CHECK_EQ(region::script_na_offset(18, 0x1234), 0x1234);
  for (int map : {0, 1, 826, 688, 704}) {
    int mapped = 0, missing = 0;
    s64 prev = -1;
    for (u32 off = 0; off < 0xC400; off += 4) {
      const s64 at = region::script_offset(map, off);
      if (at < 0) {
        ++missing;
        continue;
      }
      ++mapped;
      CHECK(at > prev);  // the order is kept
      prev = at;
      CHECK_EQ(region::script_na_offset(map, static_cast<u32>(at)), static_cast<s64>(off));
    }
    CHECK(mapped > 0x1000);
    if (map == 688) CHECK(missing > 0);  // a few words only NA has (its code is 8 bytes longer)
  }
  // Map 688's door transfers (docs/ap/doors.json offsets 0x9684 ...) moved by -8.
  CHECK_EQ(region::script_offset(688, 0x9684), 0x9684 - 8);
  CHECK_EQ(region::script_offset(0, 0), region::script_offset(826, 0));  // one shared script
}

TEST(region_module_hash_ignores_link_addresses) {
  std::vector<u8> a, b, c;
  // lui v0,0x8009 ; addiu v0,v0,0x1234 ; jal 0x80089000 ; nop ; .word 0x80088120 (a pointer)
  for (u32 w : {0x3C028009u, 0x24421234u, 0x0C022400u, 0u, 0x80088120u}) put32(a, w);
  // the same code linked 0xa4 higher
  for (u32 w : {0x3C028009u, 0x244212D8u, 0x0C022429u, 0u, 0x800881C4u}) put32(b, w);
  // a different instruction (addiu v0,v0,1 without a lui before it)
  for (u32 w : {0x24420001u, 0x244212D8u, 0x0C022429u, 0u, 0x800881C4u}) put32(c, w);
  CHECK(region::module_hash(a) == region::module_hash(b));
  CHECK(region::module_hash(a) != region::module_hash(c));
  CHECK(region::fnv1a64(a) != region::fnv1a64(b));
}

// North America: every enemy and spell module is known by its normalised hash, as the
// same module the exact hash named before (so NA behaves as it always did).
TEST(region_na_modules_identity) {
  auto d = open_disc("SS_DISC1");
  if (!d.ok) return;
  std::set<u64> enemy, spell;
  for (int enc = 0; enc < 564; ++enc) {
    auto p = d.archive.read_entry(static_cast<u32>(1405 + enc));
    if (!p) continue;
    for (const auto& b : pack_code_blocks(*p)) {
      CHECK(region::module_identity(region::ImageKind::kEnemy, b) == region::fnv1a64(b));
      enemy.insert(region::fnv1a64(b));
    }
  }
  for (int e = 0x8E1; e <= 0x982; ++e) {
    auto p = d.archive.read_entry(static_cast<u32>(e));
    if (!p) continue;
    const std::vector<u8> m = spell_module(*p);
    if (m.empty()) continue;
    CHECK(region::module_identity(region::ImageKind::kSpell, m) == region::fnv1a64(m));
    spell.insert(region::fnv1a64(m));
  }
  CHECK_EQ(enemy.size(), 40u);
  CHECK_EQ(spell.size(), 55u);
}

// Europe, France, Germany: every image the port reads is the one the map was made from,
// every enemy and spell module is identified (as its NA twin), and the boot executable's
// tables read at NA addresses.
void check_release_images(const char* env, region::Id release) {
  auto d = open_disc(env);
  if (!d.ok) return;
  RegionGuard guard;
  CHECK(region::from_serial(runtime::boot_exe_name(*d.disc))->id == release);
  region::select(release);
  for (int e : {1, 3100, 2129, 3107, 0xC1D, 0xC1E, 0xC1F, 0xBA5, 0xBB9, 0xBA6, 0xBAA, 0xBB6, 0xBCE, 1981, 2131, 2159}) {
    std::vector<u8> img = unpack(d.archive, e, 0);
    const region::ImageMap* m = region::entry_map(e);
    CHECK(m != nullptr);
    if (m != nullptr) CHECK(region::prepare(m, img));
  }
  for (int b = 0; b <= 10; ++b) {
    std::vector<u8> img = unpack(d.archive, 1984, b);
    const region::ImageMap* m = region::entry_map(1984, b);
    CHECK(m != nullptr && region::prepare(m, img));
  }
  std::set<u64> enemy, spell;
  for (int enc = 0; enc < 564; ++enc) {
    auto p = d.archive.read_entry(static_cast<u32>(1405 + enc));
    if (!p) continue;
    for (auto& b : pack_code_blocks(*p)) {
      const region::ImageMap* m = nullptr;
      const u64 id = region::module_identity(region::ImageKind::kEnemy, b, &m);
      CHECK(id != 0 && m != nullptr && m->na_fnv == id);
      if (m != nullptr) CHECK(region::prepare(m, b));
      enemy.insert(id);
    }
  }
  for (int e = 0x8E1; e <= 0x982; ++e) {
    auto p = d.archive.read_entry(static_cast<u32>(e));
    if (!p) continue;
    std::vector<u8> mod = spell_module(*p);
    if (mod.empty()) continue;
    const region::ImageMap* m = nullptr;
    const u64 id = region::module_identity(region::ImageKind::kSpell, mod, &m);
    CHECK(id != 0 && m != nullptr && m->na_fnv == id);
    if (m != nullptr) CHECK(region::prepare(m, mod));
    spell.insert(id);
  }
  CHECK_EQ(enemy.size(), 40u);
  CHECK_EQ(spell.size(), 55u);
  // The sound driver's pitch table and libspu's reverb presets (NA addresses).
  CHECK(runtime::boot_exe_open(*d.disc));
  CHECK(runtime::boot_exe_data(0x8002B6C0) != nullptr);
  CHECK(runtime::boot_exe_data(0x8002BC34) != nullptr);
  region::select(region::Id::kNorthAmerica);
  auto na = open_disc("SS_DISC1");
  if (na.ok) {  // the same bytes as NA's at those addresses
    std::vector<u8> eu_table(0x44 * 2), na_table(0x44 * 2);
    region::select(release);
    CHECK(runtime::boot_exe_open(*d.disc));
    for (u32 i = 0; i < eu_table.size(); ++i) eu_table[i] = *runtime::boot_exe_data(0x8002BC34 + i);
    region::select(region::Id::kNorthAmerica);
    CHECK(runtime::boot_exe_open(*na.disc));
    for (u32 i = 0; i < na_table.size(); ++i) na_table[i] = *runtime::boot_exe_data(0x8002BC34 + i);
    CHECK(eu_table == na_table);
    // #1984 block 1's skill_no_clash (0x80085504, u8[7], spells.cpp spell_clash_check): NA's
    // 7 spells read the same through the map (the PAL table is 9 long; the word holding
    // entries 5-7 has a pad byte that differs, which left them unmapped once).
    std::vector<u8> na_b1 = unpack(na.archive, 1984, 1), rg_b1 = unpack(d.archive, 1984, 1);
    region::select(release);
    const region::ImageMap* m1 = region::entry_map(1984, 1);
    CHECK(m1 != nullptr && region::prepare(m1, rg_b1));
    for (u32 a = 0x80085504; a < 0x8008550B; ++a) {
      const u8* n = region::image_byte(nullptr, na_b1, 0x80081728, a, nullptr);
      const u8* r = region::image_byte(m1, rg_b1, 0x80081728, a, nullptr);
      CHECK(n != nullptr && r != nullptr && *n == *r);
    }
    region::select(region::Id::kNorthAmerica);
  }
}

TEST(region_europe_disc_images) { check_release_images("SS_DISC1_EU", region::Id::kEurope); }
TEST(region_france_disc_images) { check_release_images("SS_DISC1_FR", region::Id::kFrance); }
TEST(region_germany_disc_images) { check_release_images("SS_DISC1_DE", region::Id::kGermany); }
