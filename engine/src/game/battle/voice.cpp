#include "game/battle/voice.h"

#include <cstring>

#include "game/battle/battle.h"
#include "game/battle/loaders.h"
#include "game/battle/sprite.h"
#include "game/battle/unit.h"
#include "game/boot/rng.h"
#include "runtime/runtime.h"

namespace ss::game::battle {
namespace {

u16 ovl_u16(u32 address) {
  const u8* p = ovl1984_data(address);
  u16 v = 0;
  if (p != nullptr) std::memcpy(&v, p, 2);
  return v;
}

s16 slot_s16(const u8* slot, int off) {
  s16 v;
  std::memcpy(&v, slot + off, 2);
  return v;
}

bool member_down(int i) {
  BattleGlobals& g = bg();
  // (The console indexes g_party_units with the speaker byte unchecked; 0xff when nobody
  // stood reads past the array. The port takes that as a member that is down.)
  if (i < 0 || i > 3) return true;
  return unit_is_down(g.party_units[i]);
}

}  // namespace

void voice_pick_victory() {
  BattleGlobals& g = bg();
  // voice_stop_all 0x8007f76c: the voice slots are not ported.
  int standing = 0, present = 0;
  for (PartyUnit* u : g.party_units) {
    if (!unit_is_down(u)) ++standing;
    if (u != nullptr) ++present;
  }
  int down = standing - present;  // minus the fallen members
  int down2 = down * 2;
  if (0 < down) down = down2 = 0;
  int score = down2 + down + (g.fight_frames < 0x1C20 ? 9 : 7);
  if (g.fight_frames < 0xE10) ++score;
  if (g.fight_frames < 0x708) ++score;
  if (g.fight_frames < 900) ++score;
  score <<= 1;
  int tier;
  if (0x16 < score) {
    tier = 0;
  } else if (score >= 0x12) {
    tier = rng_between(0, 2);
  } else if (0xC < score) {
    tier = 1;
  } else if (score >= 8) {
    tier = rng_between(1, 3);
  } else {
    tier = 2;
  }
  g.victory_voice_tier = static_cast<u8>(tier);
  int first = -1;
  for (int attempt = 0; attempt < 100; ++attempt) {
    const int m = attempt == 0 ? g.victory_speaker : static_cast<int>(rng_range(4));
    if (member_down(m) || m == first) continue;
    const s16 id = slot_s16(g.party_slot[m], 0);
    u32 base = ovl_u16(0x8008158C + static_cast<u32>(id) * 4);
    const u32 counts = ovl_u16(0x8008158E + static_cast<u32>(id) * 4);
    const u32 hi = counts >> 8;
    u32 n = 0;
    if (tier == 0) {
      n = hi;
    } else if (tier == 1) {
      base += hi;
      n = (counts & 0xF0) >> 4;
    } else {
      base += hi + ((counts & 0xF0) >> 4);
      n = counts & 0xF;
    }
    PartyUnit* u = g.party_units[m];
    u->victory_voice = static_cast<s16>(rng_between(static_cast<s32>(base), static_cast<s32>(base + n)));
    // TODO(port): voice_preload_entry 0x8007f9bc (the clip itself).
    runtime::Runtime::get().log_once("battle-victory-voice", "battle: victory voice clips not ported (lines picked)");
    if (first != -1) return;
    first = m;
    if (g.single_victory_voice != 0) return;
  }
}

int voice_play_result(PartyUnit* u) {
  if (unit_is_down(u)) return -1;
  const s16 level = slot_s16(u->stats, 0x26);
  u32 lines = 1;
  if (5 < level) lines = 2;
  if (0xF < level) lines = 3;
  if (0x24 < level) lines = 4;
  if (0x3C < level) lines = 5;
  const s16 id = slot_s16(u->stats, 0);
  const u32 base = ovl_u16(0x800815BE + static_cast<u32>(id) * 2);
  const int entry = static_cast<int>(base + rng_range(lines));
  // TODO(port): voice_play_entry 0x8007f818 (entry, u, 0).
  runtime::Runtime::get().log_once("battle-result-voice", "battle: result voice clips not ported (lines picked)");
  return entry;
}

}  // namespace ss::game::battle
