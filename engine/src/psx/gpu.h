#pragma once

#include <span>
#include <vector>

#include "core/types.h"

// The PS1 GPU, reproduced in software: 1024x512 16-bit VRAM and the GP0 drawing
// commands with the hardware's rules (top-left fill convention, texture pages and
// CLUTs, the four semi-transparency modes, mask bit, dithering, draw area/offset).
// Ported game code reaches it through PsyQ-shaped helpers built on gp0(), so a
// command list can be replayed exactly as the console would draw it.
// Reference: psx-spx "GPU" chapter.
namespace ss::psx {

constexpr int kVramWidth = 1024;
constexpr int kVramHeight = 512;

struct Rect {
  s16 x = 0, y = 0, w = 0, h = 0;
};

struct DisplayMode {
  int x = 0, y = 0;            // top-left of the displayed VRAM area
  int width = 320, height = 240;
  // Vertical display range (DISPENV screen.y / screen.h, GP1 07): the TV shows 'lines'
  // VRAM rows from y, 'top' lines below the default start; the rest is border.
  int top = 0, lines = 0;      // lines 0 = height
  bool color24 = false;        // 24-bit display (movies)
  bool enabled = false;
};

class Gpu {
 public:
  Gpu();

  // One complete GP0 command.
  void gp0(std::span<const u32> packet);
  // A run of GP0 words holding any number of commands, as one ordering-table
  // packet does (DR_MODE, for example, carries two).
  void gp0_stream(std::span<const u32> words);
  // Number of words in the GP0 command starting with 'first', or 0 when the
  // length depends on following data (polylines, CPU->VRAM transfers).
  static int gp0_packet_words(u32 first);

  // PsyQ LoadImage / StoreImage / MoveImage / ClearImage equivalents.
  void load_image(const Rect& r, const u16* pixels);
  void store_image(const Rect& r, u16* pixels) const;
  void move_image(const Rect& src, int x, int y);
  void clear_image(const Rect& r, u8 red, u8 green, u8 blue);

  DisplayMode& display() { return display_; }
  const DisplayMode& display() const { return display_; }

  std::span<u16> vram() { return vram_; }
  std::span<const u16> vram() const { return vram_; }
  u16 pixel(int x, int y) const { return vram_[(y & 511) * kVramWidth + (x & 1023)]; }

  // Converts the visible lines (the display range) to RGBA8888 (red in the low byte),
  // as an emulator's screenshot shows them.
  void display_to_rgba(std::vector<u32>& out, int& width, int& height) const;
  void vram_to_rgba(std::vector<u32>& out) const;

  // Draw state set by GP0 E1-E6, exposed for debugging and tests.
  struct DrawState {
    u16 texpage = 0;         // E1 bits 0-8 and 11
    bool dither = false;     // E1 bit 9
    bool rect_flip_x = false, rect_flip_y = false;
    u8 window_mask_x = 0, window_mask_y = 0, window_off_x = 0, window_off_y = 0;
    int area_left = 0, area_top = 0, area_right = 0, area_bottom = 0;  // inclusive
    int offset_x = 0, offset_y = 0;
    bool set_mask = false, check_mask = false;
  };
  const DrawState& draw_state() const { return state_; }

  // Ignore the dither bit (a presentation option; the console dithers).
  bool dithering_enabled = true;
  // The console also dithers modulated textures; PCSX-Redux's software renderer only
  // dithers gouraud shading. False matches PCSX-Redux for reference comparisons.
  bool dither_textured = true;

 private:
  struct Vertex {
    int x = 0, y = 0;
    int r = 0, g = 0, b = 0;
    int u = 0, v = 0;
  };
  struct Shading {
    bool textured = false, raw = false, semi = false, gouraud = false;
    u16 clut = 0, texpage = 0;
  };

  void draw_polygon(std::span<const u32> p);
  void draw_line(std::span<const u32> p);
  void draw_rectangle(std::span<const u32> p);
  void draw_triangle(Vertex a, Vertex b, Vertex c, const Shading& s);
  void draw_segment(Vertex a, Vertex b, const Shading& s);
  u16 sample(int u, int v, const Shading& s) const;
  void shade_pixel(int x, int y, int r, int g, int b, int u, int v, const Shading& s, bool dither);
  void write_pixel(int x, int y, u16 color, bool semi, int semi_mode);
  bool in_area(int x, int y) const {
    return x >= state_.area_left && x <= state_.area_right && y >= state_.area_top && y <= state_.area_bottom;
  }

  std::vector<u16> vram_;
  DrawState state_;
  DisplayMode display_;
};

}  // namespace ss::psx
