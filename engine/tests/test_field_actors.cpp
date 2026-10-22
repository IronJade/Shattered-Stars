// Field actors (game/field/actor_*, collision, camera): the command ring and its
// interpreter (waits, moves, turns, fades), the handoff-0xb state snapshot,
// actor-to-actor collision and the floor / region point tests. The numbers follow
// the original's arithmetic (actor_update 0x8003d95c, fieldactor_move_step
// 0x80081cc8); walking on map 7 is compared with PCSX-Redux traces of the running
// game (SS_ACTOR_TRACE prints the same fields). Moves and distances use the field
// program's SquareRoot0 / ratan2 tables, read from the disc: those tests need SS_DISC1.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "disc/archive.h"
#include "disc/disc_image.h"
#include "game/boot/system.h"

#include "game/field/actor_base.h"
#include "game/field/actor_field.h"
#include "game/field/actors.h"
#include "game/field/collision.h"
#include "game/field/field.h"
#include "game/field/map.h"
#include "game/field/script_vm.h"
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
