#include "game/field/scene_renderer.h"

#include <memory>

#include "game/field/actors.h"
#include "game/field/bg.h"
#include "game/field/effects.h"
#include "game/field/field_movie.h"
#include "game/field/screen.h"
#include "runtime/libgte.h"

namespace ss::game::field {
namespace {

std::unique_ptr<SceneRenderer> g_renderer;  // 0x800752d8

}  // namespace

SceneRenderer* scene_renderer() { return g_renderer.get(); }

SceneRenderer* scene_renderer_create(SceneRenderer* self) {
  g_renderer.reset(self);
  task_register(self, 1);
  actor_hooks().ot_layer_count = ot_layer_count;
  return self;
}

void scene_renderer_destroy() {
  if (!g_renderer) return;
  task_unregister(g_renderer.get());
  g_renderer.reset();
}

void SceneRenderer::update() { scene_renderer_update(this); }

void scene_renderer_update(SceneRenderer*) {
  // Skipped while a movie plays (the sub-program at 0x800752c8, #3103).
  if (field_sub_program() != nullptr) return;
  FxState& fx = fx_state();
  if (fx.count == 0) return;
  for (auto& layer : fx.layer) {
    if (!layer) continue;
    layer->half ^= 1;
    layer->dirty = 0;
    ot_layer_clear(layer->current());
  }
  psyq::PushMatrix();
  s16 origin[2] = {0, 0};
  if (BgRenderer* bg = bg_renderer()) bg_get_view_origin(bg, origin);
  fx.origin[0] = origin[0];
  fx.origin[1] = origin[1];
  for (int i = 0; i < fx.count; ++i) {
    Particle* p = &fx.pool[i];
    if (p->type != 0 && p->layer != nullptr) fx_particle_update(p);
  }
  psyq::PopMatrix();
}

void ot_layer_clear(psyq::P_TAG* ot) {
  // Entries 0..0x82e link to the next; 0x82f ends the table (0x800756d0). Splicing
  // replaces 0x82e's link, so 0x82f is never reached.
  psyq::ClearOTag(ot, kFxLayerEntries);
}

void ot_layers_compact() {
  // The console unlinks empty entries of the dirty layers so the GPU walks fewer tags;
  // the host's drawing is the same either way.
}

void field_free_ot_layers() {
  FxState& fx = fx_state();
  fx.pool.reset();
  for (auto& layer : fx.layer) layer.reset();
  fx.count = 0;
}

int ot_layer_count() { return fx_state().count; }

void ot_layers_splice() {
  FxState& fx = fx_state();
  Screen* screen = field_screen();
  if (screen == nullptr || screen->current == nullptr) return;
  for (int i = 0; i < 4; ++i) {
    OtLayer* layer = fx.layer[i].get();
    if (layer == nullptr || layer->dirty == 0) continue;
    psyq::P_TAG* entry = screen->ot_entry(static_cast<int>(fx.layer_id[i]) + 0x11);
    psyq::P_TAG* table = layer->current();
    psyq::P_TAG* old = entry->next;
    entry->next = &table[0];
    table[0x82E].next = old;
  }
}

psyq::P_TAG* ot_layer_table_for(u16 layer_id) {
  FxState& fx = fx_state();
  for (int i = 0; i < 4; ++i) {
    OtLayer* layer = fx.layer[i].get();
    if (layer != nullptr && fx.layer_id[i] == layer_id && layer->dirty != 0) return layer->current();
  }
  return nullptr;
}

}  // namespace ss::game::field
