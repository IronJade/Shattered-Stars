#pragma once

#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "launcher/config.h"
#include "launcher/discs.h"
#include "launcher/settings.h"

struct SDL_Window;
struct ImFont;

// The setup program's window (port feature): every setting a player needs, so the game
// itself never needs the command line. Pages: Discs, Display, Sound, Memory cards, Mods,
// and a greyed-out Archipelago placeholder. Save writes config.ini (keeping keys this
// version doesn't know); Save & Play also starts the game.
namespace ss::setup {

enum class Page { kDiscs, kDisplay, kSound, kMemoryCards, kMods, kArchipelago, kCount };

struct Options {
  bool from_game = false;  // the game started setup and waits for it (launcher/process.h)
  bool screenshot = false; // a test run: never start anything or open dialogs
  Page page = Page::kDiscs;
  // Test states: "quit-confirm" (the unsaved-changes question), "game-missing" (the
  // error when the game is not next to setup), "saved" (just after Save).
  std::string state;
  std::vector<std::pair<std::string, std::string>> sets;  // --set KEY=VALUE before the first frame
  std::vector<std::string> actions;  // SS_SETUP_SCRIPT / --do: autodetect, save, play, quit
};

bool parse_page(const std::string& name, Page* out);

struct Fonts {
  ImFont* regular = nullptr;
  ImFont* bold = nullptr;
  float size = 17.0f;  // base size (before the display's scale)
};

// What the setup edits, compared with what was saved to know about unsaved changes.
struct Settings {
  std::string disc1, disc2;
  launcher::DisplaySettings display;
  launcher::SoundSettings sound;
  bool card_inserted[2] = {true, false};
  std::string card[2];
  std::string mods;
  bool operator==(const Settings&) const = default;
};

class App {
 public:
  App(SDL_Window* window, const Fonts& fonts, Options options);

  void frame();  // the UI for one frame (between ImGui::NewFrame and ImGui::Render)
  bool done() const { return done_; }
  int exit_code() const { return exit_code_; }
  void request_close();  // the window's close button
  void forget_file_checks() { disc_cache_.clear(); card_cache_.clear(); }  // focus regained

 private:
  enum class Level { kOk, kInfo, kWarning, kError };
  enum class Target { kDisc1, kDisc2, kCard1, kCard2, kMods };
  struct CardInfo {
    enum Kind { kNew, kNotACard, kDexDrive, kUnformatted, kReady } kind = kNew;
    int saves = 0, free_blocks = 0;
  };

  // Pages and parts.
  void header();
  void sidebar();
  void bottom_bar();
  void page_discs();
  void page_display();
  void page_sound();
  void page_cards();
  void page_mods();
  void page_archipelago();
  void popups();
  void disc_field(int n);
  void card_field(int slot);

  // Widgets.
  void heading(const char* text);
  void note(const std::string& text);
  void status_line(Level level, const std::string& text);
  bool path_input(const char* id, std::string& value, const char* hint, float reserve);
  void status_icon(Level level, float size);

  // Actions.
  void auto_detect(bool quiet);
  bool save();
  void play();
  void quit();
  void browse(Target target);
  void apply_dialog_results();
  void run_action(const std::string& action);
  void open_folder(const std::filesystem::path& folder);

  const launcher::DiscInfo& disc_info(const std::string& path);
  const CardInfo& card_info(const std::string& path);
  Level disc_level(int n);
  bool disc1_ok() { return disc_info(settings_.disc1).kind == launcher::DiscKind::kDisc1; }
  Settings read(const launcher::Config& c) const;

  SDL_Window* window_;
  Fonts fonts_;
  Options options_;
  launcher::Config config_;  // config.ini as last read or written
  Settings settings_, saved_;
  Page page_ = Page::kDiscs;
  bool done_ = false;
  int exit_code_ = 0;
  int frame_ = 0;
  std::string notice_;        // after Auto-detect
  Level notice_level_ = Level::kInfo;
  std::string saved_message_;  // after Save
  std::string error_message_;  // the error popup's text
  bool open_quit_popup_ = false, open_error_popup_ = false;
  bool quit_after_save_ = false;

  std::map<std::string, launcher::DiscInfo> disc_cache_;
  std::map<std::string, CardInfo> card_cache_;

  // File dialogs answer asynchronously (maybe on another thread).
  std::mutex dialog_mutex_;
  std::vector<std::pair<Target, std::string>> dialog_results_;
  bool dialog_open_ = false;

 public:
  void dialog_results_lock(int target, const std::string& chosen);  // from the dialog callback
};

}  // namespace ss::setup
