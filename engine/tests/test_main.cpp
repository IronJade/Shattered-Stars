#include "test.h"

namespace ss::test {
std::vector<Case>& registry() {
  static std::vector<Case> cases;
  return cases;
}
int failures = 0;
}  // namespace ss::test

int main() {
  int failed_cases = 0;
  for (const auto& c : ss::test::registry()) {
    const int before = ss::test::failures;
    c.fn();
    const bool ok = ss::test::failures == before;
    failed_cases += !ok;
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", c.name);
  }
  std::printf("%zu tests, %d failed\n", ss::test::registry().size(), failed_cases);
  return failed_cases ? 1 : 0;
}
