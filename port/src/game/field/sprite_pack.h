#pragma once

#include "core/types.h"

// Sprite packs (the 16 slots of 0x8007585c, filled by sprite_pack_register; format in
// docs/port/catalog/formats.md "Sprite pack"): groups of records, each record one sprite
// key (character / object id) for a range of animation ids.
//
//   pack:   u32 group_count; u32 group_off[]                 (from the pack start)
//   group:  u32 record_count; Record[record_count]           (offsets below from the group)
//   Record: u32 key; u32 id_lo; u32 id_hi; u32 ids_off; u32 anims_off; u32 cels_off; u32 frames_off
//   ids:    u8 count; u8 anim_id[count]                      (anim id -> index)
//   anims:  u32 count; u32 off[count]                        (from anims) -> AnimData
//   AnimData: u16 frame_count; u8 mode (1 ping-pong); u8 loop_frame; {s16 cel; u8; u8 duration}[]
//   cels:   +2 u8 mode (1 = 4-byte cels), +4/+8/+0xc part table offsets (0x1c-byte parts),
//           cel table from +0x14 (8 or 4 bytes: u8 frame, s8 x, s8 y, u8 flip, u8 part a/b/c)
//   frames: u32 palettes_off; u32 pixels_off; u32; frame definitions from +0x10 (+0xc in
//           mode 1) of 0xc bytes: u8 palette, u8 w, u8 h, u8, s16 x, s16 y, u32 pixel offset
namespace ss::game::field {

struct Field;

constexpr int kSpriteRecordSize = 0x1c;

// 0x80042e4c: the group holding a record with this key whose id range covers 'id'.
const u8* sprite_pack_find_group(u32 id, u32 key);
// 0x80052480: the animation 'id' of sprite 'key': its record's anims table and the
// AnimData. Returns 1 when found.
int sprite_pack_find_anim(Field* f, int key, int id, u8** table, u8** anim);
// Run-time edits of animation timing (script table B 0x19-0x22):
void sprite_anim_set_frame_bytes(Field* f, int key, int id, int count, const int* values);  // 0x800522d4: durations
void sprite_anim_set_frame_byte(Field* f, int key, int id, int frame, u8 value);            // 0x80052364: one duration
void sprite_anim_set_mode(Field* f, int key, int id, u8 value);                            // 0x800523c4: mode
void sprite_anim_set_loop_frame(Field* f, int key, int id, int frame);                      // 0x80052410 (-1 = last)

// 0x800525ac (script service): every sprite pack palette is backed up, then each colour
// becomes its luminance scaled by (r, g, b) (16.16 factors; the result is not clamped);
// field_flags +0x27 = 1. 0x80052838 copies the backups back (field_load_map does it while
// +0x27 is set).
void sprite_packs_tint(Field* f, int r, int g, int b);
void sprite_packs_restore_palettes(Field* f);

}  // namespace ss::game::field
