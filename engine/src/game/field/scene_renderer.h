#pragma once

#include "core/types.h"
#include "game/field/tasks.h"
#include "runtime/libgpu.h"

// The field's SceneRenderer (field 0x8005f5c8-0x8005f8d8; global 0x800752d8): an 8-byte
// task at priority 1 (vtable 0x80073538), created last by field_create_subsystems. Each
// frame it flips the effects' four private OT layers (effects.h) and clears the new
// halves, takes the BG view origin (0x800761b0), and runs every live particle's
// behaviour (fx_particle_update) inside PushMatrix / PopMatrix. actor_sys_draw (priority
// 2) then splices the layers something drew into into the screen OT at layer id + 0x11,
// and sorts the actors standing on such a layer into it by depth.
namespace ss::game::field {

struct SceneRenderer final : Task {
  void update() override;  // vtable +0xc: scene_renderer_update
};

SceneRenderer* scene_renderer();                            // 0x800752d8 (null until created)
SceneRenderer* scene_renderer_create(SceneRenderer* self);  // 0x8005f5c8: registers it (priority 1)
void scene_renderer_update(SceneRenderer* self);            // 0x8005f604
// field_destroy_subsystems: deleted (the console frees it without unregistering; the
// task list is rebuilt next time).
void scene_renderer_destroy();
void ot_layer_clear(psyq::P_TAG* ot);  // 0x8005f770: links the 0x830 entries forward
void ot_layers_compact();              // 0x8005f814: unlinks empty entries (nothing to do on the host)
void field_free_ot_layers();           // 0x80053158: frees the pool and the layers; count = 0 (A 0x23, map loads)

// actor_sys_draw's OT-layer path (0x80043ac4), used while the pool exists:
int ot_layer_count();                         // 0x800761a8 (0 = no layers: actors link into the screen OT)
void ot_layers_splice();                      // dirty layers -> screen OT[id + 0x11]
psyq::P_TAG* ot_layer_table_for(u16 layer);  // current table of the dirty layer with that id, else null

}  // namespace ss::game::field
