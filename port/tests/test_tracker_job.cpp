// The tracker render's progress and stop (tools/tracker_maps.h TrackerRenderOptions::progress)
// and the setup button's render thread (setup/tracker_job.h) [setupthread]. Without a disc:
// an archive with no disc (every map fails, so area images come out as their labels on black)
// and a disc path that doesn't exist; with the player's disc (SS_DISC1; skipped without it):
// real images, the same bytes stopped or not, a stop mid-render and the job's destructor.

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "disc/archive.h"
#include "setup/tracker_job.h"
#include "test.h"
#include "tools/tracker_maps.h"

using namespace ss;
namespace fs = std::filesystem;

namespace {

using Clock = std::chrono::steady_clock;

// A temp directory of its own per case, directly under the system temp dir (not nested under a
// shared parent this file creates) so each test's own 'fs::remove_all(dir)' at the end removes
// the whole thing, not just a leaf under a 'tracker_job_test/' folder left behind in whatever
// directory ss_game_tests happens to run from.
fs::path scratch(const char* name) {
  const fs::path p = fs::temp_directory_path() / (std::string("ss_tracker_job_test_") + name);
  std::error_code ec;
  fs::remove_all(p, ec);
  fs::create_directories(p);
  return p;
}

// Every file under 'root', relative to it.
std::set<std::string> files_under(const fs::path& root) {
  std::set<std::string> out;
  std::error_code ec;
  for (fs::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec))
    if (it->is_regular_file()) out.insert(fs::relative(it->path(), root).generic_string());
  return out;
}

std::vector<char> read_file(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  return std::vector<char>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

bool has_part_file(const std::set<std::string>& files) {
  for (const std::string& f : files)
    if (f.size() >= 5 && f.compare(f.size() - 5, 5, ".part") == 0) return true;
  return false;
}

struct Seen {
  int done = 0, total = 0;
  std::string path, kind, name;
};

// Waits (polling, as the setup window does each frame) until 'until' holds or the deadline.
template <class F>
setup::TrackerRenderJob::Status poll(setup::TrackerRenderJob& job, F until, double seconds, int* polls = nullptr) {
  const auto deadline = Clock::now() + std::chrono::milliseconds(static_cast<int>(seconds * 1000));
  setup::TrackerRenderJob::Status st = job.status();
  int prev_done = 0;
  while (!until(st) && Clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    st = job.status();
    CHECK(st.done >= prev_done);  // the count never goes back
    prev_done = st.done;
    if (polls) ++*polls;
  }
  return st;
}

const char* disc1() {
  const char* path = std::getenv("SS_DISC1");
  if (!path || !*path) {
    std::printf("  (skipped: SS_DISC1 not set)\n");
    return nullptr;
  }
  return path;
}

}  // namespace

// The progress calls and a stop, without a disc: the area images still come out (their maps
// fail, drawn black under their labels), the world images fail.
TEST(tracker_progress_no_disc) {
  disc::Archive none;  // never opened: every map read fails
  const fs::path dir = scratch("nodisc");
  tools::TrackerRenderOptions options;
  options.only = {"images/maps/"};
  std::vector<Seen> seen;
  options.progress = [&](const tools::TrackerRenderProgress& p) {
    seen.push_back({p.done, p.total, p.path, p.kind, p.name});
    return true;
  };
  CHECK_EQ(tools::render_tracker_maps(none, dir / "all", options), 1);  // failures: exit 1, as before
  CHECK(seen.size() >= 3);
  if (seen.size() < 3) return;
  const int total = seen[0].total;
  CHECK(total > 2);
  CHECK_EQ(static_cast<int>(seen.size()), total + 1);  // before each image, then once at the end
  int areas = 0, worlds = 0;
  for (int i = 0; i < static_cast<int>(seen.size()); ++i) {
    CHECK_EQ(seen[i].done, i);
    CHECK_EQ(seen[i].total, total);
    if (i == total) {
      CHECK(seen[i].path.empty());
      continue;
    }
    CHECK(seen[i].path.rfind("images/maps/", 0) == 0);
    CHECK(!seen[i].name.empty());
    areas += seen[i].kind == "area";
    worlds += seen[i].kind == "world";
  }
  CHECK_EQ(areas + worlds, total);
  CHECK(worlds >= 1);
  const std::set<std::string> all = files_under(dir / "all");
  CHECK_EQ(static_cast<int>(all.size()), areas);  // the world images failed, the areas did not
  CHECK(!has_part_file(all));

  // Stopped before the fourth image: three images, whole, the same bytes; no temporary file.
  std::vector<Seen> seen2;
  options.progress = [&](const tools::TrackerRenderProgress& p) {
    seen2.push_back({p.done, p.total, p.path, p.kind, p.name});
    return p.done < 3;
  };
  CHECK_EQ(tools::render_tracker_maps(none, dir / "stopped", options), tools::kTrackerRenderStopped);
  CHECK_EQ(static_cast<int>(seen2.size()), 4);  // asked before images 0..3, no call at the end
  const std::set<std::string> three = files_under(dir / "stopped");
  CHECK_EQ(static_cast<int>(three.size()), 3);
  CHECK(!has_part_file(three));
  for (const std::string& f : three) {
    CHECK(all.count(f) == 1);
    CHECK(read_file(dir / "stopped" / f) == read_file(dir / "all" / f));
  }
  // A stop before the first image writes nothing at all.
  options.progress = [](const tools::TrackerRenderProgress&) { return false; };
  CHECK_EQ(tools::render_tracker_maps(none, dir / "none", options), tools::kTrackerRenderStopped);
  CHECK(files_under(dir / "none").empty());
  fs::remove_all(dir);
}

// The job: a disc that can't be opened ends as kNoDisc with the reason, on its thread.
TEST(tracker_job_no_disc) {
  const fs::path dir = scratch("jobnodisc");
  setup::TrackerRenderJob job;
  CHECK(job.start((dir / "no-such-disc.cue").string(), dir / "out"));
  const auto st = poll(job, [](const setup::TrackerRenderJob::Status& s) { return !s.running; }, 30);
  CHECK(!st.running);
  CHECK(st.outcome == setup::TrackerRenderJob::Outcome::kNoDisc);
  CHECK(!st.detail.empty());
  CHECK(files_under(dir).empty());
  // It can start again once finished (the old thread is collected).
  CHECK(job.start((dir / "no-such-disc.cue").string(), dir / "out"));
  job.stop();
  CHECK(job.status().outcome == setup::TrackerRenderJob::Outcome::kNoDisc);
  fs::remove_all(dir);
}

// With the disc: progress names, the same bytes stopped or not, a stop mid-render, and the
// job's own thread run to the end, stopped, and stopped by its destructor.
TEST(tracker_progress_disc) {
  const char* path = disc1();
  if (!path) return;
  const fs::path dir = scratch("disc");
  // Synchronous, four rooms and an area image.
  {
    std::string error;
    auto image = disc::DiscImage::open(path, &error);
    disc::Archive archive;
    CHECK(image && archive.open(*image, &error));
    if (!image) return;
    tools::TrackerRenderOptions options;
    options.only = {"renas_room", "rooms/18.", "rooms/24.", "rooms/25.", "rooms/38."};
    std::vector<Seen> seen;
    options.progress = [&](const tools::TrackerRenderProgress& p) {
      seen.push_back({p.done, p.total, p.path, p.kind, p.name});
      return true;
    };
    CHECK_EQ(tools::render_tracker_maps(archive, dir / "all", options), 0);
    CHECK_EQ(static_cast<int>(seen.size()), 6);
    if (seen.size() == 6) {
      CHECK_EQ(seen[0].total, 5);
      CHECK(seen[0].kind == "area");  // the area images come before the rooms
      CHECK(seen[1].kind == "room");
      CHECK(seen[1].path == "images/rooms/18.png");
      CHECK(seen[1].name.rfind("18 ", 0) == 0);  // the room's label: its number and name
      CHECK_EQ(seen[5].done, 5);
    }
    const std::set<std::string> all = files_under(dir / "all");
    CHECK_EQ(static_cast<int>(all.size()), 5);
    options.progress = [](const tools::TrackerRenderProgress& p) { return p.done < 2; };
    CHECK_EQ(tools::render_tracker_maps(archive, dir / "two", options), tools::kTrackerRenderStopped);
    const std::set<std::string> two = files_under(dir / "two");
    CHECK_EQ(static_cast<int>(two.size()), 2);
    for (const std::string& f : two) CHECK(read_file(dir / "two" / f) == read_file(dir / "all" / f));
  }
  using Outcome = setup::TrackerRenderJob::Outcome;
  // The job, to the end: the same bytes as the synchronous render.
  {
    setup::TrackerRenderJob job;
    tools::TrackerRenderOptions options;
    options.only = {"renas_room", "rooms/18.", "rooms/24.", "rooms/25.", "rooms/38."};
    CHECK(job.start(path, dir / "job", options));
    CHECK(!job.start(path, dir / "job2", options));  // one at a time
    int polls = 0;
    const auto st = poll(job, [](const setup::TrackerRenderJob::Status& s) { return !s.running; }, 120, &polls);
    CHECK(st.outcome == Outcome::kDone);
    CHECK_EQ(st.done, 5);
    CHECK_EQ(st.total, 5);
    const std::set<std::string> files = files_under(dir / "job");
    CHECK(files == files_under(dir / "all"));
    for (const std::string& f : files) CHECK(read_file(dir / "job" / f) == read_file(dir / "all" / f));
    CHECK(!fs::exists(dir / "job2"));
  }
  // Stopped mid-render (all 793 rooms asked for): it stops soon, every file whole.
  {
    setup::TrackerRenderJob job;
    tools::TrackerRenderOptions options;
    options.only = {"images/rooms/"};
    CHECK(job.start(path, dir / "stop", options));
    auto st = poll(job, [](const setup::TrackerRenderJob::Status& s) { return !s.running || s.done >= 3; }, 120);
    CHECK(st.running);
    CHECK(st.kind == "room");
    CHECK(!st.name.empty());
    const auto t0 = Clock::now();
    job.stop();
    const double took = std::chrono::duration<double>(Clock::now() - t0).count();
    std::printf("  stopped at %d of %d images, in %.3f s\n", job.status().done, job.status().total, took);
    CHECK(took < 5.0);
    st = job.status();
    CHECK(!st.running);
    CHECK(st.outcome == Outcome::kStopped);
    CHECK(st.done >= 3 && st.done < st.total);
    const std::set<std::string> files = files_under(dir / "stop");
    CHECK_EQ(static_cast<int>(files.size()), st.done);
    CHECK(!has_part_file(files));
  }
  // Stopped by its destructor (the setup window closing mid-render).
  {
    const auto t0 = Clock::now();
    {
      setup::TrackerRenderJob job;
      tools::TrackerRenderOptions options;
      options.only = {"images/rooms/"};
      CHECK(job.start(path, dir / "closed", options));
      poll(job, [](const setup::TrackerRenderJob::Status& s) { return !s.running || s.done >= 2; }, 120);
    }
    const std::set<std::string> files = files_under(dir / "closed");
    CHECK(files.size() >= 2 && files.size() < 793);
    CHECK(!has_part_file(files));
    CHECK(std::chrono::duration<double>(Clock::now() - t0).count() < 60.0);
  }
  fs::remove_all(dir);
}
