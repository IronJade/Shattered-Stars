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
| 0 | SLZ chain: block 0 the map scene (its cell grid at scene +0x80, docs/port/notes/field_part3.md 3.1.2: cell 0 list 14 the floor zones, 0x58 bytes each, list 15 the walls), block 1 the texture directory, then the 256x256 texture pages and the CLUT table (field/map.cpp `field_load_map_sections`; `tools/map_zones.py` lists a map's floor zones) |
| 1 | SLZ block: the map's event script (below) |
| 2 | SLZ block: not yet decoded (begins with a count and an offset table) |

964 map scripts parse on disc 1.

## Field party sprite packs (entries `0xC27 + 2k` and `0xC67 + k`)

`field_load_map` (map.cpp `map_resolve_entries`) loads two party packs into sprite pack slots 0
and 1, one pre-built pack per member combination: pack A = `0xC27 + 2 * k` with k = bits 2-6
of the party member mask (characters 3-7: Celine, Bowman, Dias, Precis, Ashton), pack B =
`0xC67 + k` with k = bits 7-11 (characters 8-12: Leon, Opera, Ernest, Noel, Chisato). Claude
and Rena (models 0 and 1) are in every pack A. The mask counts party slots with a positive id
(active members; plus Private-Action members with script flag 2), so a member who joins on a
map has no field sprite until the next map load. Records are keyed by model = character - 1
(sprite pack format: `port/src/game/field/sprite_pack.h`).

Direction animations (sprite anim ids; `actor_resolve_anim` maps the 12 directions to 7 ids,
the other 5 drawn mirrored: walk 0-6, stand 7-13, run 14-20):

| Models | Ids | Shapes (AnimData frame count / mode / durations) |
|---|---|---|
| 0, 1 (Claude, Rena) | 0-20 | walk 3 frames ping-pong 8 vblanks each; stand 1 frame; run 6 frames 6 vblanks each |
| 2-11 (the other ten) | 0-13 (no run) | walk and stand as the heroes' (identical shapes) |

Scene animations (ids from 0x24) come from the sprite packs the map container registers
(`field_load_map_sections`, slots 2 and up), keyed by model. The world map's party pack (entry 0x105C) has 21 animations for each hero and
4 for each member. Checked with `scratchpad/leader/anims.py` (disc 1, packs `0xC27 + 62`
and `0xC67 + 31`); used by the port's leader stand-in (`port/src/game/field/leader.h`).

## Event script (map section 1, unpacked)

| Offset | Field |
|---|---|
| 0x00 | code size in bytes |
| 0x04 | code start in words after the header (0 in all 964 scripts) |
| 0x08 | main entry (word index): `vm_init` pushes a frame returning there and starts at word 0, which is always `return` |
| 0x0C | string count |
| 0x10 | text size in bytes |
| 0x14 | size of the save-game variables (0x344-0x364): the map's own variables and the call frames' locals start above it (rounded up to 4) |
| 0x18 | size of the save-game flag bytes (0x170): the map's local flags start above it |
| 0x1C | code (32-bit instructions); branch targets are word indexes from here |
| 0x1C + code size | string offset table: `string count` u16 values, relative to the text start |
| after that | text |

### Instruction encoding

- Bits 24–31: opcode. Bit 23: operand is in the thread's locals (1) or globals (0).
  Bits 0–22: address or immediate.
- Typed operands (opcodes 0x21–0x60): bits 16–19 type (0 s32, 1 s16, 2 s8, 3 flag,
  8 u32, 9 u16, 10 u8; the binary ops read s8 unsigned), bits 0–15 address. An operand with
  `instr & 0x80ffff == 0` is `[indexed]`: the address the last `index_load` (0x0a / 0x10)
  computed is OR-ed into the instruction. `index_scale` (0x09 / 0x11) leaves the element
  size (1 byte, 2 s16, 4 s32, anything else = flags, e.g. `0x800001`) for the index loads,
  which put the element in R.
- Opcodes 0x64 and up take N arguments (`script_opcodes.csv`): inline when bit 23 is clear
  (first argument in bits 0–15, the rest packed two 16-bit values per following word,
  N // 2 extra words), popped from the value stack when it is set.
- `0xfe` / `0xff` carry a sub-command in bits 16–22 (tables A and B).

### Memory model

- `script_globals`: byte-addressed variables; word 0 is the return register R.
- `script_flags`: bit array (flag n = byte n >> 3, bit n & 7).
- Locals: a local of size s at address n is at `ctx+0x18 - n - s`, local flag n in byte
  `ctx+0x1c - (n >> 3) - 1`; `frame` (0x18) raises both tops, `return` restores them.
- All of it is the field state block (handoff 9): call stack at +0 (4 words a frame:
  locals offset, local-flags offset, wait counter, IP offset), value stack at +0x100,
  variables at +0x180 (the low variables are the field state, game_state is at +0xa0),
  flags at +0x8c0.

One script thread per map (class ScriptVM, 0x11cc bytes): `+0x08` current instruction,
`+0x0c` wait counter, `+0x10` IP, `+0x14` code base, `+0x18` / `+0x1c` locals / local flags
tops, `+0x20` / `+0x24` call / value stack, `+0x28` string offset table, `+0x2c` text,
`+0x30` wait state, `+0x34` its parameter, `+0x4c + n*0x48` the 58 **trigger regions**
(convex floor quads bound to script functions; not threads). Events (talk, regions, Private
Actions, actor command callbacks) interrupt the thread with a call frame. Full layout and the
per-frame order (triggers, wait counter, music, wait state, instructions) in
`analysis/field_catalog/part4.md`; port in `port/src/game/field/script_*.cpp`, checked
instruction for instruction against the original's openings of maps 7 and 18.

### Story globals and notable flags

From the scripts of every map (`apworld/tools/ap_extract.py`, docs/apworld/field.md):

- **Story globals.**
  - G[0xa0] = game_state +0x00 is the chapter, the main story counter: 3 at a new game as
    Claude (map 7), 4 as Rena (map 18), 60 at the end. Each value 4-60 is stored in one
    place.
  - G[0xa4] = game_state +0x04 is the step inside a chapter, set back to 0 when the chapter
    changes.
  - Chapter 35 is set at map 625 just before B 0x6a switches to disc 2 and the party moves
    to Energy Nede.
- **Flag 1** is the story_route: the new game sets it (state block +0x8c0 bit 1) in Claude's
  game; it is clear in Rena's.
- **Flags 2 and 4** are Private Action mode: the world-map chunk scripts set both before a
  town's PA entrance. Towns register their PA events (talk handlers, ext 0xf3, trigger
  regions) under flag 2, most of them also under B 0x2c saved_party_has(partner).
- **Flag 745** is set by about 750 routines: scene state, not a one-time guard.
- **Flags 66-91** are key items: the shared give routine turns an item number of 10000 + n
  into flag n, with the item fanfare.
- **Flags 758-769** mean that member n (flag 758 + n) was in the party before a split. The
  restore routines (maps 195, 447, 469, 474) add the members back by them.
  - Map 548 (the ship to El, chapter 29 step 30) stores them, then sets flag 765 (Leon).
  - In Rena's game it clears flag 765 again unless the party was Claude and Rena alone.
  - Map 229 then adds Leon to Rena's party in that one case.
- **Flag 112**: Celine travels with the party. Map 131 (chapter 15 step 0, the scene that
  moves the story to step 1000) stores party_has(Celine) in it. It is read in Mars: map 462's
  Varmillion scene (T7, Claude's game, step 2400) sends Celine home with it set (leave routine,
  gear kept) and for good without it (her accessories and item 0x2e7 in slot 4 come off); map
  125 (step 3300, both games) adds her back only with it; maps 125 / 126 pick dialogue by it.
  So the real recruitment is Cross Cave's choice (map 524, chapter 13 step 0, "Travel together
  with Celine?": 0 keeps her, 1 = leave routine with unequip) [world agent, 2026-10-23].
- **The map library's leave routine** (0x63e4 on maps 126, 234, 462, 479, 524 ...; found by
  shape: `pop L4; pop L0; if L4 { B 0x73(L0, 5); B 0x73(L0, 6) }; ext 0xaf(L0); call 0x5698`):
  a member leaves for the reserve; with "unequip" set their accessories come off first (they
  stay with the player). Its join twin (0x63c0 / 0x7dc0) is ext 0xae + A 0x3f. Item characters
  leave for good only at Celine's Cross Cave choice 1 (524, the call at 0x72bc), Mars with flag
  112 clear (462 0x7030), Leon's Eluria colony choice 0 (234 0x6eb0, "Waiting here is better";
  sets flag 500) and Opera's Hoffmann Ruins choice 1 (479 0x8250, right after her own join at
  0x8244; G[0x1be] = 9998, flag 141 cleared). Every other leave of an item character is
  temporary (`scratchpad/world/celine/leaves.txt` lists all 98 calls) [world agent].
- **The story's party splits** [party agent, 2026-10-25; every ext 0xae / 0xaf site of the 820
  field scripts with its conditions and return: `scratchpad/party/splits.md`]. A split stores
  who it took out and its restore adds exactly those back: flags 758-769 (193 Linga, 468 the
  tournament, 548 the ship, 398 chapter 53 -> 195, 469 / 474, 232, 401), **flags 490-496** (444,
  the Front Line in Rena's game, chapter 26 step 1000: Claude, Celine, Ashton, Opera, Precis,
  Ernest, Bowman -> 447 at step 3000). Members a split does not list stay in the party. The other
  hero leaves and comes back through G[0xc5] (27 0x1050 chapter 5 step 3000 -> 61 0x4858 chapter
  6 step 2100; 21 0x510 chapter 7 step 1000 -> 17 0x14ec chapter 8 step 0). **Flag 586**: the Fun
  City arena's team choice (297 sets it, 304 clears it; 305 takes the others out and back by
  locals). **Flag 2318**: the Cave of Trials' party choice (704 0x7820 sets it and takes the
  members not chosen out; 0x6e74 adds them back, 0x6f54 clears it). **Flag 2326**: a Fun City
  contest (689 sets it; 688 takes out all but G[0x20c] and adds them back at 0x966c, then
  clears it). Leaves for good besides the four above: Leon in Rena's game at the Eluria colony
  (234 0x74c8, chapter 31 step 1000, always) and Noel at the Cave of the Red Crystal (417 0x7db8,
  choice 1 "you are needed here to protect the animals"). Two scenes put a non-hero alone into a
  forced fight: map 12 (chapter 4 step 150, Rena's game: Rena out, Claude in, battle_c 0x6e44,
  then back) and the tournament's fights (477: the hero out, Dias or, in Rena's game, Claude in).
  Maps 0-3 / 826 (the world script) hold a join and a leave routine per member that no play
  reaches (a debug menu). Most maps also carry a library routine (229: 0x214c) that takes every
  active member out but four, runs a battle (0x1b94) and adds them back, all in one routine.
  (The four it keeps are named by character id, not by party slot.)
- **The map library's relation routine** (0x2104 on 419 maps such as 0, 0x4b88 on 330 such as
  12, 0x2d00 on 156; one in every map's library; called on maps 208, 211, 437, 469, 523, 575,
  603, 642, 685 (twice) at 0x4b88, 209 at 0x6588, 306 at 0x6874) [heroslot agent, 2026-10-26]: arguments (n, chance %). It reads
  party slots 0..n-1 **by position** with A 0x26 get_slot_char (the only script op that does;
  -1 for an empty or reserve slot), gives each a sex (0: Claude, Bowman, Dias, Ashton, Leon,
  Ernest, Noel; 1: Rena, Celine, Precis, Opera, Chisato), and for every ordered pair (i, j) of
  occupied slots below n (i == j included) with B 0x0a random(100) < chance: same sex B 0x12
  (char i, char j, +1) on config +0xe8, else B 0x10 on config +0x58 (the tables the endings
  read). All twelve calls pass chance 100; n is 4 (the front four: 211, 306, 523, 575, 603, 642,
  685 x2) or 8 (the whole party: 208, 209, 437, 469). So at the n = 4 sites only the members in
  party slots 0-3 gain relation points. Scan: scratchpad/heroslot/scan_slots.py / scan_rel.py.
- **Script ops that move party slots by position** [heroslot agent, 2026-10-26; every field
  script of disc 1 (disc 2's are the same)]: A 0x15 party_compact (55 sites on 26 maps: 17, 27, 61,
  124, 125, 130, 135, 209, 211, 229, 230, 234, 305, 422, 434, 437, 439, 444, 447, 462, 468, 477
  (before each tournament fight), 479, 548, 688, 704), A 0x16 / A 0x17
  save / restore the order (A 0x16 at 305, 468, 479, 688, 704; A 0x17 at 304, 469 twice, 479,
  688, 704. The spans [partybuild6, 2026-10-27; scratchpad/heroslot/slots_d1.txt and
  scratchpad/partybuild/dis/]: the arena 305 0x283d0 -> 304 0x1bd30 (flag 586 meanwhile); the
  tournament 468 0x801c (its start, before G[0xa4] = 1500 at 0x8134) -> 469 0x21d90 / 0x291d0;
  479 0x7148 -> 0x7c20 inside one scene (Opera out, A 0x15, battle 0xd3, then joins 8 and 9
  after the restore); Fun City's contest 688 0x175f0 -> 0x259e0 (flag 2326 cleared right after,
  then map 689); the Cave of Trials 704 0x1e070 (flag 2318 set right after) -> 0x1bd20 (flag
  2318 cleared right after). Free play inside a span happens only inside those flags' or the
  tournament's windows), B 0x29 / B 0x2a
  isolate / restore (the hero to slot 0 for a Private Action and every member back to its saved
  slot after: the map library's PA wrappers, one pair per map), ext 0xa8 special 1 / 2 on map 12
  only (routine 0x6db8, Rena's game chapter 4 step 150: special 1 = Claude's name "????" and
  Claude to slot 0 for battle 190; special 2 at 0x70b8 = his real name, Rena to slot 0 and
  Claude to slot 1). ext 0xb0 (swap two characters) has no call site. Natively,
  party_validate_before_battle (0x80052ff4) moves the first member of slots 4-7 to slot 0 when
  slots 0-3 hold nobody active, and re-points config +0x41 to the first active front slot when
  the leader's slot is not active (it writes config, so the leader stays changed afterwards).
  - [shingofix agent, 2026-10-27] **The tournament's fights (map 477, chapter 19)**, by G[0xa4]:
    the fighter in, the hero out, then A 0x15 and `battle_c`: Claude's game 4400 / 6200 / 10200
    (0x66f4..0x6810: leave routine 0x63e4(Claude, 0), join routine 0x63c0(Dias)); Rena's game
    (0x7b08..0x7c54: 0x63e4(Rena, 0), 0x63c0(Dias / Claude)). After the fight (0x6b34.., 0x7e74..)
    the reverse: 0x63e4(fighter, 0), 0x63c0(hero), A 0x15; Dias's first fight is followed by
    special 0xc (his fixed stats: level 35, HP 2500) and his later ones by special 0xd (his
    starting data again). [shingofix agent, second pass] Per game, by G[0xa4]: Claude's game,
    Dias at 4400 (0xc at 0x6b40), 6200 (0xd 0x6d88), 10200 (0xd 0x6ea8; the final, 469 ends the
    tournament at 10400); Rena's game, Dias at 6500 (0xc 0x7e74), 10200 (0xd 0x81a4), 11500 (no
    special), 16500 (0xd 0x806c), Claude at 8500 / 12600 / 19500 (the final, against Dias). Specials
    0xc / 0xd have no call site on any other map (all 827 scanned). Map 476 (the waiting room, steps
    4600 / 5000 / 5600 / 6000) asks "Is your preparation OK?" (string 110, library choice 0x484c at
    0x6fe0): Yes -> map_transfer_c(472, ...); otherwise A_3d(0) opens the camp menu and it asks
    again; its main routine 0x7480 loops for ever (never free play there). Confirmed headless. Routine 0x63c0 = ext 0xae, A 0x3f (bond clear), 0x5698; 0x63e4 = with
    "unequip" B 0x73 on slots 5 / 6, ext 0xaf, 0x5698. With members set aside before the fighter,
    A 0x15 moves the fighter and the hero up over them (confirmed headless:
    scratchpad/shingofix/runs/t1_base: `[(Ashton), Claude, Rena..]` -> `[Rena, .., Claude, (Dias)]`).
- **The map library's choice routine** (0x484c on maps 144 and 169): a question window and a
  choice. Pushed arguments, in order: x mode, y mode (0xf4241 / 0xf4242: placed by the
  routine), question width, question lines, question string, choice width, **option count**,
  options string, Fol window (1: the party's Fol in window 2 meanwhile). It opens the question
  (ext 0x69, window 1), the choice (ext 0x68, window 0, as many lines as options) and waits on
  ext 0xdf, which takes 2-4 options; returns the option (0-3) or -1 (cancelled) in R. The
  ship NPCs use it: Port Hilton's sailor (169, routine 0x714c: refusals at chapter 18 step >=
  6000 and chapters 23-24, the fare from 0x442c, flags 16 + 17, map_transfer_b to map 1), the
  Hoffmann ship's soldier (169, 0x79c0, registered at chapters 23-27; his sail at 0x7cf8 uses no
  locals and stores chapter 24 at 23), Herlie's sailor (144, 0x7298, flags 16 + 18). France
  numbers map 169's strings one higher (the question is 0x6d, not 0x6c); Germany lays map
  169's code out otherwise from 0x7c70 (+84 bytes). Archipelago's open world patches them
  (docs/apworld/seed_format.md "The harbour") [ships agent, 2026-10-25].
- **Flags 145-151** are the Hoffmann Ruins switches. Each is set by its room's switch
  routine, called with the flag number (maps 494, 497, 501, 505, 509, 519, 522).
  - Flag 149 (map 509) turns the transfers of maps 506 / 510 from map 507 to its other
    version, map 508.
- **Flag 730**: the Final Save Point (map 671) was used. It opens Virtual Expel: Fun City's
  Fighting Arena, map 303 actor 25, sends the party to Arlia map 18.
- **Flag 753**: set after the final boss (map 685, chapter 60). Map 18 registers its chests
  only while it is clear.
- **The hub's P5 places** [hubcore agent, 2026-10-25; docs/apworld/seed_format.md "Hub"]:
  - Eluria Tower's door 557 -> 558 (routine 0x7808): at chapter <= 32 the story's scenes; later
    (0x8584) a message, then the library walk 0x04d8 takes the leader to the door (-158, -3408)
    and the routine waits for it (wait_state 5, actor idle), plays the door's sounds and
    transfers (0x8658, map 558 at 1834, 0, 1, facing 180). Fired from anywhere but the door
    region, the walk never ends (a sweep stalls there; walking in works). Confirmed.
  - Map 558's stairs: its floor zones (scene cell 0 list 14) 0/1 (left, z 570..998) and 2/3
    (right) are ramps from y 0 at x 685 down to y 450 at x -250, landings 8-11 at y 450, then 14-17
    down to y 650. The regions at their foot (thread_start x 20..41 z 142..180, code 0x3c00; x
    282..299 z 141..180, 0x3c60) transfer to map 559 (880, -60, 842) / (935, -54, -830) only
    when actor_get_y(leader) > 400; the regions around them (0x3c44) call B 0x48
    map_preload_owned(559). Confirmed (walked headless).
  - Map 305 (the Fun City arena) set-up 0x9d9c, at chapter 57: step 0 calls 0x661c (the
    transfer to 301), step 10 0x6c14, step 20 registers actor 16 (the Clerk) with talk 0x6e78
    (ext 0x70 at 0x9e28): "Do intensive training" (choice 0: arena battles, back in 305), "Rest",
    "Go see Narl" (choice 2: 0x71b8, Marsilio's scene: battle 532, step 2000, flag 2327, transfer
    to 296 at -5, 0, 823). At 57 / 20 that talk is 305's only entry (no way out on foot).
    Confirmed. The routine before the set-up ends (0x9d3c) by u8 G[0x217] (the arena
    registration's state, 0-3) in 0x7c1c / 0x8220 / 0x8530 / 0x8e48, then 0x91c0 (the transfer
    back to the antechamber 304 at 1339, 0, -6). Likely.
  - [hubask agent, 2026-10-25] **The arena's desk** (map 297, the receptionist actor 16, talk
    0x5c20, registered by ext 0xf3 at chapter != 57): question string 155 (window 1, ext 0x69 at
    0x5c30), options 156 "Hear an explanation. / Register." (ext 0x68 at 0x5c50, window 0, h 2),
    ext 0xdc 2 at 0x5c6c, push R / pop_store L[0] (0x22800000), ext 0x81 0 at 0x5c78. Register:
    the fee (0x7d0 through 0x25a4; question 164 / options 165), the battle (168 / 169: Duel,
    Bullying, Team, Survival; ext 0xdc 4 at 0x5f20), flags 758-769 (0x2f6 + member: who is in the
    party, read back by 305's 0x91c0; likely), the mode routines 0x4fec / 0x5224 / 0x54a8 / 0x5b0c, the
    fee paid (add_counter18), flag 586 (0x24a) = 1, map_transfer_c(305, 0, -13, 0, 276, 271, 128)
    at 0x6124 (door 19470409). At the hub state (chapter 60, no view) that transfer starts the
    arena's ranking battles in 305 and ends at 304 (confirmed headless; the menus' offsets and words
    checked on the NA disc).
  - **Map 305's Clerk** (talk 0x6e78): question 110 (ext 0x69 at 0x6e90), options 111 "Do
    intensive training. / Rest. / Go see Narl." (ext 0x68 at 0x6ea8, window 0, h 3), ext 0xdc 3 at
    0x6ec4, the store, ext 0x81 0 at 0x6ed0; a cancel (R -1) ends the talk (0x71b0 ext 0x84).
    Map 305 is the arena ring itself (no door region: the arena's own way out is the scripted
    0x92cc: music fade, map_transfer_c(304, 0, 1339, 0, -6, 180, 128)). Confirmed (headless).
  - **Fun City 281's Shigeo strip** (T0, routine 0x855c, trigger space x 82..347, z 181..241):
    walking from the 280 door's arrival (-3795, 0, -3406) up, right, up crosses it at view (215,
    214) on the way to the 297 door (T3); at chapter 60 its routine does nothing. Confirmed.
  - The game's option lines begin with glyphs 0xfe 0xfe 0xea (the cursor's room; maps 297 / 305).
    A message line wraps when it passes the window's width less 16 px (text_layout_line).

### Text

Strings are **byte streams** (correction: earlier notes read them as u16): a byte below 0x80 is
a one-byte code; otherwise code = `(b & 0x7f) | next << 7`. 0 ends the string. Codes below
0x4000 are 1-based glyph indexes into the map's own font (section 1 block 1: `u32 count; u32
height (12); u8 width[count]; u16 rows[count][12]`, bit 15 = leftmost pixel). Glyphs:
'0'-'9' 0xaa-0xb3, '-' 0xb4, '.' 0xb5, 'A'-'Z' 0xb6-0xcf, 'a'-'z' 0xd0-0xe9, '?' 0xed,
'!' 0xef, '=' 0xff, ':' 0x100, ';' 0x101, ',' 0x103, '/' 0x104, space 0x105; 0x55-0xa9 kana.

Control codes (field `msg_window_print`, `text_layout_line`, `text_render_line`):

| Code | Params | Effect |
|---|---|---|
| 0x4000 | - | new line |
| 0x4001 | - | wait for a button, then clear the window (new page) |
| 0x4002 | - | wait for a button (or 300 frames with auto text, script flag byte 0x5e bit 1) |
| 0x4003 | u8 n | text speed (n < 5) or pause n - 5 frames |
| 0x4004 | u8 n | text colour n * 2 + 3 |
| 0x4005 | - | centre the line |
| 0x4006 | u16 | timed wait (frames) |
| 0x4007 | u8 v | character name of message variable v |
| 0x4008 | u8 v | item / menu string (entry 0xfc2 id = variable v + 2999), drawn with the 0xfc2 font |
| 0x4009 | - | start of a ruby base (the glyphs a 0x400a block annotates) |
| 0x400a ... 0x400b | | ruby: 6x6 glyphs (#3110 atlas) centred over the 0x4009 glyphs; inside, half-width kana are one byte and every other code two bytes |
| 0x400c | u8 c | character name c (game_state + 0x28 + 0x14 * c, ASCII, '@' toggles kana) |
| 0x400d | u8 i | fixed space (width table 0x80076318) drawn as icon i (#3110 icons, table 0x80076330; 0-5 = the configured pad buttons) |
| 0x400e | u8 v | message variable v as a decimal number |
| 0x400f | u8 x | set the x position |

Message variables are 16 s32 at script globals + 0x38 (when the 0xfc2 strings are loaded,
0x400e uses the low half and 0x4008 the high half). Lines wrap at the window width (glyph width
+ 1 px each) at class changes (kana / digits / letters / codes >= 0x107), never before closing
punctuation; glyph 1 ('「') always starts a new line. A wrapped line drops a leading space.
Port: `port/src/game/field/message*.cpp`, verified against PCSX-Redux captures.

**French and German releases** (`docs/port/notes/regions_text.md`): the same encoding, control
codes and font block format, with a 132-glyph Latin map font (962 of 964 maps; per-map
symbols from 0x82, some maps 136 glyphs) whose numbering differs: digits 0x01-0x0a, '-' 0x0b,
'.' 0x0c, A-Z 0x0d-0x26, apostrophe 0x27, a-z 0x28-0x41, accented letters 0x42-0x61 (Latin-1
order), 0x62 German u-umlaut (empty in French), '?' 0x63, '!' 0x64, closing brackets 0x65-0x68,
opening brackets 0x69-0x6c, '=' ':' ';' ',' '/' 0x6d-0x71, space 0x72, letter-like apostrophe
0x73, symbols from 0x74. Word wrap: digits and letters (0x0d-0x61, German to 0x62, and 0x73)
are the classes; 0x63-0x6c and 0x0b-0x0c keep the class; a glyph followed by '?' / '!' is no
break opportunity; no glyph-1 or squeeze rules. The title (#14 block 1) and battle (#1980
block 1) fonts have 132 glyphs in ASCII order (code = ASCII - 0x1f), the accented letters from
0x63; the 0xfc2 side font 132 glyphs in the map font's numbering.

## Inventory entry

u16: bits 0–9 item id, bits 10–14 count.

## New Game data (entries #14-#16, read back to back)

- **#14** is a chain of four SLZ blocks:
  - block 0: string table, `{u16 id, u16 offset}` pairs ending in id 0xffff, then the strings as glyph codes (code = glyph index + 1; a byte above 0x7f is followed by a second byte: `(b0 & 0x7f) | b1 << 7`; 0 ends). Glyph *i* is ASCII *i* + 0x20 for the printable range. Ids: 0-18 sound, 32-45 characters, 46-57 vibration, 58-78 combat mode, 100-101 family names, 110-114 keyboard buttons, 150-170 battle rank, 200-207 memory card notices.
  - block 1: font, `s32 count` (96; French / German 132, accented letters from code 0x63), `s32 height` (12), `u8 width[count]`, padded to 4, then `u16 rows[height]` per glyph (1 bit per pixel, bit 15 = leftmost).
  - block 2: a TIM whose palette (at +0x14) is the text CLUT row 0x1f9 (palette *n* = CLUT x 16·*n*).
  - block 3: menu art (texture pack block) uploaded to (0x140, 0), CLUT row 0x1e0.
- **#15** block 0: window skin; `+0xc` → inner block; `inner + inner[0] + 4` = 256-colour CLUT (row 0x1fe), `inner + inner[1]` = `{u16 w, u16 h, pixels}` 4-bit, uploaded to (0x3c0, 0).
- **#16** blocks 0 / 1: Claude / Rena portraits, 16-bit, at (0x140 / 0x240, 0x100); widths 0xce / 0xe4.
  They are byte-identical to the renaming page's pictures of Claude and Rena (0xbd4 / 0xbd5,
  below); no other member has a hero-select portrait or a profile text (title ids 0x20-0x2d are the
  only profiles in NA, EU, FR, DE and JP; charselect research, 2026-10-25).
- **#2674 / #2675** (0xa72 / 0xa73): `u32 bank_offset`, an SLZ sequence at +4, the sound bank at `bank_offset` (the Sound Select / later screens' demo tunes; see "Sound banks" and "Sound sequences"). #2674's sequence block runs 3 bytes past its declared unpacked size (2633 vs 2630): the console's `slz_lzss` doesn't check.
- **#2680 / #2681**: first part of Claude's / Rena's prologue (loaded into a 0x52800 buffer during name entry).

## Member pictures and profile facts (every release)

Member numbers are party ids (1 Claude, 2 Rena, 3 Celine, 4 Bowman, 5 Dias, 6 Precis, 7 Ashton,
8 Leon, 9 Opera, 10 Ernest, 11 Noel, 12 Chisato).
- **0xbd3 + member** (0xbd4-0xbdf): the renaming page's picture, one SLZ block holding a texture
  block (`u16 flags` depth 2, `u16 clut_len` 0, `u16 vram_width`, `u16 height` 240, 15-bit
  pixels); widths 206, 228, 208, 208, 208, 216, 232, 192, 224, 208, 216, 216 in member order.
  (0xbd3 itself is the save icon.)
- **0x1170 + member** (0x1171-0x117c): the Status page's portrait, a 160 x 160 16-bit TIM; every
  0x800 sector starts with 0x10 bytes of header.
- **0x117d-0x1188**: Art's pictures (items 6-0x11), 320 x 1200 15-bit, raw.
- **Status page pack 0xbb3** (strings block 0; the pack's own subset font in block 1): group 7
  the labels Name / Race / Favorite Food; group 0xa the occupation, group 0xb the race (13
  messages: the 13th is Rena once flag 0x2dc is set), group 0xc the family name (" Kenni" ...),
  one message per member; the favourite food is the item at u16 table 0x80081612 + member * 2
  in the Status overlay 0xbb2.
- **0xbcf** (the renaming page's pack) holds the family names again in the title encoding.

## Party slot (0x60 bytes, handoff 3) and character record (0xd0 bytes, handoff 4)

Written by `party_slot_init` (title 0x80061f80). Slot: `+0` u16 character (1 Claude, 2 Rena, ...; negative: a member in reserve, out of the party with the data kept), `+2` u8 field status (1 down, 2 paralysis, 4 stone, 8 poison; `& 7` set = cannot act; item effects 0x41 / 0x42 / 0x62 cure poison / paralysis / stone), `+3` u8 (1 Claude, 9 Rena), `+0x10` u32, `+0x14/+0x18/+0x1c` u32 HP-like triple (Claude 130, Rena 100), `+0x20/+0x22/+0x24` u16 MP-like triple (20 / 40), `+0x28` u16 level 1, then u16 stats at `+0x2a..+0x54`. Record: `+0` u16, `+6` u16, `+0xc..+0x19` u16 equipment-like ids (Rena: 0x21b, 0x2a9), `+0x20` u32 talent bits (`roll_talents`), `+0x24` name (C string; overwritten with the entered name), skill bytes from `+0x3c`.

- **What a party member is** [bench agent, 2026-10-26; from the port's readers, all confirmed
  against PCSX-Redux by their own agents]: the slot and the record together, and nothing else.
  The game moves a member by copying exactly these 0x130 bytes (Party Order's
  `party_swap_members` 0x8007f0bc, `party_move_character_to_slot` 0x800531dc, PA isolate /
  restore, A 0x15). Checked headless: a Party Order swap of slots 0 and 7 left every member's
  0x130 bytes identical and changed nothing in the state block, config or inventory beyond a
  control run that opened and closed the same page (scratchpad/bench/member_cmp.py).
  - In the slot: `+0x10` EXP, `+0x14..+0x24` HP / MP (base, max, current), `+0x26` the battle's
    level copy, `+0x28` level, `+0x2a..` the attributes. In the record: `+0xc..+0x18` the seven
    equipment slots (`+0x16` / `+0x18` accessories), `+0x1a` SP, `+0x20` talents, `+0x24` the name
    (a copy), `+0x38` / `+0x39` Tactics orders (and the orders before "stand by"), `+0x3a` bits
    (0 "EW", 1 favourite food known, 2 Practice), `+0x3b` Link Combo partner (a character id),
    `+0x3c + i` moves / spells known (sign = on / off), `+0x5c + skill` skill levels (sign =
    battle skill on / off), `+0x8a + 2 * move` killer-move use counts, `+0xcc..+0xcf` the two
    killer-move sets. Specialties and super specialties are worked out from these (super
    specialties over the members active in all 8 slots).
  - Kept by character id elsewhere (they stay put when a member moves): config +0x58 / +0xe8 the
    relation tables, +0x42 / +0x43 the renamed heroes, +0x45 the hero; game_state +0x28 + (id - 1)
    * 0x14 the names (the renaming page writes these and record +0x24); script flags such as
    758-769 (in the party before a split) and 112 (Celine travels); the field sprite packs
    (`party_member_mask`, the ids active in the 8 slots).
  - Kept by place (they belong to the slot, not the member): config +0x41 the Formation leader /
    controlled member (a slot; the Party Order destructor re-points it only when its slot
    empties), config +0x40's start positions (`g_party_formations[formation][slot]`), the battle
    voice banks of slots 0-3 (rebuilt when a front slot's member changes), game_state +0x280..
    +0x287 (A 0x16's saved order, ids by slot; A 0x17 moves each id back by searching the 8
    slots), field_flags +0x6b / +0x6c.. (state block +0x1eb..; the PA's saved members, slot << 4 | id) and field_flags
    +0x38 + 4 * slot (`party_stash_slot_ids`, ids around a battle).

## STR movies (`movie_open`; disc 1 #4563-#4581 and #4586, disc 2 #4573-#4604)

Mode 2 Form 1 sectors, video only (no XA audio: the music comes from the sound system),
320x240, bitstream version 2, ten sectors ("chunks") per frame. Each sector: a 32-byte
header (`0x0160`, `0x8001`, chunk, chunk count, u32 frame number from 1, u32 bitstream
bytes, u16 width, u16 height, a copy of the bitstream header, 0) and 2016 bytes of the
frame's bitstream. Decoder: `port/src/disc/str.h` + `port/src/psx/mdec.h`.

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
| 5 | s16 frames (word) | `sound_fade_out_effects(frames)`; the destructor then skips its own fade |
| other | — | ignored (no arguments) |

| Entry | Path | Cues |
|---|---|---|
| #4504 (`0x1198`) | Claude, part 1 | clip at offset 32 at time 0; end at 1220 vblanks |
| #4505 (`0x1199`) | Claude, part 2 | movie #4567 at 0; clip (offset 788) at frame 8; music at 15; 46 vibration cues; end at frame 1150 (the movie's last) |
| #4506 (`0x119a`) | Claude, part 3 | clip at 30 vblanks; end at 250 |
| #4507 (`0x119b`) | Rena | movie #4568 at 0; music at 5; two clips (offsets 268 and 200668) at frame 410; 12 vibration cues; end at frame 906 |

The music and bank come from `g_prologue_data` (entry #2680 for Claude, #2681 for Rena:
a sound bank at the offset given by the first word, then an SLZ block of music).

## Sound banks (`sound_load_bank` 0x80018ff4)

The sound driver (System + 0x5aa4, `port/src/game/boot/sound.cpp`) keeps its own
format, not libsnd's VAB. Music entries (#2674-#2677, ...) are `u32 bank_offset`, an
SLZ block with the sequence at +4, and the bank at `bank_offset`. Bank #8 (the resident
effects) is read with entry #9 right behind it.

| Offset | Field |
|---|---|
| 0x0 | u32 size of the bank (header + samples) |
| 0x4 | s16 header size = offset of the sample data |
| 0x6 | u8 (0x14 in the banks seen; not read) |
| 0x7 | u8 slot: 0 = slot 6 (SPU RAM 0x1010, bank #8), 1 = slot 7 (0x12000), 2 and up = slot 7 at 0x34000 |
| 0x8 | u32 (not read) |
| 0xC | instrument table, ended by the word 0xffffffff |
| header size | ADPCM samples, DMAed as they are to the slot's SPU address |

Instrument (program): u16 program id, u16 ADSR1, u16 ADSR2, u8 (unused), u8 region
count, then 20-byte regions:

| Offset | Field |
|---|---|
| 0x0 / 0x1 | lowest / highest note |
| 0x2 / 0x3 | lowest / highest velocity |
| 0x4 | u32 sample address (relative to the slot; `sound_copy_instruments` adds the slot's SPU address) |
| 0x8 | u32 loop address (relocated too; voices loop by their ADPCM flags) |
| 0xC | volume |
| 0xD | s8 coarse tune (semitones) |
| 0xE | s8 fine tune (1/64 semitone) |
| 0xF / 0x10 / 0x11 | note, length, pan used instead of the sequence's (tracks with flag 0x40) |

A region whose first word is 0xfffffff8 is not re-keyed after a pause. Programs are
looked up by id through the 12 bank slots in the order of 0x8002b6c0 (8 9 10 11 4 5 6
7 0 1 2 3); slots 8-11 hold one-shot clip programs.

## Sound sequences (`sound_sequence_init` 0x80018560, `sound_track_step` 0x80015c94)

Music (unpacked from the entry's SLZ block):

| Offset | Field |
|---|---|
| 0x0 | u16 0xffff |
| 0x2 | u16 (size; not read) |
| 0x4 | u32 id (a restored sequence resumes only if the id matches) |
| 0x8 | u16 offset of the track table |
| 0xF | base tempo (beats a minute) |
| 0x13 / 0x14 / 0x15 | reverb mode (libspu `SPU_REV_MODE_*`, 5 = hall), delay, feedback |

Track table: 24 entries of 6 bytes: flags (0x80 notes not transposed (drums), 0x40 the
region sets note / length / pan, 0x02 pan follows the note, 0x01 never keyed on),
priority (a voice may be stolen for a lower value), s8 transpose, first wait - 1, u16
offset of the track's pattern list. A pattern list is u16 offsets (from the sequence
start) of patterns, ended by 0xffff; a track stops at the end of its list and the music
ends when every track has.

Time: a music tick is one interrupt of root counter 2 (system clock / 8 = 4,233,600
Hz) of period 125000000 / (tempo * 24), about 48.8 ticks a beat; below tempo 80 the
counter runs two or four times faster and only every second or fourth interrupt is a
music tick. Effect ticks come every 65104 counter ticks (65.03 Hz) regardless.

Commands. A byte below 0x80 is a note: note, wait (ticks to the next command), then by
the note format set with 0x9e: 4 (default) length, velocity; 3 length (fixed velocity);
1 velocity (fixed length); 2 nothing (both fixed). The other commands start with their
wait unless noted:

| Command | Arguments | Effect |
|---|---|---|
| 0x80 | - | end of pattern: the next one (effect tracks stop) |
| 0x81 | wait, priority | |
| 0x82 | wait, s8 | tempo change: tempo * (1 + n / 64) (n / 128 below 0) |
| 0x83 | wait, u16 | program |
| 0x84 | wait, s8 | pitch bend, times the bend range / 64 semitones |
| 0x85 / 0x86 | wait, value | volume / expression |
| 0x87 | wait, value | note pan (0x40 centre) |
| 0x88 | wait, depth | vibrato from the track LFO (phase advances by the vibrato rate) |
| 0x89 | wait, on | sustain pedal (ended notes are held until it is released) |
| 0x8a | wait, depth | reverb depth (value << 8, both sides) |
| 0x8d | - | loop start |
| 0x8e | count | loop end: back to the loop start count - 1 times (0 = forever) |
| 0x8f | wait | rest |
| 0x90 | wait, on | reverb for the track's voices |
| 0x91 | wait, mode | reverb phase: 1 left inverted, 2 right, 3 both |
| 0x92 | wait, mode | surround phase of the voices: 1 left inverted, 2 right, 3 both |
| 0x93 | wait, on | delayed vibrato |
| 0x94 | wait, delay, depth, rate | delayed vibrato parameters (0x30 / 0x14 / 4 by default) |
| 0x95 | wait, s8 | fine tune (1/64 semitone) |
| 0x96 | wait, range | bend range (12 by default) |
| 0x97 | wait, u16, u16 | ADSR1, ADSR2 |
| 0x99 | wait, on | random pitch (-3..3 / 64 semitone per note) |
| 0x9a | wait, on | echo: a copy of the track on a free track, with 0x9b's parameters |
| 0x9b | wait, delay, transpose, fine, volume, pan | echo parameters |
| 0x9e | length, velocity | note format (no wait): fixed length and / or velocity, 0 = given per note |

0x8b, 0x8c, 0x98, 0x9c, 0x9d and 0x9f-0xff are not commands: the console's interpreter
would spin on them (the port stops the track).

## Sound effects (entry #9, `sound_set_sequences` 0x80014508, `sound_play_sfx` 0x80019b1c)

| Offset | Field |
|---|---|
| 0x0 | u32 offset of the script table |
| 0x4 | u16 offsets (from +4) of the effect sequences (the command set above, run on effect tracks 24-31 at the effect tick) |
| script table | s16 offsets (from the table) of the effect scripts |

A script is a u32 header (priority << 24 | group << 16) and words `op << 24 | arg`: 0
wait for the effect's track to finish, then end; 1 delay `arg` effect ticks; 2 wait for
the track; 3 start effect sequence `arg` (volume and pan from the caller's
`volume << 8 | pan`, transpose from the caller). Eight effects play at once, one per
group; a new effect takes a free slot, else its group's slot unless that one has a
higher priority, else the slot with the highest priority value. Ids with a high byte
(1-11) use the effect tables of bank slot `id >> 24 - 1` instead; 0x6f / 0x70 are
streamed clips.

## Effect banks and clips (`sound_import_effects` 0x800192fc, `sound_open_clip` 0x800197ec)

A bank that carries its own effects, imported into one of the bank slots: the
prologue's voice clips (at offsets inside the cue-list entries #4504-#4507), the Voice
Collection's, and the field's effect banks.

| Offset | Field |
|---|---|
| 0x0 | u32 offset of the bank part |
| 0x4 | s16 per effect, ended by -0x8000: offset (from +4) of its record; 0 none; another negative value: the same effect as entry -n; -0x7fff skipped |
| record | u32 record size, u32 effect-script header (priority << 24 / group << 16), the effect's sequence (record size - 8 bytes) |
| bank part | a sound bank as above (u32 size, s16 header size, instrument table at +0xc, samples) |

Each record becomes, in the slot's tables, the script "header; start sequence n; end"
(n = its sequence's index) and sequence n. The instruments are relocated to the slot's
SPU address; for the stream slots 8-11 that is the SpuSt buffer of the voice the
import takes from the free-voice ring (0x70000 + voice * 0x100), and the samples stay in
RAM: `sound_open_clip` mode 2 streams them through SpuSt, refilling the 0x100-byte buffer
0x80 bytes at a time on the SPU interrupt. Clips with 0x80 sample bytes or fewer load
instead (mode 1, slots 4 / 5 at SPU 0x28000 / 0x2d800).

#4504's clip (Claude's prologue, part 1, offset 32): one effect (header 0x401405b3, a
sequence playing note 0x3c with program 0x900 looped five times), 0x40f90 sample bytes
(about 19.6 s at pitch 0x89e).

Neither disc has XA-ADPCM sectors (no sector's submode has the audio bit) or CD-DA
tracks (one data track each): all of the game's sound goes through the SPU.
