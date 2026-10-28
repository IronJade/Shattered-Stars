#pragma once

#include <span>

#include "core/types.h"
#include "runtime/region.h"

// The per-language text rules of the releases (port feature; docs/port/notes/regions_text.md).
//
// North America and Europe share one text system. The French and German releases replaced
// the font and rewrote the code that knows glyph numbers: every map carries a 132-glyph
// Latin font (digits first, then A-Z, a-z, the accented letters, punctuation; FR == DE but
// for glyph 0x62, German u-umlaut), so text_ascii_to_glyph, the number formatting, the line
// layout's word-wrap classes and a few glyph tests in the typewriter changed; the name entry
// has one character set (capitals) and three buttons; a list of menu, title and battle
// layouts moved for the longer words. Everything else (the byte encoding, the control codes
// 0x4000-0x400f, the font block format, the window and cell code) is NA's.
//
// This module is the language-dependent part only, as plain functions and tables, so the
// ported functions can ask it instead of hard-coding NA's numbers. It holds no game data:
// tables the original keeps in its images (the title / battle ASCII tables, the keyboard
// characters, the 0x400d widths) stay there and are read through the region maps.
//
// Where the game asks it (docs/port/notes/regions_text.md section 8):
//   text_layout_line, text_ascii_to_glyph (field/message_text.cpp)  -> layout_line / ascii_to_glyph
//     for France / Germany (NA keeps its ported body)
//   text_number_width / text_format_number / text_render_line          -> map_glyphs
//   msg_window_print (field/message.cpp)                               -> map_glyphs push range
//   title / battle text_from_ascii                                     -> ascii_high_bias
//   the name keyboards (title/window.cpp, title/new_game.cpp, field/menu_rename.cpp)
//                                                                      -> name_keyboard, keyboard_button
//   cvram_alloc (common/menu_gfx.cpp)                                  -> cvram_choose
//   the layouts listed in Layout                                       -> layout(lang()).<field>
namespace ss::game::text {

using Lang = region::Text;

// The language of the release being played (region::current().text). Japanese is not
// supported yet; every function here treats it as North American.
Lang lang();

// ---- Map font glyph numbers (field text) ------------------------------------------------

struct MapGlyphs {
  u16 digit0;       // '0' (digits are digit0 + d): NA 0xaa, FR/DE 0x01
  u16 minus;        // '-': NA 0xb4, FR/DE 0x0b
  u16 space;        // the space text_render_line drops at the start of a wrapped line: NA 0x105, FR/DE 0x72
  u16 layout_skip;  // text_layout_line's test for the same (space + 2 in every release: NA 0x107, FR/DE 0x74)
  u16 push_first;   // msg_window_print: a glyph in [push_first, push_first + push_count) that would
  u16 push_count;   //   be a line's last starts the next line instead (opening brackets / quotes)
  u16 glyph_count;  // glyphs of the common map font (per-map extras follow): NA 276, FR/DE 132
};
const MapGlyphs& map_glyphs(Lang lang);

// text_ascii_to_glyph (field NA 0x8005e8f8, FR / DE 0x8005e704): the map-font glyph of an
// ASCII byte of a character name. 'next' is the following byte, 'kana' the '@' state (the
// half-width kana path, with the voiced marks 0xde / 0xdf, is the same in every release).
// Returns the extra bytes consumed (1 when a voiced mark was folded in). FR / DE map
// ' ' ! , . / 0-9 : ; = ? A-Z a-z and leave every other byte unchanged (NA: 0xfe).
int ascii_to_glyph(Lang lang, s32* code, int next, int kana);

// Port-only text (Archipelago messages: item and player names from other worlds, plain
// ASCII). France and Germany's ascii_to_glyph passes the bytes it doesn't map through as
// glyph numbers, which are letters in their map font ('-' 0x2d would show 'f', '(' an 'a').
// The map-font glyph of the closest character for such a byte: '-' 0x0b, brackets / braces /
// angle brackets 0x65-0x6c, '"' 0x7b, # $ % & * + 0x75-0x7a, '_' 0x7d, '|' 0x7e, '~' 0x7f,
// '`' as the apostrophe 0x27, '\' as '/', '@' and '^' (no such glyph) as a space; -1 for a
// byte ascii_to_glyph maps itself, and always -1 for North America / Europe (whose AP text
// keeps NA's behaviour: those bytes show NA's blank 0xfe).
s32 port_symbol_glyph(Lang lang, s32 code);

// text_format_number's digits: 'value' (already narrowed to s16 when the 0xfc2 strings are
// loaded) as glyph codes, '-' first when negative, 0-terminated; returns the glyph count.
int format_number(Lang lang, s32 value, u8* out);
// text_number_width: the width of that number in 'font' (glyph width + 1 each); '*digits'
// (optional) the glyph count.
int number_width(Lang lang, const u8* font, s32 value, s32* digits);

// ---- Line layout (text_layout_line, field NA 0x8005c2fc, FR / DE 0x8005c24c) ------------

// The window fields the layout reads.
struct LayoutWindow {
  int w = 0;            // MsgWindow::w: the text area; a line may be w - 16 pixels wide
  bool word_wrap = true;
  bool continuing = false;  // the line continues a wrapped one (its leading space is free)
  int line_height = 13;
};

// What the layout asks the game for (the field implements it on the message system).
class LayoutHost {
 public:
  virtual ~LayoutHost() = default;
  virtual int glyph_width(u32 code) = 0;                  // font[code + 7] of the current font
  virtual s32 message_variable(int var) = 0;              // script globals + 0x38 + 4 * var
  virtual int char_name_width(int ch, s32* count) = 0;    // text_char_name_width
  virtual int item_name_width(int var, s32* count) = 0;   // text_item_name_width
  virtual int number_width(int var, s32* count) = 0;      // text_number_width
  virtual int measure_block(const u8** text) = 0;         // text_measure_block (0x400a ruby)
  virtual int fixed_space_width(int index) = 0;           // the 0x400d width table (NA 0x80076318)
};

// Lays out one line of 'text' like the release's text_layout_line: out[0] glyphs, [1] width,
// [2] 0, [3] ruby glyphs + fixed spaces, [4] ended by a new-line / wait code, [6] centred,
// [5 + k] the shift of the k-th 0x4009 block. 'out' may be null (else 64 entries). Returns
// the line height (line_height, + 5 with ruby).
int layout_line(Lang lang, const LayoutWindow& w, LayoutHost& host, const u8* text, s32* out);

// The word-wrap classes the layout gives a glyph code (exposed for tests and tools):
// 0 other (a break opportunity before it), 1 kana, 2 digit, 3 letter, 4 kanji / NA codes
// >= 0x107, -1 punctuation that keeps the previous class, 5 an inserted name (0x400c).
// 'next' is the following code (FR / DE: no break before a space followed by '?' / '!').
int wrap_class(Lang lang, u32 code, u32 next);

// ---- Title / battle ASCII conversion ---------------------------------------------------

enum class Program : u8 { kTitle, kBattle, kRename };
// The title's and the battle's text_from_ascii look a byte up in their ASCII table. FR / DE
// added a branch for bytes >= 0x81 outside kana mode (Latin-1 accented letters): glyph =
// table[c] + this. -1: no branch (NA's code: table[c] for every byte, as in the rename page,
// which kept NA's copy in every release). Dormant in play: names are ASCII and the keyboards
// have no accented letters.
int ascii_high_bias(Lang lang, Program program);

// ---- Name entry (title name_keyboard_*, rename page rename_keyboard_*) --------------------

enum class KeyAction : u8 { kNone, kCapital, kLower, kOk, kDelete, kReturn };
struct NameKeyboard {
  int buttons;          // row 0 buttons: NA 5 (Capital, Lower, OK, Delete, Return), FR / DE 3 (OK, Delete, Return)
  int button_step;      // row-0 button and cursor x: 0xc + column * step (NA 0x2a, FR / DE 0x4c)
  int first_column;     // the cursor's column on row 0 when the keyboard is made (NA 2 = OK, FR / DE 1 = Delete)
  int ok_column;        // where the cursor jumps when the name is full (NA 2, FR / DE 0)
  bool charset_switch;  // L1 / L2 / R1 / R2 (and the Capital / Lower buttons) switch the character set;
                        // FR / DE draw set 0 only (digits, capitals, punctuation; 5 rows)
};
const NameKeyboard& name_keyboard(Lang lang);
KeyAction keyboard_button(Lang lang, int column);  // what Cross does on row-0 column 'column'

// ---- CLUT / text VRAM allocator (menu cvram_alloc, title 0x8006d8a4, #2981 0x800d9e8c) ----

struct FreeBlock {
  bool free;
  s16 w, h;
};
// The block cvram_alloc takes for a w x h request (1-based index, 0 = none): NA the free
// block that fits with the smallest height (first on ties); FR / DE the last fitting block
// whose width and height are both no larger than the best so far.
int cvram_choose(Lang lang, std::span<const FreeBlock> blocks, int w, int h);

// ---- Layouts that moved for the translations -----------------------------------------
// Each field names the ported function and the NA literal it replaces (docs/port/notes/regions_text.md
// has the instruction addresses).

struct Layout {
  // field/post_battle.cpp report_draw (field_equip_report_draw 0x8004b7a8)
  s16 report_heading_w;      // window 9 width in cells (NA 0x14)
  s16 report_items_dx;       // windows 10-14 dx (NA 0)
  s16 report_item_w;         // windows 10-19 width in cells (NA 0x14)
  // field/menu_options.cpp (#3016 options_draw_screen1 / 2)
  s16 options_s1_mono_dx;    // pen_move(0x70, -0xe) after group 5 message 0
  s16 options_s1_values_x;   // pen(0xa8, 0x16, 0xe) of group 6
  s16 options_s1_row8_dx;    // row 8's first move (0x38)
  s16 options_s1_rowc_dx;    // row 0xc's move (0x50)
  s16 options_s2_icon_x;     // the button icons' first x (0xb8)
  s16 options_s2_icon_dx;    // ... and their x step (0xe)
  s16 options_s2_vib_dx;     // pen_move(0x40, -0xe) after values_[7] message 0
  // field/menu_specialty.cpp
  s16 specialty_mp_x;        // show_costs box->pen(0x88, 0x40, 0xe)
  s16 link_combo_on_x;       // LinkComboDialog pen_at(0xa0, 0x1c)
  s16 link_combo_off_x;      // ... pen_at(0xc0, 0x1c)
  // field/menu_skills.cpp
  s16 skill_detail_x;        // show() pen_at(0xe0, 0x20)
  bool skills_heading_message;  // refresh_list: the heading is group 0xf message 0 (not the ASCII literal)
  s16 skills_heading_x[2];   // ... at these x (NA 0x38 / 0xcc)
  // field/menu_equip.cpp equip options dialog
  s16 equip_options_w;       // set_rect_centered(0xd0, 0x40)
  s16 equip_options_x[2];    // pen_at(0x80 / 0xa0, 0x2a)
  // common/menu_save.cpp
  s16 check_dialog_w;        // CheckDialog::setup set_rect_centered(200, 0x40)
  s16 save_slot_x;           // add_grid(..., 0xa8, 0xc, 0x40, ...) x
  s16 save_slot_dx;          // ... the 0x40
  s16 save_slot_label_x[2];  // rebuild_slots pen_at(0xb8 / 0xf8, 8)
  // title/window.cpp
  s16 sound_lines_stereo;    // SoundWindow::draw lines when selection == 2 (else 7)
  s16 vibration_heading_x;   // VibrationWindow::draw heading x (0x54)
  // title/voice_collection.cpp
  s16 digit_strip_v;         // draw_number's texture v (0x32)
  s16 voice_label_w;         // VOICE label w (0x2d)
  s16 exit_label[2];         // EXIT label w, v (0x1f, 0x50)
  s16 bgm_label[3];          // BGM label w, h, v (0x2e, 0xc, 0x32)
  s16 random_label[2];       // RANDOM label w, u (0x3f, 5)
  s16 stop_label[4];         // STOP label w, h, u, v (0x24, 0xd, 4, 0x60)
  // title/new_game.cpp (new_game_sequence)
  s16 sound_subtitle_x;      // line 243 (0x8c)
  s16 screen2_subtitle_x;    // line 336 (0x98)
  s16 screen3_subtitle_x;    // line 441 (0x80)
  s16 name_heading_w;        // line 579 (0x80)
  s16 name_subtitle_x;       // line 580 (0x80)
  s16 tpage7_heading_w;      // line 634 (0x86)
  s16 tpage7_subtitle_x;     // line 635 (0x88)
  s16 tpage8_heading_w;      // line 686 (0x96)
  s16 tpage8_subtitle_x;     // line 687 (0x90)
  bool tpage8_lit_selected;  // line 688: draw_choice_icons(..., 0 -> 1, ...)
  // field/menu_rename.cpp
  s16 rename_hand_u;         // the keyboard hand sprite u (0x80)
  s16 rename_cursor_u;       // name box cursor u (0xa0)
  s16 rename_blank_u;        // name box blank marks u (0x90)
  bool rename_extra_vram;    // rename_run also keeps VRAM (0x180, 0, 0x40, 0x40)
  s16 rename_heading_w;      // rename_screen heading sprite w (0x80)
  s16 rename_subtitle[4];    // subtitle sprite x, w, u, tpage (0x88, 0x80, 0x80, 0x25)
  // field/field_sub_overlays.cpp the_end
  s16 end_logo3_x1;          // the third logo quad x1 (0x26c)
  s16 end_logo3_u1;          // ... u1 (0x88)
  // battle
  bool result_prefix_order;  // result screen: message 5 / 8 first, then the item / skill name
  s16 scan_first_page;       // ScanWindow's first page (0 = the enemy's name; DE 1)
  // not ported (the disc-lid / disc-error notices; the port's disc never fails)
  s16 lid_notice_battle[2];  // battle hud_draw window x, w (0x3c, 0xc0)
  s16 lid_notice_field[3];   // field_main_loop window 5: x, y, w in cells (5, 8, 0x1e)
};
const Layout& layout(Lang lang);

}  // namespace ss::game::text
