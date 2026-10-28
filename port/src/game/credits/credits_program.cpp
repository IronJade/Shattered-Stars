#include "game/credits/credits_program.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <span>
#include <vector>

#include "disc/slz.h"
#include "game/boot/loader.h"
#include "game/boot/pad.h"
#include "game/boot/sound.h"
#include "game/common/program_image.h"
#include "game/title/movie.h"
#include "psx/gte.h"
#include "runtime/libgpu.h"
#include "runtime/libgte.h"
#include "runtime/region.h"
#include "runtime/runtime.h"

namespace ss::game::credits {
namespace {

using psyq::RECT;
using runtime::Runtime;

constexpr int kProgramEntry = 3107;

// Archive entries.
constexpr int kMusicEntry = 0x983;         // the ending theme (SLZ); its banks follow (0x984, 0x985)
constexpr int kLateBankEntry = 0x986;      // another bank, read at once, loaded when the music reaches bar 82
constexpr int kNamesEntry = 0x1197;        // #4503: the name images (SLZ)
constexpr int kFieldSoundEntry = 8;        // the field's sound bank, loaded before the field runs
constexpr s16 kLateBankBar = 0x52;

// Tables in the image (read at their original addresses).
// Cue list: {s16 op, s16 arg}. 0 end; 1 movie (archive entry; waits for the current
// one to finish); 2 wait (vblanks); 3 fade the music out (frames).
constexpr u32 kCueList = 0x80044290;
// Cards: s16 layout (0 centred, 1 top left, 2 top right, 3 bottom left, 4 bottom right),
// then codes: n > 0 the next n images, stacked; n < 0 colour -n (every second colour
// code leaves a 16-row gap first); -16 ends the card. The script ends with a card whose
// layout is -16 (the low half of kColours' first word). Each release has its own cards (its
// own staff, matching its #4503 name images; Europe 65 cards to NA's 78), so the script is
// read as a run in the release's own image from where it starts (program_run).
constexpr u32 kCardScript = 0x80044324;
constexpr u32 kColours = 0x8004466C;       // u32 0x00BBGGRR by -code; the default is code 1's

// Host: console time of work outside the movie player whose length matters to when
// frames go out. CPU cycles as PCSX-Redux counts them (see docs/port/notes/credits.md).
constexpr double kCpuHz = 33868800.0;
constexpr double kVblankHz = 60000.0 / 1001.0;
constexpr double kClearCycles = 0x3840 * 22.0;     // the loop clearing a frame (11 instructions an iteration)
constexpr double kGroupCycles = 14.0;              // compose: a 4-pixel group with no pixel
constexpr double kInkGroupCycles = 40.0;           // ... one with pixels to blend
constexpr double kPixelCycles = 50.0;              // ... per pixel blended (GTE GPL)
// The 20 LoadImages of a frame through the GPU's DMA, until the DrawSync callback.
constexpr double kUploadVblanks = 0.05;

double cycles_to_vblanks(double cycles) { return cycles * kVblankHz / kCpuHz; }

s16 image_s16(u32 address) {
  const u8* p = program_data(kProgramEntry, address);
  return p != nullptr ? static_cast<s16>(read_u16(p)) : 0;
}
u32 image_u32(u32 address) {
  const u8* p = program_data(kProgramEntry, address);
  return p != nullptr ? read_u32(p) : 0;
}

// The card script (see kCardScript); a read past it is the end (-16), so a short or
// unreadable script ends the credits' cards instead of running on.
std::span<const u8> g_cards;
s16 card_s16(u32 offset) {
  return offset + 2 <= g_cards.size() ? static_cast<s16>(read_u16(g_cards.data() + offset)) : -16;
}
// No card of any release has more codes than this; more means the script is misread.
constexpr int kCardCodesMax = 64;

// The program object (0x28 bytes, vtable 0x80044268; 0x8002f8dc points at it).
struct Credits {
  System* system = nullptr;   // +0x08 (set_system 0x8002f890; get_system 0x8002f898)
  u32 card = 0;               // +0x14: the card being shown (the original: its address; here its offset in the script)
  s32 fade = 0;               // +0x18: 0 fading in, 1 shown or fading out, 2 gone
  s32 level = 0;              // +0x1c: brightness, 0 to 0x1000 in steps of 0x200 a frame
  std::vector<u8> names;      // +0x20: #4503 unpacked
  size_t next_image = 0;      // +0x24: the card's first image (offset in names)
};

// The vblank handler (12 bytes, vtable 0x80044248: tick 0x80043aac).
struct Ticker : VblankHandler {
  s32 countdown = 4;  // +8: the display flips only once this is below 1, then waits 4 more
  void vblank(s32 arg) override;
};

struct Globals {
  Movie* movie = nullptr;         // 0x8004467c
  psyq::DISPENV disp[2]{};        // 0x80044680: set up and cleared, never shown
  s32 wait = 0;                   // 0x800446a8: vblanks before the next cue
  s32 unused = 0;                 // 0x800446ac: cleared, never read
  s32 display_countdown = 0;      // 0x800446b0: SetDispMask(1) at the first flip
  s32 movie_state = 0;            // 0x800446b4: 0 none, 1 opened, 2 its frames are coming
  u8 shown = 0xFF;                // 0x800446b8: VRAM buffer on screen (0 = y 240, 1 = y 0)
  u8 uploaded = 0xFF;             // 0x800446b9: VRAM buffer uploaded last (the DrawSync callback)
  s32 hold = 0;                   // 0x800446c0: vblanks a card stays up, then the gap after it
  s32 compose_frame = 0;          // 0x80044280: RAM frame compose writes into
  s32 upload_frame = -1;          // 0x80044284: RAM frame the handler uploads next
  s32 upload_buffer = 0;          // 0x80044288: VRAM buffer the next upload goes to
  s32 upload_pending = 0;         // 0x8004428c
  bool drawsync_on = false;       // DrawSyncCallback(0x800436f0) installed
  double drawsync_due = -1;       // host: console time the uploads finish, or < 0
  // The scratchpad tables (0x1f800000) compose fills for the draw.
  s32 colour[3][16]{};            // 0x1f800000 / 0x40 / 0x80: the card colour by brightness index
  s32 keep[16]{};                 // 0x1f8000c0: weight (4.12) of the frame under each index
  s32 add[16][4]{};               // 0x1f800100: colour * level by index (16 bytes each)
};
Globals g;

u8* frames() { return reinterpret_cast<u8*>(movie_globals().slice_buffers.data()); }

// 0x800436f0 (DrawSyncCallback): the frame is in VRAM; the handler may show it.
void drawsync_callback() {
  Runtime::get().trace("credits_drawsync");
  g.uploaded = static_cast<u8>(g.upload_buffer);
  g.upload_buffer ^= 1;
}

// Host: the console's GPU finishes the uploads a little after the handler queued them.
void drawsync_if_due() {
  if (g.drawsync_due >= 0 && movie_console_now() >= g.drawsync_due) {
    g.drawsync_due = -1;
    if (g.drawsync_on) drawsync_callback();
  }
}

void Ticker::vblank(s32) {
  // Host: uploads queued in the last field have finished by now.
  if (g.drawsync_due >= 0) {
    g.drawsync_due = -1;
    if (g.drawsync_on) drawsync_callback();
  }
  const s32 left = --countdown;
  if (g.uploaded != 0xFF && g.shown != g.uploaded && left < 1) {
    do countdown += 4;
    while (countdown < 1);
    if (g.display_countdown != 0 && --g.display_countdown != 0) psyq::SetDispMask(1);
    psyq::PutDispEnv(&g.movie->disp[g.uploaded]);
    g.shown = g.uploaded;
    Runtime::get().trace("credits_flip");
  }
  if (g.upload_pending != 0 && g.uploaded == g.shown) {
    // The frame in 20 columns 16 pixels wide, into the buffer not on screen.
    RECT r{0, static_cast<s16>(g.upload_buffer == 0 ? 0xF0 : 0), 0x18, 0xF0};
    for (int i = 0; i < 20; ++i) {
      psyq::LoadImage(&r, frames() + static_cast<size_t>(g.upload_frame * 0x3C0 + i * 0x30) * 0xF0);
      r.x = static_cast<s16>(r.x + 0x18);
    }
    g.upload_pending = 0;
    g.drawsync_due = static_cast<double>(Runtime::get().vblanks() + 1) + kUploadVblanks;
  }
  if (g.wait > 0) --g.wait;
  if (g.hold > 0) --g.hold;
}

// 0x80043cb4: the card colour at 15 brightness steps (index 1 full, 15 none), 12.4.
void set_palette(int r, int gr, int b) {
  g.colour[0][0] = g.colour[1][0] = g.colour[2][0] = 0;
  for (int i = 1; i < 16; ++i) {
    const int k = 15 - i;
    g.colour[0][i] = (r * k) / 14 * 16;
    g.colour[1][i] = (gr * k) / 14 * 16;
    g.colour[2][i] = (b * k) / 14 * 16;
  }
}

// 0x80043db4: the colours at 'level' (4.12), and how much of the frame each index
// keeps: all of it at index 1 (the colour is added), less further out.
void set_level(s32 level) {
  g.keep[0] = 0x1000 - level;
  for (int i = 1; i < 16; ++i) {
    for (int c = 0; c < 3; ++c) g.add[i][c] = static_cast<s32>(static_cast<u32>(g.colour[c][i] * level) >> 12);
    const s32 k = 0x1000 - ((i - 1) * level * 5) / 0x38;
    g.keep[i] = k < 0 ? 0 : k;
  }
}

// 0x80043eac: one name image into RAM frame compose_frame at (x, y), x a multiple of 4.
// Images are u16 width, u16 height, then a u16 for every 4 pixels: a 4-bit index per
// pixel, 0 = untouched; otherwise the GTE's GPL (sf=1) gives add + frame * keep.
// Returns the host's cycle estimate.
double draw_image(s32 x, s32 y, const u8* image) {
  const int w = read_u16(image), h = read_u16(image + 2);
  const u8* codes = image + 4;
  u8* frame = frames() + static_cast<size_t>(g.compose_frame) * 0x38400;
  psx::Gte& gte = psyq::gte();
  double cycles = 0;
  for (s32 row = y; row < y + h; ++row) {
    for (s32 col = x; col < x + w; col += 4) {
      const u16 code = read_u16(codes);
      codes += 2;
      if (code == 0) {
        cycles += kGroupCycles;
        continue;
      }
      cycles += kInkGroupCycles;
      u8* px = frame + row * 0x30 + (col & 0xF) * 3 + (col >> 4) * 0x2D00;
      for (int k = 0; k < 4; ++k, px += 3) {
        const int n = (code >> (4 * k)) & 0xF;
        if (n == 0) continue;
        cycles += kPixelCycles;
        gte.write_data(psx::Gte::MAC1, static_cast<u32>(g.add[n][0]));
        gte.write_data(psx::Gte::MAC2, static_cast<u32>(g.add[n][1]));
        gte.write_data(psx::Gte::MAC3, static_cast<u32>(g.add[n][2]));
        gte.write_data(psx::Gte::IR0, static_cast<u32>(g.keep[n]));
        gte.write_data(psx::Gte::IR1, static_cast<u32>(px[0]) << 4);
        gte.write_data(psx::Gte::IR2, static_cast<u32>(px[1]) << 4);
        gte.write_data(psx::Gte::IR3, static_cast<u32>(px[2]) << 4);
        gte.execute(0x1A8003E);  // GPL, sf=1
        const u32 rgb = gte.read_data(psx::Gte::RGB2);
        px[0] = static_cast<u8>(rgb);
        px[1] = static_cast<u8>(rgb >> 8);
        px[2] = static_cast<u8>(rgb >> 16);
      }
    }
  }
  return cycles;
}

// 0x80043714: the current card into RAM frame compose_frame, and its fade a step on.
void compose(Credits* self) {
  Runtime::get().trace("credits_compose");
  if (self->fade > 1) {
    // Gone: the next card starts once the gap after this one is over.
    if (g.hold == 0) self->fade = 0;
    return;
  }
  const s16 layout = card_s16(self->card);
  if (layout == -16) return;  // the end of the script
  struct Placed {
    s32 x, y;
    s32 w;
    u32 colour;
    size_t image;
  };
  Placed placed[12];  // the console's stack arrays hold 12
  int count = 0;
  int at = 1;  // halfwords into the card
  bool second = false;
  s32 height = 0;
  u32 colour = image_u32(kColours + 4);
  size_t image = self->next_image;
  for (;;) {
    const s16 code = card_s16(self->card + static_cast<u32>(at) * 2);
    ++at;
    if (code == -16) break;
    if (at > kCardCodesMax) {
      Runtime::get().log_once("credits-card-long", "credits: a card of the script has no end; the cards stop");
      g_cards = {};
      break;
    }
    if (code < 0) {
      colour = image_u32(kColours + static_cast<u32>(-code) * 4);
      second = !second;
      if (!second) height += 0x10;
      continue;
    }
    for (int i = 0; i < code; ++i) {
      if (image + 4 > self->names.size() || count == 12) {
        Runtime::get().log_once("credits-card", "credits: card script runs past its images");
        break;
      }
      const int w = read_u16(&self->names[image]);
      const int h = read_u16(&self->names[image + 2]);
      placed[count++] = Placed{0, height, w, colour, image};
      height += h;
      image += static_cast<size_t>((w * h) / 4 + 2) * 2;
    }
  }
  for (int i = 0; i < count; ++i) {
    Placed& p = placed[i];
    switch (layout) {
      case 0:
        p.x = p.x + 0xA0 - p.w / 2;
        p.y = p.y + 0x70 - height / 2;
        break;
      case 1:
        p.x += 0x10;
        p.y += 0x10;
        break;
      case 2:
        p.x = p.x + 0x134 - p.w;
        p.y += 0x10;
        break;
      case 3:
        p.x += 0x10;
        p.y = p.y + 0xE0 - height;
        break;
      case 4:
        p.x = p.x + 0x134 - p.w;
        p.y = p.y + 0xE0 - height;
        break;
      default:
        break;
    }
    p.x &= ~3;
  }
  double cycles = 0;
  for (int i = 0; i < count; ++i) {
    const Placed& p = placed[i];
    if (i == 0 || p.colour != placed[i - 1].colour) {
      set_palette(static_cast<int>(p.colour & 0xFF), static_cast<int>((p.colour >> 8) & 0xFF),
                  static_cast<int>(p.colour >> 16));
      set_level(self->level);
    }
    cycles += draw_image(p.x, p.y, &self->names[p.image]);
  }
  movie_console_spend(cycles_to_vblanks(cycles));
  if (self->fade == 0) {
    self->level += 0x200;
    if (self->level >= 0x1000) {
      g.hold = 0xCF;
      self->level = 0x1000;
      self->fade = 1;
    }
  } else if (g.hold == 0) {
    self->level -= 0x200;
    if (self->level <= 0) {
      self->fade = 2;
      g.hold = 5;
      self->level = 0;
      self->next_image = image;
      self->card += static_cast<u32>(at) * 2;
    }
  }
}

std::vector<u8> unpack(std::span<const u8> data) {
  auto out = disc::slz::unpack_block(data, 0, 0, nullptr);
  return out ? std::move(*out) : std::vector<u8>{};
}

// 0x80042ea0 (vtable +0x0c): the whole program.
[[noreturn]] void credits_run(Credits* self) {
  for (int i = 0; i < 2; ++i) {
    psyq::SetDefDispEnv(&g.disp[i], 0x1E0, i * 0xF0, 0x140, 0xF0);
    g.disp[i].screen = RECT{0, 0, 0x100, 0xF0};
    g.disp[i].isinter = 1;
    g.disp[i].isrgb24 = 1;
    const RECT r{0x1E0, static_cast<s16>(i == 0 ? 0 : 0xF0), 0x1E0, 0xF0};
    psyq::ClearImage(&r, 0, 0, 0);
  }
  System* sys = get_system();
  Loader* loader = &sys->loader;
  Sound* sound = &sys->sound;
  Pad* pad = &sys->pad;
  // The music: the theme and its first two banks in one read, then the third bank
  // (read now, loaded later).
  std::vector<u8> sequence(0xC000, 0);
  const int music_bytes = loader->entry_size(kMusicEntry);
  const int bank1_bytes = loader->entry_size(kMusicEntry + 1);
  const int total = music_bytes + bank1_bytes + loader->entry_size(kMusicEntry + 2);
  {
    std::vector<u8> music(static_cast<size_t>(std::max(total, 0)), 0);
    loader->load_sync(music.data(), kMusicEntry, total);
    sound_load_bank(sound, music.data() + music_bytes, 0, 0);
    std::vector<u8> late_bank(static_cast<size_t>(std::max(loader->entry_size(kLateBankEntry), 0)), 0);
    loader->select_entry(kLateBankEntry);
    loader->start_read(late_bank.data(), 0);
    while (sound_bank_busy(sound) != 0) {
    }
    sound_load_bank(sound, music.data() + music_bytes + bank1_bytes, 0, 0);
    const std::vector<u8> theme = unpack(std::span<const u8>(music).first(static_cast<size_t>(music_bytes)));
    std::copy_n(theme.begin(), std::min(theme.size(), sequence.size()), sequence.begin());
    while (sound_bank_busy(sound) != 0) {
    }
    sound_play_sequence(sound, sequence.data());
    while (loader->poll() != 0) {
    }
    // The names.
    std::vector<u8> packed(static_cast<size_t>(std::max(loader->entry_size(kNamesEntry), 0)), 0);
    loader->select_entry(kNamesEntry);
    loader->start_read(packed.data(), 0);
    while (loader->poll() != 0) {
    }
    self->names = unpack(packed);
    while (loader->poll() != 0) {
    }
    g.movie = movie_create(new Movie, 0, 0, 0x140, 0xF0, 1, 3);
    MovieGlobals& mg = movie_globals();
    mg.frames_in_ram = 3;
    mg.slice_buffers.assign(0xA8C00 / 4, 0);  // three frames
    mg.caller_idles = true;  // host: the loop below waits, after its own checks
    psyq::SetDispMask(0);

    u32 blank = 0;  // the RAM frame (0 or 1) for the next black frame
    bool late_bank_loaded = false;
    g.display_countdown = 2;
    g.unused = 0;
    g.movie_state = 0;
    g.wait = 0;
    auto* ticker = new Ticker;
    ticker->countdown = 4;
    pad_add_vblank_handler(pad, ticker, 0);
    self->next_image = 0;
    g.drawsync_on = true;  // DrawSyncCallback(0x800436f0)
    u32 cue = kCueList;
    g.upload_frame = -1;
    g.uploaded = 0xFF;
    g.shown = 0xFF;
    self->card = 0;
    g_cards = program_run(kProgramEntry, kProgramBase, kCardScript);
    self->fade = 0;
    self->level = 0;
    g.hold = 0;
    Runtime::get().trace("credits_start");
    for (;;) {
      drawsync_if_due();
      u32 next_blank = blank;
      const u32 cue_before = cue;
      const u64 vblank_before = Runtime::get().vblanks();
      if (g.wait == 0) {
        const s16 op = image_s16(cue);
        const s16 arg = image_s16(cue + 2);
        if (std::getenv("SS_CREDITS_CUES") != nullptr)
          std::fprintf(stderr, "[credits] cue %d %d at %.3f\n", op, arg, movie_console_now());
        if (op == 0) break;
        // (A PAL release's waits and fades count 50 Hz vblanks: Europe waits 600 where NA
        // waits 720. The port runs at 60, so they are converted to last as long.)
        if (op == 2) {
          g.wait = region::ntsc_vblanks(arg);
          cue += 4;
        } else if (op == 3) {
          sound_fade_out(sound, region::ntsc_vblanks(arg));
          cue += 4;
        } else if (op == 1 && g.movie_state == 0) {
          cue += 4;
          mg.draw_buffer = 2;  // the first frame goes to RAM frame 2
          movie_open(g.movie, arg);
          g.movie_state = 1;
        }
      }
      if (g.movie_state != 0 && movie_update(g.movie) != 1) {
        g.movie_state = 0;
        movie_close(g.movie);
        Runtime::get().trace("credits_movie_end");
        next_blank = mg.draw_buffer == 0 ? 1 : 0;
      }
      if (movie_frame_ready() && g.movie_state != 0) {
        // A movie frame is finished in RAM: the names go on it, and it is the next to show.
        if (g.movie_state == 1) {
          g.movie_state = 2;
          Runtime::get().trace("credits_movie_frames");
        }
        g.compose_frame = mg.draw_buffer;
        compose(self);
        mg.frame_ready = 0;
        mg.decoding = 0;
        g.upload_pending = 1;
        g.upload_frame = mg.draw_buffer;
        mg.draw_buffer = (mg.draw_buffer + 1) % 3;
      }
      drawsync_if_due();
      blank = next_blank;
      if (g.movie_state != 2 && g.upload_pending == 0 && g.uploaded == g.shown) {
        // No movie frame: the names on black, once the last frame is on screen.
        std::memset(frames() + static_cast<size_t>(next_blank) * 0x38400, 0, 0x38400);
        movie_console_spend(cycles_to_vblanks(kClearCycles));
        g.compose_frame = static_cast<s32>(next_blank);
        compose(self);
        blank = next_blank ^ 1;
        g.upload_pending = 1;
        g.upload_frame = static_cast<s32>(next_blank);
      }
      if (!late_bank_loaded && sound->bar >= kLateBankBar) {
        late_bank_loaded = true;
        sound_load_bank(sound, late_bank.data(), 0, 0);
        while (sound_bank_busy(sound) != 0) {
        }
        late_bank = {};
      }
      // Host: the loop spins until the player has something to do, the uploads finish
      // or the field ends (the vblank handler may change what it sees). A cue read in
      // this pass may be followed by another at once, and so may anything the vblank
      // handler changed while the pass ran.
      if (cue != cue_before || Runtime::get().vblanks() != vblank_before) continue;
      double until = g.movie_state != 0 ? movie_next_event(g.movie) : std::numeric_limits<double>::infinity();
      if (g.drawsync_due >= 0) until = std::min(until, g.drawsync_due);
      movie_console_idle_until(until);
    }
    // The end: back to the field.
    Runtime::get().trace("credits_end");
    loader->cancel();
    sound_stop(sound);
    while (loader->poll() != 0) {
    }
    g.drawsync_on = false;  // DrawSyncCallback(0)
    g.drawsync_due = -1;
    pad_remove_vblank_handler(pad, ticker);
    delete g.movie;
    g.movie = nullptr;
    sequence = {};
    self->names = {};
    delete ticker;
    psyq::SetDispMask(0);
  }
  {
    std::vector<u8> bank(static_cast<size_t>(std::max(loader->entry_size(kFieldSoundEntry), 0)), 0);
    loader->select_entry(kFieldSoundEntry);
    loader->start_read(bank.data(), 0);
    while (loader->poll() != 0) {
    }
    sound_load_bank(sound, bank.data(), 0, 0);
    while (sound_bank_busy(sound) != 0) {
    }
  }
  // The field program (#3100, entry 0xc1c) loaded and run; the console also frees the
  // buffer 0x800100dc returns first.
  boot_run_main_program(sys, Program::Field);
}

}  // namespace

void credits_entry(System* sys) {
  Credits self;
  self.system = sys;
  g = Globals{};
  if (program_data(kProgramEntry, kCueList) == nullptr) {
    std::fprintf(stderr, "[credits] program #%d is not readable; back to the field\n", kProgramEntry);
    boot_run_main_program(sys, Program::Field);
  }
  // The movies are on disc 2 only (the field asks for it long before the endings).
  if (sys->loader.entry_size(4594) == 0) loader_insert_disc(sys->loader, 2);
  credits_run(&self);
}

}  // namespace ss::game::credits
