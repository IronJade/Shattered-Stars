#pragma once

#include <filesystem>

#include "core/types.h"

// The memory card side of the BIOS and PsyQ libcard/libapi, on top of host card
// images (psx::MemoryCard): BIOS events for the card classes, the _card_* low-level
// calls, and the "bu00:" / "bu10:" file system calls. Asynchronous operations finish
// immediately and report through their events, which TestEvent returns once.
namespace ss::psyq {

// Card images: slot 0 = "bu00:", slot 1 = "bu10:". A missing image file is created
// formatted (as a card fresh from the BIOS's format).
void card_set_image(int slot, const std::filesystem::path& path);
void card_set_inserted(int slot, bool inserted);  // to test the "no card" paths

// Event classes and specs (libapi).
constexpr u32 kSwCard = 0xF4000001, kHwCard = 0xF0000011;
constexpr u32 kEvSpIoe = 0x0004, kEvSpError = 0x8000, kEvSpTimeout = 0x0100, kEvSpNew = 0x2000;

long OpenEvent(u32 desc, long spec, long mode, void (*func)());
long CloseEvent(long event);
long EnableEvent(long event);
long DisableEvent(long event);
long TestEvent(long event);
void EnterCriticalSection();
void ExitCriticalSection();

// Low-level card calls (channel = port << 4).
long _card_info(long chan);
long _card_load(long chan);
long _card_clear(long chan);
long _card_write(long chan, long frame, const u8* buf);
long _card_read(long chan, long frame, u8* buf);
void _new_card();

// File system ("bu00:NAME"). Modes as PsyQ's: FREAD 1, FWRITE 2, FASYNC 0x8000,
// FCREAT 0x200 with the block count in bits 16+.
struct DIRENTRY {
  char name[20];
  long attr;
  long size;
  DIRENTRY* next;
  long head;
  char system[4];
};
long open(const char* path, unsigned long mode);
long lseek(long fd, long offset, long whence);
long read(long fd, void* buf, long n);
long write(long fd, const void* buf, long n);
long close(long fd);
DIRENTRY* firstfile(const char* pattern, DIRENTRY* dir);
DIRENTRY* nextfile(DIRENTRY* dir);
long erase(const char* path);
long format(const char* device);

}  // namespace ss::psyq
