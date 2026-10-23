// The command ring, the member select, the escape command and the list menus. See
// command_menu.h.

#include "game/battle/command_menu.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "game/battle/bsfx.h"
#include "game/battle/camera.h"
#include "game/battle/hud.h"
#include "game/battle/killer.h"
#include "game/battle/loaders.h"
#include "game/battle/render.h"
#include "game/battle/spells.h"
#include "game/battle/sprite.h"
#include "game/battle/unit.h"
#include "game/battle/window.h"
#include "game/boot/loader.h"
#include "game/common/screen.h"
#include "game/field/post_battle.h"
#include "game/field/script_vm.h"
#include "runtime/libgpu.h"
#include "runtime/runtime.h"

namespace ss::game::battle {
namespace {

u8 image_u8(u32 address) {
  const u8* p = battle_data(address);
  return p != nullptr ? *p : 0;
}

u16 key(int i) {
  u16 v;
  std::memcpy(&v, bg().config + i * 2, 2);
  return v;
}

PartyUnit* controlled() {
  BattleGlobals& g = bg();
  return g.party_units[g.controlled_slot & 3];
}

// The menus' memory (battle-long globals of the original).
struct CommandState {
  std::array<u8, 4> spell_top{};   // g_spell_menu_top 0x8006fb24
  std::array<u8, 4> spell_col{};   // 0x8006fb1c
  std::array<u8, 4> spell_row{};   // 0x8006fb20
  u8 chosen_spell = 0;             // g_chosen_spell 0x8006fb28
  s16 item_col = 0;                // g_item_menu_cursor 0x8006fb9a
  s16 item_row = 0;                // 0x8006fb9c
  s16 item_top = 0;                // 0x8006fb9e
  s16 chosen_item = 0;             // g_chosen_item_index 0x8006fba4
  // The ring's sprites (g_input +0x60[4] icons, +0x70[4] the X marks, +0x80 the label).
  std::array<Sprite*, 4> ring_icon{};
  std::array<Sprite*, 4> ring_x{};
  Sprite* ring_label = nullptr;
};
CommandState g_cmd;

// Inventory entries: u16, bits 0-9 the item, 10-14 the count.
u16 inv_entry(int i) {
  u16 v;
  std::memcpy(&v, bg().inventory + i * 2, 2);
  return v;
}
void inv_set(int i, u16 v) { std::memcpy(bg().inventory + i * 2, &v, 2); }
int inventory_last_used() {
  // 0x8003c85c (the battle's copy).
  int last = 0;
  for (int i = 0; i < 0x400; ++i)
    if (((inv_entry(i) >> 10) & 0x1F) != 0) last = i;
  return last;
}
// The verified item at index i (a tampered entry is cleared: item 0).
u32 inv_item(int i) {
  if (!field::inventory_verify(bg().inventory, static_cast<u32>(i))) {
    inv_set(i, 0);
    return 0;
  }
  return inv_entry(i) & 0x3FF;
}
bool item_usable(u32 item) { return ((bg().item_effects[item >> 3] >> (item & 7)) & 1) != 0; }

// ---- The description balloon (DescPopup, 0x840 bytes, vtable 0x8006ccb8) ----

struct DescPopup : BWindow {
  s32 entry = 0;                 // +0x20: the archive entry of the texts (#2271 spells, #2272 items)
  s32 index = 0;                 // +0x24
  std::vector<u8> block;         // +0x28: the description's 2 KB sector (text, then its font at +0x80)
  s32 cached = -1;               // +0x828: the index the block holds
  s32 anchor_x = 0, anchor_y = 0;  // +0x82c / +0x830
  s32 timer = 0;                 // +0x834
  s32 rise = 0;                  // +0x838
  u8 visible = 0;                // +0x83c
  u8 load = 0;                   // +0x83d: 0 none, 1 loading, 2 loaded
  u8 laid_out = 0;               // +0x83e
  bool load_done_next = false;   // (port) the read finishes on the next frame

  explicit DescPopup(int e) : BWindow(0x10) {
    // desc_popup_create 0x80041620.
    entry = e;
    hide();
    open(0, 0, 8, 8);
    ot_layer_to_front(&ctx);
  }
  void hide() {
    // desc_popup_hide 0x800416f4.
    laid_out = 0;
    load = 0;
    timer = 0;
    visible = 0;
    load_done_next = false;
  }
  void show(s32 at_x, s32 at_y, int i) {
    // desc_popup_show 0x80041724.
    if (((visible == 0 && load == 0 && laid_out == 0) && timer == 0) || index != i) {
      hide();
      visible = 1;
      anchor_x = at_x;
      anchor_y = at_y;
      index = i;
    }
  }
  void read_block() {
    // bload_request(entry | (index + 1) << 16): sector 'index' of the entry.
    BattleGlobals& g = bg();
    block.assign(0x800, 0);
    if (g.loader == nullptr) return;
    const int bytes = g.loader->entry_size(entry);
    if (bytes <= 0) return;
    std::vector<u8> all(static_cast<size_t>(bytes));
    g.loader->load_sync(all.data(), entry, 0);
    const size_t at = static_cast<size_t>(index) * 0x800;
    if (at < all.size()) std::memcpy(block.data(), all.data() + at, std::min<size_t>(0x800, all.size() - at));
  }
  void draw() override;  // [1] desc_popup_draw 0x800417bc
};

void DescPopup::draw() {
  BattleGlobals& g = bg();
  if (visible == 0) return;
  if (laid_out == 0) {
    timer += g.frame_ticks;
    if (load == 0) {
      if (cached == index) {
        load = 2;
      } else if (0xE < timer) {
        read_block();
        cached = -1;
        load = 1;
        load_done_next = true;
      }
    } else if (load == 1) {
      if (load_done_next) {
        load_done_next = false;
      } else {
        load = 2;
        cached = index;
      }
    }
    if (timer < 0x20 || load != 2) return;
    laid_out = 1;
    std::vector<u8> font(block.begin() + 0x80, block.end());
    std::swap(g.text_font, font);
    const int width = text_width(block.data()) + 4;
    std::swap(g.text_font, font);
    s32 nx = anchor_x;
    if (0x13A < nx + width) nx = 0x13A - width;
    rise = 0x10;
    w = static_cast<s16>(width);
    x = static_cast<s16>(nx);
    h = 0x10;
    y = static_cast<s16>(anchor_y - 0x10);
  }
  window_text_begin(this);
  window_save_rect(this);
  std::vector<u8> font(block.begin() + 0x80, block.end());
  std::swap(g.text_font, font);
  x = static_cast<s16>(x - 6);
  y = static_cast<s16>(y - 6);
  w = static_cast<s16>(w + 0xC);
  h = static_cast<s16>(h + 0xC);
  text_draw(this, 0, 0, block.data(), 4);
  std::swap(g.text_font, font);
  window_restore_rect(this);
  // The balloon's outline (four lines) and its backdrop.
  for (int i = 0; i < 4; ++i) {
    auto* l = screen_alloc_prims<psyq::LINE_F2>(ctx.arena, 1);
    if (l == nullptr) break;
    psyq::setLineF2(l);
    l->r0 = 1, l->g0 = 1, l->b0 = 1;
    const s16 x0 = x, x1 = static_cast<s16>(x + w), y0 = y, y1 = static_cast<s16>(y + h);
    switch (i) {
      case 0: l->x0 = x0, l->y0 = y0, l->x1 = x1, l->y1 = y0; break;
      case 1: l->x0 = x1, l->y0 = y0, l->x1 = x1, l->y1 = y1; break;
      case 2: l->x0 = x0, l->y0 = y1, l->x1 = x1, l->y1 = y1; break;
      default: l->x0 = x0, l->y0 = y0, l->x1 = x0, l->y1 = y1; break;
    }
    psyq::addPrim(ctx.ot_head, l);
  }
  if (auto* t = screen_alloc_prims<psyq::TILE>(ctx.arena, 1)) {
    psyq::setTile(t);
    t->code = 0x60;
    t->r0 = 0xFF, t->g0 = 0xF0, t->b0 = 0x98;
    t->x0 = static_cast<s16>(x + 1);
    t->y0 = static_cast<s16>(y + 1);
    t->w = w;
    t->h = h;
    psyq::addPrim(ctx.ot_head, t);
  }
  // The clip area grows as the balloon rises (rise ticks to go).
  psyq::RECT r;
  r.x = x < 0 ? 0 : x;
  s32 top = y + rise;
  if (top < 0) top = 0;
  r.y = static_cast<s16>(screen_draw_y() + top);
  s32 cw = w + 3;
  if (0x140 < cw + x) cw = 0x140 - x;
  if (cw < 0) cw = 0;
  r.w = static_cast<s16>(cw);
  s32 ch = h - rise + 1;
  if (0xE8 < ch + y) ch = 0xE8 - y;
  if (ch < 0) ch = 0;
  r.h = static_cast<s16>(ch);
  if (auto* a = screen_alloc_prims<psyq::DR_AREA>(ctx.arena, 1)) {
    psyq::SetDrawArea(a, &r);
    psyq::addPrim(ctx.ot_head, a);
  }
  if (auto* m = screen_alloc_prims<psyq::DR_MODE>(ctx.arena, 1)) {
    psyq::SetDrawMode(m, 0, 0, 0x1F, nullptr);
    psyq::addPrim(ctx.ot_head, m);
  }
  if (rise != 0) {
    if (rise < g.frame_ticks * 2) {
      y = static_cast<s16>(y + rise);
      rise = 0;
    } else {
      y = static_cast<s16>(y + g.frame_ticks * 2);
      rise -= g.frame_ticks * 2;
    }
  }
}

// ---- The list menus (base vtable 0x80080fec) ----

struct ListMenu : BWindow {
  Sprite* cursor = nullptr;     // +0x20
  Sprite* icon = nullptr;       // +0x24: the menu's title icon
  s16 scroll = 0;               // +0x2a: pixels of the row scroll animation
  s16 cols = 0;                 // +0x2c
  s16 rows = 0;                 // +0x2e
  s16 from_col = 0;             // +0x30
  s16 from_row = 0;             // +0x32
  s16 last_col = 0;             // +0x36
  s16 last_row = 0;             // +0x38
  s16 bar_x = 0x133;            // +0x3a: the scroll bar's x
  u8 state = 0;                 // +0x3c: 0 opening, 1 open, 2 closing
  s8 move_t = 0;                // +0x3d: cursor move animation (4..0)
  s8 move_dx = 0;               // +0x3e
  s8 move_dy = 0;               // +0x3f
  u8 page_t = 0;                // +0x40: page key repeat
  s16 col = 0;                  // +0x42
  s16 row = 0;                  // +0x44
  s16 top = 0;                  // +0x46: first row shown
  s32 count = 0;                // +0x48 (spell / item menus)
  DescPopup* popup = nullptr;   // +0x4c (spell / item menus)

  ListMenu();                   // list_menu_create 0x8007e644
  ~ListMenu() override;
  void update() override;       // [1] list_menu_update 0x8007e750
  void release() override;      // [4]
  virtual void draw_items(int first, int ry) = 0;  // [6]
  virtual void draw_frame() { window_draw_frame(this); }  // [7] list_menu_draw_frame 0x8007ef00
  virtual void item_pos(int c, int r, s32* ox, s32* oy) = 0;  // [8]
  virtual int confirm() = 0;    // [9]: 0 stay, 1 done, 2 another input mode took over
  void draw_contents() override { draw_scrollbar(); }  // [5] (window_draw_frame calls it)
  void wrap_cursor(int c, int r, s32* oc, s32* orow) const;  // 0x8007ef20
  void clamp_scroll();          // 0x8007ef7c
  void draw_scrollbar();        // 0x8007efd0
};

ListMenu* g_open = nullptr;                    // g_open_list_menu 0x8006faa4
std::vector<ListMenu*> g_closed;               // (released menus wait here to be freed)

void retire(ListMenu* m) {
  if (g_open == m) g_open = nullptr;
  if (std::find(g_closed.begin(), g_closed.end(), m) == g_closed.end()) g_closed.push_back(m);
}

ListMenu::ListMenu() : BWindow(0x28) {
  BattleGlobals& g = bg();
  cursor = sprite_spawn_ui(0x1F, 0, nullptr);
  open(0, 0, 0x140, 0x10);
  if (BattleHud* hud = battle_hud()) hud->clip = 0x10;
  ot_layer_to_front(g.prim_pool_b);
  if (g_open != nullptr) {
    ListMenu* old = g_open;
    sprite_free(old->icon);
    old->icon = nullptr;
    sprite_free(old->cursor);
    old->cursor = nullptr;
    old->release();
    retire(old);
  }
  g_open = this;
}

ListMenu::~ListMenu() {
  if (opened) release();
  delete popup;
}

void ListMenu::release() {
  if (popup != nullptr && popup->opened) popup->release();
  BWindow::release();
}

void ListMenu::wrap_cursor(int c, int r, s32* oc, s32* orow) const {
  if (cols <= c) {
    ++r;
    c = 0;
  }
  if (c < 0) {
    --r;
    c = cols - 1;
  }
  if (r < 0) r = 0;
  if (rows <= r) r = rows - 1;
  *oc = c;
  *orow = r;
}

void ListMenu::clamp_scroll() {
  if (row < top) top = row;
  if (top + 1 < row) top = static_cast<s16>(row - 1);
}

void ListMenu::draw_scrollbar() {
  // 0x8007efd0: the thumb (a gouraud quad between the shown rows' marks), the bar's two
  // edges and its trough, when there are more than two rows.
  if (rows < 3) return;
  s32 mark[3];
  for (int i = 0; i < 3; ++i) mark[i] = (((top + i) * 0xE - scroll) * (h - 9)) / (rows * 0xE) + 4;
  if (mark[0] == mark[1]) mark[1] = mark[0] + 1;
  if (mark[2] <= mark[1]) mark[2] = mark[1] + 1;
  if (auto* p = screen_alloc_prims<psyq::POLY_G4>(ctx.arena, 1)) {
    psyq::setPolyG4(p);
    p->r0 = 0xA0, p->g0 = 0xD0, p->b0 = 0xFF, p->code = 0x38;
    p->r1 = 0x20, p->g1 = 0x40, p->b1 = 0xF0;
    p->r2 = 0x50, p->g2 = 0xA0, p->b2 = 0xFF;
    p->r3 = 0x00, p->g3 = 0x10, p->b3 = 0xA0;
    p->x0 = static_cast<s16>(bar_x + 4), p->y0 = static_cast<s16>(mark[0]);
    p->x1 = static_cast<s16>(bar_x + 8), p->y1 = static_cast<s16>(mark[1]);
    p->x2 = static_cast<s16>(bar_x + 1), p->y2 = static_cast<s16>(mark[1]);
    p->x3 = static_cast<s16>(bar_x + 4), p->y3 = static_cast<s16>(mark[2]);
    psyq::addPrim(ctx.ot_head, p);
  }
  auto line = [&](u32 rgb, s16 x0, s16 y0, s16 x1, s16 y1, s16 x2, s16 y2) {
    auto* l = screen_alloc_prims<psyq::LINE_F3>(ctx.arena, 1);
    if (l == nullptr) return;
    psyq::setLineF3(l);
    l->r0 = static_cast<u8>(rgb), l->g0 = static_cast<u8>(rgb >> 8), l->b0 = static_cast<u8>(rgb >> 16);
    l->x0 = x0, l->y0 = y0, l->x1 = x1, l->y1 = y1, l->x2 = x2, l->y2 = y2;
    psyq::addPrim(ctx.ot_head, l);
  };
  line(0x101030, static_cast<s16>(bar_x + 8), static_cast<s16>(y + 3), bar_x, static_cast<s16>(y + 3), bar_x,
       static_cast<s16>(y + h - 5));
  line(0x606080, static_cast<s16>(bar_x + 8), static_cast<s16>(y + 3), static_cast<s16>(bar_x + 8),
       static_cast<s16>(y + h - 5), bar_x, static_cast<s16>(y + h - 5));
  if (auto* t = screen_alloc_prims<psyq::TILE>(ctx.arena, 1)) {
    psyq::setTile(t);
    t->r0 = 0x30, t->g0 = 0x30, t->b0 = 0x50, t->code = 0x60;
    t->x0 = static_cast<s16>(bar_x + 1);
    t->y0 = static_cast<s16>(y + 4);
    t->w = 7;
    t->h = static_cast<s16>(h - 9);
    psyq::addPrim(ctx.ot_head, t);
  }
}

void ListMenu::update() {
  BattleGlobals& g = bg();
  BattleInput* in = &battle_input();
  BattleHud* hud = battle_hud();
  if (state == 0 && hud != nullptr) {
    h = static_cast<s16>(hud->clip);
    hud->clip += g.frame_ticks * 4;
    if (0x28 < hud->clip) {
      hud->clip = 0x28;
      h = 0x28;
      hud_set_visible(hud, 0);
      state = 1;
    }
  }
  if (state == 2 && hud != nullptr) {
    if (hud->clip == 0) {
      sprite_free(icon);
      icon = nullptr;
      sprite_free(cursor);
      cursor = nullptr;
      release();
      retire(this);
      return;
    }
    h = static_cast<s16>(hud->clip);
    hud->clip -= g.frame_ticks * 4;
    if (hud->clip < 0x10) hud->clip = 0;
  }
  const s32 t = page_t - g.frame_ticks;
  page_t = static_cast<u8>(t);
  if (static_cast<s32>(static_cast<u32>(t) << 24) < 0) page_t = 0;
  last_col = col;
  last_row = row;
  s32 cx = 0, cy = 0;
  if (move_t != 0) {
    move_t = static_cast<s8>(move_t - g.frame_ticks);
    if (move_t < 1) {
      move_t = 0;
      scroll = 0;
      clamp_scroll();
    } else {
      if (row < top) {
        const s32 d = (4 - move_t) * 0xE;
        s32 v = d + 3;
        scroll = static_cast<s16>(v < 0 ? (d + 6) >> 2 : v >> 2);
      }
      if (top + 1 < row) {
        const s32 d = (4 - move_t) * 0xE;
        s32 v = d + 3;
        if (v < 0) v = d + 6;
        scroll = static_cast<s16>(-(v >> 2));
      }
      s32 fx, fy, tx, ty;
      item_pos(from_col, from_row, &fx, &fy);
      item_pos(col, row, &tx, &ty);
      s32 a = (fx - tx) * move_t;
      if (a < 0) a += 3;
      cx = (a >> 2) + tx;
      a = (fy - ty) * move_t;
      if (a < 0) a += 3;
      cy = (a >> 2) + ty;
    }
  }
  if (state == 1 && move_t == 0) {
    item_pos(col, row, &cx, &cy);
    if ((in->repeat & key(4)) != 0 && page_t == 0) {
      top = static_cast<s16>(top - 2);
      if (top < 0) top = 0;
      s32 nc, nr;
      wrap_cursor(col, row - 2, &nc, &nr);
      row = static_cast<s16>(nr);
      clamp_scroll();
      page_t = 4;
    }
    if ((in->repeat & key(5)) != 0 && page_t == 0) {
      top = static_cast<s16>(top + 2);
      if (rows - 1 <= top) top = static_cast<s16>(rows - 2);
      s32 nc, nr;
      wrap_cursor(col, row + 2, &nc, &nr);
      row = static_cast<s16>(nr);
      clamp_scroll();
      page_t = 4;
    }
    if ((in->repeat & 0xF000) != 0 && state == 1) {
      move_t = 4;
      move_dx = 0;
      move_dy = 0;
      if (in->repeat & 0x8000) move_dx = -1;
      if (in->repeat & 0x2000) move_dx = 1;
      if ((in->repeat & 0x1000) != 0 && move_dx == 0) move_dy = -1;
      if ((in->repeat & 0x4000) != 0 && move_dx == 0) move_dy = 1;
      from_col = col;
      from_row = row;
      s32 nc, nr;
      wrap_cursor(move_dx + col, move_dy + row, &nc, &nr);
      col = static_cast<s16>(nc);
      row = static_cast<s16>(nr);
    }
  }
  auto close_to_idle = [&]() {
    in->mode = 0;
    g.flags = static_cast<u8>(g.flags & 0xFD);
    camera_end_mode(g.camera, 2, 1, nullptr);
  };
  if ((in->pressed & key(1)) != 0 && state == 1) {
    bsfx_play_ui(6);
    state = 2;
    close_to_idle();
    if (hud != nullptr) {
      hud_set_visible(hud, 1);
      hud->clip = h;
    }
  }
  if ((in->pressed & key(0)) != 0 && state == 1) {
    const int r = confirm();
    if (r != 0) {
      bsfx_play_ui(2);
      state = 2;
      if (r == 1) close_to_idle();
      if (hud != nullptr) {
        hud_set_visible(hud, 1);
        hud->clip = h;
      }
    }
  }
  if (last_col != col || last_row != row) bsfx_play_ui(1);
  sprite_set_screen_pos(cursor, static_cast<s16>(cx), static_cast<s16>(cy));
  int first = top * cols;
  int ry = scroll - 1;
  if (-1 < ry) {
    ry = scroll - 0xF;
    first -= cols;
  }
  draw_items(first, ry);
  draw_frame();
}

// ---- The spell menu (vtable 0x80080e9c) ----

struct SpellMenu : ListMenu {
  SpellMenu() {
    // spell_menu_create 0x80072c5c.
    BattleGlobals& g = bg();
    const int slot = g.controlled_slot & 3;
    cols = 2;
    count = image_u8(0x8006B1ABu + g.party_slot[static_cast<size_t>(slot)][3]);
    rows = static_cast<s16>((count + 1) >> 1);
    top = g_cmd.spell_top[static_cast<size_t>(slot)];
    col = g_cmd.spell_col[static_cast<size_t>(slot)];
    row = g_cmd.spell_row[static_cast<size_t>(slot)];
    clamp_scroll();
    const u8 klass = g.party_slot[static_cast<size_t>(slot)][3];
    if (klass == 9 || klass == 0xC) {
      icon = sprite_spawn_ui(0x20, 0, nullptr);
      sprite_set_screen_pos(icon, static_cast<s16>(x + 0x118), static_cast<s16>(y + 0x21));
    } else {
      icon = sprite_spawn_ui(0x1B, 0, nullptr);
      sprite_set_screen_pos(icon, static_cast<s16>(x + 0x10E), static_cast<s16>(y + 0x21));
    }
    popup = new DescPopup(0x8DF);
  }
  void draw_items(int first, int ry) override;  // 0x80072e10
  void draw_frame() override {
    // 0x800731c0: the list's frame right of the MP box.
    x = 0x48;
    w = 0xF8;
    window_draw_frame(this);
    x = 0;
    w = 0x48;
    window_draw_frame(this);
    w = 0x140;
  }
  void item_pos(int c, int r, s32* ox, s32* oy) override {
    // 0x80073218.
    *ox = x + c * 0x6C + 0x5D;
    *oy = y + (r - top) * 0xE + scroll + 0xD;
  }
  int confirm() override;  // 0x80073294
};

void SpellMenu::draw_items(int first, int ry) {
  BattleGlobals& g = bg();
  PartyUnit* u = controlled();
  const u8* pslot = g.party_slot[g.controlled_slot & 3];
  window_text_begin(this);
  const u8* list = pc_spell_list(u);
  const int cur = col + row * 2;
  if (u->record == nullptr || list == nullptr || u->record[0x3C + cur] == 0 || move_t != 0) {
    popup->hide();
  } else {
    const u8 sp = list[cur];
    s32 px, py;
    item_pos(col, row, &px, &py);
    if (sp != 0) popup->show(px - 3, py + 0xB, sp - 1);
  }
  s16 mp;
  std::memcpy(&mp, pslot + 0x24, 2);
  for (int i = first; ry < h - 0x11; ry += 0xE) {
    int tx = 0x56;
    for (int c = 0; c < 2 && i < count; ++c, ++i, tx += 0x6C) {
      if (u->record == nullptr || list == nullptr || u->record[0x3C + i] == 0) continue;
      const int cost = unit_spell_mp_cost(u, list[i] - 1);
      u32 pal = mp < cost ? 1 : 0;
      if (cur == i && pal == 0) pal = 0x8000;
      text_draw_id(this, tx, ry, list[i] + 999, pal);
    }
  }
  text_draw_id(this, 0, 0, 4, 0);
  char digits[8] = {};
  if (cur < count && u->record != nullptr && list != nullptr && u->record[0x3C + cur] != 0) {
    field::format_decimal(digits, unit_spell_mp_cost(u, list[cur] - 1), 3, ' ');
  } else if (const u8* d = ovl1984_data(0x80080E94)) {
    std::memcpy(digits, d, 4);
  }
  u8 glyphs[16];
  text_from_ascii(digits, glyphs);
  text_draw(this, 0x28, 0, glyphs, 0);
  window_text_end(this);
  const u8* mp_label = ovl1984_data(0x80080E98);
  window_draw_small_text(this, 0, 0x12, mp_label != nullptr ? reinterpret_cast<const char*>(mp_label) : "MP", 0);
  s16 mp_max;
  std::memcpy(&mp_max, pslot + 0x22, 2);
  s32 q = mp_max;
  if (q < 0) q += 3;
  int pal = 0;
  if (mp < (q >> 2)) pal = 0xF;
  if (mp == 0) pal = 10;
  char num[8];
  window_draw_small_text(this, 0x18, 0x12, field::format_decimal(num, mp, 4, ' '), pal);
  window_text_end(this);
}

int SpellMenu::confirm() {
  BattleGlobals& g = bg();
  BattleInput* in = &battle_input();
  PartyUnit* u = controlled();
  const int cur = col + row * 2;
  const u8* list = pc_spell_list(u);
  int cost = 99999;
  if (cur < count && list != nullptr && u->record != nullptr && u->record[0x3C + cur] != 0)
    cost = unit_mp_cost(u, spell_mp_cost_base(list[cur]));
  if (u->st()->mp < cost) {
    bsfx_play_ui(7);
    return 0;
  }
  const int slot = g.controlled_slot & 3;
  g_cmd.spell_top[static_cast<size_t>(slot)] = static_cast<u8>(top);
  g_cmd.spell_col[static_cast<size_t>(slot)] = static_cast<u8>(col);
  g_cmd.spell_row[static_cast<size_t>(slot)] = static_cast<u8>(row);
  const u8 spell = list[cur];
  const int cls = spell_target_class(spell);
  if (cls < 3) {
    camera_end_mode(g.camera, 2, 1, nullptr);
    g_cmd.chosen_spell = spell;
  }
  switch (cls) {
    case 0:
      in->target_purpose = 1;
      input_open_target_select(in);
      return 2;
    case 1:
    case 2:
      in->member_purpose = static_cast<u8>(cls);
      input_open_member_select(in);
      return 2;
    case 3:
      u->command = 4;
      u->spell = spell;
      // ORIGINAL BUG (original_bugs.md S20): a spell on the whole party keeps the member's
      // current target; with none (no action yet this battle) the SpellCast is dropped after
      // the MP is paid (spellcast_retarget keeps the null target for a party caster), so the
      // spell does nothing. Fixed: the caster stands in.
      if (u->target == nullptr) u->target = u;
      return 1;
    default: return 1;
  }
}

// ---- The tactics menu (vtable 0x8008103c) ----

struct TacticsMenu : ListMenu {
  TacticsMenu() {
    // tactics_menu_create 0x800801c0.
    PartyUnit* u = controlled();
    cols = 2;
    rows = 3;
    const u8 t = u->record != nullptr ? u->record[0x38] : 0;
    top = 0;
    col = static_cast<s16>(t & 1);
    row = static_cast<s16>(t >> 1);
    clamp_scroll();
    icon = sprite_spawn_ui(0x1E, 0, nullptr);
    sprite_set_screen_pos(icon, static_cast<s16>(x + 0x118), static_cast<s16>(y + 0x21));
  }
  void draw_items(int first, int ry) override {
    // 0x8008027c: the six tactics, the member's own in palette 3.
    BattleGlobals& g = bg();
    PartyUnit* u = controlled();
    window_text_begin(this);
    const u8 cur_t = u->record != nullptr ? u->record[0x38] : 0;
    s16 character;
    std::memcpy(&character, g.party_slot[g.controlled_slot & 3], 2);
    const int base = image_u8(0x8006B047u + static_cast<u32>(character)) + 2000;
    u32 i = static_cast<u32>(first);
    for (; ry < h - 0x11; ry += 0xE) {
      int tx = 0x10;
      for (int c = 0; c < 2; ++c, ++i, tx += 0xA0) {
        u32 pal = i == cur_t ? 3 : 0;
        if (static_cast<u32>(col + row * 2) == i) pal |= 0x8000;
        text_draw_id(this, tx, ry, static_cast<int>(i) + base, pal);
      }
    }
    window_text_end(this);
  }
  void item_pos(int c, int r, s32* ox, s32* oy) override {
    *ox = x + c * 0xA0 + 0x16;
    *oy = y + (r - top) * 0xE + scroll + 0xD;
  }
  int confirm() override {
    // 0x80080430: the tactic (5, "no orders", keeps the one to go back to).
    PartyUnit* u = controlled();
    const int i = col + row * 2;
    if (u->record != nullptr) {
      if (i != 5) u->record[0x39] = static_cast<u8>(i);
      u->record[0x38] = static_cast<u8>(i);
    }
    return 1;
  }
};

// ---- The item menu (vtable 0x80080f04) ----

struct ItemMenu : ListMenu {
  ItemMenu() {
    // item_menu_create 0x80073560.
    cols = 2;
    const int last = inventory_last_used();
    count = last + 1;
    rows = static_cast<s16>((last + 2) / 2);
    top = g_cmd.item_top;
    col = g_cmd.item_col;
    row = g_cmd.item_row;
    clamp_scroll();
    icon = sprite_spawn_ui(0x1C, 0, nullptr);
    sprite_set_screen_pos(icon, static_cast<s16>(x + 0x120), static_cast<s16>(y + 0x21));
    popup = new DescPopup(0x8E0);
  }
  void draw_items(int first, int ry) override {
    // 0x80073688: the items with their counts (unusable ones in palette 1).
    window_text_begin(this);
    const int cur = col + row * 2;
    if (((inv_entry(cur) >> 10) & 0x1F) == 0 || move_t != 0) {
      popup->hide();
    } else {
      const u32 item = inv_item(cur);
      s32 px, py;
      item_pos(col, row, &px, &py);
      if (item != 0) popup->show(px - 3, py + 0xB, static_cast<int>(item) - 1);
    }
    for (int i = first; ry < h - 0x11; ry += 0xE) {
      int tx = 0x10, nx = 0x88;
      for (int c = 0; c < 2 && i < count; ++c, ++i, tx += 0x90, nx += 0x90) {
        const u16 e = inv_entry(i);
        if (((e >> 10) & 0x1F) == 0) continue;
        u32 pal = cur == i ? 0x8000 : 0;
        const u32 item = inv_item(i);
        if (!item_usable(item)) pal |= 1;
        text_draw_id(this, tx, ry, static_cast<int>(item) + 2999, pal);
        char digits[8];
        digits[0] = ':';
        field::format_decimal(digits + 1, (e >> 10) & 0x1F, 2, ' ');
        u8 glyphs[16];
        text_from_ascii(digits, glyphs);
        text_draw(this, nx, ry, glyphs, 0);
      }
    }
    window_text_end(this);
  }
  void item_pos(int c, int r, s32* ox, s32* oy) override {
    *ox = x + c * 0x90 + 0x16;
    *oy = y + (r - top) * 0xE + scroll + 0xD;
  }
  int confirm() override;  // 0x8007398c
};

int ItemMenu::confirm() {
  BattleGlobals& g = bg();
  BattleInput* in = &battle_input();
  const int cur = col + row * 2;
  if (((inv_entry(cur) >> 10) & 0x1F) != 0) {
    const u32 item = inv_item(cur);
    if (item_usable(item)) {
      g_cmd.item_top = top;
      g_cmd.item_col = col;
      g_cmd.item_row = row;
      const u32 kind = g.item_effects[0x80 + item];
      if (kind == 0) return 0;
      const int cls = image_u8(0x8006B377u + kind);
      g_cmd.chosen_item = static_cast<s16>(cur);
      if (cls < 3) camera_end_mode(g.camera, 2, 1, nullptr);
      switch (cls) {
        case 0: in->member_purpose = 3; break;
        case 1: in->member_purpose = 4; break;
        case 2:
          in->target_purpose = 2;
          input_open_target_select(in);
          return 2;
        case 3:
        case 4:
          item_use_battle(controlled(), nullptr, cur);
          return 1;
        case 5: in->member_purpose = 5; break;
        default: return 1;
      }
      input_open_member_select(in);
      return 2;
    }
  }
  bsfx_play_ui(7);
  return 0;
}

}  // namespace

u8 chosen_spell() { return g_cmd.chosen_spell; }
s16 chosen_item_index() { return g_cmd.chosen_item; }

void spell_menu_open() { new SpellMenu(); }
void item_menu_open() { new ItemMenu(); }
void tactics_menu_open() { new TacticsMenu(); }

void command_menu_reset() {
  for (ListMenu* m : g_closed) delete m;
  g_closed.clear();
  delete g_open;
  g_open = nullptr;
  g_cmd = CommandState{};
}

// ---- The ring ----

void input_ring_menu_open(BattleInput* in) {
  BattleGlobals& g = bg();
  for (int i = 0; i < 4; ++i) {
    sprite_free(g_cmd.ring_icon[static_cast<size_t>(i)]);
    g_cmd.ring_icon[static_cast<size_t>(i)] = nullptr;
    sprite_free(g_cmd.ring_x[static_cast<size_t>(i)]);
    g_cmd.ring_x[static_cast<size_t>(i)] = nullptr;
  }
  sprite_free(g_cmd.ring_label);
  g_cmd.ring_label = nullptr;
  PartyUnit* u = controlled();
  for (int i = 0; i < 4; ++i) {
    g_cmd.ring_icon[static_cast<size_t>(i)] = sprite_spawn_ui(i + 0x17, 0, nullptr);
    bool barred = false;
    if (i == 0) barred = u->can_cast() != 1;
    else if (i == 1) barred = g.item_cooldown != 0 || g.party_escaping != 0 || u->busy != 0 || unit_is_down(u);
    else if (i == 2) barred = g.can_escape == 0;
    if (barred) g_cmd.ring_x[static_cast<size_t>(i)] = sprite_spawn_ui(0x16, 0, nullptr);
  }
  g_cmd.ring_label = nullptr;
  in->ring_radius = 0;
  in->ring_rot = 0;
  in->ring_prev = 0xFF;
}

void input_ring_menu_update(BattleInput* in, int closing) {
  BattleGlobals& g = bg();
  PartyUnit* u = controlled();
  if (g_cmd.ring_x[0] != nullptr && u->can_cast() != 0) {
    sprite_free(g_cmd.ring_x[0]);
    g_cmd.ring_x[0] = nullptr;
  }
  if (unit_is_down(u) && g_cmd.ring_x[1] == nullptr) g_cmd.ring_x[1] = sprite_spawn_ui(0x16, 0, nullptr);
  s8 layer = 0;
  u32 k = static_cast<u32>(static_cast<s8>(in->ring_sel));
  s32 deg = 0;
  for (int n = 0; n < 4; ++n, layer = static_cast<s8>(layer + 2), deg += 0x5A) {
    k = (k - 1) & 3;
    Sprite* ic = g_cmd.ring_icon[k];
    if (ic == nullptr) continue;
    ic->layer = layer;
    const u32 a = static_cast<u32>(((deg + in->ring_rot) * 0x1000) / 0x168);
    const s32 dx = (bcos(a) * in->ring_radius) >> 12;
    const s32 dy = (((-bsin(a)) * in->ring_radius) / 3 >> 12) - 0x40;
    sprite_attach_screen(ic, u, dx, dy);
    s32 sc;
    if (k == static_cast<u32>(static_cast<s8>(in->ring_prev))) {
      s32 r = in->ring_rot;
      if (r < 0) r = -r;
      sc = (((r << 10) / 0x5A + 0xC00) * in->ring_radius) / 0x18;
    } else if (k == static_cast<u32>(static_cast<s8>(in->ring_sel))) {
      s32 r = in->ring_rot;
      if (r < 0) r = -r;
      sc = ((((0x5A - r) * 0x400) / 0x5A + 0xC00) * in->ring_radius) / 0x18;
    } else {
      sc = in->ring_radius << 7;
    }
    ic->scale_x = sc;
    ic->scale_y = sc;
    if (Sprite* xm = g_cmd.ring_x[k]) {
      sprite_attach_screen(xm, u, dx, dy);
      xm->layer = static_cast<s8>(layer + 1);
      xm->scale_x = sc;
      xm->scale_y = sc;
    }
  }
  sprite_free(g_cmd.ring_label);
  const u8 klass = g.party_slot[g.controlled_slot & 3][3];
  const int label = (in->ring_sel == 0 && (klass == 9 || klass == 0xC)) ? 0x20 : static_cast<s8>(in->ring_sel) + 0x1B;
  g_cmd.ring_label = sprite_spawn_ui(label, 0, nullptr);
  sprite_attach_screen(g_cmd.ring_label, g_cmd.ring_icon[static_cast<size_t>(in->ring_sel & 3)], 8, 8);
  if (g_cmd.ring_label != nullptr) g_cmd.ring_label->layer = 9;
  if (closing == 0) {
    in->ring_radius = static_cast<s16>(in->ring_radius + g.frame_ticks * 2);
    if (0x18 < in->ring_radius) in->ring_radius = 0x18;
  } else {
    if (g_cmd.ring_label != nullptr) {
      const s32 sc = (in->ring_radius << 12) / 0x18;
      g_cmd.ring_label->scale_x = sc;
      g_cmd.ring_label->scale_y = sc;
    }
    in->ring_radius = static_cast<s16>(in->ring_radius - g.frame_ticks * 2);
    if (in->ring_radius < 1) {
      in->ring_radius = 0;
      for (int i = 0; i < 4; ++i) {
        sprite_free(g_cmd.ring_icon[static_cast<size_t>(i)]);
        g_cmd.ring_icon[static_cast<size_t>(i)] = nullptr;
        sprite_free(g_cmd.ring_x[static_cast<size_t>(i)]);
        g_cmd.ring_x[static_cast<size_t>(i)] = nullptr;
      }
      sprite_free(g_cmd.ring_label);
      g_cmd.ring_label = nullptr;
    }
  }
  if (0 < in->ring_rot) {
    in->ring_rot = static_cast<s16>(in->ring_rot - g.frame_ticks * 0x12);
    if (in->ring_rot < 0) in->ring_rot = 0;
  }
  if (in->ring_rot < 0) {
    in->ring_rot = static_cast<s16>(in->ring_rot + g.frame_ticks * 0x12);
    if (0 < in->ring_rot) in->ring_rot = 0;
  }
  if (closing != 0) return;
  if ((in->repeat & 0x9000) != 0 && in->ring_rot == 0) {
    bsfx_play_ui(1);
    in->ring_rot = static_cast<s16>(0xFFA6);
    in->ring_prev = in->ring_sel;
    in->ring_sel = static_cast<u8>((in->ring_sel + 1) & 3);
  }
  if ((in->repeat & 0x6000) != 0 && in->ring_rot == 0) {
    bsfx_play_ui(1);
    in->ring_rot = 0x5A;
    in->ring_prev = in->ring_sel;
    in->ring_sel = static_cast<u8>((in->ring_sel - 1) & 3);
  }
  bool to_idle = false;
  if ((in->pressed & key(0)) != 0 && g_open == nullptr) {
    if (g_cmd.ring_x[static_cast<size_t>(in->ring_sel & 3)] == nullptr) {
      bsfx_play_ui(2);
      switch (in->ring_sel) {
        case 0: spell_menu_open(); in->mode = 4; return;
        case 1: item_menu_open(); in->mode = 4; return;
        case 3: tactics_menu_open(); in->mode = 4; return;
        case 2:
          battle_command_escape();
          to_idle = true;
          break;
        default: in->mode = 4; return;
      }
    } else {
      bsfx_play_ui(7);
    }
  }
  if (!to_idle) {
    if (g.state == kStateFighting && (in->pressed & key(1)) == 0) return;
    bsfx_play_ui(6);
  }
  in->mode = 0;
  g.flags = static_cast<u8>(g.flags & 0xFD);
  camera_end_mode(g.camera, 2, 1, nullptr);
}

void battle_command_escape() {
  // 0x8004d914: each standing member's run time from the level difference (60..600
  // ticks; two thirds / a third with effects 0 / 0x800 and 0 / 0x1000, 4 with 2 / 0x20000).
  BattleGlobals& g = bg();
  int n = 0;
  s32 party = 0;
  for (int i = 0; i < 4; ++i) {
    if (g.party_units[static_cast<size_t>(i)] == nullptr) continue;
    ++n;
    s16 lv;
    std::memcpy(&lv, g.party_slot[static_cast<size_t>(i)] + 0x26, 2);
    party += lv;
  }
  if (n == 0) n = 1;
  party /= n;
  n = 0;
  s32 foes = 0;
  for (int i = 0; i < 8; ++i) {
    if (g.enemy_units[static_cast<size_t>(i)] == nullptr) continue;
    ++n;
    s16 lv;
    std::memcpy(&lv, g.enemy_stats[static_cast<size_t>(i)] + 0x26, 2);
    foes += lv;
  }
  if (n == 0) n = 1;
  const s32 diff = foes / n - party;
  const s32 base = (diff + 2) * 0x1E;
  s32 t = base < 0x3C ? 0x3C : base;
  if (600 < base) t = 600;
  for (PartyUnit* u : g.party_units) {
    if (unit_is_down(u)) continue;
    s32 v = t;
    if (u->status_test(0, 0x800) != 0) v = (t << 1) / 3;
    if (u->status_test(0, 0x1000) != 0) v /= 3;
    s16 run = static_cast<s16>(v);
    if (u->status_test(2, 0x20000) != 0) run = 4;
    u->flee_timer = run;
  }
  g.party_escaping = 1;
}

// ---- The member select (input mode 1) ----

void input_open_member_select(BattleInput* in) {
  BattleGlobals& g = bg();
  in->mode = 1;
  g.flags = static_cast<u8>(g.flags | 2);
  if (in->member_t != 0) input_hide_member_cursor(in);
  if (in->member_cursor != nullptr) sprite_free(in->member_cursor);
  in->member_cursor = sprite_spawn_ui(in->member_purpose != 0 ? 0x22 : 3, 0, nullptr);
  in->repeat_delay = 0;
  in->camera_return = 0;
  in->bounce = 0;
  in->saved_slot = g.controlled_slot;
  camera_set_mode(g.camera, 2, nullptr);
}

void input_member_select_update(BattleInput* in) {
  BattleGlobals& g = bg();
  int step = 0;
  if (in->repeat_delay != 0) {
    const s32 t = in->repeat_delay - g.frame_ticks;
    in->repeat_delay = static_cast<s8>(t);
    if (static_cast<s8>(t) < 0) in->repeat_delay = 0;
  }
  if ((in->repeat & 0x8000) != 0 && in->repeat_delay == 0) step = -1;
  if ((in->repeat & 0x2000) != 0 && in->repeat_delay == 0) step = 1;
  if ((in->repeat & 0x1000) != 0 && in->repeat_delay == 0) step = -2;
  if ((in->repeat & 0x4000) != 0 && in->repeat_delay == 0) step = 2;
  auto back_to_idle = [&]() {
    g.flags = static_cast<u8>(g.flags & 0xFD);
    in->member_t = 8;
    in->member_t2 = 0;
    if ((g.flags & 0x40) == 0) camera_end_mode(g.camera, 2, 1, nullptr);
  };
  auto leader_follow = [&]() {
    const u8 s = in->saved_slot;
    if (g.leader_slot != s && g.controlled_slot != s) g.leader_slot = s;
  };
  // Cancel while choosing for a spell / item: back to that menu.
  if ((g.effects_suppressed != 0 || g.state != kStateFighting || (in->pressed & key(1)) != 0) && in->member_purpose != 0) {
    g.controlled_slot = in->saved_slot;
    in->member_t = 8;
    in->member_t2 = 0;
    bsfx_play_ui(6);
    in->mode = 4;
    if (in->member_purpose < 3) spell_menu_open();
    else item_menu_open();
    return;
  }
  if ((in->pressed & key(0)) != 0) {
    const u8 purpose = in->member_purpose;
    if (purpose != 0) {
      const u32 picked = g.controlled_slot;
      PartyUnit* who = g.party_units[picked & 3];
      bool refuse = false;
      if (purpose == 1 || purpose == 3) refuse = (who->st()->status & 1) != 0;  // a living member
      else if (purpose == 2 || purpose == 4) refuse = !unit_is_down(who);    // a fallen one
      if (refuse) {
        bsfx_play_ui(7);
        return;
      }
      g.controlled_slot = in->saved_slot;
      bsfx_play_ui(2);
      if (purpose < 3) {
        PartyUnit* caster = g.party_units[g.controlled_slot & 3];
        caster->target = who;
        caster->command = 4;
        caster->spell = g_cmd.chosen_spell;
        in->mode = 0;
        back_to_idle();
        leader_follow();
        return;
      }
      item_use_battle(g.party_units[g.controlled_slot & 3], who, g_cmd.chosen_item);
      in->mode = 0;
      back_to_idle();
      leader_follow();
      return;
    }
  }
  if (in->member_purpose == 0) {
    if (g.effects_suppressed != 0 || g.state != kStateFighting || (in->pressed & (key(1) | key(3))) != 0) {
      if ((in->pressed & key(1)) != 0) bsfx_play_ui(6);
      in->mode = 0;
      back_to_idle();
      leader_follow();
      return;
    }
    PartyUnit* u = controlled();
    if ((in->pressed & key(2)) != 0 && (g.flags & 0xC5) == 0 && g.party_escaping == 0 && u->status_test(3, 0x2000) != 1) {
      bsfx_play_ui(2);
      in->mode = 3;
      if (in->member_t != 0) input_hide_member_cursor(in);
      in->member_t = 8;
      in->member_t2 = 0;
      input_ring_menu_open(in);
      leader_follow();
      return;
    }
    if ((in->pressed & key(0)) != 0 && (g.flags & 0xC5) == 0 && u->busy == 0 && !unit_is_down(u) && u->airborne != 0 &&
        (u->status_bits & 0x10) == 0) {
      bsfx_play_ui(3);
      in->target_purpose = 0;
      input_open_target_select(in);
      back_to_idle();
      leader_follow();
      return;
    }
    g.member_target[g.controlled_slot & 3] = g.target_enemy;
  }
  int mag = step < 0 ? -step : step;
  bool moved = true;
  if (mag == 1) {
    for (int i = 0; i < 4; ++i) {
      g.controlled_slot = static_cast<u8>((g.controlled_slot + step) & 3);
      const PartyUnit* p = g.party_units[g.controlled_slot];
      if (p != nullptr && p->removed == 0) break;
    }
  } else if (mag == 2) {
    // Up / down: the members by depth (z), front to back.
    int order[4];
    for (int i = 0; i < 4; ++i) {
      const PartyUnit* p = g.party_units[static_cast<size_t>(i)];
      order[i] = (p == nullptr || p->removed != 0) ? 0 : i + 1;
    }
    for (int i = 0; i < 4; ++i) {
      for (int j = i; j < 4; ++j) {
        if (order[j] != 0 && order[i] != 0 &&
            g.party_units[static_cast<size_t>(order[i] - 1)]->pos[2] < g.party_units[static_cast<size_t>(order[j] - 1)]->pos[2])
          std::swap(order[i], order[j]);
      }
    }
    u32 at = 0;
    for (u32 i = 0; i < 4; ++i)
      if (static_cast<u32>(order[i] - 1) == g.controlled_slot) at = i;
    int guard = 0;
    do {
      at = step < 0 ? (at - 1) & 3 : (at + 1) & 3;
    } while ((order[at] == 0 || g.party_units[static_cast<size_t>(order[at] - 1)] == nullptr) && ++guard < 8);
    g.controlled_slot = static_cast<u8>(order[at] - 1);
  } else {
    moved = false;
  }
  if (moved) {
    if ((g.flags & 0x40) == 0) camera_set_mode(g.camera, 2, nullptr);
    in->repeat_delay = 8;
    bsfx_play_ui(1);
  }
  if (in->member_purpose == 0) g.target_enemy = g.member_target[g.controlled_slot & 3];
  if (in->bounce < 0xE) {
    if (in->member_cursor != nullptr) in->member_cursor->scale_y = battle_s16(0x8006CD70u + (in->bounce & 0xFE));
    in->bounce = static_cast<u8>(in->bounce + g.frame_ticks);
  } else {
    if (in->member_cursor == nullptr) return;
    in->member_cursor->scale_y = 0x1000;
  }
  if (in->member_cursor == nullptr) return;
  PartyUnit* c = controlled();
  sprite_attach_screen(in->member_cursor, c, 0, c->final_scale_y * -0x24 >> 12);
}

}  // namespace ss::game::battle
