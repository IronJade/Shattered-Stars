#include "platform/window.h"

#include <SDL3/SDL.h>

#include <algorithm>

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

u16 key_to_pad(SDL_Keycode key) {
  switch (key) {
    case SDLK_UP: return kPadUp;
    case SDLK_DOWN: return kPadDown;
    case SDLK_LEFT: return kPadLeft;
    case SDLK_RIGHT: return kPadRight;
    case SDLK_RETURN: return kPadStart;
    case SDLK_BACKSPACE: return kPadSelect;
    case SDLK_Z: return kPadCross;
    case SDLK_X: return kPadCircle;
    case SDLK_A: return kPadSquare;
    case SDLK_S: return kPadTriangle;
    case SDLK_Q: return kPadL1;
    case SDLK_W: return kPadR1;
    case SDLK_1: return kPadL2;
    case SDLK_2: return kPadR2;
    default: return 0;
  }
}

u16 gamepad_to_pad(Uint8 button) {
  switch (button) {
    case SDL_GAMEPAD_BUTTON_DPAD_UP: return kPadUp;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return kPadDown;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return kPadLeft;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return kPadRight;
    case SDL_GAMEPAD_BUTTON_START: return kPadStart;
    case SDL_GAMEPAD_BUTTON_BACK: return kPadSelect;
    case SDL_GAMEPAD_BUTTON_SOUTH: return kPadCross;
    case SDL_GAMEPAD_BUTTON_EAST: return kPadCircle;
    case SDL_GAMEPAD_BUTTON_WEST: return kPadSquare;
    case SDL_GAMEPAD_BUTTON_NORTH: return kPadTriangle;
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return kPadL1;
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return kPadR1;
    case SDL_GAMEPAD_BUTTON_LEFT_STICK: return kPadL3;
    case SDL_GAMEPAD_BUTTON_RIGHT_STICK: return kPadR3;
    default: return 0;
  }
}

}  // namespace

bool Window::sticks(u8 out[4]) const {
  if (gamepad_ == nullptr) return false;
  for (int i = 0; i < 4; ++i) out[i] = static_cast<u8>((axes_[i] + 0x8000) >> 8);
  return true;
}

void Window::rumble(int small, int large) {
  if (gamepad_ == nullptr) return;
  const Uint16 low = static_cast<Uint16>((large < 0 ? 0 : large > 255 ? 255 : large) * 257);
  const Uint16 high = small != 0 ? 0xFFFF : 0;
  // The console keeps the motors at these levels until the game changes them.
  SDL_RumbleGamepad(gamepad_, low, high, low == 0 && high == 0 ? 0 : 60000);
}

Window::~Window() {
  if (gamepad_) SDL_CloseGamepad(gamepad_);
  if (context_) {
    if (texture_) gl::DeleteTextures(1, &texture_);
    if (vao_) gl::DeleteVertexArrays(1, &vao_);
    if (program_) gl::DeleteProgram(program_);
    SDL_GL_DestroyContext(static_cast<SDL_GLContext>(context_));
  }
  if (window_) SDL_DestroyWindow(window_);
  SDL_Quit();
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
        if (const u16 bit = key_to_pad(e.key.key)) pad_ = down ? (pad_ | bit) : (pad_ & ~bit);
        if (down && e.key.key == SDLK_ESCAPE) open = false;
        if (down && !e.key.repeat &&
            (e.key.key == SDLK_F11 || (e.key.key == SDLK_RETURN && (e.key.mod & SDL_KMOD_ALT)))) {
          set_fullscreen(!fullscreen_);
          if (e.key.key == SDLK_RETURN) pad_ &= static_cast<u16>(~kPadStart);  // Alt+Enter is not Start
        }
        if (!e.key.repeat) {
          out = KeyEvent{static_cast<int>(e.key.key), down};
          return true;
        }
        break;
      }
      case SDL_EVENT_GAMEPAD_ADDED:
        if (SDL_Gamepad* g = SDL_OpenGamepad(e.gdevice.which)) {
          gamepad_ = g;
          for (s16& a : axes_) a = 0;
        }
        break;
      case SDL_EVENT_GAMEPAD_REMOVED:
        if (gamepad_ != nullptr && SDL_GetGamepadID(gamepad_) == e.gdevice.which) {
          SDL_CloseGamepad(gamepad_);
          gamepad_ = nullptr;
          for (s16& a : axes_) a = 0;
        }
        break;
      case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
      case SDL_EVENT_GAMEPAD_BUTTON_UP: {
        const u16 bit = gamepad_to_pad(e.gbutton.button);
        pad_ = e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ? (pad_ | bit) : (pad_ & ~bit);
        break;
      }
      case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        // The game's pad module reads the sticks (and turns the left one into the d-pad).
        if (gamepad_ != nullptr && SDL_GetGamepadID(gamepad_) == e.gaxis.which) {
          switch (e.gaxis.axis) {
            case SDL_GAMEPAD_AXIS_RIGHTX: axes_[0] = e.gaxis.value; break;
            case SDL_GAMEPAD_AXIS_RIGHTY: axes_[1] = e.gaxis.value; break;
            case SDL_GAMEPAD_AXIS_LEFTX: axes_[2] = e.gaxis.value; break;
            case SDL_GAMEPAD_AXIS_LEFTY: axes_[3] = e.gaxis.value; break;
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
  if (win_h > 0 && static_cast<float>(win_w) / win_h > aspect) {
    view_w = static_cast<int>(win_h * aspect);
  } else {
    view_h = static_cast<int>(win_w / aspect);
  }
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

void Window::set_title(const std::string& title) { SDL_SetWindowTitle(window_, title.c_str()); }

void Window::set_fullscreen(bool on) {
  fullscreen_ = on;
  SDL_SetWindowFullscreen(window_, on);
  // The game has no use for the mouse: no pointer over the full-screen picture.
  if (on) SDL_HideCursor();
  else SDL_ShowCursor();
}

}  // namespace ss::platform
