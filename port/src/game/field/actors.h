#pragma once

#include "core/types.h"
#include "game/field/actor_base.h"
#include "game/field/tasks.h"
#include "runtime/libgte.h"

// Field actors: the ActorSystem (0x490 bytes, vtable 0x80072e4c, task priority 2,
// global 0x80075294) owning field_actors[194] (0x80075360): 0-0x81 script actors (0x80
// the save point, 0xd-0xf vehicles / helper), 0x82-0xc1 effect actors. Each frame it
// updates actors 0-0x81 (the player-controlled one through its control method, the
// camera target scrolling the background), builds every actor's primitives and links
// them into the OT sorted by depth. It also owns the two VRAM caches the sprite
// renderer packs sprites into (sprite_render.h) and the view matrix of the map's
// fixed camera.
namespace ss::game::field {

constexpr int kFieldActorCount = 0xC2;

Actor** field_actors();             // 0x80075360
Actor* field_actor(int slot);       // field_actors[slot], nullptr outside 0..0xc1
int actor_slot(const Actor* a);     // 0x80043018: index in field_actors, or -1

struct ActorSystem final : Task {
  void update() override;           // vtable +0xc: actor_sys_update

  u8 light[3] = {};                 // +0x08: colour-cycle colour (scene +0x9c entries)
  u8 cycle_timer = 0;               // +0x0b
  u8 tint[3] = {};                  // +0x0c: global actor brightness
  u8 tint_on = 0;                   // +0x0f
  psyq::SVECTOR camera_rot{};       // +0x10: scene +0xdc / +0xde / +0x30
  psyq::MATRIX view{};              // +0x18: camera rotation, translation = eye (actor_sys_project_point)
  s32 camera_yaw = 0;               // +0x38: ratan2(eye.z - at.z, eye.x - at.x) & 0xfff
  // Sprite cache A (resampled 16-bit sprites; x 0x141-0x1ff, y 1-0xff).
  s16 cache_a_x = 0, cache_a_y = 0; // +0x3c / +0x3e: shelf cursor
  s32 cache_a_size[32][2] = {};     // +0x40: secondary keys (screen width / height, 20.12)
  s16 cache_a_rect[32][4] = {};     // +0x140: x0, y0, x1, y1 (x relative to 0x140)
  uintptr_t cache_a_key[32] = {};   // +0x240: pixel data (0 = free)
  u8 cache_a_life[32] = {};         // +0x2c0: frames left before the slot may be reused
  u8 cache_a_shelf = 0;             // +0x2e0: shelf height
  u8 cache_a_next = 0;              // +0x2e1: next victim
  // Sprite cache B (raw 4/8-bit sprites; x 0x141-0x17e, y 0x101-0x1fe).
  s16 cache_b_x = 0, cache_b_y = 0; // +0x2e2 / +0x2e4
  s16 cache_b_rect[32][4] = {};     // +0x2e6
  uintptr_t cache_b_key[32] = {};   // +0x3e8: pixel data + blend
  u8 cache_b_life[32] = {};         // +0x468
  u8 cache_b_shelf = 0;             // +0x488
  u8 cache_b_next = 0;              // +0x489
  u8 cache_b_cluts = 0;             // +0x48a: 256-colour CLUTs used this frame
  s16 half_fov = 0;                 // +0x48c: ratan2(screen w / 2, H)
};

ActorSystem* actor_system();                          // 0x80075294 (null until created)
// field_create_subsystems' "new ActorSystem": allocates, constructs and registers it.
ActorSystem* actor_sys_create();
ActorSystem* actor_sys_construct(ActorSystem* s);     // 0x80043674
void actor_sys_destruct(ActorSystem* s, int flags);   // 0x80043750
void actor_sys_mark_all_dirty(ActorSystem* s);        // 0x800437e4
// fade_set_scene_brightness's actor part: the global brightness +0xc..+0xf (FadeTargets).
void actor_sys_set_brightness(int level);
void actor_sys_reset_sprite_cache(ActorSystem* s);    // 0x80043840
// 0x80043890: creates actor 'slot' (deleting the one there) of the field class with
// sprite 'model' at pos (map units), facing 'angle' degrees, on OT layer 'layer'
// (-1 = the scene default, scene +0x5a). talk_entry / pickpocket_entry are script
// entries (word indexes); 0 means none (stored as -1).
void actor_create(ActorSystem* sys, int slot, int model, const s32 pos[3], int angle, int layer, int talk_entry,
                  int pickpocket_entry, u32 flags);
// 0x800439d4: creates a screen-space actor at (x, y), or re-inits the one in the slot.
void actor_create_or_place(ActorSystem* sys, int slot, int model, int x, int y, int layer);
void actor_sys_draw(ActorSystem* s);                  // 0x80043ac4
void actor_sys_update(ActorSystem* s);                // 0x800441d0
void actor_sys_update_one(ActorSystem* sys, int slot);  // 0x80044318
void actor_sys_setup_view(ActorSystem* sys);          // 0x80044394
void actor_sys_touch_cache_a(ActorSystem* s, int slot);  // 0x80045284: keep the slot 2 more frames
void actor_sys_touch_cache_b(ActorSystem* s, int slot);  // 0x8004529c
// 0x800452b4: the world point at height y under screen point (sx, sy).
void actor_sys_project_point(ActorSystem* s, s16 sx, s16 sy, s32 y, s32 out[3]);
void actor_delete_slot(ActorSystem* sys, int slot);   // 0x8004541c (-1 = all)
void actor_delete(ActorSystem* s, Actor* a);          // 0x800454c0: also clears control / camera target
void actor_sys_save_all(ActorSystem* s);              // 0x8004557c: snapshot -> g_actor_snapshot (handoff 0xb)
void actor_sys_restore_all(ActorSystem* s);           // 0x800456f0: handoff 0xb -> actors

// The handoff-0xb snapshot built by actor_sys_save_all (0x80075724).
u8* actor_snapshot();

s16 actor_screen_x(const Actor* a);                   // actor +0x74
s16 actor_screen_y(const Actor* a);                   // actor +0x76

// Calls out of the actor code into subsystems that are not ported yet.
struct ActorHooks {
  // OT layers (0x800761a8 count, 0x800761b4 ids, 0x800761c4 tables; SceneRenderer).
  int (*ot_layer_count)() = nullptr;
};
ActorHooks& actor_hooks();

}  // namespace ss::game::field
