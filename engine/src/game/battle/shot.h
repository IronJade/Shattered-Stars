#pragma once

#include "core/types.h"
#include "game/battle/unit.h"

// The Shot: the battle's generic effect / projectile actor (0x13c bytes, vtable 0x8006ce90,
// a Collider). 160 of them in g_pool_13c (0x8006fac4); battle_main_loop runs vtable[1]
// shot_update for every Shot whose kind is not 0, before the sprites. The kind picks the
// per-frame handler (g_shot_kind_table 0x8006fea8, 0x34 entries; a switch in the port).
// analysis/battle_catalog/parts/p4.md.
namespace ss::game::battle {

struct Shot;
using ShotFn = void (*)(Shot*);

struct Shot : Collider {
  u16 kind = 0;              // +0x108: 0 free
  s16 power = 0;             // +0x10a: power / element by kind
  s16 aux = 0;               // +0x10c: power step, sfx, angle or height by kind
  u8 command = 0;            // +0x10e: 1 end, 2 power up, 3 reflect; 0xa5 runs while suppressed
  Unit* owner = nullptr;     // +0x110
  Unit* target = nullptr;    // +0x114
  s32 point[3] = {};         // +0x118: target point / centre, or three parameters
  void* trail = nullptr;     // +0x124: ribbon trail (kind 0x16)
  s32 p128 = 0;              // +0x128: timer / variant
  s32 p12c = 0;              // +0x12c: the ending fade counter (shot_fade_out)
  s32 p130 = 0;              // +0x130
  s32 p134 = 0;              // +0x134
  s32 p138 = 0;              // +0x138: flags (kind 0x16) or a parameter
  ShotFn handler = nullptr;  // +0x138 of the callback kinds 0x21 / 0x33
  BObj* spark_owner = nullptr;  // +0x110 of kind 0x11 (any object to follow)
  // (Module code keeps state in fields of its own; they are named where it is ported.)
  s32 user[4] = {};

  void update() override;    // [1] shot_update 0x80054560
};

void shot_pool_create();     // battle_init_objects: 160 x 0x13c, kind 0
void shot_pool_destroy();
void shot_pool_update();     // battle_main_loop: shot_update for kind != 0
Shot* shot_at(int index);

Shot* shot_spawn(int kind);              // 0x800543bc: the first free one (reset), or null
int shot_count_free();                   // 0x80054478
void shot_reset(Shot* s);                // 0x800544b8
void shot_kill(Shot* s);                 // 0x80054624
void shot_use_anims_f928(BObj* o);       // 0x80054698 (sprite set A)
void shot_use_anims_f920(BObj* o);       // 0x800546c4 (the effect bank)
Sprite* shot_spawn_sprite(Shot* s, int anim);            // 0x800545c8
Sprite* shot_spawn_sprite_at(Shot* s, int anim, int arg);  // 0x800545f4
// 0x800546f0: the owner's physical hit on whatever s's attack boxes meet (vs_party: the
// party). Returns hits + 1, or 0.
int shot_hit_melee(Shot* s, int power, int vs_party);
// 0x80055360: s at u's position + (facing-signed dx, -dy, dz) (units), with u's facing.
void shot_place_at_unit(Shot* s, const BObj* u, s32 dx, s32 dy, s32 dz);
// 0x80055434: project, animate and draw (g_battle_flags) and the ground shadow of the
// cel's half width * size; returns the frame (0xffff at the loop limit).
int shot_draw_and_box(Shot* s, int size, u32 shadow_colour);
// 0x800554c0: as shot_draw_and_box with the shadow shrinking with the height (min 0x10).
int shot_draw_and_box_ground(Shot* s, int size, u32 shadow_colour);
// 0x80055c60: once the battle passes state 2 (or +0x12c was set) fades the Shot over 24
// ticks and kills it; returns 1 when it died.
int shot_fade_out(Shot* s);
Shot* shot_spawn_callback(ShotFn fn);         // 0x8005b4f0: kind 0x33
Shot* shot_spawn_callback_always(ShotFn fn);  // 0x8005b4b0: kind 0x21 (runs while suppressed)
int shot_kind_absent(int kind);               // 0x800874d4: 1 when no Shot has the kind
// 0x80063824 / 0x800639ac: a kind-0x11 Shot (a ring of textured quads) at pos / following
// owner: +0x128 count, +0x118 / +0x11c / +0x120 / +0x12c / +0x130 / +0x134 / +0x138
// parameters, +0x10a flag.
Shot* effect_spawn_sparks_at(const s32 pos[3], s32 count, s32 p118, s32 p11c, s32 p130, s32 p134, s32 p12c,
                             s32 p120, s32 p138, s16 power);
Shot* effect_spawn_sparks_on(BObj* owner, s32 count, s32 p118, s32 p11c, s32 p130, s32 p134, s32 p12c, s32 p120,
                             s32 p138, s16 power);
int shot_kind21_absent(ShotFn fn);            // 0x80087510: 1 when no kind-0x21 Shot runs fn

}  // namespace ss::game::battle
