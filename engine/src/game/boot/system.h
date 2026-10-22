#pragma once

#include "core/types.h"
#include "game/boot/loader.h"
#include "game/boot/sound.h"

namespace ss::game {

struct Pad {};    // System + 0x04: controller module (state lives in pad.cpp)
// System + 0x5aa4: the sound driver (struct Sound, game/boot/sound.h).

// Pointers handed from one program to the next (System + 0x5a20; boot 0x8001206c).
// Sixteen slots, each a pointer and the tag it was stored under.
struct HandoffRegistry {
  void* ptr[16] = {};
  s32 tag[16] = {};
};
void handoff_init(HandoffRegistry* reg);                         // 0x8001206c
bool handoff_store(HandoffRegistry* reg, void* ptr, int tag);    // 0x80012090
void* handoff_get(HandoffRegistry* reg, int tag);                // 0x80012108
void handoff_clear(HandoffRegistry* reg, int tag);               // 0x800120d8

// The resident System object boot_main creates (0x8e5c bytes on the console heap).
struct System {
  Pad pad;
  Loader loader;
  HandoffRegistry handoff;
  Sound sound;
};
System* get_system();  // 0x800100c4

// Programs the boot executable swaps in at 0x8002f810, by archive entry.
enum class Program { Title = 1, Field = 3100, Battle = 2129, Credits = 3107 };

// boot_run_main_program never returns on the console: the new program overwrites
// the caller. The port unwinds to the program loop in main by throwing this.
struct ProgramSwitch {
  Program next;
};
[[noreturn]] void boot_run_main_program(System* sys, Program next);
// The field's game over (state 8): the console re-creates the whole System (heap, pad,
// loader, handoffs, sound) the way boot_main does on a reset, then runs the title.
[[noreturn]] void boot_restart_title(System* sys);

// boot 0x80010650. Returns the first program to run.
Program boot_main(int is_reset, disc::Archive* archive);

// Entry point of each ported program; returns only if the program ends the game.
void run_program(Program program);

}  // namespace ss::game
