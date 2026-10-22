#pragma once

#include <span>

#include "core/types.h"
#include "game/field/tasks.h"

namespace ss::game {
struct SoundSavedState;
}

// Callways (0x24 bytes, vtable 0x80071780, task priority 1; global 0x800752e0; class name
// from its "Callways::ERROR" message): the songs played from the camp menu (the Music
// specialty) and the walking effects of items and equipment. A song swaps the field
// music for its own sequence (the field music's state is kept and resumed), sets
// modifiers in config +0x184..+0x196 (and the encounter modifier field_flags +0x84) for
// the song's level, heals on steps, or summons a battle. Each clock second item 0x1da
// costs money and every minute item 0x1e0 earns some.
namespace ss::game::field {

struct Callways final : Task {
  u8 mod_13 = 0;              // +0x08: song modifier 13
  u8 mod_12 = 0;              // +0x09: song modifier 12 (bool)
  u8 mod_14 = 0;              // +0x0a: song modifier 14
  u8 member_flags[8] = {};    // +0x0b: per party slot: 1 heal 3% HP every 5 steps, 2 EXP +1 per step
  SoundSavedState* saved_music = nullptr;  // +0x14: the field music's state while a song plays
  s16 level = 0;              // +0x18: the song's level (< 0: humanized play)
  s8 song = 0;                // +0x1a: song id, 0 = none
  u8 step = 0;                // +0x1b: the leader walked far enough (callways_step_tick)
  s8 step_count = 0;          // +0x1c: steps, 0..99
  u32 last_clock = 0;         // +0x20: clock seconds of the last money tick

  void update() override;     // callways_update 0x800314b0
};

Callways* callways();                                   // 0x800752e0
Callways* callways_create(Callways* c);                 // 0x800313c8 (the caller registers it, prio 1)
void callways_destroy(Callways* c);
int callways_song_active(const Callways* c);            // 0x80031718
void callways_stop_song(Callways* c, bool resume_music);  // 0x80031724 -> callways_end_song 0x80031ad8
void callways_stop_song_on_exit(Callways* c);           // 0x80031764
void callways_apply_battle_bonuses(Callways* c);        // 0x8003180c
void callways_clear_encounter_flags(Callways* c);       // 0x8003199c
void callways_step_tick(Callways* c);                   // 0x800319c4
void callways_before_menu(Callways* c);                 // 0x800319d0 (empty)
void callways_after_menu(Callways* c);                  // 0x800319d8
// 0x80031ca0: plays song 'id' at 'level' from its data (an SLZ-packed sequence from byte
// 4), keeping the field music's state while it plays.
void callways_play_song(Callways* c, std::span<const u8> song, int id, int level);
void callways_set_mod(Callways* c, int mod, int value);  // 0x80032208

}  // namespace ss::game::field
