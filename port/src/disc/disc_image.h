#pragma once

#include <filesystem>
#include <fstream>
#include <memory>
#include <span>
#include <string>

#include "core/types.h"

namespace ss::disc {

// A CD image of one data track: raw 2352-byte sectors (.bin, or a .cue naming one)
// or a 2048-byte-sector .iso. Sector reads return the 2048 bytes of user data.
class DiscImage {
 public:
  static constexpr size_t kSectorData = 2048;

  static std::unique_ptr<DiscImage> open(const std::filesystem::path& path, std::string* error);

  bool read_sector(u32 lba, std::span<u8, kSectorData> out);
  bool read_sectors(u32 lba, u32 count, u8* out);
  u32 sector_count() const { return sector_count_; }
  const std::filesystem::path& path() const { return path_; }

 private:
  DiscImage() = default;

  std::filesystem::path path_;
  std::ifstream file_;
  u32 sector_size_ = 0;  // bytes per sector in the file
  u32 data_offset_ = 0;  // offset of user data within a sector
  u32 sector_count_ = 0;
};

}  // namespace ss::disc
