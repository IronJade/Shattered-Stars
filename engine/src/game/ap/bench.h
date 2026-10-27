#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <vector>

#include "core/types.h"
#include "game/ap/json.h"

// Archipelago (port feature) [partybuild agent]: the bench (docs/handoff/bench.md, the user's
// answers B1-B8; docs/ap/seed_format.md "The bench").
//
// With options.bench (open world with party_rules "open", and the hub) the party has 12 places:
// the game's 8 slots (0-3 the front, 4-7 the reserve) and four the port keeps, places 8-11. A
// member is what the game's own Party Order swap moves (party_swap_members 0x8007f0bc): the party
// slot (0x60 bytes, handoff 3) and the character record (0xd0 bytes, handoff 4). A benched member
// is those 0x130 bytes in this store, as they were when the member left the 8 (equipment, skills,
// killer moves and their use counts, talents, tactics, level, EXP, HP / MP, status). The game's
// blocks stay 8 places and no original code sees the store: a benched member is out of the party
// for every script (no Private Action, relation points, EXP, ending) until a Party Order swap
// fills a game slot from the store. The store lives in the save's sidecar (`bench`, guarded by
// `bench_hash`); the memory card stays byte-compatible (its 8 slots are a valid party without
// the benched members).
namespace ss::game::ap {

constexpr int kBenchFirst = 8;   // the first bench place (after the game's slots 0-7)
constexpr int kBenchPlaces = 4;  // places 8-11
constexpr size_t kSlotBytes = 0x60, kRecordBytes = 0xD0, kMemberBytes = kSlotBytes + kRecordBytes;

struct Bench {
  // Place kBenchFirst + i: the member's slot (0x60) then record (0xd0); an empty place is all
  // zero (the slot's +0, the party id, 0).
  std::array<std::array<u8, kMemberBytes>, kBenchPlaces> places{};

  int id(int i) const;     // the party id at index i (0-3: places 8-11), 0 empty
  bool empty() const;      // nobody on the bench
  int count() const;
  int find(int id) const;  // the index holding member 'id' (1-12), -1 none
  int free_index() const;  // the first empty index, -1 full
  u8* slot(int i) { return places[static_cast<size_t>(i)].data(); }
  u8* record(int i) { return places[static_cast<size_t>(i)].data() + kSlotBytes; }
  const u8* slot(int i) const { return places[static_cast<size_t>(i)].data(); }
  const u8* record(int i) const { return places[static_cast<size_t>(i)].data() + kSlotBytes; }
  void put(int i, const u8* slot, const u8* record);  // a member's bytes in (copies)
  void clear(int i);                                  // empty again
  bool operator==(const Bench&) const = default;
};

// B4: a benched member lent to the 8 for a fight the story makes them make alone (a split
// window needing them); the place stays theirs (empty, never given to a join) until they go back.
struct BenchLoan {
  int id = 0;     // party id
  int place = 0;  // the bench place kept for them (8-11)
  bool operator==(const BenchLoan&) const = default;
};

// The sidecar's "bench": the occupied places only, each `{"place": 8-11, "bytes": hex of the
// 0x130 bytes}` (the stash's format).
Json bench_json(const Bench& b);
// Reads it back (null or absent: empty). False with 'error' on anything else than an array of
// such entries: a place outside 8-11 or given twice, other than 0x130 bytes, a character that
// is no member (id 1-12; a set-aside member, a negative id, is never benched) or benched twice.
bool bench_from_json(const Json& a, Bench* b, std::string* error);
// The sidecar's "bench_hash": "sha256:" and the SHA-256 of the save's bytes as util::save_digest
// takes them (its first N, N = the save's length) followed by the four places' 0x130 bytes (an
// empty place: zeros), then, with a loan out, each loan's id and place (one byte each). A bench
// or loan edited, or moved to another save, no longer matches.
std::string bench_digest(const u8* file, size_t length, const Bench& b, const std::vector<BenchLoan>& lent = {});

}  // namespace ss::game::ap
