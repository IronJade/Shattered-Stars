#pragma once

#include "core/types.h"

// The game's memory card manager (title program 0x80031290-0x8003161c over the card
// driver 0x800368ec-0x80038138). Two slots; each runs a state machine through the
// BIOS card events: check the card (_card_info), read its directory (_card_load),
// list the game's saves ("buX0:BASCUS-94421S02-Snn", nn = 01..15) and read their
// summaries, and load or write a save on request. Saves are one block each.
namespace ss::game {

// Per-slot state (MemoryCard + 0xc + slot * 0x7cc).
struct MemcardSlot {
  u8 summaries[15][0x80]{};  // +0x000: frame 4 (offset 0x200) of each save
  u16 flags = 0;               // +0x780: 1 check, 2 clear, 4 read directory, 8 file operations,
                           //         0x1000 busy writing, 0x2000 format requested,
                           //         0x4000 format done, 0x8000 waiting for an event
  u16 status = 0;              // +0x782: 0x1140 error, 0x1141 no card, 0x2281 unformatted, 0x2282 ready
  s16 port = 0;                // +0x784: the X in "buX0:"
  s16 chan = 0;                // +0x786: port << 4
  s16 state = 0;               // +0x788: file-operation step (see memcard_file_update)
  s16 file_count = 0;          // +0x78a: files on the card
  s16 free_blocks = 0;         // +0x78c
  s16 save_count = 0;          // +0x78e: this game's saves
  s16 save_map[15]{};        // +0x790: save number at each position, or -(position + 1) if none
  s32 fd = -1;             // +0x7b0
  s16 file_number = 0;         // +0x7b4
  u32 open_mode = 0;           // +0x7b8
  u8* buffer = nullptr;              // +0x7bc
  s16 length = 0;              // +0x7c0
  s16 offset = 0;              // +0x7c2
  s16 retries = 0;             // +0x7c4
  s16 index = 0;               // +0x7c6
  u8* dest = nullptr;                // +0x7c8
};

struct MemoryCard {
  MemcardSlot slots[2];          // +0x00c
  s16 current = 0;               // +0xfa4: slot the driver works on this frame
  u16 advance = 0;               // +0xfa6: bit 0 = move to the other slot
  u8* write_buffer = nullptr;    // +0xfa8
  s16 write_length = 0;          // +0xfac
  long events[8] = {};           // +0xfb0: SwCARD IOE/ERROR/TIMEOUT/NEW, then HwCARD
  struct Reader {                // +0xfd0: per-slot reading of the 0x280 blocks
    u8* buffer = nullptr;        //   0xf00 bytes
    s16 step = 0;
    s16 copied = 0;
    s16 count = 0;
  } readers[2];
  s16 summaries_enabled = 0;     // +0xfec
  s16 result = 0;                // +0xfee
};

MemoryCard* memcard_create();                    // title 0x80031290 (+ driver 0x800368ec)
void memcard_destroy(MemoryCard* card);          // title 0x80031324
void memcard_update(MemoryCard* card);           // title 0x800313b0: driver step + summary readers
int memcard_start_check(MemoryCard* card);       // title 0x800315d4: always 1
// title 0x8003161c: 2 a card with saves, 1 a formatted card without, 3 unformatted /
// no card in either slot, 0 still checking.
int memcard_status(MemoryCard* card);
// title 0x800316f4: before New Game: -1 still checking, 0 fine, else notice + 1.
int memcard_new_game_notice(MemoryCard* card);
void memcard_enable_summaries(MemoryCard* card, s16 on);  // title 0x800317cc
int memcard_take_result(MemoryCard* card);                // title 0x800317d4

// Driver (title 0x80036be4 / 0x80037074).
void memcard_driver_update(MemoryCard* card);
void memcard_file_update(MemoryCard* card, MemcardSlot* slot);

// Card image files used for "bu00:" / "bu10:" (set once at startup).
void memcard_set_images(const char* slot1_path, const char* slot2_path);

}  // namespace ss::game
