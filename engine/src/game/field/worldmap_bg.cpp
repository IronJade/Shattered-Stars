// World-map BG object (overlay #3102 0x80088814-0x8008f420 minus the draw): creation,
// map section 3, block streaming, lighting regions, suspend / resume and the terrain
// height queries the walkers, the camera and the field use.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <cstring>
#include <memory>

#include "disc/slz.h"
#include "game/boot/system.h"
#include "game/common/program_image.h"
#include "game/common/texture.h"
#include "game/field/fade.h"
#include "game/field/field.h"
#include "game/field/load_queue.h"
#include "game/field/map.h"
#include "game/field/music.h"
#include "game/field/worldmap.h"
#include "game/field/worldmap_internal.h"
#include "runtime/libgpu.h"
#include "runtime/runtime.h"

namespace ss::game::field {
using namespace wm;

// worldmap_camera.cpp
void world_cam_create();
void world_cam_destroy();
void world_camera_manual(WorldBg* bg);
int worldcam_set_mode(int mode);
// worldmap_render.cpp
void world_draw_markers(WorldBg* bg, int mode);

namespace {

WorldGlobals g_world;
std::unique_ptr<WorldBg> g_bg;

constexpr int kSlotBytes = 0xd000;

FieldGlobals& fg() { return globals(); }
s16 world_map() { return static_cast<s16>(read_u16(fg().game_state + 0x1a)); }

// Map 1's alternate block (story state game_state word 0 in 0xf..0x3c): 0x1004 -> 0x1013.
bool map1_alternate() {
  return world_map() == 1 && static_cast<u32>(rd32(fg().game_state) - 0xf) < 0x2e;
}

int block_base(int map) { return map == 1 ? 0xfd4 : map == 2 ? 0x1060 : 0x10de; }

std::vector<u8> slz_unpack_at(const u8* data) {
  auto out = disc::slz::unpack_block(std::span<const u8>(data, 0x100000), 0, 0, nullptr);
  return out ? std::move(*out) : std::vector<u8>{};
}

// 0x80088e64: darker CLUT copy for distant polygons (each channel - 9, STP set).
void darken_clut(u16* clut) {
  for (int i = 0; i < 0x100; ++i) {
    const u16 c = clut[i];
    s32 r = (c & 0x1f) - 9, g = (c >> 5 & 0x1f) - 9, b = (c >> 10 & 0x1f) - 9;
    if (r < 0) r = 0;
    if (g < 0) g = 0;
    if (b < 0) b = 0;
    clut[i] = static_cast<u16>(r | g << 5 | 0x8000 | b << 10);
  }
}

// 0x80088eec (section 3 tag 5): the visibility grid, the first nine blocks (embedded in
// the container), the fog / sky defaults and the camera block.
void init_blocks(WorldBg* bg, const u8* section, const u8* list) {
  WorldGlobals& w = g_world;
  for (int row = 0; row < 13; ++row) {
    for (int col = 0; col < 13; ++col) {
      s16* p = bg->block_xz[row * 13 + col];
      p[0] = static_cast<s16>(-0x3000 + col * 0xc00);
      p[1] = -100;
      p[2] = static_cast<s16>(-0x3000 + row * 0xc00);
    }
  }
  bg->recentre = 1;
  if (w.blocks.empty()) w.blocks.assign(9 * kSlotBytes, 0);
  const int base = block_base(world_map());
  for (int i = 0; i < 9; ++i) {
    const s32 rel = rd32(list);
    if (rel == -1) {
      w.loaded[i] = -1;
      continue;  // the list pointer stays: every later slot reads the -1 too
    }
    s16 id = static_cast<s16>(rel + base);
    if (map1_alternate() && id == 0x1004) id = 0x1013;
    w.loaded[i] = id;
    const auto data = slz_unpack_at(section + rd32(list + 4));
    std::memcpy(w.blocks.data() + i * kSlotBytes, data.data(), std::min<size_t>(data.size(), kSlotBytes));
    list += 8;
  }
  w.pending = -1;
  WorldView& v = w.view;
  v.base_far[0] = 0x30;
  v.base_far[1] = 0x60;
  v.base_far[2] = 0x60;
  v.sky_g[0] = 0x18;
  v.sky_r[1] = 0x3c;
  v.sky_g[1] = 0xb8;
  map_data().projection_h = 0x200;
  w.pending_done = 0;
  v.sky_r[0] = 0;
  v.sky_b[0] = 0xff;
  v.sky_b[1] = 0xff;
  map_data().actor_scale = 100;
  v.geom_ofs_x = 0;
  v.geom_ofs_y = 0;
  w.cam_frames = 0;
  world_camera_reset();
}

// 0x8008e2ac: the nine blocks around (x, z) in layout order (row-major from (-1, -1))
// and the same nine sorted by the original's distance key (|dx|^2 * |dz|^2).
void blocks_around(int map, s32 x, s32 z, s16 layout[9], s16 by_distance[9]) {
  if (map == 3) {
    layout[0] = by_distance[0] = 0x10de;
    layout[1] = by_distance[1] = 0x10df;
    for (int i = 2; i < 9; ++i) layout[i] = by_distance[i] = -1;
    return;
  }
  s32 cx = x < 0 ? x + 0x1b000 : x;
  s32 cz = z < 0 ? z + 0x15000 : z;
  cx /= 0x3000;
  cz /= 0x3000;
  s32 dx2[3], dz2[3], key[9];
  for (int i = 0; i < 3; ++i) {
    const s32 dx = (x - 0x1800 - (cx + i - 1) * 0x3000) >> 8;
    const s32 dz = (z - 0x1800 - (cz + i - 1) * 0x3000) >> 8;
    dx2[i] = dx * dx;
    dz2[i] = dz * dz;
  }
  for (int r = 0; r < 3; ++r)
    for (int c = 0; c < 3; ++c) key[r * 3 + c] = dz2[r] * dx2[c];
  const s16 base = static_cast<s16>(map == 1 ? 0xfd4 : 0x1060);
  layout[4] = static_cast<s16>(base + cx + cz * 9);
  const s16 xm = static_cast<s16>(base + (cx + 8) % 9);
  const s16 x0 = static_cast<s16>(base + cx % 9);
  const s16 xp = static_cast<s16>(base + (cx + 1) % 9);
  s16 zr = static_cast<s16>(((cz + 6) % 7) * 9);
  layout[0] = static_cast<s16>(xm + zr);
  layout[1] = static_cast<s16>(x0 + zr);
  layout[2] = static_cast<s16>(xp + zr);
  zr = static_cast<s16>((cz % 7) * 9);
  layout[3] = static_cast<s16>(xm + zr);
  layout[5] = static_cast<s16>(xp + zr);
  zr = static_cast<s16>(((cz + 1) % 7) * 9);
  layout[6] = static_cast<s16>(xm + zr);
  layout[7] = static_cast<s16>(x0 + zr);
  layout[8] = static_cast<s16>(xp + zr);
  for (int i = 0; i < 9; ++i) by_distance[i] = layout[i];
  for (int i = 0; i < 8; ++i) {
    int best = i;
    for (int j = i + 1; j < 9; ++j)
      if (key[j] < key[best]) best = j;
    if (best != i) {
      std::swap(by_distance[best], by_distance[i]);
      std::swap(key[best], key[i]);
    }
  }
  if (map == 1 && static_cast<u32>(rd32(fg().game_state) - 0xf) < 0x2e) {
    for (int i = 0; i < 9; ++i) {
      if (by_distance[i] == 0x1004) by_distance[i] = 0x1013;
      if (layout[i] == 0x1004) layout[i] = 0x1013;
    }
  }
}

// 0x80089198: block streaming. One read at a time; a slot whose block is no longer
// wanted takes the nearest missing block.
void stream_blocks(WorldBg* bg) {
  WorldGlobals& w = g_world;
  s16 by_distance[9];
  blocks_around(world_map(), bg->centre_x, bg->centre_z, w.wanted, by_distance);
  if (w.pending != -1) {
    if (w.pending_done == 0) return;
    w.loaded[w.pending_slot] = w.pending;
    w.pending = -1;
  }
  // The nearest wanted block that is not loaded.
  int k = 0;
  for (; k < 9; ++k) {
    if (by_distance[k] == -1) continue;
    int i = 0;
    for (; i < 9; ++i)
      if (w.loaded[i] == by_distance[k]) break;
    if (i == 9) break;
  }
  if (k == 9) return;
  // A slot that is free or no longer wanted.
  int slot = 0;
  for (; slot < 9; ++slot) {
    if (w.loaded[slot] == -1) break;
    int i = 0;
    for (; i < 9; ++i)
      if (w.loaded[slot] == by_distance[i]) break;
    if (i == 9) {
      w.loaded[slot] = -1;
      break;
    }
  }
  if (slot > 8) return;
  w.pending = by_distance[k];
  w.pending_slot = static_cast<s16>(slot);
  w.pending_done = 0;
  if (w.blocks.empty()) w.blocks.assign(9 * kSlotBytes, 0);
  load_queue_push(load_queue(), w.pending, w.blocks.data() + slot * kSlotBytes, &w.pending_done, 0);
}

// 0x8008e9b4: the terrain light, fog and sky colours from the lighting region the
// camera focus is in, then the BG tint over all of them. A region has a radius and three
// inner rings (params 1-3 for the light; params 5-7 / 9-11 blended by the camera's
// facing for fog and sky): outside ring 1 the colours fade towards the "dusk" set, between
// rings 1 and 2 they are dusk, then they fade towards the "night" set (ring 3), and inside
// ring 3 they are night.
void update_lighting(WorldBg* bg) {
  WorldGlobals& w = g_world;
  WorldView& v = w.view;
  v.light[0] = v.light[1] = v.light[2] = 0x80;
  for (int i = 0; i < 3; ++i) v.far_colour[i] = v.base_far[i];
  for (int i = 0; i < 2; ++i) {
    v.sky_r_now[i] = v.sky_r[i];
    v.sky_g_now[i] = v.sky_g[i];
    v.sky_b_now[i] = v.sky_b[i];
  }
  w.in_region = 0;
  int r = 0;
  s32 dist = 0;
  for (; r < w.region_count; ++r) {
    const s32 radius = w.region[r][0];
    const s32 dx = w.region_pos[r][0] - bg->centre_x;
    const s32 dz = w.region_pos[r][1] - bg->centre_z;
    if (std::abs(dx) > radius || std::abs(dz) > radius) continue;
    dist = world_sqrt(dx * dx + dz * dz);
    if (dist < radius) break;
  }
  if (r < w.region_count) {
    const s16* reg = w.region[r];
    w.in_region = (dist * 9) / 10 < reg[0] ? 1 : 0;
    s32 a = world_camera_yaw() + 0x400;
    if (a > 0x7ff) a -= 0x1000;
    if (a < 0) a = -a;
    const s32 fa = 0x800 - a;
    const s32 fs = fa * 0x18;
    auto q11 = [](s32 t) { return (t < 0 ? t + 0x7ff : t) >> 11; };
    auto blend = [&](s32 x, s32 y) { return q11(x * fa + y * a); };
    // Dusk colours: light (0xa0, 0x32, 0); fog / sky by facing. Night: light (0x1e, 0,
    // 0x80); fog black; sky (0x1e, 0, 0x80).
    const s32 dusk_far[3] = {blend(0x20, 0x8c), q11(fs + a * 0x40), blend(0x58, 0x1f)};
    const s32 dusk_sky[3] = {blend(0x30, 200), q11(fs + a * 0x50), q11(fa * 0x60)};
    for (int pass = 0; pass < 2; ++pass) {
      s32 outer = reg[0], inner = 0;
      int ring = 1;
      for (int k = 0; ring < 4; ++k) {
        const s32 t = pass == 0 ? reg[1 + k] : q11(reg[9 + k] * fa + reg[5 + k] * a);
        inner = t;
        if (pass == 0 ? t < dist : !(dist <= t)) break;
        ++ring;
        outer = t;
      }
      s32 x = dist;
      s32 target_light[3], target_far[3], target_sky[3];
      if (ring >= 3) {
        if (ring == 4) {
          w.in_region = 2;
          x = 0;
          outer = 1;
          inner = 0;
        }
        if (pass == 0) {
          v.light[0] = 0xa0;
          v.light[1] = 0x32;
          v.light[2] = 0;
        } else {
          for (int i = 0; i < 3; ++i) v.far_colour[i] = static_cast<u8>(dusk_far[i]);
          for (int i = 0; i < 2; ++i) {
            v.sky_r_now[i] = static_cast<u8>(dusk_sky[0]);
            v.sky_g_now[i] = static_cast<u8>(dusk_sky[1]);
            v.sky_b_now[i] = static_cast<u8>(dusk_sky[2]);
          }
        }
        const s32 night[3] = {0x1e, 0, 0x80};
        for (int i = 0; i < 3; ++i) {
          target_light[i] = night[i];
          target_sky[i] = night[i];
          target_far[i] = 0;
        }
      } else {
        if (ring == 2) {
          x = 0;
          outer = 1;
          inner = 0;
        }
        const s32 dusk[3] = {0xa0, 0x32, 0};
        for (int i = 0; i < 3; ++i) {
          target_light[i] = dusk[i];
          target_far[i] = dusk_far[i];
          target_sky[i] = dusk_sky[i];
        }
      }
      s32 span = outer - inner;
      if (span == 0) span = 1;
      const s32 wa = outer - x, wb = x - inner;
      if (pass == 0) {
        for (int i = 0; i < 3; ++i) v.light[i] = static_cast<s16>((target_light[i] * wa + v.light[i] * wb) / span);
      } else {
        for (int i = 0; i < 3; ++i)
          v.far_colour[i] = static_cast<u8>((target_far[i] * wa + static_cast<s32>(v.far_colour[i]) * wb) / span);
        for (int i = 0; i < 2; ++i) {
          v.sky_r_now[i] = static_cast<u8>((target_sky[0] * wa + static_cast<s32>(v.sky_r_now[i]) * wb) / span);
          v.sky_g_now[i] = static_cast<u8>((target_sky[1] * wa + static_cast<s32>(v.sky_g_now[i]) * wb) / span);
          v.sky_b_now[i] = static_cast<u8>((target_sky[2] * wa + static_cast<s32>(v.sky_b_now[i]) * wb) / span);
        }
      }
    }
  }
  bg->light_rgb[0] = static_cast<u8>(v.light[0]);
  bg->light_rgb[1] = static_cast<u8>(v.light[1]);
  bg->light_rgb[2] = static_cast<u8>(v.light[2]);
  const s16* tint = g_fade_targets.bg_tint;
  auto mul7 = [](s32 a, s32 b) {
    s32 t = a * b;
    if (t < 0) t += 0x7f;
    return t >> 7;
  };
  for (int i = 0; i < 3; ++i) v.light[i] = static_cast<s16>(mul7(v.light[i], tint[i]));
  for (int i = 0; i < 3; ++i) v.far_colour[i] = static_cast<u8>(mul7(v.far_colour[i], tint[i]));
  for (int i = 0; i < 2; ++i) {
    v.sky_r_now[i] = static_cast<u8>(mul7(v.sky_r_now[i], tint[0]));
    v.sky_g_now[i] = static_cast<u8>(mul7(v.sky_g_now[i], tint[1]));
    v.sky_b_now[i] = static_cast<u8>(mul7(v.sky_b_now[i], tint[2]));
  }
}

}  // namespace

namespace wm {
s32 world_ratan2(s32 y, s32 x) { return ratan2(y, x); }
s32 world_sqrt(s32 v) { return SquareRoot0(v); }
}  // namespace wm

WorldGlobals& world() { return g_world; }
WorldBg* world_bg() { return g_bg.get(); }

const u8* world_ovl_data(u32 address) {
  // program_data serves images placed at 0x8002f810; the overlay sits at 0x8007e000.
  return program_data(0xc1e, address - 0x8007e000 + kProgramBase);
}

// field_load_map, world branch: new WorldBg (0x80088814), new WorldCam (0x8007e000).
void world_create_objects() {
  world_destroy_objects();
  g_bg = std::make_unique<WorldBg>();
  WorldBg* bg = g_bg.get();
  task_register(bg, 3);  // FieldTask base 0x80057450
  WorldGlobals& w = g_world;
  w.streaming = 1;
  for (int i = 0; i < 3; ++i) bg->focus[i] = 0;
  w.blocks.clear();
  w.curve.clear();
  bg->turn_speed = 0;
  bg->pitch_speed = 0;
  w.cam_pitch_deg = 0x15;
  bg->pitch = 0xee000;
  world_cam_create();
  g_fade_targets.bg_set_tint_enabled = [](int) {};  // vtable[5] 0x8008b03c: nothing
  g_fade_targets.bg_set_tint = [](int r, int g, int b) { world_bg_set_light(r, g, b); };
}

void world_register_party_sprites(const u8* t) {
  const u32 leader = fg().game_state[0x24];
  sprite_pack_register(t + rd32(t + leader * 4), -2, 2);
  sprite_pack_register(t + rd32(t + (leader ^ 1) * 4 + 8), -3, 2);
  const u32 mask = party_member_mask_all();
  s32 id = -4;
  for (u32 c = 0; c < 12; ++c) {
    if (c != leader && ((mask >> c) & 1) != 0) sprite_pack_register(t + rd32(t + c * 4 + 8), id--, 2);
  }
}

void world_destroy_objects() {
  if (g_bg) {
    // vtable[2] 0x80088894: release the blocks, then the FieldTask.
    world_bg_release(g_bg.get());
    task_unregister(g_bg.get());
    g_bg.reset();
  }
  world_cam_destroy();
}

void world_cancel_block_load() {  // 0x80089368
  WorldGlobals& w = g_world;
  if (w.pending != -1 && w.pending_done == 0) {
    load_queue_cancel_entry(load_queue(), w.pending);
    w.pending = -1;
  }
}

void world_bg_release(WorldBg*) {  // 0x800888e0
  world_cancel_block_load();
  g_world.blocks.clear();
  g_world.blocks.shrink_to_fit();
  g_world.curve.clear();
}

// 0x80088a3c: the chunk container's section 3, a tagged stream.
void world_parse_section3(WorldBg* bg, const u8* data) {
  WorldGlobals& w = g_world;
  psx::Gpu& gpu = runtime::Runtime::get().gpu;
  bg->centre_x = -1;
  for (int i = 0; i < 24; ++i) w.anim_first[i] = 0xff;
  int frame = 0, group = 0, clut_row = 0x1e0;
  const u8* p = data;
  for (;;) {
    const s32 tag = rd32(p);
    if (tag == 0) break;
    switch (tag) {
      case 1:
      case 2:
      case 7: {
        const s32 x = static_cast<s16>(read_u16(p + 4)), y = static_cast<s16>(read_u16(p + 6));
        auto img = slz_unpack_at(data + rd32(p + 8));
        img.resize(std::max<size_t>(img.size(), 4 + 0x200 + 8));
        u16* words = reinterpret_cast<u16*>(img.data());
        const u16 flags = words[0], cluts = words[1];
        u16* q = words + 2;
        if (cluts != 0) {
          if (tag == 1) {
            upload_clut256(gpu, q, 0, clut_row);
            psyq::DrawSync(0);
            darken_clut(q);
            upload_clut256(gpu, q, 0, clut_row + 0xc);
            ++clut_row;
          } else {
            upload_clut256(gpu, q, 0, tag == 2 ? 0x1fb : 0x1fa);
          }
          q += cluts;
        }
        const int depth = flags & 3;
        upload_texture(gpu, q + 2, depth, 0, x, y, q[0] << (2 - depth), q[1]);
        psyq::DrawSync(0);
        p += 12;
        break;
      }
      case 3: {
        w.anim_first[group] = static_cast<u8>(frame);
        w.anim_cur[group] = static_cast<u8>(frame);
        w.anim_dest[group][0] = static_cast<s16>(read_u16(p + 4));
        w.anim_dest[group][1] = static_cast<s16>(read_u16(p + 6));
        p += 8;
        for (;;) {
          const s32 duration = rd32(p);
          p += 4;
          if (duration == 0) break;
          if (frame < 0x83) {
            w.anim_duration[frame] = static_cast<u8>(duration);
            s16* f = w.anim_frame[frame];
            f[0] = rd16(p);
            f[1] = rd16(p + 2);
            f[2] = static_cast<s16>(rd16(p + 4) - f[0]);
            f[3] = static_cast<s16>(rd16(p + 6) - f[1]);
          }
          ++frame;
          p += 8;
        }
        if (group < 24) {
          w.anim_timer[group] = 0;
          w.anim_end[group] = static_cast<s8>(frame);
        }
        ++group;
        break;
      }
      case 4:
        w.curve = slz_unpack_at(data + rd32(p + 4));
        p += 8;
        break;
      case 5:
        if (((fg().script_flags[1] >> 4) & 1) == 0) field_start_pending_music(1);
        init_blocks(bg, data, data + rd32(p + 4));
        p += 8;
        break;
      case 6: {
        const s32 x = static_cast<s16>(read_u16(p + 4));
        const s32 y = read_u16(p + 6);
        // The CLUT is darkened in place in the container (as the original does).
        u16* clut = reinterpret_cast<u16*>(const_cast<u8*>(data + rd32(p + 8) + 0x14));
        upload_clut256(gpu, clut, x, y);
        psyq::DrawSync(0);
        darken_clut(clut);
        upload_clut256(gpu, clut, x, y + 5);
        psyq::DrawSync(0);
        p += 12;
        break;
      }
      default:
        runtime::Runtime::get().log_once("world:tag", "world: unknown section 3 tag");
        return;
    }
  }
}

// vtable[1] 0x80088948.
void WorldBg::update() {
  WorldBg* bg = this;
  FieldGlobals& g = fg();
  // Debugging: SS_WORLD_TICKS=FILE replays the original's frame pacing (one frame_ticks
  // value per frame from the world map's start, as the PCSX-Redux world script logs with
  // WW_TICKS): the next frame waits that many vblanks.
  static const std::vector<int> replay = [] {
    std::vector<int> t;
    if (const char* path = std::getenv("SS_WORLD_TICKS"))
      if (FILE* fp = std::fopen(path, "r")) {
        for (int v; std::fscanf(fp, "%d", &v) == 1;) t.push_back(v);
        std::fclose(fp);
      }
    return t;
  }();
  static size_t replay_at = 0;
  static bool started = false;
  if (!started) {
    started = true;
    runtime::Runtime::get().trace("world map (WorldBg update)");
  }
  if (!replay.empty()) g.vsync_mode = static_cast<u8>(std::max(1, replay[std::min(replay_at++, replay.size() - 1)]));
  update_lighting(bg);
  if (static_cast<s16>(read_u16(g.field_flags + 0x10)) == -1) world_camera_manual(bg);
  if (g.pressed[0] & 0x800) g.game_state[0x20] = static_cast<u8>((g.game_state[0x20] + 1) % 3);
  if (g.game_state[0x20] != 0) world_draw_markers(bg, g.game_state[0x20]);
  if (bg->recentre != 0) {
    worldcam_set_mode(10);
    bg->recentre = 0;
  }
  stream_blocks(bg);
  world_bg_draw(bg);
}

// vtable[3] 0x8008ae48: the camera yaw into game_state +0x1e; the lighting regions into
// a blob for handoff 0x10 (bg_take_anim_save hands it over, as on field maps).
u8* g_saved_regions = nullptr;  // 0x80075728 on the world map
void world_bg_suspend(WorldBg* bg) {
  WorldGlobals& w = g_world;
  const s16 yaw = static_cast<s16>(bg->yaw >> 12);
  std::memcpy(fg().game_state + 0x1e, &yaw, 2);
  delete[] g_saved_regions;
  g_saved_regions = nullptr;
  if (w.region_count == 0) return;
  const s32 size = w.region_count * 0x20 + 8;
  g_saved_regions = new u8[static_cast<size_t>(size)]();
  u8* out = g_saved_regions;
  wr32(out, static_cast<u32>(size));
  wr32(out + 4, static_cast<u32>(w.region_count));
  u8* q = out + 8;
  for (int i = 0; i < w.region_count; ++i) {
    wr32(q, static_cast<u32>(w.region_pos[i][0]));
    wr32(q + 4, static_cast<u32>(w.region_pos[i][1]));
    std::memcpy(q + 8, w.region[i], 0x18);
    q += 0x20;
  }
}
u8* world_take_region_save() {
  u8* p = g_saved_regions;
  g_saved_regions = nullptr;
  return p;
}

// vtable[4] 0x8008af54: the lighting regions back from handoff 0x10.
void world_bg_resume(WorldBg*) {
  WorldGlobals& w = g_world;
  auto* blob = static_cast<u8*>(handoff_get(fg().handoff, 0x10));
  if (blob == nullptr) return;
  w.region_count = static_cast<s16>(std::min(rd32(blob + 4), 16));
  const u8* q = blob + 8;
  for (int i = 0; i < w.region_count; ++i) {
    w.region_pos[i][0] = rd32(q);
    w.region_pos[i][1] = rd32(q + 4);
    std::memcpy(w.region[i], q + 8, 0x18);
    q += 0x20;
  }
  delete[] blob;
  handoff_clear(fg().handoff, 0x10);
}

void world_bg_set_light(int r, int g, int b) {  // vtable[6] 0x8008b044
  WorldView& v = g_world.view;
  v.light[0] = static_cast<s16>(r);
  v.light[1] = static_cast<s16>(g);
  v.light[2] = static_cast<s16>(b);
}

void world_bg_set_angle(WorldBg* bg, s32 angle) { bg->yaw = angle << 12; }  // 0x8008f404
void world_bg_set_centre(WorldBg* bg, s32 x, s32 z) {  // 0x8008f410
  bg->centre_x = x;
  bg->centre_z = z;
}

void world_load_map_reset() {
  g_world.region_count = 0;
  g_world.leader_hit = TerrainHit{};
  world_vehicle_models_reset();
}

// Script table A 0x24 (worldmap_add_entry): a lighting region.
void world_add_light_region(const s32 args[14]) {
  WorldGlobals& w = g_world;
  if (w.region_count >= 16) return;
  const int i = w.region_count++;
  w.region_pos[i][0] = args[0];
  w.region_pos[i][1] = args[1];
  for (int k = 0; k < 12; ++k) w.region[i][k] = static_cast<s16>(args[2 + k]);
}

// ---- Terrain queries ----

void world_block_origin(const WorldBg* bg, s16 out[2]) {  // 0x8008c8e0
  out[0] = static_cast<s16>((bg->centre_x / 0x3000) * 0x3000);
  out[1] = static_cast<s16>((bg->centre_z / 0x3000) * 0x3000);
}

int world_block_slot(WorldBg*, int block) {  // 0x8008c940
  const WorldGlobals& w = g_world;
  const int map = world_map();
  const int base = block_base(map);
  for (int i = 0; i < 9; ++i) {
    if (w.loaded[i] == block + base) return i;
    if (map1_alternate() && block == 0x30 && w.loaded[i] == 0x1013) return i;
  }
  return -1;
}

namespace {

// Vertex record of a block: s16 x, y, z, x (the last word packs x << 16 | z for one load).
inline s32 vx_of(const u8* v) { return rd32(v + 4) >> 16; }
inline s32 vz_of(const u8* v) { return static_cast<s16>(rd32(v + 4)); }
inline s32 vy_of(const u8* v) { return rd16(v + 2); }

// GTE OP (outer product, sf 0) of (a.x, a.y, a.z) x (b.x, b.y, b.z): the scratchpad
// SVECTORs of 0x8008da50 / 0x8008dd98 (only x, y, z are used).
void op0(s16 ax, s16 ay, s16 az, s16 bx, s16 by, s16 bz, s32 out[3]) {
  G& g = gte();
  // gte_ldopv1SV: R11 R22 R33 = a; gte_ldopv2SV: IR = b; op0.
  g.write_ctrl(G::R11R12, static_cast<u16>(ax));
  g.write_ctrl(G::R22R23, static_cast<u16>(ay));
  g.write_ctrl(G::R33, static_cast<u32>(static_cast<s32>(az)));
  ld_ir(bx, by, bz);
  run(G::kOp0);
  out[0] = mac(1);
  out[1] = mac(2);
  out[2] = mac(3);
}

// 0x8008da50: height of triangle (a, b, c) under (bg+0x18, bg+0x1c), or -0x8000 when the
// point is outside. Also stores the triangle's normal (0x800756e4).
s32 tri_height(WorldBg* bg, const u8* base, u32 a, u32 b, u32 c) {
  const s32 px = bg->query_x, pz = bg->query_z;
  const u8* va = base + a * 2;
  const u8* vb = base + b * 2;
  const u8* vc = base + c * 2;
  const s16 xa = static_cast<s16>(vx_of(va)), za = static_cast<s16>(vz_of(va));
  const s16 xb = static_cast<s16>(vx_of(vb)), zb = static_cast<s16>(vz_of(vb));
  const s16 xc = static_cast<s16>(vx_of(vc)), zc = static_cast<s16>(vz_of(vc));
  s32 r[3];
  op0(static_cast<s16>(xb - xa), 0, static_cast<s16>(zb - za), static_cast<s16>(px - xb), 0, static_cast<s16>(pz - zb),
      r);
  if (r[1] < 0) return -0x8000;
  op0(static_cast<s16>(xc - xb), 0, static_cast<s16>(zc - zb), static_cast<s16>(px - xc), 0, static_cast<s16>(pz - zc),
      r);
  if (r[1] < 0) return -0x8000;
  op0(static_cast<s16>(xa - xc), 0, static_cast<s16>(za - zc), static_cast<s16>(px - xa), 0, static_cast<s16>(pz - za),
      r);
  if (r[1] < 1) return -0x8000;
  const s16 ya = static_cast<s16>(vy_of(va));
  op0(static_cast<s16>(xb - xa), static_cast<s16>(vy_of(vb) - ya), static_cast<s16>(zb - za),
      static_cast<s16>(xc - xb), static_cast<s16>(vy_of(vc) - vy_of(vb)), static_cast<s16>(zc - zb), r);
  WorldGlobals& w = g_world;
  w.tri_normal[0] = r[0];
  w.tri_normal[1] = r[1];
  w.tri_normal[2] = r[2];
  w.tri_normal[3] = 0;
  if (r[1] == 0) return -0x8000;
  return (-r[0] * (px - xa) - r[2] * (pz - za)) / r[1] + ya;
}

// 0x8008dd98: the same for quad (a, b, c, d) packed as (a | b << 16, c | d << 16).
s32 quad_height(WorldBg* bg, const u8* base, u32 ab, u32 cd) {
  const s32 px = bg->query_x, pz = bg->query_z;
  const u8* va = base + (ab & 0xffff) * 2;
  const u8* vb = base + (ab >> 16) * 2;
  const u8* vd = base + (cd >> 16) * 2;
  const u8* vc = base + (cd & 0xffff) * 2;
  const s16 xa = static_cast<s16>(vx_of(va)), za = static_cast<s16>(vz_of(va));
  const s16 xb = static_cast<s16>(vx_of(vb)), zb = static_cast<s16>(vz_of(vb));
  const s16 xd = static_cast<s16>(vx_of(vd)), zd = static_cast<s16>(vz_of(vd));
  const s16 xc = static_cast<s16>(vx_of(vc)), zc = static_cast<s16>(vz_of(vc));
  s32 r[3];
  op0(static_cast<s16>(xb - xa), 0, static_cast<s16>(zb - za), static_cast<s16>(px - xb), 0, static_cast<s16>(pz - zb),
      r);
  if (r[1] < 0) return -0x8000;
  op0(static_cast<s16>(xd - xb), 0, static_cast<s16>(zd - zb), static_cast<s16>(px - xd), 0, static_cast<s16>(pz - zd),
      r);
  if (r[1] < 0) return -0x8000;
  op0(static_cast<s16>(xc - xd), 0, static_cast<s16>(zc - zd), static_cast<s16>(px - xc), 0, static_cast<s16>(pz - zc),
      r);
  if (r[1] < 0) return -0x8000;
  op0(static_cast<s16>(xa - xc), 0, static_cast<s16>(za - zc), static_cast<s16>(px - xa), 0, static_cast<s16>(pz - za),
      r);
  if (r[1] < 1) return -0x8000;
  const s16 ya = static_cast<s16>(vy_of(va)), yb = static_cast<s16>(vy_of(vb)), yd = static_cast<s16>(vy_of(vd));
  op0(static_cast<s16>(xb - xa), static_cast<s16>(yb - ya), static_cast<s16>(zb - za), static_cast<s16>(xd - xb),
      static_cast<s16>(yd - yb), static_cast<s16>(zd - zb), r);
  s16 y0 = ya;
  if (r[1] == 0) {
    // Degenerate first half: (d - b) x (c - d), and the height is taken from b (the
    // original keeps a's x / z in the plane equation).
    op0(static_cast<s16>(xd - xb), static_cast<s16>(yd - yb), static_cast<s16>(zd - zb), static_cast<s16>(xc - xd),
        static_cast<s16>(vy_of(vc) - yd), static_cast<s16>(zc - zd), r);
    y0 = yb;
  }
  WorldGlobals& w = g_world;
  w.tri_normal[0] = r[0];
  w.tri_normal[1] = r[1];
  w.tri_normal[2] = r[2];
  w.tri_normal[3] = 0;
  if (r[1] == 0) return -0x8000;  // the console would trap on the division
  return (-r[0] * (px - xa) - r[2] * (pz - za)) / r[1] + y0;
}

// 0x8008ca04: the polygon of one block under the local point (x, z); 'mask' selects the
// sub-cells to search (one bit, 15 - i, per 0xc00 sub-cell).
s32 block_query(WorldBg* bg, const u8* blk, u16 mask, s32 x, s32 z, TerrainHit* hit) {
  bg->query_x = x;
  bg->query_z = z;
  const u8* hdr_base = blk + rd32(blk) * 8 + 0x68;
  for (int i = 0; i < 16; ++i) {
    if (((mask >> (15 - i)) & 1) == 0) continue;
    const u8* hdr = hdr_base + i * 0x20;
    const u8* ext = hdr_base + 0x240 + i * 4;
    const s32 polys = rd32(hdr_base + 0x200 + i * 4) + 0x280;  // from hdr_base
    const s32 hx = rd16(ext), hz = rd16(ext + 2);
    const s32 x0 = (x - 1) - hx, x1 = x + 1 + hx, z0 = (z - 1) - hz, z1 = z + 1 + hz;
    const u8* verts = blk + rd32(blk + 0x28 + i * 4) + 0x68;
    const u8* rec = hdr_base + polys + (read_u16(hdr) & 0xfffc);
    const u8* attrs = hdr_base + polys;
    int index = 0;
    auto inside = [&](const u8* v) {
      const s32 vx = vx_of(v), vz = vz_of(v);
      return x0 <= vx && vx <= x1 && z0 <= vz && vz <= z1;
    };
    auto finish = [&](s32 h, int corners, const u8* a, const u8* b, const u8* c, const u8* d, int k) {
      hit->corners = static_cast<s16>(corners);
      hit->v[0] = a;
      hit->v[1] = b;
      hit->v[2] = c;
      hit->v[3] = d;
      hit->attr = attrs[k];
      return h;
    };
    // A: textured triangles (0x18): a = +0x14 lo, b = +0x14 hi, c = +4 hi.
    const int na = read_u16(hdr + 2);
    for (int k = 0; k < na; ++k, rec += 0x18) {
      const u32 ab = ru32(rec + 0x14);
      const u8* va = verts + (ab & 0xffff) * 2;
      if (!inside(va)) continue;
      const u32 c = read_u16(rec + 6);
      const s32 h = tri_height(bg, verts, ab & 0xffff, ab >> 16, c);
      if (h != -0x8000) return finish(h, 3, va, verts + (ab >> 16) * 2, verts + c * 2, nullptr, index + k);
    }
    index += na;
    // B (0x14): a = +0x10 lo, b = +0xa, c = +0x10 hi; only x is range-checked.
    const int nb = read_u16(hdr + 4);
    for (int k = 0; k < nb; ++k, rec += 0x14) {
      const u32 ac = ru32(rec + 0x10);
      const u8* va = verts + (ac & 0xffff) * 2;
      const s32 vx = rd16(va + 6);
      if (vx < x0 || vx > x1) continue;
      const u32 b = read_u16(rec + 0xa);
      const s32 h = tri_height(bg, verts, ac & 0xffff, b, ac >> 16);
      if (h != -0x8000) return finish(h, 3, va, verts + b * 2, verts + (ac >> 16) * 2, nullptr, index + k);
    }
    index += nb;
    // C (0x10): bytes: a = +0xa, c = +0xb, b = +7.
    const int nc = read_u16(hdr + 6);
    for (int k = 0; k < nc; ++k, rec += 0x10) {
      const u32 t1 = ru32(rec + 4), t0 = ru32(rec + 8);
      const u32 a = (t0 >> 14) & 0x3fc, b = (t1 >> 22) & 0x3fc, c = (t0 >> 22) & 0x3fc;
      const u8* va = verts + a * 2;
      if (!inside(va)) continue;
      const s32 h = tri_height(bg, verts, a, b, c);
      if (h != -0x8000) return finish(h, 3, va, verts + b * 2, verts + c * 2, nullptr, index + k);
    }
    index += nc;
    // D (0x10): a = +0xc lo, b = +0xa, c = +0xc hi.
    const int nd = read_u16(hdr + 8);
    for (int k = 0; k < nd; ++k, rec += 0x10) {
      const u32 ac = ru32(rec + 0xc);
      const u8* va = verts + (ac & 0xffff) * 2;
      if (!inside(va)) continue;
      const u32 b = read_u16(rec + 0xa);
      const s32 h = tri_height(bg, verts, ac & 0xffff, b, ac >> 16);
      if (h != -0x8000) return finish(h, 3, va, verts + b * 2, verts + (ac >> 16) * 2, nullptr, index + k);
    }
    index += nd;
    // E (0x20) and F (0x18) are not walkable.
    const int ne = read_u16(hdr + 0xa), nf = read_u16(hdr + 0xc);
    rec += ne * 0x20 + nf * 0x18;
    index += ne + nf;
    // G (0x14): vertex bytes at +0x10.
    const int ng = read_u16(hdr + 0xe);
    for (int k = 0; k < ng; ++k, rec += 0x14) {
      const u32 vb4 = ru32(rec + 0x10);
      const u8* va = verts + (vb4 & 0xff) * 8;
      if (!inside(va)) continue;
      const u32 b = (vb4 >> 6) & 0x3fc, c = (vb4 >> 14) & 0x3fc, d = (vb4 >> 22) & 0x3fc;
      const s32 h = quad_height(bg, verts, (vb4 & 0xff) << 2 | b << 16, c | d << 16);
      if (h != -0x8000) return finish(h, 4, va, verts + b * 2, verts + d * 2, verts + c * 2, index + k);
    }
    index += ng;
    // H (0x14) is not walkable.
    const int nh = read_u16(hdr + 0x10);
    rec += nh * 0x14;
    index += nh;
    // I (0x10): vertex bytes at +0xc.
    const int ni = read_u16(hdr + 0x12);
    for (int k = 0; k < ni; ++k, rec += 0x10) {
      const u32 vb4 = ru32(rec + 0xc);
      const u8* va = verts + (vb4 & 0xff) * 8;
      if (!inside(va)) continue;
      const u32 b = (vb4 >> 6) & 0x3fc, c = (vb4 >> 14) & 0x3fc, d = (vb4 >> 22) & 0x3fc;
      const s32 h = quad_height(bg, verts, (vb4 & 0xff) << 2 | b << 16, c | d << 16);
      if (h != -0x8000) return finish(h, 4, va, verts + b * 2, verts + d * 2, verts + c * 2, index + k);
    }
    index += ni;
    // J (0x18, semi-transparent triangles): as A.
    const int nj = read_u16(hdr + 0x14);
    for (int k = 0; k < nj; ++k, rec += 0x18) {
      const u32 ab = ru32(rec + 0x14);
      const u8* va = verts + (ab & 0xffff) * 2;
      if (!inside(va)) continue;
      const u32 c = read_u16(rec + 6);
      const s32 h = tri_height(bg, verts, ab & 0xffff, ab >> 16, c);
      if (h != -0x8000) return finish(h, 3, va, verts + (ab >> 16) * 2, verts + c * 2, nullptr, index + k);
    }
    index += nj;
    // K (0x20, semi-transparent quads): a | b << 16 at +0x18, c | d << 16 at +0x1c.
    const int nk = read_u16(hdr + 0x16);
    for (int k = 0; k < nk; ++k, rec += 0x20) {
      const u32 ab = ru32(rec + 0x18), cd = ru32(rec + 0x1c);
      const u8* va = verts + (ab & 0xffff) * 2;
      if (!inside(va)) continue;
      const s32 h = quad_height(bg, verts, ab, cd);
      if (h != -0x8000)
        return finish(h, 4, va, verts + (ab >> 16) * 2, verts + (cd >> 16) * 2, verts + (cd & 0xffff) * 2, index + k);
    }
  }
  return -0x8000;
}

// Wraps a 20.12 coordinate onto the torus.
s32 wrap(s32 v, s32 size) {
  if (v < 0) return v + size;
  if (v >= size) return v - size;
  return v;
}

}  // namespace

s32 world_terrain_query(WorldBg* bg, s32 x, s32 z, TerrainHit* hit) {
  FieldGlobals& g = fg();
  bool outside = false;  // map 2: outside the playable disc (type 0xf, not walkable)
  if (world_map() == 2) {
    const s32 dx = (x >> 12) - 67000, dz = (z >> 12) - 40000;
    if ((std::abs(dx) < 0x5cf8 || std::abs(dz) < 0x5cf8) && world_sqrt(dx * dx + dz * dz) < 0x5cf8) {
      outside = false;
    } else {
      outside = true;
    }
    if ((g.script_flags[1] >> 4 & 1) != 0) {
      const s32 ex = (x >> 12) - 0x13023, ez = (z >> 12) - 0xd4b5;
      if (!(std::abs(ex) > 7999 && std::abs(ez) > 7999) && world_sqrt(ex * ex + ez * ez) < 8000) outside = true;
    }
  }
  hit->corners = 0;
  x = wrap(x, 0x1b000000);
  z = wrap(z, 0x15000000);
  hit->off_map = 0;
  const s32 sx = x >> 31, sz = z >> 31;
  const s32 bx = (x >> 12) / 0x3000 + sx;
  const s32 bz = (z >> 12) / 0x3000 + sz;
  s32 qx = 0, qz = 0;
  for (int attempt = 0; attempt < 4; ++attempt) {
    const s32 lx = (x >> 12) - (bx - sx) * 0x3000;
    const s32 lz = (z >> 12) - (bz - sz) * 0x3000;
    // Attempt 0: the point; 1: the neighbouring sub-cell across x; 2: across z; 3: both.
    if (attempt == 0) {
      qx = x;
      qz = z;
    } else if (attempt == 1) {
      qx = x + ((lx % 0xc00) > 0x5ff ? 0xc00000 : -0xc00000);
      qz = z;
    } else if (attempt == 2) {
      qx = x;
      qz = z + ((lz % 0xc00) < 0x600 ? -0xc00000 : 0xc00000);
    } else {
      qx = x + ((lx % 0xc00) > 0x5ff ? 0xc00000 : -0xc00000);
      qz = z + ((lz % 0xc00) > 0x5ff ? 0xc00000 : -0xc00000);
    }
    const s32 cbx = (qx >> 12) / 0x3000, cbz = (qz >> 12) / 0x3000;
    const int slot = world_block_slot(bg, cbx + cbz * 9);
    if (slot == -1) {
      hit->off_map = 1;
      continue;
    }
    const s32 mx = ((qx >> 12) % 0x3000) / 0xc00, mz = ((qz >> 12) % 0x3000) / 0xc00;
    const u16 mask = static_cast<u16>(1u << (15 - (mx + mz * 4)));
    const u8* blk = g_world.blocks.data() + slot * kSlotBytes;
    const s32 h = block_query(bg, blk, mask, lx + ((bx - sx) - cbx) * 0x3000 - 0x1800,
                              lz + ((bz - sz) - cbz) * 0x3000 - 0x1800, hit);
    if (h == -0x8000) continue;
    hit->base_x = cbx * 0x3000;
    hit->base_z = cbz * 0x3000;
    if ((g.script_flags[1] >> 6 & 1) != 0 && (hit->attr & 8) == 0) hit->attr = static_cast<u8>(hit->attr << 4 | 1);
    if (outside) hit->attr = static_cast<u8>((hit->attr & 0xfe) | 0xf0);
    return h;
  }
  return -0x8000;
}

// 0x8008e750: height from the block's coarse 33 x 33 grid (bytes * -10), for the camera.
s32 world_terrain_camera_floor(WorldBg* bg, s32 x, s32 z) {
  x = wrap(x, 0x1b000000);
  z = wrap(z, 0x15000000);
  const int slot = world_block_slot(bg, (x >> 12) / 0x3000 + ((z >> 12) / 0x3000) * 9);
  if (slot == -1) return 0;
  const s32 lx = (x >> 12) % 0x3000, lz = (z >> 12) % 0x3000;
  const u8* blk = g_world.blocks.data() + slot * kSlotBytes;
  const s32 fx = lx % 0x180, fz = lz % 0x180;
  const u8* p = blk + rd32(blk + 4) + (lz / 0x180) * 0x21 + lx / 0x180;
  if (p[0] == 0 && p[1] == 0 && p[0x21] == 0 && p[0x22] == 0) return 0;
  const u32 sum = static_cast<u32>(p[0]) * 10 * (0x180 - fx) * (0x180 - fz) + static_cast<u32>(p[1]) * 10 * fx * (0x180 - fz) +
                  static_cast<u32>(p[0x21]) * 10 * (0x180 - fx) * fz + static_cast<u32>(p[0x22]) * 10 * fx * fz;
  return -static_cast<s32>(sum / 0x24000);
}

}  // namespace ss::game::field
