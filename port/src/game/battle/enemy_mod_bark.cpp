// Bark's enemy-code module: block 3f39fe9082f8 (5512 bytes, slot A linked at 0x80088110),
// vtable 0x80089570, monster types 0xc1 Bark, 0xc2 Rock-demon, 0xc3 M-eater (0x270-byte
// objects), encounters 464 509 531 556. A rooted boss that never walks or swings: it pales
// (cyan) as it takes damage, and each time it has lost a tenth of its max HP since the last
// one it casts its spell (SpellCast kind 0x36, whose handler is this block's 0x800885d8:
// a crystal grows above it, a rain of shards, a burst, a growing vortex of light around it,
// and every standing party member is hit).

#include "game/battle/bsfx.h"
#include "game/battle/camera.h"
#include "game/battle/combat.h"
#include "game/battle/enemy_classes.h"
#include "game/battle/enemy_modules.h"
#include "game/battle/enemy_support.h"
#include "game/battle/particle.h"
#include "game/battle/render.h"
#include "game/battle/shot.h"
#include "game/battle/sprite.h"
#include "game/boot/rng.h"

namespace ss::game::battle {
namespace {

u32 rng15() { return static_cast<u32>(rng_next() & 0x7FFF); }  // rng_next15 0x800103b4

struct EnemyBark : EnemyUnit {
  s32 hp_mark = -1;  // +0x26c: the HP at the last cast (-1 until the first frame)

  // 0x80088110: enemy_create, the module's vtable, +0x26c = -1.

  // 0x80088150 [26]: once a tenth of its max HP is gone since the last cast, the spell
  // (attack 1, message 0x271b) when the approach roll succeeds; otherwise nothing.
  void choose_target() override {
    const Stats* s = st();
    if (hp_mark - s->hp < s->hp_max / 10) return;
    if (enemy_try_approach(this, 100, 0x8000, 1, 0x140) == 0) return;
    battle_message_show(0x271B);
    action_kind = 1;
    begin_action();
  }

  // 0x800881f8 [32]: the tint follows the damage since the last cast (red falls, green /
  // blue rise, 0x80 per tenth of max HP), it always faces right, and while idle in the
  // fight a mote (anim 6) appears somewhere in the disc of 600 round it.
  int pre_think() override {
    BattleGlobals& g = bg();
    const Stats* s = st();
    if (hp_mark == -1) hp_mark = s->hp;
    s32 lvl = ((hp_mark - s->hp) * 0x80) / (s->hp_max / 10);
    if (0x7F < lvl) lvl = 0x7F;
    colour_mode = 1;
    rgb[0] = static_cast<u8>(0x80 - lvl);
    rgb[1] = static_cast<u8>(lvl - 0x80);
    rgb[2] = static_cast<u8>(lvl - 0x80);
    facing[0] = 0x3E8000;
    if (action != 0 || g.flags != 0 || g.state != kStateFighting) return 0;
    Sprite* sp = unit_spawn_sprite(this, 6);
    if (sp == nullptr) return 0;
    const Vec4 p = vec_random_in_disc_xz(pos, 600);
    sprite_set_pos_v(sp, p.data());
    sprite_set_fade(sp, 0x80, -4, 8);
    sp->layer = 8;
    return 0;
  }

  // 0x80088340 [22]: enemy_animate with the body drawn 8 OT layers further back and the
  // cels 0x14 lower (the global sprite offset y), the hover and the ground shadow.
  int animate(u32 anim_flags) override {
    BattleGlobals& g = bg();
    const s8 saved_layer = layer;
    layer = static_cast<s8>(saved_layer + 8);
    const s32 shadow = unit_ground_follow(this);
    const s32 y = pos[1];
    pos[1] += bsin(static_cast<u32>((hover_phase & 0x1F) << 7)) * hover_amp;
    if (g.flags == 0) hover_phase = static_cast<s16>(hover_phase + g.frame_ticks);
    unit_draw_model(this);
    pos[1] = y;
    sprite_offset_y() = 0x14;
    const int f = static_cast<s16>(bobj_anim_draw(this, anim_flags | (g.flags & 0xFE)));
    sprite_offset_y() = 0;
    layer = saved_layer;
    if (f != -1) unit_draw_shadow(this, shadow);
    return f;
  }

  // 0x8008844c [23]: it never walks: the attack starts where it stands.
  void state_approach() override {
    action_kind = 0;
    begin_action();
  }

  // 0x80088480 [24]: a hit only shakes it (super armour 20 ticks).
  void state_hit() override {
    const s32 kx = knockback[0] < 0 ? -knockback[0] : knockback[0];
    shake_amp = static_cast<s16>((kx >> 12) << 3);
    if (8 < static_cast<s16>((kx >> 12) << 3)) shake_amp = 8;
    shake_frames = 0x14;
    super_armour = 0x14;
    action_kind = 0;
    begin_action();
  }

  // 0x800884fc [27]: the spell: while fighting, once, SpellCast kind 0x36 at the target
  // (the HP mark moves to the current HP) and one frame of anim 4; the next frame ends it.
  void attack1() override {
    BattleGlobals& g = bg();
    if (g.flags != 0 || counters[0] != 0 || g.state != kStateFighting) {
      action_kind = 0;
      begin_action();
      return;
    }
    counters[0] = 1;
    spell_effect_started_flag() = 1;
    spellcast_create(0x36, this, target, 0, nullptr);
    hp_mark = st()->hp;
    anim = 4;
    loop_limit = 0;
    animate(0);
  }

  // 0x8008933c [25]: once: two flashes (anim 0xb, and 0x11 / 0x10 drifting up and turning)
  // high on its trunk with 24 violet sparks each, sfx 0x50 / 0x22; then the usual fade.
  void state_dying() override {
    anim = 0xE;
    if (counters[0] == 0) {
      for (int i = 0; i < 2; ++i) {
        const s32 at[4] = {pos[0] + (i != 0 ? -0x78000 : 0x28000), pos[1] - 0x1CC000, pos[2], 0};
        if (Sprite* sp = unit_spawn_sprite(this, 0xB)) {
          sprite_set_pos(sp, at[0], at[1], at[2]);
          sp->layer = 0x11;
          sprite_set_fade(sp, 0x80, -2, 8);
          sp->scale_y = 0x1800;
          sp->scale_x = 0x1800;
        }
        if (Sprite* sp = unit_spawn_sprite(this, 0x11 - i)) {
          sprite_set_pos(sp, at[0], at[1], at[2]);
          sprite_set_fade(sp, 0x80, -2, 8);
          sp->layer = 0x10;
          sp->accel[1] = 0x100;
          sp->vel[0] = 0x1000;
          sp->roll_speed = 0x5B;
        }
        for (int k = 0; k < 0x18; ++k) {
          const Vec4 v = vec_random_around(kZeroVector, 0x10);
          particle_spawn_spark_line(at, v.data(), 0x400080, 0xA000FF, 8, 0x3C);
        }
      }
      bsfx_play_at(0x50, this);
      bsfx_play_at(0x22, this);
      counters[0] = 1;
    }
    EnemyUnit::state_dying();
  }

  // 0x80089530 [36]: the dtor (EnemyUnit vtable, unit_destroy).
};

// ---- SpellCast kind 0x36 (the spells' kind table 0x80088020 points at 0x800885d8) ----
//
// The handler of the cast attack 1 starts, on the SpellCast's fields: +0x0e finished,
// +0x14 caster, +0x24 (s24) step 1 the shard timer / steps 3-4 the vortex angle, +0x28
// (s28) the rain ticks of step 1, +0x2c (s2c) the vortex radius ticks of steps 3-4, +0x34
// (s34) the fade of step 4, +0x40 step, +0x1bc (obj2) the crystal (anim 6) / the vortex
// cel (anim 8).
using BarkCast = SpellCast;

// The vortex (steps 3-4): 6 rings x 16 segments of anim 8's cel as textured quads round the
// caster, radius +0x2c * 9.375, rings 0x154 high, turning with +0x24.
void bark_cast_vortex(BarkCast& sc) {
  Unit* caster = sc.caster;
  sc.obj2.anim = 8;
  bobj_anim_draw(&sc.obj2, 0x9800);
  const FrameUv& f = frame_uv();
  const s32 uv[4] = {f.u0, f.v0, f.u1, f.v1};
  const u16 tpage = f.tpage, clut = f.clut;
  const u32 code = bobj_poly_tag(&sc.obj2, tpage);
  const s32 r = (sc.s2c * 0x960) / 256;
  const s32 cx = caster->pos[0], cz = caster->pos[2];
  for (int ring = 0; ring < 6; ++ring) {
    const s32 ang_a = sc.s24 + ring * 0x18;
    const s32 ang_b = sc.s24 + ring * 0x18 + 0x18;
    // ORIGINAL BUG (original_bugs.md E4): the 20.12 height minus whole units, then shifted
    // up by 12, so only the height's low 20 bits count (right only for a caster at 0):
    //   y0 = (u32)(caster->pos[1] - 0x154 * (6 - ring)) << 12, y1 the same with 5 - ring
    const s32 y0 = caster->pos[1] - ((0x154 * (6 - ring)) << 12);
    const s32 y1 = caster->pos[1] - ((0x154 * (5 - ring)) << 12);
    for (int seg = 0; seg < 0x10; ++seg) {
      // 22.5 degree steps through degrees (truncated twice: 0, 250, 500, 762, ...).
      const s32 lo = (((seg * 0x168) / 16) << 12) / 0x168;
      const s32 hi = ((((seg + 1) * 0x168) / 16) << 12) / 0x168;
      s32 c[4][4] = {};
      c[0][0] = bcos(static_cast<u32>(lo + ang_a)) * r + cx;
      c[0][1] = y0;
      c[0][2] = bsin(static_cast<u32>(lo + ang_a)) * r + cz;
      c[1][0] = bcos(static_cast<u32>(hi + ang_a)) * r + cx;
      c[1][1] = y0;
      c[1][2] = bsin(static_cast<u32>(hi + ang_a)) * r + cz;
      c[2][0] = bcos(static_cast<u32>(lo + ang_b)) * r + cx;
      c[2][1] = y1;
      c[2][2] = bsin(static_cast<u32>(lo + ang_b)) * r + cz;
      c[3][0] = bcos(static_cast<u32>(hi + ang_b)) * r + cx;
      c[3][1] = y1;
      c[3][2] = bsin(static_cast<u32>(hi + ang_b)) * r + cz;
      draw_textured_patch(uv, c, clut, tpage, 0xC, code, 1);
    }
  }
}

// 0x800885d8: kind 0x36's handler, a step machine (+0x40; spellcast_wait advances it):
// 0 dim to 0x40, two camera moves on the caster, the crystal takes its sprite, wait 18,
//   a rising hum (sfx 0x46);
// 1 the crystal (anim 6) grows above it, a shard every 6 ticks, light streaks (anim 0xc)
//   fly in from the right; after 100 ticks wait 60, sfx 0x21 / 0x22 and a white flash;
// 2 the burst: three sounds, a spark ring, two flashes (anims 8, 0xe), the camera pulls
//   back, the screen shakes for 360 ticks;
// 3 the vortex grows for 320 ticks while blasts (anim 0xd, 3 x anim 10 sucked in) hit the
//   ground in a widening disc;
// 4 the crystal greys out over 32 ticks (the vortex goes on growing);
// 5 every standing party member is hit (power 0x100) at its centre, the light comes back,
//   the dimming ends, the cast is finished.
void bark_cast_run(BarkCast& sc) {
  BattleGlobals& g = bg();
  Unit* caster = sc.caster;
  caster->facing[0] = 0x64000;
  caster->anim = 4;
  const s32 top[4] = {caster->pos[0], caster->pos[1] - 0x1C2000, caster->pos[2], 0};
  if (sc.step == 1) {
    sc.obj2.anim = 6;
    sc.obj2.pos[0] = top[0];
    sc.obj2.pos[1] = top[1];
    sc.obj2.layer = 0x10;
    sc.obj2.pos[2] = top[2];
    bobj_project(&sc.obj2);
    bobj_anim_draw(&sc.obj2, 0);
  }
  // (The console also builds the target's centre - 0x12c here and never uses it.)
  if (static_cast<u32>(sc.step - 3) < 2) {
    bark_cast_vortex(sc);
    if ((g.flags & 0xC) != 0) return;
    sc.s24 = (sc.s24 + g.frame_ticks * 8) & 0xFFF;
  }
  if ((g.flags & 0xC) != 0) return;
  switch (static_cast<u32>(sc.step)) {  // jump table 0x80089558
    case 0: {
      battle_flash_set(&sc, 0x40, 0x40, 0x40);
      const s32 at[3] = {caster->pos[0] + 0xC8000, caster->pos[1], caster->pos[2]};
      camera_move_to(g.camera, at, 0xC00, 0xE3, 0xA00, 0, 0x80, 5);
      camera_move_to(g.camera, at, 0xC00, 0x11C, 0xC00, 0xB4, 0x80, 5);
      bobj_share_sprite(caster, &sc.obj2);
      spellcast_wait(&sc, 0x12);
      const u32 h = bsfx_play_at(0x46, caster);
      sound_handle_pitch_up(h, 0x140);
      sound_handle_fade_out(h, 0x3C, 0xF0);
      return;
    }
    case 1: {
      const s32 t = g.frame_ticks;
      sc.obj2.scale_x += t * 0x60;
      sc.s24 += t;
      sc.obj2.scale_y += t * 0x60;
      if (5 < sc.s24) {
        sc.s24 -= 6;
        const s32 yaw = static_cast<s32>(rng_range(0x1000));
        const s32 pitch = static_cast<s32>(rng_range(0x1000));
        const s32 a = rng_spread(0x1E);
        const s32 b = rng_spread(0x1E);
        particle_spawn_shard(top, yaw, pitch, 0x3C0, a, b, 0xC020FF, 0x14);
      }
      s32 dest[3];
      dest[0] = rng_range(2) == 0 ? caster->pos[0] + 0xA0000 : caster->pos[0];
      dest[1] = caster->pos[1] - 0x1C2000;
      dest[2] = caster->pos[2];
      if (Sprite* sp = sprite_spawn_from(0xC, 0, caster, nullptr)) {
        const s32 dy = rng_spread_fixed(0x80);
        sprite_set_pos(sp, dest[0] + 0x200000, dest[1] + dy, dest[2]);
        const s32 d = vec_distance_fx(sp->pos, dest);
        const Vec4 v = vec_velocity_toward(sp->pos, dest, 0x10);
        sp->vel[0] = v.x;
        sp->vel[1] = v.y;
        sp->vel[2] = v.z;
        sprite_set_fade(sp, 0x80, -8, static_cast<s16>((d >> 16) - 8));
        sp->layer = 0xC;
      }
      sc.s28 += g.frame_ticks;
      if (100 < sc.s28) {
        spellcast_wait(&sc, 0x3C);
        bsfx_play_ui(0x21);
        bsfx_play_ui(0x22);
        screen_fade_create(0xFFFFFF, 0x20, 4, 8);
      }
      return;
    }
    case 2: {
      bsfx_play_ui(0x50);
      bsfx_play_ui(0x2A);
      bsfx_play_ui(0x29);
      effect_spawn_sparks_at(top, 0x18, 0x180, 0, 0x60, 0x5A, 3, 3, 0x100, 0);
      if (Sprite* sp = sprite_spawn_from(8, 0, caster, nullptr)) {
        sprite_set_pos_v(sp, top);
        sp->layer = 0x10;
        sprite_set_fade(sp, 0x80, -2, 0x78);
        sp->scale_y = 0x2000;
        sp->scale_x = 0x2000;
      }
      if (Sprite* sp = sprite_spawn_from(0xE, 1, caster, nullptr)) {
        sprite_set_pos_v(sp, top);
        sp->scale_vel[1] = 0x100;
        sp->scale_vel[0] = 0x100;
        sp->layer = 0x10;
        sprite_set_fade(sp, 0x80, -2, 0x78);
      }
      sc.s24 = 0;
      const s32 far[3] = {caster->pos[0] + 0x3E8000, caster->pos[1], caster->pos[2] - 0x1F4000};
      camera_move_to(g.camera, caster->pos, 0xE38, 0xE3, 0x1000, 0, 0x80, 5);
      camera_move_to(g.camera, far, 0xF1C, 0xE3, 0x2000, 0x140, 0x80, 5);
      screen_shake(3, 6, 4, 6, 0x168);
      sound_handle_fade_out(bsfx_play_ui(0x2D), 0x3C, 400);
      ++sc.step;
      return;
    }
    case 3: {
      sc.s2c += g.frame_ticks;
      if (0x140 < sc.s2c) ++sc.step;
      const Vec4 c = vec_random_in_disc_xz(caster->pos, (sc.s2c * 0x960) / 256);
      Sprite* sp = sprite_spawn_from(0xD, 0, caster, nullptr);
      if (sp == nullptr) return;
      if (rng15() < 0x4000) bsfx_play_at(0x50, sp);
      sprite_set_pos_v(sp, c.data());
      sp->size[2] = 0x10;
      sprite_set_fade(sp, 0x80, -4, 0x18);
      const s32 sc0 = rng_between(0x4000, 0x8000);
      sp->scale_y = sc0;
      sp->scale_x = sc0;
      sp->layer = 5;
      for (int i = 0; i < 3; ++i) {
        Sprite* q = sprite_spawn_from(10, 0, caster, nullptr);
        if (q == nullptr) continue;
        const Vec4 p = vec_random_in_disc_xz(c.data(), 0x80);
        sprite_set_pos_v(q, p.data());
        const Vec4 v = vec_velocity_toward(q->pos, c.data(), 8);
        q->vel[0] = v.x;
        q->vel[1] = v.y;
        q->vel[2] = v.z;
        sprite_set_fade(q, 0x80, -8, 0xE);
        q->layer = 5;
        const s32 s1 = rng_between(0x4000, 0x8000);
        q->scale_y = s1;
        q->scale_x = s1;
      }
      return;
    }
    case 4:
      sc.obj2.colour_mode = 2;
      sc.obj2.rgb[0] = static_cast<u8>(0x80 - sc.s34);
      sc.s34 += g.frame_ticks * 4;
      sc.s2c += g.frame_ticks;
      if (0x7F < sc.s34) ++sc.step;
      return;
    case 5: {
      Unit* units[8] = {};
      Vec4 points[8];
      int n = 0;
      for (PartyUnit* u : g.party_units) {
        if (unit_is_down(u)) continue;
        units[n] = u;
        points[n] = Vec4{u->pos[0], u->pos[1] - u->size[1] * 0x2000, u->pos[2], 0};
        ++n;
      }
      if (n != 0) {
        unit_attack_targets(caster, n, units, points, 0x100, caster->facing[0] < 0 ? 0x800 : 0, 0);
        caster->attack_recovery = 0;
      }
      battle_flash_set(&sc, 0x80, 0x80, 0x80);
      spellcast_dim_end(&sc);
      sc.finished = 1;
      ++sc.step;
      return;
    }
    default:
      return;
  }
}

}  // namespace

void bark_cast_step(SpellCast* sc) { bark_cast_run(*sc); }

std::unique_ptr<EnemyUnit> enemy_mod_bark_create() { return std::make_unique<EnemyBark>(); }

}  // namespace ss::game::battle
