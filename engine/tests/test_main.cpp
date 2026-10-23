#include <cstdlib>
#include <cstring>

#include "test.h"

namespace ss::test {
std::vector<Case>& registry() {
  static std::vector<Case> cases;
  return cases;
}
int failures = 0;
}  // namespace ss::test

// SS_TEST=a,b (optional): run only the cases whose name contains one of these substrings.
static bool selected(const char* name) {
  const char* filter = std::getenv("SS_TEST");
  if (!filter || !*filter) return true;
  for (const char* p = filter; *p;) {
    const char* end = std::strchr(p, ',');
    const size_t n = end ? static_cast<size_t>(end - p) : std::strlen(p);
    if (n && std::string(name).find(std::string(p, n)) != std::string::npos) return true;
    p += n + (end ? 1 : 0);
  }
  return false;
}

int main() {
  int failed_cases = 0;
  size_t run = 0;
  for (const auto& c : ss::test::registry()) {
    if (!selected(c.name)) continue;
    ++run;
    const int before = ss::test::failures;
    c.fn();
    const bool ok = ss::test::failures == before;
    failed_cases += !ok;
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", c.name);
  }
  std::printf("%zu tests, %d failed\n", run, failed_cases);
  return failed_cases ? 1 : 0;
}
