// The leader stand-in (game/field/leader.h; Archipelago open world, port feature): who stands
// in, the scene latch, the hero's name tokens in script messages, and the walking actor drawn
// with the stand-in's sprite from the party's real sprite pack (that test needs SS_DISC1).

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "disc/archive.h"
#include "disc/disc_image.h"
#include "game/ap/ap.h"
#include "game/boot/system.h"
#include "game/field/actor_base.h"
#include "game/field/actor_field.h"
#include "game/field/actors.h"
#include "game/field/field.h"
#include "game/field/leader.h"
#include "game/field/map.h"
#include "game/field/sprite_pack.h"
#include "test.h"

using namespace ss;
using namespace ss::game;
using namespace ss::game::field;

namespace {

// A field state of our own: Claude's game (script flag 1) or Rena's, free play on a field map,
// the party {hero, Ashton, Leon (inactive)}, the Formation leader in slot 'leader'.
struct Scene {
  u8 block[0xA40] = {};
  u8 config[0x2A0] = {};
  u8 party[0x300] = {};
  FieldGlobals saved;
  Actor walking;
  Scene(bool claude, int leader, bool open_world = true) {
    FieldGlobals& g = globals();
    saved = g;
    g.script_globals = block + 0x180;
    g.field_flags = block + 0x180;
    g.game_state = block + 0x220;
    g.script_flags = block + 0x8C0;
    g.config = config;
    g.party = party;
    g.frame_ticks = 1;
    const int hero = claude ? 0 : 1;
    if (claude) g.script_flags[0] |= 2;
    g.game_state[0x24] = static_cast<u8>(hero);
    g.game_state[0x25] = static_cast<u8>(hero ^ 1);
    const s16 control = static_cast<s16>(hero);
    std::memcpy(g.field_flags + 0x10, &control, 2);  // the player controls the walking actor
    const s16 ids[3] = {static_cast<s16>(hero + 1), 7, -8};
    for (int i = 0; i < 3; ++i) std::memcpy(party + i * 0x60, &ids[i], 2);
    config[0x41] = static_cast<u8>(leader);
    walking.model = static_cast<s16>(hero);
    field_actors()[hero] = &walking;
    leader_reset();
    ap::stop();
    if (open_world) {
      ap::Seed s;
      s.seed = "leader-test";
      s.open_world = true;
      ap::start_with(s);
    }
  }
  ~Scene() {
    for (int i = 0; i < 0x82; ++i)
      if (field_actors()[i] == &walking) field_actors()[i] = nullptr;
    ap::stop();
    leader_reset();
    globals() = saved;
  }
};

// 0x400c (a name: 0x8c 0x80 then the character), 0x4007 (the name in a message variable).
std::vector<u8> name(int ch) { return {0x8C, 0x80, static_cast<u8>(ch)}; }
std::vector<u8> text(std::initializer_list<std::vector<u8>> parts) {
  std::vector<u8> out;
  for (const auto& p : parts) out.insert(out.end(), p.begin(), p.end());
  out.push_back(0);
  return out;
}
std::vector<u8> str(const char* s) { return std::vector<u8>(s, s + std::strlen(s)); }

}  // namespace

TEST(leader_standin_follows_the_formation_leader) {
  StandIn s;
  {
    Scene scene(true, 1);  // Ashton leads Claude's game
    CHECK(leader_standin(&s));
    CHECK_EQ(s.model, 6);
    CHECK_EQ(s.hero, 0);
  }
  {
    Scene scene(true, 0);  // the hero leads
    CHECK(!leader_standin(&s));
  }
  {
    Scene scene(true, 2);  // an inactive member (a negative id: in reserve, or a Private Action's)
    CHECK(!leader_standin(&s));
  }
  {
    Scene scene(true, 5);  // an empty slot
    CHECK(!leader_standin(&s));
  }
  {
    Scene scene(true, 1, false);  // no seed: never
    CHECK(!leader_standin(&s));
  }
  {
    Scene scene(true, 1);
    ap::stop();
    ap::Seed story;
    story.seed = "story";
    ap::start_with(story);  // story mode: never
    CHECK(!leader_standin(&s));
  }
  {
    Scene scene(true, 1);
    globals().field_flags[0x68] = 1;  // a world map
    CHECK(!leader_standin(&s));
  }
  {
    Scene scene(true, 1);
    scene.walking.model = 199;  // a script rides the bunny: its sprite stays
    CHECK(!leader_standin(&s));
  }
  {
    Scene scene(false, 1);  // Rena's game: her model and name index are 1
    CHECK(leader_standin(&s));
    CHECK_EQ(s.model, 6);
    CHECK_EQ(s.hero, 1);
  }
  {
    Scene scene(true, 1, false);
    leader_set_debug(1, -2);  // SS_LEADER_STANDIN=1: on without a seed (debugging)
    CHECK(leader_standin(&s));
    leader_set_debug(-2, 0);  // SS_LEADER_SLOT=0: the hero's slot instead of config +0x41
    CHECK(!leader_standin(&s));
  }
  {
    Scene scene(true, 1);
    leader_set_debug(0, -2);  // SS_LEADER_STANDIN=0: off in an open-world seed
    CHECK(!leader_standin(&s));
  }
}

TEST(leader_scene_latch) {
  Scene scene(true, 1);
  StandIn s;
  // The stand-in's own actor on the map: the hero shows (sprite and names) while it exists.
  Actor own;
  own.model = 6;
  field_actors()[6] = &own;
  CHECK(!leader_standin(&s));
  CHECK(leader_scene_latched());
  // ... and for the rest of the scene after it goes; free play clears the latch.
  field_actors()[6] = nullptr;
  const s16 none = -1;
  std::memcpy(globals().field_flags + 0x10, &none, 2);  // a scene holds the controls
  CHECK(!leader_standin(&s));
  const s16 hero = 0;
  std::memcpy(globals().field_flags + 0x10, &hero, 2);
  CHECK(leader_standin(&s));
  CHECK(!leader_scene_latched());
  // In free play an own actor latches at once, every frame.
  field_actors()[0x40] = &own;  // any slot: a script may use another one
  CHECK(!leader_standin(&s));
  field_actors()[0x40] = nullptr;
  CHECK(leader_standin(&s));
}

TEST(leader_message_names_the_standin) {
  Scene scene(true, 1);  // Ashton (6) stands in for Claude (0)
  // {name:00}: ... {name:01} -> the hero's token names Ashton; Rena's stays hers.
  const std::vector<u8> line = text({name(0), str(": Let's go, "), name(1), str(".")});
  const u8* out = leader_message_text(line.data());
  CHECK(out != line.data());
  const std::vector<u8> want = text({name(6), str(": Let's go, "), name(1), str(".")});
  CHECK_EQ(std::memcmp(out, want.data(), want.size()), 0);
  // The same message again: the same copy.
  CHECK(leader_message_text(line.data()) == out);
  // Parameters are skipped, not read as codes: a timed wait (0x4006, two bytes) whose bytes
  // look like a name code.
  const std::vector<u8> waits = text({{0x86, 0x80, 0x8C, 0x80}, str("x"), name(0)});
  const u8* w = leader_message_text(waits.data());
  CHECK(w != waits.data() && w[2] == 0x8C && w[3] == 0x80 && w[7] == 6);
  // Nothing to change: the script's own text.
  const std::vector<u8> plain = text({str("Hello."), name(1)});
  CHECK(leader_message_text(plain.data()) == plain.data());
  CHECK(!leader_scene_latched());
  // A message naming the stand-in: the hero's names for the rest of the scene (never
  // "Ashton: ... Ashton,").
  const s16 none = -1;
  std::memcpy(globals().field_flags + 0x10, &none, 2);
  const std::vector<u8> self = text({name(0), str(": "), name(6), str(", wait!")});
  CHECK(leader_message_text(self.data()) == self.data());
  CHECK(leader_scene_latched());
  CHECK(leader_message_text(line.data()) == line.data());
  // Free play again: the stand-in again.
  const s16 hero = 0;
  std::memcpy(globals().field_flags + 0x10, &hero, 2);
  CHECK(leader_message_text(line.data()) != line.data());
  // A name variable (0x4007) holding the stand-in latches too.
  std::memcpy(globals().field_flags + 0x10, &none, 2);
  const s32 six = 6;
  std::memcpy(globals().script_globals + 0x38 + 3 * 4, &six, 4);
  const std::vector<u8> var = text({name(0), str(": "), {0x87, 0x80, 3}});
  CHECK(leader_message_text(var.data()) == var.data());
  CHECK(leader_scene_latched());
}

TEST(leader_message_in_renas_game) {
  Scene scene(false, 1);  // Ashton stands in for Rena: {name:01} is the hero's token
  const std::vector<u8> line = text({name(1), str(": Claude? "), name(0)});
  const u8* out = leader_message_text(line.data());
  CHECK(out != line.data() && out[2] == 6 && out[line.size() - 2] == 0);
}

TEST(leader_message_without_a_standin) {
  Scene scene(true, 0);  // the hero leads: messages are the script's own
  const std::vector<u8> line = text({name(0), str(": Hi.")});
  CHECK(leader_message_text(line.data()) == line.data());
}

TEST(leader_standin_animations) {
  for (int id = 0; id <= 13; ++id) CHECK_EQ(leader_standin_anim(id), id);  // walk, stand
  for (int id = 14; id <= 20; ++id) CHECK_EQ(leader_standin_anim(id), id - 14);  // run -> walk
  CHECK_EQ(leader_standin_anim(21), -1);
  CHECK_EQ(leader_standin_anim(0x24), -1);  // scene animations
  CHECK_EQ(leader_standin_anim(-1), -1);
}

namespace {

// The field program and the party's full sprite pack A (entry 0xc27 + 2 * 31: Claude, Rena and
// members 3-7) from SS_DISC1 in sprite pack slot 0; false without a disc.
bool party_pack() {
  static std::unique_ptr<disc::DiscImage> image;
  static disc::Archive own;
  static bool tried = false, ok = false;
  if (tried) return ok;
  tried = true;
  const disc::Archive* archive = get_system()->loader.archive();
  if (archive == nullptr) {
    const char* path = std::getenv("SS_DISC1");
    if (path == nullptr || *path == 0) return false;
    std::string error;
    image = disc::DiscImage::open(path, &error);
    if (!image || !own.open(*image, &error)) return false;
    boot_main(0, &own);
    archive = &own;
  }
  return ok = archive != nullptr && archive->has_entry(0xC27 + 2 * 31);
}

}  // namespace

TEST(leader_sprite_swap_draws_the_standin) {
  if (!party_pack()) {
    std::printf("  (skipped: SS_DISC1 not set)\n");
    return;
  }
  disc::Archive* archive = const_cast<disc::Archive*>(get_system()->loader.archive());
  const u32 entry = 0xC27 + 2 * 31;
  std::vector<u8> raw(archive->entry_size(entry));
  CHECK(archive->read_from_entry(entry, raw.size(), raw.data()));
  SpritePackSlot* slots = sprite_pack_slots();
  std::vector<SpritePackSlot> kept(slots, slots + 16);
  for (int i = 0; i < 16; ++i) slots[i] = SpritePackSlot{};
  CHECK(sprite_pack_register(raw.data(), static_cast<s32>(entry), 0));
  {
    Scene scene(true, 1);  // Ashton leads
    Actor& a = scene.walking;
    map_data().actor_scale = 100;
    fieldactor_construct(&a);
    const s32 pos[3] = {0, 0, 0};
    actor_init(&a, 0, pos, 0, 0, 0, -1, 0x1000F);
    // Standing, facing the camera's direction 0: the hero's sprite resolves, the swap shows
    // Ashton's stand for the same direction, and puts Claude's back after.
    a.anim = -1;
    a.auto_anim = 12;
    a.resolved_for = 0xFFFF;
    actor_resolve_anim(&a);
    CHECK(a.cel_header != nullptr && a.sprite_anim >= 7 && a.sprite_anim <= 13);
    const u8* hero_header = a.cel_header;
    const s16 hero_cel = a.cel;
    SpriteView ashton;
    CHECK(sprite_view(sprite_pack_find_group(static_cast<u32>(a.sprite_anim), 6), 6, a.sprite_anim, &ashton));
    {
      const LeaderSpriteSwap swap(&a);
      CHECK(swap.active());
      CHECK(a.cel_header == ashton.cel_header && a.cel_header != hero_header);
      CHECK(a.pixels == ashton.pixels && a.palettes == ashton.palettes);
      CHECK_EQ(a.cel, static_cast<s16>(read_u16(ashton.anim_data + 4)));
    }
    CHECK(a.cel_header == hero_header && a.cel == hero_cel);
    // Walking: Ashton's walk frame follows the hero's frame (same shape for all twelve).
    a.auto_anim = 0;
    actor_resolve_anim(&a);
    a.anim_frame = 2;
    SpriteView walk;
    CHECK(sprite_view(sprite_pack_find_group(static_cast<u32>(a.sprite_anim), 6), 6, a.sprite_anim, &walk));
    {
      const LeaderSpriteSwap swap(&a);
      CHECK(swap.active());
      CHECK(a.cel_header == walk.cel_header);
      CHECK_EQ(a.cel, static_cast<s16>(read_u16(walk.anim_data + 4 + 2 * 4)));
    }
    // Running: Ashton has no run sprites; his walk shows, on its own clock at the run's pace.
    a.auto_anim = 24;
    actor_resolve_anim(&a);
    CHECK(a.sprite_anim >= 14 && a.sprite_anim <= 20);
    SpriteView run_walk;
    const int walk_id = a.sprite_anim - 14;
    CHECK(sprite_view(sprite_pack_find_group(static_cast<u32>(walk_id), 6), 6, walk_id, &run_walk));
    std::vector<s16> cels;
    for (int frame = 0; frame < 30; ++frame) {
      const LeaderSpriteSwap swap(&a);
      CHECK(swap.active() && a.cel_header == run_walk.cel_header);
      cels.push_back(a.cel);
    }
    // The walk's three frames come round (ping-pong) every 6 vblanks or so.
    int changes = 0;
    for (size_t i = 1; i < cels.size(); ++i) changes += cels[i] != cels[i - 1];
    CHECK(changes >= 3 && changes <= 6);
    // A scene animation the stand-in lacks: the hero, latched until free play.
    a.anim = 0x30;
    a.auto_anim = 0x30;
    a.sprite_anim = 0x30;
    const s16 none = -1;
    std::memcpy(globals().field_flags + 0x10, &none, 2);
    {
      const LeaderSpriteSwap swap(&a);
      CHECK(!swap.active());
    }
    CHECK(leader_scene_latched());
    a.anim = -1;
    a.auto_anim = 12;
    actor_resolve_anim(&a);
    {
      const LeaderSpriteSwap swap(&a);
      CHECK(!swap.active());  // still the scene
    }
    const s16 hero = 0;
    std::memcpy(globals().field_flags + 0x10, &hero, 2);
    {
      const LeaderSpriteSwap swap(&a);
      CHECK(swap.active());  // free play: Ashton again
    }
    // Another actor is never swapped.
    Actor other = a;
    {
      const LeaderSpriteSwap swap(&other);
      CHECK(!swap.active());
    }
  }
  {
    // A member whose sprite the map's packs lack (pack B's Leon, 7: he joined, or was made
    // leader, after the map loaded): his own pack from the disc, read on the side.
    Scene scene(true, 1);
    const s16 leon = 8;
    std::memcpy(globals().party + 0x60, &leon, 2);
    Actor& a = scene.walking;
    fieldactor_construct(&a);
    const s32 pos[3] = {0, 0, 0};
    actor_init(&a, 0, pos, 0, 0, 0, -1, 0x1000F);
    a.anim = -1;
    a.auto_anim = 12;
    a.resolved_for = 0xFFFF;
    actor_resolve_anim(&a);
    const u8* hero_header = a.cel_header;
    CHECK(sprite_pack_find_group(static_cast<u32>(a.sprite_anim), 7) == nullptr);
    globals().loader = nullptr;  // no disc to read: the hero
    {
      const LeaderSpriteSwap swap(&a);
      CHECK(!swap.active());
    }
    CHECK(!leader_scene_latched());
    globals().loader = &get_system()->loader;
    {
      const LeaderSpriteSwap swap(&a);
      CHECK(swap.active());
      CHECK(a.cel_header != hero_header && a.cel_header != nullptr);
    }
    CHECK(a.cel_header == hero_header);
    globals().field_flags[0x27] = 1;  // a script's palette tint: the own copy has none
    {
      const LeaderSpriteSwap swap(&a);
      CHECK(!swap.active());
    }
  }
  for (int i = 0; i < 16; ++i) slots[i] = kept[i];
}
