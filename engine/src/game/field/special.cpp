#include "game/field/special.h"

#include <cstring>
#include <utility>

#include "game/common/program_image.h"
#include "game/field/bg.h"
#include "game/field/callways.h"
#include "game/field/voice_queue.h"
#include "game/field/characters.h"
#include "game/field/field.h"
#include "game/field/load_queue.h"
#include "game/field/field_sub_overlays.h"
#include "game/field/map.h"
#include "game/field/script_vm.h"
#include "runtime/runtime.h"

namespace ss::game::field {
namespace {

constexpr int kFieldOverlayEntry = 0xC1D;  // #3101 at 0x8007e000
const u8* ovl_data(u32 address) { return program_data_at(kFieldOverlayEntry, 0x8007E000, address); }

s16 rd16s(const u8* p) {
  s16 v;
  std::memcpy(&v, p, 2);
  return v;
}
s32 rd32(const u8* p) {
  s32 v;
  std::memcpy(&v, p, 4);
  return v;
}
void put16(u8* p, int offset, u32 v) {
  const u16 x = static_cast<u16>(v);
  std::memcpy(p + offset, &x, 2);
}
void put32(u8* p, int offset, u32 v) { std::memcpy(p + offset, &v, 4); }

void todo(const char* what) {
  runtime::Runtime::get().log_once(std::string("special:") + what, std::string("field: not ported yet: ") + what);
}

}  // namespace

void party_move_character_to_slot(int character, int slot) {
  FieldGlobals& g = globals();
  int from = 0;
  for (; from < 8; ++from) {
    int id = rd16s(g.party + from * 0x60);
    if (id < 0) id = -id;
    if (id == character + 1) break;
  }
  if (from == 8 || from == slot) return;
  u8 tmp[0xD0];
  std::memcpy(tmp, g.party + slot * 0x60, 0x60);
  std::memcpy(g.party + slot * 0x60, g.party + from * 0x60, 0x60);
  std::memcpy(g.party + from * 0x60, tmp, 0x60);
  std::memcpy(tmp, g.records + slot * 0xD0, 0xD0);
  std::memcpy(g.records + slot * 0xD0, g.records + from * 0xD0, 0xD0);
  std::memcpy(g.records + from * 0xD0, tmp, 0xD0);
  // An active member changed: the battle voice clips must be rebuilt.
  if (slot < 4) voice_slot_invalidate(slot);
  if (from < 4) voice_slot_invalidate(from);
  if (slot < 4 || from < 4) {
    load_queue_set_streaming(load_queue(), false);
    voice_stream().state = 0;
  }
}

void field_special_command(ScriptCtx* ctx, int op) {
  FieldGlobals& g = globals();
  u8* gs = g.game_state;
  switch (op) {
    case 0:
      // 0x800752cc = 0x801f9800(new 0xd4): the constructor of the map's 0x801f9800
      // overlay. Only map 143 uses it, with #3104 (section 10): the rainbow.
      if (field_map_sub_sections() & (1u << 10)) {
        rainbow_create();
      } else {
        todo("special 0 without #3104 at 0x801f9800");
      }
      break;
    case 1:
    case 2: {
      // Claude's record name: "????" (1) or the name entered at New Game (2).
      for (int i = 0; i < 8; ++i) {
        int id = rd16s(g.party + i * 0x60);
        if (id < 0) id = -id;
        if (id != 1) continue;
        const char* name =
            op == 1 ? reinterpret_cast<const char*>(ovl_data(0x8008EC40)) : reinterpret_cast<const char*>(gs + 0x28);
        if (name != nullptr) std::strcpy(reinterpret_cast<char*>(g.records + i * 0xD0 + 0x24), name);
      }
      if (op == 1) {
        party_move_character_to_slot(0, 0);
      } else {
        party_move_character_to_slot(1, 0);
        party_move_character_to_slot(0, 1);
      }
      break;
    }
    case 3:
    case 4: {
      // Every BG-animation frame gets the colour (3) or blend mode (4) in field_flags
      // +0x38 / +0x3c / +0x40.
      MapData& map = map_data();
      if (map.scene.empty()) break;
      u8* scene = map.scene.data();
      const int count = static_cast<u16>(rd16s(scene + 0x64));
      u8* anim = scene + rd32(scene + 0x88);
      u8* rects = scene + rd32(scene + 0x78);
      for (int i = 0; i < count; ++i, anim += 0xA0) {
        for (int k = 0; k < anim[4]; ++k) {
          u8* frame = anim + k * 0xC;
          if (op == 3) {
            frame[0x14] = static_cast<u8>(rd32(g.field_flags + 0x38));
            frame[0x15] = static_cast<u8>(rd32(g.field_flags + 0x3C));
            frame[0x16] = static_cast<u8>(rd32(g.field_flags + 0x40));
          } else {
            u8* rect = rects + static_cast<u16>(rd16s(frame + 0x10)) * 8;
            rect[4] = static_cast<u8>((rect[4] & 0xF8) | static_cast<u8>(rd32(g.field_flags + 0x38)));
          }
        }
      }
      break;
    }
    case 5: {
      // The twelve characters' default names (8 bytes each, 0x14 apart).
      static const u32 kNames[12] = {0x8008EC48, 0x8008EC50, 0x8008EC58, 0x8008EC60, 0x8008EC68, 0x8008EC70,
                                     0x8008EC78, 0x8008EC80, 0x8008EC88, 0x8008EC90, 0x8008EC98, 0x8008ECA0};
      for (int i = 0; i < 12; ++i)
        if (const u8* name = ovl_data(kNames[i])) std::memcpy(gs + 0x28 + i * 0x14, name, 8);
      break;
    }
    case 6: g.field_flags[0x80] = 1; break;
    case 7: g.config[0x4C] = 1; break;
    case 8: bg_globals().windows_on = 0; break;
    case 9: bg_globals().windows_on = 1; break;
    case 0xB: g.field_flags[0x82] = 1; break;
    case 0xC: {
      // Dias (party_find_slot's 4 = character id 5) with fixed stats.
      const int slot = party_find_slot(ctx, 4);
      if (slot == -1) break;
      u8* s = g.party + slot * 0x60;
      put32(s, 0x14, 0x9C4);
      put32(s, 0x1C, 0x9C4);
      put32(s, 0x18, 0x9C4);
      put16(s, 0x20, 0xFA);
      put16(s, 0x24, 0xFA);
      put16(s, 0x22, 0xFA);
      put16(s, 0x2A, 0x1CC);
      put16(s, 0x30, 300);
      put16(s, 0x36, 200);
      put16(s, 0x3C, 200);
      put16(s, 0x4E, 0x10);
      put16(s, 0x54, 8);
      put16(s, 0x48, 100);
      put16(s, 0x46, 0);
      s[2] = 0;
      put16(s, 0x28, 0x23);
      put32(s, 0x10, 0x17AD6);
      break;
    }
    case 0xD: {
      // Dias's slot gets his starting data again (init_dias 0x8007a840 alone) and the
      // name in game_state +0x78.
      const int slot = party_find_slot(ctx, 4);
      if (slot == -1) break;
      character_init_data(g.party + slot * 0x60, g.records + slot * 0xD0, 5);
      std::strcpy(reinterpret_cast<char*>(g.records + slot * 0xD0 + 0x24), reinterpret_cast<const char*>(gs + 0x78));
      break;
    }
    case 0xE: g.sub_overlay_hook = 1; break;
    case 0xF: g.sub_overlay_hook = 0; break;
    case 0x12:
      for (int i = 0; i < 8; ++i)
        if (rd16s(g.party + i * 0x60) > 0 && g.records[i * 0xD0 + 0x8A] == 0) g.records[i * 0xD0 + 0x8A] = 1;
      break;
    case 0x13:
      if (callways_song_active(callways()) != 0) callways_stop_song(callways(), true);
      break;
    default:
      break;
  }
}

}  // namespace ss::game::field
