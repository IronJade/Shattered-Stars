# Star Ocean 2 data formats

What's verified so far, with the code that defines each format. Addresses refer to
`functions.csv`. Tools in `tools/` implement every format listed here.

## Disc layout

| Disc | Executable | Archive | Archive LBA |
|---|---|---|---|
| 1 | `SCUS_944.21` | `SO2.BIN` (listed in the filesystem) | 300 |
| 2 | `SCUS_944.22` (byte-identical) | not in the directory; dumped as `UNKN0000.DAT` | 300 |

## Archive table of contents (`loader_read_toc`)

- The first `0x5A00` bytes of the archive: 4608 little-endian u32 words, then 4608 bytes.
- Decrypt with `key = 0x13578642`; for each `i`: `t = key ^ (key << 1)`, `word[i] ^= key`,
  `byte[i] ^= t & 0xFF`, `key = ~t ^ 0x13578642` (all 32-bit).
- Word bits 0–23 are the entry's BCD disc position (minute, second, sector).
  LBA = `(min*60 + sec)*75 + sector - 150`.
- Sector count = `(word >> 24) << 8 | byte[i]`.
- Entry 0 is the table's own slot; the game points it at LBA 300 before reading.
- Tool: `so2_archive.py toc`.

## SLZ compressed blocks (`slz_unpack`)

16-byte header, then data:

| Offset | Field |
|---|---|
| 0 | `"SLZ"` |
| 3 | method: 0 stored, 1 LZSS, 2 LZSS + run-length fills |
| 4 | packed size |
| 8 | unpacked size |
| 0xC | offset to the next block in a chain (0 = last) |

- **Method 1:** flag byte per 8 items, low bit first; 1 = literal byte. A reference is
  2 bytes `b0 b1`: distance `b0 | (b1 & 0xF) << 8`, length `(b1 >> 4) + 3`. Distance 0 ends.
- **Method 2:** as method 1, stopping after `unpacked size` bytes. Length nibble `0xF` is a
  fill: if `b1 & 0xF == 0`, repeat `b2` `b0 + 19` times (3-byte token); otherwise repeat
  `b0` `(b1 & 0xF) + 3` times.
- Tool: `so2_archive.py extract` / `unpack`.

## Programs

The boot executable stays resident at `0x80010000`. Whole programs are SLZ archive entries
that `boot_run_main_program` unpacks to `0x8002f810` and jumps into; each program ends by
loading the next one and calling it again.

| Entry | Loads at | Role |
|---|---|---|
| #1 | `0x8002f810` | title / startup |
| #3100 (= #2576) | `0x8002f810` | field: map exploration and the event-script VM |
| #2129 | `0x8002f810` | not yet identified (probably battle) |

Programs share state through the handoff registry (`handoff_store` / `handoff_get`, slots
5, 0xb, 0xd, 0xf, 0x10, 0x11) inside the System object.

Second-level code overlays load at `0x8007e000` (for the field program) and around
`0x800d1b28`–`0x800de23c` (for #2129). The field program also calls code near
`0x801ea000` and `0x801f9800` whose source entries aren't identified yet.

## Map data (entry `0xC87 + map`, i.e. 3207 + map)

Container: u32 section count, then `(u32 section id, u32 offset)` pairs.

| Section | Contents |
|---|---|
| 0 | SLZ method 2 chain (not yet decoded; probably geometry / graphics) |
| 1 | SLZ block: the map's event script (below) |
| 2 | SLZ block: not yet decoded (begins with a count and an offset table) |

964 map scripts parse on disc 1.

## Event script (map section 1, unpacked)

| Offset | Field |
|---|---|
| 0x00 | code size in bytes |
| 0x04 | unknown (0 in samples) |
| 0x08 | unknown |
| 0x0C | string count |
| 0x10 | text size in bytes |
| 0x14, 0x18 | unknown (possibly local variable / flag area sizes) |
| 0x1C | code (32-bit instructions); branch targets are word indexes from here |
| 0x1C + code size | string offset table: `string count` u16 values, relative to the text start |
| after that | text |

### Instruction encoding

- Bits 24–31: opcode. Bit 23: operand is in the thread's locals (1) or globals (0).
  Bits 0–22: address or immediate.
- Typed operands (opcodes 0x21–0x60): bits 16–19 type (0 s32, 1 s16, 2 s8, 3 flag,
  8 u32, 9 u16, 10 u8), bits 0–15 address.
- Opcodes 0x64 and up take N arguments (`script_opcodes.csv`): inline when bit 23 is clear
  (first argument in bits 0–15, the rest packed two 16-bit values per following word,
  N // 2 extra words), popped from the value stack when it is set.
- `0xfe` / `0xff` carry a sub-command in bits 16–22 (tables A and B).

### Memory model

- `script_globals`: byte-addressed variables; word 0 is the return register R.
- `script_flags`: bit array (flag n = byte n >> 3, bit n & 7).
- Thread locals: variables below `ctx+0x18`, flags below `ctx+0x1c`, both addressed downward.
- Value stack at `ctx+0x24`, growing upward. Call stack at `ctx+0x20`.

Script context fields: `+0x08` current instruction, `+0x0c` wait counter, `+0x10` instruction
pointer, `+0x14` code base, `+0x28` string offset table, `+0x2c` text, `+0x30` pending wait
state, `+0x34` its parameter, `+0x4c + n*0x48` thread slots.

### Text

- 16-bit little-endian character codes: `0x1B6`–`0x1CF` = A–Z, `0x1D0`–`0x1E9` = a–z,
  `0x285` = space. Other printable codes are not mapped yet.
- Codes with bit 15 set are control codes followed by one parameter byte.

## Inventory entry

u16: bits 0–9 item id, bits 10–14 count.

## New Game data (entries #14-#16, read back to back)

- **#14** is a chain of four SLZ blocks:
  - block 0: string table, `{u16 id, u16 offset}` pairs ending in id 0xffff, then the strings as glyph codes (code = glyph index + 1; a byte above 0x7f is followed by a second byte: `(b0 & 0x7f) | b1 << 7`; 0 ends). Glyph *i* is ASCII *i* + 0x20 for the printable range. Ids: 0-18 sound, 32-45 characters, 46-57 vibration, 58-78 combat mode, 100-101 family names, 110-114 keyboard buttons, 150-170 battle rank, 200-207 memory card notices.
  - block 1: font, `s32 count` (96), `s32 height` (12), `u8 width[count]`, padded to 4, then `u16 rows[height]` per glyph (1 bit per pixel, bit 15 = leftmost).
  - block 2: a TIM whose palette (at +0x14) is the text CLUT row 0x1f9 (palette *n* = CLUT x 16·*n*).
  - block 3: menu art (texture pack block) uploaded to (0x140, 0), CLUT row 0x1e0.
- **#15** block 0: window skin; `+0xc` → inner block; `inner + inner[0] + 4` = 256-colour CLUT (row 0x1fe), `inner + inner[1]` = `{u16 w, u16 h, pixels}` 4-bit, uploaded to (0x3c0, 0).
- **#16** blocks 0 / 1: Claude / Rena portraits, 16-bit, at (0x140 / 0x240, 0x100); widths 0xce / 0xe4.
- **#2674 / #2675** (0xa72 / 0xa73): `u32 bank_offset`, an SLZ sequence at +4, the sound bank at `bank_offset` (the Sound Select / later screens' demo tunes).
- **#2680 / #2681**: first part of Claude's / Rena's prologue (loaded into a 0x52800 buffer during name entry).

## Party slot (0x60 bytes, handoff 3) and character record (0xd0 bytes, handoff 4)

Written by `party_slot_init` (title 0x80061f80). Slot: `+0` u16 character (1 Claude, 2 Rena, ...), `+3` u8 (1 Claude, 9 Rena), `+0x10` u32, `+0x14/+0x18/+0x1c` u32 HP-like triple (Claude 130, Rena 100), `+0x20/+0x22/+0x24` u16 MP-like triple (20 / 40), `+0x28` u16 level 1, then u16 stats at `+0x2a..+0x54`. Record: `+0` u16, `+6` u16, `+0xc..+0x19` u16 equipment-like ids (Rena: 0x21b, 0x2a9), `+0x20` u32 talent bits (`roll_talents`), `+0x24` name (C string; overwritten with the entered name), skill bytes from `+0x3c`.

## STR movies (`movie_open`; disc 1 #4563-#4581 and #4586, disc 2 #4573-#4604)

Mode 2 Form 1 sectors, video only (no XA audio: the music comes from the sound system),
320x240, bitstream version 2, ten sectors ("chunks") per frame. Each sector: a 32-byte
header (`0x0160`, `0x8001`, chunk, chunk count, u32 frame number from 1, u32 bitstream
bytes, u16 width, u16 height, a copy of the bitstream header, 0) and 2016 bytes of the
frame's bitstream. Decoder: `engine/src/disc/str.h` + `engine/src/psx/mdec.h`.

The game streams an entry with ReadS at double speed (150 sectors/s: 15 frames a second)
and **never reads its last 30 sectors** (`loader_start_stream(..., size - 0xF000)`), so
the logo movie #4586 shows 45 of its 48 frames and the opening #4563 2600 of its 2603.
The ring is two slots of `0x5000` bytes; each slot holds one frame as a table of the ten
32-byte sector headers (`+0x000`-`+0x13F`) followed by the ten 2016-byte payloads
(`+0x140`, contiguous, so the bitstream can be decoded in place). The loader clears a
slot's header table before filling it, and `movie_update` decodes a slot only if its
headers carry chunks 0-9 of one frame number (else the frame is skipped but counted in
`g_movie_frame_count`). In 24-bit mode the MDEC output goes out in 20 columns of 16
pixels (`mode * 0x3c0` words each) through two slice buffers and `LoadImage`, into the
display buffer that is not on screen.

| Entry | Contents |
|---|---|
| #4563 (`0x11d3`) | opening (title intro on disc 1, attract mode) |
| #4567 (`0x11d7`) | Claude's prologue movie (cue list #4505) |
| #4568 (`0x11d8`) | Rena's prologue movie (cue list #4507) |
| #4586 (`0x11ea`) | studio logo (LINKS) |

## Prologue cue list (entries #4504-#4507, `prologue_update`)

Loaded whole (`loader_load_sync`) and handed to the prologue player as its script
(`+0x50`) and cue pointer (`+0x64`). A run of cues, each `u32 time, u32 opcode` followed
by the opcode's arguments (u32 each). A cue runs once the clock reaches its time
(unsigned compare): the clock is `g_movie_frame_count` once a movie cue has run, and
vblanks otherwise. The sound clips the cues play are stored after the cue list in the
same entry.

| Op | Arguments | Effect |
|---|---|---|
| 0 | — | end: `prologue_update` returns 1 |
| 1 | entry | `movie_open(entry)`; the clock becomes the movie's frame count |
| 2 | — | starts the music `prologue_load_sound` unpacked (`sound_play_sequence`) |
| 3 | offset, pan, volume, flag | sound clip at `script + offset`: `sound_open_clip(.., 2, 0)` then `sound_start_clip(slot, volume & 0xff, pan & 0xff, 0x2000, flag & 0xff)` |
| 4 | small, large | controller vibration (`pad_vibrate(pad, 0, small, large)`) |
| 5 | s16 frames (word) | `sound_fade_out_all(frames)`; the destructor then skips its own fade |
| other | — | ignored (no arguments) |

| Entry | Path | Cues |
|---|---|---|
| #4504 (`0x1198`) | Claude, part 1 | clip at offset 32 at time 0; end at 1220 vblanks |
| #4505 (`0x1199`) | Claude, part 2 | movie #4567 at 0; clip (offset 788) at frame 8; music at 15; 46 vibration cues; end at frame 1150 (the movie's last) |
| #4506 (`0x119a`) | Claude, part 3 | clip at 30 vblanks; end at 250 |
| #4507 (`0x119b`) | Rena | movie #4568 at 0; music at 5; two clips (offsets 268 and 200668) at frame 410; 12 vibration cues; end at frame 906 |

The music and bank come from `g_prologue_data` (entry #2680 for Claude, #2681 for Rena:
a sound bank at the offset given by the first word, then an SLZ block of music).
