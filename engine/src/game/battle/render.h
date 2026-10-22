#pragma once

#include "core/types.h"
#include "runtime/libgte.h"

// The battle's 3-D render core: the view (camera_compute_view 0x800771d8,
// camera_update_matrices 0x800473f0), the stage lights (lights_build 0x80076ce0) and
// the model matrix (obj_set_matrix 0x80077620), with the math helpers the battle
// program carries its own copies of (sin4096 0x80063cf4, RotMatrix 0x80063e1c,
// rot_matrix_y 0x8006400c, rot_matrix_axis 0x80064078, SquareRoot0 0x8006a720,
// ratan2 0x8006add0). #1984 block 0 holds the render core at run time; the port's
// copy is native.
namespace ss::game::battle {

// The view and lighting globals (0x8006ef9c..0x8006f540).
struct ViewState {
  // Stage lights (load_stage ops 7, 8, 14).
  s16 light_dir[3][4] = {};       // 0x8006ef9c g_light_dir (stride 8)
  s16 light_colour[3][4] = {};    // 0x8006efb4 g_light_colour (rgb << 2 or << 4)
  u8 light_mask = 0;              // 0x8006efee g_light_mask
  u8 ambient[3] = {};             // 0x8006efef g_ambient_rgb
  u8 fog_rgb[3] = {};             // 0x8006eff3 g_stage_op5_rgb (op 5)
  u8 fog_on = 0;                  // 0x8006eff6 (op 5 present)
  u8 clear_rgb[3] = {};           // 0x8006eff7 g_clear_rgb (op 6)
  u8 clear_rgb_alt[3] = {};       // 0x8006effa g_clear_rgb_alt (op 16)
  u8 clear_alt_on = 0;            // 0x8006effd
  s32 stage_orient[3] = {};       // 0x8006efd4 (the catalogue's g_camera_eye): stage model orientation

  u8 view_mode = 0;               // 0x8006f005 g_view_mode: 0 angles, 1 look-at
  s32 view_eye[3] = {};           // 0x8006f008 g_view_eye: render eye (last frame's camera eye)
  s32 view_target[3] = {};        // 0x8006f014: look-at point (mode 1) or angles (mode 0)
  s32 cam_eye[3] = {};            // 0x8006f020 g_cam_eye: the camera object's eye
  s32 cam_lookat[3] = {};         // 0x8006f02c g_cam_lookat
  s32 zoom = 0x1000;              // 0x8006f038 g_zoom
  s16 roll_input = 0;             // 0x8006f03c: roll for look-at mode
  s16 stage_colour[3] = {0x80, 0x80, 0x80};  // 0x8006f03e g_stage_colour (0x80 = 1.0)
  s16 stage_colour_target[3] = {0x80, 0x80, 0x80};  // 0x8006f04a
  psyq::MATRIX view_matrix{};     // 0x8006f050 g_view_matrix
  s16 view_angles[3] = {};        // 0x8006f070: pitch, yaw, roll
  s16 light_rotation[3] = {};     // 0x8006f078 g_light_rotation
  s16 pitch = 0;                  // 0x8006f080 g_bob_phase (pitch copy)
  s16 yaw = 0;                    // 0x8006f082
  s16 camera_roll = 0;            // 0x8006f084 g_camera_roll
  s16 fog_near = 0;               // 0x8006f086 g_fog_near
  u16 cull_threshold = 0;         // 0x8006f08a g_cull_threshold (model_render NCLIP limit)
  psyq::MATRIX render_view{};     // 0x8006f08c g_render_view_matrix (rotation part)
  s32 shake_x = 0;                // 0x8006f53c g_shake_offset
  s32 shake_y = 0;                // 0x8006f540

  // lights_build output.
  psyq::MATRIX colour_matrix{};   // 0x8006f4bc g_colour_matrix (LCM)
  psyq::MATRIX light_matrix{};    // 0x8006f4dc (LLM)
  psyq::MATRIX unlit_colour{};    // 0x8006f4fc: diagonal stage colour << 5
  psyq::MATRIX unlit_light{};     // 0x8006f51c: m[0][1] = 0x1000 (light straight down the normal (0, 1, 0))
};
ViewState& view();

// Math (the battle's own copies).
s32 bsin(u32 angle);                                          // 0x80063cf4 sin4096
s32 bcos(u32 angle);                                          // 0x80063d88 cos4096
void rot_matrix(const s16 angles[3], psyq::MATRIX* m);        // 0x80063e1c RotMatrix (C version)
void rot_matrix_y(s32 angle, psyq::MATRIX* m);                // 0x8006400c
void rot_matrix_axis(const s32 axis[3], s32 angle, psyq::MATRIX* m);  // 0x80064078
s32 square_root0(s32 a);                                      // 0x8006a720 SquareRoot0
s32 ratan2(s32 y, s32 x);                                     // 0x8006add0
void set_fog_near(s32 a, s32 h);                              // 0x8006a620 SetFogNear

void camera_reset();              // 0x80076be8 (#1984.0): GTE H / offset / far colour, view reset
void camera_compute_view();       // 0x800771d8 (#1984.0)
void camera_update_matrices();    // 0x800473f0
void lights_build(const s32 orient[3]);                         // 0x80076ce0 (#1984.0)
void obj_set_matrix(const s32 pos[3], s32 orient[3], const s32 scale[3]);  // 0x80077620 (#1984.0)
void gte_set_zoom();              // 0x80078350 (#1984.0)
void gte_set_back_colour();       // 0x8007838c (#1984.0)
void gte_setup();                 // 0x8006a698 new_game_gte_setup

}  // namespace ss::game::battle
