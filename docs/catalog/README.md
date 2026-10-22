# Reverse-engineering catalog

Everything identified in Star Ocean 2 goes here, so it never has to be worked out twice.
These files are the source of truth: the tools read names from them, and nothing else
should hold a name that isn't recorded here.

| File | Contents |
|---|---|
| `functions.csv` | Every named function: address, name, C-style signature, what it does |
| `globals.csv` | Global variables and the structures behind them |
| `script_opcodes.csv` | The event-script VM instruction set, with argument counts |
| `formats.md` | File and data formats (archive, compression, programs, maps, scripts, text, sound) |

The structure behind the names (classes, flows, layouts) is in `docs/notes/`.

## Columns

- **module**: which code the address belongs to. Programs share addresses, so the module
  is part of the key.
  - `boot`: `SCUS_944.21` / `.22`, resident at `0x80010000`
  - `title`: archive entry #1 at `0x8002f810`
  - `field`: archive entry #3100 at `0x8002f810`
  - `field_ovl_c1d` / `field_ovl_c1e`: the field's code overlays #3101 (field maps) and
    #3102 (world map), both at `0x8007e000`
  - `battle`: archive entry #2129 at `0x8002f810`
  - `battle_ovl1984_N` (block N of entry #1984) and `battle_ovl1981`: battle code overlays
  - `battle_enemy_<name>`: an encounter pack's enemy-code block (one EnemyUnit subclass),
    linked for slot A 0x80088110, B 0x80089f18, C 0x8008b1f0 or D 0x80088900; `<name>` is
    its monsters (`docs/notes/battle_enemies.md`). The #2129 image's own default modules in
    those slots stay in `battle`
  - `credits_3107`: archive entry #3107 at `0x8002f810`, the credits program (map 581's
    section 6, run by table A 0x2d; `docs/notes/credits.md`)
- **signature**: C-style, with struct names for pointers (`Loader *`, `ScriptCtx *`).
  Unknown parameters are `int argN`.
- **confidence**:
  - `confirmed`: read in full from the decompilation, or checked against live data or a
    reimplementation
  - `likely`: consistent with the code read and the call sites
  - `guess`: a working name from call sites only; verify before relying on it
- **evidence**: how it was established, so it can be rechecked.
- **port** (functions.csv): where the C++ port lives in `engine/`, empty until it's ported.
  The share of rows with a port is the porting progress.

Naming: `subsystem_verb_object` in snake_case (`loader_select_entry`, `inventory_add`).
Sony library functions keep their SDK names (`CdControl`). Names that were never read
(`FUN_8003xxxx` in Ghidra) stay out of the catalog until they are.

## How the tools use it

- `tools/psxexe2elf.py --symbols docs/catalog/functions.csv:boot,field` embeds the names for
  the listed modules in the ELF that REA and Ghidra analyse.
- `tools/ghidra_scripts/ApplyNames.java <csv> boot` names functions in the Ghidra project.
- `analysis/SCUS_944.21.psyq.sym` holds the 406 PsyQ library names Ghidra's signature
  matcher found; it is generated, so it isn't copied here.

## Adding an entry

Add the row when a function's purpose is established, even partially (use `guess`), and
raise the confidence when it's confirmed. Keep one row per address and module.
