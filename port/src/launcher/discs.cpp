#include "launcher/discs.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <system_error>
#include <utility>

#include "disc/archive.h"
#include "disc/disc_image.h"
#include "launcher/config.h"
#include "launcher/process.h"
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

// "North American", "European", ... (the release of a disc the port knows).
std::string release_name(const DiscInfo& info) { return info.region != nullptr ? info.region->adjective : "unknown"; }

// "the North American (SCUS-94421 / SCUS-94422) and European (SCES-02159 / SCES-12159)
// releases": what this build plays.
std::string serial(const std::string& game_id);
std::string supported_releases() {
  std::vector<std::string> parts;
  for (const region::Profile& p : region::profiles())
    if (p.supported)
      parts.push_back(std::string(p.adjective) + " (" + serial(p.serial[0]) + " / " + serial(p.serial[1]) + ")");
  std::string out = "the ";
  for (size_t i = 0; i < parts.size(); ++i) {
    if (i > 0) out += i + 1 == parts.size() ? " and " : ", ";
    out += parts[i];
  }
  return out + (parts.size() == 1 ? " release" : " releases");
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
  // The release, by the serial (runtime/region.h); every release numbers its archive the
  // same, entry 6 only on disc 1 and entry 7 only on disc 2.
  int n = 0;
  info.region = so2 ? region::from_serial(info.game_id, &n) : nullptr;
  if (so2 && info.region != nullptr && !region::playable(*info.region)) info.kind = DiscKind::kUnsupportedRelease;
  else if (so2 && info.region != nullptr && n == 1 && archive.entry_size(6) != 0) info.kind = DiscKind::kDisc1;
  else if (so2 && info.region != nullptr && n == 2 && archive.entry_size(7) != 0) info.kind = DiscKind::kDisc2;
  else if (so2) info.kind = DiscKind::kOtherRelease;
  else info.kind = DiscKind::kNotSo2;
  return info;
}

int disc_number(const fs::path& image, const region::Profile** region) {
  const DiscInfo info = identify_disc(image);
  if (region != nullptr) *region = info.region;
  switch (info.kind) {
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
      return "Star Ocean: The Second Story, but a release Shattered Stars doesn't know" + id + ". It plays " +
             supported_releases() + ".";
    case DiscKind::kUnsupportedRelease:
      return "Star Ocean: The Second Story, " + std::string(info.region->adjective) + " release" + id +
             ": not supported yet. Shattered Stars plays " + supported_releases() + ".";
    case DiscKind::kDisc1:
      return wanted == 1 ? "Disc 1, " + release_name(info) + " release: OK" : "That is Disc 1. Please choose Disc 2 here.";
    case DiscKind::kDisc2:
      return wanted == 2 ? "Disc 2, " + release_name(info) + " release: OK" : "That is Disc 2. Please choose Disc 1 here.";
  }
  return {};
}

std::string describe_pair(const DiscInfo& disc1, const DiscInfo& disc2) {
  const bool both = (disc1.kind == DiscKind::kDisc1 || disc1.kind == DiscKind::kDisc2) &&
                    (disc2.kind == DiscKind::kDisc1 || disc2.kind == DiscKind::kDisc2);
  if (!both || disc1.region == nullptr || disc2.region == nullptr || disc1.region->id == disc2.region->id) return {};
  return "Disc 2 is the " + release_name(disc2) + " release and Disc 1 the " + release_name(disc1) +
         " one. Both discs must be the same release.";
}

FoundDiscs find_discs(const std::vector<fs::path>& folders) {
  FoundDiscs found;
  const region::Profile* disc1_region = nullptr;
  std::vector<std::pair<std::string, const region::Profile*>> disc2s;
  for (const fs::path& folder : folders) {
    std::error_code ec;
    if (!fs::is_directory(folder, ec)) continue;
    std::vector<fs::path> dirs = {folder};
    for (const auto& e : fs::directory_iterator(folder, ec))
      if (e.is_directory(ec)) dirs.push_back(e.path());
    std::sort(dirs.begin() + 1, dirs.end());
    for (const fs::path& dir : dirs) {
      for (const fs::path& image : images_in(dir)) {
        const region::Profile* r = nullptr;
        const int n = disc_number(image, &r);
        if (n == 1 && found.disc1.empty()) {
          found.disc1 = normal(image);
          disc1_region = r;
        }
        if (n == 2) disc2s.emplace_back(normal(image), r);
      }
    }
  }
  // Disc 2 of disc 1's release (without a disc 1: the first disc 2).
  for (const auto& [path, r] : disc2s) {
    if (found.disc1.empty() || r == disc1_region) {
      found.disc2 = path;
      break;
    }
  }
  return found;
}

std::vector<fs::path> disc_folders(const fs::path& config_dir) {
  std::vector<fs::path> out;
  if (const char* base = SDL_GetBasePath(); base && !in_app_bundle()) out.push_back(utf8_path(base) / "discs");
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
  const region::Profile* release = identify_disc(disc1).region;
  for (const fs::path& dir : dirs) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) continue;
    for (const fs::path& image : images_in(dir)) {
      const region::Profile* r = nullptr;
      if (disc_number(image, &r) == 2 && (release == nullptr || r == release)) return normal(image);
    }
  }
  return {};
}

}  // namespace ss::launcher
