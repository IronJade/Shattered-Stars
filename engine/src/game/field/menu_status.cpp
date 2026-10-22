#include "game/field/menu_status.h"

#include <cstring>
#include <vector>

#include "disc/slz.h"
#include "game/common/menu_core.h"
#include "game/common/menu_gfx.h"
#include "game/common/menu_scene.h"
#include "game/common/menu_widget.h"
#include "game/common/party.h"
#include "game/field/field.h"
#include "game/field/script_vm.h"
#include "runtime/libgpu.h"

namespace ss::game::field {
namespace {

constexpr int kStatusCode = 0xBB2;   // page 6's code overlay (at 0x8007e000)

const char* status_text(u32 address) {
  const u8* p = menu::page_table(kStatusCode, address);
  return p ? reinterpret_cast<const char*>(p) : "";
}
u8 status_byte(u32 address) {
  const u8* p = menu::page_table(kStatusCode, address);
  return p ? *p : 0;
}
s16 status_s16(u32 address) {
  const u8* p = menu::page_table(kStatusCode, address);
  if (!p) return 0;
  s16 v;
  std::memcpy(&v, p, 2);
  return v;
}

bool script_flag(int n) { return ((globals().script_flags[n >> 3] >> (n & 7)) & 1) != 0; }   // 0x80055ecc

std::vector<u8> unpack(const u8* packed) {
  const size_t size = read_u32(packed + 4) + 0x10;
  auto out = disc::slz::unpack_block(std::span<const u8>(packed, size), 0, 0, nullptr);
  return out ? std::move(*out) : std::vector<u8>{};
}

bool item_data_resident() { return !menu::globals().item_data.empty(); }   // 0x80074068

s16 rd16s(const u8* p) {
  s16 v;
  std::memcpy(&v, p, 2);
  return v;
}

constexpr psyq::RECT kPortraitArea{0x200, 0, 0xA0, 0xA0};

// ==== The status box (TextBox, 0x90 bytes, vtable 0x8008152c; 0x8007e28c) =================
// The page's one box: two rules under the name and the portrait, which rises from the
// bottom as its rows come in and drops out when the member changes.
class StatusBox final : public menu::TextBox {
 public:
  void draw_background() override;          // [6] 0x8007e31c
  void set_reveal(int rows);                // 0x8007e2d0

 private:
  s32 shown_ = 0;     // +0x84: portrait rows shown
  s32 target_ = 0;    // +0x88: rows to show
  s32 state_ = 0;     // +0x8c: 1 rising, -1 falling, -2 falling then rising, 0 still
};

void StatusBox::set_reveal(int rows) {
  s32 state = -1;
  if (rows != 0) {
    if (state_ == 0) state_ = 1;
    if (state_ != -1) goto target;
    state = -2;
  }
  state_ = state;
target:
  target_ = rows > 0x9F ? 0xA0 : rows - 8;
}

void StatusBox::draw_background() {
  draw_gradient_line(5, 0x14, 0x98, 0x14, 0xFFFFFF, 0x404040);
  draw_gradient_line(0x30, 0x24, 0x98, 0x24, 0xFFFFFF, 0x404040);
  if (state_ > 0) {
    if (shown_ < target_) {
      shown_ += 4;
      if (shown_ > 0xA0) {
        shown_ = 0xA0;
        state_ = 0;
      }
    }
  }
  if (state_ < 0) {
    shown_ -= 0x20;
    if (shown_ < 0) {
      shown_ = 0;
      state_ = state_ == -2 ? 1 : 0;
    }
  }
  if (shown_ == 0) return;
  auto* q = menu::prim_alloc<psyq::POLY_FT4>(ctx);
  if (!q) return;
  psyq::setlen(q, 9);
  const u32 c = colour | 0x2C000000u;
  std::memcpy(&q->r0, &c, 4);
  const s16 top = static_cast<s16>(0x24 - (shown_ - 0xA0));
  const u8 v = static_cast<u8>(shown_);
  q->x0 = 0x94, q->y0 = top, q->u0 = 0, q->v0 = 0;
  q->x1 = 0x134, q->y1 = top, q->u1 = 0xA0, q->v1 = 0, q->tpage = 0x108;
  q->x2 = 0x94, q->y2 = 0xC4, q->u2 = 0, q->v2 = v;
  q->x3 = 0x134, q->y3 = 0xC4, q->u3 = 0xA0, q->v3 = v;
  menu::prim_link(ctx, slot - 1, q);
}

// ==== Talents (Dialog, 0x58 bytes, vtable 0x8008156c; 0x8007e000) =========================
// The ten talents (record +0x20 bits) in two columns, the description of the current one
// (when the member has it) under the list.
class Talents final : public menu::Dialog {
 public:
  Talents();
  void on_cursor(int) override { refresh(); }       // [12] 0x8007e0f0
  void on_confirm(int) override { sound = 0; }      // [13] 0x8007e0e8

 private:
  void refresh();                     // 0x8007e110
  s16 choice_ = 0;                    // +0x34
  menu::TextBox* list_ = nullptr;     // +0x50
  menu::TextBox* text_ = nullptr;     // +0x54
};

Talents::Talents() {
  text_ = new menu::TextBox;
  list_ = new menu::TitledBox;
  list_->set_rect_centered(0x100, 0x68, 1);
  text_->set_rect_below(0x100, 0x28, list_, 1);
  text_->move_base(0, -8);
  cursor->add_grid(&choice_, 1, 2, 5, 0, 0x1C, 0x70, 0xE, list_);
  refresh();
}

void Talents::refresh() {
  u32 bits;
  std::memcpy(&bits, party_selected_record() + 0x20, 4);
  bool known = false;
  u32 bit = 1;
  list_->clear();
  list_->set_group(3);
  list_->pen_at(10, 5);
  list_->add_message(0);
  list_->set_group(4);
  for (int row = 0; row < 5; ++row) {
    for (int col = 0; col < 2; ++col) {
      const int i = row * 2 + col;
      list_->pen_at(static_cast<s16>(0x10 + col * 0x70), static_cast<s16>(row * 0xE + 0x18));
      if (bits & bit) {
        if (i == choice_) known = true;
        list_->add_message(static_cast<s16>(i));
      }
      bit <<= 1;
    }
  }
  text_->clear();
  text_->set_group(5);
  text_->pen_reset();
  if (known) text_->add_message_lines(choice_, 1);
}

// ==== Resistances (Dialog, 0x54 bytes, vtable 0x80081484; 0x80080c4c) =====================
// The elements of the equipment (item field 6 bits) and the best status resistance of
// each kind (item fields 0x1c-0x25), as lit or dark icons.
class Resistances final : public menu::Dialog {
 public:
  Resistances();
  void on_confirm(int) override { sound = 0; }   // [13] 0x80080d20

 private:
  void show_elements();      // 0x80080d28
  void show_statuses();      // 0x80080e9c
  menu::TextBox* box_ = nullptr;   // +0x50
};

menu::PolyFT4Raw resist_icon(int i, int palette, u8 v) {
  menu::PolyFT4Raw q{};
  const u8 u0 = static_cast<u8>(i << 4), u1 = static_cast<u8>(0x10 + i * 0x10);
  const u16 clut = static_cast<u16>(((palette + 0x1E0) << 6) | 0x12);
  q.x0 = 0, q.y0 = 0, q.u0 = u0, q.v0 = v, q.clut = clut;
  q.x1 = 0x10, q.y1 = 0, q.u1 = u1, q.v1 = v, q.tpage = 0x16;
  q.x2 = 0, q.y2 = 0x10, q.u2 = u0, q.v2 = static_cast<u8>(v + 0x10);
  q.x3 = 0x10, q.y3 = 0x10, q.u3 = u1, q.v3 = static_cast<u8>(v + 0x10);
  return q;
}

Resistances::Resistances() {
  box_ = new menu::TitledBox;
  box_->set_rect_centered(200, 0x58, 1);
  box_->set_group(8);
  box_->pen_at(10, 5);
  box_->add_message(0);
  box_->set_font_group(menu::item_strings(1), menu::item_strings(0), 1);
  show_elements();
  show_statuses();
}

void Resistances::show_elements() {
  u32 bits = 0;
  for (int slot = 0; slot < 7; ++slot) {
    const s32 item = char_get_equip(slot);
    if (item != 0) bits |= static_cast<u32>(menu::item_data_get(item, 6));
  }
  box_->pen(8, 0x20, 0x10);
  box_->add_message(0);
  box_->pen_move(0x18, -0x10);
  u32 bit = 1;
  for (int i = 0; i < 10; ++i) {
    box_->add_icon(resist_icon(i, (bits & bit) != 0 ? i : 0xF, 0x80));
    box_->pen_move(0x10, -0x10);
    bit <<= 1;
  }
}

void Resistances::show_statuses() {
  s32 best[10] = {};
  for (int slot = 0; slot < 7; ++slot) {
    const s32 item = char_get_equip(slot);
    if (item == 0) continue;
    for (int i = 0; i < 10; ++i) {
      const s32 v = menu::item_data_get(item, i + 0x1C);
      if (best[i] <= v) best[i] = v;
    }
  }
  box_->pen(8, 0x38, 0x10);
  box_->add_message(1);
  box_->pen_move(0x18, -0x10);
  int row = 0, palette = 0xF;   // the original keeps the last values for other levels
  for (int i = 0; i < 10; ++i) {
    const s32 v = best[i];
    if (v == 1) {
      row = 0x20, palette = i;
    } else if (v == 0) {
      row = 0, palette = 0xF;
    } else if (v > 1 && v < 5) {
      row = 0x10, palette = i;
    }
    box_->add_icon(resist_icon(i, palette, static_cast<u8>(row + 0x80)));
    box_->pen_move(0x10, -0x10);
  }
}

// ==== The page (Panel, 200 bytes, vtable 0x800813b4; 0x8007e550) ==========================
class StatusPage final : public menu::Panel {
 public:
  StatusPage();
  ~StatusPage() override;                       // [1] 0x8007e728
  void close() override;                        // [3] 0x8007e9b0
  void on_close() override;                     // [4] 0x8007e7c0
  void frame() override;                        // [9] 0x8007e9fc
  void on_confirm(int n) override;              // [13] 0x8007ef1c
  void on_cancel(int n) override;               // [14] 0x8007eeac
  void on_button2(int n) override;              // [15] 0x8007ef84
  void on_button3(int n) override;              // [16] 0x8007efe8
  void on_member_changed() override { refresh(); }   // [21] 0x8007ee8c
  void draw_member() override;                  // [23] 0x8007f058

 private:
  void refresh();                               // 0x8007f1c8
  void finish_item_data();                      // as the camp page's on_close
  void stream_portrait(const u8* data, const u8* at);   // 0x8007ed4c
  void add_equipment(int slot);                 // 0x80080b54

  s32 portrait_state_ = 0;            // +0x98: -1 the item data is loading, 0 loading the portrait, 1 shown
  s16 portrait_ = 0;                  // +0x9c: its load handle
  StatusBox* box_ = nullptr;          // +0xa0
  std::vector<u8> saved_vram_;        // +0xa4: (0x200, 0) 0xa0 x 0xa0
  size_t streamed_ = 0;               // +0xa8: bytes of the portrait uploaded (0 none yet)
  menu::BgLoadClient client_;         // +0xac
  s32 item_sizes_[4] = {};            // +0xb4
  std::vector<u8> item_buffer_;       // +0xc4
};

StatusPage::StatusPage() {
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
  help_flags = g.session->single_member != 0 ? 0x506 : 0x1507;
  create_help();
  build_members();
  box_ = new StatusBox;
  box_->set_rect(0, 0, 0x138, 200, 0);
  portrait_state_ = item_data_resident() ? 0 : -1;
  portrait_ = 0;
  streamed_ = 0;
  saved_vram_.assign(0xC800, 0);
  psyq::StoreImage(&kPortraitArea, saved_vram_.data());
  psyq::ClearImage(&kPortraitArea, 0, 0, 0);
  refresh();
}

StatusPage::~StatusPage() {
  psyq::LoadImage(&kPortraitArea, saved_vram_.data());
  psyq::DrawSync(0);
}

void StatusPage::finish_item_data() {
  menu::MenuGlobals& g = menu::globals();
  const u8* p = item_buffer_.data();
  g.item_data = unpack(p);
  p += item_sizes_[0] + item_sizes_[1];   // entry 3001: native code
  g.session->buffer = unpack(p);
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

void StatusPage::close() {
  if (portrait_ != 0) menu::bgload_cancel(portrait_);   // 0x80039268
  portrait_state_ = 1;
  Panel::close();
}

void StatusPage::on_close() {
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

void StatusPage::frame() {
  if (!item_data_resident() && menu::bgload_owner_done(&client_)) {
    finish_item_data();
    portrait_state_ = 0;
    refresh();
  }
  if (portrait_state_ == 0 && portrait_ != 0) {
    // The portrait goes up as it is read; here the read is done in one go.
    if (menu::bgload_is_done(-1)) {
      const menu::LoadRequest* r = menu::bgload_request(portrait_);
      const u8* data = r->dest;
      const size_t end = static_cast<size_t>(r->sectors) * 0x800;
      while (streamed_ < end) {
        stream_portrait(data, data + streamed_);
        streamed_ += 0x800;
      }
      portrait_state_ = 1;
    }
  }
  Panel::frame();
}

void StatusPage::stream_portrait(const u8* data, const u8* at) {
  // The file is a TIM (+0x10) whose sectors carry 0x10 bytes of header after the first.
  const s32 read = static_cast<s32>(at - data);
  const s32 sectors = read / 0x800;
  const s32 offset = sectors != 0 ? read - 0x14 - sectors * 0x10 : 0;
  s32 y = offset / 0x140;
  const s32 x = (offset % 0x140) / 2;
  const u8* src = at + (sectors == 0 ? 0x24 : 0x10);
  if (x != 0) {
    if (y < 0xA0) menu::upload_image_ref(psyq::RECT{static_cast<s16>(x + 0x200), static_cast<s16>(y), static_cast<s16>(0xA0 - x), 1}, src);
    src += (0xA0 - x) * 2;
    ++y;
  }
  if (y < 0xA0) {
    const s32 rows = y + 8 > 0xA0 ? 0xA0 - y : 8;
    menu::upload_image_ref(psyq::RECT{0x200, static_cast<s16>(y), 0xA0, static_cast<s16>(rows)}, src);
    box_->set_reveal(y + rows);
  }
}

void StatusPage::add_equipment(int slot) {
  box_->add_message(rd16s(party_selected_record() + 0xC + slot * 2));
}

void StatusPage::refresh() {
  const int character = party_selected_char();
  const int handle = menu::bgload_queue(&client_, character + 0x1170, 0, nullptr, 0, 0);
  if (portrait_ != 0) menu::bgload_cancel(portrait_);
  portrait_ = static_cast<s16>(handle);
  streamed_ = 0;
  box_->set_reveal(0);
  if (portrait_state_ == 1) portrait_state_ = 0;
  menu::TextBox* box = box_;
  box->clear();
  if (item_data_resident()) box->set_font_group(menu::item_strings(1), menu::item_strings(0), 3);
  // The equipment slots.
  box->pen(8, 100, 0xE);
  for (int i = 0; i < 9; ++i) {
    if (i == 7) box_->pen(0x28, 0xAA, 0xE);
    const u8 u = status_byte(0x8008165C + i), w = status_byte(0x80081668 + i);
    menu::PolyFT4Raw q{};
    q.x0 = 0, q.y0 = 1, q.u0 = u, q.v0 = 0x58, q.clut = 0x7891;
    q.x1 = w, q.y1 = 1, q.u1 = static_cast<u8>(u + w), q.v1 = 0x58, q.tpage = 0x16;
    q.x2 = 0, q.y2 = 9, q.u2 = u, q.v2 = 0x60;
    q.x3 = w, q.y3 = 9, q.u3 = static_cast<u8>(u + w), q.v3 = 0x60;
    box_->add_icon(q);
  }
  box_->pen_at(0x30, 0x60);
  if (item_data_resident())
    for (int slot = 0; slot < 7; ++slot) add_equipment(slot);
  box_->pen_at(8, 0x20);
  box_->add_party_character(character, 3, 0x1000);
  // (a name string is built here from record +0x24 and a table of katakana names, unused)
  box_->pen(5, 8, 0xC);
  box_->set_font_group(menu::strings_block(1), menu::strings_block(0), 0xD);
  box_->add_message(static_cast<s16>(character - 1));
  box_->pen(0x30, 0x18, 0xE);
  box_->set_font_group(menu::strings_block(1), menu::strings_block(0), 10);
  box_->set_group(0xB);
  int title = party_selected_char();
  if (title == 2) title = script_flag(0x2DC) ? 0xD : 2;
  box_->add_message(static_cast<s16>(title - 1));
  u8* slot = party_selected_slot();
  u8* record = party_selected_record();
  {
    menu::StrBuf sb;
    sb.set(status_text(0x80081284));   // HP / MP
    box_->pen(0x30, 0x28, 8);
    box_->add_string(sb.str());
  }
  box_->pen_at(0x50, 0x28);
  box_->add_party_status(static_cast<s16>(party_get_selected()));
  const s16 level = rd16s(slot + 0x26);
  {
    menu::StrBuf sb;
    sb.set(status_text(0x80081290));   // LV
    sb.put(status_text(0x80081280));   // %3d
    sb.put_int(level);
    box_->pen(0x30, 0x38, 8);
    box_->add_string(sb.str());
  }
  {
    menu::StrBuf sb;
    sb.set(status_text(0x800812A0));   // EXP / NEXT
    box_->pen(0x30, 0x40, 8);
    box_->add_string(sb.str());
  }
  {
    menu::StrBuf sb;
    sb.set(status_text(0x800812B0));   // ""
    sb.put(status_text(0x800812B4));   // %9d
    s32 exp;
    std::memcpy(&exp, slot + 0x10, 4);
    sb.put_int(exp);
    sb.put(status_text(0x800812B8));   // new line
    box_->pen_at(0x50, 0x40);
    box_->add_string(sb.str());
  }
  box_->pen_at(0x50, 0x48);
  {
    const s32 next = exp_to_next_level(level);
    const s32 span = exp_for_level(level) - exp_for_level(level - 1);
    box_->add_level(level, next, static_cast<s16>(span != 0 ? ((span - next) * 100) / span : 0));
  }
  {
    menu::StrBuf sb;
    sb.set(status_text(0x800812BC));   // STR ... GUTS
    box_->pen(0xA0, 8, 9);
    box_->add_string(sb.str());
  }
  {
    menu::StrBuf sb;
    sb.set(status_text(0x800812B0));
    const s16 values[8] = {rd16s(slot + 0x2A), rd16s(slot + 0x30), rd16s(slot + 0x3C), rd16s(slot + 0x36),
                           rd16s(slot + 0x42), rd16s(record + 4), rd16s(record + 0xA), rd16s(slot + 0x4C)};
    for (s16 v : values) {
      sb.put(status_text(0x800812E4));   // %4d
      sb.put_int(v);
      sb.put(status_text(0x800812B8));
    }
    box_->pen_at(0xB8, 8);
    box_->add_string(sb.str());
  }
  {
    menu::StrBuf sb;
    sb.set(status_text(0x800812E8));   // FAVORITE FOOD
    box_->pen_at(0xA0, 0xA8);
    box_->add_string(sb.str());
  }
  box_->pen_at(0xC0, 0xB4);
  if ((record[0x3A] & 2) == 0) {
    box_->set_group(6);
    box_->add_message(0);   // not known yet
  } else if (item_data_resident()) {
    box_->add_item_name(status_s16(0x80081612 + character * 2));
  }
  {
    menu::StrBuf sb;
    sb.set(status_text(0x800812FC));   // ATK AC HIT AVD MAG
    box_->pen(0xE0, 8, 0xB);
    box_->add_string(sb.str());
  }
  {
    menu::StrBuf sb;
    sb.set(status_text(0x800812B0));
    if (item_data_resident()) {
      for (int i = 0; i < 5; ++i) {
        sb.put(status_text(0x80081314));   // %5d
        sb.put_int(stats.attr_total(i));
        sb.put(status_text(0x800812B8));
      }
    }
    box_->pen_at(0x108, 8);
    box_->add_string(sb.str());
  }
}

void StatusPage::on_confirm(int n) {
  if (menu::globals().session->single_member == 0) {
    finish(0);
    Widget::on_confirm(n);
  } else {
    sound = 0;
  }
}

void StatusPage::on_cancel(int n) {
  if (portrait_ != 0) {
    menu::bgload_cancel(portrait_);
    portrait_ = 0;
  }
  streamed_ = 0;
  if (portrait_state_ == 1) portrait_state_ = 0;
  Widget::on_cancel(n);
}

void StatusPage::on_button2(int n) {
  open_child(1, new Talents);
  Widget::on_button2(-n);
}

void StatusPage::on_button3(int n) {
  if (!item_data_resident()) return;
  open_child(1, new Resistances);
  Panel::on_button3(-n);
}

void StatusPage::draw_member() {
  Panel::draw_member();
  if (!member_box) return;
  menu::StrBuf sb;
  sb.put(status_text(0x80081274));   // LV
  sb.put(status_text(0x80081280));   // %3d
  sb.put_int(rd16s(party_selected_slot() + 0x26));
  member_box->add_string(sb.str());
}

}  // namespace

menu::Widget* create_talents_dialog() { return new Talents; }

// ==== Page handler ========================================================================

void menu_page06_update(menu::MenuPageLoader* p) {
  switch (p->substate) {
    case 1:
      p->publish_id();
      p->push(new StatusPage);
      break;
    case 2:
      p->pop();
      break;
    case 3:
      p->goto_page(0xE);
      break;
    default:
      break;
  }
}

}  // namespace ss::game::field
