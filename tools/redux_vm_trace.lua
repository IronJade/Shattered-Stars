-- redux_vm_trace.lua: log the field program's event-script VM in PCSX-Redux, to compare
-- with the port (engine/tests/test_script_vm.cpp writes "frame map offset instr word1
-- word2" lines with SS_VM_EXT_TRACE=FILE, one file per map).
--
-- Every call of vm_exec_ext (field 0x8006241c, i.e. every instruction 0x64 and up) prints
--   vsyncs updates map offset instr word1 word2 sync depth
-- offset = the instruction's byte offset from the code base, word1/word2 = the words after
-- it (inline arguments), sync = ctx+0x10a4 (1 inside vm_call_sync: actor command
-- callbacks, menu hooks), depth = ctx+0x118c. 'updates' counts vm_thread_update calls
-- (sync calls included). The breakpoints check the code bytes, so the title program
-- (also at 0x8002f810) is ignored.
--
-- Environment: SS_VM_LOG output file (default vmtrace.log). Load it next to
-- redux_reference.lua (which scripts the controller to reach the first map):
--   dofile('tools/redux_reference.lua'); dofile('tools/redux_vm_trace.lua')
-- Compare the lines with sync == 0 to the port's trace: same offsets and instruction
-- words, in the same order (map 7's opening: 132 of 132 up to Ronixis's first window;
-- map 18: 86 of 86). With SS_REF_FUNC=0x8003d3c8 the input schedule of
-- analysis/reference/firstmap reaches map 7 by vsync ~7600; adding "1100:right,1106:none"
-- picks Rena (map 18).

local ffi = require('ffi')
local mem = PCSX.getMemPtr()
local function u32(a) return ffi.cast('uint32_t*', mem + bit.band(a, 0x1ffffc))[0] end
local function s16(a) return ffi.cast('int16_t*', mem + bit.band(a, 0x1ffffe))[0] end

local VM_EXEC_EXT, VM_THREAD_UPDATE = 0x8006241c, 0x8006b944
local out = io.open(os.getenv('SS_VM_LOG') or 'vmtrace.log', 'w')
local updates, vsyncs = 0, 0

ss_vm_vsync = PCSX.Events.createEventListener('GPU::Vsync', function() vsyncs = vsyncs + 1 end)

ss_vm_update = PCSX.addBreakpoint(VM_THREAD_UPDATE, 'Exec', 4, 'vm update', function()
  if u32(VM_THREAD_UPDATE) ~= 0x27bdffa8 then return true end
  updates = updates + 1
  return true
end, 'ss_vm_update')

ss_vm_ext = PCSX.addBreakpoint(VM_EXEC_EXT, 'Exec', 4, 'vm ext', function()
  if u32(VM_EXEC_EXT) ~= 0x27bdfef8 then return true end
  local r = PCSX.getRegisters().GPR.n
  local ctx, instr = r.a0, r.a1
  local ipa = u32(ctx + 0x10)  -- already past the instruction word
  local map = s16(u32(0x80075710) + 0x1a)
  out:write(string.format('%d %d %d %06x %08x %08x %08x %d %d\n', vsyncs, updates, map, ipa - u32(ctx + 0x14) - 4,
                          instr, u32(ipa), u32(ipa + 4), u32(ctx + 0x10a4), u32(ctx + 0x118c)))
  out:flush()
  return true
end, 'ss_vm_ext')

print('redux_vm_trace: logging vm_exec_ext')
