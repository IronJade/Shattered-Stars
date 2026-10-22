// Table A 0x2d: the field gives way to the program in map section 6 (map 581's copy
// of #3107, the credits; game/credits/credits_program.cpp).

#include <cstdio>

#include "game/boot/sound.h"
#include "game/boot/system.h"
#include "game/field/actors.h"
#include "game/field/bg.h"
#include "game/field/encounter.h"
#include "game/field/field.h"
#include "game/field/field_sub_overlays.h"
#include "game/field/load_queue.h"
#include "game/field/map.h"
#include "game/field/message.h"
#include "game/field/music.h"
#include "game/field/scene_renderer.h"
#include "game/field/worldmap.h"
#include "runtime/libgpu.h"
#include "runtime/runtime.h"

namespace ss::game::field {

bool field_run_section6_program() {
  Field* f = field_program();
  FieldGlobals& g = globals();
  if (f == nullptr || (field_map_sub_sections() & (1u << 6)) == 0) return false;
  runtime::Runtime::get().trace("field_run_section6_program");
  g.loader->cancel();
  preload_release(-1);
  sound_stop(g.sound);
  field_unload_map(f, true);
  // mem_free of #3110's data (0x80075720): the host frees it with the field.
  field_free_ot_layers();
  bg_destroy();             // 0x8007528c: vtable +0x14
  world_destroy_objects();  // 0x800752c4 (null on field maps)
  actor_delete_slot(actor_system(), -1);
  if (MsgSystem* msg = msg_system()) msg_window_destroy(msg, -1);
  // screen_free_arena (0x800481fc): nothing to free on the host.
  field_music_sequence().clear();
  field_destroy_subsystems(f);
  // The inventory stash (0x80076228) and the battle setup (0x800752b8) are freed, not
  // handed over: the field that follows starts with an empty stash.
  psyq::ResetGraph(1);
  psyq::SetDispMask(0);
  // The script context is freed; boot_run_main_program unpacks section 6 (0x800756f4)
  // over the field and runs it.
  delete f;
  boot_run_main_program(g.system, Program::Credits);
}

}  // namespace ss::game::field
