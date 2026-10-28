#pragma once

#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/types.h"

// A small .zip writer (port feature; no zlib in the build): stored (uncompressed) entries
// only, for files that are compressed already (the tracker maps' PNGs, tools/tracker_maps.h).
// Deterministic: every entry carries the same fixed date (1980-01-01 00:00, the earliest a
// .zip can say), no extra fields, no comment, no data descriptors, so the same entries in the
// same order give the same bytes. The caller chooses the order (sorted, for a reproducible
// archive). The file is written under a temporary name ('<path>.part') and renamed to 'path'
// only by finish(), so a run cut short never leaves a partial archive under the real name.
// No zip64: at most 65535 entries and 4 GiB in all (add() refuses beyond that).
// ss::mods::ZipReader (mods/zip.h) reads what this writes.
namespace ss::util {

class ZipWriter {
 public:
  ZipWriter() = default;
  ZipWriter(const ZipWriter&) = delete;
  ZipWriter& operator=(const ZipWriter&) = delete;
  ~ZipWriter();  // an archive not finished is abandoned (its .part removed)

  // Starts '<path>.part' (its folder must exist). False and a reason when it can't be created.
  bool open(const std::filesystem::path& path, std::string* error);
  // Appends a file: 'name' is its path inside the archive ('/' separated, relative, no "..",
  // at most 65535 bytes; non-ASCII names are marked UTF-8).
  bool add(std::string_view name, std::span<const u8> data, std::string* error);
  // Writes the central directory and the end record, closes the file and renames it into
  // place (over an existing file). False and a reason on failure (the .part is removed).
  bool finish(std::string* error);
  // Closes and removes the .part (nothing is left under either name).
  void abandon();

  bool is_open() const { return file_.is_open(); }
  size_t entries() const { return central_.size(); }
  const std::filesystem::path& part_path() const { return part_; }

  // The fixed date of every entry, as MS-DOS date and time fields.
  static constexpr u16 kDosDate = (0 << 9) | (1 << 5) | 1;  // 1980-01-01
  static constexpr u16 kDosTime = 0;                        // 00:00:00

 private:
  struct Central {
    std::string name;
    u32 crc = 0, size = 0, offset = 0;
    bool utf8 = false;
  };
  bool fail(std::string* error, const std::string& why);

  std::filesystem::path path_, part_;
  std::ofstream file_;
  u64 offset_ = 0;
  std::vector<Central> central_;
};

}  // namespace ss::util
