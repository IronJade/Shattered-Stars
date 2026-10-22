#include "game/field/sprite_pack.h"

#include <algorithm>
#include <vector>

#include "game/field/field.h"
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

namespace {

std::vector<std::vector<u8>> g_saved_palettes;  // the extra arena's copies (0x80075290 +0x104)

u32 rd32le(const u8* p) { return static_cast<u32>(p[0] | p[1] << 8 | p[2] << 16 | p[3] << 24); }

// Every record's palette block (u16 count | 0x8000 flag, u16, then the colours) in the
// order the console walks them: slots, groups, records.
template <typename Fn>
void for_each_palette(Fn&& fn) {
  SpritePackSlot* slots = sprite_pack_slots();
  for (int s = 0; s < 16; ++s) {
    u8* pack = slots[s].data.empty() ? nullptr : slots[s].data.data();
    if (pack == nullptr) continue;
    const u32 groups = rd32le(pack);
    for (u32 gi = 0; gi < groups; ++gi) {
      u8* group = pack + rd32le(pack + 4 + gi * 4);
      const u32 records = rd32le(group);
      for (u32 r = 0; r < records; ++r) {
        u8* frames = group + rd32le(group + 0x1C + r * kSpriteRecordSize);
        u8* block = frames + rd32le(frames);
        const u32 count = static_cast<u32>(block[0] | block[1] << 8) & 0x7FFF;
        fn(block + 4, count);
      }
    }
  }
}

}  // namespace

void sprite_packs_tint(Field*, int r, int g, int b) {
  globals().field_flags[0x27] = 1;
  g_saved_palettes.clear();
  for_each_palette([&](u8* colours, u32 count) {
    g_saved_palettes.emplace_back(colours, colours + count * 2);
    for (u32 i = 0; i < count; ++i) {
      const u32 c = static_cast<u32>(colours[i * 2] | colours[i * 2 + 1] << 8);
      const s32 lum = static_cast<s32>((c & 0x1F) * 0x4D00 + (c >> 5 & 0x1F) * 0x9700 + (c >> 10 & 0x1F) * 0x1C00) >> 8;
      const u16 v = static_cast<u16>((c & 0x8000) + static_cast<s16>(static_cast<u32>(lum * b) >> 16) * 0x400 +
                                     static_cast<s16>(static_cast<u32>(lum * g) >> 16) * 0x20 +
                                     static_cast<s16>(static_cast<u32>(lum * r) >> 16));
      colours[i * 2] = static_cast<u8>(v);
      colours[i * 2 + 1] = static_cast<u8>(v >> 8);
    }
  });
}

void sprite_packs_restore_palettes(Field*) {
  size_t k = 0;
  for_each_palette([&](u8* colours, u32 count) {
    if (k < g_saved_palettes.size() && g_saved_palettes[k].size() == count * 2)
      std::copy(g_saved_palettes[k].begin(), g_saved_palettes[k].end(), colours);
    ++k;
  });
}

}  // namespace ss::game::field
