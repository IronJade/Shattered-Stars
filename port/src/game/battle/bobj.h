#pragma once

#include <array>

#include "core/types.h"
#include "game/battle/battle.h"

// The battle's sprite object (BObj, 0x88 bytes, vtable 0x8006cd10, ctor bobj_create
// 0x80041d74) and the sprite renderer every unit, effect and HUD sprite draws through:
// AnimSet (0x68-byte animation/texture set, pool 0x8006fa24 x40), the TexCache
// (0xe0-byte VRAM cache for streamed cels, pool 0x8006d53c), bobj_project 0x80041e3c
// and bobj_anim_draw 0x80042120. Formats: analysis/battle_catalog/parts/p2.md.
namespace ss::game::battle {

// Sprite animation and texture set (unit +0x5c).
struct AnimSet {
  const u8* layout = nullptr;              // +0x00: per animation number a direction table (4 bytes)
  std::array<const u8*, 8> sheets{};       // +0x04: texture banks
  std::array<const u8*, 8> frames{};       // +0x24: cel banks
  const u8* anims = nullptr;               // +0x44: animation directory (u32 count, u32 offsets)
  std::array<u16, 8> tpage{};              // +0x48
  std::array<u16, 8> clut{};               // +0x58
};
AnimSet* animset_alloc();                  // 0x80045750
void animset_free(AnimSet* set);           // 0x800457a8
void animset_pool_reset();

// VRAM cache for streamed cels (sheet flag +8 bit 1).
struct TexCache {
  s16 area[4] = {};       // +0x00: x0 y0 x1 y1 (VRAM)
  s16 cursor[2] = {};     // +0x08
  s16 rect[16][4] = {};   // +0x0c: u0 v0 u1 v1 per entry
  u32 key[16] = {};       // +0x8c: source address of the cached cel (-1 free)
  u8 used[16] = {};       // +0xcc: used this frame
  u8 row_height = 0;      // +0xdc
  u8 victim = 0;          // +0xdd
  u8 shared = 0;          // +0xde: bobj_anim_draw leaves the use flags alone
};
TexCache* texcache_create(const s16 area[4]);  // 0x80045338
void texcache_pool_reset();

struct BObj : Task {
  // Position, facing and velocity in 20.12.
  s32 pos[3] = {};          // +0x08
  s32 pos_w = 0;            // +0x14
  s32 facing[3] = {};       // +0x18: the sign of x is the side the sprite faces
  s32 vel[3] = {};          // +0x28
  s32 extra34 = 0;          // +0x34 (Sprite: yaw speed)
  s16 screen_x = 0;         // +0x38
  s16 screen_y = 0;         // +0x3a
  s16 view_fx = 0;          // +0x3c: facing in view space
  s16 view_fy = 0;          // +0x3e
  s32 lift = 0;             // +0x40
  s16 ext_x0 = 0;           // +0x44: last frame's sprite x extents
  s16 ext_x1 = 0;           // +0x46
  u16 depth = 0;            // +0x48: sz >> 5 (OT slot and size)
  s16 unk4a = 0;            // +0x4a
  u16 anim = 0;             // +0x4c: animation number
  s16 ticks_left = 0;       // +0x4e
  s16 ticks_elapsed = 0;    // +0x50
  s16 size[3] = {};         // +0x52: body half-extents
  s16 roll = 0;             // +0x58
  u8 clut_row_check = 0;    // +0x5a
  AnimSet* animset = nullptr;  // +0x5c
  u8 pingpong_back = 0;     // +0x60
  s8 frame = -1;            // +0x61
  u8 blend = 0;             // +0x62: bit 7 plus abr + 1
  u8 colour_mode = 0;       // +0x63: 0 neutral, 1 rgb +0x64.., 2 grey +0x64
  u8 rgb[3] = {};           // +0x64
  u8 loops = 0;             // +0x67
  u8 loop_limit = 0;        // +0x68: bobj_anim_draw returns -1 when reached
  u8 flags = 0;             // +0x69: 1 screen space, 2 absolute scale, 0x20 flip
  u8 box_mask = 0;          // +0x6a
  u8 airborne = 0;          // +0x6b
  s8 layer = 0;             // +0x6c: OT bias
  u8 hit_landed = 0;        // +0x6d
  u8 clut_offset = 0;       // +0x6e
  u8 texcache_shared = 0;   // +0x6f
  TexCache* texcache = nullptr;  // +0x70
  s32 scale_x = 0x1000;     // +0x74
  s32 scale_y = 0x1000;     // +0x78
  s32 final_scale_x = 0;    // +0x7c
  s32 final_scale_y = 0;    // +0x80
  RenderContext* pool = nullptr;  // +0x84

  BObj();                   // bobj_create 0x80041d74
  // (Port, original_bugs.md B15) a freed object stops being a battle sound's emitter.
  ~BObj() override;
  BObj(const BObj&) = default;
  BObj& operator=(const BObj&) = default;
  void update() override {}
  // vtable[2]: the collider box for 'code' (low nibble box, 0x10 attack group, 0x100 max
  // corner); the base returns the zero vector 0x8006d4ec.
  virtual s32* get_box(int code);
};

void bobj_anim_reset(BObj* o);                        // 0x80041e0c
void bobj_project(BObj* o);                           // 0x80041e3c
// 0x80042120: advances the animation (unless 'flags' & 0xff) and draws the current cel;
// returns the frame, or 0xffff when the loop limit is reached. flags & 0xff00 == 0x9800
// only computes the frame's UV box (s_frame_uv).
int bobj_anim_draw(BObj* o, u32 flags);
// 0x800443f8: the ground-hugging shadow: a grid of quads (2..8 per side) over the ground
// height under the box x0..x1, z0..z1 (units, relative to the object).
void bobj_draw_shadow(BObj* o, s32 x0, s32 x1, s32 z0, s32 z1, u32 colour);
void bobj_texcache_begin_frame(BObj* o);              // 0x80042054
void bobj_texcache_reset(BObj* o);                    // 0x80042090
void bobj_share_sprite(const BObj* src, BObj* dst);   // 0x8004547c: dst uses src's AnimSet and TexCache
s32 bobj_ground_height(const BObj* o);                // 0x80044184
void bobj_move(BObj* o, const s32 vel[3]);            // 0x800452d0
// 0x800457f4: v << 12, negated when o faces left (+0x18 < 0).
inline s32 bobj_facing_fx(const BObj* o, s32 v) { return (o->facing[0] < 0 ? -v : v) << 12; }
bool bobj_on_screen(const BObj* o);                   // 0x80045448
// 0x800454a8: |dx|, |dy| and |dz| between o and p all below r << 12.
bool bobj_near_point(const BObj* o, const s32 p[3], s32 r);
bool bobj_outside_arena(const BObj* o);               // 0x80045564
int bobj_anim_ticks(const BObj* o, int from, int to); // 0x800456a0: ticks of frames from..to-1

// The UV box bobj_anim_draw(flags 0x9800) leaves (scratchpad 0x1f8001ec..0x1f8001f8)
// and the tpage / CLUT words (0x1f800000).
struct FrameUv {
  s32 u0, v0, u1, v1;
  u16 tpage, clut;
};
const FrameUv& frame_uv();

// The global sprite offset 0x8006e65c / 0x8006e65e (added to every cel's offsets).
s16& sprite_offset_x();
s16& sprite_offset_y();

}  // namespace ss::game::battle
