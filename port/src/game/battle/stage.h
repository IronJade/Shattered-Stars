#pragma once

#include <array>
#include <span>
#include <vector>

#include "core/types.h"
#include "game/common/screen.h"

// Battle stages (archive entry 0x543 + stage id, #1347-#1404): the command list
// load_stage (0x80072270) reads, the stage model drawn every frame
// (stage_draw_and_animate 0x800471a4 with model_render 0x8007843c), the texture
// animations, the arena bounds, obstacle boxes and the ground height grid.
// Format: docs/port/notes/battle.md §6.1.
namespace ss::game::battle {

// The load context (battle_run's stack RECT, stage_load_ctx_init 0x80072164).
struct StageLoadCtx {
  s16 clut_x = 0;        // +0
  s16 clut_y = 0x1E0;    // +2: next CLUT row (ops 1, 3, 12)
  s16 panorama_x = 0;    // +4 (op 3)
  s16 panorama_y = 0;    // +6
  s32 light = 0;         // +8: next directional light (ops 8, 14)
  s32 anim_frames = 0;   // +0xc: texture animation frames so far (op 10)
  s32 anim_groups = 0;   // +0x10: texture animation groups so far
};

struct StageData {
  std::array<u16, 16> tex_clut{};          // 0x8006ef48 g_stage_tex_clut
  u16 tex_count = 0;                       // 0x8006ef40 g_stage_tex_count
  std::array<u16, 16> model_tpage{};       // 0x8006ef68 g_stage_model_clut: tpage of the stage textures (5)
  std::vector<std::vector<u8>> blocks;     // 0x8006ef88 g_stage_blocks (op 2); [0] is the stage model
  u16 panorama_clut = 0;                   // g_panorama_clut (op 3)
  u8 has_panorama = 0;                     // g_stage_has_panorama
  u8 panorama_mode = 0;                    // 0x8006f6cc (op 4)
  u16 panorama_a = 0, panorama_b = 0, panorama_c = 0;  // 0x8006f6a8 / 0x8006f6a4 / 0x8006f6a6
  std::vector<std::array<u16, 4>> panorama_rects;      // 0x8006f6ac (count 0x8006f6cd)
  u8 has_lights = 0;                       // g_stage_has_lights
  std::vector<u8> height_grid;             // 0x8006efe4 g_stage_height_grid (op 9)
  u8 height_grid_on = 0;                   // 0x8006efff g_stage_height_grid_on
  // Arena rectangle (op 6): g_stage_bounds 0x8006d4fc (x min), g_arena_z_min 0x8006d500,
  // 0x8006d504 (x max), 0x8006d508 (z max); 20.12.
  s32 bounds[4] = {};
  // Texture animations (op 10).
  std::array<psyq::RECT, 32> anim_frames{};  // 0x8006f544 g_texanim_frames
  std::array<s16, 32> anim_durations{};      // 0x8006f644 g_texanim_durations
  std::array<s32, 8> anim_dest{};            // 0x8006d50c g_texanim_dest (x, y per group)
  std::array<u8, 4> anim_start{0xFF, 0xFF, 0xFF, 0xFF};  // 0x8006f688 g_texanim_start
  std::array<u8, 4> anim_frame{};            // 0x8006f68c g_texanim_frame
  std::array<s8, 4> anim_end{};              // 0x8006f684 g_texanim_end
  std::array<s16, 4> anim_timer{};           // 0x8006f690 g_texanim_timer
  // Obstacles (ops 11, 17): boxes min 0x8006e35c / max 0x8006e4dc (20.12), kind 0x8006fb40.
  struct Obstacle {
    s32 min[3];
    s32 max[3];
    u8 kind;
  };
  std::array<Obstacle, 24> obstacles{};
  u8 obstacle_count = 0;                     // 0x8006fb1a g_obstacle_count
  // Stage sprite set (op 13).
  std::vector<u8> sprite_layout, sprite_anims, sprite_frames, sprite_sheet;  // 0x8006f934..0x8006f940
  u8 sprites_present = 0;                    // g_stage_sprites_present
  // Lens flare (op 15).
  u8 flare_on = 0;                           // 0x8006f6a1
  s32 flare_pos[3] = {};                     // 0x8006d52c g_lens_flare_src
  u16 flare_size = 0;                        // 0x8006f69c
  u32 flare_colour = 0;                      // 0x8006f698
  u8 flare_b = 0;                            // 0x8006f6a0
  s16 flare_cos = 0;                         // 0x8006f69e
};
StageData& stage();

void stage_load_ctx_init(StageLoadCtx* ctx);                // 0x80072164
// 0x80072270: returns the offset after the list's op 0 (0 when it does not end).
size_t load_stage(StageLoadCtx* ctx, std::span<const u8> list);
void stage_draw_and_animate();                              // 0x800471a4
s32 stage_ground_height(s32 x, s32 z);                      // 0x800441b0 (x, z in whole units; 20.12 result)

// 0x8007843c (#1984.0): draws a battle model (u32 part count; per part a u32 then 20
// counted primitive sections) into 'pool' at OT 0x610 + ot_bias - otz / 8. 'clut' and
// 'tpage' are added to the textured primitives' own CLUT / tpage fields; 'fog' picks
// the depth-cued colour commands. Uses the GTE matrices obj_set_matrix loaded.
void model_render(RenderContext* pool, const u8* model, u16 clut, u16 tpage, bool fog, s32 ot_bias);

// The PS1 bytes of primitive area pool A has used this frame (model_render's budget is
// screen_arena_free_bytes - 0x100 on the console).
s32& pool_a_bytes_used();
bool arena_has_room(const RenderContext* pool, s32 ps1_bytes);

}  // namespace ss::game::battle
