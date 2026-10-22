#pragma once

#include <vector>

#include "core/types.h"
#include "runtime/libspu.h"

// The boot executable's sound driver (boot 0x800144ec-0x8001ab44): the Sound object
// at System + 0x5aa4 (0x33b8 bytes on the console). It is a sequencer of its own
// (not libsnd): 24 music tracks and 8 effect tracks run byte-coded sequences
// (docs/catalog/formats.md "Sound sequences"), allocate SPU voices from a ring of
// free voices, pick an instrument region per note from the banks loaded into SPU RAM,
// and push pitch / volume / ADSR to libspu. It runs from root counter 2's interrupt
// (opened in sound_start), music at the sequence's tempo and effects at a fixed
// ~65 Hz; the game talks to it by setting request bytes that the next tick acts on.
//
// Field names follow what each byte does; the console offset of every field (from
// the Sound object) is given so the decompilation can be followed.
namespace ss::game {

// A level ramp (8 bytes): Sound + 0x5c music, + 0x64 effects, + 0x6c pause.
struct SoundFade {
  s16 rate = 0;        // +0: added to 'fraction' per effect tick; the high byte moves 'level'
  s8 direction = 0;    // +2: 1 fade in, -1 fade out, 0 idle
  u16 fraction = 0;    // +4
  u8 level = 0;        // +6: 0..0x7f
  s8 floor = 0;        // +7: where a fade out stops
};

// A sequence track (0x58 bytes at Sound + 0x98): 0-23 music, 24-31 effects.
struct SoundTrack {
  u32 flags = 0;                  // +0x00: kTrack* bits
  const u8* patterns = nullptr;   // +0x04: u16 pattern offsets (from the sequence start), 0xffff ends
  const u8* pc = nullptr;         // +0x08: next command; null once the pattern list ended (0xffff)
  const u8* instrument = nullptr; // +0x0c: region list of the current program (count, then 20-byte regions)
  const u8* loop_pc = nullptr;    // +0x10: command after the last loop start (0x8d)
  s32 voices = 0;                 // +0x14: voices playing for this track
  s32 notes = 0;                  // +0x18: notes started in this tick
  s16 pattern = 0;                // +0x1c: index into 'patterns'
  u8 active = 0;                  // +0x1e
  s8 humanize = 0;                // +0x1f: random-velocity threshold (never set by the commands)
  u16 program = 0;                // +0x20: instrument id (0x83)
  u8 wait = 0;                    // +0x22: ticks until the next command
  u8 lfo_phase = 0;               // +0x23: 0..63, advanced by lfo_rate every tick
  s8 transpose = 0;               // +0x24: semitones (track table byte 2)
  u8 note = 0;                    // +0x25: note of the last note command
  s8 detune = 0;                  // +0x26: random -2..2 per note when humanized
  s8 transpose2 = 0;              // +0x27: added with 'transpose' and 'detune'
  s16 loop_pattern = 0;           // +0x28: pattern index at the loop start
  u8 loop_count = 0;              // +0x2a: 0xff until the loop end (0x8e) sets its count
  u8 stream_voice = 0;            // +0x2b: SPU voice of a streamed clip track
  u8* stream_data = nullptr;      // +0x2c: the clip's ADPCM data (a RAM address on the console)
  u32 stream_size = 0;            // +0x30
  u16 adsr1 = 0, adsr2 = 0;       // +0x34 / +0x36: from the program header, or 0x97
  s8 bend = 0;                    // +0x38: pitch bend (0x84), times bend_range / 64 semitones
  s8 coarse = 0;                  // +0x39: 1/64-semitone steps of 12 (no command sets it)
  u8 lfo_depth = 0;               // +0x3a: track vibrato depth (0x88)
  u8 bend_range = 0;              // +0x3b: (0x96), 0x0c by default
  s8 fine = 0;                    // +0x3c: 1/64 semitones (0x95)
  s8 fine2 = 0;                   // +0x3d
  u8 pan = 0;                     // +0x3e: 0..0x7f, 0x40 centre (sound_set_pan)
  s8 random_fine = 0;             // +0x3f: random -3..3 per note with kTrackRandomPitch
  u8 volume = 0;                  // +0x40: (0x85)
  u8 expression = 0;              // +0x41: (0x86)
  u8 volume3 = 0;                 // +0x42: effects: the caller's volume; 0x7f for music
  u8 volume4 = 0;                 // +0x43: 0x7f; effect clips fade it
  u8 vibrato_delay = 0;           // +0x44: (0x94) ticks before kTrackVibrato starts
  u8 vibrato_depth = 0;           // +0x45
  u8 vibrato_rate = 0;            // +0x46
  u8 note_pan = 0;                // +0x47: (0x87) pan of the notes, 0x40 centre
  u8 voice_param[3] = {};         // +0x48: copied to the voice, unused
  u8 priority = 0;                // +0x4b: (0x81) a voice may be stolen for a lower value
  u8 echo_wait = 0;               // +0x4c: (0x9b) the echo track's delay
  u8 echo_transpose = 0;          // +0x4d
  u8 echo_fine = 0;               // +0x4e
  u8 echo_volume = 0;             // +0x4f
  u8 echo_note_pan = 0;           // +0x50
  u8 dirty = 0;                   // +0x51: kDirty* bits gathered while commands run
  u8 dirty_latched = 0;           // +0x52: last tick's 'dirty', applied to the voices
  u8 note_format = 0;             // +0x53: (0x9e) 1 note,wait,vel  2 note,wait  3 note,wait,len  4 note,wait,len,vel
  s16 pitch_scale = 0;            // +0x54: 0 = none, else pitch * scale / 0x2000 (effect clips)
  u8 fixed_velocity = 0;          // +0x56
  u8 fixed_length = 0;            // +0x57
};

// An SPU voice as the driver sees it (0x38 bytes at Sound + 0xb98).
struct SoundVoice {
  u32 flags = 0;                  // +0x00: the track's flags at note on
  const u8* region = nullptr;     // +0x04: instrument region (20 bytes; +4 = sample address)
  u8* stream_data = nullptr;      // +0x08
  u32 stream_size = 0;            // +0x0c
  u32 stream_pos = 0;             // +0x10
  u32 volume_packed = 0;          // +0x14: right << 16 | left
  psyq::SpuVolume volume;         // +0x18
  u16 base_pitch = 0;             // +0x1c
  u8 left = 0;                    // +0x1e: 0x7f - pan
  u8 right = 0;                   // +0x1f: pan
  u16 pitch = 0;                  // +0x20: base_pitch plus vibrato
  u8 note_index = 0;              // +0x22: index into the pitch table
  s8 transpose = 0;               // +0x23
  s16 hold = 0;                   // +0x24: ticks held by the sustain pedal (0x89)
  u8 active = 0;                  // +0x26
  u8 track = 0;                   // +0x27
  u8 velocity = 0;                // +0x28
  u8 length = 0;                  // +0x29: ticks left
  u8 track_volume = 0;            // +0x2a
  u8 note = 0;                    // +0x2b
  u8 vibrato_delay = 0;           // +0x2c
  u8 vibrato_depth = 0;           // +0x2d
  u8 vibrato_rate = 0;            // +0x2e
  u8 note_pan = 0;                // +0x2f
  u8 voice_param[3] = {};         // +0x30
  u8 priority = 0;                // +0x33
  s8 vibrato_level = 0;           // +0x34: rises to vibrato_depth
  u8 vibrato_phase = 0;           // +0x35
};

// A playing sound effect (0x34 bytes at Sound + 0x112c): a short script that starts
// one effect track and waits for it.
struct SoundEffect {
  const u8* script = nullptr;     // +0x00: next script word (u32); null = free slot
  u32 id = 0;                     // +0x04: the id it was started with (clips: their address)
  u8* clip = nullptr;             // +0x08: a streamed clip's ADPCM data, null for effects
  u32 clip_size = 0;              // +0x0c: its bytes
  s16 delay = 0;                  // +0x10: ticks until the next script word
  s8 priority = 0;                // +0x12: header byte 3
  s8 group = 0;                   // +0x13: header byte 2: one effect per group plays
  u16 volume_pan = 0;             // +0x14: the caller's volume << 8 | pan
  s8 transpose = 0;               // +0x16
  u8 track = 0;                   // +0x17: its effect track (24-31), 0 = none yet
  s16 pitch_scale = 0;            // +0x18: (sound_start_clip)
  u8 pan = 0;                     // +0x1a
  u8 volume = 0;                  // +0x1b
  u8 update = 0;                  // +0x1c: push pan / volume / pitch to the track
  u8 invert = 0;                  // +0x1d
  s8 table = 0;                   // +0x1e: sequence table (id >> 24)
  u8 alternate = 0;               // +0x1f
  s16 header_low = 0;             // +0x20
  s16 pitch_value = 0;            // +0x22: pitch sweep
  s16 pitch_step = 0;             // +0x24
  s16 pitch_steps = 0;            // +0x26
  s16 fade_rate = 0;              // +0x28
  s8 fade_direction = 0;          // +0x2a: -1 while fading out
  u16 fade_fraction = 0;          // +0x2c
  s8 fade_level = 0;              // +0x2e
  s8 fade_floor = 0;              // +0x2f
  s16 duck = 0;                   // +0x30: ticks of volume4 held at 0
  s16 fade_delay = 0;             // +0x32
};

// One of twelve places a bank can load to (0x30 bytes at Sound + 0x12cc): its
// instrument table in the Sound object, its SPU RAM area and (slots 0-5, 8-11) a table
// of effect sequences imported with it.
struct SoundBankSlot {
  u8* instruments = nullptr;      // +0x00: end of the instrument table
  u8* instruments_base = nullptr; // +0x04
  u32 spu = 0;                    // +0x08: next free SPU RAM address
  u32 spu_base = 0;               // +0x0c
  u8* index = nullptr;            // +0x10: u16 offsets of effect scripts
  u8* scripts = nullptr;          // +0x14: next free script record (12 bytes)
  u8* scripts_base = nullptr;     // +0x18
  u8* sequence_offsets = nullptr;      // +0x1c
  u8* sequence_offsets_base = nullptr; // +0x20
  u8* sequences = nullptr;        // +0x24
  u8* sequences_base = nullptr;   // +0x28
  u8* remap = nullptr;            // +0x2c: (slots 0-3) effect number remap, 20 bytes
};

// What sound_save_state keeps (0x858 bytes, allocated by the caller in +0x1c).
struct SoundSavedState {
  SoundTrack tracks[24];
  const u8* sequence = nullptr;
  s16 reverb_depth_l = 0, reverb_depth_r = 0;
  s16 loop_count = 0, bar = 0, bar_tick = 0, loop_bar_tick = 0;
  u8 base_tempo = 0;
  s8 sequence_tempo = 0;
  u8 flags = 0;
  u32 sequence_id = 0;
};

constexpr u32 kTrackNoKeyOn = 0x2;        // track table flag 0x01
constexpr u32 kTrackFixedNote = 0x4;      // flag 0x80: transpose ignored (drums)
constexpr u32 kTrackVibrato = 0x10;       // 0x93
constexpr u32 kTrackReverb = 0x20;        // 0x90
constexpr u32 kTrackInvertRight = 0x40;   // 0x92 (surround)
constexpr u32 kTrackInvertLeft = 0x80;
constexpr u32 kTrackSustain = 0x100;      // 0x89
constexpr u32 kTrackEffect = 0x800;       // tracks 24-31
constexpr u32 kTrackRegionNote = 0x1000;  // flag 0x40: the region sets note, length and pan
constexpr u32 kTrackRandomPitch = 0x4000; // 0x99
constexpr u32 kTrackEcho = 0x8000;        // a copy made by 0x9a
constexpr u32 kTrackStream = 0x20000;     // a streamed clip
constexpr u32 kTrackPanByNote = 0x40000;  // flag 0x02

constexpr u8 kDirtyPitch = 0x01, kDirtyVolume = 0x02, kDirtyProgram = 0x04, kDirtyRegion = 0x08,
             kDirtyTrackVolume = 0x10, kDirtyPan = 0x20;

struct Sound {
  u8 request_restore = 0;          // +0x04
  u8 request_save = 0;             // +0x05
  u8 request_output_mode = 0;      // +0x06
  u8 request_tempo = 0;            // +0x07
  u8 request_fade_in = 0;          // +0x08
  u8 request_fade_out = 0;         // +0x09
  u8 request_effects_fade_in = 0;  // +0x0a
  u8 request_effects_fade_out = 0; // +0x0b
  u8 request_stop = 0;             // +0x0c
  u8 request_rekey = 0;            // +0x0d
  u8 request_pause = 0;            // +0x0e
  u8 request_resume = 0;           // +0x0f
  u8 request_volume = 0;           // +0x10
  u8 output_mode_wanted = 0;       // +0x11: 0 surround, 1 stereo, 2 mono
  s8 tempo_adjust = 0;             // +0x12
  u8 pause_floor = 0;              // +0x13
  s16 pause_frames = 0;            // +0x14
  s16 music_fade_rate = 0;         // +0x16
  s16 effects_fade_rate = 0;       // +0x18
  SoundSavedState* saved_state = nullptr;  // +0x1c
  // +0x20..0x98: cleared by sound_reset; sound_state() points here.
  long event = 0;                  // +0x20: root counter 2 event
  s32 ticks = 0;                   // +0x2c: interrupts since start
  const u8* sequence = nullptr;    // +0x30: the music
  const u8* previous_sequence = nullptr;  // +0x34
  const u8* effect_sequences = nullptr;   // +0x38: entry #9 + 4: u16 offsets
  const u8* effect_scripts = nullptr;     // +0x3c: entry #9 + its first word: s16 offsets
  u32 flags = 0;                   // +0x40: bit 0 music playing, bit 3 humanize every note
  u8* free_read = nullptr;         // +0x44: ring of free voices
  u8* free_write = nullptr;        // +0x48
  s16 random_a = 0, random_b = 0;  // +0x4c / +0x4e
  u8 music_volume = 0;             // +0x50
  u8 effects_volume = 0;           // +0x51
  u8 output_mode = 0;              // +0x52
  u8 base_tempo = 0;               // +0x53
  u32 frame_phase = 0;             // +0x54: root counter ticks towards the next effect tick
  u16 rcnt_target = 0;             // +0x58
  s8 sequence_tempo = 0;           // +0x5a: (0x82)
  u8 tempo = 0;                    // +0x5b
  SoundFade music_fade;            // +0x5c
  SoundFade effects_fade;          // +0x64
  SoundFade pause_fade;            // +0x6c
  u8 bank_order = 0;               // +0x74: which of slots 4 / 5 is searched first
  u8 clip_slot_used[2] = {};       // +0x76
  s16 reverb_depth_l = 0;          // +0x78
  s16 reverb_depth_r = 0;          // +0x7a
  u8 reverb_invert_l = 0;          // +0x7c
  u8 reverb_invert_r = 0;          // +0x7d
  s16 loop_count = 0;              // +0x7e: track 0's loops (music position, for the field)
  s16 bar = 0;                     // +0x80
  s16 bar_tick = 0;                // +0x82
  s16 loop_bar_tick = 0;           // +0x84
  u8 stream_voice = 0;             // +0x86
  u8 releasing = 0;                // +0x87: sound_release_clip in progress (the tick waits)
  u8 restored_stopped = 0;         // +0x88
  s32 transfer_timeout = 0;        // +0x8c
  u32 saved_sequence_id = 0;       // +0x90
  SoundTrack tracks[32];           // +0x98
  SoundVoice voices[24];           // +0xb98
  void* default_notify = nullptr;  // +0x10d8
  u8 transfer_busy = 0;            // +0x10dc: a bank is on its way to SPU RAM
  u8 stream_busy = 0;              // +0x10dd
  u8 stream_delay = 0;             // +0x10de
  u8 stream_stopping = 0;          // +0x10df
  s32 stream_timer = 0;            // +0x10e0
  psyq::SpuStEnv* stream_env = nullptr;  // +0x10e4
  s32 reserved_voices = 0;         // +0x10e8: voices below this are not allocated
  s32 clip_counter = 0;            // +0x10ec
  psyq::SpuReverbAttr reverb;      // +0x10f0
  psyq::SpuCommonAttr common;      // +0x1104
  SoundEffect effects[8];          // +0x112c
  SoundBankSlot banks[12];         // +0x12cc
  u8 note_volume[36] = {};         // +0x150c: per 4 notes, 0x60
  u8 free_voices[25] = {};         // +0x1530
  char spu_malloc[0x28] = {};      // +0x154c: SpuInitMalloc table
  u8 bank_loaded[8] = {};          // +0x1574
  u8 bank_area[0x3338 - 0x1580] = {};  // +0x1580: instrument tables and effect sequences
  u8 remap[4][20] = {};            // +0x3338
  u16 tick_kind = 0;               // +0x3388: bit 0 music tick, bit 1 effect tick
  u16 tick_class = 0;              // +0x338a: kTrackEffect for effect ticks
  s32 frame_ticks = 0;             // +0x338c: 125000000 / (tempo * 6356)
  s16 subtick = 0;                 // +0x3390
  s16 extra_ticks = 0;             // +0x3392: extra passes for the next interrupt
  u32 key_on = 0;                  // +0x33a0
  u32 key_off = 0;                 // +0x33a4
  u32 key_on_delayed = 0;          // +0x33a8: re-struck voices, keyed on a tick later
  u32 stream_start = 0;            // +0x33ac
  u32 stream_stop = 0;             // +0x33b0
  s16 stream_hold = 0;             // +0x33b4
  s16 stream_reset = 0;            // +0x33b6

  // Host: the console address 'offset' bytes into the Sound object, for the buffers
  // in bank_area (0x1580-0x3337) that the bank slots point into.
  u8* at(u32 offset) { return bank_area + (offset - 0x1580); }
  // Host: the driver's copy of entry #9 (see sound_set_sequences).
  std::vector<u8> effect_data;
};

// ---- What the programs call ----------------------------------------------------------

void sound_init(Sound* s);                                       // 0x800144ec (boot_main)
void sound_start(Sound* s);                                      // 0x80014954 (boot_main)
void sound_shutdown(Sound* s);                                   // 0x80014a7c
// 0x80018ff4: copies a bank's instrument table to its slot and DMAs its samples to SPU
// RAM. 'notify' / 'notify_arg' (an object called when the transfer ends) are 0 in the
// title program.
void sound_load_bank(Sound* s, const void* bank, int notify, int notify_arg);
int sound_bank_busy(Sound* s);                                   // 0x80018f04
void sound_set_sequences(Sound* s, void* data);                  // 0x80014508: entry #9 (effects)
void sound_set_volume(Sound* s, int music, int effects);         // 0x8001ab00
void sound_play_sequence(Sound* s, void* data);                  // 0x8001a5d8
void sound_resume_sequence(Sound* s, void* data, int fade_frames, int resume);  // 0x8001a65c
void sound_stop(Sound* s);                                       // 0x8001a650
void sound_fade_out(Sound* s, int frames);                       // 0x8001a7e4: the music
// 0x80019b1c: starts effect 'id' (< 0x1000000: entry #9's scripts; else the bank slot
// id >> 24 - 1). 'transpose' in semitones, 'volume_pan' = volume << 8 | pan (0x7f40).
// Returns the effect slot (1-8), 0 if none was free.
int sound_play_sfx(Sound* s, int id, int transpose, int volume_pan);
int sound_output_mode(Sound* s);                                 // 0x8001ab30 (+0x32 of sound_state): 0 surround, 1 stereo, 2 mono
void sound_set_output_mode(Sound* s, int mode);                  // 0x8001aaf0
void sound_set_pan(Sound* s, int pan, int track_mask);           // 0x8001a9e4
void sound_nop_8001aabc(Sound* s, int a, int b);                 // 0x8001aabc: empty in this build
// 0x800197ec: an effect clip (an effect sequence with its own bank, prologue voices):
// mode 2 streams its samples through SpuSt (bank slots 8 / 9), 1 loads them to SPU RAM
// (slots 4 / 5) and plays, 0 only loads (sound_play_sfx then plays id *out). Returns the
// effect slot (1-8), 0 when none, -1 when busy. The console patches the clip's ADPCM
// flag bytes in RAM while it streams; so does the port, in the caller's buffer.
int sound_open_clip(Sound* s, const void* clip, int mode, int* out);
// 0x800192fc: copies a clip / effect bank's effects into bank slot 'slot' (from effect
// number 'first'; 'keep' leaves them there for later imports) and its instruments,
// loading the samples to SPU RAM (slots 0-7) or, for the stream slots 8-11, taking the
// next free voice to stream through. Returns the sample data.
u8* sound_import_effects(Sound* s, u8* data, int slot, int keep, int first);
void sound_release_clip(Sound* s, int slot);                                                  // 0x8001a114
void sound_start_clip(Sound* s, int slot, int volume, int pan, int pitch_scale, int invert);  // 0x8001a4b8
void sound_fade_out_all(Sound* s, int frames);                                                // 0x8001a88c
void* sound_state(Sound* s);                                     // 0x8001ab30: Sound + 0x20
// Host: what the programs read through sound_state, whose bytes the port does not lay
// out like the console: +0x20 bit 0 (Sound + 0x40, the music is playing) and +0x32
// (the output mode, sound_output_mode).
int sound_music_playing(Sound* s);

// What the field program uses besides the above.
int sound_stream_busy(Sound* s);                                 // 0x80018f20: a clip is streaming
void sound_bank_slot_clear(Sound* s, int slot);                  // 0x80018f34
int sound_bank_free_space(Sound* s, int slot);                   // 0x80019194: 0xf400 - bytes loaded to the slot
void sound_load_effect_bank(Sound* s, void* data, int notify, int notify_arg, int slot, int first);  // 0x800191c0
void sound_add_effect_bank(Sound* s, void* data, int notify, int notify_arg, int slot, int first);   // 0x80019290
void sound_copy_remap(Sound* s, int slot, int to, int from);     // 0x800197c0: effect number remap
u32 sound_effect_playing(Sound* s, int slot);                    // 0x8001a0ac: the slot's id while it plays, else 0
void sound_clip_fade_out(Sound* s, int slot, int frames, int delay);  // 0x8001a2c8
void sound_clip_pitch_rise(Sound* s, int slot, int frames);      // 0x8001a350: pitch scale 0x2000 -> 0x6000
void sound_clip_pitch_drop(Sound* s, int slot, int frames);      // 0x8001a3e8: pitch scale 0x2000 -> 0
void sound_clip_duck(Sound* s, int slot, int ticks);             // 0x8001a480: volume factor 0 for 'ticks'
void sound_fade_in_all(Sound* s, int frames);                    // 0x8001a82c: the effects
void sound_pause(Sound* s, int floor, int frames);               // 0x8001a8ec: music fades to 'floor' (0: stops at once)
void sound_resume(Sound* s, int frames);                         // 0x8001a900
void sound_set_tempo(Sound* s, int adjust);                      // 0x8001a938: tempo * (1 + adjust / 64)
void sound_end_loops(Sound* s, int track_mask);                  // 0x8001aac4: the tracks leave their loops at the loop end
u8 sound_bank_loaded(Sound* s, int slot);                        // 0x8001ab48
void sound_set_bank_loaded(Sound* s, int slot, u8 value);        // 0x8001ab58
void sound_measure_subtick(Sound* s);                            // 0x8001ab64: +0x3390 from root counter 2
void sound_add_ticks(Sound* s, int n);                           // 0x8001abc0: extra passes for the next interrupt
SoundTrack* sound_tracks(Sound* s);                              // 0x8001ab38
SoundVoice* sound_voices(Sound* s);                              // 0x8001ab40

// ---- The driver ----------------------------------------------------------------------

long sound_interrupt();                                          // 0x80014524: root counter 2 handler
void sound_tick(Sound* s);                                       // 0x800152d4
void sound_transfer_done();                                      // 0x80018e84: SPU transfer callback

// Pitch table (boot 0x8002a9b4, 84 notes; index -1 is the word before it).
s32 sound_pitch_table(int note);

}  // namespace ss::game
