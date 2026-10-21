// Decodes STR movie frames from a disc image and writes what each stage produced,
// for comparison with jPSXdec:
//   mdec_probe <disc> (--entry N | --lba L --sectors S) [--out DIR] [--first I] [--last I]
//              [--png] [--codes] [--bs]
// Frames are numbered by their index in the stream (from 0), as jPSXdec names them.
// --png writes frame_IIII.png, --codes the MDEC run-length codes (frame_IIII.mdec,
// little-endian u16, jPSXdec's -vf mdec format), --bs the joined bitstream. With no
// output options every frame is still decoded and checked.
//   mdec_probe --file <bitstream> <width> <height> <out_prefix>
// decodes one standalone bitstream (such as jPSXdec's .bs output) to <out_prefix>.mdec
// and <out_prefix>.png.

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "disc/archive.h"
#include "disc/str.h"
#include "psx/mdec.h"
#include "util/png.h"

using namespace ss;

namespace {

bool write_file(const std::filesystem::path& path, const void* data, size_t size) {
  std::ofstream out(path, std::ios::binary);
  out.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
  return static_cast<bool>(out);
}

int usage() {
  std::fprintf(stderr,
               "usage: mdec_probe <disc> (--entry N | --lba L --sectors S) [--out DIR] [--first I] [--last I]"
               " [--png] [--codes] [--bs]\n"
               "       mdec_probe --file <bitstream> <width> <height> <out_prefix>\n");
  return 2;
}

bool write_png(const std::filesystem::path& path, const std::vector<u8>& rgb, int width, int height) {
  std::vector<u32> rgba(rgb.size() / 3);
  for (size_t p = 0; p < rgba.size(); ++p)
    rgba[p] = rgb[p * 3] | rgb[p * 3 + 1] << 8 | rgb[p * 3 + 2] << 16 | 0xFF000000u;
  return util::write_png(path, rgba, width, height);
}

int decode_file(const char* path, int width, int height, const std::string& prefix) {
  std::ifstream in(path, std::ios::binary);
  const std::vector<u8> bs((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  std::vector<u16> codes;
  std::vector<u8> rgb;
  std::string err;
  bool ok = psx::mdec::decode_codes(bs, width, height, codes, &err);
  ok = psx::mdec::decode_image(codes, width, height, rgb, ok ? &err : nullptr) && ok;
  write_file(prefix + ".mdec", codes.data(), codes.size() * 2);
  write_png(prefix + ".png", rgb, width, height);
  std::printf("%s: %zu bytes, %zu codes%s%s\n", path, bs.size(), codes.size(), ok ? "" : ", error: ", err.c_str());
  return ok ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) return usage();
  if (std::string(argv[1]) == "--file") {
    if (argc != 6) return usage();
    return decode_file(argv[2], std::atoi(argv[3]), std::atoi(argv[4]), argv[5]);
  }
  const std::string disc_path = argv[1];
  long entry = -1, lba = -1, sectors = -1, first = 0, last = -1;
  bool png = false, codes_out = false, bs_out = false;
  std::filesystem::path out_dir = ".";
  for (int i = 2; i < argc; ++i) {
    const std::string a = argv[i];
    auto value = [&]() -> long { return i + 1 < argc ? std::strtol(argv[++i], nullptr, 0) : -1; };
    if (a == "--entry") entry = value();
    else if (a == "--lba") lba = value();
    else if (a == "--sectors") sectors = value();
    else if (a == "--first") first = value();
    else if (a == "--last") last = value();
    else if (a == "--out" && i + 1 < argc) out_dir = argv[++i];
    else if (a == "--png") png = true;
    else if (a == "--codes") codes_out = true;
    else if (a == "--bs") bs_out = true;
    else return usage();
  }

  std::string error;
  auto disc = disc::DiscImage::open(disc_path, &error);
  if (!disc) {
    std::fprintf(stderr, "%s\n", error.c_str());
    return 1;
  }
  if (entry >= 0) {
    disc::Archive archive;
    if (!archive.open(*disc, &error) || !archive.has_entry(static_cast<u32>(entry))) {
      std::fprintf(stderr, "no archive entry %ld %s\n", entry, error.c_str());
      return 1;
    }
    lba = archive.entry(static_cast<u32>(entry)).lba;
    sectors = archive.entry(static_cast<u32>(entry)).sectors;
  }
  if (lba < 0 || sectors <= 0) return usage();
  if (png || codes_out || bs_out) std::filesystem::create_directories(out_dir);

  disc::str::FrameReader reader(*disc, static_cast<u32>(lba), static_cast<u32>(sectors));
  disc::str::Frame frame;
  std::vector<u16> codes;
  std::vector<u8> rgb;
  int index = 0, decoded = 0, failed = 0;
  const auto t0 = std::chrono::steady_clock::now();
  for (; reader.next(frame); ++index) {
    if (index < first) continue;
    if (last >= 0 && index > last) break;
    const auto header = psx::mdec::parse_header(frame.bitstream);
    std::string err;
    bool ok = psx::mdec::decode_codes(frame.bitstream, frame.width, frame.height, codes, &err);
    ok = psx::mdec::decode_image(codes, frame.width, frame.height, rgb, ok ? &err : nullptr) && ok;
    ++decoded;
    if (!ok) {
      ++failed;
      std::printf("frame %d (header %u, lba %u): %s\n", index, frame.number, frame.lba, err.c_str());
    }
    char name[32];
    std::snprintf(name, sizeof name, "frame_%04d", index);
    if (png) write_png(out_dir / (std::string(name) + ".png"), rgb, frame.width, frame.height);
    if (codes_out) write_file(out_dir / (std::string(name) + ".mdec"), codes.data(), codes.size() * 2);
    if (bs_out) write_file(out_dir / (std::string(name) + ".bs"), frame.bitstream.data(), frame.bitstream.size());
    if (png || codes_out || bs_out)
      std::printf("frame %d: header %u lba %u %ux%u %zu bytes qscale %u version %u, %zu codes\n", index,
                  frame.number, frame.lba, frame.width, frame.height, frame.bitstream.size(),
                  header ? header->qscale : 0, header ? header->version : 0, codes.size());
  }
  const double ms =
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  std::printf("%d frames decoded, %d failed, %u dropped, %.1f ms%s%s\n", decoded, failed, reader.dropped_frames(),
              ms, reader.error().empty() ? "" : ", read error: ", reader.error().c_str());
  return failed || !reader.error().empty() ? 1 : 0;
}
