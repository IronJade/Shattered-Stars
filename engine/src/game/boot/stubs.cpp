#include "game/boot/stubs.h"

#include "runtime/runtime.h"

namespace ss::game {
namespace {

void todo(const char* what) { runtime::Runtime::get().log_once(what, std::string("not ported yet: ") + what); }

}  // namespace

void sound_load_bank(Sound*, const void*, int, int) { todo("sound_load_bank"); }
int sound_bank_busy(Sound*) { return 0; }
void sound_set_sequences(Sound*, void*) { todo("sound_set_sequences"); }
void sound_set_volume(Sound*, int, int) { todo("sound_set_volume"); }
void sound_play_sequence(Sound*, void*) { todo("sound_play_sequence"); }
void sound_stop(Sound*) {}
void sound_fade_out(Sound*, int) { todo("sound_fade_out"); }
int sound_play_sfx(Sound*, int, int, int) {
  todo("sound effects");
  return 0;
}

namespace {
int g_output_mode = 0;  // Sound + 0x52 (read through sound_state)
}  // namespace

int sound_output_mode(Sound*) { return g_output_mode; }
void sound_set_output_mode(Sound*, int mode) {
  todo("sound output mode");
  g_output_mode = mode;
}
void sound_set_pan(Sound*, int, int) {}
void sound_nop_8001aabc(Sound*, int, int) {}

}  // namespace ss::game
