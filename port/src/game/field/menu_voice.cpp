#include "game/field/menu_voice.h"

#include <cstdio>
#include <cstdlib>

#include "game/boot/rng.h"
#include "game/boot/sound.h"
#include "game/common/party.h"
#include "game/field/field.h"
#include "game/field/load_queue.h"
#include "game/field/menu_camp.h"
#include "game/field/voice_queue.h"
#include "runtime/runtime.h"

namespace ss::game::field {
namespace {

// The menu's copy of the streaming state (0x80078864, s16) and SPU budget (0x80078868, s16).
struct MenuVoice {
  s16 state = 3;
  s16 budget = 0;
} g_menu_voice;

// field_flags +0x25: encounters on, and with them the voice streaming (0x8007570c +0x25).
bool streaming() {
  const u8* flags = globals().field_flags;
  return flags != nullptr && flags[0x25] != 0;
}

// Debugging: SS_MENU_VOICE_TRACE=1 logs each step with the vblank and the rng count;
// SS_MENU_SOUND_DUMP=prefix also writes the voice slots at each step (prefix_VBLANK_STEP.bin,
// sound_debug_dump_voice_slots) for a byte compare with Redux dumps of the driver.
void trace(const char* what) {
  if (const char* dump = std::getenv("SS_MENU_SOUND_DUMP"); dump != nullptr && *dump != 0 && globals().sound != nullptr) {
    char path[512];
    std::snprintf(path, sizeof path, "%s_%llu_%c.bin", dump,
                  static_cast<unsigned long long>(runtime::Runtime::get().vblanks()), what[0]);
    sound_debug_dump_voice_slots(globals().sound, path);
  }
  static const bool on = std::getenv("SS_MENU_VOICE_TRACE") != nullptr;
  if (!on) return;
  // The sound driver's slots 0-3: loaded flag and SPU bytes used (mvref.lua's "banks").
  char banks[96] = "";
  if (Sound* s = globals().sound) {
    int at = 0;
    for (int i = 0; i < 4; ++i)
      at += std::snprintf(banks + at, sizeof banks - static_cast<size_t>(at), " %d:%x", sound_bank_loaded(s, i),
                          0xF400 - sound_bank_free_space(s, i));
  }
  std::fprintf(stderr, "[menu voice] vblank %llu rng %llu %s state %d budget %d (field %d %d) banks%s\n",
               static_cast<unsigned long long>(runtime::Runtime::get().vblanks()),
               static_cast<unsigned long long>(rng_draws()), what, g_menu_voice.state,
               g_menu_voice.budget, voice_stream().state, voice_stream().budget, banks);
}

// bgload_init 0x80038bcc (the field part).
void init() {
  if (!streaming()) return;
  g_menu_voice.state = (field_get_option_a() & 2) != 0 ? static_cast<s16>(voice_stream().state) : 3;
  if (g_menu_voice.state == 1) g_menu_voice.budget = static_cast<s16>(voice_stream().budget);
  trace("init");
}

// bgload_shutdown 0x80038c70 (after the requests are cancelled and freed).
void shutdown() {
  voice_preload_abort();
  if (!streaming()) return;
  if (g_menu_voice.state < 4) voice_stream().state = g_menu_voice.state;  // voice_stream_set_state 0x80055eb0
  voice_stream().budget = g_menu_voice.budget;                            // voice_stream_set_budget 0x80055ea0
  trace("shutdown");
}

// bgload_update 0x80039484-0x800395ec: no request in progress.
void idle(s32& timer) {
  if (!streaming()) return;
  if (timer != 0) {
    timer = static_cast<s16>(timer - 1);
    return;
  }
  switch (g_menu_voice.state) {
    case 0:
      voice_queue_reset();
      g_menu_voice.budget = static_cast<s16>(voice_party_spu_needed());
      g_menu_voice.budget = static_cast<s16>(voice_queue_build_battle(g_menu_voice.budget));
      ++g_menu_voice.state;
      trace("battle banks queued");
      break;
    case 1:
      if (voice_queue_busy()) {
        voice_queue_update();
        break;
      }
      g_menu_voice.budget = static_cast<s16>(voice_queue_build_field(g_menu_voice.budget));
      ++g_menu_voice.state;
      trace("field clips queued");
      break;
    case 2:
      if (voice_queue_busy()) {
        voice_queue_update();
        break;
      }
      ++g_menu_voice.state;
      trace("done");
      break;
    default:
      break;
  }
}

const menu::BgLoadHooks kHooks{&init, &shutdown, &voice_preload_abort, &idle};

// The hook party_update_link_combos, the killer-move list and the party order page call
// (the field's code calls 0x80038dfc directly, in and out of menus).
const bool kInstalled = [] {
  g_voice_reload_member = &voice_reload_member;
  return true;
}();

}  // namespace

const menu::BgLoadHooks& menu_voice_hooks() {
  (void)kInstalled;
  return kHooks;
}

void voice_reload_member(int slot) {
  voice_preload_abort();
  voice_slot_invalidate(static_cast<s16>(slot));
  g_menu_voice.state = 0;
  menu::bgload_restart_timer();
  trace("reload member");
}

void voice_preload_abort() { voice_queue_abort(); }

}  // namespace ss::game::field
