#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "runtime/region.h"

// Finding and identifying the player's disc images (port feature), shared by the game and
// the setup program. The game reads the discs at run time, so it needs disc 1 before even
// the logos can play; disc 2 is optional (without it the game shows the original's
// insert-disc screen where the story changes discs).
//
// The game looks in config.ini (or --disc1 / --disc2, SS_DISC1 / SS_DISC2), then in a
// `discs` folder next to the executable or next to config.ini, then opens the setup
// program (a windowed run only). The releases it plays are listed in runtime/region.h
// (North America, Europe, ...); both discs must be the same release.
namespace ss::launcher {

enum class DiscKind {
  kNone,          // no path given
  kMissing,       // the file isn't there
  kArchive,       // a .zip / .7z / .rar: still packed
  kUnsupported,   // a format the game can't read (.chd, .pbp, ...)
  kUnreadable,    // not a CD image, or a .cue whose track file is missing
  kNotSo2,        // a readable CD image of something else
  kOtherRelease,  // Star Ocean: The Second Story, but a release the port doesn't know
  kUnsupportedRelease,  // a release the port knows but can't play yet (Japan; 'region' says)
  kDisc1,         // disc 1 of a release the port plays ('region': NA SCUS-94421, Europe SCES-02159, ...)
  kDisc2,         // disc 2 of it (SCUS-94422, SCES-12159, ...)
};

struct DiscInfo {
  DiscKind kind = DiscKind::kNone;
  std::string game_id;  // from SYSTEM.CNF, e.g. "SCUS_944.21"; empty when unknown
  std::string detail;   // why an image can't be used (for messages), may be empty
  const region::Profile* region = nullptr;  // the release (runtime/region.h), when known
};

// What an image is. Opens the file and reads a few sectors (fast, even on a .bin).
DiscInfo identify_disc(const std::filesystem::path& image);

// Which disc of a release the port plays an image is: 1, 2, or 0 (anything else).
// 'region' (optional) gets the release.
int disc_number(const std::filesystem::path& image, const region::Profile** region = nullptr);

// A disc 1 that the game can start from: the file exists and is disc 1.
inline bool is_disc1(const std::filesystem::path& image) { return disc_number(image) == 1; }

// One line for the player, e.g. "Disc 1, North American release: OK".
std::string describe(const DiscInfo& info, int wanted_disc);

// Disc 2 must be the same release as disc 1 (their archives and programs belong together).
// Empty when they match (or one is missing), else one line for the player.
std::string describe_pair(const DiscInfo& disc1, const DiscInfo& disc2);

struct FoundDiscs {
  std::string disc1, disc2;  // UTF-8 paths; empty when not found
};

// The first disc 1 image in these folders and their direct subfolders (.cue / .bin / .iso /
// .img; a .bin with a .cue beside it is taken through the .cue), and the first disc 2 of
// the same release (without a disc 1: the first disc 2).
FoundDiscs find_discs(const std::vector<std::filesystem::path>& folders);

// The `discs` folders to search: next to the executable, then next to config.ini.
std::vector<std::filesystem::path> disc_folders(const std::filesystem::path& config_dir);

// Disc 2 (of disc 1's release) near a known disc 1: in disc 1's folder, or in a sibling
// folder named like disc 1's with "2" for "1" ("... (Disc 1)" -> "... (Disc 2)"). Empty when
// there is none.
std::string find_disc2_near(const std::filesystem::path& disc1);

// The file name extensions the game reads, for file dialogs: "cue;bin;iso;img".
inline constexpr const char* kDiscImagePatterns = "cue;bin;iso;img";

}  // namespace ss::launcher
