#include "game/battle/battle_debug.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "disc/slz.h"
#include "game/battle/battle.h"
#include "game/battle/camera.h"
#include "game/battle/killer.h"
#include "game/battle/render.h"
#include "game/battle/result.h"
#include "game/battle/shot.h"
#include "game/battle/unit.h"
#include "game/boot/sound.h"
#include "game/boot/rng.h"
#include "game/common/battle_setup.h"
#include "game/field/characters.h"
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

void battle_debug_trail(const RibbonTrail* t) {
  static int lo = -1, hi = -1;
  static const bool on = [] {
    const char* v = std::getenv("SS_BATTLE_TRAIL");
    return v != nullptr && std::sscanf(v, "%i:%i", &lo, &hi) == 2;
  }();
  if (!on) return;
  const BattleGlobals& g = bg();
  if (g.battle_frames < lo || hi < g.battle_frames) return;
  int idx = -1;
  for (int i = 0; i < 160; ++i)
    if (static_cast<const BObj*>(shot_at(i)) == t->source) idx = i;
  const auto* src = t->source;
  const Shot* sh = idx >= 0 ? shot_at(idx) : nullptr;
  const int h1 = (t->head + 1) & 0x1F;
  std::fprintf(stderr, "[trail] b%05d %p len %d head %d stop %d src %d kind %d w %d h %d a1 %d p1 %d %d %d\n",
               g.battle_frames, static_cast<const void*>(t), t->length, t->head, t->stop, idx,
               sh != nullptr ? sh->kind : -1, src != nullptr ? src->size[0] : 0, src != nullptr ? src->size[1] : 0,
               t->alpha[h1], t->hist[h1][0], t->hist[h1][1], t->hist[h1][2]);
}

void battle_debug_frame() {
  // (Menu agent) SS_BATTLE_UNITS=N: up to battle frame N, the generator (index and FNV-1a
  // over the 56-word table, as event_sweep.cpp and lateref.lua print it), its draws, and
  // every unit's position and HP, at the end of each frame's logic (where lateref.lua
  // breaks in the original: 0x8004693c), for comparing a battle's opening with PCSX-Redux.
  static const int units_upto = [] {
    const char* v = std::getenv("SS_BATTLE_UNITS");
    return v != nullptr ? std::atoi(v) : -1;
  }();
  if (bg().battle_frames <= units_upto) {
    u32 table[56];
    s32 index = 0;
    rng_save(table, &index);
    u32 h = 0x811c9dc5u;
    for (u32 w : table)
      for (int b = 0; b < 4; ++b) h = (h ^ (w >> (8 * b) & 0xFF)) * 0x01000193u;
    std::string line;
    char part[96];
    for (int i = 0; i < 4; ++i) {
      if (const PartyUnit* u = bg().party_units[static_cast<size_t>(i)]) {
        std::snprintf(part, sizeof part, "%sp%d %d,%d,%d hp %d", line.empty() ? "" : " | ", i, u->pos[0], u->pos[1], u->pos[2],
                      u->st()->hp);
        line += part;
      }
    }
    for (int i = 0; i < 8; ++i) {
      if (const EnemyUnit* u = bg().enemy_units[static_cast<size_t>(i)]) {
        std::snprintf(part, sizeof part, "%se%d %d,%d,%d hp %d", line.empty() ? "" : " | ", i, u->pos[0], u->pos[1], u->pos[2],
                      u->st()->hp);
        line += part;
      }
    }
    std::fprintf(stderr, "[bunits] b %d v %llu rng %d %08x draws %u; %s\n", bg().battle_frames,
                 static_cast<unsigned long long>(runtime::Runtime::get().vblanks()), index, h, rng_draws(), line.c_str());
  }
  // (Menu agent) SS_BATTLE_HP1=F: from battle frame F on, monsters above 1 HP drop to 1 (as
  // lateref.lua's SS_ENEMY_HP1), to end a long fight after the compared opening.
  static const int hp1_from = [] {
    const char* v = std::getenv("SS_BATTLE_HP1");
    return v != nullptr ? std::atoi(v) : -1;
  }();
  // (Deathlink agent) SS_BATTLE_DEBUG_ENC=E,...: SS_BATTLE_HP1 and SS_BATTLE_GOD act only in
  // those encounters (a story scene's earlier fight won, the one under test left alone).
  static const std::vector<int> only_enc = [] {
    std::vector<int> list;
    if (const char* v = std::getenv("SS_BATTLE_DEBUG_ENC"))
      for (const char* p = v;; ++p) {  // (p steps over each comma)
        char* end = nullptr;
        const long n = std::strtol(p, &end, 0);
        if (end == p) break;
        list.push_back(static_cast<int>(n));
        p = end;
        if (*p != ',') break;
      }
    return list;
  }();
  const bool enc_on = only_enc.empty() || std::find(only_enc.begin(), only_enc.end(), bg().encounter_id) != only_enc.end();
  if (enc_on && hp1_from >= 0 && bg().battle_frames >= hp1_from) {
    for (EnemyUnit* u : bg().enemy_units)
      if (u != nullptr && u->st()->hp > 1) u->st()->hp = 1;
  }
  static const int god = [] {
    const char* v = std::getenv("SS_BATTLE_GOD");
    return v == nullptr ? 0 : std::max(1, std::atoi(v));
  }();
  if (god != 0 && enc_on) {
    // Debugging (sweeps): the standing party members' HP back to the maximum every frame,
    // so long fights run on to their end. (Deathlink agent) SS_BATTLE_GOD=HP above 1 also
    // holds the maximum at HP, so a weak party outlasts one-hit kills.
    for (PartyUnit* u : bg().party_units)
      if (u != nullptr && u->st()->hp > 0) {
        if (god > 1) u->st()->hp_max = god;
        u->st()->hp = u->st()->hp_max;
      }
  }
  static const bool cam_on = std::getenv("SS_BATTLE_CAM") != nullptr;
  if (cam_on) {
    // The camera / clock fields explore.lua's BW_DENSE logs (in-frame values, before the
    // end-of-frame tick).
    const BattleGlobals& g = bg();
    const Camera* c = g.camera;
    std::fprintf(stderr, "[battle] cam v%llu f %d ticks %d st %d fl %d cam %d %d rem %d spd %d ct %d res %d %d\n",
                 static_cast<unsigned long long>(runtime::Runtime::get().vblanks()), g.battle_frames, g.frame_ticks,
                 g.state, g.flags, c ? c->mode : -1, c ? c->step : -1, c ? c->remaining : 0, c ? c->speed : 0,
                 c ? c->ticks : 0, battle_result_screen() != nullptr, g.result_started);
    // The view (explore.lua BW_VIEW): render eye / target, the camera's eye / look-at, zoom.
    const ViewState& vw = view();
    std::fprintf(stderr, "[battle] view f %d m %d eye %d %d %d tgt %d %d %d ceye %d %d %d clook %d %d %d zoom %d\n",
                 g.battle_frames, vw.view_mode, vw.view_eye[0], vw.view_eye[1], vw.view_eye[2], vw.view_target[0],
                 vw.view_target[1], vw.view_target[2], vw.cam_eye[0], vw.cam_eye[1], vw.cam_eye[2], vw.cam_lookat[0],
                 vw.cam_lookat[1], vw.cam_lookat[2], vw.zoom);
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
  static int shot_lo = -1, shot_hi = -1;
  static const bool shot_init = [] {
    if (const char* b = std::getenv("SS_BATTLE_SHOTS")) std::sscanf(b, "%d:%d", &shot_lo, &shot_hi);
    return true;
  }();
  (void)shot_init;
  if (shot_lo >= 0 && bg().battle_frames >= shot_lo && bg().battle_frames <= shot_hi) {
    // Every live Shot (explore.lua's BW_SHOTS): index, kind, position, velocity, hit flag.
    std::string line;
    char buf[160];
    for (int i = 0; i < 160; ++i) {
      const Shot* s = shot_at(i);
      if (s == nullptr || s->kind == 0) continue;
      std::snprintf(buf, sizeof buf, "%s%d:k%x x%d y%d z%d v%d,%d,%d h%d", line.empty() ? "" : " | ", i, s->kind,
                    s->pos[0] >> 12, s->pos[1] >> 12, s->pos[2] >> 12, s->vel[0], s->vel[1], s->vel[2], s->hit_landed);
      line += buf;
    }
    std::fprintf(stderr, "[battle] shots v%d %s\n", bg().battle_frames, line.c_str());
  }
  static const bool on = std::getenv("SS_BATTLE_RNG") != nullptr;
  if (!on) return;
  std::fprintf(stderr, "[battle] vblank %llu state %d rng %u\n",
               static_cast<unsigned long long>(runtime::Runtime::get().vblanks()), bg().state, rng_draws());
}

// (World agent) SS_BATTLE_TICK_MAP="F:T,...": after a frame that leaves g_battle_frames at F,
// wait T vblanks (the frames the console ran slow, from the Redux driver's BW_TICK_LOG), so
// a reference with slowdown can be followed exactly. Returns 0 when F is not in the map.
int battle_debug_tick_map(s32 battle_frames) {
  static const std::unordered_map<s32, int> map = [] {
    std::unordered_map<s32, int> m;
    if (const char* s = std::getenv("SS_BATTLE_TICK_MAP")) {
      for (const char* p = s; *p != 0;) {
        int f = 0, t = 0;
        if (std::sscanf(p, "%d:%d", &f, &t) == 2) m[f] = t;
        while (*p != 0 && *p != ',') ++p;
        if (*p == ',') ++p;
      }
    }
    return m;
  }();
  const auto it = map.find(battle_frames);
  return it != map.end() ? it->second : 0;
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

void battle_debug_loaded() {
  static const int skip = [] {
    const char* e = std::getenv("SS_BATTLE_RNG_SKIP");
    return e != nullptr ? std::atoi(e) : 0;
  }();
  for (int i = 0; i < skip; ++i) rng_next();
  if (skip != 0) std::fprintf(stderr, "[battle] loaded: %d extra rng draws (SS_BATTLE_RNG_SKIP)\n", skip);
}

void battle_debug_exit() {
  const BattleGlobals& g = bg();
  std::fprintf(stderr, "[battle] exit: rng draws %u\n", rng_draws());
  std::fprintf(stderr, "[battle] exit: encounter %d outcome %d battle frames %d fight frames %d\n", g.encounter_id,
               g.outcome, g.battle_frames, g.fight_frames);
  if (const char* path = std::getenv("SS_BATTLE_DUMP"); path != nullptr && *path != 0) {
    std::ofstream out(path, std::ios::binary);
    const std::pair<int, size_t> blocks[] = {{2, 0x2A0}, {3, 0x300}, {4, 0x680}, {6, 0xC28}};
    for (const auto& [tag, bytes] : blocks) {
      const auto* p = static_cast<const char*>(handoff_get(&get_system()->handoff, tag));
      if (p != nullptr) out.write(p, static_cast<std::streamsize>(bytes));
    }
    std::fprintf(stderr, "[battle] handoffs 2/3/4/6 written to %s\n", path);
  }
  // SS_BATTLE_QUIT_AT_EXIT=1: the run ends with the battle (sweeps don't go on to the field).
  if (std::getenv("SS_BATTLE_QUIT_AT_EXIT") != nullptr) throw runtime::QuitRequested{};
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
  if (const char* list = std::getenv("SS_BATTLE_PARTY"); list != nullptr && *list != 0) {
    // Debugging: "1,2,3,4" fills party slots 0..3 with those characters' starting data, as
    // the field's party_slot_init does when one joins (0 leaves a slot empty).
    int ids[4] = {0, 0, 0, 0};
    const int n = std::sscanf(list, "%i,%i,%i,%i", &ids[0], &ids[1], &ids[2], &ids[3]);
    for (int i = 0; i < 4; ++i) {
      if (i < n && ids[i] >= 1 && ids[i] <= 12) {
        field::party_slot_init(party + i * 0x60, records + i * 0xD0, ids[i]);
      } else if (i < n || i > 0) {
        std::memset(party + i * 0x60, 0, 0x60);
        std::memset(records + i * 0xD0, 0, 0xD0);
      }
    }
    std::fprintf(stderr, "[battle] party %d %d %d %d\n", ids[0], ids[1], ids[2], ids[3]);
  }
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
    // The field always has the right disc in; here the boot disc may lack the battle's
    // entries (disc 2's areas), so the other disc goes in, as the field's disc swap does.
    auto lacks_entries = [&]() {
      for (int i = 0; i < 12 && setup->id(i) != 0; ++i)
        if (setup->entry(i) > 0 && sys->loader.entry_size(setup->entry(i)) == 0) return setup->entry(i);
      return 0;
    };
    if (const int missing = lacks_entries(); missing != 0) {
      const int other = sys->loader.entry_size(5 + 2) != 0 ? 1 : 2;  // marker entry 5 + disc
      const bool swapped = loader_insert_disc(sys->loader, other);
      std::fprintf(stderr, "[battle] entry %d is not on this disc: disc %d %s\n", missing, other,
                   swapped && lacks_entries() == 0 ? "inserted" : "does not have it either");
    }
  }
  if (const char* strong = std::getenv("SS_BATTLE_STRONG"); strong != nullptr) {
    // Debugging: a sturdy party (x10 HP, x8 attack; "H:A[:D]" gives other factors, D for
    // defence) to reach the victory path. A scaled HP stops at the game's 9999 (the party AI
    // assumes it: pc_ai_choose_heal starts its search below 10000).
    int hp_mul = 10, atk_mul = 8, def_mul = 1;
    if (std::strchr(strong, ':') != nullptr) std::sscanf(strong, "%i:%i:%i", &hp_mul, &atk_mul, &def_mul);
    for (int i = 0; i < 4; ++i) {
      u8* slot = party + i * 0x60;
      s16 id;
      std::memcpy(&id, slot, 2);
      if (id < 1) continue;
      for (int off : {0x14, 0x18, 0x1C}) {
        s32 v;
        std::memcpy(&v, slot + off, 4);
        v = std::min(v * hp_mul, 9999);
        std::memcpy(slot + off, &v, 4);
      }
      for (int off : {0x2A, 0x2C, 0x2E}) {
        s16 v;
        std::memcpy(&v, slot + off, 2);
        v = static_cast<s16>(std::min(v * atk_mul, 0x7FFF));
        std::memcpy(slot + off, &v, 2);
      }
      for (int off : {0x30, 0x32, 0x34}) {
        s16 v;
        std::memcpy(&v, slot + off, 2);
        v = static_cast<s16>(std::min(v * def_mul, 0x7FFF));
        std::memcpy(slot + off, &v, 2);
      }
    }
  }
  if (const char* copies = std::getenv("SS_BATTLE_COPY"); copies != nullptr) {
    // Debugging: TAG:SRC:DST:LEN,... copies LEN bytes inside handoff TAG (after
    // SS_BATTLE_STRONG, before the pokes), e.g. "3:0:0x60:0x60,4:0:0xd0:0xd0" gives party
    // slot 1 a copy of slot 0's stats and record (explore.lua's BW_COPY does the same).
    std::string list(copies);
    size_t at = 0;
    while (at < list.size()) {
      size_t stop = list.find(',', at);
      if (stop == std::string::npos) stop = list.size();
      const std::string item = list.substr(at, stop - at);
      at = stop + 1;
      unsigned tag = 0;
      int src = 0, dst = 0, len = 0;
      if (std::sscanf(item.c_str(), "%u:%i:%i:%i", &tag, &src, &dst, &len) != 4) continue;
      auto* block = static_cast<u8*>(handoff_get(reg, static_cast<int>(tag)));
      if (block == nullptr || src < 0 || dst < 0 || len <= 0) continue;
      std::memmove(block + dst, block + src, static_cast<size_t>(len));
      std::fprintf(stderr, "[battle] copy handoff %u +0x%x -> +0x%x (0x%x bytes)\n", tag, src, dst, len);
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
