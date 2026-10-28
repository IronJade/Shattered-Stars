-- so2_loadwatch.lua: log Star Ocean 2's archive loads live in PCSX-Redux.
--
-- Breakpoints sit on the boot executable's loader (SCUS_944.21/.22, resident
-- the whole game) and read the game's own decrypted table of contents from RAM:
--   FUN_80011654(loader, n)            select archive entry n
--   FUN_80011804(loader, dest, size)   start reading it into dest
--   FUN_800121a8(sys, src, dest, blk)  unpack an SLZ block from src to dest
--   0x800122ac                         its only return (jr ra): output complete
-- Each breakpoint logs and returns true, so emulation never pauses.
-- The breakpoints need the debugger: start PCSX-Redux with -debugger, or enable
-- it under Configuration > Emulation. The interpreter CPU is the most reliable.
--
-- GUI: open Lua console, then dofile('/path/to/so2_loadwatch.lua')
-- CLI: pcsx-redux -loadiso "Disc 1.bin" -dofile so2_loadwatch.lua -run
-- Set SO2_DUMP_DIR (env var) to also write each unpacked block to disk.

local ffi = require('ffi')

local SELECT_ENTRY, START_READ, UNPACK, UNPACK_RETURN = 0x80011654, 0x80011804, 0x800121a8, 0x800122ac
local LOG_PATH = os.getenv('SO2_LOG') or 'so2_loads.log'
local DUMP_DIR = os.getenv('SO2_DUMP_DIR')

local mem = PCSX.getMemPtr()
local function u8(addr) return mem[bit.band(addr, 0x1fffff)] end
local function u32(addr) return ffi.cast('uint32_t*', mem + bit.band(addr, 0x1ffffc))[0] end
local function bcd(v) return bit.rshift(v, 4) * 10 + bit.band(v, 0xf) end
local function hex(v) return string.format('%08x', v) end

local logfile = io.open(LOG_PATH, 'a')
local function log(line)
  local stamped = string.format('[cycle %d] %s', tonumber(PCSX.getCPUCycles()), line)
  print(stamped)
  if logfile then logfile:write(stamped, '\n'); logfile:flush() end
end

-- An unpack's destination and size, recorded on entry and dumped at its return.
local pending = nil

local last_entry = nil
local serial = 0

PCSX.addBreakpoint(SELECT_ENTRY, 'Exec', 4, 'so2 select entry', function()
  local r = PCSX.getRegisters().GPR.n
  local loader, n = r.a0, r.a1
  local word = u32(loader + 4 + n * 4)
  local sectors = bit.bor(bit.lshift(bit.rshift(word, 24), 8), u8(loader + 0x4804 + n))
  local m, s, f = bit.band(word, 0xff), bit.band(bit.rshift(word, 8), 0xff), bit.band(bit.rshift(word, 16), 0xff)
  local lba = (bcd(m) * 60 + bcd(s)) * 75 + bcd(f) - 150
  last_entry = n
  log(string.format('SELECT entry #%d  LBA %d  %d sectors (%d bytes)  caller %s',
    n, lba, sectors, sectors * 2048, hex(r.ra)))
  return true
end, 'so2_select')

PCSX.addBreakpoint(START_READ, 'Exec', 4, 'so2 start read', function()
  local r = PCSX.getRegisters().GPR.n
  log(string.format('READ   entry #%s -> RAM %s  (a2=%s)  caller %s',
    tostring(last_entry), hex(r.a1), hex(r.a2), hex(r.ra)))
  return true
end, 'so2_read')

PCSX.addBreakpoint(UNPACK, 'Exec', 4, 'so2 unpack', function()
  local r = PCSX.getRegisters().GPR.n
  local src, dest, blk = r.a1, r.a2, r.a3
  -- Walk the block chain the way FUN_800121a8 does: +0xC holds the offset to the next block.
  local block = src
  for _ = 1, blk do
    local step = u32(block + 0xc)
    if step == 0 then break end
    block = block + step
  end
  local magic = ffi.string(mem + bit.band(block, 0x1fffff), 3)
  local method, size = u8(block + 3), u32(block + 8)
  serial = serial + 1
  log(string.format('UNPACK %s block %d method %d: %s -> %s (%d bytes)  caller %s',
    magic, blk, method, hex(block), hex(dest), size, hex(r.ra)))
  if magic == 'SLZ' then pending = { serial = serial, dest = dest, size = size } end
  return true
end, 'so2_unpack')

PCSX.addBreakpoint(UNPACK_RETURN, 'Exec', 4, 'so2 unpack done', function()
  local result = PCSX.getRegisters().GPR.n.v0
  if pending and DUMP_DIR then
    local name = string.format('%s/%04d_%s.bin', DUMP_DIR, pending.serial, hex(pending.dest))
    local f = io.open(name, 'wb')
    if f then
      f:write(ffi.string(mem + bit.band(pending.dest, 0x1fffff), pending.size))
      f:close()
      log(string.format('  done (result %d), dumped %s', result, name))
    end
  elseif result ~= 1 then
    log(string.format('  unpack returned %d', result))
  end
  pending = nil
  return true
end, 'so2_unpack_done')

log('so2_loadwatch armed: logging to ' .. LOG_PATH .. (DUMP_DIR and (', dumping to ' .. DUMP_DIR) or ''))
