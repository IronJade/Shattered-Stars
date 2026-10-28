#pragma once

#include <optional>
#include <span>
#include <string>
#include <vector>

#include "core/types.h"

// SLZ compressed blocks: ports of slz_unpack (0x800121a8), slz_lzss (0x800122b4)
// and slz_lzss_rle (0x8001275c). Format in docs/port/catalog/formats.md.
namespace ss::disc::slz {

struct BlockHeader {
  size_t offset = 0;  // where the block starts in the buffer
  u8 method = 0;      // 0 stored, 1 LZSS, 2 LZSS + run-length fills
  u32 packed = 0;
  u32 unpacked = 0;
  u32 next = 0;  // offset from this block to the next one; 0 = last
};

std::optional<BlockHeader> header_at(std::span<const u8> data, size_t offset);

// Follows the +0xC chain from 'offset' to block number 'block', as slz_unpack does.
std::optional<BlockHeader> find_block(std::span<const u8> data, size_t offset, int block);

// Unpacks the block at 'offset' (whose header must be valid) into 'out'.
bool unpack(std::span<const u8> data, size_t offset, std::vector<u8>& out, std::string* error);

// Convenience: block number 'block' of the chain starting at 'offset'.
std::optional<std::vector<u8>> unpack_block(std::span<const u8> data, size_t offset, int block,
                                            std::string* error);

}  // namespace ss::disc::slz
