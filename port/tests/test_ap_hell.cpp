// Archipelago, the hell seed (so2.apworld 0.2.1; docs/apworld/seed_format.md `hell_seed`; docs/handoff/
// hellseed.md) [hellseed]: encounter_rate 0 with killer-move checks or learn points. The seed waives
// the Fun City arena's fee with an ordinary patch line (map 297's word 0x177f, `push 0x7d0` ->
// `push 0`) and says `hell_seed: true`, which the port only shows: setup's Archipelago page and the
// game's log carry kHellSeedWarning. Every other seed has neither. No disc needed.

#include <string>

#include "game/ap/ap.h"
#include "game/ap/seed.h"
#include "mods/patch.h"
#include "test.h"

using namespace ss;
using namespace ss::game;

namespace {

const char* kFeeLine = "script 297 0x177f 0x120007d0 0x12000000";

// A small v2 seed: one killer-move check; OPTIONS and PATCHES spliced in.
std::string hell_test_seed(const std::string& options, const std::string& patches) {
  return R"seed({"format": "apso2", "version": 2, "seed": "hellseed-test", "game": "Star Ocean: The Second Story",
 "slot": {"name": "H", "id": 1},
 "options": )seed" + options + R"seed(,
 "items": {"5000021": {"name": "Revival Card", "kind": "item", "game_id": "0x15"}},
 "locations": [
   {"id": 1350574592, "name": "Ashton: Dragon Breath level 1",
    "check": {"type": "killer_move", "character": 5, "move": 6, "level": 1, "uses": 140},
    "item": {"player": 1, "kind": "item", "game_id": 21, "name": "Revival Card"}}],
 "patches": )seed" + patches + "}";
}

}  // namespace

TEST(ap_hell_seed) {
  ap::Seed s;
  std::string error;
  // A hell seed: the option and the fee's line.
  const std::string hell = hell_test_seed(
      R"({"encounter_rate": 0, "killer_move_checks": true, "skillsanity": "none", "hell_seed": true})",
      std::string("[\"") + kFeeLine + "\"]");
  const bool loaded = ap::load_seed_text(hell, &s, &error);
  if (!loaded) std::fprintf(stderr, "  %s\n", error.c_str());
  CHECK(loaded);
  CHECK(s.hell_seed && s.encounter_rate == 0);
  CHECK(s.patches.size() == 1 && s.patches[0] == kFeeLine);
  // The line is an ordinary script patch (mods/patch.h): map 297's word 0x177f, 2000 Fol -> 0.
  mods::PatchLine p;
  CHECK(mods::parse_patch_line(s.patches[0], &p));
  CHECK(p.kind == mods::Kind::kScript && p.target == 297 && p.where == 0x177f);
  CHECK(p.expected == 0x120007d0u && p.value == 0x12000000u);
  CHECK((p.expected & 0xffff) == 2000 && (p.value & 0xffff) == 0);
  // The game takes it with the seed's other lines.
  ap::start_with(s);
  CHECK(ap::seed() != nullptr && ap::seed()->hell_seed);
  bool fee = false;
  for (const std::string& l : ap::seed_patch_lines()) fee |= l == kFeeLine;
  CHECK(fee);
  ap::stop();
  // The warning setup shows and the game logs.
  const std::string warning = ap::kHellSeedWarning;
  CHECK(warning.rfind("Hell Seed: no random battles.", 0) == 0);
  CHECK(warning.find("Fun City arena") != std::string::npos && warning.find("free on this seed") != std::string::npos);
  // Every other seed: no key (absent, the apworld's way) or false: not a hell seed.
  CHECK(ap::load_seed_text(hell_test_seed(R"({"encounter_rate": 0})", "[]"), &s, &error) && !s.hell_seed);
  CHECK(ap::load_seed_text(hell_test_seed(R"({"encounter_rate": 100, "hell_seed": false})", "[]"), &s, &error) &&
        !s.hell_seed);
}
