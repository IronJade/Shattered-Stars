#pragma once

#include <string>
#include <vector>

#include "core/types.h"
#include "game/boot/system.h"
#include "game/common/party.h"

// The menu framework both the title and the field program link in (title
// 0x8002fff4-0x8003c000 + 0x80067d4c-0x80074fe4; field 0x8002f9f8-0x8003d000 plus the
// menu overlay 0xba5 at 0x800d1b28). This header holds its kernel: the task base
// class, window lists, the menu globals, the entry cache, the background loader
// and the small string / party helpers. Widgets are in menu_widget.h, the scene,
// session and page loader in menu_scene.h.
//
// Addresses are given as "title X / field Y" where both are known; the field copy
// of the library code that lives in the 0xba5 overlay is "menu_0ba5 Y".
namespace ss::game {
struct MemoryCard;
}

namespace ss::game::menu {

struct MenuScene;
struct MenuSession;

// Which program runs the menus. The library's constant tables live in that
// program's image (or in the menu overlays the field loads).
enum class Host { Title, Field };
void set_host(Host host);
Host host();

// A constant table of the running program: its address in the title image and in
// the field image (0 = not located yet).
const u8* table(u32 title_address, u32 field_address);

// The field links the library from overlays: the class library (entry 0xba5 at
// 0x800d1b28; the title's copy of each function is at a fixed distance from it, but its
// data is laid out differently) and the current page's code (at 0x8007e000).
constexpr int kLibraryEntry = 0xBA5;
constexpr u32 kLibraryBase = 0x800D1B28;
constexpr u32 kLibraryEnd = 0x800E14D0;
constexpr u32 kPageCodeBase = 0x8007E000;
// A field address: the library overlay, the loaded page code (set_page_code_entry) or
// the field program.
const u8* field_table(u32 address);
// A constant of a page module's code entry (loaded at 0x8007e000).
const u8* page_table(int entry, u32 address);
void set_page_code_entry(int entry);   // the page code the field has at 0x8007e000
int page_code_entry();

// ---- Task base class (title 0x80038228 / field 0x8003872c) -----------------------
// vtable slots: [1] +0x08 dtor, [2] +0x10 type, [3] +0x18 close, [4] +0x20 on_close,
// [5] +0x28 update. Window lists drive tasks through these.
class MenuTask {
 public:
  virtual ~MenuTask() = default;           // [1] title 0x8003823c / field 0x80038740 task_destroy
  virtual int type() { return 0; }         // [2] title 0x80038270 / field 0x80038774 task_type
  virtual void close() { on_close(); }     // [3] title 0x80038278 / field 0x8003877c task_close
  virtual void on_close() {}               // [4] title 0x800382ac / field 0x800387b0 task_on_close
  virtual void update() {}                 // [5] title 0x800382b4 / field 0x800387b8 task_update
};

// ---- Window list (title 0x80036074 / field 0x80036578, 0x18 bytes) --------------
// An ordered list of tasks, each on a layer 0-4; the last active entry that is not
// unfocusable has the focus. Entry flags: 1 active, 2 closing, 4 dying, 8 dead,
// 0x10 unfocusable.
struct WindowEntry {  // 12 bytes
  MenuTask* task = nullptr;  // +0
  s32 layer = 0;             // +4
  u32 flags = 0;             // +8
};

class WindowList : public MenuTask {
 public:
  WindowList() = default;                                   // title 0x80036074 / field 0x80036578
  ~WindowList() override;                                   // title 0x800360bc / field 0x800365c0
  void add(MenuTask* task, int layer);                      // title 0x80036124 / field 0x80036628
  int set_unfocusable(MenuTask* task);                      // title 0x800361cc / field 0x800366d0
  void update() override;                                   // title 0x80036240 / field 0x80036744
  void close_type(int type);                                // title 0x800363a4 / field 0x800368a8 (-1 = all)
  void close_all();                                         // title 0x800364a0 / field 0x800369a4
  int kill(MenuTask* task);                                 // title 0x80036540 / field 0x80036a44
  void purge();                                             // title 0x800365a4 / field 0x80036aa8
  MenuTask* focus() const { return focus_; }                // title 0x80036658 / field 0x80036b5c
  int depth_of(MenuTask* task) const;                       // title 0x80036664 / field 0x80036b68
  void compact();                                           // title 0x800366fc / field 0x80036c00
  void update_focus();                                      // title 0x800367b4 / field 0x80036cb8
  void append(const WindowEntry& e);                        // title 0x80036818 / field 0x80036d1c

  std::vector<WindowEntry> entries;  // +0x08 array, +0x0c count, +0x14 capacity
 private:
  MenuTask* focus_ = nullptr;        // +0x10
};

// ---- Menu globals ---------------------------------------------------------------
// The library's globals (title 0x8004dd94-0x8004efd0; field 0x8007405c-...).
struct MenuGlobals {
  MenuScene* scene = nullptr;       // title 0x8004dd94 / field 0x8007405c g_menu_scene
  MenuSession* session = nullptr;   // title 0x8004ddc4 / field 0x80074074 g_menu_session
  std::vector<u8> res;              // title 0x8004ddb0 / field 0x80074060 g_menu_res: resource pack (TIMs)
  std::vector<u8> data;             // title 0x8004ddb4 / field 0x80074064 g_menu_data: page data block
  std::vector<u8> item_data;        // title 0x8004ddb8 / field 0x80074068: item pack (entry 3000), empty = not loaded
  const u8* item_table = nullptr;   // field inventory +0x820: the item records (entry 3002, 0x30 bytes each)
  const u8* strings = nullptr;      // title 0x8004de94: string pack the message lookups use (= data)
  // Pointers the session reloads from the handoff registry (menu_session_create).
  u8* config = nullptr;             // title 0x8004ebfc: handoff 2 (0x2a0 bytes)
  u8* inventory = nullptr;          // title 0x8004dda4: handoff 6
  u8* party = nullptr;              // title 0x8004dda8: handoff 3, 8 x 0x60
  u8* records = nullptr;            // title 0x8004ddac: handoff 4, 8 x 0xd0
  u8 input_lock = 0;                // title 0x8004ece4: frames the pad is ignored after a press
};
MenuGlobals& globals();

System* menu_system();
HandoffRegistry* menu_handoff();
Loader* menu_loader();
Sound* menu_sound();
Pad* menu_pad();

// ---- Entry cache (title 0x80031070 / field 0x80053940; 16 slots at title
// 0x8004ddd4 / field 0x8007585c of {ptr, entry, size}) ------------------------------
// Unpacked archive entries kept resident while menus run (0xbd2: the sprite pack;
// 0xbbb).
void entry_cache_load(const u8* packed, int entry);      // title 0x80031070
void entry_cache_free(int entry);                         // title 0x80031194 / field 0x80032aec
const std::vector<u8>* entry_cache_slot(int index);       // slot contents (null when empty)
inline void entry_cache_load_0bd2(const u8* packed) { entry_cache_load(packed, 0xBD2); }  // title 0x80030fa8 / field 0x800328f8
inline void entry_cache_free_0bd2() { entry_cache_free(0xBD2); }   // title 0x80030fc8 / field 0x8003291c
inline void entry_cache_free_0bbb() { entry_cache_free(0xBBB); }   // title 0x80031008 / field 0x80032960
inline void entry_cache_load_0bbb(const u8* packed) { entry_cache_load(packed, 0xBBB); }  // title 0x80030fe8 / field 0x8003293c
// The field keeps its resident sprite packs in the same 16-slot table (0x8007585c,
// sprite_pack_register); a field host points the cache at it.
struct EntryCacheSlotRef {
  std::vector<u8>* data;
  s32* entry;
};
void set_entry_cache_table(EntryCacheSlotRef (*slot)(int index));  // null = the menu's own table

// ---- Background loads (title 0x80038644-0x80039060 / field 0x80038b48-0x80039418) --
// Requests (0x14 bytes) owned by a client task; async_load_queue_update starts and
// finishes them one at a time. Request flags: 1 used, 2 pending, 4 reading,
// 8 owns its buffer, 0x20 cancel.
class BgLoadClient : public MenuTask {   // title 0x80038644 / field 0x80038b48 bgload_client_create (8 bytes)
 public:
  ~BgLoadClient() override;              // title 0x8003867c / field 0x80038b80
};
struct LoadRequest {   // 0x14 bytes
  u16 flags = 0;       // +0x00
  s16 entry = 0;       // +0x02
  u8* dest = nullptr;  // +0x04
  s16 offset = 0;      // +0x0c: sector offset into the entry
  s16 sectors = 0;     // +0x0e
  BgLoadClient* owner = nullptr;  // +0x10
  std::vector<u8> owned;          // the buffer when flag 8 (mem_alloc'd by bgload_queue)
};
void bgload_init();                                               // title 0x800386c8 / field 0x80038bcc
void bgload_shutdown();                                           // title 0x800386fc / field 0x80038c70
void bgload_cancel(int handle);                                   // title 0x80038760 / field 0x80038d10
int bgload_queue_entry(BgLoadClient* owner, int entry, u8* dest); // title 0x800388b0 / field 0x80038eb8
int bgload_queue_bytes(BgLoadClient* owner, int entry, u8* dest, int bytes);  // title 0x800388e4 / field 0x80038eec
int bgload_queue(BgLoadClient* owner, int entry, u16 flags, u8* dest, int offset, int sectors);  // title 0x80038960
void bgload_cancel_owner(BgLoadClient* owner);                    // title 0x80038ba4 / field 0x800391ac
bool bgload_owner_done(BgLoadClient* owner);                      // title 0x80038c80 / field 0x80039288
bool bgload_is_done(int handle);                                  // title 0x80038d14 / field 0x8003931c
LoadRequest* bgload_request(int handle);                          // title 0x80038dbc
void async_load_queue_update();                                   // title 0x80038e10 / field 0x80039418 bgload_update

// ---- Strings (title 0x80035bc8-0x80036028 / field 0x800360cc-0x8003652c) ---------
int str_len(const char* s);                       // title 0x80035bc8
void str_cat(char* dst, const char* src);         // title 0x80035bec
const char* str_chr(const char* s, int c);        // title 0x80035c3c

// StrBuf (title 0x80035f8c, 0x10 bytes): a 0x100-byte buffer and the %-spec state
// used to format numbers: +0 pad char, +1 'r'/'l' justification, +2 width, +4 radix.
struct StrBuf {
  u8 pad = ' ';         // +0
  u8 justify = 'r';     // +1
  s16 width = 10;       // +2
  s16 radix = 10;       // +4
  char* cursor = nullptr;  // +8
  std::vector<char> buffer = std::vector<char>(0x100, 0);  // +0xc
  StrBuf();                                   // title 0x80035f8c strbuf_create
  char* str() { return buffer.data(); }
  void parse_format(const char* spec);        // title 0x80035c68 strbuf_parse_format
  void format_int(char* out, s32 value);      // title 0x80035e58 strbuf_format_int
  // The inlined stream pattern: a '%'-spec sets the format, anything else is
  // appended (with the "over" check at 0x80?).
  void put(const char* text);
  void put_int(s32 value);                    // format_int + append
  void set(const char* text);                 // copy
};

// ---- Party / stat library: game/common/party.h (shared with the field) ----------------
// Message pack lookups (title 0x80034390-0x80034440).
void strings_set(const u8* pack);                 // title 0x80034390
u32 strings_word(int index);                      // title 0x800343b8: u32 at pack + index * 4
const u8* strings_block(int index);               // title 0x800343fc: pack + offset[index]
const u8* item_strings(int index);                // title 0x80034440: item data + offset[index]
// title 0x80038354: a name in the game's ASCII-ish form to glyph codes ('@' toggles the
// second table). Writes at most 'out_size' bytes including the terminator.
void text_to_card_codes(const char* name, u8* out, size_t out_size);

// Colour scaling used by every widget (title 0x80038430 / 0x8003853c): the 24-bit
// colour 'rgb' divided by 0x80 / brightness, ORed with a GP0 code.
u32 menu_colour_code(u32 code, u32 brightness, u32 rgb);   // title 0x80038430
u32 menu_colour(u32 brightness, u32 rgb);                   // title 0x8003853c

// The lowest set bit of a pad mask (title 0x8003093c / field 0x8003039c).
u32 menu_input_lowest_bit(u32 bits);

}  // namespace ss::game::menu
