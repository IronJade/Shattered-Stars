-- redux_sound.lua: reference captures of Star Ocean 2's sound driver in PCSX-Redux.
--
-- Boots disc 1, skips the opening movie once its music starts, and records from the
-- title screen's music on:
--   events.log     SpuWrite / SpuSetKey / SpuSetVoiceAttr calls with the driver's
--                  interrupt count (Sound + 0x2c) -- needs -debugger (SS_SND_TRACE=1)
--   spuwrite_*.bin the bytes of every SpuWrite (DMA to SPU RAM), named by address/size
--   sound_*.bin    the Sound object (0x33b8 bytes) when each sequence starts
-- and quits SS_SND_SECONDS (default 40) seconds after the title music started, or
-- SS_SND_QUIT_AT vblanks after it when set (for runs past the title, e.g. into the prologue).
-- The trace also logs clip loading and SpuSt streaming (SpuStTransfer, the driver's three
-- SpuSt callbacks, SpuSetIRQAddr, sound_open_clip / start_clip / play_sfx / import_effects).
--
-- Environment: SS_SND_DIR output directory, SS_SND_TRACE=1 for the breakpoints.
-- Audio: run with SDL_AUDIO_DRIVER=disk SDL_AUDIO_DISK_OUTPUT_FILE=out.raw.

local ffi = require('ffi')
local mem = PCSX.getMemPtr()
local function u16(a) return ffi.cast('uint16_t*', mem + bit.band(a, 0x1ffffe))[0] end
local function s16(a) return ffi.cast('int16_t*', mem + bit.band(a, 0x1ffffe))[0] end
local function u32(a) return ffi.cast('uint32_t*', mem + bit.band(a, 0x1ffffc))[0] end
local dir = os.getenv('SS_SND_DIR') or '.'
local seconds = tonumber(os.getenv('SS_SND_SECONDS') or '40')
local trace = os.getenv('SS_SND_TRACE') == '1'
local log = io.open(dir .. '/events.log', 'w')

local G_SOUND = 0x8002b6e0
local vsync = 0
local function sound() return u32(G_SOUND) end
local function ticks()
  local s = sound()
  if s == 0 then return -1 end
  return u32(s + 0x2c)
end
local function dump(name, addr, size)
  local f = io.open(dir .. '/' .. name, 'wb')
  f:write(ffi.string(mem + bit.band(addr, 0x1fffff), size))
  f:close()
end

-- Sequence starts, by polling Sound + 0x30 every vblank (works without the debugger).
local plays, last_seq, title_start = 0, 0, nil
-- SS_SND_STEPS: extra presses after the title music starts, "OFFSET:BUTTON,..." (vblanks);
-- SS_SND_QUIT_PLAY: quit SS_SND_SECONDS after this sequence start instead of the title's.
local steps = {}
for off, name in string.gmatch(os.getenv('SS_SND_STEPS') or '', '(%d+):(%a+)') do steps[#steps + 1] = { tonumber(off), string.upper(name) } end
local quit_play = tonumber(os.getenv('SS_SND_QUIT_PLAY') or '2')
local quit_start = nil
local pad = PCSX.SIO0.slots[1].pads[1]
local press_at, release_at = nil, nil

ss_snd_vsync = PCSX.Events.createEventListener('GPU::Vsync', function()
  vsync = vsync + 1
  local s = sound()
  if s ~= 0 then
    local seq = u32(s + 0x30)
    if seq ~= last_seq and seq ~= 0 then
      plays = plays + 1
      last_seq = seq
      log:write(string.format('vsync %d tick %d play %d seq=%08x\n', vsync, ticks(), plays, seq))
      log:flush()
      dump(string.format('sound_play%d.bin', plays), s, 0x33b8)
      dump(string.format('sequence_play%d.bin', plays), seq, 0x8000)
      if plays == 1 then press_at = vsync + 90 end
      if plays == 2 then title_start = vsync end
      if plays == quit_play then quit_start = vsync end
    end
  end
  if press_at and vsync == press_at then pad.setOverride(PCSX.CONSTS.PAD.BUTTON.START) end
  if press_at and vsync == press_at + 6 then pad.clearOverride(PCSX.CONSTS.PAD.BUTTON.START) end
  if title_start then
    for _, st in ipairs(steps) do
      if vsync == title_start + st[1] then pad.setOverride(PCSX.CONSTS.PAD.BUTTON[st[2]]) end
      if vsync == title_start + st[1] + 6 then pad.clearOverride(PCSX.CONSTS.PAD.BUTTON[st[2]]) end
    end
  end
  local quit_at = tonumber(os.getenv('SS_SND_QUIT_AT') or '0')
  if quit_at > 0 and title_start and vsync >= title_start + quit_at then
    log:write(string.format('vsync %d tick %d quit\n', vsync, ticks()))
    log:close()
    PCSX.quit(0)
  end
  if quit_at == 0 and quit_start and vsync >= quit_start + math.floor(seconds * 59.94) then
    log:write(string.format('vsync %d tick %d quit\n', vsync, ticks()))
    log:close()
    PCSX.quit(0)
  end
end)

if trace then
  ss_snd_write = PCSX.addBreakpoint(0x8001d990, 'Exec', 4, 'SpuWrite', function()
    local r = PCSX.getRegisters().GPR.n
    local spu = u16(0x8002b77c) * 8
    dump(string.format('spuwrite_%05x_%05x.bin', spu, r.a1), r.a0, r.a1)
    log:write(string.format('vsync %d tick %d SpuWrite spu=%05x size=%x src=%08x\n', vsync, ticks(), spu, r.a1, r.a0))
    return true
  end, 'ss_snd_write')
  ss_snd_key = PCSX.addBreakpoint(0x8001d7d0, 'Exec', 4, 'SpuSetKey', function()
    local r = PCSX.getRegisters().GPR.n
    if r.a1 ~= 0 then
      log:write(string.format('vsync %d tick %d key %d %06x\n', vsync, ticks(), r.a0, bit.band(r.a1, 0xffffff)))
    end
    return true
  end, 'ss_snd_key')
  ss_snd_attr = PCSX.addBreakpoint(0x8001f480, 'Exec', 4, 'SpuSetVoiceAttr', function()
    local a = PCSX.getRegisters().GPR.n.a0
    log:write(string.format('vsync %d tick %d attr %06x mask=%x vol=%d,%d pitch=%04x addr=%05x adsr=%04x,%04x\n',
      vsync, ticks(), u32(a), u32(a + 4), s16(a + 8), s16(a + 10), u16(a + 0x14), u32(a + 0x1c),
      u16(a + 0x3a), u16(a + 0x3c)))
    return true
  end, 'ss_snd_attr')
end

-- Streamed clips (SpuSt) and effect loading: the prologue's voice clips.
if trace then
  local function bp(addr, name, fn) return PCSX.addBreakpoint(addr, 'Exec', 4, name, function() fn(PCSX.getRegisters().GPR.n); return true end, name) end
  ss_c1 = bp(0x8001ec20, 'SpuStTransfer', function(r) log:write(string.format('vsync %d tick %d sttransfer %d %06x\n', vsync, ticks(), r.a0, r.a1)) end)
  ss_c2 = bp(0x800145ec, 'st_prepared', function(r) log:write(string.format('vsync %d tick %d stcb prepared %06x\n', vsync, ticks(), r.a0)) end)
  ss_c3 = bp(0x80014648, 'st_transferred', function(r) log:write(string.format('vsync %d tick %d stcb transferred %06x\n', vsync, ticks(), r.a0)) end)
  ss_c4 = bp(0x80014674, 'st_finished', function(r) log:write(string.format('vsync %d tick %d stcb finished %06x\n', vsync, ticks(), r.a0)) end)
  ss_c5 = bp(0x8001f3d0, 'SpuSetIRQAddr', function(r) log:write(string.format('vsync %d tick %d irqaddr %05x\n', vsync, ticks(), r.a0)) end)
  ss_c6 = bp(0x800197ec, 'sound_open_clip', function(r) log:write(string.format('vsync %d tick %d open_clip %08x mode %d out %08x\n', vsync, ticks(), r.a1, r.a2, r.a3)) end)
  ss_c7 = bp(0x8001a4b8, 'sound_start_clip', function(r) log:write(string.format('vsync %d tick %d start_clip slot %d vol %d pan %d pitch %d\n', vsync, ticks(), r.a1, r.a2, r.a3, u16(r.sp + 0x10))) end)
  ss_c8 = bp(0x80019b1c, 'sound_play_sfx', function(r) log:write(string.format('vsync %d tick %d sfx %08x %d %04x\n', vsync, ticks(), r.a1, r.a2, r.a3)) end)
  ss_c9 = bp(0x800192fc, 'sound_import_effects', function(r) log:write(string.format('vsync %d tick %d import %08x slot %d keep %d\n', vsync, ticks(), r.a1, r.a2, r.a3)) end)
end

print('redux_sound: capturing to ' .. dir)
