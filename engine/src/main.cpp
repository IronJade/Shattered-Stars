// Shattered Stars: Star Ocean: The Second Story, hand-ported to run natively.
//
//   shattered-stars [--disc1 IMAGE] [--disc2 IMAGE]      play (paths are remembered)
//   shattered-stars --version / --help                   print the version (or usage) and exit
//   shattered-stars-setup (setup.exe on Windows)         the settings window for players
//
// Settings live in config.ini (launcher/config.h: XDG_CONFIG_HOME, %APPDATA%\ShatteredStars
// or ~/.config/shattered-stars), written by the setup program and by the remembered
// options below. Disc 1 must be disc 1 of a release the port plays (North American or
// European; runtime/region.h; disc 2 the same release); when it isn't, the game
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
// SS_SHOT="VBLANK:path.png[,VBLANK:path2.png,...]" (debug, default off; documentation
// screenshots): writes the frame actually presented at each listed vblank, i.e. what a
// player's window shows (scaled, letterboxed to 4:3) -- not the raw PS1 display buffer
// --capture / SS_VRAM_PNG dump. A headless run opens no window otherwise; this opens a
// second, hidden one just to render into and read back (SDL_VIDEO_DRIVER=offscreen keeps
// it from appearing).
//
// Audio: --audio / --no-audio (remembered; on by default with a window, off when
// headless unless --audio is given), --audio-wav FILE or SS_AUDIO_WAV=FILE (record
// the sound output as a WAV file, also headless).
//
// Memory cards: --memcard1 FILE / --memcard2 FILE (raw 128 KB images, the .mcd/.mcr
// format emulators use; remembered; "none" removes the card). Slot 1 defaults to
// memcard1.mcd next to config.ini, created formatted on first use. --no-memcard runs
// with both slots empty (testing; not remembered). The environment variables
// SS_MEMCARD1 / SS_MEMCARD2 override the remembered paths for one run. Headless runs use
// only cards named for the run (--memcardN, SS_MEMCARDN): otherwise both slots are empty,
// so tests never create or write a card in the settings folder. An Archipelago seed plays on
// its own card instead of the player's (launcher/ap_seed.h seed_memcard).
//
// Window: config.ini's window / window_scale / resolution / monitor / vsync keys (set in
// setup); F11 or Alt+Enter toggles fullscreen; Esc quits. Keys: arrows, Z (Cross), X (Circle),
// A (Square), S (Triangle), Enter (Start), Backspace (Select), Q/W (L1/R1), 1/2 (L2/R2);
// gamepads map directly (left stick = d-pad).
//
// Archipelago (port feature, docs/archipelago_plan.md): --ap-seed FILE (an .apso2 seed, or the
// generator's AP_<seed>.zip holding it; this run only) or config.ini's ap_seed key (not read
// by headless runs) turns it on; off without a seed. An archive with several Star Ocean seeds
// needs --ap-seed-entry NAME (config.ini's ap_seed_entry; launcher/ap_seed.h). [world agent]
//
// Dialogue (port feature, game/field/message.h TextOptions): config.ini's instant_text,
// auto_advance and auto_advance_delay (set in setup's Gameplay page; launcher/settings.h).
// Headless runs ignore them, so tests play as the original, unless SS_INSTANT_TEXT=0|1 or
// SS_AUTO_ADVANCE=0|1|MS (1: on at the default delay; MS > 1: on, that many milliseconds)
// say otherwise; the variables override config.ini on a windowed run too.
//
// IMAGE is a .cue, raw .bin or .iso of your own disc.

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <random>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "disc/archive.h"
#include "disc/disc_image.h"
#include "game/ap/ap.h"
#include "game/ap/seed.h"
#include "game/battle/battle_debug.h"
#include "game/boot/pad.h"
#include "game/boot/system.h"
#include "game/common/memcard.h"
#include "game/field/debug_console.h"
#include "game/field/message.h"
#ifdef SS_HAVE_AP_NET
#include "ap_net/client.h"
#endif
#include "launcher/ap_seed.h"
#include "launcher/config.h"
#include "launcher/discs.h"
#include "launcher/process.h"
#include "launcher/report.h"
#include "launcher/settings.h"
#include "platform/audio.h"
#include "platform/window.h"
#include "runtime/boot_exe.h"
#include "runtime/region.h"
#include "runtime/runtime.h"
#include "tools/viewer.h"
#include "util/png.h"

#ifndef _WIN32
extern char** environ;
#endif

namespace {

using namespace ss;

bool env_set(const char* name) {
  const char* v = std::getenv(name);
  return v != nullptr && *v != 0;
}

// An Archipelago game is played as it is: every SS_* variable (the debugging and test hooks)
// is cleared but 'keep'. Returns the names cleared. Nothing reads them before main().
std::vector<std::string> clear_debug_environment(const std::vector<std::string>& keep) {
  std::vector<std::string> names;
#ifdef _WIN32
  char** env = _environ;
#else
  char** env = environ;
#endif
  for (char** e = env; e != nullptr && *e != nullptr; ++e) {
    std::string name(*e);
    name = name.substr(0, name.find('='));
    std::string upper = name;
    for (char& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));  // Windows: any case
    if (upper.rfind("SS_", 0) == 0 && std::find(keep.begin(), keep.end(), upper) == keep.end()) names.push_back(name);
  }
  for (const std::string& n : names) {
#ifdef _WIN32
    _putenv_s(n.c_str(), "");
#else
    unsetenv(n.c_str());
#endif
  }
  return names;
}

// Why there is nothing to boot from: one line about the image (if one was given), then how
// to give one. A fatal error (launcher/report.h: a message box when nobody sees stderr).
void print_no_disc(const std::string& disc1) {
  std::string text;
  if (!disc1.empty()) {
    const launcher::DiscInfo info = launcher::identify_disc(launcher::utf8_path(disc1));
    text = "disc 1 image expected, " + disc1 +
           (info.kind == launcher::DiscKind::kDisc2 ? " is disc 2" : ": " + launcher::describe(info, 1)) + "\n";
  }
  text += std::string("No disc image yet: run ") + launcher::kSetupProgram +
          ", put your disc images in a \"discs\" folder next to the game, or\n"
          "run once with --disc1 \"/path/to/Disc 1.cue\" (both are remembered).\n";
  launcher::fatal(text);
}

int usage(const char* argv0) {
  launcher::report("Shattered Stars %s\n", SS_VERSION);
  launcher::report(
               "usage: %s [--help] [--version] [--disc1 IMAGE] [--disc2 IMAGE] [--no-dither|--dither|--reference-dither] [--viewer] [--dump DIR] [--entry N]\n"
               "       [--memcard1 FILE] [--memcard2 FILE] [--no-memcard] [--headless] [--frames N] [--map N] [--map-pos X,Y,Z[,F[,C]]] [--capture DIR --capture-frames A,B,...] [--input F:buttons,...]\n"
               "       [--audio|--no-audio] [--audio-wav FILE] [--mods DIR] [--debug] [--ap-seed FILE [--ap-seed-entry NAME]]\n",
               argv0);
  return 2;
}

}  // namespace

int main(int argc, char** argv) {
  // Messages (launcher/report.h): to the terminal the game was started from; a windowed
  // player's run also keeps them in shattered-stars.log beside config.ini (all of them on
  // Windows when the game was double-clicked). Headless and SS_DISC runs write no log.
  {
    bool headless_run = false, debug_run = false;
    for (int i = 1; i < argc; ++i) {
      headless_run = headless_run || std::strcmp(argv[i], "--headless") == 0;
      debug_run = debug_run || std::strcmp(argv[i], "--debug") == 0;
    }
    launcher::attach_console(debug_run);
    if (!headless_run && !env_set("SS_DISC1") && !env_set("SS_DISC2"))
      launcher::open_log(launcher::config_dir() / "shattered-stars.log");
  }
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
  std::string ap_seed;                  // --ap-seed FILE (Archipelago, game/ap)
  std::string ap_seed_entry;            // --ap-seed-entry NAME (the seed in an archive)
  bool ap_seed_arg = false;
  bool viewer = false, headless = false, config_changed = false, reference_dither = false,
       no_memcard = false, debug_console = false;
  int entry = 10;
  u64 frames = 0;
  bool audio_asked = false, disc1_arg = false, disc2_arg = false, memcard_arg[2] = {false, false};
  std::filesystem::path audio_wav;
  if (const char* wav = std::getenv("SS_AUDIO_WAV"); wav && *wav) audio_wav = wav;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    auto next = [&]() -> const char* { return argv[++i]; };
    const bool has_value = i + 1 < argc;
    if ((arg == "--disc1" || arg == "--disc") && has_value) {
      remember("disc1", launcher::path_utf8(std::filesystem::absolute(next())));
      config_changed = disc1_arg = true;
    } else if (arg == "--disc2" && has_value) {
      remember("disc2", launcher::path_utf8(std::filesystem::absolute(next())));
      config_changed = disc2_arg = true;
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
      config_changed = memcard_arg[arg == "--memcard2"] = true;
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
    } else if (arg == "--ap-seed" && has_value) {  // Archipelago seed (game/ap; this run only)
      ap_seed = launcher::path_utf8(std::filesystem::absolute(next()));
      ap_seed_arg = true;
    } else if (arg == "--ap-seed-entry" && has_value) {  // which seed of an AP_<seed>.zip
      ap_seed_entry = next();
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
    } else if (arg == "--version") {  // prints and exits; never touches config.ini
      launcher::report("Shattered Stars %s\n", SS_VERSION);
      return 0;
    } else if (arg == "--help") {
      usage(argv[0]);
      return 0;
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
    if (std::string save_error; !saved.save(&save_error)) launcher::report("%s\n", save_error.c_str());
  }
  if (env_set("SS_DISC1")) config.set("disc1", std::getenv("SS_DISC1"));
  if (env_set("SS_DISC2")) config.set("disc2", std::getenv("SS_DISC2"));

  // The boot disc (launcher/discs.h): the configured image if it is disc 1, else the first
  // disc 1 in the `discs` folders (silently, remembered), else the setup program. An image
  // given for this run (SS_DISC1, --disc1) or a headless run may also be disc 2, which the
  // game boots from as a console does (the event sweep of disc 2 runs that way); a windowed
  // run from config.ini wants disc 1, as setup writes it.
  const bool lenient = headless || env_set("SS_DISC1") || disc1_arg;
  auto bootable = [&](const std::string& path) {
    const int n = launcher::disc_number(launcher::utf8_path(path));
    return n == 1 || (n == 2 && lenient);
  };
  {
    const auto folders = launcher::disc_folders(launcher::config_dir());
    bool write_back = false;
    if (!bootable(config.get("disc1"))) {
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
    if (!bootable(config.get("disc1")) && may_write) {
      const std::filesystem::path setup = launcher::sibling_program(launcher::kSetupProgram);
      if (!setup.empty()) {
        launcher::report("No usable Disc 1 image yet: opening %s.\n", launcher::kSetupProgram);
        std::string setup_error;
        const int code = launcher::run_and_wait(setup, {"--from-game"}, &setup_error);
        if (code < 0) launcher::report("%s: %s\n", launcher::path_utf8(setup).c_str(), setup_error.c_str());
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
    if (!bootable(config.get("disc1"))) {
      print_no_disc(config.get("disc1"));
      return may_write ? 1 : usage(argv[0]);
    }
    // Disc 2 (optional): an image given for this run (SS_DISC2, --disc2) as it is; else the
    // configured image if it is disc 2, else one next to disc 1 or in the `discs` folders.
    const bool disc2_given = env_set("SS_DISC2") || disc2_arg;
    const bool boot_is_disc2 = launcher::disc_number(launcher::utf8_path(config.get("disc1"))) == 2;
    if (!disc2_given && !boot_is_disc2 && launcher::disc_number(launcher::utf8_path(config.get("disc2"))) != 2) {
      if (!config.get("disc2").empty())
        launcher::report("Disc 2 \"%s\": %s\n", config.get("disc2").c_str(),
                     launcher::describe(launcher::identify_disc(launcher::utf8_path(config.get("disc2"))), 2).c_str());
      std::string disc2 = launcher::find_disc2_near(launcher::utf8_path(config.get("disc1")));
      if (disc2.empty()) disc2 = launcher::find_discs(folders).disc2;
      config.set("disc2", disc2);
      if (!disc2.empty()) {
        saved.set("disc2", disc2);
        write_back = true;
      }
    }
    // Both discs must be the same release (runtime/region.h): a disc 2 of another one is
    // not used.
    const std::string pair = launcher::describe_pair(launcher::identify_disc(launcher::utf8_path(config.get("disc1"))),
                                                     launcher::identify_disc(launcher::utf8_path(config.get("disc2"))));
    if (!pair.empty()) {
      launcher::report("%s Disc 2 is not used.\n", pair.c_str());
      config.set("disc2", launcher::find_disc2_near(launcher::utf8_path(config.get("disc1"))));
    }
    if (write_back && may_write) saved.save();
  }

  std::string error;
  auto disc = disc::DiscImage::open(launcher::utf8_path(config.get("disc1")), &error);
  disc::Archive archive;
  if (!disc || !archive.open(*disc, &error)) {
    launcher::fatal(config.get("disc1") + ": " + error);
    return 1;
  }
  // The release (its address maps, save product code, text): from the boot disc's serial.
  if (const region::Profile* release = region::from_serial(runtime::boot_exe_name(*disc))) region::select(release->id);
  // The sound driver's tables (and libspu's reverb presets) are read from the boot executable.
  if (std::string exe_error; !runtime::boot_exe_open(*disc, &exe_error))
    launcher::report("%s: %s (no sound)\n", config.get("disc1").c_str(), exe_error.c_str());
  // Disc 2 (optional, --disc2): the loader swaps to it when a save or the story needs it
  // (game::loader_insert_disc); without it the game stays on disc 1, whose archive holds
  // the same field and battle data.
  std::unique_ptr<disc::DiscImage> disc2;
  disc::Archive archive2;
  game::loader_register_disc(1, &archive);
  if (!config.get("disc2").empty()) {
    disc2 = disc::DiscImage::open(launcher::utf8_path(config.get("disc2")), &error);
    if (disc2 && archive2.open(*disc2, &error)) game::loader_register_disc(2, &archive2);
    else launcher::report("%s: %s (disc 2 not available)\n", config.get("disc2").c_str(), error.c_str());
  }
  if (!dump_dir.empty()) return tools::dump_textures(archive, entry, dump_dir);
  if (viewer) return tools::run_viewer(archive, entry, static_cast<int>(frames));

  // Archipelago (game/ap): the seed is read before the memory cards (a seed plays on its own
  // card) and before any map (its patch lines join the world edits). A seed that does not load
  // stops the game (playing on would lose checks).
  // config.ini's ap_enabled (setup's Archipelago switch, default on) turns the chosen seed off
  // without forgetting it: the game then plays as the original.
  if (ap_seed.empty() && !headless && config.get_bool("ap_enabled", true)) ap_seed = config.get("ap_seed");
  if (!ap_seed_arg && !ap_seed.empty()) ap_seed_entry = config.get("ap_seed_entry");

  // An Archipelago game is played as it is (the user, 2026-10-25: debugging is for games
  // without a seed): in a windowed run with a seed, no debug console (--debug, and with it
  // SS_DEBUG_INPUT), no debug start (--map, --map-pos, --battle, --battle-end), no scripted
  // input or capture (--input, --capture), no card named for the run (the seed plays on its own
  // card), and every SS_* hook cleared but the server's (SS_AP_SERVER / SLOT / GAME) and the
  // mods and dialogue choices setup also makes. Headless runs (the tests) keep them all.
  const bool ap_locked = !ap_seed.empty() && !headless;
  if (ap_locked) {
    std::vector<std::string> off;
    if (debug_console) off.push_back("--debug");
    if (runtime::Runtime::get().debug_start_map >= 0 || runtime::Runtime::get().debug_start_pos_set) off.push_back("--map");
    if (!battle_spec.empty() || !battle_end.empty()) off.push_back("--battle");
    if (!input_script.empty()) off.push_back("--input");
    if (!capture_dir.empty()) off.push_back("--capture");
    if (memcard_arg[0] || memcard_arg[1]) off.push_back("--memcard (for the seed)");
    debug_console = false;
    runtime::Runtime::get().debug_start_map = -1;
    runtime::Runtime::get().debug_start_pos_set = false;
    battle_spec.clear();
    battle_end.clear();
    input_script.clear();
    capture_dir.clear();
    memcard_arg[0] = memcard_arg[1] = false;
    for (const std::string& n : clear_debug_environment({"SS_AP_SERVER", "SS_AP_SLOT", "SS_AP_GAME", "SS_MODS_DIR",
                                                         "SS_MOD_ORDER", "SS_INSTANT_TEXT", "SS_AUTO_ADVANCE"}))
      off.push_back(n);
    if (!off.empty()) {
      std::string list;
      for (const std::string& o : off) list += (list.empty() ? "" : ", ") + o;
      launcher::report("[ap] an Archipelago game plays without debugging; ignored: %s\n", list.c_str());
    }
  }
  game::ap::Seed ap_seed_head;  // the seed's id and slot, for its card
  if (!ap_seed.empty()) {
    std::string ap_error;
    // The generator's archive: its Star Ocean seed is copied out next to config.ini.
    std::filesystem::path seed_path;
    if (!launcher::seed_file(launcher::utf8_path(ap_seed), ap_seed_entry, &seed_path, &ap_error)) {
      launcher::fatal("Archipelago seed " + ap_seed + ": " + ap_error);
      return 1;
    }
    ap_seed = launcher::path_utf8(seed_path);
    if (!game::ap::load_seed_file(ap_seed, &ap_seed_head, &ap_error)) {
      launcher::fatal("Archipelago seed: " + ap_error);
      return 1;
    }
  }

  {
    // launcher::memcard_path: slot 1 defaults to memcard1.mcd next to config.ini; "none"
    // (an empty slot) comes back empty. Headless: only a card named for this run.
    // With an Archipelago seed: the seed's own card in slot 1 and slot 2 empty, so a seed
    // never loads an ordinary save or writes the player's cards (launcher/ap_seed.h); a card
    // named for the run (--memcardN, SS_MEMCARDN) still wins. Headless runs use it only with
    // SS_AP_SEED_CARD=1 (tests, with a scratch XDG_CONFIG_HOME).
    const char* seed_card_env = std::getenv("SS_AP_SEED_CARD");
    const bool seed_card = !ap_seed.empty() && (!headless || (seed_card_env && std::string(seed_card_env) == "1"));
    std::string cards[2];
    for (int i = 0; i < 2; ++i) {
      if (const char* env = std::getenv(i == 0 ? "SS_MEMCARD1" : "SS_MEMCARD2")) cards[i] = env;
      else if (memcard_arg[i]) cards[i] = launcher::memcard_path(config, i + 1);
      else if (seed_card && i == 0) cards[i] = launcher::path_utf8(launcher::seed_memcard(ap_seed_head.seed, ap_seed_head.slot_name));
      else if (seed_card) cards[i].clear();
      else if (!headless) cards[i] = launcher::memcard_path(config, i + 1);
    }
    if (seed_card && !std::getenv("SS_MEMCARD1") && !memcard_arg[0]) {
      // Once, when the seed's card is new: the saves an earlier version made for this seed on
      // the player's own cards are copied to it (the player's cards stay as they are).
      std::vector<std::filesystem::path> own;
      for (int i = 1; i <= 2; ++i)
        if (const std::string c = launcher::memcard_path(config, i); !c.empty() && c != "none") own.push_back(launcher::utf8_path(c));
      std::string adopt_error;
      const int adopted = launcher::adopt_seed_saves(launcher::utf8_path(cards[0]), own, ap_seed_head.seed,
                                                     ap_seed_head.slot_name, &adopt_error);
      if (adopted > 0) launcher::report("[ap] %d save(s) of this seed copied from your memory card to %s\n", adopted, cards[0].c_str());
      else if (adopted < 0) launcher::report("[ap] this seed's saves could not be copied from your memory card: %s\n", adopt_error.c_str());
      launcher::report("[ap] the seed plays on its own memory card: %s\n", cards[0].c_str());
    }
    auto slot = [&](const std::string& card) { return no_memcard || card.empty() || card == "none" ? nullptr : card.c_str(); };
    game::memcard_set_images(slot(cards[0]), slot(cards[1]));
  }

  if (!ap_seed.empty()) {
    std::string ap_error;
    if (!game::ap::start(ap_seed, &ap_error)) {
      launcher::fatal("Archipelago seed: " + ap_error);
      return 1;
    }
#ifdef SS_HAVE_AP_NET
    // The server (ap_net/client.h), from config.ini's ap_server / ap_slot / ap_password (set
    // in setup), on windowed runs: checks, the goal and DeathLink go out, items come in.
    // Without a server the seed plays offline and the checks wait in the save.
    std::string server = headless ? std::string() : config.get("ap_server");
    // [world agent] Tests: SS_AP_SERVER (+ SS_AP_SLOT) connects any run, headless ones too,
    // without config.ini (tools/ap_test_server.sh's local server; scratchpad/world/ap/e2e).
    const char* test_server = std::getenv("SS_AP_SERVER");
    if (test_server != nullptr && *test_server != 0) server = test_server;
    if (!server.empty()) {
      ap_net::ConnectInfo info;
      info.server = server;
      info.slot = config.get("ap_slot");
      if (const char* s = std::getenv("SS_AP_SLOT"); s != nullptr && *s != 0) info.slot = s;
      if (info.slot.empty() && game::ap::seed()) info.slot = game::ap::seed()->slot_name;
      info.password = config.get("ap_password");
      info.deathlink = game::ap::deathlink_enabled();
      // Tests: log in as another game's slot (SS_AP_GAME=ChecksFinder) until there is a
      // Star Ocean world to host.
      if (const char* game_name = std::getenv("SS_AP_GAME"); game_name && *game_name) info.game = game_name;
      // This install's id for the server, made once (kept unless a test run).
      info.uuid = config.get("ap_uuid");
      if (info.uuid.empty()) {
        std::random_device rd;
        char hex[33];
        for (int i = 0; i < 32; ++i) hex[i] = "0123456789abcdef"[rd() & 15];
        hex[32] = 0;
        info.uuid = hex;
        if (may_write) {
          saved.set("ap_uuid", info.uuid);
          saved.save();
        }
      }
      launcher::report("Archipelago: connecting to %s as %s\n", info.server.c_str(), info.slot.c_str());
      game::ap::connect(std::make_unique<ap_net::Client>(info));
    }
#endif
  }

  auto& rt = runtime::Runtime::get();
  // The mods folder and its load order (mods/catalog.h). config.ini's order describes the
  // configured folder only: with --mods / SS_MODS_DIR the defaults apply (or SS_MOD_ORDER).
  const char* mods_env = std::getenv("SS_MODS_DIR");
  if (rt.mods_dir.empty() && !(mods_env && *mods_env)) rt.mod_order = launcher::read_mod_order(config);
  if (rt.mods_dir.empty()) rt.mods_dir = launcher::path_utf8(launcher::mods_folder(config));
  if (debug_console) {
    game::field::debug_console_install();
    rt.start_debug_console();
    rt.debug_overlay = true;
  }
  // Dialogue (see the top of this file).
  {
    launcher::TextSettings text = headless ? launcher::TextSettings{} : launcher::read_text(config);
    if (const char* v = std::getenv("SS_INSTANT_TEXT"); v != nullptr && *v != 0) text.instant = std::atoi(v) != 0;
    if (const char* v = std::getenv("SS_AUTO_ADVANCE"); v != nullptr && *v != 0) {
      const int n = std::atoi(v);
      text.auto_advance = n != 0;
      if (n > 1) text.auto_advance_ms = n;
    }
    game::field::TextOptions& opts = game::field::text_options();
    opts.instant = text.instant;
    opts.auto_advance_frames = launcher::auto_advance_frames(text);
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

  // SS_SHOT (see the top of this file): "VBLANK:path.png[,VBLANK:path2.png,...]".
  if (const char* shot_env = std::getenv("SS_SHOT")) {
    std::stringstream shots(shot_env);
    for (std::string step; std::getline(shots, step, ',');) {
      const auto colon = step.find(':');
      if (colon == std::string::npos || colon == 0) continue;
      rt.shot_frames[std::strtoull(step.substr(0, colon).c_str(), nullptr, 10)] = launcher::utf8_path(step.substr(colon + 1));
    }
  }

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
      launcher::fatal("The game window could not be opened (" + error + ").\n\nThe game needs OpenGL 3.3: "
                      "updating the graphics driver usually helps.");
      return 1;
    }
    rt.window = &window;
    // The player's controls (setup's Controls page; launcher/settings.h).
    const launcher::ControlSettings controls = launcher::read_controls(config);
    window.set_controls(controls.controls);
    game::pad_set_player_stick(launcher::stick_deadzone_units(controls), controls.stick_dpad ? -1 : 0);
  }

  // SS_SHOT: a second window, independent of the one above (so a headless run stays
  // headless -- no real-time pacing, no input polling -- see runtime::Runtime::vblank),
  // used only to render and read back the listed vblanks. Fixed 1280x960 (4:3, matching
  // the internal 320x240 at an integer scale of 4) so every screenshot comes out the same
  // size regardless of config.ini.
  platform::Window shot_window;
  if (!rt.shot_frames.empty()) {
    platform::WindowOptions shot_options;
    shot_options.mode = platform::WindowMode::kWindowed;
    shot_options.width = 1280;
    shot_options.height = 960;
    shot_options.vsync = false;
    if (!shot_window.create("Shattered Stars (SS_SHOT)", shot_options, &error)) {
      std::fprintf(stderr, "SS_SHOT: could not open an offscreen window (%s); no screenshots will be written\n",
                   error.c_str());
    } else {
      rt.shot_window = &shot_window;
    }
  }

  const launcher::SoundSettings sound = launcher::read_sound(config);
  platform::AudioOptions audio;
  audio.device = sound.enabled && (!headless || audio_asked);
  audio.volume = sound.volume;
  audio.wav = audio_wav;
  if (std::string audio_error; !platform::audio_open(audio, &audio_error))
    launcher::report("audio: %s\n", audio_error.c_str());
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
  } catch (const std::exception& e) {
    if (headless) throw;  // tests see the crash as before
    launcher::fatal(std::string("The game stopped because of an error:\n\n") + e.what());
    platform::audio_close();
    return 3;
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
