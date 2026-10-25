// Menu framework (game/common/menu_*): the save file's zero-run packing, and with
// SS_DISC1 set a load of a real save compared with the original and the scene's
// window-art uploads against the resource packs.
//
// The save is the first position of the card image SS_SAVE_CARD (default
// analysis/saves/dex_north_city.mcd); the test reads a copy and never writes the
// original. The reference, analysis/reference/loadscreen/dex_loaded_blocks.bin, is the
// handoff blocks 2 (0x2a0), 3 (0x300), 4 (0x680), 6 (0xc28) and 0xe (0x440) as the
// original left them in PCSX-Redux when the title program ran the field after loading
// that save through the Continue screen (main RAM at boot_run_main_program).

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "disc/archive.h"
#include "disc/disc_image.h"
#include "disc/slz.h"
#include "game/boot/system.h"
#include "game/common/memcard.h"
#include "game/common/menu_save.h"
#include "game/common/menu_scene.h"
#include "game/title/load_screen.h"
#include "game/title/title_program.h"
#include "runtime/runtime.h"
#include "test.h"

using namespace ss;
using game::HandoffRegistry;
using game::handoff_clear;
using game::handoff_get;

namespace {

std::vector<u8> read_file(const std::filesystem::path& p) {
  std::ifstream f(p, std::ios::binary);
  return std::vector<u8>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

bool ensure_system() {
  if (game::get_system() != nullptr && game::get_system()->loader.archive() != nullptr) return true;
  static std::unique_ptr<disc::DiscImage> image;
  static disc::Archive archive;
  const char* path = std::getenv("SS_DISC1");
  if (!path || !*path) return false;
  std::string error;
  image = disc::DiscImage::open(path, &error);
  if (!image || !archive.open(*image, &error)) {
    std::fprintf(stderr, "  cannot open %s: %s\n", path, error.c_str());
    ++test::failures;
    return false;
  }
  game::boot_main(0, &archive);
  return true;
}

}  // namespace

TEST(menu_zero_pack_round_trip) {
  // Runs of every length around the code's limits (1, 2, 256, 257 zeros) between bytes.
  std::vector<u8> data;
  for (int run : {0, 1, 2, 3, 255, 256, 257, 300, 512, 1})
    data.insert(data.end(), static_cast<size_t>(run), 0), data.push_back(static_cast<u8>(run | 1));
  data.insert(data.end(), 5, 0);
  std::vector<u8> packed(data.size() * 2 + 16, 0xEE);
  const int size = game::menu::zero_pack(packed.data(), data.data(), static_cast<int>(data.size()));
  std::vector<u8> out(data.size() + 300, 0xEE);
  game::menu::zero_unpack(out.data(), packed.data());
  CHECK(std::memcmp(out.data(), data.data(), data.size()) == 0);
  CHECK_EQ(size, static_cast<int>(packed[0] | packed[1] << 8) + 2);
  // A single zero is one byte; two or more are three.
  const u8 one[3] = {5, 0, 6};
  u8 p1[16];
  CHECK_EQ(game::menu::zero_pack(p1, one, 3), 2 + 3);
  const u8 two[4] = {5, 0, 0, 6};
  CHECK_EQ(game::menu::zero_pack(p1, two, 4), 2 + 5);
  CHECK_EQ(p1[4], 0);  // run - 2
}

TEST(menu_load_save_matches_original) {
  const char* card_env = std::getenv("SS_SAVE_CARD");
  const std::filesystem::path card_path = card_env ? card_env : "analysis/saves/dex_north_city.mcd";
  const std::filesystem::path ref_path = "analysis/reference/loadscreen/dex_loaded_blocks.bin";
  if (!std::filesystem::exists(card_path) || !std::filesystem::exists(ref_path)) {
    std::printf("  (skipped: %s or %s missing)\n", card_path.string().c_str(), ref_path.string().c_str());
    return;
  }
  if (!ensure_system()) {
    std::printf("  (skipped: SS_DISC1 not set)\n");
    return;
  }
  // Work on a copy: the card layer writes images back.
  const std::filesystem::path copy = std::filesystem::temp_directory_path() / "ss_test_menu_card.mcd";
  std::filesystem::copy_file(card_path, copy, std::filesystem::copy_options::overwrite_existing);
  game::memcard_set_images(copy.string().c_str(), nullptr);
  game::create_game_state();
  HandoffRegistry* h = &game::get_system()->handoff;
  if (auto* old = static_cast<u8*>(handoff_get(h, 0xE))) {
    delete[] old;
    handoff_clear(h, 0xE);
  }
  game::MemoryCard* card = game::memcard_create();
  const int ok = game::load_save_without_menu(card, 0, 0);
  game::memcard_destroy(card);
  CHECK_EQ(ok, 1);
  const std::vector<u8> ref = read_file(ref_path);
  CHECK_EQ(ref.size(), static_cast<size_t>(0x2A0 + 0x300 + 0x680 + 0xC28 + 0x440));
  if (ok == 1 && ref.size() == 0x1C88) {
    struct Block {
      const char* name;
      int slot;
      size_t size;
    } blocks[] = {{"config", 2, 0x2A0}, {"party", 3, 0x300}, {"records", 4, 0x680},
                  {"inventory", 6, 0xC28}, {"snapshot", 0xE, 0x440}};
    size_t at = 0;
    for (const Block& b : blocks) {
      const auto* live = static_cast<const u8*>(handoff_get(h, b.slot));
      CHECK(live != nullptr);
      if (!live) continue;
      int differ = 0, first = -1;
      for (size_t i = 0; i < b.size; ++i) {
        // The inventory's check bytes are made with a random key at every load.
        if (b.slot == 6 && i >= 0x824 && i < 0xC24) continue;
        if (live[i] != ref[at + i]) {
          if (first < 0) first = static_cast<int>(i);
          ++differ;
        }
      }
      if (differ) std::fprintf(stderr, "  %s: %d bytes differ (first +0x%x)\n", b.name, differ, first);
      CHECK_EQ(differ, 0);
      at += b.size;
    }
  }
  // Leave no snapshot behind for the field tests.
  if (auto* snap = static_cast<u8*>(handoff_get(h, 0xE))) {
    delete[] snap;
    handoff_clear(h, 0xE);
  }
  game::memcard_set_images(nullptr, nullptr);
  std::filesystem::remove(copy);
}

TEST(menu_window_art_fits_pack) {
  // The scene's window-art rects are the sizes in the resource packs' TIM headers, and each
  // upload ends inside its pack (the port had TIM 0 as 64x256 and read 0x25e0 bytes past
  // the 23624-byte pack at every menu). Every field menu kind's pack and the title's.
  if (!ensure_system()) {
    std::printf("  (skipped: SS_DISC1 not set)\n");
    return;
  }
  disc::Archive* archive = game::get_system()->loader.archive();
  psx::Gpu& gpu = runtime::Runtime::get().gpu;
  std::vector<u8>& res = game::menu::globals().res;
  const std::vector<u8> saved_res = res;
  const std::vector<u16> saved_vram(gpu.vram().begin(), gpu.vram().end());
  for (u32 entry : {0xBA4u, 0xBA8u, 0xBACu, 0xBB0u, 0xBB4u, 0xBD0u}) {
    auto raw = archive->read_entry(entry);
    CHECK(raw.has_value());
    if (!raw) continue;
    auto pack = disc::slz::unpack_block(*raw, 0, 0, nullptr);
    CHECK(pack.has_value());
    if (!pack) continue;
    CHECK_EQ(pack->size(), static_cast<size_t>(23624));
    for (int i = 0; i < 2; ++i) {
      const game::menu::WindowArt& art = game::menu::kWindowArt[i];
      const size_t at = read_u32(pack->data() + i * 4);
      CHECK(at + 0x220 <= pack->size());
      if (at + 0x220 > pack->size()) continue;
      const u8* tim = pack->data() + at;
      CHECK_EQ(read_u32(tim), 0x10u);
      CHECK_EQ(read_u32(tim + 4), 8u);  // 4-bit, with a CLUT block
      CHECK_EQ(static_cast<s16>(read_u16(tim + 0x12)), art.clut[3]);
      CHECK_EQ(static_cast<s16>(read_u16(tim + 0x21C)), art.pixels.w);
      CHECK_EQ(static_cast<s16>(read_u16(tim + 0x21E)), art.pixels.h);
      const size_t end = at + 0x220 + static_cast<size_t>(art.pixels.w) * art.pixels.h * 2;
      CHECK(end <= pack->size());
      // The upload puts exactly those bytes at the rect.
      res = *pack;
      CHECK(game::menu::MenuScene::upload_tim(i, art.pixels, art.clut));
      int differ = 0;
      for (int y = 0; y < art.pixels.h; ++y)
        for (int x = 0; x < art.pixels.w; ++x)
          differ += gpu.pixel(art.pixels.x + x, art.pixels.y + y) != read_u16(tim + 0x220 + (y * art.pixels.w + x) * 2);
      CHECK_EQ(differ, 0);
      // A pack that ends one pixel early is not uploaded (nothing read past it).
      res.resize(end - 2);
      gpu.vram()[static_cast<size_t>(art.pixels.y) * psx::kVramWidth + art.pixels.x] = 0x1234;
      CHECK(!game::menu::MenuScene::upload_tim(i, art.pixels, art.clut));
      CHECK_EQ(gpu.pixel(art.pixels.x, art.pixels.y), 0x1234);
    }
  }
  res = saved_res;
  std::copy(saved_vram.begin(), saved_vram.end(), gpu.vram().begin());
}
