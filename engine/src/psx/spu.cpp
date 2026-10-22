#include "psx/spu.h"

#include <algorithm>
#include <cstring>

namespace ss::psx {
namespace {

s32 clamp16(s32 v) { return std::clamp<s32>(v, -0x8000, 0x7FFF); }
s32 mul15(s32 a, s32 b) { return (a * b) >> 15; }

// ADPCM prediction filters (psx-spx "SPU ADPCM Samples").
constexpr s32 kFilterPos[5] = {0, 60, 115, 98, 122};
constexpr s32 kFilterNeg[5] = {0, 0, -52, -55, -60};

// The 39-tap filter the reverb unit resamples its input (44.1 -> 22.05 kHz) and its
// output (22.05 -> 44.1 kHz) with (psx-spx "Reverb Buffer Resampling").
constexpr s32 kReverbFir[39] = {
    -0x0001, 0, 0x0002, 0, -0x000A, 0, 0x0023, 0, -0x0067, 0, 0x010A, 0, -0x0268, 0,
    0x0534,  0, -0x0B90, 0, 0x2806, 0x4000, 0x2806, 0, -0x0B90, 0, 0x0534, 0, -0x0268, 0,
    0x010A,  0, -0x0067, 0, 0x0023, 0, -0x000A, 0, 0x0002, 0, -0x0001};

// Reverb register indexes (0x1C0 + 2 * index).
enum ReverbReg : u16 {
  dAPF1, dAPF2, vIIR, vCOMB1, vCOMB2, vCOMB3, vCOMB4, vWALL, vAPF1, vAPF2,
  mLSAME, mRSAME, mLCOMB1, mRCOMB1, mLCOMB2, mRCOMB2, dLSAME, dRSAME, mLDIFF, mRDIFF,
  mLCOMB3, mRCOMB3, mLCOMB4, mRCOMB4, dLDIFF, dRDIFF, mLAPF1, mRAPF1, mLAPF2, mRAPF2, vLIN, vRIN,
};

}  // namespace

const s16 Spu::kGauss[512] = {
    -0x001, -0x001, -0x001, -0x001, -0x001, -0x001, -0x001, -0x001,
    -0x001, -0x001, -0x001, -0x001, -0x001, -0x001, -0x001, -0x001,
    0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x001,
    0x001, 0x001, 0x001, 0x002, 0x002, 0x002, 0x003, 0x003,
    0x003, 0x004, 0x004, 0x005, 0x005, 0x006, 0x007, 0x007,
    0x008, 0x009, 0x009, 0x00A, 0x00B, 0x00C, 0x00D, 0x00E,
    0x00F, 0x010, 0x011, 0x012, 0x013, 0x015, 0x016, 0x018,
    0x019, 0x01B, 0x01C, 0x01E, 0x020, 0x021, 0x023, 0x025,
    0x027, 0x029, 0x02C, 0x02E, 0x030, 0x033, 0x035, 0x038,
    0x03A, 0x03D, 0x040, 0x043, 0x046, 0x049, 0x04D, 0x050,
    0x054, 0x057, 0x05B, 0x05F, 0x063, 0x067, 0x06B, 0x06F,
    0x074, 0x078, 0x07D, 0x082, 0x087, 0x08C, 0x091, 0x096,
    0x09C, 0x0A1, 0x0A7, 0x0AD, 0x0B3, 0x0BA, 0x0C0, 0x0C7,
    0x0CD, 0x0D4, 0x0DB, 0x0E3, 0x0EA, 0x0F2, 0x0FA, 0x101,
    0x10A, 0x112, 0x11B, 0x123, 0x12C, 0x135, 0x13F, 0x148,
    0x152, 0x15C, 0x166, 0x171, 0x17B, 0x186, 0x191, 0x19C,
    0x1A8, 0x1B4, 0x1C0, 0x1CC, 0x1D9, 0x1E5, 0x1F2, 0x200,
    0x20D, 0x21B, 0x229, 0x237, 0x246, 0x255, 0x264, 0x273,
    0x283, 0x293, 0x2A3, 0x2B4, 0x2C4, 0x2D6, 0x2E7, 0x2F9,
    0x30B, 0x31D, 0x330, 0x343, 0x356, 0x36A, 0x37E, 0x392,
    0x3A7, 0x3BC, 0x3D1, 0x3E7, 0x3FC, 0x413, 0x42A, 0x441,
    0x458, 0x470, 0x488, 0x4A0, 0x4B9, 0x4D2, 0x4EC, 0x506,
    0x520, 0x53B, 0x556, 0x572, 0x58E, 0x5AA, 0x5C7, 0x5E4,
    0x601, 0x61F, 0x63E, 0x65C, 0x67C, 0x69B, 0x6BB, 0x6DC,
    0x6FD, 0x71E, 0x740, 0x762, 0x784, 0x7A7, 0x7CB, 0x7EF,
    0x813, 0x838, 0x85D, 0x883, 0x8A9, 0x8D0, 0x8F7, 0x91E,
    0x946, 0x96F, 0x998, 0x9C1, 0x9EB, 0xA16, 0xA40, 0xA6C,
    0xA98, 0xAC4, 0xAF1, 0xB1E, 0xB4C, 0xB7A, 0xBA9, 0xBD8,
    0xC07, 0xC38, 0xC68, 0xC99, 0xCCB, 0xCFD, 0xD30, 0xD63,
    0xD97, 0xDCB, 0xE00, 0xE35, 0xE6B, 0xEA1, 0xED7, 0xF0F,
    0xF46, 0xF7F, 0xFB7, 0xFF1, 0x102A, 0x1065, 0x109F, 0x10DB,
    0x1116, 0x1153, 0x118F, 0x11CD, 0x120B, 0x1249, 0x1288, 0x12C7,
    0x1307, 0x1347, 0x1388, 0x13C9, 0x140B, 0x144D, 0x1490, 0x14D4,
    0x1517, 0x155C, 0x15A0, 0x15E6, 0x162C, 0x1672, 0x16B9, 0x1700,
    0x1747, 0x1790, 0x17D8, 0x1821, 0x186B, 0x18B5, 0x1900, 0x194B,
    0x1996, 0x19E2, 0x1A2E, 0x1A7B, 0x1AC8, 0x1B16, 0x1B64, 0x1BB3,
    0x1C02, 0x1C51, 0x1CA1, 0x1CF1, 0x1D42, 0x1D93, 0x1DE5, 0x1E37,
    0x1E89, 0x1EDC, 0x1F2F, 0x1F82, 0x1FD6, 0x202A, 0x207F, 0x20D4,
    0x2129, 0x217F, 0x21D5, 0x222C, 0x2282, 0x22DA, 0x2331, 0x2389,
    0x23E1, 0x2439, 0x2492, 0x24EB, 0x2545, 0x259E, 0x25F8, 0x2653,
    0x26AD, 0x2708, 0x2763, 0x27BE, 0x281A, 0x2876, 0x28D2, 0x292E,
    0x298B, 0x29E7, 0x2A44, 0x2AA1, 0x2AFF, 0x2B5C, 0x2BBA, 0x2C18,
    0x2C76, 0x2CD4, 0x2D33, 0x2D91, 0x2DF0, 0x2E4F, 0x2EAE, 0x2F0D,
    0x2F6C, 0x2FCC, 0x302B, 0x308B, 0x30EA, 0x314A, 0x31AA, 0x3209,
    0x3269, 0x32C9, 0x3329, 0x3389, 0x33E9, 0x3449, 0x34A9, 0x3509,
    0x3569, 0x35C9, 0x3629, 0x3689, 0x36E8, 0x3748, 0x37A8, 0x3807,
    0x3867, 0x38C6, 0x3926, 0x3985, 0x39E4, 0x3A43, 0x3AA2, 0x3B00,
    0x3B5F, 0x3BBD, 0x3C1B, 0x3C79, 0x3CD7, 0x3D35, 0x3D92, 0x3DEF,
    0x3E4C, 0x3EA9, 0x3F05, 0x3F62, 0x3FBD, 0x4019, 0x4074, 0x40D0,
    0x412A, 0x4185, 0x41DF, 0x4239, 0x4292, 0x42EB, 0x4344, 0x439C,
    0x43F4, 0x444C, 0x44A3, 0x44FA, 0x4550, 0x45A6, 0x45FC, 0x4651,
    0x46A6, 0x46FA, 0x474E, 0x47A1, 0x47F4, 0x4846, 0x4898, 0x48E9,
    0x493A, 0x498A, 0x49D9, 0x4A29, 0x4A77, 0x4AC5, 0x4B13, 0x4B5F,
    0x4BAC, 0x4BF7, 0x4C42, 0x4C8D, 0x4CD7, 0x4D20, 0x4D68, 0x4DB0,
    0x4DF7, 0x4E3E, 0x4E84, 0x4EC9, 0x4F0E, 0x4F52, 0x4F95, 0x4FD7,
    0x5019, 0x505A, 0x509A, 0x50DA, 0x5118, 0x5156, 0x5194, 0x51D0,
    0x520C, 0x5247, 0x5281, 0x52BA, 0x52F3, 0x532A, 0x5361, 0x5397,
    0x53CC, 0x5401, 0x5434, 0x5467, 0x5499, 0x54CA, 0x54FA, 0x5529,
    0x5558, 0x5585, 0x55B2, 0x55DE, 0x5609, 0x5632, 0x565B, 0x5684,
    0x56AB, 0x56D1, 0x56F6, 0x571B, 0x573E, 0x5761, 0x5782, 0x57A3,
    0x57C3, 0x57E2, 0x57FF, 0x581C, 0x5838, 0x5853, 0x586D, 0x5886,
    0x589E, 0x58B5, 0x58CB, 0x58E0, 0x58F4, 0x5907, 0x5919, 0x592A,
    0x593A, 0x5949, 0x5958, 0x5965, 0x5971, 0x597C, 0x5986, 0x598F,
    0x5997, 0x599E, 0x59A4, 0x59A9, 0x59AD, 0x59B0, 0x59B2, 0x59B3,
};

// ---- Envelopes ------------------------------------------------------------------

// One step every 2^max(0, shift - 11) samples, of (7 - n) or (-8 + n) shifted left by
// max(0, 11 - shift); exponential decrease scales the step by the level, exponential
// increase slows down above 0x6000 (by 4, split between step and period the way
// Mednafen measured it).
s32 Spu::Envelope::tick(s32 level) {
  const int shift = rate >> 2;
  s32 step = decreasing ? -8 + (rate & 3) : 7 - (rate & 3);
  s32 increment = 0x8000;
  if (shift < 11) step *= 1 << (11 - shift);
  else increment >>= std::min(shift - 11, 16);
  if (exponential) {
    if (decreasing) {
      step = (step * level) >> 15;
    } else if (level >= 0x6000) {
      if (rate < 40) {
        step >>= 2;
      } else if (rate >= 44) {
        increment >>= 2;
      } else {
        step >>= 1;
        increment >>= 1;
      }
    }
  }
  counter += increment;
  if (!(counter & 0x8000)) return level;
  counter = 0;
  return std::clamp<s32>(level + step, 0, 0x7FFF);
}

// Bit 15 clear: fixed volume (bits 0-14 are volume / 2). Set: a sweep, bit 14
// exponential, bit 13 decreasing, bits 0-6 the rate. (Bit 12, the inverted phase, is
// ignored, as in DuckStation.)
void Spu::VolumeSweep::set(u16 value) {
  reg = value;
  if (!(value & 0x8000)) {
    level = static_cast<s16>(value << 1);
    active = false;
    return;
  }
  env.reset(value & 0x7F, (value & 0x2000) != 0, (value & 0x4000) != 0);
  if (level < 0) level = 0;
  active = true;
}

void Spu::VolumeSweep::tick() {
  if (!active) return;
  level = env.tick(level);
  active = env.decreasing ? level > 0 : level < 0x7FFF;
}

// ---- Registers --------------------------------------------------------------------

void Spu::reset() {
  ram_.fill(0);
  regs_.fill(0);
  for (auto& v : voices_) v = Voice{};
  main_l_ = main_r_ = VolumeSweep{};
  endx_ = 0;
  transfer_address_ = 0;
  status_ = 0;
  capture_index_ = 0;
  noise_timer_ = 0;
  noise_level_ = 1;
  reverb_current_ = 0;
  resample_pos_ = 0;
  std::memset(reverb_in_hist_, 0, sizeof reverb_in_hist_);
  std::memset(reverb_out_hist_, 0, sizeof reverb_out_hist_);
}

u16 Spu::ram16(u32 byte_address) const {
  const u32 a = byte_address & (kRamSize - 2);
  return static_cast<u16>(ram_[a] | ram_[a + 1] << 8);
}

u16 Spu::read16(u32 offset) const {
  offset &= 0x3FE;
  if (offset < 0x180) {
    const Voice& v = voices_[offset >> 4];
    if ((offset & 0xF) == kVoiceEnvx) return static_cast<u16>(v.adsr_level);
    return v.regs[(offset & 0xF) >> 1];
  }
  if (offset >= kVoiceCurVol && offset < kVoiceCurVol + kVoices * 4) {
    const Voice& v = voices_[(offset - kVoiceCurVol) >> 2];
    return static_cast<u16>((offset & 2) ? v.vol_r.level : v.vol_l.level);
  }
  switch (offset) {
    case kEndxLo: return static_cast<u16>(endx_);
    case kEndxHi: return static_cast<u16>(endx_ >> 16);
    case kStatus: return static_cast<u16>((status_ & ~0x3F) | (control() & 0x3F));
    case kCurMainVolL: return static_cast<u16>(main_l_.level);
    case kCurMainVolR: return static_cast<u16>(main_r_.level);
    default: return regs_[offset >> 1];
  }
}

void Spu::write16(u32 offset, u16 value) {
  offset &= 0x3FE;
  regs_[offset >> 1] = value;
  if (offset < 0x180) {
    const int vi = static_cast<int>(offset >> 4);
    Voice& v = voices_[vi];
    const u32 r = (offset & 0xF) >> 1;
    v.regs[r] = value;
    switch (offset & 0xF) {
      case kVoiceVolL: v.vol_l.set(value); break;
      case kVoiceVolR: v.vol_r.set(value); break;
      case kVoiceAdsr1:
      case kVoiceAdsr2:
        if (v.phase != Phase::Off) update_adsr(vi);
        break;
      case kVoiceEnvx: v.adsr_level = std::min<s32>(static_cast<s16>(value), 0x7FFF); break;
      case kVoiceRepeat: v.ignore_loop_address = true; break;
      default: break;
    }
    return;
  }
  switch (offset) {
    case kMainVolL: main_l_.set(value); break;
    case kMainVolR: main_r_.set(value); break;
    case kKeyOnLo:
    case kKeyOnHi:
      for (int b = 0; b < 16; ++b)
        if (value & (1u << b)) {
          const int vi = b + (offset == kKeyOnHi ? 16 : 0);
          if (vi < kVoices) key_on(vi);
        }
      break;
    case kKeyOffLo:
    case kKeyOffHi:
      for (int b = 0; b < 16; ++b)
        if (value & (1u << b)) {
          const int vi = b + (offset == kKeyOffHi ? 16 : 0);
          if (vi < kVoices) key_off(vi);
        }
      break;
    case kReverbBase: reverb_current_ = static_cast<u32>(value) * 4u; break;
    case kTransferAddress: transfer_address_ = (static_cast<u32>(value) * 8u) & (kRamSize - 1); break;
    case kTransferFifo:
      check_irq(transfer_address_, 2);
      ram_[transfer_address_] = static_cast<u8>(value);
      ram_[transfer_address_ + 1] = static_cast<u8>(value >> 8);
      transfer_address_ = (transfer_address_ + 2) & (kRamSize - 1);
      break;
    case kControl:
      if (!(value & 0x40)) status_ &= static_cast<u16>(~0x40);
      break;
    default: break;
  }
}

void Spu::dma_write(const u16* data, size_t halfwords) {
  check_irq(transfer_address_, static_cast<u32>(halfwords * 2));
  for (size_t i = 0; i < halfwords; ++i) {
    ram_[transfer_address_] = static_cast<u8>(data[i]);
    ram_[transfer_address_ + 1] = static_cast<u8>(data[i] >> 8);
    transfer_address_ = (transfer_address_ + 2) & (kRamSize - 1);
  }
}

void Spu::dma_read(u16* data, size_t halfwords) {
  check_irq(transfer_address_, static_cast<u32>(halfwords * 2));
  for (size_t i = 0; i < halfwords; ++i) {
    data[i] = ram16(transfer_address_);
    transfer_address_ = (transfer_address_ + 2) & (kRamSize - 1);
  }
}

void Spu::check_irq(u32 byte_address, u32 bytes) {
  if (!(control() & 0x40) || (status_ & 0x40)) return;
  const u32 irq = static_cast<u32>(regs_[kIrqAddress >> 1]) * 8u;
  if (irq - byte_address < bytes) {
    status_ |= 0x40;
    if (on_irq) on_irq();
  }
}

// ---- Voices -----------------------------------------------------------------------

void Spu::key_on(int vi) {
  Voice& v = voices_[vi];
  v.current_address = v.regs[kVoiceStart >> 1] & ~1u;
  v.counter = 0;
  v.adsr_level = 0;
  v.adpcm_last[0] = v.adpcm_last[1] = 0;
  std::memset(v.samples, 0, sizeof v.samples);
  v.has_samples = false;
  v.ignore_loop_address = false;
  v.phase = Phase::Attack;
  update_adsr(vi);
  endx_ &= ~(1u << vi);
}

void Spu::key_off(int vi) {
  Voice& v = voices_[vi];
  if (v.phase == Phase::Off || v.phase == Phase::Release) return;
  v.phase = Phase::Release;
  update_adsr(vi);
}

void Spu::update_adsr(int vi) {
  Voice& v = voices_[vi];
  const u16 adsr1 = v.regs[kVoiceAdsr1 >> 1];
  const u16 adsr2 = v.regs[kVoiceAdsr2 >> 1];
  switch (v.phase) {
    case Phase::Attack:
      v.adsr.reset((adsr1 >> 8) & 0x7F, false, (adsr1 & 0x8000) != 0);
      v.adsr_target = 0x7FFF;
      break;
    case Phase::Decay:
      v.adsr.reset(static_cast<u8>(((adsr1 >> 4) & 0xF) << 2), true, true);
      v.adsr_target = ((adsr1 & 0xF) + 1) * 0x800;
      break;
    case Phase::Sustain:
      v.adsr.reset((adsr2 >> 6) & 0x7F, (adsr2 & 0x4000) != 0, (adsr2 & 0x8000) != 0);
      v.adsr_target = 0;
      break;
    case Phase::Release:
      v.adsr.reset(static_cast<u8>((adsr2 & 0x1F) << 2), true, (adsr2 & 0x20) != 0);
      v.adsr_target = 0;
      break;
    case Phase::Off: break;
  }
}

u8 Spu::decode_adpcm(const u8* block, s16 history[2], s16 out[28]) {
  u32 shift = block[0] & 0x0F;
  if (shift > 12) shift = 9;  // reserved shifts act as 9
  const u32 filter = std::min<u32>((block[0] >> 4) & 7, 4);
  s32 h1 = history[0], h2 = history[1];
  for (int i = 0; i < 28; ++i) {
    const u32 nibble = (block[2 + i / 2] >> ((i & 1) * 4)) & 0xF;
    s32 s = static_cast<s16>(static_cast<u16>(nibble << 12)) >> shift;
    s += (h1 * kFilterPos[filter] + h2 * kFilterNeg[filter] + 32) >> 6;
    s = clamp16(s);
    out[i] = static_cast<s16>(s);
    h2 = h1;
    h1 = s;
  }
  history[0] = static_cast<s16>(h1);
  history[1] = static_cast<s16>(h2);
  return block[1];
}

void Spu::decode_block(int vi) {
  Voice& v = voices_[vi];
  const u32 address = (v.current_address * 8u) & (kRamSize - 1);
  check_irq(address, 16);
  v.samples[0] = v.samples[28];
  v.samples[1] = v.samples[29];
  v.samples[2] = v.samples[30];
  v.block_flags = decode_adpcm(&ram_[address], v.adpcm_last, v.samples + 3);
}

s32 Spu::sample_voice(int vi, s32& left, s32& right) {
  Voice& v = voices_[vi];
  if (v.phase == Phase::Off) {
    v.last_output = 0;
    left = right = 0;
    return 0;
  }
  if (!v.has_samples) {
    decode_block(vi);
    v.has_samples = true;
    if ((v.block_flags & 4) && !v.ignore_loop_address) v.regs[kVoiceRepeat >> 1] = static_cast<u16>(v.current_address);
  }

  const u32 non = regs_[kNoiseLo >> 1] | regs_[kNoiseHi >> 1] << 16;
  const bool noise = ((non >> vi) & 1) != 0;
  s32 sample = 0;
  if (v.adsr_level != 0) {
    s32 raw;
    if (noise) {
      raw = static_cast<s16>(noise_level_);
    } else {
      // Gaussian interpolation over the newest four decoded samples.
      const u32 i = (v.counter >> 4) & 0xFF;
      const u32 s = 3 + (v.counter >> 12);
      s32 out = kGauss[0x0FF - i] * v.samples[s - 3];
      out += kGauss[0x1FF - i] * v.samples[s - 2];
      out += kGauss[0x100 + i] * v.samples[s - 1];
      out += kGauss[0x000 + i] * v.samples[s];
      raw = out >> 15;
    }
    sample = mul15(raw, v.adsr_level);
  }
  v.last_output = sample;

  // Envelope.
  v.adsr_level = v.adsr.tick(v.adsr_level);
  if (v.phase != Phase::Sustain) {
    // Decay ends below the sustain level (Mednafen: "< (SL + 1) << 11"); attack at
    // 0x7fff, release at 0.
    bool reached;
    if (v.phase == Phase::Decay) reached = v.adsr_level < v.adsr_target;
    else reached = v.adsr.decreasing ? v.adsr_level <= v.adsr_target : v.adsr_level >= v.adsr_target;
    if (reached) {
      switch (v.phase) {
        case Phase::Attack: v.phase = Phase::Decay; break;
        case Phase::Decay: v.phase = Phase::Sustain; break;
        case Phase::Release: v.phase = Phase::Off; break;
        default: break;
      }
      update_adsr(vi);
    }
  }

  // Pitch counter, with modulation by the previous voice's output.
  u32 step = v.regs[kVoicePitch >> 1];
  const u32 pmon = regs_[kPmonLo >> 1] | regs_[kPmonHi >> 1] << 16;
  if (vi > 0 && ((pmon >> vi) & 1)) {
    const s32 factor = clamp16(voices_[vi - 1].last_output) + 0x8000;
    step = static_cast<u32>((static_cast<s32>(static_cast<s16>(step)) * factor) >> 15) & 0xFFFF;
  }
  if (step > 0x3FFF) step = 0x4000;
  v.counter += step;
  if ((v.counter >> 12) >= 28) {
    v.counter -= 28u << 12;
    v.current_address = (v.current_address + 2) & 0xFFFF;
    v.has_samples = false;
    if (v.block_flags & 1) {
      endx_ |= 1u << vi;
      v.current_address = v.regs[kVoiceRepeat >> 1] & ~1u;
      if (!(v.block_flags & 2) && !noise) {
        v.phase = Phase::Off;
        v.adsr_level = 0;
      }
    }
  }

  left = mul15(sample, v.vol_l.level);
  right = mul15(sample, v.vol_r.level);
  v.vol_l.tick();
  v.vol_r.tick();
  return sample;
}

void Spu::update_noise() {
  const u32 shift = (control() >> 10) & 0xF;
  const s32 step = static_cast<s32>((control() >> 8) & 3) + 4;
  noise_timer_ -= step;
  if (noise_timer_ < 0) {
    const u32 parity = ((noise_level_ >> 15) ^ (noise_level_ >> 12) ^ (noise_level_ >> 11) ^ (noise_level_ >> 10) ^ 1) & 1;
    noise_level_ = static_cast<u16>((noise_level_ << 1) | parity);
    noise_timer_ += 0x20000 >> shift;
    if (noise_timer_ < 0) noise_timer_ += 0x20000 >> shift;
  }
}

// ---- Reverb -----------------------------------------------------------------------

// Work-area address (in halfwords) 'offset' halfwords after the current position,
// wrapping from the end of sound RAM back to the area's base (mBASE).
u32 Spu::reverb_address(u32 offset) const {
  const u32 base = static_cast<u32>(regs_[kReverbBase >> 1]) * 4u;
  u32 a = reverb_current_ + (offset & 0x3FFFF);
  if (a & 0x40000) a += base;
  return a & 0x3FFFF;
}

s16 Spu::reverb_read(u16 reg, s32 delta) const {
  const u32 off = static_cast<u32>(static_cast<s32>(regs_[(kReverbRegs >> 1) + reg]) * 4 + delta);
  return static_cast<s16>(ram16(reverb_address(off) * 2));
}

void Spu::reverb_write(u16 reg, s32 value) {
  const u32 a = reverb_address(static_cast<u32>(regs_[(kReverbRegs >> 1) + reg]) * 4u) * 2;
  const u16 v = static_cast<u16>(clamp16(value));
  ram_[a] = static_cast<u8>(v);
  ram_[a + 1] = static_cast<u8>(v >> 8);
}

// psx-spx "SPU Reverb Formula", run at 22.05 kHz between the two resampling filters.
void Spu::process_reverb(s32 in_l, s32 in_r, s32& out_l, s32& out_r) {
  const u32 pos = resample_pos_;
  reverb_in_hist_[0][pos & 63] = static_cast<s16>(in_l);
  reverb_in_hist_[1][pos & 63] = static_cast<s16>(in_r);
  if (pos & 1) {
    s32 in[2];
    for (int lr = 0; lr < 2; ++lr) {
      s32 acc = 0;
      for (int j = 0; j < 39; ++j) acc += kReverbFir[j] * reverb_in_hist_[lr][(pos - j) & 63];
      in[lr] = clamp16(acc >> 15);
    }
    auto R = [&](u16 r) { return static_cast<s32>(static_cast<s16>(regs_[(kReverbRegs >> 1) + r])); };
    auto rd = [&](u16 r, s32 delta = 0) { return static_cast<s32>(reverb_read(r, delta)); };
    const s32 l_in = mul15(in[0], R(vLIN));
    const s32 r_in = mul15(in[1], R(vRIN));
    const s32 iir = R(vIIR), wall = R(vWALL);
    if (control() & 0x80) {
      const s32 l_same = clamp16(mul15(l_in + mul15(rd(dLSAME), wall) - rd(mLSAME, -1), iir) + rd(mLSAME, -1));
      const s32 r_same = clamp16(mul15(r_in + mul15(rd(dRSAME), wall) - rd(mRSAME, -1), iir) + rd(mRSAME, -1));
      const s32 l_diff = clamp16(mul15(l_in + mul15(rd(dRDIFF), wall) - rd(mLDIFF, -1), iir) + rd(mLDIFF, -1));
      const s32 r_diff = clamp16(mul15(r_in + mul15(rd(dLDIFF), wall) - rd(mRDIFF, -1), iir) + rd(mRDIFF, -1));
      reverb_write(mLSAME, l_same);
      reverb_write(mRSAME, r_same);
      reverb_write(mLDIFF, l_diff);
      reverb_write(mRDIFF, r_diff);
    }
    s32 l = mul15(R(vCOMB1), rd(mLCOMB1)) + mul15(R(vCOMB2), rd(mLCOMB2)) + mul15(R(vCOMB3), rd(mLCOMB3)) +
            mul15(R(vCOMB4), rd(mLCOMB4));
    s32 r = mul15(R(vCOMB1), rd(mRCOMB1)) + mul15(R(vCOMB2), rd(mRCOMB2)) + mul15(R(vCOMB3), rd(mRCOMB3)) +
            mul15(R(vCOMB4), rd(mRCOMB4));
    // All-pass filters: [mxAPFn - dAPFn] is read once, before [mxAPFn] is written.
    auto apf = [&](s32 x, u16 m, u16 d, s32 coef) {
      const u32 off = static_cast<u32>((R(m) - static_cast<s32>(regs_[(kReverbRegs >> 1) + d])) * 4);
      const s32 delayed = static_cast<s16>(ram16(reverb_address(off) * 2));
      x = clamp16(x - mul15(coef, delayed));
      if (control() & 0x80) reverb_write(m, x);
      return clamp16(mul15(x, coef) + delayed);
    };
    l = apf(l, mLAPF1, dAPF1, R(vAPF1));
    r = apf(r, mRAPF1, dAPF1, R(vAPF1));
    l = apf(l, mLAPF2, dAPF2, R(vAPF2));
    r = apf(r, mRAPF2, dAPF2, R(vAPF2));
    reverb_current_ = reverb_address(1);
    reverb_out_hist_[0][pos & 63] = static_cast<s16>(l);
    reverb_out_hist_[1][pos & 63] = static_cast<s16>(r);
  } else {
    reverb_out_hist_[0][pos & 63] = 0;
    reverb_out_hist_[1][pos & 63] = 0;
  }
  s32 out[2];
  for (int lr = 0; lr < 2; ++lr) {
    s32 acc = 0;
    for (int j = 0; j < 39; ++j) acc += kReverbFir[j] * reverb_out_hist_[lr][(pos - j) & 63];
    out[lr] = clamp16(acc >> 14);
  }
  out_l = out[0];
  out_r = out[1];
  resample_pos_ = (pos + 1) & 63;
}

// ---- Mixer ------------------------------------------------------------------------

void Spu::tick(s16& left, s16& right) {
  s32 sum_l = 0, sum_r = 0, rev_l = 0, rev_r = 0;
  const bool enabled = (control() & 0x8000) != 0;
  if (enabled) {
    const u32 eon = regs_[kReverbOnLo >> 1] | regs_[kReverbOnHi >> 1] << 16;
    for (int vi = 0; vi < kVoices; ++vi) {
      s32 l, r;
      sample_voice(vi, l, r);
      sum_l += l;
      sum_r += r;
      if ((eon >> vi) & 1) {
        rev_l += l;
        rev_r += r;
      }
    }
  }

  // Capture buffers: CD left / right (silent here), voice 1 and voice 3.
  const u32 cap = capture_index_ * 2;
  const s16 captured[4] = {0, 0, static_cast<s16>(clamp16(voices_[1].last_output)),
                           static_cast<s16>(clamp16(voices_[3].last_output))};
  for (u32 k = 0; k < 4; ++k) {
    const u32 a = k * 0x400 + cap;
    check_irq(a, 2);
    ram_[a] = static_cast<u8>(captured[k]);
    ram_[a + 1] = static_cast<u8>(static_cast<u16>(captured[k]) >> 8);
  }
  capture_index_ = (capture_index_ + 1) & 0x1FF;
  status_ = static_cast<u16>((status_ & ~0x800) | (capture_index_ >= 0x100 ? 0x800 : 0));

  update_noise();

  s32 reverb_l, reverb_r;
  process_reverb(clamp16(rev_l), clamp16(rev_r), reverb_l, reverb_r);
  if (!(control() & 0x4000)) sum_l = sum_r = 0;  // SPUCNT bit 14 clear: muted
  sum_l += mul15(reverb_l, static_cast<s16>(regs_[kReverbVolL >> 1]));
  sum_r += mul15(reverb_r, static_cast<s16>(regs_[kReverbVolR >> 1]));
  s32 out_l = mul15(clamp16(sum_l), main_l_.level);
  s32 out_r = mul15(clamp16(sum_r), main_r_.level);
  main_l_.tick();
  main_r_.tick();
  if (!enabled) out_l = out_r = 0;
  left = static_cast<s16>(clamp16(out_l));
  right = static_cast<s16>(clamp16(out_r));
}

void Spu::generate(s16* stereo, size_t frames) {
  for (size_t i = 0; i < frames; ++i) tick(stereo[2 * i], stereo[2 * i + 1]);
}

}  // namespace ss::psx
