// World map (overlay #3102, game/field/worldmap*): the terrain block format and the
// height queries the walkers use, checked on the disc's blocks, and the host disc
// drive (loader_insert_disc). The expected values come from the original running in
// PCSX-Redux: the user's save stands at (57970, -306, 29046) on map 2, terrain type 2
// (encounter group 50, stage 2). These tests need SS_DISC1.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "core/types.h"
#include "disc/archive.h"
#include "disc/disc_image.h"
#include "game/boot/system.h"
#include "game/field/field.h"
#include "game/field/worldmap.h"
#include "test.h"

using namespace ss;
using namespace ss::game;
using namespace ss::game::field;

namespace {

disc::Archive* g_archive = nullptr;

// The disc (another test may have booted it already) and the field program's tables
// (the overlay's SquareRoot0 / ratan2 live in the field image).
bool world_disc() {
  static std::unique_ptr<disc::DiscImage> image;
  static disc::Archive archive;
  static bool tried = false;
  if (get_system()->loader.archive() != nullptr) {
    g_archive = get_system()->loader.archive();
  } else if (!tried) {
    tried = true;
    const char* path = std::getenv("SS_DISC1");
    if (path == nullptr || *path == 0) return false;
    std::string error;
    image = disc::DiscImage::open(path, &error);
    if (!image || !archive.open(*image, &error)) {
      std::fprintf(stderr, "  cannot open %s: %s\n", path, error.c_str());
      return false;
    }
    boot_main(0, &archive);
    g_archive = &archive;
  }
  return g_archive != nullptr && field_data(0x80077BAC) != nullptr;
}

#define NEEDS_WORLD_DISC()                                     \
  do {                                                         \
    if (!world_disc()) {                                       \
      std::printf("  (skipped: SS_DISC1 not set)\n");          \
      return;                                                  \
    }                                                          \
  } while (0)

s32 rd32(const u8* p) {
  s32 v;
  std::memcpy(&v, p, 4);
  return v;
}
u16 rd16(const u8* p) {
  u16 v;
  std::memcpy(&v, p, 2);
  return v;
}

// A world-map state: game_state (+0x1a map), script flags, and the nine blocks around
// block (bx, bz) of 'map' loaded into the block buffer the way worldbg_stream_blocks
// leaves it. Restores the globals afterwards.
struct WorldFixture {
  std::vector<u8> game_state = std::vector<u8>(0x2a0, 0);
  std::vector<u8> script_flags = std::vector<u8>(0x170, 0);
  u8* saved_gs;
  u8* saved_flags;
  WorldBg bg;
  WorldFixture(int map, int bx, int bz) {
    FieldGlobals& g = globals();
    saved_gs = g.game_state;
    saved_flags = g.script_flags;
    g.game_state = game_state.data();
    g.script_flags = script_flags.data();
    const u16 m = static_cast<u16>(map);
    std::memcpy(game_state.data() + 0x1a, &m, 2);
    WorldGlobals& w = world();
    w.blocks.assign(9 * 0xd000, 0);
    const int base = map == 1 ? 0xfd4 : 0x1060;
    int slot = 0;
    for (int dz = -1; dz <= 1; ++dz) {
      for (int dx = -1; dx <= 1; ++dx, ++slot) {
        const int block = (bx + dx + 9) % 9 + ((bz + dz + 7) % 7) * 9;
        w.loaded[slot] = -1;
        auto data = g_archive->read_entry(static_cast<u32>(base + block));
        if (!data || data->size() > 0xd000) continue;
        std::memcpy(w.blocks.data() + slot * 0xd000, data->data(), data->size());
        w.loaded[slot] = static_cast<s16>(base + block);
      }
    }
  }
  ~WorldFixture() {
    globals().game_state = saved_gs;
    globals().script_flags = saved_flags;
    world().blocks.clear();
    for (s16& l : world().loaded) l = -1;
  }
};

}  // namespace

// Every block entry of maps 1 and 2 fits a 0xd000 buffer slot and its tables stay inside
// it: sub-cell vertex lists, the 16 sub-cell headers, the polygon areas (11 record types
// of sizes 0x18 0x14 0x10 0x10 0x20 0x18 0x14 0x14 0x10 0x18 0x20) and the coarse height grid.
TEST(world_block_format) {
  NEEDS_WORLD_DISC();
  static const int kSize[11] = {0x18, 0x14, 0x10, 0x10, 0x20, 0x18, 0x14, 0x14, 0x10, 0x18, 0x20};
  int checked = 0;
  for (int base : {0xfd4, 0x1060}) {
    for (int b = 0; b < 63; ++b) {
      auto data = g_archive->read_entry(static_cast<u32>(base + b));
      CHECK(data.has_value());
      if (!data) continue;
      CHECK(data->size() <= 0xd000u);
      const u8* blk = data->data();
      const s32 n = rd32(blk);
      CHECK(n > 0 && 0x68 + n * 8 + 0x280 <= static_cast<s32>(data->size()));
      if (n <= 0 || 0x68 + n * 8 + 0x280 > static_cast<s32>(data->size())) continue;
      const s32 grid = rd32(blk + 4);
      CHECK(grid > 0 && grid + 33 * 33 <= static_cast<s32>(data->size()));
      // Each sub-cell lists its own vertices (shared edge vertices appear in both lists).
      s32 total = 0;
      for (int i = 0; i < 16; ++i) {
        const s32 off = rd32(blk + 0x28 + i * 4), cnt = rd16(blk + 8 + i * 2);
        CHECK(off >= 0 && off + cnt * 8 <= n * 8);
        total += cnt;
      }
      CHECK(total >= n);
      const u8* h = blk + 0x68 + n * 8;
      for (int i = 0; i < 16; ++i) {
        const u8* hdr = h + i * 0x20;
        s32 bytes = rd16(hdr) & 0xfffc;
        for (int t = 0; t < 11; ++t) bytes += rd16(hdr + 2 + t * 2) * kSize[t];
        const s32 start = static_cast<s32>(h - blk) + 0x280 + rd32(h + 0x200 + i * 4);
        CHECK(start + bytes <= static_cast<s32>(data->size()));
      }
      ++checked;
    }
  }
  CHECK_EQ(checked, 126);
}

// The save's spot on map 2: height -306 on a walkable polygon of terrain type 2 (the
// original's leader stands there; encounter group 50 / stage 2 come from that type).
TEST(world_terrain_query_save_spot) {
  NEEDS_WORLD_DISC();
  WorldFixture fx(2, 57970 / 0x3000, 29046 / 0x3000);
  TerrainHit hit;
  const s32 h = world_terrain_query(&fx.bg, 57970 << 12, 29046 << 12, &hit);
  CHECK_EQ(h, -306);
  CHECK_EQ(hit.off_map, 0);
  CHECK((hit.attr & 1) != 0);
  CHECK_EQ(hit.attr >> 4, 2);
  CHECK(hit.corners == 3 || hit.corners == 4);
  // Far outside the loaded blocks: unknown, flagged off-map.
  TerrainHit far;
  CHECK_EQ(world_terrain_query(&fx.bg, 1000 << 12, 1000 << 12, &far), -0x8000);
  CHECK_EQ(far.off_map, 1);
  // Map 2 outside the whirlwind's disc (r 0x5cf8 around (67000, 40000)) is never walkable:
  // terrain type 0xf, walkable bit cleared (the water bits stay).
  TerrainHit out;
  const s32 hx = 67000 - 0x5cf8 - 0x200, hz = 40000;
  if (world_terrain_query(&fx.bg, hx << 12, hz << 12, &out) != -0x8000) {
    CHECK_EQ(out.attr >> 4, 0xf);
    CHECK_EQ(out.attr & 1, 0);
  }
}

// Walking 90 units ahead from the save spot (the walker's probe distance) stays on the
// same ground level within a few units, and the coarse camera grid agrees with the
// polygons to within its 10-unit steps.
TEST(world_terrain_neighbourhood) {
  NEEDS_WORLD_DISC();
  WorldFixture fx(2, 57970 / 0x3000, 29046 / 0x3000);
  TerrainHit hit;
  const s32 h0 = world_terrain_query(&fx.bg, 57970 << 12, 29046 << 12, &hit);
  const s32 h1 = world_terrain_query(&fx.bg, (57970 + 90) << 12, 29046 << 12, &hit);
  CHECK(h1 != -0x8000);
  CHECK(std::abs(h1 - h0) < 40);
  const s32 coarse = world_terrain_camera_floor(&fx.bg, 57970 << 12, 29046 << 12);
  CHECK(std::abs(coarse - h0) <= 30);
}

// The host disc drive: a loader with no archive takes the registered disc 1; disc 2 is
// only available when one was registered.
TEST(world_loader_insert_disc) {
  NEEDS_WORLD_DISC();
  loader_register_disc(1, g_archive);
  Loader loader;
  CHECK(loader_insert_disc(loader, 1));
  CHECK(loader.archive() == g_archive);
  CHECK(loader.entry_size(6) != 0);  // disc 1's marker entry
  if (g_archive->has_entry(7)) return;  // SS_DISC1 is a disc 2 image
  CHECK(!loader_insert_disc(loader, 2));
  CHECK(loader.archive() == g_archive);
}
