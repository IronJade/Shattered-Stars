# Reference: Enemies

Every enemy type in the encounter packs, with the stats every battle gives it. The type number is what `so2_mod.py enemy` takes (the name works too) with a field name: the column names below (`hp`, `mp`, `exp`, `fol`, `level`, `atk`, `def`, `drop1`, `drop2`, `drop1_odds`, `drop2_odds`, ...):

```
python3 tools/so2_mod.py enemy DISC.cue TYPE FIELD VALUE
python3 tools/so2_mod.py enemy DISC.cue Armedknight drop1 "Blackberry"
```

A drop is an item and its odds. *Where* lists the areas of the encounters it is in (random encounters, scripted battles, the world maps); the encounter numbers and every map are in [`enemies.csv`](https://github.com/IronJade/Shattered-Stars/blob/main/docs/modding/reference/enemies.csv). Two types can share a name: use the number. 187 types.

| Type | Name | Level | HP | MP | Atk | Def | Exp | Fol | Drop 1 | Drop 2 | Boss | Where |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 | Armedknight | 7 | 180 | 0 | 90 | 5 | 40 | 25 | 0x2f5 Blueberry (15) |  |  | Cross Cave; Town of Lacour |
| 2 | Gladiator | 20 | 2000 | 0 | 310 | 2 | 500 | 10 |  |  |  | Lacour Castle |
| 3 | Sargwen | 28 | 850 | 0 | 310 | 2 | 580 | 130 | 0x1ed Walloon Sword (15) |  |  | Lasguss Mountain |
| 4 | Landworm | 6 | 200 | 0 | 95 | 8 | 35 | 40 | 0x102 Cure Poison (15) |  |  | Cross Cave |
| 5 | Sandworm | 20 | 1000 | 0 | 360 | 5 | 50 | 210 | 0x1a7 Orichalcum (60) | 0x1aa Damascus (100) |  |  |
| 6 | Bloodworm | 12 | 600 | 0 | 150 | 10 | 125 | 100 | 0x1be Killer Poison (15) |  |  | Expel world map; Mars Village |
| 7 | Guarder | 37 | 200 | 0 | 350 | 10 | 1000 | 410 | 0x2cb Fine Shield (8) | 0x1a7 Orichalcum (10) |  | Hoffmann Ruins |
| 8 | Shielder | 20 | 1400 | 0 | 350 | 10 | 410 | 800 |  |  |  | Lacour Castle; Secret Charity/Fake Gallery |
| 9 | Defender | 42 | 800 | 0 | 590 | 10 | 1820 | 410 | 0x2d6 Barrier Shield (10) |  |  | Eluria Tower |
| 10 | Vopalbunny | 4 | 120 | 0 | 69 | 0 | 16 | 25 | 0x184 Baby Rabbit Risotto (15) |  |  | Salva Drift |
| 11 | Killerrabi | 35 | 2000 | 0 | 420 | 80 | 1600 | 260 | 0x184 Baby Rabbit Risotto (15) |  |  | Sanctuary of Linga |
| 12 | Saberbunny | 73 | 18600 | 0 | 935 | 50 | 4000 | 5000 | 0x184 Baby Rabbit Risotto (15) |  |  |  |
| 13 | Alraune | 8 | 120 | 0 | 92 | 7 | 38 | 34 | 0xdc Mandrake (15) | 0xdd Rose Hips (15) |  | Cross Cave |
| 14 | Shadowflower | 30 | 1200 | 0 | 320 | 10 | 151 | 220 | 0xdc Mandrake (60) | 0xde Artemis Leaf (120) |  |  |
| 15 | Mandrake | 35 | 800 | 0 | 360 | 10 | 880 | 280 | 0xdc Mandrake (15) | 0xdf Wolfsbane (16) |  | Sanctuary of Linga |
| 16 | Otif | 58 | 8000 | 0 | 880 | 20 | 3650 | 800 | 0x46 Poison Check (10) | 0x102 Cure Poison (25) |  | Field of Courage |
| 17 | Gerel | 19 | 600 | 0 | 220 | 10 | 380 | 120 | 0x102 Cure Poison (20) |  |  | Energy Nede world map; Expel world map; Salva Drift; Shingo Forest |
| 18 | Petrogerell | 26 | 656 | 0 | 290 | 0 | 600 | 360 | 0x302 Cure Stone (21) |  |  | Mountain Palace |
| 19 | Kobold | 5 | 65 | 0 | 58 | 1 | 15 | 30 | 0xdd Rose Hips (15) |  |  | Salva Drift |
| 20 | Bugbear | 14 | 600 | 0 | 170 | 5 | 130 | 90 | 0x1a2 Pet Food (13) |  |  | Expel world map; Mars Village |
| 21 | Koboldking | 28 | 800 | 0 | 300 | 10 | 600 | 150 | 0x140 Sirloin Steak (10) | 0x183 Steak (16) |  | Lasguss Mountain |
| 22 | FunnyThief | 1 | 3 | 0 | 48 | 0 | 6 | 10 | 0x36 Spectacles (15) | 0x44 Useless Decoration (20) | yes | Cave of Trials; Energy Nede world map; Mountain Palace; Salva Drift; Shingo Forest |
| 23 | Bandit | 15 | 500 | 0 | 150 | 10 | 300 | 360 | 0x1a7 Orichalcum (120) | 0x1ae Sapphire (120) |  | Mars Village |
| 24 | Lessassassin | 40 | 6000 | 0 | 340 | 10 | 700 | 10000 | 0x2bc Core Plate (100) | 0x2ef Neo Greaves (200) |  | Town of Herlie |
| 25 | Eldermagius | 45 | 800 | 50 | 500 | 10 | 2000 | 510 | 0x29f Hermit's Helm (10) |  |  | Eluria Tower |
| 26 | Pilesherry | 29 | 500 | 40 | 280 | 10 | 590 | 200 | 0x74 Flare Ring (10) | 0x1af Ruby (20) |  | Lasguss Mountain |
| 27 | Magius | 14 | 20 | 50 | 70 | 0 | 150 | 120 | 0x88 Star Earring (0) |  |  | Expel world map; Mars Village; Salva Drift |
| 28 | Doomaxe | 35 | 3200 | 0 | 400 | 5 | 1020 | 310 | 0x2ac Brigandine (12) | 0x1b0 Star Ruby (8) |  | Hoffmann Ruins |
| 29 | Lizardaxe | 3 | 60 | 0 | 56 | 0 | 10 | 18 | 0x2f5 Blueberry (15) | 0x4b Necklace (10) |  | Salva Drift; Shingo Forest |
| 30 | Robberaxe | 12 | 400 | 0 | 156 | 12 | 130 | 105 | 0x102 Cure Poison (20) |  |  | Expel world map; Mars Village |
| 31 | Slime | 9 | 200 | 0 | 95 | 6 | 45 | 40 | 0x195 Slime Jelly (5) |  |  | Cross Cave |
| 32 | Hood | 25 | 850 | 0 | 280 | 5 | 280 | 147 | 0x186 Gelatin Steak (5) |  |  | Energy Nede world map; Mountain Palace |
| 33 | Blackslime | 37 | 3600 | 0 | 400 | 40 | 1100 | 350 | 0x195 Slime Jelly (5) |  |  | Hoffmann Ruins |
| 34 | XINE | 35 | 20000 | 300 | 320 | 0 | 8300 | 3200 | 0x56 Luna Talisman (127) | 0x45 Luna Tablet (255) | yes | Lasguss Mountain |
| 35 | Arcmene | 55 | 40000 | 0 | 1080 | 80 | 39000 | 26000 | 0x2b8 Steel Armor (100) | 0x3d Reverse Doll (255) | yes | Cave of Red Crystal |
| 36 | Scewer | 20 | 880 | 0 | 260 | 10 | 520 | 337 | 0xec Paralysis Oil (15) |  |  | Energy Nede world map; Salva Drift |
| 37 | Darthwidow | 45 | 6700 | 0 | 650 | 50 | 3000 | 1000 | 0x2c3 Chaos Mail (15) |  |  | Eluria Tower |
| 38 | Carlaeagle | 15 | 800 | 0 | 167 | 10 | 300 | 260 | 0x7e Golden Earring (15) | 0x2f5 Blueberry (20) |  | Expel world map; Mars Village |
| 39 | Cockatrice | 30 | 1200 | 0 | 270 | 5 | 1000 | 250 | 0x302 Cure Stone (20) |  |  | Lasguss Mountain |
| 40 | Periton | 51 | 12500 | 0 | 745 | 30 | 2800 | 50 | 0x76 Prism Ring (10) | 0x1a8 Meteorite (10) |  | Cave of Red Crystal |
| 41 | Sandglass | 20 | 780 | 0 | 250 | 0 | 400 | 260 | 0xee Fairy Glass (12) |  |  | Energy Nede world map; Expel world map; Mountain Palace; Salva Drift |
| 42 | Timekeeper | 42 | 3500 | 0 | 635 | 5 | 1800 | 480 | 0xee Fairy Glass (11) |  |  | Eluria Tower |
| 43 | Rolesher | 32 | 3000 | 0 | 400 | 5 | 250 | 150 | 0xee Fairy Glass (65) | 0x1aa Damascus (100) |  |  |
| 44 | Controller | 55 | 8000 | 0 | 880 | 5 | 3100 | 550 | 0x1d6 Smith's Hammer (20) | 0x1d9 Vellum Paper (30) |  | Field of Intelligence |
| 45 | Controlkey | 70 | 14000 | 0 | 1200 | 50 | 6200 | 2000 |  |  |  | Field of Love |
| 46 | PADmaster | 49 | 6500 | 0 | 605 | 5 | 2100 | 50 | 0x41 Frog (90) |  |  |  |
| 47 | Hounddog | 23 | 750 | 0 | 286 | 0 | 310 | 130 | 0x183 Steak (15) |  |  | Mountain Palace |
| 48 | Blackhound | 33 | 1100 | 0 | 355 | 20 | 940 | 288 | 0x183 Steak (15) |  |  | Sanctuary of Linga |
| 49 | Hellhound | 48 | 8300 | 0 | 850 | 0 | 2300 | 654 | 0x7a Fire Ring (10) | 0x35 Conductor's Baton (20) |  | Cave of Red Crystal |
| 50 | Ooze | 33 | 2300 | 0 | 390 | 5 | 850 | 350 | 0x16b Soda-Pop (5) |  |  | Sanctuary of Linga |
| 51 | Evilwater | 63 | 40000 | 0 | 1500 | 5 | 6600 | 50 | 0x315 Slippery Slime (5) |  |  | Mihne Cavern |
| 52 | Slimepool | 23 | 1600 | 0 | 286 | 20 | 420 | 134 | 0x315 Slippery Slime (5) |  |  | Mountain Palace |
| 53 | NiquiaHG | 13 | 1300 | 50 | 110 | 5 | 1000 | 50 | 0x1b7 Magical Camera (30) |  |  | Field of Intelligence; Field of Love |
| 54 | NiquialM | 13 | 2300 | 50 | 110 | 5 | 2000 | 50 | 0x1b7 Magical Camera (30) |  |  | Fienal |
| 56 | Ericodus | 75 | 5500 | 50 | 921 | 5 | 9800 | 650 | 0x1b0 Star Ruby (0) |  |  | Fienal |
| 57 | Takicodos | 75 | 25500 | 50 | 1000 | 5 | 9000 | 50 | 0x302 Cure Stone (10) |  |  | Fienal |
| 59 | Stingray | 15 | 588 | 0 | 166 | 2 | 150 | 250 | 0x103 Cure Paralysis (15) |  |  | Expel world map; Mars Village |
| 60 | Flyingray | 30 | 1000 | 0 | 325 | 10 | 800 | 150 | 0x103 Cure Paralysis (60) |  |  | Lasguss Mountain |
| 61 | Raystinger | 50 | 4000 | 0 | 720 | 20 | 2600 | 660 | 0x103 Cure Paralysis (25) | 0xa4 Fountain Pen (30) |  | Cave of Red Crystal |
| 62 | Deathsaucer | 1 | 10 | 10 | 105 | 5 | 4 | 3 | 0x10d Aphrodisiac (5) | 0x1a1 Fruit Nectar (10) |  |  |
| 65 | Shynesslady | 32 | 1000 | 30 | 330 | 15 | 900 | 280 | 0x4b Necklace (15) | 0x55 Talisman (15) |  | Sanctuary of Linga |
| 66 | Beastmaster | 19 | 840 | 30 | 180 | 0 | 300 | 130 | 0x4b Necklace (15) | 0x4f Ruby Pendant (20) |  | Expel world map; Salva Drift |
| 68 | Werewolf | 18 | 750 | 0 | 220 | 6 | 270 | 130 | 0x102 Cure Poison (15) |  |  | Expel world map; Salva Drift |
| 69 | Wolfhead | 33 | 600 | 0 | 340 | 25 | 840 | 200 | 0x3e Silver Pendant (10) |  |  | Sanctuary of Linga |
| 71 | Miel32 | 140 | 190000 | 150 | 3500 | 300 | 400000 | 50000 |  |  | yes | Cave of Trials |
| 72 | Miel64 | 80 | 18000 | 0 | 1520 | 50 | 9900 | 2000 | 0xf3 Resurrection Mist (15) |  |  | Fienal |
| 73 | Miel128 | 190 | 59000 | 0 | 3400 | 45 | 70000 | 6000 |  |  |  | Cave of Trials |
| 74 | Ghark | 16 | 2000 | 0 | 10 | 10 | 10 | 300 |  |  |  | Shingo Forest |
| 75 | Troll | 27 | 2700 | 0 | 350 | 0 | 800 | 50 |  |  |  | Lacour Castle |
| 76 | Yety | 60 | 20000 | 0 | 120 | 100 | 6500 | 18000 | 0x1b0 Star Ruby (120) | 0x1b4 Rainbow Diamond (255) |  | Field of Power |
| 77 | Mountsnow | 76 | 20700 | 0 | 1400 | 50 | 3000 | 50 | 0x1a7 Orichalcum (50) | 0x1a8 Meteorite (80) |  |  |
| 78 | Goathead | 45 | 3000 | 50 | 630 | 50 | 1900 | 450 | 0x1b1 Crystal (35) | 0x1b0 Star Ruby (15) |  | Eluria Tower |
| 79 | Harfainx | 40 | 36700 | 100 | 600 | 50 | 20000 | 5000 | 0x1b0 Star Ruby (255) |  | yes | Hoffmann Ruins |
| 80 | Breakwing | 75 | 30000 | 100 | 1100 | 100 | 7600 | 2020 | 0xa2 Moon Tiara (10) |  |  | Mihne Cavern; North City |
| 81 | Visseyer | 40 | 10000 | 100 | 500 | 100 | 4200 | 7000 | 0x1a7 Orichalcum (120) | 0x1aa Damascus (255) | yes | Sanctuary of Linga |
| 82 | Kidnier | 76 | 14700 | 100 | 1220 | 0 | 8500 | 1020 | 0x1ad Green Beryl (20) |  |  | Fienal |
| 83 | Riverside | 76 | 5200 | 100 | 1280 | 0 | 12000 | 50 | 0x1ae Sapphire (20) |  |  | Fienal |
| 85 | Magichand | 65 | 60000 | 100 | 1100 | 50 | 80000 | 82000 | 0x2d0 Rare Gauntlets (255) |  | yes | Field of Intelligence |
| 87 | Guardbox | 16 | 30000 | 100 | 120 | 700 | 200 | 50 | 0x2f2 Cinderella Glass (255) |  |  | Field of Intelligence |
| 88 | Magicbox | 16 | 30000 | 100 | 120 | 700 | 200 | 50 | 0xf3 Resurrection Mist (255) |  |  | Field of Intelligence |
| 89 | Workbox | 16 | 30000 | 100 | 120 | 700 | 200 | 50 | 0x3d Reverse Doll (255) |  |  | Field of Intelligence |
| 90 | Kitty | 5 | 60 | 0 | 450 | 0 | 4000 | 1200 | 0x3b Pretty Idol (30) | 0x3d Reverse Doll (200) |  | Hoffmann Ruins |
| 93 | Ricki | 55 | 10000 | 0 | 820 | 30 | 3500 | 850 | 0x7b Thunder Ring (10) | 0x1b8 Magical Film (20) |  | Field of Intelligence |
| 94 | Rikilo | 53 | 10000 | 0 | 820 | 0 | 3900 | 800 | 0x7b Thunder Ring (8) | 0x1bf Peep-peep Bomb (90) |  | Field of Courage |
| 95 | Rikiha | 66 | 15000 | 0 | 1150 | 0 | 5500 | 50 | 0x7b Thunder Ring (8) |  |  | Field of Love |
| 96 | Archer | 28 | 200 | 0 | 260 | 0 | 295 | 155 | 0xe2 Skanda Compress (15) |  |  | Energy Nede world map; Mountain Palace |
| 97 | Giantbow | 35 | 600 | 0 | 372 | 20 | 1000 | 320 | 0x3e Silver Pendant (16) |  |  | Hoffmann Ruins |
| 99 | Gelatincube | 17 | 460 | 0 | 250 | 40 | 160 | 130 | 0x186 Gelatin Steak (10) | 0x1aa Damascus (6) |  | Expel world map; Salva Drift |
| 100 | Gelatinfloat | 55 | 3000 | 0 | 800 | 0 | 4000 | 900 | 0x2f4 Blackberry (150) |  |  | Eluria Tower |
| 101 | Gelatinblock | 70 | 80000 | 0 | 1600 | 900 | 65000 | 6000 | 0x10e Pixie Cologne (50) | 0x2f4 Blackberry (150) |  | Field of Love |
| 102 | Snowman | 86 | 58700 | 0 | 1720 | 50 | 5200 | 32800 | 0x1b4 Rainbow Diamond (50) | 0x1ab Rune Metal (80) |  |  |
| 105 | Azamgil | 15 | 2000 | 0 | 50 | 0 | 200 | 550 |  |  | yes | Lacour Castle; Secret Charity/Fake Gallery |
| 107 | Atulatul | 65 | 5000 | 0 | 800 | 50 | 3800 | 50 | 0x49 Purple Mist (15) |  |  | Field of Power |
| 109 | Lover | 75 | 60000 | 0 | 1100 | 0 | 40000 | 34000 |  |  | yes | North City |
| 110 | Shout | 27 | 700 | 0 | 10 | 0 | 400 | 150 | 0x2f4 Blackberry (2) |  |  | Lasguss Mountain |
| 111 | Burst | 55 | 6000 | 0 | 120 | 0 | 4800 | 600 | 0x2f4 Blackberry (8) |  |  | Mihne Cavern |
| 112 | Bang | 48 | 4000 | 0 | 60 | 10 | 2400 | 1040 | 0x2f4 Blackberry (12) | 0x1 Magic Canvas (20) |  | Cave of Red Crystal |
| 113 | Salamander | 40 | 5000 | 0 | 461 | 60 | 6000 | 2000 | 0x3d Reverse Doll (12) |  |  | Hoffmann Ruins |
| 114 | Flarelizard | 30 | 5000 | 0 | 410 | 25 | 4000 | 8000 | 0x65 Reflection Ring (255) |  | yes | Mountain Palace |
| 115 | Coldlizard | 50 | 13000 | 0 | 580 | 25 | 13000 | 4000 | 0x73 Aqua Ring (15) | 0x2be Barrier Armor (0) |  | Eluria Tower |
| 116 | Fenrilbeast | 60 | 25000 | 0 | 1000 | 60 | 22000 | 1500 | 0x6e Meteor Ring (15) | 0x1ab Rune Metal (20) |  | Field of Courage |
| 117 | Gargoyle | 18 | 1500 | 0 | 105 | 0 | 500 | 850 | 0x3d Reverse Doll (140) |  |  | Cross Cave |
| 118 | Stonestatue | 30 | 2200 | 0 | 420 | 0 | 200 | 50 |  |  |  | Eluria Tower; Ship to El |
| 119 | Alen-Tax | 18 | 400 | 0 | 40 | 0 | 150 | 500 |  |  | yes | Salva Drift |
| 120 | Varmillion | 18 | 3000 | 0 | 160 | 10 | 1000 | 1000 | 0x55 Talisman (255) |  | yes | Mars Village |
| 121 | Darkcrusader | 57 | 10000 | 0 | 920 | 20 | 4500 | 1000 | 0x2c4 Bloody Armor (13) |  |  | Field of Courage; Field of Power; Fienal |
| 122 | Insaneload | 60 | 8000 | 0 | 960 | 20 | 4950 | 1200 | 0x2a1 Bloody Helm (13) | 0x2c3 Chaos Mail (20) |  | Field of Power |
| 123 | Weirdknight | 100 | 23000 | 60 | 1900 | 50 | 11000 | 1200 |  |  |  | Cave of Trials |
| 124 | Lastavenger | 160 | 26500 | 200 | 2650 | 100 | 48000 | 3800 |  |  |  | Cave of Trials |
| 125 | Livingarmor | 100 | 11800 | 0 | 1950 | 10 | 12000 | 910 |  |  |  | Cave of Trials |
| 126 | Reflectguard | 55 | 3000 | 0 | 1000 | 10 | 3300 | 10 | 0x2d3 Mithril Shield (10) | 0x1a5 Silver (15) |  | Field of Intelligence; Field of Love |
| 127 | Starguarder | 170 | 28650 | 0 | 2700 | 10 | 50000 | 3500 | 0x2cf Star Guard (3) |  |  | Cave of Trials |
| 128 | Punkponk | 130 | 37000 | 0 | 2150 | 10 | 31000 | 4000 |  |  |  | Cave of Trials |
| 131 | Bloodgerell | 120 | 60000 | 0 | 2300 | 10 | 25000 | 20000 |  |  | yes | Cave of Trials |
| 132 | MetalFunny | 1 | 300 | 0 | 1500 | 10000 | 131070 | 10000 |  |  |  | Cave of Trials |
| 133 | F-thiefL99 | 160 | 40000 | 0 | 2600 | 10 | 200000 | 20000 |  |  | yes | Cave of Trials |
| 134 | Warlock | 100 | 14500 | 530 | 2000 | 10 | 15000 | 2000 |  |  |  | Cave of Trials |
| 135 | Meigus | 160 | 17510 | 660 | 2000 | 10 | 48000 | 6200 |  |  |  | Cave of Trials |
| 136 | Dragonaxe | 140 | 26550 | 0 | 2700 | 85 | 30000 | 10 |  |  |  | Cave of Trials |
| 137 | Weirdaxe | 110 | 23500 | 50 | 2100 | 55 | 20000 | 1500 |  |  |  | Cave of Trials |
| 139 | Huntinggel | 110 | 20000 | 0 | 2100 | 30 | 10000 | 3000 |  |  |  | Cave of Trials |
| 140 | Hell-servant | 280 | 450000 | 600 | 4000 | 1550 | 600000 | 100000 |  |  |  | Cave of Trials |
| 141 | Weirdavia | 96 | 100000 | 300 | 1420 | 350 | 160000 | 30000 | 0x2b1 Flying Hawk Robes (10) |  |  | Fienal |
| 142 | Phoenix | 220 | 350000 | 6300 | 3250 | 50 | 1000000 | 500000 | 0x2c0 Valiant Mail (15) |  | yes | Cave of Trials |
| 143 | Ladyquimira | 140 | 27500 | 200 | 2500 | 40 | 36000 | 4000 |  |  |  | Cave of Trials |
| 144 | Gloomwing | 101 | 30000 | 0 | 1900 | 45 | 15000 | 1200 |  |  |  | Cave of Trials |
| 145 | Cokatricking | 180 | 29800 | 0 | 2800 | 85 | 60000 | 5000 |  |  |  | Cave of Trials |
| 146 | Dreamshade | 130 | 35000 | 0 | 2305 | 5 | 60000 | 30000 | 0x37 Magical Drops (80) |  | yes | Cave of Trials |
| 147 | Foriger | 170 | 28200 | 0 | 2780 | 5 | 51000 | 1800 |  |  |  | Cave of Trials |
| 148 | Clubgunner | 190 | 48500 | 250 | 2900 | 85 | 67000 | 4834 |  |  |  | Cave of Trials |
| 149 | Liveflayer | 195 | 48500 | 0 | 3200 | 85 | 67000 | 4000 |  |  |  | Cave of Trials |
| 150 | Mindflayer | 15 | 66500 | 0 | 2420 | 45 | 120000 | 2000 |  |  |  | Cave of Trials |
| 151 | Cavesting | 110 | 18000 | 100 | 2100 | 45 | 23000 | 1850 |  |  |  | Cave of Trials |
| 152 | Gloomsting | 140 | 22000 | 210 | 2405 | 45 | 30000 | 2250 |  |  |  | Cave of Trials |
| 153 | Giant | 116 | 29000 | 0 | 2200 | 60 | 20000 | 3200 |  |  |  | Cave of Trials |
| 154 | Atlus | 146 | 30000 | 0 | 2220 | 50 | 35200 | 2955 |  |  |  | Cave of Trials |
| 155 | Killergigant | 170 | 35000 | 0 | 2820 | 50 | 52000 | 4200 |  |  |  | Cave of Trials |
| 156 | Greatergoat | 142 | 29700 | 600 | 2620 | 10 | 36600 | 4080 |  |  |  | Cave of Trials |
| 157 | Weirdgoat | 120 | 24600 | 500 | 2000 | 50 | 27000 | 3500 |  |  |  | Cave of Trials |
| 158 | Ghastricgel | 200 | 39000 | 100 | 3112 | 150 | 105000 | 4260 |  |  |  | Cave of Trials |
| 159 | Weirdmalesk | 180 | 28000 | 100 | 2800 | 150 | 62500 | 5000 |  |  |  | Cave of Trials |
| 160 | E-beast | 180 | 450000 | 100 | 3000 | 400 | 300000 | 42000 |  |  | yes | Cave of Trials |
| 161 | Robinfake | 160 | 18200 | 0 | 2620 | 50 | 30000 | 1024 |  |  |  | Cave of Trials |
| 162 | Robinmaster | 180 | 22000 | 0 | 2700 | 50 | 50000 | 3500 |  |  |  | Cave of Trials |
| 163 | Meduslizzard | 78 | 38000 | 0 | 1450 | 460 | 74000 | 4600 | 0x2bf Sylvan Mail (5) |  |  | Fienal |
| 164 | Orbiterbeast | 165 | 84000 | 0 | 2660 | 160 | 120000 | 6000 |  |  |  | Cave of Trials |
| 165 | Weirdbeast | 99 | 811200 | 0 | 2600 | 10000 | 165252 | 52000 |  |  | yes | Cave of Trials |
| 166 | Lesserdevil | 118 | 21200 | 0 | 1900 | 50 | 27000 | 2450 |  |  |  | Cave of Trials |
| 167 | Ogre | 30 | 4200 | 0 | 350 | 10 | 1000 | 850 |  |  |  | Lacour Castle |
| 168 | Ghast | 60 | 20000 | 0 | 780 | 30 | 4800 | 50 | 0x47 Paralysis Check (15) | 0x103 Cure Paralysis (28) |  | Field of Power |
| 169 | Brigandogre | 180 | 36200 | 0 | 2815 | 50 | 60200 | 3200 |  |  |  | Cave of Trials |
| 170 | Ghost | 48 | 8200 | 100 | 420 | 0 | 5200 | 8500 |  |  | yes | Hoffmann Ruins |
| 172 | Succubus | 90 | 21000 | 100 | 1560 | 2 | 3000 | 2800 |  |  |  | Fienal |
| 173 | Nightmare | 30 | 9000 | 100 | 500 | 20 | 4000 | 2200 | 0x59 Healing Ring (255) |  | yes | Mountain Palace |
| 174 | D-feather | 110 | 160000 | 500 | 2500 | 50 | 350000 | 20000 |  |  | yes | Cave of Trials |
| 175 | G-Celesta | 255 | 1000000 | 20000 | 5000 | 1500 | 2200000 | 80000 | 0x16a Holy Sword Farwell (255) |  | yes | Cave of Trials |
| 176 | Dias | 35 | 4000 | 0 | 460 | 300 | 20000 | 60000 |  |  | yes | Lacour Castle |
| 178 | Iselia-queen | 500 | 3300000 | 20000 | 5800 | 1500 | 4000000 | 50000 | 0x165 Levantine Sword (28) | 0x181 Angel Armband (128) | yes |  |
| 179 | Wizard | 60 | 20000 | 200 | 900 | 20 | 4600 | 2000 | 0x2cc Rune Buckler (15) | 0x1a9 Mithril (50) |  | Field of Intelligence |
| 180 | Masterwizard | 80 | 35000 | 400 | 1300 | 20 | 13000 | 2200 | 0x7c Fairy Ring (15) | 0x9e Silver Earring (20) |  | Field of Love; Fienal |
| 181 | Soulmaster | 250 | 50000 | 1900 | 4000 | 2 | 200000 | 10 |  |  |  | Cave of Trials |
| 182 | Weird-mage | 200 | 45000 | 1700 | 3560 | 50 | 80000 | 10000 |  |  |  | Cave of Trials |
| 183 | Wisesorcerer | 300 | 700000 | 30000 | 5500 | 1500 | 1000000 | 300000 | 0x2c0 Valiant Mail (100) |  | yes | Cave of Trials |
| 184 | Zand | 40 | 6000 | 100 | 240 | 0 | 2000 | 10000 | 0x37 Magical Drops (255) |  |  | Zand's Mansion |
| 185 | Shin | 60 | 21000 | 200 | 900 | 200 | 45000 | 52000 | 0x78 Emerald Ring (255) |  | yes | Eluria Tower; Meeting Room; Ship to El |
| 186 | Hellmaster | 200 | 60000 | 3500 | 3100 | 850 | 180000 | 10 |  |  |  | Cave of Trials |
| 187 | Weirddevil | 170 | 27800 | 1400 | 2800 | 50 | 60000 | 6100 |  |  |  | Cave of Trials |
| 188 | Synard | 70 | 43000 | 200 | 800 | 100 | 80000 | 20000 | 0x9c Mind Ring (255) |  | yes | Synard Plant |
| 189 | D-tyrant | 300 | 550000 | 1000 | 3800 | 800 | 1800000 | 1200000 | 0x2d4 Valiant Guard (120) |  | yes | Cave of Trials |
| 190 | Guardian | 70 | 130000 | 0 | 1500 | 650 | 150000 | 64000 | 0x66 Protection Ring (255) |  | yes | Field of Courage; Field of Power |
| 191 | Geo-guardian | 200 | 400000 | 100 | 4500 | 1100 | 300000 | 100000 | 0x2d4 Valiant Guard (255) |  | yes | Cave of Trials |
| 192 | Owlbear | 200 | 300000 | 100 | 4000 | 1600 | 800000 | 100000 |  |  |  | Cave of Trials |
| 193 | Bark | 80 | 150000 | 400 | 4000 | 650 | 59800 | 55000 | 0x1a9 Mithril (255) |  | yes | Mihne Cavern |
| 194 | Rock-demon | 250 | 550000 | 500 | 8500 | 1550 | 950000 | 50000 | 0x146 Valkyrie's Garb (40) |  |  | Cave of Trials |
| 195 | M-eater | 170 | 600000 | 400 | 8500 | 850 | 242000 | 60000 |  |  | yes | Cave of Trials |
| 196 | Berle | 77 | 100000 | 100 | 950 | 100 | 69000 | 58000 | 0x104 Wonder Drug (255) |  | yes | Eluria Tower |
| 197 | Berle | 77 | 170000 | 100 | 1800 | 550 | 99000 | 58000 | 0xf0 Merlin Drink (255) |  | yes | Town of Armlock |
| 198 | Shigeo | 75 | 160000 | 100 | 1600 | 550 | 83000 | 54000 | 0x52 Pyre Tear (255) |  | yes | Fun City |
| 199 | Marsilio | 70 | 150000 | 100 | 1800 | 600 | 72000 | 50000 | 0xee Fairy Glass (255) |  | yes | Fienal; Fun City |
| 200 | Ruprecht | 72 | 105800 | 200 | 1200 | 50 | 28000 | 12000 | 0x3d Reverse Doll (255) |  | yes | Fienal |
| 201 | Nicolus | 68 | 45500 | 150 | 900 | 700 | 19000 | 22000 | 0x3d Reverse Doll (255) |  | yes | Fienal |
| 202 | Jibril | 77 | 85200 | 180 | 1300 | 50 | 25000 | 36000 | 0x3d Reverse Doll (255) |  | yes | Fienal |
| 203 | Vesper | 95 | 140000 | 200 | 1650 | 350 | 60000 | 20000 |  |  | yes | Fienal |
| 204 | Decus | 120 | 180000 | 300 | 1650 | 400 | 125000 | 45000 | 0x51 Fairy Tear (255) |  | yes | Fienal |
| 205 | Cyril | 140 | 300000 | 5900 | 2300 | 600 | 460000 | 80000 |  |  | yes | Fienal |
| 206 | Indalecio | 160 | 500000 | 10000 | 1900 | 300 | 600000 | 90000 |  |  | yes | Fienal |
| 207 | Indalecio | 240 | 1500000 | 16000 | 3400 | 1500 | 4900000 | 60000 |  |  | yes | Fienal |

---

Made by `tools/mod_reference_wiki.py` from the tables in [`docs/modding/reference`](https://github.com/IronJade/Shattered-Stars/blob/main/docs/modding/reference) (`tools/mod_reference.py`). IDs, names and figures only; the game's own text and pictures are in the [local reference](Modding-Reference#the-local-reference) you build from your disc.
