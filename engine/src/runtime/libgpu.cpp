#include "runtime/libgpu.h"

#include <algorithm>
#include <cstring>
#include <span>
#include <vector>

#include "psx/mdec.h"
#include "runtime/runtime.h"

namespace ss::psyq {
namespace {

psx::Gpu& gpu() { return runtime::Runtime::get().gpu; }

void send(u32 word) { gpu().gp0(std::span<const u32>(&word, 1)); }

u32 texture_window(const RECT& tw) {
  if (tw.w == 0 && tw.h == 0) return 0xE2000000u;
  const u32 mask_x = static_cast<u32>((-tw.w & 0xFF) >> 3), mask_y = static_cast<u32>((-tw.h & 0xFF) >> 3);
  const u32 off_x = static_cast<u32>((tw.x >> 3) & 0x1F), off_y = static_cast<u32>((tw.y >> 3) & 0x1F);
  return 0xE2000000u | mask_x | mask_y << 5 | off_x << 10 | off_y << 15;
}

u32 draw_mode(int tpage, int dtd, int dfe) {
  return 0xE1000000u | (static_cast<u32>(tpage) & 0x9FF) | (dtd ? 1u << 9 : 0) | (dfe ? 1u << 10 : 0);
}

}  // namespace

u16 GetTPage(int tp, int abr, int x, int y) {
  return static_cast<u16>((tp & 3) << 7 | (abr & 3) << 5 | (y & 0x100) >> 4 | (x & 0x3FF) >> 6 | (y & 0x200) << 2);
}

u16 GetClut(int x, int y) { return static_cast<u16>(y << 6 | (x >> 4 & 0x3F)); }

int ResetGraph(int mode) {
  const u32 reset[] = {0xE1000000u, 0xE2000000u, 0xE3000000u, 0xE4000000u, 0xE5000000u, 0xE6000000u};
  for (u32 w : reset) send(w);
  if (mode == 0) runtime::Runtime::get().display_enabled = false;
  return 0;
}

// Matches SetDefDrawEnv as the game ships it (boot 0x80012db0).
DRAWENV* SetDefDrawEnv(DRAWENV* env, int x, int y, int w, int h) {
  env->clip = RECT{static_cast<s16>(x), static_cast<s16>(y), static_cast<s16>(w), static_cast<s16>(h)};
  env->ofs[0] = static_cast<s16>(x);
  env->ofs[1] = static_cast<s16>(y);
  env->tw = RECT{};
  env->tpage = 10;
  env->dtd = 1;
  env->dfe = h != 480;
  env->isbg = 0;
  env->r0 = env->g0 = env->b0 = 0;
  return env;
}

// Matches SetDefDispEnv as the game ships it (boot 0x80012d78).
DISPENV* SetDefDispEnv(DISPENV* env, int x, int y, int w, int h) {
  env->disp = RECT{static_cast<s16>(x), static_cast<s16>(y), static_cast<s16>(w), static_cast<s16>(h)};
  env->screen = RECT{0, 0, 256, 240};
  env->isinter = env->isrgb24 = env->pad0 = env->pad1 = 0;
  return env;
}

DRAWENV* PutDrawEnv(DRAWENV* env) {
  const RECT& c = env->clip;
  const int x1 = std::clamp(c.x + c.w - 1, 0, 1023), y1 = std::clamp(c.y + c.h - 1, 0, 511);
  send(0xE3000000u | static_cast<u32>(c.y & 0x1FF) << 10 | static_cast<u32>(c.x & 0x3FF));
  send(0xE4000000u | static_cast<u32>(y1) << 10 | static_cast<u32>(x1));
  send(0xE5000000u | static_cast<u32>(env->ofs[1] & 0x7FF) << 11 | static_cast<u32>(env->ofs[0] & 0x7FF));
  send(draw_mode(env->tpage, env->dtd, env->dfe));
  send(texture_window(env->tw));
  if (env->isbg) {
    const u32 fill[] = {0x02000000u | env->b0 << 16 | env->g0 << 8 | env->r0,
                        static_cast<u32>(c.y) << 16 | static_cast<u16>(c.x),
                        static_cast<u32>(c.h) << 16 | static_cast<u16>(c.w)};
    gpu().gp0(fill);
  }
  return env;
}

DISPENV* PutDispEnv(DISPENV* env) {
  auto& d = gpu().display();
  d.x = env->disp.x;
  d.y = env->disp.y;
  d.width = env->disp.w;
  d.height = env->disp.h;
  d.top = env->screen.y;
  d.lines = env->screen.h;  // 0: the full height
  d.color24 = env->isrgb24;
  return env;
}

void SetDispMask(int mask) { runtime::Runtime::get().display_enabled = mask != 0; }

void SetDrawMode(DR_MODE* p, int dfe, int dtd, int tpage, const RECT* tw) {
  setlen(p, 2);
  p->code[0] = draw_mode(tpage, dtd, dfe);
  p->code[1] = texture_window(tw ? *tw : RECT{});
}

void SetDrawTPage(DR_TPAGE* p, int dfe, int dtd, int tpage) {
  setlen(p, 1);
  p->code[0] = draw_mode(tpage, dtd, dfe);
}

void SetDrawOffset(DR_OFFSET* p, const u16* ofs) {
  setlen(p, 2);
  p->code[0] = 0xE5000000u | static_cast<u32>(ofs[1] & 0x7FF) << 11 | static_cast<u32>(ofs[0] & 0x7FF);
  p->code[1] = 0;
}

void SetTexWindow(DR_TWIN* p, const RECT* tw) {
  setlen(p, 2);
  p->code[0] = texture_window(tw ? *tw : RECT{});
  p->code[1] = 0;
}

void SetDrawMove(DR_MOVE* p, const RECT* r, int x, int y) {
  setlen(p, r->w != 0 && r->h != 0 ? 5 : 0);
  p->code[0] = 0x01000000;  // flush the texture cache
  p->code[1] = 0x80000000;  // VRAM-to-VRAM copy
  p->code[2] = static_cast<u32>(r->y) << 16 | static_cast<u16>(r->x);
  p->code[3] = static_cast<u32>(y) << 16 | static_cast<u16>(x);
  p->code[4] = static_cast<u32>(r->h) << 16 | static_cast<u16>(r->w);
}

void SetDrawArea(DR_AREA* p, const RECT* r) {
  setlen(p, 2);
  const int x0 = std::clamp<int>(r->x, 0, 1023), y0 = std::clamp<int>(r->y, 0, 511);
  const int x1 = std::clamp(r->x + r->w - 1, 0, 1023), y1 = std::clamp(r->y + r->h - 1, 0, 511);
  p->code[0] = 0xE3000000u | static_cast<u32>(y0) << 10 | static_cast<u32>(x0);
  p->code[1] = 0xE4000000u | static_cast<u32>(y1) << 10 | static_cast<u32>(x1);
}

P_TAG* ClearOTag(P_TAG* ot, int n) {
  for (int i = 0; i < n; ++i) {
    ot[i].len = 0;
    ot[i].next = i + 1 < n ? &ot[i + 1] : nullptr;
  }
  return ot;
}

P_TAG* ClearOTagR(P_TAG* ot, int n) {
  for (int i = 0; i < n; ++i) {
    ot[i].len = 0;
    ot[i].next = i > 0 ? &ot[i - 1] : nullptr;
  }
  return ot;
}

void DrawOTag(P_TAG* ot) {
  for (P_TAG* p = ot; p; p = p->next)
    if (p->len) gpu().gp0_stream(std::span<const u32>(prim_words(p), p->len));
}

int DrawSync(int) { return 0; }  // drawing completes synchronously

int LoadImage(const RECT* r, const void* pixels) {
  gpu().load_image(psx::Rect{r->x, r->y, r->w, r->h}, static_cast<const u16*>(pixels));
  return 0;
}

int StoreImage(const RECT* r, void* pixels) {
  gpu().store_image(psx::Rect{r->x, r->y, r->w, r->h}, static_cast<u16*>(pixels));
  return 0;
}

int MoveImage(const RECT* r, int x, int y) {
  gpu().move_image(psx::Rect{r->x, r->y, r->w, r->h}, x, y);
  return 0;
}

int ClearImage(const RECT* r, u8 red, u8 green, u8 blue) {
  gpu().clear_image(psx::Rect{r->x, r->y, r->w, r->h}, red, green, blue);
  return 0;
}

int VSync(int mode) {
  auto& rt = runtime::Runtime::get();
  if (mode < 0) return static_cast<int>(rt.vblanks());
  if (mode == 1) return 0;  // horizontal blanks since the last vblank: not modelled
  // VSync(n) returns once n vblanks have passed since the previous call; VSync(0) waits for the next one.
  const u64 since = rt.vblanks() - rt.last_vsync();
  u64 wait = mode == 0 ? 1 : (since >= static_cast<u64>(mode) ? 1 : static_cast<u64>(mode) - since);
  while (wait--) rt.vblank();
  rt.set_last_vsync(rt.vblanks());
  return 0;
}

int VSyncCallback(void (*f)()) {
  runtime::Runtime::get().vsync_callback = f;
  return 0;
}

// ---- libpress ----

namespace {

// The host MDEC: the output of the last DecDCTin, in the order the chip produces it
// (macroblock after macroblock, each 16 rows of 16 pixels), and how much of it the
// output DMA has taken.
struct Mdec {
  std::vector<u8> out;
  size_t taken = 0;
  void (*out_callback)() = nullptr;
  int vlc_size = 0xFFFFFF;  // DecDCTvlcSize: unlimited
};
Mdec g_mdec;

// Where the macroblock after 'start' ends in a run of MDEC codes (six blocks, each a
// first code and its codes up to an end-of-block, with end-of-block padding skipped
// as the chip does), or 0 if the codes run out first.
size_t macroblock_end(std::span<const u16> codes, size_t start) {
  size_t i = start;
  for (int block = 0; block < 6; ++block) {
    while (i < codes.size() && codes[i] == psx::mdec::kEndOfBlock) ++i;
    if (i >= codes.size()) return 0;
    ++i;  // first code: quantisation scale and DC
    while (i < codes.size() && codes[i] != psx::mdec::kEndOfBlock) ++i;
    if (i >= codes.size()) return 0;  // unterminated: the chip would wait for more data
    ++i;
  }
  return i;
}

}  // namespace

void DecDCTReset(int mode) {
  // Mode 0 also runs ResetCallback, which only initialises the interrupt system the
  // first time; the host has none to reset.
  if (mode == 0 || mode == 1) {
    g_mdec.out.clear();
    g_mdec.taken = 0;
  }
}

int DecDCTBufSize(const u32* bs) { return static_cast<int>(bs[0] & 0xFFFF); }

int DecDCTvlcSize(int size) {
  const int old = g_mdec.vlc_size;
  g_mdec.vlc_size = size - 1 > 0 ? size * 2 : 0xFFFFFF;
  return old;
}

int DecDCTvlc(const u32* bs, u32* buf, size_t bs_bytes, size_t buf_words, int width, int height) {
  // Continuing a frame split by DecDCTvlcSize (bs == nullptr) is not supported: the
  // host always decodes whole frames.
  if (!bs || buf_words == 0) return 0;
  std::vector<u16> codes;
  psx::mdec::decode_codes(std::span<const u8>(reinterpret_cast<const u8*>(bs), bs_bytes), width, height, codes);
  // The header word doubles as the MDEC command: 0x3800xxxx (decode, 15-bit) and the
  // frame's code words, rounded up, in the low half.
  buf[0] = bs[0];
  const size_t halfwords = std::min(size_t{bs[0] & 0xFFFF}, buf_words - 1) * 2;
  auto* out = reinterpret_cast<u16*>(buf + 1);
  const size_t n = std::min(codes.size(), halfwords);
  std::copy_n(codes.begin(), n, out);
  std::fill(out + n, out + halfwords, psx::mdec::kEndOfBlock);
  return 0;
}

void DecDCTin(u32* buf, int mode) {
  // Bits 27-28 of the command select the output depth (2 = 24-bit, 3 = 15-bit) and
  // bit 25 the mask bit of 15-bit pixels.
  buf[0] = (mode & 1) ? buf[0] & ~(1u << 27) : buf[0] | 1u << 27;
  buf[0] = (mode & 2) ? buf[0] | 1u << 25 : buf[0] & ~(1u << 25);
  const bool depth24 = (buf[0] >> 27 & 3) == 2;
  const bool mask = (buf[0] >> 25 & 1) != 0;
  const std::span<const u16> codes(reinterpret_cast<const u16*>(buf + 1), size_t{buf[0] & 0xFFFF} * 2);
  // A 16-pixel-wide image of macroblocks stacked one under the other is exactly the
  // chip's output order; psx::mdec takes images up to 4096 rows, so 256 at a time.
  std::vector<u8> rgb, part;
  for (size_t at = 0;;) {
    size_t end = at;
    int macroblocks = 0;
    for (size_t next; macroblocks < 256 && (next = macroblock_end(codes, end)) != 0; end = next) ++macroblocks;
    if (macroblocks == 0) break;
    psx::mdec::decode_image(codes.subspan(at, end - at), 16, 16 * macroblocks, part);
    rgb.insert(rgb.end(), part.begin(), part.end());
    at = end;
  }
  if (depth24) {
    g_mdec.out = std::move(rgb);
  } else {
    std::vector<u16> pixels(rgb.size() / 3);
    psx::mdec::to_rgb15(rgb, pixels, mask);
    g_mdec.out.resize(pixels.size() * 2);
    std::memcpy(g_mdec.out.data(), pixels.data(), g_mdec.out.size());
  }
  g_mdec.taken = 0;
}

void DecDCTout(u32* buf, int size) {
  const size_t bytes = static_cast<size_t>(std::max(size, 0)) * 4;
  const size_t n = std::min(bytes, g_mdec.out.size() - std::min(g_mdec.taken, g_mdec.out.size()));
  if (n) std::memcpy(buf, g_mdec.out.data() + g_mdec.taken, n);
  if (n < bytes) std::memset(reinterpret_cast<u8*>(buf) + n, 0, bytes - n);
  g_mdec.taken += n;
  if (g_mdec.out_callback) g_mdec.out_callback();  // the transfer-complete interrupt
}

int DecDCToutCallback(void (*func)()) {
  g_mdec.out_callback = func;
  return 0;
}

}  // namespace ss::psyq
