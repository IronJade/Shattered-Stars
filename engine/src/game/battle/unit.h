#pragma once

#include <array>
#include <span>

#include "core/types.h"
#include "game/battle/bobj.h"

// Battle combatants: Collider (hit boxes), Unit (0x200 bytes, vtable 0x8006cde0, ctor
// unit_create 0x8004dd30), EnemyUnit (0x26c, vtable 0x8006b908, ctor enemy_create
// 0x8003c8c8) and PartyUnit (vtable 0x8006b6d0, ctor 0x80034ed0). Layouts and slot
// tables: analysis/battle_catalog/parts/p1.md (PartyUnit), p2.md (EnemyUnit), p3.md (Unit).
// Virtual slot n of the original is the method commented "[n]".
namespace ss::game::battle {

struct Afterimage;
struct RibbonTrail;

// The stats record (the PartySlot layout, 0x60 bytes): the handoff-3 slot itself for
// party members, a 0x60-byte block for monsters (built from the encounter pack).
struct Stats {
  u16 id;             // +0x00: character / monster type
  u8 status;          // +0x02: 1 KO, 2/4 incapacitated, 8 poison, 0x40 stun
  u8 klass;           // +0x03: 9-12 casters
  u8 element[10];     // +0x04: element affinity codes (0 normal, 1 weak, 2 resist, 3 immune, 4 absorb)
  u8 resist[2];       // +0x0e: status resistance per roll kind
  s32 exp;            // +0x10
  s32 hp_base;        // +0x14
  s32 hp_max;         // +0x18
  s32 hp;             // +0x1c
  s16 mp_base;        // +0x20
  s16 mp_max;         // +0x22
  s16 mp;             // +0x24
  s16 battle_level;   // +0x26
  s16 level;          // +0x28
  s16 atk[3];         // +0x2a: base, equipped, battle
  s16 def[3];         // +0x30
  s16 eva[3];         // +0x36
  s16 acc[3];         // +0x3c
  s16 intel[3];       // +0x42
  s16 guts[3];        // +0x48
  s16 stat4e[3];      // +0x4e (+0x52 knockback power)
  s16 stat54[3];      // +0x54 (+0x58 knockback resistance)
  s16 crit;           // +0x5a: critical rate (/512)
  u16 weapon_element; // +0x5c
  u16 pad5e;
};
static_assert(sizeof(Stats) == 0x60);
inline Stats* stats_of(u8* p) { return reinterpret_cast<Stats*>(p); }

struct Collider : BObj {
  // Body boxes 0/1 and attack boxes 0/1: min corners +0x88/+0x98 and +0xc8/+0xd8, max
  // corners +0xa8/+0xb8 and +0xe8/+0xf8 (x, y, z, pad).
  s32 body_min[2][4] = {};
  s32 body_max[2][4] = {};
  s32 attack_min[2][4] = {};
  s32 attack_max[2][4] = {};
  s32* get_box(int code) override;  // collider_get_box
};

struct Unit : Collider {
  const u8* model_layout = nullptr;   // +0x108
  const u8* model_sheet = nullptr;    // +0x10c
  const u8* model_frames = nullptr;   // +0x110
  const u8* model_anims = nullptr;    // +0x114
  u16 clut = 0;                       // +0x118
  u16 tpage = 0;                      // +0x11a
  u32 status_bits = 0;                // +0x11c: rebuilt each frame
  u8 command = 0;                     // +0x120
  u8 jumping = 0;                     // +0x121
  u8 flying = 0;                      // +0x122
  u8 is_enemy = 0;                    // +0x123
  u8 action = 0;                      // +0x124
  u8 slot = 0;                        // +0x125
  u8 recovery = 0;                    // +0x126
  u8 reaction = 0;                    // +0x127: 1 hit, 2 evade, 3 bumped
  u8 death = 0;                       // +0x128
  u8 ignore_units = 0;                // +0x129
  u8 action_kind = 0;                 // +0x12a
  u8 move_mode = 0;                   // +0x12b
  u8 hit_wall = 0;                    // +0x12c
  u8 straight = 0;                    // +0x12d
  u8 spell = 0;                       // +0x12e
  u8 removed = 0;                     // +0x12f: 1 dead enemy, 2 fled
  u8 weapon_status = 0;               // +0x130
  u8 weapon_chance = 0;               // +0x131
  u8 camera_focus = 0;                // +0x132
  u8 approach_side = 0;               // +0x133
  u8 hits_mp = 0;                     // +0x134
  u8 status_enabled = 1;              // +0x135
  u8 half_damage = 0;                 // +0x136
  s16 cast_time = 0;                  // +0x138
  s16 reach = 0;                      // +0x13a
  s16 heading = 0;                    // +0x13c
  s16 detour = 0;                     // +0x13e
  s16 base_reach = 0;                 // +0x142
  s16 attack_recovery = 0;            // +0x144
  s16 poison_tick = 0;                // +0x146
  s16 dot_tick = 0;                   // +0x148
  s16 shake_amp = 0;                  // +0x14a
  s16 shake_frames = 0;               // +0x14c
  s16 shadow_scale = 0x100;           // +0x14e
  s16 recovery_gauge = 0;             // +0x150
  s16 shadow_scale2 = 0x100;          // +0x152
  s16 super_armour = 0;               // +0x154
  s16 clash = 0;                      // +0x156
  s16 stuck_ticks = 0;                // +0x158
  s16 sfx_hit = 0;                    // +0x15a
  s16 sfx_crit = 0;                   // +0x15c
  s16 sfx_guard = 0;                  // +0x15e
  s16 hover_amp = 0;                  // +0x160
  s16 lookahead = 0;                  // +0x162
  s16 special_timer = 0;              // +0x164
  s16 knockback_ticks = 0;            // +0x166
  s32 hover_height = 0;               // +0x168
  s32 run_speed = 0;                  // +0x16c
  s32 base_speed = 0;                 // +0x170
  s32 counters[4] = {};               // +0x174..+0x180
  s32 leap_ticks = 0;                 // +0x184
  s32 dist_left = 0;                  // +0x188
  s32 detour_timer = 0;               // +0x18c
  s32 last_damage = 0;                // +0x190
  s32 step_dest[4] = {};              // +0x194
  s32 vanish_target[4] = {};          // +0x1a4
  s32 home[4] = {};                   // +0x1b4: last footprint / start position
  s32 knockback[4] = {};              // +0x1c4
  Unit* target = nullptr;             // +0x1d4
  s32 walk_dest[4] = {};              // +0x1d8
  Unit* last_attacker = nullptr;      // +0x1e8
  Sprite* status_icons[2] = {};       // +0x1ec
  Afterimage* effects[2] = {};        // +0x1f4: the bound afterimage trails (killer moves)
  RibbonTrail* effect_trail = nullptr;  // +0x1f8 when a module binds a ribbon trail there (Ashton's move 1)
  u8* stats = nullptr;                // +0x1fc

  Unit();  // unit_create 0x8004dd30
  Stats* st() const { return stats_of(stats); }
  // Unit virtual slots 3..21 (1 = update, 2 = get_box).
  virtual int can_cast();                         // [3] unit_can_cast 0x8005287c (spells.cpp)
  virtual void interrupt() {}                     // [4]
  virtual int start_reaction();                   // [5] unit_start_reaction 0x80051fdc
  // [6] unit_jump_time 0x80052a3c: a leap's flight time (Chisato's module shortens it).
  virtual s32 jump_time(s32 v, const s32 dest[3]);
  virtual void update_vanish();                   // [8] unit_update_vanish 0x80053c04 (state 6)
  virtual int skill_level(int) { return 0; }      // [9]
  virtual void update_status_effects();           // [10] unit_update_status_effects 0x80053240
  virtual void release_spell() {}                 // [11]
  virtual void begin_action() {}                  // [12]
  virtual int resist_roll(int kind, int chance);  // [13] unit_resist_roll 0x80053b24
  virtual int status_test(int, u32) { return 0; } // [14]
  virtual void set_effect_timer(int, s16) {}      // [16]
  virtual int race() { return 0; }                // [17]
  virtual void accessory_proc(int, int, u32, int) {}  // [18] (item, word, mask, chance %)
  virtual void clear_status(int, u32) {}          // [19]
  virtual void set_status(int, u32) {}            // [20]
  virtual void status_recovered() {}              // [21]
  // The two bound afterimages (+0x1f4 / +0x1f8) end (their +0xc = 1) and are let go.
  void release_effects();
};

// Unit-related globals.
struct UnitGlobals {
  u8 blend_mode = 0;    // 0x8006fb38 g_unit_blend_mode: 1 all units semi-transparent, 2 opaque
  u8 blend_alpha = 0;   // 0x8006fb39 g_unit_blend_alpha
};
UnitGlobals& unit_globals();

// unit_is_down 0x80053e50: missing, status bits 0-2, or removed.
bool unit_is_down(const Unit* u);
// (world agent) The "unit at address 0" a null target reads on the console (original_bugs.md
// S3): low kernel RAM, the same on every run: position (0, 0, 0), size 0, party slot 2, stats
// pointer 0; unit_is_down calls it down (the console's null test).
Unit* unit_null_target();
// 0x80034418 unit_check_move: what blocks moving u by d (flags 1 party, 2 enemies, 4
// obstacles, 8 skip the arena bounds): a unit, kWallBlock (the original's -1: the arena
// edge or an obstacle) or null. (World agent: takes any BObj; item kind 0x2f's balls use it.)
extern Unit* const kWallBlock;
Unit* unit_check_move(const BObj* u, const s32 d[3], u32 flags);
Unit* unit_try_move_scaled(const Unit* u, const s32 v[3], u32 flags);  // 0x800343b4: d = v * ticks
void unit_draw_shadow(Unit* u, s32 height);  // 0x80052760
s32 unit_ground_follow(Unit* u);   // 0x80052504
void unit_draw_model(Unit* u);     // 0x8004dfa8
void unit_apply_global_blend(Unit* u);  // 0x8004df68
void unit_update_base(Unit* u);    // 0x8004e0ec (Unit vtable[1])
void unit_release_spell_data(Unit* u);  // 0x8004e09c: a pending spell without the hold timer is dropped

// The resident monster families (the factory's vtables); Module = a class in the
// encounter pack's code (not ported: base behaviour).
enum class EnemyFamily : u8 {
  None, Base, Knight, Guard, Plant, Kobold, Thief, Magius, Axe, Sandglass, Ooze, Beastmaster, Wolf, Giant, Goat,
  Archer, Shout, Wizard, Module,
};

struct EnemyUnit : Unit {
  EnemyFamily family = EnemyFamily::Base;
  std::array<const u8*, 7> frames_more{};  // +0x200: frames of sheets 1..7
  std::array<const u8*, 7> sheets_more{};  // +0x21c
  u8 boss = 0;                        // +0x238: immune to status kind 0
  u8 ai_ticks = 0;                    // +0x239
  u16 drop_item[2] = {};              // +0x23a
  u16 aggro_radius = 0;               // +0x23e
  s16 hover_phase = 0;                // +0x240
  s16 intro_voice = 0;                // +0x242
  u8 drop_chance[2] = {};             // +0x244
  u8 counter_window = 0;              // +0x246
  u8 race_byte = 0;                   // +0x247
  u8 voice_variants = 0;              // +0x248
  u8 spells[4] = {};                  // +0x249
  u8 odds[5] = {};                    // +0x24d
  char name[16] = {};                 // +0x252
  s32 voice_handle = 0;               // +0x264
  s32 fol = 0;                        // +0x268

  EnemyUnit();  // enemy_create 0x8003c8c8
  void update() override;             // [1] enemy_update 0x8003c944
  int start_reaction() override;      // [5] (Axe family 0x8003f37c; base Unit)
  void release_spell() override;      // [11] enemy_release_spell 0x8003e720
  void begin_action() override;       // [12] enemy_begin_action 0x8003d52c
  int resist_roll(int kind, int chance) override;  // [13] enemy_status_roll 0x8003e210
  int race() override { return race_byte; }  // [17] enemy_get_247
  virtual int animate(u32 flags);     // [22] enemy_animate 0x8003c9e4
  virtual void state_approach();      // [23] enemy_state_approach 0x8003d328
  virtual void state_hit();           // [24] enemy_state_hit 0x8003d6ec (states 3 / 4)
  virtual void state_dying();         // [25] enemy_state_dying 0x8003d764
  virtual void choose_target();       // [26] enemy_choose_target 0x8003dca0
  virtual void attack1();             // [27] enemy_attack1 0x8003de88 (state 9, anim 4)
  virtual void attack2();             // [28] 0x8003deb4 (state 10, anim 5)
  virtual void attack3();             // [29] 0x8003ded8
  virtual void attack4();             // [30] 0x8003defc
  virtual void attack5();             // [31] 0x8003df20
  virtual int pre_think() { return 0; }  // [32] enemy_pre_think_default 0x8003e060
  virtual void begin_move();          // [33] enemy_begin_move 0x8003e784
  virtual void on_attack_start(int) {}  // [34] enemy_on_attack_start 0x8003e8a0
  virtual void state_cast();          // [35] enemy_state_cast 0x8003e5dc
};
// EnemyUnit helpers.
void enemy_ai_tick(EnemyUnit* e);              // 0x8003d8a0
void enemy_wander(EnemyUnit* e);               // 0x8003db8c
Unit* enemy_find_provoker();                   // 0x8003dbe8
void enemy_aggro_check(EnemyUnit* e);          // 0x8003dda0
void enemy_start_attack(EnemyUnit* e);         // 0x8003de4c
int enemy_attack_step(EnemyUnit* e);           // 0x8003df44
int enemy_nearest_party_index(const Unit* e);  // 0x8003e068
void enemy_try_cast(EnemyUnit* e);             // 0x8003e3b0
Unit* enemy_random_party_member(const Unit* e);  // 0x8003e7d4
bool unit_pick_random_destination(Unit* u, s32 range, s32 min_dist);  // 0x800541e8: true when no free spot was found
void battle_victory_begin(Unit* last_hitter);  // 0x80048c5c

// Player characters (vtable 0x8006b6d0, ctor 0x80034ed0, 0x360 bytes for Claude). The
// character modules (#2131, #2133, ...) construct the per-character subclass; the port
// builds the base class (party_loader.cpp) and keeps the module hooks as virtuals.
struct PartyUnit : Unit {
  u8 appear = 0;                      // +0x200: 1 hidden, 2 appearing, 4
  u8 busy = 0;                        // +0x201
  u8 leap_dir = 0;                    // +0x202: leap direction class (0 none)
  u8 leaping = 0;                     // +0x203
  u8 combo = 0;                       // +0x204: in a killer-move combo
  u8 weapon_variant = 0;              // +0x205
  u8 flee_voice = 0;                  // +0x206
  u8 ai_spell = 0;                    // +0x207
  u8 chain = 0;                       // +0x208: hit-pause / chain counter of the swing
  u8 counter_ticks = 0;               // +0x209: ticks in a reaction
  u8 eva_bonus = 0;                   // +0x20a
  s16 tex_timer = 0;                  // +0x222
  s32 fury_ticks = 0;                 // +0x22c
  s32 last_dist = 0;                  // +0x230
  s32 action_count = 0;               // +0x238
  s16 flee_timer = 0;                 // +0x2dc
  s16 target_far = 0;                 // +0x2ee
  s16 killer_index = 0;               // +0x2f0
  s16 hp_at_voice = 0;                // +0x2fa
  s16 victory_voice = 0;              // +0x2fc
  s16 regen_ticks = 0;                // +0x2fe: pc_update_effects' 300-tick HP / MP regeneration
  s16 regen_mp_ticks = 0;             // +0x300: its 600-tick MP regeneration
  s16 mp_drain_ticks = 0;             // +0x302: the MP drain's tick count (effect 2 / 0x100)
  Sprite* low_hp_marker = nullptr;    // +0x328
  s32 item_value_sum = 0;             // +0x21e (item field 0xa total)
  u16 weapon_clut = 0;                // +0x21c
  s16 wait_after_spell = 0;           // +0x220
  s32 jump_threshold = 0;             // +0x224
  s32 talk_voice = 0;                 // +0x228
  s32 v234 = 0;                       // +0x234
  std::array<AnimSet*, 5> model_sets{};  // +0x23c: default + 4 alternate model sets
  std::array<const u8*, 4> alt_sheet{};   // +0x250: killer-move texture block per alternate
  std::array<const u8*, 4> alt_frames{};  // +0x260
  std::array<const u8*, 4> alt_anims{};   // +0x270
  std::array<const u8*, 4> alt_layout{};  // +0x280
  std::array<u16, 4> alt_clut{};          // +0x2d0
  std::array<s16, 8> killer_range{};  // +0x2de: [combo][move] near / far ranges
  std::array<s16, 4> killer_charge{}; // +0x2f2: the moves' proficiency (charge levels)
  // Party loader phases 1-4: the killer-move data blocks.
  std::array<std::array<const u8*, 4>, 4> move_parts{};  // +0x290: [move][part]
  std::array<std::array<const u8*, 2>, 4> attack_data{}; // +0x330: block +0x10 / +0x18 per move
  std::array<const u8*, 4> move_data{};                   // +0x350: MoveData (block +0x20)
  s16 v2d8 = 0;                       // +0x2d8 (character modules)
  s16 v2da = 0;                       // +0x2da
  s16 acc_hp_max[2] = {};             // +0x304: max HP without accessory 1 / 2
  s16 acc_mp_max[2] = {};             // +0x308: max MP without accessory 1 / 2
  s16 effect_timer[3] = {};           // +0x30c: timed effect counters 1..3
  u32 effect_bits[4] = {};            // +0x314: equipment / talent ability bits
  u8* record = nullptr;               // +0x324: handoff-4 character record
  // The member's model data (party-data loader phase 0): +0x108.. (Unit) plus the
  // second body texture block and the weapon blocks.
  const u8* sheet_alt = nullptr;      // +0x20c
  const u8* model_210 = nullptr;      // +0x210
  const u8* weapon_sheet = nullptr;   // +0x214
  const u8* model_218 = nullptr;      // +0x218

  PartyUnit();                        // 0x80034ed0
  void update() override;             // [1] pc_update 0x80034fe8
  int start_reaction() override;      // [5] pc_start_reaction 0x8003bb84
  int skill_level(int skill) override;  // [9] pc_skill_level 0x8003b794
  void update_status_effects() override;  // [10] pc_update_effects 0x8003adc8
  void begin_action() override;       // [12] pc_begin_action 0x80037098
  int status_test(int word, u32 mask) override;  // [14] pc_has_effect 0x8003b8f4
  void set_effect_timer(int which, s16 ticks) override;  // [16] 0x8003bc48
  void accessory_proc(int item, int word, u32 mask, int chance) override;  // [18] pc_break_accessory 0x8003b93c
  void clear_status(int word, u32 mask) override;  // [19] 0x8003bb44
  void set_status(int word, u32 mask) override;    // [20] 0x8003bb60
  void status_recovered() override { busy = 0; }   // [21] 0x8003bb7c
  // The spell states (spell_units.cpp).
  int can_cast() override;            // [3] pc_can_cast 0x80037a80
  void release_spell() override;      // [11] pc_cast_spell 0x8003a088
  virtual void state_cast();          // [40] pc_state_cast 0x80039198 (state 0xe)
  virtual void state_spell_release(); // [41] pc_state_spell_release 0x80039ef4 (state 0xf)
  virtual void think();               // [22] pc_think 0x80035508
  virtual void model_default();       // [23] pc_model_default 0x80035168
  virtual void model_apply();         // [24] pc_model_apply 0x800351a0
  virtual void model_select(int n);   // [25] pc_model_select 0x8003522c
  virtual int animate(u32 flags);     // [26] pc_animate 0x8003532c
  virtual void state_approach();      // [27] pc_state_approach 0x80036410
  virtual void state_leap();          // [28] pc_state_leap 0x80036c74
  virtual void start_approach();      // [29] pc_start_approach 0x80036980
  virtual void state_attack();        // [30] pc_state_attack 0x80037458 (states 9 / 12)
  virtual void state_air_attack();    // [31] pc_state_air_attack 0x80037d08 (10 / 11)
  virtual void state_knockback();     // [32] pc_state_knockback 0x800377ac (3 / 4)
  virtual void state_victory();       // [33] pc_state_victory 0x8003785c
  virtual void state_flee();          // [34] pc_state_flee 0x800378c0
  virtual int killer_move_cost(int) { return 0; }  // [35] MP cost of killer move 0 / 1 (character module)
  virtual int move_mp_cost(int) { return 0; }       // [36] MP cost of killer move index (character module)
  virtual void killer_move_state(int) {}  // [37] (the character module's killer moves)
  // [39] pc_play_move_voice 0x8003ac70: the voice of a killer move (slot 43 with move * 2).
  virtual void play_move_voice(int move) { play_voice(move * 2); }
  virtual int forced_attack_choice() { return -1; }  // [45] pc_forced_attack_choice 0x8003bc40
  virtual void camera_follow_start(); // [38] pc_camera_follow_start 0x80037b9c
  virtual void spell_hook(int) {}     // [42] pc_spell_hook 0x8003bc38: after spell_cast (modules: voice lines)
  virtual void play_voice(int id);    // [43] pc_play_voice 0x8003ac38 (party_voice.cpp)
  void appear_update();               // 0x800362a8 pc_appear_update
};
// PartyUnit helpers.
void pc_record_counter_dec(PartyUnit* u, s16 n);   // 0x8003a1d0
void pc_voice_attack_shout(PartyUnit* u);          // 0x8003ad30
void pc_camera_follow_end(PartyUnit* u);           // 0x80037cb0
int pc_try_ambush_skill(PartyUnit* u);             // 0x80036f50
void pc_attack_animate(PartyUnit* u);              // 0x80037488
void pc_attack_hit_check(PartyUnit* u);            // 0x80037660
void pc_ai_update(PartyUnit* u);                   // 0x80037fb4
int unit_target_is_far(const Unit* u, const Unit* target);  // 0x80037b64
int unit_start_sidestep(Unit* u, Unit* from);      // 0x80053054 (from == kWallBlock: the arena edge)
void unit_update_status_effects(Unit* u);          // 0x80053240 (Unit [10])

// The party-data loader task (party_loader_create 0x80082bd8) and the end-of-battle reset.
Task* party_loader_create();
void party_reset();
// (Port, original_bugs.md B14) battle_run hands over the raw stage file (load-list entry 2)
// before it is unpacked: the party units' uninitialised +0x230 reads from it.
void party_note_stage_file(const std::vector<u8>& raw);
// Item record field (inventory_item_data 0x800691c4 on the #1983 table): field 0 u32,
// 4..0x12 s16, else u8; 0 for item 0.
s32 item_data_field(int item, int field);
void battle_separate_units();         // 0x80047fdc

void enemy_think(EnemyUnit* e);       // 0x8003cac0
void encounter_reset();               // the encounter's units and sprite packs (freed at the end)
int enemy_count_active(const std::array<EnemyUnit*, 8>& units);  // 0x8003e174

// The monster factory (0x80075ad8): constructs placement 'index' of monster 'type'.
void enemy_create_by_type(int index, int type);
// The formation loader (load list 7, 0x8007344c) and the stats reader (0x80076298).
void load_formation(std::span<const u8> pack);
const u8* enemy_read_stats(int index, const u8* record);

}  // namespace ss::game::battle
