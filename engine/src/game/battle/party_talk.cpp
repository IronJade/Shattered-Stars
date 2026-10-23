#include "game/battle/party_talk.h"

#include <array>
#include <cstring>

#include "game/battle/battle.h"
#include "game/battle/bsfx.h"
#include "game/battle/loaders.h"
#include "game/battle/unit.h"
#include "game/battle/voice_player.h"
#include "game/boot/rng.h"
#include "game/boot/system.h"

namespace ss::game::battle {
namespace {

constexpr u32 kNoClip = 0xFFFFFFFFu;

// g_member_tables (0x8006fb7c: mem_alloc(0x150), 0x54 bytes per party record): 14 groups
// of three u16 voice entries, one group per character the member speaks about (Claude,
// Claude alt, Rena, Rena alt, Celine .. Chisato), three bond tiers each.
std::array<std::array<u8, 0x54>, 4> g_member_tables{};

// The opening lines (0x8006fb31 state, 0x8006fb34 ticks, 0x8006fb88 the first clip).
struct Opening {
  u8 state = 0;
  u8 ticks = 0;
  u32 first = 0;
} g_opening;

u16 rd16(const u8* p) {
  u16 v = 0;
  std::memcpy(&v, p, 2);
  return v;
}

s16 character_of(const u8* slot) { return slot != nullptr ? static_cast<s16>(rd16(slot)) : 0; }

// A table of u16 voice entries in #1984.0, one per character (1-12).
u16 entry_for(u32 table, s16 character) {
  const u8* p = ovl1984_data(table + static_cast<u32>(character - 1) * 2);
  return p != nullptr ? rd16(p) : 0;
}

// 0x80074980 party_banter_voice_table: the #2129 table 0x800799d4 + 0x54 * (character - 1);
// null for characters outside 1-12.
const u8* party_banter_voice_table(s16 character) {
  if (static_cast<u16>(character - 1) >= 12) return nullptr;
  return battle_data(0x800799D4u + static_cast<u32>(character - 1) * 0x54);
}

// battle_frame_director: the speaker in party slot 'slot' says sound 'sfx' (bank << 24).
u32 opening_line(int slot, int sfx) {
  BattleGlobals& g = bg();
  PartyUnit* u = (slot >= 0 && slot < 4) ? g.party_units[static_cast<size_t>(slot)] : nullptr;
  // (The console indexes g_party_units with the speaker byte unchecked; the port takes an
  // out-of-range slot as a member that is down, as voice.cpp does.)
  if (unit_is_down(u)) return kNoClip;
  const u32 h = bsfx_play_at(sfx, u);  // 0x8007f3ac
  pc_start_talk(u, h);
  return h;
}

}  // namespace

void pc_start_talk(PartyUnit* u, u32 handle) {
  u->busy = 1;
  u->talk_voice = static_cast<s32>(handle);
}

void pc_start_fury(PartyUnit* u, u32 handle) {
  u->action_kind = 0;
  u->begin_action();  // vtable [12]
  u->busy = 1;
  u->talk_voice = static_cast<s32>(handle);
  u->stats[2] |= 0x10;
  u->fury_ticks = 0x4B0;
}

void pc_talk_update(PartyUnit* u) {
  if (u->talk_voice != 0 && !sound_handle_active(static_cast<u32>(u->talk_voice))) {
    u->talk_voice = 0;
    u->busy = 0;
  }
}

void party_talk_init() {
  BattleGlobals& g = bg();
  g_opening = Opening{};
  for (auto& t : g_member_tables) t.fill(0);
  const auto* party = static_cast<const u8*>(handoff_get(g.handoff, 3));
  if (party == nullptr) return;
  for (int i = 0; i < 4; ++i) {
    const s16 character = character_of(party + i * 0x60);
    if (character < 1) continue;
    // (A character outside 1-12 has no row: the console copies 0x54 bytes from address 0.)
    if (const u8* row = party_banter_voice_table(character)) std::memcpy(g_member_tables[static_cast<size_t>(i)].data(), row, 0x54);
  }
}

void opening_voices_update() {
  BattleGlobals& g = bg();
  switch (g_opening.state) {
    case 0:
      if (g.battle_frames < 0x82) return;
      break;
    case 1:
      if (g.intro_voice_state & 1) g_opening.first = opening_line(g.intro_speakers[0], 0x5000000);
      g_opening.ticks = 0;
      break;
    case 2:
      g_opening.ticks = static_cast<u8>(g_opening.ticks + g.frame_ticks);
      if (g_opening.ticks < 0x3D) return;
      break;
    case 3:
      if (sound_handle_active(g_opening.first)) return;
      if (g.intro_voice_state & 2) opening_line(g.intro_speakers[1], 0x6000000);
      break;
    default:
      return;
  }
  ++g_opening.state;
}

void voice_play_extra(Unit* u) {
  if (unit_is_down(u)) return;
  const s16 character = character_of(u->stats);
  const u32 n = rng_range(2);
  voice_play_entry(static_cast<int>(entry_for(0x800815D8u, character) + n), u, -1);
}

s32 voice_play_battle_line(PartyUnit* u) {
  if (unit_is_down(u)) return -1;
  const s16 character = character_of(u->stats);
  const u32 n = rng_range(4);
  const u32 h = voice_play_entry(static_cast<int>(entry_for(0x800815F0u, character) + n), u, 0);
  if (h != kNoClip) pc_start_talk(u, h);
  return static_cast<s32>(h);
}

void voice_ally_reaction(PartyUnit* u) {
  BattleGlobals& g = bg();
  const s16 fallen = character_of(u->stats);
  for (int i = 0; i < 4; ++i) {
    PartyUnit* m = g.party_units[static_cast<size_t>(i)];
    if (unit_is_down(m) || m == u || m->action >= 2) continue;
    // The bond of member i toward the fallen one: config +0x4b and +0xdb, 12 x 12 tables
    // by character (the first counts twice).
    const s16 speaker = character_of(g.party_slot[static_cast<size_t>(i)]);
    const u8* bond = g.config + speaker * 0xC + fallen;
    const int near = bond[0x4B];
    const int sum = near * 2 + bond[0xDB];
    if (sum < 8) continue;
    if (static_cast<u32>(sum * 0x24E) < static_cast<u32>(rng_next() & 0x7FFF)) continue;  // rng_next15
    // The group for the fallen character: Claude and Rena each have two (config +0x42 /
    // +0x43 pick the second), then one per character from Celine; the line by the bond.
    int off;
    if (fallen == 1) {
      off = g.config[0x42] != 0 ? 6 : 0;
    } else if (fallen == 2) {
      off = 0xC + (g.config[0x43] != 0 ? 6 : 0);
    } else {
      off = fallen * 6 + 6;
    }
    if (near >= 7) off += 2;
    if (near >= 10) off += 2;
    const u16 entry = rd16(g_member_tables[static_cast<size_t>(i)].data() + off);
    // (The clip plays at the fallen member, u, not at the speaker.)
    const u32 h = voice_play_entry(entry, u, -1);
    if (h == kNoClip) continue;
    pc_start_fury(m, h);
    return;
  }
}

}  // namespace ss::game::battle
