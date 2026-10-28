#include "disc/archive.h"

#include <algorithm>
#include <array>

namespace ss::disc {
namespace {

int bcd(u32 v) { return static_cast<int>((v >> 4) * 10 + (v & 0xF)); }

}  // namespace

std::vector<ArchiveEntry> Archive::decrypt_toc(const u8* raw) {
  std::vector<ArchiveEntry> entries(kTocEntries);
  const u8* low_bytes = raw + kTocEntries * 4;
  u32 key = kTocKey;
  for (u32 i = 0; i < kTocEntries; ++i) {
    const u32 mixed = key ^ (key << 1);
    const u32 word = read_u32(raw + i * 4) ^ key;
    const u8 low = static_cast<u8>(low_bytes[i] ^ (mixed & 0xFF));
    key = ~mixed ^ kTocKey;
    // Entry 0 is the table's own slot; the game points it at the archive before reading.
    if (i == 0 || word == 0) continue;
    const int lba = (bcd(word & 0xFF) * 60 + bcd(word >> 8 & 0xFF)) * 75 + bcd(word >> 16 & 0xFF) - 150;
    if (lba < 0) continue;
    entries[i].lba = static_cast<u32>(lba);
    entries[i].sectors = (word >> 24) << 8 | low;
  }
  return entries;
}

bool Archive::open(DiscImage& disc, std::string* error) {
  std::vector<u8> raw(12 * DiscImage::kSectorData);
  if (!disc.read_sectors(kArchiveLba, 12, raw.data())) {
    if (error) *error = "cannot read the archive table at LBA 300";
    return false;
  }
  entries_ = decrypt_toc(raw.data());
  toc_word0_ = read_u32(raw.data()) ^ kTocKey;
  disc_ = &disc;
  return true;
}

std::optional<std::vector<u8>> Archive::read_entry(u32 index) {
  if (!disc_ || !has_entry(index)) return std::nullopt;
  const ArchiveEntry& e = entries_[index];
  std::vector<u8> data(static_cast<size_t>(e.sectors) * DiscImage::kSectorData);
  if (!disc_->read_sectors(e.lba, e.sectors, data.data())) return std::nullopt;
  return data;
}

bool Archive::read_from_entry(u32 index, size_t bytes, u8* dest, u32 first_sector) {
  if (!disc_ || !has_entry(index)) return false;
  const u32 lba = entries_[index].lba + first_sector;
  const u32 full = static_cast<u32>(bytes / DiscImage::kSectorData);
  if (full && !disc_->read_sectors(lba, full, dest)) return false;
  if (const size_t rest = bytes % DiscImage::kSectorData) {
    std::array<u8, DiscImage::kSectorData> sector{};
    if (!disc_->read_sector(lba + full, sector)) return false;
    std::copy_n(sector.begin(), rest, dest + static_cast<size_t>(full) * DiscImage::kSectorData);
  }
  return true;
}

}  // namespace ss::disc
