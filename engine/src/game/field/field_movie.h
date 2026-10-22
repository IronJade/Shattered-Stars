#pragma once

#include "core/types.h"
#include "runtime/libgpu.h"

namespace ss::game {
struct Movie;
}

// Embedded map code #3103 (map container section 5; entry 0xc1f, unpacked at 0x801e9000,
// code to 0x801ecabc): the field's copy of the movie player and the script ops around
// it. Maps 52, 106, 108, 448, 482, 548, 576, 591-594, 625, 657, 685 and 687 carry it.
//
// The player itself (0x801eb0e8-0x801ecabc: Movie, Prologue, the MDEC callback and
// libpress) is the title program's code word for word, so the port uses game::Movie /
// game::Prologue (game/title/movie.h) and their globals (0x801ecb7c-0x801ecbaa here).
// What is the field's own:
//   0x801e9000 compose   sprites from the field's ordering table blended into the
//                        24-bit movie frame in VRAM (the actors seen inside FMVs)
//   0x801ea1d0 to_15bit  the last 24-bit frame converted into a field draw buffer
//   0x801ea374 sequence  table B 0x6f: a full-screen cue-list sequence (Prologue)
//   0x801eaa30 start     table B 0x50: a movie over the field (24-bit) or, with the
//                        picture-in-picture hook (0x80076279), into a texture
//   0x801eac28 preload   table B 0x16 / 0x70: reads a sequence's cue list ahead
//   0x801ead88 pip_draw  the picture-in-picture quads (special commands 0xe / 0xf)
// and the movie branch of the field's main loop (0x80049bfc).
namespace ss::game::field {

struct Field;
struct Screen;
struct ScriptCtx;

game::Movie* field_sub_program();         // 0x800752c8: the movie playing (null: none)
s32 field_movie_frame_count();            // 0x801ecb90 (table B 0x53)
void field_movie_set_stop(int v);         // 0x800762ff: B 0x50 / 0x6f clear it, A 0x32 sets it (stops the movie)

// The main loop's sub-program branch (0x80049bfc, after field_update), run while
// field_sub_program() is set. True when the iteration goes on with the loop's usual
// tail (task_run_all, VSync, DrawOTag ...), false when it ends here.
bool field_movie_main_loop(Field* f);

void field_movie_compose(Screen* s);                    // 0x801e9000
void field_movie_to_15bit(const psyq::RECT& disp, int y);  // 0x801ea1d0 (unused middle arguments dropped)
void field_movie_pip_draw();                            // 0x801ead88
void field_movie_start(ScriptCtx* ctx);                 // 0x801eaa30 (fetches 2 arguments)
int field_movie_sequence(ScriptCtx* ctx);               // 0x801ea374 (reads the 8 fetched); 1 = reload the map
void field_movie_preload(ScriptCtx* ctx, u32 instr);    // 0x801eac28 (B 0x16 fetches 2, B 0x70 one)

}  // namespace ss::game::field
