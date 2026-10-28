#include "game/field/sprite_render.h"

#include <algorithm>
#include <cstring>

#include "game/common/texture.h"
#include "game/field/actor_base.h"
#include "game/field/actors.h"
#include "game/field/field.h"
#include "game/field/map.h"
#include "game/field/screen.h"
#include "runtime/libgpu.h"
#include "runtime/runtime.h"

namespace ss::game::field {
namespace {

// Rectangles of two cache slots overlap (both caches use the same test).
bool rects_overlap(const s16* a, const s16* b) {
  // a = the new slot, b = another one: {x0, y0, x1, y1}.
  bool hit;
  if ((a[0] <= b[0] && b[0] < a[2]) || (a[0] < b[2] && b[2] < a[2])) {
    hit = true;
  } else {
    hit = b[0] <= a[0] && a[2] <= b[2];
  }
  if (!hit) return false;
  if (b[1] < a[1] || a[3] <= b[1]) {
    if (b[3] <= a[1] || a[3] <= b[3]) return b[1] <= a[1] && a[3] <= b[3];
  }
  return true;
}

// Picks a slot for a new entry: the first free one, else the next whose lifetime has
// run out, searching round-robin from 'next'. Returns -1 when every slot is in use.
int pick_slot(const uintptr_t* keys, const u8* life, u8& next) {
  int slot = 0;
  while (slot < 32 && keys[slot] != 0) ++slot;
  if (slot == 32) {
    slot = next;
    int tries = 0;
    while (tries < 32 && life[slot] != 0) {
      slot = (slot + 1) & 0x1F;
      ++tries;
    }
    if (tries == 32) return -1;
  }
  next = static_cast<u8>((next + 1) & 0x1F);
  return slot;
}

// The end of the sprite pack holding 'p' (reads past a frame's last row stay inside it).
const u8* pack_end(const u8* p) {
  const SpritePackSlot* slots = sprite_pack_slots();
  for (int i = 0; i < 16; ++i) {
    const auto& d = slots[i].data;
    if (!d.empty() && p >= d.data() && p < d.data() + d.size()) return d.data() + d.size();
  }
  return nullptr;
}

u32 weight_table(u32 index) { return read_u32(field_data(0x80076384 + index * 4)); }

}  // namespace

u16 upload_clut16(const u16* clut, int x, int y) {
  const psyq::RECT r{static_cast<s16>(x), static_cast<s16>(y), 16, 1};
  psyq::LoadImage(&r, clut);
  return static_cast<u16>(y << 6 | x >> 4);
}

void sprite_resample(u16* out, const u8* pixels, const u8* palette, s32 sw, s32 sh, s32 tw, s32 th, s32 phase_x,
                     s32 phase_y) {
  if (sw <= 0x1000 || sh <= 0x1000) return;
  const u32 step_x = (static_cast<u32>(tw) << 12) / static_cast<u32>(sw);
  const u32 step_y = (static_cast<u32>(th) << 12) / static_cast<u32>(sh);
  // Colours spread so that a 32-bit multiply-add sums each channel apart:
  // r -> bits 10-14, g -> bits 0-4, b -> bits 20-24.
  u32 spread[16];
  for (int k = 0; k < 16; ++k) {
    const u32 c = read_u16(palette + k * 2);
    spread[k] = (c & 0x1F) << 10 | (c & 0x3E0) >> 5 | (c & 0x7C00) << 10;
  }
  const u8* end = pack_end(pixels);
  auto texel_byte = [&](const u8* p) -> u32 { return end != nullptr && p >= end ? 0 : *p; };
  const u32 stride = static_cast<u32>(tw) >> 13;
  const u32 v_end = (static_cast<u32>(th) + 1) - step_y;
  const u32 u_start = (step_x * static_cast<u32>(phase_x)) >> 12;
  const u32 v_start = (step_y * static_cast<u32>(phase_y)) >> 12;
  u32 v = v_start;
  for (; v < v_end; v += step_y) {
    const u32 wy = (v & 0xF80) >> 2;
    u32 u = u_start;
    while (u < static_cast<u32>(tw)) {
      const u8* p = pixels + (u >> 13) + (v >> 12) * stride;
      const u32 b0 = texel_byte(p);
      u32 tl, tr, bl, br;
      if (((u >> 12) & 1) == 0) {
        if ((b0 & 0xF) == 0 || (b0 >> 4) == 0) {
          *out++ = 0;
          u += step_x;
          continue;
        }
        const u32 b1 = texel_byte(p + stride);
        tl = b0 & 0xF;
        tr = b0 >> 4;
        bl = b1 & 0xF;
        br = b1 >> 4;
      } else {
        const u32 b1 = texel_byte(p + 1);
        if ((b0 >> 4) == 0 || (b1 & 0xF) == 0) {
          *out++ = 0;
          u += step_x;
          continue;
        }
        tl = b0 >> 4;
        tr = b1 & 0xF;
        bl = texel_byte(p + stride) >> 4;
        br = texel_byte(p + stride + 1) & 0xF;
      }
      const u32 w = weight_table(wy + ((u & 0xF80) >> 7));
      u += step_x;
      const u32 c = (spread[tr] * ((w >> 8) & 0x3F) + spread[bl] * ((w >> 16) & 0x3F) + spread[br] * (w >> 24) +
                     spread[tl] * (w & 0x3F)) &
                    0x3E0F83E0;
      *out++ = static_cast<u16>(c | (c >> 15) | 0x8000);
    }
    if (u < static_cast<u32>(tw) + u_start) *out++ = 0;
  }
  if (v < v_end + v_start && tw != 0) {
    for (u32 u = 0; u < static_cast<u32>(tw); u += step_x) *out++ = 0;
  }
}

int sprite_cache_a_alloc(ActorSystem* s, s32* u0, s32* v0, s32* u1, s32* v1, u8** work, const u8* pixels,
                         const u8* palette, s32 sw, s32 sh, s32 tw, s32 th, s32 w, s32 h, s32 phase_x, s32 phase_y) {
  const uintptr_t key = reinterpret_cast<uintptr_t>(pixels);
  for (int slot = 0; slot < 32; ++slot) {
    if (s->cache_a_key[slot] == key && s->cache_a_size[slot][0] == sw && s->cache_a_size[slot][1] == sh) {
      const s16* r = s->cache_a_rect[slot];
      *u0 = r[0];
      *v0 = r[1];
      *u1 = r[2];
      *v1 = r[3];
      s->cache_a_life[slot] = 2;
      return slot;
    }
  }
  const int slot = pick_slot(s->cache_a_key, s->cache_a_life, s->cache_a_next);
  if (slot < 0) return -1;
  uintptr_t backup[32];
  std::memcpy(backup, s->cache_a_key, sizeof(backup));
  s->cache_a_size[slot][0] = sw;
  s->cache_a_size[slot][1] = sh;
  for (int attempt = 0;; ++attempt) {
    if (attempt > 15) return -1;
    if (0x1FE < w + s->cache_a_x) {
      const u8 shelf = s->cache_a_shelf;
      s->cache_a_x = 0x141;
      s->cache_a_shelf = 0;
      s->cache_a_y = static_cast<s16>(s->cache_a_y + shelf);
    }
    if (0xFE < h + s->cache_a_y) {
      s->cache_a_x = 0x141;
      s->cache_a_y = 1;
      s->cache_a_shelf = 0;
    }
    if (0xFE < h) return -1;
    *u0 = s->cache_a_x - 0x140;
    *v0 = s->cache_a_y;
    *u1 = *u0 + w;
    *v1 = *v0 + h;
    s->cache_a_key[slot] = key;
    s16* r = s->cache_a_rect[slot];
    r[0] = static_cast<s16>(*u0);
    r[1] = static_cast<s16>(*v0);
    r[2] = static_cast<s16>(*u1);
    r[3] = static_cast<s16>(*v1);
    bool collided = false;
    for (int j = 0; j < 32; ++j) {
      if (j == slot || s->cache_a_key[j] == 0) continue;
      if (!rects_overlap(r, s->cache_a_rect[j])) continue;
      if (s->cache_a_life[j] == 0) {
        s->cache_a_key[j] = 0;
        continue;
      }
      std::memcpy(s->cache_a_key, backup, sizeof(backup));
      s->cache_a_key[slot] = 0;
      collided = true;
      break;
    }
    if (!collided) {
      sprite_resample(reinterpret_cast<u16*>(*work), pixels, palette, sw, sh, tw, th, phase_x, phase_y);
      s->cache_a_life[slot] = 2;
      const psyq::RECT rect{s->cache_a_x, s->cache_a_y, static_cast<s16>(w), static_cast<s16>(h)};
      psyq::LoadImage(&rect, *work);
      *work += ((w * h + 1) & ~1) * 2;
      s->cache_a_x = static_cast<s16>(s->cache_a_x + w);
      if (s->cache_a_shelf < h) s->cache_a_shelf = static_cast<u8>(h);
      return slot;
    }
    s->cache_a_x = static_cast<s16>(s->cache_a_x + w);
    if (s->cache_a_shelf < h) s->cache_a_shelf = static_cast<u8>(h);
  }
}

int sprite_cache_b_alloc(ActorSystem* s, u32* uv, int mode, u16* clut, const u8* pixels, u8* palette, s32 w, s32 h,
                         s32 blend) {
  psx::Gpu& gpu = runtime::Runtime::get().gpu;
  const uintptr_t key = reinterpret_cast<uintptr_t>(pixels) + static_cast<uintptr_t>(blend);
  auto pack_uv = [](const s16* r) {
    return static_cast<u32>(r[0] | r[1] << 8 | r[2] << 16 | r[3] << 24);
  };
  auto clut_id = [&](int slot) {
    return static_cast<u16>(slot < 16 ? slot + 0x7F00 : (slot & 0xF) + 0x7F40);
  };
  for (int slot = 0; slot < 32; ++slot) {
    if (s->cache_b_key[slot] != key) continue;
    *uv = pack_uv(s->cache_b_rect[slot]);
    if (mode == 0) {
      *clut = clut_id(slot);
    } else {
      *clut = static_cast<u16>(s->cache_b_cluts * 0x40 + 32000);
      ++s->cache_b_cluts;
      upload_clut256(gpu, reinterpret_cast<const u16*>(palette), 0, s->cache_b_cluts + 499);
    }
    s->cache_b_life[slot] = 2;
    return slot;
  }
  const int slot = pick_slot(s->cache_b_key, s->cache_b_life, s->cache_b_next);
  if (slot < 0) return -1;
  uintptr_t backup[32];
  std::memcpy(backup, s->cache_b_key, sizeof(backup));
  if (mode == 0) {
    *clut = clut_id(slot);
  } else {
    *clut = static_cast<u16>(s->cache_b_cluts * 0x40 + 32000);
    ++s->cache_b_cluts;
  }
  const s32 vw = w >> (2 - mode);
  for (int attempt = 0;; ++attempt) {
    if (attempt > 15) return -1;
    if (0x17E < vw + s->cache_b_x) {
      const u8 shelf = s->cache_b_shelf;
      s->cache_b_x = 0x141;
      s->cache_b_shelf = 0;
      s->cache_b_y = static_cast<s16>(s->cache_b_y + shelf);
    }
    if (0x1FE < h + s->cache_b_y) {
      s->cache_b_x = 0x141;
      s->cache_b_y = 0x101;
      s->cache_b_shelf = 0;
    }
    if (0xFE < h) return -1;
    s->cache_b_key[slot] = key;
    s16* r = s->cache_b_rect[slot];
    r[0] = static_cast<s16>(s->cache_b_x - 0x140);
    r[1] = static_cast<s16>(s->cache_b_y - 0x100);
    r[2] = static_cast<s16>(r[0] + vw);
    r[3] = static_cast<s16>(r[1] + h);
    bool collided = false;
    for (int j = 0; j < 32; ++j) {
      if (j == slot || s->cache_b_key[j] == 0) continue;
      if (!rects_overlap(r, s->cache_b_rect[j])) continue;
      if (s->cache_b_life[j] == 0) {
        s->cache_b_key[j] = 0;
        continue;
      }
      std::memcpy(s->cache_b_key, backup, sizeof(backup));
      s->cache_b_key[slot] = 0;
      collided = true;
      break;
    }
    if (!collided) {
      *uv = pack_uv(r);
      s->cache_b_life[slot] = 2;
      const psyq::RECT rect{s->cache_b_x, s->cache_b_y, static_cast<s16>(vw), static_cast<s16>(h)};
      psyq::LoadImage(&rect, pixels);
      if (mode != 0) {
        upload_clut256(gpu, reinterpret_cast<const u16*>(palette), 0, s->cache_b_cluts + 499);
      } else {
        // The palette gets the STP bit on every opaque colour (the sprite blends as a
        // whole when semi-transparent) unless a colour already has it; with a blend
        // mode it always does. The pack's palette is changed in place.
        bool set_stp = true;
        if (blend == 0) {
          for (int k = 1; k < 15; ++k) {
            if (read_u16(palette + k * 2) & 0x8000) {
              set_stp = false;
              break;
            }
          }
        }
        if (set_stp) {
          for (int k = 1; k < 16; ++k) {
            const u16 c = read_u16(palette + k * 2);
            if (c != 0) {
              const u16 v = static_cast<u16>(c | 0x8000);
              std::memcpy(palette + k * 2, &v, 2);
            }
          }
        }
        upload_clut16(reinterpret_cast<const u16*>(palette), (slot & 0xF) << 4, slot < 16 ? 0x1FC : 0x1FD);
      }
      s->cache_b_x = static_cast<s16>(s->cache_b_x + vw);
      if (s->cache_b_shelf < h) s->cache_b_shelf = static_cast<u8>(h);
      return slot;
    }
    s->cache_b_x = static_cast<s16>(s->cache_b_x + vw);
    if (s->cache_b_shelf < h) s->cache_b_shelf = static_cast<u8>(h);
  }
}

void sprite_draw_part(Actor* a, const u8* cel, const u8* part, s32 cos_r, s32 sin_r, int flip_x, int flip_y,
                      int scaled) {
  Screen* screen = field_screen();
  ActorSystem* sys = actor_system();
  const MapData& map = map_data();
  const u8* frame = a->frames + part[0x15] * 0xC;
  const u32 w = frame[1], h = frame[2];
  const s32 tw = static_cast<s32>(w << 12), th = static_cast<s32>(h << 12);
  s32 sw, sh;
  const s32 scale = map.actor_scale;
  if ((a->flags & kActorScreenSpace) == 0) {
    const s32 d = a->depth << 2;
    const s32 H = read_u16(map.scene.data() + 0x6A);
    const s32 kx = (((H * a->scale_x) >> 12) << 12) / d;
    const s32 ky = (((H * a->scale_y) >> 12) << 12) / d;
    sw = (static_cast<s32>(w) * scale * 0x5A / 0x960) * kx;
    sh = (static_cast<s32>(h) * scale * 0xB4 / 0x12C0) * ky;
  } else if ((a->flags & kActorRawScale) == 0) {
    sw = (static_cast<s32>(w) * scale * 0x5A / 0x960) * a->scale_x;
    sh = (static_cast<s32>(h) * scale * 0xB4 / 0x12C0) * a->scale_y;
  } else {
    sw = static_cast<s32>(w) * a->scale_x;
    sh = static_cast<s32>(h) * a->scale_y;
  }
  if (sw == 0) sw = 0x1000;
  if (sh == 0) sh = 0x1000;
  if (0x3F000 < sw) {
    if ((a->flags & kActorClampScale) == 0) {
      scaled = 0;
    } else {
      sw = 0x3F000;
      sh = static_cast<s32>((h * 0x3F000u) / w);
    }
  }
  const u32 step_x = (w << 24) / static_cast<u32>(sw);
  const u32 screen_w = (static_cast<u32>(tw) + step_x - 1) / step_x;
  const u32 screen_h = static_cast<u32>(th) / ((h << 24) / static_cast<u32>(sh));

  auto* p = static_cast<psyq::POLY_FT4*>(screen->alloc(sizeof(psyq::POLY_FT4)));
  if (p == nullptr) return;
  s32 xs = 0x1000, ys = 0x1000;
  s32 u0, v0, u1, v1;
  const u8* pixels = a->pixels + read_u32(frame + 8);
  u8* palette = const_cast<u8*>(a->palettes) + frame[0] * 0x20 + 4;
  if (scaled == 0) {
    u32 uv = 0;
    const s32 slot = sprite_cache_b_alloc(sys, &uv, read_u16(a->palettes) >> 15, &p->clut, pixels, palette,
                                          static_cast<s32>(w), static_cast<s32>(h), a->blend);
    a->cache_slot[a->cache_slots] = static_cast<s8>(slot);
    if (slot == -1) return;
    xs = static_cast<s32>((screen_w << 12) / w);
    ys = static_cast<s32>((screen_h << 12) / h);
    ++a->cache_slots;
    a->cache_slot[a->cache_slots - 1] = static_cast<s8>(a->cache_slot[a->cache_slots - 1] + 0x20);
    p->tpage = 0x15;
    u0 = static_cast<s32>((uv & 0xFF) << 2);
    v0 = static_cast<s32>(uv >> 8 & 0xFF);
    u1 = static_cast<s32>((uv >> 16 & 0xFF) << 2);
    v1 = static_cast<s32>(uv >> 24);
  } else {
    u8* work = screen->extra.get() + screen->extra_cursor;
    const s32 phase_x = (static_cast<s32>(static_cast<s16>(read_u16(frame + 4))) * sw) / static_cast<s32>(w) & 0xFFF;
    const s32 phase_y = (static_cast<s32>(static_cast<s16>(read_u16(frame + 6))) * sh) / static_cast<s32>(h) & 0xFFF;
    const s32 slot = sprite_cache_a_alloc(sys, &u0, &v0, &u1, &v1, &work, pixels, palette, sw, sh, tw, th,
                                          static_cast<s32>(screen_w), static_cast<s32>(screen_h), phase_x, phase_y);
    a->cache_slot[a->cache_slots] = static_cast<s8>(slot);
    if (slot == -1) return;
    ++a->cache_slots;
    p->tpage = 0x105;
  }

  // Colour: the actor's light, optionally tinted by the part (and shifted per frame).
  s32 c[3] = {a->light[0], a->light[1], a->light[2]};
  const u8 pflags = part[0x16];
  if (pflags & 4) {
    const s32 delta = static_cast<s8>(part[0x1A]);
    for (int k = 0; k < 3; ++k) {
      if (delta == 0) {
        c[k] = (c[k] * part[0x17 + k]) >> 7;
      } else {
        c[k] = std::clamp(((c[k] * part[0x17 + k]) >> 7) + globals().frame_ticks * delta, 0, 0xFF);
      }
    }
  }
  u8 code = 0x2E;
  if (a->blend != 0) {
    p->tpage = static_cast<u16>(p->tpage | (a->blend - 1) << 5);
  } else if (pflags & 0x80) {
    p->tpage = static_cast<u16>(p->tpage | (pflags & 0x60));
  } else {
    code = 0x2C;
  }
  psyq::setlen(p, 9);
  p->code = code;
  p->r0 = static_cast<u8>(c[0]);
  p->g0 = static_cast<u8>(c[1]);
  p->b0 = static_cast<u8>(c[2]);

  // The part's corners (+ its offset), scaled to the screen size when resampled.
  s32 px[4], py[4];
  const s32 ox = static_cast<s16>(read_u16(part + 0x10)), oy = static_cast<s16>(read_u16(part + 0x12));
  for (int k = 0; k < 4; ++k) {
    px[k] = static_cast<s16>(read_u16(part + k * 4)) + ox;
    py[k] = static_cast<s16>(read_u16(part + k * 4 + 2)) + oy;
    if (scaled != 0) {
      px[k] = (px[k] * static_cast<s32>(screen_w)) / static_cast<s32>(w);
      py[k] = (py[k] * static_cast<s32>(screen_h)) / static_cast<s32>(h);
    }
  }
  if (flip_x != 0) {
    const s32 k = scaled == 0 ? static_cast<s8>(cel[1])
                              : (static_cast<s32>(screen_w) * static_cast<s8>(cel[1])) / static_cast<s32>(w);
    for (s32& x : px) x = k - x;
  }
  if (flip_y != 0) {
    const s32 k = scaled == 0 ? static_cast<s8>(cel[2])
                              : (static_cast<s32>(screen_h) * static_cast<s8>(cel[2])) / static_cast<s32>(h);
    for (s32& y : py) y = k - y;
  }
  s16* xy[4][2] = {{&p->x0, &p->y0}, {&p->x1, &p->y1}, {&p->x2, &p->y2}, {&p->x3, &p->y3}};
  for (int k = 0; k < 4; ++k) {
    *xy[k][0] = static_cast<s16>(a->origin_x + ((((px[k] * cos_r + py[k] * sin_r) >> 7) * xs) >> 17));
    *xy[k][1] = static_cast<s16>(a->origin_y + ((((px[k] * -sin_r + py[k] * cos_r) >> 7) * ys) >> 17));
  }

  // Texture edges: shrink the UV rectangle by a texel where the quad isn't 1:1.
  const s32 dx = p->x3 - p->x0;
  if (dx < 0) {
    const s32 du = u1 - u0 < 0 ? u0 - u1 : u1 - u0;
    if (-dx == du) {
      if (u0 > 0) --u0;
    } else if (u0 > 0 && (pflags >> 3 & 1)) {
      --u0;
    }
    --u1;
  } else {
    const s32 ex = p->x2 - p->x1;
    const s32 du = u1 - u0 < 0 ? u0 - u1 : u1 - u0;
    if (dx != du || (ex < 0 ? -ex : ex) != du) --u1;
  }
  const s32 dy = p->y3 - p->y0;
  if (dy < 0 && p->y0 <= p->y1) {
    const s32 dv = v1 - v0 < 0 ? v0 - v1 : v1 - v0;
    if (-dy == dv && v0 > 0) --v0;
    --v1;
  } else {
    const s32 ey = p->y2 - p->y1;
    const s32 dv = v1 - v0 < 0 ? v0 - v1 : v1 - v0;
    if ((dy < 0 ? -dy : dy) != dv || (ey < 0 ? -ey : ey) != dv) --v1;
  }
  p->u0 = p->u2 = static_cast<u8>(u0);
  p->v0 = p->v1 = static_cast<u8>(v0);
  p->u1 = p->u3 = static_cast<u8>(u1);
  p->v2 = p->v3 = static_cast<u8>(v1);
  if (a->prim_count < 24) {  // (unchecked on the console)
    a->prim_ot[a->prim_count] = static_cast<u16>(a->layer + 1);
    a->prims[a->prim_count++] = &p->tag;
  }
}

}  // namespace ss::game::field
