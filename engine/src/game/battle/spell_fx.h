#pragma once

#include <vector>

#include "core/types.h"
#include "game/battle/battle.h"
#include "game/battle/shot.h"
#include "runtime/libgte.h"

// Resident effect helpers the spells (and the monsters' spells) use that nothing else
// ported needed so far.
namespace ss::game::battle {

struct Sprite;

// 0x8003c3b0 screen_fade_create: a full-screen tint (0x1c-byte task at priority 0, its own
// one-entry layer in front of everything but the HUD): rgb fades by step per tick (after
// 'hold' ticks) and the task ends at black; a negative step first fades up to white, then
// turns positive. tpage: the blend mode. Returns the task (owned by the task list).
Task* screen_fade_create(u32 rgb, int tpage, int step, int hold);
void spell_fx_reset();  // the battle's end: live fades are freed
// 0x80072724 (#1984.0): stage_colour_set(v, v, v).
inline void stage_colour_set_grey(s32 v) {
  stage_colour_set(static_cast<s16>(v), static_cast<s16>(v), static_cast<s16>(v));
}
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

// 0x80063a70: a kind-0x12 Shot (a light streak) at pos homing on target: colours +0x12c
// head / +0x130 tail, length +0x138, speed +0x134 growing by +0x128, +0x10c the puff at the
// end (1 a stage sprite, 2 / 3 effect sprites 2 / 5).
Shot* effect_spawn_streak_at(const s32 pos[3], const s32 target[3], u32 rgb_head, u32 rgb_tail, s32 length,
                             s32 speed, s32 accel, s16 puff);
// 0x80063b48: a kind-0x31 Shot (a vortex mesh: draw_vortex, stage grey fading in the last 32
// ticks) at pos: +0x128 rings, +0x12c segments, +0x130 radius growth, +0x134 life, +0x138
// width, vel x the step, +0x118 / +0x11c colours, +0x10a 1: held in the 0x40 pause.
Shot* effect_spawn_vortex_at(const s32 pos[3], s32 rings, s32 segments, s32 grow, s32 life, u32 rgb_a, u32 rgb_b,
                          s32 width, s32 step, s16 flag);
// 0x8007cc5c (#1984.0) draw_disc: a flat fan of POLY_G3 in the x-z plane round the current GTE
// origin (rgb_centre at the centre, rgb_edge on the rim; blend != 0: semi-transparent mode
// blend - 1), OT 0x616 + ot - otz / 8 of pool A.
void draw_disc(s32 segments, s32 radius, u32 rgb_centre, u32 rgb_edge, s32 blend, s32 ot);
// 0x8007d9e0 (#1984.0) draw_wave_grid: an n x n grid of screen-space POLY_FT4 over rect with the
// uv rectangle spread over it, rows / columns shifted by the sine waves wave_x / wave_y.
void draw_wave_grid(const s32 uv[4], const s32 rect[4], const s32 wave_x[3], const s32 wave_y[3], u16 clut,
                    u16 tpage, s32 ot, u32 code_rgb, s32 n);
// 0x8006fe04 g_stage_height_fn: set by Earthquake / Explode while the ground moves;
// unit_draw_model lifts grounded units (not flying, not airborne 1) by fn(x >> 12, 0, z >> 12)
// (20.12) for the draw.
extern s32 (*g_stage_height_fn)(s32 x, s32 y, s32 z);
// 0x80061694 model_find_vertices_near's 12-byte entry: a vertex of a battle model (its word
// offset in the model) and its xz distance to each point.
struct NearVertex {
  s32 offset = 0;
  s16 dist[4] = {};
};
// 0x800627c0 sv_distance_xz: sqrt(dx^2 + dz^2) of a model's inline SVECTOR and a point (GTE
// SQR: 16-bit inputs).
s32 sv_distance_xz(const u8* a, const psyq::SVECTOR& b);
// 0x80061694 model_find_vertices_near: every vertex of the battle model's primitive sections
// whose xz distance to points[0] is at most 12000, with its distance to each point (out
// cleared first; the console's 0xf000-byte buffer is unbounded). Returns the count.
int model_find_vertices_near(std::vector<NearVertex>& out, const std::vector<u8>& model, const psyq::SVECTOR* points,
                             int npoints);
// (port) A visual-only random source for fixes that add random draws the original never made
// (original_bugs.md S16): never the game's RNG, so the game's sequence stays the original's.
// visual_rng_between_fixed(a, b): a << 12 + a draw in [0, (b - a) << 12), as rng_between_fixed.
u32 visual_rng_next();
s32 visual_rng_between_fixed(s32 a, s32 b);
// (port) Held fixes (original_bugs.md "held"): a fix that would change play stays off; the
// debug variable SS_HELD_FIXES="S17,S19" (or "all") turns them on to measure them.
bool held_fix_enabled(const char* id);
// (port) Visual-only sprites: drawn like any other, but no game logic counts them (a cast's
// spellcast_effects_gone skips them), for fixes that add sprites the console never had.
Sprite* visual_sprite_spawn(BObj* src, int anim);
bool sprite_is_visual_only(const Sprite* s);
// 0x80080c7c (#1984.0) voice_thank_healer: a healed member may thank the healer (an RNG draw
// when it likes the healer enough; spell_shots.cpp).
void voice_thank_healer(Unit* healer, Unit* target);
}  // namespace ss::game::battle
