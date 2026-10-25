#include "game/field/menu_creation.h"

#include <cstring>
#include <vector>

#include "disc/slz.h"
#include "game/ap/ap.h"
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
#include "game/field/menu_camp.h"
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
  bool super = false;   // 0xbc2: the super specialties (page 0x10)
};
constexpr Overlay kOverlay0bc0{0xBC0, 0x80083B0A, 0x80083B8A, 0x80082A98, 0x80082AA8};
constexpr Overlay kOverlay0bae{0xBAE, 0x80081DC6, 0x80081E46, 0x80081788, 0x80081798};
constexpr Overlay kOverlay0bc2{0xBC2, 0, 0, 0x80082B58, 0x80082B68, true};

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
u32 data_u32(int entry, u32 address) {
  const u8* p = data_at(entry, address);
  if (!p) return 0;
  u32 v;
  std::memcpy(&v, p, 4);
  return v;
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
  // Port (Archipelago v2, no_fail_crafting): compute() picked a result the original can never
  // make (Customize's result equal to its input, a zero-chance pick): it stays a failure.
  bool no_result_ = false;
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
  super_ = overlay_.super ? 1 : 0;
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
  talents_ = overlay_.talent_masks != 0 ? static_cast<u16>(data_s16(overlay_.entry, overlay_.talent_masks + id_ * 2)) : 0;
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
      if (overlay_.super) {
        // 0xbc2 0x80080a04: always message 0.
        menu::MessageBox* box = new_message(10);
        menu::TextRenderer::set_expansion(0, menu::item_strings(1), menu::item_strings(0), 3, made_);
        menu::message_box_show(box, 0);
      } else {
        show_made(id_, made_);
      }
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
  no_result_ = false;
  if (!has_talents()) {
    const s16 saved = level_;
    level_ = 0;
    compute();
    level_ = saved;
  } else {
    compute();
  }
  // Archipelago v2 (port feature, the seed's no_fail_crafting; docs/ap/seed_format.md
  // "Craftsanity"): an attempt that picked a result succeeds. Which result is the original's
  // pick (a member without the talents still picks at level 0); the rare swap below is unchanged.
  if (success_ == 0 && good_item_ != 0 && !no_result_ && ap::no_fail_crafting()) success_ = 1;
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
  // Archipelago v2 (port feature, craftsanity): what an item-making specialty made, success or
  // failure, as its common kind, is the seed's craft check for that item (the player keeps it).
  // Page 0xf: Art 1, Customize 4, Metalwork 6, Authoring 7, Compounding 10, Cooking 11,
  // Alchemy 13, Machinery 17; page 0x10: Master Chef 1, Publishing 5, Blacksmith 7, Reverse
  // Side 8. Not Identify 5, Survival 14 or Reproduction 16 (they hand back or copy items).
  const bool makes = super_ != 0 ? (id_ == 1 || id_ == 5 || id_ == 7 || id_ == 8)
                                 : (id_ == 1 || id_ == 4 || id_ == 6 || id_ == 7 || id_ == 10 || id_ == 11 ||
                                    id_ == 13 || id_ == 17);
  if (made_ != 0 && makes) ap::on_craft(common_kind(made_), success_ != 0);
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
  // Archipelago v2 (port feature, no_fail_crafting): Art picks its result only on a success, so
  // the option makes the roll a success here (decide() covers the specialties that always pick).
  if (success_ == 0 && ap::no_fail_crafting()) success_ = 1;
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
    no_result_ = div == 0;  // (port: a zero chance at every level)
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
  no_result_ = scale == 0;  // (port: a pick this place never yields, at any level)
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

// ==== Overlay 0xbae: specialties 4 and 11 ===================================================

// Customize (4; vtable 0x800819b0; 0xbae 0x80080e4c): an item the character can customize
// (its list, 0x80082b00 by character) and one of the materials of 0x80081f74. The item's
// worth (0xbae 0x800810a0) plus the material's (0x80081f98) and a random part picks a tier
// of the character's results (0x80081e56); some pairs are recipes (0x80082ad0 by character).
class Customize final : public CreationPage {
 public:
  Customize() : CreationPage(kOverlay0bae) { sfx_ = 0x5E; }
 protected:
  void build_list1() override {            // 0x80080e98
    const std::vector<s16> l = list(data_u32(entry(), 0x80082B00 + character_ * 4));
    for (s16 item : l) {
      if (item == 0) break;
      if (!list_->contains(item)) list_->add_owned(item);
    }
  }
  void build_list2() override {            // 0x80080f20
    const std::vector<s16> l = list(0x80081F74);
    list_->add_owned_list(l.data());
  }
  int worth(int item) const;               // 0x800810a0
  int tier_item(int value) const;          // 0x80081118
  int recipe(int value) const;             // 0x800811c4: the result, -1 refused, 0 none
  void compute() override;                 // 0x80080f4c
};

int Customize::worth(int item) const {
  for (u32 i = 0; i < 12; ++i) {
    const std::vector<s16> l = list(data_u32(entry(), 0x80082B04 + i * 4));
    for (size_t k = 0; k + 1 < l.size(); ++k) {
      if (l[k] == item) return half(data_u32(entry(), 0x80082B34 + i * 4) + static_cast<u32>(k) * 2);
    }
  }
  return 0;
}

int Customize::tier_item(int value) const {
  static constexpr int kTop[9] = {4, 9, 0x13, 0x1D, 0x27, 0x31, 0x3B, 0x45, 0x4F};
  int tier = 10;
  if (value >= 0) {
    tier = 0;
    while (tier < 9 && value > kTop[tier]) ++tier;
  }
  return half(0x80081E56 + static_cast<u32>(character_ * 0xB + tier) * 2);
}

int Customize::recipe(int value) const {
  u32 a = data_u32(entry(), 0x80082AD0 + character_ * 4);
  for (;;) {
    const s16 threshold = half(a);
    if (threshold == 0) return 0;
    const s16 dish = half(a + 2);
    const s16 material = half(a + 4);
    a += 6;
    for (;; a += 2) {
      const s16 item = half(a);
      if (item == 0) {
        a += 2;
        break;
      }
      if (item == item1_ && material == item2_) return value <= threshold ? -1 : dish;
    }
  }
}

void Customize::compute() {
  if (owns(0x1CB)) bonus_ = static_cast<s16>(bonus_ + 0x14);
  int value = worth(item1_);
  const s16 extra = half(0x80081F98 + index2_ * 2);
  value += extra + random(level_ * 5 + 1) + bonus_;
  if (value > 100) value = 100;
  if (level_ != 0) success_ = chance(level_ + bonus_ + 0x3C) ? 1 : 0;
  fail_item_ = static_cast<s16>(tier_item(-1));
  const int r = recipe(value);
  if (r == -1) success_ = 0;
  if (r > 0) good_item_ = static_cast<s16>(r);
  if (good_item_ == 0) good_item_ = static_cast<s16>(tier_item(value));
  if (success_ != 0 && item1_ == good_item_) success_ = 0;
  no_result_ = item1_ == good_item_;  // (port: nothing would change)
}

// Cooking (11; vtable 0x80081840; 0xbae 0x80081250): an ingredient (items 0x117-0x11c,
// 0x30e-0x316, 0x336) into one of its dishes (0x80082b64 by its position; characters 1, 2,
// 6 and 8 have their own list for the third). In a query session (the cooking contest)
// the dish moves the gauge's PRESS value (field flags +0x38) and the page closes when the
// gauge's time is up.
class Cooking final : public CreationPage {
 public:
  Cooking() : CreationPage(kOverlay0bae) { sfx_ = 0x3B; }
  void frame() override {                  // [9] 0x8008166c
    CreationPage::frame();
    if ((state_ == 1 || state_ == 5) && menu::globals().session->field_query != 0 && field_gauge_seconds_left() == 0) {
      cancel();
      state_ = 0x10;
    }
  }
 protected:
  void build_list1() override {            // 0x8008129c
    list_->add_owned_range(0x117, 0x11C);
    list_->add_owned_range(0x30E, 0x316);
    list_->add_owned(0x336);
  }
  void compute() override;                 // 0x800812ec
  void step_result() override;             // 0x8008155c
};

void Cooking::compute() {
  if (owns(0x1FD)) bonus_ = static_cast<s16>(bonus_ + 0x14);
  int level = level_;
  if (menu::globals().session->field_query != 0) {
    level -= (rd32s(globals().field_flags + 0x38) - char_skill_level(0x1C) * 5) / 10;
    if (level < 0) level = 0;
  }
  u32 dishes = data_u32(entry(), 0x80082B64 + index1_ * 4);
  u32 fails = data_u32(entry(), 0x80082BA4 + index1_ * 4);
  u32 kinds = data_u32(entry(), 0x80082BE4 + index1_ * 4);
  if (index1_ == 2 && (character_ == 1 || character_ == 2 || character_ == 6 || character_ == 8)) {
    dishes = 0x800828E8, fails = 0x80082900, kinds = 0x80082904;
  }
  for (;;) {
    const int i = random(list_count(list(dishes)));
    const u8 f = byte(kinds + static_cast<u32>(i));
    if ((f & 0x10) != 0 && level < 4) continue;
    if ((f & 0x20) != 0 && level <= 6) continue;
    good_item_ = half(dishes + static_cast<u32>(i) * 2);
    if (level != 0) {
      const int div = f & 0xF;
      int p = div != 0 ? (level * 10 + 0x32) / div : 0;
      if (p > 0x5A) p = 0x5A;
      success_ = chance(p + bonus_) ? 1 : 0;
    }
    const int n = list_count(list(fails));
    if (n != 0) fail_item_ = half(fails + static_cast<u32>(random(n)) * 2);
    return;
  }
}

void Cooking::step_result() {
  if (menu::globals().session->field_query != 0) {
    int sign, lo, range;
    if (success_ == 0) {
      sign = 1;
      lo = half(0x80082A94 + index1_ * 2);
      range = half(0x80082AB4 + index1_ * 2) - lo;
    } else {
      sign = -1;
      lo = half(0x80082A54 + index1_ * 2);
      range = half(0x80082A74 + index1_ * 2) - lo;
    }
    const int r = static_cast<int>(rng_range(static_cast<u32>(range + 1)));
    u8* ff = globals().field_flags;
    s32 press = rd32s(ff + 0x38) + (lo + r) * sign;
    if (press < 0) press = 0;
    if (press > 100) press = 100;
    wr32(ff + 0x38, press);
  }
  CreationPage::step_result();
}

// 0xbae 0x8007e790
menu::Widget* create_0bae() {
  switch (menu::globals().session->variant) {
    case 4: return new Customize;
    case 0xB: return new Cooking;
    default: return nullptr;
  }
}

// ==== Overlay 0xbc2: the super specialties (page 0x10) =====================================

// The base (0xbc2 0x8007e6f8): only the selected member; no talents; the member box shows
// the party (0x8008100c); no failure or talent steps; the recipe box shows the super
// specialty's level (0x80080d98).
class SuperPage : public CreationPage {
 public:
  SuperPage() : CreationPage(kOverlay0bc2) {}
  void build_members() override {           // [25] 0x8007ec38
    member_count = 0;
    member = 0;
    members[member_count++] = static_cast<s16>(party_get_selected());
    party_select(members[member]);
  }
  void draw_member() override;              // [23] 0x8008100c
 protected:
  void step_failure() override { state_ = static_cast<s16>(state_ + 2); }   // [31] 0x80080c3c
  void step_talent() override { state_ = static_cast<s16>(state_ + 2); }    // [32] 0x80080c50
  void draw_title_text(menu::TextBox* box) override;    // [40] 0x80080c64
  void draw_recipe_text(menu::TextBox* box) override;   // [41] 0x80080d98
  void message(s16 group, int index) {      // a message dialog (vtable 0x80083bd0)
    auto* d = new menu::ConfirmDialog;
    open_child(1, d);
    menu::dialog_show_message(d, group, index);
  }
};

void SuperPage::draw_member() {
  menu::TextBox* b = member_box;
  if (!b) return;
  b->clear();
  const int selected = party_get_selected();
  for (int i = 0; i < 8; ++i) {
    party_select(i);
    const int character = party_selected_char();
    if (character == 0) continue;
    b->pen_at(static_cast<s16>((i % 4) * 0x16), static_cast<s16>((i / 4) << 5));
    b->add_party_character(static_cast<s16>(character), 0x12, 0xC00);
  }
  party_select(selected);
}

void SuperPage::draw_title_text(menu::TextBox* box) {
  if (id_ >= 1 && id_ <= 8) box->set_group(static_cast<s16>(0xF + id_));
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

void SuperPage::draw_recipe_text(menu::TextBox* box) {
  box->set_font_group(menu::strings_block(1), menu::strings_block(0), 8);
  menu::TextRenderer* r = box->text;
  r->set_colour(2);
  box->add_message(3);   // the super specialty's level
  r->set_colour(1);
  r->set_pen(static_cast<s16>(box->pen_x + 0x6E), static_cast<s16>(box->pen_y - 0xE), 0xC);
  r->add_number(level_, 2);
  if ((flags_ & 1) == 0) {
    box->set_font_group(menu::strings_block(1), menu::strings_block(0), 8);
    r->set_colour(2);
    box->add_message(0);
    r->set_colour(1);
    if (item1_ != 0) {
      if ((flags_ & 4) == 0) {
        box->set_font_group(menu::item_strings(1), menu::item_strings(0), 3);
        box->add_message(item1_);
      } else {
        box->set_font_group(menu::strings_block(1), menu::strings_block(0), 0);
        box->add_message(static_cast<s16>(item1_ + 0x18));   // a character's name
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
    box->add_message(1);
    r->set_colour(1);
    box->add_item_count(tool_);
  }
}

// Super specialty 1 (vtable 0x80083640; 0x800810e4): two ingredients (0x117-0x11c) into a
// dish (tables 0x80083c78 / 0x80083c9c / 0x80083cc8 / 0x80083cf4); 1 in 255 item 0x1a1.
class Super1 final : public SuperPage {
 public:
  Super1() { sfx_ = 0x3B; }
 protected:
  void build_list1() override { list_->add_owned_range(0x117, 0x11C); }   // 0x80081148
  void build_list2() override {            // 0x80081170
    inventory_take_one(item1_);
    list_->add_owned_range(0x117, 0x11C);
    inventory_give_one(item1_);
  }
  void compute() override {                // 0x800811bc
    if (owns(0x1FD)) bonus_ = static_cast<s16>(bonus_ + 0x14);
    const u32 pair = static_cast<u32>(index1_ * 6 + index2_);
    if (level_ != 0) {
      const int div = byte(0x80083CF4 + pair);
      int p = div != 0 ? (level_ * 10 + 0x32) / div : 0;
      if (p > 0x5A) p = 0x5A;
      success_ = chance(p + bonus_) ? 1 : 0;
    }
    const u32 i = static_cast<u32>(byte(0x80083C78 + pair) - 1) * 2;
    fail_item_ = half(0x80083CC8 + i);
    good_item_ = half(0x80083C9C + i);
    if (random(0xFF) == 0) good_item_ = 0x1A1;
  }
};

// Super specialty 2 (vtable 0x800834d0; 0x800812fc, 0x114 bytes): with four instruments and
// eight composed songs, item 0x35 plays song entry 0xaae through Callways (id 0x11).
class Super2 final : public SuperPage {
 public:
  Super2() {
    tool_ = 0x35;
    flags_ = 1;
    load_ = 0;
  }
  ~Super2() override { menu::bgload_cancel_owner(&song_client_); }
  void frame() override;                   // [9] 0x800815a8
 protected:
  void step_pick1() override;              // [26] 0x80081374
  bool decide() override;                  // [36] 0x80081414
  void step_create() override {            // [29] 0x80081524
    if (success_ == 0) {
      message(0x11, note_);
      state_ = 0xD;
    }
  }
 private:
  menu::BgLoadClient song_client_;         // +0x108
  s16 load_ = 0;                           // +0x110
  s16 note_ = 0;                           // +0x112
};

void Super2::step_pick1() {
  list_->add_owned_range(1, 2);
  list_box_->set_group(0xC);
  list_box_->add_message(0);
  choice_ = 0;
  cursor->add_grid(&choice_, 0, 1, 1, 0, 0xC, 0, 0xE, list_box_);
  state_ = 5;
}

bool Super2::decide() {
  note_ = 0;
  success_ = 0;
  if (tool_ != 0 && inventory_count(tool_) == 0) return false;
  int instruments = 0;
  for (int i = 0x2D; i < 0x35; ++i) {
    if (inventory_count(i) != 0) ++instruments;
  }
  if (instruments < 4) {
    note_ = 1;
  } else {
    int songs = 0;
    for (int i = 0; i < 0x10; ++i) {
      if (((globals().script_flags[(i + 0x2F) >> 3] >> ((i + 0x2F) & 7)) & 1) != 0) ++songs;
    }
    if (songs > 7) {
      success_ = 1;
      inventory_take_one(tool_);
      load_ = static_cast<s16>(menu::bgload_queue(&song_client_, 0xAAE, 0, nullptr, 0, 0));
      ++state_;
      return true;
    }
    note_ = 2;
  }
  ++state_;
  return true;
}

void Super2::frame() {
  CreationPage::frame();
  if (load_ != 0 && menu::bgload_owner_done(&song_client_)) {
    const menu::LoadRequest* req = menu::bgload_request(load_);
    if (req != nullptr && req->dest != nullptr && callways() != nullptr) {
      callways_play_song(callways(), std::span<const u8>(req->dest, static_cast<size_t>(req->sectors) << 11), 0x11, level_);
    }
    menu::bgload_cancel(load_);
    load_ = 0;
    state_ = 0xE;
  }
}

// Super specialty 3 (vtable 0x80083360; 0x80081634): script flag 0x25 on or off.
class Super3 final : public SuperPage {
 public:
  Super3() { flags_ = 1; }
  void on_confirm(int n) override {        // [13] 0x800817e8
    if (state_ == 5) {
      u8* f = globals().script_flags;
      f[0x25 >> 3] &= static_cast<u8>(~(1 << (0x25 & 7)));
      if (choice_ != 0) f[0x25 >> 3] |= static_cast<u8>(1 << (0x25 & 7));
      ++state_;
    }
    Widget::on_confirm(-n);
  }
 protected:
  void step_pick1() override {             // [26] 0x80081698
    list_->add_owned_range(1, 2);
    list_box_->set_group(0xD);
    list_box_->add_list_highlight(&choice_, 2);
    list_box_->add_message(0);
    list_box_->add_message(1);
    choice_ = ((globals().script_flags[0x25 >> 3] >> (0x25 & 7)) & 1) != 0 ? 1 : 0;
    cursor->add_grid(&choice_, 0, 1, 2, 0, 0xC, 0, 0xE, list_box_);
    state_ = 5;
  }
  void step_create() override {            // [29] 0x80081774
    message(0x12, 1);
    state_ = 0xD;
  }
  void draw_title_text(menu::TextBox* box) override {   // [40] 0x80081868
    if (state_ == 5) {
      box->set_group(0x12);
      box->add_message(0);
    }
  }
};

// Super specialty 4 (vtable 0x800831f0; 0x800818b0, 0x114 bytes): only on the world map
// and not with script flags 0xc / 0xe. A random member's voice (entry table 0x80083d16 by
// character) plays; success (level * 4 + bonus) sets session +0x30 and closes the menu.
class Super4 final : public SuperPage {
 public:
  Super4() { flags_ = 1; }
  ~Super4() override { menu::bgload_cancel_owner(&voice_client_); }
  void frame() override;                   // [9] 0x80081b58
 protected:
  static bool usable() {                   // 0x80081ab0
    const u8* f = globals().script_flags;
    if ((f[0xE >> 3] >> (0xE & 7)) & 1) return false;
    if ((f[0xC >> 3] >> (0xC & 7)) & 1) return false;
    return globals().field_flags[0x68] != 0;
  }
  void step_pick1() override {             // [26] 0x80081920
    list_->add_owned_range(1, 1);
    list_box_->set_group(0xE);
    menu::TextRenderer* r = list_box_->text_renderer();
    r->set_colour(usable() ? 1 : 4);
    list_box_->add_message(0);
    r->set_colour(1);
    choice_ = 0;
    cursor->add_grid(&choice_, 0, 1, 1, 0, 0xC, 0, 0xE, list_box_);
    state_ = 5;
  }
  bool decide() override;                  // [36] 0x800819fc
  void step_create() override {}           // [29] 0x80081b08
  void step_again() override {             // [33] 0x80081b10
    if (success_ == 0) {
      state_ = 0;
    } else {
      menu::globals().session->unknown30 = 1;
      finish(0);
    }
  }
 private:
  menu::BgLoadClient voice_client_;        // +0x108
  s16 load_ = 0;                           // +0x110: loading (> 0) or playing (< 0)
  s16 clip_ = 0;                           // +0x112
};

bool Super4::decide() {
  if (tool_ != 0 && inventory_count(tool_) == 0) return false;
  if (!usable()) return false;
  inventory_take_one(tool_);
  success_ = chance(level_ * 4 + bonus_) ? 1 : 0;
  const int character = party_slot_active(party_random_alive_slot());
  load_ = static_cast<s16>(menu::bgload_queue(&voice_client_, half(0x80083D16 + static_cast<u32>(character) * 2), 0, nullptr, 0, 0));
  ++state_;
  return true;
}

void Super4::frame() {
  CreationPage::frame();
  if (load_ == 0) return;
  if (load_ > 0) {
    if (menu::bgload_is_done(load_)) {
      const menu::LoadRequest* req = menu::bgload_request(load_);
      clip_ = 0;
      if (req != nullptr && req->dest != nullptr) {
        clip_ = static_cast<s16>(sound_open_clip(globals().sound, req->dest, 2, nullptr));
      }
      load_ = static_cast<s16>(-load_);
    }
    return;
  }
  if (clip_ < 1 || sound_effect_playing(globals().sound, clip_) == 0) {
    menu::bgload_cancel(-load_);
    load_ = 0;
    message(0x13, success_ + 1);
    state_ = 0xD;
  }
}

// Super specialty 5 (vtable 0x80083080; 0x80081c8c): a party member into a book about
// them (tables 0x80083d68 / 0x80083d30 / 0x80083d4c); tool 0xa4, failure 0xa3.
class Super5 final : public SuperPage {
 public:
  Super5() {
    tool_ = 0xA4;
    flags_ = 4;
    sfx_ = 0x3B;
  }
  void draw_item(menu::TextBox* box, int slot, int index) override {   // [8] 0x80081ef8
    menu::TextRenderer* r = box->text;
    r->begin_line(box->scroll->line(slot));
    const int character = list_->at(index).item;
    r->set_pen_x(0);   // 0x80081f70
    if (character != 0) r->draw_current(r->message(character + 0x18));
    r->upload_line();
  }
 protected:
  void build_list1() override {            // 0x80081d00
    for (int i = 0; i < 8; ++i) {
      const int character = party_slot_active(i);
      if (character != 0) list_->add(character);
    }
    list_box_->set_font_group(menu::strings_block(1), menu::strings_block(0), 0);
  }
  void compute() override;                 // 0x80081d88
  void show_item(int item) override {      // [42] 0x80081fe0
    menu::TextBox* b = info;
    if (state_ < 6 && state_ > 3) {
      b->clear();
      b->pen_reset();
      b->set_font_group(menu::strings_block(1), menu::strings_block(0), 6);
      b->add_message_lines(id_ - 1, 6);
      return;
    }
    set_viewer_item(0);
    b->clear();
    b->pen_reset();
    b->set_group(0x14);
    if (item != 0) b->add_message_lines(item, 1);
  }
};

void Super5::compute() {
  if (owns(0x1CE)) bonus_ = static_cast<s16>(bonus_ + 0x14);
  int index = 0;
  for (int i = 0; i < 0xC; ++i) {
    if (byte(0x80083D68 + static_cast<u32>(i)) == item1_) {
      index = i;
      break;
    }
  }
  const bool second = chance(10);
  if (level_ != 0) success_ = chance((second ? (level_ * 0x10) / 7 : (level_ << 4) / 3) + bonus_) ? 1 : 0;
  fail_item_ = 0xA3;
  good_item_ = half((second ? 0x80083D4Cu : 0x80083D30u) + static_cast<u32>(index) * 2);
  item1_ = 0;
}

// Super specialty 6 (vtable 0x80082f10; 0x800820c8): with item 0x36, script flag 0x2e0 or
// 0x2df on (once; the third choice clears both).
class Super6 final : public SuperPage {
 public:
  Super6() {
    flags_ = 1;
    tool_ = 0x36;
  }
  void on_confirm(int n) override;         // [13] 0x80082218
 protected:
  static bool flag(int n) { return ((globals().script_flags[n >> 3] >> (n & 7)) & 1) != 0; }
  static void set(int n, bool on) {
    u8& b = globals().script_flags[n >> 3];
    b = static_cast<u8>(on ? (b | (1 << (n & 7))) : (b & ~(1 << (n & 7))));
  }
  void step_pick1() override {             // [26] 0x80082134
    list_->add_owned_range(1, 2);
    list_box_->set_group(0xF);
    list_box_->add_message(0);
    list_box_->add_message(1);
    list_box_->add_message(2);
    // The original picks the current setting here and then starts at 0 anyway.
    choice_ = 0;
    cursor->add_grid(&choice_, 0, 1, 3, 0, 0xC, 0, 0xE, list_box_);
    state_ = 5;
  }
  void step_create() override {            // [29] 0x80082344
    message(0x15, note_);
    state_ = 0xD;
  }
  void draw_title_text(menu::TextBox* box) override {   // [40] 0x800823b8
    if (state_ == 5) {
      box->set_group(0x15);
      box->add_message(0);
    }
  }
 private:
  s32 note_ = 0;                           // +0x108
};

void Super6::on_confirm(int n) {
  if (state_ == 5) {
    if (choice_ == 2) {
      set(0x2DF, false);
      set(0x2E0, false);
      state_ = 0xE;
    } else if (flag(0x2DF)) {
      note_ = 4;
      state_ = 6;
    } else if (flag(0x2E0)) {
      note_ = 3;
      state_ = 6;
    } else {
      if (tool_ != 0 && inventory_count(tool_) == 0) return;
      inventory_take_one(tool_);
      set(0x2DF, false);
      set(0x2E0, false);
      if (choice_ == 0) set(0x2E0, true);
      if (choice_ == 1) set(0x2DF, true);
      state_ = 6;
      note_ = choice_ + 1;
    }
  }
  Widget::on_confirm(-n);
}

// Super specialty 7 (vtable 0x80082da0; 0x80082400): a material of 0x80083d74 into one of
// its results (0x80083e70 / 0x80083ea8, or 0x80083e8c / 0x80083ec4 with item 0x1cb);
// tool 0x1d6, failure 0x2a4.
class Super7 final : public SuperPage {
 public:
  Super7() {
    tool_ = 0x1D6;
    sfx_ = 0x5E;
  }
 protected:
  void build_list1() override {            // 0x8008246c
    const std::vector<s16> l = list(0x80083D74);
    list_->add_owned_list(l.data());
  }
  void compute() override {                // 0x80082498
    if (owns(0x1CB)) bonus_ = static_cast<s16>(bonus_ + 0x14);
    const bool better = owns(0x1CB);
    const u32 items = data_u32(entry(), (better ? 0x80083E8Cu : 0x80083E70u) + static_cast<u32>(index1_) * 4);
    const u32 divs = data_u32(entry(), (better ? 0x80083EC4u : 0x80083EA8u) + static_cast<u32>(index1_) * 4);
    const int i = random(list_count(list(items)));
    const int div = byte(divs + static_cast<u32>(i));
    int p = div != 0 ? (level_ * 8 + 0x28) / div : 0;
    if (p > 0x5A) p = 0x5A;
    if (level_ != 0) success_ = chance(p + bonus_) ? 1 : 0;
    fail_item_ = 0x2A4;
    good_item_ = half(items + static_cast<u32>(i) * 2);
  }
};

// Super specialty 8 (vtable 0x80082c30; 0x800825f0): one of 0x80083e4c; each try may lower
// a random mark of config +0x58 / +0xe8 (12 x 12); tool 0x1d9, failure 0x1da.
class Super8 final : public SuperPage {
 public:
  Super8() {
    tool_ = 0x1D9;
    flags_ = 1;
    sfx_ = 0x3B;
  }
 protected:
  void lower_mark() {                      // 0x80082730
    const int row = random(0xC);
    const int col = random(0xC);
    const int which = random(4);
    int off;
    if (which == 1) {
      off = row * 0xC + 0xE8;
    } else if (which == 0) {
      off = row * 0xC + 0x58;
    } else {
      return;
    }
    u8& m = party_config()[off + col];
    if (m != 0) --m;
  }
  void compute() override {                // 0x80082664
    lower_mark();
    const int i = random(list_count(list(0x80083E4C)));
    const int div = byte(0x80083E64 + static_cast<u32>(i));
    if (level_ != 0) success_ = chance((div != 0 ? (level_ << 3) / div : 0) + bonus_) ? 1 : 0;
    fail_item_ = 0x1DA;
    good_item_ = half(0x80083E4C + static_cast<u32>(i) * 2);
  }
};

// 0xbc2 0x8007e5e0: page 0x10's class by the super specialty in session +0x1c.
menu::Widget* create_0bc2() {
  switch (menu::globals().session->variant) {
    case 1: return new Super1;
    case 2: return new Super2;
    case 3: return new Super3;
    case 4: return new Super4;
    case 5: return new Super5;
    case 6: return new Super6;
    case 7: return new Super7;
    case 8: return new Super8;
    default: return nullptr;
  }
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
      menu::Widget* page = (v == 4 || v == 0xB) ? create_0bae() : create_0bc0();
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
    case 1: {
      p->publish_id();
      menu::Widget* page = create_0bc2();
      if (page == nullptr) {
        p->pop();
        break;
      }
      p->push(page);
      break;
    }
    case 2:
      p->pop();
      break;
    case 3:
      p->goto_page(0x14);   // super specialty 4 closes the menu
      break;
    default:
      break;
  }
}

}  // namespace ss::game::field
