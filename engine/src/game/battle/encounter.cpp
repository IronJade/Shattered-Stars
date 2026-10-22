// The encounter pack (archive entry 0x57d + encounter, #1405-#1968): sprite sets to
// VRAM, palette overrides, the formation and the per-monster stats (load_formation
// 0x8007344c, enemy_create_by_type 0x80075ad8, enemy_read_stats 0x80076298).
// Format: docs/notes/battle.md §6.4 and analysis/battle_catalog/parts/p6.md §4.
//
// The pack's enemy code blocks (code list, kinds 0-3 -> enemy-code slots A-D) are not
// loaded: monster classes are native (unit.h). The family a type belongs to is kept in
// EnemyUnit::family for the per-family behaviour.
#include <cstring>
#include <memory>

#include "disc/slz.h"
#include "game/battle/battle.h"
#include "game/battle/stage.h"
#include "game/battle/unit.h"
#include "game/battle/vram.h"
#include "runtime/libgpu.h"
#include "runtime/runtime.h"

namespace ss::game::battle {
namespace {

u32 rd32(const u8* p) {
  u32 v;
  std::memcpy(&v, p, 4);
  return v;
}
s32 rds32(const u8* p) { return static_cast<s32>(rd32(p)); }
u16 rd16(const u8* p) {
  u16 v;
  std::memcpy(&v, p, 2);
  return v;
}
s16 rds16(const u8* p) { return static_cast<s16>(rd16(p)); }

// The unpacked sprite packs (g_enemy_packs 0x8006f9a4), owned until the battle ends.
std::vector<std::unique_ptr<std::vector<u8>>> g_packs;
std::vector<std::unique_ptr<EnemyUnit>> g_enemies;
std::array<std::array<u8, 0x60>, 8> g_enemy_stat_blocks{};
std::array<const u8*, 16> g_enemy_extra_data{};  // 0x8006fd8c g_enemy_extra_data

// The class family by monster type (the factory's switch). The resident families'
// vtables are named in the catalogue; "slot X" are the encounter pack's code modules.
EnemyFamily family_of(int type) {
  switch (type) {
    case 1: case 2: case 3: case 0x79: case 0x7a: case 0x7b: case 0x7c: return EnemyFamily::Knight;
    case 4: case 5: case 6: case 0x3e: case 0x3f: case 0x40: return EnemyFamily::Base;
    case 7: case 8: case 9: case 0x7d: case 0x7e: case 0x7f: return EnemyFamily::Guard;
    case 0xd: case 0xe: case 0xf: return EnemyFamily::Plant;
    case 0x13: case 0x14: case 0x15: return EnemyFamily::Kobold;
    case 0x16: case 0x17: case 0x18: case 0x84: case 0x85: return EnemyFamily::Thief;
    case 0x19: case 0x1a: case 0x1b: case 0x86: case 0x87: return EnemyFamily::Magius;
    case 0x1c: case 0x1d: case 0x1e: case 0x88: case 0x89: return EnemyFamily::Axe;
    case 0x29: case 0x2a: case 0x2b: case 0x92: return EnemyFamily::Sandglass;
    case 0x32: case 0x33: case 0x34: return EnemyFamily::Ooze;
    case 0x41: case 0x42: case 0x43: return EnemyFamily::Beastmaster;
    case 0x44: case 0x45: case 0x46: return EnemyFamily::Wolf;
    case 0x4a: case 0x4b: case 0x4c: case 0x4d: case 0x99: case 0x9a: case 0x9b: return EnemyFamily::Giant;
    case 0x4e: case 0x4f: case 0x50: case 0x9c: case 0x9d: return EnemyFamily::Goat;
    case 0x60: case 0x61: case 0x62: case 0xa1: case 0xa2: return EnemyFamily::Archer;
    case 0x6e: case 0x6f: case 0x70: return EnemyFamily::Shout;
    case 0xb3: case 0xb4: case 0xb5: case 0xb6: case 0xb7: return EnemyFamily::Wizard;
    default:
      return (type >= 1 && type <= 0xcf) ? EnemyFamily::Module : EnemyFamily::None;
  }
}

}  // namespace

void encounter_reset() {
  g_packs.clear();
  g_enemies.clear();
  g_enemy_extra_data.fill(nullptr);
}

void enemy_create_by_type(int index, int type) {
  BattleGlobals& g = bg();
  const EnemyFamily family = family_of(type);
  EnemyUnit* e = nullptr;
  if (family != EnemyFamily::None) {
    g_enemies.push_back(std::make_unique<EnemyUnit>());
    e = g_enemies.back().get();
    e->family = family;
    // Family constructors (enemy_<family>_create) only change the hit sounds.
    switch (family) {
      case EnemyFamily::Beastmaster: e->sfx_hit = 0x4D, e->sfx_crit = 0x4E; break;
      case EnemyFamily::Wolf: e->sfx_hit = 0x4C, e->sfx_crit = 0x4E; break;
      case EnemyFamily::Archer: e->sfx_hit = 0x4B; break;
      default: break;
    }
    if (family == EnemyFamily::Module) {
      runtime::Runtime::get().log_once("battle-enemy-module-" + std::to_string(type),
                                       "battle: monster type " + std::to_string(type) +
                                           " uses encounter-pack code (not ported; base behaviour)");
    }
  }
  g.enemy_units[index] = e;
  // The common tail runs even for unknown types on the console (through a null unit).
  if (e == nullptr) return;
  e->animset = animset_alloc();
  g_enemy_stat_blocks[index].fill(0);
  e->stats = g_enemy_stat_blocks[index].data();
  e->slot = static_cast<u8>(index);
  g.enemy_stats[index] = e->stats;
  e->st()->id = static_cast<u16>(type);
  e->st()->status = 0;
  task_register(e, 2);
}

const u8* enemy_read_stats(int index, const u8* r) {
  BattleGlobals& g = bg();
  EnemyUnit* e = g.enemy_units[index];
  if (e == nullptr) return r + 0x5C;
  Stats* s = e->st();
  const s32 hp = rds32(r);
  s->hp_base = s->hp_max = s->hp = hp;
  const s16 mp = static_cast<s16>(rds32(r + 4));
  s->mp_base = s->mp_max = s->mp = mp;
  s->exp = rds32(r + 8);
  e->fol = rds32(r + 0xC);
  e->size[0] = rds16(r + 0x10);
  e->size[1] = rds16(r + 0x12);
  e->size[2] = rds16(r + 0x14);
  e->run_speed = e->base_speed = static_cast<s32>(static_cast<u32>(rd16(r + 0x16)) << 12);
  e->drop_item[0] = rd16(r + 0x18);
  e->drop_item[1] = rd16(r + 0x1A);
  s->atk[0] = s->atk[1] = s->atk[2] = rds16(r + 0x1C);
  s->def[0] = s->def[1] = s->def[2] = rds16(r + 0x1E);
  s->intel[0] = s->intel[1] = s->intel[2] = rds16(r + 0x20);
  e->base_reach = static_cast<s16>(rds16(r + 0x22) << 2);
  s->battle_level = rds16(r + 0x24);
  s->eva[0] = s->eva[1] = s->eva[2] = rds16(r + 0x26);
  s->acc[0] = s->acc[1] = s->acc[2] = rds16(r + 0x28);
  std::memcpy(s->element, r + 0x2A, 12);  // +4..+0xf (the last two are the status resistances)
  const u8* b = r + 0x2A + 11;  // the decompile's pbVar10: the last of the 12 bytes
  s->guts[0] = s->guts[1] = s->guts[2] = b[1];
  e->drop_chance[0] = b[2];
  e->drop_chance[1] = b[3];
  e->recovery = b[4];
  e->aggro_radius = rd16(b + 5);
  for (int i = 0; i < 4; ++i) e->spells[i] = b[7 + i];
  for (int i = 0; i < 5; ++i) e->odds[i] = b[0xB + i];
  s->crit = b[0x10];
  s->stat4e[2] = b[0x11];
  s->stat54[2] = b[0x12];
  e->weapon_status = b[0x13];
  e->weapon_chance = b[0x14];
  e->race_byte = b[0x15];
  s->weapon_element = b[0x16];
  std::memcpy(e->name, b + 0x17, 16);
  const u8 difficulty = g.config[0x4F];
  if (difficulty == 1) {
    for (s16& v : s->atk) v = static_cast<s16>(static_cast<u16>(v) + ((static_cast<s16>(v) - (static_cast<s16>(v) >> 15)) >> 1));
    s->hp_base += s->hp_base / 2;
    s->hp_max += s->hp_max / 2;
    s->hp += s->hp / 2;
  } else if (difficulty == 2) {
    auto third2 = [](s16 v) { return static_cast<s16>(v + (static_cast<s16>((static_cast<s64>(v * 2) * 0x55555556) >> 32) - static_cast<s16>((v * 2) >> 31))); };
    for (s16& v : s->atk) v = third2(v);
    s->hp_base <<= 1;
    s->hp_max <<= 1;
    s->hp <<= 1;
    s32 m = mp * 2;
    if (m > 999) m = 999;
    s->mp_base = s->mp_max = s->mp = static_cast<s16>(m);
    for (s16& v : s->eva) v = static_cast<s16>(v + 0x80);
    for (s16& v : s->acc) v = static_cast<s16>(v + 0x50);
    for (s16& v : s->intel) v = static_cast<s16>(v + (static_cast<s16>((static_cast<s64>(v) * 0x55555556) >> 32) - (v >> 15)));
  }
  return b + 0x17 + 16;
}

void load_formation(std::span<const u8> pack) {
  BattleGlobals& g = bg();
  const StageData& st = stage();
  const u8* base = pack.data();
  // Per sprite-set kind (the stack arrays of the original).
  const u8* layout[4] = {};
  const u8* anims[4] = {};
  const u8* frames[4][8] = {};
  const u8* sheets[4][8] = {};
  s16 cache_area[4][4] = {};
  u16 clut[4] = {};
  u16 tpage[4][8] = {};
  int kind = 0, page = 0, extra = 0;
  s32 vram_x = 0, vram_y = 0;
  size_t p = 0;
  // (1) Sprite sets: {offset, kinds} until -1.
  while (p + 4 <= pack.size() && rds32(base + p) != -1) {
    const u32 off = rd32(base + p);
    const int kinds = rds32(base + p + 4);
    p += 8;
    auto unpacked = off < pack.size() ? disc::slz::unpack_block(pack.subspan(off), 0, 0, nullptr) : std::nullopt;
    g_packs.push_back(std::make_unique<std::vector<u8>>(unpacked ? std::move(*unpacked) : std::vector<u8>{}));
    const std::vector<u8>& sp = *g_packs.back();
    if (sp.size() < 16) {
      kind += kinds;
      continue;
    }
    const u8* s = sp.data();
    u32 n_sheets = rd32(s);
    const bool has_extra = static_cast<s32>(n_sheets) > 0x7F;
    if (has_extra) n_sheets &= 0x7F;
    const u32 n_extra_sets = rd32(s + 4);
    for (int k = 0; k < kinds && kind + k < 4; ++k) {
      const int kk = kind + k;
      layout[kk] = s + rd32(s + 8);
      anims[kk] = s + rd32(s + 0xC);
      u32 w = 4;
      int set = 0;
      for (u32 i = 0; i < n_sheets && set < 8; ++i, ++set, w += 2) {
        frames[kk][set] = s + rd32(s + w * 4);
        sheets[kk][set] = s + rd32(s + (w + 1) * 4);
      }
      for (u32 i = 0; i < n_extra_sets && set < 8; ++i, ++set, w += 2) {
        frames[kk][set] = s + rd32(s + w * 4);
        sheets[kk][set] = s + rd32(s + (w + 1) * 4);
      }
      if (k == 0 && has_extra) {
        for (;; ++w) {
          const u32 o = rd32(s + w * 4);
          if (o == 0) break;
          if (extra < 16) g_enemy_extra_data[extra++] = s + o;
        }
      }
      // Upload the textured sheets, page by page across all kinds (0x340/0x380 x 0/0x100).
      for (u32 i = 0; i < n_sheets; ++i) {
        const u8* sheet = sheets[kk][i];
        const int pages = sheet[9] + 1;
        switch (page) {
          case 0: vram_x = 0x340, vram_y = 0; break;
          case 1: vram_x = 0x380, vram_y = 0; break;
          case 2: vram_x = 0x340, vram_y = 0x100; break;
          case 3: vram_x = 0x380, vram_y = 0x100; break;
          default: break;  // the console keeps the previous position
        }
        if (i == 0) {
          const u8 a = sheet[0xC] >> 2, b = sheet[0xD], c = sheet[0xE] >> 2, d = sheet[0xF];
          cache_area[kk][0] = static_cast<s16>(a == 0 ? vram_x + 1 : vram_x + a);
          cache_area[kk][2] = static_cast<s16>(c > 0x3F ? vram_x + 0x3F : vram_x + c);
          cache_area[kk][1] = static_cast<s16>(b == 0 ? vram_y + 1 : vram_y + b);
          cache_area[kk][3] = static_cast<s16>(d == 0xFF ? vram_y + 0xFE : vram_y + d);
          u16 work[256];
          const size_t clut_off = static_cast<size_t>(sheet - sp.data()) + rd32(sheet);
          clut_build_pair(work, sp, clut_off + 4, static_cast<int>(rd32(sp.data() + clut_off)));
          clut[kk] = upload_clut256(work, 0, kk + 0x1FA);
        }
        ++page;
        size_t img = static_cast<size_t>(sheet - sp.data()) + rd32(sheet + 4);
        u16 w0 = rd16(sp.data() + img), h0 = rd16(sp.data() + img + 2);
        tpage[kk][i] = upload_texture(sp, img + 4, 0, 0, vram_x, vram_y, w0, h0);
        for (int pg = 1; pg < pages; ++pg) {
          vram_x += 0x40;
          if (vram_x > 0x380) vram_x = 0x340, vram_y = 0x100;
          img += 4 + static_cast<size_t>(w0) * h0 / 2;
          img = (img + 3) & ~size_t{3};
          w0 = rd16(sp.data() + img), h0 = rd16(sp.data() + img + 2);
          ++page;
          upload_texture(sp, img + 4, 0, 0, vram_x, vram_y, w0, h0);
        }
      }
      for (u32 i = n_sheets; i < n_sheets + n_extra_sets && i < 8; ++i) tpage[kk][i] = tpage[kk][0];
    }
    psyq::DrawSync(0);
    kind += kinds;
  }
  p += 4;
  // (2) Palette overrides: {kind, TIM offset} until -1.
  while (p + 4 <= pack.size() && rds32(base + p) != -1) {
    const int k = rds32(base + p);
    const u32 off = rd32(base + p + 4);
    p += 8;
    u16 work[256];
    clut_build_pair(work, pack, off + 0x14, 0x80);
    if (k >= 0 && k < 4) clut[k] = upload_clut256(work, 0, k + 0x1FA);
    psyq::DrawSync(0);
  }
  p += 4;
  // (3) Code blocks: {kind, offset} until -1. Native classes replace the code.
  while (p + 4 <= pack.size() && rds32(base + p) != -1) p += 8;
  // (4) Placements: 12-byte records until type 0.
  int index = 0;
  const s32 span_x = st.bounds[2] - st.bounds[0];
  const s32 span_z = st.bounds[3] - st.bounds[1];
  for (p += 4; p + 12 <= pack.size() && rds16(base + p) != 0; p += 12, ++index) {
    if (index >= 8) break;
    const u8* r = base + p;
    enemy_create_by_type(index, rds16(r));
    EnemyUnit* e = g.enemy_units[index];
    if (e == nullptr) continue;
    e->pos[0] = ((rds16(r + 2) * (span_x >> 12)) / 0x1004 + ((span_x / 2 + st.bounds[0]) >> 12)) * 0x1000;
    e->pos[2] = ((rds16(r + 4) * (span_z >> 12)) / 0x1004 + ((span_z / 2 + st.bounds[1]) >> 12)) * 0x1000;
    e->pos[1] = rds16(r + 6) << 12;
    e->flying = e->pos[1] != 0;
    if (e->pos[1] == 0) e->pos[1] = bobj_ground_height(e);
    e->hover_height = e->pos[1];
    e->home[0] = e->pos[0];
    e->home[1] = e->pos[1];
    e->home[2] = e->pos[2];
    const int k = rds16(r + 8) & 3;
    e->model_layout = layout[k];
    e->model_anims = anims[k];
    e->model_frames = frames[k][0];
    e->model_sheet = sheets[k][0];
    for (int i = 0; i < 7; ++i) {
      e->frames_more[i] = frames[k][i + 1];
      e->sheets_more[i] = sheets[k][i + 1];
    }
    AnimSet* a = e->animset;
    if (a != nullptr) {
      a->layout = e->model_layout;
      a->anims = e->model_anims;
      a->frames[0] = e->model_frames;
      a->sheets[0] = e->model_sheet;
      for (int i = 0; i < 7; ++i) {
        a->frames[i + 1] = e->frames_more[i];
        a->sheets[i + 1] = e->sheets_more[i];
      }
      for (int i = 0; i < 8; ++i) {
        a->tpage[i] = tpage[k][i];
        a->clut[i] = clut[k];
      }
    }
    e->tpage = tpage[k][0];
    e->clut = clut[k];
    e->texcache = texcache_create(cache_area[k]);
    e->texcache_shared = 0;
    e->facing[1] = 0;
    e->facing[2] = 0;
    const u16 flags = rd16(r + 10);
    e->facing[0] = (flags & 1) != 0 ? 0x64000 : -0x64000;
    e->boss = (flags >> 1) & 1;
    if (flags & 4) {
      e->airborne = 1;
      e->flying = 0;
    }
  }
  g.enemy_count = static_cast<u8>(index);
  const u16 fflags = p + 4 <= pack.size() ? rd16(base + p + 2) : 0;
  g.can_escape = static_cast<u8>((fflags ^ 1) & 1);
  if (fflags & 2) g.intro_type = 1;
  if (fflags & 4) g.intro_type = 2;
  if (fflags & 8) g.intro_type = 3;
  // (5) The stats records.
  const u8* r = base + p + 4;
  for (int i = 0; i < g.enemy_count; ++i) {
    if (r + 0x5C > base + pack.size()) break;
    r = enemy_read_stats(i, r);
  }
  psyq::DrawSync(0);
}

}  // namespace ss::game::battle
