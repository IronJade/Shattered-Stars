// vm_exec_table_b (field 0x80064f30): opcode 0xff, sub-command in bits 16-22 through a
// compare tree (0x00, 0x08, 0x17, 0x52, 0x72 and 0x7e do nothing). Actors, message
// window flags, relations, map objects, camera, preloads, pickpocketing, items,
// equipment and overlay effects.
#include <cstring>

#include "game/ap/ap.h"
#include "game/boot/rng.h"
#include "game/field/script_vm.h"
#include "game/field/vm_services.h"

namespace ss::game::field {
namespace {

s32* const a = g_vm_args;

int abs_id(const u8* slot) {
  const int id = vm_rd16(slot);
  return id < 0 ? -id : id;
}

void vector12(s32 out[3], int first) {
  out[0] = a[first] << 12;
  out[1] = a[first + 1] << 12;
  out[2] = a[first + 2] << 12;
}

// Relations (config +0x58 and +0xe8: 12 x 12 bytes): += delta, clamped 0..15.
void relation_add(int table, int who, int toward, int delta) {
  u8* p = globals().config + table + who * 12 + toward;
  s32 v = *p + delta;
  if (v < 0) {
    v = 0;
  } else if (v > 15) {
    v = 15;
  }
  *p = static_cast<u8>(v);
}

}  // namespace

void vm_exec_table_b(ScriptCtx* ctx, u32 instr) {
  FieldGlobals& g = globals();
  u8* party = g.party;
  const u32 sub = instr >> 16 & 0x7F;
  switch (sub) {
    case 0x01: {  // actor_distance
      vm_fetch_args(ctx, 2);
      s32 x0, y0, z0, x1, y1, z1;
      vm_svc_actor_get_position(a[0], &x0, &y0, &z0);
      vm_svc_actor_get_position(a[1], &x1, &y1, &z1);
      const s32 dx = (x0 - x1) >> 12, dy = (y0 - y1) >> 12, dz = (z0 - z1) >> 12;
      vm_set_r(static_cast<u32>(SquareRoot0(dx * dx + dy * dy + dz * dz)));
      break;
    }
    case 0x02: {  // name_equal
      vm_fetch_args(ctx, 2);
      const char* n0 = reinterpret_cast<const char*>(g.game_state + a[0] * 0x14 + 0x28);
      const char* n1 = reinterpret_cast<const char*>(g.game_state + a[1] * 0x14 + 0x28);
      vm_set_r(std::strcmp(n0, n1) == 0);
      break;
    }
    case 0x03:  // msg_close
      vm_fetch_args(ctx, 1);
      vm_svc_msg_window_request_close(a[0]);
      break;
    case 0x04:  // actor_color_fade
      vm_fetch_args(ctx, 6);
      vm_svc_actor_queue_fade_colour(a[0], a[1], a[2], a[3], a[4], a[5]);
      break;
    case 0x05:  // actor_wait_color
      vm_fetch_args(ctx, 1);
      vm_svc_actor_queue_wait_fade(a[0]);
      break;
    case 0x06:
      vm_fetch_args(ctx, 2);
      vm_svc_walk_set_group_enabled(a[0], a[1]);
      break;
    case 0x07: {  // the scene's actor scale
      const u8* scene = vm_svc_map_scene();
      vm_set_r(scene != nullptr ? read_u16(scene + 0x68) : 0);
      break;
    }
    case 0x09:  // msg_set_13d
      vm_fetch_args(ctx, 2);
      if (vm_svc_msg_window_exists(a[1])) vm_svc_msg_window_set_close_flag(a[1], a[0] != 0);
      break;
    case 0x0A:  // random
      vm_fetch_args(ctx, 1);
      if (a[0] == 0) {
        vm_set_r(0);
      } else {
        vm_set_r(static_cast<u32>(rng_next()) % static_cast<u32>(a[0]));
      }
      break;
    case 0x0B: {
      vm_fetch_args(ctx, 11);
      s32 p0[3], p1[3];
      vector12(p0, 1);
      vector12(p1, 4);
      vm_svc_fx_spawn_beam(a[0] & 0xFF, p0, p1, a + 7);
      break;
    }
    case 0x0C:  // msg_set_140
      vm_fetch_args(ctx, 2);
      vm_svc_msg_window_set_advance(a[1], static_cast<u8>(a[0]));
      break;
    case 0x0D:
      vm_fetch_args(ctx, 1);
      vm_svc_actor_stop(a[0]);
      break;
    case 0x0E:  // trigger_set_pa
      vm_fetch_args(ctx, 2);
      ctx->triggers[a[0]].private_action = static_cast<s16>(a[1]);
      break;
    case 0x0F:  // mapobj_apply
      vm_fetch_args(ctx, 3);
      vm_svc_sprite_packs_tint(a[0], a[1], a[2]);
      vm_svc_actor_sys_reset_sprite_cache();
      vm_svc_actor_sys_mark_all_dirty();
      break;
    case 0x10:
      vm_fetch_args(ctx, 3);
      relation_add(0x58, a[0], a[1], a[2]);
      break;
    case 0x11:
      vm_fetch_args(ctx, 2);
      vm_set_r(g.config[a[0] * 12 + 0x58 + a[1]]);
      break;
    case 0x12:
      vm_fetch_args(ctx, 3);
      relation_add(0xE8, a[0], a[1], a[2]);
      break;
    case 0x13:
      vm_fetch_args(ctx, 2);
      vm_set_r(g.config[a[0] * 12 + 0xE8 + a[1]]);
      break;
    case 0x14: party_restore_hp_all(ctx); break;
    case 0x15: party_restore_mp_all(ctx); break;
    case 0x16: case 0x70:
      vm_svc_overlay_801eac28(ctx, instr);
      break;
    case 0x18: vm_svc_sprite_packs_restore_palettes(); break;
    case 0x19: case 0x1A: case 0x1B: case 0x1C: case 0x1D: case 0x1E: case 0x1F: case 0x20:  // mapobj_set_bytes
      vm_fetch_args(ctx, static_cast<int>(sub - 0x16));
      vm_svc_sprite_anim_set_frame_bytes(a[0], a[1], static_cast<int>(sub - 0x18), a + 2);
      break;
    case 0x21:
      vm_fetch_args(ctx, 3);
      vm_svc_sprite_anim_set_loop_frame(a[0], a[1], a[2]);
      break;
    case 0x22:
      vm_fetch_args(ctx, 3);
      vm_svc_sprite_anim_set_byte2(a[0], a[1], a[2]);
      break;
    case 0x23:
      vm_fetch_args(ctx, 1);
      vm_svc_actor_push_state(a[0]);
      break;
    case 0x24:
      vm_fetch_args(ctx, 1);
      vm_svc_actor_pop_state(a[0], 0);
      break;
    case 0x25:
      vm_fetch_args(ctx, 1);
      vm_set_r(vm_svc_actor_exists(a[0]) ? static_cast<u32>(static_cast<u16>(vm_svc_actor_get_layer(a[0])))
                                         : 0xFFFFFFFFu);
      break;
    case 0x26:
      vm_fetch_args(ctx, 2);
      vm_svc_actor_set_layer(a[0], static_cast<s16>(a[1]));
      break;
    case 0x27:
      vm_fetch_args(ctx, 2);
      vm_svc_actor_set_16(a[0], static_cast<s16>(a[1]));
      vm_svc_actor_mark_dirty(a[0]);
      break;
    case 0x28:
      vm_fetch_args(ctx, 1);
      vm_set_r(vm_svc_actor_exists(a[0]));
      break;
    case 0x29: party_isolate_hero(ctx); break;
    case 0x2A: party_restore_saved(ctx); break;
    case 0x2B: vm_set_r(g.field_flags[0x6B]); break;  // saved_party_count
    case 0x2C: {  // saved_party_has
      vm_fetch_args(ctx, 1);
      const int count = g.field_flags[0x6B];
      for (int i = 0; i < count; ++i) {
        if ((g.field_flags[0x6C + i] & 0xF) == static_cast<u32>(a[0] + 1)) {
          vm_set_r(1);
          return;
        }
      }
      vm_set_r(0);
      break;
    }
    case 0x2D: {  // music_preload(music, owner trigger)
      vm_fetch_args(ctx, 2);
      s32 saved = 0;
      if (a[1] != -1) {
        saved = ctx->active_trigger;
        ctx->active_trigger = a[1];
      }
      const u8* e = field_data(0x800750CC + 2 * static_cast<u32>(a[0]));
      a[0] = e != nullptr ? vm_rd16(e) : 0;
      if (a[0] != 0) {
        vm_svc_preload_release(1);
        vm_svc_set_preload_release_pending(0, 0);
        vm_preload_entry(ctx, a[0], 1, 0);
      }
      if (a[1] != -1) ctx->active_trigger = saved;
      break;
    }
    case 0x2E:
      vm_fetch_args(ctx, 1);
      vm_set_r(static_cast<u32>(static_cast<s16>(vm_svc_actor_get_view_x(a[0]))));
      break;
    case 0x2F:
      vm_fetch_args(ctx, 1);
      vm_set_r(static_cast<u32>(static_cast<s16>(vm_svc_actor_get_view_y(a[0]))));
      break;
    case 0x30: {  // bg_color_enable
      vm_fetch_args(ctx, 1);
      vm_svc_set_scene_loaded(static_cast<u8>(a[0]));
      if ((a[0] & 0xFF) == 0) {
        vm_svc_screen_set_clear_colour(0, 0, 0);
      } else {
        const u8* scene = vm_svc_map_scene();
        if (scene != nullptr) vm_svc_screen_set_clear_colour(scene[0x6F], scene[0x70], scene[0x71]);
      }
      break;
    }
    case 0x31:  // actor_draw_reset
      vm_fetch_args(ctx, 1);
      vm_svc_set_field_active(static_cast<u8>(a[0]));
      if ((a[0] & 0xFF) != 0) {
        vm_svc_actor_sys_mark_all_dirty();
        vm_svc_actor_sys_reset_sprite_cache();
      }
      break;
    case 0x32: case 0x33: {  // actor_view_x / _y: logical position minus the camera offset
      vm_fetch_args(ctx, 1);
      s16 cam_x = 0, cam_y = 0;
      vm_svc_bg_get_view_origin(&cam_x, &cam_y);
      if (sub == 0x32) {
        vm_set_r(static_cast<u32>(static_cast<s16>(vm_svc_actor_get_view_x(a[0])) - cam_x));
      } else {
        vm_set_r(static_cast<u32>(static_cast<s16>(vm_svc_actor_get_view_y(a[0])) - cam_y));
      }
      break;
    }
    case 0x34: {  // leader_can_reach (2D fields only)
      vm_set_r(0);
      vm_fetch_args(ctx, 3);
      if (g.field_flags[0x68] != 0) return;
      const int leader = leader_slot();
      if (vm_svc_actor_exists(leader)) {
        vm_set_r(vm_svc_walk_test_obstacles(leader, a[0] << 12, a[1] << 12, a[2] << 12) == 0);
      }
      break;
    }
    case 0x35: case 0x37: {  // char_get_status / char_get_hp
      vm_fetch_args(ctx, 1);
      s32 hp;
      u8 status;
      party_get_hp_status(ctx, a[0], &hp, &status);
      vm_set_r(sub == 0x35 ? status : static_cast<u32>(hp));
      break;
    }
    case 0x36:
      vm_fetch_args(ctx, 2);
      vm_svc_msg_window_set_hold(a[1], static_cast<u8>(a[0]));
      break;
    case 0x38:
      vm_fetch_args(ctx, 2);
      vm_svc_msg_window_set_13b(a[1], a[0] != 0);
      break;
    case 0x39:
      vm_fetch_args(ctx, 5);
      vm_svc_actor_create_or_place(a[0], a[1], a[2], a[3], a[4]);
      break;
    case 0x3A:  // actor_set_speed2
      vm_fetch_args(ctx, 2);
      vm_svc_actor_set_speed(a[0], a[1], a[1]);
      vm_svc_actor_mark_dirty(a[0]);
      break;
    case 0x3B:
      vm_fetch_args(ctx, 1);
      vm_svc_actor_delete_slot(static_cast<s16>(a[0]));
      break;
    case 0x3C: {  // actor_find_free in a0..a1
      vm_fetch_args(ctx, 2);
      s32 i = a[0];
      while (i <= a[1] && vm_svc_actor_exists(i)) ++i;
      vm_set_r(i != a[1] ? static_cast<u32>(i) : 0);
      break;
    }
    case 0x3D:
      vm_fetch_args(ctx, 2);
      vm_svc_actor_set_anim(a[0], a[1]);
      break;
    case 0x3E:  // actor_set_74_76
      vm_fetch_args(ctx, 3);
      vm_svc_actor_set_view_pos(a[0], static_cast<s16>(a[1]), static_cast<s16>(a[2]));
      break;
    case 0x3F: {  // char_get_skill_level
      vm_fetch_args(ctx, 2);
      const int slot = party_find_slot(ctx, a[0]);
      vm_set_r(slot == -1 ? 0xFFFFFFFFu : static_cast<u32>(vm_svc_party_get_specialty_level(slot, a[1])));
      break;
    }
    case 0x40: case 0x41: {  // camera centre x / y
      s16 cam_x = 0, cam_y = 0;
      vm_svc_bg_get_view_origin(&cam_x, &cam_y);
      vm_set_r(static_cast<u32>(sub == 0x40 ? cam_x + 0xA0 : cam_y + 0x78));
      break;
    }
    case 0x42:  // camera_set_target
      vm_fetch_args(ctx, 2);
      vm_svc_bg_set_target(static_cast<s16>(a[0]), static_cast<s16>(a[1]));
      field_state_set_s16(0xE, -1);
      break;
    case 0x43:  // actor render tint on
      vm_fetch_args(ctx, 3);
      vm_svc_set_bg_tint(static_cast<u8>(a[0]), static_cast<u8>(a[1]), static_cast<u16>(a[2]));
      vm_svc_set_bg_tint_active(1);
      break;
    case 0x44:  // off
      vm_svc_set_bg_tint_active(0);
      vm_svc_bg_restore_tint();
      break;
    case 0x45:
      vm_fetch_args(ctx, 2);
      vm_svc_msg_window_set_wrap(a[1], a[0] != 0);
      break;
    case 0x46:
      vm_fetch_args(ctx, 2);
      vm_svc_msg_window_set_anchor(a[1], a[0] != 0);
      break;
    case 0x47:
      vm_fetch_args(ctx, 4);
      vm_svc_sprite_anim_set_frame_byte(a[0], a[1], a[2], a[3]);
      break;
    case 0x48: {  // map_preload(map, owner trigger)
      vm_fetch_args(ctx, 2);
      s32 saved = 0;
      if (a[1] != -1) {
        saved = ctx->active_trigger;
        ctx->active_trigger = a[1];
      }
      s16 files[17] = {};
      vm_svc_map_resolve_entries(files, a[0], 0, 0);
      // 3D maps also take party pack A and its trailing overlay copy in one read.
      if (vm_preload_entry(ctx, files[2], 1, 0) != 0 && g.field_flags[0x68] != 0) {
        const int extra = vm_svc_loader_entry_size(files[3]) + vm_svc_loader_entry_size(files[5]);
        vm_preload_entry(ctx, files[3], 1, extra);
      }
      if (a[1] != -1) ctx->active_trigger = saved;
      break;
    }
    case 0x49:
      vm_fetch_args(ctx, 2);
      vm_svc_bg_set_114(static_cast<s16>(a[0]), static_cast<s16>(a[1]));
      break;
    case 0x4A:
      vm_fetch_args(ctx, 1);
      vm_svc_bg_set_122(static_cast<u8>(a[0]));
      break;
    case 0x4B:
      vm_fetch_args(ctx, 1);
      vm_svc_bg_set_124(static_cast<s16>(a[0]));
      break;
    case 0x4C:
      vm_fetch_args(ctx, 1);
      vm_svc_bg_set_126(static_cast<s16>(a[0]));
      break;
    case 0x4D: {
      vm_fetch_args(ctx, 15);
      s32 p0[3], p1[3], p2[3];
      vector12(p0, 1);
      vector12(p1, 4);
      vector12(p2, 7);
      vm_svc_fx_spawn_triangle(a[0] & 0xFF, p0, p1, p2, a + 10);
      break;
    }
    case 0x4E: vm_set_r(party_member_mask_all()); break;
    case 0x4F: {  // entry_ready
      vm_fetch_args(ctx, 1);
      if (vm_svc_loader_poll() == 1) return;
      const u8* e = field_data(0x800751EC + 2 * static_cast<u32>(a[0]));
      vm_set_r(static_cast<u32>(vm_svc_loader_11988(0, e != nullptr ? vm_rd16(e) : 0)));
      break;
    }
    case 0x50:
      vm_svc_set_loader_cancel_flag(0);
      vm_svc_overlay_801eaa30(ctx);
      break;
    case 0x51: vm_set_r(vm_svc_overlay_801ea_loaded() != 0); break;
    case 0x53:
      vm_set_r(vm_svc_overlay_801ea_loaded() != 0 ? static_cast<u32>(vm_svc_overlay_801ea_value()) : 0xFFFFFFFFu);
      break;
    case 0x54: vm_set_r(static_cast<u32>(static_cast<s32>(vm_rd16(g.game_state + 0x1A)))); break;
    case 0x55:
      vm_fetch_args(ctx, 1);
      g.field_flags[0x83] = static_cast<u8>(a[0]);
      break;
    case 0x56: {  // pickpocket_roll(difficulty)
      vm_fetch_args(ctx, 1);
      const s32 scaled = ctx->pickpocket_level << 4;
      u32 chance = a[0] == 0 ? 0 : static_cast<u32>(a[0] == -1 ? -scaled : scaled / a[0]);
      if (ctx->pickpocket_level != 0 && ctx->pickpocket_bonus != 0) chance += 5;
      vm_wr16(g.game_state + 0x24A, static_cast<u32>(vm_rd16(g.game_state + 0x24A) + 1));
      const u32 success = rng_range(100) < chance;
      if (success) vm_wr16(g.game_state + 0x24C, static_cast<u32>(vm_rd16(g.game_state + 0x24C) + 1));
      vm_set_r(success);
      break;
    }
    case 0x57:
      vm_fetch_args(ctx, 1);
      vm_svc_actor_delete_attachment(a[0]);
      break;
    case 0x58:
      vm_fetch_args(ctx, 1);
      vm_set_r(vm_svc_actor_has_attachment(a[0]));
      break;
    case 0x59: {  // pickpocket_penalty: party members who saw it like the thief less (10% each)
      const u32 thief = static_cast<u32>(ctx->thief_slot);
      if (thief > 7) return;
      u32 mask = party_member_mask_all();
      int character = 0;
      int c = 1;
      for (; c <= 12; ++c) {
        if (c == (vm_rd16(party + thief * 0x60) & 0x7F)) break;
      }
      if (c > 12) return;
      character = c - 1;
      for (int who = 0; who < 12; ++who, mask = static_cast<u32>(static_cast<s32>(mask) >> 1)) {
        if ((mask & 1) == 0 || who == character || rng_range(100) >= 10) continue;
        u8* b = g.config + 0xE8 + who * 12 + character;
        s32 v = *b - 1;
        *b = static_cast<u8>(v < 0 ? 0 : v);
        // The console then writes table A's value minus one over table B again.
        v = g.config[who * 12 + character + 0x58] - 1;
        *b = static_cast<u8>(v < 0 ? 0 : v);
      }
      break;
    }
    case 0x5A: vm_svc_sound_bank_holder_alloc(); break;
    case 0x5B: vm_svc_sound_bank_holder_free(); break;
    case 0x5C:  // set_menu_close_entry
      if ((instr & 0x800000) == 0) {
        vm_fetch_args(ctx, 3);
        ctx->menu_close_entry = static_cast<s16>(a[1]);
      } else {
        vm_fetch_args(ctx, 1);
        ctx->menu_close_entry = static_cast<s16>(a[0]);
      }
      break;
    case 0x5D: {  // char_set_hp, capped at the maximum (every matching slot)
      if ((instr & 0x800000) == 0) {
        vm_fetch_args(ctx, 3);
        a[1] = static_cast<s32>((static_cast<u32>(a[1]) & 0xFFFF) | static_cast<u32>(a[2]) << 16);
      } else {
        vm_fetch_args(ctx, 2);
      }
      for (int i = 0; i < 8; ++i) {
        u8* p = party + i * 0x60;
        if (abs_id(p) != a[0] + 1) continue;
        vm_wr32(p + 0x1C, static_cast<u32>(a[1]));
        if (vm_rd32(p + 0x18) < a[1]) vm_wr32(p + 0x1C, static_cast<u32>(vm_rd32(p + 0x18)));
      }
      break;
    }
    case 0x5E:  // has_item
      vm_fetch_args(ctx, 1);
      vm_set_r(inventory_find(g.inventory, a[0]) != -1);
      break;
    case 0x5F:  // party_has_equipped
      vm_fetch_args(ctx, 1);
      vm_set_r(static_cast<u32>(party_has_equipped(ctx, static_cast<s16>(a[0]))));
      break;
    case 0x60:  // give_item
      vm_fetch_args(ctx, 1);
      // Archipelago (port feature): a seed's placeholder gives its location's item instead.
      if (!ap::on_give_item(static_cast<s32>(a[0]))) inventory_add(g.inventory, a[0], 1, 1);
      vm_svc_set_global_76278(1);
      break;
    case 0x61:
      vm_set_r(vm_svc_actor_get_3e1(leader_slot()) != 0);
      break;
    case 0x62:
      vm_fetch_args(ctx, 1);
      g.field_flags[0x81] = static_cast<u8>(a[0]);
      break;
    case 0x63: {  // remove_item(item, count)
      vm_fetch_args(ctx, 2);
      if (a[0] == 0) return;
      const int i = inventory_find(g.inventory, a[0]);
      if (i != -1) inventory_remove_at(g.inventory, i, a[1]);
      break;
    }
    case 0x64:
      vm_fetch_args(ctx, 1);
      vm_set_r(static_cast<u32>(static_cast<s16>(vm_svc_actor_get_anim(a[0]))));
      break;
    case 0x65:
      vm_fetch_args(ctx, 2);
      vm_svc_bg_anim_play(a[0], a[1]);
      break;
    case 0x66:  // marker_create: actor 0x80 (sprite 0x7ffe)
      vm_fetch_args(ctx, 4);
      vm_svc_actor_create(0x80, 0x7FFE, a[0], a[1], a[2], 0, a[3], 0xFFFF, 0xFFFF, 0x1000F);
      vm_svc_actor_and_flags(0x80, 0, 0xFFDEFFFB);
      vm_svc_actor_or_flags(0x80, 0, 8);
      vm_svc_actor_cmd_set_anim(0x80, 0x24);
      break;
    case 0x67:
      vm_fetch_args(ctx, 1);
      vm_svc_set_global_76195(static_cast<u8>(a[0]));
      break;
    case 0x68:
      vm_fetch_args(ctx, 1);
      vm_set_r(static_cast<u32>(vm_svc_bg_anim_get_state(a[0])));
      break;
    case 0x69:  // menu_request_5
      vm_svc_set_menu_request(5);
      ctx->wait_state = 0xE;
      break;
    case 0x6A:  // disc_change_2
      g.config[0x4C] = 1;
      vm_svc_field_prompt_disc_change(2, 1);
      break;
    case 0x6B:
      vm_fetch_args(ctx, 3);
      vm_svc_actor_set_special(a[0], a[1], a[2]);
      break;
    case 0x6C: {  // chest_create(slot, model, x, y, z, param, flag, entry)
      vm_fetch_args(ctx, 8);
      const s32 n = a[6] < 0 ? a[6] + 7 : a[6];
      const bool open = (g.script_flags[n >> 3] >> (a[6] & 7) & 1) != 0;
      vm_svc_actor_create(a[0], a[1], a[2], a[3], a[4], 0, a[5], open ? 0xFFFF : (a[7] & 0xFFFF), 0xFFFF, 0x1000F);
      vm_svc_actor_set_anim(a[0], open ? 0x25 : 0x26);
      vm_svc_actor_and_flags(a[0], 0, 0xFFFEFFFF);
      break;
    }
    case 0x6D:  // menu_request_2
      vm_fetch_args(ctx, 1);
      vm_svc_set_menu_request(2);
      ctx->wait_state = 0xE;
      vm_svc_set_menu_param(static_cast<s16>(a[0]));
      break;
    case 0x6E:
      vm_fetch_args(ctx, 1);
      vm_set_r(static_cast<u32>(vm_svc_bg_anim_get_frame(a[0])));
      break;
    case 0x6F:  // ovl_801ea374_reload
      vm_fetch_args(ctx, 8);
      vm_svc_set_loader_cancel_flag(0);
      if (vm_svc_overlay_801ea_loaded() == 0) {
        if (vm_svc_overlay_801ea374(ctx) != 0) vm_svc_field_load_map(vm_rd16(g.game_state + 0x1A), 1, 0);
      } else {
        vm_set_r(0);
      }
      break;
    case 0x71:
      vm_fetch_args(ctx, 3);
      vm_svc_pad_vibrate(a[0], a[1], a[2]);
      break;
    case 0x73:  // char_unequip_slot
      vm_fetch_args(ctx, 2);
      char_set_equipment(ctx, static_cast<s16>(a[0]), static_cast<s16>(a[1]), 0);
      break;
    case 0x74:  // char_set_equipment
      vm_fetch_args(ctx, 3);
      char_set_equipment(ctx, static_cast<s16>(a[0]), static_cast<s16>(a[1]), a[2]);
      break;
    case 0x75:
      vm_fetch_args(ctx, 1);
      vm_set_r(static_cast<u32>(relation_hash_mod(ctx, a[0])));
      break;
    case 0x76: {
      // best_friend_of: the member liking a0 most (ties broken by relation_hash_mod).
      // The console has no break here and goes on into 0x79, fetching two more arguments.
      vm_fetch_args(ctx, 1);
      s32 sums[12];
      for (int i = 0; i < 8; ++i) sums[i] = -1;
      for (int i = 8; i < 12; ++i) sums[i] = 0;  // stack the console never reads back
      for (int i = 0; i < 8; ++i) {
        const u16 id = read_u16(party + i * 0x60);
        if (static_cast<u32>(id - 1) < 12 && static_cast<s16>(id) != a[0] + 1) {
          const int c = static_cast<s16>(id) - 1;
          sums[c] += g.config[c * 12 + a[0] + 0xE8];
          sums[c] += g.config[c * 12 + a[0] + 0x58];
        }
      }
      s32 best = -100;
      int count = 0;
      s32 candidates[8] = {};
      for (int i = 0; i < 8; ++i) {
        if (best <= sums[i]) {
          if (best < sums[i]) {
            count = 0;
            best = sums[i];
          }
          candidates[count++] = i;
        }
      }
      if (count == 0) {
        vm_set_r(0);
      } else {
        vm_set_r(static_cast<u32>(candidates[relation_hash_mod(ctx, count)]));
      }
      [[fallthrough]];
    }
    case 0x79:  // actor_set_7f
      vm_fetch_args(ctx, 2);
      vm_svc_actor_set_7f(a[0], static_cast<u8>(a[1]));
      vm_svc_actor_mark_dirty(a[0]);
      break;
    case 0x77:
      vm_fetch_args(ctx, 2);
      vm_svc_bg_anim_set_frame(a[0], a[1]);
      break;
    case 0x78: {
      vm_fetch_args(ctx, 10);
      s32 p[3];
      vector12(p, 1);
      vm_svc_fx_spawn_orb(a[0] & 0xFF, p, a + 4);
      break;
    }
    case 0x7A:  // menu_request_4
      vm_fetch_args(ctx, 1);
      vm_svc_set_menu_param(party_find_slot(ctx, a[0]));
      vm_svc_set_menu_request(4);
      ctx->wait_state = 0xE;
      break;
    case 0x7B: {
      vm_fetch_args(ctx, 13);
      s32 p0[3], p1[3];
      vector12(p0, 1);
      vector12(p1, 4);
      vm_svc_fx_spawn_streak(a[0] & 0xFF, p0, p1, a[7], a[8], a[9], a[10] << 12, a[11], a[12] != 0);
      break;
    }
    case 0x7C: {
      vm_fetch_args(ctx, 11);
      s32 p0[3], p1[3];
      vector12(p0, 1);
      vector12(p1, 4);
      vm_svc_fx_spawn_spark(a[0] & 0xFF, p0, p1, a + 7);
      break;
    }
    case 0x7D: {
      vm_fetch_args(ctx, 12);
      s32 p[3];
      vector12(p, 1);
      vm_svc_fx_spawn_ring(a[0] & 0xFF, p, a + 4);
      break;
    }
    case 0x7F:  // menu_request_3
      vm_fetch_args(ctx, 1);
      vm_svc_set_menu_param(party_find_slot(ctx, a[0]));
      vm_svc_set_menu_request(3);
      ctx->wait_state = 0xE;
      break;
    default:  // 0x00, 0x08, 0x17, 0x52, 0x72, 0x7e
      vm_note_unknown(instr);
      break;
  }
}

}  // namespace ss::game::field
