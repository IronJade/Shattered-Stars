# Reference: Maps

Every field map's number and our name for it. The number is what `so2_mod.py list`, `chest`, `door` and a door's `--to` take, and what a patch line `script MAP WORD EXPECTED VALUE` holds; `so2_mod.py` takes the name too ("Cross: Skill Guild"). A name is "area: place"; where a map has no description of its own yet, it is the banner the game shows on entering, or its area.

Doors: what `so2_mod.py door` can change ([doors](Reference-Doors)); chests: [chests](Reference-Chests); shops: the shop numbers the map opens ([shops](Reference-Shops)); save: the map's own script places a save point. Maps sharing one script (the world-map chunks) take the same patches, one map at a time.

964 maps (964 with a descriptive name so far) in one file: [`maps.csv`](https://github.com/IronJade/Shattered-Stars/blob/main/docs/modding/reference/maps.csv).

| Page | Maps | Areas |
|---|---|---|
| [Disc 1 (Expel)](Reference-Maps-Disc-1) | 633 | 57 |
| [Disc 2 (Energy Nede)](Reference-Maps-Disc-2) | 322 | 28 |
| [World script and unplaced maps](Reference-Maps-Other) | 9 | 4 |

---

Made by `tools/mod_reference_wiki.py` from the tables in [`docs/modding/reference`](https://github.com/IronJade/Shattered-Stars/blob/main/docs/modding/reference) (`tools/mod_reference.py`). IDs, names and figures only; the game's own text and pictures are in the [local reference](Modding-Reference#the-local-reference) you build from your disc.
