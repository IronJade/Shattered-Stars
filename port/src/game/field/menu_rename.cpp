#include "game/field/menu_rename.h"

#include <cstring>
#include <span>
#include <vector>

#include "disc/slz.h"
#include "game/boot/pad.h"
#include "game/boot/sound.h"
#include "game/common/menu_core.h"
#include "game/common/menu_scene.h"
#include "game/common/text_rules.h"
#include "game/common/menu_widget.h"
#include "game/common/party.h"
#include "game/common/program_image.h"
#include "game/common/screen.h"
#include "game/common/texture.h"
#include "game/field/field.h"
#include "runtime/libgpu.h"
#include "runtime/runtime.h"

namespace ss::game::field {
namespace {

constexpr int kCode = 0xBCE;

const u8* ovl(u32 address) { return menu::page_table(kCode, address); }
s32 ovl_s32(u32 address) {
  const u8* p = ovl(address);
  if (!p) return 0;
  s32 v;
  std::memcpy(&v, p, 4);
  return v;
}

s32 rd32(const u8* p) {
  s32 v;
  std::memcpy(&v, p, 4);
  return v;
}
u16 rd16(const u8* p) { return read_u16(p); }

Sound* sound() { return globals().sound; }
u16 button(int n) { return read_u16(globals().config + n * 2); }

// ---- Text (0xbce 0x80081408-0x8008221c; the title's 0x80049f40-0x8004ad4c) ---------------
// The overlay's own copy of the title's renderer. Differences: the line buffer is 216
// rows at VRAM (0x140, 0x28) (texture page 5, v = line + 0x28), the text CLUTs are row
// 0x1e1 (palette + 0x7840), and the text clip area leaves texture page 5 selected.
struct Text {
  s16 x = 0;                // 0x80082c58: next free texel column on the current line
  s16 line = 0;             // 0x80082c5a: first texel row of the current line
  std::vector<u8> strings;  // 0x80082c5c: pack block 0, {u16 id, u16 offset} pairs ending in 0xffff
  std::vector<u8> font;     // 0x80082c60: pack block 1, s32 count, s32 height, widths, u16 rows
  std::vector<u8> pixels;   // 0x80082c64: 0x6c00 bytes (216 rows of 128)
};
Text g_text;

// The line buffer: 0x6c00 bytes on the console. The last line can start at row 0xd8
// and its twelve rows run past the buffer there; the port keeps 0x100 rows so they
// land in memory it owns (the upload still stops at row 0xd8).
constexpr size_t kTextPixels = 0x8000;

class Window;
void window_text_begin(Window* w);
void window_text_end(Window* w);

// ---- Windows (base vtable 0x80082494: ctor 0x80080b04, dtor 0x80080b90) ------------------
class Window {
 public:
  explicit Window(int prims) {
    ctx = new RenderContext;
    // The console's arena is prims * 0x28 bytes; host primitives are larger.
    render_ctx_create_arena(ctx, static_cast<size_t>(prims) * 0x50);
    render_ctx_create_ot(ctx, 1);
    // (Also reloads the config pointer from handoff slot 2, which globals().config is.)
  }
  virtual ~Window() {
    screen_destroy_arena(ctx->arena);
    screen_destroy_ot(ctx->ot);
    delete ctx;
  }
  virtual void draw() = 0;                  // [1] (the base's 0x80080c44, the skinned frame, is unused)
  void set_rect(int x_, int y_, int w_, int h_) {   // [2] 0x80080c08
    x = static_cast<s16>(x_);
    y = static_cast<s16>(y_);
    w = static_cast<s16>(w_);
    h = static_cast<s16>(h_);
  }

  s16 x = 0, y = 0, w = 0, h = 0;  // +0x08
  u8 unknown18 = 0x0C;              // +0x18
  RenderContext* ctx = nullptr;     // +0x1c
  s32 selection = 0;                // +0x20
};

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

void queue_g4(Window* w, int code, const u32 colours[4], const s16 xs[4], const s16 ys[4]) {
  auto* p = alloc<psyq::POLY_G4>(w);
  psyq::setlen(p, 8);
  set_rgb(&p->r0, colours[0]);
  set_rgb(&p->r1, colours[1]);
  set_rgb(&p->r2, colours[2]);
  set_rgb(&p->r3, colours[3]);
  p->code = static_cast<u8>(code);
  p->x0 = xs[0], p->y0 = ys[0];
  p->x1 = xs[1], p->y1 = ys[1];
  p->x2 = xs[2], p->y2 = ys[2];
  p->x3 = xs[3], p->y3 = ys[3];
  add_prim(w, p);
}

// Reads one glyph code and advances; codes are stored plus one.
u32 next_code(const u8*& s) {
  u32 c = *s++;
  if (c > 0x7F) c = (c & 0x7F) | static_cast<u32>(*s++) << 7;
  return c;
}

// 0x800821dc: empty the line buffer (start of a frame).
void text_frame_begin() {
  if (g_text.x != 0 || g_text.line != 0) {
    g_text.x = 0;
    g_text.line = 0;
  }
}

// 0x8008221c: upload the used rows to VRAM (0x140, 0x28).
void text_frame_upload() {
  if (g_text.x != 0 || g_text.line != 0) {
    psyq::RECT r{0x140, 0x28, 0x40, static_cast<s16>(g_text.line + 0x0C)};
    if (r.h > 0xD7) r.h = 0xD8;
    psyq::LoadImage(&r, g_text.pixels.data());
  }
}

// 0x80081408: ASCII (with '@' toggling the kana mode) to glyph codes (the title's
// 0x80049f40, with the overlay's copy of its table at 0x80082b78).
void text_from_ascii(const char* in, u8* out) {
  // From the table's start in the release's own layout, indexed by character (its head
  // overlaps the column map, which France and Germany changed; see title/text.cpp). The
  // overlay's image ends 0xf0 bytes in (bss after it: 0 here).
  const std::span<const u8> ascii = program_run(kCode, menu::kPageCodeBase, 0x80082B78);
  const auto table = [&ascii](u32 c) { return c < ascii.size() ? ascii[c] : u8{0}; };
  const auto* s = reinterpret_cast<const u8*>(in);
  int kana = 0;
  for (;;) {
    u32 c = *s;
    const u8* next = s + 1;
    if (c == 0) {
      *out = 0;
      return;
    }
    if (c == '@') {
      kana = 1 - kana;
      s = next;
      continue;
    }
    u32 glyph;
    if (kana == 0 || c - 0xA6 > 0x37) {
      glyph = c - 0x61 <= 0x19 ? c - 0x20 : table(c);
    } else {
      glyph = table(c) + 0x38u;
    }
    // Voiced (0xde) and semi-voiced (0xdf) marks fold into the kana before them.
    const bool row_ka_ta = c - 0xB6 < 5 || c - 0xBB < 5 || c - 0xC0 < 5;
    if (*next == 0xDE) {
      if (kana == 0) {
        if (c == 0xB3) {
          glyph = 0xB3;
        } else if (row_ka_ta) {
          glyph = c - 2;
        } else if (c - 0xCA < 5) {
          glyph = c - 7;
        }
      } else if (row_ka_ta) {
        glyph = c + 0x17;
      } else if (c - 0xCA < 5) {
        glyph = c + 0x12;
      }
      next = s + 2;
    } else if (*next == 0xDF) {
      glyph = kana == 0 ? c - 2 : c + 0x17;
      next = s + 2;
    }
    const u32 code = (glyph + 1) & 0xFF;
    if (code < 0x80) {
      *out++ = static_cast<u8>(code);
    } else {
      *out++ = static_cast<u8>(code | 0x80);
      *out++ = static_cast<u8>(code >> 7);
    }
    s = next;
  }
}

// 0x800816fc: a string by id, or null.
const u8* text_find(int id) {
  const u8* table = g_text.strings.data();
  for (const u8* p = table;; p += 4) {
    const u16 key = rd16(p);
    if (key == 0xFFFF) return nullptr;
    if (key == static_cast<u32>(id)) return table + rd16(p + 2);
  }
}

// 0x80081784: width in pixels.
int text_width(const u8* s) {
  const u8* widths = g_text.font.data() + 8;
  int width = 0;
  for (u32 c; (c = next_code(s)) != 0;) width += widths[(c - 1) & 0xFFFF];
  return width;
}

// 0x80081820: the x of each glyph and the end; returns the glyph count.
int text_glyph_offsets(const u8* s, int* xs) {
  const u8* widths = g_text.font.data() + 8;
  int x = 0, count = 0;
  for (u32 c; (c = next_code(s)) != 0;) {
    *xs++ = x;
    ++count;
    x += widths[(c - 1) & 0xFFFF];
  }
  *xs = x;
  return count;
}

// 0x80081578: the underline of a highlighted string (the colour, then black one pixel
// down and right).
void text_underline(Window* w, int x, int y, const u8* s, u32 colour) {
  const int lx = x + 8 + w->x;
  const int ly = y + 0x14 + w->y;
  const int width = text_width(s);
  for (int k = 0; k < 2; ++k) {
    auto* a = alloc<psyq::LINE_F2>(w);
    psyq::setlen(a, 3);
    set_rgb(&a->r0, k == 0 ? colour : 0);
    a->code = 0x40;
    a->x0 = static_cast<s16>(lx + k);
    a->y0 = static_cast<s16>(ly + k);
    a->x1 = static_cast<s16>(lx + width + k);
    a->y1 = static_cast<s16>(ly + k);
    add_prim(w, a);
  }
}

// Queues the sprite that shows texels [start, start + width) of the current line.
void queue_line_sprite(Window* w, int sx, int sy, int start, int height, u32 palette, int width) {
  auto* p = alloc<psyq::SPRT>(w);
  psyq::setlen(p, 4);
  p->code = 0x65;
  p->x0 = static_cast<s16>(sx);
  p->y0 = static_cast<s16>(sy);
  p->u0 = static_cast<u8>(start);
  p->v0 = static_cast<u8>(g_text.line + 0x28);
  p->clut = static_cast<u16>(palette + 0x7840);
  p->w = static_cast<s16>(width);
  p->h = static_cast<s16>(height);
  add_prim(w, p);
}

// Zeroes the byte at column x / 2 on the twelve rows of the current line.
void clear_column(int x) {
  for (int row = 11; row >= 0; --row) g_text.pixels[static_cast<size_t>(x / 2 + (g_text.line + row) * 0x80)] = 0;
}

// 0x80081b9c: draws a string at window-relative (x, y) with palette 'palette' (CLUT row
// 0x1e1); bit 15 also underlines it (colours 0x800824c4). Returns (y << 16 | x) of the end.
u32 text_draw(Window* w, int x, int y, const u8* s, u32 palette) {
  const u8* font = g_text.font.data();
  const int glyph_count = rd32(font), height = rd32(font + 4);
  if (g_text.line >= 0xD9) return 0;
  if (palette > 0x7FFF) {
    palette &= 0x7FFF;
    text_underline(w, x, y, s, static_cast<u32>(ovl_s32(0x800824C4 + palette * 4)));
  }
  // Four glyph bits to four 4-bit texels of colour 1, and the same one texel right as
  // colour 2 (the shadow) (scratchpad 0x1f800100 / 0x1f800180).
  u16 glyph_bits[16], shadow_bits[16];
  for (u32 i = 0; i < 16; ++i) {
    const u32 v = (i & 1) << 12 | (i & 2) << 7 | (i & 4) << 2 | (i & 8) >> 3;
    glyph_bits[i] = static_cast<u16>(v);
    shadow_bits[i] = static_cast<u16>(static_cast<u16>(v << 5) | (i & 1) << 1);
  }
  int sx = x + 8 + w->x;
  const int sy = y + 8 + w->y;
  if (g_text.x & 1) ++g_text.x;
  int start = g_text.x;
  clear_column(start);
  for (u32 c; (c = next_code(s)) != 0;) {
    c = (c - 1) & 0xFFFF;
    const u8* rows = font + height * c * 2 + ((glyph_count + 3) & ~3) + 8;
    const u8 advance = font[8 + c];
    if (static_cast<u32>(g_text.x + 0x10) > 0xFD) {
      // The line is full: show what this string put on it and continue on the next.
      if (g_text.x != start) {
        const int drawn = g_text.x - start;
        const int width = (drawn + 1) & 1 ? drawn + 2 : drawn + 1;
        queue_line_sprite(w, sx, sy, start, height, palette, width);
        sx += drawn;
      }
      g_text.x = 0;
      g_text.line = static_cast<s16>(g_text.line + height);
      start = 0;
      if (g_text.line > 0xD7) break;
      clear_column(0);
    }
    u16 scratch[13][6] = {};  // twelve rows of 24 texels (+ the shadow's row below)
    u8* dst = g_text.pixels.data() + g_text.x / 2 + g_text.line * 0x80;
    for (int r = 0; r < 12; ++r) reinterpret_cast<u8*>(scratch[r])[0] = dst[r * 0x80];
    const int odd = g_text.x & 1;
    const int groups = (advance + odd + 3) >> 2;
    for (int r = 0; r < height; ++r) {
      u32 bits = rd16(rows + r * 2);
      if (odd) bits >>= 1;
      for (int g = 0; g < groups; ++g) {
        const u32 index = (bits >> 12) & 0xF;
        bits <<= 4;
        scratch[r][g] |= glyph_bits[index];
        const u16 shadow = shadow_bits[index];
        scratch[r + 1][g] |= shadow & 0xFFF0;
        reinterpret_cast<u8*>(&scratch[r + 1][g + 1])[0] = static_cast<u8>(shadow & 0xF);
      }
    }
    for (int r = 0; r < 12; ++r) std::memcpy(dst + r * 0x80, scratch[r], 8);
    g_text.x = static_cast<s16>(g_text.x + advance);
  }
  ++g_text.x;
  if (start != g_text.x) {
    const int drawn = g_text.x - start;
    queue_line_sprite(w, sx, sy, start, height, palette, drawn & 1 ? drawn + 1 : drawn);
    sx += drawn;
  }
  return static_cast<u32>((sy - w->y - 8) << 16) | static_cast<u32>((sx - w->x) - 8);
}

// 0x80081898
void text_draw_id(Window* w, int x, int y, int id, u32 palette) { text_draw(w, x, y, text_find(id), palette); }

// 0x80081900: the full-screen draw area that runs after the window's text.
void window_text_begin(Window* w) {
  auto* p = alloc<psyq::DR_AREA>(w);
  const psyq::RECT area{0, static_cast<s16>(screen_draw_y()), 0x140, 0xF0};
  psyq::SetDrawArea(p, &area);
  add_prim(w, p);
}

// 0x800819c8: the window's clip area and the text's texture page, which run before it.
void window_text_end(Window* w) {
  int left = w->x + 8;
  if (left < 0) left = 0;
  int top = w->y + 4;
  if (top < 0) top = 0;
  int width = w->w - 0x10;
  if (width + w->x + 8 > 0x140) width = 0x138 - w->x;
  if (width < 0) width = 0;
  int height = w->h - 8;
  if (height + w->y + 4 > 0xF0) height = 0xEC - w->y;
  if (height < 0) height = 0;
  const psyq::RECT area{static_cast<s16>(left), static_cast<s16>(screen_draw_y() + top), static_cast<s16>(width),
                        static_cast<s16>(height)};
  auto* p = alloc<psyq::DR_AREA>(w);
  psyq::SetDrawArea(p, &area);
  add_prim(w, p);
  queue_draw_mode(w, 0, 0, 5);
}

// 0x8007f0cc: the keyboard's frame, a grey centre and four edges fading out, subtracted
// from the background (the title's 0x80047228).
void draw_shade_frame(Window* w) {
  const s16 x = w->x, y = w->y, r = static_cast<s16>(w->x + w->w), b = static_cast<s16>(w->y + w->h);
  {
    const u32 grey[4] = {0x808080, 0x808080, 0x808080, 0x808080};
    const s16 xs[4] = {static_cast<s16>(x + 8), static_cast<s16>(r - 0x10), static_cast<s16>(x + 8),
                       static_cast<s16>(r - 0x10)};
    const s16 ys[4] = {static_cast<s16>(y + 8), static_cast<s16>(y + 8), static_cast<s16>(b - 0x10),
                       static_cast<s16>(b - 0x10)};
    queue_g4(w, 0x3A, grey, xs, ys);
  }
  const u32 fade[4] = {0, 0, 0x808080, 0x808080};
  const s16 x8 = static_cast<s16>(x + 8), y8 = static_cast<s16>(y + 8);
  const s16 r16 = static_cast<s16>(r - 0x10), b16 = static_cast<s16>(b - 0x10);
  const s16 edge_x[4][4] = {{x, x, x8, x8}, {x, r, x8, r16}, {r, r, r16, r16}, {x, r, x8, r16}};
  const s16 edge_y[4][4] = {{y, b, y8, b16}, {y, y, y8, y8}, {y, b, y8, b16}, {b, b, b16, b16}};
  for (int i = 0; i < 4; ++i) queue_g4(w, 0x3A, fade, edge_x[i], edge_y[i]);
  queue_draw_mode(w, 0, 1, 0x40);
}

// ---- Keyboard (vtable 0x80082324; ctor 0x8007f534) -----------------------------------------
s32 keyboard_rows(int set) { return ovl_s32(0x800824D8 + static_cast<u32>(set) * 4); }
const char* keyboard_chars(int set) {
  return reinterpret_cast<const char*>(ovl(set == 0 ? 0x800824E0 : 0x8008260C));
}
// Read from the table's start: France and Germany changed its values (three row-0 buttons),
// and the region map may align the changed words apart from it (port/tools/region_map.py).
s32 keyboard_column_map(int i) {
  const u8* table = ovl(0x80082B70);
  if (!table) return 0;
  s32 v;
  std::memcpy(&v, table + static_cast<size_t>(i) * 4, 4);
  return v;
}

class NameBox;

class Keyboard final : public Window {
 public:
  Keyboard() : Window(0x200) {
    column = text::name_keyboard(text::lang()).first_column;  // NA 2 (OK), FR / DE 1 (Delete)
    row = 0;
    selection = 0;   // the character set: 0 capitals, 1 lower case
    moving = 0;
    next_column = column;
    next_row = row;
  }
  ~Keyboard() override = default;           // 0x80080a2c
  void draw() override;                     // [1] 0x8007f774

  s32 column = 2;       // +0x24
  s32 row = 0;          // +0x28
  s32 next_column = 2;  // +0x2c
  s32 next_row = 0;     // +0x30
  s32 moving = 0;       // +0x34: frames left in the cursor's slide
};

int keyboard_x(int column, int row) {
  if (row < 1) return column * text::name_keyboard(text::lang()).button_step + 0x0C;  // NA 0x2a
  return column * 0x13 + (column > 4 ? 0x14 : 0x0C);
}

void Keyboard::draw() {
  const text::NameKeyboard& keys = text::name_keyboard(text::lang());
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
  queue_sprite(this, x + cx - 8, y + cy + 0x0E, text::layout(text::lang()).rename_hand_u, 0x10, 0x7807, 0x10,
               0x18);  // the hand (NA u 0x80)
  queue_draw_mode(this, 0, 0, 5);
  window_text_begin(this);
  const int rows = keyboard_rows(selection);
  const char* chars = keyboard_chars(keys.charset_switch ? selection : 0);  // FR / DE: set 0 only
  int line_y = 0x0E;
  int cell_x = 0x0C;
  for (int r = 0; cell_x = 0x0C, r < rows; ++r) {
    for (int c = 0; c < 10; ++c) {
      u8 glyphs[8];
      text_from_ascii(chars, glyphs);
      text_draw(this, cell_x, line_y, glyphs, row == r + 1 && column == c ? 0x8000 : 0);
      cell_x += c == 4 ? 0x1B : 0x13;
      chars += 6;
    }
    line_y += 0x0E;
  }
  for (int i = 0; i < keys.buttons; ++i) {  // NA 5 buttons, step 0x2a
    text_draw_id(this, cell_x, 0, i + 0x78, row == 0 && column == i ? 0x8004 : 4);
    cell_x += keys.button_step;
  }
  window_text_end(this);
  draw_shade_frame(this);
}

// ---- Name box (vtable 0x80082354; ctor 0x8007fcfc) -----------------------------------------
class NameBox final : public Window {
 public:
  NameBox(int character, const char* name);   // 0x8007fcfc
  ~NameBox() override = default;              // 0x80080a54
  void draw() override;                       // [1] 0x8007ff8c

  // selection (+0x20) is the character - 1 (its family name is string 100 + selection).
  s32 length = 0;        // +0x24: characters entered
  s32 blink = 0;         // +0x28: cursor glow phase
  s32 unknown2c = 0;     // +0x2c
  char original[0x18]{};  // +0x30: the name on entry (Return puts it back)
  char chars[7][6]{};     // +0x48: one string per character slot
};

// 0x8007fd68: the slots from the name on entry. Unlike the title (which sets the default
// names), this parses the stored name: '@' toggles kana mode, a kana keeps its '@'s in
// its slot, and a voiced mark goes into the slot of the character before it.
void name_box_reset(NameBox* box) {
  bool kana = false;
  box->length = 0;
  std::memset(box->chars, 0, sizeof box->chars);
  int slot = 0;
  for (const char* p = box->original; *p != 0; ++p) {
    const u8 c = static_cast<u8>(*p);
    if (c == 0xDE || c == 0xDF) continue;
    if (c == '@') {
      kana = !kana;
      continue;
    }
    // (No bound on the slots: a stored name has at most seven characters.)
    char* out = slot < 7 ? box->chars[slot] : nullptr;
    if (out == nullptr) break;
    int n = 0;
    const bool wrap = kana && static_cast<u8>(c - 0xA6) < 0x38;
    if (wrap) out[n++] = '@';
    out[n++] = static_cast<char>(c);
    const u8 mark = static_cast<u8>(p[1]);
    if (static_cast<u8>(mark + 0x22) < 2) out[n++] = static_cast<char>(mark);
    if (wrap) out[n] = '@';
    ++box->length;
    ++slot;
  }
  box->blink = static_cast<s32>(0xFFFFFF01u);
  box->unknown2c = 0;
}

NameBox::NameBox(int character, const char* name) : Window(0x80) {
  selection = character;
  std::strncpy(original, name, sizeof original - 1);
  name_box_reset(this);
}

// 0x8007fe8c: true once full.
bool name_box_append(NameBox* box, const char* text) {
  std::strcpy(box->chars[box->length], text);
  return ++box->length == 7;
}

// 0x8007fee8
void name_box_backspace(NameBox* box) {
  if (box->length != 0) {
    --box->length;
    std::memset(box->chars[box->length], 0, 4);
    sound_play_sfx(sound(), 6, 0, 0x7F40);
  }
}

// The slots joined into one string.
void name_box_join(const NameBox* box, char* out) {
  for (const auto& slot : box->chars) {
    for (const char* p = slot; *p; ++p) *out++ = *p;
  }
  *out = 0;
}

// 0x800804f4: writes the name to 'out' (an '@@' pair dropped); false when it is empty.
// Sets or clears the config's renamed flags (0x42 the first character, 0x43 the second),
// which pick the battle voices that do not call them by name.
bool name_box_commit(NameBox* box, char* out) {
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
  u8* config = globals().config;
  if (box->selection == 0) {
    // ORIGINAL BUG (original_bugs.md M1): the overlay compares with "Crawd", the Japanese
    // release's name for him, so keeping or restoring "Claude" marks him as renamed and the
    // battle voices switch to the lines that avoid his name.
    // config[0x42] = std::strcmp(out, "Crawd") == 0 ? 0 : 1;
    config[0x42] = std::strcmp(out, "Claude") == 0 ? 0 : 1;
  }
  if (box->selection == 1) config[0x43] = std::strcmp(out, "Rena") == 0 ? 0 : 1;
  return true;
}

void NameBox::draw() {
  char joined[24];
  name_box_join(this, joined);
  u8 glyphs[24];
  text_from_ascii(joined, glyphs);
  int xs[8];
  const int count = text_glyph_offsets(glyphs, xs);
  for (int i = count, pos = xs[count]; i < 8; ++i, pos += 10) xs[i] = pos;
  if (length < 7) {
    queue_sprite(this, x + (xs[length] + xs[length + 1]) / 2 - 4, y + 0x13, text::layout(text::lang()).rename_cursor_u,
                 0x10, 0x7808, 0x18, 0x0D);  // NA u 0xa0
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
    queue_g4(this, 0x3A, colours, bx, by);
    queue_draw_mode(this, 0, 1, 0x25);
  }
  window_text_begin(this);
  int pos = xs[count];
  for (int i = count; i < 7; ++i) {
    queue_sprite(this, x + pos + 8, y + 0x11, text::layout(text::lang()).rename_blank_u, 0x10, 0x7802, 10,
                 3);  // blanks still to fill (NA u 0x90)
    pos += 10;
  }
  queue_draw_mode(this, 0, 0, 5);
  text_draw(this, 0, 0, glyphs, 0);
  text_draw_id(this, pos, 0, selection + 100, 0);  // the family name after the blanks
  window_text_end(this);
}

// 0x8007f594
void keyboard_move(Keyboard* kb, NameBox* box, int dx, int dy) {
  if (kb->moving != 0) return;
  const s32 rows = keyboard_rows(kb->selection);
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
        column = keyboard_column_map(column);
        row = 0;
      }
      next = column;
      if (row < 0) row = 0;
    }
  } else if (row == 0) {
    if (box->length < 7) {
      row = 1;
      for (int i = 0; i < 10; ++i) {
        if (keyboard_column_map(i) == column) {
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
  if (kb->column != next || kb->row != row) sound_play_sfx(sound(), 1, 0, 0x7F40);
  kb->next_column = next;
  kb->next_row = row;
  kb->moving = 3;
}

// The cursor's jump to row 0 once the name is full (after a character or a reset; both
// are inline in the original).
void keyboard_jump_full(Keyboard* kb) {
  // ORIGINAL BUG (original_bugs.md M2): the overlay sends the cursor to column 3, Delete
  // (the Japanese layout's OK), so the next Cross deletes the last character.
  // kb->next_column = 3;
  // OK, as the title's copy does (NA 2; France / Germany's overlay sends it to their OK, column 0).
  kb->next_column = text::name_keyboard(text::lang()).ok_column;
  kb->next_row = 0;
  kb->moving = 4;
}

// 0x8007fb98: Cross on the keyboard; returns 1 for OK, 2 for Return, else 0.
int keyboard_confirm(Keyboard* kb, NameBox* box) {
  if (kb->row < 1) {
    // NA: Capital, Lower, OK, Delete, Return; FR / DE: OK, Delete, Return.
    switch (text::keyboard_button(text::lang(), kb->column)) {
      case text::KeyAction::kCapital:
      case text::KeyAction::kLower:
        kb->selection = kb->column;
        sound_play_sfx(sound(), 3, 0, 0x7F40);
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
  const char* chars = keyboard_chars(text::name_keyboard(text::lang()).charset_switch ? kb->selection : 0);
  if (name_box_append(box, chars + (kb->column + (kb->row - 1) * 10) * 6)) keyboard_jump_full(kb);
  sound_play_sfx(sound(), 3, 0, 0x7F40);
  return 0;
}

// ---- The screen (0x8007e4dc) --------------------------------------------------------------
// The SLZ pack at 'p': a chain of blocks, each with its next block's offset at +0xc.
std::span<const u8> pack_span(const u8* p, const u8* end) {
  size_t size = 0, at = 0;
  for (;;) {
    if (p + at + 0x10 > end) return {p, static_cast<size_t>(end - p)};
    const u32 next = read_u32(p + at + 0xC);
    if (next == 0) {
      size = at + 0x10 + read_u32(p + at + 4);
      break;
    }
    at += next;
  }
  if (p + size > end) size = static_cast<size_t>(end - p);
  return {p, size};
}

std::vector<u8> unpack(std::span<const u8> pack, int block) {
  auto out = disc::slz::unpack_block(pack, 0, block, nullptr);
  return out ? std::move(*out) : std::vector<u8>{};
}

// 0x80080880: a sprite (colour 'grey', semi-transparent when 'semi') and its texture page.
void screen_sprite(RenderContext* ctx, int x, int y, int w, int h, int u, int v, int tpage, int clut, int grey,
                   bool semi) {
  auto* p = screen_alloc_prims<psyq::SPRT>(ctx->arena, 1);
  psyq::setlen(p, 4);
  p->x0 = static_cast<s16>(x);
  p->y0 = static_cast<s16>(y);
  p->w = static_cast<s16>(w);
  p->h = static_cast<s16>(h);
  p->u0 = static_cast<u8>(u);
  p->v0 = static_cast<u8>(v);
  p->r0 = p->g0 = p->b0 = static_cast<u8>(grey);
  p->code = semi ? 0x66 : 0x64;
  p->clut = static_cast<u16>(clut);
  psyq::addPrim(ctx->ot_head, p);
  auto* m = screen_alloc_prims<psyq::DR_MODE>(ctx->arena, 1);
  psyq::SetDrawMode(m, 0, 0, tpage, nullptr);
  psyq::addPrim(ctx->ot_head, m);
}

void add_draw_mode(RenderContext* ctx, int dfe, int dtd, int tpage) {
  auto* m = screen_alloc_prims<psyq::DR_MODE>(ctx->arena, 1);
  psyq::SetDrawMode(m, dfe, dtd, tpage, nullptr);
  psyq::addPrim(ctx->ot_head, m);
}

// 'name' is record +0x24; 'pack' the page pack (strings, font, the text CLUTs as a TIM,
// the keyboard art); 'picture' the character's picture entry.
void rename_screen(char* name, int character, std::span<const u8> pack, std::vector<u8>& picture) {
  FieldGlobals& g = globals();
  psx::Gpu& gpu = runtime::Runtime::get().gpu;
  pad_set_port_mask(g.pad, 1);
  pad_set_repeat_delay(g.pad, 0x0C);
  screen_setup_buffer0(0, 0, 0x140, 0xF0, 0, 0, 0, 0);
  screen_setup_buffer1(0, 0xF0, 0x140, 0xF0, 0, 0, 0, 0);
  for (int i = 0; i < 2; ++i) {
    psyq::DRAWENV* d = screen_drawenv(i);
    d->isbg = 1;
    screen_dispenv(i)->isinter = 0;
    d->r0 = d->g0 = d->b0 = 0;
  }
  auto* ctx = new RenderContext;
  render_ctx_create_arena(ctx, 0x1680 * 2);
  render_ctx_create_ot(ctx, 1);
  bool closing = false;
  int fade = 0xF0;
  int reveal = 2;
  int cycle = 0;
  g_text.strings = unpack(pack, 0);
  g_text.font = unpack(pack, 1);
  {
    const std::vector<u8> tim = unpack(pack, 2);
    if (tim.size() >= 0x14 + 0x200) {
      std::vector<u16> row(256);
      std::memcpy(row.data(), tim.data() + 0x14, 0x200);
      upload_clut256(gpu, row.data(), 0, 0x1E1);
    }
  }
  // 0x800806e8: block 3 (the art, 4-bit) to (0x140, 0) with its CLUT row at 0x1e0.
  vram_upload_packed_block(gpu, pack, 3, 0x140, 0, 0x1E0);
  psyq::DrawSync(0);
  if (!picture.empty()) vram_upload_packed_block(gpu, picture, 0, 0x140, 0x100, 0x1E1);   // 15-bit
  picture = {};
  g_text.pixels.assign(kTextPixels, 0);
  g_text.x = g_text.line = 0;
  auto* kb = new Keyboard;
  auto* box = new NameBox(character, name);
  kb->set_rect(0x50, 0x6E, 0xE8, 100);
  box->set_rect(0xA0, 0x30, 0xAC, 0x18);
  // The cursor's colours (CLUT 0x7808 entries 7-15 and 0x7809's first) cycle.
  const psyq::RECT cycle_rect{0x87, 0x1E0, 10, 1};
  u16 colours[10];
  psyq::StoreImage(&cycle_rect, colours);
  pad_clear(g.pad, 0);
  const u8* widths = ovl(0x800822FC);   // the pictures' widths, 12 bytes
  for (;;) {
    deferred_delete_update();
    screen_flip();
    ctx->ot_head = screen_clear_ot(ctx->ot);
    kb->ctx->ot_head = screen_clear_ot(kb->ctx->ot);
    box->ctx->ot_head = screen_clear_ot(box->ctx->ot);
    text_frame_begin();
    if (fade != 0) {
      // Fading in or out: the screen less (fade + 8) per channel, over everything.
      auto* t = screen_alloc_prims<psyq::TILE>(kb->ctx->arena, 1);
      psyq::setlen(t, 3);
      const u8 level = static_cast<u8>(fade + 8);
      t->r0 = t->g0 = t->b0 = level;
      t->code = 0x62;
      t->x0 = 0;
      t->y0 = 0;
      t->w = 0x140;
      t->h = 0xF0;
      psyq::addPrim(kb->ctx->ot_head, t);
      add_draw_mode(kb->ctx, 0, 1, 0x40);
    }
    if (closing) {
      fade += 0x20;
      if (fade > 0xFF) {
        psyq::VSync(2);
        delete kb;
        delete box;
        psyq::VSync(2);
        screen_destroy_arena(ctx->arena);
        screen_destroy_ot(ctx->ot);
        delete ctx;
        g_text.strings = {};
        g_text.font = {};
        g_text.pixels = {};
        deferred_delete_update();
        deferred_delete_update();
        psyq::ResetGraph(1);
        return;
      }
    } else {
      fade -= 0x20;
      if (fade < 0) fade = 0;
    }
    const u16 pressed = pad_pressed(g.pad, 0);
    const u16 repeat = pad_pressed_repeat(g.pad, 0);
    pad_clear(g.pad, 0);
    if (!closing) {
      if (pressed & 0x800) {                      // Start: done
        if (name_box_commit(box, name)) {
          closing = true;
          sound_play_sfx(g.sound, 2, 0, 0x7F40);
        } else {
          sound_play_sfx(g.sound, 7, 0, 0x7F40);
        }
      } else if (pressed & button(0)) {
        const int r = keyboard_confirm(kb, box);
        if (r == 1) {                              // OK
          const bool ok = name_box_commit(box, name);
          if (ok) closing = true;
          sound_play_sfx(g.sound, ok ? 2 : 7, 0, 0x7F40);
        }
        if (r == 2) {                              // Return: the name on entry
          std::strcpy(name, box->original);
          closing = true;
          sound_play_sfx(g.sound, 6, 0, 0x7F40);
        }
      } else {
        if (pressed & button(1)) name_box_backspace(box);
        // FR / DE: one character set, no switch (text_rules.h NameKeyboard).
        const bool charset_switch = text::name_keyboard(text::lang()).charset_switch;
        if (charset_switch && (repeat & (button(5) | button(7)))) {
          kb->selection = (kb->selection + 1) & 1;
          sound_play_sfx(g.sound, 1, 0, 0x7F40);
          if (keyboard_rows(kb->selection) <= kb->row) kb->row = keyboard_rows(kb->selection);
        }
        if (charset_switch && (repeat & (button(4) | button(6)))) {
          if (--kb->selection < 0) kb->selection = 1;
          sound_play_sfx(g.sound, 1, 0, 0x7F40);
        }
        if (pressed & 0x100) {                     // Select: the name on entry again
          sound_play_sfx(g.sound, 3, 0, 0x7F40);
          name_box_reset(box);
          if (box->length == 7) keyboard_jump_full(kb);
        }
        if (repeat & 0x8000) keyboard_move(kb, box, -1, 0);
        if (repeat & 0x2000) keyboard_move(kb, box, 1, 0);
        if (repeat & 0x1000) keyboard_move(kb, box, 0, -1);
        if (repeat & 0x4000) keyboard_move(kb, box, 0, 1);
      }
    }
    if (keyboard_rows(kb->selection) <= kb->row) kb->row = keyboard_rows(kb->selection);
    // The heading (NA w 0x80) and the subtitle (NA x 0x88, w 0x80, u 0x80, tpage 0x25).
    const text::Layout& lay = text::layout(text::lang());
    screen_sprite(ctx, 8, 0x0C, lay.rename_heading_w, 0x20, 0, 0, 5, 0x7801, 0x80, false);
    screen_sprite(ctx, lay.rename_subtitle[0], 0x1C, lay.rename_subtitle[1], 0x10, lay.rename_subtitle[2], 0,
                  lay.rename_subtitle[3], 0x7806, 0x70, true);
    kb->draw();
    box->draw();
    const int width = character >= 0 && character < 12 && widths ? widths[character] : 0;
    screen_sprite(ctx, 0, 0, width, 0xF0, 0, 0, 0x115, 0x7880, 0x80, false);      // the picture
    {
      // The backdrop: dark cyan at the top left to black at the bottom left.
      auto* p = screen_alloc_prims<psyq::POLY_G4>(ctx->arena, 1);
      psyq::setlen(p, 8);
      set_rgb(&p->r0, 0x606000);
      p->code = 0x38;
      set_rgb(&p->r1, 0x303000);
      set_rgb(&p->r2, 0);
      set_rgb(&p->r3, 0x303000);
      p->x0 = 0, p->y0 = 0;
      p->x1 = 0x140, p->y1 = 0;
      p->x2 = 0, p->y2 = 0xF0;
      p->x3 = 0x140, p->y3 = 0xF0;
      psyq::addPrim(ctx->ot_head, p);
      add_draw_mode(ctx, 0, 1, 0);
    }
    text_frame_upload();
    if (++cycle > 3) {
      cycle = 0;
      const u16 first = colours[0];
      for (int i = 0; i < 8; ++i) colours[i] = colours[i + 1];
      colours[8] = first;
    }
    psyq::LoadImage(&cycle_rect, colours);
    psyq::DrawSync(0);
    psyq::VSync(0);
    screen_put_env();
    screen_draw_ot(ctx->ot);
    screen_draw_ot(box->ctx->ot);
    screen_draw_ot(kb->ctx->ot);
    if (reveal != 0 && --reveal == 0) psyq::SetDispMask(1);
  }
}

// 0x8007e308: keeps the menu's VRAM the screen uses and puts it back afterwards.
void rename_run(char* name, int character, std::span<const u8> pack, std::vector<u8>& picture) {
  // France / Germany's art is wider: their copy also keeps (0x180, 0, 0x40, 0x40).
  static constexpr psyq::RECT kAreas[5] = {{0x140, 0, 0x40, 0x100},
                                           {0x140, 0x100, 0xE8, 0xF0},
                                           {0x100, 0x1E0, 0x100, 2},
                                           {0, 0x1C8, 0x140, 0x18},
                                           {0x180, 0, 0x40, 0x40}};
  const int areas = text::layout(text::lang()).rename_extra_vram ? 5 : 4;
  std::vector<u16> saved[5];
  for (int i = 0; i < areas; ++i) {
    saved[i].assign(static_cast<size_t>(kAreas[i].w) * kAreas[i].h, 0);
    psyq::StoreImage(&kAreas[i], saved[i].data());
  }
  psyq::DrawSync(0);
  rename_screen(name, character, pack, picture);
  for (int i = 0; i < areas; ++i) psyq::LoadImage(&kAreas[i], saved[i].data());
  psyq::DrawSync(0);
}

// ---- The page (Panel, 0xa4 bytes, vtable 0x800823c4; 0xbce 0x8007e000) -------------------
class RenamePage final : public menu::Panel {
 public:
  RenamePage() {
    menu::strings_set(menu::globals().data.data());
    load_ = static_cast<s16>(menu::bgload_queue(&client_, 0xBD3 + party_selected_char(), 0, nullptr, 0, 0));
    step_ = 0;
    flags |= 2;   // no input
  }
  ~RenamePage() override { menu::bgload_cancel_owner(&client_); }   // 0x80080aa4
  void frame() override;                     // [9] 0x8007e098

 private:
  menu::BgLoadClient client_;                // +0x98
  s16 load_ = 0;                             // +0xa0
  s16 step_ = 0;                             // +0xa2
};

void RenamePage::frame() {
  Panel::frame();
  if (step_ == 0) {
    if (load_ == 0 || !menu::bgload_is_done(load_)) return;
  } else if (step_ == 1) {
    std::vector<u8> picture;
    if (menu::LoadRequest* req = menu::bgload_request(load_)) {
      // (Flag 8 cleared so the cancel leaves the buffer; the screen frees it.)
      if (!req->owned.empty()) {
        picture = std::move(req->owned);
      } else if (req->dest != nullptr) {
        picture.assign(req->dest, req->dest + (static_cast<size_t>(req->sectors) << 11));
      }
      req->dest = nullptr;
      req->flags = static_cast<u16>(req->flags & ~8);
    }
    menu::bgload_cancel(load_);
    load_ = 0;
    menu::MenuScene* scene = menu::globals().scene;
    scene->frame_end();
    const int character = party_selected_char();
    const std::vector<u8>& data = menu::globals().data;
    const u8* pack = menu::strings_block(0);
    char* name = reinterpret_cast<char*>(party_selected_record() + 0x24);
    rename_run(name, character - 1, pack_span(pack, data.data() + data.size()), picture);
    // character_name (0x80055f78): game_state +0x28 + (character - 1) * 0x14.
    std::memcpy(globals().game_state + 0x28 + (party_selected_char() - 1) * 0x14, name, 0x14);
    for (int i = 0; i < 2; ++i) screen_dispenv(i)->isinter = 0;
    screen_setup_buffer0(0, 0, 0x140, 0xF0, 0xC, 0xC, 4, 4);
    screen_setup_buffer1(0, 0xD8, 0x140, 0xF0, 0xC, 0xC, 4, 4);
    for (int i = 0; i < 2; ++i) {
      psyq::DRAWENV* d = screen_drawenv(i);
      d->isbg = 1;
      d->dtd = 1;
      d->r0 = d->g0 = d->b0 = 0;
    }
    scene->frame_begin();
    cancel();
  } else {
    return;
  }
  ++step_;
}

}  // namespace

void menu_page0e_update(menu::MenuPageLoader* p) {
  switch (p->substate) {
    case 1:
      p->publish_id();
      p->push(new RenamePage);
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
