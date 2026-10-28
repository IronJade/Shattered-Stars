#pragma once

#include "core/types.h"
#include "game/field/actor_base.h"
#include "game/field/tasks.h"

// Ghost trails of field actors (field 0x8005ee9c-0x8005f5c8; the event script's table A
// 0x1b / 0x1c, used on map 295). An Afterimage (0x88 bytes, vtable 0x80073528, task
// priority 2) owns twelve ghost actors in the effect slots 0x82-0xc1 of field_actors.
// Every frame it copies its source actor into the next ghost of a ring (a ghost is
// taken again 12 frames later) and fades the live ones by 10 from 0x80; once stopping,
// they fade out and it deletes itself. Up to twelve afterimages run at once
// (g_afterimages 0x800752ec).
namespace ss::game::field {

struct Afterimage final : Task {
  void update() override;   // vtable +0xc: afterimage_update 0x8005f108

  u8 count = 0;             // +0x08: ghosts recoloured by their age (<= 12)
  s8 head = 0;              // +0x09: ring position of the next ghost (counts down)
  u8 stopping = 0;          // +0x0c: no new ghosts; deleted once all have faded
  u8 blend = 0;             // +0x0b: the ghosts' semi-transparency (actor +0x7f)
  u8 mode = 0;              // +0x0d: the first ghost's motion: 1 grow, 2 spin, 3 both
  u8 state[12] = {};        // +0x0e: 0 faded, else 1 + motion
  Afterimage** entry = nullptr;  // +0x1c: its slot in g_afterimages (cleared on delete)
  u32 rgb = 0;              // +0x20: ghost colour (r | g << 8 | b << 16)
  s32 bright[12] = {};      // +0x24: 0x80 when taken, -10 a frame
  Actor* source = nullptr;  // +0x54
  s32 slot[12] = {};        // +0x58: field_actors index of each ghost, or -1
};

// 0x8005ee9c: count ghosts (at most 12) of 'source' in colour rgb.
Afterimage* afterimage_create(Afterimage* self, Actor* source, int count, u32 rgb, int blend, int mode);
void afterimage_destroy(Afterimage* self, int flags);  // 0x8005f018: frees the ghosts; flags & 1 deletes
void afterimage_update(Afterimage* self);              // 0x8005f108

// Table A 0x1b: a new afterimage of the actor in the first free list entry.
void afterimage_attach(Actor* source, int count, u32 rgb, int blend, int mode);
// Table A 0x1c and actor_destroy (0x8003d53c): deletes every afterimage of the actor.
void afterimage_detach(const Actor* source);
// field_destroy_map_objects (0x80051974): deletes them all (field_load_map /
// field_unload_map).
void field_destroy_map_objects();

}  // namespace ss::game::field
