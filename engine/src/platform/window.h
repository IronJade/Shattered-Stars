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

// The keys the port itself knows, whatever the controls bind: the quit prompt's (Esc, Enter
// or the keypad's, the Left / Right arrows; a gamepad's d-pad and left stick Left / Right,
// the stick only while the prompt is up) [escquit].
enum class HostKey : u8 { kNone, kEscape, kEnter, kLeft, kRight };

struct KeyEvent {
  int key = 0;  // SDL keycode; 0 for a gamepad button
  bool down = false;
  u16 bits = 0;  // the PS1 buttons it presses under the player's controls (none for Esc / F11)
  HostKey host = HostKey::kNone;
};

// The player's controls (config.ini's key_* / pad_* keys, set in setup): for each PS1
// button, by bit number in PadButton, one keyboard key and one gamepad button. Gamepad
// buttons are SDL_GamepadButton values, or the triggers (kLeftTrigger / kRightTrigger,
// pressed past half way). 0 / kNoButton: unbound.
inline constexpr int kNoButton = -1, kLeftTrigger = 100, kRightTrigger = 101;
struct Controls {
  int keys[16] = {};     // SDL keycodes
  int buttons[16] = {};  // gamepad buttons
  static Controls defaults();
  bool operator==(const Controls&) const = default;
};
// The buttons (PadButton bits) a key or gamepad button presses under these controls.
u16 pad_bits_for_key(const Controls& controls, int keycode);
u16 pad_bits_for_button(const Controls& controls, int button);

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
// fullscreen kind, or borderless when the window mode is windowed). Esc closes nothing
// here: the game asks first (runtime/quit_prompt.h); like F11 it is never a PS1 button.
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

  // Handles pending events; returns false once the window should close (its close button,
  // Alt+F4). 'on_key' receives key and gamepad button presses and releases.
  template <typename F>
  bool pump(F&& on_key) {
    KeyEvent e;
    bool open = true;
    while (next_event(open, e)) on_key(e);
    return open;
  }
  u16 pad() const { return static_cast<u16>(key_bits_ | button_bits_ | (trigger_bits_ & ~swallowed_triggers_)); }
  void set_controls(const Controls& controls);
  // While the port's quit prompt is up [escquit]: a key or button pressed now is the prompt's,
  // and presses no PS1 button until it is released (also after the prompt has closed); what
  // was held before keeps counting. The gamepad's motors stop, and get the game's levels back after.
  void set_modal(bool on);
  // The size 'present' scales a picture of this aspect to, in window pixels (letterboxed).
  void picture_size(float aspect, int& width, int& height) const;
  // The open gamepad's sticks as a DualShock reports them (right X, right Y, left X,
  // left Y; 0x80 centred, 0 left / up); false without a gamepad.
  bool sticks(u8 out[4]) const;

  // Draws an RGBA8888 image scaled into the window, keeping 'aspect' (width / height).
  void present(const u32* pixels, int width, int height, float aspect);
  // Reads back what 'present' just drew (RGBA8888, top row first) via glReadPixels, at
  // the window's current pixel size; false without a GL context. Debug use only
  // (SS_SHOT screenshots): this is the actual OpenGL output, not the PS1 display buffer.
  bool read_pixels(std::vector<u32>& out, int& width, int& height) const;
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
  bool initialized_ = false;  // true once this object's 'create' called SDL_Init
  unsigned texture_ = 0, program_ = 0, vao_ = 0;
  int tex_width_ = 0, tex_height_ = 0;
  s16 axes_[4] = {};        // gamepad right X, right Y, left X, left Y
  bool fullscreen_ = false; // F11 / Alt+Enter
  Controls controls_ = Controls::defaults();
  std::vector<int> held_keys_, held_buttons_;  // what is down now, to recompute the bits
  u16 key_bits_ = 0, button_bits_ = 0, trigger_bits_ = 0;
  // set_modal [escquit]: pressed while modal, not released yet (no bits); the last rumble.
  bool modal_ = false;
  std::vector<int> swallowed_keys_, swallowed_buttons_;
  u16 swallowed_triggers_ = 0;
  int rumble_small_ = 0, rumble_large_ = 0;
  int stick_dir_ = 0;  // the left stick: -1 left, 1 right, 0 centred (the prompt's Left / Right)
};

}  // namespace ss::platform
