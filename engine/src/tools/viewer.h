#pragma once

#include <filesystem>

#include "disc/archive.h"

namespace ss::tools {

// Interactive texture viewer window; closes after max_frames when that is nonzero.
int run_viewer(disc::Archive& archive, int entry, int max_frames);
// Writes an entry's textures (and the VRAM they fill) as PNG files.
int dump_textures(disc::Archive& archive, int entry, const std::filesystem::path& dir);

}  // namespace ss::tools
