#include "util/deflate.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <fstream>
#include <queue>
#include <utility>

namespace ss::util {
namespace {

// ---- Bits, least significant first (RFC 1951 3.1.1) -------------------------------------

struct BitWriter {
  std::vector<u8>* out;
  u64 acc = 0;
  int count = 0;
  void put(u32 value, int bits) {
    acc |= static_cast<u64>(value) << count;
    count += bits;
    while (count >= 8) {
      out->push_back(static_cast<u8>(acc));
      acc >>= 8;
      count -= 8;
    }
  }
  void flush() {
    if (count > 0) out->push_back(static_cast<u8>(acc));
    acc = 0;
    count = 0;
  }
};

constexpr std::array<u16, 29> kLengthBase = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                             31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr std::array<u8, 29> kLengthExtra = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                             2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr std::array<u16, 30> kDistBase = {1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
                                           193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
constexpr std::array<u8, 30> kDistExtra = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6,
                                           6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
constexpr std::array<u8, 19> kClOrder = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

int length_symbol(int len) {
  int i = 28;
  while (kLengthBase[i] > len) --i;
  return i;
}
int dist_symbol(int dist) {
  int i = 29;
  while (kDistBase[i] > dist) --i;
  return i;
}

// Huffman code lengths for 'freq' (at least two symbols should be used), at most 'limit'
// bits: a Huffman tree's depth counts, pushed under the limit as miniz does
// (tdefl_huffman_enforce_max_code_size), then handed out most frequent first.
std::vector<u8> code_lengths(const std::vector<u32>& freq, int limit) {
  const int n = static_cast<int>(freq.size());
  std::vector<u8> lengths(n, 0);
  std::vector<int> used;
  for (int i = 0; i < n; ++i)
    if (freq[i]) used.push_back(i);
  if (used.empty()) return lengths;
  if (used.size() == 1) {
    lengths[used[0]] = 1;
    return lengths;
  }
  // Tree: nodes 0..k-1 the used symbols, then internal nodes; ties broken by node number.
  const int k = static_cast<int>(used.size());
  std::vector<int> parent(2 * k, -1);
  using Item = std::pair<u64, int>;
  std::priority_queue<Item, std::vector<Item>, std::greater<Item>> heap;
  for (int i = 0; i < k; ++i) heap.emplace(freq[used[i]], i);
  int next = k;
  while (heap.size() > 1) {
    const Item a = heap.top();
    heap.pop();
    const Item b = heap.top();
    heap.pop();
    parent[a.second] = parent[b.second] = next;
    heap.emplace(a.first + b.first, next++);
  }
  std::vector<int> depth(next, 0);
  for (int i = next - 2; i >= 0; --i) depth[i] = depth[parent[i]] + 1;
  std::vector<int> count(std::max(limit, 64) + 1, 0);
  int max_depth = 0;
  for (int i = 0; i < k; ++i) {
    ++count[depth[i]];
    max_depth = std::max(max_depth, depth[i]);
  }
  if (max_depth > limit) {
    for (int d = limit + 1; d <= max_depth; ++d) {
      count[limit] += count[d];
      count[d] = 0;
    }
    u32 total = 0;
    for (int d = 1; d <= limit; ++d) total += static_cast<u32>(count[d]) << (limit - d);
    while (total != (1u << limit)) {
      --count[limit];
      for (int d = limit - 1; d > 0; --d) {
        if (count[d]) {
          --count[d];
          count[d + 1] += 2;
          break;
        }
      }
      --total;
    }
  }
  std::vector<int> order = used;
  std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return freq[a] > freq[b]; });
  size_t at = 0;
  for (int d = 1; d <= limit; ++d)
    for (int c = 0; c < count[d]; ++c) lengths[order[at++]] = static_cast<u8>(d);
  return lengths;
}

// Canonical codes (RFC 1951 3.2.2), bit-reversed for the LSB-first writer.
std::vector<u16> canonical_codes(const std::vector<u8>& lengths) {
  std::array<u16, 17> bl_count{};
  for (u8 l : lengths) ++bl_count[l];
  bl_count[0] = 0;
  std::array<u16, 17> next{};
  u16 code = 0;
  for (int bits = 1; bits <= 16; ++bits) {
    code = static_cast<u16>((code + bl_count[bits - 1]) << 1);
    next[bits] = code;
  }
  std::vector<u16> codes(lengths.size(), 0);
  for (size_t i = 0; i < lengths.size(); ++i) {
    const int len = lengths[i];
    if (!len) continue;
    u16 c = next[len]++, r = 0;
    for (int b = 0; b < len; ++b) r = static_cast<u16>(r << 1 | (c >> b & 1));
    codes[i] = r;
  }
  return codes;
}

// Make sure at least two symbols have codes (some inflaters refuse a one-code tree).
void at_least_two(std::vector<u32>& freq) {
  int used = 0;
  for (u32 f : freq) used += f != 0;
  for (size_t i = 0; used < 2 && i < freq.size(); ++i)
    if (!freq[i]) {
      freq[i] = 1;
      ++used;
    }
}

struct Token {
  u16 len_or_lit;  // a literal byte when dist == 0, else the match length
  u16 dist;
};

void write_block(BitWriter& w, const std::vector<Token>& tokens, bool last) {
  std::vector<u32> lit(286, 0), dist(30, 0);
  for (const Token& t : tokens) {
    if (t.dist == 0) {
      ++lit[t.len_or_lit];
    } else {
      ++lit[257 + length_symbol(t.len_or_lit)];
      ++dist[dist_symbol(t.dist)];
    }
  }
  lit[256] = 1;
  at_least_two(lit);
  at_least_two(dist);
  const std::vector<u8> lit_len = code_lengths(lit, 15), dist_len = code_lengths(dist, 15);
  int hlit = 286, hdist = 30;
  while (hlit > 257 && lit_len[hlit - 1] == 0) --hlit;
  while (hdist > 1 && dist_len[hdist - 1] == 0) --hdist;
  std::vector<u8> seq(lit_len.begin(), lit_len.begin() + hlit);
  seq.insert(seq.end(), dist_len.begin(), dist_len.begin() + hdist);
  // Run-length coding of the lengths: 16 repeats the previous 3-6 times, 17 / 18 are 3-10 /
  // 11-138 zeros.
  struct Cl {
    u8 sym, extra_bits;
    u8 extra;
  };
  std::vector<Cl> cl;
  for (size_t i = 0; i < seq.size();) {
    const u8 l = seq[i];
    size_t run = 1;
    while (i + run < seq.size() && seq[i + run] == l) ++run;
    if (l == 0) {
      size_t left = run;
      while (left >= 11) {
        const size_t r = std::min<size_t>(left, 138);
        cl.push_back({18, 7, static_cast<u8>(r - 11)});
        left -= r;
      }
      if (left >= 3) {
        cl.push_back({17, 3, static_cast<u8>(left - 3)});
        left = 0;
      }
      while (left--) cl.push_back({0, 0, 0});
    } else {
      cl.push_back({l, 0, 0});
      size_t left = run - 1;
      while (left >= 3) {
        const size_t r = std::min<size_t>(left, 6);
        cl.push_back({16, 2, static_cast<u8>(r - 3)});
        left -= r;
      }
      while (left--) cl.push_back({l, 0, 0});
    }
    i += run;
  }
  std::vector<u32> cl_freq(19, 0);
  for (const Cl& c : cl) ++cl_freq[c.sym];
  at_least_two(cl_freq);
  const std::vector<u8> cl_len = code_lengths(cl_freq, 7);
  int hclen = 19;
  while (hclen > 4 && cl_len[kClOrder[hclen - 1]] == 0) --hclen;

  const std::vector<u16> lit_code = canonical_codes(lit_len), dist_code = canonical_codes(dist_len),
                         cl_code = canonical_codes(cl_len);
  w.put(last ? 1 : 0, 1);
  w.put(2, 2);  // dynamic Huffman
  w.put(static_cast<u32>(hlit - 257), 5);
  w.put(static_cast<u32>(hdist - 1), 5);
  w.put(static_cast<u32>(hclen - 4), 4);
  for (int i = 0; i < hclen; ++i) w.put(cl_len[kClOrder[i]], 3);
  for (const Cl& c : cl) {
    w.put(cl_code[c.sym], cl_len[c.sym]);
    if (c.extra_bits) w.put(c.extra, c.extra_bits);
  }
  for (const Token& t : tokens) {
    if (t.dist == 0) {
      w.put(lit_code[t.len_or_lit], lit_len[t.len_or_lit]);
      continue;
    }
    const int ls = length_symbol(t.len_or_lit);
    w.put(lit_code[257 + ls], lit_len[257 + ls]);
    if (kLengthExtra[ls]) w.put(static_cast<u32>(t.len_or_lit - kLengthBase[ls]), kLengthExtra[ls]);
    const int ds = dist_symbol(t.dist);
    w.put(dist_code[ds], dist_len[ds]);
    if (kDistExtra[ds]) w.put(static_cast<u32>(t.dist - kDistBase[ds]), kDistExtra[ds]);
  }
  w.put(lit_code[256], lit_len[256]);
}

u32 adler32(std::span<const u8> data) {
  u32 a = 1, b = 0;
  for (size_t i = 0; i < data.size();) {
    const size_t n = std::min<size_t>(data.size() - i, 5552);
    for (size_t k = 0; k < n; ++k, ++i) {
      a += data[i];
      b += a;
    }
    a %= 65521;
    b %= 65521;
  }
  return b << 16 | a;
}

}  // namespace

u32 crc32(const u8* data, size_t len, u32 crc) {
  static const auto table = [] {
    std::array<u32, 256> t{};
    for (u32 i = 0; i < 256; ++i) {
      u32 c = i;
      for (int k = 0; k < 8; ++k) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      t[i] = c;
    }
    return t;
  }();
  crc = ~crc;
  for (size_t i = 0; i < len; ++i) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
  return ~crc;
}

namespace {

void put_be32(std::vector<u8>& out, u32 v) {
  out.push_back(static_cast<u8>(v >> 24));
  out.push_back(static_cast<u8>(v >> 16));
  out.push_back(static_cast<u8>(v >> 8));
  out.push_back(static_cast<u8>(v));
}

void png_chunk(std::vector<u8>& out, const char* type, std::span<const u8> body) {
  put_be32(out, static_cast<u32>(body.size()));
  const size_t start = out.size();
  out.insert(out.end(), type, type + 4);
  out.insert(out.end(), body.begin(), body.end());
  put_be32(out, crc32(out.data() + start, out.size() - start));
}

int paeth(int a, int b, int c) {
  const int p = a + b - c, pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
  if (pa <= pb && pa <= pc) return a;
  return pb <= pc ? b : c;
}

}  // namespace

std::vector<u8> zlib_compress(std::span<const u8> data) {
  constexpr int kWindow = 32768, kHashBits = 15, kMaxChain = 96, kMinMatch = 3, kMaxMatch = 258;
  constexpr size_t kBlockTokens = 1 << 16;
  std::vector<u8> out = {0x78, 0x9C};
  BitWriter w{&out};
  std::vector<s32> head(1 << kHashBits, -1), prev(kWindow, -1);
  auto hash = [&](size_t p) {
    return ((static_cast<u32>(data[p]) << 10) ^ (static_cast<u32>(data[p + 1]) << 5) ^ data[p + 2]) &
           ((1u << kHashBits) - 1);
  };
  auto insert = [&](size_t p) {
    if (p + kMinMatch > data.size()) return;
    const u32 h = hash(p);
    prev[p & (kWindow - 1)] = head[h];
    head[h] = static_cast<s32>(p);
  };
  std::vector<Token> tokens;
  tokens.reserve(kBlockTokens);
  size_t pos = 0;
  const size_t n = data.size();
  while (pos < n) {
    int best_len = 0, best_dist = 0;
    if (pos + kMinMatch <= n) {
      s32 cand = head[hash(pos)];
      const size_t max_len = std::min<size_t>(kMaxMatch, n - pos);
      for (int chain = 0; cand >= 0 && chain < kMaxChain; ++chain) {
        const size_t c = static_cast<size_t>(cand);
        if (pos - c > static_cast<size_t>(kWindow)) break;
        if (data[c + best_len] == data[pos + best_len] || best_len == 0) {
          size_t l = 0;
          while (l < max_len && data[c + l] == data[pos + l]) ++l;
          if (static_cast<int>(l) > best_len) {
            best_len = static_cast<int>(l);
            best_dist = static_cast<int>(pos - c);
            if (l == max_len) break;
          }
        }
        const s32 p = prev[c & (kWindow - 1)];
        if (p >= cand) break;  // the chain went past the window
        cand = p;
      }
    }
    if (best_len >= kMinMatch) {
      tokens.push_back({static_cast<u16>(best_len), static_cast<u16>(best_dist)});
      for (int k = 0; k < best_len; ++k) insert(pos + k);
      pos += best_len;
    } else {
      tokens.push_back({data[pos], 0});
      insert(pos);
      ++pos;
    }
    if (tokens.size() >= kBlockTokens) {
      write_block(w, tokens, pos >= n);
      tokens.clear();
    }
  }
  if (!tokens.empty() || n == 0) write_block(w, tokens, true);
  w.flush();
  put_be32(out, adler32(data));
  return out;
}

namespace {

// An 8-bit PNG of 'channels' 3 (RGB) or 4 (RGBA) per pixel (see encode_png_rgb).
std::vector<u8> encode_png(std::span<const u32> pixels, int width, int height, int channels) {
  const size_t bpp = static_cast<size_t>(channels);
  const size_t stride = static_cast<size_t>(width) * bpp;
  std::vector<u8> raw;
  raw.reserve((stride + 1) * height);
  std::vector<u8> prior(stride, 0), row(stride), best(stride), trial(stride);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const u32 p = pixels[static_cast<size_t>(y) * width + x];
      row[x * bpp] = static_cast<u8>(p);
      row[x * bpp + 1] = static_cast<u8>(p >> 8);
      row[x * bpp + 2] = static_cast<u8>(p >> 16);
      if (bpp == 4) row[x * bpp + 3] = static_cast<u8>(p >> 24);
    }
    u64 best_sum = ~0ull;
    int best_filter = 0;
    for (int f = 0; f < 5; ++f) {
      u64 sum = 0;
      for (size_t i = 0; i < stride; ++i) {
        const int a = i >= bpp ? row[i - bpp] : 0, b = prior[i], c = i >= bpp ? prior[i - bpp] : 0;
        int v = row[i];
        switch (f) {
          case 1: v -= a; break;
          case 2: v -= b; break;
          case 3: v -= (a + b) / 2; break;
          case 4: v -= paeth(a, b, c); break;
          default: break;
        }
        trial[i] = static_cast<u8>(v);
        const s8 s = static_cast<s8>(trial[i]);
        sum += static_cast<u64>(s < 0 ? -s : s);
      }
      if (sum < best_sum) {
        best_sum = sum;
        best_filter = f;
        best.swap(trial);
      }
    }
    raw.push_back(static_cast<u8>(best_filter));
    raw.insert(raw.end(), best.begin(), best.end());
    prior.swap(row);
  }
  std::vector<u8> png = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
  std::vector<u8> ihdr;
  put_be32(ihdr, static_cast<u32>(width));
  put_be32(ihdr, static_cast<u32>(height));
  // 8-bit RGB (colour type 2) or RGBA (6), deflate, adaptive filters, no interlace
  ihdr.insert(ihdr.end(), {8, static_cast<u8>(channels == 4 ? 6 : 2), 0, 0, 0});
  png_chunk(png, "IHDR", ihdr);
  png_chunk(png, "IDAT", zlib_compress(raw));
  png_chunk(png, "IEND", {});
  return png;
}

}  // namespace

std::vector<u8> encode_png_rgb(std::span<const u32> pixels, int width, int height) {
  return encode_png(pixels, width, height, 3);
}

std::vector<u8> encode_png_rgba(std::span<const u32> pixels, int width, int height) {
  return encode_png(pixels, width, height, 4);
}

bool write_png_rgb(const std::filesystem::path& path, std::span<const u32> pixels, int width, int height,
                   std::string* error) {
  const std::vector<u8> png = encode_png_rgb(pixels, width, height);
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  if (f) f.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
  if (!f) {
    if (error) *error = "could not write " + path.string();
    return false;
  }
  return true;
}

}  // namespace ss::util
