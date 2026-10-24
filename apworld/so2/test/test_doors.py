"""The door-table validator (apworld/so2/doors.py) on a small made-up door table. Runs in
Archipelago's test run (worlds/so2/test) and standalone: python3 apworld/so2/test/test_doors.py
(world agent)."""
import os
import sys
import unittest

try:
    from .. import doors
except ImportError:  # standalone, outside an Archipelago checkout
    sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
    import doors  # type: ignore

# World map 1 -> town 10 (door A), town 10 <-> 11 (B, C), 11 -> world map 1 (D), and a dead
# end 20 entered from 11 (E) with no way out.
TABLE = {"doors": [
    {"id": doors.door_id(909, 0x100), "map": 909, "world": 1, "to": {"map": 10, "x": 5, "y": 0, "z": 5, "dir": 0}},
    {"id": doors.door_id(10, 0x40), "map": 10, "routine": 0x20, "to": {"map": 11, "x": 1, "y": 0, "z": 1, "dir": 0}},
    {"id": doors.door_id(11, 0x40), "map": 11, "to": {"map": 10, "x": 9, "y": 0, "z": 9, "dir": 0}},
    {"id": doors.door_id(11, 0x80), "map": 11, "to": {"map": 1, "x": 100, "y": 0, "z": 100, "dir": 0}},
    {"id": doors.door_id(11, 0xC0), "map": 11, "to": {"map": 20, "x": 0, "y": 0, "z": 0, "dir": 0}},
    {"id": doors.door_id(1, 0x40), "map": 1, "world_script": True, "to": {"map": 11, "x": 1, "y": 0, "z": 1, "dir": 0}},
]}


class TestDoorValidator(unittest.TestCase):
    def test_good_tables(self):
        self.assertEqual(doors.validate_doors([], TABLE), [])
        # Keys only; then the world entrance sent to town 11's arrival point.
        a = doors.door_id(909, 0x100)
        self.assertEqual(doors.validate_doors([{"id": a, "key": 3}], TABLE), [])
        door = {"map": 909, "offset": "0x100", "to": {"map": 11, "x": 1, "y": 0, "z": 1}}
        self.assertEqual(doors.validate_doors([door], TABLE), [])

    def test_problems(self):
        a = doors.door_id(909, 0x100)
        got = doors.validate_doors([{"id": a + 1}], TABLE)
        self.assertIn("not a door of the game", got[0])
        got = doors.validate_doors([{"id": a, "key": 256}], TABLE)
        self.assertIn("key 256", got[0])
        got = doors.validate_doors([{"id": a, "to": {"map": 11, "x": 2, "y": 0, "z": 2}}], TABLE)
        self.assertIn("not an arrival point", got[0])
        got = doors.validate_doors([{"id": a}, {"map": 909, "offset": 0x100}], TABLE)
        self.assertIn("repeated", got[0])
        got = doors.validate_doors([{"id": a, "map": 10, "offset": 0x100}], TABLE)
        self.assertIn("is not map * 65536", got[0])
        got = doors.validate_doors([{"id": doors.door_id(1, 0x40)}], TABLE)
        self.assertIn("world script", got[0])
        # The world entrance sent into the dead end: map 20 never gets back.
        got = doors.validate_doors([{"id": a, "to": {"map": 20, "x": 0, "y": 0, "z": 0}}], TABLE)
        self.assertEqual(got, [f"door {a}: map 20 never gets back to a world map"])
        # Town 11's way out sent back into 10: neither town gets back any more.
        d = doors.door_id(11, 0x80)
        got = doors.validate_doors([{"id": d, "to": {"map": 10, "x": 9, "y": 0, "z": 9}}], TABLE)
        self.assertEqual(got, [f"door {d}: map 10 never gets back to a world map"])


    def test_gate_doors(self):
        """A door that needs several keys (a one-way story point: the El voyage's), named by its
        routine."""
        b = doors.door_id(10, 0x40)
        self.assertEqual(doors.validate_doors([{"id": b, "routine": "0x20", "keys": [17, 18, 19]}], TABLE), [])
        self.assertEqual(doors.validate_doors([{"id": b, "routine": 0x20, "keys": [5]}], TABLE), [])
        got = doors.validate_doors([{"id": b, "keys": [17, 0]}], TABLE)
        self.assertIn("key 0 is not 1-255", got[0])
        got = doors.validate_doors([{"id": b, "keys": []}], TABLE)
        self.assertIn("keys must be a list", got[0])
        got = doors.validate_doors([{"id": b, "routine": "0x24", "keys": [3]}], TABLE)
        self.assertIn("routine 0x24", got[0])
        got = doors.validate_doors([{"id": doors.door_id(909, 0x100), "routine": 0, "key": 3}], TABLE)
        self.assertIn("routine 0", got[0])
        self.assertEqual(doors.validate_doors([{"id": b, "keys": [5], "until": 38}], TABLE), [])
        got = doors.validate_doors([{"id": b, "keys": [5], "until": 0}], TABLE)
        self.assertIn("until 0", got[0])


if __name__ == "__main__":
    unittest.main()
