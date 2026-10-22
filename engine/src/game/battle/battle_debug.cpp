#include "game/battle/battle_debug.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "disc/slz.h"
#include "game/battle/battle.h"
#include "game/battle/camera.h"
#include "game/battle/unit.h"
#include "game/boot/sound.h"
#include "game/boot/rng.h"
#include "game/common/battle_setup.h"
#include "game/field/encounter.h"
#include "game/field/field.h"
#include "game/title/title_program.h"
#include "runtime/runtime.h"

namespace ss::game::battle {
namespace {

u32 dump32(const std::vector<u8>& ram, u32 address) {
  const u32 at = address & 0x1FFFFF;
  u32 v = 0;
  if (at + 4 <= ram.size()) std::memcpy(&v, ram.data() + at, 4);
  return v;
}

// Copies handoff 'tag' of the dump's System (0x8002b550 -> registry at +0x5a20) over the
// port's block of the same tag.
bool copy_handoff(const std::vector<u8>& ram, int tag, size_t bytes) {
  const u32 sys = dump32(ram, 0x8002B550);
  const u32 reg = sys + 0x5A20;
  for (int i = 0; i < 16; ++i) {
    if (static_cast<int>(dump32(ram, reg + 64 + 4 * i)) != tag) continue;
    const u32 at = dump32(ram, reg + 4 * i) & 0x1FFFFF;
    auto* dst = static_cast<u8*>(handoff_get(&get_system()->handoff, tag));
    if (dst == nullptr || at + bytes > ram.size()) return false;
    std::memcpy(dst, ram.data() + at, bytes);
    return true;
  }
  return false;
}

}  // namespace

void battle_debug_frame() {
  static const bool cam_on = std::getenv("SS_BATTLE_CAM") != nullptr;
  if (cam_on) {
    // The camera / clock fields explore.lua's BW_DENSE logs (in-frame values, before the
    // end-of-frame tick).
    const BattleGlobals& g = bg();
    const Camera* c = g.camera;
    std::fprintf(stderr, "[battle] cam v%llu f %d ticks %d st %d fl %d cam %d %d rem %d spd %d ct %d\n",
                 static_cast<unsigned long long>(runtime::Runtime::get().vblanks()), g.battle_frames, g.frame_ticks,
                 g.state, g.flags, c ? c->mode : -1, c ? c->step : -1, c ? c->remaining : 0, c ? c->speed : 0,
                 c ? c->ticks : 0);
  }
  static int box_lo = -1, box_hi = -1;
  static const bool box_init = [] {
    if (const char* b = std::getenv("SS_BATTLE_BOXES")) std::sscanf(b, "%d:%d", &box_lo, &box_hi);
    return true;
  }();
  (void)box_init;
  if (box_lo >= 0 && bg().battle_frames >= box_lo && bg().battle_frames <= box_hi) {
    // Each unit's box mask (+0x6a) and enabled boxes in units (explore.lua's BW_BOXES).
    auto line = [](const char* tag, int i, const Unit* u) {
      if (u == nullptr) return;
      std::fprintf(stderr, "[battle] box v%d %s%d m%x x%d y%d z%d an%d fr%d tl%d te%d lp%d", bg().battle_frames, tag, i,
                   u->box_mask, u->pos[0] >> 12, u->pos[1] >> 12, u->pos[2] >> 12, u->anim, u->frame, u->ticks_left,
                   u->ticks_elapsed, u->loops);
      for (int b = 0; b < 4; ++b) {
        if ((u->box_mask & (1 << b)) == 0) continue;
        const s32* lo = b < 2 ? u->body_min[b] : u->attack_min[b - 2];
        const s32* hi = b < 2 ? u->body_max[b] : u->attack_max[b - 2];
        std::fprintf(stderr, " b%d %d,%d,%d..%d,%d,%d", b, lo[0] >> 12, lo[1] >> 12, lo[2] >> 12, hi[0] >> 12,
                     hi[1] >> 12, hi[2] >> 12);
      }
      std::fprintf(stderr, "\n");
    };
    for (int i = 0; i < 8; ++i) line("e", i, bg().enemy_units[i]);
    for (int i = 0; i < 4; ++i) line("p", i, bg().party_units[i]);
  }
  static const bool on = std::getenv("SS_BATTLE_RNG") != nullptr;
  if (!on) return;
  std::fprintf(stderr, "[battle] vblank %llu state %d rng %u\n",
               static_cast<unsigned long long>(runtime::Runtime::get().vblanks()), bg().state, rng_draws());
}

int battle_debug_frame_wait(int n) {
  static const std::vector<int> waits = [] {
    std::vector<int> w;
    if (const char* s = std::getenv("SS_BATTLE_TICKS")) {
      for (const char* p = s; *p != 0;) {
        w.push_back(std::atoi(p));
        while (*p != 0 && *p != ',') ++p;
        if (*p == ',') ++p;
      }
    }
    return w;
  }();
  if (n >= 0 && static_cast<size_t>(n) < waits.size() && waits[static_cast<size_t>(n)] > 0)
    return waits[static_cast<size_t>(n)];
  return 2;
}

void battle_debug_exit() {
  const char* path = std::getenv("SS_BATTLE_DUMP");
  std::fprintf(stderr, "[battle] exit: rng draws %u\n", rng_draws());
  if (path == nullptr || *path == 0) return;
  std::ofstream out(path, std::ios::binary);
  const std::pair<int, size_t> blocks[] = {{2, 0x2A0}, {3, 0x300}, {4, 0x680}, {6, 0xC28}};
  for (const auto& [tag, bytes] : blocks) {
    const auto* p = static_cast<const char*>(handoff_get(&get_system()->handoff, tag));
    if (p != nullptr) out.write(p, static_cast<std::streamsize>(bytes));
  }
  std::fprintf(stderr, "[battle] handoffs 2/3/4/6 written to %s\n", path);
}

void battle_debug_set_end(const std::string& end) {
  if (end.empty()) return;
  int frames = 0, outcome = 0;
  std::sscanf(end.c_str(), "%i:%i", &frames, &outcome);
  battle_debug().auto_end_frames = frames;
  battle_debug().auto_end_outcome = static_cast<u8>(outcome);
}

Program battle_debug_start(const std::string& spec, const std::string& end) {
  int encounter = 1, stage = 3;
  std::sscanf(spec.c_str(), "%i:%i", &encounter, &stage);
  battle_debug_set_end(end);
  // What the title program leaves for a new game: config, party slots, character
  // records and the inventory (create_game_state 0x80061c58), with Claude in slot 0.
  create_game_state();
  System* sys = get_system();
  if (sys->sound.effect_scripts == nullptr) {
    // What the title program loads at boot: the system sound bank (#8) and the effect
    // sequences (#9), which the battle's menu and result sounds (table 0) play from.
    std::vector<u8> bank(static_cast<size_t>(sys->loader.entry_size(8)));
    std::vector<u8> seqs(static_cast<size_t>(sys->loader.entry_size(9)));
    if (!bank.empty() && !seqs.empty()) {
      sys->loader.load_sync(bank.data(), 8, 0);
      sys->loader.load_sync(seqs.data(), 9, 0);
      sound_load_bank(&sys->sound, bank.data(), 0, 0);
      while (sound_bank_busy(&sys->sound) != 0) {
      }
      if (auto seq = disc::slz::unpack_block(seqs, 0, 0, nullptr)) sound_set_sequences(&sys->sound, seq->data());
    }
  }
  HandoffRegistry* reg = &get_system()->handoff;
  auto* party = static_cast<u8*>(handoff_get(reg, 3));
  auto* records = static_cast<u8*>(handoff_get(reg, 4));
  party_slot_init(party, records, 1);
  auto* setup = new BattleSetup;
  bool have_setup = false;
  if (const char* path = std::getenv("SS_BATTLE_RAM"); path != nullptr && *path != 0) {
    std::ifstream in(path, std::ios::binary);
    std::vector<u8> ram((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (ram.size() >= 0x200000) {
      const bool ok = copy_handoff(ram, 2, 0x2A0) && copy_handoff(ram, 3, 0x300) && copy_handoff(ram, 4, 0x680) &&
                      copy_handoff(ram, 6, 0xC28);
      std::fprintf(stderr, "[battle] SS_BATTLE_RAM %s: handoffs %s\n", path, ok ? "copied" : "not found");
      // field_exit_to_program dumps: the field's setup block (0x800752b8 points at it).
      const u32 p = dump32(ram, 0x800752B8) & 0x1FFFFF;
      const s32 enc = p != 0 ? static_cast<s32>(dump32(ram, 0x80000000u | p)) : 0;
      if (p != 0 && p + 0xC0 <= ram.size() && enc > 0 && enc < 0x240) {
        std::memcpy(setup->raw.data(), ram.data() + p, 0xC0);
        for (int i = 0; i < 13; ++i) setup->set(0x74 + 4 * i, 0);
        have_setup = true;
        std::fprintf(stderr, "[battle] setup from the dump: encounter %d stage %d\n", setup->encounter(),
                     setup->stage());
        // The random generator as the field left it.
        u32 table[56];
        for (int i = 0; i < 56; ++i) table[i] = dump32(ram, 0x8002B468 + i * 4);
        rng_restore(table, static_cast<s32>(dump32(ram, 0x8002B464)));
      }
    }
  }
  if (!have_setup) {
    // As the field builds it (battle_build_load_list reads the party through the field's
    // bound globals).
    field::field_bind_system(nullptr);
    field::battle_build_load_list(setup, encounter, stage);
  }
  if (std::getenv("SS_BATTLE_STRONG") != nullptr) {
    // Debugging: a sturdy party (x10 HP, x8 attack) to reach the victory path.
    for (int i = 0; i < 4; ++i) {
      u8* slot = party + i * 0x60;
      s16 id;
      std::memcpy(&id, slot, 2);
      if (id < 1) continue;
      for (int off : {0x14, 0x18, 0x1C}) {
        s32 v;
        std::memcpy(&v, slot + off, 4);
        v *= 10;
        std::memcpy(slot + off, &v, 4);
      }
      for (int off : {0x2A, 0x2C, 0x2E}) {
        s16 v;
        std::memcpy(&v, slot + off, 2);
        v = static_cast<s16>(v * 8);
        std::memcpy(slot + off, &v, 2);
      }
    }
  }
  if (const char* pokes = std::getenv("SS_BATTLE_POKE"); pokes != nullptr) {
    // Debugging: TAG:OFFSET=VALUE/SIZE,... written into handoff TAG (as a Redux BW_POKE
    // after field_exit), e.g. 4:0xcc=10/1 gives slot 0 killer move byte 10 on L1.
    std::string list(pokes);
    size_t at = 0;
    while (at < list.size()) {
      size_t stop = list.find(',', at);
      if (stop == std::string::npos) stop = list.size();
      const std::string item = list.substr(at, stop - at);
      at = stop + 1;
      unsigned tag = 0, size = 0;
      int off = 0;
      long long value = 0;
      if (std::sscanf(item.c_str(), "%u:%i=%lli/%u", &tag, &off, &value, &size) != 4) continue;
      auto* block = static_cast<u8*>(handoff_get(reg, static_cast<int>(tag)));
      if (block == nullptr || off < 0 || (size != 1 && size != 2 && size != 4)) continue;
      const u32 v = static_cast<u32>(value);
      std::memcpy(block + off, &v, size);
      std::fprintf(stderr, "[battle] poke handoff %u +0x%x = 0x%x (%u bytes)\n", tag, off, v, size);
    }
  }
  handoff_store(reg, setup, 5);
  runtime::Runtime::get().trace("battle debug start");
  return Program::Battle;
}

}  // namespace ss::game::battle
