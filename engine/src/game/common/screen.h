#pragma once

#include <cstddef>

#include "core/types.h"
#include "runtime/libgpu.h"

// The screen manager shared by the title (0x8003ba74-0x8003c5b4) and field
// (0x8003c9d0-0x8003d4a4) programs: two draw/display
// buffers flipped every frame, plus up to twelve double-buffered primitive arenas
// and twelve double-buffered ordering tables.
namespace ss::game {

// A drawing target the program renders through: an arena and an ordering table
// (title 0x8003cc2c allocates one as a 16-byte object).
struct RenderContext {
  s32 unused = 0;
  s32 arena = -1;                 // +4
  s32 ot = -1;                    // +8
  psyq::P_TAG* ot_head = nullptr; // +0xc: current table after screen_clear_ot
};

void screen_init();                                                        // 0x8003ba74
// 0x8003baf0: frees buffers queued for deletion once the GPU is done with them (a
// frame-count list at 0x8004e174 / 0x8004e234). The port draws synchronously and
// frees immediately, so there is nothing to do.
inline void deferred_delete_update() {}
void screen_setup_buffer0(int x, int y, int w, int h, int top, int bottom, int left, int right);  // 0x8003bbc4
void screen_setup_buffer1(int x, int y, int w, int h, int top, int bottom, int left, int right);  // 0x8003bcb8
psyq::DRAWENV* screen_drawenv(int buffer);                                 // 0x8003be84
psyq::DISPENV* screen_dispenv(int buffer);                                 // 0x8003bea0
int screen_create_arena(size_t bytes);                                     // 0x8003bebc
int screen_create_ot(int entries);                                         // 0x8003bfbc
void screen_destroy_arena(int arena);                                      // 0x8003c07c
void screen_destroy_ot(int ot);                                            // 0x8003c0e0
void screen_flip();                                                        // 0x8003c3d8
psyq::P_TAG* screen_clear_ot(int ot);                                      // 0x8003c144
void screen_draw_ot(int ot);                                               // 0x8003c2d4
void screen_put_env();                                                     // 0x8003c374
void* screen_alloc(int arena, size_t bytes);                               // 0x8003c434
int screen_draw_y();                                                       // 0x8003c548
bool render_ctx_create_arena(RenderContext* ctx, size_t bytes);           // 0x8003c57c
bool render_ctx_create_ot(RenderContext* ctx, int entries);               // 0x8003c5b4

// Typed arena allocation. The original passes PS1 structure sizes (0x28 for a
// POLY_FT4); host structures are larger, so ported code allocates by type.
template <typename T>
T* screen_alloc_prims(int arena, size_t count) {
  return static_cast<T*>(screen_alloc(arena, sizeof(T) * count));
}

}  // namespace ss::game
