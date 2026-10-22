// Indalecio's enemy-code module: block afedfe13e693 (13060 bytes, slot D linked at
// 0x80088900), vtable 0x8008bad0, monster types 0xce (the first form) and 0xcf (the second
// form, his own fight), 0x2fc-byte objects; encounters 539 and 540 (the final boss). He
// casts (spells 0x14 / 3 / 0x19, 0x36 / 0x3b on himself, 0x3d at a target with status bits,
// and his big spell 0x46 as attack 2), strikes his target from afar, sends ground waves and
// counters evasions with a burst of light. The first form, at two thirds of its HP, turns
// into the second (attack 4: the kind-0x3b SpellCast whose handler lives in this block):
// he flies from then on, a halo (anim 0xd) floats over him and his spells change.

#include "game/battle/bsfx.h"
#include "game/battle/camera.h"
#include "game/battle/combat.h"
#include "game/battle/enemy_classes.h"
#include "game/battle/enemy_modules.h"
#include "game/battle/enemy_support.h"
#include "game/battle/killer.h"
#include "game/battle/particle.h"
#include "game/battle/render.h"
#include "game/battle/shot.h"
#include "game/battle/sprite.h"
#include "game/battle/stage.h"
#include "game/boot/rng.h"
#include "game/boot/sound.h"
#include "runtime/runtime.h"

namespace ss::game::battle {
namespace {

u32 rng15() { return static_cast<u32>(rng_next() & 0x7FFF); }  // rng_next15 0x800103b4

// The unit's centre (its body's mid height).
Vec4 centre_of(const Unit* u) { return {u->pos[0], u->pos[1] + u->size[1] * -0x2000, u->pos[2], 0}; }

void set_vel(BObj* o, const Vec4& v) {
  o->vel[0] = v.x;
  o->vel[1] = v.y;
  o->vel[2] = v.z;
}

// (g_spell_data_entry 0x8006d1f2: spell_data_entry_set, spells.h.)

// The rising mote (anim 0x24) of the second form: a random x within 300, 0x190 up,
// floating up faster and faster.
void spawn_mote(Unit* u) {
  Sprite* sp = unit_play_anim(u, 0x24);
  if (sp == nullptr) return;
  const s32 dx = rng_between_fixed(-300, 300);
  const s32 p[3] = {u->pos[0] + dx, u->pos[1] - 0x190000, u->pos[2]};
  sprite_set_pos_v(sp, p);
  sp->vel[1] = 0x1000;
  sp->accel[1] = 0x400;
  sprite_set_fade(sp, 0x80, -4, 8);
  sp->layer = -0x10;
}

void shot_strike(Shot* s);
void shot_quake(Shot* s);
void shot_spiral(Shot* s);
void shot_burst(Shot* s);

struct EnemyIndalecio : EnemyUnit {
  s32 spell_cooldown = 0;  // +0x26c: target choices before attack 2 may come again
  BObj halo;               // +0x270: the second form's halo (anim 0xd), 0x88 bytes
  u8 transformed = 0;      // +0x2f8: the second form

  // 0x80088900: enemy_create, the module's vtable, the halo's bobj_create, the intro voice.
  EnemyIndalecio() {
    intro_voice = 0x4EC;
    voice_variants = 2;
  }

  // 0x80088958 [34]: a voice clip from the pack's extra data: hit (0) the second, attacks
  // 1 and 3 the first.
  void on_attack_start(int attack) override {
    const void* clip;
    if (attack == 1 || attack == 3) {
      clip = enemy_extra_data(0);
    } else if (attack == 0) {
      clip = enemy_extra_data(1);
    } else {
      return;
    }
    // (The console plays unconditionally; the port has no clip until the table is exposed.)
    if (clip != nullptr) bsfx_play(0, clip, this, 1);
  }

  // 0x800889d0 [22]: the usual animation; the second form also draws the halo over him
  // (0x1f4 up, bobbing with the hover at half its rate) and now and then a rising mote.
  int animate(u32 anim_flags) override {
    BattleGlobals& g = bg();
    const int f = EnemyUnit::animate(anim_flags);
    if (transformed == 0) return f;
    halo.pool = g.prim_pool_a;
    bobj_share_sprite(this, &halo);
    halo.anim = 0xD;
    halo.layer = -10;
    halo.pos[0] = pos[0];
    const s32 bob = bsin(static_cast<u32>((hover_phase & 0x3F) << 6));
    halo.pos[2] = pos[2];
    halo.facing[1] = facing[1];
    halo.facing[2] = facing[2];
    halo.scale_x = scale_x;
    halo.scale_y = scale_y;
    halo.facing[0] = facing[0];
    halo.pos[1] = pos[1] + (bob * hover_amp - 0x1F4000);
    if (shake_frames != 0) halo.pos[0] += shake_amp * 0x1000;
    bobj_project(&halo);
    bobj_anim_draw(&halo, anim_flags | (g.flags & 0xFE));
    if (g.flags == 0 && rng15() < 0x2000 && g.state == kStateFighting) spawn_mote(this);
    // (The console returns the frame as a u16; the port's s16 convention is kept.)
    return f;
  }

  // 0x80088bb4 [26]: the first form transforms at two thirds of its HP (attack 4, message
  // 0x2722). Else, when he can cast and wins a 17000 / 32768 roll (fighting), the second
  // form casts: at whoever hit him during a counter window or a random member; with status
  // bits 0x2a7 of his own spell 0x36 on himself; a target with status bits outside 0x2a7 1
  // in 4 spell 0x3d; without status bit 0x2000 12000 / 32768 spell 0x3b on himself; else
  // one of the table 0x8008bbf8 {0x14, 3, 0x19}. Else, once the cooldown is over, the second
  // form approaches for attack 2 (message 0x42d and a voice); else attack 3 from up to
  // 0x226 away (message 0x2721); else the usual choice.
  void choose_target() override {
    if (spell_cooldown != 0) --spell_cooldown;
    const Stats* s = st();
    const s16 type = static_cast<s16>(s->id);
    if (type == 0xCE && transformed == 0 && s->hp <= (s->hp_max << 1) / 3) {
      battle_message_show(0x2722);
      action_kind = 4;
      begin_action();
      return;
    }
    bool cast = false;
    if (can_cast() != 0 && bg().state == kStateFighting) cast = rng15() < 17000;
    if (cast && (type == 0xCF || transformed != 0)) {
      if (counter_window != 0) {
        target = last_attacker;
        counter_window = 0;
        last_attacker = nullptr;
      } else {
        target = enemy_random_party_member(this);
      }
      s32 pick = 0x36;
      if ((status_bits & 0x2A7) != 0) {
        target = this;
      } else if (target != nullptr && (target->status_bits & 0xFFFFFD58u) != 0 && rng15() < 0x2000) {
        pick = 0x3D;
      } else if ((status_bits & 0x2000) == 0 && rng15() < 12000) {
        pick = 0x3B;
        target = this;
      } else {
        pick = enemy_module_s32(kSlotD, 0x8008BBF8 + 4 * rng_range(3));
      }
      if (pick == 0) return;
      spell = static_cast<u8>(pick);
      unit_begin_cast(this);
      action_kind = 5;
      move_mode = 0;
      begin_action();
      return;
    }
    if (spell_cooldown == 0 && enemy_try_approach(this, 1000, 0x2400, 2, 0x280) != 0 &&
        (type == 0xCF || transformed != 0)) {
      counters[2] = 0;
      counters[3] = 0;
      battle_message_show(0x42D);
      const int voice = transformed != 0 ? static_cast<int>(rng_range(3)) + 0x518 : static_cast<int>(rng_range(3)) + 0x4EF;
      voice_play_entry(voice, this);  // (third argument -1)
      action_kind = 2;
      begin_action();
      return;
    }
    if (enemy_try_approach(this, 0x226, 0x2000, 3, 0x280) == 0) {
      EnemyUnit::choose_target();
      return;
    }
    battle_message_show(0x2721);
    action_kind = 3;
    begin_action();
  }

  // 0x80088eac [4]: interrupted, the spell data he chants (state 10) or holds goes; an
  // evasion (reaction 2) of the second form (with +0x174 clear) bursts into light.
  void interrupt() override {
    if (action == 10) spell_data_cancel_owner(this);
    if (spell != 0) {
      spell_data_cancel_owner(this);
      spell = 0;
    }
    if (reaction != 2) return;
    if (static_cast<s16>(st()->id) != 0xCF && transformed == 0) return;
    if (counters[0] != 0) return;
    Shot* s = shot_spawn_callback(shot_burst);
    if (s == nullptr) return;
    s->owner = this;
    s->p130 = 0xF0;
    bobj_share_sprite(this, s);
  }

  // 0x80088f70 [27]: past frame 1 a strike falls on the target (a Shot above its head).
  void attack1() override {
    anim = 4;
    loop_limit = 1;
    if (static_cast<s16>(animate(static_cast<u32>(attack_recovery))) == -1) {
      action_kind = 0;
      begin_action();
      return;
    }
    if (bg().flags != 0 || frame < 2 || counters[1] != 0) return;
    counters[1] = 1;
    bsfx_play_at(0x21, this);
    sound_handle_fade_out(bsfx_play_at(0x2D, this), 0x28, 0x14);
    if (target == nullptr) return;
    Shot* s = shot_spawn_callback(shot_strike);
    if (s == nullptr) return;
    shot_place_at_unit(s, target, 0, target->size[1] << 1, 0);
    s->owner = this;
    bobj_share_sprite(this, s);
  }

  // 0x800890b4 [9]: chants spell 0x27 at level 0x16.
  int skill_level(int skill) override { return skill == 0x27 ? 0x16 : 0; }

  // 0x800890c8 [14]: has effect bit 24 of word 0 (and no other).
  int status_test(int word, u32 mask) override { return word == 0 ? static_cast<int>((mask >> 24) & 1) : 0; }

  // 0x800890e0 [28]: spell 0x46 (+0x178 data requested, +0x17c step: 0 loading, 1 cast at
  // the next loop of anim 9, 2 cast, +0x180 ticks); he keeps his distance meanwhile. The
  // data is requested at once, the cast comes after 60 ticks once it has loaded, then 20
  // target choices go by before the next. It ends once a kind-0x3a SpellCast is past step
  // 7; late in the end countdown it is interrupted.
  void attack2() override {
    BattleGlobals& g = bg();
    if (!(g.state < 3 || 0x13 < g.end_countdown)) {
      interrupt();
      end_attack();
      return;
    }
    if (const SpellCast* sc = spellcast_slot0(); sc != nullptr && sc->kind == 0x3A && 7 < sc->step) {
      end_attack();
      return;
    }
    anim = 9;
    loop_limit = 0;
    animate(0);
    if (g.flags != 0) return;  // (the console tests bit 0x40 first, then any flag)
    reposition_step();
    if (counters[2] == 1 && loops != 0) {
      counters[2] = 2;
      spell_cooldown = 0x14;
      spell_cast(0x46, this, target, 0);
      return;
    }
    if (counters[1] == 0) {
      counters[1] = 1;
      spell_data_request(0x46, 0x3C, this);
      if (counters[1] == 0) return;
    }
    counters[3] += g.frame_ticks;
    if (counters[3] < 0x3D || counters[2] != 0) return;
    if (spell_data_status(0x46, this) == 1) return;
    counters[2] = 1;
    bobj_anim_reset(this);
  }
  void end_attack() {
    action_kind = 0;
    begin_action();
  }

  // 0x800892d0 [29]: past frame 1 a ground wave from his feet (at height -0xc0).
  void attack3() override {
    anim = 4;
    loop_limit = 1;
    if (static_cast<s16>(animate(static_cast<u32>(attack_recovery))) == -1) {
      action_kind = 0;
      begin_action();
      return;
    }
    if (bg().flags != 0 || frame < 2 || counters[1] != 0) return;
    counters[1] = 1;
    bsfx_play_at(0x2A, this);
    bsfx_play_at(0x21, this);
    bsfx_play_at(0x22, this);
    Shot* s = shot_spawn_callback(shot_quake);
    if (s == nullptr) return;
    shot_place_at_unit(s, this, 0, 0, 0);
    s->pos[1] = -0xC0000;
    s->owner = this;
    bobj_share_sprite(this, s);
  }

  // 0x80089400 [30]: the transformation: at the end of a loop (fighting) the kind-0x3b
  // SpellCast starts (spellcast_kind3b_transform below runs it); until it is running he
  // holds anim 0x2e. The cast ends the attack.
  void attack4() override {
    BattleGlobals& g = bg();
    special_timer = 8;
    if (g.flags == 0 && loops != 0 && counters[0] == 0 && g.state == kStateFighting) {
      counters[0] = 1;
      spell_effect_started_flag() = 1;
      spellcast_create(0x3B, this, target, 0, nullptr);
    }
    const SpellCast* sc = spellcast_slot0();
    if (sc == nullptr || sc->kind != 0x3B) {
      anim = 0x2E;
      loop_limit = 0;
      animate(0);
    }
  }

  // 0x800894dc [31]: the chant (anim 0xf): he keeps his distance; when the cast time
  // (+0x138) runs out, action 6 (the release).
  void attack5() override {
    BattleGlobals& g = bg();
    anim = 0xF;
    loop_limit = 0;
    animate(0);
    if (g.flags != 0) return;
    reposition_step();
    cast_time = static_cast<s16>(static_cast<u16>(cast_time) - g.frame_ticks);
    if (0 < cast_time) return;
    action_kind = 6;
    begin_action();
  }

  // 0x80089584: keeping his distance while casting (not the first form): when not walking
  // (+0x12b), up to 10 tries for a spot -+0x400 in x and 0x200 to one side in z (on the
  // ground unless flying) no nearer to the nearest member than he is now and free to move
  // to; found, he walks there (+0x12b = 1, slot 33) until he arrives.
  void reposition_step() {
    if (static_cast<s16>(st()->id) == 0xCE && transformed == 0) return;
    if (bg().flags != 0) return;
    if (move_mode != 0) {
      if (unit_move_toward_goal(this, 1) != 0) move_mode = 0;
      return;
    }
    s32 dist = 0;
    const int idx = enemy_nearest_party_index(this);
    if (idx >= 0) dist = vec_distance_fx(pos, bg().party_units[idx]->pos);
    bool found = false;
    for (int i = 0; i < 10; ++i) {
      s32 d[3];
      d[0] = rng_spread_fixed(0x400);
      d[2] = rng_range(2) != 0 ? -0x200000 : 0x200000;
      if (flying != 0) {
        d[1] = 0;
      } else {
        d[1] = stage_ground_height((pos[0] + d[0]) >> 12, (pos[2] + d[2]) >> 12) - pos[1];
      }
      walk_dest[0] = pos[0] + d[0];
      walk_dest[1] = pos[1] + d[1];
      walk_dest[2] = pos[2] + d[2];
      if (vec_distance_fx(walk_dest, pos) < dist) continue;
      if (unit_check_move(this, d, 7) == nullptr) {
        found = true;
        break;
      }
    }
    if (!found) return;
    move_mode = 1;
    begin_move();
  }

  // 0x80089754 [35]: the chant's state keeps his distance too.
  void state_cast() override {
    reposition_step();
    EnemyUnit::state_cast();
  }

  // 0x8008b3d4 [25]: his death (the end countdown held at 0x78): the camera closes in, then
  // for 600 ticks beams and shards pour out of him (a voice at 60, a fade at 520); then a
  // last voice, 64 beams, 64 sparks, a sphere and spark rings, and he is removed (one enemy
  // fewer). Anim 0xb throughout.
  void state_dying() override {
    BattleGlobals& g = bg();
    g.end_countdown = 0x78;
    if (g.flags == 0) {
      if (counters[0] == 0) {
        counters[3] = 0;
        counters[2] = 0;
        counters[1] = 0;
        counters[0] = 1;
        const u32 h = bsfx_play_at(0x46, this);
        sound_handle_fade_out(h, 0x3C, 0x258);
        sound_handle_pitch_up(h, 0x258);
        bsfx_play_at(0x21, this);
        camera_reset_mode(g.camera);
        camera_move_to(g.camera, pos, 0xC00, 0x155, 0x1000, 0, 0x80, 5);
        camera_move_to(g.camera, pos, 0xC00, 0x71, 0x800, 0x12C, 0x80, 5);
      } else if (counters[0] == 1) {
        dying_burst();
      }
    }
    anim = 0xB;
    animate(0);
  }
  void dying_burst() {
    BattleGlobals& g = bg();
    for (int i = 0; i < 4; ++i) {
      const Vec4 c = centre_of(this);
      const Vec4 d = vec_random_around(c.data(), 0x3C0);
      const Vec4 from = centre_of(this);
      const s32 len = rng_between(0x140, 0x280);
      const s32 speed = rng_between_fixed(8, 0x10);
      const s32 width = rng_between(0x400, 0x1000);
      effect_spawn_beam_at(from.data(), d.data(), 1, len, 0x20, speed, width, 0);
    }
    const Vec4 c = centre_of(this);
    const s32 yaw = static_cast<s32>(rng_range(0x1000));
    const s32 pitch = static_cast<s32>(rng_range(0x1000));
    const s32 yaw_speed = rng_between(-10, 10);
    const s32 pitch_speed = rng_between(-10, 10);
    particle_spawn_shard(c.data(), yaw, pitch, 0x800, yaw_speed, pitch_speed, 0xFF8000, 0x1E);
    counters[1] += g.frame_ticks;
    if (counters[3] == 0 && 0x3C < counters[1]) {
      counters[3] = 1;
      const int voice = transformed != 0 ? static_cast<int>(rng_range(2)) + 0x523 : static_cast<int>(rng_range(2)) + 0x4FA;
      voice_play_entry(voice, this);  // (third argument 0)
    }
    if (0x208 < counters[1] && counters[2] == 0) {
      counters[2] = 1;
      screen_fade_create(0, 0x20, -4, 0x14);
    }
    if (counters[1] <= 600) return;
    voice_play_entry(transformed != 0 ? 0x525 : 0x4FC, this);  // (third argument 0)
    counters[0] = 0;
    bsfx_play_at(0x50, this);
    sound_handle_fade_out(bsfx_play_at(0x2D, this), 0x3C, 0x14);
    bsfx_play_at(0x22, this);
    bsfx_play_at(0x2A, this);
    camera_move_to(g.camera, pos, 0xC00, 0x88, 0x1000, 0x1E, 0x80, 5);
    for (int i = 0; i < 0x40; ++i) {
      const Vec4 cc = centre_of(this);
      const Vec4 d = vec_random_around(cc.data(), 0x3C0);
      const Vec4 from = centre_of(this);
      const s32 len = rng_between(0x140, 0x280);
      const s32 speed = rng_between_fixed(1, 4);
      const s32 width = rng_between(0x400, 0x1000);
      effect_spawn_beam_at(from.data(), d.data(), 0xF, len, 0x20, speed, width, 0);
    }
    for (int i = 0; i < 0x40; ++i) {
      const Vec4 cc = centre_of(this);
      const Vec4 v = vec_random_around(kZeroVector, 0x40);
      particle_spawn_spark_line(cc.data(), v.data(), 0, 0x80FF, 0x20, 0x3C);
    }
    const s32 ground[3] = {pos[0], bobj_ground_height(this), pos[2]};
    effect_spawn_sphere_at_default(ground, 8, 8, 0x1E, 0x78, 0xFF8000, 0x60, 0);
    const Vec4 c1 = centre_of(this);
    effect_spawn_sparks_at(c1.data(), 0x14, 0x100, 0, 0x60, 0x5A, 1, 3, 0x40, 0);
    for (int i = 0; i < 2; ++i) {
      const Vec4 c2 = centre_of(this);
      if (Shot* ring = effect_spawn_sparks_at(c2.data(), 0x14, 0x200, 0, 0x10, 0xB4, 0xB, 3, 0x40, 0))
        ring->vel[2] = i << 10;
    }
    removed = 1;
    g.enemy_count = static_cast<u8>(g.enemy_count - 1);
  }

  // 0x8008ba88 [36]: the dtor (EnemyUnit vtable, unit_destroy).
};

// ---- The transformation: SpellCast kind 0x3b ----------------------------------------
// g_spell_kind_handlers[0x3b] (0x80088020 table) is 0x80089c80, in this block: the
// SpellCast that attack 4 starts runs it every frame (spellcast_update 0x80085b0c, task
// priority 3) until it sets +0x0e. Its fields: +0x20 (s20) timer, +0x24 (s24) flag, +0x34
// (s34) the light funnel's turn, +0x38 (s38) fade, +0x3c (s3c) the halo's height above
// him, +0x40 step, +0x44 the wait, +0x1bc (obj2) the funnel's cel.
// 0x80089c80: the transformation. Steps 0-6 draw him (he is frozen in the spell pause)
// with his shadow, and the halo once he has it; steps 3-6 draw a funnel of light round him
// (10 bands x 10 segments of anim 0x24's cel, a bowl from 0x500 wide and 0x32 up to
// -0x5dc up at the axis, turning). The wait timer holds the steps. 0: a fade to black, a
// flash, sounds, the music fades, wait 150. 1: an afterimage, anim 0x2f, he rises to
// -0x3e8 with 64 streaks drawn in, a voice, the camera. 2: he falls 2 a tick; landing:
// sounds, a shake, shards, three spark rings, the funnel's colour from 0. 3: the funnel
// brightens, spiral sparks rise for 60 ticks. 4: rings and sprites gather at the top
// (-0x60e), at 180 a fade to white, at 270 sounds and wait 120 with the halo at -0x5dc. 5:
// the second form: animation set +0xc0, the halo shared, flying 0x50 up (hover 0x10),
// faster, no status; 64 beams, 4 rings, the battle music again, a voice, his second-form
// voices and spells. 6: the funnel fades as the halo sinks to -0x1f4. 7: a flash back, the
// cast ends and so does attack 4.
void transform_run(SpellCast* sc) {
  BattleGlobals& g = bg();
  auto* u = static_cast<EnemyIndalecio*>(sc->caster);
  if (sc->step < 7) {
    bobj_project(u);
    bobj_anim_draw(u, g.flags & 0xC);
    s32 r = u->ext_x1 < 0 ? -u->ext_x1 : u->ext_x1;
    const s32 r0 = u->ext_x0 < 0 ? -u->ext_x0 : u->ext_x0;
    if (r < r0) r = r0;
    r = r * 4 + 0x20;
    bobj_draw_shadow(u, -r, r, -r, r, 0xFFFFFFFF);
    if (u->transformed != 0) {
      BObj& h = u->halo;
      h.layer = -8;
      h.pool = g.prim_pool_a;
      h.anim = 0xD;
      h.pos[0] = u->pos[0];
      h.pos[1] = u->pos[1] + sc->s3c;
      h.pos[2] = u->pos[2];
      h.facing[0] = u->facing[0];
      h.facing[1] = u->facing[1];
      h.facing[2] = u->facing[2];
      h.scale_x = u->scale_x;
      h.scale_y = u->scale_y;
      bobj_project(&h);
      bobj_anim_draw(&h, g.flags & 0xC);
    }
  }
  if (static_cast<u32>(sc->step - 3) < 4) {
    BObj* ring = &sc->obj2;
    ring->anim = 0x24;
    bobj_anim_draw(ring, (g.flags & 0xC) | 0x9800u);
    const FrameUv& f = frame_uv();
    const s32 uv[4] = {f.u0, f.v0, f.u1, f.v1};
    const u16 tpage = f.tpage, clut = f.clut;
    const u32 code = bobj_poly_tag(ring, tpage);
    s32 twist = 0;
    for (int band = 0; band < 10; ++band, twist += 0xC000) {
      const s32 o = 10 - band, i = 9 - band;
      const s32 radius[2] = {band * 0x80, band * 0x80 + 0x80};
      const s32 base[2] = {twist / 360 + sc->s34, twist / 360 + sc->s34 + 0x88};
      const s32 y[2] = {-0x32000 - (((o * 30 * o * o * o) / 200) << 12), -0x32000 - (((i * 30 * i * i * i) / 200) << 12)};
      for (int seg = 0; seg < 10; ++seg) {
        // (The console builds the corners in the scratchpad at 0x1f8003c0.)
        s32 c[4][4] = {};
        for (int k = 0; k < 4; ++k) {
          const int side = k >> 1;
          const s32 a = base[side] + ((seg + (k & 1)) * 0x24000) / 360;
          c[k][0] = bcos(static_cast<u32>(a)) * radius[side] + u->pos[0];
          c[k][1] = y[side];
          c[k][2] = bsin(static_cast<u32>(a)) * radius[side] + u->pos[2];
        }
        draw_textured_patch(uv, c, clut, tpage, 8, code, 1);
      }
    }
    if ((g.flags & 0xC) != 0) return;
    sc->s34 = (sc->s34 + g.frame_ticks * 10) & 0xFFF;
  }
  if ((g.flags & 0xC) != 0) return;
  if (spellcast_timer_tick(sc) != 0) return;
  const s32 top[3] = {u->pos[0], -0x60E000, u->pos[2]};
  switch (sc->step) {
    case 0: {  // 0x8008a240
      screen_fade_create(0, 0x20, -8, 0x78);
      battle_flash_set(sc, 0x18, 0x18, 0x18);
      bsfx_play_at(0x2A, u);
      bsfx_play_at(0x22, u);
      spellcast_wait(sc, 0x96);
      bobj_share_sprite(u, &sc->obj2);
      sc->obj2.pool = g.prim_pool_a;
      sound_fade_out(g.sound, 500);
      break;
    }
    case 1: {  // 0x8008a2cc
      u->effects[0] = afterimage_create(u, 4, 0x5080, 1);
      u->anim = 0x2F;
      u->pos[1] = -0x3E8000;
      for (int i = 0; i < 0x40; ++i) {
        const Vec4 c = centre_of(u);
        const Vec4 d = vec_random_around(c.data(), 0x640);
        const Vec4 from = centre_of(u);
        particle_spawn_homing_line(from.data(), d.data(), 0x800000, 0xFFFF00, 0x140, 0x14000, -0x100);
      }
      voice_play_entry(static_cast<int>(rng_range(2)) + 0x4F8, u);  // (third argument -1)
      camera_move_to(g.camera, u->pos, 0xA38, 0xE3, 0xC00, 0, 0x80, 5);
      const s32 ground[3] = {u->pos[0], bobj_ground_height(u), u->pos[2]};
      camera_move_to(g.camera, ground, 0xC00, 0x71, 0x800, 0x294, 0x80, 5);
      bsfx_play_at(0x28, u);
      bsfx_play_at(0x29, u);
      bsfx_play_at(0x21, u);
      const u32 h = bsfx_play_at(0x44, u);
      sound_handle_fade_out(h, 0x1E, 0x258);
      sound_handle_pitch_down(h, 0x258);
      ++sc->step;
      break;
    }
    case 2: {  // 0x8008a4e8
      u->pos[1] += g.frame_ticks * 0x2000;
      if (u->effects[0] != nullptr) u->effects[0]->next_kind = 1;
      const s32 gy = bobj_ground_height(u);
      if (gy <= u->pos[1]) {
        bsfx_play_at(0x29, u);
        bsfx_play_at(0x21, u);
        sound_handle_fade_out(bsfx_play_ui(0x2D), 0x1E, 0x1E);
        screen_shake2(4, 2, 4, 4, 0x1F4);
        const u32 h = bsfx_play_at(0x35, u);
        sound_handle_fade_out(h, 0x3C, 0x168);
        sound_handle_pitch_up(h, 0x168);
        if (u->effects[0] != nullptr) u->effects[0]->ending = 1;
        u->effects[0] = nullptr;
        u->pos[1] = gy;
        ++sc->step;
        if (Sprite* sp = unit_play_anim_ex(u, 0x16, 1)) {
          sprite_set_pos_v(sp, u->pos);
          sp->layer = 0xC;
        }
        for (int i = 0; i < 0x18; ++i) {
          const s32 yaw = static_cast<s32>(rng_range(0x1000));
          const s32 pitch = static_cast<s32>(rng_range(0x1000));
          const s32 yaw_speed = rng_between(-10, 10);
          const s32 pitch_speed = rng_between(-10, 10);
          particle_spawn_shard(u->pos, yaw, pitch, 0x400, yaw_speed, pitch_speed, 0xFFFFFF, 0x5A);
        }
        for (int i = 0, dy = 0; i < 3; ++i, dy += 0x64000) {
          const s32 p[3] = {u->pos[0], u->pos[1] - dy, u->pos[2]};
          if (Shot* ring = effect_spawn_sparks_at(p, 0x18, 0x140, 0, 0x18 - i * 4, 0xF0, 0, 2, 0xB4, 0)) {
            ring->anim = 0x13;
            bobj_share_sprite(u, ring);
          }
        }
        sc->obj2.colour_mode = 2;
        sc->obj2.rgb[0] = 0;
        const s32 high[3] = {u->pos[0], -0x3E8000, u->pos[2]};
        camera_move_to(g.camera, high, 0xC00, -0x71, 0xC00, 0x258, 0x80, 5);
      }
      if (Sprite* sp = unit_play_anim(u, 0x17)) {
        const Vec4 p = vec_random_in_disc_xz(u->pos, 800);  // (y 0)
        sprite_set_pos_v(sp, p.data());
        const s32 ground[3] = {u->pos[0], bobj_ground_height(u), u->pos[2]};
        set_vel(sp, vec_velocity_toward(sp->pos, ground, 10));
        sprite_set_fade(sp, 0x80, -4, 8);
        sp->layer = 0xC;
      }
      break;
    }
    case 3: {  // 0x8008a834
      const u8 lit = static_cast<u8>(sc->obj2.rgb[0] + g.frame_ticks * 2);
      sc->obj2.rgb[0] = lit;
      if (0x80 < lit) sc->obj2.rgb[0] = 0x80;
      for (int i = 0; i < 2; ++i) {
        Shot* s = shot_spawn_callback_always(shot_spiral);
        if (s == nullptr) continue;
        shot_place_at_unit(s, u, 0, 0, 0);
        s->point[0] = s->pos[0];
        s->point[1] = s->pos[1];
        s->point[2] = s->pos[2];
        s->trail_rgb = static_cast<u32>(s->pos_w);  // (+0x124 = +0x14: the copy's fourth word)
        s->p128 = static_cast<s32>(rng_range(0x1000));
        s->p12c = static_cast<s32>(rng_range(0x1000));
        s->p130 = 0x280;
        s->anim = 0x14;
        bobj_share_sprite(u, s);
      }
      sc->s20 += g.frame_ticks;
      if (0x3C < sc->s20) {
        sc->step = 4;
        sc->s20 = 0;
        sc->s24 = 0;
      }
      break;
    }
    case 4: {  // 0x8008a92c
      if (Shot* ring = effect_spawn_sparks_at(top, 0x18, 0x140, 0, -0x14, 0x3C, 0, 2, 0x3E8, 2)) {
        ring->anim = 0x13;
        ring->vel[2] = static_cast<s32>(rng_range(0x1000));
        bobj_share_sprite(u, ring);
      }
      if (Sprite* sp = unit_play_anim_ex(u, 0x15, 1)) {
        const Vec4 p = vec_random_around(top, 0x4B0);
        sprite_set_pos_v(sp, p.data());
        set_vel(sp, vec_velocity_toward(sp->pos, top, 0x24));
        sprite_set_fade(sp, 0x80, -4, 8);
        sp->scale_vel[1] = 0x80;
        sp->scale_vel[0] = 0x80;
        sp->layer = 0xC;
      }
      sc->s20 += g.frame_ticks;
      if (0xB4 < sc->s20 && sc->s24 == 0) {
        screen_fade_create(0, 0x20, -4, 0x96);
        sc->s24 = 1;
        const u32 h = bsfx_play_at(0x2E, u);
        sound_handle_fade_out(h, 0x3C, 0x78);
        sound_handle_pitch_down(h, 0x78);
      }
      if (0x10E < sc->s20) {
        spellcast_wait(sc, 0x78);
        sc->s20 = 0;
        bsfx_play_ui(0x2A);
        bsfx_play_ui(0x29);
        bsfx_play_ui(0x21);
        bsfx_play_ui(0x22);
        sc->s3c = -0x5DC000;
      }
      break;
    }
    case 5: {  // 0x8008aae0
      u->animset->layout = u->model_layout + 0xC0;
      bobj_share_sprite(u, &u->halo);
      u->transformed = 1;
      u->flying = 1;
      u->pos[1] = -0x50000;
      u->hover_height = -0x50000;
      u->hover_amp = 0x10;
      u->run_speed += 0x38;
      u->base_speed += 0x38;
      u->status_bits = 0;
      sc->s38 = 0;
      sc->s34 = 0;
      camera_move_to(g.camera, u->pos, 0xC00, 0x71, 0xC30, 0x12C, 0x80, 5);
      sc->step = 6;
      for (int i = 0; i < 0x40; ++i) {
        const Vec4 d = vec_random_around(top, 0x3C0);
        const s32 len = rng_between(0x140, 0x1E0);
        const s32 speed = rng_between_fixed(1, 4);
        const s32 width = rng_between(0x400, 0x1000);
        effect_spawn_beam_at(top, d.data(), 4, len, 0x18, speed, width, 0);
      }
      for (int i = 0; i < 4; ++i) {
        if (Shot* ring = effect_spawn_sparks_at(top, 0x18, 0x500, 0, 0x60, 0x78, 0, 2, 0xB4, 0)) {
          ring->vel[2] = i << 9;
          ring->anim = 0x13;
          bobj_share_sprite(u, ring);
        }
      }
      sound_play_sequence(g.sound, g.battle_music.data());
      voice_play_entry(static_cast<int>(rng_range(2)) + 0x515, u);  // (third argument -1)
      const u32 h = bsfx_play_at(0x44, u);
      sound_handle_fade_out(h, 0x3C, 0x78);
      sound_handle_pitch_down(h, 0x78);
      // The second form's voice clips (into the pack's extra data buffers) and spell data.
      bload_request(0x517, const_cast<u8*>(enemy_extra_data(0)), 5, -1);
      bload_request(0x51C, const_cast<u8*>(enemy_extra_data(1)), 5, -1);
      spell_data_entry_set(0x46, 0, 0x982);
      spell_data_entry_set(3, 0, 0x91B);
      spell_data_entry_set(0x14, 0, 0x970);
      spell_data_entry_set(0x19, 0, 0x97B);
      spell_data_entry_set(7, 0, 0x914);
      spell_data_entry_set(0x1C, 0, 0x925);
      spell_data_entry_set(0x31, 0, 0x93D);
      spell_data_entry_set(0x36, 0, 0x950);
      spell_data_entry_set(0x3D, 0, 0x95D);
      spell_data_entry_set(0x3B, 0, 0x958);
      break;
    }
    case 6: {  // 0x8008adb0
      // ORIGINAL BUG (original_bugs.md E8): the first frame is told by +0x34 == 0, but the
      // funnel above has already turned +0x34 this frame, so the halo's afterimage is never
      // made (and its "= 1" would turn the funnel):
      //   if (sc->s34 == 0) { u->effects[0] = afterimage_create(...); sc->s34 = 1; }
      // Step 5 clears the fade +0x38 too, and the fade only grows from here: its 0 marks
      // the first frame.
      if (sc->s38 == 0) u->effects[0] = afterimage_create(&u->halo, 6, 0x802000, 1);
      sc->obj2.rgb[0] = static_cast<u8>(-0x80 - sc->s38);
      sc->s38 += g.frame_ticks * 4;
      if (0x80 < sc->s38) sc->s38 = 0x80;
      if (u->effects[0] != nullptr) u->effects[0]->next_kind = 1;
      sc->s3c += g.frame_ticks * 0x4000;
      if (-0x1F4000 < sc->s3c) {
        sc->s3c = -0x1F4000;
        ++sc->step;
        if (u->effects[0] != nullptr) u->effects[0]->ending = 1;
        u->effects[0] = nullptr;
      }
      if (rng15() < 0x2000) spawn_mote(u);
      break;
    }
    case 7: {  // 0x8008af48
      u->effects[0] = nullptr;
      battle_flash_set(sc, 0x80, 0x80, 0x80);
      sc->finished = 1;
      ++sc->step;
      u->action_kind = 0;
      u->begin_action();
      break;
    }
    default: break;
  }
}

// 0x80089784: attack 1's strike (anim 0x2a, one loop, the owner's depth, layer 8): on its
// first contact the owner hits the first unit met (power 0x300, knocked along its flight,
// element bit 4) with 8 fragments and a spark ring on it; dies at the end of the animation.
void shot_strike(Shot* s) {
  s->anim = 0x2A;
  s->loop_limit = 1;
  s->layer = 8;
  s->size[2] = s->owner->size[2];
  if (static_cast<s16>(shot_draw_and_box(s, 4, 0xFFFFFFFF)) == -1) {
    shot_kill(s);
    return;
  }
  Unit* units[8] = {};
  Vec4 points[8] = {};
  int hit = 0;
  if (collider_attack_overlaps(s, units, points, s->owner->is_enemy) != 0) hit = s->hit_landed == 0 ? 1 : 0;
  if (hit == 0) return;
  s->hit_landed = 1;
  // (The count passed is the landed flag, 1, not the number of units met.)
  unit_attack_targets(s->owner, hit, units, points, 0x300, s->vel[0] < 0 ? 0x800 : 0, 0x10);
  for (int k = 0; k < hit; ++k) {
    for (int i = 0; i < 8; ++i) {
      Sprite* sp = shot_spawn_sprite(s, static_cast<int>(rng_range(3)) + 0x2B);
      if (sp == nullptr) continue;
      sprite_set_pos_v(sp, units[k]->pos);
      sprite_set_fade(sp, 0x80, -2, 8);
      const Vec4 v = vec_random_in_disc_xz(kZeroVector, 0xC);
      sp->vel[0] = v.x;
      sp->vel[1] = v.y;
      sp->vel[2] = v.z;
      sp->extra34 = v.w;
      sp->scale_y = 0x2000;
      sp->scale_x = 0x2000;
    }
    const Vec4 c = centre_of(units[k]);
    effect_spawn_sparks_at(c.data(), 0x10, 0xC0, 0, 0x10, 0x5A, 2, 3, 0x80, 1);
  }
}

// 0x80089a00: attack 3's ground wave (a kind-0x33 Shot): a spark ring (anim 0x12) once; a
// band from radius +0x12c (0xf0, growing 8 a tick) 0x40 a tick wide hits every standing
// member whose distance falls in it and who is not above -0x64 (power 0x80, knocked along
// its facing); 120 ticks (+0x130).
void shot_quake(Shot* s) {
  BattleGlobals& g = bg();
  if (s->p128 == 0) {
    s->p128 = 1;
    if (Shot* ring = effect_spawn_sparks_at(s->pos, 0x18, 0x142, 0, 0x10, 0x8C, 0, 2, 0x30, 1)) {
      ring->anim = 0x12;
      bobj_share_sprite(s, ring);
    }
    s->p12c = 0xF0;
  }
  if (g.flags != 0) return;
  s->p130 += g.frame_ticks;
  if (0x78 < s->p130) {
    shot_kill(s);
    return;
  }
  Unit* units[8] = {};
  Vec4 points[8] = {};
  int n = 0;
  for (PartyUnit* u : g.party_units) {
    if (unit_is_down(u)) continue;
    const s32 d = vec_distance_fx(s->pos, u->pos) >> 12;
    if (d < s->p12c || s->p12c + g.frame_ticks * 0x40 <= d || u->pos[1] <= -0x64000) continue;
    units[n] = u;
    points[n] = centre_of(u);
    ++n;
  }
  if (n != 0) unit_attack_targets(s->owner, n, units, points, 0x80, s->facing[0] < 0 ? 0x800 : 0, 0);
  s->p12c += g.frame_ticks * 8;
}

// 0x8008afd8: the transformation's spiral spark (a kind-0x21 Shot, anim 0x14): circles the
// centre +0x118 at radius +0x130 (angle +0x128, 30 a tick) rising 8 a tick; above -0x2bc
// the circle narrows 4 a tick; gone above -0x6a4. Frozen only by the pause (flags 0xc).
void shot_spiral(Shot* s) {
  BattleGlobals& g = bg();
  s->pos[0] = s->point[0] + bcos(static_cast<u32>(s->p128)) * s->p130;
  s->pos[2] = s->point[2] + bsin(static_cast<u32>(s->p128)) * s->p130;
  bobj_project(s);
  bobj_anim_draw(s, g.flags & 0xC);
  if ((g.flags & 0xC) != 0) return;
  s->p128 = (s->p128 + g.frame_ticks * 0x1E) & 0xFFF;
  s->pos[1] -= g.frame_ticks * 0x8000;
  if (s->pos[1] < -0x2BC000) s->p130 -= g.frame_ticks * 4;
  if (s->pos[1] < -0x6A4000) shot_kill(s);
}

// 0x8008b0f0: the evasion burst: a 6-band x 8-segment cylinder of textured quads (bands
// 0x80 high; anim 0x11's cel, the top band anim 0x12's) of radius +0x130 round the owner,
// turning by +0x128 (20 a tick); it greys out (fade +0x12c, 4 a tick) and dies past 0x80.
void shot_burst(Shot* s) {
  BattleGlobals& g = bg();
  const s32 r = s->p130;
  s->layer = 3;
  s->pos[0] = s->owner->pos[0];
  s->pos[1] = s->owner->pos[1];
  s->pos[2] = s->owner->pos[2];
  s32 bottom = 0x80000;
  for (int band = 0; band < 6; ++band, bottom += 0x80000) {
    s->anim = band == 5 ? 0x12 : 0x11;
    bobj_anim_draw(s, g.flags | 0x9800u);
    const FrameUv& f = frame_uv();
    const s32 uv[4] = {f.u0, f.v0, f.u1, f.v1};
    const u16 tpage = f.tpage, clut = f.clut;
    // (No empty-cel test here, unlike the other textured Shots.)
    const u32 code = bobj_poly_tag(s, tpage);
    s32 c[4][4] = {};
    c[0][1] = c[1][1] = s->pos[1] - band * 0x80000;
    c[2][1] = c[3][1] = s->pos[1] - bottom;
    for (int seg = 0; seg < 8;) {
      const u32 a0 = static_cast<u32>(seg * 0x200 + s->p128);
      ++seg;
      const u32 a1 = static_cast<u32>(seg * 0x200 + s->p128);
      const s32 cos0 = bcos(a0), sin0 = bsin(a0), cos1 = bcos(a1), sin1 = bsin(a1);
      c[0][0] = c[2][0] = s->pos[0] + cos0 * r;
      c[0][2] = c[2][2] = s->pos[2] + sin0 * r;
      c[1][0] = c[3][0] = s->pos[0] + cos1 * r;
      c[1][2] = c[3][2] = s->pos[2] + sin1 * r;
      draw_textured_patch(uv, c, clut, tpage, s->layer, code, 1);
    }
  }
  if (g.flags != 0) return;
  s->colour_mode = 2;
  s->rgb[0] = static_cast<u8>(-0x80 - s->p12c);
  s->p12c += g.frame_ticks * 4;
  if (0x80 < s->p12c) {
    shot_kill(s);
    return;
  }
  s->p128 += g.frame_ticks * 0x14;
}

}  // namespace

void spellcast_kind3b_transform(SpellCast* sc) { transform_run(sc); }

std::unique_ptr<EnemyUnit> enemy_mod_indalecio_create() { return std::make_unique<EnemyIndalecio>(); }

}  // namespace ss::game::battle
