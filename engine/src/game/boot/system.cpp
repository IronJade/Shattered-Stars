#include "game/boot/system.h"

#include <cstdio>

#include "game/boot/pad.h"
#include "game/boot/rng.h"
#include "game/field/field.h"
#include "game/title/title_program.h"
#include "platform/window.h"
#include "runtime/libgpu.h"
#include "runtime/runtime.h"

namespace ss::game {
namespace {

System g_system;  // 0x8002b550 points at the console's copy

void vsync_callback() { vsync_callback_with(runtime::Runtime::get().host_buttons()); }

}  // namespace

void handoff_init(HandoffRegistry* reg) {
  for (auto& t : reg->tag) t = 0;
}

bool handoff_store(HandoffRegistry* reg, void* ptr, int tag) {
  int i = 0;
  while (reg->tag[i] != 0 && reg->tag[i] != tag) {
    if (++i > 15) return false;
  }
  reg->ptr[i] = ptr;
  reg->tag[i] = tag;
  return true;
}

void* handoff_get(HandoffRegistry* reg, int tag) {
  for (int i = 0; i < 16; ++i)
    if (reg->tag[i] == tag) return reg->ptr[i];
  return nullptr;
}

void handoff_clear(HandoffRegistry* reg, int tag) {
  for (int i = 0; i < 16; ++i)
    if (reg->tag[i] == tag) reg->tag[i] = 0;
}

System* get_system() { return &g_system; }

void boot_run_main_program(System*, Program next) { throw ProgramSwitch{next}; }

Program boot_main(int is_reset, disc::Archive* archive) {
  // InitHeap(0x80090000, 0x16e000): the port allocates from the host heap.
  System* sys = &g_system;
  pad_init(&sys->pad);                // 0x80013fb8
  sys->loader.attach(archive);        // loader_init: CdInit + reading the table, done by disc::Archive
  handoff_init(&sys->handoff);        // 0x8001206c
  sound_init(&sys->sound);            // 0x800144ec
  psyq::ResetGraph(0);
  // 0x80014020: SetGraphDebug(0); ClearOTag of the boot's own table; ResetCallback;
  // then the per-vblank handler.
  psyq::VSyncCallback(vsync_callback);
  if (is_reset == 0) {
    // TODO(port): memory card start (0x80023b30 / 0x80023b9c).
    rng_seed(0x4e421714);
  }
  // Loads archive entry #1 (the title program) and runs it; see Program::Title.
  sound_start(&sys->sound);           // 0x80014954: libspu, reverb, the root counter 2 tick
  // TODO(port): ChangeClearPAD(0); _bu_init().
  return Program::Title;
}

void run_program(Program program) {
  switch (program) {
    case Program::Title:
      title_entry(get_system());
      return;
    case Program::Field:
      field::field_entry(get_system());
    default:
      std::fprintf(stderr, "[port] program #%d is not ported yet\n", static_cast<int>(program));
      for (;;) psyq::VSync(0);  // hold on a blank screen until the window closes
  }
}

}  // namespace ss::game
