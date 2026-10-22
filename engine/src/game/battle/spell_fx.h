#pragma once

#include "core/types.h"
#include "game/battle/battle.h"

// Resident effect helpers the spells (and the monsters' spells) use that nothing else
// ported needed so far.
namespace ss::game::battle {

// 0x8003c3b0 screen_fade_create: a full-screen tint (0x1c-byte task at priority 0, its own
// one-entry layer in front of everything but the HUD): rgb fades by step per tick (after
// 'hold' ticks) and the task ends at black; a negative step first fades up to white, then
// turns positive. tpage: the blend mode. Returns the task (owned by the task list).
Task* screen_fade_create(u32 rgb, int tpage, int step, int hold);
void spell_fx_reset();  // the battle's end: live fades are freed
// 0x80072618 (#1984.0): a random point of the arena (its bounds divided by 'shrink') on the
// ground.
void stage_random_point(s32 out[4], int shrink);
// 0x8007d018 / 0x8007d0f8 (#1984.0): a model through model_render into pool A at the
// current matrix, without / with back-face culling (no fog).
void model_draw(const u8* model, u16 clut, u16 tpage, s32 ot_bias);
void model_draw_culled(const u8* model, u16 clut, u16 tpage, s32 ot_bias);
// 0x8007bbd8 (#1984.0): a ten-pointed star at the current matrix: points alternately r_a /
// r_b out in x / y, a tip h up and down in z; additive flat triangles (rgb_a even points,
// rgb_b odd).
void draw_star(s32 r_a, s32 r_b, s16 h, u32 rgb_a, u32 rgb_b);
// 0x8007c300 (#1984.0): a gouraud quad of four SVECTORs (x, y, z, pad) at the current
// matrix into pool A (colour 0 carries the code: 0x3a semi-transparent adds a DR_MODE
// tpage 0x20); 'flat' puts it at the back of its depth band's front (OT index 0x610).
void draw_quad_g4(const s16 pts[4][4], const u32 colours[4], int flat);

}  // namespace ss::game::battle
