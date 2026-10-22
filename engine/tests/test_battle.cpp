// Battle program (game/battle): math helpers, the stage command lists (#1347-#1404),
// the stage models, the encounter packs (#1405-#1968) and the field -> battle setup.
// The data tests read disc 1 (SS_DISC1) and walk every stage and encounter.

#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "disc/archive.h"
#include "disc/disc_image.h"
#include "disc/slz.h"
#include "game/battle/battle.h"
#include "game/battle/bobj.h"
#include "game/battle/camera.h"
#include "game/battle/hud.h"
#include "game/battle/loaders.h"
#include "game/battle/render.h"
#include "game/battle/stage.h"
#include "game/battle/unit.h"
#include "game/battle/vram.h"
#include "game/boot/system.h"
#include "game/common/math.h"
#include "game/field/encounter.h"
#include "game/field/field.h"
#include "game/title/title_program.h"
#include "test.h"

using namespace ss;
using namespace ss::game;
using namespace ss::game::battle;

namespace {

bool battle_disc() {
  // (Open the disc before the first battle_data: program_data caches what it finds,
  // an empty image included.)
  static std::unique_ptr<disc::DiscImage> image;
  static disc::Archive archive;
  static bool tried = false;
  if (get_system()->loader.archive() == nullptr) {
    if (tried) return false;
    tried = true;
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
  return battle_data(0x80070078) != nullptr;
}

#define NEEDS_DISC()                                  \
  do {                                                \
    if (!battle_disc()) {                             \
      std::printf("  (skipped: SS_DISC1 not set)\n"); \
      return;                                         \
    }                                                 \
  } while (0)

std::vector<u8> entry_bytes(int entry) {
  std::vector<u8> data(static_cast<size_t>(get_system()->loader.entry_size(entry)));
  if (!data.empty()) get_system()->loader.load_sync(data.data(), entry, 0);
  return data;
}

u32 rd32(const u8* p) {
  u32 v;
  std::memcpy(&v, p, 4);
  return v;
}

constexpr int kSectionWords[20] = {9, 13, 11, 15, 11, 15, 11, 17, 12, 20, 13, 19, 7, 9, 10, 12, 9, 12, 12, 15};

// Bytes a battle model spans (u32 part count; per part a u32 and 20 counted sections).
size_t model_size(const std::vector<u8>& m) {
  if (m.size() < 4) return 0;
  size_t p = 4;
  const u32 parts = rd32(m.data());
  for (u32 k = 0; k < parts; ++k) {
    p += 4;
    for (int s = 0; s < 20; ++s) {
      if (p + 4 > m.size()) return 0;
      const u32 n = rd32(m.data() + p);
      p += 4 + static_cast<size_t>(n) * kSectionWords[s] * 4;
    }
  }
  return p;
}

}  // namespace

TEST(battle_stats_layout) {
  CHECK_EQ(offsetof(Stats, hp), 0x1Cu);
  CHECK_EQ(offsetof(Stats, mp), 0x24u);
  CHECK_EQ(offsetof(Stats, atk), 0x2Au);
  CHECK_EQ(offsetof(Stats, guts), 0x48u);
  CHECK_EQ(offsetof(Stats, crit), 0x5Au);
  CHECK_EQ(offsetof(Stats, weapon_element), 0x5Cu);
}

TEST(battle_clut_build_pair) {
  const u16 src[3] = {0x1234, 0, 0x7FFF};
  u16 dst[256];
  std::memset(dst, 0xAA, sizeof dst);
  clut_build_pair(dst, std::span<const u8>(reinterpret_cast<const u8*>(src), sizeof src), 0, 3);
  CHECK_EQ(dst[0], 0x1234);
  CHECK_EQ(dst[0x80], 0x9234);
  CHECK_EQ(dst[1], 0);
  CHECK_EQ(dst[0x81], 0);  // 0 stays transparent in the STP half
  CHECK_EQ(dst[0x82], 0xFFFF);
  CHECK_EQ(dst[3], 0);
  CHECK_EQ(dst[0xFF], 0);
}

TEST(battle_rot_matrix_y) {
  NEEDS_DISC();
  psyq::MATRIX m{};
  rot_matrix_y(0x400, &m);  // 90 degrees
  CHECK_EQ(m.m[0][0], 0);
  CHECK_EQ(m.m[0][2], 0x1000);
  CHECK_EQ(m.m[2][0], -0x1000);
  CHECK_EQ(m.m[1][1], 0x1000);
}

TEST(battle_sine_table) {
  NEEDS_DISC();
  // The battle's quarter-wave table (0x80070078) is the field's and the title's.
  int mismatches = 0;
  for (u32 a = 0; a < 0x1000; ++a) {
    if (bsin(a) != sin4096(a)) ++mismatches;
    if (bcos(a) != cos4096(a)) ++mismatches;
  }
  CHECK_EQ(mismatches, 0);
  CHECK_EQ(ratan2(0, 0x1000), 0);
  CHECK_EQ(ratan2(0x1000, 0), 0x400);
  CHECK_EQ(square_root0(0x10000), 0x100);
}

TEST(battle_camera_motion) {
  // camera_set_motion / camera_motion_step: a 4-tick move reaches its end (remaining < 1)
  // and snaps the eye to the destination.
  bg().frame_ticks = 2;
  Camera c;
  c.eye_dest[0] = 0x123000;
  camera_set_motion(&c, 4, 0x100, 0, 0, 0x100);
  int frames = 0;
  while (camera_motion_step(&c, 1) == 0 && frames < 100) ++frames;
  CHECK_EQ(frames, 1);
  CHECK_EQ(view().cam_eye[0], 0x123000);
}

TEST(battle_stage_lists_parse) {
  NEEDS_DISC();
  // Every stage command list (#1347 + stage) parses to its op 0, and every op-2 block is
  // a battle model that spans its block exactly.
  int stages = 0, models = 0, bad = 0;
  for (int st = 0; st < 58; ++st) {
    const std::vector<u8> list = entry_bytes(0x543 + st);
    if (list.empty()) continue;
    stage() = StageData{};
    view() = ViewState{};
    StageLoadCtx ctx;
    stage_load_ctx_init(&ctx);
    const size_t end = load_stage(&ctx, list);
    if (end == 0 || end > list.size()) {
      std::fprintf(stderr, "  stage %d: the list does not end\n", st);
      ++bad;
    }
    ++stages;
    for (const auto& b : stage().blocks) {
      ++models;
      if (model_size(b) != b.size()) {
        std::fprintf(stderr, "  stage %d: model block of %zu bytes parses to %zu\n", st, b.size(), model_size(b));
        ++bad;
      }
    }
    CHECK(stage().bounds[0] < stage().bounds[2]);
  }
  CHECK_EQ(stages, 58);
  CHECK(models >= 58);
  CHECK_EQ(bad, 0);
}

TEST(battle_encounter_packs_parse) {
  NEEDS_DISC();
  // Every encounter pack (#1405 + encounter) loads: sprite sets, palettes, the formation
  // and one stats record per placement, all inside the entry.
  static std::vector<u8> config(0x2A0, 0);
  bg().config = config.data();
  StageLoadCtx ctx;
  stage() = StageData{};
  stage_load_ctx_init(&ctx);
  load_stage(&ctx, entry_bytes(0x543 + 3));
  int packs = 0, units = 0;
  for (int enc = 0; enc < 564; ++enc) {
    const std::vector<u8> pack = entry_bytes(0x57D + enc);
    if (pack.empty()) continue;
    BattleGlobals& g = bg();
    g.tasks.fill(nullptr);
    g.task_count = 0;
    g.enemy_units.fill(nullptr);
    encounter_reset();
    animset_pool_reset();
    texcache_pool_reset();
    load_formation(pack);
    ++packs;
    for (int i = 0; i < g.enemy_count; ++i) {
      const EnemyUnit* e = g.enemy_units[i];
      if (e == nullptr) continue;
      ++units;
      CHECK(e->st()->hp > 0);
      CHECK(e->name[0] != 0);
    }
    if (enc == 2) {
      // Encounter 2: two Lizardaxes (type 0x1d, 60 HP) facing -x on the Shingo Forest stage.
      CHECK_EQ(g.enemy_count, 2);
      CHECK_EQ(g.enemy_units[0]->st()->id, 0x1D);
      CHECK_EQ(g.enemy_units[0]->st()->hp, 60);
      CHECK(std::strcmp(g.enemy_units[0]->name, "Lizardaxe") == 0);
      CHECK(g.enemy_units[0]->family == EnemyFamily::Axe);
    }
  }
  encounter_reset();
  CHECK_EQ(packs, 564);
  CHECK(units > 1000);
}

TEST(battle_setup_builder) {
  NEEDS_DISC();
  // The field's battle_build_load_list for encounter 2 on stage 3 gives the list the
  // original handed over in PCSX-Redux (field 0x800752b8 at field_exit_to_program).
  create_game_state();
  auto* party = static_cast<u8*>(handoff_get(&get_system()->handoff, 3));
  auto* records = static_cast<u8*>(handoff_get(&get_system()->handoff, 4));
  party_slot_init(party, records, 1);
  field::field_bind_system(nullptr);
  BattleSetup s;
  field::battle_build_load_list(&s, 2, 3);
  const s32 want[10] = {1977, 1978, 1350, 1979, 1980, 1981, 1983, 1407, 1982, 1984};
  for (int i = 0; i < 10; ++i) CHECK_EQ(s.entry(i), want[i]);
  CHECK_EQ(s.encounter(), 2);
  CHECK_EQ(s.stage(), 3);
  CHECK(s.count() == 9 || s.count() == 11);
}

TEST(battle_field_reload_list) {
  NEEDS_DISC();
  // map_resolve_entries(list, -1, 0, 0) from the field's state block (handoff 9): the
  // original's list for Claude alone on map 7 was {2726, 0xc26, 0xc8e, 0xc27, -1, 0xc1d, 0}
  // with count 6 (PCSX-Redux, battle_run). battle_queue_field_preloads then keeps them
  // all, negated, with their bytes in list order.
  HandoffRegistry* reg = &get_system()->handoff;
  create_game_state();
  party_slot_init(static_cast<u8*>(handoff_get(reg, 3)), static_cast<u8*>(handoff_get(reg, 4)), 1);
  std::vector<u8> block(0x900, 0);
  const s16 map = 7, music = 2726;
  std::memcpy(block.data() + 0x23A, &map, 2);
  std::memcpy(block.data() + 0x23C, &music, 2);
  void* old9 = handoff_get(reg, 9);
  handoff_store(reg, block.data(), 9);
  bg().handoff = reg;
  bg().loader = &get_system()->loader;
  s16 list[0x11];
  battle_map_resolve_entries(list, -1, 0, 0);
  const s16 want[7] = {2726, 0xC26, 0xC8E, 0xC27, -1, 0xC1D, 0};
  for (int i = 0; i < 7; ++i) CHECK_EQ(list[i], want[i]);
  CHECK_EQ(list[0x10], 6);
  bg().preload_list.assign(list, list + 0x11);
  battle_queue_field_preloads();
  size_t total = 0;
  for (int i = 0; i < 6; ++i)
    if (want[i] > 0) {
      CHECK_EQ(bg().preload_list[i], -want[i]);
      total += static_cast<size_t>(get_system()->loader.entry_size(want[i]));
    }
  CHECK_EQ(bg().preload_list[4], -1);
  CHECK_EQ(bg().handoff10_blob.size(), total);
  const std::vector<u8> music_bytes = entry_bytes(2726);
  CHECK(std::memcmp(bg().handoff10_blob.data(), music_bytes.data(), music_bytes.size()) == 0);
  bg().preload_list.clear();
  bg().handoff10_blob.clear();
  if (old9 != nullptr) handoff_store(reg, old9, 9);
  else handoff_clear(reg, 9);
}

TEST(battle_hud_gauge) {
  // hud_draw_gauge 0x80066d84: (width - 1) * value / max filled in a G4 from 0xc0c0 to
  // (pct << 8 | 0xc0 - pct | pct / 4 << 16) (pct = value * 0xc0 / max), the rest a dark
  // TILE, and a three-point shadow line whose x leaves out the window x (the console's).
  auto w = std::make_unique<BWindow>(0x50);
  w->x = 0x10;
  w->y = 4;
  w->ctx.ot_head = screen_clear_ot(w->ctx.ot);
  hud_draw_gauge(w.get(), 0x2A, 0x13, 0x30, 65, 130);
  auto* line = reinterpret_cast<psyq::LINE_F3*>(w->ctx.ot_head->next);  // added last, so first
  CHECK(line != nullptr);
  auto* tile = reinterpret_cast<psyq::TILE*>(line->tag.next);
  auto* g4 = reinterpret_cast<psyq::POLY_G4*>(tile->tag.next);
  const int fill = 47 * 65 / 130;  // 23
  CHECK_EQ(g4->code & 0xFC, 0x38);
  CHECK_EQ(g4->x0, 0x10 + 0x2A);
  CHECK_EQ(g4->x1, 0x10 + 0x2A + fill);
  CHECK_EQ(g4->y0, 4 + 0x13);
  CHECK_EQ(g4->y2, 4 + 0x13 + 4);
  CHECK_EQ(g4->r0, 0xC0);
  CHECK_EQ(g4->g0, 0xC0);
  CHECK_EQ(g4->b0, 0);
  CHECK_EQ(g4->r1, 0x60);  // 0xc0 - 96
  CHECK_EQ(g4->g1, 0x60);  // 96
  CHECK_EQ(g4->b1, 0x18);  // 96 / 4
  CHECK_EQ(tile->code, 0x60);
  CHECK_EQ(tile->x0, 0x10 + 0x2A + fill);
  CHECK_EQ(tile->w, 47 - fill);
  CHECK_EQ(tile->h, 4);
  CHECK_EQ(tile->r0, 0x40);
  CHECK_EQ(line->code, 0x48);
  CHECK_EQ(line->x0, 0x2A + 1);
  CHECK_EQ(line->x1, 0x2A + 47 + 1);
  CHECK_EQ(line->y2, 4 + 0x13);
  CHECK(tile->tag.next == nullptr || reinterpret_cast<void*>(tile->tag.next) == g4);
}

TEST(battle_message_banner) {
  NEEDS_DISC();
  // battle_message_show 0x80074ca4 / start 0x80074d54 / update 0x80074e20: the banner is
  // the string's width + 0x18 wide, centred, rises 3 px a tick from y 0xe8 to 0xc0, holds
  // 60 ticks, sinks back and closes; a second message makes the first sink at once and
  // follows it.
  BattleGlobals& g = bg();
  std::vector<u8> config(0x2A0, 0);
  u8* old_config = g.config;
  g.config = config.data();
  g.tasks.fill(nullptr);
  g.task_count = 0;
  g.ot_layers.fill(nullptr);
  g.state = 2;
  g.flags = 0;
  g.frame_ticks = 1;
  battle_load_entry(4, 1980, nullptr, entry_bytes(1980));
  // One battle_main_loop frame: the text line buffer starts over, then the tasks run.
  auto frame = [] {
    text_frame_begin();
    task_run_all();
  };
  const int id = 0x2711;
  const int width = text_width_id(id) + 0x18;
  CHECK(width > 0x18);
  battle_message_show(id);
  MessageWindow* m = battle_message();
  CHECK(m != nullptr);
  if (m == nullptr) return;
  CHECK_EQ(m->x, 0xA0 - width / 2);
  CHECK_EQ(m->w, width);
  CHECK_EQ(m->y, 0xE8);
  int updates = 0;
  while (battle_message() == m && m->step == 0 && updates < 100) frame(), ++updates;
  CHECK_EQ(updates, 14);
  CHECK_EQ(m->y, 0xC0);
  updates = 0;
  while (battle_message() == m && m->step == 1 && updates < 100) frame(), ++updates;
  CHECK_EQ(updates, 61);
  updates = 0;
  while (battle_message() == m && updates < 100) frame(), ++updates;
  CHECK_EQ(updates, 14);
  CHECK(battle_message() == nullptr);
  // A queued message.
  battle_message_show(id);
  m = battle_message();
  frame();
  frame();
  battle_message_show(0x2714);
  CHECK_EQ(m->step, 2);
  CHECK_EQ(m->queued, 0x2714);
  frame();  // 0xe2 -> 0xe5
  CHECK(battle_message() == m);
  frame();  // 0xe8: closes and the next one opens
  MessageWindow* next = battle_message();
  CHECK(next != nullptr);
  if (next != nullptr) {
    CHECK_EQ(next->text_id, 0x2714);
    CHECK_EQ(next->step, 0);
  }
  battle_windows_free();
  g.config = old_config;
  g.tasks.fill(nullptr);
  g.task_count = 0;
  g.ot_layers.fill(nullptr);
}
