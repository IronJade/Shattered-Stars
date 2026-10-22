#pragma once

#include <array>
#include <cstring>
#include <vector>

#include "core/types.h"

// The field → battle setup (handoff 5, field 0x800752b8; built by
// battle_build_load_list 0x800788b0 and field_prepare_encounter 0x80050f64, handed over
// by field_exit_to_program 0x80051c9c, read by battle_run 0x8007160c).
//
// The console's block is 0xc0 bytes:
//   +0x00 s32 encounter        +0x04 s32 stage
//   +0x08 s32 entry[13]        load-list entries in load order (= +0x3c after sorting)
//   +0x3c s32 id[13]           archive entries; negative = not preloaded (load it then)
//   +0x70 s32 count            entries in the list (9, or 11 / 12 with intro voices)
//   +0x74 u32 data[13]         address of each preloaded entry's data (0 = not preloaded)
//   +0xa8 u8  owned[13]        the battle frees that buffer when done with it
//   +0xb8 s32 voice_member[2]  intro voice speakers (party slots, -1 none)
// The host keeps the block byte for byte except +0x74, whose addresses can't hold host
// pointers: the preloaded data travels in 'preloaded' (same index) instead.
namespace ss::game {

struct BattleSetup {
  std::array<u8, 0xC0> raw{};
  std::array<std::vector<u8>, 13> preloaded{};  // +0x74: empty = not preloaded

  s32 get(int offset) const {
    s32 v;
    std::memcpy(&v, raw.data() + offset, 4);
    return v;
  }
  void set(int offset, s32 v) { std::memcpy(raw.data() + offset, &v, 4); }

  s32 encounter() const { return get(0x00); }
  s32 stage() const { return get(0x04); }
  s32 entry(int i) const { return get(0x08 + 4 * i); }
  s32 id(int i) const { return get(0x3C + 4 * i); }
  s32 count() const { return get(0x70); }
  bool owned(int i) const { return raw[0xA8 + i] != 0; }
  s32 voice_member(int n) const { return get(0xB8 + 4 * n); }
};

}  // namespace ss::game
