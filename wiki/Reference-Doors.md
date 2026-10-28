# Reference: Doors

Every door `so2_mod.py door` can send somewhere else: a map's map transfers written as one inline instruction (map_transfer_c), numbered from 0 as `so2_mod.py list` numbers them. To change one:

```
python3 tools/so2_mod.py door DISC.cue MAP INDEX --to NEW_MAP [--x X --y Y --z Z --dir D]
```

`MAP` and `NEW_MAP` take a number or a name: `door DISC.cue 18 1 --to "Cross: Skill Guild"`. Without `--x --y --z --dir` the party arrives where the door's old target put it, which on another map may be inside a wall: copy the arrival point of a door that already leads to the new map (its row below). `mode` is the transition.

2441 doors on 806 maps in one file: [`doors.csv`](https://github.com/IronJade/Shattered-Stars/blob/main/docs/modding/reference/doors.csv).

| Page | Doors | Areas |
|---|---|---|
| [Maps 1-153](Reference-Doors-1-153) | 448 | Expel, Arlia Village, Planet Milocinia, Shingo Forest, Lacour Front Line, Field of Power and 21 more |
| [Maps 154-299](Reference-Doors-154-299) | 448 | Town of Herlie, Zand's Mansion, Port Hilton, Inn Hilton Island, Skill Guild Skill Power, Tavern Stone and 18 more |
| [Maps 300-473](Reference-Doors-300-473) | 432 | Fun City Fighting Arena, Fun City, Town of Armlock, Transporter Portal of Sealing, Giveaway, Mountain Palace and 21 more |
| [Maps 474-614](Reference-Doors-474-614) | 450 | Lacour Castle, Hoffmann Ruins, Cross Cave, Ship to El, Field of Intelligence, Eluria Tower and 4 more |
| [Maps 615-765](Reference-Doors-615-765) | 449 | Field of Power, The Outer Wall, disc-swap trigger, Eluria Tower, Field of Intelligence, Field of Courage and 7 more |
| [Maps 766-1048](Reference-Doors-766-1048) | 214 | Cave of Trials, Expel, Energy Nede |

---

Made by `tools/mod_reference_wiki.py` from the tables in [`docs/modding/reference`](https://github.com/IronJade/Shattered-Stars/blob/main/docs/modding/reference) (`tools/mod_reference.py`). IDs, names and figures only; the game's own text and pictures are in the [local reference](Modding-Reference#the-local-reference) you build from your disc.
