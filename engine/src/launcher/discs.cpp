#include "launcher/discs.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <system_error>

#include "disc/archive.h"
#include "disc/disc_image.h"
#include "launcher/config.h"
#include "runtime/boot_exe.h"

namespace ss::launcher {
namespace {

namespace fs = std::filesystem;

// The archive table's first word, decrypted: the same on both discs; loader_is_disc
// (title_program.cpp) tests it to tell a Star Ocean 2 disc from any other.
constexpr u32 kTocWord0 = 0x34829314;

std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

std::string ext_of(const fs::path& p) { return lower(path_utf8(p.extension())); }

bool is_image(const fs::path& p) {
  const std::string ext = ext_of(p);
  return ext == ".cue" || ext == ".bin" || ext == ".iso" || ext == ".img";
}

// "SCUS_944.21" -> "SCUS-94421", the way the serial is printed on the case.
std::string serial(const std::string& game_id) {
  std::string s;
  for (char c : game_id) {
    if (c == '_') s += '-';
    else if (c != '.') s += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  }
  return s;
}

// The track file a cue sheet names (first FILE line), for a clearer message when it's missing.
std::string cue_track(const fs::path& cue) {
  std::ifstream in(cue);
  for (std::string line; std::getline(in, line);) {
    const auto pos = line.find("FILE");
    if (pos == std::string::npos) continue;
    const auto open = line.find('"', pos);
    const auto close = open == std::string::npos ? open : line.find('"', open + 1);
    if (close != std::string::npos) return line.substr(open + 1, close - open - 1);
  }
  return {};
}

// The images in one folder, .cue files first; a .bin or .img with a .cue of the same name
// is left to the .cue.
std::vector<fs::path> images_in(const fs::path& folder) {
  std::vector<fs::path> cues, others;
  std::error_code ec;
  for (const auto& e : fs::directory_iterator(folder, ec)) {
    if (!e.is_regular_file(ec) || !is_image(e.path())) continue;
    (ext_of(e.path()) == ".cue" ? cues : others).push_back(e.path());
  }
  std::sort(cues.begin(), cues.end());
  std::sort(others.begin(), others.end());
  for (const fs::path& p : others) {
    fs::path cue = p;
    cue.replace_extension(".cue");
    if (std::find(cues.begin(), cues.end(), cue) == cues.end()) cues.push_back(p);
  }
  return cues;
}

std::string normal(const fs::path& p) {
  std::error_code ec;
  const fs::path abs = fs::absolute(p, ec);
  return path_utf8((ec ? p : abs).lexically_normal());
}

}  // namespace

DiscInfo identify_disc(const fs::path& image) {
  DiscInfo info;
  if (image.empty()) return info;
  std::error_code ec;
  if (!fs::exists(image, ec)) {
    info.kind = DiscKind::kMissing;
    return info;
  }
  if (fs::is_directory(image, ec)) {
    info.kind = DiscKind::kUnreadable;
    info.detail = "That is a folder; choose the .cue file inside it.";
    return info;
  }
  const std::string ext = ext_of(image);
  if (ext == ".zip" || ext == ".7z" || ext == ".rar" || ext == ".gz" || ext == ".xz") {
    info.kind = DiscKind::kArchive;
    return info;
  }
  if (ext == ".chd" || ext == ".pbp" || ext == ".ecm" || ext == ".cso" || ext == ".mds" || ext == ".ccd" ||
      ext == ".m3u" || ext == ".toc") {
    info.kind = DiscKind::kUnsupported;
    info.detail = ext;
    return info;
  }
  if (ext == ".cue") {
    const std::string track = cue_track(image);
    if (track.empty()) {
      info.kind = DiscKind::kUnreadable;
      info.detail = "The .cue file names no track file.";
      return info;
    }
    if (!fs::exists(image.parent_path() / utf8_path(track), ec)) {
      info.kind = DiscKind::kUnreadable;
      info.detail = "The .cue file needs \"" + track + "\" in the same folder, and it is not there.";
      return info;
    }
  }
  std::string error;
  auto disc = disc::DiscImage::open(image, &error);
  if (!disc) {
    info.kind = DiscKind::kUnreadable;
    return info;
  }
  info.game_id = runtime::boot_exe_name(*disc);
  disc::Archive archive;
  const bool so2 = archive.open(*disc, &error) && archive.toc_word0() == kTocWord0;
  const std::string id = lower(info.game_id);
  if (so2 && id == "scus_944.21" && archive.entry_size(6) != 0) info.kind = DiscKind::kDisc1;
  else if (so2 && id == "scus_944.22" && archive.entry_size(7) != 0) info.kind = DiscKind::kDisc2;
  else if (so2) info.kind = DiscKind::kOtherRelease;
  else info.kind = DiscKind::kNotSo2;
  return info;
}

int disc_number(const fs::path& image) {
  switch (identify_disc(image).kind) {
    case DiscKind::kDisc1: return 1;
    case DiscKind::kDisc2: return 2;
    default: return 0;
  }
}

std::string describe(const DiscInfo& info, int wanted) {
  const std::string id = info.game_id.empty() ? std::string() : " (" + serial(info.game_id) + ")";
  switch (info.kind) {
    case DiscKind::kNone:
      return wanted == 1 ? "No disc chosen yet." : "Not chosen (optional).";
    case DiscKind::kMissing:
      return "File not found. Was it moved or renamed?";
    case DiscKind::kArchive:
      return "This is a compressed archive. Extract (unzip) it first, then choose the .cue file inside.";
    case DiscKind::kUnsupported:
      return "The game can't read " + info.detail + " images yet. Convert it to .cue / .bin first.";
    case DiscKind::kUnreadable:
      return info.detail.empty() ? "Not a CD image the game can read (use .cue / .bin, .iso or .img)." : info.detail;
    case DiscKind::kNotSo2:
      return "Not a Star Ocean: The Second Story disc image" + id + ".";
    case DiscKind::kOtherRelease:
      return "Star Ocean: The Second Story, but another release" + id +
             ". Shattered Stars needs the North American discs (SCUS-94421 / SCUS-94422).";
    case DiscKind::kDisc1:
      return wanted == 1 ? "Disc 1, North American release: OK" : "That is Disc 1. Please choose Disc 2 here.";
    case DiscKind::kDisc2:
      return wanted == 2 ? "Disc 2, North American release: OK" : "That is Disc 2. Please choose Disc 1 here.";
  }
  return {};
}

FoundDiscs find_discs(const std::vector<fs::path>& folders) {
  FoundDiscs found;
  for (const fs::path& folder : folders) {
    std::error_code ec;
    if (!fs::is_directory(folder, ec)) continue;
    std::vector<fs::path> dirs = {folder};
    for (const auto& e : fs::directory_iterator(folder, ec))
      if (e.is_directory(ec)) dirs.push_back(e.path());
    std::sort(dirs.begin() + 1, dirs.end());
    for (const fs::path& dir : dirs) {
      for (const fs::path& image : images_in(dir)) {
        if (!found.disc1.empty() && !found.disc2.empty()) return found;
        const int n = disc_number(image);
        if (n == 1 && found.disc1.empty()) found.disc1 = normal(image);
        if (n == 2 && found.disc2.empty()) found.disc2 = normal(image);
      }
    }
  }
  return found;
}

std::vector<fs::path> disc_folders(const fs::path& config_dir) {
  std::vector<fs::path> out;
  if (const char* base = SDL_GetBasePath()) out.push_back(utf8_path(base) / "discs");
  fs::path second = config_dir / "discs";
  if (out.empty() || out.front().lexically_normal() != second.lexically_normal()) out.push_back(second);
  return out;
}

std::string find_disc2_near(const fs::path& disc1) {
  std::vector<fs::path> dirs = {disc1.parent_path()};
  // "... (Disc 1)" -> "... (Disc 2)": the sibling folder named like disc 1's.
  std::string name = path_utf8(disc1.parent_path().filename());
  if (const auto at = name.rfind('1'); at != std::string::npos) {
    name[at] = '2';
    dirs.push_back(disc1.parent_path().parent_path() / utf8_path(name));
  }
  for (const fs::path& dir : dirs) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) continue;
    for (const fs::path& image : images_in(dir))
      if (disc_number(image) == 2) return normal(image);
  }
  return {};
}

}  // namespace ss::launcher
