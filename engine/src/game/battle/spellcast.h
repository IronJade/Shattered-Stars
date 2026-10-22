#pragma once

#include <array>
#include <vector>

#include "core/types.h"
#include "game/battle/battle.h"
#include "game/battle/bobj.h"
#include "game/battle/spells.h"
#include "runtime/libgpu.h"

// The SpellCast (#2129 0x80085518-0x80088054, live until the result screen): the object
// that runs a spell spell_cast does not run itself. 0x248 bytes, vtable 0x80088014, task
// priority 3; at most two at once (g_spellcasts 0x8006fe08: the second is a spell clash
// and opens the clash window); g_battle_flags bit 0x40 is set while any runs.
//
// Each frame (spellcast_update) it executes one command of its spell data (sprite set,
// textures, sounds, raw data; spellcast_load_step) and then its handler: the spell's code
// module (command 6 of the data: one native function per module here, found by the
// module's bytes), else the built-in kind handler (kinds 1 and 2; 0x36 / 0x3b live in
// two enemy-code blocks). A handler is a step machine (+0x40) over the API below.
// parts/p7.md "SpellCast system".
namespace ss::game::battle {

struct Unit;
struct SpellCast;
using SpellModuleFn = void (*)(SpellCast*);

struct SpellCast : Task {
  u8 kind = 0;                // +0x08
  u8 dim_state = 0;           // +0x09: palette dim 0 off, 1 fading in, 2 fading out
  u8 dim_side = 0;            // +0x0a: 0 the party's CLUT rows, 1 the enemies'
  u8 dim_strip = 0;           // +0x0b: the strip (of two) the dim converts this frame
  u8 slot = 0;                // +0x0c: g_spellcasts index
  u8 combo = 0;               // +0x0d: the caster's clash result (+0x156): 1 / 3 between the two casters, 2 at the partner's centre, 4 adds the partner's power, 3 hits the own side
  u8 finished = 0;            // +0x0e
  u8 alt_clut = 0;            // +0x0f: set by data command 9
  u8 loaded = 0;              // +0x10: the data's command list is done
  u8 spell = 0;               // +0x11
  Unit* caster = nullptr;     // +0x14
  Unit* target = nullptr;     // +0x18
  s32 power = 0;              // +0x1c
  s32 s20 = 0, s24 = 0, s28 = 0, s2c = 0, s30 = 0, s34 = 0, s38 = 0, s3c = 0;  // +0x20..+0x3c: the handler's (cleared at start)
  s32 step = 0;               // +0x40
  s32 wait = 0;               // +0x44: spellcast_wait's ticks
  s32 eff_power = 0;          // +0x48: the power the hits use
  s16 load_state = 0;         // +0x4c: 0 next command, 1 streaming a sprite sheet, 2 streaming a texture block
  s16 tex_count = 0;          // +0x4e: texture blocks loaded (command 3)
  s16 part = 0;               // +0x50: sprite-set parts loaded (commands 4 / 7 / 9)
  s16 tex_uploads = 0;        // +0x52: images uploaded to VRAM
  SpellData data;             // +0x54: the spell data (dropped once its list is done)
  SpellData data_retired;     // (port) the dropped data, kept for the clips that play from it
  size_t cmd = 0;             // +0x58: offset of the next command in data
  std::vector<u8> header;     // +0x60: the sprite set's layout (command 4)
  std::vector<u8> anims;      // +0x64: its animation directory
  std::array<std::vector<u8>, 8> frames{};    // +0x68: cel banks per part
  std::array<std::vector<u8>, 8> sheets{};    // +0x88: texture banks per part (streamed)
  std::array<std::vector<u8>, 8> textures{};  // +0xa8: texture blocks (command 3, streamed)
  std::vector<u8> raw;        // +0xc8: command 8's block
  s32 stream_left = 0;        // +0xcc: (port) bytes still to stream of the current block
  std::array<std::vector<u16>, 2> clut_saved{};  // +0xe8: the dimmed CLUT rows as they were
  std::array<std::vector<u16>, 2> clut_work{};   // +0xf0: the converted rows
  std::array<psyq::RECT, 2> clut_rect{};         // +0xf8
  std::array<s16, 2> clut_rows{};                // +0x108
  std::array<s16, 2> clut_y{};                   // +0x10c
  s16 dim_level = 0;          // +0x110: 0..0x20
  s32 centre[4] = {};         // +0x114: the spell's centre (handlers set it)
  s32 aux_point[4] = {};      // +0x124..+0x130: a second point (modules: e.g. the combo point)
  BObj obj;                   // +0x134: the data's sprite set (obj.animset, +0x190)
  BObj obj2;                  // +0x1bc (pool +0x240)
  SpellModuleFn module = nullptr;  // +0x244: the code module's entry
  int module_index = -1;      // (port) the registry entry of the module
  bool module_missing = false;  // (port) the data names a module that is not ported
  std::vector<u8> module_code;  // (port) the module's block 0 (its tables, spell_module_u8)
  AnimSet* orb_set = nullptr;   // (port) +0x3c read as an AnimSet by kind 2's orbs

  SpellCast();                // spellcast_init 0x80085760
  ~SpellCast() override;      // spellcast_destroy 0x80085824
  void update() override;     // [1] spellcast_update 0x80085b0c
};

// g_spellcasts[i] (null when the slot is free).
SpellCast* spellcast_slot(int i);
inline const SpellCast* spellcast_slot0() { return spellcast_slot(0); }
int spellcast_kind_active(int kind);                 // 0x80085518
// 0x80085560: a cast of 'kind' (a free slot, at most 2; none once the battle state is past
// 2): a target on the other side retargeted to a standing unit there (none: null), the code
// module unpacked from the data; the second slot opens the clash window.
SpellCast* spellcast_create(int kind, Unit* caster, Unit* target, int power, SpellData data = nullptr);
void spellcast_finish(SpellCast* sc);                // 0x800859e0
void spellcast_reset();                              // battle start / end: both slots freed

// The module API.
void battle_flash_set(SpellCast* sc, int r, int g, int b);  // 0x80085c10
void spellcast_step_when_camera_idle(SpellCast* sc);       // 0x80085c50
void spellcast_wait(SpellCast* sc, int ticks);             // 0x80085c84
int spellcast_is_second_slot(SpellCast* sc);               // 0x80085c98
int spellcast_timer_tick(SpellCast* sc);                   // 0x80085ccc: 1 while waiting
void spellcast_step_when_dim_done(SpellCast* sc);          // 0x80085d04
Unit* spellcast_retarget(SpellCast* sc, Unit* target);     // 0x80085d2c
int units_in_rect(SpellCast* sc, const s32 rect[4], Unit** out, int party);  // 0x80085e3c
void spell_damage_area(SpellCast* sc, const s32 rect[4], int power, u16 element, int knockback, int party);  // 0x80085f7c
void spell_status_area(SpellCast* sc, const s32 rect[4], u32 mask, int chance, int icon, int party);  // 0x8008613c
void spellcast_dim_begin(SpellCast* sc, int side);         // 0x80086264
void spellcast_dim_end(SpellCast* sc);                     // 0x80086478
void spellcast_dim_step(SpellCast* sc);                    // 0x800864bc
void spell_hit_all(SpellCast* sc, u16 element, int knockback);              // 0x800867a0
void spell_hit_rect(SpellCast* sc, const s32 rect[4], u16 element, int knockback);  // 0x800868a0
void spell_hit_target(SpellCast* sc, u16 element, int knockback);           // 0x80086998
void spell_status_all(SpellCast* sc, u32 mask, int chance, int icon);       // 0x80086bec
void spellcast_load_step(SpellCast* sc);                   // 0x80086c68
int spellcast_effects_gone(SpellCast* sc);                 // 0x8008742c: 1 when no sprite / Shot uses the cast's set
int spellcast_combo_point(SpellCast* sc, s32 out[4]);      // 0x80087560: 0 when the cast is no combo
Sprite* spellcast_sprite_spawn(SpellCast* sc, int anim);   // 0x80087f90
Sprite* spellcast_sprite_spawn2(SpellCast* sc, int anim, int loops);  // 0x80087fbc

// Built-in kind handlers (the table 0x80088020: kinds 1 and 2 here; 0x36 and 0x3b are
// enemy-code block functions, enemy_mod_bark.cpp / enemy_mod_indalecio.cpp).
void spell_kind1_update(SpellCast* sc);  // 0x80087730
void spell_kind2_update(SpellCast* sc);  // 0x80087c98
void bark_cast_step(SpellCast* sc);              // kind 0x36 (0x800885d8, block of #1427 ...)
void spellcast_kind3b_transform(SpellCast* sc);  // kind 0x3b (0x80089c80, Indalecio)

// The spell code modules (spell_mod_<name>.cpp): a module's own tables at their link
// addresses (block 0, linked for 0x8008cc00; the same offsets serve slot 1's copy).
u8 spell_module_u8(const SpellCast* sc, u32 address);
s16 spell_module_s16(const SpellCast* sc, u32 address);
s32 spell_module_s32(const SpellCast* sc, u32 address);

}  // namespace ss::game::battle
