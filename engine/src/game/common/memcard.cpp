#include "game/common/memcard.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "game/boot/system.h"
#include "runtime/libcard.h"

namespace ss::game {
namespace {

using namespace psyq;

constexpr const char* kSaveName = "BASCUS-94421S02-S";  // title 0x8004dea0

// "bu" "%1d" "0:" (0x8004b760..68) [+ "BASCUS-94421S02-S" "%02d" (0x8004b76c)]
std::string device_path(const MemcardSlot* s) {
  char path[16];
  std::snprintf(path, sizeof path, "bu%d0:", s->port);
  return path;
}

std::string save_path(const MemcardSlot* s, int number) {
  char name[48];
  std::snprintf(name, sizeof name, "%s%s%02d", device_path(s).c_str(), kSaveName, number);
  return name;
}

// title 0x80037f60: which SwCARD event fired (1 IOE, 2 ERROR, 3 TIMEOUT, 4 NEW), or 0.
int test_sw_events(MemoryCard* mc) {
  if (TestEvent(mc->events[0]) == 1) return 1;
  if (TestEvent(mc->events[1]) == 1) return 2;
  if (TestEvent(mc->events[2]) == 1) return 3;
  return TestEvent(mc->events[3]) == 1 ? 4 : 0;
}

void clear_sw_events(MemoryCard* mc) {  // title 0x80037fe0
  for (int i = 0; i < 4; ++i) TestEvent(mc->events[i]);
}

int wait_hw_event(MemoryCard* mc) {  // title 0x800380b8
  for (;;) {
    for (int i = 0; i < 4; ++i)
      if (TestEvent(mc->events[4 + i]) == 1) return i + 1;
  }
}

void clear_hw_events(MemoryCard* mc) {  // title 0x80038138
  for (int i = 0; i < 4; ++i) TestEvent(mc->events[4 + i]);
}

void retry_once(MemcardSlot* s) {  // title 0x800378cc
  if (s->retries != 0) --s->retries;
}

// title 0x800378f0: counts every file on the card and the blocks left.
void scan_directory(MemcardSlot* s) {
  const std::string pattern = device_path(s) + "*";  // 0x8004b800
  DIRENTRY entries[16];
  s16 count = 0;
  if (firstfile(pattern.c_str(), &entries[0]) == &entries[0]) {
    count = 1;
    while (count < 16 && nextfile(&entries[count]) == &entries[count]) ++count;
  }
  s->free_blocks = 15;
  s->file_count = count;
  for (int i = 0; i < count; ++i) s->free_blocks = static_cast<s16>(s->free_blocks - entries[i].size / 0x2000);
}

// title 0x80037b60: maps this game's saves to the fifteen positions; positions with
// no save are either free (while blocks remain) or filled by a later save.
void scan_saves(MemcardSlot* s) {
  const std::string pattern = device_path(s) + kSaveName + "*";  // 0x8004b804
  DIRENTRY entries[16];
  s16 count = 0;
  if (firstfile(pattern.c_str(), &entries[0]) == &entries[0]) {
    count = 1;
    while (count < 16 && nextfile(&entries[count]) == &entries[count]) ++count;
  }
  s->save_count = count;
  for (int i = 0; i < 15; ++i) s->save_map[i] = static_cast<s16>(-(i + 1));
  const size_t prefix = std::strlen(kSaveName);
  for (int i = 0; i < count; ++i) {
    const int n = (entries[i].name[prefix] - '0') * 10 + (entries[i].name[prefix + 1] - '0');
    if (n >= 1 && n <= 15) s->save_map[n - 1] = static_cast<s16>(n);
  }
  int free_left = s->free_blocks;
  for (int i = 0; i < 15; ++i) {
    if (s->save_map[i] >= 0) continue;
    if (free_left != 0) {
      --free_left;
      continue;
    }
    for (int j = i + 1; j < 15; ++j) {
      if (s->save_map[j] > 0) {
        s->save_map[i] = s->save_map[j];
        s->save_map[j] = static_cast<s16>(0xFFF0);
        break;
      }
    }
  }
}

// title 0x80031598: ORs 'n' bytes of src into dest.
void or_bytes(u8* dest, const u8* src, int n) {
  for (int i = 0; i < n; ++i) dest[i] |= src[i];
}

// title 0x800313f0: once enabled, reads block 0x280 of every save and merges them into
// config + 0x1a0 (unlock flags shared by all saves).
void read_unlock_blocks(MemoryCard* mc, int n) {
  MemcardSlot* s = &mc->slots[n];
  MemoryCard::Reader* r = &mc->readers[n];
  if (mc->summaries_enabled == 0) r->step = 0;
  if (s->status != 0x2282) {
    r->step = 0;
    return;
  }
  switch (r->step) {
    case 0:
      if (mc->summaries_enabled != 0) r->step = 1;
      break;
    case 1:
      if (s->state == 0x14) {
        s->state = 0x0C;
        s->index = 0;
        s->dest = r->buffer;
        r->copied = 0;
        ++r->step;
      }
      break;
    case 2: {
      mc->result = 1;
      r->count = s->index;
      if (r->count == 0x0F) ++r->step;
      auto* config = static_cast<u8*>(handoff_get(&get_system()->handoff, 2));
      while (r->copied != r->count) {
        if (config) or_bytes(config + 0x1A0, r->buffer + r->copied * 0x100, 0x100);
        ++r->copied;
      }
      break;
    }
    case 3:
      mc->result = -1;
      break;
  }
}

std::string g_images[2];

}  // namespace

void memcard_set_images(const char* slot1_path, const char* slot2_path) {
  g_images[0] = slot1_path ? slot1_path : "";
  g_images[1] = slot2_path ? slot2_path : "";
  if (!g_images[0].empty()) card_set_image(0, g_images[0]);
  if (!g_images[1].empty()) card_set_image(1, g_images[1]);
  card_set_inserted(0, !g_images[0].empty());
  card_set_inserted(1, !g_images[1].empty());
}

// [world agent: Archipelago's save sidecar, game/ap]
const std::string& memcard_image(int slot) { return g_images[slot == 1 ? 1 : 0]; }

MemoryCard* memcard_create() {
  auto* mc = new MemoryCard{};
  // Driver constructor (title 0x800368ec): card events, then both slots start with a check.
  EnterCriticalSection();
  const long specs[4] = {kEvSpIoe, kEvSpError, kEvSpTimeout, kEvSpNew};
  for (int i = 0; i < 4; ++i) mc->events[i] = OpenEvent(kSwCard, specs[i], 0x2000, nullptr);
  for (int i = 0; i < 4; ++i) mc->events[4 + i] = OpenEvent(kHwCard, specs[i], 0x2000, nullptr);
  for (long e : mc->events) EnableEvent(e);
  ExitCriticalSection();
  mc->advance = 0;
  mc->current = 0;
  for (int i = 0; i < 2; ++i) {
    MemcardSlot& s = mc->slots[i];
    s = MemcardSlot{};
    s.flags = 1;
    s.status = 0;
    s.port = static_cast<s16>(i);
    s.chan = static_cast<s16>(i << 4);
    s.fd = -1;
    mc->readers[i].buffer = new u8[0xF00]();
    mc->readers[i].step = 0;
  }
  mc->summaries_enabled = 0;
  mc->result = 0;
  return mc;
}

void memcard_destroy(MemoryCard* mc) {
  if (!mc) return;
  for (auto& r : mc->readers) delete[] r.buffer;
  EnterCriticalSection();
  for (long e : mc->events) CloseEvent(e);
  ExitCriticalSection();
  delete mc;
}

void memcard_update(MemoryCard* mc) {
  memcard_driver_update(mc);
  read_unlock_blocks(mc, 0);
  read_unlock_blocks(mc, 1);
}

int memcard_start_check(MemoryCard*) { return 1; }

int memcard_status(MemoryCard* mc) {
  const MemcardSlot& a = mc->slots[0];
  const MemcardSlot& b = mc->slots[1];
  auto has_save = [](const MemcardSlot& s) {
    for (s16 n : s.save_map)
      if (n > 0) return true;
    return false;
  };
  if (a.status == 0x2282) return has_save(a) ? 2 : 1;
  if (b.status == 0x2282) return has_save(b) ? 2 : 1;
  if (a.status == 0x2281 || b.status == 0x2281) return 3;
  return a.status == 0x1141 && b.status == 0x1141 ? 3 : 0;
}

int memcard_new_game_notice(MemoryCard* mc) {
  const MemcardSlot& a = mc->slots[0];
  const MemcardSlot& b = mc->slots[1];
  if (a.status == 0x1141 && b.status == 0x1141) return 1;  // no card in either slot
  // The original also counts full cards (for the "no space" notices) but returns 0
  // whenever a card is ready, so those notices never show.
  bool ready = false;
  int full = 0;
  bool full_with_save = false;
  if (a.status == 0x2282) {
    ready = true;
    if (a.free_blocks == 0) {
      full = 1;
      full_with_save = a.save_count != 0;
    }
  }
  if (b.status == 0x2282) {
    ready = true;
    if (b.free_blocks == 0) {
      ++full;
      if (b.save_count != 0) full_with_save = true;
    }
  }
  if (ready || a.status == 0x2281 || b.status == 0x2281) return 0;
  if (full == 0) return -1;
  return full_with_save ? (full + 1) | 4 : full + 1;
}

void memcard_enable_summaries(MemoryCard* mc, s16 on) { mc->summaries_enabled = on; }

int memcard_take_result(MemoryCard* mc) {
  const int r = mc->result;
  mc->result = 0;
  return r;
}

void memcard_driver_update(MemoryCard* mc) {
  MemcardSlot* s = &mc->slots[mc->current];
  const u16 f = s->flags;
  if ((f & 1) == 0) {
    if ((f & 2) == 0) {
      if ((f & 4) == 0) {
        if (f & 8) memcard_file_update(mc, s);
      } else if ((f & 0x8000) == 0) {
        s->flags = f | 0x8000;
        clear_sw_events(mc);
        _card_load(s->chan);
      } else {
        const int e = test_sw_events(mc);
        if (e != 0) {
          const u16 g = s->flags & 0x7FFB;
          s->flags = g;
          if (e == 1) {
            s->flags = g | 8;  // directory read: start the file operations
            s->retries = 10;
            s->state = 0;
          } else if (e == 2 || e == 3) {
            s->flags = g | 1;
          } else if (e == 4) {  // unformatted
            s->flags = g | 1;
            s->status = 0x2281;
            mc->advance |= 1;
            if (s->flags & 0x2000) {
              s->flags &= 0x9FFF;
              if (psyq::format(device_path(s).c_str()) == 0) s->flags |= 0x4000;
            }
          }
        }
      }
    } else {
      clear_hw_events(mc);
      _card_clear(s->chan);
      wait_hw_event(mc);
      s->flags &= 0x7FFD;
      mc->advance |= 1;
      s->flags |= 1;
    }
  } else {
    s->state = 0;
    if ((f & 0x8000) == 0) {
      s->flags = f | 0x8000;
      clear_sw_events(mc);
      _card_info(s->chan);
    } else {
      const int e = test_sw_events(mc);
      if (e != 0) {
        const u16 g = s->flags;
        s->flags = g & 0x7FFF;
        if (e == 2 || e == 3) {
          s->status = e == 2 ? 0x1140 : 0x1141;
          mc->advance |= 1;
        } else if (e == 1) {
          s->flags = (g & 0x7FFE) | 4;
        } else if (e == 4) {
          s->flags = (g & 0x7FFE) | 2;
        }
      }
    }
  }
  if (mc->advance & 1) {
    mc->advance &= 0xFFFE;
    if (++mc->current > 1) mc->current = 0;
  }
}

void memcard_file_update(MemoryCard* mc, MemcardSlot* s) {
  for (;;) {
    if (s->flags & 0x8000) {
      int e = test_sw_events(mc);
      if (e != 0) {
        s->flags &= 0x7FFF;
        if (s->retries == 0) e = 2;
        if (e == 1) {
          if (s->state == 0x14) mc->advance |= 1;
          s->retries = 10;
        } else {
          retry_once(s);
          if (s->state != 0x14) --s->state;
        }
      }
      return;
    }
    if (s->retries == 0) {
      s->flags = (s->flags & 0xFFF7) | 1;
      s->status = 0;
      return;
    }
    auto open_current = [&]() {
      s->fd = psyq::open(save_path(s, s->file_number).c_str(), s->open_mode);
      if (s->fd != -1) {
        ++s->state;
      } else {
        retry_once(s);
      }
    };
    switch (s->state) {
      case 0:
        scan_directory(s);
        scan_saves(s);
        s->status = 0x2282;
        ++s->state;
        continue;
      case 1:  // read the summary frame (0x200) of every save
        s->index = 0;
        s->offset = 0x200;
        ++s->state;
        continue;
      case 2:
        if (s->index < 0x0F) {
          const s16 n = s->save_map[s->index];
          s->file_number = n;
          if (n >= 0) {
            s->open_mode = 0x8001;
            s->length = 0x80;
            s->buffer = s->summaries[s->index];
            ++s->state;
            return;
          }
          s->state = 2;
          ++s->index;
          return;
        }
        s->state = 0x14;
        return;
      case 3: case 8: case 0x0E: case 0x17: case 0x1A:
        open_current();
        return;
      case 4: case 9: case 0x0F: case 0x1F: {
        if (psyq::lseek(s->fd, s->offset, 0) == -1) {
          retry_once(s);
          return;
        }
        if (psyq::read(s->fd, s->buffer, s->length + 0x7F) != -1) {
          s->flags |= 0x8000;
          ++s->state;
        } else {
          retry_once(s);
        }
        return;
      }
      case 5: case 0x0A: case 0x10: case 0x18: case 0x20:
        if (psyq::close(s->fd) != -1) {
          ++s->state;
        } else {
          retry_once(s);
        }
        return;
      case 6:
        s->state = 2;
        ++s->index;
        continue;
      case 7:
        s->file_number = s->save_map[s->index];
        s->open_mode = 0x8001;
        ++s->state;
        return;
      case 0x0B:
        s->state = 0x14;
        s->flags &= 0xEFFF;
        return;
      case 0x0C:  // read block 0x280 of every save into dest
        s->index = 0;
        s->offset = 0x280;
        ++s->state;
        continue;
      case 0x0D:
        if (s->index < 0x0F) {
          const s16 n = s->save_map[s->index];
          s->file_number = n;
          if (n >= 0) {
            s->open_mode = 0x8001;
            s->length = 0x100;
            s->buffer = s->dest + s->index * 0x100;
            ++s->state;
            return;
          }
          s->state = 0x0D;
          ++s->index;
          return;
        }
        s->state = 0x14;
        return;
      case 0x11:
        s->state = 0x0D;
        ++s->index;
        continue;
      case 0x14:  // idle: keep checking the card
        clear_sw_events(mc);
        _card_info(s->chan);
        s->flags |= 0x8000;
        return;
      case 0x15: {
        u8 frame[0x80] = {0xFF, 0xFF};
        clear_hw_events(mc);
        _new_card();
        _card_write(s->chan, 0, frame);
        wait_hw_event(mc);
        s->state = 0x14;
        return;
      }
      case 0x16: {  // write a save: create its file if the position is free
        const s16 n = s->save_map[s->index];
        s->file_number = n;
        if (n < 0) {
          s->open_mode = 0x18200;
          s->file_number = static_cast<s16>(-n);
          ++s->state;
          return;
        }
        s->state = 0x19;
        return;
      }
      case 0x19:
        s->open_mode = 0x8002;
        s->dest = s->buffer;
        mc->write_length = s->length;
        ++s->state;
        return;
      case 0x1B: case 0x1D: {
        if (psyq::lseek(s->fd, s->offset, 0) == -1) {
          retry_once(s);
          return;
        }
        if (psyq::write(s->fd, s->buffer, s->length + 0x7F) != -1) {
          s->flags |= 0x8000;
          ++s->state;
        } else {
          retry_once(s);
        }
        return;
      }
      case 0x1C:
        s->offset = 0x200;
        s->length = 0x80;
        s->buffer += 0x200;
        s->buffer[0x18] = 0x55;
        s->buffer[0x19] = 0x55;
        ++s->state;
        return;
      case 0x1E:
        s->open_mode = 0x8001;
        s->offset = 0;
        s->buffer = mc->write_buffer;
        s->length = mc->write_length;
        ++s->state;
        return;
      case 0x21:
        std::memcpy(s->summaries[s->index], s->dest + 0x200, 0x80);
        scan_directory(s);
        scan_saves(s);
        s->state = 0x14;
        s->flags &= 0xEFFF;
        return;
      default:
        return;
    }
  }
}

}  // namespace ss::game
