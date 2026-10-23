#include "launcher/settings.h"

#include <algorithm>
#include <cstdio>

namespace ss::launcher {

const char* window_mode_name(WindowMode m) {
  switch (m) {
    case WindowMode::kBorderless: return "borderless";
    case WindowMode::kFullscreen: return "fullscreen";
    default: return "windowed";
  }
}

DisplaySettings read_display(const Config& c) {
  DisplaySettings d;
  const std::string mode = c.get("window", "windowed");
  if (mode == "borderless") d.mode = WindowMode::kBorderless;
  else if (mode == "fullscreen") d.mode = WindowMode::kFullscreen;
  d.scale = std::clamp(c.get_int("window_scale", d.scale), kMinScale, kMaxScale);
  const std::string res = c.get("resolution", "desktop");
  int w = 0, h = 0;
  float hz = 0;
  const int n = std::sscanf(res.c_str(), "%dx%d@%f", &w, &h, &hz);
  if (n >= 2 && w > 0 && h > 0) {
    d.width = w;
    d.height = h;
    d.refresh = n == 3 && hz > 0 ? hz : 0;
  }
  d.monitor = std::max(1, c.get_int("monitor", 1));
  d.vsync = c.get_bool("vsync", true);
  d.dither = c.get_bool("dither", true);
  return d;
}

void write_display(Config& c, const DisplaySettings& d) {
  c.set("window", window_mode_name(d.mode));
  c.set_int("window_scale", d.scale);
  if (d.width > 0 && d.height > 0) {
    char buf[64];
    if (d.refresh > 0) std::snprintf(buf, sizeof buf, "%dx%d@%g", d.width, d.height, static_cast<double>(d.refresh));
    else std::snprintf(buf, sizeof buf, "%dx%d", d.width, d.height);
    c.set("resolution", buf);
  } else {
    c.set("resolution", "desktop");
  }
  c.set_int("monitor", d.monitor);
  c.set_bool("vsync", d.vsync);
  c.set_bool("dither", d.dither);
}

SoundSettings read_sound(const Config& c) {
  SoundSettings s;
  s.enabled = c.get_bool("audio", true);
  s.volume = std::clamp(c.get_int("volume", 100), 0, 100);
  return s;
}

void write_sound(Config& c, const SoundSettings& s) {
  c.set_bool("audio", s.enabled);
  c.set_int("volume", s.volume);
}

std::filesystem::path default_memcard(int slot) {
  return config_dir() / ("memcard" + std::to_string(slot) + ".mcd");
}

std::string memcard_path(const Config& c, int slot) {
  const std::string key = "memcard" + std::to_string(slot);
  if (!c.has(key)) return slot == 1 ? path_utf8(default_memcard(1)) : std::string();
  const std::string v = c.get(key);
  return v == "none" ? std::string() : v;
}

std::filesystem::path mods_folder(const Config& c) {
  const std::string v = c.get("mods");
  return v.empty() ? config_dir() / "mods" : utf8_path(v);
}

}  // namespace ss::launcher
