#include "game/common/menu_scene.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "disc/slz.h"
#include "game/boot/pad.h"
#include "game/common/memcard.h"
#include "game/common/menu_gfx.h"
#include "game/common/menu_save.h"
#include "game/common/menu_widget.h"
#include "runtime/libgpu.h"
#include "runtime/libgte.h"
#include "runtime/runtime.h"

namespace ss::game::menu {
namespace {

PageHandler g_page_handlers[0x15] = {};
void (*g_frame_hook)() = nullptr;

// title 0x8004ae2c / field 0x8006ff3c: GTE setup shared with the New Game screens
// (ZSF3/4, H, DQA/B, screen offset 0).
void gte_setup() {
  psx::Gte& g = psyq::gte();
  g.write_ctrl(psx::Gte::ZSF3, 0x155);
  g.write_ctrl(psx::Gte::ZSF4, 0x100);
  g.write_ctrl(psx::Gte::H, 1000);
  g.write_ctrl(psx::Gte::DQA, 0xFFFFEF9Eu);
  g.write_ctrl(psx::Gte::DQB, 0x1400000);
  g.write_ctrl(psx::Gte::OFX, 0);
  g.write_ctrl(psx::Gte::OFY, 0);
}

}  // namespace

// ==== Session =========================================================================

MenuSession::MenuSession() {
  MenuGlobals& g = globals();
  g.session = this;
  heap_free = 0;  // heap statistics (boot 0x80013428)
  HandoffRegistry* h = menu_handoff();
  g.config = static_cast<u8*>(handoff_get(h, 2));
  g.inventory = static_cast<u8*>(handoff_get(h, 6));
  g.party = static_cast<u8*>(handoff_get(h, 3));
  g.records = static_cast<u8*>(handoff_get(h, 4));
  party_bind(g.config, g.inventory, g.party, g.records);
  if (handoff_get(h, 0xE) == nullptr) handoff_store(h, new u8[0x440](), 0xE);
  build_button_map();
  g.item_data.clear();
}

MenuSession::~MenuSession() {
  MenuGlobals& g = globals();
  if (inventory_dirty && g.inventory) g.inventory[0xC24] = 1;
  free_data();
  free_cache();
  if (result == 0) {
    HandoffRegistry* h = menu_handoff();
    delete[] static_cast<u8*>(handoff_get(h, 0xE));
    handoff_clear(h, 0xE);
  }
  g.session = nullptr;
}

void MenuSession::free_data() {
  globals().item_data.clear();
  globals().item_table = nullptr;
  if (cached_0bbb) {
    entry_cache_free_0bbb();
    cached_0bbb = 0;
  }
  buffer.clear();
}

void MenuSession::free_cache() {
  if (cached_0bd2) {
    entry_cache_free_0bd2();
    cached_0bd2 = 0;
  }
}

void MenuSession::build_button_map() {
  for (int i = 15; i >= 0; --i) button_map[i] = static_cast<u8>(i);
  // Default order of the buttons in the icon sheet (title 0x8004b410 / field 0x800717b0).
  const u8* defaults = table(0x8004B410, 0x800717B0);
  for (int i = 0; i < 8; ++i) {
    for (int j = 0; j < 8; ++j) {
      if (defaults && static_cast<s16>(config_get_button(i)) == static_cast<s16>(read_u16(defaults + j * 2)))
        button_map[i] = static_cast<u8>(j);
    }
  }
}

// ==== Scene ============================================================================

MenuScene::MenuScene(MemoryCard* card) {
  MenuGlobals& g = globals();
  cvram_merge_free();
  g.scene = this;
  if (g.session) g.session->scene = this;
  cvram_init();
  bgload_init();
  sprite_cache_init();
  pad_set_port_mask(menu_pad(), 3);
  pad_set_repeat_delay(menu_pad(), 0xC);
  gte_setup();
  input_off = 0;
  external_memcard = 0;
  memcard = card;
  ctx = new RenderContext;
  render_ctx_create_arena(ctx, 60000);
  render_ctx_create_ot(ctx, 0xF);
  windows = new WindowList;
  screen_setup_buffer0(0, 0, 0x140, 0xF0, 0xC, 0xC, 4, 4);
  screen_setup_buffer1(0, 0xD8, 0x140, 0xF0, 0xC, 0xC, 4, 4);
  for (int i = 0; i < 2; ++i) {
    psyq::DRAWENV* d = screen_drawenv(i);
    d->isbg = 1;
    d->dtd = 1;
    d->r0 = d->g0 = d->b0 = 0;
  }
  // Borrow VRAM: the field's icons move aside, the CLUT rows and the icon page are kept.
  const psyq::RECT menu_page{0x180, 0x100, 0x40, 0x100};
  psyq::MoveImage(&menu_page, 0x140, 0x100);
  saved_cluts.assign(0x1400, 0);
  const psyq::RECT cluts{0, 0x1E0, 0x140, 0x10};
  psyq::StoreImage(&cluts, saved_cluts.data());
  saved_icons.assign(0x2000, 0);
  const psyq::RECT icons{0x200, 0x100, 0x40, 0x80};
  psyq::StoreImage(&icons, saved_icons.data());
  // Window art: TIM 0 64x128 at (0x180,0x100), TIM 1 64x48 at (0x180,0x1c8). (The port had
  // TIM 0 as 64x256, the moved page's rect, and read 0x25e0 bytes past the 23624-byte pack.)
  for (int i = 0; i < 2; ++i) upload_tim(i, kWindowArt[i].pixels, kWindowArt[i].clut);
  // Two greyed palettes per character row (x 0x10 and 0x20) from rows 0x1ec / 0x1ed.
  for (int i = 0; i < 2; ++i) {
    for (int j = 0; j < 0xC; ++j) {
      const psyq::RECT row{0x100, static_cast<s16>(i + 0x1EC), 0x10, 1};
      psyq::MoveImage(&row, (i + 1) * 0x10, j + 0x1E4);
    }
  }
  g.res.clear();
  g.res.shrink_to_fit();
  screen_drawenv(0)->tpage = 0x16;
  screen_drawenv(1)->tpage = 0x16;
  party_select(0);  // menu_scene_select_member (title 0x800305a4 / field 0x8002ffbc)
}

MenuScene::~MenuScene() {
  MenuGlobals& g = globals();
  psyq::ResetGraph(1);
  sprite_cache_release();
  screen_destroy_arena(ctx->arena);
  screen_destroy_ot(ctx->ot);
  delete ctx;
  delete windows;
  windows = nullptr;
  delete pages;
  pages = nullptr;
  if (owns_memcard && memcard) memcard_destroy(memcard);
  deferred_delete_update();
  deferred_delete_update();
  bgload_shutdown();
  cvram_release();
  g.data.clear();
  g.data.shrink_to_fit();
  psyq::ResetGraph(1);
  psyq::SetDispMask(0);
  const psyq::RECT moved{0x140, 0x100, 0x40, 0x100};
  psyq::MoveImage(&moved, 0x180, 0x100);
  const psyq::RECT cluts{0, 0x1E0, 0x140, 0x10};
  psyq::LoadImage(&cluts, saved_cluts.data());
  const psyq::RECT icons{0x200, 0x100, 0x40, 0x80};
  psyq::LoadImage(&icons, saved_icons.data());
  psyq::DrawSync(0);
  cvram_merge_free();
  if (g.scene == this) g.scene = nullptr;
}

bool MenuScene::upload_tim(int index, const psyq::RECT& rect, const s16 clut[4]) {
  const std::vector<u8>& pack = globals().res;
  // Port: the pack is a host buffer of its unpacked size; a TIM whose pixels (+0x220) or
  // CLUT rows (+0x14) would run past it (a short or modded pack) is not uploaded. The
  // original has no check; the shipped packs always hold both.
  const size_t pixels = static_cast<size_t>(std::max<int>(rect.w, 0)) * std::max<int>(rect.h, 0) * 2;
  const size_t rows = static_cast<size_t>(std::max<int>(clut[3], 0)) * 0x20;
  const size_t at = pack.size() >= static_cast<size_t>(index + 1) * 4 ? read_u32(pack.data() + index * 4) : SIZE_MAX;
  if (at > pack.size() || pack.size() - at < std::max(0x220 + pixels, 0x14 + rows)) {
    std::fprintf(stderr, "[menu] window-art TIM %d does not fit the %zu-byte pack; not uploaded\n", index, pack.size());
    return false;
  }
  const u8* tim = pack.data() + at;
  psyq::LoadImage(&rect, tim + 0x220);
  const u8* pal = tim + 0x14;
  for (int i = 0; i < clut[3]; ++i, pal += 0x20) {
    const psyq::RECT r{clut[0], static_cast<s16>(clut[1] + i), 0x10, 1};
    psyq::LoadImage(&r, pal);
  }
  return true;
}

int MenuScene::run() {
  pages = new MenuPageLoader;
  if (external_memcard == 0) {
    memcard = memcard_create();
    owns_memcard = true;
  }
  frames = 0;
  upload_queue_init();
  vsync_mode = 0;
  sprite_cache_clear();
  ctx->ot_head = screen_clear_ot(ctx->ot);
  for (;;) {
    frame_begin();
    if (pages->update() != 0) break;
    windows->update();
    frame_end();
  }
  upload_queue_release();
  return globals().session ? globals().session->result : 0;
}

void MenuScene::frame_begin() {
  if (frames == 2) psyq::SetDispMask(1);
  screen_flip();
  deferred_delete_update();
  ctx->ot_head = screen_clear_ot(ctx->ot);
  if (g_frame_hook) g_frame_hook();
  async_load_queue_update();
  if (input_off == 0) {
    held = menu_input_lowest_bit(pad_current(menu_pad(), 0));
    repeat = menu_input_lowest_bit(pad_pressed_repeat(menu_pad(), 0));
    pressed = menu_input_lowest_bit(pad_pressed(menu_pad(), 0));
  } else {
    held = repeat = pressed = 0;
  }
  u8& lock = globals().input_lock;
  if (lock == 0) {
    if ((held & 0xFF) != 0) lock = 4;
  } else {
    --lock;
    held = repeat = pressed = 0;
  }
  pad_clear(menu_pad(), 0);
  sprite_cache_new_frame();
}

void MenuScene::frame_end() {
  if (memcard) memcard_driver_update(memcard);
  psyq::DrawSync(0);
  upload_queue_flush();
  int n = 0;
  switch (vsync_mode) {
    case 1: n = 1; break;
    case 2: n = 2; break;
    case 3: n = 10; break;
    case 4: n = 0x3C; break;
    default: n = 0; break;
  }
  vsync_result = psyq::VSync(n);
  ++frames;
  screen_put_env();
  screen_draw_ot(ctx->ot);
}

// ==== Page loader ==========================================================================

MenuPageLoader::MenuPageLoader() {
  page = 0;
  depth = 0;
  read_buf.clear();
  entry = 0;
}

MenuPageLoader::~MenuPageLoader() = default;

void MenuPageLoader::request(int p) {
  // Archive entry pair (page code, page data) for each page (title 0x8003549c).
  int e = 0;
  switch (p) {
    case 1: e = 0xBA6; break;
    case 2: e = 0xBBC; break;
    case 3: e = 0xBBE; break;
    case 4: e = 0xBC4; break;
    case 5: e = 0xBC6; break;
    case 6: e = 0xBB2; break;
    case 7: e = 0xBC8; break;
    case 8: case 9: case 10: e = 0xBCA; break;
    case 11: case 12: case 13: e = 0xBAA; break;
    case 14: e = 0xBCE; break;
    case 15: {
      const s8 v = globals().session ? globals().session->variant : 0;
      e = (v == 4 || v == 0xB) ? 0xBAE : 0xBC0;
      break;
    }
    case 16: e = 0xBC2; break;
    case 17: e = 0xBB6; break;
    case 19: e = 0xBCC; break;
    default: break;
  }
  if (e == 0) return;
  Loader* loader = menu_loader();
  code_size = loader->entry_size(e);
  data_size = 0;
  data_size = loader->entry_size(e | 1);
  if (loads_on == 0) return;
  if (entry != 0 && entry != e) {
    read_buf.assign(static_cast<size_t>(code_size + data_size), 0);
    bgload_queue_bytes(&client, e, read_buf.data(), code_size + data_size);
  }
  entry = e;
}

int MenuPageLoader::update() {
  MenuScene* s = globals().scene;
  MenuTask* focus = s->windows->focus();
  bool ready = false;
  if (focus == nullptr) {
    if (loads_on == 0) {
      ready = true;
    } else if (bgload_owner_done(&client)) {
      bgload_cancel_owner(&client);
      ready = true;
      if (!read_buf.empty()) {
        // Page code (native in the port) and its data block: the data replaces g_menu_data.
        if (auto code = disc::slz::unpack_block(read_buf, 0, 0, nullptr)) code_dest = std::move(*code);
        set_page_code_entry(entry);  // its constants are read from the entry
        if (data_size != 0) {
          auto data = disc::slz::unpack_block(read_buf, static_cast<size_t>(code_size), 0, nullptr);
          globals().data = data ? std::move(*data) : std::vector<u8>{};
        }
        read_buf.clear();
      }
    }
  } else {
    if (focus != current) return 0;
    const u16 state = current->result_state;
    if (state == 0) return 0;
    choice = current->result;
    substate = static_cast<s16>(state);
    current = nullptr;
    ready = true;
  }
  if (!ready) return 0;
  if (page == 0x14) return 1;
  if (page >= 0 && page < 0x14 && g_page_handlers[page] != nullptr) {
    g_page_handlers[page](this);
    return 0;
  }
  switch (page) {
    case 0: menu_page00_start(this); return 0;
    case 0x11: menu_page11_update(this); return 0;
    default:
      // The title's other page handlers are empty; the field's not ported yet go back to
      // the page below (as if the page had opened and been cancelled at once).
      runtime::Runtime::get().log_once("menu_page" + std::to_string(page),
                                       "menu: page " + std::to_string(page) + " is not ported yet");
      if (substate == 1) {
        depth = static_cast<s16>(depth - 1);
        if (depth < 0) {
          depth = 0;
          goto_page(0x14);
        } else {
          goto_page(stack[depth].id);
        }
      }
      return 0;
  }
}

void set_page_handler(int page, PageHandler handler) {
  if (page >= 0 && page < 0x15) g_page_handlers[page] = handler;
}

void set_menu_frame_hook(void (*hook)()) { g_frame_hook = hook; }

void MenuPageLoader::goto_page(s16 p) {
  page = p;
  substate = 1;
  request(page);
}

void MenuPageLoader::pop() {
  depth = static_cast<s16>(depth - 2);
  s16 next;
  if (depth == -1) {
    depth = 0;
    next = 0x14;
  } else {
    next = stack[depth].id;
  }
  goto_page(next);
}

void MenuPageLoader::publish_id() {
  if (MenuSession* ss = globals().session) ss->page_id = static_cast<s8>(page);
}

void MenuPageLoader::push(Widget* w) {
  current = w;
  stack[depth].widget = w;
  stack[depth].id = page;
  substate = 0;
  ++depth;
}

void menu_page00_start(MenuPageLoader* p) {
  p->loads_on = 1;
  p->goto_page(globals().session->start_page);
}

void menu_page11_update(MenuPageLoader* p) {
  switch (p->substate) {
    case 1:
      p->publish_id();
      p->push(new SaveMenu);
      break;
    case 2:
    case 3:
      p->pop();
      break;
    default:
      break;
  }
}

}  // namespace ss::game::menu
