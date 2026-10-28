#include "psx/mdec.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace ss::psx::mdec {
namespace {

constexpr u16 kMagic = 0x3800;
constexpr int kMaxDimension = 4096;  // far beyond any PS1 movie; rejects garbage headers

// The standard intra quantisation table (MPEG-1's, with 2 for DC) that libpress loads
// into the MDEC, in natural row-major order.
constexpr std::array<u8, 64> kQuant = {
    2,  16, 19, 22, 26, 27, 29, 34,  //
    16, 16, 22, 24, 27, 29, 34, 37,  //
    19, 22, 26, 27, 29, 34, 34, 38,  //
    22, 22, 26, 27, 29, 34, 37, 40,  //
    22, 26, 27, 29, 32, 35, 40, 48,  //
    26, 27, 29, 32, 35, 40, 48, 58,  //
    26, 27, 29, 34, 38, 46, 56, 69,  //
    27, 29, 35, 38, 46, 56, 69, 83,
};

// Zigzag index -> natural index.
constexpr std::array<u8, 64> kZigzag = {
    0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,   //
    12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6,  7,  14, 21, 28,  //
    35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,  //
    58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63,
};

// The IDCT basis libpress uploads to the MDEC: kCos[u * 8 + x] is frequency u at
// sample x, in 1.15 fixed point with sqrt(1/2) folded into row 0. The negative
// entries are not plain negations of the positive ones; that is the table's own rounding.
constexpr std::array<s32, 64> kCos = {
    23170, 23170,  23170,  23170,  23170,  23170,  23170,  23170,   //
    32138, 27245,  18204,  6392,   -6393,  -18205, -27246, -32139,  //
    30273, 12539,  -12540, -30274, -30274, -12540, 12539,  30273,   //
    27245, -6393,  -32139, -18205, 18204,  32138,  6392,   -27246,  //
    23170, -23171, -23171, 23170,  23170,  -23171, -23171, 23170,   //
    18204, -32139, 6392,   27245,  -27246, -6393,  32138,  -18205,  //
    12539, -30274, 30273,  -12540, -12540, 30273,  -30274, 12539,   //
    6392,  -18205, 27245,  -32139, 32138,  -27246, 18204,  -6393,
};

// AC run/level codes, MPEG-1's table B.14 without the sign bit that follows each one.
struct AcCode {
  const char* bits;
  u8 run;
  u8 level;
};
constexpr AcCode kAcCodes[] = {
    {"11", 0, 1},                 {"011", 1, 1},                {"0100", 0, 2},
    {"0101", 2, 1},               {"00101", 0, 3},              {"00111", 3, 1},
    {"00110", 4, 1},              {"000110", 1, 2},             {"000111", 5, 1},
    {"000101", 6, 1},             {"000100", 7, 1},             {"0000110", 0, 4},
    {"0000100", 2, 2},            {"0000111", 8, 1},            {"0000101", 9, 1},
    {"00100110", 0, 5},           {"00100001", 0, 6},           {"00100101", 1, 3},
    {"00100100", 3, 2},           {"00100111", 10, 1},          {"00100011", 11, 1},
    {"00100010", 12, 1},          {"00100000", 13, 1},          {"0000001010", 0, 7},
    {"0000001100", 1, 4},         {"0000001011", 2, 3},         {"0000001111", 4, 2},
    {"0000001001", 5, 2},         {"0000001110", 14, 1},        {"0000001101", 15, 1},
    {"0000001000", 16, 1},        {"000000011101", 0, 8},       {"000000011000", 0, 9},
    {"000000010011", 0, 10},      {"000000010000", 0, 11},      {"000000011011", 1, 5},
    {"000000010100", 2, 4},       {"000000011100", 3, 3},       {"000000010010", 4, 3},
    {"000000011110", 6, 2},       {"000000010101", 7, 2},       {"000000010001", 8, 2},
    {"000000011111", 17, 1},      {"000000011010", 18, 1},      {"000000011001", 19, 1},
    {"000000010111", 20, 1},      {"000000010110", 21, 1},      {"0000000011010", 0, 12},
    {"0000000011001", 0, 13},     {"0000000011000", 0, 14},     {"0000000010111", 0, 15},
    {"0000000010110", 1, 6},      {"0000000010101", 1, 7},      {"0000000010100", 2, 5},
    {"0000000010011", 3, 4},      {"0000000010010", 5, 3},      {"0000000010001", 9, 2},
    {"0000000010000", 10, 2},     {"0000000011111", 22, 1},     {"0000000011110", 23, 1},
    {"0000000011101", 24, 1},     {"0000000011100", 25, 1},     {"0000000011011", 26, 1},
    {"00000000011111", 0, 16},    {"00000000011110", 0, 17},    {"00000000011101", 0, 18},
    {"00000000011100", 0, 19},    {"00000000011011", 0, 20},    {"00000000011010", 0, 21},
    {"00000000011001", 0, 22},    {"00000000011000", 0, 23},    {"00000000010111", 0, 24},
    {"00000000010110", 0, 25},    {"00000000010101", 0, 26},    {"00000000010100", 0, 27},
    {"00000000010011", 0, 28},    {"00000000010010", 0, 29},    {"00000000010001", 0, 30},
    {"00000000010000", 0, 31},    {"000000000011000", 0, 32},   {"000000000010111", 0, 33},
    {"000000000010110", 0, 34},   {"000000000010101", 0, 35},   {"000000000010100", 0, 36},
    {"000000000010011", 0, 37},   {"000000000010010", 0, 38},   {"000000000010001", 0, 39},
    {"000000000010000", 0, 40},   {"000000000011111", 1, 8},    {"000000000011110", 1, 9},
    {"000000000011101", 1, 10},   {"000000000011100", 1, 11},   {"000000000011011", 1, 12},
    {"000000000011010", 1, 13},   {"000000000011001", 1, 14},   {"0000000000010011", 1, 15},
    {"0000000000010010", 1, 16},  {"0000000000010001", 1, 17},  {"0000000000010000", 1, 18},
    {"0000000000010100", 6, 3},   {"0000000000011010", 11, 2},  {"0000000000011001", 12, 2},
    {"0000000000011000", 13, 2},  {"0000000000010111", 14, 2},  {"0000000000010110", 15, 2},
    {"0000000000010101", 16, 2},  {"0000000000011111", 27, 1},  {"0000000000011110", 28, 1},
    {"0000000000011101", 29, 1},  {"0000000000011100", 30, 1},  {"0000000000011011", 31, 1},
};

enum class AcKind : u8 { kInvalid, kCoefficient, kEndOfBlock, kEscape };

struct AcEntry {
  u8 length = 0;
  AcKind kind = AcKind::kInvalid;
  u8 run = 0;
  u8 level = 0;
};

// Every code of up to 8 bits has a 1 in its first six bits and every longer one
// (10 to 16 bits) starts with six zeros, so two small tables cover all of them.
struct AcTables {
  std::array<AcEntry, 256> short_codes;  // indexed by the next 8 bits
  std::array<AcEntry, 1024> long_codes;  // indexed by the 10 bits after the zeros

  const AcEntry& lookup(u32 next16) const {
    return (next16 >> 10) ? short_codes[next16 >> 8] : long_codes[next16 & 0x3FF];
  }
};

const AcTables& ac_tables() {
  static const AcTables tables = [] {
    AcTables t{};
    auto add = [&t](const char* bits, AcKind kind, u8 run, u8 level) {
      const int length = static_cast<int>(std::strlen(bits));
      u32 code = 0;
      for (int i = 0; i < length; ++i) code = code << 1 | (bits[i] == '1');
      const AcEntry entry{static_cast<u8>(length), kind, run, level};
      const bool is_short = length <= 8;
      const int spare = (is_short ? 8 : 16) - length;
      const u32 base = (is_short ? code : code & 0x3FF) << spare;
      AcEntry* table = is_short ? t.short_codes.data() : t.long_codes.data();
      for (u32 i = 0; i < (1u << spare); ++i) table[base | i] = entry;
    };
    add("10", AcKind::kEndOfBlock, 0, 0);
    add("000001", AcKind::kEscape, 0, 0);
    for (const auto& c : kAcCodes) add(c.bits, AcKind::kCoefficient, c.run, c.level);
    return t;
  }();
  return tables;
}

// Version 3 DC codes: a prefix giving the bit size of the difference from the
// previous block of the same kind (luma, Cb or Cr). Values are MSB-first.
struct DcCode {
  u8 length;
  u8 bits;
  u8 size;
};
constexpr DcCode kLumaDc[] = {{2, 0b00, 1},    {2, 0b01, 2},     {3, 0b100, 0},
                              {3, 0b101, 3},   {3, 0b110, 4},    {4, 0b1110, 5},
                              {5, 0b11110, 6}, {6, 0b111110, 7}, {7, 0b1111110, 8}};
constexpr DcCode kChromaDc[] = {{2, 0b00, 0},     {2, 0b01, 1},      {2, 0b10, 2},
                                {3, 0b110, 3},    {4, 0b1110, 4},    {5, 0b11110, 5},
                                {6, 0b111110, 6}, {7, 0b1111110, 7}, {8, 0b11111110, 8}};

// STR bitstreams are read as little-endian 16-bit words, most significant bit first.
// Reads past the end return zeros; overrun() reports them.
class BitReader {
 public:
  BitReader(std::span<const u8> data, size_t byte_offset) : data_(data), pos_(byte_offset * 8) {}

  u32 peek(int n) const {  // n <= 32
    if (n == 0) return 0;
    const size_t w = pos_ >> 4;
    const u64 window = u64{word(w)} << 48 | u64{word(w + 1)} << 32 | u64{word(w + 2)} << 16 | word(w + 3);
    return static_cast<u32>((window << (pos_ & 15)) >> (64 - n));
  }
  u32 read(int n) {
    const u32 v = peek(n);
    pos_ += static_cast<size_t>(n);
    return v;
  }
  void skip(int n) { pos_ += static_cast<size_t>(n); }
  bool overrun() const { return pos_ > data_.size() * 8; }

 private:
  u16 word(size_t index) const {
    const size_t b = index * 2;
    if (b + 1 < data_.size()) return read_u16(&data_[b]);
    return b < data_.size() ? data_[b] : 0;
  }

  std::span<const u8> data_;
  size_t pos_;
};

int sign_extend10(u32 v) { return static_cast<int>(v & 0x3FF) - static_cast<int>((v & 0x200) << 1); }

bool fail(std::string* error, std::string message) {
  if (error) *error = std::move(message);
  return false;
}

int macroblocks(int pixels) { return (pixels + 15) / 16; }

// One version 3 DC value: prefix, then 'size' bits holding the difference (the
// JPEG-style encoding: a leading 0 bit means negative), in steps of 4.
bool read_dc_v3(BitReader& br, bool luma, int& predictor) {
  const u32 next = br.peek(8);
  const std::span<const DcCode> table = luma ? std::span<const DcCode>(kLumaDc) : std::span<const DcCode>(kChromaDc);
  for (const auto& c : table) {
    if ((next >> (8 - c.length)) != c.bits) continue;
    br.skip(c.length);
    int diff = 0;
    if (c.size) {
      diff = static_cast<int>(br.read(c.size));
      if (!(diff & (1 << (c.size - 1)))) diff -= (1 << c.size) - 1;
    }
    predictor += diff * 4;
    return predictor >= -512 && predictor <= 511;
  }
  return false;
}

using Block = std::array<s32, 64>;

// The IDCT as jPSXdec's PSX model computes it: a vertical then a horizontal pass
// over kCos, kept at full precision and rounded (half up) once at the end. Only
// non-zero coefficients contribute, so the passes skip the (usually many) zeros.
void idct(Block& blk) {
  std::array<s64, 64> tmp{};  // tmp[x + 8 * y]: column x after the vertical pass
  u8 columns = 0;
  for (int i = 0; i < 64; ++i) {
    if (!blk[i]) continue;
    const int x = i & 7, u = i >> 3;
    columns |= static_cast<u8>(1 << x);
    for (int y = 0; y < 8; ++y) tmp[x + 8 * y] += s64{blk[i]} * kCos[8 * u + y];
  }
  for (int y = 0; y < 8; ++y) {
    for (int x = 0; x < 8; ++x) {
      s64 sum = 0;
      for (int k = 0; k < 8; ++k) {
        if (columns >> k & 1) sum += kCos[8 * k + x] * tmp[k + 8 * y];
      }
      blk[x + 8 * y] = static_cast<s32>((sum + (s64{1} << 31)) >> 32);
    }
  }
}

// Dequantises the next block of 'codes' into 'blk' and transforms it. False when
// the codes run out or a run goes past the 64th coefficient.
bool read_block(std::span<const u16> codes, size_t& at, Block& blk) {
  blk.fill(0);
  while (at < codes.size() && codes[at] == kEndOfBlock) ++at;  // the MDEC skips padding between blocks
  if (at >= codes.size()) return false;
  const u16 first = codes[at++];
  const int qscale = first >> 10;
  blk[0] = sign_extend10(first) * kQuant[0];
  int k = 0;
  for (;;) {
    if (at >= codes.size()) return false;
    const u16 code = codes[at++];
    if (code == kEndOfBlock) break;
    k += (code >> 10) + 1;
    if (k > 63) return false;
    const int n = kZigzag[k];
    blk[n] = (sign_extend10(code) * kQuant[n] * qscale + 4) >> 3;
  }
  idct(blk);
  return true;
}

u8 clamp8(int v) { return static_cast<u8>(std::clamp(v, 0, 255)); }
s32 round_shift16(s64 v) { return static_cast<s32>((v + 0x8000) >> 16); }

// Colour conversion for one decoded macroblock (Cr, Cb, then four luma blocks).
// Each chroma sample covers 2x2 pixels, as on the MDEC (no interpolation).
void emit_macroblock(const std::array<Block, 6>& blk, int x0, int y0, int width, int height, u8* rgb) {
  const Block& cr = blk[0];
  const Block& cb = blk[1];
  for (int py = 0; py < 16; ++py) {
    const int y = y0 + py;
    if (y >= height) break;
    const Block& left = blk[py < 8 ? 2 : 4];
    const Block& right = blk[py < 8 ? 3 : 5];
    u8* out = rgb + (static_cast<size_t>(y) * width + x0) * 3;
    for (int px = 0; px < 16 && x0 + px < width; ++px) {
      const int c = (px >> 1) + (py >> 1) * 8;
      const s32 r = round_shift16(91893ll * cr[c]);
      const s32 g = round_shift16(-22525ll * cb[c] - 46812ll * cr[c]);
      const s32 b = round_shift16(116224ll * cb[c]);
      const int luma = (px < 8 ? left : right)[(px & 7) + (py & 7) * 8] + 128;
      *out++ = clamp8(luma + r);
      *out++ = clamp8(luma + g);
      *out++ = clamp8(luma + b);
    }
  }
}

bool valid_size(int width, int height) {
  return width > 0 && height > 0 && width <= kMaxDimension && height <= kMaxDimension;
}

}  // namespace

std::optional<FrameHeader> parse_header(std::span<const u8> bitstream) {
  if (bitstream.size() < 8 || read_u16(&bitstream[2]) != kMagic) return std::nullopt;
  return FrameHeader{read_u16(&bitstream[0]), read_u16(&bitstream[4]), read_u16(&bitstream[6])};
}

bool decode_codes(std::span<const u8> bitstream, int width, int height, std::vector<u16>& codes,
                  std::string* error) {
  codes.clear();
  const auto header = parse_header(bitstream);
  if (!header) return fail(error, "not an STR bitstream (no 0x3800 header)");
  if (header->version < 1 || header->version > 3)
    return fail(error, "unsupported bitstream version " + std::to_string(header->version));
  if (header->qscale > 63) return fail(error, "quantisation scale out of range");
  if (!valid_size(width, height)) return fail(error, "bad frame size");

  const AcTables& ac = ac_tables();
  const int blocks = macroblocks(width) * macroblocks(height) * 6;
  const u16 qscale_bits = static_cast<u16>(header->qscale << 10);
  codes.reserve(header->code_words * 2u);
  BitReader br(bitstream, 8);
  int dc_predictor[3] = {0, 0, 0};  // version 3: Cr, Cb, luma

  for (int b = 0; b < blocks; ++b) {
    const size_t block_start = codes.size();
    auto corrupt = [&](const char* what) {
      codes.resize(block_start);
      return fail(error, std::string(what) + " in macroblock " + std::to_string(b / 6) + " block " +
                             std::to_string(b % 6));
    };
    const int kind = std::min(b % 6, 2);  // 0 = Cr, 1 = Cb, 2 = luma
    int dc;
    if (header->version == 3) {
      if (!read_dc_v3(br, kind == 2, dc_predictor[kind])) return corrupt("bad DC");
      dc = dc_predictor[kind];
    } else {
      dc = sign_extend10(br.read(10));
    }
    codes.push_back(static_cast<u16>(qscale_bits | (dc & 0x3FF)));

    int position = 0;
    for (;;) {
      const AcEntry& e = ac.lookup(br.peek(16));
      if (e.kind == AcKind::kInvalid) return corrupt("unknown AC code");
      br.skip(e.length);
      if (e.kind == AcKind::kEndOfBlock) break;
      u16 code;
      if (e.kind == AcKind::kEscape) {
        code = static_cast<u16>(br.read(16));  // raw 6-bit run, 10-bit level
      } else {
        const int level = br.read(1) ? -e.level : e.level;
        code = static_cast<u16>(e.run << 10 | (level & 0x3FF));
      }
      position += (code >> 10) + 1;
      if (position > 63) return corrupt("AC run past the end of the block");
      codes.push_back(code);
    }
    if (br.overrun()) return corrupt("bitstream ends");
    codes.push_back(kEndOfBlock);
  }
  return true;
}

bool decode_image(std::span<const u16> codes, int width, int height, std::vector<u8>& rgb, std::string* error) {
  if (!valid_size(width, height)) {
    rgb.clear();
    return fail(error, "bad frame size");
  }
  rgb.resize(static_cast<size_t>(width) * height * 3);
  std::array<Block, 6> blk;
  size_t at = 0;
  bool ok = true;
  for (int mx = 0; mx < macroblocks(width); ++mx) {  // the MDEC's macroblock order is column-major
    for (int my = 0; my < macroblocks(height); ++my) {
      for (int b = 0; b < 6; ++b) {
        if (ok && !read_block(codes, at, blk[b])) {
          ok = false;
          fail(error, "MDEC codes end or overflow in macroblock " +
                          std::to_string(mx * macroblocks(height) + my));
        }
        if (!ok) blk[b].fill(0);
      }
      emit_macroblock(blk, mx * 16, my * 16, width, height, rgb.data());
    }
  }
  return ok;
}

bool decode_frame(std::span<const u8> bitstream, int width, int height, std::vector<u8>& rgb, std::string* error) {
  std::vector<u16> codes;
  const bool vlc_ok = decode_codes(bitstream, width, height, codes, error);
  std::string image_error;
  const bool image_ok = decode_image(codes, width, height, rgb, vlc_ok ? error : &image_error);
  return vlc_ok && image_ok;
}

std::optional<std::vector<u8>> decode_frame(std::span<const u8> bitstream, int width, int height,
                                            std::string* error) {
  std::vector<u8> rgb;
  if (!decode_frame(bitstream, width, height, rgb, error)) return std::nullopt;
  return rgb;
}

void to_rgb15(std::span<const u8> rgb, std::span<u16> out, bool mask_bit) {
  const size_t pixels = std::min(rgb.size() / 3, out.size());
  const u16 mask = mask_bit ? 0x8000 : 0;
  auto five = [](u8 c) { return static_cast<u16>(std::min((c + 4) >> 3, 31)); };
  for (size_t i = 0; i < pixels; ++i) {
    const u8* p = &rgb[i * 3];
    out[i] = static_cast<u16>(five(p[0]) | five(p[1]) << 5 | five(p[2]) << 10 | mask);
  }
}

}  // namespace ss::psx::mdec
