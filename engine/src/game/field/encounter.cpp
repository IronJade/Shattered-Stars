#include "game/field/encounter.h"

#include "game/field/worldmap.h"

#include <cstdio>
#include <cstring>
#include <memory>

#include "game/boot/rng.h"
#include "game/boot/sound.h"
#include "game/common/math.h"
#include "game/field/callways.h"
#include "game/field/post_battle.h"
#include "game/field/voice_queue.h"
#include "game/field/actors.h"
#include "game/field/bg.h"
#include "game/field/camera.h"
#include "game/field/effects.h"
#include "game/field/fade.h"
#include "game/field/load_queue.h"
#include "game/field/map.h"
#include "game/field/message.h"
#include "game/field/music.h"
#include "game/field/screen.h"
#include "game/field/script_vm.h"
#include "game/field/special.h"
#include "game/field/stash.h"
#include "runtime/libgpu.h"
#include "runtime/libgte.h"
#include "runtime/runtime.h"

namespace ss::game::field {
namespace {

BattlePreloads g_preloads;
BattleTransition* g_transition = nullptr;  // 0x800752d0
std::vector<u8> g_battle_program;          // 0x80075350: #2129 as read (the port runs native code)
u8 g_battle_program_loaded = 0;            // 0x8007534d

constexpr int kBattleProgramEntry = 0x851;  // #2129

s16 rd16s(const u8* p) {
  s16 v;
  std::memcpy(&v, p, 2);
  return v;
}
s32 rd32(const u8* p) {
  s32 v;
  std::memcpy(&v, p, 4);
  return v;
}

void todo(const char* what) {
  runtime::Runtime::get().log_once(std::string("encounter:") + what, std::string("field: not ported yet: ") + what);
}

// The party slot records (handoff 3: 8 x 0x60, +0 character id).
s16 slot_character(int slot) { return rd16s(globals().party + slot * 0x60); }

// ---- The transition's drawing ---------------------------------------------------

// 0x800467e8: copies the displayed frame (320x224) to VRAM (0x140, 0) with STP set on
// every pixel, as the source of the effect's textured tiles.
void screen_capture_to_vram() {
  Screen* s = field_screen();
  const s16 shown_y = s->current == &s->buffers[0] ? 0xE0 : 0;  // 0x80075730[current == buffer 0]
  std::vector<u16> pixels(0x140 * 0xE0);
  psyq::RECT r{0, shown_y, 0x140, 0xE0};
  psyq::StoreImage(&r, pixels.data());
  psyq::DrawSync(0);
  for (u16& p : pixels) p |= 0x8000;
  r.x = 0x140;
  r.y = 0;
  psyq::LoadImage(&r, pixels.data());
  psyq::DrawSync(0);
}

// 0x800466b0: a SPRT of the previous frame at 'brightness' with its own texture page,
// at OT 0x81e.
void screen_add_sprt_tpage(s16 x, s16 y, u8 u, u8 v, s16 w, s16 h, int tpage, u8 brightness) {
  Screen* s = field_screen();
  auto* sp = s->alloc_prims<psyq::SPRT>();
  auto* mode = s->alloc_prims<psyq::DR_MODE>();
  if (sp == nullptr || mode == nullptr) return;
  psyq::setSprt(sp);
  sp->x0 = x;
  sp->y0 = y;
  sp->u0 = u;
  sp->v0 = v;
  sp->w = w;
  sp->h = h;
  sp->r0 = sp->g0 = sp->b0 = brightness;
  psyq::addPrim(s->ot_entry(0x81E), sp);
  psyq::SetDrawMode(mode, 0, 0, tpage, nullptr);
  psyq::addPrim(s->ot_entry(0x81E), mode);
}

// 0x80046544: the previous frame (the other buffer) drawn over the new one at
// 'brightness': the motion-blur trail behind the effect.
void screen_draw_previous_frame(u8 brightness) {
  Screen* s = field_screen();
  if (s->current == &s->buffers[0]) {
    // The other buffer starts at y 0xe0: its first 0x20 lines are in texture page row 0,
    // the rest in row 256.
    screen_add_sprt_tpage(0, 0, 0, 0xE0, 0x100, 0x20, 0x100, brightness);
    screen_add_sprt_tpage(0x100, 0, 0, 0xE0, 0x40, 0x20, 0x104, brightness);
    screen_add_sprt_tpage(0, 0x20, 0, 0, 0x100, 0xC0, 0x110, brightness);
    screen_add_sprt_tpage(0x100, 0x20, 0, 0, 0x40, 0xC0, 0x114, brightness);
  } else {
    screen_add_sprt_tpage(0, 0, 0, 0, 0x100, 0xE0, 0x100, brightness);
    screen_add_sprt_tpage(0x100, 0, 0, 0, 0x40, 0xE0, 0x104, brightness);
  }
}

// 0x80045d98: types 0 / 2. The captured screen as 4x4 tiles of 80x56, rotated and
// pushed away through the GTE, over the fading trail.
void draw_swirl(BattleTransition* t, bool long_form) {
  const int end = long_form ? 0xF0 : 0x5F;
  const int fade_from = long_form ? 0xDE : 0x4D;
  if (t->timer == 0) {
    screen_set_clear_colour(field_screen(), 0, 0, 0);
    t->angle = 0;
    screen_capture_to_vram();
  }
  int trail;
  if (t->timer < fade_from) {
    trail = 0x80 - (t->timer * 0x18) / fade_from;
  } else {
    trail = 0x80 - ((t->timer - fade_from) * 0x80) / ((end - fade_from) + 10);
    if (trail < 0) trail = 0;
  }
  screen_draw_previous_frame(static_cast<u8>(trail));
  psyq::SetGeomOffset(0xA0, 0x70);
  psyq::SetGeomScreen(0x200);
  const s32 c = 0x1000 - cos4096(static_cast<u32>((t->timer * 0x1000) / 0x168));
  if (!long_form)
    t->angle = t->angle + t->timer + ((c * c) >> 0x11);
  else
    t->angle = t->angle - (t->timer + ((c * c) >> 0x14));
  psyq::SVECTOR rot{0, 0, static_cast<s16>(t->angle), 0};
  psyq::MATRIX m{};
  field_rot_matrix(&rot, &m);  // the field's own RotMatrix (0x8006e52c)
  psyq::VECTOR move{0, 0, t->timer * -3 + 0x200, 0};
  psyq::TransMatrix(&m, &move);
  psyq::SetRotMatrix(&m);
  psyq::SetTransMatrix(&m);
  if (t->timer >= end) {
    if (t->done == 0) {
      t->done = 1;
      psyq::SetDispMask(0);
    }
    return;
  }
  u8 shade = 0x90;
  if (t->timer >= fade_from) shade = static_cast<u8>(0x80 - ((t->timer - fade_from) * 0x80) / ((end - fade_from) - 1));
  Screen* s = field_screen();
  for (int row = 0; row < 4; ++row) {
    const s16 top = static_cast<s16>(-0x70 + 0x38 * row), bottom = static_cast<s16>(-0x38 + 0x38 * row);
    const u8 v0 = static_cast<u8>(1 + 0x38 * row), v1 = static_cast<u8>(0x37 + 0x38 * row);
    for (int col = 0; col < 4; ++col) {
      auto* p = s->alloc_prims<psyq::POLY_FT4>();
      if (p == nullptr) return;
      psyq::setPolyFT4(p);
      psyq::setSemiTrans(p, true);
      p->r0 = p->g0 = p->b0 = shade;
      const s16 left = static_cast<s16>(-0xA0 + 0x50 * col), right = static_cast<s16>(-0x50 + 0x50 * col);
      psyq::SVECTOR a{left, top, 0, 0}, b{right, top, 0, 0}, cc{left, bottom, 0, 0}, d{right, bottom, 0, 0};
      s32 sxy[4], depth = 0, flag = 0;
      psyq::RotTransPers4(&a, &b, &cc, &d, &sxy[0], &sxy[1], &sxy[2], &sxy[3], &depth, &flag);
      auto unpack = [](s32 v, s16& x, s16& y) {
        x = static_cast<s16>(v & 0xFFFF);
        y = static_cast<s16>(static_cast<u32>(v) >> 16);
      };
      unpack(sxy[0], p->x0, p->y0);
      unpack(sxy[1], p->x1, p->y1);
      unpack(sxy[2], p->x2, p->y2);
      unpack(sxy[3], p->x3, p->y3);
      const int x_vram = 0x140 + 0x50 * col;
      const u8 u0 = static_cast<u8>(((0x50 * col) & 0x3F) + 1);
      const u8 u1 = static_cast<u8>(((0x50 * col) & 0x3F) + 0x4F);
      p->u0 = u0, p->v0 = v0;
      p->u1 = u1, p->v1 = v0;
      p->u2 = u0, p->v2 = v1;
      p->u3 = u1, p->v3 = v1;
      p->clut = 0;
      p->tpage = static_cast<u16>(((x_vram & 0x3FF) >> 6) | 0x160);  // 15-bit, abr 3
      psyq::addPrim(s->ot_entry(0x81F), p);
    }
  }
}

// 0x8004623c: type 1. The captured screen as two halves stretching outward and fading.
void draw_split(BattleTransition* t) {
  if (t->timer == 0) {
    screen_set_clear_colour(field_screen(), 0, 0, 0);
    screen_capture_to_vram();
  }
  int trail = 0x80;
  if (t->timer > 0x2E) {
    trail = 0x80 - ((t->timer - 0x2F) * 0x80) / 0x1C;
    if (trail < 0) trail = 0;
  }
  screen_draw_previous_frame(static_cast<u8>(trail));
  if (t->timer >= 0x41) {
    if (t->done == 0) {
      t->done = 1;
      psyq::SetDispMask(0);
    }
    return;
  }
  const u8 shade = t->timer > 0x2E ? static_cast<u8>(0x80 - ((t->timer - 0x2F) * 0x80) / 0x11) : 0x68;
  const s16 k = static_cast<s16>(t->timer);
  Screen* s = field_screen();
  auto half = [&](u16 tpage, s16 x0, s16 x1, u8 u1) {
    auto* p = s->alloc_prims<psyq::POLY_FT4>();
    if (p == nullptr) return;
    psyq::setPolyFT4(p);
    psyq::setSemiTrans(p, true);
    p->r0 = p->g0 = p->b0 = shade;
    p->tpage = tpage;
    p->clut = 0;
    p->x0 = x0, p->y0 = static_cast<s16>(-k);
    p->x1 = x1, p->y1 = static_cast<s16>(-k);
    p->x2 = x0, p->y2 = static_cast<s16>(k + 0xE0);
    p->x3 = x1, p->y3 = static_cast<s16>(k + 0xE0);
    p->u0 = 0, p->v0 = 0;
    p->u1 = u1, p->v1 = 0;
    p->u2 = 0, p->v2 = 0xE0;
    p->u3 = u1, p->v3 = 0xE0;
    psyq::addPrim(s->ot_entry(0x81F), p);
  };
  half(0x165, static_cast<s16>(-k), 0x80, 0x80);
  half(0x167, 0x80, static_cast<s16>(k + 0x140), 0xC0);
}

}  // namespace

BattlePreloads& battle_preloads() { return g_preloads; }
BattleTransition* battle_transition() { return g_transition; }
bool field_battle_program_loaded() { return g_battle_program_loaded != 0; }

// ---- The load list -------------------------------------------------------------

const u8* battle_voice_table(int character) {
  if (character < 1 || character > 12) return nullptr;
  return field_data(0x8007BE00 + 0x18 * (character - 1));
}

u32 battle_pick_intro_voices(BattleSetup* b) {
  // 1-2 random healthy members among slots 0-3 (status +2 & 7 == 0), and a voice entry
  // for each by the encounter's level relative to the party's average.
  const u8* party = globals().party;
  int members = 0, healthy = 0, level_sum = 0;
  for (int slot = 0; slot < 4; ++slot) {
    const u8* m = party + slot * 0x60;
    if (rd16s(m) > 0) {
      ++members;
      level_sum += rd16s(m + 0x26);
      if ((m[2] & 7) == 0) ++healthy;
    }
  }
  if (members == 0 || healthy == 0) return 0;
  const int average = level_sum / members;
  const u16 enc_level = static_cast<u16>(rd16s(field_data(0x8007B998 + b->encounter() * 2)));
  u32 band = static_cast<int>(enc_level) < average - 4 ? 0 : 3;
  if (average + 3 < static_cast<int>(enc_level)) band = 6;
  if ((enc_level & 0x8000) != 0) band = 9;
  if (static_cast<int>(enc_level) < average - 8) band = 0;
  b->set(0xB8, -1);
  b->set(0xBC, -1);
  int picked = 0, repeats = 0;
  for (int tries = 0; tries < 100; ++tries) {
    const int slot = static_cast<int>(rng_range(4));
    const u8* m = party + slot * 0x60;
    if (rd16s(m) <= 0 || (m[2] & 7) != 0) continue;
    if (slot == b->get(0xB8) && repeats < 4) {
      ++repeats;
      continue;
    }
    b->set(0xB8 + 4 * picked, slot);
    ++picked;
    if (members == 1 || picked == 2) break;
  }
  u32 first = 0, second = 0;
  if (b->get(0xB8) >= 0) {
    const u8* table = battle_voice_table(rd16s(party + b->get(0xB8) * 0x60));
    int k = 0;
    while ((static_cast<u32>(rng_next()) & 0x7FFF) >= 0x6CCC) k = k + 1 > 2 ? 0 : k + 1;
    if (table != nullptr) first = static_cast<u16>(rd16s(table + (band + k) * 2));
  }
  if (b->get(0xBC) >= 0) {
    const u8* table = battle_voice_table(rd16s(party + b->get(0xBC) * 0x60));
    int k = 2;
    while ((static_cast<u32>(rng_next()) & 0x7FFF) > 0x5332) k = k != 0 ? k - 1 : 2;
    if (table != nullptr) second = static_cast<u16>(rd16s(table + (band + k) * 2));
  }
  return first | (second == first ? 0 : second << 16);
}

void battle_build_load_list(BattleSetup* b, int encounter, int stage) {
  b->set(0x00, encounter);
  b->set(0x04, stage);
  b->set(0x70, 9);
  const u32 voices = battle_pick_intro_voices(b);
  // Music pack #1977 (0x7b9), or #1969 for the 31 encounters in the table at 0x8007b91c,
  // or a per-encounter pack from the pairs at 0x8007b95c.
  s32 music = 0x7B9;
  for (int i = 0; i < 0x1F; ++i)
    if (encounter == rd16s(field_data(0x8007B91C + i * 2))) music = 0x7B1;
  for (int i = 0; i < 0x1E; i += 2) {
    if (encounter == rd16s(field_data(0x8007B95C + i * 2))) {
      music = rd16s(field_data(0x8007B95E + i * 2));
      break;
    }
  }
  const s32 ids[13] = {music, 0x7BA, stage + 0x543, 0x7BB, 0x7BC, 0x7BD, 0x7BF, encounter + 0x57D, 0x7BE, 0x7C0,
                       static_cast<s32>(voices & 0xFFFF), static_cast<s32>(voices >> 16), 0};
  for (int i = 0; i < 13; ++i) b->set(0x3C + 4 * i, ids[i]);
  if (b->get(0x3C + 4 * 10) != 0) b->set(0x70, 0xB);
  if (b->get(0x3C + 4 * 11) != 0) b->set(0x70, 0xC);
  for (int i = 0; i < 13; ++i) {
    b->set(0x74 + 4 * i, 0);
    b->set(0x08 + 4 * i, b->get(0x3C + 4 * i));
    b->raw[0xA8 + i] = 0;
    b->preloaded[static_cast<size_t>(i)].clear();
  }
}

// ---- Encounters ----------------------------------------------------------------

void field_prepare_encounter(Field* f, int group, int stage) {
  FieldGlobals& g = globals();
  BattlePreloads& bp = g_preloads;
  // Not while a vehicle is in use, not again for the same group, and not while a map
  // transfer's preloads belong to a trigger the leader stands in.
  if (((g.script_flags[1] >> 4) & 1) != 0) return;
  if (g.battle_preload_requested != 0 && bp.last_group == group && bp.last_stage == stage) return;
  if (group >= 0 && preload_get_kind2_tag() != -1 && g_script_vm != nullptr &&
      trigger_contains_leader(g_script_vm, preload_get_kind2_tag()))
    return;
  bp.last_group = group;
  bp.last_stage = stage;
  std::memset(g.field_flags + 0x20, 0, 4);
  int encounter = -group;
  if (group >= 0) {
    // A random member of the area's group (the table at 0x80074fe0 holds 0-terminated
    // s16 lists). With equipment ability bit 8 / 0x10 the roll is repeated while its
    // list INDEX (not the encounter) appears in the 0-terminated list 0x80074cf0 /
    // 0x80074d70, as the original compares it.
    if (group == 0) group = 1;
    auto barred = [](u32 table, int index) {
      const u8* t = field_data(table);
      int i = 0;
      while (rd16s(t + i * 2) != 0 && rd16s(t + i * 2) != index) ++i;
      return rd16s(t + i * 2) != 0;
    };
    for (;;) {
      const u8* list = field_data(static_cast<u32>(rd32(field_data(0x80074FE0 + group * 4))));
      int n = 0;
      while (rd16s(list + n * 2) != 0) ++n;
      const int index = static_cast<int>(rng_range(static_cast<u32>(n)));
      encounter = rd16s(list + index * 2);
      if ((field_ability_bits() & 8) != 0 && barred(0x80074CF0, index)) continue;
      if ((field_ability_bits() & 0x10) != 0 && barred(0x80074D70, index)) continue;
      break;
    }
  }
  BattleSetup& b = g.battle_setup;
  battle_build_load_list(&b, encounter, stage);
  preload_release(-1);
  // Sort the ids for preloading (by archive entry), remembering each one's slot.
  const int count = b.count();
  for (int i = 0; i < count; ++i) bp.order[i] = i;
  for (int i = 0; i < count - 1; ++i) {
    for (int j = i + 1; j < count; ++j) {
      const s32 a = b.get(0x3C + 4 * i), c = b.get(0x3C + 4 * j);
      if (c < a) {
        b.set(0x3C + 4 * i, c);
        b.set(0x3C + 4 * j, a);
        std::swap(bp.order[i], bp.order[j]);
      }
    }
  }
  // As many as fit in free memory are preloaded; consecutive entries share one read.
  // The console measures its heap (minus 0x800761a4 kept free); the host has room for
  // them all. Ten encounters (0x80074534) leave their last two entries to the battle.
  int to_preload = count;
  for (int i = 0; i < 10; ++i)
    if (rd16s(field_data(0x80074534 + i * 2)) == encounter) to_preload -= 2;
  int groups = 0;
  for (int i = 0; i < to_preload; ++i) {
    const s32 entry = b.get(0x3C + 4 * i);
    const s32 bytes = g.loader->entry_size(entry);
    const int slot = bp.order[i];
    if (i < 1 || static_cast<u32>(slot - 10) < 2 || entry - 1 != b.get(0x38 + 4 * i)) {
      bp.pieces[groups] = 1;
      bp.bytes[groups] = bytes;
      bp.entry[groups] = entry;
      ++groups;
    } else {
      bp.bytes[groups - 1] += bytes;
      bp.pieces[groups - 1] += 1;
    }
  }
  g.battle_preload_requested = 1;
  bp.count = static_cast<u8>(groups);
  if (voice_stream().state == 0) {
    // The battle's voice banks stream while the player walks (voice_queue.cpp). A random
    // encounter adds budget for field clips: the frames left before the battle
    // (distance / walking speed) minus those the preloads take to read, at 1000 a
    // second above 1000, up to 3000 (the console's divides by 235520 and 60 via
    // multiply-high).
    VoiceStream& v = voice_stream();
    voice_queue_abort();
    v.state = 1;
    v.budget = voice_party_spu_needed();
    if (g.scripted_battle == 0) {
      if (const Actor* leader = field_actor(g.game_state[0x24])) {
        s32 speed = leader->walk_speed;
        if (g.field_flags[0x68] == 0) {
          const s32 scale = read_u16(map_data().scene.data() + 0x68);
          if (scale != 0) speed = ((speed * 100) / scale) << 12;
        }
        s32 preloaded = 0;
        for (int i = 0; i < groups; ++i) preloaded += bp.bytes[i];
        const s32 frames = (speed << 2) != 0 ? rd32(g.field_flags + 0x1C) / (speed << 2) : 0;
        const s32 read60 = preloaded * 60;
        const s32 read_frames =
            static_cast<s32>((static_cast<s64>(read60) * 0x473C1AB7) >> 48) - (read60 >> 31);
        const s32 ms = (frames - read_frames) * 1000;
        const s32 spare = ((static_cast<s32>((static_cast<s64>(ms) * static_cast<s32>(0x88888889u)) >> 32) + ms) >> 5) -
                          (ms >> 31);
        const s32 extra = spare - v.budget - 1000;
        if (extra > 0) v.budget += extra < 3000 ? extra : 3000;
      }
      f->last_encounter_rate = g.field_flags[0x25];
    }
    voice_queue_reset();
    v.budget = voice_queue_build_battle(v.budget);
  }
  load_queue_set_streaming(load_queue(), true);
}

int field_battle_preloads_ready() {
  const int done = preload_count_done(1);
  if (done + 1 < g_preloads.count) return 0;
  return done == g_preloads.count ? 2 : 1;
}

// ---- The transition ------------------------------------------------------------

BattleTransition* battle_transition_construct(BattleTransition* t, int type) {
  t->type = type;
  t->timer = 0;
  t->done = 0;
  task_unregister(t);
  task_register(t, 1);
  g_transition = t;
  return t;
}

void battle_transition_destruct(BattleTransition* t) {
  task_unregister(t);
  if (g_transition == t) g_transition = nullptr;
  delete t;
}

void BattleTransition::update() {
  if (type == 1)
    draw_split(this);
  else
    draw_swirl(this, type != 0);
  timer += globals().frame_ticks;
}

// ---- Party ---------------------------------------------------------------------

void party_validate_before_battle() {
  FieldGlobals& g = globals();
  // Someone must stand in slots 0-3: move the first member of 4-7 up if not.
  int slot = 0;
  for (; slot < 4; ++slot)
    if (static_cast<u16>(slot_character(slot) - 1) < 12) break;
  if (slot == 4) {
    for (; slot < 8; ++slot) {
      if (static_cast<u16>(slot_character(slot) - 1) < 12) {
        party_move_character_to_slot(slot_character(slot) - 1, 0);
        break;
      }
    }
  }
  // config +0x41 (the controlled member) must be an active slot.
  const int ctrl = g.config[0x41];
  if (static_cast<u32>(slot_character(ctrl) - 1) < 12) {
    for (int i = 0; i < 4; ++i)
      if (slot_character(i) == slot_character(ctrl)) return;
  }
  for (int i = 0; i < 4; ++i) {
    if (static_cast<u16>(slot_character(i) - 1) < 12) {
      g.config[0x41] = static_cast<u8>(i);
      return;
    }
  }
}

void party_stash_slot_ids() {
  FieldGlobals& g = globals();
  for (int i = 0; i < 8; ++i) {
    s16 id = slot_character(i);
    const s32 wide = id;
    std::memcpy(g.field_flags + 0x38 + i * 4, &wide, 4);
    if (id < 0) {
      id = 0;
      std::memcpy(g.party + i * 0x60, &id, 2);
    }
  }
}

// ---- Leaving the field ---------------------------------------------------------

void field_unload_map(Field*, bool free_scene) {
  MapData& map = map_data();
  if (free_scene) {
    map.scene = {};
    g_fade_targets.scene = nullptr;
  }
  map.font = {};
  map.script = {};
  field_load_buffer() = {};
  // TODO(port): the side text (0x800762e8), stat snapshot (0x800761a0), extra buffers
  // (0x800752c4) and the per-map objects (0x800752ec) are not created by the port yet.
  world_vehicle_models_free();  // 0x800519d4: the vehicle models (0x80076230)
  glow_destroy_current();  // 0x800752dc: glow_destroy(3)
  if (WorldBg* wbg = world_bg(); wbg != nullptr && globals().field_flags[0x68] != 0)
    world_bg_release(wbg);  // overlay 0x800888e0
  // The party's sprite packs are remembered so they come back after the battle.
  FieldGlobals& g = globals();
  SpritePackSlot* packs = sprite_pack_slots();
  const s32 a = packs[0].data.empty() ? 0 : packs[0].id;
  const s32 b = packs[1].data.empty() ? 0 : packs[1].id;
  if (a != 0 || b != 0) {
    const s32 sa = a != 0 ? a : -1, sb = b != 0 ? b : -1;
    std::memcpy(g.field_flags + 0x60, &sa, 4);
    std::memcpy(g.field_flags + 0x64, &sb, 4);
  }
  for (int i = 0; i < 16; ++i) packs[i] = SpritePackSlot{};
  // The music bank is freed once the sound driver has finished with it.
  while (sound_bank_busy(g.sound) != 0) {
  }
  field_music_free_buffers();
}

void field_start_battle(Field* f, int transition) {
  FieldGlobals& g = globals();
  party_validate_before_battle();
  field_unload_map(f, false);
  g.system_data = nullptr;  // freed (field_load_system_data runs again on return)
  g.system_strings = nullptr;
  g_battle_program.assign(static_cast<size_t>(g.loader->entry_size(kBattleProgramEntry)), 0);
  if (g_script_vm != nullptr) vm_save_state(g_script_vm);  // handoff 0xd
  if (BgRenderer* bg = bg_renderer()) {
    bg_suspend(bg);
  } else if (WorldBg* wbg = world_bg()) {
    world_bg_suspend(wbg);  // vtable[3] 0x8008ae48
  }
  map_data().scene = {};
  g_fade_targets.scene = nullptr;
  ActorSystem* sys = actor_system();
  if (sys != nullptr) actor_sys_save_all(sys);  // handoff 0xb
  // The leader's position and facing go into game_state for the return.
  if (const Actor* leader = field_actor(g.game_state[0x24])) {
    const s32 pos[3] = {leader->x >> 12, leader->y >> 12, leader->z >> 12};
    std::memcpy(g.game_state + 8, pos, 12);
    std::memcpy(g.game_state + 0x18, &leader->angle, 2);
  }
  if (sys != nullptr) actor_delete_slot(sys, -1);
  if (MsgSystem* msg = msg_system()) msg_window_destroy(msg, -1);
  task_list_clear();
  battle_transition_construct(new BattleTransition, transition);
  task_register(load_queue(), 1);
  g_battle_program_loaded = 0;
  load_queue_push(load_queue(), kBattleProgramEntry, g_battle_program.data(), &g_battle_program_loaded, 0);
  sound_play_sfx(g.sound, 0x47, 0, 0x7F40);
  sound_fade_out(g.sound, 0x46);
  g.vsync_mode = 2;
  party_stash_slot_ids();
}

void field_read_title_program() {
  FieldGlobals& g = globals();
  g_battle_program.assign(static_cast<size_t>(g.loader->entry_size(1)), 0);
  g_battle_program_loaded = 0;
  load_queue_push(load_queue(), 1, g_battle_program.data(), &g_battle_program_loaded, 0);
}

void field_destroy_battle_transition(Field*) {
  if (g_transition != nullptr) battle_transition_destruct(g_transition);
  g_transition = nullptr;
}

void field_exit_to_program(Field* f, int reason) {
  FieldGlobals& g = globals();
  g.field_flags[0x7F] = static_cast<u8>(reason);
  if (reason == 0) g.config[0x181] = 0;  // the battle's rule: normal
  const s32 swaps = rd32(g.config + 0x14) + 1;
  std::memcpy(g.config + 0x14, &swaps, 4);
  if (callways_song_active(callways()) != 0) callways_stop_song_on_exit(callways());
  callways_apply_battle_bonuses(callways());
  field_music_sequence().clear();
  g.field_flags[0x24] = 1;  // the field is coming back
  field_destroy_subsystems(f);
  auto* setup = new BattleSetup(g.battle_setup);
  // The stash, the actor snapshot (0xb) and the BG animation copy (0x10) go across;
  // the VM state is in handoff 0xd already.
  if (stash_holds_items())
    handoff_store(g.handoff, stash_table(), 0x11);
  if (u8* snapshot = actor_snapshot()) handoff_store(g.handoff, snapshot, 0xB);
  if (u8* anims = bg_take_anim_save()) handoff_store(g.handoff, anims, 0x10);
  if (u8* regions = world_take_region_save()) handoff_store(g.handoff, regions, 0x10);
  sound_holder_save();  // handoff 0xf
  sound_stop(g.sound);
  psyq::ResetGraph(1);
  psyq::SetDispMask(0);
  // Map-transfer preloads other than tag 2 go; the console then defragments its heap.
  preload_release_kind2_except(2);
  // The preloaded battle entries ride along in the setup (+0x74 / +0xa8).
  const BattlePreloads& bp = g_preloads;
  int slot = 0;
  for (int group = 0; group < bp.count; ++group) {
    u8* done = nullptr;
    std::vector<u8>* data = preload_find(bp.entry[group], &done, nullptr);
    if (data == nullptr || *done == 0) {
      slot += bp.pieces[group];
      continue;
    }
    while (setup->get(0x3C + 4 * slot) < 0) ++slot;
    size_t offset = 0;
    for (int piece = 0; piece < bp.pieces[group]; ++piece, ++slot) {
      const int index = bp.order[slot];
      setup->raw[static_cast<size_t>(0xA8 + index)] = piece == 0 ? 1 : 0;
      const size_t bytes = static_cast<size_t>(g.loader->entry_size(bp.entry[group] + piece));
      if (offset + bytes <= data->size())
        setup->preloaded[static_cast<size_t>(index)].assign(data->begin() + static_cast<std::ptrdiff_t>(offset),
                                                            data->begin() + static_cast<std::ptrdiff_t>(offset + bytes));
      offset += bytes;
    }
  }
  handoff_store(g.handoff, setup, 5);
  delete f;
  boot_run_main_program(g.system, Program::Battle);
}

}  // namespace ss::game::field
