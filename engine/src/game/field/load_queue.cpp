#include "game/field/load_queue.h"

#include "game/field/field.h"
#include "runtime/runtime.h"

namespace ss::game::field {
namespace {

LoadQueue* g_queue = nullptr;  // 0x800752b0
VoiceStream g_voice;
PreloadSlot g_preloads[12];                      // 0x80075768
std::vector<PreloadSlot> g_saved_preloads;       // 0x80075858 (empty = none saved)
bool g_preloads_saved = false;

void todo(const char* what) {
  runtime::Runtime::get().log_once(std::string("field:") + what, std::string("field: not ported yet: ") + what);
}

void voice_queue_abort() { todo("voice_queue_abort (0x80079a7c)"); }

// Frees a slot's buffer and marks it free (the common tail of the release paths).
void preload_free(PreloadSlot& s) {
  if (s.done == 0) load_queue_cancel_entry(g_queue, s.entry);
  if (s.owned != 0) s.data = {};
  s.data.clear();
  s.kind = 0;
}

int entry_bytes(int entry, int bytes) { return bytes != 0 ? bytes : globals().loader->entry_size(entry); }

}  // namespace

LoadQueue* load_queue() { return g_queue; }
VoiceStream& voice_stream() { return g_voice; }

LoadQueue* load_queue_construct(LoadQueue* q) {
  g_queue = q;
  load_queue_reset(q);
  task_register(q, 1);
  return q;
}

void load_queue_destruct(LoadQueue* q) {
  globals().loader->cancel();
  task_unregister(q);
  if (g_queue == q) g_queue = nullptr;
  delete q;
}

void load_queue_push(LoadQueue* q, int entry, void* dest, u8* done, int bytes) {
  if (((q->tail + 1) & 0xF) != q->head) {
    LoadRequest& r = q->requests[q->tail];
    r.entry = entry;
    r.bytes = bytes;
    r.dest = dest;
    r.done = done;
    r.active = 1;
    q->tail = (q->tail + 1) & 0xF;
  }
  q->update();
}

void load_queue_reset(LoadQueue* q) {
  q->head = 0;
  q->tail = 0;
  q->in_flight_done = nullptr;
  q->busy = 0;
  // loader_set_callback(loader, 0, 0); a running read is cancelled unless the voice
  // queue owns it. Host reads never stay running.
  if (globals().loader->poll() != 0 && (g_voice.enabled == 0 || g_voice.state > 2)) globals().loader->cancel();
}

void load_queue_cancel_entry(LoadQueue* q, int entry) {
  if (q == nullptr) return;
  u32 i = q->head;
  if (q->busy != 0) i = (i + 0xF) & 0xF;  // the request in flight is one behind the head
  for (; i != q->tail; i = (i + 1) & 0xF) {
    LoadRequest& r = q->requests[i];
    if (r.active == 0 || r.entry != entry) continue;
    if (q->busy != 0 && q->in_flight_entry == entry && globals().loader->poll() != 0) {
      q->in_flight_done = nullptr;
      q->busy = 0;
      globals().loader->cancel();
    }
    r.active = 0;
  }
}

bool load_queue_busy(const LoadQueue* q) { return q->head != q->tail || q->busy != 0; }

void load_queue_set_streaming(LoadQueue*, bool on) {
  if (!on) {
    if (g_voice.enabled != 0 && g_voice.state != 0 && g_voice.state < 3) voice_queue_abort();
  } else if (g_voice.state < 3 && (globals().field_flags[0x25] != 0 || globals().scripted_battle != 0)) {
    g_voice.enabled = 1;
    return;
  }
  g_voice.enabled = 0;
}

void LoadQueue::update() {
  if (busy == 0 && head == tail) {
    if (g_voice.enabled != 0 && g_voice.state - 1U < 2) {
      // TODO(port): the voice-clip queue (voice_queue_busy 0x80078fd4, _update
      // 0x80079764, _build_field 0x80079308, _abort 0x80079a7c).
      todo("voice clip streaming");
      g_voice.state = 3;
      g_voice.enabled = 0;
    }
    return;
  }
  if (g_voice.enabled != 0 && g_voice.state - 1U < 2) voice_queue_abort();
  for (;;) {
    if (head == tail) return;
    if (requests[head].active != 0) break;
    head = (head + 1) & 0xF;
  }
  if (busy != 0 || globals().loader->poll() != 0) return;
  LoadRequest& r = requests[head];
  if (globals().loader->select_entry(r.entry) != 1) return;
  busy = 1;
  in_flight_done = r.done;
  in_flight_entry = r.entry;
  head = (head + 1) & 0xF;
  globals().loader->start_read(r.dest, r.bytes);
  // load_callback_done (0x80043654): the host read has already completed.
  if (in_flight_done != nullptr) *in_flight_done = 1;
  in_flight_done = nullptr;
  busy = 0;
}

// ---- Preload table --------------------------------------------------------------

PreloadSlot* preload_table() { return g_preloads; }
bool preload_table_saved() { return g_preloads_saved; }

void preload_clear_table() {
  for (auto& s : g_preloads) s = PreloadSlot{};
}

int preload_start(int entry, int kind, int tag, int bytes) {
  if (g_preloads_saved) return 0;
  globals().map_preload_pending = 0;
  for (const auto& s : g_preloads)
    if (s.kind != 0 && s.entry == entry) return 1;
  for (auto& s : g_preloads) {
    if (s.kind != 0) continue;
    // The console also refuses when the heap's free space minus a reserve
    // (0x800761a4, a third of it on the world map) can't hold the entry.
    s.data.assign(static_cast<size_t>(entry_bytes(entry, bytes)), 0);
    s.owned = 1;
    s.kind = static_cast<u8>(kind);
    s.done = 0;
    s.entry = entry;
    s.tag = static_cast<s16>(tag);
    s.bytes = bytes;
    load_queue_push(g_queue, entry, s.data.data(), &s.done, bytes);
    return 1;
  }
  return 0;
}

void preload_save_table() {
  g_saved_preloads.assign(12, PreloadSlot{});
  for (int i = 0; i < 12; ++i) {
    const PreloadSlot& s = g_preloads[i];
    PreloadSlot& d = g_saved_preloads[static_cast<size_t>(i)];
    d.kind = s.kind;
    d.done = s.done;
    d.owned = s.owned;
    d.tag = s.tag;
    d.entry = s.entry;
    d.bytes = s.bytes;  // the buffer itself stays with the live table
  }
  g_preloads_saved = true;
}

void preload_restore_table() {
  if (!g_preloads_saved) return;
  for (int i = 0; i < 12; ++i) {
    PreloadSlot& s = g_preloads[i];
    const PreloadSlot& d = g_saved_preloads[static_cast<size_t>(i)];
    if (s.kind == d.kind) continue;
    s.data.assign(static_cast<size_t>(entry_bytes(d.entry, d.bytes)), 0);
    s.owned = 1;
    s.done = 0;
    s.entry = d.entry;
    s.kind = d.kind;
    s.bytes = d.bytes;
    s.tag = d.tag;
    load_queue_push(g_queue, s.entry, s.data.data(), &s.done, s.bytes);
  }
  g_saved_preloads.clear();
  g_preloads_saved = false;
}

int preload_release(int kind) {
  int lo = kind, hi = kind;
  if (kind == -1) {
    lo = 1;
    hi = 0xFF;
  }
  int released = 0;
  for (auto& s : g_preloads) {
    if (s.kind == 0 || s.kind < (lo & 0xFF) || s.kind > (hi & 0xFF)) continue;
    preload_free(s);
    ++released;
  }
  return released;
}

int preload_release_kind2_except(int tag) {
  int freed = 0;
  for (auto& s : g_preloads) {
    if (s.kind != 2 || s.tag == tag) continue;
    if (s.owned != 0) ++freed;
    preload_free(s);
  }
  return freed;
}

void preload_drop_incomplete() {
  for (auto& s : g_preloads) {
    if (s.kind != 0 && s.owned != 0 && s.done == 0) {
      s.data = {};
      s.kind = 0;
    }
  }
}

std::vector<u8>* preload_find(int entry, u8** done, s32* bytes) {
  for (auto& s : g_preloads) {
    if (s.kind == 0 || s.entry != entry) continue;
    if (done != nullptr) *done = &s.done;
    if (bytes != nullptr) *bytes = s.bytes;
    return &s.data;
  }
  return nullptr;
}

int preload_find_music() {
  for (const auto& s : g_preloads)
    if (s.kind != 0 && static_cast<u32>(s.entry - 0xA51) <= 0x5C) return s.entry;
  return -1;
}

int preload_count_done(int kind) {
  int lo = kind, hi = kind;
  if (kind == -1) {
    lo = 1;
    hi = 0xFF;
  }
  int n = 0;
  for (const auto& s : g_preloads)
    if (s.kind != 0 && s.kind >= (lo & 0xFF) && s.kind <= (hi & 0xFF) && s.done != 0) ++n;
  return n;
}

int preload_get_kind2_tag() {
  for (const auto& s : g_preloads)
    if (s.kind == 2) return s.tag;
  return -1;
}

void preload_remove(int entry) {
  for (auto& s : g_preloads) {
    if (s.kind == 0 || s.entry != entry) continue;
    preload_free(s);
    return;
  }
}

void preload_prune_for_map(int map) {
  auto is_field_map = [](int e) { return static_cast<u32>(e - 0xC87) < 0x33B; };
  auto is_world_chunk = [](int e) { return static_cast<u32>(e - 0x1014) < 0xD2; };
  if (static_cast<u32>(map - 1) < 3) {
    // Entering a world map: a preloaded field map container means start over.
    for (const auto& s : g_preloads) {
      if (s.kind != 0 && is_field_map(s.entry)) {
        preload_release(-1);
        return;
      }
    }
    return;
  }
  for (const auto& s : g_preloads) {
    if (s.kind != 0 && is_world_chunk(s.entry)) {
      preload_release(-1);
      return;
    }
  }
  // Other maps' containers are dropped; this map's stays.
  for (auto& s : g_preloads)
    if (s.kind != 0 && is_field_map(s.entry) && s.entry != map + 0xC87) preload_free(s);
}

int preload_set_tag(int entry, int tag) {
  for (auto& s : g_preloads) {
    if (s.kind != 0 && s.entry == entry) {
      s.tag = static_cast<s16>(tag);
      return 1;
    }
  }
  return 0;
}

void preload_release_pending() {
  FieldGlobals& g = globals();
  if (g.battle_preload_requested != 0) {
    preload_release(1);
    g.battle_preloaded = 0;
    g.battle_preload_requested = 0;
  }
}

}  // namespace ss::game::field
