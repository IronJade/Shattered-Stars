#pragma once

#include "core/types.h"

// The Particle pool (Pool48: 200 x 0x48, no vtable, g_pool_48 0x8006fac8): lines, rings and
// triangles drawn into pool A. battle_main_loop runs particle_update for kind != 0 after the
// sprites. Kinds: 1 homing LINE_G2 streak, 2 gravity spark line, 3 flat ring of additive
// POLY_G4, 4 orbiting shard, 5 falling confetti. analysis/battle_catalog/parts/p4.md.
namespace ss::game::battle {

struct Particle {
  u16 kind = 0;          // +0x00 (0 free)
  u8 in_pause = 0;       // +0x02: spawned during the 0x40 pause; keeps running
  s32 pos[4] = {};       // +0x04
  s32 vel[4] = {};       // +0x14
  s32 p24[4] = {};       // +0x24: target, or colours / yaw by kind
  s32 p34 = 0;           // +0x34
  s32 p38 = 0;           // +0x38
  s32 p3c = 0;           // +0x3c
  s32 p40 = 0;           // +0x40
  s32 p44 = 0;           // +0x44
};

void particle_pool_create();
void particle_pool_destroy();
void particle_pool_update();      // battle_main_loop: particle_update 0x8005e1b0 per active one
Particle* particle_alloc(int kind);  // 0x8005dccc
void particle_free(Particle* p);     // 0x8005e250

// 0x8005dd50: a streak flying from pos to target at 'speed' (+ accel a tick), 'length'
// long, colours head / tail.
Particle* particle_spawn_homing_line(const s32 pos[4], const s32 target[4], u32 head, u32 tail, s32 length,
                                     s32 speed, s32 accel);
// 0x8005de28: a streak along its velocity with gravity for 'life' ticks.
Particle* particle_spawn_spark_line(const s32 pos[4], const s32 vel[4], u32 head, u32 tail, s32 length, s32 life);
// 0x8005def0: a flat ring of 'segments' quads, 'width' wide, growing 'growth' a tick for
// 'life' ticks from 'radius', colours (radius side / outer side), yaw.
Particle* particle_spawn_ring(const s32 pos[4], s32 segments, s32 width, s32 growth, s32 life, u32 colour_out,
                              u32 colour_in, s32 radius, s32 yaw);
Particle* particle_spawn_shard(const s32 pos[4], s32 a34, s32 a38, s32 a3c, s32 a24, s32 a2c, s32 a40, s32 a44);  // 0x8005e018
Particle* particle_spawn_confetti(const s32 pos[4], const s32 vel[4], u32 colour, s32 size, s32 life);  // 0x8005e0e4

}  // namespace ss::game::battle
