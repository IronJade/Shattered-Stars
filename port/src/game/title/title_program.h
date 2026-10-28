#pragma once

#include <vector>

#include "core/types.h"
#include "game/boot/stubs.h"
#include "game/boot/system.h"

namespace ss::game {

// Title program (#1) globals: 0x8004ebdc-0x8004ec14.
struct TitleGlobals {
  System* system = nullptr;              // 0x8004ebdc
  Pad* pad = nullptr;                    // 0x8004ebf0 = System + 4
  Loader* loader = nullptr;              // 0x8004ebe4 = System + 0x10
  HandoffRegistry* handoff = nullptr;    // 0x8004ebe0 = System + 0x5a20
  Sound* sound = nullptr;                // 0x8004ebf8 = System + 0x5aa4
  MemoryCard* memcard = nullptr;         // 0x8004ec00
  u8* config = nullptr;                  // 0x8004ebfc: handoff slot 2
  std::vector<u8> entry12;               // 0x8004ec08: archive entry #12 (title screen art)
  std::vector<u8> new_game_data;         // 0x8004ec04: entries #14-#16, loaded for New Game
  std::vector<u8> voice_data;            // 0x8004ec0c: entry #13, loaded for Voice Collection
  std::vector<u8> load_screen_data;      // 0x8004ec10: entries #3024-#3026, loaded for Continue
  std::vector<u8> prologue_data;         // 0x8004ec14: 0x52800 bytes; the prologue's first part (#2680/#2681)
  bool battle_rank_unlocked = false;     // 0x8004ec28: set by the Voice Collection; adds the Battle Rank screen
  int sound_demo_state = 0;              // 0x8004eb18: new_game_sound_update's step (0xff = playing)
};
TitleGlobals& title_globals();

// Bytes of the original title program image (archive entry #1, loaded at
// 0x8002f810), for reading its constant tables at their original addresses.
const u8* title_data(u32 address);

void title_entry(System* sys);           // 0x8002f810
void title_program_main(System* sys);    // 0x80041db0
void title_init_config();                // 0x80042bcc
void create_game_state();                // 0x80061c58: handoff slots 2, 3, 4, 6
u8 inventory_check_byte(const u16* inventory, u32 index, u32 key);  // 0x8003b944
void party_slot_init(u8* slot, u8* record, int character);           // 0x80061f80

// boot 0x80011d38: whether disc 'disc' is inserted (its marker entry 5 + disc
// exists), or 2 when the table is not the game's.
int loader_is_disc(Loader& loader, int disc);

}  // namespace ss::game
