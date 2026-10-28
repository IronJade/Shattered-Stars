#pragma once

#include <cstddef>
#include <string>

#include "core/types.h"

// World-edit patch lines (port feature): the grammar of game/field/mods.h, shared by the
// game (which applies them), setup's Mods page and the mod catalog (which explain them).
// Four commands, each "COMMAND TARGET WHERE EXPECTED VALUE":
//   script MAP WORD EXPECTED VALUE      a map script's 32-bit word
//   item ITEM FIELD EXPECTED VALUE      a field of an item's 0x30-byte record
//   shop SHOP INDEX EXPECTED VALUE      an entry of a shop's stock list
//   enemy TYPE FIELD EXPECTED VALUE     a field of an enemy type's 0x5c-byte stats
// Numbers are decimal or 0x hex; FIELD is a name or OFFSET[:SIZE]; '#' starts a comment.
namespace ss::mods {

enum class Kind { kScript, kItem, kShop, kEnemy };

struct PatchLine {
  Kind kind = Kind::kScript;
  int target = 0;  // map, item, shop or enemy type
  u32 where = 0;   // script word, record byte offset, or stock index
  int size = 4;    // bytes compared and written (records)
  u32 expected = 0;
  u32 value = 0;
};

inline constexpr size_t kItemRecord = 0x30;
inline constexpr size_t kEnemyRecord = 0x5C;

// Cuts a '#' comment off 'line'; false when only blanks are left (nothing to parse).
bool strip_comment(std::string& line);

// One line (comment already stripped); false when it is not understood. Item and enemy
// values are masked to the field's size.
bool parse_patch_line(const std::string& line, PatchLine* out);

const char* kind_name(Kind kind);  // "script", "item", "shop", "enemy"
// The field's name ("price", "hp") for an item / enemy offset and size; nullptr if none.
const char* field_name(Kind kind, u32 offset, int size);
// Where a line writes, for people: "map 18 word 0x823", "item 21 price", "shop 1 entry 0",
// "enemy 4 hp", "item 21 +0x2c:1".
std::string describe_place(const PatchLine& p);
// A value for people: decimal for record fields, hex for script words and items in shops.
std::string describe_value(const PatchLine& p, u32 value);

u32 field_mask(int size);

// Two lines write the same place: the same command and target, and the same word, stock
// entry or record bytes. 'overlap' (may be null) is set when record bytes overlap without
// being the same field. Shop appends (EXPECTED 0) never share a place.
bool same_place(const PatchLine& a, const PatchLine& b, bool* overlap);

}  // namespace ss::mods
