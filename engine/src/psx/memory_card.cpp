#include "psx/memory_card.h"

#include <cstring>
#include <fstream>

namespace ss::psx {
namespace {

u32 rd32(const u8* p) {
  u32 v;
  std::memcpy(&v, p, 4);
  return v;
}
void wr32(u8* p, u32 v) { std::memcpy(p, &v, 4); }
u16 rd16(const u8* p) {
  u16 v;
  std::memcpy(&v, p, 2);
  return v;
}
void wr16(u8* p, u16 v) { std::memcpy(p, &v, 2); }

}  // namespace

bool MemoryCard::open(const std::filesystem::path& path, bool create) {
  std::ifstream in(path, std::ios::binary);
  if (in) {
    std::vector<u8> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    // Some emulators prepend a header (DexDrive: 3904 bytes); the raw image is the tail.
    if (bytes.size() < kSize) return false;
    std::memcpy(data_.data(), bytes.data() + (bytes.size() - kSize), kSize);
    path_ = path;
    return true;
  }
  if (!create) return false;
  path_ = path;
  std::filesystem::create_directories(path.parent_path());
  format();
  return true;
}

bool MemoryCard::formatted() const { return frame(0)[0] == 'M' && frame(0)[1] == 'C'; }

void MemoryCard::set_checksum(int n) {
  u8* f = frame(n);
  u8 x = 0;
  for (int i = 0; i < kFrameSize - 1; ++i) x ^= f[i];
  f[kFrameSize - 1] = x;
}

void MemoryCard::format() {
  std::fill(data_.begin(), data_.begin() + kBlockSize, 0);
  frame(0)[0] = 'M';
  frame(0)[1] = 'C';
  set_checksum(0);
  for (int b = 1; b < kBlocks; ++b) {
    u8* f = frame(b);
    wr32(f, kFree);
    wr32(f + 4, 0);
    wr16(f + 8, 0xFFFF);
    set_checksum(b);
  }
  for (int n = 16; n < 36; ++n) {  // broken-frame list: none
    u8* f = frame(n);
    wr32(f, 0xFFFFFFFF);
    wr16(f + 8, 0xFFFF);
    set_checksum(n);
  }
  std::memcpy(frame(63), frame(0), kFrameSize);  // write-test frame
  save();
}

u32 MemoryCard::state(int block) const { return rd32(frame(block)); }

std::vector<MemoryCard::File> MemoryCard::files() const {
  std::vector<File> out;
  if (!formatted()) return out;
  for (int b = 1; b < kBlocks; ++b) {
    if (state(b) != kFirst) continue;
    const u8* f = frame(b);
    File file;
    file.name.assign(reinterpret_cast<const char*>(f + 0x0A), strnlen(reinterpret_cast<const char*>(f + 0x0A), 21));
    file.first_block = b;
    file.size = rd32(f + 4);
    out.push_back(std::move(file));
  }
  return out;
}

std::optional<MemoryCard::File> MemoryCard::find(const std::string& name) const {
  for (auto& f : files())
    if (f.name == name) return f;
  return std::nullopt;
}

int MemoryCard::free_blocks() const {
  int n = 0;
  for (int b = 1; b < kBlocks; ++b) {
    const u32 s = state(b);
    if (s == kFree || s == kDeletedFirst || s == kDeletedMiddle || s == kDeletedLast) ++n;
  }
  return n;
}

bool MemoryCard::create(const std::string& name, int blocks) {
  if (!formatted() || blocks < 1 || find(name) || free_blocks() < blocks) return false;
  std::vector<int> chain;
  for (int b = 1; b < kBlocks && static_cast<int>(chain.size()) < blocks; ++b) {
    const u32 s = state(b);
    if (s == kFree || s == kDeletedFirst || s == kDeletedMiddle || s == kDeletedLast) chain.push_back(b);
  }
  for (size_t i = 0; i < chain.size(); ++i) {
    u8* f = frame(chain[i]);
    std::memset(f, 0, kFrameSize);
    const bool first = i == 0, last = i + 1 == chain.size();
    wr32(f, first ? (last && chain.size() == 1 ? kFirst : kFirst) : (last ? kLast : kMiddle));
    if (first) {
      wr32(f + 4, static_cast<u32>(blocks) * kBlockSize);
      std::memcpy(f + 0x0A, name.c_str(), std::min<size_t>(name.size(), 20));
    }
    wr16(f + 8, last ? 0xFFFF : static_cast<u16>(chain[i + 1] - 1));
    set_checksum(chain[i]);
    std::memset(data_.data() + static_cast<size_t>(chain[i]) * kBlockSize, 0, kBlockSize);
  }
  save();
  return true;
}

bool MemoryCard::erase(const std::string& name) {
  auto f = find(name);
  if (!f) return false;
  for (int b = f->first_block; b >= 1 && b < kBlocks;) {
    u8* fr = frame(b);
    const u32 s = rd32(fr);
    wr32(fr, s == kFirst ? kDeletedFirst : s == kLast ? kDeletedLast : kDeletedMiddle);
    set_checksum(b);
    const u16 next = rd16(fr + 8);
    b = next == 0xFFFF ? -1 : next + 1;
  }
  save();
  return true;
}

int MemoryCard::block_of(const File& f, u32 offset) const {
  int b = f.first_block;
  for (u32 skip = offset / kBlockSize; skip > 0; --skip) {
    const u16 next = rd16(frame(b) + 8);
    if (next == 0xFFFF) return -1;
    b = next + 1;
  }
  return b;
}

bool MemoryCard::read(const File& f, u32 offset, u8* dest, u32 length) const {
  while (length > 0) {
    const int b = block_of(f, offset);
    if (b < 1) return false;
    const u32 in_block = offset % kBlockSize, n = std::min(length, kBlockSize - in_block);
    std::memcpy(dest, data_.data() + static_cast<size_t>(b) * kBlockSize + in_block, n);
    dest += n;
    offset += n;
    length -= n;
  }
  return true;
}

bool MemoryCard::write(const File& f, u32 offset, const u8* src, u32 length) {
  while (length > 0) {
    const int b = block_of(f, offset);
    if (b < 1) return false;
    const u32 in_block = offset % kBlockSize, n = std::min(length, kBlockSize - in_block);
    std::memcpy(data_.data() + static_cast<size_t>(b) * kBlockSize + in_block, src, n);
    src += n;
    offset += n;
    length -= n;
  }
  save();
  return true;
}

bool MemoryCard::read_frame(int n, u8* dest) const {
  if (n < 0 || n >= static_cast<int>(kSize / kFrameSize)) return false;
  std::memcpy(dest, frame(n), kFrameSize);
  return true;
}

bool MemoryCard::write_frame(int n, const u8* src) {
  if (n < 0 || n >= static_cast<int>(kSize / kFrameSize)) return false;
  std::memcpy(frame(n), src, kFrameSize);
  save();
  return true;
}

void MemoryCard::save() const {
  if (path_.empty()) return;
  // The temporary file as a path, not as path::string(): on Windows that string is UTF-8, but a
  // narrow file name is read in the ANSI code page, so a card in a folder with a non-ASCII name
  // (the user's profile folder, a seed's slot name) was never written [release agent, 0.02.50].
  std::filesystem::path tmp = path_;
  tmp += ".tmp";
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(data_.data()), static_cast<std::streamsize>(data_.size()));
  }
  std::error_code ec;
  std::filesystem::rename(tmp, path_, ec);
}

}  // namespace ss::psx
