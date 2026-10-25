// The services the event-script VM calls (vm_services.h). Wired services call the
// real subsystem (and fall back to the stand-in behaviour when it has not been created,
// as in the VM's unit tests); the rest are stand-ins that log once and return a value
// that lets scripts run on. Every call reaches the trace sink (vm_svc_set_trace).
#include "game/field/vm_services.h"
#include "game/field/worldmap.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>

#include "game/boot/pad.h"
#include "game/boot/sound.h"
#include "game/field/actors.h"
#include "game/field/afterimage.h"
#include "game/field/bg.h"
#include "game/field/callways.h"
#include "game/field/voice_queue.h"
#include "game/field/camera.h"
#include "game/field/collision.h"
#include "game/field/effects.h"
#include "game/field/characters.h"
#include "game/field/encounter.h"
#include "game/field/fade.h"
#include "game/field/field_movie.h"
#include "game/field/field_sub_overlays.h"
#include "game/field/load_queue.h"
#include "game/field/map.h"
#include "game/field/menu_camp.h"
#include "game/field/message.h"
#include "game/field/music.h"
#include "game/field/notice.h"
#include "game/field/scene_renderer.h"
#include "game/field/screen.h"
#include "game/field/special.h"
#include "game/field/stash.h"
#include "game/field/script_vm.h"
#include "game/field/sprite_pack.h"
#include "runtime/runtime.h"

namespace ss::game::field {

VmStandinStats g_vm_standins;

namespace {

VmTraceSink g_sink = nullptr;

#if defined(__GNUC__)
__attribute__((format(printf, 2, 3)))
#endif
void note(const char* name, const char* fmt, ...) {
  ++g_vm_standins.calls;
  g_vm_standins.last = name;
  runtime::Runtime::get().log_once(std::string("vm_svc:") + name,
                                   std::string("field VM: service not wired yet: ") + name);
  if (g_sink == nullptr) return;
  char args[256];
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(args, sizeof args, fmt, ap);
  va_end(ap);
  g_sink(name, args);
}

#define SVC(...) note(&__func__[7], __VA_ARGS__)

// Wired services only report to the trace sink.
#if defined(__GNUC__)
__attribute__((format(printf, 2, 3)))
#endif
void trace(const char* name, const char* fmt, ...) {
  char args[256];
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(args, sizeof args, fmt, ap);
  va_end(ap);
  if (g_sink != nullptr) g_sink(name, args);
  // SS_TRACE: the events that line the port up with reference captures (fades,
  // windows, notices, map loads), stamped with the vblank count.
  static const char* const kMilestones[] = {"fade_push", "msg_window_open", "msg_window_open_default",
                                            "msg_window_close", "notice_show", "field_load_map"};
  for (const char* m : kMilestones) {
    if (std::strcmp(name, m) == 0) {
      runtime::Runtime::get().trace((std::string("vm ") + name + "(" + args + ")").c_str());
      break;
    }
  }
}
#define TRACE(...) trace(&__func__[7], __VA_ARGS__)

MsgWindow* window_at(int n) { return msg_system() != nullptr ? msg_window(n) : nullptr; }
static_assert(sizeof(MapFiles) == 17 * sizeof(s16), "MapFiles mirrors the s16 files[17] array");

// Stand-ins of reads (and of writes whose arguments add nothing to a trace) only log.
void note_once(const char* name) {
  ++g_vm_standins.calls;
  g_vm_standins.last = name;
  runtime::Runtime::get().log_once(std::string("vm_svc:") + name,
                                   std::string("field VM: service not wired yet: ") + name);
}
#define LOG_ONCE() note_once(&__func__[7])

// Where a string argument points in the current script's text (for traces).
long text_offset(const u8* text) {
  return g_script_vm != nullptr && g_script_vm->text != nullptr ? static_cast<long>(text - g_script_vm->text) : -1;
}

// What the stand-ins remember so that scripts see their own writes.
struct StubActor {
  bool exists = false;
  s32 x = 0, y = 0, z = 0;
  s32 angle = 0, anim = 0, layer = 0, view_x = 0, view_y = 0, offset48 = 0;
  u32 flags = 0;
  s32 talk = -1, pick = -1;
};
struct StubState {
  StubActor actors[0xC2];
  int menu_request = 0;
  int preload_pending[2] = {};
  int voice_cache_state = 0;
  bool overlay_object = false;
};
StubState g_stub;

StubActor* actor(int slot) { return slot >= 0 && slot < 0xC2 ? &g_stub.actors[slot] : nullptr; }

}  // namespace

void vm_svc_set_trace(VmTraceSink sink) { g_sink = sink; }
void vm_svc_reset() { g_stub = StubState{}; }

// ---- Field program ------------------------------------------------------------------
int vm_svc_field_update_state() {
  const Field* f = field_program();
  return f != nullptr ? f->state : 0;
}
void vm_svc_field_load_map(int map, int keep_position, int flags) {
  if (Field* f = field_program(); f != nullptr && load_queue() != nullptr) {
    TRACE("map=%d keep=%d flags=%d", map, keep_position, flags);
    field_load_map(f, map, keep_position != 0, flags);  // re-runs vm_init through the map hooks
    return;
  }
  SVC("map=%d keep=%d flags=%d", map, keep_position, flags);
  // The real loader re-runs vm_init for the new map, which clears the wait state.
  if (g_script_vm != nullptr) g_script_vm->wait_state = 0;
}
void vm_svc_field_load_entry(int entry, u8* done, int a3) {
  if (Field* f = field_program(); f != nullptr && load_queue() != nullptr) {
    TRACE("entry=%d a3=%d", entry, a3);
    field_load_entry(f, entry, done, a3);
    return;
  }
  SVC("entry=%d a3=%d", entry, a3);
  if (done != nullptr) *done = 1;  // host loads complete at once
}
int vm_svc_field_require_sprite_pack(int entry) {
  if (Field* f = field_program(); f != nullptr && load_queue() != nullptr) {
    TRACE("entry=%d", entry);
    return field_require_sprite_pack(f, entry);
  }
  SVC("entry=%d", entry);
  return 1;
}
void vm_svc_reload_sprite_pack(int entry) {
  if (Field* f = field_program(); f != nullptr && load_queue() != nullptr) {
    TRACE("entry=%d", entry);
    field_load_entry(f, entry);
    while (field_require_sprite_pack(f, entry) != 1) load_queue()->update();
    return;
  }
  SVC("entry=%d", entry);
}
int vm_svc_sprite_slot_entry(int slot) {
  if (slot < 0 || slot >= 16) return 0;
  const SpritePackSlot& p = sprite_pack_slots()[slot];
  return p.data.empty() ? 0 : p.id;
}
void vm_svc_field_prompt_disc_change(int disc, int a2) { field_prompt_disc_change(field_program(), disc, a2); }
void vm_svc_field_free_ot_layers() {
  if (scene_renderer() != nullptr) {
    TRACE("%s", "");
    field_free_ot_layers();
    return;
  }
  SVC("%s", "");
}
void vm_svc_exit_to_program_teardown() {
  if (!field_run_section6_program()) SVC("%s", "");  // the credits, on map 581
}
int vm_svc_field_install_music(int entry) {
  if (Field* f = field_program(); f != nullptr && globals().sound != nullptr) {
    TRACE("entry=%d", entry);
    return field_install_music(f, entry);
  }
  SVC("entry=%d", entry);
  return 1;
}
void vm_svc_music_free_buffers() { field_music_free_buffers(); }
void vm_svc_map_resolve_entries(s16 files[17], int map, int x, int z) {
  if (field_program() != nullptr) {
    TRACE("map=%d x=%d z=%d", map, x, z);
    map_resolve_entries(reinterpret_cast<MapFiles*>(files), map, static_cast<u32>(x), static_cast<u32>(z));
    return;
  }
  SVC("map=%d x=%d z=%d", map, x, z);
  for (int i = 0; i < 17; ++i) files[i] = -1;
  files[1] = 0xC26;
  files[2] = static_cast<s16>(0xC87 + map);
  files[3] = 0xC27;
  files[5] = 0xC1D;
  files[16] = 6;
}
void vm_svc_screen_set_clear_colour(int r, int g, int b) {
  if (field_program() != nullptr) {
    TRACE("%d,%d,%d", r, g, b);
    screen_set_clear_colour(field_screen(), static_cast<u8>(r), static_cast<u8>(g), static_cast<u8>(b));
    return;
  }
  SVC("%d,%d,%d", r, g, b);
}
const u8* vm_svc_map_scene() { return map_data().scene.empty() ? nullptr : map_data().scene.data(); }
int vm_svc_scene_scale() { return map_data().actor_scale; }
int vm_svc_zone_cell_at(int actor, int sx, int sy) {
  if (!map_data().scene.empty()) return zone_cell_at_xy(field_actor(actor), sx, sy);
  LOG_ONCE();
  return -1;
}
int vm_svc_zone_test_point(int actor, const u8* item, int sx, int sy, int y) {
  if (!map_data().scene.empty()) {
    const s16 xy[2] = {static_cast<s16>(sx), static_cast<s16>(sy)};
    return zone_test_point(field_actor(actor), item, xy, y) ? 1 : 0;
  }
  LOG_ONCE();
  return 0;
}
const u8* vm_svc_ui_atlas() {
  if (globals().ui_atlas != nullptr) return globals().ui_atlas;  // 0x80075328
  static const u8 zero[0x200] = {};
  return zero;
}
// g_frame_counter 0x80075324: vblanks since field_construct (vsync_counter_tick 0x800521e8).
int vm_svc_frame_counter() { return static_cast<int>(runtime::Runtime::get().vblanks() - globals().vblank_base); }
int vm_svc_pad_button_icon_offset(int buttons) { return pad_button_icon_offset(static_cast<u32>(buttons)); }
void vm_svc_set_scene_loaded(int v) {
  TRACE("%d", v);
  bg_globals().draw_enabled = static_cast<u8>(v);  // 0x80075354
  g_fade_targets.scene_loaded = static_cast<u8>(v);
}
void vm_svc_set_field_active(int v) {
  TRACE("%d", v);
  globals().field_active = static_cast<u8>(v);
}
void vm_svc_set_encounter_7535c(int v) {
  TRACE("%d", v);
  bg_globals().colour_speed = v;  // 0x8007535c: BG colour-record speed, percent
}
void vm_svc_pad_vibrate(int port, int small, int large) {
  TRACE("%d,%d,%d", port, small, large);
  if (globals().pad != nullptr) pad_vibrate(globals().pad, port, small, large);
}

// ---- Actors -------------------------------------------------------------------------
// Wired to the ActorSystem (actors.h); the stand-ins remain for when it has not been
// created (VM unit tests).
namespace {
ActorSystem* sys() { return actor_system(); }
Actor* real_actor(int slot) { return sys() != nullptr ? field_actor(slot) : nullptr; }
}  // namespace

bool vm_svc_actor_exists(int slot) {
  if (sys() != nullptr) return field_actor(slot) != nullptr;
  LOG_ONCE();
  const StubActor* a = actor(slot);
  return a != nullptr && a->exists;
}
void vm_svc_actor_get_position(int slot, s32* x, s32* y, s32* z) {
  if (sys() != nullptr) {
    const Actor* r = field_actor(slot);
    *x = r != nullptr ? r->x : 0;
    *y = r != nullptr ? r->y : 0;
    *z = r != nullptr ? r->z : 0;
    return;
  }
  LOG_ONCE();
  const StubActor* a = actor(slot);
  *x = a != nullptr ? a->x : 0;
  *y = a != nullptr ? a->y : 0;
  *z = a != nullptr ? a->z : 0;
}
void vm_svc_actor_set_position(int slot, s32 x, s32 y, s32 z) {
  if (sys() != nullptr) {
    TRACE("slot=%d %d,%d,%d", slot, x, y, z);
    if (Actor* r = field_actor(slot)) r->x = x, r->y = y, r->z = z;
    return;
  }
  SVC("slot=%d %d,%d,%d", slot, x, y, z);
  if (StubActor* a = actor(slot)) a->x = x, a->y = y, a->z = z;
}
int vm_svc_actor_get_anim(int slot) {
  if (sys() != nullptr) return field_actor(slot) != nullptr ? field_actor(slot)->anim : 0;
  LOG_ONCE();
  return actor(slot) ? actor(slot)->anim : 0;
}
void vm_svc_actor_set_16(int slot, int v) {
  if (sys() != nullptr) {
    TRACE("slot=%d %d", slot, v);
    if (Actor* r = field_actor(slot)) {
      r->model = static_cast<s16>(v);
      r->resolved_for = 0xFFFF;
    }
    return;
  }
  SVC("slot=%d %d", slot, v);
}
int vm_svc_actor_get_angle(int slot) {
  if (sys() != nullptr) return field_actor(slot) != nullptr ? field_actor(slot)->angle : 0;
  LOG_ONCE();
  return actor(slot) ? actor(slot)->angle : 0;
}
int vm_svc_actor_get_layer(int slot) {
  if (sys() != nullptr) return field_actor(slot) != nullptr ? field_actor(slot)->layer : 0;
  LOG_ONCE();
  return actor(slot) ? actor(slot)->layer : 0;
}
void vm_svc_actor_set_layer(int slot, int v) {
  if (sys() != nullptr) {
    TRACE("slot=%d %d", slot, v);
    if (Actor* r = field_actor(slot)) r->layer = static_cast<u16>(v);
    return;
  }
  SVC("slot=%d %d", slot, v);
  if (StubActor* a = actor(slot)) a->layer = v;
}
s32 vm_svc_actor_get_offset48(int slot) {
  if (sys() != nullptr) return field_actor(slot) != nullptr ? field_actor(slot)->lift : 0;
  LOG_ONCE();
  return actor(slot) ? actor(slot)->offset48 : 0;
}
void vm_svc_actor_set_offset48(int slot, s32 v) {
  if (sys() != nullptr) {
    TRACE("slot=%d %d", slot, v);
    if (Actor* r = field_actor(slot)) r->lift = v;
    return;
  }
  SVC("slot=%d %d", slot, v);
  if (StubActor* a = actor(slot)) a->offset48 = v;
}
u32 vm_svc_actor_get_flags(int slot) {
  if (sys() != nullptr) return field_actor(slot) != nullptr ? field_actor(slot)->flags : 0;
  LOG_ONCE();
  return actor(slot) ? actor(slot)->flags : 0;
}
int vm_svc_actor_get_talk_entry(int slot) {
  if (sys() != nullptr) return field_actor(slot) != nullptr ? field_actor(slot)->talk_entry : -1;
  LOG_ONCE();
  return actor(slot) ? actor(slot)->talk : -1;
}
int vm_svc_actor_get_pickpocket_entry(int slot) {
  if (sys() != nullptr) return field_actor(slot) != nullptr ? field_actor(slot)->pickpocket_entry : -1;
  LOG_ONCE();
  return actor(slot) ? actor(slot)->pick : -1;
}
void vm_svc_actor_set_speed(int slot, s32 a, s32 b) {
  if (sys() != nullptr) {
    TRACE("slot=%d %d,%d", slot, a, b);
    if (Actor* r = field_actor(slot)) r->scale_x = a, r->scale_y = b;
    return;
  }
  SVC("slot=%d %d,%d", slot, a, b);
}
int vm_svc_actor_get_view_x(int slot) {
  if (sys() != nullptr) return field_actor(slot) != nullptr ? field_actor(slot)->screen_x : 0;
  LOG_ONCE();
  return actor(slot) ? actor(slot)->view_x : 0;
}
int vm_svc_actor_get_view_y(int slot) {
  if (sys() != nullptr) return field_actor(slot) != nullptr ? field_actor(slot)->screen_y : 0;
  LOG_ONCE();
  return actor(slot) ? actor(slot)->view_y : 0;
}
void vm_svc_actor_set_view_pos(int slot, int x, int y) {
  if (sys() != nullptr) {
    TRACE("slot=%d %d,%d", slot, x, y);
    if (Actor* r = field_actor(slot)) r->screen_x = static_cast<s16>(x), r->screen_y = static_cast<s16>(y);
    return;
  }
  SVC("slot=%d %d,%d", slot, x, y);
  if (StubActor* a = actor(slot)) a->view_x = x, a->view_y = y;
}
void vm_svc_actor_set_7f(int slot, int v) {
  TRACE("slot=%d %d", slot, v);
  if (Actor* r = real_actor(slot)) r->blend = static_cast<u8>(v);
}
void vm_svc_actor_set_8c(int slot, int v) {
  TRACE("slot=%d %d", slot, v);
  if (Actor* r = real_actor(slot)) r->spin_angle = static_cast<s16>(v);
}
void vm_svc_actor_set_9c(int slot, int a, int b) {
  TRACE("slot=%d %d,%d", slot, a, b);
  if (Actor* r = real_actor(slot)) r->shadow_w = static_cast<s16>(a), r->shadow_h = static_cast<s16>(b);
}
void vm_svc_actor_set_a0(int slot, int v) {
  TRACE("slot=%d %d", slot, v);
  if (Actor* r = real_actor(slot)) r->spin_speed = static_cast<s16>(v);
}
int vm_svc_actor_get_screen_y(int slot) {
  if (sys() != nullptr) return field_actor(slot) != nullptr ? field_actor(slot)->quad[1] : 0;
  LOG_ONCE();
  return 0x78;
}
s32 vm_svc_actor_get_talk_radius(int slot) {
  if (sys() != nullptr) return field_actor(slot) != nullptr ? field_actor(slot)->collide_radius : 0;
  LOG_ONCE();
  return 0;
}
bool vm_svc_actor_has_attachment(int slot) {
  if (sys() != nullptr) return field_actor(slot) != nullptr && field_actor(slot)->emote != nullptr;
  LOG_ONCE();
  return false;
}
int vm_svc_actor_get_3e1(int slot) {
  if (sys() != nullptr) return field_actor(slot) != nullptr ? field_actor(slot)->walked_last : 0;
  LOG_ONCE();
  return 0;
}
void vm_svc_actor_create(int slot, int model, s32 x, s32 y, s32 z, int angle, int param, int talk_entry,
                         int pickpocket_entry, u32 flags) {
  if (sys() != nullptr) {
    TRACE("slot=%d model=%d pos=%d,%d,%d angle=%d param=%d talk=%d pick=%d flags=%#x", slot, model, x, y, z, angle,
          param, talk_entry, pickpocket_entry, flags);
    const s32 pos[3] = {x, y, z};
    actor_create(sys(), slot, model, pos, angle, param, talk_entry, pickpocket_entry, flags);
    return;
  }
  SVC("slot=%d model=%d pos=%d,%d,%d angle=%d param=%d talk=%d pick=%d flags=%#x", slot, model, x, y, z, angle, param,
      talk_entry, pickpocket_entry, flags);
  if (StubActor* a = actor(slot)) {
    *a = StubActor{};
    a->exists = true;
    a->x = x << 12;
    a->y = y << 12;
    a->z = z << 12;
    a->angle = angle * 4096 / 360;
    a->talk = static_cast<s16>(talk_entry);
    a->pick = static_cast<s16>(pickpocket_entry);
  }
}
void vm_svc_actor_create_or_place(int a0, int a1, int a2, int a3, int a4) {
  if (sys() != nullptr) {
    TRACE("%d,%d,%d,%d,%d", a0, a1, a2, a3, a4);
    actor_create_or_place(sys(), a0, a1, a2, a3, a4);
    return;
  }
  SVC("%d,%d,%d,%d,%d", a0, a1, a2, a3, a4);
}
void vm_svc_actor_delete_slot(int slot) {
  if (sys() != nullptr) {
    TRACE("slot=%d", slot);
    actor_delete_slot(sys(), slot);
    return;
  }
  SVC("slot=%d", slot);
  if (slot == -1) {
    for (StubActor& a : g_stub.actors) a.exists = false;
  } else if (StubActor* a = actor(slot)) {
    a->exists = false;
  }
}
void vm_svc_actor_queue_delete(int slot) {
  if (sys() != nullptr) {
    TRACE("slot=%d", slot);
    actor_queue_delete(field_actor(slot));
    return;
  }
  SVC("slot=%d", slot);
  if (StubActor* a = actor(slot)) a->exists = false;
}
// The command pushers take a null actor as the original does (actor_cmd_has_room).
#define ACTOR_CMD(fmt, call, ...)            \
  TRACE("slot=%d " fmt, slot, __VA_ARGS__); \
  if (sys() != nullptr) call
void vm_svc_actor_cmd_wait(int slot, int frames) {
  ACTOR_CMD("%d", actor_cmd_wait(field_actor(slot), static_cast<s16>(frames)), frames);
}
void vm_svc_actor_cmd_wait_move(int slot) { ACTOR_CMD("%s", actor_cmd_wait_move(field_actor(slot)), ""); }
void vm_svc_actor_cmd_wait_move_b(int slot) { ACTOR_CMD("%s", actor_cmd_wait_move_b(field_actor(slot)), ""); }
void vm_svc_actor_cmd_wait_turn(int slot) { ACTOR_CMD("%s", actor_cmd_wait_turn(field_actor(slot)), ""); }
void vm_svc_actor_cmd_turn_to(int slot, int a, int b, int c) {
  ACTOR_CMD("%d,%d,%d", actor_cmd_turn_to(field_actor(slot), static_cast<s16>(a), static_cast<s16>(b), static_cast<s16>(c)),
            a, b, c);
}
void vm_svc_actor_cmd_turn_by(int slot, int a, int b, int c) {
  ACTOR_CMD("%d,%d,%d", actor_cmd_turn_by(field_actor(slot), static_cast<s16>(a), static_cast<s16>(b), static_cast<s16>(c)),
            a, b, c);
}
void vm_svc_actor_cmd_turn_to_actor(int slot, int a, int b, int c) {
  ACTOR_CMD("%d,%d,%d",
            actor_cmd_turn_to_actor(field_actor(slot), static_cast<s16>(a), static_cast<s16>(b), static_cast<s16>(c)), a,
            b, c);
}
void vm_svc_actor_cmd_turn_to_point(int slot, int a, int b, int c, int d) {
  ACTOR_CMD("%d,%d,%d,%d",
            actor_cmd_turn_to_point(field_actor(slot), static_cast<s16>(a), static_cast<s16>(b), static_cast<s16>(c),
                                    static_cast<s16>(d)),
            a, b, c, d);
}
void vm_svc_actor_cmd_move_to_xz(int slot, int a, int b, int c) {
  ACTOR_CMD("%d,%d,%d",
            actor_cmd_move_to_xz(field_actor(slot), static_cast<s16>(a), static_cast<s16>(b), static_cast<s16>(c)), a, b,
            c);
}
void vm_svc_actor_cmd_move_to(int slot, int a, int b, int c, int d) {
  ACTOR_CMD("%d,%d,%d,%d",
            actor_cmd_move_to(field_actor(slot), static_cast<s16>(a), static_cast<s16>(b), static_cast<s16>(c),
                              static_cast<s16>(d)),
            a, b, c, d);
}
void vm_svc_actor_cmd_move_to_far(int slot, int x, int y, int z, int speed) {
  ACTOR_CMD("%d,%d,%d speed=%d", actor_cmd_move_to_far(field_actor(slot), x, y, z, static_cast<s16>(speed)), x, y, z,
            speed);
}
void vm_svc_actor_cmd_move_by_xz(int slot, int a, int b, int c) {
  ACTOR_CMD("%d,%d,%d",
            actor_cmd_move_by_xz(field_actor(slot), static_cast<s16>(a), static_cast<s16>(b), static_cast<s16>(c)), a, b,
            c);
}
void vm_svc_actor_cmd_move_by(int slot, int a, int b, int c, int d) {
  ACTOR_CMD("%d,%d,%d,%d",
            actor_cmd_move_by(field_actor(slot), static_cast<s16>(a), static_cast<s16>(b), static_cast<s16>(c),
                              static_cast<s16>(d)),
            a, b, c, d);
}
void vm_svc_actor_cmd_move_dir(int slot, int a, int b, int c) {
  ACTOR_CMD("%d,%d,%d",
            actor_cmd_move_dir(field_actor(slot), static_cast<s16>(a), static_cast<s16>(b), static_cast<s16>(c)), a, b,
            c);
}
void vm_svc_actor_cmd_move_dir_rel(int slot, int a, int b, int c) {
  ACTOR_CMD("%d,%d,%d",
            actor_cmd_move_dir_rel(field_actor(slot), static_cast<s16>(a), static_cast<s16>(b), static_cast<s16>(c)), a,
            b, c);
}
void vm_svc_actor_cmd_set_anim(int slot, int anim) {
  ACTOR_CMD("%d", actor_cmd_set_anim(field_actor(slot), static_cast<s16>(anim)), anim);
}
void vm_svc_actor_cmd_wait_anim(int slot, int a, int b) {
  ACTOR_CMD("%d,%d", actor_cmd_wait_anim(field_actor(slot), static_cast<s16>(a), static_cast<s16>(b)), a, b);
}
void vm_svc_actor_cmd_signal(int slot, int v) {
  ACTOR_CMD("%d", actor_cmd_signal(field_actor(slot), static_cast<s16>(v)), v);
}
void vm_svc_actor_cmd_emote(int slot, int a, int b, int c) {
  ACTOR_CMD("%d,%d,%d",
            actor_cmd_emote(field_actor(slot), static_cast<s16>(a), static_cast<s16>(b), static_cast<s16>(c)), a, b, c);
}
void vm_svc_actor_cmd_clear_emote(int slot) { ACTOR_CMD("%s", actor_cmd_clear_emote(field_actor(slot)), ""); }
void vm_svc_actor_delete_attachment(int slot) {
  TRACE("slot=%d", slot);
  if (Actor* r = real_actor(slot)) actor_delete_attachment(r);
}
void vm_svc_actor_queue_move_to(int slot, int a, int b, int c, int d, int e) {
  ACTOR_CMD("%d,%d,%d,%d,%d",
            actor_queue_move_to(field_actor(slot), static_cast<s16>(a), static_cast<s16>(b), static_cast<s16>(c),
                                static_cast<s16>(d), static_cast<s16>(e)),
            a, b, c, d, e);
}
void vm_svc_actor_queue_move_by(int slot, int a, int b, int c, int d, int e) {
  ACTOR_CMD("%d,%d,%d,%d,%d",
            actor_queue_move_by(field_actor(slot), static_cast<s16>(a), static_cast<s16>(b), static_cast<s16>(c),
                                static_cast<s16>(d), static_cast<s16>(e)),
            a, b, c, d, e);
}
void vm_svc_actor_queue_fade_colour(int slot, int r, int g, int b, int frames, int mode) {
  ACTOR_CMD("rgb=%d,%d,%d frames=%d mode=%d",
            actor_queue_fade_colour(field_actor(slot), static_cast<s16>(r), static_cast<s16>(g), static_cast<s16>(b),
                                    static_cast<s16>(frames), static_cast<s16>(mode)),
            r, g, b, frames, mode);
}
void vm_svc_actor_queue_wait_fade(int slot) { ACTOR_CMD("%s", actor_queue_wait_fade(field_actor(slot)), ""); }
#undef ACTOR_CMD
int vm_svc_actor_is_busy(int slot) {
  if (sys() != nullptr) return actor_is_busy(field_actor(slot)) ? 1 : 0;
  return 0;  // idle
}
void vm_svc_actor_push_state(int slot) {
  TRACE("slot=%d", slot);
  if (sys() != nullptr) actor_push_state(field_actor(slot));
}
void vm_svc_actor_push_state_and_turn(int slot, int angle, int mode) {
  TRACE("slot=%d angle=%d mode=%d", slot, angle, mode);
  if (sys() != nullptr) actor_push_state_and_turn(field_actor(slot), angle, mode);
}
void vm_svc_actor_pop_state(int slot, int mode) {
  TRACE("slot=%d mode=%d", slot, mode);
  if (sys() != nullptr) actor_pop_state(field_actor(slot), mode);
}
void vm_svc_actor_stop(int slot) {
  TRACE("slot=%d", slot);
  if (sys() != nullptr) actor_stop(field_actor(slot));
}
void vm_svc_actor_set_anim(int slot, int anim) {
  if (sys() != nullptr) {
    TRACE("slot=%d %d", slot, anim);
    actor_set_anim(field_actor(slot), static_cast<s16>(anim));
    return;
  }
  SVC("slot=%d %d", slot, anim);
  if (StubActor* a = actor(slot)) a->anim = anim;
}
void vm_svc_actor_mark_dirty(int slot) {
  if (sys() != nullptr) actor_mark_dirty(field_actor(slot));
}
void vm_svc_actor_set_flags(int slot, int word, u32 v) {
  if (sys() != nullptr) {
    TRACE("slot=%d word=%d %#x", slot, word, v);
    actor_set_flags(field_actor(slot), static_cast<s16>(word), v);
    return;
  }
  SVC("slot=%d word=%d %#x", slot, word, v);
  if (StubActor* a = actor(slot); a != nullptr && word == 0) a->flags = v;
}
void vm_svc_actor_or_flags(int slot, int word, u32 bits) {
  if (sys() != nullptr) {
    TRACE("slot=%d word=%d %#x", slot, word, bits);
    actor_or_flags(field_actor(slot), static_cast<s16>(word), bits);
    return;
  }
  SVC("slot=%d word=%d %#x", slot, word, bits);
  if (StubActor* a = actor(slot); a != nullptr && word == 0) a->flags |= bits;
}
void vm_svc_actor_and_flags(int slot, int word, u32 mask) {
  if (sys() != nullptr) {
    TRACE("slot=%d word=%d %#x", slot, word, mask);
    actor_and_flags(field_actor(slot), static_cast<s16>(word), mask);
    return;
  }
  SVC("slot=%d word=%d %#x", slot, word, mask);
  if (StubActor* a = actor(slot); a != nullptr && word == 0) a->flags &= mask;
}
void vm_svc_actor_set_special(int slot, int a, int b) {
  TRACE("slot=%d %d,%d", slot, a, b);
  if (Actor* r = real_actor(slot)) actor_set_patrol_emote(r, static_cast<u8>(a), static_cast<s16>(b));
}
void vm_svc_actor_sys_mark_all_dirty() {
  if (sys() != nullptr) actor_sys_mark_all_dirty(sys());
}
void vm_svc_actor_sys_reset_sprite_cache() {
  if (sys() != nullptr) actor_sys_reset_sprite_cache(sys());
}
void vm_svc_afterimage_attach(int slot, int a1, int a2, int a3, int a4, int a5) {
  TRACE("slot=%d %d,%#x,%d,%d,%d", slot, a1, a2, a3, a4, a5);
  // afterimage_create (0x8005ee9c) takes a5 too but never reads it.
  if (Actor* a = field_actor(slot)) afterimage_attach(a, a1, static_cast<u32>(a2), a3, a4);
}
void vm_svc_afterimage_detach(int slot) {
  TRACE("slot=%d", slot);
  if (Actor* a = field_actor(slot)) afterimage_detach(a);
}

// ---- Message windows, notice banner ------------------------------------------------
void vm_svc_msg_window_open(int x, int y, int w, int h, const u8* text, int window, int a7) {
  if (MsgSystem* msg = msg_system()) {
    TRACE("rect=%d,%d,%d,%d text=+%#lx window=%d", x, y, w, h, text_offset(text), window);
    msg_window_open(msg, x, y, w, h, text, window, a7);
    return;
  }
  SVC("rect=%d,%d,%d,%d text=+%#lx window=%d", x, y, w, h, text_offset(text), window);
}
void vm_svc_msg_window_open_default(const u8* text, int window, int a3) {
  if (MsgSystem* msg = msg_system()) {
    TRACE("text=+%#lx window=%d", text_offset(text), window);
    msg_window_open_default(msg, text, window, a3);
    return;
  }
  SVC("text=+%#lx window=%d", text_offset(text), window);
}
void vm_svc_msg_window_close(int window) {
  if (MsgSystem* msg = msg_system()) {
    TRACE("window=%d", window);
    msg_window_close(msg, window);
    return;
  }
  SVC("window=%d", window);
}
int vm_svc_msg_window_status(int window) {
  MsgSystem* msg = msg_system();
  return msg != nullptr ? msg_window_status(msg, window) : -1;  // stand-in: closed
}
bool vm_svc_msg_window_exists(int window) { return window_at(window) != nullptr; }
int vm_svc_msg_window_get_state(int window) { return msg_window_get_state(window_at(window)); }
void vm_svc_msg_window_push_move(int window, int x, int y, int frames) {
  TRACE("window=%d %d,%d frames=%d", window, x, y, frames);
  if (MsgWindow* w = window_at(window)) msg_window_push_move(w, x, y, static_cast<u16>(frames));
}
void vm_svc_msg_window_line_pos(int window, int line, s32 pos[2], s32* width, s32* height) {
  if (MsgWindow* w = window_at(window)) {
    u32 end_x = 0;
    msg_window_line_pos(w, line, pos, &end_x, height);
    *width = static_cast<s32>(end_x);
    return;
  }
  pos[0] = pos[1] = 0;
  *width = 0;
  *height = 0;
}
void vm_svc_msg_window_set_anchor(int window, int on) {
  TRACE("window=%d %d", window, on);
  msg_window_set_anchor(window_at(window), on);
}
int vm_svc_msg_window_line_height(int window) {
  const MsgWindow* w = window_at(window);
  return w != nullptr ? w->line_height : 13;
}
void vm_svc_msg_window_set_24(int window, int v) {
  TRACE("window=%d %d", window, v);
  if (MsgWindow* w = window_at(window)) w->justify = static_cast<u8>(v);
}
void vm_svc_msg_window_set_frame(int window, int frame, int body) {
  if (MsgWindow* w = window_at(window)) {
    w->draw_frame = static_cast<u8>(frame);
    w->draw_body = static_cast<u8>(body);
  }
}
void vm_svc_msg_window_set_hold(int window, int v) {
  if (MsgWindow* w = window_at(window)) w->hold = static_cast<u8>(v);
}
void vm_svc_msg_window_set_cancel(int window, int v) {
  if (MsgWindow* w = window_at(window)) w->cancel_closes = static_cast<u8>(v);
}
void vm_svc_msg_window_set_13b(int window, int v) {
  TRACE("window=%d %d", window, v);
  if (MsgWindow* w = window_at(window)) w->fade_in = static_cast<u8>(v);
}
void vm_svc_msg_window_set_close_flag(int window, int v) {
  TRACE("window=%d %d", window, v);
  if (MsgWindow* w = window_at(window)) w->show_prompt = static_cast<u8>(v);
}
void vm_svc_msg_window_request_close(int window) {
  TRACE("window=%d", window);
  if (MsgWindow* w = window_at(window)) w->force_advance = 1;
}
void vm_svc_msg_window_set_advance(int window, int v) {
  TRACE("window=%d %d", window, v);
  if (MsgWindow* w = window_at(window)) w->button_advance = static_cast<u8>(v);
}
void vm_svc_msg_window_set_wrap(int window, int v) {
  TRACE("window=%d %d", window, v);
  if (MsgWindow* w = window_at(window)) w->word_wrap = static_cast<u8>(v);
}
void vm_svc_msg_set_default_rect(int x, int y, int w, int h) {
  if (MsgSystem* msg = msg_system()) {
    msg->default_x = x;
    msg->default_y = y;
    msg->default_w = w;
    msg->default_h = h;
  }
}
void vm_svc_msg_set_default_x(int v) {
  if (MsgSystem* msg = msg_system()) msg->default_x = v;
}
void vm_svc_msg_set_default_y(int v) {
  if (MsgSystem* msg = msg_system()) msg->default_y = v;
}
void vm_svc_msg_set_default_size(int w, int h) {
  TRACE("%d,%d", w, h);
  if (MsgSystem* msg = msg_system()) {
    msg->default_w = w;
    msg->default_h = h;
  }
}
void vm_svc_notice_show(const u8* text, int hold, int window, int top) {
  if (Notice* n = g_notice_object()) {
    TRACE("text=+%#lx hold=%d window=%d top=%d", text_offset(text), hold, window, top);
    notice_show(n, text, hold, window, top);
    return;
  }
  SVC("text=+%#lx hold=%d window=%d top=%d", text_offset(text), hold, window, top);
}
void vm_svc_notice_dismiss() {
  TRACE("%s", "");
  if (Notice* n = g_notice_object()) notice_dismiss(n);
}

// ---- Screen fade --------------------------------------------------------------------
void vm_svc_fade_push(int a, int b, int c, int d) {
  if (ScreenFade* f = field_fade()) {
    TRACE("%d,%d,%d,%d", a, b, c, d);
    fade_push(f, static_cast<s16>(a), static_cast<s16>(b), static_cast<s16>(c), static_cast<s16>(d));
    return;
  }
  SVC("%d,%d,%d,%d", a, b, c, d);
}
int vm_svc_fade_is_active(int param) {
  ScreenFade* f = field_fade();
  return f != nullptr && fade_is_active(f, param) ? 1 : 0;  // stand-in: done
}
void vm_svc_fade_capture_screen() {
  if (ScreenFade* f = field_fade()) {
    TRACE("%s", "");
    fade_capture_screen(f);
    return;
  }
  SVC("%s", "");
}

// ---- Camera script and BG renderer -------------------------------------------------
void vm_svc_camscript_push_bg(int a0, int a1, int a2, int a3, int a4, int a5, int a6) {
  if (CamScript* c = camscript()) {
    TRACE("%d,%d,%d,%d,%d,%d,%d", a0, a1, a2, a3, a4, a5, a6);
    camscript_push_bg(c, a0, a1, a2, a3, a4, a5, a6);
    return;
  }
  SVC("%d,%d,%d,%d,%d,%d,%d", a0, a1, a2, a3, a4, a5, a6);
}
void vm_svc_camscript_push_point(int a0, int a1, int a2, int a3, int a4, int a5, int a6, int a7) {
  if (CamScript* c = camscript()) {
    TRACE("%d,%d,%d,%d,%d,%d,%d,%d", a0, a1, a2, a3, a4, a5, a6, a7);
    camscript_push_point(c, a0, a1, a2, a3, a4, a5, a6, a7);
    return;
  }
  SVC("%d,%d,%d,%d,%d,%d,%d,%d", a0, a1, a2, a3, a4, a5, a6, a7);
}
void vm_svc_camscript_push_actor(int a0, int a1, int a2, int a3, int a4, int a5) {
  if (CamScript* c = camscript()) {
    TRACE("%d,%d,%d,%d,%d,%d", a0, a1, a2, a3, a4, a5);
    camscript_push_actor(c, a0, a1, a2, a3, a4, a5);
    return;
  }
  SVC("%d,%d,%d,%d,%d,%d", a0, a1, a2, a3, a4, a5);
}
bool vm_svc_camscript_done() {
  if (const CamScript* c = camscript()) return camscript_busy(c) == 0;
  LOG_ONCE();
  return true;
}
void vm_svc_bg_get_view_origin(s16* x, s16* y) {
  *x = 0;
  *y = 0;
  if (BgRenderer* bg = bg_renderer(); bg != nullptr && !map_data().scene.empty()) {
    s16 xy[2];
    bg_get_view_origin(bg, xy);
    *x = xy[0];
    *y = xy[1];
  }
}
void vm_svc_bg_set_target(int x, int y) {
  TRACE("%d,%d", x, y);
  if (BgRenderer* bg = bg_renderer()) {
    bg->focus_x = static_cast<s16>(x);
    bg->focus_y = static_cast<s16>(y);
    bg_update_scroll(bg);
  }
}
void vm_svc_bg_set_114(int a, int b) {
  TRACE("%d,%d", a, b);
  if (BgRenderer* bg = bg_renderer()) {
    bg->proportional_x = static_cast<s16>(a);
    bg->proportional_y = static_cast<s16>(b);
  }
}
void vm_svc_bg_set_121(int v) {
  TRACE("%d", v);
  if (BgRenderer* bg = bg_renderer()) bg->shake = static_cast<u8>(v);
}
void vm_svc_bg_set_122(int v) {
  TRACE("%d", v);
  if (BgRenderer* bg = bg_renderer()) bg->vertical_scroll = static_cast<u8>(v);
}
void vm_svc_bg_set_124(int v) {
  TRACE("%d", v);
  if (BgRenderer* bg = bg_renderer()) bg->forced_x = static_cast<s16>(v);
}
void vm_svc_bg_set_126(int v) {
  TRACE("%d", v);
  if (BgRenderer* bg = bg_renderer()) bg->forced_y = static_cast<s16>(v);
}
void vm_svc_bg_restore_tint() {
  TRACE("%s", "");
  if (BgRenderer* bg = bg_renderer()) bg_enable_tint(bg, 0);
}
int vm_svc_bg_anim_get_frame(int a) {
  TRACE("%d", a);
  BgRenderer* bg = bg_renderer();
  return bg != nullptr ? bg_anim_get_frame(bg, a) : 0;
}
void vm_svc_bg_anim_set_frame(int a, int b) {
  TRACE("%d,%d", a, b);
  if (BgRenderer* bg = bg_renderer()) bg_anim_set_frame(bg, a, b);
}
void vm_svc_bg_anim_play(int a, int b) {
  TRACE("%d,%d", a, b);
  if (BgRenderer* bg = bg_renderer()) bg_anim_play(bg, a, b);
}
int vm_svc_bg_anim_get_state(int a) {
  TRACE("%d", a);
  BgRenderer* bg = bg_renderer();
  return bg != nullptr ? bg_anim_get_state(bg, a) : 0;
}
int vm_svc_walk_test_obstacles(int slot, s32 x, s32 y, s32 z) {
  if (const Actor* a = real_actor(slot); a != nullptr && !map_data().scene.empty()) {
    const s32 pos[3] = {x, y, z};
    u32 hit;
    return walk_test_obstacles(a, pos, &hit) ? 1 : 0;
  }
  LOG_ONCE();
  return 0;
}
void vm_svc_set_bg_tint(int r, int g, int b) {
  TRACE("%d,%d,%d", r, g, b);
  g_fade_targets.bg_tint[0] = static_cast<s16>(r);  // 0x80075aba..0x80075abe
  g_fade_targets.bg_tint[1] = static_cast<s16>(g);
  g_fade_targets.bg_tint[2] = static_cast<s16>(b);
}
void vm_svc_set_bg_tint_active(int v) {
  TRACE("%d", v);
  g_fade_targets.bg_tint_active = static_cast<u8>(v);  // 0x80075356
}

// ---- Sound and music ----------------------------------------------------------------
// The boot sound driver (System + 0x5aa4); null in the VM's unit tests.
int vm_svc_sound_bank_busy() {
  Sound* s = globals().sound;
  return s != nullptr ? sound_bank_busy(s) : 0;
}
int vm_svc_sound_play_sfx(int id, int pan, int volume) {
  if (Sound* s = globals().sound) {
    TRACE("id=%d pan=%d volume=%#x", id, pan, volume);
    return sound_play_sfx(s, id, pan, volume);
  }
  SVC("id=%d pan=%d volume=%#x", id, pan, volume);
  return 0;
}
int vm_svc_sound_effect_playing(int slot) {
  Sound* s = globals().sound;
  return s != nullptr ? static_cast<int>(sound_effect_playing(s, slot)) : 0;
}
void vm_svc_sound_release_clip(int slot) {
  TRACE("%d", slot);
  if (Sound* s = globals().sound) sound_release_clip(s, slot);
}
void vm_svc_sound_start_clip(int slot, int volume, int pan, int pitch, int flag) {
  TRACE("%d,%d,%d,%d,%d", slot, volume, pan, pitch, flag);
  // Effect slots are 1-8. The original has no range check: slot 0 (ext 0xcb's calls, see
  // script_ext.cpp) reads Sound +0x10f8 (the reverb depth) as the busy word and at most
  // writes over its copy of the common SPU attributes, which is never applied again; the
  // port has nothing before effects[0], so it skips any slot outside 1-8.
  Sound* s = globals().sound;
  if (s == nullptr || slot < 1 || slot > static_cast<int>(std::size(s->effects))) return;
  sound_start_clip(s, slot, volume, pan, pitch, flag);
}
void vm_svc_sound_play_sequence() {
  TRACE("%s", "");
  if (Sound* s = globals().sound; s != nullptr && !field_music_sequence().empty())
    sound_play_sequence(s, field_music_sequence().data());
}
void vm_svc_sound_stop() {
  TRACE("%s", "");
  if (Sound* s = globals().sound) sound_stop(s);
}
void vm_svc_sound_play_sequence_fade(int a, int b) {
  TRACE("%d,%d", a, b);
  if (Sound* s = globals().sound; s != nullptr && !field_music_sequence().empty())
    sound_resume_sequence(s, field_music_sequence().data(), a, b);
}
void vm_svc_sound_fade_out(int frames) {
  TRACE("%d", frames);
  if (Sound* s = globals().sound) sound_fade_out(s, frames);
}
void vm_svc_sound_fade_in(int frames) {
  TRACE("%d", frames);
  if (Sound* s = globals().sound) sound_fade_in_all(s, frames);
}
void vm_svc_sound_fade_out_effects(int frames) {
  TRACE("%d", frames);
  if (Sound* s = globals().sound) sound_fade_out_effects(s, frames);
}
void vm_svc_sound_set_tempo(int v) {
  TRACE("%d", v);
  if (Sound* s = globals().sound) sound_set_tempo(s, v);
}
void vm_svc_sound_stop_voices(int mask) {
  TRACE("%#x", mask);
  if (Sound* s = globals().sound) sound_end_loops(s, mask);
}
int vm_svc_sound_state_busy() {
  Sound* s = globals().sound;
  return s != nullptr ? sound_music_playing(s) : 0;  // sound_state +0x20 bit 0
}
int vm_svc_callways_song_active() { return callways_song_active(callways()); }
void vm_svc_callways_stop_song(int a) { callways_stop_song(callways(), a != 0); }
void vm_svc_sound_bank_holder_alloc() {
  if (globals().sound != nullptr && globals().handoff != nullptr) {
    TRACE("%s", "");
    sound_holder_save();
    return;
  }
  SVC("%s", "");
}
void vm_svc_sound_bank_holder_free() {
  if (globals().sound != nullptr && globals().handoff != nullptr) {
    TRACE("%s", "");
    sound_holder_restore();
    return;
  }
  SVC("%s", "");
}

// ---- Battle start, voices, preloads, loader ----------------------------------------
// The voice queue runs with a field (VM unit tests have no sound driver).
static bool voice_live() { return field_program() != nullptr && globals().sound != nullptr && globals().party != nullptr; }
void vm_svc_voice_slot_invalidate(int slot) {
  if (voice_live()) voice_slot_invalidate(slot);
  else SVC("%d", slot);
}
void vm_svc_load_queue_set_streaming(int on) { load_queue_set_streaming(load_queue(), on != 0); }
int vm_svc_get_voice_cache_state() { return voice_stream().state; }
void vm_svc_set_voice_cache_state(int v) { voice_stream().state = v; }
void vm_svc_voice_queue_abort() {
  if (voice_live()) voice_queue_abort();
}
int vm_svc_voice_party_spu_needed() { return voice_live() ? voice_party_spu_needed() : 0; }
void vm_svc_voice_queue_reset() {
  if (voice_live()) voice_queue_reset();
}
int vm_svc_voice_queue_build_battle(int budget) { return voice_live() ? voice_queue_build_battle(budget) : budget; }
void vm_svc_set_voice_budget(int v) { voice_stream().budget = v; }
void vm_svc_set_force_streaming(int v) { globals().scripted_battle = static_cast<u8>(v); }  // 0x800756d8
void vm_svc_field_prepare_encounter(int a, int b) {
  if (Field* f = field_program(); f != nullptr && load_queue() != nullptr) {
    TRACE("%d,%d", a, b);
    field_prepare_encounter(f, a, b);
    return;
  }
  SVC("%d,%d", a, b);
}
int vm_svc_get_battle_preload_count() { return battle_preloads().count; }
int vm_svc_get_battle_preload_entry(int i) { return i >= 0 && i < 13 ? battle_preloads().entry[i] : 0; }
int vm_svc_get_battle_preload_size(int i) { return i >= 0 && i < 13 ? battle_preloads().bytes[i] : 0; }
// field_error_stub (0x80055da0): an error report left empty in this build. Its callers
// pass a code: 0x27 / 0x28 no party member (0x80052ff4), 0x7c out of memory (0x8004b760),
// 0x7f a battle preload failed (field_update, ext 0xb7).
void vm_svc_field_error_stub(int) {}
void vm_svc_field_start_battle(int r) {
  if (Field* f = field_program(); f != nullptr && load_queue() != nullptr) {
    TRACE("%d", r);
    field_start_battle(f, r);
    return;
  }
  SVC("%d", r);
}
bool vm_svc_battle_transition_done() {
  if (field_program() == nullptr) return true;
  const BattleTransition* t = battle_transition();
  return t == nullptr || t->done != 0;
}
void vm_svc_field_destroy_battle_transition() {
  TRACE("%s", "");
  field_destroy_battle_transition(field_program());
}
int vm_svc_get_battle_ready() { return field_program() == nullptr || field_battle_program_loaded() ? 1 : 0; }
void vm_svc_field_exit_to_program(int mode) {
  if (Field* f = field_program(); f != nullptr && load_queue() != nullptr) {
    TRACE("%d", mode);
    field_exit_to_program(f, mode);
  }
  SVC("%d", mode);
  // The real call never returns (the battle program replaces the field); coming back,
  // vm_init / vm_restore_state start from a clear wait state.
  if (g_script_vm != nullptr) g_script_vm->wait_state = 0;
}
// The preload table works without a load queue only in tests (nothing is read).
int vm_svc_preload_release(int kind) { return preload_release(kind); }
void vm_svc_preload_release_kind2_except(int tag) { preload_release_kind2_except(tag); }
int vm_svc_preload_start(int entry, int kind, int tag, int extra) {
  if (load_queue() != nullptr) {
    TRACE("entry=%d kind=%d tag=%d extra=%d", entry, kind, tag, extra);
    return preload_start(entry, kind, tag, extra);
  }
  SVC("entry=%d kind=%d tag=%d extra=%d", entry, kind, tag, extra);
  return 1;
}
int vm_svc_preload_count_done(int kind) { return preload_count_done(kind); }
void vm_svc_preload_prune_for_map(int map) { preload_prune_for_map(map); }
int vm_svc_preload_set_tag(int entry, int tag) { return preload_set_tag(entry, tag); }
// 0x8007533c / 0x8007533d (battle data preloaded / requested).
int vm_svc_get_preload_release_pending(int i) {
  if (i == 0) return globals().battle_preloaded;
  if (i == 1) return globals().battle_preload_requested;
  return 0;
}
void vm_svc_set_preload_release_pending(int i, int v) {
  if (i == 0) globals().battle_preloaded = static_cast<u8>(v);
  if (i == 1) globals().battle_preload_requested = static_cast<u8>(v);
}
void vm_svc_get_map_preload(int* x, int* z, int* pending) {
  *x = globals().map_preload_x;
  *z = globals().map_preload_z;
  *pending = globals().map_preload_pending;
}
void vm_svc_set_map_preload(int x, int z, int pending) {
  globals().map_preload_x = x;
  globals().map_preload_z = z;
  globals().map_preload_pending = static_cast<u8>(pending);
}
void vm_svc_loader_cancel() {
  TRACE("%s", "");
  if (globals().loader != nullptr) globals().loader->cancel();
}
int vm_svc_loader_entry_size(int entry) { return globals().loader != nullptr ? globals().loader->entry_size(entry) : 0; }
int vm_svc_loader_poll() { return globals().loader != nullptr ? globals().loader->poll() : 0; }
// loader_select_entry_at (boot 0x80011988): seeks to the entry (plus a1 sectors) and
// returns 1 unless a read is in progress. The host loader is never busy, and the field
// movie (field_movie.cpp) reads its entry itself, so only the result matters.
int vm_svc_loader_11988(int a1, int entry) {
  TRACE("%d,%d", a1, entry);
  return 1;
}
void vm_svc_set_loader_cancel_flag(int v) { field_movie_set_stop(v); }  // 0x800762ff: stops the movie

// ---- Field menus --------------------------------------------------------------------
void vm_svc_set_menu_request(int menu) {
  TRACE("%d", menu);
  field_menu_request(menu);  // g_menu_request 0x80076196 (menu_camp.cpp)
}
void vm_svc_set_menu_param(int param) { field_menu_set_param(param); }  // g_menu_param 0x8007619a
int vm_svc_menu_request_pending() { return field_menu_request_pending(); }
void vm_svc_set_global_76194(int v) { bg_globals().animated_on = static_cast<u8>(v); }  // 0x80076194
void vm_svc_set_global_76195(int v) { bg_globals().animate_all = static_cast<u8>(v); }  // 0x80076195

// ---- Party / character helpers ------------------------------------------------------
// The party queries run through a temporary menu session (menu_camp.cpp); without a
// field (VM unit tests) there are no party blocks and the stand-ins answer.
static bool party_live() {
  return field_program() != nullptr && globals().config != nullptr && globals().party != nullptr &&
         globals().records != nullptr && globals().inventory != nullptr;
}
int vm_svc_party_get_specialty_level(int slot, int n) {
  if (party_live()) return menu_member_specialty_level(slot, n);
  LOG_ONCE();
  return 0;
}
int vm_svc_party_get_super_specialty6() {
  if (party_live()) return menu_super_specialty_6_level();
  LOG_ONCE();
  return 0;
}
void vm_svc_party_member_clear_bond(int slot) {
  TRACE("%d", slot);
  if (party_live()) menu_member_clear_bond(slot);
}
void vm_svc_party_slot_init(int slot, int character_id) {
  TRACE("slot=%d id=%d", slot, character_id);
  FieldGlobals& g = globals();
  if (g.party == nullptr || g.records == nullptr || slot < 0 || slot > 7) return;
  party_slot_init(g.party + slot * 0x60, g.records + slot * 0xD0, character_id);  // 0x8007a20c
}
void vm_svc_party_move_character_to_slot(int character, int slot) {
  TRACE("%d,%d", character, slot);
  if (globals().party != nullptr && globals().records != nullptr) party_move_character_to_slot(character, slot);
}
void vm_svc_party_swap_characters(int a, int b) {
  TRACE("%d,%d", a, b);
  if (globals().party != nullptr && globals().records != nullptr) party_swap_characters(a, b);
}

// ---- Map objects / sprite packs -----------------------------------------------------
void vm_svc_sprite_packs_tint(int a, int b, int c) {
  if (field_program() != nullptr) sprite_packs_tint(field_program(), a, b, c);
  else SVC("%d,%d,%d", a, b, c);
}
void vm_svc_sprite_packs_restore_palettes() {
  if (field_program() != nullptr) sprite_packs_restore_palettes(field_program());
  else SVC("%s", "");
}
void vm_svc_sprite_anim_set_frame_bytes(int a, int b, int count, const s32* values) {
  TRACE("%d,%d count=%d", a, b, count);
  int v[16];
  const int n = count < 16 ? count : 16;
  for (int i = 0; i < n; ++i) v[i] = values[i];
  sprite_anim_set_frame_bytes(field_program(), a, b, n, v);
}
void vm_svc_sprite_anim_set_frame_byte(int a, int b, int c, int d) {
  TRACE("%d,%d,%d,%d", a, b, c, d);
  sprite_anim_set_frame_byte(field_program(), a, b, c, static_cast<u8>(d));
}
void vm_svc_sprite_anim_set_byte2(int a, int b, int c) {
  TRACE("%d,%d,%d", a, b, c);
  sprite_anim_set_mode(field_program(), a, b, static_cast<u8>(c));
}
void vm_svc_sprite_anim_set_loop_frame(int a, int b, int c) {
  TRACE("%d,%d,%d", a, b, c);
  sprite_anim_set_loop_frame(field_program(), a, b, c);
}

// ---- Overlay code -------------------------------------------------------------------
void vm_svc_glow_create(int a0, int a1, int a2, int a3) {
  if (field_screen() != nullptr) {
    TRACE("%d,%d,%d,%d", a0, a1, a2, a3);
    glow_spawn(a0, a1, a2, a3);
    return;
  }
  SVC("%d,%d,%d,%d", a0, a1, a2, a3);
}
// The 0x801f9800 overlays (field_sub_overlays.cpp) run when the map carries their
// section (VM unit tests load none).
static bool sub_live(int section) {
  return field_program() != nullptr && (field_map_sub_sections() & (1u << section)) != 0;
}
void vm_svc_overlay_801f9fe8(ScriptCtx* ctx) {
  if (sub_live(8)) {
    path_start(ctx);  // #3108
    TRACE("%d,%d,%d,...", g_vm_args[0], g_vm_args[1], g_vm_args[2]);
    return;
  }
  vm_fetch_args(ctx, 10);
  SVC("%d,%d,%d,...", g_vm_args[0], g_vm_args[1], g_vm_args[2]);
}
void vm_svc_overlay_801fa120(ScriptCtx* ctx) {
  if (sub_live(8)) {
    path_add_point(ctx);  // #3108
    TRACE("%d,%d,%d", g_vm_args[0], g_vm_args[1], g_vm_args[2]);
    return;
  }
  vm_fetch_args(ctx, 3);
  SVC("%d,%d,%d", g_vm_args[0], g_vm_args[1], g_vm_args[2]);
}
void vm_svc_overlay_801f9bb4(ScriptCtx* ctx) {
  if (sub_live(9)) {
    mirror_add(ctx);  // #3106
    TRACE("%d,%d,%d,%d,%d", g_vm_args[0], g_vm_args[1], g_vm_args[2], g_vm_args[3], g_vm_args[4]);
    return;
  }
  vm_fetch_args(ctx, 5);
  SVC("%d,%d,%d,%d,%d", g_vm_args[0], g_vm_args[1], g_vm_args[2], g_vm_args[3], g_vm_args[4]);
}
void vm_svc_field_special_command(int a0) {
  TRACE("%d", a0);
  if (globals().game_state != nullptr) field_special_command(g_script_vm, a0);
}
void vm_svc_walk_set_group_enabled(int a0, int a1) {
  if (!map_data().scene.empty()) {
    TRACE("%d,%d", a0, a1);
    walk_set_group_enabled(a0, a1);
    return;
  }
  SVC("%d,%d", a0, a1);
}
void vm_svc_inventory_stash() {
  if (stash_table() != nullptr && globals().inventory != nullptr) {
    TRACE("%s", "");
    inventory_stash();
    return;
  }
  SVC("%s", "");
}
void vm_svc_inventory_unstash() {
  if (stash_table() != nullptr && globals().inventory != nullptr) {
    TRACE("%s", "");
    inventory_unstash();
    return;
  }
  SVC("%s", "");
}
void vm_svc_inventory_clear_all() {
  if (globals().inventory != nullptr) {
    TRACE("%s", "");
    inventory_clear_all();
    return;
  }
  SVC("%s", "");
}
// The particle spawners run once the field's SceneRenderer exists (it drives them).
namespace {
struct Vec4 {
  s32 v[4];
  explicit Vec4(const s32 p[3]) : v{p[0], p[1], p[2], 0} {}
};
bool fx_running() { return scene_renderer() != nullptr; }
}  // namespace
void vm_svc_fx_spawn_burst(int id, const s32 pos[3], int a4) {
  if (fx_running()) {
    TRACE("id=%d pos=%d,%d,%d frames=%d", id, pos[0], pos[1], pos[2], a4);
    fx_spawn_burst(static_cast<u8>(id), Vec4(pos).v, a4);
    return;
  }
  SVC("id=%d", id);
}
void vm_svc_fx_spawn_ring_textured(int id, const s32 pos[3], const s32 args[8]) {
  if (fx_running()) {
    TRACE("id=%d pos=%d,%d,%d %d,%d,%d,%d,%d,%d,%d,%d", id, pos[0], pos[1], pos[2], args[0], args[1], args[2], args[3],
          args[4], args[5], args[6], args[7]);
    fx_spawn_ring_textured(static_cast<u8>(id), Vec4(pos).v, args[0], args[1], args[2], args[3], args[4], args[5], args[6],
                           args[7]);
    return;
  }
  SVC("id=%d", id);
}
void vm_svc_fx_spawn_beam(int id, const s32 a[3], const s32 b[3], const s32 args[4]) {
  if (fx_running()) {
    TRACE("id=%d %d,%d,%d -> %d,%d,%d %#x,%#x,%d,%d", id, a[0], a[1], a[2], b[0], b[1], b[2], args[0], args[1], args[2],
          args[3]);
    fx_spawn_beam(static_cast<u8>(id), Vec4(a).v, Vec4(b).v, args[0], args[1], args[2], args[3]);
    return;
  }
  SVC("id=%d", id);
}
void vm_svc_fx_spawn_triangle(int id, const s32 a[3], const s32 b[3], const s32 c[3], const s32 args[5]) {
  if (fx_running()) {
    TRACE("id=%d %#x,%#x,%#x,%d,%d", id, args[0], args[1], args[2], args[3], args[4]);
    fx_spawn_triangle(static_cast<u8>(id), Vec4(a).v, Vec4(b).v, Vec4(c).v, args[0], args[1], args[2], args[3],
                      static_cast<s16>(args[4]));
    return;
  }
  SVC("id=%d", id);
}
void vm_svc_fx_spawn_orb(int id, const s32 pos[3], const s32 args[6]) {
  if (fx_running()) {
    TRACE("id=%d pos=%d,%d,%d %d,%d,%d,%d,%#x,%d", id, pos[0], pos[1], pos[2], args[0], args[1], args[2], args[3],
          args[4], args[5]);
    fx_spawn_orb(static_cast<u8>(id), Vec4(pos).v, args[0], args[1], args[2], args[3], args[4], args[5]);
    return;
  }
  SVC("id=%d", id);
}
void vm_svc_fx_spawn_streak(int id, const s32 a[3], const s32 b[3], int a7, int a8, int a9, int a10, int a11, int a12) {
  if (fx_running()) {
    TRACE("id=%d %d,%d,%d -> %d,%d,%d %#x,%#x,%d,%d,%d,%d", id, a[0], a[1], a[2], b[0], b[1], b[2], a7, a8, a9, a10,
          a11, a12);
    Vec4 to(b);
    fx_spawn_streak(static_cast<u8>(id), Vec4(a).v, to.v, a7, a8, a9, a10, a11, a12);
    return;
  }
  SVC("id=%d", id);
}
void vm_svc_fx_spawn_spark(int id, const s32 a[3], const s32 b[3], const s32 args[4]) {
  if (fx_running()) {
    TRACE("id=%d %#x,%#x,%d,%d", id, args[0], args[1], args[2], args[3]);
    fx_spawn_spark(static_cast<u8>(id), Vec4(a).v, Vec4(b).v, args[0], args[1], args[2], args[3]);
    return;
  }
  SVC("id=%d", id);
}
void vm_svc_fx_spawn_ring(int id, const s32 pos[3], const s32 args[8]) {
  if (fx_running()) {
    TRACE("id=%d pos=%d,%d,%d %d,%d,%d,%d,%#x,%#x,%d,%d", id, pos[0], pos[1], pos[2], args[0], args[1], args[2],
          args[3], args[4], args[5], args[6], args[7]);
    fx_spawn_ring(static_cast<u8>(id), Vec4(pos).v, args[0], args[1], args[2], args[3], args[4], args[5], args[6],
                  args[7]);
    return;
  }
  SVC("id=%d", id);
}
// #3109 (section 7, map 688): the TIME / PRESS gauge (field_sub_overlays.cpp).
int vm_svc_overlay_801f9fb8(int a0) {
  if (sub_live(7)) return item_record_byte_19(a0);
  SVC("%d", a0);
  return 0;
}
bool vm_svc_overlay_801f9800_exists() {
  if (sub_live(7)) return gauge_exists();
  LOG_ONCE();
  return g_stub.overlay_object;
}
void vm_svc_overlay_801f9800_create(int a1, int a2) {
  if (sub_live(7)) {
    TRACE("%d,%d", a1, a2);
    gauge_create(a1, a2);
    return;
  }
  SVC("%d,%d", a1, a2);
  g_stub.overlay_object = true;
}
void vm_svc_overlay_801f9800_destroy() {
  if (sub_live(7)) {
    TRACE("%s", "");
    gauge_destroy();
    return;
  }
  SVC("%s", "");
  g_stub.overlay_object = false;
}
int vm_svc_overlay_801f9f54() {
  if (sub_live(7)) return gauge_seconds_left();
  LOG_ONCE();
  return 0;
}
void vm_svc_overlay_801f9a64(int a0) {
  if (sub_live(12) && field_screen() != nullptr && globals().sound != nullptr) {
    TRACE("%d", a0);
    the_end(a0);  // #3105 (section 12, map 581): never returns
  }
  SVC("%d", a0);
}
// #3103, the movie player (field_movie.cpp), runs with a field (VM unit tests have no
// field program, loader, screen or sound driver).
static bool movie_live() {
  return field_program() != nullptr && globals().loader != nullptr && globals().sound != nullptr &&
         field_screen() != nullptr;
}
void vm_svc_overlay_801eac28(ScriptCtx* ctx, u32 instr) {
  if (movie_live()) {
    field_movie_preload(ctx, instr);
    TRACE("%d", g_vm_args[0]);
    return;
  }
  vm_fetch_args(ctx, ((instr >> 16) & 0x7F) == 0x16 ? 2 : 1);
  SVC("%d", g_vm_args[0]);
}
void vm_svc_overlay_801eaa30(ScriptCtx* ctx) {
  if (movie_live()) {
    field_movie_start(ctx);
    TRACE("%d,%d", g_vm_args[0], g_vm_args[1]);
    return;
  }
  vm_fetch_args(ctx, 2);
  SVC("%d,%d", g_vm_args[0], g_vm_args[1]);
}
int vm_svc_overlay_801ea374(ScriptCtx* ctx) {
  if (movie_live()) {
    TRACE("%d,%d", g_vm_args[0], g_vm_args[1]);
    return field_movie_sequence(ctx);
  }
  SVC("%s", "");
  return 0;
}
int vm_svc_overlay_801ea_loaded() { return field_sub_program() != nullptr ? 1 : 0; }
int vm_svc_overlay_801ea_value() { return field_movie_frame_count(); }

// ---- Other field globals ------------------------------------------------------------
void vm_svc_set_global_76224(int a, int b) {  // 0x80076224 / 0x80076226: forced BG parallax
  bg_globals().parallax_x = static_cast<s16>(a);
  bg_globals().parallax_y = static_cast<s16>(b);
}
// #3108's path globals (field_sub_overlays.cpp), set and read by field code.
void vm_svc_set_global_76220(int a, int b) { path_set_end_scripts(a, b); }
void vm_svc_set_global_7621d(int a, int b) { path_set_end_stops(a, b); }
int vm_svc_get_global_761ea() { return path_count(); }
int vm_svc_get_global_76214() { return path_anim_flag(); }
void vm_svc_set_global_76310(int v) { field_set_gauge_clock(v); }  // the query gauge's clock
void vm_svc_set_global_76278(int v) { globals().show_battle_results = static_cast<u8>(v); }
void vm_svc_worldmap_add_entry(const s32 args[14]) {
  TRACE("%d,%d", args[0], args[1]);
  world_add_light_region(args);  // a lighting region of the world map (0x8007470c / 0x80076284)
}

}  // namespace ss::game::field
