#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

#include "core/types.h"

// A small .zip reader for installing mod packages and for the Archipelago generator's
// output archives (port feature; no zlib in the build). open() reads the whole archive into
// memory (mod packages are small); open_file() reads only its directory and each entry when
// it is read (a multiworld's archive holds every game's files). It extracts entries that are
// stored or deflated, checking each one's CRC-32. Encrypted entries, multi-disk and zip64
// archives are refused.
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
  // Opens 'path' of any size, reading only its directory; read() then reads from the file.
  bool open_file(const std::filesystem::path& path, std::string* error);
  const std::vector<ZipEntry>& entries() const { return entries_; }
  // The entry's bytes (at most 'max_size'); false and a reason on a damaged entry.
  bool read(const ZipEntry& e, std::vector<u8>* out, std::string* error, size_t max_size = 4u << 20) const;

 private:
  struct Directory {
    u32 count = 0, size = 0, offset = 0;
  };
  // The end record in 'tail', the last 'n' bytes of an archive 'total' bytes long.
  static bool find_directory(const u8* tail, size_t n, u64 total, Directory* d, std::string* error);
  bool parse(const u8* data, size_t n, std::string* error);
  bool parse_directory(const u8* cd, size_t n, u32 count, std::string* error);
  std::vector<u8> data_;               // open() / open_memory(): the whole archive
  std::filesystem::path path_;         // open_file(): the archive, read as needed
  u64 file_size_ = 0;
  std::vector<ZipEntry> entries_;
};

// Raw deflate (RFC 1951) into 'out' (cleared first), at most 'max_out' bytes.
bool inflate(const u8* in, size_t in_size, std::vector<u8>* out, size_t max_out, std::string* error);
u32 crc32(const u8* p, size_t n);

}  // namespace ss::mods
