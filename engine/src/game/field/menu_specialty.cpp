#include "game/field/menu_specialty.h"

#include <cstring>
#include <span>
#include <string>
#include <vector>

#include "game/ap/seed.h"
#include "game/boot/pad.h"
#include "game/boot/rng.h"
#include "game/boot/sound.h"
#include "game/common/menu_core.h"
#include "game/common/menu_gfx.h"
#include "game/common/text_rules.h"
#include "game/common/menu_scene.h"
#include "game/common/menu_stats.h"
#include "game/common/menu_widget.h"
#include "game/common/party.h"
#include "game/common/screen.h"
#include "game/field/callways.h"
#include "game/field/field.h"
#include "game/field/post_battle.h"
#include "runtime/libgpu.h"
#include "util/log.h"

namespace ss::game::field {
namespace {

constexpr int kSpecialtyCode = 0xBBC;   // page 2's code overlay (at 0x8007e000)

const char* spec_text(u32 address) {
  const u8* p = menu::page_table(kSpecialtyCode, address);
  return p ? reinterpret_cast<const char*>(p) : "";
}
u8 spec_byte(u32 address) {
  const u8* p = menu::page_table(kSpecialtyCode, address);
  return p ? *p : 0;
}
s16 spec_s16(u32 address) {
  const u8* p = menu::page_table(kSpecialtyCode, address);
  if (!p) return 0;
  s16 v;
  std::memcpy(&v, p, 2);
  return v;
}
u32 spec_u32(u32 address) {
  const u8* p = menu::page_table(kSpecialtyCode, address);
  if (!p) return 0;
  u32 v;
  std::memcpy(&v, p, 4);
  return v;
}

s16 rd16s(const u8* p) {
  s16 v;
  std::memcpy(&v, p, 2);
  return v;
}
void wr16(u8* p, s16 v) { std::memcpy(p, &v, 2); }
s32 rd32s(const u8* p) {
  s32 v;
  std::memcpy(&v, p, 4);
  return v;
}
void wr32(u8* p, s32 v) { std::memcpy(p, &v, 4); }

bool script_flag(int n) { return ((globals().script_flags[n >> 3] >> (n & 7)) & 1) != 0; }   // 0x80055ecc
void script_flag_set(int n) { globals().script_flags[n >> 3] |= static_cast<u8>(1 << (n & 7)); }   // 0x80055efc
void script_flag_clear(int n) { globals().script_flags[n >> 3] &= static_cast<u8>(~(1 << (n & 7))); }   // 0x80055f38

s32 g_specialty = 0;   // 0x80084200: the specialty whose dialog is open

// 0x8007e164: the talents a specialty can wake (table 0x80084032, by specialty).
u32 talent_mask(int specialty) { return static_cast<u16>(spec_s16(0x80084032 + specialty * 2)); }

// 0x8007e17c: whether the member has the talents a specialty wants (always with
// Callways' song modifier 12 or the creation aid).
bool has_talents(u32 talents, u32 mask) {
  if (callways() && callways()->mod_12 != 0) return true;
  if (menu::globals().session && menu::globals().session->creation_aid != 0) return true;
  return (talents & mask) == mask;
}

// 0x8007e000: using a specialty may wake one of the talents in 'mask' the member lacks
// (record +0x20): the chance is table 0x80083fb2 + character * 10 + talent out of 256,
// raised by Callways' song modifier 14. A woken talent gives 100 SP (record +0x1a, up to
// 999) and sound 0x33. Returns the talent + 1, or 0.
int wake_talent(int character, u32 mask) {
  u8* record = party_selected_record();
  u32 talents = static_cast<u32>(rd32s(record + 0x20));
  u32 bit = 1;
  for (int i = 0; i < 10; ++i, bit <<= 1) {
    if ((mask & bit) == 0 || (talents & bit) != 0) continue;
    u32 chance = spec_byte(0x80083FB2 + character * 10 + i);
    if (chance != 0) {
      const int mod = callways() ? static_cast<s8>(callways()->mod_14) : 0;
      chance += static_cast<u32>((mod << 8) / 100);
    }
    if (rng_range(0x100) < chance) {
      talents |= bit;
      wr32(record + 0x20, static_cast<s32>(talents));
      s16 sp = static_cast<s16>(rd16s(record + 0x1A) + 100);
      if (sp > 999) sp = 999;
      wr16(record + 0x1A, sp);
      sound_play_sfx(globals().sound, 0x33, 0, 0x7F40);
      return i + 1;
    }
  }
  return 0;
}

bool chance_percent(int pct) { return rng_range(100) < static_cast<u32>(pct); }   // 0x80080b8c / 0x80081398

// 0x8007e728: accessory 0x15c (it makes a Link Combo possible).
bool wears_link_item() { return char_get_equip(6) == 0x15C || char_get_equip(5) == 0x15C; }

// 0x80080a6c: the selected member's Link Combo ends (partner, second set of killer moves).
void clear_link() {
  u8* record = party_selected_record();
  record[0x3B] = 0;
  record[0xCE] = 0;
  record[0xCF] = 0;
}

// Field spells usable from the menu (table 0x80084064, 0-terminated).
bool field_spell(u32 id) {
  for (u32 a = 0x80084064;; ++a) {
    const u8 v = spec_byte(a);
    if (v == 0) return false;
    if (v == id) return true;
  }
}

// [partybuild agent] A use the bench refuses (specialty_use_allowed): the dispatch's sound 7
// stays, and the log says so (the smoke runs read it).
bool bench_refused(SpecialtyUse use, const std::string& what) {
  if (specialty_use_allowed(use)) return false;
  util::log_line(std::string("[ap] bench: ") + ap::character_name(party_selected_char()) + " is on the bench: " + what + " refused");
  return true;
}

// ---- Message boxes in a ConfirmDialog --------------------------------------------------

menu::MessageBox* new_message(s16 group) {
  auto* box = new menu::MessageBox;
  box->set_group(group);
  return box;
}

// 0x800823c8: an oracle (group 0x12).
void show_oracle(int index) { menu::message_box_show(new_message(0x12), index); }

// 0x80082420: a composed song (group 0x14): its name (group 0x1a) or the failure.
void show_composed(int song) {
  menu::MessageBox* box = new_message(0x14);
  if (song != 0) menu::TextRenderer::set_expansion(0, menu::strings_block(1), menu::strings_block(0), 0x1A, song);
  menu::message_box_show(box, song == 0 ? 1 : 0);
}

// 0x800824cc: the familiar's order (group 0x19): the shop (group 0x18) or the failure.
void show_familiar(int shop) {
  menu::MessageBox* box = new_message(0x19);
  if (shop != 0) menu::TextRenderer::set_expansion(0, menu::strings_block(1), menu::strings_block(0), 0x18, shop - 1);
  menu::message_box_show(box, shop == 0 ? 1 : 0);
}

// 0x8008257c: a woken talent (group 10): the member's name and the talent (group 9).
void show_talent(int character, int talent) {
  menu::MessageBox* box = new_message(10);
  menu::TextRenderer::set_expansion(0, menu::strings_block(1), menu::strings_block(0), 0, character + 0x18);
  menu::TextRenderer::set_expansion(1, menu::strings_block(1), menu::strings_block(0), 9, talent - 1);
  menu::message_box_show(box, 1);
}

// ---- A list of owned items or songs (0x14 bytes; 0x8007e1c8) ----------------------------
struct ItemList {
  struct Entry {
    s16 index;   // +0: the position the entry was offered at
    s16 item;    // +2
    s16 count;   // +4
  };
  std::vector<Entry> entries;   // +0x10 (+0 count, +4 capacity)
  s32 next = 0;                 // +8

  // 0x8007e2a4: 'item' if it is in the inventory (its position counts either way).
  void add_owned(int item) {
    const int count = inventory_count(item);
    const s16 at = static_cast<s16>(next++);
    if (count != 0) entries.push_back({at, static_cast<s16>(item), 0});
  }
  // 0x8007e348
  void add_owned_range(int first, int last) {
    for (int i = first; i <= last; ++i) add_owned(i);
  }
  // 0x8007e400
  void add(int item) {
    entries.push_back({static_cast<s16>(next++), static_cast<s16>(item), static_cast<s16>(inventory_count(item))});
  }
};

class SpecialtyPage;

// ---- The member to cast a spell on (Dialog, 0x88 bytes, vtables 0x80083ed4 / 0x80083ec4;
// 0x80082a00) -------------------------------------------------------------------------------
class SpellTarget final : public menu::Dialog {
 public:
  explicit SpellTarget(SpecialtyPage* page);
  void frame() override;                   // [9] 0x80082cc4
  void on_confirm(int n) override;         // [13] 0x80082be0
  void set_spell(int spell, int index);    // 0x80082ca0

 private:
  void refresh();                          // 0x80082db4
  void draw_member_box(menu::TextBox* box);   // 0x80082e7c
  SpecialtyPage* page_;
  s16 choice_ = 0;                         // +0x34
  menu::StatCalc stats_;                   // +0x50
  menu::TextBox* scroll_ = nullptr;        // +0x58
  menu::TextBox* title_ = nullptr;         // +0x5c
  menu::TextBox* members_[8] = {};         // +0x60
  s32 spell_ = 0;                          // +0x80
  s32 index_ = 0;                          // +0x84: its place in the list (record +0x8c)
};

// ---- Specialty page (Panel, 0x16c bytes, vtables 0x80083ca4 / 0x80083c94; 0x8007e61c) ---
class SpecialtyPage final : public menu::Panel {
 public:
  SpecialtyPage();
  ~SpecialtyPage() override = default;                                 // [1] 0x8007e6c8
  void draw_item(menu::TextBox* box, int slot, int index) override;   // [8] 0x8007fe80
  void frame() override;                                               // [9] 0x8007ebe8
  void on_child_done(int id, int state, int code) override;           // [10] 0x8007ecc0
  void on_cursor(int) override { refresh_info(); }                     // [12] 0x8007f2a8
  void on_confirm(int n) override;                                     // [13] 0x8007e774
  void on_cancel(int n) override;                                      // [14] 0x8007eb80
  void on_button2(int n) override;                                     // [15] 0x8007eae8
  void on_button3(int n) override;                                     // [16] 0x8007ea38
  void on_member_changed() override;                                   // [21] 0x8007f2c8
  void draw_help() override;                                           // [22] (the bench's note) [partybuild agent]
  void draw_member() override;                                         // [23] 0x8007f314

  void show_killer(int id);                // 0x8007ef74
  void draw_killer_slots();                // 0x8007fa30
  void show_viewer_item(int item) { set_viewer_item(item); }   // 0x8007e6fc
  menu::TextBox* info_box() { return info; }                    // 0x8007e71c

 private:
  int build_entries();                     // 0x8007f5ac
  void clear_boxes(bool close);            // 0x8007f1fc
  void setup_list();                       // 0x8007f6c8
  void setup_killer();                     // 0x8007f850
  void setup_spells();                     // 0x8007fc64
  void refresh_info();                     // 0x8007ed88
  void show_entry();                       // 0x8007eebc
  void show_spell(int id);                 // 0x8007f034
  void show_costs(menu::TextBox* box, int uses, u32 cost);   // 0x8007f0d0
  void draw_spell(menu::TextRenderer* r, s16 x, int index);   // 0x8007ff20

  s16 choice_ = 0;                         // +0x34
  menu::TextBox* list_ = nullptr;          // +0x98
  menu::TextBox* detail_ = nullptr;        // +0x9c
  s16 state_ = 0;                          // +0xa0: 1 list, 2 killer moves, 3 spells; negative: build it
  s16 home_ = 0;                           // +0xa2: the state the page opened in
  s16 entries_[32] = {};                   // +0xa4: specialty ids; -2 killer moves, -3 spells
  s16 count_ = 0;                          // +0xe4
  s32 spells_[32] = {};                    // +0xe8: spell ids, negative = off in battle
  s16 set_ = 0;                            // +0x168: the killer move row (or Link Combo set)
  s16 slot_ = 0;                           // +0x16a: the second Link Combo set's row
};

// ---- A killer move for a slot (Dialog, 0xdc bytes, vtable 0x80083bec; 0x80080040) -------
class KillerMoveList final : public menu::Dialog {
 public:
  explicit KillerMoveList(SpecialtyPage* page);
  void draw_item(menu::TextBox* box, int slot, int index) override;   // [8] 0x800802c4
  void on_cursor(int) override { page_->show_killer(ids_[choice_]); }  // [12] 0x80080200
  void on_confirm(int n) override;                                     // [13] 0x80080238
  void set_target(int set, int slot) { set_ = set, slot_ = slot; }     // 0x800801f4

 private:
  SpecialtyPage* page_;
  s16 choice_ = 0;                         // +0x34
  menu::TextBox* box_ = nullptr;           // +0x50
  s32 set_ = 0;                            // +0x54
  s32 slot_ = 0;                           // +0x58
  s32 ids_[32] = {};                       // +0x5c: 0 (none), then the moves not in a slot
};

KillerMoveList::KillerMoveList(SpecialtyPage* page) : page_(page) {
  box_ = new menu::TextBox;
  box_->set_rect(0x9C, 0x58, 0x9C, 0x70, 0);
  box_->set_group(spec_byte(0x80084167 + party_selected_char()));
  const u8* record = party_selected_record();
  s16 count = 1;
  ids_[0] = 0;
  for (int i = 0; i < 0x20; ++i) {
    if (record[0x3C + i] == 0) continue;
    bool free = true;
    for (int k = 0; k < 4; ++k) {
      if (static_cast<u32>(i + 1) == record[0xCC + k]) free = false;
    }
    if (free) ids_[count++] = i + 1;
  }
  const s16 rows = static_cast<s16>(box_->set_list(count, 7, 0x80, 0xE, 0x10, 8));
  cursor->add_grid(&choice_, 2, 1, rows, 0, 0xC, 0, 0xE, box_);
}

void KillerMoveList::draw_item(menu::TextBox* box, int slot, int index) {
  menu::TextRenderer* r = box->text;
  r->begin_line(box->scroll->line(slot));
  r->set_pen_x(0);   // 0x8008033c
  r->draw_current(r->message(ids_[index]));
  r->upload_line();
}

void KillerMoveList::on_confirm(int n) {
  party_selected_record()[0xCC + slot_ + set_ * 2] = static_cast<u8>(ids_[choice_]);
  finish(0);
  // (A benched member has no voice slot: their front slot's voices load when they come in
  // [partybuild agent].)
  if (g_voice_reload_member && party_get_selected() < 8) g_voice_reload_member(party_get_selected());
  Widget::on_confirm(n);
}

// ---- The Link Combo partner (Dialog, 0x6c bytes, vtable 0x80083d74; 0x80080620) ---------
class PartnerDialog final : public menu::Dialog {
 public:
  explicit PartnerDialog(const menu::TextBox* above);
  ~PartnerDialog() override { party_select(self_); }   // [1] 0x800806dc
  void on_confirm(int n) override;                     // [13] 0x80080974

 private:
  void build();                            // 0x8008072c
  s16 choice_ = 0;                         // +0x34
  menu::TextBox* box_ = nullptr;           // +0x50
  s32 self_ = 0;                           // +0x54: the member's slot
  s32 slots_[4] = {};                      // +0x58: a front slot each, or 4 for partner 0x10
  s32 count_ = 0;                          // +0x68
};

PartnerDialog::PartnerDialog(const menu::TextBox* above) {
  box_ = new menu::TextBox;
  box_->set_rect_below(0x90, 0x58, above, 1);
  self_ = party_get_selected();
  build();
  cursor->add_grid(&choice_, 0, 1, static_cast<s16>(count_), 0, 0xC, 0, 0xE, box_);
}

void PartnerDialog::build() {
  menu::TextBox* b = box_;
  b->clear();
  count_ = 0;
  for (int i = 0; i < 4; ++i) {
    if (i == self_) continue;
    party_select(i);
    const int character = party_selected_char();
    if (character != 0) {
      const s16 y = static_cast<s16>(count_ * 0xE + 8);
      b->pen_at(0x10, y);
      b->set_group(0);
      b->add_message(static_cast<s16>(character + 0x18));
      b->pen_at(0x68, y);
      b->set_group(0xB);
      menu::TextRenderer* r = b->text;
      r->set_colour(3);
      if (party_selected_record()[0x3B] != 0) {
        b->add_message(4);   // has a partner
      } else if (party_find_link_partner(character) != 0) {
        b->add_message(5);   // is someone's partner
      }
      r->set_colour(1);
    }
    slots_[count_++] = i;
  }
  if (script_flag(0x5D)) {
    const s16 y = static_cast<s16>(count_ * 0xE + 8);
    b->pen_at(0x10, y);
    b->set_group(0xB);
    b->add_message(6);
    b->pen_at(0x68, y);
    b->set_group(0xB);
    menu::TextRenderer* r = b->text;
    r->set_colour(3);
    if (party_find_link_partner(0x10) != 0) b->add_message(5);
    r->set_colour(1);
    slots_[count_++] = 4;
  }
}

void PartnerDialog::on_confirm(int n) {
  int partner;
  if (slots_[choice_] == 4) {
    partner = 0x10;
  } else {
    party_select(slots_[choice_]);
    partner = party_selected_char();
    if (party_selected_record()[0x3B] != 0) clear_link();
    if (party_find_link_partner(partner) != 0) {
      party_select(party_find_char_slot(party_find_link_partner(partner)));
      clear_link();
    }
  }
  party_select(self_);
  party_selected_record()[0x3B] = static_cast<u8>(partner);
  Widget::on_confirm(n);
  finish(0);
}

// ---- Link Combo ON / OFF (Dialog, 0x54 bytes, vtable 0x80083e1c; 0x800803ac) ------------
class LinkComboDialog final : public menu::Dialog {
 public:
  LinkComboDialog();
  void on_child_done(int id, int state, int) override {   // [10] 0x800805e0
    if (id == 1 && state == 3) finish(0);
  }
  void on_confirm(int n) override;         // [13] 0x80080524

 private:
  s16 choice_ = 0;                         // +0x34
  menu::TextBox* box_ = nullptr;           // +0x50
};

LinkComboDialog::LinkComboDialog() {
  box_ = new menu::TitledBox;
  box_->set_rect_centered(0x100, 0x30, 1);
  box_->set_font_group(menu::strings_block(1), menu::strings_block(0), 0xB);
  box_->pen_at(10, 5);
  box_->add_message(0);
  cursor->add_grid(&choice_, 1, 2, 1, 0, 0x20, 0, 0xE, box_);
  menu::TextRenderer* r = box_->text;
  box_->pen_at(0x10, 0x1C);
  r->set_colour(2);
  box_->add_message(1);
  r->set_colour(1);
  box_->add_list_highlight(&choice_, 2);
  box_->pen_at(text::layout(text::lang()).link_combo_on_x, 0x1C);  // NA 0xa0
  box_->add_message(2);
  box_->pen_at(text::layout(text::lang()).link_combo_off_x, 0x1C);  // NA 0xc0
  box_->add_message(3);
}

void LinkComboDialog::on_confirm(int n) {
  if (choice_ == 0) {
    open_child(1, new PartnerDialog(box_));
  } else if (choice_ == 1) {
    clear_link();
    finish(0);
  }
  Widget::on_confirm(n);
}

// ---- A specialty's dialog (Dialog, 0x60 bytes, vtable 0x80083b3c; 0x80080a9c) ----------
// It draws itself on its first frame ([21]), in a box over the page's detail box.
class SpecialtyDialog : public menu::Dialog {
 public:
  explicit SpecialtyDialog(SpecialtyPage* page);
  void frame() override;                   // [9] 0x80080b2c
  virtual void build() { draw_level(); }   // [21] 0x80080bbc

 protected:
  void draw_level();                       // 0x80080bbc: "Specialty Level  n"
  SpecialtyPage* page_;
  s16 choice_ = 0;                         // +0x34
  menu::TextBox* box_ = nullptr;           // +0x50
  s32 id_ = 0;                             // +0x54
  s32 level_ = 0;                          // +0x58
  s32 built_ = 0;                          // +0x5c
};

SpecialtyDialog::SpecialtyDialog(SpecialtyPage* page) : page_(page) {
  menu::strings_set(menu::globals().data.data());
  box_ = new menu::TextBox;
  box_->set_rect(0x9C, 0x58, 0x9C, 0x70, 0);
  id_ = g_specialty;
  level_ = char_specialty_level(g_specialty);
  built_ = 0;
}

void SpecialtyDialog::frame() {
  if (built_ == 0) {
    build();
    built_ = 1;
  }
  Dialog::frame();
}

void SpecialtyDialog::draw_level() {
  menu::TextBox* b = box_;
  b->clear();
  b->set_group(0xE);
  menu::TextRenderer* r = b->text_renderer();
  b->pen_reset();
  r->set_colour(2);
  b->add_message(0);
  r->set_colour(1);
  r->set_pen(0x60, 8, 0xC);
  r->add_number(level_, 2);
}

// Oracle, specialty 2 (vtable 0x80083a8c).
class OracleDialog final : public SpecialtyDialog {
 public:
  using SpecialtyDialog::SpecialtyDialog;
  void build() override;                   // [21] 0x80080c5c
  void on_confirm(int n) override;         // [13] 0x80080cec
};

void OracleDialog::build() {
  draw_level();
  cursor->add_grid(&choice_, 1, 1, 1, 0, 0x2E, 0, 0xE, box_);
  box_->pen_at(0x10, 0x2A);
  box_->set_group(0x11);
  box_->add_message(0);
}

void OracleDialog::on_confirm(int n) {
  open_child(1, new menu::ConfirmDialog);
  int range = level_ * 5;
  if (range > 0x23) range = 0x24;
  show_oracle(static_cast<int>(rng_range(static_cast<u32>(range))));
  Widget::on_confirm(-n);
}

// Musical Talent, specialty 3 (vtable 0x800839dc; 0x80080d9c).
class MusicDialog final : public SpecialtyDialog {
 public:
  using SpecialtyDialog::SpecialtyDialog;
  void build() override {                  // [21] 0x80080dd4
    cursor->add_grid(&choice_, 1, 1, 2, 0, 0x28, 0, 0xE, box_);
  }
  void on_child_done(int, int, int) override { on_cursor(1); }   // [10] 0x80081064
  void on_cursor(int n) override;          // [12] 0x80080e28
  void on_confirm(int n) override;         // [13] 0x80080fd8
  void draw_uses(int uses);                // 0x80080ea0: 1 the Feather Pen, 2 item 0x35
  SpecialtyPage* page() { return page_; }
  s32 level() const { return level_; }
};

void MusicDialog::on_cursor(int) {
  menu::TextBox* info = page_->info_box();
  info->clear();
  info->pen_reset();
  info->set_group(0x14);
  info->add_message_lines(choice_ + 2, 1);
  draw_uses(0);
}

void MusicDialog::draw_uses(int uses) {
  draw_level();
  menu::TextBox* b = box_;
  b->set_group(0x13);
  b->pen_at(0x10, 0x24);
  b->add_message(0);
  b->add_message(1);
  if (uses != 1 && uses != 2) return;
  menu::TextRenderer* r = b->text;
  b->set_group(0xE);
  b->pen_at(0x10, 0x4E);
  r->set_colour(2);
  b->add_message(1);
  r->set_colour(1);
  b->add_item_count(uses == 1 ? 0x2C : 0x35);
}

// Compose: an instrument (items 0x2d-0x34) and a Feather Pen (0x2c) make one of its two
// songs (Dialog, 100 bytes, vtable 0x80083934; 0x800810a0).
class ComposeList final : public menu::Dialog {
 public:
  explicit ComposeList(MusicDialog* music);
  ~ComposeList() override { page_->show_viewer_item(0); }              // [1] 0x800811fc
  void draw_item(menu::TextBox* box, int slot, int index) override;   // [8] 0x800812b4
  void on_cursor(int n) override;                                      // [12] 0x80081260
  void on_confirm(int n) override;                                     // [13] 0x800813c8

 private:
  s16 choice_ = 0;                         // +0x34
  menu::TextBox* box_ = nullptr;           // +0x54
  ItemList list_;                          // +0x58
  MusicDialog* music_;                     // +0x5c
  SpecialtyPage* page_;                    // +0x60
};

ComposeList::ComposeList(MusicDialog* music) : music_(music), page_(music->page()) {
  box_ = new menu::TextBox;
  box_->set_rect(0, 0x58, 0x9C, 0x70, 0);
  box_->set_font_group(menu::item_strings(1), menu::item_strings(0), 3);
  list_.add_owned_range(0x2D, 0x34);
  if (list_.entries.empty()) list_.add(0);
  const s16 rows = static_cast<s16>(box_->set_list(static_cast<s16>(list_.entries.size()), 7, 0x80, 0xE, 0x10, 8));
  cursor->add_grid(&choice_, 2, 1, rows, 0, 0xC, 0, 0xE, box_);
}

void ComposeList::draw_item(menu::TextBox* box, int slot, int index) {
  menu::TextRenderer* r = box->text;
  r->begin_line(box->scroll->line(slot));
  r->set_pen_x(0);   // 0x8008132c
  r->draw_current(r->message(list_.entries[static_cast<size_t>(index)].item));
  r->upload_line();
}

void ComposeList::on_cursor(int) {
  page_->show_viewer_item(list_.entries[static_cast<size_t>(choice_)].item);
  music_->draw_uses(1);
}

void ComposeList::on_confirm(int n) {
  if (inventory_count(0x2C) == 0) return;
  inventory_take_one(0x2C);
  bool made = false;
  music_->draw_uses(1);
  const s16 index = list_.entries[static_cast<size_t>(choice_)].index;
  const int pick = static_cast<int>(rng_range(2));
  int song = index * 2 + pick;
  const int level = char_specialty_level(g_specialty);
  if (level != 0) {
    const int bonus = (callways() ? static_cast<s8>(callways()->mod_13) : 0) + menu::globals().session->creation_bonus;
    made = chance_percent((level << 3) / (pick == 1 ? 8 : 4) + bonus);
  }
  const int flag = song + 0x2F;
  if (script_flag(flag)) made = false;
  open_child(1, new menu::ConfirmDialog);
  if (!made) {
    song = 0;
  } else {
    script_flag_set(flag);
    ++song;
  }
  show_composed(song);
  Widget::on_confirm(-n);
}

// Play music: a composed song (script flags 0x2f-0x3e) with item 0x35, through Callways
// (Dialog, 0x7c bytes, vtable 0x8008388c; 0x80081570).
class PlayList final : public menu::Dialog {
 public:
  explicit PlayList(MusicDialog* music);
  ~PlayList() override;                                                // [1] 0x800816dc
  void draw_item(menu::TextBox* box, int slot, int index) override;   // [8] 0x800817c8
  void frame() override;                                               // [9] 0x80081a5c
  void on_cursor(int n) override;                                      // [12] 0x80081740
  void on_confirm(int n) override;                                     // [13] 0x800818dc

 private:
  s16 choice_ = 0;                         // +0x34
  s32 level_ = 0;                          // +0x58: negative without the talents
  menu::TextBox* box_ = nullptr;           // +0x60
  ItemList list_;                          // +0x64
  MusicDialog* music_;                     // +0x68
  SpecialtyPage* page_;                    // +0x6c
  menu::BgLoadClient client_;              // +0x70
  s16 load_ = 0;                           // +0x78: the song's load
};

PlayList::PlayList(MusicDialog* music) : music_(music), page_(music->page()) {
  box_ = new menu::TextBox;
  box_->set_rect(0, 0x58, 0x9C, 0x70, 0);
  box_->set_group(0x1A);
  for (int i = 0; i < 0x10; ++i) {
    if (script_flag(i + 0x2F)) list_.add(i + 1);
  }
  if (list_.entries.empty()) list_.add(0);
  const s16 rows = static_cast<s16>(box_->set_list(static_cast<s16>(list_.entries.size()), 7, 0x80, 0xE, 0x10, 8));
  cursor->add_grid(&choice_, 2, 1, rows, 0, 0xC, 0, 0xE, box_);
  load_ = 0;
}

PlayList::~PlayList() { menu::bgload_cancel_owner(&client_); }

void PlayList::draw_item(menu::TextBox* box, int slot, int index) {
  menu::TextRenderer* r = box->text;
  r->begin_line(box->scroll->line(slot));
  r->set_pen_x(0);   // 0x80081840
  r->draw_current(r->message(list_.entries[static_cast<size_t>(index)].item));
  r->upload_line();
}

void PlayList::on_cursor(int) {
  menu::TextBox* info = page_->info_box();
  info->clear();
  info->pen_reset();
  info->set_group(0x1B);
  info->add_message_lines(list_.entries[static_cast<size_t>(choice_)].item, 6);
  music_->draw_uses(2);
}

void PlayList::on_confirm(int n) {
  if (inventory_count(0x35) == 0) return;
  const int song = list_.entries[static_cast<size_t>(choice_)].item;
  if (song == 0) return;
  inventory_take_one(0x35);
  music_->draw_uses(2);
  // 0x80038e64: the song's sound entry (table 0x800841de), read into its own buffer.
  load_ = static_cast<s16>(menu::bgload_queue(&client_, spec_s16(0x800841DE + song * 2), 0, nullptr, 0, 0));
  const int character = party_selected_char();
  level_ = char_specialty_level(g_specialty);
  const u32 mask = talent_mask(g_specialty);
  if (!has_talents(static_cast<u32>(rd32s(party_selected_record() + 0x20)), mask)) level_ = -level_;
  const int talent = wake_talent(character, mask);
  if (talent != 0) {
    open_child(1, new menu::ConfirmDialog);
    show_talent(character, talent);
  }
  flags |= 2;   // no input until the song plays
  Widget::on_confirm(-n);
}

void PlayList::frame() {
  Dialog::frame();
  if (load_ != 0 && menu::bgload_owner_done(&client_)) {
    const int song = list_.entries[static_cast<size_t>(choice_)].item;
    const menu::LoadRequest* req = menu::bgload_request(load_);
    if (req != nullptr && req->dest != nullptr && callways() != nullptr) {
      const size_t size = static_cast<size_t>(req->sectors) << 11;
      callways_play_song(callways(), std::span<const u8>(req->dest, size), song, level_);
    }
    menu::bgload_cancel(load_);   // 0x80039268
    load_ = 0;
    flags &= static_cast<u16>(~2);
  }
}

void MusicDialog::on_confirm(int n) {
  if (choice_ == 0) {
    open_child(2, new ComposeList(this));
  } else {
    open_child(1, new PlayList(this));
  }
  Widget::on_confirm(-n);
}

// Practice, specialty 8 (vtable 0x800837dc): record +0x3a bit 2.
class PracticeDialog final : public SpecialtyDialog {
 public:
  using SpecialtyDialog::SpecialtyDialog;
  void build() override;                   // [21] 0x80081b0c
  void on_confirm(int n) override;         // [13] 0x80081be8
};

void PracticeDialog::build() {
  draw_level();
  if (party_selected_record()[0x3A] & 4) choice_ = 1;
  cursor->add_grid(&choice_, 1, 1, 2, 0, 0x2E, 0, 0xE, box_);
  menu::TextBox* b = box_;
  b->set_group(0x15);
  b->pen_at(0x10, 0x2A);
  b->add_list_highlight(&choice_, 2);
  b->add_message(0);
  b->add_message(1);
}

void PracticeDialog::on_confirm(int n) {
  u8* record = party_selected_record();
  if (choice_ == 0) {
    record[0x3A] &= 0xFB;
  } else {
    record[0x3A] |= 4;
  }
  cancel();
  Widget::on_confirm(-n);
}

// Scout, specialty 9 (vtable 0x8008372c): script flags 0x22 / 0x23 and game_state
// +0x266 / +0x267 (more or fewer encounters). It can wake a talent once a minute.
class ScoutDialog final : public SpecialtyDialog {
 public:
  using SpecialtyDialog::SpecialtyDialog;
  void build() override;                   // [21] 0x80081c74
  void on_child_done(int, int, int) override { cancel(); }   // [10] 0x80081f80
  void on_confirm(int n) override;         // [13] 0x80081d60
};

void ScoutDialog::build() {
  draw_level();
  if (script_flag(0x22)) choice_ = 1;
  if (script_flag(0x23)) choice_ = 2;
  cursor->add_grid(&choice_, 1, 1, 3, 0, 0x2E, 0, 0xE, box_);
  menu::TextBox* b = box_;
  b->set_group(0x16);
  b->pen_at(0x10, 0x2A);
  b->add_list_highlight(&choice_, 3);
  b->add_message(0);
  b->add_message(1);
  b->add_message(2);
}

void ScoutDialog::on_confirm(int n) {
  const s8 level = static_cast<s8>(level_);
  s8 amount = static_cast<s8>(level * 5);
  const u32 mask = talent_mask(id_);
  s8 neutral = 0;
  if (!has_talents(static_cast<u32>(rd32s(party_selected_record() + 0x20)), mask)) {
    neutral = amount;
    if (rng_range(2) != 0) {
      neutral = static_cast<s8>(level * -5);
      amount = neutral;
    }
  }
  u8* gs = globals().game_state;
  switch (choice_) {
    case 1:
      script_flag_set(0x22);
      script_flag_clear(0x23);
      gs[0x266] = static_cast<u8>(amount);
      gs[0x267] = 0;
      break;
    case 2:
      script_flag_clear(0x22);
      script_flag_set(0x23);
      gs[0x266] = 0;
      gs[0x267] = static_cast<u8>(amount);
      break;
    case 0:
      script_flag_clear(0x22);
      script_flag_clear(0x23);
      gs[0x266] = static_cast<u8>(neutral);
      gs[0x267] = 0;
      break;
    default:
      break;
  }
  int talent = 0;
  const u32 now = clock_seconds();
  u8* config = party_config();
  if (static_cast<u32>(rd32s(config + 0x54)) + 0x3C < now) {
    wr32(config + 0x54, static_cast<s32>(now));
    talent = wake_talent(party_selected_char(), mask);
  }
  if (talent == 0) {
    cancel();
  } else {
    open_child(1, new menu::ConfirmDialog);
    show_talent(party_selected_char(), talent);
  }
  Widget::on_confirm(-n);
}

// Familiar, specialty 12 (vtable 0x8008367c; 0x80081fa0, 100 bytes): item 0x1a2 sends the
// familiar to a shop (game_state +0x288 = its rank, session +0x12 = the shop page's
// argument); the page then goes on to page 0xb.
class FamiliarDialog final : public SpecialtyDialog {
 public:
  using SpecialtyDialog::SpecialtyDialog;
  void build() override {                  // [21] 0x80081fd8
    draw_order();
    cursor->add_grid(&choice_, 1, 1, 1, 0, 0x28, 0, 0xE, box_);
  }
  void on_child_done(int id, int, int) override;   // [10] 0x800822e0
  void on_confirm(int n) override;                 // [13] 0x80082130

 private:
  void draw_order();                       // 0x8008203c
  s32 success_ = 0;                        // +0x60
};

void FamiliarDialog::draw_order() {
  draw_level();
  menu::TextBox* b = box_;
  b->set_group(0x17);
  b->pen_at(0x10, 0x24);
  menu::TextRenderer* r = b->text;
  const bool can = rd32s(globals().game_state + 0x294) == 0 && inventory_count(0x1A2) != 0;
  r->set_colour(can ? 1 : 4);
  b->add_message(0);
  r->set_colour(1);
  b->set_group(0xE);
  b->pen_at(0x10, 0x4E);
  r->set_colour(2);
  b->add_message(1);
  r->set_colour(1);
  b->add_item_count(0x1A2);
}

void FamiliarDialog::on_confirm(int n) {
  if (inventory_count(0x1A2) == 0 || rd32s(globals().game_state + 0x294) != 0) return;
  if (inventory_count(0x1A2) == 0) return;
  if (script_flag(0xC) || script_flag(0xE)) return;
  inventory_take_one(0x1A2);
  draw_order();
  success_ = 0;
  const int half = (level_ - 1) / 2;
  int rank = half + 1;
  if (has_talents(static_cast<u32>(rd32s(party_selected_record() + 0x20)), talent_mask(id_))) {
    const int bonus = (callways() ? static_cast<s8>(callways()->mod_13) : 0) + menu::globals().session->creation_bonus;
    success_ = chance_percent(level_ * 8 + bonus) ? 1 : 0;
  }
  open_child(1, new menu::ConfirmDialog);
  if (success_ == 0) {
    rank = 0;
  } else {
    globals().game_state[0x288] = static_cast<u8>(rank);
  }
  show_familiar(rank);
  menu::globals().session->arg = static_cast<s16>(half + 0x5A);
  Widget::on_confirm(-n);
}

void FamiliarDialog::on_child_done(int id, int, int) {
  if (id != 1) return;
  if (success_ == 0) {
    const int character = party_selected_char();
    const int talent = wake_talent(character, talent_mask(id_));
    if (talent != 0) {
      open_child(2, new menu::ConfirmDialog);
      show_talent(character, talent);
    }
  } else {
    finish(0);
  }
}

// ---- The spells' effects (a StatChange of 0x20 bytes; 0x80082648) ------------------------
struct SpellEffect : StatChange {
  u8 one = 0;          // +6
  u8 all = 0;          // +8
  s32 bonus = 0;       // +0xc: the caster's stat +0x46 / 4 + its practice with the spell / 2
  s32 spell = 0;       // +0x10
  s32 slot = 0;        // +0x14: the target
  s32 index = 0;       // +0x18: the spell's place in the list
  s32 mp = 0;          // +0x1c: its cost

  // 0x8008268c: a dry run: 1 HP would change, 3 a status, 0 nothing.
  int check() {
    stat_change_clear(this);
    run();
    if (hp_changed) return 1;
    return status_changed ? 3 : 0;
  }
  // 0x800826e0
  void cast() {
    stat_change_clear(this);
    apply = 1;
    run();
  }
  // 0x80082718: the caster pays the MP; spells 0x2f-0x31 heal one member, 0x32 / 0x33 all,
  // 0x34 revives (half HP), 0x35 / 0x36 cure statuses 8 / 0xe.
  void run() {
    const u8* caster = party_selected_slot();
    const u8* record = party_selected_record();
    bonus = rd16s(caster + 0x46) / 4 + rd16s(record + 0x8C + index * 2) / 2;
    const s16 have = rd16s(caster + 0x24);
    if (have < mp) return;
    if (apply) wr16(party_selected_slot() + 0x24, static_cast<s16>(have - mp));
    const int saved = party_get_selected();
    party_select(slot);
    if (party_selected_char() != 0) {
      switch (spell) {
        case 0x2F: heal(100); break;
        case 0x30: heal(800); break;
        case 0x31: heal(0xAF0); break;
        case 0x32: heal_all(0x4B0); break;
        case 0x33: heal_all(0x898); break;
        case 0x34:
          cure(1);
          if (status_changed) {
            stat_change_set(this, 3, 1);
            stat_change_heal_hp_pct(this, 0x32);
          }
          break;
        case 0x35: cure(8); break;
        case 0x36: cure(0xE); break;
        default: break;
      }
    }
    party_select(saved);
  }
  // 0x800828dc
  void cure(u32 bits) {
    one = 1;
    const u8 status = party_selected_status();
    if (status & bits) stat_change_set_status(this, static_cast<u8>(status & ~bits));
  }
  // 0x8008293c
  void heal(int amount) {
    one = 1;
    stat_change_heal(this, 0, amount + bonus);   // 0x800398f4
  }
  // 0x80082968
  void heal_all(int amount) {
    const int saved = party_get_selected();
    for (int i = 0; i < 8; ++i) {
      party_select(i);
      all = 1;
      if (party_selected_char() != 0) stat_change_heal(this, 0, amount + bonus);
    }
    party_select(saved);
  }
};

// ---- SpellTarget ---------------------------------------------------------------------------

SpellTarget::SpellTarget(SpecialtyPage* page) : page_(page) {
  for (int i = 7; i >= 0; --i) {
    members_[i] = new menu::TextBox;
    members_[i]->set_rect((i % 2) * 0x9C, (i / 2) * 0x32 + 100, 0x9C, 0x32, 0);
  }
  title_ = new menu::TitledBox;
  title_->set_rect(0, 0x52, 0x138, 0x76, 0);
  title_->slot = static_cast<u16>(title_->slot - 2);
  scroll_ = new menu::TextBox;
  scroll_->set_rect(0, 0x52, 0x10, 0x10, 0);
  scroll_->slot = static_cast<u16>(scroll_->slot - 2);
  scroll_->set_flags(0x1000);
  scroll_->set_list(4, 2, 0x10, 0x32, -1, 8);
  cursor->add_grid(&choice_, 2, 2, 2, 0, 0x32, 0x9C, 0x32, scroll_);
}

void SpellTarget::set_spell(int spell, int index) {
  spell_ = spell;
  index_ = index;
  refresh();
}

void SpellTarget::frame() {
  Dialog::frame();
  for (menu::TextBox* b : members_) b->dy = static_cast<s16>(-scroll_->scroll->v[0]);
  // The member boxes draw inside the band under the title.
  auto* a = screen_alloc_prims<psyq::DR_AREA>(ctx->arena, 1);
  if (!a) return;
  const psyq::RECT r{0, static_cast<s16>(screen_draw_y() + 100), 0x140, 100};
  psyq::SetDrawArea(a, &r);
  menu::prim_link(ctx, 4, a);
}

void SpellTarget::on_confirm(int) {
  SpellEffect e;
  e.spell = spell_;
  e.slot = choice_;
  e.index = index_;
  e.mp = stats_.mp_cost(spec_byte(0x8008406F + spell_) & 0x7F);
  if (party_slot_active(choice_) == 0) return;
  if (e.check() == 0) return;
  e.cast();
  refresh();
  page_->draw_member();
  sound = 0x13;
}

void SpellTarget::refresh() {
  menu::TextBox* t = title_;
  t->clear();
  t->set_group(0xD);
  t->pen_at(10, 5);
  t->add_message(0);
  const int selected = party_get_selected();
  for (int i = 0; i < 8; ++i) {
    party_select(i);
    members_[i]->clear();
    draw_member_box(members_[i]);
  }
  party_select(selected);
}

void SpellTarget::draw_member_box(menu::TextBox* box) {
  if (party_selected_char() == 0) return;
  box->pen_at(8, 0);
  box->add_party_character(static_cast<s16>(read_u16(party_selected_slot())), 6, 0x1000);
  box->pen_at(0x28, 10);
  {
    menu::StrBuf sb;
    sb.set(reinterpret_cast<const char*>(party_selected_record() + 0x24));
    box->add_string(sb.str());
  }
  box->pen_at(0x68, 10);
  {
    menu::StrBuf sb;
    sb.set(spec_text(0x800835AC));
    sb.put(spec_text(0x800835B0));   // LV
    sb.put(spec_text(0x80083558));   // %3d
    sb.put_int(rd16s(party_selected_slot() + 0x26));
    box->add_string(sb.str());
  }
  box->pen_at(0x28, 0x18);
  {
    menu::StrBuf sb;
    sb.set(spec_text(0x800835AC));
    for (u32 a : {0x800835B8u, 0x800835C0u, 0x800835C4u, 0x800835CCu}) sb.put(spec_text(a));   // HP / MP
    box->add_string(sb.str());
  }
  box->pen_at(0x48, 0x18);
  box->add_party_status(static_cast<s16>(party_get_selected()));
}

// ---- SpecialtyPage -------------------------------------------------------------------------

SpecialtyPage::SpecialtyPage() {
  menu::strings_set(menu::globals().data.data());
  menu::TextRenderer::set_default_font(menu::strings_block(1), menu::strings_block(0));
  create_help();
  create_member_box(0);
  create_info_box();
  create_viewer(0x118, 0x4A, 0x18);   // 0x800d4b04
  state_ = 0;
  clear_boxes(false);
}

int SpecialtyPage::build_entries() {
  count_ = 0;
  for (s16& e : entries_) e = 0;
  const s16 first = static_cast<s16>(spec_byte(0x80084057 + party_selected_char()) + 1);
  entries_[count_++] = static_cast<s16>(-first);
  for (int i = 0; i < 0x11; ++i) {
    switch (i) {
      case 1: case 2: case 7: case 8: case 0xB:
        if (char_specialty_level(i + 1) > 0) entries_[count_++] = static_cast<s16>(i + 1);
        break;
      default:
        break;
    }
  }
  return count_ != 1 ? 1 : first;
}

void SpecialtyPage::clear_boxes(bool close) {
  if (list_) {
    if (close) list_->on_close();
    list_ = nullptr;
  }
  if (detail_) {
    if (close) detail_->on_close();
    detail_ = nullptr;
  }
  cursor->clear_groups();
}

void SpecialtyPage::frame() {
  if (state_ == 0) {
    home_ = static_cast<s16>(build_entries());
    state_ = static_cast<s16>(-home_);
  }
  if (state_ < 0) {
    state_ = static_cast<s16>(-state_);
    clear_boxes(true);
    switch (state_) {
      case 1: setup_list(); break;
      case 2: setup_killer(); break;
      case 3: setup_spells(); break;
      default: break;
    }
  }
  Panel::frame();
}

void SpecialtyPage::setup_list() {
  list_ = new menu::TextBox;
  list_->set_rect(0, 0x58, 0x9C, 0x70, 0);
  choice_ = 0;
  cursor->add_grid(&choice_, 0, 1, count_, 0, 0x14, 0, 0xE, list_);
  detail_ = new menu::TextBox;
  detail_->set_rect(0x9C, 0x58, 0x9C, 0x70, 0);
  menu::TextBox* b = list_;
  b->set_group(0);
  b->pen(0x10, 0x10, 0xE);
  // (A benched member's specialties are greyed: specialty_use_allowed [partybuild agent].)
  const bool greyed = !specialty_use_allowed(SpecialtyUse::kSpecialty);
  for (int i = 0; i < count_; ++i) {
    s16 e = entries_[i];
    if (e < 0) {
      b->set_group(0xF);
      e = static_cast<s16>(-e - 2);
    } else {
      b->set_group(4);
      e = static_cast<s16>(e - 1);
    }
    if (greyed && entries_[i] > 0) {
      b->text->set_colour(4);
      b->add_message(e);
      b->text->set_colour(1);
    } else {
      b->add_message(e);
    }
  }
  help_flags = 1;
}

void SpecialtyPage::setup_killer() {
  list_ = new menu::TextBox;
  list_->set_rect(0, 0x58, 0x9C, 0x70, 0);
  detail_ = new menu::TextBox;
  detail_->set_rect(0x9C, 0x58, 0x9C, 0x70, 0);
  help_flags = 1;
  party_update_link_combos();
  if (party_find_link_partner(party_selected_char()) == 0) {
    if (wears_link_item() && specialty_use_allowed(SpecialtyUse::kLinkCombo)) help_flags |= 2;
    if (party_selected_record()[0x3B] == 0) {
      set_ = 0;
      cursor->add_grid(&set_, 0, 1, 2, 0, 0x20, 0, 0x3A, list_);
    } else {
      set_ = 0;
      slot_ = 0;
      cursor->add_grid(&set_, 0, 1, 2, 0, 0x1A, 0, 0xE, list_);
      cursor->add_grid(&slot_, 0, 1, 2, 0, 0x52, 0, 0xE, list_);
    }
  } else {
    // Someone's Link Combo partner: its moves are the other member's to set.
    set_ = 0;
    cursor->add_grid(&set_, 0, 1, 1, 0, 0x12, 0, 0x3A, list_);
  }
  draw_killer_slots();
  draw_member();
}

void SpecialtyPage::draw_killer_slots() {
  menu::TextBox* b = list_;
  b->clear();
  const u8* record = party_selected_record();
  const s16 group = spec_byte(0x80084167 + party_selected_char());
  if (party_find_link_partner(party_selected_char()) != 0) {
    b->pen(0x10, 0xE, 0xE);
    b->set_group(0xC);
    b->add_message(4);
    return;
  }
  if (record[0x3B] == 0) {
    b->pen(0x10, 0xE, 0xE);
    b->set_group(0xC);
    b->add_message(2);
    b->set_group(group);
    b->add_message(record[0xCC]);
    b->pen(0x10, 0x48, 0xE);
    b->set_group(0xC);
    b->add_message(3);
    b->set_group(group);
    b->add_message(record[0xCD]);
  } else {
    b->pen(0x10, 8, 0xE);
    b->set_group(0xC);
    b->add_message(2);
    b->set_group(group);
    b->add_message(record[0xCC]);
    b->add_message(record[0xCE]);
    b->pen(0x10, 0x40, 0xE);
    b->set_group(0xC);
    b->add_message(3);
    b->set_group(group);
    b->add_message(record[0xCD]);
    b->add_message(record[0xCF]);
  }
}

void SpecialtyPage::setup_spells() {
  detail_ = new menu::TextBox;
  detail_->set_rect(0, 0x58, 0x138, 0x70, 0);
  for (s32& s : spells_) s = 0;
  u32 count = 0, table = 0;
  switch (party_selected_char()) {
    case 2: count = spec_byte(0x800840B0), table = 0x800840B4; break;
    case 3: count = spec_byte(0x800840B2), table = 0x800840E0; break;
    case 8: count = spec_byte(0x800840B1), table = 0x800840CC; break;
    case 0xB: count = spec_byte(0x800840B3), table = 0x800840F8; break;
    default: break;
  }
  const u8* record = party_selected_record();
  int n = 0;
  for (u32 i = 0; i < count; ++i, ++n) {
    const s8 known = static_cast<s8>(record[0x3C + i]);
    if (known != 0) spells_[i] = spec_byte(table + i) * known;
  }
  const s16 rows = static_cast<s16>(detail_->set_list(static_cast<s16>((n + 1) / 2), 7, 0x11C, 0xE, 0x10, 8));
  choice_ = 0;
  cursor->add_grid(&choice_, 2, 2, rows, 0, 0xC, 0x96, 0xE, detail_);
  detail_->set_group(0x2C);
  help_flags = 5;
}

void SpecialtyPage::draw_item(menu::TextBox* box, int slot, int index) {
  menu::TextRenderer* r = box->text;
  r->begin_line(box->scroll->line(slot));
  draw_spell(r, 0, index * 2);
  draw_spell(r, 0x96, index * 2 + 1);
  r->upload_line();
}

void SpecialtyPage::draw_spell(menu::TextRenderer* r, s16 x, int index) {
  s32 v = spells_[index];
  if (v == 0) return;
  r->set_pen_x(x);
  const bool usable = field_spell(static_cast<u32>(v < 0 ? -v : v)) && specialty_use_allowed(SpecialtyUse::kCastSpell);
  r->set_tables(usable ? 3 : 2);
  r->draw_pack_message(usable ? 0x10 : 0x12);
  r->set_pen_x(x + 0xC);
  if (v < 1) {
    r->set_tables(4);
    v = -v;
  } else {
    r->set_tables(1);
  }
  r->draw_current(r->message(v - 1));
}

void SpecialtyPage::refresh_info() {
  menu::TextBox* b = info;
  b->clear();
  b->pen_reset();
  const u8* record = party_selected_record();
  switch (state_) {
    case 2: {
      int index = cursor->active + 1;
      int set = 0;
      if (record[0x3B] == 0) {
        index = set_;
      } else if (index == 1) {
        set = set_;
        index = 0;
      } else {
        set = slot_;
        index = index - 1;
      }
      show_killer(record[0xCC + index + set * 2]);
      break;
    }
    case 1:
      show_entry();
      break;
    case 3: {
      const s32 v = spells_[choice_];
      show_spell(v < 0 ? -v : v);
      break;
    }
    default:
      break;
  }
}

void SpecialtyPage::show_entry() {
  menu::TextBox* b = info;
  b->clear();
  b->pen_reset();
  const s16 e = entries_[choice_];
  int index, first_colour;
  if (e == -3) {
    b->set_group(0x10);
    index = 1, first_colour = 1;
  } else if (e == -2) {
    b->set_group(0x10);
    index = 0, first_colour = 1;
  } else {
    b->set_group(7);
    index = e - 1, first_colour = 6;
  }
  b->add_message_lines(index, first_colour);
}

void SpecialtyPage::show_killer(int id) {
  menu::TextBox* b = info;
  b->clear();
  b->pen_reset();
  if (id == 0) return;
  const int character = party_selected_char();
  b->set_group(spec_byte(0x80084173 + character));
  b->add_message_lines(id, 6);
  const u32 costs = spec_u32(0x800841AC + character * 4);   // a table per character
  show_costs(b, rd16s(party_selected_record() + 0x8A + id * 2), spec_byte(costs + id - 1));
}

void SpecialtyPage::show_spell(int id) {
  menu::TextBox* b = info;
  b->clear();
  b->pen_reset();
  if (id == 0) return;
  b->set_group(0x2D);
  b->add_message_lines(id - 1, 6);
  show_costs(b, rd16s(party_selected_record() + 0x8C + choice_ * 2), spec_byte(0x8008406F + id));
}

void SpecialtyPage::show_costs(menu::TextBox* box, int uses, u32 cost) {
  box->set_group(0xC);
  menu::TextRenderer* r = box->text;
  if ((cost & 0x80) == 0) {
    box->pen(0x10, 0x40, 0xE);
    r->set_colour(2);
    box->add_message(1);
    r->set_colour(1);
    r->set_pen(0x50, 0x40, 0xC);
    r->add_number(uses, 4);
  }
  box->pen(text::layout(text::lang()).specialty_mp_x, 0x40, 0xE);  // NA 0x88
  r->set_colour(2);
  box->add_message(0);
  r->set_colour(1);
  r->set_pen(0xC0, 0x40, 0xC);
  r->add_number(stats.mp_cost(static_cast<int>(cost & 0x7F)), 2);
}

void SpecialtyPage::on_confirm(int n) {
  switch (state_) {
    case 2: {
      if (party_find_link_partner(party_selected_char()) != 0) return;
      if (bench_refused(SpecialtyUse::kKillerMoves, "killer moves")) return;
      auto* d = new KillerMoveList(this);
      open_child(1, d);
      int set = 0, slot;
      if (party_selected_record()[0x3B] == 0) {
        slot = set_;
      } else {
        set = n == 1 ? set_ : slot_;
        slot = n - 1;
      }
      d->set_target(set, slot);
      break;
    }
    case 3: {
      s32 v = spells_[choice_];
      if (v < 0) v = -v;
      if (!field_spell(static_cast<u32>(v))) return;
      if (bench_refused(SpecialtyUse::kCastSpell, "a spell cast")) return;   // (sfx 7) [partybuild agent]
      auto* t = new SpellTarget(this);
      open_child(9, t);
      t->set_spell(v, choice_);
      break;
    }
    case 1:
      if (entries_[choice_] > 0 && bench_refused(SpecialtyUse::kSpecialty, "specialty " + std::to_string(entries_[choice_])))
        return;   // (sfx 7) [partybuild agent]
      g_specialty = entries_[choice_];
      switch (g_specialty) {
        case 2: open_child(3, new OracleDialog(this)); break;
        case 3: open_child(3, new MusicDialog(this)); break;
        case 8: open_child(3, new PracticeDialog(this)); break;
        case 9: open_child(3, new ScoutDialog(this)); break;
        case 0xC: open_child(3, new FamiliarDialog(this)); break;
        case -3: state_ = -3; break;
        case -2: state_ = -2; break;
        default: break;
      }
      break;
    default:
      break;
  }
  Widget::on_confirm(n);
}

void SpecialtyPage::on_cancel(int n) {
  if (state_ < 4 && state_ > 1 && home_ == 1) {
    state_ = -1;   // back to the list
    n = -n;
  }
  Widget::on_cancel(n);
}

void SpecialtyPage::on_button2(int n) {
  if (wears_link_item() && state_ == 2) {
    if (bench_refused(SpecialtyUse::kLinkCombo, "the Link Combo")) return;   // (sfx 7) [partybuild agent]
    open_child(2, new LinkComboDialog);
    n = -n;
  }
  Widget::on_button2(n);
}

void SpecialtyPage::on_button3(int n) {
  if (state_ == 3 && specialty_use_allowed(SpecialtyUse::kSpellToggle)) {
    const s32 v = spells_[choice_];
    if (v != 0) {
      spells_[choice_] = -v;
      u8* record = party_selected_record();
      record[0x3C + choice_] = static_cast<u8>(-static_cast<s8>(record[0x3C + choice_]));
      cursor->set_flags(0x80);
      n = -n;
    }
  }
  Panel::on_button3(n);
}

void SpecialtyPage::on_child_done(int id, int state, int) {
  switch (id) {
    case 2:
      if (state == 3) state_ = -2;
      break;
    case 1:
      draw_killer_slots();
      refresh_info();
      break;
    case 3:
      refresh_info();
      if (state == 3) finish(0);
      break;
    default:
      break;
  }
}

void SpecialtyPage::on_member_changed() {
  home_ = static_cast<s16>(build_entries());
  state_ = static_cast<s16>(-home_);
  draw_member();
}

// [partybuild agent] A benched member (Archipelago's bench): a note where the game's logo goes,
// in the list (their specialties greyed) and in the spells (after the ON / OFF button's icon,
// which stays: switching a spell for battle is theirs to set).
void SpecialtyPage::draw_help() {
  Panel::draw_help();
  if (!party_selected_benched()) return;
  if (state_ == 1) {
    logo_state = 2;   // (no logo)
    logo_phase = 0;
    help->pen_at(0xA4, 4);
    help->add_string("Benched: can't use");
  } else if (state_ == 3) {
    logo_state = 3;   // (the button icons at rest, open)
    logo_phase = 0;
    help->pen_at(0xF8, 0);
    help->add_string("Benched");
    help->pen_at(0xF8, 8);
    help->add_string("no cast");
  }
}

void SpecialtyPage::draw_member() {
  Panel::draw_member();
  if (!member_box) return;
  menu::StrBuf sb;
  if (state_ == 2 && wears_link_item()) {
    sb.put(spec_text(party_selected_record()[0x3B] == 0 ? 0x80083540 : 0x80083534));   // LC OFF / LC ON
  } else {
    sb.put(spec_text(0x8008354C));
  }
  sb.put(spec_text(0x80083550));   // MP
  sb.put(spec_text(0x80083558));   // %3d
  sb.put_int(rd16s(party_selected_slot() + 0x24));
  member_box->add_string(sb.str());
}

}  // namespace

bool specialty_use_allowed(SpecialtyUse use) {
  if (!party_selected_benched()) return true;
  return use == SpecialtyUse::kKillerMoves || use == SpecialtyUse::kSpellToggle;
}

void menu_page02_update(menu::MenuPageLoader* p) {
  switch (p->substate) {
    case 1:
      p->publish_id();
      p->push(new SpecialtyPage);
      break;
    case 2:
      p->pop();
      break;
    case 3:
      p->goto_page(0xB);   // the familiar's shop
      break;
    default:
      break;
  }
}

}  // namespace ss::game::field
