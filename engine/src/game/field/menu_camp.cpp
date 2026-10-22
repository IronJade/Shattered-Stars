#include "game/field/menu_camp.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <tuple>

#include "disc/slz.h"
#include "game/boot/pad.h"
#include "game/boot/sound.h"
#include "game/common/menu_core.h"
#include "game/common/menu_gfx.h"
#include "game/common/menu_save.h"
#include "game/common/menu_scene.h"
#include "game/common/menu_widget.h"
#include "game/common/party.h"
#include "game/field/actor_base.h"
#include "game/field/actors.h"
#include "game/field/bg.h"
#include "game/field/callways.h"
#include "game/field/fade.h"
#include "game/field/field_world.h"
#include "game/field/field.h"
#include "game/field/load_queue.h"
#include "game/field/map.h"
#include "game/field/menu_shop.h"
#include "game/field/screen.h"
#include "game/field/script_vm.h"
#include "game/field/worldmap.h"
#include "runtime/libgpu.h"
#include "runtime/runtime.h"

namespace ss::game::field {
namespace {

MenuLauncher* g_launcher = nullptr;  // 0x800752d4
s16 g_menu_request = 0;              // 0x80076196
s16 g_menu_param = 0;                // 0x8007619a
u16 g_option_a = 0;                  // 0x80074628
u16 g_option_b = 0;                  // 0x8007462a
s32 g_gauge_clock = -1;              // 0x80076310: when the gauge's five minutes started

constexpr int kCampPageCode = 0xBA6;   // page 1 (loaded by the launcher at 0x8007e000)

s32 rd32(const u8* p) {
  s32 v;
  std::memcpy(&v, p, 4);
  return v;
}
void wr32(u8* p, s32 v) { std::memcpy(p, &v, 4); }
u16 rd16(const u8* p) {
  u16 v;
  std::memcpy(&v, p, 2);
  return v;
}
void wr16(u8* p, u16 v) { std::memcpy(p, &v, 2); }

// A string constant of the camp page's code (overlay 0xba6).
const char* camp_text(u32 address) {
  const u8* p = menu::page_table(kCampPageCode, address);
  return p ? reinterpret_cast<const char*>(p) : "";
}

// Block 'index' / word 'index' of a pack (field 0x80033530 / 0x800334ec).
const u8* pack_block(const u8* pack, int index) { return pack + read_u32(pack + index * 4); }
u32 pack_word(const u8* pack, int index) { return read_u32(pack + index * 4); }

std::vector<u8> unpack(const u8* packed) {
  const size_t size = read_u32(packed + 4) + 0x10;
  auto out = disc::slz::unpack_block(std::span<const u8>(packed, size), 0, 0, nullptr);
  return out ? std::move(*out) : std::vector<u8>{};
}

// A TIM's pixels and CLUT rows to VRAM through the deferred upload queue, which frees
// the TIM afterwards.
void upload_tim(std::vector<u8> tim, const psyq::RECT& pixels, const psyq::RECT& cluts) {
  if (tim.size() < 0x220) return;
  menu::upload_image_ref(pixels, tim.data() + 0x220);
  menu::upload_image_ref(cluts, tim.data() + 0x14);
  menu::upload_free_later(std::move(tim));
}

// A textured quad of the page's icon sheet, as the page's code builds them on the stack.
menu::PolyFT4Raw quad(s16 x0, s16 y0, u8 u0, u8 v0, s16 x1, s16 y1, u8 u1, u8 v1, s16 x2, s16 y2, u8 u2, u8 v2, s16 x3,
                      s16 y3, u8 u3, u8 v3, u16 clut, u16 tpage) {
  menu::PolyFT4Raw q{};
  q.x0 = x0, q.y0 = y0, q.u0 = u0, q.v0 = v0, q.clut = clut;
  q.x1 = x1, q.y1 = y1, q.u1 = u1, q.v1 = v1, q.tpage = tpage;
  q.x2 = x2, q.y2 = y2, q.u2 = u2, q.v2 = v2;
  q.x3 = x3, q.y3 = y3, q.u3 = u3, q.v3 = v3;
  return q;
}

// ==== Camp menu, page 1 (overlay 0xba6; 0xec bytes, vtable 0x8008034c) ================
// The command column (Specialty, Items, Equipment, Skills, Status, Options, Combat,
// Data) beside the party's boxes and the play time / money / battles strip. Specialty,
// Equipment, Skills and Status first pick a member; Combat and Data open a submenu.

class CampMenu final : public menu::Panel {
 public:
  CampMenu();                                          // 0x8007e000
  ~CampMenu() override;                                // 0x8007ead0
  void on_close() override;                            // [4] 0x8007eb30
  void frame() override;                               // [9] 0x8007ed74
  void on_child_done(int id, int state, int code) override;  // [10] 0x8007fa58
  void on_confirm(int n) override;                     // [13] 0x8007f7c4
  void on_cancel(int n) override;                      // [14] 0x8007f9ac

 private:
  void update_time();                                  // 0x8007ee2c
  void select_member();                                // 0x8007ed28

  s16 command_ = 0;                        // +0x34
  menu::TextBox* members_[8] = {};         // +0x98
  menu::TextBox* commands_ = nullptr;      // +0xb8
  menu::TextBox* time_ = nullptr;          // +0xbc
  menu::Cursor* saved_cursor_ = nullptr;   // +0xc0
  s16 mode_ = 0;                           // +0xc4: 0 commands, 1 picking a member
  u32 last_clock_ = 0;                     // +0xc8
  s16 blink_ = 0;                          // +0xcc
  menu::BgLoadClient items_client_;        // +0xd0: the item data (entries 3000-3003)
  s32 item_sizes_[4] = {};                 // +0xd8
  std::vector<u8> item_buffer_;            // +0xe8
};

// Combat (0x54 bytes, vtable 0x80080294; ctor 0x8007fb54): three choices -> pages 8-10.
class CombatMenu final : public menu::Dialog {
 public:
  CombatMenu();
  void on_confirm(int n) override;                     // [13] 0x8007fc64
  s16 choice = 0;                                      // +0x34
  menu::TextBox* box = nullptr;                        // +0x50
};

// Data (0x54 bytes, vtable 0x800801ec; ctor 0x8007fcac): Save (greyed unless saving is
// allowed here) and Load -> page 0x11.
class DataMenu final : public menu::Dialog {
 public:
  DataMenu();
  void on_confirm(int n) override;                     // [13] 0x8007fdf0
  s16 choice = 0;                                      // +0x34
  menu::TextBox* box = nullptr;                        // +0x50
};

CampMenu::CampMenu() {
  menu::MenuGlobals& g = menu::globals();
  menu::MenuSession* session = g.session;
  stats.all();
  menu::strings_set(g.data.data());
  // The page's icon sheet (block 3 of the page data): pixels at (0x140, 0), 16 CLUTs at
  // (0x130, 0x1e0); the data block then shrinks to word 2 bytes.
  {
    std::vector<u8> tim = unpack(menu::strings_block(3));
    const u32 keep = menu::strings_word(2);
    upload_tim(std::move(tim), psyq::RECT{0x140, 0, 0x40, 0x40}, psyq::RECT{0x130, 0x1E0, 0x10, 0x10});
    if (keep < g.data.size()) g.data.resize(keep);
  }
  menu::TextRenderer::set_default_font(menu::strings_block(1), menu::strings_block(0));
  if (g.item_data.empty()) {
    // The item data for the other pages loads while this one is open (on_close).
    s32 total = 0;
    for (int i = 0; i < 4; ++i) {
      item_sizes_[i] = menu::menu_loader()->entry_size(3000 + i);
      total += item_sizes_[i];
    }
    item_buffer_.assign(static_cast<size_t>(total), 0);
    menu::bgload_queue_bytes(&items_client_, 3000, item_buffer_.data(), total);
  }
  commands_ = new menu::TextBox;
  time_ = new menu::TextBox;
  for (int i = 7; i >= 0; --i) members_[i] = new menu::TextBox;
  mode_ = 0;
  commands_->set_rect(0, 0, 0x68, 0xB8, 0);
  commands_->pen_at(0, 0);
  time_->set_rect(0, 0xC0, 0x138, 0x19, 0);
  time_->slot = static_cast<u16>(time_->slot + 2);
  // The game's logo down the column's left edge (the sheet's 192x16 strip turned on its side).
  commands_->add_icon(quad(0, 0xB7, 0, 0, 0, -9, 0xC0, 0, 0x10, 0xB7, 0, 0x10, 0x10, -9, 0xC0, 0x10, 0x7813, 5));
  // The eight command icons (20x20, a column of the sheet each).
  for (int i = 0; i < 8; ++i) {
    commands_->pen_at(0, 0);
    const s16 y0 = static_cast<s16>(8 + 0x15 * i), y1 = static_cast<s16>(0x1C + 0x15 * i);
    const u8 u0 = static_cast<u8>(0x18 * i), u1 = static_cast<u8>(0x14 + 0x18 * i);
    commands_->add_icon(quad(0x12, y0, u0, 0x10, 0x26, y0, u1, 0x10, 0x12, y1, u0, 0x24, 0x26, y1, u1, 0x24, 0x7853, 5));
  }
  // Arrows after Combat and Data (they open submenus).
  for (int i = 0; i < 2; ++i) {
    commands_->pen_at(0, 0);
    const s16 y0 = static_cast<s16>(0x88 + 0x15 * i), y1 = static_cast<s16>(0x98 + 0x15 * i);
    commands_->add_icon(quad(0x5A, y0, 0xE0, 0x30, 0x62, y0, 0xE8, 0x30, 0x5A, y1, 0xE0, 0x40, 0x62, y1, 0xE8, 0x40, 0x7850, 0x16));
  }
  for (int i = 0; i < 8; ++i) members_[i]->set_rect(0x58, 8 + 0x2E * i, 0xE0, 0x2E, 0);
  const int selected = party_get_selected();
  for (int i = 0; i < 8; ++i) {
    party_select(i);
    if (party_selected_char() == 0) continue;
    menu::TextBox* box = members_[i];
    const u8* slot = party_selected_slot();
    box->pen_at(0x10, -3);
    box->add_party_character(static_cast<s16>(rd16(slot)), 3, 0x1000);
    // "HP" / "MP" labels, "LV n NEXT" (ASCII with colour codes).
    box->pen_at(0x38, 10);
    const s16 level = static_cast<s16>(rd16(slot + 0x26));
    {
      menu::StrBuf sb;
      sb.set(camp_text(0x80080084));
      for (u32 a : {0x80080088u, 0x80080098u, 0x8008009Cu, 0x800800B0u, 0x800800B4u, 0x800800BCu}) sb.put(camp_text(a));
      sb.put_int(level);
      sb.put(camp_text(0x800800C0));
      box->add_string(sb.str());
    }
    box->pen_at(0x38, 10);
    {
      menu::StrBuf sb;
      sb.set(camp_text(0x800800CC));
      sb.put(reinterpret_cast<const char*>(party_selected_record() + 0x24));
      box->add_string(sb.str());
    }
    box->pen_at(0x90, 10);
    box->add_party_status(static_cast<s16>(i));
    box->pen_at(0x90, 0x1E);
    const s32 next = exp_to_next_level(level);
    const s32 span = exp_for_level(level) - exp_for_level(level - 1);
    const s32 percent = span != 0 ? ((span - next) * 100) / span : 0;
    box->add_level(level, next, static_cast<s16>(percent));
  }
  party_select(selected);
  command_ = session->command;
  cursor->add_grid(&command_, 1, 1, 8, 0xE, 0x12, 0, 0x15, commands_);
  commands_->set_group(3);
  commands_->pen(0x28, 0xC, 0x15);
  for (int i = 0; i < 8; ++i) commands_->add_message(static_cast<s16>(i));
  commands_->set_highlight(0, &command_);
}

CampMenu::~CampMenu() = default;

void CampMenu::select_member() { party_select(menu::globals().session->member); }

void CampMenu::on_close() {
  menu::MenuGlobals& g = menu::globals();
  if (g.item_data.empty()) {
    if (!menu::bgload_owner_done(&items_client_)) {
      menu::bgload_cancel_owner(&items_client_);
    } else {
      // 3000 the item pack, 3001 code for the other pages (0x80088000; native here),
      // 3002 the item records the inventory points at (+0x820), 3003 the resident 0xbbb.
      const u8* p = item_buffer_.data();
      g.item_data = unpack(p);
      p += item_sizes_[0] + item_sizes_[1];
      g.session->buffer = unpack(p);
      g.item_table = g.session->buffer.data();
      p += item_sizes_[2];
      menu::entry_cache_load_0bbb(p);
      g.session->cached_0bbb = 1;
      item_buffer_.clear();
      // The item icons (block 7 of the pack) at (0x180, 0x180), CLUTs at (0x120, 0x1e0).
      if (!g.item_data.empty()) {
        std::vector<u8> tim = unpack(pack_block(g.item_data.data(), 7));
        const u32 keep = pack_word(g.item_data.data(), 6);
        upload_tim(std::move(tim), psyq::RECT{0x180, 0x180, 0x40, 0x40}, psyq::RECT{0x120, 0x1E0, 0x10, 0x10});
        if (keep < g.item_data.size()) g.item_data.resize(keep);
      }
    }
  }
  Widget::on_close();
}

void CampMenu::frame() {
  // The member boxes follow the member list's scroll.
  const s32 scroll = menu::globals().session->member_scroll * -0x2E;
  for (menu::TextBox* box : members_) box->dy = static_cast<s16>((scroll < 0 ? scroll + 3 : scroll) >> 2);
  update_time();
  Panel::frame();
}

void CampMenu::update_time() {
  u8* config = menu::globals().config;
  time_->clear();
  const u32 now = clock_seconds();
  if (last_clock_ != now) {
    last_clock_ = now;
    wr32(config + 0x10, static_cast<s32>(now / 60));
    blink_ = 0;
  }
  ++blink_;
  const s32 minutes = rd32(config + 0x10);
  s32 hours = minutes / 60;
  if (hours > 999) hours = 999;
  time_->pen_at(8, 8);
  menu::StrBuf sb;
  sb.put(camp_text(0x800800D0));
  sb.put(camp_text(0x800800BC));
  sb.put_int(hours);
  // The colon blinks: blank for the first 30 frames of each second.
  sb.put(camp_text(((blink_ / 0x1E) & 0xFFFF) == 0 ? 0x8008010C : 0x800800DC));
  sb.put(camp_text(0x800800E0));
  sb.put_int(minutes % 60);
  sb.put(camp_text(0x800800E8));
  sb.put(camp_text(0x800800F4));
  sb.put_int(rd32(config + 0x18));
  sb.put(camp_text(0x800800F8));
  sb.put(camp_text(0x80080108));
  sb.put_int(rd32(config + 0x14));
  time_->add_string(sb.str());
}

void CampMenu::on_confirm(int n) {
  menu::MenuSession* session = menu::globals().session;
  session->command = static_cast<s8>(command_);
  if (mode_ == 0) {
    switch (command_) {
      case 0:
      case 2:
      case 3:
      case 4:
        // Pick a member first: a second hand cursor on the party list.
        commands_->slot = static_cast<u16>(commands_->slot - 2);
        saved_cursor_ = push_cursor();
        cursor->add_list(&session->member, &session->member_scroll, 8, 4, 0x60, 0x20, 0x2E);
        ++mode_;
        break;
      case 6:
        open_child(2, new CombatMenu);
        break;
      case 7:
        open_child(1, new DataMenu);
        break;
      default:
        finish(command_);
        select_member();
        break;
    }
  } else {
    bool ok = true;
    const int selected = party_get_selected();
    party_select(session->member);
    if (party_selected_char() == 0) {
      ok = false;
    } else if (command_ != 4) {
      ok = (party_selected_slot()[2] & 7) == 0;  // not for the incapacitated (Status is)
    }
    party_select(selected);
    if (!ok) return;
    finish(command_);
    select_member();
  }
  Widget::on_confirm(n);
}

void CampMenu::on_cancel(int n) {
  if (mode_ == 0) {
    cancel();
  } else if (mode_ == 1) {
    commands_->slot = static_cast<u16>(commands_->slot + 2);
    --mode_;
    pop_cursor(saved_cursor_);
  }
  Widget::on_cancel(-n);
}

void CampMenu::on_child_done(int id, int state, int code) {
  menu::MenuSession* session = menu::globals().session;
  if (id == 1 && state == 3) {
    session->load_mode = static_cast<s8>(code);  // Data: 0 save, 1 load
    finish(6);
    select_member();
  }
  if (id == 2 && state == 3) {
    finish(static_cast<s16>(code + 7));
    select_member();
  }
  if (id == 3 && state == 3) {
    finish(static_cast<s16>(code + 10));
    select_member();
  }
}

CombatMenu::CombatMenu() {
  box = new menu::TextBox;
  box->set_rect(0x62, 0x8A, 0x80, 0x3A, 1);
  box->set_group(4);
  box->set_highlight_next(&choice);
  box->pen(0x10, 8, 0xE);
  for (int i = 0; i < 3; ++i) box->add_message(static_cast<s16>(i));
  choice = 0;
  cursor->add_grid(&choice, 1, 1, 3, 0, 0xC, 0, 0xE, box);
}

void CombatMenu::on_confirm(int n) {
  finish(choice);
  Widget::on_confirm(n);
}

DataMenu::DataMenu() {
  box = new menu::TextBox;
  box->set_rect(0x62, 0x9F, 0x50, 0x2C, 1);
  box->set_group(5);
  box->set_highlight_next(&choice);
  box->pen(0x10, 8, 0xE);
  box->text_renderer()->set_colour((field_get_option_a() & 1) != 0 ? 1 : 4);
  box->add_message(0);
  box->text_renderer()->set_colour(1);
  box->add_message(1);
  cursor->add_grid(&choice, 0, 1, 2, 0, 0xC, 0, 0xE, box);
}

void DataMenu::on_confirm(int n) {
  if (choice == 0 && (field_get_option_a() & 1) == 0) return;  // no saving here
  finish(choice);
  Widget::on_confirm(n);
}

// ==== Page handlers (field 0x80035374 / 0x800358e8) ========================================

void page01_update(menu::MenuPageLoader* p) {
  switch (p->substate) {
    case 1:
      p->publish_id();
      p->push(new CampMenu);
      break;
    case 2:
      p->pop();
      break;
    case 3: {
      static const s16 pages[13] = {2, 3, 4, 5, 6, 7, 0x11, 8, 9, 10, 0xB, 0xC, 0xD};
      if (p->choice >= 0 && p->choice < 13) p->goto_page(pages[p->choice]);
      break;
    }
    default:
      break;
  }
}

void page11_update(menu::MenuPageLoader* p) {
  switch (p->substate) {
    case 1:
      p->publish_id();
      p->push(new menu::SaveMenu);
      break;
    case 3:
      // Saved or loaded: the camp menu closes with it.
      p->depth = static_cast<s16>(p->depth - 1);
      p->pop();
      break;
    case 2:
      p->pop();
      break;
    default:
      break;
  }
}

// ==== The query page's gauge (0x80034514-0x80034c00) ==========================================
// While a script's minigame runs (script A 0x35: the 0x801f9800 overlay object and script
// flag 20), the menu button opens one member's query page (request 3, page 0xf) and the
// field keeps this gauge on the page: "TIME", five minutes counting down from
// g_gauge_clock (set by the first draw once script flag 19 is on), and "PRESS", script
// variable +0x38 as a percentage, each with a bar 80 pixels wide. Text is the 8x8 font of
// the window art (tpage 0x16).

struct QueryGauge {     // 0x10 bytes, vtable 0x800719e0 (ctor 0x80034514, dtor 0x80034528)
  s32 x = 0x10;         // +4
  s32 y = 0xA4;         // +8
};
QueryGauge* g_query_gauge = nullptr;   // 0x80074090
bool g_query_gauge_on = false;         // 0x8007408c

s32 gauge_press() { return rd32(globals().field_flags + 0x38); }                         // [1] 0x8003455c
bool gauge_running() { return ((globals().script_flags[2] >> 3) & 1) != 0; }            // [2] 0x80034574: flag 19

// 0x80034a8c: seconds left of the five minutes (all of them until the clock starts).
s32 gauge_seconds_left() {
  if (g_gauge_clock == -1) return 300;
  const s32 left = 300 - static_cast<s32>(clock_seconds() - static_cast<u32>(g_gauge_clock));
  return left < 0 ? 0 : left;
}

// 0x80034c08: 8x8 sprites of the window art's font (u = (c - 0x20) % 32 * 8, v = 0xc8 +
// (c - 0x20) / 32 * 8; CLUT row 0x1e0 + palette at x 0x110) into OT entry 'slot'.
void debug_text(const RenderContext* ctx, int slot, s16 x, s16 y, const char* text, int palette) {
  const int n = menu::str_len(text);
  auto* sp = screen_alloc_prims<psyq::SPRT>(ctx->arena, static_cast<size_t>(n));
  if (!sp) return;
  const u16 clut = static_cast<u16>(((palette + 0x1E0) << 6) | 0x11);
  for (int i = 0; i < n; ++i, ++sp) {
    const u8 c = static_cast<u8>(static_cast<u8>(text[i]) - 0x20);
    psyq::setlen(sp, 4);
    sp->r0 = sp->g0 = sp->b0 = 0x80;
    sp->code = 0x64;
    sp->x0 = x;
    sp->y0 = y;
    sp->u0 = static_cast<u8>((c & 0x1F) << 3);
    sp->v0 = static_cast<u8>(((c >> 5) << 3) - 0x38);
    sp->clut = clut;
    sp->w = sp->h = 8;
    menu::prim_link(ctx, slot, sp);
    x = static_cast<s16>(x + 8);
  }
  auto* m = screen_alloc_prims<psyq::DR_MODE>(ctx->arena, 1);
  if (!m) return;
  const psyq::RECT tw{};
  psyq::SetDrawMode(m, 0, 1, 0x16, &tw);
  menu::prim_link(ctx, slot, m);
}

// A bar of the gauge: POLY_F4 (semi-transparent) and the blend mode it is drawn with.
void gauge_bar(const RenderContext* ctx, u32 colour, s16 x0, s16 x1, s16 y, int tpage) {
  auto* q = screen_alloc_prims<psyq::POLY_F4>(ctx->arena, 1);
  auto* m = screen_alloc_prims<psyq::DR_MODE>(ctx->arena, 1);
  if (!q || !m) return;
  psyq::setlen(q, 5);
  std::memcpy(&q->r0, &colour, 4);
  q->x0 = q->x2 = x0;
  q->x1 = q->x3 = x1;
  q->y0 = q->y1 = y;
  q->y2 = q->y3 = static_cast<s16>(y + 0xC);
  menu::prim_link(ctx, 8, q);
  const psyq::RECT tw{};
  psyq::SetDrawMode(m, 1, 0, tpage, &tw);
  menu::prim_link(ctx, 8, m);
}

// 0x80034594
void query_gauge_draw(const QueryGauge& o, const RenderContext* ctx) {
  if (gauge_running() && g_gauge_clock == -1) g_gauge_clock = static_cast<s32>(clock_seconds());
  const s32 left = gauge_seconds_left();
  const s16 x = static_cast<s16>(o.x), y = static_cast<s16>(o.y);
  debug_text(ctx, 8, x, static_cast<s16>(y + 2), "TIME", 2);
  debug_text(ctx, 8, x, static_cast<s16>(y + 0x12), "PRESS", 2);
  for (int row = 0; row < 2; ++row) {
    char text[16];
    s32 length;
    if (row == 0) {
      length = left * 80 / 300;
      const s32 minutes = left / 60;
      char seconds[8];
      menu::format_decimal(text, minutes, 0, ' ');
      menu::str_cat(text, ":");
      menu::format_decimal(seconds, left - minutes * 60, 2, '0');
      menu::str_cat(text, seconds);
      debug_text(ctx, 8, static_cast<s16>(x + 0x42), static_cast<s16>(y + 2), text, 1);
    } else {
      length = static_cast<s32>(static_cast<u32>(gauge_press()) * 80u) / 100;
      menu::format_decimal(text, gauge_press(), 0, ' ');
      debug_text(ctx, 8, static_cast<s16>(x + 0x4A), static_cast<s16>(y + 0x12), text, 1);
    }
    const s16 top = static_cast<s16>(y + row * 16);
    gauge_bar(ctx, 0x2A8020FF, static_cast<s16>(x + 0x2A), static_cast<s16>(x + 0x2A + length), top, 0x20);
    gauge_bar(ctx, 0x2A507090, static_cast<s16>(x + 0x2A), static_cast<s16>(x + 0x7A), top, 0);
  }
  auto* m = screen_alloc_prims<psyq::DR_MODE>(ctx->arena, 1);
  if (!m) return;
  const psyq::RECT tw{};
  psyq::SetDrawMode(m, 1, 0, 0x20, &tw);
  menu::prim_link(ctx, 8, m);
}

// 0x80034afc / 0x80034b6c: the menu scene creates the gauge for a query session.
void query_gauge_begin(const menu::MenuSession& session) {
  g_query_gauge = nullptr;
  g_query_gauge_on = false;
  if (session.field_query == 0) return;
  g_query_gauge_on = true;
  g_query_gauge = new QueryGauge;
}
void query_gauge_end() {
  delete g_query_gauge;
  g_query_gauge = nullptr;
}

// 0x80034ba0: the Panel frame hook, while a window has the focus.
void query_gauge_hook(const RenderContext* ctx) {
  menu::MenuScene* scene = menu::globals().scene;
  if (scene == nullptr || scene->windows == nullptr || scene->windows->focus() == nullptr) return;
  if (g_query_gauge_on && g_query_gauge != nullptr) query_gauge_draw(*g_query_gauge, ctx);
}

// ==== Field host ===========================================================================

menu::EntryCacheSlotRef field_cache_slot(int index) {
  SpritePackSlot& s = sprite_pack_slots()[index];
  return {&s.data, &s.id};
}

void field_menu_frame() {
  // field 0x800300dc: the Callways task keeps ticking (songs, money per second).
  if (Callways* c = callways()) c->update();
}

// The library and the field's page handlers for one menu run.
struct FieldMenuHost {
  FieldMenuHost() {
    menu::set_host(menu::Host::Field);
    menu::set_entry_cache_table(&field_cache_slot);
    menu::set_menu_frame_hook(&field_menu_frame);
    menu::set_page_handler(1, &page01_update);
    menu::set_page_handler(0xB, &menu_page0b_update);
    menu::set_page_handler(0x11, &page11_update);
    menu::set_panel_frame_hook(&query_gauge_hook);
  }
  ~FieldMenuHost() {
    menu::set_page_handler(1, nullptr);
    menu::set_page_handler(0xB, nullptr);
    menu::set_page_handler(0x11, nullptr);
    menu::set_menu_frame_hook(nullptr);
    menu::set_panel_frame_hook(nullptr);
    menu::set_entry_cache_table(nullptr);
    menu::set_page_code_entry(0);
    menu::set_host(menu::Host::Title);
  }
};

// The common part of the runners: session set up by 'setup', the scene run to the end.
template <typename Setup>
void run_menu(menu::MenuSession& session, Setup setup, int member = -1) {
  session.start_page = 1;
  setup(session);
  menu::MenuScene scene(nullptr);
  if (member >= 0) party_select(member);  // menu_scene_select_member (0x8002ffbc)
  query_gauge_begin(session);
  scene.run();
  query_gauge_end();
}

}  // namespace

// ==== Launcher ==============================================================================

MenuLauncher* menu_launcher_create() {
  auto* l = new MenuLauncher;
  menu::MenuGlobals& g = menu::globals();
  g.res.clear();
  g.data.clear();
  return l;
}

void menu_launcher_destroy(MenuLauncher* l) { delete l; }

void menu_launcher_start(MenuLauncher* l, int kind) {
  l->state = 1;
  l->buffer.clear();
  switch (kind) {
    case 1: l->entry = 0xBA4; break;
    case 2: l->entry = 0xBA8; break;
    case 3: l->entry = 0xBAC; break;
    case 4: l->entry = 0xBB0; break;
    case 5: l->entry = 0xBB4; break;
    default: break;
  }
}

void menu_launcher_step(MenuLauncher* l) {
  Loader* loader = globals().loader;
  switch (l->state) {
    case 1:
    case 4:
      if (loader->poll() == 1) return;
      ++l->state;
      return;
    case 2:
      l->total = 0;
      for (int i = 0; i < 4; ++i) {
        l->sizes[i] = loader->entry_size(l->entry + i);
        l->total += l->sizes[i];
      }
      l->buffer.assign(static_cast<size_t>(l->total), 0);
      ++l->state;
      return;
    case 3:
      // One read across the four consecutive entries.
      if (loader->select_entry(l->entry) == 0) return;
      loader->start_read(l->buffer.data(), l->total);
      ++l->state;
      return;
    default:
      return;
  }
}

void menu_launcher_finish(MenuLauncher* l) {
  do {
    menu_launcher_step(l);
  } while (l->state != 5);
  menu::MenuGlobals& g = menu::globals();
  size_t at = 0;
  if (auto res = disc::slz::unpack_block(l->buffer, at, 0, nullptr)) g.res = std::move(*res);
  // The library (0xba5 at 0x800d1b28) and the first page's code (at 0x8007e000) are
  // native here; their constants are read from the entries.
  at += static_cast<size_t>(l->sizes[0]);
  at += static_cast<size_t>(l->sizes[1]);
  menu::set_page_code_entry(l->entry + 2);
  at += static_cast<size_t>(l->sizes[2]);
  if (auto data = disc::slz::unpack_block(l->buffer, at, 0, nullptr)) g.data = std::move(*data);
  l->buffer.clear();
  l->buffer.shrink_to_fit();
}

// ==== Runners ===============================================================================

int menu_run_camp(MenuLauncher* l) {
  FieldMenuHost host;
  menu_launcher_finish(l);
  callways_before_menu(callways());
  FieldGlobals& g = globals();
  int result = 0;
  {
    menu::MenuSession session;
    run_menu(session, [](menu::MenuSession& s) { s.start_page = 1; });
    result = session.unknown1b != 0 ? 2 : 0;
    if (static_cast<s16>(rd16(g.game_state + 0x264)) != 0) result = 3;
    if (rd32(g.game_state + 0x294) < 0) {
      wr32(g.game_state + 0x294, -rd32(g.game_state + 0x294));
      result = 4;
    }
    if (session.unknown30 != 0) result = 5;
    if (session.unknown35 != 0) result = 6;
    if (session.result != 0) {
      result = 1;
      wr16(g.game_state + 0x264, 0);
    }
  }
  if (Callways* c = callways()) callways_after_menu(c);
  return result;
}

int menu_run_shop(MenuLauncher* l, s16 shop) {
  FieldMenuHost host;
  menu_launcher_finish(l);
  menu::MenuSession session;
  run_menu(session, [shop](menu::MenuSession& s) {
    s.start_page = 0xB;
    s.arg = shop;
  });
  return session.select_result;
}

void menu_run_member(MenuLauncher* l, int slot) {
  FieldMenuHost host;
  menu_launcher_finish(l);
  menu::MenuSession session;
  run_menu(
      session,
      [](menu::MenuSession& s) {
        s.start_page = 6;
        s.single_member = 1;
      },
      slot);
}

void menu_run_query(MenuLauncher* l, int slot) {
  FieldMenuHost host;
  menu_launcher_finish(l);
  menu::MenuSession session;
  run_menu(
      session,
      [](menu::MenuSession& s) {
        s.start_page = 0xF;
        s.variant = 0xB;
        s.field_query = 1;
      },
      slot);
}

void menu_run_save(MenuLauncher* l) {
  FieldMenuHost host;
  menu_launcher_finish(l);
  menu::MenuSession session;
  run_menu(session, [](menu::MenuSession& s) { s.start_page = 0x11; });
}

// ==== Script helpers ==========================================================================

int menu_member_specialty_level(int slot, int specialty) {
  FieldMenuHost host;
  menu::MenuSession session;
  party_select(slot);
  return party_selected_char() != 0 ? char_specialty_level(specialty) : -1;
}

int menu_super_specialty_6_level() {
  FieldMenuHost host;
  menu::MenuSession session;
  return party_super_specialty_level(6);
}

void menu_member_clear_bond(int slot) {
  FieldMenuHost host;
  menu::MenuSession session;
  party_select(slot);
  if (party_selected_char() != 0) {
    u8* record = party_selected_record();
    record[0x3B] = 0;
    record[0xCE] = 0;
    record[0xCF] = 0;
  }
}

// ==== Requests and options ===================================================================

void field_menu_request(int kind) { g_menu_request = static_cast<s16>(kind); }
void field_menu_set_param(int param) { g_menu_param = static_cast<s16>(param); }
int field_menu_request_pending() { return g_menu_request; }

void field_set_option_a(u16 v) { g_option_a = v; }
void field_set_option_b(u16 v) { g_option_b = v; }
void field_set_gauge_clock(s32 v) { g_gauge_clock = v; }
s32 field_gauge_clock() { return g_gauge_clock; }

u16 field_get_option_a() { return g_option_a; }
u16 field_get_option_b() { return g_option_b; }

// ==== field_update hooks ========================================================================

void field_menu_check(Field* f) {
  FieldGlobals& g = globals();
  if (f->state != 0) return;
  // The save point: sfx 0xc as the leader steps onto it (actor_touches_save_point).
  const s32 was_on = f->on_save_point;
  const Actor* leader = field_actor(g.game_state[0x24]);
  f->on_save_point = leader != nullptr && actor_touches_save_point(leader) ? 1 : 0;
  if (f->on_save_point != 0 && was_on == 0) sound_play_sfx(g.sound, 0xC, 0, 0x7F40);
  if (f->state != 0) return;
  const bool button = (g.pressed[0] & read_u16(g.config + 4)) != 0;
  if (!(button || g_menu_request != 0) || g.field_flags[0x93] != 0) return;
  bool allowed = true;
  if (g_script_vm != nullptr) {
    const s32 vs = g_script_vm->wait_state;
    if (vs == 1 || vs == 3 || vs == 8) allowed = false;
  }
  if ((g.script_flags[1] & 0x10) != 0 || (g.script_flags[1] & 0x40) != 0) allowed = false;
  const bool idle = g_script_vm == nullptr || g_script_vm->active_trigger == -1;
  if (!(allowed && static_cast<s16>(rd16(g.field_flags + 0x10)) != -1 && idle && g.field_flags[0x26] == 0) &&
      g_menu_request == 0)
    return;
  // TODO(port): boot 0x800134d8(0x8004b760), a pad / vibration setting for the menu.
  if (g_menu_request == 0) {
    sound_play_sfx(g.sound, 2, 0, 0x7F40);
    if ((g.script_flags[2] & 0x10) != 0 && g_script_vm != nullptr) {
      // The menu shows one member's query page instead.
      g_menu_request = 3;
      g_menu_param = static_cast<s16>(party_find_slot(g_script_vm, rd32(g.field_flags + 0x3C)));
    }
  }
  fade_push(field_fade(), 3, 0xF, 0, 0);
  load_queue_set_streaming(load_queue(), false);
  preload_save_table();
  if (g.field_flags[0x68] != 0) {
    world_cancel_block_load();
    world().streaming = 0;
  }
  load_queue_reset(load_queue());
  preload_drop_incomplete();
  f->saved_control = static_cast<s16>(rd16(g.field_flags + 0x10));
  wr16(g.field_flags + 0x10, 0xFFFF);
  f->state = 3;
  // The leader's position for the save file (game_state +8..+0x21, whole units).
  const Actor* a = field_actor(g.game_state[0x24]);
  if (g_menu_request != 5 && a != nullptr) {
    u8* gs = g.game_state;
    wr32(gs + 0x08, a->x >> 12);
    wr32(gs + 0x0C, a->y >> 12);
    wr32(gs + 0x10, a->z >> 12);
    wr32(gs + 0x14, a->w);
    wr16(gs + 0x18, static_cast<u16>(a->angle));
    gs[0x21] = static_cast<u8>(a->layer);
    if ((g.script_flags[1] & 0x10) != 0) {
      // In a vehicle: its parking place too.
      if (g.field_flags[0x68] == 1) {
        wr32(gs + 0x26C, rd32(gs + 0x08));
        wr32(gs + 0x28C, rd32(gs + 0x0C));
        wr32(gs + 0x270, rd32(gs + 0x10));
        wr16(gs + 0x27C, gs[0x21]);
      } else {
        wr32(gs + 0x274, rd32(gs + 0x08));
        wr32(gs + 0x290, rd32(gs + 0x0C));
        wr32(gs + 0x278, rd32(gs + 0x10));
        wr16(gs + 0x27E, gs[0x21]);
      }
    }
  }
  // BG vtable +0x1c: the view into field_flags; the animation copy is dropped at once.
  if (g.field_flags[0x68] == 0) {
    if (BgRenderer* bg = bg_renderer()) bg_suspend(bg);
    delete[] bg_take_anim_save();
  } else if (WorldBg* wb = world_bg()) {
    world_bg_suspend(wb);
    delete[] world_take_region_save();
  }
  if (g_launcher != nullptr) menu_launcher_destroy(g_launcher);
  g_launcher = menu_launcher_create();
  menu_launcher_start(g_launcher, g_menu_request != 0 ? g_menu_request : 1);
}

void field_menu_update(Field* f) {
  FieldGlobals& g = globals();
  if (g_launcher == nullptr) {
    f->state = 0;
    return;
  }
  menu_launcher_step(g_launcher);
  bool ready = false;
  if (!fade_is_active(field_fade(), -1)) ready = g.loader->poll() == 0;
  if (!ready) return;
  u16 option = (g.field_flags[0x68] != 0 || f->on_save_point != 0) ? 1 : 0;
  if (g.field_flags[0x25] != 0) option |= 2;
  if (const char* e = std::getenv("SS_SAVE_ANYWHERE"); e != nullptr && *e == '1') option |= 1;  // debugging
  field_set_option_a(option);
  field_set_option_b(0);
  // pad_remove_vblank_handler(pad, 0x800752bc): the field's vblank counter (host: none).
  psyq::ResetGraph(1);
  // The load-game snapshot slot: the state block as it is now.
  auto* snapshot = new u8[0x440]();
  handoff_store(g.handoff, snapshot, 0xE);
  std::memcpy(snapshot, g.game_state, 0x2A0);
  std::memcpy(snapshot + 0x2A0, g.script_flags, 0x170);
  // The music id becomes its index in the music table (0x800750cc) for the save.
  const s16 music = static_cast<s16>(rd16(g.game_state + 0x1C));
  if (music != -1) {
    for (int i = 0;; ++i) {
      const s16 id = static_cast<s16>(read_u16(field_data(0x800750CC + i * 2)));
      if (id == -1) break;
      if (id == music) {
        wr16(g.game_state + 0x1C, static_cast<u16>(i));
        break;
      }
    }
  }
  int result = 0;
  switch (g_menu_request) {
    case 2: wr32(g.script_globals, menu_run_shop(g_launcher, g_menu_param)); break;
    case 3: menu_run_query(g_launcher, g_menu_param); break;
    case 4: menu_run_member(g_launcher, g_menu_param); break;
    case 5: menu_run_save(g_launcher); break;
    default: result = menu_run_camp(g_launcher); break;
  }
  if (const char* dump = std::getenv("SS_MENU_DUMP"); dump != nullptr && *dump != 0) {
    // Debugging: the config and inventory blocks after each menu, for comparisons.
    for (const auto& [name, block, size] : {std::tuple<const char*, const u8*, size_t>{"config", g.config, 0x2A0},
                                           std::tuple<const char*, const u8*, size_t>{"inventory", g.inventory, 0xC28}}) {
      const std::string path = std::string(dump) + "_" + name + ".bin";
      if (FILE* out = std::fopen(path.c_str(), "wb")) {
        std::fwrite(block, 1, size, out);
        std::fclose(out);
      }
    }
  }
  // TODO(port): boot 0x800134d8(0): the pad setting back.
  menu_launcher_destroy(g_launcher);
  g_launcher = nullptr;
  // pad_add_vblank_handler (host: none); the field's screen again.
  field_setup_screen(f);
  Screen* screen = field_screen();
  psyq::PutDispEnv(&screen->current->disp);
  psyq::PutDrawEnv(&screen->current->draw);
  screen_flip(screen);
  screen_clear_ot(screen);
  ActorSystem* actors = actor_system();
  if (result == 1) {
    if (auto* loaded = static_cast<u8*>(handoff_get(g.handoff, 0xE))) {
      // A game was loaded: its state block, then its map.
      for (int i = 1; i < 9; ++i) sound_release_clip(g.sound, i);
      std::memset(g.field_flags, 0, 0xA0);
      std::memcpy(g.game_state, loaded, 0x2A0);
      std::memcpy(g.script_flags, loaded + 0x2A0, 0x170);
      const u16 index = rd16(g.game_state + 0x1C);
      wr16(g.game_state + 0x1C, static_cast<u16>(index - 1) < 0x5A ? read_u16(field_data(0x800750CC + index * 2)) : 0xFFFF);
      delete[] loaded;
      handoff_clear(g.handoff, 0xE);
      sound_stop(g.sound);
      f->state = 0;
      g.disc_error = 1;  // 0x800759a4: the save's disc is checked
      f->pending_map = static_cast<s16>(rd16(g.game_state + 0x1A));
      actor_sys_setup_view(actors);
      actor_sys_reset_sprite_cache(actors);
      actor_sys_mark_all_dirty(actors);
      load_queue_set_streaming(load_queue(), false);
      // preload_free_saved_table (0x800570e8) drops the menu's copy; the port restores it
      // and releases everything below, which leaves the same empty table.
      preload_restore_table();
      load_queue_reset(load_queue());
      preload_release(-1);
      // TODO(port): 0x8007622c = 0 (the stash flag; stash.h has no setter).
      field_load_map(f, f->pending_map, true, 1);
      return;
    }
  }
  const u8* scene = map_data().scene.data();
  if (g.field_flags[0x68] == 0 && scene != nullptr) {
    screen_set_clear_colour(screen, scene[0x6F], scene[0x70], scene[0x71]);
  } else {
    screen_set_clear_colour(screen, 0, 0, 0);
  }
  g.show_battle_results = 1;
  wr16(g.game_state + 0x1C, static_cast<u16>(music));
  load_queue_set_streaming(load_queue(), true);
  preload_restore_table();
  world().streaming = 1;
  {
    const s16 tint[3] = {g_fade_targets.bg_tint[0], g_fade_targets.bg_tint[1], g_fade_targets.bg_tint[2]};
    actor_sys_setup_view(actors);
    std::memcpy(g_fade_targets.bg_tint, tint, sizeof tint);
  }
  actor_sys_reset_sprite_cache(actors);
  actor_sys_mark_all_dirty(actors);
  wr16(g.field_flags + 0x10, static_cast<u16>(f->saved_control));
  f->state = 0;
  if (g_script_vm != nullptr) vm_call_sync(g_script_vm, static_cast<u16>(g_script_vm->menu_close_entry));
  g_menu_request = 0;
  if (result != 5 && result != 6) fade_push(field_fade(), 1, 0xF, 0, 0);
  switch (result) {
    case 2:
      field_begin_game_over(f);
      break;
    case 3:
      // A battle the menu asked for (game_state +0x264).
      wr16(g.field_flags + 0x10, 0xFFFF);
      f->state = 0x21;
      break;
    case 4:  // a warp (state 0x1b) and 5: the bunny call (state 0x11): field_world.cpp
    case 5:
      field_world_menu_result(f, result);
      break;
    case 6: {
      // Teleport to map 0x2c0.
      u8* gs = g.game_state;
      wr32(gs + 0x08, -0x13F);
      wr32(gs + 0x0C, 0);
      wr32(gs + 0x10, 0x15);
      wr16(gs + 0x18, 0);
      gs[0x21] = 0x76;
      field_load_map(f, 0x2C0, true, 1);
      break;
    }
    default:
      break;
  }
}

}  // namespace ss::game::field
