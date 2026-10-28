#pragma once

#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>

#include "tools/tracker_maps.h"

// The setup program's "Render tracker maps for PopTracker" button (port feature;
// docs/trackers/tracker_maps.md "The setup button"): tools::render_tracker_maps on a thread of its
// own, so the window keeps drawing, and shows the progress, during the ~20 s render (and the
// Universal Tracker zip after it, when options.ut_zip is set).
//
// Thread safety: the worker opens a disc::DiscImage and disc::Archive of its own (from a copy
// of the path; the window's thread reads the same file through its own handles, for the disc
// check and the logo) and renders with its own copy of the layout; render_tracker_maps keeps
// no global state. The worker and the window's thread share only this object's Status (under
// its mutex) and the stop flag. The folder is resolved by the caller, on its own thread
// (tools::poptracker_override_dir reads the environment). All members are called from one
// thread, the owner's.
namespace ss::setup {

class TrackerRenderJob {
 public:
  enum class Outcome {
    kNone,     // not finished (or never started)
    kDone,     // every image written
    kErrors,   // finished, but some images could not be rendered or written
    kStopped,  // stop() (or the window closing) ended it before the last image
    kNoDisc,   // Disc 1 could not be opened (detail: why)
    kFailed,   // an exception ended it (detail: what)
  };
  struct Status {
    bool running = false;     // started and not finished
    int done = 0, total = 0;  // images finished / in this render (total 0 until it starts)
    int failed = 0;           // images that could not be rendered or written
    std::string kind, name;   // the image in progress (tools::TrackerRenderProgress), or kind
                              // "zip" and the file while the Universal Tracker zip is written
    int zip_done = 0, zip_total = 0;  // the zip's files written / in it (options.ut_zip)
    bool zip_written = false;         // the zip is complete and in place
    Outcome outcome = Outcome::kNone;  // set when it finishes, until the next start()
    std::string detail;
  };

  TrackerRenderJob() = default;
  TrackerRenderJob(const TrackerRenderJob&) = delete;
  TrackerRenderJob& operator=(const TrackerRenderJob&) = delete;
  ~TrackerRenderJob();  // stops a running render (after its current image) and waits for it

  // Renders Disc 1 ('disc1', a UTF-8 path) into 'dir' on a new thread. False when a render is
  // already running. options.progress is the job's own (anything given is replaced).
  bool start(const std::string& disc1, const std::filesystem::path& dir, tools::TrackerRenderOptions options = {});
  // Asks a running render to stop before its next image, and waits until it has.
  void stop();
  // A copy of the state; a worker that has finished is joined here.
  Status status();

 private:
  void run(std::string disc1, std::filesystem::path dir, tools::TrackerRenderOptions options);

  std::mutex mutex_;
  Status status_;  // under mutex_
  std::atomic<bool> stop_{false};
  std::thread worker_;
};

}  // namespace ss::setup
