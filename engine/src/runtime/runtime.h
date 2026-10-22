#pragma once

#include <chrono>
#include <filesystem>
#include <map>
#include <set>
#include <string>

#include "core/types.h"
#include "psx/gpu.h"

namespace ss::platform {
class Window;
}

// The host side of the console: the GPU instance, the window, and vertical-blank
// timing. Ported game code reaches it through the PsyQ-shaped functions in
// runtime/libgpu.h and runtime/libetc.h, exactly where the original called Sony's
// libraries.
namespace ss::runtime {

// Thrown out of VSync when the window closes or a headless run reaches its frame limit.
struct QuitRequested {};

class Runtime {
 public:
  static Runtime& get();

  psx::Gpu gpu;
  platform::Window* window = nullptr;  // null for headless runs
  bool display_enabled = false;        // SetDispMask

  // Headless options: stop after this many vblanks, and save the display as PNG
  // on the listed vblank numbers.
  u64 frame_limit = 0;
  std::filesystem::path capture_dir;
  std::set<u64> capture_frames;

  void (*vsync_callback)() = nullptr;  // VSyncCallback
  // Runs once per vblank before the VSync callback: produces the field's sound
  // (44100 / 59.94 samples, clocking the sound driver's root counter) and outputs it.
  void (*audio_tick)() = nullptr;

  // Scripted controller input for tests: from each listed vblank on, port 0
  // holds the given buttons (replacing the window's controller).
  std::map<u64, u16> scripted_input;
  u16 host_buttons() const;

  u64 vblanks() const { return vblank_count_; }
  u64 last_vsync() const { return last_vsync_; }
  void set_last_vsync(u64 v) { last_vsync_ = v; }

  // One vertical blank: runs the VSync callback, shows the display, handles window
  // events and paces to 59.94 Hz. Throws QuitRequested to unwind the game.
  void vblank();
  void log_once(const std::string& key, const std::string& message);
  // With SS_TRACE set, prints "[trace] <what> at vblank N" (aligns port and reference captures).
  void trace(const char* what) const;

 private:
  void present();
  u64 vblank_count_ = 0;
  u64 last_vsync_ = 0;
  std::chrono::steady_clock::time_point next_deadline_{};
  std::set<std::string> logged_;
};

}  // namespace ss::runtime
