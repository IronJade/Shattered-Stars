#pragma once

#include "core/types.h"
#include "game/field/tasks.h"

// The field's screen fades (ScreenFade, 0x98 bytes, vtable 0x80072f7c, task
// priority 1, global 0x80075298; field 0x80046910-0x80048154): a ring of eight
// commands run one after the other, drawn at OT slot 0x820, over the scene and the
// message windows (0x810+).
//
//   type 0          nothing (a pause of 'frames')
//   1 / 2           fade in from black (subtractive) / white (additive): level 255 -> 0
//   3 / 4           fade out to black / white: level 0 -> 255; the screen then holds
//                   (black with the display off / white) until the next command
//   5 .. 0x24       wipes in 8 directions (table 0x8007448c); odd types black,
//                   even white; bit 0 of (type - 5) / 2 reverses the progress
//   0x25 / 0x26     iris closing around (p4, p5); 0x27 / 0x28 opening
//   0x29            cross-fade from the screen captured by fade_capture_screen
//
// Script ext 0x7c / 0xb5 push fades; the VM's wait state 3 polls fade_is_active.
namespace ss::game::field {

struct FadeCommand {  // 0xc bytes
  s16 type = 0;
  s16 frames = 0;
  s16 elapsed = 0;
  u8 active = 0;
  s16 p4 = 0, p5 = 0;
};

struct ScreenFade : Task {
  FadeCommand cmd[8];          // +0x08
  u8 write = 0;                // +0x68
  u8 read = 0;                 // +0x69
  u8 hold = 2;                 // +0x6a: after a fade out: 0 none, 1 white, 2 black + display off
  u16* tiles[10]{};            // +0x6c: the captured screen, 10 tiles of 64x112 (STP set)
  u32 phase = 0;               // +0x94: 1 just captured, 2 drawing the capture
  ~ScreenFade() override;      // fade_destruct
  void update() override;      // fade_update
};

// What the fades tint besides the screen: the BG renderer (overlay object
// 0x8007528c, vtable +0x28 tint on/off and +0x30 tint colour), the actors
// (ActorSystem +0xc..+0xf) and the per-map BG tint / clear colour. Filled in by
// those ports; null functions are skipped.
struct FadeTargets {
  void (*bg_set_tint_enabled)(int on) = nullptr;
  void (*bg_set_tint)(int r, int g, int b) = nullptr;
  void (*actors_set_brightness)(int level) = nullptr;
  u8 bg_tint_active = 0;       // 0x80075356
  s16 bg_tint[3]{};            // 0x80075aba..0x80075abe
  u8 scene_loaded = 0;         // 0x80075354
  const u8* scene = nullptr;   // g_scene 0x80075330: clear colour at +0x6f..+0x71
};
extern FadeTargets g_fade_targets;

ScreenFade* field_fade();                                               // 0x80075298
ScreenFade* fade_construct(ScreenFade* f);                              // 0x80046910
void fade_destruct(ScreenFade* f);                                      // 0x80046978
void fade_set_scene_brightness(ScreenFade* f, int level);               // 0x80046a08
void fade_update(ScreenFade* f);                                        // 0x80046ae4
bool fade_push(ScreenFade* f, s16 type, s16 frames, s16 p4, s16 p5);    // 0x800471f4: false when full
bool fade_is_active(ScreenFade* f, int type);                           // 0x80047260: -1 = anything queued
void fade_draw_grey(ScreenFade* f, int level, int semi, int abr, int ot_slot);  // 0x800472e0
void fade_draw_tile(ScreenFade* f, u8 r, u8 g, u8 b, int semi, int abr, int ot_slot);  // 0x80047314
void fade_draw_wipe(ScreenFade* f, int progress, int direction, int abr);       // 0x80047454
void fade_capture_screen(ScreenFade* f);                                // 0x80047ad8
void fade_draw_capture(ScreenFade* f, u8 brightness, int semi, int abr);  // 0x80047c54
void fade_draw_iris(ScreenFade* f, u32 centre, int segments, int radius, u32 colour, int abr);  // 0x80047ef0

}  // namespace ss::game::field
