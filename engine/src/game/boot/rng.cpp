#include "game/boot/rng.h"

#include <cstdio>
#include <cstdlib>

namespace ss::game {
namespace {

u32 g_table[56];   // 0x8002b468; [55] is the seed variable 0x8002b544
s32 g_index = 0;   // 0x8002b464
u32 g_draws = 0;   // (port debugging only)
// Debugging (world agent): SS_RNG_SITES=LO:HI prints the call site of draws LO..HI (the
// rng_range caller when it went through rng_range), for addr2line on the binary.
const void* g_site = nullptr;
struct SiteWindow {
  u32 lo = 1, hi = 0;
  SiteWindow() {
    if (const char* e = std::getenv("SS_RNG_SITES")) std::sscanf(e, "%u:%u", &lo, &hi);
  }
};
const SiteWindow g_window;

}  // namespace

void rng_seed(s32 seed) {
  u32 value = static_cast<u32>(seed);
  u32 prev = 1;
  int slot = 0x15;
  g_table[55] = value;
  for (int i = 1; i < 0x37; ++i) {
    const int idx = slot % 0x37;
    g_table[idx] = prev;
    prev = value - prev;
    value = g_table[idx];
    slot += 0x15;
  }
  rng_refill();
  rng_refill();
  rng_refill();
  g_index = 0x37;
}

void rng_refill() {
  for (int i = 1; i < 25; ++i) g_table[i] -= g_table[i + 31];
  for (int i = 25; i < 56; ++i) g_table[i] -= g_table[i - 24];
}

u32 rng_draws() { return g_draws; }

s32 rng_next() {
  ++g_draws;
  if (g_window.lo <= g_draws && g_draws <= g_window.hi) {
    std::fprintf(stderr, "[rngc] %u %p\n", g_draws, g_site != nullptr ? g_site : __builtin_return_address(0));
  }
  g_site = nullptr;
  if (++g_index > 0x37) {
    rng_refill();
    g_index = 1;
  }
  return static_cast<s32>(g_table[g_index]);
}

void rng_restore(const u32 table[56], s32 index) {
  for (int i = 0; i < 56; ++i) g_table[i] = table[i];
  g_index = index;
}

void rng_save(u32 table[56], s32* index) {
  for (int i = 0; i < 56; ++i) table[i] = g_table[i];
  *index = g_index;
}

[[gnu::noinline]] u32 rng_range(u32 n) {
  g_site = __builtin_return_address(0);
  if (n == 0) {
    g_site = nullptr;
    return 0;
  }
  return static_cast<u32>(rng_next()) % n;
}

}  // namespace ss::game
