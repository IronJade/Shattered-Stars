// Field character starting data (game/field/characters.cpp, generated from the
// decompilation) against the title program's hand-written port of the same code for
// Claude and Rena, and the names of all twelve. Needs SS_DISC1 for the program images.
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

#include "disc/archive.h"
#include "disc/disc_image.h"
#include "game/boot/rng.h"
#include "game/boot/system.h"
#include "game/field/characters.h"
#include "game/field/field.h"
#include "game/title/title_program.h"
#include "test.h"

using namespace ss;
using namespace ss::game;

namespace {

bool programs_available() {
  if (field::field_data(0x8007B6DC) != nullptr) return true;
  static std::unique_ptr<disc::DiscImage> image;
  static disc::Archive archive;
  static bool tried = false;
  if (tried) return false;
  tried = true;
  if (get_system()->loader.archive() == nullptr) {
    const char* path = std::getenv("SS_DISC1");
    if (path == nullptr || *path == 0) return false;
    std::string error;
    image = disc::DiscImage::open(path, &error);
    if (!image || !archive.open(*image, &error)) return false;
    boot_main(0, &archive);
  }
  return field::field_data(0x8007B6DC) != nullptr;
}

}  // namespace

TEST(field_character_init_matches_title_port) {
  if (!programs_available()) {
    std::printf("  (skipped: SS_DISC1 not set)\n");
    return;
  }
  for (int character = 1; character <= 2; ++character) {
    u8 slot_a[0x60], rec_a[0xD0], slot_b[0x60], rec_b[0xD0];
    rng_seed(0x1234 + character);
    party_slot_init(slot_a, rec_a, character);  // title 0x80061f80
    rng_seed(0x1234 + character);
    field::party_slot_init(slot_b, rec_b, character);  // field 0x8007a20c
    CHECK(std::memcmp(slot_a, slot_b, sizeof slot_a) == 0);
    CHECK(std::memcmp(rec_a, rec_b, sizeof rec_a) == 0);
  }
}

TEST(field_character_init_names_and_ids) {
  if (!programs_available()) {
    std::printf("  (skipped: SS_DISC1 not set)\n");
    return;
  }
  static const char* kNames[12] = {"Claude", "Rena", "Celine", "Bowman", "Dias", "Precis",
                                   "Ashton", "Leon", "Opera", "Ernest", "Noel", "Chisato"};
  for (int character = 1; character <= 12; ++character) {
    u8 slot[0x60], rec[0xD0];
    rng_seed(7);
    field::party_slot_init(slot, rec, character);
    u16 id;
    std::memcpy(&id, slot, 2);
    CHECK_EQ(id, character);
    CHECK(std::strcmp(reinterpret_cast<const char*>(rec + 0x24), kNames[character - 1]) == 0);
    u32 hp;
    std::memcpy(&hp, slot + 0x14, 4);
    CHECK(hp > 0);
  }
}
