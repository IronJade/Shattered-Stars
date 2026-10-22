#pragma once

#include "core/types.h"

// Battle voice picks (#1984.0 0x8007f76c-0x80080c7c). The voice clips themselves (load
// slots, playback) are not ported yet; what is ported are the choices and their random
// draws, which the console makes whether or not a clip plays and which the result screen
// (drops, level-ups) depends on.
namespace ss::game::battle {

struct PartyUnit;

// 0x800804d4, called when the last enemy falls: the voice tier (g_victory_voice_tier) from
// the standing members and the fight time, then the victory speaker's line and, unless
// g_single_victory_voice, a second member's (retries with rng_range(4), up to 100).
void voice_pick_victory();
// 0x800807c8 (result screen, level-up pages): one of the member's 1-5 result lines by
// level; returns the archive entry it would play, or -1 when the unit is down.
int voice_play_result(PartyUnit* u);

}  // namespace ss::game::battle
