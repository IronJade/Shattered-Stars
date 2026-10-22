#pragma once

#include "core/types.h"

// The resident popup windows of the spells and skills (0x80062844-0x80065388): the spell
// clash banner, the battle-skill notice and the enemy scan. parts/p5.md "Windows".
namespace ss::game::battle {

struct Unit;

// 0x80062844: the clash banner (bottom right; both casts' side and element, the result
// blinking) when the second SpellCast starts; it closes itself once slot 0 is empty.
void spell_clash_window_open();
// 0x800642f4: the skill notice (the controlled member's skill activations: a chain count
// and up to four skill names sliding in from the right, string 0x1f1f + skill).
void skill_notice_show(int skill);
// 0x80064c6c: the scan of an enemy (name, HP, MP, then its weak / strong elements, sliding
// in from the left); four more wait in a queue.
void scan_window_show(Unit* enemy);
// Battle end: the open windows are freed.
void spell_windows_reset();

}  // namespace ss::game::battle
