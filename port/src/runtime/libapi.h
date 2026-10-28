#pragma once

#include "core/types.h"

// PsyQ libapi: the root counters, interrupt events and critical sections the boot
// executable's sound driver uses. The driver runs its sequencer from root counter 2
// (system clock / 8 = 4,233,600 Hz) with an interrupt event (OpenEvent(RCntCNT2,
// EvSpINT, EvMdINTR, handler)); on the host the counter is clocked by emulated time
// (rcnt_advance, called by libspu's spu_run between SPU samples), so the handler
// runs at its console rate, interleaved with sound output, inside the vblank.
namespace ss::psyq {

constexpr u32 RCntCNT0 = 0xF2000000;  // pixel clock
constexpr u32 RCntCNT1 = 0xF2000001;  // horizontal retrace
constexpr u32 RCntCNT2 = 0xF2000002;  // system clock / 8
constexpr u32 RCntCNT3 = 0xF2000003;  // vertical retrace
constexpr long RCntMdINTR = 0x1000;
constexpr long RCntMdNOINTR = 0x2000;
constexpr long EvSpINT = 0x0002;
constexpr long EvMdINTR = 0x1000;
constexpr long EvMdNOINTR = 0x2000;

constexpr u32 kRCnt2Hz = 33868800 / 8;

long SetRCnt(u32 spec, u16 target, long mode);
long GetRCnt(u32 spec);
long StartRCnt(u32 spec);
long StopRCnt(u32 spec);
long ResetRCnt(u32 spec);

// Events and critical sections. The event table is shared with the memory card
// library, which defines these (runtime/libcard.cpp; interrupts are only taken
// between SPU samples on the host, so the critical sections do nothing).
long OpenEvent(u32 desc, long spec, long mode, void (*func)());
long CloseEvent(long event);
long EnableEvent(long event);
long DisableEvent(long event);
long TestEvent(long event);
void EnterCriticalSection();
void ExitCriticalSection();

// A root counter event with an interrupt handler (desc RCntCNT2, mode EvMdINTR): the
// handler runs each time the counter reaches its target while the counter runs. The
// event is opened in the shared table too, so Enable / Disable / CloseEvent accept its
// handle; on the host the handler's calls are bracketed by StartRCnt / StopRCnt.
long OpenEvent(u32 desc, long spec, long mode, long (*func)());

// Host: advances root counter 2 by 'ticks' (system clock / 8), running the handlers
// of enabled events each time it reaches its target.
void rcnt_advance(u32 ticks);
// Host: clears every counter and event (tests).
void libapi_reset();

}  // namespace ss::psyq
