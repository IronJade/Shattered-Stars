#pragma once

#include <vector>

#include "core/types.h"
#include "game/common/menu_core.h"
#include "game/common/menu_gfx.h"
#include "game/common/menu_stats.h"
#include "game/common/screen.h"

// The menu framework's widget classes (tri-Ace's class library). A Widget is a
// window on the scene's window list with its own ordering table, spliced into the
// scene's table at its depth; it owns child tasks (text boxes, a hand cursor) in a
// second window list. Input reaches the focused widget only; a child widget's result
// (confirm / cancel) comes back through on_child_done.
//
// Virtual functions keep the original vtable order; each names its slot and the
// title address of the implementation (field copies in the 0xba5 overlay).
namespace ss::game::menu {

class Widget;
class Cursor;
class ItemViewer;

// ---- Text box (title 0x8006e5e0, 0x84 bytes, vtable 0x80074a78) ----------------------
// A framed panel with a list of elements (ASCII strings, rendered text lines, icons,
// sprites) and an optional text renderer. Lives in its owner's child list (layer 2).
struct BoxElement {      // 0x24 bytes
  s16 kind = 0;          // +0x00: 1 ASCII, 2 text line, 4 icon (POLY_FT4), 5 sprite, 6-8 numbers
  u16 flags = 0;         // +0x02: 2 owns its data, 4 ...
  u16 state = 0;         // +0x04: text line index (kind 2), animation (kind 4), scale (kind 5)
  void* data = nullptr;  // +0x08
  std::vector<u8> owned; // +0x08 when the element owns a copy
  s16 x = 0;             // +0x0c
  s16 y = 0;             // +0x0e
  s16 a = 0;             // +0x10: frames (kind 4) / graphics + 1 (kind 5)
  s16 b = 0;             // +0x12: frame delay (kind 4) / animation (kind 5)
  u16 c = 0;             // +0x14: kind 5 flags
  s16 line_step = 0;     // +0x12 for kind 1 (line spacing)
  void* prims = nullptr; // +0x1c / +0x20 (kind 1: first / end of its sprites)
  s32 number = 0;        // +0x14: kind 7 experience to the next level
  s16 n18 = 0;           // +0x18: kind 7 percent of the level done
  s16 n1a = 0;           // +0x1a: kind 7 level
  s16 status[4] = {};    // +0x14..+0x1a: kind 6 HP, max HP, MP, max MP (state 0x10)
};

// The list a box shows (+0x5c, 0x26 bytes; title 0x8006e940 / menu_0ba5 0x800daf28):
// v[0] scroll position in pixels, v[1] item count, v[2] highest top row, v[3] visible
// rows, v[4] text lines (visible + 1, a ring), v[5] line width, v[6] row height, v[7] /
// v[8] x / y of the rows in the box, v[9..] the text line of each ring slot.
struct ListScroll {
  s16 v[19] = {};
  s16& pos() { return v[0]; }
  s16 line(int slot) const { return v[9 + slot]; }
};

class TextBox : public MenuTask {
 public:
  TextBox();                                            // title 0x8006e5e0
  ~TextBox() override;                                  // title 0x8006e6a4
  int type() override { return 0x102; }                 // [2] title 0x8006e6f0
  void close() override;                                // [3] title 0x8006e728
  void on_close() override;                             // [4] title 0x8006e6f8
  void update() override;                               // [5] title 0x8006ec8c
  virtual void draw_background() {}                     // [6] title 0x800703cc
  virtual void draw_decoration() {}                     // [7] title 0x800707f4

  void set_colours(const u32* colours);                 // title 0x8006e760 (null: the config's)
  void set_flags(u16 f);                                // title 0x8006e7ac (bit 15 clears)
  void reset();                                         // title 0x8006e7e4
  void clear();                                         // title 0x8006e80c
  void set_rect_centered(int w, int h, int anim);       // title 0x8006eb44
  void set_rect(int x, int y, int w, int h, int anim);  // title 0x8006ebb0 / 0x8006ebfc
  // title 0x8006eac0 / menu_0ba5 0x800db0a8: w x h centred on x 0x9c, just under 'above'.
  void set_rect_below(int w, int h, const TextBox* above, int anim);
  // title 0x8006eb28 / menu_0ba5 0x800db110: moves the resting position.
  void move_base(s16 by_x, s16 by_y) { base_x = static_cast<s16>(base_x + by_x), base_y = static_cast<s16>(base_y + by_y); }
  void draw();                                          // title 0x8006fe0c
  void draw_scrollbar();                                // title 0x800703d4
  void draw_frame();                                    // title 0x800707fc
  void draw_fill();                                     // title 0x800710c8
  int add_string(const char* text);                     // title 0x8006f01c
  void pen_reset() { pen(0x10, 8, 0xE); }               // title 0x8006f048
  void pen_move(s16 by_x, s16 by_y) { pen_x = static_cast<s16>(pen_x + by_x), pen_y = static_cast<s16>(pen_y + by_y); }  // title 0x8006f070
  void pen_at(s16 at_x, s16 at_y) { pen_x = at_x, pen_y = at_y; }  // title 0x8006f08c
  void pen(s16 at_x, s16 at_y, s16 step) { pen_x = at_x, pen_y = at_y, line_step = step; }  // title 0x8006f0c0
  int add_string_at(const s16 offset[2], const char* text);  // title 0x8006f0d0
  TextRenderer* text_renderer();                        // title 0x8006f218 (creates it)
  void set_group(s16 group);                            // title 0x8006f26c
  void set_font_group(const u8* font, const u8* strings, s16 group);  // title 0x8006f2b0
  int add_message(s16 index);                           // title 0x8006f4e8
  void add_message_lines(int index, int colour);        // title 0x8006f53c
  int add_text_line(const u8* text);                    // title 0x8006f730
  // title 0x8006f3c8 / menu_0ba5 0x800db9b0: the item's name (item text group 3) at the
  // pen, then "x" and the count held 0x6a / 0x6e pixels on; the font and group stay.
  void add_item_count(int item);
  // title 0x8006f31c / menu_0ba5 0x800db904: the item's name (item text group 3); the
  // font and group stay.
  void add_item_name(int item);
  // title 0x8006f628 / menu_0ba5 0x800dbc10: an ASCII text as a text line, each card code
  // a message of group 1; the group stays. Returns the line's element.
  int add_ascii_text(const char* ascii);
  // title 0x800732c0 / menu_0ba5 0x800df8a8: a gouraud line (colours scaled by the box's
  // brightness) into the box's OT entry.
  void draw_gradient_line(s16 x0, s16 y0, s16 x1, s16 y1, u32 c0, u32 c1);
  int add_sprite(BoxElement& e);                        // title 0x8006f88c
  int add_character(s16 gfx, s16 flags, s16 anim, s16 scale);  // title 0x8006f930
  int add_party_character(int character, s16 anim, s16 scale);  // title 0x8006f97c
  int add_icon(const PolyFT4Raw& poly);                 // title 0x8006fa58
  int add_icon_anim(const PolyFT4Raw& poly, s16 frames, s16 delay);  // title 0x8006fa7c
  int add_element(BoxElement e);                        // title 0x8006fd10
  // title 0x8006e940 / menu_0ba5 0x800daf28: a scrolling list of 'count' items showing
  // 'visible' rows of 'width' pixels, 'step' apart from (x, y); allocates visible + 1 blank
  // text lines. Returns the rows shown (count when smaller).
  int set_list(s16 count, s16 rows, s16 width, s16 step, s16 x, s16 y);
  // title 0x8006fb68 / menu_0ba5 0x800dc150: HP and MP of party slot 'slot' with gauges.
  int add_party_status(s16 slot);
  // title 0x8006fbc8 / menu_0ba5 0x800dc1b0: experience to the next level with a gauge of
  // 'percent' (or MASTER from level 255).
  int add_level(s16 level, s32 exp_to_next, s16 percent);
  // A kind 8 element: text lines first .. first + count - 1 are an option list whose
  // current one (*value; bit 15 none) is lit and framed by a glowing quad.
  int add_list_highlight(s16* value, s16 first, s16 count);
  // menu_0ba5 0x800dc29c: the same from the next text line on.
  int add_list_highlight(s16* value, s16 count) { return add_list_highlight(value, text ? text->line_count() : 0, count); }
  // title 0x800733d0 / menu_0ba5 0x800df9b8: underline text line *value + base.
  void set_highlight(s32 base, s16* value) { unknown7c = base, highlight = value; }
  // title 0x800733dc / menu_0ba5 0x800df9c4: the same from the next text line on.
  void set_highlight_next(s16* value) { unknown7c = text ? text->line_count() : 0, highlight = value; }

  // Element drawers.
  void draw_ascii(BoxElement& e);                       // title 0x80072538 (kind 1)
  void draw_icon(BoxElement& e);                        // title 0x800715bc (kind 4)
  void draw_sprite(BoxElement& e);                      // title 0x80071344 (kind 5)
  void draw_party_status(BoxElement& e);                // title 0x80071810 (kind 6)
  void draw_level(BoxElement& e);                       // title 0x80071aa0 (kind 7)
  void draw_highlight();                                // title 0x8006fe0c (+0x80)
  void draw_list_highlight(BoxElement& e);              // title 0x80071b74 (kind 8)
  // title 0x8007293c: 'value' right-aligned in 'width' 8x8 digits from (x, y), blank
  // padded, palette row 'colour'.
  void draw_number(s32 value, s16 at_x, s16 at_y, int width, int colour);
  // title 0x800727a0: an ASCII string in 8x8 glyphs with the box's font palette.
  void draw_text_at(const char* s, s16 at_x, s16 at_y);
  // title 0x80071e40 (HP / MP) and 0x800721c0 (experience): a gauge 'w' pixels wide.
  void draw_gauge(s16 at_x, s16 at_y, int w, int value, int max, bool exp);

  s16 pulse = 0;             // +0x08: phase of the kind 8 highlight glow
  Widget* owner = nullptr;   // +0x0c
  u32 colour = 0;            // +0x10: the owner's brightness colour (refreshed every frame)
  u16 frame_clut = 0x7910;   // +0x18
  u16 font_clut = 0x7851;    // +0x1a
  u16 flags = 0;             // +0x1c: 4 placed, 0x100 hidden, 0x1000 no frame, 0x2000 no border
  s16 anim_count = 0;        // +0x28
  s16 anim = 0;              // +0x2a: 1 opening, 2 closing
  s16 pen_x = 0xC, pen_y = 0xC, line_step = 0xC;  // +0x2c / +0x2e / +0x30
  s16 visible = 0;           // +0x32
  s16 unknown34 = 0;         // +0x34
  u32 colours[4] = {};       // +0x38: background gradient corners
  s32 x = 0, y = 0;          // +0x48 / +0x4c
  std::vector<BoxElement> elements;  // +0x50 (capacity +0x14, count +0x16)
  TextRenderer* text = nullptr;      // +0x54
  RenderContext* ctx = nullptr;      // +0x58: the owner's
  ListScroll* scroll = nullptr;      // +0x5c
  u16 slot = 5;                      // +0x60: ordering-table entry
  s16 base_x = 0, base_y = 0, base_w = 0, base_h = 0;  // +0x62..+0x68
  s16 rx = 0, ry = 0, rw = 0, rh = 0;                  // +0x6a..+0x70
  s16 dx = 0, dy = 0;                                  // +0x72 / +0x74: scroll offset
  s16 unknown7a = 0;                                   // +0x7a
  s32 unknown7c = 0;                                   // +0x7c
  s16* highlight = nullptr;                            // +0x80
};

// A text box with a gradient title bar (vtable 0x80074a38; the panels' help bar).
class HeaderBox : public TextBox {
 public:
  ~HeaderBox() override = default;         // title 0x800736f0
  void draw_decoration() override;         // [7] title 0x80072fbc
};

// A text box with a red title strip (vtable 0x800749f8; "Data Load Check").
class TitledBox : public TextBox {       // title 0x80072ad4
 public:
  TitledBox();
  void draw_decoration() override;         // [7] title 0x80072b18
};

// A message box sized to its text, with a "next" arrow (vtable 0x800749b8).
class MessageBox : public TextBox {      // title 0x8007343c
 public:
  MessageBox();
  void show(int index);                    // title 0x80073498
};

// title 0x80073498: fills a box with message 'index' of its text group, sized to the
// text and centred, with a "next" arrow (the save menu calls it on plain text boxes).
void message_box_show(TextBox* box, int index);

// Gradient quad helper (title 0x80073204).
void box_gradient(TextBox* box, u32 c0, u32 c1, u32 c2, u32 c3, s16 x, s16 y, s16 w, s16 h, int slot);

// ---- Hand cursor (title 0x80069184, 0x8c bytes, vtable 0x80074570) --------------------
// Moves between positions of up to eight groups (grids or scrolling lists); the
// active group reads the owner's pad repeats.
class CursorGroup {   // title 0x80069e90 (0x34 bytes), vtable +0x14
 public:
  virtual ~CursorGroup() = default;
  virtual void begin();      // [1] title 0x8006a004
  virtual void end();        // [2] title 0x8006a028
  virtual void input() {}    // [3] title 0x8006a04c
  virtual void place() {}    // [4] title 0x8006a054
  void update(u32 pressed);  // title 0x80069eac

  s16* value = nullptr;      // +0x00
  Cursor* cursor = nullptr;  // +0x04
  u32 pressed = 0;           // +0x08
  s16 index = 0;             // +0x0c
  u16 flags = 0;             // +0x0e: the cursor's flags while updating
  u16 state = 0;             // +0x10
};

class GridGroup : public CursorGroup {   // title 0x80069a80 (0x34 bytes, vtable 0x800745c8)
 public:
  void begin() override;     // [1] title 0x8006a388
  void input() override;     // [3] title 0x8006a3c8
  void place() override;     // [4] title 0x8006a60c
  void move(int delta);      // title 0x8006a4e8
  u16 mode = 0;              // +0x18: 1 wrap, 2 linked to a list scroll
  TextBox* box = nullptr;    // +0x1c
  s16 cols = 0, rows = 0;    // +0x20 / +0x22
  s16 x = 0, y = 0;          // +0x24 / +0x26
  s16 dx = 0, dy = 0;        // +0x28 / +0x2a
  ListScroll* scroll = nullptr;  // +0x2c
  s16 limit = 0;             // +0x30
};

// A grid linked to a box's list (mode 2; title 0x8006a710 / menu_0ba5 0x800d4400, 0x38
// bytes, vtable 0x800745a0): moving past the edge or the page buttons (config 8 / 10)
// scroll the list, which glides 4 pixels a frame toward the target.
class ScrollGridGroup : public GridGroup {
 public:
  void begin() override;     // [1] menu_0ba5 0x800d443c
  void end() override;       // [2] menu_0ba5 0x800d4520
  s16 target = 0;            // +0x34: scroll position aimed at
};

class ListGroup : public CursorGroup {   // title 0x8006a064 (0x2c bytes, vtable 0x800745f0)
 public:
  void begin() override;     // [1] title 0x8006a1d8
  void end() override;       // [2] title 0x8006a22c
  void input() override;     // [3] title 0x8006a0a0
  void place() override;     // [4] title 0x8006a130
  s16 count = 0;             // +0x18: visible rows
  s16 x = 0, y = 0, dy = 0;  // +0x1a / +0x1c / +0x1e
  s16 max_scroll = 0;        // +0x20 (quarter rows)
  s16 scroll_pos = 0;        // +0x22
  s16* scroll_var = nullptr; // +0x24
  u16 edge = 0;              // +0x28
};

class Cursor : public MenuTask {
 public:
  Cursor();                                   // title 0x80069184
  ~Cursor() override;                         // title 0x80069240
  int type() override { return 0x101; }       // [2] title 0x800692cc
  void on_close() override;                   // [4] title 0x800692f4
  void update() override;                     // [5] title 0x8006939c
  void draw();                                // title 0x800695a4
  void draw_hand(s16 x, s16 y, int cell, u32 colour);  // title 0x8006975c
  void set_flags(u16 f);                      // title 0x80069324 (bit 15 clears)
  bool take_flags(u16 f);                     // title 0x8006935c
  void prev_group();                          // title 0x80069984
  void next_group();                          // title 0x800699d0
  // title 0x80069a80 / 0x80069ba8: a grid of cols x rows positions from (x, y), step (dx, dy).
  void add_grid(s16* value, u16 mode, s16 cols, s16 rows, s16 x, s16 y, s16 dx, s16 dy, TextBox* box);
  // title 0x80069c78: a list of 'count' entries showing 'visible' at once.
  void add_list(s16* value, s16* scroll_var, s16 count, s16 visible, s16 x, s16 y, s16 dy);
  void move_to(s16 x, s16 y);                 // title 0x80069da4
  void jump_to(s16 x, s16 y);                 // title 0x80069dbc
  void attach(CursorGroup* g);                // title 0x80069dc8
  void clear_groups();                        // title 0x80069e04 / menu_0ba5 0x800d3af4

  Widget* owner = nullptr;           // +0x08
  RenderContext* ctx = nullptr;      // +0x0c
  CursorGroup* groups[8] = {};       // +0x10
  u16 flags = 0x84;                  // +0x30
  s16 anim = 0;                      // +0x32
  s16 active = 0;                    // +0x34
  s16 x = -1, y = -1;                // +0x36 / +0x38
  s16 from_x = -1, from_y = -1;      // +0x3a / +0x3c
  s16 to_x = -1, to_y = -1;          // +0x3e / +0x40
  s16 trail_x[16] = {}, trail_y[16] = {};  // +0x42 / +0x62
  s16 sound_delay = 0;               // +0x82
  s16 sound = 0;                     // +0x84
  u16 clut = 0;                      // +0x86
};

// ---- Widget (title 0x80073754, 0x48 bytes, vtable 0x80074ad0) -------------------------
class Widget : public MenuTask {
 public:
  Widget();                                    // title 0x80073754
  ~Widget() override;                          // [1] title 0x80073884
  void close() override { on_close(); }       // [3] title 0x80073a38
  void on_close() override;                    // [4] title 0x80073910
  void update() override { draw(); }           // [5] title 0x80073a6c
  virtual void draw();                         // [6] title 0x80073d4c
  virtual void draw_list(TextBox* box);        // [7] title 0x80073adc
  virtual void draw_item(TextBox*, int, int) {}  // [8] title 0x80073d3c
  virtual void frame();                        // [9] title 0x80073f24
  // [10] title 0x80073f1c: a child opened with open_child finished; 'state' is its
  // result_state (2 cancelled, 3 confirmed) and 'result' its result.
  virtual void on_child_done(int, int, int) {}
  virtual void input();                        // [11] title 0x80074008
  virtual void on_cursor(int) {}               // [12] title 0x800742d0
  virtual void on_confirm(int);                // [13] title 0x800742d8
  virtual void on_cancel(int n);               // [14] title 0x800742e4
  virtual void on_button2(int n);              // [15] title 0x80074314
  virtual void on_button3(int n);              // [16] title 0x80074334
  virtual void on_held(u32) {}                 // [17] title 0x80074354
  virtual void on_pressed(u32) {}              // [18] title 0x8007435c
  virtual void on_left_right(int n);           // [19] title 0x80074364
  virtual void on_shoulder(int n);             // [20] title 0x80074380

  void open_child(s16 id, Widget* widget) { child_id = id, child = widget; }  // title 0x80073f10
  void finish(s16 code);                       // title 0x8007439c: confirm with a result
  void cancel();                               // title 0x800743e8
  void set_result(s16 code);                   // title 0x80074438
  // title 0x80074494 / menu_0ba5 0x800e0a7c: a fresh hand cursor; returns the old one.
  Cursor* push_cursor();
  // title 0x800744dc / menu_0ba5 0x800e0ac4: closes the current cursor and restores 'old'.
  void pop_cursor(Cursor* old);

  Widget* parent = nullptr;          // +0x08: the focused window when it was created
  RenderContext* ctx = nullptr;      // +0x0c
  WindowList* children = nullptr;    // +0x10
  Cursor* cursor = nullptr;          // +0x14
  u32 colour = 0x01808080;           // +0x20
  u32 repeat = 0;                    // +0x24
  u32 pressed = 0;                   // +0x28
  u16 flags = 0;                     // +0x2c: 2 keep input, 8 input off, 0x100 unfocusable on close
  u16 result_state = 0;              // +0x2e: 2 cancelled, 3 confirmed
  s16 result = 0;                    // +0x30
  u16 depth = 0;                     // +0x32
  s16 unknown34 = 0;                 // +0x34
  s16 sound = 0;                     // +0x36: effect to play this frame
  s16 input_delay = 0;               // +0x38
  Widget* child = nullptr;           // +0x3c
  s16 child_id = -1;                 // +0x40
  u16 help_flags = 0;                // +0x42
  s16 pending = -1;                  // +0x44
  u8 closing = 0;                    // +0x46
  // The child's result when it was destroyed before this widget looked at it.
  u16 child_state_cache = 0;
  s16 child_result_cache = 0;
};

// ---- Dialog (title 0x8006aa6c, 0x50 bytes, vtable 0x800746e8) -------------------------
// A child window that opens with 4 frames of disabled input and closes after 6.
class Dialog : public Widget {
 public:
  Dialog();                                    // title 0x8006aa6c
  ~Dialog() override = default;                // title 0x8006aac4
  int type() override { return 0x12; }         // [2] title 0x8006aaec
  void close() override;                       // [3] title 0x8006aaf4
  void frame() override;                       // [9] title 0x8006ab68
  s16 opening = 1;                             // +0x48
  s16 open_delay = 4;                          // +0x4a
  s16 closing_anim = 0;                        // +0x4c
  s16 close_delay = 0;                         // +0x4e
};

// A dialog that returns on confirm (vtable 0x80074640 / 0x80067ab4 / 0x8006768c).
class ConfirmDialog : public Dialog {
 public:
  void on_confirm(int n) override;             // [13] title 0x8006ac24
  void on_cancel(int) override { sound = 0; }  // [14] title 0x8006ac64: cancel is ignored
};
void dialog_show_message(Dialog* d, s16 message, int arg);   // title 0x8006ac6c

// Field 0x80034ba0, which the field's Panel frame calls before its widgets: the query
// page's TIME / PRESS gauge (none on the title).
void set_panel_frame_hook(void (*hook)(const RenderContext* ctx));

// ---- Panel (title 0x8006acfc, 0x98 bytes, vtables 0x80074898 / 0x80074888) -----------
// A full-screen menu page: fades in and out, owns the help bar at the bottom
// (page name and the game logo) and a member box; embeds a StatCalc (+0x48).
class Panel : public Widget {
 public:
  Panel();                                     // title 0x8006acfc
  ~Panel() override;                           // title 0x8006ad98
  int type() override { return 0x11; }         // [2] title 0x8006adf8
  void close() override;                       // [3] title 0x8006ae00
  void frame() override;                       // [9] title 0x8006ae54
  void on_button3(int n) override;             // [16] title 0x8006aea4
  void on_shoulder(int n) override;            // [20] title 0x8006be00
  virtual void on_member_changed();            // [21] title 0x8006bdcc
  virtual void draw_help();                    // [22] title 0x8006bb5c
  virtual void draw_member();                  // [23] title 0x8006bd40
  // [24] title 0x8006ae9c (empty) / menu_0ba5 0x800d4d60: the viewer's item into the info
  // box: its description (info_mode 0) or its figures (show_item_stats).
  virtual void show_viewer_info();
  virtual void build_members();                // [25] title 0x8006bc5c

  void fade_step();                            // title 0x8006af24
  void darken(int level);                      // title 0x8006afe0
  void create_help();                          // title 0x8006b164
  void help_anim();                            // title 0x8006b1f4
  void help_rebuild();                         // title 0x8006b340

  // Field panels (menu_0ba5): the item viewer and its info box.
  void create_viewer(s16 x, s16 y, s16 z);     // menu_0ba5 0x800d4b2c (0x800d4b04: 0x118, 0x4a, 0x18)
  // menu_0ba5 0x800d4b9c: a viewer made elsewhere (the creation pages' kind) as +0x50.
  void attach_viewer(ItemViewer* v, s16 x, s16 y, s16 z);
  void set_viewer_item(int item);              // menu_0ba5 0x800d4c1c
  // menu_0ba5 0x800d5b0c: a new member box (0, 0) 'width' (0: 0x68) x 0x50, filled.
  void create_member_box(int width);
  void create_info_box();                      // menu_0ba5 0x800d5e8c: +0x70 at (0x60, 8) 0xd8 x 0x50
  void show_item_stats(int item);              // menu_0ba5 0x800d5f0c
  void show_usable_stats(int item);            // menu_0ba5 0x800d5fc4
  void show_equipment_stats(int item);         // menu_0ba5 0x800d6154

  ItemViewer* viewer = nullptr;  // +0x50
  s32 info_mode = 0;             // +0x54: 0 description, 1 figures (2 with session +0x1f)
  s32 viewer_item = 0;           // +0x58
  s16 members[8] = {};        // +0x5c
  s16 member = 0;             // +0x6c
  s16 member_count = 0;       // +0x6e
  TextBox* info = nullptr;    // +0x70
  TextBox* member_box = nullptr;  // +0x7c
  TextBox* help = nullptr;    // +0x80
  s32 viewer_shown = 0;       // +0x84: the info box shows the viewer's item
  s16 shimmer = 0;            // +0x8a
  s16 arrow_frame = 0;        // +0x8c
  s16 logo_phase = 0;         // +0x8e
  s16 logo_state = 0;         // +0x90
  s16 unknown92 = 0;          // +0x92
  s16 fade = 1;               // +0x94: 1 in, -1 out
  s16 fade_count = 0x10;      // +0x96
  StatCalc stats;             // +0x48 (a second base class in the original, vtable 0x80074888)
};

}  // namespace ss::game::menu
