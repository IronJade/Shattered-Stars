// field_update (0x8004c334) on the world map: the checks of the play state (state 0:
// the clock-driven event, getting off the bunny, boarding / landing the vehicle) and
// the states they lead to: 9-0xe (vehicle 1: boarding, rising, landing), 0x11-0x1a
// (the bunny: called from the camp menu (result 5), ridden with script flag 14, sent
// home), 0x1b-0x20 (the clock event and the camp menu's warp, result 4).
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "game/boot/pad.h"
#include "game/boot/sound.h"
#include "game/common/math.h"
#include "game/field/actor_base.h"
#include "game/field/actors.h"
#include "game/field/bg.h"
#include "game/field/message.h"
#include "game/field/fade.h"
#include "game/field/field.h"
#include "game/field/field_world.h"
#include "game/field/load_queue.h"
#include "game/field/map.h"
#include "game/field/music.h"
#include "game/field/post_battle.h"
#include "game/field/script_vm.h"
#include "game/field/worldmap.h"
#include "runtime/runtime.h"

namespace ss::game::field {
namespace {

FieldGlobals& fg() { return globals(); }
s32 rd32(const u8* p) {
  s32 v;
  std::memcpy(&v, p, 4);
  return v;
}
s16 rd16s(const u8* p) {
  s16 v;
  std::memcpy(&v, p, 2);
  return v;
}
void wr32(u8* p, s32 v) { std::memcpy(p, &v, 4); }
void wr16(u8* p, s16 v) { std::memcpy(p, &v, 2); }
u16 cfg(int i) { return read_u16(fg().config + i * 2); }  // config buttons: 0 action, 1 cancel, 2 menu, 3 hover
bool sflag(int byte, int bit) { return ((fg().script_flags[byte] >> bit) & 1) != 0; }
int lead_slot() { return fg().game_state[0x24]; }
Actor* leader() { return field_actor(lead_slot()); }
WorldActor* as_world(Actor* a) { return a != nullptr && is_world_actor(a) ? static_cast<WorldActor*>(a) : nullptr; }
void set_control(s16 v) { wr16(fg().field_flags + 0x10, v); }
s32 map_id() { return rd16s(fg().game_state + 0x1a); }
s16 world_music_entry() { return map_id() == 1 ? 0xa8a : 0xa89; }

// The camera's view direction in degrees for actor_cmd_turn_to.
s16 camera_degrees() {
  s32 d = world_camera_yaw() * 0x168;
  if (d < 0) d += 0xfff;
  return static_cast<s16>((d << 4) >> 16);
}

// field 0x80053c34: once the side load is in and no music waits in the load buffer, the
// side buffer becomes the load buffer and its music is installed. The port's
// field_install_music keeps the bank itself, so the "music waits" state is tracked here.
bool g_music_waiting = false;  // 0x80075738 != 0 (holding installed music)

void install_side_music(Field* f) {
  if (g_music_waiting || !field_side_load_done()) return;
  sound_stop(fg().sound);
  field_load_buffer() = std::move(field_side_buffer());
  field_side_buffer().clear();
  field_install_music(f, 0);
  g_music_waiting = true;
  field_side_clear_done();
}

// field 0x80053cb4: when installed music waits and the SPU is idle, play it (game_state
// +0x1c = entry) and free its buffers. 1 when started.
int play_installed_music(s16 entry) {
  FieldGlobals& g = fg();
  if (!g_music_waiting || sound_bank_busy(g.sound) == 1) return 0;
  sound_play_sequence(g.sound, field_music_sequence().data());
  field_music().pending = 1;
  wr16(g.game_state + 0x1c, entry);
  field_music_free_buffers();
  field_music().bank_kept = false;
  g_music_waiting = false;
  return 1;
}

// Music installed or playing: the original tests 0x80075738 / 0x80075740 / game_state +0x1c.
bool music_ready(s32 entry) {
  return g_music_waiting || field_music().bank_kept || rd16s(fg().game_state + 0x1c) == entry;
}

// States 0x13 / 0x19: the side load becomes the load buffer and its music is installed.
void install_side_buffer_music(Field* f) {
  sound_stop(fg().sound);
  field_load_buffer() = std::move(field_side_buffer());
  field_side_buffer().clear();
  field_install_music(f, 0);
}

// States 0x14 / 0x1a: the installed music starts.
void start_installed(s16 entry) {
  FieldGlobals& g = fg();
  sound_play_sequence(g.sound, field_music_sequence().data());
  field_music().pending = 1;
  if (entry != 0) wr16(g.game_state + 0x1c, entry);
  field_load_buffer().clear();
}

void set_y(Actor* a, s32 y) {
  if (a != nullptr) a->y = y;
}

// field 0x8004ac0c: frees every sprite-pack slot registered with 'id'.
void free_sprite_pack(s32 id) {
  SpritePackSlot* packs = sprite_pack_slots();
  for (int i = 0; i < 16; ++i)
    if (!packs[i].data.empty() && packs[i].id == id) packs[i] = SpritePackSlot{};
}

// ---- The courier (states 0x1b-0x20) ----
// A creature (kind game_state +0x288: 1-5) flies off with an order (the camp menu's
// result 4, which turns game_state +0x294 into the delivery time and arms script flag
// 13) and comes back with the goods once the play clock passes it (state 0 check).
// Field tables: 0x800744ea s16 sprite model per kind, 0x8007451c the kind's animations
// {leave, arrive, arrive early, hand over}.
s16 courier_model() {
  const u8* p = field_data(0x800744ea + fg().game_state[0x288] * 2u);
  return p != nullptr ? static_cast<s16>(read_u16(p)) : 0;
}
s16 courier_anim(int i) {
  const u8* p = field_data(0x8007451c + fg().game_state[0x288] * 4u);
  if (p == nullptr) return -1;
  const u8* list = field_data(read_u32(p) + static_cast<u32>(i) * 2);
  return list != nullptr ? static_cast<s16>(read_u16(list)) : -1;
}
// Frames of the flight: 0x14000 / field +0x5c + 20 (30 when that would be below 20).
s32 courier_frames() {
  const s32 q = 0x14000 / std::max<s32>(1, rd32(fg().field_flags + 0x5c));
  return q + 0x1e < 0x14 ? 0x1e : q + 0x14;
}
// The courier (actor 0xe) as a screen-space sprite at the leader's scale.
Actor* courier_create(s16 x, s16 y) {
  actor_create_or_place(actor_system(), 0xe, courier_model(), x, y, 0x100);
  Actor* c = field_actor(0xe);
  Actor* l = leader();
  if (c == nullptr) return nullptr;
  if (fg().field_flags[0x68] == 0) {
    const s32 depth = l != nullptr ? l->depth : 1;
    const s32 s = (static_cast<s32>(read_u16(map_data().scene.data() + 0x6a)) << 12) / std::max(1, depth << 2);
    c->scale_y = c->scale_x = s;
    actor_mark_dirty(c);
  } else {
    c->scale_y = c->scale_x = 0xe80;
    actor_mark_dirty(c);
    if (WorldActor* wc = as_world(c); wc != nullptr && as_world(l) != nullptr) wc->depth8 = as_world(l)->depth8;
  }
  return c;
}
// The BG pixel at the screen's top-left on field maps (bg_get_view_origin), 0 on the world map.
void view_origin(s16 out[2]) {
  out[0] = out[1] = 0;
  if (fg().field_flags[0x68] == 0)
    if (const BgRenderer* bg = bg_renderer()) bg_get_view_origin(bg, out);
}

}  // namespace

// overlay 0x80082b6c: 1 when (x, z) (map units; x == 10000000: the leader) is inside an
// active world-map trigger region (bounding box, then the four edge cross products).
int worldmap_trigger_test(ScriptCtx* ctx, s32 x, s32 z) {
  if (ctx == nullptr) return 0;
  if (x == 10000000) {
    const Actor* l = leader();
    if (l == nullptr) return 0;
    x = l->x >> 12;
    z = l->z >> 12;
  }
  for (int i = 0; i < 0x3a; ++i) {
    const Trigger& t = ctx->triggers[i];
    if (t.active == 0 || t.world_map == 0) continue;
    if (x < t.min_x || x > t.max_x || z < t.min_z || z > t.max_z) continue;
    const s32* c = t.corner;
    bool inside = true;
    for (int e = 0; e < 4 && inside; ++e) {
      const s32 ax = c[e * 2], az = c[e * 2 + 1];
      const s32 bx = c[((e + 1) % 4) * 2], bz = c[((e + 1) % 4) * 2 + 1];
      if ((bx - ax) * (z - az) - (bz - az) * (x - ax) >= 1) inside = false;
    }
    if (inside) return 1;
  }
  return 0;
}

// field_update state 0, after the post-battle check (the script thread idle, the leader
// in control): the clock event, getting off the bunny, boarding / landing the vehicle.
// True when the frame ends here (a state change, or the original's early return).
bool field_world_play_checks(Field* f) {
  FieldGlobals& g = fg();
  u8* gs = g.game_state;
  u8* flags = g.field_flags;
  // Debugging: SS_MENU_RESULT="VBLANK:N" acts as if the camp menu had returned N (4 warp,
  // 5 bunny call) at that vblank (the port's menu does not produce them yet).
  static const char* forced = std::getenv("SS_MENU_RESULT");
  if (forced != nullptr) {
    unsigned long long at = 0;
    int result = 0;
    if (std::sscanf(forced, "%llu:%d", &at, &result) == 2 && runtime::Runtime::get().vblanks() >= at) {
      forced = nullptr;
      field_world_menu_result(f, result);
      if (f->state != 0) return true;
    }
  }
  // The clock-driven event (script flag 13, not 20): once the play clock passes game_state
  // +0x294, side text 0xfc3 + game_state +0x288 and state 0x1b.
  if (sflag(1, 5) && !sflag(2, 4) && rd32(gs + 0x294) != 0 && static_cast<u32>(rd32(gs + 0x294)) < clock_seconds()) {
    field_side_load(static_cast<s16>(gs[0x288] + 0xfc3));
    set_control(-1);
    f->state = 0x1b;
    wr32(flags + 0x38, 1);
    wr32(flags + 0x3c, field_side_entry());
    return true;
  }
  // Off the bunny (script flag 14): the action button on walkable ground of terrain bit 4,
  // outside the world triggers. The bunny (actor 0xf, model 10000) hops away.
  if (sflag(1, 6) && (g.pressed[0] & cfg(0)) != 0 && (world().leader_hit.attr & 1) != 0 &&
      ((world().leader_hit.attr >> 4) & 1) != 0 && worldmap_trigger_test(g_script_vm, 10000000, 0) == 0) {
    const Actor* l = leader();
    if (l == nullptr) return false;
    const s32 pos[3] = {l->x >> 12, l->y >> 12, l->z >> 12};
    actor_create(actor_system(), 0xf, 10000, pos, 0, 0, 0xffff, 0xffff, 0x1000f);
    if (Actor* b = field_actor(0xf)) {
      actor_cmd_set_anim(b, 0xb5);
      actor_cmd_wait_anim(b, 1, 0);
      b->scale_y = 0x1400;
      b->scale_x = 0x1400;
      actor_mark_dirty(b);
      actor_or_flags(b, 0, 0x800000);
      if (WorldActor* wb = as_world(b)) wb->ot_bias = 10;
    }
    set_control(-1);
    f->state_timer = 0;
    sound_fade_out(g.sound, 0x3c);
    const s16 entry = world_music_entry();
    wr16(gs + 0x1c, entry);
    field_side_load(entry);
    f->state = 0x16;
    return true;
  }
  // The vehicle (script flag 11) while no fade holds the screen.
  if (flags[0x68] == 0 || !sflag(1, 3) || field_fade() == nullptr || field_fade()->hold != 0) return false;
  const u16 held = g.held[0];
  if ((held & cfg(0)) == 0 || sflag(1, 4) || sflag(1, 6)) {
    // Landing (the cancel button while flying): the eight directions 0x122 out must be
    // walkable and outside the triggers, then the eight 0x104 out (the first is where the
    // leader steps off, field +0x40).
    if ((g.pressed[0] & cfg(1)) == 0 || !sflag(1, 4)) return false;
    const int on_trigger = worldmap_trigger_test(g_script_vm, 10000000, 0);
    if ((world().leader_hit.attr & 1) == 0 || on_trigger != 0) return false;
    Actor* v = leader();
    if (v == nullptr) return false;
    const s32 pos[4] = {v->x, v->y, v->z, v->w};
    const s32 yaw = world_camera_yaw();
    s32 a = yaw;
    int i = 0;
    for (; i < 8; ++i, a += 0x200) {
      const s32 px = pos[0] + cos4096(static_cast<u32>(a)) * 0x122, pz = pos[2] + sin4096(static_cast<u32>(a)) * 0x122;
      TerrainHit hit;
      const s32 h = world_terrain_query(world_bg(), px, pz, &hit);
      if (h == -0x8000 || (hit.attr & 1) == 0) break;
      if (worldmap_trigger_test(g_script_vm, px >> 12, pz >> 12) != 0) break;
    }
    if (i != 8) return false;
    bool found = false;
    a = yaw;
    int k = 0;
    for (; k < 8; ++k, a += 0x200) {
      const s32 px = pos[0] + cos4096(static_cast<u32>(a)) * 0x104, pz = pos[2] + sin4096(static_cast<u32>(a)) * 0x104;
      TerrainHit hit;
      const s32 h = world_terrain_query(world_bg(), px, pz, &hit);
      if (h == -0x8000 || (hit.attr & 1) == 0 || worldmap_trigger_test(g_script_vm, px >> 12, pz >> 12) != 0) break;
      if (!found) {
        found = true;
        f->dismount[0] = px;
        f->dismount[1] = h << 12;
        f->dismount[2] = pz;
      }
    }
    if (k != 8) return false;
    WorldActor* wv = as_world(v);
    const s32 ground = wv != nullptr ? wv->ground : -0x8000;
    if (ground == -0x8000) return true;
    f->vehicle_b = ground << 12;
    f->vehicle_a = (pos[1] >> 12) - ground;
    actor_cmd_turn_to(v, camera_degrees(), 4, 0);
    actor_cmd_wait_move(v);
    actor_cmd_wait_turn(v);
    set_control(-1);
    g.script_flags[1] &= 0xef;
    world().cam_pitch_deg = 0x15;
    const s16 entry = world_music_entry();
    sound_fade_out(g.sound, 0x3c);
    wr32(flags + 0x38, entry);
    field_side_load(entry);
    f->state = 0xc;
    f->state_timer = 0x1e;
    return true;
  }
  // Boarding (holding the action button on foot within 0x105 of actor 0xd): the leader
  // walks into it and fades out; its music (0xa86) is read meanwhile.
  Actor* v = field_actor(0xd);
  Actor* l = leader();
  if (v == nullptr || l == nullptr) return false;
  const s32 dx = (v->x - l->x) >> 12, dy = (v->y - l->y) >> 12, dz = (v->z - l->z) >> 12;
  if (SquareRoot0(dx * dx + dy * dy + dz * dz) >= 0x105) return false;
  field_side_load(0xa86);
  sound_fade_out(g.sound, 0x3c);
  actor_cmd_move_by_xz(l, static_cast<s16>(dx), static_cast<s16>(dz), 3);
  actor_queue_fade_colour(l, 0x50, 0x50, 0x50, 1, 2);
  actor_queue_fade_colour(l, 0, 0, 0, 0x3c, 2);
  actor_cmd_wait_move(l);
  set_control(-1);
  f->state = 9;
  return true;
}

// The camp menu's results 4 (a warp: side text 0xfc3 + game_state +0x288, state 0x1b)
// and 5 (the bunny call on the world map, not while riding it: its music 0xa92, state
// 0x11).
void field_world_menu_result(Field* f, int result) {
  FieldGlobals& g = fg();
  if (result == 4) {
    field_side_load(static_cast<s16>(g.game_state[0x288] + 0xfc3));
    wr32(g.field_flags + 0x3c, field_side_entry());
    set_control(-1);
    f->state = 0x1b;
    wr32(g.field_flags + 0x38, 0);
  } else if (result == 5) {
    if (g.field_flags[0x68] == 0 || sflag(1, 6)) return;
    field_side_load(0xa92);
    set_control(-1);
    sound_fade_out(g.sound, 0x3c);
    f->state = 0x11;
  }
}

void field_world_bunny_reload(Field* f) {
  field_side_load(0xa92);
  set_control(-1);
  f->state = 0x11;
  sound_fade_out(fg().sound, 0x3c);
}

// field_update states 9-0xe, 0x11-0x1a (0x1b-0x20: field_world_update_event).
void field_world_update(Field* f) {
  FieldGlobals& g = fg();
  u8* gs = g.game_state;
  u8* flags = g.field_flags;
  ActorSystem* sys = actor_system();
  switch (f->state) {
    case 9: {
      // The leader has walked into the vehicle: actor 0xd takes the leader's slot, turns
      // to the view direction and rises (state 10).
      Actor* l = leader();
      if (l != nullptr && actor_is_busy(l)) return;
      actor_delete_slot(sys, lead_slot());
      field_actors()[lead_slot()] = field_actors()[0xd];
      field_actors()[0xd] = nullptr;
      Actor* v = leader();
      if (v == nullptr) return;
      f->vehicle_a = -0x4b0;
      const s32 y = v->y >> 12;
      if (y - 0x4b0 < -0x8fc) f->vehicle_a = y > -0x899 ? -0x8fc - y : -200;
      f->state_timer = 0x46;
      f->vehicle_b = v->y;
      actor_cmd_turn_to(v, camera_degrees(), 4, 0);
      if (WorldActor* wv = as_world(v)) wv->face_heading = 0;
      actor_cmd_wait_move(v);
      actor_cmd_wait_turn(v);
      wr16(flags + 0xe, static_cast<s16>(lead_slot()));
      g.script_flags[1] |= 0x10;
      world().cam_pitch_deg = 0x12;
      world().cam_frames = 10;
      field_load_buffer().clear();
      g_music_waiting = false;
      f->state = 10;
      return;
    }
    case 10: {
      // Rising: 70 frames, the camera lifted 50 units (GTE offset), the vehicle bobbing.
      const s32 t = f->state_timer;
      Actor* v = leader();
      if (t < 0x28) {
        if (t > 9) {
          const s32 s = sin4096(static_cast<u32>(((0x28 - t) * 0x800) / 0x1e + 0x400));
          set_y(v, f->vehicle_b + f->vehicle_a * 0x1000 + (4000 - s) * 100);
        }
      } else {
        const s32 s = sin4096(static_cast<u32>(((0x46 - t) * 0x400) / 0x1e));
        world().view.geom_ofs_y = (s * -0x32) >> 12;
        set_y(v, f->vehicle_b + s * f->vehicle_a);
      }
      play_installed_music(0xa86);
      install_side_music(f);
      f->state_timer -= g.frame_ticks;
      if (f->state_timer < 1) {
        f->state = 0xb;
        world().cam_frames = 0;
      }
      return;
    }
    case 0xb: {
      // Flying: once the vehicle music plays, the leader (the vehicle) takes control.
      install_side_music(f);
      if (sound_bank_busy(g.sound) == 1) return;
      if (!music_ready(0xa86)) return;
      play_installed_music(0xa86);
      set_control(static_cast<s16>(lead_slot()));
      Actor* v = leader();
      if (v != nullptr) actor_or_flags(v, 0, 0x1000000);
      world().streaming = 1;
      if (WorldActor* wv = as_world(v)) wv->throttle = wv->climb = wv->vel_430 = 0;
      preload_restore_table();
      f->state = 0;
      return;
    }
    case 0xc: {
      // Landing: 30 frames down to the ground, then the vehicle goes back to slot 0xd and
      // the leader steps out (fading in) and walks off the dismount offset.
      const s32 entry = rd32(flags + 0x38);
      play_installed_music(static_cast<s16>(entry));
      install_side_music(f);
      f->state_timer -= g.frame_ticks;
      Actor* v = leader();
      if (f->state_timer >= 0) {
        const s32 s = sin4096(static_cast<u32>((f->state_timer * 0x400) / 0x1e));
        world().view.geom_ofs_y = (s * -0x32) >> 12;
        set_y(v, f->vehicle_b + s * f->vehicle_a);
        return;
      }
      set_y(v, f->vehicle_b);
      world().view.geom_ofs_y = 0;
      actor_delete_slot(sys, 0xd);
      field_actors()[0xd] = v;
      field_actors()[lead_slot()] = nullptr;
      if (v == nullptr) return;
      const s32 pos[3] = {v->x >> 12, v->y >> 12, v->z >> 12};
      actor_create(sys, lead_slot(), lead_slot(), pos, 0, 0, 0xffff, 0xffff, 0x1000f);
      Actor* l = leader();
      if (l == nullptr) return;
      actor_or_flags(l, 0, 0x1200004);
      if (map_id() == 1) {
        wr32(gs + 0x26c, pos[0]);
        wr32(gs + 0x28c, pos[1]);
        wr32(gs + 0x270, pos[2]);
      } else {
        wr32(gs + 0x274, pos[0]);
        wr32(gs + 0x290, pos[1]);
        wr32(gs + 0x278, pos[2]);
      }
      actor_cmd_move_by_xz(l, static_cast<s16>(pos[0] - (f->dismount[0] >> 12)),
                           static_cast<s16>(pos[2] - (f->dismount[2] >> 12)), 3);
      actor_queue_fade_colour(l, 0, 0, 0, 1, 2);
      actor_queue_fade_colour(l, 0x80, 0x80, 0x80, 0x3c, 2);
      actor_queue_wait_fade(l);
      actor_queue_fade_colour(l, 0x80, 0x80, 0x80, 1, 0);
      actor_cmd_wait_move(l);
      wr16(flags + 0xe, static_cast<s16>(lead_slot()));
      f->state = 0xe;
      return;
    }
    case 0xd:
      // (Not entered by the field program.) After the timer: field_flags +0x92 = 1, the
      // leader in control and one extra script-thread update.
      if (f->state_timer < 1) {
        flags[0x92] = 1;
        set_control(static_cast<s16>(lead_slot()));
        if (g_script_vm != nullptr) g_script_vm->update();
      } else {
        f->state_timer -= g.frame_ticks;
      }
      return;
    case 0xe: {
      // Landed: once the leader has walked off and the world music is in, control returns.
      install_side_music(f);
      Actor* l = leader();
      const bool ready = sound_bank_busy(g.sound) == 0 && (l == nullptr || !actor_is_busy(l));
      if (!ready) return;
      const s32 entry = rd32(flags + 0x38);
      if (!music_ready(entry)) return;
      world().streaming = 1;
      set_control(static_cast<s16>(lead_slot()));
      play_installed_music(static_cast<s16>(entry));
      set_control(static_cast<s16>(lead_slot()));
      if (l != nullptr) actor_or_flags(l, 0, 0x1000000);
      if (WorldActor* wl = as_world(l)) wl->throttle = wl->climb = wl->vel_430 = 0;
      load_queue_set_streaming(load_queue(), true);
      world().streaming = 1;
      preload_restore_table();
      f->state = 0;
      return;
    }
    case 0x11:
      // The bunny call: its music (0xa92) is read; wait for the SPU.
      if (!field_side_load_done()) return;
      f->state = sound_bank_busy(g.sound) != 0 ? 0x12 : 0x13;
      return;
    case 0x12:
      if (sound_bank_busy(g.sound) != 1) f->state = 0x13;
      return;
    case 0x13:
      install_side_buffer_music(f);
      field_side_load(0xfc3);  // the bunny's sprite pack
      f->state = 0x14;
      return;
    case 0x14:
      if (sound_bank_busy(g.sound) == 1) return;
      start_installed(static_cast<s16>(0xa92));
      f->state = 0x15;
      return;
    case 0x15: {
      // Riding: script flag 14, the leader drawn as the bunny (sprite 199, pack 0xfc3).
      if (!field_side_load_done()) return;
      g.script_flags[1] |= 0x40;
      fade_push(field_fade(), 1, 0xf, 0, 0);
      load_queue_set_streaming(load_queue(), true);
      sprite_pack_register(field_side_buffer().data(), field_side_entry(), 0);
      field_side_buffer().clear();
      Actor* l = leader();
      if (l != nullptr) {
        l->model = 199;
        l->resolved_for = 0xffff;
        actor_mark_dirty(l);
        actor_or_flags(l, 0, 0x800000);
      }
      world().streaming = 1;
      set_control(static_cast<s16>(lead_slot()));
      preload_restore_table();
      f->state = 0;
      return;
    }
    case 0x16:
      // Off the bunny: after 30 frames the leader is itself again and the bunny's pack goes.
      if (f->state_timer > 0x1d) {
        sound_play_sfx(g.sound, 0x23, 0, 0x7f40);
        if (Actor* l = leader()) {
          l->model = static_cast<s16>(lead_slot());
          l->resolved_for = 0xffff;
          l->scale_y = 0xe80;
          l->scale_x = 0xe80;
          actor_mark_dirty(l);
          actor_and_flags(l, 0, 0xff7fffff);
          free_sprite_pack(0xfc3);
          actor_set_anim(l, -1);
        }
        f->state = 0x17;
      }
      f->state_timer += g.frame_ticks;
      return;
    case 0x17: {
      Actor* b = field_actor(0xf);
      const bool busy = b != nullptr && actor_is_busy(b);
      if (!busy) actor_delete_slot(sys, 0xf);
      if (!field_side_load_done() || busy) return;
      f->state = sound_bank_busy(g.sound) != 0 ? 0x18 : 0x19;
      return;
    }
    case 0x18:
      if (sound_bank_busy(g.sound) != 1) f->state = 0x19;
      return;
    case 0x19:
      install_side_buffer_music(f);
      f->state = 0x1a;
      return;
    case 0x1a:
      if (sound_bank_busy(g.sound) == 1) return;
      start_installed(0);
      field_side_buffer().clear();
      set_control(static_cast<s16>(lead_slot()));
      g.script_flags[1] &= 0xbf;
      world().streaming = 1;
      preload_restore_table();
      preload_release_pending();
      f->state = 0;
      return;
    case 0x1b: {
      // The courier's sprite pack is in: the leader turns (to the side for an order, to the
      // camera for a delivery) and the flight starts from above the leader's head.
      if (!field_side_load_done()) return;
      load_queue_set_streaming(load_queue(), true);
      sprite_pack_register(field_side_buffer().data(), field_side_entry(), 0);
      field_side_buffer().clear();
      Actor* l = leader();
      if (l == nullptr) return;
      s32 yaw;
      if (flags[0x68] == 0) {
        ActorSystem* as = actor_system();
        yaw = as != nullptr ? as->camera_yaw : 0;
        if (flags[0x83] == 1 && as != nullptr) {
          const s32 half = read_u16(map_data().scene.data() + 0x34) >> 1;
          if (half < l->screen_y) {
            yaw += ((l->screen_x - half) * as->half_fov) / std::max(1, half);
          } else {
            yaw -= ((half - l->screen_x) * as->half_fov) / std::max(1, half);
          }
        }
        wr32(flags + 0x5c, static_cast<s32>((static_cast<u32>(read_u16(map_data().scene.data() + 0x6a)) *
                                              read_u16(map_data().scene.data() + 0x68) * 0x1000) /
                                             static_cast<u32>(std::max(1, l->depth * 400))));
      } else {
        yaw = world_camera_yaw();
        wr32(flags + 0x5c, 0x578);
      }
      const s32 height = l->quad[5] - l->quad[3];
      s16 rise;
      if (rd32(flags + 0x38) == 0) {
        yaw += 0x400;
        f->state = 0x1c;
        rise = static_cast<s16>(height / 2);
      } else {
        if (flags[0x68] != 0) yaw += 0x800;
        f->state = 0x1d;
        field_side_load(0xfc2);
        rise = static_cast<s16>((height * 5) / 6);
      }
      wr32(flags + 0x60, static_cast<s32>(l->screen_x) << 12);
      wr32(flags + 0x64, static_cast<s32>(static_cast<s16>(l->screen_y - rise)) << 12);
      s32 d = yaw * 0x168;
      if (d < 0) d += 0xfff;
      actor_cmd_turn_to(l, static_cast<s16>((d << 4) >> 16), 8, 0);
      actor_cmd_wait_turn(l);
      f->state_timer = 0;
      return;
    }
    case 0x1c: {
      // The order: the courier appears over the leader and flies off along an arc; when it
      // is gone, script flag 13 waits for the delivery.
      const s32 dur = courier_frames();
      if (f->state_timer < dur) {
        if (Actor* l = leader(); l != nullptr && actor_is_busy(l)) return;
        const s32 k = f->state_timer - 0x14;
        if (k >= 0) {
          Actor* c = field_actor(0xe);
          if (c == nullptr && (c = courier_create(-100, -100)) != nullptr) {
            actor_set_anim(c, courier_anim(0));
            actor_queue_fade_colour(c, 0, 0, 0, 0, 2);
            actor_queue_fade_colour(c, 0x80, 0x80, 0x80, 0x14, 2);
            actor_queue_wait_fade(c);
            actor_queue_fade_colour(c, 0x80, 0x80, 0x80, 0, 0);
            c->spin_angle = 0x28;
            c->spin_speed = 3;
          }
          const s32 a = (k * 800) / dur + 0x400;
          const s16 x = static_cast<s16>((rd32(flags + 0x60) + cos4096(static_cast<u32>(a)) * 0x140) >> 12);
          const s16 y = static_cast<s16>((rd32(flags + 0x64) + (0x1000 - sin4096(static_cast<u32>(a))) * -0x118) >> 12);
          if (c != nullptr) {
            c->screen_x = x;
            c->screen_y = y;
          }
          s16 org[2];
          view_origin(org);
          if (x - org[0] <= -0x15 || y - org[1] <= -0x15) f->state_timer = dur;
        }
        if (Actor* c = field_actor(0xe)) actor_mark_dirty(c);
        f->state_timer += g.frame_ticks;
        return;
      }
      set_control(static_cast<s16>(lead_slot()));
      actor_delete_slot(sys, 0xe);
      preload_restore_table();
      free_sprite_pack(rd32(flags + 0x3c));
      g.script_flags[1] |= 0x20;
      world().streaming = 1;
      f->state = 0;
      return;
    }
    case 0x1d: {
      // The delivery: the courier flies in along the mirrored arc to above the leader.
      const s32 dur = courier_frames();
      if (f->state_timer < dur) {
        if (Actor* l = leader(); l != nullptr && actor_is_busy(l)) return;
        const s32 t = f->state_timer;
        Actor* c = field_actor(0xe);
        if (t - 0x14 < 0) {
          if (dur < (t * 10) / 9 && c != nullptr) actor_set_anim(c, courier_anim(2));
        } else if (c == nullptr) {
          c = courier_create(static_cast<s16>(rd32(flags + 0x60) >> 12), static_cast<s16>(rd32(flags + 0x64) >> 12));
          if (c != nullptr) {
            if (flags[0x68] == 0) actor_and_flags(c, 0, 0xfffffcff);
            actor_set_anim(c, courier_anim(1));
          }
        }
        const s32 a = ((dur - t) * 800) / dur + 0x400;
        if (c != nullptr) {
          c->screen_x = static_cast<s16>((rd32(flags + 0x60) + cos4096(static_cast<u32>(a)) * -0x140) >> 12);
          c->screen_y = static_cast<s16>((rd32(flags + 0x64) + (0x1000 - sin4096(static_cast<u32>(a))) * -0x118) >> 12);
          actor_mark_dirty(c);
        }
        f->state_timer += g.frame_ticks;
        return;
      }
      Actor* c = field_actor(0xe);
      if (c != nullptr) {
        c->screen_x = static_cast<s16>(rd32(flags + 0x60) >> 12);
        c->screen_y = static_cast<s16>(rd32(flags + 0x64) >> 12);
        actor_mark_dirty(c);
      }
      if (!field_side_load_done()) return;
      f->state_timer = 0;
      field_install_side_text(field_side_buffer().data(), field_side_buffer().size());  // 0x80053b44 (streaming back on)
      field_side_buffer().clear();
      if (c != nullptr) actor_set_anim(c, courier_anim(3));
      f->state = 0x1e;
      return;
    }
    case 0x1e: {
      // After 16 frames the goods (game_state +0x250, ten {id:10, count:5}) go into the
      // inventory and window 6 lists them (field +0x38.. holds them for the text codes).
      if (f->state_timer > 0xf) {
        wr32(flags + 0x64, rd32(flags + 0x3c));
        int k = 0;
        for (int i = 0; i < 10; ++i) {
          const u16 v = read_u16(gs + 0x250 + i * 2);
          if (v == 0) continue;
          const int count = (v & 0x7fff) >> 10;
          inventory_add(g.inventory, v & 0x3ff, count, 1);
          wr32(flags + 0x38 + k * 4, static_cast<s32>((static_cast<u32>(v & 0x3ff) << 16) | static_cast<u32>(count)));
          ++k;
        }
        const int lines = std::min(k, 4);
        sound_play_sfx(g.sound, 0xd, 0, 0x7f40);
        const Actor* l = leader();
        const int y = (l != nullptr && l->quad[5] < 0xba) ? 0x17 - lines : 1;
        const u8* strings = g.system_strings;
        if (strings != nullptr) {
          const u8* text = strings + read_u32(strings + 0xc + k * 4);
          msg_window_open(msg_system(), 4, y, 0x20, lines, text, 6, 0);
          if (MsgWindow* w6 = msg_window(6)) w6->word_wrap = 0;
        }
        f->state = 0x1f;
      }
      f->state_timer += g.frame_ticks;
      return;
    }
    case 0x1f:
      // Once window 6 has closed the courier fades out.
      if (MsgWindow* w6 = msg_window(6); w6 != nullptr && msg_window_get_state(w6) != -1) return;
      if (Actor* c = field_actor(0xe)) {
        actor_queue_fade_colour(c, 0x5a, 0x5a, 0x5a, 0, 2);
        actor_queue_fade_colour(c, 0, 0, 0, 0x3c, 2);
        actor_queue_wait_fade(c);
      }
      f->state = 0x20;
      return;
    case 0x20: {
      if (Actor* c = field_actor(0xe); c != nullptr && actor_is_busy(c)) return;
      set_control(static_cast<s16>(lead_slot()));
      actor_delete_slot(sys, 0xe);
      wr32(gs + 0x294, 0);
      free_sprite_pack(rd32(flags + 0x64));
      field_end_overlay_event();  // 0x8004c0cc
      preload_restore_table();
      g.script_flags[1] &= 0xdf;
      preload_release_pending();
      world().streaming = 1;
      f->state = 0;
      return;
    }
    default:
      runtime::Runtime::get().log_once("field_world_state_" + std::to_string(f->state),
                                       "field_update: world state " + std::to_string(f->state) + " not ported yet");
      return;
  }
}

}  // namespace ss::game::field
