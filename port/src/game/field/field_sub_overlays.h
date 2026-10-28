#pragma once

#include <vector>

#include "core/types.h"

// Embedded map code at 0x801f9800 (map container sections 7-10 and 12; field_load_map
// unpacks the section into a 0x4800-byte area, 0x800756c0). Each overlay serves the
// scripts of a few maps through fixed addresses the field calls whatever is loaded:
//
//   section 7  #3109  map 688            timer / counter panels (A 0x33 / 0x35 / 0x37, the
//                                        main loop's 0x801f999c update of object 0x800752e8)
//   section 8  #3108  maps 384, 598-602  walking along a scripted path (ext 0x9c / 0x9e;
//                                        player control calls 0x801f9800 while 0x800761ea)
//   section 9  #3106  map 139            mirror images of actors (ext 0xbf; fieldactor_draw
//                                        calls 0x801f9800 while 0x800761e8)
//   section 10 #3104  map 143            a rainbow (special command 0 creates the task)
//   section 12 #3105  map 581            (A 0x40)
//
// The field calls these only on maps that carry the section; the port checks that
// (field_map_sub_sections) and otherwise notes the call.
namespace ss::game::field {

struct Actor;
struct ScriptCtx;

// ---- #3108: scripted paths ------------------------------------------------------------
// Up to four points (0x800761ec x / 0x800761f4 y / 0x800761fc z, s16 map units), the
// running length to each (0x80076204), the position along the path (0x80076218, 20.12).
// 0x800761ea (fieldactor_globals().vehicle_control) holds the segment count while the
// leader is on a path; player control then hands the leader to path_control.
void path_start(ScriptCtx* ctx);              // 0x801f9fe8 (ext 0x9c, 10 args)
void path_add_point(ScriptCtx* ctx);          // 0x801fa120 (ext 0x9e, 3 args)
void path_control(Actor* a, s32 ax, s32 ay);  // 0x801f9800: player control on the path
void path_set_end_scripts(int start, int end);  // table A 0x2a: 0x80076220 / 0x80076222 (0xffff none)
void path_set_end_stops(int start, int end);    // table A 0x2b: 0x8007621d / 0x8007621e
int path_count();                               // 0x800761ea
int path_anim_flag();                           // 0x80076214 (s16)

// ---- #3106: mirror lines -------------------------------------------------------------
// Two lines (0x800761d4, 10 bytes each: used, OT layer, x0, z0, x1, z1). Every field
// actor standing in front of one gets an image (its child 0 / 1) reflected across it.
void mirror_add(ScriptCtx* ctx);  // 0x801f9bb4 (ext 0xbf, 5 args); sets 0x800761e8
void mirror_update(Actor* a);     // 0x801f9800: fieldactor_draw calls it while 0x800761e8

// ---- #3104: the rainbow (map 143) ----------------------------------------------------
// Special command 0: new 0xd4-byte task (priority 1) at 0x800752cc whose update fans 24
// rays (a quarter circle) x 8 colour bands of gouraud quads, additive, over the scene as its timer runs.
void rainbow_create();   // 0x801f9800 (ctor); special command 0
void rainbow_destroy();  // field_load_map / field_destroy_subsystems: task_unregister + delete

// ---- #3109: the TIME / PRESS gauge (map 688) ----------------------------------------
// Table A 0x35 creates the 0xb0-byte object 0x800752e8 at (x, y): four TextPanels, and
// each frame (the main loop, after task_run_all) "TIME" with five minutes counting down
// from the gauge clock (0x80076310, started once script flag 19 is on) and "PRESS" with
// script variable +0x38 as a percentage, each with an 80-pixel bar. The camp menu's
// query page draws the same gauge (menu_camp.cpp). Section 11 (entry #2) on the same
// map is the item records the inventory then points at (+0x820), read by A 0x33.
bool gauge_exists();                 // 0x800752e8 != 0
void gauge_create(int x, int y);     // 0x801f9800 + A 0x35's +4 / +8
void gauge_destroy();                // 0x801f98c0(obj, 3)
void gauge_update();                 // 0x801f999c (main loop)
int gauge_seconds_left();            // 0x801f9f54 (A 0x37)
int item_record_byte_19(int item);   // 0x801f9fb8 (A 0x33): inventory_item_data(inv, item, 0x19)
void sub_set_item_records(std::vector<u8> data);  // section 11 unpacked (0x800756cc, inventory +0x820)

// ---- #3105: the final screen (map 581) -----------------------------------------------
// Table A 0x40 (n = 0..2): the closing caption typed in, then the logo and "fin." at
// 640x480, interlaced, with the ending music; it never returns (the game is over).
[[noreturn]] void the_end(int n);  // 0x801f9a64 (loads with 0x801f9800)

// ---- Section 6: #3107, the credits (map 581) ---------------------------------------
// Table A 0x2d: tears the field down (map, layers, BG, actors, windows, subsystems;
// the stash is not kept), then runs the program in section 6, the credits
// (game/credits), which starts the field again at its end. Never returns on a map
// with section 6; false (nothing done) elsewhere, where the console would run whatever
// 0x800756f4 points at.
bool field_run_section6_program();  // vm_exec_table_a case 0x2d (field_section6.cpp)

// Clears what field_load_map clears (0x800761d4 mirror slots, 0x800761e8, 0x800761ea).
void sub_overlays_map_reset();

}  // namespace ss::game::field
