#pragma once

#include <cstring>

#include "core/types.h"
#include "game/field/field.h"
#include "game/field/tasks.h"

// The field program's event-script VM (class ScriptVM, 0x11cc bytes, vtable 0x80074028,
// global 0x800752ac; field 0x800621e0-0x8006e3d8). One script thread runs per map:
// events (talking to an NPC, stepping on a trigger region, Private Actions, menu hooks)
// interrupt it by pushing a call frame and jumping, and the 'return' at call depth 0
// resumes the interrupted code. The thread's first 0x4c bytes are the execution
// context; the rest holds 58 trigger regions (convex floor quads bound to script
// functions), the music change in progress and the choice prompt.
//
// Script memory is the field state block (handoff 9): the call stack at +0, the value
// stack at +0x100, the variables at +0x180 (byte-addressed; word 0 is the return
// register R) and the flags at +0x8c0. A map's own variables and flags sit above the
// save-game ones (script header words 5 and 6), and call frames grow the locals there.
// docs/catalog/formats.md "Event script" has the instruction encoding, and
// analysis/field_catalog/part4.md the semantics this port follows.
namespace ss::game::field {

constexpr int kTriggerCount = 58;

// A trigger region (0x48 bytes at ScriptCtx + 0x4c + n * 0x48).
struct Trigger {
  const u32* code = nullptr;  // +0x00: step-on code (vm_save_state stores it as a word index)
  u8 active = 0;              // +0x04
  u8 dwell = 0;               // +0x05: fires once the leader has moved inside for dwell_frames (ext 0x9a)
  u8 menu_flags = 0;          // +0x06: also fires while script flags 12 / 14 are set (A 0x1d)
  u8 world_map = 0;           // +0x07: world-map flag (A 0x27; tested by the world-map overlay)
  s16 dwell_frames = 0;       // +0x08
  s16 private_action = -1;    // +0x0a: Private Action entry (word index), -1 none (B 0x0e)
  s32 unknown0c = 0;          // +0x0c: never written
  u32 buttons = 0;            // +0x10: pad mask that fires it while inside; 0xffffffff = on entry
  s32 corner[8] = {};         // +0x14: x0, z0 .. x3, z3 in map units, clockwise
  s16 y_min = 0;              // +0x34
  s16 y_max = 0;              // +0x36
  s32 min_x = 0;              // +0x38: bounding box (trigger_compute_bbox)
  s32 min_z = 0;              // +0x3c
  s32 max_x = 0;              // +0x40
  s32 max_z = 0;              // +0x44
};

struct ScriptCtx final : Task {
  void update() override;  // vtable +0xc: vm_thread_update

  u32 instr = 0;                    // +0x08: instruction being executed (vm_fetch_args reads it)
  s32 wait = 0;                     // +0x0c: frames to wait (ext 0x7a adds)
  const u32* ip = nullptr;          // +0x10
  const u32* code = nullptr;        // +0x14: script + 0x1c + hdr[1] * 4
  u8* locals = nullptr;             // +0x18: top of the local variables (local n = top - n - size)
  u8* local_flags = nullptr;        // +0x1c: top of the local flags (byte top - (n >> 3) - 1)
  u32* call_sp = nullptr;           // +0x20: call stack (state block + 0), grows up, 4 words a frame
  u32* value_sp = nullptr;          // +0x24: value stack (state block + 0x100), grows up
  const u8* strings = nullptr;      // +0x28: u16 string offsets (script + 0x1c + hdr[0])
  const u8* text = nullptr;         // +0x2c: text (strings + hdr[3] * 2)
  s32 wait_state = 0;               // +0x30: see vm_thread_update
  s32 wait_param = 0;               // +0x34: map, window, actor, entry, fade...
  s32 unknown38[3] = {};            // +0x38
  s32 arrival_dir = 0;              // +0x44: map transfer: the leader's direction on arrival
  u8 arrival_param = 0;             // +0x48: map transfer: hero actor parameter (0x80, or ext 0xda arg 6)
  u8 transition = 0;                // +0x49: map transfer: transition type (arg 1 & 0xf)
  u8 transfer_flag = 0;             // +0x4a: map transfer: arg 1 & 0x80 (field_load_map flag 2)
  // +0x4c. The console has room for exactly 58; trigger_alloc_slot returns 58 when they
  // are all in use and the caller then writes over +0x109c onwards. The extra slot keeps
  // that write inside the object (no shipped script registers 59).
  Trigger triggers[kTriggerCount + 1];
  s32 active_trigger = -1;          // +0x109c: slot whose event runs (-1 none); tags its preloads
  s32 last_trigger = -1;            // +0x10a0: slot the leader last entered (no re-fire until left)
  s32 sync = 0;                     // +0x10a4: vm_call_sync running: waits are skipped
  s16 music = -1;                   // +0x10a8: music entry being loaded, -1 none
  u8 music_installed = 0;           // +0x10aa
  u8 talk_active = 0;               // +0x10ab: a talk event runs
  u8 talk_returned = 0;             // +0x10ac: it returned (window close pending)
  s16 talk_actor = 0;               // +0x10ae: NPC talked to (-1: pickpocket)
  u8 first_fade = 0;                // +0x10b0: 1 after vm_init: the first fade-in gives control to the hero
  u8 music_loaded = 0;              // +0x10b1: set by field_load_entry when the music entry is read
  u8 music_crossfade = 0;           // +0x10b2: ext 0xc5: start it with sound_play_sequence_fade
  s16 music_fade_b = 0;             // +0x10b4
  u32 music_fade_a = 0;             // +0x10b8
  s16 choice_count = 0;             // +0x10bc: options
  s16 choice_first_line = 0;        // +0x10be
  s16 choice_selection = 0;         // +0x10c0
  s16 choice_cancel = 0;            // +0x10c2: Circle allowed
  struct {
    s32 x = 0, y = 0;
  } choice_cursor[16];              // +0x10c4: cursor position per line
  s16 choice_slide = 0;             // +0x1144: cursor slide frames left
  s16 choice_lines = 0;             // +0x1146: lines in the selected option
  s16 choice_width[16] = {};        // +0x1148
  s16 choice_height[16] = {};       // +0x1168
  s16 choice_close = 0;             // +0x1188: close the window after the choice
  s32 depth = 0;                    // +0x118c: call depth (events start at 1)
  u8 sync_return = 0;               // +0x1190: vm_call_sync: return from the update at depth 0
  u8 pickpocket_bonus = 0;          // +0x1191: +5% (accessory 0x1bc)
  u8 restart = 0;                   // +0x1192: restart the update after this ext op (no writer in the field image)
  s32 saved_control = 0;            // +0x1194: actor the player controlled before the event
  s16 menu_close_entry = -1;        // +0x1198: script entry run by vm_call_sync when the field menu closes
  s16 dwell_counter = 0;            // +0x119a
  s32 pickpocket_level = 0;         // +0x119c
  s32 thief_slot = 0;               // +0x11a0
  s32 globals_size = 0;             // +0x11a4: hdr[5] rounded up to 4: start of the map's variables
  s32 flags_size = 0;               // +0x11a8: hdr[6]: start of the map's flags
  s16 option_lines[16] = {};        // +0x11ac: choice: lines per option (A 0x41-0x48)
};

// Globals of the VM.
extern ScriptCtx* g_script_vm;    // 0x800752ac
extern const u8* g_map_script;    // 0x800756f8: section 1 block 0 of the current map (set by the map loader)
extern s32 g_vm_args[16];         // scratchpad 0x1f800000: arguments fetched by vm_fetch_args

// Instrumentation for tests (not in the original): instructions no handler claims
// (opcode 0x00, 0x1e-0x20, 0x37, 0x39, the empty ext slots, the table A / B no-ops),
// which in a real script mean the VM lost track of the instruction stream; and an
// optional cap on instructions per update (0 = none) so a sweep cannot hang on a
// script that spins waiting for a subsystem the stand-ins never finish.
struct VmStats {
  u32 unknown_ops = 0;
  u32 last_unknown = 0;
  u32 step_limit_hits = 0;
};
extern VmStats g_vm_stats;
extern u32 g_vm_step_limit;
extern void (*g_vm_ext_hook)(ScriptCtx* ctx, u32 instr);  // called on entry to vm_exec_ext when set
inline void vm_note_unknown(u32 instr) {
  ++g_vm_stats.unknown_ops;
  g_vm_stats.last_unknown = instr;
}

// --- VM core (script_vm.cpp) ---------------------------------------------------------
ScriptCtx* vm_ctx_create(ScriptCtx* ctx);            // 0x800621e0: clears the triggers and cursors
void vm_init(ScriptCtx* ctx);                        // 0x80062260: binds g_map_script; enters at hdr[2]
void vm_thread_update(ScriptCtx* ctx);               // 0x8006b944: one frame of the script thread
void vm_fetch_args(ScriptCtx* ctx, int count);       // 0x80068db8: arguments into g_vm_args
void vm_push_frame(ScriptCtx* ctx);                  // 0x8006b518
void vm_call_sync(ScriptCtx* ctx, u16 entry);        // 0x8006a6d0: runs a function to completion now
void vm_save_state(ScriptCtx* ctx);                  // 0x800697bc: before a battle (handoff 0xd)
void vm_restore_state(ScriptCtx* ctx);               // 0x80069aac: after it (call after vm_init)
int vm_preload_entry(ScriptCtx* ctx, int entry, int own, int extra_bytes);  // 0x8006af4c
// Accessors the rest of the field program uses.
inline s32 vm_get_wait_state(const ScriptCtx* ctx) { return ctx->wait_state; }           // 0x8006e3cc
inline bool vm_music_pending(const ScriptCtx* ctx) { return ctx->music != -1; }          // 0x8006e3d8
// 0x8006e3ec
inline u16 vm_get_menu_close_entry(const ScriptCtx* ctx) { return static_cast<u16>(ctx->menu_close_entry); }
inline s32 vm_get_active_trigger(const ScriptCtx* ctx) { return ctx->active_trigger; }   // 0x8006e3f8

// --- Extended opcodes (script_ext.cpp, script_table_a.cpp, script_table_b.cpp) -------
void vm_exec_ext(ScriptCtx* ctx, u32 instr);         // 0x8006241c: opcodes 0x64-0xff
void vm_exec_table_a(ScriptCtx* ctx, u32 instr);     // 0x80067634: opcode 0xfe
void vm_exec_table_b(ScriptCtx* ctx, u32 instr);     // 0x80064f30: opcode 0xff

// --- Trigger regions and talking (script_triggers.cpp) -------------------------------
int trigger_alloc_slot(ScriptCtx* ctx);                       // 0x80068ec0
void trigger_remove(ScriptCtx* ctx, u32 slot);                // 0x80068eec
void trigger_compute_bbox(ScriptCtx* ctx, Trigger* t);        // 0x80068d3c
void trigger_check_regions(ScriptCtx* ctx);                   // 0x80068810
bool trigger_contains_leader(ScriptCtx* ctx, int slot);       // 0x8006ad40
void worldmap_trigger_add(ScriptCtx* ctx, u32 op);            // overlay #3102 0x800829fc (ext 0x76 / 0x77)
int field_try_talk(ScriptCtx* ctx, int talk);                 // 0x80069d8c
// The tail of field_try_talk: starts actor 'found''s talk (talk != 0) or pickpocket entry.
void field_start_talk(ScriptCtx* ctx, int found, int talk);
void pa_draw_prompt(ScriptCtx* ctx);                          // 0x8006b630
void choice_draw_cursor(ScriptCtx* ctx);                      // 0x8006afd8

// --- Party, characters and inventory (script_party.cpp) ------------------------------
// Party slots (0x60 bytes, handoff 3): +0 s16 character id + 1 (negative: reserve, 0:
// empty). Character records (0xd0 bytes, handoff 4) follow the slot order.
int party_find_slot(ScriptCtx* ctx, int character);                          // 0x800692a0
void party_get_hp_status(ScriptCtx* ctx, int character, s32* hp, u8* status);  // 0x80068f10
void party_restore_hp_all(ScriptCtx* ctx);                                   // 0x80068f78
void party_restore_mp_all(ScriptCtx* ctx);                                   // 0x80068fc4
void party_add_member(ScriptCtx* ctx, int character);                        // 0x80069008
void party_remove_member(ScriptCtx* ctx, int character);                     // 0x80069200
void char_unequip_item(ScriptCtx* ctx, int character, int item);             // 0x800692ec
int char_set_equipment(ScriptCtx* ctx, int character, int slot, int item);   // 0x800694a0
int party_has_equipped(ScriptCtx* ctx, int item);                            // 0x800695f4
int char_get_equipment(ScriptCtx* ctx, int character, int slot);             // 0x800696c4
void party_isolate_hero(ScriptCtx* ctx);                                     // 0x8006a7ac
void party_swap_slots(ScriptCtx* ctx, int a, int b);                         // 0x8006a8ec
void party_swap_characters(int a, int b);                                    // 0x800534b4
void party_restore_saved(ScriptCtx* ctx);                                    // 0x8006ab4c
int relation_hash_mod(ScriptCtx* ctx, int n);                                // 0x8006b59c
int party_count_members();                                                   // 0x80052268
u32 party_member_mask();                                                     // 0x80061bd0
u32 party_member_mask_all();                                                 // 0x80061c4c
// Inventory (handoff 6): u16 entry[0x400] (item | count << 10 | new << 15), u16 recent[16]
// at +0x800, u8 check[0x400] at +0x824 (rewritten with an rng_range(16) salt on every
// change), +0xc24 clear-recent flag.
int inventory_add(u8* inv, int item, int count, int is_new);   // 0x8003c594
void inventory_push_recent(u8* inv, int item);                 // 0x8003c70c
int inventory_remove_at(u8* inv, int index, int count);        // 0x8003c798 (catalogued as inventory_set_count)
int inventory_find(const u8* inv, int item);                   // 0x8003c820
u8 inventory_check_byte(const u8* inv, u32 index, u32 salt);   // 0x8003c8a0 (salt 0xffff: rng_range(16))
bool inventory_verify(const u8* inv, u32 index);               // 0x8003c928

// --- libgte helpers the VM uses (script_gte.cpp; the field program's own copies) -----
struct VmVector {
  s32 vx = 0, vy = 0, vz = 0, pad = 0;
};
s32 SquareRoot0(s32 a);                                          // 0x8006ffc4
s32 ratan2(s32 y, s32 x);                                        // 0x80070a94
s32 TransRot_32(const VmVector* v0, VmVector* v1, s32* flag);    // 0x80070cf4

// --- Script memory accessors (byte-addressed, as on the console) ---------------------
inline s32 vm_rd32(const u8* p) {
  s32 v;
  std::memcpy(&v, p, 4);
  return v;
}
inline s16 vm_rd16(const u8* p) {
  s16 v;
  std::memcpy(&v, p, 2);
  return v;
}
inline void vm_wr32(u8* p, u32 v) { std::memcpy(p, &v, 4); }
inline void vm_wr16(u8* p, u32 v) {
  const u16 h = static_cast<u16>(v);
  std::memcpy(p, &h, 2);
}
// The return register R (script variable word 0).
inline s32 vm_r() { return vm_rd32(globals().script_globals); }
inline void vm_set_r(u32 v) { vm_wr32(globals().script_globals, v); }
// The field state block (0x8007570c, = the script variables) and the game state.
inline s16 field_state_s16(int offset) { return vm_rd16(globals().field_flags + offset); }
inline void field_state_set_s16(int offset, int v) { vm_wr16(globals().field_flags + offset, static_cast<u32>(v)); }
inline u16 config_button(int n) { return static_cast<u16>(read_u16(globals().config + 2 * n)); }
inline int leader_slot() { return globals().game_state[0x24]; }  // game_state + 0x24
// The story hero's actor slot: Claude (0) when script flag 1 is set, else Rena (1).
inline int hero_slot() { return (globals().script_flags[0] >> 1 & 1) == 0 ? 1 : 0; }

}  // namespace ss::game::field
