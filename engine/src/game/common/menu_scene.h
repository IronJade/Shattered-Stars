#pragma once

#include <vector>

#include "core/types.h"
#include "game/common/menu_core.h"
#include "game/common/screen.h"

namespace ss::game {
struct MemoryCard;
}

// The menu scene, the session and the page loader shared by the title (load-game
// screen) and the field (camp menu, save points, shops).
//
// How a menu runs:
//   1. the caller unpacks the resource pack (TIMs) into g_menu_res and the page data
//      into g_menu_data, and makes the sprite pack (0xbd2) resident;
//   2. MenuSession (title 0x80030b0c / field 0x8003253c) records the start page;
//   3. MenuScene (title 0x8002fff4 / field 0x8002f9f8) sets up the 320x240 screen,
//      the window list and the menu VRAM;
//   4. menu_scene_run (title 0x800305c4 / field 0x8002ffdc) loops frame_begin, the
//      page loader (which creates each page's widget), the window list update and
//      frame_end until the pages reach page 0x14 (close);
//   5. the destructors restore the VRAM the menu borrowed and free everything.
namespace ss::game::menu {

class MenuPageLoader;
class Widget;

// Session (0x38 bytes): what the menu was opened for and what it returns.
struct MenuSession {
  MenuSession();                      // title 0x80030b0c / field 0x8003253c
  ~MenuSession();                     // title 0x80030c00 / field 0x80032630
  void free_data();                   // title 0x80030cbc / field 0x800326ec
  void free_cache();                  // title 0x80030d24 / field 0x80032754
  void build_button_map();            // title 0x80030d64 / field 0x80032794

  s32 heap_free = 0;      // +0x00
  MenuScene* scene = nullptr;  // +0x04
  std::vector<u8> buffer; // +0x08
  u16 help_flags = 0;     // +0x0c: the focused panel's help-bar flags
  s16 member = 0;         // +0x0e: party slot picked in the camp menu
  s16 member_scroll = 0;  // +0x10: its list scroll (quarter rows)
  s16 arg = 0;            // +0x12: argument of script-opened menus
  s8 page_id = 0;         // +0x14: current page (menu_pages_publish_id)
  s8 load_mode = 0;       // +0x15: page 0x11 loads (1) or saves (0)
  s8 result = 0;          // +0x16: 1 = a game was loaded (handoff 0xe is kept)
  s8 single_member = 0;   // +0x17
  s8 command = 0;         // +0x18: the camp menu's last command
  u8 cached_0bd2 = 0;     // +0x19: free entry 0xbd2 on destruction
  u8 cached_0bbb = 0;     // +0x1a
  s8 unknown1b = 0;       // +0x1b: everyone is down (0x8003284c; camp result 2)
  s8 variant = 0;         // +0x1c: page variant (4 / 0xb)
  s8 start_page = 0;      // +0x1d
  s8 select_result = 0;   // +0x1e
  s8 unknown1f = 0;       // +0x1f
  u8 button_map[16] = {}; // +0x20: icon cell for each config button
  s8 unknown30 = 0;       // +0x30
  s8 field_query = 0;     // +0x31
  s8 creation_bonus = 0;  // +0x32: item effect 0x3e (100), read by the item creation pages
  s8 creation_aid = 0;    // +0x33: item effect 0x3d, read by the item creation pages
  s8 inventory_dirty = 0; // +0x34
  s8 unknown35 = 0;       // +0x35: item effect 0x60 (camp result 6, the way out of a dungeon)
};

// The resource pack's two window-art TIMs (4-bit, 16 CLUT rows each) and where the scene
// puts them: TIM 0's pixels are 64x128 and TIM 1's 64x48 (field 0x8002fc38..0x8002fcbc /
// title 0x80030218..0x8003029c build these on the stack).
struct WindowArt {
  psyq::RECT pixels;
  s16 clut[4];  // x, y, (unused), CLUT rows
};
inline constexpr WindowArt kWindowArt[2] = {
    {{0x180, 0x100, 0x40, 0x80}, {0x100, 0x1E0, 1, 0x10}},
    {{0x180, 0x1C8, 0x40, 0x30}, {0x110, 0x1E0, 1, 0x10}},
};

// Scene (title 0x3c bytes on the stack / field 0x40).
struct MenuScene {
  explicit MenuScene(MemoryCard* card);  // title 0x8002fff4 / field 0x8002f9f8
  ~MenuScene();                          // title 0x8003036c / field 0x8002fd8c
  int run();                             // title 0x800305c4 / field 0x8002ffdc
  void frame_begin();                    // title 0x800306a4 / field 0x800300dc
  void frame_end();                      // title 0x8003083c / field 0x8003029c
  // TIM #index of g_menu_res (title 0x80030974 / field 0x800303d4); the original's self
  // argument is unused. false (nothing uploaded) when the pack lacks its pixels or CLUTs.
  static bool upload_tim(int index, const psyq::RECT& rect, const s16 clut[4]);

  RenderContext* ctx = nullptr;          // +0x04
  WindowList* windows = nullptr;         // +0x0c
  MenuPageLoader* pages = nullptr;       // +0x10
  MemoryCard* memcard = nullptr;         // +0x14
  s16 external_memcard = 0;              // +0x18: the caller owns the card manager
  s16 input_off = 0;                     // +0x1a
  u32 held = 0;                          // +0x1c (lowest set bit)
  u32 repeat = 0;                        // +0x20
  u32 pressed = 0;                       // +0x24
  s32 vsync_result = 0;                  // +0x28
  s32 frames = 0;                        // +0x2c
  s32 vsync_mode = 0;                    // +0x30: 0/1/2/10/60 vblanks
  std::vector<u16> saved_icons;          // +0x34: VRAM (0x200,0x100) 64x128
  std::vector<u16> saved_cluts;          // +0x38: VRAM (0,0x1e0) 320x16
  bool owns_memcard = false;
};

// Page loader (0x70 bytes): a stack of pages; each page creates one widget and
// reacts to its result.
class MenuPageLoader {
 public:
  MenuPageLoader();                       // title 0x8003540c / field 0x80034dd0
  ~MenuPageLoader();                      // title 0x8003544c / field 0x80034e1c
  void request(int page);                 // title 0x8003549c / field 0x80034e6c
  int update();                           // title 0x8003562c / field 0x80035020
  void goto_page(s16 page);               // title 0x80035aa4 / field 0x80035fa8
  void pop();                             // title 0x80035afc / field 0x80036000
  void publish_id();                      // title 0x80035b50 / field 0x80036054
  void push(Widget* page);          // title 0x80035b64 / field 0x80036068

  s16 page = 0;            // +0x00
  s16 substate = 0;        // +0x02: 1 create, 0 running
  s16 choice = 0;          // +0x04
  BgLoadClient client;     // +0x08
  Widget* current = nullptr;  // +0x14
  s16 depth = 0;           // +0x10
  struct Entry {           // +0x18: {Widget*, page id} (8 bytes)
    Widget* widget = nullptr;
    s16 id = 0;
  } stack[8];
  std::vector<u8> code_dest;  // +0x58: page code destination (field: 0x8007e000)
  std::vector<u8> read_buf;   // +0x5c
  s32 entry = 0;              // +0x60
  s32 code_size = 0;          // +0x64
  s32 data_size = 0;          // +0x68
  s16 loads_on = 0;           // +0x6c
};

// Page handlers (title 0x80035940-0x80035a94 / field 0x80035334-0x80035f0c). The
// title links only pages 0 and 0x11; the field registers its own (game/field/menu_camp).
// A page without a handler logs and goes back to the page below it.
void menu_page00_start(MenuPageLoader* p);   // title 0x80035940 / field 0x80035334
void menu_page11_update(MenuPageLoader* p);  // title 0x800359b8
using PageHandler = void (*)(MenuPageLoader* p);
void set_page_handler(int page, PageHandler handler);  // null restores the default

// Called once per menu frame after the background loads (field 0x800300dc: the Callways
// walking effects keep running while the field's menus are open).
void set_menu_frame_hook(void (*hook)());

}  // namespace ss::game::menu
