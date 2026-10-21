#include "runtime/runtime.h"

#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <thread>
#include <vector>

#include "platform/window.h"
#include "util/png.h"

namespace ss::runtime {
namespace {

// NTSC field rate: 60000 / 1001 Hz.
constexpr auto kFieldPeriod = std::chrono::nanoseconds(16683333);

}  // namespace

Runtime& Runtime::get() {
  static Runtime instance;
  return instance;
}

void Runtime::log_once(const std::string& key, const std::string& message) {
  if (logged_.insert(key).second) std::fprintf(stderr, "[port] %s\n", message.c_str());
}

void Runtime::trace(const char* what) const {
  static const bool enabled = std::getenv("SS_TRACE") != nullptr;
  if (enabled) std::fprintf(stderr, "[trace] %s at vblank %llu\n", what, static_cast<unsigned long long>(vblank_count_));
}

u16 Runtime::host_buttons() const {
  if (!scripted_input.empty()) {
    auto it = scripted_input.upper_bound(vblank_count_);
    return it == scripted_input.begin() ? 0 : std::prev(it)->second;
  }
  return window ? window->pad() : 0;
}

void Runtime::present() {
  std::vector<u32> pixels;
  int w = 0, h = 0;
  if (display_enabled) {
    gpu.display_to_rgba(pixels, w, h);
  } else {
    w = gpu.display().width > 0 ? gpu.display().width : 320;
    h = gpu.display().height > 0 ? gpu.display().height : 240;
    pixels.assign(static_cast<size_t>(w) * h, 0xFF000000u);
  }
  if (capture_frames.count(vblank_count_) && !capture_dir.empty()) {
    std::filesystem::create_directories(capture_dir);
    char name[64];
    std::snprintf(name, sizeof name, "frame%05llu.png", static_cast<unsigned long long>(vblank_count_));
    util::write_png(capture_dir / name, pixels, w, h);
  }
  if (window) window->present(pixels.data(), w, h, 4.0f / 3.0f);
}

void Runtime::vblank() {
  if (vsync_callback) vsync_callback();
  if (window || capture_frames.count(vblank_count_)) present();
  if (window) {
    if (!window->pump([](const platform::KeyEvent&) {})) throw QuitRequested{};
    const auto now = std::chrono::steady_clock::now();
    if (next_deadline_.time_since_epoch().count() == 0 || now - next_deadline_ > std::chrono::milliseconds(100)) {
      next_deadline_ = now;
    }
    next_deadline_ += kFieldPeriod;
    std::this_thread::sleep_until(next_deadline_);
  }
  ++vblank_count_;
  if (frame_limit && vblank_count_ >= frame_limit) throw QuitRequested{};
}

}  // namespace ss::runtime
