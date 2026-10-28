#include "game/boot/rng.h"
#include "game/common/math.h"
#include "test.h"

using namespace ss;

TEST(rng_matches_original_code) {
  // Reference: the boot executable's rng_seed / rng_next run under a MIPS emulator.
  game::rng_seed(0x4e421714);
  const u32 first[] = {0x7b45a92b, 0x1cb723f2, 0xd18ebb53, 0x31d2779f, 0x6fb915c8, 0xcda20876};
  u64 h = 0xcbf29ce484222325ull;
  for (int i = 0; i < 200; ++i) {
    const u32 v = static_cast<u32>(game::rng_next());
    if (i < 6) CHECK(v == first[i]);
    for (int b = 0; b < 4; ++b) h = (h ^ (v >> (8 * b) & 0xFF)) * 0x100000001b3ull;
  }
  CHECK(h == 0x812420b7802a04afull);
}

TEST(sin_cos_table) {
  CHECK_EQ(game::sin4096(0), 0);
  CHECK_EQ(game::sin4096(1024), 4096);
  CHECK_EQ(game::sin4096(2048), 0);
  CHECK_EQ(game::sin4096(3072), -4096);
  CHECK_EQ(game::cos4096(0), 4096);
  CHECK_EQ(game::cos4096(2048), -4096);
  CHECK_EQ(game::sin4096(5), 31);  // table entry 5 is 31
  CHECK_EQ(game::cos4096(4096 + 1024), 0);
}
