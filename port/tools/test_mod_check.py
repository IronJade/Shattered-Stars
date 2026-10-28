#!/usr/bin/env python3
"""Tests of port/tools/mod_check.py (and port/tools/mod_index.py): python3 port/tools/test_mod_check.py

No discs needed: packages are written into a temporary folder (MOD_CHECK_TMP, else the
system's) and checked against the repository's tables.
"""

import contextlib
import io
import json
import os
import sys
import tempfile
import unittest
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mod_check  # noqa: E402
import mod_index  # noqa: E402

TABLES = mod_check.Tables()
VERSION = mod_check.port_version()
ALEN_TAX = TABLES.enemies[119]  # a boss (encounter 191)


def manifest(**over):
    m = {"format": 1, "id": "test-mod", "name": "Test mod", "version": "1.0.0", "author": "Tester",
         "description": "A test.", "min_port_version": "0.01.00", "patches": ["a.patch"]}
    m.update(over)
    return {k: v for k, v in m.items() if v is not None}


class Base(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(dir=os.environ.get("MOD_CHECK_TMP"))
        self.dir = self.tmp.name

    def tearDown(self):
        self.tmp.cleanup()

    def package(self, files, name="test-mod", man=None):
        folder = os.path.join(self.dir, name)
        os.makedirs(folder, exist_ok=True)
        if man is not False:
            with open(os.path.join(folder, "mod.json"), "w") as f:
                json.dump(man if man is not None else manifest(), f)
        for fname, text in files.items():
            mode = "wb" if isinstance(text, bytes) else "w"
            with open(os.path.join(folder, fname), mode) as f:
                f.write(text)
        return folder

    def check(self, files, man=None, in_registry=False, name="test-mod"):
        return mod_check.check_package(self.package(files, name, man), TABLES, VERSION, in_registry)

    def rules(self, result, level):
        return [f.rule for f in result.findings if f.level == level]

    def assert_only(self, result, errors=(), warnings=(), declared=()):
        self.assertEqual(sorted(self.rules(result, "error")), sorted(errors), result.findings)
        self.assertEqual(sorted(self.rules(result, "warning")), sorted(warnings), result.findings)
        self.assertEqual(sorted(self.rules(result, "declared")), sorted(declared), result.findings)


class Samples(Base):
    def test_registry_samples_pass(self):
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            self.assertEqual(mod_check.main(["--all"]), 0)
        self.assertIn("arlia-crate-fol", out.getvalue())
        self.assertIn("hearn-store-plus", out.getvalue())
        self.assertNotIn("FAIL", out.getvalue())

    def test_a_good_chest_and_shop_package(self):
        r = self.check({"a.patch": "# both versions of Arlia's square\n"
                                   "script 18 0x82c 0xffffff38 0xfffffe0c\nscript 21 0x297 0xffffff38 0xfffffe0c\n"
                                   "shop 1 7 0 0x115\nshop 1 0 0x2d8 0x15\n"})
        self.assert_only(r)
        self.assertEqual(len(r.lines), 4)


class Grammar(Base):
    def one(self, line):
        return self.check({"a.patch": line + "\n"})

    def test_only_the_four_commands(self):
        r = self.one("door 18 1 2 3")
        self.assert_only(r, errors=["grammar"])
        self.assertIn("unknown command", r.findings[0].message)

    def test_counts_numbers_and_fields(self):
        for bad, why in (("script 18 0x10 1", "four values"), ("script 18 0x10 1 2 3", "four values"),
                         ("item 0 price 1 2", "1-1023"), ("item 5 price 010 2", "octal"),
                         ("shop 1 0x100 1 2", "below 256"), ("item 5 0x2f:2 1 2", "past the end"),
                         ("enemy 4 atk 1 70000", "doesn't fit"), ("item 5 price ten 2", "isn't a number"),
                         ("enemy 4 hp:3 1 2", "")):
            r = self.one(bad)
            self.assertEqual(self.rules(r, "error"), ["grammar"], bad)
            self.assertIn(why, r.findings[0].message, bad)

    def test_engine_spellings(self):
        # Hex, decimal, raw offsets with sizes, comments, CRLF, blank lines.
        r = self.check({"a.patch": "\r\n# c\nenemy 4 0x37 15 20  # drop1_odds by offset\r\nitem 0x115 0:2 3600 2400\n"})
        self.assertEqual(self.rules(r, "error"), [])
        self.assertEqual([line.field for line in r.lines], ["drop1_odds", ""])


class Manifest(Base):
    def test_required_keys_and_forms(self):
        for over, rule in ((dict(id=None), "manifest"), (dict(id="Bad_ID"), "manifest"),
                           (dict(version="1.x"), "manifest"), (dict(format=2), "manifest"),
                           (dict(min_port_version="99.0"), "manifest"), (dict(patches=["sub/a.patch"]), "manifest"),
                           (dict(patches=[]), "manifest"), (dict(tags=["Has Spaces"]), "manifest"),
                           (dict(homepage="http://x"), "manifest"), (dict(description=None), "manifest"),
                           (dict(name="x" * 61), "manifest")):
            r = self.check({"a.patch": "shop 1 7 0 0x115\n"}, man=manifest(**over))
            self.assertIn(rule, self.rules(r, "error"), over)

    def test_broken_json(self):
        folder = self.package({"a.patch": "shop 1 7 0 0x115\n"}, man=False)
        with open(os.path.join(folder, "mod.json"), "w") as f:
            f.write('{"format": 1,\n "id": "x",}\n')
        r = mod_check.check_package(folder, TABLES, VERSION)
        self.assert_only(r, errors=["manifest"])
        self.assertIn("line 2", r.findings[0].message)

    def test_unknown_key_warns(self):
        r = self.check({"a.patch": "shop 1 7 0 0x115\n"}, man=manifest(colour="blue"))
        self.assert_only(r, warnings=["manifest"])

    def test_folder_named_after_the_id_in_the_registry(self):
        r = self.check({"a.patch": "shop 1 7 0 0x115\n"}, in_registry=True, name="other-name")
        self.assert_only(r, errors=["folder-name"])


class Files(Base):
    def test_allowlist(self):
        r = self.check({"a.patch": "shop 1 7 0 0x115\n", "shot.png": b"\x89PNG", "tool.exe": b"MZ",
                        "extra.patch": "shop 1 7 0 0x116\n", "README.md": "hi\n"})
        self.assertEqual(sorted(self.rules(r, "error")), ["files", "files", "files"])

    def test_missing_listed_file_subfolder_and_binary(self):
        r = self.check({"a.patch": "shop 1 7 0 0x115\n"}, man=manifest(patches=["a.patch", "b.patch"]))
        self.assert_only(r, errors=["files"])
        os.makedirs(os.path.join(self.dir, "test-mod", "sub"))
        r = mod_check.check_package(os.path.join(self.dir, "test-mod"), TABLES, VERSION)
        self.assertIn("files", self.rules(r, "error"))
        r = self.check({"a.patch": b"shop 1 7 0 0x115\n\0"})
        self.assertIn("text", self.rules(r, "error"))

    def test_sizes(self):
        r = self.check({"a.patch": "# " + "x" * (300 * 1024) + "\nshop 1 7 0 0x115\n"})
        self.assertIn("size", self.rules(r, "error"))

    def test_zip_packages(self):
        good = os.path.join(self.dir, "good.zip")
        with zipfile.ZipFile(good, "w", zipfile.ZIP_DEFLATED) as z:
            z.writestr("test-mod/mod.json", json.dumps(manifest()))
            z.writestr("test-mod/a.patch", "shop 1 7 0 0x115\n")
        self.assert_only(mod_check.check_package(good, TABLES, VERSION))
        bad = os.path.join(self.dir, "bad.zip")
        with zipfile.ZipFile(bad, "w") as z:
            z.writestr("mod.json", json.dumps(manifest()))
            z.writestr("a.patch", "shop 1 7 0 0x115\n")
            z.writestr("preview.png", b"\x89PNG")
        self.assertIn("files", self.rules(mod_check.check_package(bad, TABLES, VERSION), "error"))
        evil = os.path.join(self.dir, "evil.zip")
        with zipfile.ZipFile(evil, "w") as z:
            z.writestr("../mod.json", "{}")
        self.assertIn("files", self.rules(mod_check.check_package(evil, TABLES, VERSION), "error"))


class DiscData(Base):
    def test_expected_values_come_from_the_disc_tables(self):
        r = self.check({"a.patch": "script 18 0x82c 0xffffff00 0xfffffe0c\nscript 21 0x297 0xffffff38 0xfffffe0c\n"})
        self.assert_only(r, errors=["expected"])
        r = self.check({"a.patch": "shop 1 0 0x15 0x16\n"})
        self.assert_only(r, errors=["expected"])
        r = self.check({"a.patch": "enemy 4 hp 150 300\n"})
        self.assert_only(r, errors=["expected"])
        self.assertIn("200", r.findings[0].message)
        r = self.check({"a.patch": "shop 1 9 0 0x115\n"})  # appends go at the end (7)
        self.assert_only(r, errors=["range"])

    def test_targets_exist(self):
        r = self.check({"a.patch": "script 5000 1 2 3\nshop 250 0 1 2\nenemy 999 hp 1 2\nscript 18 0x9000 1 2\n"})
        self.assertEqual(self.rules(r, "error"), ["range"] * 4)

    def test_same_place_in_one_package(self):
        r = self.check({"a.patch": "shop 1 0 0x2d8 0x15\nshop 1 0 0x2d8 0x16\n"})
        self.assert_only(r, errors=["same-place"])
        r = self.check({"a.patch": "item 0x115 price 3600 3000\nitem 0x115 price 3000 2000\n"})  # builds on it
        self.assertNotIn("same-place", self.rules(r, "error"))


class Risky(Base):
    def test_doors(self):
        # Map 18's second transfer (word 0x1e0) leads to map 26.
        r = self.check({"a.patch": "script 18 0x1e0 0x0400001a 0x04000021\n"})
        self.assert_only(r, warnings=["door-retarget"])
        r = self.check({"a.patch": "script 18 0x1e0 0x0400001a 0x04000fff\n"})
        self.assert_only(r, warnings=["door-retarget", "door-missing-map"])
        r = self.check({"a.patch": "script 18 0x1e0 0x0400001b 0x04000021\n"})
        self.assert_only(r, errors=["expected"])
        r = self.check({"a.patch": "script 18 0x1e0 0x0400001a 0x0500001a\n"})
        self.assert_only(r, errors=["range"])

    def test_chests(self):
        r = self.check({"a.patch": "script 18 0x82c 0xffffff38 0\nscript 21 0x297 0xffffff38 0\n"})
        self.assert_only(r, warnings=["chest-empty", "chest-empty"])
        r = self.check({"a.patch": "script 18 0x82c 0xffffff38 0xfffffe0c\n"})
        self.assert_only(r, warnings=["chest-other-sites"])
        # Cross Cave's chest gives key flag 68 (value 10068).
        r = self.check({"a.patch": "script 547 6560 301999956 0x12000115\n"})
        self.assertIn("key-item", self.rules(r, "warning"))
        # The push instruction itself may not change.
        r = self.check({"a.patch": "script 547 6560 301999956 0x13000115\n"})
        self.assertIn("range", self.rules(r, "error"))
        r = self.check({"a.patch": "script 18 0x10 1 2\n"})
        self.assert_only(r, warnings=["script-unknown"])

    def test_enemies(self):
        hp = ALEN_TAX["hp"]
        r = self.check({"a.patch": f"enemy 119 hp {hp} 0\nenemy 4 hp 200 0\nenemy 4 drop1_odds 15 150\n"
                                   "enemy 4 0x30:2 0 1\n"})
        self.assert_only(r, warnings=["boss-zero", "enemy-zero-hp", "drop-odds", "raw-field"])

    def test_items_and_shops(self):
        r = self.check({"a.patch": "item 0x115 price 3600 0\nitem 0x115 sell 25 150\nitem 0x115 effect 6 7\n"
                                   "item 0x116 price 1 99999999\n"})
        self.assert_only(r, warnings=["price-zero", "sell-over-price", "item-behaviour", "price-high"])
        only = next(i for s, stock in TABLES.shops.items() for i in stock
                    if sum(i in st for st in TABLES.shops.values()) == 1)
        shop = next(s for s, stock in TABLES.shops.items() if only in stock)
        index = TABLES.shops[shop].index(only)
        r = self.check({"a.patch": f"shop {shop} {index} {only:#x} 0\n"})
        self.assert_only(r, warnings=["shop-only-source"])
        r = self.check({"a.patch": "shop 1 7 0 0x3ff\n"})  # item 1023 exists; 0x500 doesn't
        r2 = self.check({"a.patch": "item 0x115 price 3600 3000\nshop 1 7 0 0x500\n"})
        self.assertIn("item-unknown", self.rules(r2, "warning"))

    def test_declarations(self):
        man = manifest(declared_risks=[{"rule": "price-zero", "reason": "Free bottles: the point.",
                                        "lines": ["a.patch:1"]}])
        r = self.check({"a.patch": "item 0x115 price 3600 0\n"}, man=man)
        self.assert_only(r, declared=["price-zero"])
        self.assertEqual(r.findings[0].reason, "Free bottles: the point.")
        # A declaration for another line doesn't cover this one, and is reported unused.
        man = manifest(declared_risks=[{"rule": "price-zero", "reason": "x", "lines": ["a.patch:9"]}])
        r = self.check({"a.patch": "item 0x115 price 3600 0\n"}, man=man)
        self.assert_only(r, warnings=["price-zero", "declared"])
        man = manifest(declared_risks=[{"rule": "no-such-rule", "reason": "x"}])
        r = self.check({"a.patch": "shop 1 7 0 0x115\n"}, man=man)
        self.assert_only(r, errors=["declared"])

    def test_exit_status_and_strict(self):
        folder = self.package({"a.patch": "script 18 0x10 1 2\n"})
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(mod_check.main([folder]), 0)
            self.assertEqual(mod_check.main([folder, "--strict"]), 1)
            bad = self.package({"a.patch": "nonsense\n"}, name="bad")
            self.assertEqual(mod_check.main([bad]), 1)
            out = io.StringIO()
        with contextlib.redirect_stdout(out):
            mod_check.main([folder, "--json"])
        self.assertEqual(json.loads(out.getvalue())[0]["findings"][0]["rule"], "script-unknown")


class Index(Base):
    def test_build_check_and_verification(self):
        reg = os.path.join(self.dir, "mods")
        os.makedirs(reg)
        self.package({"a.patch": "shop 1 7 0 0x115\n"}, name="mods/test-mod")
        index = mod_index.build(reg, previous=None)
        self.assertEqual(len(index["mods"]), 1)
        entry = index["mods"][0]
        self.assertFalse(entry["verified"])
        self.assertEqual(sorted(entry["files"]), ["a.patch", "mod.json"])
        verified = mod_index.mark_verified(index, "test-mod", "IronJade", "0.01.00", "2026-10-23")
        e = verified["mods"][0]
        self.assertTrue(e["verified"])
        self.assertEqual(e["verified_by"], "IronJade")
        # Rebuilt with the same files: still verified. A changed file: unverified again.
        self.assertTrue(mod_index.build(reg, previous=verified)["mods"][0]["verified"])
        with open(os.path.join(reg, "test-mod", "a.patch"), "a") as f:
            f.write("# changed\n")
        again = mod_index.build(reg, previous=verified)["mods"][0]
        self.assertFalse(again["verified"])
        self.assertNotIn("verified_by", again)

    def test_verification_fields_guard(self):
        base = {"mods": [{"id": "a", "verified": False, "files": {}}]}
        head = {"mods": [{"id": "a", "verified": True, "verified_by": "x", "files": {}}]}
        self.assertEqual(mod_index.verification_changes(base, head), ["a"])  # gained one
        self.assertEqual(mod_index.verification_changes(base, base), [])
        self.assertEqual(mod_index.verification_changes(head, base), [])  # dropping one is fine
        self.assertEqual(mod_index.verification_changes(None, head), ["a"])  # a new, verified entry
        moved = {"mods": [{"id": "a", "verified": True, "verified_by": "x", "files": {"a.patch": "1"}}]}
        self.assertEqual(mod_index.verification_changes(head, moved), ["a"])  # files changed under it


if __name__ == "__main__":
    unittest.main()
