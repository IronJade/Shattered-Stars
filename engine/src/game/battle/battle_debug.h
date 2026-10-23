#pragma once

#include <string>

#include "game/boot/system.h"

// Debug entry to the battle program without the field (main.cpp --battle).
namespace ss::game::battle {

// --battle ENC[:STAGE]: builds what the title and the field would have handed over for
// a new game's party (Claude, level 1; handoffs 2 / 3 / 4 / 6) and the battle setup of
// encounter ENC on stage STAGE (default 3, Shingo Forest) as the field's
// battle_build_load_list does (handoff 5, nothing preloaded), then returns
// Program::Battle. 'end' is --battle-end FRAMES[:OUTCOME]: end the fight after FRAMES
// vblanks in state 2 with OUTCOME (default 0, victory), since the combat and the result
// screen are not ported yet. With SS_BATTLE_RAM=<2 MB RAM dump of the original> the
// party, config and inventory (and, when the dump was taken at field_exit_to_program,
// the encounter and stage of the field's setup) come from the dump instead.
// SS_BATTLE_PARTY=1,2,3,4 fills the party slots with those characters' starting data.
// SS_BATTLE_STRONG=1 multiplies the party's HP by 10 and attack by 8 ("H:A:D": by H, A and D
// for defence, HP capped at 9999).
// SS_BATTLE_POKE=TAG:OFFSET=VALUE/SIZE,... then writes into the handoff blocks (after
// SS_BATTLE_STRONG), e.g. "4:0xcc=10/1" for killer move byte 10 on slot 0's L1.
Program battle_debug_start(const std::string& spec, const std::string& end);
// --battle-end alone: every battle of the run (the field's too) ends that way.
void battle_debug_set_end(const std::string& end);
// SS_BATTLE_RNG=1: traces the RNG draw count each frame (to line up with a PCSX-Redux
// count of rng_next calls). SS_BATTLE_DUMP=<file>: at battle_exit_to_field writes the
// handoff blocks the field reads back (2 config 0x2a0, 3 party 0x300, 4 records 0x680,
// 6 inventory 0xc28, in that order) for a comparison with a RAM dump of the original.
// SS_BATTLE_GOD=1: the standing party members' HP is refilled every frame (sweeps).
// SS_BATTLE_CAM=1: the camera / clock fields once a frame. SS_BATTLE_BOXES=LO:HI: every
// unit's box mask, enabled boxes and animation fields once a frame in that frame range.
void battle_debug_frame();
// SS_BATTLE_TRAIL=LO:HI: each RibbonTrail update in that battle-frame range (explore.lua's
// BW_TRAIL prints the same: length, head, stop, the source Shot and its size, the newest
// history entry).
struct RibbonTrail;
void battle_debug_trail(const RibbonTrail* t);
// battle_exit_to_field: prints the RNG draws, encounter, outcome and frames; SS_BATTLE_DUMP
// writes the handoffs; SS_BATTLE_QUIT_AT_EXIT=1 then ends the run (runtime::QuitRequested).
void battle_debug_exit();
// Battle state 0 -> 1 (the party loader's end; party.cpp). SS_BATTLE_RNG_SKIP=N makes N extra
// draws there: re-syncs the generator with a reference whose CD-timed loads drew differently
// (docs/notes/battle_sweep.md, "RNG after CD-timed loads").
void battle_debug_loaded();
// The vblanks battle frame 'n' (0 = the first) waits for at its end (VSync(2): 2).
// SS_BATTLE_TICKS=W0,W1,... overrides the first frames' waits, to reproduce a reference
// whose setup ended mid-way through a 2-vblank period: "1" gives the original's 2, 1, 2,
// 2, ... tick pattern (frame 1 runs with 1 tick; g_battle_frames odd from then on).
int battle_debug_frame_wait(int n);
// SS_BATTLE_TICK_MAP="F:T,..." (world agent): the wait after a frame that leaves g_battle_frames
// at F (0: none given).
int battle_debug_tick_map(s32 battle_frames);

}  // namespace ss::game::battle
