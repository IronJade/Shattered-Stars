# The endings

How the game picks the private endings it shows after the final battle, the 85 ending ids
on disc 2, how the port was checked against them, and where the Prima guide disagrees with
the disc.

Sources:
- map 581's event script (the selection), read with `scratchpad/menu/m8/dis.py 581`;
- the ending maps' scripts (the scenes);
- runs of the port and of PCSX-Redux (below).

The guide is the user's copy of the Prima guide (`docs/Star Ocean The Second Story_text.pdf`,
git-ignored). Its pages are cited by the printed page number; the PDF page is one higher.
Where the guide and the disc disagree, the disc is what the game does.

## How the game gets there

1. After the final battle, map 685's script clears script flags 0x2ee, 0x2ef and 0x2f0,
   sets flag 0x2f1, plays the last scenes and moves to map 581 (words 8432-8438 and
   8588-8594).
2. Flag 0x2f1 has two jobs:
   - map 581's main function (word 11011) runs the ending flow only while it is set;
   - it is the message system's auto-text flag (script flag byte 0x5e bit 1). A `{wait}` in a
     message then becomes a 300-frame timed wait instead of waiting for the button, so the
     endings play without input.
3. Map 581's flow (word 12811):
   1. with 0x2ee clear: the selection (word 12187), then a map transfer to the first scene;
   2. each scene map plays its scene and transfers to the next map on the list;
   3. back on 581 with the list used up: 0x2ee is set and the credits run (table A 0x2d,
      the section 6 program, #3107), then 0x2f0 and the final screen (A 0x40), where the
      game stays.
4. The list sits in the script globals: ending ids at G[0x388 + k] (bytes), their maps at
   G[0x348 + 4k], ended by id 0xff with map 581; G[0x344] is the index of the scene playing.
   A scene map reads its id as G[0x388 + G[0x344]] (on map 625, word 4615), plays that
   scene, then increments G[0x344] and transfers to G[0x348 + 4 * G[0x344]] (word 4599).

## The relationship values

Two 12 x 12 byte tables live in the config block, so they are saved with the game.
Characters are numbered 0 Claude, 1 Rena, 2 Celine, 3 Bowman, 4 Dias, 5 Precis, 6 Ashton,
7 Leon, 8 Opera, 9 Ernest, 10 Noel, 11 Chisato. Cell (a, b) is at table + 12a + b; the port
names a the holder and b the target (`relation_add`).

| Table | Script ops | Used by the selection for |
|---|---|---|
| config +0x58 | B 0x10 adds, B 0x11 reads | pairs of opposite sex (what the guide calls romantic values) |
| config +0xe8 | B 0x12 adds, B 0x13 reads | pairs of the same sex (what the guide calls friendship values), and every variant choice |

B 0x10 and B 0x12 clamp each value to 0..15.

## The selection (map 581, word 12187)

1. **Sex.** The script fills a table: female are Rena, Celine, Precis, Opera and Chisato.
2. **Candidate pairs.** For every pair a < b with both in the party (ext 0xb1):
   - same sex: the two +0xe8 values (a, b) and (b, a); opposite sex: the two +0x58 values;
   - both at least 10: the pair is a candidate, scored by their sum.
3. **Special pairs**, added after the others with a fixed score (no value is changed):

   | Pair | Score | Condition |
   |---|---|---|
   | Celine + Chris (partner 12) | 26 | Celine in the party and script flag 0x1ab (set on map 78) |
   | Ashton + Eleanor (partner 13) | 20 | Ashton in the party and script flag 0x163 (set on map 156) |
   | Opera without Ernest (partner 14) | 24 | Opera in the party and Ernest not |

4. **Sort.** An exchange sort, highest score first: for each position i and each later
   position j, the two swap when j's score is higher. This is not a stable sort, so pairs
   with equal scores can come out in a different order from the one they went in.
5. **Couples.** Down the sorted list, a pair becomes a couple when neither member is in a
   couple yet and the lookup (word 11068) has an ending for the pair. Four pairs have none
   and are passed over: Bowman + Precis, Dias + Leon, Ashton + Opera, Ashton + Ernest.
6. **The list**, in this order:
   1. the solo ending of each party member not in a couple, from Chisato down to Celine;
   2. the couple ending of each couple, taken at its higher-numbered member from Chisato
      down to Celine, except the couples with Claude or Rena;
   3. the non-hero's ending (Rena's when Claude is the hero): their couple's ending, or their
      solo ending (left out if their partner is the hero);
   4. the hero's ending: their couple's ending, or their solo ending.

   Claude's and Rena's endings come last and are always there; steps 3 and 4 do not check the
   party.
7. **Variants.** Seven pairs have two or three endings. The lookup compares the pair's two
   +0xe8 values, (a, b) against (b, a), with a the lower-numbered character:

   | Pair | (a, b) > (b, a) | equal | (a, b) < (b, a) |
   |---|---|---|---|
   | Claude + Rena | 1 (map 18, Arlia) | 3 (map 625) | 2 (map 628, Earth) |
   | Claude + Precis | 8 (map 625) | 8 | 9 (map 197, Linga) |
   | Claude + Opera | 6 (map 628, Earth) | 6 | 7 (map 643, Tetragenes) |
   | Claude + Chisato | 16 (map 625: the war correspondent scene) | 15 | 15 (map 625: the survey flight) |
   | Rena + Dias | 25 (map 10) | 25 | 26 (map 18, Arlia) |
   | Celine + Ernest | 35 (map 8) | 35 | 36 (map 139, Celine's room) |
   | Opera + Ernest | 53 (map 617) | 53 | 54 (map 643, Tetragenes) |

The whole selection is modelled in `scratchpad/menu/m8/end/model.py` (the lookup runs on its
own disassembly, `endsim.py`); `python3 model.py CARD FRAME` predicts a save's endings.

## The 85 ending ids

The lookup knows ids 1 to 85, each with one map. "Party sprites drawn" is what the port drew
in each scene: the sprite keys 0-11 are the party characters, and every scene drew exactly
its pair, or its one character (119 scenes, no exception). "Other sprites" counts the NPC
and object sprite keys drawn as well. Areas are from `docs/ap/field.json`, except maps 628
and 643, named from their scenes' text.

| Id | Ending | Variant (+0xe8 values) | Map | Area | Party sprites drawn | Other sprites |
|---|---|---|---|---|---|---|
| 1 | Claude + Rena | Claude > Rena | 18 | Arlia Village | Claude, Rena | 4 |
| 2 | Claude + Rena | Claude < Rena | 628 | Earth (scene text) | Claude, Rena | - |
| 3 | Claude + Rena | equal | 625 | - | Claude, Rena | 4 |
| 4 | Claude + Celine |  | 372 | Mountain Palace | Claude, Celine | 1 |
| 5 | Claude + Ashton |  | 349 | Mountain Palace | Claude, Ashton | - |
| 6 | Claude + Opera | Claude >= Opera | 628 | Earth (scene text) | Claude, Opera | - |
| 7 | Claude + Opera | Claude < Opera | 643 | Tetragenes (scene text) | Claude, Opera | - |
| 8 | Claude + Precis | Claude >= Precis | 625 | - | Claude, Precis | 4 |
| 9 | Claude + Precis | Claude < Precis | 197 | Town of Linga | Claude, Precis | 1 |
| 10 | Claude + Bowman |  | 229 | El Beach | Claude, Bowman | 1 |
| 11 | Claude + Leon |  | 628 | Earth (scene text) | Claude, Leon | - |
| 12 | Claude + Ernest |  | 625 | - | Claude, Ernest | 4 |
| 13 | Claude + Dias |  | 407 | Sanctuary of Linga | Claude, Dias | 2 |
| 14 | Claude + Noel |  | 12 | Shingo Forest | Claude, Noel | 5 |
| 15 | Claude + Chisato | Claude <= Chisato | 625 | - | Claude, Chisato | 4 |
| 16 | Claude + Chisato | Claude > Chisato | 625 | - | Claude, Chisato | 4 |
| 17 | Claude alone |  | 697 | - | Claude | - |
| 18 | Rena + Celine |  | 141 | Mars Village | Rena, Celine | - |
| 19 | Rena + Ashton |  | 33 | Arlia Village | Rena, Ashton | 3 |
| 20 | Rena + Opera |  | 643 | Tetragenes (scene text) | Rena, Opera | - |
| 21 | Rena + Precis |  | 75 | Town of Cross | Rena, Precis | 4 |
| 22 | Rena + Bowman |  | 189 | Town of Linga | Rena, Bowman | 4 |
| 23 | Rena + Leon |  | 18 | Arlia Village | Rena, Leon | 7 |
| 24 | Rena + Ernest |  | 18 | Arlia Village | Rena, Ernest | 3 |
| 25 | Rena + Dias | Rena >= Dias | 10 | Shingo Forest | Rena, Dias | - |
| 26 | Rena + Dias | Rena < Dias | 18 | Arlia Village | Rena, Dias | 5 |
| 27 | Rena + Noel |  | 12 | Shingo Forest | Rena, Noel | 4 |
| 28 | Rena + Chisato |  | 38 | Rena's Room | Rena, Chisato | - |
| 29 | Rena alone |  | 18 | Arlia Village | Rena | 8 |
| 30 | Celine + Ashton |  | 118 | Port of Clik | Celine, Ashton | 1 |
| 31 | Celine + Opera |  | 141 | Mars Village | Celine, Opera | - |
| 32 | Celine + Precis |  | 225 | Town of Lacour | Celine, Precis | 4 |
| 33 | Celine + Bowman |  | 232 | Eluria Colony | Celine, Bowman | 7 |
| 34 | Celine + Leon |  | 437 | Laboratory | Celine, Leon | - |
| 35 | Celine + Ernest | Celine >= Ernest | 8 | Arlia Village | Celine, Ernest | 1 |
| 36 | Celine + Ernest | Celine < Ernest | 139 | Mars Village | Celine, Ernest | - |
| 37 | Celine + Dias |  | 460 | Mars Village | Celine, Dias | - |
| 38 | Celine + Noel |  | 463 | Secret Charity/Fake Gallery | Celine, Noel | 4 |
| 39 | Celine + Chisato |  | 372 | Mountain Palace | Celine, Chisato | - |
| 40 | Celine + Chris (12) |  | 90 | Cross Castle, First Floor | Celine | 4 |
| 41 | Celine alone |  | 372 | Mountain Palace | Celine | - |
| 42 | Precis + Ashton |  | 196 | Town of Linga | Precis, Ashton | - |
| 43 | Bowman + Ashton |  | 229 | El Beach | Bowman, Ashton | 1 |
| 44 | Ashton + Leon |  | 349 | Mountain Palace | Ashton, Leon | - |
| 45 | Dias + Ashton |  | 407 | Sanctuary of Linga | Dias, Ashton | 2 |
| 46 | Ashton + Noel |  | 463 | Secret Charity/Fake Gallery | Ashton, Noel | 4 |
| 47 | Ashton + Chisato |  | 349 | Mountain Palace | Ashton, Chisato | - |
| 48 | Ashton + Eleanor (13) |  | 157 | Town of Herlie | Ashton | 2 |
| 49 | Ashton alone |  | 381 | Lasguss Mountain | Ashton | - |
| 50 | Precis + Opera |  | 643 | Tetragenes (scene text) | Precis, Opera | - |
| 51 | Bowman + Opera |  | 232 | Eluria Colony | Bowman, Opera | 7 |
| 52 | Leon + Opera |  | 643 | Tetragenes (scene text) | Leon, Opera | - |
| 53 | Opera + Ernest | Opera >= Ernest | 617 | L'Aqua | Opera, Ernest | - |
| 54 | Opera + Ernest | Opera < Ernest | 643 | Tetragenes (scene text) | Opera, Ernest | - |
| 55 | Dias + Opera |  | 460 | Mars Village | Dias, Opera | 1 |
| 56 | Opera + Noel |  | 643 | Tetragenes (scene text) | Opera, Noel | 1 |
| 57 | Opera + Chisato |  | 643 | Tetragenes (scene text) | Opera, Chisato | - |
| 58 | Opera + Ernest absent (14) |  | 643 | Tetragenes (scene text) | Opera | - |
| 59 | Opera alone |  | 643 | Tetragenes (scene text) | Opera | 1 |
| 60 | Precis + Leon |  | 207 | Town of Linga | Precis, Leon | 1 |
| 61 | Precis + Ernest |  | 196 | Town of Linga | Precis, Ernest | 1 |
| 62 | Dias + Precis |  | 196 | Town of Linga | Dias, Precis | 1 |
| 63 | Precis + Noel |  | 463 | Secret Charity/Fake Gallery | Precis, Noel | 4 |
| 64 | Precis + Chisato |  | 196 | Town of Linga | Precis, Chisato | 1 |
| 65 | Precis alone |  | 197 | Town of Linga | Precis | 2 |
| 66 | Bowman + Leon |  | 191 | Town of Linga | Bowman, Leon | 1 |
| 67 | Bowman + Ernest |  | 225 | Town of Lacour | Bowman, Ernest | 3 |
| 68 | Bowman + Dias |  | 460 | Mars Village | Bowman, Dias | - |
| 69 | Bowman + Noel |  | 191 | Town of Linga | Bowman, Noel | 5 |
| 70 | Bowman + Chisato |  | 191 | Town of Linga | Bowman, Chisato | 1 |
| 71 | Bowman alone |  | 191 | Town of Linga | Bowman | 1 |
| 72 | Leon + Ernest |  | 8 | Arlia Village | Leon, Ernest | 1 |
| 73 | Leon + Noel |  | 463 | Secret Charity/Fake Gallery | Leon, Noel | 4 |
| 74 | Leon + Chisato |  | 437 | Laboratory | Leon, Chisato | - |
| 75 | Leon alone |  | 211 | Town of Lacour | Leon | 8 |
| 76 | Dias + Ernest |  | 225 | Town of Lacour | Dias, Ernest | 2 |
| 77 | Ernest + Noel |  | 697 | - | Ernest, Noel | - |
| 78 | Ernest + Chisato |  | 8 | Arlia Village | Ernest, Chisato | 1 |
| 79 | Ernest alone |  | 8 | Arlia Village | Ernest | 1 |
| 80 | Dias + Noel |  | 312 | Town of Armlock | Dias, Noel | - |
| 81 | Dias + Chisato |  | 10 | Shingo Forest | Dias, Chisato | - |
| 82 | Dias alone |  | 10 | Shingo Forest | Dias | - |
| 83 | Noel + Chisato |  | 463 | Secret Charity/Fake Gallery | Noel, Chisato | 4 |
| 84 | Noel alone |  | 463 | Secret Charity/Fake Gallery | Noel | 4 |
| 85 | Chisato alone |  | 144 | Town of Herlie | Chisato | 2 |

Partners 12-14 are not party members: 12 is Chris, 13 Eleanor, and 14 marks Opera's ending
without Ernest (Opera is the only party sprite drawn in it).

## Where the guide disagrees with the disc

All on the guide's endings pages (printed pp. 134-135) unless noted.

1. **How many endings.** The guide speaks of 87 (also pp. 3 and 23). The disc's lookup has
   85 ids, each one scene.
2. **What decides a variant.** The guide says the higher romantic value decides which of a
   couple's endings plays. The disc compares the +0xe8 values, the table it uses for
   friendship between same-sex pairs, for every variant pair, including Claude + Rena. The
   romantic (+0x58) values only decide whether opposite-sex pairs become a couple.
3. **Claude + Chisato.** The guide puts the space-travel ending at Claude's value greater
   than or equal to Chisato's and the war-cameraman ending at less. On the disc it is the
   other way round, with equality going to space travel: id 16 (the war correspondent
   scene) when Claude's value is greater, id 15 (the survey flight) otherwise.
4. **Claude + Opera.** The guide's worked example (Claude 13 for Opera, Opera 7 for Claude:
   Tetragenes) contradicts its own list on p. 135 (Claude's value greater or equal: Earth).
   The disc agrees with the list: id 6 on Earth (map 628) when Claude's +0xe8 value is
   greater or equal, id 7 on Tetragenes (map 643) when it is less.
5. **Opera's special ending.** The guide gives it a fixed value of 26; the disc scores it 24.
6. **Fixed values.** The guide has the special endings set the characters' relationship
   values to a fixed number. The disc changes no value: it adds Celine + Chris (26), Ashton +
   Eleanor (20) or Opera without a partner (24) as one more candidate pair with that score.
7. **Rena's story only.** The guide says the Celine and Ashton specials need Rena as the main
   character. The selection does not look at the hero for them; it needs only the flag
   (0x1ab, 0x163) and the character in the party. Whether the flags can be set in Claude's
   story is up to the Private Action scripts on maps 78 and 156.
8. **Ties.** The guide says ties are settled by a fixed priority list (Claude, Rena, Celine,
   ... Chisato). The disc's exchange sort is not stable, so it is not a priority order. The
   c4_tie run below shows it: Claude + Precis and Claude + Chisato both score 20, and a
   pair with a higher score comes after them. The sort moves that pair to the front and
   Claude + Precis behind Claude + Chisato, so Claude goes with Chisato (id 15), where the
   guide's list would pick Precis (sixth) over Chisato (twelfth). PCSX-Redux plays the same.
9. The guide does not give the order of the scenes; the disc's order is in step 6 above.

## Checks

### The port, all 85 ids

`scratchpad/menu/m8/end/plan.py` wrote 32 runs (`plan.json`) that between them cover every
id. Each starts the port with `--map 581`, pokes the party, both tables and the flags
(`SS_SWEEP_PARTY`, `SS_SWEEP_CONFIG`, `SS_SWEEP_FLAGS`) and logs the list the script builds
and the maps it plays (`SS_ENDING_LOG`). `runall.py` checks the lists, `runscenes.py` the
sprites.
- All 32 built exactly the model's list and played its maps in order to the end.
- All 85 ids were played; the scenes ran 732 to 24192 vblanks (median 3014).

### PCSX-Redux, five lists from the late save

`scratchpad/menu/m8/cmp/`: the user's late save loaded in both (a fresh copy for each run),
the tables and flags poked once the field has settled, then map 581 loaded as field state 1
does (port `SS_SWEEP_GOTO=581`, Redux `endref.lua`), with no input after that.

| Run | Setup | List (both) |
|---|---|---|
| c0_save | the save's own values | 85 84 75 49 65 41 3 |
| c1 | Claude + Precis (variant 9), Rena + Leon, both specials, Noel + Chisato | 83 48 40 23 9 |
| c2 | Claude + Chisato (variant 16), Rena + Celine, Precis + Ashton, Leon + Noel | 73 42 18 16 |
| c3 | Claude + Rena (variant 1), Ashton + Chisato, Celine + Precis | 84 75 47 32 1 |
| c4_tie | the tie above | 75 65 41 46 29 15 |

In every run the port and Redux built the model's list, played the same maps in the same
order, and drew the same sprites in each of the 27 scenes (drawn frames within 37 of each
other). Scene lengths agree within 1.01 %; the largest gap is the first scene of c4_tie
(map 211, 63 vblanks), where the console's longer load falls inside the scene. Only the
first stretch on map 581 differs more: 666 vblanks in the port, 821-855 in Redux, which is
CD loading. (The port logs its return to 581 after the credits, since its log runs in field
frames and the credits are a separate program, so the last scene's length is not compared.)

### The user's late save, played to the end in both

The save (`scratchpad/latesave/card.mcd`, copied afresh for every run) stands at the Final
Save Point, Fienal's ninth floor (map 671; guide printed p. 127). Party: Ashton, Noel, Claude,
Chisato, Leon, Rena, Celine, Precis; Claude is the hero; the save maps confirm to Circle.
The model predicts 85, 84, 75, 49, 65, 41, then 3: six solo endings and Claude + Rena with
equal +0xe8 values. Claude + Rena, Claude + Celine and Rena + Ashton all score 30; the sort
keeps Claude + Rena first.

**The route.** 671's north exit (trigger T5) leads to 684, the stair room, which the game
uses three times: G[0x1ce] counts the climbs, and the top exit (T2) leads to 684 again twice,
then to 685. On 685: movie 24, a reload, the confrontation, the battle (`battle_c` at word
7229, taken because flag 0x22c is set; encounter 540), the aftermath, the final movie, map
581 and the endings.

**Same input on both sides.** The port's route mode (`SS_ROUTE`, event_sweep.cpp) and
`scratchpad/menu/m8/late/lateref.lua` in PCSX-Redux follow the same rules, decided at the end
of each frame:
- once the player has had control (field state 0, a controlled actor, the leader's actor) 20
  frames in a row on 671 or 684, hold up;
- when a window has waited for the button (state 6 / 7) or a choice has been up for 10
  frames, press confirm for 2 frames;
- nothing else, in battle neither.

The first 20-frame streak on 671 is the sync point (t = 0); the port takes the console's
generator there (`SS_ROUTE_RNG`). From battle frame 600, after the compared opening, both set
the monsters to 1 HP to end the fight (`SS_BATTLE_HP1`, lateref.lua `SS_ENEMY_HP1`).

**Results** (`scratchpad/menu/m8/late/p_b` against `r_b`, `cmplate.py`):
- **The walk:** all 114 leader samples (position and view, every 30 frames of control on 671
  and the three climbs) are equal. The maps change at t 170 (684) in both; 685 comes 45
  frames later in Redux (CD loading).
- **685 before the battle:** all 12 confirm presses come exactly 154 frames later in Redux,
  the console's load time of 685 and its movie; the battle starts at t 7024 (port) and 7182.
- **The generator:** equal at the battle's first frame (index 1, hash e2caeafb, 17 draws
  since the sync in both). Both sides make the same calls in the same order (traced: gdb on
  the port, the return addresses on the console), except for two that wait for CD loads:
  - the field voice queue on 671 (four draws, `voice_queue_build_field`): the port makes them
    when the map loads, before the sync; the console 33 frames after it;
  - the encounter distance's extra roll (field_update, decompile line 554), made only once
    the battle preloads are in: three times in the port on 671, never on the console, where
    the preloads were still loading when the party left.
- **The battle's opening** (`SS_BATTLE_UNITS` against a breakpoint at 0x8004693c, the end of
  each battle frame's logic):
  - unit positions and HP are equal every frame from 62 to 600; before frame 62 the console
    is still filling the party slots as their models load;
  - the generator is equal from frame 0 to 100;
  - the first `enemy_think` draw (rng_range(2), from 0x8003cb44) comes at battle frame 102
    on the console and at frame 0 in the port; from frame 408, when the fight starts, the
    AI's draws differ. This is for the battle work (handed over), not the story: both win
    the fight, in 2734 (port) and 2723 battle frames.
- **After the battle:** the aftermath's confirm presses come 61 frames later in Redux, all
  of them. The original writes the current map (game_state +0x1a) as 581 when the final
  movie starts, the port when 581 loads after it; from the battle's end to the first ending
  scene the console takes 619 vblanks (4 %) longer (streaming).
- **The endings:** both build and play 85, 84, 75, 49, 65, 41, 3, the model's list, on the
  same maps, with the same party sprites in every scene (drawn frames within 19) and scene
  lengths within 0.89 %.
