#include "game/battle/bobj.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "game/battle/bsfx.h"
#include "game/battle/render.h"
#include "game/battle/stage.h"
#include "psx/gte.h"
#include "runtime/libgpu.h"
#include "runtime/runtime.h"

namespace ss::game::battle {
namespace {

std::array<AnimSet, 40> g_animset_pool;      // 0x8006fa24 g_animset_pool (0x68 each)
std::array<u8, 40> g_animset_used{};         // 0x8006fa28
std::array<TexCache, 16> g_texcache_pool;    // 0x8006d53c (0xe0 each)
int g_texcache_count = 0;                    // 0x8006fa50 g_texcache_count
s32 g_zero_box[3] = {};                      // 0x8006d4ec: what the base BObj's box writes land in
FrameUv g_frame_uv{};                        // scratchpad 0x1f800000 / 0x1f8001ec
s16 g_sprite_ofs_x = 0, g_sprite_ofs_y = 0;  // 0x8006e65c / 0x8006e65e

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
s16 rds16(const u8* p) { return static_cast<s16>(rd16(p)); }

s32 iabs(s32 v) { return v < 0 ? -v : v; }

// A POLY_FT4 (0x28 PS1 bytes) from a sprite's primitive pool.
psyq::POLY_FT4* alloc_ft4(RenderContext* pool) {
  if (pool == bg().prim_pool_a) pool_a_bytes_used() += 0x28;
  return screen_alloc_prims<psyq::POLY_FT4>(pool->arena, 1);
}

// The "texture width does not match the screen width" UV pull-in shared by both quads.
// Returns true when u1 (or v1) must come in by one texel.
bool span_mismatch(s32 d, s32 e, s32 t0, s32 t1) {
  const s32 ad = iabs(d);
  const s32 tw = t1 - t0;
  if (tw < 0) {
    if (ad != t0 - t1) return true;
  } else if (ad != tw) {
    return true;
  }
  const s32 ae = iabs(e);
  return tw < 0 ? ae != t0 - t1 : ae != tw;
}

// 0x80044c5c texcache_alloc: the VRAM rectangle of a streamed cel, uploading it when it
// is not cached. 'cel' is the 12-byte sheet entry, 'image' the sheet's image block.
bool texcache_alloc(BObj* o, s32* u0, s32* v0, s32* u1, s32* v1, int depth, const u8* cel, const u8* image) {
  if ((rd32(cel) & 0xFFFF00) == 0) {
    *u0 = *v0 = *u1 = *v1 = 0;
    return true;
  }
  TexCache* tc = o->texcache;
  if (tc == nullptr) return false;
  const u32 key = static_cast<u32>(reinterpret_cast<uintptr_t>(image + rd32(cel + 8)));
  for (int i = 0; i < 16; ++i) {
    if (tc->key[i] == key) {
      *u0 = tc->rect[i][0], *v0 = tc->rect[i][1], *u1 = tc->rect[i][2], *v1 = tc->rect[i][3];
      tc->used[i] = 1;
      return true;
    }
  }
  int slot = 0;
  while (slot < 16 && tc->key[slot] != 0xFFFFFFFFu) ++slot;
  if (slot == 16) {
    slot = tc->victim;
    int tries = 0;
    while (tc->used[slot] != 0 && tries < 16) {
      slot = (slot + 1) & 15;
      tc->victim = static_cast<u8>(slot);
      ++tries;
    }
    if (tries == 16) {
      runtime::Runtime::get().log_once("battle-texcache-full", "battle: sprite texture cache full (CASH FULL)");
      return false;
    }
    slot = tc->victim;
  }
  tc->victim = static_cast<u8>((tc->victim + 1) & 15);
  const s32 w = cel[1];
  const s32 h = cel[2];
  const s32 vram_w = w >> (2 - depth);
  u32 saved[16];
  std::memcpy(saved, tc->key, sizeof saved);
  if (tc->area[3] - tc->area[1] < h) return false;
  bool collided = false;
  for (int attempt = 0; attempt < 0x3C; ++attempt) {
    if (tc->area[2] <= vram_w + tc->cursor[0]) {
      tc->cursor[0] = tc->area[0];
      tc->cursor[1] = static_cast<s16>(tc->cursor[1] + (collided ? tc->row_height >> 1 : tc->row_height));
      tc->row_height = 0;
    }
    if (tc->area[3] <= h + tc->cursor[1]) {
      tc->row_height = 0;
      tc->cursor[0] = tc->area[0];
      tc->cursor[1] = tc->area[1];
    }
    *u0 = (tc->cursor[0] - (tc->area[0] & 0xFFC0)) << (2 - depth);
    *v0 = tc->cursor[1] - (tc->area[1] & 0xFF00);
    *u1 = *u0 + w;
    *v1 = *v0 + h;
    tc->key[slot] = key;
    tc->rect[slot][0] = static_cast<s16>(*u0);
    tc->rect[slot][1] = static_cast<s16>(*v0);
    tc->rect[slot][2] = static_cast<s16>(*u1);
    tc->rect[slot][3] = static_cast<s16>(*v1);
    collided = false;
    const s16* b = tc->rect[slot];
    for (int k = 0; k < 16; ++k) {
      if (k == slot || tc->key[k] == 0xFFFFFFFFu) continue;
      const s16* a = tc->rect[k];
      const bool ox = (b[0] <= a[0] && a[0] < b[2]) || (b[0] < a[2] && a[2] < b[2]) || (a[0] <= b[0] && b[2] <= a[2]);
      const bool oy = (b[1] <= a[1] && a[1] < b[3]) || (b[1] < a[3] && a[3] < b[3]) || (a[1] <= b[1] && b[3] <= a[3]);
      if (!(ox && oy)) continue;
      if (tc->used[k] != 0) {
        std::memcpy(tc->key, saved, sizeof saved);
        tc->key[slot] = 0xFFFFFFFFu;
        collided = true;
        break;
      }
      tc->key[k] = 0xFFFFFFFFu;
    }
    if (!collided) {
      tc->used[slot] = 1;
      const psyq::RECT r{tc->cursor[0], tc->cursor[1], static_cast<s16>(vram_w), static_cast<s16>(h)};
      psyq::LoadImage(&r, image + rd32(cel + 8));
      tc->cursor[0] = static_cast<s16>(tc->cursor[0] + vram_w);
      if (tc->row_height < h) tc->row_height = static_cast<u8>(h);
      return true;
    }
    s16 step = static_cast<s16>(vram_w / 2);
    if ((vram_w / 2 & 3) != 0) step = static_cast<s16>(step + 4 - (vram_w / 2 & 3));
    tc->cursor[0] = static_cast<s16>(tc->cursor[0] + step);
    if (tc->row_height < h) tc->row_height = static_cast<u8>(h);
  }
  // The console prints "NO CASH" (the last attempt collided) or "NO CASH TAG" through the
  // empty debug_print.
  if (collided) runtime::Runtime::get().log_once("battle-texcache-none", "battle: sprite texture cache: no room (NO CASH)");
  else runtime::Runtime::get().log_once("battle-texcache-tag", "battle: sprite texture cache: no room (NO CASH TAG)");
  return false;
}

// The texture (sheet) entry of cel 'index' of bank 'bank': its VRAM UV box, offsets, tpage
// and CLUT. Returns false when a streamed cel could not be cached.
struct CelTexture {
  s32 u0, v0, u1, v1;
  s32 ox, oy;
  u16 tpage, clut;
};
bool cel_texture(BObj* o, int bank, int index, bool big, CelTexture* t) {
  const AnimSet* set = o->animset;
  const u8* sheet = set->sheets[bank];
  if (sheet == nullptr) return false;
  if ((sheet[8] & 2) == 0) {
    const u8* e = sheet + (big ? 0x10 : 0xC) + index * 8;
    const u16 base = set->tpage[bank];
    u16 tp = static_cast<u16>(base + (e[1] > 1 ? 0xE : 0) + e[1]);
    if (base == 0xE && e[1] != 0) tp = static_cast<u16>(e[1] + 0x1C);
    t->tpage = tp;
    t->u0 = e[2], t->v0 = e[3], t->u1 = e[4], t->v1 = e[5];
    t->clut = static_cast<u16>(set->clut[bank] + e[0]);
    t->ox = static_cast<s8>(e[6]) + g_sprite_ofs_x;
    t->oy = static_cast<s8>(e[7]) + g_sprite_ofs_y;
    return true;
  }
  const u8* e = sheet + (big ? 0x10 : 0xC) + index * 12;
  const int depth = rd16(sheet + rd32(sheet)) >> 15;
  if (!texcache_alloc(o, &t->u0, &t->v0, &t->u1, &t->v1, depth, e, sheet + rd32(sheet + 4))) return false;
  t->ox = rds16(e + 4) + g_sprite_ofs_x;
  t->oy = rds16(e + 6) + g_sprite_ofs_y;
  t->clut = static_cast<u16>(set->clut[bank] + e[0]);
  const TexCache* tc = o->texcache;
  const u16 y0 = static_cast<u16>(tc->area[1]);
  t->tpage = static_cast<u16>(depth << 7 | (y0 >> 4 & 0x10) | ((static_cast<u16>(tc->area[0]) & 0x3FF) >> 6) |
                              (y0 & 0x200) << 2);
  return true;
}

// The colour word of a sprite quad (code 0x2c-0x2f).
u32 sprite_colour(const BObj* o, u8 blend_in, u16* tpage, u16* clut, bool shaded, u32 r, u32 g, u32 b) {
  u32 code;
  if (blend_in == 0) {
    code = shaded ? 0x2C000000u : 0x2D000000u;
  } else {
    code = shaded ? 0x2E000000u : 0x2F000000u;
    const u8 blend = o->blend;
    if (blend != 0) {
      *tpage = static_cast<u16>((*tpage & 0xFF9F) | ((blend & 0x7F) - 1) * 0x20);
      if ((blend & 0x80) == 0) *clut = static_cast<u16>(*clut + 8);
    }
  }
  return shaded ? (code | r | g << 8 | b << 16) : code;
}

s16 corner(s16 base, s32 a, s32 b, s32 scale, s16 shake) {
  return static_cast<s16>(base + static_cast<s16>(static_cast<u32>(((a + b) >> 8) * scale) >> 16) + shake);
}

// 0x80043554: one 0x1c-byte attached part of the current cel.
bool bobj_draw_part(BObj* o, const u8* cel, const u8* part, s32 c, s32 s, bool flip_x, bool flip_y, int ot) {
  const int bank = part[0x14];
  auto* p = alloc_ft4(o->pool);
  if (p == nullptr) return o->frame != 0;
  CelTexture t{};
  if (!cel_texture(o, bank, part[0x15], true, &t)) return false;
  const u8 pflags = part[0x16];
  u8 semi = o->blend;
  if (pflags & 0x80) {
    t.tpage = static_cast<u16>(t.tpage | (pflags & 0x60));
    semi |= 1;
  }
  if (t.u0 == t.u1 && t.v0 == t.v1) return true;
  bool shaded = false;
  u32 g = 0x80, r = 0x80, b = 0x80;
  if (o->colour_mode == 1) {
    g = o->rgb[1], r = o->rgb[0], b = o->rgb[2];
    shaded = true;
  } else if (o->colour_mode == 2) {
    g = r = b = o->rgb[0];
    shaded = true;
  }
  if (pflags & 4) {
    shaded = true;
    const s8 speed = static_cast<s8>(part[0x1a]);
    auto tint = [&](u32 v, u8 k) {
      s32 x = static_cast<s32>(v * k) >> 7;
      if (speed != 0) {
        x += o->ticks_elapsed * speed;
        if (x > 0xFF) x = 0xFF;
        if (x < 0) x = 0;
      }
      return static_cast<u32>(x);
    };
    r = tint(r, part[0x17]);
    g = tint(g, part[0x18]);
    b = tint(b, part[0x19]);
  }
  u16 clut = t.clut;
  const u32 colour = sprite_colour(o, semi, &t.tpage, &clut, shaded, r, g, b);
  psyq::setPolyFT4(p);
  std::memcpy(&p->r0, &colour, 4);
  const s32 dx = rds16(part + 16) - g_sprite_ofs_x;
  const s32 dy = rds16(part + 18) - g_sprite_ofs_y;
  s32 x[4], y[4];
  for (int k = 0; k < 4; ++k) {
    x[k] = rds16(part + 4 * k) + dx;
    y[k] = rds16(part + 4 * k + 2) + dy;
    if (flip_x) x[k] = static_cast<s8>(cel[1]) - x[k];
    if (flip_y) y[k] = static_cast<s8>(cel[2]) - y[k];
  }
  s16 shx = 0, shy = 0;
  if ((o->flags & 3) == 1) {
    shx = static_cast<s16>(view().shake_x);
    shy = static_cast<s16>(view().shake_y);
  }
  s16* xy[4] = {&p->x0, &p->x1, &p->x2, &p->x3};
  for (int k = 0; k < 4; ++k) {
    xy[k][0] = corner(o->screen_x, x[k] * c, y[k] * s, o->final_scale_x, shx);
    xy[k][1] = corner(o->screen_y, x[k] * -s, y[k] * c, o->final_scale_y, shy);
  }
  s32 u0 = t.u0, v0 = t.v0, u1 = t.u1, v1 = t.v1;
  const s32 dxs = p->x3 - p->x0;
  if (dxs < 0) {
    if (iabs(dxs) == iabs(u1 - u0)) {
      if (u0 > 0) --u0;
    } else if (u0 > 0 && (pflags & 8) != 0) {
      --u0;
    }
    --u1;
  } else if (span_mismatch(dxs, p->x2 - p->x1, u0, u1)) {
    --u1;
  }
  const s32 dys = p->y3 - p->y0;
  if (dys < 0 && p->y0 <= p->y1) {
    if (iabs(dys) == iabs(v1 - v0) && v0 > 0) --v0;
    --v1;
  } else {
    if (span_mismatch(dys, p->y2 - p->y1, v0, v1)) --v1;
    if (static_cast<u16>(o->roll + 0x400) <= 0x3FF) --v1;
  }
  p->u0 = p->u2 = static_cast<u8>(u0);
  p->v0 = p->v1 = static_cast<u8>(v0);
  p->u1 = p->u3 = static_cast<u8>(u1);
  p->v2 = p->v3 = static_cast<u8>(v1);
  if (o->clut_offset == 0 || (clut & 0xF) != ((o->clut_row_check - 1) & 0xF) ||
      (o->animset->clut[0] != o->animset->clut[bank] && (o->clut_row_check & 0x10) == 0)) {
    p->clut = clut;
  } else {
    p->clut = static_cast<u16>(o->clut_offset + ((clut & 0xFFF0) - 1));
  }
  p->tpage = t.tpage;
  psyq::addPrim(&o->pool->ot_head[ot], p);
  return true;
}

}  // namespace

const FrameUv& frame_uv() { return g_frame_uv; }
s16& sprite_offset_x() { return g_sprite_ofs_x; }
s16& sprite_offset_y() { return g_sprite_ofs_y; }

AnimSet* animset_alloc() {
  for (size_t i = 0; i < g_animset_pool.size(); ++i) {
    if (g_animset_used[i] == 0) {
      g_animset_used[i] = 1;
      g_animset_pool[i] = AnimSet{};
      return &g_animset_pool[i];
    }
  }
  return nullptr;
}

void animset_free(AnimSet* set) {
  for (size_t i = 0; i < g_animset_pool.size(); ++i)
    if (&g_animset_pool[i] == set) g_animset_used[i] = 0;
}

void animset_pool_reset() {
  g_animset_used.fill(0);
  for (auto& a : g_animset_pool) a = AnimSet{};
}

TexCache* texcache_create(const s16 area[4]) {
  if (g_texcache_count >= static_cast<int>(g_texcache_pool.size())) return nullptr;
  TexCache* tc = &g_texcache_pool[g_texcache_count++];
  *tc = TexCache{};
  std::memcpy(tc->area, area, 8);
  tc->cursor[0] = area[0];
  tc->cursor[1] = area[1];
  for (auto& k : tc->key) k = 0xFFFFFFFFu;
  return tc;
}

void texcache_pool_reset() { g_texcache_count = 0; }

BObj::BObj() { bobj_anim_reset(this); }

BObj::~BObj() { bsfx_forget_emitter(this); }

s32* BObj::get_box(int) { return g_zero_box; }

void bobj_anim_reset(BObj* o) {
  o->pingpong_back = 0;
  o->unk4a = 0;
  o->ticks_left = 0;
  o->ticks_elapsed = 0;
  o->frame = -1;
  o->loops = 0;
  o->loop_limit = 0;
  o->layer = 0;
  o->hit_landed = 0;
  o->box_mask = 0;
}

void bobj_project(BObj* o) {
  ViewState& v = view();
  psx::Gte& g = psyq::gte();
  s32 lift = bsin(static_cast<u32>(static_cast<s32>(v.pitch))) << 4;
  if (lift < 0) lift = 0;
  o->lift = lift + 0x10000;
  const s16 dx = static_cast<s16>((o->pos[0] - v.view_eye[0]) >> 12);
  const s16 dy = static_cast<s16>(((o->pos[1] - o->lift) - v.view_eye[1]) >> 12);
  const s16 dz = static_cast<s16>((o->pos[2] - v.view_eye[2]) >> 12);
  psyq::SetRotMatrix(&v.render_view);
  g.write_data(psx::Gte::VXY0, static_cast<u32>(static_cast<u16>(dx)) | static_cast<u32>(static_cast<u16>(dy)) << 16);
  g.write_data(psx::Gte::VZ0, static_cast<u32>(static_cast<s32>(dz)));
  g.execute(psx::Gte::kRtv0);
  psyq::MATRIX m{};
  m.t[0] = g.mac(1), m.t[1] = g.mac(2), m.t[2] = g.mac(3);
  psyq::SetTransMatrix(&m);
  psyq::SetRotMatrix(&v.render_view);
  g.write_data(psx::Gte::VXY0, 0);
  g.write_data(psx::Gte::VZ0, 0);
  g.execute(psx::Gte::kRtps);
  const u32 sxy = g.read_data(psx::Gte::SXY2);
  o->screen_x = static_cast<s16>(sxy & 0xFFFF);
  o->screen_y = static_cast<s16>(sxy >> 16);
  const s32 sz = static_cast<s32>(g.read_data(psx::Gte::SZ3));
  m.t[0] = m.t[1] = m.t[2] = 0;
  psyq::SetTransMatrix(&m);
  const s16 fx = static_cast<s16>(o->facing[0] >> 12);
  const s16 fy = static_cast<s16>(o->facing[1] >> 12);
  const s16 fz = static_cast<s16>(o->facing[2] >> 12);
  g.write_data(psx::Gte::VXY0, static_cast<u32>(static_cast<u16>(fx)) | static_cast<u32>(static_cast<u16>(fy)) << 16);
  g.write_data(psx::Gte::VZ0, static_cast<u32>(static_cast<s32>(fz)));
  g.execute(psx::Gte::kRtv0tr);
  o->depth = static_cast<u16>(sz >> 5);
  o->view_fx = static_cast<s16>(g.mac(1));
  o->view_fy = static_cast<s16>(-g.mac(3));
}

// 0x800455d8 bobj_anim_for_view: the animation for the view direction; 'flip' is set
// when the sprite must be mirrored.
static const u8* bobj_anim_for_view(const BObj* o, const u8* dirs, bool* flip) {
  u32 idx;
  if (dirs[0] == 2 || dirs[0] != 4) {
    idx = dirs[1];
    if (o->view_fx > 0) *flip = true;
  } else {
    const s32 ay = iabs(o->view_fy), ax = iabs(o->view_fx);
    if (ax <= ay) {
      idx = o->view_fy < 0 ? dirs[2] : dirs[3];
    } else {
      idx = dirs[1];
      if (o->view_fx > 0) *flip = true;
    }
  }
  if (o->flags & 0x20) *flip = true;
  const u8* anims = o->animset->anims;
  return anims + rd32(anims + (idx + 1) * 4);
}

int bobj_anim_draw(BObj* o, u32 flags) {
  BattleGlobals& gl = bg();
  bool flip_x = false;
  if (o->animset == nullptr || o->animset->layout == nullptr || o->animset->anims == nullptr) return o->frame;
  if (o->texcache != nullptr && o->texcache_shared == 0 && o->texcache->shared == 0) {
    std::memset(o->texcache->used, 0, sizeof o->texcache->used);
  }
  const u8* anim = bobj_anim_for_view(o, o->animset->layout + o->anim * 4u, &flip_x);
  const s32 count = rd16(anim);
  const s8 last = static_cast<s8>(static_cast<s8>(anim[0]) - 1);
  const u8* fr = nullptr;
  do {
    if (anim[2] == 0) o->pingpong_back = 0;
    if (o->ticks_left < 1) o->frame = static_cast<s8>(o->pingpong_back == 0 ? o->frame + 1 : o->frame - 1);
    if (anim[2] == 0) {
      if (count <= o->frame) {
        o->frame = static_cast<s8>(anim[3]);
        ++o->loops;
      }
    } else if (o->frame < count) {
      if (o->frame < anim[3] && o->pingpong_back != 0) {
        o->frame = count == 1 ? 0 : static_cast<s8>(anim[3] + 1);
        if (count <= o->frame) o->frame = last;
        o->pingpong_back = 0;
      }
    } else {
      o->frame = static_cast<s8>(o->frame - 2);
      o->pingpong_back = 1;
      ++o->loops;
      if (o->frame < anim[3]) o->frame = static_cast<s8>(anim[3]);
      if (count <= o->frame) o->frame = last;
    }
    fr = anim + 4 + o->frame * 4;
    if (o->ticks_left > 0) break;
    o->ticks_left = static_cast<s16>(o->ticks_left + (fr[3] == 0 ? 1 : fr[3]));
    o->ticks_elapsed = 0;
  } while (o->ticks_left < 1);
  if (o->loop_limit != 0 && o->loop_limit <= o->loops) return 0xFFFF;
  if ((flags & 0xFF) == 0) {
    o->ticks_left = static_cast<s16>(o->ticks_left - gl.frame_ticks);
    o->ticks_elapsed = static_cast<s16>(o->ticks_elapsed + gl.frame_ticks);
  }
  const int bank = fr[2];
  const u8* frames = o->animset->frames[bank];
  if (frames == nullptr) return o->frame;
  const bool big = frames[2] == 0;
  const u8* cel = frames + (static_cast<u32>(rd16(fr)) << (big ? 3 : 2)) + 0x14;
  const u8 cel_flags = cel[3];
  o->box_mask = 0;
  const u8* boxes = nullptr;
  if (cel[7] != 0 && big) boxes = frames + rd32(frames + 0x10) + (cel[7] - 1) * 0x20;
  if (cel_flags & 1) flip_x = !flip_x;
  bool flip_y = (cel_flags >> 1 & 1) != 0;
  if (o->flags & 0x20) flip_y = !flip_y;
  o->ext_x0 = 0;
  o->ext_x1 = 0;
  if (boxes != nullptr) {
    static const int kLo[4] = {0, 1, 0x10, 0x11};
    for (int i = 0; i < 4; ++i) {
      s32* mn = o->get_box(kLo[i]);
      s32* mx = o->get_box(kLo[i] | 0x100);
      const u8* bx = boxes + 8 * i;
      mn[0] = rds16(bx) << 14;
      mn[1] = rds16(bx + 2) << 14;
      mn[2] = o->size[2] * -0x2000;
      mx[0] = rds16(bx + 4) << 14;
      mx[1] = rds16(bx + 6) << 14;
      mx[2] = o->size[2] << 13;
      if (flip_x) mn[0] = -mn[0], mx[0] = -mx[0];
      if (flip_y) mn[1] = -mn[1], mx[1] = -mx[1];
      if (mn[0] == 0 && mx[0] == 0) {
        o->box_mask = static_cast<u8>(o->box_mask >> 1);
      } else {
        if (mx[0] < mn[0]) std::swap(mn[0], mx[0]);
        if (mx[1] < mn[1]) std::swap(mn[1], mx[1]);
        for (int k = 0; k < 3; ++k) {
          mn[k] += o->pos[k];
          mx[k] += o->pos[k];
        }
        o->box_mask = static_cast<u8>(o->box_mask >> 1 | 8);
      }
    }
  }
  const bool uv_only = (flags & 0xFF00) == 0x9800;
  if (!uv_only && ((o->depth < 9 && o->pool == gl.prim_pool_a) || static_cast<u16>(o->screen_x + 0x140) > 0x3C0 ||
                   static_cast<u16>(o->screen_y + 0xF0) > 0x2D0)) {
    return o->frame;
  }
  CelTexture t{};
  if (!cel_texture(o, bank, cel[0], big, &t)) return o->frame;
  u8 semi = o->blend;
  if (cel_flags & 0x80) {
    t.tpage = static_cast<u16>(t.tpage | (cel_flags & 0x60));
    semi |= 1;
  }
  if (uv_only) {
    g_frame_uv = FrameUv{t.u0, t.v0, t.u1 - 1, t.v1 - 1, t.tpage, t.clut};
    return o->frame;
  }
  s32 x0 = -t.ox, x1 = -t.ox + (t.u1 - t.u0);
  s32 y0 = -t.oy, y1 = -t.oy + (t.v1 - t.v0);
  if (flip_x) {
    x0 = t.ox + static_cast<s8>(cel[1]);
    x1 = static_cast<s8>(cel[1]) - x1;
  }
  if (flip_y) {
    y0 = t.oy + static_cast<s8>(cel[2]);
    y1 = static_cast<s8>(cel[2]) - y1;
  }
  o->ext_x0 = static_cast<s16>(x0);
  o->ext_x1 = static_cast<s16>(x1);
  u16 clut = t.clut;
  u32 colour;
  if (o->colour_mode == 0) {
    colour = sprite_colour(o, semi, &t.tpage, &clut, false, 0, 0, 0);
  } else {
    const u32 r = o->colour_mode == 1 ? o->rgb[0] : o->rgb[0];
    const u32 g = o->colour_mode == 1 ? o->rgb[1] : o->rgb[0];
    const u32 b = o->colour_mode == 1 ? o->rgb[2] : o->rgb[0];
    colour = sprite_colour(o, semi, &t.tpage, &clut, true, r, g, b);
  }
  ViewState& v = view();
  s16 offx = 0, offy = 0;
  s32 sx, sy;
  if ((o->flags & 2) == 0) {
    if ((o->flags & 1) == 0) {
      const u32 k = o->depth != 0 ? 0x10000u / o->depth : 0x10000u;
      sx = (static_cast<s32>(k * static_cast<u32>(o->scale_x)) >> 12) * v.zoom >> 10;
      sy = (static_cast<s32>(k * static_cast<u32>(o->scale_y)) >> 12) * v.zoom >> 10;
    } else {
      offx = static_cast<s16>(v.shake_x);
      offy = static_cast<s16>(v.shake_y);
      sx = o->scale_x * v.zoom >> 12;
      sy = o->scale_y * v.zoom >> 12;
    }
  } else {
    sx = o->scale_x;
    sy = o->scale_y;
  }
  const s32 a = o->roll - v.camera_roll;
  const s32 c = bcos(static_cast<u32>(a));
  const s32 s = bsin(static_cast<u32>(a));
  o->final_scale_x = sx;
  o->final_scale_y = sy;
  const int ot = o->pool == gl.prim_pool_a ? (o->layer + 0x617) - o->depth : o->depth + o->layer;
  if (big && cel[4] != 0) {
    if (!bobj_draw_part(o, cel, frames + rd32(frames + 4) + (cel[4] - 1) * 0x1C, c, s, flip_x, flip_y, ot))
      return o->frame;
  }
  const u8* front = (big && cel[6] != 0) ? frames + rd32(frames + 0xC) + (cel[6] - 1) * 0x1C : nullptr;
  if (front != nullptr && front[0x1B] == 0) {
    if (!bobj_draw_part(o, cel, front, c, s, flip_x, flip_y, ot)) return o->frame;
  }
  if (t.u0 != t.u1 || t.v0 != t.v1) {
    auto* p = alloc_ft4(o->pool);
    if (p == nullptr) return o->frame;
    psyq::setPolyFT4(p);
    std::memcpy(&p->r0, &colour, 4);
    p->x0 = corner(o->screen_x, x0 * c, y0 * s, sx, offx);
    p->y0 = corner(o->screen_y, x0 * -s, y0 * c, sy, offy);
    p->x1 = corner(o->screen_x, x1 * c, y0 * s, sx, offx);
    p->y1 = corner(o->screen_y, x1 * -s, y0 * c, sy, offy);
    p->x2 = corner(o->screen_x, x0 * c, y1 * s, sx, offx);
    p->y2 = corner(o->screen_y, x0 * -s, y1 * c, sy, offy);
    p->x3 = corner(o->screen_x, x1 * c, y1 * s, sx, offx);
    p->y3 = corner(o->screen_y, x1 * -s, y1 * c, sy, offy);
    s32 u0 = t.u0, v0 = t.v0, u1 = t.u1, v1 = t.v1;
    const s32 dx = p->x3 - p->x0;
    if (dx < 0) {
      if (iabs(dx) == iabs(u1 - u0) && u0 > 0) --u0;
      --u1;
    } else if (span_mismatch(dx, p->x2 - p->x1, u0, u1)) {
      --u1;
    }
    const s32 dy = p->y3 - p->y0;
    if (dy < 0 && p->y0 <= p->y1) {
      if (iabs(dy) == iabs(v1 - v0) && v0 > 0) --v0;
      --v1;
    } else if (span_mismatch(dy, p->y2 - p->y1, v0, v1)) {
      --v1;
    }
    p->u0 = p->u2 = static_cast<u8>(u0);
    p->v0 = p->v1 = static_cast<u8>(v0);
    p->u1 = p->u3 = static_cast<u8>(u1);
    p->v2 = p->v3 = static_cast<u8>(v1);
    p->clut = o->clut_offset == 0 ? clut : static_cast<u16>(o->clut_offset + ((clut & 0xFFF0) - 1));
    p->tpage = t.tpage;
    psyq::addPrim(&o->pool->ot_head[ot], p);
  }
  if (front != nullptr && front[0x1B] != 0) {
    if (!bobj_draw_part(o, cel, front, c, s, flip_x, flip_y, ot)) return o->frame;
  }
  if (big && cel[5] != 0) bobj_draw_part(o, cel, frames + rd32(frames + 8) + (cel[5] - 1) * 0x1C, c, s, flip_x, flip_y, ot);
  return o->frame;
}

void bobj_draw_shadow(BObj* o, s32 x0, s32 x1, s32 z0, s32 z1, u32 colour) {
  ViewState& v = view();
  psx::Gte& g = psyq::gte();
  const u32 alpha = colour >> 24;
  if (stage_ground_height(o->pos[0] >> 12, o->pos[2] >> 12) < o->pos[1]) return;
  const s16 dx = static_cast<s16>((o->pos[0] - v.view_eye[0]) >> 12);
  const s16 dy = static_cast<s16>((-v.view_eye[1] - o->lift) >> 12);
  const s16 dz = static_cast<s16>((o->pos[2] - v.view_eye[2]) >> 12);
  psyq::SetRotMatrix(&v.render_view);
  g.write_data(psx::Gte::VXY0, static_cast<u32>(static_cast<u16>(dx)) | static_cast<u32>(static_cast<u16>(dy)) << 16);
  g.write_data(psx::Gte::VZ0, static_cast<u32>(static_cast<s32>(dz)));
  g.execute(psx::Gte::kRtv0);
  psyq::MATRIX m{};
  m.t[0] = g.mac(1), m.t[1] = g.mac(2), m.t[2] = g.mac(3);
  psyq::SetRotMatrix(&v.render_view);
  psyq::SetTransMatrix(&m);
  s32 extent = iabs(x1 - x0);
  if (extent < iabs(z1 - z0)) extent = iabs(z1 - z0);
  int n = 2;
  if (extent > 0x100) n = 4;
  if (extent > 0x300) n = 6;
  if (extent > 0x400) n = 8;
  // The ground under the shadow: (n + 1)^2 vertices (rows of 9) and their UVs
  // (scratchpad 0x1f800000 / 0x1f800288): a 32x32 blob at (0x30..0x4f) mirrored.
  s16 vert[81][4] = {};
  u16 uv[81] = {};
  const int half = n >> 1;
  s32 zacc = 0;
  for (int i = 0; i <= n; ++i) {
    for (int j = 0; j <= n; ++j) {
      const s32 x = (x1 - x0) * j / n + x0;
      const s32 z = zacc / n + z0;
      const s32 h = stage_ground_height((o->pos[0] >> 12) + x, (o->pos[2] >> 12) + z);
      const int idx = i * 9 + j;
      vert[idx][0] = static_cast<s16>(x);
      vert[idx][1] = static_cast<s16>(h >> 12);
      vert[idx][2] = static_cast<s16>(z);
      const u16 u = j < half ? static_cast<u16>(j * 0x1F / half + 0x30) : static_cast<u16>(0x4F - (j - half) * 0x1F / half);
      const s32 vv = i < half ? i * 0x1F / half + 0x30 : 0x4F - (i - half) * 0x1F / half;
      uv[idx] = static_cast<u16>(vv << 8 | u);
    }
    zacc += z1 - z0;
  }
  if (!arena_has_room(o->pool, n * n * 0x28)) return;
  u32 code;
  if (alpha == 0xFF) {
    if (o->colour_mode == 1) {
      code = o->rgb[0] | o->rgb[1] << 8 | 0x2E000000u | static_cast<u32>(o->rgb[2]) << 16;
    } else if (o->colour_mode == 2) {
      const u32 k = o->rgb[0];
      code = k | k << 8 | 0x2E000000u | k << 16;
    } else {
      code = 0x2F000000u;
    }
  } else if (o->colour_mode == 1 || o->colour_mode == 2) {
    // A tinted shadow follows the object's colour: each channel * rgb / 128, at most 0xff.
    const u32 kr = o->rgb[0];
    const u32 kg = o->colour_mode == 1 ? o->rgb[1] : o->rgb[0];
    const u32 kb = o->colour_mode == 1 ? o->rgb[2] : o->rgb[0];
    u32 r = (colour & 0xFF) * kr >> 7;
    if (0xFF < r) r = 0xFF;
    u32 gch = (colour & 0xFF00) * kg >> 7 & 0xFF00;
    if (0xFF00 < gch) gch = 0xFF00;
    u32 b = (colour & 0xFF0000) * kb >> 7 & 0xFF0000;
    if (0xFF0000 < b) b = 0xFF0000;
    code = r | gch | b | 0x2E000000u;
  } else {
    code = (colour & 0xFFFFFF) | 0x2E000000u;
  }
  const u16 tpage = alpha != 0xFF ? static_cast<u16>(alpha | 0xF) : 0x4F;
  auto ldv = [&](int reg, int idx) {
    g.write_data(reg, static_cast<u32>(static_cast<u16>(vert[idx][0])) | static_cast<u32>(static_cast<u16>(vert[idx][1])) << 16);
    g.write_data(reg + 1, static_cast<u32>(static_cast<s32>(vert[idx][2])));
  };
  for (int i = 0; i < n; ++i) {
    for (int j = 0; j < n; ++j) {
      const int idx = i * 9 + j;
      ldv(psx::Gte::VXY0, idx);
      g.execute(psx::Gte::kRtps);
      const u32 xy0 = g.read_data(psx::Gte::SXY2);
      const s32 f0 = static_cast<s32>(g.read_ctrl(psx::Gte::FLAG));
      ldv(psx::Gte::VXY0, idx + 1);
      ldv(psx::Gte::VXY1, idx + 9);
      ldv(psx::Gte::VXY2, idx + 10);
      g.execute(psx::Gte::kRtpt);
      const s32 f1 = static_cast<s32>(g.read_ctrl(psx::Gte::FLAG));
      if (f1 < 0 || f0 < 0) continue;
      g.execute(psx::Gte::kAvsz4);
      const s32 z = static_cast<s32>(g.read_data(psx::Gte::OTZ)) >> 3;
      if (z >= 0x5FB - o->layer) continue;
      auto* p = alloc_ft4(o->pool);
      if (p == nullptr) return;
      psyq::setPolyFT4(p);
      std::memcpy(&p->r0, &code, 4);
      const u32 xy[4] = {xy0, g.read_data(psx::Gte::SXY0), g.read_data(psx::Gte::SXY1), g.read_data(psx::Gte::SXY2)};
      s16* dst[4] = {&p->x0, &p->x1, &p->x2, &p->x3};
      for (int k = 0; k < 4; ++k) {
        dst[k][0] = static_cast<s16>(xy[k] & 0xFFFF);
        dst[k][1] = static_cast<s16>(xy[k] >> 16);
      }
      std::memcpy(&p->u0, &uv[idx], 2);
      std::memcpy(&p->u1, &uv[idx + 1], 2);
      std::memcpy(&p->u2, &uv[idx + 9], 2);
      std::memcpy(&p->u3, &uv[idx + 10], 2);
      p->clut = 0x7F82;
      p->tpage = tpage;
      psyq::addPrim(&o->pool->ot_head[o->layer + 0x615 - z], p);
    }
  }
}

void bobj_texcache_begin_frame(BObj* o) {
  if (o->texcache != nullptr) std::memset(o->texcache->used, 0, sizeof o->texcache->used);
}

void bobj_texcache_reset(BObj* o) {
  TexCache* tc = o->texcache;
  if (tc == nullptr) return;
  for (int i = 0; i < 16; ++i) {
    tc->used[i] = 0;
    tc->key[i] = 0xFFFFFFFFu;
  }
  tc->cursor[0] = tc->area[0];
  tc->cursor[1] = tc->area[1];
  tc->row_height = 0;
  tc->victim = 0;
}

void bobj_share_sprite(const BObj* src, BObj* dst) {
  if (dst == nullptr) return;
  dst->animset = src->animset;
  dst->texcache = src->texcache;
  dst->texcache_shared = 1;
}

s32 bobj_ground_height(const BObj* o) { return stage_ground_height(o->pos[0] >> 12, o->pos[2] >> 12); }

void bobj_move(BObj* o, const s32 vel[3]) {
  const s32 t = bg().frame_ticks;
  for (int i = 0; i < 3; ++i) o->pos[i] += vel[i] * t;
}

bool bobj_near_point(const BObj* o, const s32 p[3], s32 r) {
  const s32 lim = static_cast<s32>(static_cast<u32>(r) << 12);
  for (int k = 0; k < 3; ++k) {
    const s32 d = o->pos[k] - p[k];
    if (!((d < 0 ? p[k] - o->pos[k] : d) < lim)) return false;
  }
  return true;
}

bool bobj_on_screen(const BObj* o) {
  return static_cast<u16>(o->screen_x + 0x17) < 0x177 && static_cast<u16>(o->screen_y - 0x19) < 0xF7;
}

bool bobj_outside_arena(const BObj* o) {
  const StageData& s = stage();
  return o->pos[0] < s.bounds[0] || o->pos[0] > s.bounds[2] || o->pos[2] < s.bounds[1] || o->pos[2] > s.bounds[3];
}

int bobj_anim_ticks(const BObj* o, int from, int to) {
  bool flip = false;
  const u8* anim = bobj_anim_for_view(o, o->animset->layout + o->anim * 4u, &flip);
  const int count = rd16(anim);
  if (count < to) to = count;
  int sum = 0;
  for (int f = from; f < to; ++f) {
    const u8 t = anim[4 + f * 4 + 3];
    sum += t == 0 ? 1 : t;
  }
  return sum;
}

}  // namespace ss::game::battle
