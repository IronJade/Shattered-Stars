#pragma once

#include <filesystem>
#include <functional>
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

// What render_tracker_maps tells options.progress: before each image, and once after the
// last one (path empty, done == total).
struct TrackerRenderProgress {
  int done = 0;      // images finished so far (written or failed)
  int total = 0;     // images in this run (after options.only)
  std::string path;  // the image about to be rendered, e.g. "images/rooms/18.png"
  std::string kind;  // its kind: "area", "world" or "room"
  std::string name;  // its area, world or room label: "Arlia Village", "Expel", "18 Arlia Village"
};
// render_tracker_maps' result when options.progress asked it to stop (never the command's:
// the command sets no progress function).
constexpr int kTrackerRenderStopped = 2;

struct TrackerRenderOptions {
  bool dots = false;  // also draw each check's dot (SS_TRACKER_DOTS=1; for checking the layout)
  std::vector<std::string> only;  // render only the images whose path contains one of these (SS_TRACKER_ONLY)
  // Called on the rendering thread (see TrackerRenderProgress); returning false stops the
  // render before the next image, and render_tracker_maps returns kTrackerRenderStopped. The
  // images written until then are whole (each is written under a temporary name and renamed).
  std::function<bool(const TrackerRenderProgress&)> progress;
};
// The command (--render-tracker-maps [DIR]): every image of the layout as a PNG at its path
// under 'dir'; nothing else is written. Returns the process exit code (0, or 1 when an image
// could not be rendered or written), or kTrackerRenderStopped. Uses only its arguments and
// its own locals (no global state), so it may run on a thread of its own as long as nothing
// else uses 'archive' meanwhile (the setup's button gives it a disc handle of its own).
int render_tracker_maps(disc::Archive& archive, const std::filesystem::path& dir, const TrackerRenderOptions& options);

}  // namespace ss::tools
