#pragma once

#include "core/types.h"

// The title program's background load queue (0x80048f34-0x800492d0): up to sixteen
// (entry, destination) reads that load_fifo_update starts one at a time while a
// screen keeps running. On the host a read completes inside the loader, so an entry
// is done one update after it starts.
namespace ss::game {

void load_queue_reset();                             // 0x80048f34
void async_load(int entry, void* dest, int slot);    // 0x80048f7c: slot -1 appends, else inserts there
void async_load_cancel(int entry, const void* dest); // 0x80049060: dest null = any
void load_queue_compact();                           // 0x80049120
bool async_load_done(int entry);                     // 0x800491d0: entry 0 = the whole queue
void load_queue_suspend();                           // 0x80049248: puts a read in progress back in the queue
void load_queue_update();                            // 0x800492b0 -> load_fifo_update 0x800492d0

}  // namespace ss::game
