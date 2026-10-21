#pragma once

#include <string>

#include "core/types.h"

struct SDL_Window;

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

// SDL3 window with an OpenGL 3.3 core context that presents an RGBA image.
class Window {
 public:
  Window() = default;
  ~Window();
  Window(const Window&) = delete;
  Window& operator=(const Window&) = delete;

  bool create(const std::string& title, int width, int height, std::string* error);

  // Handles pending events; returns false once the window should close.
  // 'on_key' receives key presses for tools such as the viewer.
  template <typename F>
  bool pump(F&& on_key) {
    KeyEvent e;
    bool open = true;
    while (next_event(open, e)) on_key(e);
    return open;
  }
  u16 pad() const { return static_cast<u16>(pad_ | stick_); }

  // Draws an RGBA8888 image scaled into the window, keeping 'aspect' (width / height).
  void present(const u32* pixels, int width, int height, float aspect);
  void set_title(const std::string& title);

 private:
  bool next_event(bool& open, KeyEvent& out);

  SDL_Window* window_ = nullptr;
  void* context_ = nullptr;
  unsigned texture_ = 0, program_ = 0, vao_ = 0;
  int tex_width_ = 0, tex_height_ = 0;
  u16 stick_ = 0;           // d-pad bits from the left stick
  bool fullscreen_ = false; // F11 / Alt+Enter
  u16 pad_ = 0;
};

}  // namespace ss::platform
