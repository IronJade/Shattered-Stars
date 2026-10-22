#pragma once

#include "core/types.h"
#include "game/battle/bobj.h"

// The battle's effect / HUD sprite (Sprite, 0xb8 bytes, vtable 0x8006b030) and its
// pool of 296 (g_pool_b8 0x8006fabc), with the spawn helpers and the small effect
// makers built on them (0x8002f8e8..0x80030f9c). parts/p1.md "Sprite".
namespace ss::game::battle {

struct Sprite : BObj {
  // BObj fields with a Sprite meaning: +0x52 (size[0]) half size of the flat quad,
  // +0x54 (size[1]) fade hold ticks, +0x56 (size[2]) start delay, +0x58 roll,
  // +0x68 (loop_limit) loops before the sprite dies (0 forever), +0x28.. velocity (in
  // orbit mode: yaw, pitch, radius growth; +0x34 yaw speed).
  u8* owner = nullptr;        // +0x88: set to 1 when the sprite dies (if it was nonzero)
  s16 anim_id = 0;            // +0x8c: animation + 1 (0 = free)
  s16 roll_speed = 0;         // +0x8e: +0x58 += speed * ticks / 2
  BObj* parent = nullptr;     // +0x90
  u8 attached = 0;            // +0x94: follows the parent
  u8 loops_copy = 0;          // +0x95
  u8 quad_on = 0;             // +0x96: a flat ground quad (the shadow grid) is drawn too
  u8 mode = 0;                // +0x97: 1 placed, 2 orbit, 8 bounce on the ground
  u8 freeze_class = 0;        // +0x98: 0 frozen by any battle flag, 1 unless flag 0x40, 2 only by pause
  u8 flash = 0;               // +0x99
  s8 fade_speed = 0;          // +0x9a: fade level change per tick
  u8 keep_screen = 0;         // +0x9b: screen space while animating
  u32 quad_colour = 0;        // +0x9c
  s32 accel[3] = {};          // +0xa0: acceleration / parent offset / orbit radii
  s32 pitch_speed = 0;        // +0xac: orbit pitch speed
  s32 scale_vel[2] = {};      // +0xb0: scale velocity x / y

  void update() override;     // [1] sprite_update 0x800300e0
};

void sprite_pool_create();                // battle_init_objects: 296 sprites
void sprite_pool_destroy();
Sprite* sprite_at(int index);
void sprite_pool_update();                // battle_main_loop: active sprites' vtable[1]
Sprite* sprite_find_free(int start);      // 0x8002f8e8

// Spawns (anim, loops, owner flag); null when the pool is full.
Sprite* sprite_spawn_ui(int anim, int loops, u8* owner);  // 0x8002f940: set A, pool B, never frozen
Sprite* sprite_spawn_a(int anim, int loops, u8* owner);   // 0x8002f9c0: set A, pool A
Sprite* sprite_spawn_b(int anim, int loops, u8* owner);   // 0x8002fa40: set B (debris), pool A
Sprite* sprite_spawn_c(int anim, int loops, u8* owner);   // 0x8002fac0: set C (stage sprites)
Sprite* sprite_spawn_from(int anim, int loops, const BObj* src, u8* owner);  // 0x8002fb34
inline Sprite* unit_play_anim(const BObj* u, int anim) { return sprite_spawn_from(anim, 0, u, nullptr); }  // 0x80053ee0
void sprite_reset(Sprite* s, int anim, int loops, u8* owner);               // 0x8002fbc4
void sprite_set_fade(Sprite* s, u8 level, s32 speed, s16 hold);             // 0x8002fe88
void sprite_set_pos(Sprite* s, s32 x, s32 y, s32 z);                        // 0x8002feac
void sprite_set_pos_v(Sprite* s, const s32 p[3]);                           // 0x8002fee0
void sprite_set_screen_pos(Sprite* s, s16 x, s16 y);                        // 0x8002ff2c
void sprite_attach_screen(Sprite* s, BObj* parent, s32 dx, s32 dy);         // 0x8002ff5c
void sprite_attach(Sprite* s, BObj* parent, s32 dx, s32 dy, s32 dz);        // 0x8002ff94
void sprite_place_near(Sprite* s, const BObj* ref, s32 dx, s32 dy, s32 dz); // 0x8002ffc8
void sprite_set_quad(Sprite* s, s16 half, u32 colour = 0xFFFFFFFF);         // 0x80030070 / 0x80030088
void sprite_set_orbit(Sprite* s, s32 yaw, s32 pitch, s32 rx, s32 ry, s32 rz, s32 yaw_speed, s32 pitch_speed,
                      s32 growth);                                          // 0x8003009c
void sprite_free(Sprite* s);                                                // 0x80030870

// Effect makers.
void effect_rise_icon(const BObj* u, int kind);          // 0x800308c0 (0x800308a0: kind 0)
void effect_skill_popup(const BObj* u, int skill);       // 0x80030950
void effect_debris_burst(const s32 pos[3], const s32 spread[3], int count, int kind, int size, s32 scale,
                         u8 clut_offset);                // 0x80030a0c
void effect_ring(const s32 pos[3], int size);            // 0x80030cb8
Sprite* effect_dust_puff(const s32 pos[3], s32 speed);   // 0x80030d60
void effect_hit_flash(const s32 pos[3], int sparks);     // 0x80030e40

// The boot executable's ranged draws (rng_between 0x800103d4 and its wrappers).
s32 rng_between(s32 a, s32 b);                           // 0x800103d4
inline s32 rng_spread(s32 n) { return rng_between(-n, n); }                      // 0x80010460
inline s32 rng_spread_fixed(s32 n) { return rng_between(-n << 12, n << 12); }    // 0x80010484
inline s32 rng_between_fixed(s32 a, s32 b) { return rng_between(a << 12, b << 12); }  // 0x800104b0

}  // namespace ss::game::battle
