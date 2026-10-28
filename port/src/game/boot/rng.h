#pragma once

#include "core/types.h"

// Knuth's subtractive generator (Numerical Recipes ran3) as the boot executable
// implements it. The 56-entry table's last slot is the stored seed (0x8002b544),
// so the seed takes part in every refill, exactly as on the console.
namespace ss::game {

void rng_seed(s32 seed);  // 0x8001029c
void rng_refill();        // 0x80010224
s32 rng_next();           // 0x8001034c
u32 rng_range(u32 n);     // 0x800104d4: rng_next() % n, or 0 when n is 0
// Debugging (the battle's SS_BATTLE_RAM start): the generator's state from a RAM dump
// (the table at 0x8002b468, 56 words, and the index at 0x8002b464).
void rng_restore(const u32 table[56], s32 index);
// The state as rng_restore takes it (the menu's item effects try an item on a copy of
// the generator and put it back: entry 3001 0x8008bb3c / 0x8008bb78).
void rng_save(u32 table[56], s32* index);
u32 rng_draws();  // debugging: rng_next calls so far (to line up with an emulator's count)

}  // namespace ss::game
