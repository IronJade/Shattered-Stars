#include "game/battle/stage.h"

#include <cstring>

#include "disc/slz.h"
#include "game/battle/battle.h"
#include "game/battle/render.h"
#include "game/battle/vram.h"
#include "psx/gte.h"
#include "runtime/libgpu.h"
#include "runtime/runtime.h"

namespace ss::game::battle {
namespace {

StageData g_stage;
s32 g_pool_a_used = 0;

s32 rd32(const u8* p) {
  s32 v;
  std::memcpy(&v, p, 4);
  return v;
}
u32 rdu32(const u8* p) {
  u32 v;
  std::memcpy(&v, p, 4);
  return v;
}
s16 rd16(const u8* p) {
  s16 v;
  std::memcpy(&v, p, 2);
  return v;
}
u16 rdu16(const u8* p) {
  u16 v;
  std::memcpy(&v, p, 2);
  return v;
}

std::vector<u8> unpack_at(std::span<const u8> list, u32 offset) {
  if (offset >= list.size()) return {};
  auto out = disc::slz::unpack_block(list.subspan(offset), 0, 0, nullptr);
  return out ? std::move(*out) : std::vector<u8>{};
}

}  // namespace

StageData& stage() { return g_stage; }
s32& pool_a_bytes_used() { return g_pool_a_used; }

bool arena_has_room(const RenderContext* pool, s32 ps1_bytes) {
  // screen_arena_has_room 0x80069fa8: cursor + n <= end - 0x100 (only pool A is tracked).
  if (pool != bg().prim_pool_a) return true;
  return g_pool_a_used + ps1_bytes <= 62000 - 0x100;
}

void stage_load_ctx_init(StageLoadCtx* ctx) {
  ctx->clut_x = 0;
  ctx->clut_y = 0x1E0;
  ctx->light = 0;
  ctx->anim_frames = 0;
  ctx->anim_groups = 0;
  g_stage.obstacle_count = 0;
}

size_t load_stage(StageLoadCtx* ctx, std::span<const u8> list) {
  StageData& s = g_stage;
  ViewState& v = view();
  size_t p = 0;
  auto at = [&](size_t off) { return list.data() + p + off; };
  while (p + 4 <= list.size()) {
    const u32 op = rdu32(at(0));
    if (op == 0) return p + 4;
    switch (op) {
      case 1:
      case 3: {
        // SLZ texture: u16 mode, u16 CLUT count, CLUT, u16 width (VRAM units), u16 height, pixels.
        const u16 x = rdu16(at(4)), y = rdu16(at(6));
        std::vector<u8> tex = unpack_at(list, rdu32(at(8)));
        if (tex.size() >= 4) {
          const u16 mode = rdu16(tex.data()) & 3;
          const u16 clut_n = rdu16(tex.data() + 2);
          size_t q = 4;
          if (clut_n != 0) {
            const u16 id = upload_clut256(tex, q, ctx->clut_x, ctx->clut_y);
            if (op == 1) {
              s.tex_clut[s.tex_count & 15] = id;
            } else {
              s.panorama_clut = id;
            }
            q += clut_n * 2u;
            ++ctx->clut_y;
          }
          if (op == 1) {
            ++s.tex_count;
            s.model_tpage[0] = 5;
          } else {
            ctx->panorama_x = static_cast<s16>(x);
            ctx->panorama_y = static_cast<s16>(y);
            s.has_panorama = 1;
          }
          if (q + 4 <= tex.size()) {
            const u16 w = rdu16(tex.data() + q), h = rdu16(tex.data() + q + 2);
            upload_texture(tex, q + 4, mode, 0, x, y, w << (2 - mode), h);
          }
          psyq::DrawSync(0);
        }
        p += 12;
        break;
      }
      case 2:
        s.blocks.push_back(unpack_at(list, rdu32(at(4))));
        p += 8;
        break;
      case 4: {
        // Panorama layout: u16 mode, a, b, c, then {u16 x, y, w, h} until -1 (stage 0 only).
        s.panorama_mode = static_cast<u8>(rdu16(at(4)));
        s.panorama_a = rdu16(at(6));
        s.panorama_b = rdu16(at(8));
        s.panorama_c = rdu16(at(10));
        p += 12;
        s.panorama_rects.clear();
        while (p + 4 <= list.size() && rd32(at(0)) != -1) {
          s.panorama_rects.push_back({static_cast<u16>(ctx->panorama_x + rdu16(at(0))),
                                      static_cast<u16>(ctx->panorama_y + rdu16(at(2))), rdu16(at(4)), rdu16(at(6))});
          p += 8;
        }
        p += 4;
        break;
      }
      case 5:
        v.fog_on = 1;
        v.fog_rgb[0] = *at(4);
        v.fog_rgb[1] = *at(5);
        v.fog_rgb[2] = *at(6);
        v.fog_near = static_cast<s16>(rdu32(at(8)));
        p += 12;
        break;
      case 6:
        v.clear_rgb[0] = *at(4);
        v.clear_rgb[1] = *at(5);
        v.clear_rgb[2] = *at(6);
        v.cull_threshold = static_cast<u16>(rdu32(at(8)));
        s.bounds[0] = rd16(at(12)) << 12;
        s.bounds[1] = rd16(at(14)) << 12;
        s.bounds[2] = rd16(at(16)) << 12;
        s.bounds[3] = rd16(at(18)) << 12;
        p += 20;
        break;
      case 7:
        v.ambient[0] = *at(4);
        v.ambient[1] = *at(5);
        v.ambient[2] = *at(6);
        p += 8;
        break;
      case 8: {
        // Directional light: s16 dx dy dz, pad, u8 r g b (colour << 2).
        s.has_lights = 1;
        const int n = ctx->light & 3;
        v.light_mask = static_cast<u8>(v.light_mask | 1 << (ctx->light & 0x1F));
        v.light_dir[n][0] = rd16(at(4));
        v.light_dir[n][1] = rd16(at(6));
        v.light_dir[n][2] = rd16(at(8));
        v.light_colour[n][0] = static_cast<s16>(*at(12) << 2);
        v.light_colour[n][1] = static_cast<s16>(*at(13) << 2);
        v.light_colour[n][2] = static_cast<s16>(*at(14) << 2);
        ++ctx->light;
        p += 16;
        break;
      }
      case 9:
        s.height_grid = unpack_at(list, rdu32(at(4)));
        s.height_grid_on = 1;
        p += 8;
        break;
      case 10: {
        // Texture animation group: s16 dest x, y; {u32 duration, u16 x0 y0 x1 y1} until 0.
        const int group = ctx->anim_groups & 3;
        s.anim_start[group] = static_cast<u8>(ctx->anim_frames);
        s.anim_frame[group] = static_cast<u8>(ctx->anim_frames);
        s.anim_dest[group * 2] = rd16(at(4));
        s.anim_dest[group * 2 + 1] = rd16(at(6));
        p += 8;
        while (p + 4 <= list.size() && rdu32(at(0)) != 0) {
          const int f = ctx->anim_frames & 31;
          s.anim_durations[f] = static_cast<s16>(rd32(at(0)));
          const s16 x0 = rd16(at(4)), y0 = rd16(at(6)), x1 = rd16(at(8)), y1 = rd16(at(10));
          s.anim_frames[f] = psyq::RECT{x0, y0, static_cast<s16>(x1 - x0), static_cast<s16>(y1 - y0)};
          ++ctx->anim_frames;
          p += 12;
        }
        p += 4;
        s.anim_timer[group] = 0;
        s.anim_end[group] = static_cast<s8>(ctx->anim_frames);
        ++ctx->anim_groups;
        break;
      }
      case 11:
      case 17: {
        if (s.obstacle_count < s.obstacles.size()) {
          auto& o = s.obstacles[s.obstacle_count++];
          o.kind = op == 17 ? 2 : 1;
          for (int i = 0; i < 3; ++i) {
            o.min[i] = rd16(at(4 + 2 * i)) << 12;
            o.max[i] = rd16(at(10 + 2 * i)) << 12;
          }
        }
        p += 16;
        break;
      }
      case 12: {
        // The CLUT of a TIM (at +0x14) to the next row.
        const u32 off = rdu32(at(4)) + 0x14;
        if (off < list.size()) upload_clut256(list, off, ctx->clut_x, ctx->clut_y);
        ++ctx->clut_y;
        psyq::DrawSync(0);
        p += 8;
        break;
      }
      case 13: {
        // Stage sprite set: layout (raw), animation table, frames, sheet (CLUT row 0x1e8, image (0x280, 0)).
        const u32 o0 = rdu32(at(4)), o1 = rdu32(at(8)), o2 = rdu32(at(12)), o3 = rdu32(at(16));
        s.sprites_present = 1;
        if (o0 <= o1 && o1 <= list.size()) s.sprite_layout.assign(list.begin() + o0, list.begin() + o1);
        s.sprite_anims = unpack_at(list, o1);
        s.sprite_frames = unpack_at(list, o2);
        s.sprite_sheet = unpack_at(list, o3);
        if (s.sprite_sheet.size() >= 8) {
          const u32 clut = rdu32(s.sprite_sheet.data());
          const u32 image = rdu32(s.sprite_sheet.data() + 4);
          upload_clut256(s.sprite_sheet, clut + 4, 0, 0x1E8);
          if (image + 4 <= s.sprite_sheet.size()) {
            const u16 w = rdu16(s.sprite_sheet.data() + image), h = rdu16(s.sprite_sheet.data() + image + 2);
            upload_texture(s.sprite_sheet, image + 4, 0, 0, 0x280, 0, w, h);
          }
          psyq::DrawSync(0);
        }
        p += 20;
        break;
      }
      case 14: {
        // Light from angles: u16 azimuth, elevation (degrees), u8 r g b (colour << 4).
        s.has_lights = 1;
        const int n = ctx->light & 3;
        v.light_mask = static_cast<u8>(v.light_mask | 1 << (ctx->light & 0x1F));
        const u32 az = rdu16(at(4)), el = rdu16(at(6));
        const u32 e = el * 0x1000 / 0x168;
        const u32 a = az * 0x1000 / 0x168;
        v.light_dir[n][0] = static_cast<s16>(bcos(e) * bcos(a) >> 12);
        v.light_dir[n][1] = static_cast<s16>(bsin(e));
        v.light_dir[n][2] = static_cast<s16>(bcos(e) * bsin(a) >> 12);
        v.light_colour[n][0] = static_cast<s16>(*at(8) << 4);
        v.light_colour[n][1] = static_cast<s16>(*at(9) << 4);
        v.light_colour[n][2] = static_cast<s16>(*at(10) << 4);
        ++ctx->light;
        p += 12;
        break;
      }
      case 15:
        s.flare_on = 1;
        s.flare_pos[0] = rd16(at(4)) << 12;
        s.flare_pos[1] = rd16(at(6)) << 12;
        s.flare_pos[2] = rd16(at(8)) << 12;
        s.flare_size = rdu16(at(10));
        s.flare_colour = rdu32(at(12));
        s.flare_b = *at(16);
        s.flare_cos = static_cast<s16>(bcos(static_cast<u32>(rd16(at(18)) * 0x1000 / 0x168)));
        p += 20;
        break;
      case 16:
        v.clear_alt_on = 1;
        v.clear_rgb_alt[0] = *at(4);
        v.clear_rgb_alt[1] = *at(5);
        v.clear_rgb_alt[2] = *at(6);
        p += 8;
        break;
      default:
        runtime::Runtime::get().log_once("battle-stage-op", "battle: unknown stage op " + std::to_string(op));
        return 0;
    }
  }
  return 0;
}

s32 stage_ground_height(s32 x, s32 z) {
  const StageData& s = g_stage;
  s32 h = 0;
  if (s.height_grid_on && s.height_grid.size() >= 16) {
    const u8* g = s.height_grid.data();
    auto at = [&](s32 i) -> s32 {
      const size_t off = static_cast<size_t>(i) * 2;
      return off + 2 <= s.height_grid.size() ? rd16(g + off) : 0;
    };
    const s32 x0 = at(0), z0 = at(1), x1 = at(2), z1 = at(3);
    if (x0 <= x && x < x1 && z0 <= z && z < z1) {
      const s32 cw = static_cast<u16>(at(4)), ch = static_cast<u16>(at(5)), stride = at(6);
      s32 dx = x - x0, dz = z - z0;
      const s32 cx = dx / cw;
      dx %= cw;
      const s32 cz = dz / ch;
      dz %= ch;
      const s32 r0 = cz * stride, r1 = (cz + 1) * stride;
      const s32 a = at(r0 + cx + 8), b = at(r0 + cx + 9), c = at(r1 + cx + 8), d = at(r1 + cx + 9);
      if (a != 0 || b != 0 || c != 0 || d != 0) {
        const s32 wx = cw - dx, wz = ch - dz;
        const s32 area = cw * ch;
        h = (a * wx * wz + b * dx * wz + c * wx * dz + d * dx * dz) / area;
      }
    }
  }
  // TODO(port): g_stage_height_fn (0x8006efe8), set by the stage code blocks.
  return h << 12;
}

void stage_draw_and_animate() {
  ViewState& v = view();
  StageData& s = g_stage;
  BattleGlobals& g = bg();
  lights_build(v.stage_orient);
  s32 pos[3] = {0, 0, 0};
  s32 orient[3] = {v.stage_orient[0], v.stage_orient[1], v.stage_orient[2]};
  static const s32 kUnitScale[3] = {0x1000, 0x1000, 0x1000};  // g_zero_vector_2 0x8006e34c
  obj_set_matrix(pos, orient, kUnitScale);
  if (!s.blocks.empty() && !s.blocks[0].empty()) {
    model_render(g.prim_pool_a, s.blocks[0].data(), s.tex_clut[0], s.model_tpage[0], v.fog_on != 0, 0);
  }
  for (int i = 0; i < 4; ++i) {
    if (s.anim_start[i] == 0xFF) continue;
    if (s.anim_timer[i] < 1) {
      do {
        const u8 next = static_cast<u8>(s.anim_frame[i] + 1);
        s.anim_frame[i] = next;
        if (static_cast<s32>(s.anim_end[i]) <= static_cast<s32>(next)) s.anim_frame[i] = s.anim_start[i];
        s.anim_timer[i] = static_cast<s16>(s.anim_timer[i] + s.anim_durations[s.anim_frame[i] & 31]);
      } while (s.anim_timer[i] < 1);
      psyq::MoveImage(&s.anim_frames[s.anim_frame[i] & 31], s.anim_dest[i * 2], s.anim_dest[i * 2 + 1]);
    }
    s.anim_timer[i] = static_cast<s16>(s.anim_timer[i] - g.frame_ticks);
  }
}

// ---- model_render ---------------------------------------------------------------------

namespace {

using psx::Gte;

struct ModelRender {
  RenderContext* pool;
  s32 bias;
  bool fog;
  u32 tpage_hi;     // 0x1f800284: tpage << 16
  s32 thr;          // 0x1f800288: g_cull_threshold
  s32 neg_thr;      // 0x1f80028c
  s32 budget;       // 0x1f800290: free PS1 bytes - 0x100
  u16 clut;         // 0x1f800294
  u32 colour_808080 = 0x808080;  // 0x1f800000
  Gte& g = psyq::gte();

  void ldv(int i, const u8* p) {
    g.write_data(Gte::VXY0 + 2 * i, rdu32(p));
    g.write_data(Gte::VZ0 + 2 * i, rdu32(p + 4));
  }
  void ldrgb(const u8* p) { g.write_data(Gte::RGBC, rdu32(p)); }
  void ld_up_normal() {
    g.write_data(Gte::VXY0, 0x1000u << 16);
    g.write_data(Gte::VZ0, 0);
  }
  void ncs() { g.execute(fog ? Gte::kNcds : Gte::kNccs); }
  void nct() { g.execute(fog ? Gte::kNcdt : Gte::kNcct); }
  s32 flag() const { return static_cast<s32>(g.read_ctrl(Gte::FLAG)); }
  u32 sxy(int i) const { return g.read_data(Gte::SXY0 + i); }
  u32 rgb(int i) const { return g.read_data(Gte::RGB0 + i); }
  s32 otz() const { return static_cast<s32>(g.read_data(Gte::OTZ)); }
  bool semi(const u8* word) const { return (rdu32(word) & 0x10000) != 0; }
  // Any vertex inside x 0..319 (u16) and any inside y -64..279.
  static bool on_screen(const u32* xy, int n) {
    bool x_ok = false, y_ok = false;
    for (int i = 0; i < n; ++i) x_ok |= (xy[i] & 0xFFFF) < 320;
    for (int i = 0; i < n; ++i) y_ok |= static_cast<u16>((xy[i] >> 16) + 64) < 344;
    return x_ok && y_ok;
  }
  template <typename T>
  T* alloc(s32 ps1_bytes) {
    budget -= ps1_bytes;
    g_pool_a_used += pool == bg().prim_pool_a ? ps1_bytes : 0;
    return screen_alloc_prims<T>(pool->arena, 1);
  }
  void add(s32 z8, void* prim) {
    const s32 idx = bias + 0x610 - z8;
    if (prim != nullptr && pool->ot_head != nullptr) psyq::addPrim(&pool->ot_head[idx], prim);
  }
  void add_mode(s32 z8) {
    auto* m = alloc<psyq::DR_MODE>(12);
    if (m == nullptr) return;
    psyq::SetDrawMode(m, 0, 1, bg().semi_tpage, nullptr);
    add(z8, m);
  }
  static void set_xy(s16* dst, u32 v) {
    dst[0] = static_cast<s16>(v & 0xFFFF);
    dst[1] = static_cast<s16>(v >> 16);
  }
  static void set_rgb(u8* dst, u32 v) {
    dst[0] = static_cast<u8>(v);
    dst[1] = static_cast<u8>(v >> 8);
    dst[2] = static_cast<u8>(v >> 16);
  }

  // ---- lit sections ----
  void f3(const u8* r) {  // 9 words: normal, rgbc, v0 v1 v2
    ldv(0, r + 12), ldv(1, r + 20), ldv(2, r + 28);
    g.execute(Gte::kRtpt);
    if (budget < 0) throw Stop{};
    g.execute(Gte::kNclip);
    if (g.mac(0) < neg_thr) return;
    const u32 xy[3] = {sxy(0), sxy(1), sxy(2)};
    if (flag() < 0) return;
    g.execute(Gte::kAvsz3);
    const s32 z = otz();
    // ORIGINAL BUG (original_bugs.md B20): section 0 compares the OTZ before its >> 3 (the
    // shift is scheduled after the compare, 0x80078658-0x80078668), where the other 19
    // sections compare OTZ >> 3, so lit flat triangles drop out at an eighth of the depth:
    //   if (z + bias >= 0x600) return;
    // The depth test is shifted like the other sections'. Drawing only.
    if ((z >> 3) + bias >= 0x600) return;
    ldv(0, r), ldrgb(r + 8);
    ncs();
    auto* p = alloc<psyq::POLY_F3>(20);
    if (p == nullptr) return;
    psyq::setPolyF3(p);
    set_rgb(&p->r0, rgb(2));
    p->code = semi(r + 16) ? 0x22 : 0x20;
    set_xy(&p->x0, xy[0]), set_xy(&p->x1, xy[1]), set_xy(&p->x2, xy[2]);
    add(z >> 3, p);
    if (semi(r + 16)) add_mode(z >> 3);
  }
  void g3(const u8* r) {  // 13 words: rgbc, n0 n1 n2, v0 v1 v2
    ldv(0, r + 28), ldv(1, r + 36), ldv(2, r + 44);
    g.execute(Gte::kRtpt);
    if (budget < 0) throw Stop{};
    g.execute(Gte::kNclip);
    if (g.mac(0) < neg_thr) return;
    const u32 xy[3] = {sxy(0), sxy(1), sxy(2)};
    if (flag() < 0) return;
    g.execute(Gte::kAvsz3);
    const s32 z = otz() >> 3;
    ldv(0, r + 4), ldv(1, r + 12), ldv(2, r + 20), ldrgb(r);
    nct();
    if (z + bias >= 0x600) return;
    auto* p = alloc<psyq::POLY_G3>(28);
    if (p == nullptr) return;
    psyq::setPolyG3(p);
    set_rgb(&p->r0, rgb(0)), set_rgb(&p->r1, rgb(1)), set_rgb(&p->r2, rgb(2));
    p->code = semi(r + 32) ? 0x32 : 0x30;
    set_xy(&p->x0, xy[0]), set_xy(&p->x1, xy[1]), set_xy(&p->x2, xy[2]);
    add(z, p);
    if (semi(r + 32)) add_mode(z);
  }
  void f3c(const u8* r) {  // 11 words: c0 c1 c2, normal, v0 v1 v2
    ldv(0, r + 20), ldv(1, r + 28), ldv(2, r + 36);
    g.execute(Gte::kRtpt);
    if (budget < 0) throw Stop{};
    g.execute(Gte::kNclip);
    if (g.mac(0) < neg_thr) return;
    const u32 xy[3] = {sxy(0), sxy(1), sxy(2)};
    if (flag() < 0) return;
    g.execute(Gte::kAvsz3);
    const s32 z = otz() >> 3;
    ldv(0, r + 12), ldrgb(r);
    ncs();
    if (z + bias >= 0x600) return;
    const u32 c0 = rgb(2);
    ldrgb(r + 4);
    ncs();
    const u32 c1 = rgb(2);
    ldrgb(r + 8);
    ncs();
    const u32 c2 = rgb(2);
    auto* p = alloc<psyq::POLY_G3>(28);
    if (p == nullptr) return;
    psyq::setPolyG3(p);
    set_rgb(&p->r0, c0), set_rgb(&p->r1, c1), set_rgb(&p->r2, c2);
    p->code = semi(r + 24) ? 0x32 : 0x30;
    set_xy(&p->x0, xy[0]), set_xy(&p->x1, xy[1]), set_xy(&p->x2, xy[2]);
    add(z, p);
    if (semi(r + 24)) add_mode(z);
  }
  void g3n(const u8* r) {  // 15 words: c0 c1 c2, n0 n1 n2, v0 v1 v2
    ldv(0, r + 36), ldv(1, r + 44), ldv(2, r + 52);
    g.execute(Gte::kRtpt);
    if (budget < 0) throw Stop{};
    g.execute(Gte::kNclip);
    if (g.mac(0) < neg_thr) return;
    const u32 xy[3] = {sxy(0), sxy(1), sxy(2)};
    if (flag() < 0) return;
    g.execute(Gte::kAvsz3);
    const s32 z = otz() >> 3;
    ldv(0, r + 12), ldrgb(r);
    ncs();
    if (z + bias >= 0x600) return;
    const u32 c0 = rgb(2);
    ldv(0, r + 20), ldrgb(r + 4);
    ncs();
    const u32 c1 = rgb(2);
    ldv(0, r + 28), ldrgb(r + 8);
    ncs();
    const u32 c2 = rgb(2);
    auto* p = alloc<psyq::POLY_G3>(28);
    if (p == nullptr) return;
    psyq::setPolyG3(p);
    set_rgb(&p->r0, c0), set_rgb(&p->r1, c1), set_rgb(&p->r2, c2);
    p->code = semi(r + 40) ? 0x32 : 0x30;
    set_xy(&p->x0, xy[0]), set_xy(&p->x1, xy[1]), set_xy(&p->x2, xy[2]);
    add(z, p);
    if (semi(r + 40)) add_mode(z);
  }
  void ft3(const u8* r) {  // 11 words: uv0 clut, uv1 tpage, uv2 -, normal, v0 v1 v2 (colour 0x808080)
    ldv(0, r + 20), ldv(1, r + 28), ldv(2, r + 36);
    g.execute(Gte::kRtpt);
    if (budget < 0) throw Stop{};
    g.execute(Gte::kNclip);
    if (g.mac(0) < neg_thr) return;
    const u32 xy[3] = {sxy(0), sxy(1), sxy(2)};
    if (flag() < 0) return;
    g.execute(Gte::kAvsz3);
    const s32 z = otz() >> 3;
    ldv(0, r + 12);
    ncs();
    if (z + bias >= 0x600) return;
    auto* p = alloc<psyq::POLY_FT3>(32);
    if (p == nullptr) return;
    psyq::setPolyFT3(p);
    set_rgb(&p->r0, rgb(2));
    p->code = semi(r + 24) ? 0x26 : 0x24;
    set_xy(&p->x0, xy[0]), set_xy(&p->x1, xy[1]), set_xy(&p->x2, xy[2]);
    set_uv_clut(&p->u0, r, &p->u1, &p->u2);
    add(z, p);
  }
  void gt3(const u8* r) {  // 15 words: uv0 clut, uv1 tpage, uv2 -, n0 n1 n2, v0 v1 v2
    ldv(0, r + 36), ldv(1, r + 44), ldv(2, r + 52);
    g.execute(Gte::kRtpt);
    if (budget < 0) throw Stop{};
    g.execute(Gte::kNclip);
    if (g.mac(0) < neg_thr) return;
    const u32 xy[3] = {sxy(0), sxy(1), sxy(2)};
    if (flag() < 0) return;
    if (!on_screen(xy, 3)) return;
    g.execute(Gte::kAvsz3);
    const s32 z = otz() >> 3;
    ldv(0, r + 12), ldv(1, r + 20), ldv(2, r + 28);
    nct();
    if (z + bias >= 0x600) return;
    auto* p = alloc<psyq::POLY_GT3>(40);
    if (p == nullptr) return;
    psyq::setPolyGT3(p);
    set_rgb(&p->r0, rgb(0)), set_rgb(&p->r1, rgb(1)), set_rgb(&p->r2, rgb(2));
    p->code = semi(r + 40) ? 0x36 : 0x34;
    set_xy(&p->x0, xy[0]), set_xy(&p->x1, xy[1]), set_xy(&p->x2, xy[2]);
    set_uv_clut(&p->u0, r, &p->u1, &p->u2);
    add(z, p);
  }
  // Quads whose record starts with the vertex RTPS projects (it becomes xy0).
  // 'both' = the original skips only when the RTPS and the NCLIP flags are both errors.
  bool quad_rtps_first(const u8* va, const u8* vb, u32 xy[4], bool both, bool clip) {
    ldv(0, va);
    g.execute(Gte::kRtps);
    if (budget < 0) throw Stop{};
    xy[0] = sxy(2);
    const s32 f0 = flag();
    ldv(0, vb), ldv(1, vb + 8), ldv(2, vb + 16);
    g.execute(Gte::kRtpt);
    g.execute(Gte::kNclip);
    if (thr < g.mac(0)) return false;
    xy[1] = sxy(0), xy[2] = sxy(1), xy[3] = sxy(2);
    const s32 f1 = flag();
    if (both ? (f1 < 0 && f0 < 0) : (f1 < 0 || f0 < 0)) return false;
    if (clip && !on_screen(xy, 4)) return false;
    g.execute(Gte::kAvsz4);
    return true;
  }
  void f4(const u8* r) {  // 11 words: normal, rgbc, vA, v v v
    u32 xy[4];
    if (!quad_rtps_first(r + 12, r + 20, xy, true, false)) return;
    const s32 z = otz() >> 3;
    if (z + bias >= 0x600) return;
    ldv(0, r), ldrgb(r + 8);
    ncs();
    auto* p = alloc<psyq::POLY_F4>(24);
    if (p == nullptr) return;
    psyq::setPolyF4(p);
    const u32 c = rgb(2);
    set_rgb(&p->r0, c);
    p->code = static_cast<u8>(c >> 24);
    set_xy(&p->x0, xy[0]), set_xy(&p->x1, xy[1]), set_xy(&p->x2, xy[2]), set_xy(&p->x3, xy[3]);
    add(z, p);
  }
  void g4(const u8* r) {  // 17 words: rgbc, nA, n n n, vA, v v v
    u32 xy[4];
    if (!quad_rtps_first(r + 36, r + 44, xy, true, false)) return;
    const s32 z = otz() >> 3;
    ldv(0, r + 4), ldrgb(r);
    ncs();
    const u32 c0 = rgb(2);
    if (z + bias >= 0x600) return;
    ldv(0, r + 12), ldv(1, r + 20), ldv(2, r + 28);
    nct();
    auto* p = alloc<psyq::POLY_G4>(36);
    if (p == nullptr) return;
    psyq::setPolyG4(p);
    set_rgb(&p->r0, c0);
    p->code = static_cast<u8>(c0 >> 24);
    set_rgb(&p->r1, rgb(0)), set_rgb(&p->r2, rgb(1)), set_rgb(&p->r3, rgb(2));
    set_xy(&p->x0, xy[0]), set_xy(&p->x1, xy[1]), set_xy(&p->x2, xy[2]), set_xy(&p->x3, xy[3]);
    add(z, p);
  }
  void g4n(const u8* r) {  // 20 words: c0..c3, n0..n3, vA, v v v
    u32 xy[4];
    if (!quad_rtps_first(r + 48, r + 56, xy, true, false)) return;
    const s32 z = otz();
    u32 c[4];
    for (int k = 0; k < 4; ++k) {
      ldv(0, r + 16 + 8 * k), ldrgb(r + 4 * k);
      ncs();
      c[k] = rgb(2);
    }
    if ((z >> 3) + bias >= 0x600) return;
    auto* p = alloc<psyq::POLY_G4>(36);
    if (p == nullptr) return;
    psyq::setPolyG4(p);
    set_rgb(&p->r0, c[0]), set_rgb(&p->r1, c[1]), set_rgb(&p->r2, c[2]), set_rgb(&p->r3, c[3]);
    p->code = static_cast<u8>(c[0] >> 24);
    set_xy(&p->x0, xy[0]), set_xy(&p->x1, xy[1]), set_xy(&p->x2, xy[2]), set_xy(&p->x3, xy[3]);
    add(z >> 3, p);
  }
  void ft4(const u8* r) {  // 13 words: uv0 clut, uv1 tpage, uv2 uv3, normal, vA, v v v (colour 0x808080)
    u32 xy[4];
    if (!quad_rtps_first(r + 20, r + 28, xy, true, true)) return;
    const s32 z = otz() >> 3;
    ldv(0, r + 12);
    ncs();
    if (z + bias >= 0x600) return;
    auto* p = alloc<psyq::POLY_FT4>(40);
    if (p == nullptr) return;
    psyq::setPolyFT4(p);
    set_rgb(&p->r0, rgb(2));
    p->code = semi(r + 24) ? 0x2E : 0x2C;
    set_xy(&p->x0, xy[0]), set_xy(&p->x1, xy[1]), set_xy(&p->x2, xy[2]), set_xy(&p->x3, xy[3]);
    set_uv_clut(&p->u0, r, &p->u1, &p->u2);
    std::memcpy(&p->u3, r + 10, 2);
    add(z, p);
  }
  void gt4(const u8* r) {  // 19 words: uv0 clut, uv1 tpage, uv2 uv3, n0 n1 n2 n3, vA, v v v
    u32 xy[4];
    if (!quad_rtps_first(r + 44, r + 52, xy, true, true)) return;
    const s32 z = otz() >> 3;
    ldv(0, r + 12), ldv(1, r + 20), ldv(2, r + 28);
    nct();
    const u32 c0 = rgb(0), c1 = rgb(1), c2 = rgb(2);
    ldv(0, r + 36);
    ncs();
    const u32 c3 = rgb(2);
    if (z + bias >= 0x600) return;
    auto* p = alloc<psyq::POLY_GT4>(52);
    if (p == nullptr) return;
    psyq::setPolyGT4(p);
    set_rgb(&p->r0, c0), set_rgb(&p->r1, c1), set_rgb(&p->r2, c2), set_rgb(&p->r3, c3);
    p->code = semi(r + 48) ? 0x3E : 0x3C;
    set_xy(&p->x0, xy[0]), set_xy(&p->x1, xy[1]), set_xy(&p->x2, xy[2]), set_xy(&p->x3, xy[3]);
    set_uv_clut(&p->u0, r, &p->u1, &p->u2);
    std::memcpy(&p->u3, r + 10, 2);
    add(z, p);
  }

  // ---- unlit sections (light straight along (0, 1, 0); colour = vertex colour * stage colour + ambient) ----
  bool tri(const u8* v0, u32 xy[3], bool clip) {
    ldv(0, v0), ldv(1, v0 + 8), ldv(2, v0 + 16);
    g.execute(Gte::kRtpt);
    if (budget < 0) throw Stop{};
    g.execute(Gte::kNclip);
    if (g.mac(0) < neg_thr) return false;
    xy[0] = sxy(0), xy[1] = sxy(1), xy[2] = sxy(2);
    if (flag() < 0) return false;
    if (clip && !on_screen(xy, 3)) return false;
    g.execute(Gte::kAvsz3);
    return true;
  }
  void uf3(const u8* r) {  // 7 words: rgbc, v0 v1 v2
    u32 xy[3];
    if (!tri(r + 4, xy, false)) return;
    const s32 z = otz() >> 3;
    ld_up_normal(), ldrgb(r);
    ncs();
    const u32 c = rgb(2);
    if (z + bias >= 0x600) return;
    auto* p = alloc<psyq::POLY_F3>(20);
    if (p == nullptr) return;
    psyq::setPolyF3(p);
    set_rgb(&p->r0, c);
    p->code = static_cast<u8>(c >> 24);
    set_xy(&p->x0, xy[0]), set_xy(&p->x1, xy[1]), set_xy(&p->x2, xy[2]);
    add(z, p);
  }
  void ug3(const u8* r) {  // 9 words: c0 c1 c2, v0 v1 v2
    u32 xy[3];
    if (!tri(r + 12, xy, false)) return;
    const s32 z = otz();
    ld_up_normal();
    u32 c[3];
    for (int k = 0; k < 3; ++k) {
      ldrgb(r + 4 * k);
      ncs();
      c[k] = rgb(2);
    }
    if ((z >> 3) + bias >= 0x600) return;
    auto* p = alloc<psyq::POLY_G3>(28);
    if (p == nullptr) return;
    psyq::setPolyG3(p);
    set_rgb(&p->r0, c[0]), set_rgb(&p->r1, c[1]), set_rgb(&p->r2, c[2]);
    p->code = static_cast<u8>(c[0] >> 24);
    set_xy(&p->x0, xy[0]), set_xy(&p->x1, xy[1]), set_xy(&p->x2, xy[2]);
    add(z >> 3, p);
  }
  void uft3(const u8* r) {  // 10 words: uv0 clut, uv1 tpage, uv2 -, rgbc, v0 v1 v2
    u32 xy[3];
    if (!tri(r + 16, xy, true)) return;
    const s32 z = otz() >> 3;
    ld_up_normal(), ldrgb(r + 12);
    ncs();
    auto* p = static_cast<psyq::POLY_FT3*>(nullptr);
    const u32 c = rgb(2);
    if (z + bias >= 0x600) return;
    p = alloc<psyq::POLY_FT3>(32);
    if (p == nullptr) return;
    psyq::setPolyFT3(p);
    set_rgb(&p->r0, c);
    p->code = semi(r + 20) ? 0x26 : 0x24;
    set_xy(&p->x0, xy[0]), set_xy(&p->x1, xy[1]), set_xy(&p->x2, xy[2]);
    set_uv_clut(&p->u0, r, &p->u1, &p->u2);
    add(z, p);
  }
  void ugt3(const u8* r) {  // 12 words: uv0 clut, uv1 tpage, uv2 -, c0 c1 c2, v0 v1 v2
    u32 xy[3];
    if (!tri(r + 24, xy, true)) return;
    const s32 z = otz() >> 3;
    ld_up_normal(), ldrgb(r + 12);
    ncs();
    if (z + bias >= 0x600) return;
    const u32 c0 = rgb(2);
    ldrgb(r + 16);
    ncs();
    const u32 c1 = rgb(2);
    ldrgb(r + 20);
    ncs();
    const u32 c2 = rgb(2);
    auto* p = alloc<psyq::POLY_GT3>(40);
    if (p == nullptr) return;
    psyq::setPolyGT3(p);
    set_rgb(&p->r0, c0), set_rgb(&p->r1, c1), set_rgb(&p->r2, c2);
    p->code = semi(r + 28) ? 0x36 : 0x34;
    set_xy(&p->x0, xy[0]), set_xy(&p->x1, xy[1]), set_xy(&p->x2, xy[2]);
    set_uv_clut(&p->u0, r, &p->u1, &p->u2);
    add(z, p);
  }
  void uf4(const u8* r) {  // 9 words: rgbc, vA, v v v
    u32 xy[4];
    if (!quad_rtps_first(r + 4, r + 12, xy, true, false)) return;
    const s32 z = otz() >> 3;
    ld_up_normal(), ldrgb(r);
    ncs();
    if (z + bias >= 0x600) return;
    auto* p = alloc<psyq::POLY_F4>(24);
    if (p == nullptr) return;
    psyq::setPolyF4(p);
    set_rgb(&p->r0, rgb(2));
    p->code = semi(r + 8) ? 0x2A : 0x28;
    set_xy(&p->x0, xy[0]), set_xy(&p->x1, xy[1]), set_xy(&p->x2, xy[2]), set_xy(&p->x3, xy[3]);
    add(z, p);
  }
  void ug4(const u8* r) {  // 12 words: c0..c3, vA, v v v
    u32 xy[4];
    if (!quad_rtps_first(r + 16, r + 24, xy, true, false)) return;
    const s32 z = otz();
    ld_up_normal();
    u32 c[4];
    for (int k = 0; k < 4; ++k) {
      ldrgb(r + 4 * k);
      ncs();
      c[k] = rgb(2);
    }
    if ((z >> 3) + bias >= 0x600) return;
    auto* p = alloc<psyq::POLY_G4>(36);
    if (p == nullptr) return;
    psyq::setPolyG4(p);
    set_rgb(&p->r0, c[0]), set_rgb(&p->r1, c[1]), set_rgb(&p->r2, c[2]), set_rgb(&p->r3, c[3]);
    p->code = static_cast<u8>(c[0] >> 24);
    set_xy(&p->x0, xy[0]), set_xy(&p->x1, xy[1]), set_xy(&p->x2, xy[2]), set_xy(&p->x3, xy[3]);
    add(z >> 3, p);
  }
  void uft4(const u8* r) {  // 12 words: uv0 clut, uv1 tpage, uv2 uv3, rgbc, vA, v v v
    u32 xy[4];
    if (!quad_rtps_first(r + 16, r + 24, xy, false, true)) return;
    const s32 z = otz() >> 3;
    ld_up_normal(), ldrgb(r + 12);
    ncs();
    const u32 c = rgb(2);
    if (z + bias >= 0x600) return;
    auto* p = alloc<psyq::POLY_FT4>(40);
    if (p == nullptr) return;
    psyq::setPolyFT4(p);
    set_rgb(&p->r0, c);
    p->code = semi(r + 20) ? 0x2E : 0x2C;
    set_xy(&p->x0, xy[0]), set_xy(&p->x1, xy[1]), set_xy(&p->x2, xy[2]), set_xy(&p->x3, xy[3]);
    set_uv_clut(&p->u0, r, &p->u1, &p->u2);
    std::memcpy(&p->u3, r + 10, 2);
    add(z, p);
  }
  void ugt4(const u8* r) {  // 15 words: uv0 clut, uv1 tpage, uv2 uv3, c0..c3, v0 v1 v2 (RTPT), v3 (RTPS)
    ldv(0, r + 28), ldv(1, r + 36), ldv(2, r + 44);
    g.execute(Gte::kRtpt);
    if (budget < 0) throw Stop{};
    u32 xy[4] = {sxy(0), sxy(1), sxy(2), 0};
    const s32 f0 = flag();
    g.execute(Gte::kNclip);
    const s32 nclip = g.mac(0);
    ldv(0, r + 52);
    g.execute(Gte::kRtps);
    if (nclip < neg_thr) return;
    xy[3] = sxy(2);
    if (flag() < 0 || f0 < 0) return;
    if (!on_screen(xy, 4)) return;
    g.execute(Gte::kAvsz4);
    const s32 z = otz() >> 3;
    ld_up_normal(), ldrgb(r + 12);
    ncs();
    if (z + bias >= 0x600) return;
    u32 c[4];
    c[0] = rgb(2);
    for (int k = 1; k < 4; ++k) {
      ldrgb(r + 12 + 4 * k);
      ncs();
      c[k] = rgb(2);
    }
    auto* p = alloc<psyq::POLY_GT4>(52);
    if (p == nullptr) return;
    psyq::setPolyGT4(p);
    set_rgb(&p->r0, c[0]), set_rgb(&p->r1, c[1]), set_rgb(&p->r2, c[2]), set_rgb(&p->r3, c[3]);
    p->code = semi(r + 32) ? 0x3E : 0x3C;
    set_xy(&p->x0, xy[0]), set_xy(&p->x1, xy[1]), set_xy(&p->x2, xy[2]), set_xy(&p->x3, xy[3]);
    set_uv_clut(&p->u0, r, &p->u1, &p->u2);
    std::memcpy(&p->u3, r + 10, 2);
    add(z, p);
  }

  // uv0 + CLUT (+ model CLUT), uv1 + tpage (+ model tpage), uv2: the record's first three words.
  void set_uv_clut(u8* uv0, const u8* r, u8* uv1, u8* uv2) const {
    std::memcpy(uv0, r, 2);
    const u16 c = static_cast<u16>(rdu16(r + 2) + clut);
    std::memcpy(uv0 + 2, &c, 2);
    const u32 w = rdu32(r + 4) + tpage_hi;
    std::memcpy(uv1, &w, 4);
    std::memcpy(uv2, r + 8, 2);
  }

  struct Stop {};
};

constexpr int kSectionWords[20] = {9, 13, 11, 15, 11, 15, 11, 17, 12, 20, 13, 19, 7, 9, 10, 12, 9, 12, 12, 15};

}  // namespace

void model_render(RenderContext* pool, const u8* model, u16 clut, u16 tpage, bool fog, s32 ot_bias) {
  if (pool == nullptr || model == nullptr) return;
  ViewState& v = view();
  ModelRender m{pool, ot_bias, fog, static_cast<u32>(tpage) << 16, v.cull_threshold, -static_cast<s32>(v.cull_threshold),
                (62000 - g_pool_a_used) - 0x100, clut};
  if (pool != bg().prim_pool_a) m.budget = 0x7FFFFFFF;
  Gte& g = m.g;
  const u32 parts = rdu32(model);
  const u8* r = model + 4;
  try {
    for (u32 part = 0; part < parts; ++part) {
      psyq::SetLightMatrix(&v.light_matrix);
      psyq::SetColorMatrix(&v.colour_matrix);
      gte_set_back_colour();
      r += 4;  // the part's header word
      for (int sec = 0; sec < 20; ++sec) {
        if (sec == 12) {
          // The unlit sections: the up light and the stage colour as the colour matrix.
          psyq::SetLightMatrix(&v.unlit_light);
          psyq::SetColorMatrix(&v.unlit_colour);
          gte_set_back_colour();
        }
        if (sec == 4 || sec == 10) g.write_data(Gte::RGBC, m.colour_808080);
        const u32 count = rdu32(r);
        r += 4;
        const int size = kSectionWords[sec] * 4;
        for (u32 i = 0; i < count; ++i, r += size) {
          switch (sec) {
            case 0: m.f3(r); break;
            case 1: m.g3(r); break;
            case 2: m.f3c(r); break;
            case 3: m.g3n(r); break;
            case 4: m.ft3(r); break;
            case 5: m.gt3(r); break;
            case 6: m.f4(r); break;
            case 7: m.g4(r); break;
            case 8: break;  // never drawn
            case 9: m.g4n(r); break;
            case 10: m.ft4(r); break;
            case 11: m.gt4(r); break;
            case 12: m.uf3(r); break;
            case 13: m.ug3(r); break;
            case 14: m.uft3(r); break;
            case 15: m.ugt3(r); break;
            case 16: m.uf4(r); break;
            case 17: m.ug4(r); break;
            case 18: m.uft4(r); break;
            case 19: m.ugt4(r); break;
          }
        }
      }
    }
  } catch (const ModelRender::Stop&) {
    // The primitive area is full: the original returns where it stopped.
  }
}

}  // namespace ss::game::battle
