#pragma once

#include "game/title/intro.h"

namespace ss::game {

// Values title_screen returns (title_program_main stores it in config + 0x17f).
enum TitleChoice : int {
  kTitleNewGame = 0,
  kTitleContinue = 1,
  kTitleVoiceCollection = 2,
  kTitleAttractMovie = 3,  // idle timeout: the opening movie played
  // 4 + n: movie viewer entry n (when the viewer list is unlocked)
};

// title 0x8003d3c8: the 640x480 title screen with its menu. 'self' is the intro
// object; field_20 nonzero shows the movie-viewer list, field_24 is its cursor.
int title_screen(TitleIntro* self);

}  // namespace ss::game
