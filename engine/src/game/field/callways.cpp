#include "game/field/callways.h"

#include <cstring>

#include "disc/slz.h"
#include "game/boot/pad.h"
#include "game/boot/rng.h"
#include "game/boot/sound.h"
#include "game/common/party.h"
#include "game/field/field.h"
#include "game/field/music.h"
#include "runtime/libgpu.h"
#include "runtime/runtime.h"

namespace ss::game::field {
namespace {

Callways* g_callways = nullptr;  // 0x800752e0
std::vector<u8> g_song_sequence;  // the field BGM buffer + 0x4000 on the console

bool script_flag(int n) { return ((globals().script_flags[n >> 3] >> (n & 7)) & 1) != 0; }
void put16(u8* p, int offset, s32 v) {
  const s16 x = static_cast<s16>(v);
  std::memcpy(p + offset, &x, 2);
}

// The console spins on the sound driver (callways_idle 0x800313c0) while its interrupt
// keeps running; the host lets one vblank pass, which runs the driver.
void callways_idle() { psyq::VSync(0); }

bool music_playing() { return sound_music_playing(globals().sound) != 0; }

// 0x8003208c
void set_mod_scaled(Callways* c, int mod) { callways_set_mod(c, mod, c->level * 5); }

// 0x80032360: field_flags +0x84 = value +10 / -10 (script flags 0x24 / 0x26), -50..50.
void set_encounter_mod(Callways*, int value) {
  if (script_flag(0x24)) value += 10;
  if (script_flag(0x26)) value -= 10;
  s32 v = value > 0x32 ? 0x32 : value;
  if (value < -0x32) v = -0x32;
  globals().field_flags[0x84] = static_cast<u8>(v);
}

// 0x80032194: heal the party's HP (kind 1) or MP (kind 2) by the song level.
void party_heal(Callways* c, int kind) {
  StatChange sc;
  stat_change_begin(&sc);
  if (kind == 1)
    party_heal_all(&sc, 0, c->level);
  else if (kind == 2)
    party_heal_all(&sc, 1, c->level);
}

// 0x80032000: song 1 heals HP and MP, 0xf HP, 0x10 MP.
void song_heal(Callways* c, int song) {
  if (song == 1) {
    party_heal(c, 1);
    party_heal(c, 2);
  } else if (song == 0xF) {
    party_heal(c, 1);
  } else if (song == 0x10) {
    party_heal(c, 2);
  }
}

// 0x800320b8: kind 1 a battle with encounter level + 0x227; kind 2 a random one from the
// table at 0x800717a0. Stored as the forced encounter (game_state +0x264) when the map
// has encounters.
void song_summon(Callways* c, int kind) {
  int encounter = 0;
  if (kind == 1) {
    if (c->level > 1) encounter = c->level + 0x227;
  } else if (kind == 2 && c->level > 1) {
    const u32 i = rng_range(static_cast<u32>(((c->level - 1) * 7) / 9 + 1));
    s16 v;
    std::memcpy(&v, field_data(0x800717A0 + i * 2), 2);
    encounter = v;
  }
  if (encounter != 0 && globals().field_flags[0x25] != 0) put16(globals().game_state, 0x264, encounter);
}

// 0x80031eb0: a song's modifiers (jump table 0x80071708).
void song_mods(Callways* c, int song) {
  switch (song) {
    case 2:
      set_mod_scaled(c, 4);
      set_mod_scaled(c, 5);
      set_mod_scaled(c, 6);
      set_mod_scaled(c, 2);
      set_mod_scaled(c, 3);
      break;
    case 3: set_mod_scaled(c, 4); break;
    case 4: set_mod_scaled(c, 5); break;
    case 5: set_mod_scaled(c, 6); break;
    case 6: set_mod_scaled(c, 1); break;
    case 7: song_summon(c, 1); break;
    case 8: set_mod_scaled(c, 0xC); break;
    case 9: set_mod_scaled(c, 10); break;
    case 10: set_mod_scaled(c, 0xB); break;
    case 0xB: set_mod_scaled(c, 2); break;
    case 0xC: set_mod_scaled(c, 3); break;
    case 0xD: song_summon(c, 2); break;
    case 0xE: set_mod_scaled(c, 0xD); break;
    case 0x11:
      set_mod_scaled(c, 0xC);
      set_mod_scaled(c, 0xD);
      set_mod_scaled(c, 0xE);
      break;
    default: break;
  }
}

// 0x80031e58 (0x80032084 is empty)
void reset_mods(Callways* c) {
  for (int i = 1; i < 0xF; ++i) callways_set_mod(c, i, 0);
}

// 0x80031dc8
void apply_song(Callways* c) {
  reset_mods(c);
  song_mods(c, c->song);
}

// 0x80031e24
void song_step_effect(Callways* c) {
  if (c->step != 0) song_heal(c, c->song);
}

// 0x80031bb0: once the song has stopped, the field music's state goes back to the driver
// (it restores it on its next tick).
bool restore_bank(Callways* c) {
  FieldGlobals& g = globals();
  if (music_playing()) {
    song_step_effect(c);
    return false;
  }
  if (c->saved_music != nullptr) {
    g.sound->saved_state = c->saved_music;  // System + 0x5ac0 (the driver frees it)
    g.sound->request_restore = 1;           // System + 0x5aa8
  }
  c->saved_music = nullptr;
  while (g.sound->request_restore != 0) callways_idle();
  return true;
}

}  // namespace

Callways* callways() { return g_callways; }

Callways* callways_create(Callways* c) {
  // The party library's block pointers (0x80075270 / 0x80075278 / 0x8007527c / 0x80075280).
  FieldGlobals& g = globals();
  party_bind(static_cast<u8*>(handoff_get(g.handoff, 2)), static_cast<u8*>(handoff_get(g.handoff, 6)),
             static_cast<u8*>(handoff_get(g.handoff, 3)), static_cast<u8*>(handoff_get(g.handoff, 4)));
  c->saved_music = nullptr;
  c->level = 0;
  c->song = 0;
  c->step = 0;
  std::memset(c->member_flags, 0, sizeof c->member_flags);
  for (int i = 1; i < 0xF; ++i) callways_set_mod(c, i, 0);
  g_callways = c;
  return c;
}

void callways_destroy(Callways* c) {
  task_unregister(c);
  if (g_callways == c) g_callways = nullptr;
  delete c;
}

int callways_song_active(const Callways* c) { return c != nullptr ? c->song : 0; }

void callways_stop_song(Callways* c, bool resume_music) {
  // callways_end_song (0x80031ad8)
  if (c == nullptr || c->song == 0) return;
  FieldGlobals& g = globals();
  sound_stop(g.sound);
  while (music_playing()) callways_idle();
  if (!restore_bank(c)) {
    runtime::Runtime::get().log_once("callways-error", "Callways::ERROR");
    return;
  }
  if (resume_music && !field_music_sequence().empty())
    sound_resume_sequence(g.sound, field_music_sequence().data(), 0x3C, 0);
  reset_mods(c);
  c->song = 0;
}

void callways_stop_song_on_exit(Callways* c) {
  // The song's modifiers stay for the battle.
  if (c == nullptr || c->song == 0) return;
  FieldGlobals& g = globals();
  sound_stop(g.sound);
  while (music_playing()) callways_idle();
  restore_bank(c);  // the music has stopped, so this succeeds
  if (!field_music_sequence().empty()) sound_resume_sequence(g.sound, field_music_sequence().data(), 0x3C, 0);
  c->song = 0;
}

void callways_apply_battle_bonuses(Callways* c) {
  FieldGlobals& g = globals();
  party_update_bonds();
  bool applied = c != nullptr && c->song != 0;
  // Members with record +0x3a bit 2 add their specialty 8 level: a quarter of the sum
  // moves from config +0x188 / +0x18a to +0x194.
  int sum = 0;
  for (int slot = 0; slot < 8; ++slot) {
    party_select(slot);
    if (party_selected_char() != 0 && (party_selected_record()[0x3A] & 4) != 0) {
      sum += char_specialty_level(8);
      applied = true;
    }
  }
  if (sum < 0) sum += 3;
  const s16 q = static_cast<s16>(sum >> 2);
  u8* cfg = g.config;
  auto add16 = [&](int off, s32 d) {
    s16 v;
    std::memcpy(&v, cfg + off, 2);
    put16(cfg, off, v + d);
  };
  add16(0x188, -q);
  add16(0x194, q);
  add16(0x18A, -q);
  if (script_flag(0x25)) {
    const int level = party_super_specialty_level(3);
    applied = true;
    add16(0x18E, -4 * level);
    add16(0x190, -4 * level);
    add16(0x192, -4 * level);
    add16(0x18C, -((level * 4) / 5));
    add16(0x196, (level * 0x3C) / 0x32);
  }
  cfg[0x4D] = applied ? 1 : 0;
}

void callways_clear_encounter_flags(Callways*) {
  u8* sf = globals().script_flags;
  sf[0x24 >> 3] = static_cast<u8>(sf[0x24 >> 3] & ~(1 << (0x24 & 7)));
  sf[0x26 >> 3] = static_cast<u8>(sf[0x26 >> 3] & ~(1 << (0x26 & 7)));
}

void callways_step_tick(Callways* c) {
  if (c != nullptr) c->step = 1;
}

void callways_before_menu(Callways*) {}

void callways_after_menu(Callways*) {
  // field_flags +0x84 from the encounter flags; the console then moves the saved music
  // state into fresh allocations (heap compaction).
  s32 v = script_flag(0x24) ? 10 : 0;
  if (script_flag(0x26)) v -= 10;
  globals().field_flags[0x84] = static_cast<u8>(v);
}

void callways_play_song(Callways* c, std::span<const u8> song, int id, int level) {
  FieldGlobals& g = globals();
  c->level = static_cast<s16>(level);
  if (c->song == 0) {
    // The field music's state is saved first (sound_bank_holder_new 0x8006f90c).
    c->saved_music = new SoundSavedState;
    g.sound->saved_state = c->saved_music;
    g.sound->request_save = 1;
    sound_stop(g.sound);
    while (g.sound->request_save != 0) callways_idle();
  }
  c->song = static_cast<s8>(id);
  // The sequence (song +4, SLZ) goes into the second half of the BGM buffer.
  g_song_sequence.clear();
  if (song.size() > 4) disc::slz::unpack(song, 4, g_song_sequence, nullptr);
  if (!g_song_sequence.empty()) sound_play_sequence(g.sound, g_song_sequence.data());
  while (!music_playing()) callways_idle();
  if (c->level < 0) g.sound->flags |= 8;  // 0x8001a910(sound, 1): humanize every note
  apply_song(c);
}

void callways_set_mod(Callways* c, int mod, int value) {
  u8* cfg = globals().config;
  switch (mod) {
    case 1:
      put16(cfg, 0x184, value);
      put16(cfg, 0x186, value);
      break;
    case 2: put16(cfg, 0x188, value); break;
    case 3: put16(cfg, 0x18A, value); break;
    case 4: put16(cfg, 0x18C, value / 5); break;
    case 5: put16(cfg, 0x18E, value); break;
    case 6: put16(cfg, 0x190, value); break;
    case 7: put16(cfg, 0x192, value); break;
    case 8: put16(cfg, 0x194, value); break;
    case 9: put16(cfg, 0x196, (value * 0xF) / 0x32); break;
    case 10: set_encounter_mod(c, value); break;
    case 0xB: set_encounter_mod(c, -value); break;
    case 0xC: c->mod_12 = value > 0 ? 1 : 0; break;
    case 0xD: c->mod_13 = static_cast<u8>(value); break;
    case 0xE: c->mod_14 = static_cast<u8>(value); break;
    default: break;
  }
}

void Callways::update() {
  FieldGlobals& g = globals();
  const int selected = party_get_selected();
  // A song ends when its music stops: the field music comes back (fade 0x3c).
  if (song != 0 && restore_bank(this)) {
    if (!field_music_sequence().empty()) sound_resume_sequence(g.sound, field_music_sequence().data(), 0x3C, 0);
    reset_mods(this);
    song = 0;
  }
  // Each clock second item 0x1da costs one Fol per item; each minute 0x1e0 earns.
  const u32 now = clock_seconds();
  if (last_clock != now) {
    last_clock = now;
    if (const int n = inventory_count(0x1DA)) {
      StatChange sc;
      stat_change_begin(&sc);
      stat_change_add_money(&sc, -n);
    }
    if (now % 60 == 0) {
      if (const int n = inventory_count(0x1E0)) {
        StatChange sc;
        stat_change_begin(&sc);
        stat_change_add_money(&sc, n);
      }
    }
  }
  if (step != 0) {
    if (++step_count > 99) step_count = 0;
    for (int slot = 0; slot < 8; ++slot) {
      party_select(slot);
      if (party_selected_char() == 0) continue;
      if ((member_flags[slot] & 1) != 0 && (step_count % 5) == 0) {
        StatChange sc;
        stat_change_begin(&sc);
        stat_change_heal_hp_pct(&sc, 3);
      }
      if ((member_flags[slot] & 2) != 0) {
        u8* s = party_selected_slot();
        s32 exp;
        std::memcpy(&exp, s + 0x10, 4);
        if (++exp > 999999999) exp = 999999999;
        std::memcpy(s + 0x10, &exp, 4);
      }
    }
    step = 0;
  }
  party_select(selected);
}

}  // namespace ss::game::field
