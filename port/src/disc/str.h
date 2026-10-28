#pragma once

#include <optional>
#include <span>
#include <string>
#include <vector>

#include "core/types.h"
#include "disc/archive.h"
#include "disc/disc_image.h"

// STR movie streams: each video frame is split over consecutive Mode 2 Form 1
// sectors ("chunks"), each carrying a 32-byte header and 2016 bytes of the frame's
// MDEC bitstream. All of Star Ocean 2's movies use 10 chunks per frame, 320x240,
// bitstream version 2, and no interleaved audio. Reference: psx-spx "CDROM File
// Video STR Streaming and BS Picture Compression".
namespace ss::disc::str {

constexpr u32 kHeaderBytes = 32;
constexpr u32 kPayloadBytes = DiscImage::kSectorData - kHeaderBytes;  // 2016

// A video sector's header (little-endian):
//   0x00 u16 0x0160   0x02 u16 0x8001 (video)   0x04 u16 chunk   0x06 u16 chunk count
//   0x08 u32 frame number, from 1   0x0C u32 bitstream bytes in the whole frame
//   0x10 u16 width   0x12 u16 height
//   0x14 8 bytes: copy of the bitstream's own header   0x1C u32 0
struct SectorHeader {
  u16 chunk = 0;
  u16 chunk_count = 0;
  u32 frame = 0;
  u32 frame_bytes = 0;
  u16 width = 0;
  u16 height = 0;
};

// Nullopt for anything that is not an STR video sector (audio, data, padding).
std::optional<SectorHeader> parse_sector_header(std::span<const u8> sector);

struct Frame {
  u32 number = 0;  // header frame number (jPSXdec's frame index + 1)
  u32 lba = 0;     // first sector of the frame
  u16 width = 0;
  u16 height = 0;
  std::vector<u8> bitstream;  // the chunks' payloads joined, cut to the header's byte count
};

// Reassembles frames from a sector range in disc order. A frame whose chunks are
// not all present before the next frame starts is dropped (counted), as the
// game's player skips a frame it cannot assemble.
class FrameReader {
 public:
  FrameReader(DiscImage& disc, u32 lba, u32 sectors);
  FrameReader(DiscImage& disc, const ArchiveEntry& entry) : FrameReader(disc, entry.lba, entry.sectors) {}

  // The next complete frame; false at the end of the range or on a read error.
  bool next(Frame& frame);
  void rewind();

  u32 next_lba() const { return next_lba_; }
  u32 dropped_frames() const { return dropped_; }
  const std::string& error() const { return error_; }  // set when a sector read failed

 private:
  void start(const SectorHeader& h, u32 lba);

  DiscImage& disc_;
  u32 first_lba_;
  u32 end_lba_;
  u32 next_lba_;
  u32 dropped_ = 0;
  std::string error_;

  // Frame being assembled.
  bool open_ = false;
  SectorHeader current_;
  u32 current_lba_ = 0;
  std::vector<u8> chunks_;
  std::vector<bool> have_;
  u32 received_ = 0;
};

}  // namespace ss::disc::str
