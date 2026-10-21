#pragma once

#include <optional>
#include <string>
#include <vector>

#include "core/types.h"
#include "disc/disc_image.h"

namespace ss::disc {

struct ArchiveEntry {
  u32 lba = 0;      // absolute disc sector
  u32 sectors = 0;  // length in 2048-byte sectors
  bool present() const { return sectors != 0; }
};

// The game's data archive (SO2.BIN on disc 1, an unlisted file on disc 2), read
// the way loader_read_toc / loader_select_entry do. See docs/catalog/formats.md.
class Archive {
 public:
  static constexpr u32 kArchiveLba = 300;
  static constexpr u32 kTocEntries = 0x1200;
  static constexpr u32 kTocKey = 0x13578642;

  bool open(DiscImage& disc, std::string* error);

  // Decrypts the table exactly as loader_read_toc does; exposed for tests.
  static std::vector<ArchiveEntry> decrypt_toc(const u8* raw);

  size_t entry_count() const { return entries_.size(); }
  // Entry 0's decrypted word; 0x34829314 on both discs (checked by loader_is_disc).
  u32 toc_word0() const { return toc_word0_; }
  const ArchiveEntry& entry(u32 index) const { return entries_.at(index); }
  bool has_entry(u32 index) const { return index < entries_.size() && entries_[index].present(); }
  // Port of loader_entry_size: size in bytes.
  u32 entry_size(u32 index) const { return has_entry(index) ? entries_[index].sectors * 2048 : 0; }
  std::optional<std::vector<u8>> read_entry(u32 index);
  // Reads 'bytes' starting at an entry's first sector, continuing into the entries
  // after it on disc, as loader_start_read does when given a byte count.
  bool read_from_entry(u32 index, size_t bytes, u8* dest);

 private:
  DiscImage* disc_ = nullptr;
  std::vector<ArchiveEntry> entries_;
  u32 toc_word0_ = 0;
};

}  // namespace ss::disc
