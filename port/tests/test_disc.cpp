// Regression tests against a real disc. They need the user's own image, given by
// SS_DISC1 (and optionally SS_DISC2); without it they are skipped.

#include <cstdlib>
#include <string>

#include "disc/archive.h"
#include "disc/slz.h"
#include "test.h"

using namespace ss;

namespace {

u64 fnv1a(const u8* p, size_t n, u64 h = 0xcbf29ce484222325ull) {
  for (size_t i = 0; i < n; ++i) h = (h ^ p[i]) * 0x100000001b3ull;
  return h;
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

}  // namespace

TEST(disc1_table_matches_reference) {
  auto d = open_disc("SS_DISC1");
  if (!d.ok) return;
  size_t present = 0;
  u64 h = 0xcbf29ce484222325ull;
  for (size_t i = 0; i < d.archive.entry_count(); ++i) {
    const auto& e = d.archive.entry(static_cast<u32>(i));
    present += e.present();
    const u32 pair[2] = {e.lba, e.sectors};
    h = fnv1a(reinterpret_cast<const u8*>(pair), sizeof pair, h);
  }
  CHECK_EQ(present, 4155u);
  CHECK(h == 0x68ad15712d55abefull);  // same table as tools/so2_archive.py
  CHECK(d.archive.entry(3300).lba == 46458 && d.archive.entry(3300).sectors == 45);
}

TEST(disc1_main_program_unpacks_exactly) {
  auto d = open_disc("SS_DISC1");
  if (!d.ok) return;
  auto data = d.archive.read_entry(1);
  CHECK(data.has_value());
  if (!data) return;
  std::string error;
  auto out = disc::slz::unpack_block(*data, 0, 0, &error);
  CHECK(out.has_value());
  if (!out) return;
  CHECK_EQ(out->size(), 283604u);
  CHECK(fnv1a(out->data(), out->size()) == 0x62eb4792a6d87f52ull);  // byte-identical to the live RAM dump
}

TEST(disc1_every_top_level_block_unpacks) {
  auto d = open_disc("SS_DISC1");
  if (!d.ok) return;
  int blocks = 0, failed = 0;
  for (u32 i = 0; i < d.archive.entry_count(); ++i) {
    if (!d.archive.has_entry(i)) continue;
    auto data = d.archive.read_entry(i);
    if (!data) continue;
    auto h = disc::slz::header_at(*data, 0);
    while (h) {
      std::vector<u8> out;
      std::string error;
      ++blocks;
      if (!disc::slz::unpack(*data, h->offset, out, &error) || out.size() != h->unpacked) ++failed;
      if (h->next == 0) break;
      h = disc::slz::header_at(*data, h->offset + h->next);
    }
  }
  CHECK_EQ(blocks, 442);  // matches tools/so2_archive.py extract
  CHECK_EQ(failed, 0);
}
