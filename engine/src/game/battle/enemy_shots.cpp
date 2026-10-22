// Resident Shot kinds and Shot helpers only the monsters use (#2129): kind 0x27 (a tossed
// item, the Guardboxes'), kind 0x2d (a slash wave, the Ghost's) and shot_draw_frame_quad
// (Dias's slash mark). shot.cpp's kind switch reaches the kinds through enemy_shot_update.

#include "game/battle/combat.h"
#include "game/battle/enemy_support.h"
#include "game/battle/shot.h"
#include "game/battle/sprite.h"
#include "game/battle/unit.h"

namespace ss::game::battle {
namespace {

// 0x8005b558 shot_k27_tossed_item: an item (anim +0x128, ground shadow 4) thrown in an
// arc (gravity 0x800 a tick, growing to full size) at a unit (+0x114). Once falling below
// y -0x1c2: the unit gets action kind 1, and an idle unit (+0x124 0) is started at once
// (slot 12) with +0x174 = anim - 5 (+0x130 2; else 1). The item goes when it falls below
// -0xf0 after starting its catcher, or below 0 otherwise.
void shot_k27_tossed_item(Shot* s) {
  BattleGlobals& g = bg();
  s->anim = static_cast<u16>(s->p128);
  shot_draw_and_box_ground(s, 4, 0xFFFFFFFF);
  if (g.flags != 0) return;
  shot_fade_out(s);  // (its result is not looked at)
  bobj_move(s, s->vel);
  s->scale_x += g.frame_ticks * 0x80;
  if (0xFFF < s->scale_x) s->scale_x = 0x1000;
  s->scale_y = s->scale_x;
  s->vel[1] += g.frame_ticks * 0x800;
  if (s->vel[1] <= 0) return;
  Unit* u = s->target;
  if (s->p130 == 0 && -0x1C2000 < s->pos[1]) {
    s->p130 = 1;
    u->action_kind = 1;
    if (u->action == 0) {
      s->p130 = 2;
      u->action_kind = 1;
      u->begin_action();
      u->counters[0] = s->p128 - 5;
    }
  }
  if ((s->p130 == 2 && -0xF0000 < s->pos[1]) || 0 < s->pos[1]) shot_kill(s);
}

// 0x8005b9d0 shot_k2d_slash_wave: a one-shot slash (anim 0xd of the borrowed set, one
// loop, box 0x20): its first meeting with the other side runs the owner's physical hit
// (power 0x40, knocked away from the owner), clears the owner's attack recovery and puts a
// spark (anim 0xf) where it is. Gone when the animation ends.
void shot_k2d_slash_wave(Shot* s) {
  BattleGlobals& g = bg();
  s->layer = 2;
  s->size[2] = 0x20;
  s->anim = 0xD;
  s->loop_limit = 1;
  bobj_project(s);
  if (static_cast<s16>(bobj_anim_draw(s, g.flags)) == -1) {
    shot_kill(s);
    return;
  }
  if (g.flags != 0 || s->hit_landed != 0) return;
  Unit* units[8] = {};
  Vec4 points[8] = {};
  const int n = collider_attack_overlaps(s, units, points, s->owner->is_enemy);
  if (n == 0) return;
  s->hit_landed = 1;
  const int angle = s->owner->pos[0] < points[0].x ? 0 : 0x800;
  unit_attack_targets(s->owner, n, units, points, 0x40, angle, 0);
  s->owner->attack_recovery = 0;
  if (Sprite* sp = shot_spawn_sprite(s, 0xF)) {
    sprite_set_pos_v(sp, s->pos);
    sp->layer = 2;
    sprite_set_fade(sp, 0x80, -2, 4);
  }
}

// 0x8005b328: the frame (UV box uv, tpage / CLUT words) as one textured patch from o +
// c0 to o + c1 (corners 1 and 2 swap x, y or z by flip bits 0-2), 'n' x 'n' quads at o's
// layer; nothing for an empty cel (v0 == v1).
void draw_frame_patch(BObj* o, const s32 c0[3], const s32 c1[3], u32 flip, int n, const s32 uv[4], u16 tpage,
                      u16 clut) {
  if (uv[1] == uv[3]) return;
  const u32 code = bobj_poly_tag(o, tpage);
  s32 c[4][4] = {};
  for (int k = 0; k < 3; ++k) {
    const s32 a = o->pos[k] + c0[k];
    const s32 b = o->pos[k] + c1[k];
    c[0][k] = a;
    c[3][k] = b;
    const bool swap = (flip & (1u << k)) != 0;
    c[1][k] = swap ? b : a;
    c[2][k] = swap ? a : b;
  }
  draw_textured_patch(uv, c, clut, tpage, o->layer, code, n);
}

}  // namespace

// 0x8005b23c shot_draw_frame_quad: steps o's animation (g_battle_flags | 0x9800: no
// sprite drawn) and draws the frame as a free quad between o + c0 and o + c1; returns the
// frame, or -1 when the loop limit was reached (then nothing is drawn).
s16 shot_draw_frame_quad(BObj* o, const s32 c0[3], const s32 c1[3], u32 flip, int n) {
  const s16 frame = static_cast<s16>(bobj_anim_draw(o, bg().flags | 0x9800u));
  if (frame == -1) return -1;
  const FrameUv f = frame_uv();
  const s32 uv[4] = {f.u0, f.v0, f.u1, f.v1};
  draw_frame_patch(o, c0, c1, flip, n, uv, f.tpage, f.clut);
  return frame;
}

bool enemy_shot_update(Shot* s) {
  switch (s->kind) {
    case 0x27: shot_k27_tossed_item(s); return true;
    case 0x2D: shot_k2d_slash_wave(s); return true;
    default: return false;
  }
}

}  // namespace ss::game::battle
