// Field visual effects (game/field/effects*, scene_renderer): the edge glow, the
// particle pool and its private OT layers, the behaviours' primitives and the splice of
// the layers into the screen OT. The arithmetic follows overlay #3101 (0x8007e000-
// 0x8007e10c, 0x80089b68-0x8008d928) through the port's bit-exact GTE.
//
// With SS_FX_REF=DIR (PCSX-Redux RAM dumps taken while effects are on screen, by the
// scratchpad tool fx_ref.lua: ring588.bin = map 588's ring, script B 0x7d;
// streak574.bin = map 574's rising streaks, B 0x7b; ringtex61.bin = map 61's textured
// ring, A 0x13; beam575.bin / burst575.bin = map 575's beam (B 0x0b) and burst (A 0x12)
// with G[0xa0] = 0x21; glow548.bin = map 548's edge glow, ext 0x79), the particles and the
// camera block are read from each dump, every particle is stepped back one update and
// run again, and the primitives it builds are compared with the ones in the dump's OT
// layers (glow: the screen OT's entry 0x110).

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "game/field/camera.h"
#include "game/field/effects.h"
#include "game/field/field.h"
#include "game/field/map.h"
#include "game/field/scene_renderer.h"
#include "game/field/screen.h"
#include "game/field/tasks.h"
#include "psx/gte.h"
#include "runtime/libgpu.h"
#include "runtime/libgte.h"
#include "test.h"

using namespace ss;
using namespace ss::game::field;

namespace {

// A screen with its arena, current buffer flipped and OT cleared (field_screen()).
Screen* test_screen() {
  static Screen screen;
  static bool made = false;
  if (!made) {
    screen_construct(&screen);
    screen_alloc_arena(&screen);
    made = true;
  }
  screen_construct(&screen);
  screen_flip(&screen);
  screen_clear_ot(&screen);
  return &screen;
}

u32* words(psyq::P_TAG* p) { return psyq::prim_words(p); }
u8 code_of(psyq::P_TAG* p) { return static_cast<u8>(words(p)[0] >> 24); }
s16 px(psyq::P_TAG* p, int word) { return static_cast<s16>(words(p)[word]); }
s16 py(psyq::P_TAG* p, int word) { return static_cast<s16>(words(p)[word] >> 16); }

// The primitives linked after OT entry 'e' up to the next OT entry of the same table.
std::vector<psyq::P_TAG*> chain(psyq::P_TAG* e, const psyq::P_TAG* table, int entries) {
  std::vector<psyq::P_TAG*> out;
  for (psyq::P_TAG* p = e->next; p != nullptr; p = p->next) {
    if (p >= table && p < table + entries) break;
    out.push_back(p);
  }
  return out;
}

// A camera looking down -z from z = +1000 (map units), projection H 256, offset (160,
// 120): x right, y down on screen.
void test_camera() {
  CameraBlock& cam = camera_block();
  cam.eye[0] = cam.eye[1] = 0;
  cam.eye[2] = 1000 << 12;
  cam.rot = psyq::MATRIX{};
  cam.rot.m[0][0] = 0x1000;
  cam.rot.m[1][1] = 0x1000;
  cam.rot.m[2][2] = -0x1000;
  psyq::SetGeomScreen(256);
  psyq::SetGeomOffset(160, 120);
  fx_state().origin[0] = fx_state().origin[1] = 0;
  globals().frame_ticks = 1;
}

}  // namespace

TEST(fx_scale_colour) {
  CHECK_EQ(fx_scale_colour(0x808080, 0x80), 0x404040u);
  CHECK_EQ(fx_scale_colour(0x80A0FF, 0x100), 0x80A0FFu);
  CHECK_EQ(fx_scale_colour(0x80A0FF, 0), 0u);
  CHECK_EQ(fx_scale_colour(0x0000FF, 0x200), 0x0000FFu);  // saturates at 0xff
  CHECK_EQ(fx_scale_colour(0x123456, 0x40), 0x040D15u);
}

TEST(fx_pool_and_layers) {
  field_free_ot_layers();
  CHECK_EQ(fx_state().count, 0);
  Particle* a = fx_particle_alloc(3);
  CHECK(a != nullptr);
  CHECK_EQ(fx_state().count, kFxPoolSize);
  CHECK_EQ(a->type, 3);
  CHECK(a == &fx_state().pool[0]);
  Particle* b = fx_particle_alloc(1);
  CHECK(b == &fx_state().pool[1]);
  fx_particle_free(a);
  CHECK(fx_particle_alloc(2) == &fx_state().pool[0]);
  // Four layers, found again by id; a fifth id gets none.
  OtLayer* l1 = fx_layer_get(0x80);
  CHECK(l1 != nullptr && fx_layer_get(0x80) == l1);
  CHECK(fx_layer_get(0x10) != nullptr && fx_layer_get(0x20) != nullptr && fx_layer_get(0x30) != nullptr);
  CHECK(fx_layer_get(0x40) == nullptr);
  CHECK_EQ(l1->half, 0);
  CHECK(l1->ot[0][0].next == &l1->ot[0][1]);
  field_free_ot_layers();
  CHECK_EQ(fx_state().count, 0);
  CHECK(!fx_state().pool && !fx_state().layer[0]);
}

TEST(fx_glow) {
  Screen* screen = test_screen();
  globals().frame_ticks = 1;
  auto* glow = new GlowFx;
  glow_create(glow, 0xFF, 0x80, 0x40, -1);  // frames < 0 -> 1
  CHECK_EQ(glow->frames, 1);
  glow_draw(glow);  // timer 0: black
  glow_draw(glow);  // timer 1 = frames: full colour
  // The second draw's primitives are in front: DR_TPAGE, then polygons 23 .. 0.
  psyq::P_TAG* entry = screen->ot_entry(0x110);
  std::vector<psyq::P_TAG*> prims = chain(entry, screen->current->ot.data(), kOtEntries);
  CHECK_EQ(prims.size(), 50u);  // 2 x (24 polygons + DR_TPAGE)
  if (prims.size() == 50) {
    CHECK_EQ(words(prims[0])[0], 0xE1000220u);
    psyq::P_TAG* first = prims[24];  // polygon 0 of the second draw
    CHECK_EQ(code_of(first), 0x3A);
    CHECK_EQ(words(first)[0] & 0xFFFFFF, 0u);
    CHECK_EQ(words(first)[4], 0x4080FFu);
    CHECK_EQ(words(first)[6], 0x4080FFu);
    CHECK_EQ(px(first, 1), 0xA0 + 0x76);  // inner ellipse at angle 0: (278, 120)
    CHECK_EQ(py(first, 1), 0x78);
    CHECK_EQ(px(first, 5), 0xA0 + 0xEC);  // outer at angle 0: (396, 120)
    CHECK_EQ(words(prims[49])[4], 0u);    // first draw: still black
  }
  glow_destroy(glow, 3);
}

TEST(fx_beam_into_layer_and_splice) {
  Screen* screen = test_screen();
  field_free_ot_layers();
  test_camera();
  // Last average 0 (OTZ is stale in the line drawer).
  ss::psyq::gte().write_data(psx::Gte::OTZ, 0);
  const s32 from[4] = {0, 0, 0, 0}, to[4] = {100 << 12, 0, 0, 0};
  Particle* p = fx_spawn_beam(0x80, from, to, 0xFF0000, 0x0000FF, 2, 0);
  CHECK(p != nullptr);
  if (p == nullptr) return;
  CHECK_EQ(p->tgt[0], 100);
  auto renderer = std::make_unique<SceneRenderer>();
  scene_renderer_update(renderer.get());
  OtLayer* layer = fx_state().layer[0].get();
  CHECK(layer != nullptr && layer->dirty == 1 && layer->half == 1);
  if (layer == nullptr) return;
  psyq::P_TAG* table = layer->current();
  std::vector<psyq::P_TAG*> prims = chain(&table[0x820], table, kFxLayerEntries);
  CHECK_EQ(prims.size(), 2u);  // DR_MODE (abr 1) then the LINE_G2
  if (prims.size() == 2) {
    CHECK_EQ(code_of(prims[1]), 0x52);
    CHECK_EQ(words(prims[1])[0] & 0xFFFFFF, 0xFF0000u);
    CHECK_EQ(words(prims[1])[2], 0x0000FFu);
    CHECK_EQ(px(prims[1], 1), 160);  // pos at 1000 in front: screen centre
    CHECK_EQ(py(prims[1], 1), 120);
    CHECK_EQ(px(prims[1], 3), 160 + 100 * 256 / 1000);
  }
  // The splice: screen OT[0x80 + 0x11] -> layer -> the entry's old chain.
  psyq::P_TAG* entry = screen->ot_entry(0x91);
  psyq::P_TAG* old = entry->next;
  ot_layers_splice();
  CHECK(entry->next == &table[0]);
  CHECK(table[0x82E].next == old);
  CHECK(ot_layer_table_for(0x80) == table);
  CHECK(ot_layer_table_for(0x81) == nullptr);
  // Its life runs out after two updates.
  scene_renderer_update(renderer.get());
  CHECK_EQ(p->type, 0);
  field_free_ot_layers();
}

TEST(fx_streak_reaches_target) {
  // The streak's step reads the overlay's tables from the field image: needs the disc.
  if (std::getenv("SS_DISC1") == nullptr) {
    std::printf("  (skipped: SS_DISC1 not set)\n");
    return;
  }
  test_screen();
  field_free_ot_layers();
  test_camera();
  const s32 from[4] = {0, 0, 0, 0};
  s32 to[4] = {0, 0, -(200 << 12), 0};
  // Speed 10 map units per tick, no acceleration, tail 50.
  Particle* p = fx_spawn_streak(0x40, from, to, 0xFFFFFF, 0, 50, 10 << 12, 0, 1);
  CHECK(p != nullptr);
  if (p == nullptr) return;
  auto renderer = std::make_unique<SceneRenderer>();
  int frames = 0;
  while (p->type != 0 && frames < 100) {
    scene_renderer_update(renderer.get());
    ++frames;
  }
  CHECK_EQ(frames, 19);  // arrives once the distance left is within one step + the tail
  CHECK_EQ(p->pos[2], -(200 << 12));
  field_free_ot_layers();
}

TEST(fx_ring_textured_uv) {
  // The UV sets come from the overlay's table 0x8008ed58: needs the disc image.
  test_screen();
  field_free_ot_layers();
  test_camera();
  const s32 pos[4] = {0, 0, 0, 0};
  Particle* p = fx_spawn_ring_textured(0x30, pos, 8, 40, 0, 2, 50, 0, 0, 100);
  CHECK(p != nullptr);
  if (p == nullptr) return;
  CHECK_EQ(p->p38, 8);
  CHECK_EQ(p->p48, 100);
  CHECK_EQ(p->tgt[0], 40);
  auto renderer = std::make_unique<SceneRenderer>();
  scene_renderer_update(renderer.get());
  OtLayer* layer = fx_state().layer[0].get();
  int quads = 0;
  for (int i = 0; i < kFxLayerEntries; ++i)
    for (psyq::P_TAG* q : chain(&layer->current()[i], layer->current(), kFxLayerEntries))
      if (code_of(q) == 0x2E) ++quads;
  CHECK_EQ(quads, 8);
  CHECK_EQ(p->p48, 102);
  field_free_ot_layers();
}

namespace {

struct Dump {
  std::vector<u8> ram;
  u32 rd32(u32 a) const {
    u32 v;
    std::memcpy(&v, &ram[a & 0x1FFFFF], 4);
    return v;
  }
  u16 rd16(u32 a) const {
    u16 v;
    std::memcpy(&v, &ram[a & 0x1FFFFF], 2);
    return v;
  }
  s32 s(u32 a) const { return static_cast<s32>(rd32(a)); }
};

bool load_dump(const char* name, Dump* d) {
  const char* dir = std::getenv("SS_FX_REF");
  if (dir == nullptr || *dir == 0) {
    std::printf("  (skipped: SS_FX_REF not set)\n");
    return false;
  }
  const std::string path = std::string(dir) + "/" + name;
  std::ifstream in(path, std::ios::binary);
  d->ram.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  if (d->ram.size() < 0x200000) {
    std::printf("  (skipped: no %s)\n", path.c_str());
    return false;
  }
  return true;
}

// The camera block, view origin, frame ticks and the scene's projection from a dump.
void setup_from_dump(const Dump& d) {
  CameraBlock& cam = camera_block();
  for (int i = 0; i < 3; ++i) cam.eye[i] = d.s(0x80075A84 + 4 * i);
  for (int i = 0; i < 9; ++i) {
    cam.view.m[i / 3][i % 3] = static_cast<s16>(d.rd16(0x80075AC8 + 2 * i));
    cam.rot.m[i / 3][i % 3] = static_cast<s16>(d.rd16(0x80075B44 + 2 * i));
  }
  cam.zoom = d.s(0x80075AB4);
  for (int i = 0; i < 3; ++i) {
    cam.fx_scale[i] = d.s(0x800746FC + 4 * i);
    cam.fx_light_params[i] = d.s(0x800746EC + 4 * i);
  }
  globals().frame_ticks = d.s(0x8007531C);
  fx_state().origin[0] = static_cast<s16>(d.rd16(0x800761B0));
  fx_state().origin[1] = static_cast<s16>(d.rd16(0x800761B2));
  const u32 scene = d.rd32(0x80075330);
  map_data().scene.assign(0x100, 0);
  std::memcpy(map_data().scene.data(), &d.ram[scene & 0x1FFFFF], 0x100);
  globals().field_flags = nullptr;
  psyq::InitGeom();  // ZSF3 / ZSF4 as field_main_loop sets them
  psyq::SetGeomOffset(d.rd16(scene + 0x34) >> 1, d.rd16(scene + 0x36) >> 1);
  psyq::SetGeomScreen(d.rd16(scene + 0x6A));
}

// Primitives (GP0 words) by OT entry of a dumped table, walking it from entry 0 as the
// GPU would (it is compacted and spliced by then); stops at entry 'last'.
std::vector<std::vector<std::vector<u32>>> walk_ot(const Dump& d, u32 ot, int entries, int last) {
  std::vector<std::vector<std::vector<u32>>> out(static_cast<size_t>(entries));
  u32 addr = ot;
  int entry = 0;
  for (int guard = 0; guard < 200000; ++guard) {
    if (addr >= ot && addr < ot + 4 * static_cast<u32>(entries) && ((addr - ot) & 3) == 0) {
      entry = static_cast<int>((addr - ot) / 4);
      if (entry >= last) break;
      addr = 0x80000000u | (d.rd32(addr) & 0xFFFFFF);
      continue;
    }
    const u32 tag = d.rd32(addr);
    std::vector<u32> wv;
    for (u32 k = 0; k < (tag >> 24); ++k) wv.push_back(d.rd32(addr + 4 + 4 * k));
    out[static_cast<size_t>(entry)].push_back(wv);
    if ((tag & 0xFFFFFF) == 0xFFFFFF) break;
    addr = 0x80000000u | (tag & 0xFFFFFF);
  }
  return out;
}

// Same GP0 words; the top byte of a gouraud vertex colour word is unused.
bool same_prim(psyq::P_TAG* mine, const std::vector<u32>& ref) {
  if (mine->len != ref.size()) return false;
  const u8 code = static_cast<u8>(ref[0] >> 24);
  const bool gouraud = (code & 0xF0) == 0x30 || (code & 0xF0) == 0x50;
  const bool ft4 = (code & 0xFC) == 0x2C && ref.size() == 9;
  for (size_t j = 0; j < ref.size(); ++j) {
    u32 a = words(mine)[j], b = ref[j];
    if (gouraud && j > 0 && (j & 1) == 0) a &= 0xFFFFFF, b &= 0xFFFFFF;
    if (ft4 && (j == 6 || j == 8)) a &= 0xFFFF, b &= 0xFFFF;  // u2v2 / u3v3: the upper halves are padding
    if (a != b) return false;
  }
  return true;
}

// Rebuilds the dump's particles one update back, runs them in pool order and checks
// that every primitive they build is in the dump's layer at the same entry. Returns
// {built, found}; 'ref_left' gets the reference effect primitives nobody built (those
// of particles that ended in that update).
struct Result {
  int built = 0, found = 0, ref_effect = 0;
};
Result rerun_particles(const Dump& d, int stale_otz) {
  Result r;
  test_screen();
  field_free_ot_layers();
  setup_from_dump(d);
  const s32 ticks = globals().frame_ticks;
  // Layers with the dump's ids, in the same slots.
  FxState& fx = fx_state();
  u32 bufs[4];
  for (int i = 0; i < 4; ++i) {
    bufs[i] = d.rd32(0x800761C4 + 4 * i);
    if (bufs[i] != 0) {
      fx.layer[i] = std::make_unique<OtLayer>();
      fx.layer_id[i] = d.rd32(0x800761B4 + 4 * i);
      ot_layer_clear(fx.layer[i]->current());
    }
  }
  fx_pool_create();
  const u32 pool = d.rd32(0x800761AC);
  for (int i = 0; i < kFxPoolSize; ++i) {
    const u32 a = pool + static_cast<u32>(i) * 0x50;
    Particle& p = fx.pool[i];
    p.type = d.rd16(a);
    if (p.type == 0) continue;
    p.flag = static_cast<s16>(d.rd16(a + 2));
    const u32 lb = d.rd32(a + 4);
    p.layer = nullptr;
    for (int k = 0; k < 4; ++k)
      if (lb == bufs[k] && lb != 0) p.layer = fx.layer[k].get();
    for (int k = 0; k < 4; ++k) {
      p.pos[k] = d.s(a + 8 + 4 * k);
      p.vel[k] = d.s(a + 0x18 + 4 * k);
      p.tgt[k] = d.s(a + 0x28 + 4 * k);
    }
    p.p38 = d.s(a + 0x38);
    p.p3c = d.s(a + 0x3C);
    p.p40 = d.s(a + 0x40);
    p.p44 = d.s(a + 0x44);
    p.p48 = d.s(a + 0x48);
    p.p4c = static_cast<s16>(d.rd16(a + 0x4C));
    // One update back.
    switch (p.type) {
      case 1:  // streak: integrated by vel, sped up by accel
        // Spawned this frame by the script but not run yet (the dump is taken where the
        // vblank falls, between the VM and the SceneRenderer): its velocity is still 0.
        if (p.vel[0] == 0 && p.vel[1] == 0 && p.vel[2] == 0) {
          p.type = 0;
          break;
        }
        for (int k = 0; k < 3; ++k) p.pos[k] -= p.vel[k] * ticks;
        p.p44 -= p.p38 * ticks;
        break;
      case 2:  // spark: gravity after the step
        p.vel[1] -= ticks * 0x600;
        for (int k = 0; k < 3; ++k) p.pos[k] -= p.vel[k] * ticks;
        p.p44 += ticks;
        break;
      case 3:
        p.p48 -= p.p40;
        p.p44 += ticks;
        break;
      case 5:
        p.p48 -= p.p40;
        p.p44 += ticks;
        break;
      case 6:
      case 7:
        p.p38 += ticks;
        break;
      case 8:  // burst: spin, age, then the fade once older than 0x20
        p.p38 = (p.p38 - ticks * 0x20) & 0xFFF;
        if (p.p3c > 0x20) p.p40 -= ticks * 0x10;
        p.p3c -= ticks;
        break;
      case 9:
        p.p48 -= p.p40;
        p.p44 += ticks;
        p.p4c = static_cast<s16>((static_cast<u16>(p.p4c) - p.vel[0] * ticks) & 0xFFF);
        break;
      default:
        p.type = 0;  // not checked
        break;
    }
  }
  // The stale OTZ the line and burst drawers use (taken from the reference's slots).
  if (stale_otz >= 0) ss::psyq::gte().write_data(psx::Gte::OTZ, static_cast<u32>(stale_otz));
  for (int i = 0; i < kFxPoolSize; ++i) {
    Particle* p = &fx.pool[i];
    if (p->type != 0 && p->layer != nullptr) fx_particle_update(p);
  }
  for (int l = 0; l < 4; ++l) {
    if (!fx.layer[l]) continue;
    const u32 half = d.ram[bufs[l] & 0x1FFFFF];
    auto ref = walk_ot(d, bufs[l] + half * 0x20C0 + 4, kFxLayerEntries, 0x82E);
    psyq::P_TAG* table = fx.layer[l]->current();
    for (int e = 0; e < 0x82E; ++e) {
      std::vector<bool> used(ref[static_cast<size_t>(e)].size(), false);
      for (const auto& wv : ref[static_cast<size_t>(e)]) {
        const u8 code = static_cast<u8>(wv[0] >> 24);
        if (code == 0x3A || code == 0x52 || code == 0x32 || code == 0x2E || code == 0x38) ++r.ref_effect;
      }
      for (psyq::P_TAG* m : chain(&table[e], table, kFxLayerEntries)) {
        if (m->len == 2) continue;  // DR_MODEs: identical words everywhere
        ++r.built;
        for (size_t k = 0; k < ref[static_cast<size_t>(e)].size(); ++k) {
          if (!used[k] && same_prim(m, ref[static_cast<size_t>(e)][k])) {
            used[k] = true;
            ++r.found;
            break;
          }
        }
      }
    }
  }
  field_free_ot_layers();
  map_data().scene.clear();
  return r;
}

// The OTZ a dumped layer's first LINE_G2 (slot 0x820 - OTZ / 8) or burst POLY_G4 (slot
// 0x824 - OTZ / 8) was drawn with (-1 if none).
int stale_otz_of(const Dump& d, u8 code, int base) {
  for (int l = 0; l < 4; ++l) {
    const u32 buf = d.rd32(0x800761C4 + 4 * l);
    if (buf == 0) continue;
    const u32 half = d.ram[buf & 0x1FFFFF];
    auto ref = walk_ot(d, buf + half * 0x20C0 + 4, kFxLayerEntries, 0x82E);
    for (int e = 0; e < 0x82E; ++e)
      for (const auto& wv : ref[static_cast<size_t>(e)])
        if ((wv[0] >> 24) == code) return (base - e) << 3;
  }
  return -1;
}

}  // namespace

TEST(fx_reference_ring_588) {
  Dump d;
  if (!load_dump("ring588.bin", &d)) return;
  const Result r = rerun_particles(d, -1);
  std::printf("  ring: %d of %d primitives found in the original's layer (%d effect primitives there)\n", r.found,
              r.built, r.ref_effect);
  CHECK(r.built > 0);
  CHECK_EQ(r.found, r.built);
  CHECK_EQ(r.ref_effect, r.built);
}

TEST(fx_reference_streak_574) {
  Dump d;
  if (!load_dump("streak574.bin", &d)) return;
  const Result r = rerun_particles(d, stale_otz_of(d, 0x52, 0x820));
  std::printf("  streaks: %d of %d primitives found in the original's layer (%d effect primitives there)\n", r.found,
              r.built, r.ref_effect);
  CHECK(r.built > 0);
  CHECK_EQ(r.found, r.built);
}

TEST(fx_reference_ring_textured_61) {
  Dump d;
  if (!load_dump("ringtex61.bin", &d)) return;
  const Result r = rerun_particles(d, -1);
  std::printf("  textured ring: %d of %d primitives found in the original's layer (%d effect primitives there)\n",
              r.found, r.built, r.ref_effect);
  CHECK(r.built > 0);
  CHECK_EQ(r.found, r.built);
}

TEST(fx_reference_beam_575) {
  Dump d;
  if (!load_dump("beam575.bin", &d)) return;
  const Result r = rerun_particles(d, stale_otz_of(d, 0x52, 0x820));
  std::printf("  beam: %d of %d primitives found in the original's layer (%d effect primitives there)\n", r.found,
              r.built, r.ref_effect);
  CHECK(r.built > 0);
  CHECK_EQ(r.found, r.built);
}

TEST(fx_reference_burst_575) {
  Dump d;
  if (!load_dump("burst575.bin", &d)) return;
  const Result r = rerun_particles(d, stale_otz_of(d, 0x3A, 0x824));
  std::printf("  burst: %d of %d primitives found in the original's layer (%d effect primitives there)\n", r.found,
              r.built, r.ref_effect);
  CHECK(r.built > 0);
  CHECK_EQ(r.found, r.built);
}

TEST(fx_reference_glow_548) {
  Dump d;
  if (!load_dump("glow548.bin", &d)) return;
  const u32 g = d.rd32(0x800752DC);
  CHECK(g != 0);
  if (g == 0) return;
  // The screen OT the frame was built in (Screen 0x80075290: +0xf8 current buffer,
  // buffer +0x70 OT).
  const u32 screen = d.rd32(0x80075290);
  const u32 ot = d.rd32(d.rd32(screen + 0xF8) + 0x70);
  auto ref = walk_ot(d, ot, kOtEntries, 0x82F);
  const auto& at = ref[0x110];
  Screen* scr = test_screen();
  globals().frame_ticks = d.s(0x8007531C);
  auto* glow = new GlowFx;
  glow->frames = d.s(g + 8);
  glow->timer = d.s(g + 0xC) - globals().frame_ticks;  // one draw back
  for (int i = 0; i < 3; ++i) glow->rgb[i] = d.s(g + 0x10 + 4 * i);
  glow_draw(glow);
  std::vector<psyq::P_TAG*> mine = chain(scr->ot_entry(0x110), scr->current->ot.data(), kOtEntries);
  int found = 0;
  std::vector<bool> used(at.size(), false);
  for (psyq::P_TAG* m : mine)
    for (size_t k = 0; k < at.size(); ++k)
      if (!used[k] && same_prim(m, at[k])) {
        used[k] = true;
        ++found;
        break;
      }
  std::printf("  glow: %d of %zu primitives found in the original's OT[0x110] (%zu there)\n", found, mine.size(),
              at.size());
  CHECK_EQ(mine.size(), 25u);
  CHECK_EQ(found, 25);
  task_unregister(glow);
  delete glow;
}
