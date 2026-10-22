#pragma once

#include <memory>
#include <span>

#include "core/types.h"
#include "game/battle/unit.h"

// The monster classes: the 16 resident families (vtables 0x8006ba30-0x8006cb90, code
// 0x8003e8d0-0x80041600) and the classes of the enemy-code modules, which the console
// unpacks from the encounter pack into slots A 0x80088110, B 0x80089f18, C 0x8008b1f0 and
// D 0x80088900 (or finds there in the #2129 image). Every module is a native class here,
// chosen by the module's identity (the bytes of the block the pack names for the slot)
// and the monster type, as the monster factory 0x80075ad8 does by address.
namespace ss::game::battle {

// The monster factory's class choice (0x80075ad8) for 'type': a family or module class,
// the EnemyUnit base for the base types and for modules that are not ported (logged), or
// null for an unknown type.
std::unique_ptr<EnemyUnit> enemy_class_create(int type);

// The encounter pack's code list (load_formation's third list): which module each slot
// holds for this battle. reset() returns every slot to the #2129 image's default module.
void enemy_modules_reset();
void enemy_modules_load(int kind, std::span<const u8> packed_block);

// The encounter keeps a unit a module made mid-battle (a slime's or gelatin's split copy,
// which the console allocates and battle_free_all frees with the other enemies).
void enemy_adopt(std::unique_ptr<EnemyUnit> e);
// g_enemy_extra_data 0x8006fd8c[i]: the encounter pack's extra data blocks (the sprite
// pack's extra offsets: voice clips and the like for some bosses), or null.
const u8* enemy_extra_data(int i);

// 0x8003e248: the family attack helper. When fighting, not afraid and not immobile, and
// the nearest member is at least min_dist away (or rng_next15 < chance), then with
// 25000 / 32768: approach that member (or the provoker) with 'action' at 'speed' (+0x13a);
// 1 when the approach started (also 1 for immobile monsters).
int enemy_try_approach(EnemyUnit* e, s32 min_dist, u32 chance, u8 action, s16 speed);

}  // namespace ss::game::battle
