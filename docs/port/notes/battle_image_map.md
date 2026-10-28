# What the #2129 image holds at 0x800715d0-0x8008c8ec (important for p6 / p7)

Finding (coordinator, verified): most of this region is NOT code that runs at these addresses. Only a small set
of functions is live; the rest is stale RAM content captured when the program image was built: byte-identical
copies of OTHER archive modules (party-character code, enemy code) that were sitting in the load buffer, linked for
the addresses those modules really run at.

## Live code (runs at these addresses)

Reachable from battle_run (init phase, before battle_main_loop overwrites 0x800715d0-0x80081728 with #1984.0):
0x800715d0 battle_program_create, 0x8007160c battle_run, 0x80072164, 0x80072190, 0x8007221c, 0x80072270,
0x80072f70, 0x800731f4, 0x8007344c, 0x80074168, 0x800747a0, 0x80074a74, 0x80074fd0, 0x800750b8, 0x80075214,
0x800752c0, 0x8007536c.

Live until battle state 1 (then #1984.1 overwrites 0x80081728-0x80085518): the 0x54-byte party-data loader task
created by battle_main_loop before its loop: ctor 0x80082bd8 (vtable 0x80084e9c, slot 1 = update 0x80082cdc,
two 0x10c00 buffers), and what 0x80082cdc calls: 0x800818dc, 0x800823d4, 0x80082444, 0x8008248c, 0x80082504,
0x80082554, 0x800825c4, 0x80082634, 0x80082714.

Possibly live (shared library code linked into the battle image; check callers before assuming): copies of field
code such as voice_* / party_slot_init / init_<character> / roll_talents near 0x800769d8-0x80078f38.

CORRECTION (supersedes the first version of this file): the image tail 0x80088054-0x8008c1f4 IS live. It is the
ENEMY-CODE SLOT area, resident for the whole battle: slot A at 0x80088110, slot B at 0x80089f18, slot C at
0x8008b1f0. Offset 0 of each slot is the module's EnemyUnit-subclass constructor, called by the monster factory
0x80075ad8 (init phase, called from the formation loader 0x8007344c). The #2129 image holds default enemy modules
in these slots (their vtables 0x80088694, 0x800887a4, 0x80089df0, 0x8008a1d4, 0x8008a508, 0x8008a538, 0x8008b0c8,
0x8008c0c4 ... and methods are called through vtables at run time). Per battle, enemy containers replace them
(#1942's code block @0x21bf8 is linked for 0x80088110 and @0x228bc for 0x80089f18; #1933 @0x98f4 for 0x80088110).
Catalogue the tail as live code (module `battle`), noting "default enemy module in slot A/B/C".
Also live: 0x80075ad8 (monster factory), 0x80074980, 0x80075824, 0x80076298 (reached from the formation loader).

SECOND CORRECTION (from p9, verified by call sites): 0x80085518-0x80088054 in the #2129 image is LIVE battle-phase
code from the start of the battle until #1981 overwrites it at state 4: resident 0x80062c24 calls 0x80085560
(spell-cast object API: two slots at 0x8006fe08, 0x248-byte objects, spell code unpacked to 0x8008cc00 /
0x8008e600), and party module #2153 / enemy containers #1914, #1936, #1944, #1945, #1961 call 0x80085560,
0x80085c10, 0x80085c84, 0x80085ccc, 0x80086478. Catalogue it as live (module `battle`, lifetime "until result").
Only 0x800851d0-0x80085510 (the #2139 copy) is stale there.

## Stale copies identified (image range -> archive block, run address)

| Image range | Copy of | Real run address |
|---|---|---|
| 0x8007b810-0x8007cadc | #1942 block @0x21bf8 (enemy code, 4812 bytes) | 0x80088110 (enemy slot A) |
| 0x8007cc10-0x8007d790 | #2147 blocks (0/1) stored at 0x80079810 | character / module code (variants) |
| 0x8007d810-0x8007dd50 | #1933 block @0x98f4 (enemy code, 1652 bytes) | 0x80088110 (enemy slot A) |
| 0x8007e010-0x80081710 | #2131 block 2 (party character code, 14672 bytes) stored at 0x8007d810 | 0x801f6000 (party slot 2) |
| 0x800851d0-0x80085510 | #2139 block 0 stored at 0x80081810 | 0x801ee000 (party slot 0) |

## Module loading this implies (verified from the archive)

- Party-character battle code: entries #2131, #2133, #2135, #2137, #2139, ... (odd entries after #2129) each hold
  4 SLZ blocks = the same code linked for the 4 party slots at 0x801ee000 + 0x4000 * slot (internal jal targets
  0x801ee05c / 0x801f205c / 0x801f605c / 0x801fa05c). The 0x10000 buffer pinned at 0x801ee000 (g_fixed_buf_801ee000)
  holds them. The even entries (#2130, #2132, ...) are the matching data blocks (models / animation).
- The "enemy containers" are the ENCOUNTER PACKS (archive entry 0x57d + encounter = #1405-#1968, load-list
  index 7; format in battle.md 6.4): sprite-set list, CLUT list, code list ({kind, offset}: kind 0 -> 0x80088110,
  1 -> 0x80089f18, 2 -> 0x8008b1f0, 3 -> 0x80088900), formation records and 0x5c-byte monster stat records.
- These modules call #1984.0 functions (an API): 0x80072594, 0x800725d4, 0x800744cc, 0x80074ca4, 0x80075080,
  0x80075944, 0x80077620, 0x8007d2d4, 0x8007deb4, 0x8007f3ac, 0x8007f3f0, 0x8007f6c8, 0x8007f818, 0x8007fd98,
  0x8007fdec.

## Fixed decompiles

Bogus function starts created at overlay call targets inside live init functions were merged back (index.csv and
.c files updated): load_stage 0x80072270 (3328 bytes), 0x8007344c (3356), battle_init_objects 0x80074168 (1592),
battle_init_state 0x80074a74 (1372), 0x80074fd0 (232), 0x800750b8 (348), 0x800818dc (2788), 0x80082cdc (4196).

## How to catalog the stale functions

Give each stale function a short row (it is still useful decompiled code of the module it belongs to): name it by
what it does if obvious, else `fn_XXXXXXXX`; confidence `guess` or `likely`; evidence starting with
`stale copy of #NNNN (runs at 0x...)`; summary starting with `[stale]`. Do not spend long on them.
Spend the time on the live functions and the formats they read.
