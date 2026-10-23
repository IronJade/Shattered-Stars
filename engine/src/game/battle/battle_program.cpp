// The battle program's flow: battle_entry (0x8002f810), battle_run (0x8007160c: the
// System pointers, the setup from handoff 5, the 12-entry load list), the per-battle
// init (battle_init_state 0x80074a74, battle_init_objects 0x80074168), the frame loop
// (battle_main_loop 0x800459fc, 30 fps) and the swap back to the field
// (battle_exit_to_field 0x80087644, #1981). docs/notes/battle.md §3.
#include <cstring>
#include <memory>

#include "disc/slz.h"
#include "game/battle/battle.h"
#include "game/battle/fx_draw.h"
#include "game/battle/battle_debug.h"
#include "game/battle/bobj.h"
#include "game/battle/camera.h"
#include "game/battle/input.h"
#include "game/battle/killer.h"
#include "game/battle/bload.h"
#include "game/battle/bsfx.h"
#include "game/battle/combat.h"
#include "game/battle/hud.h"
#include "game/battle/result.h"
#include "game/battle/particle.h"
#include "game/battle/shot.h"
#include "game/battle/voice.h"
#include "game/battle/voice_player.h"
#include "game/battle/unit_move.h"
#include "game/battle/loaders.h"
#include "game/battle/render.h"
#include "game/battle/command_menu.h"
#include "game/battle/spell_windows.h"
#include "game/battle/spells.h"
#include "game/battle/sprite.h"
#include "game/battle/stage.h"
#include "game/battle/unit.h"
#include "game/boot/pad.h"
#include "game/boot/rng.h"
#include "game/common/program_image.h"
#include "game/common/screen.h"
#include "runtime/libgpu.h"
#include "runtime/runtime.h"

namespace ss::game::battle {
namespace {

BattleGlobals g_battle;
BattleDebug g_debug;

void todo(const std::string& what) {
  runtime::Runtime::get().log_once("battle-" + what, "battle: not ported yet: " + what);
}

s32 rd32(const u8* p) {
  s32 v;
  std::memcpy(&v, p, 4);
  return v;
}

RenderContext* new_render_context() {
  BattleGlobals& g = g_battle;
  g.render_contexts.push_back(std::make_unique<RenderContext>());
  return g.render_contexts.back().get();
}

}  // namespace

BattleGlobals& bg() { return g_battle; }
BattleDebug& battle_debug() { return g_debug; }

const u8* battle_data(u32 address) { return program_data(kBattleProgramEntry, address); }
s16 battle_s16(u32 address) {
  const u8* p = battle_data(address);
  s16 v = 0;
  if (p != nullptr) std::memcpy(&v, p, 2);
  return v;
}
u16 battle_u16(u32 address) { return static_cast<u16>(battle_s16(address)); }
s32 battle_s32(u32 address) {
  const u8* p = battle_data(address);
  return p != nullptr ? rd32(p) : 0;
}

s32 item_data_field(int item, int field) {
  const BattleGlobals& g = g_battle;
  if (item == 0) return 0;
  const size_t at = static_cast<size_t>(item - 1) * 0x30 + static_cast<size_t>(field);
  if (item < 0 || at + 4 > g.item_data.size()) return 0;
  const u8* p = g.item_data.data() + at;
  if (field == 0) return rd32(p);
  if (static_cast<u32>(field - 4) < 0xF) {
    s16 v;
    std::memcpy(&v, p, 2);
    return v;
  }
  return *p;
}

// ---- Task list and OT layers ------------------------------------------------------------

void task_register(Task* task, int prio) {
  BattleGlobals& g = g_battle;
  u32 i = 0;
  while (i < g.tasks.size() && g.tasks[i] != nullptr) ++i;
  if (i >= g.tasks.size()) return;
  g.task_prio[i] = static_cast<u8>(prio);
  g.tasks[i] = task;
  if (i >= g.task_count) g.task_count = static_cast<u16>(i + 1);
}

void task_unregister(Task* task) {
  BattleGlobals& g = g_battle;
  for (u32 i = 0; i < g.task_count; ++i) {
    if (g.tasks[i] == task) {
      g.tasks[i] = nullptr;
      if (i == static_cast<u32>(g.task_count) - 1) --g.task_count;
      return;
    }
  }
}

void task_run_all() {
  BattleGlobals& g = g_battle;
  for (u8 prio = 0; prio < 5; ++prio) {
    for (u16 i = 0; i < g.task_count; ++i) {
      if (g.task_prio[i] == prio && g.tasks[i] != nullptr) g.tasks[i]->update();
    }
  }
}

void ot_layer_add(RenderContext* layer) {
  BattleGlobals& g = g_battle;
  for (auto& slot : g.ot_layers) {
    if (slot == nullptr) {
      slot = layer;
      layer->ot_head = screen_clear_ot(layer->ot);
      return;
    }
  }
}

void ot_layer_remove(RenderContext* layer) {
  for (auto& slot : g_battle.ot_layers)
    if (slot == layer) slot = nullptr;
}

void ot_layer_to_front(RenderContext* layer) {
  auto& l = g_battle.ot_layers;
  int at = -1;
  for (int i = 0; i < 8; ++i)
    if (l[i] == layer) at = i;
  if (at < 0) return;
  for (int i = at; i > 0; --i) l[i] = l[i - 1];
  l[0] = layer;
}

// ---- Init -------------------------------------------------------------------------------

void battle_init_state() {
  BattleGlobals& g = g_battle;
  g.enemy_units.fill(nullptr);
  g.enemy_stats.fill(nullptr);
  g.ot_layers.fill(nullptr);
  g.controlled_slot = g.config[0x41];
  g.leader_slot = g.config[0x41];
  g.state = kStateLoading;
  g.dispmask_delay = 0;
  g.flags = 1;
  g.palette_cycle_timer = 4;
  g.enemy_count = 0;
  g.battle_frames = 0;
  g.fight_frames = 0;
  g.result_started = 0;
  g.reward_flags = 0;
  g.special_obj_released = false;
  g.party_escaping = 0;
  g.victory_music_started = 0;
  g.slowmo_phase = 0;
  g.last_attacker = nullptr;
  g.stage_hidden = 0;
  g.effects_suppressed = 0;
  g.special_voice_set = 0;
  g.intro_voice_gate = 0;
  g.rule_timer = 0;
  g.task_count = 0;
  g.tasks.fill(nullptr);
  view().fog_on = 0;
  stage().has_panorama = 0;
  stage().has_lights = 0;
  stage().height_grid_on = 0;
  stage().flare_on = 0;
  view().clear_alt_on = 0;
  unit_globals() = UnitGlobals{};
  // TODO(port): g_member_tables (party_banter_voice_table per member, 0x54 bytes each)
  // for the #1984.1 banter lines.
}

void battle_init_objects() {
  BattleGlobals& g = g_battle;
  // VsyncCounter, g_input (0xa0): the port's input block is not ported yet.
  g.camera_obj = std::make_unique<Camera>();
  g.camera = g.camera_obj.get();
  g.prim_pool_a = new_render_context();
  render_ctx_create_arena(g.prim_pool_a, 62000);
  render_ctx_create_ot(g.prim_pool_a, 0x621);
  g.prim_pool_b = new_render_context();
  render_ctx_create_arena(g.prim_pool_b, 0xC80);
  render_ctx_create_ot(g.prim_pool_b, 0xC);
  shot_pool_create();    // 160 x 0x13c (g_pool_13c)
  particle_pool_create();  // 200 x 0x48 (g_pool_48)
  sprite_pool_create();  // 296 x 0xb8 (g_pool_b8)
  input_reset();         // g_input (0xa0)
  damage_popups_reset(); // Pool2C (16 x 0x2c)
  bsfx_reset();
  director_reset();
  spells_reset();        // the spell-data cache, the SpellCasts (spells.cpp, spellcast.cpp)
  bload_reset();         // the load queue (bload.cpp)
  voice_player_reset();  // the voice slots (voice_player.cpp)
  spell_windows_reset();
  command_menu_reset();  // the list menus and their memory (command_menu.cpp)
  // TODO(port): the other pools: Shot (160 x 0x13c), DamagePopup (16 x 0x2c), PolyShard
  // (192 x 0x74), Particle (200 x 0x48).
  animset_pool_reset();
  auto* party = static_cast<u8*>(handoff_get(g.handoff, 3));
  auto* records = static_cast<u8*>(handoff_get(g.handoff, 4));
  for (int i = 0; i < 4; ++i) {
    g.party_units[i] = nullptr;
    s16 id = 0;
    if (party != nullptr) std::memcpy(&id, party + i * 0x60, 2);
    if (id < 1) {
      g.party_slot[i] = nullptr;
      g.party_record[i] = nullptr;
    } else {
      g.party_slot[i] = party + i * 0x60;
      g.party_record[i] = records + i * 0xD0;
    }
  }
  g.item_effects.fill(0);
  g.sprite_bank_fx = std::make_unique<BObj>();
  g.sprite_bank_fx2 = std::make_unique<BObj>();
  g.sprite_set_a = std::make_unique<BObj>();
  g.sprite_set_b = std::make_unique<BObj>();
  g.sprite_set_c = std::make_unique<BObj>();
  battle_windows_create();  // g_hud (BattleHud) and the debug window
}

void battle_separate_units() {
  BattleGlobals& g = g_battle;
  for (int i = 0; i < 12; ++i) {
    Unit* u = i < 4 ? static_cast<Unit*>(g.party_units[i]) : g.enemy_units[i - 4];
    if (u == nullptr) continue;
    const s32 step = u->pos[0] > 0 ? -0x30000 : 0x30000;
    const s32 zero[3] = {0, 0, 0};
    for (int guard = 0; guard < 0x1000 && unit_check_move(u, zero, 7) != nullptr; ++guard) {
      u->pos[0] += step;
      if (u->flying == 0 && u->airborne == 0) u->pos[1] = bobj_ground_height(u);
    }
  }
}

void battle_scene_start() {
  BattleGlobals& g = g_battle;
  ViewState& v = view();
  gte_setup();
  screen_setup_buffer0(0, 0, 0x140, 0xF0, 8, 0, 0, 0);
  screen_setup_buffer1(0, 0xE8, 0x140, 0xF0, 8, 0, 0, 0);
  for (int i = 0; i < 2; ++i) {
    psyq::DRAWENV* d = screen_drawenv(i);
    d->isbg = 1;
    d->r0 = v.clear_rgb[0];
    d->g0 = v.clear_rgb[1];
    d->b0 = v.clear_rgb[2];
  }
  g.dispmask_delay = 2;
  g.frame_ticks = 2;
  pad_set_port_mask(g.pad, 1);
  pad_set_repeat_delay(g.pad, 0xC);
  camera_reset();
  ot_layer_add(g.prim_pool_b);
  g.ot_layers[7] = g.prim_pool_a;  // DAT_8006ef3c: pool A is the bottom layer
  g.last_frame_vblank = runtime::Runtime::get().vblanks();
  bool boss = false;
  for (EnemyUnit* e : g.enemy_units)
    if (!unit_is_down(e) && e->boss != 0) boss = true;
  int mode;
  if (g.stage_id == 0x33) {
    mode = 0x11;
  } else if (boss) {
    mode = 0xD;
  } else {
    const u8* modes = battle_data(0x8006CD28);
    mode = modes != nullptr ? modes[g.intro_type & 3] : 1;
  }
  camera_set_mode(g.camera, mode, nullptr);
  battle_windows_open();  // the HUD at (0, 0, 0x140, 0x28), the debug window below it
  battle_start_burst_create();  // 0x8003bf18: the opening wipe
  ot_layer_to_front(g.prim_pool_b);
}

// ---- battle_run --------------------------------------------------------------------------

void battle_entry(System* sys) {
  // battle_program_create (0x800715d0): new BattleProgram (12 bytes) -> g_current_program;
  // vtable[2] program_set_system(sys), vtable[1] battle_run.
  (void)sys;
  battle_run();
}

void battle_run() {
  // Fresh globals: the console reloads the whole program image for every battle.
  g_battle.~BattleGlobals();
  new (&g_battle) BattleGlobals{};
  view() = ViewState{};
  stage() = StageData{};
  encounter_reset();
  party_reset();
  texcache_pool_reset();
  pool_a_bytes_used() = 0;
  BattleGlobals& g = g_battle;
  g.system = get_system();
  g.handoff = &g.system->handoff;
  g.loader = &g.system->loader;
  g.pad = &g.system->pad;
  g.sound = &g.system->sound;
  screen_init();
  pad_set_option(g.pad, 1);
  // 0x800134d8(0): mem_set_alloc_fail_hook, clearing the heap's out-of-memory hook (not
  // needed on the host).
  const psyq::RECT all{0, 0, 0x400, 0x200};
  psyq::ClearImage(&all, 0, 0, 0);
  // The setup: copied, the handoff cleared and freed.
  auto* setup = static_cast<BattleSetup*>(handoff_get(g.handoff, 5));
  if (setup != nullptr) {
    g.setup = std::move(*setup);
    delete setup;
  }
  handoff_clear(g.handoff, 5);
  g.encounter_id = static_cast<s16>(g.setup.encounter());
  g.stage_id = static_cast<s16>(g.setup.stage());
  g.config = static_cast<u8*>(handoff_get(g.handoff, 2));
  g.inventory = static_cast<u8*>(handoff_get(g.handoff, 6));
  // g_preload_list = map_resolve_entries(new 0x22, -1, 0, 0): the files the field reloads
  // afterwards (handoff 0xc). Without the field's state block (--battle) there is none.
  g.preload_list.clear();
  g.handoff10_blob.clear();
  if (handoff_get(g.handoff, 9) != nullptr) {
    g.preload_list.assign(0x11, 0);
    battle_map_resolve_entries(g.preload_list.data(), -1, 0, 0);
  }
  if (g.config == nullptr || g.inventory == nullptr) {
    std::fprintf(stderr, "[battle] no config / inventory handoff; back to the field\n");
    boot_run_main_program(g.system, Program::Field);
  }
  if (!inventory_verify_all(g.inventory)) {
    // The console shows entry 0x542 full screen and hangs (anti-tamper).
    runtime::Runtime::get().log_once("battle-inventory", "battle: the inventory check failed (the console hangs here)");
  }
  battle_init_state();
  battle_init_objects();
  StageLoadCtx ctx;
  stage_load_ctx_init(&ctx);
  StageData& st = stage();
  st.tex_count = 0;
  st.anim_start.fill(0xFF);
  g.intro_speakers[0] = static_cast<u8>(g.setup.voice_member(0));
  g.intro_speakers[1] = static_cast<u8>(g.setup.voice_member(1));
  // The load list: entry +8[i] while +0x3c[i] != 0.
  for (int i = 0; i < 12 && g.setup.id(i) != 0; ++i) {
    const s32 entry = g.setup.entry(i);
    Bytes data;
    if (!g.setup.preloaded[i].empty()) {
      data = g.setup.preloaded[i];
    } else if (entry > 0) {
      data.resize(static_cast<size_t>(g.loader->entry_size(entry)));
      g.loader->load_sync(data.data(), entry, 0);
    }
    battle_load_entry(i, entry, &ctx, std::move(data));
    if (!g.music_started && g.config[0x180] == 0 && sound_bank_busy(g.sound) != 1 && !g.battle_music.empty()) {
      g.music_started = true;
      sound_play_sequence(g.sound, g.battle_music.data());
    }
  }
  // The stage code blocks (#1984 blocks 2-10) and the stage objects (Shot kinds 0x19..0x2b)
  // are not ported yet.
  switch (g.stage_id) {
    case 0xD: case 0x38: case 0x39: case 0x10: case 0x15: case 0x28: case 0x17: case 0x1A: case 0x1F:
    case 0x24: case 0x29: case 0x33: case 0xE:
      todo("stage code and objects of stage " + std::to_string(g.stage_id));
      break;
    default:
      break;
  }
  battle_setup_sprite_banks();
  // Spell data patches (g_spell_data_entry 0x8006d1f2, spell * 5 + variant; spells.h):
  // config +0x4e picks other effect data for spells 0x13 / 0x14, a random one of three for
  // spell 0x19, and the last fights (encounters 0, 0x219-0x21c) their own versions. (The
  // enemy agent's hunk.)
  if (g.config[0x4E] != 0) {
    for (int v = 0; v < 5; ++v) {
      spell_data_entry_set(0x13, v, 0x969);
      spell_data_entry_set(0x14, v, 0x96D);
    }
    spell_data_entry_set(0x13, 3, 0x96A);
    spell_data_entry_set(0x14, 3, 0x96E);
  }
  const u32 pick = rng_range(3);
  if (pick == 1) {
    spell_data_entry_set(0x19, 0, 0x976);
    spell_data_entry_set(0x19, 4, 0x977);
  }
  if (pick == 2) {
    spell_data_entry_set(0x19, 0, 0x978);
    spell_data_entry_set(0x19, 4, 0x979);
  }
  g.single_victory_voice = 0;
  if (g.encounter_id == 0 || (g.encounter_id >= 0x21A && g.encounter_id <= 0x21C)) {
    if (g.encounter_id == 0 || g.encounter_id == 0x21A) {
      spell_data_entry_set(0x24, 0, 0x973);
      spell_data_entry_set(0x21, 0, 0x92C);
    }
    spell_data_entry_set(3, 0, 0x91A);
    spell_data_entry_set(0x14, 0, 0x96F);
    spell_data_entry_set(0x19, 0, 0x97A);
    spell_data_entry_set(7, 0, 0x913);
    spell_data_entry_set(0x1C, 0, 0x924);
    spell_data_entry_set(0x31, 0, 0x93C);
    spell_data_entry_set(0x36, 0, 0x94F);
    spell_data_entry_set(0x3D, 0, 0x95C);
    spell_data_entry_set(0x3B, 0, 0x957);
    g.special_voice_set = 1;
    g.single_victory_voice = 1;
  } else if (g.encounter_id == 0x219) {
    g.special_voice_set = 2;
    spell_data_entry_set(0x2D, 0, 0x8EE);
    spell_data_entry_set(0x2E, 0, 0x8F2);
    spell_data_entry_set(0x16, 0, 0x905);
    spell_data_entry_set(2, 0, 0x8FC);
    spell_data_entry_set(3, 0, 0x919);
    spell_data_entry_set(0x32, 0, 0x936);
    g.single_victory_voice = 1;
  }
  battle_main_loop();
}

// ---- The frame loop ---------------------------------------------------------------------

void battle_free_party_models(bool victory) {
  // 0x80046be0: frees the members' extra model / animation buffers (entries 1-4 of each
  // set at 0x8006f954), the enemy sprite packs on a victory, g_special_obj0 and the effect
  // sprite bank. The port keeps the buffers until battle_free_all; what changes is that the
  // frame loop stops servicing g_special_obj0 (0x8006f920).
  (void)victory;
  g_battle.special_obj_released = true;
}

void battle_victory_begin(Unit* last_hitter) {
  // 0x80048c5c: hit-stop 20, state 3, slow motion and countdown (longer during a special
  // move), outcome 0, the member who strikes the victory pose, music fade.
  BattleGlobals& g = g_battle;
  g.hitstop_frames = 0x14;
  g.state = kStateOver;
  const u8 extra = (g.flags & 0x40) != 0 ? 0x1E : 0;
  g.slowmo_frames = static_cast<s8>(extra + 0x1E);
  g.end_countdown = static_cast<s8>(extra + 0x5A);
  spell_data_clear_all();
  g.outcome = kVictory;
  g.victory_speaker = 0;
  if (last_hitter != nullptr) {
    g.victory_speaker = last_hitter->slot;
    if (3 < g.victory_speaker) g.victory_speaker = 0;
  }
  int tries = 0;
  for (; tries < 0x20; ++tries) {
    if (!unit_is_down(g.party_units[g.victory_speaker & 3])) break;
    g.victory_speaker = static_cast<u8>(rng_range(4));
  }
  if (tries == 0x20) g.victory_speaker = 0xFF;
  g.party_escaping = 0;
  sound_fade_out(g.sound, 0x1E0);
  voice_pick_victory();
  // battle_preload_field_program 0x80047ecc: the field program is native.
}

namespace {

void battle_end_with(u8 outcome) {
  // The common end (0x80046544): the outcome, spell data cleared, state 3, a 90-frame
  // countdown, the music fades, the field program preloads.
  BattleGlobals& g = g_battle;
  g.outcome = outcome;
  spell_data_clear_all();
  g.state = kStateOver;
  g.end_countdown = 0x5A;
  sound_fade_out(g.sound, 0x1E0);
}

void battle_party_lost() {
  BattleGlobals& g = g_battle;
  u8 outcome = kDefeat;
  if (g.party_escaping != 0 && 0 < party_count_fled()) outcome = kEscaped;
  battle_end_with(outcome);
}

bool battle_sprites_busy() {
  BattleGlobals& g = g_battle;
  auto layout_of = [](const BObj* o) -> const u8* {
    return o != nullptr && o->animset != nullptr ? o->animset->layout : nullptr;
  };
  for (int i = 0x18; i < 0x128; ++i) {
    const Sprite* s = sprite_at(i);
    if (s == nullptr || s->anim_id == 0) continue;
    const u8* l = layout_of(s);
    for (const PartyUnit* p : g.party_units) {
      if (unit_is_down(p)) continue;
      if (l == layout_of(p)) return true;
      for (const u8* a : p->alt_layout)
        if (l == a) return true;
    }
    for (const EnemyUnit* e : g.enemy_units)
      if (!unit_is_down(e) && l == layout_of(e)) return true;
    if (l == layout_of(g.sprite_bank_fx.get())) return true;
  }
  return false;
}

void battle_end(u8 outcome) {
  BattleGlobals& g = g_battle;
  g.outcome = outcome;
  g.state = kStateOver;
  g.end_countdown = 90;
  sound_fade_out(g.sound, 0x1E0);
}

}  // namespace

void battle_main_loop() {
  BattleGlobals& g = g_battle;
  runtime::Runtime& rt = runtime::Runtime::get();
  rt.trace("battle main loop");
  // #1984 block 0 is native code; the 0x10000 block at 0x801ee000 (party code slots) is not
  // needed. The party-data loader drives state 0.
  task_register(party_loader_create(), 0);
  battle_separate_units();
  battle_scene_start();
  hud_init(battle_hud());
  int frame_no = 0;
  for (;;) {
    if (g.state > kStateResult) battle_exit_to_field();
    screen_flip();
    text_frame_begin();
    // TODO(port): the clash masks (12 words at 0x8006fe10); deferred deletes.
    pool_a_bytes_used() = 0;
    for (RenderContext* layer : g.ot_layers)
      if (layer != nullptr) layer->ot_head = screen_clear_ot(layer->ot);
    // Freezes: hit-stop and slow motion drive g_battle_flags bit 3.
    u8 flags = g.flags;
    if (g.hitstop_frames == 0) {
      if (g.slowmo_frames != 0) {
        const u32 phase = g.slowmo_phase;
        g.slowmo_phase = static_cast<u8>(phase + g.frame_ticks);
        if (static_cast<s8>(phase + g.frame_ticks) < 6) {
          g.flags = static_cast<u8>(g.flags | 8);
        } else {
          g.slowmo_phase = 0;
          g.flags = static_cast<u8>(g.flags & 0xF7);
          g.slowmo_frames = static_cast<s8>(g.slowmo_frames - g.frame_ticks);
        }
        flags = g.flags;
        if (g.slowmo_frames < 1) {
          g.slowmo_frames = 0;
          flags = static_cast<u8>(g.flags & 0xF7);
        }
      }
    } else {
      const s32 left = g.hitstop_frames - g.frame_ticks;
      g.hitstop_frames = static_cast<s8>(left);
      flags = static_cast<u8>(g.flags | 8);
      if (static_cast<s32>(static_cast<u32>(left) << 24) < 1) {
        g.hitstop_frames = 0;
        flags = static_cast<u8>(g.flags & 0xF7);
      }
    }
    g.flags = flags;
    if (g.state == kStateFighting && enemy_count_active(g.enemy_units) < 1 && (g.flags & 0x40) == 0) {
      battle_victory_begin(g.last_attacker);
    }
    if (party_count_standing() == 0 && g.state == kStateFighting && (g.flags & 0x40) == 0) battle_party_lost();
    // The debug end (--battle-end): a run can end the fight with a chosen outcome.
    if (g_debug.auto_end_frames >= 0 && g.state == kStateFighting && g.fight_frames >= g_debug.auto_end_frames) {
      rt.trace("battle debug end");
      battle_end(g_debug.auto_end_outcome);
    }
    if (g.sprite_bank_fx != nullptr && !g.special_obj_released) bobj_texcache_begin_frame(g.sprite_bank_fx.get());
    voice_update();  // 0x8007faa4 (voice_player.cpp)
    bload_update();  // 0x80047c08 (bload.cpp)
    spell_data_update();
    input_update(&battle_input());
    if (g.camera != nullptr) g.camera->update();
    camera_update_matrices();
    battle_frame_director();
    bsfx_update();
    if (stage().flare_on != 0 && g.stage_hidden == 0) {
      lens_flare_draw(stage().flare_pos, stage().flare_size, stage().flare_colour, stage().flare_b, stage().flare_cos);
    }
    // The fade to the field once the result screen has closed (and the camera has settled).
    if (g.result_started == 0 && g.state == kStateResult && battle_result_screen() == nullptr &&
        (g.camera->mode == 0 || g.camera->remaining < 0xB400)) {
      battle_fade_start();
      g.result_started = 1;
      if (g.victory_music_started != 0) sound_fade_out(g.sound, 0xB4);
    }
    // The victory jingle once the result window has opened.
    if (kStateOver <= g.state && g.outcome == kVictory && battle_result_screen() != nullptr &&
        g.config[0x180] == 0 && g.victory_music_started == 0 && 1 < battle_result_screen()->state &&
        g.stage_id != 0x33) {
      g.victory_music_started = 1;
      sound_play_sequence(g.sound, g.victory_music.data());
    }
    task_run_all();
    if (g.state == kStateLoaded && !g.battle_code_unpacked) g.battle_code_unpacked = true;  // #1984 block 1
    shot_pool_update();
    sprite_pool_update();
    particle_pool_update();
    if (g.stage_hidden == 0) stage_draw_and_animate();
    // TODO(port): clash_resolve (state > 1).
    bool ended = false;
    if (g.config[0x181] == 2 && g.state == kStateFighting && g.flags == 0) {
      // The escape race: an enemy reaching the goal wins it.
      static const s32 kGoal[3] = {0x640000, 0, 0x12C000};
      for (EnemyUnit* e : g.enemy_units) {
        if (unit_is_down(e)) continue;
        if (vec_distance_xz_fx(e->pos, kGoal) < 0x30000) {
          g.outcome = kRanOff;
          bsfx_play_at_pos(0x1E, kGoal);
          battle_end_with(g.outcome);
          ended = true;
          break;
        }
      }
    }
    if (!ended) {
      bool check_defeat = true;
      if (g.config[0x181] == 3 && g.state == kStateFighting) {
        // The time limit (60 s).
        if (g.flags != 0 || g.rule_timer < 0xE11) {
          if ((g.flags & 0xBF) == 0) g.rule_timer = static_cast<s16>(g.rule_timer + g.frame_ticks);
        } else {
          battle_end_with(kTimeUp);
          check_defeat = false;
        }
      }
      if (check_defeat && party_count_standing() == 0 && g.state == kStateFighting && (g.flags & 0x40) == 0)
        battle_party_lost();
    }
    if (g.end_countdown != 0 && (g.flags & 4) == 0) {
      if ((g.flags & 8) == 0) g.end_countdown = static_cast<s8>(g.end_countdown - g.frame_ticks);
      if (g.end_countdown < 1) {
        // Wait while a living member is in a killer move or a sprite still belongs to a
        // living unit or the effect bank.
        int idle = 0;
        for (PartyUnit* u : g.party_units)
          if (unit_is_down(u) || u->action < 0x10) ++idle;
        bool busy = idle != 4;
        if (!busy) busy = battle_sprites_busy();
        if (!busy) {
          g.end_countdown = 0;
          g.state = kStateResult;
          battle_free_party_models(g.outcome == kVictory);
          // (bload_done(0xa10) and the field program parked in VRAM: the field is native.)
          battle_queue_field_preloads();
          result_screen_start();  // battle_fade_task follows it and sets state 5
        } else {
          g.end_countdown = 2;
        }
      }
    }
    battle_debug_frame();
    text_frame_upload();
    // screen_compact_ot(layer 0).
    psyq::DrawSync(0);
    // VSync(2); a debug run can shorten the first frames (battle_debug_frame_wait).
    int wait = battle_debug_frame_wait(frame_no++);
    if (const int t = battle_debug_tick_map(g.battle_frames); t != 0) wait = t;  // (world agent: SS_BATTLE_TICK_MAP)
    psyq::VSync(wait == 1 ? 0 : wait);
    // VsyncCounter (vsync_counter_tick 0x80047578): vblanks since the last frame, capped
    // at 4 (6 during a special move).
    const u64 now = rt.vblanks();
    s32 ticks = static_cast<s32>(now - g.last_frame_vblank);
    const s32 cap = (g.flags & 0x40) ? 6 : 4;
    if (ticks > cap) ticks = cap;
    if (ticks < 1) ticks = 1;
    g.last_frame_vblank = now;
    g.frame_ticks = ticks;
    if ((g.flags & 4) == 0) {
      g.battle_frames += g.frame_ticks;
      if (g.state == kStateFighting) g.fight_frames += g.frame_ticks;
    }
    // Both draw environments clear to the stage colour (the alternate one for a band of pitches).
    ViewState& v = view();
    for (int i = 0; i < 2; ++i) {
      psyq::DRAWENV* d = screen_drawenv(i);
      const u8* c = v.clear_rgb;
      if (static_cast<u16>(v.pitch + 0x781) < 0x704 && v.clear_alt_on != 0 && g.stage_hidden == 0) c = v.clear_rgb_alt;
      s32 r = c[0] * v.stage_colour[0] >> 7;
      s32 gg = c[1] * v.stage_colour[1] >> 7;
      s32 b = c[2] * v.stage_colour[2] >> 7;
      d->r0 = static_cast<u8>(r > 0xFF ? 0xFF : r);
      d->g0 = static_cast<u8>(gg > 0xFF ? 0xFF : gg);
      d->b0 = static_cast<u8>(b > 0xFF ? 0xFF : b);
    }
    screen_put_env();
    if (g.dispmask_delay != 0 && g.dispmask_delay-- == 1) psyq::SetDispMask(1);
    for (int i = 7; i >= 0; --i)
      if (g.ot_layers[i] != nullptr) screen_draw_ot(g.ot_layers[i]->ot);
  }
}

// ---- Exit ---------------------------------------------------------------------------------

void battle_free_all() {
  BattleGlobals& g = g_battle;
  result_screen_free();
  spells_reset();          // the SpellCasts, their fades and the spell data
  voice_player_reset();    // the voice slots
  bload_reset();           // the load queue
  spell_windows_reset();   // the clash / skill-notice / scan windows
  command_menu_reset();    // the command ring's list menus
  battle_windows_free();  // the debug window and the HUD release themselves
  effect_tasks_free_all();  // afterimages, ghosts and ribbon trails still running
  g.tasks.fill(nullptr);
  g.task_count = 0;
  for (auto& rc : g.render_contexts) {
    if (rc->arena >= 0) screen_destroy_arena(rc->arena);
    if (rc->ot >= 0) screen_destroy_ot(rc->ot);
  }
  g.render_contexts.clear();
  g.prim_pool_a = g.prim_pool_b = nullptr;
  g.ot_layers.fill(nullptr);
  g.party_units.fill(nullptr);
  g.enemy_units.fill(nullptr);
  encounter_reset();
  party_reset();
  g.camera = nullptr;
  g.camera_obj.reset();
  sprite_pool_destroy();
  shot_pool_destroy();
  particle_pool_destroy();
}

void battle_exit_to_field() {
  BattleGlobals& g = g_battle;
  runtime::Runtime::get().trace(("battle exit to field, outcome " + std::to_string(g.outcome)).c_str());
  if (g.outcome == kVictory) {
    int down = 0;
    for (const PartyUnit* u : g.party_units)
      if (unit_is_down(u)) ++down;
    if (down == 4 && party_count_fled() == 0) g.outcome = kDefeat;
  }
  battle_debug_exit();  // (debug traces; SS_BATTLE_QUIT_AT_EXIT stops the run here)
  pad_vibrate(g.pad, 0, 0, 0);
  const u16 outcome = g.outcome;
  std::memcpy(g.config + 0x178, &outcome, 2);
  battle_free_all();
  psyq::ResetGraph(1);
  psyq::SetDispMask(0);
  // Handoffs 0xc (the field's reload list, s16[0x11]; kept entries negated) and 10 (the
  // kept entries' bytes in list order). The field frees both as u8 arrays.
  if (!g.preload_list.empty()) {
    auto* list = new u8[0x22];
    std::memcpy(list, g.preload_list.data(), 0x22);
    handoff_store(g.handoff, list, 0xC);
  }
  if (!g.handoff10_blob.empty()) {
    auto* blob = new u8[g.handoff10_blob.size()];
    std::memcpy(blob, g.handoff10_blob.data(), g.handoff10_blob.size());
    handoff_store(g.handoff, blob, 10);
  }
  g.preload_list.clear();
  g.handoff10_blob.clear();
  boot_run_main_program(g.system, Program::Field);
}

}  // namespace ss::game::battle
