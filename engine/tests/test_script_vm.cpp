// Event-script VM tests: hand-built scripts exercising the core instructions, calls
// and frames, waits, call depth and trigger regions; then, with SS_DISC1 set, the real
// opening scripts of maps 7 (Claude) and 18 (Rena) against the service stand-ins, and
// a sweep over every map's main function counting instructions no handler claims.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "disc/archive.h"
#include "disc/disc_image.h"
#include "disc/slz.h"
#include "game/ap/ap.h"
#include "game/ap/hub.h"
#include "game/ap/seed.h"
#include "game/boot/system.h"
#include "game/field/field.h"
#include "game/field/message.h"
#include "game/field/script_vm.h"
#include "game/field/vm_services.h"
#include "test.h"

using namespace ss;
using namespace ss::game;
using namespace ss::game::field;

namespace {

// ---- Script assembly ------------------------------------------------------------
u32 op(u32 opcode, u32 operand = 0) { return opcode << 24 | (operand & 0xFFFFFF); }
// Typed operand: type in bits 16-19, address in 0-15, bit 23 = locals.
u32 typed(u32 opcode, u32 type, u32 addr, bool local = false) {
  return opcode << 24 | (local ? 0x800000u : 0) | type << 16 | (addr & 0xFFFF);
}
constexpr u32 kS32 = 0, kS16 = 1, kS8 = 2, kFlag = 3, kU32 = 8;
u32 ext(u32 opcode, s16 first = 0) { return opcode << 24 | static_cast<u16>(first); }
u32 pair(s16 lo, s16 hi) { return static_cast<u16>(lo) | static_cast<u32>(static_cast<u16>(hi)) << 16; }

// The field state block and the handoff blocks the VM reads, set up as the field
// program binds them. The low script variables are the field state block itself
// (+0x10 controlled actor, +0x26 input lock ...), so the tests use variables 0x200+.
struct Fixture {
  std::vector<u8> block = std::vector<u8>(0xA40);
  std::vector<u8> config = std::vector<u8>(0x2A0);
  std::vector<u8> party = std::vector<u8>(8 * 0x60);
  std::vector<u8> records = std::vector<u8>(8 * 0xD0);
  std::vector<u8> inventory = std::vector<u8>(0xC28);
  HandoffRegistry handoff;
  std::vector<u8> script;
  ScriptCtx ctx;
  FieldGlobals saved = globals();  // restored afterwards: other tests bind their own

  Fixture() {
    // The VM tests run without a message system (the stand-ins report windows closed). The
    // message tests keep theirs between tests, and the test order differs by platform
    // (MinGW runs the files in the other order), so drop one left behind.
    delete msg_system();
    handoff_init(&handoff);
    handoff_store(&handoff, config.data(), 2);
    handoff_store(&handoff, party.data(), 3);
    handoff_store(&handoff, records.data(), 4);
    handoff_store(&handoff, inventory.data(), 6);
    handoff_store(&handoff, block.data(), 9);
    bind();
    // Default buttons: confirm 0x40, cancel 0x20, menu 0x10, PA 0x80.
    const u16 buttons[4] = {0x40, 0x20, 0x10, 0x80};
    std::memcpy(config.data(), buttons, sizeof buttons);
    vm_svc_reset();
  }
  ~Fixture() {
    task_unregister(&ctx);  // vm_init registered the VM as a field task
    if (g_script_vm == &ctx) g_script_vm = nullptr;
    g_map_script = nullptr;
    globals() = saved;
  }
  Fixture(const Fixture&) = delete;
  Fixture& operator=(const Fixture&) = delete;
  void bind() {
    FieldGlobals& g = globals();
    g.handoff = &handoff;
    g.config = config.data();
    g.party = party.data();
    g.records = records.data();
    g.inventory = inventory.data();
    g.state_block = block.data();
    g.value_stack = block.data() + 0x100;
    g.script_flags = block.data() + 0x8C0;
    g.script_globals = block.data() + 0x180;
    g.field_flags = block.data() + 0x180;
    g.game_state = block.data() + 0x220;
    g.frame_ticks = 1;
    g.pressed[0] = g.held[0] = g.repeat[0] = 0;
    g.disc_error = 0;
  }
  // A script with 'code' (word 0 should be 'return'), entered at word 'main'.
  void load(const std::vector<u32>& code, u32 main, const std::vector<std::string>& strings = {}) {
    std::vector<u8> text;
    std::vector<u16> offsets;
    for (const auto& s : strings) {
      offsets.push_back(static_cast<u16>(text.size()));
      text.insert(text.end(), s.begin(), s.end());
      text.push_back(0);
    }
    const u32 header[7] = {static_cast<u32>(code.size() * 4), 0, main, static_cast<u32>(strings.size()),
                           static_cast<u32>(text.size()), 0x344, 0x170};
    script.assign(0x1C + code.size() * 4 + offsets.size() * 2 + text.size(), 0);
    std::memcpy(script.data(), header, sizeof header);
    std::memcpy(script.data() + 0x1C, code.data(), code.size() * 4);
    std::memcpy(script.data() + 0x1C + code.size() * 4, offsets.data(), offsets.size() * 2);
    std::memcpy(script.data() + 0x1C + code.size() * 4 + offsets.size() * 2, text.data(), text.size());
    field_state_set_s16(0x10, -1);  // no player control: no talk / trigger checks
    g_map_script = script.data();
    g_script_vm = &ctx;
    vm_ctx_create(&ctx);
    vm_init(&ctx);
  }
  void frame() { vm_thread_update(&ctx); }
  s32 var32(u32 addr) const { return vm_rd32(block.data() + 0x180 + addr); }
  s32 r() const { return var32(0); }
  bool flag(u32 n) const { return (block[0x8C0 + (n >> 3)] >> (n & 7) & 1) != 0; }
  // Byte offset of the current instruction from the code base.
  s32 ip() const {
    return static_cast<s32>(reinterpret_cast<const u8*>(ctx.ip) - reinterpret_cast<const u8*>(ctx.code));
  }
};

// Every service call, as "name(args)".
std::vector<std::string> g_trace;
void record(const char* name, const char* args) { g_trace.push_back(std::string(name) + "(" + args + ")"); }

}  // namespace

TEST(script_vm_core_arithmetic) {
  Fixture f;
  f.load(
      {
          op(0x17),                                    // 0: return (into main)
          typed(0x21, kS32, 0x210), 100,                // 1: store s32 G[0x210] = 100; R = 100
          typed(0x23, kS32, 0x210), 23,                 // 3: R = G[0x210] + 23
          op(0x0B, 0), op(0x02, 0x214),                 // 5: push R; pop_store G[0x214]
          op(0x12, 7), typed(0x28, kS32, 0x214),        // 7: push 7; R = G[0x214] * pop
          op(0x0B, 0), op(0x02, 0x218),                 // 9: G[0x218] = R (861)
          typed(0x21, kS32, 0x21C), static_cast<u32>(-17),  // 11
          typed(0x29, kS32, 0x21C), 4,                  // 13: signed div: -4
          op(0x0B, 0), op(0x02, 0x220),                 // 15: G[0x220] = -4
          typed(0x29, kU32, 0x21C), 0x10000000,         // 17: unsigned div: 0xffffffef / 0x10000000 = 15
          op(0x0B, 0), op(0x02, 0x224),                 // 19
          typed(0x4D, kS16, 0x228), 0x7FFF,             // 21: s16 G[0x228] += 0x7fff
          typed(0x4D, kS16, 0x228), 1,                  // 23: wraps to -0x8000
          typed(0x47, kS8, 0x22A),                      // 25: s8 G[0x22a]++
          typed(0x48, kS8, 0x22B),                      // 26: s8 G[0x22b]-- (0xff)
          typed(0x3F, kS32, 0x21C), 0,                  // 27: R = (-17 > 0) = 0
          op(0x0B, 0), op(0x02, 0x22C),                 // 29
          typed(0x3F, kU32, 0x21C), 0,                  // 31: unsigned: 1
          op(0x0B, 0), op(0x02, 0x230),                 // 33
          op(0x13), 0x12345678, op(0x02, 0x234),        // 35: push32; pop_store
          typed(0x35, kS32, 0x21C), 2,                  // 38: -17 >> 2 = -5
          op(0x0B, 0), op(0x02, 0x238),                 // 40
          typed(0x47, kS32, 0x23C),                     // 42: G[0x23c]++ (spins)
          op(0x15, 42),                                // 43: jmp 42 (spin; the step cap ends the frame)
      },
      1);
  g_vm_step_limit = 200;
  f.frame();
  g_vm_step_limit = 0;
  CHECK_EQ(f.var32(0x210), 100);
  CHECK_EQ(f.var32(0x214), 123);
  CHECK_EQ(f.var32(0x218), 861);
  CHECK_EQ(f.var32(0x220), -4);
  CHECK_EQ(f.var32(0x224), 15);
  CHECK_EQ(vm_rd16(f.block.data() + 0x180 + 0x228), -0x8000);
  CHECK_EQ(f.block[0x180 + 0x22A], 1);
  CHECK_EQ(f.block[0x180 + 0x22B], 0xFF);
  CHECK_EQ(f.var32(0x22C), 0);
  CHECK_EQ(f.var32(0x230), 1);
  CHECK_EQ(f.var32(0x234), 0x12345678);
  CHECK_EQ(f.var32(0x238), -5);
  CHECK(g_vm_stats.step_limit_hits > 0);
}

TEST(script_vm_flags_and_index_ops) {
  Fixture f;
  f.load(
      {
          op(0x17),                                    // 0
          typed(0x21, kFlag, 0x13), 1,                 // 1: flag 0x13 = 1
          op(0x0E, 0x13), op(0x07, 0x20),              // 3: push flag 0x13; pop_store flag 0x20
          op(0x12, 0), op(0x07, 0x13),                 // 5: flag 0x13 = 0
          typed(0x5B, kFlag, 0x21), 1,                 // 7: flag 0x21 ^= 1 -> 1
          typed(0x57, kFlag, 0x21), 2,                 // 9: and 2: (1 & 2) == 0 -> clear
          typed(0x59, kFlag, 0x22), 1,                 // 11: or 1 -> set
          // flags[0x30 + 3] = 1 through the index ops (as map 7's opening does):
          op(0x12, 3), op(0x09, 0x800001), op(0x0A, 0x30),   // 13: R = 3 * 1; indexed = 0x33 (flag element)
          typed(0x21, kFlag, 0),  1,                   // 16: store flag [indexed] = 1
          // s32 array element through index_load_var: G[0x40 + 4 * 2]
          typed(0x21, kS32, 0x248), 77,                 // 18
          op(0x12, 2), op(0x09, 4),                    // 20: R = 2 * 4, element = 4
          op(0x0A, 0x240),                              // 22: R = s32 at 0x48 -> 77
          op(0x0B, 0), op(0x02, 0x250),                 // 23: G[0x250] = 77
          op(0x15, 25),                                // 25: spin
      },
      1);
  g_vm_step_limit = 100;
  f.frame();
  g_vm_step_limit = 0;
  CHECK(!f.flag(0x13));
  CHECK(f.flag(0x20));
  CHECK(!f.flag(0x21));
  CHECK(f.flag(0x22));
  CHECK(f.flag(0x33));
  CHECK_EQ(f.var32(0x250), 77);
}

TEST(script_vm_calls_frames_and_locals) {
  Fixture f;
  f.load(
      {
          op(0x17),                                    // 0
          // 1: main
          op(0x18, 0), 8,                              // frame: 8 bytes of locals
          typed(0x21, kS32, 0, true), 5,               // 3: L[0] = 5
          op(0x0B | 0, 0x800000 | 0),                  // 5: push s32 L[0]
          op(0x14, 13),                                // 6: call 13 (doubles the argument)
          op(0x0B, 0), op(0x02, 0x260),                 // 7: G[0x260] = R
          typed(0x23, kS32, 0, true), 0,               // 9: R = L[0] (kept across the call)
          op(0x0B, 0), op(0x15, 31),                   // 11: push R; jump to 31
          // 13: callee: frame, pops its argument into a local, R = 2 * it
          op(0x18, 0), 4,
          op(0x02, 0x800000 | 0),                      // 15: pop_store L[0]
          typed(0x27, kS32, 0, true), 2,               // 16: R = L[0] * 2
          op(0x17),                                    // 18: return
          0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,          // 19-30
          op(0x02, 0x264),                              // 31: G[0x264] = popped R (5)
          op(0x15, 32),                                // 32: spin
      },
      1);
  const u8* locals_before = nullptr;
  g_vm_step_limit = 60;
  f.frame();
  g_vm_step_limit = 0;
  (void)locals_before;
  CHECK_EQ(f.var32(0x260), 10);
  CHECK_EQ(f.var32(0x264), 5);
  CHECK_EQ(f.ctx.depth, -1);  // vm_init's frame is popped without having raised the depth
  // Locals: main's frame (8 bytes) above the map's variables, the callee's popped.
  CHECK(f.ctx.locals == globals().script_globals + 0x344 + 8);
}

TEST(script_vm_wait_and_one_frame_per_wait) {
  Fixture f;
  f.load(
      {
          op(0x17),                                    // 0
          typed(0x47, kS32, 0x210),                     // 1: G[0x210]++
          ext(0x7A, 3),                                // 2: wait(3)
          op(0x15, 1),                                 // 3: loop
      },
      1);
  f.frame();  // ++, wait 3 -> 2 left
  CHECK_EQ(f.var32(0x210), 1);
  f.frame();  // 1 left
  f.frame();  // 0 left
  CHECK_EQ(f.var32(0x210), 1);
  f.frame();  // runs again
  CHECK_EQ(f.var32(0x210), 2);
  CHECK_EQ(f.ctx.wait, 2);
}

TEST(script_vm_fetch_args_inline_and_stack) {
  Fixture f;
  f.load({op(0x17), op(0x15, 1)}, 1);
  const u32 words[] = {pair(2, 3), pair(4, -5), pair(6, 0x777)};
  f.ctx.instr = ext(0x70, -1);
  f.ctx.ip = words;
  vm_fetch_args(&f.ctx, 6);  // even count: two pairs, then the low half of a third word
  CHECK_EQ(g_vm_args[0], -1);
  CHECK_EQ(g_vm_args[1], 2);
  CHECK_EQ(g_vm_args[4], -5);
  CHECK_EQ(g_vm_args[5], 6);
  CHECK(f.ctx.ip == words + 3);
  u32* sp = f.ctx.value_sp;
  *f.ctx.value_sp++ = 10;
  *f.ctx.value_sp++ = 20;
  *f.ctx.value_sp++ = 30;
  f.ctx.instr = ext(0x70) | 0x800000;
  vm_fetch_args(&f.ctx, 3);  // stack: last argument on top
  CHECK_EQ(g_vm_args[0], 10);
  CHECK_EQ(g_vm_args[2], 30);
  CHECK(f.ctx.value_sp == sp);
}

TEST(script_vm_sync_call_and_message_wait) {
  Fixture f;
  // Main shows a message, waits for it (the stand-in reports it closed) and counts.
  f.load(
      {
          op(0x17),                                                                 // 0
          // 1: msg_open(2, 1, 24, 4, string 0, window 0)
          ext(0x64, 2), pair(1, 24), pair(4, 0), pair(0, 0),
          ext(0x7F, -1),                                                            // 5: wait for all windows
          typed(0x47, kS32, 0x210),                                                  // 6
          ext(0x7A, 1),                                                             // 7
          op(0x15, 6),                                                              // 8
          // 9: hook run by vm_call_sync: R = 42 into G[0x214], waits are skipped
          ext(0x7A, 100), typed(0x21, kS32, 0x214), 42, op(0x17),
      },
      1, {"Hello"});
  g_trace.clear();
  vm_svc_set_trace(record);
  f.frame();
  vm_svc_set_trace(nullptr);
  CHECK_EQ(f.var32(0x210), 1);
  CHECK(!g_trace.empty() && g_trace[0].rfind("msg_window_open(rect=2,1,24,4 text=+0 window=0", 0) == 0);
  const s32 r = f.r();
  vm_call_sync(&f.ctx, 9);
  CHECK_EQ(f.var32(0x214), 42);
  CHECK_EQ(f.r(), r);           // R restored
  CHECK_EQ(f.ctx.sync, 0);
  CHECK_EQ(f.ctx.depth, -1);
  CHECK_EQ(f.ctx.wait, 0);      // the interrupted wait counter is back (run out this frame)
  CHECK_EQ(f.ip(), 8 * 4);      // and so is the IP
}

TEST(script_vm_trigger_region_event) {
  Fixture f;
  // Main registers a region (corners clockwise around 0..10 x 0..10) bound to word 20,
  // then idles. The event sets G[0x210] and returns.
  std::vector<u32> code(32, 0);
  code[0] = op(0x17);
  code[1] = ext(0x74, 0);          // trigger_add(0,0, 0,10, 10,10, 10,0, -, code 20)
  code[2] = pair(0, 0);
  code[3] = pair(10, 10);
  code[4] = pair(10, 10);
  code[5] = pair(0, 0);
  code[6] = pair(20, 0);
  code[7] = op(0x0B, 0);
  code[8] = op(0x02, 0x220);        // G[0x220] = slot
  code[9] = ext(0x7A, 1);
  code[10] = op(0x15, 9);
  code[20] = typed(0x47, kS32, 0x210);
  code[21] = op(0x17);
  f.load(code, 1);
  // The leader (actor 0) stands at (5, 5) on a 2D field, and the player has control.
  vm_svc_actor_create(0, 0, 0, 0, 0, 0, 0x80, 0xFFFF, 0xFFFF, 0x1000F);
  vm_svc_actor_set_view_pos(0, 5, 5);
  f.frame();                       // registers the region (no control yet), waits
  CHECK_EQ(f.var32(0x220), 0);
  CHECK(f.ctx.triggers[0].active == 1);
  CHECK_EQ(f.ctx.triggers[0].max_x, 10);
  field_state_set_s16(0x10, 0);    // control to actor 0
  f.frame();                       // enters the region: the event runs at once, up to its return
  CHECK_EQ(f.var32(0x210), 1);
  CHECK_EQ(f.ctx.active_trigger, -1);
  CHECK_EQ(field_state_s16(0x10), 0);  // control back
  CHECK_EQ(f.ctx.last_trigger, 0);
  f.frame();                       // still inside: no re-fire
  CHECK_EQ(f.var32(0x210), 1);
  vm_svc_actor_set_view_pos(0, 50, 50);
  f.frame();                       // left: re-armed
  CHECK_EQ(f.ctx.last_trigger, -1);
  vm_svc_actor_set_view_pos(0, 5, 5);
  f.frame();
  CHECK_EQ(f.var32(0x210), 2);
}

TEST(script_vm_save_and_restore_state) {
  // Before a battle the VM saves its stacks, IP and trigger regions (handoff 0xd); after
  // it, vm_init on the same script plus vm_restore_state must put everything back.
  Fixture f;
  std::vector<u32> code(32, 0);
  code[0] = op(0x17);
  code[1] = ext(0x74, 0);  // a region at (0,0)-(10,10) bound to word 20
  code[2] = pair(0, 0);
  code[3] = pair(10, 10);
  code[4] = pair(10, 10);
  code[5] = pair(0, 0);
  code[6] = pair(20, 0);
  code[7] = op(0x18, 0);   // frame: 0 flag bytes, 12 variable bytes
  code[8] = 12;
  code[9] = op(0x12, 0x55);  // something on the value stack
  code[10] = ext(0x7A, 5);   // wait(5)
  code[11] = op(0x15, 11);
  code[20] = op(0x17);
  f.load(code, 1);
  f.frame();
  const s32 ip = f.ip();
  const u8* locals = f.ctx.locals;
  const s32 wait = f.ctx.wait;
  const u32* vsp = f.ctx.value_sp;
  const u32* csp = f.ctx.call_sp;
  vm_save_state(&f.ctx);
  CHECK(handoff_get(&f.handoff, 0xD) != nullptr);
  // The field program comes back: a fresh context, vm_init in "returning" mode.
  f.ctx.triggers[0] = Trigger{};
  f.block[0x180 + 0x24] = 1;
  vm_ctx_create(&f.ctx);
  vm_init(&f.ctx);
  vm_restore_state(&f.ctx);
  f.block[0x180 + 0x24] = 0;
  CHECK_EQ(f.ip(), ip);
  CHECK(f.ctx.locals == locals);
  CHECK(f.ctx.value_sp == vsp);
  CHECK(f.ctx.call_sp == csp);
  CHECK_EQ(f.ctx.triggers[0].active, 1);
  CHECK_EQ(f.ctx.triggers[0].max_z, 10);
  CHECK(f.ctx.triggers[0].code == f.ctx.code + 20);
  CHECK(handoff_get(&f.handoff, 0xD) == nullptr);
  CHECK_EQ(wait, 4);  // the wait counter is not part of the saved state (the frame's is)
}

TEST(script_vm_ext_cb_sound_start_clip_stack) {
  // ext 0xcb (sound_start_clip) fetches five arguments, but the game's two calls (map 642
  // 0x6e2c-0x6e3c, map 575 0x760c-0x761c) push four: the clip, 0x20, 0x40, 0x2000. The
  // original pops the word below the value stack as the slot and leaves the stack a word
  // low, which the battle that follows saves as a u16 offset (0xfffc). The port pops only
  // the four: slot 0 (no clip), stack balanced, and a battle's save / restore round trip
  // brings the stack back where it was.
  Fixture f;
  std::vector<u32> code(32, 0);
  code[0] = op(0x17);
  code[1] = op(0x18, 0);  // frame: 0 flag bytes, 16 variable bytes
  code[2] = 16;
  code[3] = op(0x12, 3);  // the clip (the scripts push the local play_sfx's R went to)
  code[4] = op(0x12, 0x20);
  code[5] = op(0x12, 0x40);
  code[6] = op(0x12, 0x2000);
  code[7] = op(0xCB, 0x800000);  // stack form, as both calls
  code[8] = ext(0x7A, 5);        // wait(5)
  code[9] = op(0x15, 9);
  f.load(code, 1);
  vm_wr32(f.block.data() + 0xFC, 7);  // the word below the value stack: never an argument
  const u32* base = reinterpret_cast<const u32*>(f.block.data() + 0x100);
  g_trace.clear();
  vm_svc_set_trace(record);
  f.frame();
  vm_svc_set_trace(nullptr);
  CHECK_EQ(f.ip(), 9 * 4);
  CHECK(f.ctx.value_sp == base);  // the four values gone, nothing below them popped
  CHECK_EQ(vm_rd32(f.block.data() + 0xFC), 7);
  CHECK_EQ(g_trace.size(), static_cast<size_t>(1));
  // The original's argument order: slot <- below the stack (0 here), then the four pushes.
  CHECK(!g_trace.empty() && g_trace[0] == "sound_start_clip(0,3,32,64,0)");

  // The battle after the call (map 642 ext 0xb7 at 0x4f70, map 575 ext 0xed at 0x76f0).
  const u32* csp = f.ctx.call_sp;
  vm_save_state(&f.ctx);
  CHECK_EQ(vm_rd16(f.block.data() + 0x180 + 0x34), 0);  // the saved stack offset
  f.block[0x180 + 0x24] = 1;
  vm_ctx_create(&f.ctx);
  vm_init(&f.ctx);
  vm_restore_state(&f.ctx);
  f.block[0x180 + 0x24] = 0;
  CHECK(f.ctx.value_sp == base);
  CHECK(f.ctx.call_sp == csp);
  CHECK_EQ(f.ip(), 9 * 4);
}

TEST(script_vm_ext_cb_five_arguments) {
  // A call with all five arguments, stack or inline form, takes five as the original.
  Fixture f;
  std::vector<u32> code(32, 0);
  code[0] = op(0x17);
  code[1] = op(0x12, 0x99);  // below the call's arguments: stays on the stack
  code[2] = op(0x12, 2);
  code[3] = op(0x12, 0x60);
  code[4] = op(0x12, 0x30);
  code[5] = op(0x12, 0x1800);
  code[6] = op(0x12, 1);
  code[7] = op(0xCB, 0x800000);
  code[8] = ext(0xCB, 4);  // inline: slot 4, then (0x50, 0x7f) and (0x2000, 1)
  code[9] = pair(0x50, 0x7F);
  code[10] = pair(0x2000, 1);
  code[11] = ext(0x7A, 5);
  code[12] = op(0x15, 12);
  f.load(code, 1);
  const u32* base = reinterpret_cast<const u32*>(f.block.data() + 0x100);
  g_trace.clear();
  vm_svc_set_trace(record);
  f.frame();
  vm_svc_set_trace(nullptr);
  CHECK_EQ(f.ip(), 12 * 4);
  CHECK(f.ctx.value_sp == base + 1);
  CHECK_EQ(base[0], 0x99u);
  CHECK_EQ(g_trace.size(), static_cast<size_t>(2));
  CHECK(g_trace.size() == 2 && g_trace[0] == "sound_start_clip(2,96,48,6144,1)");
  CHECK(g_trace.size() == 2 && g_trace[1] == "sound_start_clip(4,80,127,8192,1)");
}

TEST(script_vm_inventory_check_bytes) {
  Fixture f;
  u8* inv = f.inventory.data();
  CHECK_EQ(inventory_add(inv, 0x123, 2, 1), 1);
  const int i = inventory_find(inv, 0x123);
  CHECK_EQ(i, 0);
  CHECK(inventory_verify(inv, 0));
  CHECK_EQ(read_u16(inv) & 0x3FF, 0x123);
  CHECK_EQ(read_u16(inv) >> 10 & 0x1F, 2);
  CHECK_EQ(read_u16(inv + 0x800), 0x123);  // recent list
  CHECK_EQ(inventory_add(inv, 0x123, 3, 0), 1);
  CHECK_EQ(read_u16(inv) >> 10 & 0x1F, 5);
  inventory_remove_at(inv, 0, 5);
  CHECK_EQ(read_u16(inv), 0);
  // A tampered entry is deleted by the next add.
  inventory_add(inv, 0x55, 1, 0);
  inv[0x824] ^= 0x0F;
  inventory_add(inv, 0x55, 1, 0);
  CHECK_EQ(read_u16(inv), 0);
}

TEST(script_vm_party_helpers) {
  Fixture f;
  vm_wr16(f.party.data(), 1);          // Claude
  vm_wr16(f.party.data() + 0x60, 2);   // Rena
  vm_wr16(f.party.data() + 0xC0, -3);  // Celine in the reserve
  CHECK_EQ(party_find_slot(&f.ctx, 0), 0);
  CHECK_EQ(party_find_slot(&f.ctx, 2), -1);
  party_add_member(&f.ctx, 2);         // back from the reserve
  CHECK_EQ(party_find_slot(&f.ctx, 2), 2);
  party_remove_member(&f.ctx, 1);
  CHECK_EQ(vm_rd16(f.party.data() + 0x60), -2);
  CHECK_EQ(party_member_mask(), 0x5u);
  party_add_member(&f.ctx, 4);         // Bowman: the first non-active slot is Rena's reserve
  CHECK_EQ(vm_rd16(f.party.data() + 0x60), 5);
  CHECK_EQ(vm_rd16(f.party.data() + 0x120), -2);  // Rena moved to the next empty slot
}

// ---- Disc tests -------------------------------------------------------------------
namespace {

struct Disc {
  std::unique_ptr<disc::DiscImage> image;
  disc::Archive archive;
};

disc::Archive* disc_archive() {
  static Disc disc;
  static bool tried = false;
  static disc::Archive* archive = nullptr;
  if (tried) return archive;
  tried = true;
  if (disc::Archive* a = get_system()->loader.archive()) return archive = a;  // another test opened it
  const char* path = std::getenv("SS_DISC1");
  if (!path || !*path) return nullptr;
  std::string error;
  disc.image = disc::DiscImage::open(path, &error);
  if (!disc.image || !disc.archive.open(*disc.image, &error)) {
    std::fprintf(stderr, "  cannot open %s: %s\n", path, error.c_str());
    return nullptr;
  }
  boot_main(0, &disc.archive);
  return archive = &disc.archive;
}

// Section 1 block 0 of map 'map' (archive entry 3207 + map), or empty.
std::vector<u8> map_script(disc::Archive& archive, int map) {
  const u32 entry = static_cast<u32>(0xC87 + map);
  if (!archive.has_entry(entry)) return {};
  auto data = archive.read_entry(entry);
  if (!data || data->size() < 4) return {};
  const u32 count = read_u32(data->data());
  if (count < 1 || count > 16 || data->size() < 4 + 8 * count) return {};
  for (u32 i = 0; i < count; ++i) {
    if (read_u32(data->data() + 4 + 8 * i) != 1) continue;
    const u32 offset = read_u32(data->data() + 8 + 8 * i);
    if (offset + 3 > data->size() || std::memcmp(data->data() + offset, "SLZ", 3) != 0) return {};
    auto blob = disc::slz::unpack_block(*data, offset, 0, nullptr);
    return blob ? std::move(*blob) : std::vector<u8>{};
  }
  return {};
}

// A new game's state block for the chosen hero (field_create_state_block).
struct NewGame : Fixture {
  explicit NewGame(int leader) {
    FieldGlobals& g = globals();
    config[0x45] = static_cast<u8>(leader);
    std::strcpy(reinterpret_cast<char*>(records.data() + 0x24), leader == 0 ? "Claude" : "Rena");
    vm_wr16(party.data(), static_cast<u32>(leader + 1));
    handoff_clear(&handoff, 9);
    field_create_state_block();
    auto* created = static_cast<u8*>(handoff_get(&handoff, 9));
    std::memcpy(block.data(), created, 0xA40);
    delete[] created;
    handoff_clear(&handoff, 9);
    handoff_store(&handoff, block.data(), 9);
    bind();
    (void)g;
  }
  bool start(const std::vector<u8>& blob) {
    if (blob.size() < 0x1C) return false;
    script = blob;
    g_map_script = script.data();
    g_script_vm = &ctx;
    vm_ctx_create(&ctx);
    vm_init(&ctx);
    return true;
  }
  // Problems a frame can reveal: the IP outside the code, the stacks outside their areas.
  const char* check() const {
    const u32 code_bytes = read_u32(script.data());
    const s32 at = ip();
    if (at < 0 || static_cast<u32>(at) >= code_bytes) return "ip outside the code";
    const s32 vs = static_cast<s32>(reinterpret_cast<const u8*>(ctx.value_sp) - block.data());
    if (vs < 0x100 || vs > 0x180) return "value stack outside its area";
    const s32 cs = static_cast<s32>(reinterpret_cast<const u8*>(ctx.call_sp) - block.data());
    if (cs < 0 || cs > 0x100) return "call stack outside its area";
    return nullptr;
  }
};

// The string index whose text starts at byte 'offset' of the script's text.
int string_at(const std::vector<u8>& blob, long offset) {
  const u32 code_bytes = read_u32(blob.data()), count = read_u32(blob.data() + 0xC);
  for (u32 i = 0; i < count; ++i) {
    if (read_u16(blob.data() + 0x1C + code_bytes + 2 * i) == offset) return static_cast<int>(i);
  }
  return -1;
}

// The sweep stops a map once its script asks for a map transfer.
bool g_transfer = false;
void watch_transfer(const char* name, const char*) {
  if (std::strcmp(name, "field_load_map") == 0) g_transfer = true;
}

struct OpeningRun {
  std::vector<std::string> trace;      // "frame: service(args)"
  std::vector<int> actors_created;
  int first_window_string = -1;
  int first_window_frame = -1;
  u32 unknown_ops = 0;
  const char* problem = nullptr;
};

std::string frame_tag(int frame) { return std::to_string(frame) + ": "; }

// SS_VM_EXT_TRACE=FILE: every vm_exec_ext call as "frame map offset instr next next" (the
// format of the PCSX-Redux breakpoint script used to compare with the original).
FILE* g_ext_file = nullptr;
int g_ext_frame = 0;
void ext_hook(ScriptCtx* ctx, u32 instr) {
  const long offset =
      static_cast<long>(reinterpret_cast<const u8*>(ctx->ip) - reinterpret_cast<const u8*>(ctx->code)) - 4;
  std::fprintf(g_ext_file, "%d %d %06lx %08x %08x %08x\n", g_ext_frame, vm_rd16(globals().game_state + 0x1A),
               offset, instr, ctx->ip[0], ctx->ip[1]);
}

OpeningRun run_opening(int map, int leader, int frames) {
  OpeningRun run;
  disc::Archive* archive = disc_archive();
  const std::vector<u8> blob = map_script(*archive, map);
  NewGame game(leader);
  CHECK_EQ(vm_rd16(game.block.data() + 0x220 + 0x1A), map);  // the new game starts here
  if (!game.start(blob)) {
    run.problem = "no script";
    return run;
  }
  g_vm_stats = VmStats{};
  g_vm_step_limit = 100000;
  g_trace.clear();
  vm_svc_set_trace(record);
  if (const char* path = std::getenv("SS_VM_EXT_TRACE")) {
    g_ext_file = std::fopen((std::string(path) + "." + std::to_string(map)).c_str(), "w");
    if (g_ext_file != nullptr) g_vm_ext_hook = ext_hook;
  }
  for (int frame = 0; frame < frames && run.problem == nullptr; ++frame) {
    const size_t before = g_trace.size();
    g_ext_frame = frame;
    game.frame();
    for (size_t i = before; i < g_trace.size(); ++i) {
      const std::string& line = g_trace[i];
      run.trace.push_back(frame_tag(frame) + line);
      if (line.rfind("actor_create(slot=", 0) == 0) run.actors_created.push_back(std::atoi(line.c_str() + 18));
      if (run.first_window_frame < 0 && line.rfind("msg_window_open", 0) == 0) {
        const size_t t = line.find("text=+");
        run.first_window_string = string_at(blob, std::strtol(line.c_str() + t + 6, nullptr, 16));
        run.first_window_frame = frame;
      }
    }
    run.problem = game.check();
  }
  vm_svc_set_trace(nullptr);
  g_vm_ext_hook = nullptr;
  if (g_ext_file != nullptr) std::fclose(g_ext_file);
  g_ext_file = nullptr;
  g_vm_step_limit = 0;
  run.unknown_ops = g_vm_stats.unknown_ops;
  return run;
}

void print_trace(const char* title, const OpeningRun& run, size_t limit) {
  std::printf("  %s: %zu service calls, first window: string %d at frame %d\n", title, run.trace.size(),
              run.first_window_string, run.first_window_frame);
  for (size_t i = 0; i < run.trace.size() && i < limit; ++i) std::printf("    %s\n", run.trace[i].c_str());
}

}  // namespace

TEST(script_vm_disc1_claude_opening_map7) {
  if (disc_archive() == nullptr) {
    std::printf("  (skipped: SS_DISC1 not set)\n");
    return;
  }
  const OpeningRun run = run_opening(7, 0, 600);
  if (std::getenv("SS_VM_TRACE")) print_trace("map 7", run, 400);
  CHECK(run.problem == nullptr);
  CHECK_EQ(run.unknown_ops, 0u);
  // Ronixis's survey team: actors 0 and 16-19 placed by the map setup, then 30.
  const std::vector<int> expected = {0, 16, 17, 18, 19, 30};
  CHECK(run.actors_created.size() >= expected.size());
  for (size_t i = 0; i < expected.size() && i < run.actors_created.size(); ++i) {
    CHECK_EQ(run.actors_created[i], expected[i]);
  }
  // "Ronixis: Well? Any results different from the pre-landing survey?"
  CHECK_EQ(run.first_window_string, 90);
}

TEST(script_vm_disc1_rena_opening_map18) {
  if (disc_archive() == nullptr) {
    std::printf("  (skipped: SS_DISC1 not set)\n");
    return;
  }
  const OpeningRun run = run_opening(18, 1, 600);
  if (std::getenv("SS_VM_TRACE")) print_trace("map 18", run, 400);
  CHECK(run.problem == nullptr);
  CHECK_EQ(run.unknown_ops, 0u);
  CHECK(!run.actors_created.empty());
  // "Rena: I'll be going." then "????: Wait!"
  CHECK_EQ(run.first_window_string, 1);
}

TEST(script_vm_disc1_all_map_main_functions) {
  disc::Archive* archive = disc_archive();
  if (archive == nullptr) {
    std::printf("  (skipped: SS_DISC1 not set)\n");
    return;
  }
  int maps = 0, clean = 0, transfers = 0, limited = 0;
  const int frames = std::getenv("SS_VM_SWEEP_FRAMES") ? std::atoi(std::getenv("SS_VM_SWEEP_FRAMES")) : 600;
  u32 unknown = 0;
  std::map<std::string, int> problems;
  std::map<u32, int> unknown_by_op;
  for (int map = 0; map < 1200; ++map) {  // field maps 0-908, world-map chunks 909+
    const std::vector<u8> blob = map_script(*archive, map);
    if (blob.size() < 0x1C) continue;
    ++maps;
    for (int leader = 0; leader < 2; ++leader) {  // a new game as Claude, then as Rena
      NewGame game(leader);
      vm_wr16(game.block.data() + 0x220 + 0x1A, static_cast<u32>(map));
      game.start(blob);
      g_vm_stats = VmStats{};
      g_vm_step_limit = 20000;
      g_transfer = false;
      vm_svc_set_trace(watch_transfer);
      const char* problem = nullptr;
      bool transferred = false;
      for (int frame = 0; frame < frames && problem == nullptr && !transferred; ++frame) {
        game.frame();
        problem = game.check();
        transferred = g_transfer;
      }
      vm_svc_set_trace(nullptr);
      g_vm_step_limit = 0;
      transfers += transferred;
      limited += g_vm_stats.step_limit_hits != 0;
      if (g_vm_stats.unknown_ops != 0) {
        unknown += g_vm_stats.unknown_ops;
        ++unknown_by_op[g_vm_stats.last_unknown >> 24 == 0xFE || g_vm_stats.last_unknown >> 24 == 0xFF
                            ? (g_vm_stats.last_unknown >> 16) & 0xFF7F
                            : g_vm_stats.last_unknown >> 24];
        std::printf("  map %d: %u unknown ops (last %08x at +%#x)\n", map, g_vm_stats.unknown_ops,
                    g_vm_stats.last_unknown, game.ip());
      }
      if (problem != nullptr) {
        ++problems[problem];
        std::printf("  map %d: %s\n", map, problem);
      }
      if (problem == nullptr && g_vm_stats.unknown_ops == 0) ++clean;
    }
  }
  std::printf("  %d map scripts x 2 new games: %d of %d runs clean after %d frames; %d reached a map transfer, "
              "%d hit the step cap; %u unknown ops\n",
              maps, clean, 2 * maps, frames, transfers, limited, unknown);
  CHECK(maps > 800);
  CHECK_EQ(unknown, 0u);
  CHECK(problems.empty());
}

// ---- Archipelago, the hub [hubvm agent] (game/ap/hub.h; docs/ap/seed_format.md "Hub") ----

namespace {

// A hub seed: Alen-Tax's chamber (map 61) with a story view, Azamgil's (Rena's game only) and
// Visseyer's (no view); the step passes the lock on maps 685 and 686.
const char* kHubSeed = R"seed({"format": "apso2", "version": 3, "seed": "hub-test", "slot": {"name": "H", "id": 1},
 "options": {"world_mode": "hub"}, "items": {}, "locations": [],
 "bosses": [{"encounter": 191, "name": "Alen-Tax", "map": 61,
             "view": {"chapter": 6, "step": 2100, "flags": {"745": 0, "744": 0, "11": 0},
                      "globals": {"0x1cf": {"size": 1, "value": 3}, "0x2e0": "0x1234"}, "keep": [11]}},
            {"encounter": 195, "name": "Azamgil", "map": 463, "game": "rena", "view": {"chapter": 15, "step": 2500}},
            {"encounter": 209, "name": "Visseyer", "map": 412}],
 "hub": {"chapter": 60, "step": 4000, "lock": {"chapter": true, "step": true, "step_maps": [685, 686]}}})seed";

std::string hub_seed_with(const std::string& from, const std::string& to) {
  std::string t = kHubSeed;
  const size_t at = t.find(from);
  if (at != std::string::npos) t.replace(at, from.size(), to);
  return t;
}

bool start_hub(const std::string& text) {
  ap::Seed s;
  std::string error;
  if (!ap::load_seed_text(text, &s, &error)) {
    std::fprintf(stderr, "  hub seed: %s\n", error.c_str());
    return false;
  }
  ap::start_with(s);
  return true;
}

void set_map(Fixture& f, int map) {
  const s16 m = static_cast<s16>(map);
  std::memcpy(f.block.data() + 0x220 + 0x1A, &m, 2);
}
void set_story(Fixture& f, s32 chapter, s32 step) {
  vm_wr32(f.block.data() + 0x180 + 0xA0, static_cast<u32>(chapter));
  vm_wr32(f.block.data() + 0x180 + 0xA4, static_cast<u32>(step));
}
void set_flag(Fixture& f, u32 n, bool on) {
  u8& b = f.block[0x8C0 + (n >> 3)];
  b = on ? static_cast<u8>(b | 1 << (n & 7)) : static_cast<u8>(b & ~(1 << (n & 7)));
}
size_t hub_log_count(const std::string& part) {
  size_t n = 0;
  for (const std::string& line : ap::hub_log()) n += line.find(part) != std::string::npos;
  return n;
}

// Stores to the chapter and step in every form the VM has: store = word, typed store, inc,
// block_store; then a control store.
const std::vector<u32> kLockScript = {
    op(0x17),                         // 0
    op(0x1A, 0xA0), 13,               // 1 (+0x4): store s32 G[0xa0] = 13
    op(0x1A, 0xA4), 0,                // 3 (+0xc): store s32 G[0xa4] = 0
    typed(0x21, kS32, 0xA0), 14,      // 5 (+0x14): typed store G[0xa0] = 14
    typed(0x47, kS32, 0xA4),          // 7 (+0x1c): G[0xa4]++
    op(0x61, 0xA0), 1, 15,            // 8 (+0x20): block_store 1 word at G[0xa0]: 15
    op(0x1A, 0x200), 7,               // 11: store s32 G[0x200] = 7
    op(0x15, 13),                     // 13: spin
};

}  // namespace

TEST(ap_hub_seed) {
  ap::Seed s;
  std::string error;
  CHECK(ap::load_seed_text(kHubSeed, &s, &error));
  CHECK(s.hub && !s.open_world && s.version == 3 && s.bosses.size() == 3);
  CHECK(s.hub_lock_chapter && s.hub_lock_step && s.hub_step_maps == (std::vector<int>{685, 686}));
  const ap::HubBoss& b = s.bosses[0];
  CHECK(b.encounter == 191 && b.map == 61 && b.maps == std::vector<int>{61} && b.has_view && b.game == 0);
  CHECK(b.view.chapter == 6 && b.view.step == 2100 && b.view.flags.size() == 3 && !b.view.flags.at(745));
  CHECK(b.view.globals.size() == 2 && b.view.globals[0].addr == 0x1CF && b.view.globals[0].size == 1 &&
        b.view.globals[0].value == 3 && b.view.globals[1].addr == 0x2E0 && b.view.globals[1].size == 4 &&
        b.view.globals[1].value == 0x1234);
  CHECK(b.view.keep == std::vector<int>{11});
  CHECK(s.bosses[1].game == 2 && !s.bosses[2].has_view);
  // world_mode 2, defaults of the lock.
  CHECK(ap::load_seed_text(hub_seed_with("\"world_mode\": \"hub\"", "\"world_mode\": 2"), &s, &error) && s.hub);
  CHECK(ap::load_seed_text(hub_seed_with(", \"lock\": {\"chapter\": true, \"step\": true, \"step_maps\": [685, 686]}", ""), &s, &error));
  CHECK(s.hub_step_maps == std::vector<int>{685});
  auto refused = [&](const std::string& text, const std::string& part) {
    ap::Seed t;
    std::string e;
    const bool ok = !ap::load_seed_text(text, &t, &e) && e.find(part) != std::string::npos;
    if (!ok) std::fprintf(stderr, "  expected '%s', got '%s'\n", part.c_str(), e.c_str());
    return ok;
  };
  CHECK(refused(hub_seed_with("\"keep\": [11]", "\"keep\": [12]"), "view.keep: a flag of view.flags"));
  CHECK(refused(hub_seed_with("\"0x2e0\": \"0x1234\"", "\"0x2e1\": 5"), "view.globals"));
  CHECK(refused(hub_seed_with("\"0x2e0\": \"0x1234\"", "\"0xa4\": 5"), "view.globals"));
  CHECK(refused(hub_seed_with("\"game\": \"rena\"", "\"game\": \"ashton\""), "game: claude or rena"));
  CHECK(refused(hub_seed_with("\"world_mode\": \"hub\"", "\"world_mode\": \"hubs\""), "story, open_world or hub"));
  CHECK(refused(hub_seed_with("\"step\": 2100,", ""), "view.step missing"));
  CHECK(refused(hub_seed_with("\"version\": 3", "\"version\": 4"), "unsupported version 4"));
}

TEST(ap_hub_chapter_lock) {
  ap::stop();
  {
    Fixture f;
    set_story(f, 60, 4000);
    set_map(f, 547);
    CHECK(start_hub(kHubSeed));
    f.load(kLockScript, 1);
    g_vm_step_limit = 40;
    f.frame();
    CHECK_EQ(f.var32(0xA0), 60);
    CHECK_EQ(f.var32(0xA4), 4000);
    CHECK_EQ(f.var32(0x200), 7);
    CHECK_EQ(hub_log_count("chapter store 13 dropped at map 547 offset 0x4"), 1u);
    CHECK_EQ(hub_log_count("step store 0 dropped at map 547 offset 0xc"), 1u);
    CHECK_EQ(hub_log_count("chapter store 14 dropped at map 547 offset 0x14"), 1u);
    CHECK_EQ(hub_log_count("step store 4001 dropped at map 547 offset 0x1c"), 1u);
    CHECK_EQ(hub_log_count("chapter store 15 dropped at map 547 offset 0x20"), 1u);
    // Logged once per site.
    f.load(kLockScript, 1);
    f.frame();
    CHECK_EQ(hub_log_count("dropped at map 547"), 5u);
    // A step map: the step passes, the chapter does not.
    set_map(f, 685);
    f.load(kLockScript, 1);
    f.frame();
    CHECK_EQ(f.var32(0xA0), 60);
    CHECK_EQ(f.var32(0xA4), 1);  // (0, then ++)
    // The disc switch is dropped in hub mode.
    CHECK(ap::hub_swallows_disc_change());
    g_vm_step_limit = 0;
    ap::stop();
  }
  for (const char* other : {"story", "none"}) {
    Fixture f;
    set_story(f, 60, 4000);
    set_map(f, 547);
    if (std::string(other) == "story") CHECK(start_hub(hub_seed_with("\"world_mode\": \"hub\"", "\"world_mode\": \"story\"")));
    f.load(kLockScript, 1);
    g_vm_step_limit = 40;
    f.frame();
    g_vm_step_limit = 0;
    CHECK_EQ(f.var32(0xA0), 15);  // the original: every store lands
    CHECK_EQ(f.var32(0xA4), 1);
    CHECK(!ap::hub_swallows_disc_change());
    CHECK(!ap::g_hub_vm);
    ap::stop();
  }
}

TEST(ap_hub_story_view) {
  ap::stop();
  Fixture f;
  set_story(f, 60, 4000);
  set_flag(f, 1, true);     // Claude's game
  set_flag(f, 745, true);   // real 1, the view 0
  set_flag(f, 744, false);  // real 0, the view 0, stored 1 in the view: dropped
  set_flag(f, 11, false);   // real 0, the view 0, stored 1 in the view: kept
  vm_wr32(f.block.data() + 0x180 + 0x1CC, 0x11223344u);
  CHECK(start_hub(kHubSeed));
  // Visseyer's chamber: no view. Azamgil's: Rena's game only.
  ap::on_script_loaded(412, {});
  CHECK(!ap::hub_view_on());
  ap::on_script_loaded(463, {});
  CHECK(!ap::hub_view_on());
  set_map(f, 61);
  ap::on_script_loaded(61, {});
  CHECK(ap::hub_view_on() && ap::hub_view_encounter() == 191);
  f.load(
      {
          op(0x17),                                   // 0
          op(0x19, 0xA0), 6, op(0x02, 0x200),         // 1: G[0x200] = (chapter == 6)
          op(0x0B, 0xA4), op(0x02, 0x204),            // 4: G[0x204] = step
          op(0x0E, 745), op(0x02, 0x208),             // 6: G[0x208] = flag 745
          op(0x0D, 0x1CF), op(0x02, 0x20C),           // 8: G[0x20c] = u8 G[0x1cf]
          op(0x0B, 0x1CC), op(0x02, 0x210),           // 10: G[0x210] = s32 G[0x1cc] (one byte the view's)
          op(0x1A, 0xA0), 41,                         // 12: chapter = 41
          op(0x1A, 0xA4), 0,                          // 14: step = 0
          typed(0x21, kFlag, 744), 1,                 // 16: flag 744 = 1 (the view's)
          typed(0x21, kFlag, 11), 1,                  // 18: flag 11 = 1 (the view's; kept)
          typed(0x21, kFlag, 483), 1,                 // 20: flag 483 = 1 (not the view's: through)
          op(0x12, 9), op(0x05, 0x1CF),               // 22: s8 G[0x1cf] = 9 (the view's)
          op(0x13), 0xAABBCCDDu, op(0x02, 0x1CC),     // 24: s32 G[0x1cc] = 0xaabbccdd (3 bytes through)
          typed(0x47, kS32, 0xA4),                    // 27: step++
          op(0x0B, 0xA0), op(0x02, 0x214),            // 28: G[0x214] = chapter as the view shows it
          op(0x15, 30),                               // 30: spin
      },
      1);
  g_vm_step_limit = 60;
  f.frame();
  g_vm_step_limit = 0;
  CHECK_EQ(f.var32(0x200), 1);
  CHECK_EQ(f.var32(0x204), 2100);
  CHECK_EQ(f.var32(0x208), 0);
  CHECK_EQ(f.var32(0x20C), 3);
  CHECK_EQ(f.var32(0x210), 0x03223344);
  CHECK_EQ(f.var32(0x214), 41);
  // The real state: chapter and step untouched, the view's flags untouched, flag 483 through, the
  // word's three other bytes through.
  CHECK_EQ(f.var32(0xA0), 60);
  CHECK_EQ(f.var32(0xA4), 4000);
  CHECK(f.flag(745) && !f.flag(744) && !f.flag(11) && f.flag(483));
  CHECK_EQ(static_cast<u32>(f.var32(0x1CC)), 0x11BBCCDDu);
  CHECK(ap::hub_status().find("chapter 41 step 1") != std::string::npos);
  // Party changes inside the view are dropped; none outside it.
  CHECK(ap::hub_swallows_party(true, 1));
  // The battle back on the same map: the view stays; the victory goes in goal_won.
  ap::on_battle_exit(191, 0);
  CHECK(ap::goal_won(191));
  ap::on_script_loaded(61, {});
  CHECK(ap::hub_view_on());
  // The battle hands the field back mid-scene: the controls on and the thread at depth 0 for a
  // frame or two, waiting on the map's load (wait 8): not the scene's end [hubchambers: Shin 447].
  f.ctx.wait_state = 8;
  for (int i = 0; i < 40; ++i) ap::hub_free_play();
  CHECK(ap::hub_view_on());
  f.ctx.wait_state = 0;
  // Free play by the field's rule, 10 frames in a row, ends it: flag 11 kept, the rest dropped.
  for (int i = 0; i < 9; ++i) ap::hub_free_play();
  CHECK(ap::hub_view_on());
  f.ctx.wait_state = 9;  // (a choice in between starts the count again)
  ap::hub_free_play();
  f.ctx.wait_state = 0;
  for (int i = 0; i < 9; ++i) ap::hub_free_play();
  CHECK(ap::hub_view_on());
  ap::hub_free_play();
  CHECK(!ap::hub_view_on());
  CHECK(f.flag(11) && !f.flag(744) && f.flag(745));
  CHECK_EQ(f.var32(0xA0), 60);
  CHECK_EQ(hub_log_count("dropped chapter 41, step 1, G[0x1cf] 170, flag 744=1, 1 party changes; kept flag 11=1"), 1u);
  CHECK(!ap::hub_swallows_party(true, 1));
  // Beaten: the chamber shows the hub's own state from now on.
  ap::on_script_loaded(61, {});
  CHECK(!ap::hub_view_on());
  ap::stop();
}

TEST(ap_hub_view_ends) {
  ap::stop();
  Fixture f;
  set_story(f, 60, 4000);
  set_flag(f, 1, true);
  CHECK(start_hub(hub_seed_with("\"map\": 61,", "\"map\": 61, \"maps\": [61, 62],")));
  // The player walks out before the fight: the view ends at the next map's load (nothing kept),
  // and comes back on re-entry; a map of `maps` keeps it.
  ap::on_script_loaded(61, {});
  CHECK(ap::hub_view_on());
  ap::on_script_loaded(62, {});
  CHECK(ap::hub_view_on());
  ap::on_script_loaded(60, {});
  CHECK(!ap::hub_view_on());
  CHECK_EQ(hub_log_count("story view off: map"), 1u);
  ap::on_script_loaded(61, {});
  CHECK(ap::hub_view_on());
  // Free play before the victory does not end it; another boss's victory neither.
  ap::hub_free_play();
  ap::on_battle_exit(209, 0);
  CHECK(ap::hub_view_on());
  // New Game and a load reset it (nothing written back).
  ap::on_new_game();
  CHECK(!ap::hub_view_on());
  ap::on_script_loaded(61, {});
  CHECK(ap::hub_view_on());
  ap::on_loaded("", 0);
  CHECK(!ap::hub_view_on());
  // Rena's game: Azamgil's chamber has its view, Alen-Tax's too (no `game`).
  set_flag(f, 1, false);
  ap::on_script_loaded(463, {});
  CHECK(ap::hub_view_encounter() == 195);
  ap::stop();
  // A story seed with the same rows: no view.
  CHECK(start_hub(hub_seed_with("\"world_mode\": \"hub\"", "\"world_mode\": \"open_world\"")));
  ap::on_script_loaded(61, {});
  CHECK(!ap::hub_view_on());
  ap::stop();
}

TEST(ap_hub_view_keys) {
  // The optional keys: after_maps (the view goes on into them, never starts there), until
  // "transfer" (no end at free play), cut_to (after the win, the first transfer out goes there).
  ap::stop();
  Fixture f;
  set_story(f, 60, 4000);
  set_flag(f, 1, true);
  const std::string text = hub_seed_with(
      "{\"encounter\": 209, \"name\": \"Visseyer\", \"map\": 412}",
      "{\"encounter\": 527, \"name\": \"Guardian (Courage)\", \"map\": 642, \"view\": {\"chapter\": 42, \"step\": 0, \"after_maps\": [625]}},"
      " {\"encounter\": 525, \"name\": \"Guardian (Power)\", \"map\": 603, \"maps\": [603, 604], \"view\": {\"chapter\": 42, \"step\": 0, \"until\": \"transfer\", \"done_flag\": 163}},"
      " {\"encounter\": 520, \"name\": \"Berle\", \"map\": 575, \"maps\": [557, 575], \"view\": {\"chapter\": 33, \"step\": 0,"
      " \"cut_to\": {\"map\": 557, \"x\": -92, \"y\": 0, \"z\": 1907, \"dir\": 270}}}");
  ap::Seed s;
  std::string error;
  CHECK(ap::load_seed_text(text, &s, &error));
  CHECK(s.bosses.size() == 5 && s.bosses[2].view.after_maps == std::vector<int>{625} && s.bosses[3].view.until_transfer &&
        s.bosses[4].view.has_cut_to && s.bosses[4].view.cut_to.map == 557 && s.bosses[4].view.cut_to.z == 1907 &&
        s.bosses[4].view.cut_to.dir == 270);
  CHECK(s.bosses[3].view.done_flag == 163 && s.bosses[2].view.done_flag == -1);
  ap::Seed bad_done;
  CHECK(!ap::load_seed_text(hub_seed_with("\"chapter\": 6, \"step\": 2100,", "\"chapter\": 6, \"step\": 2100, \"done_flag\": 5000,"), &bad_done, &error) &&
        error.find("view.done_flag") != std::string::npos);
  ap::Seed bad;
  CHECK(!ap::load_seed_text(hub_seed_with("\"chapter\": 6, \"step\": 2100,", "\"chapter\": 6, \"step\": 2100, \"until\": \"later\","), &bad, &error) &&
        error.find("view.until") != std::string::npos);
  CHECK(!ap::load_seed_text(hub_seed_with("\"chapter\": 6, \"step\": 2100,", "\"chapter\": 6, \"step\": 2100, \"cut_to\": {\"map\": 557, \"x\": 1, \"y\": 0},"), &bad, &error) &&
        error.find("view.cut_to") != std::string::npos);
  ap::start_with(s);
  // after_maps.
  ap::on_script_loaded(625, {});
  CHECK(!ap::hub_view_on());
  ap::on_script_loaded(642, {});
  CHECK(ap::hub_view_encounter() == 527);
  ap::on_script_loaded(625, {});
  CHECK(ap::hub_view_encounter() == 527);
  ap::on_script_loaded(641, {});
  CHECK(!ap::hub_view_on());
  // until "transfer".
  ap::on_script_loaded(603, {});
  ap::on_battle_exit(525, 0);
  ap::hub_free_play();
  CHECK(ap::hub_view_encounter() == 525);
  ap::on_script_loaded(604, {});
  CHECK(ap::hub_view_encounter() == 525);
  ap::on_script_loaded(602, {});
  CHECK(!ap::hub_view_on());
  // done_flag [hubchambers]: won, but its end scene's flag 163 is still clear: the view comes back
  // on in its maps (won from the start, so cut_to / free play act as after a win); once 163 is set,
  // no more view.
  set_flag(f, 163, false);
  ap::on_script_loaded(603, {});
  CHECK(ap::hub_view_encounter() == 525);
  CHECK(ap::hub_status().find(", won)") != std::string::npos);
  CHECK_EQ(hub_log_count("Guardian (Power), encounter 525 won before; on until flag 163 is set"), 1u);
  ap::on_script_loaded(602, {});
  CHECK(!ap::hub_view_on());
  set_flag(f, 163, true);
  ap::on_script_loaded(603, {});
  CHECK(!ap::hub_view_on());
  // A row without done_flag: once won, never again (Courage, 527, won here).
  ap::on_battle_exit(527, 0);
  ap::on_script_loaded(642, {});
  CHECK(!ap::hub_view_on());
  // cut_to: not before the win; the first transfer out after it; the view ends at that load.
  ap::on_script_loaded(575, {});
  s32 args[7] = {576, 2, 0, 0, 0, 0, 0};
  CHECK(!ap::on_map_transfer(0x100, args) && args[0] == 576);
  ap::on_battle_exit(520, 0);
  s32 inside[7] = {557, 2, 5, 0, 5, 90, 0};
  CHECK(!ap::on_map_transfer(0x100, inside) && inside[0] == 557 && inside[2] == 5);  // (a view map: no cut)
  CHECK(!ap::on_map_transfer(0x100, args) && args[0] == 557 && args[2] == -92 && args[3] == 0 && args[4] == 1907 && args[5] == 270);
  CHECK_EQ(hub_log_count("the transfer to map 576 goes to map 557 (-92, 0, 1907) instead (cut_to)"), 1u);
  ap::on_script_loaded(557, {});
  CHECK(!ap::hub_view_on());
  ap::stop();
}

// ---- The questions (a row's `ask`) [hubask agent] (the user's rulings on Shigeo 281 and the arena
// 305; docs/ap/seed_format.md "Hub") ----

namespace {

// Shigeo's town chamber asks at every way in; Marsilio's arena only at the antechamber's door (a
// seed door 304 -> 296 sent into 305 while he is unbeaten, here at offset 0x10) and by the extra
// options: the desk's (map 297) and the Clerk's in the arena (map 305, with the view on).
const char* kAskSeed = R"seed({"format": "apso2", "version": 3, "seed": "ask-test", "slot": {"name": "H", "id": 1},
 "options": {"world_mode": "hub"}, "items": {}, "locations": [],
 "doors": [{"id": 19922948, "to": {"map": 305, "x": -13, "y": 0, "z": 276, "dir": 271, "while_unbeaten": 532}}],
 "bosses": [{"encounter": 533, "name": "Shigeo", "map": 281, "view": {"chapter": 57, "step": 2000},
             "ask": {"text": "Shigeo is on the rampage ahead.\nFace Shigeo now?"}},
            {"encounter": 532, "name": "Marsilio", "map": 305, "view": {"chapter": 57, "step": 20},
             "ask": {"text": "Marsilio waits in the arena.\nFace Marsilio now?", "doors": [19922948],
                     "options": [{"map": 297, "window": "0x4", "choice": "0x14", "text": "Face Marsilio.",
                                  "to": {"map": 305, "x": -13, "y": 0, "z": 276, "dir": 271}},
                                 {"map": 305, "window": "0x4", "choice": "0x14", "text": "Leave the arena.",
                                  "to": {"map": 304, "x": 1339, "y": 0, "z": -6, "dir": 180}}]}}],
 "hub": {"chapter": 60, "step": 4000, "lock": {"chapter": true, "step": true}}})seed";

// A choice as the game writes it: ext 0x68 (9, 11, 24, 2, -, string 0; window 0) at word 1, ext
// 0xdc 2 at word 5, push R, pop_store L[0], ext 0x81 0, then a spin.
const std::vector<u32> kChoiceScript = {
    op(0x17),                                   // 0
    0x68000009u, 24u << 16 | 11u, 2u, 0u,       // 1 (+0x4): the options window
    0xDC000002u,                                // 5 (+0x14): the choice
    0x0B000000u, 0x22800000u,                   // 6, 7: push R, pop_store L[0]
    0x81000000u,                                // 8 (+0x20): close window 0
    op(0x15, 9),                                // 9: spin
};

// The phases of a question at a transfer, answered 'r' (0 yes, 1 no, -1 cancelled): the op runs
// again with the script's own arguments each time, as the VM reruns it.
bool ask_through(s32* args, int offset, s32 r) {
  s32 own[7];
  std::memcpy(own, args, sizeof own);
  for (int phase = 0; phase < 3; ++phase) {
    std::memcpy(args, own, sizeof own);
    if (!ap::on_map_transfer(offset, args) || !ap::transfer_again()) return false;
    if (phase == 1) vm_set_r(static_cast<u32>(r));  // (the choice's answer)
  }
  std::memcpy(args, own, sizeof own);
  return !ap::on_map_transfer(offset, args) && !ap::transfer_again();
}

}  // namespace

TEST(ap_hub_ask) {
  ap::stop();
  // The format.
  {
    ap::Seed s;
    std::string error;
    CHECK(ap::load_seed_text(kAskSeed, &s, &error));
    const ap::HubAsk& a = s.bosses[1].ask;
    CHECK(s.bosses[0].ask.on && s.bosses[0].ask.all_doors && s.bosses[0].ask.text.size() == 2 &&
          s.bosses[0].ask.yes == "Yes." && s.bosses[0].ask.no == "No.");
    CHECK(a.on && !a.all_doors && a.doors == std::vector<int64_t>{19922948} && a.options.size() == 2);
    CHECK(a.options[0].map == 297 && a.options[0].window == 4 && a.options[0].choice == 0x14 &&
          a.options[0].text == "Face Marsilio." && a.options[0].to.map == 305 && a.options[0].to.dir == 271);
    auto refused = [&](const std::string& from, const std::string& to, const std::string& part) {
      std::string t = kAskSeed;
      t.replace(t.find(from), from.size(), to);
      ap::Seed u;
      std::string e;
      const bool ok = !ap::load_seed_text(t, &u, &e) && e.find(part) != std::string::npos;
      if (!ok) std::fprintf(stderr, "  expected '%s', got '%s'\n", part.c_str(), e.c_str());
      return ok;
    };
    CHECK(refused("on the rampage ahead.", "on the rampage ahead of the party.", "ask.text: 1-3 lines"));
    CHECK(refused("\\nFace Shigeo now?", "\\na\\nb\\nc", "ask.text: 1-3 lines"));
    CHECK(refused("\"window\": \"0x4\", \"choice\": \"0x14\", \"text\": \"Face", "\"choice\": \"0x14\", \"text\": \"Face", "ask.options[0]"));
    CHECK(refused("\"doors\": [19922948]", "\"doors\": 5", "ask.doors must be an array"));
  }
  Fixture f;
  set_story(f, 60, 4000);
  CHECK(start_hub(kAskSeed));
  f.load({op(0x17), op(0x15, 1)}, 1);  // (a thread for the questions)
  // Shigeo: the door 280 -> 281 asks; yes: the view comes on with 281's load.
  set_map(f, 280);
  ap::on_script_loaded(280, {});
  s32 in[7] = {281, 0, -3795, 0, -3406, 177, 136};
  CHECK(ask_through(in, 0x6934, 0));
  CHECK(in[0] == 281 && in[2] == -3795);
  CHECK_EQ(hub_log_count("ask: Shigeo, encounter 533: door 18356813 (map 280 -> map 281): \"Shigeo is on the rampage ahead. / Face Shigeo now?\""), 1u);
  CHECK_EQ(hub_log_count("ask: Shigeo, encounter 533: yes: map 281 with its story view"), 1u);
  set_map(f, 281);
  ap::on_script_loaded(281, {});
  CHECK(ap::hub_view_encounter() == 533);
  // A save made now remembers the view (a yes), its load turns it on again with the map's load.
  CHECK(ap::hub_asked_view() == 533);
  ap::on_loaded("", 0);
  CHECK(!ap::hub_view_on());
  ap::hub_resume_view(533);
  ap::on_script_loaded(281, {});
  CHECK(ap::hub_view_encounter() == 533);
  // A transfer inside the chamber never asks.
  s32 inside[7] = {281, 0, 0, 0, 0, 0, 0};
  CHECK(!ap::on_map_transfer(0x100, inside) && !ap::transfer_again());
  // No (and a cancel): 281 at the hub's state; the question comes again next time; a load: no view.
  for (const s32 answer : {1, -1}) {
    set_map(f, 297);
    ap::on_script_loaded(297, {});
    CHECK(!ap::hub_view_on());
    s32 back[7] = {281, 128, -5774, 0, -3487, 357, 130};
    CHECK(ask_through(back, 0x47a4, answer));
    CHECK(back[0] == 281);
    set_map(f, 281);
    ap::on_script_loaded(281, {});
    CHECK(!ap::hub_view_on());
  }
  CHECK_EQ(hub_log_count("ask: Shigeo, encounter 533: no: map 281 at the hub's state"), 2u);
  CHECK_EQ(hub_log_count("chamber map 281 (Shigeo, encounter 533): no story view (the answer was no)"), 2u);
  CHECK(ap::hub_asked_view() == -1);
  ap::on_loaded("", 0);
  ap::on_script_loaded(281, {});
  CHECK(!ap::hub_view_on());
  CHECK_EQ(hub_log_count("no story view (not asked: a load or another way in)"), 1u);
  // Marsilio: the antechamber's door (304 -> 296, sent into 305 by the seed door) asks; no is the
  // door's own target, yes the arena with its view.
  std::vector<u8> s304(0x1C + 0x14, 0);
  const u32 transfer = 0xDA000000u;
  std::memcpy(s304.data() + 0x1C + 0x10, &transfer, 4);
  set_map(f, 304);
  ap::on_script_loaded(304, s304);
  s32 door[7] = {296, 0, -6, 0, 1014, 91, 128};
  CHECK(ask_through(door, 0x10, 1));
  CHECK(door[0] == 296 && door[2] == -6 && door[4] == 1014);
  CHECK_EQ(hub_log_count("ask: Marsilio, encounter 532: no: door 19922948 goes to its own target (map 296)"), 1u);
  s32 door2[7] = {296, 0, -6, 0, 1014, 91, 128};
  CHECK(ask_through(door2, 0x10, 0));
  CHECK(door2[0] == 305 && door2[2] == -13 && door2[4] == 276 && door2[5] == 271);
  set_map(f, 305);
  ap::on_script_loaded(305, {});
  CHECK(ap::hub_view_encounter() == 532);
  // Another way into 305 (the desk's registration, not in `doors`): no question, no view.
  set_map(f, 297);
  ap::on_script_loaded(297, {});
  s32 desk[7] = {305, 0, -13, 0, 276, 271, 128};
  CHECK(!ap::on_map_transfer(0x6124, desk) && !ap::transfer_again() && desk[0] == 305);
  set_map(f, 305);
  ap::on_script_loaded(305, {});
  CHECK(!ap::hub_view_on());
  // The desk's extra option: the options window one line taller with it, the choice one option
  // longer; chosen (R = 2 after the store), the party goes to 305 with the view on.
  set_map(f, 297);
  f.load(kChoiceScript, 1, {std::string("\x10\x11\x80\x80\x10\x12", 6)});
  ap::on_script_loaded(297, f.script);
  CHECK_EQ(ap::g_hub_sites, 1);
  g_trace.clear();
  vm_svc_set_trace(record);
  g_vm_step_limit = 20;
  f.frame();
  g_vm_step_limit = 0;
  vm_svc_set_trace(nullptr);
  bool taller = false;
  for (const std::string& t : g_trace) taller = taller || t.find("msg_window_open(rect=9,11,24,3 ") == 0;
  CHECK(taller);
  CHECK_EQ(f.ctx.choice_count, 3);
  CHECK_EQ(hub_log_count("ask: Marsilio, encounter 532: map 297's choice at 0x14: option 2 \"Face Marsilio.\" added"), 1u);
  CHECK(f.ip() == 9 * 4);  // (R -1 without windows: the routine went on as its own)
  f.ctx.ip = f.ctx.code + 8;
  vm_set_r(2);
  CHECK(ap::hub_fetch(&f.ctx));
  CHECK(f.ctx.wait_state == 1 && f.ctx.wait_param == 305);
  CHECK_EQ(hub_log_count("ask: Marsilio, encounter 532: \"Face Marsilio.\" chosen: to map 305 (-13, 0, 276), with its story view"), 1u);
  set_map(f, 305);
  ap::on_script_loaded(305, f.script);  // (the Clerk's site: 305's script here is the test's)
  CHECK(ap::hub_view_encounter() == 532);
  CHECK_EQ(ap::g_hub_sites, 1);
  // The Clerk's extra option: out of the arena, the view ends with 304's load, nothing recorded.
  f.ctx.ip = f.ctx.code + 8;
  vm_set_r(2);
  CHECK(ap::hub_fetch(&f.ctx));
  CHECK(f.ctx.wait_state == 1 && f.ctx.wait_param == 304);
  set_map(f, 304);
  ap::on_script_loaded(304, s304);
  CHECK(!ap::hub_view_on() && !ap::goal_won(532));
  // In 305 without the view (the normal arena) the Clerk's site adds nothing; another option of
  // the choice is the routine's own.
  set_map(f, 305);
  ap::on_script_loaded(305, f.script);
  CHECK(!ap::hub_view_on());
  f.ctx.ip = f.ctx.code + 1;
  CHECK(!ap::hub_fetch(&f.ctx));
  set_map(f, 297);
  ap::on_script_loaded(297, f.script);
  f.ctx.ip = f.ctx.code + 8;
  vm_set_r(1);
  CHECK(!ap::hub_fetch(&f.ctx));
  // A site whose words differ: no extra option.
  std::vector<u8> other = f.script;
  other[0x1C + 0x14 + 3] = 0xDD;  // (ext 0xdd: another form)
  const size_t misses = hub_log_count("is not the shape the seed names");
  ap::on_script_loaded(297, other);
  CHECK_EQ(ap::g_hub_sites, 0);
  CHECK_EQ(hub_log_count("is not the shape the seed names"), misses + 1);
  // Won: no question, no extra option; the door is the original again.
  ap::on_battle_exit(532, 0);
  ap::on_script_loaded(297, f.script);
  f.ctx.ip = f.ctx.code + 1;
  CHECK(!ap::hub_fetch(&f.ctx));
  set_map(f, 304);
  ap::on_script_loaded(304, s304);
  s32 after[7] = {296, 0, -6, 0, 1014, 91, 128};
  CHECK(!ap::on_map_transfer(0x10, after) && !ap::transfer_again() && after[0] == 296);
  ap::stop();
  // Without a hub seed: no question.
  set_map(f, 280);
  s32 none[7] = {281, 0, -3795, 0, -3406, 177, 136};
  CHECK(!ap::on_map_transfer(0x6934, none) && !ap::transfer_again() && none[0] == 281);
}
