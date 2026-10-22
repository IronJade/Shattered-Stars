#include "game/battle/sprite.h"

#include <memory>

#include "game/battle/render.h"
#include "game/battle/unit.h"
#include "game/boot/rng.h"

namespace ss::game::battle {
namespace {

constexpr int kSprites = 0x128;
std::unique_ptr<Sprite[]> g_pool;  // g_pool_b8 0x8006fabc

bool same_layout(const BObj* a, const BObj* b) {
  const AnimSet* sa = a != nullptr ? a->animset : nullptr;
  const AnimSet* sb = b != nullptr ? b->animset : nullptr;
  const u8* la = sa != nullptr ? sa->layout : nullptr;
  const u8* lb = sb != nullptr ? sb->layout : nullptr;
  return la == lb;
}

// sprite_init_set_a / _b (0x8002fc6c / 0x8002fcf4): the shared set, the HUD pool,
// depth 1, no texture cache.
void sprite_init_set(Sprite* s, const BObj* set, int anim, int loops, u8* owner) {
  bobj_anim_reset(s);
  bobj_share_sprite(set, s);
  s->depth = 1;
  s->texcache = nullptr;
  s->pool = bg().prim_pool_b;
  sprite_reset(s, anim, loops, owner);
}

}  // namespace

s32 rng_between(s32 a, s32 b) {
  if (b == a) return b;
  const u32 r = static_cast<u32>(rng_next());
  const s32 d = b - a;
  if (d >= 0) return static_cast<s32>(r % static_cast<u32>(d)) + a;
  return static_cast<s32>(r % static_cast<u32>(a - b)) + a;
}

void sprite_pool_create() {
  g_pool = std::make_unique<Sprite[]>(kSprites);
  for (int i = 0; i < kSprites; ++i) g_pool[i].anim_id = 0;
}

void sprite_pool_destroy() { g_pool.reset(); }

Sprite* sprite_at(int index) { return g_pool && index >= 0 && index < kSprites ? &g_pool[index] : nullptr; }

void sprite_pool_update() {
  if (!g_pool) return;
  for (int i = 0; i < kSprites; ++i)
    if (g_pool[i].anim_id != 0) g_pool[i].update();
}

Sprite* sprite_find_free(int start) {
  if (!g_pool) return nullptr;
  for (int i = start; i < kSprites; ++i)
    if (g_pool[i].anim_id == 0) return &g_pool[i];
  return nullptr;
}

void sprite_reset(Sprite* s, int anim, int loops, u8* owner) {
  s->loops_copy = static_cast<u8>(loops);
  s->loop_limit = static_cast<u8>(loops);
  s->clut_offset = 0;
  s->freeze_class = 0;
  s->anim_id = static_cast<s16>(anim);
  s->owner = owner;
  s->anim = static_cast<u16>(anim - 1);
  if (bg().flags & 0x40) s->freeze_class = 2;
  s->colour_mode = 0;
  s->size[2] = 0;
  s->scale_y = 0x1000;
  s->scale_x = 0x1000;
  s->vel[0] = s->vel[1] = s->vel[2] = 0;
  s->accel[0] = s->accel[1] = s->accel[2] = 0;
  s->scale_vel[0] = s->scale_vel[1] = 0;
  s->mode = 0;
  s->quad_on = 0;
  s->attached = 0;
  s->flags = 0;
  s->flash = 0;
  s->keep_screen = 0;
  s->blend = 0;
  s->view_fx = 0;
  s->facing[0] = s->facing[1] = s->facing[2] = 0;
  s->roll = 0;
  s->roll_speed = 0;
  s->size[1] = 0;
}

Sprite* sprite_spawn_ui(int anim, int loops, u8* owner) {
  Sprite* s = sprite_find_free(0);
  if (s == nullptr) return nullptr;
  sprite_init_set(s, bg().sprite_set_a.get(), anim, loops, owner);
  s->flags |= 2;
  return s;
}

Sprite* sprite_spawn_a(int anim, int loops, u8* owner) {
  Sprite* s = sprite_find_free(0x24);
  if (s == nullptr) return nullptr;
  sprite_init_set(s, bg().sprite_set_a.get(), anim, loops, owner);
  s->pool = bg().prim_pool_a;
  return s;
}

Sprite* sprite_spawn_b(int anim, int loops, u8* owner) {
  Sprite* s = sprite_find_free(0x20);
  if (s == nullptr) return nullptr;
  sprite_init_set(s, bg().sprite_set_b.get(), anim, loops, owner);
  s->pool = bg().prim_pool_a;
  return s;
}

Sprite* sprite_spawn_c(int anim, int loops, u8* owner) {
  // sprite_init_set_c 0x8002fd7c: set C, pool A, depth untouched.
  Sprite* s = sprite_find_free(0x20);
  if (s == nullptr) return nullptr;
  bobj_anim_reset(s);
  bobj_share_sprite(bg().sprite_set_c.get(), s);
  s->texcache = nullptr;
  s->pool = bg().prim_pool_a;
  sprite_reset(s, anim, loops, owner);
  return s;
}

Sprite* sprite_spawn_from(int anim, int loops, const BObj* src, u8* owner) {
  // sprite_init_from 0x8002fdfc: src's set and texture cache, its OT bias.
  Sprite* s = sprite_find_free(0x20);
  if (s == nullptr || src == nullptr) return nullptr;
  bobj_anim_reset(s);
  bobj_share_sprite(src, s);
  s->layer = src->layer;
  s->pool = bg().prim_pool_a;
  sprite_reset(s, anim, loops, owner);
  s->pool = bg().prim_pool_a;
  return s;
}

void sprite_set_fade(Sprite* s, u8 level, s32 speed, s16 hold) {
  s->colour_mode = 2;
  s->rgb[0] = level;
  s->fade_speed = static_cast<s8>(speed);
  s->size[1] = hold;
  if (speed < 0) s->loop_limit = 0;
}

void sprite_set_pos(Sprite* s, s32 x, s32 y, s32 z) {
  if (s == nullptr) return;
  s->pos[0] = x;
  s->pos[1] = y;
  s->pos[2] = z;
  s->flags &= 0xFE;
  s->mode |= 1;
}

void sprite_set_pos_v(Sprite* s, const s32 p[3]) {
  if (s == nullptr) return;
  sprite_set_pos(s, p[0], p[1], p[2]);
}

void sprite_set_screen_pos(Sprite* s, s16 x, s16 y) {
  if (s == nullptr) return;
  s->screen_x = x;
  s->screen_y = y;
  s->flags |= 1;
  s->mode |= 1;
}

void sprite_attach_screen(Sprite* s, BObj* parent, s32 dx, s32 dy) {
  if (s == nullptr) return;
  s->attached = 1;
  s->parent = parent;
  s->pos[0] = dx;
  s->pos[1] = dy;
  s->flags |= 1;
  s->mode |= 1;
}

void sprite_attach(Sprite* s, BObj* parent, s32 dx, s32 dy, s32 dz) {
  if (s == nullptr) return;
  s->attached = 1;
  s->parent = parent;
  s->accel[0] = dx;
  s->accel[1] = dy;
  s->accel[2] = dz;
  s->mode |= 1;
}

void sprite_place_near(Sprite* s, const BObj* ref, s32 dx, s32 dy, s32 dz) {
  if (s == nullptr) return;
  const s32 fx = (ref->facing[0] < 0 ? -dx : dx) << 12;  // bobj_facing_fx 0x800457f4
  sprite_set_pos(s, ref->pos[0] + fx, ref->pos[1] + dy * -0x1000, ref->pos[2] + dz);
  s->facing[0] = ref->facing[0];
}

void sprite_set_quad(Sprite* s, s16 half, u32 colour) {
  s->quad_on = 1;
  s->size[0] = half;
  s->quad_colour = colour;
}

void sprite_set_orbit(Sprite* s, s32 yaw, s32 pitch, s32 rx, s32 ry, s32 rz, s32 yaw_speed, s32 pitch_speed,
                      s32 growth) {
  s->vel[0] = yaw;
  s->vel[1] = pitch;
  s->accel[0] = rx;
  s->mode |= 2;
  s->vel[2] = growth;
  s->extra34 = yaw_speed;
  s->accel[1] = ry;
  s->accel[2] = rz;
  s->pitch_speed = pitch_speed;
}

void sprite_free(Sprite* s) {
  if (s == nullptr) return;
  // The console dereferences a null owner too (reading RAM address 0).
  if (s->owner != nullptr && *s->owner != 0) *s->owner = 1;
  s->anim_id = 0;
}

void Sprite::update() {
  BattleGlobals& g = bg();
  const s32 ticks = g.frame_ticks;
  int frozen = 0;
  if ((flags & 2) == 0 &&
      ((freeze_class == 0 && g.flags != 0) || (freeze_class == 1 && g.flags != 0 && (g.flags & 0x40) == 0) ||
       (g.flags & 4) != 0))
    frozen = 1;
  if (g.effects_suppressed != 0 && frozen != 0) return;
  if ((mode & 1) == 0) return;
  if ((mode & 2) == 0) {
    if (attached == 0) {
      if ((flags & 1) == 0) bobj_project(this);
    } else if ((flags & 1) == 0) {
      pos[0] = parent->pos[0] + accel[0];
      pos[1] = parent->pos[1] + accel[1];
      pos[2] = parent->pos[2] + accel[2];
      bobj_project(this);
      goto drawn;
    }
    if (attached != 0 && (flags & 1) != 0) {
      screen_x = static_cast<s16>(parent->screen_x + static_cast<s16>(pos[0]));
      const bool world = pool == g.prim_pool_a;
      screen_y = static_cast<s16>(parent->screen_y + static_cast<s16>(pos[1]));
      if (world) {
        depth = parent->depth;
        scale_x = parent->final_scale_x;
        scale_y = parent->final_scale_y;
      }
      lift = 0;
    }
  } else {
    // Orbit: the position is the centre plus the radii along the yaw / pitch angles,
    // projected, then restored (or snapped back to the parent).
    const s32 x = pos[0], y = pos[1], z = pos[2];
    pos[0] += bcos(static_cast<u32>(vel[0])) * accel[0];
    pos[1] += bsin(static_cast<u32>(vel[1])) * accel[1];
    pos[2] += (bsin(static_cast<u32>(vel[0])) * bcos(static_cast<u32>(vel[1])) >> 12) * accel[2];
    bobj_project(this);
    if (attached == 0) {
      pos[0] = x;
      pos[1] = y;
      pos[2] = z;
    } else {
      pos[0] = parent->pos[0];
      pos[1] = parent->pos[1];
      pos[2] = parent->pos[2];
    }
  }
drawn:
  // Once the battle is over, sprites of other banks than the three shared sets die when
  // the end countdown runs low.
  if (g.state > 2 && g.end_countdown < 16 && !same_layout(this, g.sprite_set_a.get()) &&
      !same_layout(this, g.sprite_set_b.get()) && !same_layout(this, g.sprite_set_c.get())) {
    sprite_free(this);
    return;
  }
  if (size[2] == 0) {
    if (keep_screen != 0) flags |= 1;
    if (static_cast<s16>(bobj_anim_draw(this, static_cast<u32>(frozen))) == -1) {
      sprite_free(this);
      return;
    }
    if (keep_screen != 0) flags &= 0xFE;
    if (quad_on != 0) {
      const s32 h = static_cast<s32>(size[0]) * 2;
      bobj_draw_shadow(this, -h, h, -h, h, quad_colour);
    }
  }
  if (frozen != 0) return;
  if (size[2] != 0) {
    const s32 d = size[2] - ticks;
    size[2] = static_cast<s16>(d);
    if (static_cast<s16>(d) < 0) size[2] = 0;
    return;
  }
  if (flash == 0) {
    if (colour_mode != 0) {
      u32 level = rgb[0];
      if (size[1] == 0) {
        level = static_cast<u32>(static_cast<s32>(level) + fade_speed * ticks);
        if (static_cast<s32>(level) < 0) {
          sprite_free(this);
          return;
        }
        if (static_cast<s32>(level) > 0xFF) level = 0xFF;
      } else {
        const s32 h = size[1] - ticks;
        size[1] = static_cast<s16>(h);
        if (static_cast<s16>(h) < 0) size[1] = 0;
      }
      rgb[0] = static_cast<u8>(level);
    }
  } else {
    colour_mode = 2;
    if (rgb[0] == 0x80) {
      rgb[0] = flash;
    } else {
      rgb[0] = 0x80;
    }
  }
  roll = static_cast<s16>(roll + (roll_speed * ticks) / 2);
  if ((mode & 2) == 0) {
    if ((flags & 1) == 0) {
      bobj_move(this, vel);
      vel[0] += accel[0] * ticks;
      vel[1] += accel[1] * ticks;
      vel[2] += accel[2] * ticks;
    } else {
      screen_x = static_cast<s16>(screen_x + static_cast<s16>(vel[0]) * static_cast<s16>(ticks));
      screen_y = static_cast<s16>(screen_y + static_cast<s16>(vel[1]) * static_cast<s16>(ticks));
    }
  } else {
    vel[0] = (vel[0] + extra34 * ticks) & 0xFFF;
    accel[0] += vel[2] * ticks;
    vel[1] = (vel[1] + pitch_speed * ticks) & 0xFFF;
    accel[1] += vel[2] * ticks;
    accel[2] += vel[2] * ticks;
  }
  if ((mode & 8) != 0) {
    const s32 ground = bobj_ground_height(this);
    if (ground <= pos[1]) {
      vel[1] = -vel[1] / 2;
      pos[1] = ground - pos[1];
    }
  }
  scale_x += scale_vel[0] * ticks;
  scale_y += scale_vel[1] * ticks;
  if ((flags & 4) != 0) {
    if (scale_x > 0x1000) scale_x = 0x1000;
    if (scale_y > 0x1000) scale_y = 0x1000;
  }
}

// ---- Effect makers ---------------------------------------------------------------------

void effect_rise_icon(const BObj* u, int kind) {
  Sprite* s = sprite_spawn_a(kind + 0x2E, 0x10, nullptr);
  if (s == nullptr) return;
  sprite_set_pos(s, u->pos[0], u->pos[1] + u->size[1] * -0x3000, u->pos[2]);
  s->vel[1] = static_cast<s32>(0xFFFFC000);
  s->accel[1] = 0x200;
  s->layer = 2;
  s->freeze_class = 2;
  s->keep_screen = 1;
}

void effect_skill_popup(const BObj* u, int skill) {
  BattleGlobals& g = bg();
  if (u == g.party_units[g.controlled_slot]) {
    // TODO(port): skill_notice_show 0x800642f4 (the skill-notice window).
    (void)skill;
  }
  Sprite* s = sprite_spawn_a(0x33, 0x10, nullptr);
  if (s == nullptr) return;
  sprite_set_pos(s, u->pos[0], u->pos[1] + u->size[1] * -0x3000, u->pos[2]);
  s->vel[1] = static_cast<s32>(0xFFFFB000);
  s->accel[1] = 0x200;
  s->layer = 2;
  s->freeze_class = 2;
  s->keep_screen = 1;
}

void effect_debris_burst(const s32 pos[3], const s32 spread[3], int count, int kind, int size, s32 scale,
                         u8 clut_offset) {
  s32 speed = (count - 1) * 2;
  if (speed > 0xC) speed = 0xC;
  speed <<= 12;
  if (spread[0] < 0x10001) speed >>= 2;
  int anim = 0;
  for (int i = 0; i < count; ++i) {
    if (kind == 1) {
      if (size == 0) anim = static_cast<int>(rng_range(2)) + 4;
      else if (size == 1) anim = static_cast<int>(rng_range(3)) + 3;
      else anim = 3;
    } else if (kind == 0) {
      if (size == 0) anim = 1;
      else if (size == 1) anim = static_cast<int>(rng_range(2)) + 1;
      else anim = 2;
    } else if (kind == 2) {
      if (size == 0) anim = static_cast<int>(rng_range(2)) + 6;
      else if (size == 1) anim = static_cast<int>(rng_range(3)) + 6;
      else anim = static_cast<int>(rng_range(2)) + 7;
    }
    Sprite* s = sprite_spawn_b(anim, 1, nullptr);
    if (s == nullptr) continue;
    const s32 dx = rng_spread(spread[0]);
    const s32 dy = rng_spread(spread[1]);
    const s32 dz = rng_spread(spread[2]);
    sprite_set_pos(s, pos[0] + dx, pos[1] + dy, pos[2] + dz);
    s->freeze_class = (bg().flags & 0x40) == 0 ? 1 : 2;
    if (kind != 0) {
      s->vel[0] = rng_spread(speed);
      s->vel[1] = rng_spread(speed >> 1);
      s->vel[2] = rng_spread(speed);
      sprite_set_fade(s, 0x80, -4, 0x1E);
    }
    const s32 sc = scale + static_cast<s32>(rng_range(static_cast<u32>(scale >> 4)));
    s->scale_y = sc;
    s->scale_x = sc;
    s->layer = 3;
    s->clut_offset = clut_offset;
    s->size[2] = static_cast<s16>(rng_range(static_cast<u32>(i * 4 + 8)));
  }
}

void effect_ring(const s32 pos[3], int size) {
  if (bg().state != kStateFighting) return;
  Sprite* s = sprite_spawn_from(size + 0xD, 4, bg().sprite_bank_fx.get(), nullptr);
  if (s == nullptr) return;
  sprite_set_pos_v(s, pos);
  sprite_set_quad(s, static_cast<s16>((size + 1) * 0x20), 0x200040FF);
  sprite_set_fade(s, 0x80, -2, 10);
  s->layer = 1;
}

Sprite* effect_dust_puff(const s32 pos[3], s32 speed) {
  if (bg().state != kStateFighting) return nullptr;
  Sprite* s = sprite_spawn_from(0x2B, 0, bg().sprite_bank_fx.get(), nullptr);
  if (s != nullptr) {
    sprite_set_pos(s, pos[0], pos[1] + 0x40000, pos[2]);
    const u32 a = rng_range(0x1000);
    s->vel[0] = bcos(a) * speed;
    s->vel[2] = bsin(a) * speed;
    sprite_set_fade(s, 0x80, -2, 0x14);
    s->layer = 1;
  }
  return s;
}

void effect_hit_flash(const s32 pos[3], int sparks) {
  if (bg().state != kStateFighting) return;
  if (Sprite* s = sprite_spawn_from(0x16, 0, bg().sprite_bank_fx.get(), nullptr)) {
    sprite_set_pos_v(s, pos);
    sprite_set_fade(s, 0x80, -2, 0x14);
    s->layer = 1;
    s->roll_speed = 0x10;
    s->scale_vel[0] = sparks << 3;
    s->scale_vel[1] = sparks << 3;
  }
  for (int i = 0; i < sparks; ++i) {
    const int anim = static_cast<int>(rng_range(2)) + 0x17;
    Sprite* s = sprite_spawn_from(anim, 0, bg().sprite_bank_fx.get(), nullptr);
    if (s == nullptr) continue;
    sprite_set_pos_v(s, pos);
    const u32 a = rng_range(0x1000);
    s->vel[0] = bcos(a) << 2;
    s->vel[1] = bsin(a) << 2;
    s->roll = static_cast<s16>(a);
    sprite_set_fade(s, 0x80, -2, 10);
    s->layer = 1;
  }
}

}  // namespace ss::game::battle
