// Party members in battle: the party-data loader (the 0x54-byte task battle_main_loop
// registers at priority 0, party_loader_create 0x80082bd8 / party_loader_update
// 0x80082cdc: the only driver of battle state 0), the member construction
// (pc_construct_slot 0x80083fc8, pc_setup_<character> 0x80084538..), the equipment
// stats (pc_calc_equipment_stats 0x80081728, pc_apply_equip_effects 0x800818dc) and
// the PartyUnit's update. Notes: analysis/battle_catalog/parts/p7.md §2-3.
//
// The console streams each member's data (entry g_char_battle_entries[character]
// [weapon variant], #2130, #2132 ...) through two 0x10c00 buffers, unpacks the matching
// code entry (#2131, #2133 ...) to 0x801ee000 + 0x4000 * slot and runs its constructor.
// The port reads the data entry in one go and builds the PartyUnit natively; the code
// entries are not read.
#include <cstring>
#include <memory>

#include "disc/slz.h"
#include "game/battle/battle.h"
#include "game/battle/battle_debug.h"
#include "game/battle/camera.h"
#include "game/battle/party_talk.h"
#include "game/battle/pc_modules.h"
#include "game/battle/render.h"
#include "game/battle/stage.h"
#include "game/battle/sprite.h"
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
s16 rds16(const u8* p) {
  s16 v;
  std::memcpy(&v, p, 2);
  return v;
}
void wr16(u8* p, s16 v) { std::memcpy(p, &v, 2); }

void todo(const std::string& what) {
  runtime::Runtime::get().log_once("battle-party-" + what, "battle: not ported yet: " + what);
}

std::array<std::unique_ptr<PartyUnit>, 4> g_members;

// (Port, original_bugs.md B14) the raw stage file (load-list entry 2) as the console's heap
// held it when the party units were built: what an uninitialised field reads.
std::vector<u8> g_stage_file;

// The size each character's module allocates for its PartyUnit (the 'li a0' before the
// operator new 0x8001fd94 in the code entry's first function; #2131 .. #2159), ids 1-12.
constexpr u32 kUnitAllocSize[12] = {0x360, 0x360, 0x360, 0x360, 0x360, 0x364,
                                    0x388, 0x360, 0x360, 0x370, 0x360, 0x368};

// What the console's heap holds at party slot SLOT's unit +OFF when the unit is built. The
// units come from mem_alloc_low's high mode (0x80012fec: (size + 0x13) & ~3 bytes per block,
// a 0x10-byte header first, top down) in slot order, right below the party code area's block
// (header 0x801edff0). That memory last held the raw stage file: the field's first preload
// (the lowest archive entry), read into the top of the heap, ending at 0x801fe000. Checked
// on twelve stages and three units (Claude, Celine, Ernest) in the Redux references.
// Outside the file: 0.
s32 heap_word_at_unit(const u8* party, int slot, u32 off) {
  u32 top = 0x801EDFF0;
  for (int j = 0; j <= slot; ++j) {
    const int id = rds16(party + j * 0x60);
    if (id < 1 || id > 12) continue;
    top -= (kUnitAllocSize[id - 1] + 0x13) & ~3u;
  }
  const u32 at = top + 0x10 + off;
  const u32 end = 0x801FE000;
  const u32 size = static_cast<u32>(g_stage_file.size());
  if (size == 0 || at + 4 > end || end - at > size) return 0;
  return static_cast<s32>(rd32(g_stage_file.data() + (size - (end - at))));
}
std::array<std::vector<u8>, 4> g_member_data;   // g_member_buf / g_member_data: the unpacked phase-0 block
std::array<std::array<std::vector<u8>, 4>, 4> g_move_blocks;  // g_member_data [slot][1..4]: killer-move data
// Claude's two upgraded moves: killer_move_data_entry patches its entry table in RAM for
// the rest of the battle (#2129 is reloaded with the next one).
u8 g_claude_upgrades = 0;

// pc_calc_equipment_stats context (g_statcalc_inventory 0x8008517c / g_statcalc_record 0x80085178).
const u8* g_statcalc_record = nullptr;

s32 equip_field(int slot_offset, int field) {
  return item_data_field(rds16(g_statcalc_record + slot_offset), field);
}

// 0x8008282c: base + the field over the 7 equipped items (record +0xc..+0x18), capped.
s32 equip_sum_field(s32 base, int field, s32 cap) {
  s32 v = base;
  for (int i = 0; i < 7; ++i) v += equip_field(0xC + 2 * i, field);
  return cap < v ? cap : v;
}

u32 equip_or_field(int field) {
  u32 v = 0;
  for (int i = 0; i < 7; ++i) v |= static_cast<u32>(equip_field(0xC + 2 * i, field));
  return v;
}

s32 equip_max_field(int field) {
  s32 v = equip_field(0xC, field);
  for (int i = 1; i < 7; ++i) {
    const s32 w = equip_field(0xC + 2 * i, field);
    if (v < w) v = w;
  }
  return v;
}

// The per-character setups (0x80084538..0x80084830): class byte, body box, speeds,
// reach, +0x234, killer-move ranges.
struct CharSetup {
  s8 klass;          // -1: not set
  s16 box[3];        // +0x52 / +0x54 / +0x56 (0: not set)
  s32 speed;         // +0x16c / +0x170
  s16 reach;         // +0x142 (0: not set)
  s32 v234;          // +0x234
  bool killer_moves;
};
const CharSetup kSetups[12] = {
    {1, {0x18, 0x2C, 0x18}, 0xF000, 0, 0x18, true},     // Claude
    {9, {0x18, 0x28, 0x18}, 0xC000, 0x88, 0x15, false}, // Rena
    {0xB, {0x18, 0x27, 0x18}, 0xB000, 0xA0, 0x14, false},  // Celine
    {-1, {0x18, 0x2E, 0x18}, 0x11000, 0, 0x18, true},   // Bowman
    {-1, {0x18, 0x28, 0x18}, 0x10000, 0, 0x18, true},   // Dias
    {-1, {0x16, 0x2B, 0x16}, 0xF000, 0, 0x15, true},    // Precis
    {-1, {0x1A, 0x2C, 0x18}, 0xE000, 0, 0x18, true},    // Ashton
    {10, {0x10, 0x20, 0x14}, 0xB000, 0x200, 0x14, false},  // Leon
    {-1, {0x14, 0x2A, 0x16}, 0xD000, 0, 0x16, true},    // Opera
    {-1, {0x18, 0x2A, 0x18}, 0xF000, 0, 0x18, true},    // Ernest
    {0xC, {0x18, 0x30, 0x18}, 0xD000, 0xA0, 0x15, false},  // Noel
    {-1, {0x18, 0x26, 0x18}, 0x10000, 0, 0x18, true},   // Chisato
};

// 0x800849c0 pc_killer_move_table: per character, 4 bytes per move (s16 near, s16 far).
u32 killer_move_table(int character) {
  switch (character) {
    case 1: return 0x8006B220;   // g_km_params_claude
    case 4: return 0x8006B258;
    case 5: return 0x8006D4B8;
    case 6: return 0x8006B298;
    case 7: return 0x8006B2CC;
    case 9: return 0x8006B300;
    case 10: return 0x8006B330;
    case 12: return 0x8006B358;
    default: return 0;
  }
}

// 0x80084874 pc_setup_killer_moves: the ranges of the 2 x 2 assigned killer moves.
void pc_setup_killer_moves(PartyUnit* u) {
  for (int combo = 0; combo < 2; ++combo) {
    for (int k = 0; k < 2; ++k) {
      const int at = combo * 4 + k * 2;  // s16 index from +0x2de
      const u32 move = u->record[0xCC + combo * 2 + k];
      if (move == 0) {
        u->killer_range[at] = 0;
        u->killer_range[at + 1] = 0;
        continue;
      }
      const u32 table = killer_move_table(u->st()->id);
      if (table == 0) continue;
      u->killer_range[at] = battle_s16(table + move * 4 - 4);
      u->killer_range[at + 1] = battle_s16(table + move * 4 - 2);
      const s16 uses = rds16(u->record + move * 2 + 0x8A);
      u->killer_charge[combo * 2 + k] = uses;
      if (u->st()->id == 5 && move == 2 && uses > 99) {
        u->killer_range[at] = static_cast<s16>(u->killer_range[at] >> 1);
        u->killer_range[at + 1] = static_cast<s16>(u->killer_range[at + 1] >> 1);
      }
    }
  }
}

// 0x80083fc8 pc_construct_slot.
void pc_construct_slot(int slot) {
  BattleGlobals& g = bg();
  const StageData& st = stage();
  auto* party = static_cast<u8*>(handoff_get(g.handoff, 3));
  auto* records = static_cast<u8*>(handoff_get(g.handoff, 4));
  g.party_standing_at_start = 0;
  // The character's module (its PartyUnit subclass; offset 0 of the code block).
  g_members[slot] = pc_module_create(rds16(party + slot * 0x60));
  PartyUnit* u = g_members[slot].get();
  g.party_units[slot] = u;
  // ORIGINAL BUG (original_bugs.md B14): nothing initialises +0x230 (the approach's last
  // distance to the approach point), so a member's first jump check on an airborne target
  // compares with heap garbage. It decides whether that first approach jumps at once, so
  // the port keeps the console's value (a word of the raw stage file).
  u->last_dist = heap_word_at_unit(party, slot, 0x230);
  u->stats = party + slot * 0x60;
  u->record = records + slot * 0xD0;
  const int character = u->st()->id;
  if (character >= 1 && character <= 12) {
    const CharSetup& c = kSetups[character - 1];
    if (c.klass >= 0) u->st()->klass = static_cast<u8>(c.klass);
    u->size[0] = c.box[0], u->size[1] = c.box[1], u->size[2] = c.box[2];
    u->run_speed = u->base_speed = c.speed;
    if (c.reach != 0) u->base_reach = c.reach;
    u->v234 = c.v234;
    if (c.killer_moves) pc_setup_killer_moves(u);
  }
  const u8 formation = g.config[0x40];
  const s32 span_x = st.bounds[2] - st.bounds[0];
  const s32 cx = (span_x / 2 + st.bounds[0]) >> 12;
  const s32 sx = span_x >> 12;
  const s32 span_z = st.bounds[3] - st.bounds[1];
  const u32 entry = 0x80084FD8 + slot * 8 + formation * 0x20;
  u->pos[0] = ((battle_s32(entry) * sx) / 0x1004 + cx) * 0x1000;
  u->pos[2] = ((battle_s32(entry + 4) * (span_z >> 12)) / 0x1004 + ((span_z / 2 + st.bounds[1]) >> 12)) * 0x1000;
  if (g.intro_type == 1) {
    u->pos[0] += ((sx * 0xAF0) / 0x1004 + cx) * -0x1000;
  } else if (g.intro_type == 2) {
    u->pos[0] += ((sx * 0x5DC) / 0x1004 + cx) * -0x1000;
    if (slot > 1) u->facing[0] = 0x64000;
  }
  u->pos[1] = bobj_ground_height(u);
  u->hover_height = 0;
  std::memcpy(u->home, u->pos, sizeof u->pos);
  u->home[3] = u->pos_w;
  u->facing[0] = static_cast<s32>(0xFFF9C000);
  u->facing[1] = 0;
  u->facing[2] = 0;
  u->appear = 1;
  u->slot = static_cast<u8>(slot);
  u->flying = 0;
  u->weapon_status = 0;
  if (g.party_slot[slot][2] & 1) u->action = 5;
  if (!unit_is_down(u)) ++g.party_standing_at_start;
  g.party_slot[slot][2] &= 0xF;
}

// 0x80081728 pc_calc_equipment_stats.
void pc_calc_equipment_stats(u8* slot, u8* rec) {
  Stats* s = stats_of(slot);
  s->atk[1] = s->atk[0];
  s->def[1] = s->def[0];
  s->acc[1] = s->acc[0];
  s->eva[1] = s->eva[0];
  s->guts[1] = s->guts[0];
  s->battle_level = s->level;
  s->intel[1] = s->intel[0];
  wr16(rec + 0xA, rds16(rec + 8));
  g_statcalc_record = rec;
  s->atk[2] = s->atk[1] = static_cast<s16>(equip_sum_field(s->atk[0], 8, 9999));
  s->def[2] = s->def[1] = static_cast<s16>(equip_sum_field(s->def[0], 0x10, 9999));
  s->eva[2] = s->eva[1] = static_cast<s16>(equip_sum_field(s->eva[0], 0xE, 9999));
  s->acc[2] = s->acc[1] = static_cast<s16>(equip_sum_field(s->acc[0], 0xE, 9999));
  s->intel[2] = s->intel[1] = static_cast<s16>(equip_sum_field(s->intel[0], 0x12, 9999));
  s->guts[2] = s->guts[1] = static_cast<s16>(equip_sum_field(s->guts[0], 0x2C, 0xFF));
  s->stat4e[2] = s->stat4e[1] = static_cast<s16>(equip_sum_field(s->stat4e[0], 0x17, 0xFF));
  s->stat54[2] = s->stat54[1] = static_cast<s16>(equip_sum_field(s->stat54[0], 0x18, 0xFF));
  s->crit = static_cast<s16>(equip_sum_field(0, 0x1B, 0xFF));
  s->weapon_element = static_cast<u16>(equip_or_field(6));
  for (int i = 0; i < 12; ++i) slot[4 + i] = static_cast<u8>(equip_max_field(i + 0x1C));
  const s16 a = static_cast<s16>(equip_sum_field(rds16(rec), 0x16, 0xFF));
  wr16(rec + 4, a);
  wr16(rec + 2, a);
  wr16(rec + 0xA, static_cast<s16>(equip_sum_field(rds16(rec + 6), 0x14, 9999)));
}

// The party loader's stat modifiers (0x800823d4-0x800827b4, slot fields): a percentage
// change, where a negative amount on HP / MP max / +0x4a is a flat increase instead.
namespace {
s16 s16_at(const u8* slot, int off) { return rds16(slot + off); }
void stat_mod_hp_max(u8* slot, int amount) {  // 0x800823d4: +0x18, at most 9999
  s32 v;
  std::memcpy(&v, slot + 0x18, 4);
  v = amount < 0 ? v - amount : v + v * amount / 100;
  if (9999 < v) v = 9999;
  std::memcpy(slot + 0x18, &v, 4);
}
void stat_reduce_hp_max(u8* slot, int pct) {  // 0x80082444: +0x18 -= pct %, at least 1
  s32 v;
  std::memcpy(&v, slot + 0x18, 4);
  v -= v * pct / 100;
  if (v < 1) v = 1;
  std::memcpy(slot + 0x18, &v, 4);
}
void stat_mod_mp_max(u8* slot, int amount) {  // 0x8008248c: +0x22, at most 999
  const s16 v = s16_at(slot, 0x22);
  wr16(slot + 0x22, static_cast<s16>(amount < 0 ? v - amount : v + static_cast<s16>(v * amount / 100)));
  if (999 < s16_at(slot, 0x22)) wr16(slot + 0x22, 999);
}
void stat_reduce_mp_max(u8* slot, int pct) {  // 0x80082504: +0x22 -= pct %, at least 0
  const s16 v = s16_at(slot, 0x22);
  wr16(slot + 0x22, static_cast<s16>(v - v * pct / 100));
  if (s16_at(slot, 0x22) < 0) wr16(slot + 0x22, 0);
}
// 0x80082554 / 0x800825c4 / 0x800826a4 / 0x80082714: +off += pct %, clamped to 0..9999.
void stat_mod_attr(u8* slot, int off, int pct) {
  const s16 v = s16_at(slot, off);
  wr16(slot + off, static_cast<s16>(v + v * pct / 100));
  if (9999 < s16_at(slot, off)) wr16(slot + off, 9999);
  if (s16_at(slot, off) < 0) wr16(slot + off, 0);
}
void stat_mod_attr_3e(u8* slot, int pct) {  // 0x80082634: changes +0x3e but clamps +0x40
  const s16 v = s16_at(slot, 0x3E);
  wr16(slot + 0x3E, static_cast<s16>(v + static_cast<s16>(v * pct / 100)));
  if (9999 < s16_at(slot, 0x40)) wr16(slot + 0x40, 9999);
  if (s16_at(slot, 0x40) < 0) wr16(slot + 0x40, 0);
}
void stat_add_26(u8* slot, s16 n) {  // 0x80082784: +0x26 += n, at most 255
  const s16 v = static_cast<s16>(s16_at(slot, 0x26) + n);
  wr16(slot + 0x26, v);
  if (0xFF < v) wr16(slot + 0x26, 0xFF);
}
void stat_mod_attr_4a(u8* slot, int amount) {  // 0x800827b4: +0x4a, at most 255
  const s16 v = s16_at(slot, 0x4A);
  wr16(slot + 0x4A, static_cast<s16>(amount < 0 ? v - amount : v + static_cast<s16>(v * amount / 100)));
  if (0xFF < s16_at(slot, 0x4A)) wr16(slot + 0x4A, 0xFF);
}

// The effect ids that only set an ability bit: unit +0x314 + 4 * word.
struct AbilityBit {
  u8 effect;
  u8 word;
  u32 mask;
};
constexpr AbilityBit kAbilityBits[] = {
    {0x69, 0, 0x1},        {0x6A, 0, 0x2},        {0x6B, 0, 0x4},        {0x6C, 0, 0x8},
    {0x6D, 0, 0x10},       {0x6E, 0, 0x20},       {0x70, 0, 0x40},       {0x71, 0, 0x80},
    {0x73, 0, 0x100},      {0x74, 0, 0x200},      {0x75, 0, 0x400},      {0x76, 0, 0x800},
    {0x77, 0, 0x1000},     {0x7B, 0, 0x2000},     {0x7C, 0, 0x4000},     {0x7D, 0, 0x4000},
    {0x7E, 0, 0x8000},     {0x85, 0, 0x10000},    {0x86, 0, 0x20000},    {0x89, 0, 0x40000},
    {0x8C, 0, 0x80000},    {0x8E, 0, 0x100000},   {0x8F, 0, 0x200000},   {0x94, 0, 0x800000},
    {0x95, 0, 0x1000000},  {0x97, 0, 0x2000000},  {0xD3, 0, 0x2000000},  {0x98, 0, 0x4000000},
    {0x99, 0, 0x8000000},  {0x9A, 0, 0x10000000}, {0x9B, 0, 0x20000000}, {0x9F, 0, 0x40000000},
    {0xA0, 0, 0x80000000}, {0xCB, 0, 0x8000000},  {0xA1, 1, 0x1},        {0xA2, 1, 0x2},
    {0xA3, 1, 0x4},        {0xA4, 1, 0x8},        {0xA5, 1, 0x10},       {0xA6, 1, 0x20},
    {0xA9, 1, 0x80},       {0xAB, 1, 0x100},      {0xAC, 1, 0x200},      {0xCC, 1, 0x200},
    {0xD0, 1, 0x200},      {0xAE, 1, 0x400},      {0xB3, 1, 0x1000},     {0xB4, 1, 0x2000},
    {0xB5, 1, 0x4000},     {0xB6, 1, 0x8000},     {0xB7, 1, 0x10000},    {0xBA, 1, 0x20000},
    {0xBB, 1, 0x40000},    {0xBF, 1, 0x80000},    {0xC2, 1, 0x100000},   {0xC4, 1, 0x200000},
    {0xC5, 1, 0x400000},   {0xCB, 1, 0x400000},   {0xC6, 1, 0x800000},   {0xC7, 1, 0x1000000},
    {0xC8, 1, 0x2000000},  {0xC9, 1, 0x4000000},  {0xCF, 1, 0x8000000},  {0xD2, 1, 0x10000000},
    {0xD5, 1, 0x20000000}, {0xD6, 1, 0x40000000}, {0xD8, 1, 0x80000000}, {0xD9, 2, 0x1},
    {0xDA, 2, 0x2},        {0xDC, 2, 0x4},        {0xDD, 2, 0x8},        {0xDE, 2, 0x10},
    {0xDF, 2, 0x20},       {0xE0, 2, 0x40},       {0xE1, 2, 0x80},       {0xE2, 2, 0x100},
    {0xE4, 2, 0x200},      {0xE5, 2, 0x400},      {0xE6, 2, 0x800},      {0xE7, 2, 0x1000},
    {0xE8, 2, 0x2000},     {0xE9, 2, 0x4000},     {0xEA, 2, 0x8000},     {0xEB, 2, 0x10000},
    {0xED, 2, 0x20000},    {0xF0, 2, 0x40000},    {0xF1, 2, 0x80000},    {0xF2, 2, 0x100000},
    {0xF3, 2, 0x200000},   {0xF4, 2, 0x400000},   {0xF8, 2, 0x800000},   {0xFA, 2, 0x1000000},
};
}  // namespace

// 0x800818dc pc_apply_equip_effects: HP / MP maxima back to the base values, the config's
// percentage modifiers (config +0x4d), then each equipped item's effect id (item field
// 0x28): ability bits in unit +0x314.. or stat changes. Each 0x86 item (also bit 0x20000)
// adds 20 % HP / MP max at the end, keeping the maxima from before in +0x304 / +0x308.
void pc_apply_equip_effects(PartyUnit* u, u8* slot, u8* rec) {
  BattleGlobals& g = bg();
  Stats* s = stats_of(slot);
  s->hp_max = s->hp_base;
  s->mp_max = s->mp_base;
  const u8* cfg = g.config;
  if (cfg[0x4D] != 0) {
    const s16 hp = rds16(cfg + 0x184);
    if (hp < 0) stat_reduce_hp_max(slot, -hp);
    else stat_mod_hp_max(slot, hp);
    const s16 mp = rds16(cfg + 0x186);
    if (mp < 0) stat_reduce_mp_max(slot, -mp);
    else stat_mod_mp_max(slot, mp);
    stat_mod_attr(slot, 0x2C, rds16(cfg + 0x188));
    stat_mod_attr(slot, 0x32, rds16(cfg + 0x18A));
    stat_mod_attr_3e(slot, rds16(cfg + 0x18C));
    stat_mod_attr(slot, 0x38, rds16(cfg + 0x18E));
    const s16 v = s16_at(slot, 0x4A);
    wr16(slot + 0x4A, static_cast<s16>(v + v * rds16(cfg + 0x192) / 100));
    if (0xFF < s16_at(slot, 0x4A)) wr16(slot + 0x4A, 0xFF);
    if (s16_at(slot, 0x4A) < 0) wr16(slot + 0x4A, 0);
  }
  int boosts = 0;  // 0x86 items
  u->effect_bits[0] = u->effect_bits[1] = u->effect_bits[2] = u->effect_bits[3] = 0;
  for (int i = 0; i < 7; ++i) {
    const s16 item = rds16(rec + 0xC + 2 * i);
    if (item == 0) continue;
    const s32 effect = item_data_field(item, 0x28);
    if (effect < 0x69 || 0xFA < effect) continue;
    for (const AbilityBit& b : kAbilityBits)
      if (b.effect == effect) u->effect_bits[b.word] |= b.mask;
    switch (effect) {
      case 0x6F: stat_mod_hp_max(slot, 0x14); stat_mod_mp_max(slot, 0x14); break;
      case 0x72: case 0xD1: stat_mod_mp_max(slot, 0x1E); break;
      case 0x78: stat_mod_attr(slot, 0x2C, 0x1E); break;
      case 0x7A: case 0x7F: stat_mod_attr(slot, 0x32, 0x1E); break;
      case 0x80: case 0x9E: stat_mod_attr(slot, 0x32, 10); break;
      case 0x81: case 0xBD: stat_mod_hp_max(slot, 0x14); break;
      case 0x83: stat_mod_mp_max(slot, -100); break;
      case 0x86: ++boosts; break;
      case 0x87: stat_mod_attr(slot, 0x2C, 0x14); stat_mod_attr_3e(slot, -5); break;
      case 0x88: stat_mod_attr(slot, 0x2C, 100); break;
      case 0x91: stat_mod_attr(slot, 0x32, 0x1E); stat_mod_attr(slot, 0x2C, -0x1E); break;
      case 0x92:
        // +20 to +0x4a for each other member this one likes (config +0x4b + id * 12 + other
        // id over 7), among the members loaded so far.
        for (PartyUnit* m : g.party_units) {
          if (m == nullptr || m == u) continue;
          if (7 < cfg[0x4B + s->id * 0xC + m->st()->id]) stat_mod_attr_4a(slot, -0x14);
        }
        break;
      case 0x96: stat_mod_attr(slot, 0x2C, 0x28); stat_mod_attr_3e(slot, 0x14); break;
      case 0xA7: stat_mod_attr(slot, 0x38, 10); break;
      case 0xAD: stat_mod_attr(slot, 0x2C, 0x1E); stat_mod_attr_3e(slot, -10); break;
      case 0xB2: stat_mod_attr(slot, 0x2C, 100); stat_mod_attr_3e(slot, -0x32); break;
      case 0xB8: stat_add_26(slot, 1); break;
      case 0xBC: stat_mod_attr(slot, 0x2C, 10); stat_mod_attr_3e(slot, 0x1E); break;
      case 0xBE: stat_mod_attr(slot, 0x44, 10); break;
      case 0xCD: case 0xD4: stat_mod_hp_max(slot, 0x1E); break;
      case 0xDB: stat_mod_hp_max(slot, -10); break;
      case 0xEC: stat_mod_mp_max(slot, 10); break;
      case 0xEE: stat_mod_mp_max(slot, 100); break;
      case 0xEF: stat_mod_hp_max(slot, -1000); break;
      case 0xF5: wr16(rec + 0xA, 0); break;
      default: break;
    }
  }
  // Last first: the maxima before each 20 % go to +0x302 + 2n / +0x306 + 2n (n = boosts..1),
  // i.e. acc_hp_max[n - 1] / acc_mp_max[n - 1]. (Only the two accessories carry 0x86; a
  // third would overwrite acc_mp_max[0] / effect_timer[0] on the console.)
  for (; boosts != 0; --boosts) {
    if (boosts <= 2) {
      u->acc_hp_max[boosts - 1] = static_cast<s16>(s->hp_max);
      u->acc_mp_max[boosts - 1] = s->mp_max;
    }
    stat_mod_hp_max(slot, 0x14);
    stat_mod_mp_max(slot, 0x14);
  }
  s->guts[2] = s->guts[1];
  if (s->hp_max < s->hp) s->hp = s->hp_max;
  if (s->mp_max < s->mp) s->mp = s->mp_max;
}

// 0x80083d40 tex_rects_offset: moves a texture block's rectangles by (dx, dy).
void tex_rects_offset(u8* block, s32 dx, s32 dy) {
  const int n = static_cast<u16>(rds16(block + 0xA));
  for (int i = 0; i < n; ++i) {
    u8* r = block + 0xC + i * 8;
    r[0] = static_cast<u8>(r[0] + dx);
    r[1] = static_cast<u8>(r[1] + dy);
  }
}

// 0x80083db0 killer_move_data_entry: the archive entry of the data of assigned killer move
// phase - 1 (record +0xcc..+0xcf; byte = move + 1), per character (and weapon variant).
s32 killer_move_data_entry(int phase, const PartyUnit* u) {
  const int k = phase - 1;
  const u8 b = u->record[0xCC + k];
  if (b == 0) return 0;
  const int idx = b - 1;
  const bool v0 = u->weapon_variant == 0;
  switch (u->st()->id - 1) {
    case 0:  // Claude
      if (199 < u->killer_charge[k] && idx == 1) g_claude_upgrades |= 1;
      if (99 < u->killer_charge[k] && idx == 7) g_claude_upgrades |= 2;
      if ((g_claude_upgrades & 1) != 0 && idx == 1) return v0 ? 0x8C0 : 0x872;
      if ((g_claude_upgrades & 2) != 0 && idx == 7) return v0 ? 0x8C7 : 0x878;
      return battle_s16((v0 ? 0x80085198u : 0x80085180u) + static_cast<u32>(idx) * 2);
    case 3: return battle_s16(0x80084F20 + static_cast<u32>(idx) * 2);                            // Bowman
    case 4: return battle_s16((v0 ? 0x80084F34u : 0x80084F48u) + static_cast<u32>(idx) * 2);  // Dias
    case 5: return battle_s16(0x80084F5C + static_cast<u32>(idx) * 2);                            // Precis
    case 6: return battle_s16((v0 ? 0x80084F84u : 0x80084F70u) + static_cast<u32>(idx) * 2);  // Ashton
    case 8: return battle_s16(0x80084F0C + static_cast<u32>(idx) * 2);                            // Opera
    case 9: return battle_s16(0x80084F98 + static_cast<u32>(idx) * 2);                            // Ernest
    case 0xB: return battle_s16(0x80084FA8 + static_cast<u32>(idx) * 2);                          // Chisato
    default: return 0;
  }
}

// Phases 1-4 of one member (party_loader_update): the killer-move data blocks.
void party_load_moves(int slot) {
  BattleGlobals& g = bg();
  PartyUnit* u = g.party_units[slot];
  for (int phase = 1; phase <= 4; ++phase) {
    const s32 entry = killer_move_data_entry(phase, u);
    if (entry <= 0) continue;
    std::vector<u8> packed(static_cast<size_t>(g.loader->entry_size(entry)));
    if (packed.empty()) continue;
    g.loader->load_sync(packed.data(), entry, 0);
    auto unpacked = disc::slz::unpack_block(packed, 0, 0, nullptr);
    if (!unpacked || unpacked->size() < 0x38) continue;
    const int k = phase - 1;
    g_move_blocks[slot][k] = std::move(*unpacked);
    const std::vector<u8>& block = g_move_blocks[slot][k];
    const u8* d = block.data();
    u->alt_layout[k] = d + rd32(d);
    u->alt_anims[k] = d + rd32(d + 4);
    u->alt_frames[k] = d + rd32(d + 8);
    u->alt_sheet[k] = d + rd32(d + 0xC);
    u->attack_data[k][0] = d + 0x10;
    u->attack_data[k][1] = d + 0x18;
    u->move_data[k] = d + 0x20;
    for (int i = 0; i < 4; ++i) u->move_parts[k][i] = d + rd32(d + 0x28 + i * 4);
    // The move's palette to CLUT row 0x1cf + slot * 4 + phase (its cels stream through
    // the unit's TexCache).
    const size_t sheet = rd32(d + 0xC);
    const size_t clut = sheet + rd32(d + sheet);
    u16 work[256];
    clut_build_pair(work, block, clut + 4, static_cast<int>(rd32(d + clut)));
    u->alt_clut[k] = upload_clut256(work, 0, slot * 4 + phase + 0x1CF);
  }
}

// Phase 0 of one member: the data entry, the unit, its textures.
void party_load_member(int slot) {
  BattleGlobals& g = bg();
  u8* pslot = g.party_slot[slot];
  const int character = rds16(pslot);
  const s16 weapon = rds16(g.party_record[slot] + 0xC);
  int variant = item_data_field(weapon, 0x2A);
  if (variant == 7) variant = 0;
  const s32 entry = battle_s16(0x80084EA8 + character * 4 + variant * 2);
  pc_construct_slot(slot);
  PartyUnit* u = g.party_units[slot];
  pc_calc_equipment_stats(pslot, u->record);
  u->item_value_sum = equip_sum_field(0, 10, 999999);
  u->recovery = static_cast<u8>(equip_sum_field(0, 0x1A, 999999) + 0x10);
  u->weapon_variant = static_cast<u8>(item_data_field(weapon, 0x2A));
  u->base_reach = battle_s16(0x80084ED8 + character * 4 + (u->weapon_variant != 0 ? 2 : 0));
  // The data entry (the code entry entry + 1 is native code here).
  std::vector<u8> packed(static_cast<size_t>(g.loader->entry_size(entry)));
  if (entry <= 0 || packed.empty()) {
    todo("party data entry for character " + std::to_string(character));
    return;
  }
  g.loader->load_sync(packed.data(), entry, 0);
  auto unpacked = disc::slz::unpack_block(packed, 0, 0, nullptr);
  if (!unpacked || unpacked->size() < 0x20) return;
  g_member_data[slot] = std::move(*unpacked);
  u8* d = g_member_data[slot].data();
  u->model_layout = d + rd32(d);
  u->model_anims = d + rd32(d + 4);
  u->model_frames = d + rd32(d + 8);
  u->model_sheet = d + rd32(d + 0xC);
  u->model_210 = d + rd32(d + 0x10);
  u->sheet_alt = d + rd32(d + 0x14);
  u->model_218 = d + rd32(d + 0x18);
  u->weapon_sheet = d + rd32(d + 0x1C);
  // The body texture: the second block if it is a plain (not streamed) one, else the first.
  const u8* body = nullptr;
  if ((u->model_sheet[8] & 2) == 0 && rd32(d + 0xC) != 0) body = u->model_sheet;
  if ((u->sheet_alt[8] & 2) == 0 && rd32(d + 0x14) != 0) body = u->sheet_alt;
  const s16 tex_x = battle_s16(0x80084FB8 + slot * 2);
  const s16 tex_y = battle_s16(0x80084FC0 + slot * 2);
  if (body != nullptr) {
    const size_t bo = static_cast<size_t>(body - d);
    u16 work[256];
    clut_build_pair(work, g_member_data[slot], bo + rd32(body) + 4, static_cast<int>(rd32(body + rd32(body))));
    u->clut = upload_clut256(work, 0, slot + 0x1F1);
    const size_t img = bo + rd32(body + 4);
    u->tpage = upload_texture(g_member_data[slot], img + 4, 0, 0, tex_x, tex_y, static_cast<u16>(rds16(d + img)),
                              static_cast<u16>(rds16(d + img + 2)));
  }
  AnimSet* a = u->animset;
  a->tpage[0] = a->tpage[1] = a->tpage[3] = u->tpage;
  a->clut[0] = a->clut[1] = u->clut;
  u8* weapon_block = d + rd32(d + 0x1C);
  if ((weapon_block[8] & 2) == 0 && rd32(d + 0x1C) != 0) {
    const size_t wo = static_cast<size_t>(weapon_block - d);
    u16 work[256];
    clut_build_pair(work, g_member_data[slot], wo + rd32(weapon_block) + 4,
                    static_cast<int>(rd32(weapon_block + rd32(weapon_block))));
    u->weapon_clut = upload_clut256(work, 0, slot + 0x1ED);
    const s16 wx = battle_s16(0x80084FC8 + slot * 2), wy = battle_s16(0x80084FD0 + slot * 2);
    const size_t img = wo + rd32(weapon_block + 4);
    upload_texture(g_member_data[slot], img + 4, 0, 0, wx, wy, static_cast<u16>(rds16(d + img)),
                   static_cast<u16>(rds16(d + img + 2)));
    a->tpage[2] = 0x19;
    a->clut[2] = u->weapon_clut;
    tex_rects_offset(weapon_block, wx - battle_s16(0x80084FC8), wy - battle_s16(0x80084FD0));
  }
  if (body != nullptr) {
    const s16 area[4] = {static_cast<s16>(tex_x + body[0xC] + 1), static_cast<s16>(tex_y + body[0xD]),
                         static_cast<s16>(tex_x + 0x3F), static_cast<s16>(tex_y + 0xFE)};
    u->texcache_shared = 0;
    u->texcache = texcache_create(area);
  }
  psyq::DrawSync(0);
  party_load_moves(slot);
}

}  // namespace

// ---- The loader task -----------------------------------------------------------------

struct PartyLoader : Task {
  void update() override;  // party_loader_update 0x80082cdc
};

void PartyLoader::update() {
  BattleGlobals& g = bg();
  // The console spreads the reads over frames; the host reads every member now.
  for (int slot = 0; slot < 4; ++slot) {
    if (g.party_slot[slot] != nullptr && rds16(g.party_slot[slot]) > 0) party_load_member(slot);
  }
  for (PartyUnit* u : g.party_units) {
    if (u == nullptr) continue;
    pc_apply_equip_effects(u, u->stats, u->record);
    const s32 lvl = u->skill_level(0x2C);
    u->run_speed += (lvl << 14) / 10;
    task_register(u, 1);
  }
  g.item_data_freed = true;  // the console frees g_item_table here (inventory +0x820 dangles)
  battle_separate_units();
  g.state = kStateLoaded;
  battle_debug_loaded();  // (debugging: SS_BATTLE_RNG_SKIP)
  task_unregister(this);
  g.party_loader.reset();  // deletes this
}

Task* party_loader_create() {
  BattleGlobals& g = bg();
  g.party_loader = std::make_unique<PartyLoader>();
  return g.party_loader.get();
}

void party_note_stage_file(const std::vector<u8>& raw) { g_stage_file = raw; }

void party_reset() {
  g_stage_file.clear();
  for (auto& m : g_members) m.reset();
  for (auto& d : g_member_data) d.clear();
  for (auto& m : g_move_blocks)
    for (auto& d : m) d.clear();
  g_claude_upgrades = 0;
  pc_modules_reset();
}

// ---- PartyUnit -------------------------------------------------------------------------

PartyUnit::PartyUnit() {
  is_enemy = 0;
  sfx_hit = 0x4A;
  sfx_crit = 0x48;
  sfx_guard = 0x4F;
  jump_threshold = static_cast<s32>(0xFFF88000);
  // The default model set and the four killer-move alternates (+0x23c..+0x24c).
  for (AnimSet*& m : model_sets) m = animset_alloc();
  animset = model_sets[0];
}

void PartyUnit::update() {
  // pc_update 0x80034fe8.
  if (removed != 0) {
    // Fled: the marker and bound effects go; a pending action is dropped.
    if (low_hp_marker != nullptr) {
      sprite_free(low_hp_marker);
      low_hp_marker = nullptr;
    }
    interrupt();
    release_effects();
    if (action != 0) {
      unit_release_spell_data(this);
      action_kind = 0;
      begin_action();
    }
    return;
  }
  BattleGlobals& g = bg();
  if (g.camera != nullptr && g.camera->target != this) camera_focus = 0;
  pc_talk_update(this);  // 0x800350cc: the talk pose ends with its clip (party_talk.cpp)
  if (ignore_units != 0) {
    const s32 zero[3] = {0, 0, 0};
    if (unit_check_move(this, zero, 7) == nullptr) ignore_units = 0;
  }
  unit_apply_global_blend(this);
  think();
  unit_update_base(this);
}

void PartyUnit::model_default() {
  // pc_model_default 0x80035168.
  animset = model_sets[0];
  model_apply();
}

void PartyUnit::model_apply() {
  // pc_model_apply 0x800351a0: the member's model blocks into the current set.
  AnimSet* a = animset;
  if (a != nullptr) {
    a->layout = model_layout;
    a->anims = model_anims;
    a->sheets[0] = model_sheet;
    a->frames[0] = model_frames;
    a->sheets[1] = sheet_alt;
    a->frames[1] = model_210;
    a->sheets[2] = weapon_sheet;
    a->frames[2] = model_218;
  }
  pool = bg().prim_pool_a;
}

void PartyUnit::appear_update() {
  // pc_appear_update 0x800362a8.
  BattleGlobals& g = bg();
  model_default();
  anim = 0;
  if (talk_voice != 0) anim = 0xF;
  if (action == 5) anim = 7;
  if (counters[0] < 0x28) {
    blend = 2;
    colour_mode = 2;
    rgb[0] = static_cast<u8>(~static_cast<u8>(counters[0] * 3) + (g.battle_frames & 3) * -0x10);
    counters[0] += g.frame_ticks;
  } else {
    colour_mode = 0;
    blend = 0;
    if (g.special_voice_set == 0 && g.intro_voice_gate == 0) {
      appear = static_cast<u8>(appear & 0xFD);
      g.state = kStateFighting;
      g.controlled_slot = g.config[0x41];
      g.leader_slot = g.config[0x41];
      g.flags = static_cast<u8>(g.flags & 0xFE);
    }
  }
  animate(0);
}

int PartyUnit::animate(u32 anim_flags) {
  // pc_animate 0x8003532c.
  BattleGlobals& g = bg();
  const s32 shadow = unit_ground_follow(this);
  unit_draw_model(this);
  const u8 saved = clut_offset;
  if (g.state > kStateLoaded) {
    const u8 status = st()->status;
    if (status & 8) clut_offset = 8;
    if (status & 4) {
      if (g.state != kStateFighting) {
        model_default();
        anim = 0;
      }
      clut_offset = 5;
      colour_mode = 0;
    }
    if ((status & 2) && g.state != kStateFighting) {
      model_default();
      anim = 0;
    }
  }
  u32 hold = (g.flags != 0 || attack_recovery != 0 || anim_flags != 0) ? 1 : 0;
  if (appear & 2) hold = 0;
  const int f = static_cast<s16>(bobj_anim_draw(this, hold));
  clut_offset = saved;
  if (f != -1) {
    if (!unit_is_down(this)) {
      unit_draw_shadow(this, shadow);
    } else {
      --layer;
      unit_draw_shadow(this, shadow);
      ++layer;
    }
  }
  return f;
}

int PartyUnit::skill_level(int skill) {
  // pc_skill_level 0x8003b794: equipment overrides for skills 0x27 / 0x22, else the
  // character record's skill level (+0x5c + skill).
  if (status_enabled == 0 || record == nullptr) return 0;
  if (skill == 0x27) {
    if (status_test(0, 0x2000)) return 5;
    if (status_test(1, 0x400)) return 6;
    if (status_test(1, 0x800)) return 3;
    if (status_test(1, 0x80000)) return 7;
    if (status_test(2, 0x40000)) return 7;
  }
  if (skill == 0x22 && status_test(0, 0x40000)) return 3;
  const s32 v = static_cast<s8>(record[0x5C + skill]);
  return v >= 0 ? v : 0;
}

int PartyUnit::status_test(int word, u32 mask) {
  // pc_has_effect 0x8003b8f4: the equipment ability bits; none while +0x135 is clear (the
  // party AI scores its attacks that way).
  if (status_enabled == 0) return 0;
  return (effect_bits[word & 3] & mask) != 0;
}

}  // namespace ss::game::battle
