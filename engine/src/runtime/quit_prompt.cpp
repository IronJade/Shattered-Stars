#include "runtime/quit_prompt.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "runtime/debug_overlay.h"

namespace ss::runtime {

using platform::HostKey;

QuitPrompt::Result QuitPrompt::press(HostKey host, u16 bits) {
  // The port's own keys first, whatever the controls bind to them; then the game's buttons.
  switch (host) {
    case HostKey::kEscape:
      return Result::kKeepPlaying;
    case HostKey::kEnter:
      return choice_ == kQuit ? Result::kQuit : Result::kKeepPlaying;
    case HostKey::kLeft:
      choice_ = kKeepPlaying;
      return Result::kOpen;
    case HostKey::kRight:
      choice_ = kQuit;
      return Result::kOpen;
    case HostKey::kNone:
      break;
  }
  // The game's confirm is Cross (and Start on the title), its cancel Circle.
  if ((bits & (platform::kPadCross | platform::kPadStart)) != 0)
    return choice_ == kQuit ? Result::kQuit : Result::kKeepPlaying;
  if ((bits & platform::kPadCircle) != 0) return Result::kKeepPlaying;
  if ((bits & platform::kPadLeft) != 0) choice_ = kKeepPlaying;
  else if ((bits & platform::kPadRight) != 0) choice_ = kQuit;
  return Result::kOpen;
}

int quit_prompt_scale(int width, int height) { return std::max(1, std::min(width / 320, height / 240)); }

namespace {

constexpr const char* kTitle = "Quit Shattered Stars?";
constexpr const char* kMessage = "Unsaved progress will be lost.";
constexpr const char* kChoices[2] = {"Keep playing", "Quit"};

struct Rgb {
  int r, g, b;
};

u32 pack(Rgb c) {  // RGBA bytes in memory (R first), as Gpu::display_to_rgba makes them
  return 0xFF000000u | static_cast<u32>(c.b) << 16 | static_cast<u32>(c.g) << 8 | static_cast<u32>(c.r);
}

Rgb lerp(Rgb a, Rgb b, int t, int n) {
  if (n <= 0) return a;
  return {a.r + (b.r - a.r) * t / n, a.g + (b.g - a.g) * t / n, a.b + (b.b - a.b) * t / n};
}

// 'over' at 'alpha' (0-256) over the pixel 'under'.
u32 blend(u32 under, Rgb over, int alpha) {
  const int r = static_cast<int>(under & 0xFF), g = static_cast<int>(under >> 8 & 0xFF),
            b = static_cast<int>(under >> 16 & 0xFF);
  return pack({r + ((over.r - r) * alpha >> 8), g + ((over.g - g) * alpha >> 8), b + ((over.b - b) * alpha >> 8)});
}

struct Canvas {
  std::vector<u32>& px;
  int w, h;
  void plot(int x, int y, Rgb c) {
    if (x >= 0 && y >= 0 && x < w && y < h) px[static_cast<size_t>(y) * w + x] = pack(c);
  }
};

// Whether (x, y) lies in the w x h box at (x0, y0) whose corners are rounded to radius r.
bool inside(int x, int y, int x0, int y0, int w, int h, int r) {
  if (w <= 0 || h <= 0 || x < x0 || y < y0 || x >= x0 + w || y >= y0 + h) return false;
  r = std::min({r, w / 2, h / 2});
  const int cx = x < x0 + r ? x0 + r : x >= x0 + w - r ? x0 + w - r - 1 : x;
  const int cy = y < y0 + r ? y0 + r : y >= y0 + h - r ? y0 + h - r - 1 : y;
  const int dx = x - cx, dy = y - cy;
  return dx * dx + dy * dy <= r * r + r;
}

// A window in the style of the game's: a light rim 'u' pixels wide (with a darker line
// inside it when 'line' is given), over a translucent top-to-bottom gradient.
void panel(Canvas& c, int x0, int y0, int w, int h, int u, int radius, Rgb rim, const Rgb* line, Rgb top, Rgb bottom,
           int alpha) {
  const int inner = line != nullptr ? 2 * u : u;
  for (int y = std::max(0, y0); y < std::min(c.h, y0 + h); ++y) {
    const Rgb fill = lerp(top, bottom, y - y0, h);
    for (int x = std::max(0, x0); x < std::min(c.w, x0 + w); ++x) {
      if (!inside(x, y, x0, y0, w, h, radius)) continue;
      u32& p = c.px[static_cast<size_t>(y) * c.w + x];
      if (!inside(x, y, x0 + u, y0 + u, w - 2 * u, h - 2 * u, std::max(0, radius - u))) {
        p = pack(rim);
      } else if (!inside(x, y, x0 + inner, y0 + inner, w - 2 * inner, h - 2 * inner, std::max(0, radius - inner))) {
        p = pack(*line);
      } else {
        p = blend(p, fill, alpha);
      }
    }
  }
}

// The letters with descenders, which the 5x7 font raises into its seven rows: here with
// their tails below the line (rows 7 and 8), the rest as the font has them.
struct Tall {
  char ch;
  const char* rows[9];
};
constexpr Tall kTall[] = {
    {'g', {".....", ".....", ".####", "#...#", "#...#", "#...#", ".####", "....#", ".###."}},
    {'j', {"...#.", ".....", "..##.", "...#.", "...#.", "...#.", "...#.", "#..#.", ".##.."}},
    {'p', {".....", ".....", "####.", "#...#", "#...#", "#...#", "####.", "#....", "#...."}},
    {'q', {".....", ".....", ".####", "#...#", "#...#", "#...#", ".####", "....#", "....#"}},
    {'y', {".....", ".....", "#...#", "#...#", "#...#", "#...#", ".####", "....#", ".###."}},
};
constexpr int kGlyphRows = 9;

// The glyph's dot at (cx, cy) of its 5 x 9 cell (none outside it).
bool dot(unsigned char ch, int cx, int cy) {
  if (cx < 0 || cx > 4 || cy < 0 || cy >= kGlyphRows) return false;
  for (const Tall& t : kTall)
    if (static_cast<unsigned char>(t.ch) == ch) return t.rows[cy][cx] == '#';
  return cy < 7 && (overlay_font_column(ch, cx) >> cy & 1) != 0;
}

// The glyph at 'p' pixels per dot with its stair steps smoothed: Scale2x's rule at any
// scale (where the two neighbours across a corner of a dot agree and the other two differ,
// the half of the dot at that corner takes the neighbours' value).
bool smooth_dot(unsigned char ch, int p, int x, int y) {
  const int cx = x / p, cy = y / p, lx = x % p, ly = y % p;
  const bool e = dot(ch, cx, cy);
  if (p < 2) return e;
  const bool b = dot(ch, cx, cy - 1), d = dot(ch, cx - 1, cy), f = dot(ch, cx + 1, cy), h = dot(ch, cx, cy + 1);
  const int edge = p - 1;
  if (d == b && b != f && d != h && lx + ly < edge) return d;                    // top left
  if (b == f && b != d && f != h && (edge - lx) + ly < edge) return f;           // top right
  if (d == h && d != b && h != f && lx + (edge - ly) < edge) return d;           // bottom left
  if (h == f && h != d && f != b && (edge - lx) + (edge - ly) < edge) return f;  // bottom right
  return e;
}

int text_width(const char* s, int p) {
  const int n = static_cast<int>(std::strlen(s));
  return n > 0 ? n * 6 * p - p : 0;
}

// 'text' at (x, y) (the top of its capitals; the tails of g, j, p, q, y hang two dots below
// the seven), 'p' pixels per dot, with a drop shadow 'off' pixels down and right.
void text(Canvas& c, int x, int y, int p, const char* s, Rgb colour, Rgb shadow, int off) {
  for (int pass = 0; pass < 2; ++pass) {
    const int dx = pass == 0 ? off : 0;
    const Rgb ink = pass == 0 ? shadow : colour;
    for (int i = 0; s[i] != 0; ++i) {
      const unsigned char ch = static_cast<unsigned char>(s[i]);
      const int gx = x + i * 6 * p + dx, gy = y + dx;
      for (int yy = 0; yy < kGlyphRows * p; ++yy)
        for (int xx = 0; xx < 5 * p; ++xx)
          if (smooth_dot(ch, p, xx, yy)) c.plot(gx + xx, gy + yy, ink);
    }
  }
}

// The cursor: a triangle pointing right, 'w' wide and 'h' high, its left edge at x.
void cursor(Canvas& c, int x, int y, int w, int h, Rgb colour, Rgb shadow, int off) {
  for (int pass = 0; pass < 2; ++pass) {
    const int d = pass == 0 ? off : 0;
    const Rgb ink = pass == 0 ? shadow : colour;
    for (int row = 0; row < h; ++row) {
      const int from_mid = std::abs(2 * row + 1 - h);
      const int len = w * (h - from_mid) / h;
      for (int i = 0; i < len; ++i) c.plot(x + i + d, y + row + d, ink);
    }
  }
}

}  // namespace

void draw_quit_prompt(const u32* frame, int frame_w, int frame_h, int choice, int scale, std::vector<u32>& canvas) {
  const int u = std::max(1, scale);  // one of the game's pixels
  const int cw = 320 * u, ch = 240 * u;
  canvas.assign(static_cast<size_t>(cw) * ch, 0xFF000000u);
  // The paused picture, scaled (nearest) and dimmed to half.
  if (frame != nullptr && frame_w > 0 && frame_h > 0) {
    std::vector<int> column(static_cast<size_t>(cw));
    for (int x = 0; x < cw; ++x) column[static_cast<size_t>(x)] = static_cast<int>(static_cast<long long>(x) * frame_w / cw);
    for (int y = 0; y < ch; ++y) {
      const u32* row = frame + static_cast<size_t>(static_cast<long long>(y) * frame_h / ch) * frame_w;
      u32* out = canvas.data() + static_cast<size_t>(y) * cw;
      for (int x = 0; x < cw; ++x) out[x] = 0xFF000000u | (row[column[static_cast<size_t>(x)]] >> 1 & 0x007F7F7Fu);
    }
  }
  Canvas c{canvas, cw, ch};

  // The layout, in the game's pixels (u) and the text's dots (p: about the game's own
  // text size, 9-10 of its pixels high).
  const int p = std::max(1, (5 * u + 2) / 4);
  const int text_h = 7 * p, shadow = std::max(1, u / 2);
  const int pad_x = 14 * u, pad_top = 11 * u, pad_bottom = 11 * u, gap_title = 6 * u, gap_buttons = 12 * u;
  const int button_pad_x = 10 * u, button_pad_y = 4 * u, button_gap = 14 * u;
  const int button_w = std::max(text_width(kChoices[0], p), text_width(kChoices[1], p)) + 2 * button_pad_x;
  const int button_h = text_h + 2 * button_pad_y;
  const int row_w = 2 * button_w + button_gap;
  const int content_w = std::max({text_width(kTitle, p), text_width(kMessage, p), row_w});
  const int box_w = std::min(cw, content_w + 2 * pad_x);
  const int box_h = pad_top + text_h + gap_title + text_h + gap_buttons + button_h + pad_bottom;
  const int box_x = (cw - box_w) / 2, box_y = (ch - box_h) / 2;

  const Rgb box_rim{224, 232, 248}, box_line{88, 104, 148};
  panel(c, box_x, box_y, box_w, box_h, u, 4 * u, box_rim, &box_line, {48, 80, 152}, {14, 26, 70}, 212);
  const Rgb ink_shadow{0, 0, 16};
  int y = box_y + pad_top;
  text(c, box_x + (box_w - text_width(kTitle, p)) / 2, y, p, kTitle, {255, 255, 255}, ink_shadow, shadow);
  y += text_h + gap_title;
  text(c, box_x + (box_w - text_width(kMessage, p)) / 2, y, p, kMessage, {196, 210, 236}, ink_shadow, shadow);
  y = box_y + box_h - pad_bottom - button_h;
  const int row_x = box_x + (box_w - row_w) / 2;
  for (int i = 0; i < 2; ++i) {
    const bool on = i == choice;
    const int bx = row_x + i * (button_w + button_gap);
    if (on) {
      panel(c, bx, y, button_w, button_h, u, 2 * u, {240, 244, 255}, nullptr, {124, 172, 244}, {56, 98, 196}, 236);
      cursor(c, bx - 8 * u, y + (button_h - 9 * u) / 2, 5 * u, 9 * u, {255, 255, 255}, ink_shadow, shadow);
    } else {
      panel(c, bx, y, button_w, button_h, u, 2 * u, {92, 108, 150}, nullptr, {34, 50, 100}, {18, 28, 62}, 180);
    }
    const Rgb ink = on ? Rgb{255, 255, 255} : Rgb{150, 162, 196};
    text(c, bx + (button_w - text_width(kChoices[i], p)) / 2, y + button_pad_y, p, kChoices[i], ink, ink_shadow, shadow);
  }
}

}  // namespace ss::runtime
