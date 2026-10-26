#include "platform/window.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <bit>
#include <cstring>

#include "platform/app_icon.h"
#include "platform/gl.h"

namespace ss::platform {
namespace {

const char* kVertexShader = R"(#version 330 core
out vec2 uv;
void main() {
  // One triangle covering the viewport; uv.y flipped so image row 0 is the top.
  vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
  uv = vec2(p.x, 1.0 - p.y);
  gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
})";

const char* kFragmentShader = R"(#version 330 core
in vec2 uv;
out vec4 color;
uniform sampler2D image;
void main() { color = vec4(texture(image, uv).rgb, 1.0); })";

unsigned compile(gl::GLenum type, const char* source, std::string* error) {
  const unsigned shader = gl::CreateShader(type);
  gl::ShaderSource(shader, 1, &source, nullptr);
  gl::CompileShader(shader);
  int ok = 0;
  gl::GetShaderiv(shader, gl::COMPILE_STATUS, &ok);
  if (!ok) {
    char log[1024] = {};
    gl::GetShaderInfoLog(shader, sizeof log, nullptr, log);
    if (error) *error = std::string("shader compile failed: ") + log;
    gl::DeleteShader(shader);
    return 0;
  }
  return shader;
}

}  // namespace

Controls Controls::defaults() {
  Controls c;
  auto set = [&](PadButton b, int key, int button) {
    const int bit = std::countr_zero(static_cast<unsigned>(b));
    c.keys[bit] = key;
    c.buttons[bit] = button;
  };
  set(kPadUp, SDLK_UP, SDL_GAMEPAD_BUTTON_DPAD_UP);
  set(kPadDown, SDLK_DOWN, SDL_GAMEPAD_BUTTON_DPAD_DOWN);
  set(kPadLeft, SDLK_LEFT, SDL_GAMEPAD_BUTTON_DPAD_LEFT);
  set(kPadRight, SDLK_RIGHT, SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
  set(kPadStart, SDLK_RETURN, SDL_GAMEPAD_BUTTON_START);
  set(kPadSelect, SDLK_BACKSPACE, SDL_GAMEPAD_BUTTON_BACK);
  set(kPadCross, SDLK_Z, SDL_GAMEPAD_BUTTON_SOUTH);
  set(kPadCircle, SDLK_X, SDL_GAMEPAD_BUTTON_EAST);
  set(kPadSquare, SDLK_A, SDL_GAMEPAD_BUTTON_WEST);
  set(kPadTriangle, SDLK_S, SDL_GAMEPAD_BUTTON_NORTH);
  set(kPadL1, SDLK_Q, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
  set(kPadR1, SDLK_W, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
  set(kPadL2, SDLK_1, kLeftTrigger);
  set(kPadR2, SDLK_2, kRightTrigger);
  set(kPadL3, 0, SDL_GAMEPAD_BUTTON_LEFT_STICK);
  set(kPadR3, 0, SDL_GAMEPAD_BUTTON_RIGHT_STICK);
  return c;
}

u16 pad_bits_for_key(const Controls& controls, int keycode) {
  u16 bits = 0;
  if (keycode == 0) return 0;
  for (int i = 0; i < 16; ++i)
    if (controls.keys[i] == keycode) bits |= static_cast<u16>(1u << i);
  return bits;
}

u16 pad_bits_for_button(const Controls& controls, int button) {
  u16 bits = 0;
  if (button == kNoButton) return 0;
  for (int i = 0; i < 16; ++i)
    if (controls.buttons[i] == button) bits |= static_cast<u16>(1u << i);
  return bits;
}

void Window::set_controls(const Controls& controls) {
  controls_ = controls;
  held_keys_.clear();
  held_buttons_.clear();
  swallowed_keys_.clear();
  swallowed_buttons_.clear();
  key_bits_ = button_bits_ = trigger_bits_ = swallowed_triggers_ = 0;
}

bool Window::sticks(u8 out[4]) const {
  if (gamepad_ == nullptr) return false;
  for (int i = 0; i < 4; ++i) out[i] = static_cast<u8>((axes_[i] + 0x8000) >> 8);
  return true;
}

void Window::rumble(int small, int large) {
  rumble_small_ = small;
  rumble_large_ = large;
  if (gamepad_ == nullptr || modal_) return;
  const Uint16 low = static_cast<Uint16>((large < 0 ? 0 : large > 255 ? 255 : large) * 257);
  const Uint16 high = small != 0 ? 0xFFFF : 0;
  // The console keeps the motors at these levels until the game changes them.
  SDL_RumbleGamepad(gamepad_, low, high, low == 0 && high == 0 ? 0 : 60000);
}

void Window::set_modal(bool on) {  // [escquit]
  if (on == modal_) return;
  modal_ = on;
  if (on && gamepad_ != nullptr) SDL_RumbleGamepad(gamepad_, 0, 0, 0);
  if (!on) rumble(rumble_small_, rumble_large_);
}

void Window::picture_size(float aspect, int& width, int& height) const {
  int win_w = 0, win_h = 0;
  SDL_GetWindowSizeInPixels(window_, &win_w, &win_h);
  width = win_w;
  height = win_h;
  if (win_h > 0 && static_cast<float>(win_w) / win_h > aspect) {
    width = static_cast<int>(win_h * aspect);
  } else {
    height = static_cast<int>(win_w / aspect);
  }
}

namespace {
// How many live Window objects called SDL_Init (normally one; SS_SHOT's second, separate
// window for screenshots makes it two). SDL_Quit tears every subsystem down regardless of
// other still-open windows, so it must wait for the last one, not just "a" destructor.
int& live_windows() {
  static int n = 0;
  return n;
}
}  // namespace

Window::~Window() {
  if (gamepad_) SDL_CloseGamepad(gamepad_);
  if (context_) {
    if (texture_) gl::DeleteTextures(1, &texture_);
    if (vao_) gl::DeleteVertexArrays(1, &vao_);
    if (program_) gl::DeleteProgram(program_);
    SDL_GL_DestroyContext(static_cast<SDL_GLContext>(context_));
  }
  if (window_) SDL_DestroyWindow(window_);
  if (initialized_ && --live_windows() == 0) SDL_Quit();
}

std::vector<u32> display_order() {
  std::vector<u32> out;
  const SDL_DisplayID primary = SDL_GetPrimaryDisplay();
  if (primary != 0) out.push_back(primary);
  int count = 0;
  if (SDL_DisplayID* ids = SDL_GetDisplays(&count)) {
    for (int i = 0; i < count; ++i)
      if (ids[i] != primary) out.push_back(ids[i]);
    SDL_free(ids);
  }
  return out;
}

bool Window::create(const std::string& title, const WindowOptions& options, std::string* error) {
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
    if (error) *error = std::string("SDL_Init: ") + SDL_GetError();
    return false;
  }
  initialized_ = true;
  ++live_windows();
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

  // The display (monitor 1 = the primary one; a monitor that is gone falls back to it).
  const std::vector<u32> displays = display_order();
  SDL_DisplayID display = 0;
  if (!displays.empty()) {
    const bool known = options.monitor >= 1 && static_cast<size_t>(options.monitor) <= displays.size();
    display = displays[known ? static_cast<size_t>(options.monitor - 1) : 0];
  }
  // The windowed size, kept 4:3 and shrunk to fit the display's usable area.
  int width = options.width > 0 ? options.width : 960, height = options.height > 0 ? options.height : 720;
  if (SDL_Rect usable; display != 0 && SDL_GetDisplayUsableBounds(display, &usable) && usable.w > 0 && usable.h > 0) {
    const int max_w = usable.w * 95 / 100, max_h = usable.h * 90 / 100;
    if (width > max_w || height > max_h) {
      const double scale = std::min(static_cast<double>(max_w) / width, static_cast<double>(max_h) / height);
      width = std::max(320, static_cast<int>(width * scale));
      height = std::max(240, static_cast<int>(height * scale));
    }
  }
  window_ = SDL_CreateWindow(title.c_str(), width, height,
                             SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY |
                                 SDL_WINDOW_HIDDEN);
  if (!window_) {
    if (error) *error = std::string("SDL_CreateWindow: ") + SDL_GetError();
    return false;
  }
  set_window_icon(window_);  // the taskbar's and title bar's icon (platform/app_icon.h)
  if (display != 0)
    SDL_SetWindowPosition(window_, static_cast<int>(SDL_WINDOWPOS_CENTERED_DISPLAY(display)),
                          static_cast<int>(SDL_WINDOWPOS_CENTERED_DISPLAY(display)));
  // Full screen: exclusive in the chosen mode (or the desktop's), or borderless (no
  // mode: the window covers the desktop). F11 later switches to the same kind.
  if (options.mode == WindowMode::kFullscreen && display != 0) {
    SDL_DisplayMode mode{};
    const SDL_DisplayMode* desktop = SDL_GetDesktopDisplayMode(display);
    if (options.fullscreen_width > 0 && options.fullscreen_height > 0 &&
        SDL_GetClosestFullscreenDisplayMode(display, options.fullscreen_width, options.fullscreen_height,
                                            options.refresh, true, &mode)) {
      SDL_SetWindowFullscreenMode(window_, &mode);
    } else if (desktop != nullptr) {
      SDL_SetWindowFullscreenMode(window_, desktop);
    }
  }
  if (options.mode != WindowMode::kWindowed) set_fullscreen(true);
  SDL_ShowWindow(window_);
  context_ = SDL_GL_CreateContext(window_);
  if (!context_) {
    if (error) *error = std::string("SDL_GL_CreateContext: ") + SDL_GetError();
    return false;
  }
  SDL_GL_SetSwapInterval(options.vsync ? 1 : 0);
  if (const char* missing = gl::load([](const char* name) -> void* {
        return reinterpret_cast<void*>(SDL_GL_GetProcAddress(name));
      })) {
    if (error) *error = std::string("OpenGL function unavailable: ") + missing;
    return false;
  }

  const unsigned vs = compile(gl::VERTEX_SHADER, kVertexShader, error);
  const unsigned fs = vs ? compile(gl::FRAGMENT_SHADER, kFragmentShader, error) : 0;
  if (!vs || !fs) return false;
  program_ = gl::CreateProgram();
  gl::AttachShader(program_, vs);
  gl::AttachShader(program_, fs);
  gl::LinkProgram(program_);
  gl::DeleteShader(vs);
  gl::DeleteShader(fs);
  int linked = 0;
  gl::GetProgramiv(program_, gl::LINK_STATUS, &linked);
  if (!linked) {
    if (error) *error = "shader link failed";
    return false;
  }
  gl::GenVertexArrays(1, &vao_);
  gl::GenTextures(1, &texture_);
  gl::BindTexture(gl::TEXTURE_2D, texture_);
  gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MIN_FILTER, gl::NEAREST);
  gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MAG_FILTER, gl::NEAREST);
  gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_S, gl::CLAMP_TO_EDGE);
  gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_T, gl::CLAMP_TO_EDGE);
  return true;
}

bool Window::next_event(bool& open, KeyEvent& out) {
  SDL_Event e;
  while (SDL_PollEvent(&e)) {
    switch (e.type) {
      case SDL_EVENT_QUIT:
      case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        open = false;
        break;
      case SDL_EVENT_KEY_DOWN:
      case SDL_EVENT_KEY_UP: {
        const bool down = e.type == SDL_EVENT_KEY_DOWN;
        const int key = static_cast<int>(e.key.key);
        const bool toggle = e.key.key == SDLK_F11 || (e.key.key == SDLK_RETURN && (e.key.mod & SDL_KMOD_ALT));
        // The keys held now give the buttons. The port's keys are none: Alt+Enter / F11 (the
        // full-screen switch) and Esc (the quit prompt), whatever config.ini binds to them.
        const bool port_key = toggle || e.key.key == SDLK_ESCAPE;
        std::erase(held_keys_, key);
        if (down && !port_key) held_keys_.push_back(key);
        if (!down) std::erase(swallowed_keys_, key);
        else if (modal_ && !e.key.repeat && !port_key && std::ranges::find(swallowed_keys_, key) == swallowed_keys_.end())
          swallowed_keys_.push_back(key);
        key_bits_ = 0;
        for (int k : held_keys_)
          if (std::ranges::find(swallowed_keys_, k) == swallowed_keys_.end()) key_bits_ |= pad_bits_for_key(controls_, k);
        if (down && !e.key.repeat && toggle) set_fullscreen(!fullscreen_);
        if (!e.key.repeat) {
          HostKey host = HostKey::kNone;
          if (e.key.key == SDLK_ESCAPE) host = HostKey::kEscape;
          else if ((e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER) && !toggle) host = HostKey::kEnter;
          else if (e.key.key == SDLK_LEFT) host = HostKey::kLeft;
          else if (e.key.key == SDLK_RIGHT) host = HostKey::kRight;
          out = KeyEvent{key, down, port_key ? u16{0} : pad_bits_for_key(controls_, key), host};
          return true;
        }
        break;
      }
      case SDL_EVENT_GAMEPAD_ADDED:
        if (SDL_Gamepad* g = SDL_OpenGamepad(e.gdevice.which)) {
          gamepad_ = g;
          for (s16& a : axes_) a = 0;
          held_buttons_.clear();
          swallowed_buttons_.clear();
          button_bits_ = trigger_bits_ = swallowed_triggers_ = 0;
          stick_dir_ = 0;
        }
        break;
      case SDL_EVENT_GAMEPAD_REMOVED:
        if (gamepad_ != nullptr && SDL_GetGamepadID(gamepad_) == e.gdevice.which) {
          SDL_CloseGamepad(gamepad_);
          gamepad_ = nullptr;
          for (s16& a : axes_) a = 0;
          held_buttons_.clear();
          swallowed_buttons_.clear();
          button_bits_ = trigger_bits_ = swallowed_triggers_ = 0;
          stick_dir_ = 0;
        }
        break;
      case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
      case SDL_EVENT_GAMEPAD_BUTTON_UP: {
        const bool down = e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN;
        const int button = e.gbutton.button;
        std::erase(held_buttons_, button);
        if (down) held_buttons_.push_back(button);
        if (!down) std::erase(swallowed_buttons_, button);
        else if (modal_ && std::ranges::find(swallowed_buttons_, button) == swallowed_buttons_.end())
          swallowed_buttons_.push_back(button);
        button_bits_ = 0;
        for (int b : held_buttons_)
          if (std::ranges::find(swallowed_buttons_, b) == swallowed_buttons_.end())
            button_bits_ |= pad_bits_for_button(controls_, b);
        const HostKey host = button == SDL_GAMEPAD_BUTTON_DPAD_LEFT    ? HostKey::kLeft
                             : button == SDL_GAMEPAD_BUTTON_DPAD_RIGHT ? HostKey::kRight
                                                                       : HostKey::kNone;
        out = KeyEvent{0, down, pad_bits_for_button(controls_, button), host};
        return true;
      }
      case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        // The game's pad module reads the sticks (and turns the left one into the d-pad).
        if (gamepad_ != nullptr && SDL_GetGamepadID(gamepad_) == e.gaxis.which) {
          switch (e.gaxis.axis) {
            case SDL_GAMEPAD_AXIS_RIGHTX: axes_[0] = e.gaxis.value; break;
            case SDL_GAMEPAD_AXIS_RIGHTY: axes_[1] = e.gaxis.value; break;
            case SDL_GAMEPAD_AXIS_LEFTX: {
              axes_[2] = e.gaxis.value;
              // The quit prompt takes the left stick as Left / Right too, as the game's menus do.
              const int x = e.gaxis.value;
              const int dir = x <= -16000 ? -1 : x >= 16000 ? 1 : (x > -8000 && x < 8000) ? 0 : stick_dir_;
              if (dir != stick_dir_) {
                stick_dir_ = dir;
                if (modal_ && dir != 0) {
                  out = KeyEvent{0, true, 0, dir < 0 ? HostKey::kLeft : HostKey::kRight};
                  return true;
                }
              }
              break;
            }
            case SDL_GAMEPAD_AXIS_LEFTY: axes_[3] = e.gaxis.value; break;
            case SDL_GAMEPAD_AXIS_LEFT_TRIGGER:
            case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER: {
              // A trigger is a button past half way (with a little hysteresis).
              const int which = e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER ? kLeftTrigger : kRightTrigger;
              const u16 bits = pad_bits_for_button(controls_, which);
              const bool was = (trigger_bits_ & bits) != 0 && bits != 0;
              const bool now = e.gaxis.value > (was ? 12000 : 18000);
              trigger_bits_ = static_cast<u16>(now ? (trigger_bits_ | bits) : (trigger_bits_ & ~bits));
              if (now && !was && modal_) swallowed_triggers_ |= bits;  // the quit prompt's (set_modal)
              if (!now) swallowed_triggers_ &= static_cast<u16>(~bits);
              break;
            }
            default: break;
          }
        }
        break;
      default:
        break;
    }
  }
  return false;
}

void Window::present(const u32* pixels, int width, int height, float aspect) {
  int win_w = 0, win_h = 0;
  SDL_GetWindowSizeInPixels(window_, &win_w, &win_h);
  int view_w = win_w, view_h = win_h;
  picture_size(aspect, view_w, view_h);
  gl::Viewport(0, 0, win_w, win_h);
  gl::ClearColor(0, 0, 0, 1);
  gl::Clear(gl::COLOR_BUFFER_BIT);
  gl::Viewport((win_w - view_w) / 2, (win_h - view_h) / 2, view_w, view_h);

  gl::ActiveTexture(gl::TEXTURE0);
  gl::BindTexture(gl::TEXTURE_2D, texture_);
  gl::PixelStorei(gl::UNPACK_ALIGNMENT, 4);
  if (width != tex_width_ || height != tex_height_) {
    gl::TexImage2D(gl::TEXTURE_2D, 0, static_cast<int>(gl::RGBA8), width, height, 0, gl::RGBA, gl::UNSIGNED_BYTE,
                   pixels);
    tex_width_ = width;
    tex_height_ = height;
  } else {
    gl::TexSubImage2D(gl::TEXTURE_2D, 0, 0, 0, width, height, gl::RGBA, gl::UNSIGNED_BYTE, pixels);
  }
  gl::UseProgram(program_);
  gl::Uniform1i(gl::GetUniformLocation(program_, "image"), 0);
  gl::BindVertexArray(vao_);
  gl::DrawArrays(gl::TRIANGLES, 0, 3);
  SDL_GL_SwapWindow(window_);
}

bool Window::read_pixels(std::vector<u32>& out, int& width, int& height) const {
  if (!context_) return false;
  SDL_GetWindowSizeInPixels(window_, &width, &height);
  if (width <= 0 || height <= 0) return false;
  std::vector<u32> raw(static_cast<size_t>(width) * height);
  gl::PixelStorei(gl::PACK_ALIGNMENT, 4);
  gl::ReadPixels(0, 0, width, height, gl::RGBA, gl::UNSIGNED_BYTE, raw.data());
  out.resize(raw.size());
  for (int y = 0; y < height; ++y)  // GL rows run bottom-up; PNGs want top row first.
    std::memcpy(&out[static_cast<size_t>(y) * width], &raw[static_cast<size_t>(height - 1 - y) * width],
                static_cast<size_t>(width) * 4);
  return true;
}

void Window::set_title(const std::string& title) { SDL_SetWindowTitle(window_, title.c_str()); }

void Window::set_fullscreen(bool on) {
  fullscreen_ = on;
  SDL_SetWindowFullscreen(window_, on);
  // The game has no use for the mouse: no pointer over the full-screen picture.
  if (on) SDL_HideCursor();
  else SDL_ShowCursor();
}

}  // namespace ss::platform
