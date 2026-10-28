// The party members' own voices (#2129 0x8003ac38): effects of each member's voice bank,
// which the field's voice queue loaded into the sound driver's bank slot of the member's
// party slot before the battle.

#include "game/battle/battle.h"
#include "game/battle/bsfx.h"
#include "game/battle/party_talk.h"
#include "game/battle/unit.h"
#include "game/boot/sound.h"

namespace ss::game::battle {

// 0x8003ac38 pc_play_voice [43]: effect 'id' of the bank in slot +0x125 (the member's party
// slot), positioned on the member: 10-13 the attack shouts, 0xe-0x13 hurt, killer moves 2 *
// their index.
void PartyUnit::play_voice(int id) {
  BattleGlobals& g = bg();
  // (port) A battle started on its own (--battle) has none of the field's voice banks.
  if (g.sound == nullptr || slot > 3 || g.sound->banks[slot].index == nullptr) return;
  talk_trace("voice", this, static_cast<u32>(id));  // (SS_TALK_TRACE: Redux's pc_play_voice hits)
  bsfx_play_at(static_cast<int>((static_cast<u32>(slot) + 1) << 24) + id, this);
}

}  // namespace ss::game::battle
