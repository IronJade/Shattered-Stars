# Spell coverage (M7, the spell part)

Every spell (0x01-0x46) and every spell module, with its block hash, the port's code and
how it is verified. Written by `scratchpad/world/cov/gen_cov.py` from the battle program's
tables (a Redux RAM dump: effect kind 0x8007002f, data entries 0x8006d1f2), the class spell
lists (0x8006b1b8..), the monsters' spell slots (encounter packs, stats record +0x3c) and
the module index (`scratchpad/world/spell/modules/index.txt`).

**Verified** means a PCSX-Redux reference and the port, run on the same forced battle, agree
on every RNG sample and every unit sample (cmp.py), with cast and finish frames equal; the
console's slow frames are replayed in the port (SS_BATTLE_TICK_MAP), and differences only
inside them are noted. Runs are in `scratchpad/world/spell/ref/<run>_3` (sweep_cfg.py
describes each: `iNN` the class's casts one after another, `eX` a monster casting, `x*` /
`m*` items, clashes and the menus), unless another agent's.

**Variants.** A spell's data entry depends on the caster (`unit_caster_variant`): monsters
and fighters 0, Rena 1, Leon 2, Celine 3, Noel 4. The module code is the same, but each
variant brings its own data (textures, models, sounds and parameters), so each variant a
caster can use is listed. The Shot spells (no data) are listed by the caster's side.

## Spells

| Spell | Name | Port | Cast by | Verification |
|---|---|---|---|---|
| 0x01 | Firebolt | resident Shot: spell_shots.cpp `spell_firebolt` | party: Celine; 2 monster types | party: verified iCv<br>monsters: verified 27 Magius, iC340 |
| 0x02 | Eruption | module 7f6a6e02c6: spell_mod_eruption.cpp | party: Celine; monsters: Decus, Pilesherry, XINE | Celine (v3 0x8fb): verified i11b<br>monsters (v0 0x8fa): verified eA |
| 0x03 | Explode | module a639d47ca2: spell_mod_explode.cpp | party: Celine; monsters: Decus, Weird-mage | Celine (v3 0x918): verified i11a, s11<br>monsters (v0 0x917): verified eA |
| 0x04 | Ice Needle | resident Shot: spell_shots.cpp `spell_ice_needle` | party: Leon | party: verified iLv |
| 0x05 | Deep Freeze | module 6673c9f485: spell_mod_deep_freeze.cpp | party: Leon; monsters: Zand | Leon (v2 0x8fe): verified i10a<br>monsters (v0 0x8fd): verified m265 Zand (enemy agent), eA |
| 0x06 | Acid Rain | module 63f77d1822: spell_mod_acid_rain.cpp | party: Leon | Leon (v2 0x8e4): verified i10a |
| 0x07 | Noah | module 6563c70f92: spell_mod_noah.cpp | party: Leon; monsters: G-Celesta, Indalecio, Meigus, Nicolus | Leon (v2 0x912): verified i10a<br>monsters (v0 0x911): verified m515 / m539 (enemy agent), eA |
| 0x08 | Wind Blade | resident Shot: spell_shots.cpp `spell_wind_blade` | party: Celine, Noel; 1 monster types | party: verified iCv, iNa<br>monsters: verified 41 Beastmaster |
| 0x09 | Sonic Saber | module b82e4cf842: spell_mod_sonic_saber.cpp | party: Noel | Noel (v4 0x900): verified i12 |
| 0x0a | Foehn | module 2a9c041ec7: spell_mod_foehn.cpp | party: Noel; monsters: G-Celesta, Shin, Wizard | Noel (v4 0x916): verified i12<br>monsters (v0 0x915): verified 516 Shin, rn340 |
| 0x0b | Magnum Tornado | module 53455d80ad: spell_mod_magnum_tornado.cpp | party: Noel; monsters: Nightmare, Phoenix, Wizard | Noel (v4 0x907): verified i12<br>monsters (v0 0x906): verified 197 Nightmare, rn340 |
| 0x0c | Tetanus Wind | module 03874d77f8: spell_mod_tetanus_wind.cpp | party: Noel | Noel (v4 0x902): verified i12 |
| 0x0d | Thunderbolt | resident Shot: spell_shots.cpp `spell_thunderbolt` | party: Celine | party: verified iCv |
| 0x0e | Thunderstorm | module 9f010f460d: spell_mod_thunderstorm.cpp | party: Celine; monsters: Harfainx | Celine (v3 0x8f7): verified i11b<br>monsters (v0 0x8f6): verified eA |
| 0x0f | Reflection | module fb4ba65cff: spell_mod_reflection.cpp | party: Celine; monsters: Weirdgoat | Celine (v3 0x91d): verified i11b<br>monsters (v0 0x91c): verified eE |
| 0x10 | Thundercloud | module 569e24f878: spell_mod_thundercloud.cpp | party: Celine; monsters: Eldermagius, Goathead, Jibril | Celine (v3 0x921): verified i11a<br>monsters (v0 0x920): verified eB |
| 0x11 | Forget | module 68317e194a: spell_mod_forget.cpp | party: Celine | Celine (v3 0x8e6): verified i11a, s11 |
| 0x12 | Starlight | module af4ef66c53: spell_mod_starlight.cpp | party: Leon, Celine; monsters: Beastmaster, Magius | Leon (v2 0x8e9): verified i10b<br>Celine (v3 0x8e8): verified iCv<br>monsters (v0 0x8e7): verified 27 Magius, 41 Beastmaster |
| 0x13 | Meteor Swarm | module ac59fc1380: spell_mod_meteor_swarm.cpp | party: Celine; monsters: Hell-servant, Iselia-queen | Celine (v3 0x968): verified i11b<br>monsters (v0 0x967): verified eB |
| 0x14 | Southern Cross | module dfd43383ca: spell_mod_southern_cross.cpp | party: Celine; monsters: Warlock | Celine (v3 0x96c): verified i11b<br>monsters (v0 0x96b): verified eB |
| 0x15 | Seven Stars | none (effect kind 0, no data: casting it does nothing) | - | never cast (no class list, no monster) |
| 0x16 | Tractor Beam | module 3d76980103: spell_mod_tractor_beam.cpp | party: Rena; monsters: Eldermagius, Vesper | Rena (v1 0x904): verified i9b<br>monsters (v0 0x903): verified eB |
| 0x17 | Grave | resident Shot: spell_shots.cpp `spell_grave` | party: Noel | party: verified rn340, iNa |
| 0x18 | Earth Grave | module fb43fdecea: spell_mod_earth_grave.cpp | party: Noel | Noel (v4 0x91f): verified i12 |
| 0x19 | Earthquake | module 43d5518828: spell_mod_earthquake.cpp | party: Noel; monsters: Greatergoat | Noel (v4 0x975): verified i12<br>monsters (v0 0x974): verified eF |
| 0x1a | Ray | resident SpellCast kind 1: spellcast.cpp `spell_kind1_update` | party: Rena, Celine | Rena (v1 0x8e1): verified x9<br>Celine (v3 0x8e2): verified iCv |
| 0x1b | Light Cross | module 440856a00a: spell_mod_light_cross.cpp | party: Rena; monsters: Warlock, XINE | Rena (v1 0x8f9): verified i9b<br>monsters (v0 0x8f8): verified eC |
| 0x1c | Star Flare | module cbdc2116f3: spell_mod_star_flare.cpp | party: Rena; monsters: Breakwing, G-Celesta, Indalecio | Rena (v1 0x923): verified i9b<br>monsters (v0 0x922): verified m528 Breakwing (enemy agent), eC |
| 0x1d | Lunar Light | module e794431858: spell_mod_lunar_light.cpp | party: Celine | Celine (v3 0x927): verified i11a |
| 0x1e | Wounds | resident Shot: spell_shots.cpp `spell_wounds` | party: Leon | party: verified iLv |
| 0x1f | Black Saber | module 67a7a8d842: spell_mod_black_saber.cpp | party: Leon; monsters: Shynesslady | Leon (v2 0x929): verified i10a<br>monsters (v0 0x928): verified eC |
| 0x20 | Shadow Bolt | module d00581e30f: spell_mod_shadow_bolt.cpp | party: Leon | Leon (v2 0x8eb): verified i10a |
| 0x21 | Shadow Flare | module ae70313b32: spell_mod_shadow_flare.cpp | party: Leon; monsters: Iselia-queen, Masterwizard, Weirdavia, Wisesorcerer | Leon (v2 0x92b): verified i10b<br>monsters (v0 0x92a): verified eC |
| 0x22 | Deep Mist | module 60d0314477: spell_mod_deep_mist.cpp | party: Rena, Leon | Rena (v1 0x92e): verified i9a, s9a<br>Leon (v2 0x92f): verified iLv |
| 0x23 | Gremlin Lair | module fc13393fe3: spell_mod_gremlin_lair.cpp | party: Leon | Leon (v2 0x90d): verified i10a |
| 0x24 | Daemon's Gate | module a6a642e880: spell_mod_daemons_gate.cpp | party: Leon; monsters: Cyril, D-feather, Weirddevil | Leon (v2 0x972): verified i10b<br>monsters (v0 0x971): verified m430 Weirddevil (enemy agent), eC |
| 0x25 | Extinction | module 02571c0499: spell_mod_extinction.cpp | party: Leon; monsters: Soulmaster | Leon (v2 0x97d): verified i10b<br>monsters (v0 0x97c): verified eD |
| 0x26 | Dark Circle | module fa606854ed: spell_mod_dark_circle.cpp | party: Leon | Leon (v2 0x93f): verified i10b |
| 0x27 | Press | module 6d0c9bf130: spell_mod_press.cpp | party: Rena | Rena (v1 0x909): verified i9b |
| 0x28 | Gravity Press | module bf76cb13f3: spell_mod_gravity_press.cpp | party: Rena | Rena (v1 0x90b): verified i9b |
| 0x29 | Energy Net | module 32406ffe6a: spell_mod_energy_net.cpp | party: Rena | Rena (v1 0x947): verified i9a, s9a |
| 0x2a | Anti | module b02674cbb8: spell_mod_anti.cpp | party: Rena, Celine; monsters: Weird-mage | Rena (v1 0x965): verified i9a<br>Celine (v3 0x966): verified iCv<br>monsters (v0 0x964): verified eE |
| 0x2b | Word of Death | resident Shot: spell_shots.cpp `spell_word_of_death` | party: Leon; 4 monster types | party: verified rn340, iLv<br>monsters: verified eF |
| 0x2c | Energy Arrow | module 00b1337668: spell_mod_energy_arrow.cpp | party: Celine, Noel | Celine (v3 0x931): verified i11a<br>Noel (v4 0x932): verified iNa |
| 0x2d | Blood Sucker | module 6d3d24cf0b: spell_mod_blood_sucker.cpp | party: Noel; monsters: Ghost, Phoenix, Pilesherry, Succubus, Vesper | Noel (v4 0x8ed): verified i12<br>monsters (v0 0x8ec): verified eD |
| 0x2e | Mind Absorber | module 6d3d24cf0b: spell_mod_blood_sucker.cpp (kind 7, +0x3c set) | party: Celine, Noel; monsters: Ruprecht, Vesper | Celine (v3 0x8f0): verified i11b<br>Noel (v4 0x8f1): verified iNa<br>monsters (v0 0x8ef): verified eD |
| 0x2f | Heal | resident Shot: spell_shots.cpp `spell_heal` | party: Rena, Noel; 1 monster types | party: verified rn340, iNa<br>monsters: verified eE |
| 0x30 | Cure Light | resident Shot: spell_shots.cpp `spell_cure_light` | party: Rena, Noel | party: verified rn340, iNa |
| 0x31 | Fairy Heal | module f8c3b94995: spell_mod_fairy_heal.cpp | party: Rena, Noel; monsters: Indalecio, Weirddevil | Rena (v1 0x93a): verified i9b<br>Noel (v4 0x93b): verified iNb<br>monsters (v0 0x939): verified eE |
| 0x32 | Cure All | module 95b9fdccfa: spell_mod_cure_all.cpp | party: Rena, Noel; monsters: Breakwing, Cyril, Decus, Goathead, Hellmaster, Jibril, Masterwizard, Shin, Weirdavia, Wisesorcerer, Wizard | Rena (v1 0x934): verified i9a, s9a<br>Noel (v4 0x935): verified iNa<br>monsters (v0 0x933): verified eE |
| 0x33 | Fairy Light | module 4d978c241f: spell_mod_fairy_light.cpp | party: Rena, Noel; monsters: Soulmaster | Rena (v1 0x90f): verified i9a, m9, m9b, s9a<br>Noel (v4 0x910): verified iNa<br>monsters (v0 0x90e): verified eE |
| 0x34 | Raise Dead | module 96440a6c9b: spell_mod_raise_dead.cpp | party: Rena | Rena (v1 0x938): verified x9b |
| 0x35 | Antidote | resident Shot: spell_shots.cpp `spell_antidote` | party: Rena | party: verified rn340 |
| 0x36 | Dispel | module e37e03cd9f: spell_mod_dispel.cpp | party: Rena, Noel | Rena (v1 0x94d): verified i9a, s9a<br>Noel (v4 0x94e): verified iNb |
| 0x37 | Haste | module 83a78870f2: spell_mod_haste.cpp | party: Rena, Leon | Rena (v1 0x941): verified i9b<br>Leon (v2 0x942): verified iLv |
| 0x38 | Delay | module 8e03dba0da: spell_mod_delay.cpp | party: Rena, Leon; monsters: Nicolus, Weirdavia | Rena (v1 0x952): verified i9b<br>Leon (v2 0x953): verified iLv<br>monsters (v0 0x951): verified eD |
| 0x39 | Growth | module 44645f977c: spell_mod_growth.cpp | party: Rena, Leon; monsters: Weird-mage | Rena (v1 0x944): verified i9a, s9a<br>Leon (v2 0x945): verified iLv<br>monsters (v0 0x943): verified eF |
| 0x3a | Protection | module 2acd939273: spell_mod_protection.cpp | party: Rena, Leon; monsters: Ruprecht | Rena (v1 0x94a): verified i9a<br>Leon (v2 0x94b): verified iLv<br>monsters (v0 0x949): verified eF |
| 0x3b | Angel Feather | module cfbae89db2: spell_mod_angel_feather.cpp | party: Rena, Celine | Rena (v1 0x955): verified i9b<br>Celine (v3 0x956): verified iCv |
| 0x3c | Silence | module 2ecc4bc085: spell_mod_silence.cpp | party: Rena, Noel; monsters: Greatergoat | Rena (v1 0x8f4): verified i9a, s9a<br>Noel (v4 0x8f5): verified iNb<br>monsters (v0 0x8f3): verified eD |
| 0x3d | Curse | module efdb20dc3f: spell_mod_curse.cpp | party: Celine, Noel | Celine (v3 0x95a): verified i11b<br>Noel (v4 0x95b): verified iNb |
| 0x3e | Bless | module 2a4716a80a: spell_mod_bless.cpp | party: Celine, Noel | Celine (v3 0x95f): verified i11b, s11<br>Noel (v4 0x960): verified iNb |
| 0x3f | Neutral | module d43a9b4c0f: spell_mod_neutral.cpp | party: Celine, Noel | Celine (v3 0x962): verified i11b, s11<br>Noel (v4 0x963): verified iNb |
| 0x40 | (unnamed) | module e75959284f: spell_mod_spell40.cpp | - | Ashton move 9: verified as_b10 Ashton move 9 (effects agent) |
| 0x41 | 'The Scream' | module a6a642e880: spell_mod_daemons_gate.cpp | - | items 3 / 4 (any member): verified x9 |
| 0x42 | 'Judgement Day' | module ae70313b32: spell_mod_shadow_flare.cpp | - | items 3 / 4 (any member): verified x9 |
| 0x43 | Mind Blast | module c5209066cb: spell_mod_mind_blast.cpp | monsters: bosses (scripted) | monsters (v0 0x97e): verified m537 Vesper (enemy agent) |
| 0x44 | Spicule | module c325548547: spell_mod_spicule.cpp | monsters: bosses (scripted) | monsters (v0 0x97f): verified m537 Decus (enemy agent) |
| 0x45 | Wind of Destruction | module 155b3550d0: spell_mod_wind_of_destruction.cpp | monsters: bosses (scripted) | monsters (v0 0x980): verified m538 Cyril (enemy agent) |
| 0x46 | Divine Comedy | module 476afdf4ce: spell_mod_divine_comedy.cpp | monsters: bosses (scripted) | monsters (v0 0x981): verified t540 Indalecio (enemy agent; exact through the first spell, then the console slows) |

## Modules

The 55 code modules (block 0 of a spell's data entry, linked for slot 0 / 1; the port finds
its module by the block's FNV-1a hash and size: `spell_modules.cpp`).

| Hash | Bytes | Entries | Spells | Port | Verified by |
|---|---|---|---|---|---|
| 63f77d1822 | 1632 | 0x8e3,0x8e4 | Acid Rain | spell_mod_acid_rain.cpp | 0x06: i10a |
| 68317e194a | 1160 | 0x8e5,0x8e6 | Forget | spell_mod_forget.cpp | 0x11: i11a, s11 |
| af4ef66c53 | 3168 | 0x8e7,0x8e8,0x8e9 | Starlight | spell_mod_starlight.cpp | 0x12: 27 Magius, 41 Beastmaster, i10b, iCv |
| d00581e30f | 2052 | 0x8ea,0x8eb | Shadow Bolt | spell_mod_shadow_bolt.cpp | 0x20: i10a |
| 6d3d24cf0b | 2928 | 0x8ec,0x8ed,0x8ee,0x8ef,0x8f0,0x8f1,0x8f2 | Blood Sucker / Mind Absorber | spell_mod_blood_sucker.cpp | 0x2d: eD, i12; 0x2e: eD, i11b, iNa |
| 2ecc4bc085 | 1524 | 0x8f3,0x8f4,0x8f5 | Silence | spell_mod_silence.cpp | 0x3c: eD, i9a, iNb, s9a |
| 9f010f460d | 2008 | 0x8f6,0x8f7 | Thunderstorm | spell_mod_thunderstorm.cpp | 0x0e: eA, i11b |
| 440856a00a | 2184 | 0x8f8,0x8f9 | Light Cross | spell_mod_light_cross.cpp | 0x1b: eC, i9b |
| 7f6a6e02c6 | 3856 | 0x8fa,0x8fb,0x8fc | Eruption | spell_mod_eruption.cpp | 0x02: eA, i11b |
| 6673c9f485 | 4096 | 0x8fd,0x8fe | Deep Freeze | spell_mod_deep_freeze.cpp | 0x05: eA, i10a, m265 Zand (enemy agent) |
| b82e4cf842 | 6132 | 0x8ff,0x900 | Sonic Saber | spell_mod_sonic_saber.cpp | 0x09: i12 |
| 03874d77f8 | 2264 | 0x901,0x902 | Tetanus Wind | spell_mod_tetanus_wind.cpp | 0x0c: i12 |
| 3d76980103 | 5112 | 0x903,0x904,0x905 | Tractor Beam | spell_mod_tractor_beam.cpp | 0x16: eB, i9b |
| 53455d80ad | 3904 | 0x906,0x907 | Magnum Tornado | spell_mod_magnum_tornado.cpp | 0x0b: 197 Nightmare, i12, rn340 |
| 6d0c9bf130 | 2628 | 0x908,0x909 | Press | spell_mod_press.cpp | 0x27: i9b |
| bf76cb13f3 | 3356 | 0x90a,0x90b | Gravity Press | spell_mod_gravity_press.cpp | 0x28: i9b |
| fc13393fe3 | 6576 | 0x90c,0x90d | Gremlin Lair | spell_mod_gremlin_lair.cpp | 0x23: i10a |
| 4d978c241f | 4468 | 0x90e,0x90f,0x910 | Fairy Light | spell_mod_fairy_light.cpp | 0x33: eE, i9a, iNa, m9, m9b, s9a |
| 6563c70f92 | 2436 | 0x911,0x912,0x913,0x914 | Noah | spell_mod_noah.cpp | 0x07: eA, i10a, m515 / m539 (enemy agent) |
| 2a9c041ec7 | 2236 | 0x915,0x916 | Foehn | spell_mod_foehn.cpp | 0x0a: 516 Shin, i12, rn340 |
| a639d47ca2 | 5660 | 0x917,0x918,0x919,0x91a,0x91b | Explode | spell_mod_explode.cpp | 0x03: eA, i11a, s11 |
| fb4ba65cff | 2856 | 0x91c,0x91d | Reflection | spell_mod_reflection.cpp | 0x0f: eE, i11b |
| fb43fdecea | 2360 | 0x91e,0x91f | Earth Grave | spell_mod_earth_grave.cpp | 0x18: i12 |
| 569e24f878 | 4172 | 0x920,0x921 | Thundercloud | spell_mod_thundercloud.cpp | 0x10: eB, i11a |
| cbdc2116f3 | 2908 | 0x922,0x923,0x924,0x925 | Star Flare | spell_mod_star_flare.cpp | 0x1c: eC, i9b, m528 Breakwing (enemy agent) |
| e794431858 | 3140 | 0x926,0x927 | Lunar Light | spell_mod_lunar_light.cpp | 0x1d: i11a |
| 67a7a8d842 | 6420 | 0x928,0x929 | Black Saber | spell_mod_black_saber.cpp | 0x1f: eC, i10a |
| ae70313b32 | 2796 | 0x92a,0x92b,0x92c | Shadow Flare / 'Judgement Day' | spell_mod_shadow_flare.cpp | 0x21: eC, i10b; 0x42: x9 |
| 60d0314477 | 1808 | 0x92d,0x92e,0x92f | Deep Mist | spell_mod_deep_mist.cpp | 0x22: i9a, iLv, s9a |
| 00b1337668 | 2648 | 0x930,0x931,0x932 | Energy Arrow | spell_mod_energy_arrow.cpp | 0x2c: i11a, iNa |
| 95b9fdccfa | 2372 | 0x933,0x934,0x935,0x936 | Cure All | spell_mod_cure_all.cpp | 0x32: eE, i9a, iNa, s9a |
| 96440a6c9b | 3916 | 0x937,0x938 | Raise Dead | spell_mod_raise_dead.cpp | 0x34: x9b |
| f8c3b94995 | 2904 | 0x939,0x93a,0x93b,0x93c,0x93d | Fairy Heal | spell_mod_fairy_heal.cpp | 0x31: eE, i9b, iNb |
| fa606854ed | 3728 | 0x93e,0x93f | Dark Circle | spell_mod_dark_circle.cpp | 0x26: i10b |
| 83a78870f2 | 2248 | 0x940,0x941,0x942 | Haste | spell_mod_haste.cpp | 0x37: i9b, iLv |
| 44645f977c | 2880 | 0x943,0x944,0x945 | Growth | spell_mod_growth.cpp | 0x39: eF, i9a, iLv, s9a |
| 32406ffe6a | 2600 | 0x946,0x947 | Energy Net | spell_mod_energy_net.cpp | 0x29: i9a, s9a |
| e75959284f | 1880 | 0x948 |  | spell_mod_spell40.cpp | 0x40: as_b10 Ashton move 9 (effects agent) |
| 2acd939273 | 1848 | 0x949,0x94a,0x94b | Protection | spell_mod_protection.cpp | 0x3a: eF, i9a, iLv |
| e37e03cd9f | 2608 | 0x94c,0x94d,0x94e,0x94f,0x950 | Dispel | spell_mod_dispel.cpp | 0x36: i9a, iNb, s9a |
| 8e03dba0da | 2312 | 0x951,0x952,0x953 | Delay | spell_mod_delay.cpp | 0x38: eD, i9b, iLv |
| cfbae89db2 | 3292 | 0x954,0x955,0x956,0x957,0x958 | Angel Feather | spell_mod_angel_feather.cpp | 0x3b: i9b, iCv |
| efdb20dc3f | 1636 | 0x959,0x95a,0x95b,0x95c,0x95d | Curse | spell_mod_curse.cpp | 0x3d: i11b, iNb |
| 2a4716a80a | 1524 | 0x95e,0x95f,0x960 | Bless | spell_mod_bless.cpp | 0x3e: i11b, iNb, s11 |
| d43a9b4c0f | 2056 | 0x961,0x962,0x963 | Neutral | spell_mod_neutral.cpp | 0x3f: i11b, iNb, s11 |
| b02674cbb8 | 3820 | 0x964,0x965,0x966 | Anti | spell_mod_anti.cpp | 0x2a: eE, i9a, iCv |
| ac59fc1380 | 4764 | 0x967,0x968,0x969,0x96a | Meteor Swarm | spell_mod_meteor_swarm.cpp | 0x13: eB, i11b |
| dfd43383ca | 4116 | 0x96b,0x96c,0x96d,0x96e,0x96f,0x970 | Southern Cross | spell_mod_southern_cross.cpp | 0x14: eB, i11b |
| a6a642e880 | 3493 | 0x971,0x972,0x973 | Daemon's Gate / 'The Scream' | spell_mod_daemons_gate.cpp | 0x24: eC, i10b, m430 Weirddevil (enemy agent); 0x41: x9 |
| 43d5518828 | 6128 | 0x974,0x975,0x976,0x977,0x978,0x979,0x97a,0x97b | Earthquake | spell_mod_earthquake.cpp | 0x19: eF, i12 |
| 02571c0499 | 2884 | 0x97c,0x97d | Extinction | spell_mod_extinction.cpp | 0x25: eD, i10b |
| c5209066cb | 2908 | 0x97e | Mind Blast | spell_mod_mind_blast.cpp | 0x43: m537 Vesper (enemy agent) |
| c325548547 | 5136 | 0x97f | Spicule | spell_mod_spicule.cpp | 0x44: m537 Decus (enemy agent) |
| 155b3550d0 | 7771 | 0x980 | Wind of Destruction | spell_mod_wind_of_destruction.cpp | 0x45: m538 Cyril (enemy agent) |
| 476afdf4ce | 8480 | 0x981 | Divine Comedy | spell_mod_divine_comedy.cpp | 0x46: t540 Indalecio (enemy agent; exact through the first spell, then the console slows) |

## The party class runs (2026-10-23)

Encounter 340, the wizard passive; the caster is the class's character with every list spell
learned, casting each when idle (sweep_cfg.py). Samples every 10 battle frames.

| Run | Caster | RNG | Enemy / party samples | Slow frames | Casts (frame, finish) |
|---|---|---|---|---|---|
| iLv | Leon (v2) | 290/290 | 290/290, 288/288 | 86 | Wounds 494, Word of Death 606, Ice Needle 684, Deep Mist 810-1052, Haste 1208-1498, Delay 1654-1944, Growth 2100-2345, Protection 2471-2738 |
| iCv | Celine (v3) | 277/280: b910 +37 (3-tick run 908-920, Ray), b2300 +84 (2298-2301, Angel Feather), one more within a frame; the next sample agrees | 280/280, 278/278 | 150 | Firebolt 474, Thunderbolt 560, Wind Blade 640, Ray 776-1107, Starlight 1233-1447, Anti 1603-2017, Angel Feather 2203-2663 |
| iNa | Noel (v4) | 254/255: b850 +23 (4-tick run 849-853, Energy Arrow); the next sample agrees | 255/255, 253/253 | 13 | Wind Blade 478, Grave 564, Energy Arrow 710-946, Mind Absorber 1092-1204, Heal 1286, Cure Light 1378, Fairy Light 1524-1944, Cure All 2060-2396 |
| iNb | Noel (v4) | 280/280 | 280/280, 278/278 | 0 | Fairy Heal 514-866, Silence 972-1138, Dispel 1264-1466, Curse 1622-1894, Bless 2030-2298, Neutral 2434-2668 |

Cast and finish frames are the same in Redux and the port in all four. The Shot spells (no
SpellCast) end inside the samples.

## Open

- none
