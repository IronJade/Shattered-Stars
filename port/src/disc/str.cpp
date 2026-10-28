#include "disc/str.h"

#include <array>
#include <cstring>

namespace ss::disc::str {
namespace {

constexpr u16 kMagic = 0x0160;
constexpr u16 kVideo = 0x8001;
constexpr u16 kMaxChunks = 256;  // real frames use a few dozen at most; bounds the buffer

}  // namespace

std::optional<SectorHeader> parse_sector_header(std::span<const u8> sector) {
  if (sector.size() < kHeaderBytes || read_u16(&sector[0]) != kMagic || read_u16(&sector[2]) != kVideo)
    return std::nullopt;
  SectorHeader h;
  h.chunk = read_u16(&sector[4]);
  h.chunk_count = read_u16(&sector[6]);
  h.frame = read_u32(&sector[8]);
  h.frame_bytes = read_u32(&sector[12]);
  h.width = read_u16(&sector[16]);
  h.height = read_u16(&sector[18]);
  if (h.chunk_count == 0 || h.chunk_count > kMaxChunks || h.chunk >= h.chunk_count ||
      h.frame_bytes > u32{h.chunk_count} * kPayloadBytes)
    return std::nullopt;
  return h;
}

FrameReader::FrameReader(DiscImage& disc, u32 lba, u32 sectors)
    : disc_(disc), first_lba_(lba), end_lba_(lba + sectors), next_lba_(lba) {}

void FrameReader::rewind() {
  next_lba_ = first_lba_;
  dropped_ = 0;
  error_.clear();
  open_ = false;
}

void FrameReader::start(const SectorHeader& h, u32 lba) {
  if (open_) ++dropped_;
  open_ = true;
  current_ = h;
  current_lba_ = lba;
  chunks_.assign(size_t{h.chunk_count} * kPayloadBytes, 0);
  have_.assign(h.chunk_count, false);
  received_ = 0;
}

bool FrameReader::next(Frame& frame) {
  std::array<u8, DiscImage::kSectorData> sector;
  while (next_lba_ < end_lba_) {
    const u32 lba = next_lba_++;
    if (!disc_.read_sector(lba, sector)) {
      error_ = "cannot read sector " + std::to_string(lba);
      next_lba_ = end_lba_;
      open_ = false;
      return false;
    }
    const auto h = parse_sector_header(sector);
    if (!h) continue;
    // A new frame number, a different layout or a chunk seen twice starts a new frame.
    if (!open_ || h->frame != current_.frame || h->chunk_count != current_.chunk_count ||
        h->frame_bytes != current_.frame_bytes || have_[h->chunk])
      start(*h, lba);
    have_[h->chunk] = true;
    ++received_;
    std::memcpy(&chunks_[size_t{h->chunk} * kPayloadBytes], sector.data() + kHeaderBytes, kPayloadBytes);
    if (received_ < current_.chunk_count) continue;

    open_ = false;
    frame.number = current_.frame;
    frame.lba = current_lba_;
    frame.width = current_.width;
    frame.height = current_.height;
    const size_t bytes = current_.frame_bytes ? current_.frame_bytes : chunks_.size();
    frame.bitstream.assign(chunks_.begin(), chunks_.begin() + static_cast<std::ptrdiff_t>(bytes));
    return true;
  }
  if (open_) {
    ++dropped_;
    open_ = false;
  }
  return false;
}

}  // namespace ss::disc::str
