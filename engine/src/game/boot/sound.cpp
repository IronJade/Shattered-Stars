#include "game/boot/sound.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>

#include "runtime/boot_exe.h"
#include "runtime/libapi.h"

namespace ss::game {

using namespace psyq;

namespace {

Sound* g_sound = nullptr;  // 0x8002b6e0
// 0x8002b6d8 / 0x8002b6dc: an object (and its argument) told when a bank transfer
// ends; the title program passes none.
int g_transfer_notify = 0;
int g_transfer_notify_arg = 0;

// (Not Runtime::log_once: the driver is in ss_core, which tests link without a window.)
void log_once(const char* key, const char* message) {
  static std::set<std::string> logged;
  if (logged.insert(key).second) std::fprintf(stderr, "[port] %s\n", message);
}

void write_u16(u8* p, u16 v) {
  p[0] = static_cast<u8>(v);
  p[1] = static_cast<u8>(v >> 8);
}
void write_u32(u8* p, u32 v) {
  for (int i = 0; i < 4; ++i) p[i] = static_cast<u8>(v >> (8 * i));
}
s16 read_s16(const u8* p) { return static_cast<s16>(read_u16(p)); }

// Constant tables in the boot executable.
s32 boot_s16(u32 address) {
  const u8* p = runtime::boot_exe_data(address);
  if (!p) {
    log_once("sound-tables", "sound: the boot executable's tables are unavailable (no disc?); music will be silent");
    return 0;
  }
  return read_s16(p);
}
// LFO wave (0x8002aa8c): 64 steps of 256 * sin.
s32 lfo_table(u32 phase) { return boot_s16(0x8002AA8C + (phase & 0x3F) * 2); }
// Order the instrument search visits the bank slots in (0x8002b6c0; 0x8002b6cc with
// slots 4 and 5 swapped).
int bank_search_order(int table, int i) {
  const u8* p = runtime::boot_exe_data(0x8002B6C0 + static_cast<u32>(table * 12 + i));
  return p ? static_cast<s8>(*p) : 11 - i;
}

// SS_SOUND_TRACE=file: logs the driver's SpuSetKey / SpuSetVoiceAttr calls and the
// sequences it starts, with its interrupt count, in the format of the PCSX-Redux
// reference captures (to compare the port with the original event by event).
std::FILE* g_trace = nullptr;
void trace_key(long on_off, u32 mask) {
  if (mask) std::fprintf(g_trace, "tick %d key %ld %06x\n", g_sound ? g_sound->ticks : -1, on_off, mask);
}
void trace_voice(const SpuVoiceAttr* a) {
  std::fprintf(g_trace, "tick %d attr %06x mask=%x vol=%d,%d pitch=%04x addr=%05x adsr=%04x,%04x\n",
               g_sound ? g_sound->ticks : -1, a->voice, a->mask, a->volume.left, a->volume.right, a->pitch, a->addr,
               a->adsr1, a->adsr2);
}
void trace_stream(const char* what, u32 a, u32 b) {
  std::fprintf(g_trace, "tick %d st %s %x %x\n", g_sound ? g_sound->ticks : -1, what, a, b);
}
void trace_open() {
  static bool tried = false;
  if (tried) return;
  tried = true;
  if (const char* path = std::getenv("SS_SOUND_TRACE"); path && *path && (g_trace = std::fopen(path, "w"))) {
    spu_trace_key = trace_key;
    spu_trace_voice = trace_voice;
    spu_trace_stream = trace_stream;
  }
}

// Forward declarations, in address order.
void sound_stream_init(Sound* s);                                 // 0x8001454c
void sound_stream_prepared(u32 voice_bit, long status);           // 0x800145ec
void sound_stream_transferred(u32 voice_bit, long status);        // 0x80014648
void sound_stream_finished(u32 voice_bit, long status);           // 0x80014674
void sound_stream_advance(Sound* s, u32 voice_bit);               // 0x80014724
void sound_stream_update(Sound* s);                               // 0x80014830
void sound_reset(Sound* s);                                       // 0x80014b94
void sound_reverb_init(Sound* s);                                 // 0x80015000
void sound_apply_reverb_mode(Sound* s);                           // 0x80015040
void sound_apply_reverb_depth(Sound* s);                          // 0x800150c8
void sound_init_common_attr(Sound* s);                            // 0x80015120
void sound_apply_tempo(Sound* s);                                 // 0x80015188
void sound_run_tracks(Sound* s);                                  // 0x80015530
void sound_handle_requests(Sound* s);                             // 0x8001567c
void sound_save_state(Sound* s);                                  // 0x800159c0
void sound_restore_state(Sound* s);                               // 0x80015a74
void sound_update_lengths(Sound* s);                              // 0x80015b98
void sound_track_step(Sound* s, int t);                           // 0x80015c94
void sound_track_echo(Sound* s, int t);                           // 0x80016b28
void sound_fade_step(Sound* s, SoundFade* f, int first, int last);  // 0x80016ebc
void sound_update_voices(Sound* s);                               // 0x80017098
void sound_voice_select_region(Sound* s, int t, int v);           // 0x8001727c
const u8* sound_find_instrument(Sound* s, int t);                 // 0x800173f8
void sound_voice_pitch(Sound* s, int t, int v);                   // 0x800174d4
void sound_voice_volume(Sound* s, int t, int v);                  // 0x80017884
void sound_track_volume(Sound* s, int t, int v);                  // 0x800179c4
void sound_voice_pan(Sound* s, int t, int v);                     // 0x80017b00
void sound_track_vibrato(Sound* s, int t, int v);                 // 0x80017c10
void sound_voice_vibrato(Sound* s, int v);                        // 0x80017cf0
int sound_alloc_voice(Sound* s, int t, int* tied);                // 0x80017e58
void sound_release_held(Sound* s, int t);                         // 0x800180bc
void sound_rekey_voices(Sound* s);                                // 0x8001814c
bool sound_music_idle(Sound* s);                                  // 0x80018228
bool sound_track_load_pattern(Sound* s, int t);                   // 0x80018258
int sound_random(Sound* s, int n);                                // 0x800182c0
void sound_stop_music(Sound* s);                                  // 0x80018328
void sound_stop_tracks(Sound* s, u32 mask);                       // 0x80018374
void sound_stop_track(Sound* s, int t);                           // 0x800183e0
void sound_voice_off(Sound* s, int v);                            // 0x8001847c
void sound_sequence_init(Sound* s, int mode);                     // 0x80018560
void sound_track_reset(Sound* s, int t);                          // 0x80018780
int sound_find_free_track(Sound* s, int first, int last);         // 0x80018804
void sound_update_effects(Sound* s);                              // 0x80018850
int sound_start_effect_track(Sound* s, int seq, u16 volume_pan, s8 transpose, s8 table);  // 0x80018d48
void sound_set_transfer_notify(Sound* s, int notify, int arg);    // 0x80018eec
void sound_copy_instruments(Sound* s, u8* dst, const u8* src, u32 spu_base);  // 0x800196f8
int sound_alloc_effect(Sound* s, u32 id, u32 header);             // 0x80019c9c
void sound_free_voice(Sound* s, int v);                           // 0x8001a0e4
void sound_set_tracks(Sound* s, u8 value, u32 mask, u8 SoundTrack::*field, u8 dirty);  // 0x8001a948

// The ring of free voices: free_voices[0 .. 24 - reserved_voices], one slot spare.
u8* ring_next(Sound* s, u8* p) { return p == &s->free_voices[24 - s->reserved_voices] ? &s->free_voices[0] : p + 1; }

// ---- Streaming (SpuSt) ------------------------------------------------------------------

void sound_stream_init(Sound* s) {
  SpuStEnv* env = SpuStInit(0);
  s->stream_env = env;
  env->size = 0x100;
  s->stream_start = 0;
  s->stream_stop = 0;
  s->stream_hold = 0;
  s->stream_reset = 0;
  for (int i = 0; i < 24; ++i) env->voice[i].buf_addr = 0x70000u + static_cast<u32>(i) * 0x100;
  SpuStSetPreparationFinishedCallback(sound_stream_prepared);
  SpuStSetTransferFinishedCallback(sound_stream_transferred);
  SpuStSetStreamFinishedCallback(sound_stream_finished);
}

void sound_stream_prepared(u32 voice_bit, long) {
  g_sound->stream_busy = 1;
  g_sound->stream_delay = 0;
  sound_stream_advance(g_sound, voice_bit);
  SpuStTransfer(SPU_ST_START, voice_bit);
}

void sound_stream_transferred(u32 voice_bit, long) { sound_stream_advance(g_sound, voice_bit); }

void sound_stream_finished(u32 voice_bit, long) {
  SpuSetKey(SPU_OFF, voice_bit);
  if ((~voice_bit & SpuStGetVoiceStatus()) == 0) g_sound->stream_reset = 1;
  for (int v = 0; voice_bit != 0; voice_bit >>= 1, ++v) {
    if (voice_bit & 1) {
      sound_free_voice(g_sound, v);
      g_sound->voices[v].active = 0;
    }
  }
}

void sound_stream_advance(Sound* s, u32 voice_bit) {
  s->stream_timer = 0;
  for (int v = 0; voice_bit != 0; voice_bit >>= 1, ++v) {
    if (!(voice_bit & 1)) continue;
    SoundVoice& V = s->voices[v];
    s->stream_env->voice[v].data_addr += 0x80;
    V.stream_pos += 0x80;
    if (static_cast<s32>(V.stream_size) - 0x80 < static_cast<s32>(V.stream_pos)) {
      s->stream_env->voice[v].status = SPU_ST_STOP;
      s->stream_env->voice[v].last_size = static_cast<s32>(V.stream_size - V.stream_pos);
    }
  }
  if (s->stream_hold == 0) {
    for (int v = 0; s->stream_stop != 0; ++v, s->stream_stop >>= 1) {
      if (s->stream_stop & 1) {
        s->stream_env->voice[v].status = SPU_ST_STOP;
        s->stream_env->voice[v].last_size = 0x80;
      }
    }
  }
}

void sound_stream_update(Sound* s) {
  ++s->stream_timer;
  if (s->stream_busy && s->stream_timer > 400) {
    sound_stream_init(s);
    s->stream_reset = 1;
    SpuSetTransferCallback(sound_transfer_done);
  }
  if (s->stream_reset != 0 && --s->stream_reset == 0) {
    s->stream_busy = 0;
    s->stream_stopping = 0;
  }
  const u16 hold = static_cast<u16>(s->stream_hold - 1);
  if (hold < 0x62) s->stream_hold = static_cast<s16>(hold);
  const u32 mask = s->stream_start;
  if (mask != 0) {
    for (int v = 0; s->stream_start != 0; ++v, s->stream_start >>= 1) {
      if (s->stream_start & 1) {
        s->stream_env->voice[v].data_addr = s->voices[v].stream_data;
        s->stream_env->voice[v].status = SPU_ST_PLAY;
        s->voices[v].stream_pos = 0;
      }
    }
    SpuStTransfer(SPU_ST_PREPARE, mask);
    s->stream_hold = 1;
  }
}

// ---- Set-up -----------------------------------------------------------------------------

void sound_reset(Sound* s) {
  // memsets of +0x20 (0x78 bytes), the tracks, the voices, the requests and the effects.
  s->event = 0;
  s->ticks = 0;
  s->sequence = s->previous_sequence = nullptr;
  s->effect_sequences = s->effect_scripts = nullptr;
  s->flags = 0;
  s->music_fade = s->effects_fade = s->pause_fade = SoundFade{};
  s->bank_order = 0;
  s->clip_slot_used[0] = s->clip_slot_used[1] = 0;
  s->reverb_invert_l = s->reverb_invert_r = 0;
  s->loop_count = s->bar = s->bar_tick = s->loop_bar_tick = 0;
  s->stream_voice = s->releasing = s->restored_stopped = 0;
  s->transfer_timeout = 0;
  s->saved_sequence_id = 0;
  s->frame_phase = 0;
  s->rcnt_target = 0;
  s->output_mode = 0;
  s->sequence_tempo = 0;
  for (auto& t : s->tracks) t = SoundTrack{};
  for (auto& v : s->voices) v = SoundVoice{};
  s->request_restore = s->request_save = s->request_output_mode = s->request_tempo = 0;
  s->request_fade_in = s->request_fade_out = s->request_effects_fade_in = s->request_effects_fade_out = 0;
  s->request_stop = s->request_rekey = s->request_pause = s->request_resume = 0;
  s->request_volume = 0;
  s->output_mode_wanted = 0;
  s->tempo_adjust = 0;
  s->pause_floor = 0;
  s->pause_frames = s->music_fade_rate = s->effects_fade_rate = 0;
  s->saved_state = nullptr;
  for (auto& e : s->effects) e = SoundEffect{};

  s->base_tempo = 0x50;
  s->tempo = 0x50;
  s->music_fade.level = 0x7F;
  s->effects_fade.level = 0x7F;
  s->pause_fade.level = 0x7F;
  s->reverb_depth_l = 0x3FFF;
  s->reverb_depth_r = 0x3FFF;
  s->music_volume = 0x7F;
  s->effects_volume = 0x7F;
  s->random_a = static_cast<s16>(0x9371);
  s->random_b = 0x5713;
  s->free_write = &s->free_voices[0];
  s->free_read = &s->free_voices[1];

  // The twelve bank slots: instrument tables, SPU RAM areas and effect tables.
  struct SlotLayout {
    u32 instruments, spu, index, scripts, offsets, sequences, remap;
  };
  static constexpr SlotLayout kSlots[12] = {
      {0x1580, 0x33000, 0x2880, 0x28A8, 0x2D88, 0x2DB0, 0x3338},
      {0x17B4, 0x42400, 0x2998, 0x29C0, 0x2EA0, 0x2EC8, 0x334C},
      {0x19E8, 0x51800, 0x2AB0, 0x2AD8, 0x2FB8, 0x2FE0, 0x3360},
      {0x1C1C, 0x60C00, 0x2BC8, 0x2BF0, 0x30D0, 0x30F8, 0x3374},
      {0x1E50, 0x28000, 0x2CE0, 0x2CE4, 0x31E8, 0x31F0, 0},
      {0x1EA8, 0x2D800, 0x2CFC, 0x2D00, 0x3220, 0x3228, 0},
      {0x1F00, 0x01010, 0, 0, 0, 0, 0},
      {0x240C, 0x12000, 0, 0, 0, 0, 0},
      {0x2790, 0x70000, 0x2D18, 0x2D1C, 0x3258, 0x3260, 0},
      {0x27CC, 0x70100, 0x2D34, 0x2D38, 0x3290, 0x3298, 0},
      {0x2808, 0x70200, 0x2D50, 0x2D54, 0x32C8, 0x32D0, 0},
      {0x2844, 0x70300, 0x2D6C, 0x2D70, 0x3300, 0x3308, 0},
  };
  for (int i = 0; i < 12; ++i) {
    SoundBankSlot& b = s->banks[i];
    const SlotLayout& l = kSlots[i];
    b.instruments_base = s->at(l.instruments);
    b.spu_base = l.spu;
    b.index = l.index ? s->at(l.index) : nullptr;
    b.scripts_base = l.scripts ? s->at(l.scripts) : nullptr;
    b.sequence_offsets_base = l.offsets ? s->at(l.offsets) : nullptr;
    b.sequences_base = l.sequences ? s->at(l.sequences) : nullptr;
    b.remap = l.remap ? s->remap[(l.remap - 0x3338) / 20] : nullptr;
  }
  for (int i = 0; i < 12; ++i) {
    SoundBankSlot& b = s->banks[i];
    b.spu = b.spu_base;
    b.instruments = b.instruments_base;
    write_u32(b.instruments_base, 0xFFFFFFFF);
    if (i != 6 && i != 7) {
      std::memset(b.index, 0, static_cast<size_t>(b.scripts_base - b.index));
      b.scripts = b.scripts_base;
      b.sequence_offsets = b.sequence_offsets_base;
      b.sequences = b.sequences_base;
    }
  }
  for (int i = 0; i < 6; ++i) sound_bank_slot_clear(s, i);
  s->reserved_voices = 0;
  for (int i = 0; i < 24 - s->reserved_voices; ++i) s->free_voices[1 + i] = static_cast<u8>(i);
  s->free_write = &s->free_voices[0];
  s->clip_counter = 0;
  s->free_read = &s->free_voices[1];
  std::fill(std::begin(s->note_volume), std::end(s->note_volume), u8{0x60});
  for (int t = 24; t < 32; ++t) s->tracks[t].flags = kTrackEffect;
  for (auto& t : s->tracks) t.volume3 = 0x7F;
  SpuVoiceAttr attr;
  attr.voice = 0xFFFFFF;
  attr.mask = SPU_VOICE_VOLMODEL | SPU_VOICE_VOLMODER;
  SpuSetVoiceAttr(&attr);
  s->transfer_busy = s->stream_busy = s->stream_delay = s->stream_stopping = 0;
}

void sound_reverb_init(Sound* s) {
  s->reverb.mode = SPU_REV_MODE_HALL;
  s->reverb.delay = 0;
  s->reverb.feedback = 0;
  sound_apply_reverb_mode(s);
  sound_apply_reverb_depth(s);
}

void sound_apply_reverb_mode(Sound* s) {
  SpuReverbAttr current;
  SpuGetReverbModeParam(&current);
  if (current.mode == s->reverb.mode) return;
  SpuSetReverb(SPU_OFF);
  SpuSetReverbVoice(SPU_OFF, 0xFFFFFF);
  SpuClearReverbWorkArea(s->reverb.mode);
  s->reverb.mask = SPU_REV_MODE | SPU_REV_DELAYTIME | SPU_REV_FEEDBACK;
  if (SpuSetReverbModeParam(&s->reverb) != -1) SpuSetReverb(SPU_ON);
}

void sound_apply_reverb_depth(Sound* s) {
  s16 l = s->reverb_depth_l, r = s->reverb_depth_r;
  if (s->reverb_invert_l) l = static_cast<s16>(-l);
  if (s->reverb_invert_r) r = static_cast<s16>(-r);
  s->reverb.depth.left = l;
  s->reverb.depth.right = r;
  s->reverb.mask = SPU_REV_DEPTHL | SPU_REV_DEPTHR;
  SpuSetReverbDepth(&s->reverb);
}

void sound_init_common_attr(Sound* s) {
  SpuCommonAttr& c = s->common;
  c.mvol = {0x3FFF, 0x3FFF};
  c.mvolmode = {0, 0};
  c.mvolx = {0x3FFF, 0x3FFF};
  c.cd.volume = {0x7FFF, 0x7FFF};
  c.cd.reverb = 0;
  c.cd.mix = 1;
  c.ext.volume = {0x7FFF, 0x7FFF};
  c.ext.reverb = 0;
  c.ext.mix = 1;
  c.mask = 0;
  SpuSetCommonAttr(&c);
}

// Tempo (beats a minute, adjusted by the sequence's 0x82 and the game's tempo_adjust,
// each in 1/64 steps) to root counter 2's period: music ticks come at about 48 per
// beat. Slow tempos double or quadruple the rate (the period must fit 16 bits) and the
// tick then skips interrupts.
void sound_apply_tempo(Sound* s) {
  s32 m = s->sequence_tempo;
  m = m < 0 ? m * 512 : m * 1024;
  s32 t = s->base_tempo + ((s->base_tempo * m) >> 16);
  m = s->tempo_adjust;
  m = m < 0 ? m * 512 : m * 1024;
  t = t + ((t * m) >> 16);
  const s32 capped = std::min(t, 0xF0);
  s->tempo = static_cast<u8>(capped);
  s32 rate = capped;
  if (t < 0x50 && (rate = capped * 2) < 0x50) rate = capped * 4;
  if (rate == 0) rate = 1;  // the console traps on the division
  s->rcnt_target = static_cast<u16>(125000000 / (rate * 24));
  s->frame_ticks = 125000000 / (rate * 0x18D4);
  SetRCnt(RCntCNT2, s->rcnt_target, RCntMdINTR);
}

// ---- Tick -------------------------------------------------------------------------------

void sound_run_tracks(Sound* s) {
  int first, last;
  if (s->tick_kind == 1) {
    first = 0;
    last = 24;
  } else {
    first = s->tick_kind == 2 ? 24 : 0;
    last = 32;
  }
  for (int t = first; t < last; ++t) {
    SoundTrack& T = s->tracks[t];
    if (!T.active) continue;
    u8 phase = static_cast<u8>(T.lfo_phase + T.vibrato_rate);
    if (phase > 0x3F) phase = static_cast<u8>(phase - 0x40);
    T.lfo_phase = phase;
    const u8 wait = T.wait;
    T.notes = 0;
    if (wait == 0 || (T.wait = static_cast<u8>(wait - 1), wait == 1)) {
      do {
        if (!T.active) break;
        sound_track_step(s, t);
      } while (T.wait == 0);
    }
    const u8 dirty = T.dirty;
    T.dirty = 0;
    T.voices += T.notes;
    T.dirty_latched = dirty;
  }
}

void sound_handle_requests(Sound* s) {
  if (s->request_restore | s->request_save | s->request_output_mode | s->request_tempo) {
    if (s->request_restore) {
      if (s->flags & 1) {
        sound_stop_music(s);  // the music must stop before the saved state comes back
        return;
      }
      s->request_restore = 0;
      sound_restore_state(s);
    }
    if (s->request_save) {
      s->request_save = 0;
      sound_save_state(s);
    }
    if (s->request_output_mode) {
      s->request_output_mode = 0;
      s->output_mode = s->output_mode_wanted;
      for (auto& t : s->tracks) t.dirty |= 0x32;
    }
    if (s->request_tempo) {
      s->request_tempo = 0;
      sound_apply_tempo(s);
    }
  }
  if (s->request_fade_in | s->request_fade_out | s->request_effects_fade_in | s->request_effects_fade_out) {
    if (s->request_fade_in || s->request_fade_out) {
      if (s->request_fade_in) {
        s->request_fade_in = 0;
        s->music_fade.level = 0;
        s->music_fade.direction = 1;
      } else {
        s->request_fade_out = 0;
        s->music_fade.floor = 0;
        s->music_fade.direction = -1;
      }
      s->music_fade.fraction = 0;
      s->music_fade.rate = s->music_fade_rate;
    }
    if (s->request_effects_fade_in || s->request_effects_fade_out) {
      if (s->request_effects_fade_in) {
        s->request_effects_fade_in = 0;
        s->effects_fade.level = 0;
        s->effects_fade.direction = 1;
      } else {
        s->request_effects_fade_out = 0;
        s->effects_fade.floor = 0;
        s->effects_fade.direction = -1;
      }
      s->effects_fade.fraction = 0;
      s->effects_fade.rate = s->effects_fade_rate;
    }
  }
  if (s->request_stop | s->request_rekey | s->request_pause | s->request_resume) {
    if (s->request_stop) {
      s->request_stop = 0;
      sound_stop_music(s);
    }
    if (s->request_rekey) {
      s->request_rekey = 0;
      sound_rekey_voices(s);
    }
    if (s->request_pause) {
      s->request_pause = 0;
      if (s->pause_frames == 0) {
        s->flags &= ~1u;
        for (int v = s->reserved_voices; v < 24; ++v)
          if (s->voices[v].active && !(s->voices[v].flags & kTrackEffect)) sound_voice_off(s, v);
      } else {
        s->pause_fade.fraction = 0;
        s->pause_fade.direction = -1;
        s->pause_fade.floor = static_cast<s8>(s->pause_floor);
        s->pause_fade.rate = static_cast<s16>(0x7800 / s->pause_frames);
      }
    }
    if (s->request_resume) {
      s->request_resume = 0;
      if (s->pause_frames == 0) {
        sound_rekey_voices(s);
        s->flags |= 1;
      } else {
        s->pause_fade.fraction = 0;
        s->pause_fade.direction = 1;
        s->pause_fade.rate = static_cast<s16>(0x7800 / s->pause_frames);
      }
    }
  }
  if (s->request_volume) {
    s->request_volume = 0;
    for (auto& t : s->tracks) t.dirty |= 0x32;
  }
}

void sound_save_state(Sound* s) {
  SoundSavedState* st = s->saved_state;
  if (!st) return;
  std::copy(s->tracks, s->tracks + 24, st->tracks);
  st->sequence = s->sequence;
  st->reverb_depth_l = s->reverb_depth_l;
  st->reverb_depth_r = s->reverb_depth_r;
  st->loop_count = s->loop_count;
  st->bar = s->bar;
  st->bar_tick = s->bar_tick;
  st->loop_bar_tick = s->loop_bar_tick;
  st->base_tempo = s->base_tempo;
  st->sequence_tempo = s->sequence_tempo;
  st->flags = static_cast<u8>(s->flags);
  st->sequence_id = s->sequence ? read_u32(s->sequence + 4) : 0;
}

void sound_restore_state(Sound* s) {
  SoundSavedState* st = s->saved_state;
  if (!st) return;
  std::copy(st->tracks, st->tracks + 24, s->tracks);
  s->previous_sequence = st->sequence;
  s->reverb_depth_l = st->reverb_depth_l;
  s->reverb_depth_r = st->reverb_depth_r;
  s->loop_count = st->loop_count;
  s->bar = st->bar;
  s->bar_tick = st->bar_tick;
  s->loop_bar_tick = st->loop_bar_tick;
  s->base_tempo = st->base_tempo;
  s->sequence_tempo = st->sequence_tempo;
  if (!(st->flags & 1)) s->restored_stopped = 1;
  s->saved_sequence_id = st->sequence_id;
  s->flags &= ~1u;
  delete st;
  s->saved_state = nullptr;
  DisableEvent(s->event);
  s->key_off = 0;
  sound_stop_music(s);
  SpuSetKey(SPU_OFF, s->key_off);
  EnableEvent(s->event);
}

// Note lengths count down on the ticks of their class; a note that ends while its
// track holds the sustain pedal (0x89) is held instead.
void sound_update_lengths(Sound* s) {
  for (int v = 0; v < 24; ++v) {
    SoundVoice& V = s->voices[v];
    const u8 length = V.length;
    if (length == 0 || !V.active) continue;
    if (s->tick_kind != 3 && (V.flags & kTrackEffect) != s->tick_class) continue;
    V.length = static_cast<u8>(length - 1);
    if (length != 1) continue;
    if (!(s->tracks[V.track].flags & kTrackSustain)) {
      sound_voice_off(s, v);
    } else {
      ++V.hold;
      ++V.length;
    }
  }
}

// One sequence command (formats.md "Sound sequences").
void sound_track_step(Sound* s, int t) {
  SoundTrack& T = s->tracks[t];
  const u8* pc = T.pc;
  const u8 op = pc[0];
  if (op < 0x80) {
    T.note = op;
    T.wait = pc[1];
    int tied;
    const int v = sound_alloc_voice(s, t, &tied);
    if (v == -1) {
      T.pc += T.note_format;
      if (T.note_format == 1) T.pc += 2;
      return;
    }
    SoundVoice& V = s->voices[v];
    if (tied) {
      // The same note again on a voice still sounding: it just plays on.
      V.length = T.note_format < 3 ? T.fixed_length : pc[2];
      V.active = T.active;
      V.flags = T.flags;
      T.pc += T.note_format;
      if (T.note_format == 1) T.pc += 2;
      return;
    }
    T.detune = 0;
    T.dirty |= 0x0F;
    ++T.notes;
    if ((s->flags & 8) || (T.humanize != 0 && static_cast<s32>(sound_random(s, 0x80) & 0xFF) < T.humanize))
      T.detune = static_cast<s8>(sound_random(s, 5) - 2);
    T.random_fine = 0;
    if (T.flags & kTrackRandomPitch) T.random_fine = static_cast<s8>(sound_random(s, 7) - 3);
    V.note = T.note;
    switch (T.note_format) {
      case 2:
        V.velocity = T.fixed_velocity;
        V.length = T.fixed_length;
        T.pc += 2;
        break;
      case 1:
        V.velocity = pc[2];
        V.length = T.fixed_length;
        T.pc += 3;
        break;
      case 3:
        V.velocity = T.fixed_velocity;
        V.length = pc[2];
        T.pc += 3;
        break;
      default:
        V.velocity = pc[3];
        V.length = pc[2];
        T.pc += 4;
        break;
    }
    V.flags = T.flags;
    V.hold = 0;
    V.active = T.active;
    V.track = static_cast<u8>(t);
    V.vibrato_delay = T.vibrato_delay;
    V.vibrato_depth = T.vibrato_depth;
    V.vibrato_rate = T.vibrato_rate;
    V.note_pan = T.note_pan;
    std::copy(std::begin(T.voice_param), std::end(T.voice_param), V.voice_param);
    V.priority = T.priority;
    V.vibrato_level = 0;
    V.vibrato_phase = 0;
    if (T.flags & kTrackNoKeyOn) return;
    if (T.flags & kTrackStream) {
      s->stream_start |= 1u << v;
      V.stream_data = T.stream_data;
      V.stream_size = T.stream_size;
      return;
    }
    if (T.instrument == nullptr) return;
    s->key_on |= 1u << v;
    return;
  }

  auto on_off = [&](u32 bit) { T.flags = pc[2] ? T.flags | bit : T.flags & ~bit; };
  switch (op) {
    case 0x80:  // end of pattern
      T.note_format = 4;
      if (!(T.flags & kTrackEffect)) {
        ++T.pattern;
        if (t == 0) {
          s->bar_tick = 0;
          s->bar = T.pattern;
        }
        if (sound_track_load_pattern(s, t) && sound_music_idle(s)) sound_stop_music(s);
      } else {
        sound_stop_track(s, t);
      }
      break;
    case 0x81:  // wait, priority
      T.wait = pc[1];
      T.priority = pc[2];
      T.pc += 3;
      break;
    case 0x82:  // wait, tempo change
      T.wait = pc[1];
      s->sequence_tempo = static_cast<s8>(pc[2]);
      sound_apply_tempo(s);
      T.pc += 3;
      break;
    case 0x83: {  // wait, program (u16)
      T.dirty |= kDirtyProgram;
      T.wait = pc[1];
      T.program = static_cast<u16>(pc[2] | pc[3] << 8);
      const u8* inst = sound_find_instrument(s, t);
      T.instrument = inst;
      if (inst) {
        T.adsr1 = read_u16(inst + 2);
        T.adsr2 = read_u16(inst + 4);
        T.instrument = inst + 7;
      }
      T.pc += 4;
      break;
    }
    case 0x84:  // wait, pitch bend
      T.dirty |= kDirtyPitch;
      T.wait = pc[1];
      T.bend = static_cast<s8>(pc[2]);
      T.pc += 3;
      break;
    case 0x85:  // wait, volume
      T.dirty |= 0x12;
      T.wait = pc[1];
      T.volume = pc[2];
      T.pc += 3;
      break;
    case 0x86:  // wait, expression
      T.dirty |= 0x12;
      T.wait = pc[1];
      T.expression = pc[2];
      T.pc += 3;
      break;
    case 0x87:  // wait, note pan
      T.dirty |= 0x32;
      T.wait = pc[1];
      T.note_pan = pc[2];
      T.pc += 3;
      break;
    case 0x88:  // wait, vibrato depth
      T.wait = pc[1];
      if (T.lfo_depth == 0) T.lfo_phase = 0;
      T.lfo_depth = pc[2];
      T.pc += 3;
      break;
    case 0x89:  // wait, sustain pedal
      T.wait = pc[1];
      if (pc[2] == 0) {
        T.flags &= ~kTrackSustain;
        sound_release_held(s, t);
      } else {
        T.flags |= kTrackSustain;
      }
      T.pc += 3;
      break;
    case 0x8A: {  // wait, reverb depth
      T.wait = pc[1];
      const s16 depth = static_cast<s16>(pc[2] << 8);
      s->reverb_depth_l = depth;
      s->reverb_depth_r = depth;
      sound_apply_reverb_depth(s);
      T.pc += 3;
      break;
    }
    case 0x8D:  // loop start
      T.loop_count = 0xFF;
      T.loop_pattern = T.pattern;
      T.loop_pc = T.pc;
      if (t == 0) s->loop_bar_tick = s->bar_tick;
      T.pc += 1;
      break;
    case 0x8E:  // loop end: count (0 = forever)
      if (T.loop_count == 0xFF) T.loop_count = pc[1];
      --T.loop_count;
      if (T.loop_count == 0) {
        T.pc += 2;
      } else {
        T.pattern = T.loop_pattern;
        T.pc = T.loop_pc;
        if (t == 0) {
          ++s->loop_count;
          s->bar_tick = s->loop_bar_tick;
        }
        T.pc += 1;
      }
      break;
    case 0x8F:  // rest
      T.wait = pc[1];
      T.pc += 2;
      break;
    case 0x90:  // wait, reverb on / off
      T.wait = pc[1];
      on_off(kTrackReverb);
      T.pc += 3;
      break;
    case 0x91:  // wait, reverb phase
      T.wait = pc[1];
      s->reverb_invert_l = pc[2] == 1 || pc[2] == 3;
      s->reverb_invert_r = pc[2] == 2 || pc[2] == 3;
      sound_apply_reverb_depth(s);
      T.pc += 3;
      break;
    case 0x92:  // wait, surround phase
      T.dirty |= 0x12;
      T.wait = pc[1];
      switch (pc[2]) {
        case 1: T.flags = (T.flags | kTrackInvertLeft) & ~kTrackInvertRight; break;
        case 2: T.flags = (T.flags & ~kTrackInvertLeft) | kTrackInvertRight; break;
        case 3: T.flags |= kTrackInvertLeft | kTrackInvertRight; break;
        default: T.flags &= ~(kTrackInvertLeft | kTrackInvertRight); break;
      }
      T.pc += 3;
      break;
    case 0x93:  // wait, vibrato on / off
      T.wait = pc[1];
      on_off(kTrackVibrato);
      T.pc += 3;
      break;
    case 0x94:  // wait, vibrato delay, depth, rate
      T.wait = pc[1];
      T.vibrato_delay = pc[2];
      T.vibrato_depth = pc[3];
      T.vibrato_rate = pc[4];
      T.pc += 5;
      break;
    case 0x95:  // wait, fine tune
      T.dirty |= kDirtyPitch;
      T.wait = pc[1];
      T.fine = static_cast<s8>(pc[2]);
      T.pc += 3;
      break;
    case 0x96:  // wait, bend range
      T.wait = pc[1];
      T.bend_range = pc[2];
      T.pc += 3;
      break;
    case 0x97:  // wait, ADSR (two u16)
      T.dirty |= kDirtyProgram;
      T.wait = pc[1];
      T.adsr1 = static_cast<u16>(pc[2] | pc[3] << 8);
      T.adsr2 = static_cast<u16>(pc[4] | pc[5] << 8);
      T.pc += 6;
      break;
    case 0x99:  // wait, random pitch on / off
      T.wait = pc[1];
      on_off(kTrackRandomPitch);
      T.pc += 3;
      break;
    case 0x9A:  // wait, echo on / off
      T.wait = pc[1];
      if (pc[2] == 0) {
        if (T.flags & kTrackEcho) {
          T.flags &= ~kTrackEcho;
          T.active = 0;
        }
      } else if (!(T.flags & kTrackEcho)) {
        sound_track_echo(s, t);
      }
      T.pc += 3;
      break;
    case 0x9B:  // wait, echo delay, transpose, fine, volume, note pan
      T.wait = pc[1];
      T.echo_wait = pc[2];
      T.echo_transpose = pc[3];
      T.echo_fine = pc[4];
      T.echo_volume = pc[5];
      T.echo_note_pan = pc[6];
      T.pc += 7;
      break;
    case 0x9E:  // note format: fixed length, fixed velocity (no wait)
      T.fixed_length = pc[1];
      T.fixed_velocity = pc[2];
      if (T.fixed_length == 0) T.note_format = T.fixed_velocity == 0 ? 4 : 3;
      else T.note_format = T.fixed_velocity == 0 ? 1 : 2;
      T.pc += 3;
      break;
    default:
      // 0x8b, 0x8c, 0x98, 0x9c, 0x9d and up do nothing on the console, not even advance,
      // so the track spins; stop it instead.
      log_once("sound-op", "sound: unknown sequence command; track stopped");
      T.active = 0;
      break;
  }
}

// 0x9a: a delayed copy of the track on a free track of the same kind.
void sound_track_echo(Sound* s, int t) {
  const int e = t < 24 ? sound_find_free_track(s, 0, 24) : sound_find_free_track(s, 24, 32);
  if (e == -1) return;
  const SoundTrack& src = s->tracks[t];
  SoundTrack& E = s->tracks[e];
  E = src;
  E.flags |= kTrackEcho;
  E.wait = static_cast<u8>(E.wait + src.echo_wait);
  E.transpose = static_cast<s8>(E.transpose + src.echo_transpose);
  E.fine = static_cast<s8>(src.echo_fine);
  E.volume = src.echo_volume;
  E.note_pan = src.echo_note_pan;
  if (e < t) {  // already passed in this tick: catch up
    E.notes = 0;
    if (E.wait != 0) --E.wait;
    while (E.wait == 0 && E.active) sound_track_step(s, e);
    E.voices += E.notes;
    E.dirty_latched = E.dirty;
    E.dirty = 0;
  }
}

void sound_fade_step(Sound* s, SoundFade* f, int first, int last) {
  if (f->direction == 0) return;
  auto mark = [&] {
    for (int i = first; i < last; ++i) s->tracks[i].dirty |= 0x12;
  };
  if (f->direction == 1) {
    if (f->level == 0x7F) {
      f->direction = 0;
      return;
    }
    const u16 acc = static_cast<u16>(f->fraction + f->rate);
    f->fraction = acc;
    if (static_cast<s16>(acc) > 0xFF) {
      const u32 level = static_cast<u32>(f->level + (static_cast<s16>(acc) >> 8));
      f->level = static_cast<u8>(level);
      if ((level & 0xFF) > 0x7F) f->level = 0x7F;
      f->fraction = acc & 0xFF;
      mark();
    }
  } else if (static_cast<s8>(f->level) > f->floor) {
    const u16 acc = static_cast<u16>(f->fraction + f->rate);
    f->fraction = acc;
    if (static_cast<s16>(acc) > 0xFF) {
      const s32 level = f->level - (static_cast<s16>(acc) >> 8);
      f->level = static_cast<u8>(level);
      if (static_cast<s8>(level) < f->floor) f->level = static_cast<u8>(f->floor);
      f->fraction = acc & 0xFF;
      mark();
    }
  } else {
    f->direction = 0;
    if (f == &s->music_fade) {
      sound_stop_music(s);
    } else if (f == &s->effects_fade) {
      sound_stop_tracks(s, 0xFF000000);
      s->effects_fade.level = 0x7F;
    }
  }
}

// Pushes the changed pitch / volume / sample / ADSR of every voice of this tick's
// class to libspu, and the reverb mask.
void sound_update_voices(Sound* s) {
  u32 reverb = 0;
  SpuVoiceAttr attr;
  attr.mask = SPU_VOICE_VOLL | SPU_VOICE_VOLR | SPU_VOICE_PITCH | SPU_VOICE_WDSA | SPU_VOICE_ADSR_ADSR1 |
              SPU_VOICE_ADSR_ADSR2;
  for (int v = 23; v >= 0; --v) {
    reverb <<= 1;
    SoundVoice& V = s->voices[v];
    if (!V.active) continue;
    if (V.flags & kTrackReverb) reverb += 1;
    if (s->tick_kind != 3 && (V.flags & kTrackEffect) != s->tick_class) continue;
    const int t = V.track;
    SoundTrack& T = s->tracks[t];
    u8 dirty = T.dirty_latched;
    if (dirty & kDirtyRegion) sound_voice_select_region(s, t, v);
    if (dirty & kDirtyPitch) sound_voice_pitch(s, t, v);
    if (dirty & kDirtyVolume) sound_voice_volume(s, t, v);
    if (T.lfo_depth != 0) {
      sound_track_vibrato(s, t, v);
      dirty |= kDirtyPitch;
    }
    if (V.flags & kTrackVibrato) {
      sound_voice_vibrato(s, v);
      dirty |= kDirtyPitch;
    }
    if (dirty != 0) {
      attr.voice = 1u << v;
      attr.addr = V.region ? read_u32(V.region + 4) : 0;
      attr.adsr1 = T.adsr1;
      attr.adsr2 = T.adsr2;
      attr.pitch = V.pitch;
      attr.volume = V.volume;
      SpuSetVoiceAttr(&attr);
    }
  }
  SpuSetReverbVoice(SPU_ON, reverb);
  SpuSetReverbVoice(SPU_OFF, ~reverb);
}

// The region of the program that covers the note and velocity (the first one if none
// does). Regions: note low / high, velocity low / high, sample address, loop address,
// volume, coarse and fine tune, then (kTrackRegionNote) the note, length and pan to use.
void sound_voice_select_region(Sound* s, int t, int v) {
  const SoundTrack& T = s->tracks[t];
  SoundVoice& V = s->voices[v];
  const u8* inst = T.instrument;
  if (!inst) {
    V.region = nullptr;
    return;
  }
  s32 note = V.note;
  if (!(V.flags & kTrackFixedNote)) note += T.transpose + T.detune + T.transpose2;
  const u8* r = inst + 1;
  for (int i = 0; i < inst[0]; ++i, r += 20) {
    if (r[0] <= note && note <= r[1] && r[2] <= V.velocity && V.velocity <= r[3]) {
      V.region = r;
      if (V.flags & kTrackRegionNote) {
        V.note = r[0xF];
        V.length = r[0x10];
        V.note_pan = r[0x11];
      }
      return;
    }
  }
  V.region = T.instrument + 1;
}

// The track's program in the bank slots, searched in bank order. Entries: u16 id,
// u16 ADSR1, u16 ADSR2, a byte, the region count, the regions; 0xffff ends a table.
// Programs in the clip slots (8-11) are used once.
const u8* sound_find_instrument(Sound* s, int t) {
  const int table = s->bank_order ? 1 : 0;
  for (int i = 0; i < 12; ++i) {
    const int slot = bank_search_order(table, i);
    u8* p = s->banks[slot].instruments_base;
    for (s16 id; (id = read_s16(p)) != -1; p += p[7] * 20 + 8) {
      if (id == static_cast<s16>(s->tracks[t].program)) {
        if (static_cast<u32>(slot - 8) > 3) return p;
        write_u16(p, 0xFFFF);
        return p;
      }
    }
  }
  return nullptr;
}

// Note (+ transpose + the region's coarse tune) from the pitch table, then the pitch
// bend (bend * bend range, in 1/64 semitones) and fine tune interpolated towards the
// neighbouring semitones; notes outside the table's seven octaves fold back into it.
void sound_voice_pitch(Sound* s, int t, int v) {
  const SoundTrack& T = s->tracks[t];
  SoundVoice& V = s->voices[v];
  if (!(V.flags & kTrackFixedNote)) V.transpose = static_cast<s8>(T.transpose + T.detune + T.transpose2);
  else V.transpose = 0;
  auto fold = [](s32 n) {
    while (n < 0) n += 12;
    while (n > 0x53) n -= 12;
    return n;
  };
  const s32 region_coarse = V.region ? static_cast<s8>(V.region[0xD]) : 0;
  const s32 region_fine = V.region ? static_cast<s8>(V.region[0xE]) : 0;
  s32 n = fold(V.note + V.transpose + region_coarse);
  s32 base = sound_pitch_table(n) << 15;
  s32 bend = T.bend * T.bend_range + T.coarse * 12;
  s32 bend_part = 0;
  if (bend != 0) {
    s32 p1, p2;
    if (bend < 0) {
      bend = -bend;
      n = fold(n - (bend >> 6));
      p1 = sound_pitch_table(n);
      p2 = sound_pitch_table(n - 1);
    } else {
      n = fold(n + (bend >> 6));
      p1 = sound_pitch_table(n);
      p2 = sound_pitch_table(n + 1);
    }
    base = p1 * 0x8000;
    bend_part = ((p2 * 0x8000 - p1 * 0x8000) >> 6) * (bend & 0x3F);
  }
  const s32 fine = T.fine + T.fine2 + T.random_fine + region_fine;
  s32 fine_part = 0;
  if (fine != 0) {
    if (fine < 0) {
      const s32 k = 1 - ((fine + 0x3F) >> 6);
      fine_part = (base - sound_pitch_table(n - k) * 0x8000) / (k * 0x40);
    } else {
      const s32 k = (fine >> 6) + 1;
      fine_part = (sound_pitch_table(n + k) * 0x8000 - base) / (k * 0x40);
    }
    fine_part *= fine;
  }
  const u32 pitch = static_cast<u32>((base + bend_part + fine_part) >> 15) & 0xFFFF;
  V.base_pitch = static_cast<u16>(pitch);
  if (T.pitch_scale == 0) {
    while (V.base_pitch > 0x3FFF) V.base_pitch >>= 1;
  } else {
    s32 scaled = static_cast<s32>(pitch * static_cast<u32>(static_cast<s32>(T.pitch_scale))) >> 13;
    if (scaled > 0x3FFF) scaled = 0x3FFF;
    V.base_pitch = static_cast<u16>(scaled);
  }
  V.note_index = static_cast<u8>(n);
  V.pitch = V.base_pitch;
}

// Velocity x region volume x track volume x the note's volume, split by pan; phases
// inverted for surround.
void sound_voice_volume(Sound* s, int t, int v) {
  const SoundTrack& T = s->tracks[t];
  if (T.dirty_latched & 0x12) sound_track_volume(s, t, v);
  SoundVoice& V = s->voices[v];
  const u32 region_volume = V.region ? V.region[0xC] : 0;
  const u32 volume = (V.velocity * region_volume * V.track_volume * s->note_volume[V.note >> 2]) >> 21;
  const u32 packed = volume * V.right * 0x10000 + volume * V.left;
  V.volume_packed = packed;
  V.volume.left = static_cast<s16>(packed);
  V.volume.right = static_cast<s16>(packed >> 16);
  if (s->output_mode == 0) {
    if (T.flags & kTrackInvertLeft) V.volume.left = static_cast<s16>(-V.volume.left);
    if (T.flags & kTrackInvertRight) V.volume.right = static_cast<s16>(-V.volume.right);
  }
}

void sound_track_volume(Sound* s, int t, int v) {
  const SoundTrack& T = s->tracks[t];
  if (T.dirty_latched & 0x32) sound_voice_pan(s, t, v);
  u32 x = (static_cast<u32>(T.volume) * T.expression * T.volume3 * T.volume4) >> 21;
  if (t < 24) {
    x = (x * s->music_volume * s->music_fade.level * s->pause_fade.level) >> 21;
  } else {
    x = (x * s->effects_volume * s->effects_fade.level) >> 13;
    if (x > 0xFF) x = 0xFF;
  }
  s->voices[v].track_volume = static_cast<u8>(x);
}

void sound_voice_pan(Sound* s, int t, int v) {
  SoundVoice& V = s->voices[v];
  if (s->output_mode == 2) {  // mono
    V.left = 0x40;
    V.right = 0x40;
    return;
  }
  const s32 centre = static_cast<s8>(s->tracks[t].pan);
  s32 pan;
  if (!(V.flags & kTrackPanByNote)) {
    pan = static_cast<s8>(V.note_pan);
  } else {
    pan = std::clamp((V.note - 0x20) * 2, 0, 0x7F);
  }
  s32 width = centre;
  if (centre - 0x40 >= 0) width = 0x80 - centre;
  s32 x = (pan - 0x40) * width;
  if (x < 0) x += 0x3F;
  V.right = static_cast<u8>(centre + (x >> 6));
  V.left = static_cast<u8>(0x7F - V.right);
}

// Vibrato: the LFO moves the pitch towards the next semitone up or down.
void apply_lfo(SoundVoice& V, s32 x) {
  if (x < 0) {
    x = -x;
    const u32 diff = static_cast<u32>(V.base_pitch - sound_pitch_table(V.note_index - 1));
    V.pitch = static_cast<u16>(V.base_pitch - static_cast<s16>(static_cast<s32>(diff * static_cast<u32>(x)) >> 15));
  } else {
    const u32 diff = static_cast<u32>(sound_pitch_table(V.note_index + 1) - V.base_pitch);
    V.pitch = static_cast<u16>(V.base_pitch + static_cast<s16>(static_cast<s32>(diff * static_cast<u32>(x)) >> 15));
  }
}

void sound_track_vibrato(Sound* s, int t, int v) {
  const SoundTrack& T = s->tracks[t];
  apply_lfo(s->voices[v], lfo_table(T.lfo_phase) * static_cast<s8>(T.lfo_depth));
}

void sound_voice_vibrato(Sound* s, int v) {
  SoundVoice& V = s->voices[v];
  if (V.vibrato_delay != 0) {
    --V.vibrato_delay;
    return;
  }
  u8 phase = static_cast<u8>(V.vibrato_phase + V.vibrato_rate);
  if (phase > 0x3F) phase = static_cast<u8>(phase - 0x40);
  V.vibrato_phase = phase;
  const s32 depth = static_cast<s8>(V.vibrato_depth);
  if (V.vibrato_level != depth) {
    const s32 level = V.vibrato_level + 1 + (depth >> 5);
    V.vibrato_level = static_cast<s8>(level);
    if (depth < static_cast<s8>(level)) V.vibrato_level = static_cast<s8>(depth);
  }
  apply_lfo(V, lfo_table(V.vibrato_phase) * V.vibrato_level);
}

// A voice for the track's note: the voice already playing that note (ties), else the
// next free voice, else the voice of the highest priority value if it is above the
// track's. -1 = none.
int sound_alloc_voice(Sound* s, int t, int* tied) {
  SoundTrack& T = s->tracks[t];
  *tied = 0;
  if (T.voices != 0 && !(T.flags & kTrackRegionNote)) {
    for (int v = 0; v < 24; ++v) {
      SoundVoice& V = s->voices[v];
      if (V.note == static_cast<u32>(static_cast<s8>(T.note)) && V.track == static_cast<u8>(t) && V.active) {
        if (V.hold == 0) {
          *tied = 1;
          return v;
        }
        sound_voice_off(s, v);
      }
    }
  }
  if (T.flags & kTrackStream) return T.stream_voice;
  if (s->free_read == s->free_write) {
    int best = -1;
    s8 highest = 0;
    for (int v = s->reserved_voices; v < 24; ++v) {
      if (highest < static_cast<s8>(s->voices[v].priority)) {
        best = v;
        highest = static_cast<s8>(s->voices[v].priority);
      }
    }
    if (best < 0 || static_cast<s8>(T.priority) >= highest) return -1;
    sound_voice_off(s, best);
    s->free_read = ring_next(s, s->free_read);
    return best;
  }
  const int v = *s->free_read;
  if ((1u << v) & SpuStGetVoiceStatus()) return -1;
  s->free_read = ring_next(s, s->free_read);
  return v;
}

void sound_release_held(Sound* s, int t) {
  for (int v = 0; v < 24; ++v) {
    const SoundVoice& V = s->voices[v];
    if (V.active && V.track == static_cast<u8>(t) && V.hold != 0) sound_voice_off(s, v);
  }
}

// Keys long sustained notes on again (after a pause).
void sound_rekey_voices(Sound* s) {
  for (int v = s->reserved_voices; v < 24; ++v) {
    const SoundVoice& V = s->voices[v];
    if (V.active && !(V.flags & 0x1800) && V.length > 5 && V.region && read_u32(V.region) != 0xFFFFFFF8u &&
        ((s->tracks[V.track].adsr2 >> 6) & 0x7F) > 0x43)
      s->key_on |= 1u << v;
  }
}

bool sound_music_idle(Sound* s) {
  for (int t = 0; t < 24; ++t)
    if (s->tracks[t].active) return false;
  return true;
}

bool sound_track_load_pattern(Sound* s, int t) {
  SoundTrack& T = s->tracks[t];
  const u8* p = T.patterns + T.pattern * 2;
  const u32 offset = p[1] << 8 | p[0];
  if (offset != 0xFFFF) {
    T.pc = s->sequence + offset;
  } else {
    T.pc = nullptr;
    T.active = 0;
  }
  return offset == 0xFFFF;
}

int sound_random(Sound* s, int n) {
  if (n == 0) return 0;
  const s16 b = s->random_b;
  s->random_b = static_cast<s16>(s->random_a + b + 0x3711);
  const int r = static_cast<u16>(s->random_b) % n;
  s->random_a = b;
  return r;
}

void sound_stop_music(Sound* s) {
  s->music_fade.direction = 0;
  s->effects_fade.direction = 0;
  s->pause_fade.direction = 0;
  sound_stop_tracks(s, 0xFFFFFF);
  s->flags &= ~1u;
}

void sound_stop_tracks(Sound* s, u32 mask) {
  for (int t = 31; mask != 0; mask <<= 1, --t)
    if (mask & 0x80000000u) sound_stop_track(s, t);
}

void sound_stop_track(Sound* s, int t) {
  s->tracks[t].active = 0;
  for (int v = 0; v < 24; ++v)
    if (s->voices[v].track == t && s->voices[v].active) sound_voice_off(s, v);
}

void sound_voice_off(Sound* s, int v) {
  SoundVoice& V = s->voices[v];
  if (!V.active) return;
  if (!(V.flags & kTrackStream)) sound_free_voice(s, v);
  V.active = 0;
  --s->tracks[V.track].voices;
  if (!(V.flags & kTrackStream)) s->key_off |= 1u << v;
  else s->stream_stop |= 1u << v;
}

// Sets up the music from the sequence header (mode 0: new; 1: resumed, tracks kept).
void sound_sequence_init(Sound* s, int mode) {
  const u8* seq = s->sequence;
  s->reverb.mode = seq[0x13];
  s->reverb.delay = seq[0x14];
  s->reverb.feedback = seq[0x15];
  sound_apply_reverb_mode(s);
  if (mode == 1) sound_apply_reverb_depth(s);
  if (mode == 0) {
    s->flags = 0;
    s->sequence_tempo = 0;
    s->loop_count = 0;
    s->bar = 0;
    s->bar_tick = 0;
    s->base_tempo = seq[0xF];
  }
  s->music_fade.level = 0x7F;
  s->effects_fade.level = 0x7F;
  s->pause_fade.level = 0x7F;
  s->pause_fade.floor = 0;
  s->pause_fade.rate = 0;
  s->pause_fade.fraction = 0;
  s->request_restore = s->request_save = s->request_output_mode = s->request_tempo = 0;
  s->request_fade_in = s->request_fade_out = s->request_effects_fade_in = s->request_effects_fade_out = 0;
  s->request_stop = s->request_rekey = s->request_pause = s->request_resume = 0;
  s->request_volume = 0;
  sound_apply_tempo(s);
  if (mode == 1) return;
  const u8* track_table = seq + (seq[9] << 8 | seq[8]);
  for (int t = 0; t < 24; ++t, track_table += 6) {
    SoundTrack& T = s->tracks[t];
    sound_track_reset(s, t);
    T.volume3 = 0x7F;
    T.volume4 = 0x7F;
    T.pan = 0x40;
    const u8 f = track_table[0];
    if (f & 0x80) T.flags |= kTrackFixedNote;
    if (f & 0x40) T.flags |= kTrackRegionNote;
    if (f & 0x02) T.flags |= kTrackPanByNote;
    if (f & 0x01) T.flags |= kTrackNoKeyOn;
    T.priority = track_table[1];
    T.transpose = static_cast<s8>(track_table[2]);
    T.wait = static_cast<u8>(track_table[3] + 1);
    T.patterns = seq + (track_table[5] << 8 | track_table[4]);
    if (!sound_track_load_pattern(s, t)) T.active = 1;
  }
}

void sound_track_reset(Sound* s, int t) {
  SoundTrack& T = s->tracks[t];
  T = SoundTrack{};
  T.note_format = 4;
  T.bend_range = 0x0C;
  T.volume = 0x7F;
  T.expression = 0x7F;
  T.vibrato_delay = 0x30;
  T.vibrato_depth = 0x14;
  T.vibrato_rate = 0x04;
  T.note_pan = 0x40;
  T.voice_param[0] = 0x30;
  T.voice_param[1] = 0x40;
  T.voice_param[2] = 0x04;
  T.priority = 0;
}

int sound_find_free_track(Sound* s, int first, int last) {
  for (int t = first; t < last; ++t)
    if (!s->tracks[t].active) return t;
  return -1;
}

// The effect scripts, once per effect tick: words of op << 24 | argument; 0 waits
// for the track and ends, 1 delays, 2 waits for the track, 3 starts sequence 'arg'.
void sound_update_effects(Sound* s) {
  for (auto& E : s->effects) {
    if (E.script) {
      const s16 delay = E.delay;
      if (delay == 0 || (E.delay = static_cast<s16>(delay - 1), delay == 1)) {
        do {
          const u8* p = E.script;
          if (!p) break;
          const u32 w = read_u32(p);
          const u32 op = w >> 24;
          if (op == 1) {
            E.delay = static_cast<s16>(w);
            E.script += 4;
          } else if (op == 0) {
            if (!s->tracks[E.track].active) E.script = nullptr;
            else E.delay = 1;
          } else if (op == 2) {
            if (s->tracks[E.track].active) E.delay = 1;
            else E.script += 4;
          } else if (op == 3) {
            const int t = sound_start_effect_track(s, static_cast<int>(w & 0xFFFF), E.volume_pan, E.transpose, E.table);
            E.track = static_cast<u8>(t);
            SoundTrack& T = s->tracks[t];
            T.flags &= ~kTrackStream;
            if (E.clip != 0) {
              T.flags |= kTrackStream;
              T.stream_data = E.clip;
              T.stream_voice = E.alternate;
              T.stream_size = E.clip_size;
            }
            E.script += 4;
          } else {
            log_once("sound-effect-op", "sound: unknown effect script word; effect stopped");
            E.script = nullptr;
            break;
          }
        } while (E.delay == 0);
      }
    }
    if (E.update) {
      E.update = 0;
      if (E.track != 0) {
        SoundTrack& T = s->tracks[E.track];
        T.dirty |= 0x33;
        T.volume3 = E.volume;
        T.pan = E.pan;
        T.pitch_scale = E.pitch_scale;
        T.flags = E.invert ? T.flags | kTrackInvertLeft : T.flags & ~kTrackInvertLeft;
      }
    }
    if (E.duck != 0) {
      --E.duck;
      if (E.script && E.track != 0) {
        SoundTrack& T = s->tracks[E.track];
        T.dirty |= 0x12;
        T.volume4 = E.duck == 0 ? 0x7F : 0;
      }
    }
    if (E.fade_direction == -1) {
      if (E.fade_delay == 0) {
        if (E.fade_floor < E.fade_level) {
          const u16 acc = static_cast<u16>(E.fade_fraction + E.fade_rate);
          E.fade_fraction = acc;
          if (static_cast<s16>(acc) > 0xFF) {
            const s32 level = static_cast<u8>(E.fade_level) - (static_cast<s16>(acc) >> 8);
            E.fade_level = static_cast<s8>(level);
            if (static_cast<s8>(level) < E.fade_floor) E.fade_level = E.fade_floor;
            E.fade_fraction = acc & 0xFF;
            s->tracks[E.track].volume = static_cast<u8>(E.fade_level);
            s->tracks[E.track].dirty |= 0x12;
          }
        } else {
          E.fade_direction = 0;
          E.fade_level = 0;
          E.pitch_steps = 0;
          E.script = nullptr;
          sound_stop_track(s, E.track);
        }
      } else {
        --E.fade_delay;
      }
    }
    if (E.pitch_steps != 0) {
      --E.pitch_steps;
      E.pitch_value = static_cast<s16>(E.pitch_value + E.pitch_step);
      s->tracks[E.track].pitch_scale = E.pitch_value;
      s->tracks[E.track].dirty |= kDirtyPitch;
    }
  }
}

int sound_start_effect_track(Sound* s, int seq, u16 volume_pan, s8 transpose, s8 table) {
  const int t = sound_find_free_track(s, 24, 32);
  if (t == -1) return 0;
  sound_track_reset(s, t);
  SoundTrack& T = s->tracks[t];
  const u16 v = static_cast<u16>(volume_pan | 0x7F);
  T.volume3 = static_cast<u8>(v);
  T.volume4 = static_cast<u8>(v >> 8);
  T.active = 1;
  T.wait = 2;
  T.flags = kTrackEffect;
  T.priority = 10;
  T.pan = static_cast<u8>(volume_pan);
  T.transpose = transpose;
  if (table == 0) {
    T.pc = s->effect_sequences + read_u16(s->effect_sequences + seq * 2);
  } else {
    const u8* base = s->banks[table - 1].sequence_offsets_base;
    T.pc = base + read_u16(base + seq * 2);
  }
  return t;
}

void sound_set_transfer_notify(Sound*, int notify, int arg) {
  g_transfer_notify = notify;
  g_transfer_notify_arg = arg;
  if (notify != 0) log_once("sound-notify", "sound: bank transfer notification objects are not supported");
}

// Copies a bank's instrument table, adding the slot's SPU RAM address to each region's
// sample and loop addresses.
void sound_copy_instruments(Sound* s, u8* dst, const u8* src, u32 spu_base) {
  const u8* end = s->bank_area + sizeof s->bank_area;
  for (;;) {
    if (read_u32(src) == 0xFFFFFFFFu) break;
    const u32 w1 = read_u32(src + 4);
    const u32 count = w1 >> 24;
    if (dst + 8 + count * 20 + 4 > end) {
      log_once("sound-bank-size", "sound: instrument table larger than its slot; truncated");
      break;
    }
    write_u32(dst, read_u32(src));
    write_u32(dst + 4, w1);
    src += 8;
    dst += 8;
    for (u32 k = 0; k < count; ++k, src += 20, dst += 20) {
      write_u32(dst, read_u32(src));
      write_u32(dst + 4, read_u32(src + 4) + spu_base);
      write_u32(dst + 8, read_u32(src + 8) + spu_base);
      write_u32(dst + 12, read_u32(src + 12));
      write_u32(dst + 16, read_u32(src + 16));
    }
  }
  write_u32(dst, 0xFFFFFFFF);
}

// An effect slot (1-8) for a new effect: a free one, else the one playing an effect of
// the same group (if its priority is not above the new one's), else the one with the
// highest priority value. Ids 0x6f / 0x70 << 24 are streamed clips and alternate
// between two SPU voices. 0 = refused.
int sound_alloc_effect(Sound* s, u32 id, u32 header) {
  s32 highest = 0;
  int highest_slot = 0;
  int clip5 = -1, clip6 = -1, same_group = -1, free_slot = -1;
  s32 clip5_priority = 0, clip6_priority = 0;
  u8 alternate = 0xFF;
  for (int i = 0; i < 8; ++i) {
    const SoundEffect& E = s->effects[i];
    if (!E.script) {
      free_slot = i;
      continue;
    }
    const s32 p = E.priority;
    if (highest < p) {
      highest = p;
      highest_slot = i;
    }
    if (E.table == 5) {
      clip5 = i;
      clip5_priority = p;
    }
    if (E.table == 6) {
      clip6 = i;
      clip6_priority = p;
    }
    if (E.group != 0 && static_cast<u32>(static_cast<s32>(E.group)) == ((header >> 16) & 0xFF)) same_group = i;
  }
  const u32 kind = id >> 24;
  const s32 priority = static_cast<s32>(header) >> 24;
  if (kind == 0x70) {
    alternate = static_cast<u8>(s->clip_counter & 1);
    ++s->clip_counter;
  }
  if (kind == 0x6F) {
    bool use_b;
    bool decided = false;
    if (s->bank_order == 0) {
      if (clip6 == -1 && s->clip_slot_used[1] == 0) {
        alternate = 1;
        s->bank_order = 1;
        decided = true;
      } else if (clip5 == -1 && s->clip_slot_used[0] == 0) {
        alternate = 0;
        s->bank_order = 0;
        decided = true;
      }
    } else if (clip5 == -1 && s->clip_slot_used[0] == 0) {
      alternate = 0;
      s->bank_order = 0;
      decided = true;
    } else if (clip6 == -1 && s->clip_slot_used[1] == 0) {
      alternate = 1;
      s->bank_order = 1;
      decided = true;
    }
    if (!decided) {
      if (same_group != -1) {
        if (s->effects[same_group].priority < priority) return 0;
        sound_release_clip(s, same_group + 1);
      }
      if (s->clip_slot_used[1] & s->clip_slot_used[0]) return 0;
      if (s->clip_slot_used[0] == 0) {
        if (s->clip_slot_used[1] == 0 && clip5_priority <= clip6_priority) {
          if (clip5_priority < clip6_priority) {
            if (static_cast<u32>(clip6_priority) < (header >> 24)) return 0;
            use_b = true;
          } else {
            if (static_cast<u32>(clip5_priority) < (header >> 24)) return 0;
            use_b = s->bank_order == 0;
          }
        } else {
          if (static_cast<u32>(clip5_priority) < (header >> 24)) return 0;
          use_b = false;
        }
      } else {
        if (static_cast<u32>(clip6_priority) < (header >> 24)) return 0;
        use_b = true;
      }
      s->bank_order = use_b;
      if (!use_b) {
        sound_release_clip(s, clip5 + 1);
        s->effects[clip5].alternate = 0;
        return clip5 + 1;
      }
      sound_release_clip(s, clip6 + 1);
      s->effects[clip6].alternate = 1;
      return clip6 + 1;
    }
  }
  int slot;
  if (same_group == -1) {
    if (free_slot != -1) {
      s->effects[free_slot].alternate = alternate;
      return free_slot + 1;
    }
    slot = highest_slot;
  } else {
    SoundEffect& E = s->effects[same_group];
    if (E.priority < priority) return 0;
    if (kind == 0x70) {
      s->stream_hold = 100;
      if (E.clip != 0 && !s->voices[E.alternate].active) sound_free_voice(s, E.alternate);
    }
    slot = same_group;
  }
  sound_release_clip(s, slot + 1);
  s->effects[slot].alternate = alternate;
  return slot + 1;
}

void sound_free_voice(Sound* s, int v) {
  *s->free_write = static_cast<u8>(v);
  s->free_write = ring_next(s, s->free_write);
}

void sound_set_tracks(Sound* s, u8 value, u32 mask, u8 SoundTrack::*field, u8 dirty) {
  for (int t = 0; mask != 0 && t < 32; mask >>= 1, ++t) {
    if (mask & 1) {
      s->tracks[t].*field = value;
      s->tracks[t].dirty |= dirty;
    }
  }
}

// Bytes of entry #9's effect data: its script table (at the first word) ends where
// the first script starts, and the scripts end with a word of op 0.
size_t effect_data_size(const u8* data) {
  const u32 scripts = read_u32(data);
  size_t end = scripts;
  s32 first = 0x7FFF;
  for (u32 i = 0; i * 2 < static_cast<u32>(first); ++i) {
    const s16 off = read_s16(data + scripts + i * 2);
    end = std::max<size_t>(end, scripts + i * 2 + 2);
    if (off <= 0) continue;
    first = std::min<s32>(first, off);
    const u8* p = data + scripts + off;
    while (read_u32(p) >> 24 != 0) p += 4;
    end = std::max<size_t>(end, static_cast<size_t>(p + 4 - data));
  }
  return end;
}

}  // namespace

// ---- Public entry points ----------------------------------------------------------------

void sound_bank_slot_clear(Sound* s, int slot) {
  SoundBankSlot& b = s->banks[slot];
  if (slot < 4)
    for (int i = 0; i < 20; ++i) b.remap[i] = static_cast<u8>(i);
  b.sequence_offsets = b.sequence_offsets_base;
  b.spu = b.spu_base;
  b.instruments = b.instruments_base;
  b.sequences = b.sequences_base;
  b.scripts = b.scripts_base;
  std::memset(b.index, 0, static_cast<size_t>(b.scripts_base - b.index));
  s->bank_loaded[slot] = 0;
}


void sound_release_clip(Sound* s, int slot) {
  SoundEffect& E = s->effects[slot - 1];
  if (!E.script) return;
  u32 bits = 0;
  if (E.track != 0) {
    s->releasing = 1;
    const u8 t = E.track;
    s->tracks[t].active = 0;
    for (int v = 0; v < 24; ++v) {
      SoundVoice& V = s->voices[v];
      if (V.track != t || !V.active) continue;
      if (!(V.flags & kTrackStream)) {
        sound_free_voice(s, v);
        V.active = 0;
      }
      bits |= 1u << v;
      --s->tracks[V.track].voices;
    }
    if (E.clip == 0) SpuSetKey(SPU_OFF, bits);
    else if (bits != 0) s->stream_stop |= bits;
  }
  E.script = nullptr;
  s->releasing = 0;
}

// The streaming slots' and effect banks' import (see sound.h).
u8* sound_import_effects(Sound* s, u8* data, int slot, int keep, int first) {
  u8* const table = data + 4;  // s16 per effect: record offset, alias (< 0) or 0; -0x8000 ends
  u8* bank = data + read_u32(data);
  const u32 bank_size = read_u32(bank);
  const s32 header = static_cast<s16>(read_u16(bank + 4));
  SoundBankSlot& b = s->banks[slot];
  u8* const scripts = b.scripts;
  u8* const offsets = b.sequence_offsets;
  u8* const sequences = b.sequences;
  if (slot <= 3 && b.scripts > b.sequences_base + 0x78) return bank;
  const u8* const area_end = s->bank_area + sizeof s->bank_area;
  int i = 0;
  for (u8* p = table; read_s16(p) != -0x8000; p += 2, ++i) {
    const s16 e = read_s16(p);
    if (e == 0) continue;
    if (slot < 4) b.remap[i + first] = static_cast<u8>(i + first);
    if (e < 0 && e != -0x7FFF) {
      // Same effect as an earlier one: its index entry is copied.
      s32 k = e;
      const u8* q;
      do {
        q = table + k * -2;
        k = read_s16(q);
      } while (k < 0);
      write_u16(b.index + (p - table), read_u16(b.index + (q - table)));
    } else if (e > 0) {
      // A record: u32 size, u32 script header, the effect's sequence. It becomes the
      // script "header; start sequence n; end" and sequence n.
      const u8* rec = table + e;
      const u32 size = read_u32(rec);
      u8* entry = b.index + (i + first) * 2;
      while (read_u16(entry) != 0) entry += 2;
      if (b.scripts + 12 > area_end || b.sequences + (size - 8) > area_end) {
        log_once("sound-import-size", "sound: imported effects larger than their slot; truncated");
        break;
      }
      write_u16(entry, static_cast<u16>(b.scripts - b.index));
      write_u32(b.scripts, read_u32(rec + 4));
      write_u32(b.scripts + 4, static_cast<u32>((b.sequence_offsets - b.sequence_offsets_base) / 2) | 0x3000000);
      write_u32(b.scripts + 8, 0);
      b.scripts += 12;
      write_u16(b.sequence_offsets, static_cast<u16>(b.sequences - b.sequence_offsets_base));
      b.sequence_offsets += 2;
      std::memcpy(b.sequences, rec + 8, size - 8);
      b.sequences += size - 8;
    }
  }
  if (keep == 0) {
    b.scripts = scripts;
    b.sequence_offsets = offsets;
    b.sequences = sequences;
  }
  u32 spu;
  if (slot < 8) {
    spu = b.spu;
  } else {  // a stream slot: the samples play from the next free voice's SpuSt buffer
    s->stream_voice = *s->free_read;
    s->free_read = ring_next(s, s->free_read);
    spu = 0x70000u | static_cast<u32>(s->stream_voice) << 8;
  }
  sound_copy_instruments(s, b.instruments, bank + 0xC, spu);
  if (keep != 0) b.instruments += header - 0x10;
  bank += header;
  if (slot < 8) {
    SpuSetTransferStartAddr(b.spu);
    if (keep != 0) b.spu += bank_size - static_cast<u32>(header);
    s->bank_loaded[slot] = 0;
    s->transfer_busy = 1;
    SpuWrite(bank, bank_size - static_cast<u32>(header));
  }
  return bank;
}

int sound_open_clip(Sound* s, const void* clip_data, int mode, int* out) {
  // The console streams straight from (and patches the flags of) the clip in RAM.
  u8* clip = static_cast<u8*>(const_cast<void*>(clip_data));
  const u8* bank = clip + read_u32(clip);
  const s32 samples = static_cast<s32>(read_u32(bank)) - static_cast<s16>(read_u16(bank + 4));
  if (mode == 2 && samples < 0x81) return sound_open_clip(s, clip, 1, nullptr);  // too short to stream
  if (s->free_read == s->free_write) return -1;
  const u32 header = read_u32(clip + read_s16(clip + 4) + 8);
  int slot;
  u8* stream = nullptr;
  if (mode == 2) {
    if (s->transfer_busy || s->stream_stopping) return -1;
    slot = sound_alloc_effect(s, 0x70000000, header);
    if (slot == 0) return 0;
    SoundEffect& E = s->effects[slot - 1];
    stream = sound_import_effects(s, clip, E.alternate + 8, 0, 0);
    E.table = static_cast<s8>(E.alternate + 9);
    if (out) *out = (E.alternate + 9) << 24;
  } else {
    if (sound_bank_busy(s)) {
      for (int i = 0; i < 8; ++i) {
        const SoundEffect& E = s->effects[i];
        if (E.script && E.clip && SpuStGetVoiceStatus() != 0) {
          sound_release_clip(s, i + 1);
          s->stream_stopping = 1;
        }
      }
      return -1;
    }
    slot = sound_alloc_effect(s, 0x6F000000, header);
    if (slot == 0) return 0;
    SoundEffect& E = s->effects[slot - 1];
    sound_import_effects(s, clip, E.alternate + 4, 0, 0);
    E.table = static_cast<s8>(E.alternate + 5);
    s->clip_slot_used[E.alternate] = out != nullptr;
    if (out) *out = (E.alternate + 5) << 24;
  }
  if (mode != 0) {
    SoundEffect& E = s->effects[slot - 1];
    E.priority = static_cast<s8>(header >> 24);
    E.group = static_cast<s8>(header >> 16);
    E.volume_pan = 0x7F40;
    E.clip = stream;
    E.id = static_cast<u32>(reinterpret_cast<uintptr_t>(clip));
    E.clip_size = static_cast<u32>(samples);
    E.header_low = static_cast<s16>(header);
    E.transpose = 0;
    E.delay = 0;
    E.track = 0;
    E.fade_direction = 0;
    E.pitch_steps = 0;
    E.volume = 0x7F;
    E.fade_level = 0x7F;
    E.update = 0;
    E.duck = 0;
    const u8* index = s->banks[E.alternate + (mode == 2 ? 8 : 4)].index;
    E.script = index + read_s16(index) + 4;
    if (mode == 2) {
      E.alternate = s->stream_voice;
      s->stream_delay = 4;
    }
  }
  return slot;
}

void sound_start_clip(Sound* s, int slot, int volume, int pan, int pitch_scale, int invert) {
  SoundEffect& E = s->effects[slot - 1];
  if (!E.script) return;
  E.pitch_scale = static_cast<s16>(pitch_scale);
  E.pan = static_cast<u8>(pan);
  E.volume = static_cast<u8>(volume);
  E.invert = static_cast<u8>(invert);
  E.update = 1;
}

void sound_fade_out_effects(Sound* s, int frames) {
  s16 f = static_cast<s16>(frames);
  if (f > 2) f = static_cast<s16>(f - 2);
  if (f == 0) f = 1;  // the console traps
  s->request_effects_fade_out = 1;
  s->effects_fade_rate = static_cast<s16>(0x7800 / f);
}


s32 sound_pitch_table(int note) { return boot_s16(0x8002A9B4 + static_cast<u32>(note * 2)); }

void sound_init(Sound* s) {
  s->subtick = 0;
  s->frame_ticks = 0;
  s->extra_ticks = 0;
  std::fill(std::begin(s->bank_loaded), std::end(s->bank_loaded), u8{0});
}

void sound_set_sequences(Sound* s, void* data) {
  // The title program frees its copy of entry #9 right after this call; the driver
  // keeps reading it, so the host keeps its own copy (the console's stays in RAM).
  const u8* d = static_cast<const u8*>(data);
  s->effect_data.assign(d, d + effect_data_size(d));
  s->effect_sequences = s->effect_data.data() + 4;
  s->effect_scripts = s->effect_data.data() + read_u32(d);
}

long sound_interrupt() {
  sound_tick(g_sound);
  return 0;
}

void sound_start(Sound* s) {
  trace_open();
  g_transfer_notify = 0;
  g_sound = s;
  EnterCriticalSection();
  SpuInitMalloc(4, s->spu_malloc);
  SpuInit();
  sound_stream_init(s);
  SpuSetMute(SPU_ON);
  SpuSetTransferMode(SPU_TRANSFER_BY_DMA);
  sound_reset(s);
  SpuSetPitchLFOVoice(SPU_OFF, 0xFFFFFF);
  SpuSetNoiseVoice(SPU_OFF, 0xFFFFFF);
  SpuSetNoiseClock(0);
  sound_reverb_init(s);
  sound_init_common_attr(s);
  ResetRCnt(RCntCNT2);
  s->event = OpenEvent(RCntCNT2, EvSpINT, EvMdINTR, sound_interrupt);
  EnableEvent(s->event);
  sound_apply_tempo(s);
  StartRCnt(RCntCNT2);
  SpuSetMute(SPU_OFF);
  SpuSetTransferCallback(sound_transfer_done);
  ExitCriticalSection();
  s->default_notify = nullptr;  // an 8-byte object (vtable 0x8002a994) whose notify method does nothing
}

void sound_shutdown(Sound* s) {
  for (int slot = 1; slot < 9; ++slot) sound_release_clip(s, slot);
  while (sound_bank_busy(s) != 0) {
  }
  EnterCriticalSection();
  s->key_off = 0;
  s->music_fade.direction = 0;
  s->effects_fade.direction = 0;
  s->pause_fade.direction = 0;
  s->flags &= ~1u;
  sound_stop_tracks(s, 0xFFFFFFFF);
  SpuSetKey(SPU_OFF, 0xFFFFFF);
  StopRCnt(RCntCNT2);
  CloseEvent(s->event);
  DisableEvent(s->event);
  SpuSetMute(SPU_ON);
  SpuSetReverb(SPU_OFF);
  SpuSetReverbVoice(SPU_OFF, 0xFFFFFF);
  s->transfer_busy = s->stream_busy = s->stream_delay = s->stream_stopping = 0;
  SpuSetTransferCallback(nullptr);
  SpuQuit();
  SpuStQuit();
  SpuInit();
  SpuQuit();
  ExitCriticalSection();
}

// Interrupt handler body: effect ticks every 65104 counter ticks (~65 Hz), music ticks
// at the tempo; requests first, then note lengths, sequences, key off, voice updates,
// streams and key on. A voice keyed off and on in one tick is keyed on a tick later.
void sound_tick(Sound* s) {
  if (s->releasing) return;
  do {
    s->tick_kind = 0;
    s->key_on = 0;
    s->key_off = 0;
    ++s->ticks;
    sound_handle_requests(s);
    const u32 phase = s->frame_phase + s->rcnt_target;
    s->frame_phase = phase;
    if (phase > 0xFE4F) {
      s->frame_phase = phase - 0xFE50;
      s->tick_kind = 2;
    }
    if (s->flags & 1) {
      bool skip = false;
      if (s->tempo < 0x50) skip = (s->tempo < 0x28 ? (s->ticks & 3) : (s->ticks & 1)) != 0;
      if (!skip) ++s->tick_kind;
    }
    if (s->stream_delay) --s->stream_delay;
    if (s->tick_kind == 0) {
      SpuSetKey(SPU_OFF, s->key_off);
    } else {
      if (s->tick_kind & 2) {
        if (!s->transfer_busy) {
          s->transfer_timeout = 0;
        } else if (++s->transfer_timeout > 0xC0) {
          s->transfer_busy = 0;
        }
        s->tick_class = kTrackEffect;
        sound_update_effects(s);
        sound_fade_step(s, &s->effects_fade, 24, 32);
        sound_fade_step(s, &s->pause_fade, 0, 24);
        sound_fade_step(s, &s->music_fade, 0, 24);
      }
      if (s->tick_kind & 1) {
        s->tick_class = 0;
        ++s->bar_tick;
      }
      sound_update_lengths(s);
      sound_run_tracks(s);
      SpuSetKey(SPU_OFF, s->key_off);
      sound_update_voices(s);
      sound_stream_update(s);
    }
    const u32 both = s->key_on & s->key_off;
    const u32 keys = (s->key_on & ~both) | s->key_on_delayed;
    s->key_on = keys;
    SpuSetKey(SPU_ON, keys);
    s->key_on_delayed = both;
  } while (--s->extra_ticks != -1);
  s->extra_ticks = 0;
}

void sound_transfer_done() {
  Sound* s = g_sound;
  if (s->transfer_busy == 1) s->transfer_busy = 0;
}

int sound_bank_busy(Sound* s) { return s->stream_delay != 0 || s->transfer_busy != 0 || s->stream_busy != 0; }

void sound_load_bank(Sound* s, const void* bank_data, int notify, int notify_arg) {
  sound_set_transfer_notify(s, notify, notify_arg);
  const u8* bank = static_cast<const u8*>(bank_data);
  const s32 header = static_cast<s16>(read_u32(bank + 4));
  const u32 size = read_u32(bank) - static_cast<u32>(header);
  if (g_trace) std::fprintf(g_trace, "tick %d load bank size=%x slot=%d\n", s->ticks, size, bank[7] + 6);
  if (size == 0) return;
  u32 slot = bank[7] + 6u;
  if (slot < 8) {
    s->banks[slot].instruments = s->banks[slot].instruments_base;
    s->banks[slot].spu = s->banks[slot].spu_base;
  } else {
    slot = 7;
    s->banks[7].spu = 0x34000;
    s->banks[7].instruments = s->banks[7].instruments_base;
  }
  SoundBankSlot& b = s->banks[slot];
  sound_copy_instruments(s, b.instruments, bank + 0xC, b.spu);
  const u32 end = b.spu + size;
  if (slot == 6) s->bank_loaded[6] = 0;
  if (end > 0x28000) s->bank_loaded[4] = 0;
  if (end > 0x2D800) s->bank_loaded[5] = 0;
  if (end > 0x33000) s->bank_loaded[0] = 0;
  if (end > 0x42400) s->bank_loaded[1] = 0;
  if (end > 0x51800) s->bank_loaded[2] = 0;
  if (end > 0x60C00) s->bank_loaded[3] = 0;
  SpuSetTransferStartAddr(b.spu);
  s->transfer_busy = 1;
  SpuWrite(bank + header, size);
}

void sound_set_volume(Sound* s, int music, int effects) {
  s->music_volume = static_cast<u8>(music);
  s->effects_volume = static_cast<u8>(effects);
  s->request_volume = 1;
}

void sound_play_sequence(Sound* s, void* data) {
  DisableEvent(s->event);
  s->key_off = 0;
  sound_stop_music(s);
  SpuSetKey(SPU_OFF, s->key_off);
  s->sequence = s->previous_sequence = static_cast<const u8*>(data);
  if (g_trace) std::fprintf(g_trace, "tick %d play seq id=%08x\n", s->ticks, read_u32(s->sequence + 4));
  sound_sequence_init(s, 0);
  s->flags |= 1;
  EnableEvent(s->event);
}

// Starts a sequence with a fade in; with 'resume', the one sound_restore_state put
// back (same sequence id) carries on where it stopped.
void sound_resume_sequence(Sound* s, void* data, int fade_frames, int resume) {
  DisableEvent(s->event);
  const s16 frames = static_cast<s16>(fade_frames);
  const s16 rate = static_cast<s16>(0x7800 / (frames == 0 ? 1 : frames));
  const u8* old = s->previous_sequence;
  const u8* seq = static_cast<const u8*>(data);
  s->sequence = s->previous_sequence = seq;
  const std::ptrdiff_t delta = seq - old;
  s->music_fade_rate = rate;
  if (static_cast<s16>(resume) != 0 && s->restored_stopped == 0 && read_u32(seq + 4) == s->saved_sequence_id) {
    sound_sequence_init(s, 1);
    for (int t = 0; t < 24; ++t) {
      SoundTrack& T = s->tracks[t];
      T.patterns += delta;
      if (T.pc != nullptr) {
        T.active = 1;
        T.pc += delta;
      }
      if (T.loop_pc) T.loop_pc += delta;
      T.dirty |= 0x3F;
    }
    s->request_rekey = 1;
  } else {
    s->restored_stopped = 0;
    s->key_off = 0;
    sound_stop_music(s);
    SpuSetKey(SPU_OFF, s->key_off);
    sound_sequence_init(s, 0);
  }
  s->request_fade_in = 1;
  s->flags |= 1;
  EnableEvent(s->event);
}

void sound_stop(Sound* s) { s->request_stop = 1; }

void sound_fade_out(Sound* s, int frames) {
  const s16 f = static_cast<s16>(frames);
  s->request_fade_out = 1;
  s->music_fade_rate = static_cast<s16>(0x7800 / (f == 0 ? 1 : f));
}

int sound_play_sfx(Sound* s, int id_arg, int transpose, int volume_pan) {
  const u32 id = static_cast<u32>(id_arg);
  u32 n = id & 0xFFFFFF;
  const u32 table = id >> 24;
  const u8* script;
  if (table == 0) {
    if (!s->effect_scripts) return 0;
    script = s->effect_scripts + read_s16(s->effect_scripts + n * 2);
  } else {
    SoundBankSlot& b = s->banks[table - 1];
    if (table < 5) n = b.remap[n];
    const s16 off = read_s16(b.index + n * 2);
    script = b.index + off;
    if (off == 0) return 0;
  }
  const u32 header = read_u32(script);
  const int slot = sound_alloc_effect(s, id, header);
  if (slot != 0) {
    SoundEffect& E = s->effects[slot - 1];
    E.priority = static_cast<s8>(header >> 24);
    E.group = static_cast<s8>(header >> 16);
    E.clip = 0;
    E.id = id;
    E.header_low = static_cast<s16>(header);
    E.volume_pan = static_cast<u16>(volume_pan);
    E.transpose = static_cast<s8>(transpose);
    E.delay = 0;
    E.track = 0;
    E.fade_direction = 0;
    E.pitch_steps = 0;
    E.volume = 0x7F;
    E.fade_level = 0x7F;
    E.update = 0;
    E.duck = 0;
    E.table = static_cast<s8>(id >> 24);
    E.script = script + 4;
  }
  return slot;
}

int sound_output_mode(Sound* s) { return s->output_mode; }
int sound_music_playing(Sound* s) { return static_cast<int>(s->flags & 1); }

void sound_set_output_mode(Sound* s, int mode) {
  s->output_mode_wanted = static_cast<u8>(mode);
  s->request_output_mode = 1;
}

void sound_set_pan(Sound* s, int pan, int track_mask) {
  sound_set_tracks(s, static_cast<u8>(pan), static_cast<u32>(track_mask), &SoundTrack::pan, 0x32);
}

void sound_nop_8001aabc(Sound*, int, int) {}

void* sound_state(Sound* s) { return &s->event; }
SoundTrack* sound_tracks(Sound* s) { return s->tracks; }
SoundVoice* sound_voices(Sound* s) { return s->voices; }

int sound_stream_busy(Sound* s) { return s->stream_busy != 0; }

int sound_bank_free_space(Sound* s, int slot) {
  const SoundBankSlot& b = s->banks[slot];
  return 0xF400 - static_cast<int>(b.spu - b.spu_base);
}

// A bank with its own effects into a slot, replacing what was there (slots 0-3 keep
// their first ten effects).
void sound_load_effect_bank(Sound* s, void* data, int notify, int notify_arg, int slot, int first) {
  if (slot < 4) {
    SoundBankSlot& b = s->banks[slot];
    std::memset(b.index + 0x14, 0, static_cast<size_t>(b.scripts_base - b.index) - 0x14);
  } else {
    sound_bank_slot_clear(s, slot);
  }
  sound_set_transfer_notify(s, notify, notify_arg);
  sound_import_effects(s, static_cast<u8*>(data), slot, 0, first);
}

// Another one after what the slot already holds.
void sound_add_effect_bank(Sound* s, void* data, int notify, int notify_arg, int slot, int first) {
  sound_set_transfer_notify(s, notify, notify_arg);
  sound_import_effects(s, static_cast<u8*>(data), slot, 1, first);
}

void sound_copy_remap(Sound* s, int slot, int to, int from) {
  u8* remap = s->banks[slot].remap;
  remap[to] = remap[from];
}

u32 sound_effect_playing(Sound* s, int slot) {
  const SoundEffect& E = s->effects[slot - 1];
  return E.script ? E.id : 0;
}

void sound_clip_fade_out(Sound* s, int slot, int frames, int delay) {
  SoundEffect& E = s->effects[slot - 1];
  if (!E.script) return;
  const s16 f = static_cast<s16>(frames);
  const s16 rate = static_cast<s16>(0x7800 / (f == 0 ? 1 : f));
  E.fade_level = 0x7F;
  E.fade_fraction = 0;
  E.fade_floor = 0;
  E.fade_direction = -1;
  E.fade_delay = static_cast<s16>(delay);
  E.fade_rate = rate;
}

// Effect ticks in 'frames' frames: frames * 0x11111 >> 16.
static s16 effect_ticks(int frames) {
  const s32 f = static_cast<s16>(frames);
  return static_cast<s16>((f * 0x11111) >> 16);
}

void sound_clip_pitch_rise(Sound* s, int slot, int frames) {
  SoundEffect& E = s->effects[slot - 1];
  if (!E.script) return;
  s16 n = effect_ticks(frames);
  if (n == 0) n = 1;  // the console traps
  E.pitch_value = 0x2000;
  E.pitch_steps = static_cast<s16>(n * 2);
  E.pitch_step = static_cast<s16>(0x2000 / n);
}

void sound_clip_pitch_drop(Sound* s, int slot, int frames) {
  SoundEffect& E = s->effects[slot - 1];
  if (!E.script) return;
  s16 n = effect_ticks(frames);
  if (n == 0) n = 1;
  E.pitch_value = 0x2000;
  E.pitch_steps = static_cast<s16>(n * 2);
  E.pitch_step = static_cast<s16>(-0x1000 / n);
}

void sound_clip_duck(Sound* s, int slot, int ticks) {
  SoundEffect& E = s->effects[slot - 1];
  if (E.script) E.duck = static_cast<s16>(ticks);
}

void sound_fade_in_all(Sound* s, int frames) {
  s16 f = static_cast<s16>(frames);
  if (f > 1) f = static_cast<s16>(f - 1);
  if (f == 0) f = 1;  // the console traps
  s->request_effects_fade_in = 1;
  s->effects_fade_rate = static_cast<s16>(0x7800 / f);
}

void sound_pause(Sound* s, int floor, int frames) {
  s->pause_floor = static_cast<u8>(floor);
  s->pause_frames = static_cast<s16>(frames);
  s->request_pause = 1;
}

void sound_resume(Sound* s, int frames) {
  s->pause_frames = static_cast<s16>(frames);
  s->request_resume = 1;
}

void sound_set_tempo(Sound* s, int adjust) {
  s->tempo_adjust = static_cast<s8>(adjust);
  s->request_tempo = 1;
}

void sound_end_loops(Sound* s, int track_mask) {
  sound_set_tracks(s, 1, static_cast<u32>(track_mask), &SoundTrack::loop_count, 0);
}

u8 sound_bank_loaded(Sound* s, int slot) { return s->bank_loaded[slot]; }
void sound_set_bank_loaded(Sound* s, int slot, u8 value) { s->bank_loaded[slot] = value; }

void sound_measure_subtick(Sound* s) {
  const s32 position = (static_cast<s32>(GetRCnt(RCntCNT2)) << 16) / (s->rcnt_target ? s->rcnt_target : 1);
  s->subtick = static_cast<s16>((s->frame_ticks * position) >> 16);
}

void sound_add_ticks(Sound* s, int n) {
  if (s->frame_ticks == 0) return;  // the console traps
  s->extra_ticks = static_cast<s16>(s->extra_ticks + (s->subtick + n) / s->frame_ticks);
}

}  // namespace ss::game
