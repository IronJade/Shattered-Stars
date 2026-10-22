#pragma once

#include <vector>

#include "core/types.h"
#include "game/common/screen.h"
#include "runtime/libgpu.h"

// Drawing services of the menu framework: the VRAM rectangle allocator text lines
// are rendered into ("CVram", title 0x8006d348-0x8006dfe8 / menu_0ba5 0x800d9930..),
// the deferred VRAM upload queue (title 0x8006dfc8-0x8006e3ac), the text renderer
// that rasterises the menu font into 4-bit images (title 0x80032134-0x80033d2c) and
// the animated character sprites ("face" objects with a VRAM cell cache, title
// 0x80067d4c-0x80069098).
namespace ss::game::menu {

// ---- Primitive helpers ---------------------------------------------------------------

// A POLY_FT4 in the console's layout (0x28 bytes), as the menu code keeps templates
// for icons and sprite frames. The tag word is unused.
struct PolyFT4Raw {
  u32 tag;
  u8 r0, g0, b0, code;
  s16 x0, y0;
  u8 u0, v0;
  u16 clut;
  s16 x1, y1;
  u8 u1, v1;
  u16 tpage;
  s16 x2, y2;
  u8 u2, v2;
  u16 pad1;
  s16 x3, y3;
  u8 u3, v3;
  u16 pad2;
};
static_assert(sizeof(PolyFT4Raw) == 0x28);

// Allocates a primitive from a context's arena (zeroed; null when the arena is full).
template <typename T>
T* prim_alloc(const RenderContext* ctx) {
  return screen_alloc_prims<T>(ctx->arena, 1);
}
// addPrim into entry 'slot' of the context's current ordering table.
inline void prim_link(const RenderContext* ctx, int slot, void* prim) { psyq::addPrim(ctx->ot_head + slot, prim); }
psyq::POLY_FT4* poly_ft4_from_raw(const RenderContext* ctx, const PolyFT4Raw& raw);

// ---- VRAM rectangle allocator (title 0x80074bcc table) -------------------------------
// Entries (12 bytes): flags (1 slot used, 2 free region, 4 4-bit page, 8 8-bit page,
// 0x10 to be freed), x, y, w, h (VRAM units), tpage. Handles are index + 1.
struct VramRect {
  u16 flags = 0;
  s16 x = 0, y = 0, w = 0, h = 0;
  u16 tpage = 0;
};
void cvram_init();                                    // title 0x8006d348: four free regions
void cvram_release();                                 // title 0x8006d3f0
void cvram_merge_free();                              // title 0x8006d424 / 0x8006d4c8: coalesce every free region
int cvram_add(int x, int y, int w, int h);            // title 0x8006dac0 (-1 when empty)
int cvram_alloc(int w, int h);                        // title 0x8006d8a4 ("CVram::Valloc")
int cvram_alloc4(int width_px, int h);                // title 0x8006d77c: 4-bit image, sets the tpage
s16 cvram_x(int handle);                              // title 0x8006d588
s16 cvram_y(int handle);                              // title 0x8006d5b4
s16 cvram_u(int handle);                              // title 0x8006d668
s16 cvram_v(int handle);                              // title 0x8006d704
void cvram_mark_free(int handle);                     // title 0x8006dcd4
void cvram_free_marked();                             // title 0x8006dd10
void cvram_merge(int handle);                         // title 0x8006ddc4
u16 cvram_tpage(int handle);

// ---- Deferred VRAM uploads (title 0x80074be0, 0x18-byte entries) ----------------------
// flags: 1 pending, 2 LoadImage of one rectangle, 4 rows split over two rectangles,
// 8 free the pixels afterwards.
void upload_queue_init();                              // title 0x8006dfc8
void upload_queue_release();                           // title 0x8006dfe8
void upload_image(const psyq::RECT& r, std::vector<u16> pixels);                        // title 0x8006e01c (2|8)
void upload_image_ref(const psyq::RECT& r, const u8* pixels);                           // title 0x8006e140 (2)
void upload_image_split(const psyq::RECT& a, const psyq::RECT& b, std::vector<u16> pixels);  // title 0x8006e1a0 (4|8)
void upload_free_later(std::vector<u8> data);                                           // title 0x8006e0b4 (8)
void upload_queue_flush();                             // title 0x8006e3ac (once per frame)

// ---- Text renderer (title 0x8003214c, 0x114 bytes) -------------------------------------
// Each text line is rasterised once into a 4-bit image in VRAM (two allocations
// when it is 200 pixels or wider) and drawn as textured quads with a drop shadow.
struct TextIcon {  // 8 bytes: a button icon inside a line
  s16 x = 0;       // +0
  s16 id = 0;      // +2: index into the session's button map
};
struct TextLine {        // 0x28 bytes
  u16 flags = 0;         // +0x00 (bit 0: hidden)
  s16 width = 0;         // +0x02: text width in pixels
  u16 alloc_w = 0;       // +0x04: image width (rounded up to 4 + 1)
  u16 w1 = 0;            // +0x06: first image width
  u16 w2 = 0;            // +0x08: second image width (0 = one image)
  u16 height = 0;        // +0x0a
  u16 u1 = 0, v1 = 0;    // +0x0c / +0x0e
  u16 u2 = 0, v2 = 0;    // +0x10 / +0x12
  s16 x = 0;             // +0x14
  s16 x2 = 0;            // +0x16
  s16 y = 0;             // +0x18
  u16 tpage1 = 0;        // +0x1a
  u16 tpage2 = 0;        // +0x1c
  s16 vram1 = 0;         // +0x1e
  s16 vram2 = 0;         // +0x20
  u16 clut = 0;          // +0x22
  std::vector<TextIcon> icons;  // +0x24 (a linked list in the original)
};

class TextRenderer {
 public:
  TextRenderer();                                   // title 0x8003214c
  ~TextRenderer();                                  // title 0x80032270
  void set_owner_colour(const u32* colour) { colour_ = colour; }  // title 0x800322f8 (+4 owner, +0xd0 owner + 0x20)
  void set_font(const u8* font, const u8* strings); // title 0x80032320
  void set_group(s16 group) { group_ = group; }     // title 0x8003236c
  s16 group() const { return group_; }               // title 0x80032374
  // title 0x80032308: the font and message pack set_font took.
  void get_font(const u8** font, const u8** strings) const { *font = font_, *strings = strings_; }
  const u8* message(int index) const;               // title 0x80032380
  void free_line(int index);                        // title 0x800323bc
  void set_pen(s16 x, s16 y, s16 step) { x_ = x; y_ = y; step_ = step; }  // title 0x80032488
  void set_colour(s16 colour) { colour_index_ = colour; }                 // title 0x80032498
  int add_line(const u8* text);                     // title 0x80032558: measure, allocate, render
  int alloc_line(int width, u16 flags);             // title 0x800325b4
  void render_line(int index, const u8* text);     // title 0x800327f8
  void begin_line(int index);                       // title 0x80032840
  void set_tables(int colour);                      // title 0x80032918
  void set_pen_x(int x);                            // title 0x80032950
  int pen_x() const;                                // title 0x80032984
  void upload_line();                               // title 0x800329b0
  void draw_all(const RenderContext* ctx, int slot, s16 x, s16 y);  // title 0x80033068
  void draw_line(int index, const RenderContext* ctx, int slot, s16 x, s16 y);  // title 0x8003312c
  int measure(const u8* text);                      // title 0x800339a8
  void draw_text(TextLine* line, const u8* text);   // title 0x80033a2c (line null: measure only)
  void draw_glyph(int glyph, int stride_px);        // title 0x80033d2c
  // title 0x800324a0 / field 0x800d7cf0: a new line of 'digits' * 6 pixels showing 'value'.
  int add_number(s32 value, int digits);
  // title 0x80032b3c / field 0x800d838c: 'value' right-aligned in digits * 6 pixels from the
  // pen into the current line; the digit glyphs are messages 0-9 of the pack's group 0
  // ('-' message 12).
  void draw_number(s32 value, int digits);
  // title 0x80032524 / field 0x800d7d74: add_line(message(index)).
  int add_message_line(int index);
  // title 0x800339f8 / field 0x800d9248: draw_text into the current line.
  void draw_current(const u8* text) { draw_text(lines_[current_], text); }
  // field 0x800d86f4: message 'index' of the pack's group 0 into the current line.
  void draw_pack_message(int index);
  // field 0x800d84a8: 'value' (0-99) in group 0 digits, right-aligned 12 pixels past the pen.
  void draw_two_digits(int value);
  s16 current_line() const { return current_; }
  s16 x() const { return x_; }
  s16 y() const { return y_; }
  TextLine* line(int i) { return lines_[static_cast<size_t>(i)]; }
  s16 line_count() const { return count_; }
  s16 height() const { return height_; }

  // Font tables shared by every renderer (title 0x8004ece8 / 0x8004ecec): blocks 1
  // and 0 of the page data.
  static void set_default_font(const u8* font, const u8* strings);  // title 0x80032134
  // title 0x80033fb0: the next entry of the 0x4009 expansion table (0x8004efb0).
  static void set_expansion_cursor(int index);
  // The field's text code 0x4006 n draws character n's name (menu_0ba5 0x800d9388;
  // the title's copy skips it): the host gives the name (field 0x80055f78).
  static void set_name_source(const char* (*source)(int index));

 private:
  s16 count_ = 0;                    // +0x02
  TextLine* lines_[50] = {};         // +0x08
  const u32* colour_ = nullptr;      // +0xd0: the owner's brightness colour
  const u8* font_ = nullptr;         // +0xd4
  const u8* widths_ = nullptr;       // +0xd8
  const u8* glyphs_ = nullptr;       // +0xdc
  const u8* strings_ = nullptr;      // +0xe0
  s16 height_ = 0;                   // +0xe4
  s16 group_ = 0;                    // +0xe6
  s16 x_ = 0x1E, y_ = 100, step_ = 0;  // +0xe8 / +0xea / +0xec
  s16 colour_index_ = 1;             // +0xee
  RenderContext ctx_;                // +0xf0
  s16 current_ = 0;                  // +0xf4
  std::vector<u16> buffer_;          // +0xf8
  size_t cursor_ = 0;                // +0xfc (u16 index into buffer_)
  s16 sub_ = 0;                      // +0x100: pixel within the u16
  int table_ = 1;                    // +0x104/+0x108/+0x10c: colour table set
  s16 pen_ = 0;                      // +0x110
};

// ---- Character sprites ("faces", title 0x80067d9c, 0x3c bytes) -------------------------
// Animated sprites from the resident sprite pack (entry 0xbd2), drawn through a cache
// of frame cells uploaded into a VRAM region.
struct SpriteCellCache;
struct Sprite {
  s16 x = 0, y = 0;       // +0x00 / +0x02
  s16 scale = 0;          // +0x04 (0x1000 = 1)
  s16 mode = 3;           // +0x06: palette column (0 normal, 1 / 2 the greyed copies)
  u32 colour = 0;         // +0x08
  s16 anim = 0;           // +0x0c: animation (direction * 12 + pose)
  s16 timer = 0;          // +0x0e
  s16 frame = 0;          // +0x10
  s16 gfx = 1;            // +0x12: graphics number (character)
  u8 backwards = 1;       // +0x14: ping-pong direction
  s16 loaded_anim = -1;   // +0x16
  const u8* pack = nullptr;        // +0x18: sub-pack of the sprite data
  const u8* cells = nullptr;       // +0x1c: cell table (frame_info + 0x14; 4 or 8 bytes a cell:
                                   //        rect index, dx, dy, flip bits)
  const u8* frame_info = nullptr;  // +0x20: [2] == 1: 4-byte cells
  const u8* rects = nullptr;       // +0x24: 12-byte image rectangles (+1 width px, +2 height,
                                   //        +4/+6 origin, +8 pixel offset)
  const u8* pixels = nullptr;      // +0x28: base of the image data
  const u8* clut = nullptr;        // +0x2c: u16 flags (bit 15: 8-bit images), CLUT at +4
  const u8* script = nullptr;      // +0x30
  u16 cell = 0;                    // +0x34
  SpriteCellCache* cache = nullptr;  // +0x38
};
void sprite_cache_init();                               // title 0x80067d4c
void sprite_cache_release();                            // title 0x80067d74
void sprite_cache_clear();                              // title 0x80068e40
void sprite_cache_new_frame();                          // title 0x8006902c
SpriteCellCache* sprite_cache_acquire(const s16 region[4]);  // title 0x80068e80
void sprite_cache_release_region(SpriteCellCache* c);   // title 0x80068fe0
void sprite_init(Sprite* s);                            // title 0x80067d9c
void sprite_destroy(Sprite* s);                         // title 0x80067de4
void sprite_sync_shared(Sprite* s);                     // title 0x80067e3c
void sprite_set(Sprite* s, const s16 pos_gfx_anim[4], s16 scale, u32 colour, s16 mode);  // title 0x80067e90
void sprite_draw(Sprite* s, const RenderContext* ctx, int slot);  // title 0x80067eec
void sprite_step(Sprite* s);                            // title 0x80068554
const u8* sprite_find_pack(u32 anim, u32 gfx);          // title 0x80069098

}  // namespace ss::game::menu
