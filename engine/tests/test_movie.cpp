// Movie player tests: the libpress layer (DecDCTvlc / DecDCTin / DecDCTout) on
// hand-built data, then, with SS_DISC1 set, the title program's movie player and
// prologue cue player on the real entries. Expected pictures come from psx::mdec,
// itself checked against jPSXdec (test_mdec.cpp).

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "disc/archive.h"
#include "disc/str.h"
#include "game/boot/loader.h"
#include "game/boot/pad.h"
#include "game/boot/system.h"
#include "game/title/movie.h"
#include "psx/mdec.h"
#include "runtime/libgpu.h"
#include "runtime/runtime.h"
#include "test.h"

using namespace ss;
namespace mdec = ss::psx::mdec;

namespace {

// An STR bitstream from MSB-first bits packed in little-endian 16-bit words.
std::vector<u32> bitstream(const char* bits, u16 code_words, u16 qscale) {
  std::vector<u16> words;
  int used = 16;
  for (; *bits; ++bits) {
    if (*bits == ' ') continue;
    if (used == 16) {
      words.push_back(0);
      used = 0;
    }
    if (*bits == '1') words.back() |= static_cast<u16>(1 << (15 - used));
    ++used;
  }
  std::vector<u32> out = {0x38000000u | code_words, u32{qscale} | 2u << 16};
  for (size_t i = 0; i < words.size(); i += 2)
    out.push_back(words[i] | (i + 1 < words.size() ? u32{words[i + 1]} << 16 : 0));
  out.resize(out.size() + 4, 0);
  return out;
}

// A 16x16 version 2 frame: Cr DC +5 with two AC codes, then five blocks of DC -1.
constexpr const char* kFrame16 =
    "0000000101 110 0111 10"
    "1111111111 10 1111111111 10 1111111111 10 1111111111 10 1111111111 10";
const std::vector<u16> kFrame16Codes = {0x0405, 0x0001, 0x07FF, 0xFE00, 0x07FF, 0xFE00, 0x07FF,
                                        0xFE00, 0x07FF, 0xFE00, 0x07FF, 0xFE00, 0x07FF, 0xFE00};

int g_callbacks = 0;
void count_callback() { ++g_callbacks; }

struct Disc {
  std::unique_ptr<disc::DiscImage> image;
  disc::Archive archive;
};

// Opens SS_DISC1 and hands it to the System's loader, as boot_main does.
Disc* open_disc1() {
  static Disc disc;
  static bool tried = false, ok = false;
  if (!tried) {
    tried = true;
    const char* path = std::getenv("SS_DISC1");
    if (path && *path) {
      std::string error;
      disc.image = disc::DiscImage::open(path, &error);
      ok = disc.image && disc.archive.open(*disc.image, &error);
      if (!ok) std::fprintf(stderr, "  cannot open %s: %s\n", path, error.c_str());
      if (ok) {
        game::boot_main(0, &disc.archive);
        game::pad_set_port_mask(&game::get_system()->pad, 3);  // as title_intro_sequence does
      }
    }
  }
  if (!ok) std::printf("  (skipped: SS_DISC1 not set)\n");
  return ok ? &disc : nullptr;
}

// The displayed 24-bit picture, as RGB bytes.
std::vector<u8> displayed_rgb() {
  std::vector<u32> rgba;
  int w = 0, h = 0;
  runtime::Runtime::get().gpu.display_to_rgba(rgba, w, h);
  std::vector<u8> rgb;
  for (u32 p : rgba) {
    rgb.push_back(static_cast<u8>(p));
    rgb.push_back(static_cast<u8>(p >> 8));
    rgb.push_back(static_cast<u8>(p >> 16));
  }
  return rgb;
}

// The decoder's picture of frame 'index' of an STR entry.
std::vector<u8> decoded_frame(Disc& d, u32 entry, u32 index) {
  disc::str::FrameReader reader(*d.image, d.archive.entry(entry));
  disc::str::Frame frame;
  for (u32 i = 0; reader.next(frame); ++i) {
    if (i == index) return mdec::decode_frame(frame.bitstream, frame.width, frame.height).value_or(std::vector<u8>{});
  }
  return {};
}

}  // namespace

TEST(libpress_vlc_keeps_command_and_pads) {
  const auto bs = bitstream(kFrame16, 16, 1);
  std::vector<u32> buf(64, 0xDEADBEEF);
  CHECK_EQ(psyq::DecDCTBufSize(bs.data()), 16);
  CHECK_EQ(psyq::DecDCTvlc(bs.data(), buf.data(), bs.size() * 4, buf.size(), 16, 16), 0);
  CHECK_EQ(buf[0], bs[0]);  // the header word is the MDEC command
  const u16* codes = reinterpret_cast<const u16*>(&buf[1]);
  for (size_t i = 0; i < kFrame16Codes.size(); ++i) CHECK_EQ(codes[i], kFrame16Codes[i]);
  for (size_t i = kFrame16Codes.size(); i < 32; ++i) CHECK_EQ(codes[i], 0xFE00);  // padded to 16 words
  CHECK_EQ(buf[17], 0xDEADBEEFu);                                                // and no further
}

TEST(libpress_mdec_output_in_slices) {
  const auto bs = bitstream(kFrame16, 16, 1);
  std::vector<u32> rl(64, 0);
  psyq::DecDCTvlc(bs.data(), rl.data(), bs.size() * 4, rl.size(), 16, 16);
  std::vector<u8> expected;
  mdec::decode_image(kFrame16Codes, 16, 16, expected);

  // 24-bit: 768 bytes, taken in two transfers; the callback runs after each.
  psyq::DecDCToutCallback(count_callback);
  g_callbacks = 0;
  psyq::DecDCTin(rl.data(), 1);
  CHECK_EQ(rl[0] >> 27 & 3, 2u);
  std::vector<u32> out(192);
  psyq::DecDCTout(out.data(), 96);
  psyq::DecDCTout(out.data() + 96, 96);
  CHECK_EQ(g_callbacks, 2);
  CHECK(std::equal(expected.begin(), expected.end(), reinterpret_cast<const u8*>(out.data())));

  // 15-bit with the mask bit.
  psyq::DecDCTvlc(bs.data(), rl.data(), bs.size() * 4, rl.size(), 16, 16);
  psyq::DecDCTin(rl.data(), 2);
  CHECK_EQ(rl[0] >> 27 & 3, 3u);
  std::vector<u16> expected15(256);
  mdec::to_rgb15(expected, expected15, true);
  std::vector<u32> out15(128);
  psyq::DecDCTout(out15.data(), 128);
  CHECK(std::equal(expected15.begin(), expected15.end(), reinterpret_cast<const u16*>(out15.data())));
  psyq::DecDCToutCallback(nullptr);
}

TEST(disc1_logo_movie_through_libpress_matches_decoder) {
  Disc* d = open_disc1();
  if (!d) return;
  // What movie_update and its DMA callback do with each frame: 16-pixel columns of
  // 24-bit output loaded into VRAM one after the other.
  disc::str::FrameReader reader(*d->image, d->archive.entry(4586));
  disc::str::Frame frame;
  std::vector<u32> rl(0x14000 / 4), slice(0x2D00 / 4), bs(0x4EC0 / 4);
  auto& gpu = runtime::Runtime::get().gpu;
  int frames = 0, exact = 0;
  while (reader.next(frame)) {
    std::fill(bs.begin(), bs.end(), 0);
    std::memcpy(bs.data(), frame.bitstream.data(), std::min(frame.bitstream.size(), bs.size() * 4));
    psyq::DecDCTvlc(bs.data(), rl.data(), bs.size() * 4, rl.size(), 320, 240);
    psyq::DecDCTin(rl.data(), 1);
    for (int x = 0; x < 480; x += 24) {
      psyq::DecDCTout(slice.data(), 0x3C0 * 3);
      const psyq::RECT r{static_cast<s16>(x), 0, 24, 240};
      psyq::LoadImage(&r, slice.data());
    }
    gpu.display().x = 0;
    gpu.display().y = 0;
    gpu.display().width = 320;
    gpu.display().height = 240;
    gpu.display().color24 = true;
    const auto expected = mdec::decode_frame(frame.bitstream, frame.width, frame.height);
    exact += expected && *expected == displayed_rgb();
    ++frames;
  }
  CHECK_EQ(frames, 48);
  CHECK_EQ(exact, 48);
}

TEST(disc1_logo_movie_plays_like_the_console) {
  Disc* d = open_disc1();
  if (!d) return;
  auto& rt = runtime::Runtime::get();
  game::System* sys = game::get_system();
  // title_intro_sequence's loop for the studio movie.
  game::Movie* movie = game::movie_create(new game::Movie, 0, 0, 0x140, 0xF0, 1, 3);
  game::movie_open(movie, 0x11EA);
  const u64 opened = rt.vblanks();
  psyq::SetDispMask(0);
  std::vector<u64> flips;
  int last_y = rt.gpu.display().y, presents = 0;
  for (;;) {
    game::pad_pressed(&sys->pad, 0);
    game::pad_clear(&sys->pad, 0);
    if (game::movie_update(movie) != 1) break;
    const s32 before = game::movie_frame_count();
    game::movie_present(movie);
    if (rt.gpu.display().y != last_y || (presents == 0 && rt.display_enabled)) {
      flips.push_back(rt.vblanks());
      last_y = rt.gpu.display().y;
      ++presents;
    }
    CHECK(game::movie_frame_count() >= before);
  }
  // The entry's 480 sectors less the 30 never read: 45 frames, about four fields apart
  // (ten sectors at 150 a second), the first after the seek and the first frame's decode.
  CHECK_EQ(flips.size(), 45u);
  CHECK_EQ(game::movie_frame_count(), 45);
  if (flips.size() == 45) {
    CHECK(flips[0] - opened >= 20 && flips[0] - opened <= 26);
    for (size_t i = 1; i < flips.size(); ++i) CHECK(flips[i] - flips[i - 1] >= 3 && flips[i] - flips[i - 1] <= 5);
    CHECK(flips[44] - flips[0] >= 44 * 4 - 2 && flips[44] - flips[0] <= 44 * 4 + 2);
  }
  // The last frame is on screen, in 24-bit, exactly as decoded.
  CHECK(rt.display_enabled);
  CHECK(rt.gpu.display().color24 && rt.gpu.display().width == 320 && rt.gpu.display().height == 240);
  CHECK(displayed_rgb() == decoded_frame(*d, 4586, 44));
  delete movie;
}

TEST(disc1_prologue_cue_lists_run) {
  Disc* d = open_disc1();
  if (!d) return;
  auto& rt = runtime::Runtime::get();
  game::System* sys = game::get_system();
  auto load = [&](int entry) {
    std::vector<u8> data(static_cast<size_t>(sys->loader.entry_size(entry)));
    sys->loader.load_sync(data.data(), entry, 0);
    return data;
  };
  auto play = [&](game::Prologue* p, int& presents) {
    int r;
    presents = 0;
    while ((r = game::prologue_update(p, 1)) != 1 && r != 2) {
      if (p->has_movie) {
        const int before = rt.gpu.display().y;
        game::movie_present(p);
        presents += rt.gpu.display().y != before;
      }
    }
    return r;
  };

  game::Prologue* p = game::prologue_create(new game::Prologue);
  std::vector<u8> sound = load(2681);  // g_prologue_data on Rena's path
  game::prologue_load_sound(p, sound);
  CHECK(sound.empty());
  CHECK_EQ(p->sequence.size(), 0x8000u);

  // #4504: a narration clip and the end at 1220 vblanks; no movie.
  std::vector<u8> script = load(4504);
  game::prologue_set_script(p, script.data());
  game::pad_clear(&sys->pad, 0);
  int presents = 0;
  u64 start = rt.vblanks();
  CHECK_EQ(play(p, presents), 1);
  CHECK(rt.vblanks() - start >= 1220 && rt.vblanks() - start <= 1224);
  CHECK_EQ(p->has_movie, 0);

  // #4507 on the same object: movie #4568 (906 frames) with music, clips and
  // vibration; the end cue comes when the last frame has been decoded.
  game::prologue_restart(p);
  script = load(4507);
  game::prologue_set_script(p, script.data());
  game::pad_clear(&sys->pad, 0);
  start = rt.vblanks();
  CHECK_EQ(play(p, presents), 1);
  CHECK_EQ(p->has_movie, 1);
  CHECK_EQ(game::movie_frame_count(), 906);
  CHECK(presents >= 900 && presents <= 906);
  CHECK(rt.vblanks() - start >= 906 * 4 - 20 && rt.vblanks() - start <= 906 * 4 + 40);
  delete p;

  // Start skips (2), unless L1 is held with it.
  p = game::prologue_create(new game::Prologue);
  game::prologue_set_script(p, script.data());
  rt.scripted_input[rt.vblanks() + 60] = 0x0804;  // L1 + Start: ignored
  rt.scripted_input[rt.vblanks() + 120] = 0;
  rt.scripted_input[rt.vblanks() + 200] = 0x0800;  // Start
  start = rt.vblanks();
  CHECK_EQ(play(p, presents), 2);
  CHECK(rt.vblanks() - start >= 200 && rt.vblanks() - start <= 206);
  rt.scripted_input.clear();
  delete p;
}
