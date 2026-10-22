#pragma once

#include <cstddef>

#include "core/types.h"
#include "psx/spu.h"

// PsyQ libspu, reimplemented on the software SPU: the functions the boot executable's
// sound driver calls (System + 0x5aa4), with Sony's semantics. Like the library, they
// work by writing the SPU's registers (psx::Spu::write16 at the offsets libspu's
// _spu_RXX table uses), so the chip ends up in the state the console's would.
//
// Host timing: SpuWrite's DMA completes inside the call (the transfer callback runs
// before it returns, where the console raises the DMA interrupt a little later: the
// game busy-waits on it without letting time pass). The streaming library's DMAs and
// the SPU interrupt are delivered by spu_run between samples, as the console's
// interrupts would arrive.
namespace ss::psyq {

struct SpuVolume {
  s16 left = 0, right = 0;
};

struct SpuVoiceAttr {
  u32 voice = 0;  // bit n = voice n
  u32 mask = 0;   // SPU_VOICE_* (0 = everything)
  SpuVolume volume, volmode, volumex;
  u16 pitch = 0, note = 0, sample_note = 0;
  s16 envx = 0;
  u32 addr = 0, loop_addr = 0;
  s32 a_mode = 0, s_mode = 0, r_mode = 0;
  u16 ar = 0, dr = 0, sr = 0, rr = 0, sl = 0;
  u16 adsr1 = 0, adsr2 = 0;
};

struct SpuReverbAttr {
  u32 mask = 0;  // SPU_REV_* (0 = everything)
  s32 mode = 0;  // SPU_REV_MODE_*; | 0x100 clears the work area
  SpuVolume depth;
  s32 delay = 0, feedback = 0;
};

struct SpuExtAttr {
  SpuVolume volume;
  s32 reverb = 0, mix = 0;
};

struct SpuCommonAttr {
  u32 mask = 0;  // SPU_COMMON_* (0 = everything)
  SpuVolume mvol, mvolmode, mvolx;
  SpuExtAttr cd, ext;
};

struct SpuStVoiceAttr {
  s8 status = 0, pad1 = 0, pad2 = 0, pad3 = 0;
  s32 last_size = 0;    // bytes of the last chunk (status SPU_ST_STOP)
  u32 buf_addr = 0;     // SPU RAM: the voice's double buffer (env.size bytes)
  u8* data_addr = nullptr;  // the next chunk (a main RAM address on the console); the
                            // library patches its ADPCM flag bytes in place
};

struct SpuStEnv {
  s32 size = 0;
  s32 low_priority = 0;
  SpuStVoiceAttr voice[24];
};

constexpr long SPU_OFF = 0, SPU_ON = 1, SPU_BIT = 8;
constexpr long SPU_TRANSFER_BY_DMA = 0, SPU_TRANSFER_BY_IO = 1;
constexpr long SPU_TRANSFER_PEEK = 0, SPU_TRANSFER_WAIT = 1;

constexpr u32 SPU_VOICE_VOLL = 1u << 0, SPU_VOICE_VOLR = 1u << 1, SPU_VOICE_VOLMODEL = 1u << 2,
              SPU_VOICE_VOLMODER = 1u << 3, SPU_VOICE_PITCH = 1u << 4, SPU_VOICE_NOTE = 1u << 5,
              SPU_VOICE_SAMPLE_NOTE = 1u << 6, SPU_VOICE_WDSA = 1u << 7, SPU_VOICE_ADSR_AMODE = 1u << 8,
              SPU_VOICE_ADSR_SMODE = 1u << 9, SPU_VOICE_ADSR_RMODE = 1u << 10, SPU_VOICE_ADSR_AR = 1u << 11,
              SPU_VOICE_ADSR_DR = 1u << 12, SPU_VOICE_ADSR_SR = 1u << 13, SPU_VOICE_ADSR_RR = 1u << 14,
              SPU_VOICE_ADSR_SL = 1u << 15, SPU_VOICE_LSAX = 1u << 16, SPU_VOICE_ADSR_ADSR1 = 1u << 17,
              SPU_VOICE_ADSR_ADSR2 = 1u << 18;

constexpr u32 SPU_REV_MODE = 1u << 0, SPU_REV_DEPTHL = 1u << 1, SPU_REV_DEPTHR = 1u << 2,
              SPU_REV_DELAYTIME = 1u << 3, SPU_REV_FEEDBACK = 1u << 4;
constexpr s32 SPU_REV_MODE_OFF = 0, SPU_REV_MODE_ROOM = 1, SPU_REV_MODE_STUDIO_A = 2, SPU_REV_MODE_STUDIO_B = 3,
              SPU_REV_MODE_STUDIO_C = 4, SPU_REV_MODE_HALL = 5, SPU_REV_MODE_SPACE = 6, SPU_REV_MODE_ECHO = 7,
              SPU_REV_MODE_DELAY = 8, SPU_REV_MODE_PIPE = 9, SPU_REV_MODE_CLEAR_WA = 0x100;

constexpr u32 SPU_COMMON_MVOLL = 1u << 0, SPU_COMMON_MVOLR = 1u << 1, SPU_COMMON_MVOLMODEL = 1u << 2,
              SPU_COMMON_MVOLMODER = 1u << 3, SPU_COMMON_RVOLL = 1u << 4, SPU_COMMON_RVOLR = 1u << 5,
              SPU_COMMON_CDVOLL = 1u << 6, SPU_COMMON_CDVOLR = 1u << 7, SPU_COMMON_CDREV = 1u << 8,
              SPU_COMMON_CDMIX = 1u << 9, SPU_COMMON_EXTVOLL = 1u << 10, SPU_COMMON_EXTVOLR = 1u << 11,
              SPU_COMMON_EXTREV = 1u << 12, SPU_COMMON_EXTMIX = 1u << 13;

constexpr long SPU_ST_ACCEPT = 1, SPU_ST_STOP = 2, SPU_ST_IDLE = 3, SPU_ST_PREPARE = 4, SPU_ST_START = 5,
               SPU_ST_PLAY = 6, SPU_ST_TRANSFER = 7, SPU_ST_FINAL = 8;

// The console's SPU (one per runtime, like the GPU and GTE).
psx::Spu& spu();

void SpuInit();  // boot 0x8001b830 (Ghidra's signature matcher misnames it SsUtReverbOff)
void SpuQuit();  // boot 0x8001c520
long SpuSetMute(long on_off);
long SpuInitMalloc(long num, char* top);  // 'top' holds (num + 1) 8-byte blocks
long SpuSetTransferMode(long mode);
u32 SpuSetTransferStartAddr(u32 addr);
u32 SpuWrite(const u8* addr, u32 size);  // boot 0x8001d990 (misnamed SpuRead by the matcher)
u32 SpuRead(u8* addr, u32 size);
long SpuIsTransferCompleted(long flag);
using SpuTransferCallbackProc = void (*)();
SpuTransferCallbackProc SpuSetTransferCallback(SpuTransferCallbackProc func);
void SpuSetKey(long on_off, u32 voice_bit);
void SpuSetVoiceAttr(const SpuVoiceAttr* arg);
void SpuSetCommonAttr(const SpuCommonAttr* attr);
long SpuSetReverb(long on_off);
long SpuSetReverbModeParam(const SpuReverbAttr* attr);
void SpuGetReverbModeParam(SpuReverbAttr* attr);
long SpuSetReverbDepth(const SpuReverbAttr* attr);
long SpuClearReverbWorkArea(long mode);
u32 SpuSetReverbVoice(long on_off, u32 voice_bit);
long SpuSetNoiseClock(long n_clock);
u32 SpuSetNoiseVoice(long on_off, u32 voice_bit);
u32 SpuSetPitchLFOVoice(long on_off, u32 voice_bit);

using SpuIRQCallbackProc = void (*)();
long SpuSetIRQ(long on_off);
u32 SpuSetIRQAddr(u32 addr);
SpuIRQCallbackProc SpuSetIRQCallback(SpuIRQCallbackProc func);

// SPU streaming (libspu's SpuSt, boot 0x8001de60-0x8001f08c): voices play from a double
// buffer in SPU RAM that the library refills from RAM, half by half, on the SPU
// interrupt; the caller moves data_addr on in the callbacks and marks the last chunk
// (status SPU_ST_STOP, last_size).
using SpuStCallbackProc = void (*)(u32 voice_bit, long status);
SpuStEnv* SpuStInit(long mode);
long SpuStQuit();
long SpuStTransfer(long flag, u32 voice_bit);
long SpuStGetStatus();
u32 SpuStGetVoiceStatus();
SpuStCallbackProc SpuStSetPreparationFinishedCallback(SpuStCallbackProc func);
SpuStCallbackProc SpuStSetTransferFinishedCallback(SpuStCallbackProc func);
SpuStCallbackProc SpuStSetStreamFinishedCallback(SpuStCallbackProc func);

// Host: observers of SpuSetKey / SpuSetVoiceAttr (sound traces and tests); null = none.
extern void (*spu_trace_key)(long on_off, u32 voice_bit);
extern void (*spu_trace_voice)(const SpuVoiceAttr* attr);
// ... and of the streaming library: "transfer" (flag, voices) for SpuStTransfer, "prepared",
// "transferred", "finished" (voices) for its callbacks, "irq" (address) for SpuSetIRQAddr.
extern void (*spu_trace_stream)(const char* what, u32 a, u32 b);

// Host: produces 'frames' 44.1 kHz stereo samples (interleaved), clocking root
// counter 2 between them as the console's clock would (96 ticks of system clock / 8
// per sample), so the sound driver's interrupt handler runs at its console rate.
void spu_run(s16* stereo, size_t frames);

}  // namespace ss::psyq
