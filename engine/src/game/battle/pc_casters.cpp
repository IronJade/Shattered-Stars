// The small party-character modules: Rena (#2135), Celine (#2137), Leon (#2141) and Noel
// (#2157). Their PartyUnit subclasses only add a voice line to certain spells (slot 42,
// the hook pc_cast_spell calls after spell_cast); Celine's attacks cast her weapon's
// spells and Leon has a jump attack that throws a projectile and a victory pose of his own.
#include <memory>

#include "game/battle/battle.h"
#include "game/battle/bsfx.h"
#include "game/battle/enemy_support.h"
#include "game/battle/killer.h"
#include "game/battle/pc_modules.h"
#include "game/battle/shot.h"
#include "game/boot/rng.h"

namespace ss::game::battle {
namespace {

// Slot 42 of each: the spell's voice line (slot 43 pc_play_voice) for the listed spells.
template <size_t N>
void spell_voice(PartyUnit* u, int spell, const int (&spells)[N]) {
  for (size_t i = 0; i < N; ++i) {
    if (spells[i] == spell) {
      u->play_voice(static_cast<int>(i));
      return;
    }
  }
}

// #2135 (vtable 0x801ee158; slots 1, 42, 46).
struct RenaUnit : PartyUnit {
  RenaUnit() {  // 0x801ee028
    v2d8 = 1;
    v2da = 1;
    clut_row_check = 0x11;
  }
  void spell_hook(int id) override {  // [42] 0x801ee094
    static const int kSpells[] = {0x2F, 0x30, 0x35, 0x3C};
    spell_voice(this, id, kSpells);
  }
};

// #2137 (vtable 0x801ee278; slots 1, 30, 42, 46).
struct CelineUnit : PartyUnit {
  CelineUnit() {  // 0x801ee028
    v2d8 = 1;
    v2da = 1;
  }
  void spell_hook(int id) override {  // [42] 0x801ee08c
    static const int kSpells[] = {1, 8, 0xD};
    spell_voice(this, id, kSpells);
  }
  // [30] 0x801ee114: the normal attack; with a spell-casting weapon status (2: 0x100000,
  // 0x200000, 0x400000) the swing casts spell 1, 0x1e or 0x2b once instead of hitting.
  void state_attack() override {
    if (frame == 0) counters[0] = 0;
    pc_attack_animate(this);
    if (bg().flags != 0) return;
    if ((box_mask & 0xC) == 0) return;
    int cast = 0;
    if (status_test(2, 0x100000) != 0) cast = 1;
    else if (status_test(2, 0x200000) != 0) cast = 0x1E;
    else if (status_test(2, 0x400000) != 0) cast = 0x2B;
    if (cast == 0) {
      pc_attack_hit_check(this);
      return;
    }
    if (counters[0] != 0) return;
    counters[0] = 1;
    spell_cast(cast, this, target, 0);
  }
};

// #2141 (vtable 0x801ee3b4; slots 1, 31, 33, 42, 46).
struct LeonUnit : PartyUnit {
  LeonUnit() {  // 0x801ee028
    v2d8 = 1;
    v2da = 1;
    clut_row_check = 0x11;
  }
  void spell_hook(int id) override {  // [42] 0x801ee094
    static const int kSpells[] = {4, 0x1E, 0x2B};
    spell_voice(this, id, kSpells);
  }
  // [31] 0x801ee11c: the jump attacks (states 10 / 11, anim state + 2): state 11 hits
  // with power 0x100; state 10 throws a projectile at the target past frame 5 (once).
  void state_air_attack() override {
    BattleGlobals& g = bg();
    loop_limit = 1;
    layer = 2;
    anim = static_cast<u16>(action + 2);
    if (static_cast<u16>(animate(0)) == 0xFFFF) {
      action_kind = 0;
      begin_action();
    }
    attack_recovery = 0;
    if (g.flags == 0 && action == 0xB) pc_attack_hit(this, 0x100, 0);
    if (frame < 5) counters[1] = 0;
    if (g.flags != 0 || counters[1] != 0 || frame < 5 || action != 0xA) return;
    counters[1] = 1;
    bsfx_play_at(0x34, this);
    ShotDesc* d = shot_desc_init(0x12);
    d->speed = 0x20;
    d->life = 0x24;
    d->flags = 0x100006;
    d->target = target;
    const s32 from[3] = {pos[0] + (0 < facing[0] ? 0xE0000 : static_cast<s32>(0xFFF20000)),
                         pos[1] - ((size[1] * 11) << 12), pos[2]};
    if (Shot* s = shot_spawn_projectile(this, from, d, this)) s->roll = static_cast<s16>(0 <= facing[0] ? 0x155 : -0x155);
  }
  // [33] 0x801ee2fc: the victory pose (anim 0x11 or 8 at even odds).
  void state_victory() override {
    if (counters[0] == 0) {
      anim = static_cast<u16>((static_cast<u32>(rng_next()) & 0x7FFF) < 0x4000 ? 0x11 : 8);
      counters[0] = 1;
    }
    model_default();
    animate(0);
  }
};

// #2157 (vtable 0x801ee164; slots 1, 42, 46).
struct NoelUnit : PartyUnit {
  NoelUnit() {  // 0x801ee028
    v2d8 = 1;
    v2da = 1;
  }
  void spell_hook(int id) override {  // [42] 0x801ee08c
    static const int kSpells[] = {8, 0x17, 0x2F, 0x30, 0x3C};
    spell_voice(this, id, kSpells);
  }
};

}  // namespace

std::unique_ptr<PartyUnit> pc_rena_create() { return std::make_unique<RenaUnit>(); }
std::unique_ptr<PartyUnit> pc_celine_create() { return std::make_unique<CelineUnit>(); }
std::unique_ptr<PartyUnit> pc_leon_create() { return std::make_unique<LeonUnit>(); }
std::unique_ptr<PartyUnit> pc_noel_create() { return std::make_unique<NoelUnit>(); }

}  // namespace ss::game::battle
