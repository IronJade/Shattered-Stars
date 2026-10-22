#include "runtime/libspu.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "runtime/boot_exe.h"
#include "runtime/libapi.h"

namespace ss::psyq {
namespace {

using psx::Spu;

// libspu's globals (boot 0x8002b6f0-0x8002b7d0).
struct LibSpu {
  bool called = false;               // _spu_isCalled (SpuStart ran)
  u32 keystat = 0;                   // _spu_keystat
  long trans_mode = SPU_TRANSFER_BY_DMA;  // _spu_trans_mode
  u32 tsa = 0;                       // _spu_tsa: transfer start address / 8
  bool in_transfer = false;          // _spu_inTransfer
  SpuTransferCallbackProc transfer_callback = nullptr;  // _spu_transferCallback
  long rev_flag = 0;                 // _spu_rev_flag
  long rev_reserve_wa = 0;           // _spu_rev_reserve_wa
  u32 rev_offsetaddr = 0;            // _spu_rev_offsetaddr (/ 8)
  SpuReverbAttr rev_attr;            // _spu_rev_attr (mode, depth, delay, feedback)
  u16 voice_center_note[24] = {};    // _spu_voice_centerNote
  u8* malloc_table = nullptr;        // _spu_memList (SpuInitMalloc's 'top')
  long malloc_blocks = 0;
  SpuIRQCallbackProc irq_callback = nullptr;  // _spu_IRQCallback
  // Host: an SPU interrupt raised during the last sample, and a DMA transfer (started
  // by the streaming library) whose completion interrupt is still to come.
  bool irq_pending = false;
  bool dma_pending = false;
};
LibSpu g;

// The streaming library's globals (boot 0x8002bbd4-0x8002bbf8, 0x8002d790-0x8002da58).
struct SpuSt {
  long state = 0;           // 0x8002bbd4: 0, 0x10 idle, 0x2x preparing, 0x3x playing, 0x4x final
  u32 voices = 0;           // 0x8002bbd8: voices streaming (SpuStGetVoiceStatus)
  u32 transfer_voices = 0;  // 0x8002bbdc: voices still to transfer this round
  u32 add_voices = 0;       // 0x8002bbe0: SPU_ST_START while playing: to key on
  u32 prepare_pending = 0;  // 0x8002bbe4: voices still to prepare
  u32 prepare_mask = 0;     // 0x8002bbe8: voices of the preparation (for its callback)
  u32 add_mask = 0;         // 0x8002bbec: prepared while playing, may be started
  u32 final_voices = 0;     // 0x8002bbf0: voices whose last chunk went out
  u32 finished = 0;         // 0x8002bbf4: final voices of the previous round
  u32 irq_addr = 0;         // 0x8002d790
  SpuStCallbackProc transfer_cb = nullptr, prepare_cb = nullptr, finished_cb = nullptr;  // 0x8002d794..9c
  SpuTransferCallbackProc saved_transfer_cb = nullptr;  // 0x8002d7a0
  SpuIRQCallbackProc saved_irq_cb = nullptr;            // 0x8002d7a4
  int irq_voice = 0x18;       // 0x8002d7a8: the voice the IRQ address follows (0x18 none)
  int transfer_voice = 0x18;  // 0x8002d7ac
  int prepare_voice = 0x18;   // 0x8002d7b0
  int final_voice = 0x18;     // 0x8002d7b8
  SpuStEnv env;               // 0x8002d7c0
  s32 half = 0;               // 0x8002d948: env.size / 2
  int second_half = 0;        // 0x8002d94c: the half the next transfer fills (1 = second)
  int prepare_lock = 0;       // 0x8002d950
  int add_lock = 0;           // 0x8002d954
  u8 saved_block[24][16] = {};  // 0x8002d958: the final chunks' last blocks
};
SpuSt st;

constexpr u32 kMemModePlus = 3;  // _spu_mem_mode_plus: addresses are in 8-byte units

// _spu_RXX[index] = value (index in halfwords from 0x1F801C00).
void reg_w(u32 index, u16 value) { spu().write16(index * 2, value); }
u16 reg_r(u32 index) { return spu().read16(index * 2); }

void log_once(const char* key) {
  static std::vector<const char*> seen;
  if (std::find(seen.begin(), seen.end(), key) != seen.end()) return;
  seen.push_back(key);
  std::fprintf(stderr, "[port] libspu: %s\n", key);
}

// _spu_FsetRXXa: an address rounded up to 8 bytes, stored / 8 in register 'index'
// (index -1: just returned).
u32 fset_rxxa(long index, u32 addr) {
  if (addr & 7) addr += 8;
  const u32 v = (addr >> kMemModePlus) & 0xFFFF;
  if (index >= 0) reg_w(static_cast<u32>(index), static_cast<u16>(v));
  return v;
}

// _SpuIsInAllocateArea_: whether 'addr' (/ 8) falls inside a block SpuMalloc handed out.
bool in_allocated_area(u32 addr) {
  if (!g.malloc_table) return false;
  const u32 a = addr << kMemModePlus;
  for (const u8* p = g.malloc_table;; p += 8) {
    const u32 w = read_u32(p);
    if (w & 0x80000000) continue;  // unused entry
    if (w & 0x40000000) return false;  // the free tail: end of the list
    const u32 start = w & 0x0FFFFFFF;
    if (a <= start) return true;
    if (a < start + read_u32(p + 4)) return true;
  }
}

// The reverb presets live in libspu's data inside the boot executable:
// _spu_rev_startaddr (10 u32 at 0x8002bc04) and _spu_rev_param (10 x 0x44 bytes at
// 0x8002bc34: a u32 register mask, then the 32 reverb registers).
struct ReverbParam {
  u32 flags = 0;
  u16 reg[32] = {};
};
u32 rev_startaddr(u32 mode) {
  const u8* p = runtime::boot_exe_data(0x8002BC04 + mode * 4);
  if (!p) {
    log_once("no boot executable: reverb presets unavailable");
    return 0xFFFE;
  }
  return read_u32(p);
}
ReverbParam rev_param(u32 mode) {
  ReverbParam r;
  if (const u8* p = runtime::boot_exe_data(0x8002BC34 + mode * 0x44)) {
    r.flags = read_u32(p);
    for (int i = 0; i < 32; ++i) r.reg[i] = read_u16(p + 4 + i * 2);
  }
  return r;
}
enum : int { kDAPF1 = 0, kDAPF2 = 1, kVWALL = 7, kMLSAME = 10, kMRSAME = 11, kMLCOMB1 = 12, kMRCOMB1 = 13,
             kDLSAME = 16, kDRSAME = 17, kMLAPF1 = 26, kMRAPF1 = 27, kMLAPF2 = 28, kMRAPF2 = 29 };

// _spu_setReverbAttr: writes the registers selected by 'flags' (0 = all).
void set_reverb_attr(const ReverbParam& p) {
  for (u32 i = 0; i < 32; ++i)
    if (p.flags == 0 || (p.flags & (1u << i))) reg_w(0xE0 + i, p.reg[i]);
}

// _SpuSetAnyVoice: a 24-bit voice mask split over two registers.
u32 set_any_voice(long on_off, u32 voice_bit, u32 lo, u32 hi) {
  const u16 l = static_cast<u16>(voice_bit), h = static_cast<u16>((voice_bit >> 16) & 0xFF);
  switch (on_off) {
    case SPU_OFF:
      reg_w(lo, reg_r(lo) & static_cast<u16>(~l));
      reg_w(hi, reg_r(hi) & static_cast<u16>(~h));
      break;
    case SPU_ON:
      reg_w(lo, reg_r(lo) | l);
      reg_w(hi, reg_r(hi) | h);
      break;
    case SPU_BIT:
      reg_w(lo, l);
      reg_w(hi, h);
      return voice_bit & 0xFFFFFF;
    default: return static_cast<u32>(-2);
  }
  return (reg_r(lo) | static_cast<u32>(reg_r(hi)) << 16) & 0xFFFFFF;
}

// Volume register from a libspu volume and mode (SPU_VOICE_DIRECT = 0, then the seven
// sweep modes LinearIncN .. ExpDec).
u16 volume_reg(s16 volume, s16 mode, bool with_mode) {
  static constexpr u16 kModes[7] = {0x8000, 0x9000, 0xA000, 0xB000, 0xC000, 0xD000, 0xE000};
  u16 bits = 0;
  if (with_mode && mode >= 1 && mode <= 7) bits = kModes[mode - 1];
  u16 v = static_cast<u16>(volume) & 0x7FFF;
  if (bits) {
    if (volume > 0x7F) v = 0x7F;
    else if (volume < 0) v = 0;
  }
  return static_cast<u16>(v | bits);
}

}  // namespace

void (*spu_trace_key)(long on_off, u32 voice_bit) = nullptr;
void (*spu_trace_voice)(const SpuVoiceAttr* attr) = nullptr;
void (*spu_trace_stream)(const char* what, u32 a, u32 b) = nullptr;

psx::Spu& spu() {
  static psx::Spu instance;
  static const bool hooked = (instance.on_irq = [] { g.irq_pending = true; }, true);
  (void)hooked;
  return instance;
}

// SpuInit -> _SpuInit(0) -> _spu_init(0), SpuStart.
void SpuInit() {
  g.tsa = 0;
  g.trans_mode = SPU_TRANSFER_BY_DMA;
  reg_w(0xC0, 0);  // main volume
  reg_w(0xC1, 0);
  reg_w(0xD5, 0);  // SPUCNT
  reg_w(0xD6, 4);  // transfer control: normal
  reg_w(0xC2, 0);  // reverb output volume
  reg_w(0xC3, 0);
  reg_w(0xC6, 0xFFFF);  // key off everything
  reg_w(0xC7, 0xFFFF);
  reg_w(0xCC, 0);  // no reverb
  reg_w(0xCD, 0);
  // mode 0: clear the rest, park every voice on a silent block at 0x1000.
  g.tsa = 0x200;
  for (u32 r = 0xC8; r <= 0xCB; ++r) reg_w(r, 0);  // pitch modulation, noise
  for (u32 r = 0xD8; r <= 0xDB; ++r) reg_w(r, 0);  // CD and external input volume
  reg_w(0xD3, static_cast<u16>(g.tsa));            // _spu_writeByIO(16 zero bytes)
  for (int i = 0; i < 8; ++i) reg_w(0xD4, 0);
  for (u32 v = 0; v < 24; ++v) {
    reg_w(v * 8 + 0, 0);
    reg_w(v * 8 + 1, 0);
    reg_w(v * 8 + 2, 0x3FFF);
    reg_w(v * 8 + 3, 0x200);
    reg_w(v * 8 + 4, 0);
    reg_w(v * 8 + 5, 0);
  }
  reg_w(0xC4, 0xFFFF);  // key on, then off, every voice
  reg_w(0xC5, 0xFF);
  reg_w(0xC6, 0xFFFF);
  reg_w(0xC7, 0xFF);
  g.in_transfer = true;
  reg_w(0xD5, 0xC000);  // SPU on, unmuted
  g.transfer_callback = nullptr;
  for (auto& n : g.voice_center_note) n = 0xC000;
  g.called = true;  // SpuStart
  g.rev_flag = 0;
  g.rev_reserve_wa = 0;
  g.rev_attr = SpuReverbAttr{};
  g.rev_offsetaddr = rev_startaddr(0);
  reg_w(0xD1, static_cast<u16>(g.rev_offsetaddr));  // reverb work area start
  g.keystat = 0;
}

void SpuQuit() {
  if (!g.called) return;
  g.called = false;
  g.transfer_callback = nullptr;
}

long SpuSetMute(long on_off) {
  if (on_off == SPU_ON) {
    reg_w(0xD5, reg_r(0xD5) & 0xBFFF);
    return 1;
  }
  if (on_off == SPU_OFF) {
    reg_w(0xD5, reg_r(0xD5) | 0x4000);
    return 0;
  }
  return -1;
}

long SpuInitMalloc(long num, char* top) {
  if (num < 1) return 0;
  auto* t = reinterpret_cast<u8*>(top);
  const u32 first = 0x40000000u | 0x1010;  // the free tail, from 0x1010
  const u32 size = (0x10000u << kMemModePlus) - 0x1010;
  std::memcpy(t, &first, 4);
  std::memcpy(t + 4, &size, 4);
  g.malloc_table = t;
  g.malloc_blocks = num;
  return num;
}

long SpuSetTransferMode(long mode) {
  g.trans_mode = mode;
  return mode == SPU_TRANSFER_BY_IO ? 1 : 0;
}

u32 SpuSetTransferStartAddr(u32 addr) {
  if (addr - 0x1010 > 0x7EFE8) return 0;
  g.tsa = fset_rxxa(-1, addr);
  return g.tsa << kMemModePlus;
}

// _spu_Fw: DMA (channel 4, 64-byte blocks) or manual FIFO writes from the transfer
// start address. The DMA completes at once on the host; its interrupt handler
// (_spu_FiDMA) clears the transfer mode and calls the transfer callback.
u32 SpuWrite(const u8* addr, u32 size) {
  if (size > 0x7EFF0) size = 0x7EFF0;
  Spu& s = spu();
  reg_w(0xD3, static_cast<u16>(g.tsa));  // _spu_t(2): transfer address
  std::vector<u16> words((size + 1) / 2, 0);
  std::memcpy(words.data(), addr, size);
  if (g.trans_mode == SPU_TRANSFER_BY_DMA) {
    reg_w(0xD5, static_cast<u16>((reg_r(0xD5) & ~0x30) | 0x20));  // _spu_t(1): DMA write
    s.dma_write(words.data(), words.size());
    reg_w(0xD5, reg_r(0xD5) & static_cast<u16>(~0x30));  // _spu_FiDMA
    if (g.transfer_callback) {
      g.transfer_callback();
    } else {
      g.in_transfer = false;
    }
  } else {
    for (u16 w : words) reg_w(0xD4, w);
    g.in_transfer = false;
  }
  return size;
}

u32 SpuRead(u8* addr, u32 size) {
  if (size > 0x7EFF0) size = 0x7EFF0;
  reg_w(0xD3, static_cast<u16>(g.tsa));
  std::vector<u16> words((size + 1) / 2, 0);
  spu().dma_read(words.data(), words.size());
  std::memcpy(addr, words.data(), size);
  if (g.transfer_callback) g.transfer_callback();
  else g.in_transfer = false;
  return size;
}

long SpuIsTransferCompleted(long flag) {
  (void)flag;
  g.in_transfer = false;
  return 1;
}

SpuTransferCallbackProc SpuSetTransferCallback(SpuTransferCallbackProc func) {
  const SpuTransferCallbackProc old = g.transfer_callback;
  g.transfer_callback = func;
  return old;
}

void SpuSetKey(long on_off, u32 voice_bit) {
  voice_bit &= 0xFFFFFF;
  if (spu_trace_key) spu_trace_key(on_off, voice_bit);
  const u16 lo = static_cast<u16>(voice_bit), hi = static_cast<u16>(voice_bit >> 16);
  if (on_off == SPU_OFF) {
    reg_w(0xC6, lo);
    reg_w(0xC7, hi);
    g.keystat &= ~voice_bit;
  } else if (on_off == SPU_ON) {
    reg_w(0xC4, lo);
    reg_w(0xC5, hi);
    g.keystat |= voice_bit;
  }
}

// _spu_note2pitch: the pitch that plays 'note' (semitone, fine 1/128) of a sample
// recorded at 'center'. Equal temperament from the 4096 (= 44.1 kHz) reference.
static u16 note2pitch(u32 center_note, u32 center_fine, u32 note, u32 fine) {
  const double semis = (static_cast<double>(note) + fine / 128.0) - (static_cast<double>(center_note) + center_fine / 128.0);
  const double p = 4096.0 * std::exp2(semis / 12.0);
  return static_cast<u16>(std::clamp(p, 0.0, 16383.0));
}

void SpuSetVoiceAttr(const SpuVoiceAttr* arg) {
  if (spu_trace_voice) spu_trace_voice(arg);
  const u32 mask = arg->mask;
  const bool all = mask == 0;
  auto has = [&](u32 bit) { return all || (mask & bit) != 0; };
  for (u32 v = 0; v < 24; ++v) {
    if (!(arg->voice & (1u << v))) continue;
    const u32 r = v * 8;
    if (has(SPU_VOICE_PITCH)) reg_w(r + 2, arg->pitch);
    if (has(SPU_VOICE_SAMPLE_NOTE)) g.voice_center_note[v] = arg->sample_note;
    if (has(SPU_VOICE_NOTE)) {
      const u16 c = g.voice_center_note[v];
      reg_w(r + 2, note2pitch(c >> 8, c & 0xFF, arg->note >> 8, arg->note & 0xFF));
    }
    if (has(SPU_VOICE_VOLL)) reg_w(r + 0, volume_reg(arg->volume.left, arg->volmode.left, has(SPU_VOICE_VOLMODEL)));
    if (has(SPU_VOICE_VOLR)) reg_w(r + 1, volume_reg(arg->volume.right, arg->volmode.right, has(SPU_VOICE_VOLMODER)));
    if (has(SPU_VOICE_WDSA)) fset_rxxa(static_cast<long>(r + 3), arg->addr);
    if (has(SPU_VOICE_LSAX)) fset_rxxa(static_cast<long>(r + 7), arg->loop_addr);
    if (has(SPU_VOICE_ADSR_ADSR1)) reg_w(r + 4, arg->adsr1);
    if (has(SPU_VOICE_ADSR_ADSR2)) reg_w(r + 5, arg->adsr2);
    if (has(SPU_VOICE_ADSR_AR)) {
      const u16 ar = std::min<u16>(arg->ar, 0x7F);
      const u16 exp = (has(SPU_VOICE_ADSR_AMODE) && arg->a_mode == 5) ? 0x80 : 0;
      reg_w(r + 4, static_cast<u16>((reg_r(r + 4) & 0xFF) | (ar | exp) << 8));
    }
    if (has(SPU_VOICE_ADSR_DR)) {
      const u16 dr = std::min<u16>(arg->dr, 0xF);
      reg_w(r + 4, static_cast<u16>((reg_r(r + 4) & 0xFF0F) | dr << 4));
    }
    if (has(SPU_VOICE_ADSR_SR)) {
      const u16 sr = std::min<u16>(arg->sr, 0x7F);
      u16 m = 0x100;  // linear decrease
      if (has(SPU_VOICE_ADSR_SMODE)) {
        switch (arg->s_mode) {
          case 1: m = 0x000; break;  // linear increase
          case 3: m = 0x100; break;  // linear decrease
          case 5: m = 0x200; break;  // exponential increase
          case 7: m = 0x300; break;  // exponential decrease
          default: break;
        }
      }
      reg_w(r + 5, static_cast<u16>((reg_r(r + 5) & 0x003F) | (sr | m) << 6));
    }
    if (has(SPU_VOICE_ADSR_RR)) {
      const u16 rr = std::min<u16>(arg->rr, 0x1F);
      const u16 exp = (has(SPU_VOICE_ADSR_RMODE) && arg->r_mode == 7) ? 0x20 : 0;
      reg_w(r + 5, static_cast<u16>((reg_r(r + 5) & 0xFFC0) | rr | exp));
    }
    if (has(SPU_VOICE_ADSR_SL)) {
      const u16 sl = std::min<u16>(arg->sl, 0xF);
      reg_w(r + 4, static_cast<u16>((reg_r(r + 4) & 0xFFF0) | sl));
    }
  }
}

void SpuSetCommonAttr(const SpuCommonAttr* attr) {
  const u32 mask = attr->mask;
  const bool all = mask == 0;
  auto has = [&](u32 bit) { return all || (mask & bit) != 0; };
  if (has(SPU_COMMON_MVOLL)) reg_w(0xC0, volume_reg(attr->mvol.left, attr->mvolmode.left, has(SPU_COMMON_MVOLMODEL)));
  if (has(SPU_COMMON_MVOLR)) reg_w(0xC1, volume_reg(attr->mvol.right, attr->mvolmode.right, has(SPU_COMMON_MVOLMODER)));
  if (has(SPU_COMMON_CDVOLL)) reg_w(0xD8, static_cast<u16>(attr->cd.volume.left));
  if (has(SPU_COMMON_CDVOLR)) reg_w(0xD9, static_cast<u16>(attr->cd.volume.right));
  if (has(SPU_COMMON_EXTVOLL)) reg_w(0xDA, static_cast<u16>(attr->ext.volume.left));
  if (has(SPU_COMMON_EXTVOLR)) reg_w(0xDB, static_cast<u16>(attr->ext.volume.right));
  u16 cnt = reg_r(0xD5);
  auto flag = [&](u32 bit, s32 on, u16 reg_bit) {
    if (!has(bit)) return;
    cnt = on == SPU_ON ? static_cast<u16>(cnt | reg_bit) : static_cast<u16>(cnt & ~reg_bit);
  };
  flag(SPU_COMMON_CDREV, attr->cd.reverb, 0x4);
  flag(SPU_COMMON_CDMIX, attr->cd.mix, 0x1);
  flag(SPU_COMMON_EXTREV, attr->ext.reverb, 0x8);
  flag(SPU_COMMON_EXTMIX, attr->ext.mix, 0x2);
  reg_w(0xD5, cnt);
}

long SpuSetReverb(long on_off) {
  if (on_off == SPU_OFF) {
    g.rev_flag = 0;
    reg_w(0xD5, reg_r(0xD5) & 0xFF7F);
  } else if (on_off == SPU_ON) {
    if (g.rev_reserve_wa != 1 && in_allocated_area(g.rev_offsetaddr)) {
      g.rev_flag = 0;
      reg_w(0xD5, reg_r(0xD5) & 0xFF7F);
    } else {
      g.rev_flag = on_off;
      reg_w(0xD5, reg_r(0xD5) | 0x80);
    }
  }
  return g.rev_flag;
}

// Mode: loads the preset (registers and work-area start) with the reverb unit off and
// the depth at 0; delay / feedback adjust the ECHO and DELAY presets' taps and vWALL.
long SpuSetReverbModeParam(const SpuReverbAttr* attr) {
  const u32 mask = attr->mask;
  const bool all = mask == 0;
  bool set_mode = false, set_delay = false, set_feedback = false, clear_wa = false;
  ReverbParam p;
  if (all || (mask & SPU_REV_MODE)) {
    s32 mode = attr->mode;
    if (mode & SPU_REV_MODE_CLEAR_WA) {
      mode &= ~SPU_REV_MODE_CLEAR_WA;
      clear_wa = true;
    }
    if (mode < 0 || mode > 9 || in_allocated_area(rev_startaddr(static_cast<u32>(mode)))) return -1;
    g.rev_offsetaddr = rev_startaddr(static_cast<u32>(mode));
    p = rev_param(static_cast<u32>(mode));
    g.rev_attr.mode = mode;
    if (mode == SPU_REV_MODE_ECHO) {
      g.rev_attr.feedback = 0x7F;
      g.rev_attr.delay = 0x7F;
    } else if (mode == SPU_REV_MODE_DELAY) {
      g.rev_attr.feedback = 0;
      g.rev_attr.delay = 0x7F;
    } else {
      g.rev_attr.feedback = 0;
      g.rev_attr.delay = 0;
    }
    set_mode = true;
  }
  const bool echo_or_delay = g.rev_attr.mode == SPU_REV_MODE_ECHO || g.rev_attr.mode == SPU_REV_MODE_DELAY;
  if ((all || (mask & SPU_REV_DELAYTIME)) && echo_or_delay) {
    set_delay = true;
    if (!set_mode) {
      p = rev_param(static_cast<u32>(g.rev_attr.mode));
      p.flags = 0x0C011C00;
    }
    g.rev_attr.delay = attr->delay;
    const s16 t = static_cast<s16>((g.rev_attr.delay * 0x1000) / 0x7F);
    p.reg[kMLSAME] = static_cast<u16>(static_cast<s16>((g.rev_attr.delay * 0x2000) / 0x7F) - static_cast<s16>(p.reg[kDAPF1]));
    p.reg[kMRSAME] = static_cast<u16>(t - static_cast<s16>(p.reg[kDAPF2]));
    p.reg[kDLSAME] = static_cast<u16>(static_cast<s16>(p.reg[kDRSAME]) + t);
    p.reg[kMLCOMB1] = static_cast<u16>(static_cast<s16>(p.reg[kMRCOMB1]) + t);
    p.reg[kMRAPF1] = static_cast<u16>(static_cast<s16>(p.reg[kMRAPF2]) + t);
    p.reg[kMLAPF1] = static_cast<u16>(static_cast<s16>(p.reg[kMLAPF2]) + t);
  }
  if ((all || (mask & SPU_REV_FEEDBACK)) && echo_or_delay) {
    set_feedback = true;
    if (!set_mode) {
      if (!set_delay) {
        p = rev_param(static_cast<u32>(g.rev_attr.mode));
        p.flags = 0x80;
      } else {
        p.flags |= 0x80;
      }
    }
    g.rev_attr.feedback = attr->feedback;
    p.reg[kVWALL] = static_cast<u16>((g.rev_attr.feedback * 0x8100) / 0x7F);
  }
  bool was_on = false;
  if (!set_mode) {
    if (all || (mask & SPU_REV_DEPTHL)) {
      reg_w(0xC2, static_cast<u16>(attr->depth.left));
      g.rev_attr.depth.left = attr->depth.left;
    }
    if (all || (mask & SPU_REV_DEPTHR)) {
      reg_w(0xC3, static_cast<u16>(attr->depth.right));
      g.rev_attr.depth.right = attr->depth.right;
    }
  } else {
    was_on = (reg_r(0xD5) & 0x80) != 0;
    if (was_on) reg_w(0xD5, reg_r(0xD5) & 0xFF7F);
    reg_w(0xC2, 0);
    reg_w(0xC3, 0);
    g.rev_attr.depth = SpuVolume{};
  }
  if (set_mode || set_delay || set_feedback) set_reverb_attr(p);
  if (clear_wa) SpuClearReverbWorkArea(g.rev_attr.mode);
  if (set_mode) {
    reg_w(0xD1, static_cast<u16>(g.rev_offsetaddr));
    if (was_on) reg_w(0xD5, reg_r(0xD5) | 0x80);
  }
  return 0;
}

void SpuGetReverbModeParam(SpuReverbAttr* attr) {
  attr->mode = g.rev_attr.mode;
  attr->depth = g.rev_attr.depth;
  attr->delay = g.rev_attr.delay;
  attr->feedback = g.rev_attr.feedback;
}

long SpuSetReverbDepth(const SpuReverbAttr* attr) {
  const u32 mask = attr->mask;
  if (mask == 0 || (mask & SPU_REV_DEPTHL)) {
    reg_w(0xC2, static_cast<u16>(attr->depth.left));
    g.rev_attr.depth.left = attr->depth.left;
  }
  if (mask == 0 || (mask & SPU_REV_DEPTHR)) {
    reg_w(0xC3, static_cast<u16>(attr->depth.right));
    g.rev_attr.depth.right = attr->depth.right;
  }
  return 0;
}

// Zeroes the work area of 'mode' (from its start to the end of sound RAM; mode 0:
// the 16 bytes at 0x7fff0). The library does it by DMA with the transfer callback
// held back, so the caller's callback does not run.
long SpuClearReverbWorkArea(long mode) {
  if (mode < 0 || mode >= 10) return -1;
  const u32 start = rev_startaddr(static_cast<u32>(mode));
  if (in_allocated_area(start)) return -1;
  u32 addr, size;
  if (mode == 0) {
    addr = 0xFFF0u << kMemModePlus;
    size = 0x10;
  } else {
    addr = start << kMemModePlus;
    size = (0x10000u - start) << kMemModePlus;
  }
  std::memset(spu().ram() + addr, 0, std::min(size, Spu::kRamSize - addr));
  return 0;
}

u32 SpuSetReverbVoice(long on_off, u32 voice_bit) { return set_any_voice(on_off, voice_bit, 0xCC, 0xCD); }
u32 SpuSetNoiseVoice(long on_off, u32 voice_bit) { return set_any_voice(on_off, voice_bit, 0xCA, 0xCB); }
u32 SpuSetPitchLFOVoice(long on_off, u32 voice_bit) { return set_any_voice(on_off, voice_bit, 0xC8, 0xC9); }

long SpuSetNoiseClock(long n_clock) {
  const long c = std::clamp<long>(n_clock, 0, 0x3F);
  reg_w(0xD5, static_cast<u16>((reg_r(0xD5) & 0xC0FF) | (c & 0x3F) << 8));
  return c;
}

// ---- IRQ ----------------------------------------------------------------------------

long SpuSetIRQ(long on_off) {
  if (on_off == SPU_OFF || on_off == 3) reg_w(0xD5, reg_r(0xD5) & 0xFFBF);  // also acknowledges
  if (on_off == SPU_ON || on_off == 3) reg_w(0xD5, reg_r(0xD5) | 0x40);
  return on_off;
}

u32 SpuSetIRQAddr(u32 addr) {
  if (spu_trace_stream) spu_trace_stream("irq", addr, 0);
  if (addr > 0x7FFF8) return 0;
  return fset_rxxa(0xD2, addr);
}

SpuIRQCallbackProc SpuSetIRQCallback(SpuIRQCallbackProc func) {
  const SpuIRQCallbackProc old = g.irq_callback;
  g.irq_callback = func;  // _SpuCallback -> InterruptCallback(9, func)
  return old;
}

// ---- Streaming (SpuSt) ------------------------------------------------------------------

namespace {

// _spu_t(2): transfer start address; _spu_t(1): DMA write mode; _spu_t(3): the DMA, whose
// completion interrupt (_spu_FiDMA) spu_run delivers before the next sample.
void st_transfer_address(u32 addr) { reg_w(0xD3, static_cast<u16>(addr >> kMemModePlus)); }
void st_dma_write(const u8* data, u32 size) {
  reg_w(0xD5, static_cast<u16>((reg_r(0xD5) & ~0x30) | 0x20));
  std::vector<u16> words((size + 1) / 2, 0);
  std::memcpy(words.data(), data, size);
  spu().dma_write(words.data(), words.size());
  g.dma_pending = true;
}

int first_voice(u32 mask, int from = 0) {
  int v = from;
  while (v < 24 && !(mask & (1u << v))) ++v;
  return v;
}

// The flag bytes the library writes into each chunk before sending it: the first half
// starts with loop start + repeat (the voice's repeat address becomes the buffer), the
// second half ends with loop end + repeat (back to the buffer's start).
void st_patch_first_half(u8* data) {
  data[1] = 6;
  data[0x11] = 2;
  data[st.half - 0xF] = 2;
}

void st_prepare_dma_done();
void st_play_dma_done();
void st_irq();
void st_final_irq();

// ST_OBJ_314: sets the next chunk of voice 'v' up for the half 'second_half' says,
// handling the last chunk (its last block becomes a silent block looping on itself;
// the original bytes are kept to be put back once sent).
SpuStVoiceAttr* st_prepare_half(int v) {
  SpuStVoiceAttr* vo = &st.env.voice[v];
  u32 addr = vo->buf_addr;
  u8* data = vo->data_addr;
  if (st.second_half == 0) {
    st_patch_first_half(data);
  } else {
    data[1] = 2;
    data[0x11] = 2;
    data[st.half - 0xF] = 3;
    addr += static_cast<u32>(st.half);
  }
  st_transfer_address(addr);
  if (vo->status == SPU_ST_STOP) {
    const u32 bit = 1u << v;
    st.final_voices |= bit;
    st.voices &= ~bit;
    u8* last = data + vo->last_size - 0x10;
    std::memcpy(st.saved_block[v], last, 16);
    std::memset(last, 0, 16);
    last[1] = 7;
    if (st.voices != 0) {
      st.irq_voice = first_voice(st.voices);
    } else {
      st.irq_voice = 0x18;
      st.final_voice = first_voice(st.final_voices);
    }
  }
  return vo;
}

void st_send(SpuStVoiceAttr* vo) { st_dma_write(vo->data_addr, static_cast<u32>(st.half)); }

// 0x8001de60: one voice's first half is in; the next one, or the preparation callback.
void st_prepare_dma_done() {
  st.prepare_pending &= ~(1u << st.prepare_voice);
  if (st.prepare_pending == 0) {
    SpuSetTransferCallback(st.saved_transfer_cb);
    st.state = 0x22;
    if (st.prepare_cb) {
      if (spu_trace_stream) spu_trace_stream("prepared", st.prepare_mask, SPU_ST_PREPARE);
      st.prepare_cb(st.prepare_mask, SPU_ST_PREPARE);
      st.prepare_mask = 0;
    }
    return;
  }
  const int v = first_voice(st.prepare_pending, st.prepare_voice + 1);
  st.prepare_voice = v;
  SpuStVoiceAttr* vo = &st.env.voice[v];
  st_transfer_address(vo->buf_addr);
  st_patch_first_half(vo->data_addr);
  st_send(vo);
}

// ST_OBJ_160: SPU_ST_PREPARE.
long st_prepare(u32 voice_bit) {
  const int v = first_voice(voice_bit);
  switch (st.state & 0xF0) {
    case 0x10:
      st.state = 0x20;
      break;
    case 0x30:  // while playing: prepared with the next first-half round
      st.prepare_lock = 1;
      st.prepare_mask = voice_bit;
      st.prepare_pending = voice_bit;
      st.prepare_voice = v;
      st.prepare_lock = 0;
      return 1;
    default:
      return -3;
  }
  st.prepare_mask = voice_bit;
  st.prepare_pending = voice_bit;
  st.half = st.env.size >> 1;
  st.saved_transfer_cb = SpuSetTransferCallback(st_prepare_dma_done);
  st.second_half = 0;
  st.prepare_voice = v;
  SpuStVoiceAttr* vo = &st.env.voice[v];
  st_transfer_address(vo->buf_addr);
  st_patch_first_half(vo->data_addr);
  st.state = 0x21;
  st_send(vo);
  return 1;
}

// ST_OBJ_C1C / ST_OBJ_CAC: SPU_ST_START (or SPU_ST_PLAY): keys the prepared voices on
// and sends their second halves.
long st_start(u32 voice_bit) {
  switch (st.state & 0xF0) {
    case 0x20:
      if (st.state != 0x22) return -3;
      st.state = 0x30;
      break;
    case 0x30:  // while playing: keyed on at the next second-half round
      st.add_lock = 1;
      st.add_voices = voice_bit;
      st.add_lock = 0;
      return 1;
    default:
      return -3;
  }
  const int v = first_voice(voice_bit);
  SpuSetKey(SPU_ON, voice_bit);
  st.voices = voice_bit;
  st.transfer_voices = voice_bit;
  st.finished = 0;
  st.final_voices = 0;
  st.saved_transfer_cb = SpuSetTransferCallback(st_play_dma_done);
  st.saved_irq_cb = SpuSetIRQCallback(st_irq);
  st.second_half = 1;
  st.transfer_voice = v;
  st.irq_voice = v;
  SpuStVoiceAttr* vo = st_prepare_half(v);
  SpuSetIRQ(SPU_OFF);
  st.state = 0x31;
  st_send(vo);
  return 1;
}

// ST_OBJ_994: a voice's half is in. Next voice; at the end of the round, the callbacks
// and the IRQ address for the half the voices play next.
void st_play_dma_done() {
  const int v = st.transfer_voice;
  st.transfer_voices &= ~(1u << v);
  SpuStVoiceAttr* vo = &st.env.voice[v];
  if (vo->status == SPU_ST_STOP) {
    std::memcpy(vo->data_addr + vo->last_size - 0x10, st.saved_block[v], 16);
    vo->status = SPU_ST_PLAY;
  }
  reg_w(static_cast<u32>(v * 8 + 7), static_cast<u16>(vo->buf_addr >> kMemModePlus));  // repeat address
  if (st.transfer_voices != 0) {
    st.transfer_voice = first_voice(st.transfer_voices, v + 1);
    st_send(st_prepare_half(st.transfer_voice));
    return;
  }
  if (st.second_half == 0 && st.prepare_cb && st.prepare_mask) {
    if (spu_trace_stream) spu_trace_stream("prepared", st.prepare_mask, SPU_ST_PLAY);
    st.prepare_cb(st.prepare_mask, SPU_ST_PLAY);
    st.prepare_mask = 0;
  }
  if (st.transfer_cb && st.voices) {
    if (spu_trace_stream) spu_trace_stream("transferred", st.voices, SPU_ST_PLAY);
    st.transfer_cb(st.voices, SPU_ST_PLAY);
  }
  const int irq_v = st.irq_voice < 0x18 ? st.irq_voice : st.final_voice;
  st.irq_addr = st.env.voice[irq_v].buf_addr;
  st.second_half = st.second_half != 1;
  if (st.second_half != 1) st.irq_addr += static_cast<u32>(st.half);
  SpuSetIRQAddr(st.irq_addr);
  SpuSetIRQ(SPU_ON);
  st.state = st.irq_voice < 0x18 ? 0x32 : 0x40;
}

// ST_OBJ_1174: the last voice has played its last chunk.
void st_final_irq() {
  SpuSetIRQ(SPU_OFF);
  st.state = 0x43;
  SpuSetIRQCallback(st.saved_irq_cb);
  SpuSetTransferCallback(st.saved_transfer_cb);
  if (st.finished_cb && st.final_voices) {
    if (spu_trace_stream) spu_trace_stream("finished", st.final_voices, SPU_ST_FINAL);
    st.finished_cb(st.final_voices, SPU_ST_FINAL);
  }
  st.final_voices = 0;
  st.finished = 0;
  st.state = 0x10;
}

// ST_OBJ_530: the voices entered the half that was just refilled; refill the other.
void st_irq() {
  SpuSetIRQ(SPU_OFF);
  st.state = (st.state & 0xF0) == 0x40 ? 0x42 : 0x33;
  s32 longest = 0;
  int longest_voice = -1;
  for (int v = 0; v < 24; ++v) {
    if (!(st.final_voices & (1u << v))) continue;
    const SpuStVoiceAttr& vo = st.env.voice[v];
    u32 addr = vo.buf_addr + static_cast<u32>(vo.last_size) - 8;
    if (st.second_half == 0) addr += static_cast<u32>(st.half);
    reg_w(static_cast<u32>(v * 8 + 7), static_cast<u16>(addr >> kMemModePlus));
    if (longest < vo.last_size) {
      longest = vo.last_size;
      longest_voice = v;
    }
  }
  if (st.voices == 0) {
    if (longest < 9) {
      st_final_irq();
      return;
    }
    longest -= 8;
    SpuSetIRQCallback(st_final_irq);
    st.irq_addr = st.env.voice[longest_voice].buf_addr + static_cast<u32>(longest);
    if (st.second_half == 0) st.irq_addr += static_cast<u32>(st.half);
    SpuSetIRQAddr(st.irq_addr);
  }
  if (st.finished_cb && st.finished) {
    if (spu_trace_stream) spu_trace_stream("finished", st.finished, SPU_ST_PLAY);
    st.finished_cb(st.finished, SPU_ST_PLAY);
  }
  if (st.voices == 0) {
    st.state = 0x41;
    SpuSetIRQ(SPU_ON);
    return;
  }
  st.transfer_voices = st.voices;
  st.finished = st.final_voices;
  st.transfer_voice = st.irq_voice;
  if (st.second_half == 0) {
    if (st.prepare_pending && !st.prepare_lock) {
      st.transfer_voices = st.voices | st.prepare_pending;
      st.add_mask = st.prepare_pending;
      st.prepare_mask = st.prepare_pending;
      st.prepare_pending = 0;
      st.transfer_voice = first_voice(st.transfer_voices);
    }
  } else if (st.add_voices && st.add_mask && !st.add_lock) {
    const u32 keys = st.add_voices & st.add_mask;
    SpuSetKey(SPU_ON, keys);
    st.add_mask = 0;
    st.add_voices = 0;
    st.voices |= keys;
    st.transfer_voices = st.voices;
    st.transfer_voice = st.irq_voice = first_voice(st.voices);
  }
  st.final_voices = 0;
  SpuStVoiceAttr* vo = st_prepare_half(st.transfer_voice);
  st.state = 0x31;
  st_send(vo);
}

void st_reset() {
  st.voices = st.transfer_voices = st.add_voices = st.prepare_pending = st.prepare_mask = 0;
  st.add_mask = st.final_voices = st.finished = 0;
  st.irq_addr = 0;
  st.transfer_cb = st.prepare_cb = st.finished_cb = nullptr;
  st.saved_transfer_cb = nullptr;
  st.saved_irq_cb = nullptr;
  st.irq_voice = st.transfer_voice = st.prepare_voice = st.final_voice = 0x18;
  for (auto& v : st.env.voice) v = SpuStVoiceAttr{SPU_ST_PLAY};
  st.env.size = 0;
  st.env.low_priority = 0;
  st.half = 0;
  st.second_half = 0;
  st.prepare_lock = st.add_lock = 0;
}

}  // namespace

SpuStEnv* SpuStInit(long mode) {
  (void)mode;
  st.state = 0x10;
  st_reset();
  return &st.env;
}

long SpuStQuit() {
  if (st.state != 0x10) return -3;
  st.state = 0;
  st_reset();
  return 1;
}

long SpuStTransfer(long flag, u32 voice_bit) {
  if (spu_trace_stream) spu_trace_stream("transfer", static_cast<u32>(flag), voice_bit);
  if (st.state == 0) return 0;
  voice_bit &= 0xFFFFFF;
  if (voice_bit == 0) return -2;
  if (flag == SPU_ST_PREPARE) return st_prepare(voice_bit);
  if (flag < SPU_ST_PREPARE || flag >= 7) return -2;
  return st_start(voice_bit);
}

long SpuStGetStatus() {
  switch (st.state & 0xF0) {
    case 0x00: return 0;
    case 0x10: return SPU_ST_IDLE;
    case 0x20: return SPU_ST_PREPARE;
    case 0x30: return SPU_ST_TRANSFER;
    case 0x40: return SPU_ST_FINAL;
    default: return -3;
  }
}

u32 SpuStGetVoiceStatus() { return st.voices; }

SpuStCallbackProc SpuStSetPreparationFinishedCallback(SpuStCallbackProc func) {
  st.prepare_cb = func;
  return func;
}
SpuStCallbackProc SpuStSetTransferFinishedCallback(SpuStCallbackProc func) {
  st.transfer_cb = func;
  return func;
}
SpuStCallbackProc SpuStSetStreamFinishedCallback(SpuStCallbackProc func) {
  st.finished_cb = func;
  return func;
}

// ---- Host clock ---------------------------------------------------------------------

void spu_run(s16* stereo, size_t frames) {
  constexpr u32 kTicksPerSample = kRCnt2Hz / Spu::kSampleRate;  // 96
  static_assert(kRCnt2Hz % Spu::kSampleRate == 0);
  Spu& s = spu();
  for (size_t i = 0; i < frames; ++i) {
    if (g.dma_pending) {  // _spu_FiDMA: the streaming library's DMA finished
      g.dma_pending = false;
      reg_w(0xD5, reg_r(0xD5) & static_cast<u16>(~0x30));
      if (g.transfer_callback) g.transfer_callback();
      else g.in_transfer = false;
    }
    rcnt_advance(kTicksPerSample);
    s.tick(stereo[2 * i], stereo[2 * i + 1]);
    if (g.irq_pending) {  // the SPU interrupt (InterruptCallback 9)
      g.irq_pending = false;
      if (g.irq_callback) g.irq_callback();
    }
  }
}

}  // namespace ss::psyq
