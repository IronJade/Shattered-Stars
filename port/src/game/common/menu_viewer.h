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

 protected:
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
  static void upload_model_texture(const std::vector<u8>& model, int x, int y, int clut_x, int clut_y, u16* clut,
                                   u16* tpage);
  void draw();                        // 0x80088ba8 on the shown item's model
  // 0x80088ba8: a model's triangles into the viewer's own OT (spliced into its owner's).
  void draw_model(const std::vector<u8>& model, u16 clut, u16 tpage);

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

// The item creation pages' viewer (entry 3001 0x8008a364, 0xa8 bytes, vtable 0x8008cf98).
// Between the creation steps it is an ItemViewer. While a creation plays it loads the four
// items it is given (the materials, the other result and the result), then morphs them for
// 300 frames (model_blend 0x8008b338): material 1 into the result and back, the other
// result into the result and back.
class CreationViewer final : public ItemViewer {
 public:
  CreationViewer();                   // 0x8008a364
  ~CreationViewer() override;         // [1] 0x8008ad00
  void update() override;             // [5] 0x8008a444
  void set_mode(int mode);            // 0x8008a3e8: +0x94 = mode, state 1 (loading)
  void set_items(int a, int b, int c, int d);   // 0x8008a3f8 (0 items: none)
  int state() const { return state_; }  // 0x8008a73c: 0 idle, 1 loading, 2 loaded, 3 morphing, 4 done
  void build_morphs();                // 0x8008a748: the four morph pairs; state 3
  void finish();                      // 0x8008a9b4: models freed; state 0

 private:
  struct Model {                      // a model object (0x80089628, 0x58 bytes)
    std::vector<u8> data;             // the unpacked model file (header +0xc in the original)
    u16 clut = 0, tpage = 0;          // +8 / +0xa
  };
  struct Pair {                       // 0x14 bytes (0x8008ad28)
    int a = 0, b = 0;                 // +0 / +4: the models (copies of their headers)
    Model blended;                    // +8: a copy of a whose vertices and normals move
    std::vector<u16> table;           // +0xc: per vertex of a: its index, b's nearest, distance
    bool a_empty = true;              // +0x10
    bool b_empty = true;              // +0x12
  };
  void release_unused();              // 0x8008a67c
  void prepare(Pair& p);              // 0x8008aecc: each vertex of a finds b's nearest
  const Pair& blend(int t);           // 0x8008aa50: the pair and weight for this frame
  void model_blend(Pair& p, int t);   // 0x8008b338
  void debug_dump(const Pair& p);     // SS_CREATION_DUMP

  s16 mode_ = 0;                      // +0x94
  s16 items_[4] = {};                 // +0x96..+0x9c: pending loads (-1 no item, 0 loaded)
  s16 last_ = 0;                      // +0x9e: the result
  Model models_[4];                   // +0x64..+0x70
  Pair pairs_[4];                     // +0x74..+0x80
  s16 state_ = 0;                     // +0xa2
  s16 frame_ = 0;                     // +0xa4
};

}  // namespace ss::game::menu
