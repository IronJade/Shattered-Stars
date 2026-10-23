#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

#include "core/types.h"

// A small .zip reader for installing mod packages (port feature; no zlib in the build).
// Reads the whole archive into memory (mod packages are small) and extracts entries that
// are stored or deflated, checking each one's CRC-32. Encrypted entries, multi-disk and
// zip64 archives are refused.
namespace ss::mods {

struct ZipEntry {
  std::string name;  // '/' separated (backslashes from some Windows tools are turned into '/')
  u32 method = 0;    // 0 stored, 8 deflated
  u32 crc = 0;
  u32 compressed = 0, size = 0;
  u32 local_offset = 0;
  bool directory = false;
};

class ZipReader {
 public:
  // Opens 'path' (at most 'max_bytes' long); false and a reason when it is not a zip this
  // reader takes.
  bool open(const std::filesystem::path& path, std::string* error, size_t max_bytes = 16u << 20);
  bool open_memory(std::vector<u8> data, std::string* error);
  const std::vector<ZipEntry>& entries() const { return entries_; }
  // The entry's bytes (at most 'max_size'); false and a reason on a damaged entry.
  bool read(const ZipEntry& e, std::vector<u8>* out, std::string* error, size_t max_size = 4u << 20) const;

 private:
  bool parse(std::string* error);
  std::vector<u8> data_;
  std::vector<ZipEntry> entries_;
};

// Raw deflate (RFC 1951) into 'out' (cleared first), at most 'max_out' bytes.
bool inflate(const u8* in, size_t in_size, std::vector<u8>* out, size_t max_out, std::string* error);
u32 crc32(const u8* p, size_t n);

}  // namespace ss::mods
