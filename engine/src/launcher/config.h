#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

// config.ini, shared by the game and the setup program (port feature).
//
// The file is plain "key=value" lines; '#' or ';' starts a comment. Reading keeps every
// line, so writing it back changes only the keys that were set: unknown keys (a newer
// version's, or the player's own notes) and comments survive. Paths are stored as UTF-8.
//
// Keys in use (launcher/settings.h has the defaults):
//   disc1, disc2          disc images (.cue / .bin / .iso / .img)
//   memcard1, memcard2    raw 128 KB card images, "none" for an empty slot
//   mods                  the world-edit folder (default: mods/ next to config.ini)
//   mod_order             the Mods page's list in load order: "(loose),id,-id" ('-' =
//                         disabled; mods/catalog.h). Written by setup only, never the game
//   window                windowed | borderless | fullscreen
//   window_scale          windowed size, 320 x 240 times this
//   resolution            exclusive fullscreen mode, WxH[@Hz], or "desktop"
//   monitor               1 = the primary display, 2 = the next, ...
//   vsync, dither, audio  0 / 1
//   volume                0-100
//   instant_text          0 / 1: message pages show at once (port; default 0)
//   auto_advance          0 / 1: messages waiting for confirm move on by themselves, never
//                         answering a choice (port; default 0)
//   auto_advance_delay    its wait in milliseconds, 100-10000 (default 1000)
namespace ss::launcher {

// Where the settings, the memory cards and mods/ live:
//   $XDG_CONFIG_HOME/shattered-stars, else %APPDATA%\ShatteredStars (Windows), else
//   ~/.config/shattered-stars; the current folder as a last resort.
std::filesystem::path config_dir();
std::filesystem::path config_path();  // config_dir() / "config.ini"

// UTF-8 strings <-> paths, the same on every platform (Windows paths are UTF-16 inside).
std::filesystem::path utf8_path(const std::string& s);
std::string path_utf8(const std::filesystem::path& p);

class Config {
 public:
  // Reads 'path' (a missing file reads as empty).
  static Config load(const std::filesystem::path& path = config_path());

  // Writes the file back (through a temporary file, so a crash never leaves half a
  // file), creating its folder. Keys keep their place; new keys go at the end.
  bool save(std::string* error = nullptr) const;

  bool has(const std::string& key) const;
  std::string get(const std::string& key, const std::string& fallback = {}) const;
  int get_int(const std::string& key, int fallback) const;
  bool get_bool(const std::string& key, bool fallback) const;
  void set(const std::string& key, const std::string& value);
  void set_int(const std::string& key, int value) { set(key, std::to_string(value)); }
  void set_bool(const std::string& key, bool value) { set(key, value ? "1" : "0"); }
  void erase(const std::string& key);

  // Every key's value (the last one wins if a key repeats).
  std::map<std::string, std::string> values() const;
  const std::filesystem::path& path() const { return path_; }
  void set_path(const std::filesystem::path& path) { path_ = path; }

  // Same keys and values (comments and order ignored).
  bool same_values(const Config& other) const { return values() == other.values(); }

 private:
  struct Line {
    std::string key;  // empty: a comment, a blank or an unparsed line, kept as 'raw'
    std::string value;
    std::string raw;
  };
  std::vector<Line> lines_;
  std::filesystem::path path_;
};

}  // namespace ss::launcher
