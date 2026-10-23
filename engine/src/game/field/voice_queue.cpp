#include "game/field/voice_queue.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "game/boot/rng.h"
#include "game/boot/sound.h"
#include "game/field/field.h"
#include "runtime/libgpu.h"
#include "runtime/runtime.h"

namespace ss::game::field {
namespace {

constexpr int kQueue = 24;
s16 g_entries[kQueue] = {};   // 0x8007bfa0: archive entries to load (-1: copy a loaded bank)
u16 g_info[kQueue] = {};      // 0x8007bfd0: slot (low 4 bits), bank index (bits 4-7), copy source (high byte)
u8 g_reading = 0;             // 0x8007c000: g_read holds the head entry (or a copy is due)
u8 g_field_mode = 0;          // 0x8007c001: the queue holds field clips (voice_queue_build_field)
std::vector<u8> g_read;       // 0x8007c004: the entry being read
std::vector<u8> g_held;       // 0x8007c008: the entry the sound driver is still uploading

s16 rd16s(const u8* p) {
  s16 v;
  std::memcpy(&v, p, 2);
  return v;
}
u16 rd16(const u8* p) {
  u16 v;
  std::memcpy(&v, p, 2);
  return v;
}

Sound* sound() { return globals().sound; }
const u8* slot_of(int i) { return globals().party + i * 0x60; }
const u8* record_of(int i) { return globals().records + i * 0xD0; }

// Entries sorted by archive entry (copies, -1, first), info alongside.
// Debugging (SS_FIELD_CLIP_RNG=file, a Redux dump of the generator at the field's first
// voice_queue_build_field, 57 words: index, table): the port's boot path draws differently
// before the field, so its first clip picks differ from a Redux run's; this lines them up
// for a byte compare of the sound slots (sound_debug_dump_voice_slots).
void debug_first_clip_rng() {
  static bool done = false;
  if (done) return;
  done = true;
  const char* path = std::getenv("SS_FIELD_CLIP_RNG");
  if (path == nullptr || *path == 0) return;
  u32 w[57];
  FILE* in = std::fopen(path, "rb");
  if (in == nullptr) return;
  const size_t n = std::fread(w, 4, 57, in);
  std::fclose(in);
  if (n == 57) rng_restore(w + 1, static_cast<s32>(w[0]));
}

void sort_queue(int n) {
  for (int i = 0; i < n; ++i) {
    for (int j = i + 1; j < n; ++j) {
      if (g_entries[j] < g_entries[i]) {
        std::swap(g_entries[i], g_entries[j]);
        std::swap(g_info[i], g_info[j]);
      }
    }
  }
}

// 0x80079b3c: per character, a 0-terminated list of clip bank sizes.
const u8* voice_field_clip_table(int character) {
  static const u32 kTables[12] = {0x8007B444, 0x8007B478, 0x8007B498, 0x8007B4B8, 0x8007B4F0, 0x8007B530,
                                  0x8007B56C, 0x8007B5A4, 0x8007B5C8, 0x8007B604, 0x8007B638, 0x8007B654};
  if (character < 1 || character > 12) return nullptr;
  return field_data(kTables[character - 1]);
}

}  // namespace

int voice_killer_bank(const u8* slot, const u8* /*record*/, int killer) {
  if (killer == 0) return 0;
  auto table = [killer](u32 base) { return static_cast<int>(rd16s(field_data(base + static_cast<u32>(killer) * 2))); };
  switch (rd16(slot) & 0x7F) {
    case 1: return table(0x8007BF26);   // 0x80079d10
    case 4: return table(0x8007BF3E);   // 0x80079d28
    case 5: return table(0x8007BF52);   // 0x80079d40
    case 6:                             // 0x80079d58
      switch (killer) {
        case 1: return 0x84;
        case 2: return 0x8B;
        case 3: return rng_range(2) != 0 ? 0x82 : 0x83;
        case 4: return 0x8D;
        case 5: return 0x8C;
        case 6: return 0x87;
        case 7: return 0x89;
        case 8: return rng_range(2) != 0 ? 0x85 : 0x86;
        case 9: return 0x88;
        case 10: return 0x8A;
        default: return 0;
      }
    case 7: return table(0x8007BF66);   // 0x80079e10
    case 9: return table(0x8007BF7A);   // 0x80079e28
    case 10: return table(0x8007BF8E);  // 0x80079e40
    case 0xC:                           // 0x80079e58
      switch (killer) {
        case 1: return 0xF9;
        case 2: return 0xFC;
        case 3: return 0xFB;
        case 4: return 0xF6;
        case 5: return 0xF7;
        case 6: return 0xF8;
        case 7: return static_cast<int>(rng_range(3)) + 0xFD;
        case 8: return 0xFA;
        default: return 0;
      }
    default: return 0;  // 2, 3, 8, 11 and anything else
  }
}

int voice_party_spu_needed() {
  int need = 0;
  bool any = false;
  for (int i = 0; i < 4; ++i) {
    const u8* slot = slot_of(i);
    const u8* rec = record_of(i);
    if (sound_bank_loaded(sound(), i) != 0 || rd16s(slot) <= 0) continue;
    if (slot[3] < 9) {
      for (int k = 0; k < 4; ++k) {
        const u8 move = rec[0xCC + k];
        if (move == 0) continue;
        any = false;
        for (int j = 0; j < k; ++j) {
          if (rec[0xCC + j] == move) {
            any = true;
            break;
          }
        }
        if (!any) need += 0x78;  // a bank shared by two moves counts once
      }
      need += 0xE2;
    } else {
      need += 0x15A;  // casters: one bank by class
    }
    any = true;
  }
  if (!any) need += 0xE2;
  return need;
}

void voice_queue_reset() {
  g_held.clear();
  g_read.clear();
  g_field_mode = 0;
  for (s16& e : g_entries) e = 0;
}

bool voice_queue_pending() { return g_entries[0] != 0; }
bool voice_queue_busy() { return g_entries[0] != 0 || !g_held.empty(); }

int voice_queue_build_battle(int budget) {
  g_reading = 0;
  g_field_mode = 0;
  if (budget < voice_party_spu_needed()) return -1;
  int n = 0;
  for (int i = 0; i < 4; ++i) {
    const u8* slot = slot_of(i);
    const u8* rec = record_of(i);
    if (sound_bank_loaded(sound(), i) != 0 || rd16s(slot) <= 0) continue;
    sound_bank_slot_clear(sound(), i);
    if (slot[3] < 9) {
      for (int k = 0; k < 4; ++k) {
        const u8 move = rec[0xCC + k];
        if (move == 0 || rd16s(slot) == 0) continue;
        const int bank = voice_killer_bank(slot, rec, move);
        if (bank == 0) continue;
        int dup = -1;
        for (int j = 0; j < n; ++j) {
          if (g_entries[j] == bank) {
            dup = j;
            break;
          }
        }
        g_info[n] = static_cast<u16>(i + k * 0x10);
        if (dup < 0) {
          g_entries[n] = static_cast<s16>(bank);
          budget -= 0x78;
        } else {
          g_entries[n] = -1;
          g_info[n] = static_cast<u16>(g_info[n] | g_info[dup] << 8);
        }
        ++n;
      }
    } else {
      g_entries[n] = rd16s(field_data(0x8007BF0E + static_cast<u32>(slot[3]) * 2));
      g_info[n] = static_cast<u16>(i);
      budget -= 0x78;
      ++n;
    }
  }
  sort_queue(n);
  return budget;
}

int voice_queue_build_field(int budget) {
  debug_first_clip_rng();
  g_reading = 0;
  g_field_mode = 1;
  if (voice_queue_pending()) return -1;
  int members = 0;
  for (int i = 0; i < 4; ++i)
    if (rd16s(slot_of(i)) > 0) ++members;
  int want[4];
  int chosen = 0, cost = 0;
  for (int i = 0; i < 4; ++i) {
    if (sound_bank_loaded(sound(), i) == 0 && rd16s(slot_of(i)) > 0) {
      want[i] = 1;
      ++chosen;
      cost += 0xE2;
    } else {
      want[i] = 0;
    }
  }
  // Everyone's banks are in: one random member gets a clip anyway.
  bool over = budget < cost;
  if (cost == 0) {
    cost = 0xE2;
    over = budget < 0xE2;
    for (int k = 0; k < 100; ++k) {
      const u32 r = rng_range(4);
      if (rd16s(slot_of(static_cast<int>(r))) > 0) {
        want[r] = 1;
        ++chosen;
        break;
      }
    }
  }
  if (over) return -1;
  budget -= cost;
  // More random members while the budget allows.
  while (budget >= 0xE3 && chosen < members) {
    budget -= 0xE2;
    for (int k = 0; k < 100; ++k) {
      const u32 r = rng_range(4);
      if (rd16s(slot_of(static_cast<int>(r))) > 0) {
        want[r] = 1;
        ++chosen;
        break;
      }
    }
  }
  // One clip bank per wanted member, sized to the SPU space its slot has left.
  int n = 0;
  for (int i = 0; i < 4; ++i) {
    if (want[i] == 0) continue;
    const int space = sound_bank_free_space(sound(), i);
    int character = rd16s(slot_of(i));
    if (character < 0) character = -character;
    const u8* table = voice_field_clip_table(character);
    int lo = space - 0x2800;
    if (table == nullptr) continue;
    if (lo > 0x7000) lo = 0x7000;
    s16 candidates[32];
    int count = 0;
    for (int pass = 0; pass < 2; ++pass) {
      count = 0;
      for (int idx = 0; rd16(table + idx * 2) != 0; ++idx) {
        const int size = rd16(table + idx * 2);
        if (size <= space && lo <= size && count < 32) candidates[count++] = static_cast<s16>(idx);
      }
      lo = 0;
      if (count > 2) break;
    }
    if (count == 0) continue;
    const int pick = static_cast<int>(rng_range(static_cast<u32>(count)));
    character = rd16s(slot_of(i));
    if (character < 0) character = -character;
    const s16 base = rd16s(field_data(0x8007B692 + static_cast<u32>(character) * 2));
    if (base != 0) {
      g_entries[n] = static_cast<s16>(base + candidates[pick]);
      g_info[n] = static_cast<u16>(i);
      ++n;
    }
  }
  sort_queue(n);
  return budget;
}

void voice_slot_invalidate(int slot) { sound_set_bank_loaded(sound(), slot, 0); }

int voice_queue_update() {
  Sound* s = sound();
  Loader* loader = globals().loader;
  if (!g_held.empty() && sound_bank_busy(s) != 1) g_held.clear();
  if (loader->poll() != 0) return 1;
  if (g_reading != 0) {
    if (sound_bank_busy(s) == 1) return 1;
    const u16 info = g_info[0];
    if (g_entries[0] == -1) {
      // A bank another member already brought: copy its effect numbers.
      sound_copy_remap(s, info & 0xF, (info & 0xF0) >> 3, info >> 8);
      sound_copy_remap(s, info & 0xF, ((info & 0xF0) >> 3) + 1, (info >> 8) + 1);
    } else if (g_field_mode == 0) {
      sound_add_effect_bank(s, g_read.data(), 0, 0, info & 0xF, (info >> 4) << 1);
    } else {
      sound_load_effect_bank(s, g_read.data(), 0, 0, info, 0);
    }
    char line[64];
    std::snprintf(line, sizeof line, "voice bank %d to slot %d (info %04x)", g_entries[0], info & 0xF, info);
    runtime::Runtime::get().trace(line);
    g_held = std::move(g_read);  // the driver uploads from it
    g_read.clear();
    g_reading = 0;
    for (int i = 0; i < kQueue - 1; ++i) {
      g_entries[i] = g_entries[i + 1];
      g_info[i] = g_info[i + 1];
    }
    g_entries[kQueue - 1] = 0;
  }
  if (g_entries[0] == 0) {
    if (g_field_mode == 1) {
      // ORIGINAL BUG (original_bugs.md F18): the console tests party slot 0's character
      // four times (its pointer is never advanced), so every slot, a member or not, is
      // marked loaded when slot 0 has one:
      //   for (i = 0; i < 4; ++i) if (*(s16*)party > 0) sound_set_bank_loaded(s, i, 1);
      for (int i = 0; i < 4; ++i)
        if (rd16s(slot_of(i)) > 0) sound_set_bank_loaded(s, i, 1);
    }
    return 1;
  }
  g_read.clear();
  if (g_entries[0] == -1) {
    g_reading = 1;
    return 1;
  }
  g_read.assign(static_cast<size_t>(loader->entry_size(g_entries[0])), 0);
  g_reading = 1;
  loader->select_entry(g_entries[0]);
  loader->start_read(g_read.data(), 0);
  return 1;
}

void voice_queue_abort() {
  Loader* loader = globals().loader;
  if (g_reading != 0 && loader != nullptr && loader->poll() != 0) loader->cancel();
  g_read.clear();
  // The console spins on the SPU transfer (an interrupt finishes it); the host's
  // driver finishes transfers as they are issued.
  while (sound() != nullptr && sound_bank_busy(sound()) != 0) psyq::VSync(0);
  g_held.clear();
  g_reading = 0;
}

}  // namespace ss::game::field
