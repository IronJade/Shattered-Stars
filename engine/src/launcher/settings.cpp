#include "launcher/settings.h"

#include <SDL3/SDL.h>

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

std::string read_mod_order(const Config& c) { return c.get("mod_order"); }

void write_mod_order(Config& c, const std::string& order) {
  if (order.empty()) c.erase("mod_order");
  else c.set("mod_order", order);
}

}  // namespace ss::launcher

namespace ss::launcher {

const char* const kPadButtonKeys[16] = {"l2",    "r2", "l1", "r1",    "triangle", "circle", "cross", "square",
                                        "select", "l3", "r3", "start", "up",       "right",  "down",  "left"};

std::string key_name(int keycode) {
  if (keycode == 0) return "none";
  const char* name = SDL_GetKeyName(static_cast<SDL_Keycode>(keycode));
  return name && *name ? name : "none";
}

int key_from_name(const std::string& name) {
  if (name.empty() || name == "none") return 0;
  const SDL_Keycode key = SDL_GetKeyFromName(name.c_str());
  return key == SDLK_UNKNOWN ? 0 : static_cast<int>(key);
}

std::string gamepad_button_name(int button) {
  if (button == platform::kLeftTrigger) return SDL_GetGamepadStringForAxis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
  if (button == platform::kRightTrigger) return SDL_GetGamepadStringForAxis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
  if (button < 0) return "none";
  const char* name = SDL_GetGamepadStringForButton(static_cast<SDL_GamepadButton>(button));
  return name ? name : "none";
}

int gamepad_button_from_name(const std::string& name) {
  if (name.empty() || name == "none") return platform::kNoButton;
  const SDL_GamepadAxis axis = SDL_GetGamepadAxisFromString(name.c_str());
  if (axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER) return platform::kLeftTrigger;
  if (axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) return platform::kRightTrigger;
  const SDL_GamepadButton b = SDL_GetGamepadButtonFromString(name.c_str());
  return b == SDL_GAMEPAD_BUTTON_INVALID ? platform::kNoButton : static_cast<int>(b);
}

ControlSettings read_controls(const Config& c) {
  ControlSettings s;
  for (int i = 0; i < 16; ++i) {
    const std::string key = std::string("key_") + kPadButtonKeys[i], pad = std::string("pad_") + kPadButtonKeys[i];
    if (c.has(key)) s.controls.keys[i] = key_from_name(c.get(key));
    if (c.has(pad)) s.controls.buttons[i] = gamepad_button_from_name(c.get(pad));
  }
  if (c.has("stick_deadzone")) s.stick_deadzone = std::clamp(c.get_int("stick_deadzone", kGameStickDeadzone), 0, 90);
  s.stick_dpad = c.get_bool("stick_dpad", true);
  return s;
}

void write_controls(Config& c, const ControlSettings& s) {
  const platform::Controls defaults = platform::Controls::defaults();
  for (int i = 0; i < 16; ++i) {
    const std::string key = std::string("key_") + kPadButtonKeys[i], pad = std::string("pad_") + kPadButtonKeys[i];
    if (s.controls.keys[i] == defaults.keys[i]) c.erase(key);
    else c.set(key, key_name(s.controls.keys[i]));
    if (s.controls.buttons[i] == defaults.buttons[i]) c.erase(pad);
    else c.set(pad, gamepad_button_name(s.controls.buttons[i]));
  }
  if (s.stick_deadzone < 0) c.erase("stick_deadzone");
  else c.set_int("stick_deadzone", s.stick_deadzone);
  if (s.stick_dpad) c.erase("stick_dpad");
  else c.set_bool("stick_dpad", false);
}

int stick_deadzone_units(const ControlSettings& s) {
  return s.stick_deadzone < 0 ? -1 : (s.stick_deadzone * 128 + 50) / 100;
}

}  // namespace ss::launcher
