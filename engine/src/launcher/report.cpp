#include "launcher/report.h"

#include <SDL3/SDL.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <system_error>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace ss::launcher {
namespace {

bool g_console = true;         // stderr reaches someone
std::FILE* g_log = nullptr;    // the log, when it is a file of its own
bool g_log_is_stderr = false;  // stderr itself was redirected into the log

#ifdef _WIN32
bool usable(DWORD which) {
  const HANDLE h = GetStdHandle(which);
  return h != nullptr && h != INVALID_HANDLE_VALUE && GetFileType(h) != FILE_TYPE_UNKNOWN;
}

// Points the C streams at the console this process now has.
void reopen_console(bool out, bool err, bool in) {
  if (out && std::freopen("CONOUT$", "w", stdout)) std::setvbuf(stdout, nullptr, _IONBF, 0);
  if (err && std::freopen("CONOUT$", "w", stderr)) std::setvbuf(stderr, nullptr, _IONBF, 0);
  if (in) std::freopen("CONIN$", "r", stdin);
}
#endif

bool tests_running() {
  const char* video = std::getenv("SDL_VIDEO_DRIVER");
  return video && (std::strcmp(video, "offscreen") == 0 || std::strcmp(video, "dummy") == 0);
}

}  // namespace

bool attach_console(bool own_console) {
#ifdef _WIN32
  const bool out = usable(STD_OUTPUT_HANDLE), err = usable(STD_ERROR_HANDLE), in = usable(STD_INPUT_HANDLE);
  if (own_console && !in) {
    // The debug console needs a keyboard: a console window of its own (attaching to the
    // terminal it was started from would share that terminal's input with its shell).
    FreeConsole();
    if (AllocConsole()) reopen_console(!out, !err, true);
  } else if ((!out || !err) && AttachConsole(ATTACH_PARENT_PROCESS)) {
    reopen_console(!out, !err, false);
  }
  g_console = usable(STD_ERROR_HANDLE) || GetConsoleWindow() != nullptr;
  // "\n" stays "\n" (not "\r\n"): the same output as elsewhere, for scripts that read it.
  _setmode(_fileno(stdout), _O_BINARY);
  _setmode(_fileno(stderr), _O_BINARY);
#else
  (void)own_console;
  struct stat st {};
  g_console = isatty(STDERR_FILENO) ||
              (fstat(STDERR_FILENO, &st) == 0 && (S_ISFIFO(st.st_mode) || S_ISREG(st.st_mode)));
#endif
  return g_console;
}

void open_log(const std::filesystem::path& file) {
  std::error_code ec;
  std::filesystem::create_directories(file.parent_path(), ec);
  constexpr const char* kHeader = "# Shattered Stars: this run's messages (the file is replaced at each start)\n";
#ifdef _WIN32
  if (!g_console) {
    // Nobody sees stderr: send stdout and stderr (the game's own messages too) to the log.
    if (_wfreopen(file.c_str(), L"wb", stderr)) {
      _dup2(_fileno(stderr), _fileno(stdout));
      std::setvbuf(stderr, nullptr, _IONBF, 0);
      std::setvbuf(stdout, nullptr, _IONBF, 0);
      g_log_is_stderr = true;
      std::fputs(kHeader, stderr);
    }
    return;
  }
  g_log = _wfopen(file.c_str(), L"wb");
#else
  g_log = std::fopen(file.c_str(), "w");
#endif
  if (g_log) {
    std::fputs(kHeader, g_log);
    std::fflush(g_log);
  }
}

void report(const char* format, ...) {
  va_list args;
  va_start(args, format);
  va_list copy;
  va_copy(copy, args);
  std::vfprintf(stderr, format, args);
  if (g_log && !g_log_is_stderr) {
    std::vfprintf(g_log, format, copy);
    std::fflush(g_log);
  }
  va_end(copy);
  va_end(args);
}

void fatal(const std::string& text) {
  std::string line = text;
  if (line.empty() || line.back() != '\n') line += '\n';
  report("%s", line.c_str());
  if (!g_console && !tests_running()) {
    std::string shown = text;
    while (!shown.empty() && shown.back() == '\n') shown.pop_back();
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Shattered Stars", shown.c_str(), nullptr);
  }
}

}  // namespace ss::launcher
