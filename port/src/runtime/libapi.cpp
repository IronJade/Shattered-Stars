#include "runtime/libapi.h"

#include <array>

namespace ss::psyq {
namespace {

struct RootCounter {
  u32 count = 0;
  u32 target = 0;
  bool interrupt = false;  // RCntMdINTR
  bool running = false;    // StartRCnt (interrupt unmasked)
};

struct Handler {
  long event = 0;
  long (*func)() = nullptr;
};

RootCounter g_rcnt2;
std::array<Handler, 4> g_handlers{};
bool g_in_handler = false;

bool is_rcnt2(u32 spec) { return spec == RCntCNT2 || (spec & 0xF) == 2; }

}  // namespace

long SetRCnt(u32 spec, u16 target, long mode) {
  if (!is_rcnt2(spec)) return 0;
  g_rcnt2.target = target;
  g_rcnt2.interrupt = (mode & RCntMdINTR) != 0;
  g_rcnt2.count = 0;
  return 1;
}

long GetRCnt(u32 spec) { return is_rcnt2(spec) ? static_cast<long>(g_rcnt2.count) : 0; }

long StartRCnt(u32 spec) {
  if (!is_rcnt2(spec)) return 0;
  g_rcnt2.running = true;
  return 1;
}

long StopRCnt(u32 spec) {
  if (!is_rcnt2(spec)) return 0;
  g_rcnt2.running = false;
  return 1;
}

long ResetRCnt(u32 spec) {
  if (!is_rcnt2(spec)) return 0;
  g_rcnt2.count = 0;
  return 1;
}

long OpenEvent(u32 desc, long spec, long mode, long (*func)()) {
  const long handle = OpenEvent(desc, spec, mode, static_cast<void (*)()>(nullptr));
  if (desc == RCntCNT2 && (mode & EvMdINTR) && func) {
    // One handler per function: re-opening (after a shutdown) replaces it.
    for (auto& h : g_handlers)
      if (h.func == func || h.func == nullptr) {
        h = Handler{handle, func};
        return handle;
      }
  }
  return handle;
}

// The counter resets when it reaches its target (the mode SetRCnt selects), so its
// interrupt period is 'target' ticks.
void rcnt_advance(u32 ticks) {
  RootCounter& c = g_rcnt2;
  if (!c.running || c.target == 0) {
    c.count = (c.count + ticks) & 0xFFFF;
    return;
  }
  c.count += ticks;
  while (c.count >= c.target) {
    c.count -= c.target;
    if (!c.interrupt || g_in_handler) continue;
    g_in_handler = true;
    for (auto& h : g_handlers)
      if (h.func) h.func();
    g_in_handler = false;
  }
}

void libapi_reset() {
  g_rcnt2 = RootCounter{};
  g_handlers.fill(Handler{});
  g_in_handler = false;
}

}  // namespace ss::psyq
