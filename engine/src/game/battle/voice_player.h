#pragma once

#include "core/types.h"

// The battle's voice player (#1984.0 0x8007f76c-0x8007fd54): two slots, each an archive
// entry loaded through the load queue (group 4) into its own buffer and then played as a
// sound clip (bsfx_play mode 1). Handles are (0x40 + slot) << 24 | entry. (The choices of
// which voice to play are voice.h's.)
namespace ss::game::battle {

struct Unit;

// 0x8007f818: a slot already holding the entry and not yet asked to play takes the
// request; else, when the queue holds nothing but voices (or 'tag' is not -1), a free
// slot loads it (priority 'tag'). The handle, or 0xffffffff.
u32 voice_play_entry(int entry, Unit* speaker, int tag = -1);  // (callers not yet updated pass no tag)
u32 voice_preload_entry(int entry);  // 0x8007f9bc: loads without asking to play (-1 / handle)
void voice_stop_all();               // 0x8007f76c
void voice_update();                 // 0x8007faa4: per frame (battle_main_loop)
// sound_handle_active's voice half (0x8007fc54): the entry still in its slot, or its clip
// still playing.
bool voice_handle_active(u32 handle);
void voice_player_reset();           // battle start / end: the slots empty

}  // namespace ss::game::battle
