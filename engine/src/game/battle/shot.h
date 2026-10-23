#pragma once

#include "core/types.h"
#include "game/battle/unit.h"
#include "game/battle/unit_move.h"

// The Shot: the battle's generic effect / projectile actor (0x13c bytes, vtable 0x8006ce90,
// a Collider). 160 of them in g_pool_13c (0x8006fac4); battle_main_loop runs vtable[1]
// shot_update for every Shot whose kind is not 0, before the sprites. The kind picks the
// per-frame handler (g_shot_kind_table 0x8006fea8, 0x34 entries; a switch in the port).
// analysis/battle_catalog/parts/p4.md.
namespace ss::game::battle {

struct Shot;
struct RibbonTrail;
using ShotFn = void (*)(Shot*);

struct Shot : Collider {
  u16 kind = 0;              // +0x108: 0 free
  s16 power = 0;             // +0x10a: power / element by kind
  s16 aux = 0;               // +0x10c: power step, sfx, angle or height by kind
  u8 command = 0;            // +0x10e: 1 end, 2 power up, 3 reflect; 0xa5 runs while suppressed
  Unit* owner = nullptr;     // +0x110
  Unit* target = nullptr;    // +0x114
  s32 point[3] = {};         // +0x118: target point / centre, or three parameters
  RibbonTrail* trail = nullptr;  // +0x124: ribbon trail (kind 0x16)
  u32 trail_rgb = 0;         // +0x124 before kind 0x16's first frame: the trail colour
  s32 p128 = 0;              // +0x128: timer / variant
  s32 p12c = 0;              // +0x12c: the ending fade counter (shot_fade_out)
  s32 p130 = 0;              // +0x130
  s32 p134 = 0;              // +0x134
  s32 p138 = 0;              // +0x138: flags (kind 0x16) or a parameter
  ShotFn handler = nullptr;  // +0x138 of the callback kinds 0x21 / 0x33
  BObj* spark_owner = nullptr;  // +0x110 of kind 0x11 (any object to follow)
  const Unit* last_hit = nullptr;  // +0x130 of kind 2 when it holds a unit (p130 is then 2)
  // (Module code keeps state in fields of its own; they are named where it is ported.)
  s32 user[4] = {};
  // (Port) a draw-only offset (kind 0x16; original_bugs.md E9): the Shot is drawn at pos +
  // draw_offset * (draw_ticks - draw_age) / draw_ticks, so it comes out of another point
  // and joins its own path by tick draw_ticks, while it moves and hits as on the console.
  s32 draw_offset[3] = {};
  s32 draw_ticks = 0;
  s32 draw_age = 0;

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
// 0x80058908: GTE rotation = the render view, translation = the view of the Shot's
// position (its owner's, 0x10000 higher, when +0x110 is set).
void shot_set_gte_origin(const Shot* s);
// 0x80063c1c: a kind-0x32 light streak (a textured quad +0x130 wide, +0x138 long, CLUT
// 0x7d40 + +0x12c) from pos homing on target at speed +0x134 growing by +0x128 a tick.
Shot* effect_spawn_beam_at(const s32 pos[3], const s32 target[3], s32 p12c, s32 p138, s32 p130, s32 p134, s32 p128,
                           s16 power);
// 0x8006375c: a kind-0x10 Shot (an expanding lit sphere: lat x lon segments, radius +0x138
// growing by +0x130, life +0x134, colour +0x118, scale +0x10c, +0x10a flag) at pos;
// 0x80063718 passes scale 0x1000.
Shot* effect_spawn_sphere_at(const s32 pos[3], s32 lat, s32 lon, s32 grow, s32 life, u32 rgb, s32 radius, s16 power,
                             s16 scale);
inline Shot* effect_spawn_sphere_at_default(const s32 pos[3], s32 lat, s32 lon, s32 grow, s32 life, u32 rgb,
                                            s32 radius, s16 power) {
  return effect_spawn_sphere_at(pos, lat, lon, grow, life, rgb, radius, power, 0x1000);
}
// 0x800548c4: spell damage from s (its owner +0x110) to n targets (hit points[]): power
// raised by the owner's statuses, per target halving, MP burns, unit_calc_damage (magic),
// misses / guts / absorb / reflect / redirect / barrier, the popup and HP (or MP when the
// owner's +0x134) damage, an optional spark; sfx on the first target. Returns 1 when a
// target took no damage. A redirect replaces targets[i].
int shot_apply_damage(Shot* s, int n, Unit** targets, const Vec4* points, int power, int sfx, u16 element, int spark,
                      int knockback);
// 0x800547d4: shot_apply_damage on the units the Shot's attack boxes meet (vs_party: the
// party), +0x6d set; returns its result + 1, or 0 when it met nothing (spells).
int shot_hit_magic(Shot* s, int power, int vs_party, int sfx, u16 element, int spark, int knockback);

// The projectile descriptor (0x74 bytes, scratchpad 0x1f800000 s_shot_desc) that
// shot_spawn_projectile turns into a kind-0x16 Shot.
struct ShotDesc {
  Unit* target = nullptr;  // +0x00: aims at the target's centre (null: at point)
  s32 point[3] = {};       // +0x04
  s32 w10 = 0;             // +0x10
  s32 angle = 0;           // +0x14: yaw offset from the target direction (4096 = 360)
  s32 speed = 0;           // +0x18
  s32 anim = 0;            // +0x1c
  s32 trail_rgb = 0;       // +0x20 (Shot +0x124): ribbon trail colour (0: none; length 12)
  s32 spin = 0;            // +0x24 (Shot +0x130 low half): trail sprites' fade speed; flag 1
  s32 trail_anim = 0;      // +0x28 (Shot +0x134 when spin is set): trail sprite anim - 1
  u32 flags = 0;           // +0x2c (Shot +0x138; see shot_k16_projectile)
  s32 trail_bits[4] = {};  // +0x30..+0x3c: impact effect kind / colour / size a / size b (flags bits 3, 6, 10, 12)
  s32 power = 0;           // +0x40 (Shot +0x118)
  s32 hit_effect = 0;      // +0x44 (Shot +0x11c low half): hit sprite anim
  s32 hit_effect_hi = 0;   // +0x48 (Shot +0x11c high half): kind of a Shot spawned on the hit
  s32 hits = 0;            // +0x4c (Shot +0x120 low half): hit sprite count
  s32 hit_bits[3] = {};    // +0x50..+0x58: flags bits 14 (scatter) / 16 (sprite set, scale) / 18
  s32 hits_hi = 0;         // +0x5c (Shot +0x120 high half): hit sprites' CLUT offset
  s32 box = 0;             // +0x60 (Shot +0x56)
  s32 sfx = 0;             // +0x64 (Shot +0x10c)
  s32 life = 0;            // +0x68 (Shot +0x130 high half, flag 0x2000000)
  s32 airborne = 0;        // +0x6c (Shot +0x6b): CLUT offset of a glow sprite each frame
  s32 element = 0;         // +0x70 (Shot +0x10a; a nonzero high half (+0x72) sets +0x6b bit 7: pierces)
};
// 0x8005a0cc: clears the descriptor and sets the defaults (anim and trail anim, box 0x60,
// sfx 0x4a, flags 0x1000000, power 0x100, one hit, +0x58 = 1).
ShotDesc* shot_desc_init(int anim);
// 0x8005a158: a kind-0x16 projectile from 'from' (owner, descriptor, sprite source).
Shot* shot_spawn_projectile(Unit* owner, const s32 from[3], const ShotDesc* d, const BObj* anim_src);

}  // namespace ss::game::battle
