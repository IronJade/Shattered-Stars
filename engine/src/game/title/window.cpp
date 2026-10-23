#include "game/title/window.h"

#include <cstring>

#include "game/boot/pad.h"
#include "game/boot/stubs.h"
#include "game/common/text_rules.h"
#include "game/title/text.h"
#include "game/title/title_program.h"
#include "runtime/libgpu.h"
#include "runtime/runtime.h"

namespace ss::game {
namespace {

void add_prim(Window* w, void* prim) { psyq::addPrim(w->ctx->ot_head, prim); }

template <typename T>
T* alloc(Window* w, size_t count = 1) {
  return screen_alloc_prims<T>(w->ctx->arena, count);
}

void set_rgb(u8* rgb, u32 colour) { std::memcpy(rgb, &colour, 3); }

void queue_draw_mode(Window* w, int dfe, int dtd, int tpage) {
  auto* p = alloc<psyq::DR_MODE>(w);
  psyq::SetDrawMode(p, dfe, dtd, tpage, nullptr);
  add_prim(w, p);
}

void queue_sprite(Window* w, int x, int y, int u, int v, int clut, int width, int height) {
  auto* p = alloc<psyq::SPRT>(w);
  psyq::setlen(p, 4);
  p->code = 0x65;
  p->x0 = static_cast<s16>(x);
  p->y0 = static_cast<s16>(y);
  p->u0 = static_cast<u8>(u);
  p->v0 = static_cast<u8>(v);
  p->clut = static_cast<u16>(clut);
  p->w = static_cast<s16>(width);
  p->h = static_cast<s16>(height);
  add_prim(w, p);
}

// A translucent gouraud quad; vertices 0/1 on top, 2/3 below (or left/right edges).
psyq::POLY_G4* queue_g4(Window* w, const u32 colours[4], const s16 xs[4], const s16 ys[4]) {
  auto* p = alloc<psyq::POLY_G4>(w);
  psyq::setlen(p, 8);
  set_rgb(&p->r0, colours[0]);
  set_rgb(&p->r1, colours[1]);
  set_rgb(&p->r2, colours[2]);
  set_rgb(&p->r3, colours[3]);
  p->code = 0x3A;
  p->x0 = xs[0], p->y0 = ys[0];
  p->x1 = xs[1], p->y1 = ys[1];
  p->x2 = xs[2], p->y2 = ys[2];
  p->x3 = xs[3], p->y3 = ys[3];
  add_prim(w, p);
  return p;
}

u8* config() { return title_globals().config; }

const s32* keyboard_rows() { return reinterpret_cast<const s32*>(title_data(0x8004E458)); }
const char* keyboard_chars(int set) {
  return reinterpret_cast<const char*>(title_data(set == 0 ? 0x8004E460 : 0x8004E58C));
}
const s32* keyboard_column_map() { return reinterpret_cast<const s32*>(title_data(0x8004EAF0)); }

// The draw method of the windows that list a heading and six lines of text.
void draw_text_lines(Window* w, int lines, int heading_x, int body_x, int first_id) {
  window_text_begin(w);
  int y = 0;
  for (int i = 0; i < lines; ++i) {
    text_draw_id(w, i == 0 ? heading_x : body_x, y, w->selection * 7 + i + first_id, static_cast<u32>(i == 0) << 2);
    y += 0x0E;
  }
  window_text_end(w);
}

}  // namespace

Window::Window(int prims) {
  ctx = new RenderContext;
  render_ctx_create_arena(ctx, static_cast<size_t>(prims) * 0x28);
  render_ctx_create_ot(ctx, 1);
  title_globals().config = static_cast<u8*>(handoff_get(title_globals().handoff, 2));
}

Window::~Window() {
  screen_destroy_arena(ctx->arena);
  screen_destroy_ot(ctx->ot);
  delete ctx;
}

void Window::draw() { window_draw_frame(this); }

void Window::set_rect(int x_, int y_, int w_, int h_) {
  x = static_cast<s16>(x_);
  y = static_cast<s16>(y_);
  w = static_cast<s16>(w_);
  h = static_cast<s16>(h_);
}

void window_draw_frame(Window* w) {
  const u8* corner_u = title_data(0x8004D570);
  const u8* corner_v = title_data(0x8004D574);
  constexpr u16 kClut = 0x7F81;
  auto* corners = alloc<psyq::SPRT_8>(w, 4);
  for (int i = 0; i < 4; ++i) {
    psyq::SPRT_8* p = &corners[i];
    p->code = 0x75;
    p->x0 = static_cast<s16>((i & 1) == 0 ? w->x : w->x - 8 + w->w);
    p->y0 = static_cast<s16>(i < 2 ? w->y : w->y - 8 + w->h);
    p->u0 = corner_u[i];
    p->v0 = corner_v[i];
    p->clut = kClut;
    psyq::setlen(p, 3);
    add_prim(w, p);
  }
  // Top and bottom edges in 32-texel pieces, then the remainder.
  const int across = w->w - 0x10;
  for (int i = 0; i < w->w - 0x2F; i += 0x20) {
    for (int side = 0; side < 2; ++side) {
      queue_sprite(w, w->x + i + 8, side == 0 ? w->y : w->y - 8 + w->h, 8, side == 0 ? 0x30 : 0x58, kClut, 0x20, 8);
    }
  }
  if (across & 0x1F) {
    const int rest = across % 0x20;
    for (int side = 0; side < 2; ++side) {
      queue_sprite(w, (w->x + w->w) - (rest + 8), side == 0 ? w->y : w->y - 8 + w->h, 8, side == 0 ? 0x30 : 0x58, kClut,
                   rest, 8);
    }
  }
  // Left and right edges.
  const int down = w->h - 0x10;
  for (int i = 0; i < w->h - 0x2F; i += 0x20) {
    for (int side = 0; side < 2; ++side) {
      queue_sprite(w, side == 0 ? w->x : w->x - 8 + w->w, w->y + i + 8, side == 0 ? 0 : 0x28, 0x38, kClut, 8, 0x20);
    }
  }
  if (down & 0x1F) {
    const int rest = down % 0x20;
    for (int side = 0; side < 2; ++side) {
      queue_sprite(w, side == 0 ? w->x : w->x - 8 + w->w, (w->y + w->h) - (rest + 8), side == 0 ? 0 : 0x28, 0x38, kClut,
                   8, rest);
    }
  }
  queue_draw_mode(w, 0, 0, 0x0F);
  w->draw_contents();
  // The translucent background in the configured window colours.
  const u8* cfg = config();
  u32 colours[4];
  std::memcpy(colours, cfg + 0x30, sizeof colours);
  const s16 left = static_cast<s16>(w->x + 3), right = static_cast<s16>(w->x + w->w - 2);
  const s16 top = static_cast<s16>(w->y + 2), bottom = static_cast<s16>(w->y + w->h - 2);
  const s16 xs[4] = {left, right, left, right};
  const s16 ys[4] = {top, top, bottom, bottom};
  queue_g4(w, colours, xs, ys);
  queue_draw_mode(w, 0, 1, 0);
}

void window_draw_shade_frame(Window* w) {
  const s16 x = w->x, y = w->y, r = static_cast<s16>(w->x + w->w), b = static_cast<s16>(w->y + w->h);
  {
    const u32 grey[4] = {0x808080, 0x808080, 0x808080, 0x808080};
    const s16 xs[4] = {static_cast<s16>(x + 8), static_cast<s16>(r - 0x10), static_cast<s16>(x + 8),
                       static_cast<s16>(r - 0x10)};
    const s16 ys[4] = {static_cast<s16>(y + 8), static_cast<s16>(y + 8), static_cast<s16>(b - 0x10),
                       static_cast<s16>(b - 0x10)};
    queue_g4(w, grey, xs, ys);
  }
  // Edges: outer vertices black, inner vertices grey.
  const u32 fade[4] = {0, 0, 0x808080, 0x808080};
  const s16 x8 = static_cast<s16>(x + 8), y8 = static_cast<s16>(y + 8);
  const s16 r16 = static_cast<s16>(r - 0x10), b16 = static_cast<s16>(b - 0x10);
  const s16 edge_x[4][4] = {{x, x, x8, x8}, {x, r, x8, r16}, {r, r, r16, r16}, {x, r, x8, r16}};
  const s16 edge_y[4][4] = {{y, b, y8, b16}, {y, y, y8, y8}, {y, b, y8, b16}, {b, b, b16, b16}};
  for (int i = 0; i < 4; ++i) queue_g4(w, fade, edge_x[i], edge_y[i]);
  queue_draw_mode(w, 0, 1, 0x40);
}

void MemcardNoticeWindow::draw() {
  window_text_begin(this);
  text_draw_id(this, 4, 0, (static_cast<u32>(selection) & 3) | 200, 4);
  const int body = selection > 3 ? 0xCE : 0xCC;
  text_draw_id(this, 4, 0x0E, body, 0);
  text_draw_id(this, 4, 0x1C, body | 1, 0);
  window_text_end(this);
  window_draw_frame(this);
}

SoundWindow::SoundWindow() : Window(0x80) { runtime::Runtime::get().trace("0x8004711c"); selection = sound_output_mode(title_globals().sound); }

void SoundWindow::draw() {
  draw_text_lines(this, selection == 2 ? text::layout(text::lang()).sound_lines_stereo : 7, 0x50, 8, 0);  // NA 5
  window_draw_frame(this);
}

VibrationWindow::VibrationWindow() : Window(0x80) { runtime::Runtime::get().trace("0x80047788"); selection = pad_is_dualshock(title_globals().pad, 0) ? 0 : 1; }

void VibrationWindow::draw() {
  draw_text_lines(this, selection == 1 ? 5 : 7, text::layout(text::lang()).vibration_heading_x, 8, 0x2E);  // NA x 0x54
  window_draw_frame(this);
}

CharacterWindow::CharacterWindow() : Window(0x80) { runtime::Runtime::get().trace("0x80047690"); }

void CharacterWindow::draw() {
  window_text_begin(this);
  x = static_cast<s16>(selection == 0 ? 8 : 0x138 - w);
  int y_ = 0;
  for (int i = 0; i < 7; ++i) {
    text_draw_id(this, 4, y_, selection * 7 + i + 0x20, static_cast<u32>(i == 0) << 2);
    y_ += 0x0E;
  }
  window_text_end(this);
  window_draw_shade_frame(this);
}

NameKeyboardWindow::NameKeyboardWindow() : Window(0x200) { runtime::Runtime::get().trace("0x800478e8");
  column = text::name_keyboard(text::lang()).first_column;  // NA 2 (OK), FR / DE 1 (Delete)
  row = 0;
  selection = 0;
  moving = 0;
  next_column = column;
  next_row = row;
}

namespace {

int keyboard_x(int column, int row) {
  if (row < 1) return column * text::name_keyboard(text::lang()).button_step + 0x0C;  // NA 0x2a
  return column * 0x13 + (column > 4 ? 0x14 : 0x0C);
}

}  // namespace

void NameKeyboardWindow::draw() {
  int cx = keyboard_x(column, row);
  int cy = row * 0x0E;
  if (moving != 0) {
    const int t = 4 - moving;
    cx += ((keyboard_x(next_column, next_row) - cx) * t) / 4;
    cy += ((next_row * 0x0E + row * -0x0E) * t) / 4;
    if (--moving == 0) {
      column = next_column;
      row = next_row;
    }
  }
  queue_sprite(this, x + cx - 8, y + cy + 0x0E, 0, 0xE0, 0x7807, 0x10, 0x18);
  queue_draw_mode(this, 0, 0, 5);
  window_text_begin(this);
  const text::NameKeyboard& keys = text::name_keyboard(text::lang());
  const int rows = keyboard_rows()[selection];
  const char* chars = keyboard_chars(keys.charset_switch ? selection : 0);  // FR / DE: set 0 only
  int line_y = 0x0E;
  int cell_x = 0x0C;
  for (int r = 0; cell_x = 0x0C, r < rows; ++r) {
    for (int c = 0; c < 10; ++c) {
      u8 glyphs[8];
      text_from_ascii(this, chars, glyphs);
      text_draw(this, cell_x, line_y, glyphs, row == r + 1 && column == c ? 0x8000 : 0);
      cell_x += c == 4 ? 0x1B : 0x13;
      chars += 6;
    }
    line_y += 0x0E;
  }
  for (int i = 0; i < keys.buttons; ++i) {  // NA 5 buttons, step 0x2a
    text_draw_id(this, cell_x, 0, i + 0x6E, row == 0 && column == i ? 0x8004 : 4);
    cell_x += keys.button_step;
  }
  window_text_end(this);
  window_draw_shade_frame(this);
}

void keyboard_move(NameKeyboardWindow* kb, NameBoxWindow* box, int dx, int dy) {
  if (kb->moving != 0) return;
  const s32 rows = keyboard_rows()[kb->selection];
  const s32* column_map = keyboard_column_map();
  const int last_button = text::name_keyboard(text::lang()).buttons - 1;  // NA 4
  int row = kb->row;
  int column = kb->column + dx;
  if (column < 0) {
    column = 9;
    if (row < 1) {
      column = last_button;
    } else {
      dy = -1;
    }
  }
  if ((column > 9 || (column > last_button && row == 0))) {
    column = 0;
    if (row < rows) dy = 1;
  }
  int next = column;
  if (dy < 1) {
    if (dy < 0) {
      const bool from_first = row == 1;
      --row;
      if (from_first) {
        column = column_map[column];
        row = 0;
      }
      next = column;
      if (row < 0) row = 0;
    }
  } else if (row == 0) {
    if (box->length < 7) {
      row = 1;
      for (int i = 0; i < 10; ++i) {
        if (column_map[i] == column) {
          next = i;
          break;
        }
      }
    }
  } else {
    ++row;
    if (box->length == 7) {
      if (row > 0) row = 0;
    } else if (rows < row) {
      row = rows;
    }
  }
  if (kb->column != next || kb->row != row) sound_play_sfx(title_globals().sound, 1, 0, 0x7F40);
  kb->next_column = next;
  kb->next_row = row;
  kb->moving = 3;
}

int keyboard_confirm(NameKeyboardWindow* kb, NameBoxWindow* box) {
  Sound* sound = title_globals().sound;
  const text::NameKeyboard& keys = text::name_keyboard(text::lang());
  if (kb->row < 1) {
    // NA: Capital, Lower, OK, Delete, Return; FR / DE: OK, Delete, Return.
    switch (text::keyboard_button(text::lang(), kb->column)) {
      case text::KeyAction::kCapital:
      case text::KeyAction::kLower:
        kb->selection = kb->column;
        sound_play_sfx(sound, 3, 0, 0x7F40);
        return 0;
      case text::KeyAction::kOk:
        return 1;
      case text::KeyAction::kDelete:
        name_box_backspace(box);
        return 0;
      case text::KeyAction::kReturn:
        return 2;
      default:
        return 0;
    }
  }
  if (box->length >= 7) return 0;
  const char* chars = keyboard_chars(keys.charset_switch ? kb->selection : 0);
  if (name_box_append(box, chars + (kb->column + (kb->row - 1) * 10) * 6)) {
    kb->next_column = keys.ok_column;  // full: jump to OK (NA 2, FR / DE 0)
    kb->next_row = 0;
    kb->moving = 4;
  }
  sound_play_sfx(sound, 3, 0, 0x7F40);
  return 0;
}

NameBoxWindow::NameBoxWindow(int character) : Window(0x80) {
  selection = character;
  name_box_reset(this);
}

void name_box_reset(NameBoxWindow* box) {
  // Two-byte strings in the program (0x8004d35c..0x8004d378): "C" "l" "a" "u" "d" "e" "R" "n".
  auto set = [box](int slot, u32 address) { std::memcpy(box->chars[slot], title_data(address), 2); };
  if (box->selection == 0) {
    set(0, 0x8004D35C);
    set(1, 0x8004D360);
    set(2, 0x8004D364);
    set(3, 0x8004D368);
    set(4, 0x8004D36C);
    box->length = 6;
    box->chars[6][0] = 0;
    box->chars[7][0] = 0;
    set(5, 0x8004D370);
  } else {
    set(0, 0x8004D374);
    set(1, 0x8004D370);
    set(2, 0x8004D378);
    box->length = 4;
    box->chars[4][0] = 0;
    box->chars[5][0] = 0;
    box->chars[6][0] = 0;
    box->chars[7][0] = 0;
    set(3, 0x8004D364);
  }
  box->blink = static_cast<s32>(0xFFFFFF01u);
  box->unknown2c = 0;
}

bool name_box_append(NameBoxWindow* box, const char* text) {
  std::strcpy(box->chars[box->length], text);
  return ++box->length == 7;
}

void name_box_backspace(NameBoxWindow* box) {
  if (box->length != 0) {
    --box->length;
    box->chars[box->length][0] = 0;
    sound_play_sfx(title_globals().sound, 6, 0, 0x7F40);
  }
}

namespace {

// The slots joined into one string (each slot's text up to its terminator).
void name_box_join(const NameBoxWindow* box, char* out) {
  for (const auto& slot : box->chars) {
    for (const char* p = slot; *p; ++p) *out++ = *p;
  }
  *out = 0;
}

}  // namespace

bool name_box_commit(NameBoxWindow* box, char* out) {
  char joined[56];
  name_box_join(box, joined);
  char* o = out;
  for (const char* p = joined; *p;) {
    if (p[0] == '@' && p[1] == '@') {
      p += 2;
    } else {
      *o++ = *p++;
    }
  }
  *o = 0;
  if (*out == 0) return false;
  if (box->selection == 0 && std::strcmp(out, "Claude") != 0) config()[0x42] = 1;
  if (box->selection == 1 && std::strcmp(out, "Rena") != 0) config()[0x43] = 1;
  return true;
}

void NameBoxWindow::draw() {
  char joined[24];
  name_box_join(this, joined);
  u8 glyphs[24];
  text_from_ascii(this, joined, glyphs);
  int xs[24];
  const int count = text_glyph_offsets(this, glyphs, xs);
  for (int i = count, pos = xs[count]; i < 8; ++i, pos += 10) xs[i] = pos;
  if (length < 7) {
    queue_sprite(this, x + (xs[length] + xs[length + 1]) / 2 - 4, y + 0x13, 0x20, 0xE0, 0x7808, 0x18, 0x0D);
    // The glowing box behind the next character.
    u32 colours[4];
    for (int k = 0; k < 4; ++k) {
      int phase = blink + k * 0x80;
      if (phase > 0xFF) phase -= 0x200;
      if (phase < 0) phase = -phase;
      colours[k] = static_cast<u32>((phase << 6) >> 8 | (phase * 0x90 >> 8) << 8 | (phase * 0xA0 >> 8) << 16);
    }
    const s16 left = static_cast<s16>(x + xs[length] + 8), right = static_cast<s16>(x + xs[length + 1] + 8);
    const s16 top = static_cast<s16>(y + 8), bottom = static_cast<s16>(y + 0x14);
    const s16 bx[4] = {left, right, left, right};
    const s16 by[4] = {top, top, bottom, bottom};
    const int old = blink;
    blink = old + 2;
    if (blink > 0xFF) blink = old - 0x1FE;
    queue_g4(this, colours, bx, by);
    queue_draw_mode(this, 0, 1, 0x25);
  }
  window_text_begin(this);
  int pos = xs[count];
  for (int i = count; i < 7; ++i) {
    queue_sprite(this, x + pos + 8, y + 0x11, 0x10, 0xE0, 0x7802, 10, 3);  // blanks still to fill
    pos += 10;
  }
  queue_draw_mode(this, 0, 0, 5);
  text_draw(this, 0, 0, glyphs, 0);
  text_draw_id(this, pos, 0, selection + 100, 0);  // the family name after the blanks
  window_text_end(this);
}

CombatModeWindow::CombatModeWindow() : Window(0x80) { runtime::Runtime::get().trace("0x800489dc"); }

void CombatModeWindow::draw() {
  draw_text_lines(this, 7, 0x48, 8, 0x3A);
  // The pad-button icon inside the description (a 14x11 sprite at a per-mode spot).
  const s32* icon_x = reinterpret_cast<const s32*>(title_data(0x8004D37C));
  const s32* icon_y = reinterpret_cast<const s32*>(title_data(0x8004D388));
  queue_sprite(this, x + icon_x[selection] + 8, y + icon_y[selection] + 8, 0, 0xA0, 0x7809, 0x0E, 0x0B);
  queue_draw_mode(this, 0, 0, 7);
  window_draw_frame(this);
}

void BattleRankWindow::draw() {
  draw_text_lines(this, 7, 0x50, 4, 0x96);
  window_draw_frame(this);
}

}  // namespace ss::game
