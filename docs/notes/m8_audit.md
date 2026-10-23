# M8 audit: can the game be finished?

Milestone M8 asks for nothing in the field's event scripts to be a stand-in that the
story needs, on either disc. This note records how that was checked and what was found.
It covers three things:
1. The script services and opcodes, read statically.
2. An event sweep that runs every map's events in the port, on both discs.
3. Key story stretches played through.

Tools:
- `tools/event_sweep.py` (the driver) and `engine/src/game/field/event_sweep.{h,cpp}`
  (`SS_EVENT_SWEEP`).
- The VM instruction trace `SS_VM_TRACE=1` (script_vm.cpp).
- `g_vm_standins` (vm_services.h).

## The two discs

All 827 field-map entries (#3207 + map) are byte-identical on disc 1 and disc 2, and so
are their scripts. The archives differ in about 60 entries only:
- #6 (disc 1) and #7 (disc 2);
- #4504-#4511 (disc 1 only) and #4512-#4522 (disc 2 only);
- #4563-#4604 (different or on one disc).

These are the disc marker and movie / streamed data. So an event behaves the same on both
discs unless it reads one of those entries.
- Disc 2 is reached through script B 0x6a (map 625 word 5608). It sets config +0x4c
  and asks for disc 2.
- The loader's disc check (map loads, a returning field) reads entry #(6 + config +0x4c).

The game always boots disc 1's image. `--disc2` / `SS_DISC2` registers disc 2, and the
loader swaps to it where the original prompts (`loader_insert_disc`).

## 1. Script services and opcodes

**Opcodes.** The 827 scripts use 288 distinct extended / table A / table B opcodes
(`scratchpad/menu/m8/opscan.py`). Each has a handler in script_ext.cpp,
script_table_a.cpp or script_table_b.cpp.
- The original's empty slots are empty in the port too: table A 0x00, 0x2f, 0x30 and
  above 0x48; table B 0x00, 0x08, 0x17, 0x52, 0x72 and 0x7e; ext codes below 0x64.
- The VM counts instructions that no handler claims (`g_vm_stats`). The unit test
  `script_vm_disc1_all_map_main_functions` and the sweep both report none on real paths.

**Services.** `vm_services.cpp` has 247 `vm_svc_*` functions.
- 160 call the real subsystem only.
- 87 call it too, but keep a stand-in for when the subsystem does not exist. That is
  the VM unit tests, which run scripts without a field. The stand-in logs "field VM:
  service not wired yet" and counts in `g_vm_standins`.
- The conditions, by count:

| Services | Real path taken when |
|---|---|
| 28 actor services | the actor system exists |
| 8 effect spawns | the SceneRenderer exists |
| 7 map / battle / program changes | the field program and load queue exist |
| 5 (A 0x33 / 0x35 / 0x37) | the map has sub-section 7 (#3101's gauge object); only map 688 calls them, and it has it |
| 3 (ext 0x9c / 0x9e) and 1 (ext 0xbf) | sub-section 8 / 9; maps 384, 598-602 / 139 call them and have them |
| 1 (A 0x40, the end) | sub-section 12; only map 581 calls it, and it has it |
| 3 movie services (B 0x16 / 0x70 / 0x50 / 0x6f) | the field, loader, sound and screen exist (section 5 maps) |
| the rest (message windows, fades, camera scripts, the notice, sound, voices, party, inventory, the stash, preloads) | their object exists, which it always does in a running field |

- A 0x2d (map 581: the credits) runs `field_run_section6_program` when the map has
  section 6, which map 581 does.
- The full list is in `scratchpad/menu/m8/svc_classes.txt`.
- In the sweep (below) no event on either disc reached a stand-in: `g_vm_standins`
  stayed at 0.

Other "not ported" notes in the field code:
- `todo()` in encounter.cpp and map.cpp is unused.
- field_update's and field_world_update's default cases are unreachable: every state
  0-0x21 is handled.
- special.cpp's "special 0 without #3104" can't happen: only map 143 uses special 0, and
  it carries #3104.
- The FieldPanel that nothing opens is not ported.
- The field model drawer skips two section types that no vehicle model has.

## 2. The event sweep

`SS_EVENT_SWEEP=1` with `--map N` (a new game started on map N) and `SS_BATTLE_STUB=1`
(battles end at once in victory).
1. The sweep waits for **free play**: field state 0, the player has control, no trigger
   or talk event, wait state 0, no fade, no busy window. The map's main thread may sit
   inside a function; some maps run a loop while the player walks, and events start then
   as well.
2. It then takes a snapshot of the state block (handoff 9), party, records, inventory and
   config, and lists the map's **entries**:
   - every active trigger region and its Private Action entry;
   - every actor's talk and pickpocket entry. Actors flagged 0x20000 are skipped, as
     field_try_talk does.
3. For each entry in turn:
   1. The snapshot goes back just before field_update's state 1 reloads the map. During
      the fade, the script thread is held with a long wait and no control, since its
      stacks live in the state block.
   2. The sweep waits for free play again.
   3. It checks the entry still exists. On later visits a map can set itself up
      differently: an NPC gone, another talk entry.
   4. It fires the entry as the VM does: push a frame, jump, depth 1, the trigger or
      talk state, control taken.
4. Input while an event runs:
   - Cross when a window waits for the button (states 6 / 7 / 8) or a choice is up.
   - Cross every 64 frames while the player has no control.
   - Circle every 8 frames inside a menu's own loop.
   - Never otherwise while the player has control. An earlier version pressed blindly,
     and a press as a talk ended started the same talk again.
5. Each entry ends as one of:
   - **ok**: back to free play on the map;
   - **map M**: the event took the party to map M;
   - **game over**;
   - **stuck** / **timeout** (below);
   - **gone**;
   - **no free play**: the reload did not settle.
6. The driver adds **crash** and **hang** (no output for 120 s) and re-runs the map from
   the next entry. `SS_EVENT_SWEEP_SKIP=k` starts at entry k.

`SS_EVENT_SWEEP_DISC=2` (driver `--disc 2`) plays as on disc 2: it sets config +0x4c and
swaps to disc 2's archive, as script B 0x6a does.

A stuck event is told from a long one by a progress digest: the map, field state,
script position, waits and every window's text position.
- **stuck**: the digest did not change for max(1200, frames / 3) frames.
- **timeout**: the event ran past 10 x frames, though it kept moving.
- A choice is answered with Cross, but every third choice in an entry with Circle, so
  that topic menus that come back after each answer are left in the end.
- Settling prefers the main thread at the top level (20 frames); a main thread inside a
  function needs 600 frames of free play.

### Results

Final runs (`scratchpad/menu/m8/end_d1` and `end_d2`, `--frames 12000`):

| | Disc 1 | Disc 2 |
|---|---|---|
| maps | 827 | 827 |
| maps that reached free play | 789 | 789 |
| entries run | 5326 | 5326 |
| ok | 3640 | 3640 |
| took the party to another map | 1678 | 1678 |
| gone on a later visit | 3 | 3 |
| stuck | 3 | 3 |
| hang (no output) | 2 | 2 |
| crash | 0 | 0 |
| stand-in services reached | 0 | 0 |
| instructions no handler claims | 0 | 0 |

The two discs give the same results. Their scripts are the same, and these runs read no
disc-specific entry.

The entries that did not end in free play or a map change:
- **Hang: maps 0 and 826, K21.** The debug room's ending launcher sets the ending flags
  and goes to map 581. That runs the credits and the final screen, which never returns,
  so it is not a hang.
- **Stuck: maps 591, 592 and 593, T9.** The trigger (region x 246..263, z 170..182) asks
  a question and then has the leader walk. The sweep fires it from the map's entry spot,
  not from inside the region, so the walk never arrives and the script waits for the
  leader (wait state 5) or loops. This is a sweep limitation.
- **Gone: map 58 K19 / S19 and map 139 K40.** After the first visit, the main script
  sets up these NPCs differently, so the entries no longer exist.

38 maps never reached free play:
- 35 run a scene that leaves the map (for example 5 and 6 to 12, 7 to 8, 172-182 to
  175, 690-696 to 695). Those scenes ran, and the next map is swept on its own.
- Map 185 is a scene chain over maps 207 and 183, and map 576 leads into disc 2's
  opening on 618 (about 17000 frames). Both were still moving at 12000 frames.
- Map 295: its main thread waits for actor 27 while the player has control. Most likely
  the NPC's walk is blocked by the leader standing at the `--map` start spot. Not
  checked against the original.

What the sweep found and what was fixed:
- **Music:** 61 "field music: the sequence block does not unpack" lines on about 50 maps.
  Fixed (SLZ).
- **V1:** the notice banner crashed when its window was already gone.
- **V2:** the end of a talk whose NPC deletes itself crashed (map 753 NPC 18). A real
  player reaches this.
- Everything else that once timed out or crashed came from the sweep itself:
  - the snapshot restored under a running thread;
  - stale entries fired;
  - blind input started talks again and looped topic menus;
  - triggers fired into intro scenes that had not locked the player yet.

## 3. Story stretches

There is no save near either point, so the scripts' own story globals were set for a
`--map` start (`SS_SCRIPT_GLOBALS`), and the sweep's settle input played the scenes
(`scratchpad/menu/m8/one.sh`).

- **Disc 1 to disc 2.** Map 625's main function plays the end of disc 1 when
  G[0xa0] = 0x22 and G[0xa4] = 0: `--map 625` with `SS_SCRIPT_GLOBALS="a0=0x22,a4=0"`.
  - The whole scene ran, with the save choice and the save menu (B 0x69). Then B 0x6a
    (config +0x4c = 1, disc 2 asked for), and the loader swapped to disc 2 without a
    prompt ("[loader] disc 2 inserted").
  - map_transfer_c(618) carried on into disc 2's opening on maps 618, 619 and 248, and
    the player had control on 248 after about 17000 frames.
- **The endings.** Map 583's main function runs the last scene when G[0xa0] = 0x3b and
  G[0xa4] = 0x7d0. It hands over to map 581, the ending map (sections 6 and 12).
  - Map 581 with script flags 0x2ee and 0x2f1 set (G[0x79c] = 0x24000) reached A 0x2d.
    That runs the credits program (#3107, disc 2's movies #4594-#4604: the loader swapped
    to disc 2 and back). Back in the field, map 581 ran A 0x40, the final screen
    ("Their adventure ..."), which the game stays on, as the console does.
  - From map 583 with the story globals, the last scene played and moved through maps
    581, 587 and 686.
  - The private endings (the map list at G[0x348 + 4 * G[0x344]]) need the relationship
    values of a real playthrough, which a `--map` start does not have. They were not
    checked one by one.

## Fixed

- **SLZ references before the output** (port decoder fault).
  - Five music banks end with an LZSS reference to before the start of the output:
    - #2646, #2678: within the declared size;
    - #2661, #2662, #2733: once the stream, which lacks its end marker, has run past the
      declared size into the bank that follows.
  - The console copies whatever memory lies before its buffer. The port rejected the
    block, so these songs had no sequence ("field music: the sequence block does not
    unpack", 61 lines in the first sweep).
  - `disc/slz.cpp` now reads those bytes as zeros.
  - **Checked against the console (PCSX-Redux):** `scratchpad/menu/m8/slz/slzref.lua`
    breaks in `slz_lzss` when the field unpacks its music into g_music_sequence
    (0x80075734, a `mem_alloc(0x4800)` made at field start).
    - It dumps the 8 KB before that buffer, writes the bank's stream over the source, and
      dumps the result. Three runs from two different saves.
    - **#2646:** declared bytes 481-485 come from 2591-2587 bytes before the buffer. On the
      console those are zeros, so all 486 declared bytes match the port.
    - **#2678:** declared byte 1225 (the last) comes from 407 bytes before the buffer. That
      is check byte 845 of the inventory stash, allocated just below. Its value is
      `((~845 & 15) ^ salt) << 4 | salt`, with `salt = rng_range(16)` drawn when the stash
      is made: the console read 0x02 (salt 2) in both runs, and it holds the real
      inventory's check bytes while the stash is in use.
    - So the byte varies with the random generator and the game state, and the port keeps
      0 there. Every other declared byte matches.
    - **#2661, #2662, #2733:** every declared byte matches; only bytes past the declared
      size differ.
  - **Songs and maps affected** (music number -> entry; maps whose scripts play it):

    | Music | Entry | Maps |
    |---|---|---|
    | 6 | #2646 | 108, 247, 251 |
    | 21 | #2661 | 14, 27, 30, 39, 247, 253, 255, 310, 386-401, 642 |
    | 22 | #2662 | 43-60, 63, 183-209, 277, 278 |
    | 38 | #2678 | every map: a shared script routine plays it with a 400-frame window (on map 26 at word 5853) |
    | 95 | #2733 | 548 |

- **V1, notice banner** (original_bugs.md): the banner's slide-out was queued through a
  null window pointer when its window was already gone. The console writes into low RAM
  there; the port crashed. Fixed in notice.cpp.
- **V2, talk end after the NPC deleted itself** (original_bugs.md):
  - `actor_pop_state` runs the actor's update first. When that update executes a queued
    delete (map 753: a talk ending with ext 0x71 on its NPC), the console goes on using
    the freed actor.
  - In the port this crashed the game at the end of that talk. A real player reaches it.
  - Fixed in actor_base.cpp: the pop stops once the actor is gone.

