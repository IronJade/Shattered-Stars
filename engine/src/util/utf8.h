#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// UTF-8 text on every platform (port feature). Every narrow string in the port is UTF-8: paths,
// config.ini, the command line, the environment, the log. Elsewhere the program's arguments and
// environment already are; Windows hands them to the narrow APIs (main's argv, getenv) in the
// ANSI code page instead (Ä = 0xC4 in 1252), which libstdc++'s paths read as UTF-8 and reject
// ("Cannot convert character sequence"), and which can't hold every name anyway. So there they
// are read through the wide APIs and converted once; nothing here throws.
namespace ss::util {

// The program's arguments as UTF-8, for main(): Windows reads them from GetCommandLineW
// (CommandLineToArgvW's split), elsewhere they are argv as it is.
//   util::Arguments utf8(argc, argv);  argc = utf8.argc(); argv = utf8.argv();
class Arguments {
 public:
  Arguments(int argc, char** argv);
  Arguments(const Arguments&) = delete;
  Arguments& operator=(const Arguments&) = delete;
  int argc() const { return static_cast<int>(strings_.size()); }
  char** argv() { return pointers_.data(); }

 private:
  std::vector<std::string> strings_;
  std::vector<char*> pointers_;  // into strings_, then a null
};

#ifdef _WIN32
// A Windows command line split into arguments (CommandLineToArgvW), each as UTF-8.
std::vector<std::string> split_command_line(const wchar_t* command_line);
#endif

// An environment variable as UTF-8 (Windows: the process's own UTF-16 block,
// GetEnvironmentVariableW); nothing when it isn't set.
std::optional<std::string> env(const char* name);
// Set and not empty.
bool env_set(const char* name);

// UTF-8 <-> UTF-16 (Windows' wide strings). An invalid sequence: false / U+FFFD.
bool utf8_to_utf16(std::string_view in, std::u16string* out);
std::string utf16_to_utf8(std::u16string_view in);

// UTF-8 strings <-> paths, never throwing. On Windows a string that isn't UTF-8 is read in
// the ANSI code page (the only other thing a narrow string there can be: a config.ini that
// Notepad saved as "ANSI"); elsewhere the bytes are the name.
std::filesystem::path utf8_path(std::string_view s);
std::string path_utf8(const std::filesystem::path& p);

// An absolute path for a command-line or environment path, as UTF-8; the path as given when
// it can't be made absolute (std::filesystem::absolute would throw).
std::string absolute_utf8(std::string_view s);

}  // namespace ss::util
