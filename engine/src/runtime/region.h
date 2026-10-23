#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/types.h"

// Region profiles (port feature; docs/notes/regions.md). One build plays every supported
// release of the game; the disc says which one it is (its SYSTEM.CNF boot file, the serial)
// and selects a profile: the release's name, its save product code, its text system, and
// the address maps of its program images.
//
// The ported code reads the original's constant tables from the program images at their
// North American addresses. Another release holds the same tables at other addresses
// (Europe, France and Germany relinked NA's code with a small PAL patch set, so their
// images shift in steps). tools/region_map.py derives, from the discs, where each NA byte
// of each image lives in the other release and writes the facts (offsets, deltas, load
// addresses, hashes; no game bytes) to engine/data/regions/<key>.inc. The readers
// (program_data_at, boot_exe_data, the battle overlays, the party / enemy / spell modules)
// translate through those maps, so every caller keeps using NA addresses; on the NA discs
// nothing is translated.
//
// Pointer words in a region's image (a table of string or function addresses) hold region
// addresses. prepare() rewrites the ones the tool listed to their NA values when the image
// is loaded, so a pointer the port reads and follows stays in NA space.
namespace ss::region {

enum class Id : u8 { kNorthAmerica, kEurope, kFrance, kGermany, kJapan };

// How a release encodes and lays out its text (game/field/message*, title/text*). Europe
// uses NA's; France and Germany have a 132-glyph Latin font each, with their own glyph
// numbers, wrapping classes and name entry (docs/notes/regions_text.md); Japan kana/kanji.
enum class Text : u8 { kNorthAmerica, kFrench, kGerman, kJapanese };

struct Profile {
  Id id;
  const char* key;        // "na", "eu", "fr", "de", "jp"
  const char* name;       // "North America", "Europe", ...
  const char* adjective;  // "North American", "European", ...
  const char* serial[2];  // SYSTEM.CNF boot files of disc 1 and disc 2: "SCUS_944.21", ...
  const char* product;    // the memory-card product code: "BASCUS-94421", ...
  Text text;
  bool supported;         // false: recognised, but the port can't play it yet
};

// Every release the port recognises, NA first.
std::span<const Profile> profiles();
const Profile& profile(Id id);

// The release whose disc 'disc' (1 or 2) boots 'game_id' (SYSTEM.CNF's boot file, any
// case, e.g. "SCES_021.59"); null if the serial is no release of the game.
const Profile* from_serial(std::string_view game_id, int* disc = nullptr);

// The release being played: North America until select() (the game calls it once disc 1
// is open, from its serial).
const Profile& current();
void select(Id id);
inline bool is_na() { return current().id == Id::kNorthAmerica; }

// ---- Image maps ------------------------------------------------------------------------

struct Segment {  // NA bytes [na_off, na_off + size) are the region's [na_off + delta, ...)
  u32 na_off;
  u32 size;
  s32 delta;
};
struct Pointers {  // 'count' pointer words from NA offset na_off, addressing image 'target'
  u32 na_off;
  u32 count;
  u16 target;
};
enum class ImageKind : u8 { kBoot, kEntry, kEnemy, kSpell };
struct ImageMap {
  ImageKind kind;
  s32 entry;  // archive entry (kEntry), -1 otherwise
  s32 block;  // SLZ block of the entry
  u32 na_base, rg_base;
  u32 na_size, rg_size;
  u64 na_fnv;          // FNV-1a 64 of NA's image (a module's identity in the port's tables)
  u64 rg_fnv;          // ... of the region's: the disc must hold exactly this image
  u64 rg_module_hash;  // module_hash() of the region's image
  const Segment* seg;
  u32 nseg;
  const Pointers* ptr;
  u32 nptr;
};

// The current region's map of archive entry 'entry' block 'block' (the boot executable:
// entry -1), or null: on NA (nothing to translate) or when the region has no map for it.
const ImageMap* entry_map(int entry, int block = 0);
const ImageMap* boot_map();
// The current region's map of an enemy-code / spell-code module, by the region block's
// bytes (exact hash, then module_hash); null on NA or when no map matches.
const ImageMap* module_map(ImageKind kind, std::span<const u8> region_block);

// The NA module an enemy-code / spell-code block is, as the key of the port's module
// tables (the FNV-1a 64 of NA's block): found by the block's normalised words, which are
// the same wherever a release linked the module (engine/data/regions/na_modules.inc), else
// by the current region's map (a module whose code the release changed); 0 when unknown.
// '*map' (optional) gets the region's map of the block (null on NA).
u64 module_identity(ImageKind kind, std::span<const u8> block, const ImageMap** map = nullptr);

// True when the current region has address maps (NA needs none).
bool has_maps();

// Makes a freshly loaded region image readable at NA addresses: checks it is the image the
// map was made from (else logs and returns false: the reads then fail instead of reading
// wrong addresses) and rewrites its pointer words to NA values.
bool prepare(const ImageMap* map, std::vector<u8>& image);

// NA byte offset -> offset in the region's image; -1 when the NA byte has no counterpart.
s64 translate(const ImageMap* map, u32 na_off);
// Region address (a pointer value in the region's image) -> NA address; 0 when unmapped.
u32 to_na_address(const ImageMap* map, u32 region_address);

// A byte of 'image' (as loaded: the region's bytes for a map, else NA's) at NA address
// 'address' of an image NA loads at 'na_base'; null outside it. 'log_key' names the image
// in the SS_REGION_READ_LOG record ("title", "7bd", "enemy.<fnv>", ...).
const u8* image_byte(const ImageMap* map, const std::vector<u8>& image, u32 na_base, u32 address,
                     const char* log_key);

// The tools' image key of an archive entry / block ("title", "field", "c1d", "7c0.1", ...).
std::string entry_key(int entry, int block = 0);

// FNV-1a 64 of a module's normalised words (tools/region_map.py module_hash): equal for the
// same code linked at other addresses.
u64 module_hash(std::span<const u8> block);
u64 fnv1a64(std::span<const u8> bytes);

// SS_REGION_READ_LOG=<path>: every distinct (image, NA address) the readers are asked for,
// appended to the file at exit (a path ending in '/': a new file in that folder per
// process). tools/region_map.py --reads checks each is mapped.
void log_read(const std::string& key, u32 address);

}  // namespace ss::region
