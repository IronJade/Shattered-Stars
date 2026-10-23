#include "game/field/field_sub_overlays.h"

#include <cstring>

#include "disc/slz.h"
#include "game/boot/loader.h"
#include "game/boot/pad.h"
#include "game/boot/sound.h"
#include "game/common/math.h"
#include "game/common/menu_core.h"
#include "game/common/text_rules.h"
#include "game/common/texture.h"
#include "game/field/actor_base.h"
#include "game/field/actor_field.h"
#include "game/field/actors.h"
#include "game/field/collision.h"
#include "game/field/field.h"
#include "game/field/field_input.h"
#include "game/field/menu_camp.h"
#include "game/field/screen.h"
#include "game/field/script_vm.h"
#include "game/field/tasks.h"
#include "game/field/text_panel.h"
#include "runtime/libgpu.h"
#include "runtime/runtime.h"

namespace ss::game::field {
namespace {

// R3000 division: no trap, a zero divisor gives -1 (or 1 for a negative dividend).
s32 mips_div(s32 n, s32 d) {
  if (d == 0) return n >= 0 ? -1 : 1;
  if (n == INT32_MIN && d == -1) return n;
  return n / d;
}

FieldActorGlobals& fag() { return fieldactor_globals(); }

// ---- #3108 path state: 0x800761ec-0x80076223 ------------------------------------------
// Kept as the console's bytes: the arrays overlap (z[2..3] is the word before the
// running lengths, a fifth point lands on y[0]), and the overlay indexes them freely.
constexpr u32 kPathBase = 0x800761EC;
u8 g_path[0x38]{};

u8* at(u32 address) { return &g_path[address - kPathBase]; }
s16 rd16(u32 address) { return static_cast<s16>(read_u16(at(address))); }
s32 rd32(u32 address) { return static_cast<s32>(read_u32(at(address))); }
void wr16(u32 address, s32 v) {
  const u16 h = static_cast<u16>(v);
  std::memcpy(at(address), &h, 2);
}
void wr32(u32 address, s32 v) { std::memcpy(at(address), &v, 4); }

constexpr u32 kX = 0x800761EC, kY = 0x800761F4, kZ = 0x800761FC;  // s16 points
constexpr u32 kLengths = 0x80076204;  // s32 running length to point i + 1
constexpr u32 kTotal = 0x80076210;
constexpr u32 kAnimFlag = 0x80076214;  // s16: ext 0xea reports whether it is -1
constexpr u32 kAnim = 0x80076216;
constexpr u32 kPos = 0x80076218;       // s32, 20.12 along the path
constexpr u32 kMode = 0x8007621C;      // u8: which pad directions move forward / back
constexpr u32 kStopStart = 0x8007621D, kStopEnd = 0x8007621E;  // u8: stay on the path at that end
constexpr u32 kStartPercent = 0x8007621F;
constexpr u32 kScriptStart = 0x80076220, kScriptEnd = 0x80076222;  // u16 script entries, 0xffff none

s16 count() { return static_cast<s16>(fag().vehicle_control); }  // 0x800761ea (s16 on the console)
void set_count(s32 n) { fag().vehicle_control = static_cast<u8>(n); }

// 0x801f9e50: segment lengths and the start position (a percentage of the length, or 1).
void path_measure() {
  wr32(kTotal, 0);
  for (int i = 0; i < count(); ++i) {
    const s32 dx = rd16(kX + 2 * i) - rd16(kX + 2 * i + 2);
    const s32 dy = rd16(kY + 2 * i) - rd16(kY + 2 * i + 2);
    const s32 dz = rd16(kZ + 2 * i) - rd16(kZ + 2 * i + 2);
    const s32 total = rd32(kTotal) + SquareRoot0(dx * dx + dy * dy + dz * dz);
    wr32(kTotal, total);
    wr32(kLengths + 4 * i, total);
  }
  const u8 percent = *at(kStartPercent);
  wr32(kPos, percent == 0 ? 1 : ((rd32(kTotal) * percent) / 100) << 12);
}

// 0x801f9af0: puts the actor at the path position. At an end (position clamped) the actor
// goes to that point; unless that end holds it (0x8007621d / 0x8007621e) the path ends:
// count 0, animation by direction, shadow back. The end's script entry runs. 1 = held.
int path_update(Actor* a) {
  int end = -1;
  s32 pos = rd32(kPos);
  const s16 n = count();
  if (pos < 0) {
    wr32(kPos, 0);
    end = 0;
  } else if (pos >= rd32(kLengths - 4 + 4 * n) << 12) {
    wr32(kPos, rd32(kLengths - 4 + 4 * n) << 12);
    end = n;
  } else {
    s32 prev = 0;
    for (int i = 0; i < count(); ++i) {
      pos = rd32(kPos);
      const s32 len = rd32(kLengths + 4 * i);
      if (pos >= prev << 12 && pos <= len << 12) {
        s32 t = pos - (prev << 12);
        if (t < 0) t += 0xFF;
        t >>= 8;
        const s32 span = len - prev;
        const s16 x0 = rd16(kX + 2 * i), y0 = rd16(kY + 2 * i), z0 = rd16(kZ + 2 * i);
        const s32 x = (x0 << 12) + (mips_div((rd16(kX + 2 * i + 2) - x0) * t, span) << 8);
        const s32 y = (y0 << 12) + (mips_div((rd16(kY + 2 * i + 2) - y0) * t, span) << 8);
        const s32 z = (z0 << 12) + (mips_div((rd16(kZ + 2 * i + 2) - z0) * t, span) << 8);
        if (z != a->z || x != a->x) a->angle = static_cast<s16>(ratan2(z - a->z, x - a->x));
        a->x = x;
        a->y = y;
        a->z = z;
      }
      prev = len;
    }
    return 0;
  }
  a->x = rd16(kX + 2 * end) << 12;
  a->y = rd16(kY + 2 * end) << 12;
  a->z = rd16(kZ + 2 * end) << 12;
  const s16 last = count();
  const int held = (end == 0 ? *at(kStopStart) : *at(kStopEnd)) != 0 ? 1 : 0;
  if (held == 0) {
    a->anim = -1;
    set_count(0);
    a->flags |= kActorShadow;
  }
  if (end == 0 && static_cast<u16>(rd16(kScriptStart)) != 0xFFFF && g_script_vm != nullptr)
    vm_call_sync(g_script_vm, static_cast<u16>(rd16(kScriptStart)));
  if (end == last && static_cast<u16>(rd16(kScriptEnd)) != 0xFFFF && g_script_vm != nullptr)
    vm_call_sync(g_script_vm, static_cast<u16>(rd16(kScriptEnd)));
  return held;
}

// ---- #3106 mirror lines: 0x800761d4, two slots of 10 bytes ------------------------------
struct MirrorSlot {
  u8 used = 0;     // +0
  u8 layer = 0;    // +1: OT layer of the images
  s16 x0 = 0, z0 = 0, x1 = 0, z1 = 0;  // +2: the mirror line, map units
};
MirrorSlot g_mirrors[2];

// 0x8004185c: (re)creates child 'slot' of an actor as a plain field actor with its sprite.
void actor_create_child(Actor* a, int slot) {
  if (a->children[slot] != nullptr) actor_destroy(a->children[slot], 3);
  a->children[slot] = nullptr;  // (the console leaves the stale pointer on the world map)
  if (globals().field_flags[0x68] != 0) return;
  a->children[slot] = fieldactor_construct(new Actor);
  const s32 pos[3] = {0, 0, 0};
  actor_init(a->children[slot], a->model, pos, 0, 0xFF, 0, -1, 0x1000F);
}

// ---- #3104 rainbow ----------------------------------------------------------------------
// The overlay's data (0x801f9eec-0x801f9f7b): band widths, then R / G / B per band edge
// (nine edges each; the B table's tenth word is 0).
constexpr s32 kRainbowWidths[10] = {5, 6, 7, 3, 1, 2, 1, 2, 3, 0};  // 0x801f9eec
constexpr s32 kRainbowR[9] = {0, 0xFC, 0xFC, 0xFC, 0x3C, 0x3C, 0x3C, 0x44, 0};     // 0x801f9f10
constexpr s32 kRainbowG[9] = {0, 0x3C, 0xB4, 0xFC, 0xB4, 0x64, 0x3C, 0x3C, 0};     // 0x801f9f34
constexpr s32 kRainbowB[9] = {0, 0x3C, 0x3C, 0x3C, 0x3C, 0xFC, 0x84, 0x84, 0};     // 0x801f9f58

struct Rainbow final : Task {  // 0xd4 bytes, vtable 0x801f9edc
  s32 timer = 0;           // +0x08: frames since it appeared (stops counting at 0xc34f)
  s32 delay[25] = {};      // +0x0c (1-24 used): -24 * ray, the ray's fade-in delay
  s32 bright[25] = {};     // +0x70: brightness per ray edge, 0-0x68
  void update() override;  // 0x801f9894
};
Rainbow* g_rainbow = nullptr;  // 0x800752cc

s32 div256(s32 v) { return (v < 0 ? v + 0xFF : v) >> 8; }

// The ten band edges at one angle: radius 250 shrinking by the widths, centre (0xd2, 0xec).
void rainbow_edges(u32 angle, s32 x[10], s32 y[10]) {
  s32 inset = 0;
  for (int k = 0; k < 10; ++k) {
    x[k] = ((cos4096(angle) * (250 - inset)) >> 12) + 0xD2;
    y[k] = ((sin4096(angle) * (250 - inset)) >> 12) + 0xEC;
    inset += kRainbowWidths[k];
  }
}

void Rainbow::update() {
  if (timer == 0) runtime::Runtime::get().trace("rainbow_first_update");
  Screen* s = field_screen();
  if (timer == 0)
    for (int i = 1; i < 25; ++i) delay[i] = -0x18 * i;
  bright[0] = 0;
  for (int i = 1; i < 25; ++i) {
    const s32 v = timer * 2 + delay[i];
    bright[i] = v < 0 ? 0 : (v > 0x68 ? 0x68 : v);
  }
  if (timer <= 0xC34F) timer += globals().frame_ticks;
  s32 x[10], y[10];
  rainbow_edges(0xA64, x, y);
  psyq::P_TAG* entry = s->ot_entry(0x110);
  for (int ray = 0; ray < 24; ++ray) {
    psyq::POLY_G4* p[8];
    for (int b = 0; b < 8; ++b) {
      p[b] = s->alloc_prims<psyq::POLY_G4>();
      if (p[b] == nullptr) return;
      p[b]->x0 = static_cast<s16>(x[b]);
      p[b]->y0 = static_cast<s16>(y[b]);
      p[b]->x2 = static_cast<s16>(x[b + 1]);
      p[b]->y2 = static_cast<s16>(y[b + 1]);
    }
    rainbow_edges(static_cast<u32>(0xA64 + (0x400 * (ray + 1)) / 24), x, y);  // rays 1/24 of 90 degrees
    const s32 near = bright[ray], far = bright[ray + 1];
    for (int b = 0; b < 8; ++b) {
      psyq::POLY_G4* q = p[b];
      q->x1 = static_cast<s16>(x[b]);
      q->y1 = static_cast<s16>(y[b]);
      q->x3 = static_cast<s16>(x[b + 1]);
      q->y3 = static_cast<s16>(y[b + 1]);
      q->r0 = static_cast<u8>(div256(kRainbowR[b] * near));
      q->g0 = static_cast<u8>(div256(kRainbowG[b] * near));
      q->b0 = static_cast<u8>(div256(kRainbowB[b] * near));
      q->r1 = static_cast<u8>(div256(kRainbowR[b] * far));
      q->g1 = static_cast<u8>(div256(kRainbowG[b] * far));
      q->b1 = static_cast<u8>(div256(kRainbowB[b] * far));
      q->r2 = static_cast<u8>(div256(kRainbowR[b + 1] * near));
      q->g2 = static_cast<u8>(div256(kRainbowG[b + 1] * near));
      q->b2 = static_cast<u8>(div256(kRainbowB[b + 1] * near));
      q->r3 = static_cast<u8>(div256(kRainbowR[b + 1] * far));
      q->g3 = static_cast<u8>(div256(kRainbowG[b + 1] * far));
      q->b3 = static_cast<u8>(div256(kRainbowB[b + 1] * far));
      q->code = 0x3A;  // gouraud quad, semi-transparent
      psyq::setlen(q, 8);
      psyq::addPrim(entry, q);
    }
  }
  // Drawn first: additive blending (abr 1), dithered.
  auto* mode = s->alloc_prims<psyq::DR_MODE>();
  if (mode == nullptr) return;
  psyq::setlen(mode, 2);
  mode->code[0] = 0xE1000220;
  mode->code[1] = 0;
  psyq::addPrim(entry, mode);
}

// ---- #3109 gauge -------------------------------------------------------------------------
struct Gauge {             // 0xb0 bytes, vtable 0x801fa00c at +0xac
  s32 x = 0, y = 0;        // +0x04 / +0x08
  TextPanel panels[4];     // +0x0c: "TIME", "PRESS", the time, the count
};
Gauge* g_gauge = nullptr;  // 0x800752e8
std::vector<u8> g_item_records;  // 0x800756cc

s32 gauge_press() { return static_cast<s32>(read_u32(globals().field_flags + 0x38)); }  // vtable +0x08: 0x801f9964
bool gauge_running() { return ((globals().script_flags[2] >> 3) & 1) != 0; }            // vtable +0x10: 0x801f997c

// The panel at (x, y), frameless, with its cursor at the start.
void gauge_panel(TextPanel* p, int x, int y) {
  text_panel_open(p, x, y, 0, 0);
  p->frame_visible = 0;
  p->text_y = 0;
  p->text_x = 0;
  p->max_width = 0x10;
}

// An 80-pixel-wide row's bar: POLY_F4 (semi-transparent) and the blend it is drawn with,
// into screen OT entry 0x810.
void gauge_bar(Screen* s, u32 colour, int x0, int x1, int y, u32 mode) {
  auto* q = s->alloc_prims<psyq::POLY_F4>();
  if (q == nullptr) return;
  psyq::setlen(q, 5);
  std::memcpy(&q->r0, &colour, 4);
  q->x0 = q->x2 = static_cast<s16>(x0);
  q->x1 = q->x3 = static_cast<s16>(x1);
  q->y0 = q->y1 = static_cast<s16>(y);
  q->y2 = q->y3 = static_cast<s16>(y + 0xC);
  psyq::addPrim(s->ot_entry(0x810), q);
  auto* m = s->alloc_prims<psyq::DR_MODE>();
  if (m == nullptr) return;
  const psyq::RECT tw{};
  psyq::SetDrawMode(m, 1, 0, static_cast<int>(mode), &tw);
  psyq::addPrim(s->ot_entry(0x810), m);
}

// ---- #3105 final screen -----------------------------------------------------------------
// The overlay's tables (0x801fa53c-0x801fa557), per variant n: where the caption starts,
// its first row in the caption texture, and its length in pixels.
constexpr s16 kEndCaptionX[3] = {20, 120, 55};    // 0x801fa53c (+4n)
constexpr s16 kEndCaptionY[3] = {220, 220, 220};  // 0x801fa53e (+4n)
constexpr s16 kEndCaptionRow[3] = {0, 3, 5};      // 0x801fa548: rows of 255 x 22 pixels
constexpr s16 kEndCaptionLength[3] = {566, 367, 487};  // 0x801fa550
std::vector<u8> g_end_music;  // 0x801fa558: the music the screen starts

constexpr u16 kCaptionTpage = 0x2A;   // x 0x280, 4-bit, additive
constexpr u16 kCaptionClut = 0x7EC0;  // (0, 0x1fb)
constexpr u16 kLogoTpage = 0x12B;     // x 0x2c0, 15-bit, additive

// 0x801f9800: the caption sheet (#0x10ef, 4-bit with its CLUT) to x 0x280, the logo
// (#0x10f0, read with it in one transfer) to x 0x2c0, then the sound bank and music of
// entry #0xa73.
void end_load() {
  FieldGlobals& g = globals();
  psx::Gpu& gpu = runtime::Runtime::get().gpu;
  auto read = [&](int entry) {
    std::vector<u8> v(static_cast<size_t>(g.loader->entry_size(entry)), 0);
    g.loader->load_sync(v.data(), entry, 0);
    return v;
  };
  if (auto img = disc::slz::unpack_block(read(0x10EF), 0, 0, nullptr)) {
    const u8* p = img->data();
    const int depth = read_u16(p) & 3;
    const int cluts = read_u16(p + 2);
    p += 4;
    upload_clut256(gpu, reinterpret_cast<const u16*>(p), 0, 0x1FB);
    p += cluts * 2;
    upload_texture(gpu, reinterpret_cast<const u16*>(p + 4), depth, 0, 0x280, 0, read_u16(p) << (2 - depth),
                   read_u16(p + 2));
  }
  psyq::DrawSync(0);
  if (auto img = disc::slz::unpack_block(read(0x10F0), 0, 0, nullptr)) {
    const u8* p = img->data();
    const int depth = read_u16(p) & 3;
    p += 6;
    upload_texture(gpu, reinterpret_cast<const u16*>(p + 4), depth, 0, 0x2C0, 0, read_u16(p) << (2 - depth),
                   read_u16(p + 2));
  }
  psyq::DrawSync(0);
  std::vector<u8> sound = read(0xA73);
  sound_load_bank(g.sound, sound.data() + read_u32(sound.data()), 0, 0);
  g_end_music.assign(0x4000, 0);
  if (auto music = disc::slz::unpack_block(std::span<const u8>(sound).subspan(4), 0, 0, nullptr))
    std::copy_n(music->begin(), std::min(music->size(), g_end_music.size()), g_end_music.begin());
  while (sound_bank_busy(g.sound) != 0) {
  }
}

psyq::POLY_GT4* end_gt4(Screen* s) {
  auto* p = s->alloc_prims<psyq::POLY_GT4>();
  if (p == nullptr) return nullptr;
  psyq::setlen(p, 0xC);
  p->code = 0x3E;  // gouraud, textured, semi-transparent
  p->tpage = kCaptionTpage;
  p->clut = kCaptionClut;
  return p;
}

psyq::POLY_FT4* end_ft4(Screen* s, u8 grey, u16 tpage) {
  auto* p = s->alloc_prims<psyq::POLY_FT4>();
  if (p == nullptr) return nullptr;
  psyq::setlen(p, 9);
  p->code = 0x2E;  // textured, semi-transparent
  p->r0 = p->g0 = p->b0 = grey;
  p->tpage = tpage;
  return p;
}

void set_xy(psyq::POLY_FT4* p, int l, int t, int r, int b) {
  p->x0 = p->x2 = static_cast<s16>(l);
  p->x1 = p->x3 = static_cast<s16>(r);
  p->y0 = p->y1 = static_cast<s16>(t);
  p->y2 = p->y3 = static_cast<s16>(b);
}

void set_uv(psyq::POLY_FT4* p, int u0, int v0, int u1, int v1) {
  p->u0 = p->u2 = static_cast<u8>(u0);
  p->u1 = p->u3 = static_cast<u8>(u1);
  p->v0 = p->v1 = static_cast<u8>(v0);
  p->v2 = p->v3 = static_cast<u8>(v1);
}

}  // namespace

// ---- #3105 ----------------------------------------------------------------------------

void the_end(int n) {
  runtime::Runtime::get().trace("the_end");
  FieldGlobals& g = globals();
  Screen* s = field_screen();
  sound_stop(g.sound);
  const psyq::RECT all{0, 0, 0x280, 0x1E0};
  psyq::ClearImage(&all, 0, 0, 0);
  psyq::DRAWENV draw{};
  psyq::SetDefDrawEnv(&draw, 0, 0, 0x280, 0x1E0);
  draw.isbg = 1;
  draw.r0 = draw.g0 = draw.b0 = 0;
  psyq::DISPENV disp{};
  disp.disp = all;
  disp.screen = psyq::RECT{0, 0, 0x100, 0xF0};
  disp.isinter = 1;
  disp.isrgb24 = 0;
  screen_layout_arena(s, 1);
  psyq::PutDrawEnv(&draw);
  psyq::PutDispEnv(&disp);
  psyq::ResetGraph(1);
  // Host: the runtime's ResetGraph resets the drawing state for every mode; the
  // console's mode 1 only cancels drawing, so the environment put above stays.
  psyq::PutDrawEnv(&draw);
  psyq::SetDispMask(1);
  end_load();
  const int x0 = kEndCaptionX[n], y0 = kEndCaptionY[n], row0 = kEndCaptionRow[n], length = kEndCaptionLength[n];
  int shown = 0;    // s4: pixels of the caption typed in so far (3 a frame)
  int frames = 0;   // stops counting at a million
  bool music = true;
  for (;;) {
    screen_flip(s);
    screen_clear_ot(s);
    psyq::P_TAG* entry = s->ot_entry(0x82E);
    int full = length;  // caption drawn at full brightness
    if (shown < length) {
      full = shown;
      // The typing edge: 30 pixels fading from grey 0x80 to black. Rows of the caption
      // texture are 255 pixels; an edge that crosses into the next row takes two quads.
      const int edge_end = std::min(shown + 0x1E, length);
      const int row = shown / 255, u = shown - row * 255;
      if (u < 0xE1) {
        if (auto* p = end_gt4(s)) {
          p->r0 = p->g0 = p->b0 = 0x80;
          p->r2 = p->g2 = p->b2 = 0x80;
          p->x0 = p->x2 = static_cast<s16>(x0 + shown);
          p->x1 = p->x3 = static_cast<s16>(x0 + shown + 0x1E);
          p->y0 = p->y1 = static_cast<s16>(y0);
          p->y2 = p->y3 = static_cast<s16>(y0 + 0x16);
          const int v = (row0 + row) * 0x16;
          p->u0 = p->u2 = static_cast<u8>(u);
          p->u1 = p->u3 = static_cast<u8>(u + 0x1E);
          p->v0 = p->v1 = static_cast<u8>(v);
          p->v2 = p->v3 = static_cast<u8>(v + 0x16);
          psyq::addPrim(entry, p);
        }
      } else {
        const int mid = ((u - 0xE1) * 0x80) / 30;  // brightness at the row's end
        int at = shown;
        for (int k = 0; k < 2; ++k) {
          auto* p = end_gt4(s);
          if (p == nullptr) break;
          const u8 left = k == 0 ? 0x80 : static_cast<u8>(mid);
          const u8 right = k == 0 ? static_cast<u8>(mid) : 0;
          p->r0 = p->g0 = p->b0 = p->r2 = p->g2 = p->b2 = left;
          p->r1 = p->g1 = p->b1 = p->r3 = p->g3 = p->b3 = right;
          const int width = k == 0 ? 0xFF - u : edge_end % 255;
          p->x0 = p->x2 = static_cast<s16>(x0 + at);
          p->x1 = p->x3 = static_cast<s16>(x0 + at + width);
          p->y0 = p->y1 = static_cast<s16>(y0);
          p->y2 = p->y3 = static_cast<s16>(y0 + 0x16);
          const int u0 = k == 0 ? at % 255 : 0;
          const int v = (row0 + at / 255) * 0x16;
          p->u0 = p->u2 = static_cast<u8>(u0);
          p->u1 = p->u3 = static_cast<u8>(k == 0 ? u0 + width : width);
          p->v0 = p->v1 = static_cast<u8>(v);
          p->v2 = p->v3 = static_cast<u8>(v + 0x16);
          psyq::addPrim(entry, p);
          at += width;
        }
      }
    }
    // The typed part, a row of the texture (255 pixels) at a time.
    int x = x0, done = 0, v = row0 * 0x16;
    for (int k = 0; k < (n == 0 ? 3 : 2); ++k, v += 0x16) {
      int w = full - done;
      if (w >= 0x100) w = 0xFF;
      done += w;
      if (w < 1) break;
      auto* p = end_ft4(s, 0x80, kCaptionTpage);
      if (p == nullptr) break;
      p->clut = kCaptionClut;
      set_xy(p, x, y0, x + w, y0 + 0x16);
      set_uv(p, 0, v, w, v + 0x16);
      psyq::addPrim(entry, p);
      x += 0xFF;
    }
    // 150 pixels' worth after the caption: the logo and "fin." fade in over 60.
    if (shown > length + 0x96) {
      const int t = std::min(shown - 0x96 - length, 0x3C);
      const u8 grey = static_cast<u8>((t * 0x80) / 60);
      if (auto* p = end_ft4(s, grey, kLogoTpage)) {
        set_xy(p, 0x44, 0x78, 0x13B, 0xC8);
        set_uv(p, 8, 0, 0xFF, 0x50);
        psyq::addPrim(entry, p);
      }
      if (auto* p = end_ft4(s, grey, kLogoTpage)) {
        set_xy(p, 0x13A, 0x78, 0x239, 0xD0);
        set_uv(p, 0, 0x50, 0xFF, 0xA8);
        psyq::addPrim(entry, p);
      }
      if (auto* p = end_ft4(s, grey, kLogoTpage)) {
        // The release's own "fin." art (NA x1 0x26c, u1 0x88).
        const text::Layout& lay = text::layout(text::lang());
        set_xy(p, 0x16D, 0x16D, lay.end_logo3_x1, 0x1BD);
        set_uv(p, 0, 0xA8, lay.end_logo3_u1, 0xF8);
        psyq::addPrim(entry, p);
      }
    }
    if (shown > length + 0xFA && music) {
      music = false;
      sound_resume_sequence(g.sound, g_end_music.data(), 0x168, 0);
    }
    // Both fields of the interlaced frame: clear, draw.
    for (int field = 0; field < 2; ++field) {
      psyq::VSync(0);
      psyq::ClearImage(&all, 0, 0, 0);
      screen_compact_ot(s);
      psyq::DrawOTag(s->current->ot.data());
      if (field == 0) psyq::DrawSync(0);
    }
    if (frames < 1000000) {
      shown += 3;
      ++frames;
    }
  }
}

// ---- #3109 ----------------------------------------------------------------------------

bool gauge_exists() { return g_gauge != nullptr; }

void gauge_create(int x, int y) {
  if (g_gauge != nullptr) return;
  g_gauge = new Gauge;  // TextPanel(): frame and text on, line height 12, cursor cleared
  g_gauge->x = x;
  g_gauge->y = y;
}

void gauge_destroy() {
  delete g_gauge;  // the panels' destructors leave the task list
  g_gauge = nullptr;
}

int gauge_seconds_left() {
  const s32 start = field_gauge_clock();
  if (start == -1) return 300;
  const s32 left = 300 - static_cast<s32>(clock_seconds() - static_cast<u32>(start));
  return left < 0 ? 0 : left;
}

void gauge_update() {
  if (g_gauge == nullptr) return;
  Gauge& o = *g_gauge;
  if (gauge_running() && field_gauge_clock() == -1) field_set_gauge_clock(static_cast<s32>(clock_seconds()));
  const s32 left = gauge_seconds_left();
  gauge_panel(&o.panels[0], o.x - 8, o.y - 6);
  text_panel_print(&o.panels[0], "TIME", 2);
  gauge_panel(&o.panels[1], o.x - 8, o.y + 10);
  text_panel_print(&o.panels[1], "PRESS", 2);
  Screen* s = field_screen();
  for (int row = 0; row < 2; ++row) {
    char text[16] = {};
    s32 length;
    if (row == 0) {
      length = (left * 80) / 300;
      const s32 minutes = left / 60;
      char seconds[8] = {};
      menu::format_decimal(text, minutes, 0, ' ');
      menu::str_cat(text, ":");
      menu::format_decimal(seconds, left - minutes * 60, 2, '0');
      menu::str_cat(text, seconds);
      gauge_panel(&o.panels[2], o.x + 0x3A, o.y - 6);
      text_panel_print(&o.panels[2], text, 0);
    } else {
      length = static_cast<s32>(static_cast<u32>(gauge_press()) * 80u) / 100;
      menu::format_decimal(text, gauge_press(), 0, ' ');
      gauge_panel(&o.panels[3], o.x + 0x42, o.y + 0xA);
      text_panel_print(&o.panels[3], text, 0);
    }
    const int top = o.y + row * 16;
    gauge_bar(s, 0x2A8020FF, o.x + 0x2A, o.x + 0x2A + length, top, 0x20);  // additive
    gauge_bar(s, 0x2A507090, o.x + 0x2A, o.x + 0x7A, top, 0);              // 50%
  }
  auto* m = s->alloc_prims<psyq::DR_MODE>();
  if (m == nullptr) return;
  const psyq::RECT tw{};
  psyq::SetDrawMode(m, 1, 0, 0x20, &tw);
  psyq::addPrim(s->ot_entry(0x810), m);
}

int item_record_byte_19(int item) {
  // inventory_item_data(inv, item, 0x19): a u8 field of the 0x30-byte record item - 1.
  const size_t at = static_cast<size_t>(item - 1) * 0x30 + 0x19;
  if (item < 1 || at >= g_item_records.size()) return 0;
  return g_item_records[at];
}

void sub_set_item_records(std::vector<u8> data) { g_item_records = std::move(data); }

// ---- #3104 ----------------------------------------------------------------------------

void rainbow_create() {
  // The console overwrites 0x800752cc if one exists (the old task keeps drawing until
  // the next map load frees only the newest); the host replaces it.
  rainbow_destroy();
  g_rainbow = new Rainbow;
  task_register(g_rainbow, 1);
}

void rainbow_destroy() {
  if (g_rainbow == nullptr) return;
  task_unregister(g_rainbow);
  delete g_rainbow;
  g_rainbow = nullptr;
}

// ---- #3108 ----------------------------------------------------------------------------

void path_start(ScriptCtx* ctx) {
  vm_fetch_args(ctx, 10);
  const s32* v = g_vm_args;
  set_count(1);
  *at(kStopStart) = 0;
  *at(kStopEnd) = 0;
  wr16(kScriptStart, 0xFFFF);
  wr16(kScriptEnd, 0xFFFF);
  wr16(kX, v[0]);
  wr16(kY, v[1]);
  wr16(kZ, v[2]);
  wr16(kX + 2, v[3]);
  wr16(kY + 2, v[4]);
  wr16(kZ + 2, v[5]);
  wr16(kAnimFlag, v[6]);
  wr16(kAnim, v[7]);
  *at(kMode) = static_cast<u8>(v[8]);
  *at(kStartPercent) = static_cast<u8>(v[9]);
  if (Actor* leader = field_actor(globals().game_state[0x24])) actor_set_anim(leader, static_cast<s16>(v[7]));
  path_measure();
}

void path_add_point(ScriptCtx* ctx) {
  vm_fetch_args(ctx, 3);
  const s16 n = static_cast<s16>(count() + 1);
  wr16(kX + 2 * n, g_vm_args[0]);
  wr16(kY + 2 * n, g_vm_args[1]);
  set_count(n);
  wr16(kZ + 2 * n, g_vm_args[2]);
  path_measure();
}

void path_control(Actor* a, s32 ax, s32 ay) {
  FieldGlobals& g = globals();
  a->speed *= g.frame_ticks;
  int dir;  // 0-7 clockwise from up, -1 none
  if (a->analog != 0) {
    dir = ((ratan2(ay, ax) + 0x500) & 0xFFF) >> 9;
  } else {
    dir = field_pad_direction();
  }
  a->walked = 1;
  const u8 mode = *at(kMode);
  // Running doubles nothing here: modes 4-7 (four-way paths) take the run speed.
  auto run_speed = [&] {
    if ((g.held[0] & read_u16(g.config + 2)) != 0 && ((g.script_flags[1] >> 2) & 1) == 0) a->speed = a->walk_speed << 2;
  };
  int step = 0;  // +1 forward, -1 back, 0 not moving
  const u32 d = static_cast<u32>(dir);
  switch (mode) {
    case 0: case 1:  // up / down
      if (d == 0) step = mode == 0 ? 1 : -1;
      else if (d == 4) step = mode == 0 ? -1 : 1;
      break;
    case 2: case 3:  // left / right
      if (d == 6) step = mode == 2 ? -1 : 1;
      else if (d == 2) step = mode == 2 ? 1 : -1;
      break;
    case 4: case 5:  // up half / down half
      run_speed();
      if (d < 3) step = mode == 4 ? 1 : -1;
      else if (d - 4 < 3) step = mode == 4 ? -1 : 1;
      break;
    case 6: case 7:  // left half / right half
      run_speed();
      if (dir == 0 || dir >= 6) step = mode == 6 ? 1 : -1;
      else if (d - 2 < 3) step = mode == 6 ? -1 : 1;
      break;
    default:
      break;
  }
  if (step == 0) {
    a->walked = 0;
  } else {
    wr32(kPos, rd32(kPos) + step * a->speed);
  }
  walk_snap_to_floor(a);
  if (path_update(a) != 0) a->walked = 0;
  if (a->walked != 0) actor_anim_advance(a);
}

void path_set_end_scripts(int start, int end) {
  wr16(kScriptStart, start);
  wr16(kScriptEnd, end);
}

void path_set_end_stops(int start, int end) {
  *at(kStopStart) = static_cast<u8>(start);
  *at(kStopEnd) = static_cast<u8>(end);
}

int path_count() { return count(); }
int path_anim_flag() { return rd16(kAnimFlag); }

// ---- #3106 ----------------------------------------------------------------------------

void mirror_add(ScriptCtx* ctx) {
  vm_fetch_args(ctx, 5);
  fag().children_pass = 1;
  int slot = 0;
  while (slot < 2 && g_mirrors[slot].used != 0) ++slot;
  if (slot == 2) return;
  MirrorSlot& m = g_mirrors[slot];
  m.used = 1;
  m.x0 = static_cast<s16>(g_vm_args[0]);
  m.z0 = static_cast<s16>(g_vm_args[1]);
  m.x1 = static_cast<s16>(g_vm_args[2]);
  m.z1 = static_cast<s16>(g_vm_args[3]);
  m.layer = static_cast<u8>(g_vm_args[4]);
}

void mirror_update(Actor* a) {
  if (a->flags & kActorFixedLight) return;  // an image itself (0x8000000)
  const s32 px = a->x >> 12, pz = a->z >> 12;
  for (int slot = 0; slot < 2; ++slot) {
    const MirrorSlot& m = g_mirrors[slot];
    if (m.used == 0) continue;
    auto hide = [&] {
      if (a->children[slot] != nullptr) actor_or_flags(a->children[slot], 0, kActorHidden);
    };
    if (a->anim != -1) {
      hide();
      continue;
    }
    // The foot of the perpendicular from the actor to the line, as a fraction (4096 =
    // the whole segment); beyond the segment the image is hidden.
    const s32 lx = m.x1 - m.x0, lz = m.z1 - m.z0;
    const s32 len2 = lx * lx + lz * lz;
    if (len2 == 0) continue;  // the console traps (break) on a zero-length line
    const u32 dot = static_cast<u32>((px - m.x0) * lx) + static_cast<u32>((pz - m.z0) * lz);
    const s32 t = mips_div(static_cast<s32>(dot << 12), len2);
    if (static_cast<u32>(t) > 0x1000) {
      hide();
      continue;
    }
    s32 fx = lx * t;
    if (fx < 0) fx += 0xFFF;
    fx = (fx >> 12) + m.x0;
    s32 fz = lz * t;
    if (fz < 0) fz += 0xFFF;
    fz = (fz >> 12) + m.z0;
    if (a->children[slot] == nullptr) actor_create_child(a, slot);
    Actor* c = a->children[slot];
    if (c == nullptr) continue;
    const s32 dx = px - fx, dz = pz - fz;
    const s32 heading = ratan2(dz, dx);
    const s32 dist = SquareRoot0(dx * dx + dz * dz);
    const u32 back = static_cast<u32>(heading + 0x800);
    s32 ox = cos4096(back) * dist;
    if (ox < 0) ox += 0xFFF;
    s32 oz = sin4096(back) * dist;
    if (oz < 0) oz += 0xFFF;
    // The console also copies a stack word into +0x0c (w); the host leaves it.
    c->x = (fx + (ox >> 12)) << 12;
    c->y = a->y;
    c->z = (fz + (oz >> 12)) << 12;
    c->angle = static_cast<s16>(heading - (((a->angle & 0xFFF) - heading) - 0x800));
    actor_or_flags(c, 0, 0x08000011);
    c->layer = m.layer;
    c->anim_timer = a->anim_timer;
    c->walked = a->walked_last;
    std::memcpy(c->light, a->light, 3);
    actor_mark_dirty(c);
    actor_and_flags(c, 0, ~static_cast<u32>(kActorHidden));
    if (a->flags & kActorRunning) {
      actor_or_flags(c, 0, kActorRunning);
    } else {
      actor_and_flags(c, 0, ~static_cast<u32>(kActorRunning));
    }
  }
}

void sub_overlays_map_reset() {
  rainbow_destroy();
  gauge_destroy();               // 0x800752e8: 0x801f98c0(obj, 3)
  g_item_records = {};           // 0x800756cc
  g_mirrors[0].used = 0;
  g_mirrors[1].used = 0;
  fag().children_pass = 0;
  set_count(0);
}

}  // namespace ss::game::field
