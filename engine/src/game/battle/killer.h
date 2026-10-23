#pragma once

#include <memory>

#include "core/types.h"
#include "game/battle/bobj.h"
#include "game/battle/unit.h"

// Killer-move support the character modules call: the afterimage trail (#1984.0
// 0x80075080-0x800751fc), the PartyUnit helpers (0x8003a88c-0x8003ab68) and the MP cost
// (unit_mp_cost 0x80053f6c).
namespace ss::game::battle {

struct Shot;

// 0x30-byte task (vtable 0x80080fdc, priority 3): up to 8 tinted copies of a moving
// object, fading 4 a tick (or 0x80 / count a tick once ending), drawn every frame.
struct Afterimage : Task {
  u8 count = 0;              // +0x08 (max 8)
  s8 head = 0;               // +0x09: next copy slot (counts down)
  u8 in_pause = 0;           // +0x0a: created during the 0x40 pause; keeps running
  u8 blend = 0;              // +0x0b: 0 opaque, 1 blend 2 (additive), 2 0x82
  u8 ending = 0;             // +0x0c: the owner is done; fade out and delete
  u8 next_kind = 0;          // +0x0d: the next copy grows (1) or spins (2) / both (3)
  Afterimage** back = nullptr;  // +0x10: cleared when the task goes
  const BObj* source = nullptr; // +0x14
  u8 colour[3] = {};         // +0x18
  u8 kinds[8] = {};          // +0x1c: 0 dead, 1 plain, 2 grows, 3 spins, 4 both
  u8 alpha[8] = {};          // +0x24
  std::unique_ptr<BObj[]> copies;  // +0x2c

  Afterimage(const BObj* src, int n, u32 rgb, u8 blend_mode);  // afterimage_create 0x80075080
  ~Afterimage() override;                                      // afterimage_destroy 0x8007518c
  void update() override;                                      // afterimage_update 0x800751fc
};
Afterimage* afterimage_create(const BObj* src, int n, u32 rgb, u8 blend_mode);

// 0x9c-byte task (vtable 0x80080fcc, priority 3): one semi-transparent copy of an object
// fading 4 a tick (the counter / chain flash).
struct Ghost : Task {
  Ghost** back = nullptr;    // +0x08
  u8 colour[3] = {};         // +0x0c
  s32 alpha = 0x80;          // +0x10
  BObj copy;                 // +0x14
  Ghost(const BObj* src, u32 rgb);   // ghost_create 0x800756e0
  ~Ghost() override;                 // ghost_destroy 0x800757b4
  void update() override;            // ghost_update 0x80075818
};
Ghost* ghost_create(const BObj* src, u32 rgb);

// 0x2a0-byte task (vtable 0x80080fbc, priority 3): a camera-facing ribbon through the last
// 'length' positions of an object (history of 32), fading 0x100 / length a tick; additive.
struct RibbonTrail : Task {
  u8 length = 0;              // +0x08
  s8 head = 0;                // +0x09: next history slot (counts down)
  u8 tpage = 0x20;            // +0x0a: the DR_MODE's semi-transparency bits
  u8 in_pause = 0;            // +0x0b: created during the 0x40 pause; keeps running
  u8 stop = 0;                // +0x0c: the owner is done; fade out and delete
  u8 no_lift = 0;             // +0x0d: follow the position itself (else 2 * height above it)
  RibbonTrail** back = nullptr;  // +0x10: cleared when the task goes
  u32 rgb_a = 0;              // +0x14: trailing edge colour
  u32 rgb_b = 0;              // +0x18: leading edge colour
  const BObj* source = nullptr;  // +0x1c
  s32 alpha[32] = {};         // +0x20
  s32 hist[32][4] = {};       // +0xa0: positions (x, y, z, w)

  RibbonTrail(const BObj* src, u8 len, u32 a, u32 b);  // ribbon_trail_create 0x80075944
  ~RibbonTrail() override;                             // ribbon_trail_destroy 0x80075a10
  void update() override;                              // ribbon_trail_update 0x80075a74
};
RibbonTrail* ribbon_trail_create(const BObj* src, u8 len, u32 rgb_a, u32 rgb_b);
// battle_free_all: deletes the Afterimage / Ghost / RibbonTrail tasks still running (the
// console drops the whole battle heap; the port frees them).
void effect_tasks_free_all();

// 0x8003a88c: animates (slot 26); at the end, or late in the end countdown, ends the move
// (action kind 0x12 + slot 12), releases the bound effects and the camera look offset;
// returns 1 when finished.
int pc_attack_anim_step(PartyUnit* u);
void pc_bind_shot(PartyUnit* u, Shot* s, s16 power);        // 0x8003a99c
int pc_move_charge_level(const PartyUnit* u, int move);     // 0x8003a9d0
void pc_camera_frame_target(PartyUnit* u, int speed);       // 0x8003aa30
void pc_camera_behind_start(PartyUnit* u);                  // 0x80037c2c: camera mode 15, +0x132 = 2
// 0x8003ab68: u's attack boxes against the enemies (knockback 9999); hits + 1, or 0.
int pc_attack_hit(PartyUnit* u, int power, u16 elements);
int unit_mp_cost(Unit* u, int cost);                        // 0x80053f6c
// One step of a jump arc (gravity a tick) for a member in the air (each module has its
// own copy: Claude 0x801f0640, Ernest 0x801ef724): 1 bumped into something (airborne 2),
// 2 touched the ground, 0 still flying (or not jumping, or paused).
int pc_jump_step(PartyUnit* u, s32 gravity);
// 0x8004c940: queues killer move 'which' (0 / 1) for the controlled member against the
// target enemy; 1 when queued (else the buzzer and the X marks).
int command_killer_move(int which);

}  // namespace ss::game::battle
