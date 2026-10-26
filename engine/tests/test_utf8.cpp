// util/utf8.h: the command line and the environment as UTF-8, and paths from UTF-8 that never
// throw (the 0.02.50 Windows check: a path with "Ä" on the command line aborted both programs
// with "filesystem error: Cannot convert character sequence: Illegal byte sequence").

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include "test.h"
#include "util/utf8.h"

using namespace ss;

namespace {

const std::string kTestA = "C:\\Users\\J\xC3\xB6rg\\Test \xC3\x84\\card.mcd";  // "...Jörg\Test Ä\card.mcd"

}  // namespace

TEST(utf8_round_trips_through_utf16) {
  std::u16string wide;
  CHECK(util::utf8_to_utf16(kTestA, &wide));
  CHECK(wide.find(u'\u00C4') != std::u16string::npos);
  CHECK(wide.find(u'\u00F6') != std::u16string::npos);
  CHECK(util::utf16_to_utf8(wide) == kTestA);
  // Four bytes: a surrogate pair.
  const std::string star = "\xF0\x9F\x8C\x9F";  // U+1F31F
  CHECK(util::utf8_to_utf16(star, &wide));
  CHECK_EQ(wide.size(), 2u);
  CHECK(util::utf16_to_utf8(wide) == star);
  // Not UTF-8: an ANSI "Ä" (1252), a lone continuation byte, an overlong '/', an encoded
  // surrogate, a sequence cut short.
  for (const std::string& bad : {std::string("Test \xC4\\card"), std::string("\x84"), std::string("\xC0\xAF"),
                                std::string("\xED\xA0\x80"), std::string("\xE2\x82")})
    CHECK(!util::utf8_to_utf16(bad, &wide));
  // Half a surrogate pair in a wide string (Windows allows it in names): U+FFFD.
  CHECK(util::utf16_to_utf8(std::u16string(1, u'\xD800')) == "\xEF\xBF\xBD");
}

TEST(utf8_paths_never_throw) {
  const std::filesystem::path p = util::utf8_path(kTestA);
  CHECK(util::path_utf8(p) == kTestA);
#ifdef _WIN32
  CHECK(util::path_utf8(p.filename()) == "card.mcd");
  CHECK(p.native().find(L'\u00C4') != std::wstring::npos);
#else
  CHECK(util::path_utf8(util::utf8_path("/tmp/Test \xC3\x84/card.mcd").parent_path().filename()) == "Test \xC3\x84");
  CHECK(p.native() == kTestA);  // Linux: the bytes are the name, as before
#endif
  // What used to throw: the ANSI "Ä" (Windows' argv before the fix). Windows reads it in the
  // ANSI code page; elsewhere the bytes stay as they are.
  bool threw = false;
  try {
    const std::filesystem::path ansi = util::utf8_path("C:\\Test \xC4\\card.mcd");
    CHECK(!util::path_utf8(ansi).empty());
#ifndef _WIN32
    CHECK(ansi.native() == "C:\\Test \xC4\\card.mcd");
#endif
  } catch (const std::exception&) {
    threw = true;
  }
  CHECK(!threw);
  // An empty path can't be made absolute: given back, no exception.
  CHECK(util::absolute_utf8("").empty());
  CHECK(util::utf8_path(util::absolute_utf8("x \xC3\x84.mcd")).is_absolute());
}

TEST(utf8_arguments) {
  char a0[] = "prog", a1[] = "--memcard1", a2[] = "/tmp/Test \xC3\x84/card.mcd";
  char* argv[] = {a0, a1, a2, nullptr};
  util::Arguments args(3, argv);
#ifdef _WIN32
  // This process's own command line (the test runner's), read through the wide API.
  CHECK(args.argc() >= 1);
  CHECK(args.argv()[args.argc()] == nullptr);
  // Windows' rules for quotes and backslashes; each argument comes back as UTF-8.
  const std::vector<std::string> split = util::split_command_line(
      L"\"C:\\Games\\Shattered Stars \u00C4\\shattered-stars.exe\" --headless --memcard1 "
      L"\"C:\\Users\\J\u00F6rg\\Test \u00C4\\card.mcd\" --set \"memcard1=C:\\Test \u00C4\\card.mcd\" "
      L"--ap-seed C:\\\u65E5\u672C\\AP.zip");
  CHECK_EQ(split.size(), 8u);
  if (split.size() == 8) {
    CHECK(split[0] == "C:\\Games\\Shattered Stars \xC3\x84\\shattered-stars.exe");
    CHECK(split[1] == "--headless");
    CHECK(split[3] == kTestA);
    CHECK(split[5] == "memcard1=C:\\Test \xC3\x84\\card.mcd");
    CHECK(split[6] == "--ap-seed");
    CHECK(split[7] == "C:\\\xE6\x97\xA5\xE6\x9C\xAC\\AP.zip");  // not in any Western ANSI code page
  }
#else
  // Elsewhere argv is UTF-8 already: as it is.
  CHECK_EQ(args.argc(), 3);
  CHECK(std::string(args.argv()[2]) == a2);
  CHECK(args.argv()[3] == nullptr);
#endif
}

TEST(utf8_environment) {
  const char* name = "SS_TEST_UTF8_VALUE";
#ifdef _WIN32
  SetEnvironmentVariableW(L"SS_TEST_UTF8_VALUE", L"C:\\Users\\J\u00F6rg\\Test \u00C4\\card.mcd");
#else
  setenv(name, kTestA.c_str(), 1);
#endif
  CHECK(util::env(name).has_value());
  CHECK(util::env(name).value_or("") == kTestA);
  CHECK(util::env_set(name));
#ifdef _WIN32
  SetEnvironmentVariableW(L"SS_TEST_UTF8_VALUE", nullptr);
#else
  unsetenv(name);
#endif
  CHECK(!util::env(name).has_value());
  CHECK(!util::env_set(name));
}
