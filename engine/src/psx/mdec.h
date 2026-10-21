#pragma once

#include <optional>
#include <span>
#include <string>
#include <vector>

#include "core/types.h"

// Software replacement for the PS1 movie path: the CPU-side bitstream decode that
// libpress's DecDCTvlc performs, then what the MDEC chip does with the result
// (dequantisation, IDCT, YCbCr->RGB). The arithmetic is jPSXdec's "psx" quality
// model, which emulates the chip's integer IDCT and colour conversion; output was
// checked against it frame by frame. Reference: psx-spx "MDEC".
namespace ss::psx::mdec {

// MDEC code that ends every block (run 63, level -512).
constexpr u16 kEndOfBlock = 0xFE00;

// The 8 bytes at the start of an STR bitstream frame.
struct FrameHeader {
  u16 code_words = 0;  // MDEC code stream size in 32-bit words, rounded up to 32
  u16 qscale = 0;      // AC quantisation scale for every block
  u16 version = 0;     // 1, 2 (identical bitstreams) or 3 (DC coded differentially)
};
std::optional<FrameHeader> parse_header(std::span<const u8> bitstream);

// Stage 1, the DecDCTvlc step: bitstream -> MDEC run-length codes. Blocks are in
// the order the MDEC consumes them (Cr, Cb, Y top-left, top-right, bottom-left,
// bottom-right per 16x16 macroblock; macroblocks column by column), each starting
// with (qscale << 10 | DC) and ending with kEndOfBlock. On a corrupt or truncated
// stream 'codes' holds every complete block before the fault and false is returned.
bool decode_codes(std::span<const u8> bitstream, int width, int height, std::vector<u16>& codes,
                  std::string* error = nullptr);

// Stage 2, the MDEC in 24-bit mode: run-length codes -> RGB, 3 bytes per pixel in
// R, G, B order (the byte order the MDEC writes and 24-bit VRAM holds), rows top to
// bottom. 'rgb' is always resized to a full image; macroblocks the codes do not
// cover are left mid-grey (as zero blocks decode), and false is returned.
bool decode_image(std::span<const u16> codes, int width, int height, std::vector<u8>& rgb,
                  std::string* error = nullptr);

// Both stages. Reuses the storage of 'rgb' across frames.
bool decode_frame(std::span<const u8> bitstream, int width, int height, std::vector<u8>& rgb,
                  std::string* error = nullptr);
std::optional<std::vector<u8>> decode_frame(std::span<const u8> bitstream, int width, int height,
                                            std::string* error = nullptr);

// The MDEC's 15-bit output mode from a 24-bit image: VRAM pixels (red in bits 0-4,
// green 5-9, blue 10-14), bit 15 set when 'mask_bit' (DecDCTin's STP flag). Each
// channel is rounded to nearest and saturated, as Mednafen models the chip; unlike
// the 24-bit path this has no jPSXdec reference to check against. 'out' must hold
// one u16 per pixel.
void to_rgb15(std::span<const u8> rgb, std::span<u16> out, bool mask_bit = false);

}  // namespace ss::psx::mdec
