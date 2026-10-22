#pragma once

#include <array>
#include <memory>

#include "core/types.h"
#include "game/field/tasks.h"
#include "runtime/libgpu.h"
#include "runtime/libgte.h"

// The field's visual effects (code overlay #3101 at 0x8007e000; catalog module
// field_ovl_c1d), used by map scripts in story scenes:
//
// - GlowFx (0x8007e000-0x8007e10c, script ext 0x79): a 0x1c-byte task at priority 1
//   (vtable 0x8008ebd0, global 0x800752dc) that draws an additive ring of 24 POLY_G4
//   round the screen edge, black inside, fading in to a colour at the outer edge.
// - Particles (0x80089b68-0x8008d928): a pool of 0x400 0x50-byte entries (0x800761ac,
//   count 0x800761a8) with nine behaviours, picked by type through the pointer-to-member
//   table 0x8008ed10: 1 streak, 2 spark, 3 ring, 4 spinner, 5 lit orb, 6 beam,
//   7 triangle, 8 burst, 9 textured ring. They draw with the GTE (inline rtps / rtpt /
//   avsz / gpf12 / ncct) into four private, double-buffered OT layers (0x800761b4 ids,
//   0x800761c4 tables) that the field's SceneRenderer flips and clears each frame and
//   actor_sys_draw splices into the screen OT at layer id + 0x11 (scene_renderer.h).
//
// Coordinates are world positions in 20.12 map units, drawn through the camera block's
// view rotation (0x80075b44) and eye (0x80075a84) with the map's projection (BG pixels),
// then moved to the screen by the BG view origin (0x800761b0).
namespace ss::game::field {

// ---- GlowFx ------------------------------------------------------------------------

struct GlowFx final : Task {
  void update() override;  // vtable +0xc: glow_draw

  s32 frames = 1;          // +0x08: fade-in length (at least 1)
  s32 timer = 0;           // +0x0c: frames shown, capped at 'frames'
  s32 rgb[3] = {};         // +0x10..+0x18: outer colour once faded in
};

GlowFx* glow_create_default(GlowFx* self);                          // 0x8007e000 (no caller in #3101)
GlowFx* glow_create(GlowFx* self, int r, int g, int b, int frames);  // 0x8007e058
void glow_destroy(GlowFx* self, int flags);                         // 0x8007e0b8: flags & 1 deletes
void glow_draw(GlowFx* self);                                       // 0x8007e10c
GlowFx* field_glow();                                               // 0x800752dc
// Script ext 0x79: new 0x1c + glow_create, stored in 0x800752dc. The previous glow is
// not destroyed: it keeps drawing (until the field program's task list is rebuilt).
GlowFx* glow_spawn(int r, int g, int b, int frames);
// field_load_map / field_unload_map: glow_destroy(0x800752dc, 3), pointer cleared.
void glow_destroy_current();
// field_destroy_subsystems: the current glow and any orphaned by a second ext 0x79.
void glow_destroy_all();

// ---- OT layers and particle pool ---------------------------------------------------

constexpr int kFxLayerEntries = 0x830;  // per half (0x20c0 bytes on the console)
constexpr int kFxPoolSize = 0x400;

// A private ordering table (0x4184 bytes on the console: byte 0 current half, byte 1
// dirty, then two 0x830-entry OTs). Entries link forward like the screen OT; drawers put
// primitives at index 0x820 - depth.
struct OtLayer {
  u8 half = 0;   // +0
  u8 dirty = 0;  // +1: something drew into it this frame (it gets spliced)
  std::array<psyq::P_TAG, kFxLayerEntries> ot[2];
  psyq::P_TAG* current() { return ot[half].data(); }
};

// One pool entry (0x50 bytes). Behaviours reuse the parameter words differently; the
// per-type meaning is given at each spawner.
struct Particle {
  u16 type = 0;              // +0x00: 0 free, 1..9
  s16 flag = 0;              // +0x02: triangle: fade with the lifetime
  OtLayer* layer = nullptr;  // +0x04 (null: never updated)
  s32 pos[4] = {};           // +0x08: VECTOR, 20.12
  s32 vel[4] = {};           // +0x18: VECTOR (streak / spark velocity; other uses below)
  s32 tgt[4] = {};           // +0x28: VECTOR (streak target; other uses below)
  s32 p38 = 0, p3c = 0, p40 = 0, p44 = 0, p48 = 0;  // +0x38..+0x48
  s16 p4c = 0;               // +0x4c: textured ring angle
};

struct FxState {
  s32 count = 0;                          // 0x800761a8: kFxPoolSize once the pool exists (0 = none)
  std::unique_ptr<Particle[]> pool;       // 0x800761ac
  s16 origin[2] = {};                     // 0x800761b0 / 0x800761b2: BG view origin this frame
  u32 layer_id[4] = {};                   // 0x800761b4
  std::unique_ptr<OtLayer> layer[4];      // 0x800761c4
};
FxState& fx_state();

void fx_pool_create();                     // 0x80089b68
void fx_particle_clear(Particle* p);       // 0x80089c3c
Particle* fx_particle_alloc(u16 type);     // 0x80089c6c
OtLayer* fx_layer_get(u8 id);              // 0x80089d34
int fx_layer_alloc(u8 id);                 // 0x80089dd8
void fx_particle_reset(Particle* p);       // 0x8008a8b4
void fx_particle_update(Particle* p);      // 0x8008a9cc
void fx_particle_free(Particle* p);        // 0x8008aa5c
void fx_set_local_transform(const Particle* p);  // 0x8008aa70
void fx_particle_integrate(Particle* p);   // 0x8008ab74
void fx_draw_line(Particle* p, const psyq::SVECTOR* end);  // 0x8008abd8
u32 fx_scale_colour(u32 rgb, s32 t);       // 0x8008bce8: rgb * t / 256 through gpf12
// 0x8008bd34: three vertices (local) -> POLY_G3 with rgb[0..2] (rgb[0] carries the
// code byte; bit 25 = semi-transparent adds a DR_MODE abr 1). 'front' puts it at the
// layer's front slot 0x820.
void fx_draw_triangle(Particle* p, const psyq::SVECTOR v[3], const u32 rgb[3], int front);
void fx_setup_light(const s32 params[3]);  // 0x8008c080: light / colour matrices
// 0x8008c378: a lit sphere of POLY_G4 quads ('hemi' 1 = the lower half).
void fx_draw_lit_sphere(Particle* p, s32 lat, s32 lon, s32 radius, u32 rgb, s32 t, s32 hemi, s32 blend);

// Spawners: layer id, positions as 20.12 VECTORs (4 words), then the script's values.
// Script B 0x7b: from -> to, accelerating; colours c0 (at the head) / c1 (tail end),
// tail length 'len' (+0x48), speed (+0x44), accel (+0x38); unless 'no_extend', 'to' is
// pushed by len along the line first.
Particle* fx_spawn_streak(u8 layer, const s32 from[4], s32 to[4], s32 c0, s32 c1, s32 len, s32 speed, s32 accel,
                          s32 no_extend);  // 0x80089ea4
// Script B 0x7c: a line along its velocity, falling (gravity 0x600 / tick).
Particle* fx_spawn_spark(u8 layer, const s32 pos[4], const s32 vel[4], s32 c0, s32 c1, s32 len,
                         s32 life);  // 0x8008a2c0
// Script B 0x0b: a line from pos to 'to', colours c0 / c1, optionally fading.
Particle* fx_spawn_beam(u8 layer, const s32 pos[4], const s32 to[4], s32 c0, s32 c1, s32 life,
                        s32 fade);  // 0x8008a3a8
// Script B 0x7d: an expanding ring of 'segments' POLY_G4, inner radius +0x48 growing by
// 'grow', width 'width'; colours at the inner / outer edge; 'tilt' rotates its plane.
Particle* fx_spawn_ring(u8 layer, const s32 pos[4], s32 segments, s32 width, s32 grow, s32 life, s32 c_outer,
                        s32 c_inner, s32 radius, s32 tilt);  // 0x8008a4b4
// Script B 0x78: a lit hemisphere growing by 'grow'.
Particle* fx_spawn_orb(u8 layer, const s32 pos[4], s32 lat, s32 lon, s32 grow, s32 life, s32 rgb,
                       s32 radius);  // 0x8008a5a8
// Script B 0x4d: one gouraud triangle (p0, p1, p2), colours c0..c2.
Particle* fx_spawn_triangle(u8 layer, const s32 p0[4], const s32 p1[4], const s32 p2[4], s32 c0, s32 c1, s32 c2,
                            s32 life, s16 fade);  // 0x8008a684
// Script A 0x12: a radial star burst lasting about 'frames'.
Particle* fx_spawn_burst(u8 layer, const s32 pos[4], s32 frames);  // 0x8008a7ec
// No caller: a spinning triangle that rises.
Particle* fx_spawn_spinner(u8 layer, const s32 pos[4], s32 a38, s32 a3c, s32 a40, s32 a28, s32 a30, s32 rgb,
                           s32 life);  // 0x8008a8e4
// Script A 0x13: a ring of textured additive quads (#3110 icon page, tpage 0x36) whose
// outer edge is raised by 'height'; 'clut' offsets the CLUT, 'uv' picks the UV set
// (table 0x8008ed58). Its spin (+0x18) is left 0 by the spawner.
Particle* fx_spawn_ring_textured(u8 layer, const s32 pos[4], s32 segments, s32 width, s32 height, s32 grow,
                                 s32 life, s32 clut, s32 uv, s32 radius);  // 0x8008d2dc

// The behaviours (table 0x8008ed10).
void fx_streak_update(Particle* p);          // 0x8008ae38 type 1
void fx_spark_update(Particle* p);           // 0x8008b1e0 type 2
void fx_ring_update(Particle* p);            // 0x8008b428 type 3
void fx_spinner_update(Particle* p);         // 0x8008b9c8 type 4
void fx_orb_update(Particle* p);             // 0x8008bfd8 type 5
void fx_beam_update(Particle* p);            // 0x8008cb4c type 6
void fx_triangle_update(Particle* p);        // 0x8008cc14 type 7
void fx_burst_update(Particle* p);           // 0x8008cd60 type 8
void fx_ring_textured_update(Particle* p);   // 0x8008d3d4 type 9

}  // namespace ss::game::field
