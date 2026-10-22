#pragma once

#include <cstddef>

#include "core/types.h"

// PsyQ libgpu / libetc, reimplemented on the native runtime. Structures keep
// Sony's field layout so ported code reads like the original, with one change:
// a primitive's tag holds a host pointer to the next primitive instead of a
// 24-bit RAM address, and the GP0 words follow the tag directly.
namespace ss::psyq {

struct RECT {
  s16 x = 0, y = 0, w = 0, h = 0;
};

struct P_TAG {
  P_TAG* next = nullptr;  // next primitive / ordering-table entry; nullptr ends the list
  u32 len = 0;            // GP0 words that follow this tag
};
inline u32* prim_words(P_TAG* p) { return reinterpret_cast<u32*>(p + 1); }

struct POLY_FT4 {
  P_TAG tag;
  u8 r0, g0, b0, code;
  s16 x0, y0;
  u8 u0, v0;
  u16 clut;
  s16 x1, y1;
  u8 u1, v1;
  u16 tpage;
  s16 x2, y2;
  u8 u2, v2;
  u16 pad1;
  s16 x3, y3;
  u8 u3, v3;
  u16 pad2;
};

struct LINE_G2 {
  P_TAG tag;
  u8 r0, g0, b0, code;
  s16 x0, y0;
  u8 r1, g1, b1, p1;
  s16 x1, y1;
};

struct LINE_F2 {
  P_TAG tag;
  u8 r0, g0, b0, code;
  s16 x0, y0;
  s16 x1, y1;
};

struct TILE {
  P_TAG tag;
  u8 r0, g0, b0, code;
  s16 x0, y0;
  s16 w, h;
};

struct POLY_G3 {
  P_TAG tag;
  u8 r0, g0, b0, code;
  s16 x0, y0;
  u8 r1, g1, b1, pad1;
  s16 x1, y1;
  u8 r2, g2, b2, pad2;
  s16 x2, y2;
};

struct POLY_G4 {
  P_TAG tag;
  u8 r0, g0, b0, code;
  s16 x0, y0;
  u8 r1, g1, b1, pad1;
  s16 x1, y1;
  u8 r2, g2, b2, pad2;
  s16 x2, y2;
  u8 r3, g3, b3, pad3;
  s16 x3, y3;
};

struct SPRT {
  P_TAG tag;
  u8 r0, g0, b0, code;
  s16 x0, y0;
  u8 u0, v0;
  u16 clut;
  s16 w, h;
};

struct SPRT_8 {
  P_TAG tag;
  u8 r0, g0, b0, code;
  s16 x0, y0;
  u8 u0, v0;
  u16 clut;
};

struct DR_MODE {
  P_TAG tag;
  u32 code[2];
};

struct DR_AREA {
  P_TAG tag;
  u32 code[2];
};

struct DR_ENV {
  P_TAG tag;
  u32 code[15];
};

struct DRAWENV {
  RECT clip;
  s16 ofs[2];
  RECT tw;
  u16 tpage;
  u8 dtd, dfe, isbg, r0, g0, b0;
  DR_ENV dr_env;
};

struct DISPENV {
  RECT disp;
  RECT screen;
  u8 isinter, isrgb24, pad0, pad1;
};

static_assert(offsetof(POLY_FT4, r0) == sizeof(P_TAG), "GP0 words must follow the tag");
// Host structures may be padded at the end; the GP0 words themselves must be contiguous.
static_assert(offsetof(POLY_FT4, pad2) + sizeof(u16) - sizeof(P_TAG) == 9 * 4, "POLY_FT4 carries 9 words");
static_assert(offsetof(LINE_G2, y1) + sizeof(s16) - sizeof(P_TAG) == 4 * 4, "LINE_G2 carries 4 words");
static_assert(offsetof(POLY_G3, y2) + sizeof(s16) - sizeof(P_TAG) == 6 * 4, "POLY_G3 carries 6 words");
static_assert(offsetof(POLY_G4, y3) + sizeof(s16) - sizeof(P_TAG) == 8 * 4, "POLY_G4 carries 8 words");
static_assert(offsetof(SPRT, h) + sizeof(s16) - sizeof(P_TAG) == 4 * 4, "SPRT carries 4 words");
static_assert(offsetof(SPRT_8, clut) + sizeof(u16) - sizeof(P_TAG) == 3 * 4, "SPRT_8 carries 3 words");

inline void setlen(void* p, u32 len) { static_cast<P_TAG*>(p)->len = len; }
inline void addPrim(P_TAG* ot, void* prim) {
  auto* p = static_cast<P_TAG*>(prim);
  p->next = ot->next;
  ot->next = p;
}
inline void termPrim(P_TAG* p) { p->next = nullptr; }

// The rest of PsyQ's primitives (field program and later). Field order as in libgpu.h.
struct POLY_F3 {
  P_TAG tag;
  u8 r0, g0, b0, code;
  s16 x0, y0, x1, y1, x2, y2;
};

struct POLY_F4 {
  P_TAG tag;
  u8 r0, g0, b0, code;
  s16 x0, y0, x1, y1, x2, y2, x3, y3;
};

struct POLY_FT3 {
  P_TAG tag;
  u8 r0, g0, b0, code;
  s16 x0, y0;
  u8 u0, v0;
  u16 clut;
  s16 x1, y1;
  u8 u1, v1;
  u16 tpage;
  s16 x2, y2;
  u8 u2, v2;
  u16 pad1;
};

struct POLY_GT3 {
  P_TAG tag;
  u8 r0, g0, b0, code;
  s16 x0, y0;
  u8 u0, v0;
  u16 clut;
  u8 r1, g1, b1, p1;
  s16 x1, y1;
  u8 u1, v1;
  u16 tpage;
  u8 r2, g2, b2, p2;
  s16 x2, y2;
  u8 u2, v2;
  u16 pad2;
};

struct POLY_GT4 {
  P_TAG tag;
  u8 r0, g0, b0, code;
  s16 x0, y0;
  u8 u0, v0;
  u16 clut;
  u8 r1, g1, b1, p1;
  s16 x1, y1;
  u8 u1, v1;
  u16 tpage;
  u8 r2, g2, b2, p2;
  s16 x2, y2;
  u8 u2, v2;
  u16 pad2;
  u8 r3, g3, b3, p3;
  s16 x3, y3;
  u8 u3, v3;
  u16 pad3;
};

struct LINE_F3 {
  P_TAG tag;
  u8 r0, g0, b0, code;
  s16 x0, y0, x1, y1, x2, y2;
  u32 pad;  // 0x55555555 terminator
};

struct LINE_G3 {
  P_TAG tag;
  u8 r0, g0, b0, code;
  s16 x0, y0;
  u8 r1, g1, b1, p1;
  s16 x1, y1;
  u8 r2, g2, b2, p2;
  s16 x2, y2;
  u32 pad;
};

struct LINE_F4 {
  P_TAG tag;
  u8 r0, g0, b0, code;
  s16 x0, y0, x1, y1, x2, y2, x3, y3;
  u32 pad;
};

struct LINE_G4 {
  P_TAG tag;
  u8 r0, g0, b0, code;
  s16 x0, y0;
  u8 r1, g1, b1, p1;
  s16 x1, y1;
  u8 r2, g2, b2, p2;
  s16 x2, y2;
  u8 r3, g3, b3, p3;
  s16 x3, y3;
  u32 pad;
};

struct SPRT_16 {
  P_TAG tag;
  u8 r0, g0, b0, code;
  s16 x0, y0;
  u8 u0, v0;
  u16 clut;
};

struct TILE_1 {
  P_TAG tag;
  u8 r0, g0, b0, code;
  s16 x0, y0;
};
using TILE_8 = TILE_1;
using TILE_16 = TILE_1;

struct DR_TWIN {
  P_TAG tag;
  u32 code[2];
};
struct DR_OFFSET {
  P_TAG tag;
  u32 code[2];
};
struct DR_TPAGE {
  P_TAG tag;
  u32 code[1];
};
struct DR_STP {
  P_TAG tag;
  u32 code[2];
};
struct DR_MOVE {
  P_TAG tag;
  u32 code[5];
};
struct DR_LOAD {
  P_TAG tag;
  u32 code[3];
  u32 p[13];
};

static_assert(offsetof(POLY_F3, y2) + sizeof(s16) - sizeof(P_TAG) == 4 * 4, "POLY_F3 carries 4 words");
static_assert(offsetof(POLY_F4, y3) + sizeof(s16) - sizeof(P_TAG) == 5 * 4, "POLY_F4 carries 5 words");
static_assert(offsetof(POLY_FT3, pad1) + sizeof(u16) - sizeof(P_TAG) == 7 * 4, "POLY_FT3 carries 7 words");
static_assert(offsetof(POLY_GT3, pad2) + sizeof(u16) - sizeof(P_TAG) == 9 * 4, "POLY_GT3 carries 9 words");
static_assert(offsetof(POLY_GT4, pad3) + sizeof(u16) - sizeof(P_TAG) == 12 * 4, "POLY_GT4 carries 12 words");
static_assert(offsetof(LINE_G4, pad) + sizeof(u32) - sizeof(P_TAG) == 9 * 4, "LINE_G4 carries 9 words");

// PsyQ's set* macros: length and command byte.
inline void setPolyF3(POLY_F3* p) { setlen(p, 4), p->code = 0x20; }
inline void setPolyFT3(POLY_FT3* p) { setlen(p, 7), p->code = 0x24; }
inline void setPolyG3(POLY_G3* p) { setlen(p, 6), p->code = 0x30; }
inline void setPolyGT3(POLY_GT3* p) { setlen(p, 9), p->code = 0x34; }
inline void setPolyF4(POLY_F4* p) { setlen(p, 5), p->code = 0x28; }
inline void setPolyFT4(POLY_FT4* p) { setlen(p, 9), p->code = 0x2C; }
inline void setPolyG4(POLY_G4* p) { setlen(p, 8), p->code = 0x38; }
inline void setPolyGT4(POLY_GT4* p) { setlen(p, 12), p->code = 0x3C; }
inline void setSprt8(SPRT_8* p) { setlen(p, 3), p->code = 0x74; }
inline void setSprt16(SPRT_16* p) { setlen(p, 3), p->code = 0x7C; }
inline void setSprt(SPRT* p) { setlen(p, 4), p->code = 0x64; }
inline void setTile1(TILE_1* p) { setlen(p, 2), p->code = 0x68; }
inline void setTile8(TILE_8* p) { setlen(p, 2), p->code = 0x70; }
inline void setTile16(TILE_16* p) { setlen(p, 2), p->code = 0x78; }
inline void setTile(TILE* p) { setlen(p, 3), p->code = 0x60; }
inline void setLineF2(LINE_F2* p) { setlen(p, 3), p->code = 0x40; }
inline void setLineG2(LINE_G2* p) { setlen(p, 4), p->code = 0x50; }
inline void setLineF3(LINE_F3* p) { setlen(p, 5), p->code = 0x48, p->pad = 0x55555555; }
inline void setLineG3(LINE_G3* p) { setlen(p, 7), p->code = 0x58, p->pad = 0x55555555, p->p2 = 0; }
inline void setLineF4(LINE_F4* p) { setlen(p, 6), p->code = 0x4C, p->pad = 0x55555555; }
inline void setLineG4(LINE_G4* p) { setlen(p, 9), p->code = 0x5C, p->pad = 0x55555555, p->p2 = p->p3 = 0; }
inline void setSemiTrans(void* p, bool on) {
  u8& code = reinterpret_cast<u8*>(prim_words(static_cast<P_TAG*>(p)))[3];
  code = on ? (code | 2) : (code & ~2);
}
inline void setShadeTex(void* p, bool on) {
  u8& code = reinterpret_cast<u8*>(prim_words(static_cast<P_TAG*>(p)))[3];
  code = on ? (code | 1) : (code & ~1);
}

void SetDrawTPage(DR_TPAGE* p, int dfe, int dtd, int tpage);
void SetDrawOffset(DR_OFFSET* p, const u16* ofs);
void SetTexWindow(DR_TWIN* p, const RECT* tw);
void SetDrawMove(DR_MOVE* p, const RECT* r, int x, int y);

u16 GetTPage(int tp, int abr, int x, int y);
u16 GetClut(int x, int y);

int ResetGraph(int mode);
DRAWENV* SetDefDrawEnv(DRAWENV* env, int x, int y, int w, int h);
DISPENV* SetDefDispEnv(DISPENV* env, int x, int y, int w, int h);
DRAWENV* PutDrawEnv(DRAWENV* env);
DISPENV* PutDispEnv(DISPENV* env);
void SetDispMask(int mask);
void SetDrawMode(DR_MODE* p, int dfe, int dtd, int tpage, const RECT* tw);
void SetDrawArea(DR_AREA* p, const RECT* r);

P_TAG* ClearOTag(P_TAG* ot, int n);   // entries linked forward, last one terminates
P_TAG* ClearOTagR(P_TAG* ot, int n);  // entries linked backward; draw from ot[n - 1]
void DrawOTag(P_TAG* ot);
int DrawSync(int mode);

int LoadImage(const RECT* r, const void* pixels);
// The address LoadImage's DMA reads from: the GPU DMA reads words from a word address
// (D2_MADR bits 0-1 are ignored), so a source that is not word-aligned starts at the
// halfword below it and the image lands one pixel right. Host buffers keep the console's
// alignment (allocations start word-aligned). The end screen's logo (#3105, an image 10
// bytes into its buffer) is uploaded so on the console and in PCSX-Redux.
const void* dma_source(const void* pixels);
int StoreImage(const RECT* r, void* pixels);
int MoveImage(const RECT* r, int x, int y);
int ClearImage(const RECT* r, u8 red, u8 green, u8 blue);

// libetc
int VSync(int mode);
int VSyncCallback(void (*f)());

// libpress: the movie decoder. DecDCTvlc is the CPU-side bitstream decode Sony's
// library performs; DecDCTin / DecDCTout stand for the MDEC chip and its two DMA
// channels (psx::mdec does the arithmetic). The host MDEC decodes everything
// DecDCTin hands it at once; DecDCTout copies the next 'size' words of its output
// and then runs the DecDCToutCallback directly, where the console raises the DMA
// interrupt when the transfer ends.
void DecDCTReset(int mode);                  // 0 = reset; 1 = abort the current decode
int DecDCTBufSize(const u32* bs);            // MDEC code words the frame needs (header's first halfword)
int DecDCTvlcSize(int size);                 // halfwords per DecDCTvlc call, 0 = whole frames; returns the old limit
// Bitstream -> MDEC run-length codes: buf[0] is the frame's MDEC command word, the
// codes follow and the rest of the frame's code words is padded with 0xFE00.
// Returns 0 when the frame is complete. Host additions to the arguments: how many
// bytes of 'bs' may be read, how many words 'buf' holds, and the frame size
// (psx::mdec decodes a given number of macroblocks where libpress stops at the
// bitstream's end-of-frame code).
int DecDCTvlc(const u32* bs, u32* buf, size_t bs_bytes, size_t buf_words, int width, int height);
void DecDCTin(u32* buf, int mode);           // mode bit 0: 24-bit output (else 15-bit); bit 1: set the mask bit
void DecDCTout(u32* buf, int size);          // size in words
int DecDCToutCallback(void (*func)());

}  // namespace ss::psyq
