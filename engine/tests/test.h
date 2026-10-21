#pragma once

// Minimal test harness: TEST(name) registers a function; CHECK records failures.

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace ss::test {

struct Case {
  const char* name;
  std::function<void()> fn;
};
std::vector<Case>& registry();
extern int failures;

struct Register {
  Register(const char* name, std::function<void()> fn) { registry().push_back({name, std::move(fn)}); }
};

}  // namespace ss::test

#define TEST(name)                                                   \
  static void test_##name();                                         \
  static ::ss::test::Register register_##name(#name, test_##name);   \
  static void test_##name()

#define CHECK(cond)                                                                  \
  do {                                                                               \
    if (!(cond)) {                                                                   \
      std::fprintf(stderr, "  %s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
      ++::ss::test::failures;                                                        \
    }                                                                                \
  } while (0)

#define CHECK_EQ(a, b)                                                                                 \
  do {                                                                                                 \
    const auto va = (a);                                                                               \
    const auto vb = (b);                                                                               \
    if (!(va == vb)) {                                                                                 \
      std::fprintf(stderr, "  %s:%d: CHECK_EQ failed: %s (%lld) != %s (%lld)\n", __FILE__, __LINE__, #a, \
                   static_cast<long long>(va), #b, static_cast<long long>(vb));                         \
      ++::ss::test::failures;                                                                          \
    }                                                                                                  \
  } while (0)
