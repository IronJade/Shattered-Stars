#include "runtime/runtime.h"

#include "runtime/debug_overlay.h"
#include "runtime/quit_prompt.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <thread>
#include <vector>

#include "platform/window.h"
#include "util/log.h"
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

// Tests [escquit]: SS_KEYS="VBLANK:key,key,...;VBLANK:..." presses host keys at a vblank as
// the window would deliver them, with the default controls: the first at that vblank, then
// one per frame of the quit prompt while it is up. Keys: escape, enter, left, right (the
// arrows), cross, circle, start, triangle (the keys bound to them), close (the window's close
// button), wait (nothing that frame). Outside the prompt only escape and close do anything.
struct ScriptedKey {
  platform::HostKey host = platform::HostKey::kNone;
  u16 bits = 0;
  bool close = false, wait = false;
};

const std::map<u64, std::deque<ScriptedKey>>& scripted_keys() {
  static const std::map<u64, std::deque<ScriptedKey>> keys = [] {
    using platform::HostKey;
    const std::map<std::string, ScriptedKey> names = {
        {"escape", {HostKey::kEscape, 0}},
        {"enter", {HostKey::kEnter, platform::kPadStart}},
        {"left", {HostKey::kLeft, platform::kPadLeft}},
        {"right", {HostKey::kRight, platform::kPadRight}},
        {"cross", {HostKey::kNone, platform::kPadCross}},
        {"circle", {HostKey::kNone, platform::kPadCircle}},
        {"start", {HostKey::kNone, platform::kPadStart}},
        {"triangle", {HostKey::kNone, platform::kPadTriangle}},
        {"close", {HostKey::kNone, 0, true, false}},
        {"wait", {HostKey::kNone, 0, false, true}}};
    std::map<u64, std::deque<ScriptedKey>> out;
    const char* spec = std::getenv("SS_KEYS");
    for (const char* p = spec; p != nullptr && *p != 0;) {
      char* end = nullptr;
      const unsigned long long at = std::strtoull(p, &end, 10);
      if (end == nullptr || *end != ':') break;
      const char* text = end + 1;
      const char* semi = std::strchr(text, ';');
      const std::string list = semi ? std::string(text, semi) : std::string(text);
      for (size_t start = 0; start <= list.size();) {
        const size_t comma = std::min(list.find(',', start), list.size());
        const std::string name = list.substr(start, comma - start);
        if (auto it = names.find(name); it != names.end()) out[at].push_back(it->second);
        else if (!name.empty()) std::fprintf(stderr, "SS_KEYS: unknown key '%s'\n", name.c_str());
        start = comma + 1;
      }
      p = semi ? semi + 1 : nullptr;
    }
    return out;
  }();
  return keys;
}

std::deque<ScriptedKey>& scripted_queue() {  // this vblank's SS_KEYS not pressed yet
  static std::deque<ScriptedKey> queue;
  return queue;
}

}  // namespace

Runtime& Runtime::get() {
  static Runtime instance;
  return instance;
}

void Runtime::log_once(const std::string& key, const std::string& message) {
  if (logged_.insert(key).second) util::log_line("[port] " + message);  // stderr and the run's log
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

void Runtime::screen(std::vector<u32>& pixels, int& w, int& h) {
  if (display_enabled) {
    gpu.display_to_rgba(pixels, w, h);
  } else {
    w = gpu.display().width > 0 ? gpu.display().width : 320;
    h = gpu.display().height > 0 ? gpu.display().height : 240;
    pixels.assign(static_cast<size_t>(w) * h, 0xFF000000u);
  }
  if (debug_overlay) draw_debug_overlay(pixels, w, h, debug_status ? debug_status() : "debug console", debug_output_);
}

// On a TV the lines outside the display range are border: put them back so the picture
// keeps its 4:3 proportions. Shared by the real window and the SS_SHOT window, so both
// present exactly the same image.
void Runtime::frame_border(std::vector<u32>& pixels, int w, int& h) const {
  const auto& d = gpu.display();
  if (!display_enabled || h >= d.height) return;
  std::vector<u32> framed(static_cast<size_t>(w) * d.height, 0xFF000000u);
  const int top = std::clamp(d.top, 0, d.height - h);
  std::copy(pixels.begin(), pixels.end(), framed.begin() + static_cast<std::ptrdiff_t>(top) * w);
  pixels.swap(framed);
  h = d.height;
}

void Runtime::present() {
  std::vector<u32> pixels;
  int w = 0, h = 0;
  screen(pixels, w, h);
  if (capture_frames.count(vblank_count_) && !capture_dir.empty()) {
    std::filesystem::create_directories(capture_dir);
    char name[64];
    std::snprintf(name, sizeof name, "frame%05llu.png", static_cast<unsigned long long>(vblank_count_));
    util::write_png(capture_dir / name, pixels, w, h);
  }
  frame_border(pixels, w, h);
  const u32* present_pixels = pixels.data();
  const int present_w = w, present_h = h;
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

void Runtime::host_events() {
  std::deque<ScriptedKey>& queue = scripted_queue();
  if (const auto& script = scripted_keys(); !script.empty())
    if (auto it = script.find(vblank_count_); it != script.end()) queue.insert(queue.end(), it->second.begin(), it->second.end());
  if (window == nullptr && queue.empty()) return;
  std::vector<platform::KeyEvent> after;  // presses after an Esc in the same events: the prompt's
  bool asked = false;
  if (window && !window->pump([&](const platform::KeyEvent& k) {
        if (asked) {
          after.push_back(k);
        } else if (k.down && k.host == platform::HostKey::kEscape) {
          asked = true;
          window->set_modal(true);  // what follows is the prompt's, not the game's
        }
      }))
    throw QuitRequested{};  // the window's close button, Alt+F4
  for (;;) {
    if (asked && ask_quit(after)) throw QuitRequested{};
    asked = false;
    while (!asked && !queue.empty()) {
      const ScriptedKey k = queue.front();
      queue.pop_front();
      if (k.close) throw QuitRequested{};
      asked = k.host == platform::HostKey::kEscape;
    }
    if (!asked) return;
  }
}

bool Runtime::ask_quit(std::vector<platform::KeyEvent>& input) {
  using Result = QuitPrompt::Result;
  util::log_line("[port] quit prompt: open at vblank " + std::to_string(vblank_count_));
  if (window) window->set_modal(true);
  if (audio_pause) audio_pause(true);
  // The picture as the window shows it now; the game's state is not touched again until the
  // prompt closes (this vblank resumes where it stopped).
  std::vector<u32> frame;
  int frame_w = 0, frame_h = 0;
  screen(frame, frame_w, frame_h);
  frame_border(frame, frame_w, frame_h);
  QuitPrompt prompt;
  Result result = Result::kOpen;
  const auto press = [&](const platform::KeyEvent& k) {
    if (k.down && result == Result::kOpen) result = prompt.press(k.host, k.bits);
  };
  for (const platform::KeyEvent& k : input) press(k);
  input.clear();
  struct Shown {  // a window's prompt picture, drawn again when the choice or the scale changes
    int scale = 0, choice = -1;
    std::vector<u32> canvas;
  } on_window, on_shot;
  const auto show = [&](platform::Window* target, Shown& s) {
    int pw = 0, ph = 0;
    target->picture_size(4.0f / 3.0f, pw, ph);
    const int scale = quit_prompt_scale(pw, ph);
    if (scale != s.scale || prompt.choice() != s.choice) {
      draw_quit_prompt(frame.data(), frame_w, frame_h, prompt.choice(), scale, s.canvas);
      s.scale = scale;
      s.choice = prompt.choice();
    }
    target->present(s.canvas.data(), 320 * scale, 240 * scale, 4.0f / 3.0f);
  };
  const auto shot = shot_window ? shot_frames.find(vblank_count_) : shot_frames.end();
  std::deque<ScriptedKey>& queue = scripted_queue();
  auto deadline = std::chrono::steady_clock::now();
  for (int shown = 0; result == Result::kOpen; ++shown) {
    if (window) show(window, on_window);
    if (shot != shot_frames.end()) {
      // SS_SHOT at this vblank: the prompt's frames too, as NAME-prompt0.png, -prompt1.png, ...
      show(shot_window, on_shot);
      std::vector<u32> pixels;
      int sw = 0, sh = 0;
      if (shot_window->read_pixels(pixels, sw, sh)) {
        std::filesystem::path path = shot->second, name = path.stem();
        name += "-prompt" + std::to_string(shown);
        name += path.extension();
        path.replace_filename(name);
        util::write_png(path, pixels, sw, sh);
      }
    }
    if (window && !window->pump(press)) result = Result::kQuit;  // closed: the OS convention
    if (result == Result::kOpen && !queue.empty()) {
      const ScriptedKey k = queue.front();
      queue.pop_front();
      if (k.close) result = Result::kQuit;
      else if (!k.wait) press(platform::KeyEvent{0, true, k.bits, k.host});
    } else if (result == Result::kOpen && !window) {
      result = Result::kKeepPlaying;  // headless, nothing more to press: back to the game
    }
    if (window && result == Result::kOpen) {
      deadline += kFieldPeriod;
      std::this_thread::sleep_until(deadline);
    }
  }
  if (window) window->set_modal(false);
  if (audio_pause) audio_pause(false);
  util::log_line(std::string("[port] quit prompt: ") + (result == Result::kQuit ? "quit" : "keep playing"));
  return result == Result::kQuit;
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
    // ASCII only: under X11, SDL drops a title the C locale can't encode (an em dash did).
    if (window && debug_status && vblank_count_ % 30 == 0) window->set_title("Shattered Stars - " + debug_status());
  }
  if (audio_tick) audio_tick();
  if (net_poll) net_poll();  // (Archipelago, game/ap) [world agent]
  if (vsync_callback) vsync_callback();
  if (window || capture_frames.count(vblank_count_) || (shot_window && shot_frames.count(vblank_count_)))
    present();
  host_events();  // the window closed, or Quit on the quit prompt: QuitRequested [escquit]
  // [world agent] SS_PACE=1: a headless run paced like a windowed one (tests that talk to a
  // server in real time).
  static const bool paced = std::getenv("SS_PACE") != nullptr;
  if (window || paced) {
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
