#pragma once

#include <vector>

#include "core/types.h"
#include "game/common/menu_core.h"
#include "game/common/screen.h"

// The item viewer the field's menu panels show in their top-right corner (the code of
// archive entry 3001 that the camp page loads to 0x80088000; a task of 0x64 bytes, vtable
// 0x8008cf50, type 0x103). For the item under the cursor it streams the item's file from
// entry 0x1189 (sector offset and length from block 2 of the item pack) into a cache of
// five, then unpacks its two blocks: a text pack (the description's font and messages,
// which the panel shows in its info box) and the 3D model it turns on screen.
namespace ss::game::menu {

class Widget;

class ItemViewer : public MenuTask {
 public:
  ItemViewer();                                   // 0x80088000
  ~ItemViewer() override;                         // [1] 0x80088178
  int type() override { return 0x103; }           // [2] 0x8008825c
  void on_close() override;                       // [4] 0x80088264
  void update() override;                         // [5] 0x80088694

  void set_table(const u8* table) { table_ = table; }  // 0x80088368: item pack block 2
  void place(s16 x, s16 y, s16 z);                // 0x80088294: screen position and depth
  void set_item(int item);                        // 0x80088370: starts loading its file
  bool ready();                                   // 0x800884a8: the current item's file is in
  // 0x800888ec: the text pack of the shown item (font, strings and a third block); false
  // while nothing is shown.
  bool info(const u8** a, const u8** strings, const u8** font) const;
  int item() const { return item_; }              // 0x8008895c

 private:
  struct Slot {                       // 0x10 bytes
    u16 flags = 1;                    // +0: 1 free, 2 used
    u16 item = 0;                     // +2
    s16 load = 0;                     // +4: background load handle (0 when done)
    s32 age = 0;                      // +8
    std::vector<u8> data;             // +0xc
  };
  Slot* find(u16 flags, int item);    // 0x800885c4
  Slot* oldest();                     // 0x8008862c
  void release(Slot* s);              // 0x8008855c
  void unpack(const Slot& s);         // 0x80088818: text pack and model
  void show();                        // the model object (0x80089628) and its turn (0x80088968)
  void spin();                        // 0x800889ac
  void upload_texture();              // 0x800898cc: (0x200, 0x100), CLUT row 0x1e0
  void draw();                        // 0x80088ba8: the model's triangles into its own OT

  Widget* owner_ = nullptr;           // +0x20
  BgLoadClient client_;               // +0x08
  const u8* table_ = nullptr;         // +0x10
  std::vector<u8> text_;              // +0x14
  std::vector<u8> model_;             // +0x18
  Slot slots_[5];                     // +0x1c
  bool shown_ = false;                // +0x2c: the model object
  s16 item_ = 0;                      // +0x30
  s32 counter_ = 0;                   // +0x34
  s16 angle_[3] = {};                 // +0x38
  s32 pos_[4] = {};                   // +0x40: translation (z from place)
  s16 phase_ = 0;                     // +0x50
  s16 speed_[3] = {};                 // +0x52
  s16 ot_far_ = 0, ot_near_ = 0;      // +0x58 / +0x5a: OTZ range of its ordering table
  RenderContext ctx_;                 // +0x24: the owner's arena, its own table
  bool has_ot_ = false;               // +0x32
  u16 clut_ = 0, tpage_ = 0;          // model object +8 / +0xa
};

}  // namespace ss::game::menu
