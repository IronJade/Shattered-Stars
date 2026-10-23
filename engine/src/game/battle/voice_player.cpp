// The battle's voice player. See voice_player.h.

#include "game/battle/voice_player.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

#include "game/battle/battle.h"
#include "game/battle/bload.h"
#include "game/battle/bsfx.h"
#include "game/battle/enemy_support.h"
#include "game/boot/loader.h"
#include "game/boot/sound.h"
#include "runtime/runtime.h"

namespace ss::game::battle {
namespace {

// The two slots: entry 0x8006fb6c (s16), buffer 0x8006fb5c, speaker 0x8006fb64, the clip
// byte 0x8006fb74, the clip's sound-test number 0x8006fb70, flags 0x8006fb78 (1 loaded,
// 2 play requested, 4 playing).
struct VoiceSlot {
  s16 entry = 0;
  std::vector<u8> buffer;
  Unit* speaker = nullptr;
  u8 clip_byte = 0;
  s16 clip_number = 0;
  u8 flags = 0;
};
VoiceSlot g_slots[2];

u32 handle_of(int slot, int entry) { return static_cast<u32>(0x40 + slot) << 24 | static_cast<u32>(entry); }

// mem_alloc_b of the entry's size (0: no such entry, the allocation fails).
bool alloc_buffer(VoiceSlot& v, int entry) {
  BattleGlobals& g = bg();
  const int bytes = g.loader != nullptr ? g.loader->entry_size(entry) : 0;
  if (bytes <= 0) return false;
  v.buffer.assign(static_cast<size_t>(bytes), 0);
  return true;
}

void free_slot(VoiceSlot& v) {
  v.buffer.clear();
  v.buffer.shrink_to_fit();
  v.entry = 0;
  v.flags = 0;
}

// sound_effect_number 0x8001a5a0: the sound-test number of the effect in the slot while it
// plays, else 0.
s16 effect_number(int slot) {
  BattleGlobals& g = bg();
  if (g.sound == nullptr || slot < 1 || 8 < slot) return 0;
  const SoundEffect& e = g.sound->effects[slot - 1];
  return e.script != nullptr ? e.header_low : 0;
}

}  // namespace

u32 voice_play_entry(int entry, Unit* speaker, int tag) {
  for (int i = 0; i < 2; ++i) {
    VoiceSlot& v = g_slots[i];
    if (v.entry == entry && (v.flags & 2) == 0) {
      v.flags |= 2;
      v.speaker = speaker;
      return handle_of(i, entry);
    }
  }
  if (bload_busy_except_group(4) != 0 && tag == -1) return 0xFFFFFFFFu;
  for (int i = 0; i < 2; ++i) {
    VoiceSlot& v = g_slots[i];
    if (v.entry != 0) continue;
    if (!alloc_buffer(v, entry)) return 0xFFFFFFFFu;
    v.entry = static_cast<s16>(entry);
    bload_request(entry, v.buffer.data(), 4, tag);
    v.flags = 2;
    v.speaker = speaker;
    return handle_of(i, entry);
  }
  return 0xFFFFFFFFu;
}

u32 voice_preload_entry(int entry) {
  for (int i = 0; i < 2; ++i) {
    VoiceSlot& v = g_slots[i];
    if (v.entry != 0) continue;
    if (!alloc_buffer(v, entry)) return 0xFFFFFFFFu;
    v.entry = static_cast<s16>(entry);
    v.clip_byte = 0;
    bload_request(entry, v.buffer.data(), 4, -1);
    v.flags = 0;
    return handle_of(i, entry);
  }
  return 0xFFFFFFFFu;
}

void voice_stop_all() {
  for (VoiceSlot& v : g_slots) {
    if (v.entry == 0) continue;
    bload_cancel(v.entry, nullptr);
    // (The console hands voice_release the clip byte, not a handle: see voice_update.)
    sound_handle_release(v.clip_byte);
    free_slot(v);
  }
  bload_cancel_group(4);
}

void voice_update() {
  BattleGlobals& g = bg();
  // SS_VOICE_TRACE: each slot's entry and flags whenever they change.
  static const bool trace = std::getenv("SS_VOICE_TRACE") != nullptr;
  static s16 last_entry[2] = {};
  static u8 last_flags[2] = {};
  if (trace) {
    for (int i = 0; i < 2; ++i) {
      if (g_slots[i].entry == last_entry[i] && g_slots[i].flags == last_flags[i]) continue;
      std::fprintf(stderr, "[voice] vblank %llu slot %d entry %d flags %d\n",
                   static_cast<unsigned long long>(runtime::Runtime::get().vblanks()), i, g_slots[i].entry,
                   g_slots[i].flags);
      last_entry[i] = g_slots[i].entry;
      last_flags[i] = g_slots[i].flags;
    }
  }
  for (VoiceSlot& v : g_slots) {
    if (v.entry == 0 || v.flags == 0) continue;
    if (bload_done(v.entry)) v.flags |= 1;
    if ((v.flags & 2) != 0 && (v.flags & 1) != 0) {
      // bsfx_play(buffer, speaker, 1): the clip, its handle's low bits the buffer's address.
      const u32 h = bsfx_play(0, v.buffer.data(), v.speaker, 1);
      if (h != 0xFFFFFFFFu) {
        // The console keeps the handle's low byte (the buffer address's) where sound_handle_
        // active later reads a sound slot, so a voice counts as playing only while its entry
        // stays in the slot; (port) the address bits are 0 here, with the same result.
        v.clip_byte = static_cast<u8>(h);
        v.clip_number = effect_number(static_cast<int>(h >> 16) & 0xFF);
        v.flags = static_cast<u8>((v.flags & 0xFD) | 4);
      }
    }
    if ((v.flags & 4) != 0 && g.sound != nullptr && sound_bank_busy(g.sound) != 1) {
      v.flags &= 0xFB;
      free_slot(v);
    }
  }
}

bool voice_handle_active(u32 handle) {
  const int slot = static_cast<int>(handle >> 24) - 0x40;
  if (slot < 0 || 1 < slot) return false;
  const VoiceSlot& v = g_slots[slot];
  if (v.entry == static_cast<s32>(handle & 0xFFFFFF)) return true;
  BattleGlobals& g = bg();
  const int s = v.clip_byte;
  if (g.sound == nullptr || s < 1 || 8 < s) return false;  // (the console reads past the 8 slots)
  return sound_effect_playing(g.sound, s) != 0 && effect_number(s) == v.clip_number;
}

void voice_player_reset() {
  for (VoiceSlot& v : g_slots) {
    free_slot(v);
    v.speaker = nullptr;
    v.clip_byte = 0;
    v.clip_number = 0;
  }
}

}  // namespace ss::game::battle
