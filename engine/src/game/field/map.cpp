#include "game/field/map.h"

#include <cstdlib>
#include <cstring>
#include <string>

#include "disc/slz.h"
#include "game/common/texture.h"
#include "game/field/actor_field.h"
#include "game/field/actors.h"
#include "game/field/afterimage.h"
#include "game/field/camera.h"
#include "game/field/field_movie.h"
#include "game/field/post_battle.h"
#include "game/field/field_sub_overlays.h"
#include "game/field/bg.h"
#include "game/field/callways.h"
#include "game/field/effects.h"
#include "game/field/fade.h"
#include "game/field/field_world.h"
#include "game/field/collision.h"
#include "game/field/load_queue.h"
#include "game/field/menu_camp.h"
#include "game/field/message.h"
#include "game/field/mods.h"
#include "game/field/music.h"
#include "game/field/notice.h"
#include "game/field/scene_renderer.h"
#include "game/field/screen.h"
#include "game/field/sprite_pack.h"
#include "game/field/script_vm.h"
#include "game/field/worldmap.h"
#include "runtime/libgpu.h"
#include "runtime/runtime.h"

namespace ss::game::field {
namespace {

MapData g_map;
u32 g_sub_sections = 0;  // bit n: container section n (5-12) on the current map
SpritePackSlot g_packs[16];
std::vector<u8> g_load_buffer;  // 0x80075738
s32 g_load_bytes = 0;           // 0x8007573c
MapHooks g_hooks;

s32 rd32(const u8* p) {
  s32 v;
  std::memcpy(&v, p, 4);
  return v;
}
s16 rd16s(const u8* p) {
  s16 v;
  std::memcpy(&v, p, 2);
  return v;
}
u16 rd16(const u8* p) {
  u16 v;
  std::memcpy(&v, p, 2);
  return v;
}

// field_update_screen_steering (0x80055c08): on maps whose camera looks down (the
// scene's target y above its eye y: +0x18 < +0x0c) the analog stick steers along the
// projected screen (fieldactor_player_control), except on a fixed list of maps.
s16 screen_clamp_flag(const u8* scene, int map) {
  if (scene == nullptr || rd32(scene + 0x18) >= rd32(scene + 0x0C)) return 0;
  const bool excepted = (map >= 0xA1 && map <= 0xA2) || map == 0xB7 || (map >= 0x17F && map <= 0x180) ||
                        map == 0x1A4 || map == 0x1B8 || (map >= 0x1C8 && map <= 0x1D2);
  return excepted ? 0 : 1;
}

[[maybe_unused]] void todo(const char* what) {
  runtime::Runtime::get().log_once(std::string("map:") + what, std::string("field: not ported yet: ") + what);
}

std::vector<u8> unpack(const u8* slz, int block) {
  // Sizes come from the block headers; 1 MB bounds a single chain.
  auto out = disc::slz::unpack_block(std::span<const u8>(slz, 0x100000), 0, block, nullptr);
  return out ? std::move(*out) : std::vector<u8>{};
}


}  // namespace

MapData& map_data() { return g_map; }
SpritePackSlot* sprite_pack_slots() { return g_packs; }
std::vector<u8>& field_load_buffer() { return g_load_buffer; }
MapHooks& map_hooks() { return g_hooks; }

void map_resolve_entries(MapFiles* files, int map, u32 x, u32 z) {
  const FieldGlobals& g = globals();
  const u8* block = g.state_block;
  files->music = -1;
  int m = map;
  if (map == -1) {
    m = rd16s(block + 0x23A);
    if (rd16s(block + 0x23C) != -1) files->music = rd16s(block + 0x23C);
    x = static_cast<u32>(rd32(block + 0x228));
    z = static_cast<u32>(rd32(block + 0x230));
  }
  files->overlay = static_cast<s16>(static_cast<u32>(m - 1) < 3 ? 0xC1E : 0xC1D);
  const u32 mask = party_member_mask_all();
  files->system = 0xC26;
  const int b = static_cast<int>((mask & 0xF80) >> 7);
  if (static_cast<u32>(m - 1) < 3) {
    // World maps: chunk containers by position, and the world party pack.
    int chunk = 0;
    if (m == 1) {
      chunk = static_cast<int>(x / 0x3000 + (z / 0x3000) * 9);
      if (static_cast<u32>(rd32(block + 0x220) - 0xF) < 0x2E) {
        static const int remap[][2] = {{0x26, 0x3F}, {0x27, 0x40}, {0x28, 0x41}, {0x2F, 0x42}, {0x30, 0x43},
                                       {0x31, 0x44}, {0x38, 0x45}, {0x39, 0x46}, {0x3A, 0x47}};
        for (const auto& r : remap)
          if (chunk == r[0]) chunk = r[1];
      }
      files->container = static_cast<s16>(chunk + 0x1014);
    } else if (m == 2) {
      files->container = static_cast<s16>(x / 0x3000 + (z / 0x3000) * 9 + 0x109F);
    } else {
      files->container = 0x10E0;
    }
    if (map == -1) {
      static const s16 restore[3][3] = {{0x105D, 0x105E, 0x105F}, {0x10E6, 0x10E7, 0x10E8}, {0x10EA, 0x10EB, 0x10EC}};
      files->pack_a = restore[m - 1][0];
      files->music = restore[m - 1][1];
      files->overlay = restore[m - 1][2];
    } else {
      files->pack_a = static_cast<s16>(m == 1 ? 0x105C : m == 2 ? 0x10E5 : 0x10E9);
    }
    files->pack_b = -1;
  } else {
    files->container = static_cast<s16>(m + 0xC87);
    files->pack_a = static_cast<s16>(((mask & 0x7C) >> 2) * 2 + 0xC27);
    files->pack_b = static_cast<s16>(b != 0 ? b + 0xC67 : -1);
  }
  if (block[0x1FF] != 0) {  // script override of the party packs
    files->pack_a = static_cast<s16>(rd32(block + 0x1E0));
    files->pack_b = static_cast<s16>(rd32(block + 0x1E4));
  }
  int count = 6;
  if (map == -1) {
    for (int i = 0; i < block[0x1EA] && i < 10; ++i) {
      files->extra[i] = rd16s(block + 0x180 + 0x28 + i * 2);
      ++count;
    }
    if (count - 6 < 10) files->extra[count - 6] = 0;
  } else {
    files->extra[0] = 0;  // the original zeroes word 0xc / 2 = files[6]
  }
  files->count = static_cast<s16>(count);
}

bool sprite_pack_register(const u8* slz, s32 id, int first_slot) {
  int slot = first_slot;
  while (slot < 16 && !g_packs[slot].data.empty()) ++slot;
  if (slot >= 16) return false;
  g_packs[slot].data = unpack(slz, 0);
  g_packs[slot].id = id;
  g_packs[slot].size = rd32(slz + 8);
  return true;
}

void field_load_entry(Field*, int entry, u8* done, int bytes) {
  // A preloaded entry is used where it is (callers look it up with preload_find);
  // otherwise the entry is read into the load buffer through the load queue, waiting
  // for it unless the caller passed a done flag.
  FieldGlobals& g = globals();
  u8* preload_done = nullptr;
  if (preload_find(entry, &preload_done, &g_load_bytes) == nullptr) {
    g_load_bytes = bytes != 0 ? bytes : g.loader->entry_size(entry);
    g_load_buffer.assign(static_cast<size_t>(g_load_bytes), 0);
    if (done == nullptr) {
      u8 local = 0;
      load_queue_push(load_queue(), entry, g_load_buffer.data(), &local, g_load_bytes);
      while (local == 0) load_queue()->update();
    } else {
      load_queue_push(load_queue(), entry, g_load_buffer.data(), done, g_load_bytes);
    }
    return;
  }
  if (g_load_bytes == 0) g_load_bytes = g.loader->entry_size(entry);
  while (*preload_done == 0) load_queue()->update();
  if (done != nullptr) *done = 1;
}

const u8* field_entry_data(int entry) {
  u8* done = nullptr;
  if (auto* pre = preload_find(entry, &done, nullptr)) {
    while (*done == 0) load_queue()->update();
    return pre->data();
  }
  return g_load_buffer.data();
}

int field_require_sprite_pack(Field*, int entry) {
  for (auto& slot : g_packs)
    if (!slot.data.empty() && slot.id == entry) return 1;
  u8* done = nullptr;
  auto* pre = preload_find(entry, &done, nullptr);
  if (pre != nullptr && *done == 0) return 0;
  sprite_pack_register(pre != nullptr ? pre->data() : g_load_buffer.data(), entry, 0);
  g_load_buffer.clear();
  return 1;
}

u32 field_map_sub_sections() { return g_sub_sections; }

void field_load_map_sections(Field*, int container_entry) {
  FieldGlobals& g = globals();
  psx::Gpu& gpu = runtime::Runtime::get().gpu;
  const u8* container = field_entry_data(container_entry);
  const int count = rd32(container);
  for (int s = 0; s < count; ++s) {
    const int id = rd32(container + 4 + s * 8);
    const u8* data = container + rd32(container + 8 + s * 8);
    switch (id) {
      case 0: {
        // Texture directory (block 1), the 256x256 pages, the CLUT rows, then the scene.
        const auto dir = unpack(data, 1);
        const int textures = rd32(dir.data());
        const int cluts = rd32(dir.data() + 4);
        int block = 2;
        for (int t = 0; t < textures; ++t, ++block) {
          const auto page = unpack(data, block);
          const u8 pos = dir[0x10 + t * 8], depth = dir[0x11 + t * 8];
          upload_texture(gpu, reinterpret_cast<const u16*>(page.data()), depth, 0, (pos & 7) << 6 | 0x200, (pos >> 3) << 8,
                         0x100, 0x100);
        }
        if (cluts != 0) {
          auto table = unpack(data, block);
          for (int c = 0; c < cluts; ++c) {
            u8* entry = table.data() + c * 0x204;
            if (entry[0] == 0) {
              for (int bank = 0; bank < 16; ++bank) std::memset(entry + 4 + bank * 0x20, 0, 2);
            } else if (entry[0] == 1) {
              std::memset(entry + 4, 0, 2);
            }
            upload_clut256(gpu, reinterpret_cast<const u16*>(entry + 4), 0, 0x1E0 + c);
          }
        }
        g_map.scene = unpack(data, 0);
        g_map.cells = g_map.scene.data() + rd32(g_map.scene.data() + 0x80);
        g_map.cell_table = g_map.cells + 4;
        bg_build_tpages(bg_renderer());          // overlay 0x80086ad4
        actor_sys_setup_view(actor_system());  // 0x80044394
        g_map.projection_h = rd16s(g_map.scene.data() + 0x6A);
        g_map.actor_scale = rd16s(g_map.scene.data() + 0x68);
        if (g.field_flags[0x24] != 0) actor_sys_restore_all(actor_system());  // 0x800456f0 (handoff 0xb)
        break;
      }
      case 1: {
        g_map.font = unpack(data, 1);
        g_map.glyph_rows = g_map.font.data() + rd32(g_map.font.data()) + 8;
        g_map.script = unpack(data, 0);
        mods_apply_script(container_entry - 0xC87, g_map.script);  // world edits (mods.h)
        if (g_hooks.font_loaded) g_hooks.font_loaded(g_map.font.data());
        if (g_hooks.script_loaded) g_hooks.script_loaded();
        if (g.field_flags[0x24] != 0 && g_script_vm != nullptr) vm_restore_state(g_script_vm);  // 0x80069aac (handoff 0xd)
        break;
      }
      case 2:
        sprite_pack_register(data, -1, 2);
        break;
      case 3:
        if (WorldBg* wbg = world_bg()) world_parse_section3(wbg, data);  // overlay 0xc1e 0x80088a3c
        if (g.field_flags[0x24] != 0) actor_sys_restore_all(actor_system());  // 0x800456f0 (handoff 0xb)
        break;
      default:
        // Sections 5-12 are copies of code overlays and data: 5 #3103 (0x801e9000, the
        // movie player, field_movie.cpp), 6 #3107, 7-10 #3109 / #3108 / #3106 / #3104 and
        // 12 #3105 (0x801f9800), 11 entry #2's data. Their code is native in the port;
        // field_load_map only needs to know which the map carries (0x800756bc ...).
        if (id >= 5 && id <= 12) g_sub_sections |= 1u << id;
        // Section 11 (entry #2): the item records, unpacked when the map loads
        // (0x800756cc) and pointed at by the inventory (+0x820).
        if (id == 11) sub_set_item_records(unpack(data, 0));
        break;
    }
  }
}

void field_load_map(Field* f, int map, bool keep_position, int music_mode) {
  FieldGlobals& g = globals();
  u8* flags = g.field_flags;
  u8* gs = g.game_state;
  preload_free_saved_table();  // 0x800570e8: a menu's copy of the preload table
  field_set_gauge_clock(-1);  // 0x80076310, the menu's query gauge
  if (callways_song_active(callways()) != 0) callways_stop_song(callways(), true);
  field_movie_set_stop(0);     // 0x800762ff
  g.sub_overlay_hook = 0;      // 0x80076279
  g.map_preload_pending = 0;   // 0x800756d9
  field_free_ot_layers();  // particle pool (0x800761a8 / 0x800761ac) and OT layers (0x80053158)
  sub_overlays_map_reset();  // mirror slots (0x800761d4), 0x800761e8, path count 0x800761ea
  world_load_map_reset();  // world globals: regions, leader hit, vehicle models (0x800519d4)
  field_destroy_map_objects();  // 0x80051974: afterimages
  field_side_free();            // the side-loaded entry (0x800762e8; its read cancelled)
  // The load queue stops streaming voices, the battle preloads go (0x8004358c,
  // 0x80056728(1)), and on the right disc the queue's reads finish before the
  // unfinished preloads are dropped (0x80056a3c); on the wrong one it is cleared.
  load_queue_set_streaming(load_queue(), false);
  preload_release(1);
  const int disc = g.config[0x4C] + 1;
  const bool right_disc = g.loader->entry_size(disc + 5) != 0;  // loader_is_disc (boot 0x80011d38)
  if (right_disc) {
    // (Bounded: the ring holds 16 reads, and a read the host loader refuses would spin.)
    for (int n = 0; n < 32 && load_queue_busy(load_queue()); ++n) load_queue()->update();
    preload_drop_incomplete();
  } else {
    load_queue_reset(load_queue());
  }
  if ((g.script_flags[1] >> 6) & 1) music_mode = 0;
  field_music().pending = 0;  // 0x80075744
  // The BG renderer's script switches back to their defaults: parallax follows
  // (0x80076224 = 30000), scrolling windows, animated sprites drawn and windowed
  // (0x80076198 / 0x80076195 / 0x80076194), colour records at full speed (0x8007535c).
  bg_globals().parallax_x = 30000;
  bg_globals().windows_on = 1;
  bg_globals().animate_all = 1;
  bg_globals().animated_on = 1;
  bg_globals().colour_speed = 100;
  flags[0x32] = 2;
  g.frame_ticks = 1;
  for (int p = 0; p < 2; ++p) g.held[p] = g.pressed[p] = g.repeat[p] = 0;
  if (((g.script_flags[0] >> 1) & 1) == 0) {
    gs[0x24] = 1;  // leader: party slot 1 (Rena) ...
    gs[0x25] = 0;
  } else {
    gs[0x24] = 0;  // ... or slot 0 (Claude)
    gs[0x25] = 1;
  }
  if (flags[0x27] != 0) {
    sprite_packs_restore_palettes(f);  // 0x80052838
    flags[0x27] = 0;
  }
  if (flags[0x24] == 0 && flags[0x68] != 0 && world_bg() != nullptr) world_bg_release(world_bg());  // overlay 0x800888e0
  flags[0x68] = static_cast<u32>(map - 1) < 3;
  std::memcpy(gs + 0x1A, &map, 2);
  preload_prune_for_map(map);  // 0x8005711c
  MapFiles files;
  const int which = flags[0x24] == 0 ? map : -1;
  int first_free_pack = 0;
  if (flags[0x68] == 0) {
    map_resolve_entries(&files, which, 0, 0);
    first_free_pack = 2;
  } else {
    // World map: a fresh arrival without a position takes the default spot (camera yaw
    // game_state +0x1e); in a vehicle (script flag bits 11 + 12) the vehicle's spot.
    if (flags[0x24] == 0 && !keep_position) {
      const s32 spot[3] = {0x15026, -0x102, 0x6485};
      std::memcpy(gs + 8, spot, 12);
      const s16 yaw = 0xE98;
      std::memcpy(gs + 0x1E, &yaw, 2);
    }
    if (((g.script_flags[1] >> 3) & 1) != 0 && ((g.script_flags[1] >> 4) & 1) != 0) {
      const bool m1 = rd16s(gs + 0x1A) == 1;
      const s32 spot[3] = {rd32(gs + (m1 ? 0x26C : 0x274)), rd32(gs + (m1 ? 0x28C : 0x290)),
                           rd32(gs + (m1 ? 0x270 : 0x278))};
      std::memcpy(gs + 8, spot, 12);
    }
    map_resolve_entries(&files, which, static_cast<u32>(rd32(gs + 8)), static_cast<u32>(rd32(gs + 0x10)));
  }
  actor_sys_reset_sprite_cache(actor_system());  // 0x80043840
  actor_delete_slot(actor_system(), -1);          // 0x8004541c
  if (MsgSystem* msg = msg_system()) msg_window_destroy(msg, -1);  // 0x800589a0
  g_fade_targets.scene = nullptr;
  g_map.scene.clear();
  g_map.font.clear();
  g_map.script.clear();
  for (int i = first_free_pack; i < 16; ++i) g_packs[i] = SpritePackSlot{};
  bg_destroy();
  glow_destroy_current();   // 0x800752dc: glow_destroy(3)
  world_destroy_objects();  // world BG (vtable delete) and WorldCam (0x800752c4)
  if (CamScript* c = camscript()) camscript_destroy(c, 3);  // 0x800752b4 (field maps make a new one)
  // The state's disc, asked for once the old map is gone (keep_vram 1).
  if (!right_disc) field_prompt_disc_change(f, disc, 1);
  // Music. A loaded game (mode 1) restarts game_state +0x1c; coming back from a battle,
  // its reload list (handoff 0xc) says whether the map music was kept there (-entry,
  // field maps) or the preloaded one plays (mode 2).
  FieldMusic& music = field_music();
  music.current = -1;
  int music_entry = -1;
  if (flags[0x24] != 0) {
    if (auto* list = static_cast<u8*>(handoff_get(g.handoff, 0xC))) {
      const s16 first = rd16s(list);
      if (rd16s(gs + 0x1C) != -1 && flags[0x7F] == 0) {
        if (flags[0x68] == 0) {
          if (first < -1) music_entry = -first;
        } else {
          music_mode = 2;
        }
      }
      music.current = static_cast<s16>(-first);
      delete[] list;
      handoff_clear(g.handoff, 0xC);
    }
  }
  if (music_mode == 1) {
    music_entry = rd16s(gs + 0x1C);
  } else if (music_mode == 2 && flags[0x68] == 0) {
    music_entry = preload_find_music();
  }
  if (flags[0x68] != 0 && music_mode != 0 && ((g.script_flags[1] >> 4) & 1) != 0 && rd16s(gs + 0x1C) != 0xA86)
    music_entry = 0xA86;  // vehicle music
  if (music_entry != -1) {
    field_load_music_sync(f, music_entry);
    if (music.current == -1) music.current = static_cast<s16>(music_entry);
  }

  // Party sprite packs (slots 0 / 1), reloaded only when their entries change.
  const int a = files.pack_a == -1 ? 0 : files.pack_a;
  const int b = files.pack_b == -1 ? 0 : files.pack_b;
  if (flags[0x68] == 0) {
    const bool keep_a = g_packs[0].id == a && !g_packs[0].data.empty();
    const bool keep_b = g_packs[1].id == b && !g_packs[1].data.empty();
    if (!keep_a) {
      g_packs[0] = SpritePackSlot{};
      if (a != 0) {
        field_load_entry(f, a);
        field_require_sprite_pack(f, a);
      }
    }
    if (!keep_b) {
      g_packs[1] = SpritePackSlot{};
      if (b != 0) {
        field_load_entry(f, b);
        field_require_sprite_pack(f, b);
      }
    }
  } else {
    // World maps: the vehicle packs (script flag bits 11 / 15 / 16), then the
    // world party pack (files[3]: sprite packs, the map music, a copy of #3102).
    if (rd16s(gs + 0x1A) < 4) {
      if ((g.script_flags[1] >> 3 & 1) != 0) world_vehicle_load_pack(f, 0x10E3, 1);  // 0x8004a920
      if ((g.script_flags[1] >> 7) != 0) world_vehicle_load_pack(f, 0x10E2, 3);
      if ((g.script_flags[2] & 1) != 0) world_vehicle_load_pack(f, 0x10E4, 4);
    }
    field_load_entry(f, files.pack_a);
  }
  // The code overlay (#3101 field / #3102 world) is native code in the port; only the
  // mode matters.
  g_map.world_map_overlay = flags[0x68] != 0;
  g_map.overlay_loaded = true;
  if (flags[0x68] == 0) {
    bg_construct();       // overlay 0x8008672c (0x128-byte BG renderer)
    bg_helper_create();  // overlay 0x8008e100 (0x17c) and 0x8008e1c0
  } else {
    // field_register_party_sprites (0x8004b5cc) from the world party pack, its music
    // (mode 2: entry 0xa8a on map 1, 0xa89 otherwise), then WorldBg (0x80088814) and
    // WorldCam (0x8007e000).
    const u8* pack = field_entry_data(files.pack_a);
    std::vector<u8> world_music;
    if (flags[0x24] == 0) {
      world_register_party_sprites(pack + rd32(pack));
      world_music.assign(pack + rd32(pack + 4), pack + rd32(pack + 8));
    } else {
      world_register_party_sprites(pack);
      preload_remove(files.pack_a);
      field_load_entry(f, files.music);
      const u8* m = field_entry_data(files.music);
      world_music.assign(m, m + g.loader->entry_size(files.music));
    }
    if (music_mode == 2 && ((g.script_flags[1] >> 4) & 1) == 0) {
      g_load_buffer = world_music;
      field_load_music_sync(f, 0);
      const s16 entry = static_cast<s16>(rd16s(gs + 0x1A) == 1 ? 0xA8A : 0xA89);
      std::memcpy(gs + 0x1C, &entry, 2);
    }
    g_load_buffer.clear();
    preload_remove(files.pack_a);
    world_create_objects();
  }
  // The map container. The previous map's embedded code areas are released first
  // (0x800756b8: 0x801e9000).
  fieldactor_globals().shadow_boxes = 0;
  g_sub_sections = 0;
  field_start_pending_music(false);  // 0x80052bf4
  field_load_entry(f, files.container);
  field_load_map_sections(f, files.container);
  preload_release(-1);
  g_load_buffer.clear();
  field_start_pending_music(true);
  if (flags[0x24] != 0) {
    if (flags[0x68] == 0) {
      bg_resume(bg_renderer());
    } else if (WorldBg* wbg = world_bg()) {
      world_bg_resume(wbg);  // vtable[4] 0x8008af54
    }
  }
  // Section 5: #3103 unpacked to 0x801e9000 (0x15000 reserved). While it is there,
  // actor shadows are drawn as boxes, which its compose can blend into movie frames.
  if (g_sub_sections & (1u << 5)) fieldactor_globals().shadow_boxes = 1;

  // Leader placement.
  if (flags[0x24] == 0) {
    flags[0x91] = flags[0x83] = flags[0x80] = flags[0x81] = flags[0x82] = 0;
    s32 pos[3] = {rd32(gs + 8), rd32(gs + 0xC), rd32(gs + 0x10)};
    u8 brightness = gs[0x21];
    int angle = (rd16s(gs + 0x18) * 360) / 4096;
    if (!keep_position) {
      brightness = 0x80;
      angle = 90;
      if (flags[0x68] == 0) {
        const u8* scene = g_map.scene.data();
        const u16 zones = rd16(g_map.cell_table + 0x1C);  // cell 0, list 14
        if (zones == 0) {
          pos[0] = rd32(scene + 0x14);
          pos[1] = rd16s(scene + 0x58);
          pos[2] = rd32(scene + 0x1C);
        } else {
          // The default entry zone ('~'), else the first one: the centroid of its
          // first three corners.
          const u8* list = g_map.cells + rd32(g_map.cell_table + 0x58);
          int pick = 0;
          for (int i = 0; i < zones; ++i) {
            if (list[i * 0x58 + 0xD] == '~') {
              pick = i;
              break;
            }
          }
          const u8* z = list + pick * 0x58;
          pos[0] = rd16s(z + 0x10);
          pos[1] = rd16s(z + 0x12);
          pos[2] = rd16s(z + 0x14);
          for (int k = 1; k < 3; ++k) {
            pos[0] += rd16s(z + 0x10 + k * 8);
            pos[1] += rd16s(z + 0x12 + k * 8);
            pos[2] += rd16s(z + 0x14 + k * 8);
          }
          pos[0] /= 3;
          pos[1] /= 3;
          pos[2] /= 3;
        }
      }
    }
    const int leader = gs[0x24];
    if (flags[0x68] == 0 || ((g.script_flags[1] >> 4) & 1) == 0) {
      actor_create(actor_system(), leader, leader, pos, angle, brightness, 0xFFFF, 0xFFFF, 0x1000F);
      actor_or_flags(field_actor(leader), 0, 0x200004);
    }
    const s16 none = -1;
    std::memcpy(flags + 0x10, &none, 2);
    if (flags[0x68] == 0) {
      Actor* a_leader = field_actor(leader);
      BgRenderer* bg = bg_renderer();
      if (a_leader && bg) {
        bg->focus_x = actor_screen_x(a_leader);
        bg->focus_y = static_cast<s16>(actor_screen_y(a_leader) - 0x10);
        bg_update_scroll(bg);  // overlay 0x800867d8
        bg->proportional_x = 0;
        bg->proportional_y = 0;
        bg->forced_x = 30000;
        bg->forced_y = 30000;
        bg->vertical_scroll = rd16(g_map.scene.data() + 6) > 0xF0;
      }
    } else {
      // The vehicles: actor 0xd (vehicle 1, script flag 11) at its spot, or the leader's
      // slot when riding it (flag 12: camera pitch 18, the camera 50 units higher);
      // actor 0xe (vehicle 4, flag 16) at game_state +0x268 / +0x298 / +0x29c, angle +0x28a.
      if ((g.script_flags[1] >> 3 & 1) != 0) {
        const bool riding = (g.script_flags[1] >> 4 & 1) != 0;
        const int slot = riding ? leader : 0xD;
        const bool m1 = rd16s(gs + 0x1A) == 1;
        s32 vpos[3] = {rd32(gs + (m1 ? 0x26C : 0x274)), rd32(gs + (m1 ? 0x28C : 0x290)), rd32(gs + (m1 ? 0x270 : 0x278))};
        actor_create(actor_system(), slot, slot, vpos, angle, brightness, 0xFFFF, 0xFFFF, 0x1000F);
        if (Actor* v = field_actor(slot)) {
          v->vehicle = 1;
          if (!riding) {
            world().cam_pitch_deg = 0x15;
          } else {
            world().view.geom_ofs_y = -50;
            world().cam_pitch_deg = 0x12;
            if (is_world_actor(v)) {
              WorldActor* wv = static_cast<WorldActor*>(v);
              wv->throttle = wv->climb = wv->vel_430 = 0;
            }
          }
        }
      }
      if ((g.script_flags[2] & 1) != 0) {
        s32 vpos[3] = {rd32(gs + 0x268), rd32(gs + 0x298), rd32(gs + 0x29C)};
        actor_create(actor_system(), 0xE, 0xE, vpos, rd16s(gs + 0x28A), brightness, 0xFFFF, 0xFFFF, 0x1000F);
        if (Actor* v = field_actor(0xE)) v->vehicle = 4;
      }
    }
  } else {
    // Back from another program: fade in (unless a script holds the screen), and
    // re-apply the walk-group switches saved in field_flags +0x89.
    if (flags[0x7F] == 0 || (read_u16(g.config + 0x178) == 2 && flags[0x7E] != 0))
      fade_push(field_fade(), 1, 0x14, 0, 0);
    for (int i = 0; i < static_cast<s8>(flags[0x91]); ++i)
      walk_set_group_enabled(flags[0x89 + i] & 0x7F, (flags[0x89 + i] & 0x80) == 0);  // overlay 0x8008df74
  }
  // World map: the camera yaw (game_state +0x1e) and centre (the leader's spot).
  if (WorldBg* wbg = world_bg(); wbg != nullptr && flags[0x68] != 0) {
    world_bg_set_angle(wbg, rd16s(gs + 0x1E));
    world_bg_set_centre(wbg, rd32(gs + 8), rd32(gs + 0x10));
  }
  if (flags[0x7F] == 0) {
    flags[0x26] = 0;
    const u16 leader = gs[0x24];
    std::memcpy(flags + 0xE, &leader, 2);
  } else {
    const u16 zero = 0;
    std::memcpy(gs + 0x1C, &zero, 2);
  }
  flags[0x24] = 0;
  g.battle_preloaded = 0;
  g.battle_preload_requested = 0;
  // The step counter (field_flags +0x18, 20.12) restarts at 0 without encounters and is
  // capped at 30000 steps; either way the next poison tick is due at once.
  if (flags[0x25] == 0 || rd32(flags + 0x18) < 0) {
    std::memset(flags + 0x18, 0, 4);
    g.poison_next_step = 1;
  } else if (rd32(flags + 0x18) > 0x7530000) {
    const s32 cap = 0x7530000;
    std::memcpy(flags + 0x18, &cap, 4);
    g.poison_next_step = 1;
  }
  g.field_active = 1;
  bg_globals().draw_enabled = 1;  // 0x80075354 (scene loaded)
  g_fade_targets.scene_loaded = 1;
  g_fade_targets.scene = g_map.scene.data();
  g.frame_ticks = 1;
  std::memset(flags + 0x1C, 0, 4);
  if (Notice* n = g_notice_object()) n->state = 0;
  g_load_buffer.clear();
  g_fade_targets.bg_tint_active = 0;  // 0x80075356
  psyq::ResetGraph(1);
  Screen* screen = field_screen();
  // Not while a fade holds the screen (white hold, or a captured frame on show), nor
  // when the previous map ran embedded code (overlay A at 0x801e9000).
  if (ScreenFade* fade = field_fade(); fade == nullptr || (fade->hold != 1 && fade->phase == 0)) field_setup_screen(f);
  if (flags[0x68] == 0) {
    if (Actor* l = field_actor(gs[0x24])) actor_and_flags(l, 0, 0xFEFFFFFF);
    g.vsync_mode = 0;
    screen_layout_arena(screen, 1);
    const u8* scene = g_map.scene.data();
    screen_set_clear_colour(screen, scene[0x6F], scene[0x70], scene[0x71]);
    fieldactor_globals().screen_clamp = screen_clamp_flag(scene, rd16s(gs + 0x1A));  // 0x80055c08
  } else {
    if (Actor* l = field_actor(gs[0x24])) actor_or_flags(l, 0, 0x1000000);  // smoothed turning
    g.vsync_mode = 2;
    // Debugging: the console rarely makes 30 fps here; PCSX-Redux runs the world map at
    // 20 (frame_ticks 3), and the follow camera's turn acceleration depends on the frame
    // rate. SS_WORLD_VSYNC=3 reproduces that pacing for reference comparisons.
    if (const char* v = std::getenv("SS_WORLD_VSYNC"); v != nullptr && std::atoi(v) > 0)
      g.vsync_mode = static_cast<u8>(std::atoi(v));
    screen_layout_arena(screen, 0);
    screen_set_clear_colour(screen, 0, 0, 0);
  }
  for (int i = 0; i < 2; ++i) {
    screen_flip(screen);
    screen_clear_ot(screen);
  }
  g.disc_error = 0;
  flags[0x7F] = 0;
  actor_sys_update_one(actor_system(), gs[0x24]);  // 0x80044318
  if (read_u16(g.config + 0x178) == 2 && flags[0x7E] != 0) {
    // A lost script battle that ends the game.
    field_begin_game_over(f);
  } else if (((g.script_flags[1] >> 6) & 1) != 0) {
    // Riding the bunny (script flag 14): its music and sprite pack again (state 0x11).
    field_world_bunny_reload(f);
  }
  g_load_buffer.clear();
  g.scripted_battle = 0;
}

}  // namespace ss::game::field
