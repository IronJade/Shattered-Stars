#include "game/field/menu_equip.h"

#include <cstring>
#include <vector>

#include "game/common/menu_core.h"
#include "game/common/menu_gfx.h"
#include "game/common/text_rules.h"
#include "game/common/menu_scene.h"
#include "game/common/menu_widget.h"
#include "game/common/party.h"
#include "game/field/field.h"
#include "game/field/script_vm.h"

namespace ss::game::field {
namespace {

constexpr int kEquipCode = 0xBC4;   // page 4's code overlay (at 0x8007e000)

const char* equip_text(u32 address) {
  const u8* p = menu::page_table(kEquipCode, address);
  return p ? reinterpret_cast<const char*>(p) : "";
}
u8 equip_byte(u32 address) {
  const u8* p = menu::page_table(kEquipCode, address);
  return p ? *p : 0;
}

bool script_flag(int n) { return ((globals().script_flags[n >> 3] >> (n & 7)) & 1) != 0; }   // 0x80055ecc

u8* inventory() { return party_inventory(); }
u16 inv_entry(int index) { return read_u16(inventory() + index * 2); }

// The slot's label from the window art (v 0x58-0x60): x 0x80081258 + i, width 0x80081264 + i
// (7 and 8: the accessories' "1" and "2").
menu::PolyFT4Raw slot_icon(int i) {
  const u8 u = equip_byte(0x80081258 + i);
  const u8 w = equip_byte(0x80081264 + i);
  menu::PolyFT4Raw q{};
  q.x0 = 0, q.y0 = 1, q.u0 = u, q.v0 = 0x58, q.clut = 0x7891;
  q.x1 = w, q.y1 = 1, q.u1 = static_cast<u8>(u + w), q.v1 = 0x58, q.tpage = 0x16;
  q.x2 = 0, q.y2 = 9, q.u2 = u, q.v2 = 0x60;
  q.x3 = w, q.y3 = 9, q.u3 = static_cast<u8>(u + w), q.v3 = 0x60;
  return q;
}

// 0x8008028c: slots 0-4 emptied into the inventory.
void unequip_all() {
  for (int slot = 0; slot < 5; ++slot) {
    const s32 old = char_get_equip(slot);
    char_set_equip(slot, 0);
    if (old != 0) inventory_add(inventory(), old, 1, 1);
  }
}

// 0x800800c4: the best weapon (ATK, field 8) and armour (AC, field 0x10) per slot of what the
// member can wear (field 4), from the inventory with the old pieces back in it.
void best_equip() {
  const u32 mask = char_equip_mask();
  s32 best[5] = {};
  s32 pick[5] = {};
  unequip_all();
  for (int i = 0; i < 0x400; ++i) {
    const int item = inv_entry(i) & 0x3FF;
    if (item == 0) continue;
    if ((static_cast<u32>(menu::item_data_get(item, 4)) & mask) == 0) continue;
    int slot = -1;
    switch (menu::item_data_get(item, 0x2A)) {
      case 1:
      case 2:
      case 7: slot = 0; break;
      case 8: slot = 1; break;
      case 9: slot = 2; break;
      case 10: slot = 3; break;
      case 11: slot = 4; break;
      default: break;
    }
    if (slot == -1) continue;
    const s32 value = menu::item_data_get(item, slot == 0 ? 8 : 0x10);
    if (best[slot] < value) {
      best[slot] = value;
      pick[slot] = item;
    }
  }
  for (int slot = 0; slot < 5; ++slot) {
    if (pick[slot] == 0) continue;
    char_set_equip(slot, pick[slot]);
    const int at = inventory_find(inventory(), pick[slot]);
    if (at >= 0) inventory_remove_at(inventory(), at, 1);
  }
}

class EquipPicker;

// ==== The page (Panel, 0xbc bytes, vtable 0x80081188; 0x8007e000) ========================
class EquipmentPage final : public menu::Panel {
 public:
  EquipmentPage();
  void on_child_done(int id, int state, int code) override;   // [10] 0x8007e378
  void on_cursor(int n) override;                             // [12] 0x8007e178
  void on_confirm(int n) override;                            // [13] 0x8007e204
  void on_button2(int n) override;                            // [15] 0x8007e2b8
  void on_member_changed() override;                          // [21] 0x8007e114
  void draw_member() override;                                // [23] 0x8007e3dc

  void refresh();                     // 0x8007e790
  void preview(int item);             // 0x8007e1d4

 private:
  s16 choice_ = 0;                    // +0x34
  menu::TextBox* right_ = nullptr;    // +0x98
  menu::TextBox* slots_ = nullptr;    // +0x9c
  s16 before_[5] = {};                // +0xa4: ATK, AC, HIT, AVD, MAG before the change
  s32 mode_ = 0;                      // +0xb0: 1 while a slot's list is open
  s32 slot_ = 0;                      // +0xb4
  s32 item_ = 0;                      // +0xb8: the slot's piece when the list opened
};

// ==== The list of pieces for a slot (Dialog, 0x6c bytes, vtable 0x80081028; 0x80080648) ===
// A scrolling list of inventory entries (and "none" first) that fit the slot and the
// member; the cursor tries each one on so the page shows the change.
class EquipPicker final : public menu::Dialog {
 public:
  explicit EquipPicker(EquipmentPage* page);
  void draw_item(menu::TextBox* box, int slot, int index) override;   // [8] 0x80080a78
  void on_cursor(int n) override;                                     // [12] 0x8008080c
  void on_confirm(int n) override;                                    // [13] 0x80080894
  void on_cancel(int n) override;                                     // [14] 0x80080a2c
  void on_button3(int n) override;                                    // [16] 0x800809cc
  void set_slot(int slot);                                            // 0x80080724

 private:
  void build(int slot);               // 0x80080c9c
  int item_at(int row) const;         // the row's piece (0 for none)

  EquipmentPage* page_ = nullptr;     // +0x08
  s16 choice_ = 0;                    // +0x34
  std::vector<s16> entries_;          // +0x50: inventory indices (-1 none); 0x80080bd8-0x80080e24
  s32 character_ = 0;                 // +0x58
  menu::TextBox* box_ = nullptr;      // +0x5c
  s32 slot_ = 0;                      // +0x60
  s32 old_ = 0;                       // +0x64
};

// ==== Options (Dialog, 0x58 bytes, vtable 0x800810d0; 0x800802fc) ==========================
class EquipOptions final : public menu::Dialog {
 public:
  explicit EquipOptions(EquipmentPage* page);
  void frame() override;                  // [9] 0x800804e4
  void on_confirm(int n) override;        // [13] 0x80080574
  void on_left_right(int n) override;     // [19] 0x80080528

 private:
  EquipmentPage* page_ = nullptr;         // +0x08
  s16 choice_ = 0;                        // +0x34
  s16 ew_ = 0;                            // +0x50: 0 ON, 1 OFF (bit 15: not lit)
  menu::TextBox* box_ = nullptr;          // +0x54
};

// ==== EquipmentPage ==========================================================================

EquipmentPage::EquipmentPage() {
  menu::MenuGlobals& g = menu::globals();
  menu::strings_set(g.data.data());
  menu::TextRenderer::set_default_font(menu::strings_block(1), menu::strings_block(0));
  create_help();
  create_member_box(0);
  create_info_box();
  slots_ = new menu::TextBox;
  slots_->set_rect(0, 0x58, 0xAC, 0x70, 0);
  right_ = new menu::TextBox;
  right_->set_rect(0x9C, 0x58, 0x9C, 0x70, 0);
  mode_ = 0;
  refresh();
  create_viewer(0x118, 0x4A, 0x18);   // 0x800d4b04
  help_flags = 7;
}

void EquipmentPage::refresh() {
  menu::TextBox* box = slots_;
  box->clear();
  if (mode_ == 0) {
    // The slot labels and the pieces worn.
    box->pen(0x10, 0xC, 0xE);
    for (int i = 0; i < 9; ++i) {
      if (i == 7) slots_->pen(0x30, 0x52, 0xE);
      box->add_icon(slot_icon(i));
    }
    box->set_font_group(menu::item_strings(1), menu::item_strings(0), 3);
    box->pen(0x38, 8, 0xE);
    box->set_highlight_next(&choice_);
    for (int slot = 0; slot < 7; ++slot) box->add_message(static_cast<s16>(char_get_equip(slot)));
    cursor->clear_groups();
    cursor->add_grid(&choice_, 0, 1, 7, 0, 0xC, 0, 0xE, slots_);
  } else if (mode_ == 1) {
    // The slot being changed, its old piece and the attributes before / after.
    box->pen(8, 0xC, 0xE);
    box->add_icon(slot_icon(slot_));
    if (static_cast<u32>(slot_ - 5) < 2) {
      slots_->pen(0x28, 0xC, 0xE);
      box->add_icon(slot_icon(slot_ + 2));
    }
    box->set_font_group(menu::item_strings(1), menu::item_strings(0), 3);
    box->pen(0x30, 8, 0xE);
    box->add_message(static_cast<s16>(item_));
    box->set_highlight_next(nullptr);
    {
      menu::StrBuf sb;
      sb.set(equip_text(0x80080FA8));   // ATK AC HIT AVD MAG
      box->pen(0x10, 0x30, 0xC);
      slots_->add_string(sb.str());
      const char arrow[2] = {static_cast<char>(0xE0), 0};
      sb.set(equip_text(0x80080FC0));
      for (int i = 0; i < 5; ++i) {
        sb.put(equip_text(0x80080FC4));   // %5d
        sb.put_int(before_[i]);
        sb.put(arrow);
        sb.put(equip_text(0x80080F94));   // new line
      }
      slots_->pen_at(0x38, 0x30);
      slots_->add_string(sb.str());
    }
    stats.member();
    {
      menu::StrBuf sb;
      sb.set(equip_text(0x80080FC8));   // ""
      s16 after[5];
      stats.snapshot_attrs(after);
      for (int i = 0; i < 5; ++i) {
        if (before_[i] == after[i]) sb.put(equip_text(0x80080FC0));   // colour 1
        if (after[i] < before_[i]) sb.put(equip_text(0x80080FCC));     // colour 2: worse
        if (before_[i] < after[i]) sb.put(equip_text(0x80080FD0));     // colour 3: better
        sb.put(equip_text(0x80080FC4));
        sb.put_int(after[i]);
        sb.put(equip_text(0x80080F94));
      }
      slots_->pen_at(0x60, 0x30);
      slots_->add_string(sb.str());
    }
  }
}

void EquipmentPage::on_cursor(int) { set_viewer_item(char_get_equip(choice_)); }

void EquipmentPage::preview(int item) {
  set_viewer_item(item);
  refresh();
}

void EquipmentPage::on_member_changed() {
  choice_ = 0;
  draw_member();
  refresh();
  on_cursor(-1);
}

void EquipmentPage::on_confirm(int n) {
  slot_ = choice_;
  item_ = char_get_equip(slot_);
  stats.member();
  stats.snapshot_attrs(before_);
  mode_ = 1;
  auto* picker = new EquipPicker(this);
  open_child(1, picker);
  picker->set_slot(choice_);
  cursor->set_flags(0x8001);
  Widget::on_confirm(n);
}

void EquipmentPage::on_child_done(int id, int, int) {
  if (id == 1) {
    mode_ = 0;
    refresh();
    cursor->set_flags(1);
  }
  draw_member();
}

void EquipmentPage::on_button2(int n) {
  bool ok = true;
  if (script_flag(0x2DB)) ok = char_get_equip(choice_) != 0x2E7;
  if (!ok) return;
  open_child(2, new EquipOptions(this));
  Widget::on_button2(-n);
}

void EquipmentPage::draw_member() {
  Panel::draw_member();   // 0x800d5cc0: sprite and name
  menu::StrBuf sb;
  sb.put(equip_text(0x80080F88));   // EW
  sb.put(equip_text((party_selected_record()[0x3A] & 1) == 0 ? 0x80080F98 : 0x80080F90));   // ON / OFF
  sb.put(equip_text(0x80080F94));
  sb.put(equip_text(0x80080F9C));   // LV
  sb.put(equip_text(0x80080FA4));   // %3d
  sb.put_int(static_cast<s16>(read_u16(party_selected_slot() + 0x26)));
  member_box->add_string(sb.str());
}

// ==== EquipPicker ==========================================================================

EquipPicker::EquipPicker(EquipmentPage* page) : page_(page) {
  menu::strings_set(menu::globals().data.data());
  box_ = new menu::TextBox;
  box_->set_rect(0x9C, 0x58, 0x9C, 0x70, 0);
  box_->set_font_group(menu::item_strings(1), menu::item_strings(0), 3);
  character_ = party_selected_char();   // 0x80080c30
  entries_.clear();
  help_flags = 4;
}

void EquipPicker::set_slot(int slot) {
  slot_ = slot;
  old_ = char_get_equip(slot);
  entries_.push_back(-1);   // 0x80080c78: "none" first
  build(slot);
  const s16 rows = static_cast<s16>(box_->set_list(static_cast<s16>(entries_.size()), 7, 0x80, 0xE, 0x10, 8));
  cursor->add_grid(&choice_, 2, 1, rows, 0, 0xC, 0, 0xE, box_);
  char_set_equip(slot_, 0);
  page_->preview(0);
}

void EquipPicker::build(int slot) {
  const u32 mask = char_equip_mask();
  for (int i = 0; i < 0x400; ++i) {
    const int item = inv_entry(i) & 0x3FF;
    if (item == 0) continue;
    const s32 type = menu::item_data_get(item, 0x2A);
    bool fits = false;
    switch (slot) {
      case 0: fits = static_cast<u32>(type - 1) < 2 || type == 7; break;
      case 1: fits = type == 8; break;
      case 2: fits = type == 9; break;
      case 3: fits = type == 10; break;
      case 4: fits = type == 11; break;
      case 5:
      case 6: fits = type == 0xC; break;
      default: break;
    }
    if ((static_cast<u32>(menu::item_data_get(item, 4)) & mask) != 0 && fits) entries_.push_back(static_cast<s16>(i));
  }
}

int EquipPicker::item_at(int row) const {
  const s16 index = entries_[static_cast<size_t>(row)];
  return index < 0 ? 0 : (inv_entry(index) & 0x3FF);
}

void EquipPicker::draw_item(menu::TextBox* box, int slot, int index) {
  menu::TextRenderer* r = box->text;
  r->begin_line(box->scroll->line(slot));
  // 0x80080af0
  const s16 entry = entries_[static_cast<size_t>(static_cast<s16>(index))];
  r->set_pen_x(0);
  if (entry < 0) {
    r->draw_current(r->message(0));
  } else {
    const u16 e = inv_entry(entry);
    r->draw_current(r->message(e & 0x3FF));
    r->set_pen_x(0x6A);
    r->draw_pack_message(0xF);
    r->set_pen_x(0x6E);
    r->draw_two_digits(e >> 10 & 0x1F);
  }
  r->upload_line();
}

void EquipPicker::on_cursor(int) {
  const int item = item_at(choice_);
  char_set_equip(slot_, item);
  page_->preview(item);
}

void EquipPicker::on_confirm(int n) {
  // Choosing by hand turns the automatic equipping off.
  party_selected_record()[0x3A] |= 1;
  if (old_ != 0) inventory_add(inventory(), old_, 1, 1);
  const s16 entry = entries_[static_cast<size_t>(choice_)];
  int item = 0;
  if (entry >= 0) {
    if (!inventory_verify(inventory(), static_cast<u32>(entry))) {
      std::memset(inventory() + entry * 2, 0, 2);
    } else {
      item = inv_entry(entry) & 0x3FF;
    }
  }
  char_set_equip(slot_, item);
  if (item != 0) {
    const int at = inventory_find(inventory(), item);
    if (at >= 0) inventory_remove_at(inventory(), at, 1);
  }
  finish(0);
  Widget::on_confirm(n);
}

void EquipPicker::on_cancel(int n) {
  char_set_equip(slot_, old_);
  Widget::on_cancel(n);
}

void EquipPicker::on_button3(int n) {
  page_->on_button3(n);
  Widget::on_button3(n);
}

// ==== EquipOptions ===========================================================================

EquipOptions::EquipOptions(EquipmentPage* page) : page_(page) {
  menu::strings_set(menu::globals().data.data());
  box_ = new menu::TitledBox;
  const text::Layout& lay = text::layout(text::lang());
  box_->set_rect_centered(lay.equip_options_w, 0x40, 1);  // NA 0xd0
  box_->set_font_group(menu::strings_block(1), menu::strings_block(0), 3);
  menu::TextRenderer* r = box_->text;
  box_->pen_at(10, 5);
  box_->add_message(0);
  r->set_colour(2);
  box_->pen_at(0x10, 0x1C);
  const int first = box_->add_message(1);
  box_->pen_at(0x10, 0x2A);
  box_->add_message(2);
  r->set_colour(1);
  box_->set_group(4);
  box_->add_list_highlight(&ew_, 2);
  box_->pen_at(lay.equip_options_x[0], 0x2A);  // NA 0x80
  box_->add_message(0);   // ON
  box_->pen_at(lay.equip_options_x[1], 0x2A);  // NA 0xa0
  box_->add_message(1);   // OFF
  box_->set_highlight(first, &choice_);
  ew_ = (party_selected_record()[0x3A] & 1) != 0 ? 1 : 0;
  cursor->add_grid(&choice_, 0, 1, 2, 0, 0x20, 0, 0xE, box_);
}

void EquipOptions::frame() {
  const u16 v = static_cast<u16>(ew_);
  ew_ = static_cast<s16>(v | 0x8000);
  if (choice_ == 1) ew_ = static_cast<s16>(v & 0x7FFF);
  Dialog::frame();
}

void EquipOptions::on_left_right(int n) {
  if (choice_ == 1) {
    ew_ = static_cast<s16>(1 - (static_cast<u16>(ew_) & 0x7FFF));
    Widget::on_left_right(-n);
  } else {
    sound = 0;
  }
}

void EquipOptions::on_confirm(int n) {
  if (choice_ == 0) {
    best_equip();
  } else if (choice_ == 1) {
    u8* record = party_selected_record();
    if ((static_cast<u16>(ew_) & 0x7FFF) == 0) {
      record[0x3A] &= 0xFE;
    } else {
      record[0x3A] |= 1;
    }
  } else {
    Widget::on_confirm(n);
    return;
  }
  page_->refresh();
  finish(0);
  Widget::on_confirm(n);
}

}  // namespace

// ==== Page handler ========================================================================

void menu_page04_update(menu::MenuPageLoader* p) {
  switch (p->substate) {
    case 1:
      p->publish_id();
      p->push(new EquipmentPage);
      break;
    case 2:
    case 3:
      p->pop();
      break;
    default:
      break;
  }
}

}  // namespace ss::game::field
