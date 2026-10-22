#pragma once

#include <vector>

#include "core/types.h"

// The field's menus: the launcher that loads a menu's overlays, the runners for each
// kind (the camp menu, shops, save points, ...) and the field_update hooks that open
// them (0x8004c334: the menu button / a script's menu request in state 0, state 3 while
// one runs). The menus themselves are the shared framework (game/common/menu_*) with
// the field as host; the camp menu's first page (overlay 0xba6) is ported here.
namespace ss::game::field {

struct Field;

// ---- Menu launcher (0x3c bytes; 0x80030470, global 0x800752d4) ----------------------
// Reads a menu kind's four consecutive archive entries: the resource pack (TIMs), the
// class library code (0xba5, loaded at 0x800d1b28), the first page's code (at
// 0x8007e000) and the page data.
struct MenuLauncher {
  s32 entry = 0;            // +0x1c: first entry (0xba4 camp, 0xba8, 0xbac, 0xbb0, 0xbb4)
  s32 sizes[4] = {};        // +0x20
  s32 state = 0;            // +0x30: 1 wait, 2 sizes, 3 read, 4 wait, 5 done
  s32 total = 0;            // +0x34
  std::vector<u8> buffer;   // +0x38
};
MenuLauncher* menu_launcher_create();                  // 0x80030470
void menu_launcher_destroy(MenuLauncher* l);           // 0x800304b4
void menu_launcher_start(MenuLauncher* l, int kind);   // 0x80030908: kind 1-5
void menu_launcher_step(MenuLauncher* l);              // 0x8003096c
void menu_launcher_finish(MenuLauncher* l);            // 0x80030a94: unpacks the resources and page data

// Menu runners (each loads, creates the session and the scene, runs it to the end).
int menu_run_camp(MenuLauncher* l);                    // 0x800304f0: 0 normal, 1 loaded, 2-6 see field_menu_update
int menu_run_shop(MenuLauncher* l, s16 shop);          // 0x8003060c: page 0xb, returns the selection
void menu_run_member(MenuLauncher* l, int slot);       // 0x800306a0: page 6 for one member
void menu_run_query(MenuLauncher* l, int slot);        // 0x80030730: page 0xf, variant 0xb, field query
void menu_run_save(MenuLauncher* l);                   // 0x800307d0: page 0x11 (save point)

// Party queries the scripts make through a temporary menu session (script table B).
int menu_member_specialty_level(int slot, int specialty);  // 0x80030ba8: -1 for an empty slot
int menu_super_specialty_6_level();                        // 0x80030c20
void menu_member_clear_bond(int slot);                     // 0x80030c64: record +0x3b / +0xce / +0xcf

// Script menu requests (g_menu_request 0x80076196 / g_menu_param 0x8007619a): 1 camp,
// 2 shop, 3 query, 4 member, 5 save. Nonzero opens the menu at the next field frame.
void field_menu_request(int kind);
void field_menu_set_param(int param);
int field_menu_request_pending();

// field_set_option_a / _b (0x80055df0 / 0x80055e00; u16 0x80074628 / 0x8007462a):
// what the menu may offer. Option a bit 0: saving (world map or a save point), bit 1:
// field_flags +0x25.
void field_set_option_a(u16 v);
void field_set_option_b(u16 v);
u16 field_get_option_a();                              // 0x80055dd0
u16 field_get_option_b();                              // 0x80055de0

// The clock (seconds, 0x80076310) at which the query gauge's five minutes started; -1
// until the gauge's first draw with script flag 19 on. Map loads and script A 0x35 set -1.
void field_set_gauge_clock(s32 v);
s32 field_gauge_clock();
// 0x80034a8c: seconds left of the query gauge's five minutes (300 until the clock starts).
s32 field_gauge_seconds_left();

// 0x8004c334 before the state switch (state 0): the save point check (sfx 0xc when the
// leader steps on one), then the menu button or a pending request opens the menu
// (fade out, preloads saved, the leader's position stored, the launcher started) and
// sets state 3.
void field_menu_check(Field* f);
// 0x8004c334 state 3: waits for the launcher and the fade, runs the menu, restores the
// field and acts on the result (a loaded game reloads the map; 2 game over, 3 a forced
// battle, 6 a teleport).
void field_menu_update(Field* f);

}  // namespace ss::game::field
