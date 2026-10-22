#pragma once

#include "core/types.h"

// Battle item use: item_use_battle and its helpers (#1984.1 0x80082dec-0x80085517; the
// entry point is declared in command_menu.h) and the item Shot kinds 0x2e-0x30.
namespace ss::game::battle {

struct Shot;
struct Unit;

// Kind 0x2e (0x8005bb50): an item's effect on +0x114 (item_spawn_heal): sparkles each
// frame, the first frame's sound / particles by +0x130 flags; after 45 ticks (or the fade)
// the status flags +0x130 are cured (a KO'd unit back on its feet), +0x128 % of max HP and
// +0x134 % of max MP restored; flag 0x40000000 then gives one of three bad statuses.
void shot_k2e_item_effect(Shot* s);
// Kind 0x2f (0x8005c5a0): a lit ball (+0x130 colour): +0x128 0 rolls on the ground with
// gravity and bounces (50 damage a hit), 1 flies straight, 2 bounces off the arena walls
// (300 damage, then fades); it fades after 180 ticks or out of the arena.
void shot_k2f_bouncing_ball(Shot* s);
// Kind 0x30 (0x8005cac4): a bomb (a vortex trail, colour +0x128; a sphere +0x130 unless
// +0x134 is 3) exploding on the ground or on the first unit it meets: damage by +0x134
// (0 the unit's ATK, 1 its DEF, 2 half its HP, 3 500; at most 9999), debris CLUT +0x138,
// sparks kind +0x10c; +0x134 2 also hits the party.
void shot_k30_bomb(Shot* s);
// 0x8005c4d4: a sprite of the effect bank (anim) round +0x114's chest, rising.
void shot_spawn_sparkle_on_target(Shot* s, int anim);

}  // namespace ss::game::battle
