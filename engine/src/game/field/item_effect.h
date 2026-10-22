#pragma once

#include "core/types.h"
#include "game/common/party.h"

// What an item does when it is used from the field's menus. The code is in archive entry
// 3001 (0x8008b9b0-0x8008ce7c, next to the item viewer) and runs on a 0x100-byte object
// on the caller's stack. It is a StatChange that runs the item's effect (item field 0x28,
// then a few item ids) on party slots:
// - A dry run (`kind`, `usable`) only records what would change. It works on a copy of
//   the random generator, so trying an item does not disturb the game's draws.
// - `apply` makes the changes.
namespace ss::game::field {

struct ItemEffect : StatChange {         // 0x100 bytes; ctor 0x8008b9b0
  s32 special = 0;      // +0x08: 1 the menu goes to page 0x13 with 'param', 2 a battle
                        // (game_state +0x264 = 0x227), 3 a teleport (session +0x35)
  s32 param = 0;        // +0x0c
  s16 item = 0;         // +0xf4
  s16 member = 0;       // +0xf6
  s16 sound = 0;        // +0xf8: 0x13 heal / status, 0x28 other, 0x16 a character's own item, 0x23 down
  u8 one = 0;           // +0xfa: targets one member
  u8 front = 0;         // +0xfb: the four from 'member' (the battle party)
  u8 all = 0;           // +0xfc: everyone
  u8 field = 0;         // +0xfd: no member (flags, songs, items)
  u8 closes = 0;        // +0xfe: ends the menu (special effects)

  // 0x8008ba00: what the item targets, from a dry run on slot 0: 5 closes the menu, 4 no
  // member, 3 everyone, 2 the front four, 1 one member, 0 nothing.
  int kind(int item_id);
  // 0x8008baa0: a dry run on 'slot': 4 money / special, 3 status, 2 MP, 1 HP, 0 nothing.
  int usable(int item_id, int slot);
  // 0x8008bbb4: uses the item on 'slot'; returns the sound to play.
  s16 apply(int item_id, int slot);

 private:
  u8& apply_flag() { return StatChange::apply; }    // +0 (hidden by apply())
  void run(int item_id, int slot);                  // 0x8008bc10
  void revive();                                    // 0x8008c6b0: clear status 1, HP 1
  void map_flag();                                  // 0x8008c6f8: the map's region flag 0x2cb + n
  void add_status(u8 bits);                         // 0x8008c77c
  void remove_status(u8 bits);                      // 0x8008c7d8
  void play_song();                                 // 0x8008c838: entry 0xa9b through Callways
  void random_items(int count);                     // 0x8008c8e0
  void character_flag(int character, int n);       // 0x8008c96c: record +0x3c + n
  void set_flag(int flag);                          // 0x8008ca08
  void skill_book(int skill);                       // 0x8008ca70
  void special_page(int value);                     // 0x8008cb34
  void config_mark(int table, int character, int n);  // 0x8008cbcc: config +0xe8 / +0x58
  void config_mark_selected(int table);             // 0x8008cb5c
  void heal(u32 what, int target, int amount);      // 0x8008cc54: 1 HP, 2 MP; target 1 one, 2 front, 3 all
  void own_item();                                  // 0x8008ce7c
};

// 0x8008b92c: whether a skill book for 'skill' may be read (its group's script flag 700 + n).
bool skill_book_allowed(int skill);
// 0x8008b530: the bonus a member gets when skill 'skill' reaches its (new) level:
// attributes, HP / MP, experience, money or items.
void skill_level_bonus(StatChange* sc, int skill);

}  // namespace ss::game::field
