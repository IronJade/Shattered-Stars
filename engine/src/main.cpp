// Shattered Stars: Star Ocean: The Second Story, hand-ported to run natively.
//
//   shattered-stars [--disc1 IMAGE] [--disc2 IMAGE]      play (paths are remembered)
//   shattered-stars-setup (setup.exe on Windows)         the settings window for players
//
// Settings live in config.ini (launcher/config.h: XDG_CONFIG_HOME, %APPDATA%\ShatteredStars
// or ~/.config/shattered-stars), written by the setup program and by the remembered
// options below. Disc 1 must be a North American disc 1 image; when it isn't, the game
// looks in the `discs` folders (next to the game, next to config.ini), and then, on a
// windowed run, opens setup and carries on once a disc 1 is chosen. Headless runs and
// SS_DISC1 / SS_DISC2 runs never open setup and never write config.ini.
//   shattered-stars --viewer [--entry N]                 texture viewer
//   shattered-stars --dump DIR --entry N                 write an entry's textures as PNGs
//
// Display: --no-dither / --dither (remembered); --reference-dither draws like
// PCSX-Redux's software renderer for comparisons (gouraud-only dithering, blending before
// dithering, captures expanded as v << 3), not remembered.
// Testing options: --headless (no window), --frames N (stop after N vblanks),
// --map N (debug: new game as Claude, straight onto field map N),
// --map-pos X,Y,Z[,FACING[,CAMERA]] (debug: the leader's spot on the first map loaded,
// with --map or a loaded game),
// --battle ENC[:STAGE] [--battle-end FRAMES[:OUTCOME]] (debug: straight into a battle with a
// new game's party; see game/battle/battle_debug.h),
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
// Window: config.ini's window / window_scale / resolution / monitor / vsync keys (set in
// setup); F11 or Alt+Enter toggles fullscreen; Esc quits. Keys: arrows, Z (Cross), X (Circle),
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
#include "game/battle/battle_debug.h"
#include "game/boot/system.h"
#include "game/common/memcard.h"
#include "game/field/debug_console.h"
#include "launcher/config.h"
#include "launcher/discs.h"
#include "launcher/process.h"
#include "launcher/settings.h"
#include "platform/audio.h"
#include "platform/window.h"
#include "runtime/boot_exe.h"
#include "runtime/runtime.h"
#include "tools/viewer.h"
#include "util/png.h"

namespace {

using namespace ss;

bool env_set(const char* name) {
  const char* v = std::getenv(name);
  return v != nullptr && *v != 0;
}

void print_no_disc(const std::string& disc1) {
  if (!disc1.empty())
    std::fprintf(stderr, "Disc 1 \"%s\": %s\n", disc1.c_str(),
                 launcher::describe(launcher::identify_disc(launcher::utf8_path(disc1)), 1).c_str());
  std::fprintf(stderr, "No disc image yet: run %s, put your disc images in a \"discs\" folder next to the game, or\n"
                       "run once with --disc1 \"/path/to/Disc 1.cue\" (both are remembered).\n",
               launcher::kSetupProgram);
}

int usage(const char* argv0) {
  std::fprintf(stderr,
               "usage: %s [--disc1 IMAGE] [--disc2 IMAGE] [--no-dither|--dither|--reference-dither] [--viewer] [--dump DIR] [--entry N]\n"
               "       [--memcard1 FILE] [--memcard2 FILE] [--no-memcard] [--headless] [--frames N] [--map N] [--map-pos X,Y,Z[,F[,C]]] [--capture DIR --capture-frames A,B,...] [--input F:buttons,...]\n"
               "       [--audio|--no-audio] [--audio-wav FILE] [--mods DIR] [--debug]\n",
               argv0);
  return 2;
}

}  // namespace

int main(int argc, char** argv) {
  // 'saved' is config.ini as written back; 'config' the run's settings (saved, plus this
  // run's options and SS_DISC1 / SS_DISC2).
  launcher::Config saved = launcher::Config::load();
  launcher::Config config = saved;
  std::vector<std::string> remembered;  // keys the options changed
  auto remember = [&](const std::string& key, const std::string& value) {
    config.set(key, value);
    remembered.push_back(key);
  };
  std::filesystem::path dump_dir, capture_dir;
  std::string capture_frames, input_script;
  std::string battle_spec, battle_end;  // --battle ENC[:STAGE], --battle-end FRAMES[:OUTCOME]
  bool viewer = false, headless = false, config_changed = false, reference_dither = false,
       no_memcard = false, debug_console = false;
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
      remember("disc1", launcher::path_utf8(std::filesystem::absolute(next())));
      config_changed = true;
    } else if (arg == "--disc2" && has_value) {
      remember("disc2", launcher::path_utf8(std::filesystem::absolute(next())));
      config_changed = true;
    } else if (arg == "--viewer") {
      viewer = true;
    } else if (arg == "--dump" && has_value) {
      dump_dir = next();
    } else if (arg == "--entry" && has_value) {
      entry = std::atoi(next());
    } else if (arg == "--no-dither") {
      remember("dither", "0");
      config_changed = true;
    } else if (arg == "--dither") {
      remember("dither", "1");
      config_changed = true;
    } else if ((arg == "--memcard1" || arg == "--memcard2") && has_value) {
      const std::string value = next();
      remember(arg.substr(2), value == "none" ? "none" : launcher::path_utf8(std::filesystem::absolute(value)));
      config_changed = true;
    } else if (arg == "--no-memcard") {
      no_memcard = true;
    } else if (arg == "--reference-dither") {
      reference_dither = true;
    } else if (arg == "--headless") {
      headless = true;
    } else if (arg == "--battle" && has_value) {
      battle_spec = next();
    } else if (arg == "--battle-end" && has_value) {
      battle_end = next();
    } else if (arg == "--debug") {  // debug console on the terminal (game/field/debug_console.h)
      debug_console = true;
    } else if (arg == "--mods" && has_value) {  // world-edit patches (default: mods/ next to config.ini)
      runtime::Runtime::get().mods_dir = std::filesystem::absolute(next()).string();
    } else if (arg == "--map" && has_value) {
      runtime::Runtime::get().debug_start_map = std::atoi(next());
    } else if (arg == "--map-pos" && has_value) {  // X,Y,Z[,FACING[,CAMERA]] on the first map loaded
      auto& rt = runtime::Runtime::get();
      rt.debug_start_pos_set = std::sscanf(next(), "%d,%d,%d,%d,%d", &rt.debug_start_pos[0], &rt.debug_start_pos[1],
                                           &rt.debug_start_pos[2], &rt.debug_start_pos[3], &rt.debug_start_pos[4]) >= 3;
    } else if (arg == "--frames" && has_value) {
      frames = std::strtoull(next(), nullptr, 10);
    } else if (arg == "--capture" && has_value) {
      capture_dir = next();
    } else if (arg == "--capture-frames" && has_value) {
      capture_frames = next();
    } else if (arg == "--input" && has_value) {
      input_script = next();
    } else if (arg == "--audio") {
      remember("audio", "1");
      config_changed = true;
      audio_asked = true;
    } else if (arg == "--no-audio") {
      remember("audio", "0");
      config_changed = true;
    } else if (arg == "--audio-wav" && has_value) {
      audio_wav = next();
    } else {
      return usage(argv[0]);
    }
  }
  // Tests and scripts: SS_DISC1 / SS_DISC2 choose the images for this run only. Such runs,
  // and headless ones, never write config.ini and never open setup.
  const bool discs_from_env = env_set("SS_DISC1") || env_set("SS_DISC2");
  const bool may_write = !headless && !discs_from_env;
  if (config_changed && !headless) {
    for (const std::string& key : remembered) saved.set(key, config.get(key));
    if (std::string save_error; !saved.save(&save_error)) std::fprintf(stderr, "%s\n", save_error.c_str());
  }
  if (env_set("SS_DISC1")) config.set("disc1", std::getenv("SS_DISC1"));
  if (env_set("SS_DISC2")) config.set("disc2", std::getenv("SS_DISC2"));

  // Disc 1 (launcher/discs.h): the configured image if it is disc 1, else the first disc 1
  // in the `discs` folders (silently, remembered), else the setup program.
  {
    const auto folders = launcher::disc_folders(launcher::config_dir());
    bool write_back = false;
    if (!launcher::is_disc1(launcher::utf8_path(config.get("disc1")))) {
      const launcher::FoundDiscs found = launcher::find_discs(folders);
      if (!found.disc1.empty()) {
        config.set("disc1", found.disc1);
        saved.set("disc1", found.disc1);
        if (!found.disc2.empty() && launcher::disc_number(launcher::utf8_path(config.get("disc2"))) != 2) {
          config.set("disc2", found.disc2);
          saved.set("disc2", found.disc2);
        }
        write_back = true;
      }
    }
    if (!launcher::is_disc1(launcher::utf8_path(config.get("disc1"))) && may_write) {
      const std::filesystem::path setup = launcher::sibling_program(launcher::kSetupProgram);
      if (!setup.empty()) {
        std::fprintf(stderr, "No usable Disc 1 image yet: opening %s.\n", launcher::kSetupProgram);
        std::string setup_error;
        const int code = launcher::run_and_wait(setup, {"--from-game"}, &setup_error);
        if (code < 0) std::fprintf(stderr, "%s: %s\n", launcher::path_utf8(setup).c_str(), setup_error.c_str());
        if (code != launcher::kSetupPlay) {
          print_no_disc(config.get("disc1"));
          return 1;
        }
        // Setup wrote config.ini: everything (display, sound, cards) is read again.
        saved = launcher::Config::load();
        config = saved;
        write_back = false;
      }
    }
    if (!launcher::is_disc1(launcher::utf8_path(config.get("disc1")))) {
      print_no_disc(config.get("disc1"));
      return may_write ? 1 : usage(argv[0]);
    }
    // Disc 2 (optional): the configured image if it is disc 2, else one next to disc 1 or
    // in the `discs` folders.
    if (launcher::disc_number(launcher::utf8_path(config.get("disc2"))) != 2) {
      if (!config.get("disc2").empty())
        std::fprintf(stderr, "Disc 2 \"%s\": %s\n", config.get("disc2").c_str(),
                     launcher::describe(launcher::identify_disc(launcher::utf8_path(config.get("disc2"))), 2).c_str());
      std::string disc2 = launcher::find_disc2_near(launcher::utf8_path(config.get("disc1")));
      if (disc2.empty()) disc2 = launcher::find_discs(folders).disc2;
      config.set("disc2", disc2);
      if (!disc2.empty()) {
        saved.set("disc2", disc2);
        write_back = true;
      }
    }
    if (write_back && may_write) saved.save();
  }

  std::string error;
  auto disc = disc::DiscImage::open(launcher::utf8_path(config.get("disc1")), &error);
  disc::Archive archive;
  if (!disc || !archive.open(*disc, &error)) {
    std::fprintf(stderr, "%s: %s\n", config.get("disc1").c_str(), error.c_str());
    return 1;
  }
  // The sound driver's tables (and libspu's reverb presets) are read from the boot executable.
  if (std::string exe_error; !runtime::boot_exe_open(*disc, &exe_error))
    std::fprintf(stderr, "%s: %s (no sound)\n", config.get("disc1").c_str(), exe_error.c_str());
  // Disc 2 (optional, --disc2): the loader swaps to it when a save or the story needs it
  // (game::loader_insert_disc); without it the game stays on disc 1, whose archive holds
  // the same field and battle data.
  std::unique_ptr<disc::DiscImage> disc2;
  disc::Archive archive2;
  game::loader_register_disc(1, &archive);
  if (!config.get("disc2").empty()) {
    disc2 = disc::DiscImage::open(launcher::utf8_path(config.get("disc2")), &error);
    if (disc2 && archive2.open(*disc2, &error)) game::loader_register_disc(2, &archive2);
    else std::fprintf(stderr, "%s: %s (disc 2 not available)\n", config.get("disc2").c_str(), error.c_str());
  }
  if (!dump_dir.empty()) return tools::dump_textures(archive, entry, dump_dir);
  if (viewer) return tools::run_viewer(archive, entry, static_cast<int>(frames));

  {
    const char* env1 = std::getenv("SS_MEMCARD1");
    const char* env2 = std::getenv("SS_MEMCARD2");
    // launcher::memcard_path: slot 1 defaults to memcard1.mcd next to config.ini; "none"
    // (an empty slot) comes back empty.
    const std::string card1 = env1 ? env1 : launcher::memcard_path(config, 1);
    const std::string card2 = env2 ? env2 : launcher::memcard_path(config, 2);
    auto slot = [&](const std::string& card) { return no_memcard || card.empty() || card == "none" ? nullptr : card.c_str(); };
    game::memcard_set_images(slot(card1), slot(card2));
  }

  auto& rt = runtime::Runtime::get();
  if (rt.mods_dir.empty()) rt.mods_dir = launcher::path_utf8(launcher::mods_folder(config));
  if (debug_console) {
    game::field::debug_console_install();
    rt.start_debug_console();
    rt.debug_overlay = true;
  }
  const launcher::DisplaySettings display = launcher::read_display(config);
  rt.gpu.dithering_enabled = reference_dither || display.dither;
  if (reference_dither) {
    rt.gpu.dither_textured = rt.gpu.dither_lines = false;
    rt.gpu.blend_before_dither = rt.gpu.blend_unsaturated_modulation = rt.gpu.capture_shift = true;
  }
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
    platform::WindowOptions options;
    options.mode = display.mode == launcher::WindowMode::kBorderless   ? platform::WindowMode::kBorderless
                   : display.mode == launcher::WindowMode::kFullscreen ? platform::WindowMode::kFullscreen
                                                                       : platform::WindowMode::kWindowed;
    options.width = 320 * display.scale;
    options.height = 240 * display.scale;
    options.fullscreen_width = display.width;
    options.fullscreen_height = display.height;
    options.refresh = display.refresh;
    options.monitor = display.monitor;
    options.vsync = display.vsync;
    if (!window.create("Star Ocean: The Second Story (Shattered Stars)", options, &error)) {
      std::fprintf(stderr, "%s\n", error.c_str());
      return 1;
    }
    rt.window = &window;
  }

  const launcher::SoundSettings sound = launcher::read_sound(config);
  platform::AudioOptions audio;
  audio.device = sound.enabled && (!headless || audio_asked);
  audio.volume = sound.volume;
  audio.wav = audio_wav;
  if (std::string audio_error; !platform::audio_open(audio, &audio_error))
    std::fprintf(stderr, "audio: %s\n", audio_error.c_str());
  rt.audio_tick = platform::audio_vblank;

  try {
    game::Program program = game::boot_main(0, &archive);
    if (!battle_spec.empty()) program = game::battle::battle_debug_start(battle_spec, battle_end);
    else game::battle::battle_debug_set_end(battle_end);
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
