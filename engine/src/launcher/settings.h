#pragma once

#include <filesystem>
#include <string>

#include "launcher/config.h"

// The player's settings as config.ini stores them, with their defaults, shared by the game
// (main.cpp hands them to the platform layer) and the setup program.
namespace ss::launcher {

enum class WindowMode { kWindowed, kBorderless, kFullscreen };

struct DisplaySettings {
  WindowMode mode = WindowMode::kWindowed;
  int scale = 3;  // windowed: 320 x 240 times this (3: 960 x 720)
  // Exclusive fullscreen: the display mode (0 x 0: the desktop's).
  int width = 0, height = 0;
  float refresh = 0;  // Hz; 0: any
  int monitor = 1;    // 1 = the primary display
  bool vsync = true;
  bool dither = true;  // the PlayStation's colour dithering
  bool operator==(const DisplaySettings&) const = default;
};

struct SoundSettings {
  bool enabled = true;
  int volume = 100;  // 0-100
  bool operator==(const SoundSettings&) const = default;
};

inline constexpr int kMinScale = 1, kMaxScale = 8;

DisplaySettings read_display(const Config& c);
void write_display(Config& c, const DisplaySettings& d);
SoundSettings read_sound(const Config& c);
void write_sound(Config& c, const SoundSettings& s);

const char* window_mode_name(WindowMode m);  // "windowed" / "borderless" / "fullscreen"

// Memory card slots: the configured file, or the default (slot 1: memcard1.mcd next to
// config.ini; slot 2: empty). "none" (an empty slot) is returned as an empty string.
std::string memcard_path(const Config& c, int slot);
std::filesystem::path default_memcard(int slot);  // config_dir() / "memcardN.mcd"

// The world-edit folder: the configured one, or mods/ next to config.ini.
std::filesystem::path mods_folder(const Config& c);

}  // namespace ss::launcher
