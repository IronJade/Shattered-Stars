# Bugs Fixed and Quirks Kept

The original *Star Ocean: The Second Story* has bugs, as most games do. Rewriting its code by
hand meant reading every one of them. This page explains how Shattered Stars treats them and
lists every case found so far.

## The policy

**Fix bugs without changing how the game plays.** Hit timing, the number of hits, damage,
difficulty and battle pacing stay exactly as in the original. A fix may correct how something
looks, or an obvious mistake around the gameplay, but it must not change the gameplay itself.

**Measured, not guessed.** "How the game plays" is checked against the original game running
in the PCSX-Redux emulator. Before a fix goes in, the faithful version is run side by side
with the original and compared: the frames on which hits land, the hits, the damage, the waits,
and the game's random numbers. If a fix would change any of those, it is **held** and the
numbers go to the project lead to decide.

**Crashes are avoided.** Where the original reads or writes memory it shouldn't, and on a
console only got away with it by luck, the port does something safe instead. Up to the point
where the original would have gone wrong, it behaves exactly the same.

**Saves stay compatible.** Save files keep the original's exact format, even the spots where
the original stores leftover memory, so cards work in both directions.

**Nothing is hidden.** In the source code, the original's line is kept as a comment marked
`ORIGINAL BUG (original_bugs.md <id>)`, with the corrected code below it. Every case is listed
in the project's [original_bugs.md](https://github.com/IronJade/Shattered-Stars/blob/main/docs/port/notes/original_bugs.md),
which has the technical details. The ids on this page (B1, E5, S20, ...) are the same, so you
can look any of them up there.

### What the words mean

| Status | Meaning |
|---|---|
| **Fixed** | Corrected in the port. Unless stated otherwise, the fix is visual only and was measured to leave gameplay unchanged |
| **Avoided** | The original reads garbage or overruns memory there. The port does something safe instead and otherwise plays the same |
| **Kept (intended)** | Looked like a bug, but the project lead decided it is the original's intended behaviour, so it stays |
| **Kept (gameplay)** | A real bug, but fixing it would change gameplay, so the original's behaviour stays |
| **Held** | A real bug whose fix would change gameplay. Not fixed for now; the fix exists but is off |
| **Not fixed yet** | Still as in the original, waiting for a fix |
| **Not a bug** | On closer reading, the code is right |

Some held fixes can be switched on for testing with the `SS_HELD_FIXES` environment variable,
for example `SS_HELD_FIXES=S5`. This changes gameplay, so it is not meant for normal play.

## At a glance

| Area | Fixed | Avoided | Kept or held | Total |
|---|---:|---:|---:|---:|
| Battle and stages | 10 | 6 | 4 | 20 |
| Enemies | 9 | 2 | 3 (and 1 not a bug) | 15 |
| Field effects, and party killer moves | 8 | 7 | 3 | 18 |
| Field, menus and events | 6 | 4 | 2 | 12 |
| Spells and items | 11 | 1 | 8 | 20 |
| **All** | **44** | **20** | **20** (and 1 not a bug) | **85** |

The crash and memory cases from every area are also gathered in one table at the end. Bugs in
Shattered Stars itself, rather than in the original, are listed separately:
[Bugs in the port](#bugs-in-the-port).

## Battle and stages

| Id | What it was | What the port does |
|---|---|---|
| B1 | The ribbon trails of some killer moves (Opera's, for example) changed width along their length, because every segment used the first segment's angle | **Fixed**, visual only |
| B2 | Afterimage copies: with an even number of copies, half were drawn twice and half never | **Fixed**: each copy is drawn once, newest first |
| B3 | Particle lines were sorted at the wrong depth, so they could appear in front of or behind the wrong things | **Fixed**, visual only |
| B5 | The stage lens flare changed the shared flare tables in place every time it was drawn | **Fixed**: it works on a copy. The flare looks the same |
| B9 | Bowman's leap can miss in depth when the target is far away or moving | **Kept (intended)**: the leap is meant to be able to miss, as Claude's and Dias's Headsplitter can |
| B10 | Bowman's move 6 places its clones at the target's four corners; a fifth clone (at move level 3) appears wherever its memory slot was last used | **Not fixed yet** |
| B11 | On one special stage, the first hanging object's shadow used the second object's size and stretched towards it | **Fixed**, visual only |
| B12 | On the same stage, the first hanging object never falls after a spell's finishing hit, because the fall check looks at the wrong object | **Kept (gameplay)**: it decides whether the object falls |
| B13 | On the stage with the mine cart, the damage number said 50 while the cart took 100 HP | **Fixed**: the number shows the 100 actually taken |
| B14 | When a party member first jumps at a flying enemy depends on a value nobody set: whatever the stage file left in that spot of memory | **Kept (gameplay)**, and reproduced exactly: the port works out the value the console would read, so the jump starts on the same frame |
| B18 | In a killer move's ribbon trail, when the trail's first segment was off screen, the first segment drawn started from leftover values nobody had set, which could stretch it across the screen | **Fixed**, visual only: that segment works out its own starting edge. In every recorded case the graphics chip dropped the stretched piece either way, so those frames are unchanged |
| B19 | The stage lens flare's fan of triangles left the camera's roll out of its first point, so with the camera rolled one wedge overlapped its neighbour (a brighter wedge) and another was left open | **Fixed**, visual only. The roll was 0 at every flare in the recordings and a sweep of 64 stages, so no recorded frame changes |
| B20 | One kind of stage-model polygon (lit flat triangles) was cut off at an eighth of the distance every other kind reaches, because its depth test ran before a shift instead of after | **Fixed**, visual only. No stage model uses that kind; the few spell polygons it restores in the recordings are hidden behind nearer ones |

B4, B6, B7, B8, B15, B16 and B17 are memory problems; see [Crashes and memory errors](#crashes-and-memory-errors).

## Enemies

| Id | What it was | What the port does |
|---|---|---|
| E1 | A range test in the Arcmene's behaviour compared the wrong value and always passed | **Fixed**: it checks whether the nearest party member is within range, as intended (the faithful version was checked against the original first) |
| E2 | The Ericodus's attack 5 could reach a step no code handles, freezing the monster in place | **Fixed**: a block or a landing moves it on |
| E3 | When Visseyer or Jibril released a swallowed party member, some attempts stored the member's position as its speed | **Fixed** |
| E4 | Bark's vortex rings were drawn at the wrong heights (right only at ground level) | **Fixed** |
| E5 | Marsilio's afterimage stopped as soon as his swing hit, instead of lasting to the end of the swing | **Fixed**: it lasts the whole swing. Visual only; measured identical |
| E6 | Shigeo's chance test always passes | **Kept (intended)**: Shigeo is meant to be unbeatable in the first fight |
| E7 | Celesta's status check looked like it returned the wrong thing | **Not a bug**: it reports her immunity to stun |
| E8, E10 | Indalecio's halo afterimage was never made | **Fixed** (E10 turned out to be the cause of E8) |
| E9 | The Carlaeagle's feathers started a fixed distance above the ground instead of above the bird | **Fixed**: they are drawn starting at the bird and slide onto the original path, so they hit on the same frames, in the same places, for the same damage |
| E13 | The splashes of the Noah spell's wall (sounds, sparks, screen shakes) appeared on the caster's own side; the damage was right | **Fixed** |
| E14 | A released party member comes out facing an arbitrary way instead of towards the monster | **Kept (intended)** |
| E15 | A quirk in how battle voices are tracked decides how long a voice counts as playing, and so how long a boss waits after its intro line | **Kept (intended)**: the original's voice timing stays |

E11 and E12 are memory problems; see [Crashes and memory errors](#crashes-and-memory-errors).

## Field effects, and party killer moves

Move numbers here and below are the game code's own numbering of each character's killer
moves, counted from 0.

| Id | What it was | What the port does |
|---|---|---|
| F1 | Some line and burst effects on field maps were sorted at the wrong depth | **Fixed** |
| F3 | Starting a second glow effect left the first one drawing | **Fixed**: the new glow replaces the old |
| F4 | A lit sphere effect only draws its lower half | **Kept (intended)**: this is the intended look |
| F5 | When a field movie was drawn partly off the screen edge, its rows were all drawn onto the top row | **Fixed** |
| F6 | A clipping rectangle on some map layers is set too late to have any effect | **Avoided**: the port leaves it out, which gives the same picture as the real hardware |
| F7 | A glow at full tint wrapped round to no tint at all | **Fixed** |
| F8 | Five small mistakes in how characters' sprites on field maps are split into lit pieces (a stale colour, wrong texture coordinates, flipped pieces with no texture, hidden polygons drawn, stopping early at the map's edge) | **Fixed**, all five |
| F18 | When the field's voice clips finished loading, the "loaded" mark went on every party slot rather than each member's own | **Avoided**: each member's own slot is marked. Gameplay is unaffected |
| F12 | Chisato's move 6 scatters its stars sideways but, because of a unit mix-up, never in depth | **Held**: the fix would make more of the later stars miss, weakening the move |
| F13 | In Opera's move 7, the last shard should fly straight up but drops from the burst point | **Kept (gameplay)**: with the fix, the move's two hits land 8 and 19 frames earlier |
| F14 | The hit sparks of the same move jittered vertically by almost nothing, where every other move's sparks jitter visibly | **Fixed**: the same jitter as the other moves; the same random numbers, hits and damage |
| F15 | The darts of Ashton's move 3 could be drawn facing the wrong way | **Fixed**: drawn by Ashton's facing; measured: the same hits and damage |
| F17 | When Ashton's move 0 hit several enemies, every hit sprite was picked by the first hit's height | **Fixed**: each by its own hit's height; the same hits and damage |

F2, F9, F10, F11 and F16 are memory problems; see [Crashes and memory errors](#crashes-and-memory-errors).

## Field, menus and events

| Id | What it was | What the port does |
|---|---|---|
| W1 | The report window after a battle had a fifth row (MAG) hidden under the window's edge | **Fixed**: the window is one line taller. Its content is unchanged |
| M1 | Renaming Claude in the camp menu compared the new name with "Crawd" (the Japanese release's name) instead of "Claude", so keeping the name "Claude" switched his battle voices to the lines that avoid saying his name | **Fixed**: the check uses "Claude", as the title screen's naming already did |
| M2 | When typing a name, reaching seven characters moved the cursor onto Delete, so the next press deleted the letter just typed | **Fixed**: the cursor moves to OK |
| M3 | The rename screen doesn't put back some of the video memory it uses | **Not fixed yet**, and harmless: everything affected is reloaded afterwards |
| W5 | The courier who delivers an order adds all its copies at once. If that would take you past 20 of the item (because you found more of it between ordering and the delivery), every copy is lost, though the delivery still says they arrived | **Kept (gameplay)**: fixing it changes what you hold. With an [Archipelago](Archipelago#shops) seed the copies that fit go in and the rest wait until you have room, since a shop slot's check can bring one more of an item you ordered |

W2, W3, W4, M4, M5, V1 and V2 are memory problems; see [Crashes and memory errors](#crashes-and-memory-errors).

## Spells and items

| Id | What it was | What the port does |
|---|---|---|
| S1 | Foehn: the wall's sounds and leaves appeared on the caster's own side; the damage was right | **Fixed**: the leaves are drawn on the correct side, using a separate random-number source for looks only, so the game's own random numbers don't change |
| S2 | One battle item's 28 blasts landed along the middle of the arena instead of at the enemies | **Fixed**, visual only |
| S3 | A monster's counter-spell cast right after an item hit it had no target, and the bolt-type spells (Fire Bolt, Ice Needle and others) aimed at an empty spot | **Fixed**, gameplay identical: the port aims them exactly where the console's went |
| S5 | Black Saber / Sonic Saber: in a combo, the blade returning to its owner flies back almost flat | **Held**: the returning blade still hits, so its path decides what it hits. `SS_HELD_FIXES=S5` turns the fix on |
| S6 | Earthquake: the third wave lifted the wrong part of the ground | **Fixed** |
| S7 | Blood Sucker: the glow appears at the feet of a unit on the ground and far off for a flying one | **Kept (intended)** |
| S8 | Lunar Light: the second ring of sparks didn't turn, and its turn went to the first ring | **Fixed** |
| S9 | Shadow Flare / Judgement Day: the second shimmer row used the wrong wave phase | **Fixed** |
| S10 | Press / Gravity Press: of the dizzy stars, one was drawn twice, and the animation ran too fast | **Fixed**: three stars spaced evenly, at the right speed. Measured: visual only |
| S11 | Deep Mist kept running while the battle was paused | **Held**: the fix would change the game's random numbers whenever you pause during the spell. `SS_HELD_FIXES=S11` |
| S12 | Explode in a three-way combo hits the caster's own side | **Held**: the fix changes who is hit. `SS_HELD_FIXES=S12` |
| S13 | The shatter effect (Haste, Delay) had several drawing mistakes: shards ignoring their colour, reading the wrong corners and so on | **Fixed** (checked against the original first) |
| S14 | Silence: the sign starts at the target's feet instead of at its height | **Kept (intended)** |
| S15 | Curse: the skull starts at the wrong height | **Kept (intended)** |
| S16 | Haste / Delay: the shatter sprites had no spread in depth | **Fixed**, using the looks-only random source, so the game's random numbers don't change |
| S17 | Gremlin Lair never advances its own timer, so its pauses don't pause | **Held**: the fix makes the spell 42 frames longer and shifts every later random roll, and those frames count against timed battles. `SS_HELD_FIXES=S17` |
| S18 | Raise Dead: the funnel's height was wrong except at ground level | **Fixed**, visual only |
| S19 | Deep Mist: the opening wait doesn't wait | **Held**: the fix shifts the game's random numbers. `SS_HELD_FIXES=S19` |
| S20 | A spell on the whole party (Fairy Light, Cure All, ...) chosen as a member's very first action of the battle took the MP and did nothing | **Fixed**: the spell now works, targeting the caster. Spending MP for nothing was judged a real bug, not intended play. When the member already has a target, nothing changes |

S4 is a memory problem; see the next section.

## Crashes and memory errors

In these cases the original reads uninitialised memory, reads past the end of a table, or
writes over memory it doesn't own. On a console this sometimes just happens to work, and
sometimes would crash. The port does something safe instead. Up to the point where the
original goes wrong, it plays exactly the same.

| Id | Where | What could go wrong | What the port does |
|---|---|---|---|
| B4 | Battle damage | A loop that picks a random target walks past the end of its list into leftover memory | **Avoided**: it stays within the list |
| B6 | Battle movement | A movement result starts from leftover memory | **Avoided**: it starts from the unit's position |
| B7 | Battle voices | Picking a voice for an absent party member reads past a table | **Avoided**: the member counts as down |
| B8 | Battle sound | An invalid sound handle reads past the eight sound slots | **Avoided** |
| B15 | Battle sound | A sound keeps reading the object that started it after that object is deleted | **Fixed**: the sound carries on from the object's last position. Sound only |
| B16 | Spell projectiles | A spell projectile meeting five or more enemies at once overruns its four-entry lists, likely a crash on the console | **Avoided**: the lists hold eight. With up to four it plays exactly as the console |
| B17 | Rolling objects on three stages (a mine cart, two boulders) | Five or more enemies inside a rolling object at once overrun its four-entry list, likely a crash | **Avoided**: the list holds eight. With up to four it plays exactly as the console |
| S4 | Black Saber / Sonic Saber | More than four units in the blade's path overrun its lists, likely a crash with six or more | **Avoided**: the lists hold eight. With up to four it plays exactly as the console |
| E11 | The bunny | When no spot is found, it reads an unset value | **Avoided** (zero) |
| E12 | Enemy drawing | An unknown drawing mode leaves the colour unset | **Avoided** (zero) |
| F2 | Field burst effect | Its depth is not range-checked | **Avoided**: an out-of-range piece is dropped |
| F9 | Mirror images | A stale pointer is left behind on the world map | **Avoided**: cleared |
| F10 | World map | Points beyond the horizon read past a table | **Avoided** |
| F11 | Precis's move 8 | With every effect slot in use, the beam is drawn from nothing | **Avoided**: no beam then |
| F16 | Ashton's darts | The sound plays at a dart that failed to appear | **Avoided**: no sound then |
| W2 | Vehicles on the world map | With no direction held, the speed limit comes from an unset value | **Avoided** (zero, as the original shows in practice) |
| W3 | An event-script command | Gives a character two unset values | **Avoided**: they keep their previous values |
| W4 | A sound command in two scenes: the Field of Courage before the Guardian, and Eluria Tower before Berle | The scenes give the command one value fewer than it takes, so it takes a stray value from below the script's stack and leaves the stack one place short. After the battle that follows, the script ran with its stack far outside its own memory until the map changed. In the port this was a memory overflow that could crash | **Fixed** (sound only): the command reads the missing value as 0, as the console in practice did, and the stack stays balanced. The scenes' sound and everything after them are unchanged. The values the scenes pass suggest a change of volume and pitch was meant, but the original never applied it, so the port doesn't either |
| M4 | Menu sprites | Some sprites read their "colours loaded" mark past the end of a table | **Avoided**: their colours are uploaded every time |
| M5 | Rename screen | A line of text could be written past the end of its buffer | **Avoided**: a bigger buffer |
| V1 | On-screen notices | Closing a notice whose window is already gone writes through an empty pointer into low memory. No shipped event was seen doing this; the port's full event sweep found it | **Fixed**: the step is skipped when the window is gone |
| V2 | Talking to characters | When a conversation ends by removing the character you spoke to (for example on map 753), the original keeps using it after freeing it, and frees it twice. Harmless on the console only by luck; the port crashed there | **Fixed**: the step stops once the character is gone |

## Bugs in the port

The lists above are the original game's bugs. Shattered Stars has had bugs of its own too, where
the port didn't yet do what the original does. These were fixed in **0.02.50**:

| What it was | Since | What the port does now |
|---|---|---|
| **Claude's sword in Rena's game.** A party member's weapon was drawn with the wrong part of its texture whenever they weren't in the party's first slot, so in Rena's game, where Rena is first, Claude's sword looked like a stretched blur. It affected every weapon user outside the first slot, in any game | 0.01.00 | Weapons are drawn right in every slot, as on the console. Graphics only: the battles play out exactly as before |
| **The menus read past their window art.** Every menu (the camp menu, shops, saving, the title's Continue screen) read about 9.5 KB past the end of its window graphics when it opened, and copied those bytes into a part of video memory the screen doesn't show. Nothing on screen changed, but reading past a buffer can crash | 0.01.00 | The menus upload exactly the window art the original does. Every menu looks the same as before |
| **The Cave of Trials' learn chests in Archipelago seeds.** In seeds made with `so2.apworld` 0.1.x and the Cave of Trials on, the three spots where Leon learns Extinction, Ashton tri-Ace and Celine Meteor Swarm counted as item chests, and opening one made the game read and write outside the party members' records | 0.01.00 | Nothing is learned or written there, and the spot gives or sends its location's item. Script commands that pick a member's move by number now check that the number is in range (the original's own scripts always pass one that is). 0.2.0 seeds no longer have those locations |
| **The shop mark that never showed.** 0.02.00 meant to show Archipelago shop slots still to check in another colour, but the colour was set too late to take effect, so they looked like any other entry | 0.02.00 | The Archipelago logo marks them; see [Archipelago](Archipelago#shops). Buying from such a slot also gives you what you bought now, as well as the check |

And in **0.02.51**, all of them in Archipelago games (details on
[Archipelago](Archipelago#what-changed-in-021)):

- **Items still waiting to be given were lost when you saved.** A check that gives several items
  hands them out one at a time; a save made before the last one arrived didn't keep the rest.
  They are kept with the save now, and nothing is given twice.
- **A full inventory held up everything else you received**, other players' items included,
  until you made room. The item that doesn't fit now waits on its own.
- **A door a seed keeps shut let you walk off the map** (open world and hub mode), and you could
  get stuck. You now stop at the doorway.
- **A member borrowed for a fight could push someone out of a full party** (Claude in Shingo
  Forest in Rena's game, Dias or Claude at the Lacour tournament) when the scene moved people
  around meanwhile. The game now remembers who borrowed the place.
- **Dias came out of the Lacour tournament** at the tournament's level with his starting
  equipment, when you had received him or started with him. He now comes out as he went in.

## Reporting a new one

Found something that looks like a bug in the original, or a difference from the console?
See [Contributing](Contributing#reporting-a-bug). If it is a bug in the original, it gets the
next id in its area and goes through the same process: measure the faithful version against
the original first, then fix it only if gameplay stays the same.
