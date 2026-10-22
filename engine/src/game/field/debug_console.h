#pragma once

#include <string>

// Debug console commands for the field (port feature; `--debug`): typed into the
// terminal, run on the game thread between frames. Installed by field_entry while the
// field program runs. `help` lists them.
namespace ss::game::field {

void debug_console_install();
void debug_console_command(const std::string& line);
std::string debug_console_status();

}  // namespace ss::game::field
