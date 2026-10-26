#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

// Where the programs' messages go (port feature). On Windows the game and setup are GUI
// programs (no console window when double-clicked); this gives them one when it helps:
//
// - started from a terminal (cmd, PowerShell): output goes to that terminal;
// - started with redirected output (a pipe or a file: tests, scripts, Wine): kept as is;
// - double-clicked: no console, so a windowed game run keeps everything in its log
//   (shattered-stars.log beside config.ini) and fatal errors also show a message box.
//
// Lines end in "\n" on every platform (Windows' text mode would add "\r"). Elsewhere stdout /
// stderr are left alone; the log and the message box work the same.
namespace ss::launcher {

// Call first thing in main. 'own_console' (--debug: the debug console reads the terminal)
// opens a console window of its own when there is no terminal to attach to. Returns
// whether stderr reaches someone: a terminal, a pipe or a file.
bool attach_console(bool own_console);

// The run's log (overwritten): report() and fatal() lines go to it too, and so do the game's
// own message lines (util/log.h: "[ap] ...", "[port] ...", "[mods] ..."). Without a console
// (see above) stdout and stderr are redirected into it, so everything is kept.
// Bounded: past 4 MB (a long Archipelago session's server messages) it keeps the run's first
// MB, a line saying how much was left out, and the newest 2 MB, so it never passes 4 MB by
// more than a line.
void open_log(const std::filesystem::path& file);
// Tests: the log closed (stderr stays where it is), and another bound in bytes (0: 4 MB).
void close_log();
void set_log_limit(std::uintmax_t bytes);
// Tests: as if stderr reached someone or no one (the double-clicked game: on Windows the next
// open_log() then redirects stdout and stderr into the log).
void set_console_for_tests(bool reaches);

// A message for the player: stderr, and the log.
void report(const char* format, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 1, 2)))
#endif
    ;

// A fatal error: report(), and a message box when stderr reaches no one (never when
// SDL_VIDEO_DRIVER is offscreen or dummy: tests).
void fatal(const std::string& text);

}  // namespace ss::launcher
