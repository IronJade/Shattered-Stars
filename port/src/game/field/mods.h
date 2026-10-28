#pragma once

#include <vector>

#include "core/types.h"

// World edits (port feature, in the spirit of Severed Chains' mods): patch files change
// map scripts and game data as they load — chest contents, door targets, item records,
// shop stock, enemy stats — without touching the discs.
//
// Patch files live in the mods directory (Runtime::mods_dir: `mods/` next to config.ini, or
// SS_MODS_DIR): loose `*.patch` files (applied in name order) and mod packages (folders
// with a mod.json; mods/catalog.h, docs/port/notes/mods.md), in the load order config.ini's
// mod_order sets (Runtime::mod_order, or SS_MOD_ORDER). With no packages and no
// mod_order, exactly the loose files apply, in name order. When two mods change the same
// place, the later one in the load order wins (the earlier line is left out and logged).
// The grammar (parsed by mods/patch.h) is one patch per line; numbers in decimal or 0x
// hex, `#` starts a comment:
//
//   script MAP WORD EXPECTED VALUE
//       map MAP's script word WORD (the offset tools/so2_script.py prints, from 0x1c,
//       divided by 4)
//   item ITEM FIELD EXPECTED VALUE
//       item ITEM's 0x30-byte record (archive entry #3002; the menus, the battle and the
//       field's map copy all get it). FIELD is a name, or OFFSET[:SIZE] (SIZE 1, 2 or 4
//       bytes; by default 4 at +0, 2 at +0x04..+0x13, else 1): price (+0x00), atk
//       (+0x08), def (+0x10), amount (+0x19, effect amount), effect (+0x28, kind), type
//       (+0x2a), sell (+0x2d, sell price %).
//   shop SHOP INDEX EXPECTED VALUE
//       entry INDEX of shop SHOP's stock (overlay 0xbaa's lists): item EXPECTED becomes
//       VALUE (0 removes it); INDEX = the list's length with EXPECTED 0 appends VALUE
//   enemy TYPE FIELD EXPECTED VALUE
//       the 0x5c-byte stats record of every enemy of type TYPE as a battle loads it.
//       FIELD is a name, or OFFSET[:SIZE] (by default 4 bytes below +0x10, 2 at
//       +0x10..+0x29 and +0x3a, else 1): hp (+0x00), mp, exp, fol (+0x0c), speed
//       (+0x16), drop1 / drop2 (+0x18 / +0x1a, items), atk (+0x1c), def, int, level
//       (+0x24), eva, acc (+0x28), guts (+0x36), drop1_odds / drop2_odds (+0x37 / +0x38).
//
// A value is replaced only if the data still holds EXPECTED, so a patch written for
// other data is skipped (and logged) instead of corrupting it. Applied patches are
// logged once each. tools/so2_mod.py writes these lines, reading EXPECTED from the disc.
namespace ss::game::field {

// Applies every patch for 'map' to its freshly unpacked script (section 1 block 0).
// Returns the number of words changed.
int mods_apply_script(int map, std::vector<u8>& script);

// Item records (0x30 bytes, item 1 first) as they are unpacked; returns the changes made.
int mods_apply_items(u8* records, size_t bytes);
inline int mods_apply_items(std::vector<u8>& records) { return mods_apply_items(records.data(), records.size()); }

// A shop's stock: the zero-terminated u16 list 'list' with the patches applied, as a
// new zero-terminated list (the original one when nothing applies).
std::vector<u8> mods_shop_stock(int shop, const u8* list);

// An enemy's stats record (0x5c bytes) as the battle reads it: a patched copy, valid
// until the next call, or 'record' itself when no patch is for 'type'.
const u8* mods_enemy_stats(int type, const u8* record);

// Counts of loaded patches by kind (script, item, shop, enemy), for the log / tests.
struct ModsSummary {
  int script = 0, item = 0, shop = 0, enemy = 0;
};
ModsSummary mods_summary();

}  // namespace ss::game::field
