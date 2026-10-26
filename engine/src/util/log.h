#pragma once

#include <cstddef>
#include <string_view>

// The programs' messages for the player (port feature): one place every message line passes,
// so the run's log (launcher/report.h open_log: shattered-stars.log beside config.ini, the file
// players attach to bug reports) gets them as the terminal does. ss_core has no launcher, so
// the launcher installs its log here; without one (tests, headless runs) a line goes to stderr
// only, as it always did.
namespace ss::util {

// Text (whole lines, "\n" included) to stderr, then to the log sink if there is one. On a
// Windows console the text is shown through the wide API, so non-ASCII names read right.
void log_text(std::string_view text);
// One line ("\n" added): the game's "[ap] ...", "[port] ...", "[mods] ..." messages.
void log_line(std::string_view line);
// printf-style text, as std::fprintf(stderr, ...) wrote it (the "\n" is the caller's).
void log_printf(const char* format, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 1, 2)))
#endif
    ;

// The log: called with each text after stderr got it (never at the same time twice).
using LogSink = void (*)(std::string_view text);
void set_log_sink(LogSink sink);

}  // namespace ss::util
