#include "game/field/menu_items.h"

#include <cstring>
#include <vector>

#include "disc/slz.h"
#include "game/boot/pad.h"
#include "game/common/menu_core.h"
#include "game/common/menu_gfx.h"
#include "game/common/menu_scene.h"
#include "game/common/menu_viewer.h"
#include "game/common/menu_widget.h"
#include "game/common/party.h"
#include "game/field/field.h"
#include "game/field/item_effect.h"
#include "game/field/post_battle.h"
#include "game/field/script_vm.h"
#include "runtime/libgpu.h"

namespace ss::game::field {
namespace {

constexpr int kItemsCode = 0xBBE;   // page 3's code overlay (at 0x8007e000)

const char* items_text(u32 address) {
  const u8* p = menu::page_table(kItemsCode, address);
  return p ? reinterpret_cast<const char*>(p) : "";
}

bool script_flag(int n) { return ((globals().script_flags[n >> 3] >> (n & 7)) & 1) != 0; }   // 0x80055ecc

std::vector<u8> unpack(const u8* packed) {
  const size_t size = read_u32(packed + 4) + 0x10;
  auto out = disc::slz::unpack_block(std::span<const u8>(packed, size), 0, 0, nullptr);
  return out ? std::move(*out) : std::vector<u8>{};
}

u16 inv_entry(int index) { return read_u16(party_inventory() + index * 2); }

// The item at inventory 'index', or 0 (and the entry cleared) when its check byte is
// wrong (the pages' inlined inventory_verify).
int checked_item(int index) {
  u8* inv = party_inventory();
  if (!inventory_verify(inv, static_cast<u32>(index))) {
    std::memset(inv + index * 2, 0, 2);
    return 0;
  }
  return inv_entry(index) & 0x3FF;
}

// 0x8003c85c: the last inventory index holding something.
int inventory_last_used() {
  int last = 0;
  for (int i = 0; i < 0x400; ++i)
    if ((inv_entry(i) >> 10 & 0x1F) != 0) last = i;
  return last;
}

// 0x8003284c: session +0x1b = 1 unless some member can still act.
bool party_all_down(menu::MenuSession* s) {
  s->unknown1b = 1;
  const int selected = party_get_selected();
  for (int i = 0; i < 8; ++i) {
    party_select(i);
    if (party_selected_char() != 0 && (party_selected_status() & 7) == 0) s->unknown1b = 0;
  }
  party_select(selected);
  return s->unknown1b != 0;
}

// Arrow icons over a member: 1 worn, 2 even, 3 better, 4 worse (window art row 0x20).
menu::PolyFT4Raw verdict_arrow(int verdict) {
  menu::PolyFT4Raw q{};
  const u8 u0 = static_cast<u8>((verdict - 1) * 0x18), u1 = static_cast<u8>(u0 + 0x18);
  q.x0 = 4, q.y0 = 0x18, q.u0 = u0, q.v0 = 0x20, q.clut = 0x7850;
  q.x1 = 0x1C, q.y1 = 0x18, q.u1 = u1, q.v1 = 0x20, q.tpage = 0x16;
  q.x2 = 4, q.y2 = 0x28, q.u2 = u0, q.v2 = 0x30;
  q.x3 = 0x1C, q.y3 = 0x28, q.u3 = u1, q.v3 = 0x30;
  return q;
}

// ==== The picture box (TextBox, 0x90 bytes, vtable 0x80081c88; 0x8007e000) ===============
// The info box of the page: the item's picture (a 4-bit image the viewer's text pack
// carries, uploaded to (0x200, 0)) with its shadow, a rule under it and the description
// below; with the figures shown (mode 1) only a short rule on the right.
class PictureBox final : public menu::TextBox {
 public:
  PictureBox() { set_rect(0, 0, 0x138, 0x58, 0); }
  void draw_background() override;                       // [6] 0x8007e064
  void set_mode(int mode) { mode_ = mode; }              // 0x8007e5ac
  void set_size(int w, int h) { w_ = w, h_ = h; }        // 0x8007e5b4

 private:
  s32 w_ = 0;      // +0x84
  s32 h_ = 0;      // +0x88
  s32 mode_ = 0;   // +0x8c
};

void PictureBox::draw_background() {
  const RenderContext* c = ctx;
  auto* m = screen_alloc_prims<psyq::DR_MODE>(c->arena, 1);
  if (!m) return;
  const psyq::RECT tw{};
  psyq::SetDrawMode(m, 0, 1, 0x16, &tw);
  menu::prim_link(c, slot, m);
  auto quad = [&](u32 rgb, s16 ox, s16 oy, u16 tpage) {
    auto* q = screen_alloc_prims<psyq::POLY_FT4>(c->arena, 1);
    if (!q) return;
    psyq::setlen(q, 9);
    std::memcpy(&q->r0, &rgb, 4);
    const s16 x0 = static_cast<s16>(x + ox), y0 = static_cast<s16>(y + oy);
    q->x0 = x0, q->y0 = y0, q->u0 = 0, q->v0 = 0, q->clut = 0x7B90;
    q->x1 = static_cast<s16>(x0 + w_), q->y1 = y0, q->u1 = static_cast<u8>(w_), q->v1 = 0, q->tpage = tpage;
    q->x2 = x0, q->y2 = static_cast<s16>(y0 + h_), q->u2 = 0, q->v2 = static_cast<u8>(h_);
    q->x3 = static_cast<s16>(x0 + w_), q->y3 = static_cast<s16>(y0 + h_), q->u3 = static_cast<u8>(w_), q->v3 = static_cast<u8>(h_);
    menu::prim_link(c, slot, q);
  };
  if (mode_ == 0 && w_ != 0) {
    quad(colour | 0x2E000000u, 0xC, 8, 0x28);                                  // additive
    quad(menu::menu_colour_code(0x2E000000, colour, 0x202020), 0xE, 0xA, 0x48);  // the shadow, subtracted
  }
  const s32 ox = mode_ != 0 ? 0xDC : 0;
  const s32 len = mode_ != 0 ? 0x43 : 0x11F;
  // Note: the original takes the rules' second y from the box's x (both are 0 here).
  auto rule = [&](u32 rgb, s32 d) {
    auto* l = screen_alloc_prims<psyq::LINE_F2>(c->arena, 1);
    if (!l) return;
    psyq::setlen(l, 3);
    std::memcpy(&l->r0, &rgb, 4);
    l->x0 = static_cast<s16>(x + ox + 0xC + d), l->y0 = static_cast<s16>(y + 0x1E + d);
    l->x1 = static_cast<s16>(x + ox + 0xC + d + len), l->y1 = static_cast<s16>(x + 0x1E + d);
    menu::prim_link(c, slot, l);
  };
  rule(menu::menu_colour_code(0x42000000, colour, 0xC0C0C0), 0);
  auto* m2 = screen_alloc_prims<psyq::DR_MODE>(c->arena, 1);
  if (!m2) return;
  psyq::SetDrawMode(m2, 0, 1, 0x36, &tw);
  menu::prim_link(c, slot, m2);
  rule(menu::menu_colour_code(0x40000000, colour, 0x080000), 1);
}

class ItemsPage;

// ==== Item sorter (0x10 bytes; 0x80080f04-0x800815e8) =====================================
// The inventory's entries sorted by a key per order (a stable merge sort), written back
// with fresh check bytes.
class ItemSorter {
 public:
  ItemSorter();                                  // 0x80080f04
  void set_table(const u8* t) { table_ = t; }    // 0x800810ac: page data block 2 (u16 per item)
  void sort(int order);                          // 0x800810b4
  void write_back();                             // 0x80080ff4

 private:
  struct Entry {
    s32 key;
    u32 value;
  };
  void merge_sort(int lo, int hi);               // 0x80081378

  std::vector<Entry> entries_;   // +0 (0x400)
  std::vector<Entry> temp_;      // +4 (0x201)
  u8* inventory_ = nullptr;      // +8
  const u8* table_ = nullptr;    // +0xc
};

constexpr s32 kEmptyKey = 0x3B9ACA02;    // empty entries last
constexpr s32 kOtherKey = 0x3B9ACA01;    // items the order does not pick

ItemSorter::ItemSorter() {
  inventory_ = party_inventory();
  entries_.assign(0x400, Entry{kEmptyKey, 0});
  temp_.assign(0x201, Entry{0, 0});
  int n = 0;
  for (int i = 0; i < 0x400; ++i) {
    const u16 e = inv_entry(i);
    if ((e & 0x3FF) != 0) entries_[static_cast<size_t>(n++)] = Entry{kEmptyKey, e};
  }
}

void ItemSorter::sort(int order) {
  for (Entry& en : entries_) {
    const int item = static_cast<int>(en.value & 0x3FF);
    if (item == 0) continue;
    s32 key = kOtherKey;
    switch (order) {
      case 0:  // the recently obtained first
        for (int j = 0; j < 0x10; ++j)
          if (item == read_u16(inventory_ + 0x800 + j * 2)) key = 1000000000 - j;
        break;
      case 1:  // usable in battle
        if (menu::item_data_get(item, 0x29) & 1) key = 1;
        break;
      case 2:  // the game's own order
        key = table_ ? read_u16(table_ + item * 2 - 2) : kOtherKey;
        break;
      case 3: {  // weapons, strongest first
        const s32 type = menu::item_data_get(item, 0x2A);
        if (type > 0 && (type < 3 || type == 7)) key = 1000000000 - menu::item_data_get(item, 8);
        break;
      }
      case 4: {  // armour, best first
        const s32 type = menu::item_data_get(item, 0x2A);
        if (type < 0xC && type > 7) key = 1000000000 - menu::item_data_get(item, 0x10);
        break;
      }
      case 5:  // accessories
        if (menu::item_data_get(item, 0x2A) == 0xC) key = 1;
        break;
      case 6: {  // by price, dearest first
        const s32 value = (menu::item_data_get(item, 0) * menu::item_data_get(item, 0x2D)) / 100;
        key = 1000000000 - value;
        if (item == 0x171) {
          key = 1000000000 - (value + static_cast<s32>(clock_seconds() / 60) * 0xFA);
        } else if (item > 0x171) {
          if (item == 0x1DA) key = kOtherKey;
        } else if (item == 0x30) {
          key = 1;
        }
        break;
      }
      case 7:  // usable in the field
        if (menu::item_data_get(item, 0x29) & 2) key = 1;
        break;
      default: break;
    }
    en.key = key;
  }
  merge_sort(0, 0x3FF);
}

void ItemSorter::merge_sort(int lo, int hi) {
  if (lo >= hi) return;
  const int mid = (lo + hi) / 2;
  merge_sort(lo, mid);
  merge_sort(mid + 1, hi);
  int n = 0;
  for (int i = lo; i <= mid; ++i) temp_[static_cast<size_t>(n++)] = entries_[static_cast<size_t>(i)];
  int right = mid + 1, left = 0, out = lo;
  while (right <= hi) {
    if (n <= left) return;
    if (entries_[static_cast<size_t>(right)].key < temp_[static_cast<size_t>(left)].key) {
      entries_[static_cast<size_t>(out++)] = entries_[static_cast<size_t>(right++)];
    } else {
      entries_[static_cast<size_t>(out++)] = temp_[static_cast<size_t>(left++)];
    }
  }
  while (left < n) entries_[static_cast<size_t>(out++)] = temp_[static_cast<size_t>(left++)];
}

void ItemSorter::write_back() {
  for (int i = 0; i < 0x400; ++i) {
    const u16 v = static_cast<u16>(entries_[static_cast<size_t>(i)].value);
    std::memcpy(inventory_ + i * 2, &v, 2);
    inventory_[0x824 + i] = inventory_check_byte(inventory_, static_cast<u32>(i), 0xFFFF);
  }
}

// ==== Specialty / Super Specialty (Dialogs; 0x8008097c / 0x80080c8c) ======================
// The item creation specialties someone can use (group 4, a fixed order of 11 with gaps),
// or the super specialties the party has (group 5). Confirming one stores it in session
// +0x1c and finishes.
class CreationChoice final : public menu::Dialog {
 public:
  explicit CreationChoice(bool super);
  void on_confirm(int n) override;   // [13] 0x80080c1c / 0x80080e94

 private:
  void list_specialties();           // 0x80080af4
  void list_super();                 // 0x80080dfc
  s16 choice_ = 0;                   // +0x34
  menu::TextBox* box_ = nullptr;     // +0x50
  s32 ids_[0x12] = {};               // +0x54
  s32 count_ = 0;                    // +0x9c (super: +0x74)
};

CreationChoice::CreationChoice(bool super) {
  box_ = new menu::TitledBox;
  box_->set_rect_centered(0xF0, super ? 0x60 : 0x80, 1);
  box_->set_group(9);
  box_->pen_at(10, 5);
  box_->add_message(super ? 3 : 2);
  box_->set_group(super ? 5 : 4);
  if (super) {
    list_super();
  } else {
    list_specialties();
  }
  for (int i = 0; i < count_; ++i) {
    box_->pen_at(static_cast<s16>((i % 2) * 0x60 | 0x10), static_cast<s16>((i / 2) * 0xE + 0x1C));
    if (ids_[i] != 0) box_->add_message(static_cast<s16>(ids_[i] - 1));
  }
  cursor->add_grid(&choice_, 1, 2, super ? 4 : 6, 0, 0x20, 0x60, 0xE, box_);
}

void CreationChoice::list_specialties() {
  // Specialties 1, 4, 5, 6, 7, 10, 11, 13, 14, 16 and 17 (jump table 0x80081740): each
  // shows when a member who can act knows it.
  static constexpr bool kListed[17] = {true, false, false, true, true, true, true, false, false,
                                       true, true, false, true, true, false, true, true};
  count_ = 0;
  const int selected = party_get_selected();
  for (int s = 0; s < 17; ++s) {
    if (!kListed[s]) continue;
    ids_[count_] = 0;
    for (int i = 0; i < 8; ++i) {
      party_select(i);
      if (party_selected_alive() != 0 && char_specialty_level(s + 1) > 0) ids_[count_] = s + 1;
    }
    ++count_;
  }
  party_select(selected);
}

void CreationChoice::list_super() {
  count_ = 0;
  for (int i = 0; i < 8; ++i) {
    ids_[count_] = party_super_specialty_level(i + 1) < 1 ? 0 : i + 1;
    ++count_;
  }
}

void CreationChoice::on_confirm(int n) {
  const s32 id = ids_[choice_];
  if (id == 0) return;
  menu::globals().session->variant = static_cast<s8>(id);
  finish(0);
  Widget::on_confirm(n);
}

// ==== Valuables (Dialog, 0xd8 bytes, vtable 0x80081a58; 0x8007f7ac) =======================
// The key items the story has given (script flags 0x40-0x5d): a list of names (group 0xd)
// with the description of the current one (group 0xe) in a box under it.
class Valuables final : public menu::Dialog {
 public:
  Valuables();
  void draw_item(menu::TextBox* box, int slot, int index) override;   // [8] 0x8007f9cc
  void on_cursor(int n) override;                                     // [12] 0x8007f954

 private:
  s16 choice_ = 0;                  // +0x34
  s32 ids_[0x20] = {};              // +0x50
  menu::TextBox* text_ = nullptr;   // +0xd0
  menu::TextBox* list_ = nullptr;   // +0xd4
};

Valuables::Valuables() {
  text_ = new menu::TextBox;
  list_ = new menu::TitledBox;
  list_->set_rect_centered(0xB4, 0x70, 1);
  text_->set_rect_below(0xB4, 0x38, list_, 1);
  text_->move_base(0, -8);
  list_->set_group(9);
  list_->pen_at(10, 5);
  list_->add_message(4);
  list_->set_group(0xD);
  s16 n = 0;
  for (int i = 0; i < 0x1E; ++i) {
    if (script_flag(i + 0x40)) ids_[n++] = i + 1;
  }
  const s16 rows = static_cast<s16>(list_->set_list(n, 5, 0x98, 0xE, 0x10, 0x1E));
  cursor->add_grid(&choice_, 2, 1, rows, 0, 0x22, 0, 0xE, list_);
}

void Valuables::draw_item(menu::TextBox* box, int slot, int index) {
  menu::TextRenderer* r = box->text;
  r->begin_line(box->scroll->line(slot));
  // 0x8007fa44
  const s32 id = ids_[static_cast<s16>(index)];
  if (id != 0) r->draw_current(r->message(id - 1));
  r->upload_line();
}

void Valuables::on_cursor(int) {
  menu::TextBox* box = text_;
  box->clear();
  box->pen_reset();
  box->set_group(0xE);
  const s32 id = ids_[choice_];
  if (id != 0) box->add_message_lines(id - 1, 1);
}

// ==== Commands (Dialog, 0x58 bytes, vtable 0x80081b00; 0x8007f40c) =======================
// A row of 8 sort orders and three commands: Specialty, Super Specialty, Valuables.
class Commands final : public menu::Dialog {
 public:
  explicit Commands(ItemsPage* page);
  void on_child_done(int id, int state, int result) override;   // [10] 0x8007f75c
  void on_confirm(int n) override;                              // [13] 0x8007f618

 private:
  ItemsPage* page_ = nullptr;        // +0x08 (the parent)
  s16 choice_ = 0;                   // +0x34
  menu::TextBox* box_ = nullptr;     // +0x50
  s16 order_ = 0;                    // +0x54
};

// ==== Use on a member (Dialog, 0x7c bytes, vtable 0x800818f8; 0x8007fa98) ================
// The eight members in boxes of two columns (two rows shown, scrolling with the list
// box +0x50), the item and its count in the title.
class UseOnMember final : public menu::Dialog {
 public:
  UseOnMember();
  void frame() override;                // [9] 0x8007fd5c
  void on_confirm(int n) override;      // [13] 0x8007fc64
  void set_item(int item);              // 0x8007fd3c

 private:
  void refresh();                       // 0x8007fe4c
  void draw_member(menu::TextBox* box); // 0x8007ff30

  s16 choice_ = 0;                      // +0x34
  menu::TextBox* scroll_ = nullptr;     // +0x50
  menu::TextBox* title_ = nullptr;      // +0x54
  menu::TextBox* members_[8] = {};      // +0x58
  s32 item_ = 0;                        // +0x78
};

// ==== Equip from the list (Dialog + StatCalc, 0x20c bytes, vtable 0x800819b0; 0x800803d8) ==
// The eight members with arrows comparing the item with what they wear; confirming puts it
// on the member (the old piece goes back to the inventory).
class EquipDialog final : public menu::Dialog {
 public:
  EquipDialog();
  ~EquipDialog() override;              // [1] 0x800804a0
  void on_confirm(int n) override;      // [13] 0x80080548
  void set_item(int item);              // 0x80080508

 private:
  void refresh();                       // 0x80080644

  s16 choice_ = 0;                      // +0x34
  menu::StatCalc stats_;                // +0x50
  menu::TextBox* box_ = nullptr;        // +0x58
  s32 selected_ = 0;                    // +0x5c
  s16 compare_[8][0x18] = {};           // +0x60
  s32 verdict_[8] = {};                 // +0x1e0
  s32 item_ = 0;                        // +0x200
  s32 slot_ = 0;                        // +0x204
  s32 shown_item_ = 0;                  // +0x208
};

// ==== The page (Panel, 0x18b0 bytes, vtable 0x80081bb8; 0x8007e5c0) ======================
class ItemsPage final : public menu::Panel {
 public:
  ItemsPage();
  ~ItemsPage() override;                                              // [1] 0x8007e890
  void draw_item(menu::TextBox* box, int slot, int index) override;   // [8] 0x8007f070
  void on_child_done(int id, int state, int code) override;           // [10] 0x8007f2f0
  void on_cursor(int n) override;                                     // [12] 0x8007eaec
  void on_confirm(int n) override;                                    // [13] 0x8007ec50
  void on_button2(int n) override;                                    // [15] 0x8007eefc
  void show_viewer_info() override;                                   // [24] 0x8007e9a8

  void setup_list();                    // 0x8007ef60
  void renumber();                      // 0x8007e944

 private:
  void draw_row(menu::TextRenderer* r, s16 at_x, int index);   // 0x8007f110
  void finish_special(s32 special, s32 param);                // 0x8007eebc

  s16 choice_ = 0;                        // +0x34
  menu::TextBox* list_ = nullptr;         // +0x9c
  menu::TextBox* count_ = nullptr;        // +0xa0
  PictureBox* picture_ = nullptr;         // +0x70 (the info box)
  std::vector<u8> saved_vram_;            // +0xa8: (0x200, 0) 0x40 x 0x20, restored on exit
  s32 total_ = 0;                         // +0xac
  s16 numbers_[0x400] = {};               // +0xb0: position of each entry among the used ones
  const u8* names_[0x400] = {};           // +0x8b0: item names (item text group 3)
};

constexpr psyq::RECT kPictureArea{0x200, 0, 0x40, 0x20};

ItemsPage::ItemsPage() {
  menu::MenuGlobals& g = menu::globals();
  menu::strings_set(g.data.data());
  // The page's icon sheet (block 4 of the page data), as the camp page's.
  {
    std::vector<u8> tim = unpack(menu::strings_block(4));
    const u32 keep = menu::strings_word(3);
    if (keep < g.data.size()) g.data.resize(keep);
    g.session->free_cache();
    if (tim.size() >= 0x220) {
      menu::upload_image_ref(psyq::RECT{0x140, 0, 0x40, 0x40}, tim.data() + 0x220);
      menu::upload_image_ref(psyq::RECT{0x130, 0x1E0, 0x10, 0x10}, tim.data() + 0x14);
      menu::upload_free_later(std::move(tim));
    }
  }
  menu::TextRenderer::set_default_font(menu::strings_block(1), menu::strings_block(0));
  create_help();
  count_ = new menu::TextBox;
  count_->set_rect(0xF0, 0x11, 8, 8, 0);
  count_->set_flags(0x1000);
  picture_ = new PictureBox;
  info = picture_;                        // 0x800d5f04
  create_viewer(0x10E, 0x3E, 0x12);
  list_ = new menu::TextBox;
  list_->set_rect(0, 0x58, 0x138, 0x70, 0);
  setup_list();
  saved_vram_.assign(0x1000, 0);
  psyq::StoreImage(&kPictureArea, saved_vram_.data());
  psyq::ClearImage(&kPictureArea, 0, 0, 0);
  renumber();
  // The item names: group 3 of the item text, one message after the other.
  {
    const u8* pack = menu::item_strings(0);
    const u8* p = pack ? pack + read_u32(pack + 0xC) : nullptr;
    for (int i = 0; i < 0x400 && p; ++i) {
      names_[i] = p;
      for (;;) {
        const u8 c = *p++;
        if (c == 0) break;
        if (c >= 0x80) ++p;
      }
    }
  }
  // (0x8003c974: a failed inventory check prints "item checksum error!")
  help_flags = 6;
  g.session->inventory_dirty = 1;
}

ItemsPage::~ItemsPage() {
  menu::upload_queue_flush();
  psyq::LoadImage(&kPictureArea, saved_vram_.data());
  psyq::DrawSync(0);
}

void ItemsPage::renumber() {
  total_ = 0;
  for (int i = 0; i < 0x400; ++i) numbers_[i] = (inv_entry(i) & 0x3FF) == 0 ? 0 : static_cast<s16>(++total_);
}

void ItemsPage::setup_list() {
  list_->clear();
  list_->set_font_group(menu::item_strings(1), menu::item_strings(0), 3);
  cursor->clear_groups();
  choice_ = 0;
  const int last = inventory_last_used();
  const s16 rows = static_cast<s16>(list_->set_list(static_cast<s16>((last + 2) / 2), 7, 0x11C, 0xE, 0x10, 8));
  cursor->add_grid(&choice_, 2, 2, rows, 0, 0xC, 0x96, 0xE, list_);
  on_cursor(-1);
}

void ItemsPage::draw_item(menu::TextBox* box, int slot, int index) {
  menu::TextRenderer* r = box->text;
  r->begin_line(box->scroll->line(slot));
  const s16 row = static_cast<s16>(index);
  draw_row(r, 0, row * 2);
  draw_row(r, 0x96, row * 2 + 1);
  r->upload_line();
}

void ItemsPage::draw_row(menu::TextRenderer* r, s16 at_x, int index) {
  const u16 e = inv_entry(index);
  const int item = e & 0x3FF;
  if (item == 0) return;
  const u32 use = static_cast<u32>(menu::item_data_get(item, 0x29));
  const s32 type = menu::item_data_get(item, 0x2A);
  bool recent = false;
  for (int j = 0; j < 0x10; ++j)
    if (item == read_u16(party_inventory() + 0x800 + j * 2)) recent = true;
  r->set_pen_x(at_x);
  int icon;
  if ((use & 2) == 0) {
    if (type == 0) {
      r->set_tables(2);
      icon = 0x12;
    } else {
      r->set_tables(3);
      icon = 0x13;
    }
  } else {
    r->set_tables(3);
    icon = 0x10;
  }
  r->draw_pack_message(icon);
  r->set_pen_x(at_x + 0xC);
  r->set_tables(recent ? 3 : 1);
  if (names_[item]) r->draw_current(names_[item]);
  r->set_tables(1);
  r->set_pen_x(at_x + 0x76);
  r->draw_pack_message(0xF);
  r->set_pen_x(at_x + 0x7A);
  r->draw_two_digits(e >> 10 & 0x1F);
}

void ItemsPage::on_cursor(int) {
  picture_->set_size(0, 0);
  set_viewer_item(checked_item(choice_));
  const s16 position = numbers_[choice_];
  menu::TextBox* box = count_;
  box->clear();
  box->set_font_group(menu::strings_block(1), menu::strings_block(0), 0);
  menu::TextRenderer* r = box->text;
  r->set_pen(0, 0, 0xC);
  box->pen_at(0, 0);
  if (position == 0) {
    box->add_message(0xC);
  } else {
    r->add_number(position, 4);
  }
  box->pen_at(0x1A, 0);
  box->add_message(0xE);
  r->set_pen(0x23, 0, 0xC);
  r->add_number(total_, 4);
}

void ItemsPage::show_viewer_info() {
  const u8 *image = nullptr, *strings = nullptr, *font = nullptr;
  viewer->info(&image, &strings, &font);
  if (!picture_ || !image) return;
  picture_->set_mode(info_mode);
  picture_->clear();
  picture_->pen(0x14, 0x24, 0xC);
  if (info_mode == 0) {
    if (strings && read_u32(strings) == 4) {
      picture_->set_font_group(font, strings, 0);
      picture_->add_message_lines(0, 1);
    }
  } else {
    picture_->pen(0x14, 8, 0xE);
    show_item_stats(viewer->item());
  }
  const u16 w = read_u16(image), h = read_u16(image + 2);
  menu::upload_image_ref(psyq::RECT{0x200, 0, static_cast<s16>(w >> 2), static_cast<s16>(h)}, image + 4);
  picture_->set_size(w, h);
}

void ItemsPage::on_confirm(int n) {
  const int item = checked_item(choice_);
  const u32 use = static_cast<u32>(menu::item_data_get(item, 0x29));
  const s32 type = menu::item_data_get(item, 0x2A);
  if ((use & 2) == 0 && item != 0x1E4) {
    // Not usable here: equipment opens the member picker.
    if (type == 0) return;
    auto* d = new EquipDialog;
    open_child(3, d);
    d->set_item(item);
  } else {
    ItemEffect e;
    const int kind = e.kind(item);
    if (kind == 4) {
      // No target: used straight away.
      if (e.usable(item, 0) == 0) return;
      const s16 s = e.apply(item, 0);
      inventory_take_one(item);
      if (s != 0) sound = s;
      renumber();
      cursor->set_flags(0x80);
      on_cursor(1);
      return;
    }
    if (kind > 0 && kind < 5) {
      auto* d = new UseOnMember;
      open_child(2, d);
      d->set_item(item);
    } else if (kind == 5) {
      if (e.usable(item, 0) != 0) {
        const s16 s = e.apply(item, 0);
        inventory_take_one(item);
        if (s != 0) sound = s;
        finish_special(e.special, e.param);
        renumber();
        cursor->set_flags(0x80);
        on_cursor(1);
      }
    }
  }
  Widget::on_confirm(n);
}

void ItemsPage::finish_special(s32 special, s32 param) {
  int code = 3;
  if (special == 1) {
    code = 4;
    menu::globals().session->arg = static_cast<s16>(param);
  }
  finish(static_cast<s16>(code));
}

void ItemsPage::on_button2(int n) {
  open_child(1, new Commands(this));
  Widget::on_button2(-n);
}

void ItemsPage::on_child_done(int id, int state, int code) {
  if (id == 1 && state == 3) {
    if (code == 0) {
      finish(1);
    } else if (code == 1) {
      finish(2);
    }
  }
  bool refresh = false;
  if (id == 2) {
    refresh = true;
    if (menu::globals().session->unknown1b != 0) finish(3);
  }
  if (id == 3) refresh = true;
  renumber();
  if (refresh) {
    cursor->set_flags(0x80);
    on_cursor(1);
  }
}

// ==== Commands ============================================================================

Commands::Commands(ItemsPage* page) : page_(page) {
  box_ = new menu::TitledBox;
  box_->set_rect_centered(0xF0, 0x5C, 1);
  box_->set_font_group(menu::strings_block(1), menu::strings_block(0), 9);
  box_->pen_at(10, 5);
  box_->add_message(0);
  for (int i = 0; i < 8; ++i) {
    box_->pen_at(static_cast<s16>(0x48 + i * 0x14), 0x18);
    menu::PolyFT4Raw q{};
    const u8 u0 = static_cast<u8>(i << 4), u1 = static_cast<u8>(0x10 + i * 0x10);
    q.x0 = 0, q.y0 = 0, q.u0 = u0, q.v0 = 0, q.clut = 0x7813;
    q.x1 = 0x10, q.y1 = 0, q.u1 = u1, q.v1 = 0, q.tpage = 5;
    q.x2 = 0, q.y2 = 0x10, q.u2 = u0, q.v2 = 0x10;
    q.x3 = 0x10, q.y3 = 0x10, q.u3 = u1, q.v3 = 0x10;
    box_->add_icon(q);
  }
  box_->pen(0x10, 0x1C, 0xE);
  for (int i = 1; i <= 4; ++i) box_->add_message(static_cast<s16>(i));
  order_ = 0;
  cursor->add_grid(&order_, 0, 8, 1, 0x40, 0x20, 0x14, 0, box_);
  cursor->add_grid(&choice_, 0, 1, 3, 0, 0x2E, 0, 0xE, box_);
}

void Commands::on_confirm(int n) {
  if (n == 1) {
    // The sort row.
    ItemSorter sorter;
    sorter.set_table(menu::strings_block(2));
    sorter.sort(order_);
    sorter.write_back();
    page_->setup_list();
  } else if (choice_ == 0) {
    open_child(1, new CreationChoice(false));
  } else if (choice_ == 1) {
    open_child(2, new CreationChoice(true));
  } else if (choice_ == 2) {
    open_child(3, new Valuables);
  }
  Widget::on_confirm(n);
}

void Commands::on_child_done(int id, int state, int) {
  if ((id == 1 || id == 2) && state == 3) finish(choice_);
}

// ==== Use on a member =====================================================================

UseOnMember::UseOnMember() {
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

void UseOnMember::set_item(int item) {
  item_ = item;
  refresh();
}

void UseOnMember::frame() {
  Dialog::frame();
  for (menu::TextBox* b : members_) b->dy = static_cast<s16>(-scroll_->scroll->v[0]);
  // The member boxes draw inside the band under the title.
  auto* a = screen_alloc_prims<psyq::DR_AREA>(ctx->arena, 1);
  if (!a) return;
  const psyq::RECT r{0, static_cast<s16>(screen_draw_y() + 100), 0x140, 100};
  psyq::SetDrawArea(a, &r);
  menu::prim_link(ctx, 4, a);
}

void UseOnMember::on_confirm(int) {
  if (inventory_count(item_) == 0) return;
  ItemEffect e;
  if (party_slot_active(choice_) == 0) return;
  if (e.kind(item_) == 0) return;
  if (e.usable(item_, choice_) == 0) return;
  const s16 s = e.apply(item_, choice_);
  inventory_take_one(item_);
  refresh();
  if (s != 0) sound = s;
  if (party_all_down(menu::globals().session)) cancel();
}

void UseOnMember::refresh() {
  menu::TextBox* t = title_;
  t->clear();
  t->set_group(0xB);
  t->pen_at(10, 5);
  t->add_message(0);
  t->pen_at(0xAA, 5);
  t->add_item_count(static_cast<s16>(item_));
  const int selected = party_get_selected();
  for (int i = 0; i < 8; ++i) {
    party_select(i);
    members_[i]->clear();
    draw_member(members_[i]);
  }
  party_select(selected);
}

void UseOnMember::draw_member(menu::TextBox* box) {
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
    sb.set(items_text(0x800816F8));
    sb.put(items_text(0x800816FC));   // LV
    sb.put(items_text(0x80081704));   // %3d
    sb.put_int(static_cast<s16>(read_u16(party_selected_slot() + 0x26)));
    box->add_string(sb.str());
  }
  box->pen_at(0x28, 0x18);
  {
    menu::StrBuf sb;
    sb.set(items_text(0x800816F8));
    for (u32 a : {0x80081708u, 0x80081710u, 0x80081714u, 0x8008171Cu}) sb.put(items_text(a));   // HP / MP
    box->add_string(sb.str());
  }
  box->pen_at(0x48, 0x18);
  box->add_party_status(static_cast<s16>(party_get_selected()));
}

// ==== Equip ===============================================================================

EquipDialog::EquipDialog() {
  box_ = new menu::TitledBox;
  box_->set_rect(0, 0x52, 0x138, 0x76, 0);
  cursor->add_grid(&choice_, 1, 4, 2, 0, 0x84, 0x4E, 0x32, nullptr);
  selected_ = party_get_selected();
}

EquipDialog::~EquipDialog() { party_select(selected_); }

void EquipDialog::set_item(int item) {
  shown_item_ = item;
  item_ = item;
  slot_ = stats_.equip_slot(item);
  refresh();
}

void EquipDialog::refresh() {
  menu::TextBox* box = box_;
  box->clear();
  box->set_group(0xB);
  box->pen_at(10, 5);
  box->add_message(1);
  box->pen_at(0xAA, 5);
  box->add_item_count(static_cast<s16>(shown_item_));
  for (int i = 0; i < 8; ++i) {
    party_select(i);
    const s16 at_x = static_cast<s16>((i % 4) * 0x4E + 0x10);
    const s16 at_y = static_cast<s16>((i / 4) * 0x32 + 0x12);
    const int character = party_selected_char();
    if (character == 0) continue;
    const s32 worn = char_get_equip(slot_);
    s16* c = compare_[i];
    stats_.snapshot_attrs(c);
    stats_.snapshot_equip(c + 10);
    const u32 can = stats_.can_equip(item_);
    if (can != 0) char_set_equip(slot_, item_);
    stats_.snapshot_attrs(c + 5);
    stats_.snapshot_equip(c + 0x11);
    char_set_equip(slot_, worn);
    s16 before = 0, after = 0;
    if (slot_ == 0) {
      before = c[0], after = c[5];
    } else if (slot_ >= 1 && slot_ <= 4) {
      before = c[1], after = c[6];
    }
    int verdict = 0;
    if (can != 0) {
      verdict = 1;
      if (worn != item_) verdict = after == before ? 2 : (before < after ? 3 : 4);
    }
    verdict_[i] = verdict;
    box->pen_at(at_x, at_y);
    box->add_party_character(character, verdict == 0 ? 0x12 : 6, 0x1000);
    box->pen_at(at_x, at_y);
    if (verdict != 0) box->add_icon_anim(verdict_arrow(verdict), 2, 0x10);
  }
}

void EquipDialog::on_confirm(int n) {
  party_select(choice_);
  if (party_selected_alive() != 0 && verdict_[choice_] != 0 && inventory_count(item_) != 0) {
    // 0x800805f0
    inventory_give_one(char_get_equip(slot_));
    char_set_equip(slot_, item_);
    inventory_take_one(item_);
    refresh();
    Widget::on_confirm(-n);
  }
}

}  // namespace

// ==== Page handler ========================================================================

void menu_page03_update(menu::MenuPageLoader* p) {
  switch (p->substate) {
    case 1:
      p->publish_id();
      p->push(new ItemsPage);
      break;
    case 2:
      p->pop();
      break;
    case 3:
      switch (p->choice) {
        case 1: p->goto_page(0xF); break;
        case 2: p->goto_page(0x10); break;
        case 3: p->goto_page(0x14); break;
        case 4: p->goto_page(0x13); break;
        default: break;
      }
      break;
    default:
      break;
  }
}

}  // namespace ss::game::field
