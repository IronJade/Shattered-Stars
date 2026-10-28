#pragma once

#include "core/types.h"

// The party's spoken lines in battle (#2129 0x8003acac / 0x8003acbc / 0x80074980, #1984.0
// 0x80072228-0x80072444 and 0x80080878-0x80080b84): the opening lines, the provoke line,
// the flee line and an ally's call when a member falls. A member who speaks takes the talk
// pose (PartyUnit::talk_voice, +0x228; busy) until the clip ends (pc_update 0x800350cc).
namespace ss::game::battle {

struct PartyUnit;
struct Unit;

// 0x8003acac pc_start_talk: busy (+0x201) = 1, talk_voice (+0x228) = the clip's handle.
void pc_start_talk(PartyUnit* u, u32 handle);
// 0x8003acbc pc_start_fury: the member's action ends (action_kind 0, begin_action), it
// talks, and its attack doubles (stats +2 bit 0x10) for 1200 ticks (fury_ticks, +0x22c).
void pc_start_fury(PartyUnit* u, u32 handle);
// pc_update 0x800350cc: the talk pose ends with its clip.
void pc_talk_update(PartyUnit* u);

// battle_init_state 0x80074ea4-0x80074fac: each party record's row of the banter table
// (party_banter_voice_table 0x80074980) into g_member_tables (0x8006fb7c).
void party_talk_init();
// battle_frame_director 0x80072228-0x80072444: the opening lines. From frame 0x82 the
// first intro speaker says bank 5's line; 0x3d ticks later, once that clip has ended, the
// second says bank 6's (the banks load_intro_voice put in; g_intro_voice_state bits 1 / 2).
void opening_voices_update();

// 0x80080878: one of the member's two flee lines (pc_state_flee).
void voice_play_extra(Unit* u);
// 0x800808e8: one of the member's four battle lines, with the talk pose (the provoke
// command). Returns the clip's handle, or -1 (down, or no voice slot).
s32 voice_play_battle_line(PartyUnit* u);
// 0x80080974 (pc_think, when u falls): the first standing member whose bond with u is
// strong enough calls out (rng_next15 against bond * 0x24e) and goes into a fury.
void voice_ally_reaction(PartyUnit* u);

// Debugging: SS_TALK_TRACE=1 logs 'what' with the vblank, the rng draws so far, the unit's
// party slot and 'value' (lines up with explore.lua's BW_HIT_BP hits on the same addresses).
void talk_trace(const char* what, const Unit* u, u32 value);

}  // namespace ss::game::battle
