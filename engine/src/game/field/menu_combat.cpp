#include "game/field/menu_combat.h"

#include <cstring>
#include <string>
#include <vector>

#include "disc/slz.h"
#include "game/ap/ap.h"
#include "game/common/menu_core.h"
#include "game/common/menu_gfx.h"
#include "game/common/menu_scene.h"
#include "game/common/menu_widget.h"
#include "game/common/party.h"
#include "game/common/screen.h"
#include "game/field/field.h"
#include "game/field/load_queue.h"
#include "psx/gte.h"
#include "runtime/libgpu.h"
#include "runtime/libgte.h"

namespace ss::game::field {
namespace {

constexpr int kCombatCode = 0xBCA;   // pages 8-10's code overlay (at 0x8007e000)

const char* combat_text(u32 address) {
  const u8* p = menu::page_table(kCombatCode, address);
  return p ? reinterpret_cast<const char*>(p) : "";
}
u8 combat_byte(u32 address) {
  const u8* p = menu::page_table(kCombatCode, address);
  return p ? *p : 0;
}
s32 combat_s32(u32 address) {
  const u8* p = menu::page_table(kCombatCode, address);
  if (!p) return 0;
  s32 v;
  std::memcpy(&v, p, 4);
  return v;
}
s16 combat_s16(u32 address) {
  const u8* p = menu::page_table(kCombatCode, address);
  if (!p) return 0;
  s16 v;
  std::memcpy(&v, p, 2);
  return v;
}

std::vector<u8> unpack(const u8* packed) {
  const size_t size = read_u32(packed + 4) + 0x10;
  auto out = disc::slz::unpack_block(std::span<const u8>(packed, size), 0, 0, nullptr);
  return out ? std::move(*out) : std::vector<u8>{};
}

// The grid runs row by row over boxes laid out column by column (table 0x800808e0).
int box_slot(int choice) { return combat_byte(0x800808E0 + choice); }
// The class of a character (table 0x800808e7): which six orders it has.
int char_class(int character) { return combat_byte(0x800808E7 + character); }

// Eight member boxes, two columns of four (the Tactics and Party Order pages).
void make_member_boxes(menu::TextBox* boxes[8]) {
  for (int i = 7; i >= 0; --i) {
    boxes[i] = new menu::TextBox;
    boxes[i]->set_rect((i / 4) * 0x9C, (i % 4) * 0x32, 0x9C, 0x32, 0);
  }
}

// ==== Tactics: one member's orders (Dialog, 0x54 bytes, vtable 0x800806b0; 0x8007e34c) ===
class OrdersDialog final : public menu::Dialog {
 public:
  OrdersDialog();
  void set_member(int slot);               // 0x8007e3f8
  void on_confirm(int n) override;         // [13] 0x8007e504

 private:
  s16 choice_ = 0;                         // +0x34
  menu::TextBox* box_ = nullptr;           // +0x50
};

OrdersDialog::OrdersDialog() {
  box_ = new menu::TitledBox;
  box_->set_rect_centered(0xE0, 0x78, 1);
  box_->set_font_group(menu::strings_block(1), menu::strings_block(0), 3);
  box_->pen_at(10, 5);
  box_->add_message(0);
}

void OrdersDialog::set_member(int slot) {
  box_->set_group(4);
  box_->set_highlight_next(&choice_);
  box_->pen(0x10, 0x1C, 0xE);
  party_select(slot);   // (left selected)
  const int character = party_selected_char();
  for (int i = 0; i < 6; ++i) box_->add_message(static_cast<s16>(char_class(character) * 6 + i));
  choice_ = party_selected_record()[0x38];
  cursor->add_grid(&choice_, 1, 1, 6, 0, 0x20, 0, 0xE, box_);
}

void OrdersDialog::on_confirm(int n) {
  u8* record = party_selected_record();
  if (choice_ == 5 && record[0x38] != 5) record[0x39] = record[0x38];   // the orders before "stand by"
  record[0x38] = static_cast<u8>(choice_);
  finish(0);
  Widget::on_confirm(n);
}

// ==== Tactics (Panel, 0xb8 bytes, vtable 0x80080768; 0x8007e000) =========================
class TacticsPage final : public menu::Panel {
 public:
  TacticsPage();
  void on_child_done(int, int state, int) override {   // [10] 0x8007e1ec
    if (state == 3) refresh();
  }
  void on_confirm(int n) override;                    // [13] 0x8007e14c

 private:
  void refresh();                                     // 0x8007e21c
  s16 choice_ = 0;                                    // +0x34
  menu::TextBox* boxes_[8] = {};                      // +0x98
};

TacticsPage::TacticsPage() {
  menu::strings_set(menu::globals().data.data());
  menu::TextRenderer::set_default_font(menu::strings_block(1), menu::strings_block(0));
  create_help();
  make_member_boxes(boxes_);
  refresh();
  cursor->add_grid(&choice_, 1, 2, 4, 0, 0x20, 0x9C, 0x32, nullptr);
}

void TacticsPage::refresh() {
  const int selected = party_get_selected();
  for (int i = 0; i < 8; ++i) {
    party_select(i);
    menu::TextBox* b = boxes_[i];
    b->clear();
    b->set_font_group(menu::strings_block(1), menu::strings_block(0), 4);
    const int character = party_selected_char();
    if (character == 0) continue;
    b->pen_at(8, 0);
    b->add_party_character(static_cast<s16>(read_u16(party_selected_slot())), 6, 0x1000);
    b->pen_at(0x28, 0xC);
    b->add_message_lines(char_class(character) * 6 + party_selected_record()[0x38], 1);
  }
  party_select(selected);
}

void TacticsPage::on_confirm(int n) {
  const int slot = box_slot(choice_);
  if (party_slot_active(slot) == 0) return;
  auto* d = new OrdersDialog;
  open_child(1, d);
  d->set_member(slot);
  Widget::on_confirm(n);
}

// ==== Party Order (Panel, 0xcc bytes, vtable 0x800805e0; 0x8007e594) =====================
class PartyOrderPage final : public menu::Panel {
 public:
  PartyOrderPage();
  ~PartyOrderPage() override;                         // [1] 0x8007e760
  void on_confirm(int n) override;                    // [13] 0x8007e908
  void on_cancel(int n) override;                     // [14] 0x8007eae8

 private:
  void refresh();                                     // 0x8007eb78
  void draw_member_box(menu::TextBox* box);           // 0x8007ec14
  s16 choice_ = 0;                                    // +0x34
  menu::TextBox* boxes_[8] = {};                      // +0x98
  s16 front_[4] = {};                                 // +0xb8: the front four's characters
  menu::Cursor* first_cursor_ = nullptr;              // +0xc0
  s16 state_ = 0;                                     // +0xc4: 1 the first member picked
  s16 second_ = 0;                                    // +0xc6
  s32 selected_ = 0;                                  // +0xc8
};

void draw_order_box(menu::TextBox* box);   // (below)

// 0x8007f0bc: two party slots and their records change places.
void swap_members(int a, int b) {
  u8* slots = party_slots();
  u8* records = party_records();
  u8 tmp[0xD0];
  std::memcpy(tmp, slots + a * 0x60, 0x60);
  std::memcpy(slots + a * 0x60, slots + b * 0x60, 0x60);
  std::memcpy(slots + b * 0x60, tmp, 0x60);
  std::memcpy(tmp, records + a * 0xD0, 0xD0);
  std::memcpy(records + a * 0xD0, records + b * 0xD0, 0xD0);
  std::memcpy(records + b * 0xD0, tmp, 0xD0);
}

PartyOrderPage::PartyOrderPage() {
  menu::strings_set(menu::globals().data.data());
  menu::TextRenderer::set_default_font(menu::strings_block(1), menu::strings_block(0));
  create_help();
  selected_ = party_get_selected();
  for (int i = 0; i < 4; ++i) {
    party_select(i);
    front_[i] = static_cast<s16>(party_selected_char());
  }
  make_member_boxes(boxes_);
  refresh();
  cursor->add_grid(&choice_, 1, 2, 4, 0, 0x20, 0x9C, 0x32, nullptr);
  state_ = 0;
}

PartyOrderPage::~PartyOrderPage() {
  preload_release_pending();
  // New members in the front four get their voices; the bench loses its pairings.
  for (int i = 0; i < 4; ++i) {
    party_select(i);
    if (front_[i] != party_selected_char() && g_voice_reload_member) g_voice_reload_member(i);
  }
  for (int i = 4; i < 8; ++i) {
    party_select(i);
    if (party_selected_char() == 0) continue;
    u8* record = party_selected_record();
    record[0x3B] = 0;
    record[0xCE] = 0;
    record[0xCF] = 0;
  }
  party_select(selected_);
  if (party_selected_char() == 0) {
    for (int i = 0; i < 8; ++i) {
      party_select(i);
      if (party_selected_char() != 0) break;
    }
  }
  u8* config = party_config();
  if (party_slot_active(config[0x41]) == 0) {
    for (int i = 0; i < 8; ++i) {
      if (party_slot_active(i) != 0) {
        config[0x41] = static_cast<u8>(i);
        break;
      }
    }
  }
}

void PartyOrderPage::refresh() {
  const int selected = party_get_selected();
  for (int i = 0; i < 8; ++i) {
    party_select(i);
    boxes_[i]->clear();
    draw_member_box(boxes_[i]);
  }
  party_select(selected);
}

void PartyOrderPage::draw_member_box(menu::TextBox* box) { draw_order_box(box); }

// 0x8007ec14's body: the selected member's box (sprite, name, level, HP / MP and gauges).
void draw_order_box(menu::TextBox* box) {
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
    sb.set(combat_text(0x80080488));   // ""
    sb.put(combat_text(0x8008048C));   // LV
    sb.put(combat_text(0x80080494));   // %3d
    sb.put_int(static_cast<s16>(read_u16(party_selected_slot() + 0x26)));
    box->add_string(sb.str());
  }
  box->pen_at(0x28, 0x18);
  {
    menu::StrBuf sb;
    sb.set(combat_text(0x80080488));
    for (u32 a : {0x80080498u, 0x800804A0u, 0x800804A4u, 0x800804ACu}) sb.put(combat_text(a));   // HP / MP
    box->add_string(sb.str());
  }
  box->pen_at(0x48, 0x18);
  box->add_party_status(static_cast<s16>(party_get_selected()));
}

void PartyOrderPage::on_confirm(int n) {
  if (state_ == 0) {
    first_cursor_ = push_cursor();
    second_ = choice_;
    cursor->add_grid(&second_, 1, 2, 4, 8, 0x20, 0x9C, 0x32, nullptr);
    ++state_;
  } else if (state_ == 1) {
    swap_members(box_slot(second_), box_slot(choice_));
    bool fighter = false;
    const int selected = party_get_selected();
    for (int i = 0; i < 4; ++i) {
      party_select(i);
      if (party_selected_char() != 0 && (party_selected_status() & 7) == 0) fighter = true;
    }
    party_select(selected);
    if (!fighter) {
      swap_members(box_slot(second_), box_slot(choice_));
      return;
    }
    refresh();
    cursor->on_close();
    cursor = first_cursor_;
    --state_;
  }
  Widget::on_confirm(n);
}

void PartyOrderPage::on_cancel(int n) {
  if (state_ == 0) {
    cancel();
  } else if (state_ == 1) {
    pop_cursor(first_cursor_);
    --state_;
  }
  Widget::on_cancel(-n);
}

// ==== Party Order with the bench [partybuild agent] ======================================
// Archipelago (port feature; docs/handoff/bench.md B1: Option A of the user's mock-up, two
// columns and a flip). Opened only with a bench seed (ap::bench_enabled()); every other game
// opens PartyOrderPage above as it is. The left column is the front four (slots 0-3) as today;
// the right column shows the reserve (slots 4-7) or, after L1 / R1 (the menus' page buttons),
// the bench (places 8-11), and the help bar says which ("L1/R1: Reserve 5-8" / "L1/R1: Bench
// 9-12", the buttons as the config has them, where the game's logo goes on the other pages). A pick is a place 0-11, so a first pick survives the flip: any two
// of the 12 places change places, the reserve and the bench included. The held pick's hand
// hides while its column is off screen, and the help bar names the member held. While the page
// is open it works on a copy of the 8 slots and the bench (party_bind / party_bind_places: every
// accessor and the boxes' HP / MP read the copy); the destructor writes the copy back (the 8
// slots, the bench's store) and then does what the original's does (voices for a new front
// member, the Link Combo bonds of everyone behind the front four, the bench included, the
// leader's slot). A move to or from the bench can be refused (ap::bench_swap_refused: sfx 7, as
// the original's swap that leaves no front member able to fight, which still applies).
class BenchOrderPage final : public menu::Panel {
 public:
  BenchOrderPage();
  ~BenchOrderPage() override;
  void on_confirm(int n) override;
  void on_cancel(int n) override;
  void on_pressed(u32 buttons) override;
  void draw_help() override;

 private:
  int place(int choice) const;   // grid position -> place 0-11 (the page shown)
  bool shown(int place) const;   // the place's box is on screen
  void refresh();
  void show_held();
  s16 choice_ = 0;
  menu::TextBox* boxes_[8] = {};
  s16 front_[4] = {};
  menu::Cursor* first_cursor_ = nullptr;
  s16 state_ = 0;
  s16 second_ = 0;
  s32 selected_ = 0;
  int bench_page_ = 0;     // the right column: 0 the reserve (4-7), 1 the bench (8-11)
  int first_place_ = -1;   // the first pick while state_ is 1
  // The working copy: places 0-11 (the 8 slots, then the bench), and what was bound before.
  u8 slots_[12 * 0x60] = {};
  u8 records_[12 * 0xD0] = {};
  u8* game_config_ = nullptr;
  u8* game_inventory_ = nullptr;
  u8* game_slots_ = nullptr;
  u8* game_records_ = nullptr;
  PartyPlaces bound_places_;
  bool places_were_bound_ = false;
};

BenchOrderPage::BenchOrderPage() {
  menu::strings_set(menu::globals().data.data());
  menu::TextRenderer::set_default_font(menu::strings_block(1), menu::strings_block(0));
  create_help();
  selected_ = party_get_selected();
  for (int i = 0; i < 4; ++i) {
    party_select(i);
    front_[i] = static_cast<s16>(party_selected_char());
  }
  game_config_ = party_config();
  game_inventory_ = party_inventory();
  game_slots_ = party_slots();
  game_records_ = party_records();
  if (const PartyPlaces* p = party_bound_places()) {
    bound_places_ = *p;
    places_were_bound_ = true;
  }
  std::memcpy(slots_, game_slots_, 8 * 0x60);
  std::memcpy(records_, game_records_, 8 * 0xD0);
  if (const ap::Bench* b = ap::bench_store()) {
    for (int i = 0; i < ap::kBenchPlaces; ++i) {
      std::memcpy(slots_ + (8 + i) * 0x60, b->slot(i), 0x60);
      std::memcpy(records_ + (8 + i) * 0xD0, b->record(i), 0xD0);
    }
  }
  PartyPlaces work;
  for (int i = 0; i < 4; ++i) {
    work.slot[i] = slots_ + (8 + i) * 0x60;
    work.record[i] = records_ + (8 + i) * 0xD0;
  }
  party_bind(game_config_, game_inventory_, slots_, records_);
  party_bind_places(&work);
  make_member_boxes(boxes_);
  refresh();
  cursor->add_grid(&choice_, 1, 2, 4, 0, 0x20, 0x9C, 0x32, nullptr);
  state_ = 0;
}

BenchOrderPage::~BenchOrderPage() {
  preload_release_pending();
  // The copy back: the 8 slots, the bench; then the blocks bound before.
  std::memcpy(game_slots_, slots_, 8 * 0x60);
  std::memcpy(game_records_, records_, 8 * 0xD0);
  if (ap::Bench* b = ap::bench_store()) {
    for (int i = 0; i < ap::kBenchPlaces; ++i) {
      u8* record = records_ + (8 + i) * 0xD0;
      if (static_cast<s16>(read_u16(slots_ + (8 + i) * 0x60)) != 0) {  // (the bench loses its pairings too)
        record[0x3B] = 0;
        record[0xCE] = 0;
        record[0xCF] = 0;
      }
      b->put(i, slots_ + (8 + i) * 0x60, record);
    }
  }
  party_bind(game_config_, game_inventory_, game_slots_, game_records_);
  party_bind_places(places_were_bound_ ? &bound_places_ : nullptr);
  // The original's close (0x8007e760) on the 8 slots.
  for (int i = 0; i < 4; ++i) {
    party_select(i);
    if (front_[i] != party_selected_char() && g_voice_reload_member) g_voice_reload_member(i);
  }
  for (int i = 4; i < 8; ++i) {
    party_select(i);
    if (party_selected_char() == 0) continue;
    u8* record = party_selected_record();
    record[0x3B] = 0;
    record[0xCE] = 0;
    record[0xCF] = 0;
  }
  party_select(selected_);
  if (party_selected_char() == 0) {
    for (int i = 0; i < 8; ++i) {
      party_select(i);
      if (party_selected_char() != 0) break;
    }
  }
  u8* config = party_config();
  if (party_slot_active(config[0x41]) == 0) {
    for (int i = 0; i < 8; ++i) {
      if (party_slot_active(i) != 0) {
        config[0x41] = static_cast<u8>(i);
        break;
      }
    }
  }
  ap::on_bench_order_closed();
}

// A pad button's short name (its bit as the config keeps it).
std::string button_name(u16 bit) {
  static const char* const kNames[16] = {"L2", "R2", "L1", "R1", "Tri", "O", "X", "Sq",
                                         "Sel", "L3", "R3", "St", "Up", "Rt", "Dn", "Lt"};
  for (int i = 0; i < 16; ++i)
    if (bit == (1u << i)) return kNames[i];
  return "?";
}

int BenchOrderPage::place(int choice) const {
  const int slot = box_slot(choice);
  return slot >= 4 && bench_page_ != 0 ? slot + 4 : slot;
}

bool BenchOrderPage::shown(int place) const { return place < 4 || (place >= 8) == (bench_page_ != 0); }

void BenchOrderPage::refresh() {
  const int selected = party_get_selected();
  for (int i = 0; i < 8; ++i) {
    party_select(i >= 4 && bench_page_ != 0 ? i + 4 : i);
    boxes_[i]->clear();
    draw_order_box(boxes_[i]);
  }
  party_select(selected);
}

// The first pick's hand stays on its box while that box is shown (the help bar names the member
// held otherwise).
void BenchOrderPage::show_held() {
  if (first_cursor_ == nullptr) return;
  if (state_ != 1 || shown(first_place_)) {
    first_cursor_->flags |= 1;
  } else {
    first_cursor_->flags = static_cast<u16>(first_cursor_->flags & ~1u);
  }
}

void BenchOrderPage::draw_help() {
  help->add_message(menu::globals().session->page_id);
  // In the logo's place: which column the right side shows; under it, the member held while
  // their column is off screen, else whether the bench is locked now.
  logo_state = 2;
  logo_phase = 0;
  std::string held;
  if (state_ == 1 && !shown(first_place_)) {
    const u8* slot = slots_ + first_place_ * 0x60;
    held = static_cast<s16>(read_u16(slot)) != 0 ? std::string("Held: ") + reinterpret_cast<const char*>(records_ + first_place_ * 0xD0 + 0x24)
                                                 : std::string("Held: (empty)");
  } else if (ap::bench_locked()) {
    held = "Bench locked";
  }
  const u8* config = party_config();
  std::string hint = button_name(read_u16(config + 8)) + "/" + button_name(read_u16(config + 0xA)) + ": ";
  hint += bench_page_ != 0 ? "Bench 9-12" : "Reserve 5-8";
  help->pen_at(0xA4, held.empty() ? 4 : 0);
  help->add_string(hint.c_str());
  if (!held.empty()) {
    help->pen_at(0xA4, 8);
    help->add_string(held.c_str());
  }
}

// The menus' page buttons (config buttons 4 / 5: L1 / R1 unless the player changed them, the
// lists' page up / down) flip the right column; they do nothing on the original's page.
void BenchOrderPage::on_pressed(u32 buttons) {
  const u8* config = party_config();
  if ((buttons & (read_u16(config + 8) | read_u16(config + 0xA))) == 0) return;
  bench_page_ = 1 - bench_page_;
  refresh();
  show_held();
  sound = 1;
  input_delay = 4;
}

void BenchOrderPage::on_confirm(int n) {
  if (state_ == 0) {
    first_cursor_ = push_cursor();
    second_ = choice_;
    first_place_ = place(choice_);
    cursor->add_grid(&second_, 1, 2, 4, 8, 0x20, 0x9C, 0x32, nullptr);
    ++state_;
  } else if (state_ == 1) {
    const int a = first_place_, b = place(second_);
    if ((a >= 8) != (b >= 8)) {
      // A member to or from the bench: the port's rules first.
      const int slot = a < 8 ? a : b, bench = a < 8 ? b : a;
      const s16 in_slot = static_cast<s16>(read_u16(slots_ + slot * 0x60));
      const s16 benched = static_cast<s16>(read_u16(slots_ + bench * 0x60));
      if (ap::bench_swap_refused(in_slot, benched) != nullptr) return;
    }
    swap_members(a, b);
    bool fighter = false;
    const int selected = party_get_selected();
    for (int i = 0; i < 4; ++i) {
      party_select(i);
      if (party_selected_char() != 0 && (party_selected_status() & 7) == 0) fighter = true;
    }
    party_select(selected);
    if (!fighter) {
      swap_members(a, b);
      return;
    }
    refresh();
    cursor->on_close();
    cursor = first_cursor_;
    --state_;
    show_held();
    first_place_ = -1;
  }
  Widget::on_confirm(n);
}

void BenchOrderPage::on_cancel(int n) {
  if (state_ == 0) {
    cancel();
  } else if (state_ == 1) {
    pop_cursor(first_cursor_);
    --state_;
    show_held();
    first_place_ = -1;
  }
  Widget::on_cancel(-n);
}

// ==== Formation: the leader (Dialog, 0x54 bytes, vtable 0x80080838; 0x800801ac) ==========
class LeaderDialog final : public menu::Dialog {
 public:
  LeaderDialog();
  void on_confirm(int n) override;         // [13] 0x80080334

 private:
  s16 choice_ = 0;                         // +0x34
  menu::TextBox* box_ = nullptr;           // +0x50
};

LeaderDialog::LeaderDialog() {
  box_ = new menu::TitledBox;
  box_->set_rect_centered(0x80, 0x5C, 1);
  box_->set_group(8);
  box_->pen_at(10, 5);
  box_->add_message(0);
  // 0x80080298: the front four's names (group 0 message character + 0x18).
  for (int i = 0; i < 4; ++i) {
    const int character = party_slot_active(i);
    if (character == 0) continue;
    box_->pen_at(0x10, static_cast<s16>(0x1C + i * 0xE));
    box_->set_group(0);
    box_->add_message(static_cast<s16>(character + 0x18));
  }
  choice_ = party_config()[0x41];
  cursor->add_grid(&choice_, 0, 1, 4, 0, 0x20, 0, 0xE, box_);
}

void LeaderDialog::on_confirm(int n) {
  if (party_slot_active(choice_) == 0) return;
  party_config()[0x41] = static_cast<u8>(choice_);
  finish(0);
  Widget::on_confirm(-n);
}

// ==== Formation: the preview (TextBox, 0x9c bytes, vtable 0x80080590; 0x8007f2e4) ========
// The front four's sprites on a floor grid in perspective (its own 0x400-entry ordering
// table spliced into the box's entry), the controlled member marked.
s32 g_marker_delay = 0;   // 0x80080af4
s32 g_marker_frame = 0;   // 0x80080af8

class FormationPreview final : public menu::TextBox {
 public:
  FormationPreview();
  void on_close() override;                   // [4] 0x8007f418
  void draw_background() override;            // [6] 0x8007f4ac
  void set_formation(int f) { formation_ = f; }   // 0x8007f4a4

 private:
  menu::Sprite* sprites_[4] = {};   // +0x84
  s32 formation_ = 0;               // +0x94
  RenderContext preview_;           // +0x98
};

FormationPreview::FormationPreview() {
  psyq::SetGeomOffset(0xA0, 0x82);
  psyq::SetGeomScreen(0x100);
  const psyq::SVECTOR angle{combat_s16(0x80080ADC), combat_s16(0x80080ADE), combat_s16(0x80080AE0), 0};
  const psyq::VECTOR trans{combat_s32(0x80080AE4), combat_s32(0x80080AE8), combat_s32(0x80080AEC), 0};
  const psyq::VECTOR scale{0x1000, 0x1000, 0x1000, 0};
  psyq::MATRIX m;
  psyq::RotMatrix(&angle, &m);
  psyq::TransMatrix(&m, &trans);
  psyq::ScaleMatrix(&m, &scale);
  psyq::SetRotMatrix(&m);
  psyq::SetTransMatrix(&m);
  const s16 region[4] = {0x1C1, 1, 0x1FF, 0xC0};
  for (auto*& s : sprites_) {
    s = new menu::Sprite;
    menu::sprite_init(s);
    s->cache = menu::sprite_cache_acquire(region);
  }
  preview_.arena = ctx->arena;
  render_ctx_create_ot(&preview_, 0x400);
}

void FormationPreview::on_close() {
  for (auto*& s : sprites_) {
    if (s) {
      menu::sprite_destroy(s);
      delete s;
      s = nullptr;
    }
  }
  screen_destroy_ot(preview_.ot);
  TextBox::on_close();
}

void FormationPreview::draw_background() {
  psx::Gte& g = psyq::gte();
  preview_.ot_head = screen_clear_ot(preview_.ot);
  auto load = [&](int i, s16 vx, s16 vy) {
    g.write_data(psx::Gte::VXY0 + i * 2, static_cast<u32>(static_cast<u16>(vx)) | static_cast<u32>(static_cast<u16>(vy)) << 16);
    g.write_data(psx::Gte::VZ0 + i * 2, 0);
  };
  auto link = [&](int entry, void* prim) { psyq::addPrim(preview_.ot_head + entry, prim); };
  for (int i = 0; i < 4; ++i) {
    const u32 at = 0x800808F4 + static_cast<u32>((i * 2 + formation_ * 8) * 4);
    const s16 vx = static_cast<s16>(combat_s32(at));
    const s16 vy = static_cast<s16>(-combat_s32(at + 4));
    load(0, vx, vy), load(1, vx, vy), load(2, vx, vy);
    g.execute(psx::Gte::kRtpt);
    g.execute(psx::Gte::kNclip);
    const u32 sxy = g.read_data(psx::Gte::SXY0);
    g.execute(psx::Gte::kAvsz3);
    const s32 otz = static_cast<s32>(g.read_data(psx::Gte::OTZ));
    const s16 sx = static_cast<s16>(sxy & 0xFFFF), sy = static_cast<s16>(sxy >> 16);
    if (i == party_config()[0x41]) {
      // The controlled member's marker, three frames of eight vblanks.
      auto* q = menu::prim_alloc<psyq::POLY_FT4>(&preview_);
      if (q) {
        psyq::setlen(q, 9);
        const u32 c = colour | 0x2C000000u;
        std::memcpy(&q->r0, &c, 4);
        const u8 v0 = static_cast<u8>(g_marker_frame << 4), v1 = static_cast<u8>((g_marker_frame << 4) + 0x10);
        q->x0 = static_cast<s16>(sx + 2), q->y0 = static_cast<s16>(sy - 0x24), q->u0 = 0x40, q->v0 = v0, q->clut = 0x7813;
        q->x1 = static_cast<s16>(sx + 0x12), q->y1 = static_cast<s16>(sy - 0x24), q->u1 = 0x50, q->v1 = v0, q->tpage = 5;
        q->x2 = static_cast<s16>(sx + 2), q->y2 = static_cast<s16>(sy - 0x14), q->u2 = 0x40, q->v2 = v1;
        q->x3 = static_cast<s16>(sx + 0x12), q->y3 = static_cast<s16>(sy - 0x14), q->u3 = 0x50, q->v3 = v1;
        if (static_cast<u32>(0x6F4 - otz) < 0x400) link(0x6F4 - otz, q);
      }
      if (++g_marker_delay >= 8) {
        g_marker_delay = 0;
        if (++g_marker_frame >= 3) g_marker_frame = 0;
      }
    }
    const int character = party_slot_active(i);
    if (character != 0) {
      const s16 pga[4] = {sx, sy, static_cast<s16>(character - 1), 9};
      menu::sprite_set(sprites_[i], pga, static_cast<s16>(static_cast<u16>(0x1000 - otz)), colour, 0);
      menu::sprite_sync_shared(sprites_[i]);
      if (static_cast<u32>(0x6F4 - otz) < 0x400) menu::sprite_draw(sprites_[i], &preview_, static_cast<s16>(0x6F4 - otz));
    }
  }
  // The floor: eight white lines and a blue quad (table 0x80080a94).
  for (int i = 0; i < 9; ++i) {
    const u32 at = 0x80080A94 + static_cast<u32>(i * 8);
    const s16 x0 = combat_s16(at), y0 = combat_s16(at + 2), x1 = combat_s16(at + 4), y1 = combat_s16(at + 6);
    if (i == 8) {
      auto* q = menu::prim_alloc<psyq::POLY_F4>(&preview_);
      if (!q) continue;
      psyq::setlen(q, 5);
      const u32 c = 0x28802010u;
      std::memcpy(&q->r0, &c, 4);
      load(0, x0, y0), load(1, x1, y0), load(2, x1, y1);
      g.execute(psx::Gte::kRtpt);
      const u32 a = g.read_data(psx::Gte::SXY0), b = g.read_data(psx::Gte::SXY1);
      load(0, x0, y1), load(1, x1, y1), load(2, x1, y1);
      g.execute(psx::Gte::kRtpt);
      const u32 d = g.read_data(psx::Gte::SXY0), e = g.read_data(psx::Gte::SXY1);
      q->x0 = static_cast<s16>(a & 0xFFFF), q->y0 = static_cast<s16>(a >> 16);
      q->x1 = static_cast<s16>(b & 0xFFFF), q->y1 = static_cast<s16>(b >> 16);
      q->x2 = static_cast<s16>(d & 0xFFFF), q->y2 = static_cast<s16>(d >> 16);
      q->x3 = static_cast<s16>(e & 0xFFFF), q->y3 = static_cast<s16>(e >> 16);
      link(1, q);
    } else {
      auto* l = menu::prim_alloc<psyq::LINE_F2>(&preview_);
      if (!l) continue;
      psyq::setlen(l, 3);
      const u32 c = 0x40FFFFFFu;
      std::memcpy(&l->r0, &c, 4);
      load(0, x0, y0), load(1, x1, y1), load(2, x1, y1);
      g.execute(psx::Gte::kRtpt);
      const u32 a = g.read_data(psx::Gte::SXY0), b = g.read_data(psx::Gte::SXY1);
      l->x0 = static_cast<s16>(a & 0xFFFF), l->y0 = static_cast<s16>(a >> 16);
      l->x1 = static_cast<s16>(b & 0xFFFF), l->y1 = static_cast<s16>(b >> 16);
      link(2, l);
    }
  }
  // Into the box's entry.
  psyq::P_TAG* own = preview_.ot_head;
  psyq::P_TAG* host = ctx->ot_head + slot;
  own[0x3FF].next = host->next;
  host->next = own;
}

// ==== Formation (Panel, 0xa8 bytes, vtable 0x800804c0; 0x8007fc1c) =======================
class FormationPage final : public menu::Panel {
 public:
  FormationPage();
  void on_cursor(int n) override;          // [12] 0x8007feec
  void on_confirm(int n) override;         // [13] 0x8007fe4c
  void on_button2(int n) override;         // [15] 0x8007fe88

 private:
  void refresh_list();                     // 0x8007ffa0
  void show_description();                 // 0x8007ff34
  void place_markers();                    // 0x8008002c
  s16 choice_ = 0;                         // +0x34
  menu::TextBox* text_ = nullptr;          // +0x98
  menu::TextBox* list_ = nullptr;          // +0x9c
  FormationPreview* preview_ = nullptr;    // +0xa0
  s16 current_ = 0;                        // +0xa4: config +0x40
};

FormationPage::FormationPage() {
  menu::MenuGlobals& g = menu::globals();
  menu::strings_set(g.data.data());
  {
    std::vector<u8> tim = unpack(menu::strings_block(3));
    const u32 keep = menu::strings_word(2);
    if (keep < g.data.size()) g.data.resize(keep);
    if (tim.size() >= 0x220) {
      menu::upload_image_ref(psyq::RECT{0x140, 0, 0x40, 0x40}, tim.data() + 0x220);
      menu::upload_image_ref(psyq::RECT{0x130, 0x1E0, 0x10, 0x10}, tim.data() + 0x14);
      menu::upload_free_later(std::move(tim));
    }
  }
  menu::TextRenderer::set_default_font(menu::strings_block(1), menu::strings_block(0));
  create_help();
  list_ = new menu::TextBox;
  list_->set_rect(0, 0, 0x7C, 200, 0);
  text_ = new menu::TextBox;
  text_->set_rect(0x7C, 0, 0xBC, 0x48, 0);
  preview_ = new FormationPreview;
  preview_->set_rect(0x9C, 0x58, 0x9C, 0x70, 0);
  preview_->set_flags(0x1000);
  current_ = party_config()[0x40];
  choice_ = current_;
  cursor->add_grid(&choice_, 0, 1, 0xD, 0, 0xC, 0, 0xE, list_);
  preview_->set_formation(choice_);
  help_flags = 2;
  refresh_list();
}

void FormationPage::refresh_list() {
  menu::TextBox* b = list_;
  b->clear();
  b->pen_reset();
  b->set_highlight_next(&choice_);
  b->add_list_highlight(&current_, 0xD);
  b->set_group(9);
  for (int i = 0; i < 0xD; ++i) b->add_message(static_cast<s16>(i));
}

void FormationPage::show_description() {
  menu::TextBox* b = text_;
  b->clear();
  b->pen_reset();
  b->pen_move(-8, 0);
  b->set_group(10);
  b->add_message_lines(choice_, 6);
}

void FormationPage::place_markers() {
  // (moves the preview box's pen over the four places; nothing is added)
  preview_->clear();
}

void FormationPage::on_cursor(int) {
  place_markers();
  refresh_list();
  show_description();
  preview_->set_formation(choice_);
}

void FormationPage::on_confirm(int n) {
  current_ = choice_;
  party_config()[0x40] = static_cast<u8>(choice_);
  Widget::on_confirm(-n);
}

void FormationPage::on_button2(int n) {
  open_child(1, new LeaderDialog);
  Widget::on_button2(-n);
}

template <typename Page>
void simple_page(menu::MenuPageLoader* p) {
  switch (p->substate) {
    case 1:
      p->publish_id();
      p->push(new Page);
      break;
    case 2:
    case 3:
      p->pop();
      break;
    default:
      break;
  }
}

}  // namespace

void menu_page08_update(menu::MenuPageLoader* p) { simple_page<TacticsPage>(p); }
void menu_page09_update(menu::MenuPageLoader* p) {
  // Archipelago (port feature): with a bench seed, the page with the bench [partybuild agent].
  if (ap::bench_enabled()) {
    simple_page<BenchOrderPage>(p);
  } else {
    simple_page<PartyOrderPage>(p);
  }
}
void menu_page0a_update(menu::MenuPageLoader* p) { simple_page<FormationPage>(p); }

}  // namespace ss::game::field
