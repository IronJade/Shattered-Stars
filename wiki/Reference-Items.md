# Reference: Items

Every item's number, name and figures, read from the item records (archive entry 3002). The number (decimal `id` or `hex`) is what `so2_mod.py item`, `chest --item`, `shop` and the enemy fields `drop1` / `drop2` take, and what a patch line `item ID FIELD EXPECTED VALUE` holds; `so2_mod.py` takes the name too. Two items that share a name need their number.

`price` is what a shop asks; a shop pays `sell %` of it. `effect` is what a usable item does (the record's effect kind in brackets: the value of the `effect` field), `amount` its amount (a percentage for the restoring kinds). `type` is the value of the `type` field.

All 1023 items in one file: [`items.csv`](https://github.com/IronJade/Shattered-Stars/blob/main/docs/modding/reference/items.csv).

| Page | Items |
|---|---|
| [Weapons](Reference-Items-Weapons) | 177 |
| [Armor, shields, headgear and footwear](Reference-Items-Armor) | 106 |
| [Accessories](Reference-Items-Accessories) | 131 |
| [Usable items](Reference-Items-Usable) | 318 |
| [Other items (ingredients, materials, valuables, unused)](Reference-Items-Other) | 291 |

---

Made by `port/tools/mod_reference_wiki.py` from the tables in [`docs/modding/reference`](https://github.com/IronJade/Shattered-Stars/blob/main/docs/modding/reference) (`port/tools/mod_reference.py`). IDs, names and figures only; the game's own text and pictures are in the [local reference](Modding-Reference#the-local-reference) you build from your disc.
