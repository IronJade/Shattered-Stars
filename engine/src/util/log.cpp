#include "util/log.h"

#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <io.h>
#include <windows.h>

#include "util/utf8.h"
#endif

namespace ss::util {
namespace {

std::mutex& log_mutex() {
  static std::mutex m;
  return m;
}
LogSink g_sink = nullptr;

void to_stderr(std::string_view text) {
#ifdef _WIN32
  // A console's code page is the OEM one, not UTF-8: through the wide API the text reads as
  // written. A pipe or a file (scripts, tests, the log) gets the UTF-8 bytes below.
  const auto h = reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(stderr)));
  DWORD mode = 0;
  if (h != INVALID_HANDLE_VALUE && h != nullptr && GetConsoleMode(h, &mode)) {
    std::u16string wide;
    if (utf8_to_utf16(text, &wide)) {
      std::fflush(stderr);
      DWORD written = 0;
      if (WriteConsoleW(h, wide.data(), static_cast<DWORD>(wide.size()), &written, nullptr)) return;
    }
  }
#endif
  std::fwrite(text.data(), 1, text.size(), stderr);
}

}  // namespace

void log_text(std::string_view text) {
  std::lock_guard<std::mutex> lock(log_mutex());
  to_stderr(text);
  if (g_sink != nullptr) g_sink(text);
}

void log_line(std::string_view line) {
  std::string text(line);
  text += '\n';
  log_text(text);
}

void log_printf(const char* format, ...) {
  va_list args;
  va_start(args, format);
  va_list copy;
  va_copy(copy, args);
  char small[1024];
  const int n = std::vsnprintf(small, sizeof small, format, args);
  std::string text;
  if (n >= static_cast<int>(sizeof small)) {
    text.resize(static_cast<size_t>(n));
    std::vsnprintf(text.data(), text.size() + 1, format, copy);
  } else if (n > 0) {
    text.assign(small, static_cast<size_t>(n));
  }
  va_end(copy);
  va_end(args);
  log_text(text);
}

void set_log_sink(LogSink sink) {
  std::lock_guard<std::mutex> lock(log_mutex());
  g_sink = sink;
}

}  // namespace ss::util
