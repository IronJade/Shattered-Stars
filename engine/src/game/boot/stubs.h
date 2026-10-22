#pragma once

#include "core/types.h"
#include "game/boot/sound.h"      // the sound driver (ported; no longer a stub)
#include "game/boot/system.h"
#include "game/common/memcard.h"  // the memory card manager (ported; no longer a stub)

// Subsystems the title program calls that are not ported yet. Each logs once
// and behaves as "finished" so the original control flow continues; the
// checklist (docs/CHECKLIST.md) tracks replacing them. Nothing is left here today:
// the sound calls moved to game/boot/sound.h and the memory card to
// game/common/memcard.h; callers keep including this header.
namespace ss::game {}  // namespace ss::game
