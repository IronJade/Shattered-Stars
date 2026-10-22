#include "game/battle/combat.h"

#include <array>
#include <memory>

#include "game/battle/bsfx.h"
#include "game/battle/camera.h"
#include "game/battle/render.h"
#include "game/battle/sprite.h"
#include "game/battle/stage.h"
#include "game/boot/rng.h"
#include "runtime/libgpu.h"
#include "runtime/runtime.h"

namespace ss::game::battle {
namespace {

void todo(const char* what) {
  runtime::Runtime::get().log_once(std::string("battle-combat-") + what,
                                   std::string("battle: not ported yet: ") + what);
}

bool overlaps(s32 c0, s32 c1, s32 b0, s32 b1) {
  return (c0 <= b0 && b0 < c1) || (c0 < b1 && b1 < c1) || (b0 <= c0 && c1 <= b1);
}

Unit* unit_slot(int i) {
  const BattleGlobals& g = bg();
  return i < 4 ? static_cast<Unit*>(g.party_units[i]) : g.enemy_units[i - 4];
}

s32 hp_percent(const Stats* s) { return s->hp_max != 0 ? (s->hp * 100) / s->hp_max : 0; }

// ---- Damage popups ------------------------------------------------------------------

struct DamagePopup : Task {
  u8 active = 0;     // +0x08
  u8 is_mp = 0;      // +0x09
  s16 age = 0;       // +0x0a
  s32 vx = 0;        // +0x0c
  s32 vy = 0;        // +0x10
  s32 floor_y = 0;   // +0x14
  s32 pos[4] = {};   // +0x18
  s32 value = 0;     // +0x28
  void update() override;  // damage_popup_update 0x8005d818
};
std::array<DamagePopup, 16> g_popups;  // Pool2C 0x8006fac0

void DamagePopup::update() {
  BattleGlobals& g = bg();
  ViewState& v = view();
  psx::Gte& gte = psyq::gte();
  // The point in view space through the render matrix, then projected (RTPS of the
  // origin with that translation).
  const s16 sx = static_cast<s16>((pos[0] - v.view_eye[0]) >> 12);
  const s16 sy = static_cast<s16>((pos[1] - v.view_eye[1]) >> 12);
  const s16 sz = static_cast<s16>((pos[2] - v.view_eye[2]) >> 12);
  psyq::SetRotMatrix(&v.render_view);
  gte.write_data(psx::Gte::VXY0, static_cast<u32>(static_cast<u16>(sx)) | static_cast<u32>(static_cast<u16>(sy)) << 16);
  gte.write_data(psx::Gte::VZ0, static_cast<u32>(static_cast<s32>(sz)));
  gte.execute(psx::Gte::kRtv0);
  psyq::MATRIX m = v.render_view;
  m.t[0] = static_cast<s32>(gte.read_data(psx::Gte::MAC1));
  m.t[1] = static_cast<s32>(gte.read_data(psx::Gte::MAC2));
  m.t[2] = static_cast<s32>(gte.read_data(psx::Gte::MAC3));
  psyq::SetTransMatrix(&m);
  psyq::SetRotMatrix(&v.render_view);
  gte.write_data(psx::Gte::VXY0, 0);
  gte.write_data(psx::Gte::VZ0, 0);
  gte.execute(psx::Gte::kRtps);
  const u32 sxy = gte.read_data(psx::Gte::SXY2);
  const s32 depth = static_cast<s32>(gte.read_data(psx::Gte::SZ3)) >> 5;
  const s16 x = static_cast<s16>(sxy & 0xFFFF), y = static_cast<s16>(sxy >> 16);
  s32 n = value;
  const bool heal = n < 0;
  if (heal) n = -n;
  int digits = 1;
  s32 cx = x - 5;
  if (n >= 1000) {
    digits = 4;
    cx = x + 0xE;
  } else if (n >= 100) {
    digits = 3;
    cx = x + 9;
  } else if (n > 9) {
    digits = 2;
    cx = x + 5;
  }
  if (depth <= 0xD) return;
  RenderContext* pool = g.prim_pool_a;
  const int ot = 0x61E - depth;
  const int glyphs = digits + is_mp;
  u32 colour = 0x65000000;
  if (age > 0xF) {
    const u32 c = static_cast<u32>((age - 0x10) * -8 + 0x80);
    colour = c | c << 8 | 0x66000000 | c << 16;
  }
  pool_a_bytes_used() += static_cast<u32>(glyphs * 0x14 + 0xC);
  for (int i = 0; i < glyphs; ++i) {
    auto* p = screen_alloc_prims<psyq::SPRT>(pool->arena, 1);
    if (p == nullptr) break;
    psyq::setSprt(p);
    std::memcpy(&p->r0, &colour, 4);
    p->x0 = static_cast<s16>(cx);
    p->y0 = y;
    const s32 u = (n % 10) * 10 + (i == digits ? 0x90 : 0x9A);
    p->u0 = static_cast<u8>(u);
    p->v0 = 0x63;
    p->clut = static_cast<u16>(0x7F80 + (heal ? 1 : 0));
    p->w = 10;
    p->h = 12;
    psyq::addPrim(&pool->ot_head[ot], p);
    cx -= 9;
    n /= 10;
  }
  if (auto* dm = screen_alloc_prims<psyq::DR_MODE>(pool->arena, 1)) {
    psyq::SetDrawMode(dm, 0, 0, 0x2F, nullptr);
    psyq::addPrim(&pool->ot_head[ot], dm);
  }
  if ((g.flags & 0xC) == 0) {
    const s32 t = g.frame_ticks;
    pos[1] += vy * t;
    vy += t * 0x1000;
    pos[0] += vx * t;
    if (floor_y <= pos[1]) {
      pos[1] = floor_y;
      vy = -(vy / 2);
    }
    if (++age > 0x1F) {
      active = 0;
      task_unregister(this);
    }
  }
}

}  // namespace

void damage_popups_reset() {
  for (auto& p : g_popups) {
    p.active = 0;
    p.age = 0;
  }
}

void damage_popup_spawn_at(const s32 pos[3], s32 value, s32 vx, int is_mp) {
  for (auto& p : g_popups) {
    if (p.active != 0) continue;
    p.active = 1;
    p.is_mp = static_cast<u8>(is_mp);
    p.value = value;
    p.pos[0] = pos[0];
    p.pos[1] = pos[1];
    p.pos[2] = pos[2];
    if (is_mp != 0 && bg().state < 4) p.pos[1] -= rng_between_fixed(0x30, 0x38);
    p.vx = vx;
    // damage_popup_start 0x8005d7d4.
    task_register(&p, 3);
    p.age = 0;
    p.vy = static_cast<s32>(0xFFFF7000);
    p.floor_y = p.pos[1];
    return;
  }
}

void damage_popup_spawn(Unit* u, s32 value, s32 vx, int is_mp) {
  if (value < 0 && u->status_test(3, 8) != 0 && is_mp == 0) value = 0;
  const s32 p[3] = {u->pos[0], u->pos[1] + u->size[1] * -0x4000, u->pos[2]};
  damage_popup_spawn_at(p, value, vx, is_mp);
}

void screen_shake(int amp_y, int period_y, int frames) {
  BattleGlobals& g = bg();
  g.shake_amp[0] = 0;
  g.shake_amp[1] = static_cast<s8>(amp_y);
  g.shake_period[0] = 0;
  g.shake_period[1] = static_cast<s8>(period_y);
  g.shake_phase[0] = g.shake_phase[1] = 0;
  g.shake_frames = static_cast<s16>(frames);
}

void screen_shake2(int amp_x, int amp_y, int period_x, int period_y, int frames) {
  BattleGlobals& g = bg();
  g.shake_amp[0] = static_cast<s8>(amp_x);
  g.shake_amp[1] = static_cast<s8>(amp_y);
  g.shake_period[0] = static_cast<s8>(period_x);
  g.shake_period[1] = static_cast<s8>(period_y);
  g.shake_phase[0] = g.shake_phase[1] = 0;
  g.shake_frames = static_cast<s16>(frames);
}

Vec4 vec_normalize(const s32 v[3]) {
  const s32 x = static_cast<s16>(v[0] >> 12), y = static_cast<s16>(v[1] >> 12), z = static_cast<s16>(v[2] >> 12);
  s32 d = square_root0(x * x + y * y + z * z);
  if (d == 0) d = 1;
  return Vec4{v[0] / d, v[1] / d, v[2] / d, 0};
}

int party_count_standing() {
  int n = 0;
  for (const PartyUnit* u : bg().party_units)
    if (!unit_is_down(u)) ++n;
  return n;
}

int party_count_fled() {
  int n = 0;
  for (const PartyUnit* u : bg().party_units)
    if (u != nullptr && u->removed == 2) ++n;
  return n;
}

int collider_attack_overlaps(const Collider* atk, Unit* units[8], Vec4 points[8], int vs_party) {
  int count = 0;
  if ((atk->box_mask & 0xC) == 0) return 0;
  const int first = vs_party == 0 ? 4 : 0;
  const int last = vs_party == 0 ? 0xC : 4;
  const bool on[2] = {((atk->box_mask >> 2) & 1) != 0, ((atk->box_mask >> 3) & 1) != 0};
  for (int i = first; i < last; ++i) {
    Unit* u = unit_slot(i);
    if (unit_is_down(u)) continue;
    bool hit = false;
    for (int b = 0; b < 2 && !hit; ++b) {
      if (((u->box_mask >> b) & 1) == 0) continue;
      const s32* bmin = u->body_min[b];
      const s32* bmax = u->body_max[b];
      for (int a = 0; a < 2; ++a) {
        if (!on[a]) continue;
        const s32* amin = atk->attack_min[a];
        const s32* amax = atk->attack_max[a];
        if (!overlaps(bmin[0], bmax[0], amin[0], amax[0])) continue;
        if (!overlaps(bmin[2], bmax[2], amin[2], amax[2])) continue;
        if (!overlaps(bmin[1], bmax[1], amin[1], amax[1])) continue;
        // Halfway between the attack box's centre and the body box's.
        const s32 hb = (bmax[0] - bmin[0]) / 2;
        Vec4& p = points[count];
        p.x = (((amax[0] - amin[0]) / 2 + amin[0]) - (hb + bmin[0])) / 2 + hb + bmin[0];
        p.z = atk->pos[2];
        const s32 cy = (bmax[1] - bmin[1]) / 2 + bmin[1];
        p.y = (((amax[1] - amin[1]) / 2 + amin[1]) - cy) / 2 + cy;
        units[count] = u;
        ++count;
        hit = true;
        break;
      }
    }
  }
  return count;
}

bool unit_attack_hits(Unit* atk, Unit* def) {
  if (def->special_timer != 0) return true;
  if (def->facing[0] > 0 && atk->facing[0] > 0) return true;  // from behind
  if (def->facing[0] < 0 && atk->facing[0] < 0) return true;
  if (def->flying == 0 && def->airborne != 0) return true;
  if (atk->status_test(1, 0x2000) != 0 || atk->status_test(2, 0x40) != 0) return true;
  if ((def->st()->status & 0xE6) != 0) return true;
  if (atk->super_armour != 0) return true;
  s32 eva = def->st()->eva[2];
  s32 acc = atk->st()->acc[2];
  const u32 s25 = static_cast<u32>(atk->skill_level(0x25));
  const u32 r1 = rng_range(0x1E);
  if (r1 < s25) acc = static_cast<s32>(acc * static_cast<s32>(s25 + 10)) / 10;
  const u32 s2a = static_cast<u32>(def->skill_level(0x2A));
  const u32 r2 = rng_range(0x1E);
  if (r2 < s2a) eva = static_cast<s32>(eva * static_cast<s32>(s2a + 10)) / 10;
  const s32 r3 = static_cast<s32>(rng_range(0x100));
  const bool hit = eva - acc <= r3;
  if (r1 < s25 && hit) effect_skill_popup(atk, 0x25);
  if (r2 >= s2a) return hit;
  if (hit) return true;
  effect_skill_popup(def, 0x2A);
  return false;
}

int unit_calc_damage(Unit* atk, Unit* def, int power, int magic, u16 elements) {
  if (def->special_timer != 0) return 0;
  if (def->status_test(0, 0x40000000) != 0 && rng_range(100) <= 4) return 0;
  Stats* ds = def->st();
  s32 dfn = ds->def[2] + (ds->def[2] >> 1);
  const u32 s29 = static_cast<u32>(def->skill_level(0x29));
  if (rng_range(0x28) < s29) {
    effect_skill_popup(def, 0x29);
    dfn = static_cast<s32>(dfn * static_cast<s32>(s29 + 10)) / 10;
  }
  if (def->status_test(0, 4) != 0) dfn += (dfn * (((hp_percent(ds) + 9) / 10) * -10 + 100)) / 100;
  if (magic == 0) {
    const u32 s21 = static_cast<u32>(atk->skill_level(0x21));
    if (rng_range(0x32) < s21 && bg().config[0x182] != 1) {
      effect_skill_popup(atk, 0x21);
      dfn = 0;
    }
  } else {
    dfn = 0;
  }
  s32 at = atk->st()->atk[2];
  if (dfn != 0 && magic == 0) {
    const u32 s26 = static_cast<u32>(atk->skill_level(0x26));
    if (rng_range(0x28) < s26) {
      effect_skill_popup(atk, 0x26);
      at = static_cast<s32>(at * static_cast<s32>(s26 + 0x1E)) / 0x1E;
    }
  }
  if (atk->status_test(0, 1) != 0) {
    // Low HP raises ATK by table 0x8006cdd4 % at the thresholds 0x8006cdc8.
    s32 add = 0;
    for (u32 i = 0; i < 0xB; ++i) {
      if (hp_percent(atk->st()) <= static_cast<s32>(battle_data(0x8006CDC8)[i])) {
        add = at * static_cast<s32>(battle_data(0x8006CDD4)[i]);
        break;
      }
    }
    at += add / 100;
  }
  if (atk->status_test(0, 2) != 0) {
    // vtable[15]: no PartyUnit override counts anything (the bonus is dead).
    const s32 k = 0;
    s32 add = at * k;
    if (9 < k) add = at * 9;
    at += add / 10;
  }
  if (magic == 0) power = ((at - dfn) * power) >> 8;
  if (power < 0) power = 0;
  if (magic == 0) elements = static_cast<u16>(elements | atk->st()->weapon_element);
  if (def->status_test(2, 0x800) != 0 && ds->hp < 100) power = 10;
  for (int i = 0; i < 10; ++i)
    if ((elements >> i) & 1) power = (power * static_cast<s8>(battle_data(0x8006CDC0)[ds->element[i]])) / 2;
  s32 var = 0;
  if (9 < power) {
    s32 r = power / 10;
    if (r < 0) r = -r;
    var = static_cast<s32>(rng_range(static_cast<u32>(r)));
  }
  if (power < 0) var = -var;
  power += var;
  if (def->status_test(1, 0x8000) != 0) power += (power * rng_between(-0x4B, 100)) / 100;
  s32 dmg = power;
  if (9999 < power) dmg = 9999;
  if (power < -9999) dmg = -9999;
  if (def->is_enemy == 1 && (ds->id == 0x84 || ds->id == 0xA5)) dmg = 1;
  return dmg;
}

void unit_show_guts_effect(Unit* src, Unit* at, int offensive) {
  int sfx;
  if (offensive == 0) sfx = at->is_enemy != 0 ? 10 : 0xB;
  else sfx = at->is_enemy != 0 ? 0xB : 10;
  bsfx_play_at(sfx, src);
  Sprite* s = sprite_spawn_a(0x12, 0x10, nullptr);
  if (s == nullptr) return;
  sprite_set_pos(s, at->pos[0], at->pos[1] + at->size[1] * -0x3000, at->pos[2]);
  s->vel[1] = static_cast<s32>(0xFFFFC000);
  s->accel[1] = 0x200;
  s->layer = 2;
  s->keep_screen = 1;
}

void unit_damage_hp(Unit* u, s32 dmg) {
  if (dmg < 0 && u->status_test(3, 8) != 0) return;
  Stats* s = u->st();
  if (s->status & 1) return;
  s->hp -= dmg;
  if (s->hp_max < s->hp) s->hp = s->hp_max;
  if (0 < s->hp) return;
  if (u->status_test(2, 0x400) != 0) {
    s32 q = s->hp_max;
    if (q < 0) q += 3;
    if ((q >> 2) <= dmg) {
      u->clear_status(2, 0x400);
      s->hp = 1;
      return;
    }
  }
  s32 guts = s->guts[2] - 0x4B;
  if (guts < 0) guts = 0;
  if (static_cast<u32>(guts) <= rng_range(0x100)) {
    s->hp = 0;
    u->death = 1;
    return;
  }
  unit_show_guts_effect(u, u, 0);
  s->hp = 1;
}

void unit_damage_mp(Unit* u, s32 dmg) {
  Stats* s = u->st();
  s->mp = static_cast<s16>(static_cast<u16>(s->mp) - dmg);
  if (s->mp_max < s->mp) s->mp = s->mp_max;
  if (s->mp < 1) s->mp = 0;
}

int unit_attack_targets(Unit* atk, int count, Unit** targets, const Vec4* points, int power, int angle,
                        u16 elements) {
  BattleGlobals& g = bg();
  int landed = 0;
  if (atk->status_test(1, 0x200000) != 0) {
    damage_popup_spawn(atk, 10, 0, 0);
    unit_damage_hp(atk, 10);
  }
  if (atk->status_test(1, 0x1000000) != 0 || atk->status_test(2, 0x40) != 0) unit_damage_mp(atk, 2);
  for (int n = 0; n < count; ++n) {
    Unit* t = targets[n];
    s32 kx, kz;
    if (angle == 9999) {
      kx = t->pos[0] < atk->pos[0] ? -0x1000 : 0x1000;
      kz = 0;
    } else {
      kx = bcos(static_cast<u32>(angle));
      kz = bsin(static_cast<u32>(angle));
    }
    // Knockback: the attacker's power (+0x52) against the target's resistance (+0x58).
    s32 kb = atk->st()->stat4e[2] - t->st()->stat54[2];
    if (kb < 1) kb = 1;
    bool crit = false;
    if (atk->special_timer == 0) {
      crit = rng_range(0x200) < static_cast<u32>(static_cast<s32>(atk->st()->crit));
      if (!crit && rng_range(0x400) < static_cast<u32>(static_cast<s32>(atk->st()->guts[2]))) {
        crit = true;
        unit_show_guts_effect(atk, atk, 1);
      }
      if (atk->status_test(2, 0x4000000) != 0) {
        crit = true;
        atk->clear_status(2, 0x4000000);
      }
    }
    u32 stun_chance = 0x4000;
    s32 dmg = 0;
    t->last_attacker = atk;
    g.last_attacker = atk;
    const bool hit = unit_attack_hits(atk, t);
    if (hit) dmg = unit_calc_damage(atk, t, crit ? power + (power >> 1) : power, 0, elements);
    if (atk->half_damage != 0) dmg >>= 1;
    if (t->status_test(0, 0x80000) != 0 && rng_range(100) < 10) dmg <<= 1;
    if (atk->status_test(1, 0x2000000) != 0) dmg <<= 1;
    if (atk->status_test(1, 0x20000000) != 0) dmg = 200;
    if (t->status_test(0, 0x4000000) != 0) t->accessory_proc(0x6F, 0, 0x4000000, 5);
    int guard = 0;
    bool missed = false;
    if (!hit) {
      t->reaction = 2;
      t->knockback[0] = kx * kb;
      t->knockback[1] = kz * kb;
      t->knockback[2] = 0;
      bsfx_play_at(atk->sfx_guard, atk);
      missed = true;
      atk->attack_recovery = atk->recovery;
    } else {
      if (dmg != 0 && t->status_test(0, 0x10000000) != 0) {
        t->accessory_proc(0x71, 0, 0x10000000, 10);
        dmg /= 10;
      }
      if (dmg != 0 && t->status_test(1, 0x80) != 0) {
        t->accessory_proc(0x81, 1, 0x80, 5);
        dmg /= 5;
      }
      if (dmg != 0) {
        const u32 s22 = static_cast<u32>(atk->skill_level(0x22));
        if (rng_range(0x28) < s22) {
          effect_skill_popup(atk, 0x22);
          kb += 4;
          stun_chance = 22000;
        }
        const u32 s28 = static_cast<u32>(t->skill_level(0x28));
        guard = rng_range(0x1E) < s28 ? 1 : 0;
        if (guard != 0) stun_chance /= 2;
        t->reaction = 1;
        t->knockback[0] = kx * kb * 2;
        t->knockback[1] = kz * kb * 2;
        t->knockback[2] = 0;
        if (rng_range(0x400) < static_cast<u32>(static_cast<s32>(t->st()->guts[2]))) {
          dmg /= 2;
          unit_show_guts_effect(atk, t, 0);
        }
        if (atk->weapon_status != 0) {
          if (rng_range(guard != 0 ? 0x200 : 0x100) < atk->weapon_chance) {
            bool inflict = true;
            if (t->status_test(0, 0x20000) != 0) {
              if (rng_range(100) <= 0x45) {
                t->accessory_proc(0x5B, 0, 0x20000, 0xF);
                inflict = false;
              }
            }
            if (inflict) {
              if (t->status_test(1, 0x1000) != 0 && static_cast<u32>(rng_next() & 0x7FFF) < 0x4000) {
                t->accessory_proc(0x8C, 1, 0x1000, 10);
              } else if (t->status_test(3, 0x4000) == 0) {
                if ((atk->weapon_status & 1) && t->status_test(0, 0x100) != 1) {
                  if (t->is_enemy == 0 || t->resist_roll(0, 0x19) != 0) t->st()->status |= 8;
                }
                if ((atk->weapon_status & 2) && t->status_test(0, 0x200) != 1) {
                  if (t->is_enemy == 0) {
                    t->st()->status |= 2;
                  } else if (t->resist_roll(0, 0x19) != 0) {
                    t->st()->hp = 0;
                    t->death = 1;
                  }
                }
                if ((atk->weapon_status & 4) && t->status_test(0, 0x400) != 1) {
                  if (t->is_enemy == 0) {
                    t->st()->status |= 4;
                  } else if (t->resist_roll(0, 0x19) != 0) {
                    t->st()->hp = 0;
                    t->death = 1;
                  }
                }
              }
            }
          } else if (guard != 0) {
            guard = 2;
          }
        }
        landed = 1;
      }
      if (bobj_on_screen(t)) screen_shake(2, 2, 10);
      s32 d = dmg;
      if (dmg != 0 && t->status_test(0, 0x200000) != 0) {
        d = -dmg;
        t->accessory_proc(0x66, 0, 0x200000, 10);
      }
      if (t->status_test(1, 0x40) != 0 && rng_range(100) < 10) d = -d;
      dmg = d;
      if (9999 < d) dmg = 9999;
      if (d < -9999) dmg = -9999;
      if (dmg != 0 && t->status_test(1, 0x4000) != 0 && rng_range(100) < 10) {
        damage_popup_spawn(atk, dmg, 0, 0);
        unit_damage_hp(atk, dmg);
        dmg = 0;
      }
      if (dmg != 0 && t->status_test(1, 0x10000) != 0 && rng_range(100) < 5) {
        damage_popup_spawn(atk, dmg, 0, 0);
        unit_damage_hp(atk, dmg);
        dmg = 0;
      }
      if (dmg != 0 && t->status_test(1, 0x20000) != 0 && rng_range(100) < 10) {
        // The hit is passed to another party member.
        for (int k = 0; k < 100; ++k) {
          Unit* o = g.party_units[rng_range(4)];
          if (o != t && !unit_is_down(o)) {
            targets[n] = t = o;
            break;
          }
        }
      }
      if (dmg != 0 && t->status_test(2, 0x10000) != 0) {
        dmg = 0;
        t->clear_status(2, 0x10000);
      }
      s32 kq = kx * kb;
      if (kq < 0) kq += 3;
      kq >>= 2;
      if (atk->hits_mp == 0) {
        bool kill = false;
        bool apply = true;
        if (atk->status_test(0, 0x10) != 0) {
          // Drain: the attacker heals half.
          damage_popup_spawn(atk, -dmg / 2, kq, 0);
          unit_damage_hp(atk, -dmg / 2);
          if (t->race() == 1) kill = true;
        }
        if (!kill) {
          if (atk->status_test(2, 0x1000000) != 0 && t->race() == 2) {
            kill = true;
          } else if (atk->status_test(1, 0x400000) != 0 && t->resist_roll(0, 0xF) != 0) {
            kill = true;
          } else if (atk->status_test(2, 0x20) != 0 && t->resist_roll(0, 0xFF) != 0) {
            atk->clear_status(2, 0x20);
            kill = true;
          } else {
            if (atk->status_test(1, 0x4000000) != 0) {
              if (t->resist_roll(0, 0x28) != 0) {
                kill = true;
                apply = false;
              } else {
                dmg = 0;
              }
            }
            if (!kill && apply) {
              unit_damage_hp(t, dmg);
              damage_popup_spawn(t, dmg, kq, 0);
            }
          }
        }
        if (kill) unit_damage_hp(t, 2000000000);
      } else {
        damage_popup_spawn(t, dmg, kq, 1);
        unit_damage_mp(t, dmg);
      }
      if (t->status_test(0, 8) != 0) {
        damage_popup_spawn(t, -(dmg / 5), 0, 1);
        unit_damage_mp(t, -(dmg / 5));
      }
      if (t->status_test(2, 0x8000) != 0 && t->resist_roll(0, 0x1A) != 0) unit_damage_hp(atk, 2000000000);
      if (atk->status_test(1, 0x80000000) != 0 && t->resist_roll(1, 0x14) != 0) t->status_bits |= 0x20;
      if (atk->status_test(2, 1) != 0 && t->resist_roll(1, 0x14) != 0) t->status_bits |= 8;
      if (atk->status_test(2, 2) != 0 && t->resist_roll(1, 0x14) != 0) t->status_bits |= 0x40;
      atk->last_damage = dmg;
    }
    atk->hit_landed = 1;
    if (Sprite* spark = sprite_spawn_a(0x11, 1, nullptr)) {
      if (atk->half_damage == 0) {
        sprite_set_pos_v(spark, points[n].data());
      } else {
        const s32 jx = rng_spread_fixed(0x50);
        const s32 x = points[n].x;
        const s32 jy = rng_spread_fixed(0x50);
        sprite_set_pos(spark, x + jx, points[n].y + jy, points[n].z);
      }
      spark->layer = 2;
      if (missed) {
        spark->clut_offset = 10;
      } else if (crit) {
        bsfx_play_at(atk->sfx_crit, atk);
        ViewState& v = view();
        v.stage_colour[0] = v.stage_colour[1] = v.stage_colour[2] = 0x200;
        g.flash_active = 1;
        g.flash_delay = 2;
        g.flash_speed = 0x20;
        if (bobj_on_screen(atk)) {
          screen_shake2(4, 4, 4, 6, 0xE);
          g.zoom_kick = 5000;
          g.zoom_kick_decay = 200;
        }
        spark->scale_vel[0] = 0x60;
        spark->scale_vel[1] = 0x60;
        if (static_cast<u32>(rng_next() & 0x7FFF) < stun_chance && dmg != 0) {
          unit_apply_stun(t);
        } else if (guard == 1) {
          guard = 2;
        }
      } else {
        bsfx_play_at(atk->sfx_hit, atk);
      }
    }
    if (guard == 2) effect_skill_popup(t, 0x28);
  }
  return landed;
}

int unit_start_reaction(Unit* u) {
  BattleGlobals& g = bg();
  if (u->death == 0) {
    if (u->reaction == 0) return 0;
    if (u->camera_focus != 0 && g.camera != nullptr) camera_set_lookat_offset(g.camera, kZeroVector);
    u->interrupt();
    unit_release_spell_data(u);
    u->ignore_units = 0;
    u->target = nullptr;
    u->knockback_ticks = 0;
    if (u->super_armour != 0) {
      // Super armour: no flinch, only a shake.
      s32 k = u->knockback[0];
      u->reaction = 0;
      u->colour_mode = 0;
      u->blend = 0;
      if (k < 0) k = -k;
      u->shake_amp = static_cast<s16>((k >> 12) << 3);
      if (0x10 < static_cast<s16>((k >> 12) << 3)) u->shake_amp = 0x10;
      u->shake_frames = 0x14;
      return 0x14;
    }
    if (u->reaction == 3) u->reaction = 2;
    u->action = static_cast<u8>(u->reaction + 2);
    u->vel[0] = u->knockback[0];
    u->vel[1] = u->knockback[1];
    u->vel[2] = u->knockback[2];
    u->extra34 = u->knockback[3];
    const Vec4 n = vec_normalize(u->knockback);
    u->knockback[0] = n.x;
    u->knockback[1] = n.y;
    u->knockback[2] = n.z;
    u->knockback[3] = n.w;
  } else {
    u->target = nullptr;
    if (u->camera_focus != 0 && g.camera != nullptr) camera_set_lookat_offset(g.camera, kZeroVector);
    u->interrupt();
    u->release_effects();
    u->ignore_units = 0;
    if (u->death == 1) {
      u->action = 5;
    } else if (u->death == 2) {
      u->action = 0;
    } else {
      u->action = 6;
      u->counters[1] = 0;
      u->counters[2] = u->death - 3;
    }
    u->st()->hp = 0;
    u->st()->status |= 1;
    u->death = 0;
  }
  u->action_kind = 0;
  bobj_anim_reset(u);
  u->reaction = 0;
  u->counters[0] = 0;
  u->jumping = 0;
  u->colour_mode = 0;
  u->blend = 0;
  return 1;
}

bool unit_knockback_slide(Unit* u) {
  BattleGlobals& g = bg();
  u->release_effects();
  if (u->counters[0] != 0) {
    const s32 c = u->counters[0] + g.frame_ticks;
    u->counters[0] = c;
    return 0xC < c;
  }
  u->jumping = 0;
  Unit* blocker = unit_try_move_scaled(u, u->vel, 7);
  if (blocker == nullptr) {
    if (u->vel[1] != 0 && u->flying == 0 && u->airborne == 0) u->airborne = 1;
    bobj_move(u, u->vel);
    const s32 k = u->knockback_ticks + g.frame_ticks;
    u->knockback_ticks = static_cast<s16>(k);
    if (static_cast<s16>(k) < 0x259) {
      const s32 a0 = ratan2(u->vel[2], u->vel[0]);
      const s32 div = u->airborne != 0 ? 2 : 1;
      const s32 t = g.frame_ticks;
      u->vel[0] -= (u->knockback[0] * t) / div;
      u->vel[1] -= (u->knockback[1] * t) / div;
      u->vel[2] -= (u->knockback[2] * t) / div;
      s32 d = a0 - ratan2(u->vel[2], u->vel[0]);
      if (d < 0) d = -d;
      if (d < 0x21) return false;
    }
  } else if (blocker != kWallBlock && (0x200 < u->knockback[0] || 0x200 < u->knockback[2])) {
    const s32 k0 = u->knockback[0];
    if (k0 == 0 || (0 < k0 && u->pos[0] < blocker->pos[0]) || (k0 < 0 && blocker->pos[0] < u->pos[0])) {
      // Knocked into another unit: it is bumped too.
      blocker->reaction = 3;
      blocker->knockback[1] = 0;
      blocker->knockback[0] = u->vel[0] > 0x200 ? u->vel[0] : 0;
      blocker->knockback[2] = u->vel[2] > 0x200 ? u->vel[2] : 0;
    }
  }
  u->counters[0] = 1;
  return false;
}

int unit_resist_roll(Unit* u, int kind, int chance) {
  if ((u->special_timer == 0 || kind != 0) && !unit_is_down(u)) {
    const s32 diff = chance - static_cast<s32>(u->st()->resist[kind & 1]);
    if (0 < diff) {
      bool blocked = false;
      if (kind == 0 && u->status_test(2, 0x200) != 0) blocked = static_cast<u32>(rng_next() & 0x7FFF) < 0x4000;
      if (!blocked) return rng_range(0xFF) < static_cast<u32>(diff) ? 1 : 0;
    }
  }
  return 0;
}

void unit_update_vanish(Unit* u) {
  BattleGlobals& g = bg();
  if ((g.flags & 0xBF) != 0) return;
  if (u->counters[2] < 2) {
    s32 sp = u->counters[1] << 9;
    if (0x8000 < sp) sp = 0x8000;
    const Vec4 v = vec_velocity_toward(u->pos, u->vanish_target, sp >> 12);
    u->vel[0] = v.x;
    u->vel[1] = v.y;
    u->vel[2] = v.z;
    u->extra34 = v.w;
    u->airborne = 1;
    bobj_move(u, u->vel);
    if (u->counters[2] == 1) u->roll = static_cast<s16>((u->roll + g.frame_ticks * 0x14) & 0xFFF);
    if (vec_distance_fx(u->pos, u->vanish_target) < 0x10000 || (2 < g.state && g.end_countdown < 8)) {
      if (u->is_enemy == 0) {
        u->action = 5;
      } else {
        u->removed = 1;
        --g.enemy_count;
        u->action = 0;
      }
      u->scale_x = u->scale_y = 0x1000;
    } else {
      const s32 c = u->counters[1] + g.frame_ticks;
      s32 sc = c * -0x20 + 0x1000;
      u->counters[1] = c;
      if (sc < 0) sc = 0;
      u->scale_x = u->scale_y = sc;
    }
  } else if (u->counters[2] == 2) {
    u->blend = 2;
    u->colour_mode = 2;
    u->rgb[0] = static_cast<u8>('`' - static_cast<u8>(u->counters[1]));
    u->anim = 1;
    const s32 c = u->counters[1] + g.frame_ticks;
    u->counters[1] = c;
    if (0x60 < c || (2 < g.state && g.end_countdown < 8)) {
      u->removed = 1;
      --g.enemy_count;
    }
  }
}

void unit_apply_stun(Unit* u) {
  if (u->status_test(0, 0x1000000) != 0) return;
  // TODO(port): the kind-1 Shot (stun stars, 120 frames) that keeps status 0x40 set.
  todo("unit_apply_stun: the stun Shot (kind 1)");
}


int Unit::start_reaction() { return unit_start_reaction(this); }
void Unit::update_vanish() { unit_update_vanish(this); }
int Unit::resist_roll(int kind, int chance) { return unit_resist_roll(this, kind, chance); }

void unit_release_spell_data(Unit* u) {
  if (u->spell != 0 && u->special_timer == 0) {
    // spell_data_cancel_owner 0x80048a20: the spell-data cache is not ported (spells).
    u->spell = 0;
  }
}

}  // namespace ss::game::battle
