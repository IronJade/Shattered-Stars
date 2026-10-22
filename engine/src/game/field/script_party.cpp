// Party, character and inventory helpers the script VM calls (field 0x80068f10-
// 0x8006ab4c, 0x8006b59c; inventory 0x8003c594-0x8003c928; masks 0x80052268,
// 0x80061bd0, 0x80061c4c). Pure manipulation of the handoff blocks: party slots
// (handoff 3), character records (handoff 4), inventory (handoff 6), config
// (handoff 2). Each change of party slots 0-3 drops that slot's battle voices.
#include <cstring>

#include "game/boot/rng.h"
#include "game/boot/system.h"
#include "game/field/script_vm.h"
#include "game/field/vm_services.h"

namespace ss::game::field {
namespace {

constexpr int kSlotSize = 0x60;
constexpr int kRecordSize = 0xD0;

u8* slot_ptr(int slot) { return globals().party + slot * kSlotSize; }
u8* record_ptr(int slot) { return globals().records + slot * kRecordSize; }
s16 slot_id(int slot) { return vm_rd16(slot_ptr(slot)); }
void set_slot_id(int slot, int id) { vm_wr16(slot_ptr(slot), static_cast<u32>(id)); }

void invalidate_voice(int slot) {
  vm_svc_voice_slot_invalidate(slot);
  vm_svc_load_queue_set_streaming(0);
  vm_svc_set_voice_cache_state(0);
}

// The active or reserve slot of a character (record index), or 8.
int any_slot_of(int character) {
  int i = 0;
  for (; i < 8; ++i) {
    int id = slot_id(i);
    if (id < 0) id = -id;
    if (id == character + 1) break;
  }
  return i;
}

// The active slot of a character (ids compared signed, so reserves never match), or 8.
int active_slot_of(int character) {
  int i = 0;
  for (; i < 8; ++i) {
    if (slot_id(i) == character + 1) break;
  }
  return i;
}

}  // namespace

int party_find_slot(ScriptCtx*, int character) {
  const int i = active_slot_of(character);
  return i == 8 ? -1 : i;
}

void party_get_hp_status(ScriptCtx*, int character, s32* hp, u8* status) {
  const int i = any_slot_of(character);
  if (i > 7) {
    *hp = 0;
    *status = 0;
    return;
  }
  *hp = vm_rd32(slot_ptr(i) + 0x1C);
  *status = slot_ptr(i)[2];
}

void party_restore_hp_all(ScriptCtx*) {
  for (int i = 0; i < 8; ++i) {
    u8* p = slot_ptr(i);
    if (vm_rd16(p) > 0) {
      vm_wr32(p + 0x1C, static_cast<u32>(vm_rd32(p + 0x18)));
      p[2] &= 0xFE;
    }
  }
}

void party_restore_mp_all(ScriptCtx*) {
  for (int i = 0; i < 8; ++i) {
    u8* p = slot_ptr(i);
    if (vm_rd16(p) > 0) vm_wr16(p + 0x24, static_cast<u32>(vm_rd16(p + 0x22)));
  }
}

void party_add_member(ScriptCtx* ctx, int character) {
  if (party_find_slot(ctx, character) != -1) return;  // already active
  // A reserve member comes back in its slot.
  for (int i = 0; i < 8; ++i) {
    const int id = slot_id(i);
    if ((id < 0 ? -id : id) == character + 1) {
      if (id < 0) set_slot_id(i, -id);
      if (i < 4) invalidate_voice(i);
      return;
    }
  }
  // A new member takes the first slot that is not active: an empty one directly; a
  // reserve one after its occupant moves to the next empty slot.
  int i = 0;
  for (; i < 8; ++i) {
    const int id = slot_id(i);
    if (id == 0) {
      vm_svc_party_slot_init(i, character + 1);
      if (i < 4) invalidate_voice(i);
      return;
    }
    if (id < 0) {
      int j = i + 1;
      for (; j < 8; ++j) {
        if (slot_id(j) == 0) {
          party_swap_slots(ctx, i, j);
          vm_svc_party_slot_init(i, character + 1);
          return;
        }
      }
      i = 8;
      break;
    }
  }
  // No empty slot at all: the first reserve slot is overwritten.
  if (i == 8) {
    for (i = 0; i < 8; ++i) {
      if (slot_id(i) < 0) {
        vm_svc_party_slot_init(i, character + 1);
        if (i < 4) invalidate_voice(i);
        return;
      }
    }
  }
}

void party_remove_member(ScriptCtx* ctx, int character) {
  if (party_find_slot(ctx, character) == -1) return;
  for (int i = 0; i < 8; ++i) {
    if (slot_id(i) == character + 1) {
      set_slot_id(i, -slot_id(i));  // to the reserve, data kept
      if (i < 4) invalidate_voice(i);
      return;
    }
  }
}

void char_unequip_item(ScriptCtx*, int character, int item) {
  if (static_cast<s16>(item) < 0) return;
  const int i = active_slot_of(static_cast<s16>(character));
  if (i == 8) return;
  u8* rec = record_ptr(i);
  const s16 it = static_cast<s16>(item);
  u8* inv = globals().inventory;
  for (int off = 0xC; off <= 0x14; off += 2) {
    if (vm_rd16(rec + off) == it) {
      inventory_add(inv, it, 1, 1);
      vm_wr16(rec + off, 0);
    }
  }
  for (int k = 0; k < 2; ++k) {  // the two accessory slots
    if (vm_rd16(rec + 0x16 + 2 * k) == it) {
      inventory_add(inv, it, 1, 1);
      vm_wr16(rec + 0x16 + 2 * k, 0);
    }
  }
}

int char_set_equipment(ScriptCtx*, int character, int slot, int item) {
  const int i = active_slot_of(static_cast<s16>(character));
  if (i == 8) return 0;
  u8* rec = record_ptr(i);
  int old = 0;
  const u16 s = static_cast<u16>(slot);
  if (s <= 6) {
    old = vm_rd16(rec + 0xC + 2 * s);
    vm_wr16(rec + 0xC + 2 * s, static_cast<u32>(item));
  }
  u8* inv = globals().inventory;
  if (old != 0) inventory_add(inv, old, 1, 1);
  // The console passes the item id where inventory_remove_at wants an entry index, so
  // one is taken from whatever entry sits at that index (if it holds any).
  if (static_cast<s16>(item) != 0) inventory_remove_at(inv, static_cast<s16>(item), 1);
  return old;
}

int party_has_equipped(ScriptCtx*, int item) {
  for (int i = 0; i < 8; ++i) {
    if (static_cast<u32>(read_u16(slot_ptr(i)) - 1) >= 12) continue;
    const u8* rec = record_ptr(i);
    for (int off = 0xC; off <= 0x18; off += 2) {
      if (vm_rd16(rec + off) == static_cast<s16>(item)) return 1;
    }
  }
  return 0;
}

int char_get_equipment(ScriptCtx*, int character, int slot) {
  const int i = active_slot_of(static_cast<s16>(character));
  if (i == 8) return 0;
  const u16 s = static_cast<u16>(slot);
  return s <= 6 ? vm_rd16(record_ptr(i) + 0xC + 2 * s) : 0;
}

void party_isolate_hero(ScriptCtx* ctx) {
  // The active party is saved in field state +0x6c (slot << 4 | character id), and
  // everyone but the story hero (Claude with script flag 1, else Rena) moves to the
  // reserve; the hero ends in slot 0.
  FieldGlobals& g = globals();
  const int hero_id_minus_1 = (g.script_flags[0] >> 1 & 1) == 0 ? 1 : 0;
  bool invalidated = false;
  int count = 0;
  int hero = 0;
  int last = 0;
  for (int i = 0; i < 8; ++i) {
    const s16 id = slot_id(i);
    int keep = last;
    if (id != 0 && id > 0) {
      g.field_flags[0x6C + count] = static_cast<u8>(i << 4 | (id & 0xF));
      ++count;
      keep = i;
      if (id - 1 != hero_id_minus_1) {
        set_slot_id(i, -id);
        keep = last;
        if (i < 4) {
          vm_svc_voice_slot_invalidate(i);
          invalidated = true;
        }
      }
    }
    last = keep;
    hero = keep;
  }
  if (invalidated) {
    vm_svc_load_queue_set_streaming(0);
    vm_svc_set_voice_cache_state(0);
  }
  if (hero != 0) party_swap_slots(ctx, hero, 0);
  g.field_flags[0x6B] = static_cast<u8>(count);
}

void party_swap_slots(ScriptCtx*, int a, int b) {
  u8 slot[kSlotSize], rec[kRecordSize];
  std::memcpy(slot, slot_ptr(a), kSlotSize);
  std::memcpy(rec, record_ptr(a), kRecordSize);
  std::memmove(slot_ptr(a), slot_ptr(b), kSlotSize);
  std::memmove(record_ptr(a), record_ptr(b), kRecordSize);
  std::memcpy(slot_ptr(b), slot, kSlotSize);
  std::memcpy(record_ptr(b), rec, kRecordSize);
  if (static_cast<u32>(a) < 4) invalidate_voice(a);
  if (static_cast<u32>(b) < 4) invalidate_voice(b);
}

void party_restore_saved(ScriptCtx* ctx) {
  FieldGlobals& g = globals();
  bool invalidated = false;
  // Everyone saved by party_isolate_hero comes back from the reserve...
  for (int i = 0; i < 8; ++i) {
    if (slot_id(i) == 0) continue;
    for (int k = 0; k < g.field_flags[0x6B]; ++k) {
      const int id = slot_id(i);
      if ((id < 0 ? -id : id) == (g.field_flags[0x6C + k] & 0xF)) {
        if (id < 0) set_slot_id(i, -id);
        if (i < 4) {
          vm_svc_voice_slot_invalidate(i);
          invalidated = true;
        }
      }
    }
  }
  // ...and goes back to its saved slot.
  for (int i = 0; i < 8; ++i) {
    if (slot_id(i) != 0) {
      for (int k = 0; k < g.field_flags[0x6B]; ++k) {
        const int id = slot_id(i);
        const u8 saved = g.field_flags[0x6C + k];
        const int target = saved >> 4;
        if ((id < 0 ? -id : id) == (saved & 0xF)) {
          if (i != target) {
            party_swap_slots(ctx, i, target);
            i = target < i ? target - 1 : i - 1;  // rescan from the lower slot
          }
          break;
        }
      }
    }
  }
  if (invalidated) {
    vm_svc_load_queue_set_streaming(0);
    vm_svc_set_voice_cache_state(0);
  }
}

int relation_hash_mod(ScriptCtx*, int n) {
  const u8* config = globals().config;
  int sum = 0;
  for (int i = 0; i < 12 * 12; ++i) sum += config[0xE8 + i];
  for (int i = 0; i < 12 * 12; ++i) sum += config[0x58 + i];
  return n == 0 ? sum : sum % n;  // the console traps on n = 0
}

int party_count_members() {
  const u8* party = static_cast<const u8*>(handoff_get(globals().handoff, 3));
  int count = 0;
  for (int i = 0; i < 8; ++i) count += static_cast<u32>(read_u16(party + i * kSlotSize) - 1) < 12;
  return count;
}

u32 party_member_mask() {
  const u8* party = static_cast<const u8*>(handoff_get(globals().handoff, 3));
  u32 mask = 0;
  for (int i = 0; i < 8; ++i) {
    const u16 id = read_u16(party + i * kSlotSize);
    if (static_cast<u32>(id - 1) < 12) mask |= 1u << ((static_cast<s16>(id) - 1) & 0x1F);
  }
  return mask;
}

u32 party_member_mask_all() {
  u32 mask = party_member_mask();
  const u8* block = static_cast<const u8*>(handoff_get(globals().handoff, 9));
  // With script flag 2 the members saved by party_isolate_hero count too.
  if ((block[0x8C0] >> 2 & 1) != 0) {
    for (int i = 0; i < block[0x1EB]; ++i) {
      const u32 c = static_cast<u32>((block[0x1EC + i] & 0xF) - 1);
      if (c < 12) mask |= 1u << (c & 0x1F);
    }
  }
  return mask;
}

int inventory_add(u8* inv, int item, int count, int is_new) {
  int empty = -1;
  int i = 0;
  for (; i < 0x400; ++i) {
    const u16 e = read_u16(inv + 2 * i);
    if ((e >> 10 & 0x1F) == 0) {
      if (empty == -1) empty = i;
    } else if (static_cast<u32>(e & 0x3FF) == static_cast<u32>(item)) {
      break;
    }
  }
  u8 check;
  if (i == 0x400) {
    if (empty < 0) return 0;
    vm_wr16(inv + 2 * empty, static_cast<u32>(item | count << 10 | is_new << 15));
    check = inventory_check_byte(inv, static_cast<u32>(empty), 0xFFFF);
    i = empty;
  } else {
    // A tampered entry (check byte mismatch) is silently deleted.
    if (!inventory_verify(inv, static_cast<u32>(i))) {
      vm_wr16(inv + 2 * i, 0);
      return 1;
    }
    count += read_u16(inv + 2 * i) >> 10 & 0x1F;
    if (count > 20) return 0;
    vm_wr16(inv + 2 * i, static_cast<u32>(item | count * 0x400 | is_new << 15));
    check = inventory_check_byte(inv, static_cast<u32>(i), 0xFFFF);
  }
  inv[i + 0x824] = check;
  inventory_push_recent(inv, item);
  return 1;
}

void inventory_push_recent(u8* inv, int item) {
  if (inv[0xC24] != 0) {
    inv[0xC24] = 0;
    for (int k = 0; k < 16; ++k) vm_wr16(inv + 0x800 + 2 * k, 0);
  }
  int i = 0;
  for (; i < 16; ++i) {
    if (static_cast<u32>(item) == read_u16(inv + 0x800 + 2 * i)) break;
  }
  if (i == 16) i = 15;
  for (; i > 0; --i) vm_wr16(inv + 0x800 + 2 * i, read_u16(inv + 0x800 + 2 * (i - 1)));
  vm_wr16(inv + 0x800, static_cast<u32>(item));
}

int inventory_remove_at(u8* inv, int index, int count) {
  u8* e = inv + 2 * index;
  const int left = static_cast<int>(read_u16(e) >> 10 & 0x1F) - count;
  if (left < 0) return 0;
  if (left == 0) {
    vm_wr16(e, 0);
  } else {
    vm_wr16(e, static_cast<u32>((read_u16(e) & 0x83FF) | left * 0x400));
  }
  const u8 check = inventory_check_byte(inv, static_cast<u32>(index), 0xFFFF);
  inv[index + 0x824] = check;
  return check;
}

int inventory_find(const u8* inv, int item) {
  for (int i = 0; i < 0x400; ++i) {
    if (static_cast<u32>(read_u16(inv + 2 * i) & 0x3FF) == static_cast<u32>(item)) return i;
  }
  return -1;
}

u8 inventory_check_byte(const u8* inv, u32 index, u32 salt) {
  u32 v = read_u16(inv + 2 * index) & 0x7FFF;
  if (salt == 0xFFFF) salt = rng_range(16);
  v ^= v >> 8;
  return static_cast<u8>(((((~index & 0xF) ^ salt) << 4) | ((v & 0xF) ^ ((v & 0xFF) >> 4) ^ salt)) & 0xFF);
}

bool inventory_verify(const u8* inv, u32 index) {
  const u8 check = inv[index + 0x824];
  return check == inventory_check_byte(inv, index, (~index & 0xF) ^ (check >> 4));
}

}  // namespace ss::game::field
