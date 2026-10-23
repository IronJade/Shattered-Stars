// Ashton's party-character module (#2147, slot-0 link 0x801ee000): the PartyUnit subclass
// (0x388 bytes, vtable 0x801f1e4c: slots 4, 6, 33, 35, 36, 37, 46), his 10 killer moves
// (states 0x10-0x19, direct pointers in the table 0x801f1fd0) and the Shot callbacks they
// spawn. Move ids (record +0xcc byte - 1); names from what the code does: 0 a swing whose
// hit sprites follow the height of the hit (stronger from a frame per level), 1 a running
// thrust (a ribbon trail; a burst of light on the second part), 2 a two-part slash (the
// dragons on his back flash, weapon variant), 3 a ball (or, with the dragons, lvl + 3
// darts), 4 a leap behind the target and a triangle of two clones and beams that strikes
// it, 5 the two dragon heads breathing fire for 243 ticks, 6 a slash throwing a wave, 7
// a sparkling dash that teleports to the target lvl + 2 times, 8 a charge-up and a beam,
// 9 a breath (spell 0x40 of his own) the dragons start.
#include <cstdlib>
#include <cstring>

#include "game/battle/battle.h"
#include "game/battle/bobj.h"
#include "game/battle/bsfx.h"
#include "game/battle/camera.h"
#include "game/battle/combat.h"
#include "game/battle/enemy_support.h"
#include "game/battle/killer.h"
#include "game/battle/particle.h"
#include "game/battle/pc_module_util.h"
#include "game/battle/pc_modules.h"
#include "game/battle/render.h"
#include "game/battle/shot.h"
#include "game/battle/spell_fx.h"
#include "game/battle/spellcast.h"
#include "game/battle/spells.h"
#include "game/battle/sprite.h"
#include "game/battle/stage.h"
#include "game/battle/unit_move.h"
#include "game/boot/rng.h"
#include "game/title/new_game.h"

namespace ss::game::battle {
namespace {

constexpr u32 kTrailRgb = 0x507020;                   // the afterimages (0x801ee3b4)
const s32 kZero[3] = {0, 0, 0};                       // 0x8006e33c

u32 rng_next15() { return static_cast<u32>(rng_next()) & 0x7FFF; }  // 0x800103b4

// A byte of the module's data (the slot-0 image).
s8 module_s8(u32 address) { return static_cast<s8>(pc_module_u8(7, address)); }

// The knock-back angle of a sweeping hit: up and away from the side o faces.
s32 sweep_angle(const BObj* o) { return o->facing[0] < 0 ? 0xB8E : static_cast<s32>(0xFFFFFC72); }

// A Shot's ribbon trail (the console keeps it at +0x134; created again while that is 0).
void shot_trail_on(Shot* s, u32 rgb_a, u32 rgb_b, bool no_lift) {
  if (s->p134 != 0) return;
  s->trail = ribbon_trail_create(s, 0x18, rgb_a, rgb_b);
  if (s->trail == nullptr) return;
  s->trail->back = &s->trail;
  if (no_lift) s->trail->no_lift = 1;
  s->p134 = 1;
}
void shot_trail_end(Shot* s) {
  if (s->p134 != 0 && s->trail != nullptr) s->trail->stop = 1;
}

// LoadAverageShort12: (a * pa + b * pb) >> 12 per component.
void average12(const s16 a[4], const s16 b[4], s32 pa, s32 pb, s16 out[4]) {
  for (int k = 0; k < 3; ++k) out[k] = static_cast<s16>((a[k] * pa + b[k] * pb) >> 12);
  out[3] = 0;
}

// ---- Shot callbacks ------------------------------------------------------------------------

// Mirrors a collider's boxes about its x position (min / max swapped).
void mirror_boxes_x(Collider* c) {
  for (int b = 0; b < 2; ++b) {
    const s32 bmn = c->body_min[b][0], bmx = c->body_max[b][0];
    c->body_min[b][0] = 2 * c->pos[0] - bmx, c->body_max[b][0] = 2 * c->pos[0] - bmn;
    const s32 amn = c->attack_min[b][0], amx = c->attack_max[b][0];
    c->attack_min[b][0] = 2 * c->pos[0] - amx, c->attack_max[b][0] = 2 * c->pos[0] - amn;
  }
}

// 0x801f11a8 (move 3, the dragons): a dart with a ribbon trail; on a hit a sprite stuck to
// the unit it met.
void cb_dart(Shot* s) {
  BattleGlobals& g = bg();
  s->size[0] = 0x18;
  s->size[2] = 0x10;
  s->size[1] = 6;
  // ORIGINAL BUG (original_bugs.md F15): the dart's facing is Ashton's x velocity (0 when he
  // throws), so the darts are drawn the way facing 0 points whichever way he faces; the
  // facing also places the attack box (mirrored with the drawing):
  //   shot_draw_and_box(s, 3, 0xFFFFFFFF);
  // The fix is play-neutral: the drawing follows his facing (kept in +0x128 at the spawn),
  // and the boxes are put back on the side the original facing gives them.
  const s32 box_facing = s->facing[0];
  s->facing[0] = s->p128;
  shot_draw_and_box(s, 3, 0xFFFFFFFF);
  s->facing[0] = box_facing;
  if (s->p128 != 0) {
    // The facings are x-only, so the view's sign of x is the drawn facing's sign times the
    // view matrix's: the original's mirroring follows from it.
    const bool drawn_flip = 0 < s->view_fx;
    const s32 view_sign = ((0 < s->view_fx) == (0 < s->p128)) ? 1 : -1;
    const bool box_flip = s->view_fx != 0 && 0 < static_cast<s64>(box_facing) * view_sign;
    if (drawn_flip != box_flip) mirror_boxes_x(s);
  }
  if (g.flags != 0) return;
  s->p130 += g.frame_ticks;
  if (s->p12c == 0 && 0x28 < s->p130) s->p12c = 1;
  shot_trail_on(s, 0x308000, 0x80C020, false);
  if (shot_fade_out(s) == 0) {
    if (shot_hit_melee(s, 0xC0, 0) == 0) {
      bobj_move(s, s->vel);
      return;
    }
    Unit* found[8] = {};
    Vec4 pts[8] = {};
    collider_attack_overlaps(s, found, pts, 0);
    if (Sprite* sp = shot_spawn_sprite(s, 7)) {
      const Unit* f = found[0];
      sprite_attach(sp, found[0], pts[0].x - f->pos[0], s->pos[1] - f->pos[1], pts[0].z - f->pos[2]);
      sp->facing[0] = s->facing[0];
      sprite_set_fade(sp, 0x80, -4, 0x10);
      sp->layer = 2;
      sp->blend = 2;
    }
    shot_kill(s);
  }
  shot_trail_end(s);
}

// 0x801f136c (move 4): a clone at a corner of the triangle; when its owner strikes (state
// 5 of move 4) it dives at him, fading.
void cb_clone(Shot* s) {
  BattleGlobals& g = bg();
  if (s->p130 != 0) {
    s->colour_mode = 2;
    s->blend = 2;
    s->rgb[0] = static_cast<u8>(0x80 - s->p130);
    if (0x7F < s->p130) {
      shot_kill(s);
      return;
    }
  }
  shot_draw_and_box(s, 4, 0xFFFFFFFF);
  if (g.flags != 0) return;
  const PartyUnit* o = static_cast<const PartyUnit*>(s->owner);
  if (o->action == 0x14 && o->removed == 0 && s->p130 == 0) {
    if (o->counters[1] != 5) return;
    if (s->p12c == 0) {
      s->p12c = 1;
      s->anim = static_cast<u16>(s->p134 + 5);
      const Vec4 v = vec_velocity_toward_xz(s->pos, o->pos, 0xE);
      s->vel[0] = v.x, s->vel[1] = v.y, s->vel[2] = v.z, s->extra34 = v.w;
      s->vel[1] = static_cast<s32>(0xFFFFE000);
    }
    s->p130 += g.frame_ticks * 4;
    bobj_move(s, s->vel);
    s->vel[1] += g.frame_ticks * 0x200;
    return;
  }
  s->p130 += g.frame_ticks * 4;
}

// 0x801f14dc (move 4): a beam between two corners (a frame quad stretched along +0x28);
// anim 8 shrinks (+0x128) and fades from its frame 4.
void cb_beam4(Shot* s) {
  BattleGlobals& g = bg();
  s->layer = 2;
  if (s->anim == 8) {
    if (g.flags == 0) {
      s->p128 -= g.frame_ticks * 0xA000;
      if (s->frame == 4) {
        s->colour_mode = 2;
        s->p12c += g.frame_ticks * 4;
        if (0x7F < s->p12c) {
          shot_kill(s);
          return;
        }
        s->rgb[0] = static_cast<u8>(0x80 - s->p12c);
      }
    }
  } else {
    s->loop_limit = 1;
  }
  const s32 c0[3] = {0, s->p128, 0};
  if (shot_draw_frame_quad(s, c0, s->vel, 5, 3) == -1) shot_kill(s);
}

// 0x801f1934 (move 5): a fireball the dragons breathe (element 4, or 2 from the blue
// head), 40 ticks.
void cb_flame(Shot* s) {
  BattleGlobals& g = bg();
  s->size[2] = 0x18;
  s->anim = 0xB;
  bobj_project(s);
  bobj_anim_draw(s, g.flags);
  if (shot_fade_out(s) != 0 || g.flags != 0) return;
  s->p130 += g.frame_ticks;
  if (s->p12c == 0 && 0x28 < s->p130) s->p12c = 1;
  if (s->hit_landed == 0) {
    WeaponElementBits el(s->owner, s->clut_offset == 3 ? 2 : 4);
    shot_hit_melee(s, 0x80, 0);
  }
  bobj_move(s, s->vel);
}

// 0x801f15dc (move 5): a dragon head (anims 3 / 4, or 5 / 6 without the charge); it
// roars (a flash sprite past tick 160), breathes sparks and every 16 ticks a fireball
// from its frame 9, and closes its mouth (anims 7 / 8) past tick 170; it goes when the
// owner leaves the move.
void cb_dragon(Shot* s) {
  BattleGlobals& g = bg();
  bobj_project(s);
  if (shot_fade_out(s) != 0 || s->owner->action != 0x15 || static_cast<s16>(bobj_anim_draw(s, g.flags)) == -1) {
    shot_kill(s);
    return;
  }
  if (s->p134 != 0) {
    sound_handle_fade_out(static_cast<u32>(s->p134), 0x14, 8);
    if (g.flags != 0) {
      sound_handle_duck(static_cast<u32>(s->p134), 8);
      return;
    }
  } else if (g.flags != 0) {
    return;
  }
  if (0xA0 < s->p128 && s->p130 == 0) {
    s->p130 = 1;
    if (s->anim < 5) {
      if (Sprite* sp = shot_spawn_sprite_at(s, 10, 1)) {
        sprite_place_near(sp, s, s->anim == 3 ? 0xAC : 0, 0xC0, 0);
        sp->clut_offset = s->anim == 3 ? 3 : 2;
      }
    }
  }
  if (0xAA < s->p128 && s->p130 == 1) {
    bobj_anim_reset(s);
    s->loop_limit = 1;
    s->p130 = 2;
    s->anim = static_cast<u16>(8 - (s->anim & 1));
  }
  s->p128 += g.frame_ticks;
  if (s->frame <= 8 || 5 <= s->anim) return;
  if (s->p134 == 0) {
    s->p134 = static_cast<s32>(bsfx_play_at(0x2D, s));
    sound_handle_fade_out(static_cast<u32>(s->p134), 0x14, 8);
  }
  const s32 dx = s->anim == 3 ? 0x90 : -0x10;
  const u8 clut = s->anim == 3 ? 3 : 2;
  const int anim_no = rng_range(2) != 0 ? 0xB : 0xD;
  if (Sprite* sp = shot_spawn_sprite(s, anim_no)) {
    sprite_place_near(sp, s, dx, 0xB8, 0);
    sp->vel[0] = bobj_facing_fx(s, 0x10);
    sp->vel[1] = rng_between_fixed(0, 4);
    sp->clut_offset = clut;
    sprite_set_fade(sp, 0x80, -2, 2);
  }
  s->aux = static_cast<s16>(s->aux - g.frame_ticks);
  if (s->aux >= 1) return;
  s->aux = static_cast<s16>(s->aux + 0x10);
  if (Shot* f = shot_spawn_callback(cb_flame)) {
    shot_place_at_unit(f, s, dx, 0xB8, 0);
    bobj_share_sprite(s, f);
    f->vel[0] = bobj_facing_fx(s, 0x10);
    f->vel[1] = rng_between_fixed(0, 4);
    f->owner = s->owner;
    f->clut_offset = clut;
    f->power = s->power;
  }
}

// 0x801f1a3c (move 6): the wave; its first hit (sweep knock-back, power 0x80 + 0x80 a
// level) makes it grow and fade; 40 ticks.
void cb_wave(Shot* s) {
  BattleGlobals& g = bg();
  s->size[2] = 0x30;
  s->layer = 2;
  shot_draw_and_box(s, 3, 0xFFFFFFFF);
  if (g.flags != 0) return;
  if (s->p12c != 0 || s->hit_landed != 0) {
    s->scale_x += g.frame_ticks * 0x80;
    s->scale_y += g.frame_ticks * 0x80;
  }
  if (s->p12c == 0 && 0x28 < s->p130) s->p12c = 1;
  s->p130 += g.frame_ticks;
  if (shot_fade_out(s) != 0) return;
  if (s->hit_landed == 0) {
    Unit* found[8] = {};
    Vec4 pts[8] = {};
    const int n = collider_attack_overlaps(s, found, pts, 0);
    if (n != 0) {
      s->hit_landed = 1;
      unit_attack_targets(s->owner, n, found, pts, s->power * 0x80 + 0x80, sweep_angle(s), 0);
      s->owner->attack_recovery = 0;
      if (s->p12c == 0) s->p12c = 1;
    }
  }
  bobj_move(s, s->vel);
}

// 0x801f1bc0 (move 8): the beam (anims 1 / 2 near, played once; 3 far, flying with an
// afterimage from level 1), one hit (power 0x100 + 0x80 a level, flags 2).
void cb_beam8(Shot* s) {
  BattleGlobals& g = bg();
  s->size[2] = 0x30;
  s->layer = 3;
  s->loop_limit = s->anim != 3 ? 1 : 0;
  if (static_cast<s16>(shot_draw_and_box(s, 3, 0xFFFFFFFF)) == -1) {
    shot_kill(s);
    shot_image_end(s);
    return;
  }
  if (g.flags != 0) return;
  s->p130 += g.frame_ticks;
  if (s->anim == 3) {
    if (s->p12c == 0 && 0x28 < s->p130) s->p12c = 1;
    if (shot_image(s) == nullptr && s->power != 0) shot_image_set(s, afterimage_create(s, s->power * 2 + 2, kTrailRgb, 1));
  }
  if (Afterimage* img = shot_image(s)) img->next_kind = 1;
  if (shot_fade_out(s) != 0) {
    shot_image_end(s);
    return;
  }
  if (s->hit_landed == 0) {
    Unit* found[8] = {};
    Vec4 pts[8] = {};
    const int n = collider_attack_overlaps(s, found, pts, 0);
    if (n != 0) {
      s->hit_landed = 1;
      unit_attack_targets(s->owner, n, found, pts, s->power * 0x80 + 0x100, 9999, 2);
      s->owner->attack_recovery = 0;
    }
  }
  bobj_move(s, s->vel);
}

// 0x801f1d94 (move 9): the breath glow at his mouth, until the move ends or it is told
// to go (+0x12c).
void cb_breath(Shot* s) {
  BattleGlobals& g = bg();
  s->anim = 1;
  bobj_project(s);
  bobj_anim_draw(s, g.flags);
  if (s->owner->action != 0x19 || s->p12c != 0) shot_kill(s);
}

// ---- The unit ---------------------------------------------------------------------------

struct AshtonUnit : PartyUnit {
  s32 mark[3] = {};          // +0x360: move 4's target position
  Shot* sparks[5] = {};      // +0x370: move 7's sparks on the target
  Shot* breath = nullptr;    // +0x384: move 9's glow
  s16 shaken = 0;            // +0x21e (module use): the target shook during move 4

  AshtonUnit() {  // 0x801ee05c
    v2d8 = 1;
    v2da = 1;
    clut_row_check = 1;
  }
  void interrupt() override;  // [4] 0x801ee2e8
  s32 jump_time(s32 v, const s32 dest[3]) override {  // [6] 0x801ee2b0
    const s32 t = Unit::jump_time(v, dest) - 0x68000;
    return t < 1 ? 1 : t;
  }
  void state_victory() override {  // [33] 0x801ee220: anim 8, or 0x14 at 1 in 128
    if (counters[0] == 0) {
      anim = 0xFF < rng_next15() ? 8 : 0x14;
      counters[0] = 1;
    }
    model_default();
    animate(0);
  }
  int killer_move_cost(int which) override {  // [35] 0x801ee0cc
    int total = 0;
    for (int c = 0; c < 2; ++c) {
      const u8 move = record[0xCC + c * 2 + which];
      if (move != 0) total += unit_mp_cost(this, *battle_data(0x8006B2BF + move));
    }
    return total;
  }
  int move_mp_cost(int index) override {  // [36] 0x801ee164
    const u8 move = record[0xCC + (index >> 1) * 2 + (index & 1)];
    return unit_mp_cost(this, move == 0 ? 0 : *battle_data(0x8006B2BF + move));
  }
  void killer_move_state(int i) override {  // [37] 0x801ee028 (table 0x801f1fd0)
    switch (i) {
      case 0: move0(); break;
      case 1: move1(); break;
      case 2: move2(); break;
      case 3: move3(); break;
      case 4: move4(); break;
      case 5: move5(); break;
      case 6: move6(); break;
      case 7: move7(); break;
      case 8: move8(); break;
      case 9: move9(); break;
      default: break;
    }
  }

  void move0();  // 0x801ee424
  void move1();  // 0x801ee6d4
  void move2();  // 0x801eea60
  void move3();  // 0x801eecf8
  void move4();  // 0x801eef60
  void move5();  // 0x801efc1c
  void move6();  // 0x801efef8
  void move7();  // 0x801f012c
  void move8();  // 0x801f09b4
  void move9();  // 0x801f0d68

  // 0x801ee1c4: once per move (counters[1]), at frame f while the battle is on.
  bool once_at(int f) {
    BattleGlobals& g = bg();
    if (g.flags != 0 || counters[1] != 0 || frame < f || g.state != kStateFighting) return false;
    counters[1] = 1;
    return true;
  }
  // 0x801ee3b4: an afterimage of n copies.
  void trail_on(int n) {
    if (effects[0] == nullptr && n != 0) effects[0] = afterimage_create(this, n, kTrailRgb, 1);
  }
  // 0x801efe64: when the battle is ending, the move is cut (slot 4 first).
  bool battle_ending() {
    BattleGlobals& g = bg();
    if (g.state < 3 || 0x13 < g.end_countdown) return false;
    interrupt();
    action_kind = 0x12;
    begin_action();
    return true;
  }
  void end_move() {
    action_kind = 0x12;
    begin_action();
  }
  void step(s32* v) {
    if (unit_try_move_scaled(this, v, 7) == nullptr) bobj_move(this, v);
  }
  void face_target() {
    if (target != nullptr) facing[0] = pos[0] < target->pos[0] ? 0x40000 : static_cast<s32>(0xFFFC0000);
  }
};

// [4] 0x801ee2e8: move 7's sparks on the target go (kind 0x11 ones drawn with his sprite
// set); move 7 (state 0x17) loses its fade, move 9 (0x19) cancels its spell data.
void AshtonUnit::interrupt() {
  for (Shot*& s : sparks) {
    if (s != nullptr && s->kind == 0x11 && s->animset != nullptr && animset != nullptr &&
        s->animset->layout == animset->layout) {
      shot_kill(s);
    }
    s = nullptr;
  }
  if (action == 0x17) colour_mode = 0;
  if (action == 0x19) spell_data_cancel_owner(this);
}

// Move 0: a swing (anim lvl); each unit hit gets a sprite by its hit's height, stronger
// (power 0x100 and a zoom kick) from frame 0x801f1fc4[lvl] on.
void AshtonUnit::move0() {
  BattleGlobals& g = bg();
  const int lvl = pc_move_charge_level(this, killer_index);
  loop_limit = 1;
  anim = static_cast<u16>(lvl);
  trail_on(lvl);
  if (pc_attack_anim_step(this) != 0 || g.flags != 0) return;
  camera_follow_start();
  Unit* found[8] = {};
  Vec4 pts[8] = {};
  const int n = collider_attack_overlaps(this, found, pts, 0);
  if (n != 0 && hit_landed == 0) {
    const s32 angle = facing[0] < 0 ? 0xB8E : static_cast<s32>(0xFFFFFC72);
    s16 kb;
    std::memcpy(&kb, stats + 0x52, 2);
    const s16 lowered = static_cast<s16>(kb - 4);
    std::memcpy(stats + 0x52, &lowered, 2);
    const int strong_at = static_cast<u8>(module_s8(0x801F1FC4 + static_cast<u32>(lvl)));
    const int r = unit_attack_targets(this, n, found, pts, strong_at <= frame ? 0x100 : 0x80, angle, 0);
    if (r != 0 && effects[0] != nullptr) effects[0]->next_kind = 1;
    for (int i = 0; i < n; ++i) {
      // ORIGINAL BUG (original_bugs.md F17): every unit's hit sprite is picked by the first
      // hit point's height (scratch 0x1f8002a0), not its own:
      //   int k = pts[0].y < -0x60000 ? 3 : 1;
      //   if (pts[0].y < -0x90000) k = 2;
      int k = pts[i].y < -0x60000 ? 3 : 1;
      if (pts[i].y < -0x90000) k = 2;
      if (Sprite* sp = unit_play_anim_ex(this, r != 0 ? k + 3 : k + 6, 1)) {
        if (strong_at <= frame && r != 0) {
          if (bobj_on_screen(this)) {
            g.zoom_kick = 0x157C;
            g.zoom_kick_decay = 100;
          }
          bsfx_play_at(0x48, this);
          bsfx_play_at(0x49, this);
        }
        sprite_set_pos_v(sp, &pts[i].x);
        sp->layer = 2;
      }
    }
    std::memcpy(stats + 0x52, &kb, 2);
    if (bobj_on_screen(this)) g.hitstop_frames = 4;
  }
  if ((box_mask & 0xC) == 0) hit_landed = 0;
}

// Move 1: a running thrust (near: a burst over frames 8-10; far: a run over 3-9 and a
// burst over 14-20) with a ribbon trail from level 1. A hit before frame 8 (13 far) sticks
// a sprite on the unit; a hit after that, if the first part hit, is a burst of light
// (lvl * 16 + 16 homing lines).
void AshtonUnit::move1() {
  BattleGlobals& g = bg();
  loop_limit = 1;
  vel[0] = 0;
  const int lvl = pc_move_charge_level(this, killer_index);
  camera_follow_start();
  anim = static_cast<u16>(target_far);
  if (effect_trail == nullptr && lvl != 0) {
    effect_trail = ribbon_trail_create(this, 0x18, 0x308000, 0x80C020);
    if (effect_trail != nullptr) effect_trail->back = &effect_trail;
  }
  if (g.flags == 0) {
    Unit* found[8] = {};
    Vec4 pts[8] = {};
    const int n = collider_attack_overlaps(this, found, pts, 0);
    if (n != 0 && hit_landed == 0) {
      const int r = unit_attack_targets(this, n, found, pts, lvl * 0x80 + 0x100, 9999, 0);
      if (frame < 8 || (frame < 13 && target_far != 0)) {
        if (r != 0) {
          counters[2] = 1;
          if (Sprite* sp = unit_play_anim(this, 4)) {
            sprite_attach(sp, found[0], 0, static_cast<s32>(0xFFFB0000), 0);
            sprite_set_fade(sp, 0x80, -4, 0xC);
            sp->facing[0] = facing[0];
          }
        }
      } else if (counters[2] != 0) {
        bsfx_play_at(0x29, this);
        if (Sprite* sp = unit_play_anim(this, 3)) {
          sprite_set_pos_v(sp, &pts[0].x);
          sprite_set_fade(sp, 0x80, -2, 10);
          const s32 sc = lvl * 0x1000 + 0x1000;
          sp->scale_y = sc;
          sp->scale_x = sc;
          sp->scale_vel[1] = 0x80;
          sp->scale_vel[0] = 0x80;
          sp->roll_speed = 0x2D;
          sp->layer = 8;
        }
        const s32 at[4] = {pts[0].x, pts[0].y, pts[0].z, pts[0].w};
        for (int i = 0; i < lvl * 0x10 + 0x10; ++i) {
          const Vec4 v = vec_random_around(at, 1000);
          const s32 to[4] = {v.x, v.y, v.z, v.w};
          particle_spawn_homing_line(at, to, 0x801020, 0xFF4080, 0x78, 0x18000, -0x100);
        }
      }
      if (bobj_on_screen(this)) g.hitstop_frames = 4;
    }
    bool dash;
    const u32 f = static_cast<u8>(frame);
    if (target_far == 0) {
      dash = f - 8 < 3;
    } else {
      if (f - 3 < 7) vel[0] = bobj_facing_fx(this, 0xC);
      dash = f - 0xE < 7;
    }
    if (dash) vel[0] = bobj_facing_fx(this, 0x14);
    if (hit_landed != 0 && (box_mask & 0xC) == 0) hit_landed = 0;
    step(vel);
  }
  bobj_texcache_reset(this);
  pc_attack_anim_step(this);
}

// Move 2: a two-part slash (far: with the dragons, a flash on his back at frames 4 and 10);
// each hit leaves a slash sprite on the unit (larger on the second part).
void AshtonUnit::move2() {
  BattleGlobals& g = bg();
  loop_limit = 1;
  const int lvl = pc_move_charge_level(this, killer_index);
  camera_follow_start();
  anim = static_cast<u16>(target_far);
  trail_on(lvl);
  if (g.flags == 0) {
    if (target_far != 0) {
      bool fire = false;
      if (counters[2] == 0) {
        fire = 4 <= frame;
      } else if (counters[2] == 1) {
        fire = 10 <= frame;
      }
      if (fire && weapon_variant != 0) {
        ++counters[2];
        if (Sprite* sp = unit_play_anim_ex(this, 5, 1)) {
          sprite_place_near(sp, this, 0, 0x54, 0);
          sp->layer = 2;
        }
      }
    }
    Unit* found[8] = {};
    Vec4 pts[8] = {};
    const int n = collider_attack_overlaps(this, found, pts, 0);
    if (n != 0 && hit_landed == 0) {
      if (unit_attack_targets(this, n, found, pts, lvl * 0x80 + 0x100, 9999, 0) != 0) {
        if (Sprite* sp = unit_play_anim_ex(this, 6 < frame ? 4 : 3, 1)) {
          const Unit* f = found[0];
          sp->layer = 2;
          const s32 rise = frame < 7 ? 0x58000 : 0x40000;
          sprite_attach(sp, found[0], pts[0].x - f->pos[0], pos[1] - (f->pos[1] + rise), pts[0].z - f->pos[2]);
          sp->facing[0] = facing[0];
          if (6 < frame) {
            const s32 sc = lvl * 0x400 + 0xC00;
            sp->scale_y = sc;
            sp->scale_x = sc;
          }
        }
      }
    }
    if (hit_landed != 0 && (box_mask & 0xC) == 0) hit_landed = 0;
  }
  pc_attack_anim_step(this);
}

// Move 3: without the dragons a ball at frame 3; with them, at frame lvl + 11, lvl + 3
// darts (2 at level 0) in pairs at the heights 0x801f1e2c.
void AshtonUnit::move3() {
  BattleGlobals& g = bg();
  face_target();
  const int lvl = pc_move_charge_level(this, killer_index);
  loop_limit = 1;
  if (weapon_variant == 0) {
    anim = 0;
    if (g.flags == 0 && once_at(3)) {
      bsfx_play_at(0x1F, this);
      ShotDesc* d = shot_desc_init(1);
      d->speed = 0x20;
      d->life = 0x28;
      d->flags = 6;
      d->target = target;
      const s32 from[3] = {bobj_front_x(this, 0x80), pos[1] - 0x80000, pos[2]};
      shot_spawn_projectile(this, from, d, this);
    }
  } else {
    anim = static_cast<u16>(lvl);
    if (g.flags == 0 && once_at(lvl + 0xB)) {
      const s8 heights[3] = {module_s8(0x801F1E2C), module_s8(0x801F1E2D), module_s8(0x801F1E2E)};
      const int count = lvl != 0 ? lvl + 3 : 2;
      Shot* last = nullptr;
      for (int i = 0; i < count; ++i) {
        Shot* s = shot_spawn_callback(cb_dart);
        last = s;
        if (s == nullptr) continue;
        s->pos[0] = pos[0] + facing[0];
        s->pos[1] = pos[1] + heights[i >> 1] * 0x1000 - 0x60000;
        s->pos[2] = pos[2] + static_cast<s32>(((i & 1) != 0 ? 0x20000u : 0xFFFD0000u) | 0x8000u);
        s->vel[0] = bobj_facing_fx(this, 0x28);
        s->anim = static_cast<u16>((i >> 1) + 7);
        s->facing[0] = vel[0];  // (+0x28: his x velocity, not his facing; original_bugs F15)
        s->p128 = facing[0];    // (port: the facing the dart is drawn by, F15)
        pc_bind_shot(this, s, static_cast<s16>(lvl));
        pc_camera_frame_target(this, 0x18);
      }
      // ORIGINAL BUG (original_bugs.md F16): the sound plays at the last dart even when it
      // could not be spawned (a null Shot):
      //   bsfx_play_at(0x1F, last);
      if (last != nullptr) bsfx_play_at(0x1F, last);
    }
  }
  pc_attack_anim_step(this);
}

// Move 4: a leap (back near, forward far); he lands at the first corner of a triangle
// round the target's position (moved in depth until the spot is free), two clones at the
// others; from frame 19 the beams between the corners, which strike the target at tick 60
// of state 4 (power 0x300 within 200 of the centre); a glow (gouraud quads along the
// edges) while it runs.
void AshtonUnit::move4() {
  BattleGlobals& g = bg();
  if (unit_is_down(target) && counters[1] <= 4) {
    end_move();
    return;
  }
  const StageData& st = stage();
  s32 cx[3], cy[3] = {}, cz[3];
  cx[0] = mark[0] + 0x60000;
  cz[0] = mark[2] - 0x12C000;
  if (st.bounds[2] - 0x40000 < cx[0]) cx[0] = st.bounds[2] - 0x40000;
  for (;;) {
    const s32 d[3] = {cx[0] - pos[0], 0, cz[0] - pos[2]};
    if (unit_check_move(this, d, 7) == nullptr) break;
    cz[0] += 0x64000;
    if (st.bounds[3] < cz[0]) {
      jumping = 0;
      end_move();
      return;
    }
  }
  cx[1] = mark[0] - 0x12C000;
  cz[1] = mark[2] + 0x12C000;
  cx[2] = mark[0] + 0x168000;
  cz[2] = cz[1];
  s32 sx = 0, sy = 0, sz = 0;
  for (int i = 0; i < 3; ++i) {
    cy[i] = stage_ground_height(cx[i] >> 12, cz[i] >> 12);
    sx += cx[i], sy += cy[i], sz += cz[i];
  }
  const s32 centre[3] = {sx / 3, sy / 3, sz / 3};
  switch (counters[1]) {
    case 0:
      anim = static_cast<u16>(target_far);
      if (0 < frame) {
        face_target();
        vel[0] = bobj_facing_fx(this, target_far == 0 ? -4 : 8);
        vel[1] = static_cast<s32>(0xFFFFE000);
        airborne = 1;
        jumping = 1;
        vel[2] = 0;
        counters[1] = 1;
        bsfx_play_at(0x26, this);
        shaken = 0;
      }
      animate(0);
      break;
    case 1:
      if (g.flags == 0) {
        camera_follow_start();
        pc_jump_step(this, 0x80);
      }
      counters[0] = 0;
      if (static_cast<u16>(animate(0)) != 0xFFFF) {
        if (target != nullptr) mark[0] = target->pos[0], mark[1] = target->pos[1], mark[2] = target->pos[2];
        break;
      }
      airborne = 0;
      jumping = 0;
      counters[1] = 2;
      bobj_anim_reset(this);
      pos[0] = cx[0];
      pos[2] = cz[0];
      facing[0] = static_cast<s32>(0xFFFC0000), facing[1] = 0, facing[2] = 0;
      anim = 3;
      pos[1] = cy[0];
      bsfx_play_at(0x1E, this);
      for (int i = 1; i < 3; ++i) {
        if (Shot* s = shot_spawn_callback(cb_clone)) {
          s->pos[0] = cx[i], s->pos[1] = cy[i], s->pos[2] = cz[i];
          s->facing[0] = i == 1 ? 0x40000 : static_cast<s32>(0xFFFC0000);
          s->anim = 2;
          s->p134 = i - 1;
          pc_bind_shot(this, s, 0);
        }
      }
      [[fallthrough]];
    case 2:
      animate(0);
      if (g.flags == 0 && 0x12 < frame) {
        if (counters[0] == 0) {
          pc_voice_attack_shout(this);
          bsfx_play_at(0x4A, this);
          sound_handle_pitch_down(static_cast<u32>(bsfx_play_at(0x34, this)), 0x3C);
          counters[0] = 1;
          for (int i = 0; i < 3; ++i) {
            const int next = i + 1 > 2 ? 0 : i + 1;
            const int prev = i - 1 < 0 ? 2 : i - 1;
            for (int k = 0; k < 2; ++k) {
              Shot* s = shot_spawn_callback(cb_beam4);
              if (s == nullptr) continue;
              s->pos[0] = cx[i], s->pos[1] = cy[i], s->pos[2] = cz[i];
              const int t = k != 0 ? prev : next;
              s->vel[0] = cx[t] - cx[i];
              s->vel[1] = cy[t] - cy[i];
              s->p128 = static_cast<s32>(0xFFF6A000);
              s->anim = 4;
              s->vel[2] = cz[t] - cz[i];
              bobj_share_sprite(this, s);
            }
          }
        }
        counters[2] += g.frame_ticks * 8;
        if (0x100 < counters[2]) {
          counters[2] = 0x100;
          counters[1] = 3;
        }
      }
      break;
    case 3: {
      animate(0);
      for (int i = 0; i < 3; ++i) {
        const int next = i + 1 > 2 ? 0 : i + 1;
        if (Shot* s = shot_spawn_callback(cb_beam4)) {
          s->pos[0] = cx[i], s->pos[1] = cy[i], s->pos[2] = cz[i];
          s->vel[0] = cx[next] - cx[i];
          s->vel[1] = cy[next] - cy[i];
          s->p128 = static_cast<s32>(0xFFE70000);
          s->anim = 8;
          s->vel[2] = cz[next] - cz[i];
          bobj_share_sprite(this, s);
        }
      }
      s32 growth = 0x20, ry = centre[1];
      for (int i = 0; i < 8; ++i, growth -= 2, ry -= 0x1E000) {
        const s32 at[4] = {centre[0], ry, centre[2], 0};
        particle_spawn_ring(at, 0xC, 0x60, growth, 0x3C, 0x102040, 0x3060C0, 100, 0);
      }
      counters[3] = static_cast<s32>(bsfx_play_at(0x2D, this));
      counters[1] = 4;
      counters[0] = 0;
      break;
    }
    case 4:
      sound_handle_fade_out(static_cast<u32>(counters[3]), 0x3C, 10);
      if (g.flags != 0) sound_handle_duck(static_cast<u32>(counters[3]), 6);
      bobj_texcache_reset(this);
      animate(0);
      if (g.flags == 0) {
        for (int i = 0; i < 2; ++i) {
          if (Sprite* sp = sprite_spawn_b(8, 1, nullptr)) {
            const Vec4 p = vec_random_in_disc_xz(centre, 100);
            sprite_set_pos_v(sp, &p.x);
            const s32 sc = rng_between(0x1000, 0x2000);
            sp->scale_y = sc;
            sp->scale_x = sc;
            sp->layer = 2;
            sprite_set_fade(sp, 0x80, -4, 8);
            sp->accel[1] = static_cast<s32>(0xFFFFF800);
            sp->clut_offset = 5;
          }
        }
        if (g.flags == 0) counters[0] += g.frame_ticks;
      }
      if (0x3C < counters[0]) {
        counters[1] = 5;
        if (target != nullptr && vec_distance_fx(target->pos, centre) < 0xC8000) {
          Unit* one[1] = {target};
          Vec4 pt{target->pos[0], target->pos[1] - target->size[1] * 0x2000, target->pos[2], 0};
          unit_attack_targets(this, 1, one, &pt, 0x300, 9999, 0);
          bsfx_play_at(0x48, target);
          bsfx_play_at(0x4C, target);
        }
      }
      break;
    case 5:
      bobj_texcache_reset(this);
      animate(0);
      if (g.flags == 0) counters[2] -= g.frame_ticks * 4;
      if (counters[2] <= 0) {
        end_move();
        return;
      }
      break;
    default:
      break;
  }
  if (counters[1] < 2) return;
  if (target != nullptr && counters[1] < 5 && static_cast<const EnemyUnit*>(target)->boss == 0) {
    if (target->shake_frames != 0) shaken = 1;
    if (shaken == 0) target->super_armour = 2;
  }
  // The glow along the edges: 4 gouraud quads per edge between the corners and the corners
  // moved by 0x801f1fc8 / 0x801f1fcc (x / z).
  gte_set_view_matrix();
  u32 colours[4];
  colours[0] = scale_colour(0x805000, counters[2]) | 0x3A000000;
  colours[1] = colours[0];
  colours[2] = scale_colour(0xC0A040, counters[2]);
  colours[3] = colours[2];
  for (int i = 0; i < 3; ++i) {
    const int j = i + 1 > 2 ? 0 : i + 1;
    s16 p[4], q[4], pt[4], qt[4];
    p[0] = static_cast<s16>(cx[i] >> 12), p[1] = static_cast<s16>(cy[i] >> 12), p[2] = static_cast<s16>(cz[i] >> 12), p[3] = 0;
    pt[0] = static_cast<s16>(p[0] + module_s8(0x801F1FC8 + static_cast<u32>(i))), pt[1] = p[1];
    pt[2] = static_cast<s16>(p[2] + module_s8(0x801F1FCC + static_cast<u32>(i))), pt[3] = 0;
    q[0] = static_cast<s16>(cx[j] >> 12), q[1] = static_cast<s16>(cy[j] >> 12), q[2] = static_cast<s16>(cz[j] >> 12), q[3] = 0;
    qt[0] = static_cast<s16>(q[0] + module_s8(0x801F1FC8 + static_cast<u32>(j))), qt[1] = q[1];
    qt[2] = static_cast<s16>(q[2] + module_s8(0x801F1FCC + static_cast<u32>(j))), qt[3] = 0;
    for (int seg = 0; seg < 4; ++seg) {
      s16 quad[4][4];
      for (int e = 0; e < 2; ++e) {
        const s32 f = ((seg + e) * 0x1000) >> 2;
        average12(p, q, f, 0x1000 - f, quad[e * 2]);
        average12(pt, qt, f, 0x1000 - f, quad[e * 2 + 1]);
      }
      draw_quad_g4(quad, colours, 0);
    }
  }
}

// Move 5: after the wind-up the two dragon heads (anims 3 / 4 at a level, else one of
// them picked at random) breathe for 243 ticks; the camera looks ahead.
void AshtonUnit::move5() {
  BattleGlobals& g = bg();
  const int lvl = pc_move_charge_level(this, killer_index);
  loop_limit = 0;
  if (g.flags == 0) camera_follow_start();
  if (battle_ending()) return;
  switch (counters[1]) {
    case 0: {
      anim = 0;
      loop_limit = 1;
      if (static_cast<u16>(animate(0)) != 0xFFFF) return;
      bobj_anim_reset(this);
      counters[1] = 1;
      const int pick = static_cast<int>(rng_range(2));
      for (int i = 0; i < 2; ++i) {
        Shot* s = shot_spawn_callback(cb_dragon);
        if (s == nullptr) continue;
        shot_place_at_unit(s, this, 0, 0, 0);
        pc_bind_shot(this, s, static_cast<s16>(lvl));
        s->layer = static_cast<s8>(i == 0 ? -4 : -3);
        s->anim = static_cast<u16>(lvl != 0 || i == pick ? i + 3 : i + 5);
      }
      if (camera_focus != 0) {
        const s32 ofs[3] = {bobj_facing_fx(this, 300), 0, 0};
        camera_set_lookat_offset(g.camera, ofs);
      }
      pc_camera_frame_target_for(this, 0x104);
      [[fallthrough]];
    }
    case 1:
      loop_limit = 0;
      anim = 2;
      if (g.flags == 0) counters[2] += g.frame_ticks;
      pc_attack_anim_step(this);
      if (counters[2] < 0xF3) return;
      bobj_anim_reset(this);
      counters[1] = 2;
      [[fallthrough]];
    case 2:
      anim = 1;
      loop_limit = 1;
      pc_attack_anim_step(this);
      return;
    default:
      return;
  }
}

// Move 6: a slash (a run over frames 3-9 far) throwing a wave at frame 6 (14 far).
void AshtonUnit::move6() {
  BattleGlobals& g = bg();
  loop_limit = 1;
  vel[0] = 0;
  const int lvl = pc_move_charge_level(this, killer_index);
  if (battle_ending()) return;
  camera_follow_start();
  anim = static_cast<u16>(target_far);
  trail_on(lvl);
  if (g.flags == 0) {
    Unit* found[8] = {};
    Vec4 pts[8] = {};
    const int n = collider_attack_overlaps(this, found, pts, 0);
    if (n != 0 && hit_landed == 0) unit_attack_targets(this, n, found, pts, lvl * 0x80 + 0x100, 9999, 0);
    const s8 at_frame = target_far != 0 ? 0xE : 6;
    if (at_frame <= frame && counters[3] == 0 && g.state == kStateFighting) {
      counters[3] = 1;
      if (Shot* s = shot_spawn_callback(cb_wave)) {
        bsfx_play_at(0x21, s);
        shot_place_at_unit(s, this, 0x80, 0, 0);
        s->anim = static_cast<u16>(lvl + 2);
        s->vel[0] = bobj_facing_fx(this, 0x10);
        pc_bind_shot(this, s, static_cast<s16>(lvl));
      }
    }
    if (target_far != 0 && static_cast<u32>(static_cast<u8>(frame)) - 3 < 7) vel[0] = bobj_facing_fx(this, 0xC);
    if (hit_landed != 0 && (box_mask & 0xC) == 0) hit_landed = 0;
    step(vel);
  }
  bobj_texcache_reset(this);
  pc_attack_anim_step(this);
}

// Move 7: a dash (sparks swirl on the target); at its end he appears by the target
// (unit_find_approach_point) in a burst of light and strikes, fading out and appearing
// again lvl + 2 times; then a step back.
void AshtonUnit::move7() {
  BattleGlobals& g = bg();
  const int lvl = pc_move_charge_level(this, killer_index);
  if (3 <= g.state && g.end_countdown <= 0x17 && counters[1] <= 2) {
    end_move();
    return;
  }
  bool run_two = false;  // state 2's body (also right after state 1 ends)
  switch (counters[1]) {
    case 0: {
      if (g.flags == 0) {
        if (effects[0] == nullptr && lvl != 0) trail_on(2);
        camera_follow_start();
        step(vel);
        if (1 < frame && counters[2] == 0 && g.state == kStateFighting) {
          counters[2] = 1;
          if (Sprite* sp = unit_play_anim(this, 7)) {
            sprite_set_pos_v(sp, pos);
            sprite_set_fade(sp, 0x80, -2, 0x10);
            sp->layer = 2;
          }
          for (int i = 0; i < 4; ++i) {
            Shot* r = effect_spawn_sparks_on(target, 0x10, 4, 0x50 + i * 5, -3, lvl * 0x3C + 0xF0, 0, 2, 0x4B0 - i * 200, 1);
            sparks[i] = r;
            if (r != nullptr) {
              r->anim = 7;
              bobj_share_sprite(this, r);
              r->vel[0] = rng_spread(0x22);
            }
          }
        }
      }
      anim = static_cast<u16>(target_far);
      animate(0);
      if (frame <= 7) break;
      vel[0] = bobj_facing_fx(this, 0xC);
      counters[1] = 1;
      counters[2] = 0;
      bsfx_play_at(0x1D, this);
      [[fallthrough]];
    }
    case 1: {
      if (g.flags == 0) step(vel);
      counters[0] = 0;
      if (static_cast<u16>(animate(0)) != 0xFFFF) break;
      counters[1] = 2;
      reach = 0x10;
      counters[2] = 1;
      Vec4 p{};
      if (!unit_find_approach_point(this, &p)) {
        end_move();
        return;
      }
      pos[0] = p.x, pos[1] = p.y, pos[2] = p.z;
      bobj_anim_reset(this);
      run_two = true;
      break;
    }
    case 2:
      run_two = true;
      break;
    case 3:
      anim = 5;
      loop_limit = 1;
      pc_attack_anim_step(this);
      break;
    default:
      break;
  }
  if (run_two) {
    if (g.flags == 0) {
      if (counters[2] != 0) {
        counters[2] -= g.frame_ticks;
        if (counters[2] < 1) {
          counters[2] = 0;
          for (int i = 0; i < 0x20; ++i) {
            const u32 a = rng_range(0x1000);
            const u32 b = static_cast<u32>(rng_spread(0x400));
            const s32 ca = bcos(a);
            s32 pt[2][4] = {};
            s32 r = 600;
            for (int k = 0; k < 2; ++k, r += 600) {
              pt[k][0] = ((ca * bcos(b)) >> 12) * r + pos[0];
              pt[k][1] = pos[1] - (bsin(a) * r + 0x50000);
              pt[k][2] = ((ca * bsin(b)) >> 12) * r + pos[2];
            }
            particle_spawn_homing_line(pt[1], pt[0], 0x201000, 0xE0C040, 0x2D0, 0x1C000, 0);
          }
          if (bobj_on_screen(this)) {
            ViewState& v = view();
            v.stage_colour[0] = v.stage_colour[1] = v.stage_colour[2] = 0x180;
            g.flash_active = 1;
            g.flash_delay = 2;
            g.flash_speed = 0x20;
            g.zoom_kick = 5000;
            g.zoom_kick_decay = 200;
          }
        }
      }
      Unit* found[8] = {};
      Vec4 pts[8] = {};
      const int n = collider_attack_overlaps(this, found, pts, 0);
      if (n != 0 && hit_landed == 0 && unit_attack_targets(this, n, found, pts, 0x100, 9999, 0) != 0 &&
          g.state == kStateFighting) {
        for (int i = 0; i < 0xC; ++i) {
          if (Sprite* sp = unit_play_anim_ex(this, static_cast<int>(rng_range(4)) + 9, 0x1E)) {
            const s32 dy = rng_between_fixed(-0xE0, 0x60);
            sprite_set_pos(sp, pts[0].x, pts[0].y + dy, pts[0].z);
            const s32 yaw = static_cast<s32>(rng_range(0x1000));
            const s32 spin = rng_spread(0x44);
            sprite_set_orbit(sp, yaw, 0, 0x20, 0, 0, spin, 0, 4);
          }
        }
      }
    }
    if (counters[2] == 0) {
      if (counters[3] == 0) {
        colour_mode = 0;
      } else {
        colour_mode = 2;
        rgb[0] = static_cast<u8>(0x80 - counters[3]);
      }
      animate(0);
      anim = static_cast<u16>(counters[0] + 2);
      if (0 < frame) {
        counters[3] += g.frame_ticks * 4;
        if (0x7F < counters[3]) {
          counters[2] = 10;
          ++counters[0];
          bobj_anim_reset(this);
          if (lvl + 1 < counters[0]) {
            counters[1] = 3;
            int run = 0;
            s32 d = 0x90;
            for (int i = 3; i < 0x10; ++i, d += 0x30) {
              vel[0] = bobj_facing_fx(this, d);
              const bool blocked = unit_try_move_scaled(this, vel, 7) != nullptr;
              ++run;
              if (blocked) {
                run = 0;
              } else if (1 < run) {
                bobj_move(this, vel);
                break;
              }
            }
            bsfx_play_at(0x28, this);
            colour_mode = 0;
          } else {
            Vec4 p{};
            if (!unit_find_approach_point(this, &p)) {
              colour_mode = 0;
              end_move();
              return;
            }
            counters[3] = 0;
          }
        }
      }
    }
  }
  if (g.flags == 0 && effects[0] != nullptr) effects[0]->next_kind = 1;
}

// Move 8: a charge (sparks drawn in over frames 7-11, a rising sound), then at frame 15
// the beam (near: anims 1 / 2 in place; far: anim 3 flying, the camera ahead) in a spray
// of sparkles and lines.
void AshtonUnit::move8() {
  BattleGlobals& g = bg();
  const int lvl = pc_move_charge_level(this, killer_index);
  anim = 0;
  if (pc_attack_anim_step(this) != 0 || g.flags != 0) return;
  pc_attack_hit(this, 0x100, 0);
  if (hit_landed != 0 && (box_mask & 0xC) == 0) hit_landed = 0;
  if (static_cast<u32>(static_cast<u8>(frame)) - 7 < 5) {
    if (counters[2] == 0) {
      counters[2] = static_cast<s32>(bsfx_play_at(0x45, this));
      sound_handle_pitch_up(static_cast<u32>(counters[2]), 0x78);
    }
    for (int i = 0; i < 2; ++i) {
      const s32 at[4] = {bobj_front_x(this, 0x90), pos[1] - 0x90000, pos[2], 0};
      const Vec4 v = vec_random_around(at, 300);
      const s32 from[4] = {v.x, v.y, v.z, v.w};
      particle_spawn_homing_line(from, at, 0x201000, 0xE07000, 0x1E, 0x2000, 0x400);
    }
    sound_handle_fade_out(static_cast<u32>(counters[2]), 0x1E, 8);
  }
  if (!once_at(0xF)) return;
  const s32 at[4] = {bobj_front_x(this, 0x40), pos[1] - 0x60000, pos[2], 0};
  bsfx_play_at(0x30, this);
  if (Shot* s = shot_spawn_callback(cb_beam8)) {
    shot_image_reset(s);
    pc_bind_shot(this, s, static_cast<s16>(lvl));
    if (target_far == 0) {
      shot_place_at_unit(s, this, 0x50, 0x60, 0);
      s->anim = lvl == 2 ? 2 : 1;
      if (lvl == 0) {
        s->scale_x = 0xC00;
        s->scale_y = 0xC00;
      }
    } else {
      if (camera_focus != 0) {
        const s32 ofs[3] = {bobj_facing_fx(this, 300), 0, 0};
        camera_set_lookat_offset(g.camera, ofs);
      }
      shot_place_at_unit(s, this, 0x40, 0x60, 0);
      s->anim = 3;
      s->vel[0] = bobj_facing_fx(this, 0x14);
      pc_camera_frame_target_for(this, 0x2D);
    }
  }
  for (int i = 0; i < lvl * 6 + 0xC; ++i) {
    if (Sprite* sp = unit_play_anim(this, 6)) {
      sprite_set_pos_v(sp, at);
      const Vec4 v = vec_random_around(kZero, 10);
      sp->vel[0] = v.x, sp->vel[1] = v.y, sp->vel[2] = v.z, sp->extra34 = v.w;
      sprite_set_fade(sp, 0x80, -4, 4);
      sp->layer = 2;
    }
  }
  for (int i = 0; i < lvl * 0xC + 0x18; ++i) {
    const Vec4 v = vec_random_around(at, 400);
    const s32 to[4] = {v.x, v.y, v.z, v.w};
    particle_spawn_homing_line(at, to, 0x201000, 0xE07000, 6, 0xC000, -0x80);
  }
}

// Move 9: at frame 8 (5 with the dragons) he asks for spell 0x40's data and a glow
// appears; 120 ticks later, once the data is in, the release animation; at its first loop
// spell 0x40 is cast. A spell running in slot 0 (kind 0x15) can end it.
void AshtonUnit::move9() {
  BattleGlobals& g = bg();
  const int lvl = pc_move_charge_level(this, killer_index);
  if (3 <= g.state && g.end_countdown <= 0x13) {
    interrupt();
    end_move();
    return;
  }
  if (const SpellCast* sc = spellcast_slot(0); sc != nullptr && sc->kind == 0x15) {
    if (counters[2] == 3 && 2 < sc->step) {
      end_move();
      return;
    }
    if ((g.flags & 0x40) != 0 && 1 < sc->step) {
      counters[2] = 3;
      return;
    }
  }
  loop_limit = 0;
  anim = counters[2] != 0 ? 2 : 0;
  animate(0);
  camera_follow_start();
  if (g.flags != 0) return;
  if (counters[2] == 1 && loops != 0) {
    counters[2] = 2;
    spell_cast(0x40, this, target, lvl);
    return;
  }
  if (once_at(8) || (weapon_variant != 0 && once_at(5))) {
    spell_data_request(0x40, 0x78, this);
    sound_handle_pitch_up(static_cast<u32>(bsfx_play_at(0x34, this)), 0x1E);
    sound_handle_pitch_down(static_cast<u32>(bsfx_play_at(0x21, this)), 0x28);
    breath = shot_spawn_callback(cb_breath);
    if (breath != nullptr) {
      shot_place_at_unit(breath, this, 0, 0, 0);
      pc_bind_shot(this, breath, static_cast<s16>(lvl));
    }
  }
  if (counters[1] == 0) return;
  counters[3] += g.frame_ticks;
  if (counters[3] <= 0x78 || counters[2] != 0 || spell_data_status(0x40, this) == 1) return;
  counters[2] = 1;
  if (breath != nullptr) breath->p12c = 1;
  bobj_anim_reset(this);
  bsfx_play_at(0x1E, this);
}

}  // namespace

std::unique_ptr<PartyUnit> pc_ashton_create() { return std::make_unique<AshtonUnit>(); }

}  // namespace ss::game::battle
