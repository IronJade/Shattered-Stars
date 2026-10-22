// Shattered Stars: Star Ocean: The Second Story, hand-ported to run natively.
//
//   shattered-stars [--disc1 IMAGE] [--disc2 IMAGE]      play (paths are remembered)
//   shattered-stars --viewer [--entry N]                 texture viewer
//   shattered-stars --dump DIR --entry N                 write an entry's textures as PNGs
//
// Display: --no-dither / --dither (remembered); --reference-dither dithers like
// PCSX-Redux's software renderer (gouraud only) for comparisons, not remembered.
// Testing options: --headless (no window), --frames N (stop after N vblanks),
// --capture DIR --capture-frames A,B,... (save the display on those vblanks),
// --input FRAME:buttons,... (scripted controller, e.g. 900:down,906:none,930:cross).
// SS_VRAM_PNG=file writes VRAM as a 1024x512 PNG when the run ends.
//
// Audio: --audio / --no-audio (remembered; on by default with a window, off when
// headless unless --audio is given), --audio-wav FILE or SS_AUDIO_WAV=FILE (record
// the sound output as a WAV file, also headless).
//
// Memory cards: --memcard1 FILE / --memcard2 FILE (raw 128 KB images, the .mcd/.mcr
// format emulators use; remembered; "none" removes the card). Slot 1 defaults to
// memcard1.mcd next to config.ini, created formatted on first use. --no-memcard runs
// with both slots empty (testing; not remembered). The environment variables
// SS_MEMCARD1 / SS_MEMCARD2 override the remembered paths for one run.
//
// Window: F11 or Alt+Enter toggles fullscreen; Esc quits. Keys: arrows, Z (Cross), X (Circle),
// A (Square), S (Triangle), Enter (Start), Backspace (Select), Q/W (L1/R1), 1/2 (L2/R2);
// gamepads map directly (left stick = d-pad).
//
// IMAGE is a .cue, raw .bin or .iso of your own disc.

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

#include "disc/archive.h"
#include "disc/disc_image.h"
#include "game/boot/system.h"
#include "game/common/memcard.h"
#include "platform/audio.h"
#include "platform/window.h"
#include "runtime/boot_exe.h"
#include "runtime/runtime.h"
#include "tools/viewer.h"
#include "util/png.h"

namespace {

using namespace ss;

std::filesystem::path config_path() {
  if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg)
    return std::filesystem::path(xdg) / "shattered-stars" / "config.ini";
  if (const char* appdata = std::getenv("APPDATA"); appdata && *appdata)
    return std::filesystem::path(appdata) / "ShatteredStars" / "config.ini";
  if (const char* home = std::getenv("HOME"); home && *home)
    return std::filesystem::path(home) / ".config" / "shattered-stars" / "config.ini";
  return "shattered-stars.ini";
}

std::map<std::string, std::string> load_config() {
  std::map<std::string, std::string> out;
  std::ifstream in(config_path());
  for (std::string line; std::getline(in, line);) {
    const auto eq = line.find('=');
    if (eq != std::string::npos && line[0] != '#') out[line.substr(0, eq)] = line.substr(eq + 1);
  }
  return out;
}

void save_config(const std::map<std::string, std::string>& config) {
  const auto path = config_path();
  std::filesystem::create_directories(path.parent_path());
  std::ofstream out(path);
  out << "# Shattered Stars settings\n";
  for (const auto& [k, v] : config) out << k << '=' << v << '\n';
}

int usage(const char* argv0) {
  std::fprintf(stderr,
               "usage: %s [--disc1 IMAGE] [--disc2 IMAGE] [--no-dither|--dither|--reference-dither] [--viewer] [--dump DIR] [--entry N]\n"
               "       [--memcard1 FILE] [--memcard2 FILE] [--no-memcard] [--headless] [--frames N] [--capture DIR --capture-frames A,B,...] [--input F:buttons,...]\n"
               "       [--audio|--no-audio] [--audio-wav FILE]\n",
               argv0);
  return 2;
}

}  // namespace

int main(int argc, char** argv) {
  auto config = load_config();
  std::filesystem::path dump_dir, capture_dir;
  std::string capture_frames, input_script;
  bool viewer = false, headless = false, config_changed = false, reference_dither = false,
       no_memcard = false;
  int entry = 10;
  u64 frames = 0;
  bool audio_asked = false;
  std::filesystem::path audio_wav;
  if (const char* wav = std::getenv("SS_AUDIO_WAV"); wav && *wav) audio_wav = wav;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    auto next = [&]() -> const char* { return argv[++i]; };
    const bool has_value = i + 1 < argc;
    if ((arg == "--disc1" || arg == "--disc") && has_value) {
      config["disc1"] = std::filesystem::absolute(next()).string();
      config_changed = true;
    } else if (arg == "--disc2" && has_value) {
      config["disc2"] = std::filesystem::absolute(next()).string();
      config_changed = true;
    } else if (arg == "--viewer") {
      viewer = true;
    } else if (arg == "--dump" && has_value) {
      dump_dir = next();
    } else if (arg == "--entry" && has_value) {
      entry = std::atoi(next());
    } else if (arg == "--no-dither") {
      config["dither"] = "0";
      config_changed = true;
    } else if (arg == "--dither") {
      config["dither"] = "1";
      config_changed = true;
    } else if ((arg == "--memcard1" || arg == "--memcard2") && has_value) {
      const std::string value = next();
      config[arg.substr(2)] = value == "none" ? "none" : std::filesystem::absolute(value).string();
      config_changed = true;
    } else if (arg == "--no-memcard") {
      no_memcard = true;
    } else if (arg == "--reference-dither") {
      reference_dither = true;
    } else if (arg == "--headless") {
      headless = true;
    } else if (arg == "--frames" && has_value) {
      frames = std::strtoull(next(), nullptr, 10);
    } else if (arg == "--capture" && has_value) {
      capture_dir = next();
    } else if (arg == "--capture-frames" && has_value) {
      capture_frames = next();
    } else if (arg == "--input" && has_value) {
      input_script = next();
    } else if (arg == "--audio") {
      config["audio"] = "1";
      config_changed = true;
      audio_asked = true;
    } else if (arg == "--no-audio") {
      config["audio"] = "0";
      config_changed = true;
    } else if (arg == "--audio-wav" && has_value) {
      audio_wav = next();
    } else {
      return usage(argv[0]);
    }
  }
  if (config["disc1"].empty()) {
    std::fprintf(stderr, "No disc image yet: run once with --disc1 \"/path/to/Disc 1.cue\" (it is remembered).\n");
    return usage(argv[0]);
  }
  if (config_changed) save_config(config);

  std::string error;
  auto disc = disc::DiscImage::open(config["disc1"], &error);
  disc::Archive archive;
  if (!disc || !archive.open(*disc, &error)) {
    std::fprintf(stderr, "%s: %s\n", config["disc1"].c_str(), error.c_str());
    return 1;
  }
  // The sound driver's tables (and libspu's reverb presets) are read from the boot executable.
  if (std::string exe_error; !runtime::boot_exe_open(*disc, &exe_error))
    std::fprintf(stderr, "%s: %s (no sound)\n", config["disc1"].c_str(), exe_error.c_str());
  if (!dump_dir.empty()) return tools::dump_textures(archive, entry, dump_dir);
  if (viewer) return tools::run_viewer(archive, entry, static_cast<int>(frames));

  {
    const char* env1 = std::getenv("SS_MEMCARD1");
    const char* env2 = std::getenv("SS_MEMCARD2");
    const std::string card1 = env1 ? env1
                              : config.count("memcard1") ? config["memcard1"]
                                                         : (config_path().parent_path() / "memcard1.mcd").string();
    const std::string card2 = env2 ? env2 : config.count("memcard2") ? config["memcard2"] : "none";
    game::memcard_set_images(no_memcard || card1 == "none" ? nullptr : card1.c_str(),
                             no_memcard || card2 == "none" ? nullptr : card2.c_str());
  }

  auto& rt = runtime::Runtime::get();
  rt.gpu.dithering_enabled = reference_dither || config["dither"] != "0";
  if (reference_dither) rt.gpu.dither_textured = false;
  rt.frame_limit = frames;
  rt.capture_dir = capture_dir;
  std::stringstream list(capture_frames);
  for (std::string n; std::getline(list, n, ',');)
    if (!n.empty()) rt.capture_frames.insert(std::strtoull(n.c_str(), nullptr, 10));

  // --input 900:down,906:none,930:cross+start
  const std::map<std::string, u16> button_names = {
      {"l2", platform::kPadL2},       {"r2", platform::kPadR2},         {"l1", platform::kPadL1},
      {"r1", platform::kPadR1},       {"triangle", platform::kPadTriangle}, {"circle", platform::kPadCircle},
      {"cross", platform::kPadCross}, {"square", platform::kPadSquare}, {"select", platform::kPadSelect},
      {"start", platform::kPadStart}, {"up", platform::kPadUp},         {"right", platform::kPadRight},
      {"down", platform::kPadDown},   {"left", platform::kPadLeft},     {"none", 0}};
  std::stringstream steps(input_script);
  for (std::string step; std::getline(steps, step, ',');) {
    const auto colon = step.find(':');
    if (colon == std::string::npos) continue;
    u16 buttons = 0;
    std::stringstream names(step.substr(colon + 1));
    for (std::string name; std::getline(names, name, '+');) {
      auto it = button_names.find(name);
      if (it == button_names.end()) {
        std::fprintf(stderr, "unknown button '%s' in --input\n", name.c_str());
        return 2;
      }
      buttons |= it->second;
    }
    rt.scripted_input[std::strtoull(step.substr(0, colon).c_str(), nullptr, 10)] = buttons;
  }

  platform::Window window;
  if (!headless) {
    if (!window.create("Star Ocean: The Second Story (Shattered Stars)", 960, 720, &error)) {
      std::fprintf(stderr, "%s\n", error.c_str());
      return 1;
    }
    rt.window = &window;
  }

  platform::AudioOptions audio;
  audio.device = config["audio"] != "0" && (!headless || audio_asked);
  audio.wav = audio_wav;
  if (std::string audio_error; !platform::audio_open(audio, &audio_error))
    std::fprintf(stderr, "audio: %s\n", audio_error.c_str());
  rt.audio_tick = platform::audio_vblank;

  try {
    game::Program program = game::boot_main(0, &archive);
    for (;;) {
      try {
        game::run_program(program);
        break;
      } catch (const game::ProgramSwitch& s) {
        program = s.next;
      }
    }
  } catch (const runtime::QuitRequested&) {
  }
  if (const char* vram_png = std::getenv("SS_VRAM_PNG")) {  // debugging: VRAM at exit
    std::vector<u32> pixels;
    rt.gpu.vram_to_rgba(pixels);
    util::write_png(vram_png, pixels, 1024, 512);
  }
  platform::audio_close();
  std::printf("stopped after %llu vblanks\n", static_cast<unsigned long long>(rt.vblanks()));
  return 0;
}
