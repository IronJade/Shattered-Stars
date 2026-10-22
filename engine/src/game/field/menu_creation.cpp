#include "game/field/menu_creation.h"

#include <cstring>
#include <vector>

#include "disc/slz.h"
#include "game/boot/rng.h"
#include "game/boot/sound.h"
#include "game/common/menu_core.h"
#include "game/common/menu_gfx.h"
#include "game/common/menu_scene.h"
#include "game/common/menu_viewer.h"
#include "game/common/menu_widget.h"
#include "game/common/party.h"
#include "game/field/callways.h"
#include "game/field/field.h"
#include "game/field/menu_item_list.h"
#include "game/field/mods.h"
#include "game/field/post_battle.h"
#include "game/field/script_vm.h"
#include "runtime/libgpu.h"
#include "runtime/runtime.h"

namespace ss::game::field {
namespace {

// ---- Page data ------------------------------------------------------------------------------

// What differs between the three code overlays that carry the creation page.
struct Overlay {
  int entry;            // the code overlay (at 0x8007e000)
  u32 talent_chances;   // u8[13][10]: by character and talent (0 = none: no talents)
  u32 talent_masks;     // s16[]: by specialty (0 = none)
  u32 lv_label;         // the member box's "SPECIAL / LV" label
  u32 lv_format;        // "%3d"
};
constexpr Overlay kOverlay0bc0{0xBC0, 0x80083B0A, 0x80083B8A, 0x80082A98, 0x80082AA8};
constexpr Overlay kOverlay0bae{0xBAE, 0x80081DC6, 0x80081E46, 0x80081788, 0x80081798};

const u8* data_at(int entry, u32 address) { return menu::page_table(entry, address); }
u8 data_byte(int entry, u32 address) {
  const u8* p = data_at(entry, address);
  return p ? *p : 0;
}
s16 data_s16(int entry, u32 address) {
  const u8* p = data_at(entry, address);
  if (!p) return 0;
  s16 v;
  std::memcpy(&v, p, 2);
  return v;
}
// A 0-terminated s16 list in the page data.
std::vector<s16> data_list(int entry, u32 address) {
  std::vector<s16> out;
  for (u32 a = address;; a += 2) {
    const u8* p = data_at(entry, a);
    if (!p) break;
    s16 v;
    std::memcpy(&v, p, 2);
    if (v == 0) break;
    out.push_back(v);
  }
  out.push_back(0);
  return out;
}
const char* data_text(int entry, u32 address) {
  const u8* p = data_at(entry, address);
  return p ? reinterpret_cast<const char*>(p) : "";
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

std::vector<u8> unpack(const u8* packed) {
  const size_t size = read_u32(packed + 4) + 0x10;
  auto out = disc::slz::unpack_block(std::span<const u8>(packed, size), 0, 0, nullptr);
  return out ? std::move(*out) : std::vector<u8>{};
}

bool item_data_resident() { return !menu::globals().item_data.empty(); }   // 0x80074068

// 0x8003363c: 'count' (1 when 0) of 'item' into the inventory, marked new.
void inventory_give(int item, int count) {
  if (item == 0) return;
  inventory_add(globals().inventory, item, count != 0 ? count : 1, 1);
}

// The item pack's blocks 3 / 4 / 5: the rare items (0x338 + n, block 3 holding what each
// is a rare kind of) and pairs of common (block 5) and rare (block 4) items.
const s16* pack_list(int block) { return reinterpret_cast<const s16*>(menu::item_strings(block)); }

// 0xbc0 0x8007e274: the rare kind of 'item' (or the item itself).
int rare_kind(int item) {
  if (item == 0) return 0;
  const s16* rare_of = pack_list(3);
  const s16* rare = pack_list(4);
  const s16* common = pack_list(5);
  if (!rare_of || !rare || !common) return item;
  for (int i = 0; rare_of[i] != 0; ++i) {
    if (rare_of[i] == item) return i + 0x338;
  }
  for (int i = 0; common[i] != 0; ++i) {
    if (common[i] == item) return rare[i];
  }
  return item;
}

// 0xbc0 0x8007e1c8: the common kind of a rare item (or the item itself).
int common_kind(int item) {
  const s16* rare_of = pack_list(3);
  const s16* rare = pack_list(4);
  const s16* common = pack_list(5);
  if (!rare_of || !rare || !common) return item;
  if (static_cast<u32>(item - 0x338) < 200) return rare_of[item - 0x338];
  for (int i = 0;; ++i) {
    if (rare[i] == 0) return item;
    if (rare[i] == item) return common[i];
  }
}

// ---- The creation animation (Dialog, 0x5c bytes, vtable 0x800838fc; 0xbc0 0x80080a18) ----
// It moves the viewer to the middle of the screen, has it load the four items and morph
// them, and closes when the morph is over.
class CreationAnim final : public menu::Dialog {
 public:
  explicit CreationAnim(menu::CreationViewer* viewer);
  ~CreationAnim() override;                       // [1] 0x80080a6c
  void frame() override;                          // [9] 0x80080ba0
  void on_confirm(int) override { sound = 0; }    // [13] 0x80080c64
  void on_cancel(int) override { sound = 0; }     // [14] 0x80080c6c
  void start(int a, int b, int c, int d);         // 0x80080acc

 private:
  menu::CreationViewer* viewer_;
  s32 step_ = 0;                                  // +0x54
  s32 started_ = 0;                               // +0x58
};

CreationAnim::CreationAnim(menu::CreationViewer* viewer) : viewer_(viewer) {
  if (viewer_) viewer_->place(0x9C, 0x70, 0xC);
  step_ = 0;
}

CreationAnim::~CreationAnim() {
  if (viewer_) viewer_->place(0x118, 0x4A, 0x18);
}

void CreationAnim::start(int a, int b, int c, int d) {
  if (!viewer_) return;
  viewer_->set_item(a);
  viewer_->set_item(b);
  viewer_->set_item(c);
  viewer_->set_item(d);
  viewer_->set_item(0);
  started_ = 1;
  viewer_->set_mode(1);
  viewer_->set_items(a, b, c, d);
}

void CreationAnim::frame() {
  if (step_ == 1) {
    if (viewer_ == nullptr || viewer_->state() == 4) {
      if (viewer_) viewer_->finish();
      cancel();
      ++step_;
    }
  } else if (step_ == 0) {
    if (viewer_ == nullptr || viewer_->state() == 2) {
      if (viewer_) viewer_->build_morphs();
      ++step_;
    }
  }
  Dialog::frame();
}

// ---- Message boxes (in a ConfirmDialog, vtable 0x80083854) ------------------------------

menu::MessageBox* new_message(s16 group) {
  auto* box = new menu::MessageBox;
  box->set_group(group);
  return box;
}

// 0x80080c74: what was made (group 10 with the item's name): message 2 for specialty 5,
// 3 for 14, else 0.
void show_made(int specialty, int item) {
  menu::MessageBox* box = new_message(10);
  menu::TextRenderer::set_expansion(0, menu::item_strings(1), menu::item_strings(0), 3, item);
  menu::message_box_show(box, specialty == 5 ? 2 : specialty == 0xE ? 3 : 0);
}

// 0x80080d34: a failure: nothing (group 0xb, message by specialty) or a failure item
// (group 0xc, the character's line).
void show_failed(int specialty, int character, int made) {
  menu::MessageBox* box = new menu::MessageBox;
  int index;
  if (made == 0) {
    box->set_group(0xB);
    index = specialty == 5 ? 1 : specialty == 0xE ? 2 : 0;
  } else {
    box->set_group(0xC);
    index = character - 1;
  }
  menu::message_box_show(box, index);
}

// 0x80080de8: the rare kind came out (group 10 message 1).
void show_rare(int made) {
  auto* box = new menu::MessageBox;
  if (made != 0) {
    box->set_group(10);
    menu::message_box_show(box, 1);
  }
}

// 0x80080e44: a woken talent (group 10 message 4: the name and the talent, group 7).
void show_talent(int character, int talent) {
  menu::MessageBox* box = new_message(10);
  menu::TextRenderer::set_expansion(0, menu::strings_block(1), menu::strings_block(0), 0, character + 0x18);
  menu::TextRenderer::set_expansion(1, menu::strings_block(1), menu::strings_block(0), 7, talent - 1);
  menu::message_box_show(box, 4);
}

// ---- The creation page (Panel, 0x108 bytes; 0xbc0 0x8007e8c0, vtable 0x800839b4) --------

menu::CreationViewer* g_viewer = nullptr;   // 0x80084190 (0xbc0)

class CreationPage : public menu::Panel {
 public:
  explicit CreationPage(const Overlay& overlay);
  ~CreationPage() override;                                            // [1] 0x8007eb54
  void on_close() override;                                            // [4] 0x8007ec0c
  void draw_item(menu::TextBox* box, int slot, int index) override;   // [8] 0x8008032c
  void frame() override;                                               // [9] 0x8007f230
  void on_child_done(int id, int, int) override {                     // [10] 0x80080300
    if (id == 1) ++state_;
  }
  void on_cursor(int n) override;                                      // [12] 0x8007f124
  void on_confirm(int n) override;                                     // [13] 0x8007fea8
  void on_cancel(int n) override;                                      // [14] 0x80080268
  void on_member_changed() override {                                  // [21] 0x8007f084
    draw_member();
    state_ = 0;
  }
  void draw_help() override;                                           // [22] 0x8007f0c8
  void draw_member() override;                                         // [23] 0x8007ef14
  void build_members() override;                                       // [25] 0x8007edfc

 protected:
  // The loop's steps, by state (even states start a step, odd ones wait for input or a
  // dialog).
  virtual void step_pick1();               // [26] 0x8007f7fc (state 0)
  virtual void step_pick2();               // [27] 0x8007f944 (2)
  virtual void step_prompt();              // [28] 0x8007fa8c (4)
  virtual void step_create();              // [29] 0x8007fb64 (6)
  virtual void step_result();              // [30] 0x8007fc1c (8)
  virtual void step_failure();             // [31] 0x8007fd2c (10)
  virtual void step_talent();              // [32] 0x8007fde0 (12)
  virtual void step_again() { state_ = 0; }   // [33] 0x8007fea0 (14)
  virtual bool pick1();                    // [34] 0x8007ffac
  virtual bool pick2();                    // [35] 0x80080028
  virtual bool decide();                   // [36] 0x800800a4
  virtual void build_list1() { has_list1_ = 0; }   // [37] 0x80080480
  virtual void build_list2() { has_list2_ = 0; }   // [38] 0x80080488
  virtual void compute() {}                // [39] 0x80080490
  virtual void draw_title_text(menu::TextBox* box);    // [40] 0x80080658
  virtual void draw_recipe_text(menu::TextBox* box);   // [41] 0x80080820
  virtual void show_item(int item);        // [42] 0x8007f178
  virtual void produce();                  // [43] 0x80080498

  void finish_item_data();                 // the item data's background load (as the camp page)
  void draw_title();                       // 0x800805ec
  void draw_recipe();                      // 0x800807b0
  void open_list();                        // the list box and its cursor (states 0 / 2)
  bool owns(int item) const { return inventory_count(item) != 0; }   // 0x80080594
  bool chance(int pct) const { return rng_range(100) < static_cast<u32>(pct); }   // 0x80080540
  int random(int n) const { return n != 0 ? static_cast<int>(rng_range(static_cast<u32>(n))) : 0; }   // 0x80080514
  int wake_talent();                       // 0x8007e000
  bool has_talents() const;                // 0x8007e17c

  int entry() const { return overlay_.entry; }
  u8 byte(u32 a) const { return data_byte(overlay_.entry, a); }
  s16 half(u32 a) const { return data_s16(overlay_.entry, a); }
  std::vector<s16> list(u32 a) const { return data_list(overlay_.entry, a); }
  static int list_count(const std::vector<s16>& l) { return static_cast<int>(l.size()) - 1; }   // 0x80080570

  const Overlay& overlay_;
  s16 choice_ = 0;                          // +0x34
  s16 super_ = 0;                           // +0x98: 1 a super specialty
  s16 id_ = 0;                              // +0x9a: the (super) specialty
  s16 tool_ = 0;                            // +0x9c: an item used up by every try
  u32 talents_ = 0;                         // +0xa0: the talents the specialty wants
  s16 bonus_ = 0;                           // +0xa4: chance bonus
  s16 character_ = 0;                       // +0xa6
  s16 level_ = 0;                           // +0xa8
  s16 index1_ = 0, item1_ = 0;              // +0xaa / +0xac: the first material
  s16 index2_ = 0, item2_ = 0;              // +0xae / +0xb0: the second
  s16 success_ = 0;                         // +0xb2
  s16 fail_item_ = 0;                       // +0xb4
  s16 good_item_ = 0;                       // +0xb6
  s16 made_ = 0;                            // +0xb8
  ItemList* list_ = nullptr;                // +0xbc
  menu::TextBox* recipe_ = nullptr;         // +0xc0
  menu::TextBox* list_box_ = nullptr;       // +0xc4
  menu::TextBox* title_ = nullptr;          // +0xc8
  s16 state_ = 0;                           // +0xcc
  s16 shown_ = 0;                           // +0xce: the state the boxes show
  u16 flags_ = 0;                           // +0xd0: 1 no materials label, 2 material 1 is a skill
  s16 has_list1_ = 0, has_list2_ = 0;       // +0xd2 / +0xd4
  s16 rare_chance_ = 1;                     // +0xd8: a result may come out as its rare kind
  s16 rare_ = 0;                            // +0xda
  s16 scroll1_ = 0, choice1_ = 0;           // +0xdc / +0xde
  s16 scroll2_ = 0, choice2_ = 0;           // +0xe0 / +0xe2
  s16 sfx_ = 0;                             // +0xe4: played while creating
  s16 sfx_clip_ = 0;                        // +0xe6
  std::vector<u8> saved_vram_;              // +0xe8: (0x240, 0x100) 0xc0 x 0x80
  menu::BgLoadClient client_;               // +0xec
  s32 item_sizes_[4] = {};                  // +0xf4
  std::vector<u8> item_buffer_;             // +0x104
};

constexpr psyq::RECT kSavedArea{0x240, 0x100, 0xC0, 0x80};

CreationPage::CreationPage(const Overlay& overlay) : overlay_(overlay) {
  menu::MenuGlobals& g = menu::globals();
  menu::strings_set(g.data.data());
  menu::TextRenderer::set_default_font(menu::strings_block(1), menu::strings_block(0));
  if (!item_data_resident()) {
    s32 total = 0;
    for (int i = 0; i < 4; ++i) {
      item_sizes_[i] = menu::menu_loader()->entry_size(3000 + i);
      total += item_sizes_[i];
    }
    item_buffer_.assign(static_cast<size_t>(total), 0);
    menu::bgload_queue_bytes(&client_, 3000, item_buffer_.data(), total);
  }
  create_help();
  super_ = 0;
  id_ = g.session->variant;
  level_ = static_cast<s16>(super_ == 0 ? char_specialty_level(id_) : party_super_specialty_level(id_));
  create_member_box(0);
  draw_member();
  create_info_box();
  title_ = new menu::TextBox;
  title_->set_rect(0, 0x58, 0x138, 0x18, 0);
  recipe_ = new menu::TextBox;
  recipe_->set_rect(0, 0x70, 0x9C, 0x58, 0);
  list_box_ = new menu::TextBox;
  list_box_->set_rect(0x9C, 0x70, 0x9C, 0x58, 0);
  shown_ = item_data_resident() ? -1 : 0;
  flags_ = 0;
  state_ = 0;
  tool_ = 0;
  scroll1_ = choice1_ = scroll2_ = choice2_ = 0;
  sfx_ = sfx_clip_ = 0;
  rare_chance_ = 1;
  talents_ = static_cast<u16>(data_s16(overlay_.entry, overlay_.talent_masks + id_ * 2));
  saved_vram_.assign(0xC000, 0);
  psyq::StoreImage(&kSavedArea, saved_vram_.data());
  list_ = nullptr;
}

CreationPage::~CreationPage() {
  psyq::LoadImage(&kSavedArea, saved_vram_.data());
  psyq::DrawSync(0);
  delete list_;
  if (g_viewer && viewer == g_viewer) g_viewer = nullptr;
}

void CreationPage::finish_item_data() {
  menu::MenuGlobals& g = menu::globals();
  const u8* p = item_buffer_.data();
  g.item_data = unpack(p);
  p += item_sizes_[0] + item_sizes_[1];   // entry 3001: native code
  g.session->buffer = unpack(p);
  mods_apply_items(g.session->buffer);  // world edits (mods.h)
  g.item_table = g.session->buffer.data();
  p += item_sizes_[2];
  menu::entry_cache_load_0bbb(p);
  g.session->cached_0bbb = 1;
  item_buffer_.clear();
  if (g.item_data.empty()) return;
  const u8* pack = g.item_data.data();
  std::vector<u8> tim = unpack(pack + read_u32(pack + 7 * 4));
  const u32 keep = read_u32(pack + 6 * 4);
  if (tim.size() >= 0x220) {
    menu::upload_image_ref(psyq::RECT{0x180, 0x180, 0x40, 0x40}, tim.data() + 0x220);
    menu::upload_image_ref(psyq::RECT{0x120, 0x1E0, 0x10, 0x10}, tim.data() + 0x14);
    menu::upload_free_later(std::move(tim));
  }
  if (keep < g.item_data.size()) g.item_data.resize(keep);
}

void CreationPage::on_close() {
  if (!item_data_resident()) {
    if (!menu::bgload_owner_done(&client_)) {
      menu::bgload_cancel_owner(&client_);
      item_buffer_.clear();
    } else {
      finish_item_data();
    }
  }
  Widget::on_close();
}

void CreationPage::build_members() {
  menu::MenuSession* session = menu::globals().session;
  if (session->field_query == 0) {
    // Everyone who can act and knows the specialty; the best of them is chosen.
    member_count = 0;
    int best = 0;
    for (int i = 0; i < 8; ++i) {
      party_select(i);
      if (party_selected_alive() == 0) continue;
      const int level = char_specialty_level(id_);
      if (level <= 0) continue;
      if (best <= level) {
        member = member_count;
        best = level;
      }
      members[member_count++] = static_cast<s16>(i);
    }
  } else {
    member_count = 0;
    member = 0;
    members[member_count++] = static_cast<s16>(party_get_selected());
  }
  party_select(members[member]);
}

void CreationPage::draw_member() {
  Panel::draw_member();
  if (!member_box) return;
  menu::StrBuf sb;
  sb.put(data_text(overlay_.entry, overlay_.lv_label));    // SPECIAL / LV
  sb.put(data_text(overlay_.entry, overlay_.lv_format));   // %3d
  sb.put_int(level_);
  member_box->add_string(sb.str());
}

void CreationPage::draw_help() {
  help->set_group(5);
  help->add_message(static_cast<s16>(menu::globals().session->variant - 1));
}

void CreationPage::on_cursor(int) {
  if (list_ != nullptr && choice_ < list_->count()) show_item(list_->at(choice_).item);
}

void CreationPage::show_item(int item) {
  if (state_ < 6 && state_ > 3) {
    set_viewer_item(0);
    menu::TextBox* b = info;
    b->clear();
    b->pen_reset();
    b->set_font_group(menu::strings_block(1), menu::strings_block(0), 6);
    b->add_message_lines(id_ - 1, 6);
  } else {
    set_viewer_item(item);
  }
}

void CreationPage::draw_item(menu::TextBox* box, int slot, int index) {
  menu::TextRenderer* r = box->text;
  r->begin_line(box->scroll->line(slot));
  const ItemList::Entry& e = list_->at(index);
  r->set_pen_x(0);   // 0x800803a4
  if (e.count == 0 || e.item == 0) {
    r->draw_current(r->message(0));
  } else {
    r->draw_current(r->message(e.item));
    r->set_pen_x(0x6A);
    r->draw_pack_message(0xF);
    r->set_pen_x(0x6E);
    r->draw_two_digits(e.count);
  }
  r->upload_line();
}

void CreationPage::draw_title() {
  menu::TextBox* b = title_;
  b->clear();
  b->pen_reset();
  b->pen_move(0, -2);
  draw_title_text(b);
}

void CreationPage::draw_title_text(menu::TextBox* box) {
  s16 group = -1;
  switch (id_) {
    case 1: group = 0xD; break;
    case 4: group = 0xE; break;
    case 5: group = 0xF; break;
    case 6: group = 0x10; break;
    case 7: group = 0x11; break;
    case 10: group = 0x12; break;
    case 11: group = 0x13; break;
    case 13: group = 0x14; break;
    case 14: group = 0x15; break;
    case 16: group = 0x16; break;
    case 17: group = 0x17; break;
    default: break;
  }
  if (group >= 0) box->set_group(group);
  switch (state_) {
    case 1: box->add_message(0); break;
    case 3: box->add_message(1); break;
    case 5:
      box->set_group(9);
      box->add_message(2);
      break;
    default: break;
  }
}

void CreationPage::draw_recipe() {
  menu::TextBox* b = recipe_;
  b->clear();
  b->pen_reset();
  if (state_ < 6) draw_recipe_text(b);
}

void CreationPage::draw_recipe_text(menu::TextBox* box) {
  box->set_font_group(menu::strings_block(1), menu::strings_block(0), 8);
  menu::TextRenderer* r = box->text;
  if ((flags_ & 1) == 0) {
    r->set_colour(2);
    box->add_message(0);   // the materials
    r->set_colour(1);
    if (item1_ != 0) {
      if ((flags_ & 2) == 0) {
        box->set_font_group(menu::item_strings(1), menu::item_strings(0), 3);
        box->add_message(item1_);
      } else {
        box->set_font_group(menu::strings_block(1), menu::strings_block(0), 3);
        box->add_message(static_cast<s16>(item1_ - 1));
      }
    }
    if (item2_ != 0) {
      box->set_font_group(menu::item_strings(1), menu::item_strings(0), 3);
      box->add_message(item2_);
    }
  }
  if (tool_ != 0) {
    box->set_font_group(menu::strings_block(1), menu::strings_block(0), 8);
    box->pen_at(0x10, 0x32);
    r->set_colour(2);
    box->add_message(1);   // used up
    r->set_colour(1);
    box->add_item_count(tool_);
  }
}

void CreationPage::frame() {
  if (!item_data_resident() && menu::bgload_owner_done(&client_)) {
    finish_item_data();
    shown_ = -1;
  }
  if (viewer == nullptr && item_data_resident()) {
    g_viewer = new menu::CreationViewer;
    attach_viewer(g_viewer, 0x118, 0x4A, 0x18);
  }
  if (state_ != shown_) {
    shown_ = state_;
    if (state_ == 0) {
      character_ = static_cast<s16>(party_selected_char());
      level_ = static_cast<s16>(super_ == 0 ? char_specialty_level(id_) : party_super_specialty_level(id_));
      index1_ = item1_ = index2_ = item2_ = 0;
    }
    draw_title();
    draw_recipe();
    draw_member();
    bonus_ = static_cast<s16>((callways() ? static_cast<s8>(callways()->mod_13) : 0) + menu::globals().session->creation_bonus);
    switch (state_) {
      case 0:
        index1_ = item1_ = 0;
        if (cursor == nullptr) cursor = new menu::Cursor;
        cursor->clear_groups();
        list_box_->clear();
        list_box_->pen_reset();
        delete list_;
        list_ = new ItemList;
        has_list1_ = 0;
        has_list2_ = 0;
        step_pick1();
        break;
      case 2:
        index2_ = item2_ = 0;
        cursor->clear_groups();
        list_box_->clear();
        list_box_->pen_reset();
        delete list_;
        list_ = new ItemList;
        step_pick2();
        break;
      case 4:
        cursor->clear_groups();
        list_box_->clear();
        list_box_->pen_reset();
        delete list_;
        list_ = nullptr;
        step_prompt();
        break;
      case 6:
        if (sfx_ != 0) sfx_clip_ = static_cast<s16>(sound_play_sfx(globals().sound, sfx_, 0, 0x7F40));
        show_item(0);
        cursor->on_close();
        cursor = nullptr;
        list_box_->clear();
        step_create();
        break;
      case 8:
        if (sfx_ != 0 && sfx_clip_ > 0) sound_release_clip(globals().sound, sfx_clip_);
        step_result();
        break;
      case 10: step_failure(); break;
      case 12: step_talent(); break;
      case 14: step_again(); break;
      default: break;
    }
  }
  Panel::frame();
}

void CreationPage::open_list() {
  if (list_->count() == 0) list_->add(0);
  const s16 rows = static_cast<s16>(list_box_->set_list(static_cast<s16>(list_->count()), 5, 0x80, 0xE, 0x10, 8));
  choice_ = 0;
  cursor->add_grid(&choice_, 2, 1, rows, 0, 0xC, 0, 0xE, list_box_);
}

void CreationPage::step_pick1() {
  help_flags |= 1;
  list_box_->set_font_group(menu::item_strings(1), menu::item_strings(0), 3);
  has_list1_ = 1;
  build_list1();
  if (has_list1_ == 0) {
    ++state_;
  } else {
    open_list();
  }
  ++state_;
}

void CreationPage::step_pick2() {
  help_flags &= 0xFFFE;
  list_box_->set_font_group(menu::item_strings(1), menu::item_strings(0), 3);
  has_list2_ = 1;
  build_list2();
  if (has_list2_ == 0) {
    ++state_;
  } else {
    open_list();
  }
  ++state_;
}

void CreationPage::step_prompt() {
  list_box_->set_font_group(menu::strings_block(1), menu::strings_block(0), 9);
  list_box_->add_message(0);
  list_box_->add_message(1);
  show_item(0);
  choice_ = 0;
  cursor->add_grid(&choice_, 0, 1, 2, 0, 0xC, 0, 0xE, list_box_);
  ++state_;
}

void CreationPage::step_create() {
  help_flags &= 0xFFFE;
  auto* d = new CreationAnim(g_viewer);
  open_child(1, d);
  const int first = item1_ != 0 ? item1_ : tool_;
  if (success_ != 0) {
    d->start(first, item2_, fail_item_, good_item_);
  } else {
    d->start(first, item2_, good_item_, fail_item_);
  }
  ++state_;
}

void CreationPage::step_result() {
  produce();
  if (made_ == 0) {
    ++state_;
  } else {
    set_viewer_item(made_);
    open_child(1, new menu::ConfirmDialog);
    if (rare_ == 0) {
      if (success_ != 0) sound_play_sfx(globals().sound, 0xC, 0, 0x7F40);
      show_made(id_, made_);
    } else {
      show_rare(made_);
    }
  }
  ++state_;
}

void CreationPage::step_failure() {
  if (success_ == 0 && rare_ == 0) {
    open_child(1, new menu::ConfirmDialog);
    show_failed(id_, character_, made_);
  } else {
    ++state_;
  }
  ++state_;
}

void CreationPage::step_talent() {
  const int talent = wake_talent();
  if (talent == 0) {
    ++state_;
  } else {
    open_child(1, new menu::ConfirmDialog);
    show_talent(character_, talent);
  }
  ++state_;
}

bool CreationPage::pick1() {
  scroll1_ = list_box_->scroll ? list_box_->scroll->v[0] : 0;
  choice1_ = choice_;
  index1_ = list_->at(choice_).index;
  item1_ = list_->at(choice_).item;
  if (item1_ != 0) ++state_;
  return item1_ != 0;
}

bool CreationPage::pick2() {
  scroll2_ = list_box_->scroll ? list_box_->scroll->v[0] : 0;
  choice2_ = choice_;
  index2_ = list_->at(choice_).index;
  item2_ = list_->at(choice_).item;
  if (item2_ != 0) ++state_;
  return item2_ != 0;
}

bool CreationPage::decide() {
  if (tool_ != 0 && inventory_count(tool_) == 0) return false;
  success_ = fail_item_ = good_item_ = 0;
  if (!has_talents()) {
    const s16 saved = level_;
    level_ = 0;
    compute();
    level_ = saved;
  } else {
    compute();
  }
  u8* config = party_config();
  wr32(config + 0x20, rd32s(config + 0x20) + 1);   // tries
  if (success_ != 0) wr32(config + 0x28, rd32s(config + 0x28) + 1);   // successes
  rare_ = 0;
  if (rare_chance_ != 0) {
    bool first = false, second = false;
    if (chance(0x1E)) {
      const int r = rare_kind(fail_item_);
      if (fail_item_ != r) {
        fail_item_ = static_cast<s16>(r);
        first = true;
      }
    }
    if (chance(0x1E)) {
      const int r = rare_kind(good_item_);
      if (good_item_ != r) {
        good_item_ = static_cast<s16>(r);
        second = true;
      }
    }
    if (success_ != 0) first = second;
    if (first) rare_ = 1;
  }
  ++state_;
  return true;
}

void CreationPage::produce() {
  made_ = success_ == 0 ? fail_item_ : good_item_;
  inventory_take_one(item1_);
  inventory_take_one(item2_);
  inventory_take_one(tool_);
  inventory_give(made_, 0);
}

void CreationPage::on_confirm(int n) {
  bool used;
  switch (state_) {
    case 1: used = pick1(); break;
    case 3: used = pick2(); break;
    case 5:
      if (choice_ == 1) {
        on_cancel(n);
        return;
      }
      used = decide();
      break;
    default:
      sound = 0;
      return;
  }
  if (used) Widget::on_confirm(-n);
}

void CreationPage::on_cancel(int n) {
  switch (state_) {
    case 1:
      break;
    case 3:
      state_ = 0;
      n = -n;
      break;
    case 5:
      if (has_list2_ != 0) {
        state_ = 2;
      } else if (has_list1_ != 0) {
        state_ = 0;
      } else {
        break;
      }
      n = -n;
      break;
    default:
      sound = 0;
      return;
  }
  Widget::on_cancel(n);
}

int CreationPage::wake_talent() {
  // 0x8007e000 (as the Specialty page's): a talent the specialty wants may wake.
  if (overlay_.talent_chances == 0) return 0;
  u8* record = party_selected_record();
  u32 talents = static_cast<u32>(rd32s(record + 0x20));
  u32 bit = 1;
  for (int i = 0; i < 10; ++i, bit <<= 1) {
    if ((talents_ & bit) == 0 || (talents & bit) != 0) continue;
    u32 c = byte(overlay_.talent_chances + character_ * 10 + i);
    if (c != 0) {
      const int mod = callways() ? static_cast<s8>(callways()->mod_14) : 0;
      c += static_cast<u32>((mod << 8) / 100);
    }
    if (rng_range(0x100) < c) {
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

bool CreationPage::has_talents() const {
  if (callways() && callways()->mod_12 != 0) return true;
  if (menu::globals().session->creation_aid != 0) return true;
  const u32 have = static_cast<u32>(rd32s(party_selected_record() + 0x20));
  return (have & talents_) == talents_;
}

// ==== Overlay 0xbc0: specialties 1, 5, 6, 7, 10, 13, 14, 16, 17 ============================

// Specialty 1 (vtable 0x800836f4; 0x80080f10): materials 1 or 0x2b; the level picks a row
// of five weights, the weight a group of results.
class Creation1 final : public CreationPage {
 public:
  Creation1() : CreationPage(kOverlay0bc0) { sfx_ = 0x3B; }
 protected:
  void build_list1() override {            // 0x80080f5c
    list_->add_owned(1);
    list_->add_owned(0x2B);
  }
  void compute() override;                 // 0x80080f98
  int pick_from_groups(int group, u32 lists);   // 0x80081158
};

int Creation1::pick_from_groups(int group, u32 lists) {
  // Skip 'group' 0-terminated groups, then one entry of the next at random.
  int at = 0;
  for (int g = 0; g < group; ++g) at += list_count(list(lists + at * 2)) + 1;   // 0x80081204
  const std::vector<s16> l = list(lists + at * 2);
  const int i = random(list_count(l));
  return half(lists + (at + i) * 2);
}

void Creation1::compute() {
  if (owns(0x1BB)) bonus_ = static_cast<s16>(bonus_ + 0x14);
  u32 weights = 0, groups = 0;
  if (item1_ == 1) {
    weights = 0x80083BBC, groups = 0x80083BD8;
    fail_item_ = 0x12;
  } else if (item1_ == 0x2B) {
    weights = 0x80083C14, groups = 0x80083C30;
    fail_item_ = 0x1E;
  }
  const int r = random(100);
  const int row = byte(0x80083BB0 + level_) * 5;
  int sum = 0, pick = 0;
  for (int i = 0; i < 5; ++i) {
    sum += byte(weights + row + i);
    if (r < sum) {
      pick = i;
      break;
    }
  }
  if (level_ != 0) success_ = chance(level_ * 8 + bonus_ + 10) ? 1 : 0;
  if (success_ != 0) {
    int item = pick_from_groups(pick, groups);
    if (static_cast<u32>(item - 6) < 0xC) {
      // A character's favourite: the one of a random member who can act.
      const int character = party_slot_active(party_random_alive_slot());
      for (int i = 6; i < 0x12; ++i) {
        if (static_cast<u32>(character) == byte(0x80083C52 + i)) item = i;
      }
    }
    good_item_ = static_cast<s16>(item);
  }
}

// Specialty 5 (vtable 0x80083584; 0x80081228): a rare item (0x338-0x3ff, or one of the item
// pack's block 4) back into its common kind, every one of it; tool 0x36.
class Creation5 final : public CreationPage {
 public:
  Creation5() : CreationPage(kOverlay0bc0) {
    tool_ = 0x36;
    rare_chance_ = 0;
    sfx_ = 0x3B;
  }
 protected:
  void build_list1() override {            // 0x80081280
    list_->add_owned_range(0x338, 0x3FF);
    list_->add_owned_list(pack_list(4));
  }
  void compute() override {                // 0x800812c8
    if (owns(0x1BA)) bonus_ = static_cast<s16>(bonus_ + 0x14);
    if (level_ != 0) success_ = chance(level_ * 5 + bonus_ + 0x1E) ? 1 : 0;
    fail_item_ = item1_;
    good_item_ = static_cast<s16>(common_kind(item1_));
  }
  void produce() override {                // 0x8008134c
    inventory_take_one(tool_);
    const int n = inventory_count(item1_);
    if (success_ != 0) {
      for (int i = 0; i < n; ++i) {
        inventory_take_one(item1_);
        inventory_give(good_item_, 0);
      }
      made_ = good_item_;
      return;
    }
    made_ = fail_item_;
  }
};

// Specialty 6 (vtable 0x80083414; 0x800813fc): the materials of table 0x80083d3c (and 0x121).
class Creation6 final : public CreationPage {
 public:
  Creation6() : CreationPage(kOverlay0bc0) { sfx_ = 0x5E; }
 protected:
  void build_list1() override {            // 0x80081448
    const std::vector<s16> l = list(0x80083D3C);
    list_->add_owned_list(l.data());
    if (owns(0x121)) list_->add(0x121);
  }
  bool usable(int r) const {               // 0x800815f4
    if (item1_ == 0x121) return true;
    return (static_cast<u16>(half(0x80083C7C + (index1_ * 8 + r) * 2)) & static_cast<u16>(half(0x80083C62 + character_ * 2))) != 0;
  }
  void compute() override;                 // 0x800814a0
};

void Creation6::compute() {
  if (owns(0x1CC)) bonus_ = static_cast<s16>(bonus_ + 0x14);
  int r;
  do {
    r = random(8);
  } while (!usable(r));
  const int div = byte(0x80083D58 + r);
  int p = div != 0 ? level_ * 10 / div : 0;
  if (p > 0x4B) p = 0x4B;
  if (level_ != 0) success_ = chance(p + bonus_) ? 1 : 0;
  const std::vector<s16> fails = list(0x80083E20);
  const int i = random(list_count(fails));
  const s16 good = half(0x80083D60 + (index1_ * 8 + r) * 2);
  fail_item_ = half(0x80083E20 + i * 2);
  good_item_ = good;
  if (item1_ == 0x121) {
    good_item_ = 0x13D;
    success_ = 1;
  }
}

// Specialty 7 (vtable 0x800832a4; 0x80081648): a skill of level 5 or more (table 0x80083e28)
// into its book; tool 0xa4, failure 0xa3.
class Creation7 final : public CreationPage {
 public:
  Creation7() : CreationPage(kOverlay0bc0) {
    tool_ = 0xA4;
    flags_ = 2;
    sfx_ = 0x3B;
  }
  void draw_item(menu::TextBox* box, int slot, int index) override;   // [8] 0x80081904
 protected:
  void build_list1() override;             // 0x800816a4
  void compute() override;                 // 0x80081740
  void show_item(int item) override;       // 0x800819ec
};

void Creation7::build_list1() {
  for (s16 skill : list(0x80083E28)) {
    if (skill == 0) break;
    if (char_skill_level(skill) > 4) list_->add(skill);
  }
  list_box_->set_font_group(menu::strings_block(1), menu::strings_block(0), 3);
}

void Creation7::draw_item(menu::TextBox* box, int slot, int index) {
  menu::TextRenderer* r = box->text;
  r->begin_line(box->scroll->line(slot));
  const int skill = list_->at(index).item;
  r->set_pen_x(0);   // 0x8008197c
  if (skill != 0) r->draw_current(r->message(skill - 1));
  r->upload_line();
}

void Creation7::show_item(int item) {
  set_viewer_item(0);
  menu::TextBox* b = info;
  b->clear();
  b->pen_reset();
  b->set_group(4);
  if (item != 0) b->add_message_lines(item - 1, 6);
}

void Creation7::compute() {
  if (owns(0x1CE)) bonus_ = static_cast<s16>(bonus_ + 10);
  if (char_get_equip(3) == 0xA5) bonus_ = static_cast<s16>(bonus_ + 10);
  int index = 0;
  for (s16 skill : list(0x80083E28)) {
    if (skill == item1_) break;
    ++index;
  }
  if (level_ != 0) {
    const int div = byte(0x80083E8C + index);
    success_ = chance((div != 0 ? (level_ << 3) / div : 0) + bonus_) ? 1 : 0;
  }
  const s16 book = half(0x80083E54 + index * 2);
  fail_item_ = 0xA3;
  good_item_ = book;
  if (character_ == 8 && chance(10)) {
    const std::vector<s16> rare = list(0x80083E80);
    int n = list_count(rare);
    int owned = 0;
    for (int i = 0; i < n; ++i) {
      if (inventory_count(rare[static_cast<size_t>(i)]) != 0) owned = i + 1;
    }
    if (owned < n) n = owned + 1;
    good_item_ = half(0x80083E80 + random(n) * 2);
  }
  item1_ = 0;
}

// Specialty 10 (vtable 0x80083134; 0x80081a58): two of items 0xdc-0xe1.
class Creation10 final : public CreationPage {
 public:
  Creation10() : CreationPage(kOverlay0bc0) { sfx_ = 0x3B; }
 protected:
  void build_list1() override { list_->add_owned_range(0xDC, 0xE1); }   // 0x80081aa4
  void build_list2() override {           // 0x80081acc: without the first material
    inventory_take_one(item1_);
    list_->add_owned_range(0xDC, 0xE1);
    inventory_give_one(item1_);
  }
  void compute() override {               // 0x80081b18
    if (owns(0x1CF)) bonus_ = static_cast<s16>(bonus_ + 0x14);
    const int r = random(4);
    if (level_ != 0) success_ = chance(byte(0x80083EA0 + level_ * 4 + r) + bonus_) ? 1 : 0;
    good_item_ = half(0x80083EE8 + (byte(0x80083ECC + index1_ * 6 + index2_) * 4 + r) * 2);
  }
};

// Specialty 13 (vtable 0x80082fc4; 0x80081bf4): the higher the level, the more of items
// 0x1a4-0x1b4 can be made (no material used); tool 0x1a3, failure 0x1ac.
class Creation13 final : public CreationPage {
 public:
  Creation13() : CreationPage(kOverlay0bc0) {
    tool_ = 0x1A3;
    sfx_ = 0x3B;
  }
  void draw_item(menu::TextBox* box, int slot, int index) override;   // [8] 0x80081ec0
 protected:
  void build_list1() override;             // 0x80081c48
  void compute() override {                // 0x80081de0
    if (owns(0x1CA)) bonus_ = static_cast<s16>(bonus_ + 0x14);
    const int div = byte(0x80083F98 + index1_);
    if (level_ != 0 && div != 0) success_ = chance((level_ << 4) / div + bonus_) ? 1 : 0;
    fail_item_ = 0x1AC;
    good_item_ = item1_;
  }
  void produce() override {                // 0x80081ea0
    item1_ = 0;
    CreationPage::produce();
  }
};

void Creation13::build_list1() {
  ItemList& l = *list_;
  l.add(0x1A5);
  l.add(0x1A4);
  if (level_ <= 1) return;
  l.add(0x1AE);
  l.add(0x1AF);
  if (level_ <= 2) return;
  l.add(0x1AD);
  l.add(0x1B1);
  if (level_ <= 3) return;
  l.add(0x1B3);
  if (level_ <= 4) return;
  l.add(0x1B0);
  if (level_ <= 5) return;
  l.add(0x1AA);
  if (owns(0x144)) l.add(0x1AB);
  if (level_ <= 6) return;
  l.add(0x1A7);
  if (!owns(0x144)) return;
  l.add(0x1B4);
  if (level_ <= 7) return;
  l.add(0x1A6);
  l.add(0x1B2);
  l.add(0x1A8);
  l.add(0x1A9);
}

void Creation13::draw_item(menu::TextBox* box, int slot, int index) {
  menu::TextRenderer* r = box->text;
  r->begin_line(box->scroll->line(slot));
  const ItemList::Entry& e = list_->at(index);
  r->set_pen_x(0);   // 0x80081f38
  r->draw_current(r->message(e.item));
  r->set_pen_x(0x6A);
  r->draw_pack_message(0xF);
  r->set_pen_x(0x6E);
  r->draw_two_digits(e.count);
  r->upload_line();
}

// Specialty 14 (vtable 0x80082e54; 0x80081fec): no material; each try costs 4 MP; what comes
// out depends on the place (field_flags +0x30 / +0x31).
class Creation14 final : public CreationPage {
 public:
  Creation14() : CreationPage(kOverlay0bc0) {
    flags_ = 1;
    sfx_ = 0x3B;
  }
  void on_confirm(int n) override {        // [13] 0x80082314
    if (state_ == 5 && choice_ != 1) {
      u8* slot = party_selected_slot();
      if (rd16s(slot + 0x24) < 4) return;
      wr16(slot + 0x24, static_cast<s16>(rd16s(slot + 0x24) - 4));
    }
    CreationPage::on_confirm(n);
  }
 protected:
  void step_prompt() override {            // 0x80082040
    help_flags |= 1;
    CreationPage::step_prompt();
  }
  void compute() override;                 // 0x8008206c
  void draw_recipe_text(menu::TextBox* box) override;   // 0x8008237c
};

void Creation14::compute() {
  if (owns(0x1CD)) bonus_ = static_cast<s16>(bonus_ + 0x14);
  const int place = globals().field_flags[0x31];
  const u32 a = static_cast<u32>(place - 1);
  if (a > 10) return;
  const int kind = globals().field_flags[0x30];
  const u32 b = static_cast<u32>(kind - 1);
  if (b > 8) return;
  const int div = byte(0x80083FFF + place);
  const int r = random(100);
  int sum = 0, pick = 0;
  for (int k = 0; k < 8; ++k) {
    sum += byte(0x80083FA8 + a + k * 0xB);
    if (r < sum) {
      pick = k;
      break;
    }
  }
  int scale = -1;
  if (pick < 6) {
    good_item_ = static_cast<s16>(pick + 0x117);
  } else if (b <= 3) {
    const int i = random(list_count(list(0x8008400C)));
    scale = byte(0x8008403C + i * 4 + b);
    good_item_ = half(0x8008400C + i * 2);
  } else if (b <= 8) {
    const int i = random(list_count(list(0x80084098)));
    scale = byte((b == 8 ? 0x800840C3u : 0x800840C4u) + i * 4 + b);
    good_item_ = half(0x80084098 + i * 2);
  }
  if (level_ != 0) {
    int p;
    if (scale == -1) {
      p = (div != 0 ? (level_ * 5 + 0x28) / div : 0) + bonus_;
    } else {
      if (scale == 0) return;
      p = level_ * scale / 10 + bonus_;
    }
    success_ = chance(p) ? 1 : 0;
  }
}

void Creation14::draw_recipe_text(menu::TextBox* box) {
  box->set_font_group(menu::strings_block(1), menu::strings_block(0), 0x15);
  menu::TextRenderer* r = box->text;
  r->set_colour(2);
  box->add_message(1);
  r->set_colour(1);
  r->set_pen(static_cast<s16>(box->pen_x + 0x20), static_cast<s16>(box->pen_y - 0xE), 0xC);
  r->add_number(rd16s(party_selected_slot() + 0x24), 4);
}

// Specialty 16 (vtable 0x80082ce4; 0x80082430): item 0x1b7 or 0x1bd and any item with a
// rank (item field 0x2b) is copied; nothing but the tool (0x1b8) is used up; failure 0x1b6.
class Creation16 final : public CreationPage {
 public:
  Creation16() : CreationPage(kOverlay0bc0) {
    tool_ = 0x1B8;
    sfx_ = 0x3B;
  }
 protected:
  void build_list1() override {            // 0x80082484
    list_->add_owned(0x1B7);
    list_->add_owned(0x1BD);
  }
  void build_list2() override {            // 0x800824c0
    const u8* inv = globals().inventory;
    for (int i = 0; i < 0x400; ++i) {
      const int item = read_u16(inv + i * 2) & 0x3FF;
      if (item != 0 && menu::item_data_get(item, 0x2B) != 0) list_->add_owned(item);
    }
  }
  void compute() override {                // 0x8008254c
    if (item1_ == 0x1BD) bonus_ = static_cast<s16>(bonus_ + 0x14);
    const s16 item = item2_;
    if (level_ != 0) {
      const int r = random(0x100);
      if (r < menu::item_data_get(item, 0x2B) + level_ * 3 + (bonus_ << 8) / 100) success_ = 1;
    }
    fail_item_ = 0x1B6;
    good_item_ = item;
  }
  void produce() override {                // 0x80082620
    item1_ = 0;
    item2_ = 0;
    CreationPage::produce();
  }
};

// Specialty 17 (vtable 0x80082b74; 0x80082644): item 0x1c8 into one of table 0x80084124
// (characters 9 and 6 now and then reach rarer tables).
class Creation17 final : public CreationPage {
 public:
  Creation17() : CreationPage(kOverlay0bc0) { sfx_ = 0x5E; }
 protected:
  void build_list1() override { list_->add_owned(0x1C8); }   // 0x80082690
  void compute() override;                 // 0x800826b4
};

void Creation17::compute() {
  u32 items = 0x80084124, chances = 0x8008414C;
  bool rare = false;
  s16 character = character_;
  if (character == 9) {
    rare = chance(10);
    if (rare) items = 0x80084160, chances = 0x80084170;
    character = character_;
  }
  if (character == 6 && chance(10)) {
    items = 0x80084178, chances = 0x80084188;
    rare = true;
  }
  const std::vector<s16> l = list(items);
  int n = list_count(l);
  if (rare) {
    int owned = 0;
    for (int i = 0; i < n; ++i) {
      if (inventory_count(l[static_cast<size_t>(i)]) != 0) owned = i + 1;
    }
    if (owned < n) n = owned + 1;
  }
  const int i = random(n);
  const int div = byte(chances + i);
  if (level_ != 0) success_ = chance((div != 0 ? (level_ << 4) / div : 0) + bonus_) ? 1 : 0;
  good_item_ = l[static_cast<size_t>(i)];
}

// 0xbc0 0x8007e790: the class by the specialty in session +0x1c.
menu::Widget* create_0bc0() {
  switch (menu::globals().session->variant) {
    case 1: return new Creation1;
    case 5: return new Creation5;
    case 6: return new Creation6;
    case 7: return new Creation7;
    case 10: return new Creation10;
    case 13: return new Creation13;
    case 14: return new Creation14;
    case 16: return new Creation16;
    case 17: return new Creation17;
    default: return nullptr;
  }
}

}  // namespace

void menu_page0f_update(menu::MenuPageLoader* p) {
  switch (p->substate) {
    case 1: {
      p->publish_id();
      const s8 v = menu::globals().session->variant;
      menu::Widget* page = (v == 4 || v == 0xB) ? nullptr : create_0bc0();
      if (page == nullptr) {
        runtime::Runtime::get().log_once("menu:page0f_variant", "menu: page 0xf variant not ported");
        p->pop();
        break;
      }
      p->push(page);
      break;
    }
    case 2:
    case 3:
      p->pop();
      break;
    default:
      break;
  }
}

void menu_page10_update(menu::MenuPageLoader* p) {
  switch (p->substate) {
    case 1:
      runtime::Runtime::get().log_once("menu:page10", "menu: page 0x10 (super specialties) not ported");
      p->pop();
      break;
    case 2:
      p->pop();
      break;
    case 3:
      p->goto_page(0x14);
      break;
    default:
      break;
  }
}

}  // namespace ss::game::field
