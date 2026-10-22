#pragma once

#include <memory>

#include "core/types.h"
#include "game/battle/unit.h"

// The enemy-code modules (internal to the monster classes). Each distinct code block of
// the encounter packs (40 on the two discs, identified by their bytes) and the three
// default modules of the #2129 image is one native class; the module files below define
// them. A module's own tables are read from its bytes at their link addresses.
namespace ss::game::battle {

// The four enemy-code slots (the code list's kinds).
enum EnemySlot : int { kSlotA = 0, kSlotB = 1, kSlotC = 2, kSlotD = 3 };
constexpr u32 kSlotBase[4] = {0x80088110, 0x80089F18, 0x8008B1F0, 0x80088900};

// A byte / halfword / word of the module in 'slot' at its link 'address' (the pack's
// block, or the #2129 image's default module); 0 outside it.
u8 enemy_module_u8(int slot, u32 address);
s16 enemy_module_s16(int slot, u32 address);
s32 enemy_module_s32(int slot, u32 address);

// The module classes (enemy_mod_<name>.cpp; names: the monsters each one serves).
std::unique_ptr<EnemyUnit> enemy_mod_guardian_create();   // #2129 slot A (0x80088110, vt 0x80089df0)
std::unique_ptr<EnemyUnit> enemy_mod_nicolus_create();    // #2129 slot B (0x80089f18, vt 0x8008b0c8)
std::unique_ptr<EnemyUnit> enemy_mod_jibril_create();     // #2129 slot C (0x8008b1f0, vt 0x8008c0c4)
std::unique_ptr<EnemyUnit> enemy_mod_cyril_create();
std::unique_ptr<EnemyUnit> enemy_mod_carlaeagle_create();
std::unique_ptr<EnemyUnit> enemy_mod_gelatin_create();
std::unique_ptr<EnemyUnit> enemy_mod_arcmene_create();
std::unique_ptr<EnemyUnit> enemy_mod_hounddog_create();
std::unique_ptr<EnemyUnit> enemy_mod_salamander_create();
std::unique_ptr<EnemyUnit> enemy_mod_kitty_create();
std::unique_ptr<EnemyUnit> enemy_mod_ricki_create();
std::unique_ptr<EnemyUnit> enemy_mod_ogre_create();
std::unique_ptr<EnemyUnit> enemy_mod_ericodus_create();
std::unique_ptr<EnemyUnit> enemy_mod_xine_create();
std::unique_ptr<EnemyUnit> enemy_mod_succubus_create();
std::unique_ptr<EnemyUnit> enemy_mod_dias_create();
std::unique_ptr<EnemyUnit> enemy_mod_ghost_create();
std::unique_ptr<EnemyUnit> enemy_mod_zand_create();
std::unique_ptr<EnemyUnit> enemy_mod_gargoyle_create();
std::unique_ptr<EnemyUnit> enemy_mod_bark_create();
std::unique_ptr<EnemyUnit> enemy_mod_synard_create();
std::unique_ptr<EnemyUnit> enemy_mod_celesta_create();
std::unique_ptr<EnemyUnit> enemy_mod_berle_create();
std::unique_ptr<EnemyUnit> enemy_mod_magichand_create();
std::unique_ptr<EnemyUnit> enemy_mod_lover_create();
std::unique_ptr<EnemyUnit> enemy_mod_marsilio_create();
std::unique_ptr<EnemyUnit> enemy_mod_shigeo_create();
std::unique_ptr<EnemyUnit> enemy_mod_ruprecht_create();
std::unique_ptr<EnemyUnit> enemy_mod_vesper_create();
std::unique_ptr<EnemyUnit> enemy_mod_snowman_create();
std::unique_ptr<EnemyUnit> enemy_mod_bunny_create();
std::unique_ptr<EnemyUnit> enemy_mod_stingray_create();
std::unique_ptr<EnemyUnit> enemy_mod_gerel_create();
std::unique_ptr<EnemyUnit> enemy_mod_controller_create();
std::unique_ptr<EnemyUnit> enemy_mod_niquia_create();
std::unique_ptr<EnemyUnit> enemy_mod_azamgil_create();
std::unique_ptr<EnemyUnit> enemy_mod_visseyer_create();
std::unique_ptr<EnemyUnit> enemy_mod_guardbox_create();
std::unique_ptr<EnemyUnit> enemy_mod_decus_create();
std::unique_ptr<EnemyUnit> enemy_mod_slime_create();
std::unique_ptr<EnemyUnit> enemy_mod_miel_create();
std::unique_ptr<EnemyUnit> enemy_mod_shin_create();
std::unique_ptr<EnemyUnit> enemy_mod_indalecio_create();

}  // namespace ss::game::battle
