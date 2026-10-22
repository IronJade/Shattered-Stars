#pragma once

#include <vector>

#include "core/types.h"
#include "runtime/libgpu.h"

namespace ss::game {

class Loader;
struct Sound;

// The title program's movie player (0x8004f014-0x8004fa94; 0x50 bytes on the
// console, vtable 0x800509f8 at +0x4c). It streams an STR entry through a two-slot
// CD ring buffer, decodes each complete frame with libpress (DecDCTvlc on the CPU,
// then the MDEC) straight into the hidden display buffer in 16-pixel columns, and
// flips the display when a frame is finished. Frames are 320x240 and come at the
// drive's pace: ten sectors per frame at double speed, 15 frames a second.
//
// Use (as title_intro_sequence does):
//   Movie* m = movie_create(new Movie, 0, 0, 0x140, 0xF0, 1, 3);
//   movie_open(m, entry);
//   while (movie_update(m) == 1) movie_present(m);
//   delete m;
struct Movie {
  psyq::DISPENV disp[2]{};      // +0x00 / +0x14: [0] shows y + 240, [1] shows y
  Loader* loader = nullptr;     // +0x28: System + 0x10
  std::vector<u8> ring;         // +0x2c: CD ring buffer, two 0x5000-byte slots
  std::vector<u32> rl_buffers;  // +0x30: two 0x14000-byte buffers of MDEC run-length codes
  s32 last_slot = 0;            // +0x34: ring slot last looked at
  s32 frames_until_display = 0; // +0x38: SetDispMask(1) once this many frames were shown
  s32 rl_index = 0;             // +0x3c: run-length buffer the next frame is decoded into
  s32 rl_decoded = 0;           // +0x40: buffer holding the last VLC-decoded frame
  s32 rl_pending = 0;           // +0x44: that frame has not gone to the MDEC yet
  s16 dct_flags = 0;            // +0x48: ORed into DecDCTin's mode (bit 1: set the mask bit); always 0

  Movie() = default;
  Movie(const Movie&) = delete;
  Movie& operator=(const Movie&) = delete;
  virtual ~Movie();             // 0x8004f3ac (vtable +0x0c): stops the stream, frees the buffers
};

// 0x8004f204: the constructor. Display buffers at (x, y) and (x, y + 240); 'mode'
// 3 = 24-bit output, 2 = 15-bit (VRAM halfwords per two pixels).
Movie* movie_create(Movie* self, int x, int y, int w, int h, int interlace, int mode);
void movie_open(Movie* self, int entry);  // 0x8004f4dc: allocates the buffers and starts streaming 'entry'
// 0x8004f61c: decodes what the drive has delivered. 1 while the movie plays, 0 at
// its end (stream finished, nothing left to show) or if the drive door opened.
int movie_update(Movie* self);
void movie_present(Movie* self);          // 0x8004f9f4: shows a finished frame (waits for the vblank)
void movie_close(Movie* self);            // 0x8004f444: stops and frees the stream (unused by the game)
// 0x80050abc: frames decoded or skipped since movie_create. The intro starts the
// opening's music once it passes 9; the prologue's cues run on it.
s32 movie_frame_count();

// The player's globals (title 0x80050aa8-0x80050ad6). The field's embedded copy of
// the player (#3103, globals at 0x801ecb7c-0x801ecbaa) is the same code, and the
// field's main loop reads and clears some of them (game/field/field_movie.cpp).
struct MovieGlobals {
  s32 screen_cleared = 0;          // 0x80050aa8: both buffers were cleared before the first frame
  s32 slice_half = 0;              // 0x80050aac: half of slice_buffers the MDEC fills next
  psyq::RECT slice_rect{};         // 0x80050ab0: VRAM column the finished slice goes to
  std::vector<u32> slice_buffers;  // 0x80050ab8: two MDEC output slices (16 x 240 pixels each)
  s32 frame_count = 0;             // 0x80050abc: frames decoded or skipped
  s32 frame_ready = 0;             // 0x80050ac0: a frame is finished and waits for movie_present
  s32 decoding = 0;                // 0x80050ac4: a frame is on its way through the MDEC
  s32 draw_buffer = 0;             // 0x80050ac8: frames go to y (0) or y + 240 (1)
  s16 x = 0, y = 0, width = 0, height = 0;  // 0x80050acc-0x80050ad2
  s16 mode = 0;                    // 0x80050ad4: 3 = 24-bit, 2 = 15-bit
  // 0x80050ad6: decode whole frames to RAM instead of VRAM. movie_create clears it and
  // nothing sets it, so its paths are not ported.
  u8 frames_in_ram = 0;
  // Host: console time one pass of the caller's loop takes when the caller polls the
  // player from its own loop (the field: ~1/28 vblank, measured in PCSX-Redux). Each
  // movie_update then spends it instead of idling to the next event. 0 (movie_create)
  // for callers that only wait on the player, as the title does.
  double loop_vblanks = 0;
};
MovieGlobals& movie_globals();
// frame_ready as the console's code sees it. The host decodes a frame at once and sets
// the flag then; the console's MDEC sets it only when the frame is done, which
// movie_present waits for. Code outside the player tests this instead of the flag.
bool movie_frame_ready();
// Host: the console spends 'vblanks' of CPU time (code outside the player whose cost
// matters to the movie's timing, such as the field's compose).
void movie_console_spend(double vblanks);

// The scripted movie player title_program_main uses after New Game (0x8004fa98-
// 0x8004ffb4; 0x70 bytes, vtable 0x80050a20): a Movie driven by a cue list
// (archive entries #4504-#4507, see docs/catalog/formats.md "Prologue cue list")
// that opens STR movies and starts music, sound clips, controller vibration and a
// fade-out at given times. The clock is the movie's frame count while a movie
// plays and the vblank count otherwise.
//
// Use (title_program_main):
//   Prologue* p = prologue_create(new Prologue);
//   prologue_load_sound(p, sound_entry);          // g_prologue_data; freed
//   prologue_set_script(p, script.data());        // entry #4504 ... loaded whole
//   pad_clear(pad, 0);
//   int r;
//   while ((r = prologue_update(p, 1)) != 1 && r != 2)
//     if (p->has_movie) movie_present(p);
//   prologue_restart(p); prologue_set_script(p, next.data()); ... (same object, next part)
//   delete p;
struct Prologue : Movie {
  // The 12-byte vblank handler (vtable 0x80050a38) the pad module's handler list
  // ticks: counts vblanks up to 4 between two prologue_update calls (0x8004ff78).
  struct VblankCounter {
    s32 count = 0;   // +8
    u64 seen = 0;    // host: runtime vblank the count was last brought up to date at
  };

  const u8* script = nullptr;  // +0x50: the cue list's entry (sound clips are offsets into it)
  std::vector<u8> sequence;    // +0x54: music from prologue_load_sound (0x8000 bytes)
  u8 faded_out = 0;            // +0x58: a fade cue ran, so the destructor need not fade
  u8 has_movie = 0;            // +0x59: a movie cue ran; movie_present has frames to show
  u8 keep_music = 0;           // +0x5a: never set; the destructor stops the music unless set
  u32 clock = 0;               // +0x5c: vblanks counted while no movie plays
  u32 clock_step = 0;          // +0x60: vblanks since the previous update (at most 4)
  const u8* cue = nullptr;     // +0x64: next cue
  VblankCounter* counter = nullptr;  // +0x68
  Sound* sound = nullptr;      // +0x6c: System + 0x5aa4

  ~Prologue() override;        // 0x8004fb64 (vtable +0x0c): fades and stops the sound, then ~Movie
};

Prologue* prologue_create(Prologue* self);                           // 0x8004fa98
void prologue_load_sound(Prologue* self, std::vector<u8>& sound_entry);  // 0x8004fc7c: bank + music; frees the entry
void prologue_restart(Prologue* self);                               // 0x8004fb54: clock to 0, no movie
void prologue_set_script(Prologue* self, const u8* script);          // 0x8004ffa0: script and cue pointer
// 0x8004fd08 (vtable +0x14): runs the cues that are due and steps the movie.
// 0 = running, 1 = finished (end cue), 2 = cancelled (Start or Circle without L1,
// when 'allow_cancel').
int prologue_update(Prologue* self, int allow_cancel);

}  // namespace ss::game
