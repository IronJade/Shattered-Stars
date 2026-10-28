#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

#include "psx/memory_card.h"
#include "runtime/libcard.h"
#include "test.h"

using namespace ss;

namespace {

std::filesystem::path temp_card(const char* name) {
  auto dir = std::filesystem::temp_directory_path() / "ss_tests_memcard";
  std::filesystem::create_directories(dir);
  auto path = dir / name;
  std::filesystem::remove(path);
  return path;
}

u8 xor_frame(const std::vector<u8>& img, int frame) {
  u8 x = 0;
  for (int i = 0; i < 0x7F; ++i) x ^= img[frame * 0x80 + i];
  return x;
}

}  // namespace

TEST(memcard_format_layout) {
  const auto path = temp_card("format.mcd");
  psx::MemoryCard card;
  CHECK(card.open(path, true));
  std::ifstream in(path, std::ios::binary);
  std::vector<u8> img((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  CHECK_EQ(img.size(), psx::MemoryCard::kSize);
  CHECK(img[0] == 'M' && img[1] == 'C');
  CHECK_EQ(img[0x7F], xor_frame(img, 0));
  for (int b = 1; b < 16; ++b) {
    CHECK_EQ(img[b * 0x80], 0xA0);       // free
    CHECK_EQ(img[b * 0x80 + 0x7F], xor_frame(img, b));
  }
  CHECK(std::memcmp(img.data(), img.data() + 63 * 0x80, 0x80) == 0);  // write-test frame
  CHECK_EQ(card.free_blocks(), 15);
}

TEST(memcard_files_chain_and_erase) {
  psx::MemoryCard card;
  CHECK(card.open(temp_card("files.mcd"), true));
  CHECK(card.create("BASCUS-94421S02-S01", 1));
  CHECK(card.create("OTHERGAME", 3));
  CHECK(!card.create("BASCUS-94421S02-S01", 1));  // exists
  CHECK_EQ(card.free_blocks(), 11);
  auto f = card.find("OTHERGAME");
  CHECK(f.has_value());
  CHECK_EQ(f->size, 3u * 0x2000);
  // Data spanning block boundaries reads back intact.
  std::vector<u8> data(0x2000 + 0x300);
  for (size_t i = 0; i < data.size(); ++i) data[i] = static_cast<u8>(i * 7);
  CHECK(card.write(*f, 0x1E00, data.data(), static_cast<u32>(data.size())));
  std::vector<u8> back(data.size());
  CHECK(card.read(*f, 0x1E00, back.data(), static_cast<u32>(back.size())));
  CHECK(back == data);
  CHECK(card.erase("OTHERGAME"));
  CHECK(!card.find("OTHERGAME"));
  CHECK_EQ(card.free_blocks(), 14);
  // Reopening the saved image keeps the remaining file.
  psx::MemoryCard again;
  CHECK(again.open(card.path(), false));
  CHECK(again.find("BASCUS-94421S02-S01").has_value());
}

TEST(libcard_bios_file_calls) {
  const auto path = temp_card("bios.mcd");
  psyq::card_set_image(0, path);
  psyq::card_set_inserted(0, true);
  const long ioe = psyq::OpenEvent(psyq::kSwCard, psyq::kEvSpIoe, 0x2000, nullptr);
  psyq::EnableEvent(ioe);
  // Create one block (FCREAT | FASYNC, block count in bits 16+), write, read back.
  long fd = psyq::open("bu00:BASCUS-94421S02-S03", 0x18200);
  CHECK(fd >= 0);
  psyq::close(fd);
  fd = psyq::open("bu00:BASCUS-94421S02-S03", 0x8002);
  CHECK(fd >= 0);
  u8 frame[0x80];
  for (int i = 0; i < 0x80; ++i) frame[i] = static_cast<u8>(i);
  CHECK_EQ(psyq::lseek(fd, 0x200, 0), 0x200);
  CHECK_EQ(psyq::write(fd, frame, 0x80 + 0x7F), 0x80);  // the game passes length + 0x7f
  CHECK_EQ(psyq::TestEvent(ioe), 1);
  CHECK_EQ(psyq::TestEvent(ioe), 0);  // fires once
  psyq::close(fd);
  fd = psyq::open("bu00:BASCUS-94421S02-S03", 0x8001);
  u8 back[0x80] = {};
  psyq::lseek(fd, 0x200, 0);
  CHECK_EQ(psyq::read(fd, back, 0x80), 0x80);
  CHECK(std::memcmp(back, frame, 0x80) == 0);
  psyq::close(fd);
  psyq::DIRENTRY dir[2];
  CHECK(psyq::firstfile("bu00:BASCUS-94421S02-S*", &dir[0]) == &dir[0]);
  CHECK(std::strcmp(dir[0].name, "BASCUS-94421S02-S03") == 0);
  CHECK_EQ(dir[0].size, 0x2000);
  CHECK(psyq::nextfile(&dir[1]) == nullptr);
  CHECK(psyq::firstfile("bu00:NOPE*", &dir[0]) == nullptr);
  // No card: the check reports a timeout.
  const long timeout = psyq::OpenEvent(psyq::kSwCard, psyq::kEvSpTimeout, 0x2000, nullptr);
  psyq::EnableEvent(timeout);
  psyq::card_set_inserted(0, false);
  psyq::_card_info(0);
  CHECK_EQ(psyq::TestEvent(timeout), 1);
  psyq::card_set_inserted(0, true);
  psyq::CloseEvent(ioe);
  psyq::CloseEvent(timeout);
}
