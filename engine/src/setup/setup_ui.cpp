#include "setup/setup_ui.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <system_error>

#include "imgui.h"
#include "imgui_stdlib.h"
#include "game/ap/seed.h"
#include "launcher/process.h"
#include "launcher/title_art.h"
#include "platform/window.h"
#include "psx/memory_card.h"

namespace ss::setup {
namespace {

namespace fs = std::filesystem;
using launcher::DiscKind;

// Colours (the theme itself is set in setup_main.cpp).
constexpr ImU32 kOk = IM_COL32(76, 195, 138, 255);
constexpr ImU32 kInfo = IM_COL32(110, 150, 220, 255);
constexpr ImU32 kWarning = IM_COL32(232, 176, 74, 255);
constexpr ImU32 kError = IM_COL32(229, 83, 75, 255);
constexpr ImVec4 kAccent(0.24f, 0.48f, 0.85f, 1.0f);
constexpr ImVec4 kAccentHover(0.30f, 0.55f, 0.92f, 1.0f);
constexpr ImVec4 kAccentActive(0.19f, 0.41f, 0.76f, 1.0f);

constexpr const char* kPageNames[] = {"Discs", "Display", "Sound", "Controls", "Memory cards", "Mods", "Archipelago"};
constexpr const char* kPageIds[] = {"discs", "display", "sound", "controls", "memcards", "mods", "archipelago"};
static_assert(std::size(kPageNames) == static_cast<size_t>(Page::kCount));

// The North American game's save names on a card: "BASCUS-94421S02-Snn".
constexpr const char* kSavePrefix = "BASCUS-94421S02";

float em() { return ImGui::GetFontSize(); }

std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

// A pasted path loses the quotes Windows' "Copy as path" adds, and stray spaces.
std::string clean_path(std::string s) {
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\n' || s.back() == '\r')) s.pop_back();
  size_t start = s.find_first_not_of(" \t");
  s = start == std::string::npos ? std::string() : s.substr(start);
  if (s.size() >= 2 && s.front() == '"' && s.back() == '"') s = s.substr(1, s.size() - 2);
  return s;
}

std::string plural(int n, const char* one, const char* many) { return std::to_string(n) + " " + (n == 1 ? one : many); }

bool same_file(const std::string& a, const std::string& b) {
  if (a.empty() || b.empty()) return false;
  std::error_code ec;
  if (fs::equivalent(launcher::utf8_path(a), launcher::utf8_path(b), ec)) return true;
  return launcher::utf8_path(a).lexically_normal() == launcher::utf8_path(b).lexically_normal();
}

// A file: URL for SDL_OpenURL (the system's file manager), percent-encoded.
std::string file_url(const fs::path& folder) {
  std::string path = launcher::path_utf8(folder);
  std::replace(path.begin(), path.end(), '\\', '/');
  std::string url = path.rfind('/', 0) == 0 ? "file://" : "file:///";
  for (unsigned char c : path) {
    if (std::isalnum(c) || c == '/' || c == '-' || c == '_' || c == '.' || c == '~' || c == ':') {
      url += static_cast<char>(c);
    } else {
      char buf[4];
      std::snprintf(buf, sizeof buf, "%%%02X", c);
      url += buf;
    }
  }
  return url;
}

}  // namespace

bool parse_page(const std::string& name, Page* out) {
  for (size_t i = 0; i < std::size(kPageIds); ++i) {
    if (name == kPageIds[i]) {
      *out = static_cast<Page>(i);
      return true;
    }
  }
  return false;
}

// ---------------------------------------------------------------------------------------
// State

static_assert(sizeof(ImTextureID) == sizeof(u64));

App::App(SDL_Window* window, const Fonts& fonts, const Textures& textures, Options options)
    : window_(window), fonts_(fonts), textures_(textures), options_(std::move(options)) {
  config_ = launcher::Config::load();
  saved_ = read(config_);
  // --set KEY=VALUE (tests): as if the player had changed these, not yet saved.
  launcher::Config edited = config_;
  for (const auto& [key, value] : options_.sets) edited.set(key, value);
  settings_ = read(edited);
  page_ = options_.page;
}

App::~App() {
  if (logo_ && textures_.destroy) textures_.destroy(logo_);
}

void App::update_logo() {
  // Read once per disc: at start from config.ini's Disc 1, then whenever the player picks
  // another valid one. Decoded in memory only (launcher/title_art.h).
  if (!disc1_ok() || settings_.disc1 == logo_disc_ || !textures_.create) return;
  logo_disc_ = settings_.disc1;
  const auto picture = launcher::read_title_picture(launcher::utf8_path(settings_.disc1));
  if (!picture) return;
  const launcher::Picture band =
      launcher::crop(*picture, launcher::kLogoX, launcher::kLogoY, launcher::kLogoWidth, launcher::kLogoHeight);
  const u64 texture = textures_.create(band.rgba.data(), band.width, band.height);
  if (!texture) return;
  if (logo_ && textures_.destroy) textures_.destroy(logo_);
  logo_ = texture;
  logo_width_ = band.width;
  logo_height_ = band.height;
}

Settings App::read(const launcher::Config& c) const {
  Settings s;
  s.disc1 = c.get("disc1");
  s.disc2 = c.get("disc2");
  s.display = launcher::read_display(c);
  s.sound = launcher::read_sound(c);
  s.controls = launcher::read_controls(c);
  for (int slot = 1; slot <= 2; ++slot) {
    const std::string path = launcher::memcard_path(c, slot);
    s.card_inserted[slot - 1] = !path.empty();
    s.card[slot - 1] = path;
  }
  s.mods = launcher::path_utf8(launcher::mods_folder(c));
  s.ap_seed = c.get("ap_seed");
  s.ap_server = c.get("ap_server");
  s.ap_slot = c.get("ap_slot");
  s.ap_password = c.get("ap_password");
  return s;
}

const App::SeedInfo& App::seed_info(const std::string& path) {
  auto it = seed_cache_.find(path);
  if (it != seed_cache_.end()) return it->second;
  // The game's own reader (game/ap/seed.h): what loads here loads in the game.
  SeedInfo info;
  game::ap::Seed seed;
  std::string error;
  std::error_code ec;
  if (!fs::exists(launcher::utf8_path(path), ec)) {
    info.error = "File not found. Was it moved or renamed?";
  } else if (!game::ap::load_seed_file(launcher::path_utf8(launcher::utf8_path(path)), &seed, &error)) {
    info.error = "This seed can't be used: " + error;
  } else {
    info.ok = true;
    info.seed = seed.seed;
    info.slot = seed.slot_name;
    info.game = seed.game;
    info.locations = seed.locations.size();
  }
  return seed_cache_.emplace(path, info).first->second;
}

const launcher::DiscInfo& App::disc_info(const std::string& path) {
  auto it = disc_cache_.find(path);
  if (it == disc_cache_.end()) it = disc_cache_.emplace(path, launcher::identify_disc(launcher::utf8_path(path))).first;
  return it->second;
}

const App::CardInfo& App::card_info(const std::string& path) {
  auto it = card_cache_.find(path);
  if (it != card_cache_.end()) return it->second;
  CardInfo info;
  const fs::path p = launcher::utf8_path(path);
  std::error_code ec;
  if (fs::exists(p, ec)) {
    const auto size = fs::is_regular_file(p, ec) ? fs::file_size(p, ec) : 0;
    const bool gme = lower(launcher::path_utf8(p.extension())) == ".gme";
    psx::MemoryCard card;
    if (ec || (size != psx::MemoryCard::kSize && !(gme && size == psx::MemoryCard::kSize + 0xF40)) ||
        !card.open(p, false)) {
      info.kind = CardInfo::kNotACard;
    } else if (!card.formatted()) {
      info.kind = gme ? CardInfo::kNotACard : CardInfo::kUnformatted;
    } else {
      info.kind = gme ? CardInfo::kDexDrive : CardInfo::kReady;
      for (const auto& f : card.files())
        if (f.name.rfind(kSavePrefix, 0) == 0) ++info.saves;
      info.free_blocks = card.free_blocks();
    }
  }
  return card_cache_.emplace(path, info).first->second;
}

App::Level App::disc_level(int n) {
  const std::string& path = n == 1 ? settings_.disc1 : settings_.disc2;
  const DiscKind kind = disc_info(path).kind;
  if (kind == (n == 1 ? DiscKind::kDisc1 : DiscKind::kDisc2)) return Level::kOk;
  if (kind == DiscKind::kNone) return n == 1 ? Level::kWarning : Level::kInfo;
  return n == 1 ? Level::kError : Level::kWarning;
}

// ---------------------------------------------------------------------------------------
// Actions

void App::auto_detect(bool quiet) {
  const auto folders = launcher::disc_folders(launcher::config_dir());
  launcher::FoundDiscs found = launcher::find_discs(folders);
  const bool had_disc2 = disc_info(settings_.disc2).kind == DiscKind::kDisc2;
  if (found.disc2.empty() && !found.disc1.empty()) found.disc2 = launcher::find_disc2_near(launcher::utf8_path(found.disc1));
  if (found.disc2.empty() && found.disc1.empty() && disc1_ok() && !had_disc2)
    found.disc2 = launcher::find_disc2_near(launcher::utf8_path(settings_.disc1));
  if (!found.disc1.empty()) settings_.disc1 = found.disc1;
  if (!found.disc2.empty()) settings_.disc2 = found.disc2;
  if (!found.disc1.empty() && !found.disc2.empty()) {
    notice_ = "Found Disc 1 and Disc 2.";
    notice_level_ = Level::kOk;
  } else if (!found.disc1.empty()) {
    notice_ = "Found Disc 1 (no Disc 2 there).";
    notice_level_ = Level::kOk;
  } else if (!found.disc2.empty()) {
    notice_ = "Found Disc 2, but no Disc 1.";
    notice_level_ = Level::kWarning;
  } else {
    notice_ = "No disc images found in the discs folders.";
    notice_level_ = Level::kWarning;
  }
  if (quiet) {
    if (found.disc1.empty()) notice_.clear();
    else notice_ = "Found your discs in the discs folder. Press Save to keep them.";
  }
}

launcher::Config App::compose(const launcher::Config& base) const {
  launcher::Config c = base;
  c.set("disc1", settings_.disc1);
  if (settings_.disc2.empty()) c.erase("disc2");
  else c.set("disc2", settings_.disc2);
  launcher::write_display(c, settings_.display);
  launcher::write_sound(c, settings_.sound);
  launcher::write_controls(c, settings_.controls);
  for (int slot = 1; slot <= 2; ++slot) {
    const std::string key = "memcard" + std::to_string(slot);
    const std::string path = settings_.card[slot - 1];
    const bool inserted = settings_.card_inserted[slot - 1] && !path.empty();
    const bool is_default = slot == 1 && same_file(path, launcher::path_utf8(launcher::default_memcard(1)));
    if (!inserted) {
      if (slot == 1 || c.has(key)) c.set(key, "none");
    } else if (is_default && !c.has(key)) {
      // The default card stays implicit (it follows the settings folder).
    } else {
      c.set(key, path);
    }
  }
  const std::string default_mods = launcher::path_utf8(launcher::config_dir() / "mods");
  if (settings_.mods.empty() || same_file(settings_.mods, default_mods)) c.erase("mods");
  else c.set("mods", settings_.mods);
  const std::pair<const char*, const std::string*> ap_keys[] = {{"ap_seed", &settings_.ap_seed},
                                                                {"ap_server", &settings_.ap_server},
                                                                {"ap_slot", &settings_.ap_slot},
                                                                {"ap_password", &settings_.ap_password}};
  for (const auto& [key, value] : ap_keys) {
    if (value->empty()) c.erase(key);
    else c.set(key, *value);
  }
  return c;
}

bool App::save() {
  const bool cards_clash = settings_.card_inserted[0] && settings_.card_inserted[1] && same_file(settings_.card[0], settings_.card[1]);
  if (!disc1_ok() || cards_clash) return false;
  // Start from the file as it is now: keys this version doesn't know (or that the game
  // wrote meanwhile) stay as they are.
  launcher::Config c = compose(launcher::Config::load(config_.path()));
  std::string error;
  if (!c.save(&error)) {
    error_message_ = "The settings could not be saved.\n\n" + error;
    open_error_popup_ = true;
    return false;
  }
  config_ = c;
  saved_ = read(config_);
  settings_ = saved_;
  saved_message_ = "Saved.";
  return true;
}

void App::play() {
  if (!save()) return;
  if (options_.from_game) {  // the game is waiting for us
    exit_code_ = launcher::kSetupPlay;
    done_ = true;
    return;
  }
  const fs::path game = launcher::sibling_program(launcher::kGameProgram);
  if (!game.empty() && options_.screenshot) {  // test runs never start anything
    std::printf("setup: would start %s\n", launcher::path_utf8(game).c_str());
    done_ = true;
    return;
  }
  std::string error;
  if (game.empty() || !launcher::start_program(game, {}, &error)) {
    error_message_ = game.empty() ? std::string("The game (") + launcher::kGameProgram +
                                        ") is not in the same folder as setup, so it can't be started from here.\n\n"
                                        "Your settings are saved: start the game itself."
                                  : "The game could not be started:\n\n" + error;
    open_error_popup_ = true;
    return;
  }
  exit_code_ = 0;
  done_ = true;
}

void App::quit() {
  exit_code_ = options_.from_game ? launcher::kSetupQuit : 0;
  done_ = true;
}

void App::request_close() {
  if (settings_ == saved_) quit();
  else open_quit_popup_ = true;
}

void App::run_action(const std::string& action) {
  if (action.rfind("set:", 0) == 0 && action.find('=') != std::string::npos) {  // set:KEY=VALUE
    const auto eq = action.find('=');
    launcher::Config c = compose(config_);
    c.set(action.substr(4, eq - 4), action.substr(eq + 1));
    settings_ = read(c);
  } else if (action == "autodetect") auto_detect(false);
  else if (action == "save") save();
  else if (action == "play") play();
  else if (action == "quit") quit();
  else if (action == "close") request_close();
  else std::fprintf(stderr, "setup: unknown action '%s'\n", action.c_str());
}

void App::open_folder(const fs::path& folder) {
  std::error_code ec;
  fs::create_directories(folder, ec);
  if (options_.screenshot) return;
  if (!SDL_OpenURL(file_url(folder).c_str())) {
    error_message_ = "The folder could not be opened:\n\n" + launcher::path_utf8(folder) + "\n\n" + SDL_GetError();
    open_error_popup_ = true;
  }
}

struct DialogCall {
  App* app;
  int target;
  std::string location;  // kept alive until the dialog answers
};

namespace {

void SDLCALL on_dialog(void* user, const char* const* files, int /*filter*/) {
  auto* call = static_cast<DialogCall*>(user);
  std::string chosen;
  if (files == nullptr) chosen = std::string("\x01") + SDL_GetError();  // no dialog available
  else if (files[0] != nullptr) chosen = files[0];                     // else cancelled
  call->app->dialog_results_lock(call->target, chosen);
  delete call;
}

}  // namespace

void App::dialog_results_lock(int target, const std::string& chosen) {
  std::lock_guard<std::mutex> lock(dialog_mutex_);
  dialog_results_.emplace_back(static_cast<Target>(target), chosen);
}

void App::browse(Target target) {
  if (options_.screenshot || dialog_open_) return;
  static const SDL_DialogFileFilter disc_filters[] = {
      {"Disc images (.cue, .bin, .iso, .img)", launcher::kDiscImagePatterns},
      {"All files", "*"},
  };
  static const SDL_DialogFileFilter card_filters[] = {
      {"Memory card images (.mcd, .mcr, .gme)", "mcd;mcr;gme;mc;srm"},
      {"All files", "*"},
  };
  std::string current;
  switch (target) {
    case Target::kDisc1: current = settings_.disc1; break;
    case Target::kDisc2: current = settings_.disc2.empty() ? settings_.disc1 : settings_.disc2; break;
    case Target::kCard1: current = settings_.card[0]; break;
    case Target::kCard2: current = settings_.card[1]; break;
    case Target::kMods: current = settings_.mods; break;
    case Target::kSeed: current = settings_.ap_seed; break;
  }
  auto* call = new DialogCall{this, static_cast<int>(target), {}};
  std::error_code ec;
  if (!current.empty()) {
    const fs::path p = launcher::utf8_path(current);
    const fs::path dir = target == Target::kMods ? p : p.parent_path();
    if (fs::is_directory(dir, ec)) call->location = launcher::path_utf8(dir);
  }
  if (call->location.empty()) {
    for (const fs::path& d : launcher::disc_folders(launcher::config_dir()))
      if (fs::is_directory(d, ec)) {
        call->location = launcher::path_utf8(d);
        break;
      }
  }
  const char* location = call->location.empty() ? nullptr : call->location.c_str();
  dialog_open_ = true;
  if (target == Target::kMods) {
    SDL_ShowOpenFolderDialog(on_dialog, call, window_, location, false);
  } else if (target == Target::kDisc1 || target == Target::kDisc2) {
    SDL_ShowOpenFileDialog(on_dialog, call, window_, disc_filters, 2, location, false);
  } else if (target == Target::kSeed) {
    static const SDL_DialogFileFilter seed_filters[] = {{"Archipelago seed (.apso2)", "apso2"}, {"All files", "*"}};
    SDL_ShowOpenFileDialog(on_dialog, call, window_, seed_filters, 2, location, false);
  } else {
    SDL_ShowOpenFileDialog(on_dialog, call, window_, card_filters, 2, location, false);
  }
}

void App::apply_dialog_results() {
  std::vector<std::pair<Target, std::string>> results;
  {
    std::lock_guard<std::mutex> lock(dialog_mutex_);
    results.swap(dialog_results_);
  }
  for (auto& [target, chosen] : results) {
    dialog_open_ = false;
    if (!chosen.empty() && chosen[0] == '\x01') {
      error_message_ = "The file dialog could not be opened:\n\n" + chosen.substr(1) +
                       "\n\nYou can type or paste the path into the box instead.";
      open_error_popup_ = true;
      continue;
    }
    if (chosen.empty()) continue;  // cancelled
    switch (target) {
      case Target::kDisc1:
      case Target::kDisc2: {
        const int wanted = target == Target::kDisc1 ? 1 : 2;
        const DiscKind kind = disc_info(chosen).kind;
        // The other disc chosen by mistake goes where it belongs, when that slot is free.
        if (wanted == 1 && kind == DiscKind::kDisc2 && disc_info(settings_.disc2).kind != DiscKind::kDisc2) {
          settings_.disc2 = chosen;
          notice_ = "That image is Disc 2, so it went into the Disc 2 box. Now choose Disc 1.";
          notice_level_ = Level::kWarning;
          break;
        }
        if (wanted == 2 && kind == DiscKind::kDisc1 && !disc1_ok()) {
          settings_.disc1 = chosen;
          notice_ = "That image is Disc 1, so it went into the Disc 1 box.";
          notice_level_ = Level::kWarning;
          break;
        }
        (wanted == 1 ? settings_.disc1 : settings_.disc2) = chosen;
        notice_.clear();
        // Disc 1 chosen: disc 2 is often right beside it.
        if (wanted == 1 && kind == DiscKind::kDisc1 && disc_info(settings_.disc2).kind != DiscKind::kDisc2) {
          const std::string disc2 = launcher::find_disc2_near(launcher::utf8_path(chosen));
          if (!disc2.empty()) {
            settings_.disc2 = disc2;
            notice_ = "Found Disc 2 next to Disc 1.";
            notice_level_ = Level::kOk;
          }
        }
        break;
      }
      case Target::kCard1: settings_.card[0] = chosen; break;
      case Target::kCard2: settings_.card[1] = chosen; break;
      case Target::kMods: settings_.mods = chosen; break;
      case Target::kSeed: settings_.ap_seed = chosen; break;
    }
  }
}

// ---------------------------------------------------------------------------------------
// Widgets

void App::status_icon(Level level, float size) {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 pos = ImGui::GetCursorScreenPos();
  const float r = size * 0.5f;
  const ImVec2 c(pos.x + r, pos.y + ImGui::GetTextLineHeight() * 0.5f);
  const ImU32 colour = level == Level::kOk ? kOk : level == Level::kWarning ? kWarning : level == Level::kError ? kError : kInfo;
  dl->AddCircleFilled(c, r, colour, 20);
  const ImU32 ink = IM_COL32(16, 20, 30, 255);
  const float t = std::max(1.5f, size * 0.13f);
  switch (level) {
    case Level::kOk:
      dl->AddLine(ImVec2(c.x - r * 0.45f, c.y + r * 0.02f), ImVec2(c.x - r * 0.1f, c.y + r * 0.38f), ink, t);
      dl->AddLine(ImVec2(c.x - r * 0.1f, c.y + r * 0.38f), ImVec2(c.x + r * 0.5f, c.y - r * 0.35f), ink, t);
      break;
    case Level::kError:
      dl->AddLine(ImVec2(c.x - r * 0.38f, c.y - r * 0.38f), ImVec2(c.x + r * 0.38f, c.y + r * 0.38f), ink, t);
      dl->AddLine(ImVec2(c.x + r * 0.38f, c.y - r * 0.38f), ImVec2(c.x - r * 0.38f, c.y + r * 0.38f), ink, t);
      break;
    case Level::kWarning:
      dl->AddLine(ImVec2(c.x, c.y - r * 0.5f), ImVec2(c.x, c.y + r * 0.12f), ink, t);
      dl->AddCircleFilled(ImVec2(c.x, c.y + r * 0.45f), t * 0.6f, ink);
      break;
    case Level::kInfo:
      dl->AddLine(ImVec2(c.x, c.y - r * 0.1f), ImVec2(c.x, c.y + r * 0.5f), ink, t);
      dl->AddCircleFilled(ImVec2(c.x, c.y - r * 0.45f), t * 0.6f, ink);
      break;
  }
  ImGui::Dummy(ImVec2(size, ImGui::GetTextLineHeight()));
}

void App::status_line(Level level, const std::string& text) {
  status_icon(level, em() * 0.95f);
  ImGui::SameLine(0, em() * 0.5f);
  const ImU32 colour = level == Level::kError ? IM_COL32(240, 140, 132, 255)
                       : level == Level::kWarning ? IM_COL32(240, 200, 120, 255)
                       : level == Level::kOk ? IM_COL32(150, 225, 185, 255)
                                             : ImGui::GetColorU32(ImGuiCol_Text);
  ImGui::PushStyleColor(ImGuiCol_Text, colour);
  ImGui::TextWrapped("%s", text.c_str());
  ImGui::PopStyleColor();
}

void App::heading(const char* text) {
  ImGui::PushFont(fonts_.bold, fonts_.size * 1.45f);
  ImGui::TextUnformatted(text);
  ImGui::PopFont();
  ImGui::Dummy(ImVec2(0, em() * 0.15f));
}

void App::note(const std::string& text) {
  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
  ImGui::TextWrapped("%s", text.c_str());
  ImGui::PopStyleColor();
}

// A path box. At rest it shows the end of the path ("...Disc 1)/SO2 (Disc 1).cue"), where
// the file name is; a click (or Enter on it) turns it into a text box for typing or pasting.
bool App::path_input(const char* id, std::string& value, const char* hint, float reserve) {
  const float width = std::max(em() * 8, ImGui::GetContentRegionAvail().x - reserve);
  ImGuiStorage* storage = ImGui::GetStateStorage();
  const ImGuiID editing_key = ImGui::GetID(id), focus_key = ImGui::GetID((std::string(id) + "#focus").c_str());
  bool changed = false;
  if (storage->GetBool(editing_key, false)) {
    if (storage->GetBool(focus_key, false)) {
      ImGui::SetKeyboardFocusHere();
      storage->SetBool(focus_key, false);
    }
    ImGui::SetNextItemWidth(width);
    std::string edited = value;
    if (ImGui::InputTextWithHint(id, hint, &edited, ImGuiInputTextFlags_AutoSelectAll)) {
      value = clean_path(edited);
      changed = true;
    }
    if (ImGui::IsItemDeactivated()) storage->SetBool(editing_key, false);
    return changed;
  }
  const ImGuiStyle& style = ImGui::GetStyle();
  const ImVec2 pos = ImGui::GetCursorScreenPos();
  const ImVec2 size(width, ImGui::GetFrameHeight());
  if (ImGui::InvisibleButton(id, size)) {
    storage->SetBool(editing_key, true);
    storage->SetBool(focus_key, true);
  }
  const bool hovered = ImGui::IsItemHovered();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 end(pos.x + size.x, pos.y + size.y);
  dl->AddRectFilled(pos, end, ImGui::GetColorU32(hovered ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg), style.FrameRounding);
  if (style.FrameBorderSize > 0)
    dl->AddRect(pos, end, ImGui::GetColorU32(ImGuiCol_Border), style.FrameRounding, style.FrameBorderSize);
  const float room = size.x - style.FramePadding.x * 2;
  std::string shown = value.empty() ? std::string(hint) : value;
  bool cut = false;
  if (!value.empty() && ImGui::CalcTextSize(shown.c_str()).x > room) {
    size_t start = 0;  // drop leading characters (whole UTF-8 sequences) until it fits
    while (start < value.size() && ImGui::CalcTextSize(("..." + value.substr(start)).c_str()).x > room) {
      ++start;
      while (start < value.size() && (static_cast<unsigned char>(value[start]) & 0xC0) == 0x80) ++start;
    }
    shown = "..." + value.substr(start);
    cut = true;
  }
  dl->PushClipRect(pos, end, true);
  dl->AddText(ImVec2(pos.x + style.FramePadding.x, pos.y + style.FramePadding.y),
              ImGui::GetColorU32(value.empty() ? ImGuiCol_TextDisabled : ImGuiCol_Text), shown.c_str());
  dl->PopClipRect();
  if (hovered && cut) ImGui::SetTooltip("%s", value.c_str());
  return false;
}

namespace {

void subheading(ImFont* bold, float size, const char* text) {
  ImGui::Dummy(ImVec2(0, ImGui::GetFontSize() * 0.35f));
  ImGui::PushFont(bold, size * 1.08f);
  ImGui::TextUnformatted(text);
  ImGui::PopFont();
}

bool primary_button(const char* label, const ImVec2& size = ImVec2(0, 0)) {
  ImGui::PushStyleColor(ImGuiCol_Button, kAccent);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kAccentHover);
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, kAccentActive);
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
  const bool pressed = ImGui::Button(label, size);
  ImGui::PopStyleColor(4);
  return pressed;
}

float button_width(const char* label) {
  return ImGui::CalcTextSize(label, nullptr, true).x + ImGui::GetStyle().FramePadding.x * 2;
}

// Checkboxes and radio buttons a little smaller than the text boxes.
bool check(const char* label, bool* v) {
  const float pad = ImGui::GetFontSize() * 0.16f;
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(pad, pad));
  const bool changed = ImGui::Checkbox(label, v);
  ImGui::PopStyleVar();
  return changed;
}

bool radio(const char* label, bool active) {
  const float pad = ImGui::GetFontSize() * 0.16f;
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(pad, pad));
  const bool pressed = ImGui::RadioButton(label, active);
  ImGui::PopStyleVar();
  return pressed;
}

// Indent of a checkbox's description, under its label.
float check_indent() { return ImGui::GetFontSize() * 1.32f + ImGui::GetStyle().ItemInnerSpacing.x; }

// A label column: the text, then the widget at a fixed indent (further when the text is
// wider, as with a wide fallback font).
void field_label(const char* text, float width) {
  const float start = ImGui::GetCursorPosX();  // SameLine's offset counts from the window's edge
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(text);
  ImGui::SameLine(start + std::max(width, ImGui::CalcTextSize(text).x + ImGui::GetStyle().ItemSpacing.x));
}

}  // namespace

// ---------------------------------------------------------------------------------------
// The window

void App::frame() {
  ++frame_;
  apply_dialog_results();
  if (frame_ == 1) {
    if (!disc1_ok() && settings_.disc1.empty() && options_.sets.empty()) auto_detect(true);
    if (options_.state == "quit-confirm") open_quit_popup_ = true;
    if (options_.state == "capture-key" || options_.state == "capture-pad") {
      capture_bit_ = std::countr_zero(static_cast<unsigned>(platform::kPadCross));
      capture_gamepad_ = options_.state == "capture-pad";
    }
    if (options_.state == "game-missing") {
      error_message_ = std::string("The game (") + launcher::kGameProgram +
                       ") is not in the same folder as setup, so it can't be started from here.\n\n"
                       "Your settings are saved: start the game itself.";
      open_error_popup_ = true;
    }
  } else if (!options_.actions.empty()) {  // one scripted action per frame
    const std::string action = options_.actions.front();
    options_.actions.erase(options_.actions.begin());
    run_action(action);
  }
  if (settings_ != saved_) saved_message_.clear();
  update_logo();

  const ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(vp->WorkPos);
  ImGui::SetNextWindowSize(vp->WorkSize);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
  ImGui::Begin("##setup", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar |
                   ImGuiWindowFlags_NoScrollWithMouse);
  ImGui::PopStyleVar(2);

  header();
  const float bar_height = em() * 3.4f;
  const float body_height = std::max(em() * 4, ImGui::GetContentRegionAvail().y - bar_height);

  ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(12, 16, 26, 255));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(em() * 0.6f, em() * 0.9f));
  ImGui::BeginChild("##sidebar", ImVec2(em() * 11.5f, body_height), ImGuiChildFlags_AlwaysUseWindowPadding,
                    ImGuiWindowFlags_NoScrollbar);
  sidebar();
  ImGui::EndChild();
  ImGui::PopStyleVar();
  ImGui::PopStyleColor();

  ImGui::SameLine(0, 0);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(em() * 1.6f, em() * 1.2f));
  ImGui::BeginChild("##page", ImVec2(0, body_height), ImGuiChildFlags_AlwaysUseWindowPadding);
  ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX());
  switch (page_) {
    case Page::kDiscs: page_discs(); break;
    case Page::kDisplay: page_display(); break;
    case Page::kSound: page_sound(); break;
    case Page::kControls: page_controls(); break;
    case Page::kMemoryCards: page_cards(); break;
    case Page::kMods: page_mods(); break;
    case Page::kArchipelago: page_archipelago(); break;
    case Page::kCount: break;
  }
  ImGui::PopTextWrapPos();
  ImGui::EndChild();
  ImGui::PopStyleVar();

  bottom_bar();
  popups();
  ImGui::End();
}

void App::header() {
  const float height = em() * 5.6f;
  const ImVec2 p0 = ImGui::GetCursorScreenPos();
  const ImVec2 p1(p0.x + ImGui::GetContentRegionAvail().x, p0.y + height);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->AddRectFilledMultiColor(p0, p1, IM_COL32(10, 22, 52, 255), IM_COL32(34, 20, 66, 255), IM_COL32(26, 14, 50, 255),
                              IM_COL32(8, 16, 38, 255));
  // A starfield (fixed, so screenshots compare).
  u32 seed = 0x5eed1234;
  auto rnd = [&]() {
    seed = seed * 1664525u + 1013904223u;
    return static_cast<float>(seed >> 8) / static_cast<float>(1 << 24);
  };
  const float scale = em() / 17.0f;
  for (int i = 0; i < 90; ++i) {
    const ImVec2 at(p0.x + rnd() * (p1.x - p0.x), p0.y + rnd() * height);
    const float r = (0.5f + rnd() * rnd() * 1.4f) * scale;
    const int a = 50 + static_cast<int>(rnd() * 180);
    dl->AddCircleFilled(at, r, IM_COL32(220, 228, 255, a), 8);
  }
  for (int i = 0; i < 4; ++i) {  // a few brighter ones with a glow
    const ImVec2 at(p0.x + (0.35f + rnd() * 0.6f) * (p1.x - p0.x), p0.y + (0.15f + rnd() * 0.7f) * height);
    dl->AddCircleFilled(at, 4.0f * scale, IM_COL32(150, 180, 255, 40), 12);
    dl->AddCircleFilled(at, 1.6f * scale, IM_COL32(240, 245, 255, 235), 8);
  }

  // With a valid Disc 1: the game's own title logo over its nebula, the full height of the
  // header, fading into the starfield on the right (a mesh with per-vertex alpha).
  const bool logo = logo_ != 0 && disc1_ok();
  float text_left = p0.x + em() * 1.4f;
  if (logo) {
    const float h = height - 2.0f * scale;
    const float w = h * static_cast<float>(logo_width_) / static_cast<float>(logo_height_);
    constexpr int kColumns = 32;
    dl->PushTexture(ImTextureRef(static_cast<ImTextureID>(logo_)));
    dl->PrimReserve(kColumns * 6, (kColumns + 1) * 2);
    const unsigned base = dl->_VtxCurrentIdx;
    for (int i = 0; i <= kColumns; ++i) {
      const float u = static_cast<float>(i) / kColumns;
      // Opaque over the logo, fading out over the last 16% (its right edge) and the first 3%.
      const float right = std::clamp((1.0f - u) / 0.16f, 0.0f, 1.0f), left = std::clamp(u / 0.03f, 0.0f, 1.0f);
      const float a = std::min(left, right * right * (3 - 2 * right));
      const ImU32 col = IM_COL32(255, 255, 255, static_cast<int>(a * 255.0f + 0.5f));
      dl->PrimWriteVtx(ImVec2(p0.x + u * w, p0.y), ImVec2(u, 0), col);
      dl->PrimWriteVtx(ImVec2(p0.x + u * w, p0.y + h), ImVec2(u, 1), col);
    }
    for (int i = 0; i < kColumns; ++i) {
      const auto v = static_cast<ImDrawIdx>(base + i * 2);
      for (int k : {0, 1, 3, 0, 3, 2}) dl->PrimWriteIdx(static_cast<ImDrawIdx>(v + k));
    }
    dl->PopTexture();
    text_left = p0.x + w + em() * 0.6f;
  }
  dl->AddLine(ImVec2(p0.x, p1.y - 1), ImVec2(p1.x, p1.y - 1), ImGui::GetColorU32(kAccent), 2.0f * scale);

  ImGui::SetCursorScreenPos(ImVec2(text_left, p0.y + em() * 1.0f));
  ImGui::PushFont(fonts_.bold, fonts_.size * (logo ? 1.5f : 1.75f));
  ImGui::TextUnformatted("Shattered Stars");
  ImGui::PopFont();
  ImGui::SetCursorScreenPos(ImVec2(text_left, ImGui::GetCursorScreenPos().y - em() * 0.1f));
  ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(176, 190, 225, 255));
  ImGui::TextUnformatted(options_.from_game ? "Setup  -  the game is waiting for its discs"
                         : logo             ? "Setup"
                                            : "Setup for Star Ocean: The Second Story");
  ImGui::PopStyleColor();

  // Where the settings live, at the bottom right (small; for helping someone over the phone).
  const std::string where = "Settings: " + launcher::path_utf8(config_.path());
  ImGui::PushFont(nullptr, fonts_.size * 0.8f);
  const float max_w = logo ? std::max(em() * 6, p1.x - text_left - em() * 1.2f) : (p1.x - p0.x) * 0.45f;
  std::string shown = where;
  while (shown.size() > 12 && ImGui::CalcTextSize(shown.c_str()).x > max_w) shown = "..." + shown.substr(4);
  const ImVec2 size = ImGui::CalcTextSize(shown.c_str());
  ImGui::SetCursorScreenPos(ImVec2(p1.x - size.x - em() * 1.2f, p1.y - size.y - em() * 0.55f));
  ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(150, 160, 190, 200));
  ImGui::TextUnformatted(shown.c_str());
  ImGui::PopStyleColor();
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", where.c_str());
  ImGui::PopFont();
  ImGui::SetCursorScreenPos(ImVec2(p0.x, p1.y));
}

void App::sidebar() {
  const float row = em() * 2.2f;
  for (size_t i = 0; i < static_cast<size_t>(Page::kCount); ++i) {
    const Page page = static_cast<Page>(i);
    const bool selected = page_ == page;
    const bool placeholder = false;  // (the Archipelago page was one before it went live)
    ImGui::PushID(static_cast<int>(i));
    ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0, 0.5f));
    ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(40, 70, 130, 150));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(40, 60, 100, 140));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, IM_COL32(50, 80, 140, 180));
    if (placeholder) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    const std::string label = std::string("    ") + kPageNames[i];
    if (ImGui::Selectable(label.c_str(), selected, 0, ImVec2(0, row))) page_ = page;
    if (placeholder) ImGui::PopStyleColor();
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();
    const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (selected) dl->AddRectFilled(a, ImVec2(a.x + em() * 0.22f, b.y), ImGui::GetColorU32(kAccent), 2.0f);
    // A dot for what needs attention.
    ImU32 dot = 0;
    if (page == Page::kDiscs) dot = disc1_ok() ? (disc_level(2) == Level::kWarning ? kWarning : kOk) : kError;
    if (page == Page::kMemoryCards) {
      for (int s = 0; s < 2; ++s)
        if (settings_.card_inserted[s] && !settings_.card[s].empty()) {
          const auto k = card_info(settings_.card[s]).kind;
          if (k == CardInfo::kNotACard || k == CardInfo::kDexDrive) dot = kWarning;
        }
      if (settings_.card_inserted[0] && settings_.card_inserted[1] && same_file(settings_.card[0], settings_.card[1]))
        dot = kError;
    }
    if (page == Page::kArchipelago && !settings_.ap_seed.empty()) dot = seed_info(settings_.ap_seed).ok ? kOk : kError;
    if (dot) dl->AddCircleFilled(ImVec2(b.x - em() * 0.8f, (a.y + b.y) * 0.5f), em() * 0.26f, dot, 12);
    if (placeholder) {
      const char* tag = "soon";
      ImGui::PushFont(nullptr, fonts_.size * 0.72f);
      const ImVec2 ts = ImGui::CalcTextSize(tag);
      const ImVec2 tp(b.x - ts.x - em() * 0.6f, (a.y + b.y - ts.y) * 0.5f);
      dl->AddRectFilled(ImVec2(tp.x - 5, tp.y - 1), ImVec2(tp.x + ts.x + 5, tp.y + ts.y + 1), IM_COL32(60, 66, 84, 255), 4);
      dl->AddText(tp, IM_COL32(170, 176, 196, 255), tag);
      ImGui::PopFont();
    }
    ImGui::PopID();
  }
}

void App::bottom_bar() {
  const float height = ImGui::GetContentRegionAvail().y;
  ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(20, 25, 38, 255));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(em() * 1.4f, 0));
  ImGui::BeginChild("##bar", ImVec2(0, height), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
  const ImVec2 p0 = ImGui::GetWindowPos();
  ImGui::GetWindowDrawList()->AddLine(p0, ImVec2(p0.x + ImGui::GetWindowWidth(), p0.y), IM_COL32(44, 52, 72, 255), 1.0f);

  const bool clash = settings_.card_inserted[0] && settings_.card_inserted[1] && same_file(settings_.card[0], settings_.card[1]);
  const bool can_save = disc1_ok() && !clash;
  const bool dirty = settings_ != saved_;
  Level level;
  std::string text;
  if (!disc1_ok()) {
    level = Level::kError;
    text = "Choose your Disc 1 image to continue.";
  } else if (clash) {
    level = Level::kError;
    text = "Both memory card slots use the same file.";
  } else if (dirty) {
    level = Level::kWarning;
    text = "You have unsaved changes.";
  } else if (!saved_message_.empty()) {
    level = Level::kOk;
    text = "Settings saved. Ready to play.";
  } else {
    level = Level::kOk;
    text = "Ready to play.";
  }

  const char* play_label = "Save & Play";
  const float spacing = ImGui::GetStyle().ItemSpacing.x;
  const float buttons = button_width("Quit") + button_width("Save") + button_width(play_label) + em() * 1.6f + spacing * 2;
  const float frame_h = ImGui::GetFrameHeight();
  const float y = (height - frame_h) * 0.5f;

  ImGui::SetCursorPos(ImVec2(em() * 1.4f, y + ImGui::GetStyle().FramePadding.y));
  const float text_room = ImGui::GetContentRegionAvail().x - buttons - em();
  ImGui::BeginGroup();
  ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + text_room);
  status_line(level, text);
  ImGui::PopTextWrapPos();
  ImGui::EndGroup();

  ImGui::SetCursorPos(ImVec2(ImGui::GetWindowWidth() - em() * 1.4f - buttons, y));
  if (ImGui::Button("Quit")) request_close();
  ImGui::SameLine();
  ImGui::BeginDisabled(!can_save || !dirty);
  if (ImGui::Button("Save")) save();
  ImGui::EndDisabled();
  if (!can_save && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    ImGui::SetTooltip(clash ? "Choose a different file for one of the memory card slots first."
                            : "Choose a valid Disc 1 image first.");
  ImGui::SameLine();
  ImGui::BeginDisabled(!can_save);
  if (primary_button(play_label, ImVec2(button_width(play_label) + em() * 1.6f, 0))) play();
  ImGui::EndDisabled();
  if (!can_save && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    ImGui::SetTooltip(clash ? "Choose a different file for one of the memory card slots first."
                            : "Choose a valid Disc 1 image first.");
  ImGui::EndChild();
  ImGui::PopStyleVar();
  ImGui::PopStyleColor();
}

void App::popups() {
  if (open_quit_popup_) {
    ImGui::OpenPopup("Unsaved changes");
    open_quit_popup_ = false;
  }
  if (open_error_popup_) {
    ImGui::OpenPopup("Shattered Stars Setup");
    open_error_popup_ = false;
  }
  const ImVec2 centre = ImGui::GetMainViewport()->GetCenter();
  ImGui::SetNextWindowPos(centre, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(em() * 26, 0));
  if (ImGui::BeginPopupModal("Unsaved changes", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
    ImGui::TextWrapped("You changed some settings without saving them.");
    const bool can_save = disc1_ok();
    if (!can_save) note("They can't be saved yet: Disc 1 isn't a valid image.");
    ImGui::Dummy(ImVec2(0, em() * 0.6f));
    ImGui::BeginDisabled(!can_save);
    if (primary_button("Save and quit")) {
      ImGui::CloseCurrentPopup();
      if (save()) quit();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Quit without saving")) {
      ImGui::CloseCurrentPopup();
      quit();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
  ImGui::SetNextWindowPos(centre, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(em() * 28, 0));
  if (ImGui::BeginPopupModal("Shattered Stars Setup", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
    ImGui::TextWrapped("%s", error_message_.c_str());
    ImGui::Dummy(ImVec2(0, em() * 0.6f));
    if (primary_button("OK", ImVec2(em() * 6, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
}

// ---------------------------------------------------------------------------------------
// Pages

void App::disc_field(int n) {
  std::string& path = n == 1 ? settings_.disc1 : settings_.disc2;
  ImGui::PushID(n);
  subheading(fonts_.bold, fonts_.size, n == 1 ? "Disc 1" : "Disc 2");
  ImGui::SameLine();
  ImGui::AlignTextToFramePadding();
  note(n == 1 ? "(required)" : "(optional)");
  const char* browse_label = "Browse...";
  const float reserve = button_width(browse_label) + ImGui::GetStyle().ItemSpacing.x;
  if (path_input("##path", path, n == 1 ? "Choose your Disc 1 image (.cue, .bin, .iso or .img)"
                                        : "Choose your Disc 2 image (optional)",
                 reserve))
    notice_.clear();
  ImGui::SameLine();
  ImGui::BeginDisabled(dialog_open_);
  if (ImGui::Button(browse_label)) browse(n == 1 ? Target::kDisc1 : Target::kDisc2);
  ImGui::EndDisabled();
  status_line(disc_level(n), launcher::describe(disc_info(path), n));
  ImGui::PopID();
}

void App::page_discs() {
  heading("Discs");
  if (options_.from_game && !disc1_ok()) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(40, 52, 84, 255));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(em() * 0.8f, em() * 0.6f));
    ImGui::BeginChild("##waiting", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
    status_line(Level::kInfo, "The game needs your Disc 1 image before it can start. Choose it below, then press "
                              "Save & Play.");
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0, em() * 0.2f));
  }
  note("Shattered Stars plays Star Ocean: The Second Story from images of your own discs, North American "
       "release: cue sheets with their bin files, or iso / img images.");
  disc_field(1);
  disc_field(2);
  if (disc_info(settings_.disc2).kind == DiscKind::kNone)
    note("Without Disc 2 you can play until the story changes discs; the game then asks for it, as the console did.");

  ImGui::Dummy(ImVec2(0, em() * 0.4f));
  ImGui::Separator();
  ImGui::Dummy(ImVec2(0, em() * 0.2f));
  if (ImGui::Button("Auto-detect")) auto_detect(false);
  ImGui::SameLine();
  const auto folders = launcher::disc_folders(launcher::config_dir());
  ImGui::BeginDisabled(options_.screenshot);
  if (ImGui::Button("Open discs folder") && !folders.empty()) {
    // Next to the game if that folder can be made (not in a read-only install).
    fs::path target = folders.back();
    std::error_code ec;
    for (const fs::path& f : folders) {
      fs::create_directories(f, ec);
      if (fs::is_directory(f, ec)) {
        target = f;
        break;
      }
    }
    open_folder(target);
  }
  ImGui::EndDisabled();
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort)) {
    std::string where = "The game looks for disc images in:";
    for (const fs::path& f : folders) where += "\n" + launcher::path_utf8(f);
    ImGui::SetTooltip("%s", where.c_str());
  }
  if (!notice_.empty()) status_line(notice_level_, notice_);
  note("Tip: put your disc images in a folder named \"discs\" next to the game and it finds them by itself.");
}

void App::page_display() {
  heading("Display");
  launcher::DisplaySettings& d = settings_.display;
  const float label_w = em() * 9.0f;
  const std::vector<u32> displays = platform::display_order();
  const int monitors = static_cast<int>(displays.size());
  const int monitor = d.monitor >= 1 && d.monitor <= monitors ? d.monitor : 1;
  const SDL_DisplayID id = displays.empty() ? 0 : displays[static_cast<size_t>(monitor - 1)];
  const SDL_DisplayMode* desktop = id ? SDL_GetDesktopDisplayMode(id) : nullptr;

  subheading(fonts_.bold, fonts_.size, "Window mode");
  struct ModeChoice {
    launcher::WindowMode mode;
    const char* name;
    const char* about;
  };
  const ModeChoice modes[] = {
      {launcher::WindowMode::kWindowed, "Windowed", "A window you can move and resize."},
      {launcher::WindowMode::kBorderless, "Borderless full screen",
       "Covers the screen at the desktop's resolution; switching to other programs is instant."},
      {launcher::WindowMode::kFullscreen, "Full screen", "Takes over the monitor in the resolution chosen below."},
  };
  const char* about = "";
  for (const ModeChoice& m : modes) {
    if (&m != modes) ImGui::SameLine(0, em() * 1.6f);
    if (radio(m.name, d.mode == m.mode)) d.mode = m.mode;
    if (d.mode == m.mode) about = m.about;
  }
  note(about);
  ImGui::Dummy(ImVec2(0, em() * 0.2f));

  if (monitors > 1) {
    field_label("Monitor", label_w);
    ImGui::SetNextItemWidth(em() * 18);
    auto monitor_name = [&](int i) {
      const char* name = SDL_GetDisplayName(displays[static_cast<size_t>(i - 1)]);
      return std::to_string(i) + ": " + (name ? name : "Display") + (i == 1 ? " (main)" : "");
    };
    if (ImGui::BeginCombo("##monitor", monitor_name(monitor).c_str())) {
      for (int i = 1; i <= monitors; ++i)
        if (ImGui::Selectable(monitor_name(i).c_str(), i == monitor)) d.monitor = i;
      ImGui::EndCombo();
    }
  }

  if (d.mode == launcher::WindowMode::kWindowed) {
    field_label("Window size", label_w);
    int max_scale = launcher::kMaxScale;
    if (SDL_Rect usable; id && SDL_GetDisplayUsableBounds(id, &usable) && usable.w > 0)
      max_scale = std::clamp(std::min(usable.w * 95 / 100 / 320, usable.h * 90 / 100 / 240), 1, launcher::kMaxScale);
    auto scale_name = [&](int s) {
      std::string text = std::to_string(s) + "x   " + std::to_string(320 * s) + " x " + std::to_string(240 * s);
      if (s == 3) text += s > max_scale ? "  (default, shrunk to fit)" : "  (default)";
      else if (s > max_scale) text += "  (shrunk to fit)";
      return text;
    };
    ImGui::SetNextItemWidth(em() * 20);
    if (ImGui::BeginCombo("##scale", scale_name(d.scale).c_str())) {
      for (int s = 1; s <= std::max(max_scale, d.scale); ++s)
        if (ImGui::Selectable(scale_name(s).c_str(), s == d.scale)) d.scale = s;
      ImGui::EndCombo();
    }
  } else if (d.mode == launcher::WindowMode::kFullscreen) {
    field_label("Resolution", label_w);
    struct Mode {
      int w, h;
      float hz;
    };
    std::vector<Mode> list;
    int count = 0;
    if (SDL_DisplayMode** modes_list = id ? SDL_GetFullscreenDisplayModes(id, &count) : nullptr) {
      for (int i = 0; i < count; ++i) {
        const SDL_DisplayMode* m = modes_list[i];
        const bool dup = std::any_of(list.begin(), list.end(), [&](const Mode& o) {
          return o.w == m->w && o.h == m->h && std::fabs(o.hz - m->refresh_rate) < 0.05f;
        });
        if (!dup) list.push_back({m->w, m->h, m->refresh_rate});
      }
      SDL_free(modes_list);
    }
    auto mode_name = [](int w, int h, float hz) {
      char buf[64];
      if (hz > 0) std::snprintf(buf, sizeof buf, "%d x %d  @ %.4g Hz", w, h, static_cast<double>(hz));
      else std::snprintf(buf, sizeof buf, "%d x %d", w, h);
      return std::string(buf);
    };
    const std::string desktop_name =
        desktop ? "Desktop  (" + mode_name(desktop->w, desktop->h, desktop->refresh_rate) + ")" : std::string("Desktop");
    const bool custom = d.width > 0 && d.height > 0;
    std::string preview = custom ? mode_name(d.width, d.height, d.refresh) : desktop_name;
    if (custom && !std::any_of(list.begin(), list.end(), [&](const Mode& m) {
          return m.w == d.width && m.h == d.height && (d.refresh <= 0 || std::fabs(m.hz - d.refresh) < 0.05f);
        }))
      preview += "  (closest mode is used)";
    ImGui::SetNextItemWidth(em() * 20);
    if (ImGui::BeginCombo("##resolution", preview.c_str())) {
      if (ImGui::Selectable(desktop_name.c_str(), !custom)) d.width = d.height = 0, d.refresh = 0;
      for (const Mode& m : list) {
        const bool sel = custom && m.w == d.width && m.h == d.height && std::fabs(m.hz - d.refresh) < 0.05f;
        if (ImGui::Selectable(mode_name(m.w, m.h, m.hz).c_str(), sel)) {
          d.width = m.w;
          d.height = m.h;
          d.refresh = m.hz;
        }
      }
      ImGui::EndCombo();
    }
  } else {
    field_label("Resolution", label_w);
    ImGui::TextUnformatted(desktop ? ("The desktop's: " + std::to_string(desktop->w) + " x " + std::to_string(desktop->h)).c_str()
                                   : "The desktop's");
  }

  subheading(fonts_.bold, fonts_.size, "Picture");
  check("Vertical sync", &d.vsync);
  ImGui::Indent(check_indent());
  note("Shows each frame in step with the monitor's refresh, so the picture never tears.");
  ImGui::Unindent(check_indent());
  check("Dithering", &d.dither);
  ImGui::Indent(check_indent());
  note("The PlayStation's colour dithering, as on a real console. Off gives smooth, banded gradients.");
  ImGui::Unindent(check_indent());
  ImGui::Dummy(ImVec2(0, em() * 0.3f));
  status_line(Level::kInfo, "While playing, F11 or Alt+Enter switches between the window and full screen.");
}

void App::page_sound() {
  heading("Sound");
  launcher::SoundSettings& s = settings_.sound;
  check("Sound on", &s.enabled);
  ImGui::Dummy(ImVec2(0, em() * 0.3f));
  ImGui::BeginDisabled(!s.enabled);
  field_label("Volume", em() * 6);
  ImGui::SetNextItemWidth(std::min(em() * 20, ImGui::GetContentRegionAvail().x));
  ImGui::SliderInt("##volume", &s.volume, 0, 100, "%d%%", ImGuiSliderFlags_AlwaysClamp);
  ImGui::EndDisabled();
  ImGui::Dummy(ImVec2(0, em() * 0.3f));
  note("Music, voices and sound effects all follow this volume.");
}

// ---------------------------------------------------------------------------------------
// Controls

namespace {

// Rows of the two halves of the table, as PadButton bits.
constexpr platform::PadButton kLeftHalf[] = {platform::kPadUp,    platform::kPadDown,   platform::kPadLeft,
                                             platform::kPadRight, platform::kPadStart,  platform::kPadSelect,
                                             platform::kPadL3,    platform::kPadR3};
constexpr platform::PadButton kRightHalf[] = {platform::kPadCross, platform::kPadCircle, platform::kPadSquare,
                                              platform::kPadTriangle, platform::kPadL1, platform::kPadR1,
                                              platform::kPadL2, platform::kPadR2};

int bit_of(platform::PadButton b) { return std::countr_zero(static_cast<unsigned>(b)); }

const char* ps1_name(int bit) {
  static const char* const names[16] = {"L2",       "R2",        "L1",        "R1",       "Triangle", "Circle",
                                        "Cross",    "Square",    "Select",    "L3",       "R3",       "Start",
                                        "D-pad Up", "D-pad Right", "D-pad Down", "D-pad Left"};
  return names[bit];
}

std::string key_label(int keycode) {
  if (keycode == 0) return "-";
  std::string name = launcher::key_name(keycode);
  if (name == "Return") name = "Enter";
  return name;
}

std::string button_label(int button) {
  switch (button) {
    case platform::kNoButton: return "-";
    case platform::kLeftTrigger: return "LT / L2";
    case platform::kRightTrigger: return "RT / R2";
    case SDL_GAMEPAD_BUTTON_SOUTH: return "A / Cross";
    case SDL_GAMEPAD_BUTTON_EAST: return "B / Circle";
    case SDL_GAMEPAD_BUTTON_WEST: return "X / Square";
    case SDL_GAMEPAD_BUTTON_NORTH: return "Y / Triangle";
    case SDL_GAMEPAD_BUTTON_BACK: return "Back";
    case SDL_GAMEPAD_BUTTON_START: return "Start";
    case SDL_GAMEPAD_BUTTON_GUIDE: return "Guide";
    case SDL_GAMEPAD_BUTTON_LEFT_STICK: return "L-stick click";
    case SDL_GAMEPAD_BUTTON_RIGHT_STICK: return "R-stick click";
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return "LB / L1";
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return "RB / R1";
    case SDL_GAMEPAD_BUTTON_DPAD_UP: return "D-pad Up";
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return "D-pad Down";
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return "D-pad Left";
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return "D-pad Right";
    case SDL_GAMEPAD_BUTTON_TOUCHPAD: return "Touchpad";
    default: return launcher::gamepad_button_name(button);
  }
}

}  // namespace

bool App::capture_event(const SDL_Event& e) {
  if (capture_bit_ < 0) return false;
  platform::Controls& c = settings_.controls.controls;
  auto done = [&] { capture_bit_ = -1; };
  switch (e.type) {
    case SDL_EVENT_KEY_DOWN:
      if (e.key.repeat) return true;
      if (e.key.key == SDLK_ESCAPE) {
        done();
      } else if (e.key.key == SDLK_DELETE) {
        if (capture_gamepad_) c.buttons[capture_bit_] = platform::kNoButton;
        else c.keys[capture_bit_] = 0;
        done();
      } else if (!capture_gamepad_) {
        if (e.key.key == SDLK_F11) {
          controls_notice_ = "F11 switches between the window and full screen while playing: choose another key.";
        } else {
          c.keys[capture_bit_] = static_cast<int>(e.key.key);
          controls_notice_.clear();
          done();
        }
      }
      return true;
    case SDL_EVENT_KEY_UP:
    case SDL_EVENT_TEXT_INPUT:
      return true;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
      if (capture_gamepad_) {
        c.buttons[capture_bit_] = e.gbutton.button;
        done();
      }
      return true;
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
      return true;
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
      if (capture_gamepad_ && e.gaxis.value > 20000 &&
          (e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || e.gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)) {
        c.buttons[capture_bit_] =
            e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER ? platform::kLeftTrigger : platform::kRightTrigger;
        done();
      }
      return true;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
      done();  // a click elsewhere cancels (and still clicks)
      return false;
    default:
      return false;
  }
}

void App::control_cell(int bit, bool gamepad, float width, bool duplicate) {
  const platform::Controls& c = settings_.controls.controls;
  const bool capturing = capture_bit_ == bit && capture_gamepad_ == gamepad;
  ImGui::PushID(bit * 2 + (gamepad ? 1 : 0));
  std::string label = capturing ? "Press now"
                                : (gamepad ? button_label(c.buttons[bit]) : key_label(c.keys[bit]));
  int colours = 0;
  if (capturing) {
    ImGui::PushStyleColor(ImGuiCol_Button, kAccent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kAccentHover);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
    colours = 3;
  } else if (duplicate) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(IM_COL32(240, 200, 120, 255)));
    colours = 1;
  }
  if (ImGui::Button(label.c_str(), ImVec2(width, 0)) && !capturing) {
    capture_bit_ = bit;
    capture_gamepad_ = gamepad;
    controls_notice_.clear();
  }
  ImGui::PopStyleColor(colours);
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
    ImGui::SetTooltip("%s", capturing ? "Esc cancels, Delete leaves it unbound"
                                      : "Click, then press the key or button for this PlayStation button");
  ImGui::PopID();
}

void App::page_controls() {
  launcher::ControlSettings& cs = settings_.controls;
  const float heading_y = ImGui::GetCursorPosY();
  const float right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
  heading("Controls");
  const float after_heading = ImGui::GetCursorPosY();
  const char* reset = "Reset to defaults";
  ImGui::SetCursorPos(ImVec2(right - button_width(reset), heading_y));
  if (ImGui::Button(reset)) {
    cs = launcher::ControlSettings{};
    capture_bit_ = -1;
    controls_notice_.clear();
  }
  ImGui::SetCursorPosY(after_heading);
  note("Click a box, then press the key or gamepad button for it (Esc cancels, Delete unbinds).");
  const platform::Controls& c = cs.controls;

  // Keys or buttons that press more than one PlayStation button.
  std::vector<std::string> clashes;
  bool key_clash[16] = {}, button_clash[16] = {};
  for (int i = 0; i < 16; ++i)
    for (int j = i + 1; j < 16; ++j) {
      if (c.keys[i] != 0 && c.keys[i] == c.keys[j]) {
        key_clash[i] = key_clash[j] = true;
        clashes.push_back("The " + key_label(c.keys[i]) + " key presses both " + ps1_name(i) + " and " + ps1_name(j) + ".");
      }
      if (c.buttons[i] != platform::kNoButton && c.buttons[i] == c.buttons[j]) {
        button_clash[i] = button_clash[j] = true;
        clashes.push_back("The gamepad's " + button_label(c.buttons[i]) + " presses both " + ps1_name(i) + " and " +
                          ps1_name(j) + ".");
      }
    }

  // Columns as wide as their widest label (fonts differ: Segoe UI, Noto, the fallback).
  const float pad_x = em() * 0.6f;
  float name_w = ImGui::CalcTextSize("PlayStation").x, key_w = em() * 4.5f, pad_w = em() * 5.5f;
  for (int i = 0; i < 16; ++i) {
    name_w = std::max(name_w, ImGui::CalcTextSize(ps1_name(i)).x);
    key_w = std::max(key_w, ImGui::CalcTextSize(key_label(c.keys[i]).c_str()).x + pad_x);
    pad_w = std::max(pad_w, ImGui::CalcTextSize(button_label(c.buttons[i]).c_str()).x + pad_x);
  }
  // Too wide for the page (a wide font): the boxes give way, their labels clip.
  const float gap = em() * 0.8f, cell = em() * 0.2f * 2 * 7;
  const float room = ImGui::GetContentRegionAvail().x - 2 * name_w - gap - cell;
  if (room > 0 && 2 * (key_w + pad_w) > room) {
    const float k = room / (2 * (key_w + pad_w));
    key_w *= k;
    pad_w *= k;
  }
  ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(em() * 0.2f, em() * 0.1f));
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(em() * 0.3f, em() * 0.2f));
  if (ImGui::BeginTable("##controls", 7, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoPadOuterX)) {
    ImGui::TableSetupColumn("PlayStation", ImGuiTableColumnFlags_WidthFixed, name_w);
    ImGui::TableSetupColumn("Keyboard", ImGuiTableColumnFlags_WidthFixed, key_w);
    ImGui::TableSetupColumn("Gamepad", ImGuiTableColumnFlags_WidthFixed, pad_w);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, gap);
    ImGui::TableSetupColumn("PlayStation", ImGuiTableColumnFlags_WidthFixed, name_w);
    ImGui::TableSetupColumn("Keyboard", ImGuiTableColumnFlags_WidthFixed, key_w);
    ImGui::TableSetupColumn("Gamepad", ImGuiTableColumnFlags_WidthFixed, pad_w);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
    for (int col = 0; col < 7; ++col) {
      ImGui::TableSetColumnIndex(col);
      ImGui::TextUnformatted(ImGui::TableGetColumnName(col));
    }
    ImGui::PopStyleColor();
    for (size_t row = 0; row < std::size(kLeftHalf); ++row) {
      ImGui::TableNextRow();
      for (int half = 0; half < 2; ++half) {
        const int bit = bit_of(half == 0 ? kLeftHalf[row] : kRightHalf[row]);
        ImGui::TableSetColumnIndex(half * 4);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(ps1_name(bit));
        ImGui::TableSetColumnIndex(half * 4 + 1);
        control_cell(bit, false, key_w, key_clash[bit]);
        ImGui::TableSetColumnIndex(half * 4 + 2);
        control_cell(bit, true, pad_w, button_clash[bit]);
      }
    }
    ImGui::EndTable();
  }
  ImGui::PopStyleVar(2);
  for (const std::string& clash : clashes) status_line(Level::kWarning, clash);
  if (!controls_notice_.empty()) status_line(Level::kInfo, controls_notice_);

  subheading(fonts_.bold, fonts_.size, "Analog stick");
  check("The left stick also works the d-pad (menus, as on the console)", &cs.stick_dpad);
  field_label("Dead zone", em() * 6);
  int dead = cs.stick_deadzone < 0 ? launcher::kGameStickDeadzone : cs.stick_deadzone;
  ImGui::SetNextItemWidth(em() * 14);
  if (ImGui::SliderInt("##deadzone", &dead, 0, 60, "%d%%", ImGuiSliderFlags_AlwaysClamp)) cs.stick_deadzone = dead;
  ImGui::SameLine();
  ImGui::BeginDisabled(cs.stick_deadzone < 0);
  if (ImGui::Button("Game's")) cs.stick_deadzone = -1;
  ImGui::EndDisabled();
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("The game's own: 25%%");
  note("How far the stick moves before the game notices: raise it if your character walks by itself.");
}

void App::card_field(int slot) {
  const int i = slot - 1;
  ImGui::PushID(slot);
  subheading(fonts_.bold, fonts_.size, slot == 1 ? "Slot 1" : "Slot 2");
  bool inserted = settings_.card_inserted[i];
  if (check("Card inserted", &inserted)) {
    settings_.card_inserted[i] = inserted;
    if (inserted && settings_.card[i].empty()) settings_.card[i] = launcher::path_utf8(launcher::default_memcard(slot));
  }
  if (!settings_.card_inserted[i]) {
    status_line(Level::kInfo, "Empty slot.");
    ImGui::PopID();
    return;
  }
  const char* browse_label = "Browse...";
  const char* default_label = "Default";
  const float reserve = button_width(browse_label) + button_width(default_label) + ImGui::GetStyle().ItemSpacing.x * 2;
  path_input("##card", settings_.card[i], "A memory card image (.mcd)", reserve);
  ImGui::SameLine();
  ImGui::BeginDisabled(dialog_open_);
  if (ImGui::Button(browse_label)) browse(slot == 1 ? Target::kCard1 : Target::kCard2);
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button(default_label)) settings_.card[i] = launcher::path_utf8(launcher::default_memcard(slot));
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
    ImGui::SetTooltip("%s", launcher::path_utf8(launcher::default_memcard(slot)).c_str());

  if (slot == 2 && settings_.card_inserted[0] && same_file(settings_.card[0], settings_.card[1])) {
    status_line(Level::kError, "Slot 1 uses this file already. Each slot needs its own card file.");
    ImGui::PopID();
    return;
  }
  const std::string& path = settings_.card[i];
  if (path.empty()) {
    status_line(Level::kWarning, "No file chosen: the slot will be empty.");
    ImGui::PopID();
    return;
  }
  const CardInfo& info = card_info(path);
  switch (info.kind) {
    case CardInfo::kNew:
      status_line(Level::kInfo, "No card file here yet: the game creates a new, formatted card when it starts.");
      break;
    case CardInfo::kNotACard:
      status_line(Level::kError, "Not a memory card image (the game uses raw 128 KB cards: .mcd / .mcr).");
      break;
    case CardInfo::kUnformatted:
      status_line(Level::kWarning, "Unformatted card: the game offers to format it.");
      break;
    case CardInfo::kDexDrive: {
      status_line(Level::kWarning, "A DexDrive save (.gme), with " + plural(info.saves, "Star Ocean save", "Star Ocean saves") +
                                       ". The game needs a raw card: make a .mcd copy to use it.");
      if (ImGui::Button("Make a .mcd copy")) {
        // The card is the file's last 128 KB (after the DexDrive header); never overwrite.
        const fs::path src = launcher::utf8_path(path);
        std::ifstream in(src, std::ios::binary);
        std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        fs::path dst;
        std::error_code ec;
        for (int n = 1; n < 100; ++n) {
          const std::string stem = launcher::path_utf8(src.stem()) + (n == 1 ? "" : "-" + std::to_string(n));
          dst = launcher::config_dir() / launcher::utf8_path(stem + ".mcd");
          if (!fs::exists(dst, ec)) break;
        }
        fs::create_directories(dst.parent_path(), ec);
        std::ofstream out(dst, std::ios::binary | std::ios::trunc);
        if (bytes.size() >= psx::MemoryCard::kSize && out) {
          out.write(bytes.data() + (bytes.size() - psx::MemoryCard::kSize), static_cast<std::streamsize>(psx::MemoryCard::kSize));
          settings_.card[i] = launcher::path_utf8(dst);
        } else {
          error_message_ = "The copy could not be written:\n\n" + launcher::path_utf8(dst);
          open_error_popup_ = true;
        }
      }
      break;
    }
    case CardInfo::kReady:
      status_line(Level::kOk, (info.saves ? plural(info.saves, "Star Ocean save", "Star Ocean saves")
                                          : std::string("No Star Ocean saves yet")) +
                                  ", " + plural(info.free_blocks, "free block", "free blocks") + ".");
      break;
  }
  ImGui::PopID();
}

void App::page_cards() {
  heading("Memory cards");
  note("The game saves on memory card images, like a PlayStation's cards: one save takes one of a card's 15 "
       "blocks. The cards are files you can copy and back up. Emulators' raw cards (.mcd / .mcr) work too.");
  card_field(1);
  card_field(2);
}

void App::page_mods() {
  heading("Mods");
  note("World edits: patch files (*.patch) that change map scripts, items, shops and enemies as the game loads "
       "them. Your disc images are never modified. tools/so2_mod.py writes them (see the README).");
  subheading(fonts_.bold, fonts_.size, "Mods folder");
  const char* labels[] = {"Browse...", "Default", "Open folder"};
  float reserve = 0;
  for (const char* l : labels) reserve += button_width(l) + ImGui::GetStyle().ItemSpacing.x;
  path_input("##mods", settings_.mods, "The folder with your *.patch files", reserve);
  ImGui::SameLine();
  ImGui::BeginDisabled(dialog_open_);
  if (ImGui::Button(labels[0])) browse(Target::kMods);
  ImGui::EndDisabled();
  ImGui::SameLine();
  const std::string default_mods = launcher::path_utf8(launcher::config_dir() / "mods");
  if (ImGui::Button(labels[1])) settings_.mods = default_mods;
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("%s", default_mods.c_str());
  ImGui::SameLine();
  ImGui::BeginDisabled(options_.screenshot || settings_.mods.empty());
  if (ImGui::Button(labels[2])) open_folder(launcher::utf8_path(settings_.mods));
  ImGui::EndDisabled();

  const fs::path folder = launcher::utf8_path(settings_.mods.empty() ? default_mods : settings_.mods);
  std::error_code ec;
  if (!fs::is_directory(folder, ec)) {
    status_line(Level::kInfo, "The folder doesn't exist yet (Open folder makes it). No mods are used.");
    return;
  }
  std::vector<std::string> patches;
  for (const auto& e : fs::directory_iterator(folder, ec))
    if (e.is_regular_file(ec) && lower(launcher::path_utf8(e.path().extension())) == ".patch")
      patches.push_back(launcher::path_utf8(e.path().filename()));
  std::sort(patches.begin(), patches.end());
  if (patches.empty()) {
    status_line(Level::kInfo, "No patch files in this folder: the game plays unmodified.");
    return;
  }
  status_line(Level::kOk, plural(static_cast<int>(patches.size()), "patch file", "patch files") +
                              ", applied in this order:");
  ImGui::Indent(em() * 1.5f);
  for (const std::string& p : patches) ImGui::BulletText("%s", p.c_str());
  ImGui::Unindent(em() * 1.5f);
}

void App::page_archipelago() {
  heading("Archipelago");
  note("Archipelago is a multiworld randomizer: items are shuffled across games, and players find each "
       "other's. Choose the seed file the generator made for you; with a server, the game sends your "
       "checks and receives your items as you play.");

  subheading(fonts_.bold, fonts_.size, "Seed");
  const char* browse_label = "Browse...";
  const char* clear_label = "Clear";
  const float reserve = button_width(browse_label) + button_width(clear_label) + ImGui::GetStyle().ItemSpacing.x * 2;
  path_input("##seed", settings_.ap_seed, "A seed file from the Archipelago generator (.apso2)", reserve);
  ImGui::SameLine();
  ImGui::BeginDisabled(dialog_open_);
  if (ImGui::Button(browse_label)) browse(Target::kSeed);
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(settings_.ap_seed.empty());
  if (ImGui::Button(clear_label)) settings_.ap_seed.clear();
  ImGui::EndDisabled();
  if (settings_.ap_seed.empty()) {
    status_line(Level::kInfo, "No seed: the game plays as the original.");
  } else {
    const SeedInfo& seed = seed_info(settings_.ap_seed);
    if (seed.ok)
      status_line(Level::kOk, "Seed " + seed.seed + ", slot " + seed.slot + ": " +
                                  plural(static_cast<int>(seed.locations), "location", "locations") + ".");
    else
      status_line(Level::kError, seed.error);
  }

  subheading(fonts_.bold, fonts_.size, "Server");
  const float label_w = em() * 7;
  const float width = std::min(em() * 22, ImGui::GetContentRegionAvail().x - label_w);
  ImGui::BeginDisabled(settings_.ap_seed.empty());
  field_label("Address", label_w);
  ImGui::SetNextItemWidth(width);
  std::string server = settings_.ap_server;
  if (ImGui::InputTextWithHint("##server", "archipelago.gg:38281 (empty: play offline)", &server))
    settings_.ap_server = clean_path(server);
  field_label("Slot name", label_w);
  ImGui::SetNextItemWidth(width);
  const SeedInfo* seed = settings_.ap_seed.empty() ? nullptr : &seed_info(settings_.ap_seed);
  const std::string slot_hint = seed && seed->ok ? "From the seed: " + seed->slot : std::string("Your slot name");
  ImGui::InputTextWithHint("##slot", slot_hint.c_str(), &settings_.ap_slot);
  field_label("Password", label_w);
  ImGui::SetNextItemWidth(width);
  ImGui::InputTextWithHint("##password", "If the room has one", &settings_.ap_password,
                           show_password_ ? 0 : ImGuiInputTextFlags_Password);
  ImGui::SameLine();
  check("Show", &show_password_);
  ImGui::EndDisabled();
  ImGui::Dummy(ImVec2(0, em() * 0.2f));
  if (!settings_.ap_seed.empty())
    note(settings_.ap_server.empty()
             ? "Offline: your checks are kept in your save and sent when you play with a server."
             : "The game connects when it starts. Checks found while it can't reach the server are sent later.");
}

}  // namespace ss::setup
