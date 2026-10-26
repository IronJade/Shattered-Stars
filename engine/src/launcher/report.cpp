#include "launcher/report.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>

#include "util/log.h"

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
std::filesystem::path g_log_path;

// The log's bound (see open_log): past the limit it keeps its first quarter and its last half.
constexpr std::uintmax_t kLogLimit = 4u << 20;
std::uintmax_t g_log_limit = kLogLimit;
std::string g_log_head;         // after the first trim: the run's first lines, kept at the top
std::size_t g_log_marker = 0;   // the "left out" line's length, below the head
std::uintmax_t g_log_dropped = 0;

std::FILE* log_stream() { return g_log_is_stderr ? stderr : g_log; }

// The log past its limit: the head (the run's start: the version, the discs, the seed), one
// line saying how much was left out, and the newest lines, each part cut at a line's end.
void trim_log(std::FILE* f) {
  std::fflush(f);
  std::string all;
  {
    std::ifstream in(g_log_path, std::ios::binary);
    if (!in) return;
    all.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  }
  if (all.size() <= g_log_limit) return;
  std::size_t body = 0;
  if (g_log_head.empty()) {
    const std::size_t quarter = static_cast<std::size_t>(g_log_limit / 4);
    const std::size_t nl = all.rfind('\n', quarter);
    body = nl == std::string::npos ? quarter : nl + 1;
    g_log_head = all.substr(0, body);
  } else {
    body = std::min(all.size(), g_log_head.size() + g_log_marker);
  }
  const std::string_view rest(all.data() + body, all.size() - body);
  const std::size_t half = static_cast<std::size_t>(g_log_limit / 2);
  std::size_t from = rest.size() > half ? rest.size() - half : 0;
  if (from > 0)
    if (const std::size_t nl = rest.find('\n', from); nl != std::string_view::npos) from = nl + 1;
  g_log_dropped += from;
  const std::string limit = g_log_limit >= (1u << 20) ? std::to_string(g_log_limit >> 20) + " MB"
                                                      : std::to_string(g_log_limit >> 10) + " KB";
  const std::string marker = "[log] ... " + std::to_string(g_log_dropped) +
                             " bytes of this run's messages left out here (the log keeps its first and last part, " +
                             limit + " at most) ...\n";
  g_log_marker = marker.size();
  const std::string out = g_log_head + marker + std::string(rest.substr(from));
#ifdef _WIN32
  _chsize(_fileno(f), 0);
#else
  if (ftruncate(fileno(f), 0) != 0) return;
#endif
  std::fseek(f, 0, SEEK_SET);
  std::fwrite(out.data(), 1, out.size(), f);
  std::fflush(f);
}

// util::log_text's sink: the text is in the log (stderr already put it there when stderr is
// the log), then the bound.
void log_sink(std::string_view text) {
  std::FILE* f = log_stream();
  if (f == nullptr) return;
  if (!g_log_is_stderr) std::fwrite(text.data(), 1, text.size(), f);
  std::fflush(f);
  const long long size =
#ifdef _WIN32
      _ftelli64(f);
#else
      ftello(f);
#endif
  if (size > 0 && static_cast<std::uintmax_t>(size) > g_log_limit) trim_log(f);
}

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
  g_log_path = file;
  g_log_head.clear();
  g_log_marker = 0;
  g_log_dropped = 0;
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
      util::set_log_sink(log_sink);
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
    util::set_log_sink(log_sink);
  }
}

void close_log() {
  util::set_log_sink(nullptr);
  if (g_log) std::fclose(g_log);
  g_log = nullptr;
  g_log_is_stderr = false;  // (stderr itself stays where it is)
  g_log_limit = kLogLimit;
}

void set_console_for_tests(bool reaches) { g_console = reaches; }

void set_log_limit(std::uintmax_t bytes) { g_log_limit = bytes > 0 ? bytes : kLogLimit; }

void report(const char* format, ...) {
  va_list args;
  va_start(args, format);
  va_list copy;
  va_copy(copy, args);
  char small[1024];
  const int n = std::vsnprintf(small, sizeof small, format, args);
  std::string text;
  if (n >= static_cast<int>(sizeof small)) {
    text.resize(static_cast<std::size_t>(n));
    std::vsnprintf(text.data(), text.size() + 1, format, copy);
  } else if (n > 0) {
    text.assign(small, static_cast<std::size_t>(n));
  }
  va_end(copy);
  va_end(args);
  util::log_text(text);  // stderr, and the log
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
