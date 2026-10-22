#pragma once

#include "core/types.h"
#include "game/battle/battle.h"
#include "game/battle/stage.h"

// The handlers of battle_run's load list (jump table 0x80078f38), by list index:
// 0 load_battle_music_pack 0x80072190, 1 load_victory_music 0x8007221c, 2 load_stage
// 0x80072270, 3 load_ui_sprites 0x80072f70, 4 load_battle_text 0x80074fd0, 5
// load_result_code 0x800752c0, 6 load_item_data 0x800750b8, 7 load_formation 0x8007344c,
// 8 load_effect_sprites 0x800731f4, 9 load_battle_code 0x80075214, 10/11
// load_intro_voice 0x8007536c. docs/notes/battle.md §4.2.
namespace ss::game::battle {

void battle_load_entry(int index, s32 entry, StageLoadCtx* ctx, Bytes data);
void battle_setup_sprite_banks();          // 0x800747a0
bool inventory_verify_all(const u8* inv);  // 0x80069600

// The field's files for its return (the battle's copy of the field's map_resolve_entries,
// reading the field state block from handoff 9): out[0] music [1] 0xc26 [2] container
// [3]/[4] party sprite packs [5] code overlay [6..] the restore list, 0-terminated,
// [0x10] count. map -1 takes the map, music and position from the state block.
void battle_map_resolve_entries(s16 out[0x11], int map, u32 x, u32 z);  // 0x8006a11c
u32 battle_party_member_mask();      // 0x8006a464: the party slots' characters (handoff 3)
u32 battle_party_member_mask_all();  // 0x8006a4e0: plus the state block's extra members
// Before the result screen: reads as many of g_preload_list's entries as fit into one
// blob (g_handoff10_blob, handed back in handoff 10), negating the kept ones.
void battle_queue_field_preloads();  // 0x800480d8

// The code overlays' data tables (their code is native): #1984 block 0 (0x800715d0 ..
// 0x80081728) and #1981 (0x80085518 .. 0x80088054) as the load list read them; null
// outside the images. (battle_data reads #2129, which these overlays cover at run time.)
const u8* ovl1984_data(u32 address);
const u8* ovl1981_data(u32 address);

}  // namespace ss::game::battle
