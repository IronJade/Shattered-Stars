#include "game/battle/loaders.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "disc/slz.h"
#include "game/battle/bobj.h"
#include "game/battle/window.h"
#include "game/battle/unit.h"
#include "game/battle/vram.h"
#include "game/field/mods.h"
#include "game/title/title_program.h"
#include "runtime/libgpu.h"
#include "runtime/runtime.h"

namespace ss::game::battle {
namespace {

u32 rd32(const u8* p) {
  u32 v;
  std::memcpy(&v, p, 4);
  return v;
}
u16 rd16(const u8* p) {
  u16 v;
  std::memcpy(&v, p, 2);
  return v;
}

Bytes unpack(const Bytes& data, int block) {
  auto out = disc::slz::unpack_block(data, 0, block, nullptr);
  return out ? std::move(*out) : Bytes{};
}

// The kept blocks of the sprite banks, with the pointers the setup reads.
struct UiBank {
  Bytes block;
  const u8* layout = nullptr;
  const u8* anims = nullptr;
  const u8* frames = nullptr;
  const u8* sheet = nullptr;
};
UiBank g_ui0, g_ui1;     // 0x8006f8d4.. / 0x8006f8e8..
Bytes g_intro_voice[2];  // the two intro voice banks (SPU slots 4 / 5)

// The sheet image (u16 w, u16 h, pixels) of a sprite sheet to (x, y); the tpage id.
u16 upload_sheet(const Bytes& b, const u8* sheet, int x, int y) {
  const size_t img = static_cast<size_t>(sheet - b.data()) + rd32(sheet + 4);
  if (img + 4 > b.size()) return 0;
  return upload_texture(b, img + 4, 0, 0, x, y, rd16(b.data() + img), rd16(b.data() + img + 2));
}

// The sheet's CLUT (at sheet + sheet[0] + 4) to row y; the CLUT id.
u16 upload_sheet_clut(const Bytes& b, const u8* sheet, int y) {
  return upload_clut256(b, static_cast<size_t>(sheet - b.data()) + rd32(sheet) + 4, 0, y);
}

void load_battle_music_pack(Bytes data) {
  BattleGlobals& g = bg();
  if (g.config[0x180] != 0 || data.size() < 8) return;
  g.music_pack = std::move(data);
  sound_load_bank(g.sound, g.music_pack.data() + rd32(g.music_pack.data()), 0, 0);
  auto seq = disc::slz::unpack_block(std::span<const u8>(g.music_pack).subspan(4), 0, 0, nullptr);
  if (seq) g.battle_music = std::move(*seq);
}

void load_ui_sprites(const Bytes& data) {
  BattleGlobals& g = bg();
  g_ui0 = UiBank{};
  g_ui0.block = unpack(data, 0);
  const Bytes& b0 = g_ui0.block;
  if (b0.size() >= 0x14) {
    const u8* p = b0.data();
    g_ui0.layout = p + rd32(p);
    g_ui0.anims = p + rd32(p + 4);
    g_ui0.frames = p + rd32(p + 8);
    g_ui0.sheet = p + rd32(p + 0xC);
    const size_t clut = static_cast<size_t>(g_ui0.sheet - p) + rd32(g_ui0.sheet) + 4;
    upload_clut256(b0, clut, 0, 0x1FE);
    for (int i = 0; i < 256; ++i) g.ui_palette[i] = clut + 2 * i + 2 <= b0.size() ? rd16(p + clut + 2 * i) : 0;
    upload_sheet(b0, g_ui0.sheet, 0x3C0, 0);
    upload_clut256(b0, rd32(p + 0x10) + 0x14, 0, 0x1F5);
    psyq::DrawSync(0);
  }
  g_ui1 = UiBank{};
  g_ui1.block = unpack(data, 1);
  const Bytes& b1 = g_ui1.block;
  if (b1.size() >= 0x10) {
    const u8* p = b1.data();
    g_ui1.layout = p + rd32(p);
    g_ui1.anims = p + rd32(p + 4);
    g_ui1.frames = p + rd32(p + 8);
    g_ui1.sheet = p + rd32(p + 0xC);
    upload_sheet_clut(b1, g_ui1.sheet, 0x1FF);
    upload_sheet(b1, g_ui1.sheet, 0x3C0, 0x100);
    psyq::DrawSync(0);
  }
}

void load_effect_sprites(const Bytes& data) {
  BattleGlobals& g = bg();
  g.fx_sprites = unpack(data, 0);
  const Bytes& b = g.fx_sprites;
  if (b.size() < 0x20) return;
  const u8* p = b.data();
  AnimSet* a = animset_alloc();
  g.sprite_bank_fx->animset = a;
  a->layout = p + rd32(p);
  a->anims = p + rd32(p + 4);
  a->frames[0] = p + rd32(p + 8);
  a->sheets[0] = p + rd32(p + 0xC);
  a->frames[1] = p + rd32(p + 0x10);
  a->sheets[1] = p + rd32(p + 0x14);
  upload_clut256(b, rd32(p + 0x18) + 0x14, 0, 0x1F6);
  a->clut[1] = upload_sheet_clut(b, a->sheets[1], 0x1F8);
  a->clut[0] = upload_sheet_clut(b, a->sheets[0], 0x1F7);
  a->tpage[0] = upload_sheet(b, a->sheets[0], 0x280, 0x100);
  a->tpage[1] = a->tpage[0];
  static const s16 kArea[4] = {0x280, 0x180, 0x2BF, 0x1FF};
  g.sprite_bank_fx->texcache_shared = 0;
  g.sprite_bank_fx->texcache = texcache_create(kArea);
  for (size_t i = 0, w = 0x1C; w + 4 <= b.size() && i < g.effect_data.size(); w += 4, ++i) {
    const u32 off = rd32(p + w);
    if (off == 0) break;
    g.effect_data[i] = p + off;
  }
  psyq::DrawSync(0);
}

void load_battle_text(const Bytes& data) {
  BattleGlobals& g = bg();
  g.text_strings = unpack(data, 0);
  g.text_font = unpack(data, 1);
  const Bytes tim = unpack(data, 2);
  upload_clut256(tim, 0x14, 0, 0x1F9);
  psyq::DrawSync(0);
  text_reset();  // g_text_pixels, the #1980 line buffer
}

void load_item_data(const Bytes& data) {
  BattleGlobals& g = bg();
  g.item_data = unpack(data, 0);
  field::mods_apply_items(g.item_data);  // world edits (game/field/mods.h)
  // The console also points inventory +0x820 at this table; the port's readers take it
  // from item_data_field.
  for (int item = 1; item < 0x400; ++item) {
    g.item_effects[0x80 + item] = static_cast<u8>(item_data_field(item, 0x28));
    g.item_effects[0x480 + item] = static_cast<u8>(item_data_field(item, 0x19));
  }
  for (int i = 0; i < 0x80; ++i) {
    u32 bits = 0, next = 0;
    for (int k = 0; k < 8; ++k) {
      bits = (item_data_field(i * 8 + k, 0x29) & 1) ? (next | 0x80) : next;
      next = bits >> 1;
    }
    g.item_effects[i] = static_cast<u8>(bits);
  }
}

void load_intro_voice(int which, Bytes data) {
  BattleGlobals& g = bg();
  while (sound_bank_busy(g.sound) != 0) {
  }
  g.intro_voice_state = static_cast<u8>(which == 0 ? 1 : 3);
  g_intro_voice[which] = std::move(data);
  if (!g_intro_voice[which].empty())
    sound_load_effect_bank(g.sound, g_intro_voice[which].data(), 0, 0, which + 4, 0);
  while (sound_bank_busy(g.sound) != 0) {
  }
}

}  // namespace

const u8* ovl1984_data(u32 address) {
  const Bytes& b = bg().ovl1984;
  const u32 off = address - 0x800715D0u;
  return off < b.size() ? b.data() + off : nullptr;
}

const u8* ovl1981_data(u32 address) {
  const Bytes& b = bg().ovl1981;
  const u32 off = address - 0x80085518u;
  return off < b.size() ? b.data() + off : nullptr;
}

void battle_load_entry(int index, s32 entry, StageLoadCtx* ctx, Bytes data) {
  BattleGlobals& g = bg();
  (void)entry;
  switch (index) {
    case 0:
      load_battle_music_pack(std::move(data));
      while (sound_bank_busy(g.sound) != 0) {
      }
      break;
    case 1: g.victory_music = unpack(data, 0); break;
    case 2: load_stage(ctx, data); break;
    case 3: load_ui_sprites(data); break;
    case 4: load_battle_text(data); break;
    case 5:
      // #1981 (the result screen) is native; the console keeps the packed entry and unpacks
      // it at state 4. Its level-up tables are read from the image.
      g.ovl1981 = unpack(data, 0);
      g.have_result_code = true;
      break;
    case 6: load_item_data(data); break;
    case 7: load_formation(data); break;
    case 8: load_effect_sprites(data); break;
    case 9:
      // #1984 (the battle code chain) is native; its data tables are read from block 0.
      g.ovl1984 = unpack(data, 0);
      g.have_battle_code = true;
      break;
    case 10: load_intro_voice(0, std::move(data)); break;
    case 11: load_intro_voice(1, std::move(data)); break;
    default: break;
  }
}

void battle_setup_sprite_banks() {
  BattleGlobals& g = bg();
  const StageData& st = stage();
  if (AnimSet* a = animset_alloc()) {
    g.sprite_set_a->animset = a;
    a->layout = g_ui0.layout;
    a->sheets[0] = g_ui0.sheet;
    a->frames[0] = g_ui0.frames;
    a->anims = g_ui0.anims;
    a->tpage[0] = 0xF;
    a->clut[0] = 0x7F80;
  }
  if (AnimSet* a = animset_alloc()) {
    g.sprite_set_b->animset = a;
    a->layout = g_ui1.layout;
    a->sheets[0] = g_ui1.sheet;
    a->frames[0] = g_ui1.frames;
    a->anims = g_ui1.anims;
    a->tpage[0] = 0x1F;
    a->clut[0] = 0x7FC0;
  }
  if (AnimSet* a = animset_alloc()) {
    g.sprite_set_c->animset = a;
    a->layout = st.sprite_layout.empty() ? nullptr : st.sprite_layout.data();
    a->sheets[0] = st.sprite_sheet.empty() ? nullptr : st.sprite_sheet.data();
    a->frames[0] = st.sprite_frames.empty() ? nullptr : st.sprite_frames.data();
    a->anims = st.sprite_anims.empty() ? nullptr : st.sprite_anims.data();
    a->tpage[0] = 10;
    a->clut[0] = 0x7A00;
  }
  if (AnimSet* a = animset_alloc()) {
    g.sprite_bank_fx2->animset = a;
    if (g.sprite_bank_fx->animset != nullptr) *a = *g.sprite_bank_fx->animset;
    a->clut[0] = 0x7D80;
  }
}

u32 battle_party_member_mask() {
  u32 mask = 0;
  const auto* party = static_cast<const u8*>(handoff_get(bg().handoff, 3));
  if (party == nullptr) return 0;
  for (int i = 0; i < 8; ++i) {
    const u16 id = rd16(party + i * 0x60);
    if (static_cast<u16>(id - 1) < 0xC) mask |= 1u << ((static_cast<s16>(id) - 1) & 0x1F);
  }
  return mask;
}

u32 battle_party_member_mask_all() {
  u32 mask = battle_party_member_mask();
  const auto* block = static_cast<const u8*>(handoff_get(bg().handoff, 9));
  if (block == nullptr || ((block[0x8C0] >> 2) & 1) == 0) return mask;
  for (int i = 0; i < block[0x1EB]; ++i) {
    const u32 c = static_cast<u32>(block[0x1EC + i] & 0xF) - 1;
    if (c < 0xC) mask |= 1u << c;
  }
  return mask;
}

void battle_map_resolve_entries(s16 out[0x11], int map, u32 x, u32 z) {
  const auto* block = static_cast<const u8*>(handoff_get(bg().handoff, 9));
  out[0] = -1;
  int m = map;
  if (map == -1) {
    m = static_cast<s16>(rd16(block + 0x23A));
    if (static_cast<s16>(rd16(block + 0x23C)) != -1) out[0] = static_cast<s16>(rd16(block + 0x23C));
    x = rd32(block + 0x228);
    z = rd32(block + 0x230);
  }
  out[5] = static_cast<s16>(static_cast<u32>(m - 1) > 2 ? 0xC1D : 0xC1E);
  const u32 mask = battle_party_member_mask_all();
  out[1] = 0xC26;
  const int b = static_cast<int>((mask & 0xF80) >> 7);
  if (static_cast<u32>(m - 1) < 3) {
    // World maps: the chunk container by position (map 1 swaps nine chunks while the
    // state block's +0x220 is 0xf..0x3c), and the world's party pack.
    if (m == 1) {
      int chunk = static_cast<int>(x / 0x3000 + (z / 0x3000) * 9);
      if (static_cast<u32>(rd32(block + 0x220) - 0xF) < 0x2E) {
        static const int remap[][2] = {{0x26, 0x3F}, {0x27, 0x40}, {0x28, 0x41}, {0x2F, 0x42}, {0x30, 0x43},
                                       {0x31, 0x44}, {0x38, 0x45}, {0x39, 0x46}, {0x3A, 0x47}};
        for (const auto& r : remap)
          if (chunk == r[0]) {
            chunk = r[1];
            break;
          }
      }
      out[2] = static_cast<s16>(chunk + 0x1014);
    } else if (m == 2) {
      out[2] = static_cast<s16>(x / 0x3000 + (z / 0x3000) * 9 + 0x109F);
    } else {
      out[2] = 0x10E0;
    }
    if (map == -1) {
      static const s16 restore[3][3] = {{0x105D, 0x105E, 0x105F}, {0x10E6, 0x10E7, 0x10E8}, {0x10EA, 0x10EB, 0x10EC}};
      out[3] = restore[m - 1][0];
      out[0] = restore[m - 1][1];
      out[5] = restore[m - 1][2];
    } else {
      out[3] = static_cast<s16>(m == 1 ? 0x105C : m == 2 ? 0x10E5 : 0x10E9);
    }
    out[4] = -1;
  } else {
    out[2] = static_cast<s16>(m + 0xC87);
    out[3] = static_cast<s16>(((mask & 0x7C) >> 2) * 2 + 0xC27);
    out[4] = static_cast<s16>(b != 0 ? b + 0xC67 : -1);
  }
  if (block[0x1FF] != 0) {  // the script's override of the party packs
    out[3] = static_cast<s16>(rd32(block + 0x1E0));
    out[4] = static_cast<s16>(rd32(block + 0x1E4));
  }
  int count = 6;
  int end = 6;  // the original zeroes word 0xc / 2 when map != -1
  if (map == -1) {
    for (int i = 0; i < block[0x1EA] && count < 0x10; ++i) out[count++] = static_cast<s16>(rd16(block + 0x1A8 + i * 2));
    end = count;
  }
  out[end] = 0;
  out[0x10] = static_cast<s16>(count);
}

void battle_queue_field_preloads() {
  // The console takes min(free heap - 0x4000, 0xa0000) bytes, retrying 32 KB smaller until
  // mem_alloc_high succeeds (nothing below 0x801). The host has the room, so the cap holds.
  BattleGlobals& g = bg();
  if (g.preload_list.empty()) return;
  s32 room = 0xA0000;
  struct Pick {
    s32 entry;
    size_t at;
  };
  std::vector<Pick> picks;
  size_t total = 0;
  for (size_t i = 0; i < g.preload_list.size() && g.preload_list[i] != 0; ++i) {
    const s32 entry = g.preload_list[i];
    if (entry <= 0) continue;
    const s32 bytes = g.loader->entry_size(entry);
    if (room - bytes < 0) continue;
    g.preload_list[i] = static_cast<s16>(-entry);
    picks.push_back({entry, total});
    total += static_cast<size_t>(bytes);
    room -= bytes;
  }
  // The reads are queued sorted by entry (bload_request group 2); battle_exit_to_field
  // waits for them. The host reads them now.
  std::sort(picks.begin(), picks.end(), [](const Pick& a, const Pick& b) { return a.entry < b.entry; });
  g.handoff10_blob.assign(total, 0);
  for (const Pick& p : picks) g.loader->load_sync(g.handoff10_blob.data() + p.at, p.entry, 0);
  std::string list;
  for (const s16 e : g.preload_list) list += " " + std::to_string(e);
  runtime::Runtime::get().trace(("battle field preloads:" + list + ", " + std::to_string(total) + " bytes kept").c_str());
}

bool inventory_verify_all(const u8* inv) {
  // inventory_verify 0x800695b4: each entry's check byte (+0x824 + i) against the value
  // recomputed with the key it carries.
  const auto* items = reinterpret_cast<const u16*>(inv);
  for (u32 i = 0; i < 0x400; ++i) {
    const u8 stored = inv[0x824 + i];
    const u32 key = (~i & 0xF) ^ (stored >> 4);
    if (inventory_check_byte(items, i, key) != stored) return false;
  }
  return true;
}

}  // namespace ss::game::battle
