#include "launcher/config.h"

#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <string>
#include <system_error>

#ifdef _WIN32
#include <cwchar>
#endif

namespace ss::launcher {
namespace {

namespace fs = std::filesystem;

std::string trim(const std::string& s) {
  const auto first = s.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) return {};
  const auto last = s.find_last_not_of(" \t\r\n");
  return s.substr(first, last - first + 1);
}

// An environment variable as a path; on Windows read as UTF-16 so any user name works.
fs::path env_path(const char* name) {
#ifdef _WIN32
  std::wstring wide(name, name + std::char_traits<char>::length(name));
  if (const wchar_t* v = _wgetenv(wide.c_str()); v && *v) return fs::path(v);
  return {};
#else
  if (const char* v = std::getenv(name); v && *v) return fs::path(v);
  return {};
#endif
}

}  // namespace

fs::path utf8_path(const std::string& s) {
  return fs::path(std::u8string(reinterpret_cast<const char8_t*>(s.data()), s.size()));
}

std::string path_utf8(const fs::path& p) {
  const std::u8string u = p.u8string();
  return std::string(reinterpret_cast<const char*>(u.data()), u.size());
}

fs::path config_dir() {
  if (fs::path xdg = env_path("XDG_CONFIG_HOME"); !xdg.empty()) return xdg / "shattered-stars";
  if (fs::path appdata = env_path("APPDATA"); !appdata.empty()) return appdata / "ShatteredStars";
  if (fs::path home = env_path("HOME"); !home.empty()) return home / ".config" / "shattered-stars";
  return fs::path(".");
}

fs::path config_path() {
  const fs::path dir = config_dir();
  // No settings folder at all (no HOME): the historical name in the current folder.
  return dir == fs::path(".") ? fs::path("shattered-stars.ini") : dir / "config.ini";
}

Config Config::load(const fs::path& path) {
  Config c;
  c.path_ = path;
  std::ifstream in(path, std::ios::binary);
  std::string line;
  bool first = true;
  while (std::getline(in, line)) {
    if (first && line.size() >= 3 && line.compare(0, 3, "\xEF\xBB\xBF") == 0) line.erase(0, 3);  // Notepad's BOM
    first = false;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    Line l;
    l.raw = line;
    const std::string t = trim(line);
    const auto eq = t.find('=');
    if (!t.empty() && t[0] != '#' && t[0] != ';' && t[0] != '[' && eq != std::string::npos && eq > 0) {
      l.key = trim(t.substr(0, eq));
      l.value = trim(t.substr(eq + 1));
    }
    c.lines_.push_back(std::move(l));
  }
  return c;
}

bool Config::save(std::string* error) const {
  std::error_code ec;
  if (path_.has_parent_path()) fs::create_directories(path_.parent_path(), ec);
  fs::path tmp = path_;
  tmp += ".new";
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) {
      if (error) *error = "cannot write " + path_utf8(tmp);
      return false;
    }
    if (lines_.empty()) out << "# Shattered Stars settings (the game and setup both write this file)\n";
    for (const Line& l : lines_) out << (l.key.empty() ? l.raw : l.key + "=" + l.value) << '\n';
    out.flush();
    if (!out) {
      if (error) *error = "cannot write " + path_utf8(tmp);
      return false;
    }
  }
  fs::rename(tmp, path_, ec);
  if (ec) {
    // Some file systems refuse to replace: remove, then move.
    std::error_code ec2;
    fs::remove(path_, ec2);
    fs::rename(tmp, path_, ec);
    if (ec) {
      if (error) *error = "cannot replace " + path_utf8(path_) + ": " + ec.message();
      return false;
    }
  }
  return true;
}

bool Config::has(const std::string& key) const {
  for (const Line& l : lines_)
    if (l.key == key) return true;
  return false;
}

std::string Config::get(const std::string& key, const std::string& fallback) const {
  for (auto it = lines_.rbegin(); it != lines_.rend(); ++it)
    if (it->key == key) return it->value;
  return fallback;
}

int Config::get_int(const std::string& key, int fallback) const {
  const std::string v = get(key);
  if (v.empty()) return fallback;
  char* end = nullptr;
  const long n = std::strtol(v.c_str(), &end, 10);
  return end != v.c_str() ? static_cast<int>(n) : fallback;
}

bool Config::get_bool(const std::string& key, bool fallback) const {
  const std::string v = get(key);
  if (v.empty()) return fallback;
  if (v == "0" || v == "false" || v == "no" || v == "off") return false;
  if (v == "1" || v == "true" || v == "yes" || v == "on") return true;
  return fallback;
}

void Config::set(const std::string& key, const std::string& value) {
  size_t last = lines_.size();
  for (size_t i = 0; i < lines_.size(); ++i)
    if (lines_[i].key == key) last = i;
  if (last != lines_.size()) {
    lines_[last].value = value;
    // Earlier duplicates of the key would no longer mean anything: drop them.
    for (size_t i = last; i-- > 0;)
      if (lines_[i].key == key) lines_.erase(lines_.begin() + static_cast<std::ptrdiff_t>(i));
    return;
  }
  if (lines_.empty()) lines_.push_back({{}, {}, "# Shattered Stars settings (the game and setup both write this file)"});
  lines_.push_back({key, value, {}});
}

void Config::erase(const std::string& key) {
  std::erase_if(lines_, [&](const Line& l) { return l.key == key; });
}

std::map<std::string, std::string> Config::values() const {
  std::map<std::string, std::string> out;
  for (const Line& l : lines_)
    if (!l.key.empty()) out[l.key] = l.value;
  return out;
}

}  // namespace ss::launcher
