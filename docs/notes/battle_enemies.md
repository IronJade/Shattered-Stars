# Battle: the monster classes (families and enemy-code modules)

Notes from the enemy worker. Names are in `docs/catalog` (module `battle` for the resident
families and the #2129 image's default modules, `battle_enemy_<name>` for the encounter
packs' blocks); the port is `engine/src/game/battle/enemy_*.cpp`. The EnemyUnit base class,
its AI and the factory's data side are in `docs/notes/battle.md` §5-7 and
`analysis/battle_catalog/parts/p2.md`.

## Where a monster's class comes from

The monster factory (0x80075ad8, `enemy_create_by_type`) switches on the type id:

- **Base** (worms 4-6, saucers 0x3e-0x40): EnemyUnit (vtable 0x8006b908).
- **16 resident families** (knight, guard, plant, kobold, thief, magius, axe, sandglass, ooze,
  beastmaster, wolf, giant, goat, archer, shout, wizard): code 0x8003e248-0x80041600 and
  vtables 0x8006ba30-0x8006cb90 in the #2129 image. Five have constructors (they change the
  hit sounds; the goat's sets the hover amplitude); the others are a plain EnemyUnit with the
  family vtable stored over it.
- **Module classes**: the factory either calls the constructor at a slot base (A 0x80088110,
  B 0x80089f18, C 0x8008b1f0, D 0x80088900) or stores a vtable address inside a slot. The slot
  holds whatever the encounter pack's code list unpacked there (load list 7), or the #2129
  image's own default when the pack names no block for it.

### The blocks on the discs

All 564 encounter packs (#1405-#1968) were scanned (`scratchpad/enemy/scan_packs.py`). Disc 2's
packs are byte-identical to disc 1's. There are **40 distinct code blocks**, and each block
holds **exactly one class**: every (slot, factory entry) pair the types use maps to a single
block, and every block has a single vtable. A vtable-type block has no constructor at its
base; offset 0 is an ordinary method (Snowman: slot 26).

| Slot | Block (name = its monsters) |
|---|---|
| A (27) | cyril, carlaeagle, gelatin, arcmene, hounddog, salamander, kitty, ricki, ogre, ericodus, xine, succubus, dias, ghost, zand, gargoyle, bark, synard, celesta, berle, magichand, lover, marsilio, shigeo, ruprecht, vesper, snowman |
| B (9) | bunny, stingray, gerel, controller, niquia, azamgil, visseyer, guardbox, decus |
| C (3) | slime, miel, shin |
| D (1) | indalecio (13 KB, the largest; overlaps A, B and C) |

The #2129 image's defaults are three more classes: **slot A = Guardian / Geo-guardian /
Owlbear** (0xbe-0xc0; encounters 457 458 501 512 525 527 558 load no code), **slot B =
Nicolus** (0xc9) and **slot C = Jibril** (0xca, 0x294 bytes), both used only by encounter 536,
which loads slot A (Ruprecht) and keeps B and C.

Hashes, sizes, factory entries, object sizes, types and encounters per block:
`scratchpad/enemy/type_map.txt` and the registry in `enemy_modules.cpp`.

## The port

- `enemy_class_create(type)` (enemy_modules.cpp) is the factory's class choice. Families by
  type; module types by (slot, entry): the class of the block the slot holds, if that block
  provides the entry. A slot's block is identified by FNV-1a 64 of its unpacked bytes and its
  size, so a modified or unknown block falls back to the EnemyUnit base (logged once).
- `enemy_modules_load(kind, block)` records each code-list block; `enemy_module_u8/s16/s32`
  read a block's own tables at their link addresses (only data comes from the disc).
- One file per block: `enemy_mod_<name>.cpp`, an EnemyUnit subclass in an anonymous
  namespace with an exported `enemy_mod_<name>_create()`. Extra virtual slots (37+) are plain
  members. Shot callbacks are Shot kind 0x33 handlers (`shot_spawn_callback`).
- `enemy_support.{h,cpp}`: resident helpers only the monsters need so far, and logging
  stand-ins for the spell / SpellCast API, voices, the load queue and Shot kinds that are not
  ported yet (the battle agent's).

## Verification

Redux references: New Game, the field state poked to a forced battle (`*0x80075710 + 0x264 =
encounter`, stage 3), Claude poked sturdy (HP 1300, ATK 80) and left standing so the monsters
attack, RNG draws and every enemy's type / state / removal / death / position / HP / scratch
words logged every 10 vsyncs (`scratchpad/enemy/redux/explore.lua`). The port runs the same
battle from the reference's field_exit dump (`SS_BATTLE_RAM`, `SS_BATTLE_STRONG`,
`SS_ENEMY_TRACE`), and `scratchpad/enemy/cmp.py` compares (the action kind +0x12a is ignored,
it is uninitialised heap on the console).

The original's phase relative to battle_main_loop is not fixed, so each reference is compared
at its own offset (cmp.py tries 0..8 vsyncs and keeps the one with the fewest RNG
mismatches). How long the battle setup took decides the first frames' tick pattern: in
encounter 10 the first frame comes at vsync 5 and the frames tick 2, 2, 2, ...; in encounter
285 it comes at vsync 6 and a 1-tick frame follows. The camera intro (a 190-tick motion
integrated with g_frame_ticks) then ends its first step at vsync 159 or 155, and the party's
appear, battle state 2 and the AI batches follow it. The port does not model the setup time
(its intro step ends at vblank 154 every time), so references fall into two groups: the port
matches at an offset of 5-6 vsyncs or of 0-2.

1500 vsyncs each (or to the battle's end); "exact" = every RNG sample and every enemy sample
(type, state, removal, death, position, HP, scratch words) equal:

| Encounter | Monsters (class) | Result |
|---|---|---|
| 315 | Bugbear (kobold) | exact; Claude KO on the same frame |
| 47 | 2 Sandglass | exact |
| 412 | 2 Dreamshade (sandglass: time stop, heal) | exact to the battle's end |
| 205 | Shielder (guard) | exact |
| 320 | Shout (MP drain particles) | exact |
| 92 | 2 Ooze | exact (they never move: attack odds 0) |
| 193 / 19 / 202 / 21 / 268 / 352 | Bandit (thief), 2 Landworm (base), Troll (giant), 2 Alraune (plant), Lizardaxe (axe), F-thiefL99 | exact |
| 280 | Darkcrusader (knight) | exact until its sword waves (Shot kind 2, not ported) hit Claude |
| 277 | Goathead (goat) | exact until its first spell (enemy_try_cast, not ported) |
| 420 | 4 Robinfake (archer) | exact to vsync 1110; from vsync 1078 the original slows to 3-vsync frames (the robins' volleys), which the port does not model, and a later choice of which robin attacks differs |
| 457 | Owlbear (image slot A: guardian) | exact |
| 80 | Cockatrice (carlaeagle) | exact |
| 269 | Vopalbunny (bunny) | exact |
| 314 | Slime | exact |
| 43 | Gerel | exact |
| 99 | Salamander | exact |
| 50 | Scewer (arcmene) | exact |
| 326 | Hellhound (hounddog) | exact |
| 34 | 2 Stingray | exact |
| 285 | Miel64 (miel) | exact |
| 286 | Kidnier (visseyer) | exact |
| 107 | Kitty | exact |
| 282 | Rikiha (ricki) | exact |
| 191 | Alen-Tax (ogre) | exact |

A sweep of every disc-1 encounter (0-563, 3000 frames, Claude idle) runs without a crash.
What the monsters still miss is the battle agent's: spells and SpellCast (casters stand in
their cast animation), Shot kinds 2, 0x27, 0x2c and 0x2d, shot_draw_frame_quad.

## Findings

- Monsters with behaviour odds 0 for "attack" (Ooze [0, 0, 250, 250, 0], Alraune [0, 0, 250, 0,
  250]) never start their special attack on their own: the ooze spits only after a member
  comes within its aggro radius (512 units); the Alraune only counters with attack 1 after a
  hit, so its spores (attack 2, reached only through slot 26) never happen in those
  encounters.
- `enemy_thief_attack1` animates twice a frame (enemy_attack_step and then slot 22 again).
- `enemy_sandglass_attack3` (time stop) re-freezes the party every frame past frame 9.
- Nothing clears the knight's stance (+0x178) after the waves except an attack start.
- Guardian (image slot A): phase 1 sets weapon element bit 4 before each melee test and only
  restores it when a hit lands, so the bit stays once phase 1 starts.
- The thunks 0x800553f0 (shot_place_at_unit) and 0x80030050 (sprite_place_near) pass dz = 0.
- Some blocks hold code that resident tables point at: Shot kinds 0x26 (stingray, 0x8008a30c)
  and 0x29 (magichand, 0x80088cb0) in g_shot_kind_table 0x8006fea8, SpellCast kinds 0x36
  (bark, 0x800885d8) and 0x3b (indalecio, 0x80089c80, the transformation). Nothing inside the
  block calls them, so a disassembler that follows calls misses them. The port spawns the Shot
  handlers as callback Shots (kind 0x33); the SpellCast handlers wait for SpellCast.
- Three classes add enemies mid-battle (slime and gelatin split, gerel copies itself): a
  full copy of the object and its stats record, registered in a free g_enemy_units slot as a
  task. The port hands the copy to the encounter (`enemy_adopt`).
- Original quirks kept (each commented in its file): arcmene's blink test compares an index
  with 0x3e8000 (always true); ericodus's attack 5 can reach a step no branch handles (frozen in
  state 13); visseyer's and jibril's release search writes even tries to the member's z
  velocity instead of its z; bark's vortex ring heights shift a 20.12 value again (right only
  at height 0); marsilio's afterimage ends on a hit, not at the end of the swing; shigeo's
  choose_target chance 0x291c0 always passes; celesta's status_test returns bit 24 of the mask
  argument; indalecio's step-6 halo afterimage is never made.
