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
#include "game/boot/system.h"
#include "game/field/field.h"
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
