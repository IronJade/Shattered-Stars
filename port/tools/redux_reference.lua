-- redux_reference.lua: capture reference frames from the original game in PCSX-Redux,
-- aligned to calls of a function rather than to vblanks (load times and slow
-- frames shift vblank counts between the console and the port).
--
-- Environment:
--   SS_REF_FUNC   address to count calls of (default 0x8003e54c, title_logo_update)
--   SS_REF_CALLS  comma-separated call numbers to capture at (1 = first call)
--   SS_REF_DIR    output directory; files are call<N>_<w>x<h>_<bpp>.raw
--   SS_REF_QUIT   quit after this many calls (default: last capture + 1)
--   SS_REF_VSYNCS instead of call numbers: vblank offsets after the FIRST call to
--                 capture at (files vsyncNNNNN_*); quits after the last one
--   SS_REF_INPUT  controller script on the same clock: OFFSET:button+button,...
--                 (from that offset on, port 1 holds those buttons; 'none' releases)
--   SS_REF_MARKS  comma-separated addresses; each call prints "[mark] ADDR at vsync N"
--                 on the same clock (to line up screens whose start depends on loading)
--   SS_REF_DUMP   comma-separated mark addresses whose first call also saves main RAM
--                 (2 MB) as SS_REF_DIR/ram_<addr>.bin
--
-- pcsx-redux -cli -interpreter -debugger -loadiso DISC1.cue -dofile redux_reference.lua -run

local func = tonumber(os.getenv('SS_REF_FUNC') or '0x8003e54c')
local dir = os.getenv('SS_REF_DIR') or '.'
local wanted, last = {}, 0
for n in string.gmatch(os.getenv('SS_REF_CALLS') or '', '%d+') do
  wanted[tonumber(n)] = true
  if tonumber(n) > last then last = tonumber(n) end
end
local quit_after = tonumber(os.getenv('SS_REF_QUIT') or tostring(last + 1))
local calls = 0

local function capture(tag)
  local ss = PCSX.GPU.takeScreenShot()
  local bpp = tonumber(ss.bpp) == 0 and 16 or 24
  local name = string.format('%s/%s_%dx%d_%d.raw', dir, tag, ss.width, ss.height, bpp)
  local f = io.open(name, 'wb')
  f:write(tostring(ss.data))
  f:close()
  print('captured ' .. name)
end

local vsync_offsets, last_offset = {}, -1
for n in string.gmatch(os.getenv('SS_REF_VSYNCS') or '', '%d+') do
  vsync_offsets[tonumber(n)] = true
  if tonumber(n) > last_offset then last_offset = tonumber(n) end
end
local since_first = nil
local input_steps = {}
for offset, names in string.gmatch(os.getenv('SS_REF_INPUT') or '', '(%d+):([%a%+]+)') do
  input_steps[tonumber(offset)] = names
end
local pad = PCSX.SIO0.slots[1].pads[1]
local held = {}
local function apply_input(names)
  for button in pairs(held) do pad.clearOverride(PCSX.CONSTS.PAD.BUTTON[button]) end
  held = {}
  for name in string.gmatch(names, '%a+') do
    local button = string.upper(name)
    if PCSX.CONSTS.PAD.BUTTON[button] then
      pad.setOverride(PCSX.CONSTS.PAD.BUTTON[button])
      held[button] = true
    end
  end
end
if last_offset >= 0 then
  ss_ref_vsync = PCSX.Events.createEventListener('GPU::Vsync', function()
    if since_first == nil then return end
    since_first = since_first + 1
    if input_steps[since_first] then apply_input(input_steps[since_first]) end
    if vsync_offsets[since_first] then capture(string.format('vsync%05d', since_first)) end
    if since_first >= last_offset then PCSX.quit(0) end
  end)
end

ss_ref_bp = PCSX.addBreakpoint(func, 'Exec', 4, 'reference capture', function()
  calls = calls + 1
  if since_first == nil then since_first = 0 end
  if last_offset >= 0 then return true end
  if wanted[calls] then capture(string.format('call%05d', calls)) end
  if calls >= quit_after then PCSX.quit(0) end
  return true
end, 'ss_reference')

ss_ref_marks = {}
local dump_at = {}
for addr in string.gmatch(os.getenv('SS_REF_DUMP') or '', '0x%x+') do dump_at[tonumber(addr)] = true end
for addr in string.gmatch(os.getenv('SS_REF_MARKS') or '', '0x%x+') do
  local a = tonumber(addr)
  ss_ref_marks[#ss_ref_marks + 1] = PCSX.addBreakpoint(a, 'Exec', 4, 'mark', function()
    print(string.format('[mark] 0x%08x at vsync %d', a, since_first or -1))
    if dump_at[a] then
      dump_at[a] = nil
      local ffi = require('ffi')
      local f = io.open(string.format('%s/ram_%08x.bin', dir, a), 'wb')
      f:write(ffi.string(PCSX.getMemPtr(), 0x200000))
      f:close()
    end
    return true
  end, 'ss_mark')
end

print(string.format('redux_reference: counting calls of 0x%08x, capturing %s', func, os.getenv('SS_REF_CALLS') or ''))
