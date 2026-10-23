#pragma once

// Helpers shared by the party-character modules of Precis, Ashton, Opera and Chisato
// (pc_precis.cpp, pc_ashton.cpp, pc_opera.cpp, pc_chisato.cpp).
#include "core/types.h"
#include "game/battle/killer.h"
#include "game/battle/shot.h"
#include "game/battle/unit.h"

namespace ss::game::battle {

// The console keeps a callback Shot's afterimage in a field of the Shot (+0x130 mostly);
// the port keeps them in a table keyed by the Shot. The afterimage task clears its entry
// through its back pointer when it goes.
Afterimage* shot_image(const Shot* s);
void shot_image_set(const Shot* s, Afterimage* a);
// A Shot spawned for a module callback starts without an afterimage: one left from the
// slot's previous use is let go (it ends).
void shot_image_reset(const Shot* s);
// The afterimage ends (+0xc = 1) if the Shot has one.
void shot_image_end(const Shot* s);

// 0x8003aad8 pc_camera_frame_target_for: for the controlled member whose camera follows
// it, frame its target (camera_set_focus_unit) for 'ticks' (camera_schedule_end 7).
void pc_camera_frame_target_for(PartyUnit* u, int ticks);

// The owner's weapon element (stats +0x5c) ORed with 'bits' for the life of the object
// (module code does this around a Shot's hit).
struct WeaponElementBits {
  u8* stats;
  u16 saved;
  WeaponElementBits(Unit* owner, u16 bits);
  ~WeaponElementBits();
};

// The GTE set up for a lit model at pos, as module code does inline: rotation the render
// view (0x8006f08c), translation the view of pos - the eye (0x8006f008) (MVMVA rt * v0).
void gte_origin_at(const s32 pos[3]);

}  // namespace ss::game::battle
