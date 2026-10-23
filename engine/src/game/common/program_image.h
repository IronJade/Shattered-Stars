#pragma once

#include <span>

#include "core/types.h"

// The unpacked image of a whole program (archive entry #1 title, #3100 field, ...),
// as boot_run_main_program would place it at 0x8002f810. Ported code reads the
// original's constant tables (strings, layouts, coefficients) from it at their
// original addresses instead of copying game data into the source.
namespace ss::game {

constexpr u32 kProgramBase = 0x8002F810;

// Byte at 'address' of program 'entry', or null outside the image (or with no disc).
const u8* program_data(int entry, u32 address);
// The same for an image loaded at 'base' (code overlays: #3101 / #3102 at 0x8007e000).
const u8* program_data_at(int entry, u32 base, u32 address);

// Data a release lays out its own way, read in that release's order: a script or list whose
// length differs between releases (the credits' card script), where only its start lies at
// a place the address map knows. The image's bytes from where NA's 'address' lies to the
// image's end (on NA: from 'address'); empty when the start has no counterpart. The reader
// walks it with its own bounds and must stop at the span's end.
std::span<const u8> program_run(int entry, u32 base, u32 address);

}  // namespace ss::game
