#include "game/field/menu_options.h"

#include <cstring>
#include <vector>

#include "disc/slz.h"
#include "game/boot/pad.h"
#include "game/boot/sound.h"
#include "game/common/menu_core.h"
#include "game/common/menu_gfx.h"
#include "game/common/menu_scene.h"
#include "game/common/menu_widget.h"
#include "game/common/party.h"
#include "game/field/field.h"
#include "runtime/libgpu.h"
#include "runtime/runtime.h"

namespace ss::game::field {
namespace {

constexpr int kOptionsCode = 0xBC8;   // page 7's code overlay (at 0x8007e000)

const char* options_text(u32 address) {
  const u8* p = menu::page_table(kOptionsCode, address);
  return p ? reinterpret_cast<const char*>(p) : "";
}
s16 options_s16(u32 address) {
  const u8* p = menu::page_table(kOptionsCode, address);
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

// 0x8007ee00: v + delta within 0..count - 1, wrapping; bit 15 (not lit) kept.
s16 cycle(s16 v, int delta, int count) {
  int x = (static_cast<u16>(v) & 0x7FFF) + delta;
  if (x < 0) x += count;
  if (count <= x) x -= count;
  if (static_cast<u16>(v) & 0x8000) x |= 0x8000;
  return static_cast<s16>(x);
}

// 0x8007f5d0 / 0x8007f5fc: config +0x49 to its row and back.
s16 from_config49(int v) { return v == 1 ? 2 : (v == 0 ? 1 : 0); }
u8 to_config49(int v) { return v == 1 ? 0 : (v == 0 ? 2 : (v == 2 ? 1 : 0)); }

// The pad mask -> its icon in the window art row of button icons.
int button_icon(u16 button) {
  switch (button) {
    case 0x20: return 0;
    case 0x40: return 1;
    case 0x10: return 2;
    case 0x80: return 3;
    case 4: return 4;
    case 8: return 5;
    case 1: return 6;
    case 2: return 7;
    default: return 0;
  }
}

class OptionsPage;

// ==== The colour sliders (TextBox, 0x90 bytes, vtable 0x80080834; 0x8007f64c) ============
// Three gradients (red, green, blue) with a white marker at each value.
class ColourBox final : public menu::TextBox {
 public:
  void draw_background() override;   // [6] 0x8007f684
  s32 r = 0, g = 0, b = 0;           // +0x84 / +0x88 / +0x8c
};

void ColourBox::draw_background() {
  const RenderContext* c = ctx;
  const int s = slot + 1;
  auto marker = [&](s32 value, s16 top) {
    auto* l = screen_alloc_prims<psyq::LINE_F2>(c->arena, 1);
    if (!l) return;
    psyq::setlen(l, 3);
    const u32 col = menu::menu_colour_code(0x40000000, colour, 0xFFFFFF);
    std::memcpy(&l->r0, &col, 4);
    s32 v = value * 0x30;
    if (v < 0) v += 0xFF;
    const s16 lx = static_cast<s16>(x + (v >> 8) + 0x10);
    l->x0 = lx, l->y0 = static_cast<s16>(y + top);
    l->x1 = lx, l->y1 = static_cast<s16>(y + top + 8);
    menu::prim_link(c, s, l);
  };
  marker(r, 4);
  marker(g, 0x10);
  marker(b, 0x1C);
  auto gradient = [&](u32 rgb, s16 top) {
    auto* q = screen_alloc_prims<psyq::POLY_G4>(c->arena, 1);
    if (!q) return;
    psyq::setlen(q, 8);
    const u32 c0 = 0x38000000u, c2 = 0;
    std::memcpy(&q->r0, &c0, 4);
    std::memcpy(&q->r1, &rgb, 4);
    std::memcpy(&q->r2, &c2, 4);
    std::memcpy(&q->r3, &rgb, 4);
    q->x0 = static_cast<s16>(x + 0x10), q->y0 = static_cast<s16>(y + top);
    q->x1 = static_cast<s16>(x + 0x40), q->y1 = static_cast<s16>(y + top);
    q->x2 = static_cast<s16>(x + 0x10), q->y2 = static_cast<s16>(y + top + 8);
    q->x3 = static_cast<s16>(x + 0x40), q->y3 = static_cast<s16>(y + top + 8);
    menu::prim_link(c, s, q);
  };
  gradient(0xFF, 4);
  gradient(0xFF00, 0x10);
  gradient(0xFF0000, 0x1C);
}

// ==== Window colour (Dialog, 0x6c bytes, vtable 0x800806ac; 0x8007fc04) ==================
class ColourDialog final : public menu::Dialog {
 public:
  explicit ColourDialog(OptionsPage* page) : page_(page) {}
  void set_corner(int index);          // 0x8007fc44
  void on_left_right(int n) override;  // [19] 0x80080238

 private:
  void refresh();                      // 0x8007fd6c
  OptionsPage* page_ = nullptr;        // +0x08
  s16 choice_ = 0;                     // +0x34
  ColourBox* box_ = nullptr;           // +0x60
  s16 corner_ = 0;                     // +0x64
  s16 r_ = 0, g_ = 0, b_ = 0;          // +0x66 / +0x68 / +0x6a
};

// ==== The page (Panel, 0xec bytes, vtable 0x80080764; 0x8007e000) ========================
class OptionsPage final : public menu::Panel {
 public:
  OptionsPage();
  ~OptionsPage() override;                                  // [1] 0x8007e3cc
  void frame() override;                                    // [9] 0x8007e4fc
  void on_child_done(int id, int state, int code) override; // [10] 0x8007eb14
  void input() override;                                    // [11] 0x8007e898
  void on_cursor(int) override {}                           // [12] 0x8007f5c8
  void on_confirm(int n) override;                          // [13] 0x8007e990
  void on_cancel(int n) override;                           // [14] 0x8007eadc
  void on_left_right(int n) override;                       // [19] 0x8007ec28

  u32 colours[4] = {};                // +0xd8: the window colours being edited
  menu::TextBox* sample_ = nullptr;   // +0xa4: a box in those colours

 private:
  void draw_screen1();                // 0x8007eea8
  void draw_screen2();                // 0x8007f268
  void scroll(int dy);                // 0x8007ee38
  void add_grids(menu::TextBox* box1, menu::TextBox* box2);

  s16 choice_ = 0;                    // +0x34: the highlighted label (0-9)
  menu::TextBox* screen1_ = nullptr;  // +0x9c
  menu::TextBox* screen2_ = nullptr;  // +0xa0
  menu::TextBox* message_ = nullptr;  // +0xa8: the message speed sample
  s32 sample_message_ = 0;            // +0xac
  s32 sample_timer_ = 0;              // +0xb0
  s32 lower_ = 0;                     // +0xb4: screen 2 shown
  s32 offset_ = -1;                   // +0xb8
  s16 group_a_ = 0, group_b_ = 0, group_c_ = 0, group_d_ = 0;   // +0xbc..+0xc2: cursor rows
  s16 values_[10] = {};               // +0xc4: message speed, sound, colour corner, +0x49 row,
                                      // +0x4a, +0x4b, (unused), vibration, analog; bit 15 not lit
  s32 dualshock_ = 0;                 // +0xe8
};

OptionsPage::OptionsPage() {
  menu::MenuGlobals& g = menu::globals();
  FieldGlobals& f = globals();
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
  std::memcpy(colours, f.config + 0x30, sizeof(colours));
  sample_ = new menu::TextBox;
  sample_->set_rect(0xA4, 0x48, 0x50, 0x20, 0);
  message_ = new menu::TextBox;
  message_->set_rect(0x88, 8, 0x50, 0x10, 0);
  message_->set_flags(0x1000);
  screen1_ = new menu::TextBox;
  screen1_->set_rect(0, 0, 0x138, 200, 0);
  screen2_ = new menu::TextBox;
  screen2_->set_rect(0, 0, 0x138, 200, 0);
  cursor = new menu::Cursor;   // a second cursor, drawn above the boxes
  group_a_ = group_b_ = group_c_ = group_d_ = 0;
  add_grids(screen1_, screen2_);
  values_[0] = f.game_state[0x118];
  values_[1] = f.config[0x44];
  values_[2] = 0;
  values_[3] = from_config49(f.config[0x49]);
  values_[4] = f.config[0x4A];
  values_[5] = f.config[0x4B];
  values_[6] = 0;
  values_[7] = f.config[0x46];
  values_[8] = 0;
  dualshock_ = 0;
  draw_screen1();
  draw_screen2();
  sample_message_ = 0;
  sample_timer_ = 0;
  lower_ = 0;
  offset_ = -1;
  scroll(0);
}

void OptionsPage::add_grids(menu::TextBox* box1, menu::TextBox* box2) {
  cursor->add_grid(&group_a_, 0, 1, 3, 0, 0x1A, 0, 0x15, box1);
  cursor->add_grid(&group_b_, 0, 1, 3, 0, 0x74, 0, 0x15, box1);
  cursor->add_grid(&group_c_, 0, 1, 8, 0x78, 0x1A, 0, 0xE, box2);
  cursor->add_grid(&group_d_, 0, 1, 1, 0, 0x90, 0, 0x15, box2);
}

OptionsPage::~OptionsPage() {
  FieldGlobals& f = globals();
  f.game_state[0x118] = static_cast<u8>(values_[0]);
  f.config[0x44] = static_cast<u8>(values_[1]);
  f.config[0x49] = to_config49(static_cast<u16>(values_[3]) & 0x7FFF);
  f.config[0x4A] = static_cast<u8>(values_[4]);
  f.config[0x4B] = static_cast<u8>(values_[5]);
  f.config[0x46] = static_cast<u8>(values_[7]);
  std::memcpy(f.config + 0x30, colours, sizeof(colours));
  pad_enable_vibration(menu::menu_pad(), 0, f.config[0x46] != 0);
  sound_set_output_mode(menu::menu_sound(), f.config[0x44]);
}

void OptionsPage::scroll(int dy) {
  lower_ = dy != 0 ? 1 : 0;
  screen1_->dy = static_cast<s16>(dy);
  sample_->dy = static_cast<s16>(dy);
  message_->dy = static_cast<s16>(dy);
  screen2_->dy = static_cast<s16>(dy + 0xD8);
  if ((dy == 0 || dy == -0xD8) && dy != offset_) offset_ = dy;
}

void OptionsPage::frame() {
  // (0x8007e4fc reads the stick when there is no DualShock, then ignores it; +0xe8 is never
  // set in this overlay, so the analog row never appears.)
  if (dualshock_ != 0) {
    dualshock_ = 0;
    if (group_d_ == 1) group_d_ = 0;
    draw_screen1();
    draw_screen2();
    const s16 active = cursor->active;
    cursor->clear_groups();
    add_grids(nullptr, nullptr);
    cursor->active = active;
  }
  const int group = static_cast<s16>(cursor->active + 1);
  bool sample = false;
  if (group == 2) {
    choice_ = static_cast<s16>(group_b_ + 3);
    scroll(0);
  } else if (group == 1) {
    choice_ = group_a_;
    scroll(0);
    if (group_a_ == 0) {
      sample = true;
    } else {
      message_->clear();
    }
  } else if (group == 3) {
    choice_ = static_cast<s16>(group_c_ + 6);
    scroll(-0xD8);
  } else if (group == 4) {
    choice_ = static_cast<s16>(group_d_ + 7);
    scroll(-0xD8);
  }
  if (sample) {
    // The message speed sample: message ac of group 7, one more each speed + 1 frames.
    menu::TextBox* box = message_;
    box->clear();
    box->set_group(7);
    box->pen_at(0, 0);
    box->add_message(static_cast<s16>(sample_message_));
    ++sample_timer_;
    const int speed = static_cast<u16>(values_[0]) & 0x7FFF;
    if (speed == 0) {
      if (sample_timer_ < 5) {
        sample_message_ = 0;
      } else {
        sample_timer_ = 0;
        sample_message_ = 0xB;
      }
    } else {
      if (speed - 1 < sample_timer_) {
        sample_timer_ = 0;
        ++sample_message_;
      }
      if (sample_message_ > 0xB) sample_message_ = 0;
    }
  }
  for (int i = 0; i < 10; ++i) {
    const u16 v = static_cast<u16>(values_[i]);
    values_[i] = static_cast<s16>(v | 0x8000);
    if (choice_ == i && group != 3 && i != 6) values_[i] = static_cast<s16>(v & 0x7FFF);
  }
  Panel::frame();
}

void OptionsPage::input() {
  Widget::input();
  if (cursor == nullptr || lower_ != 1 || static_cast<s16>(cursor->active + 1) != 3) return;
  const u16 press = static_cast<u16>(pressed);
  if ((press & 0xFF) == 0) return;
  // Assigning the pressed button to the row; the row that had it gets the old one.
  const u16 old = config_get_button(group_c_);
  config_set_button(group_c_, 0);
  for (int i = 0; i < 8; ++i)
    if (config_get_button(i) == press) config_set_button(i, old);
  config_set_button(group_c_, press);
  draw_screen2();
  menu::globals().session->build_button_map();
}

void OptionsPage::on_confirm(int n) {
  if (n == 1 && group_a_ == 2) {
    auto* d = new ColourDialog(this);
    open_child(1, d);
    d->set_corner(values_[2]);
    Widget::on_confirm(1);
    return;
  }
  if (n == 4 && group_d_ == 1) {
    // The vibration test (DualShock only; the row never shows here).
    runtime::Runtime::get().log_once("menu:vibration_test", "menu: not ported yet: Options vibration test");
  }
  sound = 0;
}

void OptionsPage::on_cancel(int n) {
  if (n == 3) {
    sound = 0;   // the button assignment rows take every button
  } else {
    Widget::on_cancel(n);
  }
}

void OptionsPage::on_child_done(int id, int, int) {
  if (id == 2) runtime::Runtime::get().log_once("menu:vibration_test2", "menu: not ported yet: Options vibration calibration");
}

void OptionsPage::on_left_right(int n) {
  const int d = n == 1 ? -1 : 1;
  const int group = static_cast<s16>(cursor->active + 1);
  if (group == 2) {
    if (group_b_ == 0) values_[3] = cycle(values_[3], d, 3);
    else if (group_b_ == 1) values_[4] = cycle(values_[4], d, 2);
    else if (group_b_ == 2) values_[5] = cycle(values_[5], d, 2);
  } else if (group == 1) {
    if (group_a_ == 0) values_[0] = cycle(values_[0], d, 8);
    else if (group_a_ == 1) values_[1] = cycle(values_[1], d, 3);
    else if (group_a_ == 2) values_[2] = cycle(values_[2], d, 4);
  } else if (group == 4) {
    if (group_d_ == 0) values_[7] = cycle(values_[7], d, 2);
    else if (group_d_ == 1) values_[8] = cycle(values_[8], d, 1);
  }
  Widget::on_left_right(-n);
}

void OptionsPage::draw_screen1() {
  menu::TextBox* b = screen1_;
  b->clear();
  b->set_group(4);
  menu::TextRenderer* r = b->text;
  r->set_colour(2);
  b->pen(0x10, 0x16, 0x15);
  b->set_highlight_next(&choice_);
  b->add_message(0);
  b->add_message(1);
  b->add_message(3);
  b->pen(0x10, 0x70, 0x15);
  b->add_message(4);
  b->add_message(5);
  b->add_message(6);
  b->set_group(3);
  b->add_message(0);
  r->set_colour(1);
  b->set_group(5);
  b->pen(0x88, 0x16, 0xE);
  b->add_message(0);
  b->pen_move(0x70, -0xE);
  b->add_message(1);
  b->set_group(6);
  b->add_list_highlight(&values_[0], 8);
  b->pen(0xA8, 0x16, 0xE);
  for (int i = 0; i < 8; ++i) {
    b->add_message(static_cast<s16>(i));
    b->pen_move(10, -0xE);
  }
  auto row = [&](s16 group, s16 y, s16* value, int count, const s16 (&moves)[3][2]) {
    b->set_group(group);
    b->pen_at(0x88, y);
    b->add_list_highlight(value, static_cast<s16>(count));
    for (int i = 0; i < count; ++i) {
      if (i > 0) b->pen_move(moves[i - 1][0], moves[i - 1][1]);
      b->add_message(static_cast<s16>(i));
    }
  };
  row(8, 0x2B, &values_[1], 3, {{0x38, -0xE}, {0x38, -0xE}, {0, 0}});
  row(10, 0x47, &values_[2], 4, {{0x70, -0xE}, {-0x70, 4}, {0x70, -0xE}});
  row(0xB, 0x70, &values_[3], 3, {{0x30, -0xE}, {0x40, -0xE}, {0, 0}});
  row(0xC, 0x85, &values_[4], 2, {{0x50, -0xE}, {0, 0}, {0, 0}});
  row(0xD, 0x9A, &values_[5], 2, {{0x50, -0xE}, {0, 0}, {0, 0}});
}

void OptionsPage::draw_screen2() {
  menu::TextBox* b = screen2_;
  b->clear();
  b->set_group(0xE);
  menu::TextRenderer* r = b->text;
  r->set_colour(2);
  b->pen_at(0x10, 0x16);
  b->add_message(0);
  b->pen(0x10, 0x8C, 0x15);
  b->add_message(1);
  if (dualshock_ != 0) b->add_message(2);
  r->set_colour(1);
  b->set_group(0xF);
  b->pen(0x88, 0x16, 0xE);
  for (int i = 0; i < 8; ++i) b->add_message(static_cast<s16>(i));
  s16 ix = 0xB8, iy = 0x14;
  for (int i = 0; i < 8; ++i) {
    b->pen_at(ix, iy);
    const u8 u0 = static_cast<u8>(button_icon(config_get_button(i)) * 0x10);
    menu::PolyFT4Raw q{};
    q.x0 = 0, q.y0 = 0, q.u0 = u0, q.v0 = 0, q.clut = 0x7990;
    q.x1 = 0x10, q.y1 = 0, q.u1 = static_cast<u8>(u0 + 0x10), q.v1 = 0, q.tpage = 0x16;
    q.x2 = 0, q.y2 = 0x10, q.u2 = u0, q.v2 = 0x10;
    q.x3 = 0x10, q.y3 = 0x10, q.u3 = static_cast<u8>(u0 + 0x10), q.v3 = 0x10;
    b->add_icon(q);
    iy = static_cast<s16>(iy + 0xE);
    ix = static_cast<s16>(ix + 0xE);
  }
  b->set_group(0x10);
  b->pen_at(0x88, 0x8C);
  b->add_list_highlight(&values_[7], 2);
  b->add_message(0);
  b->pen_move(0x40, -0xE);
  b->add_message(1);
  if (dualshock_ != 0) {
    b->set_group(0x11);
    b->pen_at(0x88, 0xA1);
    b->add_list_highlight(&values_[8], 1);
    b->add_message(0);
  }
}

// ==== ColourDialog =========================================================================

void ColourDialog::set_corner(int index) {
  corner_ = static_cast<s16>(index);
  const s16 bx = options_s16(0x80080874 + corner_ * 4), by = options_s16(0x80080876 + corner_ * 4);
  box_ = new ColourBox;
  box_->set_rect(bx, by, 0x30, 0x30, 0);
  box_->set_flags(0x1000);
  const u32 c = page_->colours[corner_];
  r_ = static_cast<s16>(c & 0xFF);
  g_ = static_cast<s16>(c >> 8 & 0xFF);
  b_ = static_cast<s16>(c >> 16 & 0xFF);
  cursor->add_grid(&choice_, 0, 1, 3, 0, 6, 0, 0xC, box_);
  refresh();
}

void ColourDialog::refresh() {
  menu::TextBox* b = box_;
  b->clear();
  b->pen_at(0, 0);
  menu::PolyFT4Raw q{};
  q.x0 = 0, q.y0 = 0, q.u0 = 0, q.v0 = 0, q.clut = 0x7813;
  q.x1 = 0x60, q.y1 = 0, q.u1 = 0x60, q.v1 = 0, q.tpage = 5;
  q.x2 = 0, q.y2 = 0x30, q.u2 = 0, q.v2 = 0x30;
  q.x3 = 0x60, q.y3 = 0x30, q.u3 = 0x60, q.v3 = 0x30;
  b->add_icon(q);
  b->pen(0x45, 4, 0xC);
  menu::StrBuf sb;
  sb.set(options_text(0x8008054C));   // ""
  for (s16 v : {r_, g_, b_}) {
    sb.put(options_text(0x80080550));   // %3d
    sb.put_int(v);
    sb.put(options_text(0x80080554));   // new line
  }
  b->add_string(sb.str());   // (0x800db788 points at the StrBuf instead of copying)
  page_->colours[corner_] = static_cast<u32>(r_) | static_cast<u32>(g_) << 8 | static_cast<u32>(b_) << 16;
  page_->sample_->set_colours(page_->colours);
  box_->r = r_, box_->g = g_, box_->b = b_;
}

void ColourDialog::on_left_right(int n) {
  const s16 d = n == 1 ? -1 : 1;
  if (choice_ == 0) {
    r_ = static_cast<s16>((r_ + d) & 0xFF);
  } else if (choice_ == 1) {
    g_ = static_cast<s16>((g_ + d) & 0xFF);
  } else if (choice_ == 2) {
    b_ = static_cast<s16>((b_ + d) & 0xFF);
  }
  refresh();
  Widget::on_left_right(n);
}

}  // namespace

// ==== Page handler ========================================================================

void menu_page07_update(menu::MenuPageLoader* p) {
  switch (p->substate) {
    case 1:
      p->publish_id();
      p->push(new OptionsPage);
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
