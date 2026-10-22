#pragma once

#include <cstddef>

#include "core/types.h"
#include "game/field/tasks.h"
#include "runtime/libgpu.h"

// The field's message system (field program 0x8005835c-0x8005ea4c; analysis notes:
// field_catalog part3 4.3 and 3.2): up to 20 message windows, typewriter text in the
// map font, control codes 0x4000-0x400f, the button prompt, scrolling, choices.
//
// Text is the map script's byte stream: b < 0x80 is a one-byte code, otherwise
// code = (b & 0x7f) | next << 7. Codes below 0x4000 are 1-based glyphs of the map
// font (map section 1, block 1: u32 count, u32 height (12), u8 width[count], then
// 12 u16 rows per glyph, bit 15 leftmost); 0 ends the string; 0x4000-0x400f are
// control codes (see msg_window_print). Every printed line is rendered with a
// one-pixel drop shadow into a 4-bit bitmap and uploaded into the window's text
// cells, 8x13-pixel cells in VRAM (0x1c0 + 2 * (i % 31), 0x100 + 13 * (i / 31)),
// which the window then draws as sprites (CLUT row 0x1ff from entry #3110).
//
// API for the script VM (ext 0x64-0x6f, 0x7f, 0x81, 0x83, 0xaa, 0xba/0xd2/0xf5,
// 0xdc-0xe0; field_catalog part3 6 and part4 4):
//   msg_window_open(msg_system(), x, y, w, h, text, window, hold)   ext 0x64-0x6d
//     (the VM passes hold 1, then sets msg_window(n)->hold = 1 for ext 0x68/0x69 and 0
//     for the others; ext 0x6c/0x6d also clear draw_frame and draw_body)
//   msg_window_open_default(msg_system(), text, window, hold)       ext 0x6f
//   msg_window_close(msg_system(), window)                          ext 0x66.. / 0x81
//   msg_window_status(msg_system(), window)                          wait state 2, ext 0x83
//     -1 no window; 1 idle (all text shown, held open); 4 printing; 6/7 waiting for
//     the confirm button (7 = a new page follows); 8 timed wait; 9 closing.
//   msg_window(window)->hold / draw_frame / draw_body / ... set by the opcodes
//   msg_window_push_move(msg_window(n), x, y, frames)               ext 0xba/0xd2/0xf5
//   choice_draw_cursor(ChoiceState*)                                 wait state 9
// For map loading: msg_set_map_font(font) after unpacking section 1 block 1, and
// msg_window_destroy(msg_system(), -1) before freeing the previous map's text.
// For the 0xfc2 side load: field_install_side_text(entry) / field_free_side_text().
namespace ss::game::field {

constexpr int kMsgWindowCount = 20;
constexpr int kMsgMaxLines = 16;

// A first-fit block of text cells (0x1ec bytes). The free list is circular with
// a sentinel inside MsgSystem (cells 0, used 1).
struct MsgCells {
  u16 cells = 0;                 // +0x000: block size in cells
  u8 used = 0;                   // +0x002
  s32 pos[kMsgMaxLines][3][2]{};  // +0x004: {col, row} of up to 3 runs per line
  u16 run_len[kMsgMaxLines][3]{}; // +0x184: cells in each run
  MsgCells* next = nullptr;      // +0x1e4
  MsgCells* prev = nullptr;      // +0x1e8
};

// One extra sprite on a line (+0xd8 of MsgLine): ruby glyphs of a 0x400a block
// (6x6 glyphs in the #3110 atlas) and the 0x400d icons (code = ~index).
struct MsgExtra {
  s16 x = 0;      // pixel x in the line
  s16 glyph = 0;  // index of the line glyph it belongs to
  s16 code = 0;   // >= 0: ruby glyph; < 0: ~icon
};

// A laid-out line (0x198 bytes).
struct MsgLine {
  u16 shown = 0;           // +0x00: glyphs revealed
  u16 remaining = 0;       // +0x02: glyphs still to reveal (the print cursor's count)
  u8 fade[64]{};           // +0x04: per-glyph fade-in 0..8 (when MsgWindow::fade_in)
  u16 glyph_x[64]{};       // +0x44: x of glyph i; glyph i covers [glyph_x[i], glyph_x[i + 1])
  u16* bitmap[3]{};        // +0xc4: upload buffers of the three runs (freed next update)
  s16 y_offset = 0;        // +0xd0: height - line height (5 for lines with ruby)
  s16 height = 0;          // +0xd2
  u8 unknown_d4 = 0;       // +0xd4
  u8 all_faded = 0;        // +0xd5
  s16 extra_count = 0;     // +0xd6
  MsgExtra extra[32]{};    // +0xd8
};

// Layout of one line (text_layout_line); index 5 + k holds the shift of the k-th
// 0x4009 choice block, overlapping 'centred' as in the original's int array.
struct TextLine {
  s32 v[64]{};
  s32& glyphs() { return v[0]; }
  s32& width() { return v[1]; }
  s32& ruby() { return v[3]; }
  s32& ended() { return v[4]; }   // ended by a new line / wait code
  s32& centred() { return v[6]; }
};

struct MsgMove {  // +0x6c ring entry (0xc bytes)
  s32 x = 0, y = 0;
  u16 frames = 0;
  u16 t = 0;
};

// A message window (0x168 bytes, msg_window_construct).
struct MsgWindow {
  s16 x = 0, y = 0, w = 0, h = 0;          // +0x00: text area in pixels
  s16 move_x = 0, move_y = 0;              // +0x08: start of the current move
  s16 draw_x = 0, draw_y = 0, draw_w = 0, draw_h = 0;  // +0x0c: drawn frame (animated)
  u32 colour[4]{};                         // +0x14: body gradient (config + 0x30..0x3c)
  u8 justify = 1;                          // +0x24: spread wrapped lines to the width (ext 0xaa)
  u8 speed = 0;                            // +0x25: text speed index (g_text_speed)
  u8 body_semi = 1;                        // +0x26: body drawn semi-transparent
  u8 state = 1;                            // +0x27: see msg_window_status
  const u8* pages[16]{};                   // +0x28: page ring
  u8 page_read = 0, page_write = 0;        // +0x68 / +0x69
  u16 page_offset = 0;                     // +0x6a: bytes of the current page consumed
  MsgMove moves[8]{};                      // +0x6c
  u8 move_read = 0, move_write = 0;        // +0xcc / +0xcd
  u16 unknown_ce = 0;                      // +0xce
  s16 timer = 0;                           // +0xd0
  MsgCells* cells = nullptr;               // +0xd4
  u16 text_lut[16]{};                      // +0xd8: 4 glyph bits -> 4 texels, text colour
  u16 shadow_lut[16]{};                    // +0xf8: the same in colour 1 (shadow)
  u16* text_lut_ptr = nullptr;             // +0x118: scratchpad copies used by text_blit_glyph
  u16* shadow_lut_ptr = nullptr;           // +0x11c
  MsgLine* lines = nullptr;                // +0x120: line_count records (a ring)
  s16 first_line = 0;                      // +0x124
  s16 used_lines = 0;                      // +0x126: lines laid out after the first
  s16 text_height = 0;                     // +0x128: height of the laid-out lines
  s16 page_height = 0;                     // +0x12a
  s16 scroll_remaining = 0;                // +0x12c
  s16 scroll_offset = 0;                   // +0x12e
  u8 draw_frame = 1;                       // +0x130
  u8 draw_body = 1;                        // +0x131
  u8 scroll_pending = 0;                   // +0x132: pixels to scroll after the wait (or 'render first line')
  s16 line_count = 0;                      // +0x134
  u8 ot_offset = 0;                        // +0x136: drawn at OT 0x810 + this
  u8 unknown_137 = 0;                      // +0x137
  u8 blink = 1;                            // +0x138: button prompt animation (mod 56)
  u8 hold = 0;                             // +0x139: stay open (state 1) after the last page
  u8 cancel_closes = 0;                    // +0x13a: the cancel button closes the window
  u8 fade_in = 0;                          // +0x13b: glyphs fade in (notice banner)
  u8 insert_start = 1;                     // +0x13c: next 0x4007/8/c/e starts an insertion
  u8 show_prompt = 1;                      // +0x13d: draw the button prompt while waiting
  u8 page_new_line = 1;                    // +0x13e: a new page starts on a fresh line
  u8 force_advance = 0;                    // +0x13f: leave a wait without the button
  u8 button_advance = 1;                   // +0x140: the confirm button ends waits
  s32 insert_remaining = 0;                // +0x144: glyphs left in the inserted string
  s16 pending_glyphs = 0;                  // +0x148: glyph count of the last rendered line
  u8 anchored = 0;                         // +0x14a: follows a projected 3D point
  u8 word_wrap = 1;                        // +0x14b
  u8 page_started = 0;                     // +0x14c
  s16 anchor[2]{};                         // +0x14e
  u8 custom_clip = 0;                      // +0x152
  u8 continuing = 0;                       // +0x153: the next line continues a wrapped one
  s32 line_height = 13;                    // +0x154
  void (*draw_callback)(int x, int y) = nullptr;  // +0x158: after the text, unless opening
  void (*close_callback)() = nullptr;      // +0x15c: when the closing animation ends
  psyq::RECT clip{};                       // +0x160
};

// The message system (0x20c bytes, vtable 0x800731a4, task priority 4).
struct MsgSystem : Task {
  MsgCells head;               // +0x008: free-list sentinel
  MsgCells* free_list = nullptr;  // +0x1f4: -> head
  s32 default_x = 0, default_y = 0, default_w = 0, default_h = 0;  // +0x1f8
  u8 tint[3]{};                // +0x208
  u8 tint_enabled = 0;         // +0x20b
  ~MsgSystem() override;
  void update() override;      // msg_system_update
};

// Globals.
MsgSystem* msg_system();                 // 0x80075288
MsgWindow* msg_window(int n);            // message_windows[n] (0x80075668)
void msg_set_map_font(const u8* font);   // g_map_font 0x80075714 / g_map_glyphs 0x80075718
const u8* msg_map_font();
extern s32 g_text_speed[5];              // 0x800751b8; [0] from 0x800751cc[game_state + 0x118]

MsgSystem* msg_system_create(MsgSystem* self);                       // 0x8005835c
void msg_system_destroy(MsgSystem* self);                            // 0x80058430
void msg_system_update(MsgSystem* self);                             // 0x800584f8
// Positive x/y/w are 8-pixel cells and positive h lines of 13 px (w clamped to
// 6..40 cells); negative values are pixels. Returns 0 when window n exists.
int msg_window_create(MsgSystem* msg, int x, int y, int w, int h, const u8* text, int window);  // 0x800586ac
void msg_window_open(MsgSystem* msg, int x, int y, int w, int h, const u8* text, int window, int hold);  // 0x80058838
void msg_window_open_default(MsgSystem* msg, const u8* text, int window, int hold);  // 0x800588cc
void msg_window_close(MsgSystem* msg, int window);                   // 0x80058964
void msg_window_destroy(MsgSystem* msg, int window);                 // 0x800589a0 (-1 = all)
void msg_window_destroy_ptr(MsgSystem* msg, MsgWindow* w);           // 0x80058a80
int msg_window_status(MsgSystem* msg, int window);                   // 0x80058b00 (-1 = any busy)
MsgCells* msg_cells_alloc(MsgSystem* msg, int cols, int lines);      // 0x80058bac
void msg_cells_free(MsgSystem* msg, MsgCells* cells);                // 0x80058d8c
void msg_window_resize(MsgSystem* msg, int window, int w, int h);    // 0x80058e58
// Accessors 0x80058f58-0x80058fbc.
u8* msg_system_tint(MsgSystem* msg);
void msg_system_set_tint_enabled(MsgSystem* msg, int on);

MsgWindow* msg_window_construct(MsgWindow* w);                       // 0x80058fc0
void msg_window_free(MsgWindow* w);                                  // 0x800590fc
void msg_window_init(MsgWindow* w, s16 x, s16 y, s16 w_px, int h_px, const u8* text);  // 0x80059190
void msg_window_begin_close(MsgWindow* w);                           // 0x800592a8
void msg_window_update(MsgWindow* w);                                // 0x800592d0
void msg_window_scroll(MsgWindow* w, int frames);                    // 0x800599c8
void msg_window_step_move(MsgWindow* w);                             // 0x80059acc
void msg_window_step_open_anim(MsgWindow* w);                        // 0x80059c84
// Typewriter: reveals up to 'chars' characters of the current page and runs the
// control codes it meets:
//   0x4000 new line; 0x4001 wait for the button, then a new page (scrolls the
//   window empty); 0x4002 wait for the button (300 frames with auto text, script
//   flag byte 0x5e bit 1); 0x4003 n: speed n (< 5, table) or a pause of n - 5
//   frames; 0x4004 n: text colour 2n + 3; 0x4005 centre the line; 0x4006 u16 timed
//   wait; 0x4007 v: name of the character in message variable v; 0x4008 v: 0xfc2
//   string (item name) of variable v; 0x4009 choice block; 0x400a .. 0x400b ruby
//   block; 0x400c c: name of character c; 0x400d i: fixed space / icon i;
//   0x400e v: variable v in decimal; 0x400f x: set the x position.
void msg_window_print(MsgWindow* w, int chars);                      // 0x80059db8
int msg_window_new_line(MsgWindow* w);                               // 0x8005a678
void msg_window_draw(MsgWindow* w);                                  // 0x8005a7bc
void msg_window_get_clip(MsgWindow* w, psyq::RECT* r);               // 0x8005c130
void msg_window_set_clip(MsgWindow* w, const psyq::RECT* r);         // 0x8005c1ec
void msg_window_set_text_colour(MsgWindow* w, int colour);           // 0x8005c21c
void msg_window_init_shadow_lut(MsgWindow* w);                       // 0x8005c298
int text_layout_line(MsgWindow* w, const u8* text, TextLine* out);   // 0x8005c2fc: returns the line height
int text_number_width(MsgWindow* w, int var, s32* digits);           // 0x8005c9e4
int text_item_name_width(MsgWindow* w, int var, s32* count);         // 0x8005ca94
int text_copy_char_name(MsgWindow* w, int ch, u8* out);              // 0x8005cb90
int text_format_number(MsgWindow* w, int var, u8* out);              // 0x8005cbf0
void text_find_item_name(MsgWindow* w, int var, const u8** out);     // 0x8005ccb8
void text_render_line(MsgWindow* w, const u8* text, int line, int height, TextLine* info);  // 0x8005cd44
int text_measure_block(MsgWindow* w, const u8** text);               // 0x8005dfb0
void text_blit_glyph(MsgWindow* w, int glyph, int pitch, u16* dst, u32 x);  // 0x8005e1a4
void msg_window_push_text(MsgWindow* w, const u8* text);             // 0x8005e594
void msg_window_push_move(MsgWindow* w, s32 x, s32 y, u16 frames);   // 0x8005e5d0
void msg_window_clear_line(MsgWindow* w, int line);                  // 0x8005e630
// Screen position (xy[2]), last glyph x and height of displayed line n (choices).
void msg_window_line_pos(MsgWindow* w, int line, s32* xy, u32* end_x, s32* height);  // 0x8005e6c8
void msg_window_set_anchor(MsgWindow* w, int on);                    // 0x8005e78c
int msg_window_get_state(MsgWindow* w);                              // 0x8005e7c8
int text_char_name_width(int ch, s32* count);                        // 0x8005e808
int text_ascii_to_glyph(s32* code, int next, int kana);              // 0x8005e8f8

// The projection the anchored windows follow (overlay 0x80086bbc on the BG renderer);
// set by whoever ports the BG renderer. Null leaves the point unchanged.
extern void (*g_msg_project_anchor)(s16* xy);

// Choices (ScriptCtx +0x10bc..+0x11ac). Ext 0xdc-0xe0 (options, first line, selection,
// cancel allowed, window; missing arguments 0 / 0 / 1 / 0) set window->cancel_closes =
// cancel allowed, window->show_prompt = 0, close_after = 0, cursor[0][0] = -1, slide = 0,
// and enter wait state 9. Wait state 9 (vm_thread_update), while the window's status is
// -1, 1, 6 or 7: choice_draw_cursor(c); confirm: sfx 2, R = selection, done; cancel:
// sfx 7 if not allowed, else sfx 6, R = -1, window->force_advance = 1, done; when done
// lines_per_option[] = 1 and, if close_after, window->show_prompt = 1. Otherwise, unless
// sliding, Up / Down (repeat; wrap on a fresh press) move the selection with sfx 1.
struct ChoiceState {
  s16 options = 0;            // +0x10bc: option count
  s16 first_line = 0;         // +0x10be: line of the first option
  s16 selection = 0;          // +0x10c0
  s16 cancel_allowed = 0;     // +0x10c2
  s32 cursor[16][2]{};        // +0x10c4: {x, y} of each line of the selection ({-1} = none yet)
  s16 slide = 0;              // +0x1144: 4-frame cursor slide
  s16 option_lines_drawn = 0; // +0x1146
  s16 line_width[16]{};       // +0x1148
  s16 line_height[16]{};      // +0x1168
  s16 close_after = 0;        // +0x1188
  s32 window = 0;             // ctx +0x34
  s16 lines_per_option[16]{}; // +0x11ac (reset to 1 after each choice)
};
void choice_draw_cursor(ChoiceState* c);                              // 0x8006afd8

// Entry 0xfc2 (menu strings + the second font, used by windows 10-19).
void field_install_side_text(const u8* entry, size_t size);           // 0x80053b44
void field_free_side_text();                                          // part of 0x8004c0cc
const u8* field_side_strings();                                       // 0x800762f8

// Pad bit -> offset of its icon in the #3110 button strip (0x80052f24): circle 0,
// triangle 0x10, square 0x20, cross 0x30, L1 0x40, R1 0x50, L2 0x60, R2 0x70.
int pad_button_icon_offset(u32 button);

// #3110's UV table (g_sys_icons 0x80075328): 8-byte records, +2 u0, +3 v0, +4 u1, +5 v1.
const u8* field_sys_icons();

}  // namespace ss::game::field
