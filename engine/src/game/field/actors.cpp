#include "game/field/actors.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

#include "game/boot/system.h"
#include "game/field/actor_field.h"
#include "game/field/bg.h"
#include "game/field/camera.h"
#include "game/field/worldmap.h"
#include "game/field/collision.h"
#include "game/field/fade.h"
#include "game/field/field.h"
#include "game/field/map.h"
#include "game/field/scene_renderer.h"
#include "game/field/script_vm.h"
#include "game/field/vm_services.h"
#include "runtime/runtime.h"

namespace ss::game::field {
namespace {

Actor* g_field_actors[kFieldActorCount] = {};   // 0x80075360
ActorSystem* g_actor_sys = nullptr;             // 0x80075294
std::unique_ptr<ActorSystem> g_actor_sys_storage;
std::unique_ptr<u8[]> g_snapshot;               // 0x80075724
ActorHooks g_hooks;

s16 flag_s16(int offset) {
  s16 v;
  std::memcpy(&v, globals().field_flags + offset, 2);
  return v;
}
void set_flag_s16(int offset, s16 v) { std::memcpy(globals().field_flags + offset, &v, 2); }

const u8* scene() { return map_data().scene.empty() ? nullptr : map_data().scene.data(); }
s32 scene_s32(int offset) { return static_cast<s32>(read_u32(scene() + offset)); }
s16 scene_s16(int offset) { return static_cast<s16>(read_u16(scene() + offset)); }
u16 scene_u16(int offset) { return read_u16(scene() + offset); }

// The field class's actors (0x3fc bytes; the world map's 0x44c-byte class is not
// ported and the field class stands in for it).
Actor* new_field_actor() { return new Actor; }

// field_actors[slot]'s update with the player-control and camera-target arguments.
void update_slot(int slot) {
  Actor* a = g_field_actors[slot];
  bool control = false;
  if (flag_s16(0x10) == slot) control = globals().field_flags[0x26] == 0;
  static const int dbg = std::getenv("SS_DBG_SLOT") ? std::atoi(std::getenv("SS_DBG_SLOT")) : -1;
  if (slot == dbg)
    std::printf("[dbg %d] before ang %d turning %d tgt %d step %d frames %d frozen %d wait %d q %d/%d\n", slot, a->angle,
                a->turning, a->turn_target, a->turn_step, a->turn_frames, a->frozen, a->wait_state, a->cmd_read,
                a->cmd_write);
  actor_update(a, control ? 1 : 0, flag_s16(0xE) == slot ? 1 : 0);
  if (slot == dbg)
    std::printf("[dbg %d] after ang %d turning %d tgt %d step %d frames %d frozen %d wait %d\n", slot, a->angle,
                a->turning, a->turn_target, a->turn_step, a->turn_frames, a->frozen, a->wait_state);
}

}  // namespace

Actor** field_actors() { return g_field_actors; }
Actor* field_actor(int slot) {
  return static_cast<u32>(slot) < static_cast<u32>(kFieldActorCount) ? g_field_actors[slot] : nullptr;
}
int actor_slot(const Actor* a) {
  for (int i = 0; i < kFieldActorCount; ++i)
    if (g_field_actors[i] == a) return i;
  return -1;
}

ActorHooks& actor_hooks() { return g_hooks; }
u8* actor_snapshot() { return g_snapshot.get(); }

ActorSystem* actor_system() { return g_actor_sys; }

ActorSystem* actor_sys_create() {
  g_actor_sys_storage = std::make_unique<ActorSystem>();
  g_actor_sys = actor_sys_construct(g_actor_sys_storage.get());
  return g_actor_sys;
}

ActorSystem* actor_sys_construct(ActorSystem* s) {
  s->camera_rot.vz = 0;
  s->camera_rot.vy = 0;
  s->camera_rot.vx = 0;
  s->cache_a_y = 0;
  s->cache_a_x = 0;
  for (int i = 0x1F; i != -1; --i)
    for (int k = 0; k < 4; ++k) s->cache_a_rect[0x1F - i][k] = 0;
  s->cache_b_y = 0;
  s->cache_b_x = 0;
  for (int i = 0x1F; i != -1; --i)
    for (int k = 0; k < 4; ++k) s->cache_b_rect[0x1F - i][k] = 0;
  s->tint_on = 0;
  std::memset(g_field_actors, 0, sizeof(g_field_actors));
  actor_sys_reset_sprite_cache(s);
  g_actor_sys = s;
  task_register(s, 2);
  g_fade_targets.actors_set_brightness = &actor_sys_set_brightness;
  return s;
}

void actor_sys_set_brightness(int level) {
  ActorSystem* s = g_actor_sys;
  if (s == nullptr) return;
  s->tint_on = 1;
  actor_sys_mark_all_dirty(s);
  s->tint[0] = s->tint[1] = s->tint[2] = static_cast<u8>(level);
}

void actor_sys_destruct(ActorSystem* s, int flags) {
  for (Actor*& a : g_field_actors) {
    if (a != nullptr) actor_destroy(a, 3);
    a = nullptr;
  }
  task_unregister(s);
  if (g_actor_sys == s) g_actor_sys = nullptr;
  if ((flags & 1) && s == g_actor_sys_storage.get()) g_actor_sys_storage.reset();
}

void actor_sys_mark_all_dirty(ActorSystem*) {
  for (Actor* a : g_field_actors)
    if (a != nullptr) actor_mark_dirty(a);
}

void actor_sys_reset_sprite_cache(ActorSystem* s) {
  if (s == nullptr) return;
  for (int i = 0; i < 32; ++i) {
    s->cache_a_key[i] = 0;
    s->cache_b_key[i] = 0;
  }
  s->cache_a_x = 0x141;
  s->cache_b_x = 0x141;
  s->cache_a_next = 0;
  s->cache_a_y = 1;
  s->cache_a_shelf = 0;
  s->cache_b_next = 0;
  s->cache_b_y = 0x101;
  s->cache_b_shelf = 0;
}

void actor_create(ActorSystem*, int slot, int model, const s32 pos[3], int angle, int layer, int talk_entry,
                  int pickpocket_entry, u32 flags) {
  s16 talk = static_cast<s16>(talk_entry), pickpocket = static_cast<s16>(pickpocket_entry);
  if (talk == 0) talk = -1;
  if (pickpocket == 0) pickpocket = -1;
  Actor*& a = g_field_actors[slot];
  if (a != nullptr) actor_destroy(a, 3);
  if (globals().field_flags[0x68] != 0) {  // world map: the 0xc1e class (0x80082e5c)
    a = worldactor_construct_full(worldactor_new(), static_cast<s16>(model), pos, angle, layer, talk, pickpocket, flags);
    return;
  }
  a = fieldactor_construct_full(new_field_actor(), static_cast<s16>(model), pos, angle, layer, talk, pickpocket, flags);
}

void actor_create_or_place(ActorSystem*, int slot, int model, int x, int y, int layer) {
  Actor*& a = g_field_actors[slot];
  if (a == nullptr) {
    if (globals().field_flags[0x68] != 0) {  // world map: 0x80082dd4
      a = worldactor_construct_screen(worldactor_new(), static_cast<s16>(model), static_cast<s16>(x),
                                      static_cast<s16>(y), layer);
      return;
    }
    a = fieldactor_construct_screen(new_field_actor(), static_cast<s16>(model), static_cast<s16>(x), static_cast<s16>(y),
                                  layer);
  } else {
    actor_init_screen(a, static_cast<s16>(model), static_cast<s16>(x), static_cast<s16>(y), layer);
  }
}

void actor_sys_draw(ActorSystem* s) {
  for (int i = 0; i < 32; ++i) {
    if (s->cache_a_key[i] != 0 && s->cache_a_life[i] != 0) --s->cache_a_life[i];
    if (s->cache_b_key[i] != 0 && s->cache_b_life[i] != 0) --s->cache_b_life[i];
  }
  s->cache_b_cluts = 0;

  // The scene's colour cycle (scene +0x98 steps of {r, g, b, frames} at +0x9c; +0x99 = 1
  // ping-pongs). Storing a step writes all four bytes, the frame count over the timer,
  // as the original does.
  const u8* sc = scene();
  if (sc != nullptr && sc[0x98] != 0) {
    const s32 ticks = globals().frame_ticks;
    for (s32 t = 0; t < ticks; ++t) {
      const int count = sc[0x98];
      s32 sum = 0;
      bool found = false;
      int k = 0;
      auto take = [&](int step) {
        found = true;
        if (t == ticks - 1) {
          std::memcpy(s->light, sc + 0x9C + step * 4, 3);
          s->cycle_timer = sc[0x9C + step * 4 + 3];
        }
      };
      for (k = 0; k < count; ++k) {
        sum += sc[0x9F + k * 4];
        if (s->cycle_timer < sum) {
          take(k);
          break;
        }
      }
      if (k == count && sc[0x99] == 1) {
        for (int j = k - 2; j > 0; --j) {
          sum += sc[0x9F + j * 4];
          if (s->cycle_timer < sum) {
            take(j);
            break;
          }
        }
      }
      if (!found) {
        std::memcpy(s->light, sc + 0x9C, 3);
        s->cycle_timer = 0;
      }
      ++s->cycle_timer;
    }
  }

  // Every actor's primitives (effects first), then their sort keys.
  u8 order[kFieldActorCount];
  s32 key[kFieldActorCount];
  int n = 0;
  for (int i = 0x82; i < kFieldActorCount; ++i)
    if (g_field_actors[i] != nullptr) order[n++] = static_cast<u8>(i);
  for (int i = 0; i < 0x82; ++i)
    if (g_field_actors[i] != nullptr) order[n++] = static_cast<u8>(i);
  const bool by_y = globals().field_flags[0x80] != 0;
  for (int i = 0; i < n; ++i) {
    Actor* a = g_field_actors[order[i]];
    a->vtbl->draw(a, 1);
    a = g_field_actors[order[i]];
    key[i] = by_y ? -static_cast<s32>(a->screen_y) : a->depth;
  }
  // Selection sort: ascending key, then slot. Actors linked first end up last in
  // their OT entries, so the nearest one draws on top.
  for (int i = 0; i < n - 1; ++i) {
    int m = i;
    for (int j = i + 1; j < n; ++j)
      if (key[j] < key[m] || (key[m] == key[j] && order[j] < order[m])) m = j;
    if (m != i) {
      std::swap(order[i], order[m]);
      std::swap(key[i], key[m]);
    }
  }
  if (g_hooks.ot_layer_count == nullptr || g_hooks.ot_layer_count() == 0) {
    for (int i = 0; i < n; ++i) actor_link_prims(g_field_actors[order[i]], 0, 0xFFFFFFFF, nullptr);
  } else {
    // OT layers (0x800761a8..0x800761c4, scene_renderer.h): the layers the effects drew
    // into are spliced into the main OT at id + 0x11, and actors whose layer matches one
    // of their ids sort by depth into that layer's table.
    ot_layers_splice();
    for (int i = 0; i < n; ++i) {
      Actor* a = g_field_actors[order[i]];
      psyq::P_TAG* table = ot_layer_table_for(a->layer);
      actor_link_prims(a, 0, table != nullptr ? static_cast<u32>(a->layer) + 1 : 0xFFFFFFFF, table);
    }
    ot_layers_compact();
  }
  if (fieldactor_globals().children_pass) {
    for (int i = n - 1; i >= 0; --i) actor_link_prims(g_field_actors[order[i]], 1, 0xFFFFFFFF, nullptr);
  }
}

// SS_ACTOR_TRACE=1 prints the script actors' state every frame (for comparisons with
// RAM dumps of the original); SS_ACTOR_TRACE=2 also prints the VM's service calls.
void trace_vm_call(const char* service, const char* args) {
  std::printf("[vm] vblank %llu %s(%s)\n", static_cast<unsigned long long>(runtime::Runtime::get().vblanks()), service,
              args);
}

void trace_actors() {
  static const char* env = std::getenv("SS_ACTOR_TRACE");
  static const bool on = env != nullptr;
  if (!on) return;
  static bool sink = false;
  if (!sink && std::atoi(env) >= 2) {
    vm_svc_set_trace(trace_vm_call);
    sink = true;
  }
  const unsigned long long v = runtime::Runtime::get().vblanks();
  std::printf("[actors] vblank %llu ctrl %d cam %d lock %d held %04x run %04x dist %d\n", v, flag_s16(0x10),
              flag_s16(0xE), globals().field_flags[0x26], globals().held[0],
              globals().config != nullptr ? read_u16(globals().config + 2) : 0, static_cast<s32>(read_u32(globals().field_flags + 0x18)));
  for (int i = 0; i < 0x82; ++i) {
    const Actor* a = g_field_actors[i];
    if (a == nullptr) continue;
    std::printf("[actor %d] pos %d %d %d ang %d anim %d/%d frame %d cel %d mv %d wait %d q %d/%d scr %d %d depth %d layer %d"
                " quad %d %d flags %x\n",
                i, a->x, a->y, a->z, a->angle, a->anim, a->auto_anim, a->anim_frame, a->cel, a->move_mode, a->wait_state,
                a->cmd_read, a->cmd_write, a->screen_x, a->screen_y, a->depth, a->layer, a->quad[0], a->quad[1], a->flags);
  }
}

void actor_sys_update(ActorSystem* s) {
  FieldGlobals& g = globals();
  if (g.field_active == 0) return;
  trace_actors();
  if (g.field_flags[0x81] != 0) {
    for (int i = 0x1F; i >= 0; --i) s->cache_b_key[i] = 0;
    s->cache_b_x = 0x141;
    s->cache_b_next = 0;
    s->cache_b_y = 0x101;
    s->cache_b_shelf = 0;
  }
  if (flag_s16(0xE) == -1 && g.field_flags[0x68] == 0) bg_update_scroll(bg_renderer());
  if (g.disc_error == 0) {
    for (int i = 0; i < 0x82; ++i)
      if (g_field_actors[i] != nullptr) update_slot(i);
  }
  actor_sys_draw(s);
}

void ActorSystem::update() { actor_sys_update(this); }

void actor_sys_update_one(ActorSystem*, int slot) {
  if (slot < 0 || slot >= kFieldActorCount || g_field_actors[slot] == nullptr) return;
  update_slot(slot);
}

void actor_sys_setup_view(ActorSystem* sys) {
  if (sys == nullptr || scene() == nullptr) return;
  psyq::SetGeomOffset(scene_u16(0x34) >> 1, scene_u16(0x36) >> 1);
  psyq::SetGeomScreen(scene_u16(0x6A));
  sys->camera_rot.vy = scene_s16(0xDE);
  sys->camera_rot.vx = scene_s16(0xDC);
  sys->camera_rot.vz = static_cast<s16>(scene_s32(0x30));
  // The constant flip matrix (0x80072e2c): diag(1, -1, -1).
  psyq::MATRIX flip{};
  const u8* fm = field_data(0x80072E2C);
  for (int i = 0; i < 9; ++i) flip.m[i / 3][i % 3] = static_cast<s16>(read_u16(fm + i * 2));
  for (int i = 0; i < 3; ++i) flip.t[i] = static_cast<s32>(read_u32(fm + 0x14 + i * 4));
  auto inverse = [](s16 a) { return static_cast<s16>(a < 1 ? -a : 0x1000 - (a & 0xFFF)); };
  const s16 rx = inverse(sys->camera_rot.vx), ry = inverse(sys->camera_rot.vy), rz = inverse(sys->camera_rot.vz);
  psyq::MATRIX m = flip;
  sys->view = psyq::MATRIX{};
  sys->view.m[0][0] = sys->view.m[1][1] = sys->view.m[2][2] = 0x1000;
  psyq::RotMatrixZ(rz, &sys->view);
  psyq::RotMatrixX(rx, &sys->view);
  psyq::RotMatrixY(ry, &sys->view);
  psyq::MulMatrix(&m, &sys->view);
  sys->view = m;
  sys->view.t[0] = scene_s32(0x08);
  sys->view.t[1] = scene_s32(0x0C);
  sys->view.t[2] = scene_s32(0x10);

  psyq::MATRIX r;
  psyq::MATRIX base = flip;
  const s16 roll = sys->camera_rot.vz;
  sys->camera_rot.vz = 0;
  field_rot_matrix(&sys->camera_rot, &r);  // the field's own RotMatrix (0x8006e52c)
  sys->camera_rot.vz = roll;
  psyq::RotMatrixZ(roll, &r);
  psyq::MulMatrix(&r, &base);
  const psyq::VECTOR t{scene_s32(0x08) * -0x1000, scene_s32(0x0C) * -0x1000, scene_s32(0x10) * -0x1000, 0};
  psyq::TransMatrix(&r, &t);
  psyq::SetRotMatrix(&r);
  psyq::SetTransMatrix(&r);
  sys->camera_yaw = ratan2(scene_s32(0x10) - scene_s32(0x1C), scene_s32(0x08) - scene_s32(0x14)) & 0xFFF;
  sys->half_fov = static_cast<s16>(ratan2(scene_u16(0x34) >> 1, scene_u16(0x6A)));
  camera_reset();
  CameraBlock& cam = camera_block();
  cam.look_at = 1;
  for (int i = 0; i < 3; ++i) {
    cam.eye[i] = scene_s32(0x08 + i * 4) << 12;
    cam.target[i] = scene_s32(0x14 + i * 4) << 12;
  }
  camera_update_matrices();
}

void actor_sys_touch_cache_a(ActorSystem* s, int slot) {
  if (slot >= 0) s->cache_a_life[slot] = 2;
}

void actor_sys_touch_cache_b(ActorSystem* s, int slot) {
  if (slot >= 0) s->cache_b_life[slot] = 2;
}

void actor_sys_project_point(ActorSystem* s, s16 sx, s16 sy, s32 y, s32 out[3]) {
  psyq::PushMatrix();
  psyq::SetRotMatrix(&s->view);
  psyq::SetTransMatrix(&s->view);
  const s32 hw = static_cast<s32>(static_cast<u32>(scene_u16(0x34)) << 16);
  const s32 hh = static_cast<s32>(static_cast<u32>(scene_u16(0x36)) << 16);
  psyq::SVECTOR v{static_cast<s16>(sx - static_cast<s16>(((hw >> 16) - (hw >> 31)) >> 1)),
                  static_cast<s16>(sy - static_cast<s16>(((hh >> 16) - (hh >> 31)) >> 1)), scene_s16(0x6A), 0};
  psyq::VECTOR w{};
  s32 flag;
  psyq::RotTrans(&v, &w, &flag);
  out[1] = y;
  s32 f = 0;
  if (scene_s32(0x0C) != w.vy) f = ((y - w.vy) * 0x1000) / (scene_s32(0x0C) - w.vy);
  out[2] = ((f * (scene_s32(0x10) - w.vz)) >> 12) + w.vz;
  out[0] = ((f * (scene_s32(0x08) - w.vx)) >> 12) + w.vx;
  psyq::PopMatrix();
}

void actor_delete_slot(ActorSystem*, int slot) {
  if (static_cast<s16>(slot) == -1) {
    for (Actor*& a : g_field_actors) {
      if (a != nullptr) {
        actor_destroy(a, 3);
        a = nullptr;
      }
    }
  } else if (Actor*& a = g_field_actors[static_cast<s16>(slot)]; a != nullptr) {
    actor_destroy(a, 3);
    a = nullptr;
  }
}

void actor_delete(ActorSystem*, Actor* a) {
  for (int i = 0; i < kFieldActorCount; ++i) {
    if (g_field_actors[i] == nullptr || g_field_actors[i] != a) continue;
    actor_destroy(a, 3);
    g_field_actors[i] = nullptr;
    if (flag_s16(0x10) == i) set_flag_s16(0x10, -1);
    if (flag_s16(0xE) == i) set_flag_s16(0xE, -1);
  }
}

void actor_sys_save_all(ActorSystem*) {
  u8 slots[kFieldActorCount];
  u16 offsets[kFieldActorCount];
  int n = 0;
  int bytes = 0;
  for (int i = 0; i < kFieldActorCount; ++i) {
    if (g_field_actors[i] == nullptr) continue;
    slots[n] = static_cast<u8>(i);
    offsets[n] = static_cast<u16>(bytes);
    ++n;
    bytes += actor_state_size(g_field_actors[i]);
  }
  const int header = n * 4 + 4;
  const int total = bytes + header;
  g_snapshot = std::make_unique<u8[]>(static_cast<size_t>(total));
  u8* blob = g_snapshot.get();
  const u16 total16 = static_cast<u16>(total), n16 = static_cast<u16>(n);
  std::memcpy(blob, &total16, 2);
  std::memcpy(blob + 2, &n16, 2);
  for (int i = 0; i < n; ++i) {
    const u16 slot = slots[i];
    const u16 off = static_cast<u16>(offsets[i] + header);
    std::memcpy(blob + 4 + i * 2, &slot, 2);
    std::memcpy(blob + 4 + (n + i) * 2, &off, 2);
    actor_save_state(g_field_actors[slot], blob + off);
  }
}

void actor_sys_restore_all(ActorSystem*) {
  auto* blob = static_cast<u8*>(handoff_get(globals().handoff, 0xB));
  if (blob == nullptr) return;
  const u16 n = read_u16(blob + 2);
  const s32 zero[3] = {0, 0, 0};
  for (int i = 0; i < n; ++i) {
    const u16 slot = read_u16(blob + 4 + i * 2);
    Actor*& a = g_field_actors[slot];
    if (a == nullptr) {
      a = globals().field_flags[0x68] != 0 ? worldactor_construct_full(worldactor_new(), 0, zero, 0, 0, -1, -1, 0x1000F)
                                           : fieldactor_construct_full(new_field_actor(), 0, zero, 0, 0, -1, -1, 0x1000F);
    } else {
      actor_init(a, 0, zero, 0, 0, -1, -1, 0x1000F);
    }
    actor_load_state(a, blob + 4 + (read_u16(blob + 4 + (i + n) * 2) - 4), 1);
    if (globals().field_flags[0x68] == 0) walk_snap_to_floor(a);
  }
  handoff_clear(globals().handoff, 0xB);
  // The blob itself belongs to whoever stored it in the handoff (mem_free on the console).
}

s16 actor_screen_x(const Actor* a) { return a->screen_x; }
s16 actor_screen_y(const Actor* a) { return a->screen_y; }

}  // namespace ss::game::field
