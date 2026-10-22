#pragma once

#include "core/types.h"

// 3-D effect drawers of #1984 block 0 shared by the Shot kinds and the party-character
// modules. They draw into pool A with the GTE matrices obj_set_matrix loaded.
namespace ss::game::battle {

// The parameter block draw_vortex reads (the modules build it in the scratchpad at
// 0x1f800000).
struct VortexParams {
  s32 radius = 0;      // +0x00: ring radius profile (sqrt of ring / rings * radius)
  s32 base = 0;        // +0x04: x of ring 0 (the funnel's axis is x)
  s32 width = 0;       // +0x08: radius scale
  s32 step = 0;        // +0x0c: x step per ring
  s32 rings = 0;       // +0x10
  s32 segments = 0;    // +0x14
  u32 colour_a = 0;    // +0x18
  u32 colour_b = 0;    // +0x1c
  s32 phase_span = 0;  // +0x20: colour phase across the rings
  s32 phase = 0;       // +0x24: colour phase (0..0x1fff, folded at 0x1000)
  s32 twist_freq = 0;  // +0x28: sine twist of the phase around a ring
  s32 twist_amp = 0;   // +0x2c
  s32 blend = 0;       // +0x30: 0 opaque; else semi-transparency mode blend - 1
  s32 ot_bias = 0;     // +0x34
};

// 0x8007deb4: a funnel of rings x segments gouraud quads, fading toward the last ring.
void draw_vortex(const VortexParams& p);

// 0x8007c520: a lit UV sphere of rings x segs POLY_G4 (NCCS / NCCT with the light
// matrices built by lights_build*: normals are the unit sphere), radius 'radius' (y scaled
// by y_scale / 0x1000), colour scale_colour(rgb, level); only the lower half of the rings
// (y below the centre) with 'hemisphere' 1; blend != 0 adds a DR_MODE (mode blend - 1).
// Uses the GTE rotation / translation the caller set.
void draw_sphere(s32 rings, s32 segs, s32 radius, u32 rgb, s32 level, s32 hemisphere, s32 blend, s32 y_scale);
// 0x80073bb8: the screen x / y of a world point (20.12) through the render view. Leaves
// the GTE translation at the rotated offset.
void project_to_screen(const s32 world[3], s16 out[2]);
// 0x80073cd0: a flat disc (r_inner 0: POLY_G3 fan, code 0x32) or ring (POLY_G4, 0x3a) of
// r_outer / 4 + 8 (at most 64) segments around pos on screen, in 'bands' bands: band b
// ends at r_inner + (r_outer - r_inner) * pcts[b] / 100 and takes colours[2b] inside,
// colours[2b + 1] outside; additive, at OT entry 'ot' of pool A.
void draw_flare_ring(const s32 pos[3], s32 r_inner, s32 r_outer, int bands, const u32* colours, const s32* pcts, int ot);
// 0x800741b0: a flat additive polygon fan (POLY_F3 0x22) of 'sides' sides, rotated by the
// camera roll.
void draw_flare_polygon(const s32 pos[3], int sides, s32 radius, u32 rgb, int ot);
// 0x8007443c: the stage's lens flare (11 elements; the first two sizes and the first
// intensity follow 'size').
void lens_flare_draw(const s32 sun[3], s32 dist, u32 rgb, s32 size, s32 threshold);
// 0x800744cc: the small 4-element flare the party-character modules use.
void lens_flare_draw_small(const s32 src[3], s32 dist, u32 rgb, s32 size, s32 threshold);
// 0x8003c7a4 colour_modulate: per channel a * b >> 7, clamped (0x80 = unchanged).
u32 colour_modulate(u32 a, u32 b);

}  // namespace ss::game::battle
