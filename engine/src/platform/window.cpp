#include "platform/window.h"

#include <SDL3/SDL.h>

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

Window::~Window() {
  if (context_) {
    if (texture_) gl::DeleteTextures(1, &texture_);
    if (vao_) gl::DeleteVertexArrays(1, &vao_);
    if (program_) gl::DeleteProgram(program_);
    SDL_GL_DestroyContext(static_cast<SDL_GLContext>(context_));
  }
  if (window_) SDL_DestroyWindow(window_);
  SDL_Quit();
}

bool Window::create(const std::string& title, int width, int height, std::string* error) {
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
    if (error) *error = std::string("SDL_Init: ") + SDL_GetError();
    return false;
  }
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  window_ = SDL_CreateWindow(title.c_str(), width, height,
                             SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
  if (!window_) {
    if (error) *error = std::string("SDL_CreateWindow: ") + SDL_GetError();
    return false;
  }
  context_ = SDL_GL_CreateContext(window_);
  if (!context_) {
    if (error) *error = std::string("SDL_GL_CreateContext: ") + SDL_GetError();
    return false;
  }
  SDL_GL_SetSwapInterval(1);
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
          fullscreen_ = !fullscreen_;
          SDL_SetWindowFullscreen(window_, fullscreen_);
          if (e.key.key == SDLK_RETURN) pad_ &= static_cast<u16>(~kPadStart);  // Alt+Enter is not Start
        }
        if (!e.key.repeat) {
          out = KeyEvent{static_cast<int>(e.key.key), down};
          return true;
        }
        break;
      }
      case SDL_EVENT_GAMEPAD_ADDED:
        SDL_OpenGamepad(e.gdevice.which);
        break;
      case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
      case SDL_EVENT_GAMEPAD_BUTTON_UP: {
        const u16 bit = gamepad_to_pad(e.gbutton.button);
        pad_ = e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ? (pad_ | bit) : (pad_ & ~bit);
        break;
      }
      case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
        // The left stick doubles as the d-pad (the game reads a digital pad).
        constexpr int kThreshold = 16000;
        const int v = e.gaxis.value;
        if (e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX) {
          stick_ = static_cast<u16>((stick_ & ~(kPadLeft | kPadRight)) | (v < -kThreshold ? kPadLeft : 0) |
                                    (v > kThreshold ? kPadRight : 0));
        } else if (e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY) {
          stick_ = static_cast<u16>((stick_ & ~(kPadUp | kPadDown)) | (v < -kThreshold ? kPadUp : 0) |
                                    (v > kThreshold ? kPadDown : 0));
        }
        break;
      }
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

}  // namespace ss::platform
