#include "game/field/tasks.h"

namespace ss::game::field {
namespace {

Task* g_tasks[24] = {};  // 0x8007591c
u8 g_prio[24] = {};      // 0x8007597c
u16 g_count = 0;         // 0x80075994

}  // namespace

void task_list_clear() {
  g_count = 0;
  for (auto& t : g_tasks) t = nullptr;
}

void task_run_all() {
  for (int prio = 0; prio < 5; ++prio) {
    for (int i = 0; i < g_count; ++i) {
      if (g_prio[i] == prio && g_tasks[i]) g_tasks[i]->update();
    }
  }
}

void task_register(Task* task, int prio) {
  u32 i = 0;
  while (i < static_cast<u32>(g_count) + 1 && i < 24 && g_tasks[i] != nullptr) ++i;
  g_prio[i] = static_cast<u8>(prio);
  g_tasks[i] = task;
  if (i == g_count) ++g_count;
}

void task_unregister(Task* task) {
  for (int i = 0; i < g_count; ++i) {
    if (g_tasks[i] == task) {
      g_tasks[i] = nullptr;
      if (i == g_count - 1) --g_count;
      return;
    }
  }
}

}  // namespace ss::game::field
