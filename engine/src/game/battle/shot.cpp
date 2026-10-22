#include "game/battle/shot.h"

#include <memory>
#include <string>

#include "game/battle/bobj.h"
#include "game/battle/camera.h"
#include "game/battle/combat.h"
#include "game/battle/render.h"
#include "game/battle/sprite.h"
#include "runtime/runtime.h"

namespace ss::game::battle {
namespace {

constexpr int kShots = 160;
std::unique_ptr<Shot[]> g_pool;  // g_pool_13c 0x8006fac4

// Kind 1 (0x8005566c): three stun stars circling the head of +0x114; keeps the target's
// stun bit (stats +2 0x40) set for +0x128 ticks.
void shot_k01_stun_orbit(Shot* s) {
  BattleGlobals& g = bg();
  shot_use_anims_f928(s);
  s->anim = 0x12;
  Unit* t = s->target;
  if (t == nullptr) {
    shot_kill(s);
    return;
  }
  if (s->p128 < 1 || unit_is_down(t)) {
    t->stats[2] = static_cast<u8>(t->stats[2] & 0xBF);
    if (t->is_enemy == 0) static_cast<PartyUnit*>(t)->busy = 0;
    shot_kill(s);
    return;
  }
  t->stats[2] = static_cast<u8>(t->stats[2] | 0x40);
  const s32 cx = t->pos[0], cz = t->pos[2];
  const s32 rx = t->size[0] * 2 + 0x10, rz = t->size[2] * 2;
  s->pos[0] = cx + bcos(static_cast<u32>(s->p12c)) * rx;
  s->pos[2] = cz + bsin(static_cast<u32>(s->p12c)) * rz;
  s->pos[1] = t->pos[1] + t->size[1] * -0x4000;
  // (The console draws the first star, then twice at 120 degrees and once at 240: four
  // draws, each advancing the animation.)
  for (int deg = 0x78; deg < 0xF1; deg += 0x78) {
    bobj_project(s);
    bobj_anim_draw(s, 0);
    const s32 a = (deg * 0x1000) / 0x168;
    s->pos[0] = cx + bcos(static_cast<u32>(s->p12c + a)) * rx;
    s->pos[2] = cz + bsin(static_cast<u32>(s->p12c + a)) * rz;
    bobj_project(s);
    bobj_anim_draw(s, 0);
  }
  if ((g.flags & 8) == 0) {
    const u32 a = static_cast<u32>(s->p12c + (g.frame_ticks * 0xA000) / 0x168) & 0xFFF;
    s->p12c = static_cast<s32>(a);
    s->roll = static_cast<s16>(a);
    if (g.flags == 0) s->p128 -= g.frame_ticks;
  }
}

// Kinds 0x21 / 0x33 (0x8005b530): the handler at +0x138.
void shot_k21_callback(Shot* s) {
  if (s->handler != nullptr) s->handler(s);
}

void unported_kind(Shot* s) {
  runtime::Runtime::get().log_once("battle-shot-" + std::to_string(s->kind),
                                   "battle: Shot kind " + std::to_string(s->kind) + " not ported (removed)");
  shot_kill(s);
}

}  // namespace

void shot_pool_create() {
  g_pool = std::make_unique<Shot[]>(kShots);
  for (int i = 0; i < kShots; ++i) g_pool[i].kind = 0;
}

void shot_pool_destroy() { g_pool.reset(); }

Shot* shot_at(int index) { return g_pool && index >= 0 && index < kShots ? &g_pool[index] : nullptr; }

void shot_pool_update() {
  if (!g_pool) return;
  for (int i = 0; i < kShots; ++i)
    if (g_pool[i].kind != 0) g_pool[i].update();
}

void Shot::update() {
  BattleGlobals& g = bg();
  if (g.effects_suppressed != 0 && kind != 0x21 && command != 0xA5) return;
  switch (kind) {
    case 1: shot_k01_stun_orbit(this); break;
    case 0x21:
    case 0x33: shot_k21_callback(this); break;
    default: unported_kind(this); break;
  }
}

Shot* shot_spawn(int kind) {
  if (!g_pool) return nullptr;
  for (int i = 0; i < kShots; ++i) {
    Shot* s = &g_pool[i];
    if (s->kind != 0) continue;
    shot_reset(s);
    s->kind = static_cast<u16>(kind);
    return s;
  }
  return nullptr;
}

int shot_count_free() {
  if (!g_pool) return 0;
  int n = 0;
  for (int i = 0; i < kShots; ++i)
    if (g_pool[i].kind == 0) ++n;
  return n;
}

void shot_reset(Shot* s) {
  bobj_anim_reset(s);
  s->p128 = s->p12c = s->p130 = s->p134 = s->p138 = 0;
  s->handler = nullptr;
  for (s32& v : s->user) v = 0;
  s->lift = 0;
  s->animset = nullptr;
  s->blend = 0;
  s->clut_offset = 0;
  s->loop_limit = 0;
  s->texcache = nullptr;
  s->colour_mode = 0;
  s->owner = nullptr;
  s->target = nullptr;
  s->scale_y = s->scale_x = 0x1000;
  s->facing[0] = s->facing[1] = s->facing[2] = 0;
  s->vel[0] = s->vel[1] = s->vel[2] = 0;
  s->point[0] = s->point[1] = s->point[2] = 0;
  s->roll = 0;
  s->aux = 0;
  s->command = 0;
  s->pool = bg().prim_pool_a;
}

void shot_kill(Shot* s) {
  BattleGlobals& g = bg();
  if (s == nullptr) return;
  s->kind = 0;
  if (g.camera == nullptr) return;
  camera_set_focus_timer(g.camera, s, 0x14);
  if (g.camera->mode == 0xC && g.camera->target == s) camera_reset_mode(g.camera);
}

void shot_use_anims_f928(BObj* o) { bobj_share_sprite(bg().sprite_set_a.get(), o); }
void shot_use_anims_f920(BObj* o) { bobj_share_sprite(bg().sprite_bank_fx.get(), o); }

Sprite* shot_spawn_sprite(Shot* s, int anim) { return sprite_spawn_from(anim, 0, s, nullptr); }
Sprite* shot_spawn_sprite_at(Shot* s, int anim, int arg) { return sprite_spawn_from(anim, arg, s, nullptr); }

int shot_hit_melee(Shot* s, int power, int vs_party) {
  Unit* units[8] = {};
  Vec4 points[8] = {};
  const int n = collider_attack_overlaps(s, units, points, vs_party);
  if (n == 0) return 0;
  s->hit_landed = 1;
  s32 dir = s->vel[0];
  if (dir == 0) dir = s->owner->facing[0];
  const int r = unit_attack_targets(s->owner, n, units, points, power, dir < 0 ? 0x800 : 0, 0) + 1;
  s->owner->attack_recovery = 0;
  return r;
}

void shot_place_at_unit(Shot* s, const BObj* u, s32 dx, s32 dy, s32 dz) {
  s->pos[0] = u->pos[0] + bobj_facing_fx(u, dx);
  s->pos[1] = u->pos[1] + dy * -0x1000;
  s->pos[2] = u->pos[2] + dz;
  s->facing[0] = u->facing[0];
}

int shot_draw_and_box(Shot* s, int size, u32 shadow_colour) {
  bobj_project(s);
  const int frame = bobj_anim_draw(s, bg().flags);
  s32 r = s->ext_x1;
  if (r < 0) r = -r;
  r *= size;
  bobj_draw_shadow(s, -r, r, -r, r, shadow_colour);
  return frame;
}

int shot_draw_and_box_ground(Shot* s, int size, u32 shadow_colour) {
  bobj_project(s);
  const int frame = bobj_anim_draw(s, bg().flags);
  s32 h = bobj_ground_height(s) - s->pos[1];
  if (h < 0) h += 0xFFFF;
  s32 r = s->ext_x1;
  if (r < 0) r = -r;
  r = r * size - (h >> 16);
  if (r < 0x10) r = 0x10;
  bobj_draw_shadow(s, -r, r, -r, r, shadow_colour);
  return frame;
}

int shot_fade_out(Shot* s) {
  BattleGlobals& g = bg();
  if (kStateOver <= g.state) {
    if (s->p12c == 0) s->p12c = 1;
  }
  if (s->p12c == 0) return 0;
  if ((g.flags & 0xBF) != 0) return 0;
  s->colour_mode = 2;
  s->rgb[0] = static_cast<u8>(s->p12c * -4 - 0x80);
  s->p12c += g.frame_ticks;
  if (0x18 < s->p12c) {
    shot_kill(s);
    return 1;
  }
  return 0;
}

Shot* shot_spawn_callback(ShotFn fn) {
  Shot* s = shot_spawn(0x33);
  if (s != nullptr) s->handler = fn;
  return s;
}

Shot* shot_spawn_callback_always(ShotFn fn) {
  Shot* s = shot_spawn(0x21);
  if (s != nullptr) s->handler = fn;
  return s;
}

int shot_kind_absent(int kind) {
  if (!g_pool) return 1;
  for (int i = 0; i < kShots; ++i)
    if (g_pool[i].kind == static_cast<u16>(kind)) return 0;
  return 1;
}

int shot_kind21_absent(ShotFn fn) {
  if (!g_pool) return 1;
  for (int i = 0; i < kShots; ++i)
    if (g_pool[i].kind == 0x21 && g_pool[i].handler == fn) return 0;
  return 1;
}

// ---- unit_apply_stun (0x80055588) --------------------------------------------------------

void unit_apply_stun(Unit* u) {
  if (u->status_test(0, 0x1000000) != 0) return;
  if (g_pool) {
    for (int i = 0; i < kShots; ++i) {
      Shot* s = &g_pool[i];
      if (s->kind == 1 && s->target == u) {
        s->p128 = 0x78;
        return;
      }
    }
  }
  Shot* s = shot_spawn(1);
  if (s == nullptr) return;
  s->target = u;
  s->p128 = u->status_test(0, 0x800000) != 0 ? 0x3C : 0x78;
}

}  // namespace ss::game::battle
