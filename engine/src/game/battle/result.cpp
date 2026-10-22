#include "game/battle/result.h"

#include <cstring>
#include <memory>

#include "game/battle/bsfx.h"
#include "game/battle/camera.h"
#include "game/battle/combat.h"
#include "game/battle/input.h"
#include "game/battle/loaders.h"
#include "game/battle/sprite.h"
#include "game/battle/unit.h"
#include "game/battle/voice.h"
#include "game/boot/rng.h"
#include "game/common/screen.h"
#include "game/field/post_battle.h"
#include "game/field/script_vm.h"
#include "runtime/libgpu.h"

namespace ss::game::battle {
namespace {

ResultScreen* g_result = nullptr;  // 0x8006fab8 g_result_screen

s16 rd16(const u8* p) {
  s16 v;
  std::memcpy(&v, p, 2);
  return v;
}
s32 rd32(const u8* p) {
  s32 v;
  std::memcpy(&v, p, 4);
  return v;
}
void wr16(u8* p, s32 v) {
  const s16 s = static_cast<s16>(v);
  std::memcpy(p, &s, 2);
}
void wr32(u8* p, s32 v) { std::memcpy(p, &v, 4); }

u8 ovl1981_u8(u32 address) {
  const u8* p = ovl1981_data(address);
  return p != nullptr ? *p : 0;
}

// The 0-terminated list of learned-skill message ids (g_learned_list 0x80088024).
u16 g_learned[25];

// ---- Level-up attribute gains (0x80085da4-0x80085ed4) -------------------------------

void add_attr(LevelUpCtx* ctx, int which, s16 n) {
  u8* s = ctx->slot;
  switch (which) {
    case 0:  // attr3: the base only
      wr16(s + 0x2A, rd16(s + 0x2A) + n);
      break;
    case 1:  // attr7
      wr16(s + 0x42, rd16(s + 0x42) + n);
      wr16(s + 0x44, rd16(s + 0x44) + n);
      break;
    case 2:  // attr6
      wr16(s + 0x3C, rd16(s + 0x3C) + n);
      wr16(s + 0x3E, rd16(s + 0x3E) + n);
      break;
    case 3:  // attr4
      wr16(s + 0x30, rd16(s + 0x30) + n);
      wr16(s + 0x32, rd16(s + 0x32) + n);
      break;
    case 4:  // attr8, each capped at 255
      wr16(s + 0x48, rd16(s + 0x48) + n);
      if (0xFF < rd16(s + 0x48)) wr16(s + 0x48, 0xFF);
      wr16(s + 0x4A, rd16(s + 0x4A) + n);
      if (0xFF < rd16(s + 0x4A)) wr16(s + 0x4A, 0xFF);
      break;
    default: {  // the record's attr9, capped at 9999
      u8* r = ctx->rec;
      wr16(r + 6, rd16(r + 6) + n);
      if (9999 < rd16(r + 6)) wr16(r + 6, 9999);
      wr16(r + 8, rd16(r + 8) + n);
      if (9999 < rd16(r + 8)) wr16(r + 8, 9999);
      break;
    }
  }
}

// 0x8008590c: {level, move} pairs, 0-terminated, learned up to the slot's level.
const u16* learn_from_table(LevelUpCtx* ctx, u32 table, s16 msg_base) {
  int n = 0;
  for (;; table += 2) {
    const u8 level = ovl1981_u8(table);
    if (level == 0 || rd16(ctx->slot + 0x26) < static_cast<s16>(level)) break;
    const int idx = ovl1981_u8(table + 1) - 1;
    u8* flag = ctx->rec + 0x3C + idx;
    if (*flag == 0) {
      *flag = 1;
      wr16(ctx->rec + 0x8C + idx * 2, 0);  // move proficiency
      g_learned[n++] = static_cast<u16>(msg_base + idx);
    }
  }
  g_learned[n] = 0;
  return g_learned;
}

// 0x800859b8: as learn_from_table, the flag index being the spell's place in the class
// list (its length from class_spell_count 0x8006b1ab by the slot's class byte +3).
const u16* learn_spells_from_table(LevelUpCtx* ctx, u32 table, u32 class_spells, s16 msg_base) {
  int n = 0;
  const u8* count_p = battle_data(0x8006B1AB + ctx->slot[3]);
  const int count = count_p != nullptr ? *count_p : 0;
  for (;; table += 2) {
    const u8 level = ovl1981_u8(table);
    if (level == 0 || rd16(ctx->slot + 0x26) < static_cast<s16>(level)) break;
    const u8 spell = ovl1981_u8(table + 1);
    for (int i = 0; i < count; ++i) {
      const u8* c = battle_data(class_spells + static_cast<u32>(i));
      if (c == nullptr || *c != spell) continue;
      u8* flag = ctx->rec + 0x3C + i;
      if (*flag == 0) {
        *flag = 1;
        wr16(ctx->rec + 0x8C + i * 2, 0);
        g_learned[n++] = static_cast<u16>(msg_base + (spell - 1));
      }
      break;
    }
  }
  g_learned[n] = 0;
  return g_learned;
}

// ---- The pages -------------------------------------------------------------------------

void prompt_reset(ResultScreen* rs) {  // 0x800872b4
  rs->confirmed = 0;
  rs->scroll = 0;
  rs->timer = 0;
  // (A prompt that was never confirmed stays where it was: the console spawns a new one.)
  rs->prompt = sprite_spawn_ui(0x15, 0, nullptr);
}

void window_grow(ResultScreen* rs, int w, int h) {  // 0x800861bc
  const s32 ticks = bg().frame_ticks;
  rs->target_w = static_cast<s16>(w);
  rs->target_h = static_cast<s16>(h);
  if (rs->w < w - ticks * 0x18) {
    rs->w = static_cast<s16>(rs->w + ticks * 0x18);
    rs->x = static_cast<s16>(rs->x - ticks * 0xC);
  } else {
    rs->x = static_cast<s16>(0xA0 - w / 2);
    rs->w = static_cast<s16>(w);
    if (rs->h < h - ticks * 0x18) {
      rs->h = static_cast<s16>(rs->h + ticks * 0x18);
      rs->y = static_cast<s16>(rs->y - ticks * 0xC);
    } else {
      rs->y = static_cast<s16>(0x60 - h / 2);
      rs->h = static_cast<s16>(h);
      ++rs->state;
    }
  }
}

void window_shrink(ResultScreen* rs) {  // 0x800867b0
  const s32 ticks = bg().frame_ticks;
  if (rs->h < ticks * 0xC + 0x10) {
    rs->y = 0x60;
    rs->h = 0x10;
    if (rs->w < ticks * 0x24) {
      rs->x = 0x98;
      rs->w = 0x10;
      ++rs->state;
    } else {
      rs->w = static_cast<s16>(rs->w - ticks * 0x18);
      rs->x = static_cast<s16>(rs->x + ticks * 0xC);
    }
  } else {
    rs->h = static_cast<s16>(rs->h - ticks * 0xC);
    rs->y = static_cast<s16>(rs->y + ticks * 6);
  }
}

void wait_button(ResultScreen* rs) {  // 0x800868dc
  BattleGlobals& g = bg();
  if (rs->confirmed != 0) {
    rs->scroll += g.frame_ticks * 4;
    if (0x1B < rs->scroll) ++rs->state;
    return;
  }
  if ((g.flags & 0x20) != 0) return;
  if (rs->prompt != nullptr)
    sprite_set_screen_pos(rs->prompt, static_cast<s16>(rs->x + rs->target_w - 0x18),
                          static_cast<s16>(rs->y + rs->target_h - 0x16));
  rs->timer = static_cast<u16>(rs->timer + g.frame_ticks);
  u16 confirm;
  std::memcpy(&confirm, g.config, 2);
  if ((battle_input().pressed & confirm) != 0 || 0x168 < static_cast<s16>(rs->timer)) {
    bsfx_play_ui(2);
    rs->confirmed = 1;
    sprite_free(rs->prompt);
    rs->prompt = nullptr;
  }
}

// x and y of text_draw's end position (y as the console passes it: the high half).
int end_x(u32 end) { return static_cast<int>(end & 0xFFFF); }
int end_y(u32 end) { return static_cast<int>(end >> 16); }

u32 draw_number(ResultScreen* rs, int x, int y, s32 value, int width) {
  char digits[24];
  u8 glyphs[48];
  field::format_decimal(digits, value, width, ' ');
  text_from_ascii(digits, glyphs);
  return text_draw(rs, x, y, glyphs, 0);
}

void award_exp_money(ResultScreen* rs) {  // 0x80086290
  BattleGlobals& g = bg();
  rs->fol = 0;
  rs->exp = 0;
  for (int i = 0; i < 8; ++i) {
    if (g.enemy_units[i] == nullptr) continue;
    rs->fol += g.enemy_units[i]->fol;
    rs->exp += rd32(g.enemy_stats[i] + 0x10);
  }
  if (g.reward_flags & 1) rs->fol <<= 1;
  if (g.reward_flags & 4) rs->exp <<= 1;
  if (g.config[0x4D] != 0) rs->exp += rs->exp * rd16(g.config + 0x194) / 100;
  const s32 money = rd32(g.config + 0x18) + rs->fol;
  wr32(g.config + 0x18, money);
  if (999999999 < money) wr32(g.config + 0x18, 999999999);
  // Shared among the members still up (KO, stone and the like excluded; the fled count).
  int members = 0;
  for (int i = 0; i < 4; ++i)
    if (g.party_units[i] != nullptr && (g.party_slot[i][2] & 7) == 0) ++members;
  rs->exp = members < 1 ? 0 : rs->exp / members;
  for (int i = 0; i < 4; ++i) {
    if (g.party_units[i] == nullptr || (g.party_slot[i][2] & 7) != 0) continue;
    // The member's EXP bonus is written back, so it compounds from member to member.
    rs->exp += rs->exp * rd16(g.party_record[i] + 0x1C) / 100;
    u8* slot = g.party_slot[i];
    wr32(slot + 0x10, rd32(slot + 0x10) + rs->exp);
    if (999999999 < rd32(slot + 0x10)) wr32(slot + 0x10, 999999999);
  }
  ++rs->state;
  // The after-battle recovery: attribute 9's final value / 8 percent of HP and MP.
  for (int i = 0; i < 4; ++i) {
    PartyUnit* u = g.party_units[i];
    if (unit_is_down(u)) continue;
    s32 pct = rd16(g.party_record[i] + 0xA);
    if (pct < 0) pct += 7;
    pct >>= 3;
    const s32 hp = (-rd32(g.party_slot[i] + 0x18) * pct) / 100;
    const s32 mp = (-static_cast<s32>(rd16(g.party_slot[i] + 0x22)) * pct) / 100;
    if (hp != 0) damage_popup_spawn(u, hp, -0x2000, 0);
    unit_damage_hp(u, hp);
    if (mp != 0) damage_popup_spawn(u, mp, 0x2000, 1);
    unit_damage_mp(u, mp);
  }
}

void draw_exp_money(ResultScreen* rs) {  // 0x8008668c
  window_text_begin(rs);
  text_draw_id(rs, 0, -rs->scroll, 0, 0);
  text_draw_id(rs, 0x40, -rs->scroll, 1, 0);
  text_draw_id(rs, 0x40, 0xE - rs->scroll, 2, 0);
  draw_number(rs, 0xA0, -rs->scroll, rs->exp, 7);
  draw_number(rs, 0xA0, 0xE - rs->scroll, rs->fol, 7);
  window_text_end(rs);
  wait_button(rs);
}

void draw_defeat_message(ResultScreen* rs) {  // 0x80086864
  const u8 outcome = bg().outcome;
  const int id = outcome == kDefeat ? 3 : (outcome == kTimeUp ? 0x13 : 0x12);
  window_text_begin(rs);
  text_draw_id(rs, 8, -rs->scroll, id, 0);
  window_text_end(rs);
  wait_button(rs);
}

void roll_drops(ResultScreen* rs) {  // 0x800869fc
  BattleGlobals& g = bg();
  s32 luck = 0;
  int standing = 0;
  for (int i = 0; i < 4; ++i) {
    if (unit_is_down(g.party_units[i])) continue;
    ++standing;
    luck += rd16(g.party_record[i] + 4);
  }
  if (standing != 0) luck /= standing;
  if (luck < 0) luck += 0xF;
  rs->drop_count = 0;
  for (int e = 0; e < 8; ++e) {
    EnemyUnit* u = g.enemy_units[e];
    if (u == nullptr) continue;
    for (int k = 0; k < 2; ++k) {
      if (u->drop_item[k] == 0) continue;
      u32 chance = static_cast<u32>(u->drop_chance[k]) + static_cast<u32>(luck >> 4) - 7;
      if (g.reward_flags & 2) chance *= 2;
      if (0 < static_cast<s32>(chance) && rng_range(0x100) < chance) {
        rs->drops[rs->drop_count++] = u->drop_item[k];
        field::inventory_add(g.inventory, static_cast<s16>(u->drop_item[k]), 1, 1);
        break;  // one drop per enemy
      }
    }
  }
  rs->drop_row = 0;
  ++rs->state;
  prompt_reset(rs);
}

void draw_drops(ResultScreen* rs) {  // 0x80086bdc
  if (rs->drop_count == 0) {
    ++rs->state;
    return;
  }
  window_text_begin(rs);
  for (int row = rs->drop_row; row < rs->drop_row + 4 && row < rs->drop_count; ++row) {
    const int y = (row - rs->drop_row) * 0xE - rs->scroll;
    if (0x1C <= y) break;
    const u32 end = text_draw_id(rs, 8, y, static_cast<s16>(rs->drops[row]) + 2999, 0);
    text_draw_id(rs, end_x(end) + 4, end_y(end), 5, 0);
  }
  window_text_end(rs);
  const u8 before = rs->state;
  wait_button(rs);
  if (before != rs->state) {
    rs->drop_row = static_cast<u8>(rs->drop_row + 2);
    if (rs->drop_row < rs->drop_count) {
      --rs->state;
      prompt_reset(rs);
    }
  }
}

void compute_levelups(ResultScreen* rs) {  // 0x80086d30
  BattleGlobals& g = bg();
  auto ctx = std::make_unique<LevelUpCtx>();
  exp_table_build(ctx->table);
  for (int i = 0; i < 4; ++i) {
    rs->levels[i] = 0;
    for (s32& id : rs->learned[i]) id = 0;
  }
  for (int i = 0; i < 4; ++i) {
    PartyUnit* u = g.party_units[i];
    if (unit_is_down(u)) continue;
    rs->levels[i] = levelup_apply(ctx.get(), u->stats, u->record);
    rs->sp[i] = static_cast<s16>(ctx->table[0]);
    if (rs->levels[i] == 0) continue;
    const u16* ids = levelup_learn_skills(ctx.get(), u->stats, u->record);
    for (int k = 0; k < 24 && ids[k] != 0; ++k) rs->learned[i][k] = static_cast<s16>(ids[k]);
  }
  rs->member = 0;
  rs->line = 0;
  ++rs->state;
  prompt_reset(rs);
}

void draw_levelups(ResultScreen* rs) {  // 0x80086ec8
  BattleGlobals& g = bg();
  while (rs->levels[rs->member] == 0) {
    rs->member = static_cast<u8>(rs->member + 1);
    if (3 < rs->member) {
      ++rs->state;
      return;
    }
  }
  const int m = rs->member;
  PartyUnit* u = g.party_units[m];
  if (g.camera->mode == 6) camera_reset_mode(g.camera);
  camera_request_mode(g.camera, 5, u);
  window_text_begin(rs);
  if (rs->voice_done[m] == 0) {
    voice_play_result(u);
    rs->voice_done[m] = 1;
  }
  int lines = 2;
  for (int k = 0; k < 24 && rs->learned[m][k] != 0; ++k) ++lines;
  for (int line = rs->line; line < rs->line + 0x18 && line < lines; ++line) {
    const int y = (line - rs->line) * 0xE - rs->scroll;
    if (0x1B < y) break;
    u32 end;
    int suffix;
    if (line == 0) {
      u8 name[48];
      text_from_ascii(reinterpret_cast<const char*>(u->record + 0x24), name);
      end = text_draw(rs, 8, y, name, 0);
      end = text_draw_id(rs, end_x(end) + 6, end_y(end), 6, 0);
      if (1 < rs->levels[m]) end = draw_number(rs, end_x(end) + 6, end_y(end), rs->levels[m], 0);
      suffix = 7;
    } else if (line == 1) {
      end = draw_number(rs, 8, y, rs->sp[m], 0);
      suffix = 0x32;
    } else {
      end = text_draw_id(rs, 8, y, rs->learned[m][line - 2], 0);
      suffix = 8;
    }
    text_draw_id(rs, end_x(end) + 6, end_y(end), suffix, 0);
  }
  window_text_end(rs);
  const u8 before = rs->state;
  wait_button(rs);
  if (before == rs->state) return;
  rs->line = static_cast<u8>(rs->line + 2);
  --rs->state;
  prompt_reset(rs);
  if (rs->line < lines) return;
  rs->member = static_cast<u8>(rs->member + 1);
  if (rs->member < 4) {
    rs->line = 0;
    return;
  }
  ++rs->state;
}

void result_release(ResultScreen* rs) {
  rs->release();  // vtable [4]
  g_result = nullptr;
  delete rs;  // (out of the task list: safe from its own update)
}

// ---- battle_fade_task (0x800872f8-0x8008741c) ------------------------------------------

struct BattleFadeTask : Task {
  u32 colour = 0;      // +0x08: the band's edge colour (subtracted)
  s32 band = 0;        // +0x0c: half height of the band
  RenderContext layer; // +0x10: a 0x90-byte arena and a one-entry OT

  BattleFadeTask() {
    task_register(this, 0);
    render_ctx_create_arena(&layer, 0x90);
    render_ctx_create_ot(&layer, 1);
    layer.ot_head = screen_clear_ot(layer.ot);
    ot_layer_add(&layer);
  }
  ~BattleFadeTask() override {
    task_unregister(this);
    ot_layer_remove(&layer);
    if (layer.arena >= 0) screen_destroy_arena(layer.arena);
    if (layer.ot >= 0) screen_destroy_ot(layer.ot);
  }
  void update() override;
};

void BattleFadeTask::update() {
  BattleGlobals& g = bg();
  ot_layer_to_front(&layer);
  auto* quads = screen_alloc_prims<psyq::POLY_G4>(layer.arena, 2);
  if (quads == nullptr) return;
  // A black band opening from the middle line: white (fully subtracted) at y 0x78, the
  // edge colour at 0x78 +- band; then the edges darken too.
  for (int i = 0; i < 2; ++i) {
    psyq::POLY_G4* p = &quads[i];
    psyq::setPolyG4(p);
    p->code = 0x3A;
    p->r0 = p->g0 = p->b0 = 0xFF;
    p->r1 = p->g1 = p->b1 = 0xFF;
    std::memcpy(&p->r2, &colour, 3);
    std::memcpy(&p->r3, &colour, 3);
    p->x0 = p->x2 = 0;
    p->x1 = p->x3 = 0x140;
    p->y0 = p->y1 = 0x78;
    p->y2 = p->y3 = static_cast<s16>(i == 0 ? 0x78 + band : 0x78 - band);
    psyq::addPrim(layer.ot_head, p);
  }
  if (auto* m = screen_alloc_prims<psyq::DR_MODE>(layer.arena, 1)) {
    psyq::SetDrawMode(m, 0, 1, 0x40, nullptr);  // subtractive
    psyq::addPrim(layer.ot_head, m);
  }
  const s32 ticks = g.frame_ticks;
  band += ticks * 2;
  if (0x78 < band) {
    band = 0x78;
    colour += static_cast<u32>(ticks) * 0x40404;
    if (0xFFFFFF < colour) {
      delete this;  // (battle_fade_task_dtor with 3; out of the task list first)
      g.state = kStateLeave;
    }
  }
}

}  // namespace

// ---- Level-ups (0x80085518-0x80085ed4) ---------------------------------------------------

s32* exp_table_build(s32* table) {
  s32 sum = 0;
  for (int level = 1; level < 0xFF; ++level) {
    sum += level < 100 ? (level * level + 1) * ((level - 1) / 10 + 5) : (level * level + 1) * 0xF;
    table[level] = sum;
  }
  return table;
}

void levelup_gain_hp_mp_sp(LevelUpCtx* ctx) {
  BattleGlobals& g = bg();
  u8* s = ctx->slot;
  const s16 level = rd16(s + 0x26);
  s32 hp;
  if (level < 0x32) {
    hp = static_cast<s32>(rng_range(3));
    hp = (level * 3) / 2 + (hp + 3) * 8;
  } else {
    hp = static_cast<s32>(rng_range(3)) * 8 + 0x56;
  }
  for (int off : {0x1C, 0x14, 0x18}) {
    wr32(s + off, rd32(s + off) + hp);
    if (9999 < rd32(s + off)) wr32(s + off, 9999);
  }
  const s16 mp = static_cast<s16>(rng_range(3) + 4);
  for (int off : {0x24, 0x20, 0x22}) {
    wr16(s + off, rd16(s + off) + mp);
    if (999 < rd16(s + off)) wr16(s + off, 999);
  }
  const s32 v = (rd16(s + 0x26) * 3) / 10;
  s32 r = v;
  if (2 < v) r = static_cast<s32>(rng_range(static_cast<u32>(v)));
  s32 sp = (r - v / 2) + rd16(s + 0x26);
  if (sp < 1) sp = 1;
  if (g.config[0x4D] != 0) sp += (sp * rd16(g.config + 0x196)) / 100;
  u8* rec = ctx->rec;
  wr16(rec + 0x1A, rd16(rec + 0x1A) + sp);
  const s32 before = ctx->table[0];
  ctx->table[0] = before + sp;
  if (999 < rd16(rec + 0x1A)) {
    // Past 999 the excess is not counted.
    ctx->table[0] = (before + sp + 999) - rd16(rec + 0x1A);
    wr16(rec + 0x1A, 999);
  }
}

int levelup_apply(LevelUpCtx* ctx, u8* slot, u8* rec) {
  if (rd16(slot + 0x26) == 0xFF || rd16(slot + 0x28) == 0xFF) return 0;
  ctx->slot = slot;
  ctx->rec = rec;
  ctx->table[0] = 0;
  int gained = 0;
  for (;;) {
    const s16 level = rd16(slot + 0x26);
    // (At level 0xff the console compares against table[255], its slot pointer, and then
    // stops on the level test; the port tests the level first.)
    if (level < 0 || 0xFE < level) break;
    if (rd32(slot + 0x10) < ctx->table[level]) break;
    if (rd16(slot + 0x28) == 0xFF) break;
    ++gained;
    const s16 id = rd16(slot);
    levelup_gain_hp_mp_sp(ctx);
    // Six attribute gains: rng_range(range) + base from the character's row of
    // levelup_growth 0x80087f94 (attr3, attr7, attr6, attr4, attr8, record attr9).
    const u32 row = 0x80087F94 + static_cast<u32>((id - 1) * 0xC);
    for (int k = 0; k < 6; ++k) {
      const s32 n = static_cast<s32>(rng_range(ovl1981_u8(row + k * 2))) + ovl1981_u8(row + k * 2 + 1);
      add_attr(ctx, k, static_cast<s16>(n));
    }
    wr16(slot + 0x26, rd16(slot + 0x26) + 1);
    wr16(slot + 0x28, rd16(slot + 0x28) + 1);
  }
  return gained;
}

const u16* levelup_learn_skills(LevelUpCtx* ctx, u8* slot, u8* rec) {
  ctx->slot = slot;
  ctx->rec = rec;
  switch (rd16(slot) - 1) {
    case 0: return learn_from_table(ctx, 0x80087DD0, 5000);    // Claude
    case 1: return learn_spells_from_table(ctx, 0x80087E74, 0x8006B1B8, 1000);  // Rena
    case 2: return learn_spells_from_table(ctx, 0x80087EA4, 0x8006B1E4, 1000);  // Celine
    case 3: return learn_from_table(ctx, 0x80087DE8, 0x141E);  // Bowman
    case 4: return learn_from_table(ctx, 0x80087E34, 0x1450);  // Dias
    case 5: return learn_from_table(ctx, 0x80087E24, 0x1482);  // Precis
    case 6: return learn_from_table(ctx, 0x80087E10, 0x14B4);  // Ashton
    case 7: return learn_spells_from_table(ctx, 0x80087EFC, 0x8006B1D0, 1000);  // Leon
    case 8: return learn_from_table(ctx, 0x80087E00, 0x1518);  // Opera
    case 9: return learn_from_table(ctx, 0x80087E4C, 0x154A);  // Ernest
    case 10: return learn_spells_from_table(ctx, 0x80087ED0, 0x8006B1FC, 1000);  // Noel
    case 11: return learn_from_table(ctx, 0x80087E60, 0x15AE);  // Chisato
    default:
      for (u16& id : g_learned) id = 0;
      return g_learned;
  }
}

// ---- ResultScreen ----------------------------------------------------------------------

ResultScreen::ResultScreen() : BWindow(0x80) {}

void ResultScreen::open_screen() {
  open(0xA0, 0x60, 0, 0x10);
  for (u8& v : voice_done) v = 0;
  ot_layer_to_front(bg().prim_pool_b);
  prompt_reset(this);
}

void ResultScreen::draw() {
  BattleGlobals& g = bg();
  const u8 outcome = g.outcome;
  if (outcome == kEscaped) {
    result_release(this);
    return;
  }
  if (outcome == kVictory) {
    switch (state) {
      case 0: window_grow(this, 0xE8, 0x2A); break;
      case 1:
        award_exp_money(this);
        draw_exp_money(this);
        break;
      case 2: draw_exp_money(this); break;
      case 3: roll_drops(this); break;
      case 4: draw_drops(this); break;
      case 5: compute_levelups(this); break;
      case 6: draw_levelups(this); break;
      case 7: window_shrink(this); break;
      case 8:
        // voice_stop_all 0x8007f76c: the voice slots are not ported.
        result_release(this);
        return;
      default: break;
    }
  } else if (outcome < 5) {
    switch (state) {
      case 0: window_grow(this, 0x90, 0x1C); break;
      case 1: draw_defeat_message(this); break;
      case 2: window_shrink(this); break;
      case 3: result_release(this); return;
      default: break;
    }
  }
  window_draw_frame(this);
}

ResultScreen* battle_result_screen() { return g_result; }

void result_screen_start() {
  // (slz_unpack of #1981 into 0x80085518: the code is native, its tables are in g.ovl1981.)
  g_result = new ResultScreen();
  g_result->open_screen();
}

void result_screen_free() {
  if (g_result == nullptr) return;
  delete g_result;  // the window closes itself
  g_result = nullptr;
}

void battle_fade_start() { new BattleFadeTask(); }

}  // namespace ss::game::battle
