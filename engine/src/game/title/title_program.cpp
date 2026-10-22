#include "game/title/title_program.h"

#include <cstring>
#include <memory>
#include <optional>

#include "disc/slz.h"
#include "game/common/program_image.h"
#include "game/boot/pad.h"
#include "game/boot/rng.h"
#include "game/title/intro.h"
#include "game/title/load_queue.h"
#include "game/title/movie.h"
#include "game/title/new_game.h"
#include "game/title/title_screen.h"
#include "runtime/libgpu.h"
#include "runtime/runtime.h"

namespace ss::game {
namespace {

TitleGlobals g_title;
u8 g_title_shown = 0;  // boot 0x8002b54c: 1 once the title has run (skips the logos)

}  // namespace

TitleGlobals& title_globals() { return g_title; }

const u8* title_data(u32 address) { return program_data(static_cast<int>(Program::Title), address); }

int loader_is_disc(Loader& loader, int disc) {
  if (!loader.archive() || loader.archive()->toc_word0() != 0x34829314) return 2;
  return loader.entry_size(disc + 5) != 0;
}

u8 inventory_check_byte(const u16* inventory, u32 index, u32 key) {
  u32 v = inventory[index] & 0x7FFF;
  if (key == 0xFFFF) key = rng_range(0x10);
  v ^= v >> 8;
  return static_cast<u8>((((~index & 0xF) ^ key) << 4 | ((v & 0xF) ^ ((v & 0xFF) >> 4) ^ key)) & 0xFF);
}

namespace {

void put16(u8* p, int offset, u16 v) { std::memcpy(p + offset, &v, 2); }
void put32(u8* p, int offset, u32 v) { std::memcpy(p + offset, &v, 4); }

// 0x80062f64: rolls up to four talents (record + 0x20, a bit mask) from a table of ten
// {u16 bit, u16 percent chance}, visiting the entries in a shuffled order with any
// 100% talent first.
void roll_talents(u8* record, const u8* table) {
  auto chance = [table](int k) {
    u16 v;
    std::memcpy(&v, table + k * 2 + 2, 2);
    return v;
  };
  put32(record, 0x20, 0);
  int order[10];
  for (int i = 0; i < 10; ++i) order[i] = i * 2;  // byte offsets / 2 of each entry
  for (int i = 0; i < 100; ++i) {
    const u32 a = rng_range(10), b = rng_range(10);
    std::swap(order[a], order[b]);
  }
  for (int i = 1; i < 10; ++i) {
    if (chance(order[i]) == 100) std::swap(order[0], order[i]);
  }
  int picked = 0;
  for (int i = 0; i < 10 && picked < 4; ++i) {
    const u16 percent = chance(order[i]);
    if (rng_range(100) < percent) {
      ++picked;
      u16 bit;
      std::memcpy(&bit, table + order[i] * 2, 2);
      u32 talents;
      std::memcpy(&talents, record + 0x20, 4);
      put32(record, 0x20, talents | bit);
    }
  }
}

// 0x800620f0: Claude's starting stats.
void init_claude(u8* slot, u8* record) {
  put32(slot, 0x14, 0x82);
  put32(slot, 0x1C, 0x82);
  put32(slot, 0x18, 0x82);
  put16(slot, 0x30, 5);
  put16(slot, 0x4E, 0x10);
  put16(slot, 0x54, 9);
  put16(slot, 0x28, 1);
  put16(slot, 0x20, 0x14);
  put16(slot, 0x24, 0x14);
  put16(slot, 0x22, 0x14);
  put16(slot, 0x2A, 10);
  put16(slot, 0x36, 10);
  put16(slot, 0x3C, 10);
  put16(slot, 0x46, 10);
  slot[2] = 0;
  put16(slot, 0x48, 0x14);
  put32(slot, 0x10, 0);
  put16(record, 6, 0x0F);
  put16(record, 0, 0x84);
  for (int i = 6; i <= 0x0C; ++i) put16(record, i * 2, 0);
  record[0x3E] = 1;
  record[0xCC] = 3;
  record[0xCD] = 0;
  record[0x72] = 1;
  record[0x77] = 1;
  slot[3] = 1;
  roll_talents(record, title_data(0x80063170));
  std::memcpy(record + 0x24, title_data(0x80063110), 7);  // "Claude"
}

// 0x80062210: Rena's starting stats.
void init_rena(u8* slot, u8* record) {
  put32(slot, 0x14, 100);
  put32(slot, 0x1C, 100);
  put32(slot, 0x18, 100);
  put16(slot, 0x20, 0x28);
  put16(slot, 0x24, 0x28);
  put16(slot, 0x22, 0x28);
  put16(slot, 0x2A, 5);
  put16(slot, 0x30, 2);
  put16(slot, 0x36, 8);
  put16(slot, 0x46, 0x0F);
  put16(slot, 0x4E, 0x0F);
  put16(slot, 0x54, 6);
  put16(slot, 0x48, 0x1E);
  put16(slot, 0x28, 1);
  put16(slot, 0x3C, 10);
  slot[2] = 0;
  put32(slot, 0x10, 0);
  put16(record, 0, 0x82);
  put16(record, 0x0C, 0x21B);
  put16(record, 6, 10);
  put16(record, 0x0E, 0x2A9);
  for (int i = 8; i <= 0x0C; ++i) put16(record, i * 2, 0);
  record[0x3C] = 1;
  record[0x6C] = 1;
  record[0x6D] = 4;
  record[0x6E] = 1;
  slot[3] = 9;
  roll_talents(record, title_data(0x80063198));
  std::memcpy(record + 0x24, title_data(0x80063118), 5);  // "Rena"
}

}  // namespace

void party_slot_init(u8* slot, u8* record, int character) {
  std::memset(slot, 0, 0x60);
  std::memset(record, 0, 0xD0);
  switch (character) {
    case 1: init_claude(slot, record); break;
    case 2: init_rena(slot, record); break;
    default:
      // Characters 3-12 (0x80062330..0x80062e34) join in the field program, which has
      // its own copy; the title program only starts Claude or Rena.
      if (character != 0) runtime::Runtime::get().log_once("party_slot_init", "party_slot_init: character not ported");
      break;
  }
  put16(slot, 0, static_cast<u16>(character & 0xFF));
}

void create_game_state() {
  // Allocation order is shuffled with 100 random swaps (200 draws), as on the console.
  int order[4] = {0, 1, 2, 3};
  for (int i = 0; i < 100; ++i) {
    const int a = static_cast<int>(rng_range(4)), b = static_cast<int>(rng_range(4));
    std::swap(order[a], order[b]);
  }
  u8 *config = nullptr, *party = nullptr, *records = nullptr, *inventory = nullptr;
  for (int which : order) {
    switch (which) {
      case 0: config = new u8[0x2A0](); break;
      case 1: party = new u8[0x300](); break;
      case 2: records = new u8[0x680](); break;
      case 3: {
        inventory = new u8[0xC28]();
        auto* items = reinterpret_cast<u16*>(inventory);
        for (u32 i = 0; i < 0x400; ++i) {
          items[i] = 0;
          inventory[0x824 + i] = inventory_check_byte(items, i, 0xFFFF);
        }
        for (int i = 0; i < 16; ++i) items[0x400 + i] = 0;
        inventory[0xC24] = 0;
        break;
      }
    }
  }
  HandoffRegistry* reg = &get_system()->handoff;
  handoff_store(reg, config, 2);
  handoff_store(reg, party, 3);
  handoff_store(reg, records, 4);
  handoff_store(reg, inventory, 6);
  party_slot_init(party, records, 1);
  for (int i = 1; i < 8; ++i) party_slot_init(party + i * 0x60, records + i * 0xD0, 0);
  const u8 defaults[16] = {0x40, 0, 0x20, 0, 0x10, 0, 0x80, 0, 4, 0, 8, 0, 1, 0, 2, 0};
  std::memcpy(config, defaults, sizeof defaults);
  config[0x40] = 0;
  config[0x46] = 1;
  const u8 colours[16] = {0, 0xFF, 0xFF, 0, 0, 0x80, 0x80, 0, 0, 0x80, 0x80, 0, 0, 0x10, 0x10, 0};
  std::memcpy(config + 0x30, colours, sizeof colours);
}

void title_init_config() {
  create_game_state();
  g_title.config = static_cast<u8*>(handoff_get(g_title.handoff, 2));
  const u8* order = title_data(0x8004E32C);
  const u8* table_a = title_data(0x8004E338);
  const u8* table_b = title_data(0x8004E3C8);
  if (!order || !table_a || !table_b || !g_title.config) return;
  for (int i = 0; i < 12; ++i) {
    for (int j = 0; j < 12; ++j) {
      g_title.config[order[j] * 12 + order[i] + 0x4B] = table_a[j * 12 + i];
      g_title.config[order[j] * 12 + order[i] + 0xDB] = table_b[j * 12 + i];
    }
  }
}

namespace {

// LAB_8004241c: after New Game. Frees the title's data, sets up the chosen leader,
// plays the prologue and hands over to the field program (#3100).
[[noreturn]] void start_new_game(System* sys, const char* name) {
  g_title.entry12.clear();
  g_title.entry12.shrink_to_fit();
  // The title intro object and the memory card manager are freed here (0x8003cc88,
  // 0x80031324); the port's intro is owned by title_program_main's frame.
  u8* config = g_title.config;
  std::memset(config + 0x1A0, 0, 0x100);
  clock_reset(g_title.pad);
  auto* party = static_cast<u8*>(handoff_get(g_title.handoff, 3));
  auto* records = static_cast<u8*>(handoff_get(g_title.handoff, 4));
  while (!async_load_done(0)) load_queue_update();
  while (g_title.loader->poll() != 0) {
  }
  load_queue_reset();
  const bool rena = config[0x45] != 0;
  Loader* loader = g_title.loader;
  auto load_whole = [loader](int entry) {
    std::vector<u8> data(static_cast<size_t>(loader->entry_size(entry)));
    loader->load_sync(data.data(), entry, 0);
    return data;
  };
  // Plays one part of the prologue; 1 when it ends, 2 when the player skips it.
  auto play = [](Prologue* p) {
    int r;
    while ((r = prologue_update(p, 1)) != 1 && r != 2) {
      if (p->has_movie) movie_present(p);
    }
    return r;
  };
  std::vector<u8> field;
  if (rena) {
    party_slot_init(party, records, 2);
    std::strcpy(reinterpret_cast<char*>(records + 0x24), name);
    sound_set_volume(g_title.sound, 0x30, 0x7F);
    auto* p = prologue_create(new Prologue);
    prologue_load_sound(p, g_title.prologue_data);
    std::vector<u8> script = load_whole(0x119B);  // #4507
    prologue_set_script(p, script.data());
    pad_clear(g_title.pad, 0);
    play(p);
    sound_stop(g_title.sound);
    sound_set_volume(g_title.sound, 0x40, 0x7F);
    delete p;
  } else {
    party_slot_init(party, records, 1);
    std::strcpy(reinterpret_cast<char*>(records + 0x24), name);
    auto* p = prologue_create(new Prologue);
    prologue_load_sound(p, g_title.prologue_data);
    std::vector<u8> part1 = load_whole(0x1198);  // #4504
    prologue_set_script(p, part1.data());
    // Part 2 (#4505) streams in while part 1 plays.
    std::vector<u8> part2(static_cast<size_t>(loader->entry_size(0x1199)));
    loader->select_entry(0x1199);
    loader->start_read(part2.data(), 0);
    pad_clear(g_title.pad, 0);
    int r = play(p);
    if (r == 2) {
      loader->cancel();
      while (loader->poll() != 0) {
      }
      delete p;
      p = nullptr;
    }
    part1.clear();
    while (loader->poll() != 0) {
    }
    if (r != 2) {
      prologue_restart(p);
      sound_set_volume(g_title.sound, 0x38, 0x7F);
      prologue_set_script(p, part2.data());
      pad_clear(g_title.pad, 0);
      r = play(p);
      sound_set_volume(g_title.sound, 0x40, 0x7F);
      delete p;
      p = nullptr;
      if (r != 2) {
        // Part 3 (#4506) while the field program loads.
        auto* p3 = prologue_create(new Prologue);
        std::vector<u8> part3 = load_whole(0x119A);
        prologue_set_script(p3, part3.data());
        field.assign(static_cast<size_t>(loader->entry_size(0xC1C)), 0);
        async_load(0xC1C, field.data(), -1);
        pad_clear(g_title.pad, 0);
        while (prologue_update(p3, 1) == 0) {
          load_queue_update();
          if (p3->has_movie) movie_present(p3);
        }
        delete p3;
      }
    }
  }
  g_title.prologue_data.clear();
  if (field.empty()) {
    field.assign(static_cast<size_t>(loader->entry_size(0xC1C)), 0);
    async_load(0xC1C, field.data(), -1);
  }
  sound_stop(g_title.sound);
  psyq::SetDispMask(0);
  while (loader->poll() != 0) {
  }
  do {
    load_queue_update();
  } while (!async_load_done(0));
  // The field program image (#3100) is what the console runs next; the port runs its
  // native translation instead.
  boot_run_main_program(sys, Program::Field);
}

}  // namespace

void title_program_main(System* sys) {
  g_title.system = sys;
  g_title.pad = &sys->pad;
  g_title.loader = &sys->loader;
  g_title.handoff = &sys->handoff;
  g_title.sound = &sys->sound;
  g_title.memcard = memcard_create();
  memcard_update(g_title.memcard);

  // Sound bank (#8) and its sequence data (#9), read in one pass.
  const int size8 = g_title.loader->entry_size(8), size9 = g_title.loader->entry_size(9);
  std::vector<u8> sound(static_cast<size_t>(size8 + size9));
  g_title.loader->select_entry(8);
  g_title.loader->start_read(sound.data(), size8 + size9);
  // TODO(port): controller vibration setup (0x8001408c, 0x80013c0c/60/a4).
  while (g_title.loader->poll() != 0) {
    memcard_update(g_title.memcard);
    rng_next();
  }
  sound_load_bank(g_title.sound, sound.data(), 0, 0);
  while (sound_bank_busy(g_title.sound) != 0) {
    memcard_update(g_title.memcard);
    rng_next();
  }
  if (auto seq = disc::slz::unpack_block(std::span<const u8>(sound).subspan(static_cast<size_t>(size8)), 0, 0,
                                         nullptr)) {
    sound_set_sequences(g_title.sound, seq->data());
  }
  sound_set_volume(g_title.sound, 0x40, 0x7F);
  title_init_config();
  if (runtime::Runtime::get().debug_start_map >= 0) {
    // Debug start (--map N): New Game as Claude, straight into the field; the field's
    // new-game state block puts him on map N.
    auto* records = static_cast<u8*>(handoff_get(g_title.handoff, 4));
    if (records[0x24] == 0) std::strcpy(reinterpret_cast<char*>(records + 0x24), "Claude");
    g_title.config[0x45] = 0;
    boot_run_main_program(sys, Program::Field);
  }
  g_title.entry12.assign(static_cast<size_t>(g_title.loader->entry_size(0x0C)), 0);

  auto intro = std::make_unique<TitleIntro>();
  title_intro_create(intro.get());
  if (g_title_shown == 1) {
    g_title.loader->select_entry(0x0C);
    g_title.loader->start_read(g_title.entry12.data(), 0);
  } else {
    title_intro_sequence(intro.get());
  }
  while (g_title.loader->poll() != 0) {
  }
  pad_set_option(g_title.pad, 0);
  g_title_shown = 1;

  // The title menu loop (0x80041db0): back to the title after the attract movie
  // and the Voice Collection; on to the field program after New Game / Continue.
  for (;;) {
    const int choice = title_screen(intro.get());
    if (g_title.config) g_title.config[0x17F] = static_cast<u8>(choice);
    if (choice == kTitleNewGame) {
      load_queue_reset();
      if (loader_is_disc(*g_title.loader, 1) != 1) {
        // TODO(port): the "insert disc 1" prompt (0x8003c5ec), then entries #14-#16 again.
        runtime::Runtime::get().log_once("disc1_prompt", "New Game needs disc 1; the swap prompt is not ported");
      }
      RenderContext* holder = nullptr;  // the original's 4-byte object
      char name[24] = {};
      if (new_game_sequence(&holder, name) == 0) start_new_game(sys, name);
      load_queue_reset();
      load_queue_suspend();
      g_title.prologue_data.clear();
      continue;
    }
    if (choice == kTitleContinue) {
      // TODO(port): load-game screen 0x800317e0 (needs the memory card).
      runtime::Runtime::get().log_once("continue", "Continue chosen: the load screen is not ported yet");
      continue;
    }
    if (choice == kTitleVoiceCollection) {
      // TODO(port): Voice Collection 0x8003fb78.
      runtime::Runtime::get().log_once("voice", "Voice Collection is not ported yet; back to the title");
    }
  }
}

void title_entry(System* sys) { title_program_main(sys); }

}  // namespace ss::game
