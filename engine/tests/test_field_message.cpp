// Field message system, notice banner and fades (game/field/message*, notice,
// fade). With SS_DISC1 set, the windows are run on map 7's real text and font
// (entry 3214) with the field system data (#3110) and compared, pixel for pixel,
// with PCSX-Redux captures of the original when analysis/reference/message/ exists
// (SS_MSG_REF overrides the directory). SS_MSG_PNG=DIR writes the port's frames
// next to the references for inspection.
//
// The captures (vsyncNNNNN_320x224_16.raw) count vblanks from the first call of
// title 0x8003d3c8 with the input schedule of analysis/reference/firstmap; in that
// run the map fade-in is pushed at vblank 2526 (type 1, 120 frames), the map-name
// notice is shown at 2646 and Ronixis's window opens at 3190 (12, 6, 26, 3, string
// 90). A frame built after vblank N is on screen in capture N + 3.

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "disc/archive.h"
#include "disc/disc_image.h"
#include "disc/slz.h"
#include "game/boot/pad.h"
#include "game/boot/system.h"
#include "game/field/fade.h"
#include "game/field/field.h"
#include "game/field/message.h"
#include "game/field/notice.h"
#include "game/field/screen.h"
#include "game/field/tasks.h"
#include "game/title/title_program.h"
#include "runtime/libgpu.h"
#include "runtime/runtime.h"
#include "test.h"
#include "util/png.h"

using namespace ss;
using namespace ss::game::field;

namespace {

u32 rd32(const u8* p) {
  u32 v;
  std::memcpy(&v, p, 4);
  return v;
}
u16 rd16(const u8* p) {
  u16 v;
  std::memcpy(&v, p, 2);
  return v;
}

// The field state as field_run leaves it for a new game with Claude, the field
// screen, #3110 and map 7's script text and font.
struct TestField {
  std::unique_ptr<disc::DiscImage> image;
  disc::Archive archive;
  Screen screen;
  std::vector<u8> script, font;
  const u8* string(int i) const {
    const u32 code = rd32(script.data());
    const u32 count = rd32(script.data() + 0xC);
    const u8* table = script.data() + 0x1C + code;
    return table + count * 2 + rd16(table + i * 2);
  }
};

TestField* field() {
  static TestField f;
  static bool tried = false, ok = false;
  if (tried) return ok ? &f : nullptr;
  tried = true;
  if (game::get_system() == nullptr || game::get_system()->loader.archive() == nullptr) {
    const char* path = std::getenv("SS_DISC1");
    if (!path || !*path) {
      std::printf("  (skipped: SS_DISC1 not set)\n");
      return nullptr;
    }
    std::string error;
    f.image = disc::DiscImage::open(path, &error);
    if (!f.image || !f.archive.open(*f.image, &error)) {
      std::fprintf(stderr, "  cannot open %s: %s\n", path, error.c_str());
      ++test::failures;
      return nullptr;
    }
    game::boot_main(0, &f.archive);
    game::pad_set_port_mask(&game::get_system()->pad, 3);
  }
  game::create_game_state();  // handoffs 2, 3, 4, 6 as the title program leaves them
  field_bind_system(nullptr);
  if (handoff_get(globals().handoff, 9) == nullptr) field_create_state_block();
  field_bind_state_block(nullptr);
  task_list_clear();
  screen_construct(&f.screen);
  screen_alloc_arena(&f.screen);
  screen_layout_arena(&f.screen, 1);
  screen_setup_buffer0(&f.screen, 0, 0, 0x140, 0xF0, 8, 8, 0, 0);
  screen_setup_buffer1(&f.screen, 0, 0xE0, 0x140, 0xF0, 8, 8, 0, 0);
  field_load_system_data();
  // Map 7: container entry 3207 + 7, section 1 = SLZ chain {script, font}.
  game::Loader* loader = globals().loader;
  std::vector<u8> map(static_cast<size_t>(loader->entry_size(3214)));
  loader->load_sync(map.data(), 3214, 0);
  const u32 sections = rd32(map.data());
  u32 offset = 0;
  for (u32 i = 0; i < sections; ++i) {
    if (rd32(map.data() + 4 + i * 8) == 1) offset = rd32(map.data() + 8 + i * 8);
  }
  auto script = disc::slz::unpack_block(map, offset, 0, nullptr);
  auto font = disc::slz::unpack_block(map, offset, 1, nullptr);
  if (!script || !font) {
    std::fprintf(stderr, "  map 7 section 1 did not unpack\n");
    ++test::failures;
    return nullptr;
  }
  f.script = std::move(*script);
  f.font = std::move(*font);
  msg_set_map_font(f.font.data());
  auto& gpu = runtime::Runtime::get().gpu;
  gpu.dithering_enabled = true;  // --reference-dither: PCSX-Redux dithers gouraud only
  gpu.dither_textured = false;
  gpu.dither_lines = false;
  ok = true;
  return &f;
}

std::filesystem::path reference_dir() {
  if (const char* d = std::getenv("SS_MSG_REF"); d && *d) return d;
  for (std::filesystem::path p = std::filesystem::current_path(); !p.empty(); p = p.parent_path()) {
    const auto candidate = p / "analysis" / "reference" / "message";
    if (std::filesystem::is_directory(candidate)) return candidate;
    if (p == p.parent_path()) break;
  }
  return {};
}

// A 320x224 capture of the original, 15-bit.
std::optional<std::vector<u16>> reference(int vsync) {
  const auto dir = reference_dir();
  if (dir.empty()) return std::nullopt;
  char name[64];
  std::snprintf(name, sizeof name, "vsync%05d_320x224_16.raw", vsync);
  FILE* f = std::fopen((dir / name).string().c_str(), "rb");
  if (!f) return std::nullopt;
  std::vector<u16> px(320 * 224);
  const size_t n = std::fread(px.data(), 2, px.size(), f);
  std::fclose(f);
  if (n != px.size()) return std::nullopt;
  for (auto& p : px) p &= 0x7FFF;
  return px;
}

// One field frame: flip, clear the OT, run the tasks, draw over 'background'.
// Returns the drawn 320x224 picture (15-bit).
std::vector<u16> run_frame(TestField* f, const std::vector<u16>* background, u16 pressed = 0,
                           const std::function<void()>& before_tasks = {}) {
  FieldGlobals& g = globals();
  screen_flip(&f->screen);
  screen_clear_ot(&f->screen);
  g.frame_ticks = 1;
  g.pressed[0] = pressed;
  if (before_tasks) before_tasks();
  task_run_all();
  psyq::PutDrawEnv(&f->screen.current->draw);
  const s16 top = f->screen.current->draw.clip.y;
  if (background) {
    const psyq::RECT r{0, top, 320, 224};
    psyq::LoadImage(&r, background->data());
  }
  psyq::DrawOTag(f->screen.current->ot.data());
  psyq::DrawSync(0);
  std::vector<u16> out(320 * 224);
  auto& gpu = runtime::Runtime::get().gpu;
  for (int y = 0; y < 224; ++y)
    for (int x = 0; x < 320; ++x) out[static_cast<size_t>(y * 320 + x)] = gpu.pixel(x, top + y) & 0x7FFF;
  return out;
}

void write_pair_png(const char* tag, int n, const std::vector<u16>& port, const std::vector<u16>* ref) {
  const char* dir = std::getenv("SS_MSG_PNG");
  if (!dir || !*dir) return;
  std::filesystem::create_directories(dir);
  const int w = ref ? 640 : 320;
  std::vector<u32> rgba(static_cast<size_t>(w * 224));
  auto conv = [](u16 p) {
    const u32 r = (p & 31) << 3, g = (p >> 5 & 31) << 3, b = (p >> 10 & 31) << 3;
    return r | g << 8 | b << 16 | 0xFF000000u;
  };
  for (int y = 0; y < 224; ++y) {
    for (int x = 0; x < 320; ++x) {
      if (ref) rgba[static_cast<size_t>(y * w + x)] = conv((*ref)[static_cast<size_t>(y * 320 + x)]);
      rgba[static_cast<size_t>(y * w + x + (ref ? 320 : 0))] = conv(port[static_cast<size_t>(y * 320 + x)]);
    }
  }
  char name[96];
  std::snprintf(name, sizeof name, "%s_%05d.png", tag, n);
  util::write_png(std::filesystem::path(dir) / name, rgba, w, 224);
}

// Comparison of a region with a capture. Pixels must match exactly, except that
// inside 'soft' (a semi-transparent, dithered gouraud body) a channel may be one step
// off: PCSX-Redux blends the 8-bit colour with the background and dithers the result,
// the hardware (and this GPU) dithers the colour to 5 bits first and blends 5-bit values
// (docs/notes/gpu_accuracy.md).
struct Compare {
  int frames = 0, exact_frames = 0, hard_frames = 0, soft_pixels = 0, worst = 0, excluded = 0;
  // 'skip': a rectangle left out (textured quads whose UVs are scaled or rotated:
  // PCSX-Redux's texture stepping differs from the hardware's there; the primitives
  // themselves were checked against the original's RAM).
  void add(const char* tag, int vsync, const std::vector<u16>& port, const std::vector<u16>& ref, psyq::RECT area,
           psyq::RECT soft, psyq::RECT skip = {0, 0, 0, 0}) {
    int hard = 0, softn = 0;
    for (int y = std::max<int>(area.y, 0); y < std::min(area.y + area.h, 224); ++y) {
      for (int x = std::max<int>(area.x, 0); x < std::min(area.x + area.w, 320); ++x) {
        if (x >= skip.x && x < skip.x + skip.w && y >= skip.y && y < skip.y + skip.h) {
          ++excluded;
          continue;
        }
        const u16 p = port[static_cast<size_t>(y * 320 + x)], q = ref[static_cast<size_t>(y * 320 + x)];
        if (p == q) continue;
        bool small = x >= soft.x && x < soft.x + soft.w && y >= soft.y && y < soft.y + soft.h;
        for (int shift = 0; shift < 15 && small; shift += 5) {
          const int c = (p >> shift & 31) - (q >> shift & 31);
          small = c >= -1 && c <= 1;
        }
        if (small) {
          ++softn;
        } else {
          ++hard;
        }
      }
    }
    ++frames;
    exact_frames += hard == 0 && softn == 0;
    soft_pixels += softn;
    if (hard != 0) {
      ++hard_frames;
      if (hard_frames <= 5) std::fprintf(stderr, "  %s vsync %d: %d pixels differ\n", tag, vsync, hard);
    }
    worst = std::max(worst, hard);
  }
  void report(const char* what) const {
    std::printf("  %s: %d frames compared with the original: %d identical, %d with differences beyond one gouraud step "
                "(worst %d pixels); %d body pixels one step off; %d pixels of scaled/rotated textured quads left out\n",
                what, frames, exact_frames, hard_frames, worst, soft_pixels, excluded);
  }
};

int differences_count(const std::vector<u16>& a, const std::vector<u16>& b, int x0, int y0, int x1, int y1) {
  int n = 0;
  for (int y = y0; y < y1; ++y)
    for (int x = x0; x < x1; ++x) n += a[static_cast<size_t>(y * 320 + x)] != b[static_cast<size_t>(y * 320 + x)];
  return n;
}

// The body rectangle of a window (the gouraud quad).
psyq::RECT body_rect(const MsgWindow* w) {
  return {static_cast<s16>(w->draw_x + 3), static_cast<s16>(w->draw_y + 3), static_cast<s16>(w->draw_w - 6),
          static_cast<s16>(w->draw_h - 6)};
}

}  // namespace

TEST(field_text_layout_map7) {
  TestField* f = field();
  if (!f) return;
  MsgSystem* msg = msg_system() ? msg_system() : msg_system_create(new MsgSystem);
  // Ronixis's first line: "Ronixis{1}Well? ..." - glyph 1 starts the second line.
  msg_window_create(msg, 12, 6, 26, 3, f->string(90), 0);
  MsgWindow* w = msg_window(0);
  CHECK(w != nullptr);
  if (!w) return;
  TextLine line;
  const int h = text_layout_line(w, f->string(90), &line);
  CHECK_EQ(h, 13);
  CHECK_EQ(line.glyphs(), 7);  // "Ronixis"
  CHECK_EQ(line.width(), 52);  // glyph 1 is measured before it ends the line
  CHECK_EQ(line.ended(), 0);
  msg_window_destroy(msg, 0);
  CHECK(msg_window(0) == nullptr);
}

TEST(field_message_window_ronixis_matches_original) {
  TestField* f = field();
  if (!f) return;
  MsgSystem* msg = msg_system() ? msg_system() : msg_system_create(new MsgSystem);
  msg_window_destroy(msg, -1);
  // The background: the capture just before the window opens.
  const auto background = reference(3192);
  std::vector<u16> blank(320 * 224, 0);
  const std::vector<u16>* bg = background ? &*background : &blank;
  // The script's ext 0x64(12, 6, 26, 3, 0, 90, 0): msg_window_open(..., hold 1), then
  // the opcode clears the hold (only ext 0x68 / 0x69 keep it).
  msg_window_open(msg, 12, 6, 26, 3, f->string(90), 0, 1);
  MsgWindow* w = msg_window(0);
  CHECK(w != nullptr);
  if (!w) return;
  w->hold = 0;
  Compare cmp;
  int closed_at = 0;
  constexpr int kPress = 912;
  // Frame k is built after vblank 3189 + k and captured at 3192 + k. Cross is
  // pressed at the vblank-4100 event and latched by the pad module at the next vblank,
  // so frame 912 sees it; the next window opens at vblank 4226 (frame 1037).
  for (int frame = 1; frame <= 1110; ++frame) {
    if (frame == 1037) {
      // ext 0x64(3, 8, 32, 3, 0, 91, 0): "Science Officer / No. There continues ..."
      CHECK(msg_window(0) == nullptr);
      msg_window_open(msg, 3, 8, 32, 3, f->string(91), 0, 1);
      w = msg_window(0);
      if (!w) break;
      w->hold = 0;
    }
    const auto port = run_frame(f, bg, frame == kPress ? 0x40 : 0);
    w = msg_window(0);
    if (frame == 1) {  // RAM of the original after its first update (vblank 3191)
      CHECK_EQ(w->timer, 9);
      CHECK_EQ(w->draw_x, 180);
      CHECK_EQ(w->draw_y, 71);
      CHECK_EQ(w->draw_w, 41);
      CHECK_EQ(w->draw_h, 8);
      CHECK_EQ(w->blink, 2);
    }
    if (frame == 4010 - 3190) {
      // The original's window at its button wait (RAM at vblank 4010).
      CHECK_EQ(w->state, 6);
      CHECK_EQ(w->blink, 37);
      CHECK_EQ(w->line_count, 3);
      CHECK_EQ(w->first_line, 0);
      CHECK_EQ(w->used_lines, 2);
      CHECK_EQ(w->text_height, 39);
      CHECK_EQ(w->page_offset, 129);
      CHECK_EQ(w->pending_glyphs, 28);
      CHECK_EQ(w->lines[0].shown, 7);
      CHECK_EQ(w->lines[0].remaining, 1);
      CHECK_EQ(w->lines[1].shown, 29);
      CHECK_EQ(w->lines[2].shown, 28);
      const u16 x1[] = {0, 7, 17, 24, 29, 33, 40, 45, 55, 62, 70, 75, 80, 88, 95, 103, 107, 113, 120, 126, 133, 137, 143, 148, 156, 161, 169, 176, 182, 187};
      const u16 x2[] = {0, 5, 10, 17, 27, 32, 37, 44, 51, 56, 63, 68, 75, 82, 86, 93, 100, 107, 111, 118, 125, 130, 137, 144, 149, 156, 163, 170, 176};
      for (size_t i = 0; i < std::size(x1); ++i) CHECK_EQ(w->lines[1].glyph_x[i], x1[i]);
      for (size_t i = 0; i < std::size(x2); ++i) CHECK_EQ(w->lines[2].glyph_x[i], x2[i]);
    }
    if (frame > kPress && frame < 1037 && w == nullptr && closed_at == 0) closed_at = frame;
    const int vsync = 3190 + 2 + frame;
    const auto ref = reference(vsync);
    if (frame <= 300 || (frame >= 790 && frame <= 960) || frame >= 1030) {
      write_pair_png("ronixis", vsync, port, ref ? &*ref : nullptr);
    }
    if (ref) {
      // The window's area (the actors below and beside it move in the original).
      const psyq::RECT area = frame < 1037 ? psyq::RECT{92, 44, 216, 64} : psyq::RECT{20, 60, 264, 61};
      const psyq::RECT none{0, 0, 0, 0};
      cmp.add("window", vsync, port, *ref, area, w ? body_rect(w) : none);
    }
  }
  // Cross ends the wait; the page is done and, without hold, the window closes
  // (state 9 for 10 frames, then destroyed by msg_system_update).
  CHECK_EQ(closed_at, kPress + 12);
  if (cmp.frames != 0) {
    cmp.report("Ronixis and Science Officer windows");
    CHECK_EQ(cmp.hard_frames, 0);
  } else {
    std::printf("  (no reference captures: analysis/reference/message)\n");
  }
  msg_window_destroy(msg, -1);
}

TEST(field_notice_banner_matches_original) {
  TestField* f = field();
  if (!f) return;
  MsgSystem* msg = msg_system() ? msg_system() : msg_system_create(new MsgSystem);
  msg_window_destroy(msg, -1);
  Notice* notice = g_notice_object() ? g_notice_object() : notice_create(new Notice);
  const auto background = reference(2648);
  std::vector<u16> blank(320 * 224, 0);
  const std::vector<u16>* bg = background ? &*background : &blank;
  // Script ext 0xb9 at vblank 2646: notice_show(string 89 "Planet Milocinia", icon 0x11,
  // window 7, at the top). The script VM runs after the notice in that frame, so the
  // notice's first update is in the next frame (after vblank 2647).
  notice_show(notice, f->string(89), 0x11, 7, 1);
  CHECK_EQ(notice->text_width, 111);  // the original creates window 7 with w = -(111 + 0x12)
  Compare cmp;
  int skipped = 0;
  bool created = false, moved = false, gone = false;
  for (int frame = 1; frame <= 250; ++frame) {
    const auto port = run_frame(f, bg);
    MsgWindow* w = msg_window(7);
    if (w && !created) {
      created = true;
      CHECK_EQ(frame, 2672 - 2646);  // msg_window_create at vblank 2672
      CHECK_EQ(w->x, 24);
      CHECK_EQ(w->y, 8);
      CHECK_EQ(w->w, 129);
      CHECK_EQ(w->h, 13);
    }
    if (w && !moved && w->move_write != w->move_read) {
      moved = true;
      CHECK_EQ(frame, 2853 - 2646);  // msg_window_push_move at vblank 2853
    }
    if (created && !w && !gone) {
      gone = true;
      CHECK_EQ(frame, 2863 - 2646);  // msg_window_destroy at vblank 2863
    }
    const int vsync = 2646 + 3 + frame;
    const auto ref = reference(vsync);
    write_pair_png("notice", vsync, port, ref ? &*ref : nullptr);
    if (ref) {
      // The map darkens for a moment around vblank 2830 (a BG tint the script sets):
      // those captures have another background.
      if (background && differences_count(*ref, *background, 0, 44, 320, 64) > 64) {
        ++skipped;
        continue;
      }
      const psyq::RECT sweep{static_cast<s16>(notice->x + 2), static_cast<s16>(notice->y + 2), 0xF8, 0x14};
      psyq::RECT skip{0, 0, 0, 0};
      if (notice->state == 3 && notice->shown_frames < 0x10) {
        skip = {static_cast<s16>(notice->x + 4), static_cast<s16>(notice->y + 2), 0x14, 0x14};  // the icon's spin
      } else if (notice->effect == 1 && notice->effect_timer >= 0x9C41) {
        skip = {static_cast<s16>(notice->x + 0x8A - 4), static_cast<s16>(notice->y + 0xD), 0xE, 0x10};  // the sparkle
      }
      cmp.add("notice", vsync, port, *ref, {0, 0, 320, 64}, sweep, skip);
    }
  }
  CHECK(created && moved && gone);
  CHECK_EQ(notice->state, 0);
  if (cmp.frames != 0) {
    cmp.report("notice banner");
    std::printf("  (%d captures with a darkened map skipped)\n", skipped);
    CHECK_EQ(cmp.hard_frames, 0);
  }
}

TEST(field_fade_in_matches_original) {
  TestField* f = field();
  if (!f) return;
  ScreenFade* fade = field_fade() ? field_fade() : fade_construct(new ScreenFade);
  if (MsgSystem* msg = msg_system()) msg_window_destroy(msg, -1);
  if (Notice* n = g_notice_object()) notice_reset(n);
  // The scene after the fade (no window yet), and the fade-in pushed at vblank 2526
  // by the map script (ext 0x7c: type 1, 120 frames): a subtractive tile, 255 -> 0.
  const auto background = reference(2648);
  std::vector<u16> blank(320 * 224, 0x3DEF);
  const std::vector<u16>* bg = background ? &*background : &blank;
  const int slot = fade->write;
  CHECK(fade_push(fade, 1, 120, 0, 0));
  CHECK(fade_is_active(fade, -1));
  CHECK(fade_is_active(fade, 1));
  CHECK(!fade_is_active(fade, 3));
  Compare cmp;
  for (int frame = 1; frame <= 122; ++frame) {
    const auto port = run_frame(f, bg);
    CHECK_EQ(fade->cmd[slot].elapsed, std::min(frame, 120));
    const int vsync = 2526 + 2 + frame;
    const auto ref = reference(vsync);
    write_pair_png("fade", vsync, port, ref ? &*ref : nullptr);
    // The top of the map: background only (the actors below still move).
    if (ref) cmp.add("fade", vsync, port, *ref, {0, 0, 320, 100}, {0, 0, 0, 0});
  }
  CHECK(!fade_is_active(fade, -1));
  CHECK_EQ(fade->hold, 0);
  if (cmp.frames != 0) {
    cmp.report("fade in");
    CHECK_EQ(cmp.hard_frames, 0);
  }
  // A fade out holds black with the display off; the next command releases it.
  CHECK(fade_push(fade, 3, 4, 0, 0));
  for (int i = 0; i < 4; ++i) run_frame(f, bg);
  CHECK_EQ(fade->hold, 2);
  CHECK(!runtime::Runtime::get().display_enabled);
  CHECK(fade_push(fade, 2, 4, 0, 0));
  run_frame(f, bg);
  CHECK_EQ(fade->hold, 0);
  CHECK(runtime::Runtime::get().display_enabled);
  for (int i = 0; i < 4; ++i) run_frame(f, bg);
  // The ring holds 7 commands.
  for (int i = 0; i < 7; ++i) CHECK(fade_push(fade, 0, 1, 0, 0));
  CHECK(!fade_push(fade, 0, 1, 0, 0));
  for (int i = 0; i < 8; ++i) run_frame(f, bg);
  CHECK(!fade_is_active(fade, -1));
}

TEST(field_choice_cursor) {
  TestField* f = field();
  if (!f) return;
  MsgSystem* msg = msg_system() ? msg_system() : msg_system_create(new MsgSystem);
  msg_window_destroy(msg, -1);
  // Map 7's shop prompt: ext 0x68(x, y, 0x11, 2, string 39) "Yes / No thank you",
  // ext 0xaa(0, 0) (no justification), then ext 0xdf(2, 0, 0, 1): two options from
  // line 0, cancel allowed, window 0.
  msg_window_open(msg, 2, 2, 0x11, 2, f->string(39), 0, 1);
  MsgWindow* w = msg_window(0);
  CHECK(w != nullptr);
  if (!w) return;
  w->justify = 0;
  std::vector<u16> bg(320 * 224, 0x2108);
  for (int i = 0; i < 60 && w->state != 6; ++i) run_frame(f, &bg);
  CHECK_EQ(w->state, 6);
  CHECK_EQ(w->lines[0].shown, 6);   // two {fe}, {ea}, "Yes"
  CHECK_EQ(w->lines[1].shown, 15);  // two {fe}, {ea}, "No thank you"
  ChoiceState c;
  c.options = 2;
  c.first_line = 0;
  c.selection = 0;
  c.cancel_allowed = 1;
  c.window = 0;
  c.cursor[0][0] = -1;
  c.slide = 0;
  for (auto& n : c.lines_per_option) n = 1;
  w->cancel_closes = 1;
  w->show_prompt = 0;
  auto choice = [&c]() { choice_draw_cursor(&c); };
  auto port = run_frame(f, &bg, 0, choice);
  write_pair_png("choice", 0, port, nullptr);
  CHECK_EQ(c.option_lines_drawn, 1);
  CHECK_EQ(c.cursor[0][0], w->x + 8);
  CHECK_EQ(c.cursor[0][1], w->y + 8);
  CHECK_EQ(c.line_width[0], w->lines[0].glyph_x[w->lines[0].shown]);
  CHECK_EQ(c.line_height[0], 13);
  // The selection moves down: the underline follows after a 4-frame slide.
  c.selection = 1;
  int frames = 0;
  do {
    port = run_frame(f, &bg, 0, choice);
    ++frames;
    write_pair_png("choice", frames, port, nullptr);
  } while (c.slide != 0 && frames < 10);
  CHECK_EQ(frames, 4);
  CHECK_EQ(c.cursor[0][1], w->y + 8 + 13);
  CHECK_EQ(c.line_width[0], w->lines[1].glyph_x[w->lines[1].shown]);
  // The cursor sprite and the two underline lines are in the frame (gold over black).
  const int ux = c.cursor[0][0] + 0xE, uy = c.cursor[0][1] + 13 - 1;
  const u16 gold = static_cast<u16>(0xF0 >> 3 | (0xD8 >> 3) << 5 | (0x18 >> 3) << 10);
  CHECK_EQ(port[static_cast<size_t>(uy * 320 + ux + 4)], gold);
  CHECK_EQ(port[static_cast<size_t>((uy + 1) * 320 + ux + 5)], 0);
  // Cancel: the window holds (ext 0x68), so it stays; the VM then sets +0x13f to end the
  // wait, the page ends and the window idles until the script closes it.
  run_frame(f, &bg, 0x20, choice);
  CHECK_EQ(w->state, 6);
  w->force_advance = 1;
  run_frame(f, &bg);
  run_frame(f, &bg);
  CHECK_EQ(msg_window_status(msg, 0), 1);
  msg_window_close(msg, 0);
  for (int i = 0; i < 12; ++i) run_frame(f, &bg);
  CHECK(msg_window(0) == nullptr);
}
