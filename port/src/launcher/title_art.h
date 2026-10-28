#pragma once

#include <filesystem>
#include <optional>
#include <vector>

#include "core/types.h"

// The title screen's picture, read from the player's own Disc 1 for the setup program's
// header (port feature). Decoded in memory only: never written anywhere (no game assets
// in the repository or the package).
namespace ss::launcher {

struct Picture {
  int width = 0, height = 0;
  std::vector<u32> rgba;  // RGBA8888, red in the low byte, rows top to bottom
};

// The title background: archive entry 12 (0xC), block 0, the 640 x 480 picture the title
// screen uploads to VRAM (0, 0) with its palette (title_screen.cpp), "Star Ocean / THE
// SECOND STORY" over the nebula. Empty when the image isn't a readable disc 1.
std::optional<Picture> read_title_picture(const std::filesystem::path& disc1);

// The band of it that holds the logo and its TM, without the copyright line below.
inline constexpr int kLogoX = 20, kLogoY = 106, kLogoWidth = 600, kLogoHeight = 110;
Picture crop(const Picture& picture, int x, int y, int width, int height);

}  // namespace ss::launcher
