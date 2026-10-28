#include "game/battle/party_talk.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "game/battle/battle.h"
#include "game/battle/bsfx.h"
#include "game/battle/loaders.h"
#include "game/battle/unit.h"
#include "game/battle/voice_player.h"
#include "game/boot/rng.h"
#include "game/boot/sound.h"
#include "game/boot/system.h"
#include "runtime/runtime.h"

namespace ss::game::battle {
namespace {

constexpr u32 kNoClip = 0xFFFFFFFFu;

// g_member_tables (0x8006fb7c: mem_alloc(0x150), 0x54 bytes per party record): 14 groups
// of three u16 voice entries, one group per character the member speaks about (Claude,
// Claude alt, Rena, Rena alt, Celine .. Chisato), three bond tiers each.
std::array<std::array<u8, 0x54>, 4> g_member_tables{};

// The opening lines (0x8006fb31 state, cleared by battle_init_state; 0x8006fb34 ticks;
// 0x8006fb88 the first clip, 0 in the freshly loaded #2129 image).
struct Opening {
  u8 state = 0;
  u8 ticks = 0;
  u32 first = 0;
} g_opening;

// Debugging: SS_TALK_TRACE=1 logs each call with the vblank and the rng draws so far, to
// line up with explore.lua's BW_HIT_BP on the same addresses.
void trace(const char* what, const Unit* u, u32 value) {
  static const bool on = std::getenv("SS_TALK_TRACE") != nullptr;
  if (!on) return;
  int slot = -1;
  for (int i = 0; i < 4; ++i)
    if (u != nullptr && bg().party_units[static_cast<size_t>(i)] == u) slot = i;
  std::fprintf(stderr, "[talk] vblank %llu rng %u %s p%d %08x\n",
               static_cast<unsigned long long>(runtime::Runtime::get().vblanks()), rng_draws(), what, slot, value);
}

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

// battle_frame_director: the speaker in party slot 'slot' says sound 'sfx' (bank << 24)
// unless down; true when it spoke (its handle in *handle).
bool opening_line(int slot, int sfx, u32* handle) {
  BattleGlobals& g = bg();
  PartyUnit* u = (slot >= 0 && slot < 4) ? g.party_units[static_cast<size_t>(slot)] : nullptr;
  // (The console indexes g_party_units with the speaker byte unchecked; the port takes an
  // out-of-range slot as a member that is down, as voice.cpp does.)
  if (unit_is_down(u)) return false;
  *handle = bsfx_play_at(sfx, u);  // 0x8007f3ac
  trace(sfx == 0x5000000 ? "op1" : "op2", u, *handle);
  pc_start_talk(u, *handle);
  return true;
}

}  // namespace

void talk_trace(const char* what, const Unit* u, u32 value) { trace(what, u, value); }

void pc_start_talk(PartyUnit* u, u32 handle) {
  trace("talk", u, handle);
  u->busy = 1;
  u->talk_voice = static_cast<s32>(handle);
}

void pc_start_fury(PartyUnit* u, u32 handle) {
  trace("fury", u, handle);
  u->action_kind = 0;
  u->begin_action();  // vtable [12]
  u->busy = 1;
  u->talk_voice = static_cast<s32>(handle);
  u->stats[2] |= 0x10;
  u->fury_ticks = 0x4B0;
  // (Debugging: +0x124 action, +0x12a action kind, the status byte and +0x201 busy, then
  // +0x22c, to compare with the unit in a Redux dump at 0x80080b40.)
  trace("fury_state", u, static_cast<u32>(u->action) << 24 | static_cast<u32>(u->action_kind) << 16 |
                             static_cast<u32>(u->stats[2]) << 8 | u->busy);
  trace("fury_ticks", u, static_cast<u32>(u->fury_ticks));
}

void pc_talk_update(PartyUnit* u) {
  if (u->talk_voice != 0 && !sound_handle_active(static_cast<u32>(u->talk_voice))) {
    trace("talkend", u, static_cast<u32>(u->talk_voice));
    u->talk_voice = 0;
    u->busy = 0;
  }
}

void party_talk_init() {
  BattleGlobals& g = bg();
  g_opening = Opening{};
  for (auto& t : g_member_tables) t.fill(0);
  // (SS_TALK_TRACE: the sound driver's party slots as the battle starts, loaded flag and SPU
  // bytes used, as mvref.lua's "banks".)
  if (g.sound != nullptr)
    for (int i = 0; i < 4; ++i)
      trace("bank", nullptr, static_cast<u32>(i) << 28 | static_cast<u32>(sound_bank_loaded(g.sound, i)) << 24 |
                                 static_cast<u32>(0xF400 - sound_bank_free_space(g.sound, i)));
  // (SS_TALK_SOUND_DUMP=path: the same slots byte for byte, for a Redux dump of the driver.)
  if (const char* path = std::getenv("SS_TALK_SOUND_DUMP"); path != nullptr && *path != 0 && g.sound != nullptr)
    sound_debug_dump_voice_slots(g.sound, path);
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
  static const bool off = std::getenv("SS_TALK_OFF") != nullptr;  // debugging: the port without the opening lines
  // Debugging: SS_TALK_FLEE=F makes the party flee at battle frame F (escape flag; each
  // standing member's flee timer 0x3c), as a Redux poke of 0x8006f6cf and +0x2dc does.
  static const int flee_at = [] {
    const char* v = std::getenv("SS_TALK_FLEE");
    return v != nullptr ? std::atoi(v) : -1;
  }();
  static bool fled = false;
  if (g.battle_frames < 2) fled = false;
  if (flee_at >= 0 && g.battle_frames >= flee_at && !fled) {
    fled = true;
    g.party_escaping = 1;
    for (PartyUnit* m : g.party_units)
      if (!unit_is_down(m)) m->flee_timer = 0x3C;
  }
  if (off) return;
  switch (g_opening.state) {
    case 0:
      if (g.battle_frames < 0x82) return;
      break;
    case 1:
      if (g.intro_voice_state & 1) opening_line(g.intro_speakers[0], 0x5000000, &g_opening.first);
      g_opening.ticks = 0;
      break;
    case 2:
      g_opening.ticks = static_cast<u8>(g_opening.ticks + g.frame_ticks);
      if (g_opening.ticks < 0x3D) return;
      break;
    case 3:
      if (sound_handle_active(g_opening.first)) return;
      if (g.intro_voice_state & 2) {
        u32 second = 0;
        opening_line(g.intro_speakers[1], 0x6000000, &second);
      }
      break;
    default:
      return;
  }
  ++g_opening.state;
}

void voice_play_extra(Unit* u) {
  trace("extra", u, 0);
  if (unit_is_down(u)) return;
  const s16 character = character_of(u->stats);
  const u32 n = rng_range(2);
  voice_play_entry(static_cast<int>(entry_for(0x800815D8u, character) + n), u, -1);
}

s32 voice_play_battle_line(PartyUnit* u) {
  trace("line", u, 0);
  if (unit_is_down(u)) return -1;
  const s16 character = character_of(u->stats);
  const u32 n = rng_range(4);
  const u32 h = voice_play_entry(static_cast<int>(entry_for(0x800815F0u, character) + n), u, 0);
  trace("line_ret", u, h);
  if (h != kNoClip) pc_start_talk(u, h);
  return static_cast<s32>(h);
}

void voice_ally_reaction(PartyUnit* u) {
  trace("ally", u, 0);
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
