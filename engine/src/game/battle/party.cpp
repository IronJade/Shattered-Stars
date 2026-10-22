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
#include "game/battle/camera.h"
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
std::array<std::vector<u8>, 4> g_member_data;   // g_member_buf / g_member_data: the unpacked phase-0 block

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
  g_members[slot] = std::make_unique<PartyUnit>();
  PartyUnit* u = g_members[slot].get();
  g.party_units[slot] = u;
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

// 0x800818dc pc_apply_equip_effects: HP / MP maxima back to the base values, the
// config's percentage modifiers, then each equipped item's effect id (item field 0x28).
void pc_apply_equip_effects(PartyUnit* u, u8* slot, u8* rec) {
  BattleGlobals& g = bg();
  Stats* s = stats_of(slot);
  s->hp_max = s->hp_base;
  s->mp_max = s->mp_base;
  if (g.config[0x4D] != 0) todo("pc_apply_equip_effects: config stat modifiers (config +0x4d)");
  u->effect_bits[0] = u->effect_bits[1] = u->effect_bits[2] = u->effect_bits[3] = 0;
  for (int i = 0; i < 7; ++i) {
    const s16 item = rds16(rec + 0xC + 2 * i);
    if (item == 0) continue;
    const s32 effect = item_data_field(item, 0x28);
    if (effect != 0) todo("pc_apply_equip_effects: equipment effect " + std::to_string(effect));
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
  // TODO(port): phases 1-4, the killer-move data (killer_move_data_entry 0x80083db0).
  if (u->record[0xCC] | u->record[0xCD] | u->record[0xCE] | u->record[0xCF])
    todo("killer-move data (party loader phases 1-4)");
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
  task_unregister(this);
  g.party_loader.reset();  // deletes this
}

Task* party_loader_create() {
  BattleGlobals& g = bg();
  g.party_loader = std::make_unique<PartyLoader>();
  return g.party_loader.get();
}

void party_reset() {
  for (auto& m : g_members) m.reset();
  for (auto& d : g_member_data) d.clear();
}

// ---- PartyUnit -------------------------------------------------------------------------

PartyUnit::PartyUnit() {
  is_enemy = 0;
  sfx_hit = 0x4A;
  sfx_crit = 0x48;
  sfx_guard = 0x4F;
  jump_threshold = static_cast<s32>(0xFFF88000);
  animset = animset_alloc();
  for (AnimSet*& m : model_sets) m = nullptr;
  model_sets[0] = animset;
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
  // pc_has_effect 0x8003b8f4: the equipment ability bits.
  return (effect_bits[word & 3] & mask) != 0;
}

}  // namespace ss::game::battle
