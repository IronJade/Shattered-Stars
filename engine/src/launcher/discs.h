#pragma once

#include <filesystem>
#include <string>
#include <vector>

// Finding and identifying the player's disc images (port feature), shared by the game and
// the setup program. The game reads the discs at run time, so it needs disc 1 before even
// the logos can play; disc 2 is optional (without it the game shows the original's
// insert-disc screen where the story changes discs).
//
// The game looks in config.ini (or --disc1 / --disc2, SS_DISC1 / SS_DISC2), then in a
// `discs` folder next to the executable or next to config.ini, then opens the setup
// program (a windowed run only).
namespace ss::launcher {

enum class DiscKind {
  kNone,          // no path given
  kMissing,       // the file isn't there
  kArchive,       // a .zip / .7z / .rar: still packed
  kUnsupported,   // a format the game can't read (.chd, .pbp, ...)
  kUnreadable,    // not a CD image, or a .cue whose track file is missing
  kNotSo2,        // a readable CD image of something else
  kOtherRelease,  // Star Ocean: The Second Story, but not the North American release
  kDisc1,         // North American disc 1 (SCUS-94421)
  kDisc2,         // North American disc 2 (SCUS-94422)
};

struct DiscInfo {
  DiscKind kind = DiscKind::kNone;
  std::string game_id;  // from SYSTEM.CNF, e.g. "SCUS_944.21"; empty when unknown
  std::string detail;   // why an image can't be used (for messages), may be empty
};

// What an image is. Opens the file and reads a few sectors (fast, even on a .bin).
DiscInfo identify_disc(const std::filesystem::path& image);

// Which North American disc an image is: 1, 2, or 0 (anything else).
int disc_number(const std::filesystem::path& image);

// A disc 1 that the game can start from: the file exists and is disc 1.
inline bool is_disc1(const std::filesystem::path& image) { return disc_number(image) == 1; }

// One line for the player, e.g. "Disc 1, North American release: OK".
std::string describe(const DiscInfo& info, int wanted_disc);

struct FoundDiscs {
  std::string disc1, disc2;  // UTF-8 paths; empty when not found
};

// The first disc 1 and disc 2 images in these folders and their direct subfolders
// (.cue / .bin / .iso / .img; a .bin with a .cue beside it is taken through the .cue).
FoundDiscs find_discs(const std::vector<std::filesystem::path>& folders);

// The `discs` folders to search: next to the executable, then next to config.ini.
std::vector<std::filesystem::path> disc_folders(const std::filesystem::path& config_dir);

// Disc 2 near a known disc 1: in disc 1's folder, or in a sibling folder named like disc
// 1's with "2" for "1" ("... (Disc 1)" -> "... (Disc 2)"). Empty when there is none.
std::string find_disc2_near(const std::filesystem::path& disc1);

// The file name extensions the game reads, for file dialogs: "cue;bin;iso;img".
inline constexpr const char* kDiscImagePatterns = "cue;bin;iso;img";

}  // namespace ss::launcher
