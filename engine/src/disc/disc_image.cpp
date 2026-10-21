#include "disc/disc_image.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>

namespace ss::disc {
namespace {

constexpr std::array<u8, 12> kSync = {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00};

std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
  return s;
}

// The first FILE entry of a cue sheet, resolved next to the sheet.
std::filesystem::path bin_from_cue(const std::filesystem::path& cue, std::string* error) {
  std::ifstream in(cue);
  std::string line;
  while (std::getline(in, line)) {
    auto pos = line.find("FILE");
    if (pos == std::string::npos) continue;
    auto open = line.find('"', pos);
    auto close = open == std::string::npos ? open : line.find('"', open + 1);
    if (close == std::string::npos) break;
    return cue.parent_path() / line.substr(open + 1, close - open - 1);
  }
  if (error) *error = "cue sheet has no FILE line: " + cue.string();
  return {};
}

}  // namespace

std::unique_ptr<DiscImage> DiscImage::open(const std::filesystem::path& path, std::string* error) {
  std::filesystem::path image = path;
  if (lower(path.extension().string()) == ".cue") {
    image = bin_from_cue(path, error);
    if (image.empty()) return nullptr;
  }

  std::unique_ptr<DiscImage> disc(new DiscImage());
  disc->path_ = image;
  disc->file_.open(image, std::ios::binary);
  if (!disc->file_) {
    if (error) *error = "cannot open " + image.string();
    return nullptr;
  }
  std::error_code ec;
  const auto size = std::filesystem::file_size(image, ec);
  if (ec) {
    if (error) *error = "cannot size " + image.string();
    return nullptr;
  }

  std::array<u8, 16> head{};
  disc->file_.read(reinterpret_cast<char*>(head.data()), head.size());
  if (size % 2352 == 0 && std::memcmp(head.data(), kSync.data(), kSync.size()) == 0) {
    disc->sector_size_ = 2352;
    // Mode 2 Form 1 user data follows the 4-byte header and 8-byte subheader; mode 1 has no subheader.
    disc->data_offset_ = head[15] == 2 ? 24 : 16;
  } else if (size % 2048 == 0) {
    disc->sector_size_ = 2048;
    disc->data_offset_ = 0;
  } else {
    if (error) *error = "unrecognised CD image layout: " + image.string();
    return nullptr;
  }
  disc->sector_count_ = static_cast<u32>(size / disc->sector_size_);
  return disc;
}

bool DiscImage::read_sector(u32 lba, std::span<u8, kSectorData> out) {
  return read_sectors(lba, 1, out.data());
}

bool DiscImage::read_sectors(u32 lba, u32 count, u8* out) {
  if (lba >= sector_count_ || count > sector_count_ - lba) return false;
  for (u32 i = 0; i < count; ++i) {
    file_.clear();
    file_.seekg(static_cast<std::streamoff>(lba + i) * sector_size_ + data_offset_);
    file_.read(reinterpret_cast<char*>(out + i * kSectorData), kSectorData);
    if (!file_) return false;
  }
  return true;
}

}  // namespace ss::disc
