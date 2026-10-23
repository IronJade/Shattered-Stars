#pragma once

#include "core/types.h"

// The battle's model shatter (resident #2129, 0x8005f30c..0x80061690): a battle model (the
// format model_render 0x8007843c draws) broken into PolyShard tasks, one per polygon, that
// fly apart from the model's position, spin, fade and go. The shards are the Pool74 objects
// (192 x 0x74, g_pool_74 0x8006facc, vtable 0x8006cf94), registered with the battle task
// list at priority 3 and drawn into prim pool A.
namespace ss::game::battle {

// 0x8005f30c model_shatter: breaks 'model' into shards. pos is 20.12 (the shards fly away
// from it); rot[1] is the yaw and rot[2] the tilt about the yaw's axis (rot[0] is unused);
// scale is 4.12 per axis (ScaleMatrix). Textured shards take CLUT clut_base + the polygon's
// and tpage abr_tpage | ((tpage_base + the polygon's) & ~0x60). flags: 1 gravity and an
// upward kick, 2 they keep moving in the 0x40 pause, 4 semi-transparent. abr_tpage is also
// the DR_MODE tpage of the untextured shards; ot_bias offsets their OT depth; delay is the
// ticks before they start fading (by speed / 4, at least 2, a tick); speed is their speed.
// Stops when the pool is exhausted.
void model_shatter(const u8* model, const s32 pos[3], const s32 rot[3], const s32 scale[3], u16 clut_base,
                   u16 tpage_base, u16 flags, u16 abr_tpage, s32 ot_bias, s32 delay, s32 speed);

// (port) Frees every live shard and clears the pool and the shatter globals (0x80070010..
// 0x8007001c); for battle start and end (battle_init_objects clears the pool's flags).
void shatter_reset();

}  // namespace ss::game::battle
