#pragma once

#include "core/types.h"
#include "game/field/tasks.h"

// The one-line notice banner (0x3c bytes, vtable 0x80073194, task priority 0,
// global 0x800752c0; field 0x800574e0-0x80058354): a message window without frame
// that slides in from the right (or appears at the top left) with a skinned
// backing, an optional icon and glyphs that fade in; script ext 0x84 / 0xb9 (the
// map name when a map is entered). Opening any of windows 0-4 dismisses it.
namespace ss::game::field {

struct Notice : Task {
  s32 timer = 0;           // +0x08: slide timer
  s32 shown_frames = 0;    // +0x0c: frames since the window opened
  s32 effect_timer = 0;    // +0x10: frames of the 'timed' effect
  s32 state = 0;           // +0x14: 0 idle, 1 slide in, 2 open window, 3 shown, 4 slide out
  s32 window = 0;          // +0x18
  s32 x = 0, y = 0;        // +0x1c / +0x20
  s32 icon = 0;            // +0x24: 0 none, else #3110 icon 0x13 + icon
  s32 text_width = 0;      // +0x28
  const u8* text = nullptr;  // +0x2c
  s32 effect = 0;          // +0x30: 0 none, 1 sweep running (started by the timed wait), 2 done
  s32 at_top = 0;          // +0x34
  u8 close_request = 0;    // +0x38
  ~Notice() override;      // notice_destroy
  void update() override;  // notice_update
};

Notice* g_notice_object();                                   // 0x800752c0
Notice* notice_create(Notice* self);                         // 0x800574e0
void notice_destroy(Notice* self);                           // 0x80057528
void notice_show(Notice* n, const u8* text, int icon, int window, int at_top);  // 0x8005757c
void notice_dismiss(Notice* n);                              // 0x80057734
void notice_update(Notice* n);                               // 0x80057790
void notice_draw(Notice* n);                                 // 0x80057b28
void notice_reset(Notice* n);                                // 0x80058354

}  // namespace ss::game::field
