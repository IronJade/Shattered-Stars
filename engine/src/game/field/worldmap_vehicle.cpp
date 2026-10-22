// World-map vehicles: the model packs (field 0x8004a920 / 0x800519d4), the field's model
// renderer they are drawn with (0x8006053c), the vehicle branch of the world actor's draw
// (overlay 0x80082f44) and the steering (overlay 0x80085944).
//
// Vehicle ids (actor +0x85): 1 = the flying vehicle of actor 0xd (pack 0x10e3, script
// flag 11; boarded from field_update), 3 = pack 0x10e2 (script flag 15), 4 = actor 0xe
// (pack 0x10e4, script flag 16, parked with its own angle).
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "disc/slz.h"
#include "game/common/texture.h"
#include "game/field/fade.h"
#include "game/field/field.h"
#include "game/field/field_input.h"
#include "game/field/load_queue.h"
#include "game/field/map.h"
#include "game/field/screen.h"
#include "game/field/worldmap.h"
#include "game/field/worldmap_internal.h"
#include "runtime/libgpu.h"
#include "runtime/runtime.h"

namespace ss::game::field {
using namespace wm;

namespace {

// 0x80076230..0x80076256: up to four vehicle models.
struct VehicleModels {
  std::vector<u8> data[4];  // 0x80076230: unpacked models (empty = free slot)
  u8 vehicle[4]{};          // 0x80076240: the vehicle id each belongs to
  u16 tpage[4]{};           // 0x80076244: texture page of its image
  u16 clut[4]{};            // 0x8007624c: CLUT id of its palette
  s16 clut_row = 0x1f8;     // 0x80076254: VRAM row of the next palette
  s16 tex_x = 0x280;        // 0x80076256: VRAM x of the next image (y 0x100)
};
VehicleModels g_models;

WorldActor* W(Actor* a) { return static_cast<WorldActor*>(a); }
FieldGlobals& fg() { return globals(); }
WorldView& view() { return world().view; }

std::vector<u8> slz_unpack_at(const u8* data, size_t avail) {
  auto out = disc::slz::unpack_block(std::span<const u8>(data, avail), 0, 0, nullptr);
  return out ? std::move(*out) : std::vector<u8>{};
}

// ---- The model renderer (field 0x8006053c) ----

psyq::P_TAG* alloc_prim(int words) {
  auto* t = static_cast<psyq::P_TAG*>(field_screen()->alloc(sizeof(psyq::P_TAG) + static_cast<size_t>(words) * 4));
  if (t != nullptr) t->len = static_cast<u32>(words);
  return t;
}
inline u8* at(psyq::P_TAG* t, int o) { return reinterpret_cast<u8*>(psyq::prim_words(t)) + (o - 4); }
inline void put32(psyq::P_TAG* t, int o, u32 v) { std::memcpy(at(t, o), &v, 4); }
inline void put16(psyq::P_TAG* t, int o, u16 v) { std::memcpy(at(t, o), &v, 2); }
inline void put8(psyq::P_TAG* t, int o, u8 v) { *at(t, o) = v; }

// Scratchpad state of 0x8006053c (0x1f80027c..0x1f800296).
struct ModelCtx {
  int ot_off = 0;         // 0x1f80027c: added to OT 0x820 - OTZ / 2
  bool depth_cue = false; // 0x1f800280: NCDS / NCDT instead of NCCS / NCCT (always 0)
  u32 tpage_hi = 0;       // 0x1f800284: texture page << 16, added to the uv1 word
  s32 nclip_min = 0;      // 0x1f80028c: -(s16 0x80075b42), the pad after the camera block's
                          // 0x80075b40 that nothing writes: front faces and edge-on ones
  u16 clut = 0;           // 0x1f800294: added to each record's CLUT
};

void load3(const u8* a, const u8* b, const u8* c) {
  ldv_raw(0, a);
  ldv_raw(1, b);
  ldv_raw(2, c);
}

// NCCS / NCDS of one colour with the normal in V0; the result with its code byte.
u32 light1(const ModelCtx& m, u32 rgb) {
  ld_rgbc(rgb);
  run(m.depth_cue ? G::kNcds : G::kNccs);
  return rgb2();
}

void link_at(const ModelCtx& m, int z, psyq::P_TAG* p) {
  psyq::addPrim(field_screen()->ot_entry(0x820 + m.ot_off - z), p);
}

// Section 1 (0x34-byte records): a G3 lit from three normals in one colour.
//   +0 rgb, +4 / +0xc / +0x14 normals, +0x1c / +0x24 / +0x2c vertices (the pad of the
//   first vertex, bit 0: semi-transparent, drawn after a DR_MODE with tpage 0x80075b33).
const u8* draw_lit_g3(const ModelCtx& m, const u8* p) {
  const u32 n = read_u32(p);
  p += 4;
  for (u32 i = 0; i < n; ++i, p += 0x34) {
    load3(p + 0x1c, p + 0x24, p + 0x2c);
    run(G::kRtpt);
    run(G::kNclip);
    if (mac(0) < m.nclip_min) continue;
    const u32 xy[3] = {sxy(0), sxy(1), sxy(2)};
    if (flag() < 0) continue;
    run(G::kAvsz3);
    const s32 z = otz() >> 1;
    load3(p + 4, p + 0xc, p + 0x14);
    ld_rgbc(read_u32(p));
    run(m.depth_cue ? G::kNcdt : G::kNcct);
    if (z + m.ot_off >= 0x800) continue;
    psyq::P_TAG* prim = alloc_prim(6);
    if (prim == nullptr) return p;
    const bool semi = (read_u32(p + 0x20) & 0x10000) != 0;
    for (int k = 0; k < 3; ++k) {
      put32(prim, 4 + k * 8, gte().read_data(G::RGB0 + k));
      put32(prim, 8 + k * 8, xy[k]);
    }
    put8(prim, 7, semi ? 0x32 : 0x30);
    link_at(m, z, prim);
    if (semi) {
      psyq::P_TAG* dm = alloc_prim(2);
      if (dm == nullptr) return p;
      psyq::DR_MODE mode{};
      psyq::SetDrawMode(&mode, 0, 1, view().ambient_pad, nullptr);
      put32(dm, 4, mode.code[0]);
      put32(dm, 8, mode.code[1]);
      link_at(m, z, dm);
    }
  }
  return p;
}

// GT3 shared by sections 4 and 6: any vertex x in 0..0x13f and any y in -0x40..0x117.
bool on_screen(const u32 xy[3]) {
  bool xs = false, ys = false;
  for (int k = 0; k < 3; ++k) {
    if ((xy[k] & 0xffff) < 0x140) xs = true;
    if ((((xy[k] >> 16) + 0x40) & 0xffff) < 0x158) ys = true;
  }
  return xs && ys;
}

// Section 4 (0x3c-byte records): a GT3 in grey (0x808080) lit from three normals.
//   +0 uv0, +2 clut, +4 uv1 | tpage << 16, +8 uv2, +0xc / +0x14 / +0x1c normals,
//   +0x24 / +0x2c / +0x34 vertices (first vertex's pad bit 0: semi-transparent).
const u8* draw_lit_gt3(const ModelCtx& m, const u8* p) {
  const u32 n = read_u32(p);
  p += 4;
  for (u32 i = 0; i < n; ++i, p += 0x3c) {
    load3(p + 0x24, p + 0x2c, p + 0x34);
    run(G::kRtpt);
    run(G::kNclip);
    if (mac(0) < m.nclip_min) continue;
    const u32 xy[3] = {sxy(0), sxy(1), sxy(2)};
    if (flag() < 0 || !on_screen(xy)) continue;
    run(G::kAvsz3);
    const s32 z = otz() >> 1;
    ld_rgbc(0x808080);
    load3(p + 0xc, p + 0x14, p + 0x1c);
    run(m.depth_cue ? G::kNcdt : G::kNcct);
    if (z + m.ot_off >= 0x800) continue;
    psyq::P_TAG* prim = alloc_prim(9);
    if (prim == nullptr) return p;
    for (int k = 0; k < 3; ++k) {
      put32(prim, 4 + k * 0xc, gte().read_data(G::RGB0 + k));
      put32(prim, 8 + k * 0xc, xy[k]);
    }
    put16(prim, 0xc, read_u16(p));
    put16(prim, 0xe, static_cast<u16>(m.clut + read_u16(p + 2)));
    put32(prim, 0x18, read_u32(p + 4) + m.tpage_hi);
    put16(prim, 0x24, read_u16(p + 8));
    put8(prim, 7, (read_u32(p + 0x28) & 0x10000) != 0 ? 0x36 : 0x34);
    link_at(m, z, prim);
  }
  return p;
}

// Section 5 (0x24-byte records): a G3 with three colours lit by the terrain light from
// straight above.  +0 / +4 / +8 colours (their code bytes become the primitive's),
// +0xc / +0x14 / +0x1c vertices.
const u8* draw_world_g3(const ModelCtx& m, const u8* p) {
  const u32 n = read_u32(p);
  p += 4;
  for (u32 i = 0; i < n; ++i, p += 0x24) {
    load3(p + 0xc, p + 0x14, p + 0x1c);
    run(G::kRtpt);
    run(G::kNclip);
    if (mac(0) < m.nclip_min) continue;
    const u32 xy[3] = {sxy(0), sxy(1), sxy(2)};
    if (flag() < 0) continue;
    run(G::kAvsz3);
    const s32 z = otz() >> 1;
    ldv(0, 0, 0x1000, 0);
    u32 c[3];
    for (int k = 0; k < 3; ++k) c[k] = light1(m, read_u32(p + k * 4));
    if (z + m.ot_off >= 0x800) continue;
    psyq::P_TAG* prim = alloc_prim(6);
    if (prim == nullptr) return p;
    for (int k = 0; k < 3; ++k) {
      put32(prim, 4 + k * 8, c[k]);
      put32(prim, 8 + k * 8, xy[k]);
    }
    link_at(m, z, prim);
  }
  return p;
}

// Section 6 (0x30-byte records): a GT3 with three colours lit by the terrain light from
// straight above.  +0 uv0, +2 clut, +4 uv1 | tpage << 16, +8 uv2, +0xc / +0x10 / +0x14
// colours, +0x18 / +0x20 / +0x28 vertices (first vertex's pad bit 0: semi-transparent).
const u8* draw_world_gt3(const ModelCtx& m, const u8* p) {
  const u32 n = read_u32(p);
  p += 4;
  for (u32 i = 0; i < n; ++i, p += 0x30) {
    load3(p + 0x18, p + 0x20, p + 0x28);
    run(G::kRtpt);
    run(G::kNclip);
    if (mac(0) < m.nclip_min) continue;
    const s32 fl = flag();
    const u32 xy[3] = {sxy(0), sxy(1), sxy(2)};
    if (fl < 0 || !on_screen(xy)) continue;
    run(G::kAvsz3);
    const s32 z = otz() >> 1;
    ldv(0, 0, 0x1000, 0);
    const u32 c0 = light1(m, read_u32(p + 0xc));
    if (z + m.ot_off >= 0x800) continue;
    const u32 c1 = light1(m, read_u32(p + 0x10));
    const u32 c2 = light1(m, read_u32(p + 0x14));
    psyq::P_TAG* prim = alloc_prim(9);
    if (prim == nullptr) return p;
    const u32 c[3] = {c0, c1, c2};
    for (int k = 0; k < 3; ++k) {
      put32(prim, 4 + k * 0xc, c[k]);
      put32(prim, 8 + k * 0xc, xy[k]);
    }
    put16(prim, 0xc, read_u16(p));
    put16(prim, 0xe, static_cast<u16>(m.clut + read_u16(p + 2)));
    put32(prim, 0x18, read_u32(p + 4) + m.tpage_hi);
    put16(prim, 0x24, read_u16(p + 8));
    put8(prim, 7, (read_u32(p + 0x1c) & 0x10000) != 0 ? 0x36 : 0x34);
    link_at(m, z, prim);
  }
  return p;
}

const u8* skip(const u8* p, u32 size) { return p + 4 + read_u32(p) * size; }

// The section types no vehicle model has (2: G3 three colours / one normal, 3: three
// colours / three normals) are skipped with a note.
const u8* skip_unported(const u8* p, u32 size, const char* what) {
  if (read_u32(p) != 0) runtime::Runtime::get().log_once(std::string("model:") + what, std::string("field model draw (0x8006053c): section ") + what + " not ported");
  return skip(p, size);
}

// 0x8006053c: draws a model with the current GTE matrices (camera_set_object_matrix).
// u32 object count; per object a word and twenty sections {u32 count; records}, in
// order: 0x24 -, 0x34 lit G3, 0x2c G3*, 0x3c G3*, 0x2c -, 0x3c lit GT3, 0x2c -, 0x44 -,
// 0x30 -, 0x50 -, 0x34 -, 0x4c -, then with the terrain light: 0x1c -, 0x24 G3,
// 0x28 -, 0x30 GT3, 0x24 -, 0x30 -, 0x30 -, 0x3c - ("-": skipped, *: not ported). The
// first group is lit by the camera block's colour matrix 0x80075b64 (cleared by every
// camera_update_matrices) and BK = ambient x BG tint, so only the ambient shows.
void model_draw(const u8* model, u16 clut, u16 tpage, int ot_off) {
  WorldView& v = view();
  ModelCtx m;
  m.ot_off = ot_off;
  m.tpage_hi = static_cast<u32>(tpage) << 16;
  m.clut = clut;
  const u32 objects = read_u32(model);
  const u8* p = model + 4;
  for (u32 o = 0; o < objects; ++o) {
    psyq::SetLightMatrix(&v.light_b84);
    psyq::SetColorMatrix(&v.colour_b64);
    {
      // field 0x800617d8 (gte_set_back_colour): ambient x BG tint / 128.
      s32 c[3];
      for (int i = 0; i < 3; ++i) {
        c[i] = static_cast<s32>(static_cast<u32>(v.ambient[i]) * static_cast<u32>(static_cast<s32>(g_fade_targets.bg_tint[i]))) >> 7;
        if (c[i] > 0xff) c[i] = 0xff;
      }
      psyq::SetBackColor(c[0], c[1], c[2]);
    }
    p += 4;
    p = skip(p, 0x24);
    p = draw_lit_g3(m, p);
    p = skip_unported(p, 0x2c, "G3 (0x2c)");
    p = skip_unported(p, 0x3c, "G3 (0x3c)");
    p = skip(p, 0x2c);
    p = draw_lit_gt3(m, p);
    for (u32 size : {0x2cu, 0x44u, 0x30u, 0x50u, 0x34u, 0x4cu}) p = skip(p, size);
    psyq::SetLightMatrix(&v.light_m);
    psyq::SetColorMatrix(&v.colour);
    psyq::SetBackColor(0, 0, 0);
    p = skip(p, 0x1c);
    p = draw_world_g3(m, p);
    p = skip(p, 0x28);
    p = draw_world_gt3(m, p);
    for (u32 size : {0x24u, 0x30u, 0x30u, 0x3cu}) p = skip(p, size);
  }
}

s32 table32(u32 address, int index) {
  const u8* t = field_data(address + static_cast<u32>(index) * 4);
  return t != nullptr ? static_cast<s32>(read_u32(t)) : 0;
}

}  // namespace

// ---- Model packs ----

void world_vehicle_models_free() {  // 0x800519d4
  for (auto& d : g_models.data) {
    d.clear();
    d.shrink_to_fit();
  }
}

void world_vehicle_models_reset() {  // field_load_map: 0x800519d4 and the VRAM cursors
  world_vehicle_models_free();
  g_models.tex_x = 0x280;
  g_models.clut_row = 0x1f8;
}

// 0x8004a920: a vehicle pack {u32 tag, u32 offset}... ending with tag 0: 5 = the SLZ model,
// 1 = its SLZ image {u16 flags (depth & 3), u16 palette size, palette, u16 w, h, pixels}
// (palette row 0x80076254 at x 0, pixels at (0x80076256, 0x100); both cursors move on),
// 9 = a sprite pack registered with id 30000.
void world_vehicle_load_pack(Field* f, int entry, int vehicle) {
  field_load_entry(f, entry);
  const u8* pack = field_entry_data(entry);
  if (pack == nullptr) return;
  const size_t size = static_cast<size_t>(fg().loader->entry_size(entry));
  int slot = 0;
  for (; slot < 4; ++slot)
    if (g_models.data[slot].empty()) break;
  if (slot == 4) return;
  g_models.vehicle[slot] = static_cast<u8>(vehicle);
  psx::Gpu& gpu = runtime::Runtime::get().gpu;
  for (const u8* t = pack; read_u32(t) != 0; t += 8) {
    const u32 tag = read_u32(t), off = read_u32(t + 4);
    if (off >= size) break;
    if (tag == 5) {
      g_models.data[slot] = slz_unpack_at(pack + off, size - off);
    } else if (tag == 1) {
      std::vector<u8> img = slz_unpack_at(pack + off, size - off);
      img.resize(std::max<size_t>(img.size(), 4 + 0x200 + 4));
      const u16 flags = read_u16(img.data()), colours = read_u16(img.data() + 2);
      const u16* clut = reinterpret_cast<const u16*>(img.data() + 4);
      std::vector<u16> row(256, 0);  // upload_clut256 reads 256 entries, as the console does
      const size_t avail = (img.size() - 4) / 2;
      std::memcpy(row.data(), clut, std::min<size_t>(256, avail) * 2);
      upload_clut256(gpu, row.data(), 0, g_models.clut_row);
      g_models.clut[slot] = static_cast<u16>(g_models.clut_row << 6);
      const u8* hdr = img.data() + 4 + colours * 2;
      const int depth = flags & 3;
      const int w = read_u16(hdr), h = read_u16(hdr + 2);
      std::vector<u16> pixels(static_cast<size_t>(w) * h, 0);
      const size_t have = std::min<size_t>(pixels.size() * 2, img.size() - static_cast<size_t>(hdr + 4 - img.data()));
      std::memcpy(pixels.data(), hdr + 4, have);
      g_models.tpage[slot] = upload_texture(gpu, pixels.data(), depth, 0, g_models.tex_x, 0x100, w << (2 - depth), h);
      ++g_models.clut_row;
      g_models.tex_x = static_cast<s16>(g_models.tex_x + 0x40);
    } else if (tag == 9) {
      sprite_pack_register(pack + off, 30000, 0);
    }
  }
}

// ---- Drawing ----

// The vehicle branch of 0x80082f44 after worldactor_project and the auto animation:
// the model at the bent position (field tables 0x80075190: height offset, 0x800751a4:
// yaw offset per vehicle id; pitched by the climb speed), then for vehicle 1 its
// shadow, a subtractive FT4 of the UI atlas' shadow icon on the ground below.
void worldactor_draw_vehicle(Actor* a) {
  WorldActor* w = W(a);
  WorldBg* bg = world_bg();
  WorldView& v = view();
  if (bg == nullptr) return;
  if (std::abs((a->x >> 12) - bg->centre_x) > 0x6000) return;
  if (std::abs((a->z >> 12) - bg->centre_z) > 0x6000) return;
  const int id = static_cast<s8>(a->vehicle);
  s16 org[2];
  world_block_origin(bg, org);
  s16 p[3] = {static_cast<s16>((a->x - org[0] * 0x1000) >> 12),
              static_cast<s16>(static_cast<s16>(a->y >> 12) + static_cast<s16>(table32(0x80075190, id))),
              static_cast<s16>((a->z - org[1] * 0x1000) >> 12)};
  const s32 zero[3] = {0, 0, 0}, one[3] = {0x1000, 0x1000, 0x1000};
  world_set_object_matrix(zero, v.light_angle, one);
  s16 out[3] = {0, 0, 0};
  world_bend_to_world(p, out);
  {
    const s32 pos[3] = {out[0] << 12, out[1] << 12, out[2] << 12};
    const s32 rot[3] = {v.light_angle[0], table32(0x800751a4, id) - a->angle, (w->climb * -100) / 0x28000};
    world_flat_object_matrix(pos, rot, one);
  }
  for (int i = 0; i < 4; ++i) {
    if (g_models.data[i].empty() || static_cast<s8>(g_models.vehicle[i]) != id) continue;
    model_draw(g_models.data[i].data(), g_models.clut[i], g_models.tpage[i], 10);
    break;
  }
  if (id != 1) return;
  // The shadow. Its scale is ((ground << 12) >> 12) - ground + 0x1000, i.e. always 0x1000;
  // the vehicle's y is the ground's while the shadow is drawn and stays so when the
  // shadow is culled (the original restores it only after linking the quad).
  const u32 ot = static_cast<u32>((w->ot_bias - w->depth8) + 0x84a);
  if (ot > 0x820) return;
  s32 k = ((w->ground << 12) >> 12) - w->ground + 0x1000;
  if (k < 0) k = 0;
  const s32 saved_y = a->y;
  a->y = w->ground << 12;
  world_set_object_matrix(zero, v.light_angle, one);
  p[1] = static_cast<s16>(w->ground);
  s16 g[3] = {0, 0, 0};
  world_bend_to_world(p, g);
  {
    const s32 pos[3] = {g[0] << 12, g[1] << 12, g[2] << 12};
    const s32 rot[3] = {0, table32(0x800751a4, id) - a->angle, 0};
    world_flat_object_matrix(pos, rot, one);
  }
  s16 pts[4][3];
  s32 ang = 0xa00;
  for (int i = 0; i < 4; ++i, ang += 0x400) {
    pts[i][0] = static_cast<s16>(((rcos(ang) * 0x140) >> 12) * k >> 12);
    pts[i][1] = 0;
    pts[i][2] = static_cast<s16>(((rsin(ang) * 0x140) >> 12) * k >> 12);
  }
  ldv(0, pts[0][0], pts[0][1], pts[0][2]);
  run(G::kRtps);
  const u32 xy0 = sxy(2);
  const s32 flag0 = flag();
  ldv(0, pts[1][0], pts[1][1], pts[1][2]);
  ldv(1, pts[2][0], pts[2][1], pts[2][2]);
  ldv(2, pts[3][0], pts[3][1], pts[3][2]);
  run(G::kRtpt);
  run(G::kNclip);
  if (mac(0) > 0) return;
  const u32 xy1 = sxy(0), xy2 = sxy(1), xy3 = sxy(2);
  if (flag() < 0 && flag0 < 0) return;
  const u8* uv = fg().ui_atlas != nullptr ? fg().ui_atlas + 0x1c2 : nullptr;
  if (uv == nullptr) return;
  psyq::P_TAG* prim = alloc_prim(9);
  if (prim == nullptr) return;
  const u8 u0 = uv[0], v0 = uv[1], u1 = uv[2], v1 = uv[3];
  put32(prim, 4, 0x2f000000);
  put32(prim, 8, xy0);
  put8(prim, 0xc, u0);
  put8(prim, 0xd, v0);
  put16(prim, 0xe, 0x7fcd);
  put32(prim, 0x10, xy1);
  put8(prim, 0x14, static_cast<u8>(u0 + (u1 - u0)));
  put8(prim, 0x15, v0);
  put16(prim, 0x16, 0x56);
  put32(prim, 0x18, xy3);
  put8(prim, 0x1c, u0);
  put8(prim, 0x1d, static_cast<u8>(v0 + (v1 - v0)));
  put32(prim, 0x20, xy2);
  put8(prim, 0x24, static_cast<u8>(u0 + (u1 - u0)));
  put8(prim, 0x25, static_cast<u8>(v0 + (v1 - v0)));
  psyq::addPrim(field_screen()->ot_entry(static_cast<int>(ot)), prim);
  a->y = saved_y;
}

// ---- Steering (overlay 0x80085944) ----

// The value the original's throttle cap takes when the hover button (config +6) is held
// with no direction: the register s5 is not set on that path and still holds the
// caller's value. Measured in PCSX-Redux (see kHoverCapNote in the catalog).
constexpr s32 kHoverIdleCap = 0;

void worldactor_vehicle_control(Actor* a, int camera, s32 mag) {
  FieldGlobals& g = fg();
  WorldActor* w = W(a);
  WorldView& v = view();
  const s32 saved[4] = {a->x, a->y, a->z, a->w};
  const s16 saved_angle = a->angle;
  const u16 held = g.held[0];
  const u16 hover_btn = read_u16(g.config + 6);
  bool steer = false;
  s32 dir = 0;
  if (a->analog == 0) {
    const int d = field_pad_direction();
    if (d != -1) {
      dir = d << 9;
      steer = true;
    }
  } else if (mag != 0) {
    dir = world_ratan2(field_analog().y, field_analog().x) + 0x400;
    steer = true;
  }
  while (dir > 0x800) dir -= 0x1000;
  while (dir < -0x800) dir += 0x1000;
  // The turn rate settles by 0x600 a frame.
  if (w->turn < 1) {
    w->turn += 0x600;
    if (w->turn > 0) w->turn = 0;
  } else {
    w->turn -= 0x600;
    if (w->turn < 0) w->turn = 0;
  }
  s32 steer_amt = dir;
  if (dir < 1) {
    if (dir < -0x3ff) steer_amt = -0x800 - dir;
  } else if (dir > 0x3ff) {
    steer_amt = 0x800 - dir;
  }
  steer_amt /= 2;
  if (held & 4) steer_amt -= 0x200;
  if (held & 8) steer_amt += 0x200;
  const s32 lim = std::abs(steer_amt * 0x40);
  if ((held & hover_btn) == 0) {
    if (std::abs(w->turn) < lim) {
      w->turn -= steer_amt * 5;
      if (w->turn < 1) {
        if (w->turn < -lim) w->turn = -lim;
      } else if (w->turn > lim) {
        w->turn = lim;
      }
    }
  } else if (held & 0xf000) {
    mag = 0x50;
  }
  // The climb settles by 0x1000; forward dives, back-right climbs.
  if (w->climb < 1) {
    w->climb += 0x1000;
    if (w->climb > 0) w->climb = 0;
  } else {
    w->climb -= 0x1000;
    if (w->climb < 0) w->climb = 0;
  }
  if ((held & hover_btn) == 0 && steer) {
    if (dir < 0x481) {
      if (static_cast<u32>(dir + 0x3ff) <= 0x7fe) {
        w->climb += 0x5000;
        if (w->climb > 0x28000) w->climb = 0x28000;
      }
    } else {
      w->climb -= 0x5000;
      if (w->climb < -0x28000) w->climb = -0x28000;
    }
  }
  a->y += w->climb;
  // Throttle: the action button (config +0), or the hover button with a direction.
  bool moving = false;
  s32 cap;
  if ((held & hover_btn) == 0) {
    cap = 0x1000;
    if (held & read_u16(g.config)) {
      moving = true;
      w->throttle += 0x200;
    }
  } else {
    w->throttle += 0x200;
    if (mag != 0 || (held & 0xf000) != 0) moving = true;
    cap = moving ? (mag << 12) / 0xaa : kHoverIdleCap;
  }
  if (cap < w->throttle) w->throttle = cap;
  w->throttle -= 200;
  if (w->throttle < 0) w->throttle = 0;
  if (w->throttle > 0x1000) w->throttle = 0x1000;
  {
    s32 t = a->walk_speed * w->throttle;
    if (t < 0) t += 0xfff;
    a->speed = (t >> 12) << 5;
  }
  const s32 yaw = world_camera_yaw();
  const s32 heading = (held & hover_btn) == 0 ? a->angle : yaw - dir;
  auto step = [&](s32 trig) {
    s32 d = trig * a->speed;
    if (d < 0) d += 0x3f;
    d = (d >> 6) * g.frame_ticks;
    if (d < 0) d += 0xfff;
    return (d >> 12) * 0x40;
  };
  a->x += step(rcos(heading));
  a->z += step(rsin(heading));
  if (a->x < 0) {
    a->x += 0x1b000000;
  } else if (a->x > 0x1affffff) {
    a->x -= 0x1b000000;
  }
  if (a->z < 0) {
    a->z += 0x15000000;
  } else if (a->z > 0x14ffffff) {
    a->z -= 0x15000000;
  }
  // The right shoulder must not be over an unwalkable (non-type 0xf) polygon or off map.
  TerrainHit hit;
  s32 h = world_terrain_query(world_bg(), a->x + rcos(a->angle + 0x200) * 0x3c, a->z + rsin(a->angle + 0x200) * 0x3c,
                              &hit);
  if ((hit.attr >> 4) != 0xf) hit.attr |= 1;
  if ((h != -0x8000 && (hit.attr & 1) == 0) || hit.off_map != 0) {
    a->x = saved[0];
    a->y = saved[1];
    a->z = saved[2];
    a->w = saved[3];
    a->angle = saved_angle;
    moving = false;
  }
  if (h == -0x8000 && (h = w->ground) == -0x8000) h = 0;
  if (a->y < -0x8fc000) {
    a->y = -0x8fc000;
    w->climb = 0;
  }
  w->ground = h;
  if (((h - 100) << 12) < a->y) {
    a->y = (h - 100) << 12;
    w->climb = 0;
  }
  // Banking (camera roll): right turns (0x201..0x480) or R1 roll one way, L1 the other
  // (the left-turn range tested is 0xb81..0xe00, which a normalised direction never is).
  bool rolled = false;
  if ((held & hover_btn) == 0 && moving) {
    if (static_cast<u32>(dir - 0x201) < 0x280 || (held & 8)) {
      v.roll = static_cast<s16>(v.roll - 2);
      rolled = true;
    } else if (static_cast<u32>(dir - 0xb81) < 0x280 || (held & 4)) {
      v.roll = static_cast<s16>(v.roll + 2);
      rolled = true;
    }
    const s32 rl = (held & 0xc) != 0 ? 0x50 : 0x48;
    if (v.roll > rl) v.roll = static_cast<s16>(rl);
    if (v.roll < -rl) v.roll = static_cast<s16>(-rl);
  }
  if (!rolled) {
    s16& roll = v.roll;
    s16 next = 0;
    if (roll != 0) next = roll < 1 ? static_cast<s16>(roll + 8) : static_cast<s16>(roll - 8);
    const bool crossed = roll != 0 && next != 0 && ((roll > 0) != (next > 0));
    roll = crossed || next == 0 ? 0 : next;
  }
  world().leader_hit = hit;
  if (camera == 0) return;
  if (dir == 0 && (held & 0xc) == 0) {
    // No steering: the camera swings round behind the heading.
    s32 d = a->angle - yaw;
    if (d >= 0x800) {
      d -= 0x1000;
    } else if (d < -0x800) {
      d += 0x1000;
    }
    const s32 rate = std::abs(w->turn);
    d = (d << 12) / g.frame_ticks;
    if (rate < 0x2000) {
      if (std::abs(d) > 0x2000) {
        w->turn = d < 1 ? -0x2000 : 0x2000;
      } else {
        w->turn = d;
      }
    } else {
      s32 sign = d == 0 ? 0 : (d > 0 ? 1 : -1);
      if (w->turn != 0) sign = w->turn < 1 ? ~sign : sign ^ 1;
      if (sign != 0) w->turn = d;
    }
  }
  world_camera_follow(world_bg(), a, w->turn, w->climb, 1);
  const s32 bank_lim = ((lim >> 6) * 5) / 6;
  if (steer_amt == 0 || (held & hover_btn) != 0) {
    s32 d = yaw - a->angle;
    if (d >= 0x800) {
      d -= 0x1000;
    } else if (d < -0x800) {
      d += 0x1000;
    }
    w->bank = -d;
  } else {
    const s32 yaw2 = world_camera_yaw();
    if (static_cast<u32>(steer_amt - 0x15) < 0x7d7) {
      if (-bank_lim <= w->bank) {
        w->bank -= 0x20;
        if (w->bank < -bank_lim) w->bank = -bank_lim;
      }
    } else if (static_cast<u32>(steer_amt + 0x7eb) < 0x7d7 && w->bank <= bank_lim) {
      w->bank += 0x20;
      if (w->bank > bank_lim) w->bank = bank_lim;
    }
    a->angle = static_cast<s16>(yaw2 + w->bank);
  }
}

}  // namespace ss::game::field
