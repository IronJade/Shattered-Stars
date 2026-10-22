#pragma once

#include <cstddef>

#include "core/types.h"
#include "runtime/libgpu.h"

// The field actor base class (field program 0x8003d4d8-0x80042ffc): one character or
// object on the map. Concrete actors are the code overlay's classes (field maps:
// actor_field.h, 0x3fc bytes; the world map's 0x44c-byte class is not ported). The
// base holds the position (20.12 fixed point, map units << 12), the animation, the
// turn / move / colour-fade state, a 256-entry ring of queued commands that scripts
// fill (actor_cmd_* / actor_queue_*) and actor_update runs, and the primitives drawn
// for the current frame.
//
// The struct keeps the original field order. Offsets are exact up to the sprite group
// pointer (+0x2d0): the first 0xa2 bytes are what actor_save_state copies into the
// handoff-0xb snapshot, and scripts address several fields by offset. From +0x2d0 the
// pointers are host pointers, so later offsets are only noted in comments.
namespace ss::game::field {

struct Actor;

// The virtual methods (vtable entries 1-4; the vptr is at +0x3f8).
struct ActorVtable {
  void (*draw)(Actor* self, int arg);             // +0x08: builds the frame's primitives (actor_sys_draw passes 1)
  void (*control)(Actor* self, int camera);       // +0x10: player control (actor_update when controlled)
  void (*follow_camera)(Actor* self);             // +0x18: camera target: projects and scrolls the BG
  void (*move_step)(Actor* self);                 // +0x20: one frame of the move in progress (+0x27 != 0)
};

// Command words in the ring (actor_update's switch; arguments follow the command).
enum ActorCmd : u16 {
  kActorCmdWait = 1,          // frames
  kActorCmdWaitMove = 2,      // wait state 2
  kActorCmdWaitTurn = 3,      // wait state 4
  kActorCmdWaitAnim = 4,      // loops, absolute
  kActorCmdWaitFade = 5,      // wait state 6
  kActorCmdTurnTo = 6,        // degrees, speed (degrees), mode
  kActorCmdTurnBy = 7,        // degrees, speed, mode
  kActorCmdTurnToActor = 8,   // actor, speed, mode
  kActorCmdMoveTo = 9,        // x, y, z, speed
  kActorCmdMoveToXz = 0xa,    // x, z, speed (screen-space actors: x, y, frames)
  kActorCmdMoveBy = 0xb,      // dx, dy, dz, speed
  kActorCmdMoveByXz = 0xc,    // dx, dz, speed
  kActorCmdMoveDir = 0xd,     // degrees, distance, speed
  kActorCmdMoveDirRel = 0xe,  // degrees from the facing, distance, speed
  kActorCmdSetAnim = 0xf,     // animation
  kActorCmdDelete = 0x10,
  kActorCmdWaitMoveB = 0x11,  // wait state 3
  kActorCmdTurnToPoint = 0x12,// x, z, speed, mode
  kActorCmdEmote = 0x13,      // icon, frames, flag
  kActorCmdClearEmote = 0x14,
  kActorCmdSignal = 0x15,     // script entry run synchronously (vm_call_sync)
  kActorCmdJumpTo = 0x19,     // x, y, z, height, frames
  kActorCmdJumpBy = 0x1a,     // dx, y, dz, height, frames
  kActorCmdFade = 0x1b,       // r, g, b, frames, blend
  kActorCmdMoveToFar = 0x1c,  // x (hi, lo), y (hi, lo), z (hi, lo), speed
};

// Flags at +0x4c (actor_create's default is 0x1000f).
enum : u32 {
  kActorShaded = 0x1,          // shading regions (cell lists 4 / 6) and tint regions (list 8)
  kActorZones = 0x2,           // floor zones (cell 0 list 14): height and control angles
  kActorBlockedByActors = 0x4, // stops against actors with kActorSolid
  kActorSolid = 0x8,           // blocks other actors
  kActorFixedLayer = 0x10,     // keeps its OT layer (no cell list 2 lookup)
  kActorClampScale = 0x20,     // clamps sprites wider than 63 px instead of drawing them unscaled
  kActorAnimPaused = 0x80,     // no automatic (direction) animation
  kActorRawScale = 0x100,      // screen-space sprite scale is +0x64 / +0x68 only
  kActorScreen = 0x200,        // drawn at +0x74 / +0x76 without projection (0x80081538)
  kActorShadow = 0x10000,      // drop shadow
  kActorScreenSpace = 0x20000, // +0x74 / +0x76 are the position (no 3D moves, no collision)
  kActorLockedFacing = 0x40000,// talks don't turn it
  kActorFlipX = 0x80000,
  kActorFlipY = 0x100000,
  kActorWalls = 0x200000,      // collides with walls (cell 0 list 15)
  kActorRunning = 0x400000,    // run animations (set by player control)
  kActorNoCache = 0x800000,    // draws through sprite cache B (unscaled 4-bit)
  kActorPatrol = 0x2000000,    // move commands shuttle between start and target
  kActorPatrolEmote = 0x4000000,
  kActorFixedLight = 0x8000000,// keeps +0x3c6..+0x3c8
  kActorHidden = 0x10000000,   // draws nothing
  kActorNoScroll = 0x20000000, // ignores the BG scroll
};

// Every field starts at zero; actor_construct_base / actor_set_defaults / actor_init
// set them as the original does (its heap blocks are not cleared).
struct Actor {
  s32 x = 0, y = 0, z = 0, w = 0;   // +0x00: position, 20.12 (+0x0c travels with it)
  s16 anim = 0;                     // +0x10: animation id; -1 = by direction (auto_anim)
  s16 anim_timer = 0;               // +0x12: vsyncs into the current frame
  s16 anim_frame = 0;               // +0x14: frame index in the AnimData
  s16 model = 0;                    // +0x16: sprite key (0x7fff = attachment)
  s16 wait_count = 0;               // +0x18: cmd 1 frames / cmd 4 loops
  s16 turn_target = 0;              // +0x1a
  s16 turn_step = 0;                // +0x1c: angle per frame (signed)
  s16 turn_frames = 0;              // +0x1e: frames left
  s16 angle = 0;                    // +0x20: facing, 4096 = 360 degrees
  u16 layer = 0;                    // +0x22: OT layer (prims go to OT[layer + 0x11]); 0 = not drawn
  u8 radius = 0;                    // +0x24: size (0x18), the collision radius before scaling
  u8 turning = 0;                   // +0x25
  u8 anim_forward = 0;              // +0x26: ping-pong direction (1 = forward)
  u8 move_mode = 0;                 // +0x27: 0 none, 1 xyz, 2 xz (walk), 3 hop, 4 jump
  s32 target[4] = {};               // +0x28: move target
  s32 start[4] = {};                // +0x38: move start
  s32 lift = 0;                     // +0x48: height above the position (hops)
  u32 flags = 0;                    // +0x4c
  s16 jump_height = 0;              // +0x50
  s16 talk_entry = 0;               // +0x52: script entry (word index) run when talked to
  s32 speed = 0;                    // +0x54: per-vsync step (20.12); negative before actor_calc_speed = frames
  s16 emote_timer = 0;              // +0x58: frames the emote child stays (-1 = forever)
  s16 emote_anim = 0;               // +0x5a: the emote child's animation (snapshots)
  s16 move_frames = 0;              // +0x5c
  s16 move_left = 0;                // +0x5e
  s16 jump_angle_a = 0;             // +0x60
  s16 jump_angle_b = 0;             // +0x62
  s32 scale_x = 0;                  // +0x64
  s32 scale_y = 0;                  // +0x68
  s32 vel_x = 0;                    // +0x6c: per-frame step of hops / screen moves
  s32 vel_z = 0;                    // +0x70
  s16 screen_x = 0;                 // +0x74: projected position (or the position of screen-space actors)
  s16 screen_y = 0;                 // +0x76
  u8 rgb[3] = {};                   // +0x78: colour (0x80 = neutral)
  u8 fading = 0;                    // +0x7b
  u8 fade_from[3] = {};             // +0x7c
  u8 blend = 0;                     // +0x7f: semi-transparency: 0 off, else abr + 1
  u8 fade_to[3] = {};               // +0x80
  u8 tinted = 0;                    // +0x83: the global brightness (ActorSystem +0xc) applies
  u8 wait_state = 0;                // +0x84: 1 timer, 2/3 move, 4 turn, 5 anim loops, 6 fade
  u8 vehicle = 0;                   // +0x85
  u8 moved = 0;                     // +0x86: moved or turned this frame
  u8 shuttle = 0;                   // +0x87: patrol: swap target and start at the end
  s16 fade_frames = 0;              // +0x88
  s16 fade_timer = 0;               // +0x8a
  s16 spin_angle = 0;               // +0x8c: sprite rotation (table A 0x0e; 0x7fff = none)
  s16 heading = 0;                  // +0x8e: ratan2 of the move
  s16 pickpocket_entry = 0;         // +0x90: script entry for a pickpocket (actor_create a7)
  s16 pad_92 = 0;                   // +0x92
  s32 walk_speed = 0;               // +0x94: scale% * 0x3000 / 100
  s16 prev_angle = 0;               // +0x98: facing before this frame
  u16 auto_anim = 0;                // +0x9a: animation picked from the facing (0x80085d94)
  s16 shadow_w = 0;                 // +0x9c: shadow size, 0x400 = 1.0
  s16 shadow_h = 0;                 // +0x9e
  s16 spin_speed = 0;               // +0xa0: rotation per vsync (table A 0x0f)
  u16 cmd[256] = {};                // +0xa2: command ring
  u16 cmd_read = 0;                 // +0x2a2
  u16 cmd_write = 0;                // +0x2a4
  u16 anim_loops = 0;               // +0x2a6: loops of the current animation
  s32 drawn_pos[4] = {};            // +0x2a8: position of the cached primitives
  s16 quad[8] = {};                 // +0x2b8: main sprite corners x0, y0 .. x3, y3 (screen)
  s8 cache_slot[4] = {};            // +0x2c8: sprite cache slots in use this frame (B slots + 0x20)
  u8 cache_slots = 0;               // +0x2cc
  u8 walked = 0;                    // +0x2cd: moved this frame (player control)
  u16 resolved_for = 0;             // +0x2ce: auto_anim the sprite was resolved for (0xffff = none)
  // --- host pointers from here; original offsets in the comments ---
  const u8* group = nullptr;        // +0x2d0: sprite group (sprite_pack_find_group)
  u8 analog = 0;                    // +0x2d4: steered by the analog stick this frame
  u8 pad_2d5 = 0;                   // +0x2d5
  u8 pushed = 0;                    // +0x2d6: actor_push_state snapshot taken
  s32 collide_radius = 0;           // +0x2d8: radius * scale%
  s32 field_2dc = 0;                // +0x2dc
  const u8* zone = nullptr;         // +0x2e0: floor zone the actor stands in (cell 0 list 14)
  const u8* cel_header = nullptr;   // +0x2f4: record "cels" block: +2 mode, +4/+8/+0xc part tables
  const u8* cels = nullptr;         // +0x2f8: cel table (header + 0x14)
  const u8* frames = nullptr;       // +0x2fc: frame definitions (0xc bytes)
  uintptr_t drawn_frame = 0;        // +0x300: frame of the cached primitives (0 = redraw)
  const u8* pixels = nullptr;       // +0x304
  const u8* palettes = nullptr;     // +0x308: u16 flags, then 16-colour palettes from +4
  const u8* anim_data = nullptr;    // +0x30c: AnimData
  s16 cel = 0;                      // +0x310
  s32 depth = 0;                    // +0x314: view depth / 4 (sort key)
  psyq::P_TAG* prims[24] = {};      // +0x318
  u16 prim_ot[24] = {};             // +0x378: OT index - 16 of each primitive
  s32 prim_count = 0;               // +0x3a8
  s32 drawn_lift = 0;               // +0x3ac: lift of the cached primitives
  s16 drawn_scroll[2] = {};         // +0x3b0: BG scroll of the cached primitives
  s16 saved_angle = 0;              // +0x3b4: actor_push_state
  s16 origin_x = 0;                 // +0x3b6: screen origin of the primitives
  s16 origin_y = 0;                 // +0x3b8
  s16 field_3ba = 0;                // +0x3ba
  s16 field_3bc = 0;                // +0x3bc
  s16 frozen_turn_target = 0;       // +0x3be: actor_save_anim
  s16 frozen_turn_step = 0;         // +0x3c0
  s16 frozen_turn_frames = 0;       // +0x3c2
  u8 frozen_turning = 0;            // +0x3c4
  u8 frozen = 0;                    // +0x3c5: only the turn runs until it ends
  u8 light[3] = {};                 // +0x3c6: colour of this frame's primitives
  u16 loops_base = 0;               // +0x3ca: cmd 4
  u8 loops_absolute = 0;            // +0x3cc
  u8 pop_turn_mode = 0;             // +0x3cd
  Actor* emote = nullptr;           // +0x3d0: attachment (emote icon) child
  Actor* children[2] = {};          // +0x3d4
  u8* pushed_state = nullptr;       // +0x3dc: actor_push_state snapshot
  u8 drawn_arg = 0;                 // +0x3e0: draw argument of the cached primitives
  u8 walked_last = 0;               // +0x3e1: +0x2cd of the previous frame
  u8 field_3e8 = 0;                 // +0x3e8
  s16 sprite_anim = 0;              // +0x3ea: animation id the sprite was resolved for
  u8 blocked = 0;                   // +0x3ec: walked into a wall last frame
  u8 zone_dir = 0;                  // +0x3ed: 0x80 = none
  u8 zone_dir_prev = 0;             // +0x3ee
  u8 laps = 0;                      // +0x3ef: patrol laps
  u8 lap_limit = 0;                 // +0x3f0: laps before the patrol emote (actor_set_patrol_emote)
  s16 still_time = 0;               // +0x3f2: frames without moving
  s16 lap_emote = 0;                // +0x3f4: patrol emote animation
  const ActorVtable* vtbl = nullptr;// +0x3f8
};

static_assert(offsetof(Actor, anim) == 0x10 && offsetof(Actor, layer) == 0x22 && offsetof(Actor, target) == 0x28);
static_assert(offsetof(Actor, flags) == 0x4c && offsetof(Actor, speed) == 0x54 && offsetof(Actor, scale_x) == 0x64);
static_assert(offsetof(Actor, screen_x) == 0x74 && offsetof(Actor, wait_state) == 0x84 && offsetof(Actor, walk_speed) == 0x94);
static_assert(offsetof(Actor, cmd) == 0xa2 && offsetof(Actor, cmd_read) == 0x2a2 && offsetof(Actor, quad) == 0x2b8);
static_assert(offsetof(Actor, resolved_for) == 0x2ce, "offsets are exact up to the sprite group pointer");

// The size actor_save_state copies before the command queue.
constexpr int kActorStateBytes = 0xa2;

// --- base class (0x8003d4d8-0x8003f948) ---
Actor* actor_construct_base(Actor* self);                       // 0x8003d4d8
void actor_destroy(Actor* self, int flags);                     // 0x8003d53c: flags & 1 frees it
void actor_set_defaults(Actor* self);                           // 0x8003d63c
void actor_init(Actor* self, s16 model, const s32 pos[3], int angle_deg, int layer, s16 talk_entry,
                s16 pickpocket_entry, u32 flags);               // 0x8003d750
void actor_init_screen(Actor* self, s16 model, s16 x, s16 y, int layer);  // 0x8003d8e4
// 0x8003d95c: one frame. 'control' = the player controls it (calls vtable control
// instead of advancing the animation); 'camera' = it is the camera target.
void actor_update(Actor* self, int control, int camera);
void actor_jump_setup(Actor* self);                             // 0x8003edec
void actor_anim_advance(Actor* self);                           // 0x8003ef38
void actor_calc_speed(Actor* self);                             // 0x8003f0f4
void actor_turn_start(Actor* self, s32 angle, u16 step, int mode);  // 0x8003f240
void actor_turn_step(Actor* self);                              // 0x8003f3a4
void actor_fade_step(Actor* self);                              // 0x8003f424
void actor_resolve_anim(Actor* self);                           // 0x8003f518
bool actor_cmd_has_room(const Actor* self, s16 n);              // 0x8003f8f8
// 0x8003f948: links the frame's primitives into the OT (tinted by the global
// brightness and the actor colour). Primitives whose OT index is 'layer_sel' go to
// alt_ot[0x820 - depth / 2] instead (OT layers).
void actor_link_prims(Actor* self, int children_only, u32 layer_sel, psyq::P_TAG* alt_ot);

// --- command pushers (0x8003fce0-0x80040bd0); angles in degrees, positions in map units ---
void actor_cmd_wait(Actor* self, s16 frames);                               // 0x8003fce0
void actor_cmd_wait_move(Actor* self);                                      // 0x8003fd68
void actor_cmd_wait_move_b(Actor* self);                                    // 0x8003fdc4
void actor_cmd_wait_turn(Actor* self);                                      // 0x8003fe20
void actor_cmd_turn_to(Actor* self, s16 deg, s16 speed_deg, s16 mode);      // 0x8003fe7c
void actor_cmd_turn_by(Actor* self, s16 deg, s16 speed_deg, s16 mode);      // 0x8003ff5c
void actor_cmd_turn_to_actor(Actor* self, s16 actor, s16 speed_deg, s16 mode);  // 0x8004003c
void actor_cmd_turn_to_point(Actor* self, s16 x, s16 z, s16 speed_deg, s16 mode);  // 0x8004011c
void actor_cmd_move_to_xz(Actor* self, s16 x, s16 z, s16 speed);            // 0x80040228
void actor_cmd_move_to(Actor* self, s16 x, s16 y, s16 z, s16 speed);        // 0x80040308
void actor_cmd_move_to_far(Actor* self, s32 x, s32 y, s32 z, s16 speed);    // 0x80040414
void actor_cmd_move_by_xz(Actor* self, s16 dx, s16 dz, s16 speed);          // 0x80040580
void actor_cmd_move_by(Actor* self, s16 dx, s16 dy, s16 dz, s16 speed);     // 0x80040660
void actor_cmd_move_dir(Actor* self, s16 deg, s16 dist, s16 speed);         // 0x8004076c
void actor_cmd_move_dir_rel(Actor* self, s16 deg, s16 dist, s16 speed);     // 0x8004084c
void actor_cmd_set_anim(Actor* self, s16 anim);                             // 0x8004092c
void actor_cmd_wait_anim(Actor* self, s16 loops, s16 absolute);             // 0x800409b4
void actor_cmd_signal(Actor* self, s16 value);                              // 0x80040a68
void actor_cmd_emote(Actor* self, s16 icon, s16 frames, s16 flag);          // 0x80040af0
void actor_cmd_clear_emote(Actor* self);                                    // 0x80040bd0
void actor_delete_attachment(Actor* a);                                     // 0x80040c2c
void actor_queue_delete(Actor* a);                                          // 0x80040c6c
void actor_queue_move_to(Actor* a, s16 x, s16 y, s16 z, s16 height, s16 frames);    // 0x80040cc8 (cmd 0x19)
void actor_queue_move_by(Actor* a, s16 dx, s16 y, s16 dz, s16 height, s16 frames);  // 0x80040e00 (cmd 0x1a)
void actor_queue_fade_colour(Actor* a, s16 r, s16 g, s16 b, s16 frames, s16 blend); // 0x80040f38
void actor_queue_wait_fade(Actor* a);                                       // 0x80041070

// --- queries and state (0x800410cc-0x80042ffc) ---
bool actor_collides(Actor* a, const s32* pos, int absolute);   // 0x800410cc: another solid actor within reach
bool actor_touches_save_point(const Actor* a);                 // 0x80041310
bool actor_is_busy(const Actor* a);                            // 0x80041450: queue, turn, move or wait pending
int actor_state_size(const Actor* a);                          // 0x800414a0
void actor_save_state(Actor* a, u8* out);                      // 0x800414d4
void actor_load_state(Actor* a, const u8* in, int recreate_attachment);  // 0x80041608
void actor_create_attachment(Actor* a);                        // 0x80041748
void actor_push_state(Actor* a);                               // 0x80041918
void actor_push_state_and_turn(Actor* a, int angle, int mode); // 0x80041998
void actor_pop_state(Actor* a, int turn_back);                 // 0x80041aa4
void actor_stop(Actor* a);                                     // 0x80042ca0
void actor_set_anim(Actor* a, s16 anim);                       // 0x80042cc4
void actor_mark_dirty(Actor* a);                               // 0x80042ce4
void actor_set_flags(Actor* a, s16 part, u32 flags);           // 0x80042cf8
void actor_or_flags(Actor* a, s16 part, u32 bits);             // 0x80042d14
void actor_and_flags(Actor* a, s16 part, u32 mask);            // 0x80042d3c
int actor_walk_frame_delay(const Actor* a);                    // 0x80042f2c
void actor_save_anim(Actor* a);                                // 0x80042fd0
void actor_set_patrol_emote(Actor* a, u8 laps, s16 emote);          // 0x80042ffc

// Angle conversion used by the command interpreter: degrees -> 4096 per turn.
inline s32 actor_deg_to_angle(s32 deg) { return (deg << 12) / 360; }

}  // namespace ss::game::field
