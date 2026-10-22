// The SpellCast object and its module API (#2129 0x80085518-0x80088054). See spellcast.h.

#include "game/battle/spellcast.h"

#include <cstring>
#include <string>

#include "disc/slz.h"
#include "game/battle/bsfx.h"
#include "game/battle/camera.h"
#include "game/battle/combat.h"
#include "game/battle/render.h"
#include "game/battle/particle.h"
#include "game/battle/shot.h"
#include "game/battle/spell_fx.h"
#include "game/battle/spell_modules.h"
#include "game/battle/spell_windows.h"
#include "game/battle/sprite.h"
#include "game/battle/stage.h"
#include "game/battle/unit.h"
#include "game/battle/vram.h"
#include "game/boot/rng.h"
#include "game/boot/sound.h"
#include "runtime/libgpu.h"
#include "runtime/runtime.h"

namespace ss::game::battle {
namespace {

std::array<SpellCast*, 2> g_spellcasts{};  // 0x8006fe08
u8 g_spellcast_count = 0;                   // 0x8006fb2d
u8 g_dim_active[2] = {};                    // 0x8006fb2b / 0x8006fb2c
// The module last unpacked into slot 0 (0x8008cc00): the kind table sends kinds 3..0x3a
// there when a cast has no module of its own.
int g_slot0_module = -1;

void log_once(const std::string& key, const std::string& text) { runtime::Runtime::get().log_once(key, text); }

u32 rd32le(const std::vector<u8>& v, size_t at) {
  if (at + 4 > v.size()) return 0;
  u32 x;
  std::memcpy(&x, v.data() + at, 4);
  return x;
}

std::vector<u8> unpack_at(const std::vector<u8>& data, size_t offset, int block = 0) {
  auto b = disc::slz::unpack_block(data, offset, block, nullptr);
  return b ? std::move(*b) : std::vector<u8>{};
}

void spellcast_start(SpellCast* sc, int kind) {
  // 0x80085904.
  BattleGlobals& g = bg();
  task_register(sc, 3);
  sc->kind = static_cast<u8>(kind);
  sc->step = 0;
  for (int k = 0; k < 2; ++k) {
    sc->clut_saved[k].clear();
    sc->clut_work[k].clear();
  }
  sc->dim_state = 0;
  sc->wait = 0;
  ++g_spellcast_count;
  g.flags = static_cast<u8>(g.flags | 0x40);
  sc->finished = 0;
  camera_end_scheduled();
  camera_reset_mode(g.camera);
  sc->s20 = sc->s24 = sc->s28 = sc->s2c = sc->s30 = sc->s34 = sc->s38 = sc->s3c = 0;
  sc->load_state = 0;
  sc->tex_count = 0;
  sc->part = 0;
  sc->tex_uploads = 0;
  sc->alt_clut = 0;
}

// The temporary Shot the hit functions hand shot_apply_damage (a Shot on the stack of the
// original: owner the caster, x far to the side the spell comes from).
void damage_list(SpellCast* sc, int n, Unit** list, int power, u16 element, int knockback, s32 x) {
  Shot tmp;
  tmp.owner = sc->caster;
  tmp.pos[0] = x;
  tmp.pos[1] = 0;
  tmp.pos[2] = 0;
  shot_apply_damage(&tmp, n, list, nullptr, power, 0, element, 0, knockback);
}

// The sprite set's texture bank 'part' is in: its CLUT and images to VRAM (the end of a
// command 4 / 7 / 9 stream).
void upload_part(SpellCast* sc) {
  const int part = sc->part;
  if (part < 0 || part >= 8) return;
  const std::vector<u8>& b = sc->sheets[static_cast<size_t>(part)];
  AnimSet* set = sc->obj.animset;
  if (b.size() < 0xC || set == nullptr) return;
  const u32 clut_off = rd32le(b, 0);
  s16 first = 0;
  if (clut_off + 2 <= b.size()) std::memcpy(&first, b.data() + clut_off, 2);
  const int depth = first < 0 ? 1 : 0;
  const int row = sc->slot != sc->alt_clut ? 0x1EB : 0x1E9;
  set->clut[static_cast<size_t>(part)] = upload_clut256(b, clut_off + 4, 0, row);
  size_t img = rd32le(b, 4);
  const int count = b[9];
  u16 tpage = 0;
  for (int i = 0; i <= count && img + 4 <= b.size(); ++i) {
    u16 w, h;
    std::memcpy(&w, b.data() + img, 2);
    std::memcpy(&h, b.data() + img + 2, 2);
    const int y = ((sc->tex_uploads - (sc->tex_uploads >> 15)) >> 1) * 0x100 + (sc->slot != 0 ? 0x100 : 0);
    const int x = (sc->tex_uploads & 1) * 0x40 * (depth + 1) + 0x2C0;
    const u16 id = upload_texture(b, img + 4, depth, 0, x, y, w, h);
    if (i == 0) tpage = id;
    s32 halves = static_cast<s32>(w) * static_cast<s32>(h);
    halves = depth == 0 ? halves >> 2 : halves / 2;
    img += static_cast<size_t>(halves + 2) * 2;
    if ((img & 3) != 0) img += 2;
    ++sc->tex_uploads;
  }
  set->tpage[static_cast<size_t>(part)] = tpage;
}

}  // namespace

// ---- The object ----

SpellCast::SpellCast() {
  // spellcast_init 0x80085760: bobj_create for both objects, the first one's AnimSet,
  // the second one draws into pool A.
  obj.animset = animset_alloc();
  obj2.pool = bg().prim_pool_a;
}

SpellCast::~SpellCast() {
  // spellcast_destroy 0x80085824.
  animset_free(obj.animset);
  obj.animset = nullptr;
}

void SpellCast::update() {
  // spellcast_update 0x80085b0c.
  if (data != nullptr) spellcast_load_step(this);
  if (finished == 0) {
    if (combo != 0 && g_spellcasts[static_cast<size_t>(slot ^ 1)] == nullptr) combo = 0;
    spellcast_dim_step(this);
    if (module != nullptr) {
      module(this);
      return;
    }
    switch (kind) {
      case 1: spell_kind1_update(this); return;
      case 2: spell_kind2_update(this); return;
      case 0x36: bark_cast_step(this); return;
      case 0x3B: spellcast_kind3b_transform(this); return;
      default: break;
    }
    // Kinds 3..0x3a go to the code at 0x8008cc00 (whatever module slot 0 last held).
    if (SpellModuleFn fn = spell_module_fn(g_slot0_module)) {
      fn(this);
      return;
    }
    log_once("battle-spellcast-kind-" + std::to_string(kind),
             "battle: SpellCast kind " + std::to_string(kind) + " (spell " + std::to_string(spell) +
                 ") has no handler: not ported yet; the cast ends");
    finished = 1;
    return;
  }
  if (spellcast_timer_tick(this) != 0) return;
  if (dim_state == 0) {
    spellcast_finish(this);  // (may delete this)
    return;
  }
  spellcast_dim_step(this);
}

SpellCast* spellcast_slot(int i) { return i >= 0 && i < 2 ? g_spellcasts[static_cast<size_t>(i)] : nullptr; }

int spellcast_kind_active(int kind) {
  for (SpellCast* sc : g_spellcasts)
    if (sc != nullptr && sc->kind == static_cast<u8>(kind)) return 1;
  return 0;
}

SpellCast* spellcast_create(int kind, Unit* caster, Unit* target, int power, SpellData data) {
  BattleGlobals& g = bg();
  if (2 < g.state) return nullptr;  // (the data goes with it)
  for (int i = 0; i < 2; ++i) {
    if (g_spellcasts[static_cast<size_t>(i)] != nullptr) continue;
    SpellCast* sc = new SpellCast();
    g_spellcasts[static_cast<size_t>(i)] = sc;
    sc->data = std::move(data);
    sc->caster = caster;
    Unit* t = spellcast_retarget(sc, target);
    if (t == nullptr) {
      delete sc;
      g_spellcasts[static_cast<size_t>(i)] = nullptr;
      return nullptr;
    }
    spellcast_start(sc, kind);
    sc->slot = static_cast<u8>(i);
    sc->target = t;
    sc->power = power;
    sc->combo = static_cast<u8>(caster->clash);
    sc->cmd = 0;
    sc->module = nullptr;
    if (sc->data != nullptr) {
      const std::vector<u8>& d = *sc->data;
      size_t at = 0;
      u32 op = rd32le(d, 0);
      if (op == 5) {
        at = 0xC;
        op = rd32le(d, at);
      }
      if (op == 6) {
        // The module's block i (linked for slot i) is unpacked to 0x8008cc00 / 0x8008e600;
        // the port knows each module by its block 0.
        sc->module_code = unpack_at(d, rd32le(d, at + 4), 0);
        const std::vector<u8>& block = sc->module_code;
        sc->module_index = spell_module_find(block);
        sc->module = spell_module_fn(sc->module_index);
        if (sc->module == nullptr) {
          sc->module_missing = true;
          log_once("battle-spell-module-" + std::to_string(kind) + "-" + std::to_string(block.size()),
                   "battle: spell code module of " + std::to_string(block.size()) + " bytes (SpellCast kind " +
                       std::to_string(kind) + ") not ported yet; the cast ends");
          sc->module = spell_module_missing;
        }
        if (i == 0) g_slot0_module = sc->module_index;
      }
    }
    if (i == 1) spell_clash_window_open();
    return sc;
  }
  return nullptr;
}

void spellcast_finish(SpellCast* sc) {
  // 0x800859e0.
  BattleGlobals& g = bg();
  task_unregister(sc);
  for (int k = 0; k < 2; ++k) {
    sc->clut_saved[k].clear();
    sc->clut_work[k].clear();
  }
  if (g_spellcast_count != 0) --g_spellcast_count;
  if (g_spellcast_count != 0) return;  // (it stays in its slot until the other one ends)
  g.flags = static_cast<u8>(g.flags & 0xBF);
  const int other = sc->slot ^ 1;
  if (g_spellcasts[static_cast<size_t>(other)] != nullptr) {
    delete g_spellcasts[static_cast<size_t>(other)];
    g_spellcasts[static_cast<size_t>(other)] = nullptr;
  }
  camera_reset_mode(g.camera);
  g.director_timer = 0x78;
  g_spellcasts[sc->slot] = nullptr;
  delete sc;
}

void spellcast_reset() {
  for (SpellCast*& sc : g_spellcasts) {
    if (sc == nullptr) continue;
    task_unregister(sc);
    delete sc;
    sc = nullptr;
  }
  g_spellcast_count = 0;
  g_dim_active[0] = g_dim_active[1] = 0;
  g_slot0_module = -1;
}

// ---- The module API ----

void battle_flash_set(SpellCast*, int r, int gg, int b) {
  BattleGlobals& g = bg();
  ViewState& v = view();
  g.flash_active = 1;
  v.stage_colour_target[0] = static_cast<s16>(r);
  v.stage_colour_target[1] = static_cast<s16>(gg);
  v.stage_colour_target[2] = static_cast<s16>(b);
  g.flash_delay = 0;
  g.flash_speed = 4;
}

void spellcast_step_when_camera_idle(SpellCast* sc) {
  if (bg().camera->mode == 0) ++sc->step;
}

void spellcast_wait(SpellCast* sc, int ticks) {
  sc->wait = ticks;
  ++sc->step;
}

int spellcast_is_second_slot(SpellCast* sc) {
  if (sc->slot == 0) return 0;
  spellcast_timer_tick(sc);
  return 1;
}

int spellcast_timer_tick(SpellCast* sc) {
  if (sc->wait == 0) return 0;
  sc->wait -= bg().frame_ticks;
  if (sc->wait < 0) sc->wait = 0;
  return 1;
}

void spellcast_step_when_dim_done(SpellCast* sc) {
  if (sc->dim_state == 0) ++sc->step;
}

Unit* spellcast_retarget(SpellCast* sc, Unit* target) {
  BattleGlobals& g = bg();
  if (target == nullptr || sc->caster == nullptr) return target;
  if (target->is_enemy == sc->caster->is_enemy) return target;
  Unit* t = target;
  int idx = target->slot;
  if (target->is_enemy == 0) {
    for (int n = 0; n < 5; ++n) {
      if (!unit_is_down(t)) return t;
      if (3 < ++idx) idx = 0;
      t = g.party_units[static_cast<size_t>(idx)];
    }
  } else {
    for (int n = 0; n < 9; ++n) {
      if (!unit_is_down(t)) return t;
      if (7 < ++idx) idx = 0;
      t = g.enemy_units[static_cast<size_t>(idx)];
    }
  }
  return nullptr;
}

int units_in_rect(SpellCast*, const s32 rect[4], Unit** out, int party) {
  BattleGlobals& g = bg();
  const int count = party != 0 ? 4 : 8;
  int n = 0;
  for (int i = 0; i < count; ++i) {
    Unit* u = party != 0 ? static_cast<Unit*>(g.party_units[static_cast<size_t>(i)])
                         : static_cast<Unit*>(g.enemy_units[static_cast<size_t>(i)]);
    if (unit_is_down(u)) continue;
    if (rect[0] <= u->pos[0] && u->pos[0] < rect[2] && rect[1] <= u->pos[2] && u->pos[2] < rect[3]) out[n++] = u;
  }
  return n;
}

void spell_damage_area(SpellCast* sc, const s32 rect[4], int power, u16 element, int knockback, int party) {
  Unit* list[8] = {};
  const int n = units_in_rect(sc, rect, list, party);
  if (n == 0) return;
  damage_list(sc, n, list, power, element, knockback, party == 0 ? 0x2710000 : static_cast<s32>(0xFD8F0000));
}

void spell_status_area(SpellCast* sc, const s32 rect[4], u32 mask, int chance, int icon, int party) {
  Unit* list[10] = {};
  bool hit = false;
  const int n = units_in_rect(sc, rect, list, party);
  for (int i = 0; i < n; ++i) {
    Unit* u = list[i];
    if ((u->status_bits & mask) == 0 && u->resist_roll(1, chance) != 0) {
      if (icon != 0) effect_rise_icon(u, icon);
      hit = true;
      u->status_bits |= mask;
    } else {
      effect_rise_icon(u, 0);
    }
  }
  bsfx_play_ui(hit ? 10 : 0xB);
}

void spellcast_dim_begin(SpellCast* sc, int side) {
  const u8 busy = side != 0 ? g_dim_active[1] : g_dim_active[0];
  if (busy != 0) return;
  SpellCast* other = g_spellcasts[static_cast<size_t>(sc->slot ^ 1)];
  if (other != nullptr && other->caster->is_enemy != sc->caster->is_enemy) return;
  if (sc->combo == 3) return;
  sc->dim_side = static_cast<u8>(side);
  if (side == 0) {
    sc->clut_rows = {0x10, 8};
    sc->clut_y = {0x1D0, 0x1ED};
    g_dim_active[0] = 1;
  } else {
    sc->clut_rows = {4, 0};
    sc->clut_y = {0x1FA, 0};
    g_dim_active[1] = 1;
  }
  for (int k = 0; k < 2; ++k) {
    const s16 rows = sc->clut_rows[static_cast<size_t>(k)];
    if (rows == 0) continue;
    sc->clut_saved[static_cast<size_t>(k)].assign(static_cast<size_t>(rows) * 256, 0);
    sc->clut_work[static_cast<size_t>(k)].assign(static_cast<size_t>(rows) * 256, 0);
    psyq::RECT& r = sc->clut_rect[static_cast<size_t>(k)];
    r.x = 0;
    r.y = sc->clut_y[static_cast<size_t>(k)];
    r.w = 0x100;
    r.h = rows;
    psyq::StoreImage(&r, sc->clut_saved[static_cast<size_t>(k)].data());
  }
  sc->dim_state = 1;
  sc->dim_level = 0;
  sc->dim_strip = 0;
}

void spellcast_dim_end(SpellCast* sc) {
  if (sc == nullptr || sc->clut_saved[0].empty()) return;
  sc->dim_state = 2;
  g_dim_active[sc->dim_side != 0 ? 1 : 0] = 0;
  sc->dim_strip = 0;
}

void spellcast_dim_step(SpellCast* sc) {
  if (sc->dim_state == 0) return;
  BattleGlobals& g = bg();
  const int k = sc->dim_strip;
  const s16 rows = sc->clut_rows[static_cast<size_t>(k)];
  std::vector<u16>& saved = sc->clut_saved[static_cast<size_t>(k)];
  std::vector<u16>& work = sc->clut_work[static_cast<size_t>(k)];
  if (rows != 0 && !saved.empty() && !work.empty()) {
    const s32 lvl = sc->dim_level;
    for (int i = 0; i < rows * 256; ++i) {
      const u32 c = saved[static_cast<size_t>(i)];
      // (The original converts the pixel pairs of nonzero words; zero pairs stay zero.)
      const u32 pair = static_cast<u32>(saved[static_cast<size_t>(i & ~1)]) | static_cast<u32>(saved[static_cast<size_t>(i | 1)]) << 16;
      if (pair == 0) continue;
      if (c == 0) {
        work[static_cast<size_t>(i)] = 0;
        continue;
      }
      const u32 r = c & 0x1F, gg = (c & 0x3E0) >> 5, b = (c & 0x7C00) >> 10;
      const u32 grey = (r * 3 + gg * 6 + b) / 10;
      const s32 r2 = (static_cast<s32>((grey - r) * static_cast<u32>(lvl)) >> 5) + static_cast<s32>(r);
      const s32 g2 = ((((static_cast<s32>(grey * 3) >> 2) - static_cast<s32>(gg)) * lvl) >> 5) + static_cast<s32>(gg);
      const s32 b2 = ((-static_cast<s32>(b) * lvl) >> 5) + static_cast<s32>(b);
      work[static_cast<size_t>(i)] = static_cast<u16>((c & 0x8000) | static_cast<u32>(r2) | static_cast<u32>(g2) << 5 | static_cast<u32>(b2) << 10);
    }
    psyq::LoadImage(&sc->clut_rect[static_cast<size_t>(k)], work.data());
  }
  if (sc->dim_strip != 0) {
    bool done = false;
    if (sc->dim_state == 1) {
      if (sc->dim_level != 0x20) {
        sc->dim_level = static_cast<s16>(sc->dim_level + g.frame_ticks * 2);
        if (0x20 < sc->dim_level) sc->dim_level = 0x20;
      } else {
        done = true;
      }
    } else if (sc->dim_state == 2) {
      if (sc->dim_level != 0) {
        sc->dim_level = static_cast<s16>(sc->dim_level - g.frame_ticks * 2);
        if (sc->dim_level < 0) sc->dim_level = 0;
      } else {
        done = true;
      }
    }
    if (done) sc->dim_state = 0;
  }
  sc->dim_strip ^= 1;
}

void spell_hit_all(SpellCast* sc, u16 element, int knockback) {
  spell_hit_rect(sc, stage().bounds, element, knockback);
}

void spell_hit_rect(SpellCast* sc, const s32 rect[4], u16 element, int knockback) {
  if (1 < static_cast<u8>(sc->combo - 1)) {
    const int side = sc->combo == 3 ? sc->caster->is_enemy ^ 1 : sc->caster->is_enemy;
    if (sc->combo == 4) sc->eff_power += g_spellcasts[static_cast<size_t>(sc->slot ^ 1)]->eff_power;
    spell_damage_area(sc, rect, sc->eff_power, element, knockback, side);
  }
  battle_flash_set(sc, 0x80, 0x80, 0x80);
  spellcast_dim_end(sc);
  spell_landed_flag() = 1;
  ++sc->step;
}

void spell_hit_target(SpellCast* sc, u16 element, int knockback) {
  if (1 < static_cast<u8>(sc->combo - 1)) {
    if (sc->combo == 4) sc->eff_power += g_spellcasts[static_cast<size_t>(sc->slot ^ 1)]->eff_power;
    Unit* t = sc->combo == 3 ? sc->caster : sc->target;
    if (!unit_is_down(t)) {
      Unit* list[1] = {t};
      damage_list(sc, 1, list, sc->eff_power, element, knockback,
                  sc->caster->is_enemy != 0 ? static_cast<s32>(0xFD8F0000) : 0x2710000);
    }
  }
  battle_flash_set(sc, 0x80, 0x80, 0x80);
  ++sc->step;
}

void spell_status_all(SpellCast* sc, u32 mask, int chance, int icon) {
  spell_status_area(sc, stage().bounds, mask, chance, icon, sc->caster->is_enemy);
  battle_flash_set(sc, 0x80, 0x80, 0x80);
  spellcast_dim_end(sc);
  ++sc->step;
}

void spellcast_load_step(SpellCast* sc) {
  BattleGlobals& g = bg();
  if (sc->load_state != 0) {
    // The streamed block decompresses g_frame_ticks * 8 KB a frame.
    sc->stream_left -= g.frame_ticks << 13;
    if (0 < sc->stream_left) return;
    if (sc->load_state == 1) upload_part(sc);
    sc->load_state = 0;
    return;
  }
  const std::vector<u8>& d = *sc->data;
  const u32 op = rd32le(d, sc->cmd);
  if (op == 0) {
    // The list is done: the data goes once no sound bank upload is busy (handlers wait for
    // +0x54 to clear).
    sc->loaded = 1;
    if (sound_bank_busy(g.sound) != 0) return;
    // (The port keeps the bytes with the cast: a clip opened from them may still play.)
    sc->data_retired = std::move(sc->data);
    return;
  }
  // (Port: the heap always has room; the console waits for enough free memory.)
  sc->cmd += 4;
  auto arg = [&](int k) { return rd32le(d, sc->cmd + static_cast<size_t>(k) * 4); };
  AnimSet* set = sc->obj.animset;
  switch (op) {
    case 3: {
      const int n = sc->tex_count;
      if (n < 8) {
        sc->textures[static_cast<size_t>(n)] = unpack_at(d, arg(0));
        sc->stream_left = static_cast<s32>(sc->textures[static_cast<size_t>(n)].size());
      }
      sc->load_state = 2;
      sc->cmd += 4;
      ++sc->tex_count;
      break;
    }
    case 4: {
      const u32 a = arg(0), b = arg(1);
      if (a <= b && b <= d.size()) sc->header.assign(d.begin() + a, d.begin() + b);
      sc->cmd += 4;
      if (set != nullptr) set->layout = sc->header.data();
      sc->obj.pool = g.prim_pool_a;
      sc->anims = unpack_at(d, arg(0));
      sc->cmd += 4;
      if (set != nullptr) set->anims = sc->anims.data();
      sc->frames[0] = unpack_at(d, arg(0));
      sc->cmd += 4;
      if (set != nullptr) set->frames[0] = sc->frames[0].data();
      sc->sheets[0] = unpack_at(d, arg(0));
      sc->stream_left = static_cast<s32>(sc->sheets[0].size());
      if (set != nullptr) set->sheets[0] = sc->sheets[0].data();
      bobj_anim_reset(&sc->obj);
      sc->load_state = 1;
      sc->cmd += 4;
      break;
    }
    case 5: {
      u32 clip = arg(0);
      if (arg(1) != 0 && static_cast<u32>(rng_next() & 0x7FFF) < 0x100) clip = arg(1);
      if (clip < d.size()) bsfx_play(0, d.data() + clip, sc->caster, 1);
      sc->cmd += 8;
      break;
    }
    case 6:
      sc->cmd += 4;
      break;
    case 8: {
      const u32 size = arg(0), off = arg(1);
      sc->cmd += 8;
      const u32 words = static_cast<u32>(static_cast<s32>(size) / 4);
      sc->raw.assign(size, 0);
      if (off + words * 4 <= d.size()) std::memcpy(sc->raw.data(), d.data() + off, words * 4);
      break;
    }
    case 9:
      sc->alt_clut = 1;
      [[fallthrough]];
    case 7: {
      ++sc->part;
      const size_t part = static_cast<size_t>(sc->part & 7);
      const u32 frames = arg(0);
      if (frames != 0) {
        sc->frames[part] = unpack_at(d, frames);
        if (set != nullptr) set->frames[part] = sc->frames[part].data();
      }
      sc->cmd += 4;
      sc->sheets[part] = unpack_at(d, arg(0));
      sc->stream_left = static_cast<s32>(sc->sheets[part].size());
      if (set != nullptr) set->sheets[part] = sc->sheets[part].data();
      bobj_anim_reset(&sc->obj);
      sc->load_state = 1;
      sc->cmd += 4;
      break;
    }
    default:
      log_once("battle-spell-data-op-" + std::to_string(op), "battle: spell data command " + std::to_string(op) + " unknown");
      sc->loaded = 1;
      break;
  }
}

int spellcast_effects_gone(SpellCast* sc) {
  const AnimSet* mine = sc->obj.animset;
  const u8* layout = mine != nullptr ? mine->layout : nullptr;
  for (int i = 0; i < 0x128; ++i) {
    const Sprite* s = sprite_at(i);
    if (s != nullptr && s->anim_id != 0 && s->animset != nullptr && s->animset->layout == layout) return 0;
  }
  for (int i = 0; i < 0xA0; ++i) {
    const Shot* s = shot_at(i);
    if (s != nullptr && s->kind != 0 && s->animset != nullptr && s->animset->layout == layout) return 0;
  }
  return 1;
}

int spellcast_combo_point(SpellCast* sc, s32 out[4]) {
  SpellCast* other = g_spellcasts[static_cast<size_t>(sc->slot ^ 1)];
  int r;
  if (sc->combo == 2) {
    std::memcpy(out, other->centre, 16);
    r = 2;
  } else if (sc->combo == 1 || sc->combo == 3) {
    for (int k = 0; k < 3; ++k) out[k] = (other->centre[k] - sc->centre[k]) / 2 + sc->centre[k];
    out[3] = 0;
    r = sc->combo;
  } else {
    return 0;
  }
  out[0] += rng_spread_fixed(0x80);
  out[1] += rng_spread_fixed(0x80);
  out[2] += rng_spread_fixed(0x80);
  return r;
}

Sprite* spellcast_sprite_spawn(SpellCast* sc, int anim) { return sprite_spawn_from(anim, 0, &sc->obj, nullptr); }
Sprite* spellcast_sprite_spawn2(SpellCast* sc, int anim, int loops) { return sprite_spawn_from(anim, loops, &sc->obj, nullptr); }

}  // namespace ss::game::battle

namespace ss::game::battle {

u8 spell_module_u8(const SpellCast* sc, u32 address) {
  const u32 off = address - 0x8008CC00u;
  return off < sc->module_code.size() ? sc->module_code[off] : 0;
}
s16 spell_module_s16(const SpellCast* sc, u32 address) {
  return static_cast<s16>(spell_module_u8(sc, address) | spell_module_u8(sc, address + 1) << 8);
}
s32 spell_module_s32(const SpellCast* sc, u32 address) {
  return static_cast<s32>(static_cast<u32>(spell_module_u8(sc, address)) | static_cast<u32>(spell_module_u8(sc, address + 1)) << 8 |
                          static_cast<u32>(spell_module_u8(sc, address + 2)) << 16 |
                          static_cast<u32>(spell_module_u8(sc, address + 3)) << 24);
}

}  // namespace ss::game::battle

// ---- The built-in kinds ----

namespace ss::game::battle {

void spell_kind1_update(SpellCast* sc) {
  // 0x80087730: Ray (the spell 0x1a casters without data): beams drawn into a point 0x300
  // above the target, meteors (Shot kind 0x13) falling round it, the camera's tour, then
  // the hit on the whole field.
  BattleGlobals& g = bg();
  if ((g.flags & 4) != 0) return;
  s32 i8 = sc->caster->st()->intel[2];
  if (i8 < 0) i8 += 7;
  s32 p8 = sc->power;
  if (p8 < 0) p8 += 7;
  sc->eff_power = (i8 >> 3) + (p8 >> 3) + 0x50;
  sc->centre[0] = sc->target->pos[0];
  sc->centre[1] = sc->target->pos[1] - 0x300000;
  sc->centre[2] = sc->target->pos[2];
  for (int i = 0; i < g.frame_ticks; ++i) {
    s32 at[4];
    at[0] = rng_spread_fixed(0x400);
    at[1] = rng_spread_fixed(0xC0);
    at[2] = rng_spread_fixed(0x400);
    at[0] += sc->centre[0];
    at[1] += sc->centre[1];
    at[2] += sc->centre[2];
    at[3] = 0;
    effect_spawn_beam_at(at, sc->centre, 1, 0xF0, 0x10, 0x14000, 0, 0);
  }
  sc->s20 += g.frame_ticks;
  int every = 100 < sc->power ? 6 : 4;
  if (sc->combo == 4) every -= 2;
  if (every <= sc->s20) {
    sc->s20 = 0;
    if (Shot* s = shot_spawn(0x13)) {
      const int r = spellcast_combo_point(sc, s->point);
      if (r == 0) {
        s32 p[4];
        stage_random_point(p, 1);
        std::memcpy(s->point, p, 12);
      }
      std::memcpy(s->pos, sc->centre, 12);
      s->pos_w = sc->centre[3];
      s->p138 = r;
      s->power = static_cast<s16>(sc->power);
    }
  }
  if (spellcast_timer_tick(sc) != 0) return;
  SpellCast* first = spellcast_slot(0);
  const bool lead = sc->slot == 0 || (first != nullptr && first->finished != 0);
  switch (sc->step) {
    case 0:
      spellcast_dim_begin(sc, sc->caster->is_enemy);
      if (lead) {
        camera_reset_mode(g.camera);
        camera_move_to(g.camera, sc->centre, 0xC00, -0x71, 0x800, 0x32, 0x100, 5);
      }
      spellcast_wait(sc, 0x28);
      battle_flash_set(sc, 0x40, 0x40, 0x40);
      return;
    case 1:
      if (lead) {
        if (sc->combo == 3 || sc->combo == 1) {
          const SpellCast* other = spellcast_slot(sc->slot ^ 1);
          s32 mid[4] = {};
          for (int k = 0; k < 3; ++k) mid[k] = (other->centre[k] - sc->centre[k]) / 2 + sc->centre[k];
          camera_move_to(g.camera, mid, 0x8E3, 0x1C7, 0xC00, 0x78, 0x100, 5);
        } else {
          const u32 r = rng_range(3);
          if (r == 1) camera_move_to(g.camera, sc->centre, 0xE3, 0x1C7, 0xC00, 0x78, 0x100, 5);
          else if (r == 0) camera_move_to(g.camera, sc->centre, 0x955, 0x238, 0xC00, 0x78, 0x100, 5);
          else camera_move_to(g.camera, sc->target->pos, 0x871, 0x238, 0xC00, 0x78, 0x100, 5);
        }
      }
      spellcast_wait(sc, 0x78);
      return;
    case 2:
      if (lead) camera_move_to(g.camera, sc->target->pos, 0xC00, 0xE3, 0xC00, 0x78, 0x100, 5);
      spellcast_wait(sc, 0x78);
      return;
    case 3:
      spell_hit_all(sc, 0x80, 0x10);
      sc->finished = 1;
      return;
    default: return;
  }
}

void spell_kind2_update(SpellCast* sc) {
  // 0x80087c98: a white flash, 40 light lines drawn into the centre and twelve healing
  // orbs (Shot kind 0x15) sent round the standing members; the camera on the caster.
  BattleGlobals& g = bg();
  if ((g.flags & 4) != 0 || spellcast_timer_tick(sc) != 0) return;
  switch (sc->step) {
    case 0:
      spellcast_dim_begin(sc, sc->caster->is_enemy == 0 ? 1 : 0);
      screen_fade_create(0xFFFFFF, 0x20, 4, 10);
      sc->step = 1;
      return;
    case 1: {
      for (int i = 0; i < 0x28; ++i) {
        s32 at[4];
        at[0] = rng_spread_fixed(0x600);
        at[1] = rng_spread_fixed(0x100);
        at[2] = rng_spread_fixed(0x600);
        at[0] += sc->centre[0];
        at[1] += sc->centre[1];
        at[2] += sc->centre[2];
        at[3] = 0;
        particle_spawn_homing_line(sc->centre, at, 0x4040, 0xFFFF, 0xC0, 0x10000, 0);
      }
      int idx = -1, laps = 0;
      bool anyone = false;
      for (PartyUnit* u : g.party_units)
        if (!unit_is_down(u)) anyone = true;
      for (int k = 0; k < 0xC && anyone; ++k) {
        do {
          if (3 < ++idx) {
            ++laps;
            idx = 0;
          }
        } while (unit_is_down(g.party_units[static_cast<size_t>(idx)]));
        Shot* s = shot_spawn(0x15);
        if (s == nullptr) continue;
        std::memcpy(s->pos, sc->centre, 12);
        s->owner = sc->caster;
        s->target = g.party_units[static_cast<size_t>(idx)];
        s->power = static_cast<s16>(sc->power);
        s->vel[0] = rng_spread_fixed(0xC);
        s->vel[1] = rng_spread_fixed(0xC);
        s->vel[2] = rng_spread_fixed(0xC);
        s->p138 = laps;
        bobj_share_sprite(sc->caster, s);
        // (+0x5c = the cast's +0x3c: an AnimSet a module left there.)
        if (sc->s3c != 0) s->animset = sc->orb_set;
      }
      spellcast_wait(sc, 10);
      return;
    }
    case 2:
      camera_move_to(g.camera, sc->caster->pos, 0xC00, 0x1C7, 0x1000, 0x78, 0x100, 5);
      spellcast_wait(sc, 0x1E);
      return;
    case 3:
      spellcast_dim_end(sc);
      sc->finished = 1;
      ++sc->step;
      return;
    default: return;
  }
}

}  // namespace ss::game::battle
