#include "setup/tracker_job.h"

#include <exception>
#include <memory>
#include <utility>

#include "disc/archive.h"
#include "disc/disc_image.h"
#include "util/utf8.h"

namespace ss::setup {

TrackerRenderJob::~TrackerRenderJob() { stop(); }

bool TrackerRenderJob::start(const std::string& disc1, const std::filesystem::path& dir,
                             tools::TrackerRenderOptions options) {
  {
    std::lock_guard lock(mutex_);
    if (status_.running) return false;
  }
  if (worker_.joinable()) worker_.join();  // the last one, finished but not yet collected
  stop_ = false;
  {
    std::lock_guard lock(mutex_);
    status_ = Status{};
    status_.running = true;
  }
  worker_ = std::thread(&TrackerRenderJob::run, this, disc1, dir, std::move(options));
  return true;
}

void TrackerRenderJob::stop() {
  stop_ = true;
  if (worker_.joinable()) worker_.join();
}

TrackerRenderJob::Status TrackerRenderJob::status() {
  Status s;
  {
    std::lock_guard lock(mutex_);
    s = status_;
  }
  // Finished: the worker has nothing left but to return, so this join is immediate.
  if (!s.running && worker_.joinable()) worker_.join();
  return s;
}

// The worker thread. Everything it uses is its own (the arguments are copies) except
// status_ (under mutex_) and stop_.
void TrackerRenderJob::run(std::string disc1, std::filesystem::path dir, tools::TrackerRenderOptions options) {
  Outcome outcome = Outcome::kFailed;
  std::string detail;
  try {
    std::string error;
    const std::unique_ptr<disc::DiscImage> disc = disc::DiscImage::open(util::utf8_path(disc1), &error);
    disc::Archive archive;
    if (!disc || !archive.open(*disc, &error)) {
      outcome = Outcome::kNoDisc;
      detail = error;
    } else {
      options.progress = [this](const tools::TrackerRenderProgress& p) {
        std::lock_guard lock(mutex_);
        if (p.kind == "zip") {  // the Universal Tracker zip's files: the images are all finished
          status_.done = status_.total;
          status_.zip_done = p.done;
          status_.zip_total = p.total;
          status_.zip_written = p.total > 0 && p.done == p.total;  // the call once it is in place
        } else {
          status_.done = p.done;
          status_.total = p.total;
        }
        status_.failed = p.failed;
        status_.kind = p.kind;
        status_.name = p.name;
        return !stop_.load();
      };
      const int rc = tools::render_tracker_maps(archive, dir, options);
      outcome = rc == 0 ? Outcome::kDone : rc == tools::kTrackerRenderStopped ? Outcome::kStopped : Outcome::kErrors;
    }
  } catch (const std::exception& e) {  // a thread must not let one out (std::terminate)
    outcome = Outcome::kFailed;
    detail = e.what();
  } catch (...) {
    outcome = Outcome::kFailed;
    detail = "an unexpected error";
  }
  std::lock_guard lock(mutex_);
  status_.running = false;
  status_.outcome = outcome;
  status_.detail = std::move(detail);
}

}  // namespace ss::setup
