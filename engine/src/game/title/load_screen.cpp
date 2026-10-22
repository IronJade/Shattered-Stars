#include "game/title/load_screen.h"

#include <cstring>

#include "disc/slz.h"
#include "game/boot/system.h"
#include "game/common/memcard.h"
#include "game/common/menu_save.h"
#include "game/common/menu_scene.h"
#include "runtime/runtime.h"

namespace ss::game {

// title 0x800317e0 stores the three buffers' addresses, the program entry and its
// destination in the card manager (+0xff0..+0x1000) and calls 0x8003186c, ported here.
int load_game_screen(MemoryCard* card, std::vector<u8>& data) {
  runtime::Runtime::get().trace("load_game_screen");
  menu::set_host(menu::Host::Title);
  Loader* loader = menu::menu_loader();
  menu::MenuGlobals& g = menu::globals();
  g.config = static_cast<u8*>(handoff_get(menu::menu_handoff(), 2));
  while (loader->poll() == 1) {
  }
  const size_t size0 = static_cast<size_t>(loader->entry_size(0xBD0));
  const size_t size1 = static_cast<size_t>(loader->entry_size(0xBD1));
  // Window art (TIMs) and the page data; the party sprite pack stays resident.
  if (auto res = disc::slz::unpack_block(data, 0, 0, nullptr)) g.res = std::move(*res);
  if (auto page = disc::slz::unpack_block(data, size0, 0, nullptr)) g.data = std::move(*page);
  if (data.size() > size0 + size1) menu::entry_cache_load_0bd2(data.data() + size0 + size1);
  data.clear();
  data.shrink_to_fit();
  int result = 0;
  {
    menu::MenuSession session;
    session.cached_0bd2 = 1;
    session.start_page = 0x11;
    session.load_mode = 1;
    std::memset(g.config + 0x198, 0, 8);  // no last-used position: the newest save is picked
    {
      menu::MenuScene scene(card);
      scene.external_memcard = 1;
      scene.memcard = card;
      // The console queues the field program (#3100) on a BgLoadClient here and waits
      // for it after a load; the port runs the field natively.
      result = scene.run();
    }
  }
  return result;
}

int load_save_without_menu(MemoryCard* card, int slot, int index) {
  menu::set_host(menu::Host::Title);
  return menu::load_save_direct(card, slot, index);
}

}  // namespace ss::game
