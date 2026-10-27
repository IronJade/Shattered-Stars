// Shattered Stars Setup: the settings window for players (port feature).
//
//   shattered-stars-setup                    (setup.exe on Windows)
//   shattered-stars-setup --from-game        the game opened it and waits: Save & Play
//                                            returns to the game (exit 0), Quit exits 1
//   shattered-stars-setup --version / --help   print the version (or usage) and exit
//
// It edits config.ini (launcher/config.h): the disc images, display, sound, memory cards
// and the mods folder; setup_ui.h has the pages.
//
// Testing (never needs a click; run with SDL_VIDEO_DRIVER=offscreen):
//   --screenshot FILE    render offscreen, write the window as a PNG after --frames N
//                        frames (default 6) and exit; nothing is started or opened
//   --page NAME          discs | display | sound | controls | gameplay | memcards | mods | archipelago
//   --set KEY=VALUE      start as if the player had set KEY (not saved unless Save is pressed)
//   --state NAME         quit-confirm | game-missing (show that dialog); capture-key /
//                        capture-pad (Controls: waiting for a key / button for Cross)
//   --do A,B,...         actions, one per frame: autodetect, save, play, quit, close,
//                        set:KEY=VALUE, render-tracker-maps (Archipelago page; the PopTracker
//                        map images from the disc, tools/tracker_maps.h, on a thread of its
//                        own), wait-tracker-render[:N] (hold the script until the render is
//                        over, or N images are done; those frames don't count for --frames)
//                        (also SS_SETUP_SCRIPT="A,B,...", which reaches setup when the game
//                        starts it)
//   --renderer gl|sdl    OpenGL 3.3 (default) or SDL's renderer (the fallback when OpenGL
//                        can't start)
//   --frames N           stop after N frames

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"
#include "launcher/config.h"
#include "launcher/process.h"
#include "launcher/report.h"
#include "platform/app_icon.h"
#include "setup/setup_ui.h"
#include "util/png.h"
#include "util/utf8.h"

namespace {

using namespace ss;

int usage() {
  std::fprintf(stderr, "Shattered Stars Setup %s\n", SS_VERSION);
  std::fprintf(stderr,
               "usage: shattered-stars-setup [--help] [--version] [--from-game] [--screenshot FILE] [--frames N] [--page NAME]\n"
               "                             [--set KEY=VALUE] [--state NAME] [--do A,B,...] [--renderer gl|sdl]\n");
  return 2;
}

// "a, b;c" -> {"a", "b", "c"} (items trimmed; spaces inside an item, as in a path, stay).
std::vector<std::string> split(const std::string& s) {
  std::vector<std::string> out;
  std::string item;
  auto flush = [&] {
    const auto first = item.find_first_not_of(' '), last = item.find_last_not_of(' ');
    if (first != std::string::npos) out.push_back(item.substr(first, last - first + 1));
    item.clear();
  };
  for (char c : s) {
    if (c == ',' || c == ';') flush();
    else item += c;
  }
  flush();
  return out;
}

// The first font file that exists: the system's UI font, so setup looks at home there.
std::string find_font(bool bold) {
  std::vector<std::string> candidates;
#ifdef _WIN32
  std::string windir = "C:\\Windows";
  if (util::env_set("WINDIR")) windir = *util::env("WINDIR");
  for (const char* f : bold ? std::vector<const char*>{"segoeuib.ttf", "tahomabd.ttf", "arialbd.ttf"}
                            : std::vector<const char*>{"segoeui.ttf", "tahoma.ttf", "arial.ttf"})
    candidates.push_back(windir + "\\Fonts\\" + f);
#else
  const char* regular[] = {
      "/usr/share/fonts/google-noto/NotoSans-Regular.ttf",
      "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
      "/usr/share/fonts/noto/NotoSans-Regular.ttf",
      "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf",
      "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
      "/usr/share/fonts/TTF/DejaVuSans.ttf",
      "/usr/share/fonts/liberation-sans-fonts/LiberationSans-Regular.ttf",
      "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
  };
  const char* heavy[] = {
      "/usr/share/fonts/google-noto/NotoSans-Bold.ttf",
      "/usr/share/fonts/truetype/noto/NotoSans-Bold.ttf",
      "/usr/share/fonts/noto/NotoSans-Bold.ttf",
      "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans-Bold.ttf",
      "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
      "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf",
      "/usr/share/fonts/liberation-sans-fonts/LiberationSans-Bold.ttf",
      "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
  };
  for (const char* f : bold ? std::vector<const char*>(std::begin(heavy), std::end(heavy))
                            : std::vector<const char*>(std::begin(regular), std::end(regular)))
    candidates.push_back(f);
#endif
  for (const std::string& f : candidates) {
    std::error_code ec;
    if (std::filesystem::is_regular_file(launcher::utf8_path(f), ec)) return f;
  }
  return {};
}

void apply_theme(float scale) {
  ImGuiStyle& style = ImGui::GetStyle();
  ImGui::StyleColorsDark(&style);
  style.WindowRounding = 0;
  style.ChildRounding = 0;
  style.FrameRounding = 5;
  style.PopupRounding = 8;
  style.GrabRounding = 4;
  style.ScrollbarRounding = 6;
  style.TabRounding = 5;
  style.WindowPadding = ImVec2(18, 16);
  style.FramePadding = ImVec2(10, 6);
  style.ItemSpacing = ImVec2(10, 8);
  style.ItemInnerSpacing = ImVec2(8, 6);
  style.ScrollbarSize = 13;
  style.GrabMinSize = 14;
  style.FrameBorderSize = 1;
  style.PopupBorderSize = 1;
  ImVec4* c = style.Colors;
  auto rgb = [](int r, int g, int b, float a = 1.0f) { return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a); };
  c[ImGuiCol_Text] = rgb(230, 233, 240);
  c[ImGuiCol_TextDisabled] = rgb(138, 147, 168);
  c[ImGuiCol_WindowBg] = rgb(17, 21, 31);
  c[ImGuiCol_ChildBg] = rgb(17, 21, 31, 0);
  c[ImGuiCol_PopupBg] = rgb(24, 29, 42);
  c[ImGuiCol_ModalWindowDimBg] = rgb(0, 0, 0, 0.55f);
  c[ImGuiCol_Border] = rgb(48, 56, 76);
  c[ImGuiCol_FrameBg] = rgb(27, 33, 48);
  c[ImGuiCol_FrameBgHovered] = rgb(35, 43, 62);
  c[ImGuiCol_FrameBgActive] = rgb(42, 52, 74);
  c[ImGuiCol_TitleBg] = rgb(24, 29, 42);
  c[ImGuiCol_TitleBgActive] = rgb(30, 40, 64);
  c[ImGuiCol_Button] = rgb(40, 48, 68);
  c[ImGuiCol_ButtonHovered] = rgb(52, 63, 90);
  c[ImGuiCol_ButtonActive] = rgb(62, 76, 108);
  c[ImGuiCol_CheckMark] = rgb(110, 165, 245);
  c[ImGuiCol_SliderGrab] = rgb(80, 135, 225);
  c[ImGuiCol_SliderGrabActive] = rgb(110, 165, 245);
  c[ImGuiCol_Header] = rgb(48, 72, 120);
  c[ImGuiCol_HeaderHovered] = rgb(56, 84, 138);
  c[ImGuiCol_HeaderActive] = rgb(64, 96, 156);
  c[ImGuiCol_Separator] = rgb(44, 52, 72);
  c[ImGuiCol_ScrollbarBg] = rgb(17, 21, 31, 0);
  c[ImGuiCol_ScrollbarGrab] = rgb(52, 60, 82);
  c[ImGuiCol_NavCursor] = rgb(110, 165, 245);
  c[ImGuiCol_TextSelectedBg] = rgb(61, 123, 217, 0.45f);
  style.ScaleAllSizes(scale);
  style.FontScaleDpi = scale;
}

struct Gl {  // the few GL calls setup makes itself (ImGui's backend loads its own)
  using Viewport = void (*)(int, int, int, int);
  using ClearColor = void (*)(float, float, float, float);
  using Clear = void (*)(unsigned);
  using PixelStorei = void (*)(unsigned, int);
  using ReadPixels = void (*)(int, int, int, int, unsigned, unsigned, void*);
  using GenTextures = void (*)(int, unsigned*);
  using BindTexture = void (*)(unsigned, unsigned);
  using TexParameteri = void (*)(unsigned, unsigned, int);
  using TexImage2D = void (*)(unsigned, int, int, int, int, int, unsigned, unsigned, const void*);
  using DeleteTextures = void (*)(int, const unsigned*);
  Viewport viewport = nullptr;
  ClearColor clear_color = nullptr;
  Clear clear = nullptr;
  PixelStorei pixel_store = nullptr;
  ReadPixels read_pixels = nullptr;
  GenTextures gen_textures = nullptr;
  BindTexture bind_texture = nullptr;
  TexParameteri tex_parameter = nullptr;
  TexImage2D tex_image = nullptr;
  DeleteTextures delete_textures = nullptr;
  bool load() {
    auto get = [](const char* name) { return SDL_GL_GetProcAddress(name); };
    viewport = reinterpret_cast<Viewport>(get("glViewport"));
    clear_color = reinterpret_cast<ClearColor>(get("glClearColor"));
    clear = reinterpret_cast<Clear>(get("glClear"));
    pixel_store = reinterpret_cast<PixelStorei>(get("glPixelStorei"));
    read_pixels = reinterpret_cast<ReadPixels>(get("glReadPixels"));
    gen_textures = reinterpret_cast<GenTextures>(get("glGenTextures"));
    bind_texture = reinterpret_cast<BindTexture>(get("glBindTexture"));
    tex_parameter = reinterpret_cast<TexParameteri>(get("glTexParameteri"));
    tex_image = reinterpret_cast<TexImage2D>(get("glTexImage2D"));
    delete_textures = reinterpret_cast<DeleteTextures>(get("glDeleteTextures"));
    return viewport && clear_color && clear && pixel_store && read_pixels && gen_textures && bind_texture &&
           tex_parameter && tex_image && delete_textures;
  }
};

constexpr unsigned kGlColorBufferBit = 0x4000, kGlRgba = 0x1908, kGlUnsignedByte = 0x1401, kGlPackAlignment = 0x0D05,
                   kGlUnpackAlignment = 0x0CF5, kGlTexture2D = 0x0DE1, kGlMinFilter = 0x2801, kGlMagFilter = 0x2800,
                   kGlLinear = 0x2601, kGlWrapS = 0x2802, kGlWrapT = 0x2803, kGlClampToEdge = 0x812F,
                   kGlRgba8 = 0x8058;

}  // namespace

int main(int argc, char** argv) try {
  // The arguments as UTF-8: on Windows argv is in the ANSI code page (util/utf8.h).
  util::Arguments utf8_args(argc, argv);
  argc = utf8_args.argc();
  argv = utf8_args.argv();
  launcher::attach_console(false);  // started from a terminal: messages go there
  for (int i = 1; i < argc; ++i) {  // --version / --help: print and exit, before anything else
    if (std::strcmp(argv[i], "--version") == 0) {
      std::fprintf(stderr, "Shattered Stars Setup %s\n", SS_VERSION);
      return 0;
    }
    if (std::strcmp(argv[i], "--help") == 0) {
      usage();
      return 0;
    }
  }
  setup::Options options;
  std::string screenshot, renderer_choice = "gl";
  int frames = 0;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    const bool has_value = i + 1 < argc;
    if (arg == "--from-game") {
      options.from_game = true;
    } else if (arg == "--screenshot" && has_value) {
      screenshot = argv[++i];
      options.screenshot = true;
    } else if (arg == "--frames" && has_value) {
      frames = std::atoi(argv[++i]);
    } else if (arg == "--page" && has_value) {
      if (!setup::parse_page(argv[++i], &options.page)) return usage();
    } else if (arg == "--set" && has_value) {
      const std::string kv = argv[++i];
      const auto eq = kv.find('=');
      if (eq == std::string::npos) return usage();
      options.sets.emplace_back(kv.substr(0, eq), kv.substr(eq + 1));
    } else if (arg == "--state" && has_value) {
      options.state = argv[++i];
    } else if (arg == "--do" && has_value) {
      for (const std::string& a : split(argv[++i])) options.actions.push_back(a);
    } else if (arg == "--renderer" && has_value) {
      renderer_choice = argv[++i];
    } else {
      return usage();
    }
  }
  if (util::env_set("SS_SETUP_SCRIPT"))
    for (const std::string& a : split(*util::env("SS_SETUP_SCRIPT"))) options.actions.push_back(a);
  if (!screenshot.empty() && frames <= 0) frames = 6;
  const int quit_code = options.from_game ? launcher::kSetupQuit : 1;

  // Its name, version and app id on the desktop (platform/app_icon.h): its own taskbar entry.
  platform::set_app_metadata(platform::kSetupName, SS_VERSION, platform::kSetupAppId, false);
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
    launcher::fatal(std::string("Setup could not start (SDL_Init: ") + SDL_GetError() + ").");
    return quit_code;
  }
  float scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
  if (!(scale > 0.5f && scale < 8.0f)) scale = 1.0f;
  int width = static_cast<int>(920 * scale), height = static_cast<int>(660 * scale);
  if (SDL_Rect usable; SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &usable) && usable.w > 0) {
    width = std::min(width, usable.w * 95 / 100);
    height = std::min(height, usable.h * 92 / 100);
  }
  const char* title = "Shattered Stars Setup";
  const SDL_WindowFlags base_flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN;

  // OpenGL 3.3 when it starts; SDL's own renderer (Direct3D, Vulkan or software) otherwise,
  // so setup opens even where the game's OpenGL doesn't (and the player can read why).
  SDL_Window* window = nullptr;
  SDL_GLContext gl_context = nullptr;
  SDL_Renderer* renderer = nullptr;
  Gl gl;
  if (renderer_choice != "sdl") {
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    window = SDL_CreateWindow(title, width, height, base_flags | SDL_WINDOW_OPENGL);
    if (window) gl_context = SDL_GL_CreateContext(window);
    if (gl_context && (!SDL_GL_MakeCurrent(window, gl_context) || !gl.load())) {
      SDL_GL_DestroyContext(gl_context);
      gl_context = nullptr;
    }
    if (!gl_context) {
      std::fprintf(stderr, "setup: OpenGL unavailable (%s); using SDL's renderer\n", SDL_GetError());
      if (window) SDL_DestroyWindow(window);
      window = nullptr;
    }
  }
  if (!gl_context) {
    window = SDL_CreateWindow(title, width, height, base_flags);
    if (window) renderer = SDL_CreateRenderer(window, nullptr);
    if (!renderer) {
      launcher::fatal(std::string("Setup could not open its window (") + SDL_GetError() + ").");
      SDL_Quit();
      return quit_code;
    }
    SDL_SetRenderVSync(renderer, 1);
  } else {
    SDL_GL_SetSwapInterval(1);
  }
  platform::set_window_icon(window);  // the taskbar's and title bar's icon
  SDL_SetWindowMinimumSize(window, static_cast<int>(640 * scale), static_cast<int>(480 * scale));
  SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
  SDL_ShowWindow(window);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr;  // nothing written next to the program
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
  apply_theme(scale);

  setup::Fonts fonts;
  fonts.size = 17.0f;
  const std::string regular = find_font(false), bold = find_font(true);
  if (!regular.empty()) fonts.regular = io.Fonts->AddFontFromFileTTF(regular.c_str(), fonts.size);
  if (!fonts.regular) fonts.regular = io.Fonts->AddFontDefaultVector();
  if (!bold.empty()) fonts.bold = io.Fonts->AddFontFromFileTTF(bold.c_str(), fonts.size);
  if (!fonts.bold) fonts.bold = fonts.regular;
  io.FontDefault = fonts.regular;
  ImGui::GetStyle().FontSizeBase = fonts.size;

  if (gl_context) {
    ImGui_ImplSDL3_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init("#version 330 core");
  } else {
    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);
  }

  // Pictures (the title logo from the player's disc), filtered linearly when scaled.
  setup::Textures textures;
  if (gl_context) {
    textures.create = [&gl](const u32* rgba, int w, int h) -> u64 {
      unsigned id = 0;
      gl.gen_textures(1, &id);
      gl.bind_texture(kGlTexture2D, id);
      gl.tex_parameter(kGlTexture2D, kGlMinFilter, static_cast<int>(kGlLinear));
      gl.tex_parameter(kGlTexture2D, kGlMagFilter, static_cast<int>(kGlLinear));
      gl.tex_parameter(kGlTexture2D, kGlWrapS, static_cast<int>(kGlClampToEdge));
      gl.tex_parameter(kGlTexture2D, kGlWrapT, static_cast<int>(kGlClampToEdge));
      gl.pixel_store(kGlUnpackAlignment, 4);
      gl.tex_image(kGlTexture2D, 0, static_cast<int>(kGlRgba8), w, h, 0, kGlRgba, kGlUnsignedByte, rgba);
      return id;
    };
    textures.destroy = [&gl](u64 id) {
      const unsigned name = static_cast<unsigned>(id);
      gl.delete_textures(1, &name);
    };
  } else {
    textures.create = [renderer](const u32* rgba, int w, int h) -> u64 {
      SDL_Texture* t = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, w, h);
      if (!t) return 0;
      SDL_UpdateTexture(t, nullptr, rgba, w * 4);
      SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR);
      SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
      return static_cast<u64>(reinterpret_cast<uintptr_t>(t));
    };
    textures.destroy = [](u64 id) { SDL_DestroyTexture(reinterpret_cast<SDL_Texture*>(static_cast<uintptr_t>(id))); };
  }

  int exit_code = 0;
  {
    setup::App app(window, fonts, textures, options);
    int frame = 0, busy = 8;  // frames still to draw after the last event
    for (bool running = true; running;) {
      SDL_Event e;
      bool any = false;
      auto handle = [&](const SDL_Event& ev) {
        any = true;
        if (app.capture_event(ev)) return;  // a key or gamepad button being bound (Controls)
        ImGui_ImplSDL3_ProcessEvent(&ev);
        if (ev.type == SDL_EVENT_QUIT ||
            (ev.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && ev.window.windowID == SDL_GetWindowID(window)))
          app.request_close();
        if (ev.type == SDL_EVENT_WINDOW_FOCUS_GAINED) app.forget_file_checks();
      };
      // Idle: wait for an event, but draw at least 4 times a second (10 while the tracker
      // maps render, for the progress bar).
      if (screenshot.empty() && busy <= 0 && SDL_WaitEventTimeout(&e, app.working() ? 100 : 250)) handle(e);
      while (SDL_PollEvent(&e)) handle(e);
      busy = any ? 8 : busy - 1;
      if (screenshot.empty() && (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED)) {
        SDL_Delay(50);
        continue;
      }
      if (gl_context) ImGui_ImplOpenGL3_NewFrame();
      else ImGui_ImplSDLRenderer3_NewFrame();
      ImGui_ImplSDL3_NewFrame();
      ImGui::NewFrame();
      app.frame();
      ImGui::Render();
      if (app.waiting()) {  // a scripted wait-tracker-render: not a frame of --frames
        if (!screenshot.empty()) SDL_Delay(10);  // test runs draw unthrottled: leave the CPU to the render
      } else {
        ++frame;
      }

      int pw = 0, ph = 0;
      SDL_GetWindowSizeInPixels(window, &pw, &ph);
      const bool grab = !screenshot.empty() && frame >= frames;
      std::vector<u32> pixels;
      if (gl_context) {
        gl.viewport(0, 0, pw, ph);
        gl.clear_color(17 / 255.0f, 21 / 255.0f, 31 / 255.0f, 1);
        gl.clear(kGlColorBufferBit);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        if (grab) {
          std::vector<u32> raw(static_cast<size_t>(pw) * ph);
          gl.pixel_store(kGlPackAlignment, 4);
          gl.read_pixels(0, 0, pw, ph, kGlRgba, kGlUnsignedByte, raw.data());
          pixels.resize(raw.size());
          for (int y = 0; y < ph; ++y)  // GL rows run bottom-up
            std::memcpy(&pixels[static_cast<size_t>(y) * pw], &raw[static_cast<size_t>(ph - 1 - y) * pw],
                        static_cast<size_t>(pw) * 4);
        }
        SDL_GL_SwapWindow(window);
      } else {
        SDL_SetRenderScale(renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
        SDL_SetRenderDrawColor(renderer, 17, 21, 31, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        if (grab) {
          if (SDL_Surface* shot = SDL_RenderReadPixels(renderer, nullptr)) {
            if (SDL_Surface* rgba = SDL_ConvertSurface(shot, SDL_PIXELFORMAT_RGBA32)) {
              pw = rgba->w;
              ph = rgba->h;
              pixels.resize(static_cast<size_t>(pw) * ph);
              for (int y = 0; y < ph; ++y)
                std::memcpy(&pixels[static_cast<size_t>(y) * pw], static_cast<const u8*>(rgba->pixels) + static_cast<size_t>(y) * rgba->pitch,
                            static_cast<size_t>(pw) * 4);
              SDL_DestroySurface(rgba);
            }
            SDL_DestroySurface(shot);
          }
        }
        SDL_RenderPresent(renderer);
      }
      if (grab) {
        const bool ok = !pixels.empty() && util::write_png(launcher::utf8_path(screenshot), pixels, pw, ph);
        std::printf("setup: %s %s (%dx%d, %s)\n", ok ? "wrote" : "could not write", screenshot.c_str(), pw, ph,
                    gl_context ? "OpenGL" : "SDL renderer");
        exit_code = ok ? 0 : 3;
        break;
      }
      if (app.done()) {
        exit_code = app.exit_code();
        running = false;
      }
      if (frames > 0 && frame >= frames && screenshot.empty()) {
        exit_code = app.done() ? app.exit_code() : quit_code;
        running = false;
      }
    }
  }

  if (gl_context) ImGui_ImplOpenGL3_Shutdown();
  else ImGui_ImplSDLRenderer3_Shutdown();
  ImGui_ImplSDL3_Shutdown();
  ImGui::DestroyContext();
  if (gl_context) SDL_GL_DestroyContext(gl_context);
  if (renderer) SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return exit_code;
} catch (const std::filesystem::filesystem_error& e) {
  // A path that can't be used (util/utf8.h converts without throwing; this is the net): said,
  // and a clean exit (1: as Quit, so a game waiting on setup stops too) instead of std::terminate.
  launcher::fatal(std::string("A file or folder could not be used: ") + e.what());
  return 1;
}
