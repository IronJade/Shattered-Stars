#include "game/field/sprite_pack.h"

#include "game/field/map.h"

namespace ss::game::field {

const u8* sprite_pack_find_group(u32 id, u32 key) {
  const SpritePackSlot* slots = sprite_pack_slots();
  for (int s = 0; s < 16; ++s) {
    if (slots[s].data.empty()) continue;
    const u8* pack = slots[s].data.data();
    const u32 groups = read_u32(pack);
    for (u32 g = 0; g < groups; ++g) {
      const u8* group = pack + read_u32(pack + 4 + 4 * g);
      const u32 records = read_u32(group);
      for (u32 r = 0; r < records; ++r) {
        const u8* rec = group + 4 + r * kSpriteRecordSize;
        if (read_u32(rec) == key && read_u32(rec + 4) <= id && id <= read_u32(rec + 8)) return group;
      }
    }
  }
  return nullptr;
}

int sprite_pack_find_anim(Field*, int key, int id, u8** table, u8** anim) {
  const u8* group = sprite_pack_find_group(static_cast<u32>(id), static_cast<u32>(key));
  if (group == nullptr) return 0;
  const s32 records = static_cast<s32>(read_u32(group));
  u32 index = 0;
  s32 r = 0;
  for (; r < records; ++r) {
    const u8* rec = group + 4 + r * kSpriteRecordSize;
    if (static_cast<s32>(read_u32(rec)) != key) continue;
    const u8* ids = group + read_u32(rec + 0xc);
    index = 0;
    while (static_cast<s32>(index) < ids[0] && static_cast<u32>(id) != ids[1 + index]) ++index;
    if (index != ids[0]) break;
  }
  if (r == records) return 0;
  // The pack data is the slot's own unpacked copy: edits stick until the pack is freed.
  u8* anims = const_cast<u8*>(group + read_u32(group + 4 + r * kSpriteRecordSize + 0x10));
  *table = anims;
  *anim = anims + read_u32(anims + 4 + index * 4);
  return 1;
}

void sprite_anim_set_frame_bytes(Field* f, int key, int id, int count, const int* values) {
  u8 *table, *anim;
  if (sprite_pack_find_anim(f, key, id, &table, &anim) != 1) return;
  const int frames = read_u16(anim);
  if (frames < count) count = frames;
  for (int i = 0; i < count; ++i) anim[4 + i * 4 + 3] = static_cast<u8>(values[i]);
}

void sprite_anim_set_frame_byte(Field* f, int key, int id, int frame, u8 value) {
  u8 *table, *anim;
  if (sprite_pack_find_anim(f, key, id, &table, &anim) == 1) anim[4 + frame * 4 + 3] = value;
}

void sprite_anim_set_byte2(Field* f, int key, int id, u8 value) {
  u8 *table, *anim;
  if (sprite_pack_find_anim(f, key, id, &table, &anim) == 1) anim[2] = value;
}

void sprite_anim_set_loop_frame(Field* f, int key, int id, int frame) {
  u8 *table, *anim;
  if (sprite_pack_find_anim(f, key, id, &table, &anim) != 1) return;
  anim[3] = static_cast<u8>(frame == -1 ? anim[0] - 1 : frame);
}

}  // namespace ss::game::field
