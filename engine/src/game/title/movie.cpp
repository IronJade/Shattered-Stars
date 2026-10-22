#include "game/title/movie.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <span>
#include <string>

#include "disc/archive.h"
#include "disc/slz.h"
#include "game/boot/loader.h"
#include "game/boot/pad.h"
#include "game/boot/stubs.h"
#include "game/boot/system.h"
#include "runtime/runtime.h"

namespace ss::game {
namespace {

using psyq::RECT;
using runtime::Runtime;

// ---- Console time ----------------------------------------------------------------
//
// On the console the player is polled in a busy loop while the drive, the MDEC and
// the vblank interrupt run on their own; host code takes no time at all. The player
// therefore keeps the console's clock itself, in vblanks: the runtime's vblank count
// plus how far into the current field the console has got. Work the console spends
// CPU time on advances it (vblank interrupts fire as fields end), and when the loop
// would only spin it jumps to the next thing that happens.

constexpr double kVblankHz = 60000.0 / 1001.0;  // the runtime's field rate
constexpr double kCpuHz = 33868800.0;
constexpr double kSectorVblanks = kVblankHz / 150.0;  // double speed: 150 sectors a second

// From movie_open to the first sector: PCSX-Redux's drive seeks 3 us a sector of
// distance plus 1 ms for a read (cdrom.cc computeSeekDelay), then reads the sector; in
// Redux the first sector comes about 30 ms later still (the boot loader's Setloc /
// ReadS round trips). Measured on the title's opening (0.288 s for the 84800 sectors
// from the end of the studio movie: 26 ms), its studio movie (first frame 41 vblanks
// after movie_open, 189230 sectors from #2676) and the eleven credit movies (#3107; the
// first 116363 sectors from the names it read last, the others 30 sectors on: 26-45 ms).
// The decode is in CPU cycles.
constexpr double kSeekSecondsPerSector = 3e-6;
constexpr double kSeekSeconds = 0.001 + 0.030;
constexpr double kVlcCyclesPerCodeWord = 33.6;     // DecDCTvlc: per MDEC code word (header's first halfword)
constexpr double kVlcCyclesPerByte = 30.5;         // ... and per bitstream byte
constexpr double kVlcCyclesBase = -18000.0;
constexpr double kMdecCyclesPerWord = 9.2;         // MDEC output, 530000 cycles for a 24-bit frame
constexpr double kPauseVblanks = kSectorVblanks;   // last sector to the Pause command's completion

double cycles_to_vblanks(double cycles) { return std::max(cycles, 0.0) * kVblankHz / kCpuHz; }

struct ConsoleClock {
  u64 vblank = std::numeric_limits<u64>::max();  // runtime vblank 'phase' belongs to
  double phase = 0;                               // fraction of that field already spent
};
ConsoleClock g_clock;

double console_now() {
  const u64 v = Runtime::get().vblanks();
  return static_cast<double>(v) + (g_clock.vblank == v ? g_clock.phase : 0.0);
}

// Moves the clock to 't', running the vblank interrupt for every field that ends on the way.
void console_advance_to(double t) {
  Runtime& rt = Runtime::get();
  while (t >= static_cast<double>(rt.vblanks() + 1)) rt.vblank();
  g_clock.vblank = rt.vblanks();
  g_clock.phase = std::max(t - static_cast<double>(rt.vblanks()), 0.0);
}

// The CPU is busy for 'vblanks' (decoding, for instance).
void console_spend(double vblanks) { console_advance_to(console_now() + vblanks); }

// The loop has nothing to do until 't': it spins until then or until the current
// field ends, whichever comes first (the vblank interrupt may change what it sees).
void console_idle_until(double t) {
  const double now = console_now();
  console_advance_to(std::max(now, std::min(t, std::floor(now) + 1.0)));
}

// ---- The drive streaming an entry into the boot loader's ring ---------------------
//
// On the console the boot loader (0x80011744-0x8001205c) reads with ReadS at double
// speed and cd_sector_ready_callback (0x800111f0) copies every sector into the ring
// as it arrives; here sectors arrive on the console clock: the first after the seek
// from the drive head (Loader::head), then one every 1/150 s.
// (The host loader reads synchronously and the archive has no per-sector reads, so the
// streamed range is read when the stream starts and handed out on the console clock.)

struct Stream {
  int entry = -1;               // Setloc target (loader_select_entry)
  Loader* loader = nullptr;     // host: its head follows the stream
  u32 lba = 0;                  // host: the entry's first sector
  bool busy = false;            // loader_poll: until the drive pauses after the last sector
  std::vector<u8> sectors;      // the range being streamed, 2048 bytes per sector
  u32 count = 0;                // sectors to stream
  u32 delivered = 0;            // sectors already in the ring
  double first_arrival = 0;     // console time of the first sector
  // cd_sector_ready_callback's state in header mode (0x8002b56c-0x8002b594).
  u8* ring = nullptr;           // 0x8002b578
  u32 slot_bytes = 0;           // 0x8002b584
  int slot_count = 0;           // 0x8002b58c
  int slot = 0;                 // 0x8002b58e: slot being filled
  size_t write_at = 0;          // 0x8002b56c: where the sector would go without headers
  size_t header_at = 0;         // 0x8002b570: next 32-byte STR header
  size_t data_at = 0;           // 0x8002b574: next 2016 bytes of frame data
  s32 slot_left = 0;            // 0x8002b588: bytes until the slot is full
  u8 defer_sectors = 0;         // 0x8002b594: sectors wait while the MDEC owns the bus
};
Stream g_stream;

double sector_arrival(u32 index) { return g_stream.first_arrival + index * kSectorVblanks; }
u32 slot_chunks() { return g_stream.slot_bytes >> 11; }
u16 sector_chunk(u32 index) { return read_u16(&g_stream.sectors[size_t{index} * 2048 + 4]); }

void stream_stop() {
  if (g_stream.busy && g_stream.loader != nullptr) g_stream.loader->set_head(g_stream.lba + g_stream.delivered);
  g_stream.busy = false;
  g_stream.sectors.clear();
  g_stream.sectors.shrink_to_fit();
}

// cd_sector_ready_callback for one sector of a stream with headers: the STR header to
// the slot's header table, the rest after it; a slot is done when it is full or holds
// its frame's last chunk, and the next one starts with a cleared header table.
void stream_deliver_sector() {
  Stream& s = g_stream;
  const u8* sector = &s.sectors[size_t{s.delivered} * 2048];
  std::memcpy(s.ring + s.header_at, sector, 32);
  std::memcpy(s.ring + s.data_at, sector + 32, 2048 - 32);
  s.header_at += 32;
  s.data_at += 2048 - 32;
  s.write_at += 2048;
  s.slot_left -= 0x800;
  if (s.slot_left < 1 || read_u16(s.ring + s.header_at - 28) == slot_chunks() - 1) {
    if (++s.slot >= s.slot_count) {
      s.slot = 0;
      s.write_at = 0;
    }
    s.header_at = s.write_at;
    std::memset(s.ring + s.header_at, 0, slot_chunks() * 32);
    s.data_at = s.header_at + slot_chunks() * 32;
    s.slot_left = static_cast<s32>(s.slot_bytes);
  }
  ++s.delivered;
}

// Delivers what has arrived by now; the loader goes idle once the drive has paused.
void stream_update() {
  Stream& s = g_stream;
  if (!s.busy) return;
  const double now = console_now();
  while (s.delivered < s.count && sector_arrival(s.delivered) <= now) stream_deliver_sector();
  if (s.delivered == s.count && now >= sector_arrival(s.count - 1) + kPauseVblanks) stream_stop();
}

// When the stream next changes something the game can see: a slot completing, or
// the loader going idle.
double stream_next_event() {
  const Stream& s = g_stream;
  if (!s.busy) return std::numeric_limits<double>::infinity();
  s32 left = s.slot_left;
  for (u32 i = s.delivered; i < s.count; ++i) {
    left -= 0x800;
    if (left < 1 || sector_chunk(i) == slot_chunks() - 1) return sector_arrival(i);
  }
  return sector_arrival(s.count - 1) + kPauseVblanks;
}

// loader_select_entry (boot 0x80011654) for the stream: the drive's Setloc target.
void loader_select_entry(Loader* loader, int entry) {
  loader->select_entry(entry);
  g_stream.entry = entry;
}

// 0x8001177c: streams 'bytes' of the selected entry into 'ring', 'slots' slots of
// 'slot_bytes'; with 'headers' each sector's first 32 bytes go to a table at the
// slot's start and the rest after it (0x8002b592).
void loader_start_stream(Loader* loader, u8* ring, int headers, int slot_bytes, int slots, int bytes) {
  Stream& s = g_stream;
  s.ring = ring;
  s.slot_bytes = static_cast<u32>(slot_bytes);
  s.slot_count = slots;
  s.slot = 0;
  s.write_at = 0;
  s.header_at = 0;
  s.data_at = static_cast<size_t>(slot_bytes >> 11) * 32;
  s.slot_left = slot_bytes;
  s.defer_sectors = 0;
  if (!headers) Runtime::get().log_once("stream-headers", "movie stream without headers is not supported");
  // 0x80011844: the read itself (ReadS once the Setloc has completed).
  s.count = bytes > 0 ? static_cast<u32>(bytes) / 2048 : 0;
  s.sectors.assign(size_t{s.count} * 2048, 0);
  disc::Archive* archive = loader->archive();
  if (s.count == 0 || !archive ||
      !archive->read_from_entry(static_cast<u32>(s.entry), s.sectors.size(), s.sectors.data())) {
    std::fprintf(stderr, "[movie] cannot read entry %d\n", s.entry);
    stream_stop();
    return;
  }
  s.delivered = 0;
  s.busy = true;
  s.loader = loader;
  s.lba = archive->entry(static_cast<u32>(s.entry)).lba;
  const u32 head = loader->head();
  const u32 distance = s.lba > head ? s.lba - head : head - s.lba;
  s.first_arrival = console_now() + (distance * kSeekSecondsPerSector + kSeekSeconds) * kVblankHz + kSectorVblanks;
}

int loader_stream_slot(Loader*) {  // 0x80011744: the slot completed last
  int slot = g_stream.slot - 1;
  if (slot < 0) slot += g_stream.slot_count;
  return slot;
}
int loader_stream_busy(Loader*) { return g_stream.busy ? 1 : 0; }  // loader_poll (0x80011b88)
void loader_stream_cancel(Loader*) { stream_stop(); }              // loader_cancel (0x80011b48)
u8 loader_shell_open(Loader*) { return 0; }                         // 0x80011be0: drive door opened
void loader_defer_sectors(Loader*, u8 on) { g_stream.defer_sectors = on; }  // 0x8001204c
// 0x8001205c: a sector arrived while deferred. Host sectors never wait for the MDEC.
u8 loader_sector_deferred(Loader*) { return 0; }
void cd_sector_ready_callback(Loader*, int) {}  // boot 0x800111f0: see stream_deliver_sector

// ---- The player's globals (0x80050aa8-0x80050ad6; MovieGlobals in movie.h) -----------

MovieGlobals g;
double g_frame_done_at = 0;  // host: console time the MDEC finishes the current frame

// 0x8004f014, the MDEC output DMA's callback: the slice just transferred goes to its
// VRAM column and the next one is requested; after the last column the frame is ready.
void movie_dma_callback() {
  Loader* loader = &get_system()->loader;
  // A CD sector that arrived while the MDEC held the bus is read now.
  if (g.mode == 3 && loader_sector_deferred(loader)) cd_sector_ready_callback(loader, 1);
  const size_t slice_words = static_cast<size_t>(g.mode) * 0xF00 / 4;
  if (g.frames_in_ram == 0) psyq::LoadImage(&g.slice_rect, g.slice_buffers.data() + g.slice_half * slice_words);
  g.slice_half ^= 1;
  g.slice_rect.x = static_cast<s16>(g.slice_rect.x + g.mode * 8);
  if (g.slice_rect.x < g.x + g.mode * 0xA0) {
    if (g.frames_in_ram == 0) {
      psyq::DecDCTout(g.slice_buffers.data() + g.slice_half * slice_words, g.mode * 0x3C0);
    } else {
      // Frames in RAM: frame draw_buffer of the caller's buffer, stored as columns 16
      // pixels wide (the slice x counts halfwords, as in VRAM).
      const size_t at = static_cast<size_t>((g.draw_buffer * g.mode * 0x140 + g.slice_rect.x * 2) * 0xF0);
      psyq::DecDCTout(g.slice_buffers.data() + at / 4, g.mode * 0x3C0);
    }
  } else {
    g.frame_ready = 1;
    ++g.frame_count;
    if (g.mode == 3) loader_defer_sectors(loader, 0);
  }
}

// ---- Prologue support ----------------------------------------------------------------

// The vblank counter's handler (0x8004ff78) runs from the pad module's handler list
// (boot 0x800141bc / 0x80014230) on the console. The port keeps no handler list: the
// counter catches up from the runtime's vblank count when read, which gives the same
// values since it only counts and saturates.
void counter_catch_up(Prologue::VblankCounter* c) {
  const u64 now = Runtime::get().vblanks();
  for (; c->seen < now; ++c->seen) {
    if (++c->count > 4) c->count = 4;
  }
}

}  // namespace

// ---- Movie -----------------------------------------------------------------------------

Movie* movie_create(Movie* self, int x, int y, int w, int h, int interlace, int mode) {
  psyq::SetDefDispEnv(&self->disp[0], x, y + 0xF0, w, h);
  psyq::SetDefDispEnv(&self->disp[1], x, y, w, h);
  g.mode = static_cast<s16>(mode);
  self->dct_flags = 0;
  for (psyq::DISPENV& d : self->disp) {
    d.screen = RECT{0, static_cast<s16>((0xF0 - h) / 2), 0x100, static_cast<s16>(h)};
    g.height = static_cast<s16>(h);
  }
  g.x = static_cast<s16>(x);
  g.y = static_cast<s16>(y);
  g.width = static_cast<s16>(w);
  self->disp[0].isinter = self->disp[1].isinter = static_cast<u8>(interlace);
  self->disp[0].isrgb24 = self->disp[1].isrgb24 = g.mode == 3;
  psyq::DecDCTReset(0);
  self->loader = &get_system()->loader;
  g.draw_buffer = 0;
  g.frame_count = 0;
  self->frames_until_display = 1;
  self->rl_pending = 0;
  self->rl_index = 0;
  g.frame_ready = 0;
  g.decoding = 0;
  g.screen_cleared = 0;
  g.frames_in_ram = 0;
  g.loop_vblanks = 0;
  g.caller_idles = false;
  g.slice_buffers.clear();
  self->ring.clear();
  self->rl_buffers.clear();
  return self;
}

Movie::~Movie() {
  loader_stream_cancel(loader);
  loader_defer_sectors(loader, 0);
  ring = {};
  rl_buffers = {};
  g.slice_buffers = {};
  psyq::DecDCToutCallback(nullptr);
}

void movie_close(Movie* self) {
  loader_stream_cancel(self->loader);
  psyq::DecDCTReset(0);
  self->ring = {};
  self->rl_buffers = {};
  g.draw_buffer = 0;
  g.frame_count = 0;
  self->frames_until_display = 1;
  self->rl_pending = 0;
  self->rl_index = 0;
  g.frame_ready = 0;
  g.decoding = 0;
  g.screen_cleared = 0;
  psyq::DecDCToutCallback(nullptr);
}

void movie_open(Movie* self, int entry) {
  self->ring.assign(0xA000, 0);
  self->rl_buffers.assign(0x28000 / 4, 0);
  if (g.slice_buffers.empty()) g.slice_buffers.assign(static_cast<size_t>(g.mode) * 0x1E00 / 4, 0);
  loader_select_entry(self->loader, entry);
  const int size = self->loader->entry_size(entry);
  // Two slots of ten sectors (one frame), each sector's STR header kept in a table
  // at the slot's start. The entry's last 30 sectors are never read.
  loader_start_stream(self->loader, self->ring.data(), 1, 0x5000, 2, size - 0xF000);
  self->last_slot = 1;
  self->rl_index = 0;
  self->rl_decoded = 1;
  Runtime::get().trace("movie_open");
  psyq::DecDCToutCallback(movie_dma_callback);
  psyq::DecDCTvlcSize(0);
}

int movie_update(Movie* self) {
  stream_update();  // host: the sectors that have arrived by now
  if (loader_shell_open(self->loader)) {
    loader_stream_cancel(self->loader);
    return 0;
  }
  bool busy = false;  // host: whether the console spent time here
  const int slot = loader_stream_slot(self->loader);
  if (slot != self->last_slot && self->rl_decoded != self->rl_index) {
    if (g.screen_cleared == 0) {
      // Both display buffers to black before the first frame.
      const RECT all{g.x, g.y, static_cast<s16>(g.mode * 0xA0), 0x1E0};
      if (g.mode == 3 && g.frames_in_ram == 0) psyq::ClearImage(&all, 0, 0, 0);
      g.screen_cleared = 1;
    }
    self->last_slot = slot;
    // The slot holds a whole frame when its ten headers carry chunks 0-9 of one frame.
    u8* const base = self->ring.data() + static_cast<size_t>(slot) * 0x5000;
    u32 frame = 0;
    int chunk = 0;
    for (; chunk < 10; ++chunk) {
      const u8* header = base + chunk * 32;
      if (read_u16(header + 4) != chunk) break;
      if (chunk == 0) {
        frame = read_u16(header + 8);
      } else if (read_u16(header + 8) != frame) {
        break;
      }
    }
    if (chunk == 10) {
      u8* const bitstream = base + 0x140;
      const int size = psyq::DecDCTBufSize(reinterpret_cast<const u32*>(bitstream));
      if (size > 0x5000) std::fprintf(stderr, "error : %d\n", size);
      u32* const rl = self->rl_buffers.data() + static_cast<size_t>(self->rl_index) * (0x14000 / 4);
      // The console's DecDCTvlc decodes the whole bitstream; the host decoder needs the
      // frame's size for that, from the sector header (+0x10 / +0x12). It can differ
      // from the shown size: the field shows 320x240 movies 224 rows high.
      const int frame_w = read_u16(base + 0x10) != 0 ? read_u16(base + 0x10) : g.width;
      const int frame_h = read_u16(base + 0x12) != 0 ? read_u16(base + 0x12) : g.height;
      psyq::DecDCTvlc(reinterpret_cast<const u32*>(bitstream), rl, 0x5000 - 0x140, 0x14000 / 4, frame_w, frame_h);
      // Host: the CPU time of the decode, from the frame's size (sector header +0x0c: bitstream bytes).
      console_spend(cycles_to_vblanks(kVlcCyclesBase + kVlcCyclesPerCodeWord * size +
                                      kVlcCyclesPerByte * read_u32(base + 0x0C)));
      busy = true;
      self->rl_pending = 1;
      self->rl_decoded = self->rl_index;
    } else {
      ++g.frame_count;  // a frame with missing sectors is skipped but counted
    }
  }
  if (g.decoding != 1 && self->rl_pending != 0) {
    u32* const rl = self->rl_buffers.data() + static_cast<size_t>(self->rl_index) * (0x14000 / 4);
    psyq::DecDCTin(rl, (g.mode - 2) | self->dct_flags);
    self->rl_pending = 0;
    self->rl_index = (self->rl_index + 1) % 2;
    g.decoding = 1;
    // The frame goes out in columns 16 pixels wide, starting at the left of the
    // buffer that is not on screen.
    g.slice_rect.x = g.x;
    g.slice_rect.y = static_cast<s16>(g.draw_buffer != 0 ? g.y + 0xF0 : g.y);
    g.slice_half = 0;
    g.slice_rect.w = static_cast<s16>(g.mode * 8);
    g.slice_rect.h = g.height;
    if (g.mode == 3) loader_defer_sectors(self->loader, 1);
    // Frames in RAM: the first column of frame draw_buffer.
    const size_t first = g.frames_in_ram != 0 ? static_cast<size_t>(g.draw_buffer * g.mode * 0x12C00) / 4 : 0;
    psyq::DecDCTout(g.slice_buffers.data() + first, g.mode * 0x3C0);
    // Host: the frame went through at once; the console's MDEC takes a while, and
    // movie_present holds it back until then.
    g_frame_done_at = console_now() + cycles_to_vblanks(kMdecCyclesPerWord * g.mode * 0x3C0 * (0xA0 / 8));
    busy = true;
  }
  // Host: with nothing to do the console keeps polling until the drive delivers more
  // or the MDEC finishes.
  if (g.caller_idles) {
    // host: the caller waits (movie_next_event)
  } else if (g.loop_vblanks > 0) {
    console_spend(g.loop_vblanks);  // host: the caller's loop runs on while the player waits
  } else if (!busy) {
    if (g.frame_ready != 0 && console_now() < g_frame_done_at) {
      console_idle_until(std::min(g_frame_done_at, stream_next_event()));
    } else if (g.frame_ready == 0 && loader_stream_busy(self->loader)) {
      console_idle_until(stream_next_event());
    }
  }
  return loader_stream_busy(self->loader) != 0 || g.decoding != 0 || g.frame_ready != 0 ? 1 : 0;
}

void movie_present(Movie* self) {
  if (g.frame_ready != 1) return;
  if (console_now() < g_frame_done_at) return;  // host: the console's MDEC is not done yet
  psyq::DrawSync(0);
  psyq::VSync(0);
  g.draw_buffer ^= 1;
  psyq::PutDispEnv(&self->disp[g.draw_buffer]);
  Runtime::get().trace("movie_flip");
  g.frame_ready = 0;
  g.decoding = 0;
  if (self->frames_until_display != 0 && --self->frames_until_display == 0) psyq::SetDispMask(1);
}

s32 movie_frame_count() { return g.frame_count; }

MovieGlobals& movie_globals() { return g; }

bool movie_frame_ready() { return g.frame_ready != 0 && console_now() >= g_frame_done_at; }

void movie_console_spend(double vblanks) { console_spend(vblanks); }

double movie_console_now() { return console_now(); }

double movie_next_event(const Movie* self) {
  const double now = console_now();
  if (g.frame_ready != 0) return now < g_frame_done_at ? std::min(g_frame_done_at, stream_next_event()) : now;
  // A decoded frame waiting for the MDEC, or a slot not looked at yet: at once.
  if (self->rl_pending != 0 || g.decoding != 0) return now;
  if (loader_stream_slot(self->loader) != self->last_slot && self->rl_decoded != self->rl_index) return now;
  return stream_next_event();  // infinity once the stream has stopped
}

void movie_console_idle_until(double t) { console_idle_until(t); }

// ---- Prologue --------------------------------------------------------------------------

Prologue* prologue_create(Prologue* self) {
  movie_create(self, 0, 0, 0x140, 0xF0, 1, 3);
  self->has_movie = 0;
  self->clock = 0;
  self->clock_step = 0;
  self->cue = nullptr;
  self->faded_out = 0;
  self->keep_music = 0;
  self->counter = new Prologue::VblankCounter;
  self->counter->count = 0;
  self->sequence.clear();
  self->sound = &get_system()->sound;
  // pad_add_vblank_handler (boot 0x800141bc): see counter_catch_up.
  self->counter->seen = Runtime::get().vblanks();
  psyq::SetDispMask(0);
  return self;
}

void prologue_restart(Prologue* self) {
  self->has_movie = 0;
  self->clock = 0;
  self->clock_step = 0;
}

void prologue_set_script(Prologue* self, const u8* script) {
  self->script = script;
  self->cue = script;
}

void prologue_load_sound(Prologue* self, std::vector<u8>& sound_entry) {
  // The entry is a sound bank (at the offset its first word gives) and an SLZ block
  // of music right after that word, as for the title's own music (#2676).
  self->sequence.assign(0x8000, 0);
  sound_load_bank(self->sound, sound_entry.data() + read_u32(sound_entry.data()), 0, 0);
  if (auto music = disc::slz::unpack_block(std::span<const u8>(sound_entry).subspan(4), 0, 0, nullptr))
    std::copy_n(music->begin(), std::min(music->size(), self->sequence.size()), self->sequence.begin());
  while (sound_bank_busy(self->sound) != 0) {
  }
  sound_entry = {};
}

Prologue::~Prologue() {
  psyq::SetDispMask(0);
  System* sys = get_system();
  pad_vibrate(&sys->pad, 0, 0, 0);
  // pad_remove_vblank_handler (boot 0x80014230).
  delete counter;
  if (faded_out == 0) {
    sound_fade_out_effects(sound, 8);
    psyq::VSync(8);
  }
  for (int clip = 1; clip < 9; ++clip) sound_release_clip(sound, clip);
  psyq::VSync(8);
  if (keep_music == 0) sound_stop(sound);
  sequence = {};
}

int prologue_update(Prologue* self, int allow_cancel) {
  self->clock += self->clock_step;
  const u32 now = self->has_movie != 0 ? static_cast<u32>(g.frame_count) : self->clock;
  System* sys = get_system();
  const u16 pressed = pad_pressed(&sys->pad, 0);
  pad_clear(&sys->pad, 0);
  // Start or Circle skips, unless L1 is held too.
  if ((pressed & 0x0004) == 0 && (pressed & 0x0820) != 0 && allow_cancel != 0) return 2;
  // Cues: u32 time, u32 opcode, arguments. Every cue that is due runs.
  for (;;) {
    const u8* c = self->cue;
    if (now < read_u32(c)) break;
    const u32 op = read_u32(c + 4);
    self->cue = c + 8;
    switch (op) {
      case 0:  // end
        return 1;
      case 1: {  // movie: archive entry
        self->has_movie = 1;
        const int entry = static_cast<int>(read_u32(self->cue));
        self->cue += 4;
        movie_open(self, entry);
        break;
      }
      case 2:  // the music prologue_load_sound unpacked
        if (!self->sequence.empty()) sound_play_sequence(self->sound, self->sequence.data());
        break;
      case 3: {  // sound clip: offset in the script's entry, pan, volume, flag
        const u32 offset = read_u32(self->cue);
        const u32 pan = read_u32(self->cue + 4);
        const u32 volume = read_u32(self->cue + 8);
        const u32 flag = read_u32(self->cue + 12);
        self->cue += 16;
        const int clip = sound_open_clip(self->sound, self->script + offset, 2, nullptr);
        if (clip > 0)
          sound_start_clip(self->sound, clip, static_cast<int>(volume & 0xFF), static_cast<int>(pan & 0xFF), 0x2000,
                           static_cast<int>(flag & 0xFF));
        break;
      }
      case 4: {  // controller vibration: small motor, large motor
        const int small = static_cast<int>(read_u32(self->cue));
        const int large = static_cast<int>(read_u32(self->cue + 4));
        self->cue += 8;
        pad_vibrate(&sys->pad, 0, small, large);
        break;
      }
      case 5:  // fade the sound out over a number of frames (s16, padded to a word)
        sound_fade_out_effects(self->sound, static_cast<s16>(read_u16(self->cue)));
        self->cue += 4;
        self->faded_out = 1;
        break;
      default:  // unknown opcodes take no arguments
        break;
    }
  }
  if (self->has_movie != 0) movie_update(self);
  counter_catch_up(self->counter);
  self->clock_step = static_cast<u32>(self->counter->count);
  self->counter->count = 0;
  // Host: the console calls this in a loop. Unless a movie is streaming (movie_update
  // waits for the drive itself), only the next vblank can change anything once the
  // counter has been taken into the clock, so let the field end.
  const bool streaming = self->has_movie != 0 && (loader_stream_busy(self->loader) || g.frame_ready != 0);
  if (!streaming && self->clock_step == 0) console_idle_until(std::numeric_limits<double>::infinity());
  return 0;
}

}  // namespace ss::game
