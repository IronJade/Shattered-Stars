#pragma once

#include "core/types.h"

// Actor sprites in VRAM (field 0x80041ce4, 0x80044754, 0x80044c8c, 0x80061cf8). Sprite
// frames are 4-bit images with 16-colour palettes in the sprite packs; they are copied
// into VRAM on demand, into one of two shelf-packed caches the ActorSystem owns:
//
//  - cache A (x 0x141-0x1ff, y 1-0xff, texture page (0x140, 0), 15-bit): the frame
//    resampled in software to its exact size on screen (bilinear, 0x80061cf8), so the
//    quad is drawn 1:1. Used for normal draws.
//  - cache B (x 0x141-0x17e, y 0x101-0x1fe, page (0x140, 0x100), 4-bit): the frame as
//    stored, its palette in CLUT row 0x1fc / 0x1fd (slot * 16); the quad scales it.
//    Used for draws with argument 0, flag 0x800000, or sprites wider than 63 px.
//
// A slot found again is kept two more frames; free slots are reused round-robin, and a
// new rectangle evicts the old slots it overlaps once their lifetime has run out.
namespace ss::game::field {

struct ActorSystem;
struct Actor;

// 0x80044754: cache A. 'work' is the resampling buffer (the screen's extra arena);
// sw / sh are the size on screen and tw / th the frame size, all 20.12; w / h the size
// on screen in pixels; phase_x / phase_y the sub-texel start. Writes the slot's UV
// rectangle (x relative to 0x140) and returns the slot, or -1.
int sprite_cache_a_alloc(ActorSystem* s, s32* u0, s32* v0, s32* u1, s32* v1, u8** work, const u8* pixels,
                         const u8* palette, s32 sw, s32 sh, s32 tw, s32 th, s32 w, s32 h, s32 phase_x, s32 phase_y);
// 0x80044c8c: cache B. Writes the UV rectangle as bytes x0 | y0 << 8 | x1 << 16 | y1 << 24
// (x in VRAM halfwords from 0x140, y from 0x100) and the CLUT id; returns the slot or -1.
// mode 1 = 8-bit frame with a 256-colour palette (CLUT rows 0x1f4 + n, n per frame).
int sprite_cache_b_alloc(ActorSystem* s, u32* uv, int mode, u16* clut, const u8* pixels, u8* palette, s32 w, s32 h,
                         s32 blend);
// 0x80061cf8: resamples a 4-bit frame to sw x sh (20.12) with bilinear weights (table
// 0x80076384), as 15-bit pixels with the STP bit set; texel pairs with a transparent
// colour give transparent pixels.
void sprite_resample(u16* out, const u8* pixels, const u8* palette, s32 sw, s32 sh, s32 tw, s32 th, s32 phase_x,
                     s32 phase_y);
// 0x80041ce4: one sprite part (a 0x1c-byte part record: corners, offset, frame, colour)
// of the actor's current cel, as a POLY_FT4 appended to the actor's primitives.
void sprite_draw_part(Actor* a, const u8* cel, const u8* part, s32 cos_r, s32 sin_r, int flip_x, int flip_y,
                      int scaled);

// boot 0x80012f28: uploads a 16-colour palette row and returns its CLUT id.
u16 upload_clut16(const u16* clut, int x, int y);

}  // namespace ss::game::field
