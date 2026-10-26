#include "util/utf8.h"

#include <cstdlib>
#include <cstring>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>  // CommandLineToArgvW (shell32)
#endif

namespace ss::util {
namespace {

#ifdef _WIN32
static_assert(sizeof(wchar_t) == sizeof(char16_t));
std::u16string_view as_utf16(const wchar_t* s, size_t n) { return {reinterpret_cast<const char16_t*>(s), n}; }
std::string wide_utf8(const wchar_t* s) { return utf16_to_utf8(as_utf16(s, std::wcslen(s))); }
#endif

void put_utf8(char32_t cp, std::string& out) {
  if (cp < 0x80) {
    out += static_cast<char>(cp);
  } else if (cp < 0x800) {
    out += static_cast<char>(0xC0 | (cp >> 6));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    out += static_cast<char>(0xE0 | (cp >> 12));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    out += static_cast<char>(0xF0 | (cp >> 18));
    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  }
}

}  // namespace

Arguments::Arguments(int argc, char** argv) {
#ifdef _WIN32
  strings_ = split_command_line(GetCommandLineW());
#endif
  if (strings_.empty())  // elsewhere; or Windows without a command line to read
    for (int i = 0; i < argc; ++i) strings_.emplace_back(argv[i] != nullptr ? argv[i] : "");
  for (std::string& s : strings_) pointers_.push_back(s.data());
  pointers_.push_back(nullptr);
}

#ifdef _WIN32
std::vector<std::string> split_command_line(const wchar_t* command_line) {
  std::vector<std::string> out;
  if (command_line == nullptr) return out;
  int n = 0;
  wchar_t** args = CommandLineToArgvW(command_line, &n);
  if (args == nullptr) return out;
  for (int i = 0; i < n; ++i) out.push_back(wide_utf8(args[i]));
  LocalFree(args);
  return out;
}
#endif

std::optional<std::string> env(const char* name) {
#ifdef _WIN32
  // The process's own block, not the C runtime's copies (msvcrt may derive its wide copy,
  // _wgetenv's, from the narrow ANSI one). _putenv_s changes the block too.
  const std::wstring wide_name(name, name + std::strlen(name));  // the names are ASCII
  std::wstring value(256, L'\0');
  for (;;) {
    SetLastError(ERROR_SUCCESS);
    const DWORD n = GetEnvironmentVariableW(wide_name.c_str(), value.data(), static_cast<DWORD>(value.size()));
    if (n == 0) {
      if (GetLastError() == ERROR_ENVVAR_NOT_FOUND) return std::nullopt;
      return std::string();  // set, empty
    }
    if (n < value.size()) {
      value.resize(n);
      return utf16_to_utf8(as_utf16(value.data(), value.size()));
    }
    value.resize(n);  // n: the size needed, with its terminator
  }
#else
  const char* v = std::getenv(name);
  if (v == nullptr) return std::nullopt;
  return std::string(v);
#endif
}

bool env_set(const char* name) {
  const std::optional<std::string> v = env(name);
  return v && !v->empty();
}

bool utf8_to_utf16(std::string_view in, std::u16string* out) {
  out->clear();
  out->reserve(in.size());
  static constexpr char32_t kSmallest[] = {0, 0, 0x80, 0x800, 0x10000};  // by length: no overlong forms
  for (size_t i = 0; i < in.size();) {
    const auto c = static_cast<unsigned char>(in[i]);
    int len;
    char32_t cp;
    if (c < 0x80) len = 1, cp = c;
    else if ((c & 0xE0) == 0xC0) len = 2, cp = c & 0x1F;
    else if ((c & 0xF0) == 0xE0) len = 3, cp = c & 0x0F;
    else if ((c & 0xF8) == 0xF0) len = 4, cp = c & 0x07;
    else return false;
    if (in.size() - i < static_cast<size_t>(len)) return false;
    for (int k = 1; k < len; ++k) {
      const auto cc = static_cast<unsigned char>(in[i + k]);
      if ((cc & 0xC0) != 0x80) return false;
      cp = (cp << 6) | (cc & 0x3F);
    }
    if (cp < kSmallest[len] || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
    if (cp >= 0x10000) {
      cp -= 0x10000;
      out->push_back(static_cast<char16_t>(0xD800 + (cp >> 10)));
      out->push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
    } else {
      out->push_back(static_cast<char16_t>(cp));
    }
    i += static_cast<size_t>(len);
  }
  return true;
}

std::string utf16_to_utf8(std::u16string_view in) {
  std::string out;
  out.reserve(in.size());
  for (size_t i = 0; i < in.size(); ++i) {
    char32_t cp = in[i];
    if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < in.size() && in[i + 1] >= 0xDC00 && in[i + 1] <= 0xDFFF) {
      cp = 0x10000 + ((cp - 0xD800) << 10) + (in[i + 1] - 0xDC00);
      ++i;
    } else if (cp >= 0xD800 && cp <= 0xDFFF) {
      cp = 0xFFFD;  // half a pair
    }
    put_utf8(cp, out);
  }
  return out;
}

std::filesystem::path utf8_path(std::string_view s) {
#ifdef _WIN32
  std::u16string wide;
  if (!utf8_to_utf16(s, &wide)) {
    // Not UTF-8: the ANSI code page's text.
    std::wstring ansi;
    const int n = MultiByteToWideChar(CP_ACP, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (n > 0) {
      ansi.resize(static_cast<size_t>(n));
      MultiByteToWideChar(CP_ACP, 0, s.data(), static_cast<int>(s.size()), ansi.data(), n);
    }
    return std::filesystem::path(ansi);
  }
  return std::filesystem::path(std::wstring(wide.begin(), wide.end()));
#else
  return std::filesystem::path(std::string(s));
#endif
}

std::string path_utf8(const std::filesystem::path& p) {
#ifdef _WIN32
  const std::wstring& w = p.native();
  return utf16_to_utf8(as_utf16(w.data(), w.size()));
#else
  return p.native();
#endif
}

std::string absolute_utf8(std::string_view s) {
  std::error_code ec;
  const std::filesystem::path p = std::filesystem::absolute(utf8_path(s), ec);
  return ec ? std::string(s) : path_utf8(p);
}

}  // namespace ss::util
