#pragma once

#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "core/types.h"
#include "launcher/config.h"
#include "launcher/discs.h"
#include "launcher/settings.h"
#include "mods/catalog.h"

struct SDL_Window;
struct ImFont;
union SDL_Event;

// The setup program's window (port feature): every setting a player needs, so the game
// itself never needs the command line. Pages: Discs, Display, Sound, Controls, Gameplay
// (instant text, auto-advance), Memory cards, Mods, and Archipelago (seed file, server, slot, password). Save writes config.ini (keeping keys this
// version doesn't know); Save & Play also starts the game.
namespace ss::setup {

enum class Page { kDiscs, kDisplay, kSound, kControls, kGameplay, kMemoryCards, kMods, kArchipelago, kCount };

struct Options {
  bool from_game = false;  // the game started setup and waits for it (launcher/process.h)
  bool screenshot = false; // a test run: never start anything or open dialogs
  Page page = Page::kDiscs;
  // Test states: "quit-confirm" (the unsaved-changes question), "game-missing" (the
  // error when the game is not next to setup).
  std::string state;
  std::vector<std::pair<std::string, std::string>> sets;  // --set KEY=VALUE before the first frame
  // SS_SETUP_SCRIPT / --do, one per frame: autodetect, save, play, quit, close (the window's
  // close button), set:KEY=VALUE (as if typed in); on the Mods page mods-install:ZIP,
  // mods-select:ID, mods-toggle:ID, mods-up, mods-down, mods-remove (asks; never confirmed),
  // mods-show-info (scrolls to the selected mod's details).
  std::vector<std::string> actions;
};

bool parse_page(const std::string& name, Page* out);

// Textures for pictures, made by the renderer in use (OpenGL or SDL's): an ImTextureID,
// 0 on failure. Pixels are RGBA8888 (red in the low byte); scaling filters linearly.
struct Textures {
  std::function<u64(const u32* rgba, int width, int height)> create;
  std::function<void(u64)> destroy;
};

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
  launcher::ControlSettings controls;
  launcher::TextSettings text;  // the Gameplay page's dialogue settings
  bool card_inserted[2] = {true, false};
  std::string card[2];
  std::string mods;
  std::string mod_order;  // config.ini's mod_order: the Mods page's list (mods/catalog.h)
  std::string ap_seed, ap_server, ap_slot, ap_password;  // Archipelago (config.ini's ap_* keys)
  std::string ap_seed_entry;  // the seed in an archive holding several (launcher/ap_seed.h)
  bool ap_enabled = true;     // the sidebar's Archipelago switch (config.ini ap_enabled, default on)
  bool operator==(const Settings&) const = default;
};

class App {
 public:
  App(SDL_Window* window, const Fonts& fonts, const Textures& textures, Options options);
  ~App();

  void frame();  // the UI for one frame (between ImGui::NewFrame and ImGui::Render)
  bool done() const { return done_; }
  int exit_code() const { return exit_code_; }
  void request_close();  // the window's close button
  void forget_file_checks() {  // focus regained: files may have changed
    disc_cache_.clear();
    card_cache_.clear();
    seed_cache_.clear();
    mods_scanned_ = false;
    mods_seed_lines_.clear();
  }
  // While a control is being bound: takes the key or gamepad button (true: consumed, not
  // for ImGui). Esc cancels, Delete unbinds; a click elsewhere cancels.
  bool capture_event(const SDL_Event& e);

 private:
  enum class Level { kOk, kInfo, kWarning, kError };
  enum class Target { kDisc1, kDisc2, kCard1, kCard2, kMods, kSeed, kModZip };
  struct SeedInfo {  // a quick look at a seed (the game reads it in full)
    bool ok = false;
    std::string error, seed, slot, game;
    size_t locations = 0;
    bool hell = false;  // options.hell_seed: the page warns (game::ap::kHellSeedWarning) [hellseed]
    std::string entry;  // in an archive: the seed read (its name in the archive)
    // An archive with several Star Ocean seeds: each one's name and slot, to choose from.
    std::vector<std::pair<std::string, std::string>> choices;
  };
  struct CardInfo {
    enum Kind { kNew, kNotACard, kDexDrive, kUnformatted, kReady } kind = kNew;
    int saves = 0, free_blocks = 0;
  };

  // Pages and parts.
  void header();
  void update_logo();  // the title logo from the Disc 1 on screen (when it is valid)
  void sidebar();
  void bottom_bar();
  void page_discs();
  void page_display();
  void page_sound();
  void page_controls();
  void control_cell(int bit, bool gamepad, float width, bool duplicate);
  void page_gameplay();
  void page_cards();
  void page_mods();
  void mods_list(const mods::Catalog& catalog, std::vector<mods::Slot>& order, float height);
  void mods_info(const mods::Catalog& catalog, int entry);
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

  // The Mods page (mods/catalog.h): the folder's packages, rescanned when the folder
  // changes, after an install or removal, and when the window regains focus; the conflict
  // analysis of the enabled ones (and an Archipelago seed's edits) for the list on screen.
  std::filesystem::path mods_folder_path() const;
  const mods::Catalog& mods_catalog();
  std::vector<mods::Slot> mods_order();
  void mods_set_order(const std::vector<mods::Slot>& order);
  void mods_analyse();
  std::string mods_name(int source) const;
  void mods_install(const std::string& zip);
  void mods_finish_install(bool replace);
  void mods_remove_selected();
  int mods_selected_entry();
  void mods_move(int delta);

  const launcher::DiscInfo& disc_info(const std::string& path);
  const CardInfo& card_info(const std::string& path);
  const SeedInfo& seed_info(const std::string& path, const std::string& entry);
  Level disc_level(int n);
  bool disc1_ok() { return disc_info(settings_.disc1).kind == launcher::DiscKind::kDisc1; }
  // Why the chosen discs can't be played together (Disc 2 of another release than Disc 1's),
  // or empty.
  std::string pair_error() {
    return launcher::describe_pair(disc_info(settings_.disc1), disc_info(settings_.disc2));
  }
  Settings read(const launcher::Config& c) const;
  // 'base' with the settings on screen written into it (the keys setup doesn't show stay).
  launcher::Config compose(const launcher::Config& base) const;

  SDL_Window* window_;
  Fonts fonts_;
  Textures textures_;
  u64 logo_ = 0;            // the logo band from the disc's title picture (0: none)
  int logo_width_ = 0, logo_height_ = 0;
  std::string logo_disc_;   // the disc it was read from (or tried)
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

  int capture_bit_ = -1;          // Controls: the PS1 button waiting for a key / button
  bool capture_gamepad_ = false;
  std::string controls_notice_;

  std::map<std::string, launcher::DiscInfo> disc_cache_;
  std::map<std::string, CardInfo> card_cache_;
  std::map<std::string, SeedInfo> seed_cache_;
  bool show_password_ = false;

  mods::Catalog mods_catalog_;
  bool mods_scanned_ = false;
  std::filesystem::path mods_scanned_folder_;
  int mods_generation_ = 0;          // bumped at each scan
  std::string mods_selected_;        // the selected entry's id
  std::string mods_analysis_key_;    // what the analysis below was made for
  std::vector<mods::Line> mods_lines_;
  mods::Analysis mods_analysis_;
  int mods_warnings_ = 0;            // conflicts worth a warning among the enabled entries
  bool mods_enabled_broken_ = false;  // an enabled package that can't load
  std::map<std::string, std::vector<std::string>> mods_seed_lines_;  // an Archipelago seed's edits, by path
  mods::ZipPackage mods_pending_;     // a package waiting for "replace?"
  std::string mods_pending_zip_;
  std::string mods_notice_;
  Level mods_notice_level_ = Level::kInfo;
  bool open_replace_popup_ = false, open_remove_popup_ = false;
  bool mods_scroll_to_info_ = false;

  // File dialogs answer asynchronously (maybe on another thread).
  std::mutex dialog_mutex_;
  std::vector<std::pair<Target, std::string>> dialog_results_;
  bool dialog_open_ = false;

 public:
  void dialog_results_lock(int target, const std::string& chosen);  // from the dialog callback
};

}  // namespace ss::setup
