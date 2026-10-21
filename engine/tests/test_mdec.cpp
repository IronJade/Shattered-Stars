// MDEC decoder and STR reader tests. The unit tests use hand-built bitstreams; the
// disc tests decode real movie frames and compare them with hashes of jPSXdec's
// "psx" quality output (analysis/reference/mdec), skipping when SS_DISC1 is unset.

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "disc/archive.h"
#include "disc/str.h"
#include "psx/mdec.h"
#include "test.h"

using namespace ss;
namespace mdec = ss::psx::mdec;

namespace {

// Builds an STR bitstream: 16-bit little-endian words filled most significant bit first.
struct BitWriter {
  std::vector<u16> words;
  int used = 16;

  void bit(bool b) {
    if (used == 16) {
      words.push_back(0);
      used = 0;
    }
    if (b) words.back() |= static_cast<u16>(1 << (15 - used));
    ++used;
  }
  BitWriter& put(const char* bits) {
    for (; *bits; ++bits) bit(*bits == '1');
    return *this;
  }
  std::vector<u8> frame(u16 qscale, u16 version) const {
    std::vector<u8> out = {0, 0, 0x00, 0x38, static_cast<u8>(qscale), 0, static_cast<u8>(version), 0};
    for (u16 w : words) {
      out.push_back(static_cast<u8>(w));
      out.push_back(static_cast<u8>(w >> 8));
    }
    return out;
  }
};

// Codes for a 16x16 frame whose six blocks hold only the given DC values.
std::vector<u16> dc_only(int qscale, std::initializer_list<int> dcs) {
  std::vector<u16> codes;
  for (int dc : dcs) {
    codes.push_back(static_cast<u16>(qscale << 10 | (dc & 0x3FF)));
    codes.push_back(mdec::kEndOfBlock);
  }
  return codes;
}

u64 fnv1a(const u8* p, size_t n, u64 h = 0xcbf29ce484222325ull) {
  for (size_t i = 0; i < n; ++i) h = (h ^ p[i]) * 0x100000001b3ull;
  return h;
}

// Movie entries in disc 1's archive.
constexpr u32 kLogoMovie = 4586;     // 48 frames
constexpr u32 kOpeningMovie = 4563;  // 2603 frames

struct Movie {
  std::unique_ptr<disc::DiscImage> disc;
  disc::ArchiveEntry entry;
};

std::optional<Movie> open_movie(u32 index) {
  const char* path = std::getenv("SS_DISC1");
  if (!path || !*path) {
    std::printf("  (skipped: SS_DISC1 not set)\n");
    return std::nullopt;
  }
  std::string error;
  Movie m;
  m.disc = disc::DiscImage::open(path, &error);
  disc::Archive archive;
  if (!m.disc || !archive.open(*m.disc, &error) || !archive.has_entry(index)) {
    std::fprintf(stderr, "  cannot open movie %u in %s: %s\n", index, path, error.c_str());
    ++test::failures;
    return std::nullopt;
  }
  m.entry = archive.entry(index);
  return m;
}

}  // namespace

TEST(mdec_vlc_decodes_v2_block) {
  BitWriter w;
  w.put("0000000101");                         // Cr DC +5
  w.put("110");                                // run 0, +1
  w.put("0111");                               // run 1, -1
  w.put("000001").put("000011").put("1111111110");  // escape: run 3, -2
  w.put("0000000000011111").put("0");          // longest code: run 27, +1
  w.put("10");                                 // end of block
  for (int i = 0; i < 5; ++i) w.put("1111111111").put("10");  // DC -1, end of block
  std::vector<u16> codes;
  std::string error;
  CHECK(mdec::decode_codes(w.frame(1, 2), 16, 16, codes, &error));
  const std::vector<u16> expected = {0x0405, 0x0001, 0x07FF, 0x0FFE, 0x6C01, 0xFE00, 0x07FF, 0xFE00, 0x07FF,
                                     0xFE00, 0x07FF, 0xFE00, 0x07FF, 0xFE00, 0x07FF, 0xFE00};
  CHECK(codes == expected);
}

TEST(mdec_vlc_decodes_v3_dc) {
  // Version 3 codes each DC as a difference from the previous block of its kind.
  BitWriter w;
  w.put("01").put("1").put("10");            // Cr: size 1, +1 -> 4
  w.put("110").put("010").put("10");         // Cb: size 3, 2-7 = -5 -> -20
  w.put("100").put("10");                    // Y: size 0 -> 0
  w.put("01").put("11").put("10");           // Y: size 2, +3 -> 12
  w.put("00").put("0").put("10");            // Y: size 1, -1 -> 8
  w.put("111110").put("1000000").put("10");  // Y: size 7, +64 -> 264
  std::vector<u16> codes;
  std::string error;
  CHECK(mdec::decode_codes(w.frame(2, 3), 16, 16, codes, &error));
  const auto expected = dc_only(2, {4, -20, 0, 12, 8, 264});
  CHECK(codes == expected);
}

TEST(mdec_dc_only_block_is_flat) {
  // DC 100 dequantises to 200, and the IDCT spreads it as 200/8 = 25 over the block.
  std::vector<u8> rgb;
  CHECK(mdec::decode_image(dc_only(1, {0, 0, 100, 100, 100, 100}), 16, 16, rgb));
  CHECK_EQ(rgb.size(), 16u * 16u * 3u);
  bool flat = true;
  for (u8 c : rgb) flat = flat && c == 128 + 25;
  CHECK(flat);
}

TEST(mdec_colour_conversion_matches_psx_model) {
  // Cr = 10, Cb = -10, Y = 0: R = 128 + round(1.402 * 10), G = 128 + round(-0.3437 * -10
  // - 0.7143 * 10), B = 128 + round(1.772 * -10), each in 16.16 fixed point rounded half up.
  std::vector<u8> rgb;
  CHECK(mdec::decode_image(dc_only(1, {40, -40, 0, 0, 0, 0}), 16, 16, rgb));
  CHECK_EQ(rgb[0], 142);
  CHECK_EQ(rgb[1], 124);
  CHECK_EQ(rgb[2], 110);
  CHECK_EQ(rgb[rgb.size() - 3], 142);
}

TEST(mdec_odd_sizes_crop_macroblocks) {
  BitWriter w;
  for (int i = 0; i < 12; ++i) w.put("0000000000").put("10");  // two macroblocks of zero blocks
  std::vector<u8> rgb;
  CHECK(mdec::decode_frame(w.frame(1, 2), 20, 10, rgb));
  CHECK_EQ(rgb.size(), 20u * 10u * 3u);
}

TEST(mdec_truncated_stream_fails_with_grey_image) {
  BitWriter w;
  for (int i = 0; i < 6; ++i) w.put("0000000101").put("10");
  auto bs = w.frame(1, 2);
  bs.resize(9);
  std::vector<u8> rgb;
  std::string error;
  CHECK(!mdec::decode_frame(bs, 16, 16, rgb, &error));
  CHECK(!error.empty());
  CHECK_EQ(rgb.size(), 16u * 16u * 3u);
  bool grey = true;
  for (u8 c : rgb) grey = grey && c == 128;
  CHECK(grey);

  std::vector<u16> codes;
  CHECK(!mdec::decode_codes(std::vector<u8>{1, 2, 3, 4, 5, 6, 7, 8}, 16, 16, codes, &error));  // no 0x3800
}

TEST(mdec_rgb15_rounds_and_saturates) {
  const std::vector<u8> rgb = {0, 4, 255, 3, 252, 8};
  std::vector<u16> out(2);
  mdec::to_rgb15(rgb, out);
  CHECK_EQ(out[0], 0 | 1 << 5 | 31 << 10);
  CHECK_EQ(out[1], 0 | 31 << 5 | 1 << 10);
  mdec::to_rgb15(rgb, out, true);
  CHECK_EQ(out[0], 0x8000 | 1 << 5 | 31 << 10);
}

TEST(str_frame_reader_assembles_frames) {
  // A synthetic 2048-byte-sector image: frame 1 in two chunks with a data sector
  // between them, frame 2 missing its second chunk, frame 3 complete.
  auto sector = [](u16 chunk, u16 count, u32 frame, u32 bytes, u8 fill) {
    std::vector<u8> s(disc::DiscImage::kSectorData, fill);
    const u8 header[20] = {0x60, 0x01, 0x01, 0x80, static_cast<u8>(chunk), 0, static_cast<u8>(count), 0,
                           static_cast<u8>(frame), 0, 0, 0, static_cast<u8>(bytes), static_cast<u8>(bytes >> 8),
                           0, 0, 0x40, 0x01, 0xF0, 0x00};
    std::copy(std::begin(header), std::end(header), s.begin());
    return s;
  };
  std::vector<std::vector<u8>> sectors = {
      sector(0, 2, 1, 3000, 0xA1),
      std::vector<u8>(disc::DiscImage::kSectorData, 0x00),
      sector(1, 2, 1, 3000, 0xB1),
      sector(0, 2, 2, 2500, 0xA2),
      sector(0, 2, 3, 2100, 0xA3),
      sector(1, 2, 3, 2100, 0xB3),
  };
  const auto path = std::filesystem::temp_directory_path() / "ss_test_str.iso";
  {
    std::ofstream out(path, std::ios::binary);
    for (const auto& s : sectors) out.write(reinterpret_cast<const char*>(s.data()), static_cast<std::streamsize>(s.size()));
  }
  std::string error;
  auto image = disc::DiscImage::open(path, &error);
  CHECK(image != nullptr);
  if (image) {
    disc::str::FrameReader reader(*image, 0, static_cast<u32>(sectors.size()));
    disc::str::Frame frame;
    CHECK(reader.next(frame));
    CHECK_EQ(frame.number, 1u);
    CHECK_EQ(frame.lba, 0u);
    CHECK_EQ(frame.width, 320);
    CHECK_EQ(frame.height, 240);
    CHECK_EQ(frame.bitstream.size(), 3000u);
    CHECK_EQ(frame.bitstream[0], 0xA1);
    CHECK_EQ(frame.bitstream[disc::str::kPayloadBytes - 1], 0xA1);
    CHECK_EQ(frame.bitstream[disc::str::kPayloadBytes], 0xB1);
    CHECK(reader.next(frame));
    CHECK_EQ(frame.number, 3u);
    CHECK_EQ(frame.lba, 4u);
    CHECK_EQ(frame.bitstream.size(), 2100u);
    CHECK(!reader.next(frame));
    CHECK_EQ(reader.dropped_frames(), 1u);
    CHECK(reader.error().empty());
    reader.rewind();
    CHECK(reader.next(frame) && frame.number == 1u);
  }
  image.reset();
  std::filesystem::remove(path);
}

// Expected hashes are FNV-1a over the RGB bytes of jPSXdec 2.1's "-q psx" PNGs
// (analysis/reference/mdec), not over this decoder's own output.

TEST(disc1_logo_movie_matches_jpsxdec) {
  auto movie = open_movie(kLogoMovie);
  if (!movie) return;
  disc::str::FrameReader reader(*movie->disc, movie->entry);
  disc::str::Frame frame;
  std::vector<u8> rgb;
  u32 frames = 0;
  u64 all = 0xcbf29ce484222325ull;
  while (reader.next(frame)) {
    if (frames == 0) CHECK(fnv1a(frame.bitstream.data(), frame.bitstream.size()) == 0xa5e5e0c33d20cbe5ull);
    ++frames;
    CHECK_EQ(frame.number, frames);
    CHECK(frame.width == 320 && frame.height == 240);
    const auto header = mdec::parse_header(frame.bitstream);
    CHECK(header && header->version == 2);
    std::string error;
    CHECK(mdec::decode_frame(frame.bitstream, frame.width, frame.height, rgb, &error));
    all = fnv1a(rgb.data(), rgb.size(), all);
    if (frames == 11) CHECK(fnv1a(rgb.data(), rgb.size()) == 0x25f7c9b39d123c66ull);
  }
  CHECK_EQ(frames, 48u);
  CHECK_EQ(reader.dropped_frames(), 0u);
  CHECK(reader.error().empty());
  CHECK(all == 0x0be241ce31fbe876ull);
}

TEST(disc1_opening_movie_frames_match_jpsxdec) {
  auto movie = open_movie(kOpeningMovie);
  if (!movie) return;
  disc::str::FrameReader reader(*movie->disc, movie->entry);
  disc::str::Frame frame;
  std::vector<u8> rgb;
  int checked = 0;
  for (u32 index = 0; reader.next(frame); ++index) {
    if (index != 1500 && index != 2200) continue;
    std::string error;
    CHECK(mdec::decode_frame(frame.bitstream, frame.width, frame.height, rgb, &error));
    const u64 h = fnv1a(rgb.data(), rgb.size());
    CHECK(h == (index == 1500 ? 0xc02ae8d3df0a2451ull : 0xa41d91e616fed5a8ull));
    if (++checked == 2) break;
  }
  CHECK_EQ(checked, 2);
}
