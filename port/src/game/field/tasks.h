#pragma once

#include "core/types.h"

// The field task list (0x80056178-0x80056374): up to 24 objects with a priority
// 0-4; every frame task_run_all calls each one's update (vtable +0xc), priority by
// priority. Registered: 0 script VM; 1 load queue, fades, battle transition, sound
// helpers; 2 actors; 4 message windows and text panels.
namespace ss::game::field {

struct Task {
  virtual ~Task() = default;
  virtual void update() = 0;  // vtable +0xc
};

void task_list_clear();                  // 0x80056178
void task_run_all();                     // 0x800561a4
void task_register(Task* task, int prio);  // 0x80056270
void task_unregister(Task* task);        // 0x80056304

}  // namespace ss::game::field
