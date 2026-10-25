// vm_exec_table_a (field 0x80067634): opcode 0xfe, sub-command in bits 16-22 through
// the jump table 0x8007380c (0x01-0x48; 0x00, 0x2f, 0x30 and above 0x48 do nothing).
// Counters, party and character records, equipment, the world-map entries, overlay
// effects and the choice line counts. Subs 0x70-0x72 are the port's (Archipelago: Port
// Hilton's harbour in an open-world seed).
#include <cstdio>

#include "game/ap/ap.h"
#include "game/field/script_vm.h"
#include "game/field/vm_services.h"

namespace ss::game::field {
namespace {

s32* const a = g_vm_args;

// Record of the character's slot (active or reserve), or null.
u8* record_of(int character) {
  FieldGlobals& g = globals();
  for (int i = 0; i < 8; ++i) {
    int id = vm_rd16(g.party + i * 0x60);
    if (id < 0) id = -id;
    if (id == character + 1) return g.records + i * 0xD0;
  }
  return nullptr;
}

}  // namespace

void vm_exec_table_a(ScriptCtx* ctx, u32 instr) {
  FieldGlobals& g = globals();
  u8* gs = g.game_state;
  u8* party = g.party;
  const u32 sub = instr >> 16 & 0x7F;
  switch (sub) {
    case 0x01:  // add_treasure_count
      vm_fetch_args(ctx, 1);
      vm_wr16(gs + 0x248, static_cast<u32>(vm_rd16(gs + 0x248) + a[0]));
      break;
    case 0x02: vm_set_r(static_cast<u32>(static_cast<s32>(vm_rd16(gs + 0x248)))); break;  // get_treasure_count
    case 0x03: vm_set_r(static_cast<u32>(static_cast<s32>(vm_rd16(gs + 0x24A)))); break;  // pickpocket tries
    case 0x04: vm_set_r(static_cast<u32>(static_cast<s32>(vm_rd16(gs + 0x24C)))); break;  // pickpocket wins
    case 0x05: vm_set_r(read_u32(g.config + 0x20)); break;
    case 0x06: vm_set_r(read_u32(g.config + 0x28)); break;
    case 0x07: vm_set_r(read_u32(g.config + 0x24)); break;
    case 0x08: vm_set_r(read_u32(gs + 0x244)); break;
    case 0x09: vm_set_r(read_u32(g.config + 0x1C)); break;
    case 0x0A: {  // record +0x1a += a1, clamped 0..999
      vm_fetch_args(ctx, 2);
      u8* rec = record_of(a[0]);
      if (rec == nullptr) break;
      const s32 v = static_cast<s16>(static_cast<u32>(read_u16(rec + 0x1A)) + static_cast<u32>(a[1]));
      vm_wr16(rec + 0x1A, static_cast<u32>(v));
      if (v < 0) {
        vm_wr16(rec + 0x1A, 0);
      } else if (v >= 1000) {
        vm_wr16(rec + 0x1A, 999);
      }
      break;
    }
    case 0x0B: vm_set_r(read_u32(g.config + 0x14)); break;
    case 0x0C: vm_svc_sound_stop_voices(0xFFFFFF); break;
    case 0x0D: vm_set_r(read_u32(g.config + 0x2C)); break;
    case 0x0E:
      vm_fetch_args(ctx, 2);
      vm_svc_actor_set_8c(a[0], static_cast<s16>(a[1]));
      break;
    case 0x0F:
      vm_fetch_args(ctx, 2);
      vm_svc_actor_set_a0(a[0], static_cast<s16>(a[1]));
      break;
    case 0x10: vm_svc_inventory_stash(); break;
    case 0x11: vm_svc_inventory_unstash(); break;
    case 0x12: {
      vm_fetch_args(ctx, 5);
      const s32 pos[3] = {a[1] << 12, a[2] << 12, a[3] << 12};
      vm_svc_fx_spawn_burst(a[0] & 0xFF, pos, a[4]);
      break;
    }
    case 0x13: {
      vm_fetch_args(ctx, 12);
      const s32 pos[3] = {a[1] << 12, a[2] << 12, a[3] << 12};
      vm_svc_fx_spawn_ring_textured(a[0] & 0xFF, pos, a + 4);
      break;
    }
    case 0x14:  // get_equipment
      vm_fetch_args(ctx, 2);
      vm_set_r(static_cast<u32>(char_get_equipment(ctx, static_cast<s16>(a[0]), static_cast<s16>(a[1]))));
      break;
    case 0x15:  // party_compact: active members move up over empty / reserve slots
      for (int i = 0; i < 8; ++i) {
        int j = i + 1;
        if (vm_rd16(party + i * 0x60) < 1) {
          for (; j < 8; ++j) {
            if (vm_rd16(party + j * 0x60) > 0) {
              party_swap_slots(ctx, i, j);
              break;
            }
          }
          if (j == 8) return;
        }
      }
      break;
    case 0x16:  // party_save_order
      for (int i = 0; i < 8; ++i) {
        int id = vm_rd16(party + i * 0x60);
        if (id < 0) id = -id;
        gs[0x280 + i] = static_cast<u8>(id);
      }
      break;
    case 0x17:  // party_reload_changed
      for (int i = 0; i < 8; ++i) {
        int id = vm_rd16(party + i * 0x60);
        if (id < 0) id = -id;
        const int saved = gs[0x280 + i];
        if (id != saved && saved != 0) vm_svc_party_move_character_to_slot(saved - 1, i);
      }
      break;
    case 0x18:
      vm_fetch_args(ctx, 1);
      g.config[0x4E] = static_cast<u8>(a[0]);
      break;
    case 0x19:  // set_point (vehicle positions)
      vm_fetch_args(ctx, 5);
      if (a[0] == 0) {
        vm_wr32(gs + 0x26C, static_cast<u32>(a[1]));
        vm_wr32(gs + 0x28C, static_cast<u32>(a[2]));
        vm_wr32(gs + 0x270, static_cast<u32>(a[3]));
        vm_wr16(gs + 0x27C, static_cast<u32>(a[4]));
      } else {
        vm_wr32(gs + 0x274, static_cast<u32>(a[1]));
        vm_wr32(gs + 0x290, static_cast<u32>(a[2]));
        vm_wr32(gs + 0x278, static_cast<u32>(a[3]));
        vm_wr16(gs + 0x27E, static_cast<u32>(a[4]));
      }
      break;
    case 0x1A:  // actor_set_speed
      vm_fetch_args(ctx, 3);
      vm_svc_actor_set_speed(a[0], a[1], a[2]);
      vm_svc_actor_mark_dirty(a[0]);
      break;
    case 0x1B:
      vm_fetch_args(ctx, 6);
      if (vm_svc_actor_exists(a[0])) vm_svc_afterimage_attach(a[0], a[1], a[2], a[3], a[4] & 0xFF, a[5] & 0xFF);
      break;
    case 0x1C:
      vm_fetch_args(ctx, 1);
      vm_svc_afterimage_detach(a[0]);
      break;
    case 0x1D:  // trigger_set_flag6
      vm_fetch_args(ctx, 1);
      ctx->triggers[a[0]].menu_flags = 1;
      break;
    case 0x1E: {  // get_mp
      vm_fetch_args(ctx, 1);
      const int slot = party_find_slot(ctx, a[0]);
      if (slot != -1) vm_set_r(static_cast<u32>(static_cast<s32>(vm_rd16(party + slot * 0x60 + 0x24))));
      break;
    }
    case 0x1F: {  // set_mp, capped at the maximum
      vm_fetch_args(ctx, 2);
      const int slot = party_find_slot(ctx, a[0]);
      if (slot != -1) {
        u8* p = party + slot * 0x60;
        const s16 v = static_cast<s16>(a[1]);
        vm_wr16(p + 0x24, static_cast<u32>(v));
        if (vm_rd16(p + 0x22) < v) vm_wr16(p + 0x24, static_cast<u32>(vm_rd16(p + 0x22)));
      }
      break;
    }
    case 0x20: {  // get_max_hp
      vm_fetch_args(ctx, 1);
      const int slot = party_find_slot(ctx, a[0]);
      if (slot != -1) vm_set_r(read_u32(party + slot * 0x60 + 0x18));
      break;
    }
    case 0x21: {  // get_skill
      vm_fetch_args(ctx, 2);
      const u8* rec = record_of(a[0]);
      if (rec != nullptr && !vm_move_index_ok("A 0x21 get_skill", a[0], a[1])) {
        vm_set_r(0);  // (port) no such move, so not known: a learn chest opens as before
        break;
      }
      if (rec != nullptr) vm_set_r(static_cast<u32>(static_cast<s32>(static_cast<s8>(rec[a[1] + 0x3C]))));
      break;
    }
    case 0x22:  // set_status
      vm_fetch_args(ctx, 2);
      for (int i = 0; i < 8; ++i) {
        int id = vm_rd16(party + i * 0x60);
        if (id < 0) id = -id;
        if (id == a[0] + 1) {
          party[i * 0x60 + 2] = static_cast<u8>(a[1]);
          break;
        }
      }
      break;
    case 0x23: vm_svc_field_free_ot_layers(); break;
    case 0x24:  // worldmap_add_entry
      vm_fetch_args(ctx, 14);
      vm_svc_worldmap_add_entry(a);
      break;
    case 0x25: {  // get_killer_move
      vm_fetch_args(ctx, 3);
      const u8* rec = record_of(a[0]);
      vm_set_r(rec != nullptr ? rec[a[1] * 2 + 0xCC + a[2]] : 0);
      break;
    }
    case 0x26: {  // get_slot_char
      vm_fetch_args(ctx, 1);
      const s16 id = vm_rd16(party + a[0] * 0x60);
      vm_set_r(id < 1 ? 0xFFFFFFFFu : static_cast<u32>(id - 1));
      break;
    }
    case 0x27:  // trigger_set_flag7
      vm_fetch_args(ctx, 1);
      ctx->triggers[a[0]].world_map = 1;
      break;
    case 0x28:  // menu_enable
      vm_fetch_args(ctx, 1);
      g.field_flags[0x93] = a[0] == 0;
      break;
    case 0x29: vm_set_r(static_cast<u32>(g.frame_ticks)); break;
    case 0x2A:
      vm_fetch_args(ctx, 2);
      vm_svc_set_global_76220(static_cast<s16>(a[0]), static_cast<s16>(a[1]));
      break;
    case 0x2B:
      vm_fetch_args(ctx, 2);
      vm_svc_set_global_7621d(static_cast<u8>(a[0]), static_cast<u8>(a[1]));
      break;
    case 0x2C:
      vm_fetch_args(ctx, 1);
      vm_set_r(vm_svc_actor_get_3e1(a[0]) != 0);
      break;
    case 0x2D:  // exit_to_program (deletes the VM; never returns on the console)
      vm_svc_exit_to_program_teardown();
      break;
    case 0x2E:
      vm_fetch_args(ctx, 4);
      vm_wr32(gs + 0x268, static_cast<u32>(a[0]));
      vm_wr32(gs + 0x298, static_cast<u32>(a[1]));
      vm_wr32(gs + 0x29C, static_cast<u32>(a[2]));
      vm_wr16(gs + 0x28A, static_cast<u32>(a[3]));
      break;
    case 0x31:  // actor_walk_to
      vm_fetch_args(ctx, 5);
      vm_svc_actor_cmd_move_to_far(a[0], a[1], a[2], a[3], static_cast<s16>(a[4]));
      break;
    case 0x32:
      vm_svc_loader_cancel();
      vm_svc_set_loader_cancel_flag(1);
      break;
    case 0x33:
      vm_fetch_args(ctx, 1);
      vm_set_r(static_cast<u32>(vm_svc_overlay_801f9fb8(a[0])));
      break;
    case 0x34: vm_svc_inventory_clear_all(); break;
    case 0x35:  // the 0x801f9800 overlay object (script flags 19 / 20 track it)
      vm_fetch_args(ctx, 3);
      if (a[0] == 0) {
        if (vm_svc_overlay_801f9800_exists()) {
          vm_svc_overlay_801f9800_destroy();
          g.script_flags[2] &= 0xF7;
          g.script_flags[2] &= 0xEF;
        }
      } else {
        vm_svc_set_global_76310(-1);
        if (!vm_svc_overlay_801f9800_exists()) {
          vm_svc_overlay_801f9800_create(a[1], a[2]);
          g.script_flags[2] |= 0x10;
        }
      }
      break;
    case 0x36: g.script_flags[2] |= 8; break;  // script flag 19
    case 0x37: vm_set_r(static_cast<u32>(vm_svc_overlay_801f9f54())); break;
    case 0x38: {  // get_char_5c
      vm_fetch_args(ctx, 2);
      const u8* rec = record_of(a[0]);
      if (rec != nullptr) vm_set_r(static_cast<u32>(static_cast<s32>(static_cast<s8>(rec[a[1] + 0x5C]))));
      break;
    }
    case 0x39: {  // item_count
      vm_fetch_args(ctx, 1);
      const int i = inventory_find(g.inventory, a[0]);
      // An absent item reads the u16 before the table on the console; the port reads 0.
      vm_set_r(i < 0 ? 0 : static_cast<u32>(read_u16(g.inventory + 2 * i) >> 10 & 0x1F));
      break;
    }
    case 0x3A:  // actor_set_pos
      vm_fetch_args(ctx, 4);
      vm_svc_actor_set_position(a[0], a[1] << 12, a[2] << 12, a[3] << 12);
      break;
    case 0x3B: {  // config +0x14 += a, not below 0
      vm_fetch_args(ctx, 1);
      const s32 v = vm_rd32(g.config + 0x14) + a[0];
      vm_wr32(g.config + 0x14, static_cast<u32>(v < 0 ? 0 : v));
      break;
    }
    case 0x3C: vm_svc_notice_dismiss(); break;
    case 0x3D:  // menu_request_1
      vm_fetch_args(ctx, 1);
      vm_svc_set_menu_param(party_find_slot(ctx, a[0]));
      vm_svc_set_menu_request(1);
      ctx->wait_state = 0xE;
      break;
    case 0x3E: vm_set_r(static_cast<u32>(vm_svc_party_get_super_specialty6())); break;
    case 0x3F: {
      vm_fetch_args(ctx, 1);
      // [world agent] Only for a member in the party: slot -1 (a join that Archipelago's
      // character items held back; in the original the join before it never fails) would
      // select the 0x60 bytes before the party block.
      const int slot = party_find_slot(ctx, a[0]);
      if (slot >= 0) vm_svc_party_member_clear_bond(slot);
      break;
    }
    case 0x40:
      vm_fetch_args(ctx, 1);
      vm_svc_overlay_801f9a64(a[0]);
      break;
    case 0x41: case 0x42: case 0x43: case 0x44: case 0x45: case 0x46: case 0x47: case 0x48:
      // choice_set_lines: lines per option. All sixteen are copied from the scratchpad,
      // so options past the fetched ones take what the previous fetch left there.
      vm_fetch_args(ctx, static_cast<int>(sub - 0x40));
      for (int i = 0; i < 16; ++i) ctx->option_lines[i] = static_cast<s16>(a[i]);
      break;
    // Archipelago (port feature; not in the original, where these subs do nothing): Port Hilton's
    // harbour in an open-world seed (game/ap harbour_*, docs/ap/seed_format.md "Phase 4"). Only the
    // patch lines an open-world seed adds put them in a script (maps 169 and 144).
    case 0x70: {
      // In place of the Hilton sailor's `push 2` before the choice library call (0x484c): the
      // captain's option count. The stack holds the call's arguments so far (..., the question's
      // string, the choice window's width); the next instruction pushes the options' string.
      const s32 question = static_cast<s32>(ctx->value_sp[-2]);
      const u32 next = *ctx->ip;
      const s32 options = (next >> 24) == 0x12 ? static_cast<s32>(next & 0xFFFFFF) : question + 1;
      *ctx->value_sp++ = static_cast<u32>(ap::harbour_open(question, options));
      break;
    }
    case 0x71: {
      // After the choice (the option popped): pushes 1 for the sailor's own fare and Cross voyage,
      // 0 for nothing (the routine's jz goes to its end), or goes on elsewhere.
      constexpr u32 kElResume = 0x454C0001u;  // (not an option: this op again after 444's fade)
      vm_fetch_args(ctx, 1);
      if (static_cast<u32>(a[0]) == kElResume) {
        // Room 444's ending, after its 90 frames: chapter 28 step 0 and its transfer to Lacour
        // Castle (map_transfer_c, from the stack: the op's own offset is no seed door).
        s32 args[7];
        ap::el_voyage_transfer(args);
        *ctx->value_sp++ = 0;
        for (const s32 v : args) *ctx->value_sp++ = static_cast<u32>(v);
        vm_exec_ext(ctx, 0xDA800000u);
        break;
      }
      const ap::HarbourChoice c = ap::harbour_choose(a[0]);
      if (c.kind == ap::HarbourChoice::kCross) {
        *ctx->value_sp++ = 1;
      } else if (c.kind == ap::HarbourChoice::kJump && c.offset >= 0 && (c.offset & 3) == 0 &&
                 ctx->code[c.offset / 4] == 0x12000001u) {
        // The Hoffmann soldier's sail (map 169 0x7cf8: `push 1`, the preload of map 1, no locals;
        // its routine's return ends the talk).
        ctx->ip = ctx->code + c.offset / 4;
      } else if (c.kind == ap::HarbourChoice::kEl) {
        // Room 444's ending before its transfer (routine 0x7c28: ext 0xba 90, ext 0x7c 3 90,
        // wait 90), then this op again.
        if (vm_rd16(g.game_state + 0x1C) != -1 && ctx->music == -1) vm_svc_sound_fade_out(90);
        vm_svc_fade_push(3, 90, 0, 0);
        *ctx->value_sp++ = kElResume;
        --ctx->ip;
        ctx->wait += 90;
      } else {
        if (c.kind == ap::HarbourChoice::kJump) std::fprintf(stderr, "[ap] harbour: the Hoffmann sail is not at %#x\n", c.offset);
        *ctx->value_sp++ = 0;
      }
      break;
    }
    case 0x72: {
      // In place of `push_eq <option>, 0` before a voyage (the Hoffmann soldier's yes, Herlie's
      // sailor's): pushes option == 0 and the landing's key is held (instr & 0xff: the voyage's
      // landing flag; a banner names a missing key), before any fare is paid.
      vm_fetch_args(ctx, 1);
      const bool go = a[0] == 0 && ap::voyage_landing_open(static_cast<int>(instr & 0xFF));
      *ctx->value_sp++ = go ? 1u : 0u;
      break;
    }
    default:  // 0x00, 0x2f, 0x30, > 0x48 (and the port's above)
      vm_note_unknown(instr);
      break;
  }
}

}  // namespace ss::game::field
