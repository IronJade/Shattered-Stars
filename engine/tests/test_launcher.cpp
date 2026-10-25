// The launcher library (config.ini, disc identification, settings) and the game's command
// line. The disc tests need SS_DISC1 (disc 2 is looked for next to it, or SS_DISC2); the
// command-line tests also need the game built next to this test program. Scratch files go
// in test_launcher_scratch/ next to the test program; the player's settings are never read.

#include <SDL3/SDL.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "launcher/ap_seed.h"
#include "launcher/config.h"
#include "launcher/discs.h"
#include "launcher/process.h"
#include "launcher/settings.h"
#include "launcher/title_art.h"
#include "game/boot/pad.h"
#include "platform/window.h"
#include "test.h"

#include <bit>
#include <set>

using namespace ss;
namespace fs = std::filesystem;

namespace {

fs::path scratch(const std::string& name) {
  const char* base = SDL_GetBasePath();
  const fs::path dir = fs::path(base ? base : ".") / "test_launcher_scratch" / name;
  std::error_code ec;
  fs::remove_all(dir, ec);
  fs::create_directories(dir, ec);
  return dir;
}

std::string read_file(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  std::stringstream s;
  s << in.rdbuf();
  return s.str();
}

void write_file(const fs::path& p, const std::string& text) {
  std::ofstream out(p, std::ios::binary | std::ios::trunc);
  out << text;
}

bool contains(const std::string& haystack, const std::string& needle) { return haystack.find(needle) != std::string::npos; }

std::string disc1_path() {
  const char* d = std::getenv("SS_DISC1");
  return d && *d ? d : "";
}

std::string disc2_path() {
  if (const char* d = std::getenv("SS_DISC2"); d && *d) return d;
  const std::string d1 = disc1_path();
  return d1.empty() ? "" : launcher::find_disc2_near(launcher::utf8_path(d1));
}

// Runs the game next to this test program with these arguments and environment changes
// (an empty value unsets); its exit code and everything it printed.
struct GameRun {
  int code = -1;
  std::string output;
};

GameRun run_game(const std::vector<std::string>& args, const std::vector<std::pair<std::string, std::string>>& env) {
  GameRun run;
  const fs::path game = launcher::sibling_program(launcher::kGameProgram);
  std::vector<std::string> argv_strings = {launcher::path_utf8(game)};
  argv_strings.insert(argv_strings.end(), args.begin(), args.end());
  std::vector<const char*> argv;
  for (const std::string& a : argv_strings) argv.push_back(a.c_str());
  argv.push_back(nullptr);
  SDL_Environment* environment = SDL_CreateEnvironment(true);
  for (const auto& [name, value] : env) {
    if (value.empty()) SDL_UnsetEnvironmentVariable(environment, name.c_str());
    else SDL_SetEnvironmentVariable(environment, name.c_str(), value.c_str(), true);
  }
  SDL_PropertiesID props = SDL_CreateProperties();
  SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, const_cast<char**>(argv.data()));
  SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ENVIRONMENT_POINTER, environment);
  SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_NULL);
  SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP);
  SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true);
  SDL_Process* process = SDL_CreateProcessWithProperties(props);
  SDL_DestroyProperties(props);
  SDL_DestroyEnvironment(environment);
  if (!process) {
    run.output = SDL_GetError();
    return run;
  }
  size_t size = 0;
  if (void* data = SDL_ReadProcess(process, &size, &run.code)) {
    run.output.assign(static_cast<const char*>(data), size);
    SDL_free(data);
  }
  SDL_DestroyProcess(process);
  return run;
}

// The environment of a headless test run: settings in 'config_home', no window or sound.
std::vector<std::pair<std::string, std::string>> test_env(const fs::path& config_home) {
  const std::string home = launcher::path_utf8(config_home);
  return {{"XDG_CONFIG_HOME", home}, {"APPDATA", home},        {"SDL_VIDEO_DRIVER", "offscreen"},
          {"SDL_AUDIO_DRIVER", "dummy"}, {"SS_DISC1", ""},     {"SS_DISC2", ""},
          {"SS_MEMCARD1", ""},           {"SS_MEMCARD2", ""},  {"SS_EVENT_SWEEP", ""}};
}

bool game_available() {
  if (disc1_path().empty()) {
    std::printf("  (skipped: SS_DISC1 not set)\n");
    return false;
  }
  if (launcher::sibling_program(launcher::kGameProgram).empty()) {
    std::printf("  (skipped: %s is not built next to the tests)\n", launcher::kGameProgram);
    return false;
  }
  return true;
}

}  // namespace

TEST(launcher_config_keeps_unknown_keys_and_comments) {
  const fs::path dir = scratch("config");
  const fs::path file = dir / "config.ini";
  write_file(file, "\xEF\xBB\xBF# my notes\r\ndisc1 = old.cue\r\nfuture_key=42\r\n\r\n; another note\r\ndither=0\r\ndither=1\r\n");
  launcher::Config c = launcher::Config::load(file);
  CHECK(c.get("disc1") == "old.cue");          // spaces trimmed, CR stripped, BOM skipped
  CHECK(c.get("future_key") == "42");
  CHECK(c.get_bool("dither", false));          // the last duplicate wins
  c.set("disc1", "new.cue");
  c.set("volume", "80");
  c.set("dither", "0");
  CHECK(c.save());
  const std::string text = read_file(file);
  CHECK(contains(text, "# my notes\n"));
  CHECK(contains(text, "; another note\n"));
  CHECK(contains(text, "future_key=42\n"));
  CHECK(contains(text, "disc1=new.cue\n"));
  CHECK(contains(text, "volume=80\n"));
  CHECK(!contains(text, "dither=1"));
  CHECK(text.find("dither=0") == text.rfind("dither="));  // duplicates collapse to one line
  CHECK(text.find("# my notes") < text.find("disc1=new.cue"));
  CHECK(text.find("disc1=new.cue") < text.find("future_key=42"));  // keys keep their place
  CHECK(!fs::exists(dir / "config.ini.new"));
  const launcher::Config again = launcher::Config::load(file);
  CHECK(again.same_values(c));
}

TEST(launcher_config_new_file) {
  const fs::path dir = scratch("config_new");
  launcher::Config c = launcher::Config::load(dir / "sub" / "config.ini");
  CHECK(!c.has("disc1"));
  CHECK(c.get("disc1", "fallback") == "fallback");
  CHECK_EQ(c.get_int("volume", 77), 77);
  c.set("disc1", "C:\\Games\\SO2 (Disc 1).cue");
  CHECK(c.save());  // creates the folder
  const std::string text = read_file(dir / "sub" / "config.ini");
  CHECK(text.rfind("# Shattered Stars settings", 0) == 0);
  CHECK(contains(text, "disc1=C:\\Games\\SO2 (Disc 1).cue\n"));
}

TEST(launcher_settings_defaults_and_round_trip) {
  launcher::Config empty;
  const launcher::DisplaySettings d = launcher::read_display(empty);
  CHECK(d.mode == launcher::WindowMode::kWindowed);
  CHECK_EQ(d.scale, 3);
  CHECK_EQ(d.width, 0);
  CHECK_EQ(d.monitor, 1);
  CHECK(d.vsync);
  CHECK(d.dither);
  const launcher::SoundSettings s = launcher::read_sound(empty);
  CHECK(s.enabled);
  CHECK_EQ(s.volume, 100);

  launcher::DisplaySettings e;
  e.mode = launcher::WindowMode::kFullscreen;
  e.scale = 4;
  e.width = 1920;
  e.height = 1080;
  e.refresh = 59.94f;
  e.monitor = 2;
  e.vsync = false;
  e.dither = false;
  launcher::Config c;
  launcher::write_display(c, e);
  launcher::write_sound(c, {false, 35});
  CHECK(c.get("window") == "fullscreen");
  CHECK(c.get("resolution") == "1920x1080@59.94");
  CHECK(launcher::read_display(c) == e);
  CHECK(launcher::read_sound(c) == (launcher::SoundSettings{false, 35}));
  c.set("window", "borderless");
  c.set("resolution", "desktop");
  c.set("window_scale", "99");
  c.set("volume", "-5");
  CHECK(launcher::read_display(c).mode == launcher::WindowMode::kBorderless);
  CHECK_EQ(launcher::read_display(c).width, 0);
  CHECK_EQ(launcher::read_display(c).scale, launcher::kMaxScale);
  CHECK_EQ(launcher::read_sound(c).volume, 0);
}

// The dialogue settings (port enhancements): off by default, left out of the file at their
// defaults, the delay clamped and turned into vblanks.
TEST(launcher_text_settings_defaults_and_round_trip) {
  launcher::Config empty;
  const launcher::TextSettings t = launcher::read_text(empty);
  CHECK(!t.instant);
  CHECK(!t.auto_advance);
  CHECK_EQ(t.auto_advance_ms, 1000);
  CHECK_EQ(launcher::auto_advance_frames(t), 0);  // off

  launcher::Config c;
  launcher::write_text(c, t);
  CHECK(!c.has("instant_text"));
  CHECK(!c.has("auto_advance"));
  CHECK(!c.has("auto_advance_delay"));
  const launcher::TextSettings on{true, true, 1500};
  launcher::write_text(c, on);
  CHECK(c.get("instant_text") == "1");
  CHECK(c.get("auto_advance") == "1");
  CHECK(c.get("auto_advance_delay") == "1500");
  CHECK(launcher::read_text(c) == on);
  CHECK_EQ(launcher::auto_advance_frames(on), 90);
  launcher::write_text(c, t);  // back to the defaults: the keys go again
  CHECK(!c.has("instant_text"));
  CHECK(!c.has("auto_advance_delay"));

  c.set("auto_advance", "1");
  c.set("auto_advance_delay", "5");
  CHECK_EQ(launcher::read_text(c).auto_advance_ms, launcher::kMinAutoAdvanceMs);
  CHECK_EQ(launcher::auto_advance_frames(launcher::read_text(c)), 6);
  c.set("auto_advance_delay", "999999");
  CHECK_EQ(launcher::read_text(c).auto_advance_ms, launcher::kMaxAutoAdvanceMs);
  CHECK_EQ(launcher::auto_advance_frames(launcher::read_text(c)), 600);
  CHECK_EQ(launcher::auto_advance_frames({false, true, 500}), 30);
  CHECK_EQ(launcher::auto_advance_frames({false, true, 1000}), 60);
}

TEST(launcher_memcards_and_mods_defaults) {
  launcher::Config c;
  CHECK(launcher::memcard_path(c, 1) == launcher::path_utf8(launcher::config_dir() / "memcard1.mcd"));
  CHECK(launcher::memcard_path(c, 2).empty());
  c.set("memcard1", "none");
  c.set("memcard2", "/cards/two.mcd");
  CHECK(launcher::memcard_path(c, 1).empty());
  CHECK(launcher::memcard_path(c, 2) == "/cards/two.mcd");
  CHECK(launcher::mods_folder(c) == launcher::config_dir() / "mods");
  c.set("mods", "/my/mods");
  CHECK(launcher::mods_folder(c) == launcher::utf8_path("/my/mods"));
}

TEST(launcher_identify_files_that_are_not_discs) {
  const fs::path dir = scratch("identify");
  CHECK(launcher::identify_disc("").kind == launcher::DiscKind::kNone);
  CHECK(launcher::identify_disc(dir / "missing.cue").kind == launcher::DiscKind::kMissing);
  CHECK(launcher::identify_disc(dir).kind == launcher::DiscKind::kUnreadable);
  write_file(dir / "game.zip", "PK");
  CHECK(launcher::identify_disc(dir / "game.zip").kind == launcher::DiscKind::kArchive);
  write_file(dir / "game.chd", "MComprHD");
  CHECK(launcher::identify_disc(dir / "game.chd").kind == launcher::DiscKind::kUnsupported);
  write_file(dir / "lonely.cue", "FILE \"lonely.bin\" BINARY\n  TRACK 01 MODE2/2352\n    INDEX 01 00:00:00\n");
  const launcher::DiscInfo cue = launcher::identify_disc(dir / "lonely.cue");
  CHECK(cue.kind == launcher::DiscKind::kUnreadable);
  CHECK(contains(cue.detail, "lonely.bin"));
  write_file(dir / "zeros.iso", std::string(400 * 2048, '\0'));  // a CD image of nothing
  CHECK(launcher::identify_disc(dir / "zeros.iso").kind == launcher::DiscKind::kNotSo2);
  CHECK_EQ(launcher::disc_number(dir / "zeros.iso"), 0);
  CHECK(launcher::describe({}, 1) == "No disc chosen yet.");
}

TEST(launcher_identify_the_discs) {
  const std::string d1 = disc1_path();
  if (d1.empty()) {
    std::printf("  (skipped: SS_DISC1 not set)\n");
    return;
  }
  const launcher::DiscInfo one = launcher::identify_disc(launcher::utf8_path(d1));
  CHECK(one.kind == launcher::DiscKind::kDisc1);
  CHECK(one.game_id == "SCUS_944.21");
  CHECK(launcher::describe(one, 1) == "Disc 1, North American release: OK");
  CHECK(launcher::describe(one, 2) == "That is Disc 1. Please choose Disc 2 here.");
  CHECK(launcher::is_disc1(launcher::utf8_path(d1)));

  // The `discs` folder search (subfolders too), through a link to disc 1's folder.
  const fs::path dir = scratch("discs");
  std::error_code ec;
  fs::create_directory_symlink(launcher::utf8_path(d1).parent_path(), dir / "Disc One", ec);
  if (!ec) {  // (needs links: skipped where the file system has none)
    const launcher::FoundDiscs found = launcher::find_discs({dir / "nowhere", dir});
    CHECK(launcher::disc_number(launcher::utf8_path(found.disc1)) == 1);
  }

  const std::string d2 = disc2_path();
  if (d2.empty()) {
    std::printf("  (disc 2 checks skipped: no disc 2 image next to disc 1)\n");
    return;
  }
  const launcher::DiscInfo two = launcher::identify_disc(launcher::utf8_path(d2));
  CHECK(two.kind == launcher::DiscKind::kDisc2);
  CHECK(two.game_id == "SCUS_944.22");
  CHECK(launcher::describe(two, 1) == "That is Disc 2. Please choose Disc 1 here.");
  CHECK_EQ(launcher::disc_number(launcher::utf8_path(d2)), 2);
}

// Other releases (runtime/region.h), each given by its disc 1 image in SS_DISC1_EU /
// SS_DISC1_FR / SS_DISC1_DE / SS_DISC1_JP (disc 2 is looked for next to it); skipped when
// unset. Europe, France and Germany play; Japan is refused clearly; the two discs must be
// the same release.
TEST(launcher_identify_other_releases) {
  auto env_path = [](const char* name) {
    const char* p = std::getenv(name);
    return std::string(p && *p ? p : "");
  };
  const std::string eu1 = env_path("SS_DISC1_EU");
  if (eu1.empty()) {
    std::printf("  (Europe skipped: SS_DISC1_EU not set)\n");
  } else {
    const launcher::DiscInfo one = launcher::identify_disc(launcher::utf8_path(eu1));
    CHECK(one.kind == launcher::DiscKind::kDisc1);
    CHECK(one.game_id == "SCES_021.59");
    CHECK(one.region != nullptr && one.region->id == region::Id::kEurope);
    CHECK(launcher::describe(one, 1) == "Disc 1, European release: OK");
    const std::string eu2 = launcher::find_disc2_near(launcher::utf8_path(eu1));
    CHECK(!eu2.empty());
    if (!eu2.empty()) {
      const launcher::DiscInfo two = launcher::identify_disc(launcher::utf8_path(eu2));
      CHECK(two.kind == launcher::DiscKind::kDisc2);
      CHECK(launcher::describe(two, 2) == "Disc 2, European release: OK");
      CHECK(launcher::describe_pair(one, two).empty());
      if (!disc2_path().empty()) {  // NA disc 1 with Europe's disc 2: not a pair
        const launcher::DiscInfo na1 = launcher::identify_disc(launcher::utf8_path(disc1_path()));
        CHECK(launcher::describe_pair(na1, two) ==
              "Disc 2 is the European release and Disc 1 the North American one. Both discs must be the same release.");
        // Next to NA's disc 1, only NA's disc 2 is found.
        CHECK(launcher::find_disc2_near(launcher::utf8_path(disc1_path())) == disc2_path());
      }
    }
  }
  struct Expect {
    const char* env;
    region::Id id;
    const char* disc1;
    const char* disc2;
  };
  for (const Expect& e : {Expect{"SS_DISC1_FR", region::Id::kFrance, "Disc 1, French release: OK", "Disc 2, French release: OK"},
                          Expect{"SS_DISC1_DE", region::Id::kGermany, "Disc 1, German release: OK", "Disc 2, German release: OK"}}) {
    const std::string path = env_path(e.env);
    if (path.empty()) {
      std::printf("  (%s not set: skipped)\n", e.env);
      continue;
    }
    const launcher::DiscInfo one = launcher::identify_disc(launcher::utf8_path(path));
    CHECK(one.kind == launcher::DiscKind::kDisc1);
    CHECK(one.region != nullptr && one.region->id == e.id && one.region->supported);
    CHECK(launcher::describe(one, 1) == e.disc1);
    CHECK_EQ(launcher::disc_number(launcher::utf8_path(path)), 1);
    const std::string two_path = launcher::find_disc2_near(launcher::utf8_path(path));
    CHECK(!two_path.empty());
    if (two_path.empty()) continue;
    const launcher::DiscInfo two = launcher::identify_disc(launcher::utf8_path(two_path));
    CHECK(two.kind == launcher::DiscKind::kDisc2);
    CHECK(launcher::describe(two, 2) == e.disc2);
    CHECK(launcher::describe_pair(one, two).empty());
  }
  for (const char* name : {"SS_DISC1_JP"}) {
    const std::string path = env_path(name);
    if (path.empty()) {
      std::printf("  (%s not set: skipped)\n", name);
      continue;
    }
    const launcher::DiscInfo info = launcher::identify_disc(launcher::utf8_path(path));
    CHECK(info.region != nullptr);
    if (info.region == nullptr) continue;
    if (info.region->supported) {
      CHECK(info.kind == launcher::DiscKind::kDisc1);
    } else {
      CHECK(info.kind == launcher::DiscKind::kUnsupportedRelease);
      CHECK(contains(launcher::describe(info, 1), "release (" ));
      CHECK(contains(launcher::describe(info, 1), "not supported yet"));
      CHECK_EQ(launcher::disc_number(launcher::utf8_path(path)), 0);
    }
  }
  const std::string jp = env_path("SS_DISC1_JP");
  if (!jp.empty()) {
    const launcher::DiscInfo info = launcher::identify_disc(launcher::utf8_path(jp));
    CHECK(launcher::describe(info, 1) ==
          "Star Ocean: The Second Story, Japanese release (SLPM-86105): not supported yet. Shattered Stars plays "
          "the North American (SCUS-94421 / SCUS-94422), European (SCES-02159 / SCES-12159), French (SCES-02160 / "
          "SCES-12160) and German (SCES-02161 / SCES-12161) releases.");
  }
}

// The game on the European, French and German discs: a new game on Arlia (map 18) reads
// every table it needs through the address maps (no "region:" complaint).
void check_game_runs(const char* env_name, const char* tag) {
  const char* disc = std::getenv(env_name);
  if (disc == nullptr || *disc == 0 || launcher::sibling_program(launcher::kGameProgram).empty()) {
    std::printf("  (skipped: %s not set or the game not built)\n", env_name);
    return;
  }
  const fs::path home = scratch(tag);
  auto env = test_env(home);
  env.emplace_back("SS_DISC1", disc);
  env.emplace_back("SS_MEMCARD1", launcher::path_utf8(home / "card.mcd"));
  GameRun run = run_game({"--headless", "--frames", "400", "--map", "18"}, env);
  CHECK_EQ(run.code, 0);
  CHECK(contains(run.output, "stopped after 400 vblanks"));
  CHECK(!contains(run.output, "region:"));
  CHECK(!contains(run.output, "disc 1 image expected"));
}
TEST(game_runs_the_european_release) { check_game_runs("SS_DISC1_EU", "game_eu"); }
TEST(game_runs_the_french_release) { check_game_runs("SS_DISC1_FR", "game_fr"); }
TEST(game_runs_the_german_release) { check_game_runs("SS_DISC1_DE", "game_de"); }

// The Japanese release is recognised and refused, with the reason and what the game plays.
TEST(game_refuses_the_japanese_release) {
  const char* jp = std::getenv("SS_DISC1_JP");
  if (jp == nullptr || *jp == 0 || launcher::sibling_program(launcher::kGameProgram).empty()) {
    std::printf("  (skipped: SS_DISC1_JP not set or the game not built)\n");
    return;
  }
  const fs::path home = scratch("game_jp");
  auto env = test_env(home);
  env.emplace_back("SS_DISC1", jp);
  GameRun run = run_game({"--headless", "--frames", "5"}, env);
  CHECK_EQ(run.code, 2);
  CHECK(contains(run.output, "Japanese release (SLPM-86105): not supported yet"));
  CHECK(contains(run.output, "Shattered Stars plays the North American"));
}

// The command line the tools use (tools/event_sweep.py, smoke_field_events.sh) still
// parses, and the boot disc rules hold: SS_DISC1 may be disc 2 (the disc 2 sweep), a bad
// image says why before the usage text, and headless runs never write config.ini.
TEST(game_command_line_headless_runs) {
  if (!game_available()) return;
  const fs::path home = scratch("game_cli");
  const fs::path config = home / "shattered-stars" / "config.ini";
  auto env = test_env(home);
  env.emplace_back("SS_DISC1", disc1_path());
  env.emplace_back("SS_EVENT_SWEEP", "");

  GameRun run = run_game({"--headless", "--frames", "30", "--map", "26"}, env);
  CHECK_EQ(run.code, 0);
  CHECK(contains(run.output, "stopped after 30 vblanks"));
  // A normal boot to the title, where the game checks the memory cards.
  run = run_game({"--headless", "--frames", "1500"}, env);
  CHECK_EQ(run.code, 0);

  run = run_game({"--headless", "--frames", "40", "--map", "26", "--map-pos", "0,0,0", "--input", "10:cross,12:none",
                  "--no-memcard", "--mods", launcher::path_utf8(home / "mods"), "--no-dither"},
                 env);
  CHECK_EQ(run.code, 0);
  CHECK(contains(run.output, "stopped after 40 vblanks"));
  CHECK(!fs::exists(config));  // headless: --no-dither is not remembered

  const std::string d2 = disc2_path();
  if (!d2.empty()) {  // the disc 2 sweep: boots from disc 2, disc 1 in the second drive
    auto swapped = test_env(home);
    swapped.emplace_back("SS_DISC1", d2);
    swapped.emplace_back("SS_DISC2", disc1_path());
    run = run_game({"--headless", "--frames", "30", "--map", "26"}, swapped);
    CHECK_EQ(run.code, 0);
    CHECK(contains(run.output, "stopped after 30 vblanks"));
  }

  auto missing = test_env(home);
  missing.emplace_back("SS_DISC1", launcher::path_utf8(home / "no such disc.cue"));
  run = run_game({"--headless", "--frames", "5"}, missing);
  CHECK_EQ(run.code, 2);
  CHECK(contains(run.output, "disc 1 image expected"));
  CHECK(contains(run.output, "usage:"));

  run = run_game({"--headless", "--frames", "5", "--no-such-option"}, env);
  CHECK_EQ(run.code, 2);
  CHECK(contains(run.output, "usage:"));
  CHECK(!fs::exists(config));
  // Headless runs leave the settings folder alone: no memory card made there either.
  std::error_code ec;
  for (const auto& e : fs::recursive_directory_iterator(home / "shattered-stars", ec)) {
    const std::string name = launcher::path_utf8(e.path().filename());
    CHECK(!(name.rfind("memcard", 0) == 0 && e.path().extension() == ".mcd"));
    CHECK(name != "config.ini");
    CHECK(name != "shattered-stars.log");
  }
}

// A windowed player's run (offscreen here) keeps its messages in shattered-stars.log beside
// config.ini, replaced at each start; a bad disc 1 there is a fatal error that lands in it.
TEST(game_windowed_run_writes_its_log) {
  if (!game_available()) return;
  const fs::path home = scratch("game_log");
  const fs::path dir = home / "shattered-stars";
  fs::create_directories(dir);
  write_file(dir / "config.ini", "disc1=" + disc1_path() + "\naudio=0\n");
  auto env = test_env(home);
  GameRun run = run_game({"--frames", "20"}, env);
  const std::string log = read_file(dir / "shattered-stars.log");
  CHECK(log.rfind("# Shattered Stars", 0) == 0);
  if (contains(run.output, "game window could not be opened")) {
    // No OpenGL offscreen (Wine): the fatal error is what the log must hold then.
    std::printf("  (no OpenGL window here: checked the error in the log instead)\n");
    CHECK_EQ(run.code, 1);
    CHECK(contains(log, "game window could not be opened"));
  } else {
    CHECK_EQ(run.code, 0);
  }

  // A config.ini pointing at a missing disc, and no setup to open (SS_SETUP_SCRIPT quits it
  // if one is built next to the game): the reason is in the log and the run exits 1.
  write_file(dir / "config.ini", "disc1=" + launcher::path_utf8(home / "gone.cue") + "\naudio=0\n");
  env.emplace_back("SS_SETUP_SCRIPT", "quit");
  run = run_game({"--frames", "20"}, env);
  CHECK_EQ(run.code, 1);
  const std::string log2 = read_file(dir / "shattered-stars.log");
  CHECK(contains(log2, "disc 1 image expected"));
  CHECK(contains(run.output, "disc 1 image expected"));  // and on stderr
  CHECK(log2.find('\r') == std::string::npos);           // "\n" line ends on every platform
}

// An Archipelago game is played as it is (the user, 2026-10-25): a windowed run with a seed
// ignores --debug, the debug starts and the SS_* hooks, and plays on the seed's own card. With
// config.ini's ap_enabled=0 (setup's switch) the seed isn't loaded at all: the original game.
TEST(game_archipelago_runs_without_debugging) {
  if (!game_available()) return;
  const fs::path home = scratch("game_ap_locked");
  const fs::path dir = home / "shattered-stars";
  fs::create_directories(dir);
  const fs::path seed = home / "lock.apso2";
  write_file(seed, R"({"format": "apso2", "version": 1, "seed": "lock-1", "slot": {"name": "Claude", "id": 1},
 "items": {"5000021": {"name": "Revival Card", "kind": "item", "game_id": 21}},
 "locations": [{"id": 1347569665, "name": "Arlia: crate by the store",
                "check": {"type": "chest", "map": 18, "flag": "0x962"},
                "item": {"player": 1, "kind": "fol", "amount": 5000, "name": "5000 Fol"}}]})");
  const std::string base = "disc1=" + disc1_path() + "\naudio=0\nap_seed=" + launcher::path_utf8(seed) + "\n";
  write_file(dir / "config.ini", base);
  auto env = test_env(home);
  env.emplace_back("SS_BATTLE_STUB", "1");
  GameRun run = run_game({"--frames", "20", "--debug", "--map", "18"}, env);
  if (contains(run.output, "game window could not be opened")) {
    std::printf("  (no OpenGL window here: skipped)\n");
    return;
  }
  CHECK_EQ(run.code, 0);
  CHECK(contains(run.output, "[ap] an Archipelago game plays without debugging; ignored: --debug, --map"));
  CHECK(contains(run.output, "SS_BATTLE_STUB"));
  CHECK(contains(run.output, "[ap] seed lock-1"));
  CHECK(contains(run.output, "[ap] the seed plays on its own memory card: " +
                                 launcher::path_utf8(launcher::seed_memcard("lock-1", "Claude", dir / "archipelago"))));
  // Archipelago off: no seed, nothing of it in the run.
  write_file(dir / "config.ini", base + "ap_enabled=0\n");
  run = run_game({"--frames", "20"}, env);
  CHECK_EQ(run.code, 0);
  CHECK(!contains(run.output, "[ap]"));
}

// Setup's header logo: the title picture from disc 1 (entry 12, block 0), decoded in memory.
TEST(launcher_title_picture_from_disc1) {
  const std::string d1 = disc1_path();
  if (d1.empty()) {
    std::printf("  (skipped: SS_DISC1 not set)\n");
    return;
  }
  const auto picture = launcher::read_title_picture(launcher::utf8_path(d1));
  CHECK(picture.has_value());
  if (!picture) return;
  CHECK_EQ(picture->width, 640);
  CHECK_EQ(picture->height, 480);
  CHECK_EQ(picture->rgba.size(), static_cast<size_t>(640 * 480));
  std::set<u32> colours(picture->rgba.begin(), picture->rgba.end());
  CHECK(colours.size() > 200);  // the nebula, not a flat fill
  auto bright = [&](int x0, int x1, int y0, int y1) {
    int n = 0;
    for (int y = y0; y < y1; ++y)
      for (int x = x0; x < x1; ++x) {
        const u32 c = picture->rgba[static_cast<size_t>(y) * 640 + x];
        if ((c & 0xFF) > 170 && (c >> 8 & 0xFF) > 170 && (c >> 16 & 0xFF) > 170) ++n;
      }
    return n;
  };
  // The silver logo fills the band setup shows; the copyright line is just below it.
  const int x0 = launcher::kLogoX, x1 = launcher::kLogoX + launcher::kLogoWidth;
  const int y0 = launcher::kLogoY, y1 = launcher::kLogoY + launcher::kLogoHeight;
  CHECK(bright(x0, x1, y0, y1) > 4000);
  CHECK(bright(x0, x1, y0, y0 + 12) < 20);       // nebula above the logo
  CHECK(bright(110, 490, y1 + 1, y1 + 14) > 300);  // "(c) 1999 tri-Ace ..." left out of the band
  const launcher::Picture band = launcher::crop(*picture, launcher::kLogoX, launcher::kLogoY, launcher::kLogoWidth,
                                                launcher::kLogoHeight);
  CHECK_EQ(band.width, launcher::kLogoWidth);
  CHECK_EQ(band.height, launcher::kLogoHeight);
  CHECK(band.rgba[0] == picture->rgba[static_cast<size_t>(launcher::kLogoY) * 640 + launcher::kLogoX]);
  // Anything else gives nothing.
  const fs::path dir = scratch("title_art");
  write_file(dir / "zeros.iso", std::string(400 * 2048, '\0'));
  CHECK(!launcher::read_title_picture(dir / "zeros.iso").has_value());
}

// Controls: config.ini round trip (only changed keys written), the window's key / button
// maps, and the player's stick settings winning over the game's own.
TEST(launcher_controls_round_trip) {
  using namespace ss::platform;
  const Controls defaults = Controls::defaults();
  const int cross = std::countr_zero(static_cast<unsigned>(kPadCross));
  const int l2 = std::countr_zero(static_cast<unsigned>(kPadL2));
  CHECK(pad_bits_for_key(defaults, SDLK_Z) == kPadCross);
  CHECK(pad_bits_for_key(defaults, SDLK_RETURN) == kPadStart);
  CHECK(pad_bits_for_button(defaults, SDL_GAMEPAD_BUTTON_SOUTH) == kPadCross);
  CHECK(pad_bits_for_button(defaults, kLeftTrigger) == kPadL2);  // the triggers are L2 / R2
  CHECK_EQ(pad_bits_for_key(defaults, 0), 0);

  launcher::Config empty;
  CHECK(launcher::read_controls(empty) == launcher::ControlSettings{});
  launcher::ControlSettings s;
  s.controls.keys[cross] = SDLK_SPACE;
  s.controls.buttons[l2] = kNoButton;
  s.stick_deadzone = 40;
  s.stick_dpad = false;
  launcher::Config c;
  launcher::write_controls(c, s);
  CHECK(c.get("key_cross") == "Space");
  CHECK(c.get("pad_l2") == "none");
  CHECK(!c.has("key_circle"));  // defaults stay out of the file
  CHECK(c.get("stick_deadzone") == "40");
  CHECK(c.get("stick_dpad") == "0");
  CHECK(launcher::read_controls(c) == s);
  CHECK_EQ(launcher::stick_deadzone_units(s), 51);
  CHECK_EQ(launcher::stick_deadzone_units(launcher::ControlSettings{}), -1);
  // Hand-written names, two keys on one button, a trigger elsewhere.
  c.set("key_circle", "Space");
  c.set("pad_r1", "righttrigger");
  const launcher::ControlSettings r = launcher::read_controls(c);
  CHECK(pad_bits_for_key(r.controls, SDLK_SPACE) == (kPadCross | kPadCircle));
  CHECK(pad_bits_for_button(r.controls, kRightTrigger) == (kPadR1 | kPadR2));
  CHECK_EQ(launcher::key_from_name("No Such Key"), 0);
  CHECK_EQ(launcher::gamepad_button_from_name("nonsense"), kNoButton);
}

TEST(pad_player_stick_settings_win_over_the_game) {
  game::Pad* pad = nullptr;
  game::pad_init(pad);
  game::pad_set_port_mask(pad, 1);
  const u8 centred[4] = {0x80, 0x80, 0x80, 0x80};
  game::vsync_callback_with(0, centred);  // a gamepad appears: the game's 0x20 dead zone, d-pad mode 1
  const u8 nudge[4] = {0x80, 0x80, 0x80 + 0x28, 0x80};  // left stick 40 units right
  s32 rx = 0, ry = 0, lx = 0, ly = 0;
  game::vsync_callback_with(0, nudge);
  game::pad_stick(pad, 0, &rx, &ry, &lx, &ly);
  CHECK_EQ(lx, 0x28 - 0x20);                         // less the game's dead zone
  CHECK((game::pad_current(pad, 0) & 0x2000) == 0);  // not past half way (0x28)
  game::pad_set_player_stick(0, -1);                 // the player's: no dead zone
  game::vsync_callback_with(0, nudge);
  game::pad_stick(pad, 0, &rx, &ry, &lx, &ly);
  CHECK_EQ(lx, 0x28);
  CHECK((game::pad_current(pad, 0) & 0x2000) != 0);  // the stick works the d-pad: right
  game::pad_set_stick_centre(pad, 0, 0, 0, 0, 0, 0x30);  // the game sets its own again...
  game::vsync_callback_with(0, nudge);
  game::pad_stick(pad, 0, &rx, &ry, &lx, &ly);
  CHECK_EQ(lx, 0x28);                                 // ...the player's setting stays
  game::pad_set_player_stick(0, 0);                  // the stick no longer works the d-pad
  game::vsync_callback_with(0, nudge);
  CHECK((game::pad_current(pad, 0) & 0x2000) == 0);
  game::pad_set_player_stick(-1, -1);
  game::vsync_callback_with(0);
  game::pad_init(pad);
}

// The game window turns keys into PS1 buttons through the player's controls. The video
// driver is forced to offscreen here (a hint over any environment), so no window shows.
TEST(window_honours_the_controls) {
  using namespace ss::platform;
  SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "offscreen", SDL_HINT_OVERRIDE);
  {
    Window window;
    std::string error;
    if (!window.create("controls test", 320, 240, &error)) {
      std::printf("  (skipped: no offscreen OpenGL window here: %s)\n", error.c_str());
    } else {
      Controls controls = Controls::defaults();
      controls.keys[std::countr_zero(static_cast<unsigned>(kPadCross))] = SDLK_SPACE;
      window.set_controls(controls);
      auto key = [&](SDL_Keycode k, bool down) {
        SDL_Event e{};
        e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
        e.key.key = k;
        e.key.down = down;
        SDL_PushEvent(&e);
        window.pump([](const KeyEvent&) {});
      };
      key(SDLK_SPACE, true);
      CHECK(window.pad() == kPadCross);
      key(SDLK_X, true);  // Circle as well
      CHECK(window.pad() == (kPadCross | kPadCircle));
      key(SDLK_SPACE, false);
      CHECK(window.pad() == kPadCircle);
      key(SDLK_X, false);
      key(SDLK_Z, true);  // Cross's old key does nothing now
      CHECK(window.pad() == 0);
      key(SDLK_Z, false);
    }
  }
  SDL_ResetHint(SDL_HINT_VIDEO_DRIVER);
}
