#include "game/field/music.h"

#include <cstring>
#include <memory>
#include <span>

#include "disc/slz.h"
#include "game/boot/sound.h"
#include "game/field/load_queue.h"
#include "game/field/map.h"
#include "runtime/runtime.h"

namespace ss::game::field {
namespace {

std::vector<u8> g_sequence;  // 0x80075734
std::vector<u8> g_bank;      // 0x80075740: the music entry, kept while the driver uses its bank
std::unique_ptr<SoundSavedState> g_saved;  // the 0xb18-byte save buffer (the driver uses 0x858)

struct SoundHolder {  // 4 bytes on the console: the save buffer's address
  SoundSavedState* state = nullptr;
};

}  // namespace

std::vector<u8>& field_music_sequence() { return g_sequence; }

int field_install_music(Field*, int entry) {
  FieldGlobals& g = globals();
  // TODO(port): callways_song_active / callways_stop_song(1) (a Callways song playing).
  std::vector<u8>* source = &field_load_buffer();
  if (entry != 0) {
    u8* done = nullptr;
    if (auto* pre = preload_find(entry, &done, nullptr)) {
      if (*done == 0) return 0;
      source = pre;
    }
  }
  // The original keeps the load buffer itself as the bank buffer; a preload stays in the
  // preload table. The host keeps its own copy either way.
  g_bank = *source;
  if (source == &field_load_buffer()) field_load_buffer().clear();
  if (g_bank.size() < 8) return 1;
  sound_stop(g.sound);
  u32 bank_offset;
  std::memcpy(&bank_offset, g_bank.data(), 4);
  sound_load_bank(g.sound, g_bank.data() + bank_offset, 0, 0);
  if (auto seq = disc::slz::unpack_block(std::span<const u8>(g_bank).subspan(4), 0, 0, nullptr)) {
    g_sequence = std::move(*seq);
  } else {
    runtime::Runtime::get().log_once("field-music-seq", "field music: the sequence block does not unpack");
  }
  return 1;
}

void sound_holder_restore() {
  FieldGlobals& g = globals();
  auto* holder = static_cast<SoundHolder*>(handoff_get(g.handoff, 0xF));
  if (holder != nullptr) {
    g.sound->saved_state = holder->state;  // System + 0x5ac0
    g.sound->request_restore = 1;          // System + 0x5aa8
    delete holder;
  }
  handoff_clear(g.handoff, 0xF);
}

void sound_holder_save() {
  FieldGlobals& g = globals();
  // TODO(port): callways_song_active / callways_stop_song(1).
  if (auto* holder = static_cast<SoundHolder*>(handoff_get(g.handoff, 0xF))) {
    g.sound->saved_state = holder->state;
    g.sound->request_restore = 1;
    delete holder;
  }
  g_saved = std::make_unique<SoundSavedState>();
  auto* holder = new SoundHolder{g_saved.get()};
  g.sound->saved_state = holder->state;
  g.sound->request_save = 1;  // System + 0x5aa9
  handoff_store(g.handoff, holder, 0xF);
}

void field_music_free_buffers() {
  g_bank = {};
  field_load_buffer().clear();
}

}  // namespace ss::game::field
