// Software SPU: ADPCM decoding, envelope timing, pitch, loops and reverb.

#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

#include "psx/spu.h"
#include "test.h"

using namespace ss;
using psx::Spu;

namespace {

// A block whose 28 nibbles are given low nibble first.
std::vector<u8> block(u8 header, u8 flags, const std::vector<u8>& nibbles) {
  std::vector<u8> b(16, 0);
  b[0] = header;
  b[1] = flags;
  for (size_t i = 0; i < nibbles.size() && i < 28; ++i) b[2 + i / 2] |= static_cast<u8>((nibbles[i] & 0xF) << ((i & 1) * 4));
  return b;
}

void write_ram(Spu& spu, u32 address, const std::vector<u8>& data) { std::memcpy(spu.ram() + address, data.data(), data.size()); }

// One voice set up to play a sample at 'address' (8-byte aligned).
void setup_voice(Spu& spu, int v, u32 address, u16 pitch, u16 adsr1, u16 adsr2) {
  const u32 r = static_cast<u32>(v) * 16;
  spu.write16(r + Spu::kVoiceVolL, 0x3FFF);
  spu.write16(r + Spu::kVoiceVolR, 0x3FFF);
  spu.write16(r + Spu::kVoicePitch, pitch);
  spu.write16(r + Spu::kVoiceStart, static_cast<u16>(address / 8));
  spu.write16(r + Spu::kVoiceAdsr1, adsr1);
  spu.write16(r + Spu::kVoiceAdsr2, adsr2);
}

void key_on(Spu& spu, int v) { spu.write16(v < 16 ? Spu::kKeyOnLo : Spu::kKeyOnHi, static_cast<u16>(1u << (v & 15))); }
void key_off(Spu& spu, int v) { spu.write16(v < 16 ? Spu::kKeyOffLo : Spu::kKeyOffHi, static_cast<u16>(1u << (v & 15))); }

// A looping block of silence at 'address' (start + end + repeat).
void silent_loop(Spu& spu, u32 address) { write_ram(spu, address, block(0, 0x07, {})); }

std::unique_ptr<Spu> fresh_spu() {
  auto spu = std::make_unique<Spu>();
  spu->write16(Spu::kControl, 0xC000);  // on, unmuted
  spu->write16(Spu::kMainVolL, 0x3FFF);
  spu->write16(Spu::kMainVolR, 0x3FFF);
  return spu;
}

}  // namespace

TEST(spu_adpcm_decode_vectors) {
  s16 h[2] = {0, 0};
  s16 out[28];
  // Shift 0, filter 0: each nibble is the sample's top four bits.
  auto b = block(0x00, 0, {1, 2, 7, 8, 0xF, 0});
  Spu::decode_adpcm(b.data(), h, out);
  CHECK_EQ(out[0], 4096);
  CHECK_EQ(out[1], 8192);
  CHECK_EQ(out[2], 28672);
  CHECK_EQ(out[3], -32768);
  CHECK_EQ(out[4], -4096);
  CHECK_EQ(out[5], 0);
  CHECK_EQ(h[0], 0);

  // Shift 12, filter 1 (s + old * 60 / 64, rounded): 7, 7 + 7 = 14, 7 + 13 = 20 ...
  h[0] = h[1] = 0;
  b = block(0x1C, 0, std::vector<u8>(28, 7));
  Spu::decode_adpcm(b.data(), h, out);
  CHECK_EQ(out[0], 7);
  CHECK_EQ(out[1], 14);
  CHECK_EQ(out[2], 20);
  s32 expect = 0, prev = 0;
  for (int i = 0; i < 28; ++i) {
    expect = 7 + ((prev * 60 + 32) >> 6);
    prev = expect;
  }
  CHECK_EQ(out[27], expect);
  CHECK_EQ(h[0], expect);

  // Filter 2 uses both history samples (115, -52); filters 5-7 act as 4, shifts 13-15 as 9.
  h[0] = 1000;
  h[1] = 500;
  b = block(0x2C, 0, {0});
  Spu::decode_adpcm(b.data(), h, out);
  CHECK_EQ(out[0], (1000 * 115 + 500 * -52 + 32) >> 6);
  s16 h4[2] = {1000, 500}, h7[2] = {1000, 500};
  s16 out4[28], out7[28];
  auto b4 = block(0x49, 0, {3, 0xC, 5}), b7 = block(0x7D, 0, {3, 0xC, 5});
  Spu::decode_adpcm(b4.data(), h4, out4);
  Spu::decode_adpcm(b7.data(), h7, out7);
  CHECK(std::memcmp(out4, out7, sizeof out4) == 0);
  // Saturation.
  h[0] = 32000;
  h[1] = -32000;
  b = block(0x40, 0, {7});
  Spu::decode_adpcm(b.data(), h, out);
  CHECK_EQ(out[0], 32767);
  // The flag byte is returned.
  CHECK_EQ(Spu::decode_adpcm(block(0, 0x03, {}).data(), h, out), 3);
}

// Attack 0 (fastest linear): +14336 a sample, so 0x7fff on the third sample. Attack
// 0x30 (shift 12, step 7): +7 every second sample, 4681 steps.
TEST(spu_adsr_attack_timing) {
  for (const auto& [rate, expected] : std::vector<std::pair<u16, int>>{{0x00, 3}, {0x30, 9362}, {0x2C, 4681}}) {
    auto spu = fresh_spu();
    silent_loop(*spu, 0x1000);
    setup_voice(*spu, 0, 0x1000, 0x1000, static_cast<u16>(rate << 8 | 0xF), 0);
    key_on(*spu, 0);
    int samples = 0;
    s16 l, r;
    while (spu->voice_envelope(0) < 0x7FFF && samples < 100000) {
      spu->tick(l, r);
      ++samples;
    }
    CHECK_EQ(samples, expected);
  }
}

// Exponential attack slows to a quarter above 0x6000; decay is exponential down to
// the sustain level, which holds with sustain rate 0x7f; release (linear, shift 0:
// -8 << 11 a sample) reaches 0 and the voice stops.
TEST(spu_adsr_phases) {
  s16 l, r;
  {
    auto spu = fresh_spu();
    silent_loop(*spu, 0x1000);
    // Exponential attack 0x34 (shift 13): +7 every 4 samples below 0x6000, then every 16.
    setup_voice(*spu, 0, 0x1000, 0x1000, static_cast<u16>((0x80 | 0x34) << 8 | 0x0F), 0);
    key_on(*spu, 0);
    int samples = 0;
    while (spu->voice_envelope(0) < 0x6000) {
      spu->tick(l, r);
      ++samples;
    }
    const int fast_steps = (0x6000 + 6) / 7;
    CHECK_EQ(samples, fast_steps * 4);
    const int at = samples;
    while (spu->voice_envelope(0) < 0x7FFF) {
      spu->tick(l, r);
      ++samples;
    }
    CHECK_EQ(samples - at, (0x7FFF - fast_steps * 7 + 6) / 7 * 16);
  }
  {
    // Decay shift 0 (exponential: -16384 * level / 0x8000 a sample) to sustain level
    // 0x800 * (1 + 1); sustain rate 0x7f never moves; release shift 0, linear.
    auto spu = fresh_spu();
    silent_loop(*spu, 0x1000);
    setup_voice(*spu, 0, 0x1000, 0x1000, 0x0001, 0x7F << 6);
    key_on(*spu, 0);
    for (int i = 0; i < 3; ++i) spu->tick(l, r);
    CHECK_EQ(spu->voice_envelope(0), 0x7FFF);
    int decay = 0;
    while (spu->voice_envelope(0) > 0x1000 && decay < 10000) {
      spu->tick(l, r);
      ++decay;
    }
    // Halving every sample: 0x7fff -> 0x1000 in 3 samples.
    CHECK_EQ(decay, 3);
    const u16 sustain = spu->voice_envelope(0);
    for (int i = 0; i < 1000; ++i) spu->tick(l, r);
    CHECK_EQ(spu->voice_envelope(0), sustain);
    key_off(*spu, 0);
    spu->tick(l, r);
    CHECK_EQ(spu->voice_envelope(0), 0);
    CHECK(!spu->voice_on(0));
  }
}

// Pitch 0x1000 reads one sample per output sample; a loop block sets ENDX and jumps to
// the repeat address; an end block without repeat silences the voice.
TEST(spu_voice_pitch_and_loops) {
  auto spu = fresh_spu();
  // Two blocks: the first marks the loop start, the second ends and repeats.
  auto b0 = block(0x00, 0x04, std::vector<u8>(28, 1));
  auto b1 = block(0x00, 0x03, std::vector<u8>(28, 2));
  write_ram(*spu, 0x2000, b0);
  write_ram(*spu, 0x2010, b1);
  setup_voice(*spu, 0, 0x2000, 0x1000, 0x000F, 0x0000);
  key_on(*spu, 0);
  s16 l, r;
  for (int i = 0; i < 56; ++i) spu->tick(l, r);
  CHECK_EQ(spu->endx() & 1, 1u);
  CHECK_EQ(spu->voice_address(0), 0x2000u);  // back at the loop start
  // Double pitch: two blocks in 28 samples.
  auto spu2 = fresh_spu();
  write_ram(*spu2, 0x2000, b0);
  write_ram(*spu2, 0x2010, block(0x00, 0x01, std::vector<u8>(28, 2)));  // end, no repeat
  setup_voice(*spu2, 0, 0x2000, 0x2000, 0x000F, 0x0000);
  key_on(*spu2, 0);
  for (int i = 0; i < 27; ++i) spu2->tick(l, r);
  CHECK(spu2->voice_on(0));
  spu2->tick(l, r);
  CHECK(!spu2->voice_on(0));  // end without repeat: off at once
  CHECK_EQ(spu2->endx() & 1, 1u);
  // Key on clears ENDX.
  key_on(*spu2, 0);
  CHECK_EQ(spu2->endx() & 1, 0u);
}

// A steady tone through the Gaussian interpolator keeps its level: a DC sample of
// 0x4000 comes out near 0x4000 * (sum of the four taps ~ 0x7f6..0x8000) / 0x8000.
TEST(spu_gaussian_and_output_level) {
  auto spu = fresh_spu();
  write_ram(*spu, 0x3000, block(0x00, 0x07, std::vector<u8>(28, 4)));  // 0x4000, looping
  setup_voice(*spu, 0, 0x3000, 0x0C00, 0x000F, 0x0000);
  key_on(*spu, 0);
  s16 l = 0, r = 0;
  for (int i = 0; i < 400; ++i) spu->tick(l, r);
  // 0x4000 * Gaussian gain (0x7f80 / 0x8000) * envelope 0x7fff * voice 0x7ffe * main 0x7ffe.
  const s32 expected = ((((0x4000 * 0x7F80 >> 15) * 0x7FFF >> 15) * 0x7FFE >> 15) * 0x7FFE) >> 15;
  CHECK(std::abs(l - expected) <= 4);
  CHECK_EQ(l, r);
  // The ROM's taps sum to about 1.0 at every phase.
  for (int i = 0; i < 256; ++i) {
    const int sum = Spu::kGauss[0xFF - i] + Spu::kGauss[0x1FF - i] + Spu::kGauss[0x100 + i] + Spu::kGauss[i];
    CHECK(sum >= 0x7F7F && sum <= 0x7F81);
  }
  // Muting (SPUCNT bit 14 clear) silences the voices.
  spu->write16(Spu::kControl, 0x8000);
  for (int i = 0; i < 4; ++i) spu->tick(l, r);
  CHECK_EQ(l, 0);
}

// Reverb: a comb delay of 0x200 steps (22.05 kHz) through two all-pass stages used as
// 4-step delays. Without the reverb master enable the work area is never written and
// only what is already in it plays; with it, an impulse returns ~23 ms later, bounded,
// and the dry path is unchanged.
TEST(spu_reverb_sanity) {
  auto setup = [](Spu& spu, bool enable) {
    spu.write16(Spu::kControl, enable ? 0xC080 : 0xC000);
    spu.write16(Spu::kReverbVolL, 0x7FFF);
    spu.write16(Spu::kReverbVolR, 0x7FFF);
    spu.write16(Spu::kReverbBase, 0xE000);  // 0x70000
    auto reg = [&](int i, u16 v) { spu.write16(Spu::kReverbRegs + i * 2, v); };
    reg(0, 1);         // dAPF1
    reg(1, 1);         // dAPF2
    reg(2, 0x7FFF);    // vIIR
    reg(3, 0x7FFF);    // vCOMB1
    reg(10, 0x100);    // mLSAME
    reg(11, 0x100);    // mRSAME
    reg(12, 0x080);    // mLCOMB1
    reg(13, 0x080);    // mRCOMB1
    reg(26, 0x400);    // mLAPF1
    reg(27, 0x500);    // mRAPF1
    reg(28, 0x600);    // mLAPF2
    reg(29, 0x700);    // mRAPF2
    reg(30, 0x7FFF);   // vLIN
    reg(31, 0x7FFF);   // vRIN
    write_ram(spu, 0x3000, block(0x00, 0x01, std::vector<u8>(28, 4)));  // one block, then off
    write_ram(spu, 0x3010, block(0x00, 0x00, {}));
    setup_voice(spu, 0, 0x3000, 0x1000, 0x000F, 0x0000);
    spu.write16(Spu::kReverbOnLo, 1);
  };
  for (bool enable : {false, true}) {
    auto spu = fresh_spu();
    setup(*spu, enable);
    key_on(*spu, 0);
    std::vector<s16> out;
    s16 l, r;
    for (int i = 0; i < 4000; ++i) {
      spu->tick(l, r);
      out.push_back(l);
    }
    int late_peak = 0;
    for (size_t i = 200; i < out.size(); ++i) late_peak = std::max(late_peak, std::abs(static_cast<int>(out[i])));
    bool touched = false;
    for (u32 a = 0x70000; a < 0x80000; ++a) touched |= spu->ram()[a] != 0;
    if (!enable) {
      CHECK(!touched);
      CHECK_EQ(late_peak, 0);
    } else {
      CHECK(touched);
      CHECK(late_peak > 0x400 && late_peak <= 0x7FFF);
      // The echo arrives about 2 * (0x100 - 0x80) * 4 samples after the note.
      int first = -1;
      for (size_t i = 200; i < out.size() && first < 0; ++i)
        if (std::abs(static_cast<int>(out[i])) > 0x100) first = static_cast<int>(i);
      CHECK(first > 900 && first < 1200);
    }
  }
}

// Data transfers: DMA and FIFO writes land at the transfer address and advance it; the
// IRQ address raises SPUSTAT bit 6 when a transfer touches it.
TEST(spu_transfers_and_irq) {
  Spu spu;
  spu.write16(Spu::kControl, 0xC040);  // IRQ enabled
  spu.write16(Spu::kIrqAddress, 0x1004 / 8 * 1);
  int irqs = 0;
  spu.on_irq = [&] { ++irqs; };
  spu.write16(Spu::kTransferAddress, 0x1000 / 8);
  const u16 data[4] = {0x1234, 0x5678, 0x9ABC, 0xDEF0};
  spu.dma_write(data, 4);
  CHECK_EQ(spu.ram16(0x1000), 0x1234);
  CHECK_EQ(spu.ram16(0x1006), 0xDEF0);
  CHECK_EQ(irqs, 1);
  CHECK((spu.read16(Spu::kStatus) & 0x40) != 0);
  spu.write16(Spu::kTransferFifo, 0xAAAA);  // continues at 0x1008
  CHECK_EQ(spu.ram16(0x1008), 0xAAAA);
  spu.write16(Spu::kControl, 0xC000);  // acknowledge
  CHECK((spu.read16(Spu::kStatus) & 0x40) == 0);
  u16 back[2];
  spu.write16(Spu::kTransferAddress, 0x1000 / 8);
  spu.dma_read(back, 2);
  CHECK_EQ(back[1], 0x5678);
}
