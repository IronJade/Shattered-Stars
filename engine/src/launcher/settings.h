#pragma once

#include <filesystem>
#include <string>

#include "launcher/config.h"
#include "platform/window.h"

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

// Dialogue (port enhancements, both off by default; game/field/message.h TextOptions):
// instant_text shows each message page at once; auto_advance moves a message waiting for
// the confirm button on by itself after auto_advance_delay milliseconds, but never answers
// a choice. Keys at their default are left out of the file.
struct TextSettings {
  bool instant = false;
  bool auto_advance = false;
  int auto_advance_ms = 1000;
  bool operator==(const TextSettings&) const = default;
};
// The delays setup offers (milliseconds); config.ini may hold any value in the range.
inline constexpr int kAutoAdvanceDelays[] = {500, 1000, 1500, 2000, 3000};
inline constexpr int kMinAutoAdvanceMs = 100, kMaxAutoAdvanceMs = 10000;
TextSettings read_text(const Config& c);
void write_text(Config& c, const TextSettings& s);
// The delay in vblanks (60 a second), at least 1; 0 when auto-advance is off.
int auto_advance_frames(const TextSettings& s);

// Controls: config.ini's key_<button> (an SDL key name: "Z", "Return", "Left Shift") and
// pad_<button> (an SDL gamepad name: "a", "dpup", "leftshoulder", "lefttrigger"), "none"
// for unbound, for the buttons in kPadButtonKeys; stick_deadzone (percent of the stick's
// travel; absent: the game's own, 25%) and stick_dpad (0: the left stick doesn't work the
// d-pad). Keys at their default are left out of the file.
struct ControlSettings {
  platform::Controls controls = platform::Controls::defaults();
  int stick_deadzone = -1;  // percent; -1: the game's
  bool stick_dpad = true;
  bool operator==(const ControlSettings&) const = default;
};
// By PadButton bit: "l2", "r2", "l1", "r1", "triangle", "circle", "cross", "square",
// "select", "l3", "r3", "start", "up", "right", "down", "left".
extern const char* const kPadButtonKeys[16];
inline constexpr int kGameStickDeadzone = 25;  // percent: the game's 0x20 of 0x80

ControlSettings read_controls(const Config& c);
void write_controls(Config& c, const ControlSettings& s);
// The stick dead zone in raw stick units for game::pad_set_player_stick (-1: the game's).
int stick_deadzone_units(const ControlSettings& s);

// Names as config.ini stores them (SDL's); 0 / platform::kNoButton for "none" or unknown.
std::string key_name(int keycode);
int key_from_name(const std::string& name);
std::string gamepad_button_name(int button);
int gamepad_button_from_name(const std::string& name);

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
// The mods' load order and enabled set (config.ini's mod_order; mods/catalog.h has the
// syntax and the defaults). Empty: the defaults (loose patches on, packages off).
std::string read_mod_order(const Config& c);
void write_mod_order(Config& c, const std::string& order);  // an empty order removes the key

}  // namespace ss::launcher
