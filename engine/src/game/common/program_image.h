#pragma once

#include "core/types.h"

// The unpacked image of a whole program (archive entry #1 title, #3100 field, ...),
// as boot_run_main_program would place it at 0x8002f810. Ported code reads the
// original's constant tables (strings, layouts, coefficients) from it at their
// original addresses instead of copying game data into the source.
namespace ss::game {

constexpr u32 kProgramBase = 0x8002F810;

// Byte at 'address' of program 'entry', or null outside the image (or with no disc).
const u8* program_data(int entry, u32 address);

}  // namespace ss::game
