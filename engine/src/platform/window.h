#pragma once

#include <string>
#include <vector>

#include "core/types.h"

struct SDL_Window;
struct SDL_Gamepad;

namespace ss::platform {

// PS1 controller buttons in the layout the game uses (vsync_callback inverts the
// two raw pad bytes into this order; active-high).
enum PadButton : u16 {
  kPadL2 = 1 << 0, kPadR2 = 1 << 1, kPadL1 = 1 << 2, kPadR1 = 1 << 3,
  kPadTriangle = 1 << 4, kPadCircle = 1 << 5, kPadCross = 1 << 6, kPadSquare = 1 << 7,
  kPadSelect = 1 << 8, kPadL3 = 1 << 9, kPadR3 = 1 << 10, kPadStart = 1 << 11,
  kPadUp = 1 << 12, kPadRight = 1 << 13, kPadDown = 1 << 14, kPadLeft = 1 << 15,
};

struct KeyEvent {
  int key = 0;  // SDL keycode
  bool down = false;
};

// How the game window opens (config.ini's display keys, launcher/settings.h).
enum class WindowMode { kWindowed, kBorderless, kFullscreen };

struct WindowOptions {
  WindowMode mode = WindowMode::kWindowed;
  int width = 960, height = 720;  // the windowed size (shrunk to fit the display)
  // Exclusive fullscreen: the display mode closest to this (0 x 0: the desktop's).
  int fullscreen_width = 0, fullscreen_height = 0;
  float refresh = 0;  // Hz; 0: any
  int monitor = 1;    // 1 = the primary display, then the others (display_order())
  bool vsync = true;
};

// The displays as the settings number them: the primary one first, then the others in
// SDL's order (SDL_DisplayID values). Needs the video subsystem.
std::vector<u32> display_order();

// SDL3 window with an OpenGL 3.3 core context that presents an RGBA image.
// F11 or Alt+Enter switches between the window and full screen (the configured
// fullscreen kind, or borderless when the window mode is windowed).
class Window {
 public:
  Window() = default;
  ~Window();
  Window(const Window&) = delete;
  Window& operator=(const Window&) = delete;

  bool create(const std::string& title, const WindowOptions& options, std::string* error);
  bool create(const std::string& title, int width, int height, std::string* error) {
    WindowOptions options;
    options.width = width;
    options.height = height;
    return create(title, options, error);
  }

  // Handles pending events; returns false once the window should close.
  // 'on_key' receives key presses for tools such as the viewer.
  template <typename F>
  bool pump(F&& on_key) {
    KeyEvent e;
    bool open = true;
    while (next_event(open, e)) on_key(e);
    return open;
  }
  u16 pad() const { return pad_; }
  // The open gamepad's sticks as a DualShock reports them (right X, right Y, left X,
  // left Y; 0x80 centred, 0 left / up); false without a gamepad.
  bool sticks(u8 out[4]) const;

  // Draws an RGBA8888 image scaled into the window, keeping 'aspect' (width / height).
  void present(const u32* pixels, int width, int height, float aspect);
  void set_title(const std::string& title);

  // DualShock actuators on the open gamepad: 'small' on / off (the high-frequency motor),
  // 'large' 0-255 (the low-frequency one); 0 / 0 stops. Nothing without a gamepad that
  // can rumble.
  void rumble(int small, int large);

 private:
  bool next_event(bool& open, KeyEvent& out);
  void set_fullscreen(bool on);

  SDL_Window* window_ = nullptr;
  SDL_Gamepad* gamepad_ = nullptr;  // the last one connected
  void* context_ = nullptr;
  unsigned texture_ = 0, program_ = 0, vao_ = 0;
  int tex_width_ = 0, tex_height_ = 0;
  s16 axes_[4] = {};        // gamepad right X, right Y, left X, left Y
  bool fullscreen_ = false; // F11 / Alt+Enter
  u16 pad_ = 0;
};

}  // namespace ss::platform
