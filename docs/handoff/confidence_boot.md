# Handoff: catalog confidence pass, boot program (conf-boot agent, 2026-10-23)

Assignment (coordinator): raise / correct the `likely` and `guess` rows of the `boot` module in
`docs/catalog/functions.csv` (373 rows at the start: ~13 game code, ~360 PsyQ library / `_OBJ_`
internals). Two other agents did field-side (`docs/handoff/confidence_field.md`) and battle-side
(`confidence_battle.md`) modules at the same time; their rows were never touched.

## Done (all batches applied; scratchpad/conf_boot/u1.py..u7.py)

**371 of 373 rows raised to `confirmed`.** 2 left at `likely` (both were `likely` already; neither
was downgraded) with corrected evidence, listed below for a stronger agent.

- **13 game-code rows** (u1): 12 confirmed by reading the REA pseudocode of
  `analysis/elf/SO2_boot+main.elf` against the port C++ (`get_system`, `loader_shell_open`,
  `cd_check_new_disc`, `loader_defer_sectors`, `loader_sector_deferred`, `handoff_store/clear/get`,
  `clock_reset`, `sound_release_clip/start_clip/measure_subtick`). Corrections: 3 signatures fixed
  to the original's actual (void) parameter list (`loader_shell_open`, `loader_sector_deferred`,
  `clock_reset` — the catalog had copied the port wrapper's phantom `Loader*`/`Pad*` parameter);
  `handoff_store`'s return type `void` → `int` (it returns 0/1); the port column for
  `sound_release_clip`/`sound_start_clip` was wrong (pointed at a `title/movie.cpp` stub that is
  only a caller) — corrected to `engine/src/game/boot/sound.cpp`, where both are fully implemented
  and match the original field-for-field.
- **PsyQ / library rows** (u2-u7), settled in families, in order:
  - u2 (75): rows whose evidence was exactly `"PsyQ signature"` — verified each address+name
    against `analysis/SCUS_944.21.psyq.sym` (Ghidra's FunctionID byte-match of the real Sony
    object code). One claim (`CdRemove`) didn't actually match the `.sym` file — see below.
  - u3 (82): the SPU family (`SPU_OBJ_*`, `S_*_OBJ_*`, `_spu_*`). Settled by `read_bytes`:
    `PTR_DAT_8002b764` (the base every one of them indexes from) is the literal word
    `0x1f801c00`, the real PS1 SPU hardware base; offsets from it match the documented SPU
    register map exactly (reverb volume, voice key on/off, data-transfer FIFO, SPUCNT/SPUSTAT).
  - u4 (96): the kernel/libgpu/libetc-interrupt family (`SYS_OBJ_*`, `VSYNC_OBJ_*`, `INTR_OBJ_*`,
    `INTR_VB_OBJ_*`, `INTR_DMA_OBJ_*`, `ST_OBJ_*`). Settled the same way: verified pointers to
    `0x1f801070`/`74` (I_STAT/I_MASK), `0x1f801814` (GPU GP1), `0x1f8010f0`/`f4` (DMA DPCR/DICR).
  - u5 (43): `PRNT_OBJ_*`/`PUTCHAR_OBJ_*`/`PUTS_OBJ_28`/`MEMCHR_OBJ_20` — contiguous internal
    continuations of the byte-signature-confirmed `prnt`/`_putchar`/`putchar`/`puts`/`memchr`
    bodies, reached by each parent's own terminal `jal` (REA `procedure_info`/`procedure_callers`).
  - u6 (51): the CD-ROM driver family (`cd_*`/`cdrom_*`/`Cd*`, except `CdRemove`) — verified
    pointers to `0x1f801800`-`03` (the real CD-ROM controller ports) plus the SPU base for
    CD-audio mixing; the `PAD_OBJ_*` driver-install chain; 9 small library/glue rows
    (`loadimage_tpage_wrapper`, `mem_realloc_inplace`, `kernel_noop_callback`, `_OP_NEW_OBJ_30`,
    `memcard_init_and_patch`/`_start`/`_stop`, `gpu_video_mode_flag_get`/`_patch`,
    `COUNTER_OBJ_74`) read in full and matching their existing summaries exactly.
  - u7 (12): the last "guess" rows (`icache_flush_trampoline`, `cxx_set_terminate_pair`,
    `cxx_clear_terminate_pair`, `identity_callback`, `clear_boot_ot`, `mem_pool_init`,
    `mem_pool_gc`, `boot_vsync_hook_disable`, `struct2_copy_keep_tail`, `struct2_copy_set_tag`,
    `struct2_set_tag`, `kernel_noop_callback2`) — each matches its summary on a full read (8 via
    REA pseudocode, 4 via the row's existing full raw-disassembly evidence, since Ghidra makes no
    function boundary at those 4 addresses). These were `guess` only because no caller was found
    anywhere in the archive (naming confidence from usage), not because the behaviour was unclear.

## Rows left for a stronger agent (both `likely`, unchanged)

**Both settled (leads agent, 2026-10-23, docs/handoff/port_leads.md lead 5): confirmed.**
sound_add_ticks: the second word is 0x8c86338c = `lw a2, 0x338c(a0)` (opcode 0x23), not
0x0c86338c; the body is exactly the summary. CdRemove: `li t2, 0xa0; jr t2; li t1, 0x72`, the
BIOS A(72h) stub (the number sits in the delay slot the decompiler drops).

1. **`0x8001abc0 sound_add_ticks`** — Ghidra creates no function boundary here (`resolve_
   containing_procedure`: not_in_procedure; no xrefs in `SO2_boot+main.elf`, since its only
   callers are in an overlay program). Hand-disassembly of the raw bytes matches the summary/port
   for 9 of ~10 words (`lh +0x3390`, `addu` with `n`, `div`, `mflo`, `lhu +0x3392`, `nop`, `addu`,
   `jr ra`, `sh +0x3392` in the delay slot), but the 2nd word (`0x0c86338c`) decodes unambiguously
   (opcode field 3, J-type) as `jal 0x8218ce30`, outside all mapped RAM — almost certainly not a
   real `jal`, but hand-disassembly can't resolve what it actually is without a Ghidra-recognized
   function at this address. The port (`engine/src/game/boot/sound.cpp sound_add_ticks`) matches
   the summary's math exactly, so this is purely a reverse-engineering gap, not a port concern.
   Next step for whoever picks this up: force a Ghidra function definition at `0x8001abc0` (no
   MCP tool for that was available this pass), or find/build an overlay ELF that already analyses
   it as a function.
2. **`0x80024a98 CdRemove`** — REA pseudocode is a bare tail call through the fixed low-memory
   table-A slot at address `0xa0`: `(*(code *)&LAB_000000a0)();`, no visible index add, no
   arguments forwarded. There is no entry for this address in `analysis/SCUS_944.21.psyq.sym` at
   all (nearest entries are `GPU_cw` at `0x80024a80` and `DeliverEvent` at `0x80024ab0`, both
   distinct, real bodies) — the row's previous evidence claiming a `.sym` match was wrong and has
   been removed. Trivial 1-2 instruction trampolines of this shape are close to byte-identical
   regardless of which real BIOS stub they are, so the name can't be pinned down beyond doubt from
   this binary alone. Needs the real PS1 table-A layout (what index 0 really is) and this
   function's actual callers (none found in `SO2_boot+main.elf`) to settle.

## Port leads

None. The 13 game-code rows' ports already matched the original's behaviour; the only corrections
needed were catalog bookkeeping (signatures, a stale port pointer), not engine code. No behaviour
differences found, so nothing to flag in `docs/notes/original_bugs.md` either.

## How to verify

- `python3 -c "import csv; rows=list(csv.DictReader(open('docs/catalog/functions.csv'))); print(len([r for r in rows if r['module']=='boot' and r['confidence'] in ('likely','guess')]))"`
  → `2` (the two rows above).
- Each batch's update file is still on disk at `scratchpad/conf_boot/u1.py`..`u7.py` with the
  evidence text; `scratchpad/fx_worker/cat3/apply3.py UPDATES.py --dry` replays any of them
  harmlessly (already-applied rows just get their evidence/confidence reset to the same values).
- No engine code was changed (the port's existing files already matched what was found; no
  renames were needed).

## Rules followed (for the record)

Evidence-only confirmation bar: hardware registers / BIOS addresses read directly from the binary
(`read_bytes`), or chaining into an already-confirmed row via a real call/tail-call, or a
`SCUS_944.21.psyq.sym` FunctionID byte-match — never invented SDK names, never guessed through a
Ghidra boundary gap without saying so. Catalog writes only via
`flock scratchpad/catalog.lock python3 scratchpad/fx_worker/cat3/apply3.py scratchpad/conf_boot/uN.py`
(7 batches, each applied once, LF line endings preserved, row order preserved — the pre-existing
`order warning boot 0x80014020` is unrelated to this pass, present before and after). No game code
or assets were pasted into this file or into git; no audio was played; REA was re-opened against
`analysis/elf/SO2_boot+main.elf` and the subject checked before every batch of calls (one
mid-session catch: the shared REA server had switched to another agent's `field1d.elf` between
calls; re-opened and re-verified the affected reads before using them — the boot resident code is
byte-identical across both programs' ELFs, confirmed). No engine changes were made (none were
needed). One line per finished batch went into `scratchpad/checkpoint_notes.txt`, prefixed
`conf-boot: `.
