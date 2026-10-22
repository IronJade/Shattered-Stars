#pragma once

#include "core/types.h"
#include "game/common/menu_widget.h"

namespace ss::game {
struct MemoryCard;
}

// Menu page 0x11: the memory card save / load screen (title 0x80063350-0x800673fc; the
// field runs the same module from overlay 0xbb6). The session's load_mode picks loading
// (title Continue) or saving (field save points). The page is a Panel with the two card
// slots at the bottom; confirming a slot opens the list of the card's fifteen positions,
// confirming a position runs the load / save flow: a yes / no check, the progress bar
// while the card driver reads or writes the file, then the result.
//
// Save file (one block, "buX0:BASCUS-94421S02-Snn"), see analysis/menu_catalog/menu.md:
//   0x000 PS1 header (title, icon), 0x200 summary (what the list shows), 0x280 the
//   unlock flags (config + 0x1a0), 0x380 config / party / records / inventory / field
//   snapshot, packed with a zero-run code; byte-sum checksums over the summary and the
//   whole file.
namespace ss::game::menu {

// Offsets in a save file and its summary (frame 4).
constexpr int kSaveSummary = 0x200;
constexpr int kSaveUnlocks = 0x280;
constexpr int kSavePacked = 0x380;
constexpr int kSaveImageSize = 0x1B88;  // unpacked: config, party, records, inventory, snapshot

// ---- Save menu (Panel, 0xc4 bytes, vtables 0x800679e4 / 0x800679d4) ------------------
class SaveMenu : public Panel {
 public:
  SaveMenu();                                   // title 0x80063350
  ~SaveMenu() override;                         // [1] title 0x8006739c
  void frame() override;                        // [9] title 0x80063574
  void on_child_done(int id, int state, int result) override;  // [10] title 0x800639d8
  void input() override;                        // [11] title 0x800636a8
  void on_confirm(int n) override;              // [13] title 0x80063820
  void draw_help() override;                    // [22] title 0x80063534

  bool slot_usable(int slot) const;             // title 0x80063788
  void rebuild_slots();                         // title 0x80063b70

  BgLoadClient loads;           // +0x98
  MemoryCard* card = nullptr;   // +0xa0
  TextBox* no_card = nullptr;   // +0xa4: "no memory card" notice
  TextBox* slot_box = nullptr;  // +0xa8: the slot chooser
  s32 status[2] = {};           // +0xac / +0xb0: slot status last shown
  s32 shown_load = 0;           // +0xb4
  s32 load_handle = 0;          // +0xb8: field only: the sprite pack (0xbd2) still loading
  s32 usable[2] = {};           // +0xbc / +0xc0
};

// ---- Position list (Dialog, 0xd0 bytes, vtable 0x8006792c) ----------------------------
class SaveList : public Dialog {
 public:
  SaveList();                                   // title 0x80063c78
  ~SaveList() override;                         // [1] title 0x80067348
  void frame() override;                        // [9] title 0x80063f00
  void on_child_done(int id, int state, int result) override;  // [10] title 0x80064194
  void input() override;                        // [11] title 0x8006401c
  void on_confirm(int n) override;              // [13] title 0x800640dc
  void show_entry(int index);                   // title 0x80064248

  MemoryCard* card = nullptr;   // +0x50
  BgLoadClient loads;           // +0x54
  s32 pending_load = 0;         // +0x5c (field: a load the flow waits for)
  TextBox* boxes[15] = {};      // +0x60
  s16 empty[15] = {};           // +0x9c: position holds no (valid) save
  s16 cursor_index = 0;         // +0xbc
  s16 scroll = 0;               // +0xbe: in quarter rows
  s32 free_blocks = 0;          // +0xc0
  s32 save_count = 0;           // +0xc4
  s32 total = 0;                // +0xc8
  s32 card_gone = 0;            // +0xcc
};

// ---- Format flow (Dialog, 0x60 bytes, vtable 0x80067884): save mode, unformatted card --
class FormatFlow : public Dialog {
 public:
  FormatFlow();                                 // title 0x80064904
  void frame() override;                        // [9] title 0x8006495c
  void on_child_done(int id, int state, int result) override;  // [10] title 0x80064b18
  s32 state = 0;      // +0x54
  s32 last = -1;      // +0x58
  s32 slot = 0;       // +0x5c (title 0x80064954 sets it)
};

// ---- Load / save flow (Dialog, 0x6c bytes, vtable 0x800677dc) --------------------------
class LoadFlow : public Dialog {
 public:
  LoadFlow();                                   // title 0x80064ba4
  ~LoadFlow() override;                         // [1] title 0x80064c00
  void frame() override;                        // [9] title 0x80064c64
  void on_child_done(int id, int state, int result) override;  // [10] title 0x80064f5c
  void start(int slot_, int index_) { slot = slot_, index = index_; }  // title 0x80064c58

  MemoryCard* card = nullptr;   // +0x50
  s32 state = 0;                // +0x54
  s32 last = -1;                // +0x58
  s32 slot = 0;                 // +0x5c
  s32 index = 0;                // +0x60
  s32 saved_last[2] = {};       // +0x64 / +0x68: config + 0x198 / + 0x19c
};

// ---- Yes / no check (Dialog, 0x5c bytes, vtable 0x80067734) ----------------------------
class CheckDialog : public Dialog {
 public:
  CheckDialog();                                // title 0x80064fe4
  void input() override;                        // [11] title 0x80065040
  void on_confirm(int n) override;              // [13] title 0x800653f4
  void setup();                                 // title 0x8006514c
  void setup_check();                           // title 0x800650d4: "Do you want to load / save?"
  void setup_overwrite();                       // title 0x8006510c: cursor on NO
  void setup_format();                          // title 0x8006529c

  MemoryCard* card = nullptr;   // +0x50
  TextBox* box = nullptr;       // +0x54: a TitledBox
  s32 card_gone = 0;            // +0x58
};

// ---- Message dialogs (Dialog with ConfirmDialog's handlers; vtables 0x8006768c,
// 0x80067ab4): confirm closes them, cancel is ignored. ------------------------------------
class NoticeDialog : public Dialog {
 public:
  void on_confirm(int n) override;              // [13] title 0x8006ac24
  void on_cancel(int) override { sound = 0; }   // [14] title 0x8006ac64
};
void notice_card_removed();                     // title 0x80065584
void notice_card_error();                       // title 0x800654ec ("format failed")
void notice_card_formatted();                   // title 0x80065538
void notice_transfer_failed();                  // title 0x80065478

// ---- Progress bar box (TextBox, 0x90 bytes, vtable 0x8006764c) -------------------------
class ProgressBox : public TextBox {
 public:
  ProgressBox();                                // title 0x800655f4
  void set_progress(s32 to, s32 of) { target = to, total = of; }  // title 0x80065640
  void draw_background() override;              // [6] title 0x8006564c
  s32 target = 0x38400;   // +0x84
  s32 total = 0x38400;    // +0x88
  s32 current = 0;        // +0x8c
};

// ---- Progress dialog (Dialog, 0x74 bytes, vtable 0x800675a4) ---------------------------
class ProgressDialog : public Dialog {
 public:
  ProgressDialog();                             // title 0x800658fc
  ~ProgressDialog() override;                   // [1] title 0x80065994
  void frame() override;                        // [9] title 0x80065b18
  void start(int slot_, int index_);            // title 0x80065a00

  ProgressBox* bar = nullptr;   // +0x50
  MemoryCard* card = nullptr;   // +0x54
  s32 done = 0;                 // +0x58
  std::vector<u8> buffer;       // +0x5c: the file (0x2000 bytes)
  s32 slot = 0;                 // +0x60
  s32 index = 0;                // +0x64
  s32 length = 0;               // +0x68: bytes moved (+0x7f), -1 = could not start
  s32 total = 0;                // +0x6c: sum of the steps
  const s16* steps = nullptr;   // +0x70: progress per driver state (-1 = the file length)
};

// ---- Format dialog (Dialog, 0x60 bytes, vtable 0x800674fc) -----------------------------
class FormatDialog : public Dialog {
 public:
  FormatDialog();                               // title 0x80065ca8
  ~FormatDialog() override;                     // [1] title 0x80065d38
  void frame() override;                        // [9] title 0x80065df8
  void start(int slot_);                        // title 0x80065d90
  TextBox* box = nullptr;       // +0x50
  MemoryCard* card = nullptr;   // +0x54
  s16 step = 0;                 // +0x58
  s16 delay = 0;                // +0x5a
  s16 slot = 0;                 // +0x5c
};

// ---- Card operations (title 0x80065fa4-0x80067244) -------------------------------------
bool save_summary_valid(MemoryCard* card, int slot, int index);  // title 0x80065fa4
// title 0x80065fec: fills 'file' with a save of the current game and starts the driver
// writing it at 'index'; returns the file length, or -1 when the card is busy.
int save_build(MemoryCard* card, u8* file, int slot, int index);
int save_finish(MemoryCard* card, int slot);          // title 0x800668bc: 0 busy, 1 ok, <0 failed
int load_start(MemoryCard* card, u8* file, int slot, int index);  // title 0x80066948
int load_finish(MemoryCard* card, int slot);          // title 0x800669b4: 0 busy, 1 ok, <0 failed
// title 0x80066a94: copies the game state into (load == 0, returns the packed size) or
// out of (load == 1) the packed image at 'packed'.
int save_serialize(int load, u8* packed);
bool save_summary_check(const u8* summary);           // title 0x80066c98
void card_request_format(MemoryCard* card, int slot); // title 0x80066da4
void party_summary(int slot, u8* out);                // title 0x80066e48
u32 byte_sum(const u8* data, int n);                  // title 0x80066ee0
int zero_pack(u8* out, const u8* in, int n);          // title 0x80066f18
void zero_unpack(u8* out, const u8* in);              // title 0x8006700c
char* sjis_digit(int digit, char* out);               // title 0x80067098
char* sjis_2digits(int value, char* out);             // title 0x800670d4
char* sjis_number(int value, char* out);              // title 0x8006714c
char* sjis_name(const u8* codes, char* out);          // title 0x80067244

// Loading a save without the menu (the launcher's --load-save): reads position 'index'
// (0-14) of card slot 'slot' and fills the handoff blocks as the load flow does. Runs
// the card driver until it is done. Returns 1 on success.
int load_save_direct(MemoryCard* card, int slot, int index);

}  // namespace ss::game::menu
