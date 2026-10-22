"""Authored PS2-layout collision checks; no original game fixture is stored."""
import struct
import sys
import unittest
from pathlib import Path
from types import SimpleNamespace

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from ps2_export import collision
from vehicle_source_collision import read_vehicle_collision


class Chunk:
    def __init__(self):
        self.data = bytearray(1024)
        struct.pack_into("<4f", self.data, 128, 1, 2, 3, 4)
        struct.pack_into("<4f", self.data, 144, 0, 0, 0, 987)
        struct.pack_into("<4f", self.data, 160, 6, 7, 8, 876)
        struct.pack_into("<3h", self.data, 176, 1, 0, 0)
        struct.pack_into("<6I", self.data, 188, 512, 0, 0, 0, 0, 0)
        struct.pack_into("<4f2B", self.data, 512, 1, 2, 3, 0.5, 2, 7)

    def span(self, offset, size):
        if offset < 0 or offset + size > len(self.data):
            raise ValueError("Authored chunk access outside bounds")
        return bytes(self.data[offset:offset + size])

    def read(self, fmt, offset):
        return struct.unpack(fmt, self.span(offset, struct.calcsize(fmt)))

    def read_array(self, fmt, offset, count):
        size = struct.calcsize(fmt)
        return [self.read(fmt, offset + size * i) for i in range(count)]


def source():
    chunk = Chunk()
    return SimpleNamespace(chunk=chunk, models={7: {"name": "testheli", "colPointer": 128}}, collisions={7: (chunk, 128)})


class VehicleSourceCollisionTests(unittest.TestCase):
    def test_bounds_ignore_ps2_vector_padding_and_keep_source_broad_phase(self):
        record = read_vehicle_collision(source(), 7, "testheli")
        self.assertEqual(struct.unpack_from("<10f", record, 32), (4, 1, 2, 3, 0, 0, 0, 6, 7, 8))

    def test_surface_mapping_is_left_to_the_vehicle_converter(self):
        game = source()
        raw = read_vehicle_collision(game, 7, "testheli")
        mapped = collision(game, 7, version=1)
        self.assertEqual(raw[92:94], bytes((2, 7)))
        self.assertEqual(mapped[92:94], bytes((9, 7)))

    def test_null_pointer_has_no_invented_collision_and_wrong_identity_is_refused(self):
        game = source()
        game.models[7]["colPointer"] = 0
        self.assertIsNone(read_vehicle_collision(game, 7, "testheli"))
        with self.assertRaisesRegex(ValueError, "name do not match"):
            read_vehicle_collision(game, 7, "anothermodel")


if __name__ == "__main__":
    unittest.main()
