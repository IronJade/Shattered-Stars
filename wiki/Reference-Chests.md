# Reference: Chests

Every chest in the map scripts. A chest's ID is its opened flag (`flag`, also in decimal): the game sets it when the chest is opened. To change what one holds:

```
python3 tools/so2_mod.py chest DISC.cue MAP FLAG --item ITEM
python3 tools/so2_mod.py chest DISC.cue MAP FLAG --fol N
```

`MAP` and `ITEM` take a number or a name. A chest holding an item can only be changed to another item; one holding Fol takes either (`Can hold`). The same chest can be in several versions of a map (`Also on`): change each, or it holds the old contents there.

338 chest sites, 313 chests: [`chests.csv`](https://github.com/IronJade/Shattered-Stars/blob/main/docs/modding/reference/chests.csv).

| Map | Map name | Flag | Flag (dec) | Contents | Item | Can hold | Also on |
|---|---|---|---|---|---|---|---|
| 18 | Arlia Village: north: fountain square | 0x961 | 2401 | Leather Armor | 0x2a5 | items | 21 |
| 18 | Arlia Village: north: fountain square | 0x962 | 2402 | 200 Fol |  | items or Fol | 21 |
| 18 | Arlia Village: north: fountain square | 0x963 | 2403 | Resurrection Bottle | 0x115 | items | 21 |
| 21 | Arlia Village: north: fountain square, at night | 0x961 | 2401 | Leather Armor | 0x2a5 | items | 18 |
| 21 | Arlia Village: north: fountain square, at night | 0x962 | 2402 | 200 Fol |  | items or Fol | 18 |
| 21 | Arlia Village: north: fountain square, at night | 0x963 | 2403 | Resurrection Bottle | 0x115 | items | 18 |
| 25 | Arlia Village: Newlyweds' House, bedroom | 0x964 | 2404 | Blueberry | 0x2f5 | items |  |
| 25 | Arlia Village: Newlyweds' House, bedroom | 0x965 | 2405 | Rose Hips | 0xdd | items |  |
| 33 | Arlia Village: Carpenter's House | 0x966 | 2406 | Strawberry Jam | 0x2f6 | items |  |
| 38 | Arlia Village: Rena's Room | 0x960 | 2400 | Blackberry | 0x2f4 | items | 40 |
| 40 | Rena's Room: Rena's Room, at night | 0x960 | 2400 | Blackberry | 0x2f4 | items | 38 |
| 50 | Alen's Mansion: Alen's Mansion, bedroom | 0x967 | 2407 | Portrait B | 0x7 | items |  |
| 59 | Town of Salva: Jewelry Store Fairy Tear | 0x968 | 2408 | Heavy Ring | 0x5e | items |  |
| 64 | Salva Drift: mine tunnel, deeper | 0x969 | 2409 | Silver | 0x1a5 | items |  |
| 64 | Salva Drift: mine tunnel, deeper | 0x9c7 | 2503 | Blueberry | 0x2f5 | items |  |
| 65 | Salva Drift: interior room, about 1 door in from the entrance | 0x96c | 2412 | Blackberry | 0x2f4 | items |  |
| 65 | Salva Drift: interior room, about 1 door in from the entrance | 0x9cc | 2508 | Rose Hips | 0xdd | items |  |
| 66 | Salva Drift: interior room, about 2 doors in from the entrance | 0x9c8 | 2504 | Iron | 0x1a3 | items |  |
| 67 | Salva Drift: interior room, about 3 doors in from the entrance | 0x9c9 | 2505 | Gold | 0x1a4 | items |  |
| 67 | Salva Drift: interior room, about 3 doors in from the entrance | 0x9ca | 2506 | Maple Syrup | 0xfd | items |  |
| 68 | Salva Drift: interior room, about 3 doors in from the entrance | 0x96a | 2410 | Gold | 0x1a4 | items |  |
| 68 | Salva Drift: interior room, about 3 doors in from the entrance | 0x96b | 2411 | Spectacles | 0x36 | items |  |
| 68 | Salva Drift: interior room, about 3 doors in from the entrance | 0x9cb | 2507 | Blueberry | 0x2f5 | items |  |
| 69 | Salva Drift: interior room, about 2 doors in from the entrance | 0x96e | 2414 | Golden Earring | 0x7e | items |  |
| 69 | Salva Drift: interior room, about 2 doors in from the entrance | 0x96f | 2415 | Iron | 0x1a3 | items |  |
| 69 | Salva Drift: interior room, about 2 doors in from the entrance | 0x970 | 2416 | Aquaberry | 0x2f3 | items |  |
| 70 | Salva Drift: interior room, about 1 door in from the entrance | 0x9cd | 2509 | Cinderella Glass | 0x2f2 | items |  |
| 71 | Salva Drift: dark cave bridge, dead end off the tunnel | 0x971 | 2417 | Star Ruby | 0x1b0 | items |  |
| 71 | Salva Drift: dark cave bridge, dead end off the tunnel | 0x972 | 2418 | Diamond | 0x1b3 | items |  |
| 72 | Salva Drift: interior room, about 2 doors in from the entrance | 0x96d | 2413 | Brigandine | 0x2ac | items |  |
| 92 | Armory: Armory | 0x9fa | 2554 | Wooden Shield | 0x2c5 | items |  |
| 92 | Armory: Armory | 0x9fb | 2555 | Leather Helm | 0x28f | items |  |
| 100 | The Prince's Chambers: The Prince's Chambers | 0x9fc | 2556 | 500 Fol |  | items or Fol |  |
| 117 | Port of Clik: The House on the Hill | 0x9f6 | 2550 | Lyre | 0x32 | items |  |
| 119 | Port of Clik: Restaurant The Big Potato | 0x9f7 | 2551 | Seafood | 0x117 | items |  |
| 119 | Port of Clik: Restaurant The Big Potato | 0x9f8 | 2552 | Grain | 0x119 | items |  |
| 119 | Port of Clik: Restaurant The Big Potato | 0x9f9 | 2553 | Vegetables | 0x11b | items |  |
| 132 | Elder's House: Elder's House, study | 0x973 | 2419 | Silence Card | 0x1b | items |  |
| 132 | Elder's House: Elder's House, study | 0x974 | 2420 | Sour Syrup | 0xfb | items |  |
| 132 | Elder's House: Elder's House, study | 0x975 | 2421 | Purple Mist | 0x49 | items |  |
| 139 | Mars Village: Celine's Room | 0x976 | 2422 | Silk Robe | 0x2ab | items |  |
| 157 | Town of Herlie: Eleanor's House, upstairs | 0x97c | 2428 | Ring of Sadness | 0x68 | items |  |
| 163 | Zand's Mansion: parlor (checkerboard floor), front room | 0x97d | 2429 | Ringed Mail | 0x2aa | items |  |
| 163 | Zand's Mansion: parlor (checkerboard floor), front room | 0x97e | 2430 | Sinclair Sabre | 0x1eb | items |  |
| 166 | Zand's Mansion: storage room (barrels) | 0x97f | 2431 | Leather Boots | 0x2df | items |  |
| 166 | Zand's Mansion: storage room (barrels) | 0x980 | 2432 | 1200 Fol |  | items or Fol |  |
| 277 | Noel's House: Noel's House | 0xa36 | 2614 | Nature's Life Force | 0xaf | items |  |
| 277 | Noel's House: Noel's House | 0xa37 | 2615 | Pet Food | 0x1a2 | items |  |
| 277 | Noel's House: Noel's House | 0xa38 | 2616 | 50000 Fol |  | items or Fol |  |
| 332 | Giveaway: Nede History Research Lab | 0x9a4 | 2468 | Fairy Glass | 0xee | items |  |
| 351 | Mountain Palace: Ernest's path (east passageway, toward the Flarelizards): chest room (Cure Stone, Sweet Syrup) | 0x981 | 2433 | Cure Stone | 0x302 | items |  |
| 351 | Mountain Palace: Ernest's path (east passageway, toward the Flarelizards): chest room (Cure Stone, Sweet Syrup) | 0x982 | 2434 | Sweet Syrup | 0xfc | items |  |
| 355 | Mountain Palace: entrance (guard post) | 0x983 | 2435 | Luna Tablet | 0x45 | items |  |
| 355 | Mountain Palace: entrance (guard post) | 0x984 | 2436 | Crystal | 0x1b1 | items |  |
| 358 | Mountain Palace: Ashton's path (west archway, toward Nightmare): chest room (Fairy's Statue, Emerald Ring) | 0x985 | 2437 | Fairy's Statue | 0x23 | items |  |
| 358 | Mountain Palace: Ashton's path (west archway, toward Nightmare): chest room (Fairy's Statue, Emerald Ring) | 0x986 | 2438 | Emerald Ring | 0x78 | items |  |
| 367 | Mountain Palace: Ashton's path (west archway, toward Nightmare): chest room (Sour Syrup, Walloon Sword) | 0x987 | 2439 | Sour Syrup | 0xfb | items |  |
| 367 | Mountain Palace: Ashton's path (west archway, toward Nightmare): chest room (Sour Syrup, Walloon Sword) | 0x988 | 2440 | Walloon Sword | 0x1ed | items |  |
| 371 | Mountain Palace: Ernest's path (east passageway, toward the Flarelizards): chest room (Crest Rod, Amber Robe, ?MINERAL) | 0x989 | 2441 | Crest Rod | 0x23b | items |  |
| 371 | Mountain Palace: Ernest's path (east passageway, toward the Flarelizards): chest room (Crest Rod, Amber Robe, ?MINERAL) | 0x98a | 2442 | Amber Robe | 0x2ad | items |  |
| 371 | Mountain Palace: Ernest's path (east passageway, toward the Flarelizards): chest room (Crest Rod, Amber Robe, ?MINERAL) | 0x98b | 2443 | ?MINERAL | 0x33f | items |  |
| 373 | Mountain Palace: Ernest's path (east passageway, toward the Flarelizards): chest room (Damascus, Fairy's Cologne, Wolfsbane) | 0x9a3 | 2467 | Damascus | 0x1aa | items |  |
| 373 | Mountain Palace: Ernest's path (east passageway, toward the Flarelizards): chest room (Damascus, Fairy's Cologne, Wolfsbane) | 0x9ce | 2510 | Fairy's Cologne | 0xef | items |  |
| 373 | Mountain Palace: Ernest's path (east passageway, toward the Flarelizards): chest room (Damascus, Fairy's Cologne, Wolfsbane) | 0x9cf | 2511 | Wolfsbane | 0xdf | items |  |
| 375 | Mountain Palace: Ernest's path (east passageway, toward the Flarelizards): chest room (?HERB) | 0x98c | 2444 | ?HERB | 0x30a | items |  |
| 376 | Mountain Palace: Ernest's path (east passageway, toward the Flarelizards): chest room (Resurrection Bottle, ?JEWELRY) | 0x9d0 | 2512 | Resurrection Bottle | 0x115 | items |  |
| 376 | Mountain Palace: Ernest's path (east passageway, toward the Flarelizards): chest room (Resurrection Bottle, ?JEWELRY) | 0x9d1 | 2513 | ?JEWELRY | 0x3b4 | items |  |
| 380 | Mountain Palace: Ashton's path (west archway, toward Nightmare): boss room (Nightmare) | 0x98d | 2445 | Cestus | 0x21d | items |  |
| 380 | Mountain Palace: Ashton's path (west archway, toward Nightmare): boss room (Nightmare) | 0x98e | 2446 | ?MINERAL | 0x33c | items |  |
| 387 | Heraldry Weapons Laboratory: derelict quarters (piano, chest) | 0x9aa | 2474 | Mind Ring | 0x9c | items |  |
| 402 | Sanctuary of Linga: cave entrance (ledge fork) | 0xa0f | 2575 | Might Chain | 0x4c | items |  |
| 402 | Sanctuary of Linga: cave entrance (ledge fork) | 0xa16 | 2582 | Ruby | 0x1af | items |  |
| 403 | Sanctuary of Linga: carved stairway ledge (chest) | 0xa10 | 2576 | Mixed Syrup | 0x101 | items |  |
| 403 | Sanctuary of Linga: carved stairway ledge (chest) | 0xa17 | 2583 | Bubble Lotion | 0xeb | items |  |
| 404 | Sanctuary of Linga: winding crossover ledge (chest) | 0xa18 | 2584 | Rainbow Diamond | 0x1b4 | items |  |
| 408 | Sanctuary of Linga: stairway up (bone-strewn ledge) | 0xa19 | 2585 | Poison Check | 0x46 | items |  |
| 409 | Sanctuary of Linga: deep fork (final choice of paths) | 0xa11 | 2577 | Twin-edge | 0x20f | items |  |
| 409 | Sanctuary of Linga: deep fork (final choice of paths) | 0xa12 | 2578 | Straight Punch | 0x255 | items |  |
| 412 | Sanctuary of Linga: grassy chamber (two chests, skull) -- near the heart of the Sanctuary | 0xa13 | 2579 | ?GUARD | 0x383 | items |  |
| 412 | Sanctuary of Linga: grassy chamber (two chests, skull) -- near the heart of the Sanctuary | 0xa14 | 2580 | Cinderella Glass | 0x2f2 | items |  |
| 412 | Sanctuary of Linga: grassy chamber (two chests, skull) -- near the heart of the Sanctuary | 0xa15 | 2581 | Spring Water | 0x116 | items |  |
| 413 | Cave of Red Crystal: entrance / threshold area | 0xa39 | 2617 | Resurrection Bottle | 0x115 | items |  |
| 414 | Cave of Red Crystal: interior room, about 1 door in from the entrance | 0xa54 | 2644 | Cinderella Glass | 0x2f2 | items |  |
| 415 | Cave of Red Crystal: interior room, about 2 doors in from the entrance | 0xa3a | 2618 | Flying Hawk Robes | 0x2b1 | items |  |
| 416 | Cave of Red Crystal: interior room, about 1 door in from the entrance | 0xa3b | 2619 | Heraldry | 0x276 | items |  |
| 416 | Cave of Red Crystal: interior room, about 1 door in from the entrance | 0xa3c | 2620 | Dream Bracelet | 0x98 | items |  |
| 416 | Cave of Red Crystal: interior room, about 1 door in from the entrance | 0xa55 | 2645 | ?MINERAL | 0x33b | items |  |
| 418 | Cave of Red Crystal: interior room, about 4 doors in from the entrance | 0xa3d | 2621 | ?MINERAL | 0x33d | items |  |
| 418 | Cave of Red Crystal: interior room, about 4 doors in from the entrance | 0xa3e | 2622 | Fruit Syrup | 0xfe | items |  |
| 429 | King's Room: King's Room, antechamber | 0xa0d | 2573 | 10000 Fol |  | items or Fol |  |
| 430 | Rosalia's Room: Rosalia's Room | 0xa0e | 2574 | Star Ruby | 0x1b0 | items |  |
| 449 | Mihne Cavern: entrance / threshold area | 0x9ab | 2475 | ?MINERAL | 0x33f | items |  |
| 449 | Mihne Cavern: entrance / threshold area | 0x9e5 | 2533 | Fruit Syrup | 0xfe | items |  |
| 450 | Mihne Cavern: interior room, about 1 door in from the entrance | 0x9ac | 2476 | Aceras | 0xe1 | items |  |
| 450 | Mihne Cavern: interior room, about 1 door in from the entrance | 0x9ad | 2477 | Dummy Doll | 0x25 | items |  |
| 450 | Mihne Cavern: interior room, about 1 door in from the entrance | 0x9ae | 2478 | Lightning Gun | 0x288 | items |  |
| 451 | Mihne Cavern: interior room, about 1 door in from the entrance | 0x9af | 2479 | Encyclopedia | 0x27c | items |  |
| 451 | Mihne Cavern: interior room, about 1 door in from the entrance | 0x9b0 | 2480 | Wonder Drug | 0x104 | items |  |
| 451 | Mihne Cavern: interior room, about 1 door in from the entrance | 0x9e6 | 2534 | Meteorite | 0x1a8 | items |  |
| 451 | Mihne Cavern: interior room, about 1 door in from the entrance | 0x9e7 | 2535 | Fresh Syrup | 0xff | items |  |
| 452 | Mihne Cavern: interior room, about 2 doors in from the entrance | 0x9b1 | 2481 | Mithril Mesh | 0x2bd | items |  |
| 452 | Mihne Cavern: interior room, about 2 doors in from the entrance | 0x9b2 | 2482 | Smith's Hammer | 0x1d6 | items |  |
| 452 | Mihne Cavern: interior room, about 2 doors in from the entrance | 0x9b3 | 2483 | Spring Water | 0x116 | items |  |
| 452 | Mihne Cavern: interior room, about 2 doors in from the entrance | 0x9e8 | 2536 | Smelling Salts | 0xe6 | items |  |
| 459 | Mars Village: Heraldry Forest, mossy hollow | 0x977 | 2423 | Smelling Salts | 0xe6 | items |  |
| 460 | Mars Village: Heraldry Forest, fallen-log crossing | 0x978 | 2424 | Dummy Doll | 0x25 | items |  |
| 461 | Mars Village: Heraldry Forest, path (monster den) | 0x979 | 2425 | Amber Robe | 0x2ad | items |  |
| 461 | Mars Village: Heraldry Forest, path (monster den) | 0x9e9 | 2537 | Flame Blade | 0x1ec | items |  |
| 462 | Mars Village: Heraldry Forest, clearing behind Celine's House | 0x97a | 2426 | Mandrake | 0xdc | items |  |
| 465 | Mars Village: Heraldry Forest | 0x97b | 2427 | ?HERB | 0x309 | items |  |
| 485 | Hoffmann Ruins: upper tunnels: chest room (Cure Stone) | 0x9d2 | 2514 | Cure Stone | 0x302 | items |  |
| 486 | Hoffmann Ruins: upper tunnels: chest room (Recoil Bracelet) | 0x98f | 2447 | Recoil Bracelet | 0x97 | items |  |
| 487 | Hoffmann Ruins: upper tunnels: chest room (Angel Hair, Light Box) | 0x990 | 2448 | Angel Hair | 0x93 | items |  |
| 487 | Hoffmann Ruins: upper tunnels: chest room (Angel Hair, Light Box) | 0x991 | 2449 | Light Box | 0x248 | items |  |
| 490 | Hoffmann Ruins: upper tunnels: chest room (Green Beryl, Holy Mist) | 0x992 | 2450 | Green Beryl | 0x1ad | items |  |
| 490 | Hoffmann Ruins: upper tunnels: chest room (Green Beryl, Holy Mist) | 0x993 | 2451 | Holy Mist | 0x113 | items |  |
| 491 | Hoffmann Ruins: mine-cart tunnels: chest room (Cure Paralysis) | 0x9d3 | 2515 | Cure Paralysis | 0x103 | items |  |
| 492 | Hoffmann Ruins: mine-cart tunnels: chest room (Aquaberry) | 0x9d4 | 2516 | Aquaberry | 0x2f3 | items | 493 |
| 493 | Hoffmann Ruins: mine-cart tunnels, about 6 doors in from the entrance | 0x9d4 | 2516 | Aquaberry | 0x2f3 | items | 492 |
| 495 | Hoffmann Ruins: mine-cart tunnels: chest room (Dictionary, Cure Paralysis) | 0x994 | 2452 | Dictionary | 0x277 | items | 496 |
| 495 | Hoffmann Ruins: mine-cart tunnels: chest room (Dictionary, Cure Paralysis) | 0x995 | 2453 | Cure Paralysis | 0x103 | items | 496 |
| 496 | Hoffmann Ruins: mine-cart tunnels, about 7 doors in from the entrance | 0x994 | 2452 | Dictionary | 0x277 | items | 495 |
| 496 | Hoffmann Ruins: mine-cart tunnels, about 7 doors in from the entrance | 0x995 | 2453 | Cure Paralysis | 0x103 | items | 495 |
| 498 | Hoffmann Ruins: mine-cart tunnels: chest room (Hexagram Card) | 0x996 | 2454 | Hexagram Card | 0x1c | items |  |
| 502 | Hoffmann Ruins: mine-cart tunnels: chest room (1-up Pudding, Zephyr Earring, Gold, Orichalcum) | 0x997 | 2455 | 1-up Pudding | 0x330 | items |  |
| 502 | Hoffmann Ruins: mine-cart tunnels: chest room (1-up Pudding, Zephyr Earring, Gold, Orichalcum) | 0x998 | 2456 | Zephyr Earring | 0x82 | items |  |
| 502 | Hoffmann Ruins: mine-cart tunnels: chest room (1-up Pudding, Zephyr Earring, Gold, Orichalcum) | 0x9d5 | 2517 | Gold | 0x1a4 | items |  |
| 502 | Hoffmann Ruins: mine-cart tunnels: chest room (1-up Pudding, Zephyr Earring, Gold, Orichalcum) | 0x9d6 | 2518 | Orichalcum | 0x1a7 | items |  |
| 503 | Hoffmann Ruins: mine-cart tunnels: chest room (Hyperball) | 0x999 | 2457 | Hyperball | 0x20 | items | 504 |
| 504 | Hoffmann Ruins: mine-cart tunnels, about 9 doors in from the entrance | 0x999 | 2457 | Hyperball | 0x20 | items | 503 |
| 506 | Hoffmann Ruins: mine-cart tunnels: chest room (Spring Water, Diamond) | 0x9d7 | 2519 | Spring Water | 0x116 | items |  |
| 506 | Hoffmann Ruins: mine-cart tunnels: chest room (Spring Water, Diamond) | 0x9d8 | 2520 | Diamond | 0x1b3 | items |  |
| 507 | Hoffmann Ruins: mine-cart tunnels: chest room (Hot Syrup, Peep Half, Fruit Syrup) | 0x99a | 2458 | Hot Syrup | 0x100 | items | 508 |
| 507 | Hoffmann Ruins: mine-cart tunnels: chest room (Hot Syrup, Peep Half, Fruit Syrup) | 0x99b | 2459 | Peep Half | 0x6b | items | 508 |
| 507 | Hoffmann Ruins: mine-cart tunnels: chest room (Hot Syrup, Peep Half, Fruit Syrup) | 0x9d9 | 2521 | Fruit Syrup | 0xfe | items | 508 |
| 508 | Hoffmann Ruins: mine-cart tunnels, about 11 doors in from the entrance | 0x99a | 2458 | Hot Syrup | 0x100 | items | 507 |
| 508 | Hoffmann Ruins: mine-cart tunnels, about 11 doors in from the entrance | 0x99b | 2459 | Walloon Sword | 0x1ed | items | 507 |
| 508 | Hoffmann Ruins: mine-cart tunnels, about 11 doors in from the entrance | 0x9d9 | 2521 | Fruit Syrup | 0xfe | items | 507 |
| 510 | Hoffmann Ruins: mine-cart tunnels: chest room (Material Kit, Peep-peep Bomb) | 0x99c | 2460 | Material Kit | 0x1c8 | items |  |
| 510 | Hoffmann Ruins: mine-cart tunnels: chest room (Material Kit, Peep-peep Bomb) | 0x99d | 2461 | Peep-peep Bomb | 0x1bf | items |  |
| 513 | Hoffmann Ruins: lower passages: chest room (Green Beryl, Blackberry) | 0x9da | 2522 | Green Beryl | 0x1ad | items |  |
| 513 | Hoffmann Ruins: lower passages: chest room (Green Beryl, Blackberry) | 0x9db | 2523 | Blackberry | 0x2f4 | items |  |
| 514 | Hoffmann Ruins: lower passages: chest room (Sturm Ring) | 0x99e | 2462 | Sturm Ring | 0x3f | items |  |
| 515 | Hoffmann Ruins: lower passages: chest room ('Spring') | 0x99f | 2463 | 'Spring' | 0x2 | items |  |
| 517 | Hoffmann Ruins: lower passages: chest room (Paralysis Check) | 0x9a0 | 2464 | Paralysis Check | 0x47 | items | 518 |
| 518 | Hoffmann Ruins: lower passages, about 9 doors in from the entrance | 0x9a0 | 2464 | Paralysis Check | 0x47 | items | 517 |
| 520 | Hoffmann Ruins: lower passages: chest room (Sour Syrup, ?HERB) | 0x9a1 | 2465 | Sour Syrup | 0xfb | items | 521 |
| 520 | Hoffmann Ruins: lower passages: chest room (Sour Syrup, ?HERB) | 0x9a2 | 2466 | ?HERB | 0x30a | items | 521 |
| 521 | Hoffmann Ruins: lower passages, about 10 doors in from the entrance | 0x9a1 | 2465 | Sour Syrup | 0xfb | items | 520 |
| 521 | Hoffmann Ruins: lower passages, about 10 doors in from the entrance | 0x9a2 | 2466 | ?HERB | 0x30a | items | 520 |
| 526 | Cross Cave: passage (single chest) | 0xa1a | 2586 | Blueberry | 0x2f5 | items |  |
| 528 | Cross Cave: pool alcove (chest) | 0xa1b | 2587 | Iron | 0x1a3 | items |  |
| 528 | Cross Cave: pool alcove (chest) | 0xa1c | 2588 | Blackberry | 0x2f4 | items |  |
| 529 | Cross Cave: treasure nook (four chests, skeleton) | 0xa1d | 2589 | Cure Poison | 0x102 | items |  |
| 531 | Cross Cave: passage (distant chest) | 0x9fd | 2557 | Artemis Leaf | 0xde | items |  |
| 531 | Cross Cave: passage (distant chest) | 0xa1e | 2590 | Sweet Syrup | 0xfc | items |  |
| 532 | Cross Cave: chest alcove (two chests) | 0x9fe | 2558 | ?JEWELRY | 0x390 | items |  |
| 532 | Cross Cave: chest alcove (two chests) | 0xa1f | 2591 | Lavender | 0xe0 | items |  |
| 532 | Cross Cave: chest alcove (two chests) | 0xa20 | 2592 | ?MINERAL | 0x341 | items |  |
| 535 | Cross Cave: chest hollow (two chests) | 0x9ff | 2559 | Magic Canvas | 0x1 | items |  |
| 535 | Cross Cave: chest hollow (two chests) | 0xa00 | 2560 | Tri-ball | 0x1f | items |  |
| 538 | Cross Cave: Ancient Writings marker (chest nearby) | 0xa21 | 2593 | ?ITEM | 0x3b9 | items |  |
| 540 | Cross Cave: Ancient Writings marker (fork) | 0xa01 | 2561 | Cure Poison | 0x102 | items |  |
| 540 | Cross Cave: Ancient Writings marker (fork) | 0xa02 | 2562 | Blackberry | 0x2f4 | items |  |
| 540 | Cross Cave: Ancient Writings marker (fork) | 0xa03 | 2563 | Feather Pen | 0x2c | items |  |
| 540 | Cross Cave: Ancient Writings marker (fork) | 0xa04 | 2564 | 600 Fol |  | items or Fol |  |
| 540 | Cross Cave: Ancient Writings marker (fork) | 0xa05 | 2565 | Resurrection Mist | 0xf3 | items |  |
| 541 | Cross Cave: Ancient Writings marker (chest) | 0xa06 | 2566 | Lavender | 0xe0 | items |  |
| 543 | Cross Cave: chest chamber (ghost enemy) | 0xa07 | 2567 | Sweet Syrup | 0xfc | items |  |
| 543 | Cross Cave: chest chamber (ghost enemy) | 0xa08 | 2568 | Stink Gel | 0x10b | items |  |
| 547 | Cross Cave: innermost chamber (star-shaped dais, 5 chests) | 0xa09 | 2569 | item 0x2754 | 0x2754 | items |  |
| 547 | Cross Cave: innermost chamber (star-shaped dais, 5 chests) | 0xa0a | 2570 | Heart Barriers | 0xae | items |  |
| 547 | Cross Cave: innermost chamber (star-shaped dais, 5 chests) | 0xa0b | 2571 | Green Beryl | 0x1ad | items |  |
| 547 | Cross Cave: innermost chamber (star-shaped dais, 5 chests) | 0xa0c | 2572 | 700 Fol |  | items or Fol |  |
| 550 | Field of Intelligence: Mirror Room (east) | 0x9a5 | 2469 | Alpha Box | 0x24c | items |  |
| 550 | Field of Intelligence: Mirror Room (east) | 0x9a6 | 2470 | Mirror of Wisdom | 0x2a | items |  |
| 551 | Field of Intelligence: main floor, transporter-block maze | 0x9a7 | 2471 | Great Punch | 0x25a | items |  |
| 551 | Field of Intelligence: main floor, transporter-block maze | 0x9a8 | 2472 | Fairy Glass | 0xee | items |  |
| 551 | Field of Intelligence: main floor, transporter-block maze | 0x9a9 | 2473 | Jeanne's Helm | 0x29a | items |  |
| 551 | Field of Intelligence: main floor, transporter-block maze | 0x9dc | 2524 | Rune Metal | 0x1ab | items |  |
| 551 | Field of Intelligence: main floor, transporter-block maze | 0x9dd | 2525 | Fruit Syrup | 0xfe | items |  |
| 558 | Eluria Tower: entrance hall (red light, statue) | 0xa28 | 2600 | X Box | 0x24a | items |  |
| 558 | Eluria Tower: entrance hall (red light, statue) | 0xa29 | 2601 | Steel Helm | 0x299 | items |  |
| 558 | Eluria Tower: entrance hall (red light, statue) | 0xa4b | 2635 | Holy Mist | 0x113 | items |  |
| 558 | Eluria Tower: entrance hall (red light, statue) | 0xa4c | 2636 | Stone Check | 0x48 | items |  |
| 558 | Eluria Tower: entrance hall (red light, statue) | 0xa5f | 2655 | Meteor Ring | 0x6e | items |  |
| 559 | Eluria Tower: picture gallery (eerie frescoes) | 0xa4d | 2637 | Cure Paralysis | 0x103 | items |  |
| 559 | Eluria Tower: picture gallery (eerie frescoes) | 0xa4e | 2638 | Sapphire | 0x1ae | items |  |
| 559 | Eluria Tower: picture gallery (eerie frescoes) | 0xa4f | 2639 | Aquaberry | 0x2f3 | items |  |
| 561 | Eluria Tower: vine chamber (floating chests) | 0xa2a | 2602 | Trickster | 0x304 | items |  |
| 561 | Eluria Tower: vine chamber (floating chests) | 0xa2b | 2603 | Musical Software | 0x1c9 | items |  |
| 561 | Eluria Tower: vine chamber (floating chests) | 0xa2c | 2604 | Crestier Guard | 0x2cd | items |  |
| 562 | Eluria Tower: statue garden, crystal pool (1 of 6) | 0xa50 | 2640 | Cure Stone | 0x302 | items |  |
| 562 | Eluria Tower: statue garden, crystal pool (1 of 6) | 0xa51 | 2641 | Star Ruby | 0x1b0 | items |  |
| 564 | Eluria Tower: statue garden, crystal pool (3 of 6) | 0xa2d | 2605 | Ultra Punch | 0x259 | items |  |
| 564 | Eluria Tower: statue garden, crystal pool (3 of 6) | 0xa2e | 2606 | Crystal | 0x1b1 | items |  |
| 565 | Eluria Tower: statue garden, crystal pool (4 of 6) | 0xa2f | 2607 | Crestier Guard | 0x2cd | items |  |
| 565 | Eluria Tower: statue garden, crystal pool (4 of 6) | 0xa30 | 2608 | ?GUARD | 0x37e | items |  |
| 565 | Eluria Tower: statue garden, crystal pool (4 of 6) | 0xa31 | 2609 | Fresh Syrup | 0xff | items |  |
| 571 | Eluria Tower: chamber (chests, spirit enemy) | 0xa32 | 2610 | Splinter | 0x264 | items |  |
| 571 | Eluria Tower: chamber (chests, spirit enemy) | 0xa33 | 2611 | Spring Water | 0x116 | items |  |
| 572 | Eluria Tower: crystal chamber (chests) | 0xa34 | 2612 | Giant Fists | 0x229 | items |  |
| 572 | Eluria Tower: crystal chamber (chests) | 0xa35 | 2613 | Murasame Sword | 0x201 | items |  |
| 573 | Eluria Tower: statue hall (archways) | 0xa60 | 2656 | Gale Earring | 0x85 | items |  |
| 589 | Field of Love: star-platform walkway (entrance) | 0xa49 | 2633 | Serpent's Tooth | 0x231 | items |  |
| 593 | Field of Love: floating star platforms (rotating-bridge switch, 1 of 2) | 0xa4a | 2634 | Holy Rod | 0x23c | items | 594 |
| 593 | Field of Love: floating star platforms (rotating-bridge switch, 1 of 2) | 0xa52 | 2642 | Resurrection Mist | 0xf3 | items | 594 |
| 593 | Field of Love: floating star platforms (rotating-bridge switch, 1 of 2) | 0xa53 | 2643 | Hot Syrup | 0x100 | items | 594 |
| 594 | Field of Love: floating star platforms (rotating-bridge switch, 2 of 2) | 0xa4a | 2634 | Holy Rod | 0x23c | items | 593 |
| 594 | Field of Love: floating star platforms (rotating-bridge switch, 2 of 2) | 0xa52 | 2642 | Resurrection Mist | 0xf3 | items | 593 |
| 594 | Field of Love: floating star platforms (rotating-bridge switch, 2 of 2) | 0xa53 | 2643 | Hot Syrup | 0x100 | items | 593 |
| 598 | Field of Power: junction passage (ladder up) | 0xa3f | 2623 | Rune Full Moon | 0x224 | items |  |
| 601 | Field of Power: ladder climb | 0xa40 | 2624 | Atlas Ring | 0x5f | items |  |
| 607 | Field of Power: Yeti cavern, switch guard (A) | 0xa41 | 2625 | Marvel Sword | 0x1f5 | items | 608 |
| 608 | Field of Power: Yeti cavern, switch guard (A, far side) | 0xa41 | 2625 | Marvel Sword | 0x1f5 | items | 607 |
| 609 | Field of Power: cavern passage (item cache) | 0xa59 | 2649 | Blackberry | 0x2f4 | items | 610 |
| 610 | Field of Power: cavern passage (item cache, far side) | 0xa59 | 2649 | Blackberry | 0x2f4 | items | 609 |
| 611 | Field of Power: cavern passage (quiet) | 0xa42 | 2626 | ?GUARD | 0x374 | items | 612 |
| 612 | Field of Power: cavern passage (quiet, far side) | 0xa42 | 2626 | ?GUARD | 0x374 | items | 611 |
| 615 | Field of Power: Yeti cavern, switch guard (C) | 0xa43 | 2627 | Assault Bomb | 0x1c3 | items | 616 |
| 615 | Field of Power: Yeti cavern, switch guard (C) | 0xa44 | 2628 | Melting Lotion | 0xf2 | items | 616 |
| 615 | Field of Power: Yeti cavern, switch guard (C) | 0xa45 | 2629 | Jeanne's Armor | 0x2b7 | items | 616 |
| 616 | Field of Power: Yeti cavern, switch guard (C, far side) | 0xa43 | 2627 | Assault Bomb | 0x1c3 | items | 615 |
| 616 | Field of Power: Yeti cavern, switch guard (C, far side) | 0xa44 | 2628 | Melting Lotion | 0xf2 | items | 615 |
| 616 | Field of Power: Yeti cavern, switch guard (C, far side) | 0xa45 | 2629 | Jeanne's Armor | 0x2b7 | items | 615 |
| 630 | Field of Courage: crystal-pillar cavern (chest) | 0xa56 | 2646 | Spring Water | 0x116 | items |  |
| 632 | Field of Courage: cavern passage (junction) | 0xa57 | 2647 | Stone Check | 0x48 | items |  |
| 633 | Field of Courage: cavern passage | 0xa46 | 2630 | Jeanne's Shield | 0x2d1 | items |  |
| 635 | Field of Courage: cavern passage | 0xa47 | 2631 | Mithril Greaves | 0x2eb | items |  |
| 636 | Field of Courage: crystal-pillar cavern (chest, pool) | 0xa48 | 2632 | Scyther | 0x213 | items |  |
| 639 | Field of Courage: looped tunnel (chest) | 0xa58 | 2648 | Sour Syrup | 0xfb | items |  |
| 647 | Fienal: bonus treasure vault (many chests, near the lobby) | 0x9b5 | 2485 | Mithril Shield | 0x2d3 | items |  |
| 647 | Fienal: bonus treasure vault (many chests, near the lobby) | 0x9b6 | 2486 | Heraldry Book | 0x279 | items |  |
| 647 | Fienal: bonus treasure vault (many chests, near the lobby) | 0x9b7 | 2487 | Spark Whip | 0x26c | items |  |
| 647 | Fienal: bonus treasure vault (many chests, near the lobby) | 0x9b8 | 2488 | Nuclear Bomb | 0x1c7 | items |  |
| 647 | Fienal: bonus treasure vault (many chests, near the lobby) | 0x9b9 | 2489 | Left Cross | 0x91 | items |  |
| 647 | Fienal: bonus treasure vault (many chests, near the lobby) | 0x9bc | 2492 | Atomic Punch | 0x25d | items |  |
| 647 | Fienal: bonus treasure vault (many chests, near the lobby) | 0x9bd | 2493 | Rune Shoes | 0x2ea | items |  |
| 647 | Fienal: bonus treasure vault (many chests, near the lobby) | 0x9be | 2494 | Sylvan Boots | 0x2ee | items |  |
| 647 | Fienal: bonus treasure vault (many chests, near the lobby) | 0x9bf | 2495 | Mithril | 0x1a9 | items |  |
| 647 | Fienal: bonus treasure vault (many chests, near the lobby) | 0x9c0 | 2496 | Titan's Fists | 0x22c | items |  |
| 647 | Fienal: bonus treasure vault (many chests, near the lobby) | 0x9c1 | 2497 | Spark | 0x28b | items |  |
| 647 | Fienal: bonus treasure vault (many chests, near the lobby) | 0x9c2 | 2498 | Duel Suit | 0x2c1 | items |  |
| 647 | Fienal: bonus treasure vault (many chests, near the lobby) | 0x9c3 | 2499 | Goddess Statue | 0x24 | items |  |
| 647 | Fienal: bonus treasure vault (many chests, near the lobby) | 0x9c4 | 2500 | Cure Stone | 0x302 | items |  |
| 647 | Fienal: bonus treasure vault (many chests, near the lobby) | 0x9c5 | 2501 | Duel Helm | 0x29e | items |  |
| 647 | Fienal: bonus treasure vault (many chests, near the lobby) | 0x9c6 | 2502 | Sage's Stone | 0x1b2 | items |  |
| 647 | Fienal: bonus treasure vault (many chests, near the lobby) | 0x9de | 2526 | ?ITEM | 0x3b8 | items |  |
| 647 | Fienal: bonus treasure vault (many chests, near the lobby) | 0x9df | 2527 | Resurrection Mist | 0xf3 | items |  |
| 647 | Fienal: bonus treasure vault (many chests, near the lobby) | 0x9e3 | 2531 | Flash Pot | 0xf8 | items |  |
| 647 | Fienal: bonus treasure vault (many chests, near the lobby) | 0x9e4 | 2532 | Aceras | 0xe1 | items |  |
| 661 | Fienal: mid floors, toward Vesper & Decus (Floor 5): chest room (Lotus Eater, Beta Box) | 0x9ba | 2490 | Lotus Eater | 0x216 | items | 662 |
| 661 | Fienal: mid floors, toward Vesper & Decus (Floor 5): chest room (Lotus Eater, Beta Box) | 0x9bb | 2491 | Beta Box | 0x24f | items | 662 |
| 662 | Fienal: mid floors, toward Vesper & Decus (Floor 5), about 4 doors in from the lobby | 0x9ba | 2490 | Lotus Eater | 0x216 | items | 661 |
| 662 | Fienal: mid floors, toward Vesper & Decus (Floor 5), about 4 doors in from the lobby | 0x9bb | 2491 | Beta Box | 0x24f | items | 661 |
| 673 | Fienal: mid floors, toward Vesper & Decus (Floor 5): chest room (Spring Water, Pleiad Sword, Fresh Syrup) | 0x9e0 | 2528 | Spring Water | 0x116 | items |  |
| 673 | Fienal: mid floors, toward Vesper & Decus (Floor 5): chest room (Spring Water, Pleiad Sword, Fresh Syrup) | 0x9e1 | 2529 | Pleiad Sword | 0x207 | items |  |
| 673 | Fienal: mid floors, toward Vesper & Decus (Floor 5): chest room (Spring Water, Pleiad Sword, Fresh Syrup) | 0x9e2 | 2530 | Fresh Syrup | 0xff | items |  |
| 679 | Fienal: upper floors, toward Cyril (Floor 7): chest room (Clap Rod) | 0x9b4 | 2484 | Clap Rod | 0x243 | items |  |
| 703 | Cave of Trials: Level 1 (Heraldic Ruins): chest room (Fruit Nectar) | 0xa66 | 2662 | Fruit Nectar | 0x1a1 | items |  |
| 706 | Cave of Trials: Level 1 (Heraldic Ruins): interior room, about 2 doors in from the level entrance | 0xa67 | 2663 | item 0x276d | 0x276d | items |  |
| 707 | Cave of Trials: Level 1 (Heraldic Ruins): chest room (Juicy Beef, Wonder Drug) | 0xa63 | 2659 | Juicy Beef | 0x30f | items |  |
| 707 | Cave of Trials: Level 1 (Heraldic Ruins): chest room (Juicy Beef, Wonder Drug) | 0xa64 | 2660 | Wonder Drug | 0x104 | items |  |
| 709 | Cave of Trials: Level 1 (Heraldic Ruins): chest room (Tri-emplem) | 0xa65 | 2661 | Tri-emplem | 0x141 | items |  |
| 710 | Cave of Trials: bonus treasure vault (many chests, reachable from several levels) | 0xa62 | 2658 | Blueberry | 0x2f5 | items |  |
| 710 | Cave of Trials: bonus treasure vault (many chests, reachable from several levels) | 0xa6a | 2666 | Reflective Armor | 0x2c2 | items |  |
| 710 | Cave of Trials: bonus treasure vault (many chests, reachable from several levels) | 0xa6b | 2667 | Resurrection Mist | 0xf3 | items |  |
| 710 | Cave of Trials: bonus treasure vault (many chests, reachable from several levels) | 0xa75 | 2677 | Black Earring | 0x7d | items |  |
| 710 | Cave of Trials: bonus treasure vault (many chests, reachable from several levels) | 0xa76 | 2678 | Peep Non | 0x6c | items |  |
| 710 | Cave of Trials: bonus treasure vault (many chests, reachable from several levels) | 0xa79 | 2681 | Seraphic Garb | 0x16c | items |  |
| 710 | Cave of Trials: bonus treasure vault (many chests, reachable from several levels) | 0xa7c | 2684 | Wonder Drug | 0x104 | items |  |
| 710 | Cave of Trials: bonus treasure vault (many chests, reachable from several levels) | 0xa83 | 2691 | Peep Non | 0x6c | items |  |
| 710 | Cave of Trials: bonus treasure vault (many chests, reachable from several levels) | 0xa84 | 2692 | Purity Leaf | 0x30e | items |  |
| 710 | Cave of Trials: bonus treasure vault (many chests, reachable from several levels) | 0xa8e | 2702 | Special Tuna | 0x322 | items |  |
| 710 | Cave of Trials: bonus treasure vault (many chests, reachable from several levels) | 0xa8f | 2703 | Peep Non | 0x6c | items |  |
| 711 | Cave of Trials: Weird Beast arena (Puffy's trial; cross-linked, not on Level 1's own path) | 0xa68 | 2664 | Nuclear Bomb | 0x1c7 | items |  |
| 711 | Cave of Trials: Weird Beast arena (Puffy's trial; cross-linked, not on Level 1's own path) | 0xa81 | 2689 | Cure Poison | 0x102 | items |  |
| 712 | Cave of Trials: Level 1 (Heraldic Ruins): chest room (Dream Crown) | 0xa82 | 2690 | Dream Crown | 0xa1 | items |  |
| 713 | Cave of Trials: Level 1 (Heraldic Ruins): chest room (Wonder Drug, Fresh Syrup, Ganze Sea Urchin) | 0xa91 | 2705 | Wonder Drug | 0x104 | items |  |
| 713 | Cave of Trials: Level 1 (Heraldic Ruins): chest room (Wonder Drug, Fresh Syrup, Ganze Sea Urchin) | 0xa92 | 2706 | Fresh Syrup | 0xff | items |  |
| 713 | Cave of Trials: Level 1 (Heraldic Ruins): chest room (Wonder Drug, Fresh Syrup, Ganze Sea Urchin) | 0xa93 | 2707 | Ganze Sea Urchin | 0x311 | items |  |
| 719 | Cave of Trials: Level 2 (Love Alley): chest room (Purity Leaf) | 0xa69 | 2665 | Purity Leaf | 0x30e | items |  |
| 722 | Cave of Trials: Level 2 (Love Alley): chest room (Fruit Nectar, Slayer's Ring) | 0xa6c | 2668 | Fruit Nectar | 0x1a1 | items |  |
| 722 | Cave of Trials: Level 2 (Love Alley): chest room (Fruit Nectar, Slayer's Ring) | 0xa6d | 2669 | Slayer's Ring | 0x6d | items |  |
| 724 | Cave of Trials: Level 2 (Love Alley): chest room (Fruit Nectar) | 0xa72 | 2674 | Fruit Nectar | 0x1a1 | items |  |
| 730 | Cave of Trials: Level 3 (Single Path Cave): chest room (Sylvan Mail) | 0xa6e | 2670 | Sylvan Mail | 0x2bf | items |  |
| 735 | Cave of Trials: Level 3 (Single Path Cave): chest room (Wonder Drug, Dream Crown, Juicy Beef) | 0xa6f | 2671 | Wonder Drug | 0x104 | items |  |
| 735 | Cave of Trials: Level 3 (Single Path Cave): chest room (Wonder Drug, Dream Crown, Juicy Beef) | 0xa70 | 2672 | Dream Crown | 0xa1 | items |  |
| 735 | Cave of Trials: Level 3 (Single Path Cave): chest room (Wonder Drug, Dream Crown, Juicy Beef) | 0xa71 | 2673 | Juicy Beef | 0x30f | items |  |
| 740 | Cave of Trials: Cave of Trials Level 4 | 0xa73 | 2675 | Portrait F | 0xc | items |  |
| 740 | Cave of Trials: Cave of Trials Level 4 | 0xa74 | 2676 | Fruit Nectar | 0x1a1 | items |  |
| 741 | Cave of Trials: Level 4 (Dancing God's Altar): chest room (Resurrection Mist, Purity Leaf) | 0xa77 | 2679 | Resurrection Mist | 0xf3 | items |  |
| 741 | Cave of Trials: Level 4 (Dancing God's Altar): chest room (Resurrection Mist, Purity Leaf) | 0xa78 | 2680 | Purity Leaf | 0x30e | items |  |
| 744 | Cave of Trials: Cave of Trials Level 5 | 0xa7a | 2682 | Fresh Syrup | 0xff | items |  |
| 744 | Cave of Trials: Cave of Trials Level 5 | 0xa7b | 2683 | Slayer's Ring | 0x6d | items |  |
| 745 | Cave of Trials: Level 5 (Lady's Revenge): chest room (Juicy Beef) | 0xa7d | 2685 | Juicy Beef | 0x30f | items |  |
| 747 | Cave of Trials: Level 5 (Lady's Revenge): chest room (Fairy Ring, Fruit Nectar) | 0xa7e | 2686 | Fairy Ring | 0x7c | items |  |
| 747 | Cave of Trials: Level 5 (Lady's Revenge): chest room (Fairy Ring, Fruit Nectar) | 0xa7f | 2687 | Fruit Nectar | 0x1a1 | items |  |
| 748 | Cave of Trials: Level 5 (Lady's Revenge): chest room (Seraphic Garb) | 0xa80 | 2688 | Seraphic Garb | 0x16c | items |  |
| 762 | Cave of Trials: Level 6 (Burglar's Nest): chest room (Weird Slayer) | 0xa85 | 2693 | Weird Slayer | 0x188 | items |  |
| 763 | Cave of Trials: Cave of Trials Level 7 | 0xa8a | 2698 | 'Ishidaya' Tea | 0x15e | items |  |
| 763 | Cave of Trials: Cave of Trials Level 7 | 0xa8b | 2699 | Yukiyucho Tea | 0x15f | items |  |
| 763 | Cave of Trials: Cave of Trials Level 7 | 0xa8c | 2700 | Bunny Shoes | 0x189 | items |  |
| 767 | Cave of Trials: Level 7 (Goddess's Altar): chest room (Tri-emplem) | 0xa86 | 2694 | Tri-emplem | 0x141 | items |  |
| 769 | Cave of Trials: Level 7 (Goddess's Altar): chest room (Fresh Syrup) | 0xa87 | 2695 | Fresh Syrup | 0xff | items |  |
| 770 | Cave of Trials: Level 7 (Goddess's Altar): chest room (Ganze Sea Urchin, Resurrection Mist) | 0xa88 | 2696 | Ganze Sea Urchin | 0x311 | items |  |
| 770 | Cave of Trials: Level 7 (Goddess's Altar): chest room (Ganze Sea Urchin, Resurrection Mist) | 0xa89 | 2697 | Resurrection Mist | 0xf3 | items |  |
| 771 | Cave of Trials: Level 7 (Goddess's Altar): chest room (Cracked Gem) | 0xa5a | 2650 | Cracked Gem | 0x121 | items |  |
| 773 | Cave of Trials: Level 7 (Goddess's Altar): chest room (Slime Jelly, Soda-Pop, Million Staff) | 0xa5c | 2652 | Slime Jelly | 0x195 | items |  |
| 773 | Cave of Trials: Level 7 (Goddess's Altar): chest room (Slime Jelly, Soda-Pop, Million Staff) | 0xa5d | 2653 | Soda-Pop | 0x16b | items |  |
| 773 | Cave of Trials: Level 7 (Goddess's Altar): chest room (Slime Jelly, Soda-Pop, Million Staff) | 0xa90 | 2704 | Million Staff | 0x16f | items |  |
| 777 | Cave of Trials: Cave of Trials Level 8 | 0xa8d | 2701 | Lavender | 0xe0 | items |  |
| 779 | Cave of Trials: Level 8 (Food God): chest room (Funny Slayer) | 0xa94 | 2708 | Funny Slayer | 0x18c | items |  |
| 780 | Cave of Trials: Level 8 (Food God): chest room (Amoeba Soup, Seraphic Garb) | 0xa5b | 2651 | Amoeba Soup | 0x155 | items |  |
| 780 | Cave of Trials: Level 8 (Food God): chest room (Amoeba Soup, Seraphic Garb) | 0xa95 | 2709 | Seraphic Garb | 0x16c | items |  |
| 782 | Cave of Trials: Level 8 (Food God): chest room (Resurrection Mist) | 0xa99 | 2713 | Resurrection Mist | 0xf3 | items |  |
| 791 | Cave of Trials: Cave of Trials Level 9 | 0xa96 | 2710 | Artemis Leaf | 0xde | items |  |
| 791 | Cave of Trials: Cave of Trials Level 9 | 0xa97 | 2711 | Wonder Drug | 0x104 | items |  |
| 791 | Cave of Trials: Cave of Trials Level 9 | 0xa98 | 2712 | Valiant Boots | 0x2ed | items |  |
| 792 | Cave of Trials: Level 9 (Sealed Coffin): chest room (Bunny Shoes) | 0xaa4 | 2724 | Bunny Shoes | 0x189 | items |  |
| 793 | Cave of Trials: Level 9 (Sealed Coffin): chest room (Funny Slayer) | 0xaa3 | 2723 | Funny Slayer | 0x18c | items |  |
| 797 | Cave of Trials: Level 10 (Decision Point): chest room (Fresh Syrup) | 0xa9a | 2714 | Fresh Syrup | 0xff | items |  |
| 798 | Cave of Trials: Cave of Trials Level 10 | 0xa61 | 2657 | Portrait G | 0x9 | items |  |
| 799 | Cave of Trials: Level 10 (Decision Point): chest room (Million Staff, Portrait J) | 0xaa0 | 2720 | Million Staff | 0x16f | items |  |
| 799 | Cave of Trials: Level 10 (Decision Point): chest room (Million Staff, Portrait J) | 0xaa1 | 2721 | Portrait J | 0xd | items |  |
| 800 | Cave of Trials: Level 10 (Decision Point): chest room (Valiant Boots) | 0xa9e | 2718 | Valiant Boots | 0x2ed | items |  |
| 803 | Cave of Trials: Level 10 (Decision Point): chest room (Valkyrie Boots) | 0xaa2 | 2722 | Valkyrie Boots | 0x2e8 | items |  |
| 805 | Cave of Trials: Level 10 (Decision Point): chest room (Sage's Stone, Valkyrie Boots, Rainbow Diamond) | 0xa9b | 2715 | Sage's Stone | 0x1b2 | items |  |
| 805 | Cave of Trials: Level 10 (Decision Point): chest room (Sage's Stone, Valkyrie Boots, Rainbow Diamond) | 0xa9c | 2716 | Valkyrie Boots | 0x2e8 | items |  |
| 805 | Cave of Trials: Level 10 (Decision Point): chest room (Sage's Stone, Valkyrie Boots, Rainbow Diamond) | 0xa9d | 2717 | Rainbow Diamond | 0x1b4 | items |  |
| 806 | Cave of Trials: Level 10 (Decision Point): chest room (Purity Leaf) | 0xa9f | 2719 | Purity Leaf | 0x30e | items |  |
| 812 | Cave of Trials: Level 12 (Hall of Warriors): chest room (Valkyrie Guard, Battle Suit) | 0xaa7 | 2727 | Valkyrie Guard | 0x2d5 | items |  |
| 812 | Cave of Trials: Level 12 (Hall of Warriors): chest room (Valkyrie Guard, Battle Suit) | 0xaa8 | 2728 | Battle Suit | 0x2b9 | items |  |
| 813 | Cave of Trials: Level 12 (Hall of Warriors): chest room (Battle Suit, Bunny Shoes) | 0xaa5 | 2725 | Battle Suit | 0x2b9 | items |  |
| 813 | Cave of Trials: Level 12 (Hall of Warriors): chest room (Battle Suit, Bunny Shoes) | 0xaa6 | 2726 | Bunny Shoes | 0x189 | items |  |
| 815 | Cave of Trials: Level 13 (Holy Nest of Angels): boss room (Indalecio Celesta) -- final boss | 0xa5e | 2654 | Angel Armband | 0x181 | items |  |
| 818 | Cave of Trials: Level 12 (Hall of Warriors): chest room (Valiant Mail) | 0xaa9 | 2729 | Valiant Mail | 0x2c0 | items |  |
| 822 | Cave of Trials: Level 13 (Holy Nest of Angels): chest room (Valiant Guard) | 0xaaa | 2730 | Valiant Guard | 0x2d4 | items |  |

---

Made by `tools/mod_reference_wiki.py` from the tables in [`docs/modding/reference`](https://github.com/IronJade/Shattered-Stars/blob/main/docs/modding/reference) (`tools/mod_reference.py`). IDs, names and figures only; the game's own text and pictures are in the [local reference](Modding-Reference#the-local-reference) you build from your disc.
