#pragma once

#include <array>

#include "core/types.h"
#include "game/common/screen.h"

namespace ss::game {

// The startup sequence object (title 0x8003cc2c, 0x19030 bytes on the console).
struct TitleIntro {
  static constexpr int kMaxParticles = 0x800;

  RenderContext* render = nullptr;  // [0]
  u8* textures = nullptr;           // [1]: archive entry #10 while uploading
  s32 field_08 = 0;                 // [2]
  u8 skip_requested = 0;            // +0x0c
  s32 timer = 0;                    // [4]  frames to hold the current picture
  s32 state = 0;                    // [5]  -1 when finished
  s32 substate = 0;                 // [6]
  s32 brightness = 0;               // [7]  0..0x80
  s32 field_20 = 0, field_24 = 0;   // [8], [9]
  s32 particles_moving = 0;         // [10]
  s32 particle_count = 0;           // [11]

  // tri-Ace logo particles, positions in 12.20 fixed point (+0x30 onward).
  std::array<s32, kMaxParticles> alive{};      // +0x00030
  std::array<s32, kMaxParticles> pos_x{};      // +0x02030
  std::array<s32, kMaxParticles> pos_y{};      // +0x04030
  std::array<s32, kMaxParticles> target_x{};   // +0x06030
  std::array<s32, kMaxParticles> target_y{};   // +0x08030
  std::array<s32, kMaxParticles> vel_x{};      // +0x0a030
  std::array<s32, kMaxParticles> vel_y{};      // +0x0c030
  std::array<s32, kMaxParticles> trail_x{};    // +0x0e030
  std::array<s32, kMaxParticles> trail_y{};    // +0x10030
  std::array<u8, kMaxParticles> red{}, green{}, blue{};                      // +0x12030..
  std::array<u8, kMaxParticles> target_red{}, target_green{}, target_blue{}; // +0x13830..
  std::array<s32, kMaxParticles> accel_x{};    // +0x15030
  std::array<s32, kMaxParticles> accel_y{};    // +0x17030
};

TitleIntro* title_intro_create(TitleIntro* self);  // 0x8003cc2c
void title_intro_sequence(TitleIntro* self);        // 0x8003cd34
void title_logo_update(TitleIntro* self);           // 0x8003e54c

// 0x8003e958: a textured quad covering (x, y, w, h) of the screen with texels
// from (u, v), modulated by 'brightness'; 'semi' selects the semi-transparent code.
void set_textured_quad(psyq::POLY_FT4* p, int x, int y, int w, int h, int u, int v, u16 tpage, u16 clut,
                       u8 brightness, bool semi);

}  // namespace ss::game
