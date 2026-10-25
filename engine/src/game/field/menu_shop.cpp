#include "game/field/menu_shop.h"

#include <algorithm>
#include <cstring>
#include <vector>

#include "disc/slz.h"
#include "game/ap/ap.h"
#include "game/boot/pad.h"
#include "game/common/menu_core.h"
#include "game/common/menu_gfx.h"
#include "game/common/menu_scene.h"
#include "game/common/menu_widget.h"
#include "game/common/party.h"
#include "game/field/field.h"
#include "game/field/mods.h"
#include "game/field/script_vm.h"

namespace ss::game::field {
namespace {

constexpr int kShopCode = 0xBAA;   // page 0xb's code overlay (at 0x8007e000)

const u8* shop_table(u32 address) { return menu::page_table(kShopCode, address); }

bool script_flag(int n) { return ((globals().script_flags[n >> 3] >> (n & 7)) & 1) != 0; }   // 0x80055ecc
void script_flag_set(int n) { globals().script_flags[n >> 3] |= static_cast<u8>(1 << (n & 7)); }   // 0x80055efc
void script_flag_clear(int n) { globals().script_flags[n >> 3] &= static_cast<u8>(~(1 << (n & 7))); }  // 0x80055f3c

s32 rd32(const u8* p) {
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

s32 g_clock_price = 0;  // 0x80081e28: item 0x171's price rises 250 Fol a minute of play

// 0x80033abc: 0..max.
s32 clamp_money(s32 v, s32 max) {
  if (v < 0) return 0;
  return v > max ? max : v;
}

// ---- Item list (0x10 bytes; 0x80080b2c-0x80080de4) -----------------------------------
// The items a shop shows: the stock to buy or the inventory to sell, with the count held
// and the count in the cart.
struct ItemList {
  struct Entry {            // 8 bytes
    s16 index = 0;          // +0
    s16 item = 0;           // +2
    s16 have = 0;           // +4
    s16 cart = 0;           // +6
  };
  std::vector<Entry> entries;  // +0 count, +4 capacity, +0xc entries
  u8* inventory = nullptr;     // +8: handoff 6

  ItemList() { inventory = globals().inventory; }   // 0x80080b2c
  int count() const { return static_cast<int>(entries.size()); }
  void add(s16 item, s16 have) {                    // 0x80080de4
    entries.push_back(Entry{static_cast<s16>(entries.size()), item, have, 0});
  }
  void add_held(s16 item) {                         // 0x80080d14
    const int i = inventory_find(inventory, item);
    s16 have = 0;
    if (i != -1) have = static_cast<s16>(read_u16(inventory + i * 2) >> 10 & 0x1F);
    add(item, have);
  }
  void add_list(const u8* list) {                   // 0x80080d8c: zero-terminated s16 items
    for (; list && read_u16(list) != 0; list += 2) add_held(static_cast<s16>(read_u16(list)));
  }
  void add_inventory() {                            // 0x80080c04: what can be sold
    for (int i = 0; i < 0x400; ++i) {
      const u16 e = read_u16(inventory + i * 2);
      if ((e & 0x3FF) != 0 && (e >> 10 & 0x1F) != 0 && menu::item_data_get(e & 0x3FF, 0) != 0 &&
          !ap::sell_locked(e & 0x3FF))  // Archipelago v2: the Silver Trumpet in an Iseria Queen goal [hooks agent]
        add(static_cast<s16>(e & 0x3FF), static_cast<s16>(e >> 10 & 0x1F));
    }
  }
  void add_owned_range(int first, int n) {          // 0x80080ca4
    for (int item = first; item < first + n; ++item)
      if (inventory_count(item) != 0) add_held(static_cast<s16>(item));
  }
};

class Shop;

// ---- The buy / sell list (Dialog, 0x70 bytes, vtable 0x800813c0; 0x8007f788) ----------
// Left / right change the count in the cart; confirming asks to settle.
class ShopList final : public menu::Dialog {
 public:
  explicit ShopList(Shop* shop);
  ~ShopList() override;
  void draw_item(menu::TextBox* box, int slot, int index) override;   // [8] 0x8007fe34
  void on_child_done(int id, int state, int result) override;         // [10] 0x8007fb54
  void on_cursor(int n) override;                                     // [12] 0x8007fdd8
  void on_confirm(int n) override;                                    // [13] 0x8007fa34
  void on_button3(int n) override;                                    // [16] 0x8007faf4
  void on_left_right(int n) override;                                 // [19] 0x8007fc64

 private:
  void show_held(int item);       // 0x8007fb94
  bool change(int delta);         // 0x8007fd48
  s32 price(int item) const;      // 0x8007ff94
  void draw_row(menu::TextRenderer* r, s16 x, int index);  // 0x8007feac
  void upload_ap_mark();          // port (Archipelago)

  s16 choice_ = 0;                // +0x34
  Shop* shop_ = nullptr;          // +0x50
  s32 buying_ = 0;                // +0x54
  menu::TextBox* list_box_ = nullptr;   // +0x58
  menu::TextBox* held_box_ = nullptr;   // +0x5c
  ItemList* list_ = nullptr;      // +0x60
  s32 discount_ = 0;              // +0x64: buying, percent off
  s32 haggle_ = 0;                // +0x68: selling, percent on top (skill 4)
  s32 bargain_ = 0;               // +0x6c: super specialty 6, percent either way
  int ap_mark_ = 0;               // port: the Archipelago mark's VRAM (a CVram handle; 0 none)
};

// ---- "Account settlement: is this acceptable?" (Dialog, vtable 0x80081318; 0x80080158) ---
class SettleDialog final : public menu::Dialog {
 public:
  SettleDialog();
  void on_confirm(int n) override;   // [13] 0x80080294
  s16 choice = 0;                    // +0x34
};

// ---- The shop page (Panel, 0x2b8 bytes, vtable 0x80081478; 0x8007e000) ----------------
class Shop final : public menu::Panel {
 public:
  Shop();
  ~Shop() override;                                            // [1] 0x8007e304
  void on_close() override;                                    // [4] 0x8007e37c
  void frame() override;                                       // [9] 0x8007ecd8
  void on_child_done(int id, int state, int result) override;  // [10] 0x8007f0f8
  void on_cursor(int n) override;                              // [12] 0x8007f280
  void on_confirm(int n) override;                             // [13] 0x8007ef28
  void on_button3(int n) override { Panel::on_button3(n); }    // [16] 0x8007f25c
  void draw_help() override;                                   // [22] 0x8007ec84
  void draw_member() override;                                 // [23] 0x8007e97c

  void hover(int item);           // 0x8007f31c: the list's current item
  void show_money();              // 0x8007f52c

  s32 buying = 0;                 // +0x98: the list opened to buy
  ItemList sell;                  // +0x9c
  ItemList buy;                   // +0xac
  s32 purchase = 0;               // +0xbc
  s32 sales = 0;                  // +0xc0
  s32 balance = 0;                // +0xc4
  u8 order = 0;                   // +0xc8: an order shop (ids 0x5a-0x5e), delivered later
  u8 order_level = 0;             // +0xc9: the member's specialty 0xc level
  s32 money = 0;                  // +0xcc: Fol when the shop opened

 private:
  void load_items();              // the item data, as the camp page's on_close
  void setup_lists();             // 0x8007e56c
  int settle();                   // 0x8007e6cc
  void show_commands();           // 0x8007f37c
  void show_help_text();          // 0x8007f6f0
  void show_choice_info();        // 0x8007f2a0
  void notice(int id, int message);

  s16 choice_ = 0;                        // +0x34
  menu::TextBox* commands_ = nullptr;     // +0xd0
  menu::TextBox* money_box_ = nullptr;    // +0xd4
  menu::TextBox* help_text_ = nullptr;    // +0xd8
  menu::TextBox* members_ = nullptr;      // +0xdc
  s32 can_buy_ = 0;                       // +0xe4
  s32 can_sell_ = 0;                      // +0xe8
  s32 can_settle_ = 0;                    // +0xec
  s16 compare_[8][24] = {};               // +0xf4: attributes / equipment before and after
  s32 verdict_[8] = {};                   // +0x274: 0 cannot wear, 1 same item, 2 even, 3 better, 4 worse
  s32 hovered_ = 0;                       // +0x294
  s32 hovered_slot_ = -1;                 // +0x298
  menu::BgLoadClient items_client_;       // +0x29c
  s32 item_sizes_[4] = {};                // +0x2a4
  std::vector<u8> item_buffer_;           // +0x2b4
};

// ==== Shop ===============================================================================

Shop::Shop() {
  menu::MenuGlobals& g = menu::globals();
  menu::strings_set(g.data.data());
  menu::TextRenderer::set_default_font(menu::strings_block(1), menu::strings_block(0));
  if (g.item_data.empty()) {
    s32 total = 0;
    for (int i = 0; i < 4; ++i) {
      item_sizes_[i] = menu::menu_loader()->entry_size(3000 + i);
      total += item_sizes_[i];
    }
    item_buffer_.assign(static_cast<size_t>(total), 0);
    menu::bgload_queue_bytes(&items_client_, 3000, item_buffer_.data(), total);
  }
  create_help();
  commands_ = new menu::TextBox;
  commands_->set_rect(0, 0x53, 0x88, 0x18, 0);
  members_ = new menu::TextBox;
  members_->set_rect(0, 0, 0x68, 0x50, 0);
  create_info_box();
  help_text_ = new menu::TextBox;
  help_text_->set_rect(0, 0x6A, 0xE8, 0x5E, 0);
  money_box_ = new menu::TextBox;
  money_box_->set_rect(0xE8, 0x58, 0x50, 0x70, 0);
  const s16 shop = g.session->arg;
  if (static_cast<u16>(shop - 0x5A) < 5) {
    order = 1;
    order_level = static_cast<u8>(char_specialty_level(0xC));
  } else {
    order = 0;
  }
  cursor->add_grid(&choice_, 0, order ? 2 : 3, 1, 0, 8, 0x28, 0, commands_);
  purchase = 0;
  sales = 0;
  money = rd32(g.config + 0x18);
  if (!g.item_data.empty()) setup_lists();
  hovered_ = 0;
  hovered_slot_ = -1;
  draw_member();
  show_commands();
  show_money();
  show_help_text();
}

Shop::~Shop() = default;

void Shop::load_items() {
  // 3000 the item pack, 3001 code for the panels (native here), 3002 the records the
  // inventory points at, 3003 the resident 0xbbb; the item icons to (0x180, 0x180).
  menu::MenuGlobals& g = menu::globals();
  const u8* p = item_buffer_.data();
  g.item_data = unpack(p);
  p += item_sizes_[0] + item_sizes_[1];
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

void Shop::on_close() {
  if (menu::globals().item_data.empty()) {
    if (!menu::bgload_owner_done(&items_client_)) {
      menu::bgload_cancel_owner(&items_client_);
    } else {
      load_items();
    }
  }
  Widget::on_close();
}

void Shop::frame() {
  menu::MenuGlobals& g = menu::globals();
  if (g.item_data.empty() && menu::bgload_owner_done(&items_client_)) {
    load_items();
    setup_lists();
    show_commands();
  }
  if (viewer == nullptr && !g.item_data.empty()) create_viewer(0x118, 0x4A, 0x18);
  Panel::frame();
}

void Shop::setup_lists() {
  g_clock_price = static_cast<s32>(clock_seconds() / 60) * 0xFA;
  const s16 shop = menu::globals().session->arg;
  if (shop == 0x39 || shop == 0x58) {
    sell.add_owned_range(0xC4, 0x18);
  } else {
    sell.add_inventory();
  }
  if (shop == 0x58) {
    // The stock grows with script flags 0x194 + item.
    for (const u8* p = shop_table(read_u32(shop_table(0x80081DF8))); p && read_u16(p) != 0; p += 2) {
      const s16 item = static_cast<s16>(read_u16(p));
      if (script_flag(item + 0x194)) buy.add_held(item);
    }
  } else {
    const u8* entry = shop_table(0x80081C98 + shop * 4);
    if (entry) buy.add_list(mods_shop_stock(shop, shop_table(read_u32(entry))).data());  // with world edits
  }
  can_settle_ = 1;
  can_buy_ = buy.count() != 0;
  can_sell_ = sell.count() != 0;
}

int Shop::settle() {
  FieldGlobals& g = globals();
  menu::MenuSession* session = menu::globals().session;
  const s16 shop = session->arg;
  int sold = 0, bought = 0, ordered = 0;
  for (const ItemList::Entry& e : sell.entries) {
    if (e.cart == 0) continue;
    inventory_remove_at(g.inventory, inventory_find(g.inventory, e.item), e.cart);
    sold = 1;
    if (shop == 0x39 && static_cast<u32>(e.item - 0xC4) < 0x18) script_flag_set(e.item + 0x16C);
    if (shop == 0x58 && static_cast<u32>(e.item - 0xC4) < 0x18) script_flag_set(e.item + 0x194);
  }
  if (!order) {
    for (const ItemList::Entry& e : buy.entries) {
      if (e.cart == 0) continue;
      bought = 1;
      inventory_add(g.inventory, e.item, e.cart, 1);
    }
  } else {
    // Orders arrive later: up to ten in game_state +0x250, due at the clock in +0x294
    // (negative until the field takes them; sooner with specialty 0xc).
    u8* gs = g.game_state;
    std::memset(gs + 0x250, 0, 20);
    int n = 0;
    for (const ItemList::Entry& e : buy.entries) {
      if (e.cart == 0) continue;
      ordered = 1;
      if (n < 10) {
        const u16 v = static_cast<u16>(e.cart << 10 | e.item);
        std::memcpy(gs + 0x250 + n * 2, &v, 2);
        ++n;
      }
    }
    if (ordered) {
      const u8* wait = shop_table(0x80081C86 + order_level * 2);
      wr32(gs + 0x294, -static_cast<s32>(clock_seconds() + (wait ? static_cast<s16>(read_u16(wait)) : 0)));
    }
  }
  wr32(g.config + 0x18, clamp_money(balance, 999999999));
  // Archipelago (port feature; shop slots) [world agent, shopboth agent]: the first purchase of
  // a slot (an order: placing it) is also its check, its item given or sent; every copy paid
  // for is the player's all the same. Booked after the purchases and the money, so that a
  // check's item never takes a bought copy's room (the courier lets an ordered copy wait,
  // field_world.cpp) and a check's Fol is not overwritten by the balance.
  for (const ItemList::Entry& e : buy.entries)
    if (e.cart != 0) ap::on_shop_buy(shop, e.index, e.item, e.cart);
  session->select_result = static_cast<s8>(bought);
  const int any = bought + sold + ordered;
  if (any != 0) {
    // One-shot bargains are used up.
    script_flag_clear(0x2C8);
    script_flag_clear(0x2C9);
    script_flag_clear(0x2DF);
    script_flag_clear(0x2E0);
  }
  return any;
}

void Shop::notice(int id, int message) {
  auto* d = new menu::ConfirmDialog;
  open_child(static_cast<s16>(id), d);
  menu::dialog_show_message(d, 0xB, message);
}

void Shop::on_confirm(int n) {
  if (menu::globals().item_data.empty()) return;
  if (choice_ == 0) {
    if (!can_buy_) return;
    buying = 1;
    open_child(1, new ShopList(this));
  }
  if (!order && choice_ == 1) {
    if (!can_sell_) return;
    buying = 0;
    open_child(1, new ShopList(this));
  }
  if (choice_ == 2 || (choice_ == 1 && order)) {
    if (!can_settle_) {
      notice(2, 1);  // not enough Fol
      return;
    }
    if (settle() == 0) {
      notice(3, 0);  // nothing bought or sold
    } else {
      finish(0);
    }
  }
  Widget::on_confirm(n);
}

void Shop::on_child_done(int id, int state, int) {
  if (id == 1) {
    if (state == 3) {
      if (settle() == 0) {
        notice(3, 0);
      } else {
        finish(0);
      }
    } else {
      can_settle_ = balance >= 0 ? 1 : 0;
      set_viewer_item(0);
      on_cursor(0);
      show_commands();
      hovered_ = 0;
      hovered_slot_ = -1;
      draw_member();
    }
  } else if (id == 3) {
    cancel();
  }
}

void Shop::on_cursor(int) { show_choice_info(); }

void Shop::show_choice_info() {
  menu::TextBox* box = info;
  box->clear();
  box->set_group(7);
  box->pen_reset();
  box->add_message_lines(order ? choice_ + 3 : choice_, 1);
}

void Shop::draw_help() {
  help->set_group(0xC);
  help->add_message(static_cast<s16>(menu::globals().session->arg - 1));
}

void Shop::hover(int item) {
  set_viewer_item(item);
  hovered_ = item;
  hovered_slot_ = stats.equip_slot(item);
  draw_member();
}

void Shop::show_commands() {
  int b = can_buy_, s = can_sell_, e = can_settle_;
  if (menu::globals().item_data.empty()) b = s = e = 0;
  menu::TextBox* box = commands_;
  box->clear();
  box->set_group(6);
  menu::TextRenderer* r = box->text_renderer();
  box->pen_at(0x10, 6);
  r->set_colour(b ? 1 : 4);
  if (!order) {
    box->add_message(0);
    box->pen_at(0x38, 6);
    r->set_colour(s ? 1 : 4);
    box->add_message(1);
    box->pen_at(0x60, 6);
    r->set_colour(e ? 1 : 4);
    box->add_message(2);
  } else {
    box->add_message(3);
    box->pen_at(0x38, 6);
    r->set_colour(s ? 1 : 4);
    box->add_message(4);
  }
}

void Shop::show_money() {
  menu::TextBox* box = money_box_;
  box->clear();
  box->set_group(8);
  menu::TextRenderer* r = box->text_renderer();
  balance = money + sales - purchase;
  r->set_pen(8, 8, 0xC);
  const s32 values[4] = {money, purchase, sales, balance};
  for (int i = 0; i < 4; ++i) {
    r->set_colour(2);
    r->add_message_line(i);
    r->set_colour(1);
    s32 v = values[i];
    if (v < -999999999) v = -999999999;  // 0x8007f6c4
    if (v > 999999999) v = 999999999;
    r->add_number(v, 10);
  }
}

void Shop::show_help_text() {
  menu::TextBox* box = help_text_;
  box->clear();
  box->pen_reset();
  box->set_group(10);
  box->add_message_lines(order ? 1 : 0, 1);
  box->add_message(2);
  box->add_message(3);
  box->add_message(4);
}

void Shop::draw_member() {
  // Each member's sprite; with an equippable item under the list cursor, an arrow says
  // whether it would raise (3), keep (2) or lower (4) ATK (weapons) or AC (armour), or
  // that it is already worn (1).
  const int selected = party_get_selected();
  menu::TextBox* box = members_;
  box->clear();
  for (int i = 0; i < 8; ++i) {
    party_select(i);
    const s16 col = static_cast<s16>((i % 4) * 0x16);
    const s16 row = static_cast<s16>((i / 4) << 5);
    const int character = party_selected_char();
    if (character == 0) continue;
    int verdict = 0;
    if (!menu::globals().item_data.empty()) {
      const int slot = hovered_slot_;
      const s32 worn = char_get_equip(slot);
      s16* c = compare_[i];
      stats.snapshot_attrs(c);
      stats.snapshot_equip(c + 10);
      const u32 can = stats.can_equip(hovered_);
      if (can != 0) char_set_equip(slot, hovered_);
      stats.snapshot_attrs(c + 5);
      stats.snapshot_equip(c + 0x11);
      char_set_equip(slot, worn);
      s16 before = 0, after = 0;
      if (slot == 0) {
        before = c[0], after = c[5];
      } else if (slot >= 1 && slot <= 4) {
        before = c[1], after = c[6];
      }
      if (can != 0) {
        verdict = 1;
        if (worn != hovered_) verdict = after == before ? 2 : (before < after ? 3 : 4);
      }
    }
    verdict_[i] = verdict;
    box->pen_at(col, row);
    box->add_party_character(character, verdict == 0 ? 0x12 : 6, 0xC00);
    box->pen_at(col, row);
    if (verdict != 0) {
      menu::PolyFT4Raw q{};
      const u8 u0 = static_cast<u8>((verdict - 1) * 0x18), u1 = static_cast<u8>(u0 + 0x18);
      q.x0 = 4, q.y0 = 0x18, q.u0 = u0, q.v0 = 0x20, q.clut = 0x7850;
      q.x1 = 0x1C, q.y1 = 0x18, q.u1 = u1, q.v1 = 0x20, q.tpage = 0x16;
      q.x2 = 4, q.y2 = 0x28, q.u2 = u0, q.v2 = 0x30;
      q.x3 = 0x1C, q.y3 = 0x28, q.u3 = u1, q.v3 = 0x30;
      box->add_icon_anim(q, 2, 0x10);
    }
  }
  party_select(selected);
}

// ==== ShopList ===========================================================================

// Archipelago (port feature; shop slots) [shopboth agent]: the mark of a slot whose check is
// still open, drawn after its name: the Archipelago logo's six circles (archipelago.gg, MIT)
// with the text's drop shadow ('s'), 12 x 12 texels of a 15-bit page in the menus' VRAM.
constexpr int kMarkSize = 12;
constexpr const char* kApMark[kMarkSize] = {
    "....rrr.....", "...rrrrr....", ".yyyrrrggg..", "yyyyyrggggg.", "yyyy...ggggs", ".bbb...pppss",
    "bbbb...pppp.", "bbbbooopppps", ".bboooooppss", "..sooooosss.", "....oooss...", ".....sss....",
};

u16 mark_texel(char c) {
  u32 rgb = 0;
  switch (c) {
    case 'r': rgb = 0xC97682; break;
    case 'y': rgb = 0xEEE391; break;
    case 'g': rgb = 0x75C275; break;
    case 'b': rgb = 0x767EBD; break;
    case 'p': rgb = 0xCA94C2; break;
    case 'o': rgb = 0xD9A07D; break;
    case 's': rgb = 0x101018; break;
    default: return 0;  // transparent
  }
  return static_cast<u16>(0x8000 | (rgb >> 19 & 0x1F) | (rgb >> 11 & 0x1F) << 5 | (rgb >> 3 & 0x1F) << 10);
}

void ShopList::upload_ap_mark() {
  ap_mark_ = menu::cvram_alloc(kMarkSize, kMarkSize);  // (0 when the text VRAM is full: colour 2 instead)
  if (ap_mark_ == 0) return;
  std::vector<u16> texels(static_cast<size_t>(kMarkSize * kMarkSize));
  for (int y = 0; y < kMarkSize; ++y)
    for (int x = 0; x < kMarkSize; ++x) texels[static_cast<size_t>(y * kMarkSize + x)] = mark_texel(kApMark[y][x]);
  menu::upload_image(psyq::RECT{menu::cvram_x(ap_mark_), menu::cvram_y(ap_mark_), kMarkSize, kMarkSize}, std::move(texels));
}

ShopList::ShopList(Shop* shop) {
  shop_ = shop;
  buying_ = shop->buying;
  list_ = buying_ ? &shop->buy : &shop->sell;
  if (buying_) {
    for (const ItemList::Entry& e : list_->entries) {
      if (ap::shop_slot_open(menu::globals().session->arg, e.index, e.item)) {
        upload_ap_mark();  // Archipelago: an open slot on the list [shopboth agent]
        break;
      }
    }
  }
  discount_ = 0;
  if (script_flag(0x2C8)) discount_ += 0x19;
  if (script_flag(0x2C9)) discount_ += 5;
  haggle_ = 0;
  const int selected = party_get_selected();
  int best = 0;
  for (int i = 0; i < 8; ++i) {
    party_select(i);
    if (party_selected_char() != 0 && best < char_skill_level(4)) best = char_skill_level(4);
  }
  bargain_ = 0;
  haggle_ += best * 3;
  if (script_flag(0x2DF)) bargain_ = party_super_specialty_level(6) * 3;
  if (script_flag(0x2E0)) bargain_ = party_super_specialty_level(6) * -3;
  party_select(selected);
  held_box_ = new menu::TextBox;
  held_box_->set_rect(0x88, 0x53, 0x60, 0x18, 0);
  show_held(0);
  list_box_ = new menu::TextBox;
  list_box_->set_rect(0, 0x6A, 0xE8, 0x5E, 0);
  list_box_->set_font_group(menu::item_strings(1), menu::item_strings(0), 3);
  const s16 shown = static_cast<s16>(list_box_->set_list(static_cast<s16>(list_->count()), 6, 0xCA, 0xE, 0x10, 8));
  list_box_->scroll->v[8] = 6;
  cursor->add_grid(&choice_, 2, 1, shown, 0, 10, 0, 0xE, list_box_);
  help_flags = 4;
}

ShopList::~ShopList() { menu::cvram_mark_free(ap_mark_); }

void ShopList::show_held(int item) {
  menu::TextBox* box = held_box_;
  box->clear();
  box->set_group(9);
  box->pen_at(0x10, 6);
  box->add_message(0);
  if (item != 0) {
    u16 count = 0;
    const int i = inventory_find(globals().inventory, item);
    if (i >= 0) count = static_cast<u16>(read_u16(globals().inventory + i * 2) >> 10 & 0x1F);
    menu::TextRenderer* r = box->text_renderer();
    r->set_pen(0x44, 6, 0xC);
    r->add_number(count, 2);
  }
}

s32 ShopList::price(int item) const {
  s32 mult = 1;
  s32 base = menu::item_data_get(item, 0);
  if (base > 9999999) {
    mult = 10;
    base /= 10;
  }
  if (item == 0x171) base += g_clock_price;
  if (item == 0x1DA) base = -base;
  s32 adjust;
  if (buying_ == 0) {
    base = (base * menu::item_data_get(item, 0x2D)) / 100;
    adjust = (base * haggle_) / 100 + (base * bargain_) / 100;
  } else {
    adjust = (base * bargain_) / 100 - (base * discount_) / 100;
  }
  const s32 p = (base + adjust) * mult;
  return p == 0 ? 1 : p;
}

bool ShopList::change(int delta) {
  ItemList::Entry& e = list_->entries[static_cast<size_t>(choice_)];
  int cart = e.cart + delta;
  if (buying_ == 0) {
    if (cart < 0 || cart > e.have) cart -= delta;  // no more than is held
  } else if (cart < 0) {
    cart -= delta;
  } else if (cart + e.have > 0x14) {
    cart -= delta;                                 // 20 of a kind at most
  }
  if (e.cart == cart) return false;
  e.cart = static_cast<s16>(cart);
  return true;
}

void ShopList::on_left_right(int n) {
  const int delta = n == 1 ? -1 : 1;
  if (change(delta)) {
    const s32 p = price(list_->entries[static_cast<size_t>(choice_)].item);
    if (buying_ == 0) {
      shop_->sales += delta * p;
    } else {
      shop_->purchase += delta * p;
    }
    shop_->show_money();
    cursor->set_flags(0x80);
    Widget::on_left_right(-n);
  }
}

void ShopList::on_cursor(int) {
  if (choice_ < 0 || choice_ >= list_->count()) return;
  const int item = list_->entries[static_cast<size_t>(choice_)].item;
  shop_->hover(item);
  show_held(item);
}

void ShopList::on_confirm(int n) {
  if (shop_->balance < 0) {
    auto* d = new menu::ConfirmDialog;
    open_child(2, d);
    menu::dialog_show_message(d, 0xB, 1);
  } else {
    open_child(1, new SettleDialog);
    Widget::on_confirm(-n);
  }
}

void ShopList::on_button3(int n) {
  shop_->on_button3(n);
  Widget::on_button3(n);
}

void ShopList::on_child_done(int id, int state, int) {
  if (id == 1 && state == 3) finish(0);
}

void ShopList::draw_item(menu::TextBox* box, int slot, int index) {
  menu::TextRenderer* r = box->text;
  r->begin_line(box->scroll->line(slot));
  draw_row(r, 0, index);
  r->upload_line();
}

void ShopList::draw_row(menu::TextRenderer* r, s16 x, int index) {
  // The original reads the entry even past the list's end (rows below a short list).
  if (index < 0 || index >= list_->count()) return;
  const ItemList::Entry& e = list_->entries[static_cast<size_t>(index)];
  r->set_pen_x(x);
  // Archipelago (port feature) [world agent, shopboth agent]: a slot whose check is still open
  // has the Archipelago mark after its name (no room for it in VRAM: the name in colour 2).
  // The rows are a ring of lines: a line's mark from the row it showed before goes first.
  std::vector<menu::TextIcon>& marks = r->line(r->current_line())->icons;
  std::erase_if(marks, [](const menu::TextIcon& i) { return i.tpage != 0; });
  const bool slot = buying_ && ap::shop_slot_open(menu::globals().session->arg, e.index, e.item);
  if (slot && ap_mark_ == 0) r->set_tables(2);
  r->draw_current(r->message(e.item));
  if (slot && ap_mark_ == 0) r->set_tables(1);
  const s32 cost = price(e.item);
  if (slot && ap_mark_ != 0) {
    // After the name, left of the price's first digit (the price is right-aligned in 8 x 6 px).
    int digits = 1;
    for (s32 v = cost < 0 ? -cost : cost; v >= 10; v /= 10) ++digits;
    const int room = x + 0x78 + (8 - digits - (cost < 0 ? 1 : 0)) * 6 - kMarkSize - 1;
    menu::TextIcon mark;
    mark.x = static_cast<s16>(std::min(r->pen_x() + 2, room));
    const int vx = menu::cvram_x(ap_mark_), vy = menu::cvram_y(ap_mark_);
    mark.tpage = static_cast<u16>(0x100 | (vy >> 4 & 0x10) | (vx & 0x3FF) >> 6);  // 15-bit page
    mark.u = static_cast<u8>(vx & 0x3F);
    mark.v = static_cast<u8>(vy & 0xFF);
    mark.w = mark.h = kMarkSize;
    marks.push_back(mark);
  }
  r->set_pen_x(x + 0x78);
  r->draw_number(cost, 8);
  r->set_pen_x(x + 0xA8);
  r->draw_pack_message(0xD);
  r->set_pen_x(x + 0xB8);
  r->draw_two_digits(e.cart);
}

// ==== SettleDialog ===========================================================================

SettleDialog::SettleDialog() {
  auto* box = new menu::TitledBox;
  box->set_rect_centered(0xD0, 0x40, 1);
  box->pen_at(10, 5);
  box->set_group(5);
  box->add_message(0);
  box->set_group(3);
  box->pen(0x10, 0x1C, 0xE);
  box->add_message_lines(0, 1);
  box->set_group(4);
  box->pen_at(0x38, 0x2C);
  box->add_message(0);
  box->pen_at(0x68, 0x2C);
  box->add_message(1);
  cursor->add_grid(&choice, 1, 2, 1, 0x28, 0x30, 0x30, 0, box);
}

void SettleDialog::on_confirm(int n) {
  if (choice == 0) {
    finish(0);
  } else {
    cancel();
  }
  Widget::on_confirm(-n);
}

}  // namespace

void menu_page0b_update(menu::MenuPageLoader* p) {
  switch (p->substate) {
    case 1:
      p->publish_id();
      p->push(new Shop);
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
