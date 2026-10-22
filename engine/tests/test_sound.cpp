// The boot executable's sound driver on the software SPU: sequencer timing (with
// synthetic data) and, given SS_DISC1, the banks and music of the real disc.

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "disc/archive.h"
#include "disc/disc_image.h"
#include "disc/slz.h"
#include "game/boot/sound.h"
#include "runtime/boot_exe.h"
#include "runtime/libapi.h"
#include "runtime/libspu.h"
#include "test.h"

using namespace ss;
using game::Sound;

namespace {

void put16(std::vector<u8>& v, size_t at, u32 x) {
  if (v.size() < at + 2) v.resize(at + 2);
  v[at] = static_cast<u8>(x);
  v[at + 1] = static_cast<u8>(x >> 8);
}
void put32(std::vector<u8>& v, size_t at, u32 x) {
  put16(v, at, x & 0xFFFF);
  put16(v, at + 2, x >> 16);
}

// A bank with one program (id 1, one region covering everything, volume 0x7f) and
// one looping ADPCM block, for slot 6 (byte 7 = 0).
std::vector<u8> synthetic_bank() {
  std::vector<u8> b(0x40, 0);
  put32(b, 0, 0x40);      // total size
  put32(b, 4, 0x30);      // header size (sample data offset)
  // Instrument table at +0xc: id, ADSR1 (attack 0, sustain level 0xf), ADSR2 (sustain
  // rate 0x7f, release shift 0), count 1.
  put16(b, 0x0C, 1);
  put16(b, 0x0E, 0x000F);
  put16(b, 0x10, 0x1FC0);
  b[0x12] = 0;
  b[0x13] = 1;
  b[0x14] = 0x00;  // note low / high, velocity low / high
  b[0x15] = 0x7F;
  b[0x16] = 0x00;
  b[0x17] = 0x7F;
  put32(b, 0x18, 0);  // sample address (relative to the slot)
  put32(b, 0x1C, 0);  // loop address
  b[0x20] = 0x7F;     // volume
  put32(b, 0x28, 0xFFFFFFFF);
  // Sample: one block of 0x4000, looping.
  b[0x30] = 0x00;
  b[0x31] = 0x07;
  for (int i = 2; i < 16; ++i) b[0x30 + i] = 0x44;
  return b;
}

// A sequence: track 0 plays 'notes' notes of 'length' ticks every 'spacing' ticks
// with program 1; the other tracks are empty.
std::vector<u8> synthetic_sequence(u8 tempo, int notes, u8 spacing, u8 length) {
  std::vector<u8> s(0x16 + 24 * 6, 0);
  put16(s, 0, 0xFFFF);
  put32(s, 4, 0x1234);
  put16(s, 8, 0x16);
  s[0xF] = tempo;
  const size_t patterns = s.size();
  const size_t pattern = patterns + 4;
  for (int t = 0; t < 24; ++t) {
    s[0x16 + t * 6 + 1] = 0x10;
    put16(s, 0x16 + t * 6 + 4, t == 0 ? static_cast<u32>(patterns) : 0);  // offset 0 holds 0xffff
  }
  put16(s, patterns, static_cast<u32>(pattern));
  put16(s, patterns + 2, 0xFFFF);
  std::vector<u8> cmd = {0x83, 0x00, 0x01, 0x00};
  for (int i = 0; i < notes; ++i) {
    for (u8 c : {u8{0x3C}, spacing, length, u8{0x7F}}) cmd.push_back(c);
  }
  cmd.push_back(0x80);
  s.insert(s.end(), cmd.begin(), cmd.end());
  return s;
}

// Entry #9's layout: u32 offset of the script table, u16 sequence offsets (from +4),
// sequences, then s16 script offsets and the scripts. One effect: sequence 0 plays a
// note.
std::vector<u8> synthetic_effects() {
  std::vector<u8> d(8, 0);
  put16(d, 4, 4);  // sequence 0 at +4 + 4
  const std::vector<u8> seq = {0x83, 0x00, 0x01, 0x00, 0x3C, 0x06, 0x06, 0x7F, 0x80};  // note, wait 6, length 6
  d.insert(d.end(), seq.begin(), seq.end());
  while (d.size() % 4) d.push_back(0);
  const size_t scripts = d.size();
  put32(d, 0, static_cast<u32>(scripts));
  put16(d, scripts, 4);  // script 0 right after the 2-entry table
  put16(d, scripts + 2, 4);
  put32(d, scripts + 4, 0x40000000);  // priority 0x40, group 0
  put32(d, scripts + 8, 0x03000000);  // start sequence 0
  put32(d, scripts + 12, 0x00000000);  // wait for it, end
  return d;
}

// Sample times of the key-ons the driver issues (SpuSetKey(SPU_ON, mask != 0)), one
// entry per voice keyed.
std::vector<long> g_key_ons;
long g_now = 0;
void record_key(long on_off, u32 mask) {
  if (on_off != psyq::SPU_ON) return;
  for (; mask != 0; mask &= mask - 1) g_key_ons.push_back(g_now);
}

struct Driver {
  std::unique_ptr<Sound> sound = std::make_unique<Sound>();
  std::vector<s16> out = std::vector<s16>(2);
  Driver() {
    psyq::libapi_reset();
    psyq::spu().reset();
    game::sound_init(sound.get());
    game::sound_start(sound.get());
    psyq::spu_trace_key = record_key;
    g_key_ons.clear();
    g_now = 0;
  }
  ~Driver() { psyq::spu_trace_key = nullptr; }
  // Runs 'samples' samples; returns the key-on times seen meanwhile.
  std::vector<long> run(long samples) {
    g_key_ons.clear();
    for (long i = 0; i < samples; ++i, ++g_now) psyq::spu_run(out.data(), 1);
    return g_key_ons;
  }
};

}  // namespace

// Music ticks come from root counter 2 at 125000000 / (tempo * 24) ticks of system
// clock / 8, i.e. 96 ticks per 44.1 kHz sample: at tempo 120 a tick is 43402 / 96 =
// 452.1 samples, so notes 24 ticks apart start 10850.5 samples apart. Below tempo 80
// the counter runs at double rate and every second interrupt is skipped.
TEST(sound_sequencer_timing) {
  for (const auto& [tempo, notes_apart] : std::vector<std::pair<u8, double>>{{120, 24 * 43402 / 96.0},
                                                                             {60, 2 * 24 * 43402 / 96.0},
                                                                             {200, 24 * 26041 / 96.0}}) {
    Driver d;
    const auto bank = synthetic_bank();
    game::sound_load_bank(d.sound.get(), bank.data(), 0, 0);
    CHECK_EQ(game::sound_bank_busy(d.sound.get()), 0);
    auto seq = synthetic_sequence(tempo, 4, 24, 12);
    game::sound_play_sequence(d.sound.get(), seq.data());
    const auto starts = d.run(static_cast<long>(notes_apart * 5));
    CHECK_EQ(starts.size(), 4u);
    if (starts.size() == 4) {
      for (size_t i = 1; i < 4; ++i) {
        const double gap = static_cast<double>(starts[i] - starts[i - 1]);
        CHECK(std::abs(gap - notes_apart) <= 1.0);
      }
      CHECK(starts[0] < notes_apart / 24 * 2 + 2);  // first note on the first music tick
    }
    // The music ended by itself: every track idle, nothing playing.
    CHECK((d.sound->flags & 1) == 0);
  }
}

// Effects run on their own ~65 Hz tick (every 65104 counter ticks = 678.2 samples): the
// script starts its track on the first effect tick and the track's first note (wait 2)
// sounds two effect ticks later. sound_stop / fades act on the next tick.
TEST(sound_effects_and_requests) {
  Driver d;
  const auto bank = synthetic_bank();
  game::sound_load_bank(d.sound.get(), bank.data(), 0, 0);
  auto effects = synthetic_effects();
  game::sound_set_sequences(d.sound.get(), effects.data());
  effects.assign(effects.size(), 0xEE);  // the driver keeps its own copy
  CHECK_EQ(game::sound_play_sfx(d.sound.get(), 0, 0, 0x7F40), 8);  // the last free slot
  const auto starts = d.run(4000);
  CHECK_EQ(starts.size(), 1u);
  if (!starts.empty()) CHECK(starts[0] > 678 && starts[0] < 3 * 679);

  // A fade out of the music lowers its level each effect tick and stops it at 0.
  auto seq = synthetic_sequence(120, 40, 24, 30);
  game::sound_play_sequence(d.sound.get(), seq.data());
  d.run(20000);
  CHECK((d.sound->flags & 1) != 0);
  game::sound_fade_out(d.sound.get(), 0x10);
  d.run(679 * 20);
  CHECK((d.sound->flags & 1) == 0);
  CHECK_EQ(d.sound->music_fade.direction, 0);

  // Output mode and volume requests are applied by the tick; mono centres every voice.
  game::sound_set_output_mode(d.sound.get(), 2);
  CHECK_EQ(game::sound_output_mode(d.sound.get()), 0);
  d.run(500);
  CHECK_EQ(game::sound_output_mode(d.sound.get()), 2);
  static std::vector<psyq::SpuVolume> volumes;
  volumes.clear();
  psyq::spu_trace_voice = [](const psyq::SpuVoiceAttr* a) { volumes.push_back(a->volume); };
  CHECK(game::sound_play_sfx(d.sound.get(), 0, 0, 0x7F10) != 0);  // pan 0x10: far left in stereo
  d.run(3000);
  psyq::spu_trace_voice = nullptr;
  CHECK(!volumes.empty());
  for (const auto& v : volumes) CHECK_EQ(v.left, v.right);
  game::sound_set_volume(d.sound.get(), 0x30, 0x7F);
  CHECK_EQ(d.sound->music_volume, 0x30);
}

namespace {

struct Disc {
  std::unique_ptr<disc::DiscImage> image;
  disc::Archive archive;
  bool ok = false;
};

Disc open_disc1() {
  Disc d;
  const char* path = std::getenv("SS_DISC1");
  if (!path || !*path) {
    std::printf("  (skipped: SS_DISC1 not set)\n");
    return d;
  }
  std::string error;
  d.image = disc::DiscImage::open(path, &error);
  d.ok = d.image && d.archive.open(*d.image, &error) && runtime::boot_exe_open(*d.image, &error);
  if (!d.ok) std::fprintf(stderr, "  cannot open %s: %s\n", path, error.c_str());
  return d;
}

u64 fnv1a(const u8* p, size_t n) {
  u64 h = 0xcbf29ce484222325ull;
  for (size_t i = 0; i < n; ++i) h = (h ^ p[i]) * 0x100000001b3ull;
  return h;
}

}  // namespace

// Bank #8 (the resident effects bank, with #9 behind it) as title_program_main loads
// it: the samples go to SPU RAM 0x1010 and the instrument table to bank slot 6 with
// the sample addresses relocated. The reference values come from the original running
// in PCSX-Redux, captured with breakpoints on SpuWrite (0x8001d990), SpuSetKey
// (0x8001d7d0) and SpuSetVoiceAttr (0x8001f480) logging the driver's interrupt count
// (Sound + 0x2c): the bytes the console DMAs, and the slot 6 table in its Sound object.
TEST(disc1_sound_bank_8_in_spu_ram) {
  auto disc = open_disc1();
  if (!disc.ok) return;
  const u32 size8 = disc.archive.entry_size(8), size9 = disc.archive.entry_size(9);
  std::vector<u8> data(size8 + size9);
  CHECK(disc.archive.read_from_entry(8, data.size(), data.data()));
  Driver d;
  game::sound_load_bank(d.sound.get(), data.data(), 0, 0);
  CHECK_EQ(game::sound_bank_busy(d.sound.get()), 0);
  const u32 header = read_u16(data.data() + 4);
  const u32 bytes = read_u32(data.data()) - header;
  CHECK_EQ(header, 0x44Cu);
  CHECK_EQ(bytes, 0xF980u);
  CHECK(std::memcmp(psyq::spu().ram() + 0x1010, data.data() + header, bytes) == 0);
  // What the console wrote (SpuWrite at 0x8001d990: transfer address 0x1010, 0xf980 bytes).
  CHECK(fnv1a(psyq::spu().ram() + 0x1010, 0xF980) == 0x84f1fb81a3e34c0bull);
  // The relocated table: first region's sample at 0x1010.
  const u8* table = d.sound->banks[6].instruments_base;
  CHECK_EQ(read_u32(table + 8 + 4), 0x1010u);
  CHECK(fnv1a(table, 0x440) == 0x1cbcffdd3de5c8c1ull);  // Sound + 0x1f00 on the console
  // Effect 1 (the menu cursor) through #9's tables.
  auto seq = disc::slz::unpack_block(std::span<const u8>(data).subspan(size8), 0, 0, nullptr);
  CHECK(seq.has_value());
  if (!seq) return;
  game::sound_set_sequences(d.sound.get(), seq->data());
  CHECK(game::sound_play_sfx(d.sound.get(), 1, 0, 0x7F40) != 0);
  CHECK(!d.run(44100 / 2).empty());  // its notes are keyed on
}

namespace {

// The driver's SpuSetKey / SpuSetVoiceAttr calls per interrupt, without voice numbers
// (which depend on what played before): per tick from the first key-on, the sorted
// list of "a<vol l>,<vol r>,<pitch>,<address>,<adsr1>,<adsr2>" and "k<on><voices>".
struct EventLog {
  std::map<long, std::vector<std::string>> per_tick;
  long first_key = -1;
};
EventLog* g_log = nullptr;
Sound* g_logged = nullptr;
void log_key(long on_off, u32 mask) {
  if (!mask) return;
  const long t = g_logged->ticks;
  if (on_off == psyq::SPU_ON && g_log->first_key < 0) g_log->first_key = t;
  g_log->per_tick[t].push_back("k" + std::to_string(on_off) + std::to_string(__builtin_popcount(mask)));
}
void log_voice(const psyq::SpuVoiceAttr* a) {
  char buf[96];
  std::snprintf(buf, sizeof buf, "a%d,%d,%04x,%05x,%04x,%04x", a->volume.left, a->volume.right, a->pitch, a->addr,
                a->adsr1, a->adsr2);
  g_log->per_tick[g_logged->ticks].push_back(buf);
}
u64 digest(EventLog& log) {
  std::string text;
  for (auto& [t, v] : log.per_tick) {
    if (t < log.first_key) continue;
    std::sort(v.begin(), v.end());
    text += std::to_string(t - log.first_key) + "|";
    for (size_t i = 0; i < v.size(); ++i) text += (i ? ";" : "") + v[i];
    text += "\n";
  }
  return fnv1a(reinterpret_cast<const u8*>(text.data()), text.size());
}

}  // namespace

// The title music (#2677) driven from bank #8 + #9 and its own bank, as the title
// screen starts it: the 974 SpuSetKey / SpuSetVoiceAttr calls of the piece (pitch,
// volume, ADSR and sample address of every note, interrupt by interrupt) digest to the
// same value as the calls the original makes in PCSX-Redux. The music starts on an
// even interrupt count, as it did there: once the piece's tempo drops below 80 only
// every second interrupt runs it, so the parity decides which.
TEST(disc1_title_music_matches_original) {
  auto disc = open_disc1();
  if (!disc.ok) return;
  const u32 size8 = disc.archive.entry_size(8), size9 = disc.archive.entry_size(9);
  std::vector<u8> base(size8 + size9);
  CHECK(disc.archive.read_from_entry(8, base.size(), base.data()));
  auto effects = disc::slz::unpack_block(std::span<const u8>(base).subspan(size8), 0, 0, nullptr);
  auto entry = disc.archive.read_entry(0xA75);
  CHECK(effects.has_value() && entry.has_value());
  if (!effects || !entry) return;
  auto seq = disc::slz::unpack_block(std::span<const u8>(*entry).subspan(4), 0, 0, nullptr);
  CHECK(seq.has_value());
  if (!seq) return;
  Driver d;
  game::sound_load_bank(d.sound.get(), base.data(), 0, 0);
  game::sound_set_sequences(d.sound.get(), effects->data());
  game::sound_set_volume(d.sound.get(), 0x40, 0x7F);
  game::sound_load_bank(d.sound.get(), entry->data() + read_u32(entry->data()), 0, 0);
  while (d.sound->ticks < 100 || (d.sound->ticks & 1) != 0) psyq::spu_run(d.out.data(), 1);
  EventLog log;
  g_log = &log;
  g_logged = d.sound.get();
  psyq::spu_trace_key = log_key;
  psyq::spu_trace_voice = log_voice;
  game::sound_play_sequence(d.sound.get(), seq->data());
  log.per_tick.clear();
  double sum2 = 0;
  for (long i = 0; i < 44100L * 30; ++i) {
    psyq::spu_run(d.out.data(), 1);
    sum2 += static_cast<double>(d.out[0]) * d.out[0];
  }
  psyq::spu_trace_voice = nullptr;
  size_t events = 0;
  for (auto& [t, v] : log.per_tick) events += t >= log.first_key ? v.size() : 0;
  CHECK_EQ(events, 974u);
  CHECK(digest(log) == 0x90b6da0235e1a3c0ull);
  // And it is loud enough: RMS over the 30 s near the original's (0.17 of full scale
  // over the first 26 s in the PCSX-Redux capture).
  const double rms = std::sqrt(sum2 / (44100.0 * 30)) / 32768.0;
  CHECK(rms > 0.10 && rms < 0.25);
}

namespace {

// SLZ method 1 as the console's slz_lzss runs it: until the zero-distance terminator,
// even past the header's unpacked size (#2674's tune ends 3 bytes later; disc::slz
// rejects that block today).
std::vector<u8> lzss_like_console(const u8* block) {
  const u8* src = block + 16;
  std::vector<u8> out;
  unsigned flags = 0, bits = 0;
  for (;;) {
    if (bits == 0) {
      flags = *src++;
      bits = 8;
    }
    const bool literal = flags & 1;
    flags >>= 1;
    --bits;
    if (literal) {
      out.push_back(*src++);
      continue;
    }
    const size_t distance = src[0] | (src[1] & 0xF) << 8;
    const size_t length = (src[1] >> 4) + 3;
    src += 2;
    if (distance == 0 || distance > out.size()) return out;
    for (size_t k = 0; k < length; ++k) out.push_back(out[out.size() - distance]);
  }
}

u64 fnv_text(const std::string& s) { return fnv1a(reinterpret_cast<const u8*>(s.data()), s.size()); }

}  // namespace

// The Sound Select demo tune (#2674, its bank in slot 7 next to bank #8): the notes it
// keys (per interrupt, from the first) and the pitch / sample / ADSR combinations its
// voices get match what the original does in PCSX-Redux over the same 588 interrupts
// (up to where the capture's script pressed Cross). (Volumes are left out: the screen sweeps the pan once a frame, and
// where frames fall between interrupts differs between the two.)
TEST(disc1_sound_select_tune_matches_original) {
  auto disc = open_disc1();
  if (!disc.ok) return;
  const u32 size8 = disc.archive.entry_size(8), size9 = disc.archive.entry_size(9);
  std::vector<u8> base(size8 + size9);
  CHECK(disc.archive.read_from_entry(8, base.size(), base.data()));
  auto effects = disc::slz::unpack_block(std::span<const u8>(base).subspan(size8), 0, 0, nullptr);
  auto entry = disc.archive.read_entry(0xA72);
  CHECK(effects.has_value() && entry.has_value());
  if (!effects || !entry) return;
  std::vector<u8> seq = lzss_like_console(entry->data() + 4);
  CHECK_EQ(seq.size(), 2633u);
  seq.resize(0x8000);
  Driver d;
  game::sound_load_bank(d.sound.get(), base.data(), 0, 0);
  game::sound_set_sequences(d.sound.get(), effects->data());
  game::sound_set_volume(d.sound.get(), 0x40, 0x7F);
  game::sound_load_bank(d.sound.get(), entry->data() + read_u32(entry->data()), 0, 0);
  while (d.sound->ticks < 100) psyq::spu_run(d.out.data(), 1);
  EventLog log;
  g_log = &log;
  g_logged = d.sound.get();
  psyq::spu_trace_key = log_key;
  psyq::spu_trace_voice = log_voice;
  game::sound_play_sequence(d.sound.get(), seq.data());
  log.per_tick.clear();
  while (log.first_key < 0 || d.sound->ticks < log.first_key + 588) psyq::spu_run(d.out.data(), 1);
  psyq::spu_trace_voice = nullptr;
  std::string keys, attrs;
  std::set<std::string> distinct;
  size_t key_events = 0;
  for (auto& [t, v] : log.per_tick) {
    if (t < log.first_key) continue;
    std::vector<std::string> k;
    for (const auto& e : v) {
      if (e[0] == 'k') {
        k.push_back(e);
      } else {
        // "a<l>,<r>,<pitch>,<addr>,<adsr1>,<adsr2>" -> "<pitch>,<addr>,<adsr1>,<adsr2>"
        const size_t second = e.find(',', e.find(',') + 1);
        distinct.insert(e.substr(second + 1));
      }
    }
    if (k.empty()) continue;
    std::sort(k.begin(), k.end());
    keys += std::to_string(t - log.first_key) + "|";
    for (size_t i = 0; i < k.size(); ++i) keys += (i ? ";" : "") + k[i];
    keys += "\n";
    key_events += k.size();
  }
  for (const auto& a : distinct) {
    // Redux's form: pitch,addr,adsr1,adsr2 -> "%04x,%05x,%04x,%04x" with "," before ADSR2.
    attrs += a + "\n";
  }
  CHECK_EQ(key_events, 46u);
  CHECK(fnv_text(keys) == 0xbaaa55290d124044ull);
  CHECK_EQ(distinct.size(), 9u);
  CHECK(fnv_text(attrs) == 0x2775ee72efb99db6ull);
}

// The menu's confirm effect (#2 of entry #9): an arpeggio played three times, the
// echo copies quieter, 2 effect ticks (~31 ms) a note. Pitches and volumes as the
// original's SpuSetVoiceAttr calls in PCSX-Redux (title screen, Cross pressed).
TEST(disc1_confirm_effect_matches_original) {
  auto disc = open_disc1();
  if (!disc.ok) return;
  const u32 size8 = disc.archive.entry_size(8), size9 = disc.archive.entry_size(9);
  std::vector<u8> base(size8 + size9);
  CHECK(disc.archive.read_from_entry(8, base.size(), base.data()));
  auto effects = disc::slz::unpack_block(std::span<const u8>(base).subspan(size8), 0, 0, nullptr);
  CHECK(effects.has_value());
  if (!effects) return;
  Driver d;
  game::sound_load_bank(d.sound.get(), base.data(), 0, 0);
  game::sound_set_sequences(d.sound.get(), effects->data());
  game::sound_set_volume(d.sound.get(), 0x40, 0x7F);
  d.run(2000);
  EventLog log;
  g_log = &log;
  g_logged = d.sound.get();
  psyq::spu_trace_key = log_key;
  psyq::spu_trace_voice = log_voice;
  CHECK(game::sound_play_sfx(d.sound.get(), 2, 0, 0x7F40) != 0);
  for (long i = 0; i < 44100; ++i) psyq::spu_run(d.out.data(), 1);
  psyq::spu_trace_voice = nullptr;
  std::vector<std::string> seen;
  for (auto& [tick, v] : log.per_tick)
    for (const auto& e : v)
      if (e[0] == 'a') seen.push_back(e.substr(1, e.find(',', e.find(',') + 1) + 4));  // "<l>,<r>,<pitch>"
  const std::vector<std::string> expected = {
      "4473,4544,1e22", "4473,4544,25f6", "4473,4544,2d25", "4473,4544,3c44",
      "819,832,1e22",   "819,832,25f6",   "819,832,2d25",   "819,832,3c44",
      "315,320,1e22",   "315,320,25f6",   "315,320,2d25",   "315,320,3c44"};
  CHECK(seen == expected);
  if (seen != expected)
    for (const auto& x : seen) std::printf("    %s\n", x.c_str());
}

namespace {

struct StreamLog {
  long key_on = -1, finished = -1;
  int transferred = 0, prepared = 0;
  std::vector<u32> irq;
  std::vector<psyq::SpuVoiceAttr> attrs;
};
StreamLog* g_stream = nullptr;

}  // namespace

// The prologue's first voice clip (#4504 at offset 32, the cue's pan 0x40, volume 0x7f)
// streams through SpuSt as in the original (PCSX-Redux, breakpoints on SpuStTransfer,
// the driver's three SpuSt callbacks, SpuSetIRQAddr and SpuSetVoiceAttr): the voice is
// set to pitch 0x89e, volume 11340 / 11520, ADSR 053f / 5fc3 at its stream buffer
// (0x70000 + voice * 0x100), prepared, keyed on, refilled 2078 times with the IRQ address
// alternating between the buffer's halves, and finished 19.6 s later (1177 vblanks).
TEST(disc1_prologue_clip_streams_like_original) {
  auto disc = open_disc1();
  if (!disc.ok) return;
  const u32 size8 = disc.archive.entry_size(8), size9 = disc.archive.entry_size(9);
  std::vector<u8> base(size8 + size9);
  CHECK(disc.archive.read_from_entry(8, base.size(), base.data()));
  auto effects = disc::slz::unpack_block(std::span<const u8>(base).subspan(size8), 0, 0, nullptr);
  auto script = disc.archive.read_entry(0x1198);
  CHECK(effects.has_value() && script.has_value());
  if (!effects || !script) return;
  Driver d;
  game::sound_load_bank(d.sound.get(), base.data(), 0, 0);
  game::sound_set_sequences(d.sound.get(), effects->data());
  d.run(2000);
  StreamLog log;
  g_stream = &log;
  psyq::spu_trace_stream = [](const char* what, u32 a, u32) {
    if (!std::strcmp(what, "transferred")) ++g_stream->transferred;
    if (!std::strcmp(what, "prepared")) ++g_stream->prepared;
    if (!std::strcmp(what, "finished") && g_stream->finished < 0) g_stream->finished = g_now;
    if (!std::strcmp(what, "irq")) g_stream->irq.push_back(a);
  };
  psyq::spu_trace_voice = [](const psyq::SpuVoiceAttr* a) { g_stream->attrs.push_back(*a); };
  const int slot = game::sound_open_clip(d.sound.get(), script->data() + 32, 2, nullptr);
  CHECK_EQ(slot, 8);
  game::sound_start_clip(d.sound.get(), slot, 0x7F, 0x40, 0x2000, 0);
  const int voice = d.sound->effects[slot - 1].alternate;
  g_key_ons.clear();
  for (long i = 0; i < 44100L * 22 && log.finished < 0; ++i, ++g_now) {
    psyq::spu_run(d.out.data(), 1);
    if (log.key_on < 0 && !g_key_ons.empty()) log.key_on = g_now;
  }
  psyq::spu_trace_stream = nullptr;
  psyq::spu_trace_voice = nullptr;
  CHECK_EQ(log.prepared, 1);
  CHECK_EQ(log.transferred, 2078);
  CHECK(log.key_on >= 0 && log.finished > log.key_on);
  const double seconds = static_cast<double>(log.finished - log.key_on) / 44100.0;
  CHECK(std::abs(seconds - 1177 / 59.94) < 0.05);
  // The IRQ address: the second half, the first, ... (the last one inside the final chunk).
  const u32 buffer = 0x70000u + static_cast<u32>(voice) * 0x100;
  CHECK_EQ(log.irq.size(), 2080u);  // one per refill, then the end of the last chunk (Redux: 2080)
  if (log.irq.size() > 3) {
    CHECK_EQ(log.irq[0], buffer + 0x80);
    CHECK_EQ(log.irq[1], buffer);
    CHECK_EQ(log.irq[2], buffer + 0x80);
    CHECK_EQ(log.irq.back(), buffer + 0x88);  // the last chunk's end, as in Redux (0x70888 for voice 8)
  }
  CHECK(!log.attrs.empty());
  if (!log.attrs.empty()) {
    const psyq::SpuVoiceAttr& a = log.attrs.front();
    CHECK_EQ(a.voice, 1u << voice);
    CHECK_EQ(a.pitch, 0x89E);
    CHECK_EQ(a.addr, buffer);
    CHECK_EQ(a.adsr1, 0x053F);
    CHECK_EQ(a.adsr2, 0x5FC3);
    CHECK_EQ(a.volume.left, 11340);
    CHECK_EQ(a.volume.right, 11520);
  }
}

// libspu's streaming on synthetic data: a voice plays 0x80-byte chunks through a 0x100
// buffer; the library refills each half on the SPU interrupt, calls back once per
// round, and the last chunk (SPU_ST_STOP) ends the stream with the finished callback.
TEST(spu_streaming_library) {
  psyq::libapi_reset();
  psyq::spu().reset();
  psyq::SpuInit();
  static std::vector<u8> data;
  data.assign(0x80 * 12, 0);
  for (size_t b = 0; b < data.size(); b += 16) {
    data[b] = 0x00;
    for (int i = 2; i < 16; ++i) data[b + i] = 0x22;  // a steady 0x2000
  }
  static psyq::SpuStEnv* env;
  static int prepared, transferred, finished;
  static u8* end;
  prepared = transferred = finished = 0;
  end = data.data() + data.size();
  env = psyq::SpuStInit(0);
  env->size = 0x100;
  env->voice[0].buf_addr = 0x70000;
  env->voice[0].data_addr = data.data();
  auto advance = [] {
    psyq::SpuStVoiceAttr& v = env->voice[0];
    v.data_addr += 0x80;
    if (end - v.data_addr <= 0x80) {
      v.status = static_cast<s8>(psyq::SPU_ST_STOP);
      v.last_size = static_cast<s32>(end - v.data_addr);
    }
  };
  static void (*advance_fn)() = nullptr;
  advance_fn = +advance;
  psyq::SpuStSetPreparationFinishedCallback([](u32 voices, long) {
    ++prepared;
    advance_fn();
    psyq::SpuStTransfer(psyq::SPU_ST_START, voices);
  });
  psyq::SpuStSetTransferFinishedCallback([](u32, long) {
    ++transferred;
    advance_fn();
  });
  psyq::SpuStSetStreamFinishedCallback([](u32, long status) {
    if (status == psyq::SPU_ST_FINAL) ++finished;
  });
  psyq::SpuVoiceAttr attr;
  attr.voice = 1;
  attr.mask = psyq::SPU_VOICE_VOLL | psyq::SPU_VOICE_VOLR | psyq::SPU_VOICE_PITCH | psyq::SPU_VOICE_WDSA |
              psyq::SPU_VOICE_ADSR_ADSR1 | psyq::SPU_VOICE_ADSR_ADSR2;
  attr.volume = {0x3FFF, 0x3FFF};
  attr.pitch = 0x1000;
  attr.addr = 0x70000;
  attr.adsr1 = 0x000F;
  attr.adsr2 = 0x1FC0;
  psyq::SpuSetVoiceAttr(&attr);
  psyq::SpuCommonAttr common;
  common.mask = psyq::SPU_COMMON_MVOLL | psyq::SPU_COMMON_MVOLR;
  common.mvol = {0x3FFF, 0x3FFF};
  psyq::SpuSetCommonAttr(&common);
  CHECK_EQ(psyq::SpuStTransfer(psyq::SPU_ST_PREPARE, 1), 1);
  std::vector<s16> out(2);
  long first_sound = -1, silent_from = -1;
  for (long i = 0; i < 44100; ++i) {
    psyq::spu_run(out.data(), 1);
    if (out[0] != 0 && first_sound < 0) first_sound = i;
    if (out[0] == 0 && first_sound >= 0 && silent_from < 0 && finished) silent_from = i;
  }
  CHECK_EQ(prepared, 1);
  CHECK_EQ(finished, 1);
  CHECK(transferred >= 9 && transferred <= 11);  // one per 0x80 bytes after the first two
  CHECK_EQ(psyq::SpuStGetVoiceStatus(), 0u);
  CHECK_EQ(psyq::SpuStGetStatus(), psyq::SPU_ST_IDLE);
  // 12 chunks of 8 blocks = 2688 samples at pitch 0x1000, played out before the end.
  CHECK(first_sound >= 0 && first_sound < 10);
  CHECK(psyq::spu().ram()[0x70001] == 6 || psyq::spu().ram()[0x70001] == 2);
  // The data's flags were put back where the last block was swapped for a silent loop.
  CHECK_EQ(data[data.size() - 16 + 2], 0x22);
}

// The field program's sound calls on synthetic data.
TEST(sound_field_helpers) {
  Driver d;
  const auto bank = synthetic_bank();
  game::sound_load_bank(d.sound.get(), bank.data(), 0, 0);
  // A sequence looping one note forever: 0x8d, note, 0x8e 0.
  std::vector<u8> s(0x16 + 24 * 6, 0);
  put16(s, 0, 0xFFFF);
  put16(s, 8, 0x16);
  s[0xF] = 120;
  const size_t patterns = s.size();
  for (int t = 0; t < 24; ++t) put16(s, 0x16 + t * 6 + 4, t == 0 ? static_cast<u32>(patterns) : 0);
  put16(s, patterns, static_cast<u32>(patterns + 4));
  put16(s, patterns + 2, 0xFFFF);
  for (u8 c : {u8{0x83}, u8{0}, u8{1}, u8{0}, u8{0x8D}, u8{0x3C}, u8{12}, u8{6}, u8{0x7F}, u8{0x8E}, u8{0}, u8{0x80}})
    s.push_back(c);
  game::sound_play_sequence(d.sound.get(), s.data());
  CHECK_EQ(d.run(452 * 12 * 6).size(), 6u);  // a note every 12 ticks of 452 samples
  game::sound_set_tempo(d.sound.get(), 64);   // twice as fast
  d.run(1000);
  CHECK_EQ(d.run(452 * 12 * 6).size(), 12u);
  game::sound_set_tempo(d.sound.get(), 0);
  game::sound_end_loops(d.sound.get(), 1);    // leaves the loop at its end: the music ends
  d.run(452 * 30);
  CHECK((d.sound->flags & 1) == 0);
  // Pause (0 frames: at once) and resume.
  game::sound_play_sequence(d.sound.get(), s.data());
  d.run(3000);
  game::sound_pause(d.sound.get(), 0, 0);
  d.run(1000);
  CHECK((d.sound->flags & 1) == 0);
  CHECK(d.run(452 * 30).empty());
  game::sound_resume(d.sound.get(), 0);
  d.run(1000);
  CHECK((d.sound->flags & 1) != 0);
  CHECK(!d.run(452 * 30).empty());
  // Effects: the playing slot reports its id; a fade in of the effects.
  auto effects = synthetic_effects();
  game::sound_set_sequences(d.sound.get(), effects.data());
  const int slot = game::sound_play_sfx(d.sound.get(), 0, 0, 0x7F40);
  CHECK(slot != 0);
  CHECK_EQ(game::sound_effect_playing(d.sound.get(), slot), 0u);  // id 0
  game::sound_fade_in_all(d.sound.get(), 30);
  d.run(700);
  CHECK_EQ(d.sound->effects_fade.direction, 1);
  CHECK(d.sound->effects_fade.level < 0x7F);
  CHECK_EQ(game::sound_bank_free_space(d.sound.get(), 6), 0xF400 - 0);
  game::sound_set_bank_loaded(d.sound.get(), 3, 1);
  CHECK_EQ(game::sound_bank_loaded(d.sound.get(), 3), 1);
}

// The other clip modes (the Voice Collection uses 1 for short clips): mode 1 loads the
// samples to bank slot 4 or 5 (SPU 0x28000 / 0x2d800, alternating; the first one takes
// 5) and plays them as an effect; mode 0 only loads, and sound_play_sfx plays the id it
// hands back.
TEST(disc1_clip_load_modes) {
  auto disc = open_disc1();
  if (!disc.ok) return;
  auto script = disc.archive.read_entry(0x1198);
  CHECK(script.has_value());
  if (!script) return;
  Driver d;
  u8* clip = script->data() + 32;
  const u8* bank = clip + read_u32(clip);
  const u32 samples = read_u32(bank) - read_u16(bank + 4);
  const int slot = game::sound_open_clip(d.sound.get(), clip, 1, nullptr);
  CHECK(slot >= 1 && slot <= 8);
  const u32 spu = d.sound->effects[slot - 1].alternate ? 0x2D800 : 0x28000;
  CHECK_EQ(spu, 0x2D800u);
  CHECK(std::memcmp(psyq::spu().ram() + spu, bank + read_u16(bank + 4), samples) == 0);
  static std::vector<u32> addrs;
  addrs.clear();
  static u32 expected_addr;
  expected_addr = spu;
  psyq::spu_trace_voice = [](const psyq::SpuVoiceAttr* a) { addrs.push_back(a->addr); };
  CHECK(!d.run(44100).empty());
  psyq::spu_trace_voice = nullptr;
  CHECK(!addrs.empty() && addrs.front() == expected_addr);
  game::sound_release_clip(d.sound.get(), slot);
  int id = 0;
  CHECK(game::sound_open_clip(d.sound.get(), clip, 0, &id) != 0);
  CHECK(id == 5 << 24 || id == 6 << 24);
  d.run(100);
  CHECK(game::sound_play_sfx(d.sound.get(), id, 0, 0x7F40) != 0);
  CHECK(!d.run(44100).empty());
}
