#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
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
// The pack's boards this build carries (tools/poptracker/boards.json, written by
// tools/ap_poptracker.py): for the Universal Tracker zip below.
std::string_view tracker_boards_json();

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

// What render_tracker_maps tells options.progress: before each image; then, when it writes
// the Universal Tracker zip (options.ut_zip), before each of the zip's files with kind "zip"
// (done / total count the zip's files, path is the zip, name the file) and once more when the
// zip is complete and in place (kind "zip", done == total, name empty); and last, once after
// all that (path empty, done == total: the images).
struct TrackerRenderProgress {
  int done = 0;      // images finished so far (written or failed)
  int total = 0;     // images in this run (after options.only)
  std::string path;  // the image about to be rendered, e.g. "images/rooms/18.png"
  std::string kind;  // its kind: "area", "world" or "room" ("zip": see above)
  std::string name;  // its area, world or room label: "Arlia Village", "Expel", "18 Arlia Village"
  int failed = 0;    // images so far that could not be rendered or written
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
  // When set, also the Universal Tracker zip at this path, after the images (empty: none).
  std::filesystem::path ut_zip;
};

// The Universal Tracker zip (docs/ap/tracker_maps.md "The Universal Tracker zip"). UT's map tab
// (so2.apworld's ut_map.py) takes its map data from so2.apworld and only the pictures from a
// .zip the player chooses once; it can't read PopTracker's user-override folder. So the render
// also writes a zip of the pictures at the pack's image paths: every image of the layout found
// in the folder (read back after this render, so also an earlier run's), plus what else UT's
// map tab shows from the zip, drawn by the port: the pack's boards (tools/poptracker/boards.json)
// and UT's player marker. Entries stored (PNGs are compressed already), sorted, with a fixed
// date: the same images give the same bytes (util/zip_writer.h). Written as '<zip>.part' and
// renamed when complete, so a stopped or failed run never leaves a partial zip (an earlier
// run's zip stays as it was).
constexpr char kTrackerUtZipName[] = "so2_shattered_stars_ut.zip";
// The zip's default place: beside the image folder 'dir', in its parent. For PopTracker's
// override folder <PopTracker>/user-override/so2_shattered_stars that is
// <PopTracker>/user-override/so2_shattered_stars_ut.zip: next to the rendered folder, in no
// folder PopTracker looks for packs in (only packs/) and not an override (PopTracker reads
// only user-override/<uid> folders: src/core/pack.cpp).
std::filesystem::path tracker_ut_zip_path(const std::filesystem::path& dir);
// The zip's files that are no disc render, as PNGs at their pack paths, sorted by path: the
// boards (at the pack's sizes, each cell at the pack's box with its label, drawn with the
// overlay font) and the player marker (64x64 RGBA). False and a reason when the built-in
// boards can't be read.
bool render_tracker_ut_extras(std::vector<std::pair<std::string, std::vector<u8>>>* out, std::string* error);
// The command (--render-tracker-maps [DIR]): every image of the layout as a PNG at its path
// under 'dir', and the Universal Tracker zip when options.ut_zip is set; nothing else is
// written. Returns the process exit code (0, or 1 when an image or the zip could not be
// rendered or written; the zip is still written when some images failed, from what the folder
// holds), or
// kTrackerRenderStopped (stopped: the images written by then stay; stopped before the zip
// was complete: no new zip). Uses only its arguments and its own locals (no global state), so
// it may run on a thread of its own as long as nothing else uses 'archive' meanwhile (the
// setup's button gives it a disc handle of its own).
int render_tracker_maps(disc::Archive& archive, const std::filesystem::path& dir, const TrackerRenderOptions& options);

}  // namespace ss::tools
