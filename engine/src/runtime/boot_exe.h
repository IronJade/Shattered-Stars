#pragma once

#include <string>

#include "core/types.h"

namespace ss::disc {
class DiscImage;
}

// The resident boot executable (SCUS_944.21 / .22, a PS-X EXE loaded at 0x80010000),
// read from the disc's ISO 9660 file system the way the BIOS finds it (SYSTEM.CNF's
// BOOT line). Ported code reads constant tables from it at their original addresses
// (the sound driver's pitch table, libspu's reverb presets) instead of copying the
// game's data into the source.
namespace ss::runtime {

// Loads the executable named by SYSTEM.CNF. Returns false (with 'error') if the disc
// has no such file; boot_exe_data then returns null.
bool boot_exe_open(disc::DiscImage& disc, std::string* error = nullptr);

// Byte at 'address' of the loaded image, or null outside it / with no executable.
const u8* boot_exe_data(u32 address);

}  // namespace ss::runtime
