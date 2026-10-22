#include "game/field/menu_picture.h"

#include <cstring>
#include <vector>

#include "game/boot/loader.h"
#include "game/boot/pad.h"
#include "game/boot/sound.h"
#include "game/common/menu_core.h"
#include "game/common/menu_scene.h"
#include "game/common/menu_widget.h"
#include "game/common/screen.h"
#include "game/field/field.h"
#include "game/field/load_queue.h"
#include "runtime/libgpu.h"

namespace ss::game::field {
namespace {

constexpr int kStrips = 75;          // 320 x 16 each
constexpr int kStripBytes = 0x2800;  // 5 sectors
constexpr int kSlots = 30;           // VRAM rows of strips at (0x140, slot * 16)
constexpr int kPictureEntry = 0x117C;

// ---- The picture viewer (0x2e0 bytes; 0xbcc 0x8007e598) ---------------------------------
struct PictureViewer {
  explicit PictureViewer(int entry);       // 0x8007e598
  ~PictureViewer();                        // 0x8007e740
  bool step();                             // 0x8007e7ec: true when it is over
  void load();                             // 0x8007ebd4
  int alloc_slot();                        // 0x8007edc4
  void upload();                           // 0x8007eec8
  void draw();                             // 0x8007f028
  void forget(int y, int dir);             // 0x8007eb50
  void want(int strip) {                   // 0x8007eb0c
    if (state[strip] == 0 && loaded[strip] == 0 && vram[strip] < 0) state[strip] = 1;
  }

  s16 entry = 0;                 // +0
  s16 y = 0;                     // +2: the scroll, 0..0x2f0
  s16 bright = 0x100;            // +4
  s16 speed = 0;                 // +6
  u8 closing = 0;                // +8
  u8 dir = 0;                    // +9
  u8 state[kStrips] = {};        // +0xa: 1 wanted, 2 / 3 being read into buffer 0 / 1
  u8 loaded[kStrips] = {};       // +0x55: 1 / 2 being read, 3 / 4 read into buffer 0 / 1
  s16 vram[kStrips] = {};        // +0xa0: the strip's VRAM row, -1 none
  u8 fade[kStrips] = {};         // +0x136
  const u8* data[kStrips] = {};  // +0x184
  std::vector<u8> buffers[2];    // +0x2b0 / +0x2b4
  u8 used[32] = {};              // +0x2b8: VRAM slots
  RenderContext ctx;             // +0x2d8
  std::vector<u8> picture;       // the entry, read at once here (the console streams it)
};

PictureViewer::PictureViewer(int e) {
  render_ctx_create_arena(&ctx, 0x4000);
  render_ctx_create_ot(&ctx, 1);
  screen_setup_buffer0(0, 0, 0x140, 0x1E0, 0, 0, 0, 0);
  screen_setup_buffer1(0, 0, 0x140, 0x1E0, 0, 0, 0, 0);
  for (int i = 0; i < 2; ++i) {
    psyq::DRAWENV* d = screen_drawenv(i);
    d->isbg = 1;
    screen_dispenv(i)->isinter = 1;
    d->r0 = d->g0 = d->b0 = 0;
  }
  entry = static_cast<s16>(e);
  y = 0;
  bright = 0x100;
  closing = 0;
  for (int i = 0; i < kStrips; ++i) {
    state[i] = 0;
    loaded[i] = 0;
    vram[i] = -1;
    fade[i] = 0;
    data[i] = nullptr;
  }
  dir = 0;
  for (int i = 0; i < 6; ++i) state[i] = 1;
  std::memset(used, 0, sizeof used);
  speed = 0;
  if (Loader* loader = globals().loader) {
    const int size = loader->entry_size(e);
    if (size > 0) {
      picture.assign(static_cast<size_t>(size), 0);
      loader->load_sync(picture.data(), e, size);
    }
  }
}

PictureViewer::~PictureViewer() {
  screen_destroy_arena(ctx.arena);
  screen_destroy_ot(ctx.ot);
  psyq::ResetGraph(1);
}

void PictureViewer::forget(int at, int direction) {
  int i = at >> 4;
  if (direction < 1) {
    while (--i >= 0) {
      if (state[i] == 1) state[i] = 0;
    }
  } else {
    while (++i < kStrips) {
      if (state[i] == 1) state[i] = 0;
    }
  }
}

bool PictureViewer::step() {
  FieldGlobals& g = globals();
  const u16 pad = pad_current(g.pad, 0);
  const u8* config = g.config;
  if (bright < 1) return true;
  if (pad & 0x1000) {
    speed = static_cast<s16>(speed - 2);
    if (speed < -0x10) speed = -0x10;
  } else if (pad & 0x4000) {
    speed = static_cast<s16>(speed + 2);
    if (speed > 0x10) speed = 0x10;
  } else {
    int s = speed;
    bool done = false;
    if (s > 0) {
      speed = static_cast<s16>(s - 2);
      if (s - 2 >= 0) {
        done = true;
      } else {
        speed = 0;
        dir = 0;
        s = 0;
      }
    }
    if (!done && s < 0) {
      speed = static_cast<s16>(s + 2);
      if (s + 2 > 0) {
        speed = 0;
        dir = 0;
      }
    }
  }
  s32 rx, ry, lx, ly;
  if (pad_stick(g.pad, 0, &rx, &ry, &lx, &ly)) speed = static_cast<s16>((ly * 0x18) / 0x50);
  y = static_cast<s16>(y + speed);
  if (y < 0) y = 0;
  if (y > 0x2F0) y = 0x2F0;
  if ((read_u16(config + 8) & pad) != 0) {          // button 4: the top
    y = 0;
    dir = 0;
    forget(y + 0x1C0, 1);
  } else if ((read_u16(config + 10) & pad) != 0) {  // button 5: the bottom
    y = 0x2F0;
    dir = 0;
    forget(y, -1);
  }
  // (dir is only ever cleared, so these two never run)
  if (dir == 2 && speed < 0) {
    forget(y + 0x1C0, 1);
    dir = 1;
  }
  if (dir == 1 && speed > 0) {
    forget(y, -1);
    dir = 2;
  }
  bool fade_out = closing != 0;
  if ((read_u16(config + 2) & pad) != 0) {          // cancel
    if (closing == 0) {
      sound_play_sfx(g.sound, 6, 0, 0x7F40);
      closing = 1;
    }
    fade_out = true;
  }
  if (fade_out) {
    bright = static_cast<s16>(bright - 8);
    if (bright < 1) return true;
  }
  load();
  upload();
  draw();
  return false;
}

void PictureViewer::load() {
  // The read issued last frame is done (reads finish at once here).
  for (int i = 0; i < kStrips; ++i) {
    if (state[i] > 1) {
      loaded[i] = static_cast<u8>(state[i] + 1);
      state[i] = 0;
    }
  }
  if (!buffers[0].empty() && !buffers[1].empty()) return;
  int bytes = 0, count = 0, first = 0;
  for (int i = 0; i < kStrips; ++i) {
    if (state[i] == 1) {
      bytes += kStripBytes;
      if (count == 0) first = i;
      ++count;
    } else if (bytes != 0) {
      break;
    }
  }
  for (;;) {
    if (count == 0) return;
    if (bytes < 0x8001) break;
    --count;
    bytes -= kStripBytes;
  }
  int b = buffers[0].empty() ? 0 : 1;
  std::vector<u8>& buffer = buffers[b];
  buffer.assign(static_cast<size_t>(bytes), 0);
  // loader_select_entry_at(entry, first * 5) and loader_start_read(buffer, bytes).
  const size_t from = static_cast<size_t>(first) * kStripBytes;
  if (from < picture.size()) {
    const size_t n = std::min(static_cast<size_t>(bytes), picture.size() - from);
    std::memcpy(buffer.data(), picture.data() + from, n);
  }
  for (int i = first; i < first + count; ++i) {
    loaded[i] = static_cast<u8>(b + 1);
    state[i] = static_cast<u8>(b + 2);
    data[i] = buffer.data() + static_cast<size_t>(i - first) * kStripBytes;
  }
}

int PictureViewer::alloc_slot() {
  for (int s = 0; s < kSlots; ++s) {
    if (used[s] == 0) {
      used[s] = 1;
      return s << 4;
    }
  }
  // All taken: the strip outside the view whose VRAM row is farthest from the view's
  // bottom (in picture rows) gives up its slot.
  int best = 0, pick = -1;
  const int top = y >> 4;
  const int bottom = ((y + 0xF) >> 4) + 0x1C;
  for (int i = 0; i < kStrips; ++i) {
    if (i < top || bottom <= i) {
      const int v = vram[i];
      int d = v - bottom * 0x10;
      if (v >= 0) {
        if (d < 0) d = bottom * 0x10 - v;
        if (best < d) {
          best = d;
          pick = i;
        }
      }
    }
  }
  if (pick < 0) return -1;
  const int v = vram[pick];
  vram[pick] = -1;
  used[v >> 4] = 1;
  return v;
}

void PictureViewer::upload() {
  int in0 = 0, in1 = 0;
  for (int i = 0; i < kStrips; ++i) {
    const u8 b = loaded[i];
    if (b == 0) continue;
    if (b & 1) {
      ++in0;
    } else {
      ++in1;
    }
  }
  if (!buffers[0].empty() && in0 == 0) buffers[0] = {};
  if (!buffers[1].empty() && in1 == 0) buffers[1] = {};
  for (int i = 0; i < kStrips; ++i) {
    if (loaded[i] <= 2) continue;
    const int slot = alloc_slot();
    if (slot == -1) return;
    const psyq::RECT r{0x140, static_cast<s16>(slot), 0x140, 0x10};
    psyq::LoadImage(&r, data[i]);
    loaded[i] = 0;
    vram[i] = static_cast<s16>(slot);
    fade[i] = 8;
  }
}

void PictureViewer::draw() {
  const int top = y >> 4;
  const int bottom = ((y + 0xF) >> 4) + 0x1C;
  const int frac = y & 0xF;
  for (int i = top; i < bottom && i < kStrips; ++i) {
    if (vram[i] < 0) {
      want(i);
      continue;
    }
    int c = fade[i] * bright;
    if (c < 0) c += 0xFF;
    const u8 level = static_cast<u8>(c >> 8);
    const s16 sy = static_cast<s16>((i - top) * 0x10 - frac + 0x10);
    for (int k = 0; k < 2; ++k) {
      auto* sp = screen_alloc_prims<psyq::SPRT>(ctx.arena, 1);
      auto* mode = screen_alloc_prims<psyq::DR_MODE>(ctx.arena, 1);
      if (!sp || !mode) return;
      psyq::setSprt(sp);
      sp->r0 = sp->g0 = sp->b0 = level;
      sp->x0 = static_cast<s16>(k == 0 ? 0 : 0xC0);
      sp->y0 = sy;
      sp->u0 = 0;
      sp->v0 = static_cast<u8>(vram[i]);
      sp->clut = 0;
      sp->w = static_cast<s16>(k == 0 ? 0xC0 : 0x80);
      sp->h = 0x10;
      psyq::addPrim(ctx.ot_head, sp);
      const int v = vram[i];
      const int tpage = ((v >> 4) & 0x10) | 0x100 | (k == 0 ? 5 : 8) | ((v & 0x200) << 2);
      psyq::SetDrawMode(mode, 0, 0, tpage, nullptr);
      psyq::addPrim(ctx.ot_head, mode);
    }
    if (fade[i] < 0x80) fade[i] = static_cast<u8>(fade[i] + 8);
  }
  if (auto* area = screen_alloc_prims<psyq::DR_AREA>(ctx.arena, 1)) {
    const psyq::RECT r{0, 0x10, 0x140, 0x1C0};
    psyq::SetDrawArea(area, &r);
    psyq::addPrim(ctx.ot_head, area);
  }
}

// 0xbcc 0x8007e48c: the viewer's own frame loop; each frame's table goes out to both fields.
void show_picture(int entry) {
  auto* v = new PictureViewer(entry);
  psyq::SetDispMask(0);
  int reveal = 2;
  for (;;) {
    screen_flip();
    v->ctx.ot_head = screen_clear_ot(v->ctx.ot);
    if (v->step()) break;
    psyq::DrawSync(0);
    psyq::VSync(0);
    screen_put_env();
    if (reveal > 0 && --reveal == 0) psyq::SetDispMask(1);
    screen_draw_ot(v->ctx.ot);
    psyq::VSync(0);
    screen_put_env();
    screen_draw_ot(v->ctx.ot);
  }
  delete v;
}

// ---- The page (Panel, 0xa4 bytes, vtable 0x8007f380; 0xbcc 0x8007e000) -------------------
class PicturePage final : public menu::Panel {
 public:
  PicturePage() {
    timer_ = 0;
    step_ = 0;
    shown_ = -1;
    flags |= 2;   // no input
    menu::globals().session->free_cache();
  }
  void frame() override;                   // [9] 0x8007e09c

 private:
  void run();                              // 0x8007e170
  s32 timer_ = 0;                          // +0x98
  s32 step_ = 0;                           // +0x9c
  s32 shown_ = -1;                         // +0xa0
};

void PicturePage::frame() {
  switch (step_) {
    case 0:
      timer_ = 0;
      ++step_;
      break;
    case 1:
      if (++timer_ >= 0x1F) {
        timer_ = 0;
        run();
        ++step_;
      }
      break;
    case 2:
      if (++timer_ >= 0x1F) {
        cancel();
        ++step_;
      }
      break;
    default:
      break;
  }
  Panel::frame();
}

void PicturePage::run() {
  menu::MenuScene* scene = menu::globals().scene;
  scene->frame_end();
  // The menu's VRAM the picture's display and strips use.
  static constexpr psyq::RECT kAreas[5] = {
      {0, 0x1C0, 0x140, 0x20}, {0x140, 0x100, 0x140, 0xE0}, {0x200, 0, 0x80, 0x100},
      {0x240, 0x100, 0x40, 0x80}, {0x200, 0x180, 0x80, 0x60}};
  std::vector<u16> saved[5];
  for (int i = 0; i < 5; ++i) {
    saved[i].assign(static_cast<size_t>(kAreas[i].w) * kAreas[i].h, 0);
    psyq::StoreImage(&kAreas[i], saved[i].data());
  }
  psyq::DrawSync(0);
  preload_release(-1);
  // mem_set_alloc_fail_hook(0) around the viewer (boot 0x800134d8): not needed here.
  show_picture(kPictureEntry + menu::globals().session->arg);
  for (int i = 0; i < 2; ++i) screen_dispenv(i)->isinter = 0;
  screen_setup_buffer0(0, 0, 0x140, 0xF0, 0xC, 0xC, 4, 4);
  screen_setup_buffer1(0, 0xD8, 0x140, 0xF0, 0xC, 0xC, 4, 4);
  for (int i = 0; i < 2; ++i) {
    psyq::DRAWENV* d = screen_drawenv(i);
    d->isbg = 1;
    d->dtd = 1;
    d->r0 = d->g0 = d->b0 = 0;
  }
  for (int i = 0; i < 5; ++i) psyq::LoadImage(&kAreas[i], saved[i].data());
  psyq::DrawSync(0);
  scene->frame_begin();
}

}  // namespace

void menu_page13_update(menu::MenuPageLoader* p) {
  switch (p->substate) {
    case 1:
      p->publish_id();
      p->push(new PicturePage);
      break;
    case 2:
    case 3:
      p->pop();
      break;
    default:
      break;
  }
}

}  // namespace ss::game::field
