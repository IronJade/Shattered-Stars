#include "game/ap/bench.h"

#include <cstring>
#include <vector>

#include "util/sha256.h"

// The bench store (bench.h) [partybuild agent].
namespace ss::game::ap {
namespace {

constexpr int kMembers = 12;  // party ids 1-12 (seed.h kCharacterCount)

s16 rd16s(const u8* p) {
  s16 v;
  std::memcpy(&v, p, 2);
  return v;
}

bool fail(std::string* error, const std::string& what) {
  if (error != nullptr) *error = what;
  return false;
}

int nibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

}  // namespace

int Bench::id(int i) const {
  if (i < 0 || i >= kBenchPlaces) return 0;
  const int v = rd16s(slot(i));
  return v >= 1 && v <= kMembers ? v : 0;
}

bool Bench::empty() const { return count() == 0; }

int Bench::count() const {
  int n = 0;
  for (int i = 0; i < kBenchPlaces; ++i) n += id(i) != 0;
  return n;
}

int Bench::find(int member) const {
  if (member < 1 || member > kMembers) return -1;
  for (int i = 0; i < kBenchPlaces; ++i)
    if (id(i) == member) return i;
  return -1;
}

int Bench::free_index() const {
  for (int i = 0; i < kBenchPlaces; ++i)
    if (id(i) == 0) return i;
  return -1;
}

void Bench::put(int i, const u8* s, const u8* r) {
  if (i < 0 || i >= kBenchPlaces) return;
  std::memcpy(slot(i), s, kSlotBytes);
  std::memcpy(record(i), r, kRecordBytes);
}

void Bench::clear(int i) {
  if (i >= 0 && i < kBenchPlaces) places[static_cast<size_t>(i)].fill(0);
}

Json bench_json(const Bench& b) {
  static const char* const kHex = "0123456789abcdef";
  Json a = Json::array();
  for (int i = 0; i < kBenchPlaces; ++i) {
    if (b.id(i) == 0) continue;
    std::string hex;
    hex.reserve(kMemberBytes * 2);
    for (const u8 v : b.places[static_cast<size_t>(i)]) {
      hex += kHex[v >> 4];
      hex += kHex[v & 15];
    }
    Json e = Json::object();
    e.set("place", Json::number(static_cast<int64_t>(kBenchFirst + i)));
    e.set("bytes", Json::string(hex));
    a.push(e);
  }
  return a;
}

bool bench_from_json(const Json& a, Bench* b, std::string* error) {
  Bench r;
  if (a.is_null()) {
    *b = r;
    return true;
  }
  if (!a.is_array()) return fail(error, "bench: an array expected");
  bool taken[kBenchPlaces] = {};
  for (size_t k = 0; k < a.size(); ++k) {
    const Json& e = a[k];
    const int place = static_cast<int>(e["place"].as_int(-1));
    if (place < kBenchFirst || place >= kBenchFirst + kBenchPlaces || taken[place - kBenchFirst])
      return fail(error, "bench: a place 8-11 (each once) expected");
    const std::string& hex = e["bytes"].as_string();
    if (hex.size() != kMemberBytes * 2) return fail(error, "bench: 0x130 bytes expected");
    const int i = place - kBenchFirst;
    u8* out = r.places[static_cast<size_t>(i)].data();
    for (size_t n = 0; n < kMemberBytes; ++n) {
      const int hi = nibble(hex[2 * n]), lo = nibble(hex[2 * n + 1]);
      if (hi < 0 || lo < 0) return fail(error, "bench: 0x130 bytes expected");
      out[n] = static_cast<u8>(hi << 4 | lo);
    }
    const int id = rd16s(out);
    if (id < 1 || id > kMembers) return fail(error, "bench: a member (party id 1-12) expected");
    for (int j = 0; j < kBenchPlaces; ++j)
      if (j != i && r.id(j) == id) return fail(error, "bench: a member benched twice");
    taken[i] = true;
  }
  *b = r;
  return true;
}

std::string bench_digest(const u8* file, size_t length, const Bench& b, const std::vector<BenchLoan>& lent) {
  // The save's bytes as util::save_digest takes them: its first N, N = the u16 at +0x21a.
  size_t n = file != nullptr && length >= 0x21C ? static_cast<size_t>(file[0x21A] | file[0x21B] << 8) : 0;
  if (n > length) n = length;
  if (n > 0x2000) n = 0x2000;
  std::vector<u8> data(file, file + n);
  for (const auto& place : b.places) data.insert(data.end(), place.begin(), place.end());
  // The loans (B4) [partybuild agent]: none adds nothing (a bench's digest as before).
  for (const BenchLoan& l : lent) {
    data.push_back(static_cast<u8>(l.id));
    data.push_back(static_cast<u8>(l.place));
  }
  return "sha256:" + util::sha256_hex(data.data(), data.size());
}

}  // namespace ss::game::ap
