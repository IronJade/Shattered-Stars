#include <vector>

#include "disc/slz.h"
#include "test.h"

using namespace ss;

namespace {

std::vector<u8> block(u8 method, u32 unpacked, std::vector<u8> body, u32 next = 0) {
  std::vector<u8> out = {'S', 'L', 'Z', method};
  auto put = [&](u32 v) {
    for (int i = 0; i < 4; ++i) out.push_back(v >> (8 * i) & 0xFF);
  };
  put(static_cast<u32>(body.size()));
  put(unpacked);
  put(next);
  out.insert(out.end(), body.begin(), body.end());
  return out;
}

}  // namespace

TEST(slz_stored) {
  const auto data = block(0, 3, {1, 2, 3});
  std::vector<u8> out;
  CHECK(disc::slz::unpack(data, 0, out, nullptr));
  CHECK(out == (std::vector<u8>{1, 2, 3}));
}

TEST(slz_method1_literals_and_overlapping_reference) {
  // Flags 0b011: two literals 'A','B', then a reference distance 2 length 4 -> ABABAB,
  // then a zero-distance reference ending the stream (bit 3 clear).
  const auto data = block(1, 6, {0x03, 'A', 'B', 0x02, 0x10, 0x00, 0x00});
  std::vector<u8> out;
  CHECK(disc::slz::unpack(data, 0, out, nullptr));
  CHECK(out == (std::vector<u8>{'A', 'B', 'A', 'B', 'A', 'B'}));
}

TEST(slz_method2_fills) {
  // Literal 'x'; long fill (b1 = 0xF0: b0 + 19 copies of b2); short fill (b1 = 0xF2: 5 copies of b0).
  const auto data = block(2, 1 + 20 + 5, {0x01, 'x', 0x01, 0xF0, 'y', 'z', 0xF2});
  std::vector<u8> out;
  CHECK(disc::slz::unpack(data, 0, out, nullptr));
  CHECK_EQ(out.size(), 26u);
  CHECK(out[0] == 'x');
  CHECK(out[1] == 'y' && out[20] == 'y');
  CHECK(out[21] == 'z' && out[25] == 'z');
}

// A reference before the start of the output reads zeros (the compressor's cleared
// window; the console reads the memory before its buffer). Music banks #2646, #2661,
// #2662, #2678 and #2733 have one.
TEST(slz_reference_before_start_reads_zeros) {
  const auto data = block(1, 4, {0x00, 0x05, 0x10, 0x00, 0x00});
  std::vector<u8> out;
  std::string error;
  CHECK(disc::slz::unpack(data, 0, out, &error));
  CHECK(out.size() >= 4 && out[0] == 0 && out[1] == 0 && out[2] == 0 && out[3] == 0);
}

TEST(slz_block_chain) {
  auto first = block(0, 1, {7}, 0);
  const auto second = block(0, 1, {9});
  const u32 next = static_cast<u32>(first.size());
  for (int i = 0; i < 4; ++i) first[12 + i] = next >> (8 * i) & 0xFF;
  first.insert(first.end(), second.begin(), second.end());
  auto out = disc::slz::unpack_block(first, 0, 1, nullptr);
  CHECK(out && out->size() == 1 && (*out)[0] == 9);
  CHECK(!disc::slz::unpack_block(first, 0, 2, nullptr));
}
