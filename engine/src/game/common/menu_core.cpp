#include "game/common/menu_core.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "disc/slz.h"
#include "game/common/program_image.h"
#include "runtime/runtime.h"

namespace ss::game::menu {
namespace {

Host g_host = Host::Title;
MenuGlobals g_menu;
int g_page_code_entry = 0;

// Entry cache slots (title 0x8004ddd4: {u32 ptr, u32 entry, u32 size} x 16).
struct CacheSlot {
  std::vector<u8> data;
  int entry = 0;
};
CacheSlot g_cache[16];
EntryCacheSlotRef (*g_cache_table)(int index) = nullptr;

EntryCacheSlotRef cache_slot(int index) {
  if (g_cache_table) return g_cache_table(index);
  return {&g_cache[index].data, &g_cache[index].entry};
}

// Background load state (title 0x8004efb8-0x8004efd0).
struct BgLoad {
  s16 state = 0;                     // 0x8004efb8
  s16 capacity = 0;                  // 0x8004efbc
  s16 current = 0;                   // 0x8004efc0
  std::vector<LoadRequest> requests; // 0x8004efc4
  s32 timer = 0x3C;                  // 0x8004efd0
} g_bg;

}  // namespace

void set_host(Host h) { g_host = h; }
Host host() { return g_host; }

const u8* table(u32 title_address, u32 field_address) {
  if (g_host == Host::Title) return program_data(static_cast<int>(Program::Title), title_address);
  if (field_address == 0) {
    runtime::Runtime::get().log_once("menu_table", "menu: a constant table has no field address yet");
    return nullptr;
  }
  return field_table(field_address);
}

const u8* field_table(u32 address) {
  // The library overlay, the page code overlay, else the field program.
  if (address >= kLibraryBase && address < kLibraryEnd) return program_data_at(kLibraryEntry, kLibraryBase, address);
  if (address >= kPageCodeBase && address < kLibraryBase) {
    if (g_page_code_entry == 0) return nullptr;
    return program_data_at(g_page_code_entry, kPageCodeBase, address);
  }
  return program_data(static_cast<int>(Program::Field), address);
}

const u8* page_table(int entry, u32 address) { return program_data_at(entry, kPageCodeBase, address); }

void set_page_code_entry(int entry) { g_page_code_entry = entry; }
int page_code_entry() { return g_page_code_entry; }

void set_entry_cache_table(EntryCacheSlotRef (*slot)(int index)) { g_cache_table = slot; }

MenuGlobals& globals() { return g_menu; }
System* menu_system() { return get_system(); }
HandoffRegistry* menu_handoff() { return &get_system()->handoff; }
Loader* menu_loader() { return &get_system()->loader; }
Sound* menu_sound() { return &get_system()->sound; }
Pad* menu_pad() { return &get_system()->pad; }

// ---- WindowList ------------------------------------------------------------------

WindowList::~WindowList() {
  close_all();
  purge();
  compact();
}

void WindowList::add(MenuTask* task, int layer) {
  append(WindowEntry{task, layer, 1});
  focus_ = task;
}

int WindowList::set_unfocusable(MenuTask* task) {
  for (auto& e : entries) {
    if (e.task == task) {
      e.flags |= 0x10;
      update_focus();
      return 1;
    }
  }
  return 0;
}

void WindowList::update() {
  // Every active entry, layer by layer; entries added meanwhile join the walk.
  for (int layer = 0; layer < 5; ++layer) {
    for (size_t i = 0; i < entries.size(); ++i) {
      if (entries[i].layer == layer && (entries[i].flags & 1)) entries[i].task->update();
    }
  }
  u32 all = 0;
  for (const auto& e : entries) all |= e.flags;
  if (all & 4) purge();
  all = 0;
  for (const auto& e : entries) all |= e.flags;
  if (all & 8) compact();
}

void WindowList::close_type(int type) {
  for (size_t i = 0; i < entries.size(); ++i) {
    if (!(entries[i].flags & 1)) continue;
    MenuTask* t = entries[i].task;
    if (type != -1 && t->type() != type) continue;
    entries[i].flags |= 2;
    t->close();
  }
}

void WindowList::close_all() {
  for (size_t i = 0; i < entries.size(); ++i) {
    if (entries[i].flags & 1) entries[i].task->on_close();
  }
}

int WindowList::kill(MenuTask* task) {
  for (auto& e : entries) {
    if (e.task == task) {
      e.flags = (e.flags & ~1u) | 4;
      return 1;
    }
  }
  return 0;
}

void WindowList::purge() {
  for (size_t i = 0; i < entries.size(); ++i) {
    if (!(entries[i].flags & 4)) continue;
    MenuTask* t = entries[i].task;
    entries[i].flags = (entries[i].flags & ~4u) | 8;
    delete t;  // vtable[1] with flags 3
  }
}

int WindowList::depth_of(MenuTask* task) const {
  int depth = 1;
  for (int layer = 0; layer < 5; ++layer) {
    for (const auto& e : entries) {
      if (e.layer != layer || !(e.flags & 1)) continue;
      if (e.task == task) return depth;
      ++depth;
    }
  }
  return 0;
}

void WindowList::compact() {
  if (entries.empty()) return;
  std::vector<WindowEntry> old;
  old.swap(entries);
  focus_ = nullptr;
  for (const auto& e : old) {
    if ((e.flags & 1) && !(e.flags & 8)) append(e);
  }
  update_focus();
}

void WindowList::update_focus() {
  focus_ = nullptr;
  for (const auto& e : entries) {
    if ((e.flags & 1) && !(e.flags & 0x10)) focus_ = e.task;
  }
}

void WindowList::append(const WindowEntry& e) { entries.push_back(e); }

// ---- Entry cache -----------------------------------------------------------------

void entry_cache_load(const u8* packed, int entry) {
  for (int i = 0; i < 16; ++i) {
    const EntryCacheSlotRef s = cache_slot(i);
    if (!s.data->empty() && *s.entry == entry) return;
  }
  int slot = 0;
  while (slot < 16 && !cache_slot(slot).data->empty()) ++slot;
  if (slot == 16) slot = 15;  // the original writes past the table; never happens in practice
  // SLZ header: +4 packed size, +8 unpacked size.
  const size_t packed_size = read_u32(packed + 4) + 0x10;
  auto out = disc::slz::unpack_block(std::span<const u8>(packed, packed_size), 0, 0, nullptr);
  const EntryCacheSlotRef s = cache_slot(slot);
  *s.data = out ? std::move(*out) : std::vector<u8>{};
  *s.entry = entry;
}

void entry_cache_free(int entry) {
  for (int i = 0; i < 16; ++i) {
    const EntryCacheSlotRef s = cache_slot(i);
    if (!s.data->empty() && *s.entry == entry) {
      s.data->clear();
      s.data->shrink_to_fit();
      *s.entry = 0;
      return;
    }
  }
}

const std::vector<u8>* entry_cache_slot(int index) {
  const EntryCacheSlotRef s = cache_slot(index);
  return s.data->empty() ? nullptr : s.data;
}

// ---- Background loads --------------------------------------------------------------

BgLoadClient::~BgLoadClient() { bgload_cancel_owner(this); }

void bgload_init() {
  g_bg = BgLoad{};
  g_bg.timer = 0x3C;
}

void bgload_shutdown() {
  for (int i = 0; i < g_bg.capacity; ++i) bgload_cancel(i + 1);
  g_bg.requests.clear();
  g_bg.capacity = 0;
}

void bgload_cancel(int handle) {
  if (handle == 0) return;
  LoadRequest& r = g_bg.requests[static_cast<size_t>(handle - 1)];
  if (!(r.flags & 1)) return;
  if (r.flags & 4) {
    r.flags |= 0x20;
    menu_loader()->cancel();
    while (g_bg.state != 0) async_load_queue_update();
  }
  r.flags &= ~1u;
  if (r.flags & 8) {
    r.owned.clear();
    r.owned.shrink_to_fit();
    r.dest = nullptr;
  }
}

int bgload_queue_entry(BgLoadClient* owner, int entry, u8* dest) { return bgload_queue(owner, entry, 0, dest, 0, 0); }

int bgload_queue_bytes(BgLoadClient* owner, int entry, u8* dest, int bytes) {
  return bgload_queue(owner, entry, 0, dest, 0, (bytes + 0x7FF) >> 11);
}

int bgload_queue(BgLoadClient* owner, int entry, u16 flags, u8* dest, int offset, int sectors) {
  LoadRequest req;
  req.flags = static_cast<u16>(flags | 2);
  if (dest == nullptr) req.flags |= 10;
  req.entry = static_cast<s16>(entry);
  req.offset = static_cast<s16>(offset);
  req.sectors = static_cast<s16>(sectors);
  if (sectors == 0) req.sectors = static_cast<s16>(menu_loader()->entry_size(entry) >> 11);
  if (dest == nullptr) {
    req.owned.assign(static_cast<size_t>(req.sectors) << 11, 0);
    req.dest = req.owned.data();
  } else {
    req.dest = dest;
  }
  // bgload_add (title 0x80038a1c): the first unused slot; the table grows by ten.
  int index = -1;
  for (int i = 0; i < g_bg.capacity; ++i) {
    if (!(g_bg.requests[static_cast<size_t>(i)].flags & 1)) {
      index = i;
      break;
    }
  }
  if (index == -1) {
    index = g_bg.capacity;
    g_bg.capacity = static_cast<s16>(g_bg.capacity + 10);
    g_bg.requests.resize(static_cast<size_t>(g_bg.capacity));
  }
  req.owner = owner;
  req.flags |= 1;
  LoadRequest& slot = g_bg.requests[static_cast<size_t>(index)];
  slot = std::move(req);
  if (slot.flags & 8) slot.dest = slot.owned.data();
  return index + 1;
}

void bgload_cancel_owner(BgLoadClient* owner) {
  for (int i = 0; i < g_bg.capacity; ++i) {
    const LoadRequest& r = g_bg.requests[static_cast<size_t>(i)];
    if ((r.flags & 1) && r.owner == owner) bgload_cancel(i + 1);
  }
}

bool bgload_is_done(int handle) {
  if (handle == -1) {
    for (const auto& r : g_bg.requests) {
      if ((r.flags & 1) && (r.flags & 2)) return false;
    }
    return true;
  }
  const LoadRequest& r = g_bg.requests[static_cast<size_t>(handle - 1)];
  return !(r.flags & 1) || !(r.flags & 2);
}

bool bgload_owner_done(BgLoadClient* owner) {
  for (int i = 0; i < g_bg.capacity; ++i) {
    if (g_bg.requests[static_cast<size_t>(i)].owner == owner && !bgload_is_done(i + 1)) return false;
  }
  return true;
}

LoadRequest* bgload_request(int handle) {
  if (handle == 0) std::fprintf(stderr, "error StructScdque\n");
  return &g_bg.requests[static_cast<size_t>(handle - 1)];
}

void async_load_queue_update() {
  if (g_bg.state == 0) {
    for (int i = 0; i < g_bg.capacity; ++i) {
      const u16 f = g_bg.requests[static_cast<size_t>(i)].flags;
      if ((f & 1) && (f & 2)) {
        g_bg.current = static_cast<s16>(i);
        g_bg.state = 1;
        break;
      }
    }
  }
  if (g_bg.state == 0) return;
  Loader* loader = menu_loader();
  LoadRequest& r = g_bg.requests[static_cast<size_t>(g_bg.current)];
  for (;;) {
    bool advance = false;
    switch (g_bg.state) {
      case 1:  // mark reading, then wait for the drive
        r.flags |= 4;
        advance = loader->poll() == 0;
        break;
      case 2:
        if (loader->select_entry(r.entry) == 0) return;
        // A nonzero sector offset seeks into the entry (boot 0x80011988); see state 3.
        advance = true;
        break;
      case 3:
        if (r.flags & 0x20) {
          g_bg.state = 5;
          continue;
        }
        if (r.offset != 0 && loader->archive() != nullptr) {
          advance = loader->archive()->read_from_entry(static_cast<u32>(r.entry), static_cast<size_t>(r.sectors) << 11, r.dest,
                                                       static_cast<u32>(r.offset));
        } else {
          advance = loader->start_read(r.dest, r.sectors << 11) != 0;
        }
        break;
      case 4:
        if (r.flags & 0x20) {
          loader->cancel();
          g_bg.state = 5;
          continue;
        }
        advance = loader->poll() == 0;
        break;
      case 5:
        advance = loader->poll() == 0;
        break;
      case 6:
        g_bg.state = 0;
        g_bg.timer = 0x3C;
        r.flags &= 0xFFD9;
        return;
      default:
        return;
    }
    if (!advance) return;
    ++g_bg.state;
  }
}

// ---- Strings -----------------------------------------------------------------------

int str_len(const char* s) { return static_cast<int>(std::strlen(s)); }
void str_cat(char* dst, const char* src) { std::memcpy(dst + str_len(dst), src, static_cast<size_t>(str_len(src)) + 1); }
const char* str_chr(const char* s, int c) {
  for (;; ++s) {
    if (*s == static_cast<char>(c)) return s;
    if (*s == 0) return nullptr;
  }
}

StrBuf::StrBuf() {
  cursor = buffer.data();
  buffer[0] = 0;
}

void StrBuf::parse_format(const char* spec) {
  // States: 0 flags, 1 after a flag, 2 width digits, 3 conversion.
  int state = 0, w = 0;
  pad = ' ';
  for (; *spec != 0; ++spec) {
    const char c = *spec;
    if (state == 1) {
      if (str_chr("123456789", c)) {
        state = 2;
        w = w * 10 + (c - '0');  // the original resumes at the same character
        continue;
      }
      if (str_chr("dx", c)) {
        state = 3;
        --spec;
        continue;
      }
      if (c == '-') {
        justify = 'l';
      } else if (c == '0' || c == ' ') {
        pad = static_cast<u8>(c);
      }
    } else if (state == 0) {
      if (str_chr("-+0 #", c)) {
        state = 1;
        --spec;  // the flag itself is read again in state 1
        continue;
      }
      if (str_chr("123456789", c)) {
        state = 2;
        --spec;
        continue;
      }
      if (str_chr("dx", c)) {
        state = 3;
        --spec;
        continue;
      }
    } else if (state == 2) {
      if (str_chr("dx", c)) {
        state = 3;
        --spec;
        continue;
      }
      w = w * 10 + (c - '0');
    } else if (state == 3) {
      if (c == 'd') radix = 10;
      if (c == 'x') radix = 16;
    }
  }
  width = static_cast<s16>(w == 0 ? 10 : w);
}

void StrBuf::format_int(char* out, s32 value) {
  char digits[11];
  for (int i = 1; i < 11; ++i) digits[i] = static_cast<char>(pad);
  s16 w = width;
  u32 v = static_cast<u32>(value);
  if (value < 0 && radix == 10) {
    v = static_cast<u32>(-value);
    --w;
    *out++ = '-';
  }
  int n = 0;
  do {
    if (v == 0) {
      if (n == 0) digits[1] = '0';
      break;
    }
    const u32 d = v % static_cast<u32>(radix);
    digits[1 + n] = static_cast<char>((d & 0xFF) < 10 ? '0' + d : '7' + d);
    v /= static_cast<u32>(radix);
    ++n;
  } while (n < 9);
  if (justify == 'r') n = w;
  for (int i = n; i >= 1; --i) *out++ = digits[i];
  *out = 0;
}

void StrBuf::put(const char* text) {
  if (text[0] == '%') {
    parse_format(text);
    return;
  }
  str_cat(buffer.data(), text);
  if (str_len(buffer.data()) > 0x7F) std::fprintf(stderr, "over\n");
}

void StrBuf::put_int(s32 value) {
  char tmp[256];
  format_int(tmp, value);
  str_cat(buffer.data(), tmp);
  if (str_len(buffer.data()) > 0x7F) std::fprintf(stderr, "over\n");
}

void StrBuf::set(const char* text) {
  std::memcpy(buffer.data(), text, static_cast<size_t>(str_len(text)) + 1);
  if (str_len(buffer.data()) > 0x7F) std::fprintf(stderr, "over\n");
}

// ---- Strings ---------------------------------------------------------------------------

char* format_decimal(char* out, s32 value, int width, char pad) {
  char* p = out;
  if (value < 0) {
    *p++ = '-';
    value = -value;
  }
  if (width < 1) {
    char tmp[16];
    int n = 0;
    do {
      tmp[n++] = static_cast<char>('0' + value % 10);
      value /= 10;
    } while (value > 0);
    while (n > 0) *p++ = tmp[--n];
    *p = 0;
    return out;
  }
  p += width;
  *p = 0;
  int written = 0;
  for (;;) {
    *--p = static_cast<char>('0' + value % 10);
    value /= 10;
    ++written;
    if (value == 0 || written >= width) break;
  }
  while (written < width) {
    *--p = pad;
    ++written;
  }
  return out;
}

void strings_set(const u8* pack) { g_menu.strings = pack; }
u32 strings_word(int index) { return read_u32(g_menu.strings + index * 4); }
const u8* strings_block(int index) { return g_menu.strings + read_u32(g_menu.strings + index * 4); }
const u8* item_strings(int index) {
  return g_menu.item_data.data() + read_u32(g_menu.item_data.data() + index * 4);
}

void text_to_card_codes(const char* name, u8* out, size_t out_size) {
  // Table title 0x8004b8c8: pairs (first byte, second byte); a second byte 0xde/0xdf
  // (voiced marks) belongs to the character, anything else reads as ' '.
  const u8* pairs = table(0x8004B8C8, 0x80071CD8);
  size_t n = 0;
  int alternate = 0;
  const auto* p = reinterpret_cast<const u8*>(name);
  while (*p != 0 && n + 1 < out_size) {
    const u8 c = *p++;
    if (c == '@') {
      alternate = 1 - alternate;
      continue;
    }
    u8 second = *p;
    if (static_cast<u8>(second + 0x22) < 2) {
      ++p;
    } else {
      second = ' ';
    }
    u8 code = 0;
    int skip = alternate;
    if (pairs) {
      for (int i = 0; i < 0xFF; ++i) {
        if (pairs[i * 2] == c && pairs[i * 2 + 1] == second) {
          if (skip-- == 0) {
            code = static_cast<u8>(i);
            break;
          }
        }
      }
    }
    out[n++] = static_cast<u8>(code + 1);
  }
  out[n] = 0;
}

u32 menu_colour_code(u32 code, u32 brightness, u32 rgb) { return code | menu_colour(brightness, rgb); }

u32 menu_colour(u32 brightness, u32 rgb) {
  brightness &= 0xFF;
  if (brightness == 0) return 0;
  const u32 div = 0x80 / brightness;
  if (div == 0) return 0;  // the original traps (division by zero) for brightness > 0x80
  return ((rgb >> 16 & 0xFF) / div) << 16 | ((rgb >> 8 & 0xFF) / div) << 8 | (rgb & 0xFF) / div;
}

u32 menu_input_lowest_bit(u32 bits) {
  if (bits == 0) return 0;
  for (u32 b = 1, i = 0; i < 16; ++i, b <<= 1) {
    if (bits & b) return b;
  }
  return 0;
}

}  // namespace ss::game::menu
