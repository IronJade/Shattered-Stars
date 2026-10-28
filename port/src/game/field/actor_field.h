#pragma once

#include "core/types.h"
#include "game/field/actor_base.h"

// The field-map actor class of the code overlay (#3101, entry 0xc1d at 0x8007e000):
// 0x3fc bytes (the base plus its vptr), vtable 0x8008ebe0. It projects the actor
// through the map's fixed camera, draws its sprite with a drop shadow (or a slope-
// following 3x3 shadow on sloped floor zones), reflections (cell list 9), shading
// (lists 4 / 6) and the sprite parts of its cel, steers the player-controlled actor
// with camera-relative pad directions, and walks it with wall and actor collision
// (collision.h). The world map's 0x44c-byte class (#3102) is not ported.
namespace ss::game::field {

// Field globals this class reads.
struct FieldActorGlobals {
  u8 children_pass = 0;      // 0x800761e8: overlay 0x801f9800 present (children drawn in a second pass)
  u8 shadow_boxes = 0;       // 0x800756b8 != 0: the 0x801e9000 overlay is loaded (shadows drawn as boxes)
  u8 vehicle_control = 0;    // 0x800761ea: the 0x801f9800 overlay steers the player (vehicles)
  s16 screen_clamp = 0;      // 0x8007627e: control directions follow the projected screen (field_update_screen_steering)
  // fieldactor_build_light_split's colour of the list-6 region under the feet (its stack
  // slot sp+0x10). The original never clears it: a call that finds no region still
  // compares list-4 polygons flagged +0x48 against what the slot holds, mostly the
  // colour of an earlier call (PCSX-Redux, map 18: 627 of 807 calls), so it is kept here.
  // (No map on either disc flags a list-4 polygon.)
  u8 shade_colour[3] = {0, 0, 0};
};
FieldActorGlobals& fieldactor_globals();

// The class's methods (vtable 0x8008ebe0).
extern const ActorVtable kFieldActorVtable;

Actor* fieldactor_construct(Actor* self);                           // 0x8007e478: attachment (no init)
Actor* fieldactor_construct_screen(Actor* self, s16 model, s16 x, s16 y, int layer);  // 0x8007e4b8
Actor* fieldactor_construct_full(Actor* self, s16 model, const s32 pos[3], int angle_deg, int layer, s16 talk_entry,
                          s16 pickpocket_entry, u32 flags);          // 0x8007e540
void fieldactor_destroy(Actor* self, int flags);                    // 0x8007e600
void fieldactor_place(Actor* self);                                     // 0x8007e628: +0x74 / +0x76, floor zone
void fieldactor_draw(Actor* self, int arg);                         // 0x8007e704 (vtable +0x08)
void fieldactor_build_shadow(Actor* self, psyq::POLY_FT4* prim, const s16* points, int count);  // 0x80081080
void fieldactor_draw_attachment(Actor* self, const s32 pos[4], s32 h);         // 0x80081330
void fieldactor_draw_extra_children(Actor* self);                               // 0x80081494
void fieldactor_draw_flat(Actor* self, const s16 camera[2], const u8* frame, const u8* cel);  // 0x80081538
void fieldactor_camera_follow(Actor* self);                               // 0x80081b54 (vtable +0x18)
void fieldactor_move_step(Actor* self);                                   // 0x80081cc8 (vtable +0x20)
void fieldactor_player_control(Actor* self, int camera);                         // 0x80082838 (vtable +0x10)
// 0x80085b6c: tint regions (cell list 8) add their colour to the light.
void fieldactor_apply_light_zone(Actor* self);
// 0x80085d94: the facing (relative to the camera) picks one of 12 directions: stand,
// walk (moving) or run animation; or the set animation when +0x10 != -1.
void fieldactor_select_auto_anim(Actor* self, int walked);
// 0x80083d70: shading regions. When a list-6 polygon lies under the feet (the bottom
// edge's middle, or half a sprite to either side), every list-6 polygon near the sprite
// (those flagged +0x48 only in that polygon's colour), then, if any list-4 polygon comes
// near the sprite, every list-4 polygon near it, is clipped to the sprite's box and
// drawn over the sprite as POLY_FT4 / POLY_FT3 pieces with the sprite's texture, tinted
// by the polygon's colour minus the ambient (scene +0x6c). 'prim' is the main sprite (quad corners in actor +0x2b8), w / h
// its size on screen, flips = flip x << 1 | flip y, scaled = drawn from sprite cache A.
// The original writes through the screen's primitive cursor; here the pieces come from
// field_screen()->alloc.
void fieldactor_build_light_split(Actor* self, s32 w, s32 h, psyq::POLY_FT4* prim, int flips, int scaled);

}  // namespace ss::game::field
