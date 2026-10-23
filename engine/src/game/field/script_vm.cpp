#include "game/field/script_vm.h"

#include <cstring>

#include "game/ap/ap.h"
#include "game/boot/rng.h"
#include "game/boot/system.h"
#include "game/field/vm_services.h"

namespace ss::game::field {

ScriptCtx* g_script_vm = nullptr;
const u8* g_map_script = nullptr;
s32 g_vm_args[16] = {};
VmStats g_vm_stats;
u32 g_vm_step_limit = 0;
void (*g_vm_ext_hook)(ScriptCtx* ctx, u32 instr) = nullptr;

void ScriptCtx::update() { vm_thread_update(this); }

namespace {

// Byte distance between two pointers into script memory or code.
s32 byte_offset(const void* p, const void* base) {
  return static_cast<s32>(static_cast<const u8*>(p) - static_cast<const u8*>(base));
}

const u32* code_at_bytes(const ScriptCtx* ctx, s32 offset) {
  return reinterpret_cast<const u32*>(reinterpret_cast<const u8*>(ctx->code) + offset);
}

s32 script_header(int word) { return vm_rd32(g_map_script + 4 * word); }

// Untyped operands (opcodes 0x01-0x1d, 0x61-0x63): bit 23 selects the locals, bits 0-22
// the address. A local of 'size' bytes at address n lives at top - n - size.
u8* var_ptr(ScriptCtx* ctx, u32 local, u32 addr, int size) {
  return local ? ctx->locals - addr - size : globals().script_globals + addr;
}
// Flag n: bit n & 7 of byte n >> 3, counted down from the top for locals.
u8* flag_ptr(ScriptCtx* ctx, u32 local, u32 addr) {
  return local ? ctx->local_flags - (static_cast<s32>(addr) >> 3) - 1
               : globals().script_flags + (static_cast<s32>(addr) >> 3);
}
u32 flag_get(ScriptCtx* ctx, u32 local, u32 addr) { return *flag_ptr(ctx, local, addr) >> (addr & 7) & 1; }
void flag_set(ScriptCtx* ctx, u32 local, u32 addr, bool on) {
  u8* p = flag_ptr(ctx, local, addr);
  const u8 bit = static_cast<u8>(1 << (addr & 7));
  // Archipelago (port feature): a global flag turning on may be a location's (a chest's
  // opened flag).
  if (on && local == 0 && (*p & bit) == 0) ap::on_flag_set(static_cast<int>(addr));
  *p = on ? static_cast<u8>(*p | bit) : static_cast<u8>(*p & ~bit);
}

u32 pop(ScriptCtx* ctx) { return *--ctx->value_sp; }
void push(ScriptCtx* ctx, u32 v) { *ctx->value_sp++ = v; }
u32 next_word(ScriptCtx* ctx) { return *ctx->ip++; }

// Loads an element of the array addressed by an index op: 'element' is what the last
// index_scale (0x09 / 0x11) left, 'addr' is the computed address (bit 23 = locals).
u32 load_indexed(ScriptCtx* ctx, u32 element, u32 addr) {
  const u32 local = addr & 0x800000;
  const u32 a = addr & 0x7FFFFF;
  if (element == 2) return static_cast<u32>(static_cast<s32>(vm_rd16(var_ptr(ctx, local, a, 2))));
  if (element == 1) return *var_ptr(ctx, local, a, 1);
  if (element == 4) return static_cast<u32>(vm_rd32(var_ptr(ctx, local, a, 4)));
  return flag_get(ctx, local, a);
}

// Typed operand read of the binary ops (0x23-0x46): types 0/8 32-bit, 1 s16, 9 u16,
// 2/10 byte (read unsigned for both), 3 flag; any other type leaves 'value' as it was.
void load_typed(ScriptCtx* ctx, u32 instr, u32& value) {
  const u32 local = instr & 0x800000;
  const u32 a = instr & 0xFFFF;
  switch (instr >> 16 & 0xF) {
    case 0:
    case 8: value = static_cast<u32>(vm_rd32(var_ptr(ctx, local, a, 4))); break;
    case 1: value = static_cast<u32>(static_cast<s32>(vm_rd16(var_ptr(ctx, local, a, 2)))); break;
    case 9: value = static_cast<u16>(vm_rd16(var_ptr(ctx, local, a, 2))); break;
    case 2:
    case 10: value = *var_ptr(ctx, local, a, 1); break;
    case 3: value = flag_get(ctx, local, a); break;
    default: break;
  }
}

// Typed store of 0x21 / 0x22 (type taken from bits 16-22, so types with bits 20-22 set
// store nothing).
void store_typed(ScriptCtx* ctx, u32 instr, u32 v) {
  const u32 local = instr & 0x800000;
  const u32 a = instr & 0xFFFF;
  switch (instr >> 16 & 0x7F) {
    case 0:
    case 8: vm_wr32(var_ptr(ctx, local, a, 4), v); break;
    case 1:
    case 9: vm_wr16(var_ptr(ctx, local, a, 2), v); break;
    case 2:
    case 10: *var_ptr(ctx, local, a, 1) = static_cast<u8>(v); break;
    case 3: flag_set(ctx, local, a, v != 0); break;
    default: break;
  }
}

// The console traps on a zero divisor (break 7) and on INT_MIN / -1 (break 6); scripts
// never divide by zero, so these only keep the host defined.
s32 sdiv(s32 a, s32 b) {
  if (b == 0) return a < 0 ? 1 : -1;
  return b == -1 ? static_cast<s32>(0u - static_cast<u32>(a)) : a / b;
}
s32 smod(s32 a, s32 b) { return b == 0 ? a : (b == -1 ? 0 : a % b); }
u32 udiv(u32 a, u32 b) { return b == 0 ? 0xFFFFFFFFu : a / b; }
u32 umod(u32 a, u32 b) { return b == 0 ? a : a % b; }

// Binary ops 0x23-0x46 in pairs (odd: immediate operand, even: popped): R = value OP
// operand, signed unless the operand type has bit 3 set (u32 / u16 / u8). 'parity' is
// the handler's (1 for the immediate forms): an opcode of the other parity (possible
// only when an indexed address overflowed into the opcode bits) stores nothing.
bool binary_op(u32 op, u32 parity, u32 type, u32 value, u32 operand, u32* out) {
  if ((op & 1) != parity) return false;
  const bool is_unsigned = (type & 8) != 0;
  const s32 sv = static_cast<s32>(value), so = static_cast<s32>(operand);
  switch ((op & 1) != 0 ? op : op - 1) {
    case 0x23: *out = value + operand; return true;
    case 0x25: *out = value - operand; return true;
    case 0x27: *out = value * operand; return true;
    case 0x29: *out = is_unsigned ? udiv(value, operand) : static_cast<u32>(sdiv(sv, so)); return true;
    case 0x2B: *out = is_unsigned ? umod(value, operand) : static_cast<u32>(smod(sv, so)); return true;
    case 0x2D: *out = value & operand; return true;
    case 0x2F: *out = value | operand; return true;
    case 0x31: *out = value ^ operand; return true;
    case 0x33: *out = value << (operand & 0x1F); return true;
    case 0x35: *out = is_unsigned ? value >> (operand & 0x1F) : static_cast<u32>(sv >> (operand & 0x1F)); return true;
    case 0x3B: *out = value == operand; return true;
    case 0x3D: *out = value != operand; return true;
    case 0x3F: *out = is_unsigned ? operand < value : so < sv; return true;
    case 0x41: *out = is_unsigned ? value < operand : sv < so; return true;
    case 0x43: *out = (is_unsigned ? value < operand : sv < so) ^ 1; return true;
    case 0x45: *out = (is_unsigned ? operand < value : so < sv) ^ 1; return true;
    default: return false;
  }
}

// Assignment ops 0x4d-0x60 (odd: immediate, even: popped): var OP= operand.
void assign_op(ScriptCtx* ctx, u32 instr, u32 operand, u32& value, u32& result) {
  const u32 local = instr & 0x800000;
  const u32 a = instr & 0xFFFF;
  const u32 type = instr >> 16 & 0xF;
  // The pairs share a handler: 0x4d / 0x4e add, 0x4f / 0x50 sub ... (odd = immediate).
  const u32 op = (instr >> 24 & 1) != 0 ? instr >> 24 : (instr >> 24) - 1;
  switch (type) {
    case 0: case 1: case 2: case 8: case 9: case 10: {
      const u32 width = type & 7;  // 0 = 32-bit, 1 = 16-bit (read signed), 2 = byte (read unsigned)
      if (width == 1) value = static_cast<u32>(static_cast<s32>(vm_rd16(var_ptr(ctx, local, a, 2))));
      else if (width == 0) value = static_cast<u32>(vm_rd32(var_ptr(ctx, local, a, 4)));
      else value = *var_ptr(ctx, local, a, 1);
      const bool is_unsigned = (type & 8) != 0;
      const s32 sv = static_cast<s32>(value), so = static_cast<s32>(operand);
      switch (op) {
        case 0x4D: result = value + operand; break;
        case 0x4F: result = value - operand; break;
        case 0x51: result = value * operand; break;
        case 0x53: result = is_unsigned ? udiv(value, operand) : static_cast<u32>(sdiv(sv, so)); break;
        case 0x55: result = is_unsigned ? umod(value, operand) : static_cast<u32>(smod(sv, so)); break;
        case 0x57: result = value & operand; break;
        case 0x59: result = value | operand; break;
        case 0x5B: result = value ^ operand; break;
        case 0x5D: result = value << (operand & 0x1F); break;
        case 0x5F: result = is_unsigned ? value >> (operand & 0x1F) : static_cast<u32>(sv >> (operand & 0x1F)); break;
        default: break;
      }
      if (width == 1) vm_wr16(var_ptr(ctx, local, a, 2), result);
      else if (width == 0) vm_wr32(var_ptr(ctx, local, a, 4), result);
      else *var_ptr(ctx, local, a, 1) = static_cast<u8>(result);
      break;
    }
    case 3: {
      // Flags take and / or / xor only.
      value = flag_get(ctx, local, a);
      if (op == 0x57) flag_set(ctx, local, a, (value & operand) != 0);
      else if (op == 0x59) flag_set(ctx, local, a, !(value == 0 && (operand & 1) == 0));
      else if (op == 0x5B) flag_set(ctx, local, a, ((value ^ operand) & 1) != 0);
      break;
    }
    default: break;
  }
}

}  // namespace

ScriptCtx* vm_ctx_create(ScriptCtx* ctx) {
  for (int i = 0; i < kTriggerCount; ++i) {
    Trigger& t = ctx->triggers[i];
    for (s32& c : t.corner) c = 0;
    t.min_x = t.min_z = t.max_x = t.max_z = 0;
  }
  for (auto& c : ctx->choice_cursor) c.x = c.y = 0;
  return ctx;
}

void vm_init(ScriptCtx* ctx) {
  FieldGlobals& g = globals();
  const u8* blob = g_map_script;
  ctx->code = reinterpret_cast<const u32*>(blob + 0x1C + 4 * script_header(1));
  ctx->ip = reinterpret_cast<const u32*>(blob + 0x1C + 4 * script_header(2));
  ctx->globals_size = (script_header(5) + 3) & ~3;
  ctx->flags_size = script_header(6);
  ctx->locals = g.script_globals + ctx->globals_size;
  ctx->local_flags = g.script_flags + ctx->flags_size;
  ctx->strings = blob + script_header(0) + 0x1C;
  ctx->active_trigger = -1;
  ctx->last_trigger = -1;
  ctx->music = -1;
  ctx->first_fade = 1;
  ctx->menu_close_entry = -1;
  ctx->text = ctx->strings + script_header(3) * 2;
  ctx->wait = 0;
  ctx->dwell_counter = 0;
  ctx->call_sp = reinterpret_cast<u32*>(g.state_block);
  ctx->value_sp = reinterpret_cast<u32*>(g.value_stack);
  vm_svc_msg_set_default_rect(2, 1, 0x18, 4);
  for (int i = kTriggerCount - 1; i >= 0; --i) ctx->triggers[i].active = 0;
  // A fresh entry pushes a frame whose return address is the main function; word 0 of
  // every script is 'return', so the first instruction "returns" into it. Coming back
  // from another program, vm_restore_state supplies the saved stack instead.
  if (g.field_flags[0x24] == 0) vm_push_frame(ctx);
  ctx->wait_state = 0;
  ctx->sync = 0;
  ctx->talk_active = 0;
  ctx->talk_returned = 0;
  ctx->sync_return = 0;
  ctx->depth = 0;
  ctx->ip = ctx->code;
  for (s16& n : ctx->option_lines) n = 1;
  task_unregister(ctx);
  task_register(ctx, 0);
}

void vm_push_frame(ScriptCtx* ctx) {
  FieldGlobals& g = globals();
  *ctx->call_sp++ = static_cast<u32>(byte_offset(ctx->locals, g.script_globals + ctx->globals_size));
  *ctx->call_sp++ = static_cast<u32>(byte_offset(ctx->local_flags, g.script_flags + ctx->flags_size));
  *ctx->call_sp++ = static_cast<u32>(ctx->wait);
  *ctx->call_sp++ = static_cast<u32>(byte_offset(ctx->ip, ctx->code));
}

void vm_fetch_args(ScriptCtx* ctx, int count) {
  if (count == 0) return;
  if ((ctx->instr & 0x800000) == 0) {
    // Inline: the first in the instruction's low half, then two s16 per following word;
    // an even count takes one more word for the last.
    g_vm_args[0] = static_cast<s16>(ctx->instr);
    const int pairs = (count - 1) / 2;
    int i = 0;
    for (; i < pairs; ++i) {
      const u32 w = *ctx->ip++;
      g_vm_args[1 + 2 * i] = static_cast<s16>(w);
      g_vm_args[2 + 2 * i] = static_cast<s16>(w >> 16);
    }
    if ((count & 1) == 0) g_vm_args[1 + 2 * i] = static_cast<s16>(*ctx->ip++);
  } else {
    for (int i = count - 1; i >= 0; --i) g_vm_args[i] = static_cast<s32>(*--ctx->value_sp);
  }
}

void vm_call_sync(ScriptCtx* ctx, u16 entry) {
  if (entry == 0xFFFF) return;
  vm_push_frame(ctx);
  const s32 depth = ctx->depth;
  const s32 wait_state = ctx->wait_state;
  const u8 talk = ctx->talk_active;
  ctx->wait = 0;
  ctx->sync = 1;
  const s32 r = vm_r();
  ctx->sync_return = 1;
  ctx->depth = 1;
  ctx->wait_state = 0;
  ctx->talk_active = 0;
  ctx->ip = ctx->code + entry;
  ctx->update();  // through the vtable, as the original
  ctx->talk_active = talk;
  ctx->depth = depth;
  ctx->wait_state = wait_state;
  ctx->sync = 0;
  vm_set_r(static_cast<u32>(r));
}

int vm_preload_entry(ScriptCtx* ctx, int entry, int own, int extra_bytes) {
  if (own != 0) vm_svc_preload_release_kind2_except(ctx->active_trigger);
  if (vm_svc_get_preload_release_pending(0) != 0) {
    vm_svc_preload_release(1);
    vm_svc_set_preload_release_pending(0, 0);
  }
  vm_svc_set_preload_release_pending(1, 0);
  return vm_svc_preload_start(entry, 2, ctx->active_trigger, extra_bytes);
}

void vm_thread_update(ScriptCtx* ctx) {
  FieldGlobals& g = globals();
  // Registers that survive from one instruction to the next within this call ($s2, $s5,
  // $fp, $s6 on the console): the last value loaded, the address an index op computed,
  // the element size index_scale left, and the last assignment result.
  u32 value = 0;
  u32 indexed = 0;
  u32 element = 0;
  u32 result = 0;
  u32 steps = 0;  // instrumentation (g_vm_step_limit)

  ctx->restart = 0;
  if (g.field_flags[0x32] != 0) --g.field_flags[0x32];

restart:
  if (g.disc_error != 0) return;
  if (ctx->sync == 0 && field_state_s16(0x10) != -1) {
    // Talk (Cross) or pickpocket (the Private Action button) next to an NPC.
    const u16 talk = g.pressed[0] & config_button(0);
    if ((talk != 0 || (g.pressed[0] & config_button(3)) != 0) && g.field_flags[0x26] == 0 &&
        ctx->active_trigger == -1 && ctx->talk_active == 0 && vm_svc_field_update_state() == 0 &&
        field_try_talk(ctx, talk) != 0) {
      ctx->talk_active = 1;
      ctx->saved_control = field_state_s16(0x10);
      field_state_set_s16(0x10, -1);
    }
    if (field_state_s16(0x10) != -1 && g.field_flags[0x26] == 0 && ctx->active_trigger == -1) {
      trigger_check_regions(ctx);
      g.field_flags[0x33] = ctx->active_trigger != -1 ? g.field_flags[0x32] != 0 : 0;
    }
  }

  for (;;) {
    // A pending wait ends the frame (a new event starts only once it has run out).
    if (ctx->sync == 0 && ctx->wait > 0) {
      ctx->wait -= g.frame_ticks;
      if (ctx->wait < 0) ctx->wait = 0;
      return;
    }

    // A music change: start the new song once its bank is installed and the old one is quiet.
    if (ctx->music != -1) {
      if (ctx->music_installed == 0) {
        bool installed = false;
        if (ctx->music_loaded != 0 && vm_svc_sound_bank_busy() == 0) {
          installed = vm_svc_field_install_music(ctx->music) != 0;
        }
        if (installed) ctx->music_installed = 1;
      } else if (vm_svc_sound_bank_busy() != 1) {
        if (vm_svc_sound_state_busy() == 0) {
          if (ctx->music_crossfade == 0) {
            vm_svc_sound_play_sequence();
          } else {
            vm_svc_sound_play_sequence_fade(static_cast<s16>(ctx->music_fade_a), ctx->music_fade_b);
          }
        }
        vm_svc_music_free_buffers();
        ctx->music = -1;
        ctx->music_installed = 0;
      }
    }

    // Wait states.
    bool clear = false;
    switch (ctx->wait_state) {
      case 1: {  // map transfer
        const int hero = hero_slot();
        if (ctx->transition == 0 && vm_svc_actor_exists(hero)) vm_svc_actor_or_flags(hero, 0, 0x80);
        if (vm_svc_fade_is_active(-1) != 0 && static_cast<u32>(ctx->transition - 1) >= 2) return;
        if (ctx->transition == 1) vm_svc_fade_capture_screen();
        vm_svc_field_load_map(ctx->wait_param, 1, ctx->transfer_flag != 0 ? 2 : 0);
        if ((g.script_flags[1] >> 4 & 1) == 0 || g.field_flags[0x68] == 0) {
          const u8* gs = g.game_state;
          vm_svc_actor_create(hero, hero, vm_rd32(gs + 8), vm_rd32(gs + 0xC), vm_rd32(gs + 0x10), ctx->arrival_dir,
                              ctx->arrival_param, 0xFFFF, 0xFFFF, 0x1000F);
          vm_svc_actor_or_flags(hero, 0, 0x200004);
        }
        goto restart;
      }
      case 2:  // message window closed (window -1: all)
        if (ctx->sync != 0) {
          clear = true;
          break;
        }
        {
          const int status = vm_svc_msg_window_status(ctx->wait_param);
          if (status != 1 && status != -1) return;
        }
        if (ctx->talk_active != 0 && ctx->talk_returned != 0) {
          // End of a talk event: the NPC and the leader turn back, the player gets control.
          if (ctx->talk_actor != -1) {
            if (vm_svc_actor_exists(ctx->talk_actor)) vm_svc_actor_pop_state(ctx->talk_actor, 1);
            if (vm_svc_actor_exists(ctx->saved_control)) vm_svc_actor_pop_state(ctx->saved_control, 0);
          }
          ctx->talk_active = 0;
          field_state_set_s16(0x10, ctx->saved_control);
        }
        ctx->wait_state = 0;
        ctx->talk_returned = 0;
        break;
      case 3:  // fade finished
        if (ctx->sync != 0) {
          clear = true;
          break;
        }
        if (vm_svc_fade_is_active(ctx->wait_param) != 0) return;
        clear = true;
        break;
      case 4:  // camera script finished
        if (ctx->sync != 0) {
          clear = true;
          break;
        }
        if (!vm_svc_camscript_done()) return;
        ctx->wait_state = 0;
        break;
      case 5:  // actor idle
        if (ctx->sync != 0) {
          clear = true;
          break;
        }
        if (vm_svc_actor_is_busy(ctx->wait_param) == 1) return;
        ctx->wait_state = 0;
        break;
      case 6:  // sprite pack / map object entry loaded
        if (ctx->sync != 0) {
          clear = true;
          break;
        }
        if (vm_svc_field_require_sprite_pack(ctx->wait_param) == 0) return;
        ctx->wait_state = 0;
        break;
      case 8:  // music change done (in sync mode execution goes on without clearing it)
        if (ctx->sync == 0) {
          if (ctx->music != -1) return;
          ctx->wait_state = 0;
        }
        break;
      case 9: {  // choice
        const int window = ctx->wait_param;
        const int state = vm_svc_msg_window_get_state(window);
        if (state != -1 && state != 1 && static_cast<u32>(state - 6) >= 2) return;
        choice_draw_cursor(ctx);
        if ((g.pressed[0] & config_button(0)) != 0) {
          vm_svc_sound_play_sfx(2, 0, 0x7F40);
          vm_set_r(static_cast<u32>(static_cast<s32>(ctx->choice_selection)));
          ctx->wait_state = 0;
          for (int i = 0; i < 8; ++i) ctx->option_lines[i] = 1;
          if (ctx->choice_close == 0) return;
          vm_svc_msg_window_set_close_flag(window, 1);
          return;
        }
        if ((g.pressed[0] & config_button(1)) != 0) {
          if (ctx->choice_cancel == 0) {
            vm_svc_sound_play_sfx(7, 0, 0x7F40);
            return;
          }
          vm_svc_sound_play_sfx(6, 0, 0x7F40);
          vm_set_r(0xFFFFFFFFu);
          for (int i = 0; i < 8; ++i) ctx->option_lines[i] = 1;
          ctx->wait_state = 0;
          vm_svc_msg_window_request_close(window);
          if (ctx->choice_close == 0) return;
          vm_svc_msg_window_set_close_flag(window, 1);
          return;
        }
        if (ctx->choice_slide != 0) return;
        if ((g.repeat[0] & 0x1000) != 0) {  // up
          if (ctx->choice_selection < 1) {
            if ((g.pressed[0] & 0x1000) == 0) return;
            vm_svc_sound_play_sfx(1, 0, 0x7F40);
            ctx->choice_selection = static_cast<s16>(ctx->choice_count - 1);
          } else {
            vm_svc_sound_play_sfx(1, 0, 0x7F40);
            ctx->choice_selection = static_cast<s16>(ctx->choice_selection - 1);
          }
          return;
        }
        if ((g.repeat[0] & 0x4000) == 0) return;  // down
        if (ctx->choice_selection < ctx->choice_count - 1) {
          vm_svc_sound_play_sfx(1, 0, 0x7F40);
          ctx->choice_selection = static_cast<s16>(ctx->choice_selection + 1);
          return;
        }
        if ((g.pressed[0] & 0x4000) == 0) return;
        vm_svc_sound_play_sfx(1, 0, 0x7F40);
        ctx->choice_selection = 0;
        return;
      }
      case 0xB:  // battle: voices cached and the battle preloads done
        if (vm_svc_get_voice_cache_state() < 3) return;
        if (vm_svc_preload_count_done(1) != vm_svc_get_battle_preload_count()) return;
        {
          const u32 r = rng_range(2);
          vm_svc_field_start_battle(static_cast<int>(r));
        }
        task_register(g_script_vm, 1);
        ctx->wait_state = 0xC;
        continue;
      case 0xC:
        if (!vm_svc_battle_transition_done()) return;
        vm_svc_field_destroy_battle_transition();
        ctx->wait_state = 0xD;
        g.draw_enabled = 0;  // 0x8007572c
        continue;
      case 0xD:
        if (vm_svc_get_battle_ready() == 0) return;
        vm_svc_field_exit_to_program(1);
        break;
      case 0xE:  // field menu requested by the script closed
        if (vm_svc_menu_request_pending() != 0) return;
        clear = true;
        break;
      default:  // 0, and 7 / 0xa which have no handler
        break;
    }
    if (clear) ctx->wait_state = 0;

    // Fetch and execute one instruction.
    if (g_vm_step_limit != 0 && ++steps > g_vm_step_limit) {
      ++g_vm_stats.step_limit_hits;
      return;
    }
    // Debugging (SS_VM_TRACE=1): every instruction with its word offset, call depth and stacks.
    static const bool trace_ops = std::getenv("SS_VM_TRACE") != nullptr;
    if (trace_ops) {
      std::fprintf(stderr, "[vm] w%ld %08x depth %d call_sp %ld value_sp %ld\n", static_cast<long>(ctx->ip - ctx->code),
                   *ctx->ip, ctx->depth, static_cast<long>(ctx->call_sp - reinterpret_cast<u32*>(g.state_block)),
                   static_cast<long>(ctx->value_sp - reinterpret_cast<u32*>(g.value_stack)));
    }
    const u32 fetched = *ctx->ip++;
    u32 instr = fetched;
    const u32 local = instr & 0x800000;
    const u32 addr = instr & 0x7FFFFF;
    switch (instr >> 24) {
      case 0x01: case 0x02: case 0x08:  // pop_store s32
        value = pop(ctx);
        vm_wr32(var_ptr(ctx, local, addr, 4), value);
        break;
      case 0x03: case 0x04:  // pop_store s16
        value = pop(ctx);
        vm_wr16(var_ptr(ctx, local, addr, 2), value);
        break;
      case 0x05: case 0x06:  // pop_store s8
        value = pop(ctx);
        *var_ptr(ctx, local, addr, 1) = static_cast<u8>(value);
        break;
      case 0x07:  // pop_store flag
        flag_set(ctx, local, addr, pop(ctx) != 0);
        break;
      case 0x09:  // index_scale: R = pop * imm; remembers the element size
        element = instr & 0xFFFFFF;
        vm_set_r(pop(ctx) * (element & 0x7FFFFF));
        break;
      case 0x0A:  // index_load: R = element at R + imm
        indexed = static_cast<u32>(vm_r()) + (instr & 0xFFFFFF);
        value = load_indexed(ctx, element, indexed);
        vm_set_r(value);
        break;
      case 0x0B: case 0x0F:  // push s32
        value = static_cast<u32>(vm_rd32(var_ptr(ctx, local, addr, 4)));
        push(ctx, value);
        break;
      case 0x0C:  // push s16
        value = static_cast<u32>(static_cast<s32>(vm_rd16(var_ptr(ctx, local, addr, 2))));
        push(ctx, value);
        break;
      case 0x0D:  // push u8
        value = *var_ptr(ctx, local, addr, 1);
        push(ctx, value);
        break;
      case 0x0E:  // push flag; a zero operand pushes R
        if ((instr & 0x80FFFF) == 0) {
          push(ctx, static_cast<u32>(vm_r()));
        } else {
          push(ctx, flag_get(ctx, local, addr));
        }
        break;
      case 0x10: {  // index_load_var: R = element at var32 + next word
        const u32 offset = next_word(ctx);
        indexed = static_cast<u32>(vm_rd32(var_ptr(ctx, local, addr, 4))) + offset;
        value = load_indexed(ctx, element, indexed);
        vm_set_r(value);
        break;
      }
      case 0x11:  // ret_mul: R *= imm
        element = instr & 0xFFFFFF;
        vm_set_r(static_cast<u32>(vm_r()) * (element & 0x7FFFFF));
        break;
      case 0x12:  // push imm24
        value = instr & 0xFFFFFF;
        push(ctx, value);
        break;
      case 0x13:  // push next word
        push(ctx, next_word(ctx));
        break;
      case 0x14:  // call
        vm_push_frame(ctx);
        ++ctx->depth;
        ctx->wait = 0;
        ctx->ip = ctx->code + (instr & 0xFFFFFF);
        break;
      case 0x15:  // jmp
        ctx->ip = ctx->code + (instr & 0xFFFFFF);
        break;
      case 0x16:  // jz
        if (pop(ctx) == 0) ctx->ip = ctx->code + (instr & 0xFFFFFF);
        break;
      case 0x17: {  // return
        ctx->ip = code_at_bytes(ctx, static_cast<s32>(*--ctx->call_sp));
        ctx->wait = static_cast<s32>(*--ctx->call_sp);
        // Rebased on the header's sizes (hdr[6] / hdr[5] as read, not rounded).
        ctx->local_flags = g.script_flags + script_header(6) + static_cast<s32>(*--ctx->call_sp);
        value = *--ctx->call_sp;
        --ctx->depth;
        ctx->locals = g.script_globals + script_header(5) + static_cast<s32>(value);
        if (ctx->depth < 1) {
          if (ctx->talk_active != 0) {
            ctx->wait_state = 2;
            ctx->wait_param = -1;
            ctx->talk_returned = 1;
          }
          if (ctx->sync == 0) {
            if (ctx->active_trigger != -1) {
              ctx->active_trigger = -1;
              field_state_set_s16(0x10, ctx->saved_control);
            }
          } else {
            ctx->sync = 0;
            if (ctx->sync_return != 0) {
              ctx->sync_return = 0;
              return;
            }
          }
        }
        break;
      }
      case 0x18:  // frame: reserve local flag bytes (imm) and variable bytes (next word)
        ctx->local_flags += instr & 0xFFFFFF;
        ctx->locals += static_cast<s32>(next_word(ctx));
        break;
      case 0x19: {  // push_eq s32
        const u32 imm = next_word(ctx);
        push(ctx, static_cast<u32>(vm_rd32(var_ptr(ctx, local, addr, 4))) == imm);
        break;
      }
      case 0x1A:  // store s32 = next word
        vm_wr32(var_ptr(ctx, local, addr, 4), next_word(ctx));
        break;
      case 0x1B: {  // push_eq s16
        const u32 imm = next_word(ctx);
        push(ctx, static_cast<u32>(static_cast<s32>(vm_rd16(var_ptr(ctx, local, addr, 2)))) == imm);
        break;
      }
      case 0x1C: {  // push_eq u8
        const u32 imm = next_word(ctx);
        push(ctx, *var_ptr(ctx, local, addr, 1) == imm);
        break;
      }
      case 0x1D: {  // push_eq flag
        const u32 imm = next_word(ctx);
        push(ctx, flag_get(ctx, local, addr) == imm);
        break;
      }
      case 0x21:  // store typed = next word; R = value
        if ((instr & 0x80FFFF) == 0) instr = (instr & 0xFFFF0000) | indexed;
        store_typed(ctx, instr, *ctx->ip);
        vm_set_r(next_word(ctx));
        break;
      case 0x22:  // pop_store typed; R = value
        if ((instr & 0x80FFFF) == 0) instr = (instr & 0xFFFF0000) | indexed;
        value = pop(ctx);
        store_typed(ctx, instr, value);
        vm_set_r(value);
        break;
      case 0x23: case 0x25: case 0x27: case 0x29: case 0x2B: case 0x2D: case 0x2F: case 0x31:
      case 0x33: case 0x35: case 0x3B: case 0x3D: case 0x3F: case 0x41: case 0x43: case 0x45: {
        // R = var OP next word
        if ((instr & 0x80FFFF) == 0) instr = (instr & 0xFFFF0000) | indexed;
        load_typed(ctx, instr, value);
        const u32 operand = next_word(ctx);
        u32 r;
        if (binary_op(instr >> 24, 1, instr >> 16 & 0xF, value, operand, &r)) vm_set_r(r);
        break;
      }
      case 0x24: case 0x26: case 0x28: case 0x2A: case 0x2C: case 0x2E: case 0x30: case 0x32:
      case 0x34: case 0x36: case 0x3C: case 0x3E: case 0x40: case 0x42: case 0x44: case 0x46: {
        // R = var OP pop
        if ((instr & 0x80FFFF) == 0) instr = (instr & 0xFFFF0000) | indexed;
        load_typed(ctx, instr, value);
        const u32 operand = pop(ctx);
        u32 r;
        if (binary_op(instr >> 24, 0, instr >> 16 & 0xF, value, operand, &r)) vm_set_r(r);
        break;
      }
      case 0x38: {  // logical_and pop, pop
        const u32 a = pop(ctx);
        const u32 b = pop(ctx);
        vm_set_r(a != 0 && b != 0);
        break;
      }
      case 0x3A: {  // logical_or pop, pop
        const u32 a = pop(ctx);
        const u32 b = pop(ctx);
        vm_set_r(a != 0 || b != 0);
        break;
      }
      case 0x47:    // inc
      case 0x48: {  // dec
        const s32 d = (instr >> 24) == 0x47 ? 1 : -1;
        const u32 a = instr & 0xFFFF;
        switch (instr >> 16 & 0xF) {
          case 0: case 8: {
            u8* p = var_ptr(ctx, local, a, 4);
            vm_wr32(p, static_cast<u32>(vm_rd32(p) + d));
            break;
          }
          case 1: case 9: {
            u8* p = var_ptr(ctx, local, a, 2);
            vm_wr16(p, static_cast<u32>(vm_rd16(p) + d));
            break;
          }
          case 2: case 10: {
            u8* p = var_ptr(ctx, local, a, 1);
            *p = static_cast<u8>(*p + d);
            break;
          }
          default: break;
        }
        break;
      }
      case 0x49: {  // not: R = ~var (flags: logical not)
        const u32 a = instr & 0xFFFF;
        switch (instr >> 16 & 0xF) {
          case 0: case 8: vm_set_r(~static_cast<u32>(vm_rd32(var_ptr(ctx, local, a, 4)))); break;
          case 1: case 9: vm_set_r(~static_cast<u32>(static_cast<s32>(vm_rd16(var_ptr(ctx, local, a, 2))))); break;
          case 2: case 10: vm_set_r(~static_cast<u32>(*var_ptr(ctx, local, a, 1))); break;
          case 3: vm_set_r(flag_get(ctx, local, a) == 0 ? 1 : 0); break;
          default: break;
        }
        break;
      }
      case 0x4A:  // not pop
        value = ~pop(ctx);
        vm_set_r(value);
        break;
      case 0x4B: {  // is_zero
        const u32 a = instr & 0xFFFF;
        switch (instr >> 16 & 0xF) {
          case 0: case 8: vm_set_r(vm_rd32(var_ptr(ctx, local, a, 4)) == 0); break;
          case 1: case 9: vm_set_r(vm_rd16(var_ptr(ctx, local, a, 2)) == 0); break;
          case 2: case 10: vm_set_r(*var_ptr(ctx, local, a, 1) == 0); break;
          case 3: vm_set_r(flag_get(ctx, local, a) == 0 ? 1 : 0); break;
          default: break;
        }
        break;
      }
      case 0x4C:  // is_zero pop
        value = pop(ctx) == 0;
        vm_set_r(value);
        break;
      case 0x4D: case 0x4E: case 0x4F: case 0x50: case 0x51: case 0x52: case 0x53: case 0x54: case 0x55: case 0x56:
      case 0x57: case 0x58: case 0x59: case 0x5A: case 0x5B: case 0x5C: case 0x5D: case 0x5E: case 0x5F: case 0x60: {
        if ((instr & 0x80FFFF) == 0) instr = (instr & 0xFFFF0000) | indexed;
        const u32 op = instr >> 24;
        const u32 operand = (op >= 0x4D && op <= 0x5F && (op & 1) != 0) ? next_word(ctx) : pop(ctx);
        assign_op(ctx, instr, operand, value, result);
        break;
      }
      case 0x61: case 0x62: case 0x63: {
        // block_store: N words from the code stream into the variables. The console
        // writes every word to the same address (the destination never advances) and
        // adds the address to the locals top instead of counting down from it.
        const u32 count = next_word(ctx);
        u8* base = local ? ctx->locals : g.script_globals;
        u8* dst = base + addr;
        for (u32 n = count; n != 0; --n) vm_wr32(dst, next_word(ctx));
        value = 0xFFFFFFFFu;
        break;
      }
      default:  // 0x00, 0x1e-0x20, 0x37, 0x39 and 0x64-0xff
        vm_exec_ext(ctx, fetched);
        if (ctx->restart != 0) {
          ctx->restart = 0;
          goto restart;
        }
        break;
    }
  }
}

void vm_save_state(ScriptCtx* ctx) {
  FieldGlobals& g = globals();
  u8* fs = g.field_flags;
  vm_wr16(fs + 4, static_cast<u32>(byte_offset(ctx->ip, ctx->code)));
  vm_wr16(fs + 6, static_cast<u32>(byte_offset(ctx->locals, g.script_globals + ctx->globals_size)));
  vm_wr16(fs + 8, static_cast<u32>(byte_offset(ctx->local_flags, g.script_flags + ctx->flags_size)));
  vm_wr16(fs + 0x36, static_cast<u32>(byte_offset(ctx->call_sp, g.state_block)));
  vm_wr16(fs + 0x34, static_cast<u32>(byte_offset(ctx->value_sp, g.value_stack)));
  vm_wr16(fs + 0xC, static_cast<u32>(ctx->active_trigger));
  int count = 0;
  for (int i = 0; i < kTriggerCount; ++i) count += ctx->triggers[i].active != 0;
  // Handoff 0xd: u32 size, u16 count, then per active trigger its 0x48 bytes (code as a
  // word index) and its slot, then six words of the event state.
  handoff_clear(g.handoff, 0xD);
  const int size = count * 0x4C + 0x20;
  auto* block = new u8[static_cast<size_t>(size)]();
  handoff_store(g.handoff, block, 0xD);
  vm_wr32(block, static_cast<u32>(size));
  vm_wr16(block + 4, static_cast<u32>(count));
  u8* p = block + 8;
  for (int i = 0; i < kTriggerCount; ++i) {
    const Trigger& t = ctx->triggers[i];
    if (t.active == 0) continue;
    vm_wr32(p + 0x00, static_cast<u32>(byte_offset(t.code, ctx->code) >> 2));
    p[0x04] = t.active;
    p[0x05] = t.dwell;
    p[0x06] = t.menu_flags;
    p[0x07] = t.world_map;
    vm_wr16(p + 0x08, static_cast<u32>(t.dwell_frames));
    vm_wr16(p + 0x0A, static_cast<u32>(t.private_action));
    vm_wr32(p + 0x0C, static_cast<u32>(t.unknown0c));
    vm_wr32(p + 0x10, t.buttons);
    for (int k = 0; k < 8; ++k) vm_wr32(p + 0x14 + 4 * k, static_cast<u32>(t.corner[k]));
    vm_wr16(p + 0x34, static_cast<u32>(t.y_min));
    vm_wr16(p + 0x36, static_cast<u32>(t.y_max));
    vm_wr32(p + 0x38, static_cast<u32>(t.min_x));
    vm_wr32(p + 0x3C, static_cast<u32>(t.min_z));
    vm_wr32(p + 0x40, static_cast<u32>(t.max_x));
    vm_wr32(p + 0x44, static_cast<u32>(t.max_z));
    vm_wr32(p + 0x48, static_cast<u32>(i));
    p += 0x4C;
  }
  vm_wr32(p + 0x00, static_cast<u32>(ctx->saved_control));
  vm_wr32(p + 0x04, static_cast<u32>(ctx->depth));
  vm_wr32(p + 0x08, ctx->first_fade);
  vm_wr32(p + 0x0C, static_cast<u32>(ctx->last_trigger));
  vm_wr32(p + 0x10, static_cast<u32>(ctx->active_trigger));
  vm_wr32(p + 0x14, static_cast<u16>(ctx->menu_close_entry));
  // The map's extra sprite packs (sprite slots 2-15 with an archive id), at most four,
  // to reload on the way back.
  int packs = 0;
  for (int slot = 2; slot < 16; ++slot) {
    const int entry = vm_svc_sprite_slot_entry(slot);
    if (entry > 0) {
      vm_wr16(fs + 0x28 + 2 * packs, static_cast<u32>(entry));
      if (++packs == 4) break;
    }
  }
  fs[0x6A] = static_cast<u8>(packs);
}

void vm_restore_state(ScriptCtx* ctx) {
  FieldGlobals& g = globals();
  const u8* fs = g.field_flags;
  ctx->ip = code_at_bytes(ctx, read_u16(fs + 4));
  ctx->locals += read_u16(fs + 6);
  ctx->local_flags += read_u16(fs + 8);
  ctx->call_sp = reinterpret_cast<u32*>(g.state_block + read_u16(fs + 0x36));
  ctx->value_sp = reinterpret_cast<u32*>(g.value_stack + read_u16(fs + 0x34));
  ctx->active_trigger = vm_rd16(fs + 0xC);
  auto* block = static_cast<u8*>(handoff_get(g.handoff, 0xD));
  if (block != nullptr) {
    const int count = read_u16(block + 4);
    const u8* p = block + 8;
    for (int i = 0; i < count; ++i, p += 0x4C) {
      const u32 slot = read_u32(p + 0x48);
      if (slot > static_cast<u32>(kTriggerCount)) continue;
      Trigger& t = ctx->triggers[slot];
      t.code = ctx->code + read_u32(p);
      t.active = p[0x04];
      t.dwell = p[0x05];
      t.menu_flags = p[0x06];
      t.world_map = p[0x07];
      t.dwell_frames = vm_rd16(p + 0x08);
      t.private_action = vm_rd16(p + 0x0A);
      t.unknown0c = vm_rd32(p + 0x0C);
      t.buttons = read_u32(p + 0x10);
      for (int k = 0; k < 8; ++k) t.corner[k] = vm_rd32(p + 0x14 + 4 * k);
      t.y_min = vm_rd16(p + 0x34);
      t.y_max = vm_rd16(p + 0x36);
      t.min_x = vm_rd32(p + 0x38);
      t.min_z = vm_rd32(p + 0x3C);
      t.max_x = vm_rd32(p + 0x40);
      t.max_z = vm_rd32(p + 0x44);
    }
    ctx->saved_control = vm_rd32(p + 0x00);
    ctx->depth = vm_rd32(p + 0x04);
    ctx->first_fade = p[0x08];
    ctx->last_trigger = vm_rd32(p + 0x0C);
    ctx->active_trigger = vm_rd32(p + 0x10);
    ctx->menu_close_entry = vm_rd16(p + 0x14);
  }
  for (int i = 0; i < fs[0x6A]; ++i) vm_svc_reload_sprite_pack(vm_rd16(fs + 0x28 + 2 * i));
  if (block != nullptr) {
    handoff_clear(g.handoff, 0xD);
    delete[] block;
  }
}

}  // namespace ss::game::field
