#pragma once

#include "core/types.h"

// The battle program's load queue (#2129 0x800475c0-0x80047e..): up to 16 archive reads,
// ordered by priority, run one at a time by the boot loader. Groups: 0 spell data, 1 the
// field program, 2 field preloads, 4 voices, 5 the encounter pack's extra data, 6 an
// entry and the next one together, 7 the description balloon's sectors.
//
// The console's reads take disc time; the host loader reads at once, and (the project's
// no-slowdown rule) a read is over by the next frame. SS_BLOAD_DRIVE_TIME=1 times its end
// as PCSX-Redux's drive would (a seek of 3 us a sector of distance plus 31 ms, then the
// sectors at single speed, 75 a second), for comparisons with Redux.
namespace ss::game::battle {

// 0x800475c0: queues 'entry' (or ((file + 1) << 16) + sector: one 2 KB sector of 'file')
// into 'dest'. priority >= 0 goes before the first queued request with a larger priority
// (or -1); -1 at the first free place. The owner token g_bload_owner is stamped on it.
void bload_request(int entry, void* dest, s16 group, int priority);
void bload_cancel_group(int group);       // 0x800477a4: 0xff clears the queue (not the read)
void bload_cancel(int entry, void* dest); // 0x80047878: the running read or the first queued match
void bload_cancel_dest(void* dest);       // 0x80047938
// 0x80047ac0: entry 0: nothing runs and nothing is queued. Else false while the entry is
// running or queued under the current owner token (token 0 matches every request).
bool bload_done(int entry);
int bload_busy_except_group(int group);   // 0x80047b90
void bload_update();                      // 0x80047c08: per frame (battle_main_loop)
u32& bload_owner();                       // g_bload_owner 0x8006fbac
void bload_reset();                       // battle start / end: the queue empties

}  // namespace ss::game::battle
