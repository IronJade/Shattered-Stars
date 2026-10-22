#include "runtime/runtime.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <iostream>
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

bool Runtime::host_sticks(u8 out[4]) const { return scripted_input.empty() && window != nullptr && window->sticks(out); }

void Runtime::host_rumble(int small, int large) {
  if (window != nullptr) window->rumble(small, large);
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
  if (window) {
    // On a TV the lines outside the display range are border: put them back so the
    // picture keeps its 4:3 proportions.
    const auto& d = gpu.display();
    if (display_enabled && h < d.height) {
      std::vector<u32> framed(static_cast<size_t>(w) * d.height, 0xFF000000u);
      const int top = std::clamp(d.top, 0, d.height - h);
      std::copy(pixels.begin(), pixels.end(), framed.begin() + static_cast<std::ptrdiff_t>(top) * w);
      window->present(framed.data(), w, d.height, 4.0f / 3.0f);
    } else {
      window->present(pixels.data(), w, h, 4.0f / 3.0f);
    }
  }
}

void Runtime::start_debug_console() {
  if (debug_console_) return;
  debug_console_ = true;
  std::fprintf(stderr, "[debug] console on: type 'help' and press Enter\n");
  std::thread([this] {
    for (std::string line; std::getline(std::cin, line);) {
      std::lock_guard<std::mutex> lock(debug_mutex_);
      debug_lines_.push_back(line);
    }
  }).detach();
}

void Runtime::vblank() {
  if (debug_console_) {
    std::deque<std::string> lines;
    {
      std::lock_guard<std::mutex> lock(debug_mutex_);
      lines.swap(debug_lines_);
    }
    for (const std::string& line : lines) {
      if (debug_command) debug_command(line);
      else std::fprintf(stderr, "[debug] no game state to act on yet\n");
    }
    if (window && debug_status && vblank_count_ % 30 == 0) window->set_title("Shattered Stars — " + debug_status());
  }
  if (audio_tick) audio_tick();
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
