#pragma once

#include <array>
#include <cstddef>
#include <functional>

#include "core/types.h"

// A software replacement for the PS1 Sound Processing Unit, modelled on psx-spx and
// on how Mednafen / DuckStation behave: 24 ADPCM voices with ADSR envelopes, pitch
// counters with Gaussian interpolation, pitch modulation, noise, volume sweeps, the
// hardware reverb (with its 22.05 kHz resampling filters), key on/off, ENDX, the IRQ
// address, the capture buffers and data transfers into the 512 KB sound RAM. CD and
// external audio inputs are present as registers but carry silence.
//
// Software sees the chip through its registers: read16 / write16 take the byte offset
// from 0x1F801C00 (0x000-0x3FF), exactly what PsyQ's libspu pokes. tick() produces one
// 44.1 kHz stereo sample.
namespace ss::psx {

class Spu {
 public:
  static constexpr u32 kRamSize = 512 * 1024;
  static constexpr int kVoices = 24;
  static constexpr int kSampleRate = 44100;

  // Register offsets (from 0x1F801C00).
  enum Reg : u32 {
    kVoiceVolL = 0x0, kVoiceVolR = 0x2, kVoicePitch = 0x4, kVoiceStart = 0x6,
    kVoiceAdsr1 = 0x8, kVoiceAdsr2 = 0xA, kVoiceEnvx = 0xC, kVoiceRepeat = 0xE,
    kMainVolL = 0x180, kMainVolR = 0x182, kReverbVolL = 0x184, kReverbVolR = 0x186,
    kKeyOnLo = 0x188, kKeyOnHi = 0x18A, kKeyOffLo = 0x18C, kKeyOffHi = 0x18E,
    kPmonLo = 0x190, kPmonHi = 0x192, kNoiseLo = 0x194, kNoiseHi = 0x196,
    kReverbOnLo = 0x198, kReverbOnHi = 0x19A, kEndxLo = 0x19C, kEndxHi = 0x19E,
    kReverbBase = 0x1A2, kIrqAddress = 0x1A4, kTransferAddress = 0x1A6, kTransferFifo = 0x1A8,
    kControl = 0x1AA, kTransferControl = 0x1AC, kStatus = 0x1AE,
    kCdVolL = 0x1B0, kCdVolR = 0x1B2, kExtVolL = 0x1B4, kExtVolR = 0x1B6,
    kCurMainVolL = 0x1B8, kCurMainVolR = 0x1BA,
    kReverbRegs = 0x1C0,     // 32 registers dAPF1 .. vRIN
    kVoiceCurVol = 0x200,    // 24 x (left, right), read-only
  };

  Spu() { reset(); }
  void reset();

  u16 read16(u32 offset) const;
  void write16(u32 offset, u16 value);

  // DMA channel 4 (or the manual FIFO): halfwords to / from sound RAM at the current
  // transfer address, which advances and wraps at 512 KB.
  void dma_write(const u16* data, size_t halfwords);
  void dma_read(u16* data, size_t halfwords);

  // One output sample at 44.1 kHz.
  void tick(s16& left, s16& right);
  void generate(s16* stereo, size_t frames);  // interleaved left/right

  // Raised when the IRQ address is touched with SPUCNT bit 6 set (the flag stays in
  // SPUSTAT bit 6 until software clears bit 6).
  std::function<void()> on_irq;

  u8* ram() { return ram_.data(); }
  const u8* ram() const { return ram_.data(); }
  u16 ram16(u32 byte_address) const;

  // Inspection, for tests and tools.
  bool voice_on(int v) const { return voices_[v].phase != Phase::Off; }
  u16 voice_envelope(int v) const { return static_cast<u16>(voices_[v].adsr_level); }
  u32 voice_address(int v) const { return voices_[v].current_address * 8u; }
  u32 endx() const { return endx_; }

  // The 512-entry Gaussian interpolation ROM (psx-spx "SPU Interpolation").
  static const s16 kGauss[512];

  // Decodes one 16-byte ADPCM block into 28 samples; 'history' holds the previous two
  // decoded samples (newest first) and is updated. Returns the block's flag byte
  // (bit 0 loop end, bit 1 repeat, bit 2 loop start).
  static u8 decode_adpcm(const u8* block, s16 history[2], s16 out[28]);

 private:
  enum class Phase : u8 { Off, Attack, Decay, Sustain, Release };

  // One ADSR / volume-sweep stepper (psx-spx "SPU Volume and ADSR Generator").
  struct Envelope {
    u8 rate = 0;  // 7 bits: shift (bits 2-6) and step (bits 0-1)
    bool decreasing = false;
    bool exponential = false;
    s32 counter = 0;  // output samples since the last step
    void reset(u8 r, bool dec, bool exp) {
      rate = r;
      decreasing = dec;
      exponential = exp;
      counter = 0;
    }
    s32 tick(s32 level);  // returns the new level, saturated to 0..0x7FFF
  };

  struct VolumeSweep {
    u16 reg = 0;
    s32 level = 0;  // current level (-0x8000..0x7FFF)
    bool active = false;
    Envelope env;
    void set(u16 value);
    void tick();
  };

  struct Voice {
    u16 regs[8] = {};  // the eight registers as written
    VolumeSweep vol_l, vol_r;
    u32 current_address = 0;  // in 8-byte units
    u32 counter = 0;          // pitch counter: bits 12+ sample index, bits 4-11 interpolation
    Phase phase = Phase::Off;
    s32 adsr_level = 0;
    s32 adsr_target = 0;
    Envelope adsr;
    bool has_samples = false;
    bool ignore_loop_address = false;
    u8 block_flags = 0;
    s16 samples[3 + 28] = {};  // 3 from the previous block (interpolation), then the block
    s16 adpcm_last[2] = {};    // decoder history
    s32 last_output = 0;       // post-envelope sample, for pitch modulation and capture
  };

  void key_on(int v);
  void key_off(int v);
  void update_adsr(int v);
  void decode_block(int v);
  s32 sample_voice(int v, s32& left, s32& right);
  void update_noise();
  void process_reverb(s32 in_l, s32 in_r, s32& out_l, s32& out_r);
  u32 reverb_address(u32 halfword_offset) const;
  s16 reverb_read(u16 reg, s32 delta = 0) const;
  void reverb_write(u16 reg, s32 value);
  void check_irq(u32 byte_address, u32 bytes);
  u16 control() const { return regs_[kControl / 2]; }

  std::array<u8, kRamSize> ram_{};
  std::array<u16, 0x200> regs_{};  // shadow of everything written (0x000-0x3FF)
  std::array<Voice, kVoices> voices_{};
  VolumeSweep main_l_, main_r_;
  u32 endx_ = 0;
  u32 transfer_address_ = 0;  // bytes
  u16 status_ = 0;
  u32 capture_index_ = 0;

  s32 noise_timer_ = 0;
  u16 noise_level_ = 1;

  u32 reverb_current_ = 0;  // halfwords
  u32 resample_pos_ = 0;
  s16 reverb_in_hist_[2][64] = {};
  s16 reverb_out_hist_[2][64] = {};
};

}  // namespace ss::psx
