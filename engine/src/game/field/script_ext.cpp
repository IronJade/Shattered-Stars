// vm_exec_ext (field 0x8006241c): opcodes 0x64-0xff. Arguments come from
// vm_fetch_args into g_vm_args (scratchpad 0x1f800000 on the console): inline after the
// instruction, or popped from the value stack when bit 23 is set. Opcodes the jump
// table sends to the default case (0x7e, 0x82, 0xa6, 0xa7, 0xb2, 0xc1, 0xc6, 0xc7,
// 0xfd) and anything outside 0x64-0xff do nothing.
#include <cstdio>

#include "game/ap/ap.h"
#include "game/boot/pad.h"
#include "game/common/math.h"
#include "game/field/script_vm.h"
#include "game/field/vm_services.h"
#include "runtime/runtime.h"

namespace ss::game::field {
namespace {

s32* const a = g_vm_args;

// A string of the map script by index (u16 offsets into the text).
const u8* script_string(ScriptCtx* ctx, s32 index) {
  // [world agent] The console's address arithmetic is 32-bit: index -1 (a script's computed
  // index, seen from --map starts on maps 71 and 277) reads the u16 just before the table
  // rather than 8 GB past it.
  return ctx->text + read_u16(ctx->strings + static_cast<s32>(2u * static_cast<u32>(index)));
}

// A message window's text: the script's string, or (Archipelago, port feature) the message
// of a seed placeholder given just before ("Got Blueberries." / "Sent Hookshot to Alice.").
const u8* message_text(ScriptCtx* ctx, s32 index) {
  if (const u8* text = ap::take_message_override()) return text;
  return script_string(ctx, index);
}

// Two 16-bit inline halves combined (the second argument carries bits 16-31).
s32 join(s32 lo, s32 hi) { return static_cast<s32>((static_cast<u32>(lo) & 0xFFFF) | static_cast<u32>(hi) << 16); }

// Angle in degrees (0-359) from a ratan2 result (4096 = 360).
s32 degrees(s32 angle) { return static_cast<s32>((static_cast<u32>(angle) + 0x1000) & 0xFFF) * 0x168 >> 12; }

// The party slot holding a character (active or reserve), or 8.
int slot_of_character(int character) {
  const u8* party = globals().party;
  int i = 0;
  for (; i < 8; ++i) {
    int id = vm_rd16(party + i * 0x60);
    if (id < 0) id = -id;
    if (id == character + 1) break;
  }
  return i;
}

// The voice cache rebuilt for a battle (ext 0xb7 / 0xbc / 0xed).
void prepare_battle_voices() {
  vm_svc_set_force_streaming(1);
  if (vm_svc_get_voice_cache_state() == 0) {
    vm_svc_voice_queue_abort();
    vm_svc_set_voice_cache_state(1);
    int budget = vm_svc_voice_party_spu_needed();
    vm_svc_set_voice_budget(budget);
    vm_svc_voice_queue_reset();
    budget = vm_svc_voice_queue_build_battle(budget);
    vm_svc_set_voice_budget(budget);
    vm_svc_load_queue_set_streaming(1);
  }
}

// Party changes of slots 0-3 drop the slot's battle voices.
void invalidate_voice(int slot) {
  vm_svc_voice_slot_invalidate(slot);
  vm_svc_load_queue_set_streaming(0);
  vm_svc_set_voice_cache_state(0);
}

// Message window opened by 0x64-0x6f: hold (0x68/0x69, 0x6a/0x6b) and no frame
// (0x6c/0x6d, 0x6e/0x6f).
void window_style(int window, bool hold, bool frameless) {
  vm_svc_msg_window_set_hold(window, hold ? 1 : 0);
  if (frameless) {
    vm_svc_msg_window_set_frame(window, 0, 0);
  } else {
    vm_svc_msg_window_set_frame(window, 1, 1);
  }
}

}  // namespace

void vm_exec_ext(ScriptCtx* ctx, u32 instr) {
  FieldGlobals& g = globals();
  const u32 op = instr >> 24;
  const bool inline_args = (instr & 0x800000) == 0;
  if (g_vm_ext_hook != nullptr) g_vm_ext_hook(ctx, instr);  // instrumentation
  ctx->instr = instr;
  switch (op) {
    case 0x64: case 0x65: case 0x68: case 0x69: case 0x6C: case 0x6D: {
      // msg_open(x, y, w, h, string, [window]): the odd forms name the window.
      const bool named = op == 0x65 || op == 0x69 || op == 0x6D;
      int window;
      if (inline_args) {
        if (named) {
          vm_fetch_args(ctx, 8);
        } else {
          vm_fetch_args(ctx, 7);
          a[7] = 0;
        }
        a[5] = join(a[5], a[6]);
        vm_svc_msg_window_open(a[0], a[1], a[2], a[3], message_text(ctx, a[5]), a[7], 1);
        window = a[7];
      } else {
        if (named) {
          vm_fetch_args(ctx, 6);
        } else {
          vm_fetch_args(ctx, 5);
          a[5] = 0;
        }
        vm_svc_msg_window_open(a[0], a[1], a[2], a[3], message_text(ctx, a[4]), a[5], 1);
        window = a[5];
      }
      window_style(window, op == 0x68 || op == 0x69, op == 0x6C || op == 0x6D);
      break;
    }
    case 0x66: case 0x67: case 0x6A: case 0x6B: case 0x6E: case 0x6F: {
      // Short form at the default position: bottom of the screen unless the leader
      // stands low on it.
      const int leader = leader_slot();
      if (!vm_svc_actor_exists(leader)) {
        vm_svc_msg_set_default_y(2);
      } else {
        vm_svc_msg_set_default_x(2);
        vm_svc_msg_set_default_y(vm_svc_actor_get_screen_y(leader) < 0x60 ? 0x10 : 2);
      }
      const bool named = op == 0x67 || op == 0x6B || op == 0x6F;
      int window;
      if (inline_args) {
        if (named) {
          vm_fetch_args(ctx, 4);
        } else {
          vm_fetch_args(ctx, 3);
          a[3] = 0;
        }
        a[1] = join(a[1], a[2]);
        vm_svc_msg_window_open_default(script_string(ctx, a[1]), a[3], 1);
        window = a[3];
      } else {
        if (named) {
          vm_fetch_args(ctx, 2);
        } else {
          vm_fetch_args(ctx, 1);
          a[1] = 0;
        }
        vm_svc_msg_window_open_default(script_string(ctx, a[0]), a[1], 1);
        window = a[1];
      }
      window_style(window, op == 0x6A || op == 0x6B, op == 0x6E || op == 0x6F);
      break;
    }
    case 0x70:  // actor_create(slot, model, x, y, z, angle, param, talk entry)
      if (inline_args) {
        vm_fetch_args(ctx, 9);
        a[7] = join(a[7], a[8]);
      } else {
        vm_fetch_args(ctx, 8);
      }
      vm_svc_actor_create(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7] & 0xFFFF, 0xFFFF, 0x1000F);
      break;
    case 0x71:
      vm_fetch_args(ctx, 1);
      vm_svc_actor_queue_delete(a[0]);
      break;
    case 0x72:
      vm_fetch_args(ctx, 2);
      g.config[0x181] = static_cast<u8>(a[0]);
      g.config[0x182] = static_cast<u8>(a[1]);
      break;
    case 0x73: {  // learn skill: record +0x3c + skill = 1
      vm_fetch_args(ctx, 2);
      const int slot = slot_of_character(a[0]);
      if (slot < 8) g.records[slot * 0xD0 + a[1] + 0x3C] = 1;
      break;
    }
    case 0x74: case 0x75: {  // trigger_add(corners x8, -, code[, y range])
      const int slot = trigger_alloc_slot(ctx);
      Trigger& t = ctx->triggers[slot];
      if (inline_args) {
        if (op == 0x75) {
          vm_fetch_args(ctx, 13);
        } else {
          vm_fetch_args(ctx, 11);
          a[11] = static_cast<s32>(0xFFFF8000u);
          a[12] = 0x7FFF;
        }
        const u32 lo = static_cast<u32>(a[9]) & 0xFFFF;
        a[9] = join(a[9], a[10]);
        t.code = ctx->code + (lo | (static_cast<u32>(a[10]) << 16 & 0x7FFFFF));
        t.y_min = static_cast<s16>(a[11]);
        t.y_max = static_cast<s16>(a[12]);
      } else {
        if (op == 0x75) {
          vm_fetch_args(ctx, 11);
        } else {
          vm_fetch_args(ctx, 9);
          a[9] = static_cast<s32>(0xFFFF8000u);
          a[10] = 0x7FFF;
        }
        t.code = ctx->code + (static_cast<u32>(a[8]) & 0x7FFFFF);
        t.y_min = static_cast<s16>(a[9]);
        t.y_max = static_cast<s16>(a[10]);
      }
      t.active = 1;
      t.private_action = -1;
      t.dwell = 0;
      t.menu_flags = 0;
      t.world_map = 0;
      t.buttons = 0xFFFFFFFFu;
      for (int i = 0; i < 8; ++i) t.corner[i] = a[i];
      trigger_compute_bbox(ctx, &t);
      vm_set_r(static_cast<u32>(slot));
      char line[160];
      std::snprintf(line, sizeof line, "trigger %d added: x %d..%d z %d..%d y %d..%d code word %ld", slot, t.min_x,
                    t.max_x, t.min_z, t.max_z, t.y_min, t.y_max, static_cast<long>(t.code - ctx->code));
      runtime::Runtime::get().trace(line);
      break;
    }
    case 0x76: case 0x77:
      worldmap_trigger_add(ctx, op);
      break;
    case 0x78:
      vm_fetch_args(ctx, 3);
      vm_svc_actor_set_9c(a[0], static_cast<s16>(a[1]), static_cast<s16>(a[2]));
      break;
    case 0x79:
      vm_fetch_args(ctx, 4);
      vm_svc_glow_create(a[0], a[1], a[2], a[3]);
      break;
    case 0x7A:  // wait
      vm_fetch_args(ctx, 1);
      ctx->wait += a[0];
      break;
    case 0x7B:  // player_lock
      vm_svc_actor_and_flags(leader_slot(), 0, 0xFFBFFFFF);
      g.field_flags[0x26] = 1;
      break;
    case 0x7C: case 0xB5:  // fade; the first one after vm_init gives the hero to the player
      if (ctx->first_fade != 0) {
        field_state_set_s16(0x10, (g.script_flags[0] >> 1 & 1) ^ 1);
        ctx->first_fade = 0;
      }
      if (op == 0x7C) {
        vm_fetch_args(ctx, 2);
        vm_svc_fade_push(a[0], a[1], 0, 0);
      } else {
        vm_fetch_args(ctx, 4);
        vm_svc_fade_push(a[0], a[1], a[2], a[3]);
      }
      break;
    case 0x7D: case 0xA4: case 0xDA: {  // map_transfer(map, transition, x, y, z, dir[, extra])
      if (op == 0x7D) {
        vm_fetch_args(ctx, 6);
        ctx->arrival_param = 0x80;
      } else if (op == 0xDA) {
        vm_fetch_args(ctx, 7);
        ctx->arrival_param = static_cast<u8>(a[6]);
      } else {
        vm_fetch_args(ctx, 7);
        ctx->arrival_param = 0x80;
        vm_wr16(g.game_state + 0x1E, static_cast<u32>(a[6]));
      }
      vm_svc_preload_release(1);
      ctx->wait_state = 1;
      ctx->wait_param = a[0];
      // The console copies a four-word VECTOR here, its fourth word (game_state +0x14)
      // uninitialised stack; the port leaves +0x14 as it was.
      vm_wr32(g.game_state + 8, static_cast<u32>(a[2]));
      vm_wr32(g.game_state + 0xC, static_cast<u32>(a[3]));
      vm_wr32(g.game_state + 0x10, static_cast<u32>(a[4]));
      ctx->arrival_dir = a[5];
      if (static_cast<u32>(ctx->wait_param - 1) >= 2) {  // not world maps 1 / 2
        vm_svc_preload_prune_for_map(ctx->wait_param);
        vm_preload_entry(ctx, ctx->wait_param + 0xC87, 0, 0);
      }
      ctx->transition = static_cast<u8>(a[1] & 0xF);
      ctx->transfer_flag = static_cast<u8>(a[1] & 0x80);
      if (ctx->transition == 0 || ctx->transition == 3) vm_svc_fade_push(3, 0x10, 0, 0);
      field_state_set_s16(0x10, -1);
      break;
    }
    case 0x7F:  // wait for a message window to close (-1: all)
      vm_fetch_args(ctx, 1);
      ctx->wait_state = 2;
      ctx->wait_param = a[0];
      break;
    case 0x80:
      vm_svc_msg_window_close(0);
      break;
    case 0x81:
      vm_fetch_args(ctx, 1);
      vm_svc_msg_window_close(a[0]);
      break;
    case 0x83:
      vm_fetch_args(ctx, 1);
      vm_set_r(vm_svc_msg_window_status(a[0]) != -1);
      break;
    case 0x84: {  // player_unlock: control (and the camera) back to the leader
      g.field_flags[0x26] = 0;
      const int leader = leader_slot();
      field_state_set_s16(0x10, leader);
      field_state_set_s16(0xE, leader);
      break;
    }
    case 0x85: case 0x86: case 0x87: case 0x88: case 0x89: {  // camera script, 3-7 arguments
      vm_fetch_args(ctx, static_cast<int>(op - 0x82));
      const int n = static_cast<int>(op - 0x85);
      if (n == 0) a[3] = 0x100;
      if (n <= 1) a[4] = 0;
      if (n <= 2) a[5] = 0;
      if (n <= 3) a[6] = 0x10000;
      vm_svc_camscript_push_bg(a[0], a[1], a[2], a[3], a[4], a[5], a[6]);
      break;
    }
    case 0x8A: case 0x8B: case 0x8C: case 0x8D: case 0x8E: {  // 4-8 arguments
      vm_fetch_args(ctx, static_cast<int>(op - 0x86));
      const int n = static_cast<int>(op - 0x8A);
      if (n == 0) a[4] = 0x100;
      if (n <= 1) a[5] = 0;
      if (n <= 2) a[6] = 0;
      if (n <= 3) a[7] = 0x10000;
      vm_svc_camscript_push_point(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7]);
      break;
    }
    case 0x8F: case 0x90: case 0x91: case 0x92: case 0x93: {  // 2-6 arguments
      vm_fetch_args(ctx, static_cast<int>(op - 0x8D));
      const int n = static_cast<int>(op - 0x8F);
      if (n == 0) a[2] = 0x100;
      if (n <= 1) a[3] = 0;
      if (n <= 2) a[4] = 0;
      if (n <= 3) a[5] = 0x10000;
      vm_svc_camscript_push_actor(a[0], a[1], a[2], a[3], a[4], a[5]);
      break;
    }
    case 0x94:
      vm_fetch_args(ctx, 4);
      vm_svc_actor_cmd_move_to_xz(a[0], static_cast<s16>(a[1]), static_cast<s16>(a[2]), static_cast<s16>(a[3]));
      break;
    case 0x95:
      vm_fetch_args(ctx, 5);
      vm_svc_actor_cmd_move_to(a[0], static_cast<s16>(a[1]), static_cast<s16>(a[2]), static_cast<s16>(a[3]),
                               static_cast<s16>(a[4]));
      break;
    case 0x96:
      vm_fetch_args(ctx, 4);
      vm_svc_actor_cmd_move_by_xz(a[0], static_cast<s16>(a[1]), static_cast<s16>(a[2]), static_cast<s16>(a[3]));
      break;
    case 0x97:
      vm_fetch_args(ctx, 5);
      vm_svc_actor_cmd_move_by(a[0], static_cast<s16>(a[1]), static_cast<s16>(a[2]), static_cast<s16>(a[3]),
                               static_cast<s16>(a[4]));
      break;
    case 0x98:
      vm_fetch_args(ctx, 4);
      vm_svc_actor_cmd_move_dir(a[0], static_cast<s16>(a[1]), static_cast<s16>(a[2]), static_cast<s16>(a[3]));
      break;
    case 0x99:
      vm_fetch_args(ctx, 4);
      vm_svc_actor_cmd_move_dir_rel(a[0], static_cast<s16>(a[1]), static_cast<s16>(a[2]), static_cast<s16>(a[3]));
      break;
    case 0x9A: {  // trigger_set_dwell
      vm_fetch_args(ctx, 2);
      Trigger& t = ctx->triggers[a[0]];
      t.dwell = 1;
      t.dwell_frames = static_cast<s16>(a[1]);
      break;
    }
    case 0x9B:
      vm_fetch_args(ctx, 4);
      vm_svc_actor_cmd_turn_to(a[0], static_cast<s16>(a[1]), static_cast<s16>(a[2]), static_cast<s16>(a[3]));
      break;
    case 0x9C:
      vm_svc_overlay_801f9fe8(ctx);
      break;
    case 0x9D:
      vm_fetch_args(ctx, 4);
      vm_svc_actor_cmd_turn_by(a[0], static_cast<s16>(a[1]), static_cast<s16>(a[2]), static_cast<s16>(a[3]));
      break;
    case 0x9E:
      vm_svc_overlay_801fa120(ctx);
      break;
    case 0x9F:
      vm_fetch_args(ctx, 4);
      vm_svc_actor_cmd_turn_to_actor(a[0], static_cast<s16>(a[1]), static_cast<s16>(a[2]), static_cast<s16>(a[3]));
      break;
    case 0xA0:
      vm_fetch_args(ctx, 2);
      vm_svc_actor_cmd_set_anim(a[0], static_cast<s16>(a[1]));
      break;
    case 0xA1:
      vm_fetch_args(ctx, 1);
      vm_set_r(static_cast<u32>(vm_svc_actor_is_busy(a[0])));
      break;
    case 0xA2:  // wait for an actor's command queue
      vm_fetch_args(ctx, 1);
      ctx->wait_state = 5;
      ctx->wait_param = a[0];
      break;
    case 0xA3:
      vm_fetch_args(ctx, 1);
      trigger_remove(ctx, static_cast<u32>(a[0]));
      break;
    case 0xA5: {  // preload the map container and party pack for a coming transfer
      vm_fetch_args(ctx, 3);
      int px, pz, pending;
      vm_svc_get_map_preload(&px, &pz, &pending);
      const bool same = px == a[1] && pz == a[2] && pending != 0;
      s16 files[17] = {};
      vm_svc_map_resolve_entries(files, a[0], a[1], a[2]);
      if (same) {
        if (vm_svc_get_preload_release_pending(0) != 0) {
          vm_svc_preload_release(1);
          vm_svc_set_preload_release_pending(0, 0);
        }
        if (vm_svc_preload_set_tag(files[2], ctx->active_trigger) != 0) return;
        if (vm_svc_preload_set_tag(files[3], ctx->active_trigger) != 0) return;
      }
      vm_preload_entry(ctx, files[2], 1, 0);
      vm_preload_entry(ctx, files[3], 1, 0);
      vm_svc_set_map_preload(a[1], a[2], 1);  // x / z before the preloads on the console; nothing reads them between
      break;
    }
    case 0xA8:
      vm_fetch_args(ctx, 1);
      vm_svc_field_special_command(a[0]);
      break;
    case 0xA9:
      vm_fetch_args(ctx, 1);
      vm_svc_set_encounter_7535c(static_cast<s16>(a[0]));
      break;
    case 0xAA:
      vm_fetch_args(ctx, 2);
      vm_svc_msg_window_set_24(a[1], static_cast<u8>(a[0]));
      break;
    case 0xAB: {  // assign a killer move: record +0xcc + a1 * 2 + a2 = a3
      vm_fetch_args(ctx, 4);
      const int slot = slot_of_character(a[0]);
      if (slot < 8) g.records[a[1] * 2 + slot * 0xD0 + 0xCC + a[2]] = static_cast<u8>(a[3]);
      if (static_cast<u32>(slot) < 4) invalidate_voice(slot);
      break;
    }
    case 0xAC: {  // unassign killer move a1 + 1 everywhere
      vm_fetch_args(ctx, 2);
      const int slot = slot_of_character(a[0]);
      if (slot < 8) {
        u8* moves = g.records + slot * 0xD0 + 0xCC;
        for (int i = 0; i < 4; ++i) {
          if (moves[i] == static_cast<u32>(a[1] + 1)) moves[i] = 0;
        }
      }
      break;
    }
    case 0xAD:  // camera target actor
      vm_fetch_args(ctx, 1);
      field_state_set_s16(0xE, a[0]);
      break;
    case 0xAE: {
      vm_fetch_args(ctx, 1);
      // Archipelago (port feature; character_items) [world agent]: a join site the seed lists
      // is a check, and the member joins only once their item is received. The site is this
      // instruction or the call into the library's join routine (the innermost frame's return
      // offset - 4).
      const int at = static_cast<int>(ctx->ip - 1 - ctx->code) * 4;
      const int call_at = ctx->depth > 0 ? static_cast<int>(ctx->call_sp[-1]) - 4 : -1;
      if (!ap::on_party_add(a[0], at, call_at)) party_add_member(ctx, a[0]);
      break;
    }
    case 0xAF:
      vm_fetch_args(ctx, 1);
      party_remove_member(ctx, a[0]);
      break;
    case 0xB0:
      vm_fetch_args(ctx, 2);
      vm_svc_party_swap_characters(a[0], a[1]);
      break;
    case 0xB1:
      vm_fetch_args(ctx, 1);
      vm_set_r(party_find_slot(ctx, a[0]) != -1);
      break;
    case 0xB3:
      vm_fetch_args(ctx, 1);
      vm_svc_actor_cmd_clear_emote(a[0]);
      break;
    case 0xB4:
      vm_fetch_args(ctx, 2);
      vm_svc_set_global_76224(static_cast<s16>(a[0]), static_cast<s16>(a[1]));
      break;
    case 0xB6:
      vm_fetch_args(ctx, 2);
      char_unequip_item(ctx, static_cast<s16>(a[0]), static_cast<s16>(a[1]));
      break;
    case 0xB7: {  // battle(encounter, background, param): preloads, then wait state 0xb
      vm_fetch_args(ctx, 3);
      g.field_flags[0x7E] = static_cast<u8>(a[2]);
      prepare_battle_voices();
      vm_svc_field_prepare_encounter(-a[0], a[1]);
      const int count = vm_svc_get_battle_preload_count();
      for (int i = 0; i < count; ++i) {
        if (vm_svc_preload_start(vm_svc_get_battle_preload_entry(i), 1, -1, vm_svc_get_battle_preload_size(i)) == 0) {
          vm_svc_field_error_stub(0x7F);
        }
      }
      ctx->wait_state = 0xB;
      break;
    }
    case 0xB8: case 0xC5: {  // music change (numbers below 100 through the music table)
      if (vm_svc_callways_song_active() != 0) vm_svc_callways_stop_song(1);
      if (op == 0xB8) {
        vm_fetch_args(ctx, 1);
        ctx->music_crossfade = 0;
      } else {
        vm_fetch_args(ctx, 3);
        ctx->music_crossfade = 1;
        ctx->music_fade_a = static_cast<u32>(a[1]);
        ctx->music_fade_b = static_cast<s16>(a[2]);
      }
      if (a[0] < 100) {
        const u8* entry = field_data(0x800750CC + 2 * static_cast<u32>(a[0]));
        a[0] = entry != nullptr ? vm_rd16(entry) : 0;
      }
      if (a[0] != 0) {
        if (vm_rd16(g.game_state + 0x1C) == a[0]) {
          if (op == 0xC5) vm_svc_sound_play_sequence_fade(static_cast<s16>(a[1]), static_cast<s16>(a[2]));
        } else {
          vm_svc_sound_stop();
          vm_wr16(g.game_state + 0x1C, static_cast<u32>(a[0]));
          ctx->music_loaded = 0;
          ctx->music = static_cast<s16>(a[0]);
          vm_svc_field_load_entry(ctx->music, &ctx->music_loaded, 0);
          ctx->music_installed = 0;
          ctx->wait_state = 8;
        }
      }
      break;
    }
    case 0xB9: {  // notice banner (string, hold); at the top when the leader is low on screen
      vm_fetch_args(ctx, 4);
      s16 cam_x = 0, cam_y = 0;
      a[1] = join(a[1], a[2]);
      vm_svc_bg_get_view_origin(&cam_x, &cam_y);
      const int leader = leader_slot();
      bool top = false;
      if (vm_svc_actor_exists(leader)) top = vm_svc_actor_get_view_y(leader) - cam_y > 0xB3;
      vm_svc_notice_show(script_string(ctx, a[1]), a[3], 7, top ? 1 : 0);
      break;
    }
    case 0xBA:  // music fade out
      vm_fetch_args(ctx, 1);
      if (vm_rd16(g.game_state + 0x1C) != -1 && ctx->music == -1) vm_svc_sound_fade_out(static_cast<s16>(a[0]));
      break;
    case 0xBB: {  // load a map-object entry (table 0x80077384) and wait for it
      vm_fetch_args(ctx, 1);
      const u8* e = field_data(0x80077384 + 2 * static_cast<u32>(a[0]));
      const s16 entry = e != nullptr ? vm_rd16(e) : 0;
      ctx->wait_param = entry;
      vm_preload_entry(ctx, entry, 1, 0);
      ctx->wait_state = 6;
      break;
    }
    case 0xBC: case 0xED: {  // battle preloads only (0xbc tags them with a trigger)
      s32 saved = 0;
      if (op == 0xBC) {
        vm_fetch_args(ctx, 3);
        saved = ctx->active_trigger;
        ctx->active_trigger = a[1];
      } else {
        vm_fetch_args(ctx, 2);
      }
      prepare_battle_voices();
      vm_svc_preload_release(2);
      vm_svc_field_prepare_encounter(-a[0], a[1]);
      const int count = vm_svc_get_battle_preload_count();
      for (int i = 0; i < count; ++i) {
        vm_svc_preload_start(vm_svc_get_battle_preload_entry(i), 1, -1, vm_svc_get_battle_preload_size(i));
      }
      if (op == 0xBC) ctx->active_trigger = saved;
      break;
    }
    case 0xBD:  // wait for a fade
      vm_fetch_args(ctx, 1);
      ctx->wait_state = 3;
      ctx->wait_param = a[0];
      break;
    case 0xBE:  // wait for the camera script
      ctx->wait_state = 4;
      break;
    case 0xBF:
      vm_svc_overlay_801f9bb4(ctx);
      break;
    case 0xC0:
      vm_fetch_args(ctx, 3);
      vm_svc_actor_cmd_wait_anim(a[0], static_cast<s16>(a[1]), static_cast<s16>(a[2]));
      break;
    case 0xC2: case 0xC3: case 0xC4:  // actor flags set / and / or
      if (inline_args) {
        vm_fetch_args(ctx, 4);
        a[2] = join(a[2], a[3]);
      } else {
        vm_fetch_args(ctx, 3);
      }
      if (op == 0xC2) {
        vm_svc_actor_set_flags(a[0], static_cast<s16>(a[1]), static_cast<u32>(a[2]));
      } else if (op == 0xC3) {
        vm_svc_actor_and_flags(a[0], static_cast<s16>(a[1]), static_cast<u32>(a[2]));
      } else {
        vm_svc_actor_or_flags(a[0], static_cast<s16>(a[1]), static_cast<u32>(a[2]));
      }
      break;
    case 0xC8:  // play_sfx(id, pan, volume)
      vm_fetch_args(ctx, 3);
      vm_set_r(static_cast<u32>(vm_svc_sound_play_sfx(a[0], static_cast<s8>(a[1]), a[2] & 0xFFFF)));
      break;
    case 0xC9:
      vm_fetch_args(ctx, 1);
      vm_svc_sound_release_clip(a[0]);
      break;
    case 0xCA:
      vm_fetch_args(ctx, 1);
      vm_set_r(static_cast<u32>(vm_svc_sound_effect_playing(a[0])));
      break;
    case 0xCB:
      vm_fetch_args(ctx, 5);
      vm_svc_sound_start_clip(a[0], a[1] & 0xFF, a[2] & 0xFF, static_cast<s16>(a[3]), a[4] & 0xFF);
      break;
    case 0xCC:
      vm_fetch_args(ctx, 1);
      vm_svc_sound_fade_in(static_cast<s16>(a[0]));
      break;
    case 0xCD:
      vm_fetch_args(ctx, 1);
      vm_svc_sound_fade_out_effects(static_cast<s16>(a[0]));
      break;
    case 0xCE:
      vm_fetch_args(ctx, 1);
      vm_svc_sound_set_tempo(static_cast<s8>(a[0]));
      break;
    case 0xCF:
      if (inline_args) {
        vm_fetch_args(ctx, 3);
        a[1] = join(a[1], a[2]);
      } else {
        vm_fetch_args(ctx, 2);
      }
      vm_svc_actor_cmd_signal(a[0], a[1] & 0xFFFF);
      break;
    case 0xD0:
      vm_fetch_args(ctx, 1);
      vm_svc_actor_cmd_wait_move(a[0]);
      break;
    case 0xD1:
      vm_fetch_args(ctx, 1);
      vm_svc_actor_cmd_wait_turn(a[0]);
      break;
    case 0xD2:  // move window 0
      vm_fetch_args(ctx, 3);
      vm_svc_msg_window_push_move(0, a[0], a[1], a[2] & 0xFFFF);
      break;
    case 0xD3:  // money
      vm_set_r(read_u32(g.config + 0x18));
      break;
    case 0xD4: {  // add money (clamped 0..999999999)
      if (inline_args) {
        vm_fetch_args(ctx, 3);
        a[0] = join(a[1], a[2]);
      } else {
        vm_fetch_args(ctx, 1);
      }
      // Archipelago (port feature): a seed's placeholder gives its location's item instead.
      if (ap::on_give_money(static_cast<s32>(a[0]))) break;
      const s32 money = static_cast<s32>(static_cast<u32>(vm_rd32(g.config + 0x18)) + static_cast<u32>(a[0]));
      if (money < 0) {
        vm_wr32(g.config + 0x18, 0);
      } else if (a[0] < 1000000000 && money < 1000000000) {
        vm_wr32(g.config + 0x18, static_cast<u32>(money));
      } else {
        vm_wr32(g.config + 0x18, 999999999);
      }
      break;
    }
    case 0xD5:
      vm_set_r(static_cast<u32>(party_count_members()));
      break;
    case 0xD6:
      vm_set_r(party_member_mask());
      break;
    case 0xD7: {  // angle from actor a0 to actor a1, degrees
      vm_fetch_args(ctx, 2);
      s32 x0, y0, z0, x1, y1, z1;
      vm_svc_actor_get_position(a[0], &x0, &y0, &z0);
      vm_svc_actor_get_position(a[1], &x1, &y1, &z1);
      vm_set_r(static_cast<u32>(degrees(ratan2(z1 - z0, x1 - x0))));
      break;
    }
    case 0xD8: case 0xD9: {  // trigger_add_button(buttons, corners x8, code[, y range])
      const int slot = trigger_alloc_slot(ctx);
      Trigger& t = ctx->triggers[slot];
      int first, y;
      if (inline_args) {
        if (op == 0xD8) {
          vm_fetch_args(ctx, 15);
        } else {
          vm_fetch_args(ctx, 13);
          a[13] = static_cast<s32>(0xFFFF8000u);
          a[14] = 0x7FFF;
        }
        first = 3;
        y = 13;
        t.buttons = static_cast<u32>(join(a[1], a[2]));
        const u32 lo = static_cast<u32>(a[11]) & 0xFFFF;
        a[11] = join(a[11], a[12]);
        t.code = ctx->code + (lo | (static_cast<u32>(a[12]) << 16 & 0x7FFFFF));
      } else {
        if (op == 0xD8) {
          vm_fetch_args(ctx, 12);
        } else {
          vm_fetch_args(ctx, 10);
          a[10] = -0x8000;
          a[11] = 0x7FFF;
        }
        first = 1;
        y = 10;
        t.buttons = static_cast<u32>(a[0]);
        t.code = ctx->code + (static_cast<u32>(a[9]) & 0x7FFFFF);
      }
      for (int i = 0; i < 8; ++i) t.corner[i] = a[first + i];
      t.y_min = static_cast<s16>(a[y]);
      t.active = 1;
      t.menu_flags = 0;
      t.dwell = 0;
      t.world_map = 0;
      t.private_action = -1;
      t.y_max = static_cast<s16>(a[y + 1]);
      trigger_compute_bbox(ctx, &t);
      vm_set_r(static_cast<u32>(slot));
      break;
    }
    case 0xDB:  // actor's direction, degrees
      vm_fetch_args(ctx, 1);
      vm_set_r(static_cast<u32>((vm_svc_actor_get_angle(a[0]) & 0xFFF) * 0x168 >> 12));
      break;
    case 0xDC: case 0xDD: case 0xDE: case 0xDF: case 0xE0: {
      // choice(options, first line, selection, cancel allowed, window): 1-5 arguments
      vm_fetch_args(ctx, static_cast<int>(op - 0xDB));
      ctx->choice_count = static_cast<s16>(a[0]);
      if (op == 0xDC) a[1] = 0;
      if (op <= 0xDD) a[2] = 0;
      if (op <= 0xDE) a[3] = 1;
      if (op <= 0xDF) a[4] = 0;
      ctx->choice_first_line = static_cast<s16>(a[1]);
      ctx->choice_selection = static_cast<s16>(a[2]);
      ctx->choice_cancel = static_cast<s16>(a[3]);
      ctx->wait_param = a[4];
      if (!vm_svc_msg_window_exists(a[4])) {
        vm_set_r(0xFFFFFFFFu);
      } else {
        vm_svc_msg_window_set_cancel(a[4], ctx->choice_cancel != 0);
        vm_svc_msg_window_set_close_flag(ctx->wait_param, 0);
        ctx->wait_state = 9;
        ctx->choice_close = 0;
        ctx->choice_cursor[0].x = -1;
        ctx->choice_slide = 0;
      }
      break;
    }
    case 0xE1:
      vm_fetch_args(ctx, 1);
      vm_svc_set_global_76194(static_cast<u8>(a[0]));
      break;
    case 0xE2: {  // angle from an actor to a point, degrees
      vm_fetch_args(ctx, 3);
      if (!vm_svc_actor_exists(a[0])) return;
      s32 x, y, z;
      vm_svc_actor_get_position(a[0], &x, &y, &z);
      vm_set_r(static_cast<u32>(degrees(ratan2(a[2] - (z >> 12), a[1] - (x >> 12)))));
      break;
    }
    case 0xE3: case 0xE4: case 0xE5: {  // actor x / y / z
      vm_fetch_args(ctx, 1);
      if (vm_svc_actor_exists(a[0])) {
        s32 x, y, z;
        vm_svc_actor_get_position(a[0], &x, &y, &z);
        vm_set_r(static_cast<u32>((op == 0xE3 ? x : op == 0xE4 ? y : z) >> 12));
      }
      break;
    }
    case 0xE6:  // sin of degrees
      vm_fetch_args(ctx, 1);
      vm_set_r(static_cast<u32>(sin4096(static_cast<u32>(static_cast<s32>(static_cast<u32>(a[0]) * 0x1000) / 0x168))));
      break;
    case 0xE7:
      vm_fetch_args(ctx, 1);
      vm_set_r(static_cast<u32>(cos4096(static_cast<u32>(static_cast<s32>(static_cast<u32>(a[0]) * 0x1000) / 0x168))));
      break;
    case 0xE8:
      vm_fetch_args(ctx, 1);
      break;
    case 0xE9:
      vm_fetch_args(ctx, 1);
      vm_svc_actor_cmd_wait_move_b(a[0]);
      break;
    case 0xEA:
      vm_set_r(vm_svc_get_global_761ea() != 0 ? vm_svc_get_global_76214() != -1 : 0);
      break;
    case 0xEB:
      vm_fetch_args(ctx, 5);
      vm_svc_actor_cmd_turn_to_point(a[0], static_cast<s16>(a[1]), static_cast<s16>(a[2]), static_cast<s16>(a[3]),
                                     static_cast<s16>(a[4]));
      break;
    case 0xEC:
      vm_fetch_args(ctx, 2);
      vm_svc_actor_cmd_wait(a[0], static_cast<s16>(a[1]));
      break;
    case 0xEE:
      vm_fetch_args(ctx, 4);
      vm_svc_actor_cmd_emote(a[0], static_cast<s16>(a[1]), static_cast<s16>(a[2]), static_cast<s16>(a[3]));
      break;
    case 0xEF:  // angle between two points, degrees
      vm_fetch_args(ctx, 4);
      vm_set_r(static_cast<u32>(degrees(ratan2(a[3] - a[1], a[2] - a[0]))));
      break;
    case 0xF0:
      vm_fetch_args(ctx, 6);
      vm_svc_actor_queue_move_to(a[0], a[1], a[2], a[3], a[4], a[5]);
      break;
    case 0xF1:
      vm_fetch_args(ctx, 2);
      vm_svc_actor_set_offset48(a[0], static_cast<s32>(static_cast<u32>(a[1]) << 12));
      break;
    case 0xF2:
      vm_fetch_args(ctx, 1);
      vm_set_r(static_cast<u32>(vm_svc_actor_get_offset48(a[0]) >> 12));
      break;
    case 0xF3:  // actor_create with a pickpocket entry
      if (inline_args) {
        vm_fetch_args(ctx, 11);
        a[7] = join(a[7], a[8]);
        a[8] = join(a[9], a[10]);
      } else {
        vm_fetch_args(ctx, 9);
      }
      vm_svc_actor_create(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7] & 0xFFFF, a[8] & 0xFFFF, 0x1000F);
      break;
    case 0xF4:
      vm_fetch_args(ctx, 6);
      vm_svc_actor_queue_move_by(a[0], a[1], a[2], a[3], a[4], a[5]);
      break;
    case 0xF5:  // move window n
      vm_fetch_args(ctx, 4);
      vm_svc_msg_window_push_move(a[3], a[0], a[1], a[2] & 0xFFFF);
      break;
    case 0xF6:
      vm_fetch_args(ctx, 1);
      vm_svc_bg_set_121(static_cast<u8>(a[0]));
      break;
    case 0xF7:  // distance between two points
      vm_fetch_args(ctx, 6);
      vm_set_r(static_cast<u32>(SquareRoot0((a[0] - a[3]) * (a[0] - a[3]) + (a[1] - a[4]) * (a[1] - a[4]) +
                                            (a[2] - a[5]) * (a[2] - a[5]))));
      break;
    case 0xF8:  // play-time clock
      vm_set_r(clock_seconds());
      break;
    case 0xF9:
      vm_fetch_args(ctx, 1);
      if (static_cast<u32>(a[0]) < 0x30) vm_set_r(read_u32(g.game_state + 0x180 + 4 * a[0]));
      break;
    case 0xFA:
      vm_fetch_args(ctx, 1);
      if (static_cast<u32>(a[0]) < 0x30) vm_wr32(g.game_state + 0x180 + 4 * a[0], clock_seconds());
      break;
    case 0xFB:
      vm_fetch_args(ctx, 2);
      vm_svc_msg_set_default_size(a[0], a[1]);
      break;
    case 0xFC: {  // forget skill a1: skill byte cleared, killer move a1 + 1 unassigned
      vm_fetch_args(ctx, 2);
      const int slot = slot_of_character(a[0]);
      if (slot < 8) {
        u8* rec = g.records + slot * 0xD0;
        rec[a[1] + 0x3C] = 0;
        for (int i = 0; i < 4; ++i) {
          if (rec[0xCC + i] == static_cast<u32>(a[1] + 1)) rec[0xCC + i] = 0;
        }
      }
      break;
    }
    case 0xFE:
      vm_exec_table_a(ctx, instr);
      break;
    case 0xFF:
      vm_exec_table_b(ctx, instr);
      break;
    default:  // 0x7e, 0x82, 0xa6, 0xa7, 0xb2, 0xc1, 0xc6, 0xc7, 0xfd and opcodes below 0x64
      vm_note_unknown(instr);
      break;
  }
}

}  // namespace ss::game::field
