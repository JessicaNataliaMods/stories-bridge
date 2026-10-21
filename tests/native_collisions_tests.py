"""Native prop dependency regression, with synthetic records only (no game media)."""
import importlib.util
import pathlib
import struct
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('native_collisions', pathlib.Path(__file__).resolve().parents[1] / 'tools/native_collisions.py')
native = importlib.util.module_from_spec(spec)
spec.loader.exec_module(native)


def col(name, body=1):
    return b'COL3' + struct.pack('<I', 104 - 8) + name.encode().ljust(22, b'\0') + b'\0\0' + bytes([body]) * 72


def img(path, data):
    directory = b'VER2' + struct.pack('<I', 1) + struct.pack('<IHH24s', 1, 1, 0, b'dynamic.col')
    path.write_bytes(directory.ljust(2048, b'\0') + data.ljust(2048, b'\0'))


class CollisionDependencies(unittest.TestCase):
    def test_mount_preserves_money_and_props_without_sa_map_records(self):
        with tempfile.TemporaryDirectory(dir=pathlib.Path(__file__).resolve().parent) as temporary:
            root = pathlib.Path(temporary)
            game, package = root / 'game', root / 'package'
            (game / 'models').mkdir(parents=True)
            (package / 'models/lcs').mkdir(parents=True)
            (package / 'data').mkdir()
            (package / 'metadata').mkdir()
            original = col('Money') + col('phonebooth1', 2) + col('removed_sa_building', 3)
            img(game / 'models/gta3.img', original)
            img(game / 'models/gta_int.img', col('removed_interior', 4))
            profile = package / 'data/gta.dat'
            profile.write_text('IDE DATA\\MAPS\\GENERIC\\DYNAMIC.IDE\nCOLFILE 0 MODELS\\LCS\\WORLD.COL\nIPL DATA\\LCS\\WORLD0.IPL\n')
            defs = [('objs', ['1212', 'Money', 'dyn_cash']), ('objs', ['1216', 'phonebooth1', 'dynphn'])]
            with self.assertRaisesRegex(ValueError, 'Money 1212'):
                native.validate(package, game, defs)
            native.mount(package, game, defs)
            self.assertEqual((package / native.RESOURCE).read_bytes(), col('Money') + col('phonebooth1', 2))
            self.assertEqual(native.validate(package, game, defs)['records'], 2)
            native.mount(package, game, defs)
            self.assertEqual(profile.read_text().count(native.DIRECTIVE), 1)
            (package / native.RESOURCE).write_bytes(col('Money', 9) + col('phonebooth1', 2))
            with self.assertRaisesRegex(ValueError, 'differs from original'):
                native.validate(package, game, defs)

    def test_rejects_truncation_and_out_of_bounds(self):
        for data in (b'COL3', col('Money')[:-1], col('Money')[:4] + struct.pack('<I', 9999) + col('Money')[8:]):
            with self.assertRaises(ValueError):
                list(native.records(data))

    def test_rejects_conflicting_original_records(self):
        with tempfile.TemporaryDirectory(dir=pathlib.Path(__file__).resolve().parent) as temporary:
            game = pathlib.Path(temporary)
            (game / 'models').mkdir()
            img(game / 'models/gta3.img', col('Money'))
            img(game / 'models/gta_int.img', col('Money', 2))
            with self.assertRaisesRegex(ValueError, 'Ambiguous'):
                native.source_records(game, [('objs', ['1212', 'Money', 'dyn_cash'])])


if __name__ == '__main__':
    unittest.main()
