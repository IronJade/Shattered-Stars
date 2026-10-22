// Field actors (game/field/actor_*, collision, camera): the command ring and its
// interpreter (waits, moves, turns, fades), the handoff-0xb state snapshot,
// actor-to-actor collision and the floor / region point tests. The numbers follow
// the original's arithmetic (actor_update 0x8003d95c, fieldactor_move_step
// 0x80081cc8); walking on map 7 is compared with PCSX-Redux traces of the running
// game (SS_ACTOR_TRACE prints the same fields). Moves and distances use the field
// program's SquareRoot0 / ratan2 tables, read from the disc: those tests need SS_DISC1.
// The shading regions (fieldactor_build_light_split 0x80083d70) are checked on built
// polygons, and, with SS_DISC1 and the PCSX-Redux logs in analysis/reference/shade (or
// SS_SHADE_REF=DIR; see the README there), call by call against the original on map 18.

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "disc/archive.h"
#include "disc/disc_image.h"
#include "disc/slz.h"
#include "game/boot/system.h"

#include "game/field/actor_base.h"
#include "game/field/actor_field.h"
#include "game/field/actors.h"
#include "game/field/afterimage.h"
#include "game/field/bg.h"
#include "game/field/collision.h"
#include "game/field/tasks.h"
#include "game/field/field.h"
#include "game/field/map.h"
#include "game/field/screen.h"
#include "game/field/script_vm.h"
#include "runtime/libgpu.h"
#include "test.h"

using namespace ss;
using namespace ss::game;
using namespace ss::game::field;

namespace {

// The field program image (SquareRoot0's table 0x80077bac, ratan2's 0x8007803c), from
// the disc another test opened or SS_DISC1.
bool field_tables() {
  constexpr u32 kSqrtTable = 0x80077BAC;
  if (field_data(kSqrtTable) != nullptr) return true;
  static std::unique_ptr<disc::DiscImage> image;
  static disc::Archive archive;
  static bool tried = false;
  if (tried) return false;
  tried = true;
  if (get_system()->loader.archive() == nullptr) {
    const char* path = std::getenv("SS_DISC1");
    if (path == nullptr || *path == 0) return false;
    std::string error;
    image = disc::DiscImage::open(path, &error);
    if (!image || !archive.open(*image, &error)) {
      std::fprintf(stderr, "  cannot open %s: %s\n", path, error.c_str());
      return false;
    }
    boot_main(0, &archive);
  }
  return field_data(kSqrtTable) != nullptr;
}

#define NEEDS_FIELD_TABLES()                                   \
  do {                                                         \
    if (!field_tables()) {                                     \
      std::printf("  (skipped: SS_DISC1 not set)\n");          \
      return;                                                  \
    }                                                          \
  } while (0)

// A field-class actor at (x, 0, z) map units, facing 0, not drawn, with no floor zones.
struct TestActor {
  Actor actor;
  explicit TestActor(s32 x = 0, s32 z = 0, u32 flags = 0) {
    globals().frame_ticks = 1;
    map_data().actor_scale = 100;
    fieldactor_construct(&actor);
    const s32 pos[3] = {x, 0, z};
    actor_init(&actor, 0, pos, 0, 0, 0, -1, flags);
  }
  Actor* operator->() { return &actor; }
  Actor* get() { return &actor; }
  // Runs actor_update (no player control, not the camera target) until 'done' or 'limit' frames.
  template <typename Done>
  int run_until(Done done, int limit = 1000) {
    for (int frame = 1; frame <= limit; ++frame) {
      actor_update(&actor, 0, 0);
      if (done()) return frame;
    }
    return -1;
  }
};

std::vector<u16> queued(const Actor* a) {
  std::vector<u16> out;
  for (u32 r = a->cmd_read; r != a->cmd_write; r = (r + 1) & 0xFF) out.push_back(a->cmd[r]);
  return out;
}

void put16(u8* p, s32 v) {
  const u16 u = static_cast<u16>(v);
  std::memcpy(p, &u, 2);
}

}  // namespace

TEST(field_actor_pushers_encode_commands) {
  TestActor a;
  actor_cmd_wait(a.get(), 30);
  actor_cmd_turn_to(a.get(), 90, 10, 0);
  actor_cmd_move_to_far(a.get(), 0x12345, -2, 7, 3);
  actor_queue_fade_colour(a.get(), 0x40, 0x50, 0x60, 8, 1);
  const std::vector<u16> expected = {kActorCmdWait,     30,     kActorCmdTurnTo, 90,   10,   0,
                                     kActorCmdMoveToFar, 0x1,    0x2345,          0xFFFF, 0xFFFE, 0,
                                     7,                  3,      kActorCmdFade,   0x40, 0x50, 0x60,
                                     8,                  1};
  CHECK(queued(a.get()) == expected);
  CHECK(actor_is_busy(a.get()));
}

TEST(field_actor_ring_rejects_commands_that_do_not_fit) {
  TestActor a;
  // The ring holds 255 words: a command is dropped whole when its words would reach the reader.
  a->cmd_read = 0x10;
  a->cmd_write = 0x0C;
  CHECK(actor_cmd_has_room(a.get(), 3));
  CHECK(!actor_cmd_has_room(a.get(), 4));
  actor_cmd_turn_to(a.get(), 1, 1, 0);  // 4 words: dropped
  CHECK_EQ(a->cmd_write, 0x0C);
  actor_cmd_wait(a.get(), 5);  // 2 words
  CHECK_EQ(a->cmd_write, 0x0E);
  CHECK_EQ(a->cmd[0x0C], kActorCmdWait);
  // Wrapping past the end of the ring.
  a->cmd_read = 0x02;
  a->cmd_write = 0xFE;
  CHECK(actor_cmd_has_room(a.get(), 3));
  CHECK(!actor_cmd_has_room(a.get(), 4));
  actor_cmd_set_anim(a.get(), 7);
  CHECK_EQ(a->cmd_write, 0x00);
  CHECK_EQ(a->cmd[0xFF], 7);
}

TEST(field_actor_wait_then_set_animation) {
  TestActor a;
  actor_cmd_wait(a.get(), 3);
  actor_cmd_set_anim(a.get(), 0x30);
  // The wait counts down once per frame and the next command runs on the frame it ends.
  CHECK_EQ(a.run_until([&] { return a->anim == 0x30; }), 4);
  CHECK_EQ(a->wait_state, 0);
  CHECK(!actor_is_busy(a.get()));
}

TEST(field_actor_walks_to_target_and_waits) {
  NEEDS_FIELD_TABLES();
  TestActor a(0, 0);
  // Speed 2 = 2 map units per vsync at scale 100: 100 units take 50 frames, the last
  // step snapping onto the target.
  actor_cmd_move_to_xz(a.get(), 100, 0, 2);
  actor_cmd_wait_move(a.get());
  actor_cmd_set_anim(a.get(), 0x25);
  CHECK_EQ(a.run_until([&] { return a->move_mode == 0; }), 50);
  CHECK_EQ(a->x, 100 << 12);
  CHECK_EQ(a->z, 0);
  CHECK_EQ(a->speed, 0x2000);
  CHECK_EQ(a->angle, 0);
  CHECK_EQ(a->walked, 0);
  // The wait ends on the next frame.
  CHECK(a->anim != 0x25);
  actor_update(a.get(), 0, 0);
  CHECK_EQ(a->anim, 0x25);
  CHECK_EQ(a->wait_state, 0);
}

TEST(field_actor_moves_diagonally_by_heading) {
  NEEDS_FIELD_TABLES();
  TestActor a(0, 0);
  // Negative speed = frames: the speed is the distance / frames (in 1/256 units).
  actor_cmd_move_by_xz(a.get(), 30, 40, -10);
  actor_update(a.get(), 0, 0);
  CHECK_EQ(a->speed, (SquareRoot0(800 * 800) << 8) / 10);  // the table's root of 640000 is 799
  CHECK_EQ(a->heading, static_cast<s16>(ratan2(40 << 12, 30 << 12)));
  CHECK_EQ(a->angle, a->heading);
  const int frames = a.run_until([&] { return a->move_mode == 0; });
  CHECK(frames >= 8 && frames <= 10);  // 10 frames in all, the first one above
  CHECK_EQ(a->x, 30 << 12);
  CHECK_EQ(a->z, 40 << 12);
}

TEST(field_actor_turns_in_steps) {
  TestActor a;
  // 90 degrees at 10 degrees per frame: 0x400 / 0x71 = 9 frames, landing on the target.
  actor_cmd_turn_to(a.get(), 90, 10, 0);
  actor_cmd_wait_turn(a.get());
  actor_cmd_set_anim(a.get(), 0x26);
  actor_update(a.get(), 0, 0);
  CHECK_EQ(a->turn_frames, 8);
  CHECK_EQ(a->turn_step, 0x71);
  CHECK_EQ(a->angle, 0x71);
  CHECK_EQ(a.run_until([&] { return a->turning == 0; }), 8);
  CHECK_EQ(a->angle, 0x400);
  actor_update(a.get(), 0, 0);
  CHECK_EQ(a->anim, 0x26);
  // The short way round: from 0x400 to -170 degrees turns clockwise through 0x800.
  actor_cmd_turn_to(a.get(), -170, 20, 0);
  actor_update(a.get(), 0, 0);
  CHECK(a->turn_step > 0);
  a.run_until([&] { return a->turning == 0; });
  CHECK_EQ(a->angle, static_cast<s16>((-170 << 12) / 360));
}

TEST(field_actor_fades_colour) {
  TestActor a;
  actor_queue_fade_colour(a.get(), 0, 0x40, 0xFF, 4, 2);
  actor_queue_wait_fade(a.get());
  actor_cmd_set_anim(a.get(), 0x27);
  actor_update(a.get(), 0, 0);
  // fade_step runs in the frame the fade starts: 1/4 of the way.
  CHECK_EQ(a->rgb[0], 0x80 - 0x20);
  CHECK_EQ(a->rgb[1], 0x80 - 0x10);
  CHECK_EQ(a->rgb[2], 0x80 + 0x1F);
  CHECK_EQ(a->blend, 2);
  CHECK_EQ(a.run_until([&] { return a->fading == 0; }), 3);
  CHECK_EQ(a->rgb[0], 0);
  CHECK_EQ(a->rgb[1], 0x40);
  CHECK_EQ(a->rgb[2], 0xFF);
  actor_update(a.get(), 0, 0);
  CHECK_EQ(a->anim, 0x27);
}

TEST(field_actor_state_snapshot_round_trip) {
  TestActor a(12, -34);
  a->angle = 0x123;
  a->anim = 5;
  a->cmd_read = 0xFC;  // a queue that wraps
  a->cmd_write = 0xFC;
  actor_cmd_move_to_xz(a.get(), 1, 2, 3);
  actor_cmd_wait_move(a.get());
  const int size = actor_state_size(a.get());
  CHECK_EQ(size, 0xA6 + 5 * 2);
  std::vector<u8> snap(static_cast<size_t>(size));
  actor_save_state(a.get(), snap.data());

  TestActor b;
  actor_load_state(b.get(), snap.data(), 0);
  CHECK_EQ(b->x, 12 << 12);
  CHECK_EQ(b->z, -34 << 12);
  CHECK_EQ(b->angle, 0x123);
  CHECK_EQ(b->anim, 5);
  CHECK_EQ(b->cmd_read, 0);
  CHECK_EQ(b->cmd_write, 5);
  CHECK(queued(b.get()) == queued(a.get()));
  CHECK(b->vtbl == &kFieldActorVtable);  // the snapshot stops before the host part
}

TEST(field_actor_collision_with_solid_actors) {
  NEEDS_FIELD_TABLES();
  Actor** table = field_actors();
  TestActor a(-200, 0, kActorBlockedByActors), b(0, 0, kActorSolid);
  CHECK_EQ(a->collide_radius, 90);  // 0x18 * scale * 0x5a / 0x960
  table[1] = a.get();
  table[2] = b.get();
  const s32 near[3] = {190 << 12, 0, 0};
  const s32 far[3] = {100 << 12, 0, 0};
  CHECK(actor_collides(a.get(), near, 0));   // 10 units apart: within (90 + 90) / 2
  CHECK(!actor_collides(a.get(), far, 0));   // 100 units apart
  b->flags &= ~kActorSolid;
  CHECK(!actor_collides(a.get(), near, 0));
  b->flags |= kActorSolid;
  // A walk through the solid actor stops at the last step that stays clear of it.
  actor_cmd_move_to_xz(a.get(), 200, 0, 1);
  a.run_until([&] { return false; }, 300);
  CHECK(a->move_mode != 0);
  CHECK_EQ(a->x, -91 << 12);
  CHECK_EQ(a->walked, 0);
  table[1] = table[2] = nullptr;
}

TEST(field_walk_point_in_floor_triangle) {
  // A floor zone (cell 0 list 14): y range, x / z box, then the vertices {x, y, z, pad}.
  u8 item[0x58] = {};
  put16(item + 0, -10);
  put16(item + 2, 10);
  put16(item + 4, 0);
  put16(item + 6, 0);
  put16(item + 8, 100);
  put16(item + 0xA, 100);
  item[0xC] = 1;  // enabled
  const s16 verts[3][2] = {{0, 0}, {0, 100}, {100, 0}};
  for (int i = 0; i < 3; ++i) {
    put16(item + 0x10 + i * 8, verts[i][0]);
    put16(item + 0x14 + i * 8, verts[i][1]);
  }
  TestActor a;
  auto at = [&](s32 x, s32 y, s32 z) {
    const s32 pos[3] = {x << 12, y << 12, z << 12};
    return walk_point_in_tri(a.get(), item, pos);
  };
  CHECK(at(10, 0, 10));
  CHECK(at(49, 0, 50));
  CHECK(!at(60, 0, 60));   // past the diagonal
  CHECK(!at(10, 20, 10));  // above the zone's height range
  CHECK(!at(-1, 0, 10));
  item[0xC] = 0;  // switched off (walk_set_group_enabled)
  CHECK(!at(10, 0, 10));
}

TEST(field_zone_test_point_shapes) {
  // A screen-space region: [type, r, g, b], y range, box, enabled, then {x, y} vertices.
  u8 item[0x24] = {};
  auto setup = [&](u8 type) {
    std::memset(item, 0, sizeof item);
    item[0] = type;
    put16(item + 4, -100);
    put16(item + 6, 100);
    put16(item + 8, 0);
    put16(item + 0xA, 0);
    put16(item + 0xC, 64);
    put16(item + 0xE, 64);
    item[0x10] = 1;
  };
  TestActor a;
  auto at = [&](s16 x, s16 y) {
    const s16 xy[2] = {x, y};
    return zone_test_point(a.get(), item, xy, 0);
  };
  setup(0);  // triangle (0,0) (0,64) (64,0): every edge function <= 0 inside
  const s16 tri[3][2] = {{0, 0}, {0, 64}, {64, 0}};
  for (int i = 0; i < 3; ++i) {
    put16(item + 0x14 + i * 4, tri[i][0]);
    put16(item + 0x16 + i * 4, tri[i][1]);
  }
  CHECK(at(10, 10));
  CHECK(!at(50, 50));
  setup(2);  // the box
  CHECK(at(50, 50));
  CHECK(!at(65, 10));
  if (!field_tables()) return;  // the ellipse uses SquareRoot0
  setup(3);  // ellipse centred (32, 32), 32 x 16
  put16(item + 0x14, 32);
  put16(item + 0x16, 32);
  put16(item + 0x18, 32);
  put16(item + 0x1A, 16);
  CHECK(at(32, 32));
  CHECK(at(60, 32));
  CHECK(at(40, 46));
  CHECK(!at(60, 46));
  CHECK(!at(60, 44));
  // (Rows past the ellipse's height are left to the box: the original takes the
  // square root of a negative number there.)
  {
    const s16 xy[2] = {32, 32};
    CHECK(!zone_test_point(a.get(), item, xy, 200));  // outside the height range
  }
}

// --- shading regions (fieldactor_build_light_split 0x80083d70) ---

namespace {

// The field screen with its primitive arena (the pieces come from field_screen()->alloc).
void shade_screen() {
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
}

void put32(u8* p, s32 v) {
  const u32 u = static_cast<u32>(v);
  std::memcpy(p, &u, 4);
}

// A scene swapped into map_data() (cell grid at scene + [0x80]) and a BG renderer for
// the view origin; both restored afterwards.
struct ShadeWorld {
  std::vector<u8> saved_scene;
  u8* saved_cells = nullptr;
  u8* saved_table = nullptr;
  bool made_bg = false;
  s16 saved_view[4] = {};

  explicit ShadeWorld(std::vector<u8> scene) {
    MapData& m = map_data();
    saved_scene = std::move(m.scene);
    saved_cells = m.cells;
    saved_table = m.cell_table;
    m.scene = std::move(scene);
    m.cells = m.scene.data() + read_u32(m.scene.data() + 0x80);
    m.cell_table = m.cells + 4;
    made_bg = bg_renderer() == nullptr;
    if (made_bg) bg_construct();
    BgRenderer* bg = bg_renderer();
    saved_view[0] = bg->scroll_x, saved_view[1] = bg->scroll_y;
    saved_view[2] = bg->offset_x, saved_view[3] = bg->offset_y;
    shade_screen();
  }
  ~ShadeWorld() {
    BgRenderer* bg = bg_renderer();
    bg->scroll_x = saved_view[0], bg->scroll_y = saved_view[1];
    bg->offset_x = saved_view[2], bg->offset_y = saved_view[3];
    if (made_bg) bg_destroy();
    MapData& m = map_data();
    m.scene = std::move(saved_scene);
    m.cells = saved_cells;
    m.cell_table = saved_table;
  }
  // Scrolls so that bg_get_view_origin reports (x, y).
  void set_view(s16 x, s16 y) {
    BgRenderer* bg = bg_renderer();
    const u8* scene = map_data().scene.data();
    bg->offset_x = bg->offset_y = 0;
    bg->scroll_x = static_cast<s16>(x - static_cast<s16>(read_u32(scene + 0x28)));
    bg->scroll_y = static_cast<s16>(y - static_cast<s16>(read_u32(scene + 0x2C)));
  }
};

// A light polygon of cell lists 4 / 6 (0x4c bytes) from its corners in BG pixels,
// clockwise on screen: the box, the corners closed by the first, each edge's line
// a x + b y + c = 0, and +0x48 (drawn only with the colour of the region underfoot).
std::vector<u8> light_poly(const std::vector<std::pair<s16, s16>>& pts, u8 r, u8 g, u8 b, u8 same_colour) {
  std::vector<u8> p(0x4C, 0);
  const int n = static_cast<int>(pts.size());
  p[0] = n == 3 ? 0 : 1;
  p[1] = r, p[2] = g, p[3] = b;
  put16(p.data() + 4, -1000);
  put16(p.data() + 6, 1000);
  s16 x0 = pts[0].first, x1 = x0, y0 = pts[0].second, y1 = y0;
  for (const auto& [x, y] : pts) {
    x0 = std::min(x0, x), x1 = std::max(x1, x), y0 = std::min(y0, y), y1 = std::max(y1, y);
  }
  put16(p.data() + 8, x0);
  put16(p.data() + 0xA, y0);
  put16(p.data() + 0xC, x1);
  put16(p.data() + 0xE, y1);
  p[0x10] = 1;
  for (int k = 0; k <= n; ++k) {
    put16(p.data() + 0x14 + k * 4, pts[k % n].first);
    put16(p.data() + 0x16 + k * 4, pts[k % n].second);
  }
  for (int e = 0; e < n; ++e) {
    const auto [px, py] = pts[e];
    const auto [qx, qy] = pts[(e + 1) % n];
    const s32 a = qy - py, bb = px - qx;
    put16(p.data() + 0x28 + e * 2, a);
    put16(p.data() + 0x30 + e * 2, bb);
    put32(p.data() + 0x38 + e * 4, -(a * px + bb * py));
  }
  p[0x48] = same_colour;
  return p;
}

// A scene with its BG origin at (ox, oy), ambient 0x80, and a 4 x 4 grid of 128-pixel
// cells whose lists 6 and 4 all hold the given polygons.
std::vector<u8> light_scene(s32 ox, s32 oy, const std::vector<std::vector<u8>>& list6,
                            const std::vector<std::vector<u8>>& list4) {
  std::vector<u8> scene(0x2000, 0);
  put32(scene.data() + 0x28, ox);
  put32(scene.data() + 0x2C, oy);
  scene[0x6C] = scene[0x6D] = scene[0x6E] = 0x80;
  put32(scene.data() + 0x80, 0x100);
  u8* cells = scene.data() + 0x100;
  cells[0] = cells[1] = 4;
  const u32 items = 0x700;
  for (int c = 0; c < 16; ++c) {
    u8* cell = cells + 4 + c * 0x60;
    put16(cell + 6 * 2, static_cast<s32>(list6.size()));
    put32(cell + 0x20 + 6 * 4, static_cast<s32>(items));
    put16(cell + 4 * 2, static_cast<s32>(list4.size()));
    put32(cell + 0x20 + 4 * 4, static_cast<s32>(items + 0x4C * list6.size()));
  }
  u8* out = cells + items;
  for (const auto& p : list6) out = std::copy(p.begin(), p.end(), out);
  for (const auto& p : list4) out = std::copy(p.begin(), p.end(), out);
  return scene;
}

// A shaded actor whose sprite quad (screen pixels) is (100, 50) - (132, 100), already
// holding two primitives, and its main sprite: colour 0x80, texture (u0, v0) - (u0 + du,
// v0 + dv), tpage 0x105, CLUT 0x7fcd.
struct ShadeActor {
  Actor actor;
  psyq::POLY_FT4 prim{};
  ShadeActor(u8 u0, u8 v0, u8 du, u8 dv) {
    actor.flags = kActorShaded;
    actor.layer = 5;
    actor.prim_count = 2;
    const s16 quad[8] = {100, 50, 132, 50, 100, 100, 132, 100};
    std::memcpy(actor.quad, quad, sizeof quad);
    prim.r0 = prim.g0 = prim.b0 = 0x80;
    prim.code = 0x2C;
    prim.u0 = u0, prim.v0 = v0, prim.u1 = static_cast<u8>(u0 + du), prim.v1 = v0;
    prim.u2 = u0, prim.v2 = static_cast<u8>(v0 + dv), prim.u3 = prim.u1, prim.v3 = prim.v2;
    prim.tpage = 0x105;
    prim.clut = 0x7FCD;
  }
};

}  // namespace

TEST(field_actor_light_split_shade_under_the_feet) {
  // The view origin is (552, 16) in a scene whose BG starts at x 512, so the sprite
  // covers BG (652, 66) - (684, 116), all in cell 6; its feet probe is (668, 116). A
  // list-6 rectangle (642, 91) - (702, 146), 0x404040, covers the lower half.
  const auto shade = light_poly({{642, 91}, {642, 146}, {702, 146}, {702, 91}}, 0x40, 0x40, 0x40, 1);
  ShadeWorld world(light_scene(512, 0, {shade}, {}));
  world.set_view(552, 16);
  ShadeActor a(0, 0, 31, 49);
  fieldactor_build_light_split(&a.actor, 32, 50, &a.prim, 0, 1);
  CHECK_EQ(a.actor.prim_count, 3);
  if (a.actor.prim_count != 3) return;
  CHECK_EQ(a.actor.prim_ot[2], 6);  // layer + 1
  auto* p = reinterpret_cast<psyq::POLY_FT4*>(a.actor.prims[2]);
  CHECK_EQ(p->tag.len, 9u);
  CHECK_EQ(p->code, 0x2C);
  CHECK_EQ(p->r0, 0x40);  // 0x80 + 0x40 - ambient 0x80
  CHECK_EQ(p->b0, 0x40);
  // The clipped rectangle (100, 75) - (132, 100) as a fan from its bottom-left corner:
  // FT4 corners 0, 1, 3, 2 = fan vertices 0, 1, 2, 3.
  CHECK_EQ(p->x0, 100); CHECK_EQ(p->y0, 100);
  CHECK_EQ(p->x1, 132); CHECK_EQ(p->y1, 100);
  CHECK_EQ(p->x2, 100); CHECK_EQ(p->y2, 75);
  CHECK_EQ(p->x3, 132); CHECK_EQ(p->y3, 75);
  // Prescaled texture: texels = pixels from the box's top-left.
  CHECK_EQ(p->u0, 0); CHECK_EQ(p->v0, 50);
  CHECK_EQ(p->u1, 32); CHECK_EQ(p->v1, 50);
  CHECK_EQ(p->u2, 0); CHECK_EQ(p->v2, 25);
  CHECK_EQ(p->u3, 32); CHECK_EQ(p->v3, 25);
  CHECK_EQ(p->tpage, 0x105);
  CHECK_EQ(p->clut, 0x7FCD);
  CHECK_EQ(fieldactor_globals().shade_colour[0], 0x40);

  // A full actor (22 primitives) gets no pieces; nor one standing outside the region.
  ShadeActor full(0, 0, 31, 49);
  full.actor.prim_count = 22;
  fieldactor_build_light_split(&full.actor, 32, 50, &full.prim, 0, 1);
  CHECK_EQ(full.actor.prim_count, 22);
  ShadeActor away(0, 0, 31, 49);
  for (int k = 0; k < 4; ++k) away.actor.quad[k * 2 + 1] = static_cast<s16>(away.actor.quad[k * 2 + 1] - 60);
  fieldactor_build_light_split(&away.actor, 32, 50, &away.prim, 0, 1);
  CHECK_EQ(away.actor.prim_count, 2);
}

TEST(field_actor_light_split_list4_triangle) {
  // No list-6 region: a list-4 triangle (652, 76) (652, 106) (682, 106), colour a0 90 80,
  // lies inside the sprite's box and is drawn whole as one FT3.
  const auto tri = light_poly({{652, 76}, {652, 106}, {682, 106}}, 0xA0, 0x90, 0x80, 0);
  ShadeWorld world(light_scene(512, 0, {}, {tri}));
  world.set_view(552, 16);
  {
    // Unscaled (cache B) and mirrored, semi-transparent: texels scale by 31 / 32 and
    // 49 / 50 from the box's right edge and top.
    ShadeActor a(64, 10, 31, 49);
    a.actor.blend = 2;
    fieldactor_build_light_split(&a.actor, 32, 50, &a.prim, 2, 0);
    CHECK_EQ(a.actor.prim_count, 3);
    if (a.actor.prim_count != 3) return;
    auto* t = reinterpret_cast<psyq::POLY_FT3*>(a.actor.prims[2]);
    CHECK_EQ(t->tag.len, 7u);
    CHECK_EQ(t->code, 0x26);
    CHECK_EQ(t->r0, 0xA0); CHECK_EQ(t->g0, 0x90); CHECK_EQ(t->b0, 0x80);
    CHECK_EQ(t->x0, 130); CHECK_EQ(t->y0, 90);
    CHECK_EQ(t->x1, 100); CHECK_EQ(t->y1, 60);
    CHECK_EQ(t->x2, 100); CHECK_EQ(t->y2, 90);
    CHECK_EQ(t->u0, 65); CHECK_EQ(t->v0, 49);  // 64 + 31 * 2 / 32, 10 + 49 * 40 / 50
    CHECK_EQ(t->u1, 95); CHECK_EQ(t->v1, 19);
    CHECK_EQ(t->u2, 95); CHECK_EQ(t->v2, 49);
  }
  {
    // Upright, unscaled: the original divides the base in too ((u0 + du * dx) / w).
    ShadeActor a(64, 10, 31, 49);
    fieldactor_build_light_split(&a.actor, 32, 50, &a.prim, 0, 0);
    CHECK_EQ(a.actor.prim_count, 3);
    if (a.actor.prim_count != 3) return;
    auto* t = reinterpret_cast<psyq::POLY_FT3*>(a.actor.prims[2]);
    CHECK_EQ(t->code, 0x24);
    CHECK_EQ(t->u0, 31); CHECK_EQ(t->v0, 39);  // (64 + 31 * 30) / 32, (10 + 49 * 40) / 50
    CHECK_EQ(t->u1, 2); CHECK_EQ(t->v1, 10);
    CHECK_EQ(t->u2, 2); CHECK_EQ(t->v2, 39);
  }
}

namespace {

// Map 'map''s scene (archive entry 0xc87 + map, section 0, SLZ block 0), or empty.
std::vector<u8> map_scene_from_disc(int map) {
  disc::Archive* archive = get_system()->loader.archive();
  if (archive == nullptr) return {};
  const u32 entry = static_cast<u32>(0xC87 + map);
  if (!archive->has_entry(entry)) return {};
  auto data = archive->read_entry(entry);
  if (!data || data->size() < 4) return {};
  const u32 count = read_u32(data->data());
  for (u32 i = 0; i < count && 12 + 8 * i <= data->size(); ++i) {
    if (read_u32(data->data() + 4 + 8 * i) != 0) continue;
    auto scene = disc::slz::unpack_block(*data, read_u32(data->data() + 8 + 8 * i), 0, nullptr);
    return scene ? std::move(*scene) : std::vector<u8>{};
  }
  return {};
}

std::vector<u8> unhex(const std::string& s) {
  std::vector<u8> out(s.size() / 2);
  for (size_t i = 0; i < out.size(); ++i) out[i] = static_cast<u8>(std::stoul(s.substr(i * 2, 2), nullptr, 16));
  return out;
}

std::filesystem::path shade_reference_dir() {
  if (const char* d = std::getenv("SS_SHADE_REF"); d && *d) return d;
  for (std::filesystem::path p = std::filesystem::current_path(); !p.empty(); p = p.parent_path()) {
    const auto candidate = p / "analysis" / "reference" / "shade";
    if (std::filesystem::is_directory(candidate)) return candidate;
    if (p == p.parent_path()) break;
  }
  return {};
}

}  // namespace

TEST(field_actor_light_split_matches_redux) {
  // Each log line is one call of 0x80083d70 in PCSX-Redux (shade_probe.lua): the actor
  // fields it reads, its arguments, the main sprite primitive, the view origin, the
  // stack colour slot, and the pieces it added (OT index, primitive bytes after the tag).
  const auto dir = shade_reference_dir();
  if (dir.empty()) {
    std::printf("  (skipped: no analysis/reference/shade)\n");
    return;
  }
  NEEDS_FIELD_TABLES();
  std::map<int, std::vector<u8>> scenes;
  int calls = 0, pieces = 0, bad = 0;
  for (const auto& file : std::filesystem::directory_iterator(dir)) {
    if (file.path().extension() != ".log") continue;
    std::ifstream in(file.path());
    bool dup46 = false;
    std::string line;
    while (std::getline(in, line)) {
      if (line.rfind("# options", 0) == 0) dup46 = line.find("dup46") != std::string::npos;
      if (line.rfind("call ", 0) != 0) continue;
      std::istringstream ss(line);
      std::vector<std::string> t;
      for (std::string w; ss >> w;) t.push_back(w);
      auto at = [&](const char* key, int skip = 1) -> const std::string& {
        for (size_t i = 0; i + skip < t.size(); ++i)
          if (t[i] == key) return t[i + skip];
        static const std::string none = "0";
        return none;
      };
      const int map = std::stoi(at("map"));
      auto& scene = scenes[map];
      if (scene.empty()) scene = map_scene_from_disc(map);
      if (scene.empty()) {
        std::printf("  (skipped: no scene for map %d)\n", map);
        return;
      }
      std::vector<u8> s = scene;
      if (dup46) {
        // The run copied list 6 into list 4 in every cell.
        u8* cells = s.data() + read_u32(s.data() + 0x80);
        for (int c = 0; c < cells[0] * cells[1]; ++c) {
          u8* cell = cells + 4 + c * 0x60;
          put16(cell + 8, read_u16(cell + 0xC));
          put32(cell + 0x30, static_cast<s32>(read_u32(cell + 0x38)));
        }
      }
      ShadeWorld world(std::move(s));
      world.set_view(static_cast<s16>(std::stoi(at("cam"))), static_cast<s16>(std::stoi(at("cam", 2))));
      const auto stack = unhex(at("stack"));
      std::memcpy(fieldactor_globals().shade_colour, stack.data(), 3);
      Actor a;
      a.flags = static_cast<u32>(std::stoul(at("flags"), nullptr, 16));
      a.y = std::stoi(at("y"));
      a.layer = static_cast<u16>(std::stoi(at("layer")));
      a.blend = static_cast<u8>(std::stoi(at("blend")));
      a.prim_count = std::stoi(at("count"));
      const auto quad = unhex(at("quad"));
      std::memcpy(a.quad, quad.data(), sizeof a.quad);
      psyq::POLY_FT4 prim{};
      const auto prim_bytes = unhex(at("prim"));
      std::memcpy(&prim.r0, prim_bytes.data() + 4, 0x24);
      const int flips = std::stoi(at("flips"));
      fieldactor_build_light_split(&a, std::stoi(at("w")), std::stoi(at("h")), &prim, flips, std::stoi(at("scaled")));
      ++calls;
      // The pieces after "pieces": OT:bytes.
      std::vector<std::string> want;
      for (size_t i = 0; i < t.size(); ++i)
        if (t[i] == "pieces") want.assign(t.begin() + static_cast<long>(i) + 1, t.end());
      const int before = std::stoi(at("count"));
      bool ok = a.prim_count == std::stoi(at("->", 2)) && a.prim_count - before == static_cast<int>(want.size());
      for (size_t k = 0; ok && k < want.size(); ++k) {
        const auto colon = want[k].find(':');
        const auto bytes = unhex(want[k].substr(colon + 1));
        const int i = before + static_cast<int>(k);
        if (a.prim_ot[i] != std::stoi(want[k].substr(0, colon))) ok = false;
        const u8* got = reinterpret_cast<const u8*>(psyq::prim_words(a.prims[i]));
        for (size_t b = 0; b < bytes.size(); ++b) {
          // Bytes the original leaves as they were: the pads after uv2 / uv3, and the
          // texture coordinates of vertically flipped sprites.
          if (b == 0x1A || b == 0x1B || b == 0x22 || b == 0x23) continue;
          const bool uv = b == 8 || b == 9 || b == 0x10 || b == 0x11 || b == 0x18 || b == 0x19 || b == 0x20 || b == 0x21;
          if (uv && (flips & 1)) continue;
          if (got[b] != bytes[b]) ok = false;
        }
        ++pieces;
      }
      if (!ok && ++bad <= 5) std::fprintf(stderr, "  %s: mismatch: %s\n", file.path().filename().c_str(), line.c_str());
    }
  }
  std::printf("  %d calls, %d pieces compared, %d mismatches\n", calls, pieces, bad);
  CHECK(calls > 0);
  CHECK_EQ(bad, 0);
}

// Ghost trails (field 0x8005ee9c-0x8005f5c8, table A 0x1b / 0x1c): twelve effect actors
// from slot 0x82; each frame the next one in the ring (counting down) copies the
// source and every live one fades by 10, so a ghost is taken again 12 frames later at
// brightness 8; only the first ghost takes the motion (mode 1: grow). Once stopping,
// the ghosts fade out and the afterimage deletes itself.
TEST(field_afterimage_trails_and_fades) {
  Actor** actors = field_actors();
  for (int s = 0x82; s < 0x8e; ++s) {
    if (actors[s] != nullptr) {
      std::printf("  (skipped: effect slots in use)\n");
      return;
    }
  }
  // An empty map: no floor zones (the ghosts are placed with fieldactor_place).
  std::vector<u8> saved_scene = map_data().scene;
  const auto saved_table = map_data().cell_table;
  const auto saved_cells = map_data().cells;
  static u8 no_cells[0x100] = {};
  map_data().scene.assign(0x100, 0);
  map_data().cell_table = no_cells;
  map_data().cells = no_cells;
  globals().frame_ticks = 1;
  map_data().actor_scale = 100;
  auto* src = new Actor;
  fieldactor_construct(src);
  const s32 pos[3] = {100, 0, 200};
  actor_init(src, 0, pos, 0, 0, 0, -1, 0x1000F);
  actors[5] = src;

  Afterimage* a = afterimage_create(new Afterimage, src, 3, 0x80FF80, 1, 1);
  for (int i = 0; i < 12; ++i) {
    CHECK_EQ(a->slot[i], 0x82 + i);
    CHECK(actors[0x82 + i] != nullptr);
  }
  afterimage_update(a);
  Actor* g0 = actors[0x82];
  CHECK_EQ(a->state[0], 2);  // mode 1 + 1, then the mode is used up
  CHECK_EQ(a->mode, 0);
  CHECK_EQ(a->bright[0], 0x80);
  CHECK_EQ(a->head, 11);
  CHECK_EQ(g0->x, src->x);
  CHECK_EQ(g0->blend, 1);
  CHECK(g0->rgb[0] == 0x80 && g0->rgb[1] == 0xFF && g0->rgb[2] == 0x80);
  CHECK((g0->flags & kActorHidden) == 0 && (g0->flags & kActorAnimPaused) != 0);
  CHECK(g0->vtbl == &kFieldActorVtable);

  src->x += 0x10000;
  const s32 scale = g0->scale_x;
  afterimage_update(a);
  CHECK_EQ(a->bright[0], 0x80 - 10);
  CHECK_EQ(g0->scale_x, scale + 0x80);  // grows
  CHECK_EQ(a->state[11], 1);
  CHECK_EQ(actors[0x82 + 11]->x, src->x);
  // Ghost 0 is two frames older than the next one (head 10): recoloured by its age.
  afterimage_update(a);
  CHECK_EQ(g0->rgb[1], static_cast<u8>((0xFF * (0x80 - 10)) >> 7));
  // Frame 12: ghost 0 has faded 11 times; frame 13 takes it again.
  for (int k = 0; k < 9; ++k) afterimage_update(a);
  CHECK_EQ(a->bright[0], 0x80 - 110);
  CHECK_EQ(a->head, 0);
  afterimage_update(a);
  CHECK_EQ(a->bright[0], 0x80);
  CHECK_EQ(a->state[0], 1);
  // Stopping: no new ghosts; the last one fades out on the 13th frame, and the frame
  // after finds none left (afterimage_destroy(self, 3)).
  a->stopping = 1;
  for (int k = 0; k < 13; ++k) afterimage_update(a);
  CHECK_EQ(a->state[0], 0);
  CHECK(actors[0x82] != nullptr);
  afterimage_update(a);
  for (int s = 0x82; s < 0x8e; ++s) CHECK(actors[s] == nullptr);

  // Through the list: deleting the source deletes its afterimages.
  afterimage_attach(src, 3, 0x808080, 1, 0);
  CHECK(actors[0x82] != nullptr);
  actor_destroy(src, 3);
  actors[5] = nullptr;
  for (int s = 0x82; s < 0x8e; ++s) CHECK(actors[s] == nullptr);
  map_data().scene = saved_scene;
  map_data().cell_table = saved_table;
  map_data().cells = saved_cells;
}
