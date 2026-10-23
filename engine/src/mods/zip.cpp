#include "mods/zip.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <system_error>

namespace ss::mods {
namespace {

u32 le16(const u8* p) { return static_cast<u32>(p[0] | p[1] << 8); }
u32 le32(const u8* p) { return static_cast<u32>(p[0] | p[1] << 8 | p[2] << 16) | static_cast<u32>(p[3]) << 24; }

// ---- inflate: canonical Huffman decoding, as RFC 1951 describes it --------------------

constexpr int kMaxBits = 15;

struct Huffman {
  std::array<short, kMaxBits + 1> count{};  // codes of each length
  std::array<short, 288> symbol{};          // symbols ordered by code
};

struct Inflater {
  const u8* in;
  size_t size;
  size_t pos = 0;
  u32 bitbuf = 0;
  int bitcnt = 0;
  bool overrun = false;
  std::vector<u8>* out;
  size_t max_out;

  int bits(int need) {
    u32 val = bitbuf;
    while (bitcnt < need) {
      if (pos >= size) {
        overrun = true;
        return 0;
      }
      val |= static_cast<u32>(in[pos++]) << bitcnt;
      bitcnt += 8;
    }
    bitbuf = val >> need;
    bitcnt -= need;
    return static_cast<int>(val & ((1u << need) - 1));
  }

  // Builds the decoding table from code lengths; returns 0 for a complete code, > 0 for an
  // incomplete one, < 0 for an over-subscribed one.
  static int construct(Huffman* h, const short* length, int n) {
    h->count.fill(0);
    for (int s = 0; s < n; ++s) ++h->count[length[s]];
    if (h->count[0] == n) return 0;  // no codes: complete, but decoding fails
    int left = 1;
    for (int len = 1; len <= kMaxBits; ++len) {
      left <<= 1;
      left -= h->count[len];
      if (left < 0) return left;
    }
    std::array<short, kMaxBits + 1> offs{};
    for (int len = 1; len < kMaxBits; ++len) offs[len + 1] = static_cast<short>(offs[len] + h->count[len]);
    for (int s = 0; s < n; ++s)
      if (length[s] != 0) h->symbol[offs[length[s]]++] = static_cast<short>(s);
    return left;
  }

  int decode(const Huffman& h) {
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= kMaxBits; ++len) {
      code |= bits(1);
      if (overrun) return -1;
      const int count = h.count[len];
      if (code - count < first) return h.symbol[index + (code - first)];
      index += count;
      first += count;
      first <<= 1;
      code <<= 1;
    }
    return -1;
  }

  bool put(u8 b) {
    if (out->size() >= max_out) return false;
    out->push_back(b);
    return true;
  }

  const char* stored() {
    bitbuf = 0;
    bitcnt = 0;
    if (pos + 4 > size) return "the archive is cut short";
    const u32 len = le16(in + pos), nlen = le16(in + pos + 2);
    pos += 4;
    if (len != (~nlen & 0xFFFF)) return "a damaged stored block";
    if (pos + len > size) return "the archive is cut short";
    if (out->size() + len > max_out) return "an entry is larger than allowed";
    out->insert(out->end(), in + pos, in + pos + len);
    pos += len;
    return nullptr;
  }

  const char* codes(const Huffman& lencode, const Huffman& distcode) {
    static constexpr short kBase[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                        31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
    static constexpr short kExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                         2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
    static constexpr short kDist[30] = {1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
                                        193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
    static constexpr short kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                             6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
    for (;;) {
      int symbol = decode(lencode);
      if (symbol < 0) return "damaged compressed data";
      if (symbol < 256) {
        if (!put(static_cast<u8>(symbol))) return "an entry is larger than allowed";
        continue;
      }
      if (symbol == 256) return nullptr;
      symbol -= 257;
      if (symbol >= 29) return "damaged compressed data";
      const size_t len = static_cast<size_t>(kBase[symbol] + bits(kExtra[symbol]));
      const int dsym = decode(distcode);
      if (dsym < 0 || dsym >= 30) return "damaged compressed data";
      const size_t dist = static_cast<size_t>(kDist[dsym] + bits(kDistExtra[dsym]));
      if (overrun) return "the archive is cut short";
      if (dist > out->size()) return "damaged compressed data";
      for (size_t i = 0; i < len; ++i)
        if (!put((*out)[out->size() - dist])) return "an entry is larger than allowed";
    }
  }

  const char* fixed() {
    static Huffman lencode, distcode;
    static bool built = false;
    if (!built) {
      short lengths[288];
      int s = 0;
      for (; s < 144; ++s) lengths[s] = 8;
      for (; s < 256; ++s) lengths[s] = 9;
      for (; s < 280; ++s) lengths[s] = 7;
      for (; s < 288; ++s) lengths[s] = 8;
      construct(&lencode, lengths, 288);
      for (s = 0; s < 30; ++s) lengths[s] = 5;
      construct(&distcode, lengths, 30);
      built = true;
    }
    return codes(lencode, distcode);
  }

  const char* dynamic() {
    static constexpr short kOrder[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    short lengths[320] = {};
    const int nlen = bits(5) + 257, ndist = bits(5) + 1, ncode = bits(4) + 4;
    if (overrun) return "the archive is cut short";
    if (nlen > 286 || ndist > 30) return "damaged compressed data";
    int index = 0;
    for (; index < ncode; ++index) lengths[kOrder[index]] = static_cast<short>(bits(3));
    for (; index < 19; ++index) lengths[kOrder[index]] = 0;
    Huffman lencode, distcode;
    if (construct(&lencode, lengths, 19) != 0) return "damaged compressed data";
    index = 0;
    while (index < nlen + ndist) {
      int symbol = decode(lencode);
      if (symbol < 0) return "damaged compressed data";
      if (symbol < 16) {
        lengths[index++] = static_cast<short>(symbol);
        continue;
      }
      short len = 0;
      if (symbol == 16) {
        if (index == 0) return "damaged compressed data";
        len = lengths[index - 1];
        symbol = 3 + bits(2);
      } else if (symbol == 17) {
        symbol = 3 + bits(3);
      } else {
        symbol = 11 + bits(7);
      }
      if (index + symbol > nlen + ndist) return "damaged compressed data";
      while (symbol--) lengths[index++] = len;
    }
    if (overrun) return "the archive is cut short";
    if (lengths[256] == 0) return "damaged compressed data";
    // Incomplete codes are allowed only when there is a single code.
    int err = construct(&lencode, lengths, nlen);
    if (err < 0 || (err > 0 && nlen - lencode.count[0] != 1)) return "damaged compressed data";
    err = construct(&distcode, lengths + nlen, ndist);
    if (err < 0 || (err > 0 && ndist - distcode.count[0] != 1)) return "damaged compressed data";
    return codes(lencode, distcode);
  }
};

}  // namespace

u32 crc32(const u8* p, size_t n) {
  static const std::array<u32, 256> table = [] {
    std::array<u32, 256> t{};
    for (u32 i = 0; i < 256; ++i) {
      u32 c = i;
      for (int k = 0; k < 8; ++k) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      t[i] = c;
    }
    return t;
  }();
  u32 c = 0xFFFFFFFFu;
  for (size_t i = 0; i < n; ++i) c = table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
  return c ^ 0xFFFFFFFFu;
}

bool inflate(const u8* in, size_t in_size, std::vector<u8>* out, size_t max_out, std::string* error) {
  out->clear();
  Inflater s{in, in_size, 0, 0, 0, false, out, max_out};
  int last = 0;
  do {
    last = s.bits(1);
    const int type = s.bits(2);
    const char* problem = nullptr;
    if (s.overrun) problem = "the archive is cut short";
    else if (type == 0) problem = s.stored();
    else if (type == 1) problem = s.fixed();
    else if (type == 2) problem = s.dynamic();
    else problem = "damaged compressed data";
    if (problem == nullptr && s.overrun) problem = "the archive is cut short";
    if (problem != nullptr) {
      if (error) *error = problem;
      return false;
    }
  } while (!last);
  return true;
}

bool ZipReader::open(const std::filesystem::path& path, std::string* error, size_t max_bytes) {
  std::error_code ec;
  const auto size = std::filesystem::file_size(path, ec);
  if (ec) {
    if (error) *error = "it can't be read (" + ec.message() + ")";
    return false;
  }
  if (size > max_bytes) {
    if (error) *error = "it is too large for a mod package";
    return false;
  }
  std::ifstream in(path, std::ios::binary);
  std::vector<u8> data(static_cast<size_t>(size));
  if (!in || !in.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()))) {
    if (error) *error = "it can't be read";
    return false;
  }
  return open_memory(std::move(data), error);
}

bool ZipReader::open_memory(std::vector<u8> data, std::string* error) {
  data_ = std::move(data);
  entries_.clear();
  return parse(error);
}

bool ZipReader::parse(std::string* error) {
  auto fail = [&](const char* why) {
    if (error) *error = why;
    entries_.clear();
    return false;
  };
  const size_t n = data_.size();
  if (n < 22) return fail("it is not a .zip archive");
  // The end-of-central-directory record: within the last 64 KiB + 22 bytes (a comment).
  size_t eocd = std::string::npos;
  const size_t lowest = n > 0xFFFF + 22 ? n - (0xFFFF + 22) : 0;
  for (size_t p = n - 22;; --p) {
    if (le32(&data_[p]) == 0x06054B50u) {
      eocd = p;
      break;
    }
    if (p == lowest) break;
  }
  if (eocd == std::string::npos) return fail("it is not a .zip archive");
  const u8* e = &data_[eocd];
  const u32 disk = le16(e + 4), cd_disk = le16(e + 6), count = le16(e + 10), cd_size = le32(e + 12),
            cd_offset = le32(e + 16);
  if (disk != 0 || cd_disk != 0 || le16(e + 8) != count) return fail("multi-part archives aren't supported");
  if (count == 0xFFFF || cd_offset == 0xFFFFFFFFu) return fail("zip64 archives aren't supported");
  if (static_cast<size_t>(cd_offset) + cd_size > eocd) return fail("the archive is damaged");
  size_t p = cd_offset;
  for (u32 i = 0; i < count; ++i) {
    if (p + 46 > eocd || le32(&data_[p]) != 0x02014B50u) return fail("the archive is damaged");
    const u8* h = &data_[p];
    ZipEntry z;
    const u32 flags = le16(h + 8);
    z.method = le16(h + 10);
    z.crc = le32(h + 16);
    z.compressed = le32(h + 20);
    z.size = le32(h + 24);
    const u32 name_len = le16(h + 28), extra_len = le16(h + 30), comment_len = le16(h + 32);
    z.local_offset = le32(h + 42);
    if (p + 46 + name_len > eocd) return fail("the archive is damaged");
    z.name.assign(reinterpret_cast<const char*>(h + 46), name_len);
    std::replace(z.name.begin(), z.name.end(), '\\', '/');
    z.directory = !z.name.empty() && z.name.back() == '/';
    if (flags & 1) return fail("encrypted archives aren't supported");
    if (z.size == 0xFFFFFFFFu || z.compressed == 0xFFFFFFFFu || z.local_offset == 0xFFFFFFFFu)
      return fail("zip64 archives aren't supported");
    entries_.push_back(std::move(z));
    p += 46 + name_len + extra_len + comment_len;
  }
  return true;
}

bool ZipReader::read(const ZipEntry& z, std::vector<u8>* out, std::string* error, size_t max_size) const {
  auto fail = [&](const std::string& why) {
    if (error) *error = z.name + ": " + why;
    return false;
  };
  if (z.size > max_size) return fail("larger than allowed");
  const size_t p = z.local_offset;
  if (p + 30 > data_.size() || le32(&data_[p]) != 0x04034B50u) return fail("the archive is damaged");
  const size_t start = p + 30 + le16(&data_[p + 26]) + le16(&data_[p + 28]);
  if (start + z.compressed > data_.size()) return fail("the archive is cut short");
  if (z.method == 0) {
    if (z.compressed != z.size) return fail("the archive is damaged");
    out->assign(data_.begin() + static_cast<std::ptrdiff_t>(start),
                data_.begin() + static_cast<std::ptrdiff_t>(start + z.size));
  } else if (z.method == 8) {
    std::string why;
    if (!inflate(&data_[start], z.compressed, out, z.size, &why)) return fail(why);
    if (out->size() != z.size) return fail("damaged compressed data");
  } else {
    return fail("compressed with a method this installer doesn't read (only stored and deflated)");
  }
  if (crc32(out->data(), out->size()) != z.crc) return fail("damaged (its checksum doesn't match)");
  return true;
}

}  // namespace ss::mods
