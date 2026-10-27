#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "core/types.h"
#include "disc/archive.h"

// The tracker maps (port feature; decision D5 of docs/plans/trackers.md): renders the
// PopTracker pack's map images from the player's own disc into PopTracker's user-override
// folder, so the pack itself ships only original placeholder maps. The layout (image paths
// and sizes, each field map's rectangle, the check dots) is docs/ap/tracker_maps.json
// (tools/ap_tracker_maps.py; format in docs/ap/tracker_maps.md), compiled in. Nothing here
// runs during play: the command reads the disc, writes PNGs and exits.
namespace ss::tools {

// The layout this build carries (docs/ap/tracker_maps.json).
std::string_view tracker_layout_json();

// Where PopTracker reads the pack's user overrides (its own search order, src/poptracker.cpp):
// the first existing <home>/PopTracker/user-override/<uid> or
// <documents>/PopTracker/user-override/<uid>; else under <home>/PopTracker when that folder
// exists (PopTracker's default user folder), else under <documents>/PopTracker.
std::filesystem::path poptracker_override_dir(const std::filesystem::path& home,
                                              const std::filesystem::path& documents);
// The same for this user: home and documents as PopTracker finds them (Windows: the
// profile and Documents folders; elsewhere $HOME and $XDG_DOCUMENTS_DIR, else ~/Documents
// when it exists, else $HOME).
std::filesystem::path poptracker_override_dir();

// A field map's whole background in a fixed state (no scrolling window, animations at
// their first frame, colour records at their first step, no tint), RGBA8888 with red in
// the low byte.
struct Background {
  int width = 0, height = 0;
  std::vector<u32> pixels;
};
bool render_field_background(disc::Archive& archive, int map, Background* out, std::string* error);

struct TrackerRenderOptions {
  bool dots = false;  // also draw each check's dot (SS_TRACKER_DOTS=1; for checking the layout)
  std::vector<std::string> only;  // render only the images whose path contains one of these (SS_TRACKER_ONLY)
};
// The command (--render-tracker-maps [DIR]): every image of the layout as a PNG at its path
// under 'dir'; nothing else is written. Returns the process exit code.
int render_tracker_maps(disc::Archive& archive, const std::filesystem::path& dir, const TrackerRenderOptions& options);

}  // namespace ss::tools
