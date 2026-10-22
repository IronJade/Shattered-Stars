// Magichand's enemy-code module: block a5d12269e803 (3736 bytes, slot A at 0x80088110-0x80088fa8),
// vtable 0x80088e78 (38 slots; the factory stores it over a 0x2f4-byte object and builds the
// second sprite at +0x26c with bobj_create); monster type 0x55 Magichand (encounter 524, with
// the Guardboxes). A rooted giant hand, always facing right (+0x18 = 3.0): it never chooses an
// attack itself; each attack 1 (anim 4) chains into attack (+0x174) + 2: a sweeping beam, a
// spray of shots, a self-heal shower, a lobbed rock (the module's own Shot kind 0x29).

#include <cstring>

#include "game/battle/bsfx.h"
#include "game/battle/combat.h"
#include "game/battle/enemy_modules.h"
#include "game/battle/enemy_support.h"
#include "game/battle/shot.h"
#include "game/battle/sprite.h"
#include "game/battle/unit_move.h"
#include "game/boot/rng.h"
#include "game/title/new_game.h"

namespace ss::game::battle {
namespace {

void shot_rock(Shot* s);

struct EnemyMagichand : EnemyUnit {
  // +0x26c: the second sprite (the arm), drawn 0x70 behind the hand by slot 22.
  BObj arm;

  // 0x80088110 [32]: keeps facing right.
  int pre_think() override {
    facing[0] = 0x30000;
    return 0;
  }

  // 0x80088120 [26]: nothing (the attacks are chained from attack 1).
  void choose_target() override {}

  // 0x80088b34 [22]: enemy_animate, then the arm: the hand's sprite set, anim 1, 0x70 to
  // the left (shaken with the hand), its facing and scale, projected and drawn.
  int animate(u32 anim_flags) override {
    facing[0] = 0x30000;
    const int r = EnemyUnit::animate(anim_flags);
    arm.pool = bg().prim_pool_a;
    bobj_share_sprite(this, &arm);
    arm.anim = 1;
    arm.loop_limit = 0;
    arm.pos[0] = pos[0] - 0x70000;
    arm.pos[1] = pos[1];
    arm.pos[2] = pos[2];
    std::memcpy(arm.facing, facing, sizeof(arm.facing));
    arm.scale_x = scale_x;
    arm.scale_y = scale_y;
    if (shake_frames != 0) arm.pos[0] += shake_amp * 0x1000;
    bobj_project(&arm);
    bobj_anim_draw(&arm, anim_flags | (bg().flags & 0xFE));
    return static_cast<u16>(r);
  }

  // 0x80088afc: slot 37, the hand's ground shadow (-0x80..0x180 x -0x80..0x80). Nothing
  // calls slot 37 (EnemyUnit vtables end at 36), so it stays a plain member.
  [[maybe_unused]] void draw_shadow() { bobj_draw_shadow(this, -0x80, 0x180, -0x80, 0x80, 0xFFFFFFFF); }

  // 0x80088128 [27]: anim 4 to its end (held on the frame it had), then the attack +0x174 +
  // 2 while fighting (else idle), the sound handle +0x17c cleared.
  void attack1() override {
    const s8 held = frame;
    facing[0] = 0x30000;
    anim = 4;
    if (static_cast<s16>(animate(static_cast<u32>(attack_recovery))) != -1) return;
    frame = held;
    loop_limit = 0;
    animate(static_cast<u32>(attack_recovery));
    action_kind = bg().state == kStateFighting ? static_cast<u8>(counters[0] + 2) : 0;
    counters[2] = 0;
    begin_action();
  }

  // 0x800881f8 [28]: past frame 3 a beam to the right (radius +0x174 growing 8 a tick to
  // 200, fading after 42 ticks of +0x178) with its sound (+0x17c) held; at full width its
  // attack box (0x190..0x1590 right, -0x64..0x12c, +-0xc8) hits the party every frame.
  void attack2() override {
    BattleGlobals& g = bg();
    facing[0] = 0x30000;
    anim = 5;
    if (static_cast<s16>(animate(static_cast<u32>(attack_recovery))) == -1) {
      action_kind = 0;
      begin_action();
      return;
    }
    if (frame < 4) return;
    if (counters[2] == 0) counters[2] = static_cast<s32>(bsfx_play_at(0x40, this));
    sound_handle_fade_out(static_cast<u32>(counters[2]), 0x3C, 8);
    if (g.flags != 0) sound_handle_duck(static_cast<u32>(counters[2]), 6);
    s32 level = 0x100;
    const s32 from[3] = {pos[0] + 0x190000, pos[1] + 0x64000, pos[2]};
    if (0x29 < counters[1]) level = (counters[1] - 0x2A) * -8 + 0x100;
    if (level < 0) level = 0;
    const s32 dir[3] = {0xA000, 0, 0};
    const u32 c0 = scale_colour(0x507050, level);
    const u32 c1 = scale_colour(0x608060, level);
    draw_beam(from, dir, c0, c1, 0x1400, counters[0], 0, 0x10);
    if (g.flags != 0) return;
    counters[0] += g.frame_ticks * 8;
    if (200 < counters[0]) {
      counters[0] = 200;
      box_mask |= 4;
      attack_min[0][0] = pos[0] + 0x190000;
      attack_min[0][1] = pos[1] - 0x64000;
      attack_min[0][2] = pos[2] - 0xC8000;
      attack_max[0][0] = pos[0] + 0x1590000;
      attack_max[0][1] = pos[1] + 0x12C000;
      attack_max[0][2] = pos[2] + 0xC8000;
      Unit* units[8] = {};
      Vec4 points[8];
      const int n = collider_attack_overlaps(this, units, points, 1);
      if (n != 0) unit_attack_targets(this, n, units, points, 0x100, 9999, 0);
    }
    counters[1] += g.frame_ticks;
  }

  // 0x800884f4 [29]: past frame 3 a spray of piercing shots from the palm, one every 2
  // ticks (+0x178), aimed 0x180 ahead with random spread, for 120 ticks (+0x174); its
  // sound (+0x17c) held.
  void attack3() override {
    BattleGlobals& g = bg();
    facing[0] = 0x30000;
    anim = 6;
    loop_limit = 0;
    animate(static_cast<u32>(attack_recovery));
    if (g.flags == 0) {
      if (3 < frame) {
        if (counters[2] == 0) counters[2] = static_cast<s32>(bsfx_play_at(0x2D, this));
        counters[1] += g.frame_ticks;
        while (1 < counters[1]) {
          counters[1] -= 2;
          ShotDesc* d = shot_desc_init(9);
          const s32 from[3] = {bobj_front_x(this, 0x1CC), pos[1] + 0x98000, pos[2]};
          d->speed = 0x38;
          d->point[0] = from[0] + bobj_facing_fx(this, 0x180);
          d->point[1] = from[1] + rng_between_fixed(0x80, 0x180);
          const s32 dz = rng_spread_fixed(0x100);
          d->life = 0x16;
          d->power = 0x100;
          d->hit_effect = 2;
          d->hit_bits[1] = 3;
          d->hit_bits[2] = 0x4000;
          d->flags = 6;
          d->hits = 6;
          d->hit_bits[0] = 1;
          d->element = 0x10004;  // +0x70 = 4, +0x72 = 1 (pierces)
          d->point[2] = from[2] + dz;
          shot_spawn_projectile(this, from, d, this);
        }
      }
      counters[0] += g.frame_ticks;
      if (0x78 < counters[0]) {
        action_kind = 0;
        begin_action();
        return;
      }
    }
    if (counters[2] != 0) {
      sound_handle_fade_out(static_cast<u32>(counters[2]), 0x3C, 8);
      if (g.flags != 0) sound_handle_duck(static_cast<u32>(counters[2]), 6);
    }
  }

  // 0x80088748 [30]: a shower of falling sparkles (effect bank anims 3-5, one every 2 ticks
  // of +0x174 while fighting) and every 50 ticks of +0x178 a heal of a quarter of its max
  // HP (sfx 0x13). (The end of the animation does not return early: the frame goes on.)
  void attack4() override {
    BattleGlobals& g = bg();
    facing[0] = 0x30000;
    anim = 8;
    if (static_cast<s16>(animate(static_cast<u32>(attack_recovery))) == -1) {
      action_kind = 0;
      begin_action();
    }
    if (counters[2] == 0) {
      bsfx_play_at(0x16, this);
      counters[2] = 1;
    }
    if (g.flags != 0) return;
    counters[0] += g.frame_ticks;
    while (1 < counters[0] && g.state == kStateFighting) {
      counters[0] -= 2;
      const int a = static_cast<int>(rng_range(3)) + 3;
      Sprite* sp = sprite_spawn_from(a, 0, g.sprite_bank_fx.get(), nullptr);
      if (sp == nullptr) continue;
      const s32 dx = rng_between(-0x80000, 0x180000);
      const s32 x = pos[0] + dx;
      const s32 dz = rng_between(-0x40000, 0x10000);
      sprite_set_pos(sp, x, pos[1] - 0xC0000, pos[2] + dz);
      sp->vel[1] = 0x8000;
      sprite_set_fade(sp, 0x80, -4, 0x14);
    }
    counters[1] += g.frame_ticks;
    if (0x32 < counters[1]) {
      counters[1] = 0;
      const s32 heal = -st()->hp_max / 4;
      damage_popup_spawn_at(pos, heal, -0x2000, 0);
      unit_damage_hp(this, heal);
      bsfx_play_at(0x13, this);
    }
  }

  // 0x80088948 [31]: once (+0x178), a rock (Shot kind 0x29) lobbed from 0x190 right of the
  // hand at a random member: speed 0x18 over the ground, the vertical speed set to land
  // there (time = distance / 2 / 0x18 ticks). (The end of the animation does not return.)
  void attack5() override {
    facing[0] = 0x30000;
    anim = 7;
    if (static_cast<s16>(animate(static_cast<u32>(attack_recovery))) == -1) {
      action_kind = 0;
      begin_action();
    }
    if (counters[1] != 0 || bg().flags != 0) return;
    counters[1] = 1;
    // The module's own kind 0x29 (handler 0x80088cb0): run here as a callback Shot, the
    // port's Shot switch dispatching only the resident kinds.
    Shot* s = shot_spawn_callback(shot_rock);
    if (s == nullptr) return;
    Unit* t = enemy_random_party_member(this);
    s->pos[0] = pos[0] + 0x190000;
    s->pos[1] = pos[1] - 0x32000;
    s->target = t;
    s->owner = this;
    s->pos[2] = pos[2];
    const s32 d = vec_distance_xz_fx(t->pos, pos);
    const s32 h = (d / 2) / 0x18;
    s32 time = h >> 12;
    if (time == 0) time = 1;
    const s32 vy = (t->pos[1] - s->pos[1]) / time;
    const Vec4 v = vec_velocity_toward_xz(s->pos, t->pos, 0x18);
    std::memcpy(s->vel, &v, sizeof(s->vel));
    bsfx_play_at(0x20, this);
    s->vel[1] = -h / 2 + vy;
    bobj_share_sprite(this, s);
  }

  // 0x80088c34 [24]: a hit only shakes it (super armour 20 ticks).
  void state_hit() override {
    const s32 kx = knockback[0] < 0 ? -knockback[0] : knockback[0];
    shake_amp = static_cast<s16>((kx >> 12) << 3);
    if (8 < static_cast<s16>((kx >> 12) << 3)) shake_amp = 8;
    shake_frames = 0x14;
    super_armour = 0x14;
    action_kind = 0;
    begin_action();
  }
};

// 0x80088cb0: Shot kind 0x29, the rock (anim 0xa, ground shadow) falling under gravity 0.5 a
// tick; on the ground, or on first contact with the owner's foes (power 0x200, knocked along
// its flight), debris, a crash (sfx 0x50) and it is gone. (The result of shot_fade_out is
// not looked at: a rock that faded out this frame still moves and can still hit.)
void shot_rock(Shot* s) {
  BattleGlobals& g = bg();
  s->anim = 0xA;
  shot_draw_and_box_ground(s, 4, 0xFFFFFFFF);
  if (g.flags != 0) return;
  shot_fade_out(s);
  bobj_move(s, s->vel);
  s->vel[1] += g.frame_ticks << 11;
  bool landed = false;
  if (0 < s->vel[1]) {
    const s32 ground = bobj_ground_height(s);
    if (ground < s->pos[1]) {
      s->pos[1] = ground;
      landed = true;
    }
  }
  if (!landed) {
    Unit* units[8] = {};
    Vec4 points[8];
    const int n = collider_attack_overlaps(s, units, points, s->owner->is_enemy);
    if (n == 0) return;
    s->hit_landed = 1;
    unit_attack_targets(s->owner, n, units, points, 0x200, s->vel[0] < 0 ? 0x800 : 0, 0);
  }
  const s32 spread[3] = {0x40000, 0, 0x40000};
  effect_debris_burst(s->pos, spread, 0xC, 0, 1, 0x1000, 0);
  bsfx_play_at_pos(0x50, s->pos);
  shot_kill(s);
}

}  // namespace

std::unique_ptr<EnemyUnit> enemy_mod_magichand_create() { return std::make_unique<EnemyMagichand>(); }

}  // namespace ss::game::battle
