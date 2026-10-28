#pragma once

#include <filesystem>
#include <string>
#include <vector>

// Starting the game from setup and setup from the game (port feature). Both programs sit
// in the same folder: shattered-stars(.exe) and setup.exe on Windows /
// shattered-stars-setup elsewhere (in a macOS .app: its Contents/MacOS, both apps holding both).
namespace ss::launcher {

#ifdef _WIN32
inline constexpr const char* kGameProgram = "shattered-stars.exe";
inline constexpr const char* kSetupProgram = "setup.exe";
#else
inline constexpr const char* kGameProgram = "shattered-stars";
inline constexpr const char* kSetupProgram = "shattered-stars-setup";
#endif

// The setup program's exit codes when the game started it (--from-game).
inline constexpr int kSetupPlay = 0;  // settings saved with a valid disc 1: carry on
inline constexpr int kSetupQuit = 1;  // the player closed setup: the game exits too

// 'name' next to this executable; empty when it isn't there.
std::filesystem::path sibling_program(const char* name);

// macOS: this program runs from an .app bundle (Shattered Stars.app, Shattered Stars
// Setup.app; port/tools/build_macos.sh), whose contents must stay as signed: no discs
// folder inside it. False everywhere else.
bool in_app_bundle();

// Runs a program and waits for it; its exit code, or -1 if it could not be started.
int run_and_wait(const std::filesystem::path& program, const std::vector<std::string>& args,
                 std::string* error = nullptr);

// Starts a program in the background and returns at once; false if it could not start.
bool start_program(const std::filesystem::path& program, const std::vector<std::string>& args,
                   std::string* error = nullptr);

}  // namespace ss::launcher
