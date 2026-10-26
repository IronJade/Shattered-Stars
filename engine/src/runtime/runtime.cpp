#include "runtime/runtime.h"

#include "runtime/debug_overlay.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <thread>
#include <vector>

#include "platform/window.h"
#include "util/png.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <unistd.h>
#endif

namespace ss::runtime {
namespace {

// NTSC field rate: 60000 / 1001 Hz.
constexpr auto kFieldPeriod = std::chrono::nanoseconds(16683333);

// The debug console's reader (--debug) [cleanup]. It reads the OS handle itself (read(2) /
// ReadFile), never the C stream: a std::getline(std::cin) waiting for a line holds stdin's
// FILE lock, and exit() takes every stream's lock to flush it, so a --debug run whose stdin
// stayed open (a terminal, a pipe) could hang in exit(). The thread is detached and exit()
// ends it wherever it waits; the lines it reads go to a block that is never freed, so a line
// arriving while the process exits never touches a destroyed Runtime.
struct ConsoleInput {
  std::mutex mutex;
  std::deque<std::string> lines;
};

ConsoleInput& console_input() {
  static ConsoleInput* input = new ConsoleInput;  // never freed (above)
  return *input;
}

#ifdef _WIN32
using StdinHandle = HANDLE;
#else
using StdinHandle = int;
#endif

// Up to 'n' bytes of stdin; 0 at its end or on an error.
size_t read_stdin(StdinHandle in, char* buffer, size_t n) {
#ifdef _WIN32
  DWORD got = 0;
  return ReadFile(in, buffer, static_cast<DWORD>(n), &got, nullptr) ? got : 0;
#else
  for (;;) {
    const ssize_t got = ::read(in, buffer, n);
    if (got >= 0) return static_cast<size_t>(got);
    if (errno != EINTR) return 0;
  }
#endif
}

// Splits stdin into lines (without "\n" / "\r\n") until its end, as std::getline did: a
// last line without its newline counts too. On Windows Ctrl+Z ends the input, as the C
// runtime's text mode did.
void console_reader(StdinHandle in) {
  ConsoleInput& input = console_input();
  std::string line;
  bool more = true;
  const auto push = [&] {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    std::lock_guard<std::mutex> lock(input.mutex);
    input.lines.push_back(std::move(line));
    line.clear();
  };
  char buffer[512];
  for (size_t got; more && (got = read_stdin(in, buffer, sizeof buffer)) != 0;) {
    for (size_t i = 0; i < got && more; ++i) {
#ifdef _WIN32
      if (buffer[i] == 0x1A) {
        more = false;
        continue;
      }
#endif
      if (buffer[i] == '\n') push();
      else line += buffer[i];
    }
  }
  if (!line.empty()) push();
}

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
  if (debug_overlay) draw_debug_overlay(pixels, w, h, debug_status ? debug_status() : "debug console", debug_output_);
  if (capture_frames.count(vblank_count_) && !capture_dir.empty()) {
    std::filesystem::create_directories(capture_dir);
    char name[64];
    std::snprintf(name, sizeof name, "frame%05llu.png", static_cast<unsigned long long>(vblank_count_));
    util::write_png(capture_dir / name, pixels, w, h);
  }
  // On a TV the lines outside the display range are border: put them back so the picture
  // keeps its 4:3 proportions. Shared by the real window and the SS_SHOT window below, so
  // both present exactly the same image.
  const auto& d = gpu.display();
  const u32* present_pixels = pixels.data();
  int present_w = w, present_h = h;
  std::vector<u32> framed;
  if (display_enabled && h < d.height) {
    framed.assign(static_cast<size_t>(w) * d.height, 0xFF000000u);
    const int top = std::clamp(d.top, 0, d.height - h);
    std::copy(pixels.begin(), pixels.end(), framed.begin() + static_cast<std::ptrdiff_t>(top) * w);
    present_pixels = framed.data();
    present_h = d.height;
  }
  if (window) window->present(present_pixels, present_w, present_h, 4.0f / 3.0f);
  if (shot_window) {
    if (auto it = shot_frames.find(vblank_count_); it != shot_frames.end()) {
      shot_window->present(present_pixels, present_w, present_h, 4.0f / 3.0f);
      std::vector<u32> shot;
      int sw = 0, sh = 0;
      if (shot_window->read_pixels(shot, sw, sh)) {
        if (it->second.has_parent_path()) std::filesystem::create_directories(it->second.parent_path());
        util::write_png(it->second, shot, sw, sh);
      }
    }
  }
}

void Runtime::start_debug_console() {
  if (debug_console_) return;
  debug_console_ = true;
  std::fprintf(stderr, "[debug] console on: type 'help' and press Enter\n");
  console_input();  // made here, before the reader runs
#ifdef _WIN32
  // The process's stdin: what the C runtime's stdin was opened on (a redirected handle, or
  // the console of its own that launcher::attach_console allocated for --debug).
  const HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
  if (in == nullptr || in == INVALID_HANDLE_VALUE) {
    std::fprintf(stderr, "[debug] no console input; only SS_DEBUG_INPUT commands run\n");
    return;
  }
#else
  const int in = STDIN_FILENO;
#endif
  std::thread(console_reader, in).detach();
}

void Runtime::debug_say(const std::string& text) {
  size_t start = 0;
  while (start <= text.size()) {
    const size_t end = text.find('\n', start);
    const std::string line = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
    std::fprintf(stderr, "[debug] %s\n", line.c_str());
    debug_output_.push_back(line);
    while (debug_output_.size() > 6) debug_output_.pop_front();
    if (end == std::string::npos) break;
    start = end + 1;
  }
}

void Runtime::vblank() {
  if (debug_console_) {
    std::deque<std::string> lines;
    {
      ConsoleInput& input = console_input();
      std::lock_guard<std::mutex> lock(input.mutex);
      lines.swap(input.lines);
    }
    // Tests: SS_DEBUG_INPUT="VBLANK:command;VBLANK:command" types commands at set vblanks.
    static const char* scripted = std::getenv("SS_DEBUG_INPUT");
    for (const char* p = scripted; p != nullptr && *p != 0;) {
      char* end = nullptr;
      const unsigned long long at = std::strtoull(p, &end, 10);
      if (end == nullptr || *end != ':') break;
      const char* semi = std::strchr(end + 1, ';');
      const char* text = end + 1;
      if (at == vblank_count_) lines.push_back(semi ? std::string(text, static_cast<size_t>(semi - text)) : std::string(text));
      p = semi ? semi + 1 : nullptr;
    }
    for (const std::string& line : lines) {
      if (debug_overlay && !line.empty()) {
        debug_output_.push_back("> " + line);
        while (debug_output_.size() > 6) debug_output_.pop_front();
      }
      if (debug_command) debug_command(line);
      else std::fprintf(stderr, "[debug] no game state to act on yet\n");
    }
    if (window && debug_status && vblank_count_ % 30 == 0) window->set_title("Shattered Stars — " + debug_status());
  }
  if (audio_tick) audio_tick();
  if (net_poll) net_poll();  // (Archipelago, game/ap) [world agent]
  if (vsync_callback) vsync_callback();
  if (window || capture_frames.count(vblank_count_) || (shot_window && shot_frames.count(vblank_count_)))
    present();
  // [world agent] SS_PACE=1: a headless run paced like a windowed one (tests that talk to a
  // server in real time).
  static const bool paced = std::getenv("SS_PACE") != nullptr;
  if (window || paced) {
    if (window && !window->pump([](const platform::KeyEvent&) {})) throw QuitRequested{};
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
