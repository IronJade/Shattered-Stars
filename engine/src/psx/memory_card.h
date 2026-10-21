#pragma once

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "core/types.h"

// A PlayStation memory card as a raw 128 KB image (the .mcd / .mcr format emulators
// use): 16 blocks of 8 KB, each 64 frames of 128 bytes. Block 0 holds the header
// frame ("MC"), fifteen directory frames (one per data block) and the broken-frame
// list; blocks 1-15 hold file data. A file occupies a chain of blocks linked through
// the directory frames. Layout per psx-spx "Memory Card Data Format".
namespace ss::psx {

class MemoryCard {
 public:
  static constexpr int kFrameSize = 0x80;
  static constexpr int kBlockSize = 0x2000;
  static constexpr int kBlocks = 16;
  static constexpr size_t kSize = static_cast<size_t>(kBlockSize) * kBlocks;

  // Directory frame states (u32 at +0).
  enum : u32 {
    kFirst = 0x51, kMiddle = 0x52, kLast = 0x53,  // block in use
    kFree = 0xA0, kDeletedFirst = 0xA1, kDeletedMiddle = 0xA2, kDeletedLast = 0xA3,
  };

  struct File {
    std::string name;
    int first_block = 0;  // 1..15
    u32 size = 0;         // bytes, a multiple of 8 KB
  };

  // Opens (or, with create, makes a freshly formatted) card image at 'path'.
  bool open(const std::filesystem::path& path, bool create);
  bool is_open() const { return !path_.empty(); }
  bool formatted() const;
  void format();  // empty directory, header and write-test frames; saved at once

  std::vector<File> files() const;
  std::optional<File> find(const std::string& name) const;
  int free_blocks() const;
  // Allocates 'blocks' free blocks for a new file; false when it exists or there is no room.
  bool create(const std::string& name, int blocks);
  bool erase(const std::string& name);

  // Byte access inside a file (offset and length in bytes, frame aligned by the caller).
  bool read(const File& f, u32 offset, u8* dest, u32 length) const;
  bool write(const File& f, u32 offset, const u8* src, u32 length);

  // Raw frame access (the BIOS's _card_read / _card_write).
  bool read_frame(int frame, u8* dest) const;
  bool write_frame(int frame, const u8* src);

  const std::filesystem::path& path() const { return path_; }

 private:
  u8* frame(int n) { return data_.data() + static_cast<size_t>(n) * kFrameSize; }
  const u8* frame(int n) const { return data_.data() + static_cast<size_t>(n) * kFrameSize; }
  u32 state(int block) const;               // directory state of data block 1..15
  void set_checksum(int frame_number);
  int block_of(const File& f, u32 offset) const;  // data block holding 'offset', or -1
  void save() const;

  std::filesystem::path path_;
  std::vector<u8> data_ = std::vector<u8>(kSize, 0);
};

}  // namespace ss::psx
