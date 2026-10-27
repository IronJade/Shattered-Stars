// Trigger regions, the talk / pickpocket probe and the VM's two screen elements (the
// Private Action prompt and the choice cursor). Field 0x80068810-0x80068eec,
// 0x80069d8c, 0x8006ad40, 0x8006afd8, 0x8006b630; ext 0x76 / 0x77 run world-map
// overlay code (#3102 0x800829fc) that only touches the trigger table.
#include <cstdio>
#include <cstring>

#include "game/ap/ap.h"
#include "game/common/math.h"
#include "game/field/actor_field.h"
#include "game/field/actors.h"
#include "game/field/screen.h"
#include "game/field/script_vm.h"
#include "game/field/vm_services.h"
#include "runtime/libgpu.h"
#include "runtime/runtime.h"

namespace ss::game::field {
namespace {

// The point the triggers test: the leader's logical position (actor +0x74 / +0x76,
// placed by the scripts with B 0x3e) on 2D fields, its world position >> 12 on 3D and
// world maps (field state +0x68).
void leader_trigger_point(int leader, s32* x, s32* z) {
  if (globals().field_flags[0x68] == 0) {
    *x = vm_svc_actor_get_view_x(leader);
    *z = vm_svc_actor_get_view_y(leader);
  } else {
    s32 px, py, pz;
    vm_svc_actor_get_position(leader, &px, &py, &pz);
    *x = px >> 12;
    *z = pz >> 12;
  }
}

// Edge test of the clockwise quad: every cross product <= 0 inside.
bool quad_contains(const Trigger& t, s32 x, s32 z) {
  const s32* c = t.corner;
  return (c[2] - c[0]) * (z - c[1]) - (c[3] - c[1]) * (x - c[0]) < 1 &&
         (c[4] - c[2]) * (z - c[3]) - (c[5] - c[3]) * (x - c[2]) < 1 &&
         (c[6] - c[4]) * (z - c[5]) - (c[7] - c[5]) * (x - c[4]) < 1 &&
         (c[0] - c[6]) * (z - c[7]) - (c[1] - c[7]) * (x - c[6]) < 1;
}

// Perspective projection of a world point as field_try_talk does it (H = scene +0x6a,
// offset = scene +0x34 / +0x36 halved). Returns false where the console would trap
// (point at depth 0).
bool project(const u8* scene, const VmVector& world, s16* sx, s16* sy) {
  VmVector v;
  s32 flag;
  TransRot_32(&world, &v, &flag);
  const s32 z = v.vz >> 4;
  if (scene == nullptr || z == 0) return false;
  const s32 h = read_u16(scene + 0x6A);
  *sx = static_cast<s16>(((v.vx >> 4) * h + (v.vz >> 5)) / z + (read_u16(scene + 0x34) >> 1));
  *sy = static_cast<s16>(((v.vy >> 4) * h + (v.vz >> 5)) / z + (read_u16(scene + 0x36) >> 1));
  return true;
}

// Archipelago (port feature; open world and the hub) [salvadrift agent]: a door region the seed
// keeps shut works as a shut door. The original never leaves the hero walking about in a door's
// region (its transfer starts at once; where a routine refuses by itself, as Alen's room 53
// 0x6c08 does at flag 2 or chapter 7, it walks the hero back out), and past some of them no wall
// stops anyone: the ramp behind Alen's bookcase (map 53) ends in open space where the hero walks
// on at the ramp's last height, hidden behind the room; past Salva's barricade (map 43) the hero
// climbs the cliff picture. So once ap::door_blocked_at_trigger refuses a region, the hero gets
// no further in than the step that entered it: while they touch the region, a step that ends
// deeper along that step's line goes back to where they stood; any other step stands. The
// banner comes once per approach (until the hero is kShutNear away from the region). A door that
// opens meanwhile (its key received) fires at the next step in, not under a hero standing still.
// Field maps only (world-map entrances lie on open land); not when the entering step is unknown
// (the first scan on a map or after a scene: the hero walks on, as before). Archipelago off, or
// the door open: none of this runs.
constexpr s32 kShutNear = 8;  // trigger-point units (pixels on a field map) around the region
struct ShutDoor {
  const ScriptCtx* ctx = nullptr;  // the scan before: its script, map, vblank, the hero's spot
  int map = -1;
  u64 vblank = 0;
  s32 pos[3] = {0, 0, 0};
  int slot = -1;                   // the refused region (-1 none)
  s32 spot[3] = {0, 0, 0};         // where the hero stands in it
  s32 entry_x = 0, entry_z = 0;    // where that step ended (x, z; 20.12)
  s32 in_x = 0, in_z = 0;          // the step that entered it (x, z; 20.12)
};
ShutDoor g_shut;

// The hero (at px, pz) touches the refused region: does this step end deeper than the entering
// one (by more than two such steps: the walk drifts a little on ramps)?
bool shut_door_deeper(s32 px, s32 pz) {
  const s64 dx = static_cast<s64>(px - g_shut.entry_x) >> 4, dz = static_cast<s64>(pz - g_shut.entry_z) >> 4;
  const s64 ix = g_shut.in_x >> 4, iz = g_shut.in_z >> 4;
  return dx * ix + dz * iz > 2 * (ix * ix + iz * iz);
}

// A deeper step goes back to the spot; another stands and is the new spot.
void shut_door_step(int leader, s32 px, s32 py, s32 pz) {
  if (!shut_door_deeper(px, pz)) {
    g_shut.spot[0] = px;
    g_shut.spot[1] = py;
    g_shut.spot[2] = pz;
    return;
  }
  Actor* a = field_actor(leader);
  if (a == nullptr) return;
  a->x = g_shut.spot[0];
  a->y = g_shut.spot[1];
  a->z = g_shut.spot[2];
  fieldactor_place(a);  // its view position (the triggers' point) and floor zone
  std::memcpy(g_shut.pos, g_shut.spot, sizeof g_shut.pos);
}

}  // namespace

int trigger_alloc_slot(ScriptCtx* ctx) {
  int i = 0;
  do {
    if (ctx->triggers[i].active == 0) return i;
    ++i;
  } while (i < kTriggerCount);
  return i;
}

void trigger_remove(ScriptCtx* ctx, u32 slot) {
  if (slot < static_cast<u32>(kTriggerCount)) ctx->triggers[slot].active = 0;
}

void trigger_compute_bbox(ScriptCtx* ctx, Trigger* t) {
  s32 max_x = t->corner[0], min_z = t->corner[1];
  s32 max_z = min_z, min_x = max_x;
  for (int i = 1; i < 4; ++i) {
    const s32 x = t->corner[2 * i], z = t->corner[2 * i + 1];
    if (x < min_x) min_x = x;
    if (z < min_z) min_z = z;
    if (max_x < x) max_x = x;
    if (max_z < z) max_z = z;
  }
  t->min_x = min_x;
  t->min_z = min_z;
  t->max_x = max_x;
  t->max_z = max_z;
}

void trigger_check_regions(ScriptCtx* ctx) {
  FieldGlobals& g = globals();
  const int leader = leader_slot();
  if (!vm_svc_actor_exists(leader)) return;
  s32 px, py, pz;
  vm_svc_actor_get_position(leader, &px, &py, &pz);
  const s32 y = py >> 12;
  s32 x, z;
  leader_trigger_point(leader, &x, &z);
  // [salvadrift agent] The hero's spot at the scan before, when it was this map's, a moment ago;
  // the approach to a shut door lasts while the hero stays near its region (kShutNear).
  const int map = vm_rd16(g.game_state + 0x1A);
  const u64 now = runtime::Runtime::get().vblanks();
  const bool same_map = g.field_flags[0x68] == 0 && g_shut.ctx == ctx && g_shut.map == map;
  const bool had_spot = same_map && now - g_shut.vblank <= 8;
  const s32 before[3] = {g_shut.pos[0], g_shut.pos[1], g_shut.pos[2]};
  g_shut.ctx = ctx;
  g_shut.map = map;
  g_shut.vblank = now;
  g_shut.pos[0] = px;
  g_shut.pos[1] = py;
  g_shut.pos[2] = pz;
  if (g_shut.slot >= 0) {
    const Trigger& t = ctx->triggers[g_shut.slot];
    if (!same_map || t.active == 0 || x < t.min_x - kShutNear || t.max_x + kShutNear < x || z < t.min_z - kShutNear ||
        t.max_z + kShutNear < z) {
      g_shut.slot = -1;
    }
  }
  // Once one quad has contained the point this stays set for the rest of the scan (as
  // on the console), so a later region only needs its bounding box to pass.
  bool inside = false;
  for (int i = 0; i < kTriggerCount; ++i) {
    Trigger& t = ctx->triggers[i];
    if (t.active == 0 || y < t.y_min || t.y_max < y || x < t.min_x || t.max_x < x || z < t.min_z || t.max_z < z) {
      continue;
    }
    // Script flags 12 / 14 (vehicle, cutscene) mute every region without +6.
    if (!(((g.script_flags[1] >> 4 & 1) == 0 && (g.script_flags[1] >> 6 & 1) == 0) || t.menu_flags != 0)) continue;
    bool button = false;
    if (t.buttons != 0xFFFFFFFFu) {
      u32 pressed = g.pressed[0] & 0xFFF0FFFFu;
      if ((g.pressed[0] & config_button(0)) != 0) pressed |= 0x10000;
      if ((g.pressed[0] & config_button(1)) != 0) pressed |= 0x20000;
      if ((g.pressed[0] & config_button(2)) != 0) pressed |= 0x40000;
      if ((g.pressed[0] & config_button(3)) != 0) pressed |= 0x80000;
      button = true;
      if ((t.buttons & pressed) == 0) continue;
    }
    if (quad_contains(t, x, z)) inside = true;
    if (!inside) {
      ctx->dwell_counter = 0;
      continue;
    }
    bool started = false;
    bool fire = t.dwell == 0;
    if (!fire && (vm_svc_actor_get_flags(leader) & 0x400000) != 0) {
      ctx->dwell_counter = static_cast<s16>(ctx->dwell_counter + 1);
      if (t.dwell_frames <= ctx->dwell_counter) {
        vm_push_frame(ctx);
        started = true;
        ctx->ip = t.code;
        fire = true;
      }
    }
    if (!fire) continue;
    if (t.private_action != -1) {
      if ((g.script_flags[0] >> 2 & 1) == 0) pa_draw_prompt(ctx);
      if ((g.held[0] & config_button(3)) != 0) {
        started = true;
        g.script_flags[0] |= 4;
        vm_push_frame(ctx);
        ctx->ip = ctx->code + static_cast<u16>(t.private_action);
      }
    }
    if (!started) {
      if (button) ctx->last_trigger = -1;
      // [salvadrift agent] In the shut door's region on the same approach (ShutDoor): no second
      // banner; a step further in goes back. Once the door has opened (its key received), a step
      // further in fires the region as a new approach would. (A button region refuses each press.)
      if (i == g_shut.slot && !button) {
        if (ap::door_shut_at_trigger(static_cast<int>(t.code - ctx->code) * 4) || !shut_door_deeper(px, pz)) {
          ctx->last_trigger = i;
          shut_door_step(leader, px, py, pz);
          return;
        }
        g_shut.slot = -1;
        ctx->last_trigger = -1;
      }
      if (i != ctx->last_trigger && i != ctx->active_trigger) {
        // Archipelago (port feature; open world) [world agent]: a locked door's routine does
        // not start (nor its music fade); the region fires again only once left.
        if (ap::door_blocked_at_trigger(static_cast<int>(t.code - ctx->code) * 4)) {
          ctx->last_trigger = i;
          // [salvadrift agent] ... and the hero goes no further in (ShutDoor).
          g_shut.slot = had_spot ? i : -1;
          if (had_spot) {
            g_shut.spot[0] = px;
            g_shut.spot[1] = py;
            g_shut.spot[2] = pz;
            g_shut.entry_x = px;
            g_shut.entry_z = pz;
            g_shut.in_x = px - before[0];
            g_shut.in_z = pz - before[2];
            char line[112];
            std::snprintf(line, sizeof line, "shut door region %d: leader %d,%d,%d, entered along %d,%d (1/256)", i,
                          px >> 12, py >> 12, pz >> 12, g_shut.in_x >> 4, g_shut.in_z >> 4);
            runtime::Runtime::get().trace(line);
          }
          return;
        }
        vm_push_frame(ctx);
        started = true;
        ctx->ip = t.code;
      }
      ctx->last_trigger = i;
      if (!started) return;
    }
    char line[96];
    std::snprintf(line, sizeof line, "trigger %d fires (code word %ld) at leader %d,%d,%d", i,
                  static_cast<long>(ctx->ip - ctx->code), x, y, z);
    runtime::Runtime::get().trace(line);
    ctx->active_trigger = i;
    ctx->depth = 1;
    ctx->saved_control = field_state_s16(0x10);
    field_state_set_s16(0x10, -1);
    vm_svc_actor_and_flags(leader, 0, 0xFFBFFFFF);
    return;
  }
  ctx->last_trigger = -1;
}

bool trigger_contains_leader(ScriptCtx* ctx, int slot) {
  const Trigger& t = ctx->triggers[slot];
  if (t.active == 0) return false;
  const int leader = leader_slot();
  if (!vm_svc_actor_exists(leader)) return false;
  s32 x, z;
  leader_trigger_point(leader, &x, &z);
  if (x < t.min_x || t.max_x < x || z < t.min_z || t.max_z < z) return false;
  return quad_contains(t, x, z);
}

void worldmap_trigger_add(ScriptCtx* ctx, u32 op) {
  const int slot = trigger_alloc_slot(ctx);
  Trigger& t = ctx->triggers[slot];
  if (op == 0x77) {
    vm_fetch_args(ctx, 11);
  } else {
    vm_fetch_args(ctx, 9);
    g_vm_args[9] = -0x8000;
    g_vm_args[10] = 0x7FFF;
  }
  t.code = ctx->code + (static_cast<u32>(g_vm_args[8]) & 0x7FFFFF);
  t.y_min = static_cast<s16>(g_vm_args[9]);
  t.active = 1;
  t.private_action = -1;
  t.menu_flags = 0;
  t.dwell = 0;
  t.world_map = 0;
  t.buttons = 0xFFFFFFFFu;
  t.y_max = static_cast<s16>(g_vm_args[10]);
  for (int i = 0; i < 8; ++i) t.corner[i] = g_vm_args[i];
  trigger_compute_bbox(ctx, &t);
  vm_set_r(static_cast<u32>(slot));
  char line[160];
  std::snprintf(line, sizeof line, "world trigger %d added: x %d..%d z %d..%d y %d..%d code word %ld", slot, t.min_x,
                t.max_x, t.min_z, t.max_z, t.y_min, t.y_max, static_cast<long>(t.code - ctx->code));
  runtime::Runtime::get().trace(line);
}

int field_try_talk(ScriptCtx* ctx, int talk) {
  FieldGlobals& g = globals();
  if (vm_svc_msg_window_get_state(0) != -1) return 0;
  if (talk == 0) {
    // Pickpocketing needs a member wearing accessory 0x1b5 or 0x1bc with talent bit 2;
    // the best skill level (specialty 0xf) is the thief.
    int best = -1;
    int thief = 0;
    for (int slot = 0; slot < 8; ++slot) {
      const u8* rec = g.records + slot * 0xD0;
      const u16 id = read_u16(g.party + slot * 0x60);
      if (static_cast<u32>(id - 1) >= 12) continue;
      const s16 a = vm_rd16(rec + 0x16), b = vm_rd16(rec + 0x18);
      if (a != 0x1B5 && b != 0x1B5 && a != 0x1BC && b != 0x1BC) continue;
      int level = vm_svc_party_get_specialty_level(slot, 0xF);
      if ((read_u32(rec + 0x20) & 4) == 0) level = 0;
      if (best < level) {
        best = level;
        thief = slot;
      }
    }
    if (best == -1) {
      ctx->pickpocket_level = 0;
      return 0;
    }
    const u8* rec = g.records + thief * 0xD0;
    ctx->pickpocket_level = best;
    ctx->pickpocket_bonus = (vm_rd16(rec + 0x16) == 0x1BC || vm_rd16(rec + 0x18) == 0x1BC) ? 1 : 0;
    ctx->thief_slot = thief;
  }
  const int self = field_state_s16(0x10);
  if (!vm_svc_actor_exists(self)) return 0;
  VmVector probe;
  vm_svc_actor_get_position(self, &probe.vx, &probe.vy, &probe.vz);
  const int angle = vm_svc_actor_get_angle(self);
  const s32 self_radius = vm_svc_actor_get_talk_radius(self);
  const s32 reach = vm_svc_scene_scale() * 90 / 100;
  const u8* scene = vm_svc_map_scene();
  for (int pass = 0; pass < 2; ++pass) {
    if (pass == 0) {
      // A point 'reach' units ahead.
      probe.vx += cos4096(static_cast<u32>(angle)) * reach;
      probe.vz += sin4096(static_cast<u32>(angle)) * reach;
      s16 sx, sy;
      project(scene, probe, &sx, &sy);
    } else if (g.field_flags[0x68] == 0) {
      // 2D fields: keep stepping the probe while it lands in a talk-through region
      // (cell list 13, e.g. across a shop counter); talk only if it moved at least once.
      bool extended = false;
      for (;;) {
        s16 sx = 0, sy = 0;
        project(scene, probe, &sx, &sy);
        const int cell = vm_svc_zone_cell_at(self, sx, sy);
        if (cell == -1 || scene == nullptr) return 0;
        const u8* grid = scene + read_u32(scene + 0x80);  // g_scene_grid 0x80075338
        const u8* cells = grid + 4;                       // g_scene_cells 0x80075334
        const u8* c = cells + cell * 0x60;
        const int count = read_u16(c + 0x1A);             // list 13
        const u8* item = grid + read_u32(c + 0x54);
        int k = 0;
        for (; k < count; ++k, item += 0x24) {
          if (vm_svc_zone_test_point(self, item, sx, sy, probe.vy >> 12) != 0) break;
        }
        if (k == count) {
          if (!extended) return 0;
          break;
        }
        probe.vx += cos4096(static_cast<u32>(angle)) * reach;
        probe.vz += sin4096(static_cast<u32>(angle)) * reach;
        extended = true;
      }
    }
    // The nearest actor around the probe that can be talked to (or pickpocketed).
    int found = -1;
    s32 best = 0x40000000;
    for (int i = 0; i < 0xC2; ++i) {
      if (!vm_svc_actor_exists(i) || i == self || (vm_svc_actor_get_flags(i) & 0x20000) != 0) continue;
      s32 ax, ay, az;
      vm_svc_actor_get_position(i, &ax, &ay, &az);
      const s32 radius = vm_svc_actor_get_talk_radius(i);
      const s32 dx = probe.vx - ax < 0 ? ax - probe.vx : probe.vx - ax;
      const s32 dz = probe.vz - az < 0 ? az - probe.vz : probe.vz - az;
      const s32 dy = probe.vy - ay < 0 ? ay - probe.vy : probe.vy - ay;
      if ((dy >> 12) >= 0xB5) continue;
      const s32 dist = SquareRoot0((dx >> 12) * (dx >> 12) + (dy >> 12) * (dy >> 12) + (dz >> 12) * (dz >> 12));
      if (dist <= ((radius + self_radius) * 3) / 5 && dist < best) {
        const int entry = talk == 0 ? vm_svc_actor_get_pickpocket_entry(i) : vm_svc_actor_get_talk_entry(i);
        best = dist;
        if (entry != -1) found = i;
      }
    }
    if (found == -1) continue;
    field_start_talk(ctx, found, talk);
    return 1;
  }
  return 0;
}

void field_start_talk(ScriptCtx* ctx, int found, int talk) {
  const int self = field_state_s16(0x10);
  {
    u16 entry;
    if (talk == 0) {
      entry = static_cast<u16>(vm_svc_actor_get_pickpocket_entry(found));
      ctx->talk_actor = -1;
    } else {
      entry = static_cast<u16>(vm_svc_actor_get_talk_entry(found));
      // Message windows at the bottom unless the NPC stands low on the screen.
      vm_svc_msg_set_default_x(2);
      vm_svc_msg_set_default_y(vm_svc_actor_get_screen_y(found) < 0x60 ? 0x10 : 2);
      s32 nx, ny, nz, sx, sy, sz;
      vm_svc_actor_get_position(found, &nx, &ny, &nz);
      vm_svc_actor_get_position(self, &sx, &sy, &sz);
      const s32 face = ratan2(sz - nz, sx - nx);
      s32 turn = static_cast<s32>((static_cast<u32>(face) + 0x800) & 0xFFF) - vm_svc_actor_get_angle(found);
      if (turn < 0x800) {
        if (turn < -0x800) turn += 0x1000;
      } else {
        turn -= 0x1000;
      }
      vm_svc_actor_push_state_and_turn(found, face, turn < 0 ? 1 : 2);
      vm_svc_actor_push_state_and_turn(self, face + 0x800, 0);
      ctx->talk_actor = static_cast<s16>(found);
    }
    vm_svc_actor_and_flags(leader_slot(), 0, 0xFFBFFFFF);
    vm_push_frame(ctx);
    ctx->depth = 1;
    ctx->ip = ctx->code + entry;
  }
}

void pa_draw_prompt(ScriptCtx* ctx) {
  Screen* s = field_screen();
  if (s == nullptr || s->current == nullptr) return;  // host: no screen yet (tests)
  const u8* atlas = vm_svc_ui_atlas();
  const s16 w = static_cast<s16>(atlas[0x94] - atlas[0x92]);
  // The "PA" plate, top right.
  auto* plate = s->alloc_prims<psyq::SPRT>();
  if (plate == nullptr) return;
  psyq::setSprt(plate);
  plate->y0 = 0x14;
  plate->x0 = static_cast<s16>(0x130 - w);
  plate->w = w;
  plate->h = static_cast<s16>(atlas[0x95] - atlas[0x93]);
  plate->u0 = atlas[0x92];
  plate->code |= 3;
  plate->clut = 0x7FC7;
  plate->v0 = atlas[0x93];
  psyq::addPrim(s->ot_entry(0x812), plate);
  // The blinking arrow (16 frames in 48 on its upper position).
  const u16 blink = (vm_svc_frame_counter() % 0x30 < 0x10) ^ 1;
  auto* arrow = s->alloc_prims<psyq::SPRT>();
  if (arrow == nullptr) return;
  arrow->x0 = static_cast<s16>(0x123 - w);
  psyq::setSprt(arrow);
  arrow->y0 = static_cast<s16>(blink * 5 + 0xB);
  arrow->w = static_cast<s16>(atlas[0x8C] - atlas[0x8A]);
  arrow->h = static_cast<s16>(atlas[0x8D] - atlas[0x8B]);
  arrow->u0 = atlas[0x8A];
  arrow->code = static_cast<u8>((arrow->code & 0xFD) | 1);
  arrow->clut = 0x7FC6;
  arrow->v0 = atlas[0x8B];
  psyq::addPrim(s->ot_entry(0x812), arrow);
  // The configured button's glyph.
  auto* button = s->alloc_prims<psyq::SPRT>();
  if (button == nullptr) return;
  button->x0 = static_cast<s16>(0x120 - w);
  button->y0 = 0x18;
  psyq::setSprt(button);
  button->w = 0x10;
  button->h = 0x10;
  button->u0 = static_cast<u8>((atlas[0x82] & 0xF0) + vm_svc_pad_button_icon_offset(config_button(3)));
  button->clut = 0x7FC6;
  button->v0 = static_cast<u8>((atlas[0x83] & 0xF0) + blink * 0x10);
  button->code = static_cast<u8>((button->code & 0xFD) | 1);
  psyq::addPrim(s->ot_entry(0x812), button);
  auto* mode = s->alloc_prims<psyq::DR_MODE>();
  if (mode == nullptr) return;
  psyq::setlen(mode, 2);
  mode->code[0] = 0xE1000216;
  mode->code[1] = 0;
  psyq::addPrim(s->ot_entry(0x812), mode);
}

void choice_draw_cursor(ScriptCtx* ctx) {
  const int window = ctx->wait_param;
  if (!vm_svc_msg_window_exists(window)) return;
  s32 pos[2] = {0, 0};  // x, y of the line
  s32 first_x = 0, first_y = 0;
  int line = 0;
  for (int i = 0; i < ctx->choice_selection; ++i) line += ctx->option_lines[i];
  const s16 lift = static_cast<s16>(static_cast<s16>(vm_svc_msg_window_line_height(window)) - 0xD);
  for (int k = 0; k < ctx->option_lines[ctx->choice_selection]; ++k) {
    s32 width = 0, height = 0;
    vm_svc_msg_window_line_pos(window, line + k + ctx->choice_first_line, pos, &width, &height);
    bool store = true;
    if (k == 0) {
      first_x = pos[0];
      first_y = pos[1];
      bool slide = ctx->choice_slide != 0;
      if (!slide) {
        if (ctx->choice_cursor[0].x != -1 && ctx->choice_cursor[0].y != pos[1]) ctx->choice_slide = 4;
        slide = ctx->choice_slide != 0;
      }
      if (slide) {
        // The underline slides to the new option over four frames.
        const s32 left = ctx->choice_slide;
        const s32 step = left < globals().frame_ticks ? left : globals().frame_ticks;
        ctx->choice_slide = static_cast<s16>(left - step);
        s32 d = (pos[1] - ctx->choice_cursor[0].y) * (4 - static_cast<s16>(left - step));
        if (d < 0) d += 3;
        pos[1] = ctx->choice_cursor[0].y + (d >> 2);
        store = ctx->choice_slide == 0;
      }
      if (store) ctx->choice_lines = ctx->option_lines[ctx->choice_selection];
    }
    if (store && ctx->choice_slide == 0) {
      ctx->choice_height[k] = static_cast<s16>(height);
      ctx->choice_width[k] = static_cast<s16>(width);
      ctx->choice_cursor[k].x = pos[0];
      ctx->choice_cursor[k].y = pos[1];
    }
  }
  Screen* s = field_screen();
  if (s == nullptr || s->current == nullptr) return;  // host: no screen yet (tests)
  for (int i = 0; i < ctx->choice_lines; ++i) {
    const s16 cx = static_cast<s16>(ctx->choice_cursor[i].x), cy = static_cast<s16>(ctx->choice_cursor[i].y);
    const s16 y = static_cast<s16>(ctx->choice_height[i] + cy - lift);
    // Yellow underline with a black shadow one pixel down-right.
    auto* hi = s->alloc_prims<psyq::LINE_F2>();
    auto* shadow = s->alloc_prims<psyq::LINE_F2>();
    if (hi == nullptr || shadow == nullptr) return;
    hi->r0 = 0xF0;
    hi->g0 = 0xD8;
    hi->b0 = 0x18;
    psyq::setLineF2(hi);
    hi->x0 = static_cast<s16>(cx + 0xE);
    hi->y0 = static_cast<s16>(y - 1);
    hi->x1 = static_cast<s16>(ctx->choice_width[i] + cx);
    hi->y1 = static_cast<s16>(y - 1);
    psyq::addPrim(s->ot_entry(0x817), hi);
    psyq::setLineF2(shadow);
    shadow->x0 = static_cast<s16>(cx + 0xF);
    shadow->y0 = y;
    shadow->x1 = static_cast<s16>(ctx->choice_width[i] + cx + 1);
    shadow->y1 = y;
    psyq::addPrim(s->ot_entry(0x810), shadow);
  }
  // The hand cursor beside the selected option's first line.
  const u8* atlas = vm_svc_ui_atlas();
  auto* hand = s->alloc_prims<psyq::SPRT>();
  auto* mode = s->alloc_prims<psyq::DR_MODE>();
  if (hand == nullptr || mode == nullptr) return;
  psyq::setSprt(hand);
  hand->x0 = static_cast<s16>(first_x - 2);
  hand->w = 0x10;
  hand->h = 0x10;
  hand->y0 = static_cast<s16>(first_y + 3);
  hand->u0 = atlas[0x5A];
  hand->code = static_cast<u8>((hand->code & 0xFD) | 1);
  hand->clut = 0x7FC4;
  hand->v0 = atlas[0x5B] != 1 ? atlas[0x5B] : 0;
  psyq::addPrim(s->ot_entry(0x817), hand);
  psyq::setlen(mode, 2);
  mode->code[0] = 0xE1000216;
  mode->code[1] = 0;
  psyq::addPrim(s->ot_entry(0x817), mode);
}

}  // namespace ss::game::field
