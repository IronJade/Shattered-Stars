#pragma once

#include <vector>

#include "core/types.h"
#include "game/field/tasks.h"

// The field's asynchronous CD loads (load_queue 0x80043050-0x80043654) and the
// preload table on top of it (0x80056374-0x800572c0).
//
// LoadQueue (0x160 bytes, vtable 0x80072dfc, task priority 1; global 0x800752b0) is a
// 16-entry ring of archive reads. Its update starts the next active request when the
// loader is idle; the completion callback (LoadCallback, vtable 0x80072e0c) sets the
// request's done byte. When the ring is empty it pumps the voice-clip queue instead.
// On the host the loader completes a read inside loader_start_read, so the done byte
// is set as soon as the request starts (still from the queue's update, as on the
// console: push runs one update immediately).
//
// The preload table (0x80075768, 12 x 0x14 bytes) holds entries read ahead of use:
// kind 1 battle data, 2 the next map's files (tagged with the trigger that asked),
// others from scripts. field_load_entry takes an entry from it when present.
namespace ss::game::field {

struct LoadRequest {    // 0x14 bytes at LoadQueue +0x08
  s32 entry = 0;        // +0x00
  u8 active = 0;        // +0x04: 0 = cancelled
  s32 bytes = 0;        // +0x08: 0 = whole entry
  void* dest = nullptr; // +0x0c
  u8* done = nullptr;   // +0x10: set to 1 when the read completes
};

struct LoadQueue : Task {
  LoadRequest requests[16];   // +0x08
  u32 head = 0;               // +0x148
  u32 tail = 0;               // +0x14c
  // +0x150: the LoadCallback object (load_callback_done 0x80043654).
  u8* in_flight_done = nullptr;  // +0x154
  s32 busy = 0;                  // +0x158: a read is in progress
  s32 in_flight_entry = 0;       // +0x15c

  void update() override;  // 0x80043324
};

LoadQueue* load_queue();                                  // 0x800752b0
LoadQueue* load_queue_construct(LoadQueue* q);            // 0x80043050: also registers it (prio 1)
void load_queue_destruct(LoadQueue* q);                   // 0x800430a8
void load_queue_push(LoadQueue* q, int entry, void* dest, u8* done, int bytes);  // 0x8004310c
void load_queue_reset(LoadQueue* q);                      // 0x8004319c
void load_queue_cancel_entry(LoadQueue* q, int entry);    // 0x80043228
bool load_queue_busy(const LoadQueue* q);                 // 0x8004355c
void load_queue_set_streaming(LoadQueue* q, bool on);     // 0x8004358c: voice streaming on/off

// Voice-clip streaming state the queue drives when idle (0x8007599c / 0x800759a0 /
// 0x80075998; the queue itself is at 0x80078fd4-0x80079a7c, not ported yet).
struct VoiceStream {
  u8 enabled = 0;   // 0x8007599c
  s32 state = 0;    // 0x800759a0: 0 idle, 1 build, 2 loading, 3 done (encounters wait for 3)
  s32 budget = 0;   // 0x80075998
};
VoiceStream& voice_stream();

// ---- Preload table --------------------------------------------------------------
struct PreloadSlot {     // 0x14 bytes
  u8 kind = 0;           // +0x00: 0 free; 1 battle; 2 map transfer; ...
  u8 done = 0;           // +0x01: the read has completed
  u8 owned = 0;          // +0x02: the buffer belongs to the table
  s16 tag = 0;           // +0x04: owner (map transfer: trigger slot)
  s32 entry = 0;         // +0x08
  s32 bytes = 0;         // +0x0c: requested size (0 = whole entry)
  std::vector<u8> data;  // +0x10
};
PreloadSlot* preload_table();  // 0x80075768[12]
bool preload_table_saved();    // 0x80075858 != 0: a menu holds the table

void preload_clear_table();                                    // 0x80056374
int preload_start(int entry, int kind, int tag, int bytes);    // 0x800563a0: 1 = queued or present
void preload_save_table();                                     // 0x8005658c (before a menu)
void preload_restore_table();                                  // 0x80056600 (after it)
int preload_release(int kind);                                 // 0x80056728 (-1 = all)
int preload_release_kind2_except(int tag);                     // 0x80056830
void preload_drop_incomplete();                                // 0x80056a3c
// 0x80056acc: the entry's buffer (null when not preloaded); 'done' and 'bytes' as stored.
std::vector<u8>* preload_find(int entry, u8** done, s32* bytes);
int preload_find_music();                                      // 0x80056b98
int preload_count_done(int kind);                              // 0x80056c68 (-1 = all)
int preload_get_kind2_tag();                                   // 0x80056cf4
void preload_remove(int entry);                                // 0x8005703c
void preload_prune_for_map(int map);                           // 0x8005711c
int preload_set_tag(int entry, int tag);                       // 0x800572c0
void preload_release_pending();                                // 0x8005613c

}  // namespace ss::game::field
