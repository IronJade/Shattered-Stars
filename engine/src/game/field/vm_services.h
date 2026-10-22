#pragma once

#include "core/types.h"

// Everything the event-script VM does to the rest of the field program goes through
// these functions: one per distinct external call or external object/global the VM
// touches, named after what it does, with plain parameters mirroring the original
// call. The comment names the original (field addresses unless "boot"). Actors are
// passed as their slot in field_actors[194] (0x80075360), message windows as their
// index in message_windows[20] (0x80075668); the singletons the original passes
// (ActorSystem 0x80075294, MsgSystem 0x80075288, ScreenFade 0x80075298, Field
// 0x80075284, Sound boot System + 0x5aa4, BG renderer 0x8007528c, camera script
// 0x800752b4) are implied.
//
// vm_services.cpp holds stand-ins until the subsystems are ported: each logs once and
// returns a value that lets scripts run on ("window closed", "fade done", "actor idle",
// "entry loaded"), and keeps the little state needed for that (actors created,
// positions written). Replace each body with the real call as its owner is ported.
namespace ss::game::field {

struct ScriptCtx;

// Tests can watch every service call: 'service' is the function name without the
// vm_svc_ prefix, 'args' its arguments as text.
using VmTraceSink = void (*)(const char* service, const char* args);
void vm_svc_set_trace(VmTraceSink sink);
void vm_svc_reset();  // clears the stand-ins' state (tests)

// ---- Field program ------------------------------------------------------------------
int vm_svc_field_update_state();                         // Field +0x14 (field_update state; talk only in 0)
void vm_svc_field_load_map(int map, int keep_position, int flags);  // field_load_map 0x80053e8c (runs vm_init)
void vm_svc_field_load_entry(int entry, u8* done, int a3);          // field_load_entry 0x8004a654 (*done = 1 when read)
int vm_svc_field_require_sprite_pack(int entry);         // field_require_sprite_pack 0x8004a830: 1 = loaded
// vm_restore_state: field_load_entry + spin on 0x8004a830 pumping g_task
void vm_svc_reload_sprite_pack(int entry);
int vm_svc_sprite_slot_entry(int slot);                  // sprite slot table 0x8007585c: archive id when loaded, else 0
void vm_svc_field_prompt_disc_change(int disc, int a2);  // field_prompt_disc_change 0x80052ce8
void vm_svc_field_free_ot_layers();                      // field_free_ot_layers 0x80053158
// table A 0x2d: tears the field down, runs the program in 0x800756f4
void vm_svc_exit_to_program_teardown();
int vm_svc_field_install_music(int entry);               // field_install_music 0x8004b2b4: 1 = installed
void vm_svc_music_free_buffers();                        // mem_free of 0x80075740 / 0x80075738 after a music change
void vm_svc_map_resolve_entries(s16 files[17], int map, int x, int z);  // map_resolve_entries 0x80061888
void vm_svc_screen_set_clear_colour(int r, int g, int b);  // screen_set_clear_colour 0x80048538 (0x80075290)
// g_scene 0x80075330 (MapScene, section 0 block 0); may be null
const u8* vm_svc_map_scene();
int vm_svc_scene_scale();                                // g_scene_scale 0x80075766 (scene +0x68)
// zone_cell_at 0x80085cc4 (#3101): cell of a screen point, -1 outside
int vm_svc_zone_cell_at(int actor, int sx, int sy);
int vm_svc_zone_test_point(int actor, const u8* item, int sx, int sy, int y);  // zone_test_point 0x80083944 (#3101)
const u8* vm_svc_ui_atlas();                             // g_ui_atlas 0x80075328 (UV table)
int vm_svc_frame_counter();                              // g_frame_counter 0x80075324
int vm_svc_pad_button_icon_offset(int buttons);          // pad_button_icon_offset 0x80052f24
void vm_svc_set_scene_loaded(int v);                     // 0x80075354
void vm_svc_set_field_active(int v);                     // 0x80075355
void vm_svc_set_encounter_7535c(int v);                  // 0x8007535c (s16; 100 at map load)
void vm_svc_pad_vibrate(int port, int small, int large); // boot pad_vibrate 0x80013e14

// ---- Actors (field_actors[slot]) ----------------------------------------------------
bool vm_svc_actor_exists(int slot);                                   // field_actors[slot] != 0
void vm_svc_actor_get_position(int slot, s32* x, s32* y, s32* z);     // Actor +0x00 / +0x04 / +0x08 (20.12)
// Actor +0x00.. (A 0x3a; +0x0c gets stack garbage on the console)
void vm_svc_actor_set_position(int slot, s32 x, s32 y, s32 z);
int vm_svc_actor_get_anim(int slot);                                  // Actor +0x10 (s16)
void vm_svc_actor_set_16(int slot, int v);                            // Actor +0x16 = v, +0x2ce = -1 (B 0x27)
int vm_svc_actor_get_angle(int slot);                                 // Actor +0x20 (s16, 4096 = 360 degrees)
int vm_svc_actor_get_layer(int slot);                                 // Actor +0x22 (u16)
void vm_svc_actor_set_layer(int slot, int v);                         // Actor +0x22
s32 vm_svc_actor_get_offset48(int slot);                              // Actor +0x48 (s32)
void vm_svc_actor_set_offset48(int slot, s32 v);                      // Actor +0x48
// Actor +0x4c (0x400000 moved, 0x20000 not talkable)
u32 vm_svc_actor_get_flags(int slot);
int vm_svc_actor_get_talk_entry(int slot);                            // Actor +0x52 (s16 word index, -1 none)
int vm_svc_actor_get_pickpocket_entry(int slot);                      // Actor +0x90 (s16)
void vm_svc_actor_set_speed(int slot, s32 a, s32 b);                  // Actor +0x64 = a, +0x68 = b
int vm_svc_actor_get_view_x(int slot);                                // Actor +0x74 (s16, logical / screen position)
int vm_svc_actor_get_view_y(int slot);                                // Actor +0x76
void vm_svc_actor_set_view_pos(int slot, int x, int y);               // Actor +0x74 / +0x76 (B 0x3e)
void vm_svc_actor_set_7f(int slot, int v);                            // Actor +0x7f (u8)
void vm_svc_actor_set_8c(int slot, int v);                            // Actor +0x8c (s16)
void vm_svc_actor_set_9c(int slot, int a, int b);                     // Actor +0x9c / +0x9e (s16)
void vm_svc_actor_set_a0(int slot, int v);                            // Actor +0xa0 (s16)
int vm_svc_actor_get_screen_y(int slot);                              // Actor +0x2ba (s16; < 0x60 = high on screen)
s32 vm_svc_actor_get_talk_radius(int slot);                           // Actor +0x2d8
bool vm_svc_actor_has_attachment(int slot);                           // Actor +0x3d0 != 0
int vm_svc_actor_get_3e1(int slot);                                   // Actor +0x3e1 (u8)
void vm_svc_actor_create(int slot, int model, s32 x, s32 y, s32 z, int angle, int param, int talk_entry,
                         int pickpocket_entry, u32 flags);            // actor_create 0x80043890
void vm_svc_actor_create_or_place(int a0, int a1, int a2, int a3, int a4);  // actor_create_or_place 0x800439d4
void vm_svc_actor_delete_slot(int slot);                              // actor_delete_slot 0x8004541c (-1 = all)
void vm_svc_actor_queue_delete(int slot);                             // actor_queue_delete 0x80040c6c
void vm_svc_actor_cmd_wait(int slot, int frames);                     // actor_cmd_wait 0x8003fce0
void vm_svc_actor_cmd_wait_move(int slot);                            // actor_cmd_wait_move 0x8003fd68
void vm_svc_actor_cmd_wait_move_b(int slot);                          // actor_cmd_wait_move_b 0x8003fdc4
void vm_svc_actor_cmd_wait_turn(int slot);                            // actor_cmd_wait_turn 0x8003fe20
void vm_svc_actor_cmd_turn_to(int slot, int a, int b, int c);         // actor_cmd_turn_to 0x8003fe7c
void vm_svc_actor_cmd_turn_by(int slot, int a, int b, int c);         // actor_cmd_turn_by 0x8003ff5c
void vm_svc_actor_cmd_turn_to_actor(int slot, int a, int b, int c);   // actor_cmd_turn_to_actor 0x8004003c
void vm_svc_actor_cmd_turn_to_point(int slot, int a, int b, int c, int d);  // actor_cmd_turn_to_point 0x8004011c
void vm_svc_actor_cmd_move_to_xz(int slot, int a, int b, int c);      // actor_cmd_move_to_xz 0x80040228
void vm_svc_actor_cmd_move_to(int slot, int a, int b, int c, int d);  // actor_cmd_move_to 0x80040308
void vm_svc_actor_cmd_move_to_far(int slot, int x, int y, int z, int speed);  // actor_cmd_move_to_far 0x80040414
void vm_svc_actor_cmd_move_by_xz(int slot, int a, int b, int c);      // actor_cmd_move_by_xz 0x80040580
void vm_svc_actor_cmd_move_by(int slot, int a, int b, int c, int d);  // actor_cmd_move_by 0x80040660
void vm_svc_actor_cmd_move_dir(int slot, int a, int b, int c);        // actor_cmd_move_dir 0x8004076c
void vm_svc_actor_cmd_move_dir_rel(int slot, int a, int b, int c);    // actor_cmd_move_dir_rel 0x8004084c
void vm_svc_actor_cmd_set_anim(int slot, int anim);                   // actor_cmd_set_anim 0x8004092c
void vm_svc_actor_cmd_wait_anim(int slot, int a, int b);              // actor_cmd_wait_anim 0x800409b4
void vm_svc_actor_cmd_signal(int slot, int v);                        // actor_cmd_signal 0x80040a68
void vm_svc_actor_cmd_emote(int slot, int a, int b, int c);           // actor_cmd_emote 0x80040af0
void vm_svc_actor_cmd_clear_emote(int slot);                          // actor_cmd_clear_emote 0x80040bd0
void vm_svc_actor_delete_attachment(int slot);                        // actor_delete_attachment 0x80040c2c
void vm_svc_actor_queue_move_to(int slot, int a, int b, int c, int d, int e);   // actor_queue_move_to 0x80040cc8
void vm_svc_actor_queue_move_by(int slot, int a, int b, int c, int d, int e);   // actor_queue_move_by 0x80040e00
// actor_queue_fade_colour 0x80040f38
void vm_svc_actor_queue_fade_colour(int slot, int r, int g, int b, int frames, int mode);
void vm_svc_actor_queue_wait_fade(int slot);                          // actor_queue_wait_fade 0x80041070
int vm_svc_actor_is_busy(int slot);                                   // actor_is_busy 0x80041450 (1 = busy)
void vm_svc_actor_push_state(int slot);                               // actor_push_state 0x80041918
void vm_svc_actor_push_state_and_turn(int slot, int angle, int mode); // actor_push_state_and_turn 0x80041998
void vm_svc_actor_pop_state(int slot, int mode);                      // actor_pop_state 0x80041aa4
void vm_svc_actor_stop(int slot);                                     // actor_stop 0x80042ca0
void vm_svc_actor_set_anim(int slot, int anim);                       // actor_set_anim 0x80042cc4
void vm_svc_actor_mark_dirty(int slot);                               // actor_mark_dirty 0x80042ce4
void vm_svc_actor_set_flags(int slot, int word, u32 v);               // actor_set_flags 0x80042cf8
void vm_svc_actor_or_flags(int slot, int word, u32 bits);             // actor_or_flags 0x80042d14
void vm_svc_actor_and_flags(int slot, int word, u32 mask);            // actor_and_flags 0x80042d3c
void vm_svc_actor_set_special(int slot, int a, int b);                // actor_set_patrol_emote 0x80042ffc
void vm_svc_actor_sys_mark_all_dirty();                               // actor_sys_mark_all_dirty 0x800437e4
void vm_svc_actor_sys_reset_sprite_cache();                           // actor_sys_reset_sprite_cache 0x80043840
// A 0x1b: new Afterimage (0x88) afterimage_create 0x8005ee9c(actor, a1, a2, a3, a4 & 0xff,
// a5 & 0xff) into the first free entry of g_afterimages 0x800752ec[12] (only if the actor exists).
void vm_svc_afterimage_attach(int slot, int a1, int a2, int a3, int a4, int a5);
// A 0x1c: afterimage_destroy 0x8005f018(obj, 3) of every list entry whose source is the actor.
void vm_svc_afterimage_detach(int slot);

// ---- Message windows, notice banner ------------------------------------------------
// msg_window_open 0x80058838
void vm_svc_msg_window_open(int x, int y, int w, int h, const u8* text, int window, int a7);
void vm_svc_msg_window_open_default(const u8* text, int window, int a3);  // msg_window_open_default 0x800588cc
void vm_svc_msg_window_close(int window);                 // msg_window_close 0x80058964
int vm_svc_msg_window_status(int window);                 // msg_window_status 0x80058b00 (1 / -1 = done or closed)
bool vm_svc_msg_window_exists(int window);                // message_windows[window] != 0
int vm_svc_msg_window_get_state(int window);              // msg_window_get_state 0x8005e7c8 (-1: none)
void vm_svc_msg_window_push_move(int window, int x, int y, int frames);  // msg_window_push_move 0x8005e5d0
// msg_window_line_pos 0x8005e6c8
void vm_svc_msg_window_line_pos(int window, int line, s32 pos[2], s32* width, s32* height);
void vm_svc_msg_window_set_anchor(int window, int on);    // msg_window_set_anchor 0x8005e78c
int vm_svc_msg_window_line_height(int window);            // MsgWindow +0x154
void vm_svc_msg_window_set_24(int window, int v);         // MsgWindow +0x24 (u8)
void vm_svc_msg_window_set_frame(int window, int frame, int body);  // MsgWindow +0x130 / +0x131
void vm_svc_msg_window_set_hold(int window, int v);       // MsgWindow +0x139
void vm_svc_msg_window_set_cancel(int window, int v);     // MsgWindow +0x13a (choice: Circle allowed)
void vm_svc_msg_window_set_13b(int window, int v);        // MsgWindow +0x13b
void vm_svc_msg_window_set_close_flag(int window, int v); // MsgWindow +0x13d
void vm_svc_msg_window_request_close(int window);         // MsgWindow +0x13f = 1
void vm_svc_msg_window_set_advance(int window, int v);    // MsgWindow +0x140 (button advance)
void vm_svc_msg_window_set_wrap(int window, int v);       // MsgWindow +0x14b
void vm_svc_msg_set_default_rect(int x, int y, int w, int h);  // MsgSystem +0x1f8 .. +0x204
void vm_svc_msg_set_default_x(int v);                     // MsgSystem +0x1f8
void vm_svc_msg_set_default_y(int v);                     // MsgSystem +0x1fc
void vm_svc_msg_set_default_size(int w, int h);           // MsgSystem +0x200 / +0x204
void vm_svc_notice_show(const u8* text, int hold, int window, int top);  // notice_show 0x8005757c (g_notice 0x800752c0)
void vm_svc_notice_dismiss();                             // notice_dismiss 0x80057734

// ---- Screen fade --------------------------------------------------------------------
void vm_svc_fade_push(int a, int b, int c, int d);        // fade_push 0x800471f4
int vm_svc_fade_is_active(int param);                     // fade_is_active 0x80047260
void vm_svc_fade_capture_screen();                        // fade_capture_screen 0x80047ad8

// ---- Camera script (0x800752b4) and BG renderer (0x8007528c), overlay #3101 ----------
// camscript_push_bg 0x8008e228 (ext 0x85-0x89)
void vm_svc_camscript_push_bg(int a0, int a1, int a2, int a3, int a4, int a5, int a6);
// camscript_push_point 0x8008e1d0 (ext 0x8a-0x8e)
void vm_svc_camscript_push_point(int a0, int a1, int a2, int a3, int a4, int a5, int a6, int a7);
// camscript_push_actor 0x8008e270 (ext 0x8f-0x93)
void vm_svc_camscript_push_actor(int a0, int a1, int a2, int a3, int a4, int a5);
bool vm_svc_camscript_done();                            // CamScript byte +9 == +8 (wait state 4)
void vm_svc_bg_get_view_origin(s16* x, s16* y);          // bg_get_view_origin 0x80086bbc(bg, out)
void vm_svc_bg_set_target(int x, int y);                  // bg +8 / +0xa, then bg_update_scroll 0x800867d8
void vm_svc_bg_set_114(int a, int b);                     // bg +0x114 / +0x116
void vm_svc_bg_set_121(int v);                            // bg +0x121 (u8)
void vm_svc_bg_set_122(int v);                            // bg +0x122 (u8)
void vm_svc_bg_set_124(int v);                            // bg +0x124 (s16)
void vm_svc_bg_set_126(int v);                            // bg +0x126 (s16)
void vm_svc_bg_restore_tint();                            // bg vtable +0x2c (bg, 0)
int vm_svc_bg_anim_get_frame(int id);                     // bg_anim_get_frame 0x80086bf8
void vm_svc_bg_anim_set_frame(int id, int frame);         // bg_anim_set_frame 0x80086c4c
void vm_svc_bg_anim_play(int id, int b);                  // bg_anim_play 0x80086d20
int vm_svc_bg_anim_get_state(int id);                     // bg_anim_get_state 0x80086e70
// walk_test_obstacles 0x800863fc(actor, &pos, out): 0 = free
int vm_svc_walk_test_obstacles(int slot, s32 x, s32 y, s32 z);
void vm_svc_set_bg_tint(int r, int g, int b);             // 0x80075aba / abc / abe
void vm_svc_set_bg_tint_active(int v);                    // 0x80075356

// ---- Sound and music (boot sound driver at System + 0x5aa4) -------------------------
int vm_svc_sound_bank_busy();                             // boot sound_bank_busy 0x80018f04
int vm_svc_sound_play_sfx(int id, int pan, int volume);   // boot sound_play_sfx 0x80019b1c
int vm_svc_sound_effect_playing(int slot);                // boot 0x8001a0ac: effect slot's id while it plays, else 0
void vm_svc_sound_release_clip(int slot);                 // boot sound_release_clip 0x8001a114
void vm_svc_sound_start_clip(int slot, int volume, int pan, int pitch, int flag);  // boot sound_start_clip 0x8001a4b8
void vm_svc_sound_play_sequence();                        // boot sound_play_sequence 0x8001a5d8(sound, 0x80075734)
void vm_svc_sound_stop();                                 // boot sound_stop 0x8001a650
void vm_svc_sound_play_sequence_fade(int a, int b);       // boot 0x8001a65c(sound, 0x80075734, a, b)
void vm_svc_sound_fade_out(int frames);                   // boot sound_fade_out 0x8001a7e4
void vm_svc_sound_fade_in(int frames);                    // boot 0x8001a82c
void vm_svc_sound_fade_out_effects(int frames);               // boot sound_fade_out_effects 0x8001a88c
void vm_svc_sound_set_tempo(int v);                       // boot 0x8001a938
void vm_svc_sound_stop_voices(int mask);                  // boot 0x8001aac4
int vm_svc_sound_state_busy();                            // boot sound_state 0x8001ab30 +0x20 & 1
int vm_svc_callways_song_active();                        // callways_song_active 0x80031718 (g_stream 0x800752e0)
void vm_svc_callways_stop_song(int a);                    // callways_stop_song 0x80031724
// table B 0x5a (handoff 0xf, sound_holder_create 0x8006f90c)
void vm_svc_sound_bank_holder_alloc();
void vm_svc_sound_bank_holder_free();                     // table B 0x5b

// ---- Battle start, voices, preloads, loader ----------------------------------------
void vm_svc_voice_slot_invalidate(int slot);              // voice_slot_invalidate 0x8007972c
void vm_svc_load_queue_set_streaming(int on);             // load_queue_set_streaming 0x8004358c (g_task 0x800752b0)
int vm_svc_get_voice_cache_state();                       // 0x800759a0
void vm_svc_set_voice_cache_state(int v);                 // 0x800759a0
void vm_svc_voice_queue_abort();                          // voice_queue_abort 0x80079a7c
int vm_svc_voice_party_spu_needed();                      // voice_party_spu_needed 0x80078e0c
void vm_svc_voice_queue_reset();                          // voice_queue_reset 0x80078f88
int vm_svc_voice_queue_build_battle(int budget);          // voice_queue_build_battle 0x80079000
void vm_svc_set_voice_budget(int v);                      // 0x80075998
void vm_svc_set_force_streaming(int v);                   // g_force_streaming 0x800756d8
void vm_svc_field_prepare_encounter(int a, int b);        // field_prepare_encounter 0x80050f64
int vm_svc_get_battle_preload_count();                    // 0x8007534c
int vm_svc_get_battle_preload_entry(int i);               // 0x800759a8[i]
int vm_svc_get_battle_preload_size(int i);                // 0x800759dc[i]
void vm_svc_field_error_stub(int code);                  // field_error_stub 0x80055da0 (empty)
void vm_svc_field_start_battle(int r);                    // field_start_battle 0x80051a34 (calls vm_save_state)
bool vm_svc_battle_transition_done();                     // 0x800752d0 +0x10
void vm_svc_field_destroy_battle_transition();            // field_destroy_battle_transition 0x80051c68
int vm_svc_get_battle_ready();                            // 0x8007534d
void vm_svc_field_exit_to_program(int mode);              // field_exit_to_program 0x80051c9c
int vm_svc_preload_release(int kind);                     // preload_release 0x80056728
void vm_svc_preload_release_kind2_except(int tag);       // preload_release_kind2_except 0x80056830
int vm_svc_preload_start(int entry, int kind, int tag, int extra);  // preload_start 0x800563a0
int vm_svc_preload_count_done(int kind);                  // preload_count_done 0x80056c68
void vm_svc_preload_prune_for_map(int map);               // preload_prune_for_map 0x8005711c
int vm_svc_preload_set_tag(int entry, int tag);           // preload_set_tag 0x800572c0
int vm_svc_get_preload_release_pending(int i);            // g_preload_release_pending 0x8007533c[i]
void vm_svc_set_preload_release_pending(int i, int v);
void vm_svc_get_map_preload(int* x, int* z, int* pending);  // 0x800756dc / 0x800756e0 / 0x800756d9 (ext 0xa5)
void vm_svc_set_map_preload(int x, int z, int pending);
void vm_svc_loader_cancel();                              // boot loader_cancel 0x80011b48
int vm_svc_loader_entry_size(int entry);                  // boot loader_entry_size 0x80011b98
int vm_svc_loader_poll();                                 // boot loader_poll 0x80011b88 (1 = busy)
int vm_svc_loader_11988(int a1, int entry);               // boot 0x80011988(loader, a1, entry)
void vm_svc_set_loader_cancel_flag(int v);                // 0x800762ff

// ---- Field menus --------------------------------------------------------------------
void vm_svc_set_menu_request(int menu);                   // g_menu_request 0x80076196
void vm_svc_set_menu_param(int param);                    // g_menu_param 0x8007619a
int vm_svc_menu_request_pending();                        // 0x80076196 != 0
void vm_svc_set_global_76194(int v);                      // 0x80076194 (u8, ext 0xe1)
void vm_svc_set_global_76195(int v);                      // 0x80076195 (u8, B 0x67)

// ---- Party / character helpers outside the VM's range -------------------------------
int vm_svc_party_get_specialty_level(int slot, int n);    // party_get_specialty_level 0x80030ba8
int vm_svc_party_get_super_specialty6();                  // party_get_super_specialty6 0x80030c20
void vm_svc_party_member_clear_bond(int slot);            // party_member_clear_bond 0x80030c64
// party_slot_init 0x8007a20c(&party[slot], &records[slot], id)
void vm_svc_party_slot_init(int slot, int character_id);
void vm_svc_party_move_character_to_slot(int character, int slot);  // party_move_character_to_slot 0x800531dc
void vm_svc_party_swap_characters(int a, int b);          // party_swap_characters 0x800534b4

// ---- Map objects / sprite packs -----------------------------------------------------
void vm_svc_sprite_packs_tint(int a, int b, int c);       // sprite_packs_tint 0x800525ac
void vm_svc_sprite_packs_restore_palettes();              // sprite_packs_restore_palettes 0x80052838
// sprite_anim_set_frame_bytes 0x800522d4
void vm_svc_sprite_anim_set_frame_bytes(int a, int b, int count, const s32* values);
void vm_svc_sprite_anim_set_frame_byte(int a, int b, int c, int d);  // sprite_anim_set_frame_byte 0x80052364
void vm_svc_sprite_anim_set_byte2(int a, int b, int c);   // sprite_anim_set_mode 0x800523c4
void vm_svc_sprite_anim_set_loop_frame(int a, int b, int c);  // sprite_anim_set_loop_frame 0x80052410

// ---- Overlay code (#3101 0x8007e000, 0x801ea000 = #3103, 0x801f9800 = #3104-#3109) ---
void vm_svc_glow_create(int r, int g, int b, int frames);  // ext 0x79: new 0x1c, glow_create 0x8007e058 -> 0x800752dc
void vm_svc_overlay_801f9fe8(ScriptCtx* ctx);             // ext 0x9c (#3108; fetches 10 arguments)
void vm_svc_overlay_801fa120(ScriptCtx* ctx);             // ext 0x9e (#3108; fetches 3)
void vm_svc_overlay_801f9bb4(ScriptCtx* ctx);             // ext 0xbf (#3106; fetches 5)
void vm_svc_field_special_command(int op);               // ext 0xa8: field_special_command 0x8008d928(ctx, op)
// table B 0x06: walk_set_group_enabled 0x8008df74(ctx, tag, on)
void vm_svc_walk_set_group_enabled(int tag, int on);
void vm_svc_inventory_stash();                           // table A 0x10: inventory_stash 0x8008ea0c
void vm_svc_inventory_unstash();                         // table A 0x11: inventory_unstash 0x8008ead8
void vm_svc_inventory_clear_all();                       // table A 0x34: inventory_clear_all 0x8008eb60
// Particle effects (#3101 fx_spawn_*: layer, positions << 12 as int[4] vectors, then parameters).
// A 0x12 fx_spawn_burst 0x8008a7ec
void vm_svc_fx_spawn_burst(int id, const s32 pos[3], int a4);
// A 0x13 fx_spawn_ring_textured 0x8008d2dc (a4..a11)
void vm_svc_fx_spawn_ring_textured(int id, const s32 pos[3], const s32 args[8]);
// B 0x0b fx_spawn_beam 0x8008a3a8 (a7..a10)
void vm_svc_fx_spawn_beam(int id, const s32 a[3], const s32 b[3], const s32 args[4]);
// B 0x4d fx_spawn_triangle 0x8008a684
void vm_svc_fx_spawn_triangle(int id, const s32 a[3], const s32 b[3], const s32 c[3], const s32 args[5]);
// B 0x78 fx_spawn_orb 0x8008a5a8 (a4..a9)
void vm_svc_fx_spawn_orb(int id, const s32 pos[3], const s32 args[6]);
// B 0x7b fx_spawn_streak 0x80089ea4
void vm_svc_fx_spawn_streak(int id, const s32 a[3], const s32 b[3], int a7, int a8, int a9, int a10, int a11, int a12);
// B 0x7c fx_spawn_spark 0x8008a2c0 (a7..a10)
void vm_svc_fx_spawn_spark(int id, const s32 a[3], const s32 b[3], const s32 args[4]);
// B 0x7d fx_spawn_ring 0x8008a4b4 (a4..a11)
void vm_svc_fx_spawn_ring(int id, const s32 pos[3], const s32 args[8]);
int vm_svc_overlay_801f9fb8(int a0);                      // table A 0x33: 0x801f9fb8(ctx, a0)
bool vm_svc_overlay_801f9800_exists();                    // table A 0x35: object 0x800752e8
void vm_svc_overlay_801f9800_create(int a1, int a2);      // new 0xb0, 0x801f9800 ctor; +4 = a1, +8 = a2
void vm_svc_overlay_801f9800_destroy();                   // 0x801f98c0(obj, 3)
int vm_svc_overlay_801f9f54();                            // table A 0x37: 0x801f9f54(obj)
void vm_svc_overlay_801f9a64(int a0);                     // table A 0x40
void vm_svc_overlay_801eac28(ScriptCtx* ctx, u32 instr);  // table B 0x16 / 0x70 (#3103; fetches 2 / 1): field_movie_preload
void vm_svc_overlay_801eaa30(ScriptCtx* ctx);             // table B 0x50 (#3103; fetches 2): field_movie_start
int vm_svc_overlay_801ea374(ScriptCtx* ctx);              // table B 0x6f (#3103; reads the 8 fetched): field_movie_sequence
int vm_svc_overlay_801ea_loaded();                        // 0x800752c8
int vm_svc_overlay_801ea_value();                         // 0x801ecb90

// ---- Other field globals the scripts write ------------------------------------------
void vm_svc_set_global_76224(int a, int b);               // 0x80076224 / 0x80076226 (s16, ext 0xb4)
void vm_svc_set_global_76220(int a, int b);               // 0x80076220 / 0x80076222 (s16, A 0x2a)
void vm_svc_set_global_7621d(int a, int b);               // 0x8007621d / 0x8007621e (u8, A 0x2b)
int vm_svc_get_global_761ea();                            // 0x800761ea (u16, ext 0xea)
int vm_svc_get_global_76214();                            // 0x80076214 (s16, ext 0xea)
void vm_svc_set_global_76310(int v);                      // 0x80076310 (A 0x35)
void vm_svc_set_global_76278(int v);                      // g_show_battle_results 0x80076278 (B 0x60)
void vm_svc_worldmap_add_entry(const s32 args[14]);       // A 0x24: 0x8007470c / 0x80076284 tables, count 0x80076282

}  // namespace ss::game::field
