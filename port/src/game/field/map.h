#pragma once

#include <functional>
#include <vector>

#include "core/types.h"
#include "game/field/field.h"

// Map loading (field_load_map 0x80053e8c and its helpers): the map's files (container,
// party sprite packs, code overlay), the container's sections (0 scene and background,
// 1 script and font, 2 sprites, 3 world terrain, 5+ embedded code) and the leader's
// placement. Formats: docs/port/catalog/formats.md "Map container".
namespace ss::game::field {

// map_resolve_entries (0x80061888) output: s16 files[17].
struct MapFiles {
  s16 music = -1;      // [0] music to restore, or -1
  s16 system = 0;      // [1] 0xc26
  s16 container = 0;   // [2] 0xc87 + map (field) or a world chunk
  s16 pack_a = 0;      // [3] party sprite pack A (0xc27 + 2m)
  s16 pack_b = -1;     // [4] party sprite pack B (0xc67 + m) or -1
  s16 overlay = 0;     // [5] code overlay 0xc1d / 0xc1e
  s16 extra[10]{};     // [6..] restore-only preload list
  s16 count = 0;       // [0x10]
};
void map_resolve_entries(MapFiles* files, int map, u32 x, u32 z);  // 0x80061888

// The current map's data (globals 0x80075330-0x80075718).
struct MapData {
  std::vector<u8> scene;           // 0x80075330: MapScene (section 0 block 0)
  u8* cells = nullptr;             // 0x80075338: cell grid (scene + scene[0x80])
  u8* cell_table = nullptr;        // 0x80075334: cells (grid + 4)
  std::vector<u8> font;            // 0x80075714: map font (section 1 block 1)
  const u8* glyph_rows = nullptr;  // 0x80075718
  std::vector<u8> script;          // 0x800756f8: event script (section 1 block 0)
  s16 projection_h = 0;            // 0x80075764: scene +0x6a
  s16 actor_scale = 0;             // 0x80075766: scene +0x68
  bool world_map_overlay = false;  // 0x80075358: overlay in place is the world one (field_flags+0x68)
  bool overlay_loaded = false;
};
MapData& map_data();

// Sprite pack slots (0x8007585c: 16 x {void *data; int id; int size}).
struct SpritePackSlot {
  std::vector<u8> data;
  s32 id = 0;
  s32 size = 0;
};
SpritePackSlot* sprite_pack_slots();
bool sprite_pack_register(const u8* slz, s32 id, int first_slot);  // 0x80053940

// The field's load buffer (0x80075738 / size 0x8007573c) and blocking loads.
std::vector<u8>& field_load_buffer();
// 0x8004a654: reads 'entry' into the load buffer through the load queue (waiting
// unless 'done' is given), or waits for its preload. 'bytes' 0 = the whole entry.
void field_load_entry(Field* f, int entry, u8* done = nullptr, int bytes = 0);
// The data of an entry field_load_entry fetched: its preload, else the load buffer.
const u8* field_entry_data(int entry);
int field_require_sprite_pack(Field* f, int entry);        // 0x8004a830: 1 = registered
void field_load_map_sections(Field* f, int entry);         // 0x8004aca0
// Embedded code / data sections (5-12) the current map's container carries, bit n for
// section n (field_load_map_sections records them).
u32 field_map_sub_sections();

// 0x80053e8c: loads map 'map'. 'keep_position' keeps the saved leader position
// (else the map's default spawn); 'music_mode' 0 keep, 1 game_state music, 2 preloaded.
void field_load_map(Field* f, int map, bool keep_position, int music_mode);

// Subsystems not in this file attach here (script VM: section 1 loaded; message
// system: map font changed).
struct MapHooks {
  std::function<void()> script_loaded;       // vm_init(ctx) after section 1
  std::function<void(const u8*)> font_loaded;
};
MapHooks& map_hooks();

}  // namespace ss::game::field
