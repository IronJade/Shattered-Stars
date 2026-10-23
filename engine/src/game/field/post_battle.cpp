#include "game/field/post_battle.h"

#include <array>
#include <cstring>
#include <memory>
#include <vector>

#include "game/boot/sound.h"
#include "game/common/menu_core.h"
#include "game/common/menu_scene.h"
#include "game/common/menu_stats.h"
#include "game/common/party.h"
#include "game/common/text_rules.h"
#include "game/field/callways.h"
#include "game/field/fade.h"
#include "game/field/load_queue.h"
#include "game/field/map.h"
#include "game/field/message.h"
#include "game/field/message_internal.h"
#include "game/field/screen.h"
#include "game/field/script_vm.h"
#include "game/field/text_panel.h"
#include "game/field/worldmap.h"
#include "runtime/libgpu.h"

namespace ss::game::field {
namespace {

u32 g_ability_bits = 0;              // 0x80076190
std::vector<u8> g_report;            // 0x800761a0: 8 x 0x30 (empty = none)
u8 g_auto_equipped = 0;              // 0x8007619e
u8 g_report_showing = 0;             // 0x80076274: a member's page is up
u8 g_report_member = 0;              // 0x80076275
u8 g_report_opened = 0xFF;           // 0x80076276: member the window was opened for
u8 g_report_drawn = 0xFF;            // 0x80076277: member whose item names are open
std::unique_ptr<std::array<TextPanel, 4>> g_panels;  // 0x800752e4
s16 g_side_entry = 0;                // 0x800762ec
std::vector<u8> g_side_buffer;       // 0x800762e8
u8 g_side_done = 0;                  // 0x800762e4

s16 rd16s(const u8* p) {
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
  const s16 x = static_cast<s16>(v);
  std::memcpy(p, &x, 2);
}

// 0x8003121c: the walking flags (Callways +0xb: effect 0x84 regenerates, 0x8b gains
// EXP) and the ability mask from every member's equipment.
u32 stat_calc_scan_effects(const menu::StatCalc& calc) {
  u32 bits = 0;
  Callways* cw = callways();
  for (int slot = 0; slot < 8; ++slot) {
    if (cw != nullptr) cw->member_flags[slot] = 0;
    party_select(slot);
    if (party_selected_char() == 0) continue;
    for (int i = 0; i < 7; ++i) {
      const int item = char_get_equip(i);
      if (item == 0) continue;
      switch (calc.item_stat(item, 2)) {
        case 0x79: bits |= 1; break;
        case 0x8D: bits |= 2; break;
        case 0xB0: bits |= 4; break;
        case 0xB1: bits |= 8; break;
        case 0xCE: bits |= 0x10; break;
        case 0x84: if (cw != nullptr) cw->member_flags[slot] |= 1; break;
        case 0x8B: if (cw != nullptr) cw->member_flags[slot] |= 2; break;
        default: break;
      }
    }
  }
  return bits;
}

// 0x8003b834: attribute 'which' (ATK, AC, HIT, AVD, MAG) of the selected member: its
// equipment's item fields 3-7 plus the slot's base value.
s32 stat_calc_attr_total(const menu::StatCalc& calc, int which) {
  static const int kBase[5] = {0x2C, 0x32, 0x3E, 0x38, 0x44};
  s32 sum = 0;
  for (int i = 0; i < 7; ++i) sum += static_cast<s32>(calc.item_stat(char_get_equip(i), 3 + which));
  return sum + rd16s(party_selected_slot() + kBase[which]);
}

void stat_calc_snapshot_attrs(const menu::StatCalc& calc, u8* out) {  // 0x8003b7b8
  for (int i = 0; i < 5; ++i) wr16(out + i * 2, stat_calc_attr_total(calc, i));
}

void stat_calc_snapshot_equip(u8* out) {  // 0x8003b9fc
  for (int i = 0; i < 7; ++i) wr16(out + i * 2, char_get_equip(i));
}

// 0x80030fc8: for the selected member, the best new (inventory bit 15) wearable item per
// slot (weapon by ATK, the rest by AC) replaces what is worn when it beats it.
bool stat_calc_equip_best_new(const menu::StatCalc& calc) {
  const u32 mask = char_equip_mask();
  s32 best[5];
  int chosen[5] = {};
  best[0] = static_cast<s32>(calc.item_stat(char_get_equip(0), 3));
  for (int s = 1; s < 5; ++s) best[s] = static_cast<s32>(calc.item_stat(char_get_equip(s), 4));
  const u8* inv = party_inventory();
  for (int i = 0; i < 0x400; ++i) {
    const u16 e = static_cast<u16>(inv[i * 2] | inv[i * 2 + 1] << 8);
    const int id = e & 0x3FF;
    if (id == 0 || id >= 0x338 || static_cast<s16>(e) >= 0) continue;
    if ((calc.item_stat(id, 0) & mask) == 0) continue;
    int slot = -1;
    switch (calc.item_stat(id, 1)) {
      case 1: case 2: case 7: slot = 0; break;
      case 8: slot = 1; break;
      case 9: slot = 2; break;
      case 10: slot = 3; break;
      case 0xB: slot = 4; break;
      default: break;
    }
    if (slot == -1) continue;
    const s32 v = static_cast<s32>(calc.item_stat(id, slot == 0 ? 3 : 4));
    if (best[slot] < v) {
      best[slot] = v;
      chosen[slot] = id;
    }
  }
  bool changed = false;
  for (int s = 0; s < 5; ++s) {
    if (chosen[s] == 0) continue;
    const int old = char_get_equip(s);
    char_set_equip(s, 0);
    if (old != 0) inventory_give_one(old);
    char_set_equip(s, chosen[s]);
    inventory_take_one(chosen[s]);
    changed = true;
  }
  return changed;
}

// 0x80030e6c: members without manual equipment (record +0x3a bit 0) get the best new
// items; the report keeps their attributes and equipment before (+0, +0x14) and after
// (+0xa, +0x22).
bool stat_calc_auto_equip(menu::StatCalc& calc, u8* report) {
  std::memset(report, 0, 0x180);
  bool any = false;
  for (int slot = 0; slot < 8; ++slot) {
    party_select(slot);
    if (party_selected_char() == 0 || (party_selected_record()[0x3A] & 1) != 0) continue;
    u8* e = report + slot * 0x30;
    calc.member();
    stat_calc_snapshot_attrs(calc, e);
    stat_calc_snapshot_equip(e + 0x14);
    if (stat_calc_equip_best_new(calc)) any = true;
    calc.member();
    stat_calc_snapshot_attrs(calc, e + 0xA);
    stat_calc_snapshot_equip(e + 0x22);
  }
  inventory_clear_new_flags();
  return any;
}

void quiet_window(MsgWindow* w) {
  w->cancel_closes = 0;
  w->fade_in = 0;
  w->show_prompt = 0;
  w->speed = 1;
  w->page_new_line = 0;
  w->timer = 0;
  w->draw_frame = 0;
  w->draw_body = 0;
}

// 0x8004b7a8: the report window's draw callback (window 6). Windows 8 / 9 carry the
// member's name and the heading, 10-14 / 15-19 the equipment before and after (grey
// when unchanged), and the four text panels the attribute columns.
void report_draw(int x, int y) {
  FieldGlobals& g = globals();
  if (g_report.empty()) return;
  const u8* e = g_report.data() + g_report_member * 0x30;
  auto at = [e](int i) { return rd16s(e + i * 2); };
  if (field_side_strings() == nullptr && g_side_done != 0) {
    field_install_side_text(g_side_buffer.data(), g_side_buffer.size());  // 0x80053b44 (streaming back on)
    g_side_buffer.clear();
  }
  psyq::RECT clip;
  MsgWindow* window6 = msg_window(6);
  if (window6 != nullptr) msg_window_get_clip(window6, &clip);
  if (!g_panels) return;
  MsgSystem* msg = msg_system();
  const u8* sys = g.system_strings;
  const text::Layout& lay = text::layout(text::lang());  // FR / DE widen the heading (NA 0x14 cells)
  if (g_report_member != g_report_drawn) {
    const s32 character = rd16s(g.party + g_report_member * 0x60) - 1;
    std::memcpy(g.field_flags + 0x38, &character, 4);
    msg_window_open(msg, -x, -(y + 0xD), 0x24, 1, sys + rd32(sys + 0xC), 8, 1);
    msg_window_open(msg, -x, -(y + 0x1A), lay.report_heading_w, 1, sys + rd32(sys + 0x38), 9, 1);
    for (int n = 8; n < 10; ++n)
      if (MsgWindow* w = msg_window(n)) quiet_window(w);
  }
  const u8* names = field_side_strings();
  if (g_side_font != nullptr && g_report_member != g_report_drawn) {
    for (int i = 0; i < 10; ++i) {
      const s16 item = i < 5 ? at(10 + i) : at(0xC + i);
      const int dx = i < 5 ? lay.report_items_dx : 0x86;  // NA 0
      const u8* p = names;
      if (p == nullptr || read_u16(p) == 0xFFFF) continue;
      while (read_u16(p) != 0xFFFF && static_cast<int>(read_u16(p) - 2999) != item) p += 4;
      if (read_u16(p) == 0xFFFF) continue;
      msg_window_open(msg, -(x + dx + 0x1E), -((i % 5) * 0xD + y + 0x2B), lay.report_item_w, 1, names + read_u16(p + 2),
                      10 + i, 1);  // NA width 0x14
      MsgWindow* w = msg_window(10 + i);
      if (w == nullptr) continue;
      if (i > 4) msg_window_set_text_colour(w, at(10 + i % 5) == at(0x11 + i % 5) ? 3 : 0xF);
      quiet_window(w);
      w->justify = 0;
    }
    g_report_drawn = g_report_member;
  }
  for (int n = 8; n < 20; ++n) {
    if (MsgWindow* w = msg_window(n)) {
      if (window6 != nullptr) msg_window_set_clip(w, &clip);
      w->button_advance = 0;
    }
  }

  // Icons beside the columns (SPRT at OT 0x819): UI atlas records 0x30-0x34 on the left;
  // the right-hand ones reload the atlas base and so all use record 0 (0x8004bce0).
  // Under the full-screen draw area, the window's clip restored on top.
  Screen* screen = field_screen();
  auto* full = screen->alloc_prims<psyq::DR_AREA>();
  const psyq::RECT whole{0, static_cast<s16>(screen->current->draw.clip.y), 0x140, 0xE0};
  psyq::SetDrawArea(full, &whole);
  psyq::addPrim(screen->ot_entry(0x819), full);
  s32 row = y + 0x35;
  for (int i = 0; i < 5; ++i) {
    for (int col = 0; col < 2; ++col) {
      const u8* uv = g.ui_atlas + (col == 0 ? (0x30 + i) * 8 : 0);
      auto* sp = screen->alloc_prims<psyq::SPRT>();
      psyq::setSprt(sp);
      psyq::setShadeTex(sp, true);
      sp->x0 = static_cast<s16>(x + (col == 0 ? 8 : 0x9E));
      sp->y0 = static_cast<s16>(row);
      sp->u0 = uv[2];
      sp->v0 = uv[3];
      sp->w = static_cast<s16>(uv[4] - uv[2]);
      sp->h = static_cast<s16>(uv[5] - uv[3]);
      sp->clut = col == 0 ? 0x7F82 : 0x7FC0;
      psyq::addPrim(screen->ot_entry(0x819), sp);
    }
    row += 0xD;
  }
  auto* mode = screen->alloc_prims<psyq::DR_MODE>();
  psyq::SetDrawMode(mode, 0, 1, 0x16, nullptr);
  psyq::addPrim(screen->ot_entry(0x819), mode);

  // The columns: labels, the values before, arrows, the values after (red when lower,
  // green when higher).
  std::array<TextPanel, 4>& panels = *g_panels;
  const int py = y + 0x71;
  text_panel_open(&panels[0], x, py, 0, 0);
  text_panel_open(&panels[1], x + 0x30, py, 0, 0);
  text_panel_open(&panels[2], x + 0x50, py, 0, 0);
  text_panel_open(&panels[3], x + 0x58, py, 0, 0);
  for (TextPanel& p : panels) {
    p.text_x = 0;
    p.text_y = 0;
    p.max_width = 0x10;
  }
  text_panel_print(&panels[0], reinterpret_cast<const char*>(field_data(0x80072FC0)), 2);
  char digits[16];
  for (int i = 0; i < 5; ++i) {
    format_decimal(digits, at(i), 4, ' ');
    text_panel_print(&panels[1], digits, 0);
    panels[1].text_y += 0xC;
    panels[1].text_x = 0;
  }
  text_panel_print(&panels[2], reinterpret_cast<const char*>(field_data(0x80072FD4)), 0);
  for (int i = 0; i < 5; ++i) {
    format_decimal(digits, at(5 + i), 4, ' ');
    const int palette = at(5 + i) > at(i) ? 1 : (at(5 + i) != at(i) ? 2 : 0);
    text_panel_print(&panels[3], digits, palette);
    panels[3].text_y += 0xC;
    panels[3].text_x = 0;
  }
  if (window6 != nullptr) {
    auto* back = screen->alloc_prims<psyq::DR_AREA>();
    psyq::SetDrawArea(back, &clip);
    psyq::addPrim(screen->ot_entry(0x819), back);
  }
}

}  // namespace

u32 field_ability_bits() { return g_ability_bits; }

char* format_decimal(char* out, s32 value, int width, char pad) {
  char* p = out;
  if (value < 0) {
    *p++ = '-';
    value = -value;
  }
  if (width < 1) {
    char tmp[21];
    char* t = tmp + 20;
    int n = 0;
    do {
      *--t = static_cast<char>('0' + value % 10);
      value /= 10;
      ++n;
    } while (value > 0);
    for (int i = 0; i < n; ++i) *p++ = t[i];
    *p = '\0';
    return out;
  }
  p += width;
  *p = '\0';
  int written = 0;
  do {
    *--p = static_cast<char>('0' + value % 10);
    value /= 10;
    ++written;
  } while (value != 0 && written < width);
  while (written++ < width) *--p = pad;
  return out;
}

u32 party_scan_equip_effects() {
  // A StatCalc, then a MenuSession (0x8003253c / 0x80032630: binds the library to the
  // handoff blocks); the session goes first, then the StatCalc recomputes every member.
  // The item table is the field program's (menu library host).
  menu::set_host(menu::Host::Field);
  menu::StatCalc calc;
  menu::MenuSession session;
  return stat_calc_scan_effects(calc);
}

int party_auto_equip(u8* report) {
  menu::set_host(menu::Host::Field);
  menu::StatCalc calc;
  menu::MenuSession session;
  return stat_calc_auto_equip(calc, report) ? 1 : 0;
}

void inventory_clear_new_flags() {
  u8* inv = party_inventory();
  for (int i = 0; i < 0x400; ++i) {
    if (((inv[i * 2] | inv[i * 2 + 1] << 8) & 0x3FF) != 0) inv[i * 2 + 1] &= 0x7F;
  }
}

void inventory_give_one(int item) {
  if (item != 0) inventory_add(party_inventory(), item, 1, 1);
}

void inventory_take_one(int item) {
  if (item == 0) return;
  const int index = inventory_find(party_inventory(), item);
  if (index >= 0) inventory_remove_at(party_inventory(), index, 1);
}

void field_side_load(s16 entry) {
  FieldGlobals& g = globals();
  field_load_buffer().clear();  // 0x80075738
  g_side_entry = entry;
  load_queue_set_streaming(load_queue(), false);
  preload_save_table();
  if (g.field_flags[0x68] != 0) {
    world_cancel_block_load();
    world().streaming = 0;
  }
  load_queue_reset(load_queue());
  preload_release(-1);
  g_side_buffer.assign(static_cast<size_t>(g.loader->entry_size(entry)), 0);
  g_side_done = 0;
  load_queue_push(load_queue(), entry, g_side_buffer.data(), &g_side_done, 0);
}

bool field_side_load_done() { return g_side_done != 0; }
std::vector<u8>& field_side_buffer() { return g_side_buffer; }
s16 field_side_entry() { return g_side_entry; }
void field_side_clear_done() { g_side_done = 0; }
void field_free_equip_report() { g_report.clear(); }
void field_side_free() {
  if (g_side_buffer.empty()) return;
  if (g_side_done == 0) load_queue_cancel_entry(load_queue(), g_side_entry);
  g_side_buffer.clear();
}

bool field_post_battle_check(Field*) {
  FieldGlobals& g = globals();
  if (g.show_battle_results == 0) return false;
  g_ability_bits = party_scan_equip_effects();
  g_report.assign(0x180, 0);
  g_auto_equipped = static_cast<u8>(party_auto_equip(g_report.data()));
  if (g_auto_equipped == 0) {
    g.show_battle_results = 0;
    g_report.clear();
    return false;
  }
  g_report_opened = 0xFF;
  g_report_drawn = 0xFF;
  g_report_showing = 0;
  g_report_member = 0;
  g.field_flags[0x10] = 0xFF;
  g.field_flags[0x11] = 0xFF;
  field_side_load(0xFC2);
  field_free_side_text();  // 0x800762f0 / 0x800762f8 / 0x800762f4 = 0
  g.show_battle_results = 0;
  g_panels = std::make_unique<std::array<TextPanel, 4>>();
  for (TextPanel& p : *g_panels) p.frame_visible = 0;
  return true;
}

void field_update_equip_report(Field* f) {
  FieldGlobals& g = globals();
  auto scan = [&] {
    for (int m = g_report_member; m < 8; ++m) {
      const u8* e = g_report.data() + m * 0x30;
      bool changed = false;
      for (int i = 0; i < 5; ++i) changed |= rd16s(e + 0x14 + i * 2) != rd16s(e + 0x22 + i * 2);
      if (rd16s(g.party + m * 0x60) > 0 && changed) {
        g_report_showing = 1;
        g_report_member = static_cast<u8>(m);
        return;
      }
    }
    f->state = 0x10;
    g_report_member = static_cast<u8>(g_report_member - 1);
  };
  if (g_report_showing == 0) {
    scan();
  } else {
    if (g_report_opened == 0xFF) {
      sound_play_sfx(g.sound, 0xE, 0, 0x7F40);
      const u8* sys = g.system_strings;
      // ORIGINAL BUG (original_bugs.md W1): the window is 0xc lines high, so the fifth
      // attribute row (MAG, at y + 0x71 + 4 * 0xc) falls under its clip:
      //   msg_window_open(msg_system(), 1, 2, 0x26, 0xC, sys + rd32(sys + 8), 6, 1);
      msg_window_open(msg_system(), 1, 2, 0x26, 0xD, sys + rd32(sys + 8), 6, 1);
      msg_window_resize(msg_system(), 6, 0x26, 1);
      if (MsgWindow* w = msg_window(6)) {
        w->draw_callback = report_draw;
        w->cancel_closes = 0;
        w->fade_in = 0;
        w->button_advance = 0;
        w->speed = 1;
        w->page_new_line = 0;
        w->close_callback = field_end_overlay_event;
      }
      g_report_opened = g_report_member;
    }
    if ((g.pressed[0] & read_u16(g.config)) != 0) {
      g_report_showing = 0;
      g.pressed[0] = 0;
      g_report_member = static_cast<u8>(g_report_member + 1);
    }
    if (g_report_showing == 0) scan();
  }
  fade_draw_grey(field_fade(), 0x30, 1, 2, 0x80C);
}

void field_update_equip_report_close(Field* f) {
  FieldGlobals& g = globals();
  if (MsgWindow* w = msg_window(6)) {
    const int state = msg_window_get_state(w);
    if (state == -1 || state == 9 || state == 10) return;
    msg_window_begin_close(w);
    return;
  }
  field_end_overlay_event();
  const s16 leader = g.game_state[0x24];  // DAT_80075710 is an int*: +9 words
  std::memcpy(g.field_flags + 0x10, &leader, 2);
  preload_restore_table();
  world().streaming = 1;
  f->state = 0;
}

void field_end_overlay_event() {
  g_report.clear();
  if (g_side_done == 0) load_queue_cancel_entry(load_queue(), 0xFC2);
  field_free_side_text();
  g_side_buffer.clear();
  post_battle_destroy_panels();
  for (int n = 8; n < 20; ++n) msg_window_destroy(msg_system(), n);
  load_queue_set_streaming(load_queue(), true);
}

void post_battle_reset() {
  g_ability_bits = 0;
  g_side_buffer.clear();
  g_auto_equipped = 0;
  g_report.clear();
  g_panels.reset();
}

void post_battle_destroy_panels() { g_panels.reset(); }

}  // namespace ss::game::field
