#include "game/field/menu_skills.h"

#include <cstring>

#include "game/common/menu_core.h"
#include "game/common/menu_gfx.h"
#include "game/common/text_rules.h"
#include "game/common/menu_scene.h"
#include "game/common/menu_widget.h"
#include "game/common/party.h"
#include "game/field/field.h"
#include "game/field/item_effect.h"
#include "game/field/menu_status.h"

namespace ss::game::field {
namespace {

constexpr int kSkillsCode = 0xBC6;   // page 5's code overlay (at 0x8007e000)

const char* skills_text(u32 address) {
  const u8* p = menu::page_table(kSkillsCode, address);
  return p ? reinterpret_cast<const char*>(p) : "";
}
u8 skills_byte(u32 address) {
  const u8* p = menu::page_table(kSkillsCode, address);
  return p ? *p : 0;
}

s16 rd16s(const u8* p) {
  s16 v;
  std::memcpy(&v, p, 2);
  return v;
}
void wr16(u8* p, s16 v) { std::memcpy(p, &v, 2); }

s16 skill_points() { return rd16s(party_selected_record() + 0x1A); }   // record +0x1a

// 0x8007eae8: SP for the skill's next level (table 0x80080f92 + skill * 10 + level, less
// twice skill 0xb's level, at least 1); -1 at level 10.
int skill_cost(int skill) {
  const int level = char_skill_level(skill);
  if (level == 10) return -1;
  const int cost = skills_byte(0x80080F92 + skill * 10 + level) - char_skill_level(0xB) * 2;
  return cost < 1 ? 1 : cost;
}

// 0x8007ea10: pays and raises (a switched-off battle skill stays off).
bool raise_skill(int skill) {
  int level = char_skill_level(skill);
  if (level >= 10) return false;
  const int cost = skill_cost(skill);
  if (cost > skill_points()) return false;
  u8* record = party_selected_record();
  wr16(record + 0x1A, static_cast<s16>(skill_points() - cost));
  ++level;
  if (static_cast<s8>(record[skill + 0x5C]) < 0) level = -level;
  char_set_skill_level(skill, level);
  return true;
}

bool battle_skill_known(int skill) {
  return static_cast<u32>(skill - 0x21) < 0xE && party_selected_record()[skill + 0x5C] != 0;
}

// ==== The notices after a raise (Dialog, 0x120 bytes, vtable 0x80080b74; 0x80080290) =====
// Built before the raise with the specialty and super specialty levels, given the levels
// after it; one message box per change ("learned" or "rose to"), then it closes.
class RaiseNotice final : public menu::ConfirmDialog {   // [13] / [14] ConfirmDialog's
 public:
  RaiseNotice() = default;
  void snapshot_before();               // 0x800802d4
  void snapshot_after();                // 0x80080360
  void frame() override;                // [9] 0x800803ec
  void on_child_done(int id, int, int) override {   // [10] 0x80080600
    if (id == 1) --state_;
  }

 private:
  void notify_specialty(int character, int id, int old_level, int level);   // 0x8008062c
  void notify_super(int character, int id, int old_level, int level);       // 0x800807ac
  s32 spec_before_[17] = {};   // +0x50
  s32 spec_after_[17] = {};    // +0x94
  s32 super_before_[8] = {};   // +0xd8
  s32 super_after_[8] = {};    // +0xf8
  s32 state_ = 0;              // +0x118
  s32 seen_ = -1;              // +0x11c
};

void RaiseNotice::snapshot_before() {
  for (int i = 0; i < 17; ++i) spec_before_[i] = char_specialty_level(i + 1);
  for (int i = 0; i < 8; ++i) super_before_[i] = party_super_specialty_level(i + 1);
}

void RaiseNotice::snapshot_after() {
  for (int i = 0; i < 17; ++i) spec_after_[i] = char_specialty_level(i + 1);
  for (int i = 0; i < 8; ++i) super_after_[i] = party_super_specialty_level(i + 1);
}

void RaiseNotice::frame() {
  if (state_ != seen_) {
    seen_ = state_;
    bool shown = false;
    if (state_ == 0) {
      for (int i = 0; i < 17; ++i) {
        const s32 old = spec_before_[i];
        if (old != spec_after_[i]) {
          spec_before_[i] = spec_after_[i];
          if (spec_after_[i] != 0) {
            auto* d = new menu::ConfirmDialog;
            open_child(1, d);
            notify_specialty(party_selected_char(), i + 1, old, spec_after_[i]);
            shown = true;
            break;
          }
        }
      }
    } else if (state_ == 2) {
      for (int i = 0; i < 8; ++i) {
        const s32 old = super_before_[i];
        if (old != super_after_[i]) {
          super_before_[i] = super_after_[i];
          if (super_after_[i] != 0) {
            auto* d = new menu::ConfirmDialog;
            open_child(1, d);
            notify_super(party_selected_char(), i + 1, old, super_after_[i]);
            shown = true;
            break;
          }
        }
      }
    } else {
      if (state_ == 4) finish(0);
      Dialog::frame();
      return;
    }
    if (!shown) ++state_;
    ++state_;
  }
  Dialog::frame();
}

void RaiseNotice::notify_specialty(int character, int id, int old_level, int level) {
  auto* box = new menu::MessageBox;
  box->set_group(0xC);
  const u8* font = menu::strings_block(1);
  const u8* strings = menu::strings_block(0);
  menu::TextRenderer::set_expansion(0, font, strings, 0, character + 0x18);
  menu::TextRenderer::set_expansion(1, font, strings, 7, id - 1);
  if (old_level > 0) menu::TextRenderer::set_expansion(2, font, strings, 0, level);
  menu::message_box_show(box, old_level > 0 ? 1 : 0);
}

void RaiseNotice::notify_super(int, int id, int old_level, int level) {
  auto* box = new menu::MessageBox;
  box->set_group(0xC);
  const u8* font = menu::strings_block(1);
  const u8* strings = menu::strings_block(0);
  menu::TextRenderer::set_expansion(0, font, strings, 8, id - 1);
  if (old_level >= 1) menu::TextRenderer::set_expansion(1, font, strings, 0, level);
  menu::message_box_show(box, old_level < 1 ? 2 : 3);
}

// ==== A specialty's skills (Dialog, 0x5c bytes, vtable 0x80080cc4; 0x8007fb34) ===========
class SpecialtyDetail final : public menu::Dialog {
 public:
  SpecialtyDetail();
  void on_confirm(int) override { sound = 0; }   // [13] 0x8007fc0c
  void set_specialty(int id);                    // 0x8007fbec

 private:
  void show();                                   // 0x8007fc14
  s16 choice_ = 0;                               // +0x34
  menu::TextBox* info_ = nullptr;                // +0x50 (the list's)
  menu::TextBox* box_ = nullptr;                 // +0x54
  s32 id_ = 0;                                   // +0x58
};

SpecialtyDetail::SpecialtyDetail() {
  box_ = new menu::TitledBox;
  box_->set_rect(0, 0x58, 0x138, 0x70, 1);
  cursor->add_grid(&choice_, 1, 1, 1, 0, 0x34, 0, 0, box_);
}

void SpecialtyDetail::set_specialty(int id) {
  id_ = id;
  show();
}

void SpecialtyDetail::show() {
  menu::TextBox* b = box_;
  menu::TextRenderer* r = b->text_renderer();
  b->set_group(0xD);
  b->pen_at(10, 5);
  b->add_message(1);
  r->set_colour(2);
  b->pen_at(0x10, 0x20);
  b->add_message(3);
  b->pen_at(0x50, 0x20);
  b->add_message(4);
  r->set_colour(1);
  b->set_group(7);
  b->pen_at(0x10, 0x30);
  b->add_message(static_cast<s16>(id_ - 1));
  u16 info[6];
  char_specialty_info(id_, info);
  r->set_pen(0x7A, 0x30, 0xC);
  r->add_number(static_cast<s16>(info[5]), 2);
  b->set_group(0xD);
  r->set_colour(2);
  b->pen_at(0xA0, 0x20);
  b->add_message(5);
  b->pen_at(text::layout(text::lang()).skill_detail_x, 0x20);  // NA 0xe0
  b->add_message(6);
  r->set_colour(1);
  b->set_group(6);
  s16 y = 0x30;
  for (int i = 0; i < static_cast<s16>(info[4]); ++i) {
    b->pen_at(0xA0, y);
    const int skill = static_cast<s16>(info[i]);
    b->add_message(static_cast<s16>(skill - 1));
    r->set_pen(0x114, y, 0xC);
    r->add_number(char_skill_level(skill), 2);
    y = static_cast<s16>(y + 0x10);
  }
}

// ==== A super specialty (Dialog, 0x60 bytes, vtable 0x80080c1c; 0x8007fe4c) ==============
// Its specialties and their levels for each member, picked in the member box.
class SuperDetail final : public menu::Dialog {
 public:
  explicit SuperDetail(menu::TextBox* members);
  void on_cursor(int) override {                 // [12] 0x8007ff58
    party_select(choice_);
    show();
  }
  void on_confirm(int) override { sound = 0; }   // [13] 0x8007ff50
  void set_super(int id) {                       // 0x8007ff30
    id_ = id;
    show();
  }

 private:
  void show();                                   // 0x8007ff90
  void show_members();                           // 0x800801b8
  s16 choice_ = 0;                               // +0x34
  s32 id_ = 0;                                   // +0x50
  menu::TextBox* box_ = nullptr;                 // +0x54
  menu::TextBox* members_ = nullptr;             // +0x5c (the page's member box)
};

SuperDetail::SuperDetail(menu::TextBox* members) : members_(members) {
  box_ = new menu::TitledBox;
  box_->set_rect(0, 0x58, 0x138, 0x70, 1);
  choice_ = static_cast<s16>(party_get_selected());
  cursor->add_grid(&choice_, 1, 4, 2, 0, 0x1E, 0x16, 0x28, members_);
  show_members();
}

void SuperDetail::show_members() {
  menu::TextBox* b = members_;
  b->clear();
  const int selected = party_get_selected();
  for (int i = 0; i < 8; ++i) {
    party_select(i);
    const int character = party_selected_char();
    if (character == 0) continue;
    b->pen_at(static_cast<s16>((i % 4) * 0x16), static_cast<s16>((i / 4) << 5));
    b->add_party_character(character, 0x12, 0xC00);
  }
  party_select(selected);
}

void SuperDetail::show() {
  menu::TextBox* b = box_;
  menu::TextRenderer* r = b->text_renderer();
  b->clear();
  b->set_group(0xD);
  b->pen_at(10, 5);
  b->add_message(2);
  r->set_colour(2);
  b->pen_at(0x10, 0x20);
  b->add_message(7);
  b->pen_at(0x10, 0x40);
  b->add_message(8);
  r->set_colour(1);
  b->set_group(8);
  b->pen_at(0x10, 0x30);
  b->add_message(static_cast<s16>(id_ - 1));
  u16 info[5];
  party_super_specialty_info(id_, info);
  r->set_pen(0x10, 0x50, 0xC);
  r->add_number(static_cast<s16>(info[4]), 2);
  b->set_group(0xD);
  r->set_colour(2);
  b->pen_at(0xA0, 0x20);
  b->add_message(3);
  b->pen_at(0xE8, 0x20);
  b->add_message(4);
  r->set_colour(1);
  b->set_group(7);
  s16 y = 0x30;
  for (int i = 0; i < static_cast<s16>(info[3]); ++i) {
    box_->pen_at(0xA0, y);
    const int spec = static_cast<s16>(info[i]);
    b->add_message(static_cast<s16>(spec - 1));
    r->set_pen(0x114, y, 0xC);
    r->add_number(char_specialty_level(spec), 2);
    y = static_cast<s16>(y + 0x10);
  }
}

class SkillsPage;

// ==== Specialties (Dialog, 0x134 bytes, vtable 0x80080d6c; 0x8007f314) ===================
// The specialties and super specialties the member has started, with dots for the skills
// (or specialties) they need; a complete one opens its details.
class SpecialtyList final : public menu::Dialog {
 public:
  explicit SpecialtyList(SkillsPage* page);
  void draw_item(menu::TextBox* box, int slot, int index) override;   // [8] 0x8007f764
  void on_child_done(int id, int, int) override;                       // [10] 0x8007f6f4
  void on_cursor(int n) override;                                      // [12] 0x8007f4c8
  void on_confirm(int n) override;                                     // [13] 0x8007f5ec
  void on_button2(int n) override;                                     // [15] 0x8007f464

 private:
  struct Entry {          // 8 bytes at +0x54
    s16 specialty = 0;    // +0
    s16 super = 0;        // +2
    s16 done = 0;         // +4: skills (specialties) the member has
    s16 needed = 0;       // +6
  };
  int build();                                          // 0x8007f9e0
  void draw_row(menu::TextRenderer* r, s16 x, int index);   // 0x8007f804
  void show_specialty(int id);                          // 0x8007f524
  void show_super(int id);                              // 0x8007f588

  SkillsPage* page_ = nullptr;      // +0x08
  s16 choice_ = 0;                  // +0x34
  menu::TextBox* info_ = nullptr;   // +0x50: the page's info box
  Entry entries_[0x1B] = {};        // +0x54
  s32 selected_ = 0;                // +0x130
  menu::TextBox* box_ = nullptr;    // +0x12c
};

// ==== The page (Panel, 0xfc bytes, vtable 0x80080e24; 0x8007e740) ========================
class SkillsPage final : public menu::Panel {
 public:
  SkillsPage();
  void draw_item(menu::TextBox* box, int slot, int index) override;   // [8] 0x8007f080
  void on_child_done(int id, int, int) override {                      // [10] 0x8007ebcc
    if (id == 1) on_cursor(-1);
  }
  void on_cursor(int n) override;                                      // [12] 0x8007ec14
  void on_confirm(int n) override;                                     // [13] 0x8007e928
  void on_button2(int n) override;                                     // [15] 0x8007e820
  void on_button3(int n) override;                                     // [16] 0x8007e884
  void on_member_changed() override;                                   // [21] 0x8007ecac
  void draw_member() override;                                         // [23] 0x8007ed10

  menu::TextBox* info_box() const { return info; }
  menu::TextBox* members_box() const { return member_box; }

 private:
  void refresh_list();                                    // 0x8007ee80
  int build_list();                                       // 0x8007f26c
  void draw_row(menu::TextRenderer* r, s16 x, int index); // 0x8007f120
  void show_description(int skill);                       // 0x8007eb6c

  s16 choice_ = 0;                  // +0x34
  menu::TextBox* list_ = nullptr;   // +0x98
  s16 skills_[0x30] = {};           // +0x9c: skill id per row slot (0 none)
};

// ==== SkillsPage ===========================================================================

SkillsPage::SkillsPage() {
  menu::MenuGlobals& g = menu::globals();
  menu::strings_set(g.data.data());
  menu::TextRenderer::set_default_font(menu::strings_block(1), menu::strings_block(0));
  create_help();
  create_member_box(0);
  draw_member();
  create_info_box();
  list_ = new menu::TextBox;
  list_->set_rect(0, 0x58, 0x138, 0x70, 0);
  refresh_list();
  help_flags = 0x207;
}

int SkillsPage::build_list() {
  for (s16& s : skills_) s = 0;
  for (int skill = 1; skill <= 0x2E; ++skill) {
    if (skill_book_allowed(skill) || char_skill_level(skill) != 0) skills_[skill - 1] = static_cast<s16>(skill);
  }
  return 0x2E;
}

void SkillsPage::refresh_list() {
  menu::TextBox* box = list_;
  box->clear();
  box->set_font_group(menu::strings_block(1), menu::strings_block(0), 1);
  menu::TextRenderer* r = box->text;
  const text::Layout& lay = text::layout(text::lang());
  r->set_colour(2);
  if (lay.skills_heading_message) {
    // France / Germany: the translated heading, group 0xf message 0 of the strings.
    box->set_group(0xF);
    box->pen_at(lay.skills_heading_x[0], 8);
    box->add_message(0);
    box->pen_at(lay.skills_heading_x[1], 8);
    box->add_message(0);
  } else {
    box->pen_at(lay.skills_heading_x[0], 8);  // NA 0x38
    box->add_ascii_text(skills_text(0x80080AC0));   // SKILL LV SP
    box->pen_at(lay.skills_heading_x[1], 8);  // NA 0xcc
    box->add_ascii_text(skills_text(0x80080AC0));
  }
  r->set_colour(1);
  box->set_group(6);
  int rows = (build_list() + 1) / 2;
  if (rows == 0) rows = 1;
  const s16 shown = static_cast<s16>(box->set_list(static_cast<s16>(rows), 6, 0x11C, 0xE, 0x10, 8));
  box->scroll->v[8] = 0x16;
  cursor->clear_groups();
  choice_ = 0;
  cursor->add_grid(&choice_, 2, 2, shown, 0, 0x1A, 0x96, 0xE, list_);
}

void SkillsPage::draw_item(menu::TextBox* box, int slot, int index) {
  menu::TextRenderer* r = box->text;
  r->begin_line(box->scroll->line(slot));
  const s16 row = static_cast<s16>(index);
  draw_row(r, 0, row * 2);
  draw_row(r, 0x96, row * 2 + 1);
  r->upload_line();
}

void SkillsPage::draw_row(menu::TextRenderer* r, s16 x, int index) {
  const s16 skill = skills_[static_cast<s16>(index)];
  if (skill == 0) return;
  r->set_pen_x(x);
  int table = 1;
  if (skill >= 0x21) table = static_cast<s8>(party_selected_record()[skill + 0x5C]) < 0 ? 4 : 3;
  r->set_tables(table);
  r->draw_current(r->message(skill - 1));
  r->set_tables(1);
  r->set_pen_x(x + 0x5A);
  r->draw_two_digits(char_skill_level(skill));
  r->set_pen_x(x + 0x73);
  const int cost = skill_cost(skill);
  if (cost < 0) {
    r->draw_pack_message(0xC);
  } else {
    r->draw_two_digits(cost);
  }
}

void SkillsPage::show_description(int skill) {
  menu::TextBox* box = info;
  box->clear();
  box->pen_reset();
  if (skill != 0) {
    box->set_group(9);
    box->add_message_lines(skill - 1, 6);
  }
}

void SkillsPage::on_cursor(int) {
  const int skill = skills_[choice_];
  show_description(skill);
  if (battle_skill_known(skill)) {
    help_flags |= 4;
  } else {
    help_flags &= 0xFFFB;
  }
}

void SkillsPage::on_confirm(int n) {
  const int skill = skills_[choice_];
  if (skill == 0) return;
  auto* notice = new RaiseNotice;
  notice->snapshot_before();
  const bool raised = raise_skill(skill);
  notice->snapshot_after();
  if (!raised) return;
  StatChange bonus;
  stat_change_begin(&bonus);   // 0x8007e28c
  skill_level_bonus(&bonus, skill);   // 0x8007e2c0 (the copy of 3001's)
  cursor->set_flags(0x80);
  draw_member();
  Widget::on_confirm(n);
}

void SkillsPage::on_button2(int n) {
  open_child(1, new SpecialtyList(this));
  Widget::on_button2(-n);
}

void SkillsPage::on_button3(int n) {
  const int skill = skills_[choice_];
  if (battle_skill_known(skill)) {
    u8* record = party_selected_record();
    record[skill + 0x5C] = static_cast<u8>(-static_cast<s8>(record[skill + 0x5C]));
    cursor->set_flags(0x80);
    Panel::on_button3(-n);
    return;
  }
  sound = 0;
}

void SkillsPage::on_member_changed() {
  draw_member();
  refresh_list();
  on_cursor(-1);
}

void SkillsPage::draw_member() {
  Panel::draw_member();
  if (!member_box) return;
  menu::StrBuf sb;
  sb.put(skills_text(0x80080AB0));   // SP
  sb.put(skills_text(0x80080ABC));   // %3d
  sb.put_int(skill_points());
  member_box->add_string(sb.str());
}

// ==== SpecialtyList ========================================================================

SpecialtyList::SpecialtyList(SkillsPage* page) : page_(page) {
  info_ = page->info_box();
  box_ = new menu::TitledBox;
  box_->set_group(0xD);
  box_->pen_at(10, 5);
  box_->add_message(0);
  box_->set_group(7);
  box_->set_rect(0, 0x58, 0x138, 0x70, 0);
  int rows = (build() + 1) / 2;
  if (rows == 0) rows = 1;
  const s16 shown = static_cast<s16>(box_->set_list(static_cast<s16>(rows), 6, 0x11C, 0xE, 0x10, 0x16));
  cursor->add_grid(&choice_, 2, 2, shown, 0, 0x1A, 0x96, 0xE, box_);
  help_flags = 0x102;
}

int SpecialtyList::build() {
  for (Entry& e : entries_) e = Entry{};
  int n = 0;
  for (int id = 1; id <= 0x11; ++id) {
    u16 info[6];
    char_specialty_info(id, info);
    if (static_cast<s16>(info[5]) == -1) continue;
    entries_[n].specialty = static_cast<s16>(id);
    entries_[n].done = static_cast<s16>(info[3]);
    entries_[n].needed = static_cast<s16>(info[4]);
    ++n;
  }
  for (int id = 1; id <= 8; ++id) {
    u16 info[5];
    party_super_specialty_info(id, info);
    if (static_cast<s16>(info[4]) == -1) continue;
    entries_[n].super = static_cast<s16>(id);
    entries_[n].done = static_cast<s16>(info[2]);
    entries_[n].needed = static_cast<s16>(info[3]);
    ++n;
  }
  return n;
}

void SpecialtyList::draw_item(menu::TextBox* box, int slot, int index) {
  menu::TextRenderer* r = box->text;
  r->begin_line(box->scroll->line(slot));
  const s16 row = static_cast<s16>(index);
  draw_row(r, 0, row * 2);
  draw_row(r, 0x96, row * 2 + 1);
  r->upload_line();
}

void SpecialtyList::draw_row(menu::TextRenderer* r, s16 x, int index) {
  const Entry& e = entries_[static_cast<s16>(index)];
  const bool complete = e.done == e.needed;
  if (e.specialty != 0) {
    r->set_tables(complete ? 1 : 4);
    r->set_pen_x(x);
    r->draw_current(r->message(e.specialty - 1));
    for (int i = 0; i < e.needed; ++i) {
      r->set_pen_x(x + 0x5A + i * 10);
      const bool lit = i + 1 <= e.done;
      r->set_tables(lit ? 1 : 4);
      r->draw_pack_message(lit ? 0x15 : 0x14);
    }
  } else if (e.super != 0) {
    r->set_tables(complete ? 1 : 4);
    r->set_pen_x(x);
    r->draw_current(r->message(e.super + 0x10));
    for (int i = 0; i < e.needed; ++i) {
      r->set_pen_x(x + 0x5A + i * 10);
      const bool lit = i + 1 <= e.done;
      r->set_tables(lit ? 1 : 4);
      r->draw_pack_message(lit ? 0x11 : 0x10);
    }
  }
}

void SpecialtyList::show_specialty(int id) {
  info_->clear();
  info_->pen_reset();
  info_->set_group(10);
  if (id != 0) info_->add_message_lines(id - 1, 6);
}

void SpecialtyList::show_super(int id) {
  info_->clear();
  info_->pen_reset();
  info_->set_group(0xB);
  if (id != 0) info_->add_message_lines(id - 1, 6);
}

void SpecialtyList::on_cursor(int) {
  const Entry& e = entries_[choice_];
  if (e.specialty == 0) {
    show_super(e.super != 0 ? e.super : 0);
  } else {
    show_specialty(e.specialty);
  }
}

void SpecialtyList::on_confirm(int n) {
  const Entry& e = entries_[choice_];
  if (e.specialty == 0) {
    if (e.super == 0 || e.done != e.needed) return;
    selected_ = party_get_selected();
    auto* d = new SuperDetail(page_->members_box());
    open_child(2, d);
    d->set_super(e.super);
  } else {
    if (e.done != e.needed) return;
    auto* d = new SpecialtyDetail;
    open_child(1, d);
    d->set_specialty(e.specialty);
  }
  Widget::on_confirm(n);
}

void SpecialtyList::on_child_done(int id, int, int) {
  if (id == 2) {
    party_select(selected_);
    page_->draw_member();
  }
}

void SpecialtyList::on_button2(int n) {
  open_child(3, create_talents_dialog());
  Widget::on_button2(-n);
}

}  // namespace

// ==== Page handler ========================================================================

void menu_page05_update(menu::MenuPageLoader* p) {
  switch (p->substate) {
    case 1:
      p->publish_id();
      p->push(new SkillsPage);
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
