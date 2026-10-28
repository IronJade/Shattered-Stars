#pragma once

// The "insert disc" screen (field 0x8006f958 disc_change_screen; the title's copy
// 0x8003c5ec): 640x480 interlaced 24-bit, a random background (#0x12-0x14) and the prompt
// pack #0x11. The console waits on the CD drive (lid opened and closed, then the new
// disc's table checked with loader_is_disc) and shows a status line. The host has no lid:
// the action button or Start stands for closing it (the configured images are tried with
// loader_insert_disc), and Select continues with the disc in the drive (a port addition:
// both discs carry the same field and battle data).
namespace ss::game {

void disc_change_screen(int disc);
// True once the player chose to carry on without disc 'disc' (Select): later disc checks
// for it do not ask again.
bool disc_screen_declined(int disc);

}  // namespace ss::game
