// Save files written the way the original writes them (phase 4 round-trip work): the
// field save page's icon (overlay 0xbb6 0x8007f648, entry #3027) and the bytes its file
// buffer inherits from the freed icon sheet. The expected bytes come from a save
// PCSX-Redux wrote from the north-city state (story progress 38, game_state +0x24c 272).
// Needs SS_DISC1.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "disc/archive.h"
#include "disc/disc_image.h"
#include "game/boot/system.h"
#include "game/common/save_icon.h"
#include "test.h"

using namespace ss;
using namespace ss::game;

namespace {

bool disc_ready() {
  static std::unique_ptr<disc::DiscImage> image;
  static disc::Archive archive;
  if (get_system()->loader.archive() != nullptr) return true;
  const char* path = std::getenv("SS_DISC1");
  if (path == nullptr || *path == 0) return false;
  std::string error;
  image = disc::DiscImage::open(path, &error);
  if (!image || !archive.open(*image, &error)) return false;
  get_system()->loader.attach(&archive);
  return true;
}

std::vector<u8> hex(const char* s) {
  std::vector<u8> out;
  for (; s[0] && s[1]; s += 2) out.push_back(static_cast<u8>(std::strtoul(std::string(s, 2).c_str(), nullptr, 16)));
  return out;
}

}  // namespace

TEST(save_field_icon_and_leftovers) {
  if (!disc_ready()) {
    std::printf("  (skipped: SS_DISC1 not set)\n");
    return;
  }
  std::vector<u8> block(0xA40, 0);
  const s32 progress = 38;
  const s16 count = 272;
  std::memcpy(block.data() + 0x220, &progress, 4);
  std::memcpy(block.data() + 0x220 + 0x24C, &count, 2);
  CHECK_EQ(menu::field_save_icon_index(block.data() + 0x220), 9);
  HandoffRegistry* reg = &get_system()->handoff;
  void* saved = handoff_get(reg, 9);
  handoff_clear(reg, 9);
  handoff_store(reg, block.data(), 9);
  std::vector<u8> file(0x2000, 0);
  const u8* icon = menu::field_save_prefill(file.data());
  handoff_clear(reg, 9);
  if (saved) handoff_store(reg, saved, 9);
  CHECK(icon != nullptr);
  if (icon == nullptr) return;
  const auto palette = hex("000084101c6b565290395a5f14479036eb2100030003891d");
  CHECK(std::memcmp(icon, palette.data(), palette.size()) == 0);
  const auto summary_tail = hex("debbbbbbbbbbabeddebabbbbbbbbabed");
  CHECK(std::memcmp(file.data() + 0x260, summary_tail.data(), summary_tail.size()) == 0);
  const auto frame_tail = hex("33c4edde342221122243edde4c22f22f");
  CHECK(std::memcmp(file.data() + 0xDD9, frame_tail.data(), frame_tail.size()) == 0);
}
