#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/types.h"

// Archipelago (port feature) [agent skillsanity2]: the join level (open world and hub) and
// skillsanity (docs/apworld/seed_format.md "The join level", "Skillsanity"; docs/handoff/skillsanity.md).
//
// The join level: in open world and hub mode every new member (a join: the game's
// party_slot_init) takes the median level of the party, up through the game's level-up routine,
// down by the growth table's mean gains (floored at the heroes' level-1 values). Story mode, no
// seed: the original join levels.
//
// Skillsanity (options.skillsanity): the guild sets' purchases are checks and the skills come as
// items (skills_only / insanity); the killer moves' and spells' learn points are checks and the
// moves come as items, each member knowing only the first from the start (special_only /
// insanity). Without the option (or a seed) nothing here changes the game.
namespace ss::game::ap {

struct Seed;
struct ItemDef;

// ---- The join level ------------------------------------------------------------------------

// The level a member joining into 'party' (8 slots of 0x60 bytes) at 'slot' takes: the median
// level (slot +0x28) of the other members (ids 1-12; a member set aside has a negative id), an
// even count's middle two averaged and rounded down, clamped to 1-255. 0: nobody else.
int join_target_level(const u8* party, int slot);

// The growth table's row of a member (#1981 0x80087f94 + (id - 1) * 0xc): a level-up adds
// rng_range(range) + base to STR (+0x2a), INT (+0x42/+0x44), DEX (+0x3c/+0x3e), CON
// (+0x30/+0x32), slot +0x48/+0x4a and record +6/+8 (battle/result.cpp add_attr).
struct Growth {
  u8 range[6] = {};
  u8 base[6] = {};
};
Growth growth_row(int character);  // zeros without the disc

// Down: the mean gains of the levels from 'target' to the member's own taken off its stats
// (HP L*3/2 + 32 below level 50 and 94 from it, MP 5, the six attributes base + (range - 1) / 2),
// each never below the lower of Claude's and Rena's level-1 values (HP 100, MP 20, STR 5, INT 0,
// DEX 10, CON 2, slot +0x48 20, record +6 10) and never raised by it; the level and the EXP the
// target level starts at. SP, equipment and moves are left alone.
void scale_down_stats(u8* slot, u8* rec, int target, const Growth& g);

// The open world's / hub's join: called after the game's party_slot_init of a new member in
// party slot 'slot' (field vm_svc_party_slot_init; special 0xd's re-init of Dias).
void on_member_init(int slot);
// The same for a new member built straight into a bench place (its slot and record; ap.cpp's
// join to the bench) [partybuild agent]: the level of the median over all 8 slots (B6).
void on_bench_member_init(u8* slot, u8* rec);

// ---- Skillsanity -----------------------------------------------------------------------------

void skills_index(const Seed& seed);  // a seed was loaded (ap.cpp index_seed)
bool skills_shuffled();               // skills_only / insanity
bool specials_shuffled();             // special_only / insanity

// skill_book_allowed under skills_shuffled: the skill item was received (no guild set opens it).
bool skill_received(int skill);

// A learn point: true when the seed lists it as a location (it is checked once, and the move is
// not taught; received moves are known anyway). character 1-12; index: record +0x3c + index.
bool on_learn_point(int character, int index);
// A learn point that teaches by an item (464-469, character_flag): -1 not a location (teach as
// the original), 0 checked already (no effect, the item kept), 1 not checked yet; and its check.
int learn_point_state(int character, int index);
void on_item_learn_point(int character, int index);
// Special 0x12, the Cave of Trials level 11 prize (Float): true when it is the seed's location.
bool on_float_prize();
// The guild sets' flags 700-711 turning on (the purchase): their checks.
void on_guild_flag(int flag);

// A received skill or move item (ap.cpp give): kept in the sidecar; a move is known at once when
// its member is in the party (any slot) or on the bench [partybuild agent], else from their join.
void receive_skill(int skill);
void receive_move(int character, int index);

// A shop slot that sells this world's own skill / move item as itself (skillsanity_shop_share: the
// seed's `price`) while its check is open: the list's name ("Perseverance", "Claude: Air Slash")
// and the price. false: an ordinary entry (bought as the game's item).
bool shop_ap_item(int shop, int index, int item, std::string* name, s32* price);

// New Game's first free play: the heroes' moves (special_only / insanity: the first move only).
void skills_new_game();

// The record's move index of a fighter's / caster's first learn-table move, -1 none.
int first_move(int character);

}  // namespace ss::game::ap
