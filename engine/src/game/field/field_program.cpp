#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

#include "disc/slz.h"
#include "game/boot/pad.h"
#include "game/boot/stubs.h"
#include "game/common/program_image.h"
#include "game/common/texture.h"
#include "game/field/actors.h"
#include "game/field/bg.h"
#include "game/field/callways.h"
#include "game/field/camera.h"
#include "game/boot/rng.h"
#include "game/field/actors.h"
#include "game/field/encounter.h"
#include "game/field/fade.h"
#include "game/field/music.h"
#include "game/field/field.h"
#include "game/field/load_queue.h"
#include "game/field/map.h"
#include "game/field/message.h"
#include "game/field/notice.h"
#include "game/field/music.h"
#include "game/field/screen.h"
#include "game/field/script_vm.h"
#include "game/field/stash.h"
#include "game/field/tasks.h"
#include "game/field/text_panel.h"
#include "runtime/libgpu.h"
#include "runtime/runtime.h"

namespace ss::game::field {
namespace {

FieldGlobals g_field;
Field* g_program = nullptr;   // 0x8002f8dc (g_current_program)
Screen g_screen;              // 0x80075290 points at a 0x10c-byte heap object
TextPanel* g_text_panels[3]{};  // 0x8007529c..0x800752a4

s32 rd32(const u8* p) {
  s32 v;
  std::memcpy(&v, p, 4);
  return v;
}
u16 rd16(const u8* p) {
  u16 v;
  std::memcpy(&v, p, 2);
  return v;
}

// The pad module's vblank counter (VsyncCounter, 0xc bytes, vtable 0x80073084;
// vsync_counter_tick 0x800521e8 runs on every vblank): vblanks since the last
// frame, capped at 3. The host reads the runtime's vblank count instead.
struct VsyncCounter {
  u64 last = 0;
  s16 frames() {
    const u64 now = runtime::Runtime::get().vblanks();
    const u64 n = now - last;
    last = now;
    return static_cast<s16>(n < 1 ? 1 : n > 3 ? 3 : n);
  }
} g_vsync_counter;

void todo(const std::string& what) {
  runtime::Runtime::get().log_once("field:" + what, "field: not ported yet: " + what);
}

}  // namespace

const u8* field_data(u32 address) { return program_data(kFieldProgramEntry, address); }

FieldGlobals& globals() { return g_field; }
Field* field_program() { return g_program; }

// field_construct (0x8004880c): the field program starts from a clean slate each time
// it runs (after the title, after a battle).
void field_construct_globals() {
  FieldGlobals& g = g_field;
  g.battle_preloaded = 0;
  g.battle_preload_requested = 0;
  g.vsync_mode = 0;
  g.disc_error = g.disc_error_prev = 0;
  g.returning = 0;
  g.show_battle_results = 0;
  g.draw_enabled = 1;
  g.field_active = 0;
  g.scripted_battle = 0;
  battle_preloads().last_group = 0;
  battle_preloads().last_stage = -1;
  map_data() = MapData{};
  field_load_buffer() = {};
  for (int i = 0; i < 16; ++i) sprite_pack_slots()[i] = SpritePackSlot{};
  preload_clear_table();
  task_list_clear();
}

void field_entry(System* sys) {
  // field_program_create (0x800487e4): new Field (0x50 bytes) -> field_construct.
  field_construct_globals();
  auto* f = new Field;
  g_program = f;
  f->state = 0;
  f->present = 0;
  // vtable[2] program_set_system, then vtable[1] field_run.
  f->system = sys;
  field_run(f);
}

void field_bind_system(Field*) {
  FieldGlobals& g = g_field;
  g.system = get_system();
  g.handoff = &g.system->handoff;
  g.loader = &g.system->loader;
  g.pad = &g.system->pad;
  g.sound = &g.system->sound;
  g.config = static_cast<u8*>(handoff_get(g.handoff, 2));
  g.inventory = static_cast<u8*>(handoff_get(g.handoff, 6));
  g.party = static_cast<u8*>(handoff_get(g.handoff, 3));
  g.records = static_cast<u8*>(handoff_get(g.handoff, 4));
  pad_set_option(g.pad, 1);
}

void field_bind_state_block(Field*) {
  FieldGlobals& g = g_field;
  auto* block = static_cast<u8*>(handoff_get(g.handoff, 9));
  g.state_block = block;
  g.value_stack = block + 0x100;
  g.script_flags = block + 0x8C0;
  std::memset(block + 0x198, 0, 8);
  g.script_globals = block + 0x180;
  g.field_flags = block + 0x180;
  g.game_state = block + 0x220;
}

void field_create_state_block() {
  FieldGlobals& g = g_field;
  auto* block = new u8[0xA40]();
  handoff_store(g.handoff, block, 9);
  u8* gs = block + 0x220;  // game_state
  auto* snapshot = static_cast<u8*>(handoff_get(g.handoff, 0xE));
  if (snapshot == nullptr) {
    // New game.
    block[0x338] = 3;
    block[0x241] = 0x80;
    block[0x242] = block[0x243] = 0;
    block[0x23C] = block[0x23D] = 0xFF;
    std::memset(block + 0x228, 0, 12);
    std::memset(block + 0x238, 0, 4);
    const auto* records = static_cast<const u8*>(handoff_get(g.handoff, 4));
    const char* entered = reinterpret_cast<const char*>(records + 0x24);
    if (g.config[0x45] == 0) {  // Claude
      gs[0x00] = 3;
      block[0x245] = 1;
      block[0x244] = 0;
      gs[0x1A] = 7;  // map 7: Milokeenia
      gs[0x1B] = 0;
      if (const int m = runtime::Runtime::get().debug_start_map; m >= 0) {  // --map N (once)
        gs[0x1A] = static_cast<u8>(m);
        gs[0x1B] = static_cast<u8>(m >> 8);
        runtime::Runtime::get().debug_start_map = -1;
      }
      block[0x8C0] |= 2;
      std::strcpy(reinterpret_cast<char*>(block + 0x248), entered);
      std::memcpy(block + 0x25C, field_data(0x800734C8), 5);  // "Rena"
    } else {  // Rena
      gs[0x00] = 4;
      block[0x244] = 1;
      block[0x245] = 0;
      gs[0x1A] = 0x12;  // map 18: Arlia
      gs[0x1B] = 0;
      std::memcpy(block + 0x248, field_data(0x800734D0), 7);  // "Claude"
      std::strcpy(reinterpret_cast<char*>(block + 0x25C), entered);
    }
    // The other ten characters' default names (game_state + 0x28 + 0x14 * n).
    const struct {
      int offset;
      u32 address;
      int bytes;
    } names[] = {{0x270, 0x800734D8, 7}, {0x284, 0x800734E0, 7}, {0x298, 0x800734E8, 5},
                 {0x2AC, 0x800734F0, 7}, {0x2C0, 0x800734F8, 7}, {0x2D4, 0x80073500, 5},
                 {0x2E8, 0x80073508, 6}, {0x2FC, 0x80073510, 7}, {0x310, 0x80073518, 5},
                 {0x324, 0x80073520, 8}};
    for (const auto& n : names) std::memcpy(block + n.offset, field_data(n.address), n.bytes);
    std::memset(g.config + 0x10, 0, 12);  // config+0x10, +0x14 and money (+0x18)
  } else {
    // Loading a game: the snapshot holds game_state (0x2a0) and script flags (0x170).
    std::memcpy(gs, snapshot, 0x2A0);
    std::memcpy(block + 0x8C0, snapshot + 0x2A0, 0x170);
    delete[] snapshot;
    handoff_clear(g.handoff, 0xE);
    g.returning = 1;
    const u16 v = rd16(block + 0x23C);
    if (static_cast<u16>(v - 1) < 0x5A) {
      std::memcpy(block + 0x23C, field_data(0x800750CC + v * 2), 2);
    } else {
      block[0x23C] = block[0x23D] = 0xFF;
    }
  }
}

void field_load_system_data() {
  // Entry #3110 (0xc26): strings, window and icon art, two CLUT rows.
  FieldGlobals& g = g_field;
  psx::Gpu& gpu = runtime::Runtime::get().gpu;
  std::vector<u8> packed(static_cast<size_t>(g.loader->entry_size(0xC26)));
  g.loader->load_sync(packed.data(), 0xC26, 0);
  auto unpacked = disc::slz::unpack_block(packed, 0, 0, nullptr);
  if (!unpacked) return;
  static std::vector<u8> data;
  data = std::move(*unpacked);
  g.system_data = data.data();
  g.system_strings = data.data() + rd32(data.data());
  u8* icons = data.data() + rd32(data.data() + 0x0C);
  g.ui_atlas = icons + 0xC;  // 0x80075328
  const u8* image = icons + rd32(icons + 4);
  upload_texture(gpu, reinterpret_cast<const u16*>(image + 4), 0, 0, 0x180, 0x100, rd16(image), rd16(image + 2));
  // Clear the first colour of each 16-colour palette in the icons' CLUT, then upload it at (0, 0x1ff).
  u8* clut = icons + rd32(icons) + 4;
  for (int i = 0; i < 16; ++i) std::memset(clut + 0x1E0 - i * 0x20, 0, 2);
  const psyq::RECT row{0, 0x1FF, 0x100, 1};
  psyq::LoadImage(&row, clut);
  u8* windows = data.data() + rd32(data.data() + 0x10);
  upload_texture(gpu, reinterpret_cast<const u16*>(windows + 0x208), 0, 0, 0x180, 0x1C8, 0x100, 0x30);
  for (int i = 0; i < 16; ++i) std::memset(windows + 0x1E4 - i * 0x20, 0, 2);
  upload_clut256(gpu, reinterpret_cast<const u16*>(windows + 4), 0, 0x1FE);
  psyq::DrawSync(0);
}

void field_setup_screen(Field*) {
  screen_setup_buffer0(&g_screen, 0, 0, 0x140, 0xF0, 8, 8, 0, 0);
  screen_setup_buffer1(&g_screen, 0, 0xE0, 0x140, 0xF0, 8, 8, 0, 0);
}

void field_create_subsystems(Field*) {
  // 0x800494dc, in the original order: Screen, ActorSystem (0x490), ScreenFade (0x98),
  // ScriptCtx (0x11cc), MsgSystem (0x20c), FieldPanel (0x6c), three TextPanels
  // (0x28), LoadQueue (0x160), the pad's VsyncCounter, Notice (0x3c), Callways
  // (0x24, task prio 1) and SceneRenderer (8). The task list itself is cleared by
  // field_construct.
  task_list_clear();
  preload_clear_table();
  screen_construct(&g_screen);
  actor_sys_create();  // 0x80075294: ActorSystem (0x80043674), task priority 2
  fade_construct(new ScreenFade);
  g_script_vm = vm_ctx_create(new ScriptCtx);  // 0x800752ac; registered by vm_init
  msg_system_create(new MsgSystem);
  // Section 1 of each map: its font goes to the message system, then the script VM
  // starts on its event script.
  map_hooks().font_loaded = [](const u8* font) { msg_set_map_font(font); };
  map_hooks().script_loaded = [] {
    g_map_script = map_data().script.data();
    vm_init(g_script_vm);
  };
  // TODO(port): FieldPanel (field_panel_create 0x80055cd4, 0x6c bytes).
  for (auto*& panel : g_text_panels) panel = new TextPanel;  // 0x8007529c / 0x800752a0 / 0x800752a4
  load_queue_construct(new LoadQueue);
  notice_create(new Notice);
  task_register(callways_create(new Callways), 1);
  // TODO(port): SceneRenderer (scene_renderer_create 0x8005f5c8).
}

// 0x8004b418: the party fell (a lost script battle). The VM stops, encounters stop, the
// leader and the event actors stand still (anim 0xff, shadows grown), the title program
// is read in the background and the music fades: state 6.
void field_begin_game_over(Field* f) {
  FieldGlobals& g = g_field;
  g.game_state[0x20] = 0;
  if (g_script_vm != nullptr) task_unregister(g_script_vm);
  g.field_flags[0x25] = 0;
  const s16 none = -1;
  std::memcpy(g.field_flags + 0x10, &none, 2);
  if (Actor* leader = field_actor(g.game_state[0x24])) {
    actor_set_anim(leader, -1);
    actor_and_flags(leader, 0, 0xFFFFFF7Fu);
  }
  for (int slot = 0; slot < 12; ++slot) {
    Actor* a = field_actor(slot);
    if (a == nullptr || a->anim != -1) continue;
    actor_stop(a);
    actor_set_anim(a, 0xFF);
    a->shadow_w = 0x800;
    a->shadow_h = 900;
    a->tinted = 0;
  }
  field_read_title_program();
  sound_fade_out(g.sound, 0x46);
  f->state = 6;
}

// field_update state 7: the game-over sequence. The BG fades to black from the kept
// colour, the actors (apart from the fallen, anim 0xff) leave, the rest darken, and the
// "GAME OVER" logo (UI atlas +0x1ba) fades in and out at the centre (OT 0x818).
void field_update_game_over(Field* f) {
  FieldGlobals& g = g_field;
  if (f->state_timer <= 0) {
    f->state = 8;
    return;
  }
  s32 t = f->state_timer - g.frame_ticks;
  if (t < 0x78) t = 0;
  f->state_timer = t;
  if (t > 0x1E0) return;
  g_fade_targets.bg_tint_active = 1;
  ActorSystem* sys = actor_system();
  auto set_actor_tint = [&](u8 v) {
    if (sys == nullptr) return;
    actor_sys_mark_all_dirty(sys);
    sys->tint[0] = sys->tint[1] = sys->tint[2] = v;
  };
  if (t < 0x1A5) {
    g_fade_targets.bg_tint[0] = g_fade_targets.bg_tint[1] = g_fade_targets.bg_tint[2] = 0;
    if (rd32(g.field_flags + 0x44) == 0) {
      int slot = 0;
      for (; slot < 12; ++slot) {
        Actor* a = field_actor(slot);
        if (a == nullptr) continue;
        if (a->anim == 0xFF)
          a->tinted = 1;
        else if (sys != nullptr)
          actor_delete_slot(sys, slot);
      }
      for (; slot < 0xC2; ++slot)
        if (field_actor(slot) != nullptr && sys != nullptr) actor_delete_slot(sys, slot);
      set_actor_tint(0x80);
      const s32 one = 1;
      std::memcpy(g.field_flags + 0x44, &one, 4);
    }
  } else {
    const s32 k = t - 0x1A4;
    for (int i = 0; i < 3; ++i)
      g_fade_targets.bg_tint[i] = static_cast<s16>((k * rd32(g.field_flags + 0x38 + i * 4)) / 0x3C);
    if (sys != nullptr) sys->tint_on = 1;
    set_actor_tint(static_cast<u8>((k * 0x80) / 0x3C));
  }
  if (t < 0x168) {
    s32 v = ((0x168 - t) * 0x80) / 0x3C;
    if (v > 0x80) v = 0x80;
    if (sys != nullptr) sys->tint_on = 1;
    set_actor_tint(static_cast<u8>(0x80 - v));
  }
  if (t > 299 || g.ui_atlas == nullptr) return;
  s32 a;
  if (t < 0xB5) {
    a = ((t - 0x78) * 0x80) / 0x3C;
    if (a < 0) a = 0;
  } else {
    a = ((300 - t) * 0x80) / 0x3C;
    if (a > 0x80) a = 0x80;
  }
  const u8* uv = g.ui_atlas + 0x1BA;  // u0, v0, u1, v1
  const int w = uv[2] - uv[0], h = uv[3] - uv[1];
  auto* p = g_screen.alloc_prims<psyq::POLY_FT4>();
  if (p == nullptr) return;
  psyq::setPolyFT4(p);
  psyq::setSemiTrans(p, true);
  p->r0 = p->g0 = p->b0 = static_cast<u8>(a);
  const s16 x0 = static_cast<s16>(0xA0 - w / 2), x1 = static_cast<s16>(w + 0xA0 - w / 2);
  const s16 y0 = static_cast<s16>(0x70 - h / 2), y1 = static_cast<s16>(h + 0x70 - h / 2);
  p->x0 = x0, p->y0 = y0;
  p->x1 = x1, p->y1 = y0;
  p->x2 = x0, p->y2 = y1;
  p->x3 = x1, p->y3 = y1;
  p->u0 = uv[0], p->v0 = uv[1];
  p->u1 = static_cast<u8>(uv[0] + w), p->v1 = uv[1];
  p->u2 = uv[0], p->v2 = static_cast<u8>(uv[1] + h);
  p->u3 = static_cast<u8>(uv[0] + w), p->v3 = static_cast<u8>(uv[1] + h);
  p->tpage = 0x36;
  p->clut = 0x7FC0;
  psyq::addPrim(g_screen.ot_entry(0x818), p);
}

// field_update state 0, the random-encounter part (0x8004c334): each area's rate
// (field_flags +0x25) sets a walking distance; once the battle's data is preloaded and
// the leader has walked that far (field_flags +0x18 >= +0x1c), the battle starts.
// 0x80049098: deletes what field_create_subsystems made (before another program runs).
// The pad's vblank callback is not registered on the host (the counter reads the runtime).
void field_destroy_subsystems(Field*) {
  if (MsgSystem* msg = msg_system()) {
    msg_system_destroy(msg);
    delete msg;
  }
  if (ActorSystem* sys = actor_system()) actor_sys_destruct(sys, 3);
  delete field_fade();  // ~ScreenFade: fade_destruct
  for (auto*& panel : g_text_panels) {
    delete panel;
    panel = nullptr;
  }
  if (g_script_vm != nullptr) {
    task_unregister(g_script_vm);
    delete g_script_vm;
    g_script_vm = nullptr;
  }
  if (LoadQueue* q = load_queue()) load_queue_destruct(q);
  delete g_notice_object();  // ~Notice: notice_destroy
  bg_destroy();
  if (CamScript* c = camscript()) camscript_destroy(c, 3);
  field_destroy_battle_transition(nullptr);
  if (Callways* c = callways()) callways_destroy(c);
  // TODO(port): FieldPanel, SceneRenderer, the glow and overlay objects.
}

void field_update_encounters(Field* f) {
  FieldGlobals& g = g_field;
  u8* flags = g.field_flags;
  auto get = [&](int off) { return rd32(flags + off); };
  auto put = [&](int off, s32 v) { std::memcpy(flags + off, &v, 4); };
  s32 rate = flags[0x25];
  if (rate == 0) {
    g.battle_preloaded = 0;
    put(0x1C, 0);
  } else if (get(0x1C) == 0) {
    // A new distance: the rate adjusted by the encounter modifiers (game_state
    // +0x266 / +0x267, field_flags +0x84; Callways songs 0x80076190, not ported), then
    // randomised by up to a third and scaled to the map.
    const int mod = static_cast<s8>(g.game_state[0x266]) + static_cast<s8>(g.game_state[0x267]) +
                    static_cast<s8>(flags[0x84]);
    if (mod >= 1) {
      rate -= (rate * mod) / 100;
      if (rate < 3) rate = 3;
    } else if (mod < 0) {
      rate += (rate * -mod) / 100;
    }
    runtime::Runtime::get().trace(("encounters: rate " + std::to_string(flags[0x25]) + " group " +
                                   std::to_string(static_cast<s16>(rd16(flags + 0x14))) + " stage " +
                                   std::to_string(static_cast<s16>(rd16(flags + 0x12))))
                                      .c_str());
    s32 distance = (rate * 2 * 8 + rate * 9) * 4;
    const s32 spread = distance / 3;
    if (spread != 0) {
      s32 r = rng_next() % spread;
      if ((rng_next() & 1) != 0) r = -r;
      distance += r;
      if (distance < 600) distance = 600;
      if (flags[0x68] == 0)
        put(0x1C, ((distance * rd16(map_data().scene.data() + 0x68)) / 100) * 0x1000);
      else
        put(0x1C, distance << 12);
    }
  } else if (!preload_table_saved() && g.battle_preloaded == 0) {
    // Preload the battle's entries once its voice clips are in (and not inside a map
    // transfer's trigger, nor while music loads).
    if (g.battle_preload_requested != 0 && voice_stream().state > 2) {
      const int tag = preload_get_kind2_tag();
      if ((tag == -1 || g_script_vm == nullptr || !trigger_contains_leader(g_script_vm, tag)) &&
          (g_script_vm == nullptr || g_script_vm->music == -1)) {
        const BattlePreloads& bp = battle_preloads();
        int started = 0;
        for (; started < bp.count; ++started) {
          if (preload_start(bp.entry[started], 1, -1, bp.bytes[started]) == 0) {
            g.battle_preloaded = 0;
            g.battle_preload_requested = 0;
            preload_release(1);
            break;
          }
        }
        if (started == bp.count) g.battle_preloaded = 1;
      }
    }
  }
  if (g.battle_preloaded == 0) {
    if (get(0x1C) == 0 || flags[0x26] != 0 || static_cast<s16>(rd16(flags + 0x10)) == -1) return;
    field_prepare_encounter(f, static_cast<s16>(rd16(flags + 0x14)), static_cast<s16>(rd16(flags + 0x12)));
    return;
  }
  const int ready = field_battle_preloads_ready();
  if (voice_stream().state < 3 || ready == 0) return;
  if (get(0x20) == 0) {
    put(0x20, get(0x18));
    const s32 extra = static_cast<s32>(static_cast<u32>(rng_next()) % 300) + 300;
    f->encounter_extra = extra * 0x1000;
    if (const Actor* leader = field_actor(g.game_state[0x24]); leader != nullptr && leader->walked_last != 0)
      put(0x20, get(0x20) - extra * 0x1000);
    if (get(0x20) == 0) return;
  }
  if (get(0x18) < get(0x20) + f->encounter_extra) return;
  if (get(0x18) < get(0x1C)) return;
  if (static_cast<s16>(rd16(flags + 0x10)) == -1 || flags[0x26] != 0) return;
  field_start_battle(f, ready - 1);
  f->state = 4;
}

// 0x8004c250: poisoned members (status nibble 8, slot bits 16-19) lose a 33rd of their
// maximum HP (at least 1, never below 1 HP); the screen flashes green.
void field_poison_step(Field*) {
  FieldGlobals& g = g_field;
  bool hurt = false;
  for (int slot = 0; slot < 8; ++slot) {
    u8* p = g.party + slot * 0x60;
    const u32 w = static_cast<u32>(rd32(p));
    if (static_cast<s16>(w) <= 0 || (w & 0xF0000) != 0x80000) continue;
    s32 damage = rd32(p + 0x18) / 0x21;
    if (damage < 1) damage = 1;
    s32 hp = rd32(p + 0x1C) - damage;
    if (hp < 1) hp = 1;
    std::memcpy(p + 0x1C, &hp, 4);
    hurt = true;
  }
  if (hurt) fade_draw_tile(field_fade(), 0, 0x80, 0, 1, 1, 0x80C);
}

// Debug (SS_TRACE_LEADER=N): the leader's position in map units and the field state every
// N vblanks, for placing --map-pos and checking trigger regions.
void trace_leader(Field* f) {
  static const int every = [] {
    const char* v = std::getenv("SS_TRACE_LEADER");
    return v != nullptr ? std::max(1, std::atoi(v)) : 0;
  }();
  if (every == 0) return;
  auto& rt = runtime::Runtime::get();
  if (rt.vblanks() % static_cast<u64>(every) != 0) return;
  const Actor* leader = field_actor(g_field.game_state[0x24]);
  std::fprintf(stderr, "[leader] vblank %llu map %d state %d pos %s%d,%d,%d view %d,%d control %d fol %d\n",
               static_cast<unsigned long long>(rt.vblanks()), static_cast<s16>(read_u16(g_field.game_state + 0x1A)), f->state,
               leader == nullptr ? "(none) " : "", leader ? leader->x >> 12 : 0, leader ? leader->y >> 12 : 0,
               leader ? leader->z >> 12 : 0, leader ? leader->screen_x : 0, leader ? leader->screen_y : 0,
               static_cast<s16>(read_u16(g_field.field_flags + 0x10)), rd32(g_field.config + 0x18));
}

void field_update(Field* f) {
  // 0x8004c334. Before the switch (state 0 only): the save-point marker check and the
  // menu button (pressed & config button 2, or a forced menu 0x80076196) -> fade
  // out, close the windows, state 3 (camp menu).
  FieldGlobals& g = g_field;
  trace_leader(f);
  if (f->state == 0 && (g.pressed[0] & read_u16(g.config + 4)) != 0) todo("field_update: camp menu");
  switch (f->state) {
    case 0:
      // Play. Every 400 steps (scaled by scene +0x68 on field maps) the walking effects
      // tick. TODO(port): the post-battle stat window (0x80076278), the save point and
      // vehicles (field_flags / script flag bits).
      if (field_actors()[g.game_state[9]] != nullptr && g.poison_next_step < (rd32(g.field_flags + 0x18) >> 12)) {
        callways_step_tick(callways());
        field_poison_step(f);
        const s32 steps = rd32(g.field_flags + 0x18) >> 12;
        g.poison_next_step = g.field_flags[0x68] != 0 ? steps + 400 : steps + (read_u16(map_data().scene.data() + 0x68) * 400) / 100;
      }
      field_update_encounters(f);
      return;
    case 1: {
      // Map change: wait out the fade, then load the pending map; the leader takes
      // control again (field_flags +0x10).
      if (!fade_is_active(field_fade(), -1)) {
        field_load_map(f, f->pending_map, false, 0);
        f->state = 0;
        const u16 leader = g.game_state[0x24];
        std::memcpy(g.field_flags + 0x10, &leader, 2);
      }
      return;
    }
    case 2:  // forced battle: wait for the battle's voice clips too
      if (voice_stream().state < 3) return;
      [[fallthrough]];
    case 4:  // random battle: wait for the transition to end
      if (BattleTransition* t = battle_transition(); t != nullptr && t->done != 0) {
        field_destroy_battle_transition(f);
        f->state = 5;
        g.draw_enabled = 0;
      }
      return;
    case 5:  // hand over once #2129 and every battle preload are in
      if (field_battle_program_loaded() && preload_count_done(1) == battle_preloads().count) {
        g.field_flags[0x7E] = 1;
        field_exit_to_program(f, 0);
      }
      return;
    case 0x21: {
      // Forced battle (scripts / menus): encounter -(game_state +0x264), stage
      // field_flags +0x12; its entries are preloaded at once.
      if (fade_is_active(field_fade(), -1)) return;
      g.scripted_battle = 1;
      if (voice_stream().state == 0) {
        voice_stream().state = 1;  // TODO(port): voice-clip queue
        load_queue_set_streaming(load_queue(), true);
      }
      field_prepare_encounter(f, -static_cast<s16>(rd16(g.game_state + 0x264)),
                              static_cast<s16>(rd16(g.field_flags + 0x12)));
      std::memset(g.game_state + 0x264, 0, 2);
      const BattlePreloads& bp = battle_preloads();
      for (int i = 0; i < bp.count; ++i) preload_start(bp.entry[i], 1, -1, bp.bytes[i]);
      g.scripted_battle = 0;
      field_start_battle(f, 2);
      const s16 control = static_cast<s16>(f->saved_control);
      std::memcpy(g.field_flags + 0x10, &control, 2);
      f->state = 2;
      return;
    }
    case 6: {
      // Game over, once the screen has faded: 540 frames of sequence, the field music
      // fading; the colour the BG fades from is kept in field_flags +0x38..+0x40.
      if (fade_is_active(field_fade(), -1)) return;
      f->state_timer = 0x21C;
      f->state = 7;
      sound_fade_out(g.sound, 0x46);
      if (g.field_flags[0x68] == 0) {
        s32 rgb[3] = {0x80, 0x80, 0x80};
        if (g_fade_targets.bg_tint_active != 0)
          if (const BgRenderer* bg = bg_renderer()) rgb[0] = bg->tint[0], rgb[1] = bg->tint[1], rgb[2] = bg->tint[2];
        std::memcpy(g.field_flags + 0x38, rgb, 12);
      } else {
        const s32 rgb[3] = {g_fade_targets.bg_tint[0], g_fade_targets.bg_tint[1], g_fade_targets.bg_tint[2]};
        std::memcpy(g.field_flags + 0x38, rgb, 12);
      }
      std::memset(g.field_flags + 0x44, 0, 4);
      return;
    }
    case 7:
      field_update_game_over(f);
      return;
    case 8:
      // The title program is in: tear everything down and start it like a reset.
      if (!field_battle_program_loaded()) return;
      if (ActorSystem* sys = actor_system()) actor_delete_slot(sys, -1);
      if (MsgSystem* msg = msg_system()) msg_window_destroy(msg, -1);
      task_list_clear();
      field_music_sequence().clear();
      field_unload_map(f, true);
      field_destroy_subsystems(f);
      preload_release(-1);
      if (auto* block = static_cast<u8*>(handoff_get(g.handoff, 9))) {
        handoff_clear(g.handoff, 9);
        delete[] block;
      }
      sound_stop(g.sound);
      psyq::ResetGraph(1);
      psyq::SetDispMask(0);
      delete f;
      boot_restart_title(g.system);
    default:
      // TODO(port): 3 camp menu, battle results (0xf / 0x10), save point,
      // vehicles, world map transfers (0x8004c334 cases 3, 6-0x20).
      todo("field_update: state " + std::to_string(f->state));
      return;
  }
}

// 0x80049738: the battle program keeps the field's preloaded map files in handoff 10
// (one blob, the entries handoff 0xc lists as -id, in list order). The console parks
// the blob in VRAM at (0x140, 0) to free main RAM, then reads each entry back into its
// own buffer as a finished kind-2 preload; the host copies straight from the blob. On
// the world map, list entry 2 (the terrain chunk) is read from the disc again.
void field_restore_preloads(Field*) {
  FieldGlobals& g = globals();
  auto* blob = static_cast<u8*>(handoff_get(g.handoff, 10));
  if (blob == nullptr) return;
  auto* list = static_cast<s16*>(handoff_get(g.handoff, 0xC));
  const int count = list != nullptr ? list[0x10] : 0;
  size_t offsets[16]{};
  size_t at = 0;
  for (int i = 0, kept = 0; i < count && i < 16 && kept < 12; ++i) {
    if (list[i] >= -1) continue;
    offsets[i] = at;
    at += static_cast<size_t>(g.loader->entry_size(-list[i]));
    ++kept;
  }
  preload_release(-1);
  PreloadSlot* table = preload_table();
  int slot = 0;
  for (int i = 0; i < count && slot < 12; ++i) {
    // The first six are taken in the order of the table at 0x800744cc.
    const int k = i < 6 ? rd32(field_data(0x800744CC + i * 4)) : i;
    if (list[k] < -1) {
      PreloadSlot& p = table[slot++];
      p.owned = 1;
      p.kind = 2;
      p.done = 1;
      p.tag = -1;
      p.bytes = 0;
      p.entry = -list[k];
      const size_t size = static_cast<size_t>(g.loader->entry_size(p.entry));
      p.data.assign(blob + offsets[k], blob + offsets[k] + size);
    } else if (g.field_flags[0x68] != 0 && k == 2) {
      list[2] = static_cast<s16>(-list[2]);
      PreloadSlot& p = table[slot++];
      p.owned = 1;
      p.kind = 2;
      p.done = 1;
      p.tag = -1;
      p.bytes = 0;
      p.entry = -list[2];
      p.data.assign(static_cast<size_t>(g.loader->entry_size(p.entry)), 0);
      g.loader->load_sync(p.data.data(), p.entry, 0);
    }
  }
  // The console then waits for the sound driver to restore its saved music (Sound +4).
  delete[] blob;
  handoff_clear(g.handoff, 10);
}

// 0x80052bb8: the party slots' character ids come back from field_flags +0x38.
void party_restore_slot_ids(Field*) {
  FieldGlobals& g = globals();
  for (int i = 0; i < 8; ++i) {
    const s32 id = rd32(g.field_flags + 0x38 + i * 4);
    const u16 v = static_cast<u16>(id);
    std::memcpy(g.party + i * 0x60, &v, 2);
  }
}

void field_run(Field* f) {
  FieldGlobals& g = g_field;
  psyq::DrawSync(0);
  field_bind_system(f);
  g.config[0x181] = 0;
  g.config[0x182] = 0;
  if (handoff_get(g.handoff, 9) == nullptr) {
    field_create_state_block();
    while (sound_stream_busy(g.sound) != 0) {
    }
  }
  field_bind_state_block(f);
  // A loaded game checks the disc in the drive against the save's (config +0x4c);
  // the host's disc is whichever the save needs. TODO(port): disc 2 (field_prompt_disc_change 0x80052ce8).
  if (g.field_flags[0x24] != 0) {
    // Back from another program (a battle).
    g.show_battle_results = 1;  // 0x80076278
    sound_holder_restore();
    // The outcome (config +0x178: 0 won, 1 escaped, 2 wiped out, 3 ran off, 4 time up)
    // goes to the script register R; escapes are counted.
    const s16 outcome = static_cast<s16>(rd16(g.config + 0x178));
    vm_set_r(static_cast<u32>(static_cast<s32>(outcome)));
    if (outcome == 1) {
      const s32 escapes = rd32(g.game_state + 0x244) + 1;
      std::memcpy(g.game_state + 0x244, &escapes, 4);
    }
    // The console re-allocates handoffs 0xb, 0x10, 0xd and 0xc here to compact its heap.
    field_restore_preloads(f);
    party_restore_slot_ids(f);
  }
  stash_init();  // handoff 0x11
  g.battle_setup = BattleSetup{};  // 0x800752b8 (0xc0 bytes); the music sequence buffer is music.cpp's
  field_create_subsystems(f);
  if (g.field_flags[0x24] != 0) callways_clear_encounter_flags(callways());
  field_main_loop(f);
}

void field_main_loop(Field* f) {
  FieldGlobals& g = g_field;
  field_load_system_data();
  screen_alloc_arena(&g_screen);
  screen_layout_arena(&g_screen, 1);
  // 0x8006ff3c: GTE setup (as the title's new_game_gte_setup).
  field_setup_screen(f);
  screen_set_clear_colour(&g_screen, 0, 0, 0);
  for (int i = 0; i < 2; ++i) {
    screen_flip(&g_screen);
    psyq::VSync(0);
    psyq::PutDrawEnv(&g_screen.current->draw);
    psyq::PutDispEnv(&g_screen.current->disp);
  }
  pad_set_port_mask(g.pad, 3);
  pad_set_repeat_delay(g.pad, 12);
  const s16 map = static_cast<s16>(rd16(g.game_state + 0x1A));
  field_load_map(f, map, g.returning != 0, g.returning != 0);
  f->present = 1;
  g_vsync_counter.frames();
  for (;;) {
    screen_flip(&g_screen);
    pad_set_option(g.pad, 1);
    screen_clear_ot(&g_screen);
    for (int port = 0; port < 2; ++port) {
      g.held[port] = pad_current(g.pad, port);
      g.pressed[port] = pad_pressed(g.pad, port);
      g.repeat[port] = pad_pressed_repeat(g.pad, port);
    }
    pad_clear(g.pad, 0);
    pad_clear(g.pad, 1);
    g.disc_error_prev = g.disc_error;
    g.disc_error = 0;  // the host disc never opens
    field_update(f);
    // TODO(port): field_update_encounter_streaming (0x80050dec).
    task_run_all();
    psyq::DrawSync(0);
    psyq::VSync(g.vsync_mode);
    g.frame_ticks = g_vsync_counter.frames();
    g.gpu_busy = 0;
    if (f->present == 0) {
      f->present = 1;
    } else {
      psyq::PutDispEnv(&g_screen.current->disp);
      psyq::PutDrawEnv(&g_screen.current->draw);
    }
    if (g.draw_enabled) {
      screen_compact_ot(&g_screen);
      psyq::DrawOTag(g_screen.current->ot.data());
    }
  }
}

}  // namespace ss::game::field
