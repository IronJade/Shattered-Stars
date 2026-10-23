#!/usr/bin/env python3
"""Tests of the name lookup (tools/so2_names.py, so2_mod.py's arguments) and the reference
table writers (tools/mod_reference.py): python3 tools/test_mod_reference.py

No discs needed: names come from the repository's tables, and the map tests use their own
small map_names.csv in a temporary folder (MOD_REF_TMP, else the system's). With SS_DISC1 set
to disc 1's .cue, one more test builds the tables from the disc and compares them with docs/ap.
"""

import contextlib
import io
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mod_reference  # noqa: E402
import so2_data  # noqa: E402
import so2_mod  # noqa: E402
import so2_names  # noqa: E402

TMP = os.environ.get("MOD_REF_TMP") or None
NO_CSV = os.path.join(os.path.dirname(os.path.abspath(__file__)), "no-such-map-names.csv")


def tables_with(rows=None):
    """Tables with a map_names.csv of these rows (map, disc, area, place, kind), or none."""
    if rows is None:
        return so2_names.Tables(map_csv=NO_CSV)
    d = tempfile.mkdtemp(dir=TMP)
    path = os.path.join(d, "map_names.csv")
    with open(path, "w", encoding="utf-8") as f:
        f.write("map,disc,area,place,kind,notes,basis\n")
        for r in rows:
            f.write(",".join(f'"{x}"' if "," in str(x) else str(x) for x in r) + ",,\n")
    return so2_names.Tables(map_csv=path)


BANNERS = tables_with(None)
CSV = tables_with([(18, 1, "Arlia Village", "north: fountain and Hearn's", "town"),
                   (17, 1, "Arlia Village", "south: bridge", "town"),
                   (80, 1, "Cross", "Skill Guild", "shop"),
                   (147, 1, "Herlie", "Skill Guild", "shop")])


class ItemNames(unittest.TestCase):
    def test_numbers_pass_through(self):
        self.assertEqual(BANNERS.resolve("item", "0x2a5"), 0x2A5)
        self.assertEqual(BANNERS.resolve("item", "677"), 677)
        self.assertEqual(BANNERS.resolve("map", "26"), 26)

    def test_case_and_punctuation(self):
        self.assertEqual(BANNERS.resolve("item", "leather ARMOR"), 0x2A5)
        self.assertEqual(BANNERS.resolve("item", "Resurrection-Bottle"), 0x115)

    def test_plural(self):
        self.assertEqual(BANNERS.resolve("item", "Blueberries"), 0x2F5)

    def test_shared_name_is_ambiguous(self):
        with self.assertRaises(so2_names.Ambiguous) as e:
            BANNERS.resolve("item", "Fanzine")
        self.assertEqual(len(e.exception.candidates), 2)
        self.assertTrue(any(c.startswith("0xbe ") for c in e.exception.candidates))
        self.assertTrue(any(c.startswith("0xc2 ") for c in e.exception.candidates))
        self.assertEqual(BANNERS.resolve("item", "Fanzine (194)"), 194)

    def test_misspelling_suggests(self):
        with self.assertRaises(so2_names.NotFound) as e:
            BANNERS.resolve("item", "Bluebery")
        self.assertEqual(e.exception.candidates[0], "0x2f5 Blueberry")

    def test_part_of_a_name(self):
        self.assertEqual(BANNERS.resolve("item", "Resurrection Bott"), 0x115)
        with self.assertRaises(so2_names.Ambiguous):
            BANNERS.resolve("item", "berry")


class MapNames(unittest.TestCase):
    def test_banner_fallback(self):
        self.assertEqual(BANNERS.map_name(26), "Arlia Village: General Store Hearn's of Arlia")
        self.assertEqual(BANNERS.resolve("map", "General Store Hearn's"), 26)
        self.assertEqual(BANNERS.resolve("map", "Cross: Skill Guild"), 80)
        self.assertEqual(BANNERS.map_name(1), "Expel world map")
        self.assertTrue(BANNERS.map_name(915).startswith("Expel world map"))
        self.assertEqual(BANNERS.map_info(26)["source"], "banner")

    def test_area_alone_is_ambiguous(self):
        with self.assertRaises(so2_names.Ambiguous) as e:
            BANNERS.resolve("map", "Arlia Village")
        ids = {int(c.split()[0]) for c in e.exception.candidates}
        self.assertTrue({17, 18} <= ids)

    def test_csv_names_win(self):
        self.assertEqual(CSV.map_name(18), "Arlia Village: north: fountain and Hearn's")
        self.assertEqual(CSV.map_info(18)["source"], "map_names.csv")
        self.assertEqual(CSV.map_info(18)["kind"], "town")
        self.assertEqual(CSV.map_name(26), "Arlia Village: General Store Hearn's of Arlia")  # not in the csv
        self.assertEqual(CSV.resolve("map", "Cross: Skill Guild"), 80)
        self.assertEqual(CSV.resolve("map", "arlia village: north"), 18)
        self.assertEqual(CSV.resolve("map", "Arlia Village: south: bridge"), 17)
        with self.assertRaises(so2_names.Ambiguous) as e:
            CSV.resolve("map", "Skill Guild")
        self.assertTrue({80, 147} <= {int(c.split()[0]) for c in e.exception.candidates})

    def test_misspelt_map(self):
        with self.assertRaises(so2_names.NotFound) as e:
            BANNERS.resolve("map", "Hearns Genral Store")
        self.assertTrue(any(c.startswith("26 ") for c in e.exception.candidates))


class ShopsAndEnemies(unittest.TestCase):
    def test_shop_by_place(self):
        self.assertEqual(BANNERS.resolve("shop", "Hearn's"), 1)
        self.assertEqual(BANNERS.resolve("shop", "shop 7"), 7)
        self.assertIn("map 26", BANNERS.shop_name(1))

    def test_enemy(self):
        self.assertEqual(BANNERS.resolve("enemy", "armedknight"), 1)
        with self.assertRaises(so2_names.Ambiguous) as e:
            BANNERS.resolve("enemy", "Berle")
        self.assertEqual(len(e.exception.candidates), 2)
        with self.assertRaises(so2_names.NotFound) as e:
            BANNERS.resolve("enemy", "Armed Knigt")
        self.assertEqual(e.exception.candidates[0], "1 Armedknight")

    def test_find(self):
        hits = BANNERS.find("skill guild")
        self.assertIn(("map", 80), [h[:2] for h in hits])
        hits = BANNERS.find("blueberry")
        self.assertIn(("item", 0x2F5), [h[:2] for h in hits])
        self.assertTrue(BANNERS.find("Bluebery"))   # close spellings when nothing contains it


class Commands(unittest.TestCase):
    def test_resolve_exits_with_candidates(self):
        err = io.StringIO()
        with contextlib.redirect_stderr(err), self.assertRaises(SystemExit) as e:
            so2_mod.resolve("item", "Fanzine")
        self.assertEqual(e.exception.code, 2)
        self.assertIn("0xbe Fanzine", err.getvalue())
        self.assertEqual(so2_mod.resolve("item", "0x115"), 0x115)

    def test_describe_value(self):
        self.assertEqual(so2_mod.describe_value(-200), "200 Fol")
        self.assertEqual(so2_mod.describe_value(0x2A5), "item 0x2a5 Leather Armor")

    def test_words(self):
        self.assertEqual(so2_data.item_type_name(8), "armor")
        self.assertEqual(so2_data.item_effect_name(1), "restores HP (%)")
        self.assertEqual(so2_data.item_effect_name(40), "special effect 40")
        self.assertEqual(so2_data.item_use_name(3), "battle, field")
        self.assertEqual(so2_data.equip_names(0x10), "Dias")
        self.assertEqual(so2_data.equip_names(0xFFF), "all")


class Writers(unittest.TestCase):
    def test_ranges(self):
        self.assertEqual(mod_reference.ranges([7, 1, 2, 3, 9, 10]), "1-3, 7, 9-10")
        self.assertEqual(mod_reference.ranges([]), "")

    def test_csv_is_stable(self):
        d = tempfile.mkdtemp(dir=TMP)
        path = os.path.join(d, "t.csv")
        cols = ["map", "name", "note"]
        rows = [{"map": 18, "name": "Arlia Village: north", "note": "a, b"}, {"map": 26, "name": 'Hearn\'s "store"'}]
        self.assertTrue(mod_reference.write_csv(path, cols, rows))
        self.assertFalse(mod_reference.write_csv(path, cols, rows))   # same text: not rewritten
        with open(path, "rb") as f:
            raw = f.read()
        self.assertNotIn(b"\r", raw)
        self.assertTrue(raw.startswith(b"map,name,note\n"))
        back = mod_reference.read_csv(path)
        self.assertEqual(back[0]["note"], "a, b")
        self.assertEqual(back[1]["name"], 'Hearn\'s "store"')
        self.assertEqual(back[1]["note"], "")

    def test_columns(self):
        for name in ("items", "maps", "doors", "chests", "shops", "enemies"):
            self.assertIn(name, mod_reference.COLUMNS)
        self.assertEqual(mod_reference.COLUMNS["doors"][-1], "command")
        self.assertEqual(mod_reference.COLUMNS["chests"][-1], "command")

    def test_write_all(self):
        d = tempfile.mkdtemp(dir=TMP)
        ref = {"tables": {k: [] for k in mod_reference.COLUMNS}}
        ref["tables"]["items"] = [{"id": 1, "hex": "0x1", "name": "Magic Canvas", "category": "item"}]
        mod_reference.write_all(ref, d, log=lambda *_: None)
        self.assertEqual(sorted(os.listdir(d)), sorted([f"{k}.csv" for k in mod_reference.COLUMNS] + ["README.md"]))
        with open(os.path.join(d, "README.md"), encoding="utf-8") as f:
            self.assertIn("| `items.csv` | 1 |", f.read())


@unittest.skipUnless(os.environ.get("SS_DISC1"), "SS_DISC1 (disc 1's .cue) not set")
class FromDisc(unittest.TestCase):
    def test_tables_match_docs_ap(self):
        disc = os.environ["SS_DISC1"]
        if os.path.isdir(disc):
            disc = next(os.path.join(disc, f) for f in sorted(os.listdir(disc)) if f.lower().endswith(".cue"))
        ref = mod_reference.collect(disc, BANNERS, log=lambda *_: None)
        for ok, text in mod_reference.check(ref, BANNERS):
            self.assertTrue(ok, text)
        d = so2_data.Disc(disc)
        desc = so2_data.item_descriptions(d)
        d.f.close()
        self.assertEqual(len(desc), 1023)
        self.assertFalse([i for i, v in desc.items() if "\ufffd" in v])      # every glyph read
        self.assertGreater(sum(1 for v in desc.values() if v), 1000)        # unused items have none


if __name__ == "__main__":
    unittest.main()
