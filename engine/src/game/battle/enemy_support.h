#pragma once

#include "core/types.h"
#include "game/battle/bload.h"
#include "game/battle/bsfx.h"
#include "game/battle/shot.h"
#include "game/battle/spell_fx.h"
#include "game/battle/spellcast.h"
#include "game/battle/spells.h"
#include "game/battle/unit.h"
#include "game/battle/unit_move.h"
#include "game/battle/voice_player.h"
#include "runtime/libgte.h"

// Resident helpers the monster classes (the families and the encounter packs' modules)
// call and nothing else in the port needed yet: small #2129 helpers and the #1984 block 0
// drawing API. (The monster projectile, ShotDesc / shot_desc_init / shot_spawn_projectile
// and Shot kind 0x16, lives in shot.h with the other Shot kinds.)
namespace ss::game::battle {

struct Shot;
struct Sprite;

// 0x80045814: x + dist << 12 in the direction o faces (spawn points in front of a unit).
s32 bobj_front_x(const BObj* o, s32 dist);
// 0x80053f0c: an effect sprite of u's set (sprite_spawn_from(anim, loops, u, 0)).
Sprite* unit_spawn_sprite_ex(const Unit* u, int anim, int loops);
// 0x800540e0: when 32 Shots are free, 4 (8 above 48 free) kind-0x2c thrown weapons fly off
// u with random velocities (the Starguarder's knockback).
void unit_throw_weapons(Unit* u, s32 variant);
// 0x8007c020 (#1984.0): GTE rotation = the render view, translation = view * -eye, so
// world-space vertices (units) can be drawn directly.
void gte_set_view_matrix();
// 0x8007c110 (#1984.0): a POLY_G3 from the current GTE matrix into pool A at otz/8 - 8
// (front: OT 0); a semi-transparent code (bit 25) adds an additive DR_MODE.
void draw_tri_g3(const psyq::SVECTOR v[3], const u32 code_rgb[3], int front);

// 0x8007b3c0 (#1984.0): a beam (cylinder / cone of 'sides' additive gouraud quads per slice,
// length / 0x140 + 1 slices, at most 8) from pos along dir, colours rgb_a -> rgb_b along it;
// each quad joins the previous angle at radius r0 to this one at r1 (r1 0: r0).
void draw_beam(const s32 pos[3], const s32 dir[3], u32 rgb_a, u32 rgb_b, s32 length, s32 r0, s32 r1, s32 sides);
// 0x8007d2d4 (#1984.0): an n x n grid (n <= 8) of POLY_FT4 bilinearly spread between four
// world corners (units << 12; c0 c1 the first row, c2 c3 the last) with the UVs spread over
// uv {u0, v0, u1, v1}; 'code_rgb' is each quad's colour word, OT 0x610 + bias - otz/8.
void draw_textured_patch(const s32 uv[4], const s32 corners[4][4], u16 clut, u16 tpage, s32 ot_bias, u32 code_rgb,
                         s32 n);
// 0x8007d808 (#1984.0): one world-space POLY_GT4 (vertices in units) with the UV rectangle
// {u0, v0, u1, v1} and four colour words.
void draw_quad_gt4(const s32 uv[4], const psyq::SVECTOR v[4], u16 clut, s32 tpage, s32 ot_bias, const u32 rgb[4]);
// 0x80048ebc: a random point of the disc of 'radius' units round centre (y 0).
Vec4 vec_random_in_disc_xz(const s32 centre[3], s32 radius);
// 0x80045848: the colour word of a POLY_FT4 for o (colour mode +0x63) with code 0x2c, or
// 0x2e when flags & 0x60 (semi-transparent).
u32 bobj_poly_tag(const BObj* o, u16 flags);
inline void bobj_set_pos(BObj* o, const s32 p[3]) { o->pos[0] = p[0], o->pos[1] = p[1], o->pos[2] = p[2]; }  // 0x80055410
// 0x80034e0c: attack box 0 = the position +- the half sizes (units); enables box bit 2.
void collider_set_attack_box(Collider* c, s32 hx, s32 hy, s32 hz);
// 0x80053e94: 1 when status bits 0xe6, +0x11c bits 0x48 or the freeze timer +0x154 hold u.
int unit_is_disabled(const Unit* u);
// 0x80056a0c: when s's owner holds the skill camera (+0x132), camera mode 7 ends on it after
// 60 ticks.
void shot_skill_camera_end(Shot* s);
// 0x8007fd54 voice_release (#1984.0): releases the clip playing in the handle's slot (a
// bsfx_play handle), if it still plays there. (Next to bsfx.h's other handle calls; only
// the monster spells use it so far.)
void sound_handle_release(u32 handle);

// ---- Spells: the world agent's spells.h / spellcast.h / spell_fx.h (included above) have
// the spell API, the SpellCast and the screen fade the stand-ins here stood for.


// ---- Resident Shot kinds and helpers only the monsters use (enemy_shots.cpp).
// 0x8005b23c: steps o's animation (no sprite drawn) and draws the frame as a free quad from
// o + c0 to o + c1 (flip bits 0-2 swap corners 1 / 2 in x / y / z), n x n patches; the
// frame, or -1 at the loop limit.
s16 shot_draw_frame_quad(BObj* o, const s32 c0[3], const s32 c1[3], u32 flip, int n);
// Shot::update's kinds 0x27 (0x8005b558, a tossed item) and 0x2d (0x8005b9d0, a slash
// wave); false for any other kind.
bool enemy_shot_update(Shot* s);

// Debugging (SS_ENEMY_TRACE=1): once a frame, from enemy_think of the first enemy still in
// the battle, prints every enemy's type, state, action, position, HP and scratch words and
// the party's state, position and HP (the same line the enemy worker's Redux script logs).
void enemy_debug_trace(const EnemyUnit* e);


}  // namespace ss::game::battle
